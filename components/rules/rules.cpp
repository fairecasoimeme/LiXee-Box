// ============================================================================
// Moteur de regles - portage de RulesManager (rules.cpp de la LiXee-Box)
//
// Chargement de /littlefs/config/rules.json (format inchange, migration des
// anciennes "timeRanges" comprise), evaluation des conditions, machine a etats
// (maintien, repetition, intervalle minimum, maximum par jour), actions ALORS /
// SINON et etat persistant statusRules.json.
//
// Differences voulues avec la LiXee-Box (signalees aussi dans le code) :
//   - rules.json n'est plus relu chaque minute : il est recharge a chaque
//     modification (API, editeur JSON) ;
//   - l'etat des regles est garde en RAM et statusRules.json n'est ecrit qu'une
//     fois par minute au plus (la LiXee-Box relisait et reecrivait le fichier a
//     chaque evaluation) ;
//   - les conditions sont evaluees de gauche a droite SANS arret premature : la
//     LiXee-Box s'arretait au premier ET faux, ignorant un OU suivant ;
//   - une condition sans "logic" vaut ET (elle etait ignoree) ;
//   - un seuil decimal ("21.5") est compare numeriquement (il etait refuse) ;
//   - les valeurs Zigbee sont lues sur 64 bits (strtol 32 bits saturait les index) ;
//   - nouveau type de condition "energy" (puissance, tarif, Tempo, couts du jour /
//     du mois...) via energy_service ; ignore (faux) par une LiXee-Box.
// ============================================================================

#include "rules_internal.h"
#include "action_groups.h"
#include "config_manager.h"
#include "energy_service.h"
#include "zigate_devices.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "RULES";

// ----------------------------------------------------------------------------
// Structures (en PSRAM)
// ----------------------------------------------------------------------------

typedef struct {
    char    type[RULES_TYPE_LEN];   // device, device_compare, time, time_range, weekday,
                                    // date, datetime, day, month, energy (extension)
    char    IEEE[RULES_IEEE_LEN];
    int32_t cluster;
    int32_t attribute;
    char    op[8];
    char    value[48];
    char    value2[16];             // time_range : heure de fin
    char    logic[4];               // AND / OR
    char    subfield[24];           // champ de bits de STGE (issue #31)
    char    metric[20];             // type "energy" : grandeur suivie
    char    IEEE2[RULES_IEEE_LEN];  // device_compare : 2e appareil
    int32_t cluster2;
    int32_t attribute2;
} rule_cond_t;

typedef struct {
    char           name[RULES_NAME_LEN];
    bool           enabled;
    bool           repeat;
    int32_t        duration;        // maintien, en secondes
    int32_t        cooldown;        // secondes minimum entre deux executions
    int32_t        max_per_day;
    char           trig_mode[8];    // "timer" ou "event"
    char           trig_IEEE[RULES_IEEE_LEN];
    int32_t        trig_cluster;
    int32_t        trig_attribute;
    uint64_t       trig_ieee;       // trig_IEEE decode (pre-filtre des rapports)
    rule_cond_t   *cond;
    uint8_t        n_cond;
    rule_action_t *act;
    uint8_t        n_act;
    rule_action_t *els;
    uint8_t        n_els;

    // Etat d'execution, conserve d'un rechargement a l'autre (par nom)
    int8_t         status;          // -1 inconnu, 0 faux, 1 vrai, 2 attente du maintien
    char           last_date[24];
    bool           pending;
    int64_t        pending_since;   // ms
    bool           has_exec;
    int64_t        last_exec;       // ms
    int            exec_count;
    int            exec_day;
} rule_t;

typedef struct {
    uint64_t ieee;
    uint16_t cluster;
    uint16_t attr;
} trig_key_t;

static SemaphoreHandle_t s_lock;        // regles et etat
static SemaphoreHandle_t s_edit_lock;   // serialise les modifications (fichier compris)
static rule_t           *s_rules;       // tableau de RULES_MAX entrees
static int               s_count;
static bool              s_file_bad;    // rules.json illisible : on refuse de l'ecraser
static bool              s_load_trunc;  // conditions / actions au-dela des limites au chargement
static volatile bool     s_status_dirty;
static bool              s_inited;

static portMUX_TYPE      s_trig_mux = portMUX_INITIALIZER_UNLOCKED;
static trig_key_t       *s_trig;
static int               s_trig_count;

static bool lock_rules(void)
{
    return s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(3000)) == pdTRUE;
}

static void unlock_rules(void)
{
    xSemaphoreGive(s_lock);
}

// ----------------------------------------------------------------------------
// Liberation / etat
// ----------------------------------------------------------------------------

static void rule_free_arrays(rule_t *r)
{
    free(r->cond);
    free(r->act);
    free(r->els);
    r->cond = NULL;
    r->act = r->els = NULL;
    r->n_cond = r->n_act = r->n_els = 0;
}

static void set_free(rule_t *arr, int count)
{
    if (!arr) return;
    for (int i = 0; i < count; i++) rule_free_arrays(&arr[i]);
    free(arr);
}

static void state_reset(rule_t *r)
{
    r->status = -1;
    r->last_date[0] = '\0';
    r->pending = false;
    r->pending_since = 0;
    r->has_exec = false;
    r->last_exec = 0;
    r->exec_count = 0;
    r->exec_day = -1;
}

static void state_copy(rule_t *dst, const rule_t *src)
{
    dst->status = src->status;
    memcpy(dst->last_date, src->last_date, sizeof(dst->last_date));
    dst->pending = src->pending;
    dst->pending_since = src->pending_since;
    dst->has_exec = src->has_exec;
    dst->last_exec = src->last_exec;
    dst->exec_count = src->exec_count;
    dst->exec_day = src->exec_day;
}

static int find_rule(const rule_t *arr, int count, const char *name)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(arr[i].name, name) == 0) return i;
    }
    return -1;
}

/** Index des declencheurs "event" (a appeler sous s_lock). */
static void rebuild_triggers(void)
{
    if (!s_trig) return;
    portENTER_CRITICAL(&s_trig_mux);
    int n = 0;
    for (int i = 0; i < s_count && n < RULES_MAX; i++) {
        const rule_t *r = &s_rules[i];
        if (!r->enabled || strcmp(r->trig_mode, "event") != 0 || !r->trig_ieee) continue;
        s_trig[n].ieee = r->trig_ieee;
        s_trig[n].cluster = (uint16_t)r->trig_cluster;
        s_trig[n].attr = (uint16_t)(r->trig_attribute & 0xFFFF);
        n++;
    }
    s_trig_count = n;
    portEXIT_CRITICAL(&s_trig_mux);
}

// ----------------------------------------------------------------------------
// Lecture d'une regle (format rules.json). Chaque condition et chaque action est
// analysee seule : l'arbre cJSON d'une regle complete peut compter des centaines
// de noeuds, alloues en RAM interne.
// ----------------------------------------------------------------------------

/**
 * Attribut d'une regle de la LiXee-Box : une ligne propre a un endpoint y est
 * codee attribut | (endpoint << 16) (epAttrKey). Convertie vers la cle de
 * rangement du pont (devdata_ep_key), les autres valeurs passent telles quelles.
 */
static int32_t box_attr(int64_t v)
{
    if (v > 0xFFFF && v <= 0xFFFFFFFFLL) return devdata_ep_key((uint16_t)(v & 0xFFFF), (uint8_t)(v >> 16));
    return (int32_t)v;
}

static void cond_from_json(const cJSON *c, rule_cond_t *o)
{
    memset(o, 0, sizeof(*o));
    jcopy(o->type, sizeof(o->type), jstr(c, "type", "device"));
    jcopy(o->IEEE, sizeof(o->IEEE), jstr(c, "IEEE", ""));
    o->cluster   = (int32_t)jint(c, "cluster", 0);
    o->attribute = box_attr(jint(c, "attribute", 0));
    jcopy(o->op, sizeof(o->op), jstr(c, "operator", ""));
    jitem_text(cJSON_GetObjectItemCaseSensitive(c, "value"), o->value, sizeof(o->value));
    jcopy(o->value2, sizeof(o->value2), jstr(c, "value2", ""));
    jcopy(o->logic, sizeof(o->logic), jstr(c, "logic", ""));
    jcopy(o->subfield, sizeof(o->subfield), jstr(c, "subfield", ""));
    jcopy(o->metric, sizeof(o->metric), jstr(c, "metric", ""));
    jcopy(o->IEEE2, sizeof(o->IEEE2), jstr(c, "IEEE2", ""));
    o->cluster2   = (int32_t)jint(c, "cluster2", 0);
    o->attribute2 = box_attr(jint(c, "attribute2", 0));
}

typedef struct {
    rule_t        *r;
    rule_cond_t   *cond;   int n_cond;
    rule_cond_t   *mig;    int n_mig;     // conditions issues des anciennes timeRanges
    rule_action_t *act;    int n_act;
    rule_action_t *els;    int n_els;
    char          *old_name;
    size_t         old_len;
    bool           ok;
} rp_ctx_t;

static bool rp_cond(const char *e, size_t n, void *ctx)
{
    rp_ctx_t *c = (rp_ctx_t *)ctx;
    if (c->n_cond >= RULES_MAX_CONDITIONS) { s_load_trunc = true; return true; }
    cJSON *j = cJSON_ParseWithLength(e, n);
    if (cJSON_IsObject(j)) cond_from_json(j, &c->cond[c->n_cond++]);
    else c->ok = false;
    cJSON_Delete(j);
    return true;
}

static bool rp_act_common(const char *e, size_t n, rp_ctx_t *c, rule_action_t *arr, int *count)
{
    if (*count >= RULES_MAX_ACTIONS) { s_load_trunc = true; return true; }
    if (rule_action_from_text(e, n, &arr[*count])) (*count)++;
    else c->ok = false;
    return true;
}

