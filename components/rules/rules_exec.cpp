// ============================================================================
// Execution des actions : tache "rules", file cadencee des actions radio
// (portage de actionPacer.cpp), notifications et executeur des commandes de
// template (portage de SendAction / SendActionEx de zigbee.cpp).
//
// Pourquoi une tache dediee : les rapports d'attributs arrivent dans la tache de
// traitement ZiGate, qui ne doit jamais attendre. rules_on_attribute() ne fait que
// poster un evenement ; l'evaluation, le cadencement (attente de l'accuse avant
// l'action suivante) et l'envoi des trames ont lieu ici.
//
// Pile en PSRAM (8 Ko, la RAM interne est rare) : cette tache n'accede donc
// JAMAIS a la flash. Les fichiers (rules.json, statusRules.json) sont ecrits par
// rules_tick() et les routes web, dans leur propre tache.
// ============================================================================

#include "rules_internal.h"
#include "action_groups.h"
#include "zigate_driver.h"
#include "zigate_devices.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "RULES_EXEC";

// Mettre a 1 (option de compilation -DRULES_TASK_STACK_INTERNAL=1) si le crochet de
// notification cable par l'integrateur doit ecrire en flash depuis cette tache :
// la pile passe alors en RAM interne (6 Ko).
#ifndef RULES_TASK_STACK_INTERNAL
#define RULES_TASK_STACK_INTERNAL 0
#endif

#define RULES_TASK_STACK_PSRAM   8192
#define RULES_TASK_STACK_IRAM    6144
#define RULES_TASK_PRIO          5
#define RULES_EVT_QUEUE_LEN      32
#define RULES_NOTIF_MAX          8

// Commandes de template (codes de la LiXee-Box)
#define CMD_ONOFF                146
#define CMD_COVER                250
#define CMD_COVER_POSITION       251
#define CMD_COVER_POSITION_INV   252
#define CMD_TUYA_BOOL            300
#define CMD_TUYA_VALUE           301
#define CMD_TUYA_ENUM            302
#define CMD_TUYA_RAW             303
#define CMD_HVAC_MODE            310
#define CMD_HVAC_SETPOINT_BOTH   311
#define CMD_HVAC_FAN             312
#define CMD_HVAC_LOUVER          313
#define CMD_HVAC_COOL_SETPOINT   314
#define CMD_HVAC_HEAT_SETPOINT   315
#define CMD_CLUSTER_SPECIFIC     400

typedef struct {
    rule_action_t act;
    char          origin[RULES_ORIGIN_LEN];
    int64_t       queued_ms;
    uint8_t       busy_retries;
} rules_job_t;

typedef struct {
    char title[RULES_TITLE_LEN];
    char msg[RULES_NOTIF_LEN];
} rules_notif_t;

static TaskHandle_t       s_task;
static QueueHandle_t      s_evt_q;

// File des actions radio : anneau en PSRAM, protege par mutex (copie de ~600 octets
// par element, trop long pour une section critique).
static SemaphoreHandle_t  s_job_lock;
static rules_job_t       *s_jobs;
static int                s_job_head;
static int                s_job_count;

static SemaphoreHandle_t  s_notif_lock;
static rules_notif_t     *s_notifs;
static int                s_notif_head;
static int                s_notif_count;

static rules_notify_cb_t  s_notify_cb;
static rules_command_fn_t s_cmd_fn;

// Attente de l'issue de l'action en cours, partagee avec les crochets ZiGate.
static portMUX_TYPE s_ack_mux = portMUX_INITIALIZER_UNLOCKED;
static struct {
    bool     waiting;
    uint16_t target;       // adresse courte attendue
    uint16_t zcmd;         // commande ZiGate emise (0 : inconnue)
    bool     done;         // issue connue (accuse, echec, refus)
    uint8_t  result;
    uint16_t result_msg;
    bool     routing;      // D4 recu : recherche de route en cours
    int64_t  route_ms;
} s_w;
static volatile bool s_ack_feedback;   // un retour radio a deja ete recu : crochets cables
static volatile bool s_slot_freed;     // un accuse quelconque a libere une place ZiGate

