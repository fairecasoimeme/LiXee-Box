// ============================================================================
// Thermostats virtuels - portage de virtualThermostat.cpp (LiXee-Box v2.25)
// Voir thermostat.h. Les commentaires "LiXee-Box" signalent les choix repris
// tels quels de la version Arduino.
// ============================================================================

#include "thermostat.h"
#include "presence.h"
#include "zigate_devices.h"
#include "zigate_driver.h"
#include "zigate_actions.h"
#include "device_data.h"
#include "energy_service.h"
#include "ntp_sync.h"
#include "matter_bridge.h"
#include "matter_device_manager.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static const char *TAG = "THERMO";

#define CFG_PATH        "/littlefs/config/thermostats.json"
#define CFG_TMP         "/littlefs/config/tmpThermostats.json"
#define CFG_MAX_SIZE    20000

// 3 h : un capteur sur pile n'emet souvent que sur variation de temperature.
// L'ancien defaut (60 min) est migre au chargement (LiXee-Box).
#define SENSOR_TIMEOUT_DEFAULT_SEC  10800
#define SENSOR_TIMEOUT_OLD_DEFAULT  3600

#define TICK_MS                30000
#define PRESENCE_WINDOW_S      1800      // presence : mouvement dans les 30 dernieres minutes
#define PRESENCE_SLOTS         16
#define HYSTERESIS_C           0.3f      // bande morte des appareils pilotes par actions
#define MATTER_UID_BASE        0x4C58544800000000ULL     // UniqueID Matter : "LXTH" + uid de la zone

typedef struct {
    // --- Configuration (persistee) ---
    char  name[32];
    char  sensor[20];                    // capteur de temperature (0402, ou 0201 attr 0)
    char  actuator[20];                  // appareil principal : son template fournit les actions
    char  extra[THERMO_MAX_EXTRA][20];   // prises supplementaires (meme commande)
    int   extra_count;
    char  action_heat[40];               // action "chauffe" ("" : on/off 0006)
    char  action_cool[40];               // action "refroidit" (clim reversible)
    char  action_off[40];                // action au repos
    char  presence[20];                  // capteur de presence 0406 (optionnel)
    char  open[THERMO_MAX_OPEN][20];     // contacts d'ouverture IAS 0500 (optionnels)
    int   open_count;
    bool  enabled;
    bool  heating;                       // true chauffage, false rafraichissement
    float setpoint;
    float frost_temp;
    bool  frost_mode;                    // bascule hors-gel active (consigne = frost_temp)
    float setpoint_saved;                // consigne memorisee avant le hors-gel
    int   tpi_cycle_s;
    float kp, ki;
    int   min_on_s, min_off_s;
    int   sensor_timeout_s;
    int   oper_mode;                     // 0 toujours, 1 plages horaires, 2 tarif Linky
    char  schedule[192];                 // "[jours/]HH:MM-HH:MM,..." (jours : 1 lundi .. 7 dimanche)
    char  tariff_periods[48];            // "256,260,264"
    int   priority;                      // reserve (LiXee-Box : futur delestage)
    int   nominal_w;                     // reserve
    int   force_mode;                    // 0 auto, 1 marche forcee, 2 arret force (persiste)
    uint32_t uid;                        // identifiant stable (UniqueID Matter)
    int   matter_ep;                     // endpoint Matter memorise (0 : aucun)

    // --- Etat (RAM) ---
    float   current_temp;
    bool    sensor_valid;
    uint8_t sensor_reason;               // 0 OK, 1 introuvable, 2 sans temperature, 3 trop ancien
    long    sensor_age_s;                // -1 inconnu
    bool    occupied;
    bool    window_open;
    bool    sched_active;
    float   duty;                        // 0..1
    bool    output;                      // etat commande
    bool    actual_on;                   // etat reel de l'appareil (sinon commande)
    float   integral;
    int64_t cycle_start_ms;              // 0 : cycle a (re)demarrer
    int64_t last_switch_ms;              // 0 : aucune commande depuis le demarrage
    // Thermostat Matter : endpoint cree a ce demarrage, derniere valeurs publiees
    bool    m_created;
    uint8_t m_feat;
    bool    m_pushed;
    bool    m_valid;
    int32_t m_temp, m_sp, m_mode, m_run;
} zone_t;

static zone_t *s_z;
static int s_count;
static SemaphoreHandle_t s_lock;

static struct { uint64_t ieee; int64_t last_s; } s_pres[PRESENCE_SLOTS];
static portMUX_TYPE s_pres_mux = portMUX_INITIALIZER_UNLOCKED;

static void lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

// ----------------------------------------------------------------------------
// Outils
// ----------------------------------------------------------------------------

static uint64_t parse_ieee(const char *s)
{
    return (s && s[0]) ? strtoull(s, NULL, 16) : 0;
}

/** IEEE en 16 chiffres hexadecimaux majuscules, "" si invalide. */
static void copy_ieee(char *dst, const char *src)
{
    dst[0] = '\0';
    if (!src) return;
    size_t n = strlen(src);
    if (n == 0 || n > 16) return;
    for (size_t i = 0; i < n; i++) if (!isxdigit((unsigned char)src[i])) return;
    snprintf(dst, 20, "%016llX", (unsigned long long)strtoull(src, NULL, 16));
}

/** Valeur hexa stockee d'un attribut, false si absente ou vide. */
static bool get_hex(uint64_t ieee, uint16_t cl, uint16_t at, char *out, size_t len)
{
    out[0] = '\0';
    if (!ieee) return false;
    return device_data_get_value(ieee, cl, at, out, len) == ESP_OK && out[0];
}

static bool action_based(const zone_t *t)
{
    return t->action_heat[0] || t->action_cool[0] || t->action_off[0];
}

/** Coefficient de l'etat (cluster, 0) du template, 0,01 a defaut (LiXee-Box). */
static float temp_coef(uint64_t ieee, uint16_t cluster)
{
    const devdata_template_t *tp = device_data_get_template(ieee);
    for (int i = 0; tp && i < tp->state_count; i++) {
        if (tp->states[i].cluster == cluster && tp->states[i].attribute == 0 && tp->states[i].coefficient != 0.0f) {
            return tp->states[i].coefficient;
        }
    }
    return 0.01f;
}

/** Libelle d'un appareil : alias, sinon IEEE. */
static void device_label(const char *ieee_s, char *out, size_t len)
{
    out[0] = '\0';
    uint64_t ieee = parse_ieee(ieee_s);
    if (!ieee) return;
    const zigate_device_t *d = zigate_devices_find_by_ieee(ieee);
    if (d && d->alias[0]) { strlcpy(out, d->alias, len); return; }
    const device_data_t *dd = device_data_find(ieee);
    if (dd && dd->info.alias[0]) { strlcpy(out, dd->info.alias, len); return; }
    strlcpy(out, ieee_s, len);
}

/** Identifiant stable suivant (plus grand uid des zones + 1). */
static uint32_t next_uid(void)
{
    uint32_t m = 0;
    for (int i = 0; i < s_count; i++) if (s_z[i].uid > m) m = s_z[i].uid;
    return m + 1;
}

static void zone_defaults(zone_t *t)
{
    memset(t, 0, sizeof(*t));
    t->enabled = true;
    t->heating = true;
    t->setpoint = 19.0f;
    t->frost_temp = 7.0f;
    t->setpoint_saved = 19.0f;
    t->tpi_cycle_s = 900;
    t->kp = 0.8f;
    t->ki = 0.1f;
    t->min_on_s = 180;
    t->min_off_s = 180;
    t->sensor_timeout_s = SENSOR_TIMEOUT_DEFAULT_SEC;
    t->sensor_reason = 1;
    t->sensor_age_s = -1;
    t->occupied = true;
    t->sched_active = true;
}

