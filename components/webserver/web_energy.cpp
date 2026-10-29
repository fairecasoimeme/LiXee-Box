// ============================================================================
// Routes de la page Energie - voir web_energy.h et docs/energy_api.md
// Les calculs sont dans components/energy_service ; ce fichier ne fait que
// lire les parametres, appeler le service et renvoyer sa reponse.
// ============================================================================

#include "web_energy.h"
#include "web_server.h"
#include "energy_service.h"
#include "presence.h"
#include "delestage.h"
#include "config_manager.h"
#include "zigate_devices.h"
#include "device_data.h"
#include "esp_log.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static const char *TAG = "WEB_NRG";

// ----------------------------------------------------------------------------
// Outils
// ----------------------------------------------------------------------------

/** Parametre de requete, ou valeur par defaut. */
static void query_param(httpd_req_t *req, const char *key, char *out, size_t len, const char *def)
{
    char q[160];
    strlcpy(out, def, len);
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        if (httpd_query_key_value(q, key, out, len) != ESP_OK) strlcpy(out, def, len);
    }
}

static void period_param(httpd_req_t *req, char *out, size_t len)
{
    query_param(req, "time", out, len, "hour");
    if (strcmp(out, "hour") && strcmp(out, "day") && strcmp(out, "month") && strcmp(out, "year")) {
        strlcpy(out, "hour", len);
    }
}

/** IEEE d'un compteur : ?ieee= (ou IEEE= / id=, noms de la LiXee-Box). 0 = ZLinky. */
static uint64_t ieee_param(httpd_req_t *req)
{
    char v[24];
    static const char *const KEYS[] = { "ieee", "IEEE", "id" };
    for (size_t i = 0; i < 3; i++) {
        query_param(req, KEYS[i], v, sizeof(v), "");
        if (v[0]) return strtoull(v, NULL, 16);
    }
    return 0;
}

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

static esp_err_t send_csv_owned(httpd_req_t *req, char *csv, const char *basename)
{
    if (!csv) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "Memoire insuffisante");
    }
    char disp[96], date[16] = "";
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    strftime(date, sizeof(date), "%Y-%m-%d", &t);
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s_%s.csv\"", basename, date);
    httpd_resp_set_type(req, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    esp_err_t err = httpd_resp_send(req, csv, strlen(csv));
    free(csv);
    return err;
}

static esp_err_t send_status(httpd_req_t *req, const char *status, const char *msg)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "{\"status\":\"%s\",\"message\":\"%s\"}",
             strncmp(status, "200", 3) == 0 ? "ok" : "error", msg);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

static cJSON *read_body_json(httpd_req_t *req)
{
    int len = req->content_len;
    if (len <= 0 || len > 1024) return NULL;
    char buf[1025];
    int got = 0, timeouts = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
        if (r <= 0) return NULL;
        got += r;
    }
    buf[got] = '\0';
    return cJSON_Parse(buf);
}

// ----------------------------------------------------------------------------
// Page Energie
// ----------------------------------------------------------------------------

static esp_err_t summary_handler(httpd_req_t *req)
{
    char t[8];
    period_param(req, t, sizeof(t));
    return send_json_owned(req, energy_summary_json(t));
}

static esp_err_t gauge_handler(httpd_req_t *req)
{
    char t[8];
    period_param(req, t, sizeof(t));
    return send_json_owned(req, energy_gauge_json(t));
}

static esp_err_t distribution_handler(httpd_req_t *req)
{
    char t[8], type[8];
    period_param(req, t, sizeof(t));
    query_param(req, "type", type, sizeof(type), "");
    return send_json_owned(req, energy_distribution_json(t, strcmp(type, "euro") == 0));
}

static esp_err_t chart_handler(httpd_req_t *req)
{
    char t[8];
    period_param(req, t, sizeof(t));
    return send_json_owned(req, energy_chart_json(t, ieee_param(req)));
}

/** /loadEnergyChart : format de la LiXee-Box (tableau de lignes). */
static esp_err_t legacy_chart_handler(httpd_req_t *req)
{
    char t[8];
    period_param(req, t, sizeof(t));
    return send_json_owned(req, energy_chart_legacy_json(t, ieee_param(req)));
}

static esp_err_t chart_export_handler(httpd_req_t *req)
{
    char t[8], name[40];
    period_param(req, t, sizeof(t));
    snprintf(name, sizeof(name), "usage_electricite_%s", t);
    return send_csv_owned(req, energy_chart_csv(t, ieee_param(req)), name);
}

static esp_err_t power_handler(httpd_req_t *req)
{
    return send_json_owned(req, energy_power_json());
}

