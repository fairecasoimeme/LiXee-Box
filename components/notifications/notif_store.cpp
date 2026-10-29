// ============================================================================
// Notifications - magasin (portage de NotificationManager, notificationManager.cpp)
//
// Differences avec la LiXee-Box :
//   - chaque notification a un identifiant stable ("id"). La LiXee-Box
//     designait les notifications par leur INDEX dans le vecteur : entre
//     l'affichage de la page et le clic, un ajout ou une suppression decalait
//     les index et l'action portait sur une autre notification ;
//   - l'ecriture en flash est differee (notifications_tick) au lieu d'avoir
//     lieu a chaque ajout, depuis la tache Zigbee ;
//   - quand le magasin est plein, seule la plus ancienne est retiree (la
//     LiXee-Box en retirait 10 d'un coup).
// ============================================================================

#include "notifications.h"
#include "notif_internal.h"
#include "config_manager.h"
#include "ntp_sync.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "NOTIF";

// ============================================================================
// Tampon de texte extensible
// ============================================================================

void *notif_psram_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

void nbuf_init(nbuf_t *b, size_t cap)
{
    b->len = 0;
    b->oom = false;
    b->cap = cap < 64 ? 64 : cap;
    b->buf = (char *)notif_psram_alloc(b->cap);
    if (!b->buf) { b->cap = 0; b->oom = true; }
    else b->buf[0] = '\0';
}

static bool nbuf_reserve(nbuf_t *b, size_t extra)
{
    if (b->oom) return false;
    if (b->len + extra + 1 <= b->cap) return true;
    size_t ncap = b->cap * 2;
    while (ncap < b->len + extra + 1) ncap *= 2;
    char *n = (char *)heap_caps_realloc(b->buf, ncap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!n) n = (char *)realloc(b->buf, ncap);
    if (!n) { b->oom = true; return false; }
    b->buf = n;
    b->cap = ncap;
    return true;
}

void nbuf_add(nbuf_t *b, const char *s, size_t n)
{
    if (!nbuf_reserve(b, n)) return;
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
}

void nbuf_str(nbuf_t *b, const char *s)
{
    if (s) nbuf_add(b, s, strlen(s));
}

void nbuf_printf(nbuf_t *b, const char *fmt, ...)
{
    if (b->oom) return;
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }
    if ((size_t)n >= b->cap - b->len) {
        if (nbuf_reserve(b, (size_t)n)) vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap2);
        else { va_end(ap2); return; }
    }
    va_end(ap2);
    b->len += (size_t)n;
}

void nbuf_json_str(nbuf_t *b, const char *s)
{
    nbuf_add(b, "\"", 1);
    if (s) {
        for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
            char esc[16];
            switch (*p) {
            case '"':  nbuf_add(b, "\\\"", 2); break;
            case '\\': nbuf_add(b, "\\\\", 2); break;
            case '\n': nbuf_add(b, "\\n", 2); break;
            case '\r': nbuf_add(b, "\\r", 2); break;
            case '\t': nbuf_add(b, "\\t", 2); break;
            default:
                if (*p < 0x20) {
                    // Sans printf : appele depuis la tache principale (pile de 3,5 Ko)
                    static const char HEX[] = "0123456789abcdef";
                    esc[0] = '\\'; esc[1] = 'u'; esc[2] = '0'; esc[3] = '0';
                    esc[4] = HEX[*p >> 4]; esc[5] = HEX[*p & 15];
                    nbuf_add(b, esc, 6);
                } else {
                    nbuf_add(b, (const char *)p, 1);
                }
            }
        }
    }
    nbuf_add(b, "\"", 1);
}

char *nbuf_finish(nbuf_t *b)
{
    if (b->oom) { nbuf_free(b); return NULL; }
    char *r = b->buf;
    b->buf = NULL;
    b->cap = b->len = 0;
    return r;
}

void nbuf_free(nbuf_t *b)
{
    free(b->buf);
    b->buf = NULL;
    b->cap = b->len = 0;
}

// ============================================================================
// Etat du magasin
// ============================================================================