// ----------------------------------------------------------------------------
// Presence (handleOccupancyChange / getCurrentPresenceStatus de la LiXee-Box) :
// une zone est occupee si son capteur a signale un mouvement dans les 30 min.
// ----------------------------------------------------------------------------

void thermostat_note_occupancy(uint64_t ieee, bool occupied)
{
    presence_note(ieee, occupied);               // historique (barres du graphe d'energie)
    if (!occupied || !ieee) return;              // seuls les evenements positifs comptent
    int64_t now = esp_timer_get_time() / 1000000;
    portENTER_CRITICAL(&s_pres_mux);
    int slot = -1, oldest = 0;
    for (int i = 0; i < PRESENCE_SLOTS; i++) {
        if (s_pres[i].ieee == ieee) { slot = i; break; }
        if (s_pres[i].last_s < s_pres[oldest].last_s) oldest = i;
    }
    if (slot < 0) slot = oldest;
    s_pres[slot].ieee = ieee;
    s_pres[slot].last_s = now;
    portEXIT_CRITICAL(&s_pres_mux);
}

bool thermostat_presence_recent(uint64_t ieee)
{
    int64_t now = esp_timer_get_time() / 1000000;
    bool occ = false;
    portENTER_CRITICAL(&s_pres_mux);
    for (int i = 0; i < PRESENCE_SLOTS; i++) {
        if (!s_pres[i].ieee || (ieee && s_pres[i].ieee != ieee)) continue;
        if (now - s_pres[i].last_s < PRESENCE_WINDOW_S) occ = true;
    }
    portEXIT_CRITICAL(&s_pres_mux);
    return occ;
}

static bool zone_occupied(const zone_t *t)
{
    uint64_t ieee = parse_ieee(t->presence);
    if (!ieee) return true;                      // pas de capteur de presence
    int64_t now = esp_timer_get_time() / 1000000;
    bool occ = false;
    portENTER_CRITICAL(&s_pres_mux);
    for (int i = 0; i < PRESENCE_SLOTS; i++) {
        if (s_pres[i].ieee == ieee) { occ = (now - s_pres[i].last_s) < PRESENCE_WINDOW_S; break; }
    }
    portEXIT_CRITICAL(&s_pres_mux);
    return occ;
}

// ----------------------------------------------------------------------------
// Capteurs de la zone
// ----------------------------------------------------------------------------

/** Au moins un contact ouvert (IAS Zone 0500 attr 2, bit 0). */
static bool zone_window_open(const zone_t *t)
{
    char v[DEVDATA_VALUE_LEN];
    for (int k = 0; k < t->open_count; k++) {
        if (get_hex(parse_ieee(t->open[k]), 0x0500, 0x0002, v, sizeof(v)) && (strtol(v, NULL, 16) & 0x1)) return true;
    }
    return false;
}

/** La zone peut-elle fonctionner maintenant ? (plages horaires / tarif Linky) */
static bool zone_schedule_active(const zone_t *t)
{
    if (t->oper_mode == 0) return true;

    if (t->oper_mode == 2) {
        if (!t->tariff_periods[0]) return true;          // aucune restriction
        char cur[12], list[sizeof(t->tariff_periods) + 2];
        snprintf(cur, sizeof(cur), ",%u,", energy_tariff_current_index());
        snprintf(list, sizeof(list), ",%s,", t->tariff_periods);
        return strstr(list, cur) != NULL;
    }

    // Plages "[jours/]HH:MM-HH:MM" separees par des virgules. Jours : chiffres
    // 1 (lundi) a 7 (dimanche), ex. "12345/06:00-09:00,67/08:00-23:00" ; sans
    // jours, la plage vaut pour tous les jours (format de la LiXee-Box).
    // Debut = fin : toute la journee. Plage qui passe minuit : sa partie
    // apres minuit appartient au jour de son debut.
    if (!t->schedule[0]) return true;
    time_t tt = time(NULL);
    struct tm lt;
    localtime_r(&tt, &lt);
    int now = lt.tm_hour * 60 + lt.tm_min;
    int today = lt.tm_wday == 0 ? 7 : lt.tm_wday;           // 1 lundi .. 7 dimanche
    int yesterday = today == 1 ? 7 : today - 1;
    char buf[sizeof(t->schedule)];
    strlcpy(buf, t->schedule, sizeof(buf));
    char *save = NULL;
    for (char *seg = strtok_r(buf, ",", &save); seg; seg = strtok_r(NULL, ",", &save)) {
        while (*seg == ' ') seg++;
        unsigned days = 0;
        char *slash = strchr(seg, '/');
        if (slash) {
            for (const char *p = seg; p < slash; p++) if (*p >= '1' && *p <= '7') days |= 1u << (*p - '0');
            seg = slash + 1;
            while (*seg == ' ') seg++;
        }
        if (!days) days = 0xFE;                              // tous les jours
        char *dash = strchr(seg, '-');
        if (!dash || dash == seg) continue;
        *dash = '\0';
        const char *a = seg, *b = dash + 1;
        while (*b == ' ') b++;
        // "HH:MM" : heures sur 2 caracteres, minutes apres le separateur (LiXee-Box)
        int am = atoi(a) * 60 + (strlen(a) > 3 ? atoi(a + 3) : 0);
        int bm = atoi(b) * 60 + (strlen(b) > 3 ? atoi(b + 3) : 0);
        bool d_today = days & (1u << today), d_yest = days & (1u << yesterday);
        bool in;
        if (am == bm)     in = d_today;
        else if (am < bm) in = d_today && now >= am && now < bm;
        else              in = (d_today && now >= am) || (d_yest && now < bm);     // minuit
        if (in) return true;
    }
    return false;
}

/** Etat reel de l'appareil principal : 1 actif, 0 inactif, -1 inconnu. */
static int read_actuator_state(const zone_t *t)
{
    uint64_t ieee = parse_ieee(t->actuator);
    if (!ieee || !zigate_devices_find_by_ieee(ieee)) return -1;
    char v[DEVDATA_VALUE_LEN];
    // Clim / thermostat : System Mode reel (0201/0x001C), meme pilote par actions
    if (get_hex(ieee, 0x0201, 0x001C, v, sizeof(v))) return strtol(v, NULL, 16) != 0 ? 1 : 0;
    // Pilote par actions sans System Mode lisible (fil pilote...) : inconnu
    if (action_based(t)) return -1;
    // Prise / relais : on/off 0006
    if (!get_hex(ieee, 0x0006, 0x0000, v, sizeof(v))) return -1;
    return strtol(v, NULL, 16) != 0 ? 1 : 0;
}

