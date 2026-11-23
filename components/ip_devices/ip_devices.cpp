// ============================================================================
// Appareils IP (Shelly, Tasmota...) - voir ip_devices.h et IP_DEVICES.md
// ============================================================================
#include "ip_devices.h"
#include "sdkconfig.h"

#if !CONFIG_LIXEE_IP_DEVICES
// Module retire a la compilation : fonctions vides
esp_err_t ip_devices_init(void) { return ESP_OK; }
void ip_devices_register_report_cb(ipdev_report_cb_t) {}
bool ip_devices_enabled(void) { return false; }
esp_err_t ip_devices_set_enabled(bool) { return ESP_ERR_NOT_SUPPORTED; }
bool ip_devices_is_ip(uint64_t) { return false; }
int  ip_devices_count(void) { return 0; }
bool ip_devices_get(int, ipdev_info_t *) { return false; }
bool ip_devices_find(uint64_t, ipdev_info_t *) { return false; }
esp_err_t ip_devices_send_action(uint64_t, int, uint8_t, int32_t) { return ESP_ERR_NOT_SUPPORTED; }
void ip_devices_poll_now(uint64_t) {}
char *ip_devices_list_json(void) { return NULL; }
char *ip_devices_models_json(void) { return NULL; }
char *ip_devices_probe_json(const char *, uint16_t, const char *, const char *) { return NULL; }
esp_err_t ip_devices_add(const char *, uint16_t, const char *, const char *, const char *, int, const char *,
                         uint64_t *, char *, size_t) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t ip_devices_update(uint64_t, const char *, uint16_t, const char *, const char *, const char *, int,
                            char *, size_t) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t ip_devices_remove(uint64_t, bool) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t ip_devices_set_matter_endpoint(uint64_t, uint16_t) { return ESP_ERR_NOT_SUPPORTED; }
#else

#include "device_data.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "lwip/inet.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

static const char *TAG = "IPDEV";

#define LIST_FILE      "/littlefs/db/ip_devices.json"
#define TPL_USER       "/littlefs/tpu/" IPDEV_TEMPLATE_FILE
#define TPL_BASE       "/littlefs/tp/" IPDEV_TEMPLATE_FILE
#define HTTP_TIMEOUT   3000
#define RESP_MAX       8192
#define RESEND_S       300          // valeur inchangee republiee au moins toutes les 5 min
#define BACKOFF_MAX_S  300

#define M_MAX     16
#define ST_MAX    16
#define ACT_MAX   8
#define POLL_MAX  6
#define DET_MAX   3
#define MAP_MAX   4
#define URL_LEN   112
#define PATH_LEN  64

// ----------------------------------------------------------------------------
// Modeles (tp/ip.json)
// ----------------------------------------------------------------------------

typedef enum { AUTH_NONE = 0, AUTH_BASIC, AUTH_DIGEST, AUTH_TASMOTA } auth_t;

typedef struct { char url[URL_LEN]; char path[PATH_LEN]; bool has_gt; double gt; } urlpath_t;

typedef struct {
    uint16_t cluster, attr;
    int8_t   src;               // index de l'URL de releve ("src"), -1 : n'importe laquelle
    char     path[PATH_LEN];
    double   scale;
    bool     is_bool;
    int      nmap;
    char     mtxt[MAP_MAX][12];
    int32_t  mval[MAP_MAX];
} mstatus_t;

typedef struct {
    int      command;
    int32_t  value;
    uint8_t  endpoint;
    char     url[URL_LEN];
} maction_t;

typedef struct {
    char      key[32];
    char      label[48];
    char      vendor[24];
    auth_t    auth;
    int       npoll;
    char      poll[POLL_MAX][URL_LEN];
    int       ndet;
    urlpath_t det[DET_MAX];
    urlpath_t mac, name;
    int       nst;
    mstatus_t st[ST_MAX];
    int       nact;
    maction_t act[ACT_MAX];
} model_t;

static model_t *s_models;
static int      s_nmodels;

// ----------------------------------------------------------------------------
// Appareils
// ----------------------------------------------------------------------------

typedef struct {
    bool     used;
    uint64_t ieee;
    char     ip[40];
    uint16_t port;
    char     model[32];
    char     user[32];
    char     pass[64];
    uint16_t interval;
    char     key[33];               // cle de reveil (lot 3)
    uint16_t matter_ep;
    // Etat (non enregistre)
    bool     online;
    int64_t  last_ok_us;
    int64_t  next_us;
    uint8_t  fails;
    uint32_t polls, errors;
    char     last_error[48];
    bool     has_val[ST_MAX];
    int32_t  last_val[ST_MAX];
    int64_t  sent_us[ST_MAX];
} ipdev_entry_t;

static ipdev_entry_t            *s_dev;
static SemaphoreHandle_t s_mx;
static bool              s_enabled;
static TaskHandle_t      s_task;
static QueueHandle_t     s_q;
static ipdev_report_cb_t s_cb;

typedef enum { JOB_POLL = 1, JOB_ACTION } job_kind_t;
typedef struct { job_kind_t kind; uint64_t ieee; int command; uint8_t endpoint; int32_t value; } job_t;

