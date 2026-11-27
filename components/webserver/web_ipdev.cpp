// ============================================================================
// Routes des appareils IP - voir web_ipdev.h et IP_DEVICES.md
//
// GET  /api/ip/devices                      liste, etat, module actif
// GET  /api/ip/models                       modeles connus (choix manuel)
// POST /api/ip/probe   {ip,port,user,pass}  detection : modele, MAC, nom
// POST /api/ip/add     {ip,port,model,user,pass,interval,alias}
// POST /api/ip/update  {id,ip,port,model,user,pass,interval}  (pass absent : inchange)
// POST /api/ip/delete  {id,removeData}
// POST /api/ip/poll    {id}
// POST /api/ip/enable  {enabled}
// ============================================================================
#include "web_ipdev.h"
#include "web_server.h"
#include "ip_devices.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "WEB_IP";

#define IP_API_PREFIX "/api/ip/"
#define BODY_MAX      1024

static esp_err_t send_json_owned(httpd_req_t *req, char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (!json) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Module IP indisponible\"}");
    }
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

static esp_err_t send_result(httpd_req_t *req, bool ok, const char *msg, const char *id)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "status", ok ? "ok" : "error");
    cJSON_AddBoolToObject(o, "ok", ok);
    if (msg && msg[0]) cJSON_AddStringToObject(o, "message", msg);
    if (id && id[0]) cJSON_AddStringToObject(o, "id", id);
    char *txt = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!ok) httpd_resp_set_status(req, "400 Bad Request");
    return send_json_owned(req, txt);
}

static cJSON *read_json(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > BODY_MAX) return NULL;
    char buf[BODY_MAX + 1];
    size_t got = 0;
    int timeouts = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
        if (r <= 0) return NULL;
        got += (size_t)r;
    }
    buf[got] = '\0';
    return cJSON_Parse(buf);
}

static const char *jstr(const cJSON *j, const char *k)
{
    const cJSON *v = cJSON_GetObjectItem(j, k);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static int jint(const cJSON *j, const char *k, int def)
{
    const cJSON *v = cJSON_GetObjectItem(j, k);
    if (cJSON_IsNumber(v)) return v->valueint;
    if (cJSON_IsString(v) && v->valuestring[0]) return atoi(v->valuestring);
    return def;
}

static uint64_t jid(const cJSON *j)
{
    const char *s = jstr(j, "id");
    return s ? strtoull(s, NULL, 16) : 0;
}

static const char *sub_path(httpd_req_t *req)
{
    const char *p = req->uri + strlen(IP_API_PREFIX);
    return p;
}

static bool sub_is(httpd_req_t *req, const char *name)
{
    const char *p = sub_path(req);
    size_t n = strlen(name);
    return strncmp(p, name, n) == 0 && (p[n] == '\0' || p[n] == '?');
}

static esp_err_t ip_get_handler(httpd_req_t *req)
{
    if (sub_is(req, "devices")) return send_json_owned(req, ip_devices_list_json());
    if (sub_is(req, "models"))  return send_json_owned(req, ip_devices_models_json());
    httpd_resp_set_status(req, "404 Not Found");
    return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Route inconnue\"}");
}

static esp_err_t ip_post_handler(httpd_req_t *req)
{
    cJSON *j = read_json(req);
    if (!j) return send_result(req, false, "Corps JSON attendu", NULL);
    esp_err_t ret;
    char err[96] = "";

    if (sub_is(req, "enable")) {
        bool on = cJSON_IsTrue(cJSON_GetObjectItem(j, "enabled"));
        esp_err_t e = ip_devices_set_enabled(on);
        ret = send_result(req, e == ESP_OK, e == ESP_OK ? (on ? "Module IP actif" : "Module IP arr\u00eat\u00e9")
                                                        : "Module IP non disponible", NULL);
    } else if (sub_is(req, "probe")) {
        const char *ip = jstr(j, "ip");
        ret = send_json_owned(req, ip_devices_probe_json(ip ? ip : "", (uint16_t)jint(j, "port", 80),
                                                         jstr(j, "user"), jstr(j, "pass")));
    } else if (sub_is(req, "add")) {
        uint64_t ieee = 0;
        const char *ip = jstr(j, "ip");
        esp_err_t e = ip_devices_add(ip ? ip : "", (uint16_t)jint(j, "port", 80), jstr(j, "model"),
                                     jstr(j, "user"), jstr(j, "pass"), jint(j, "interval", 15),
                                     jstr(j, "alias"), &ieee, err, sizeof(err));
        char id[20] = "";
        if (e == ESP_OK) snprintf(id, sizeof(id), "%016llX", (unsigned long long)ieee);
        ret = send_result(req, e == ESP_OK, e == ESP_OK ? "Appareil ajout\u00e9" : err, id);
    } else if (sub_is(req, "update")) {
        // Mot de passe absent du corps : inchange ; chaine vide : efface
        esp_err_t e = ip_devices_update(jid(j), jstr(j, "ip"), (uint16_t)jint(j, "port", 0), jstr(j, "model"),
                                        jstr(j, "user"), jstr(j, "pass"), jint(j, "interval", 0),
                                        err, sizeof(err));
        ret = send_result(req, e == ESP_OK, e == ESP_OK ? "Appareil modifi\u00e9" : err, NULL);
    } else if (sub_is(req, "delete")) {
        esp_err_t e = ip_devices_remove(jid(j), cJSON_IsTrue(cJSON_GetObjectItem(j, "removeData")));
        ret = send_result(req, e == ESP_OK, e == ESP_OK ? "Appareil supprim\u00e9" : "Appareil inconnu", NULL);
    } else if (sub_is(req, "poll")) {
        uint64_t id = jid(j);
        bool known = ip_devices_is_ip(id);
        if (known) ip_devices_poll_now(id);
        ret = send_result(req, known, known ? "Relev\u00e9 demand\u00e9" : "Appareil inconnu", NULL);
    } else {
        httpd_resp_set_status(req, "404 Not Found");
        ret = httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Route inconnue\"}");
    }
    cJSON_Delete(j);
    if (err[0]) ESP_LOGW(TAG, "%s : %s", req->uri, err);
    return ret;
}

void web_ipdev_register(httpd_handle_t server)
{
    httpd_uri_t g = {};
    g.uri = IP_API_PREFIX "*";
    g.method = HTTP_GET;
    g.handler = ip_get_handler;
    web_server_register_uri(server, &g);

    httpd_uri_t p = {};
    p.uri = IP_API_PREFIX "*";
    p.method = HTTP_POST;
    p.handler = ip_post_handler;
    web_server_register_uri(server, &p);
}