typedef struct {
    uint32_t id;
    uint32_t ts;                        // epoch, 0 si heure inconnue
    int8_t   type;
    uint8_t  viewed;
    char     stamp[20];                 // "JJ/MM/AAAA HH:MM" (timeStamp Arduino)
    char     alert[NOTIF_ALERT_LEN];
    char     title[NOTIF_TITLE_LEN];
    char     msg[NOTIF_MSG_LEN];
} notif_entry_t;

typedef struct {
    int8_t state;
    char   msg[120];
} notif_toast_t;

#define TOAST_MAX 8

static notif_entry_t   *s_items = NULL;        // du plus ancien au plus recent (PSRAM)
static int              s_count = 0;
static uint32_t         s_next_id = 1;
static bool             s_dirty = false;
static int64_t          s_dirty_since = 0;
static uint32_t         s_poll[NOTIF_POLL_MAX];
static int              s_poll_n = 0;
static notif_toast_t   *s_toasts = NULL;
static int              s_toast_head = 0, s_toast_n = 0;
static notif_push_cb_t  s_push_cb = NULL;

static StaticSemaphore_t s_mtx_buf, s_file_mtx_buf;
static SemaphoreHandle_t s_mtx = NULL;         // donnees du magasin
static SemaphoreHandle_t s_file_mtx = NULL;    // ecriture du fichier (tick et routes web)

// Correction : notifications.json illisible (JSON casse, format inconnu,
// fichier trop grand) etait "ignore" : le magasin repartait vide et la
// premiere ecriture (3 s apres la premiere alerte) ECRASAIT le fichier, d'ou
// la perte definitive de l'historique. Comme rules.cpp (s_file_bad), le
// fichier n'est alors plus jamais reecrit... sauf s'il a pu etre mis de cote
// (renomme en notifications.json.bad) : son contenu est conserve et le
// magasin repart sur un fichier neuf.
#define NOTIF_BAD_SUFFIX ".bad"
static bool s_file_bad = false;                // ecriture du fichier interdite

static void lock(void)   { xSemaphoreTake(s_mtx, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mtx); }

static void mark_dirty(void)
{
    if (!s_dirty) s_dirty_since = esp_timer_get_time();
    s_dirty = true;
}

/** Copie tronquee sans couper un caractere UTF-8 multi-octets. */
static void copy_utf8(char *dst, const char *src, size_t size)
{
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlcpy(dst, src, size);
    if (n < size) return;
    size_t end = size - 1;              // dst[end] == '\0'
    size_t p = end;
    while (p > 0 && ((unsigned char)dst[p - 1] & 0xC0) == 0x80) p--;
    if (p == 0) return;
    // dst[p-1] : premier octet du dernier caractere ; incomplet -> retire
    unsigned char lead = (unsigned char)dst[p - 1];
    size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if ((p - 1) + need > end) dst[p - 1] = '\0';
}

static int find_index(uint32_t id)
{
    for (int i = 0; i < s_count; i++) {
        if (s_items[i].id == id) return i;
    }
    return -1;
}

void notif_format_stamp(char *out, size_t len)
{
    out[0] = '\0';
    if (!ntp_sync_is_synced()) return;
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    strftime(out, len, "%d/%m/%Y %H:%M", &t);
}

// ============================================================================
// Persistance
// ============================================================================

/**
 * Fichier illisible : mis de cote sous ".bad" (contenu conserve, le magasin
 * repart sur un fichier neuf) ; si c'est impossible, ecriture interdite pour
 * ne pas l'ecraser.
 */
static void set_aside_bad_file(const char *why)
{
    const char *bad = CFG_PATH_NOTIFICATIONS NOTIF_BAD_SUFFIX;
    if (rename(CFG_PATH_NOTIFICATIONS, bad) == 0) {
        ESP_LOGE(TAG, "%s %s : conserve sous %s, historique repris a zero",
                 CFG_PATH_NOTIFICATIONS, why, bad);
        s_file_bad = false;
    } else {
        ESP_LOGE(TAG, "%s %s et impossible a mettre de cote : il ne sera pas reecrit",
                 CFG_PATH_NOTIFICATIONS, why);
        s_file_bad = true;
    }
}