static void lock(void)   { xSemaphoreTakeRecursive(s_mx, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGiveRecursive(s_mx); }
static int64_t now_us(void) { return esp_timer_get_time(); }

static void jstr(const cJSON *o, const char *k, char *out, size_t len)
{
    const cJSON *v = cJSON_GetObjectItem(o, k);
    if (cJSON_IsString(v) && v->valuestring) strlcpy(out, v->valuestring, len);
}

static double jnum(const cJSON *o, const char *k, double def)
{
    const cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

static char *read_file(const char *path, size_t max)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char *buf = (char *)heap_caps_malloc(max + 1, MALLOC_CAP_SPIRAM);
    if (!buf) buf = (char *)malloc(max + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, max, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static const model_t *find_model(const char *key)
{
    for (int i = 0; i < s_nmodels; i++) if (!strcmp(s_models[i].key, key)) return &s_models[i];
    return NULL;
}

static void parse_urlpath(const cJSON *o, urlpath_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!cJSON_IsObject(o)) return;
    jstr(o, "url", out->url, sizeof(out->url));
    jstr(o, "path", out->path, sizeof(out->path));
    // "gt" : la valeur doit aussi etre un nombre superieur (ex. num_meters > 0)
    const cJSON *g = cJSON_GetObjectItem(o, "gt");
    if (cJSON_IsNumber(g)) { out->has_gt = true; out->gt = g->valuedouble; }
}

static void load_models(void)
{
    if (!s_models) {
        s_models = (model_t *)heap_caps_calloc(M_MAX, sizeof(model_t), MALLOC_CAP_SPIRAM);
        if (!s_models) s_models = (model_t *)calloc(M_MAX, sizeof(model_t));
        if (!s_models) return;
    }
    s_nmodels = 0;
    char *txt = read_file(TPL_USER, 64 * 1024);
    if (!txt) txt = read_file(TPL_BASE, 64 * 1024);
    if (!txt) { ESP_LOGW(TAG, "Templates %s introuvables", IPDEV_TEMPLATE_FILE); return; }
    cJSON *root = cJSON_Parse(txt);
    free(txt);
    if (!root) { ESP_LOGE(TAG, "%s illisible", IPDEV_TEMPLATE_FILE); return; }
    const cJSON *it;
    cJSON_ArrayForEach(it, root) {
        if (s_nmodels >= M_MAX) break;
        const cJSON *m = cJSON_GetArrayItem(it, 0);
        const cJSON *h = cJSON_GetObjectItem(m, "http");
        if (!it->string || !cJSON_IsObject(h)) continue;
        model_t *md = &s_models[s_nmodels];
        memset(md, 0, sizeof(*md));
        strlcpy(md->key, it->string, sizeof(md->key));
        jstr(h, "label", md->label, sizeof(md->label));
        if (!md->label[0]) strlcpy(md->label, md->key, sizeof(md->label));
        jstr(h, "vendor", md->vendor, sizeof(md->vendor));
        char a[12] = "";
        jstr(h, "auth", a, sizeof(a));
        md->auth = !strcmp(a, "basic") ? AUTH_BASIC : !strcmp(a, "digest") ? AUTH_DIGEST
                 : !strcmp(a, "tasmota") ? AUTH_TASMOTA : AUTH_NONE;
        const cJSON *p;
        cJSON_ArrayForEach(p, cJSON_GetObjectItem(h, "poll")) {
            if (md->npoll < POLL_MAX && cJSON_IsString(p)) strlcpy(md->poll[md->npoll++], p->valuestring, URL_LEN);
        }
        cJSON_ArrayForEach(p, cJSON_GetObjectItem(h, "detect")) {
            if (md->ndet < DET_MAX) parse_urlpath(p, &md->det[md->ndet++]);
        }
        parse_urlpath(cJSON_GetObjectItem(h, "mac"), &md->mac);
        parse_urlpath(cJSON_GetObjectItem(h, "name"), &md->name);
        cJSON_ArrayForEach(p, cJSON_GetObjectItem(m, "status")) {
            const cJSON *path = cJSON_GetObjectItem(p, "json");
            const cJSON *cl = cJSON_GetObjectItem(p, "cluster");
            if (md->nst >= ST_MAX || !cJSON_IsString(path) || !cJSON_IsString(cl)) continue;
            mstatus_t *s = &md->st[md->nst++];
            s->cluster = (uint16_t)strtoul(cl->valuestring, NULL, 16);
            s->attr = (uint16_t)jnum(p, "attribut", 0);
            strlcpy(s->path, path->valuestring, sizeof(s->path));
            s->scale = jnum(p, "scale", 1);
            // "src" (et non "poll", lu par device_data pour la scrutation Zigbee) :
            // reponse a lire quand plusieurs URL donnent les memes noms (voies EM1)
            s->src = (int8_t)jnum(p, "src", -1);
            s->is_bool = s->cluster == 0x0006 && s->attr == 0x0000;
            const cJSON *mp, *e;
            if (cJSON_IsObject(mp = cJSON_GetObjectItem(p, "map"))) {
                cJSON_ArrayForEach(e, mp) {
                    if (s->nmap >= MAP_MAX || !cJSON_IsNumber(e)) continue;
                    strlcpy(s->mtxt[s->nmap], e->string, sizeof(s->mtxt[0]));
                    s->mval[s->nmap++] = e->valueint;
                }
            }
        }
        cJSON_ArrayForEach(p, cJSON_GetObjectItem(m, "action")) {
            const cJSON *u = cJSON_GetObjectItem(p, "http");
            if (md->nact >= ACT_MAX || !cJSON_IsString(u)) continue;
            maction_t *ac = &md->act[md->nact++];
            ac->command = (int)jnum(p, "command", 0);
            ac->value = (int32_t)jnum(p, "value", 0);
            ac->endpoint = (uint8_t)jnum(p, "endpoint", 0);
            strlcpy(ac->url, u->valuestring, sizeof(ac->url));
        }
        s_nmodels++;
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "%d modele(s) d'appareil IP", s_nmodels);
}

// ----------------------------------------------------------------------------
// Chemins JSON : "a.b[0].c", "*" = n'importe quel enfant
// ----------------------------------------------------------------------------

static const cJSON *jpath(const cJSON *node, const char *path)
{
    if (!node) return NULL;
    if (!*path) return node;
    // Segment courant
    char seg[PATH_LEN];
    size_t n = strcspn(path, ".");
    if (n >= sizeof(seg)) return NULL;
    memcpy(seg, path, n);
    seg[n] = '\0';
    const char *rest = path[n] == '.' ? path + n + 1 : path + n;
    // Index de tableau : "nom[2]"
    char *br = strchr(seg, '[');
    int idx = -1;
    if (br) { idx = atoi(br + 1); *br = '\0'; }
    const cJSON *next;
    if (!strcmp(seg, "*")) {
        const cJSON *c;
        cJSON_ArrayForEach(c, node) {
            const cJSON *x = c;
            if (idx >= 0) x = cJSON_GetArrayItem(c, idx);
            const cJSON *r = jpath(x, rest);
            if (r) return r;
        }
        return NULL;
    }
    next = seg[0] ? cJSON_GetObjectItemCaseSensitive(node, seg) : node;
    if (idx >= 0) next = cJSON_GetArrayItem(next, idx);
    return next ? jpath(next, rest) : NULL;
}

/** Valeur d'un etat ; false si absente ou illisible. */
static bool status_value(const mstatus_t *s, const cJSON *v, int32_t *out)
{
    double d;
    if (cJSON_IsBool(v)) d = cJSON_IsTrue(v) ? 1 : 0;
    else if (cJSON_IsNumber(v)) d = v->valuedouble;
    else if (cJSON_IsString(v) && v->valuestring) {
        for (int i = 0; i < s->nmap; i++) {
            if (!strcasecmp(v->valuestring, s->mtxt[i])) { *out = s->mval[i]; return true; }
        }
        char *end;
        d = strtod(v->valuestring, &end);
        if (end == v->valuestring) return false;
    } else return false;
    if (s->is_bool) { *out = d != 0 ? 1 : 0; return true; }
    double x = d * s->scale;
    if (!isfinite(x) || x > 2147483647.0 || x < -2147483648.0) return false;
    *out = (int32_t)lround(x);
    return true;
}

// ----------------------------------------------------------------------------
// HTTP
// ----------------------------------------------------------------------------

typedef struct { char *buf; size_t cap, len; bool overflow; } body_t;

static esp_err_t http_evt(esp_http_client_event_t *e)
{
    body_t *b = (body_t *)e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && b && esp_http_client_get_status_code(e->client) == 200) {
        if (b->len + e->data_len >= b->cap) { b->overflow = true; return ESP_OK; }
        memcpy(b->buf + b->len, e->data, e->data_len);
        b->len += e->data_len;
        b->buf[b->len] = '\0';
    }
    return ESP_OK;
}

static void url_encode(const char *in, char *out, size_t len)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in && o + 4 < len; in++) {
        unsigned char c = (unsigned char)*in;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[o++] = (char)c;
        else { out[o++] = '%'; out[o++] = HEX[c >> 4]; out[o++] = HEX[c & 15]; }
    }
    out[o] = '\0';
}

