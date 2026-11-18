// ============================================================================
// Historique de temperature - voir temp_history.h
// ============================================================================

#include "temp_history.h"
#include "zigate_devices.h"
#include "zigate_driver.h"
#include "device_data.h"
#include "config_manager.h"
#include "ntp_sync.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "TEMP_HST";

#define TH_PATH         "/littlefs/hst/temperature.json"
#define TH_TMP          "/littlefs/hst/tmpTemperature.json"
#define TH_MAX_DEV      8                      // sondes gardees dans l'historique
#define TH_MAX_CFG      4                      // sondes configurables (Config Energie)
#define TH_FILE_MAX     65536                  // 8 sondes x 31 jours : ~35 Ko
#define TH_NONE         INT16_MIN              // heure sans mesure
#define TH_STALE_S      10800                  // sonde muette depuis 3 h : ignoree
#define SAVE_PERIOD_US  (15LL * 60 * 1000000)  // enregistrement au plus tous les 1/4 h

typedef struct {
    uint64_t ieee;
    uint32_t date[31];                         // jour de la ligne (aaaammjj, 0 = vide)
    int16_t  v[31][24];                        // moyenne horaire en 0,1 °C
    uint32_t cur_key;                          // heure en cours (aaaammjjhh)
    int32_t  cur_sum;                          // somme des releves de l'heure (0,1 °C)
    uint16_t cur_cnt;
} th_dev_t;

static th_dev_t *s_dev;                        // PSRAM
static int s_count;
static SemaphoreHandle_t s_lock;
static bool s_dirty;
static int64_t s_last_save;
static int s_last_min = -1;                    // minute deja relevee
static uint32_t s_last_hour;                   // heure du dernier releve (aaaammjjhh)

static uint32_t ymd(const struct tm *t) { return (uint32_t)((t->tm_year + 1900) * 10000 + (t->tm_mon + 1) * 100 + t->tm_mday); }

static bool local_time(struct tm *t)
{
    if (!ntp_sync_is_synced()) return false;
    time_t now = time(NULL);
    localtime_r(&now, t);
    return true;
}

/** Jour decale de delta jours (calendrier, juste aux changements d'heure). */
static void shift_day(const struct tm *t, int delta, struct tm *out)
{
    *out = *t;
    out->tm_mday += delta;
    out->tm_hour = 12;
    out->tm_min = out->tm_sec = 0;
    out->tm_isdst = -1;
    mktime(out);
}

static void clear_dev(th_dev_t *d, uint64_t ieee)
{
    memset(d, 0, sizeof(*d));
    d->ieee = ieee;
    for (int i = 0; i < 31; i++) for (int h = 0; h < 24; h++) d->v[i][h] = TH_NONE;
}

static th_dev_t *find_dev(uint64_t ieee)
{
    for (int i = 0; i < s_count; i++) if (s_dev[i].ieee == ieee) return &s_dev[i];
    return NULL;
}

/** Sondes configurees (IEEE separes par des virgules), sans doublon. */
static int configured(uint64_t out[TH_MAX_CFG])
{
    const char *s = cfg_get_general()->temperature;
    int n = 0;
    while (*s && n < TH_MAX_CFG) {
        while (*s == ',' || *s == ' ') s++;
        const char *b = s;
        while (*s && *s != ',') s++;
        size_t len = (size_t)(s - b);
        while (len && b[len - 1] == ' ') len--;
        if (len != 16) continue;
        bool hex = true;
        for (size_t i = 0; i < len; i++) if (!isxdigit((unsigned char)b[i])) hex = false;
        if (!hex) continue;
        char tmp[17];
        memcpy(tmp, b, 16);
        tmp[16] = '\0';
        uint64_t ieee = strtoull(tmp, NULL, 16);
        bool dup = !ieee;
        for (int i = 0; i < n; i++) if (out[i] == ieee) dup = true;
        if (!dup) out[n++] = ieee;
    }
    return n;
}

/** Emplacement d'une sonde configuree : existant, libre, ou celui d'une sonde retiree. */
static th_dev_t *slot_for(uint64_t ieee, const uint64_t *cfg, int ncfg)
{
    th_dev_t *d = find_dev(ieee);
    if (d) return d;
    if (s_count < TH_MAX_DEV) {
        d = &s_dev[s_count++];
        clear_dev(d, ieee);
        return d;
    }
    for (int i = 0; i < s_count; i++) {
        bool used = false;
        for (int k = 0; k < ncfg; k++) if (cfg[k] == s_dev[i].ieee) used = true;
        if (!used) { clear_dev(&s_dev[i], ieee); return &s_dev[i]; }
    }
    return NULL;
}