static bool rp_act(const char *e, size_t n, void *ctx)
{
    rp_ctx_t *c = (rp_ctx_t *)ctx;
    return rp_act_common(e, n, c, c->act, &c->n_act);
}

static bool rp_els(const char *e, size_t n, void *ctx)
{
    rp_ctx_t *c = (rp_ctx_t *)ctx;
    return rp_act_common(e, n, c, c->els, &c->n_els);
}

// Migration : timeRanges -> conditions time_range (+ weekday si tous les jours ne
// sont pas coches), comme RulesManager::loadFromFile.
static bool rp_tr(const char *e, size_t n, void *ctx)
{
    rp_ctx_t *c = (rp_ctx_t *)ctx;
    cJSON *j = cJSON_ParseWithLength(e, n);
    if (!cJSON_IsObject(j)) {
        cJSON_Delete(j);
        return true;
    }
    if (c->n_mig < RULES_MAX_CONDITIONS) {
        rule_cond_t *t = &c->mig[c->n_mig++];
        memset(t, 0, sizeof(*t));
        jcopy(t->type, sizeof(t->type), "time_range");
        jcopy(t->op, sizeof(t->op), "in");
        jcopy(t->value, sizeof(t->value), jstr(j, "startTime", "00:00"));
        jcopy(t->value2, sizeof(t->value2), jstr(j, "endTime", "23:59"));
        jcopy(t->logic, sizeof(t->logic), "AND");
    }
    const cJSON *days = cJSON_GetObjectItemCaseSensitive(j, "days");
    int nd = cJSON_IsArray(days) ? cJSON_GetArraySize(days) : 0;
    if (nd > 0 && nd < 7 && c->n_mig < RULES_MAX_CONDITIONS) {
        rule_cond_t *w = &c->mig[c->n_mig++];
        memset(w, 0, sizeof(*w));
        jcopy(w->type, sizeof(w->type), "weekday");
        jcopy(w->op, sizeof(w->op), "in");
        jcopy(w->logic, sizeof(w->logic), "AND");
        size_t o = 0;
        const cJSON *d;
        cJSON_ArrayForEach(d, days) {
            char num[8];
            snprintf(num, sizeof(num), "%s%d", o ? "," : "", (int)(jitem_int(d, 0) % 100));
            size_t l = strlen(num);
            if (o + l + 1 >= sizeof(w->value)) break;
            memcpy(w->value + o, num, l + 1);
            o += l;
        }
    }
    cJSON_Delete(j);
    return true;
}

static bool rp_member(const char *key, size_t klen, const char *val, size_t vlen, void *ctx)
{
    rp_ctx_t *c = (rp_ctx_t *)ctx;
    rule_t *r = c->r;
    bool (*elem_cb)(const char *, size_t, void *) = NULL;
    if (json_key_is(key, klen, "conditions"))       elem_cb = rp_cond;
    else if (json_key_is(key, klen, "actions"))     elem_cb = rp_act;
    else if (json_key_is(key, klen, "elseActions")) elem_cb = rp_els;
    else if (json_key_is(key, klen, "timeRanges"))  elem_cb = rp_tr;
    if (elem_cb) {
        if (!json_foreach_element(val, vlen, elem_cb, c)) c->ok = false;
        return c->ok;
    }

    // Valeurs simples et "trigger" (petit objet) : un arbre cJSON minuscule chacune
    cJSON *v = cJSON_ParseWithLength(val, vlen);
    if (!v) {
        c->ok = false;
        return true;
    }
    if (json_key_is(key, klen, "trigger")) {
        if (cJSON_IsObject(v)) {
            jcopy(r->trig_mode, sizeof(r->trig_mode), jstr(v, "mode", "timer"));
            jcopy(r->trig_IEEE, sizeof(r->trig_IEEE), jstr(v, "IEEE", ""));
            r->trig_cluster = (int32_t)jint(v, "cluster", 0);
            r->trig_attribute = box_attr(jint(v, "attribute", 0));
        }
    } else if (json_key_is(key, klen, "name")) {
        jcopy(r->name, sizeof(r->name), jitem_str(v, ""));
    } else if (json_key_is(key, klen, "enabled")) {
        r->enabled = jitem_bool(v, true);
    } else if (json_key_is(key, klen, "duration")) {
        r->duration = (int32_t)jitem_int(v, 0);
    } else if (json_key_is(key, klen, "repeat")) {
        r->repeat = jitem_bool(v, false);
    } else if (json_key_is(key, klen, "cooldown")) {
        r->cooldown = (int32_t)jitem_int(v, 0);
    } else if (json_key_is(key, klen, "maxExecPerDay")) {
        r->max_per_day = (int32_t)jitem_int(v, 0);
    } else if (json_key_is(key, klen, "oldName") && c->old_name) {
        jcopy(c->old_name, c->old_len, jitem_str(v, ""));
    }
    cJSON_Delete(v);
    return true;
}

/**
 * Analyse le texte JSON d'une regle. false : JSON invalide (la regle n'est pas
 * utilisable). old_name (peut etre NULL) recoit "oldName" (route edit).
 */
static bool parse_rule_text(const char *text, size_t len, rule_t *r, char *old_name, size_t old_len)
{
    memset(r, 0, sizeof(*r));
    r->enabled = true;
    jcopy(r->trig_mode, sizeof(r->trig_mode), "timer");
    state_reset(r);
    if (old_name && old_len) old_name[0] = '\0';

    rp_ctx_t c = {};
    c.r = r;
    c.old_name = old_name;
    c.old_len = old_len;
    c.ok = true;
    c.cond = (rule_cond_t *)rules_psram_calloc(RULES_MAX_CONDITIONS, sizeof(rule_cond_t));
    c.mig  = (rule_cond_t *)rules_psram_calloc(RULES_MAX_CONDITIONS, sizeof(rule_cond_t));
    c.act  = (rule_action_t *)rules_psram_calloc(RULES_MAX_ACTIONS, sizeof(rule_action_t));
    c.els  = (rule_action_t *)rules_psram_calloc(RULES_MAX_ACTIONS, sizeof(rule_action_t));
    bool ok = c.cond && c.mig && c.act && c.els;
    if (ok) ok = json_foreach_member(text, len, rp_member, &c) && c.ok;

    if (ok) {
        int nc = c.n_mig + c.n_cond;
        if (nc > RULES_MAX_CONDITIONS) nc = RULES_MAX_CONDITIONS;
        if (nc > 0) {
            r->cond = (rule_cond_t *)rules_psram_calloc((size_t)nc, sizeof(rule_cond_t));
            if (!r->cond) ok = false;
            else {
                int k = 0;
                for (int i = 0; i < c.n_mig && k < nc; i++) r->cond[k++] = c.mig[i];
                for (int i = 0; i < c.n_cond && k < nc; i++) r->cond[k++] = c.cond[i];
                r->n_cond = (uint8_t)k;
            }
        }
        if (ok && c.n_act > 0) {
            r->act = (rule_action_t *)rules_psram_malloc((size_t)c.n_act * sizeof(rule_action_t));
            if (!r->act) ok = false;
            else {
                memcpy(r->act, c.act, (size_t)c.n_act * sizeof(rule_action_t));
                r->n_act = (uint8_t)c.n_act;
            }
        }
        if (ok && c.n_els > 0) {
            r->els = (rule_action_t *)rules_psram_malloc((size_t)c.n_els * sizeof(rule_action_t));
            if (!r->els) ok = false;
            else {
                memcpy(r->els, c.els, (size_t)c.n_els * sizeof(rule_action_t));
                r->n_els = (uint8_t)c.n_els;
            }
        }
        if (c.n_mig > 0) ESP_LOGI(TAG, "Migration : regle '%s', timeRanges converties en conditions", r->name);
    }
    free(c.cond);
    free(c.mig);
    free(c.act);
    free(c.els);
    if (!ok) rule_free_arrays(r);
    r->trig_ieee = rules_parse_ieee(r->trig_IEEE);
    return ok;
}

typedef struct {
    rule_t *arr;
    int     count;
    int     errors;
    bool    found;
} doc_ctx_t;

static bool doc_rule(const char *e, size_t n, void *ctx)
{
    doc_ctx_t *c = (doc_ctx_t *)ctx;
    if (c->count >= RULES_MAX) {
        c->errors++;
        return true;
    }
    if (parse_rule_text(e, n, &c->arr[c->count], NULL, 0)) c->count++;
    else c->errors++;
    return true;
}

static bool doc_member(const char *key, size_t klen, const char *val, size_t vlen, void *ctx)
{
    doc_ctx_t *c = (doc_ctx_t *)ctx;
    if (!json_key_is(key, klen, "rules")) return true;
    c->found = true;
    return json_foreach_element(val, vlen, doc_rule, c);
}

/** Analyse un rules.json complet ; *out : tableau de RULES_MAX entrees (a liberer). */
static bool parse_rules_doc(const char *text, size_t len, rule_t **out, int *count, int *errors, bool *found)
{
    doc_ctx_t c = {};
    c.arr = (rule_t *)rules_psram_calloc(RULES_MAX, sizeof(rule_t));
    if (!c.arr) return false;
    bool ok = json_foreach_member(text, len, doc_member, &c);
    if (!ok) {
        set_free(c.arr, c.count);
        return false;
    }
    *out = c.arr;
    *count = c.count;
    if (errors) *errors = c.errors;
    if (found) *found = c.found;
    return true;
}

// ----------------------------------------------------------------------------
// Ecriture (memes cles que web.cpp de la LiXee-Box)
// ----------------------------------------------------------------------------

