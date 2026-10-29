// ============================================================================
// Web Server - Serveur HTTP pour configuration et monitoring
// Sert les fichiers statiques depuis LittleFS + API REST
// ============================================================================

#include "web_server.h"
#include "web_api.h"
#include "web_tunnel.h"
#include "tunnel.h"
#include "config_manager.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_vfs.h"
#include "mbedtls/base64.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>

static const char *TAG = "WEB_SRV";

static httpd_handle_t s_server = NULL;

// ============================================================================
// Authentification HTTP (Basic)
//
// Avant ce bloc, les champs user_http / pass_http / enable_secure_http etaient
// saisis dans l'interface, persistes et modifiables par l'API, mais lus par
// AUCUN code : activer "Securite HTTP" ne protegeait rien. Tous les endpoints
// etaient anonymes, y compris ceux qui rendent le mot de passe du broker MQTT,
// redemarrent l'appareil ou reecrivent les identifiants WiFi.
//
// Le controle est pose par web_server_register_uri() : le handler reel voyage
// dans user_ctx et un trampoline commun l'appelle seulement apres validation.
// Aucun endpoint ne peut etre oublie sans contourner cette fonction.
//
// Limite assumee : Basic transmet les identifiants en base64, donc en clair
// sur le reseau local, faute de TLS sur ce serveur. Cela protege contre
// l'acces trivial, pas contre un attaquant capable d'ecouter le trafic.
// ============================================================================

typedef esp_err_t (*web_handler_fn)(httpd_req_t *);

/** Comparaison a temps constant, pour ne pas fuir le prefixe correct. */
static bool const_time_equals(const char *a, const char *b)
{
    size_t la = strlen(a);
    if (la != strlen(b)) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < la; i++) {
        diff |= (unsigned char)(a[i] ^ b[i]);
    }
    return diff == 0;
}

/**
 * Valide l'en-tete Authorization contre les identifiants configures.
 *
 * On encode l'attendu et on le compare a ce qui arrive, plutot que de decoder
 * l'entree : aucun decodeur base64 n'est expose a une donnee hostile, et le
 * dimensionnement reste trivialement sur.
 */
static bool basic_auth_ok(httpd_req_t *req, const char *user, const char *pass)
{
    size_t hlen = httpd_req_get_hdr_value_len(req, "Authorization");
    if (hlen == 0 || hlen >= 256) return false;

    char hdr[264];
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK) {
        return false;
    }
    if (strncmp(hdr, "Basic ", 6) != 0) return false;

    // user_http[50] + ':' + pass_http[50] tient dans 104 octets
    char plain[104];
    int n = snprintf(plain, sizeof(plain), "%s:%s", user, pass);
    if (n < 0 || n >= (int)sizeof(plain)) return false;

    char b64[144];
    size_t olen = 0;
    int rc = mbedtls_base64_encode((unsigned char *)b64, sizeof(b64) - 1, &olen,
                                   (const unsigned char *)plain, (size_t)n);
    memset(plain, 0, sizeof(plain));   // ne pas laisser le secret sur la pile
    if (rc != 0) return false;
    b64[olen] = '\0';

    bool ok = const_time_equals(hdr + 6, b64);
    memset(b64, 0, sizeof(b64));
    return ok;
}

// ============================================================================
// Sessions (cookie) et protection CSRF -- portage de checkAuth() LiXee-Box
//
// Un navigateur se connecte par la page /login et recoit un cookie de session
// (24 h, 4 sessions au plus). Basic reste accepte pour les clients API et
// LiXee-Assist. Les requetes qui modifient l'etat (POST/PUT/DELETE) venant
// d'un navigateur doivent provenir du pont lui-meme (Origin/Referer == Host) :
// une page tierce ouverte par l'utilisateur ne peut donc pas les declencher
// avec son cookie.
// ============================================================================

#define SESSION_MAX        4
#define SESSION_TOKEN_LEN  32                      // hexadecimal
#define SESSION_LIFETIME_US (24LL * 3600 * 1000000)

typedef struct {
    char    token[SESSION_TOKEN_LEN + 1];
    int64_t expires_us;
} web_session_t;

// Acces depuis la seule tache du serveur HTTP : pas de verrou necessaire
static web_session_t s_sessions[SESSION_MAX];

static bool auth_required(void)
{
    const cfg_settings_t *st = cfg_get_settings();
    if (!st->enable_secure_http) return false;   // protection desactivee
    const cfg_general_t *gen = cfg_get_general();
    if (gen->user_http[0] == '\0' || gen->pass_http[0] == '\0') {
        // Protection demandee mais identifiants incomplets. On laisse passer
        // volontairement : le seul moyen de corriger les identifiants est
        // l'interface web elle-meme, donc refuser rendrait l'appareil
        // inaccessible sans reprovisionnement BLE ou reflashage.
        ESP_LOGE(TAG, "enableSecureHttp=1 mais identifiants vides : acces LAISSE OUVERT");
        return false;
    }
    return true;
}