/**
 * GET http://ip:port/path ; corps (200 seulement) dans buf. *status : code HTTP
 * (401 : identifiants), 0 si l'appareil ne repond pas.
 */
static esp_err_t http_get(const char *ip, uint16_t port, const char *path, auth_t auth,
                          const char *user, const char *pass, char *buf, size_t cap, int *status)
{
    char url[256];
    int n = snprintf(url, sizeof(url), "http://%s:%u%s", ip, (unsigned)(port ? port : 80), path);
    if (auth == AUTH_TASMOTA && user && user[0] && n > 0 && n < (int)sizeof(url)) {
        char u[64], p[96];
        url_encode(user, u, sizeof(u));
        url_encode(pass ? pass : "", p, sizeof(p));
        n += snprintf(url + n, sizeof(url) - n, "%suser=%s&password=%s", strchr(path, '?') ? "&" : "?", u, p);
    }
    if (n <= 0 || n >= (int)sizeof(url)) return ESP_ERR_INVALID_SIZE;
    body_t b = { buf, cap, 0, false };
    if (buf && cap) buf[0] = '\0';
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = HTTP_TIMEOUT;
    cfg.disable_auto_redirect = true;
    cfg.event_handler = http_evt;
    cfg.user_data = &b;
    cfg.buffer_size = 1024;
    cfg.keep_alive_enable = false;
    if ((auth == AUTH_BASIC || auth == AUTH_DIGEST) && user && user[0]) {
        cfg.username = user;
        cfg.password = pass ? pass : "";
        // Basic : envoye d'emblee ; Digest : sur le defi 401 (Shelly Gen2, SHA-256)
        cfg.auth_type = auth == AUTH_BASIC ? HTTP_AUTH_TYPE_BASIC : HTTP_AUTH_TYPE_NONE;
    }
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_perform(c);
    int st = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    if (status) *status = err == ESP_OK ? st : 0;
    if (err != ESP_OK) return err;
    if (st != 200) return ESP_FAIL;
    if (b.overflow) return ESP_ERR_INVALID_SIZE;
    return ESP_OK;
}

static char *resp_buf(void)
{
    char *b = (char *)heap_caps_malloc(RESP_MAX, MALLOC_CAP_SPIRAM);
    return b ? b : (char *)malloc(RESP_MAX);
}

/** Message d'erreur lisible d'un GET. */
static void http_error_text(esp_err_t err, int status, char *out, size_t len)
{
    if (status == 401) strlcpy(out, "identifiants refus\u00e9s (401)", len);
    else if (status) snprintf(out, len, "r\u00e9ponse HTTP %d", status);
    else if (err == ESP_ERR_INVALID_SIZE) strlcpy(out, "r\u00e9ponse trop longue", len);
    else strlcpy(out, "appareil injoignable", len);
}

// ----------------------------------------------------------------------------
// Fiche device_data
// ----------------------------------------------------------------------------

