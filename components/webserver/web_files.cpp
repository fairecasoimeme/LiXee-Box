// ============================================================================
// API de fichiers des outils developpeur
//
// Remplace les editeurs de la LiXee-Box (configFiles, fsbrowser, hst, tp,
// rules, debugFiles, javascript et leurs routes saveFile*), avec trois
// differences voulues :
//  - les repertoires accessibles sont une liste fermee, et un nom de fichier
//    ne peut contenir ni '/' ni ".." : l'original n'en verifiait aucun pour la
//    plupart de ses enregistrements, et un nom forge ecrivait n'importe ou ;
//  - tout est reserve au mode developpeur : ces fichiers contiennent les mots
//    de passe (WiFi, MQTT, SMTP, WebPush, HTTP) ;
//  - en lecture, les champs de mot de passe des fichiers de /config sont
//    masques ; une valeur masquee renvoyee telle quelle a l'enregistrement
//    conserve le mot de passe en place.
// Les ecritures sont atomiques (cfg_write_file) et un .json doit etre valide.
// ============================================================================

#include "web_files.h"
#include "web_server.h"
#include "config_manager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "zigate_devices.h"
#include "device_setup.h"
#include "device_data.h"
#include "lora.h"
#include "energy_history.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "WEB_FILES";

#define FILES_ROOT       "/littlefs"
#define FILES_MAX_SIZE   (256 * 1024)   // tampon en PSRAM
#define PASS_MASK        "********"

static const char *const ALLOWED_DIRS[] = {
    "config", "db", "hst", "tp", "tpu", "debug", "bk", "web/js",
};

// ----------------------------------------------------------------------------
// Validation
// ----------------------------------------------------------------------------

static bool dir_allowed(const char *dir)
{
    for (size_t i = 0; i < sizeof(ALLOWED_DIRS) / sizeof(ALLOWED_DIRS[0]); i++) {
        if (strcmp(dir, ALLOWED_DIRS[i]) == 0) return true;
    }
    return false;
}

/** Nom de fichier simple : [A-Za-z0-9._-], sans ".." ni separateur. */
static bool name_valid(const char *name)
{
    size_t n = strlen(name);
    if (n == 0 || n > 64 || strstr(name, "..")) return false;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

static bool ends_with(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && strcasecmp(s + ls - lx, suffix) == 0;
}

static esp_err_t send_json_error(httpd_req_t *req, const char *status, const char *msg)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "{\"status\":\"error\",\"message\":\"%s\"}", msg);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

static bool dev_mode_ok(httpd_req_t *req)
{
    if (cfg_get_general()->developer_mode) return true;
    send_json_error(req, "403 Forbidden", "Mode developpeur requis (Parametres generaux)");
    return false;
}

/**
 * Lit dir et name de la query string et construit le chemin complet.
 * @param need_name false pour la liste d'un repertoire
 * @return false si une erreur a deja ete renvoyee
 */
static bool parse_target(httpd_req_t *req, bool need_name, char *dir, size_t dir_len,
                         char *name, size_t name_len, char *path, size_t path_len)
{
    char query[160];
    dir[0] = name[0] = '\0';
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "dir", dir, dir_len) != ESP_OK) {
        send_json_error(req, "400 Bad Request", "Parametre dir manquant");
        return false;
    }
    // "web/js" arrive encode "web%2Fjs" par certains clients
    if (strcasecmp(dir, "web%2Fjs") == 0) strcpy(dir, "web/js");
    if (!dir_allowed(dir)) {
        send_json_error(req, "400 Bad Request", "Repertoire non autorise");
        return false;
    }
    if (need_name) {
        if (httpd_query_key_value(query, "name", name, name_len) != ESP_OK || !name_valid(name)) {
            send_json_error(req, "400 Bad Request", "Nom de fichier invalide");
            return false;
        }
        if (ends_with(name, ".gz")) {
            send_json_error(req, "400 Bad Request", "Fichier compresse : non modifiable ici");
            return false;
        }
        snprintf(path, path_len, FILES_ROOT "/%s/%s", dir, name);
    } else {
        snprintf(path, path_len, FILES_ROOT "/%s", dir);
    }
    return true;
}