static const char *session_create(void)
{
    int64_t now = esp_timer_get_time();
    int slot = 0;
    for (int i = 0; i < SESSION_MAX; i++) {
        // Une place libre ou expiree, sinon la session la plus ancienne
        if (s_sessions[i].token[0] == '\0' || s_sessions[i].expires_us < now) { slot = i; break; }
        if (s_sessions[i].expires_us < s_sessions[slot].expires_us) slot = i;
    }
    uint8_t rnd[SESSION_TOKEN_LEN / 2];
    esp_fill_random(rnd, sizeof(rnd));
    for (size_t i = 0; i < sizeof(rnd); i++) {
        snprintf(&s_sessions[slot].token[i * 2], 3, "%02x", rnd[i]);
    }
    s_sessions[slot].expires_us = now + SESSION_LIFETIME_US;
    return s_sessions[slot].token;
}

/** Index de la session portee par le cookie de la requete, -1 si aucune. */
static int session_find(httpd_req_t *req)
{
    char tok[SESSION_TOKEN_LEN + 8];
    size_t len = sizeof(tok);
    if (httpd_req_get_cookie_val(req, "session", tok, &len) != ESP_OK) return -1;
    if (strlen(tok) != SESSION_TOKEN_LEN) return -1;
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < SESSION_MAX; i++) {
        if (s_sessions[i].token[0] && s_sessions[i].expires_us > now &&
            const_time_equals(s_sessions[i].token, tok)) {
            return i;
        }
    }
    return -1;
}

/** Hote d'une URL "http://hote[:port]/..." compare a l'en-tete Host. */
static bool url_host_matches(const char *url, const char *host)
{
    const char *p = strstr(url, "://");
    if (!p) return false;
    p += 3;
    size_t n = strcspn(p, "/");
    return n == strlen(host) && strncasecmp(p, host, n) == 0;
}

/**
 * CSRF : une requete qui modifie l'etat doit venir d'une page du pont.
 * Sans Origin ni Referer (client API, LiXee-Assist, curl), on laisse passer :
 * ces clients s'authentifient par Basic, que le navigateur d'une victime
 * n'envoie pas de lui-meme a un site tiers.
 */
static bool origin_ok(httpd_req_t *req);

bool web_server_origin_ok(httpd_req_t *req)
{
    if (web_tunnel_req_is_tunnel(req)) return true;
    return origin_ok(req);
}

static bool csrf_ok(httpd_req_t *req)
{
    if (req->method != HTTP_POST && req->method != HTTP_PUT && req->method != HTTP_DELETE) {
        return true;
    }
    return origin_ok(req);
}

/** Origin (ou Referer) absent, ou egal a l'hote du pont. */
static bool origin_ok(httpd_req_t *req)
{
    char host[96], src[256];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) return false;
    if (httpd_req_get_hdr_value_str(req, "Origin", src, sizeof(src)) == ESP_OK) {
        return url_host_matches(src, host);
    }
    if (httpd_req_get_hdr_value_str(req, "Referer", src, sizeof(src)) == ESP_OK) {
        return url_host_matches(src, host);
    }
    return true;
}

/** Navigation d'un navigateur vers une page (pas un appel XHR ni un fichier). */
/**
 * Navigation (regle checkAuth de la LiXee-Box) : ni XHR, ni demande de JSON,
 * et page HTML demandee ou requete sans Referer (lien, favori, widget
 * LiXee-Assist qui se reconnecte par "/poll -> 302 -> POST /login").
 */
static bool is_page_navigation(httpd_req_t *req)
{
    if (req->method != HTTP_GET) return false;
    char xrw[32], accept[128], ref[8];
    if (httpd_req_get_hdr_value_str(req, "X-Requested-With", xrw, sizeof(xrw)) == ESP_OK) return false;
    bool has_accept = httpd_req_get_hdr_value_str(req, "Accept", accept, sizeof(accept)) != ESP_ERR_NOT_FOUND;
    if (has_accept && strstr(accept, "application/json")) return false;
    bool wants_html = has_accept && strstr(accept, "text/html") != NULL;
    bool from_page = httpd_req_get_hdr_value_str(req, "Referer", ref, sizeof(ref)) != ESP_ERR_NOT_FOUND;
    return wants_html || !from_page;
}

/**
 * Page de retour apres connexion (champ "next", sanitizeNext de la Box) :
 * chemin local uniquement, jamais /login ni /logout.
 */
static void sanitize_next(const char *in, char *out, size_t len)
{
    strlcpy(out, "/", len);
    if (!in || in[0] != '/' || in[1] == '/' || in[1] == '\\') return;
    for (const char *c = in; *c; c++) if ((unsigned char)*c < 0x20 || *c == '\\') return;
    if (strncmp(in, "/login", 6) == 0 || strncmp(in, "/logout", 7) == 0) return;
    strlcpy(out, in, len);
}

/**
 * Ajoute 'src' a 'dst' comme valeur d'un parametre d'URL : les caracteres des
 * chemins locaux sont surs, sauf & ? % # + (encodes en %XX).
 */
static void append_param(char *dst, size_t len, const char *src)
{
    size_t n = strlen(dst);
    for (const char *c = src; *c && n + 4 < len; c++) {
        if (*c == '&' || *c == '?' || *c == '%' || *c == '#' || *c == '+') {
            n += snprintf(dst + n, len - n, "%%%02X", (unsigned char)*c);
        } else {
            dst[n++] = *c;
            dst[n] = '\0';
        }
    }
}