static void refresh_sensors(zone_t *t)
{
    bool valid = false;
    float tc = t->current_temp;
    t->sensor_reason = 1;                        // introuvable, sauf preuve du contraire
    t->sensor_age_s = -1;
    uint64_t ieee = parse_ieee(t->sensor);
    const zigate_device_t *d = ieee ? zigate_devices_find_by_ieee(ieee) : NULL;
    if (d) {
        uint32_t last_seen = d->last_seen, last_rx = d->last_rx_s;
        t->sensor_reason = 2;                    // trouve, mais aucune temperature lue
        uint16_t cl = 0x0402;
        char raw[DEVDATA_VALUE_LEN];
        bool ok = get_hex(ieee, 0x0402, 0x0000, raw, sizeof(raw));
        if (!ok && get_hex(ieee, 0x0201, 0x0000, raw, sizeof(raw))) { ok = true; cl = 0x0201; }
        if (ok) {
            tc = (int16_t)strtol(raw, NULL, 16) * temp_coef(ieee, cl);
            // Fraicheur : derniere trame quelle qu'elle soit (temperature,
            // humidite, batterie...) ; l'echec d'un envoi VERS le capteur
            // n'est pas pris en compte (LiXee-Box)
            bool fresh = true;
            long timeout = (t->sensor_timeout_s > 0 ? t->sensor_timeout_s : SENSOR_TIMEOUT_DEFAULT_SEC) + 60;
            time_t now = time(NULL);
            long age = -1;
            if (ntp_sync_is_synced() && last_seen > 1600000000 && now >= (time_t)last_seen) age = (long)(now - last_seen);
            else if (last_rx) age = (long)(zigate_uptime_s() - last_rx);
            if (age >= 0) {
                t->sensor_age_s = age;
                if (age > timeout) fresh = false;
            }
            valid = fresh;
            t->sensor_reason = fresh ? 0 : 3;
        }
    }
    t->current_temp = tc;
    t->sensor_valid = valid;
    t->occupied = zone_occupied(t);
    t->window_open = zone_window_open(t);
    t->sched_active = zone_schedule_active(t);
    // Etat affiche : etat reel lu sur l'appareil, a defaut l'etat commande
    int act = read_actuator_state(t);
    t->actual_on = act >= 0 ? act != 0 : t->output;
}

// ----------------------------------------------------------------------------
// Commandes
// ----------------------------------------------------------------------------

/** Action nommee du template de l'appareil (sans la casse). */
static bool fire_device_action(uint64_t ieee, const char *name)
{
    if (!ieee || !name || !name[0]) return false;
    const zigate_device_t *d = zigate_devices_find_by_ieee(ieee);
    const devdata_template_t *tp = device_data_get_template(ieee);
    if (!d || !tp) return false;
    uint16_t sa = d->short_addr;
    uint8_t dep = d->endpoint ? d->endpoint : 1;
    for (int i = 0; i < tp->action_count; i++) {
        const devdata_template_action_t *a = &tp->actions[i];
        if (strcasecmp(a->name, name) != 0) continue;
        esp_err_t err = zigate_action_send(a->command, sa, a->endpoint ? a->endpoint : dep,
                                           a->value, a->cluster, a->manufacturer_code);
        if (err != ESP_OK) ESP_LOGW(TAG, "Action %s -> 0x%04X : %s", name, sa, esp_err_to_name(err));
        return true;
    }
    return false;
}

/** Commande d'UN appareil : action si configuree, sinon on/off 0006. */
static void fire_actuator_one(const char *ieee_s, bool on, const char *name)
{
    uint64_t ieee = parse_ieee(ieee_s);
    if (!ieee) return;
    if (name[0] && fire_device_action(ieee, name)) return;
    const zigate_device_t *d = zigate_devices_find_by_ieee(ieee);
    if (!d) return;
    zigate_send_onoff(d->short_addr, d->endpoint ? d->endpoint : 1, on ? 1 : 0);
}

static void send_actuator(zone_t *t, bool on)
{
    const char *name = on ? (t->heating ? t->action_heat : t->action_cool) : t->action_off;
    ESP_LOGI(TAG, "[Action] thermostat \"%s\" : %s -> %s (%d appareil(s))", t->name,
             on ? "marche" : "arret", name[0] ? name : "on/off", 1 + t->extra_count);
    fire_actuator_one(t->actuator, on, name);
    // Un appareil declare deux fois ne recoit la commande qu'une fois (une
    // clim bipait deux fois)
    for (int k = 0; k < t->extra_count; k++) {
        const char *ieee = t->extra[k];
        bool dup = strcmp(ieee, t->actuator) == 0;
        for (int j = 0; j < k && !dup; j++) dup = strcmp(ieee, t->extra[j]) == 0;
        if (dup) {
            ESP_LOGI(TAG, "zone \"%s\" : %s declare deux fois, commande envoyee une seule fois", t->name, ieee);
            continue;
        }
        fire_actuator_one(ieee, on, name);
    }
}

/**
 * Aligne l'appareil sur l'etat voulu, commande envoyee au seul changement.
 * Prise : comparaison a l'etat reel (corrige un changement manuel). Appareil
 * pilote par actions : comparaison au dernier etat commande, pour ne pas se
 * battre avec le thermostat interne d'une clim.
 */
static void apply_output(zone_t *t, bool desired, int64_t now)
{
    bool current;
    if (action_based(t)) {
        if (t->last_switch_ms == 0) {
            // Rien envoye depuis le demarrage : etat reel comme reference
            // (pas de HEAT/COOL renvoye a chaque redemarrage) ; inconnu, on
            // suppose l'etat vise atteint en attendant un rapport
            int actual = read_actuator_state(t);
            current = actual >= 0 ? actual != 0 : desired;
            t->output = current;
        } else {
            current = t->output;
        }
    } else {
        int actual = read_actuator_state(t);
        current = actual >= 0 ? actual != 0 : t->output;
    }

    if (desired != current) {
        int64_t min_hold = (int64_t)(current ? t->min_on_s : t->min_off_s) * 1000;
        // Anti court-cycle (sauf premier changement) ; output n'est PAS
        // modifie : la bascule sera retentee au prochain passage
        if (t->last_switch_ms != 0 && now - t->last_switch_ms < min_hold) return;
        send_actuator(t, desired);
        t->output = desired;
        t->last_switch_ms = now;
    } else {
        t->output = desired;
    }
}

static void regulation_tick(void)
{
    int64_t now = now_ms();
    for (int i = 0; i < s_count; i++) {
        zone_t *t = &s_z[i];
        if (!t->enabled) {
            if (t->output) {                     // zone qui vient d'etre desactivee
                send_actuator(t, false);
                t->output = false;
                t->last_switch_ms = now;
            }
            continue;
        }

        refresh_sensors(t);

        // Au demarrage, adopter l'etat reel de la clim avant toute decision
        if (t->last_switch_ms == 0 && action_based(t)) {
            int real = read_actuator_state(t);
            if (real >= 0) t->output = real != 0;
        }

        float tc = t->current_temp;
        bool desired;
        if (!t->sensor_valid) {
            // Capteur HS : arret de securite, sauf marche forcee
            desired = (t->force_mode == 1);
            t->duty = 0.0f;
            t->integral = 0.0f;
        } else {
            bool frost = t->heating && tc < t->frost_temp;
            bool inhibit = (t->window_open || !t->occupied || !t->sched_active) && !frost;
            if (frost) {
                desired = true;
            } else if (t->force_mode == 1) {
                desired = true;
            } else if (t->force_mode == 2) {
                desired = false;
            } else if (inhibit) {
                desired = false;                 // integrale gelee
            } else if (action_based(t)) {
                float error = t->heating ? (t->setpoint - tc) : (tc - t->setpoint);
                if (error > HYSTERESIS_C) desired = true;
                else if (error < -HYSTERESIS_C) desired = false;
                else desired = t->output;
                t->duty = desired ? 1.0f : 0.0f;
            } else {
                // TPI : rapport cyclique recalcule a chaque debut de cycle
                if (t->cycle_start_ms == 0 || now - t->cycle_start_ms >= (int64_t)t->tpi_cycle_s * 1000) {
                    t->cycle_start_ms = now;
                    float error = t->heating ? (t->setpoint - tc) : (tc - t->setpoint);
                    t->integral += error;
                    if (t->ki > 0.0f) {          // anti-windup : Ki x integrale dans [0, 1]
                        float imax = 1.0f / t->ki;
                        if (t->integral > imax) t->integral = imax;
                        if (t->integral < 0.0f) t->integral = 0.0f;
                    } else {
                        t->integral = 0.0f;
                    }
                    float duty = t->kp * error + t->ki * t->integral;
                    t->duty = duty < 0.0f ? 0.0f : (duty > 1.0f ? 1.0f : duty);
                }
                int64_t on_ms = (int64_t)(t->duty * (float)t->tpi_cycle_s * 1000.0f);
                desired = t->duty > 0.0f && (now - t->cycle_start_ms) < on_ms;
            }
        }
        apply_output(t, desired, now);
    }
}

