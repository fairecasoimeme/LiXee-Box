// ============================================================================
// Routes du centre de notifications et de la configuration email
// (lot 4 du portage LiXee-Box : /notifications, /api/notifications*,
// /configNotifMail + /saveConfigNotificationMail, /generateNotif, /getAlert).
//
// Contrat :
//   GET  /api/notifications?page=1&limit=10   liste, plus recentes d'abord
//        -> {total,unread,offset,limit,page,notifications:[{id,title,message,
//            timeStamp,ts,type,viewed,alertType}]}
//   GET  /api/notifications/stats   (alias GET /api/stats)
//        -> {total,unread,read,maxCapacity,psramFree,psramTotal}
//   GET  /api/notifications/debug   -> {notifications:[{index,id,title,viewed}],total,...}
//   GET  /api/notifications/send-status -> {mail:{...},webpush:{...},queue:{...}}
//   POST /api/notifications            {title,message,type,alertType?} -> {status,id}
//   POST /api/notifications/read       {id} (ou ?id=)
//   POST /api/notifications/read-all
//   POST /api/notifications/delete     {id} (ou ?id=)
//   POST /api/notifications/clear
//   POST /api/notifications/test       notification de test (/generateNotif)
//   POST /api/notifications/test-webpush  envoi de test (resultat : send-status)
//   GET  /api/config/notifmail         configuration email (sans mot de passe)
//   POST /api/config/notifmail         {enableNotif,servSMTP,portSMTP,userSMTP,
//        passSMTP,clearPassSMTP,toSMTP,fromSMTP,securitySMTP,minTypeSMTP}
//   POST /api/config/notifmail/test    email de test (resultat : send-status)
//   GET  /getAlert                     texte "etat;message" ou vide (bandeaux)
//
// Differences avec la LiXee-Box : les actions designent un "id" stable et non
// l'index dans le vecteur ; PUT / DELETE et /generateNotif (GET qui modifiait
// l'etat) sont remplaces par des POST.
// ============================================================================

#include "web_notif.h"
#include "web_server.h"
#include "notifications.h"
#include "config_manager.h"
#include "esp_log.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "WEB_NOTIF";

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

static esp_err_t send_status(httpd_req_t *req, const char *status, const char *msg)
{
    char buf[200];
    bool ok = strncmp(status, "2", 1) == 0;
    snprintf(buf, sizeof(buf), "{\"status\":\"%s\",\"success\":%s,\"message\":\"%s\"}",
             ok ? "ok" : "error", ok ? "true" : "false", msg);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

/** Parametre de requete entier, ou valeur par defaut. */
static long query_long(httpd_req_t *req, const char *key, long def)
{
    char q[96], v[16];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return def;
    if (httpd_query_key_value(q, key, v, sizeof(v)) != ESP_OK || !v[0]) return def;
    char *end;
    long r = strtol(v, &end, 10);
    return end == v ? def : r;
}

/** Partie de l'URI apres le prefixe ("/api/notifications/read" -> "read"), sans la requete. */
static void uri_suffix(httpd_req_t *req, const char *prefix, char *out, size_t len)
{
    const char *u = req->uri;
    size_t pl = strlen(prefix);
    if (strncmp(u, prefix, pl) == 0) u += pl;
    if (*u == '/') u++;
    size_t n = strcspn(u, "?");
    if (n >= len) n = len - 1;
    memcpy(out, u, n);
    out[n] = '\0';
}

/**
 * Corps JSON (2 Ko maximum, tampon en tas : la pile HTTP fait 8 Ko).
 * Corps absent : objet vide. NULL (reponse deja envoyee) si invalide.
 */
static cJSON *read_body(httpd_req_t *req)
{
    int len = req->content_len;
    if (len <= 0) return cJSON_CreateObject();
    if (len > 2048) {
        send_status(req, "413 Content Too Large", "Corps trop volumineux");
        return NULL;
    }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        send_status(req, "500 Internal Server Error", "Memoire insuffisante");
        return NULL;
    }
    int got = 0, timeouts = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
        if (r <= 0) {
            free(buf);
            send_status(req, "400 Bad Request", "Corps incomplet");
            return NULL;
        }
        got += r;
    }
    buf[got] = '\0';
    // Objets plats attendus : refuser une imbrication profonde (cJSON est recursif)
    int depth = 0, maxd = 0;
    bool in_str = false, esc = false;
    for (const char *p = buf; *p; p++) {
        if (in_str) {
            if (esc) esc = false;
            else if (*p == '\\') esc = true;
            else if (*p == '"') in_str = false;
        } else if (*p == '"') {
            in_str = true;
        } else if (*p == '{' || *p == '[') {
            if (++depth > maxd) maxd = depth;
        } else if ((*p == '}' || *p == ']') && depth > 0) {
            depth--;
        }
    }
    cJSON *doc = maxd <= 4 ? cJSON_Parse(buf) : NULL;
    free(buf);
    if (!doc || !cJSON_IsObject(doc)) {
        cJSON_Delete(doc);
        send_status(req, "400 Bad Request", "JSON invalide");
        return NULL;
    }
    return doc;
}

