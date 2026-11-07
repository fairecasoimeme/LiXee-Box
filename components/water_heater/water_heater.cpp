// ============================================================================
// Chauffe-eau virtuels - voir water_heater.h. Portage simplifie du thermostat
// virtuel (components/thermostat/thermostat.cpp) : un seul actionneur on/off
// (contacteur/relais, cluster 0x0006, pas de pilotage par actions de
// template), 3 modes fixes Off/Manuel/Programme au lieu des reglages libres
// du thermostat, boost temporaire en plus (Water Heater Management).
// ============================================================================

#include "water_heater.h"
#include "zigate_devices.h"
#include "zigate_driver.h"
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

static const char *TAG = "WATERHEATER";

#define CFG_PATH        "/littlefs/config/waterheaters.json"
#define CFG_TMP         "/littlefs/config/tmpWaterHeaters.json"
#define CFG_MAX_SIZE    8000

#define SENSOR_TIMEOUT_DEFAULT_SEC  10800   // 3 h, voir thermostat.cpp

#define TICK_MS                30000
#define HYSTERESIS_C           0.3f
#define MATTER_UID_BASE        0x4C58574800000000ULL     // UniqueID Matter : "LXWH" + uid de la zone

typedef struct {
    // --- Configuration (persistee) ---
    char  name[32];
    char  sensor[20];                      // sonde temperature ballon (0402 ou 0201 attr 0), optionnelle
    char  actuator[20];                    // contacteur/relais (on/off 0006)
    char  extra[WATERHEATER_MAX_EXTRA][20];
    int   extra_count;
    int   mode;                            // WATERHEATER_MODE_* (persiste : etat normal hors boost)
    float setpoint;
    int   tpi_cycle_s;
    float kp, ki;
    int   min_on_s, min_off_s;
    int   sensor_timeout_s;
    char  schedule[192];                   // mode Programme : plages horaires (voir thermostat.cpp)
    char  tariff_periods[48];              // mode Programme : periodes tarifaires Linky
    uint32_t uid;
    int   matter_ep;

    // --- Etat (RAM) ---
    float   current_temp;
    bool    sensor_valid;
    uint8_t sensor_reason;                 // 0 OK, 1 introuvable, 2 sans temperature, 3 trop ancien
    long    sensor_age_s;
    bool    sched_active;                  // mode Programme : dans une plage active
    float   duty;
    bool    output;                        // etat commande
    bool    actual_on;                     // etat reel lu sur le contacteur
    float   integral;
    int64_t cycle_start_ms;
    int64_t last_switch_ms;

    // Boost (Water Heater Management)
    int64_t boost_until_ms;                // 0 : pas de boost en cours
    bool    boost_one_shot;
    bool    boost_reached;                 // consigne atteinte (oneShot) -> fin anticipee
    float   boost_setpoint;                // consigne effective pendant le boost

    // Matter : endpoint cree a ce demarrage, dernieres valeurs publiees
    bool    m_created;
    bool    m_pushed;
    int32_t m_temp, m_sp, m_mode_val, m_run;
    bool    m_heat_demand, m_boost_active;
} wh_t;

static wh_t *s_z;
static int s_count;
static SemaphoreHandle_t s_lock;

static void lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

// ----------------------------------------------------------------------------
// Outils (repris de thermostat.cpp)
// ----------------------------------------------------------------------------

static uint64_t parse_ieee(const char *s)
{
    return (s && s[0]) ? strtoull(s, NULL, 16) : 0;
}

static void copy_ieee(char *dst, const char *src)
{
    dst[0] = '\0';
    if (!src) return;
    size_t n = strlen(src);
    if (n == 0 || n > 16) return;
    for (size_t i = 0; i < n; i++) if (!isxdigit((unsigned char)src[i])) return;
    snprintf(dst, 20, "%016llX", (unsigned long long)strtoull(src, NULL, 16));
}

static bool get_hex(uint64_t ieee, uint16_t cl, uint16_t at, char *out, size_t len)
{
    out[0] = '\0';
    if (!ieee) return false;
    return device_data_get_value(ieee, cl, at, out, len) == ESP_OK && out[0];
}

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

static uint32_t next_uid(void)
{
    uint32_t m = 0;
    for (int i = 0; i < s_count; i++) if (s_z[i].uid > m) m = s_z[i].uid;
    return m + 1;
}

static void zone_defaults(wh_t *t)
{
    memset(t, 0, sizeof(*t));
    t->mode = WATERHEATER_MODE_MANUAL;
    t->setpoint = 55.0f;           // consigne ballon usuelle (securite anti-legionellose)
    t->tpi_cycle_s = 900;
    t->kp = 0.8f;
    t->ki = 0.1f;
    t->min_on_s = 180;
    t->min_off_s = 180;
    t->sensor_timeout_s = SENSOR_TIMEOUT_DEFAULT_SEC;
    t->sensor_reason = 1;
    t->sensor_age_s = -1;
    t->sched_active = true;
}

