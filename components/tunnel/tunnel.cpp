// ============================================================================
// Tunnel d'acces distant - portage de LiXeeBoxTunnel (tunnel.h LiXee-Box)
// Protocole et contrat : voir include/tunnel.h.
//
// Organisation (differente de la LiXee-Box, qui faisait tout dans loop()) :
//  - tache "tunnel_ws" : SEULE proprietaire du transport WebSocket/TLS. Elle
//    lit les trames, repond aux pings, emet les messages de la file d'emission
//    par fragments de 8 Ko, envoie heartbeat et pings, gere la reconnexion.
//    Comme elle est la seule a emettre, aucune trame de donnees ne peut
//    s'intercaler au milieu d'un message fragmente (le defaut corrige par
//    ChunkedWsClient::isFragmenting sur la LiXee-Box ne peut pas se produire).
//  - WORKER_COUNT taches "tunnel_wN" : rejouent chaque requete sur
//    127.0.0.1:80 (socket brut), lisent la reponse (Content-Length, chunked ou
//    fermeture), la mettent en forme (JSON + base64) et la deposent dans la
//    file d'emission. Elles executent aussi l'activation par code (HTTPS).
//  - Les piles de ces taches sont en PSRAM : elles n'accedent JAMAIS a la flash
//    (pas de fichier, pas de NVS). Les enregistrements de tunnel.json sont faits
//    par l'appelant (handler HTTP, pile interne) ou, apres une activation, par
//    une tache ephemere a pile interne (save_task).
// ============================================================================

#include "tunnel.h"
#include "config_manager.h"

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_app_desc.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "mbedtls/base64.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "lwip/sockets.h"

#include <atomic>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static const char *TAG = "TUNNEL";

// ----------------------------------------------------------------------------
// Relais LiXee : valeurs IDENTIQUES a la LiXee-Box, pour que le service
// existant reconnaisse le pont sans modification cote serveur.
// ----------------------------------------------------------------------------
#define TUNNEL_HOST          "remote.lixee-box.fr"
#define TUNNEL_PORT          443
#define TUNNEL_WS_PATH       "/tunnel"
#define TUNNEL_SUBPROTO      "lixeebox"                    // beginSslWithCA(..., "lixeebox")
#define TUNNEL_USER_AGENT    "arduino-WebSocket-Client"    // en-tete envoye par WebSocketsClient
#define TUNNEL_EXTRA_HDRS    "Origin: file://\r\n"          // extraHeaders par defaut de WebSocketsClient
#define TUNNEL_ACTIVATE_URL  "https://" TUNNEL_HOST "/api/activate?code="
#define TUNNEL_FW_NAME       "LiXeeBoxTunnel/1.0"
#define LOCAL_HTTP_PORT      80

#ifdef CONFIG_HTTPD_MAX_URI_LEN
#define LOCAL_MAX_URI        CONFIG_HTTPD_MAX_URI_LEN
#else
#define LOCAL_MAX_URI        512
#endif

#if CONFIG_IDF_TARGET_ESP32S3
#define CHIP_NAME            "ESP32-S3"                    // ESP.getChipModel() de la LiXee-Box
#else
#define CHIP_NAME            CONFIG_IDF_TARGET
#endif

// ----------------------------------------------------------------------------
// Dimensionnement
// ----------------------------------------------------------------------------
// 2 workers au lieu des 6 "slots" de la LiXee-Box : chaque requete relayee
// occupe une socket cliente ET une session du serveur HTTP local, et
// CONFIG_LWIP_MAX_SOCKETS vaut 10 sur ce projet. Les requetes en surplus
// attendent dans la file (16 au plus, 8 s au plus), comme sur la LiXee-Box.
static constexpr int         WORKER_COUNT          = 2;
// Pile de la tache WebSocket : poignee TLS (verification de chaine RSA/ECDSA
// avec le bundle de certificats, ~6 Ko mesures habituellement) + cJSON.
static constexpr uint32_t    WS_TASK_STACK         = 10 * 1024;
// Pile des workers : socket local + mise en forme ; l'activation HTTPS
// (esp_http_client + poignee TLS) est le cas le plus gourmand.
static constexpr uint32_t    WORKER_TASK_STACK     = 8 * 1024;
// Pile de la tache de sauvegarde (interne : ecriture LittleFS). Correction :
// 4096 octets ne suffisaient pas a la serialisation cJSON + cfg_write_file
// (fopen/fwrite/fsync LittleFS, journalisation) : marge portee a 6 Ko.
static constexpr uint32_t    SAVE_TASK_STACK       = 6144;
static constexpr UBaseType_t WS_TASK_PRIO          = 5;
static constexpr UBaseType_t WORKER_TASK_PRIO      = 4;

// Messages entrants : la LiXee-Box etait bornee a 15 Ko (WEBSOCKETS_MAX_DATA_SIZE
// de la bibliotheque WebSockets), d'ou les envois par morceaux de ~8 Ko des
// pages (restauration, OTA, modeles). On accepte davantage, en PSRAM.
static constexpr size_t      MAX_IN_MSG            = 64 * 1024;
static constexpr size_t      RX_SPARE              = 256;    // place reservee aux trames de controle (<= 125 o)
// Corps de reponse : le serveur statique sert jusqu'a 512 Ko.
static constexpr size_t      MAX_BODY              = 640 * 1024;
static constexpr size_t      WS_FRAGMENT           = 8192;   // sendTXTChunked(..., 8192) de la LiXee-Box
static constexpr size_t      RX_CHUNK              = 4096;
static constexpr int         JOB_QUEUE_LEN         = 16;     // MAX_PENDING de la LiXee-Box
static constexpr int         OUT_QUEUE_LEN         = 8;
static constexpr int64_t     PENDING_TIMEOUT_MS    = 8000;   // bien sous le delai du relais
static constexpr int64_t     LOCAL_IDLE_TIMEOUT_MS = 30000;  // SLOT_TIMEOUT_MS de la LiXee-Box
static constexpr int64_t     LOCAL_TOTAL_TIMEOUT_MS = 120000;
static constexpr int64_t     HEARTBEAT_MS          = 15000;
static constexpr int64_t     PING_INTERVAL_MS      = 15000;  // enableHeartbeat(15000, 10000, 3)
static constexpr int64_t     PONG_TIMEOUT_MS       = 10000;
static constexpr int         PONG_MAX_MISSES       = 3;
static constexpr uint32_t    RECONNECT_MIN_MS      = 3000;   // setReconnectInterval(3000)
static constexpr uint32_t    RECONNECT_MAX_MS      = 60000;
static constexpr int         CONNECT_TIMEOUT_MS    = 15000;
static constexpr int         IO_TIMEOUT_MS         = 10000;
// Plancher de RAM interne pour ouvrir une connexion locale de plus (equivalent
// du plancher 55 Ko de la LiXee-Box, adapte : lwIP et mbedTLS sont ici en PSRAM).
static constexpr size_t      MIN_INTERNAL_FREE     = 24 * 1024;
// Budget des en-tetes relayes : CONFIG_HTTPD_MAX_REQ_HDR_LEN vaut 1024 et le
// serveur local repondrait 431 au-dela.
static constexpr size_t      MAX_FWD_HEADERS       = 760;
static constexpr size_t      MAX_FWD_HEADER_VALUE  = 400;
static constexpr size_t      MAX_RESP_HEADERS      = 8192;
static constexpr size_t      SMALL_FALLBACK        = 4096;   // repli en RAM interne autorise sous ce seuil

// ============================================================================
// Outils memoire / temps / chaines
// ============================================================================

static void *tn_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // Repli en RAM interne seulement pour les petits blocs : un gros tampon
    // de reponse en interne mettrait le reste du firmware a genoux.
    if (!p && n <= SMALL_FALLBACK) p = malloc(n);
    return p;
}

static void *tn_realloc(void *old, size_t n)
{
    void *p = heap_caps_realloc(old, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p && n <= SMALL_FALLBACK) p = realloc(old, n);
    return p;
}

static char *tn_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)tn_alloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static inline int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/** snprintf sans avertissement de troncature : les messages sont informatifs. */
static void sfmt(char *dst, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void sfmt(char *dst, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst, n, fmt, ap);
    va_end(ap);
}

/** Tampon de texte extensible en PSRAM (toujours termine par '\0'). */
typedef struct {
    char  *p;
    size_t len;
    size_t cap;
    bool   oom;
} sbuf_t;

static bool sb_reserve(sbuf_t *b, size_t extra)
{
    if (b->oom) return false;
    size_t need = b->len + extra + 1;
    if (need <= b->cap) return true;
    size_t ncap = b->cap ? b->cap * 2 : 256;
    if (ncap < need) ncap = need;   // gros ajout (corps base64) : taille exacte, pas de doublement
    char *np = (char *)tn_realloc(b->p, ncap);
    if (!np) { b->oom = true; return false; }
    b->p = np;
    b->cap = ncap;
    return true;
}

static void sb_add(sbuf_t *b, const char *s, size_t n)
{
    if (!sb_reserve(b, n)) return;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

static void sb_adds(sbuf_t *b, const char *s)
{
    sb_add(b, s, strlen(s));
}

static void sb_addf(sbuf_t *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sb_addf(sbuf_t *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0 || !sb_reserve(b, (size_t)n)) return;
    va_start(ap, fmt);
    vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

/** Ajoute s[0..n) echappe pour une chaine JSON (sans les guillemets). */
static void sb_add_json(sbuf_t *b, const char *s, size_t n)
{
    size_t run = 0;   // copie par blocs des caracteres ordinaires
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        const char *esc = NULL;
        char u[8];
        switch (c) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\n': esc = "\\n";  break;
            case '\r': esc = "\\r";  break;
            case '\t': esc = "\\t";  break;
            case '\b': esc = "\\b";  break;
            case '\f': esc = "\\f";  break;
            default:
                if (c < 0x20) {
                    snprintf(u, sizeof(u), "\\u%04x", (unsigned)c);
                    esc = u;
                }
                break;
        }
        if (esc) {
            if (i > run) sb_add(b, s + run, i - run);
            sb_adds(b, esc);
            run = i + 1;
        }
    }
    if (n > run) sb_add(b, s + run, n - run);
}