static long json_long(const cJSON *doc, const char *key, long def)
{
    const cJSON *it = cJSON_GetObjectItem(doc, key);
    if (cJSON_IsNumber(it)) return (long)it->valuedouble;
    if (cJSON_IsString(it) && it->valuestring[0]) {
        char *end;
        long v = strtol(it->valuestring, &end, 10);
        if (end != it->valuestring) return v;
    }
    return def;
}

static bool json_flag(const cJSON *it)
{
    if (cJSON_IsBool(it)) return cJSON_IsTrue(it);
    if (cJSON_IsNumber(it)) return it->valuedouble != 0;
    if (cJSON_IsString(it)) return strcmp(it->valuestring, "1") == 0 || strcmp(it->valuestring, "on") == 0 ||
                                   strcmp(it->valuestring, "true") == 0;
    return false;
}

/** id d'une action : corps {"id":N} ou ?id=N. 0 si absent. */
static uint32_t action_id(httpd_req_t *req, const cJSON *doc)
{
    long id = json_long(doc, "id", 0);
    if (id <= 0) id = query_long(req, "id", 0);
    return id > 0 ? (uint32_t)id : 0;
}

// ----------------------------------------------------------------------------
// /api/notifications*
// ----------------------------------------------------------------------------

static esp_err_t notif_get_handler(httpd_req_t *req)
{
    char sub[24];
    uri_suffix(req, "/api/notifications", sub, sizeof(sub));
    if (sub[0] == '\0') {
        long page = query_long(req, "page", 1);
        long limit = query_long(req, "limit", 10);
        if (page < 1) page = 1;
        if (limit < 1) limit = 1;
        if (limit > 50) limit = 50;
        if (page > NOTIF_MAX) page = NOTIF_MAX;
        return send_json_owned(req, notifications_list_json((int)((page - 1) * limit), (int)limit));
    }
    if (strcmp(sub, "stats") == 0)       return send_json_owned(req, notifications_stats_json());
    if (strcmp(sub, "debug") == 0)       return send_json_owned(req, notifications_debug_json());
    if (strcmp(sub, "send-status") == 0) return send_json_owned(req, notifications_send_status_json());
    return send_status(req, "404 Not Found", "Route inconnue");
}

static esp_err_t stats_handler(httpd_req_t *req)
{
    return send_json_owned(req, notifications_stats_json());
}