// ----------------------------------------------------------------------------
// Masquage des mots de passe (fichiers de /config)
// ----------------------------------------------------------------------------

static bool is_secret_key(const char *key)
{
    // passHTTP, passMQTT, passSMTP, passWebPush, password (WiFi), token (tunnel)...
    // tunnelToken (tunnel.json), key = cle AES d'un emetteur (lora.json)
    return strncasecmp(key, "pass", 4) == 0 || strcasecmp(key, "token") == 0 ||
           strcasecmp(key, "tokenTunnel") == 0 || strcasecmp(key, "tunnelToken") == 0 ||
           strcasecmp(key, "key") == 0;
}

static void mask_secrets(cJSON *node)
{
    for (cJSON *it = node ? node->child : NULL; it; it = it->next) {
        if (it->string && is_secret_key(it->string) && cJSON_IsString(it) && it->valuestring[0]) {
            cJSON_SetValuestring(it, PASS_MASK);
        } else if (cJSON_IsObject(it) || cJSON_IsArray(it)) {
            mask_secrets(it);
        }
    }
}

/**
 * Serveur d'un secret "passXxx" (servXxx, portXxx, tlsXxx, securityXxx)
 * different entre 'doc' et la version actuelle ?
 */
static bool secret_target_changed(const cJSON *doc, const cJSON *current, const char *secret_key)
{
    if (strncasecmp(secret_key, "pass", 4) != 0 || !secret_key[4]) return false;
    static const char *const PFX[] = { "serv", "port", "tls", "security" };
    char k[48];
    for (size_t i = 0; i < sizeof(PFX) / sizeof(PFX[0]); i++) {
        snprintf(k, sizeof(k), "%s%s", PFX[i], secret_key + 4);
        const cJSON *a = cJSON_GetObjectItem(doc, k), *b = cJSON_GetObjectItem(current, k);
        if (!a && !b) continue;
        char *sa = a ? cJSON_PrintUnformatted(a) : NULL;
        char *sb = b ? cJSON_PrintUnformatted(b) : NULL;
        bool diff = !sa || !sb || strcasecmp(sa, sb) != 0;
        free(sa);
        free(sb);
        if (diff) return true;
    }
    return false;
}

/**
 * Remet dans 'doc' les secrets laisses masques, depuis la version actuelle.
 * Les tableaux sont suivis par position (ex. emetteurs de lora.json).
 * false si un secret masque accompagne un serveur modifie : il partirait
 * vers ce serveur sans avoir ete ressaisi.
 */
static bool restore_secrets(cJSON *doc, const cJSON *current)
{
    int idx = 0;
    for (cJSON *it = doc ? doc->child : NULL; it; it = it->next, idx++) {
        const cJSON *cur = it->string ? cJSON_GetObjectItem(current, it->string)
                                      : (cJSON_IsArray(current) ? cJSON_GetArrayItem(current, idx) : NULL);
        if (it->string && is_secret_key(it->string) && cJSON_IsString(it) &&
            strcmp(it->valuestring, PASS_MASK) == 0 && cJSON_IsString(cur)) {
            if (secret_target_changed(doc, current, it->string)) return false;
            cJSON_SetValuestring(it, cur->valuestring);
        } else if ((cJSON_IsObject(it) && cJSON_IsObject(cur)) || (cJSON_IsArray(it) && cJSON_IsArray(cur))) {
            if (!restore_secrets(it, cur)) return false;
        }
    }
    return true;
}

/** Fichier jamais servi par l'API : sauvegardes (mots de passe), cles privees. */
static bool sensitive_name(const char *dir, const char *name)
{
    return strcmp(dir, "bk") == 0 || ends_with(name, ".tar") || ends_with(name, ".key");
}

static bool has_private_key(const char *buf, size_t len)
{
    static const char K[] = "PRIVATE KEY";
    for (size_t i = 0; i + sizeof(K) - 1 <= len; i++) {
        if (memcmp(buf + i, K, sizeof(K) - 1) == 0) return true;
    }
    return false;
}

// ----------------------------------------------------------------------------
// Lecture d'un fichier en PSRAM
// ----------------------------------------------------------------------------

