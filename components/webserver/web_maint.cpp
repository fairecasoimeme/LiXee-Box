// ============================================================================
// Routes de maintenance - voir web_maint.h
// Le travail (tar, OTA, telechargement) est dans components/maintenance ; ce
// fichier lit les requetes, recoit les corps en flux et met en forme les
// reponses.
//
// Differences avec la LiXee-Box :
//  - les envois (/doUpdate, /doRestore) sont des corps BRUTS
//    (application/octet-stream), lus par blocs de 4 Ko en PSRAM et transmis
//    au fur et a mesure : esp_http_server n'a pas d'analyseur multipart, et
//    rien ne justifie de garder l'image en memoire ;
//  - /createBackupFile, /resetUpdateStatus, /downloadUpdate modifiaient
//    l'etat en GET ou sans controle : tout ce qui modifie passe ici par POST
//    (donc par la protection CSRF de web_server_register_uri) ;
//  - les morceaux du tunnel sont numerotes et verifies (?n=) : un morceau
//    rejoue apres une reponse perdue est accepte sans etre ecrit deux fois,
//    un morceau manquant est refuse au lieu de corrompre l'image.
// ============================================================================

#include "web_maint.h"
#include "web_server.h"
#include "maintenance.h"
#include "config_manager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "mbedtls/base64.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static const char *TAG = "WEB_MAINT";

#define IO_BUF        4096
#define RECV_RETRIES  3

// ----------------------------------------------------------------------------
// Outils
// ----------------------------------------------------------------------------

static void *buf_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

/** Chemin de l'URI sans la query string. */
static bool path_is(httpd_req_t *req, const char *path)
{
    size_t n = strcspn(req->uri, "?");
    return n == strlen(path) && strncmp(req->uri, path, n) == 0;
}

static void query_param(httpd_req_t *req, const char *key, char *out, size_t len)
{
    char q[160];
    out[0] = '\0';
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        if (httpd_query_key_value(q, key, out, len) != ESP_OK) out[0] = '\0';
    }
}

static esp_err_t send_json_owned(httpd_req_t *req, char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (!json) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"ok\":false,\"message\":\"Memoire insuffisante\"}");
    }
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

/**
 * Reponse {"status":"ok"|"error","ok":bool,"message":...} (+ "ok" et
 * "error" : noms des reponses de la LiXee-Box, lus par le tunnel).
 */
static esp_err_t send_result(httpd_req_t *req, const char *http_status, const char *msg,
                             const char *extra_key = NULL, double extra_val = 0, bool reboot = false)
{
    bool ok = strncmp(http_status, "200", 3) == 0;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", ok ? "ok" : "error");
    cJSON_AddBoolToObject(root, "ok", ok);
    cJSON_AddStringToObject(root, "message", msg ? msg : "");
    if (!ok) cJSON_AddStringToObject(root, "error", msg ? msg : "");
    if (extra_key) cJSON_AddNumberToObject(root, extra_key, extra_val);
    if (reboot) cJSON_AddBoolToObject(root, "reboot", true);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_status(req, http_status);
    return send_json_owned(req, json);
}

/** Statut HTTP d'une erreur du composant. */
static const char *err_status(esp_err_t e)
{
    switch (e) {
    case ESP_ERR_INVALID_STATE:  return "409 Conflict";
    case ESP_ERR_NOT_FOUND:      return "404 Not Found";
    case ESP_ERR_NO_MEM:         return "507 Insufficient Storage";
    case ESP_ERR_NOT_SUPPORTED:  return "501 Not Implemented";
    case ESP_ERR_INVALID_SIZE:   return "413 Content Too Large";
    default:                     return "400 Bad Request";
    }
}

/** Corps JSON court (<= 1 Ko). */
static cJSON *read_body_json(httpd_req_t *req)
{
    int len = req->content_len;
    if (len <= 0 || len > 1024) return NULL;
    char *buf = (char *)malloc(len + 1);
    if (!buf) return NULL;
    int got = 0, timeouts = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= RECV_RETRIES) continue;
        if (r <= 0) {
            free(buf);
            return NULL;
        }
        got += r;
    }
    buf[got] = '\0';
    cJSON *doc = cJSON_Parse(buf);
    free(buf);
    return doc;
}