static esp_err_t notif_post_handler(httpd_req_t *req)
{
    char sub[24];
    uri_suffix(req, "/api/notifications", sub, sizeof(sub));
    cJSON *doc = read_body(req);
    if (!doc) return ESP_OK;
    esp_err_t ret;
    char buf[96];

    if (sub[0] == '\0') {
        const cJSON *t = cJSON_GetObjectItem(doc, "title");
        const cJSON *m = cJSON_GetObjectItem(doc, "message");
        const cJSON *a = cJSON_GetObjectItem(doc, "alertType");
        if (!cJSON_IsString(t) || !t->valuestring[0] || !cJSON_IsString(m) || !m->valuestring[0]) {
            ret = send_status(req, "400 Bad Request", "Title et message requis");
        } else {
            long type = json_long(doc, "type", 0);
            uint32_t id = notifications_add_alert(t->valuestring, m->valuestring, (int)type,
                                                  cJSON_IsString(a) ? a->valuestring : "api", 0, 0);
            notifications_flush();
            if (!id) {
                ret = send_status(req, "500 Internal Server Error", "Erreur ajout notification");
            } else {
                snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"success\":true,\"id\":%lu}", (unsigned long)id);
                httpd_resp_set_type(req, "application/json");
                ret = httpd_resp_sendstr(req, buf);
            }
        }
    } else if (strcmp(sub, "read") == 0 || strcmp(sub, "delete") == 0) {
        uint32_t id = action_id(req, doc);
        bool del = sub[0] == 'd';
        if (!id) {
            ret = send_status(req, "400 Bad Request", "Parametre id manquant");
        } else if (del ? notifications_delete(id) : notifications_mark_read(id)) {
            notifications_flush();
            ret = send_status(req, "200 OK", del ? "Notification supprimee" : "Notification lue");
        } else {
            ret = send_status(req, "404 Not Found", "Notification non trouvee");
        }
    } else if (strcmp(sub, "read-all") == 0) {
        int n = notifications_mark_all_read();
        notifications_flush();
        snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"success\":true,\"marked\":%d}", n);
        httpd_resp_set_type(req, "application/json");
        ret = httpd_resp_sendstr(req, buf);
    } else if (strcmp(sub, "clear") == 0) {
        notifications_clear();
        notifications_flush();
        ret = send_status(req, "200 OK", "Notifications supprimees");
    } else if (strcmp(sub, "test") == 0) {
        // /generateNotif de la LiXee-Box (qui etait un GET)
        char msg[64];
        snprintf(msg, sizeof(msg), "Test de %s", cfg_get_general()->zlinky);
        notifications_add_alert("TEST", msg, NOTIF_WARNING, "test", 0, 0);
        notifications_flush();
        ret = send_status(req, "200 OK", "Notification de test ajoutee");
    } else if (strcmp(sub, "test-webpush") == 0) {
        esp_err_t e = notifications_test_webpush();
        ret = e == ESP_OK ? send_status(req, "202 Accepted", "Envoi en cours")
            : e == ESP_ERR_INVALID_ARG ? send_status(req, "400 Bad Request", "Serveur WebPush non renseigne")
            : send_status(req, "503 Service Unavailable", "File d'envoi pleine");
    } else {
        ret = send_status(req, "404 Not Found", "Route inconnue");
    }
    cJSON_Delete(doc);
    return ret;
}

// ----------------------------------------------------------------------------
// /api/config/notifmail (configNotifMail / saveConfigNotificationMail)
// ----------------------------------------------------------------------------

static esp_err_t mail_get_handler(httpd_req_t *req)
{
    const cfg_general_t *g = cfg_get_general();
    cJSON *root = cJSON_CreateObject();
    if (!root) return send_json_owned(req, NULL);
    cJSON_AddBoolToObject(root, "enableNotif", cfg_get_settings()->enable_notif);
    cJSON_AddStringToObject(root, "servSMTP", g->serv_smtp);
    cJSON_AddStringToObject(root, "portSMTP", g->port_smtp);
    cJSON_AddStringToObject(root, "userSMTP", g->user_smtp);
    cJSON_AddBoolToObject(root, "hasPassSMTP", g->pass_smtp[0] != '\0');   // jamais renvoye
    cJSON_AddStringToObject(root, "toSMTP", g->to_smtp);
    cJSON_AddStringToObject(root, "fromSMTP", g->from_smtp);
    cJSON_AddNumberToObject(root, "securitySMTP", g->security_smtp);
    cJSON_AddNumberToObject(root, "minTypeSMTP", g->min_type_smtp);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return send_json_owned(req, json);
}

/** Copie un champ texte s'il est present ; refuse les retours a la ligne (injection SMTP). */
static bool take_line(const cJSON *doc, const char *key, char *dst, size_t len, bool *bad)
{
    const cJSON *it = cJSON_GetObjectItem(doc, key);
    if (!cJSON_IsString(it)) return false;
    const char *v = it->valuestring;
    while (*v == ' ') v++;
    if (strpbrk(v, "\r\n") || strlen(v) >= len) { *bad = true; return false; }
    strlcpy(dst, v, len);
    size_t n = strlen(dst);
    while (n > 0 && dst[n - 1] == ' ') dst[--n] = '\0';
    return true;
}

