// ============================================================================
// Notifications - tache "notif" : alertes, email (SMTP) et WebPush
//
// Portage de mail.cpp et WebPush.cpp. Differences avec la LiXee-Box :
//   - mail.cpp n'envoyait qu'un message de test code en dur (IP 193.70.18.147,
//     port 465, sans verification du certificat, setInsecure()) et n'etait
//     appele par aucune notification. Ici : serveur, port et securite
//     configurables (SSL/TLS implicite ou STARTTLS), certificat verifie par le
//     magasin de certificats d'ESP-IDF, et chaque notification est envoyee
//     par email quand l'option est active (filtre de type, 30 mails/h maxi) ;
//   - WebPush() etait synchrone dans la tache Zigbee : un serveur WebPush
//     injoignable bloquait la reception des trames pendant le delai HTTP. Ici
//     les envois sont en file pour la tache "notif", avec une pause de 30 s
//     apres un echec.
//
// La pile de la tache est en PSRAM : la tache ne doit JAMAIS acceder a la
// flash (fichiers, NVS). Elle ne lit que la RAM (device_data, historique,
// configuration) ; les ecritures sont faites par notifications_tick().
// ============================================================================

#include "notifications.h"
#include "device_data.h"
#include "notif_internal.h"
#include "config_manager.h"
#include "ntp_sync.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "mbedtls/base64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static const char *TAG = "NOTIF_TX";

// ============================================================================
// Files et etat des envois
// ============================================================================

typedef enum {
    JOB_REPORT = 0,         // rapport d'attribut : alertes et/ou WebPush
    JOB_DAILY,              // controles de fin de journee
    JOB_WEBPUSH_TEST,
    JOB_MAIL,               // file des mails uniquement
    JOB_MAIL_TEST,
} job_kind_t;

typedef struct {
    uint8_t       kind;
    bool          alert;    // JOB_REPORT : a evaluer par les alertes
    bool          push;     // JOB_REPORT : a envoyer au serveur WebPush
    uint32_t      id;       // JOB_MAIL
    notif_event_t ev;
} job_t;

#define MAIN_QUEUE_LEN   48
#define MAIL_QUEUE_LEN   8
#define MAIL_PER_HOUR    30
#define WEBPUSH_BACKOFF_US (30LL * 1000000LL)

typedef struct {
    uint32_t sent, failed, dropped;
    uint32_t last_ts;       // epoch du dernier essai
    int      last_code;     // code HTTP / SMTP du dernier essai
    bool     has_last, last_ok;
    char     last_msg[96];
} send_stat_t;

static QueueHandle_t     s_q = NULL;
static QueueHandle_t     s_mail_q = NULL;
static TaskHandle_t      s_task = NULL;
static StaticSemaphore_t s_stat_mtx_buf;
static SemaphoreHandle_t s_stat_mtx = NULL;
static send_stat_t       s_mail_stat, s_push_stat;
static uint32_t          s_queue_dropped = 0;
static int64_t           s_push_pause_until = 0;
static int64_t           s_mail_hour_start = 0;
static int               s_mail_hour_count = 0;

static void stat_set(send_stat_t *st, bool ok, int code, const char *msg)
{
    xSemaphoreTake(s_stat_mtx, portMAX_DELAY);
    if (ok) st->sent++; else st->failed++;
    st->has_last = true;
    st->last_ok = ok;
    st->last_code = code;
    st->last_ts = ntp_sync_is_synced() ? (uint32_t)time(NULL) : 0;
    strlcpy(st->last_msg, msg ? msg : "", sizeof(st->last_msg));
    xSemaphoreGive(s_stat_mtx);
}

static void stat_drop(send_stat_t *st)
{
    if (!s_stat_mtx) return;
    xSemaphoreTake(s_stat_mtx, portMAX_DELAY);
    st->dropped++;
    xSemaphoreGive(s_stat_mtx);
}

// ============================================================================
// Reglages SMTP : copie coherente
//
// Correction : smtp_send() lisait les champs SMTP directement dans
// cfg_general_t pendant tout l'envoi (plusieurs secondes : connexion TLS,
// dialogue SMTP), pendant que la route web de configuration les reecrivait
// (strlcpy successifs) : serveur d'un reglage et mot de passe d'un autre,
// chaine lue en cours d'ecriture. L'ecrivain prend desormais ce verrou
// (notifications_smtp_cfg_lock) et l'envoi copie les champs dont il a besoin
// au debut, sous le meme verrou.
// ============================================================================

static StaticSemaphore_t s_smtp_cfg_mtx_buf;
static SemaphoreHandle_t s_smtp_cfg_mtx = NULL;

