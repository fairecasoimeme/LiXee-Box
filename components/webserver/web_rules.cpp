// ============================================================================
// Routes des regles et des groupes d'actions - voir web_rules.h
// Portage des routes /api/rules/add|edit|delete|toggle, /getRuleStatus, /rules
// (+ /saveFileRules), /getDevices + /getTemplates (catalogue de l'editeur) et
// /api/actiongroups/* de web.cpp. Le travail est fait par components/rules ; ce
// fichier lit les parametres et le corps, puis renvoie la reponse.
//
// Ecarts avec la LiXee-Box :
//   - reponses JSON {"status":"ok"|"error","message":...} au lieu de texte brut ;
//   - /saveFileRules prenait un nom de fichier fourni par le client (traversee de
//     chemin possible) et ecrivait sans validation : POST /api/rules/raw n'ecrit
//     que rules.json, apres validation de chaque regle ;
//   - run / delete des groupes acceptent un corps JSON {"index":n} ou un
//     formulaire "index=n" (format de la LiXee-Box).
// ============================================================================

#include "web_rules.h"
#include "web_server.h"
#include "rules.h"
#include "action_groups.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "cJSON.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "WEB_RULES";

#define BODY_MAX_RULE    (16 * 1024)
#define BODY_MAX_RAW     (192 * 1024)
#define BODY_MAX_GROUP   (8 * 1024)

// ----------------------------------------------------------------------------
// Outils
// ----------------------------------------------------------------------------

static esp_err_t send_json_owned(httpd_req_t *req, char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (!json) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Memoire insuffisante\"}");
    }
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

/** {"status":"ok"|"error","message":"..."} ; extra : membres JSON supplementaires ou NULL. */
static esp_err_t send_status(httpd_req_t *req, const char *status, const char *msg, const char *extra)
{
    cJSON *o = cJSON_CreateObject();
    if (o) {
        cJSON_AddStringToObject(o, "status", strncmp(status, "200", 3) == 0 ? "ok" : "error");
        cJSON_AddStringToObject(o, "message", msg ? msg : "");
    }
    char *txt = o ? cJSON_PrintUnformatted(o) : NULL;
    cJSON_Delete(o);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (!txt) return httpd_resp_sendstr(req, "{\"status\":\"error\"}");
    if (extra && extra[0]) {
        // Ajout de membres avant l'accolade finale ("ok":true,"sent":3 pour les groupes)
        size_t l = strlen(txt);
        size_t el = strlen(extra);
        char *buf = (char *)malloc(l + el + 2);
        if (buf) {
            memcpy(buf, txt, l - 1);
            buf[l - 1] = ',';
            memcpy(buf + l, extra, el);
            buf[l + el] = '}';
            buf[l + el + 1] = '\0';
            free(txt);
            txt = buf;
        }
    }
    esp_err_t err = httpd_resp_sendstr(req, txt);
    free(txt);
    return err;
}

static const char *res_status(rules_res_t r)
{
    switch (r) {
    case RULES_RES_OK:          return "200 OK";
    case RULES_RES_NOT_FOUND:   return "404 Not Found";
    case RULES_RES_CONFLICT:    return "409 Conflict";
    case RULES_RES_BAD_REQUEST:
    case RULES_RES_FULL:        return "400 Bad Request";
    default:                    return "500 Internal Server Error";
    }
}

/** Corps de la requete en PSRAM (termine par '\0'), NULL si absent ou trop gros. */
static char *read_body(httpd_req_t *req, size_t max, size_t *out_len)
{
    *out_len = 0;
    int len = req->content_len;
    if (len <= 0 || (size_t)len > max) return NULL;
    char *buf = (char *)heap_caps_malloc((size_t)len + 1, MALLOC_CAP_SPIRAM);
    if (!buf) buf = (char *)malloc((size_t)len + 1);
    if (!buf) return NULL;
    int got = 0, timeouts = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
        if (r <= 0) {
            free(buf);
            return NULL;
        }
        got += r;
    }
    buf[got] = '\0';
    *out_len = (size_t)got;
    return buf;
}

