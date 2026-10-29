// ============================================================================
// Delestage - voir delestage.h
// ============================================================================

#include "delestage.h"
#include "zigate_devices.h"
#include "zigate_driver.h"
#include "device_data.h"
#include "energy_service.h"
#include "config_manager.h"
#include "notifications.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static const char *TAG = "DELESTAGE";

#define DL_FILE         "/littlefs/config/delestage.json"
#define DL_TMP          "/littlefs/config/tmpDelestage.json"
#define DL_MAX          16
#define DL_RECUT_US     (60LL * 1000000)    // re-coupure d'une prise rallumee pendant le delestage
#define ADPS_FRESH_S    120                 // ADPS (historique) : n'est envoye que pendant le depassement
#define AVG_SLOTS       60                  // moyenne sur 1 minute, un echantillon par seconde
#define AVG_MIN_SAMPLES 20

typedef struct {
    char ieee[20];
    char prev[8];                           // etat 0006/0000 avant la coupure, tel que stocke
} dl_dev_t;

static dl_dev_t s_dev[DL_MAX];
static int      s_count;
static bool     s_active;
static char     s_since[20], s_last_end[20], s_reason[16];
static int64_t  s_below_since, s_last_cut;
static volatile bool s_restore_req;
static SemaphoreHandle_t s_mx;

// Echantillons de puissance apparente (VA) : total et 3 phases, -1 = inconnu
static int32_t  s_samp[AVG_SLOTS][4];
static int      s_samp_pos;

static void lock(void)   { xSemaphoreTake(s_mx, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mx); }

static void now_str(char *out, size_t len)
{
    time_t t = time(NULL);
    struct tm lt;
    localtime_r(&t, &lt);
    snprintf(out, len, "%02d/%02d/%04d %02d:%02d", lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900,
             lt.tm_hour, lt.tm_min);
}

static uint64_t ieee_of(const char *s) { return (s && s[0]) ? strtoull(s, NULL, 16) : 0; }

/** Etat 0006/0000 stocke ("01", "00"...), "" si inconnu. */
static void onoff_value(uint64_t ieee, char *out, size_t len)
{
    out[0] = '\0';
    if (ieee) device_data_get_value(ieee, 0x0006, 0x0000, out, len);
}

static bool is_on(const char *v) { return v && v[0] && strtol(v, NULL, 16) != 0; }

static void send_onoff(uint64_t ieee, bool on)
{
    const zigate_device_t *d = zigate_devices_find_by_ieee(ieee);
    if (!d) return;
    zigate_send_onoff(d->short_addr, d->endpoint ? d->endpoint : 1, on ? 1 : 0);
}

/** Prises de la configuration ("ieee1,ieee2") : fn(ieee) pour chaque entree. */
template <typename F>
static void for_each_configured(F fn)
{
    char list[sizeof(((cfg_general_t *)0)->delestage)];
    strlcpy(list, cfg_get_general()->delestage, sizeof(list));
    char *save = NULL;
    for (char *id = strtok_r(list, ",", &save); id; id = strtok_r(NULL, ",", &save)) {
        while (*id == ' ') id++;
        size_t n = strlen(id);
        while (n > 0 && id[n - 1] == ' ') id[--n] = '\0';
        if (n && strcmp(id, "null") != 0) fn(id);
    }
}

// ----------------------------------------------------------------------------
// Persistance (format de /config/delestage.json de la LiXee-Box)
// ----------------------------------------------------------------------------

static void save_file(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", s_active ? "1" : "0");
    cJSON_AddStringToObject(root, "dateOn", s_since);
    cJSON_AddStringToObject(root, "dateOff", s_last_end);
    cJSON_AddStringToObject(root, "reason", s_reason);
    for (int i = 0; i < s_count; i++) cJSON_AddStringToObject(root, s_dev[i].ieee, s_dev[i].prev);
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) return;
    bool ok = false;
    FILE *f = fopen(DL_TMP, "w");
    if (f) {
        size_t n = strlen(txt);
        ok = fwrite(txt, 1, n, f) == n;
        ok = (fclose(f) == 0) && ok;
        if (ok) {
            remove(DL_FILE);
            ok = rename(DL_TMP, DL_FILE) == 0;
        }
    }
    free(txt);
    if (!ok) ESP_LOGE(TAG, "Ecriture de %s impossible", DL_FILE);
}