static void load_from_file(void)
{
    FILE *f = fopen(CFG_PATH_NOTIFICATIONS, "r");
    if (!f) return;                     // pas encore de fichier : normal
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 512 * 1024) {
        fclose(f);
        set_aside_bad_file(size <= 0 ? "vide" : "trop grand");
        return;
    }
    char *txt = (char *)notif_psram_alloc((size_t)size + 1);
    if (!txt) {
        fclose(f);
        // Fichier peut-etre sain : seulement ne pas l'ecraser pendant ce demarrage
        ESP_LOGE(TAG, "Memoire insuffisante pour lire %s : il ne sera pas reecrit", CFG_PATH_NOTIFICATIONS);
        s_file_bad = true;
        return;
    }
    size_t got = fread(txt, 1, (size_t)size, f);
    fclose(f);
    txt[got] = '\0';

    cJSON *doc = cJSON_Parse(txt);
    free(txt);
    if (!doc) {
        set_aside_bad_file("illisible");
        return;
    }

    const cJSON *arr = cJSON_IsArray(doc) ? doc : cJSON_GetObjectItem(doc, "notifications");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(doc);
        set_aside_bad_file("au format inconnu");
        return;
    }
    const cJSON *nid = cJSON_GetObjectItem(doc, "nextId");
    uint32_t next = cJSON_IsNumber(nid) && nid->valuedouble >= 1 ? (uint32_t)nid->valuedouble : 1;

    int total = cJSON_GetArraySize(arr);
    int skip = total > NOTIF_MAX ? total - NOTIF_MAX : 0;   // on garde les plus recentes
    int idx = 0;
    const cJSON *o = NULL;
    cJSON_ArrayForEach(o, arr) {
        if (idx++ < skip || !cJSON_IsObject(o)) continue;
        const cJSON *t = cJSON_GetObjectItem(o, "title");
        if (!cJSON_IsString(t)) continue;
        notif_entry_t *e = &s_items[s_count];
        memset(e, 0, sizeof(*e));
        copy_utf8(e->title, t->valuestring, sizeof(e->title));
        const cJSON *v;
        if (cJSON_IsString(v = cJSON_GetObjectItem(o, "message")))   copy_utf8(e->msg, v->valuestring, sizeof(e->msg));
        if (cJSON_IsString(v = cJSON_GetObjectItem(o, "timeStamp"))) strlcpy(e->stamp, v->valuestring, sizeof(e->stamp));
        if (cJSON_IsString(v = cJSON_GetObjectItem(o, "alertType"))) strlcpy(e->alert, v->valuestring, sizeof(e->alert));
        v = cJSON_GetObjectItem(o, "type");
        e->type = (int8_t)(cJSON_IsNumber(v) ? v->valueint : (cJSON_IsString(v) ? atoi(v->valuestring) : 0));
        v = cJSON_GetObjectItem(o, "viewed");
        e->viewed = (cJSON_IsNumber(v) && v->valueint) || cJSON_IsTrue(v) ? 1 : 0;
        v = cJSON_GetObjectItem(o, "ts");
        e->ts = cJSON_IsNumber(v) && v->valuedouble > 0 ? (uint32_t)v->valuedouble : 0;
        v = cJSON_GetObjectItem(o, "id");
        uint32_t id = cJSON_IsNumber(v) && v->valuedouble >= 1 ? (uint32_t)v->valuedouble : 0;
        // Fichier de la LiXee-Box (sans id) ou id en double : nouvel id
        if (id == 0 || find_index(id) >= 0) id = 0;
        e->id = id;
        s_count++;
        if (id >= next) next = id + 1;
    }
    for (int i = 0; i < s_count; i++) {
        if (s_items[i].id == 0) s_items[i].id = next++;
    }
    s_next_id = next ? next : 1;
    cJSON_Delete(doc);
    ESP_LOGI(TAG, "%d notifications chargees", s_count);
}

/** Entier decimal sans printf (serialisation depuis la tache principale, pile de 3,5 Ko). */
static void nbuf_long(nbuf_t *b, long long v)
{
    char t[24];
    int n = 0;
    bool neg = v < 0;
    unsigned long long u = neg ? (unsigned long long)(-(v + 1)) + 1 : (unsigned long long)v;
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u && n < 22);
    if (neg) t[n++] = '-';
    char o[24];
    for (int i = 0; i < n; i++) o[i] = t[n - 1 - i];
    nbuf_add(b, o, (size_t)n);
}