/** Decodage %XX et '+' d'un parametre d'URL ou de formulaire. */
static void url_decode(char *s)
{
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '+') {
            *o++ = ' ';
        } else if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
            char hx[3] = { p[1], p[2], 0 };
            *o++ = (char)strtol(hx, NULL, 16);
            p += 2;
        } else {
            *o++ = *p;
        }
    }
    *o = '\0';
}

static bool query_param(httpd_req_t *req, const char *key, char *out, size_t len)
{
    out[0] = '\0';
    size_t ql = httpd_req_get_url_query_len(req);
    if (ql == 0 || ql > 512) return false;
    char q[516];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return false;
    if (httpd_query_key_value(q, key, out, len) != ESP_OK) return false;
    url_decode(out);
    return true;
}

/** Partie de l'URI apres le prefixe, sans la query string. */
static void uri_suffix(httpd_req_t *req, const char *prefix, char *out, size_t len)
{
    const char *p = req->uri + strlen(prefix);
    size_t n = strcspn(p, "?#");
    if (n >= len) n = len - 1;
    memcpy(out, p, n);
    out[n] = '\0';
}

/** Champ texte d'un petit corps JSON ({"name":"..."}). */
static bool body_str(const char *body, size_t len, const char *key, char *out, size_t olen, bool *bval)
{
    cJSON *j = cJSON_ParseWithLength(body, len);
    if (!cJSON_IsObject(j)) {
        cJSON_Delete(j);
        return false;
    }
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    bool ok = cJSON_IsString(v) && v->valuestring;
    if (ok) strlcpy(out, v->valuestring, olen);
    if (bval) {
        const cJSON *e = cJSON_GetObjectItemCaseSensitive(j, "enabled");
        *bval = cJSON_IsBool(e) ? cJSON_IsTrue(e) : true;
    }
    cJSON_Delete(j);
    return ok;
}

/** "index" d'un corps JSON {"index":n} ou formulaire "index=n" ; -1 si absent. */
static int body_index(const char *body, size_t len)
{
    size_t i = 0;
    while (i < len && isspace((unsigned char)body[i])) i++;
    if (i < len && body[i] == '{') {
        cJSON *j = cJSON_ParseWithLength(body, len);
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "index");
        int r = -1;
        if (cJSON_IsNumber(v)) r = (int)v->valuedouble;
        else if (cJSON_IsString(v) && v->valuestring) r = atoi(v->valuestring);
        cJSON_Delete(j);
        return r;
    }
    char v[16];
    if (httpd_query_key_value(body, "index", v, sizeof(v)) == ESP_OK && v[0]) return atoi(v);
    return -1;
}

// ----------------------------------------------------------------------------
// Regles
// ----------------------------------------------------------------------------

static esp_err_t rules_get_handler(httpd_req_t *req)
{
    char op[24];
    uri_suffix(req, "/api/rules/", op, sizeof(op));

    if (!strcmp(op, "list")) return send_json_owned(req, rules_list_json());
    if (!strcmp(op, "devices")) return send_json_owned(req, rules_devices_json());
    if (!strcmp(op, "raw")) return send_json_owned(req, rules_raw_get());

    char name[RULES_NAME_LEN * 3];
    if (!strcmp(op, "get")) {
        if (!query_param(req, "name", name, sizeof(name)) || !name[0]) {
            return send_status(req, "400 Bad Request", "Nom de r\xC3\xA8gle manquant", NULL);
        }
        bool found = false;
        char *json = rules_get_json(name, &found);
        if (!found) return send_status(req, "404 Not Found", "R\xC3\xA8gle non trouv\xC3\xA9" "e", NULL);
        return send_json_owned(req, json);
    }
    if (!strcmp(op, "status")) {
        // Equivalent de /getRuleStatus ("etat|date" de la LiXee-Box) en JSON
        if (!query_param(req, "name", name, sizeof(name)) || !name[0]) {
            return send_status(req, "400 Bad Request", "Nom de r\xC3\xA8gle manquant", NULL);
        }
        int st = 0;
        char date[24];
        if (!rules_get_status(name, &st, date, sizeof(date))) {
            return send_status(req, "404 Not Found", "R\xC3\xA8gle non trouv\xC3\xA9" "e", NULL);
        }
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "status", st);
        cJSON_AddStringToObject(o, "lastDate", date);
        char *json = cJSON_PrintUnformatted(o);
        cJSON_Delete(o);
        return send_json_owned(req, json);
    }
    httpd_resp_send_404(req);
    return ESP_OK;
}

