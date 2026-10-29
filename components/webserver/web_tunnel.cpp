// ============================================================================
// Routes de la page Tunnel (acces distant) - voir web_tunnel.h
// La logique est dans components/tunnel ; ce fichier lit les parametres,
// applique les regles de la LiXee-Box (handleSaveConfigTunnel, /api/tunnel/*)
// et met en forme les reponses.
//
// Contrat (toutes les routes passent par web_server_register_uri : session ou
// Basic, CSRF) :
//
// GET /api/tunnel/status  (alias GET /api/tunnelStatus)
//   {"enabled":bool,"connected":bool,"url":"https://<sous-domaine>" (si connecte),
//    "state":"disabled|suspended|connecting|connected|waiting",
//    "securityOk":bool,"securityLocked":bool,"viaTunnel":bool,
//    "clientId":"..","hasToken":bool,"deviceId":"..","subdomain":"..",
//    "lastError":"..","upgradeStatus":n,"connectedFor":s,"retryIn":s,
//    "reconnects":n,"requests":n,"queued":n,"inFlight":n,
//    "activation":{"status":"idle|processing|done","success":bool,"deviceId":"..","error":".."}}
//   Sur-ensemble de la reponse LiXee-Box {enabled, connected, url}.
//
// POST /api/tunnel/config   (JSON ; remplace le formulaire /saveConfigTunnel)
//   {"enableTunnel":bool,"tunnelClientId":"..","tunnelToken":"..","credsOnly":bool}
//   credsOnly : enregistre les identifiants sans toucher a l'interrupteur.
//   tunnelToken absent, vide ou "********" : jeton inchange.
//   200 {"status":"ok"} ; erreurs {"status":"error","code":n,"message":".."} avec
//   les codes de la LiXee-Box : 1 Client ID requis (400), 2 Token requis (400),
//   4 desactivation depuis le tunnel (403), 8 securite HTTP requise (403),
//   16 caracteres invalides (400, nouveau), 0 JSON invalide (400).
//
// POST /api/tunnel/activate  {"code":"123456"}
//   200 {"status":"pending"|"processing"} ou {"status":"error","error":"..","message":".."}
//   (GET /api/tunnelActivate?code= de la LiXee-Box non repris : il modifiait l'etat)
//
// GET /api/tunnel/activate/status  (alias GET /api/tunnelActivateStatus)
//   {"status":"idle"} | {"status":"processing"} |
//   {"status":"done","success":true,"deviceId":".."} | {"status":"done","success":false,"error":".."}
//   Le resultat reste disponible jusqu'a l'activation suivante (la LiXee-Box
//   l'effacait a la premiere lecture : un GET qui modifiait l'etat).
//
// GET /api/tunnel/credentials   {"tunnelClientId":"..","tunnelToken":".."}
//   403 si la requete arrive par le tunnel (le jeton ne transite pas sur Internet).
// POST /api/tunnel/credentials  {"tunnelClientId"?:"..","tunnelToken"?:".."}
//   200 {"status":"ok"} ; redemarre le tunnel s'il est actif.
// ============================================================================

#include "web_tunnel.h"
#include "web_server.h"
#include "tunnel.h"

#include "esp_log.h"
#include "cJSON.h"
#include "sdkconfig.h"
#include "lwip/sockets.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "WEB_TUN";

// Anciens noms de la LiXee-Box (serverWeb.rewrite) : 2 entrees de la table
// des handlers (max_uri_handlers), a 0 pour les economiser.
#ifndef WEB_TUNNEL_LEGACY_ALIASES
#define WEB_TUNNEL_LEGACY_ALIASES 1
#endif

#define MASKED_TOKEN "********"

// Delai avant de couper la connexion quand la requete arrive par le tunnel :
// sa reponse doit partir avant (la LiXee-Box attendait 2 s pour l'activation).
#define RESTART_DELAY_VIA_TUNNEL_MS 2000

// ----------------------------------------------------------------------------
// Outils
// ----------------------------------------------------------------------------