/** Ajoute s encode pour une composante de query string (RFC 3986). */
static void sb_add_urlenc(sbuf_t *b, const char *s)
{
    static const char HEX[] = "0123456789ABCDEF";
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            sb_add(b, (const char *)&c, 1);
        } else {
            char e[3] = { '%', HEX[c >> 4], HEX[c & 0x0F] };
            sb_add(b, e, 3);
        }
    }
}

static void sb_free(sbuf_t *b)
{
    free(b->p);
    b->p = NULL;
    b->len = b->cap = 0;
}

static void sb_wipe_free(sbuf_t *b)
{
    if (b->p) memset(b->p, 0, b->len);
    sb_free(b);
}

/** Identifiant / jeton : ASCII imprimable, sans espace, longueur bornee. */
static bool valid_cred(const char *s, size_t max)
{
    size_t n = strlen(s);
    if (n >= max) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c <= 0x20 || c >= 0x7F) return false;
    }
    return true;
}

static bool json_truthy(const cJSON *it)
{
    if (cJSON_IsBool(it))   return cJSON_IsTrue(it);
    if (cJSON_IsNumber(it)) return it->valuedouble != 0;
    // config_write() de la LiXee-Box enregistrait "1" / "0" en CHAINE
    if (cJSON_IsString(it) && it->valuestring) {
        return strcmp(it->valuestring, "1") == 0 || strcasecmp(it->valuestring, "true") == 0 ||
               strcasecmp(it->valuestring, "on") == 0;
    }
    return false;
}

static bool contains_ci(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    for (; *hay; hay++) {
        if (strncasecmp(hay, needle, nl) == 0) return true;
    }
    return false;
}

// ============================================================================
// Etat partage (protege par s_mtx)
// ============================================================================

typedef struct {
    bool enable;
    char client_id[TUNNEL_CLIENT_ID_MAX];
    char token[TUNNEL_TOKEN_MAX];
} tn_cfg_t;

static SemaphoreHandle_t s_mtx;
static SemaphoreHandle_t s_save_mtx;     // serialise les ecritures de tunnel.json
static bool              s_inited;
static bool              s_started;
static TaskHandle_t      s_ws_task;
static QueueHandle_t     s_job_q;        // job_t*      : requetes / activation pour les workers
static QueueHandle_t     s_out_q;        // out_msg_t*  : messages a emettre par la tache WebSocket

static tn_cfg_t            s_cfg;
static bool                s_dirty;      // enregistrement differe en echec, a retenter
static tunnel_state_t      s_state = TUNNEL_STATE_DISABLED;
static char                s_device_id[TUNNEL_CLIENT_ID_MAX];
static char                s_subdomain[128];
static char                s_last_error[96];
static int                 s_upgrade_status;
static int64_t             s_connected_at_ms;
static int64_t             s_retry_at_ms;
static int64_t             s_restart_at_ms = -1;   // -1 : aucun redemarrage demande
static uint32_t            s_reconnects;
static tunnel_activation_t s_act;

// Numero de la connexion WebSocket ouverte (0 = aucune). Chaque requete et
// chaque message porte le numero de sa connexion : apres une reconnexion, le
// relais ne saurait plus a quoi rattacher les reponses de l'ancienne, on les
// jette (la LiXee-Box videait ses slots et sa file pour la meme raison).
static std::atomic<uint32_t> s_session_seq{0};
static std::atomic<uint32_t> s_connected_session{0};
static std::atomic<uint32_t> s_requests{0};
static std::atomic<uint32_t> s_in_flight{0};

static inline void lock(void)   { xSemaphoreTake(s_mtx, portMAX_DELAY); }
static inline void unlock(void) { xSemaphoreGive(s_mtx); }

static void wake_ws_task(void)
{
    if (s_ws_task) xTaskNotifyGive(s_ws_task);
}

static void set_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_error(const char *fmt, ...)
{
    char buf[sizeof(s_last_error)];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lock();
    strlcpy(s_last_error, buf, sizeof(s_last_error));
    unlock();
    ESP_LOGW(TAG, "%s", buf);
}

bool tunnel_security_ok(void)
{
    const cfg_settings_t *st = cfg_get_settings();
    const cfg_general_t  *g  = cfg_get_general();
    return st->enable_secure_http && g->user_http[0] != '\0' && g->pass_http[0] != '\0';
}

/** A appeler sous s_mtx. */
static bool should_run_locked(tunnel_state_t *idle_state)
{
    if (!s_cfg.enable || s_cfg.token[0] == '\0') { *idle_state = TUNNEL_STATE_DISABLED;  return false; }
    if (!tunnel_security_ok())                    { *idle_state = TUNNEL_STATE_SUSPENDED; return false; }
    return true;
}

static void request_restart(uint32_t delay_ms)
{
    lock();
    int64_t at = now_ms() + delay_ms;
    if (s_restart_at_ms < 0 || at < s_restart_at_ms) s_restart_at_ms = at;
    unlock();
    wake_ws_task();
}

// ============================================================================
// Configuration : /littlefs/config/tunnel.json (+ migration LiXee-Box)
// ============================================================================

static char *read_text_file(const char *path, size_t max)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || (size_t)sz > max) { fclose(f); return NULL; }
    char *buf = (char *)tn_alloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    return buf;
}

static void wipe_free(char *s)
{
    if (!s) return;
    memset(s, 0, strlen(s));
    free(s);
}

static void cfg_from_json(const cJSON *doc, tn_cfg_t *c)
{
    const cJSON *it = cJSON_GetObjectItem(doc, "enableTunnel");
    if (it) c->enable = json_truthy(it);
    it = cJSON_GetObjectItem(doc, "tunnelClientId");
    if (cJSON_IsString(it)) {
        if (valid_cred(it->valuestring, sizeof(c->client_id))) strlcpy(c->client_id, it->valuestring, sizeof(c->client_id));
        else ESP_LOGW(TAG, "tunnelClientId invalide (longueur ou caracteres) : ignore");
    }
    it = cJSON_GetObjectItem(doc, "tunnelToken");
    if (cJSON_IsString(it)) {
        if (valid_cred(it->valuestring, sizeof(c->token))) strlcpy(c->token, it->valuestring, sizeof(c->token));
        else ESP_LOGW(TAG, "tunnelToken invalide (longueur ou caracteres) : ignore");
    }
}

/**
 * Ecrit tunnel.json. Ecrit en FLASH : jamais depuis une tache a pile PSRAM.
 * Le cliche de la configuration est pris sous s_save_mtx, pour qu'un
 * enregistrement plus ancien ne puisse pas ecraser un plus recent.
 */
static esp_err_t save_config(void)
{
    xSemaphoreTake(s_save_mtx, portMAX_DELAY);
    tn_cfg_t c;
    lock();
    c = s_cfg;
    unlock();

    esp_err_t err = ESP_ERR_NO_MEM;
    cJSON *doc = cJSON_CreateObject();
    if (doc) {
        // Memes noms de cles que configGeneral.json sur la LiXee-Box
        cJSON_AddNumberToObject(doc, "enableTunnel", c.enable ? 1 : 0);
        cJSON_AddStringToObject(doc, "tunnelClientId", c.client_id);
        cJSON_AddStringToObject(doc, "tunnelToken", c.token);
        char *txt = cJSON_PrintUnformatted(doc);
        cJSON_Delete(doc);
        if (txt) {
            err = cfg_write_file(TUNNEL_CFG_PATH, txt);
            wipe_free(txt);
        }
    }
    memset(&c, 0, sizeof(c));
    xSemaphoreGive(s_save_mtx);

    lock();
    s_dirty = (err != ESP_OK);
    unlock();
    if (err != ESP_OK) ESP_LOGE(TAG, "Echec d'ecriture de %s (%s)", TUNNEL_CFG_PATH, esp_err_to_name(err));
    return err;
}

static void save_task(void *arg)
{
    (void)arg;
    save_config();
    vTaskDelete(NULL);   // tache creee par xTaskCreate : l'idle libere pile et TCB
}

/**
 * Enregistrement demande depuis une tache a pile PSRAM (worker) : confie a
 * une tache ephemere a pile INTERNE, seule autorisee a ecrire en flash.
 */
static void persist_async(void)
{
    if (xTaskCreatePinnedToCore(save_task, "tunnel_save", SAVE_TASK_STACK, NULL, 3, NULL,
                                tskNO_AFFINITY) != pdPASS) {
        ESP_LOGE(TAG, "Tache d'enregistrement impossible : reessai au prochain appel de l'API");
        lock();
        s_dirty = true;
        unlock();
    }
}

esp_err_t tunnel_init(void)
{
    if (s_inited) return ESP_OK;
    s_mtx      = xSemaphoreCreateMutex();
    s_save_mtx = xSemaphoreCreateMutex();
    if (!s_mtx || !s_save_mtx) return ESP_ERR_NO_MEM;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_inited = true;

    char *txt = read_text_file(TUNNEL_CFG_PATH, 2048);
    if (txt) {
        cJSON *doc = cJSON_Parse(txt);
        wipe_free(txt);
        if (doc) {
            cfg_from_json(doc, &s_cfg);
            cJSON_Delete(doc);
        } else {
            ESP_LOGE(TAG, "%s illisible : tunnel desactive", TUNNEL_CFG_PATH);
        }
    } else {
        // Migration LiXee-Box : les reglages etaient dans configGeneral.json.
        // config_manager ne les connait pas et les perdra au prochain
        // cfg_save_general() : il faut les reprendre AVANT (voir tunnel.h).
        char *gen = read_text_file(CFG_PATH_GENERAL, 64 * 1024);
        if (gen) {
            cJSON *doc = cJSON_Parse(gen);
            wipe_free(gen);   // contient aussi les mots de passe HTTP/MQTT/SMTP
            if (doc) {
                bool found = cJSON_GetObjectItem(doc, "enableTunnel") ||
                             cJSON_GetObjectItem(doc, "tunnelToken") ||
                             cJSON_GetObjectItem(doc, "tunnelClientId");
                if (found) cfg_from_json(doc, &s_cfg);
                cJSON_Delete(doc);
                if (found) {
                    ESP_LOGI(TAG, "Reglages du tunnel repris de configGeneral.json (LiXee-Box)");
                    save_config();
                }
            }
        }
    }

    ESP_LOGI(TAG, "Tunnel %s, clientId '%s', jeton %s", s_cfg.enable ? "active" : "desactive",
             s_cfg.client_id, s_cfg.token[0] ? "present" : "absent");
    if (s_cfg.enable && s_cfg.token[0] && !tunnel_security_ok()) {
        // Comme la LiXee-Box : jamais d'interface exposee sans protection.
        ESP_LOGW(TAG, "Tunnel suspendu : acces securise HTTP (identifiant + mot de passe) non configure");
    }
    return ESP_OK;
}