static void ser_cond(sb_t *sb, const rule_cond_t *c)
{
    sb_putc(sb, '{');
    sb_kstr(sb, "type", c->type);
    sb_kstr(sb, "IEEE", c->IEEE);
    sb_kint(sb, "cluster", c->cluster);
    sb_kint(sb, "attribute", c->attribute);
    sb_kstr(sb, "operator", c->op);
    sb_kstr(sb, "value", c->value);
    sb_kstr(sb, "value2", c->value2);
    sb_kstr(sb, "logic", c->logic);
    if (strcmp(c->type, "device_compare") == 0 || c->IEEE2[0]) {
        sb_kstr(sb, "IEEE2", c->IEEE2);
        sb_kint(sb, "cluster2", c->cluster2);
        sb_kint(sb, "attribute2", c->attribute2);
    }
    if (c->subfield[0]) sb_kstr(sb, "subfield", c->subfield);
    if (c->metric[0])   sb_kstr(sb, "metric", c->metric);
    sb_close(sb, '}');
}

static void ser_actions(sb_t *sb, const char *key, const rule_action_t *a, int n)
{
    sb_key(sb, key);
    sb_putc(sb, '[');
    for (int i = 0; i < n; i++) {
        rule_action_to_json(sb, &a[i], ACT_STYLE_RULE);
        sb_putc(sb, ',');
    }
    sb_close(sb, ']');
    sb_putc(sb, ',');
}

static void ser_rule(sb_t *sb, const rule_t *r, bool with_status)
{
    sb_putc(sb, '{');
    sb_kstr(sb, "name", r->name);
    sb_kbool(sb, "enabled", r->enabled);
    sb_kint(sb, "duration", r->duration);
    sb_kbool(sb, "repeat", r->repeat);
    sb_kint(sb, "cooldown", r->cooldown);
    sb_kint(sb, "maxExecPerDay", r->max_per_day);
    sb_key(sb, "trigger");
    sb_putc(sb, '{');
    sb_kstr(sb, "mode", r->trig_mode);
    sb_kstr(sb, "IEEE", r->trig_IEEE);
    sb_kint(sb, "cluster", r->trig_cluster);
    sb_kint(sb, "attribute", r->trig_attribute);
    sb_close(sb, '}');
    sb_putc(sb, ',');
    sb_key(sb, "conditions");
    sb_putc(sb, '[');
    for (int i = 0; i < r->n_cond; i++) {
        ser_cond(sb, &r->cond[i]);
        sb_putc(sb, ',');
    }
    sb_close(sb, ']');
    sb_putc(sb, ',');
    ser_actions(sb, "actions", r->act, r->n_act);
    ser_actions(sb, "elseActions", r->els, r->n_els);
    if (with_status) {
        sb_kint(sb, "status", r->status < 0 ? 0 : r->status);
        sb_kstr(sb, "lastDate", r->last_date);
    }
    sb_close(sb, '}');
}

/** Texte complet de rules.json (sous s_lock). */
static char *ser_rules_file(void)
{
    sb_t sb;
    sb_init(&sb, 1024 + (size_t)s_count * 768);
    sb_puts(&sb, "{\"rules\":[");
    for (int i = 0; i < s_count; i++) {
        ser_rule(&sb, &s_rules[i], false);
        sb_putc(&sb, ',');
    }
    sb_close(&sb, ']');
    sb_putc(&sb, '}');
    return sb_take(&sb);
}

// ----------------------------------------------------------------------------
// Etat persistant (statusRules.json)
// ----------------------------------------------------------------------------

static void set_status(rule_t *r, int st, const char *date)
{
    r->status = (int8_t)st;
    jcopy(r->last_date, sizeof(r->last_date), date);
    s_status_dirty = true;
}

static void load_status(void)
{
    size_t len = 0;
    char *t = rules_read_file(RULES_STATUS_PATH, &len);
    if (!t) {
        // Comme la LiXee-Box : fichier cree vide au premier demarrage
        cfg_write_file(RULES_STATUS_PATH, "{}");
        return;
    }
    cJSON *j = cJSON_ParseWithLength(t, len);
    free(t);
    if (!cJSON_IsObject(j)) {
        cJSON_Delete(j);
        ESP_LOGW(TAG, "statusRules.json illisible, etats ignores");
        return;
    }
    if (lock_rules()) {
        for (int i = 0; i < s_count; i++) {
            const char *v = jstr(j, s_rules[i].name, NULL);
            if (!v || !v[0] || strcmp(v, "null") == 0) continue;
            s_rules[i].status = (int8_t)atoi(v);
            const char *bar = strchr(v, '|');
            jcopy(s_rules[i].last_date, sizeof(s_rules[i].last_date), bar ? bar + 1 : "");
        }
        unlock_rules();
    }
    cJSON_Delete(j);
}

static void save_status_if_dirty(void)
{
    if (!s_status_dirty || !lock_rules()) return;
    sb_t sb;
    sb_init(&sb, 64 + (size_t)s_count * 64);
    sb_putc(&sb, '{');
    for (int i = 0; i < s_count; i++) {
        const rule_t *r = &s_rules[i];
        if (r->status < 0) continue;
        char v[48];
        snprintf(v, sizeof(v), "%d|%s", (int)r->status, r->last_date);
        sb_kstr(&sb, r->name, v);
    }
    sb_close(&sb, '}');
    s_status_dirty = false;
    unlock_rules();
    char *txt = sb_take(&sb);
    if (!txt || cfg_write_file(RULES_STATUS_PATH, txt) != ESP_OK) {
        s_status_dirty = true;
        ESP_LOGW(TAG, "Ecriture de statusRules.json impossible");
    }
    free(txt);
}

// ----------------------------------------------------------------------------
// Chargement / remplacement du jeu de regles
// ----------------------------------------------------------------------------

/** Installe un nouveau jeu (etat repris par nom), libere l'ancien. */
static void install_set(rule_t *arr, int count, const char *renamed_from, const char *renamed_to)
{
    if (!lock_rules()) {
        set_free(arr, count);
        return;
    }
    for (int i = 0; i < count; i++) {
        const char *src = arr[i].name;
        if (renamed_to && renamed_from && strcmp(src, renamed_to) == 0) src = renamed_from;
        int k = find_rule(s_rules, s_count, src);
        if (k >= 0) state_copy(&arr[i], &s_rules[k]);
    }
    rule_t *old = s_rules;
    int old_count = s_count;
    s_rules = arr;
    s_count = count;
    rebuild_triggers();
    s_status_dirty = true;   // etats des regles supprimees retires au prochain enregistrement
    unlock_rules();
    set_free(old, old_count);
}

static void load_from_file(void)
{
    size_t len = 0;
    char *text = rules_read_file(RULES_FILE_PATH, &len);
    rule_t *arr = NULL;
    int count = 0, errors = 0;
    if (!text) {
        s_file_bad = false;
        arr = (rule_t *)rules_psram_calloc(RULES_MAX, sizeof(rule_t));
        if (arr) install_set(arr, 0, NULL, NULL);
        ESP_LOGI(TAG, "Pas de rules.json : aucune regle");
        return;
    }
    s_load_trunc = false;
    if (!parse_rules_doc(text, len, &arr, &count, &errors, NULL)) {
        free(text);
        s_file_bad = true;
        ESP_LOGE(TAG, "rules.json illisible : regles desactivees, fichier conserve tel quel");
        arr = (rule_t *)rules_psram_calloc(RULES_MAX, sizeof(rule_t));
        if (arr) install_set(arr, 0, NULL, NULL);
        return;
    }
    free(text);
    // Regle ignoree ou tronquee : le fichier n'est pas reecrit depuis la memoire
    // (une modification perdrait ce qui n'a pas ete charge) ; l'editeur brut
    // permet de le corriger
    s_file_bad = errors > 0 || s_load_trunc;
    if (errors) ESP_LOGW(TAG, "rules.json : %d regle(s) ignoree(s) (JSON invalide ou plus de %d)", errors, RULES_MAX);
    if (s_load_trunc) ESP_LOGW(TAG, "rules.json : conditions ou actions au-dela des limites (%d / %d)",
                               RULES_MAX_CONDITIONS, RULES_MAX_ACTIONS);
    if (s_file_bad) ESP_LOGW(TAG, "rules.json protege : modifiez-le par l'editeur brut");
    install_set(arr, count, NULL, NULL);
    ESP_LOGI(TAG, "%d regle(s) chargee(s)", count);
}

// ----------------------------------------------------------------------------
// Valeurs des appareils
// ----------------------------------------------------------------------------

// Champs de bits du registre STGE (ZLinky FF66/535), issue #31. Les positions
// suivent la table Enedis (getStatusRegisterFieldValue de zigbee.cpp).
static const struct {
    const char *name;
    uint8_t     shift;
    uint8_t     mask;
} STGE_FIELDS[] = {
    {"contact_sec", 0, 0x1},           {"organe_coupure", 1, 0x7},
    {"cache_borne_dist", 4, 0x1},      {"surtension_phase", 6, 0x1},
    {"depassement_ref_pow", 7, 0x1},   {"producteur", 8, 0x1},
    {"sens_energie_active", 9, 0x1},   {"tarif_four", 10, 0xF},
    {"tarif_dist", 14, 0x3},           {"horloge", 16, 0x1},
    {"type_tic", 17, 0x1},             {"comm_euridis", 19, 0x3},
    {"etat_cpl", 21, 0x3},             {"sync_cpl", 23, 0x1},
    {"tempo_jour", 24, 0x3},           {"tempo_demain", 26, 0x3},
    {"preavis_pointe_mobile", 28, 0x3}, {"pointe_mobile", 30, 0x3},
};