/** Serialise le magasin (appele verrou pris). */
static char *serialize_locked(void)
{
    nbuf_t b;
    nbuf_init(&b, 512 + (size_t)s_count * 320);
    nbuf_str(&b, "{\"nextId\":");
    nbuf_long(&b, (long long)s_next_id);
    nbuf_str(&b, ",\"notifications\":[");
    for (int i = 0; i < s_count; i++) {
        const notif_entry_t *e = &s_items[i];
        if (i) nbuf_add(&b, ",", 1);
        nbuf_str(&b, "{\"id\":");
        nbuf_long(&b, (long long)e->id);
        nbuf_str(&b, ",\"title\":");
        nbuf_json_str(&b, e->title);
        nbuf_str(&b, ",\"message\":");
        nbuf_json_str(&b, e->msg);
        nbuf_str(&b, ",\"timeStamp\":");
        nbuf_json_str(&b, e->stamp);
        nbuf_str(&b, ",\"ts\":");
        nbuf_long(&b, (long long)e->ts);
        nbuf_str(&b, ",\"type\":");
        nbuf_long(&b, e->type);
        nbuf_str(&b, ",\"viewed\":");
        nbuf_long(&b, e->viewed);
        nbuf_str(&b, ",\"alertType\":");
        nbuf_json_str(&b, e->alert);
        nbuf_add(&b, "}", 1);
    }
    nbuf_str(&b, "]}");
    return nbuf_finish(&b);
}

esp_err_t notifications_flush(void)
{
    if (!s_items) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_file_mtx, portMAX_DELAY);
    lock();
    if (s_file_bad) {
        // Fichier illisible non mis de cote : ne jamais l'ecraser (voir
        // set_aside_bad_file). Drapeau 'dirty' efface : pas de nouvel essai
        // a chaque tick, les notifications restent en RAM.
        s_dirty = false;
        unlock();
        xSemaphoreGive(s_file_mtx);
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_dirty) { unlock(); xSemaphoreGive(s_file_mtx); return ESP_OK; }
    char *txt = serialize_locked();
    if (txt) s_dirty = false;
    unlock();

    esp_err_t err = ESP_ERR_NO_MEM;
    if (txt) {
        err = cfg_write_file(CFG_PATH_NOTIFICATIONS, txt);
        free(txt);
        if (err != ESP_OK) {
            lock();
            mark_dirty();               // nouvel essai au prochain tick
            unlock();
        }
    }
    xSemaphoreGive(s_file_mtx);
    if (err != ESP_OK) ESP_LOGW(TAG, "Ecriture de %s impossible (%s)", CFG_PATH_NOTIFICATIONS, esp_err_to_name(err));
    return err;
}

// Historique des alertes (alertHistory.cpp de la LiXee-Box) : anneau de 20
// entrees en RAM, alimente par les alertes typees (alert_type non NULL)
#define ALERT_HIST_MAX 20
typedef struct {
    uint32_t id;
    char     stamp[20];
    char     type[NOTIF_ALERT_LEN];
    int8_t   level;
    char     title[NOTIF_TITLE_LEN];
    char     msg[NOTIF_MSG_LEN];
    float    value, threshold;
} alert_hist_t;
static alert_hist_t *s_alerts = NULL;       // PSRAM, a la premiere alerte
static int           s_alert_n = 0;
static uint32_t      s_alert_next = 1;
static bool          s_alert_dirty;         // a ecrire (tache principale, notifications_tick)
static int64_t       s_alert_dirty_since;
#define ALERT_FILE   "/littlefs/hst/alerts.json"   // alertHistory.cpp de la Box
#define ALERT_DEDUP_S 600                    // meme alerte ignoree pendant 10 min