static void load_file(void)
{
    FILE *f = fopen(DL_FILE, "r");
    if (!f) return;
    char buf[2048];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    cJSON *root = cJSON_Parse(buf);
    if (!root) return;
    const cJSON *it;
    s_count = 0;
    cJSON_ArrayForEach(it, root) {
        if (!it->string || !cJSON_IsString(it)) continue;
        const char *k = it->string, *v = it->valuestring;
        if (!strcmp(k, "state"))        s_active = !strcmp(v, "1");
        else if (!strcmp(k, "dateOn"))  strlcpy(s_since, v, sizeof(s_since));
        else if (!strcmp(k, "dateOff")) strlcpy(s_last_end, v, sizeof(s_last_end));
        else if (!strcmp(k, "reason"))  strlcpy(s_reason, v, sizeof(s_reason));
        else if (s_count < DL_MAX) {
            strlcpy(s_dev[s_count].ieee, k, sizeof(s_dev[0].ieee));
            strlcpy(s_dev[s_count].prev, v, sizeof(s_dev[0].prev));
            s_count++;
        }
    }
    cJSON_Delete(root);
    if (s_active) ESP_LOGW(TAG, "Delestage en cours depuis %s (repris au demarrage)", s_since);
}

// ----------------------------------------------------------------------------
// Coupure / rallumage (sous le verrou)
// ----------------------------------------------------------------------------

static void trigger_locked(const char *reason)
{
    if (s_active) {
        // Deja deleste : le compteur signale le depassement en continu. On
        // recoupe seulement, au plus une fois par minute, une prise rallumee
        // entre-temps (l'etat memorise avant la coupure n'est pas touche)
        s_below_since = 0;
        if (esp_timer_get_time() - s_last_cut < DL_RECUT_US) return;
        s_last_cut = esp_timer_get_time();
        for (int i = 0; i < s_count; i++) {
            char v[8];
            uint64_t ieee = ieee_of(s_dev[i].ieee);
            onoff_value(ieee, v, sizeof(v));
            if (is_on(v)) send_onoff(ieee, false);
        }
        return;
    }
    s_count = 0;
    for_each_configured([](const char *id) {
        if (s_count >= DL_MAX) return;
        uint64_t ieee = ieee_of(id);
        if (!zigate_devices_find_by_ieee(ieee)) return;
        strlcpy(s_dev[s_count].ieee, id, sizeof(s_dev[0].ieee));
        onoff_value(ieee, s_dev[s_count].prev, sizeof(s_dev[0].prev));
        s_count++;
        send_onoff(ieee, false);
    });
    s_active = true;
    now_str(s_since, sizeof(s_since));
    strlcpy(s_reason, reason, sizeof(s_reason));
    s_below_since = 0;
    s_last_cut = esp_timer_get_time();
    save_file();
    ESP_LOGW(TAG, "%s : %d prise(s) coupee(s)", reason, s_count);
    char msg[96];
    snprintf(msg, sizeof(msg), "%d prise(s) coupée(s) (%s)", s_count,
             strcmp(reason, "anticipation") == 0 ? "délestage anticipé" : "signal du compteur");
    notifications_add_alert("⚡Délestage", msg, NOTIF_WARNING, "delestage", (float)s_count, 0);
}