/** Temperature actuelle d'une sonde en 0,1 °C ; false si inconnue ou perimee. */
static bool read_temp(uint64_t ieee, int *out)
{
    const zigate_device_t *z = zigate_devices_find_by_ieee(ieee);
    if (!z) return false;
    // Fraicheur : derniere trame quelle qu'elle soit (comme les sondes des zones)
    long age = -1;
    time_t now = time(NULL);
    if (z->last_seen > 1600000000 && now >= (time_t)z->last_seen) age = (long)(now - z->last_seen);
    else if (z->last_rx_s) age = (long)(zigate_uptime_s() - z->last_rx_s);
    if (age < 0 || age > TH_STALE_S) return false;
    char raw[DEVDATA_VALUE_LEN];
    if (device_data_get_value(ieee, 0x0402, 0x0000, raw, sizeof(raw)) != ESP_OK || !raw[0]) return false;
    int16_t r = (int16_t)strtol(raw, NULL, 16);
    if (r == (int16_t)0x8000) return false;                 // MeasuredValue invalide (ZCL)
    // Coefficient du template (0x0402, 0), 0,01 a defaut
    float coef = 0.01f;
    const devdata_template_t *tp = device_data_get_template(ieee);
    for (int i = 0; tp && i < tp->state_count; i++) {
        if (tp->states[i].cluster == 0x0402 && tp->states[i].attribute == 0 && tp->states[i].coefficient != 0.0f) {
            coef = tp->states[i].coefficient;
            break;
        }
    }
    float c = r * coef;
    if (c < -60.0f || c > 100.0f) return false;
    *out = (int)(c * 10.0f + (c >= 0 ? 0.5f : -0.5f));
    return true;
}

/** Ecrit la moyenne en cours dans la ligne de son jour (remise a vide si la ligne est d'un autre jour). */
static void store_current(th_dev_t *d)
{
    if (!d->cur_cnt) return;
    uint32_t day = d->cur_key / 100;
    int h = (int)(d->cur_key % 100), idx = (int)(day % 100) - 1;
    if (idx < 0 || idx > 30 || h > 23) return;
    if (d->date[idx] != day) {
        for (int k = 0; k < 24; k++) d->v[idx][k] = TH_NONE;
        d->date[idx] = day;
    }
    int32_t s = d->cur_sum, n = d->cur_cnt;
    d->v[idx][h] = (int16_t)(s >= 0 ? (s + n / 2) / n : (s - n / 2) / n);
}