/** Decodage d'un champ de formulaire (+ et %xx), en place. */
static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '+') { *o++ = ' '; continue; }
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hx[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(hx, NULL, 16);
            s += 2;
            continue;
        }
        *o++ = *s;
    }
    *o = '\0';
}

/**
 * @return true si la requete peut poursuivre. Sinon la reponse (401, 403 ou
 *         redirection vers /login) a deja ete emise par cette fonction.
 */
static bool login_asset(const char *uri);

static bool auth_check(httpd_req_t *req)
{
    // Feuille de style et logo de la page de connexion : sans session (rien de prive)
    if (req->method == HTTP_GET && login_asset(req->uri)) return true;
    // Requete relayee par le tunnel distant (connexion locale 127.0.0.1,
    // qu'un client du reseau ne peut pas usurper) : comme sur la LiXee-Box,
    // jamais d'acces sans identifiants par ce chemin, pas de Basic, et pas de
    // controle CSRF (le relais envoie l'Origin de son sous-domaine avec
    // Host 127.0.0.1 ; le cookie SameSite=Strict protege la session).
    const bool via_tunnel = web_tunnel_req_is_tunnel(req);
    if (via_tunnel && !tunnel_security_ok()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "Forbidden");
        return false;
    }
    if (!auth_required()) {
        // Sans protection HTTP, une page tierce ouverte sur le reseau local
        // pouvait encore poster vers le pont (WiFi efface, reboot...) : l'origine
        // est controlee dans tous les cas (requetes sans Origin ni Referer admises)
        if (!via_tunnel && !csrf_ok(req)) {
            ESP_LOGW(TAG, "Requete refusee (origine etrangere) sur %s", req->uri);
            httpd_resp_set_status(req, "403 Forbidden");
            httpd_resp_sendstr(req, "Forbidden");
            return false;
        }
        return true;
    }

    const cfg_general_t *gen = cfg_get_general();
    bool by_session = session_find(req) >= 0;
    bool by_basic = !by_session && !via_tunnel && basic_auth_ok(req, gen->user_http, gen->pass_http);
    if (!by_session && !by_basic) {
        ESP_LOGW(TAG, "Acces refuse sur %s", req->uri);
        if (is_page_navigation(req)) {
            // Navigateur : page de connexion, avec retour a la page demandee
            static char loc[CONFIG_HTTPD_MAX_URI_LEN + 24];   // tache HTTP seule
            const char *u = req->uri;
            if (strcmp(u, "/") == 0 || strncmp(u, "/index.html", 11) == 0) {
                strlcpy(loc, "/login", sizeof(loc));
            } else {
                strlcpy(loc, "/login?next=", sizeof(loc));
                append_param(loc, sizeof(loc), u);
            }
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", loc);
            httpd_resp_send(req, NULL, 0);
        } else {
            // 401 JSON sans WWW-Authenticate, comme la LiXee-Box : le navigateur
            // n'ouvre pas sa fenetre Basic, l'application renvoie vers /login ;
            // un client API envoie ses identifiants Basic d'emblee
            httpd_resp_set_status(req, "401 Unauthorized");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"error\":\"unauthorized\"}");
        }
        return false;
    }

    // Origine controlee pour la session ET pour Basic (comme checkCsrf de la
    // LiXee-Box) : un navigateur rejoue seul des identifiants Basic memorises.
    // Sans Origin ni Referer (client non navigateur), la requete passe.
    if ((by_session || by_basic) && !via_tunnel && !csrf_ok(req)) {
        ESP_LOGW(TAG, "Requete refusee (origine etrangere) sur %s", req->uri);
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "Forbidden");
        return false;
    }
    return true;
}

// ----------------------------------------------------------------------------
// Page de connexion : autonome (aucune ressource externe), donc servie sans
// authentification.
// ----------------------------------------------------------------------------