// ============================================================================
// Messages a emettre (file s_out_q)
// ============================================================================

typedef struct {
    uint32_t session;
    size_t   len;
    char    *data;   // tas (ecrivable : le masquage WebSocket se fait en place)
} out_msg_t;

static void out_free(out_msg_t *m)
{
    if (!m) return;
    free(m->data);
    free(m);
}

/** Prend possession de data (alloue par malloc/tn_alloc/cJSON). */
static bool out_push(char *data, size_t len, uint32_t session, TickType_t wait)
{
    if (!data) return false;
    if (!s_out_q || session == 0 || session != s_connected_session.load()) {
        free(data);
        return false;
    }
    out_msg_t *m = (out_msg_t *)tn_alloc(sizeof(out_msg_t));
    if (!m) { free(data); return false; }
    m->session = session;
    m->len     = len;
    m->data    = data;
    if (xQueueSend(s_out_q, &m, wait) != pdTRUE) {
        ESP_LOGW(TAG, "File d'emission pleine : message de %u o perdu", (unsigned)len);
        out_free(m);
        return false;
    }
    return true;
}

static bool out_push_copy(const char *s, size_t len, uint32_t session, TickType_t wait)
{
    char *d = (char *)tn_alloc(len + 1);
    if (!d) return false;
    memcpy(d, s, len);
    d[len] = '\0';
    return out_push(d, len, session, wait);
}

static bool out_push_sbuf(sbuf_t *b, uint32_t session, TickType_t wait)
{
    if (b->oom || !b->p) { sb_free(b); return false; }
    char *p = b->p;
    size_t n = b->len;
    b->p = NULL;
    b->len = b->cap = 0;
    return out_push(p, n, session, wait);
}

static bool out_push_cjson(cJSON *doc, uint32_t session, TickType_t wait)
{
    char *s = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (!s) return false;
    return out_push(s, strlen(s), session, wait);
}

/** Reponse d'erreur au format de sendErrorResponse() de la LiXee-Box. */
static void send_error_response(uint32_t session, const char *req_id, int status,
                                const char *msg, TickType_t wait)
{
    unsigned char b64[160];
    size_t olen = 0;
    if (mbedtls_base64_encode(b64, sizeof(b64), &olen, (const unsigned char *)msg, strlen(msg)) != 0) olen = 0;
    sbuf_t b = {};
    sb_adds(&b, "{\"type\":\"http_response\",\"reqId\":\"");
    sb_add_json(&b, req_id, strlen(req_id));
    sb_addf(&b, "\",\"status\":%d,\"headers\":{\"Content-Type\":\"text/plain\",\"Content-Length\":\"%u\"},\"body\":\"",
            status, (unsigned)(olen ? strlen(msg) : 0));
    sb_add(&b, (const char *)b64, olen);
    sb_adds(&b, "\"}");
    out_push_sbuf(&b, session, wait);
}

// ============================================================================
// Travaux des workers (file s_job_q)
// ============================================================================

typedef enum { JOB_HTTP = 0, JOB_ACTIVATE } job_type_t;

typedef struct {
    job_type_t type;
    uint32_t   session;
    int64_t    queued_ms;
    char       req_id[64];
    char       method[8];
    char       code[8];        // JOB_ACTIVATE
    char      *path;
    char      *headers;        // lignes "Nom: valeur\r\n" deja filtrees
    bool       has_ctype;
    uint8_t   *body;
    size_t     body_len;
} job_t;

static job_t *job_new(job_type_t type)
{
    job_t *j = (job_t *)tn_alloc(sizeof(job_t));
    if (!j) return NULL;
    memset(j, 0, sizeof(*j));
    j->type = type;
    j->queued_ms = now_ms();
    return j;
}

static void job_free(job_t *j)
{
    if (!j) return;
    free(j->path);
    if (j->headers) wipe_free(j->headers);   // cookie de session
    free(j->body);
    free(j);
}

// ---------------------------------------------------------------------------
// Echange avec le serveur HTTP local
// ---------------------------------------------------------------------------

enum { CH_SIZE = 0, CH_EXT, CH_DATA, CH_DATA_END, CH_TRAILER, CH_DONE };

typedef struct {
    int      status;
    long     content_length;   // -1 : inconnu
    bool     chunked;
    sbuf_t   hdrs;             // paires JSON "Nom":"valeur", deja echappees
    uint8_t *body;
    size_t   body_len;
    size_t   body_cap;
    int      ch_state;
    size_t   ch_remain;
    char     ch_line[20];
    size_t   ch_line_len;
} resp_t;

static void resp_free(resp_t *r)
{
    free(r->body);
    r->body = NULL;
    sb_free(&r->hdrs);
}

static bool body_append(resp_t *r, const uint8_t *d, size_t n)
{
    if (n == 0) return true;
    if (r->body_len + n > MAX_BODY) return false;
    if (r->body_len + n > r->body_cap) {
        size_t nc = r->body_cap ? r->body_cap : 16384;
        while (nc < r->body_len + n) nc *= 2;
        if (nc > MAX_BODY) nc = MAX_BODY;
        uint8_t *nb = (uint8_t *)tn_realloc(r->body, nc);
        if (!nb) return false;
        r->body = nb;
        r->body_cap = nc;
    }
    memcpy(r->body + r->body_len, d, n);
    r->body_len += n;
    return true;
}

/**
 * Decodage incremental de Transfer-Encoding: chunked (les pages assemblees
 * par httpd_resp_send_chunk). Le relais recoit le corps dechunke et un
 * Content-Length exact (sans cela la LiXee-Box voyait des reponses collees).
 * @return 1 termine, 0 a suivre, -1 erreur (trop gros, format invalide)
 */
static int chunked_feed(resp_t *r, const uint8_t *d, size_t n)
{
    size_t i = 0;
    while (i < n) {
        switch (r->ch_state) {
            case CH_SIZE:
            case CH_EXT: {
                char c = (char)d[i++];
                if (c == '\n') {
                    r->ch_line[r->ch_line_len] = '\0';
                    char *end = NULL;
                    unsigned long sz = strtoul(r->ch_line, &end, 16);
                    if (end == r->ch_line) return -1;
                    r->ch_line_len = 0;
                    if (sz == 0) { r->ch_state = CH_TRAILER; break; }
                    if (sz > MAX_BODY) return -1;
                    r->ch_remain = sz;
                    r->ch_state  = CH_DATA;
                } else if (c == ';') {
                    r->ch_state = CH_EXT;              // extension de chunk : ignoree
                } else if (c != '\r' && r->ch_state == CH_SIZE) {
                    if (r->ch_line_len >= sizeof(r->ch_line) - 1) return -1;
                    r->ch_line[r->ch_line_len++] = c;
                }
                break;
            }
            case CH_DATA: {
                size_t take = n - i;
                if (take > r->ch_remain) take = r->ch_remain;
                if (!body_append(r, d + i, take)) return -1;
                i += take;
                r->ch_remain -= take;
                if (r->ch_remain == 0) r->ch_state = CH_DATA_END;
                break;
            }
            case CH_DATA_END:
                if (d[i++] == '\n') r->ch_state = CH_SIZE;
                break;
            case CH_TRAILER: {
                char c = (char)d[i++];
                if (c == '\n') {
                    if (r->ch_line_len == 0) { r->ch_state = CH_DONE; return 1; }
                    r->ch_line_len = 0;
                } else if (c != '\r') {
                    r->ch_line_len++;
                }
                break;
            }
            default:
                return 1;
        }
    }
    return r->ch_state == CH_DONE ? 1 : 0;
}

/** @return 1 corps complet, 0 a suivre, -1 erreur. */
static int body_feed(resp_t *r, const uint8_t *d, size_t n)
{
    if (r->chunked) return n ? chunked_feed(r, d, n) : 0;
    if (r->content_length >= 0) {
        size_t want = (size_t)r->content_length - r->body_len;
        if (n > want) n = want;   // octets en trop : ignores
        if (!body_append(r, d, n)) return -1;
        return r->body_len >= (size_t)r->content_length ? 1 : 0;
    }
    return body_append(r, d, n) ? 0 : -1;   // delimite par la fermeture
}

/** Analyse le bloc d'en-tetes (termine par '\0', sans la ligne vide finale). */
static bool parse_head(resp_t *r, char *head)
{
    char *save = NULL;
    char *line = strtok_r(head, "\r\n", &save);
    if (!line || strncmp(line, "HTTP/1.", 7) != 0) return false;
    const char *sp = strchr(line, ' ');
    if (!sp) return false;
    r->status = atoi(sp + 1);
    if (r->status < 100 || r->status > 999) return false;

    while ((line = strtok_r(NULL, "\r\n", &save)) != NULL) {
        char *colon = strchr(line, ':');
        if (!colon || colon == line) continue;
        *colon = '\0';
        char *name = line;
        char *val  = colon + 1;
        while (*val == ' ' || *val == '\t') val++;
        size_t vl = strlen(val);
        while (vl && (val[vl - 1] == ' ' || val[vl - 1] == '\t')) val[--vl] = '\0';
        size_t nl = strlen(name);
        while (nl && (name[nl - 1] == ' ' || name[nl - 1] == '\t')) name[--nl] = '\0';

        if (strcasecmp(name, "content-length") == 0) {
            r->content_length = strtol(val, NULL, 10);
            if (r->content_length < 0) return false;
            continue;   // recalcule depuis le corps reellement transmis
        }
        if (strcasecmp(name, "transfer-encoding") == 0) {
            if (contains_ci(val, "chunked")) r->chunked = true;
            continue;   // le corps relaye est dechunke
        }
        if (strcasecmp(name, "connection") == 0 || strcasecmp(name, "keep-alive") == 0) continue;

        if (r->hdrs.len) sb_add(&r->hdrs, ",", 1);
        sb_add(&r->hdrs, "\"", 1);
        sb_add_json(&r->hdrs, name, nl);
        sb_add(&r->hdrs, "\":\"", 3);
        sb_add_json(&r->hdrs, val, vl);
        sb_add(&r->hdrs, "\"", 1);
    }
    if (r->chunked) r->content_length = -1;
    return !r->hdrs.oom;
}