// ----------------------------------------------------------------------------
// Persistance (format de /config/thermostats.json de la LiXee-Box)
// ----------------------------------------------------------------------------

static void j_str(const cJSON *o, const char *k, char *dst, size_t len)
{
    const cJSON *it = cJSON_GetObjectItem(o, k);
    if (cJSON_IsString(it)) strlcpy(dst, it->valuestring, len);
}
static void j_num(const cJSON *o, const char *k, float *dst)
{
    const cJSON *it = cJSON_GetObjectItem(o, k);
    if (cJSON_IsNumber(it) && isfinite(it->valuedouble)) *dst = (float)it->valuedouble;
}
static void j_int(const cJSON *o, const char *k, int *dst)
{
    const cJSON *it = cJSON_GetObjectItem(o, k);
    if (cJSON_IsNumber(it)) *dst = it->valueint;
}
static void j_bool(const cJSON *o, const char *k, bool *dst)
{
    const cJSON *it = cJSON_GetObjectItem(o, k);
    if (cJSON_IsBool(it)) *dst = cJSON_IsTrue(it);
    else if (cJSON_IsNumber(it)) *dst = it->valueint != 0;
}
static int j_ieee_list(const cJSON *o, const char *k, char (*dst)[20], int max, const char *skip)
{
    int n = 0;
    const cJSON *arr = cJSON_GetObjectItem(o, k);
    const cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (n >= max) break;
        if (!cJSON_IsString(it)) continue;
        char v[20];
        copy_ieee(v, it->valuestring);
        if (!v[0] || (skip && !strcmp(v, skip))) continue;
        strlcpy(dst[n++], v, 20);
    }
    return n;
}

/** Reel simple precision sans decimales parasites (0.8 et non 0.800000011920929). */
static double r3(float v) { return round((double)v * 1000.0) / 1000.0; }

static bool save_locked(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "thermostats");
    for (int i = 0; i < s_count; i++) {
        const zone_t *t = &s_z[i];
        if (!t->name[0] && !t->sensor[0] && !t->actuator[0]) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", t->name);
        cJSON_AddStringToObject(o, "sensorIEEE", t->sensor);
        cJSON_AddStringToObject(o, "actuatorIEEE", t->actuator);
        cJSON *ex = cJSON_AddArrayToObject(o, "actuatorsExtra");
        for (int k = 0; k < t->extra_count; k++) cJSON_AddItemToArray(ex, cJSON_CreateString(t->extra[k]));
        cJSON_AddStringToObject(o, "actionHeat", t->action_heat);
        cJSON_AddStringToObject(o, "actionCool", t->action_cool);
        cJSON_AddStringToObject(o, "actionOff", t->action_off);
        cJSON_AddStringToObject(o, "presenceIEEE", t->presence);
        cJSON *op = cJSON_AddArrayToObject(o, "openSensors");
        for (int k = 0; k < t->open_count; k++) cJSON_AddItemToArray(op, cJSON_CreateString(t->open[k]));
        cJSON_AddBoolToObject(o, "enabled", t->enabled);
        cJSON_AddBoolToObject(o, "heating", t->heating);
        cJSON_AddNumberToObject(o, "setpoint", r3(t->setpoint));
        cJSON_AddNumberToObject(o, "frostTemp", r3(t->frost_temp));
        cJSON_AddBoolToObject(o, "frostMode", t->frost_mode);
        cJSON_AddNumberToObject(o, "forceMode", t->force_mode);
        cJSON_AddNumberToObject(o, "setpointSaved", r3(t->setpoint_saved));
        cJSON_AddNumberToObject(o, "tpiCycleSec", t->tpi_cycle_s);
        cJSON_AddNumberToObject(o, "tpiKp", r3(t->kp));
        cJSON_AddNumberToObject(o, "tpiKi", r3(t->ki));
        cJSON_AddNumberToObject(o, "minOnSec", t->min_on_s);
        cJSON_AddNumberToObject(o, "minOffSec", t->min_off_s);
        cJSON_AddNumberToObject(o, "sensorTimeoutSec", t->sensor_timeout_s);
        cJSON_AddNumberToObject(o, "operMode", t->oper_mode);
        cJSON_AddStringToObject(o, "schedule", t->schedule);
        cJSON_AddStringToObject(o, "tariffPeriods", t->tariff_periods);
        cJSON_AddNumberToObject(o, "priority", t->priority);
        cJSON_AddNumberToObject(o, "nominalPowerW", t->nominal_w);
        cJSON_AddNumberToObject(o, "uid", t->uid);
        cJSON_AddNumberToObject(o, "matterEp", t->matter_ep);
        cJSON_AddItemToArray(arr, o);
    }
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) return false;
    bool ok = false;
    FILE *f = fopen(CFG_TMP, "w");
    if (f) {
        size_t n = strlen(txt);
        ok = fwrite(txt, 1, n, f) == n;
        ok = (fclose(f) == 0) && ok;
        if (ok) {
            remove(CFG_PATH);                    // rename() n'ecrase pas toujours la cible
            ok = rename(CFG_TMP, CFG_PATH) == 0;
        }
    }
    free(txt);
    if (!ok) ESP_LOGE(TAG, "Echec d'enregistrement de %s", CFG_PATH);
    return ok;
}