// Page de la LiXee-Box (HTTP_LOGIN de web.cpp), a l'identique : Bootstrap,
// logo, bouton oeil du mot de passe, formulaire classique (POST -> 303).
// Format snprintf : next, classe du message d'erreur, message, version
// ('%' du CSS doubles).
static const char LOGIN_HTML[] =
    "<!DOCTYPE html>\n<html>\n<head>\n"
    "  <meta charset='utf-8'>\n"
    "  <meta name='viewport' content='width=device-width, initial-scale=1'>\n"
    "  <title>Box - Connexion</title>\n"
    "  <link href='/css/bootstrap.min.css' rel='stylesheet'>\n"
    "  <style>\n"
    "    body { background-color: #f7f9fc; font-family: 'Inter', sans-serif; min-height: 100vh;"
    " display: flex; align-items: center; justify-content: center; }\n"
    "    .login-card { width: 100%%; max-width: 400px; border-radius: 1rem; box-shadow: 0 4px 24px rgba(0,0,0,0.10); }\n"
    "    .login-logo { text-align: center; margin-bottom: 1.5rem; }\n"
    "    .login-logo img { height: 48px; }\n"
    "  </style>\n"
    "</head>\n<body>\n"
    "  <div class='card login-card'>\n"
    "    <div class='card-body p-4'>\n"
    "      <div class='login-logo'>\n"
    "        <img src='/img/logo.png' alt='Box' onerror=\"this.style.display='none'\">\n"
    "        <h4 class='mt-2'>Box</h4>\n"
    "      </div>\n"
    "      <div id='error' class='alert alert-danger %s'>%s</div>\n"
    "      <form method='POST' action='/login'>\n"
    "        <input type='hidden' name='next' value='%s'>\n"
    "        <div class='mb-3'>\n"
    "          <label for='user' class='form-label'>Identifiant</label>\n"
    "          <input class='form-control' id='user' type='text' name='user' autocomplete='username' required autofocus>\n"
    "        </div>\n"
    "        <div class='mb-3'>\n"
    "          <label for='pass' class='form-label'>Mot de passe</label>\n"
    "          <div class='input-group'>\n"
    "            <input class='form-control' id='pass' type='password' name='pass' autocomplete='current-password' required>\n"
    "            <button class='btn btn-outline-secondary' type='button' id='togglePass' tabindex='-1'>\n"
    "              <svg id='eyeIcon' xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' viewBox='0 0 16 16'>\n"
    "                <path d='M16 8s-3-5.5-8-5.5S0 8 0 8s3 5.5 8 5.5S16 8 16 8zM1.173 8a13.133 13.133 0 0 1 1.66-2.043C4.12 4.668 5.88 3.5 8 3.5c2.12 0 3.879 1.168 5.168 2.457A13.133 13.133 0 0 1 14.828 8c-.058.087-.122.183-.195.288-.335.48-.83 1.12-1.465 1.755C11.879 11.332 10.119 12.5 8 12.5c-2.12 0-3.879-1.168-5.168-2.457A13.134 13.134 0 0 1 1.172 8z'/>\n"
    "                <path d='M8 5.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5zM4.5 8a3.5 3.5 0 1 1 7 0 3.5 3.5 0 0 1-7 0z'/>\n"
    "              </svg>\n"
    "            </button>\n"
    "          </div>\n"
    "        </div>\n"
    "        <button type='submit' class='btn btn-warning w-100 btn-lg'>Se connecter</button>\n"
    "      </form>\n"
    "      <div class='text-center text-muted mt-3' style='font-size:0.8rem;'>%s</div>\n"
    "    </div>\n"
    "  </div>\n"
    "  <script>\n"
    "    document.getElementById('togglePass').addEventListener('click',function(){\n"
    "      var p=document.getElementById('pass'),e=document.getElementById('eyeIcon');\n"
    "      if(p.type==='password'){p.type='text';e.innerHTML='<path d=\"M13.359 11.238C15.06 9.72 16 8 16 8s-3-5.5-8-5.5a7.028 7.028 0 0 0-2.79.588l.77.771A5.944 5.944 0 0 1 8 3.5c2.12 0 3.879 1.168 5.168 2.457A13.134 13.134 0 0 1 14.828 8c-.058.087-.122.183-.195.288-.335.48-.83 1.12-1.465 1.755-.165.165-.337.328-.517.486l.708.709z\"/><path d=\"M11.297 9.176a3.5 3.5 0 0 0-4.474-4.474l.823.823a2.5 2.5 0 0 1 2.829 2.829l.822.822zm-2.943 1.299l.822.822a3.5 3.5 0 0 1-4.474-4.474l.823.823a2.5 2.5 0 0 0 2.829 2.829z\"/><path d=\"M3.35 5.47c-.18.16-.353.322-.518.487A13.134 13.134 0 0 0 1.172 8l.195.288c.335.48.83 1.12 1.465 1.755C4.121 11.332 5.881 12.5 8 12.5c.716 0 1.39-.133 2.02-.36l.77.772A7.029 7.029 0 0 1 8 13.5C3 13.5 0 8 0 8s.939-1.721 2.641-3.238l.708.709z\"/><path d=\"M13.646 14.354l-12-12 .708-.708 12 12-.708.708z\"/>';}\n"
    "      else{p.type='password';e.innerHTML='<path d=\"M16 8s-3-5.5-8-5.5S0 8 0 8s3 5.5 8 5.5S16 8 16 8zM1.173 8a13.133 13.133 0 0 1 1.66-2.043C4.12 4.668 5.88 3.5 8 3.5c2.12 0 3.879 1.168 5.168 2.457A13.133 13.133 0 0 1 14.828 8c-.058.087-.122.183-.195.288-.335.48-.83 1.12-1.465 1.755C11.879 11.332 10.119 12.5 8 12.5c-2.12 0-3.879-1.168-5.168-2.457A13.134 13.134 0 0 1 1.172 8z\"/><path d=\"M8 5.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5zM4.5 8a3.5 3.5 0 1 1 7 0 3.5 3.5 0 0 1-7 0z\"/>';}\n"
    "    });\n"
    "  </script>\n"
    "</body>\n</html>\n";

/** Echappement pour un attribut HTML entre apostrophes (htmlAttrEscape de la Box). */
static void attr_escape(const char *in, char *out, size_t len)
{
    size_t n = 0;
    for (; *in && n + 7 < len; in++) {
        const char *r = NULL;
        switch (*in) {
        case '&':  r = "&amp;"; break;
        case '<':  r = "&lt;"; break;
        case '>':  r = "&gt;"; break;
        case '"':  r = "&quot;"; break;
        case '\'': r = "&#39;"; break;
        default: break;
        }
        if (r) { size_t l = strlen(r); memcpy(out + n, r, l); n += l; }
        else out[n++] = *in;
    }
    out[n] = '\0';
}