/** MAC (12 chiffres hexa, separateurs ignores) -> EUI-64 AA:BB:CC:FF:FE:DD:EE:FF. */
static uint64_t mac_to_eui64(const char *mac)
{
    uint8_t b[6];
    int n = 0;
    for (const char *p = mac; *p && n < 12; p++) {
        if (!isxdigit((unsigned char)*p)) continue;
        int v = isdigit((unsigned char)*p) ? *p - '0' : (toupper((unsigned char)*p) - 'A' + 10);
        if (n % 2 == 0) b[n / 2] = (uint8_t)(v << 4);
        else b[n / 2] |= (uint8_t)v;
        n++;
    }
    if (n != 12) return 0;
    return ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) | ((uint64_t)b[2] << 40) |
           (0xFFULL << 32) | (0xFEULL << 24) | ((uint64_t)b[3] << 16) | ((uint64_t)b[4] << 8) | b[5];
}

static void set_basic_string(uint64_t ieee, uint16_t attr, const char *s)
{
    char hex[2 * 32 + 1];
    size_t n = 0;
    for (; *s && n + 2 < sizeof(hex); s++) n += snprintf(hex + n, sizeof(hex) - n, "%02X", (unsigned char)*s);
    hex[n] = '\0';
    device_data_set_value_typed(ieee, 0x0000, attr, hex, 0x42);
}

/** Cree/complete la fiche et charge le template du modele. */
static void ensure_device_data(const ipdev_entry_t *d, const char *alias)
{
    device_data_t *dd = device_data_get_or_create(d->ieee);
    if (!dd) { ESP_LOGE(TAG, "Fiche impossible pour %016llX", (unsigned long long)d->ieee); return; }
    const model_t *m = find_model(d->model);
    set_basic_string(d->ieee, 0x0004, m && m->vendor[0] ? m->vendor : "IP");
    set_basic_string(d->ieee, 0x0005, m ? m->label : d->model);
    if (alias && alias[0]) strlcpy(dd->info.alias, alias, sizeof(dd->info.alias));
    if (device_data_load_template(d->ieee, IPDEV_TEMPLATE_FILE, d->model) != ESP_OK)
        ESP_LOGW(TAG, "Template %s / %s non charge", IPDEV_TEMPLATE_FILE, d->model);
    dd->dirty = true;
}

// ----------------------------------------------------------------------------
// Liste (db/ip_devices.json)
// ----------------------------------------------------------------------------

static ipdev_entry_t *find_dev(uint64_t ieee)
{
    for (int i = 0; i < IPDEV_MAX; i++) if (s_dev[i].used && s_dev[i].ieee == ieee) return &s_dev[i];
    return NULL;
}

static void save_list(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", s_enabled);
    cJSON *arr = cJSON_AddArrayToObject(root, "devices");
    lock();
    for (int i = 0; i < IPDEV_MAX; i++) {
        const ipdev_entry_t *d = &s_dev[i];
        if (!d->used) continue;
        char id[20];
        snprintf(id, sizeof(id), "%016llX", (unsigned long long)d->ieee);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", id);
        cJSON_AddStringToObject(o, "ip", d->ip);
        cJSON_AddNumberToObject(o, "port", d->port);
        cJSON_AddStringToObject(o, "model", d->model);
        cJSON_AddStringToObject(o, "user", d->user);
        cJSON_AddStringToObject(o, "pass", d->pass);      // masque par l'API fichiers ("pass*")
        cJSON_AddNumberToObject(o, "interval", d->interval);
        cJSON_AddStringToObject(o, "key", d->key);        // masque aussi ("key")
        cJSON_AddNumberToObject(o, "ep", d->matter_ep);
        cJSON_AddItemToArray(arr, o);
    }
    unlock();
    char *js = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!js) return;
    FILE *f = fopen(LIST_FILE ".tmp", "w");
    if (f) {
        bool ok = fputs(js, f) >= 0;
        ok = (fclose(f) == 0) && ok;
        if (ok) { remove(LIST_FILE); rename(LIST_FILE ".tmp", LIST_FILE); }
    }
    free(js);
}

static void load_list(void)
{
    char *txt = read_file(LIST_FILE, 32 * 1024);
    if (!txt) return;
    cJSON *root = cJSON_Parse(txt);
    free(txt);
    if (!root) { ESP_LOGE(TAG, "%s illisible", LIST_FILE); return; }
    s_enabled = cJSON_IsTrue(cJSON_GetObjectItem(root, "enabled"));
    int k = 0;
    const cJSON *o;
    cJSON_ArrayForEach(o, cJSON_GetObjectItem(root, "devices")) {
        if (k >= IPDEV_MAX) break;
        char id[20] = "";
        jstr(o, "id", id, sizeof(id));
        uint64_t ieee = strtoull(id, NULL, 16);
        if (!ieee) continue;
        ipdev_entry_t *d = &s_dev[k++];
        memset(d, 0, sizeof(*d));
        d->used = true;
        d->ieee = ieee;
        jstr(o, "ip", d->ip, sizeof(d->ip));
        d->port = (uint16_t)jnum(o, "port", 80);
        jstr(o, "model", d->model, sizeof(d->model));
        jstr(o, "user", d->user, sizeof(d->user));
        jstr(o, "pass", d->pass, sizeof(d->pass));
        d->interval = (uint16_t)jnum(o, "interval", 15);
        jstr(o, "key", d->key, sizeof(d->key));
        d->matter_ep = (uint16_t)jnum(o, "ep", 0);
        d->next_us = now_us() + (int64_t)(2 + k) * 1000000;   // premiers releves echelonnes
    }
    cJSON_Delete(root);
}

// ----------------------------------------------------------------------------
// Releve et commandes (tache "ipdev")
// ----------------------------------------------------------------------------

static void emit(uint64_t ieee, const mstatus_t *s, int32_t v)
{
    if (!s_cb) return;
    ipdev_report_t r = {};
    r.ieee = ieee;
    r.cluster = s->cluster;
    r.attr = s->attr;
    if (s->is_bool) {
        r.type = 0x10;
        r.len = 1;
        r.data[0] = v ? 1 : 0;
    } else {
        r.type = 0x2B;
        r.len = 4;
        r.data[0] = (uint8_t)((uint32_t)v >> 24);
        r.data[1] = (uint8_t)((uint32_t)v >> 16);
        r.data[2] = (uint8_t)((uint32_t)v >> 8);
        r.data[3] = (uint8_t)v;
    }
    s_cb(&r);
}