static void load_config(void)
{
    s_count = 0;
    FILE *f = fopen(CFG_PATH, "r");
    if (!f) { ESP_LOGI(TAG, "Aucun thermostat configure"); return; }
    char *buf = (char *)heap_caps_malloc(CFG_MAX_SIZE + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (char *)malloc(CFG_MAX_SIZE + 1);
    if (!buf) { fclose(f); return; }
    size_t n = fread(buf, 1, CFG_MAX_SIZE, f);
    fclose(f);
    buf[n] = '\0';
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    const cJSON *arr = root ? cJSON_GetObjectItem(root, "thermostats") : NULL;
    const cJSON *o;
    cJSON_ArrayForEach(o, arr) {
        if (s_count >= THERMO_MAX_ZONES) break;
        zone_t *t = &s_z[s_count];
        zone_defaults(t);
        j_str(o, "name", t->name, sizeof(t->name));
        char v[40] = "";
        j_str(o, "sensorIEEE", v, sizeof(v));   copy_ieee(t->sensor, v);
        v[0] = '\0';
        j_str(o, "actuatorIEEE", v, sizeof(v)); copy_ieee(t->actuator, v);
        t->extra_count = j_ieee_list(o, "actuatorsExtra", t->extra, THERMO_MAX_EXTRA, NULL);
        j_str(o, "actionHeat", t->action_heat, sizeof(t->action_heat));
        if (!t->action_heat[0]) j_str(o, "actionOn", t->action_heat, sizeof(t->action_heat));   // ancien nom
        j_str(o, "actionCool", t->action_cool, sizeof(t->action_cool));
        j_str(o, "actionOff", t->action_off, sizeof(t->action_off));
        v[0] = '\0';
        j_str(o, "presenceIEEE", v, sizeof(v)); copy_ieee(t->presence, v);
        t->open_count = j_ieee_list(o, "openSensors", t->open, THERMO_MAX_OPEN, NULL);
        j_bool(o, "enabled", &t->enabled);
        j_bool(o, "heating", &t->heating);
        j_num(o, "setpoint", &t->setpoint);
        j_num(o, "frostTemp", &t->frost_temp);
        j_bool(o, "frostMode", &t->frost_mode);
        j_int(o, "forceMode", &t->force_mode);
        t->setpoint_saved = t->setpoint;
        j_num(o, "setpointSaved", &t->setpoint_saved);
        j_int(o, "tpiCycleSec", &t->tpi_cycle_s);
        j_num(o, "tpiKp", &t->kp);
        j_num(o, "tpiKi", &t->ki);
        j_int(o, "minOnSec", &t->min_on_s);
        j_int(o, "minOffSec", &t->min_off_s);
        j_int(o, "sensorTimeoutSec", &t->sensor_timeout_s);
        if (t->sensor_timeout_s == SENSOR_TIMEOUT_OLD_DEFAULT) t->sensor_timeout_s = SENSOR_TIMEOUT_DEFAULT_SEC;
        j_int(o, "operMode", &t->oper_mode);
        j_str(o, "schedule", t->schedule, sizeof(t->schedule));
        j_str(o, "tariffPeriods", t->tariff_periods, sizeof(t->tariff_periods));
        j_int(o, "priority", &t->priority);
        j_int(o, "nominalPowerW", &t->nominal_w);
        if (t->force_mode < 0 || t->force_mode > 2) t->force_mode = 0;
        int uid = 0;
        j_int(o, "uid", &uid);
        t->uid = uid > 0 ? (uint32_t)uid : 0;
        j_int(o, "matterEp", &t->matter_ep);
        s_count++;
    }
    cJSON_Delete(root);
    for (int i = 0; i < s_count; i++) if (!s_z[i].uid) s_z[i].uid = next_uid();   // anciens fichiers
    ESP_LOGI(TAG, "%d zone(s) chargee(s)", s_count);
}

// ----------------------------------------------------------------------------
// JSON des pages
// ----------------------------------------------------------------------------

static double round1(float v) { return round((double)v * 10.0) / 10.0; }

static char *json_out(cJSON *root)
{
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

char *thermostat_status_json(void)
{
    cJSON *arr = cJSON_CreateArray();
    lock();
    bool tariff = false;
    for (int i = 0; i < s_count; i++) tariff |= (s_z[i].oper_mode == 2);
    char tariff_now[32] = "";
    if (tariff) energy_tariff_label(energy_tariff_current_index(), tariff_now, sizeof(tariff_now));
    for (int i = 0; i < s_count; i++) {
        zone_t *t = &s_z[i];
        // Etat en direct, aussi pour une zone desactivee (lecture seule)
        refresh_sensors(t);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "id", i);
        cJSON_AddStringToObject(o, "name", t->name);
        cJSON_AddNumberToObject(o, "enabled", t->enabled ? 1 : 0);
        cJSON_AddNumberToObject(o, "heating", t->heating ? 1 : 0);
        cJSON_AddNumberToObject(o, "setpoint", round1(t->setpoint));
        cJSON_AddNumberToObject(o, "frostTemp", round1(t->frost_temp));
        if (t->sensor_valid) cJSON_AddNumberToObject(o, "temp", round1(t->current_temp));
        else cJSON_AddNullToObject(o, "temp");
        cJSON_AddNumberToObject(o, "sensorValid", t->sensor_valid ? 1 : 0);
        cJSON_AddNumberToObject(o, "sensorReason", t->sensor_reason);
        cJSON_AddNumberToObject(o, "sensorAge", t->sensor_age_s);
        cJSON_AddNumberToObject(o, "occupied", t->occupied ? 1 : 0);
        cJSON_AddNumberToObject(o, "windowOpen", t->window_open ? 1 : 0);
        cJSON_AddNumberToObject(o, "hasPresence", t->presence[0] ? 1 : 0);
        cJSON_AddNumberToObject(o, "openCount", t->open_count);
        cJSON_AddNumberToObject(o, "actuatorCount", (t->actuator[0] ? 1 : 0) + t->extra_count);
        cJSON_AddNumberToObject(o, "operMode", t->oper_mode);
        cJSON_AddNumberToObject(o, "schedActive", t->sched_active ? 1 : 0);
        cJSON_AddStringToObject(o, "tariffNow", t->oper_mode == 2 ? tariff_now : "");
        cJSON_AddNumberToObject(o, "output", t->output ? 1 : 0);
        cJSON_AddNumberToObject(o, "actualOn", t->actual_on ? 1 : 0);
        cJSON_AddNumberToObject(o, "forceMode", t->force_mode);
        cJSON_AddNumberToObject(o, "reversible", (t->action_heat[0] && t->action_cool[0]) ? 1 : 0);
        cJSON_AddNumberToObject(o, "frostMode", t->frost_mode ? 1 : 0);
        cJSON_AddNumberToObject(o, "duty", (int)(t->duty * 100.0f));
        cJSON_AddStringToObject(o, "sensorIEEE", t->sensor);
        cJSON_AddStringToObject(o, "actuatorIEEE", t->actuator);
        cJSON_AddItemToArray(arr, o);
    }
    unlock();
    return json_out(arr);
}

char *thermostat_config_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "max", THERMO_MAX_ZONES);
    cJSON *arr = cJSON_AddArrayToObject(root, "zones");
    lock();
    for (int i = 0; i < s_count; i++) {
        const zone_t *t = &s_z[i];
        cJSON *o = cJSON_CreateObject();
        char lbl[40];
        cJSON_AddNumberToObject(o, "id", i);
        cJSON_AddStringToObject(o, "name", t->name);
        cJSON_AddBoolToObject(o, "enabled", t->enabled);
        cJSON_AddBoolToObject(o, "heating", t->heating);
        cJSON_AddNumberToObject(o, "setpoint", round1(t->setpoint));
        cJSON_AddNumberToObject(o, "frostTemp", round1(t->frost_temp));
        cJSON_AddStringToObject(o, "sensorIEEE", t->sensor);
        device_label(t->sensor, lbl, sizeof(lbl));
        cJSON_AddStringToObject(o, "sensorLabel", lbl);
        cJSON_AddStringToObject(o, "actuatorIEEE", t->actuator);
        device_label(t->actuator, lbl, sizeof(lbl));
        cJSON_AddStringToObject(o, "actuatorLabel", lbl);
        cJSON *ex = cJSON_AddArrayToObject(o, "actuatorsExtra");
        for (int k = 0; k < t->extra_count; k++) cJSON_AddItemToArray(ex, cJSON_CreateString(t->extra[k]));
        cJSON_AddStringToObject(o, "actionHeat", t->action_heat);
        cJSON_AddStringToObject(o, "actionCool", t->action_cool);
        cJSON_AddStringToObject(o, "actionOff", t->action_off);
        cJSON_AddStringToObject(o, "presenceIEEE", t->presence);
        device_label(t->presence, lbl, sizeof(lbl));
        cJSON_AddStringToObject(o, "presenceLabel", lbl);
        cJSON *op = cJSON_AddArrayToObject(o, "openSensors");
        for (int k = 0; k < t->open_count; k++) cJSON_AddItemToArray(op, cJSON_CreateString(t->open[k]));
        cJSON_AddNumberToObject(o, "operMode", t->oper_mode);
        cJSON_AddStringToObject(o, "schedule", t->schedule);
        cJSON_AddStringToObject(o, "tariffPeriods", t->tariff_periods);
        cJSON_AddNumberToObject(o, "tpiCycleSec", t->tpi_cycle_s);
        cJSON_AddNumberToObject(o, "tpiKp", r3(t->kp));
        cJSON_AddNumberToObject(o, "tpiKi", r3(t->ki));
        cJSON_AddNumberToObject(o, "minOnSec", t->min_on_s);
        cJSON_AddNumberToObject(o, "minOffSec", t->min_off_s);
        cJSON_AddNumberToObject(o, "sensorTimeoutSec", t->sensor_timeout_s);
        cJSON_AddNumberToObject(o, "matterEndpoint", t->m_created ? t->matter_ep : -1);
        char uid[20];
        snprintf(uid, sizeof(uid), "%016llX", (unsigned long long)(MATTER_UID_BASE | t->uid));
        cJSON_AddStringToObject(o, "matterUid", uid);
        cJSON_AddItemToArray(arr, o);
    }
    unlock();
    // Valeurs par defaut d'une nouvelle zone (formulaire "Nouvelle zone")
    zone_t d;
    zone_defaults(&d);
    cJSON *def = cJSON_AddObjectToObject(root, "defaults");
    cJSON_AddBoolToObject(def, "enabled", d.enabled);
    cJSON_AddBoolToObject(def, "heating", d.heating);
    cJSON_AddNumberToObject(def, "setpoint", d.setpoint);
    cJSON_AddNumberToObject(def, "frostTemp", d.frost_temp);
    cJSON_AddNumberToObject(def, "operMode", 0);
    cJSON_AddNumberToObject(def, "tpiCycleSec", d.tpi_cycle_s);
    cJSON_AddNumberToObject(def, "tpiKp", r3(d.kp));
    cJSON_AddNumberToObject(def, "tpiKi", r3(d.ki));
    cJSON_AddNumberToObject(def, "minOnSec", d.min_on_s);
    cJSON_AddNumberToObject(def, "minOffSec", d.min_off_s);
    cJSON_AddNumberToObject(def, "sensorTimeoutSec", d.sensor_timeout_s);
    return json_out(root);
}