/** Champ "name" (ou "filename", nom LiXee-Box) d'un corps JSON. */
static bool body_name(httpd_req_t *req, char *out, size_t len)
{
    cJSON *doc = read_body_json(req);
    const cJSON *n = doc ? cJSON_GetObjectItem(doc, "name") : NULL;
    if (!cJSON_IsString(n) && doc) n = cJSON_GetObjectItem(doc, "filename");
    bool ok = cJSON_IsString(n) && strlen(n->valuestring) < len;
    if (ok) strlcpy(out, n->valuestring, len);
    cJSON_Delete(doc);
    return ok;
}

// ----------------------------------------------------------------------------
// Etat
// ----------------------------------------------------------------------------

static esp_err_t status_handler(httpd_req_t *req)
{
    return send_json_owned(req, maintenance_status_json());
}

static esp_err_t status_reset_handler(httpd_req_t *req)
{
    maintenance_reset_status();
    return send_result(req, "200 OK", "OK");
}

static esp_err_t abort_handler(httpd_req_t *req)
{
    maint_session_abort(MAINT_SESSION_CURRENT, "Annul\xC3\xA9 par l'utilisateur");   // la session active, quelle qu'elle soit
    return send_result(req, "200 OK", "Operation annulee");
}

// ----------------------------------------------------------------------------
// Sauvegardes
// ----------------------------------------------------------------------------

/** Puits tampon vers la reponse HTTP en morceaux (evite les petits segments). */
typedef struct {
    httpd_req_t *req;
    uint8_t *buf;
    size_t len;
} http_sink_t;

static esp_err_t http_sink_flush(http_sink_t *s)
{
    if (s->len == 0) return ESP_OK;
    esp_err_t e = httpd_resp_send_chunk(s->req, (const char *)s->buf, s->len);
    s->len = 0;
    return e;
}

static esp_err_t http_sink(void *ctx, const void *data, size_t len)
{
    http_sink_t *s = (http_sink_t *)ctx;
    const uint8_t *p = (const uint8_t *)data;
    while (len > 0) {
        size_t n = IO_BUF - s->len;
        if (n > len) n = len;
        memcpy(s->buf + s->len, p, n);
        s->len += n;
        p += n;
        len -= n;
        if (s->len == IO_BUF) {
            esp_err_t e = http_sink_flush(s);
            if (e != ESP_OK) return e;
        }
    }
    return ESP_OK;
}

static esp_err_t backup_list_handler(httpd_req_t *req)
{
    return send_json_owned(req, maint_backup_list_json());
}

static esp_err_t backup_create_handler(httpd_req_t *req)
{
    char err[128] = "";
    size_t size = 0;
    esp_err_t e = maint_backup_create(&size, err, sizeof(err));
    if (e != ESP_OK) return send_result(req, err_status(e), err[0] ? err : "Echec de la sauvegarde");
    return send_result(req, "200 OK", MAINT_BACKUP_NAME, "size", (double)size);
}