static char *read_whole_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > FILES_MAX_SIZE) {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM);
    if (!buf) buf = (char *)malloc(size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, size, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

// ----------------------------------------------------------------------------
// Handlers
// ----------------------------------------------------------------------------

static esp_err_t files_list_handler(httpd_req_t *req)
{
    if (!dev_mode_ok(req)) return ESP_OK;
    char dir[16], name[72], path[128];
    if (!parse_target(req, false, dir, sizeof(dir), name, sizeof(name), path, sizeof(path))) return ESP_OK;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "dir", dir);
    cJSON *arr = cJSON_AddArrayToObject(root, "files");
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_type == DT_DIR) continue;
            // Fichiers compresses (interface) : non editables en texte
            if (ends_with(e->d_name, ".gz")) continue;
            // Historiques : seuls les .json s'editent (pw24.bin, courbe de
            // puissance binaire, n'a rien a faire dans l'outil)
            if (strcmp(dir, "hst") == 0 && !ends_with(e->d_name, ".json")) continue;
            char fpath[400];   // d_name peut atteindre 255 octets
            snprintf(fpath, sizeof(fpath), "%s/%s", path, e->d_name);
            struct stat st;
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "name", e->d_name);
            cJSON_AddNumberToObject(o, "size", stat(fpath, &st) == 0 ? st.st_size : 0);
            cJSON_AddItemToArray(arr, o);
        }
        closedir(d);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json ? json : "{}");
    free(json);
    return ESP_OK;
}

static esp_err_t files_read_handler(httpd_req_t *req)
{
    if (!dev_mode_ok(req)) return ESP_OK;
    char dir[16], name[72], path[128];
    if (!parse_target(req, true, dir, sizeof(dir), name, sizeof(name), path, sizeof(path))) return ESP_OK;
    if (sensitive_name(dir, name)) return send_json_error(req, "403 Forbidden", "Fichier sensible : non lisible ici");

    size_t len = 0;
    char *buf = read_whole_file(path, &len);
    if (!buf) return send_json_error(req, "404 Not Found", "Fichier introuvable ou trop grand");
    if (has_private_key(buf, len)) {
        free(buf);
        return send_json_error(req, "403 Forbidden", "Fichier sensible : non lisible ici");
    }

    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (strcmp(dir, "config") == 0 && ends_with(name, ".json")) {
        cJSON *doc = cJSON_Parse(buf);
        char *masked = NULL;
        if (doc) {
            mask_secrets(doc);
            masked = cJSON_Print(doc);
            cJSON_Delete(doc);
        }
        free(buf);
        // Jamais le fichier brut (secrets), meme illisible
        if (!masked) return send_json_error(req, "500 Internal Server Error", "Fichier de configuration illisible");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, masked);
        free(masked);
        return ESP_OK;
    }
    httpd_resp_set_type(req, ends_with(name, ".json") ? "application/json" : "text/plain");
    httpd_resp_send(req, buf, len);
    free(buf);
    return ESP_OK;
}

/**
 * GET /readFile?<dir>&<fichier> (handleReadfile de la LiXee-Box) : les deux
 * premiers parametres de la requete, quels que soient leurs noms. Memes
 * repertoires et memes secrets masques que /api/files/read ; texte brut.
 */