static SemaphoreHandle_t smtp_cfg_mtx(void)
{
    // Cree par notif_worker_start() au demarrage, avant le serveur web ; a
    // defaut, a la premiere utilisation (statique : pas d'allocation)
    if (!s_smtp_cfg_mtx) s_smtp_cfg_mtx = xSemaphoreCreateMutexStatic(&s_smtp_cfg_mtx_buf);
    return s_smtp_cfg_mtx;
}

void notifications_smtp_cfg_lock(void)   { xSemaphoreTake(smtp_cfg_mtx(), portMAX_DELAY); }
void notifications_smtp_cfg_unlock(void) { xSemaphoreGive(smtp_cfg_mtx()); }

typedef struct {
    char serv[sizeof(((cfg_general_t *)0)->serv_smtp)];
    char port[sizeof(((cfg_general_t *)0)->port_smtp)];
    char user[sizeof(((cfg_general_t *)0)->user_smtp)];
    char pass[sizeof(((cfg_general_t *)0)->pass_smtp)];
    char to[sizeof(((cfg_general_t *)0)->to_smtp)];
    char from[sizeof(((cfg_general_t *)0)->from_smtp)];
    int  security;
} smtp_cfg_t;

static void smtp_cfg_copy(smtp_cfg_t *c)
{
    const cfg_general_t *g = cfg_get_general();
    notifications_smtp_cfg_lock();
    strlcpy(c->serv, g->serv_smtp, sizeof(c->serv));
    strlcpy(c->port, g->port_smtp, sizeof(c->port));
    strlcpy(c->user, g->user_smtp, sizeof(c->user));
    strlcpy(c->pass, g->pass_smtp, sizeof(c->pass));
    strlcpy(c->to, g->to_smtp, sizeof(c->to));
    strlcpy(c->from, g->from_smtp, sizeof(c->from));
    c->security = g->security_smtp;
    notifications_smtp_cfg_unlock();
}

static bool mail_enabled(void)
{
    const cfg_settings_t *s = cfg_get_settings();
    const cfg_general_t *g = cfg_get_general();
    return s->enable_notif && g->serv_smtp[0];
}

static bool webpush_enabled(void)
{
    return cfg_get_settings()->enable_webpush && cfg_get_general()->serv_webpush[0];
}

// ============================================================================
// Base64 (authentification SMTP / HTTP, corps de mail)
// ============================================================================

/** Encode en base64 dans un tampon PSRAM a liberer par free(). NULL si echec. */
static char *b64_alloc(const void *src, size_t len)
{
    size_t need = 4 * ((len + 2) / 3) + 1;
    char *out = (char *)notif_psram_alloc(need);
    if (!out) return NULL;
    size_t olen = 0;
    if (mbedtls_base64_encode((unsigned char *)out, need, &olen,
                              (const unsigned char *)src, len) != 0) {
        free(out);
        return NULL;
    }
    out[olen] = '\0';
    return out;
}

// ============================================================================
// WebPush (esp_http_client, connexion conservee entre deux envois)
// ============================================================================

static esp_http_client_handle_t s_http = NULL;
static char s_http_url[sizeof(((cfg_general_t *)0)->serv_webpush)];

static void http_reset(void)
{
    if (s_http) {
        esp_http_client_cleanup(s_http);
        s_http = NULL;
    }
    s_http_url[0] = '\0';
}

/** POST d'un corps JSON au serveur WebPush. Renvoie le code HTTP, < 0 si erreur reseau. */
static int webpush_post(const char *body, char *err, size_t errlen)
{
    const cfg_general_t *g = cfg_get_general();
    err[0] = '\0';
    if (strncmp(g->serv_webpush, "http://", 7) != 0 && strncmp(g->serv_webpush, "https://", 8) != 0) {
        strlcpy(err, "URL invalide (http:// ou https:// attendu)", errlen);
        return -1;
    }
    if (!s_http || strcmp(s_http_url, g->serv_webpush) != 0) {
        http_reset();
        esp_http_client_config_t c = {};
        c.url = g->serv_webpush;
        c.method = HTTP_METHOD_POST;
        c.timeout_ms = 5000;
        c.crt_bundle_attach = esp_crt_bundle_attach;
        c.keep_alive_enable = true;
        s_http = esp_http_client_init(&c);
        if (!s_http) {
            strlcpy(err, "Client HTTP non cree (memoire)", errlen);
            return -1;
        }
        strlcpy(s_http_url, g->serv_webpush, sizeof(s_http_url));
    }

    esp_http_client_set_method(s_http, HTTP_METHOD_POST);
    esp_http_client_set_header(s_http, "Content-Type", "application/json");
    if (g->webpush_auth) {
        char cred[sizeof(g->user_webpush) + sizeof(g->pass_webpush) + 2];
        int n = snprintf(cred, sizeof(cred), "%s:%s", g->user_webpush, g->pass_webpush);
        char *b64 = b64_alloc(cred, n > 0 ? (size_t)n : 0);
        if (b64) {
            char hdr[200];
            snprintf(hdr, sizeof(hdr), "Basic %s", b64);
            esp_http_client_set_header(s_http, "Authorization", hdr);
            free(b64);
        }
    } else {
        esp_http_client_delete_header(s_http, "Authorization");
    }
    esp_http_client_set_post_field(s_http, body, (int)strlen(body));
    esp_err_t e = esp_http_client_perform(s_http);
    if (e != ESP_OK) {
        snprintf(err, errlen, "Erreur reseau : %s", esp_err_to_name(e));
        http_reset();                   // repartir d'une connexion neuve
        return -1;
    }
    int code = esp_http_client_get_status_code(s_http);
    if (code < 200 || code >= 300) snprintf(err, errlen, "Reponse HTTP %d", code);
    else strlcpy(err, "OK", errlen);
    return code;
}