static bool send_all(int fd, const char *p, size_t n)
{
    while (n > 0) {
        int w = send(fd, p, n, 0);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        p += w;
        n -= (size_t)w;
    }
    return true;
}

struct fd_guard {
    int fd;
    ~fd_guard() { if (fd >= 0) close(fd); }
};

struct mem_guard {
    void *p;
    ~mem_guard() { free(p); }
};

/**
 * Rejoue la requete sur 127.0.0.1:80 et lit la reponse dans *r.
 * @return 0 si une reponse complete a ete lue, sinon le statut HTTP a
 *         renvoyer au relais (*err : texte de l'erreur).
 */
static int local_exchange(job_t *j, resp_t *r, const char **err)
{
    const int64_t t0 = now_ms();
    fd_guard g{ socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) };
    if (g.fd < 0) { *err = "Cannot connect to local server"; return 502; }

    // Reception par tranches de 1 s : permet de surveiller les delais ci-dessous
    struct timeval tv = {};
    tv.tv_sec = 1;
    setsockopt(g.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct timeval tvs = {};
    tvs.tv_sec = IO_TIMEOUT_MS / 1000;
    setsockopt(g.fd, SOL_SOCKET, SO_SNDTIMEO, &tvs, sizeof(tvs));

    struct sockaddr_in a = {};
    a.sin_family      = AF_INET;
    a.sin_port        = htons(LOCAL_HTTP_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(g.fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        *err = "Cannot connect to local server";
        return 502;
    }

    // Requete : memes en-tetes de base que la LiXee-Box (Host 127.0.0.1,
    // Connection: close), Content-Length recalcule depuis le corps decode.
    sbuf_t req = {};
    sb_addf(&req, "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n", j->method, j->path);
    if (j->headers) sb_adds(&req, j->headers);
    if (j->body_len > 0) {
        if (!j->has_ctype) sb_adds(&req, "Content-Type: application/x-www-form-urlencoded\r\n");
        sb_addf(&req, "Content-Length: %u\r\n", (unsigned)j->body_len);
    }
    sb_adds(&req, "\r\n");
    bool sent = !req.oom && send_all(g.fd, req.p, req.len) &&
                (j->body_len == 0 || send_all(g.fd, (const char *)j->body, j->body_len));
    sb_wipe_free(&req);
    free(j->body);            // plus utile : liberer avant d'accumuler la reponse
    j->body = NULL;
    if (!sent) { *err = "Local server write error"; return 502; }

    mem_guard rx{ tn_alloc(RX_CHUNK) };
    if (!rx.p) { *err = "Out of memory"; return 500; }

    sbuf_t hb = {};           // en-tetes bruts, jusqu'a la ligne vide
    bool head_done = false;
    int fin = 0;
    int64_t last_data = now_ms();
    for (;;) {
        if (now_ms() - t0 > LOCAL_TOTAL_TIMEOUT_MS) { sb_free(&hb); *err = "Gateway timeout"; return 504; }
        int n = recv(g.fd, rx.p, RX_CHUNK, 0);
        if (n > 0) {
            last_data = now_ms();
            if (head_done) {
                fin = body_feed(r, (const uint8_t *)rx.p, (size_t)n);
                if (fin != 0) break;
                continue;
            }
            sb_add(&hb, (const char *)rx.p, (size_t)n);
            if (hb.oom) { sb_free(&hb); *err = "Out of memory"; return 500; }
            char *e = strstr(hb.p, "\r\n\r\n");
            if (!e) {
                if (hb.len > MAX_RESP_HEADERS) { sb_free(&hb); *err = "Bad response from local server"; return 502; }
                continue;
            }
            size_t head_len = (size_t)(e - hb.p) + 4;
            *e = '\0';
            if (!parse_head(r, hb.p)) { sb_free(&hb); *err = "Bad response from local server"; return 502; }
            head_done = true;
            if (strcmp(j->method, "HEAD") == 0 || r->status == 204 || r->status == 304 || r->status < 200) {
                fin = 1;      // pas de corps, quel que soit Content-Length
                break;
            }
            if (r->content_length > (long)MAX_BODY) {
                // La LiXee-Box tronquait silencieusement (page incomplete) : on
                // prefere une erreur franche.
                sb_free(&hb);
                *err = "Response too large";
                return 502;
            }
            if (r->content_length > 0) {
                r->body = (uint8_t *)tn_alloc((size_t)r->content_length);
                if (!r->body) { sb_free(&hb); *err = "Out of memory"; return 500; }
                r->body_cap = (size_t)r->content_length;
            }
            fin = body_feed(r, (const uint8_t *)hb.p + head_len, hb.len - head_len);
            sb_free(&hb);
            if (fin == 0 && r->content_length == 0) fin = 1;
            if (fin != 0) break;
            continue;
        }
        if (n == 0) {
            // Fermeture par le serveur : fin normale seulement pour un corps
            // delimite par la fermeture.
            fin = (head_done && !r->chunked && r->content_length < 0) ? 1 : -2;
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            if (now_ms() - last_data > LOCAL_IDLE_TIMEOUT_MS) { sb_free(&hb); *err = "Gateway timeout"; return 504; }
            continue;
        }
        fin = -2;
        break;
    }
    sb_free(&hb);
    if (fin == -1) { *err = "Response too large"; return 502; }
    if (fin < 0)   { *err = "Bad response from local server"; return 502; }
    return 0;
}

/** Traite un job HTTP : echange local puis message http_response. */
static void forward_request(job_t *j)
{
    const int64_t t0 = now_ms();
    resp_t r = {};
    r.content_length = -1;
    const char *err = "Error";
    int code = local_exchange(j, &r, &err);
    if (code != 0) {
        ESP_LOGW(TAG, "%s %s -> %d (%s)", j->method, j->path, code, err);
        resp_free(&r);
        send_error_response(j->session, j->req_id, code, err, pdMS_TO_TICKS(2000));
        return;
    }

    // Message assemble directement en PSRAM, base64 encode en place : un seul
    // gros tampon (la LiXee-Box en tenait trois : corps, base64, JSON).
    size_t b64_len = 4 * ((r.body_len + 2) / 3);
    sbuf_t m = {};
    sb_adds(&m, "{\"type\":\"http_response\",\"reqId\":\"");
    sb_add_json(&m, j->req_id, strlen(j->req_id));
    sb_addf(&m, "\",\"status\":%d,\"headers\":{", r.status);
    if (r.hdrs.len) {
        sb_add(&m, r.hdrs.p, r.hdrs.len);
        sb_add(&m, ",", 1);
    }
    sb_addf(&m, "\"Content-Length\":\"%u\"},\"body\":\"", (unsigned)r.body_len);
    sb_free(&r.hdrs);
    if (r.body_len > 0 && sb_reserve(&m, b64_len + 3)) {
        size_t olen = 0;
        if (mbedtls_base64_encode((unsigned char *)m.p + m.len, m.cap - m.len, &olen,
                                  r.body, r.body_len) == 0) {
            m.len += olen;
            m.p[m.len] = '\0';
        } else {
            m.oom = true;
        }
    }
    unsigned body_len = (unsigned)r.body_len;
    resp_free(&r);
    sb_adds(&m, "\"}");

    if (m.oom) {
        sb_free(&m);
        ESP_LOGW(TAG, "%s %s : memoire insuffisante pour %u o", j->method, j->path, body_len);
        send_error_response(j->session, j->req_id, 500, "Out of memory", pdMS_TO_TICKS(2000));
        return;
    }
    ESP_LOGI(TAG, "%s %s -> %d (%u o, %lld ms)", j->method, j->path, r.status,
             body_len, (long long)(now_ms() - t0));
    // Contre-pression : si la file d'emission est pleine (lien montant lent),
    // le worker patiente au lieu d'accumuler des reponses en memoire.
    out_push_sbuf(&m, j->session, pdMS_TO_TICKS(10000));
}

// ---------------------------------------------------------------------------
// Activation par code (processTunnelActivation de la LiXee-Box)
// ---------------------------------------------------------------------------

static void act_finish(bool ok, const char *device_id, const char *error)
{
    lock();
    s_act.status  = TUNNEL_ACT_DONE;
    s_act.success = ok;
    strlcpy(s_act.device_id, device_id ? device_id : "", sizeof(s_act.device_id));
    strlcpy(s_act.error, error ? error : "", sizeof(s_act.error));
    unlock();
}

static void run_activation(const char *code)
{
    // Comme la LiXee-Box : laisser partir la reponse {"status":"pending"}
    // (elle peut transiter par le tunnel, qui va redemarrer).
    vTaskDelay(pdMS_TO_TICKS(2000));

    char url[sizeof(TUNNEL_ACTIVATE_URL) + 8];
    sfmt(url, sizeof(url), "%s%s", TUNNEL_ACTIVATE_URL, code);

    char *body = (char *)tn_alloc(1025);
    if (!body) { act_finish(false, "", "M\u00e9moire insuffisante"); return; }
    size_t blen = 0;
    int http_code = 0;
    esp_err_t last_err = ESP_OK;

    // 3 essais : un echec isole du handshake TLS n'est pas rare.
    for (int attempt = 1; attempt <= 3 && http_code != 200; attempt++) {
        esp_http_client_config_t c = {};
        c.url               = url;
        c.method            = HTTP_METHOD_GET;
        c.timeout_ms        = 20000;
        c.crt_bundle_attach = esp_crt_bundle_attach;   // Let's Encrypt (ISRG X1/X2) dans le bundle complet
        c.buffer_size       = 1024;
        c.buffer_size_tx    = 512;
        c.disable_auto_redirect = true;
        esp_http_client_handle_t h = esp_http_client_init(&c);
        if (!h) {
            last_err = ESP_ERR_NO_MEM;
            http_code = -1;
        } else {
            esp_err_t e = esp_http_client_open(h, 0);
            if (e == ESP_OK && esp_http_client_fetch_headers(h) >= 0) {
                http_code = esp_http_client_get_status_code(h);
                blen = 0;
                if (http_code == 200) {
                    int n;
                    while (blen < 1024 && (n = esp_http_client_read(h, body + blen, (int)(1024 - blen))) > 0) {
                        blen += (size_t)n;
                    }
                }
            } else {
                last_err = (e != ESP_OK) ? e : ESP_FAIL;
                http_code = -1;
            }
            esp_http_client_close(h);
            esp_http_client_cleanup(h);
        }
        if (http_code != 200) {
            ESP_LOGW(TAG, "Activation : essai %d/3 echoue (HTTP %d, %s, RAM interne %u)", attempt, http_code,
                     esp_err_to_name(last_err), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            if (attempt < 3) vTaskDelay(pdMS_TO_TICKS(1500));
        }
    }
    body[blen] = '\0';

    char msg[192];
    if (http_code != 200) {
        free(body);
        if (http_code < 0) {
            // Transport : on n'a jamais parle au serveur, le code n'est pas en cause.
            sfmt(msg, sizeof(msg), "Impossible de joindre le serveur d'activation (%s). Le code n'est pas en "
                 "cause : v\u00e9rifiez l'acc\u00e8s Internet, puis r\u00e9essayez.", esp_err_to_name(last_err));
        } else {
            sfmt(msg, sizeof(msg), "Le serveur d'activation a r\u00e9pondu HTTP %d", http_code);
        }
        act_finish(false, "", msg);
        return;
    }

    cJSON *doc = cJSON_Parse(body);
    memset(body, 0, blen);
    free(body);
    if (!doc) { act_finish(false, "", "R\u00e9ponse invalide du serveur"); return; }

    if (!cJSON_IsTrue(cJSON_GetObjectItem(doc, "success"))) {
        const cJSON *e = cJSON_GetObjectItem(doc, "error");
        act_finish(false, "", cJSON_IsString(e) ? e->valuestring : "Activation refus\u00e9e");
        cJSON_Delete(doc);
        return;
    }
    const cJSON *did = cJSON_GetObjectItem(doc, "deviceId");
    const cJSON *tok = cJSON_GetObjectItem(doc, "token");
    if (!cJSON_IsString(did) || !cJSON_IsString(tok) || tok->valuestring[0] == '\0' ||
        !valid_cred(did->valuestring, TUNNEL_CLIENT_ID_MAX) || !valid_cred(tok->valuestring, TUNNEL_TOKEN_MAX)) {
        cJSON_Delete(doc);
        act_finish(false, "", "R\u00e9ponse invalide du serveur");
        return;
    }
    // Derniere verification : la securite a pu changer pendant l'echange.
    if (!tunnel_security_ok()) {
        cJSON_Delete(doc);
        act_finish(false, "", "L'acc\u00e8s s\u00e9curis\u00e9 HTTP (identifiant et mot de passe) doit \u00eatre "
                              "configur\u00e9 avant d'activer le tunnel.");
        return;
    }

    char dev[TUNNEL_CLIENT_ID_MAX];
    strlcpy(dev, did->valuestring, sizeof(dev));
    lock();
    s_cfg.enable = true;
    strlcpy(s_cfg.client_id, did->valuestring, sizeof(s_cfg.client_id));
    strlcpy(s_cfg.token, tok->valuestring, sizeof(s_cfg.token));
    s_dirty = true;
    unlock();
    memset(tok->valuestring, 0, strlen(tok->valuestring));
    cJSON_Delete(doc);

    persist_async();          // ce worker a une pile PSRAM : pas d'ecriture flash ici
    request_restart(0);       // rechargement a chaud, comme la LiXee-Box
    act_finish(true, dev, "");
    ESP_LOGI(TAG, "Tunnel active par code d'activation (deviceId %s)", dev);
}

static void worker_task(void *arg)
{
    (void)arg;
    for (;;) {
        job_t *j = NULL;
        if (xQueueReceive(s_job_q, &j, portMAX_DELAY) != pdTRUE || !j) continue;

        if (j->type == JOB_ACTIVATE) {
            run_activation(j->code);
            job_free(j);
            continue;
        }
        if (j->session != s_connected_session.load()) {   // connexion perdue entre-temps
            job_free(j);
            continue;
        }
        // Memoire interne basse : on differe plutot que de rejeter (une
        // requete jetee casse la page), dans la limite du delai d'attente.
        while (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < MIN_INTERNAL_FREE &&
               now_ms() - j->queued_ms < PENDING_TIMEOUT_MS) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        int64_t waited = now_ms() - j->queued_ms;
        if (waited > PENDING_TIMEOUT_MS) {
            // Le relais repondrait de lui-meme plus tard (et en JSON, que le
            // navigateur refuse pour un .js) : un 503 franc, plus tot.
            ESP_LOGW(TAG, "Abandon apres %lld ms d'attente : %s %s", (long long)waited, j->method, j->path);
            send_error_response(j->session, j->req_id, 503, "Server busy", pdMS_TO_TICKS(2000));
            job_free(j);
            continue;
        }
        s_in_flight++;
        forward_request(j);
        s_in_flight--;
        job_free(j);
    }
}

// ============================================================================
// Tache WebSocket
// ============================================================================

// En-tetes relayes au serveur local : liste de la LiXee-Box, dans l'ordre de
// priorite (le budget MAX_FWD_HEADERS peut ecarter les derniers).
static const char *const FWD_HEADERS[] = {
    "cookie", "content-type", "x-requested-with", "origin",
    "accept", "accept-encoding", "authorization", "referer",
};

static const cJSON *obj_get_ci(const cJSON *obj, const char *name)
{
    for (const cJSON *c = obj ? obj->child : NULL; c; c = c->next) {
        if (c->string && strcasecmp(c->string, name) == 0) return c;
    }
    return NULL;
}

static bool has_ctl(const char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c == 0x7F) return true;
    }
    return false;
}