bool web_tunnel_req_is_tunnel(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    if (fd < 0) return false;
    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof(ss));
    socklen_t sl = sizeof(ss);
    if (getpeername(fd, (struct sockaddr *)&ss, &sl) != 0) return false;
    if (ss.ss_family == AF_INET) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)&ss;
        return a->sin_addr.s_addr == htonl(INADDR_LOOPBACK);
    }
#if CONFIG_LWIP_IPV6
    if (ss.ss_family == AF_INET6) {
        // Serveur en double pile : un client IPv4 apparait en ::ffff:a.b.c.d
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)&ss;
        const uint8_t *b = (const uint8_t *)&a->sin6_addr;
        static const uint8_t MAPPED[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF };
        static const uint8_t LOOP6[16]  = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
        if (memcmp(b, MAPPED, sizeof(MAPPED)) == 0) {
            return b[12] == 127 && b[13] == 0 && b[14] == 0 && b[15] == 1;
        }
        return memcmp(b, LOOP6, sizeof(LOOP6)) == 0;
    }
#endif
    return false;
}

static esp_err_t send_json(httpd_req_t *req, cJSON *root, bool secret = false)
{
    char *s = root ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (!s) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Memoire insuffisante\"}");
    }
    esp_err_t err = httpd_resp_sendstr(req, s);
    if (secret) memset(s, 0, strlen(s));
    free(s);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, const char *status, int code, const char *msg)
{
    cJSON *r = cJSON_CreateObject();
    if (r) {
        cJSON_AddStringToObject(r, "status", "error");
        cJSON_AddNumberToObject(r, "code", code);
        cJSON_AddStringToObject(r, "message", msg);
    }
    httpd_resp_set_status(req, status);
    return send_json(req, r);
}

static esp_err_t send_ok(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
}

/** Corps JSON (1 Ko max) ; le tampon, qui peut contenir le jeton, est efface. */
static cJSON *read_body_json(httpd_req_t *req)
{
    int len = req->content_len;
    if (len <= 0 || len > 1024) return NULL;
    char buf[1025];
    int got = 0, timeouts = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
        if (r <= 0) { memset(buf, 0, sizeof(buf)); return NULL; }
        got += r;
    }
    buf[got] = '\0';
    cJSON *doc = cJSON_Parse(buf);
    memset(buf, 0, sizeof(buf));
    return doc;
}

static bool json_bool(const cJSON *it)
{
    if (cJSON_IsBool(it))   return cJSON_IsTrue(it);
    if (cJSON_IsNumber(it)) return it->valuedouble != 0;
    if (cJSON_IsString(it) && it->valuestring) {
        // "on" : valeur d'une case cochee du formulaire LiXee-Box
        return strcmp(it->valuestring, "1") == 0 || strcasecmp(it->valuestring, "on") == 0 ||
               strcasecmp(it->valuestring, "true") == 0;
    }
    return false;
}

/** Chaine du document, ou NULL si absente / non chaine. */
static const char *json_str(const cJSON *doc, const char *key)
{
    const cJSON *it = cJSON_GetObjectItem(doc, key);
    return (cJSON_IsString(it) && it->valuestring) ? it->valuestring : NULL;
}

/** Jeton saisi : NULL (inchange) si absent, vide ou masque. */
static const char *token_param(const cJSON *doc)
{
    const char *t = json_str(doc, "tunnelToken");
    if (!t || t[0] == '\0' || strcmp(t, MASKED_TOKEN) == 0) return NULL;
    return t;
}

static bool hostname_ok(const char *h)
{
    if (!h[0]) return false;
    for (; *h; h++) {
        char c = *h;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '-' || c == ':')) return false;
    }
    return true;
}

static const char *state_name(tunnel_state_t s)
{
    switch (s) {
        case TUNNEL_STATE_SUSPENDED:  return "suspended";
        case TUNNEL_STATE_CONNECTING: return "connecting";
        case TUNNEL_STATE_CONNECTED:  return "connected";
        case TUNNEL_STATE_WAITING:    return "waiting";
        default:                      return "disabled";
    }
}