static void webpush_send(const char *ieee, const char *cluster, const char *attr, const char *value,
                         bool record_stat_ok)
{
    nbuf_t b;
    nbuf_init(&b, 256);
    nbuf_str(&b, "{\"IEEE\":");      nbuf_json_str(&b, ieee);
    nbuf_str(&b, ",\"cluster\":");   nbuf_json_str(&b, cluster);
    nbuf_str(&b, ",\"attribute\":"); nbuf_json_str(&b, attr);
    nbuf_str(&b, ",\"value\":");     nbuf_json_str(&b, value);
    nbuf_str(&b, "}");
    char *json = nbuf_finish(&b);
    if (!json) { stat_drop(&s_push_stat); return; }

    char err[96];
    int code = webpush_post(json, err, sizeof(err));
    free(json);
    bool ok = code >= 200 && code < 300;
    if (!ok) {
        s_push_pause_until = esp_timer_get_time() + WEBPUSH_BACKOFF_US;
        ESP_LOGW(TAG, "WebPush : %s (pause 30 s)", err);
    }
    // Les succes ordinaires ne mettent a jour que le compteur, pas le message
    if (ok && !record_stat_ok) {
        xSemaphoreTake(s_stat_mtx, portMAX_DELAY);
        s_push_stat.sent++;
        xSemaphoreGive(s_stat_mtx);
    } else {
        stat_set(&s_push_stat, ok, code, err);
    }
}

static void webpush_report(const notif_event_t *ev)
{
    if (esp_timer_get_time() < s_push_pause_until) { stat_drop(&s_push_stat); return; }
    char ieee[20], cl[12], at[12], val[48];
    snprintf(ieee, sizeof(ieee), "%016llX", (unsigned long long)ev->ieee);
    snprintf(cl, sizeof(cl), "%u", (unsigned)ev->cluster);
    // Ligne propre a un endpoint : "<attribut>_ep<N>" (epAttrLabel de la Box)
    uint8_t kep = 0;
    uint16_t zat = device_data_resolve_key(ev->ieee, ev->cluster, ev->attr, &kep);
    if (kep) snprintf(at, sizeof(at), "%u_ep%u", (unsigned)zat, (unsigned)kep);
    else snprintf(at, sizeof(at), "%u", (unsigned)ev->attr);
    if (ev->is_str) strlcpy(val, ev->str, sizeof(val));
    else snprintf(val, sizeof(val), "%lld", (long long)ev->num);
    webpush_send(ieee, cl, at, val, false);
}

// ============================================================================
// SMTP (esp-tls : SSL/TLS implicite, ou TCP clair puis STARTTLS)
// ============================================================================

#define SMTP_RX_LEN    512
#define SMTP_RESP_LEN  1024
#define SMTP_LINE_LEN  600

typedef struct {
    esp_tls_t *tls;
    char      *rx;          // octets recus non consommes
    size_t     rx_len;
    char      *resp;        // texte cumule de la derniere reponse (capacites EHLO)
    char      *line;        // commande a envoyer
    char       err[96];
} smtp_t;

// Delai global d'une lecture / ecriture : la socket a deja un delai de 10 s
// par appel (timeout_ms) ; WANT_READ revient aussi apres un ticket de session
// TLS 1.3, d'ou une echeance plutot qu'un nombre d'essais.
#define SMTP_IO_DEADLINE_US (15LL * 1000000LL)