static esp_err_t legacy_readfile_handler(httpd_req_t *req)
{
    // Meme regle que /api/files/read : mode developpeur
    if (!dev_mode_ok(req)) return ESP_OK;
    char q[160], dir[16] = "", name[72] = "";
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        char *save = NULL;
        int k = 0;
        for (char *tok = strtok_r(q, "&", &save); tok && k < 2; tok = strtok_r(NULL, "&", &save), k++) {
            char *eq = strchr(tok, '=');
            const char *v = eq ? eq + 1 : tok;
            if (k == 0) strlcpy(dir, v, sizeof(dir));
            else strlcpy(name, v, sizeof(name));
        }
    }
    if (strcasecmp(dir, "web%2Fjs") == 0) strcpy(dir, "web/js");
    // Sauvegardes (mots de passe en clair dans l'archive) jamais servies ici
    if (!dir_allowed(dir) || !name_valid(name) || sensitive_name(dir, name)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "Invalid path");
    }
    char path[128];
    snprintf(path, sizeof(path), FILES_ROOT "/%s/%s", dir, name);
    size_t len = 0;
    char *buf = read_whole_file(path, &len);
    if (!buf) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "Fichier introuvable");
    }
    if (has_private_key(buf, len)) {
        free(buf);
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "Fichier sensible");
    }
    // Texte brut : un fichier ne doit pas s'executer comme page du pont
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (strcmp(dir, "config") == 0 && ends_with(name, ".json")) {
        cJSON *doc = cJSON_Parse(buf);
        char *masked = NULL;
        if (doc) {
            mask_secrets(doc);
            masked = cJSON_Print(doc);
            cJSON_Delete(doc);
        }
        free(buf);
        if (!masked) return httpd_resp_sendstr(req, "");   // jamais le fichier brut (secrets)
        esp_err_t e = httpd_resp_sendstr(req, masked);
        free(masked);
        return e;
    }
    esp_err_t e = httpd_resp_send(req, buf, len);
    free(buf);
    return e;
}

/**
 * Recharge les templates de tous les appareils (Zigbee et LoRa) apres la
 * modification d'un fichier tp/ : le template est relu en place, sous le
 * verrou de device_data. Lecture flash : tache HTTP (pile en RAM interne).
 * @return nombre d'appareils dont le template a ete recharge
 */
static int reload_templates(void)
{
    int n = 0;
    int count = zigate_devices_count();
    for (int i = 0; i < count; i++) {
        const zigate_device_t *d = zigate_devices_get(i);
        if (d && d->active && device_setup_load_template_now(d->ieee_addr)) n++;
    }
    lora_emitter_info_t lem[LORA_MAX_EMITTERS];
    int nl = lora_get_emitters(lem, LORA_MAX_EMITTERS);
    for (int i = 0; i < nl; i++) {
        if (!lem[i].device_id[0]) continue;
        char file[16];
        snprintf(file, sizeof(file), "%s.json", lem[i].device_id);
        if (device_data_load_template(lem[i].ieee, file, lem[i].model) == ESP_OK) n++;
    }
    ESP_LOGI(TAG, "Templates recharges : %d appareil(s)", n);
    return n;
}

