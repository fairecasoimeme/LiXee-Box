// ============================================================================
// Routes des chauffe-eau virtuels - voir web_waterheater.h. La logique est
// dans components/water_heater ; ce fichier lit les parametres et renvoie les
// reponses du module (meme schema que web_thermostat.cpp).
// ============================================================================

#include "web_waterheater.h"
#include "web_server.h"
#include "water_heater.h"
#include "esp_log.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BODY_MAX 4096

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
    char buf[160];
    snprintf(buf, sizeof(buf), "{\"status\":\"%s\",\"ok\":%s,\"message\":\"%s\"}",
             strncmp(status, "200", 3) == 0 ? "ok" : "error",
             strncmp(status, "200", 3) == 0 ? "true" : "false", msg);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

static cJSON *read_body(httpd_req_t *req)
{
    int len = req->content_len;
    if (len <= 0) { send_status(req, "400 Bad Request", "Corps JSON attendu"); return NULL; }
    if (len > BODY_MAX) { send_status(req, "413 Payload Too Large", "Corps trop long"); return NULL; }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { send_status(req, "500 Internal Server Error", "Memoire insuffisante"); return NULL; }
    int got = 0, timeouts = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
        if (r <= 0) { free(buf); send_status(req, "400 Bad Request", "Corps incomplet"); return NULL; }
        got += r;
    }
    buf[got] = '\0';
    cJSON *doc = cJSON_Parse(buf);
    free(buf);
    if (!doc) send_status(req, "400 Bad Request", "JSON invalide");
    return doc;
}

/** Partie de l'URI apres "/api/waterheaters", sans la query ("" pour la racine). */
static void route_name(httpd_req_t *req, char *out, size_t len)
{
    const char *p = req->uri + strlen("/api/waterheaters");
    if (*p == '/') p++;
    size_t n = strcspn(p, "?");
    if (n >= len) n = len - 1;
    memcpy(out, p, n);
    out[n] = '\0';
}

static bool j_num(const cJSON *j, const char *k, double *out)
{
    const cJSON *it = cJSON_GetObjectItem(j, k);
    if (cJSON_IsNumber(it)) { *out = it->valuedouble; return true; }
    if (cJSON_IsBool(it)) { *out = cJSON_IsTrue(it) ? 1 : 0; return true; }
    if (cJSON_IsString(it) && it->valuestring[0]) { *out = strtod(it->valuestring, NULL); return true; }
    return false;
}

static bool j_bool_default(const cJSON *j, const char *k, bool def)
{
    const cJSON *it = cJSON_GetObjectItem(j, k);
    if (cJSON_IsBool(it)) return cJSON_IsTrue(it);
    if (cJSON_IsNumber(it)) return it->valuedouble != 0;
    return def;
}

static esp_err_t wh_get(httpd_req_t *req)
{
    char r[24];
    route_name(req, r, sizeof(r));
    if (!r[0])                   return send_json_owned(req, water_heater_status_json());
    if (!strcmp(r, "config"))    return send_json_owned(req, water_heater_config_json());
    if (!strcmp(r, "options"))   return send_json_owned(req, water_heater_options_json());
    return send_status(req, "404 Not Found", "Route inconnue");
}

static esp_err_t wh_post(httpd_req_t *req)
{
    char r[24];
    route_name(req, r, sizeof(r));
    cJSON *j = read_body(req);
    if (!j) return ESP_OK;
    esp_err_t ret;
    double id = -1, v = 0;
    bool has_id = j_num(j, "id", &id);           // "new" : non numerique -> nouvelle zone
    if (!strcmp(r, "save")) {
        int n = water_heater_save_zone(has_id ? (int)id : -1, j);
        if (n < 0) {
            ret = send_status(req, "400 Bad Request", "Nombre maximal de chauffe-eau atteint");
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"ok\":true,\"id\":%d}", n);
            httpd_resp_set_type(req, "application/json");
            ret = httpd_resp_sendstr(req, buf);
        }
        cJSON_Delete(j);
        return ret;
    }
    bool ok = false;
    if (!has_id) {
        ok = false;
    } else if (!strcmp(r, "setpoint")) {
        ok = j_num(j, "value", &v) && water_heater_set_setpoint((int)id, (float)v);
    } else if (!strcmp(r, "mode")) {
        ok = j_num(j, "mode", &v) && water_heater_set_mode((int)id, (int)v);
    } else if (!strcmp(r, "toggle")) {
        ok = water_heater_toggle((int)id);
    } else if (!strcmp(r, "boost")) {
        double dur = 0, temp = 0;
        bool has_dur = j_num(j, "durationSec", &dur);
        bool has_temp = j_num(j, "tempC", &temp);
        ok = has_dur && dur > 0 && water_heater_boost((int)id, (uint32_t)dur,
                 j_bool_default(j, "oneShot", false), j_bool_default(j, "emergency", false),
                 has_temp, (float)temp);
    } else if (!strcmp(r, "cancelboost")) {
        ok = water_heater_cancel_boost((int)id);
    } else if (!strcmp(r, "delete")) {
        ok = water_heater_delete((int)id);
    } else {
        cJSON_Delete(j);
        return send_status(req, "404 Not Found", "Route inconnue");
    }
    cJSON_Delete(j);
    return ok ? send_status(req, "200 OK", "OK") : send_status(req, "400 Bad Request", "Zone ou parametre invalide");
}

void web_waterheater_register(httpd_handle_t server)
{
    httpd_uri_t get = {};
    get.uri = "/api/waterheaters*";
    get.method = HTTP_GET;
    get.handler = wh_get;
    web_server_register_uri(server, &get);

    httpd_uri_t post = {};
    post.uri = "/api/waterheaters*";
    post.method = HTTP_POST;
    post.handler = wh_post;
    web_server_register_uri(server, &post);
}