// ----------------------------------------------------------------------------
// Mode Programme : plages horaires / tarif Linky (copie de
// zone_schedule_active() de thermostat.cpp, voir ce fichier pour le detail du
// format "[jours/]HH:MM-HH:MM,...")
// ----------------------------------------------------------------------------

static bool zone_schedule_active(const wh_t *t)
{
    if (t->mode != WATERHEATER_MODE_TIMED) return true;    // Off/Manual : pas de restriction horaire

    if (t->tariff_periods[0]) {
        char cur[12], list[sizeof(t->tariff_periods) + 2];
        snprintf(cur, sizeof(cur), ",%u,", energy_tariff_current_index());
        snprintf(list, sizeof(list), ",%s,", t->tariff_periods);
        if (strstr(list, cur)) return true;
        if (!t->schedule[0]) return false;              // tarif configure et hors periode : pas de repli silencieux
    }

    if (!t->schedule[0]) return true;
    time_t tt = time(NULL);
    struct tm lt;
    localtime_r(&tt, &lt);
    int now = lt.tm_hour * 60 + lt.tm_min;
    int today = lt.tm_wday == 0 ? 7 : lt.tm_wday;
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
        if (!days) days = 0xFE;
        char *dash = strchr(seg, '-');
        if (!dash || dash == seg) continue;
        *dash = '\0';
        const char *a = seg, *b = dash + 1;
        while (*b == ' ') b++;
        int am = atoi(a) * 60 + (strlen(a) > 3 ? atoi(a + 3) : 0);
        int bm = atoi(b) * 60 + (strlen(b) > 3 ? atoi(b + 3) : 0);
        bool d_today = days & (1u << today), d_yest = days & (1u << yesterday);
        bool in;
        if (am == bm)     in = d_today;
        else if (am < bm) in = d_today && now >= am && now < bm;
        else              in = (d_today && now >= am) || (d_yest && now < bm);
        if (in) return true;
    }
    return false;
}

// ----------------------------------------------------------------------------
// Capteurs / actionneur
// ----------------------------------------------------------------------------

static int read_actuator_state(const wh_t *t)
{
    uint64_t ieee = parse_ieee(t->actuator);
    if (!ieee || !zigate_devices_find_by_ieee(ieee)) return -1;
    char v[DEVDATA_VALUE_LEN];
    if (!get_hex(ieee, 0x0006, 0x0000, v, sizeof(v))) return -1;
    return strtol(v, NULL, 16) != 0 ? 1 : 0;
}