static esp_err_t mail_post_handler(httpd_req_t *req)
{
    char sub[16];
    uri_suffix(req, "/api/config/notifmail", sub, sizeof(sub));
    cJSON *doc = read_body(req);
    if (!doc) return ESP_OK;

    if (strcmp(sub, "test") == 0) {
        cJSON_Delete(doc);
        esp_err_t e = notifications_test_mail();
        return e == ESP_OK ? send_status(req, "202 Accepted", "Envoi en cours")
             : e == ESP_ERR_INVALID_ARG ? send_status(req, "400 Bad Request", "Serveur SMTP non renseigne")
             : send_status(req, "503 Service Unavailable", "File d'envoi pleine");
    }
    if (sub[0] != '\0') {
        cJSON_Delete(doc);
        return send_status(req, "404 Not Found", "Route inconnue");
    }

    // Validation complete avant toute modification (copies locales des seuls
    // champs concernes : cfg_general_t pese plusieurs Ko, la pile HTTP 8 Ko)
    const cfg_general_t *cur = cfg_get_general();
    char serv[sizeof(cur->serv_smtp)], user[sizeof(cur->user_smtp)], to[sizeof(cur->to_smtp)];
    char from[sizeof(cur->from_smtp)], pass[sizeof(cur->pass_smtp)], port[sizeof(cur->port_smtp)];
    strlcpy(serv, cur->serv_smtp, sizeof(serv));
    strlcpy(user, cur->user_smtp, sizeof(user));
    strlcpy(to, cur->to_smtp, sizeof(to));
    strlcpy(from, cur->from_smtp, sizeof(from));
    strlcpy(pass, cur->pass_smtp, sizeof(pass));
    strlcpy(port, cur->port_smtp, sizeof(port));
    bool bad = false;
    take_line(doc, "servSMTP", serv, sizeof(serv), &bad);
    take_line(doc, "userSMTP", user, sizeof(user), &bad);
    take_line(doc, "toSMTP", to, sizeof(to), &bad);
    take_line(doc, "fromSMTP", from, sizeof(from), &bad);
    if (bad) {
        cJSON_Delete(doc);
        return send_status(req, "400 Bad Request", "Champ invalide (trop long ou retour a la ligne)");
    }
    const cJSON *it = cJSON_GetObjectItem(doc, "portSMTP");
    if (it) {
        long pn = json_long(doc, "portSMTP", -1);
        bool empty = cJSON_IsString(it) && it->valuestring[0] == '\0';
        if (!empty && (pn < 1 || pn > 65535)) {
            cJSON_Delete(doc);
            return send_status(req, "400 Bad Request", "Port SMTP invalide");
        }
        char pb[24] = "";
        if (!empty) snprintf(pb, sizeof(pb), "%ld", pn);
        strlcpy(port, pb, sizeof(port));
    }
    long sec = json_long(doc, "securitySMTP", cur->security_smtp);
    long mt = json_long(doc, "minTypeSMTP", cur->min_type_smtp);
    if (sec < 0 || sec > 3 || mt < 0 || mt > 3) {
        cJSON_Delete(doc);
        return send_status(req, "400 Bad Request", "Valeur de securite ou de type invalide");
    }
    const cJSON *pw = cJSON_GetObjectItem(doc, "passSMTP");
    if (cJSON_IsString(pw) && pw->valuestring[0]) {
        // Mot de passe en clair dans la configuration, comme la LiXee-Box :
        // le protocole SMTP AUTH l'exige a chaque envoi
        if (strlen(pw->valuestring) >= sizeof(pass)) {
            cJSON_Delete(doc);
            return send_status(req, "400 Bad Request", "Mot de passe trop long");
        }
        strlcpy(pass, pw->valuestring, sizeof(pass));
    }
    if (json_flag(cJSON_GetObjectItem(doc, "clearPassSMTP"))) pass[0] = '\0';

    // Sous le verrou des reglages SMTP : la tache "notif" peut etre en train
    // d'envoyer un mail ; elle copie ces champs sous ce verrou au debut de
    // chaque envoi (sans lui, serveur et mot de passe de deux reglages
    // differents, ou chaine lue pendant sa reecriture)
    cfg_general_t *g = cfg_get_general_mut();
    notifications_smtp_cfg_lock();
    strlcpy(g->serv_smtp, serv, sizeof(g->serv_smtp));
    strlcpy(g->port_smtp, port, sizeof(g->port_smtp));
    strlcpy(g->user_smtp, user, sizeof(g->user_smtp));
    strlcpy(g->pass_smtp, pass, sizeof(g->pass_smtp));
    strlcpy(g->to_smtp, to, sizeof(g->to_smtp));
    strlcpy(g->from_smtp, from, sizeof(g->from_smtp));
    memset(pass, 0, sizeof(pass));
    g->security_smtp = (int)sec;
    g->min_type_smtp = (int)mt;
    notifications_smtp_cfg_unlock();
    it = cJSON_GetObjectItem(doc, "enableNotif");
    if (it) cfg_get_settings_mut()->enable_notif = json_flag(it);
    cJSON_Delete(doc);

    esp_err_t err = cfg_save_general();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Sauvegarde de la configuration email impossible");
        return send_status(req, "500 Internal Server Error", "Echec d'ecriture de la configuration");
    }
    return send_status(req, "200 OK", "Configuration email enregistree");
}