/** Copie des parametres d'un appareil (la requete HTTP se fait sans verrou). */
typedef struct { uint64_t ieee; char ip[40]; uint16_t port; char model[32]; char user[32]; char pass[64]; } dev_ref_t;

static bool take_ref(uint64_t ieee, dev_ref_t *r)
{
    lock();
    ipdev_entry_t *d = find_dev(ieee);
    if (d) {
        r->ieee = d->ieee;
        strlcpy(r->ip, d->ip, sizeof(r->ip));
        r->port = d->port;
        strlcpy(r->model, d->model, sizeof(r->model));
        strlcpy(r->user, d->user, sizeof(r->user));
        strlcpy(r->pass, d->pass, sizeof(r->pass));
    }
    unlock();
    return d != NULL;
}

static void poll_device(uint64_t ieee)
{
    dev_ref_t r;
    if (!take_ref(ieee, &r)) return;
    const model_t *m = find_model(r.model);
    char err_txt[48] = "";
    bool ok = m && m->npoll > 0;
    if (!m) strlcpy(err_txt, "mod\u00e8le inconnu", sizeof(err_txt));
    // Valeurs relevees, comparees et emises apres coup (sans verrou pendant HTTP)
    int32_t vals[ST_MAX];
    bool got[ST_MAX] = {};
    char *buf = ok ? resp_buf() : NULL;
    if (ok && !buf) { ok = false; strlcpy(err_txt, "m\u00e9moire", sizeof(err_txt)); }
    for (int p = 0; ok && p < m->npoll; p++) {
        int status = 0;
        esp_err_t e = http_get(r.ip, r.port, m->poll[p], m->auth, r.user, r.pass, buf, RESP_MAX, &status);
        if (e != ESP_OK) { ok = false; http_error_text(e, status, err_txt, sizeof(err_txt)); break; }
        cJSON *root = cJSON_Parse(buf);
        if (!root) { ok = false; strlcpy(err_txt, "r\u00e9ponse non JSON", sizeof(err_txt)); break; }
        for (int s = 0; s < m->nst; s++) {
            if (got[s] || (m->st[s].src >= 0 && m->st[s].src != p)) continue;
            const cJSON *v = jpath(root, m->st[s].path);
            if (v && status_value(&m->st[s], v, &vals[s])) got[s] = true;
        }
        cJSON_Delete(root);
    }
    free(buf);

    int64_t t = now_us();
    bool send[ST_MAX] = {};
    lock();
    ipdev_entry_t *d = find_dev(ieee);
    if (d) {
        d->polls++;
        if (ok) {
            d->online = true;
            d->fails = 0;
            d->last_ok_us = t;
            d->last_error[0] = '\0';
            for (int s = 0; m && s < m->nst; s++) {
                if (!got[s]) continue;
                if (!d->has_val[s] || d->last_val[s] != vals[s] || t - d->sent_us[s] > (int64_t)RESEND_S * 1000000) {
                    d->has_val[s] = true;
                    d->last_val[s] = vals[s];
                    d->sent_us[s] = t;
                    send[s] = true;
                }
            }
            d->next_us = t + (int64_t)d->interval * 1000000;
        } else {
            d->errors++;
            if (d->fails < 255) d->fails++;
            if (d->fails >= 3) d->online = false;
            strlcpy(d->last_error, err_txt, sizeof(d->last_error));
            // Attente doublee apres 3 echecs, 5 min au plus
            int64_t wait = d->interval;
            for (int k = 3; k <= d->fails && wait < BACKOFF_MAX_S; k++) wait *= 2;
            if (wait > BACKOFF_MAX_S) wait = BACKOFF_MAX_S;
            d->next_us = t + wait * 1000000;
        }
    }
    unlock();
    if (!ok) ESP_LOGD(TAG, "%016llX : %s", (unsigned long long)ieee, err_txt);
    for (int s = 0; m && s < m->nst; s++) if (send[s]) emit(ieee, &m->st[s], vals[s]);
}

static void run_action(const job_t *j)
{
    dev_ref_t r;
    if (!take_ref(j->ieee, &r)) return;
    const model_t *m = find_model(r.model);
    const maction_t *a = NULL;
    for (int i = 0; m && i < m->nact; i++) {
        const maction_t *x = &m->act[i];
        if (x->command != j->command || x->value != j->value) continue;
        if (j->endpoint && x->endpoint && x->endpoint != j->endpoint) continue;
        a = x;
        break;
    }
    if (!a) {
        ESP_LOGW(TAG, "%016llX : pas d'action pour la commande %d valeur %ld", (unsigned long long)j->ieee,
                 j->command, (long)j->value);
        return;
    }
    int status = 0;
    esp_err_t e = http_get(r.ip, r.port, a->url, m->auth, r.user, r.pass, NULL, 0, &status);
    ESP_LOGI(TAG, "%016llX : %s -> %s", (unsigned long long)j->ieee, a->url,
             e == ESP_OK ? "ok" : (status ? "refus" : "injoignable"));
    // Etat relu aussitot
    poll_device(j->ieee);
}

static void ipdev_task(void *arg)
{
    for (;;) {
        // Prochain releve du
        int64_t t = now_us(), next = t + 1000000;
        uint64_t due = 0;
        lock();
        if (s_enabled) {
            for (int i = 0; i < IPDEV_MAX; i++) {
                if (!s_dev[i].used) continue;
                if (s_dev[i].next_us <= t && !due) due = s_dev[i].ieee;
                if (s_dev[i].next_us < next) next = s_dev[i].next_us;
            }
        }
        unlock();
        job_t j;
        TickType_t wait = due ? 0 : pdMS_TO_TICKS((next - t) / 1000 > 0 ? (next - t) / 1000 : 10);
        if (wait > pdMS_TO_TICKS(1000)) wait = pdMS_TO_TICKS(1000);
        if (xQueueReceive(s_q, &j, wait) == pdTRUE) {
            if (!s_enabled) continue;
            if (j.kind == JOB_ACTION) run_action(&j);
            else poll_device(j.ieee);
            continue;
        }
        if (due) poll_device(due);
    }
}