void temp_history_init(void)
{
    if (s_dev) return;
    s_lock = xSemaphoreCreateMutex();
    s_dev = (th_dev_t *)heap_caps_calloc(TH_MAX_DEV, sizeof(th_dev_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_dev) s_dev = (th_dev_t *)calloc(TH_MAX_DEV, sizeof(th_dev_t));
    if (!s_dev || !s_lock) return;
    FILE *f = fopen(TH_PATH, "r");
    if (!f) return;
    char *buf = (char *)heap_caps_malloc(TH_FILE_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { fclose(f); return; }
    size_t n = fread(buf, 1, TH_FILE_MAX, f);
    fclose(f);
    buf[n] = '\0';
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    const cJSON *dev;
    cJSON_ArrayForEach(dev, cJSON_GetObjectItem(root, "devices")) {
        if (!dev->string || s_count >= TH_MAX_DEV) continue;
        uint64_t ieee = strtoull(dev->string, NULL, 16);
        if (!ieee || find_dev(ieee)) continue;
        th_dev_t *d = &s_dev[s_count++];
        clear_dev(d, ieee);
        const cJSON *day;
        cJSON_ArrayForEach(day, cJSON_GetObjectItem(dev, "days")) {
            int dn = day->string ? atoi(day->string) : 0;
            const cJSON *dt = cJSON_GetObjectItem(day, "d");
            if (dn < 1 || dn > 31 || !cJSON_IsNumber(dt)) continue;
            d->date[dn - 1] = (uint32_t)dt->valuedouble;
            int h = 0;
            const cJSON *v;
            cJSON_ArrayForEach(v, cJSON_GetObjectItem(day, "v")) {
                if (h < 24 && cJSON_IsNumber(v)) d->v[dn - 1][h] = (int16_t)v->valueint;
                h++;
            }
        }
        // Heure en cours au moment de l'enregistrement : reprise si c'est encore elle
        const cJSON *cur = cJSON_GetObjectItem(dev, "cur");
        const cJSON *k = cJSON_GetObjectItem(cur, "k"), *s = cJSON_GetObjectItem(cur, "s"), *c = cJSON_GetObjectItem(cur, "n");
        if (cJSON_IsNumber(k) && cJSON_IsNumber(s) && cJSON_IsNumber(c) && c->valueint > 0) {
            d->cur_key = (uint32_t)k->valuedouble;
            d->cur_sum = s->valueint;
            d->cur_cnt = (uint16_t)c->valueint;
        }
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "%d sonde(s) dans l'historique de temperature", s_count);
}

static void save(void)
{
    // Copie sous verrou, ecriture hors verrou
    th_dev_t *copy = (th_dev_t *)heap_caps_malloc(sizeof(th_dev_t) * TH_MAX_DEV, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_count;
    memcpy(copy, s_dev, sizeof(th_dev_t) * n);
    s_dirty = false;
    xSemaphoreGive(s_lock);

    cJSON *root = cJSON_CreateObject();
    cJSON *devs = cJSON_AddObjectToObject(root, "devices");
    for (int i = 0; i < n; i++) {
        char id[20];
        snprintf(id, sizeof(id), "%016llX", (unsigned long long)copy[i].ieee);
        cJSON *o = cJSON_AddObjectToObject(devs, id);
        cJSON *days = cJSON_AddObjectToObject(o, "days");
        for (int dn = 1; dn <= 31; dn++) {
            if (!copy[i].date[dn - 1]) continue;
            bool any = false;
            for (int h = 0; h < 24; h++) if (copy[i].v[dn - 1][h] != TH_NONE) any = true;
            if (!any) continue;                          // jour sans mesure : omis
            char k[4];
            snprintf(k, sizeof(k), "%d", dn);
            cJSON *dobj = cJSON_AddObjectToObject(days, k);
            cJSON_AddNumberToObject(dobj, "d", copy[i].date[dn - 1]);
            cJSON *arr = cJSON_AddArrayToObject(dobj, "v");
            for (int h = 0; h < 24; h++) {
                int16_t v = copy[i].v[dn - 1][h];
                cJSON_AddItemToArray(arr, v == TH_NONE ? cJSON_CreateNull() : cJSON_CreateNumber(v));
            }
        }
        if (copy[i].cur_cnt) {
            cJSON *cur = cJSON_AddObjectToObject(o, "cur");
            cJSON_AddNumberToObject(cur, "k", copy[i].cur_key);
            cJSON_AddNumberToObject(cur, "s", copy[i].cur_sum);
            cJSON_AddNumberToObject(cur, "n", copy[i].cur_cnt);
        }
    }
    free(copy);
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) return;
    FILE *f = fopen(TH_TMP, "w");
    bool ok = false;
    if (f) {
        size_t len = strlen(txt);
        ok = fwrite(txt, 1, len, f) == len;
        ok = (fclose(f) == 0) && ok;
        if (ok) {
            remove(TH_PATH);
            ok = rename(TH_TMP, TH_PATH) == 0;
        }
    }
    free(txt);
    if (!ok) ESP_LOGW(TAG, "Echec d'enregistrement de %s", TH_PATH);
}

void temp_history_tick(void)
{
    if (!s_dev || !s_lock) return;
    struct tm t;
    if (!local_time(&t)) return;                         // heure inconnue : creneau inconnu
    int minute = t.tm_hour * 60 + t.tm_min;
    if (minute == s_last_min) return;                    // un releve par minute
    s_last_min = minute;
    uint32_t key = ymd(&t) * 100 + (uint32_t)t.tm_hour;
    bool new_hour = s_last_hour && key != s_last_hour;
    s_last_hour = key;

    uint64_t cfg[TH_MAX_CFG];
    int ncfg = configured(cfg);
    for (int i = 0; i < ncfg; i++) {
        int v;
        bool ok = read_temp(cfg[i], &v);                 // hors verrou (base des appareils)
        xSemaphoreTake(s_lock, portMAX_DELAY);
        th_dev_t *d = slot_for(cfg[i], cfg, ncfg);
        if (d) {
            if (d->cur_key != key) { d->cur_key = key; d->cur_sum = 0; d->cur_cnt = 0; }
            if (ok && d->cur_cnt < 0xFFFF) {
                d->cur_sum += v;
                d->cur_cnt++;
                store_current(d);
                s_dirty = true;
            }
        }
        xSemaphoreGive(s_lock);
    }

    int64_t now = esp_timer_get_time();
    if (!s_last_save) s_last_save = now;
    if (s_dirty && (new_hour || now - s_last_save >= SAVE_PERIOD_US)) {
        s_last_save = now;
        save();
    }
}

static void add_temp(cJSON *arr, int16_t v)
{
    cJSON_AddItemToArray(arr, v == TH_NONE ? cJSON_CreateNull() : cJSON_CreateNumber(v / 10.0));
}

/** Valeur horaire d'un jour donne, TH_NONE si la ligne n'est pas de ce jour. */
static int16_t value_at(const th_dev_t *d, const struct tm *day, int h)
{
    int idx = day->tm_mday - 1;
    return d->date[idx] == ymd(day) ? d->v[idx][h] : TH_NONE;
}

char *temp_history_chart_json(const char *time_s)
{
    const cfg_general_t *g = cfg_get_general();
    uint64_t cfg[TH_MAX_CFG];
    int ncfg = configured(cfg);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", ncfg > 0 && g->enable_temperature_graph);
    cJSON *sensors = cJSON_AddArrayToObject(root, "sensors");
    struct tm t;
    memset(&t, 0, sizeof(t));
    // Copie d'une sonde sur le tas : 1,6 Ko, trop pour la pile du serveur HTTP
    th_dev_t *d = (th_dev_t *)heap_caps_malloc(sizeof(th_dev_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool ok = local_time(&t) && s_dev && s_lock && d;
    bool by_day = time_s && !strcmp(time_s, "day");
    for (int i = 0; i < ncfg; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddItemToArray(sensors, o);
        char id[20];
        snprintf(id, sizeof(id), "%016llX", (unsigned long long)cfg[i]);
        cJSON_AddStringToObject(o, "ieee", id);
        const zigate_device_t *z = zigate_devices_find_by_ieee(cfg[i]);
        const char *name = z && z->alias[0] ? z->alias : (z && z->model[0] ? z->model : id);
        cJSON_AddStringToObject(o, "name", name);

        bool have = false;
        if (ok) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            const th_dev_t *src = find_dev(cfg[i]);
            if (src) { *d = *src; have = true; }
            xSemaphoreGive(s_lock);
        }
        if (by_day) {
            // 30 derniers jours, cles "jj/mm" comme les libelles du graphe
            cJSON *day = cJSON_AddObjectToObject(o, "day");
            for (int k = 29; have && k >= 0; k--) {
                struct tm dt;
                shift_day(&t, -k, &dt);
                int32_t sum = 0;
                int n = 0, mn = INT16_MAX, mx = INT16_MIN;
                for (int h = 0; h < 24; h++) {
                    int16_t v = value_at(d, &dt, h);
                    if (v == TH_NONE) continue;
                    sum += v;
                    n++;
                    if (v < mn) mn = v;
                    if (v > mx) mx = v;
                }
                if (!n) continue;
                char key[24];
                snprintf(key, sizeof(key), "%02d/%02d", dt.tm_mday, dt.tm_mon + 1);
                cJSON *e = cJSON_AddObjectToObject(day, key);
                int avg = sum >= 0 ? (sum + n / 2) / n : (sum - n / 2) / n;
                cJSON_AddNumberToObject(e, "avg", avg / 10.0);
                cJSON_AddNumberToObject(e, "min", mn / 10.0);
                cJSON_AddNumberToObject(e, "max", mx / 10.0);
            }
        } else {
            // 24 h glissantes alignees sur le graphe : index 0 = heure courante + 1
            // de la veille, index 23 = heure courante (moyenne partielle)
            cJSON *arr = cJSON_AddArrayToObject(o, "hour");
            struct tm ty = t;
            if (ok) shift_day(&t, -1, &ty);
            int h0 = ok ? t.tm_hour : 23;
            for (int h = h0 + 1; h <= 23; h++) add_temp(arr, have ? value_at(d, &ty, h) : TH_NONE);
            for (int h = 0; h <= h0; h++) add_temp(arr, have ? value_at(d, &t, h) : TH_NONE);
        }
    }
    free(d);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}