static bool restore_locked(bool manual)
{
    if (!s_active) return false;
    int n = 0;
    for (int i = 0; i < s_count; i++) {
        if (!is_on(s_dev[i].prev)) continue;         // eteinte avant la coupure : laissee eteinte
        uint64_t ieee = ieee_of(s_dev[i].ieee);
        if (!zigate_devices_find_by_ieee(ieee)) continue;
        send_onoff(ieee, true);
        n++;
    }
    s_active = false;
    now_str(s_last_end, sizeof(s_last_end));
    s_below_since = 0;
    save_file();
    ESP_LOGW(TAG, "Fin du delestage (%s) : %d prise(s) rallumee(s)", manual ? "manuelle" : "automatique", n);
    char msg[96];
    snprintf(msg, sizeof(msg), "%d prise(s) rallumée(s) (%s)", n, manual ? "manuel" : "automatique");
    notifications_add_alert("⚡Fin du délestage", msg, NOTIF_INFO, "delestage", (float)n, 0);
    return true;
}

// ----------------------------------------------------------------------------
// Charge sur 1 minute et signal du compteur
// ----------------------------------------------------------------------------

/** Moyenne sur 1 minute d'une voie (0 total, 1-3 phases), -1 si trop peu d'echantillons. */
static int32_t minute_avg(int ch)
{
    int64_t sum = 0;
    int n = 0;
    for (int i = 0; i < AVG_SLOTS; i++) {
        if (s_samp[i][ch] >= 0) { sum += s_samp[i][ch]; n++; }
    }
    return n >= AVG_MIN_SAMPLES ? (int32_t)(sum / n) : -1;
}

/** Charge de la derniere minute en % de la puissance souscrite (triphase : phase la plus chargee). */
static int minute_load_pct(const energy_live_t *L)
{
    if (L->goal_va <= 0) return -1;
    if (!L->tri) {
        int32_t a = minute_avg(0);
        return a < 0 ? -1 : (int)((int64_t)a * 100 / L->goal_va);
    }
    // Limite par phase : Standard, PREF total / 3 ; Historique triphase, ISOUSC
    // est deja l'intensite d'UNE phase (goal_va = ISOUSC x 200)
    int32_t per = (L->mode == 2) ? L->goal_va : L->goal_va / 3;
    int best = -1;
    for (int p = 1; p <= 3 && per > 0; p++) {
        int32_t a = minute_avg(p);
        if (a < 0) continue;
        int pct = (int)((int64_t)a * 100 / per);
        if (pct > best) best = pct;
    }
    return best;
}

/** Depassement signale par le compteur : ADPS recent (historique) ou STGE bit 7 (standard). */
static bool meter_overload(const energy_live_t *L)
{
    if (L->mode == 0 || L->mode == 2) {
        uint64_t zl = ieee_of(cfg_get_general()->zlinky);
        const devdata_attr_t *a = zl ? device_data_get_attr(zl, 0xFF66, 0x0005) : NULL;
        // last_update = 0 : valeur relue du fichier au demarrage, pas un depassement
        // en cours (un ADPS memorise coupait les prises a chaque redemarrage)
        if (!a || !a->value[0] || a->last_update == 0) return false;
        uint32_t now = esp_log_timestamp() / 1000;
        return strtol(a->value, NULL, 16) > 0 && now - a->last_update <= ADPS_FRESH_S;
    }
    return L->overload;
}

static void tick(void)
{
    energy_live_t L;
    bool live = energy_service_get_live(&L) && L.valid;

    // Echantillon de la seconde (inconnu si le ZLinky ne remonte plus)
    int32_t *s = s_samp[s_samp_pos];
    s_samp_pos = (s_samp_pos + 1) % AVG_SLOTS;
    bool ok = live && L.connected;
    s[0] = ok ? L.total_va : -1;
    for (int p = 0; p < 3; p++) s[p + 1] = (ok && L.has_phase[p]) ? L.phase_va[p] : -1;

    lock();
    if (s_restore_req) {                                     // demande de la page web
        s_restore_req = false;
        restore_locked(true);
        unlock();
        return;
    }
    const cfg_general_t *g = cfg_get_general();
    bool configured = g->delestage[0] != '\0';
    if (!live || (!configured && !s_active)) { unlock(); return; }

    if (configured && meter_overload(&L)) {
        trigger_locked("meter");
        unlock();
        return;
    }

    // Charge de la derniere minute ; a defaut, derniere mesure du total
    int load = minute_load_pct(&L);
    if (load < 0 && L.goal_va > 0 && ok) load = (int)((int64_t)(L.total_va < 0 ? 0 : L.total_va) * 100 / L.goal_va);
    if (load < 0) { unlock(); return; }                      // puissance souscrite inconnue

    if (!s_active) {
        // Delestage anticipe (option) : couper avant que le compteur ne le fasse
        if (configured && g->delestage_anticip_pct > 0 && load >= g->delestage_anticip_pct) trigger_locked("anticipation");
        unlock();
        return;
    }
    if (g->delestage_restore_min > 0) {
        int64_t now = esp_timer_get_time();
        if (load >= g->delestage_restore_pct) {
            s_below_since = 0;
        } else if (!s_below_since) {
            s_below_since = now;
        } else if (now - s_below_since >= (int64_t)g->delestage_restore_min * 60 * 1000000) {
            restore_locked(false);
        }
    }
    unlock();
}