static void start_task(void)
{
    if (s_task) return;
    if (!s_q) s_q = xQueueCreate(16, sizeof(job_t));
    // Pile interne : la tache ecrit la flash (fiche, liste) et fait du reseau
    if (xTaskCreate(ipdev_task, "ipdev", 6144, NULL, 3, &s_task) != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "Tache ipdev impossible");
    }
}

// ----------------------------------------------------------------------------
// API
// ----------------------------------------------------------------------------

esp_err_t ip_devices_init(void)
{
    if (!s_mx) s_mx = xSemaphoreCreateRecursiveMutex();
    if (!s_dev) {
        s_dev = (ipdev_entry_t *)heap_caps_calloc(IPDEV_MAX, sizeof(ipdev_entry_t), MALLOC_CAP_SPIRAM);
        if (!s_dev) s_dev = (ipdev_entry_t *)calloc(IPDEV_MAX, sizeof(ipdev_entry_t));
        if (!s_dev) return ESP_ERR_NO_MEM;
    }
    load_models();
    load_list();
    for (int i = 0; i < IPDEV_MAX; i++) if (s_dev[i].used) ensure_device_data(&s_dev[i], NULL);
    if (!s_q) s_q = xQueueCreate(16, sizeof(job_t));
    if (s_enabled) start_task();
    ESP_LOGI(TAG, "%d appareil(s) IP, module %s", ip_devices_count(), s_enabled ? "actif" : "inactif");
    return ESP_OK;
}

void ip_devices_register_report_cb(ipdev_report_cb_t cb) { s_cb = cb; }

bool ip_devices_enabled(void) { return s_enabled; }

esp_err_t ip_devices_set_enabled(bool on)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    s_enabled = on;
    save_list();
    if (on) start_task();
    return ESP_OK;
}

bool ip_devices_is_ip(uint64_t ieee)
{
    if (!s_dev || !ieee) return false;
    lock();
    bool r = find_dev(ieee) != NULL;
    unlock();
    return r;
}

int ip_devices_count(void)
{
    if (!s_dev) return 0;
    int n = 0;
    lock();
    for (int i = 0; i < IPDEV_MAX; i++) if (s_dev[i].used) n++;
    unlock();
    return n;
}

static void fill_info(const ipdev_entry_t *d, ipdev_info_t *out)
{
    memset(out, 0, sizeof(*out));
    out->ieee = d->ieee;
    strlcpy(out->ip, d->ip, sizeof(out->ip));
    out->port = d->port;
    strlcpy(out->model, d->model, sizeof(out->model));
    strlcpy(out->user, d->user, sizeof(out->user));
    out->has_pass = d->pass[0] != '\0';
    out->interval_s = d->interval;
    out->online = d->online;
    out->age_s = d->last_ok_us ? (int32_t)((now_us() - d->last_ok_us) / 1000000) : -1;
    out->polls = d->polls;
    out->errors = d->errors;
    strlcpy(out->last_error, d->last_error, sizeof(out->last_error));
    out->matter_ep = d->matter_ep;
}

bool ip_devices_get(int index, ipdev_info_t *out)
{
    if (!s_dev) return false;
    lock();
    int n = 0;
    for (int i = 0; i < IPDEV_MAX; i++) {
        if (!s_dev[i].used) continue;
        if (n++ == index) { fill_info(&s_dev[i], out); unlock(); return true; }
    }
    unlock();
    return false;
}

bool ip_devices_find(uint64_t ieee, ipdev_info_t *out)
{
    if (!s_dev) return false;
    lock();
    ipdev_entry_t *d = find_dev(ieee);
    if (d && out) fill_info(d, out);
    unlock();
    return d != NULL;
}

esp_err_t ip_devices_send_action(uint64_t ieee, int command, uint8_t endpoint, int32_t value)
{
    if (!s_enabled || !s_q) return ESP_ERR_INVALID_STATE;
    if (!ip_devices_is_ip(ieee)) return ESP_ERR_NOT_FOUND;
    job_t j = { JOB_ACTION, ieee, command, endpoint, value };
    return xQueueSend(s_q, &j, pdMS_TO_TICKS(100)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void ip_devices_poll_now(uint64_t ieee)
{
    if (!s_enabled || !s_q) return;
    job_t j = { JOB_POLL, ieee, 0, 0, 0 };
    xQueueSend(s_q, &j, 0);
}

char *ip_devices_list_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", s_enabled);
    cJSON *arr = cJSON_AddArrayToObject(root, "devices");
    int n = ip_devices_count();
    for (int i = 0; i < n; i++) {
        ipdev_info_t in;
        if (!ip_devices_get(i, &in)) continue;
        char id[20];
        snprintf(id, sizeof(id), "%016llX", (unsigned long long)in.ieee);
        const model_t *m = find_model(in.model);
        const device_data_t *dd = device_data_find(in.ieee);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", id);
        cJSON_AddStringToObject(o, "alias", dd ? dd->info.alias : "");
        cJSON_AddStringToObject(o, "ip", in.ip);
        cJSON_AddNumberToObject(o, "port", in.port);
        cJSON_AddStringToObject(o, "model", in.model);
        cJSON_AddStringToObject(o, "label", m ? m->label : in.model);
        cJSON_AddStringToObject(o, "user", in.user);
        cJSON_AddBoolToObject(o, "hasPass", in.has_pass);
        cJSON_AddNumberToObject(o, "interval", in.interval_s);
        cJSON_AddBoolToObject(o, "online", in.online);
        cJSON_AddNumberToObject(o, "age", in.age_s);
        cJSON_AddNumberToObject(o, "polls", in.polls);
        cJSON_AddNumberToObject(o, "errors", in.errors);
        cJSON_AddStringToObject(o, "lastError", in.last_error);
        cJSON_AddItemToArray(arr, o);
    }
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

char *ip_devices_models_json(void)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < s_nmodels; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "key", s_models[i].key);
        cJSON_AddStringToObject(o, "label", s_models[i].label);
        cJSON_AddItemToArray(arr, o);
    }
    char *s = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return s;
}