static esp_err_t rules_post_handler(httpd_req_t *req)
{
    char op[24];
    uri_suffix(req, "/api/rules/", op, sizeof(op));
    bool raw = !strcmp(op, "raw");
    size_t len = 0;
    char *body = read_body(req, raw ? BODY_MAX_RAW : BODY_MAX_RULE, &len);
    if (!body) return send_status(req, "400 Bad Request", "Corps de requ\xC3\xAAte absent ou trop gros", NULL);

    char err[160] = "";
    rules_res_t r = RULES_RES_OK;
    const char *ok_msg = "";
    if (!strcmp(op, "add")) {
        r = rules_add_json(body, len, err, sizeof(err));
        ok_msg = "R\xC3\xA8gle cr\xC3\xA9\xC3\xA9" "e";
    } else if (!strcmp(op, "edit")) {
        r = rules_edit_json(body, len, err, sizeof(err));
        ok_msg = "R\xC3\xA8gle modifi\xC3\xA9" "e";
    } else if (raw) {
        r = rules_raw_set(body, len, err, sizeof(err));
        ok_msg = "rules.json enregistr\xC3\xA9";
    } else if (!strcmp(op, "delete") || !strcmp(op, "toggle")) {
        char name[RULES_NAME_LEN + 8];
        bool enabled = true;
        if (!body_str(body, len, "name", name, sizeof(name), &enabled) || !name[0]) {
            free(body);
            return send_status(req, "400 Bad Request", "Nom de r\xC3\xA8gle manquant", NULL);
        }
        if (op[0] == 'd') {
            r = rules_delete(name);
            ok_msg = "R\xC3\xA8gle supprim\xC3\xA9" "e";
        } else {
            r = rules_set_enabled(name, enabled);
            ok_msg = enabled ? "R\xC3\xA8gle activ\xC3\xA9" "e" : "R\xC3\xA8gle d\xC3\xA9sactiv\xC3\xA9" "e";
        }
        if (r == RULES_RES_NOT_FOUND) strlcpy(err, "R\xC3\xA8gle non trouv\xC3\xA9" "e", sizeof(err));
        else if (r == RULES_RES_BAD_REQUEST) strlcpy(err, "rules.json illisible : corrigez-le dans l'\xC3\xA9" "diteur JSON", sizeof(err));
    } else {
        free(body);
        httpd_resp_send_404(req);
        return ESP_OK;
    }
    free(body);
    if (r != RULES_RES_OK) {
        ESP_LOGW(TAG, "/api/rules/%s : %s", op, err);
        return send_status(req, res_status(r), err[0] ? err : "Erreur", NULL);
    }
    return send_status(req, "200 OK", ok_msg, NULL);
}

// ----------------------------------------------------------------------------
// Groupes d'actions
// ----------------------------------------------------------------------------

static esp_err_t groups_get_handler(httpd_req_t *req)
{
    char op[24];
    uri_suffix(req, "/api/actiongroups/", op, sizeof(op));
    if (!strcmp(op, "list")) return send_json_owned(req, action_groups_list_json());
    if (!strcmp(op, "devices")) return send_json_owned(req, action_groups_devices_json());
    httpd_resp_send_404(req);
    return ESP_OK;
}