/**
 * Ne relaie que les cookies "session" du pont (32 caracteres hexadecimaux,
 * voir web_server.cpp). Le domaine lixee-box.fr peut porter d'autres cookies
 * (ceux du relais) : les relayer tous pouvait depasser la limite d'en-tetes
 * du serveur local (431) sans rien apporter.
 */
static bool filter_session_cookie(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    const char *p = in;
    out[0] = '\0';
    while (*p) {
        while (*p == ' ' || *p == ';') p++;
        const char *end = strchr(p, ';');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n > 8 && strncmp(p, "session=", 8) == 0) {
            size_t vl = n - 8;
            bool hex = (vl == 32);
            for (size_t i = 0; hex && i < vl; i++) hex = isxdigit((unsigned char)p[8 + i]) != 0;
            if (hex && o + n + 3 < out_len) {
                if (o) { out[o++] = ';'; out[o++] = ' '; }
                memcpy(out + o, p, n);
                o += n;
                out[o] = '\0';
            }
        }
        p += n;
    }
    return o > 0;
}

static bool method_ok(const char *m)
{
    static const char *const OK[] = { "GET", "POST", "PUT", "DELETE", "HEAD", "OPTIONS", "PATCH" };
    for (size_t i = 0; i < sizeof(OK) / sizeof(OK[0]); i++) {
        if (strcmp(m, OK[i]) == 0) return true;
    }
    return false;
}

static void handle_http_request(const cJSON *doc, uint32_t session)
{
    const cJSON *jid = cJSON_GetObjectItem(doc, "reqId");
    const cJSON *jm  = cJSON_GetObjectItem(doc, "method");
    const cJSON *jp  = cJSON_GetObjectItem(doc, "path");
    if (!cJSON_IsString(jid) || !cJSON_IsString(jm) || !cJSON_IsString(jp)) return;   // ignore (LiXee-Box)
    const char *rid = jid->valuestring;
    size_t rl = strlen(rid);
    if (rl == 0 || rl >= sizeof(job_t::req_id)) {
        ESP_LOGW(TAG, "reqId invalide (%u o) : requete ignoree", (unsigned)rl);
        return;
    }
    s_requests++;

    // Validation : la LiXee-Box recopiait methode et chemin tels quels dans la
    // ligne de requete locale ; un CR/LF permettait d'y injecter des en-tetes.
    if (!method_ok(jm->valuestring)) { send_error_response(session, rid, 405, "Method not allowed", 0); return; }
    const char *path = jp->valuestring;
    size_t pl = strlen(path);
    if (pl == 0 || path[0] != '/' || has_ctl(path) || strchr(path, ' ')) {
        send_error_response(session, rid, 400, "Bad request", 0);
        return;
    }
    if (pl >= LOCAL_MAX_URI) { send_error_response(session, rid, 414, "URI too long", 0); return; }

    job_t *j = job_new(JOB_HTTP);
    if (!j) { send_error_response(session, rid, 503, "Out of memory", 0); return; }
    j->session = session;
    strlcpy(j->req_id, rid, sizeof(j->req_id));
    strlcpy(j->method, jm->valuestring, sizeof(j->method));
    j->path = tn_strdup(path);

    const cJSON *hs = cJSON_GetObjectItem(doc, "headers");
    sbuf_t hb = {};
    if (cJSON_IsObject(hs)) {
        for (size_t k = 0; k < sizeof(FWD_HEADERS) / sizeof(FWD_HEADERS[0]); k++) {
            const cJSON *h = obj_get_ci(hs, FWD_HEADERS[k]);
            if (!cJSON_IsString(h)) continue;
            const char *v = h->valuestring;
            char cookie[160];
            if (k == 0) {
                if (!filter_session_cookie(v, cookie, sizeof(cookie))) continue;
                v = cookie;
            }
            size_t vl = strlen(v);
            if (vl == 0 || vl > MAX_FWD_HEADER_VALUE || has_ctl(v)) continue;   // has_ctl : pas de CR/LF
            if (hb.len + strlen(FWD_HEADERS[k]) + vl + 4 > MAX_FWD_HEADERS) {
                ESP_LOGD(TAG, "En-tete %s ecarte (budget)", FWD_HEADERS[k]);
                continue;
            }
            sb_adds(&hb, h->string);   // nom tel qu'envoye (meme casse que la LiXee-Box)
            sb_add(&hb, ": ", 2);
            sb_add(&hb, v, vl);
            sb_add(&hb, "\r\n", 2);
            if (k == 1) j->has_ctype = true;
            if (k == 0) memset(cookie, 0, sizeof(cookie));
        }
    }
    j->headers = hb.p;
    bool oom = !j->path || hb.oom;

    const cJSON *jb = cJSON_GetObjectItem(doc, "body");
    if (!oom && cJSON_IsString(jb) && jb->valuestring[0]) {
        const unsigned char *src = (const unsigned char *)jb->valuestring;
        size_t sl = strlen(jb->valuestring);
        size_t need = 0;
        mbedtls_base64_decode(NULL, 0, &need, src, sl);
        if (need == 0) { job_free(j); send_error_response(session, rid, 400, "Bad request", 0); return; }
        j->body = (uint8_t *)tn_alloc(need);
        if (!j->body) {
            oom = true;
        } else if (mbedtls_base64_decode(j->body, need, &j->body_len, src, sl) != 0) {
            job_free(j);
            send_error_response(session, rid, 400, "Bad request", 0);
            return;
        }
    }
    if (oom) { job_free(j); send_error_response(session, rid, 503, "Out of memory", 0); return; }

    if (xQueueSend(s_job_q, &j, 0) != pdTRUE) {
        // Dernier recours seulement : jeter une requete casse la page.
        ESP_LOGW(TAG, "File pleine (%d), rejet %s %s", JOB_QUEUE_LEN, j->method, j->path);
        send_error_response(session, rid, 503, "Server busy", 0);
        job_free(j);
    }
}