// Filtres des listes d'appareils (thermoDeviceMatch de la LiXee-Box)
enum { KIND_TEMP = 0, KIND_ACTUATOR, KIND_PRESENCE, KIND_CONTACT };

static bool has_cluster(const zigate_device_t *d, uint16_t c)
{
    for (int i = 0; i < d->in_cluster_count && i < 32; i++) if (d->in_clusters[i] == c) return true;
    return false;
}

static bool model_has(const zigate_device_t *d, const char *const *words)
{
    char m[sizeof(d->model)];
    size_t i = 0;
    for (; d->model[i] && i + 1 < sizeof(m); i++) m[i] = (char)tolower((unsigned char)d->model[i]);
    m[i] = '\0';
    for (; *words; words++) if (strstr(m, *words)) return true;
    return false;
}

static bool device_match(const zigate_device_t *d, int kind)
{
    char v[DEVDATA_VALUE_LEN];
    switch (kind) {
    case KIND_TEMP:
        return has_cluster(d, 0x0402) || get_hex(d->ieee_addr, 0x0402, 0, v, sizeof(v)) ||
               get_hex(d->ieee_addr, 0x0201, 0, v, sizeof(v));
    case KIND_ACTUATOR: {
        if (has_cluster(d, 0x0006) || has_cluster(d, 0x0201)) return true;
        const device_data_t *dd = device_data_find(d->ieee_addr);
        if (dd && !strcmp(dd->info.power_socket, "1")) return true;
        const devdata_template_t *tp = device_data_get_template(d->ieee_addr);
        return tp && tp->action_count > 0;
    }
    case KIND_PRESENCE: {
        static const char *const W[] = { "motion", "presence", "occupancy", "fp1", "sml", NULL };
        return has_cluster(d, 0x0406) || model_has(d, W);
    }
    default: {
        static const char *const W[] = { "contact", "door", "magnet", "ouverture", NULL };
        return has_cluster(d, 0x0500) || model_has(d, W);
    }
    }
}

char *thermostat_options_json(void)
{
    static const char *const KEYS[] = { "sensors", "actuators", "presence", "contacts" };
    cJSON *root = cJSON_CreateObject();
    cJSON *lists[4];
    for (int k = 0; k < 4; k++) lists[k] = cJSON_AddArrayToObject(root, KEYS[k]);
    int n = zigate_devices_count();
    for (int i = 0; i < n; i++) {
        const zigate_device_t *d = zigate_devices_get(i);
        if (!d || !d->ieee_addr) continue;
        char ieee[20], lbl[40];
        snprintf(ieee, sizeof(ieee), "%016llX", (unsigned long long)d->ieee_addr);
        device_label(ieee, lbl, sizeof(lbl));
        for (int k = 0; k < 4; k++) {
            if (!device_match(d, k)) continue;
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "ieee", ieee);
            cJSON_AddStringToObject(o, "label", lbl);
            cJSON_AddItemToArray(lists[k], o);
        }
    }
    bool zl = energy_tariff_available();
    cJSON_AddBoolToObject(root, "hasZlinky", zl);
    cJSON *tar = cJSON_AddArrayToObject(root, "tariffs");
    if (zl) {
        uint16_t ids[8];
        int np = energy_tariff_periods(ids, 8);
        for (int i = 0; i < np; i++) {
            char l[32];
            energy_tariff_label(ids[i], l, sizeof(l));
            cJSON *o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "id", ids[i]);
            cJSON_AddStringToObject(o, "label", l);
            cJSON_AddItemToArray(tar, o);
        }
    }
    return json_out(root);
}

char *thermostat_device_actions_json(uint64_t ieee)
{
    cJSON *arr = cJSON_CreateArray();
    const devdata_template_t *tp = ieee ? device_data_get_template(ieee) : NULL;
    for (int i = 0; tp && i < tp->action_count; i++) {
        if (tp->actions[i].name[0]) cJSON_AddItemToArray(arr, cJSON_CreateString(tp->actions[i].name));
    }
    return json_out(arr);
}

// ----------------------------------------------------------------------------
// Modifications
// ----------------------------------------------------------------------------

static void post_sync(void);