static esp_err_t files_write_handler(httpd_req_t *req)
{
    if (!dev_mode_ok(req)) return ESP_OK;
    char dir[16], name[72], path[128];
    if (!parse_target(req, true, dir, sizeof(dir), name, sizeof(name), path, sizeof(path))) return ESP_OK;

    size_t len = req->content_len;
    if (len > FILES_MAX_SIZE) return send_json_error(req, "413 Content Too Large", "Fichier trop grand");

    char *buf = (char *)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM);
    if (!buf) buf = (char *)malloc(len + 1);
    if (!buf) return send_json_error(req, "500 Internal Server Error", "Memoire insuffisante");

    size_t got = 0;
    int timeouts = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
        if (r <= 0) {
            free(buf);
            return send_json_error(req, "400 Bad Request", "Corps incomplet");
        }
        got += (size_t)r;
    }
    buf[len] = '\0';

    esp_err_t err;
    if (ends_with(name, ".json")) {
        cJSON *doc = cJSON_Parse(buf);
        if (!doc) {
            free(buf);
            return send_json_error(req, "400 Bad Request", "JSON invalide : fichier non enregistre");
        }
        if (strcmp(dir, "config") == 0) {
            // Secrets laisses masques : reprendre ceux du fichier actuel
            char *cur_txt = read_whole_file(path, NULL);
            cJSON *cur = cur_txt ? cJSON_Parse(cur_txt) : NULL;
            free(cur_txt);
            if (cur) {
                bool ok = restore_secrets(doc, cur);
                cJSON_Delete(cur);
                if (!ok) {
                    cJSON_Delete(doc);
                    free(buf);
                    return send_json_error(req, "400 Bad Request",
                                           "Serveur modifie : ressaisissez le mot de passe correspondant");
                }
            }
        }
        char *out = cJSON_Print(doc);
        cJSON_Delete(doc);
        err = out ? cfg_write_file(path, out) : ESP_ERR_NO_MEM;
        free(out);
    } else {
        err = cfg_write_file(path, buf);
    }
    free(buf);

    if (err != ESP_OK) return send_json_error(req, "500 Internal Server Error", "Echec d'ecriture");
    ESP_LOGI(TAG, "Fichier ecrit : %s (%u octets)", path, (unsigned)len);
    httpd_resp_set_type(req, "application/json");
    // Appareil ou historique edite / importe : relu en memoire tout de suite,
    // sinon la sauvegarde periodique (60 s) remettrait l'ancienne copie
    if ((strcmp(dir, "db") == 0 || strcmp(dir, "hst") == 0) && ends_with(name, ".json")) {
        const char *base = name;
        bool hst = strcmp(dir, "hst") == 0;
        if (hst && strncmp(name, "nrg_", 4) == 0) base = name + 4;
        uint64_t ieee = strlen(base) == 21 ? strtoull(base, NULL, 16) : 0;
        esp_err_t re = ESP_ERR_NOT_SUPPORTED;
        if (ieee && hst && base != name) re = energy_history_reload(ieee);
        else if (ieee && !hst) re = device_data_reload(ieee);
        if (re == ESP_OK) {
            return httpd_resp_sendstr(req, "{\"status\":\"ok\",\"rebootRequired\":false,\"reloaded\":1}");
        }
    }
    if (strcmp(dir, "tp") == 0 || strcmp(dir, "tpu") == 0) {
        // Template modifie : recharge a chaud des appareils (comme la LiXee-Box)
        int n = reload_templates();
        char msg[96];
        snprintf(msg, sizeof(msg), "{\"status\":\"ok\",\"rebootRequired\":false,\"reloaded\":%d}", n);
        return httpd_resp_sendstr(req, msg);
    }
    // La configuration en memoire n'est relue qu'au demarrage
    return httpd_resp_sendstr(req, "{\"status\":\"ok\",\"rebootRequired\":true}");
}

static esp_err_t files_delete_handler(httpd_req_t *req)
{
    if (!dev_mode_ok(req)) return ESP_OK;
    char dir[16], name[72], path[128];
    if (!parse_target(req, true, dir, sizeof(dir), name, sizeof(name), path, sizeof(path))) return ESP_OK;

    // Historique d'un compteur (hst/nrg_<IEEE>.json) : efface aussi en
    // memoire, sinon la sauvegarde periodique recreait le fichier avec
    // l'ancien contenu
    if (strcmp(dir, "hst") == 0 && strncmp(name, "nrg_", 4) == 0 &&
        strlen(name) == 4 + 16 + 5 && ends_with(name, ".json")) {
        uint64_t ieee = strtoull(name + 4, NULL, 16);
        if (ieee) energy_history_clear(ieee, false);
    }

    if (unlink(path) != 0) return send_json_error(req, "404 Not Found", "Fichier introuvable");
    ESP_LOGI(TAG, "Fichier supprime : %s", path);
    httpd_resp_set_type(req, "application/json");
    // Template supprime : les appareils reprennent l'autre dossier (tpu/ -> tp/)
    if ((strcmp(dir, "tp") == 0 || strcmp(dir, "tpu") == 0) && ends_with(name, ".json")) {
        char msg[64];
        snprintf(msg, sizeof(msg), "{\"status\":\"ok\",\"reloaded\":%d}", reload_templates());
        return httpd_resp_sendstr(req, msg);
    }
    return httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
}

void web_files_register(httpd_handle_t server)
{
    static const struct { const char *uri; httpd_method_t method; esp_err_t (*fn)(httpd_req_t *); } routes[] = {
        { "/api/files",        HTTP_GET,  files_list_handler   },
        { "/api/files/read",   HTTP_GET,  files_read_handler   },
        { "/api/files/write",  HTTP_POST, files_write_handler  },
        { "/api/files/delete", HTTP_POST, files_delete_handler },
        { "/readFile",         HTTP_GET,  legacy_readfile_handler },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_uri_t uri = {};
        uri.uri = routes[i].uri;
        uri.method = routes[i].method;
        uri.handler = routes[i].fn;
        web_server_register_uri(server, &uri);
    }
}