static long stge_field_value(const char *hex, const char *field)
{
    // Texte de 8 chiffres hexa range en hexa de ses caracteres (ZLinky)
    uint32_t v;
    if (!device_data_stge_value(hex, &v)) return -1;
    for (size_t i = 0; i < sizeof(STGE_FIELDS) / sizeof(STGE_FIELDS[0]); i++) {
        if (strcmp(field, STGE_FIELDS[i].name) == 0) {
            return (long)((v >> STGE_FIELDS[i].shift) & STGE_FIELDS[i].mask);
        }
    }
    return -1;
}

/** Libelle d'un champ STGE pour le texte des notifications (parseStatusRegister). */
static void stge_field_label(const char *field, long v, char *out, size_t len)
{
    static const char *const CONTACT[] = { "Ferm\xC3\xA9", "Ouvert" };
    static const char *const COUPURE[] = { "Ferm\xC3\xA9", "Surpuissance", "Surtension", "Delestage",
                                           "Ordre_CPL_Euridis", "Surchauffe_surcourant", "Surchauffe_simple", "Inconnu" };
    static const char *const TEMPO[]   = { "UNDEF", "BLEU", "BLANC", "ROUGE" };
    static const char *const PM[]      = { "AUCUN", "PM1", "PM2", "PM3" };
    if (v < 0) {
        jcopy(out, len, "");
    } else if (!strcmp(field, "contact_sec") || !strcmp(field, "cache_borne_dist")) {
        jcopy(out, len, CONTACT[v & 1]);
    } else if (!strcmp(field, "organe_coupure")) {
        jcopy(out, len, COUPURE[v & 7]);
    } else if (!strcmp(field, "producteur")) {
        jcopy(out, len, v ? "Producteur" : "Consommateur");
    } else if (!strcmp(field, "sens_energie_active")) {
        jcopy(out, len, v ? "Negative" : "Positive");
    } else if (!strcmp(field, "tarif_four") || !strcmp(field, "tarif_dist")) {
        snprintf(out, len, "index_%ld", v + 1);
    } else if (!strcmp(field, "horloge")) {
        jcopy(out, len, v ? "D\xC3\xA9grad\xC3\xA9" "e" : "Correcte");
    } else if (!strcmp(field, "type_tic")) {
        jcopy(out, len, v ? "Standard" : "Historique");
    } else if (!strcmp(field, "tempo_jour") || !strcmp(field, "tempo_demain")) {
        jcopy(out, len, TEMPO[v & 3]);
    } else if (!strcmp(field, "preavis_pointe_mobile") || !strcmp(field, "pointe_mobile")) {
        jcopy(out, len, PM[v & 3]);
    } else {
        snprintf(out, len, "%ld", v);
    }
}

/**
 * Valeur courante d'un attribut (getCurrentValueAsString). Texte decode si le
 * template declare l'attribut "string" (device_data garde les octets en hexa),
 * valeur numerique du champ si subfield, sinon chaine hexa brute.
 */
static bool attr_value(const char *ieee_str, int cluster, int attr, const char *subfield,
                       char *out, size_t len, bool *is_text, float *coeff)
{
    *is_text = false;
    if (coeff) *coeff = 1.0f;
    uint64_t ieee = rules_parse_ieee(ieee_str);
    if (!ieee) return false;
    char raw[DEVDATA_VALUE_LEN];
    if (device_data_get_value(ieee, (uint16_t)cluster, (uint16_t)(attr & 0xFFFF), raw, sizeof(raw)) != ESP_OK ||
        !raw[0]) return false;
    const devdata_template_state_t *st = rules_tpl_state(ieee, cluster, attr);
    if (coeff && st) *coeff = st->coefficient;
    if (subfield && subfield[0]) {
        long fv = stge_field_value(raw, subfield);
        if (fv < 0) return false;
        snprintf(out, len, "%ld", fv);
        return true;
    }
    // Texte : etat declare "string" dans le template, ou valeur recue en
    // chaine ZCL (action d'un bouton sans fil, libelle TIC...) : comparee en
    // clair, comme la LiXee-Box qui range le texte decode
    const devdata_attr_t *da = device_data_get_attr(ieee, (uint16_t)cluster, (uint16_t)(attr & 0xFFFF));
    bool zcl_text = da && da->type >= 0x41 && da->type <= 0x44;
    if ((st && strcmp(st->type, "string") == 0) || zcl_text) {
        rules_hex_to_text(raw, out, len);
        *is_text = true;
        return out[0] != '\0';
    }
    jcopy(out, len, raw);
    return true;
}

/** isNumeric() de la LiXee-Box pour une valeur Zigbee (chiffres hexadecimaux). */
static bool zb_is_numeric(const char *s)
{
    if (!s || !s[0]) return false;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return true;
    for (size_t i = 0; s[i]; i++) {
        if (i == 0 && s[i] == '-') continue;
        if (!isxdigit((unsigned char)s[i])) return false;
    }
    return true;
}

static double zb_number(const char *s)
{
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return (double)strtoull(s + 2, NULL, 16);
    return (double)strtoull(s, NULL, 16);
}

/** Seuil saisi : decimal (21.5) ou hexadecimal 0x... */
static bool user_number(const char *s, double *out)
{
    if (!s) return false;
    while (*s == ' ') s++;
    if (!*s) return false;
    char *end;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        *out = (double)strtoull(s + 2, &end, 16);
    } else {
        *out = strtod(s, &end);
    }
    while (*end == ' ') end++;
    return *end == '\0';
}

// Forme canonique pour == / != : espaces de debut et de fin retires, suites
// d'espaces reduites a un (libelles TIC completes par des espaces).
static void normalize_text(const char *in, char *out, size_t len)
{
    size_t o = 0;
    bool pending = false;
    for (const char *p = in; *p && o + 2 < len; p++) {
        char c = *p;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            pending = o > 0;
            continue;
        }
        if (pending) {
            out[o++] = ' ';
            pending = false;
        }
        out[o++] = c;
    }
    out[o] = '\0';
}

static bool text_equal(const char *a, const char *b)
{
    char na[64], nb[64];
    normalize_text(a, na, sizeof(na));
    normalize_text(b, nb, sizeof(nb));
    return strcasecmp(na, nb) == 0;
}

static bool cmp_num(const char *op, double a, double b)
{
    if (!strcmp(op, "==")) return a == b;
    if (!strcmp(op, "!=")) return a != b;
    if (!strcmp(op, "<"))  return a < b;
    if (!strcmp(op, "<=")) return a <= b;
    if (!strcmp(op, ">"))  return a > b;
    if (!strcmp(op, ">=")) return a >= b;
    return false;
}

// ----------------------------------------------------------------------------
// Conditions temporelles
// ----------------------------------------------------------------------------

typedef struct {
    bool          tm_ok;
    struct tm     tm;
    bool          live_done;
    bool          live_ok;
    energy_live_t live;
    bool          cost_done[4];
    bool          cost_ok[4];
    int64_t       wh[4];
    double        eur[4];
    int64_t       prod[4];
} eval_ctx_t;

static int parse_hhmm(const char *s)
{
    if (!s || strlen(s) < 5) return -1;
    return atoi(s) * 60 + atoi(s + 3);
}

static int weekday_iso(const struct tm *t)
{
    return t->tm_wday == 0 ? 7 : t->tm_wday;
}

static bool in_weekday_list(const char *list, int day)
{
    const char *p = list;
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        if (atoi(p) == day) return true;
        while (*p && *p != ',') p++;
    }
    return false;
}

static bool eval_time_cond(const rule_cond_t *c, const eval_ctx_t *x)
{
    const struct tm *t = &x->tm;
    const char *op = c->op;
    const char *type = c->type;
    int cur_min = t->tm_hour * 60 + t->tm_min;

    if (!strcmp(type, "time")) {
        int m = parse_hhmm(c->value);
        return m >= 0 && cmp_num(op, cur_min, m);
    }
    if (!strcmp(type, "time_range")) {
        int s = parse_hhmm(c->value), e = parse_hhmm(c->value2);
        if (s < 0 || e < 0) return false;
        bool in = (s <= e) ? (cur_min >= s && cur_min <= e) : (cur_min >= s || cur_min <= e);
        if (!strcmp(op, "in") || !strcmp(op, "==")) return in;
        if (!strcmp(op, "!=") || !strcmp(op, "not_in")) return !in;
        return false;
    }
    if (!strcmp(type, "weekday")) {
        int day = weekday_iso(t);
        if (!strcmp(op, "in")) return in_weekday_list(c->value, day);
        if (!strcmp(op, "not_in") || (!strcmp(op, "!=") && strchr(c->value, ','))) return !in_weekday_list(c->value, day);
        return cmp_num(op, day, atoi(c->value));
    }
    if (!strcmp(type, "date")) {
        int d = 0, m = 0, y = 0;
        size_t l = strlen(c->value);
        if (l >= 5) {
            d = atoi(c->value);
            m = atoi(c->value + 3);
            if (l >= 10) y = atoi(c->value + 6);
        }
        int cd = t->tm_mday, cm = t->tm_mon + 1, cy = t->tm_year + 1900;
        bool same = (y == 0 || y == cy) && m == cm && d == cd;
        if (!strcmp(op, "==")) return same;
        if (!strcmp(op, "!=")) return !same;
        if (y == 0) return cmp_num(op, cm * 100 + cd, m * 100 + d);
        return cmp_num(op, (double)cy * 10000 + cm * 100 + cd, (double)y * 10000 + m * 100 + d);
    }
    if (!strcmp(type, "datetime")) {
        if (strlen(c->value) < 16) return false;
        const char *v = c->value;
        double cond_ts = (double)atoi(v + 6) * 100000000.0 + atoi(v + 3) * 1000000.0 +
                         atoi(v) * 10000.0 + atoi(v + 11) * 100.0 + atoi(v + 14);
        double cur_ts = (double)(t->tm_year + 1900) * 100000000.0 + (t->tm_mon + 1) * 1000000.0 +
                        t->tm_mday * 10000.0 + t->tm_hour * 100.0 + t->tm_min;
        return cmp_num(op, cur_ts, cond_ts);
    }
    if (!strcmp(type, "day"))   return cmp_num(op, t->tm_mday, atoi(c->value));
    if (!strcmp(type, "month")) return cmp_num(op, t->tm_mon + 1, atoi(c->value));
    return false;
}

