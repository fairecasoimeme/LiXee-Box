// ============================================================================
// Historique de presence - voir presence.h
// ============================================================================

#include "presence.h"
#include "zigate_devices.h"
#include "config_manager.h"
#include "ntp_sync.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static const char *TAG = "PRESENCE";

#define PRES_PATH       "/littlefs/hst/presence.json"
#define PRES_TMP        "/littlefs/hst/tmpPresence.json"
#define PRES_MAX_DEV    8
#define PRES_FILE_MAX   16384
#define SAVE_DELAY_US   (60LL * 1000000)       // ecritures groupees (une par minute au plus)

typedef struct {
    uint64_t ieee;
    uint32_t hours[31];                        // bit h du jour d (index d - 1)
    char     last[20];                         // derniere detection "JJ/MM/AAAA HH:MM"
} pres_dev_t;

static pres_dev_t *s_dev;                      // PSRAM
static int s_count;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_dirty;
static int64_t s_dirty_since;
static int s_last_key = -1;                    // jour x 24 + heure deja remis a zero

// Modeles de capteurs de presence (isPresenceSensor de la LiXee-Box)
static bool is_presence_model(const char *m)
{
    if (!m || !m[0]) return false;
    return !strcmp(m, "lumi.motion.ac01") || !strcmp(m, "SNZB-06P") || strstr(m, "motion") ||
           strstr(m, "presence") || strstr(m, "occupancy") || !strncmp(m, "lumi.sensor_motion", 18) ||
           strstr(m, "SML") || strstr(m, "TRADFRI motion");
}

static bool local_time(struct tm *t)
{
    if (!ntp_sync_is_synced()) return false;
    time_t now = time(NULL);
    localtime_r(&now, t);
    return true;
}

static pres_dev_t *find_dev(uint64_t ieee, bool create)
{
    for (int i = 0; i < s_count; i++) if (s_dev[i].ieee == ieee) return &s_dev[i];
    if (!create || s_count >= PRES_MAX_DEV) return NULL;
    pres_dev_t *d = &s_dev[s_count++];
    memset(d, 0, sizeof(*d));
    d->ieee = ieee;
    return d;
}