static esp_err_t power_export_handler(httpd_req_t *req)
{
    return send_csv_owned(req, energy_power_csv(), "puissance_apparente");
}

static esp_err_t tariff_handler(httpd_req_t *req)
{
    return send_json_owned(req, energy_tariff_json());
}

static esp_err_t linky_handler(httpd_req_t *req)
{
    return send_json_owned(req, energy_linky_json());
}

// ----------------------------------------------------------------------------
// Sous-compteurs (config "subMeters" de configGeneral.json)
// ----------------------------------------------------------------------------

static esp_err_t submeters_get_handler(httpd_req_t *req)
{
    const cfg_general_t *g = cfg_get_general();
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "subMeters");
    for (int i = 0; i < g->sub_meter_count && i < CFG_MAX_SUBMETERS; i++) {
        const cfg_submeter_t *s = &g->sub_meters[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "index", i);
        cJSON_AddStringToObject(o, "IEEE", s->IEEE);
        cJSON_AddStringToObject(o, "alias", s->alias);
        cJSON_AddStringToObject(o, "color", s->color);
        cJSON_AddBoolToObject(o, "enabled", s->enabled);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddNumberToObject(root, "count", g->sub_meter_count);   // champ de la LiXee-Box
    cJSON_AddNumberToObject(root, "max", CFG_MAX_SUBMETERS);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return send_json_owned(req, json);
}

/** Appareils avec un index d'energie (0x0702/0), hors ZLinky. */
static esp_err_t submeters_eligible_handler(httpd_req_t *req)
{
    uint64_t zl = strtoull(cfg_get_general()->zlinky, NULL, 16);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < zigate_devices_count(); i++) {
        const zigate_device_t *d = zigate_devices_get(i);
        if (!d || d->ieee_addr == zl || !device_data_get_value_ptr(d->ieee_addr, 0x0702, 0)) continue;
        char ieee[20];
        snprintf(ieee, sizeof(ieee), "%016llX", (unsigned long long)d->ieee_addr);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "IEEE", ieee);
        cJSON_AddStringToObject(o, "alias", d->alias);
        cJSON_AddStringToObject(o, "model", d->model);
        cJSON_AddStringToObject(o, "manufacturer", d->manufacturer);
        cJSON_AddItemToArray(arr, o);
    }
    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return send_json_owned(req, json);
}

static bool ieee_valid(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > 16) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static esp_err_t submeters_post_handler(httpd_req_t *req)
{
    cJSON *j = read_body_json(req);
    if (!j) return send_status(req, "400 Bad Request", "JSON invalide");
    const cJSON *ieee = cJSON_GetObjectItem(j, "IEEE");
    const cJSON *alias = cJSON_GetObjectItem(j, "alias");
    const cJSON *color = cJSON_GetObjectItem(j, "color");
    const cJSON *enabled = cJSON_GetObjectItem(j, "enabled");
    if (!cJSON_IsString(ieee) || !ieee_valid(ieee->valuestring) || !cJSON_IsString(alias) || !alias->valuestring[0]) {
        cJSON_Delete(j);
        return send_status(req, "400 Bad Request", "IEEE et alias requis");
    }
    char key[20];
    snprintf(key, sizeof(key), "%016llX", (unsigned long long)strtoull(ieee->valuestring, NULL, 16));

    cfg_general_t *g = cfg_get_general_mut();
    int idx = -1;
    for (int i = 0; i < g->sub_meter_count; i++) {
        if (strcasecmp(g->sub_meters[i].IEEE, key) == 0) { idx = i; break; }
    }
    if (idx < 0) {
        if (g->sub_meter_count >= CFG_MAX_SUBMETERS) {
            cJSON_Delete(j);
            return send_status(req, "400 Bad Request", "Nombre maximum de sous-compteurs atteint");
        }
        idx = g->sub_meter_count++;
        memset(&g->sub_meters[idx], 0, sizeof(g->sub_meters[idx]));
        strlcpy(g->sub_meters[idx].IEEE, key, sizeof(g->sub_meters[idx].IEEE));
    }
    cfg_submeter_t *s = &g->sub_meters[idx];
    strlcpy(s->alias, alias->valuestring, sizeof(s->alias));
    const char *col = (cJSON_IsString(color) && color->valuestring[0] == '#' && strlen(color->valuestring) <= 7)
                      ? color->valuestring : "#3498db";
    strlcpy(s->color, col, sizeof(s->color));
    s->enabled = !cJSON_IsBool(enabled) || cJSON_IsTrue(enabled);
    cJSON_Delete(j);

    if (cfg_save_general() != ESP_OK) return send_status(req, "500 Internal Server Error", "Echec d'enregistrement");
    ESP_LOGI(TAG, "Sous-compteur %s enregistre", key);
    return send_status(req, "200 OK", "Sous-compteur enregistre");
}