/** Fichiers servis sans session : ceux de la page de connexion. */
static bool login_asset(const char *uri)
{
    return !strcmp(uri, "/css/bootstrap.min.css") || !strcmp(uri, "/img/logo.png");
}

static esp_err_t login_get_handler(httpd_req_t *req)
{
    if (!auth_required()) {
        // Protection desactivee : rien a saisir
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        return httpd_resp_send(req, NULL, 0);
    }
    if (session_find(req) >= 0) {
        // Deja connecte (autre onglet) : directement a la page demandee
        static char q[3 * 128], dest[128];   // tache HTTP seule
        q[0] = '\0';
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
            char v[3 * 128] = "";
            if (httpd_query_key_value(q, "next", v, sizeof(v)) == ESP_OK) {
                url_decode(v);
                strlcpy(q, v, sizeof(q));
            } else {
                q[0] = '\0';
            }
        }
        sanitize_next(q, dest, sizeof(dest));
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", dest);
        return httpd_resp_send(req, NULL, 0);
    }
    // Page de retour (champ cache) et message d'erreur ecrits par le serveur,
    // comme la Box : la page n'a plus besoin de JavaScript pour se connecter
    static char q[3 * 128], nx[128], nx_html[6 * 128];   // tache HTTP seule
    bool error = false;
    q[0] = '\0';
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        char v[3 * 128] = "";
        error = httpd_query_key_value(q, "error", v, sizeof(v)) == ESP_OK;
        if (httpd_query_key_value(q, "next", v, sizeof(v)) == ESP_OK) url_decode(v);
        else v[0] = '\0';
        strlcpy(q, v, sizeof(q));
    }
    sanitize_next(q, nx, sizeof(nx));
    attr_escape(nx, nx_html, sizeof(nx_html));
    const size_t cap = sizeof(LOGIN_HTML) + sizeof(nx_html) + 128;
    char *page = (char *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!page) return httpd_resp_send_500(req);
    int n = snprintf(page, cap, LOGIN_HTML, error ? "" : "d-none",
                     error ? "Identifiant ou mot de passe incorrect" : "", nx_html,
                     esp_app_get_description()->version);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, page, n > 0 && (size_t)n < cap ? n : (ssize_t)strlen(page));
    heap_caps_free(page);
    return r;
}

static int64_t s_last_fail_us;          // dernier echec de connexion

static esp_err_t login_post_handler(httpd_req_t *req)
{
    if (!auth_required()) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    }
    // Frein aux essais en serie sans bloquer la seule tache HTTP : un essai
    // moins d'une seconde apres un echec est refuse tout de suite
    if (s_last_fail_us && esp_timer_get_time() - s_last_fail_us < 1000000) {
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Trop de tentatives\"}");
    }
    static char body[768];                  // tache HTTP seule
    int len = req->content_len;
    if (len <= 0 || len >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_FAIL;
    }
    // Nombre d'attentes borne : un client qui annonce un corps puis se tait
    // bloquait la seule tache du serveur (interface et tunnel), sans login
    int got = 0, timeouts = 0;
    while (got < len) {
        int r = httpd_req_recv(req, body + got, len - got);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
            return ESP_FAIL;
        }
        got += r;
    }
    body[got] = '\0';

    // Formulaire classique (LiXee-Box) : user, pass, next -> redirection 303
    char ctype[64] = "";
    httpd_req_get_hdr_value_str(req, "Content-Type", ctype, sizeof(ctype));
    if (strstr(ctype, "x-www-form-urlencoded")) {
        // Champs encodes jusqu'a 3 fois plus longs que decodes (%XX)
        static char fu[3 * 64], fp[3 * 96], fn[3 * 128];
        char next[128];
        fu[0] = fp[0] = fn[0] = '\0';
        bool trunc = httpd_query_key_value(body, "user", fu, sizeof(fu)) == ESP_ERR_HTTPD_RESULT_TRUNC ||
                     httpd_query_key_value(body, "pass", fp, sizeof(fp)) == ESP_ERR_HTTPD_RESULT_TRUNC;
        httpd_query_key_value(body, "next", fn, sizeof(fn));
        if (trunc) fp[0] = '\0';
        memset(body, 0, sizeof(body));
        url_decode(fu);
        url_decode(fp);
        url_decode(fn);
        sanitize_next(fn, next, sizeof(next));
        const cfg_general_t *gen = cfg_get_general();
        bool fok = const_time_equals(fu, gen->user_http) && const_time_equals(fp, gen->pass_http);
        memset(fp, 0, sizeof(fp));
        httpd_resp_set_status(req, "303 See Other");
        if (!fok) {
            ESP_LOGW(TAG, "Echec de connexion");
            s_last_fail_us = esp_timer_get_time();
            static char loc[3 * 128 + 24];
            strlcpy(loc, strcmp(next, "/") ? "/login?error=1&next=" : "/login?error=1", sizeof(loc));
            if (strcmp(next, "/")) append_param(loc, sizeof(loc), next);   // "next" decode : re-encode
            httpd_resp_set_hdr(req, "Location", loc);
            return httpd_resp_send(req, NULL, 0);
        }
        static char cookie[96], dest[128];
        snprintf(cookie, sizeof(cookie),
                 "session=%s; Path=/; Max-Age=86400; HttpOnly; SameSite=Strict", session_create());
        strlcpy(dest, next, sizeof(dest));
        httpd_resp_set_hdr(req, "Set-Cookie", cookie);
        httpd_resp_set_hdr(req, "Location", dest);
        ESP_LOGI(TAG, "Connexion de '%s'", gen->user_http);
        return httpd_resp_send(req, NULL, 0);
    }

    cJSON *json = cJSON_Parse(body);
    memset(body, 0, sizeof(body));
    const cJSON *u = json ? cJSON_GetObjectItem(json, "user") : NULL;
    const cJSON *p = json ? cJSON_GetObjectItem(json, "pass") : NULL;
    const cfg_general_t *gen = cfg_get_general();
    bool ok = cJSON_IsString(u) && cJSON_IsString(p) &&
              const_time_equals(u->valuestring, gen->user_http) &&
              const_time_equals(p->valuestring, gen->pass_http);
    cJSON_Delete(json);

    if (!ok) {
        ESP_LOGW(TAG, "Echec de connexion");
        s_last_fail_us = esp_timer_get_time();   // freine les essais en serie
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"status\":\"error\"}");
    }

    char cookie[96];
    snprintf(cookie, sizeof(cookie),
             "session=%s; Path=/; Max-Age=86400; HttpOnly; SameSite=Strict", session_create());
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    httpd_resp_set_type(req, "application/json");
    ESP_LOGI(TAG, "Connexion de '%s'", gen->user_http);
    return httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
}