// ----------------------------------------------------------------------------
// Detection
// ----------------------------------------------------------------------------

#define PROBE_CACHE 8
typedef struct { char url[URL_LEN]; auth_t auth; cJSON *doc; int status; bool done; } probe_entry_t;

typedef struct {
    const char *ip; uint16_t port; const char *user; const char *pass;
    probe_entry_t e[PROBE_CACHE];
    int n;
    char *buf;
    bool unreachable, auth_needed;
} probe_t;

static const cJSON *probe_get(probe_t *p, const char *url, auth_t auth)
{
    for (int i = 0; i < p->n; i++) if (!strcmp(p->e[i].url, url) && p->e[i].auth == auth) return p->e[i].doc;
    if (p->unreachable || p->n >= PROBE_CACHE) return NULL;
    probe_entry_t *e = &p->e[p->n++];
    strlcpy(e->url, url, sizeof(e->url));
    e->auth = auth;
    esp_err_t err = http_get(p->ip, p->port, url, auth, p->user, p->pass, p->buf, RESP_MAX, &e->status);
    e->done = true;
    if (err != ESP_OK) {
        if (e->status == 401) p->auth_needed = true;
        else if (!e->status) p->unreachable = true;     // plus rien a essayer
        return NULL;
    }
    e->doc = cJSON_Parse(p->buf);
    return e->doc;
}

/** Modele detecte (premier du fichier dont toutes les conditions sont vraies). */
static const model_t *probe_model(probe_t *p)
{
    for (int i = 0; i < s_nmodels; i++) {
        const model_t *m = &s_models[i];
        if (!m->ndet) continue;
        bool all = true;
        for (int k = 0; k < m->ndet && all; k++) {
            const cJSON *doc = probe_get(p, m->det[k].url, m->auth);
            const cJSON *v = doc ? jpath(doc, m->det[k].path) : NULL;
            all = v && (!m->det[k].has_gt || (cJSON_IsNumber(v) && v->valuedouble > m->det[k].gt));
        }
        if (all) return m;
        if (p->unreachable) return NULL;
    }
    return NULL;
}

static void probe_text(probe_t *p, const urlpath_t *up, auth_t auth, char *out, size_t len)
{
    out[0] = '\0';
    if (!up->url[0]) return;
    const cJSON *v = jpath(probe_get(p, up->url, auth), up->path);
    if (cJSON_IsString(v) && v->valuestring) strlcpy(out, v->valuestring, len);
}

static void probe_free(probe_t *p)
{
    for (int i = 0; i < p->n; i++) if (p->e[i].doc) cJSON_Delete(p->e[i].doc);
    free(p->buf);
}

/** Detection : modele, MAC, nom. false si l'appareil n'est pas reconnu. */
static bool probe(const char *ip, uint16_t port, const char *user, const char *pass,
                  const model_t **m_out, char *mac, size_t mac_len, char *name, size_t name_len,
                  char *err, size_t err_len)
{
    probe_t p = {};
    p.ip = ip; p.port = port; p.user = user; p.pass = pass;
    p.buf = resp_buf();
    if (!p.buf) { strlcpy(err, "m\u00e9moire", err_len); return false; }
    const model_t *m = probe_model(&p);
    if (m) {
        probe_text(&p, &m->mac, m->auth, mac, mac_len);
        probe_text(&p, &m->name, m->auth, name, name_len);
    } else if (p.unreachable) {
        strlcpy(err, "Appareil injoignable \u00e0 cette adresse", err_len);
    } else if (p.auth_needed) {
        strlcpy(err, "Identifiants requis ou refus\u00e9s", err_len);
    } else {
        strlcpy(err, "Mod\u00e8le non reconnu : choisissez-le dans la liste", err_len);
    }
    probe_free(&p);
    *m_out = m;
    return m != NULL;
}

/** Adresse privee (RFC 1918, lien local) ou nom simple du reseau local. */
static bool local_address(const char *ip)
{
    struct in_addr a;
    if (!inet_aton(ip, &a)) return strchr(ip, '.') == NULL || strstr(ip, ".local") != NULL;
    uint32_t h = ntohl(a.s_addr);
    return (h >> 24) == 10 || (h >> 20) == 0xAC1 || (h >> 16) == 0xC0A8 || (h >> 16) == 0xA9FE;
}