// ----------------------------------------------------------------------------
// /getAlert : bandeau suivant, format texte de la LiXee-Box "etat;message"
// ----------------------------------------------------------------------------

static esp_err_t get_alert_handler(httpd_req_t *req)
{
    int state = 0;
    char msg[128], out[140];
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (!notifications_toast_take(&state, msg, sizeof(msg))) return httpd_resp_sendstr(req, "");
    snprintf(out, sizeof(out), "%d;%s", state, msg);
    return httpd_resp_sendstr(req, out);
}

// ----------------------------------------------------------------------------
// Formes PUT / DELETE de la LiXee-Box (id en parametre de requete) :
//   PUT /api/notifications/read?id=N, PUT /api/notifications/read-all,
//   DELETE /api/notifications/delete?id=N, DELETE /api/notifications/clear
// ----------------------------------------------------------------------------

static esp_err_t box_reply(httpd_req_t *req, const char *status, const char *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t notif_put_delete_handler(httpd_req_t *req)
{
    char sub[24];
    uri_suffix(req, "/api/notifications", sub, sizeof(sub));
    bool put = req->method == HTTP_PUT;
    if ((put && strcmp(sub, "read") == 0) || (!put && strcmp(sub, "delete") == 0)) {
        long id = query_long(req, "id", -1);
        if (id < 0) return box_reply(req, "400 Bad Request", "{\"error\":\"Param\u00e8tre id manquant\"}");
        bool ok = put ? notifications_mark_read((uint32_t)id) : notifications_delete((uint32_t)id);
        if (!ok) return box_reply(req, "404 Not Found", "{\"error\":\"Notification non trouv\u00e9e\"}");
        return box_reply(req, "200 OK", "{\"success\":true}");
    }
    if (put && strcmp(sub, "read-all") == 0) {
        notifications_mark_all_read();
        return box_reply(req, "200 OK", "{\"success\":true,\"message\":\"Toutes les notifications "
                                        "marqu\u00e9es comme lues\"}");
    }
    if (!put && strcmp(sub, "clear") == 0) {
        notifications_clear();
        return box_reply(req, "200 OK", "{\"success\":true}");
    }
    return send_status(req, "404 Not Found", "Route inconnue");
}

/** GET /api/alerts?since=N : historique des alertes (alertHistory de la Box). */
static esp_err_t alerts_handler(httpd_req_t *req)
{
    long since = query_long(req, "since", 0);
    return send_json_owned(req, notifications_alerts_json(since > 0 ? (uint32_t)since : 0));
}

// ----------------------------------------------------------------------------
// Enregistrement
// ----------------------------------------------------------------------------

void web_notif_register(httpd_handle_t server)
{
    // "/x/?*" (httpd_uri_match_wildcard) : "/x" et "/x/..." mais pas "/xy" ;
    // une route par methode au lieu d'une par action (max_uri_handlers limite)
    httpd_uri_t u = {};
    u.uri = "/api/notifications/?*";   u.method = HTTP_GET;  u.handler = notif_get_handler;
    web_server_register_uri(server, &u);
    u = {};
    u.uri = "/api/notifications/?*";   u.method = HTTP_POST; u.handler = notif_post_handler;
    web_server_register_uri(server, &u);
    u = {};
    u.uri = "/api/notifications/?*";   u.method = HTTP_PUT;    u.handler = notif_put_delete_handler;
    web_server_register_uri(server, &u);
    u = {};
    u.uri = "/api/notifications/?*";   u.method = HTTP_DELETE; u.handler = notif_put_delete_handler;
    web_server_register_uri(server, &u);
    u = {};
    u.uri = "/api/alerts";            u.method = HTTP_GET;  u.handler = alerts_handler;
    web_server_register_uri(server, &u);
    u = {};
    u.uri = "/api/stats";             u.method = HTTP_GET;  u.handler = stats_handler;
    web_server_register_uri(server, &u);
    u = {};
    u.uri = "/api/config/notifmail";  u.method = HTTP_GET;  u.handler = mail_get_handler;
    web_server_register_uri(server, &u);
    u = {};
    u.uri = "/api/config/notifmail/?*"; u.method = HTTP_POST; u.handler = mail_post_handler;
    web_server_register_uri(server, &u);
    u = {};
    u.uri = "/getAlert";              u.method = HTTP_GET;  u.handler = get_alert_handler;
    web_server_register_uri(server, &u);
}