static bool is_time_type(const char *type)
{
    return !strcmp(type, "time") || !strcmp(type, "time_range") || !strcmp(type, "weekday") ||
           !strcmp(type, "date") || !strcmp(type, "datetime") || !strcmp(type, "day") ||
           !strcmp(type, "month");
}

// ----------------------------------------------------------------------------
// Conditions "energy" (extension MatterBridge)
// ----------------------------------------------------------------------------

static const struct {
    const char *key;
    const char *label;
    const char *unit;
} ENERGY_METRICS[] = {
    {"power_va",       "Puissance soutir\xC3\xA9" "e",          "VA"},
    {"inj_va",         "Puissance inject\xC3\xA9" "e",          "VA"},
    {"phase1_va",      "Puissance phase 1",                     "VA"},
    {"phase2_va",      "Puissance phase 2",                     "VA"},
    {"phase3_va",      "Puissance phase 3",                     "VA"},
    {"voltage1",       "Tension phase 1",                       "V"},
    {"voltage2",       "Tension phase 2",                       "V"},
    {"voltage3",       "Tension phase 3",                       "V"},
    {"overload",       "D\xC3\xA9passement de puissance",       ""},
    {"tariff",         "Tarif en cours",                        ""},
    {"tariff_price",   "Prix du kWh en cours",                  "EUR/kWh"},
    {"tempo_today",    "Couleur Tempo du jour",                 ""},
    {"tempo_tomorrow", "Couleur Tempo de demain",               ""},
    {"wh_today",       "Consommation du jour",                  "Wh"},
    {"eur_today",      "Co\xC3\xBBt du jour",                   "EUR"},
    {"prod_today",     "Production du jour",                    "Wh"},
    {"wh_yesterday",   "Consommation de la veille",             "Wh"},
    {"eur_yesterday",  "Co\xC3\xBBt de la veille",              "EUR"},
    {"wh_month",       "Consommation du mois",                  "Wh"},
    {"eur_month",      "Co\xC3\xBBt du mois",                   "EUR"},
    {"wh_year",        "Consommation de l'ann\xC3\xA9" "e",     "Wh"},
    {"eur_year",       "Co\xC3\xBBt de l'ann\xC3\xA9" "e",      "EUR"},
};

static int energy_metric_index(const char *key)
{
    for (size_t i = 0; i < sizeof(ENERGY_METRICS) / sizeof(ENERGY_METRICS[0]); i++) {
        if (!strcmp(key, ENERGY_METRICS[i].key)) return (int)i;
    }
    return -1;
}

static bool period_cost(eval_ctx_t *x, energy_cost_period_t p, int64_t *wh, double *eur, int64_t *prod)
{
    int i = (int)p;
    if (i < 0 || i > 3) return false;
    if (!x->cost_done[i]) {
        x->cost_done[i] = true;
        x->cost_ok[i] = energy_service_period_cost(p, &x->wh[i], &x->eur[i], &x->prod[i]);
    }
    if (!x->cost_ok[i]) return false;
    if (wh) *wh = x->wh[i];
    if (eur) *eur = x->eur[i];
    if (prod) *prod = x->prod[i];
    return true;
}

/** Valeur d'une grandeur "energy" : numerique, ou texte (tarif). */
static bool energy_value(eval_ctx_t *x, const char *m, double *num, char *text, size_t tlen, bool *is_text)
{
    *is_text = false;
    if (!x->live_done) {
        x->live_done = true;
        x->live_ok = energy_service_get_live(&x->live);
    }
    const energy_live_t *l = &x->live;
    bool live = x->live_ok && l->valid;
    // Grandeurs instantanees : seulement si le ZLinky emet encore (valeur perimee sinon)
    bool fresh = live && l->connected;
    int64_t wh = 0, prod = 0;
    double eur = 0;

    if (!strcmp(m, "power_va"))  { *num = l->total_va; return fresh; }
    if (!strcmp(m, "inj_va"))    { *num = l->inj_va; return fresh; }
    if (!strncmp(m, "phase", 5) && m[5] >= '1' && m[5] <= '3') {
        int p = m[5] - '1';
        *num = l->phase_va[p];
        return fresh && l->has_phase[p];
    }
    if (!strncmp(m, "voltage", 7) && m[7] >= '1' && m[7] <= '3') {
        int p = m[7] - '1';
        *num = l->voltage[p];
        return fresh && l->has_voltage[p];
    }
    if (!strcmp(m, "overload"))       { *num = l->overload ? 1 : 0; return live; }
    if (!strcmp(m, "tariff_price"))   { *num = l->tariff_price; return live; }
    if (!strcmp(m, "tempo_today"))    { *num = l->tempo_today; return live; }
    if (!strcmp(m, "tempo_tomorrow")) { *num = l->tempo_tomorrow; return live; }
    if (!strcmp(m, "tariff")) {
        *is_text = true;
        jcopy(text, tlen, l->tariff);
        return live && l->tariff[0];
    }
    if (!strcmp(m, "wh_today") || !strcmp(m, "eur_today") || !strcmp(m, "prod_today")) {
        if (!period_cost(x, ENERGY_COST_TODAY, &wh, &eur, &prod)) return false;
        *num = m[0] == 'w' ? (double)wh : (m[0] == 'e' ? eur : (double)prod);
        return true;
    }
    if (!strcmp(m, "wh_yesterday") || !strcmp(m, "eur_yesterday")) {
        if (!period_cost(x, ENERGY_COST_YESTERDAY, &wh, &eur, NULL)) return false;
        *num = m[0] == 'w' ? (double)wh : eur;
        return true;
    }
    if (!strcmp(m, "wh_month") || !strcmp(m, "eur_month")) {
        if (!period_cost(x, ENERGY_COST_MONTH, &wh, &eur, NULL)) return false;
        *num = m[0] == 'w' ? (double)wh : eur;
        return true;
    }
    if (!strcmp(m, "wh_year") || !strcmp(m, "eur_year")) {
        if (!period_cost(x, ENERGY_COST_YEAR, &wh, &eur, NULL)) return false;
        *num = m[0] == 'w' ? (double)wh : eur;
        return true;
    }
    return false;
}

// ----------------------------------------------------------------------------
// Evaluation d'une condition (RulesManager::evaluateCondition)
// ----------------------------------------------------------------------------

static bool eval_cond(const rule_cond_t *c, eval_ctx_t *x)
{
    const char *type = c->type;

    if (is_time_type(type)) return x->tm_ok && eval_time_cond(c, x);

    if (!strcmp(type, "energy")) {
        double num = 0, ref = 0;
        char text[32];
        bool is_text;
        if (!energy_value(x, c->metric, &num, text, sizeof(text), &is_text)) return false;
        if (is_text) {
            if (!strcmp(c->op, "==")) return text_equal(text, c->value);
            if (!strcmp(c->op, "!=")) return !text_equal(text, c->value);
            return false;
        }
        return user_number(c->value, &ref) && cmp_num(c->op, num, ref);
    }

    if (!strcmp(type, "device_compare")) {
        char v1[DEVDATA_VALUE_LEN], v2[DEVDATA_VALUE_LEN];
        bool t1, t2;
        float k1, k2;
        if (!attr_value(c->IEEE, c->cluster, c->attribute, NULL, v1, sizeof(v1), &t1, &k1) ||
            !attr_value(c->IEEE2, c->cluster2, c->attribute2, NULL, v2, sizeof(v2), &t2, &k2)) return false;
        if (t1 || t2 || !zb_is_numeric(v1) || !zb_is_numeric(v2)) return false;
        double a = zb_number(v1) * k1;
        double b = zb_number(v2) * k2;
        double off = 0;
        if (c->value[0] && user_number(c->value, &off)) b += off;   // decalage optionnel
        return cmp_num(c->op, a, b);
    }

    if (strcmp(type, "device") != 0) return false;   // type inconnu

    char cur[DEVDATA_VALUE_LEN];
    bool is_text;
    float coeff;
    if (!attr_value(c->IEEE, c->cluster, c->attribute, c->subfield, cur, sizeof(cur), &is_text, &coeff)) return false;

    // Sous-champ STGE : valeurs numeriques du champ, en decimal, sans coefficient
    if (c->subfield[0]) return cmp_num(c->op, atol(cur), atol(c->value));

    double ref = 0;
    bool cur_num = !is_text && zb_is_numeric(cur);
    bool ref_num = !is_text && user_number(c->value, &ref);

    if (!strcmp(c->op, "==") || !strcmp(c->op, "!=")) {
        bool eq;
        if (!cur_num || !ref_num) eq = text_equal(cur, c->value);
        else eq = zb_number(cur) * coeff == ref;
        return c->op[0] == '=' ? eq : !eq;
    }
    if (!cur_num || !ref_num) {
        ESP_LOGW(TAG, "Comparaison numerique impossible : valeur '%s', seuil '%s'", cur, c->value);
        return false;
    }
    return cmp_num(c->op, zb_number(cur) * coeff, ref);
}

// ----------------------------------------------------------------------------
// Texte des notifications (buildConditionsText / substituteVariables)
// ----------------------------------------------------------------------------

static const char *usable_unit(const devdata_template_state_t *st)
{
    return (st && st->unit[0] && strcmp(st->unit, "null") != 0) ? st->unit : "";
}