char *ip_devices_probe_json(const char *ip, uint16_t port, const char *user, const char *pass)
{
    cJSON *root = cJSON_CreateObject();
    char err[96] = "", mac[24] = "", name[48] = "";
    const model_t *m = NULL;
    if (!ip || !ip[0] || !local_address(ip)) {
        strlcpy(err, "Adresse du r\u00e9seau local attendue", sizeof(err));
    } else if (probe(ip, port, user, pass, &m, mac, sizeof(mac), name, sizeof(name), err, sizeof(err))) {
        cJSON_AddStringToObject(root, "model", m->key);
        cJSON_AddStringToObject(root, "label", m->label);
        cJSON_AddStringToObject(root, "mac", mac);
        cJSON_AddStringToObject(root, "name", name);
        uint64_t ieee = mac_to_eui64(mac);
        if (ieee) {
            char id[20];
            snprintf(id, sizeof(id), "%016llX", (unsigned long long)ieee);
            cJSON_AddStringToObject(root, "id", id);
            cJSON_AddBoolToObject(root, "known", ip_devices_is_ip(ieee));
        }
    }
    cJSON_AddBoolToObject(root, "ok", m != NULL);
    if (err[0]) cJSON_AddStringToObject(root, "error", err);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

esp_err_t ip_devices_add(const char *ip, uint16_t port, const char *model, const char *user,
                         const char *pass, int interval_s, const char *alias,
                         uint64_t *ieee_out, char *err, size_t err_len)
{
    if (!s_dev) { strlcpy(err, "module non initialis\u00e9", err_len); return ESP_ERR_INVALID_STATE; }
    if (!ip || !ip[0] || strlen(ip) >= sizeof(s_dev[0].ip) || !local_address(ip)) {
        strlcpy(err, "Adresse du r\u00e9seau local attendue", err_len);
        return ESP_ERR_INVALID_ARG;
    }
    if (interval_s < 5 || interval_s > 600) interval_s = 15;
    // Detection : modele et MAC (la MAC fait la cle de l'appareil)
    const model_t *m = NULL;
    char mac[24] = "", name[48] = "";
    if (!probe(ip, port, user, pass, &m, mac, sizeof(mac), name, sizeof(name), err, err_len)) {
        return ESP_FAIL;
    }
    if (model && model[0]) {
        const model_t *forced = find_model(model);
        if (!forced) { strlcpy(err, "Mod\u00e8le inconnu", err_len); return ESP_ERR_INVALID_ARG; }
        m = forced;
    }
    uint64_t ieee = mac_to_eui64(mac);
    if (!ieee) { strlcpy(err, "Adresse MAC de l'appareil introuvable", err_len); return ESP_FAIL; }

    lock();
    if (find_dev(ieee)) { unlock(); strlcpy(err, "Appareil d\u00e9j\u00e0 ajout\u00e9", err_len); return ESP_ERR_INVALID_STATE; }
    ipdev_entry_t *d = NULL;
    for (int i = 0; i < IPDEV_MAX && !d; i++) if (!s_dev[i].used) d = &s_dev[i];
    if (!d) { unlock(); snprintf(err, err_len, "%d appareils IP au plus", IPDEV_MAX); return ESP_ERR_NO_MEM; }
    memset(d, 0, sizeof(*d));
    d->used = true;
    d->ieee = ieee;
    strlcpy(d->ip, ip, sizeof(d->ip));
    d->port = port ? port : 80;
    strlcpy(d->model, m->key, sizeof(d->model));
    strlcpy(d->user, user ? user : "", sizeof(d->user));
    strlcpy(d->pass, pass ? pass : "", sizeof(d->pass));
    d->interval = (uint16_t)interval_s;
    uint8_t rnd[16];
    esp_fill_random(rnd, sizeof(rnd));
    for (int i = 0; i < 16; i++) snprintf(d->key + 2 * i, 3, "%02x", rnd[i]);
    d->next_us = now_us();
    ipdev_entry_t copy = *d;
    unlock();

    ensure_device_data(&copy, (alias && alias[0]) ? alias : name);
    device_data_save(ieee);
    save_list();
    ip_devices_poll_now(ieee);
    if (ieee_out) *ieee_out = ieee;
    ESP_LOGI(TAG, "Appareil IP ajoute : %016llX %s (%s)", (unsigned long long)ieee, ip, m->key);
    return ESP_OK;
}

esp_err_t ip_devices_update(uint64_t ieee, const char *ip, uint16_t port, const char *model,
                            const char *user, const char *pass, int interval_s,
                            char *err, size_t err_len)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    if (ip && ip[0] && (strlen(ip) >= sizeof(s_dev[0].ip) || !local_address(ip))) {
        strlcpy(err, "Adresse du r\u00e9seau local attendue", err_len);
        return ESP_ERR_INVALID_ARG;
    }
    if (model && model[0] && !find_model(model)) { strlcpy(err, "Mod\u00e8le inconnu", err_len); return ESP_ERR_INVALID_ARG; }
    lock();
    ipdev_entry_t *d = find_dev(ieee);
    if (!d) { unlock(); strlcpy(err, "Appareil inconnu", err_len); return ESP_ERR_NOT_FOUND; }
    bool model_changed = model && model[0] && strcmp(model, d->model) != 0;
    if (ip && ip[0]) strlcpy(d->ip, ip, sizeof(d->ip));
    if (port) d->port = port;
    if (model && model[0]) strlcpy(d->model, model, sizeof(d->model));
    if (user) strlcpy(d->user, user, sizeof(d->user));
    if (pass) strlcpy(d->pass, pass, sizeof(d->pass));
    if (interval_s >= 5 && interval_s <= 600) d->interval = (uint16_t)interval_s;
    d->fails = 0;
    d->next_us = now_us();
    if (model_changed) memset(d->has_val, 0, sizeof(d->has_val));
    ipdev_entry_t copy = *d;
    unlock();
    if (model_changed) { ensure_device_data(&copy, NULL); device_data_save(ieee); }
    save_list();
    ip_devices_poll_now(ieee);
    return ESP_OK;
}

esp_err_t ip_devices_remove(uint64_t ieee, bool remove_data)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    lock();
    ipdev_entry_t *d = find_dev(ieee);
    if (d) memset(d, 0, sizeof(*d));
    unlock();
    if (!d) return ESP_ERR_NOT_FOUND;
    save_list();
    if (remove_data) device_data_remove(ieee);
    ESP_LOGI(TAG, "Appareil IP supprime : %016llX", (unsigned long long)ieee);
    return ESP_OK;
}

esp_err_t ip_devices_set_matter_endpoint(uint64_t ieee, uint16_t ep)
{
    lock();
    ipdev_entry_t *d = find_dev(ieee);
    if (d) d->matter_ep = ep;
    unlock();
    if (!d) return ESP_ERR_NOT_FOUND;
    save_list();
    return ESP_OK;
}

#endif  // CONFIG_LIXEE_IP_DEVICES