static void add_activation(cJSON *obj, const tunnel_activation_t *a)
{
    if (a->status == TUNNEL_ACT_DONE) {
        cJSON_AddStringToObject(obj, "status", "done");
        cJSON_AddBoolToObject(obj, "success", a->success);
        if (a->success) cJSON_AddStringToObject(obj, "deviceId", a->device_id);
        else            cJSON_AddStringToObject(obj, "error", a->error);
    } else {
        cJSON_AddStringToObject(obj, "status", a->status == TUNNEL_ACT_PROCESSING ? "processing" : "idle");
    }
}

// ----------------------------------------------------------------------------
// Handlers
// ----------------------------------------------------------------------------

static esp_err_t status_handler(httpd_req_t *req)
{
    tunnel_status_t st;
    tunnel_get_status(&st);
    tunnel_activation_t act;
    tunnel_get_activation(&act);

    cJSON *r = cJSON_CreateObject();
    if (!r) return send_json(req, NULL);
    cJSON_AddBoolToObject(r, "enabled", st.enabled);
    cJSON_AddBoolToObject(r, "connected", st.connected);
    if (st.connected && hostname_ok(st.subdomain)) {
        char url[sizeof(st.subdomain) + 16];
        strlcpy(url, "https://", sizeof(url));
        strlcat(url, st.subdomain, sizeof(url));
        cJSON_AddStringToObject(r, "url", url);
    }
    cJSON_AddStringToObject(r, "state", state_name(st.state));
    cJSON_AddBoolToObject(r, "securityOk", st.security_ok);
    cJSON_AddBoolToObject(r, "securityLocked", tunnel_security_locked());
    cJSON_AddBoolToObject(r, "viaTunnel", web_tunnel_req_is_tunnel(req));
    cJSON_AddStringToObject(r, "clientId", st.client_id);
    cJSON_AddBoolToObject(r, "hasToken", st.has_token);
    cJSON_AddStringToObject(r, "deviceId", st.device_id);
    cJSON_AddStringToObject(r, "subdomain", st.subdomain);
    cJSON_AddStringToObject(r, "lastError", st.last_error);
    cJSON_AddNumberToObject(r, "upgradeStatus", st.upgrade_status);
    cJSON_AddNumberToObject(r, "connectedFor", st.connected_for_s);
    cJSON_AddNumberToObject(r, "retryIn", st.retry_in_s);
    cJSON_AddNumberToObject(r, "reconnects", st.reconnects);
    cJSON_AddNumberToObject(r, "requests", st.requests);
    cJSON_AddNumberToObject(r, "queued", st.queued);
    cJSON_AddNumberToObject(r, "inFlight", st.in_flight);
    cJSON *a = cJSON_AddObjectToObject(r, "activation");
    if (a) add_activation(a, &act);
    return send_json(req, r);
}