static void local_ip(char *out, size_t n)
{
    out[0] = '\0';
    esp_netif_t *nif = esp_netif_get_default_netif();
    esp_netif_ip_info_t ip;
    if (nif && esp_netif_get_ip_info(nif, &ip) == ESP_OK) {
        sfmt(out, n, IPSTR, IP2STR(&ip.ip));
    }
}

static void handle_welcome(const cJSON *doc, uint32_t session)
{
    const cJSON *did = cJSON_GetObjectItem(doc, "deviceId");
    const cJSON *sub = cJSON_GetObjectItem(doc, "subdomain");
    lock();
    strlcpy(s_device_id, cJSON_IsString(did) ? did->valuestring : "", sizeof(s_device_id));
    strlcpy(s_subdomain, cJSON_IsString(sub) ? sub->valuestring : "", sizeof(s_subdomain));
    ESP_LOGI(TAG, "Enregistre aupres du relais : %s (%s)", s_device_id, s_subdomain);
    unlock();

    // Message "info" identique a la LiXee-Box (le relais l'affiche)
    cJSON *info = cJSON_CreateObject();
    if (!info) return;
    cJSON_AddStringToObject(info, "type", "info");
    cJSON *data = cJSON_AddObjectToObject(info, "data");
    if (data) {
        char ip[32];
        local_ip(ip, sizeof(ip));
        cJSON_AddStringToObject(data, "firmware", TUNNEL_FW_NAME);
        cJSON_AddStringToObject(data, "version", esp_app_get_description()->version);
        cJSON_AddStringToObject(data, "chip", CHIP_NAME);
        cJSON_AddNumberToObject(data, "freeHeap", (double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        cJSON_AddStringToObject(data, "localIP", ip);
    }
    out_push_cjson(info, session, 0);
}

static void handle_message(char *msg, size_t len, uint32_t session)
{
    cJSON *doc = cJSON_ParseWithLength(msg, len);
    if (!doc) {
        ESP_LOGW(TAG, "JSON invalide recu du relais (%u o)", (unsigned)len);
        return;
    }
    const cJSON *type = cJSON_GetObjectItem(doc, "type");
    if (cJSON_IsString(type)) {
        const char *t = type->valuestring;
        if (strcmp(t, "http_request") == 0) {
            handle_http_request(doc, session);
        } else if (strcmp(t, "welcome") == 0) {
            handle_welcome(doc, session);
        } else if (strcmp(t, "ping") == 0) {
            // Ping applicatif de l'ancien protocole (TunnelClient) : sans cout
            static const char PONG[] = "{\"type\":\"pong\"}";
            out_push_copy(PONG, sizeof(PONG) - 1, session, 0);
        } else if (strcmp(t, "auth_failed") == 0 || strcmp(t, "error") == 0) {
            const cJSON *m = cJSON_GetObjectItem(doc, "message");
            if (!cJSON_IsString(m)) m = cJSON_GetObjectItem(doc, "error");
            set_error("Relais : %s", cJSON_IsString(m) ? m->valuestring : t);
        } else {
            ESP_LOGD(TAG, "Message ignore : %s", t);
        }
    }
    cJSON_Delete(doc);
}

static void queue_heartbeat(uint32_t session)
{
    char buf[160];
    int n = snprintf(buf, sizeof(buf), "{\"type\":\"heartbeat\",\"uptime\":%lu,\"freeHeap\":%u,\"freePsram\":%u}",
                     (unsigned long)(esp_timer_get_time() / 1000000),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (n > 0 && n < (int)sizeof(buf)) out_push_copy(buf, (size_t)n, session, 0);
    ESP_LOGD(TAG, "Heartbeat - en attente: %u, en cours: %u", (unsigned)uxQueueMessagesWaiting(s_job_q),
             (unsigned)s_in_flight.load());
}

// ---------------------------------------------------------------------------
// Reception des trames
// ---------------------------------------------------------------------------

typedef struct {
    char  *buf;          // MAX_IN_MSG + RX_SPARE + 1 octets (PSRAM)
    size_t len;          // octets du message de donnees en cours
    bool   in_msg;       // message fragmente en cours (FIN pas encore recu)
    int    msg_op;       // opcode du premier fragment
    bool   overflow;     // message trop grand : lu et jete
    int    frame_left;   // octets restant a lire dans la trame courante
    int    frame_op;
    bool   frame_fin;
    int    frame_pos;    // octets deja lus (trame de controle)
    bool   pong_ok;
} rx_t;

static bool ws_send_ctrl(esp_transport_handle_t ws, int opcode, char *payload, int len)
{
    char z[1];
    int w = esp_transport_ws_send_raw(ws, (ws_transport_opcodes_t)(opcode | WS_TRANSPORT_OPCODES_FIN),
                                      payload ? payload : z, len, IO_TIMEOUT_MS);
    return w == len;
}

/**
 * Lit une portion de trame (appele quand le transport est lisible).
 * esp_transport_read() sur un transport WS ne depasse jamais la trame
 * courante : on suit nous-memes les frontieres de trames et de messages.
 * @return 0 normal, 1 fermeture demandee par le relais, -1 erreur.
 */
static int rx_step(esp_transport_handle_t ws, rx_t *rx, uint32_t session)
{
    const bool new_frame = (rx->frame_left == 0);
    const bool ctrl_cont = !new_frame && (rx->frame_op & 0x08);
    // Zone de lecture : a la suite du message en cours ; une trame de controle
    // y est lue sans faire avancer le message (RX_SPARE lui est reserve).
    char  *base = rx->overflow ? rx->buf + MAX_IN_MSG : rx->buf + rx->len;
    size_t room = rx->overflow ? RX_SPARE : (MAX_IN_MSG - rx->len) + RX_SPARE;
    char  *dst  = base;
    if (ctrl_cont) {
        dst  += rx->frame_pos;
        room -= (size_t)rx->frame_pos;
    }

    int n = esp_transport_read(ws, dst, (int)room, IO_TIMEOUT_MS);
    if (n < 0) return -1;

    if (new_frame) {
        ws_transport_opcodes_t op = esp_transport_ws_get_read_opcode(ws);
        if (op == WS_TRANSPORT_OPCODES_NONE) return 0;   // rien de complet
        int plen = esp_transport_ws_get_read_payload_len(ws);
        // En-tete lu mais charge non recue : le transport a deja oublie la
        // trame, le flux est desynchronise.
        if (plen < 0 || (plen > 0 && n == 0)) return -1;
        rx->frame_op   = (int)op & 0x0F;
        rx->frame_fin  = esp_transport_ws_get_fin_flag(ws);
        rx->frame_left = plen;
        rx->frame_pos  = 0;
        if (!(rx->frame_op & 0x08)) {
            if (rx->frame_op == WS_TRANSPORT_OPCODES_CONT) {
                if (!rx->in_msg) return -1;
            } else {
                if (rx->in_msg) return -1;   // nouveau message avant la fin du precedent
                rx->in_msg   = true;
                rx->msg_op   = rx->frame_op;
                rx->len      = 0;
                rx->overflow = false;
            }
        }
    } else if (n == 0) {
        return -1;   // suite de trame non recue dans le delai : desynchronise
    }

    rx->frame_left -= n;
    if (rx->frame_left < 0) return -1;

    if (rx->frame_op & 0x08) {
        // Trame de controle (propagate_control_frames = true : geree ici)
        rx->frame_pos += n;
        if (rx->frame_left > 0) return 0;
        int plen = rx->frame_pos;
        rx->frame_pos = 0;
        switch (rx->frame_op) {
            case WS_TRANSPORT_OPCODES_PING:
                // Pong avec la meme charge ; autorise entre deux fragments
                return ws_send_ctrl(ws, WS_TRANSPORT_OPCODES_PONG, base, plen) ? 0 : -1;
            case WS_TRANSPORT_OPCODES_PONG:
                rx->pong_ok = true;
                return 0;
            case WS_TRANSPORT_OPCODES_CLOSE:
                ws_send_ctrl(ws, WS_TRANSPORT_OPCODES_CLOSE, NULL, 0);
                return 1;
            default:
                return 0;
        }
    }

    // Trame de donnees
    if (!rx->overflow) {
        rx->len += (size_t)n;
        if (rx->len > MAX_IN_MSG) {
            rx->overflow = true;
            ESP_LOGW(TAG, "Message entrant > %u o : ignore", (unsigned)MAX_IN_MSG);
        }
    }
    if (rx->frame_left > 0 || !rx->frame_fin) return 0;

    bool keep = !rx->overflow && rx->msg_op == WS_TRANSPORT_OPCODES_TEXT;   // binaire : ignore (LiXee-Box)
    size_t len = rx->len;
    rx->in_msg   = false;
    rx->len      = 0;
    rx->overflow = false;
    if (keep) {
        rx->buf[len] = '\0';
        handle_message(rx->buf, len, session);
    }
    return 0;
}

/** Emet le fragment suivant de m (TEXT puis CONTINUATION, FIN sur le dernier). */
static bool send_fragment(esp_transport_handle_t ws, out_msg_t *m, size_t *off)
{
    size_t n = m->len - *off;
    if (n > WS_FRAGMENT) n = WS_FRAGMENT;
    bool first = (*off == 0);
    bool last  = (*off + n >= m->len);
    int op = (first ? WS_TRANSPORT_OPCODES_TEXT : WS_TRANSPORT_OPCODES_CONT) | (last ? WS_TRANSPORT_OPCODES_FIN : 0);
    int w = esp_transport_ws_send_raw(ws, (ws_transport_opcodes_t)op, m->data + *off, (int)n, IO_TIMEOUT_MS);
    if (w != (int)n) return false;
    *off += n;
    return true;
}

typedef enum { SESS_FAILED = 0, SESS_REFUSED, SESS_CLOSED, SESS_RESTART } sess_end_t;

/** Boucle d'une connexion etablie. */
static sess_end_t ws_loop(esp_transport_handle_t ws, rx_t *rx, uint32_t session)
{
    out_msg_t *cur = NULL;
    size_t cur_off = 0;
    int64_t t = now_ms();
    // Premier ping immediat. Si le relais a envoye "welcome" dans le meme
    // enregistrement TLS que la reponse 101, esp_transport_ws garde ces octets
    // dans son tampon de poignee de main, mais ne les lit qu'une fois la socket
    // de nouveau lisible : le pong du relais la rend lisible tout de suite
    // (sinon "welcome" attendrait le premier ping, 15 s plus tard).
    int64_t last_hb = t, last_ping = t - PING_INTERVAL_MS;
    int misses = 0;
    rx->pong_ok = true;
    sess_end_t end = SESS_CLOSED;

    for (;;) {
        // Arret (desactivation, securite perdue) ou redemarrage (nouveaux identifiants)
        tunnel_state_t idle;
        lock();
        bool run = should_run_locked(&idle);
        bool restart = (s_restart_at_ms >= 0 && now_ms() >= s_restart_at_ms);
        unlock();
        if (!run || restart) {
            ws_send_ctrl(ws, WS_TRANSPORT_OPCODES_CLOSE, NULL, 0);
            end = SESS_RESTART;
            break;
        }

        // Lecture : attente courte seulement s'il n'y a rien a emettre
        int pr = esp_transport_poll_read(ws, cur ? 0 : 20);
        if (pr < 0) { set_error("Connexion perdue (lecture)"); break; }
        if (pr > 0) {
            int r = rx_step(ws, rx, session);
            if (r < 0) { set_error("Connexion perdue (trame invalide ou lecture)"); break; }
            if (r > 0) { set_error("Connexion ferm\u00e9e par le relais"); break; }
            if (rx->pong_ok) misses = 0;
        }

        // Emission : un fragment par tour, pour continuer a lire (pings) entre deux
        if (!cur && xQueueReceive(s_out_q, &cur, 0) == pdTRUE) {
            cur_off = 0;
            if (cur && cur->session != session) { out_free(cur); cur = NULL; }
        }
        if (cur) {
            if (!send_fragment(ws, cur, &cur_off)) { set_error("Connexion perdue (\u00e9criture)"); break; }
            if (cur_off >= cur->len) { out_free(cur); cur = NULL; }
        }

        // Minuteries : heartbeat applicatif et ping WebSocket (15 s), pong sous 10 s
        int64_t now = now_ms();
        if (now - last_hb >= HEARTBEAT_MS) {
            last_hb = now;
            queue_heartbeat(session);
        }
        if (now - last_ping >= PING_INTERVAL_MS) {
            if (!ws_send_ctrl(ws, WS_TRANSPORT_OPCODES_PING, NULL, 0)) { set_error("Connexion perdue (ping)"); break; }
            last_ping = now;
            rx->pong_ok = false;
        } else if (!rx->pong_ok && now - last_ping > PONG_TIMEOUT_MS) {
            misses++;
            ESP_LOGW(TAG, "Pong non recu (%d/%d)", misses, PONG_MAX_MISSES);
            if (misses >= PONG_MAX_MISSES) { set_error("Le relais ne r\u00e9pond plus (pong)"); break; }
            last_ping = now - PING_INTERVAL_MS;   // nouveau ping immediat, comme WebSocketsClient
        }
    }
    out_free(cur);
    return end;
}

/** Une tentative de connexion, puis la session si elle aboutit. */
static sess_end_t ws_session(void)
{
    tn_cfg_t c;
    lock();
    c = s_cfg;
    s_restart_at_ms = -1;
    s_state = TUNNEL_STATE_CONNECTING;
    s_retry_at_ms = 0;
    unlock();

    // wss://remote.lixee-box.fr/tunnel?token=...&clientId=... (comme la LiXee-Box).
    // Valeurs encodees : la LiXee-Box les inserait brutes (un '+' du jeton
    // aurait ete lu comme un espace par le relais).
    sbuf_t path = {};
    sb_adds(&path, TUNNEL_WS_PATH "?token=");
    sb_add_urlenc(&path, c.token);
    if (c.client_id[0]) {
        sb_adds(&path, "&clientId=");
        sb_add_urlenc(&path, c.client_id);
    }
    memset(&c, 0, sizeof(c));
    if (path.oom) { sb_free(&path); set_error("M\u00e9moire insuffisante"); return SESS_FAILED; }

    esp_transport_handle_t ssl = esp_transport_ssl_init();
    esp_transport_handle_t ws  = ssl ? esp_transport_ws_init(ssl) : NULL;
    if (!ws) {
        sb_wipe_free(&path);
        if (ssl) esp_transport_destroy(ssl);
        set_error("M\u00e9moire insuffisante (transport)");
        return SESS_FAILED;
    }
    // Verification du certificat par le bundle ESP-IDF. La LiXee-Box se
    // connectait SANS verification (beginSslWithCA(..., nullptr)).
    esp_transport_ssl_crt_bundle_attach(ssl, esp_crt_bundle_attach);
    esp_transport_keep_alive_t ka = {};
    ka.keep_alive_enable   = true;
    ka.keep_alive_idle     = 30;
    ka.keep_alive_interval = 10;
    ka.keep_alive_count    = 3;
    esp_transport_ssl_set_keep_alive(ssl, &ka);

    esp_transport_ws_config_t wc = {};
    wc.ws_path      = path.p;
    wc.sub_protocol = TUNNEL_SUBPROTO;
    wc.user_agent   = TUNNEL_USER_AGENT;
    wc.headers      = TUNNEL_EXTRA_HDRS;
    wc.propagate_control_frames = true;   // pings / pongs / close geres par rx_step
    esp_err_t cerr = esp_transport_ws_set_config(ws, &wc);   // copie les chaines
    sb_wipe_free(&path);
    if (cerr != ESP_OK) {
        esp_transport_destroy(ws);
        esp_transport_destroy(ssl);
        set_error("Configuration du transport impossible");
        return SESS_FAILED;
    }

    ESP_LOGI(TAG, "Connexion a wss://%s%s (RAM interne %u)", TUNNEL_HOST, TUNNEL_WS_PATH,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    if (esp_transport_connect(ws, TUNNEL_HOST, TUNNEL_PORT, CONNECT_TIMEOUT_MS) < 0) {
        int st = esp_transport_ws_get_upgrade_request_status(ws);
        esp_tls_error_handle_t te = esp_transport_get_error_handle(ws);
        if (st > 0 && st != 101) {
            set_error("Connexion refus\u00e9e par le relais (HTTP %d)", st);
        } else if (te && (te->last_error != ESP_OK || te->esp_tls_error_code != 0)) {
            set_error("Connexion impossible (%s, TLS 0x%x)", esp_err_to_name(te->last_error),
                      (unsigned)te->esp_tls_error_code);
        } else {
            set_error("Relais injoignable");
        }
        lock();
        s_upgrade_status = st;
        unlock();
        esp_transport_close(ws);
        esp_transport_destroy(ws);
        esp_transport_destroy(ssl);
        // Jeton refuse : inutile d'insister toutes les 3 s
        return (st == 401 || st == 403) ? SESS_REFUSED : SESS_FAILED;
    }

    uint32_t session = ++s_session_seq;
    if (session == 0) session = ++s_session_seq;   // 0 reserve a "aucune connexion"
    s_connected_session.store(session);
    lock();
    s_state           = TUNNEL_STATE_CONNECTED;
    s_upgrade_status  = 101;
    s_last_error[0]   = '\0';
    s_subdomain[0]    = '\0';   // rempli par "welcome"
    s_connected_at_ms = now_ms();
    s_reconnects++;
    unlock();
    ESP_LOGI(TAG, "WebSocket connecte (RAM interne %u, PSRAM %u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    sess_end_t end = SESS_CLOSED;
    rx_t rx = {};
    rx.buf = (char *)tn_alloc(MAX_IN_MSG + RX_SPARE + 1);
    if (rx.buf) {
        end = ws_loop(ws, &rx, session);
    } else {
        set_error("M\u00e9moire insuffisante (tampon de r\u00e9ception)");
    }

    // Les requetes et reponses de cette connexion n'ont plus de destinataire
    s_connected_session.store(0);
    int64_t dur = 0;
    lock();
    dur = now_ms() - s_connected_at_ms;
    s_connected_at_ms = 0;
    unlock();
    esp_transport_close(ws);
    esp_transport_destroy(ws);
    esp_transport_destroy(ssl);
    free(rx.buf);
    out_msg_t *m = NULL;
    while (xQueueReceive(s_out_q, &m, 0) == pdTRUE) out_free(m);
    ESP_LOGI(TAG, "WebSocket deconnecte apres %lld s (RAM interne %u)", (long long)(dur / 1000),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return end;
}

static void ws_task(void *arg)
{
    (void)arg;
    uint32_t backoff = RECONNECT_MIN_MS;
    for (;;) {
        tunnel_state_t idle = TUNNEL_STATE_DISABLED;
        lock();
        bool run = should_run_locked(&idle);
        if (!run) {
            s_state = idle;
            s_retry_at_ms = 0;
        }
        unlock();
        if (!run) {
            // Reveil par notification (configuration, securite) ; reevaluation
            // periodique en filet de securite (securite modifiee ailleurs).
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
            backoff = RECONNECT_MIN_MS;
            continue;
        }
        ulTaskNotifyTake(pdTRUE, 0);   // notifications deja prises en compte

        sess_end_t end = ws_session();
        if (end == SESS_RESTART) {
            backoff = RECONNECT_MIN_MS;
            continue;   // reconnexion immediate avec la nouvelle configuration
        }
        uint32_t wait;
        if (end == SESS_CLOSED) {
            wait = RECONNECT_MIN_MS;       // connexion perdue : 3 s comme la LiXee-Box
            backoff = RECONNECT_MIN_MS;
        } else if (end == SESS_REFUSED) {
            wait = RECONNECT_MAX_MS;
        } else {
            // Echecs repetes (pas d'Internet...) : espacement progressif, la
            // LiXee-Box refaisait une poignee TLS toutes les 3 s indefiniment.
            wait = backoff;
            backoff = (backoff * 2 > RECONNECT_MAX_MS) ? RECONNECT_MAX_MS : backoff * 2;
        }
        lock();
        s_state = TUNNEL_STATE_WAITING;
        s_retry_at_ms = now_ms() + wait;
        unlock();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait));
    }
}

static BaseType_t create_psram_task(TaskFunction_t fn, const char *name, uint32_t stack,
                                    UBaseType_t prio, TaskHandle_t *handle)
{
    // Pile en PSRAM : ces taches n'accedent jamais a la flash (voir en-tete).
    BaseType_t r = xTaskCreatePinnedToCoreWithCaps(fn, name, stack, NULL, prio, handle, tskNO_AFFINITY,
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (r != pdPASS) {
        ESP_LOGW(TAG, "%s : pile PSRAM indisponible, pile interne", name);
        r = xTaskCreatePinnedToCore(fn, name, stack, NULL, prio, handle, tskNO_AFFINITY);
    }
    return r;
}

esp_err_t tunnel_start(void)
{
    if (!s_inited) {
        esp_err_t e = tunnel_init();
        if (e != ESP_OK) return e;
    }
    if (s_started) return ESP_OK;

    s_job_q = xQueueCreate(JOB_QUEUE_LEN, sizeof(job_t *));
    s_out_q = xQueueCreate(OUT_QUEUE_LEN, sizeof(out_msg_t *));
    if (!s_job_q || !s_out_q) return ESP_ERR_NO_MEM;

    for (int i = 0; i < WORKER_COUNT; i++) {
        char name[24];
        sfmt(name, sizeof(name), "tunnel_w%d", i);
        if (create_psram_task(worker_task, name, WORKER_TASK_STACK, WORKER_TASK_PRIO, NULL) != pdPASS) {
            ESP_LOGE(TAG, "Creation de %s impossible", name);
            return ESP_ERR_NO_MEM;
        }
    }
    if (create_psram_task(ws_task, "tunnel_ws", WS_TASK_STACK, WS_TASK_PRIO, &s_ws_task) != pdPASS) {
        ESP_LOGE(TAG, "Creation de la tache WebSocket impossible");
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "Tunnel demarre (%d workers)", WORKER_COUNT);
    return ESP_OK;
}

// ============================================================================
// API publique
// ============================================================================

bool tunnel_security_locked(void)
{
    if (!s_inited) return false;
    lock();
    bool en = s_cfg.enable;
    unlock();
    return en && cfg_get_settings()->enable_secure_http;
}

void tunnel_on_security_changed(void)
{
    if (!s_inited) return;
    if (!tunnel_security_ok()) {
        // Comme handleSaveConfigHTTP de la LiXee-Box : sans securite, le
        // tunnel n'est plus autorise et il est desactive (pas seulement suspendu).
        lock();
        bool was = s_cfg.enable;
        s_cfg.enable = false;
        unlock();
        if (was) {
            ESP_LOGW(TAG, "Tunnel desactive : securite HTTP desactivee");
            save_config();
        }
    }
    wake_ws_task();   // arret immediat, ou reprise d'un tunnel suspendu
}

void tunnel_get_config(bool *enabled, char *client_id, size_t client_id_len, char *token, size_t token_len)
{
    if (!s_inited) {
        if (enabled) *enabled = false;
        if (client_id && client_id_len) client_id[0] = '\0';
        if (token && token_len) token[0] = '\0';
        return;
    }
    lock();
    if (enabled) *enabled = s_cfg.enable;
    if (client_id && client_id_len) strlcpy(client_id, s_cfg.client_id, client_id_len);
    if (token && token_len) strlcpy(token, s_cfg.token, token_len);
    unlock();
}

esp_err_t tunnel_set_config(bool enabled, const char *client_id, const char *token, uint32_t restart_delay_ms)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (client_id && !valid_cred(client_id, TUNNEL_CLIENT_ID_MAX)) return ESP_ERR_INVALID_ARG;
    if (token && !valid_cred(token, TUNNEL_TOKEN_MAX)) return ESP_ERR_INVALID_ARG;

    lock();
    bool changed = (s_cfg.enable != enabled);
    s_cfg.enable = enabled;
    if (client_id && strcmp(client_id, s_cfg.client_id) != 0) {
        strlcpy(s_cfg.client_id, client_id, sizeof(s_cfg.client_id));
        changed = true;
    }
    if (token && strcmp(token, s_cfg.token) != 0) {
        strlcpy(s_cfg.token, token, sizeof(s_cfg.token));
        changed = true;
    }
    unlock();

    esp_err_t err = save_config();
    if (changed) {
        ESP_LOGI(TAG, "Configuration modifiee : tunnel %s", enabled ? "active" : "desactive");
        // Rechargement a chaud (la LiXee-Box detruisait et recreait l'objet)
        request_restart(restart_delay_ms);
    }
    return err;
}

void tunnel_get_status(tunnel_status_t *o)
{
    memset(o, 0, sizeof(*o));
    if (!s_inited) return;
    const bool sec = tunnel_security_ok();
    const int64_t now = now_ms();
    lock();
    o->enabled   = s_cfg.enable;
    o->has_token = s_cfg.token[0] != '\0';
    strlcpy(o->client_id, s_cfg.client_id, sizeof(o->client_id));
    o->state = s_state;
    // La tache ne met son etat a jour qu'a son prochain passage : refleter
    // tout de suite une configuration qui vient de changer.
    if (!o->enabled || !o->has_token)  o->state = TUNNEL_STATE_DISABLED;
    else if (!sec)                     o->state = TUNNEL_STATE_SUSPENDED;
    else if (o->state == TUNNEL_STATE_DISABLED || o->state == TUNNEL_STATE_SUSPENDED) o->state = TUNNEL_STATE_CONNECTING;
    o->connected = (o->state == TUNNEL_STATE_CONNECTED);
    strlcpy(o->device_id, s_device_id, sizeof(o->device_id));
    strlcpy(o->subdomain, s_subdomain, sizeof(o->subdomain));
    strlcpy(o->last_error, s_last_error, sizeof(o->last_error));
    o->upgrade_status = s_upgrade_status;
    o->reconnects     = s_reconnects;
    if (o->connected && s_connected_at_ms > 0) o->connected_for_s = (uint32_t)((now - s_connected_at_ms) / 1000);
    if (o->state == TUNNEL_STATE_WAITING && s_retry_at_ms > now) {
        o->retry_in_s = (uint32_t)((s_retry_at_ms - now + 999) / 1000);
    }
    unlock();
    o->security_ok = sec;
    o->requests    = s_requests.load();
    o->in_flight   = s_in_flight.load();
    o->queued      = s_job_q ? (uint32_t)uxQueueMessagesWaiting(s_job_q) : 0;
}

tunnel_act_start_t tunnel_start_activation(const char *code)
{
    if (!code || strlen(code) != 6) return TUNNEL_ACT_START_BAD_CODE;
    for (int i = 0; i < 6; i++) {
        // Chiffres seulement : le code finit dans l'URL d'activation
        if (!isdigit((unsigned char)code[i])) return TUNNEL_ACT_START_BAD_CODE;
    }
    if (!tunnel_security_ok()) return TUNNEL_ACT_START_NO_SECURITY;
    if (!s_started || !s_job_q) return TUNNEL_ACT_START_FAILED;

    lock();
    if (s_act.status == TUNNEL_ACT_PROCESSING) { unlock(); return TUNNEL_ACT_START_BUSY; }
    memset(&s_act, 0, sizeof(s_act));
    s_act.status = TUNNEL_ACT_PROCESSING;
    unlock();

    job_t *j = job_new(JOB_ACTIVATE);
    const bool allocated = (j != NULL);
    if (j) strlcpy(j->code, code, sizeof(j->code));
    if (!allocated || xQueueSend(s_job_q, &j, 0) != pdTRUE) {
        job_free(j);
        lock();
        s_act.status = TUNNEL_ACT_IDLE;
        unlock();
        return allocated ? TUNNEL_ACT_START_BUSY : TUNNEL_ACT_START_FAILED;
    }
    ESP_LOGI(TAG, "Activation demandee (RAM interne %u)", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return TUNNEL_ACT_START_OK;
}

void tunnel_get_activation(tunnel_activation_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!s_inited) return;
    lock();
    *out = s_act;
    bool dirty = s_dirty;
    unlock();
    // Enregistrement apres activation en echec (tache ephemere non creee) :
    // cette fonction est appelee par le serveur HTTP (pile interne), on retente.
    if (dirty) save_config();
}

bool tunnel_send_notification(const char *title, const char *message, int type,
                              const char *alert_type, float value, float threshold)
{
    uint32_t session = s_connected_session.load();
    if (!s_started || session == 0) return false;

    char dev[TUNNEL_CLIENT_ID_MAX], sub[sizeof(s_subdomain)];
    lock();
    strlcpy(dev, s_device_id, sizeof(dev));
    strlcpy(sub, s_subdomain, sizeof(sub));
    unlock();

    // Horodatage au format FormattedDate de la LiXee-Box : "JJ/MM/AAAA HH:MM"
    char ts[24] = "";
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year >= 120) strftime(ts, sizeof(ts), "%d/%m/%Y %H:%M", &tm);

    cJSON *d = cJSON_CreateObject();
    if (!d) return false;
    cJSON_AddStringToObject(d, "type", "notification");
    cJSON_AddStringToObject(d, "deviceId", dev);
    cJSON_AddStringToObject(d, "subdomain", sub);
    cJSON_AddStringToObject(d, "title", title ? title : "");
    cJSON_AddStringToObject(d, "message", message ? message : "");
    cJSON_AddNumberToObject(d, "notifType", type);
    cJSON_AddStringToObject(d, "timestamp", ts);
    if (alert_type) cJSON_AddStringToObject(d, "alertType", alert_type);
    if (value != 0) cJSON_AddNumberToObject(d, "value", value);
    if (threshold != 0) cJSON_AddNumberToObject(d, "threshold", threshold);
    return out_push_cjson(d, session, 0);
}