static void delestage_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        tick();
    }
}

esp_err_t delestage_init(void)
{
    if (s_mx) return ESP_OK;
    s_mx = xSemaphoreCreateMutex();
    if (!s_mx) return ESP_ERR_NO_MEM;
    for (int i = 0; i < AVG_SLOTS; i++) for (int c = 0; c < 4; c++) s_samp[i][c] = -1;
    load_file();
    // Pile en RAM interne : la tache ecrit l'etat en flash
    if (xTaskCreate(delestage_task, "delestage", 4096, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

bool delestage_request_restore(void)
{
    lock();
    bool active = s_active;
    unlock();
    if (active) s_restore_req = true;
    return active;
}

char *delestage_json(void)
{
    const cfg_general_t *g = cfg_get_general();
    energy_live_t L;
    bool live = energy_service_get_live(&L) && L.valid;
    cJSON *root = cJSON_CreateObject();
    lock();
    cJSON_AddBoolToObject(root, "active", s_active);
    if (s_since[0]) cJSON_AddStringToObject(root, "since", s_since); else cJSON_AddNullToObject(root, "since");
    if (s_last_end[0]) cJSON_AddStringToObject(root, "lastEnd", s_last_end); else cJSON_AddNullToObject(root, "lastEnd");
    cJSON_AddNumberToObject(root, "restoreMinutes", g->delestage_restore_min);
    cJSON_AddNumberToObject(root, "restorePercent", g->delestage_restore_pct);
    cJSON_AddNumberToObject(root, "anticipationPercent", g->delestage_anticip_pct);
    if (s_reason[0]) cJSON_AddStringToObject(root, "reason", s_reason); else cJSON_AddNullToObject(root, "reason");
    cJSON_AddNumberToObject(root, "minuteVA", minute_avg(0));
    cJSON_AddNumberToObject(root, "minuteLoadPercent", live ? minute_load_pct(&L) : -1);
    // Puissance souscrite totale (Historique triphase : ISOUSC x 200 x 3, comme la Box)
    cJSON_AddNumberToObject(root, "subscribedVA", live ? (L.mode == 2 ? L.goal_va * 3 : L.goal_va) : 0);
    cJSON *arr = cJSON_AddArrayToObject(root, "devices");
    auto add = [&](const char *id, bool cut) {
        const cJSON *o;
        cJSON_ArrayForEach(o, arr) {
            const cJSON *e = cJSON_GetObjectItem(o, "ieee");
            if (cJSON_IsString(e) && !strcasecmp(e->valuestring, id)) return;
        }
        const zigate_device_t *d = zigate_devices_find_by_ieee(ieee_of(id));
        cJSON *n = cJSON_CreateObject();
        cJSON_AddStringToObject(n, "ieee", id);
        cJSON_AddStringToObject(n, "name", d ? (d->alias[0] ? d->alias : d->model) : "");
        cJSON_AddBoolToObject(n, "cut", cut);
        cJSON_AddItemToArray(arr, n);
    };
    if (s_active) for (int i = 0; i < s_count; i++) add(s_dev[i].ieee, true);
    for_each_configured([&](const char *id) { add(id, false); });
    unlock();
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}