static void alert_hist_add(const char *stamp, const char *alert_type, int level, const char *title,
                           const char *msg, float value, float threshold)
{
    if (!s_alerts) {
        s_alerts = (alert_hist_t *)heap_caps_calloc(ALERT_HIST_MAX, sizeof(alert_hist_t), MALLOC_CAP_SPIRAM);
        if (!s_alerts) return;
    }
    // Doublon : meme titre et meme message il y a moins de 10 min (alertHistoryAdd)
    static int64_t s_last_add_us;
    if (s_alert_n > 0) {
        const alert_hist_t *l = &s_alerts[s_alert_n - 1];
        if (!strcmp(l->title, title) && !strcmp(l->msg, msg) &&
            esp_timer_get_time() - s_last_add_us < (int64_t)ALERT_DEDUP_S * 1000000) return;
    }
    s_last_add_us = esp_timer_get_time();
    if (s_alert_n >= ALERT_HIST_MAX) {
        memmove(&s_alerts[0], &s_alerts[1], sizeof(alert_hist_t) * (ALERT_HIST_MAX - 1));
        s_alert_n = ALERT_HIST_MAX - 1;
    }
    alert_hist_t *a = &s_alerts[s_alert_n++];
    memset(a, 0, sizeof(*a));
    a->id = s_alert_next++;
    strlcpy(a->stamp, stamp, sizeof(a->stamp));
    strlcpy(a->type, alert_type && alert_type[0] ? alert_type : "info", sizeof(a->type));
    a->level = (int8_t)level;
    copy_utf8(a->title, title, sizeof(a->title));
    copy_utf8(a->msg, msg, sizeof(a->msg));
    a->value = value;
    a->threshold = threshold;
    if (!s_alert_dirty) s_alert_dirty_since = esp_timer_get_time();
    s_alert_dirty = true;
}

/** Relit l'historique des alertes et le dernier numero (demarrage). */
static void alerts_load(void)
{
    FILE *f = fopen(ALERT_FILE, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (n > 0 && n < 64 * 1024) ? (char *)heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM) : NULL;
    size_t got = buf ? fread(buf, 1, n, f) : 0;
    fclose(f);
    if (!buf) return;
    buf[got] = '\0';
    cJSON *doc = cJSON_Parse(buf);
    heap_caps_free(buf);
    if (!doc) return;
    if (!s_alerts) s_alerts = (alert_hist_t *)heap_caps_calloc(ALERT_HIST_MAX, sizeof(alert_hist_t), MALLOC_CAP_SPIRAM);
    const cJSON *o;
    s_alert_n = 0;
    uint32_t last = 0;
    cJSON_ArrayForEach(o, cJSON_GetObjectItem(doc, "alerts")) {
        if (!s_alerts || s_alert_n >= ALERT_HIST_MAX) break;
        alert_hist_t *a = &s_alerts[s_alert_n++];
        memset(a, 0, sizeof(*a));
        const cJSON *v;
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "id"))) a->id = (uint32_t)v->valuedouble;
        if (cJSON_IsString(v = cJSON_GetObjectItem(o, "ts"))) strlcpy(a->stamp, v->valuestring, sizeof(a->stamp));
        if (cJSON_IsString(v = cJSON_GetObjectItem(o, "type"))) strlcpy(a->type, v->valuestring, sizeof(a->type));
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "level"))) a->level = (int8_t)v->valueint;
        if (cJSON_IsString(v = cJSON_GetObjectItem(o, "title"))) copy_utf8(a->title, v->valuestring, sizeof(a->title));
        if (cJSON_IsString(v = cJSON_GetObjectItem(o, "message"))) copy_utf8(a->msg, v->valuestring, sizeof(a->msg));
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "value"))) a->value = (float)v->valuedouble;
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "threshold"))) a->threshold = (float)v->valuedouble;
        if (a->id > last) last = a->id;
    }
    const cJSON *li = cJSON_GetObjectItem(doc, "lastId");
    if (cJSON_IsNumber(li) && (uint32_t)li->valuedouble > last) last = (uint32_t)li->valuedouble;
    s_alert_next = last + 1;
    cJSON_Delete(doc);
}