/** Valeur affichee (coefficient applique) suivie de l'unite du template. */
static void display_value(const char *ieee, int cluster, int attr, char *out, size_t len)
{
    char raw[DEVDATA_VALUE_LEN];
    bool is_text;
    float coeff;
    out[0] = '\0';
    if (!attr_value(ieee, cluster, attr, NULL, raw, sizeof(raw), &is_text, &coeff)) return;
    const devdata_template_state_t *st = rules_tpl_state(rules_parse_ieee(ieee), cluster, attr);
    if (!is_text && zb_is_numeric(raw)) {
        double v = zb_number(raw) * coeff;
        if (coeff == 1.0f) snprintf(out, len, "%lld%s", (long long)v, usable_unit(st));
        else snprintf(out, len, "%.2f%s", v, usable_unit(st));
    } else {
        snprintf(out, len, "%s%s", raw, is_text ? usable_unit(st) : "");
    }
}

static void build_conditions_text(const rule_t *r, eval_ctx_t *x, char *out, size_t cap)
{
    size_t o = 0;
    out[0] = '\0';
    for (int i = 0; i < r->n_cond; i++) {
        const rule_cond_t *c = &r->cond[i];
        char line[200];
        line[0] = '\0';
        if (!strcmp(c->type, "energy")) {
            int k = energy_metric_index(c->metric);
            double num = 0;
            char text[32];
            bool is_text;
            if (k < 0 || !energy_value(x, c->metric, &num, text, sizeof(text), &is_text)) continue;
            if (is_text) snprintf(line, sizeof(line), "%s: %s", ENERGY_METRICS[k].label, text);
            else snprintf(line, sizeof(line), "%s: %.10g%s%s", ENERGY_METRICS[k].label, num,
                          ENERGY_METRICS[k].unit[0] ? " " : "", ENERGY_METRICS[k].unit);
        } else if (strcmp(c->type, "device") != 0 && strcmp(c->type, "device_compare") != 0) {
            if (!c->type[0]) continue;
            snprintf(line, sizeof(line), "%s: %s", c->type, c->value);
        } else {
            uint64_t ieee = rules_parse_ieee(c->IEEE);
            if (!ieee || !device_data_find(ieee)) continue;
            char val[64];
            if (c->subfield[0]) {
                char raw[DEVDATA_VALUE_LEN];
                if (device_data_get_value(ieee, (uint16_t)c->cluster, (uint16_t)(c->attribute & 0xFFFF),
                                          raw, sizeof(raw)) != ESP_OK || !raw[0]) continue;
                stge_field_label(c->subfield, stge_field_value(raw, c->subfield), val, sizeof(val));
            } else {
                display_value(c->IEEE, c->cluster, c->attribute, val, sizeof(val));
                if (!val[0]) continue;
            }
            char dev[48];
            rules_device_label(c->IEEE, dev, sizeof(dev));
            const devdata_template_state_t *st = rules_tpl_state(ieee, c->cluster, c->attribute);
            if (st && st->name[0] && strcmp(st->name, "null") != 0) {
                snprintf(line, sizeof(line), "%s - %s: %s", dev, st->name, val);
            } else {
                snprintf(line, sizeof(line), "%s: %s", dev, val);
            }
        }
        size_t l = strlen(line);
        if (o + l + 2 >= cap) break;
        if (o > 0) out[o++] = '\n';
        memcpy(out + o, line, l + 1);
        o += l;
    }
}

/** n-ieme condition d'appareil (device ou device_compare), n >= 1. */
static const rule_cond_t *device_cond_nth(const rule_t *r, int n)
{
    int k = 0;
    for (int i = 0; i < r->n_cond; i++) {
        if (!strcmp(r->cond[i].type, "device") || !strcmp(r->cond[i].type, "device_compare")) {
            if (++k == n) return &r->cond[i];
        }
    }
    return NULL;
}

static bool token_value(const rule_t *r, const char *tok, char *out, size_t len)
{
    if (!strcmp(tok, "rule")) {
        jcopy(out, len, r->name);
        return true;
    }
    if (!strcmp(tok, "date")) {
        rules_formatted_date(out, len);
        return true;
    }
    if (!strcmp(tok, "time")) {
        struct tm t;
        if (rules_local_time(&t)) snprintf(out, len, "%02d:%02d", t.tm_hour % 100, t.tm_min % 100);
        else jcopy(out, len, "");
        return true;
    }
    int n = 1;
    const char *base = tok;
    char head[16];
    const char *us = strchr(tok, '_');
    if (us) {
        size_t hl = (size_t)(us - tok);
        if (hl >= sizeof(head)) return false;
        memcpy(head, tok, hl);
        head[hl] = '\0';
        base = head;
        n = atoi(us + 1);
        if (n < 1) return false;
    }
    if (strcmp(base, "value") && strcmp(base, "device") && strcmp(base, "threshold")) return false;
    if (us && !strcmp(base, "threshold")) return false;   // {threshold} seulement pour la 1re
    const rule_cond_t *c = device_cond_nth(r, n);
    if (!c) return false;
    if (!strcmp(base, "value")) {
        display_value(c->IEEE, c->cluster, c->attribute, out, len);
    } else if (!strcmp(base, "device")) {
        rules_device_label(c->IEEE, out, len);
    } else {
        jcopy(out, len, c->value);
    }
    return true;
}

/** Remplace {rule} {date} {time} {value} {device} {threshold} {value_N} {device_N}. */
static void substitute(const rule_t *r, const char *in, char *out, size_t cap)
{
    size_t o = 0;
    const char *p = in;
    while (*p && o + 1 < cap) {
        if (*p == '{') {
            const char *e = strchr(p, '}');
            if (e && e - p > 1 && e - p < 24) {
                char tok[24], val[96];
                size_t tl = (size_t)(e - p - 1);
                memcpy(tok, p + 1, tl);
                tok[tl] = '\0';
                if (token_value(r, tok, val, sizeof(val))) {
                    size_t vl = strlen(val);
                    if (o + vl + 1 >= cap) vl = cap - o - 1;
                    memcpy(out + o, val, vl);
                    o += vl;
                    p = e + 1;
                    continue;
                }
            }
        }
        out[o++] = *p++;
    }
    out[o] = '\0';
}

static void notify_from_rule(const rule_t *r, const rule_action_t *a, eval_ctx_t *x)
{
    // Tampons de travail en PSRAM, propres a la tache "rules" (seule a evaluer)
    static char *title = NULL, *msg = NULL, *conds = NULL;
    if (!title) title = (char *)rules_psram_malloc(RULES_TITLE_LEN * 2);
    if (!msg)   msg   = (char *)rules_psram_malloc(RULES_NOTIF_LEN);
    if (!conds) conds = (char *)rules_psram_malloc(RULES_NOTIF_LEN);
    if (!title || !msg || !conds) return;

    substitute(r, a->title, title, RULES_TITLE_LEN * 2);
    substitute(r, a->message, msg, RULES_NOTIF_LEN);
    build_conditions_text(r, x, conds, RULES_NOTIF_LEN);
    if (conds[0]) {
        size_t l = strlen(msg);
        if (l > 0 && l + 2 < RULES_NOTIF_LEN) {
            msg[l++] = '\n';
            msg[l++] = '\n';
            msg[l] = '\0';
        }
        strlcat(msg, conds, RULES_NOTIF_LEN);
    }
    ESP_LOGI(TAG, "Action : notification '%s'", title);
    rules_notify_post(title, msg);
}

static void dispatch_action(const rule_t *r, const rule_action_t *a, eval_ctx_t *x)
{
    if (rule_action_is_radio(a)) {
        rules_job_enqueue(a, r->name);
    } else if (!strcmp(a->type, "notification")) {
        notify_from_rule(r, a, x);
    } else if (!strcmp(a->type, "actiongroup")) {
        // Le groupe est designe par son NOM : un index se decalerait a la
        // suppression d'un autre groupe.
        if (action_groups_run_by_name(a->actionName) < 0) {
            ESP_LOGW(TAG, "Groupe d'actions '%s' introuvable, desactive ou imbrique", a->actionName);
        }
    } else {
        ESP_LOGW(TAG, "Action de type inconnu '%s' (regle '%s')", a->type, r->name);
    }
}

// ----------------------------------------------------------------------------
// Machine a etats d'une regle (RulesManager::evaluateRule)
// ----------------------------------------------------------------------------

static int day_key(const eval_ctx_t *x)
{
    if (x->tm_ok) return x->tm.tm_year * 400 + x->tm.tm_yday;
    return (int)(rules_now_ms() / 86400000LL);
}

static bool exec_allowed(rule_t *r, int64_t now, const eval_ctx_t *x)
{
    if (r->cooldown > 0 && r->has_exec && now - r->last_exec < (int64_t)r->cooldown * 1000) return false;
    if (r->max_per_day > 0) {
        int today = day_key(x);
        if (r->exec_day != today) {
            r->exec_count = 0;
            r->exec_day = today;
        }
        if (r->exec_count >= r->max_per_day) return false;
        r->exec_count++;
    }
    return true;
}

static void fire(rule_t *r, int old, const char *date, int64_t now, eval_ctx_t *x)
{
    if (old != 1) set_status(r, 1, date);
    r->last_exec = now;
    r->has_exec = true;
    ESP_LOGI(TAG, "Regle '%s' declenchee", r->name);
    for (int i = 0; i < r->n_act; i++) dispatch_action(r, &r->act[i], x);
}