// Etat propre a la tache "rules"
static int64_t      s_start_ms;
static int64_t      s_pause_until;
static rules_job_t *s_cur;             // derniere action lancee (remise en file si refus)
static bool         s_has_cur;

// ----------------------------------------------------------------------------
// Crochets
// ----------------------------------------------------------------------------

void rules_set_notify_cb(rules_notify_cb_t cb)       { s_notify_cb = cb; }
void rules_set_command_executor(rules_command_fn_t fn) { s_cmd_fn = fn; }

void rules_wake_task(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}

bool rules_post_event(const rules_evt_t *e)
{
    if (!s_evt_q) return false;
    if (xQueueSend(s_evt_q, e, 0) != pdTRUE) {
        ESP_LOGW(TAG, "File d'evenements pleine, evenement ignore");
        return false;
    }
    rules_wake_task();
    return true;
}

// Statuts 0x8000 signifiant "pas de place pour l'instant" et non un echec de
// l'action : 0x04 ZiGate occupee, 0x14 plus de tampon ZCL, 0x15 table d'accuses pleine.
static bool is_busy_status(uint8_t s)
{
    return s == 0x04 || s == 0x14 || s == 0x15;
}

void rules_on_zigate_status(uint8_t status, uint16_t packet_type)
{
    if (status == 0x00) return;
    bool hit = false;
    portENTER_CRITICAL(&s_ack_mux);
    // Correction : un opcode inconnu (0) acceptait N'IMPORTE QUEL statut 0x8000
    // comme accuse de l'action (une lecture du poll_engine en echec terminait
    // l'action en cours, voire la remettait en file comme "ZiGate saturee").
    // Sans opcode, les statuts sont ignores : l'issue vient des retours radio
    // (0x8011 / 0x8702 / 0x8701) ou du delai maximal.
    if (s_w.waiting && !s_w.done && s_w.zcmd != 0 && packet_type == s_w.zcmd) {
        s_w.done = true;
        s_w.result = status;
        s_w.result_msg = 0x8000;
        hit = true;
    }
    portEXIT_CRITICAL(&s_ack_mux);
    if (hit) rules_wake_task();
}

void rules_on_zigate_radio_result(uint16_t msg_type, uint16_t short_addr, uint8_t status)
{
    s_ack_feedback = true;
    // Tout accuse ou echec libere une place dans la table d'accuses de la ZiGate.
    if (msg_type == 0x8011 || msg_type == 0x8702) s_slot_freed = true;
    bool hit = false;
    portENTER_CRITICAL(&s_ack_mux);
    if (s_w.waiting && !s_w.done && short_addr == s_w.target) {
        if (msg_type == 0x8702 && status == 0xD4) {
            // D4 : pas de route, la ZiGate garde la trame et cherche une route ;
            // l'issue reelle viendra ensuite (0x8011 ou 0x8701).
            if (!s_w.routing) {
                s_w.routing = true;
                s_w.route_ms = rules_now_ms();
            }
        } else if (msg_type == 0x8011 || msg_type == 0x8702 || msg_type == 0x8701 ||
                   (msg_type == 0x8012 && status != 0x00)) {
            s_w.done = true;
            s_w.result = status;
            s_w.result_msg = msg_type;
            hit = true;
        }
    }
    portEXIT_CRITICAL(&s_ack_mux);
    if (hit || s_slot_freed) rules_wake_task();
}

// ----------------------------------------------------------------------------
// Notifications
// ----------------------------------------------------------------------------

void rules_notify_post(const char *title, const char *msg)
{
    if (!s_notifs || !s_notif_lock) return;
    if (xSemaphoreTake(s_notif_lock, pdMS_TO_TICKS(50)) != pdTRUE) return;
    if (s_notif_count >= RULES_NOTIF_MAX) {
        xSemaphoreGive(s_notif_lock);
        ESP_LOGW(TAG, "Notification ignoree (file pleine) : %s", title ? title : "");
        return;
    }
    rules_notif_t *n = &s_notifs[(s_notif_head + s_notif_count) % RULES_NOTIF_MAX];
    jcopy(n->title, sizeof(n->title), title);
    jcopy(n->msg, sizeof(n->msg), msg);
    s_notif_count++;
    xSemaphoreGive(s_notif_lock);
    rules_wake_task();
}