static esp_err_t backup_download_handler(httpd_req_t *req)
{
    char name[72], path[128], inl[4];
    query_param(req, "name", name, sizeof(name));
    query_param(req, "inline", inl, sizeof(inl));
    if (!maint_backup_path(name, path, sizeof(path))) return send_result(req, "400 Bad Request", "Nom de fichier invalide");
    FILE *f = fopen(path, "r");
    if (!f) return send_result(req, "404 Not Found", "Fichier introuvable");
    uint8_t *buf = (uint8_t *)buf_alloc(IO_BUF);
    if (!buf) {
        fclose(f);
        return send_result(req, "500 Internal Server Error", "Memoire insuffisante");
    }
    size_t nl = strlen(name);
    bool is_json = nl >= 5 && strcasecmp(name + nl - 5, ".json") == 0;
    httpd_resp_set_type(req, is_json ? "application/json" : "application/x-tar");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char disp[112];   // nom valide : [A-Za-z0-9._-] uniquement, sans guillemet
    snprintf(disp, sizeof(disp), "%s; filename=\"%s\"", (is_json && inl[0] == '1') ? "inline" : "attachment", name);
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    esp_err_t e = ESP_OK;
    size_t n;
    while (e == ESP_OK && (n = fread(buf, 1, IO_BUF, f)) > 0) {
        e = httpd_resp_send_chunk(req, (const char *)buf, n);
    }
    fclose(f);
    free(buf);
    if (e != ESP_OK) return ESP_FAIL;   // client parti : connexion fermee
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t backup_export_handler(httpd_req_t *req)
{
    http_sink_t s = {};
    s.req = req;
    s.buf = (uint8_t *)buf_alloc(IO_BUF);
    if (!s.buf) return send_result(req, "500 Internal Server Error", "Memoire insuffisante");
    char date[24] = "", disp[96];
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    strftime(date, sizeof(date), "%Y%m%d-%H%M", &t);
    snprintf(disp, sizeof(disp), "attachment; filename=\"lixee_backup_%s.tar\"", date);
    httpd_resp_set_type(req, "application/x-tar");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    esp_err_t e = maint_backup_stream(http_sink, &s);
    if (e == ESP_OK) e = http_sink_flush(&s);
    free(s.buf);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "Export interrompu : %s", esp_err_to_name(e));
        return ESP_FAIL;   // en-tetes deja partis : seule la fermeture signale l'echec
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t backup_delete_handler(httpd_req_t *req)
{
    char name[72];
    if (!body_name(req, name, sizeof(name))) return send_result(req, "400 Bad Request", "Parametre name manquant");
    esp_err_t e = maint_backup_delete(name);
    if (e == ESP_ERR_INVALID_ARG) return send_result(req, "400 Bad Request", "Nom de fichier invalide");
    if (e != ESP_OK) return send_result(req, "404 Not Found", "Fichier introuvable");
    return send_result(req, "200 OK", "Supprime");
}

static esp_err_t backup_restore_json_handler(httpd_req_t *req)
{
    char name[72], err[128] = "";
    if (!body_name(req, name, sizeof(name))) return send_result(req, "400 Bad Request", "Parametre name manquant");
    esp_err_t e = maint_backup_restore_json(name, err, sizeof(err));
    if (e != ESP_OK) return send_result(req, err_status(e), err);
    // Applique au prochain demarrage (voir maint_backup_restore_json)
    return send_result(req, "200 OK", "Fichier pret : appliqu\xC3\xA9 au red\xC3\xA9marrage", NULL, 0, true);
}

// ----------------------------------------------------------------------------
// Envoi direct (corps brut)
// ----------------------------------------------------------------------------

static esp_err_t upload_raw(httpd_req_t *req, maint_kind_t kind)
{
    char ctype[64] = "", err[160] = "";
    httpd_req_get_hdr_value_str(req, "Content-Type", ctype, sizeof(ctype));
    if (strncasecmp(ctype, "multipart/", 10) == 0) {
        return send_result(req, "415 Unsupported Media Type",
                           "Envoyer le fichier brut (application/octet-stream), pas un formulaire multipart");
    }
    size_t total = req->content_len;
    if (total == 0) return send_result(req, "400 Bad Request", "Corps vide");

    // Identifiant de la session ouverte ici : les appels suivants ne peuvent
    // agir que sur elle (pas sur une session ouverte apres son annulation)
    uint32_t sid = 0;
    esp_err_t e = maint_session_begin(kind, MAINT_CH_MANUAL, total, &sid, err, sizeof(err));
    if (e != ESP_OK) return send_result(req, err_status(e), err);

    uint8_t *buf = (uint8_t *)buf_alloc(IO_BUF);
    if (!buf) {
        maint_session_abort(sid, "Memoire insuffisante");
        return send_result(req, "500 Internal Server Error", "Memoire insuffisante");
    }
    size_t got = 0;
    int timeouts = 0;
    while (got < total) {
        size_t want = total - got < IO_BUF ? total - got : IO_BUF;
        int r = httpd_req_recv(req, (char *)buf, want);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= RECV_RETRIES) continue;
        if (r <= 0) {
            free(buf);
            maint_session_abort(sid, "Envoi interrompu");
            ESP_LOGW(TAG, "Envoi interrompu apres %u/%u octets", (unsigned)got, (unsigned)total);
            return ESP_FAIL;   // socket deja en erreur
        }
        timeouts = 0;
        e = maint_session_feed(sid, buf, (size_t)r, err, sizeof(err));
        if (e != ESP_OK) {
            free(buf);
            // Session annulee ; la suite du corps n'est pas lue : la reponse
            // part puis la connexion est fermee (ESP_FAIL)
            send_result(req, err_status(e), err);
            return ESP_FAIL;
        }
        got += (size_t)r;
    }
    free(buf);
    e = maint_session_finish(sid, err, sizeof(err));
    if (e != ESP_OK) return send_result(req, err_status(e), err);
    return send_result(req, "200 OK", err, NULL, 0, true);
}

static esp_err_t restore_handler(httpd_req_t *req)  { return upload_raw(req, MAINT_KIND_RESTORE); }
static esp_err_t firmware_handler(httpd_req_t *req) { return upload_raw(req, MAINT_KIND_FIRMWARE); }