static void evaluate_rule(rule_t *r, eval_ctx_t *x)
{
    bool result = r->n_cond > 0;
    for (int i = 0; i < r->n_cond; i++) {
        bool ok = eval_cond(&r->cond[i], x);
        if (i == 0) result = ok;
        else if (!strcasecmp(r->cond[i].logic, "OR")) result = result || ok;
        else result = result && ok;          // AND, ou logique absente
    }

    int old = r->status;
    char date[24];
    rules_formatted_date(date, sizeof(date));
    int64_t now = rules_now_ms();

    if (result) {
        if (old == 1 && !r->repeat) return;
        if (r->duration > 0) {
            if (!r->pending) {
                r->pending = true;
                r->pending_since = now;
                if (old != 2) set_status(r, 2, date);
            } else if (now - r->pending_since + 2000 >= (int64_t)r->duration * 1000) {
                // Tolerance de 2 s : les regles timer ne sont evaluees qu'une fois par
                // minute ; sans elle un maintien de 60 s ne partait qu'a la 3e evaluation.
                r->pending = false;
                if (!exec_allowed(r, now, x)) return;
                fire(r, old, date, now, x);
            }
        } else {
            if (!exec_allowed(r, now, x)) return;
            fire(r, old, date, now, x);
        }
    } else {
        r->pending = false;
        if (old != 0) {
            set_status(r, 0, date);
            if (old == 1) {
                for (int i = 0; i < r->n_els; i++) dispatch_action(r, &r->els[i], x);
            }
        }
    }
}

void rules_process_event(const rules_evt_t *e)
{
    if (!lock_rules()) return;
    eval_ctx_t x;
    memset(&x, 0, sizeof(x));
    x.tm_ok = rules_local_time(&x.tm);
    for (int i = 0; i < s_count; i++) {
        rule_t *r = &s_rules[i];
        if (!r->enabled) continue;
        bool is_event = !strcmp(r->trig_mode, "event");
        if (e->kind == RULES_EVT_TICK) {
            // applyRules() : regles "timer" (ou sans mode)
            if (is_event || (r->trig_mode[0] && strcmp(r->trig_mode, "timer") != 0)) continue;
            evaluate_rule(r, &x);
        } else if (e->kind == RULES_EVT_ATTR) {
            if (!is_event || r->trig_ieee != e->ieee || (uint16_t)r->trig_cluster != e->cluster ||
                (uint16_t)(r->trig_attribute & 0xFFFF) != e->attr) continue;
            evaluate_rule(r, &x);
        }
    }
    unlock_rules();
}

// ----------------------------------------------------------------------------
// Points d'entree
// ----------------------------------------------------------------------------