/** Portage de handleSaveConfigTunnel (memes regles, memes codes d'erreur). */
static esp_err_t config_post_handler(httpd_req_t *req)
{
    cJSON *doc = read_body_json(req);
    if (!doc) return send_error(req, "400 Bad Request", 0, "JSON invalide");

    bool cur_enabled = false;
    char cur_id[TUNNEL_CLIENT_ID_MAX], cur_tok[TUNNEL_TOKEN_MAX];
    tunnel_get_config(&cur_enabled, cur_id, sizeof(cur_id), cur_tok, sizeof(cur_tok));
    const bool has_token = cur_tok[0] != '\0';
    memset(cur_tok, 0, sizeof(cur_tok));

    // Un SEUL interrupteur pilote le tunnel : le formulaire de connexion
    // manuelle envoie credsOnly et ne change pas l'etat marche/arret.
    const bool creds_only = json_bool(cJSON_GetObjectItem(doc, "credsOnly"));
    const bool want = creds_only ? cur_enabled : json_bool(cJSON_GetObjectItem(doc, "enableTunnel"));
    const bool via  = web_tunnel_req_is_tunnel(req);

    // Couper le tunnel depuis le tunnel ferait perdre l'acces distant
    if (via && !want) {
        cJSON_Delete(doc);
        return send_error(req, "403 Forbidden", 4,
                          "Impossible de d\u00e9sactiver le tunnel depuis un acc\u00e8s distant.");
    }
    if (want && !tunnel_security_ok()) {
        cJSON_Delete(doc);
        return send_error(req, "403 Forbidden", 8,
                          "L'acc\u00e8s s\u00e9curis\u00e9 HTTP doit \u00eatre activ\u00e9, avec un identifiant "
                          "et un mot de passe, avant d'activer le tunnel.");
    }

    const char *id  = json_str(doc, "tunnelClientId");
    const char *tok = token_param(doc);
    const char *eff_id = id ? id : cur_id;
    int err = 0;
    if (want) {
        if (eff_id[0] == '\0')     err |= 1;
        if (!tok && !has_token)    err |= 2;
    }
    if (err) {
        cJSON_Delete(doc);
        return send_error(req, "400 Bad Request", err,
                          (err & 1) ? "Client ID requis quand le tunnel est activ\u00e9."
                                    : "Token requis quand le tunnel est activ\u00e9.");
    }

    esp_err_t e = tunnel_set_config(want, id, tok, via ? RESTART_DELAY_VIA_TUNNEL_MS : 0);
    cJSON_Delete(doc);
    if (e == ESP_ERR_INVALID_ARG) {
        return send_error(req, "400 Bad Request", 16,
                          "Caract\u00e8res invalides ou longueur excessive (Client ID : 63, Token : 127).");
    }
    if (e != ESP_OK) return send_error(req, "500 Internal Server Error", 0, "Echec d'enregistrement");
    ESP_LOGI(TAG, "Configuration du tunnel enregistree (%s%s)", want ? "actif" : "inactif", via ? ", via le tunnel" : "");
    return send_ok(req);
}

static esp_err_t activate_reply(httpd_req_t *req, const char *status, const char *error)
{
    cJSON *r = cJSON_CreateObject();
    if (r) {
        cJSON_AddStringToObject(r, "status", status);
        if (error) {
            cJSON_AddStringToObject(r, "error", error);     // nom LiXee-Box
            cJSON_AddStringToObject(r, "message", error);   // pour apiErrorText()
        }
    }
    return send_json(req, r);
}

static esp_err_t activate_with_code(httpd_req_t *req, const char *code);

static esp_err_t activate_post_handler(httpd_req_t *req)
{
    cJSON *doc = read_body_json(req);
    char code[8] = "";
    const char *c = doc ? json_str(doc, "code") : NULL;
    if (c) strlcpy(code, c, sizeof(code));
    cJSON_Delete(doc);
    return activate_with_code(req, code);
}

/**
 * GET /api/tunnel/activate?code=XXXXXX (et /api/tunnelActivate) : forme de la
 * LiXee-Box, gardee pour les clients existants. Le cookie de session
 * SameSite=Strict n'accompagne pas un lien venu d'un autre site.
 */
static esp_err_t activate_get_handler(httpd_req_t *req)
{
    char q[64], code[8] = "";
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        httpd_query_key_value(q, "code", code, sizeof(code));
    }
    return activate_with_code(req, code);
}

static esp_err_t activate_with_code(httpd_req_t *req, const char *code)
{
    switch (tunnel_start_activation(code)) {
        case TUNNEL_ACT_START_OK:          return activate_reply(req, "pending", NULL);
        case TUNNEL_ACT_START_BUSY:        return activate_reply(req, "processing", NULL);
        case TUNNEL_ACT_START_BAD_CODE:    return activate_reply(req, "error", "Code invalide");
        case TUNNEL_ACT_START_NO_SECURITY:
            return activate_reply(req, "error",
                                  "L'acc\u00e8s s\u00e9curis\u00e9 HTTP doit \u00eatre activ\u00e9, avec un "
                                  "identifiant et un mot de passe, avant d'activer le tunnel.");
        default:
            return activate_reply(req, "error", "Tunnel non d\u00e9marr\u00e9 ou m\u00e9moire insuffisante");
    }
}