/** Pre-requete CORS : 204, sans authentification (aucune donnee renvoyee). */
static esp_err_t options_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "600");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t logout_handler(httpd_req_t *req)
{
    int i = session_find(req);
    if (i >= 0) memset(&s_sessions[i], 0, sizeof(s_sessions[i]));
    httpd_resp_set_hdr(req, "Set-Cookie", "session=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/login");
    return httpd_resp_send(req, NULL, 0);
}

/** Trampoline commun : authentifie puis delegue au handler reel. */
static esp_err_t auth_trampoline(httpd_req_t *req)
{
    // Acces a l'API depuis une autre origine (comme la LiXee-Box) : sans
    // identifiants, un navigateur n'envoyant pas de cookie ni de Basic avec "*"
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    if (!auth_check(req)) return ESP_OK;   // 401 deja envoye

    web_handler_fn real = (web_handler_fn)req->user_ctx;
    if (!real) {
        ESP_LOGE(TAG, "Handler absent pour %s", req->uri);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No handler");
        return ESP_FAIL;
    }
    return real(req);
}

esp_err_t web_server_register_uri(httpd_handle_t server, const httpd_uri_t *uri)
{
    if (!uri || !uri->handler) return ESP_ERR_INVALID_ARG;

    httpd_uri_t wrapped = *uri;
    wrapped.handler  = auth_trampoline;
    wrapped.user_ctx = (void *)uri->handler;
    return httpd_register_uri_handler(server, &wrapped);
}

// ============================================================================
// Servir les fichiers statiques depuis LittleFS
// ============================================================================

static const char* get_content_type(const char *path)
{
    if (strstr(path, ".html")) return "text/html";
    if (strstr(path, ".css"))  return "text/css";
    if (strstr(path, ".js"))   return "application/javascript";
    if (strstr(path, ".json")) return "application/json";
    if (strstr(path, ".png"))  return "image/png";
    if (strstr(path, ".gif"))  return "image/gif";
    if (strstr(path, ".ico"))  return "image/x-icon";
    return "application/octet-stream";
}

/**
 * Refuse toute URI capable de sortir de /littlefs/web.
 *
 * LittleFS RESOUT les ".." au lieu de les rejeter : lfs_dir_find annule la
 * composante precedente, si bien que "/../config/configWifi.json" devenait
 * "/config/configWifi.json" et servait le mot de passe WiFi en clair. Les
 * fichiers de /littlefs/config/ contiennent aussi passMQTT, passSMTP,
 * passWebPush et passHTTP, et /littlefs/db/ et /littlefs/hst/ etaient
 * accessibles de la meme facon.
 *
 * On REFUSE les ".." au lieu de normaliser le chemin : un site statique n'a
 * aucun besoin legitime de remonter, donc le rejet est plus simple et plus sur
 * qu'une normalisation qu'il faudrait prouver exacte.
 *
 * Note : esp_http_server ne decode pas le pourcent-encodage du chemin. Un
 * "%2e%2e" arrive donc litteralement et designe un nom de fichier inexistant,
 * pas une remontee : inutile de le traiter a part.
 */
static bool uri_path_is_safe(const char *uri)
{
    if (!uri || uri[0] != '/') return false;

    // Rejeter ".." uniquement en tant que composante COMPLETE de chemin :
    // un nom de fichier comme "jquery..min.js" reste legitime.
    for (const char *p = uri; *p; p++) {
        if (p[0] == '.' && p[1] == '.' &&
            (p == uri || p[-1] == '/') &&
            (p[2] == '\0' || p[2] == '/')) {
            return false;
        }
    }
    return true;
}