char *notifications_alerts_json(uint32_t since)
{
    nbuf_t b;
    nbuf_init(&b, 512 + ALERT_HIST_MAX * 600);
    lock();
    nbuf_str(&b, "{\"alerts\":[");
    int n = 0;
    uint32_t last = 0;
    for (int i = 0; s_alerts && i < s_alert_n; i++) {
        const alert_hist_t *a = &s_alerts[i];
        if (a->id > last) last = a->id;
        if (a->id <= since) continue;
        if (n++) nbuf_add(&b, ",", 1);
        nbuf_printf(&b, "{\"id\":%lu,\"ts\":", (unsigned long)a->id);
        nbuf_json_str(&b, a->stamp);
        nbuf_str(&b, ",\"type\":");
        nbuf_json_str(&b, a->type);
        nbuf_printf(&b, ",\"level\":%d,\"title\":", a->level);
        nbuf_json_str(&b, a->title);
        nbuf_str(&b, ",\"message\":");
        nbuf_json_str(&b, a->msg);
        nbuf_printf(&b, ",\"value\":%.2f,\"threshold\":%.2f}", (double)a->value, (double)a->threshold);
    }
    unlock();
    nbuf_printf(&b, "],\"lastId\":%lu}", (unsigned long)last);
    return nbuf_finish(&b);
}

void notifications_tick(void)
{
    if (!s_items) return;
    // Regroupe les rafales (plusieurs alertes du meme rapport) en une ecriture
    bool due;
    lock();
    due = s_dirty && (esp_timer_get_time() - s_dirty_since) >= 3000000LL;
    bool alerts_due = s_alert_dirty && (esp_timer_get_time() - s_alert_dirty_since) >= 3000000LL;
    unlock();
    if (due) notifications_flush();
    if (alerts_due) {
        // Historique des alertes (et dernier numero) : tache principale
        char *js = notifications_alerts_json(0);
        if (js) {
            lock();
            s_alert_dirty = false;
            unlock();
            if (cfg_write_file(ALERT_FILE, js) != ESP_OK) {
                lock();
                s_alert_dirty = true;
                unlock();
            }
            free(js);
        }
    }
}

// ============================================================================
// Cycle de vie
// ============================================================================

esp_err_t notifications_init(void)
{
    if (s_items) return ESP_OK;
    s_mtx = xSemaphoreCreateMutexStatic(&s_mtx_buf);
    s_file_mtx = xSemaphoreCreateMutexStatic(&s_file_mtx_buf);
    s_items = (notif_entry_t *)notif_psram_alloc(sizeof(notif_entry_t) * NOTIF_MAX);
    s_toasts = (notif_toast_t *)notif_psram_alloc(sizeof(notif_toast_t) * TOAST_MAX);
    if (!s_items || !s_toasts) {
        free(s_items);
        free(s_toasts);
        s_items = NULL;
        s_toasts = NULL;
        ESP_LOGE(TAG, "Memoire insuffisante pour le magasin de notifications");
        return ESP_ERR_NO_MEM;
    }
    s_count = 0;
    load_from_file();
    alerts_load();
    esp_err_t err = notif_worker_start();
    if (err != ESP_OK) ESP_LOGE(TAG, "Tache notif non demarree : %s", esp_err_to_name(err));
    return ESP_OK;
}

void notifications_set_push_callback(notif_push_cb_t cb)
{
    s_push_cb = cb;
}

// ============================================================================
// Ajout
// ============================================================================

uint32_t notifications_add_alert(const char *title, const char *message, int type,
                                 const char *alert_type, float value, float threshold)
{
    if (!s_items || !title || !title[0]) return 0;
    if (type < 0) type = 0;
    if (type > 3) type = 3;

    char stamp[20];
    notif_format_stamp(stamp, sizeof(stamp));
    uint32_t ts = ntp_sync_is_synced() ? (uint32_t)time(NULL) : 0;

    lock();
    if (s_count >= NOTIF_MAX) {
        memmove(&s_items[0], &s_items[1], sizeof(notif_entry_t) * (NOTIF_MAX - 1));
        s_count = NOTIF_MAX - 1;
    }
    notif_entry_t *e = &s_items[s_count++];
    memset(e, 0, sizeof(*e));
    e->id = s_next_id++;
    if (s_next_id == 0) s_next_id = 1;
    e->ts = ts;
    e->type = (int8_t)type;
    strlcpy(e->stamp, stamp, sizeof(e->stamp));
    copy_utf8(e->title, title, sizeof(e->title));
    copy_utf8(e->msg, message ? message : "", sizeof(e->msg));
    if (alert_type) strlcpy(e->alert, alert_type, sizeof(e->alert));
    uint32_t id = e->id;
    if (alert_type) alert_hist_add(stamp, alert_type, type, title, message ? message : "", value, threshold);

    // File /poll (LiXee-Assist) : la plus ancienne part si elle est pleine,
    // comme notifList->shift() + push()
    if (s_poll_n >= NOTIF_POLL_MAX) {
        memmove(&s_poll[0], &s_poll[1], sizeof(s_poll[0]) * (NOTIF_POLL_MAX - 1));
        s_poll_n = NOTIF_POLL_MAX - 1;
    }
    s_poll[s_poll_n++] = id;
    mark_dirty();
    notif_push_cb_t cb = s_push_cb;
    unlock();

    ESP_LOGI(TAG, "Notification %lu (type %d) : %s", (unsigned long)id, type, title);
    notif_worker_queue_mail(id, type);
    if (cb) cb(title, message ? message : "", type, alert_type, value, threshold);
    return id;
}