// ----------------------------------------------------------------------------
// Envoi par morceaux (tunnel : trames limitees a 15 Ko)
// ----------------------------------------------------------------------------

static esp_err_t chunked_init(httpd_req_t *req, maint_kind_t kind)
{
    cJSON *doc = read_body_json(req);
    const cJSON *ts = doc ? cJSON_GetObjectItem(doc, "totalSize") : NULL;
    const cJSON *fn = doc ? cJSON_GetObjectItem(doc, "filename") : NULL;
    bool fs_image = cJSON_IsString(fn) &&
                    (strstr(fn->valuestring, "spiffs") || strstr(fn->valuestring, "littlefs"));
    double total = cJSON_IsNumber(ts) ? ts->valuedouble : -1;
    cJSON_Delete(doc);
    if (total <= 0 || total > 64.0 * 1024 * 1024) return send_result(req, "400 Bad Request", "totalSize manquant ou invalide");
    if (kind == MAINT_KIND_FIRMWARE && fs_image) {
        // Image de systeme de fichiers (U_SPIFFS de la LiXee-Box) : non prise en
        // charge, elle effacerait configuration et appareils
        return send_result(req, "400 Bad Request", "Image de syst\xC3\xA8me de fichiers non prise en charge");
    }
    char err[160] = "";
    esp_err_t e = maint_session_begin(kind, MAINT_CH_MANUAL, (size_t)total, NULL, err, sizeof(err));
    if (e != ESP_OK) return send_result(req, err_status(e), err);
    return send_result(req, "200 OK", "OK", "next", 0);
}

static esp_err_t chunked_chunk(httpd_req_t *req, maint_kind_t kind)
{
    char nstr[12];
    query_param(req, "n", nstr, sizeof(nstr));
    char *end = NULL;
    unsigned long n = strtoul(nstr, &end, 10);
    if (!nstr[0] || (end && *end)) return send_result(req, "400 Bad Request", "Parametre n manquant");

    uint32_t next = 0, sid = 0;
    if (!maint_session_expected_chunk(kind, &next, &sid)) {
        return send_result(req, "409 Conflict", "Aucun envoi en cours (init manquant, annul\xC3\xA9 ou expir\xC3\xA9)");
    }
    if (n + 1 == next) {
        // Morceau rejoue (reponse precedente perdue) : deja ecrit
        return send_result(req, "200 OK", "duplicate", "next", next);
    }
    if (n != next) return send_result(req, "409 Conflict", "Morceau inattendu", "expected", next);

    size_t len = req->content_len;
    if (len == 0 || len > MAINT_CHUNK_MAX_B64) return send_result(req, "413 Content Too Large", "Morceau vide ou trop grand");

    char *b64 = (char *)buf_alloc(len + 1);
    size_t raw_cap = len / 4 * 3 + 3;
    uint8_t *raw = (uint8_t *)buf_alloc(raw_cap);
    if (!b64 || !raw) {
        free(b64);
        free(raw);
        return send_result(req, "500 Internal Server Error", "Memoire insuffisante");
    }
    size_t got = 0;
    int timeouts = 0;
    while (got < len) {
        int r = httpd_req_recv(req, b64 + got, len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= RECV_RETRIES) continue;
        if (r <= 0) break;
        got += (size_t)r;
    }
    if (got < len) {
        free(b64);
        free(raw);
        return ESP_FAIL;   // morceau incomplet : le client le renverra avec le meme n
    }
    size_t olen = 0;
    int rc = mbedtls_base64_decode(raw, raw_cap, &olen, (const unsigned char *)b64, len);
    free(b64);
    if (rc != 0 || olen == 0) {
        free(raw);
        return send_result(req, "400 Bad Request", "Morceau base64 invalide");
    }
    char err[160] = "";
    // sid : la session verifiee ci-dessus, pas une autre ouverte entre-temps
    esp_err_t e = maint_session_feed(sid, raw, olen, err, sizeof(err));
    free(raw);
    if (e != ESP_OK) return send_result(req, err_status(e), err);
    maint_session_chunk_done();
    return send_result(req, "200 OK", "OK", "next", next + 1);
}

static esp_err_t chunked_finish(httpd_req_t *req, maint_kind_t kind)
{
    uint32_t sid = 0;
    if (!maint_session_expected_chunk(kind, NULL, &sid)) {
        return send_result(req, "409 Conflict", "Aucun envoi en cours");
    }
    char err[160] = "";
    esp_err_t e = maint_session_finish(sid, err, sizeof(err));
    if (e != ESP_OK) return send_result(req, err_status(e), err);
    return send_result(req, "200 OK", err, NULL, 0, true);
}