int thermostat_save_zone(int id, const cJSON *j)
{
    lock();
    zone_t *t;
    if (id < 0 || id >= s_count) {
        if (s_count >= THERMO_MAX_ZONES) { unlock(); return -1; }
        uint32_t uid = next_uid();
        id = s_count++;
        t = &s_z[id];
        zone_defaults(t);
        t->uid = uid;
    } else {
        t = &s_z[id];
    }
    char v[64];
    // Nom : espaces retires, "Zone" si vide
    v[0] = '\0';
    j_str(j, "name", v, sizeof(v));
    char *s = v;
    while (*s == ' ') s++;
    size_t n = strlen(s);
    while (n > 0 && s[n - 1] == ' ') s[--n] = '\0';
    strlcpy(t->name, s[0] ? s : "Zone", sizeof(t->name));

    v[0] = '\0'; j_str(j, "sensorIEEE", v, sizeof(v));   copy_ieee(t->sensor, v);
    v[0] = '\0'; j_str(j, "actuatorIEEE", v, sizeof(v)); copy_ieee(t->actuator, v);
    t->extra_count = j_ieee_list(j, "actuatorsExtra", t->extra, THERMO_MAX_EXTRA, t->actuator);
    t->action_heat[0] = t->action_cool[0] = t->action_off[0] = '\0';
    j_str(j, "actionHeat", t->action_heat, sizeof(t->action_heat));
    j_str(j, "actionCool", t->action_cool, sizeof(t->action_cool));
    j_str(j, "actionOff", t->action_off, sizeof(t->action_off));
    v[0] = '\0'; j_str(j, "presenceIEEE", v, sizeof(v)); copy_ieee(t->presence, v);
    t->open_count = j_ieee_list(j, "openSensors", t->open, THERMO_MAX_OPEN, NULL);

    j_bool(j, "enabled", &t->enabled);
    j_bool(j, "heating", &t->heating);
    j_num(j, "setpoint", &t->setpoint);
    j_num(j, "frostTemp", &t->frost_temp);
    int cycle = 0;
    j_int(j, "tpiCycleSec", &cycle);
    if (cycle >= 60) t->tpi_cycle_s = cycle;
    j_num(j, "tpiKp", &t->kp);
    j_num(j, "tpiKi", &t->ki);
    j_int(j, "minOnSec", &t->min_on_s);
    j_int(j, "minOffSec", &t->min_off_s);
    if (t->min_on_s < 0) t->min_on_s = 0;
    if (t->min_off_s < 0) t->min_off_s = 0;
    int timeout = 0;
    j_int(j, "sensorTimeoutSec", &timeout);
    if (timeout >= 60) t->sensor_timeout_s = timeout;
    j_int(j, "operMode", &t->oper_mode);
    if (t->oper_mode < 0 || t->oper_mode > 2) t->oper_mode = 0;
    t->schedule[0] = '\0';
    j_str(j, "schedule", t->schedule, sizeof(t->schedule));
    t->tariff_periods[0] = '\0';
    j_str(j, "tariffPeriods", t->tariff_periods, sizeof(t->tariff_periods));
    save_locked();
    unlock();
    post_sync();                                 // thermostat Matter cree / mis a jour
    return id;
}

static zone_t *zone_get(int id) { return (id >= 0 && id < s_count) ? &s_z[id] : NULL; }

// Les modifications *_locked s'appliquent sous le verrou et enregistrent la
// configuration ; elles servent a la page web comme aux commandes Matter.

static bool set_setpoint_locked(zone_t *t, float value)
{
    if (!isfinite(value)) value = t->setpoint;
    t->setpoint = value < 0.0f ? 0.0f : (value > 40.0f ? 40.0f : value);
    t->frost_mode = false;                       // reglage manuel : sortie du hors-gel
    t->integral = 0.0f;
    t->cycle_start_ms = 0;
    return save_locked();
}

static bool set_mode_locked(zone_t *t, bool heating)
{
    if (t->frost_mode) { t->setpoint = t->setpoint_saved; t->frost_mode = false; }
    t->heating = heating;
    t->integral = 0.0f;
    t->cycle_start_ms = 0;
    // En marche : action du nouveau mode appliquee tout de suite
    if (t->output) { send_actuator(t, true); t->last_switch_ms = now_ms(); }
    return save_locked();
}

static bool set_enabled_locked(zone_t *t, bool enabled)
{
    t->enabled = enabled;
    t->force_mode = 0;                           // toute bascule annule un forcage
    if (!enabled) {
        if (t->output) { send_actuator(t, false); t->output = false; t->last_switch_ms = now_ms(); }
        t->duty = 0.0f;
        t->integral = 0.0f;
    } else {
        t->integral = 0.0f;
        t->cycle_start_ms = 0;
    }
    return save_locked();
}

// ----------------------------------------------------------------------------
// Matter : chaque zone est un thermostat ponte (endpoint virtuel). Les
// ecritures d'un controleur arrivent dans la tache CHIP, qui ne prend jamais
// le verrou des zones (la tache des thermostats tient ce verrou en publiant
// vers Matter) : elles sont mises en file pour la tache des thermostats.
// ----------------------------------------------------------------------------

#define MSG_SYNC            1
#define MSG_WRITE           2

typedef struct {
    uint8_t  kind;
    int      ep;
    uint32_t attr;
    int32_t  value;
} thermo_msg_t;

static QueueHandle_t s_q;
static volatile bool s_matter_on;
static volatile int s_vep[THERMO_MAX_ZONES];         // endpoint Matter de chaque zone (0 : aucun)

static void post_sync(void)
{
    thermo_msg_t m = { MSG_SYNC, 0, 0, 0 };
    if (s_q) xQueueSend(s_q, &m, 0);
}

static esp_err_t matter_write_cb(int ep, uint32_t cluster, uint32_t attr, int32_t value, bool is_null)
{
    bool ours = false;
    for (int i = 0; i < THERMO_MAX_ZONES && !ours; i++) ours = (ep > 1 && s_vep[i] == ep);
    if (!ours) return ESP_ERR_NOT_FOUND;
    if (cluster == 0x0201 && !is_null && (attr == 0x0011 || attr == 0x0012 || attr == 0x001C)) {
        thermo_msg_t m = { MSG_WRITE, ep, attr, value };
        if (s_q) xQueueSend(s_q, &m, 0);
    }
    return ESP_OK;
}

/** Types d'appareil Matter de la zone : bit 0 chauffage, bit 1 froid. */
static uint8_t zone_features(const zone_t *t)
{
    bool rev = t->action_heat[0] && t->action_cool[0];
    return (uint8_t)((t->heating || rev ? 1 : 0) | (!t->heating || rev ? 2 : 0));
}

static void vep_rebuild(void)
{
    for (int i = 0; i < THERMO_MAX_ZONES; i++) s_vep[i] = (i < s_count && s_z[i].m_created) ? s_z[i].matter_ep : 0;
}