// Anciennes pages de la LiXee-Box -> route de l'interface (favoris, liens,
// LiXee-Assist). Les parametres de la requete suivent la route (#device?id=...).
static const struct { const char *page; const char *route; } LEGACY_PAGES[] = {
    { "/dashboard", "dashboard" },          { "/statusEnergy", "energy" },
    { "/statusEnergyTV", "energy" },        { "/statusNetwork", "network" },
    { "/statusDevices", "devices" },        { "/thermostats", "thermostats" },
    { "/configThermostats", "configThermostats" }, { "/editThermostat", "editThermostat" },
    { "/configLora", "configLora" },        { "/configLoraSettings", "configLoraSettings" },
    { "/loraOta", "loraOta" },              { "/configGeneral", "configGeneral" },
    { "/configZigbee", "configZigbee" },    { "/configHorloge", "configHorloge" },
    { "/configEnergy", "configEnergy" },    { "/configNotifications", "configNotifications" },
    { "/configGaz", "configEnergy" },       { "/configWater", "configEnergy" },
    { "/configMQTT", "mqtt" },              { "/configHTTP", "configHTTP" },
    { "/configRules", "configRules" },      { "/editRule", "editRule" },
    { "/addRule", "addRule" },              { "/configActionGroups", "configActionGroups" },
    { "/configWebPush", "webpush" },        { "/configTunnel", "configTunnel" },
    { "/configUdpClient", "configUdp" },    { "/configNotifMail", "configNotifMail" },
    { "/configWiFi", "wifi" },              { "/configDevices", "configDevices" },
    { "/configDevice", "device" },          { "/assistDevice", "assistDevice" },
    { "/update", "update" },                { "/backup", "backup" },
    { "/notifications", "notifications" },  { "/tools", "tools" },
    { "/help", "help" },                    { "/ota", "ota" },
    { "/logs", "logs" },                    { "/debugFiles", "files?dir=debug" },
    { "/configFiles", "files?dir=config" }, { "/fsbrowser", "files?dir=db" },
    { "/fsbrowserBackup", "files?dir=bk" }, { "/hst", "files?dir=hst" },
    { "/tp", "files?dir=tp" },              { "/rules", "files?dir=config&only=rules" },
    { "/javascript", "files?dir=web/js" },  { "/createDevice", "files?dir=db" },
    { "/createHistory", "files?dir=hst" },  { "/createTemplate", "files?dir=tp" },
    { "/filesManager", "files?dir=config" }, { "/reboot", "system" },
};

/** Redirection d'une ancienne page ; false si l'URI n'en est pas une. */
static bool legacy_redirect(httpd_req_t *req, const char *path)
{
    if (strcmp(path, "/web/backup.tar") == 0) {
        // Lien "Telecharger" des sauvegardes de la LiXee-Box
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/api/maint/backup/download?name=backup.tar");
        httpd_resp_send(req, NULL, 0);
        return true;
    }
    for (size_t i = 0; i < sizeof(LEGACY_PAGES) / sizeof(LEGACY_PAGES[0]); i++) {
        if (strcmp(path, LEGACY_PAGES[i].page) != 0) continue;
        static char loc[CONFIG_HTTPD_MAX_URI_LEN + 48];   // tache HTTP seule
        const char *q = strchr(req->uri, '?');
        const char *route = LEGACY_PAGES[i].route;
        snprintf(loc, sizeof(loc), "/#%s%s%s", route, q ? (strchr(route, '?') ? "&" : "?") : "",
                 q ? q + 1 : "");
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", loc);
        httpd_resp_send(req, NULL, 0);
        return true;
    }
    return false;
}