// ----------------------------------------------------------------------------
// Mise a jour automatique
// ----------------------------------------------------------------------------

static esp_err_t auto_config_get_handler(httpd_req_t *req)
{
    return send_json_owned(req, maint_auto_config_json());
}

static esp_err_t auto_config_post_handler(httpd_req_t *req)
{
    // Source fixe (release GitHub de la LiXee-Box) : choisir la source du
    // firmware reviendrait a choisir le code execute. Refuse, mode developpeur
    // compris.
    return send_result(req, "403 Forbidden", "Source des mises \xC3\xA0 jour fixe : non modifiable");
}

static esp_err_t auto_start_handler(httpd_req_t *req)
{
    char err[160] = "";
    esp_err_t e = maint_auto_start(err, sizeof(err));
    if (e != ESP_OK) return send_result(req, err_status(e), err);
    return send_result(req, "200 OK", "T\xC3\xA9l\xC3\xA9" "chargement d\xC3\xA9marr\xC3\xA9");
}

// ----------------------------------------------------------------------------
// Aiguillage
// ----------------------------------------------------------------------------

typedef struct {
    const char *path;
    esp_err_t (*fn)(httpd_req_t *);
} route_t;

static esp_err_t restore_init(httpd_req_t *r)    { return chunked_init(r, MAINT_KIND_RESTORE); }
static esp_err_t restore_chunk(httpd_req_t *r)   { return chunked_chunk(r, MAINT_KIND_RESTORE); }
static esp_err_t restore_finish(httpd_req_t *r)  { return chunked_finish(r, MAINT_KIND_RESTORE); }
static esp_err_t fw_init(httpd_req_t *r)         { return chunked_init(r, MAINT_KIND_FIRMWARE); }
static esp_err_t fw_chunk(httpd_req_t *r)        { return chunked_chunk(r, MAINT_KIND_FIRMWARE); }
static esp_err_t fw_finish(httpd_req_t *r)       { return chunked_finish(r, MAINT_KIND_FIRMWARE); }

static const route_t GET_ROUTES[] = {
    { "/api/maint/status",          status_handler },
    { "/api/maint/backup/list",     backup_list_handler },
    { "/api/maint/backup/download", backup_download_handler },
    { "/api/maint/backup/export",   backup_export_handler },
    { "/api/maint/auto/config",     auto_config_get_handler },
};

static const route_t POST_ROUTES[] = {
    { "/api/maint/status/reset",         status_reset_handler },
    { "/api/maint/abort",                abort_handler },
    { "/api/maint/backup/create",        backup_create_handler },
    { "/api/maint/backup/delete",        backup_delete_handler },
    { "/api/maint/backup/restore-json",  backup_restore_json_handler },
    { "/api/maint/restore",              restore_handler },
    { "/api/maint/restore/init",         restore_init },
    { "/api/maint/restore/chunk",        restore_chunk },
    { "/api/maint/restore/finish",       restore_finish },
    { "/api/maint/firmware",             firmware_handler },
    { "/api/maint/firmware/init",        fw_init },
    { "/api/maint/firmware/chunk",       fw_chunk },
    { "/api/maint/firmware/finish",      fw_finish },
    { "/api/maint/auto/config",          auto_config_post_handler },
    { "/api/maint/auto/start",           auto_start_handler },
};

static esp_err_t dispatch(httpd_req_t *req, const route_t *routes, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (path_is(req, routes[i].path)) return routes[i].fn(req);
    }
    return send_result(req, "404 Not Found", "Route inconnue");
}

static esp_err_t maint_get_handler(httpd_req_t *req)
{
    return dispatch(req, GET_ROUTES, sizeof(GET_ROUTES) / sizeof(GET_ROUTES[0]));
}

static esp_err_t maint_post_handler(httpd_req_t *req)
{
    return dispatch(req, POST_ROUTES, sizeof(POST_ROUTES) / sizeof(POST_ROUTES[0]));
}

void web_maint_register(httpd_handle_t server)
{
    httpd_uri_t get = {};
    get.uri = "/api/maint/*";
    get.method = HTTP_GET;
    get.handler = maint_get_handler;
    web_server_register_uri(server, &get);

    httpd_uri_t post = {};
    post.uri = "/api/maint/*";
    post.method = HTTP_POST;
    post.handler = maint_post_handler;
    web_server_register_uri(server, &post);
}