static void drain_notifications(void)
{
    // Copie locale (pile en PSRAM, 8 Ko) : le crochet est appele hors verrou
    static rules_notif_t *one = NULL;
    if (!one) one = (rules_notif_t *)rules_psram_calloc(1, sizeof(rules_notif_t));
    if (!one) return;
    for (;;) {
        if (xSemaphoreTake(s_notif_lock, pdMS_TO_TICKS(50)) != pdTRUE) return;
        if (s_notif_count == 0) {
            xSemaphoreGive(s_notif_lock);
            return;
        }
        *one = s_notifs[s_notif_head];
        s_notif_head = (s_notif_head + 1) % RULES_NOTIF_MAX;
        s_notif_count--;
        xSemaphoreGive(s_notif_lock);

        ESP_LOGI(TAG, "Notification : %s", one->title);
        if (s_notify_cb) s_notify_cb(one->title, one->msg, 0);
    }
}

// ----------------------------------------------------------------------------
// File des actions radio
// ----------------------------------------------------------------------------

static bool same_action(const rule_action_t *a, const rule_action_t *b)
{
    return strcmp(a->type, b->type) == 0 && strcmp(a->IEEE, b->IEEE) == 0 &&
           strcmp(a->actionName, b->actionName) == 0 && a->endpoint == b->endpoint &&
           a->command == b->command && strcmp(a->value, b->value) == 0 &&
           strcmp(a->sourceIEEE, b->sourceIEEE) == 0;
}

bool rules_job_enqueue(const rule_action_t *act, const char *origin)
{
    if (!s_jobs || !s_job_lock) return false;
    if (xSemaphoreTake(s_job_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "[Cadence] file occupee, action ignoree : %s -> %s", act->IEEE, act->actionName);
        return false;
    }
    for (int i = 0; i < s_job_count; i++) {
        if (same_action(&s_jobs[(s_job_head + i) % RULES_JOB_MAX].act, act)) {
            xSemaphoreGive(s_job_lock);
            // Deja en attente (regle en mode repetition, double clic sur un groupe)
            ESP_LOGW(TAG, "[Cadence] action deja en file, doublon ignore : %s -> %s",
                     act->IEEE, act->actionName);
            return false;
        }
    }
    if (s_job_count >= RULES_JOB_MAX) {
        xSemaphoreGive(s_job_lock);
        ESP_LOGW(TAG, "[Cadence] file pleine (%d), action ignoree : %s -> %s",
                 RULES_JOB_MAX, act->IEEE, act->actionName);
        return false;
    }
    rules_job_t *j = &s_jobs[(s_job_head + s_job_count) % RULES_JOB_MAX];
    j->act = *act;
    jcopy(j->origin, sizeof(j->origin), origin);
    j->queued_ms = rules_now_ms();
    j->busy_retries = 0;
    s_job_count++;
    xSemaphoreGive(s_job_lock);
    rules_wake_task();
    return true;
}