uint32_t notifications_add(const char *title, const char *message, int type)
{
    return notifications_add_alert(title, message, type, NULL, 0, 0);
}

bool notif_store_get(uint32_t id, char *title, size_t tlen, char *msg, size_t mlen,
                     char *stamp, size_t slen, int *type)
{
    if (!s_items) return false;
    bool ok = false;
    lock();
    int i = find_index(id);
    if (i >= 0) {
        strlcpy(title, s_items[i].title, tlen);
        strlcpy(msg, s_items[i].msg, mlen);
        strlcpy(stamp, s_items[i].stamp, slen);
        if (type) *type = s_items[i].type;
        ok = true;
    }
    unlock();
    return ok;
}

// ============================================================================
// Gestion
// ============================================================================

bool notifications_mark_read(uint32_t id)
{
    if (!s_items) return false;
    lock();
    int i = find_index(id);
    if (i >= 0 && !s_items[i].viewed) {
        s_items[i].viewed = 1;
        mark_dirty();
    }
    unlock();
    return i >= 0;
}

int notifications_mark_all_read(void)
{
    if (!s_items) return 0;
    int n = 0;
    lock();
    for (int i = 0; i < s_count; i++) {
        if (!s_items[i].viewed) { s_items[i].viewed = 1; n++; }
    }
    if (n) mark_dirty();
    unlock();
    return n;
}

bool notifications_delete(uint32_t id)
{
    if (!s_items) return false;
    lock();
    int i = find_index(id);
    if (i >= 0) {
        memmove(&s_items[i], &s_items[i + 1], sizeof(notif_entry_t) * (size_t)(s_count - i - 1));
        s_count--;
        mark_dirty();
    }
    unlock();
    return i >= 0;
}

void notifications_clear(void)
{
    if (!s_items) return;
    lock();
    s_count = 0;
    s_poll_n = 0;
    mark_dirty();
    unlock();
}

int notifications_count(void)
{
    return s_count;
}

static int unread_locked(void)
{
    int n = 0;
    for (int i = 0; i < s_count; i++) if (!s_items[i].viewed) n++;
    return n;
}

int notifications_unread(void)
{
    if (!s_items) return 0;
    lock();
    int n = unread_locked();
    unlock();
    return n;
}

// ============================================================================
// JSON pour les routes web
// ============================================================================

static void entry_json(nbuf_t *b, const notif_entry_t *e)
{
    nbuf_printf(b, "{\"id\":%lu,\"title\":", (unsigned long)e->id);
    nbuf_json_str(b, e->title);
    nbuf_str(b, ",\"message\":");
    nbuf_json_str(b, e->msg);
    nbuf_str(b, ",\"timeStamp\":");
    nbuf_json_str(b, e->stamp);
    nbuf_printf(b, ",\"ts\":%lu,\"type\":%d,\"viewed\":%d,\"alertType\":",
                (unsigned long)e->ts, e->type, e->viewed);
    nbuf_json_str(b, e->alert);
    nbuf_add(b, "}", 1);
}