static int32_t clamp32(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

/** Cree l'endpoint de la zone si besoin, puis publie son etat (au changement). */
static void matter_sync_zone(zone_t *t, bool *dirty)
{
    // Zone retiree de Matter par l'utilisateur : pas d'endpoint (le numero
    // reste memorise pour la reprendre si elle est remise)
    if (matter_device_manager_is_excluded(MATTER_UID_BASE | t->uid)) {
        if (t->m_created) {
            matter_bridge_remove_endpoint(t->matter_ep);
            t->m_created = false;
        }
        return;
    }
    uint8_t feat = zone_features(t);
    if (t->m_created && t->m_feat != feat) {
        // Chauffage / froid changes : les fonctions du cluster Thermostat sont
        // fixees a la creation, l'endpoint est recree
        matter_bridge_remove_endpoint(t->matter_ep);
        t->m_created = false;
        t->matter_ep = 0;
        *dirty = true;
    }
    if (!t->m_created) {
        matter_profile_t *p = (matter_profile_t *)calloc(1, sizeof(matter_profile_t));
        if (!p) return;
        p->caps = MCAP_THERMOSTAT;
        p->heat = feat & 1;
        p->cool = (feat & 2) != 0;
        p->link_count = 1;
        p->links[0].target = ML_TH_RUNNING;          // cree ThermostatRunningState
        int ep = matter_bridge_add_virtual_endpoint(MATTER_UID_BASE | t->uid, p, t->name,
                                                    "Thermostat virtuel", t->matter_ep);
        free(p);
        if (ep <= 1) return;
        if (ep != t->matter_ep) { t->matter_ep = ep; *dirty = true; }
        t->m_created = true;
        t->m_feat = feat;
        t->m_pushed = false;
        ESP_LOGI(TAG, "zone \"%s\" : thermostat Matter sur l'endpoint %d", t->name, ep);
    }

    int ep = t->matter_ep;
    int32_t temp = t->sensor_valid ? (int32_t)lroundf(t->current_temp * 100.0f) : 0;
    int32_t sp = (int32_t)lroundf(t->setpoint * 100.0f);
    int32_t mode = !t->enabled ? 0 : (t->heating ? 4 : 3);
    int32_t run = (t->enabled && t->actual_on) ? (t->heating ? 0x1 : 0x2) : 0;
    bool force = !t->m_pushed;
    if (force || t->m_valid != t->sensor_valid || t->m_temp != temp) {
        matter_bridge_set_number(ep, 0x0201, 0x0000, temp, !t->sensor_valid);
    }
    if (force || t->m_sp != sp) {
        if (feat & 1) matter_bridge_set_number(ep, 0x0201, 0x0012, clamp32(sp, 700, 3000), false);
        if (feat & 2) matter_bridge_set_number(ep, 0x0201, 0x0011, clamp32(sp, 1600, 3200), false);
    }
    if (force || t->m_mode != mode) matter_bridge_set_number(ep, 0x0201, 0x001C, mode, false);
    if (force || t->m_run != run) matter_bridge_set_number(ep, 0x0201, 0x0029, run, false);
    t->m_valid = t->sensor_valid;
    t->m_temp = temp;
    t->m_sp = sp;
    t->m_mode = mode;
    t->m_run = run;
    t->m_pushed = true;
}

static void matter_sync_all(void)
{
    if (!s_matter_on || !matter_bridge_is_started()) return;
    bool dirty = false;
    for (int i = 0; i < s_count; i++) {
        zone_t *t = &s_z[i];
        if (!t->enabled) refresh_sensors(t);     // les zones actives le sont par la regulation
        matter_sync_zone(t, &dirty);
    }
    vep_rebuild();
    if (dirty) save_locked();                    // endpoints memorises
}

/** Ecriture d'un controleur Matter sur une zone. */
static void matter_apply_write(const thermo_msg_t *m)
{
    zone_t *t = NULL;
    for (int i = 0; i < s_count && !t; i++) if (s_z[i].m_created && s_z[i].matter_ep == m->ep) t = &s_z[i];
    if (!t) return;
    ESP_LOGI(TAG, "zone \"%s\" : Matter 0201/%04lX = %ld", t->name, (unsigned long)m->attr, (long)m->value);
    if (m->attr == 0x0012 || m->attr == 0x0011) {
        // Consigne du mode en cours (une zone n'a qu'une consigne)
        if ((m->attr == 0x0012) == t->heating) set_setpoint_locked(t, m->value / 100.0f);
    } else if (m->attr == 0x001C) {
        if (m->value == 0) {
            if (t->enabled) set_enabled_locked(t, false);
        } else if (m->value == 4 || m->value == 3) {
            bool heat = (m->value == 4);
            if (t->heating != heat) set_mode_locked(t, heat);
            if (!t->enabled) set_enabled_locked(t, true);
        }
    }
    refresh_sensors(t);
    t->m_pushed = false;                         // republier l'etat reel
}

void thermostat_matter_refresh(void) { post_sync(); }

void thermostat_matter_start(void)
{
    matter_bridge_set_virtual_write_cb(matter_write_cb);
    s_matter_on = true;
    post_sync();
}

static void regulation_task(void *arg)
{
    (void)arg;
    int64_t next_tick = now_ms() + TICK_MS;
    for (;;) {
        int64_t wait = next_tick - now_ms();
        thermo_msg_t m;
        if (wait > 0 && xQueueReceive(s_q, &m, pdMS_TO_TICKS(wait)) == pdTRUE) {
            lock();
            if (m.kind == MSG_WRITE) matter_apply_write(&m);
            matter_sync_all();
            unlock();
            continue;
        }
        lock();
        regulation_tick();
        matter_sync_all();
        unlock();
        presence_tick();                         // remise a zero horaire, enregistrement differe
        next_tick = now_ms() + TICK_MS;
    }
}

esp_err_t thermostat_init(void)
{
    if (s_z) return ESP_OK;
    s_z = (zone_t *)heap_caps_calloc(THERMO_MAX_ZONES, sizeof(zone_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_z) s_z = (zone_t *)calloc(THERMO_MAX_ZONES, sizeof(zone_t));
    s_lock = xSemaphoreCreateMutex();
    s_q = xQueueCreate(16, sizeof(thermo_msg_t));
    if (!s_z || !s_lock || !s_q) return ESP_ERR_NO_MEM;
    load_config();
    presence_init();
    // Pile en RAM interne : la tache envoie des trames ZiGate, publie vers
    // Matter et enregistre la configuration apres une commande Matter
    if (xTaskCreate(regulation_task, "thermo", 6144, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

// ----------------------------------------------------------------------------
// Modifications depuis la page web
// ----------------------------------------------------------------------------

bool thermostat_set_setpoint(int id, float value)
{
    lock();
    zone_t *t = zone_get(id);
    bool ok = t && set_setpoint_locked(t, value);
    unlock();
    post_sync();
    return ok;
}

bool thermostat_set_frost(int id, bool on)
{
    lock();
    zone_t *t = zone_get(id);
    bool ok = t != NULL;
    if (t) {
        if (on) {
            if (!t->frost_mode) { t->setpoint_saved = t->setpoint; t->frost_mode = true; }
            t->setpoint = t->frost_temp;
            t->force_mode = 0;
        } else if (t->frost_mode) {
            t->setpoint = t->setpoint_saved;
            t->frost_mode = false;
        }
        t->integral = 0.0f;
        t->cycle_start_ms = 0;
        ok = save_locked();
    }
    unlock();
    post_sync();
    return ok;
}

bool thermostat_set_mode(int id, bool heating)
{
    lock();
    zone_t *t = zone_get(id);
    bool ok = t && set_mode_locked(t, heating);
    unlock();
    post_sync();
    return ok;
}

bool thermostat_set_force(int id, int mode)
{
    lock();
    zone_t *t = zone_get(id);
    bool ok = t && t->enabled && mode >= 0 && mode <= 2;   // zone desactivee : refuse
    if (ok) {
        t->force_mode = mode;
        if (mode == 1) { send_actuator(t, true); t->output = true; t->last_switch_ms = now_ms(); }
        else if (mode == 2) { send_actuator(t, false); t->output = false; t->last_switch_ms = now_ms(); }
        ok = save_locked();
    }
    unlock();
    post_sync();
    return ok;
}

bool thermostat_toggle(int id)
{
    lock();
    zone_t *t = zone_get(id);
    bool ok = t && set_enabled_locked(t, !t->enabled);
    unlock();
    post_sync();
    return ok;
}

bool thermostat_delete(int id)
{
    lock();
    zone_t *t = zone_get(id);
    bool ok = t != NULL;
    if (t) {
        // L'appareil d'une zone supprimee en marche est coupe (la LiXee-Box le
        // laissait allume), et son thermostat Matter disparait
        if (t->output) send_actuator(t, false);
        if (t->m_created) matter_bridge_remove_endpoint(t->matter_ep);
        for (int k = id; k < s_count - 1; k++) s_z[k] = s_z[k + 1];
        s_count--;
        memset(&s_z[s_count], 0, sizeof(zone_t));
        vep_rebuild();
        ok = save_locked();
    }
    unlock();
    return ok;
}