static bool job_requeue_front(const rules_job_t *job)
{
    if (xSemaphoreTake(s_job_lock, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    bool ok = s_job_count < RULES_JOB_MAX;
    if (ok) {
        s_job_head = (s_job_head + RULES_JOB_MAX - 1) % RULES_JOB_MAX;
        s_jobs[s_job_head] = *job;
        s_job_count++;
    }
    xSemaphoreGive(s_job_lock);
    return ok;
}

/** Retire l'action suivante (en ecartant les actions trop anciennes). */
static bool job_pop(rules_job_t *out, int *left)
{
    if (xSemaphoreTake(s_job_lock, pdMS_TO_TICKS(10)) != pdTRUE) return false;
    int64_t now = rules_now_ms();
    while (s_job_count > 0 && now - s_jobs[s_job_head].queued_ms > RULES_JOB_MAX_AGE_MS) {
        ESP_LOGW(TAG, "[Cadence] action abandonnee apres %d s d'attente : %s -> %s",
                 RULES_JOB_MAX_AGE_MS / 1000, s_jobs[s_job_head].act.IEEE,
                 s_jobs[s_job_head].act.actionName);
        s_job_head = (s_job_head + 1) % RULES_JOB_MAX;
        s_job_count--;
    }
    bool have = s_job_count > 0;
    if (have) {
        *out = s_jobs[s_job_head];
        s_job_head = (s_job_head + 1) % RULES_JOB_MAX;
        s_job_count--;
    }
    *left = s_job_count;
    xSemaphoreGive(s_job_lock);
    return have;
}

// ----------------------------------------------------------------------------
// Executeur par defaut des commandes de template
// ----------------------------------------------------------------------------

static uint16_t s_tuya_seq;

static uint8_t zcl_type_size(uint8_t t)
{
    switch (t) {
    case 0x21: case 0x29: case 0x31: return 2;   // uint16, int16, enum16
    case 0x22: return 3;                          // uint24
    case 0x23: case 0x2B: return 4;               // uint32, int32
    default: return 1;                            // bool, uint8, int8, enum8...
    }
}

/** Ecriture d'attribut (commande ZiGate 0x0110), comme SendAttributeWrite(). */
static esp_err_t write_attr(uint16_t sa, uint8_t ep, uint16_t cluster, uint16_t attr,
                            uint8_t type, uint32_t value)
{
    uint8_t d[19];
    uint8_t n = zcl_type_size(type);
    d[0] = 0x02;                       // adresse courte
    d[1] = (uint8_t)(sa >> 8);
    d[2] = (uint8_t)sa;
    d[3] = 0x01;                       // endpoint source
    d[4] = ep;
    d[5] = (uint8_t)(cluster >> 8);
    d[6] = (uint8_t)cluster;
    d[7] = 0x00;                       // direction
    d[8] = 0x00;                       // pas specifique fabricant
    d[9] = 0x00;
    d[10] = 0x00;
    d[11] = 0x01;                      // un attribut
    d[12] = (uint8_t)(attr >> 8);
    d[13] = (uint8_t)attr;
    d[14] = type;
    for (uint8_t i = 0; i < n; i++) d[15 + i] = (uint8_t)(value >> (8 * (n - 1 - i)));
    return zigate_send_command(ZIGATE_CMD_WRITE_ATTRIBUTE, d, (uint16_t)(15 + n));
}

/** Datapoint Tuya (cluster 0xEF00, commande 0x00), comme sendTuyaDatapointSet(). */
static esp_err_t tuya_set(uint16_t sa, uint8_t dp, uint8_t dp_type, uint32_t value)
{
    uint8_t len = (dp_type == 2) ? 4 : 1;      // VALUE : 4 octets, BOOL/ENUM/RAW : 1
    uint8_t d[32];
    int i = 0;
    d[i++] = 0x02;
    d[i++] = (uint8_t)(sa >> 8);
    d[i++] = (uint8_t)sa;
    d[i++] = 0x01;                     // endpoint source
    d[i++] = 0x01;                     // endpoint destination (le DP est dans le champ endpoint du template)
    d[i++] = 0xEF;
    d[i++] = 0x00;
    d[i++] = 0x01;                     // profil HA 0x0104
    d[i++] = 0x04;
    d[i++] = 0x02;                     // securite
    d[i++] = 0x1E;                     // rayon
    int len_idx = i;
    d[i++] = 0x00;                     // longueur ZCL, completee plus bas
    d[i++] = 0x01;                     // cluster specifique, client -> serveur
    d[i++] = (uint8_t)s_tuya_seq;
    d[i++] = 0x00;                     // Datapoint Set
    d[i++] = (uint8_t)(s_tuya_seq >> 8);
    d[i++] = (uint8_t)s_tuya_seq;
    s_tuya_seq++;
    d[i++] = dp;
    d[i++] = dp_type;
    d[i++] = 0x00;
    d[i++] = len;
    for (int k = len - 1; k >= 0; k--) d[i++] = (uint8_t)(value >> (8 * k));
    d[len_idx] = (uint8_t)(i - len_idx - 1);
    return zigate_send_command(ZIGATE_CMD_RAW_APS, d, (uint16_t)i);
}

esp_err_t rules_default_command_executor(uint64_t ieee, uint16_t sa, uint8_t ep, int command,
                                         uint16_t cluster, uint16_t mfr, const char *value,
                                         uint16_t *zcmd_out)
{
    (void)ieee;
    long v = (value && value[0]) ? strtol(value, NULL, 10) : 0;
    uint16_t zcmd = 0;
    esp_err_t err;
    switch (command) {
    case CMD_ONOFF: {
        // Trame 0x0092 a 6 octets (dernier octet : 0 OFF, 1 ON, 2 TOGGLE), comme
        // SendOnOffAction() de la LiXee-Box.
        uint8_t d[6] = { 0x02, (uint8_t)(sa >> 8), (uint8_t)sa, 0x01, ep, (uint8_t)v };
        zcmd = ZIGATE_CMD_ONOFF;
        err = zigate_send_command(zcmd, d, sizeof(d));
        break;
    }
    case CMD_COVER: {
        uint8_t d[6] = { 0x02, (uint8_t)(sa >> 8), (uint8_t)sa, 0x01, ep, (uint8_t)v };
        zcmd = ZIGATE_CMD_WINDOW_COVERING;
        err = zigate_send_command(zcmd, d, sizeof(d));
        break;
    }
    case CMD_COVER_POSITION:
    case CMD_COVER_POSITION_INV: {
        // La position recue est une OUVERTURE (0 ferme, 100 ouvert) ; en ZCL la
        // position de levage mesure la fermeture, sauf appareils inverses (252).
        long pos = v < 0 ? 0 : (v > 100 ? 100 : v);
        uint8_t lift = (command == CMD_COVER_POSITION_INV) ? (uint8_t)pos : (uint8_t)(100 - pos);
        uint8_t d[7] = { 0x02, (uint8_t)(sa >> 8), (uint8_t)sa, 0x01, ep, 0x05, lift };
        zcmd = ZIGATE_CMD_WINDOW_COVERING;
        err = zigate_send_command(zcmd, d, sizeof(d));
        break;
    }
    case CMD_TUYA_BOOL:  zcmd = ZIGATE_CMD_RAW_APS; err = tuya_set(sa, ep, 1, (uint32_t)v); break;
    case CMD_TUYA_VALUE: zcmd = ZIGATE_CMD_RAW_APS; err = tuya_set(sa, ep, 2, (uint32_t)v); break;
    case CMD_TUYA_ENUM:  zcmd = ZIGATE_CMD_RAW_APS; err = tuya_set(sa, ep, 4, (uint32_t)v); break;
    case CMD_TUYA_RAW:   zcmd = ZIGATE_CMD_RAW_APS; err = tuya_set(sa, ep, 0, (uint32_t)v); break;
    case CMD_HVAC_MODE:
        zcmd = ZIGATE_CMD_WRITE_ATTRIBUTE;
        err = write_attr(sa, ep, 0x0201, 0x001C, 0x30, (uint8_t)v);
        break;
    case CMD_HVAC_SETPOINT_BOTH:
        zcmd = ZIGATE_CMD_WRITE_ATTRIBUTE;
        err = write_attr(sa, ep, 0x0201, 0x0011, 0x29, (uint32_t)((int16_t)v & 0xFFFF));
        if (err == ESP_OK) err = write_attr(sa, ep, 0x0201, 0x0012, 0x29, (uint32_t)((int16_t)v & 0xFFFF));
        break;
    case CMD_HVAC_FAN:
        zcmd = ZIGATE_CMD_WRITE_ATTRIBUTE;
        err = write_attr(sa, ep, 0x0202, 0x0000, 0x30, (uint8_t)v);
        break;
    case CMD_HVAC_LOUVER:
        zcmd = ZIGATE_CMD_WRITE_ATTRIBUTE;
        err = write_attr(sa, ep, 0x0201, 0x0045, 0x30, (uint8_t)v);   // ACLouverPosition
        break;
    case CMD_HVAC_COOL_SETPOINT:
    case CMD_HVAC_HEAT_SETPOINT:
        zcmd = ZIGATE_CMD_WRITE_ATTRIBUTE;
        err = write_attr(sa, ep, 0x0201, command == CMD_HVAC_COOL_SETPOINT ? 0x0011 : 0x0012, 0x29,
                         (uint32_t)((int16_t)v & 0xFFFF));
        break;
    case CMD_CLUSTER_SPECIFIC: {
        // Commande cluster specifique fabricant (ex. fil pilote NodOn), comme
        // SendClusterSpecificCommand(..., commandId 0x00, ...).
        uint8_t d[18] = {
            0x02, (uint8_t)(sa >> 8), (uint8_t)sa, 0x01, ep,
            (uint8_t)(cluster >> 8), (uint8_t)cluster,
            0x01, 0x04,                 // profil HA
            0x02, 0x00,                 // securite, rayon
            0x06,                       // longueur de la trame ZCL
            0x05,                       // cluster specifique + specifique fabricant
            (uint8_t)mfr, (uint8_t)(mfr >> 8),
            0x00,                       // numero de sequence
            0x00,                       // commande 0x00
            (uint8_t)v
        };
        zcmd = ZIGATE_CMD_RAW_APS;
        err = zigate_send_command(zcmd, d, sizeof(d));
        break;
    }
    default:
        // 200/201 (IR), 320 (requete compteur Tuya)... : non portes ici, a fournir
        // par l'executeur du lot Zigbee (rules_set_command_executor).
        ESP_LOGW(TAG, "Commande de template %d non geree par l'executeur par defaut", command);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (zcmd_out) *zcmd_out = zcmd;
    return err;
}

// ----------------------------------------------------------------------------
// Execution d'une action radio (portage de RulesManager::executeAction)
// ----------------------------------------------------------------------------

static const devdata_template_action_t *find_tpl_action(uint64_t ieee, const char *name)
{
    const devdata_template_t *t = device_data_get_template(ieee);
    if (!t || !t->loaded) return NULL;
    for (int i = 0; i < t->action_count; i++) {
        if (strcasecmp(t->actions[i].name, name) == 0) return &t->actions[i];
    }
    return NULL;
}

static void origin_text(const char *origin, char *out, size_t len)
{
    if (strncmp(origin, "groupe \"", 8) == 0) jcopy(out, len, origin);
    else snprintf(out, len, "regle \"%.48s\"", origin);
}

/** Envoie l'action ; *sa / *zcmd : trame emise. false si rien n'est parti. */
static bool exec_radio_action(const rules_job_t *job, uint16_t *sa_out, uint16_t *zcmd_out)
{
    const rule_action_t *a = &job->act;
    char org[RULES_ORIGIN_LEN + 16];
    origin_text(job->origin, org, sizeof(org));

    uint64_t ieee = rules_parse_ieee(a->IEEE);
    const zigate_device_t *z = ieee ? zigate_devices_find_by_ieee(ieee) : NULL;
    if (!z) {
        ESP_LOGE(TAG, "[Action] %s : appareil introuvable IEEE=%s", org, a->IEEE);
        return false;
    }
    uint16_t sa = z->short_addr;
    rules_command_fn_t fn = s_cmd_fn ? s_cmd_fn : rules_default_command_executor;
    char value[32];
    int command, endpoint;
    uint16_t cluster = 0, mfr = 0;

    if (strcmp(a->type, "onoff") == 0) {
        // Retrocompatibilite : ancien type "onoff"
        command = CMD_ONOFF;
        endpoint = a->endpoint > 0 ? a->endpoint : 1;
        jcopy(value, sizeof(value), a->value);
    } else {
        const devdata_template_action_t *ta = find_tpl_action(ieee, a->actionName);
        if (!ta) {
            ESP_LOGE(TAG, "[Action] %s : action '%s' absente du template de %s", org, a->actionName, a->IEEE);
            return false;
        }
        command  = a->command >= 0 ? a->command : ta->command;
        endpoint = a->endpoint > 0 ? a->endpoint : ta->endpoint;
        cluster  = ta->cluster;
        mfr      = ta->manufacturer_code;
        if (strcmp(a->type, "dynamic") == 0) {
            // Valeur dynamique : source x coefficient du template x coeff + offset
            uint64_t src = rules_parse_ieee(a->sourceIEEE);
            char raw[DEVDATA_VALUE_LEN];
            if (!src || device_data_get_value(src, (uint16_t)a->sourceCluster,
                                              (uint16_t)(a->sourceAttribute & 0xFFFF), raw, sizeof(raw)) != ESP_OK ||
                !raw[0]) {
                ESP_LOGE(TAG, "[Action] %s : source dynamique sans valeur IEEE=%s", org, a->sourceIEEE);
                return false;
            }
            double sv = (double)strtoull(raw, NULL, 16) *
                        device_data_get_coefficient(src, (uint16_t)a->sourceCluster,
                                                    (uint16_t)(a->sourceAttribute & 0xFFFF));
            double cv = sv * a->coefficient + a->offset;
            snprintf(value, sizeof(value), "%ld", (long)lround(cv));
            ESP_LOGI(TAG, "[Action] dynamique : %.2f x %.2f + %.2f = %s", sv, a->coefficient, a->offset, value);
        } else if (command == CMD_COVER_POSITION || command == CMD_COVER_POSITION_INV) {
            // La position doit venir de l'action : la valeur du template (0)
            // FERMERAIT le volet par surprise. Sans position, rien n'est envoye.
            if (!a->value[0]) {
                ESP_LOGW(TAG, "[Action] %s : '%s' attend une position (0-100), aucune fournie", org, a->actionName);
                return false;
            }
            jcopy(value, sizeof(value), a->value);
        } else {
            snprintf(value, sizeof(value), "%u", (unsigned)ta->value);
        }
    }

    ESP_LOGI(TAG, "[Action] %s : %s -> %s (cmd=%d ep=%d val=%s)", org, a->IEEE,
             a->actionName[0] ? a->actionName : a->type, command, endpoint, value);
    uint16_t zcmd = 0;
    esp_err_t err = fn(ieee, sa, (uint8_t)endpoint, command, cluster, mfr, value, &zcmd);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "[Action] %s : envoi impossible (%s)", org, esp_err_to_name(err));
        return false;
    }
    *sa_out = sa;
    *zcmd_out = zcmd;
    return true;
}

// ----------------------------------------------------------------------------
// Cadencement (portage de ActionPacer::tick)
// ----------------------------------------------------------------------------

static void pacer_step(void)
{
    int64_t now = rules_now_ms();

    // 1. Action en cours : attendre son issue, ou le delai maximal
    if (s_w.waiting) {
        portENTER_CRITICAL(&s_ack_mux);
        bool     done = s_w.done, routing = s_w.routing;
        int64_t  route_ms = s_w.route_ms;
        uint8_t  res = s_w.result;
        uint16_t msg = s_w.result_msg, target = s_w.target;
        portEXIT_CRITICAL(&s_ack_mux);

        if (done) {
            if (msg == 0x8000 && is_busy_status(res) && s_has_cur &&
                s_cur->busy_retries < RULES_MAX_BUSY_RETRY) {
                // ZiGate saturee : l'action n'est pas partie, on la remet en tete
                s_cur->busy_retries++;
                bool requeued = job_requeue_front(s_cur);
                s_slot_freed = false;
                s_pause_until = now + RULES_BUSY_WAIT_MS;
                ESP_LOGW(TAG, "[Cadence] %04X : ZiGate saturee (%02X), %s", target, res,
                         requeued ? "nouvel essai des qu'une place se libere" : "action perdue");
            } else if (res != 0x00) {
                ESP_LOGW(TAG, "[Cadence] %04X : echec (%02X, 0x%04X), action suivante", target, res, msg);
            }
        } else if (routing) {
            if (now - route_ms < RULES_ROUTE_WAIT_MS) return;
            ESP_LOGW(TAG, "[Cadence] %04X : toujours pas de route, action suivante", target);
        } else {
            int64_t lim = s_ack_feedback ? RULES_ACK_WAIT_MS : RULES_PACE_NO_ACK_MS;
            if (now - s_start_ms < lim) return;
            if (s_ack_feedback) ESP_LOGW(TAG, "[Cadence] %04X : pas d'accuse, action suivante", target);
        }
        portENTER_CRITICAL(&s_ack_mux);
        s_w.waiting = false;
        portEXIT_CRITICAL(&s_ack_mux);
    }

    // 2. Pause apres un refus "ZiGate saturee"
    if (s_pause_until != 0) {
        if (!s_slot_freed && now < s_pause_until) return;
        s_pause_until = 0;
    }

    // 3. Action suivante
    if (!s_cur) return;
    int left = 0;
    if (!job_pop(s_cur, &left)) return;
    s_has_cur = true;
    if (left > 0) ESP_LOGI(TAG, "[Cadence] %d action(s) encore en file", left);

    uint16_t sa = 0, zcmd = 0;
    if (exec_radio_action(s_cur, &sa, &zcmd)) {
        portENTER_CRITICAL(&s_ack_mux);
        s_w.target = sa;
        s_w.zcmd = zcmd;
        s_w.done = false;
        s_w.routing = false;
        s_w.result = 0;
        s_w.result_msg = 0;
        s_w.waiting = true;
        portEXIT_CRITICAL(&s_ack_mux);
        s_start_ms = now;
    }
}

static TickType_t pacer_wait_ticks(void)
{
    if (s_w.waiting || s_pause_until != 0) return pdMS_TO_TICKS(50);
    int count = 0;
    if (xSemaphoreTake(s_job_lock, pdMS_TO_TICKS(10)) == pdTRUE) {
        count = s_job_count;
        xSemaphoreGive(s_job_lock);
    } else {
        return pdMS_TO_TICKS(10);
    }
    return count > 0 ? 0 : portMAX_DELAY;
}

// ----------------------------------------------------------------------------
// Tache
// ----------------------------------------------------------------------------

static void rules_task(void *arg)
{
    (void)arg;
    rules_evt_t e;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pacer_wait_ticks());
        while (xQueueReceive(s_evt_q, &e, 0) == pdTRUE) rules_process_event(&e);
        drain_notifications();
        pacer_step();
    }
}