static esp_err_t groups_post_handler(httpd_req_t *req)
{
    char op[24];
    uri_suffix(req, "/api/actiongroups/", op, sizeof(op));
    size_t len = 0;
    char *body = read_body(req, BODY_MAX_GROUP, &len);
    if (!body) return send_status(req, "400 Bad Request", "Corps de requ\xC3\xAAte absent ou trop gros", "\"ok\":false");

    if (!strcmp(op, "run")) {
        int idx = body_index(body, len);
        free(body);
        if (idx < 0) return send_status(req, "400 Bad Request", "index manquant", "\"ok\":false");
        // Retour "optimiste" : confirme des que les commandes sont en file
        int sent = action_groups_run(idx);
        if (sent < 0) return send_status(req, "404 Not Found", "groupe introuvable ou d\xC3\xA9sactiv\xC3\xA9", "\"ok\":false");
        char extra[40];
        snprintf(extra, sizeof(extra), "\"ok\":true,\"sent\":%d", sent);
        return send_status(req, "200 OK", "Groupe d\xC3\xA9" "clench\xC3\xA9", extra);
    }
    if (!strcmp(op, "delete")) {
        int idx = body_index(body, len);
        free(body);
        rules_res_t r = action_groups_remove(idx);
        if (r != RULES_RES_OK) return send_status(req, res_status(r), "Suppression impossible", "\"ok\":false");
        return send_status(req, "200 OK", "Groupe supprim\xC3\xA9", "\"ok\":true");
    }
    if (!strcmp(op, "save")) {
        char err[96] = "";
        rules_res_t r = action_groups_save_json(body, len, err, sizeof(err));
        free(body);
        if (r != RULES_RES_OK) {
            // "error" : champ lu par l'editeur de la LiXee-Box
            char extra[140];
            snprintf(extra, sizeof(extra), "\"ok\":false,\"error\":\"%.96s\"", err);
            return send_status(req, res_status(r), err, extra);
        }
        return send_status(req, "200 OK", "Groupe enregistr\xC3\xA9", "\"ok\":true");
    }
    free(body);
    httpd_resp_send_404(req);
    return ESP_OK;
}

// ----------------------------------------------------------------------------
// Enregistrement
// ----------------------------------------------------------------------------

/**
 * GET /getRuleStatus?<nom> (LiXee-Box) : "etat|date" en texte, nom de la regle
 * en premier parametre quel que soit son nom.
 */
static esp_err_t legacy_rule_status_handler(httpd_req_t *req)
{
    char q[RULES_NAME_LEN * 3 + 16] = "", name[RULES_NAME_LEN * 3] = "";
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        char *v = strchr(q, '=');
        const char *src = v ? v + 1 : q;
        size_t n = strcspn(src, "&");
        // decodage du parametre (+, %xx)
        size_t o = 0;
        for (size_t i = 0; i < n && o + 1 < sizeof(name); i++) {
            if (src[i] == '+') { name[o++] = ' '; continue; }
            if (src[i] == '%' && i + 2 < n && isxdigit((unsigned char)src[i + 1]) && isxdigit((unsigned char)src[i + 2])) {
                char hx[3] = { src[i + 1], src[i + 2], 0 };
                name[o++] = (char)strtol(hx, NULL, 16);
                i += 2;
                continue;
            }
            name[o++] = src[i];
        }
        name[o] = '\0';
    }
    int st = 0;
    char date[24] = "";
    char out[48];
    if (name[0] && rules_get_status(name, &st, date, sizeof(date))) snprintf(out, sizeof(out), "%d|%s", st, date);
    else strlcpy(out, "0|", sizeof(out));
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, out);
}

void web_rules_register(httpd_handle_t server)
{
    static const struct { const char *uri; httpd_method_t method; esp_err_t (*fn)(httpd_req_t *); } routes[] = {
        { "/api/rules/*",        HTTP_GET,  rules_get_handler },
        { "/api/rules/*",        HTTP_POST, rules_post_handler },
        { "/api/actiongroups/*", HTTP_GET,  groups_get_handler },
        { "/api/actiongroups/*", HTTP_POST, groups_post_handler },
        { "/getRuleStatus",      HTTP_GET,  legacy_rule_status_handler },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_uri_t uri = {};
        uri.uri = routes[i].uri;
        uri.method = routes[i].method;
        uri.handler = routes[i].fn;
        web_server_register_uri(server, &uri);
    }
}