static esp_err_t submeters_delete_handler(httpd_req_t *req)
{
    cJSON *j = read_body_json(req);
    const cJSON *ieee = j ? cJSON_GetObjectItem(j, "IEEE") : NULL;
    if (!cJSON_IsString(ieee)) {
        cJSON_Delete(j);
        return send_status(req, "400 Bad Request", "IEEE requis");
    }
    uint64_t v = strtoull(ieee->valuestring, NULL, 16);
    cJSON_Delete(j);

    cfg_general_t *g = cfg_get_general_mut();
    for (int i = 0; i < g->sub_meter_count; i++) {
        if (strtoull(g->sub_meters[i].IEEE, NULL, 16) != v) continue;
        for (int k = i; k < g->sub_meter_count - 1; k++) g->sub_meters[k] = g->sub_meters[k + 1];
        g->sub_meter_count--;
        memset(&g->sub_meters[g->sub_meter_count], 0, sizeof(cfg_submeter_t));
        if (cfg_save_general() != ESP_OK) return send_status(req, "500 Internal Server Error", "Echec d'enregistrement");
        return send_status(req, "200 OK", "Sous-compteur supprime");
    }
    return send_status(req, "404 Not Found", "Sous-compteur introuvable");
}

// ----------------------------------------------------------------------------
// Enregistrement
// ----------------------------------------------------------------------------

// GET /api/energy/presence?time=hour|day : barres de presence du graphe d'usage
static esp_err_t presence_handler(httpd_req_t *req)
{
    char t[8];
    period_param(req, t, sizeof(t));
    return send_json_owned(req, presence_chart_json(t));
}

// GET /api/delestage : etat du delestage ; POST /api/delestage/restore : rallumage manuel
static esp_err_t delestage_get_handler(httpd_req_t *req)
{
    return send_json_owned(req, delestage_json());
}

static esp_err_t delestage_restore_handler(httpd_req_t *req)
{
    // Comme la LiXee-Box : toujours 202 {"status":"pending"} (rien a rallumer : sans effet)
    delestage_request_restore();
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"pending\"}");
}

void web_energy_register(httpd_handle_t server)
{
    static const struct { const char *uri; httpd_method_t method; esp_err_t (*fn)(httpd_req_t *); } routes[] = {
        { "/api/energy/summary",            HTTP_GET,  summary_handler },
        { "/api/energy/gauge",              HTTP_GET,  gauge_handler },
        { "/api/energy/distribution",       HTTP_GET,  distribution_handler },
        // /api/energy/chart et /loadEnergyChart : lignes {"y",...} de la LiXee-Box ;
        // l'interface lit les series de /api/energy/series
        { "/api/energy/series",             HTTP_GET,  chart_handler },
        { "/api/energy/chart",              HTTP_GET,  legacy_chart_handler },
        { "/api/energy/chart/export",       HTTP_GET,  chart_export_handler },
        { "/loadEnergyChart",               HTTP_GET,  legacy_chart_handler },
        { "/api/energy/power",              HTTP_GET,  power_handler },
        { "/api/energy/power/export",       HTTP_GET,  power_export_handler },
        { "/api/energy/tariff",             HTTP_GET,  tariff_handler },
        { "/api/energy/linky",              HTTP_GET,  linky_handler },
        { "/api/energy/presence",           HTTP_GET,  presence_handler },
        { "/api/delestage",                 HTTP_GET,  delestage_get_handler },
        { "/api/delestage/restore",         HTTP_POST, delestage_restore_handler },
        { "/api/energy/submeters",          HTTP_GET,  submeters_get_handler },
        { "/api/energy/submeters",          HTTP_POST, submeters_post_handler },
        { "/api/energy/submeters/eligible", HTTP_GET,  submeters_eligible_handler },
        { "/getSubMeters",                  HTTP_GET,  submeters_get_handler },        // noms LiXee-Box
        { "/getEligibleSubMeters",          HTTP_GET,  submeters_eligible_handler },
        { "/api/energy/submeters/delete",   HTTP_POST, submeters_delete_handler },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_uri_t uri = {};
        uri.uri = routes[i].uri;
        uri.method = routes[i].method;
        uri.handler = routes[i].fn;
        web_server_register_uri(server, &uri);
    }
}