static esp_err_t static_file_handler(httpd_req_t *req)
{
    char filepath[CONFIG_HTTPD_MAX_URI_LEN + 16];

    // Copier l'URI et supprimer la query string (?xxx) et le fragment (#xxx)
    char uri_clean[CONFIG_HTTPD_MAX_URI_LEN];
    strncpy(uri_clean, req->uri, sizeof(uri_clean) - 1);
    uri_clean[sizeof(uri_clean) - 1] = '\0';
    char *q = strchr(uri_clean, '?');
    if (q) *q = '\0';
    char *h = strchr(uri_clean, '#');
    if (h) *h = '\0';

    // Valider AVANT de construire le chemin, sur l'URI deja debarrassee de sa
    // query string et de son fragment : le controle ne porte donc que sur la
    // partie qui sera effectivement concatenee au prefixe /littlefs/web.
    if (!uri_path_is_safe(uri_clean)) {
        ESP_LOGW(TAG, "Traversee de repertoire refusee: %s", req->uri);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    const char *uri = uri_clean;

    // Redirect / -> /index.html
    if (strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }

    // Construire le chemin LittleFS
    snprintf(filepath, sizeof(filepath), "/littlefs/web%s", uri);

    // Essayer d'abord la version .gz
    char gz_path[CONFIG_HTTPD_MAX_URI_LEN + 20];
    snprintf(gz_path, sizeof(gz_path), "%s.gz", filepath);

    FILE *f = fopen(gz_path, "r");
    bool is_gzip = (f != NULL);

    if (!f) {
        f = fopen(filepath, "r");
    }

    if (!f) {
        if (legacy_redirect(req, uri)) return ESP_OK;
        ESP_LOGW(TAG, "File not found: %s (uri=%s)", filepath, req->uri);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    // Content-Type (base sur le chemin original, pas le .gz)
    httpd_resp_set_type(req, get_content_type(filepath));

    // Pas de cache — le proxy shelf_proxy de LiXee-Assist peut cacher
    // des reponses partielles/corrompues, ce qui oblige a rafraichir
    // plusieurs fois pour que la page se charge correctement.
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    // Forcer Connection: close — le proxy shelf_proxy auto-decompress
    // le gzip et re-encode en chunked. Sur une connexion keep-alive,
    // le stream decompresse peut ne jamais signaler sa fin, ce qui
    // bloque le WebView indefiniment. Connection: close force le proxy
    // a detecter la fin de reponse via la fermeture de la socket.
    httpd_resp_set_hdr(req, "Connection", "close");

    // Si c'est un .gz, indiquer Content-Encoding: gzip
    if (is_gzip) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }

    // Lire le fichier entier en memoire et envoyer avec Content-Length
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 512 * 1024) {
        ESP_LOGE(TAG, "File too large or empty: %s (%ld bytes)", filepath, file_size);
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "File too large");
        return ESP_FAIL;
    }

    char *file_buf = (char *)malloc(file_size);
    if (!file_buf) {
        ESP_LOGE(TAG, "Failed to allocate %ld bytes for %s", file_size, filepath);
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    size_t read_bytes = fread(file_buf, 1, file_size, f);
    fclose(f);

    ESP_LOGI(TAG, "Serving: %s (%lu bytes%s)", uri, (unsigned long)read_bytes,
             is_gzip ? ", gzip" : "");

    esp_err_t err = httpd_resp_send(req, file_buf, read_bytes);
    free(file_buf);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_resp_send failed for %s: %s", filepath, esp_err_to_name(err));
    }

    // Forcer la fermeture de la socket apres envoi.
    // Le header "Connection: close" est un hint pour le client mais
    // esp_http_server ne ferme PAS la socket tout seul — il reste en
    // keep-alive et attend une nouvelle requete. Le proxy shelf_proxy
    // de LiXee-Assist ne detecte la fin du stream gzip decompresse
    // que via le EOF de la socket. Sans fermeture explicite, le proxy
    // attend indefiniment et le WebView bloque.
    httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));

    return err;
}

// ============================================================================
// Init
// ============================================================================

esp_err_t web_server_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 160;  // ~130 enregistres (API, alias de la LiXee-Box) + marge
    config.max_resp_headers = 12;   // CORS + cache + cookie + disposition
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;

    // LiXee-Assist utilise un proxy HTTP (shelf_proxy) qui ouvre
    // plusieurs connexions keep-alive en parallele. LRU purge recycle
    // la socket la plus ancienne quand toutes sont occupees.
    config.lru_purge_enable = true;
    config.send_wait_timeout = 20;   // Default=5s, augmente pour gros fichiers
    config.recv_wait_timeout = 20;   // Default=5s

    ESP_LOGI(TAG, "Starting web server on port %d", config.server_port);

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Connexion / deconnexion : hors trampoline, accessibles sans session
    httpd_uri_t login_get = {};
    login_get.uri = "/login";  login_get.method = HTTP_GET;  login_get.handler = login_get_handler;
    httpd_uri_t login_post = {};
    login_post.uri = "/login"; login_post.method = HTTP_POST; login_post.handler = login_post_handler;
    httpd_uri_t logout = {};
    logout.uri = "/logout";    logout.method = HTTP_GET;     logout.handler = logout_handler;
    httpd_register_uri_handler(s_server, &login_get);
    httpd_register_uri_handler(s_server, &login_post);
    httpd_register_uri_handler(s_server, &logout);
    httpd_uri_t opts = {};
    opts.uri = "/*";          opts.method = HTTP_OPTIONS;   opts.handler = options_handler;
    httpd_register_uri_handler(s_server, &opts);

    // Enregistrer les handlers API
    web_api_register_handlers(s_server);

    // Handler generique pour les fichiers statiques (wildcard, doit etre le dernier)
    httpd_uri_t static_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = static_file_handler,
        .user_ctx = NULL,
    };
    web_server_register_uri(s_server, &static_uri);

    // Rendre l'etat de la protection visible : un serveur ouvert ne doit pas
    // l'etre silencieusement.
    const cfg_settings_t *st  = cfg_get_settings();
    const cfg_general_t  *gen = cfg_get_general();
    if (!st->enable_secure_http) {
        ESP_LOGW(TAG, "Authentification HTTP DESACTIVEE (enableSecureHttp=0) : "
                      "tous les endpoints sont anonymes");
    } else if (gen->user_http[0] == '\0' || gen->pass_http[0] == '\0') {
        ESP_LOGE(TAG, "Authentification HTTP demandee mais identifiants vides : "
                      "acces laisse ouvert");
    } else {
        ESP_LOGI(TAG, "Authentification HTTP active (Basic, utilisateur '%s')",
                 gen->user_http);
    }

    ESP_LOGI(TAG, "Web server started");
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    return ESP_OK;
}

httpd_handle_t web_server_get_handle(void)
{
    return s_server;
}