static void refresh_sensors(wh_t *t)
{
    bool valid = false;
    float tc = t->current_temp;
    t->sensor_reason = 1;
    t->sensor_age_s = -1;
    uint64_t ieee = parse_ieee(t->sensor);
    const zigate_device_t *d = ieee ? zigate_devices_find_by_ieee(ieee) : NULL;
    if (d) {
        uint32_t last_seen = d->last_seen, last_rx = d->last_rx_s;
        t->sensor_reason = 2;
        uint16_t cl = 0x0402;
        char raw[DEVDATA_VALUE_LEN];
        bool ok = get_hex(ieee, 0x0402, 0x0000, raw, sizeof(raw));
        if (!ok && get_hex(ieee, 0x0201, 0x0000, raw, sizeof(raw))) { ok = true; cl = 0x0201; }
        if (ok) {
            tc = (int16_t)strtol(raw, NULL, 16) * temp_coef(ieee, cl);
            bool fresh = true;
            long timeout = (t->sensor_timeout_s > 0 ? t->sensor_timeout_s : SENSOR_TIMEOUT_DEFAULT_SEC) + 60;
            time_t tnow = time(NULL);
            long age = -1;
            if (ntp_sync_is_synced() && last_seen > 1600000000 && tnow >= (time_t)last_seen) age = (long)(tnow - last_seen);
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
    t->sched_active = zone_schedule_active(t);
    int act = read_actuator_state(t);
    t->actual_on = act >= 0 ? act != 0 : t->output;
}

static void fire_actuator_one(const char *ieee_s, bool on)
{
    uint64_t ieee = parse_ieee(ieee_s);
    if (!ieee) return;
    const zigate_device_t *d = zigate_devices_find_by_ieee(ieee);
    if (!d) return;
    zigate_send_onoff(d->short_addr, d->endpoint ? d->endpoint : 1, on ? 1 : 0);
}

static void send_actuator(wh_t *t, bool on)
{
    ESP_LOGI(TAG, "[Action] chauffe-eau \"%s\" : %s (%d appareil(s))", t->name,
             on ? "marche" : "arret", 1 + t->extra_count);
    fire_actuator_one(t->actuator, on);
    for (int k = 0; k < t->extra_count; k++) {
        const char *ieee = t->extra[k];
        bool dup = strcmp(ieee, t->actuator) == 0;
        for (int j = 0; j < k && !dup; j++) dup = strcmp(ieee, t->extra[j]) == 0;
        if (!dup) fire_actuator_one(ieee, on);
    }
}

static void apply_output(wh_t *t, bool desired, int64_t now)
{
    int actual = read_actuator_state(t);
    bool current = actual >= 0 ? actual != 0 : t->output;

    if (desired != current) {
        int64_t min_hold = (int64_t)(current ? t->min_on_s : t->min_off_s) * 1000;
        if (t->last_switch_ms != 0 && now - t->last_switch_ms < min_hold) return;
        send_actuator(t, desired);
        t->output = desired;
        t->last_switch_ms = now;
    } else {
        t->output = desired;
    }
}

// ----------------------------------------------------------------------------
// Regulation
// ----------------------------------------------------------------------------

static void end_boost_locked(wh_t *t)
{
    t->boost_until_ms = 0;
    t->boost_one_shot = false;
    t->boost_reached = false;
}

static void regulation_tick(void)
{
    int64_t now = now_ms();
    for (int i = 0; i < s_count; i++) {
        wh_t *t = &s_z[i];
        refresh_sensors(t);

        bool boosting = t->boost_until_ms != 0;
        if (boosting && now >= t->boost_until_ms) { end_boost_locked(t); boosting = false; }
        if (boosting && t->boost_one_shot && t->sensor_valid && t->boost_reached) { end_boost_locked(t); boosting = false; }

        float target = boosting ? t->boost_setpoint : t->setpoint;
        bool want_on;
        if (t->mode == WATERHEATER_MODE_OFF && !boosting) {
            want_on = false;
        } else if (!t->sched_active && !boosting) {
            want_on = false;                      // mode Programme, hors plage : arret (sauf boost)
        } else {
            want_on = true;                        // Manuel, ou Programme en plage, ou boost
        }

        bool desired;
        if (!want_on) {
            desired = false;
            t->duty = 0.0f;
            t->integral = 0.0f;
        } else if (!t->sensor_valid) {
            // Pas de sonde (ou HS) : pilotage ouvert, pas de regulation possible
            desired = true;
            t->duty = 1.0f;
        } else {
            float error = target - t->current_temp;
            if (boosting) {
                // Boost : marche continue jusqu'a la consigne (pas de TPI, on veut
                // chauffer vite), oneShot s'arrete des que la consigne est atteinte
                desired = error > 0.0f;
                if (error <= 0.0f) t->boost_reached = true;
                t->duty = desired ? 1.0f : 0.0f;
            } else if (t->tpi_cycle_s < 60) {
                // Hysteresis simple si cycle TPI desactive (configuration invalide)
                if (error > HYSTERESIS_C) desired = true;
                else if (error < -HYSTERESIS_C) desired = false;
                else desired = t->output;
                t->duty = desired ? 1.0f : 0.0f;
            } else {
                if (t->cycle_start_ms == 0 || now - t->cycle_start_ms >= (int64_t)t->tpi_cycle_s * 1000) {
                    t->cycle_start_ms = now;
                    t->integral += error;
                    if (t->ki > 0.0f) {
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
// Persistance (/littlefs/config/waterheaters.json)
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

static double r3(float v) { return round((double)v * 1000.0) / 1000.0; }

static bool save_locked(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "waterheaters");
    for (int i = 0; i < s_count; i++) {
        const wh_t *t = &s_z[i];
        if (!t->name[0] && !t->actuator[0]) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", t->name);
        cJSON_AddStringToObject(o, "sensorIEEE", t->sensor);
        cJSON_AddStringToObject(o, "actuatorIEEE", t->actuator);
        cJSON *ex = cJSON_AddArrayToObject(o, "actuatorsExtra");
        for (int k = 0; k < t->extra_count; k++) cJSON_AddItemToArray(ex, cJSON_CreateString(t->extra[k]));
        cJSON_AddNumberToObject(o, "mode", t->mode);
        cJSON_AddNumberToObject(o, "setpoint", r3(t->setpoint));
        cJSON_AddNumberToObject(o, "tpiCycleSec", t->tpi_cycle_s);
        cJSON_AddNumberToObject(o, "tpiKp", r3(t->kp));
        cJSON_AddNumberToObject(o, "tpiKi", r3(t->ki));
        cJSON_AddNumberToObject(o, "minOnSec", t->min_on_s);
        cJSON_AddNumberToObject(o, "minOffSec", t->min_off_s);
        cJSON_AddNumberToObject(o, "sensorTimeoutSec", t->sensor_timeout_s);
        cJSON_AddStringToObject(o, "schedule", t->schedule);
        cJSON_AddStringToObject(o, "tariffPeriods", t->tariff_periods);
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
            remove(CFG_PATH);
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
    if (!f) { ESP_LOGI(TAG, "Aucun chauffe-eau configure"); return; }
    char *buf = (char *)heap_caps_malloc(CFG_MAX_SIZE + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (char *)malloc(CFG_MAX_SIZE + 1);
    if (!buf) { fclose(f); return; }
    size_t n = fread(buf, 1, CFG_MAX_SIZE, f);
    fclose(f);
    buf[n] = '\0';
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    const cJSON *arr = root ? cJSON_GetObjectItem(root, "waterheaters") : NULL;
    const cJSON *o;
    cJSON_ArrayForEach(o, arr) {
        if (s_count >= WATERHEATER_MAX_ZONES) break;
        wh_t *t = &s_z[s_count];
        zone_defaults(t);
        j_str(o, "name", t->name, sizeof(t->name));
        char v[40] = "";
        j_str(o, "sensorIEEE", v, sizeof(v));   copy_ieee(t->sensor, v);
        v[0] = '\0';
        j_str(o, "actuatorIEEE", v, sizeof(v)); copy_ieee(t->actuator, v);
        t->extra_count = j_ieee_list(o, "actuatorsExtra", t->extra, WATERHEATER_MAX_EXTRA, NULL);
        j_int(o, "mode", &t->mode);
        if (t->mode < WATERHEATER_MODE_OFF || t->mode > WATERHEATER_MODE_TIMED) t->mode = WATERHEATER_MODE_MANUAL;
        j_num(o, "setpoint", &t->setpoint);
        j_int(o, "tpiCycleSec", &t->tpi_cycle_s);
        j_num(o, "tpiKp", &t->kp);
        j_num(o, "tpiKi", &t->ki);
        j_int(o, "minOnSec", &t->min_on_s);
        j_int(o, "minOffSec", &t->min_off_s);
        j_int(o, "sensorTimeoutSec", &t->sensor_timeout_s);
        j_str(o, "schedule", t->schedule, sizeof(t->schedule));
        j_str(o, "tariffPeriods", t->tariff_periods, sizeof(t->tariff_periods));
        int uid = 0;
        j_int(o, "uid", &uid);
        t->uid = uid > 0 ? (uint32_t)uid : 0;
        j_int(o, "matterEp", &t->matter_ep);
        s_count++;
    }
    cJSON_Delete(root);
    for (int i = 0; i < s_count; i++) if (!s_z[i].uid) s_z[i].uid = next_uid();
    ESP_LOGI(TAG, "%d chauffe-eau(x) charge(s)", s_count);
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

char *water_heater_status_json(void)
{
    cJSON *arr = cJSON_CreateArray();
    lock();
    for (int i = 0; i < s_count; i++) {
        wh_t *t = &s_z[i];
        refresh_sensors(t);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "id", i);
        cJSON_AddStringToObject(o, "name", t->name);
        cJSON_AddNumberToObject(o, "mode", t->mode);
        cJSON_AddNumberToObject(o, "setpoint", round1(t->setpoint));
        if (t->sensor_valid) cJSON_AddNumberToObject(o, "temp", round1(t->current_temp));
        else cJSON_AddNullToObject(o, "temp");
        cJSON_AddNumberToObject(o, "sensorValid", t->sensor_valid ? 1 : 0);
        cJSON_AddNumberToObject(o, "sensorReason", t->sensor_reason);
        cJSON_AddNumberToObject(o, "sensorAge", t->sensor_age_s);
        cJSON_AddNumberToObject(o, "hasSensor", t->sensor[0] ? 1 : 0);
        cJSON_AddNumberToObject(o, "schedActive", t->sched_active ? 1 : 0);
        cJSON_AddNumberToObject(o, "output", t->output ? 1 : 0);
        cJSON_AddNumberToObject(o, "actualOn", t->actual_on ? 1 : 0);
        cJSON_AddNumberToObject(o, "duty", (int)(t->duty * 100.0f));
        cJSON_AddNumberToObject(o, "boosting", t->boost_until_ms != 0 ? 1 : 0);
        cJSON_AddNumberToObject(o, "boostRemainingSec",
            t->boost_until_ms != 0 ? (int)((t->boost_until_ms - now_ms()) / 1000) : 0);
        cJSON_AddStringToObject(o, "sensorIEEE", t->sensor);
        cJSON_AddStringToObject(o, "actuatorIEEE", t->actuator);
        cJSON_AddItemToArray(arr, o);
    }
    unlock();
    return json_out(arr);
}

char *water_heater_config_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "max", WATERHEATER_MAX_ZONES);
    cJSON *arr = cJSON_AddArrayToObject(root, "zones");
    lock();
    for (int i = 0; i < s_count; i++) {
        const wh_t *t = &s_z[i];
        cJSON *o = cJSON_CreateObject();
        char lbl[40];
        cJSON_AddNumberToObject(o, "id", i);
        cJSON_AddStringToObject(o, "name", t->name);
        cJSON_AddNumberToObject(o, "mode", t->mode);
        cJSON_AddNumberToObject(o, "setpoint", round1(t->setpoint));
        cJSON_AddStringToObject(o, "sensorIEEE", t->sensor);
        device_label(t->sensor, lbl, sizeof(lbl));
        cJSON_AddStringToObject(o, "sensorLabel", lbl);
        cJSON_AddStringToObject(o, "actuatorIEEE", t->actuator);
        device_label(t->actuator, lbl, sizeof(lbl));
        cJSON_AddStringToObject(o, "actuatorLabel", lbl);
        cJSON *ex = cJSON_AddArrayToObject(o, "actuatorsExtra");
        for (int k = 0; k < t->extra_count; k++) cJSON_AddItemToArray(ex, cJSON_CreateString(t->extra[k]));
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
    wh_t d;
    zone_defaults(&d);
    cJSON *def = cJSON_AddObjectToObject(root, "defaults");
    cJSON_AddNumberToObject(def, "mode", d.mode);
    cJSON_AddNumberToObject(def, "setpoint", d.setpoint);
    cJSON_AddNumberToObject(def, "tpiCycleSec", d.tpi_cycle_s);
    cJSON_AddNumberToObject(def, "tpiKp", r3(d.kp));
    cJSON_AddNumberToObject(def, "tpiKi", r3(d.ki));
    cJSON_AddNumberToObject(def, "minOnSec", d.min_on_s);
    cJSON_AddNumberToObject(def, "minOffSec", d.min_off_s);
    cJSON_AddNumberToObject(def, "sensorTimeoutSec", d.sensor_timeout_s);
    return json_out(root);
}

enum { KIND_TEMP = 0, KIND_ACTUATOR };

static bool has_cluster(const zigate_device_t *d, uint16_t c)
{
    for (int i = 0; i < d->in_cluster_count && i < 32; i++) if (d->in_clusters[i] == c) return true;
    return false;
}

static bool device_match(const zigate_device_t *d, int kind)
{
    char v[DEVDATA_VALUE_LEN];
    if (kind == KIND_TEMP) {
        return has_cluster(d, 0x0402) || get_hex(d->ieee_addr, 0x0402, 0, v, sizeof(v)) ||
               get_hex(d->ieee_addr, 0x0201, 0, v, sizeof(v));
    }
    if (has_cluster(d, 0x0006)) return true;
    const device_data_t *dd = device_data_find(d->ieee_addr);
    return dd && !strcmp(dd->info.power_socket, "1");
}

char *water_heater_options_json(void)
{
    static const char *const KEYS[] = { "sensors", "actuators" };
    cJSON *root = cJSON_CreateObject();
    cJSON *lists[2];
    for (int k = 0; k < 2; k++) lists[k] = cJSON_AddArrayToObject(root, KEYS[k]);
    int n = zigate_devices_count();
    for (int i = 0; i < n; i++) {
        const zigate_device_t *d = zigate_devices_get(i);
        if (!d || !d->ieee_addr) continue;
        char ieee[20], lbl[40];
        snprintf(ieee, sizeof(ieee), "%016llX", (unsigned long long)d->ieee_addr);
        device_label(ieee, lbl, sizeof(lbl));
        for (int k = 0; k < 2; k++) {
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

// ----------------------------------------------------------------------------
// Modifications
// ----------------------------------------------------------------------------

static void post_sync(void);

int water_heater_save_zone(int id, const cJSON *j)
{
    lock();
    wh_t *t;
    if (id < 0 || id >= s_count) {
        if (s_count >= WATERHEATER_MAX_ZONES) { unlock(); return -1; }
        uint32_t uid = next_uid();
        id = s_count++;
        t = &s_z[id];
        zone_defaults(t);
        t->uid = uid;
    } else {
        t = &s_z[id];
    }
    char v[64];
    v[0] = '\0';
    j_str(j, "name", v, sizeof(v));
    char *s = v;
    while (*s == ' ') s++;
    size_t n = strlen(s);
    while (n > 0 && s[n - 1] == ' ') s[--n] = '\0';
    strlcpy(t->name, s[0] ? s : "Chauffe-eau", sizeof(t->name));

    v[0] = '\0'; j_str(j, "sensorIEEE", v, sizeof(v));   copy_ieee(t->sensor, v);
    v[0] = '\0'; j_str(j, "actuatorIEEE", v, sizeof(v)); copy_ieee(t->actuator, v);
    t->extra_count = j_ieee_list(j, "actuatorsExtra", t->extra, WATERHEATER_MAX_EXTRA, t->actuator);

    j_int(j, "mode", &t->mode);
    if (t->mode < WATERHEATER_MODE_OFF || t->mode > WATERHEATER_MODE_TIMED) t->mode = WATERHEATER_MODE_MANUAL;
    j_num(j, "setpoint", &t->setpoint);
    if (t->setpoint < 0.0f) t->setpoint = 0.0f;
    if (t->setpoint > 70.0f) t->setpoint = 70.0f;
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
    t->schedule[0] = '\0';
    j_str(j, "schedule", t->schedule, sizeof(t->schedule));
    t->tariff_periods[0] = '\0';
    j_str(j, "tariffPeriods", t->tariff_periods, sizeof(t->tariff_periods));
    save_locked();
    unlock();
    post_sync();
    return id;
}

static wh_t *zone_get(int id) { return (id >= 0 && id < s_count) ? &s_z[id] : NULL; }

static bool set_setpoint_locked(wh_t *t, float value)
{
    if (!isfinite(value)) value = t->setpoint;
    t->setpoint = value < 0.0f ? 0.0f : (value > 70.0f ? 70.0f : value);
    t->integral = 0.0f;
    t->cycle_start_ms = 0;
    return save_locked();
}

static bool set_mode_locked(wh_t *t, int mode)
{
    if (mode < WATERHEATER_MODE_OFF || mode > WATERHEATER_MODE_TIMED) return false;
    t->mode = mode;
    t->integral = 0.0f;
    t->cycle_start_ms = 0;
    if (mode == WATERHEATER_MODE_OFF && t->output) {
        send_actuator(t, false);
        t->output = false;
        t->last_switch_ms = now_ms();
    }
    return save_locked();
}

// ----------------------------------------------------------------------------
// Matter : chaque zone est un endpoint Water Heater (0x050F) ponte. Ecritures
// et commandes d'un controleur arrivent dans la tache CHIP, mise en file pour
// la tache water_heater (meme regle que thermostat.cpp : la tache CHIP ne
// prend jamais le verrou des zones).
// ----------------------------------------------------------------------------

#define MSG_SYNC        1
#define MSG_WRITE       2
#define MSG_CMD         3

typedef struct {
    uint8_t  kind;
    int      ep;
    uint32_t attr;           // MSG_WRITE
    int32_t  value;          // MSG_WRITE : valeur attribut : MSG_CMD : mode ou 0
    uint8_t  cmd;            // MSG_CMD : matter_virtual_cmd_t
    matter_wh_boost_t boost; // MSG_CMD (MATTER_VCMD_WH_BOOST)
} wh_msg_t;

static QueueHandle_t s_q;
static volatile bool s_matter_on;
static volatile int s_vep[WATERHEATER_MAX_ZONES];

static void post_sync(void)
{
    wh_msg_t m = {}; m.kind = MSG_SYNC;
    if (s_q) xQueueSend(s_q, &m, 0);
}

static esp_err_t matter_write_cb(int ep, uint32_t cluster, uint32_t attr, int32_t value, bool is_null)
{
    bool ours = false;
    for (int i = 0; i < WATERHEATER_MAX_ZONES && !ours; i++) ours = (ep > 1 && s_vep[i] == ep);
    if (!ours) return ESP_ERR_NOT_FOUND;
    if (cluster == 0x0201 && !is_null && (attr == 0x0012 || attr == 0x001C)) {
        wh_msg_t m = {}; m.kind = MSG_WRITE; m.ep = ep; m.attr = attr; m.value = value;
        if (s_q) xQueueSend(s_q, &m, 0);
    }
    return ESP_OK;
}

static esp_err_t matter_command_cb(int ep, matter_virtual_cmd_t cmd, int32_t value, const matter_wh_boost_t *boost)
{
    bool ours = false;
    for (int i = 0; i < WATERHEATER_MAX_ZONES && !ours; i++) ours = (ep > 1 && s_vep[i] == ep);
    if (!ours) return ESP_ERR_NOT_FOUND;
    wh_msg_t m = {}; m.kind = MSG_CMD; m.ep = ep; m.cmd = (uint8_t)cmd; m.value = value;
    if (boost) m.boost = *boost;
    if (s_q) xQueueSend(s_q, &m, 0);
    return ESP_OK;
}

static void vep_rebuild(void)
{
    for (int i = 0; i < WATERHEATER_MAX_ZONES; i++) s_vep[i] = (i < s_count && s_z[i].m_created) ? s_z[i].matter_ep : 0;
}

static int32_t clamp32(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void matter_sync_zone(wh_t *t, bool *dirty)
{
    if (matter_device_manager_is_excluded(MATTER_UID_BASE | t->uid)) {
        if (t->m_created) {
            matter_bridge_remove_endpoint(t->matter_ep);
            t->m_created = false;
        }
        return;
    }
    if (!t->m_created) {
        matter_profile_t *p = (matter_profile_t *)calloc(1, sizeof(matter_profile_t));
        if (!p) return;
        p->caps = MCAP_WATER_HEATER;
        int ep = matter_bridge_add_virtual_endpoint(MATTER_UID_BASE | t->uid, p, t->name,
                                                    "Chauffe-eau virtuel", t->matter_ep);
        free(p);
        if (ep <= 1) return;
        if (ep != t->matter_ep) { t->matter_ep = ep; *dirty = true; }
        t->m_created = true;
        t->m_pushed = false;
        ESP_LOGI(TAG, "zone \"%s\" : Water Heater Matter sur l'endpoint %d", t->name, ep);
    }

    int ep = t->matter_ep;
    bool boosting = t->boost_until_ms != 0;
    int32_t temp = t->sensor_valid ? (int32_t)lroundf(t->current_temp * 100.0f) : 0;
    int32_t sp = (int32_t)lroundf((boosting ? t->boost_setpoint : t->setpoint) * 100.0f);
    int32_t mode_val = (int32_t)t->mode;
    int32_t run = (t->output && t->actual_on) ? 0x1 : 0;
    bool heat_demand = t->output && t->actual_on;
    bool force = !t->m_pushed;

    if (force || t->m_temp != temp) matter_bridge_set_number(ep, 0x0201, 0x0000, temp, !t->sensor_valid);
    // 0-7000 (0-70 degC) : plage d'un ballon d'eau chaude, pas celle d'une
    // piece (700-3000 chez le thermostat virtuel, voir thermostat.cpp)
    if (force || t->m_sp != sp) matter_bridge_set_number(ep, 0x0201, 0x0012, clamp32(sp, 0, 7000), false);
    if (force || t->m_run != run) matter_bridge_set_number(ep, 0x0201, 0x0029, run, false);
    if (force || t->m_mode_val != mode_val || t->m_heat_demand != heat_demand || t->m_boost_active != boosting) {
        matter_bridge_wh_push_state(ep, heat_demand, boosting, (uint8_t)t->mode);
    }
    t->m_temp = temp;
    t->m_sp = sp;
    t->m_run = run;
    t->m_mode_val = mode_val;
    t->m_heat_demand = heat_demand;
    t->m_boost_active = boosting;
    t->m_pushed = true;
}

static void matter_sync_all(void)
{
    if (!s_matter_on || !matter_bridge_is_started()) return;
    bool dirty = false;
    for (int i = 0; i < s_count; i++) {
        matter_sync_zone(&s_z[i], &dirty);
    }
    vep_rebuild();
    if (dirty) save_locked();
}

static void matter_apply_write(const wh_msg_t *m)
{
    wh_t *t = NULL;
    for (int i = 0; i < s_count && !t; i++) if (s_z[i].m_created && s_z[i].matter_ep == m->ep) t = &s_z[i];
    if (!t) return;
    ESP_LOGI(TAG, "zone \"%s\" : Matter 0201/%04lX = %ld", t->name, (unsigned long)m->attr, (long)m->value);
    if (m->attr == 0x0012) {
        set_setpoint_locked(t, m->value / 100.0f);
    } else if (m->attr == 0x001C) {
        // SystemMode ecrit par un controleur generique (Apple Home...) : Off (0)
        // ou Heat (4) -> bascule Off/Manuel, le mode Programme reste accessible
        // uniquement via ChangeToMode (Water Heater Mode)
        if (m->value == 0 && t->mode != WATERHEATER_MODE_OFF) set_mode_locked(t, WATERHEATER_MODE_OFF);
        else if (m->value == 4 && t->mode == WATERHEATER_MODE_OFF) set_mode_locked(t, WATERHEATER_MODE_MANUAL);
    }
    refresh_sensors(t);
    t->m_pushed = false;
}

static void matter_apply_command(const wh_msg_t *m)
{
    wh_t *t = NULL;
    for (int i = 0; i < s_count && !t; i++) if (s_z[i].m_created && s_z[i].matter_ep == m->ep) t = &s_z[i];
    if (!t) return;
    switch ((matter_virtual_cmd_t)m->cmd) {
    case MATTER_VCMD_WH_CHANGE_MODE:
        ESP_LOGI(TAG, "zone \"%s\" : ChangeToMode -> %ld", t->name, (long)m->value);
        set_mode_locked(t, (int)m->value);
        break;
    case MATTER_VCMD_WH_BOOST:
        ESP_LOGI(TAG, "zone \"%s\" : Boost %lu s%s%s", t->name, (unsigned long)m->boost.duration_s,
                 m->boost.one_shot ? " oneShot" : "", m->boost.emergency ? " emergency" : "");
        t->boost_until_ms = now_ms() + (int64_t)m->boost.duration_s * 1000;
        t->boost_one_shot = m->boost.one_shot;
        t->boost_reached = false;
        t->boost_setpoint = m->boost.has_temp ? m->boost.temp_centidegrees / 100.0f : t->setpoint;
        break;
    case MATTER_VCMD_WH_CANCEL_BOOST:
        ESP_LOGI(TAG, "zone \"%s\" : CancelBoost", t->name);
        end_boost_locked(t);
        break;
    }
    refresh_sensors(t);
    t->m_pushed = false;
}

void water_heater_matter_refresh(void) { post_sync(); }

void water_heater_matter_start(void)
{
    matter_bridge_set_virtual_write_cb(matter_write_cb);
    matter_bridge_set_virtual_command_cb(matter_command_cb);
    s_matter_on = true;
    post_sync();
}

static void regulation_task(void *arg)
{
    (void)arg;
    int64_t next_tick = now_ms() + TICK_MS;
    for (;;) {
        int64_t wait = next_tick - now_ms();
        wh_msg_t m;
        if (wait > 0 && xQueueReceive(s_q, &m, pdMS_TO_TICKS(wait)) == pdTRUE) {
            lock();
            if (m.kind == MSG_WRITE) matter_apply_write(&m);
            else if (m.kind == MSG_CMD) matter_apply_command(&m);
            matter_sync_all();
            unlock();
            continue;
        }
        lock();
        regulation_tick();
        matter_sync_all();
        unlock();
        next_tick = now_ms() + TICK_MS;
    }
}

esp_err_t water_heater_init(void)
{
    if (s_z) return ESP_OK;
    s_z = (wh_t *)heap_caps_calloc(WATERHEATER_MAX_ZONES, sizeof(wh_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_z) s_z = (wh_t *)calloc(WATERHEATER_MAX_ZONES, sizeof(wh_t));
    s_lock = xSemaphoreCreateMutex();
    s_q = xQueueCreate(16, sizeof(wh_msg_t));
    if (!s_z || !s_lock || !s_q) return ESP_ERR_NO_MEM;
    load_config();
    if (xTaskCreate(regulation_task, "waterheater", 6144, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

// ----------------------------------------------------------------------------
// Modifications depuis la page web
// ----------------------------------------------------------------------------

bool water_heater_set_setpoint(int id, float value)
{
    lock();
    wh_t *t = zone_get(id);
    bool ok = t && set_setpoint_locked(t, value);
    unlock();
    post_sync();
    return ok;
}

bool water_heater_set_mode(int id, int mode)
{
    lock();
    wh_t *t = zone_get(id);
    bool ok = t && set_mode_locked(t, mode);
    unlock();
    post_sync();
    return ok;
}

bool water_heater_toggle(int id)
{
    lock();
    wh_t *t = zone_get(id);
    bool ok = t && set_mode_locked(t, t->mode == WATERHEATER_MODE_OFF ? WATERHEATER_MODE_MANUAL : WATERHEATER_MODE_OFF);
    unlock();
    post_sync();
    return ok;
}

bool water_heater_boost(int id, uint32_t duration_s, bool one_shot, bool emergency, bool has_temp, float temp_c)
{
    lock();
    wh_t *t = zone_get(id);
    bool ok = t != NULL;
    if (t) {
        (void)emergency;   // pas de distinction materielle possible (un seul relais)
        t->boost_until_ms = now_ms() + (int64_t)duration_s * 1000;
        t->boost_one_shot = one_shot;
        t->boost_reached = false;
        t->boost_setpoint = has_temp ? temp_c : t->setpoint;
    }
    unlock();
    post_sync();
    return ok;
}

bool water_heater_cancel_boost(int id)
{
    lock();
    wh_t *t = zone_get(id);
    bool ok = t != NULL;
    if (t) end_boost_locked(t);
    unlock();
    post_sync();
    return ok;
}

bool water_heater_delete(int id)
{
    lock();
    wh_t *t = zone_get(id);
    bool ok = t != NULL;
    if (t) {
        if (t->output) send_actuator(t, false);
        if (t->m_created) matter_bridge_remove_endpoint(t->matter_ep);
        for (int k = id; k < s_count - 1; k++) s_z[k] = s_z[k + 1];
        s_count--;
        memset(&s_z[s_count], 0, sizeof(wh_t));
        vep_rebuild();
        ok = save_locked();
    }
    unlock();
    return ok;
}