void presence_init(void)
{
    if (s_dev) return;
    s_dev = (pres_dev_t *)heap_caps_calloc(PRES_MAX_DEV, sizeof(pres_dev_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_dev) s_dev = (pres_dev_t *)calloc(PRES_MAX_DEV, sizeof(pres_dev_t));
    if (!s_dev) return;
    FILE *f = fopen(PRES_PATH, "r");
    if (!f) return;
    char *buf = (char *)heap_caps_malloc(PRES_FILE_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { fclose(f); return; }
    size_t n = fread(buf, 1, PRES_FILE_MAX, f);
    fclose(f);
    buf[n] = '\0';
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    const cJSON *dev;
    cJSON_ArrayForEach(dev, cJSON_GetObjectItem(root, "devices")) {
        if (!dev->string) continue;
        pres_dev_t *d = find_dev(strtoull(dev->string, NULL, 16), true);
        if (!d) break;
        const cJSON *day;
        cJSON_ArrayForEach(day, cJSON_GetObjectItem(dev, "days")) {
            int dn = day->string ? atoi(day->string) : 0;
            if (dn < 1 || dn > 31) continue;
            uint32_t m = 0;
            int h = 0;
            const cJSON *v;
            cJSON_ArrayForEach(v, day) {
                if (h < 24 && cJSON_IsNumber(v) && v->valueint) m |= 1u << h;
                h++;
            }
            d->hours[dn - 1] = m;
        }
        const cJSON *lu = cJSON_GetObjectItem(dev, "lastUpdate");
        if (cJSON_IsString(lu)) strlcpy(d->last, lu->valuestring, sizeof(d->last));
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "%d capteur(s) dans l'historique de presence", s_count);
}

void presence_note(uint64_t ieee, bool occupied)
{
    if (!occupied || !s_dev) return;                 // seuls les evenements positifs comptent
    struct tm t;
    if (!local_time(&t)) return;                     // heure inconnue : creneau inconnu
    const cfg_general_t *g = cfg_get_general();
    bool configured = g->presence[0] && strtoull(g->presence, NULL, 16) == ieee;
    if (!configured) {
        const zigate_device_t *z = zigate_devices_find_by_ieee(ieee);
        if (!z || !is_presence_model(z->model)) return;
    }
    char stamp[48];
    snprintf(stamp, sizeof(stamp), "%02d/%02d/%04d %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900,
             t.tm_hour, t.tm_min);
    portENTER_CRITICAL(&s_mux);
    pres_dev_t *d = find_dev(ieee, true);
    if (d) {
        d->hours[t.tm_mday - 1] |= 1u << t.tm_hour;
        strlcpy(d->last, stamp, sizeof(d->last));
        if (!s_dirty) s_dirty_since = esp_timer_get_time();
        s_dirty = true;
    }
    portEXIT_CRITICAL(&s_mux);
}

static void save(void)
{
    // Copie sous verrou, ecriture hors verrou
    pres_dev_t *copy = (pres_dev_t *)heap_caps_malloc(sizeof(pres_dev_t) * PRES_MAX_DEV, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) return;
    portENTER_CRITICAL(&s_mux);
    int n = s_count;
    memcpy(copy, s_dev, sizeof(pres_dev_t) * n);
    s_dirty = false;
    portEXIT_CRITICAL(&s_mux);

    cJSON *root = cJSON_CreateObject();
    cJSON *devs = cJSON_AddObjectToObject(root, "devices");
    for (int i = 0; i < n; i++) {
        char id[20];
        snprintf(id, sizeof(id), "%016llX", (unsigned long long)copy[i].ieee);
        cJSON *o = cJSON_AddObjectToObject(devs, id);
        cJSON *days = cJSON_AddObjectToObject(o, "days");
        for (int dn = 1; dn <= 31; dn++) {
            uint32_t m = copy[i].hours[dn - 1];
            if (!m) continue;                            // jour sans presence : omis
            char k[4];
            snprintf(k, sizeof(k), "%d", dn);
            cJSON *arr = cJSON_AddArrayToObject(days, k);
            for (int h = 0; h < 24; h++) cJSON_AddItemToArray(arr, cJSON_CreateNumber((m >> h) & 1));
        }
        cJSON_AddNumberToObject(o, "lastState", 1);
        cJSON_AddStringToObject(o, "lastUpdate", copy[i].last);
    }
    free(copy);
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) return;
    FILE *f = fopen(PRES_TMP, "w");
    bool ok = false;
    if (f) {
        size_t len = strlen(txt);
        ok = fwrite(txt, 1, len, f) == len;
        ok = (fclose(f) == 0) && ok;
        if (ok) {
            remove(PRES_PATH);
            ok = rename(PRES_TMP, PRES_PATH) == 0;
        }
    }
    free(txt);
    if (!ok) ESP_LOGW(TAG, "Echec d'enregistrement de %s", PRES_PATH);
}

void presence_tick(void)
{
    if (!s_dev) return;
    struct tm t;
    if (local_time(&t)) {
        int key = t.tm_mday * 24 + t.tm_hour;
        if (s_last_key < 0) {
            s_last_key = key;                            // demarrage : l'heure en cours est gardee
        } else if (key != s_last_key) {
            // Nouvelle heure : son creneau du mois precedent est efface
            s_last_key = key;
            portENTER_CRITICAL(&s_mux);
            bool changed = false;
            for (int i = 0; i < s_count; i++) {
                uint32_t *m = &s_dev[i].hours[t.tm_mday - 1];
                if (*m & (1u << t.tm_hour)) { *m &= ~(1u << t.tm_hour); changed = true; }
            }
            if (changed && !s_dirty) { s_dirty = true; s_dirty_since = esp_timer_get_time(); }
            portEXIT_CRITICAL(&s_mux);
        }
    }
    bool due = false;
    portENTER_CRITICAL(&s_mux);
    due = s_dirty && esp_timer_get_time() - s_dirty_since >= SAVE_DELAY_US;
    portEXIT_CRITICAL(&s_mux);
    if (due) save();
}

char *presence_chart_json(const char *time_s)
{
    const cfg_general_t *g = cfg_get_general();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", g->presence[0] && g->enable_presence_graph);
    struct tm t;
    bool ok = local_time(&t) && s_dev;
    if (time_s && !strcmp(time_s, "day")) {
        // Jours du mois avec au moins une heure de presence (tous capteurs)
        cJSON *day = cJSON_AddObjectToObject(root, "day");
        for (int dn = 1; dn <= 31; dn++) {
            uint32_t any = 0;
            portENTER_CRITICAL(&s_mux);
            for (int i = 0; ok && i < s_count; i++) any |= s_dev[i].hours[dn - 1];
            portEXIT_CRITICAL(&s_mux);
            char k[4];
            snprintf(k, sizeof(k), "%d", dn);
            cJSON_AddNumberToObject(day, k, any ? 1 : 0);
        }
    } else {
        // 24 h glissantes, alignees sur le graphe : index 0 = heure courante + 1
        // de la veille, index 23 = heure courante (tous capteurs)
        int v[24] = {};
        if (ok) {
            time_t y = time(NULL) - 86400;
            struct tm ty;
            localtime_r(&y, &ty);
            int h0 = t.tm_hour;
            portENTER_CRITICAL(&s_mux);
            for (int i = 0; i < s_count; i++) {
                for (int h = h0 + 1; h <= 23; h++) if (s_dev[i].hours[ty.tm_mday - 1] & (1u << h)) v[h - h0 - 1] = 1;
                for (int h = 0; h <= h0; h++) if (s_dev[i].hours[t.tm_mday - 1] & (1u << h)) v[23 - h0 + h] = 1;
            }
            portEXIT_CRITICAL(&s_mux);
        }
        cJSON *arr = cJSON_AddArrayToObject(root, "hour");
        for (int i = 0; i < 24; i++) cJSON_AddItemToArray(arr, cJSON_CreateNumber(v[i]));
    }
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}