static esp_err_t activate_status_handler(httpd_req_t *req)
{
    tunnel_activation_t act;
    tunnel_get_activation(&act);
    cJSON *r = cJSON_CreateObject();
    if (r) add_activation(r, &act);
    return send_json(req, r);
}

static esp_err_t credentials_get_handler(httpd_req_t *req)
{
    // La LiXee-Box le servait aussi par le tunnel : le jeton est l'identite du
    // pont aupres du relais, inutile de le faire transiter sur Internet.
    if (web_tunnel_req_is_tunnel(req)) {
        return send_error(req, "403 Forbidden", 0, "Disponible uniquement en acc\u00e8s local");
    }
    bool en = false;
    char id[TUNNEL_CLIENT_ID_MAX], tok[TUNNEL_TOKEN_MAX];
    tunnel_get_config(&en, id, sizeof(id), tok, sizeof(tok));
    cJSON *r = cJSON_CreateObject();
    if (r) {
        cJSON_AddStringToObject(r, "tunnelClientId", id);
        cJSON_AddStringToObject(r, "tunnelToken", tok);
    }
    memset(tok, 0, sizeof(tok));
    return send_json(req, r, true);
}

static esp_err_t credentials_post_handler(httpd_req_t *req)
{
    cJSON *doc = read_body_json(req);
    if (!doc) return send_error(req, "400 Bad Request", 0, "JSON invalide");
    bool en = false;
    tunnel_get_config(&en, NULL, 0, NULL, 0);
    const char *id  = json_str(doc, "tunnelClientId");
    const char *tok = token_param(doc);
    esp_err_t e = ESP_OK;
    if (id || tok) {
        e = tunnel_set_config(en, id, tok, web_tunnel_req_is_tunnel(req) ? RESTART_DELAY_VIA_TUNNEL_MS : 0);
    }
    cJSON_Delete(doc);
    if (e == ESP_ERR_INVALID_ARG) {
        return send_error(req, "400 Bad Request", 16, "Caract\u00e8res invalides ou longueur excessive");
    }
    if (e != ESP_OK) return send_error(req, "500 Internal Server Error", 0, "Echec d'enregistrement");
    return send_ok(req);
}

// ----------------------------------------------------------------------------
// Enregistrement
// ----------------------------------------------------------------------------

void web_tunnel_register(httpd_handle_t server)
{
    static const struct {
        const char    *uri;
        httpd_method_t method;
        esp_err_t    (*fn)(httpd_req_t *);
    } routes[] = {
        { "/api/tunnel/status",          HTTP_GET,  status_handler },
        { "/api/tunnel/config",          HTTP_POST, config_post_handler },
        { "/api/tunnel/activate",        HTTP_POST, activate_post_handler },
        { "/api/tunnel/activate",        HTTP_GET,  activate_get_handler },
        { "/api/tunnel/activate/status", HTTP_GET,  activate_status_handler },
        { "/api/tunnel/credentials",     HTTP_GET,  credentials_get_handler },
        { "/api/tunnel/credentials",     HTTP_POST, credentials_post_handler },
#if WEB_TUNNEL_LEGACY_ALIASES
        { "/api/tunnelStatus",           HTTP_GET,  status_handler },
        { "/api/tunnelActivateStatus",   HTTP_GET,  activate_status_handler },
        { "/api/tunnelActivate",         HTTP_GET,  activate_get_handler },
        { "/api/tunnelActivate",         HTTP_POST, activate_post_handler },
#endif
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_uri_t uri = {};
        uri.uri     = routes[i].uri;
        uri.method  = routes[i].method;
        uri.handler = routes[i].fn;
        if (web_server_register_uri(server, &uri) != ESP_OK) {
            ESP_LOGE(TAG, "Enregistrement de %s impossible (max_uri_handlers ?)", routes[i].uri);
        }
    }
}