static bool smtp_write(smtp_t *s, const char *data, size_t len)
{
    size_t done = 0;
    int64_t deadline = esp_timer_get_time() + SMTP_IO_DEADLINE_US;
    while (done < len) {
        ssize_t r = esp_tls_conn_write(s->tls, data + done, len - done);
        if (r > 0) { done += (size_t)r; continue; }
        if ((r == ESP_TLS_ERR_SSL_WANT_WRITE || r == ESP_TLS_ERR_SSL_WANT_READ) &&
            esp_timer_get_time() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        snprintf(s->err, sizeof(s->err), "Envoi SMTP impossible (%d)", (int)r);
        return false;
    }
    return true;
}

/** Lit une ligne (sans CRLF) dans out. false si connexion fermee ou delai depasse. */
static bool smtp_read_line(smtp_t *s, char *out, size_t outlen)
{
    int64_t deadline = esp_timer_get_time() + SMTP_IO_DEADLINE_US;
    for (;;) {
        char *nl = (char *)memchr(s->rx, '\n', s->rx_len);
        if (nl) {
            size_t n = (size_t)(nl - s->rx);
            size_t c = n < outlen - 1 ? n : outlen - 1;
            memcpy(out, s->rx, c);
            out[c] = '\0';
            if (c > 0 && out[c - 1] == '\r') out[c - 1] = '\0';
            memmove(s->rx, nl + 1, s->rx_len - n - 1);
            s->rx_len -= n + 1;
            return true;
        }
        if (s->rx_len >= SMTP_RX_LEN - 1) s->rx_len = 0;   // ligne demesuree : ignoree
        ssize_t r = esp_tls_conn_read(s->tls, s->rx + s->rx_len, SMTP_RX_LEN - 1 - s->rx_len);
        if (r > 0) { s->rx_len += (size_t)r; continue; }
        if ((r == ESP_TLS_ERR_SSL_WANT_READ || r == ESP_TLS_ERR_SSL_WANT_WRITE) &&
            esp_timer_get_time() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        strlcpy(s->err, r == 0 ? "Connexion fermee par le serveur" : "Pas de reponse du serveur SMTP",
                sizeof(s->err));
        return false;
    }
}

/** Lit une reponse (multi-lignes "250-...") ; renvoie le code, -1 si erreur. */
static int smtp_reply(smtp_t *s)
{
    char ln[256];
    size_t rl = 0;
    s->resp[0] = '\0';
    for (int lines = 0; lines < 64; lines++) {
        if (!smtp_read_line(s, ln, sizeof(ln))) return -1;
        size_t n = strlen(ln);
        if (rl + n + 2 < SMTP_RESP_LEN) {
            memcpy(s->resp + rl, ln, n);
            rl += n;
            s->resp[rl++] = '\n';
            s->resp[rl] = '\0';
        }
        if (n < 3) continue;
        if (n == 3 || ln[3] == ' ') return atoi(ln);
    }
    strlcpy(s->err, "Reponse SMTP trop longue", sizeof(s->err));
    return -1;
}

/** Envoie une commande et verifie le code attendu (expect2 : code alternatif, 0 si aucun). */
static bool smtp_cmd(smtp_t *s, int expect, int expect2, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
static bool smtp_cmd(smtp_t *s, int expect, int expect2, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(s->line, SMTP_LINE_LEN - 2, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= SMTP_LINE_LEN - 2) {
        strlcpy(s->err, "Commande SMTP trop longue", sizeof(s->err));
        return false;
    }
    s->line[n++] = '\r';
    s->line[n++] = '\n';
    if (!smtp_write(s, s->line, (size_t)n)) return false;
    int code = smtp_reply(s);
    if (code == expect || (expect2 && code == expect2)) return true;
    if (code > 0) {
        // Premiere ligne de la reponse, pour l'affichage
        char first[64];
        strlcpy(first, s->resp, sizeof(first));
        char *nl = strchr(first, '\n');
        if (nl) *nl = '\0';
        // Ne jamais recopier la commande (elle peut contenir un identifiant encode)
        snprintf(s->err, sizeof(s->err), "Refus du serveur : %.60s", first);
    }
    return false;
}

/** Capacite annoncee dans la reponse EHLO (ligne "250-NOM" ou "250 NOM ..."). */
static bool ehlo_has(const char *resp, const char *cap)
{
    size_t cl = strlen(cap);
    for (const char *p = resp; *p; ) {
        if (strlen(p) > 4 && strncasecmp(p + 4, cap, cl) == 0) return true;
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    return false;
}

static bool ehlo_auth_has(const char *resp, const char *mech)
{
    for (const char *p = resp; *p; ) {
        if (strlen(p) > 9 && strncasecmp(p + 4, "AUTH", 4) == 0) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            char tmp[160];
            strlcpy(tmp, p, len + 1 < sizeof(tmp) ? len + 1 : sizeof(tmp));
            for (char *t = tmp; *t; t++) if (*t >= 'a' && *t <= 'z') *t = (char)(*t - 32);
            if (strstr(tmp, mech)) return true;
        }
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    return false;
}

/** Ajoute un texte au corps en convertissant les fins de ligne en CRLF. */
static void add_crlf(nbuf_t *b, const char *s)
{
    for (; s && *s; s++) {
        if (*s == '\r') continue;
        if (*s == '\n') nbuf_add(b, "\r\n", 2);
        else nbuf_add(b, s, 1);
    }
}

/**
 * Envoie un email. Toutes les ressources sont en PSRAM (tampons, et contexte
 * mbedTLS grace a CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC).
 * @return true si le serveur a accepte le message ; err rempli sinon
 */
static bool smtp_send_cfg(const smtp_cfg_t *g, const char *subject, const char *text,
                          char *err, size_t errlen, int *code_out);

static bool smtp_send(const char *subject, const char *text, char *err, size_t errlen, int *code_out)
{
    *code_out = 0;
    // Copie des reglages en debut d'envoi (voir smtp_cfg_copy), dans le tas :
    // ~330 octets, mot de passe compris, efface avant liberation
    smtp_cfg_t *c = (smtp_cfg_t *)notif_psram_alloc(sizeof(smtp_cfg_t));
    if (!c) {
        strlcpy(err, "Memoire insuffisante", errlen);
        return false;
    }
    smtp_cfg_copy(c);
    bool ok = smtp_send_cfg(c, subject, text, err, errlen, code_out);
    memset(c, 0, sizeof(*c));
    free(c);
    return ok;
}

static bool smtp_send_cfg(const smtp_cfg_t *g, const char *subject, const char *text,
                          char *err, size_t errlen, int *code_out)
{
    const char *to = g->to[0] ? g->to : g->user;
    const char *from = g->from[0] ? g->from : (strchr(g->user, '@') ? g->user : to);
    if (!g->serv[0] || !strchr(to, '@') || !strchr(from, '@')) {
        strlcpy(err, "Serveur, destinataire ou expediteur non renseigne", errlen);
        return false;
    }
    // Garde-fou : pas de retour a la ligne dans les adresses (injection d'en-tetes)
    if (strpbrk(to, "\r\n<>") || strpbrk(from, "\r\n<>")) {
        strlcpy(err, "Adresse email invalide", errlen);
        return false;
    }

    int sec = g->security;
    int port = atoi(g->port);
    if (port <= 0 || port > 65535) port = (sec == 1) ? 465 : 587;
    bool implicit_tls = (sec == 1) || (sec == 0 && port == 465);
    bool want_starttls = !implicit_tls && sec != 3;

    smtp_t s = {};
    s.rx = (char *)notif_psram_alloc(SMTP_RX_LEN);
    s.resp = (char *)notif_psram_alloc(SMTP_RESP_LEN);
    s.line = (char *)notif_psram_alloc(SMTP_LINE_LEN);
    s.tls = esp_tls_init();
    bool ok = false;
    nbuf_t body;
    body.buf = NULL;
    char *b64 = NULL;

    esp_tls_cfg_t tls_cfg = {};
    tls_cfg.timeout_ms = 10000;
    tls_cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_tls_cfg_t plain_cfg = {};
    plain_cfg.timeout_ms = 10000;
    plain_cfg.is_plain_tcp = true;
    int hostlen = (int)strlen(g->serv);

    if (!s.rx || !s.resp || !s.line || !s.tls) {
        strlcpy(s.err, "Memoire insuffisante", sizeof(s.err));
        goto done;
    }
    s.resp[0] = '\0';

    if (esp_tls_conn_new_sync(g->serv, hostlen, port, implicit_tls ? &tls_cfg : &plain_cfg, s.tls) != 1) {
        snprintf(s.err, sizeof(s.err), "Connexion a %.40s:%d impossible%s", g->serv, port,
                 implicit_tls ? " (TLS)" : "");
        goto done;
    }
    if (smtp_reply(&s) != 220) {
        if (!s.err[0]) strlcpy(s.err, "Accueil SMTP inattendu", sizeof(s.err));
        goto done;
    }
    if (!smtp_cmd(&s, 250, 0, "EHLO lixee-matterbridge")) goto done;

    if (want_starttls) {
        if (!ehlo_has(s.resp, "STARTTLS")) {
            strlcpy(s.err, sec == 2 || sec == 0 ? "STARTTLS non propose par le serveur (choisir SSL/TLS ou Aucune)"
                                                : "STARTTLS non propose", sizeof(s.err));
            goto done;
        }
        if (!smtp_cmd(&s, 220, 0, "STARTTLS")) goto done;
        // Bascule TLS sur la meme socket : l'etat CONNECTING fait faire a
        // esp-tls la poignee de main sans rouvrir de connexion
        s.rx_len = 0;
        esp_tls_set_conn_state(s.tls, ESP_TLS_CONNECTING);
        if (esp_tls_conn_new_sync(g->serv, hostlen, port, &tls_cfg, s.tls) != 1) {
            strlcpy(s.err, "Echec de la negociation TLS (STARTTLS)", sizeof(s.err));
            goto done;
        }
        if (!smtp_cmd(&s, 250, 0, "EHLO lixee-matterbridge")) goto done;
    }

    if (g->user[0]) {
        if (ehlo_auth_has(s.resp, "PLAIN") || !ehlo_auth_has(s.resp, "LOGIN")) {
            // AUTH PLAIN : base64("\0user\0pass")
            char cred[sizeof(g->user) + sizeof(g->pass) + 2];
            size_t ul = strlen(g->user), pl = strlen(g->pass);
            cred[0] = '\0';
            memcpy(cred + 1, g->user, ul);
            cred[1 + ul] = '\0';
            memcpy(cred + 2 + ul, g->pass, pl);
            char *a = b64_alloc(cred, 2 + ul + pl);
            memset(cred, 0, sizeof(cred));
            bool auth = a && smtp_cmd(&s, 235, 0, "AUTH PLAIN %s", a);
            free(a);
            if (!auth) { if (!s.err[0]) strlcpy(s.err, "Authentification refusee", sizeof(s.err)); goto done; }
        } else {
            // AUTH LOGIN, comme la LiXee-Box
            if (!smtp_cmd(&s, 334, 0, "AUTH LOGIN")) goto done;
            char *u = b64_alloc(g->user, strlen(g->user));
            bool auth = u && smtp_cmd(&s, 334, 0, "%s", u);
            free(u);
            if (auth) {
                char *p = b64_alloc(g->pass, strlen(g->pass));
                auth = p && smtp_cmd(&s, 235, 0, "%s", p);
                free(p);
            }
            if (!auth) { strlcpy(s.err, "Authentification refusee", sizeof(s.err)); goto done; }
        }
    }

    if (!smtp_cmd(&s, 250, 0, "MAIL FROM:<%s>", from)) goto done;
    if (!smtp_cmd(&s, 250, 251, "RCPT TO:<%s>", to)) goto done;
    if (!smtp_cmd(&s, 354, 0, "DATA")) goto done;

    {
        // En-tetes : sujet en mot encode UTF-8, corps en base64 (accents et
        // emojis des titres, et aucune ligne commencant par '.' a echapper)
        char *subj = b64_alloc(subject, strlen(subject));
        nbuf_init(&body, 1024);
        time_t now = time(NULL);
        struct tm gm;
        gmtime_r(&now, &gm);
        char date[48];
        strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S +0000", &gm);
        nbuf_printf(&body, "Date: %s\r\nFrom: LiXee MatterBridge <%s>\r\nTo: <%s>\r\n", date, from, to);
        nbuf_printf(&body, "Subject: =?UTF-8?B?%s?=\r\n", subj ? subj : "");
        nbuf_printf(&body, "Message-ID: <%lld.%lu@lixee-matterbridge>\r\n",
                    (long long)now, (unsigned long)(esp_timer_get_time() & 0xFFFFFF));
        nbuf_str(&body, "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\n"
                        "Content-Transfer-Encoding: base64\r\n\r\n");
        free(subj);

        nbuf_t txt;
        nbuf_init(&txt, 512);
        add_crlf(&txt, text);
        b64 = b64_alloc(txt.buf ? txt.buf : "", txt.len);
        nbuf_free(&txt);
        if (!b64 || body.oom) { strlcpy(s.err, "Memoire insuffisante", sizeof(s.err)); goto done; }
        for (size_t off = 0, n = strlen(b64); off < n; off += 76) {
            size_t c = n - off < 76 ? n - off : 76;
            nbuf_add(&body, b64 + off, c);
            nbuf_add(&body, "\r\n", 2);
        }
        if (body.oom) { strlcpy(s.err, "Memoire insuffisante", sizeof(s.err)); goto done; }
        if (!smtp_write(&s, body.buf, body.len)) goto done;
        if (!smtp_cmd(&s, 250, 0, ".")) goto done;
    }
    ok = true;
    smtp_cmd(&s, 221, 0, "QUIT");       // reponse sans importance

done:
    *code_out = s.resp && s.resp[0] ? atoi(s.resp) : 0;
    if (ok) strlcpy(err, "Message accepte par le serveur", errlen);
    else strlcpy(err, s.err[0] ? s.err : "Echec SMTP", errlen);
    if (s.tls) esp_tls_conn_destroy(s.tls);
    nbuf_free(&body);
    free(b64);
    free(s.rx);
    free(s.resp);
    free(s.line);
    return ok;
}

static void mail_notification(uint32_t id, bool test)
{
    char title[NOTIF_TITLE_LEN], msg[NOTIF_MSG_LEN], stamp[20];
    int type = 0;
    if (test) {
        strlcpy(title, "Test de notification", sizeof(title));
        strlcpy(msg, "Ceci est un email de test envoy\u00E9 par la LiXee MatterBridge.", sizeof(msg));
        notif_format_stamp(stamp, sizeof(stamp));
    } else if (!notif_store_get(id, title, sizeof(title), msg, sizeof(msg), stamp, sizeof(stamp), &type)) {
        return;                         // supprimee avant l'envoi
    }

    // Limite de debit : une alerte qui boucle ne doit pas inonder la boite
    int64_t now = esp_timer_get_time();
    if (now - s_mail_hour_start > 3600LL * 1000000LL) {
        s_mail_hour_start = now;
        s_mail_hour_count = 0;
    }
    if (!test && s_mail_hour_count >= MAIL_PER_HOUR) {
        stat_drop(&s_mail_stat);
        return;
    }
    s_mail_hour_count++;

    static const char *const TYPE_LBL[4] = { "Info", "Attention", "Erreur", "Critique" };
    char subject[NOTIF_TITLE_LEN + 32];
    snprintf(subject, sizeof(subject), "[LiXee] %s", title);
    nbuf_t t;
    nbuf_init(&t, 768);
    nbuf_printf(&t, "%s\n\n%s\n\n", title, msg);
    nbuf_printf(&t, "Type : %s\n", TYPE_LBL[type & 3]);
    if (stamp[0]) nbuf_printf(&t, "Date : %s\n", stamp);
    nbuf_str(&t, "\n-- \nLiXee MatterBridge\n");
    char *text = nbuf_finish(&t);
    if (!text) { stat_drop(&s_mail_stat); return; }

    char err[96];
    int code = 0;
    bool ok = smtp_send(subject, text, err, sizeof(err), &code);
    free(text);
    if (!ok) ESP_LOGW(TAG, "Email non envoye : %s", err);
    else ESP_LOGI(TAG, "Email envoye : %s", title);
    stat_set(&s_mail_stat, ok, code, err);
}

// ============================================================================
// Tache
// ============================================================================

static void handle_job(const job_t *j)
{
    switch (j->kind) {
    case JOB_REPORT:
        if (j->alert) notif_alerts_on_event(&j->ev);
        if (j->push && webpush_enabled()) webpush_report(&j->ev);
        break;
    case JOB_DAILY:
        notif_alerts_daily();
        break;
    case JOB_WEBPUSH_TEST:
        s_push_pause_until = 0;
        webpush_send("TEST", "0", "0", "test", true);
        break;
    case JOB_MAIL:
        if (mail_enabled()) mail_notification(j->id, false);
        break;
    case JOB_MAIL_TEST:
        mail_notification(0, true);
        break;
    default:
        break;
    }
}

static void notif_task(void *arg)
{
    (void)arg;
    int64_t last_periodic = 0;
    job_t j;
    for (;;) {
        if (xQueueReceive(s_q, &j, pdMS_TO_TICKS(500)) == pdTRUE) handle_job(&j);
        if (xQueueReceive(s_mail_q, &j, 0) == pdTRUE) handle_job(&j);
        int64_t now = esp_timer_get_time();
        if (now - last_periodic >= 10LL * 1000000LL) {
            last_periodic = now;
            notif_alerts_periodic();
        }
    }
}

esp_err_t notif_worker_start(void)
{
    if (s_task) return ESP_OK;
    s_stat_mtx = xSemaphoreCreateMutexStatic(&s_stat_mtx_buf);
    smtp_cfg_mtx();                     // verrou des reglages SMTP, avant le serveur web
    s_q = xQueueCreateWithCaps(MAIN_QUEUE_LEN, sizeof(job_t), MALLOC_CAP_SPIRAM);
    if (!s_q) s_q = xQueueCreate(MAIN_QUEUE_LEN, sizeof(job_t));
    s_mail_q = xQueueCreateWithCaps(MAIL_QUEUE_LEN, sizeof(job_t), MALLOC_CAP_SPIRAM);
    if (!s_mail_q) s_mail_q = xQueueCreate(MAIL_QUEUE_LEN, sizeof(job_t));
    if (!s_q || !s_mail_q) return ESP_ERR_NO_MEM;

    // 12 Ko de pile : poignee de main TLS (SMTP / WebPush https). En PSRAM car
    // la tache ne touche jamais a la flash ; repli en RAM interne reduit.
    if (xTaskCreatePinnedToCoreWithCaps(notif_task, "notif", 12288, NULL, 3, &s_task,
                                        0, MALLOC_CAP_SPIRAM) != pdPASS) {
        s_task = NULL;
        if (xTaskCreatePinnedToCore(notif_task, "notif", 8192, NULL, 3, &s_task, 0) != pdPASS) {
            s_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

void notif_worker_queue_mail(uint32_t id, int type)
{
    if (!s_mail_q || !mail_enabled()) return;
    if (type < cfg_get_general()->min_type_smtp) return;
    job_t j = {};
    j.kind = JOB_MAIL;
    j.id = id;
    if (xQueueSend(s_mail_q, &j, 0) != pdTRUE) stat_drop(&s_mail_stat);
}

void notif_worker_queue_daily(void)
{
    if (!s_q) return;
    job_t j = {};
    j.kind = JOB_DAILY;
    xQueueSend(s_q, &j, 0);
}

void notifications_daily_hook(void)
{
    notif_worker_queue_daily();
}

void notifications_on_report(uint64_t ieee, uint16_t cluster, uint16_t attr,
                             bool is_str, int64_t num, const char *str)
{
    if (!s_q) return;
    bool alert = notif_alerts_wants(ieee, cluster);
    bool push = webpush_enabled() && esp_timer_get_time() >= s_push_pause_until;
    if (!alert && !push) return;

    job_t j = {};
    j.kind = JOB_REPORT;
    j.alert = alert;
    j.push = push;
    j.ev.ieee = ieee;
    j.ev.cluster = cluster;
    j.ev.attr = attr;
    j.ev.is_str = is_str;
    j.ev.num = num;
    if (is_str && str) {
        // Texte affichable uniquement (les chaines ZCL peuvent contenir des
        // octets de bourrage), espaces de debut et de fin retires
        size_t n = 0;
        for (const char *p = str; *p && n + 1 < sizeof(j.ev.str); p++) {
            unsigned char c = (unsigned char)*p;
            if (c >= 32 && c < 127 && !(n == 0 && c == ' ')) j.ev.str[n++] = (char)c;
        }
        while (n > 0 && j.ev.str[n - 1] == ' ') n--;
        j.ev.str[n] = '\0';
    }
    if (xQueueSend(s_q, &j, 0) != pdTRUE) s_queue_dropped++;
}

esp_err_t notifications_test_mail(void)
{
    if (!s_mail_q) return ESP_ERR_INVALID_STATE;
    if (!cfg_get_general()->serv_smtp[0]) return ESP_ERR_INVALID_ARG;
    job_t j = {};
    j.kind = JOB_MAIL_TEST;
    return xQueueSend(s_mail_q, &j, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t notifications_test_webpush(void)
{
    if (!s_q) return ESP_ERR_INVALID_STATE;
    if (!cfg_get_general()->serv_webpush[0]) return ESP_ERR_INVALID_ARG;
    job_t j = {};
    j.kind = JOB_WEBPUSH_TEST;
    return xQueueSend(s_q, &j, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}

// ============================================================================
// Etat des envois
// ============================================================================

static void stat_json(nbuf_t *b, const char *name, const send_stat_t *st, bool enabled, bool configured)
{
    nbuf_printf(b, "\"%s\":{\"enabled\":%s,\"configured\":%s,\"sent\":%lu,\"failed\":%lu,\"dropped\":%lu,",
                name, enabled ? "true" : "false", configured ? "true" : "false",
                (unsigned long)st->sent, (unsigned long)st->failed, (unsigned long)st->dropped);
    if (!st->has_last) {
        nbuf_str(b, "\"last\":null}");
        return;
    }
    char when[24] = "";
    if (st->last_ts) {
        time_t t = (time_t)st->last_ts;
        struct tm lt;
        localtime_r(&t, &lt);
        strftime(when, sizeof(when), "%d/%m/%Y %H:%M:%S", &lt);
    }
    nbuf_printf(b, "\"last\":{\"ok\":%s,\"code\":%d,\"time\":", st->last_ok ? "true" : "false", st->last_code);
    nbuf_json_str(b, when);
    nbuf_str(b, ",\"message\":");
    nbuf_json_str(b, st->last_msg);
    nbuf_str(b, "}}");
}

char *notifications_send_status_json(void)
{
    if (!s_stat_mtx) return NULL;
    send_stat_t m, p;
    xSemaphoreTake(s_stat_mtx, portMAX_DELAY);
    m = s_mail_stat;
    p = s_push_stat;
    xSemaphoreGive(s_stat_mtx);
    const cfg_general_t *g = cfg_get_general();
    const cfg_settings_t *s = cfg_get_settings();
    nbuf_t b;
    nbuf_init(&b, 640);
    nbuf_str(&b, "{");
    stat_json(&b, "mail", &m, s->enable_notif, g->serv_smtp[0] != '\0');
    nbuf_str(&b, ",");
    stat_json(&b, "webpush", &p, s->enable_webpush, g->serv_webpush[0] != '\0');
    int64_t pause = s_push_pause_until - esp_timer_get_time();
    nbuf_printf(&b, ",\"webpushPausedS\":%d,\"queue\":{\"waiting\":%u,\"mailWaiting\":%u,\"dropped\":%lu}}",
                pause > 0 ? (int)(pause / 1000000) : 0,
                s_q ? (unsigned)uxQueueMessagesWaiting(s_q) : 0u,
                s_mail_q ? (unsigned)uxQueueMessagesWaiting(s_mail_q) : 0u,
                (unsigned long)s_queue_dropped);
    return nbuf_finish(&b);
}