char *notifications_list_json(int offset, int limit)
{
    if (!s_items) return NULL;
    if (offset < 0) offset = 0;
    if (limit < 1) limit = 1;
    if (limit > 50) limit = 50;
    nbuf_t b;
    nbuf_init(&b, 256 + (size_t)limit * 700);
    lock();
    nbuf_printf(&b, "{\"total\":%d,\"unread\":%d,\"offset\":%d,\"limit\":%d,\"page\":%d,\"notifications\":[",
                s_count, unread_locked(), offset, limit, offset / limit + 1);
    // Plus recentes d'abord, comme NotificationManager::toJson()
    int n = 0;
    for (int i = s_count - 1 - offset; i >= 0 && n < limit; i--, n++) {
        if (n) nbuf_add(&b, ",", 1);
        entry_json(&b, &s_items[i]);
    }
    unlock();
    nbuf_str(&b, "]}");
    return nbuf_finish(&b);
}

char *notifications_stats_json(void)
{
    nbuf_t b;
    nbuf_init(&b, 192);
    int total = 0, unread = 0;
    if (s_items) {
        lock();
        total = s_count;
        unread = unread_locked();
        unlock();
    }
    nbuf_printf(&b, "{\"total\":%d,\"unread\":%d,\"read\":%d,\"maxCapacity\":%d,"
                    "\"psramFree\":%u,\"psramTotal\":%u}",
                total, unread, total - unread, NOTIF_MAX,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    return nbuf_finish(&b);
}

char *notifications_debug_json(void)
{
    if (!s_items) return NULL;
    nbuf_t b;
    nbuf_init(&b, 256 + (size_t)s_count * 140);
    nbuf_str(&b, "{\"notifications\":[");
    lock();
    for (int i = 0; i < s_count; i++) {
        if (i) nbuf_add(&b, ",", 1);
        nbuf_printf(&b, "{\"index\":%d,\"id\":%lu,\"title\":", i, (unsigned long)s_items[i].id);
        nbuf_json_str(&b, s_items[i].title);
        nbuf_printf(&b, ",\"viewed\":%d}", s_items[i].viewed);
    }
    nbuf_printf(&b, "],\"total\":%d,\"dirty\":%s,\"nextId\":%lu,\"psramFree\":%u}",
                s_count, s_dirty ? "true" : "false", (unsigned long)s_next_id,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    unlock();
    return nbuf_finish(&b);
}

void notifications_poll_add_to(struct cJSON *root)
{
    if (!root) return;
    cJSON *arr = cJSON_AddArrayToObject(root, "notifications");
    if (!arr || !s_items) return;
    lock();
    for (int k = 0; k < s_poll_n; k++) {
        int i = find_index(s_poll[k]);
        if (i < 0) continue;            // supprimee entre-temps
        const notif_entry_t *e = &s_items[i];
        cJSON *o = cJSON_CreateObject();
        if (!o) break;
        cJSON_AddNumberToObject(o, "id", e->id);
        cJSON_AddStringToObject(o, "title", e->title);
        cJSON_AddStringToObject(o, "message", e->msg);
        cJSON_AddStringToObject(o, "timeStamp", e->stamp);
        cJSON_AddNumberToObject(o, "type", e->type);
        cJSON_AddItemToArray(arr, o);
    }
    s_poll_n = 0;                       // file videe, comme notifList->take()
    unlock();
}

// ============================================================================
// Bandeaux (/getAlert)
// ============================================================================

void notifications_toast(int state, const char *msg)
{
    if (!s_toasts || !msg || !msg[0]) return;
    lock();
    if (s_toast_n >= TOAST_MAX) {       // plein : le plus ancien est perdu
        s_toast_head = (s_toast_head + 1) % TOAST_MAX;
        s_toast_n--;
    }
    notif_toast_t *t = &s_toasts[(s_toast_head + s_toast_n) % TOAST_MAX];
    t->state = (int8_t)state;
    copy_utf8(t->msg, msg, sizeof(t->msg));
    s_toast_n++;
    unlock();
}

bool notifications_toast_take(int *state, char *msg, size_t len)
{
    if (!s_toasts) return false;
    bool ok = false;
    lock();
    if (s_toast_n > 0) {
        notif_toast_t *t = &s_toasts[s_toast_head];
        if (state) *state = t->state;
        strlcpy(msg, t->msg, len);
        s_toast_head = (s_toast_head + 1) % TOAST_MAX;
        s_toast_n--;
        ok = true;
    }
    unlock();
    return ok;
}