esp_err_t rules_exec_start(void)
{
    if (s_task) return ESP_OK;
    s_jobs = (rules_job_t *)rules_psram_calloc(RULES_JOB_MAX, sizeof(rules_job_t));
    s_cur = (rules_job_t *)rules_psram_calloc(1, sizeof(rules_job_t));
    s_notifs = (rules_notif_t *)rules_psram_calloc(RULES_NOTIF_MAX, sizeof(rules_notif_t));
    s_job_lock = xSemaphoreCreateMutex();
    s_notif_lock = xSemaphoreCreateMutex();
    s_evt_q = xQueueCreateWithCaps(RULES_EVT_QUEUE_LEN, sizeof(rules_evt_t), MALLOC_CAP_SPIRAM);
    if (!s_evt_q) s_evt_q = xQueueCreate(RULES_EVT_QUEUE_LEN, sizeof(rules_evt_t));
    if (!s_jobs || !s_cur || !s_notifs || !s_job_lock || !s_notif_lock || !s_evt_q) {
        ESP_LOGE(TAG, "Memoire insuffisante pour la tache des regles");
        return ESP_ERR_NO_MEM;
    }
#if RULES_TASK_STACK_INTERNAL
    BaseType_t ok = xTaskCreatePinnedToCore(rules_task, "rules", RULES_TASK_STACK_IRAM, NULL,
                                            RULES_TASK_PRIO, &s_task, tskNO_AFFINITY);
#else
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(rules_task, "rules", RULES_TASK_STACK_PSRAM, NULL,
                                                    RULES_TASK_PRIO, &s_task, tskNO_AFFINITY,
                                                    MALLOC_CAP_SPIRAM);
#endif
    if (ok != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "Creation de la tache des regles impossible");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