void rules_on_attribute(uint64_t ieee, uint16_t cluster, uint16_t attr, const char *value)
{
    (void)value;
    if (!s_inited || !s_trig) return;
    bool match = false;
    portENTER_CRITICAL(&s_trig_mux);
    for (int i = 0; i < s_trig_count; i++) {
        if (s_trig[i].ieee == ieee && s_trig[i].cluster == cluster && s_trig[i].attr == attr) {
            match = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_trig_mux);
    if (!match) return;
    rules_evt_t e = {};
    e.kind = RULES_EVT_ATTR;
    e.ieee = ieee;
    e.cluster = cluster;
    e.attr = attr;
    rules_post_event(&e);
}

void rules_tick(void)
{
    if (!s_inited) return;
    rules_evt_t e = {};
    e.kind = RULES_EVT_TICK;
    rules_post_event(&e);
    save_status_if_dirty();
}

esp_err_t rules_init(void)
{
    if (s_inited) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    s_edit_lock = xSemaphoreCreateMutex();
    s_trig = (trig_key_t *)rules_psram_calloc(RULES_MAX, sizeof(trig_key_t));
    if (!s_lock || !s_edit_lock || !s_trig) return ESP_ERR_NO_MEM;
    load_from_file();
    load_status();
    action_groups_load();
    esp_err_t err = rules_exec_start();
    if (err != ESP_OK) return err;
    s_inited = true;
    return ESP_OK;
}

// ----------------------------------------------------------------------------
// API web
// ----------------------------------------------------------------------------

char *rules_list_json(void)
{
    if (!lock_rules()) return NULL;
    sb_t sb;
    sb_init(&sb, 1024 + (size_t)s_count * 768);
    sb_puts(&sb, "{\"rules\":[");
    for (int i = 0; i < s_count; i++) {
        ser_rule(&sb, &s_rules[i], true);
        sb_putc(&sb, ',');
    }
    sb_close(&sb, ']');
    sb_putc(&sb, ',');
    sb_kint(&sb, "max", RULES_MAX);
    sb_kbool(&sb, "fileError", s_file_bad);
    sb_close(&sb, '}');
    unlock_rules();
    return sb_take(&sb);
}

char *rules_get_json(const char *name, bool *found)
{
    *found = false;
    if (!name || !lock_rules()) return NULL;
    int k = find_rule(s_rules, s_count, name);
    if (k < 0) {
        unlock_rules();
        return NULL;
    }
    *found = true;
    sb_t sb;
    sb_init(&sb, 1024);
    ser_rule(&sb, &s_rules[k], true);
    unlock_rules();
    return sb_take(&sb);
}

bool rules_get_status(const char *name, int *status, char *last_date, size_t len)
{
    if (!name || !lock_rules()) return false;
    int k = find_rule(s_rules, s_count, name);
    if (k >= 0) {
        *status = s_rules[k].status < 0 ? 0 : s_rules[k].status;
        jcopy(last_date, len, s_rules[k].last_date);
    }
    unlock_rules();
    return k >= 0;
}

char *rules_raw_get(void)
{
    size_t len = 0;
    char *t = rules_read_file(RULES_FILE_PATH, &len);
    if (t) return t;
    t = (char *)rules_psram_malloc(16);
    if (t) strlcpy(t, "{\"rules\":[]}", 16);
    return t;
}

// Nom de regle : lettres, chiffres, espace, _ et - (comme la LiXee-Box, qui
// refusait par ailleurs l'espace a la modification mais l'acceptait a l'ajout).
static bool valid_name(const char *n, char *err, size_t errlen)
{
    size_t l = strlen(n);
    if (l == 0) {
        jcopy(err, errlen, "Le nom de la r\xC3\xA8gle ne peut pas \xC3\xAAtre vide");
        return false;
    }
    if (l >= RULES_NAME_LEN) {
        snprintf(err, errlen, "Nom trop long (%d caract\xC3\xA8res au plus)", RULES_NAME_LEN - 1);
        return false;
    }
    for (size_t i = 0; i < l; i++) {
        unsigned char c = (unsigned char)n[i];
        if (!isalnum(c) && c != '_' && c != '-' && c != ' ') {
            jcopy(err, errlen, "Le nom ne peut contenir que des lettres, chiffres, espaces, _ et -");
            return false;
        }
    }
    return true;
}

/** Ecrit rules.json depuis le jeu en memoire ; resynchronise depuis le fichier si echec. */
static rules_res_t write_rules_file(char *err, size_t errlen)
{
    if (!lock_rules()) return RULES_RES_NO_MEM;
    char *txt = ser_rules_file();
    unlock_rules();
    if (!txt) {
        jcopy(err, errlen, "M\xC3\xA9moire insuffisante");
        return RULES_RES_NO_MEM;
    }
    esp_err_t e = cfg_write_file(RULES_FILE_PATH, txt);
    free(txt);
    if (e != ESP_OK) {
        jcopy(err, errlen, "Erreur d'\xC3\xA9" "criture de rules.json");
        load_from_file();
        return RULES_RES_IO;
    }
    return RULES_RES_OK;
}

static bool edit_begin(void)
{
    return xSemaphoreTake(s_edit_lock, pdMS_TO_TICKS(5000)) == pdTRUE;
}

static void edit_end(void)
{
    xSemaphoreGive(s_edit_lock);
}

static rules_res_t refuse_bad_file(char *err, size_t errlen)
{
    jcopy(err, errlen, "rules.json est illisible : corrigez-le dans l'\xC3\xA9" "diteur JSON");
    return RULES_RES_BAD_REQUEST;
}

rules_res_t rules_add_json(const char *body, size_t len, char *err, size_t errlen)
{
    if (!s_inited) return RULES_RES_NO_MEM;
    rule_t nr;
    if (!parse_rule_text(body, len, &nr, NULL, 0)) {
        jcopy(err, errlen, "JSON invalide");
        return RULES_RES_BAD_REQUEST;
    }
    if (!valid_name(nr.name, err, errlen)) {
        rule_free_arrays(&nr);
        return RULES_RES_BAD_REQUEST;
    }
    if (!edit_begin()) {
        rule_free_arrays(&nr);
        return RULES_RES_NO_MEM;
    }
    rules_res_t res = RULES_RES_OK;
    if (s_file_bad) {
        res = refuse_bad_file(err, errlen);
    } else if (!lock_rules()) {
        res = RULES_RES_NO_MEM;
    } else {
        if (find_rule(s_rules, s_count, nr.name) >= 0) {
            jcopy(err, errlen, "Une r\xC3\xA8gle avec ce nom existe d\xC3\xA9j\xC3\xA0");
            res = RULES_RES_CONFLICT;
        } else if (s_count >= RULES_MAX) {
            snprintf(err, errlen, "Nombre maximal de r\xC3\xA8gles atteint (%d)", RULES_MAX);
            res = RULES_RES_FULL;
        } else {
            s_rules[s_count++] = nr;       // le tableau a toujours RULES_MAX places
            nr.cond = NULL;
            nr.act = nr.els = NULL;
            rebuild_triggers();
        }
        unlock_rules();
        if (res == RULES_RES_OK) res = write_rules_file(err, errlen);
    }
    edit_end();
    rule_free_arrays(&nr);
    return res;
}

rules_res_t rules_edit_json(const char *body, size_t len, char *err, size_t errlen)
{
    if (!s_inited) return RULES_RES_NO_MEM;
    rule_t nr;
    char old_name[RULES_NAME_LEN + 8];
    if (!parse_rule_text(body, len, &nr, old_name, sizeof(old_name))) {
        jcopy(err, errlen, "JSON invalide");
        return RULES_RES_BAD_REQUEST;
    }
    if (!old_name[0]) {
        rule_free_arrays(&nr);
        jcopy(err, errlen, "Ancien nom manquant");
        return RULES_RES_BAD_REQUEST;
    }
    if (!valid_name(nr.name, err, errlen)) {
        rule_free_arrays(&nr);
        return RULES_RES_BAD_REQUEST;
    }
    if (!edit_begin()) {
        rule_free_arrays(&nr);
        return RULES_RES_NO_MEM;
    }
    rules_res_t res = RULES_RES_OK;
    if (s_file_bad) {
        res = refuse_bad_file(err, errlen);
    } else if (!lock_rules()) {
        res = RULES_RES_NO_MEM;
    } else {
        int k = find_rule(s_rules, s_count, old_name);
        int other = find_rule(s_rules, s_count, nr.name);
        if (k < 0) {
            jcopy(err, errlen, "R\xC3\xA8gle non trouv\xC3\xA9" "e");
            res = RULES_RES_NOT_FOUND;
        } else if (other >= 0 && other != k) {
            jcopy(err, errlen, "Une r\xC3\xA8gle avec ce nom existe d\xC3\xA9j\xC3\xA0");
            res = RULES_RES_CONFLICT;
        } else {
            // Etat conserve, y compris en cas de renommage (perdu sur la LiXee-Box)
            state_copy(&nr, &s_rules[k]);
            rule_free_arrays(&s_rules[k]);
            s_rules[k] = nr;
            nr.cond = NULL;
            nr.act = nr.els = NULL;
            rebuild_triggers();
            s_status_dirty = true;
        }
        unlock_rules();
        if (res == RULES_RES_OK) res = write_rules_file(err, errlen);
    }
    edit_end();
    rule_free_arrays(&nr);
    return res;
}

rules_res_t rules_delete(const char *name)
{
    if (!s_inited || !name) return RULES_RES_BAD_REQUEST;
    if (!edit_begin()) return RULES_RES_NO_MEM;
    rules_res_t res = RULES_RES_OK;
    char err[8];
    if (s_file_bad) {
        res = RULES_RES_BAD_REQUEST;
    } else if (!lock_rules()) {
        res = RULES_RES_NO_MEM;
    } else {
        int k = find_rule(s_rules, s_count, name);
        if (k < 0) {
            res = RULES_RES_NOT_FOUND;
        } else {
            rule_free_arrays(&s_rules[k]);
            for (int i = k; i < s_count - 1; i++) s_rules[i] = s_rules[i + 1];
            s_count--;
            memset(&s_rules[s_count], 0, sizeof(rule_t));
            rebuild_triggers();
            s_status_dirty = true;   // l'entree de statusRules.json disparait
        }
        unlock_rules();
        if (res == RULES_RES_OK) res = write_rules_file(err, sizeof(err));
    }
    edit_end();
    if (res == RULES_RES_OK) save_status_if_dirty();
    return res;
}

rules_res_t rules_set_enabled(const char *name, bool enabled)
{
    if (!s_inited || !name) return RULES_RES_BAD_REQUEST;
    if (!edit_begin()) return RULES_RES_NO_MEM;
    rules_res_t res = RULES_RES_OK;
    char err[8];
    if (s_file_bad) {
        res = RULES_RES_BAD_REQUEST;
    } else if (!lock_rules()) {
        res = RULES_RES_NO_MEM;
    } else {
        int k = find_rule(s_rules, s_count, name);
        if (k < 0) {
            res = RULES_RES_NOT_FOUND;
        } else {
            s_rules[k].enabled = enabled;
            rebuild_triggers();
        }
        unlock_rules();
        if (res == RULES_RES_OK) res = write_rules_file(err, sizeof(err));
    }
    edit_end();
    return res;
}

rules_res_t rules_raw_set(const char *text, size_t len, char *err, size_t errlen)
{
    if (!s_inited) return RULES_RES_NO_MEM;
    rule_t *arr = NULL;
    int count = 0, errors = 0;
    bool found = false;
    if (!parse_rules_doc(text, len, &arr, &count, &errors, &found)) {
        jcopy(err, errlen, "JSON invalide");
        return RULES_RES_BAD_REQUEST;
    }
    if (!found || errors) {
        set_free(arr, count);
        if (!found) jcopy(err, errlen, "Cl\xC3\xA9 \"rules\" absente");
        else snprintf(err, errlen, "%d r\xC3\xA8gle(s) invalide(s) ou au-del\xC3\xA0 de %d", errors, RULES_MAX);
        return RULES_RES_BAD_REQUEST;
    }
    if (!edit_begin()) {
        set_free(arr, count);
        return RULES_RES_NO_MEM;
    }
    // Le texte est enregistre tel quel (mise en forme de l'utilisateur conservee)
    char *copy = (char *)rules_psram_malloc(len + 1);
    rules_res_t res = RULES_RES_OK;
    if (!copy) {
        res = RULES_RES_NO_MEM;
    } else {
        memcpy(copy, text, len);
        copy[len] = '\0';
        if (cfg_write_file(RULES_FILE_PATH, copy) != ESP_OK) {
            jcopy(err, errlen, "Erreur d'\xC3\xA9" "criture de rules.json");
            res = RULES_RES_IO;
        }
        free(copy);
    }
    if (res == RULES_RES_OK) {
        s_file_bad = false;
        install_set(arr, count, NULL, NULL);
        arr = NULL;
        ESP_LOGI(TAG, "rules.json remplace : %d regle(s)", count);
    }
    edit_end();
    if (arr) set_free(arr, count);
    return res;
}

// ----------------------------------------------------------------------------
// Catalogue des appareils pour l'editeur
// ----------------------------------------------------------------------------

char *rules_devices_json(void)
{
    sb_t sb;
    sb_init(&sb, 16 * 1024);
    sb_putc(&sb, '{');
    int n = zigate_devices_count();
    for (int i = 0; i < n; i++) {
        const zigate_device_t *z = zigate_devices_get(i);
        if (!z || !z->ieee_addr) continue;
        char ieee[20];
        snprintf(ieee, sizeof(ieee), "%016llX", (unsigned long long)z->ieee_addr);
        const device_data_t *dd = device_data_find(z->ieee_addr);
        const devdata_template_t *t = device_data_get_template(z->ieee_addr);
        if (t && !t->loaded) t = NULL;

        sb_key(&sb, ieee);
        sb_putc(&sb, '{');
        sb_key(&sb, "INFO");
        sb_putc(&sb, '{');
        sb_kstr(&sb, "alias", z->alias[0] ? z->alias : (dd ? dd->info.alias : ""));
        sb_kstr(&sb, "model", z->model);
        sb_kstr(&sb, "manufacturer", z->manufacturer);
        sb_kint(&sb, "device_id", z->device_id);
        sb_kint(&sb, "endpoint", z->endpoint ? z->endpoint : 1);
        sb_close(&sb, '}');
        sb_putc(&sb, ',');

        sb_key(&sb, "status");
        sb_putc(&sb, '[');
        for (int k = 0; t && k < t->state_count; k++) {
            const devdata_template_state_t *s = &t->states[k];
            char cl[8];
            snprintf(cl, sizeof(cl), "%04X", (unsigned)s->cluster);
            sb_putc(&sb, '{');
            sb_kstr(&sb, "name", s->name);
            sb_kstr(&sb, "cluster", cl);
            sb_kint(&sb, "attribut", s->attribute);
            sb_kstr(&sb, "unit", s->unit);
            sb_knum(&sb, "coefficient", s->coefficient);
            sb_kstr(&sb, "type", s->type);
            sb_close(&sb, '}');
            sb_putc(&sb, ',');
        }
        sb_close(&sb, ']');
        sb_putc(&sb, ',');

        sb_key(&sb, "action");
        sb_putc(&sb, '[');
        for (int k = 0; t && k < t->action_count; k++) {
            const devdata_template_action_t *a = &t->actions[k];
            sb_putc(&sb, '{');
            sb_kstr(&sb, "name", a->name);
            sb_kint(&sb, "endpoint", a->endpoint);
            sb_kint(&sb, "command", a->command);
            sb_kint(&sb, "visible", a->visible ? 1 : 0);
            sb_close(&sb, '}');
            sb_putc(&sb, ',');
        }
        sb_close(&sb, ']');
        sb_putc(&sb, ',');

        // Valeurs courantes des etats du template, cle "CCCC:attribut"
        sb_key(&sb, "values");
        sb_putc(&sb, '{');
        for (int k = 0; t && k < t->state_count; k++) {
            const devdata_template_state_t *s = &t->states[k];
            char raw[DEVDATA_VALUE_LEN];
            if (device_data_get_value(z->ieee_addr, s->cluster, s->attribute, raw, sizeof(raw)) != ESP_OK) continue;
            char key[16];
            snprintf(key, sizeof(key), "%04X:%u", (unsigned)s->cluster, (unsigned)s->attribute);
            const devdata_attr_t *da = device_data_get_attr(z->ieee_addr, s->cluster, s->attribute);
            if (strcmp(s->type, "string") == 0 || (da && da->type >= 0x41 && da->type <= 0x44)) {
                char txt[DEVDATA_VALUE_LEN];
                rules_hex_to_text(raw, txt, sizeof(txt));
                sb_kstr(&sb, key, txt);
            } else {
                sb_kstr(&sb, key, raw);
            }
        }
        sb_close(&sb, '}');
        sb_close(&sb, '}');
        sb_putc(&sb, ',');
    }
    sb_close(&sb, '}');
    return sb_take(&sb);
}
