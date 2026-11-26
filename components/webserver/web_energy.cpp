// ============================================================================
// Routes de la page Energie - voir web_energy.h et docs/energy_api.md
// Les calculs sont dans components/energy_service ; ce fichier ne fait que
// lire les parametres, appeler le service et renvoyer sa reponse.
// ============================================================================

#include "web_energy.h"
#include "web_server.h"
#include "energy_service.h"
#include "energy_advice.h"
#include "notifications.h"
#include "energy_archive.h"
#include "energy_history.h"
#include "presence.h"
#include "temp_history.h"
#include "delestage.h"
#include "config_manager.h"
#include "zigate_devices.h"
#include "device_data.h"
#include "lora.h"
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

static bool is_zlinky_model(const char *m) { return m && strncmp(m, "ZLinky", 6) == 0; }

/** Appareil Zigbee ou emetteur LoRa de modele ZLinky. */
static bool is_zlinky(uint64_t ieee)
{
    const zigate_device_t *d = zigate_devices_find_by_ieee(ieee);
    if (d) return is_zlinky_model(d->model);
    int slot = lora_find_slot_by_ieee(ieee);
    lora_emitter_info_t e;
    return slot >= 0 && lora_get_emitter(slot, &e) && is_zlinky_model(e.model);
}

/**
 * ?zl= : ZLinky affiche par le tableau de bord, s'il n'est pas celui de
 * Config Energie (plusieurs compteurs Linky). Ignore si ce n'est pas un ZLinky.
 */
static void view_begin(httpd_req_t *req)
{
    char v[24];
    query_param(req, "zl", v, sizeof(v), "");
    uint64_t z = v[0] ? strtoull(v, NULL, 16) : 0;
    if (z && is_zlinky(z)) energy_service_view_begin(z);
}

/** Reponse JSON calculee pour le ZLinky choisi (view_begin). */
#define VIEW_JSON(req, call) do {                    \
        view_begin(req);                             \
        char *_json = (call);                        \
        energy_service_view_end();                   \
        return send_json_owned(req, _json);          \
    } while (0)

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
    VIEW_JSON(req, energy_summary_json(t));
}

static esp_err_t gauge_handler(httpd_req_t *req)
{
    char t[8];
    period_param(req, t, sizeof(t));
    VIEW_JSON(req, energy_gauge_json(t));
}

static esp_err_t distribution_handler(httpd_req_t *req)
{
    char t[8], type[8];
    period_param(req, t, sizeof(t));
    query_param(req, "type", type, sizeof(type), "");
    VIEW_JSON(req, energy_distribution_json(t, strcmp(type, "euro") == 0));
}

static esp_err_t chart_handler(httpd_req_t *req)
{
    char t[8];
    period_param(req, t, sizeof(t));
    VIEW_JSON(req, energy_chart_json(t, ieee_param(req)));
}

/** /loadEnergyChart : format de la LiXee-Box (tableau de lignes). */
static esp_err_t legacy_chart_handler(httpd_req_t *req)
{
    char t[8];
    period_param(req, t, sizeof(t));
    VIEW_JSON(req, energy_chart_legacy_json(t, ieee_param(req)));
}

static esp_err_t chart_export_handler(httpd_req_t *req)
{
    char t[8], name[40];
    period_param(req, t, sizeof(t));
    snprintf(name, sizeof(name), "usage_electricite_%s", t);
    view_begin(req);
    char *csv = energy_chart_csv(t, ieee_param(req));
    energy_service_view_end();
    return send_csv_owned(req, csv, name);
}

static esp_err_t power_handler(httpd_req_t *req)
{
    VIEW_JSON(req, energy_power_json());
}

static esp_err_t power_export_handler(httpd_req_t *req)
{
    view_begin(req);
    char *csv = energy_power_csv();
    energy_service_view_end();
    return send_csv_owned(req, csv, "puissance_apparente");
}

static esp_err_t tariff_handler(httpd_req_t *req)
{
    VIEW_JSON(req, energy_tariff_json());
}

static esp_err_t linky_handler(httpd_req_t *req)
{
    VIEW_JSON(req, energy_linky_json());
}

/** Gravite d'une alerte de la box d'apres son type (comme LiXee-Assist). */
static const char *box_alert_level(const char *type)
{
    static const char *const DANGER[] = { "subscribed_power", "over_power", "water_leak", "night_water_leak",
                                          "water_continuous", "freeze", "power_outage" };
    static const char *const WARNING[] = { "over_budget", "red_day", "price_change", "daily_anomaly" };
    for (size_t i = 0; i < sizeof(DANGER) / sizeof(DANGER[0]); i++) if (!strcmp(type, DANGER[i])) return "danger";
    for (size_t i = 0; i < sizeof(WARNING) / sizeof(WARNING[0]); i++) if (!strcmp(type, WARNING[i])) return "warning";
    return "info";
}

/** "JJ/MM/AAAA HH:MM" (heure locale) -> epoch, 0 si illisible. */
static time_t box_alert_time(const char *s)
{
    struct tm t = {};
    if (!s || sscanf(s, "%d/%d/%d %d:%d", &t.tm_mday, &t.tm_mon, &t.tm_year, &t.tm_hour, &t.tm_min) != 5) return 0;
    t.tm_mon -= 1;
    t.tm_year -= 1900;
    t.tm_isdst = -1;
    return mktime(&t);
}

/**
 * GET /api/energy/advice : conseils calcules par la box (energy_advice.h),
 * plus ses alertes des 6 dernieres heures. Une alerte calculee qui double une
 * alerte de la box (puissance, eau, couleur du lendemain, anomalie) est retiree.
 */
static esp_err_t advice_handler(httpd_req_t *req)
{
    // ?part=weather : meteo seule, pour le bandeau du tarif (rafraichi chaque minute)
    char q[48], part[16];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "part", part, sizeof(part)) == ESP_OK && !strcmp(part, "weather")) {
        return send_json_owned(req, energy_advice_weather_json());
    }
    char *js = energy_advice_json();
    cJSON *root = js ? cJSON_Parse(js) : NULL;
    free(js);
    if (!root) return send_json_owned(req, strdup("{}"));
    cJSON *alerts = cJSON_GetObjectItem(root, "alerts");
    if (!cJSON_IsArray(alerts)) alerts = cJSON_AddArrayToObject(root, "alerts");

    char *bj = notifications_alerts_json(0);
    cJSON *box = bj ? cJSON_Parse(bj) : NULL;
    free(bj);
    bool power = false, water = false, tomorrow = false, anomaly = false;
    cJSON *recent = cJSON_CreateArray();
    time_t now = time(NULL);
    const cJSON *a;
    cJSON_ArrayForEach(a, cJSON_GetObjectItem(box, "alerts")) {
        const cJSON *ty = cJSON_GetObjectItem(a, "type"), *ti = cJSON_GetObjectItem(a, "title");
        const cJSON *ms = cJSON_GetObjectItem(a, "message"), *ts = cJSON_GetObjectItem(a, "ts");
        const char *type = cJSON_IsString(ty) ? ty->valuestring : "";
        time_t at = box_alert_time(cJSON_IsString(ts) ? ts->valuestring : NULL);
        if (!at || now - at > 6 * 3600 || now < at - 300 || !cJSON_IsString(ti) || !ti->valuestring[0]) continue;
        if (!strcmp(type, "subscribed_power") || !strcmp(type, "over_power")) power = true;
        if (strstr(type, "water")) water = true;
        if (!strcmp(type, "color_tomorrow")) tomorrow = true;
        if (!strcmp(type, "daily_anomaly")) anomaly = true;
        char code[48];
        snprintf(code, sizeof(code), "box.%s", type[0] ? type : "alert");
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "code", code);
        cJSON_AddStringToObject(o, "level", box_alert_level(type));
        cJSON_AddStringToObject(o, "title", ti->valuestring);
        cJSON_AddStringToObject(o, "text", cJSON_IsString(ms) ? ms->valuestring : "");
        cJSON_AddNumberToObject(o, "at", (double)at);
        cJSON_AddItemToArray(recent, o);
    }
    cJSON_Delete(box);

    // Les plus recentes d'abord, puis les alertes calculees non redondantes
    cJSON *out = cJSON_CreateArray();
    for (int i = cJSON_GetArraySize(recent) - 1; i >= 0; i--)
        cJSON_AddItemToArray(out, cJSON_DetachItemFromArray(recent, i));
    cJSON_Delete(recent);
    cJSON *c;
    while ((c = cJSON_DetachItemFromArray(alerts, 0)) != NULL) {
        const cJSON *code = cJSON_GetObjectItem(c, "code");
        const char *k = cJSON_IsString(code) ? code->valuestring : "";
        bool dup = (power && !strcmp(k, "alert.near_max_power")) || (water && !strcmp(k, "alert.water_continuous")) ||
                   (tomorrow && !strncmp(k, "alert.tomorrow_", 15)) || (anomaly && !strcmp(k, "alert.yesterday_high"));
        if (dup) cJSON_Delete(c);
        else cJSON_AddItemToArray(out, c);
    }
    cJSON_ReplaceItemInObject(root, "alerts", out);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return send_json_owned(req, s);
}

/**
 * POST /api/energy/history/clear {"confirm":true} : efface l'historique de
 * consommation et la courbe de puissance (issue #65 : donnees enregistrees a
 * une date fausse). La configuration est conservee.
 */
static esp_err_t history_clear_handler(httpd_req_t *req)
{
    cJSON *doc = read_body_json(req);
    bool ok = doc && cJSON_IsTrue(cJSON_GetObjectItem(doc, "confirm"));
    cJSON_Delete(doc);
    if (!ok) return send_status(req, "400 Bad Request", "confirm requis");
    energy_history_clear_all();
    energy_archive_request_clear();
    energy_service_clear_power();
    return send_status(req, "200 OK", "Historique efface");
}

/**
 * GET /api/energy/zlinkys : ZLinky Zigbee et LoRa, pour le choix du compteur
 * du tableau de bord. {"main":"<IEEE de Config Energie>","list":[{ieee,name}]}
 */
static esp_err_t zlinkys_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "main", cfg_get_general()->zlinky);
    cJSON *list = cJSON_AddArrayToObject(root, "list");
    char ieee[20];
    for (int i = 0; i < zigate_devices_count(); i++) {
        const zigate_device_t *d = zigate_devices_get(i);
        if (!d || !is_zlinky_model(d->model)) continue;
        snprintf(ieee, sizeof(ieee), "%016llX", (unsigned long long)d->ieee_addr);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ieee", ieee);
        cJSON_AddStringToObject(o, "name", d->alias[0] ? d->alias : d->model);
        cJSON_AddItemToArray(list, o);
    }
    lora_emitter_info_t lem[LORA_MAX_EMITTERS];
    int nl = lora_get_emitters(lem, LORA_MAX_EMITTERS);
    for (int i = 0; i < nl; i++) {
        if (!is_zlinky_model(lem[i].model)) continue;
        snprintf(ieee, sizeof(ieee), "%016llX", (unsigned long long)lem[i].ieee);
        const device_data_t *dd = device_data_find(lem[i].ieee);
        char name[48];
        if (dd && dd->info.alias[0]) strlcpy(name, dd->info.alias, sizeof(name));
        else snprintf(name, sizeof(name), "%s (LoRa)", lem[i].model);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ieee", ieee);
        cJSON_AddStringToObject(o, "name", name);
        cJSON_AddItemToArray(list, o);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return send_json_owned(req, json);
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
        cJSON_AddStringToObject(o, "category", s->category[0] ? s->category : "other");
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

/** Categories predefinies d'un sous-compteur (diagnostic de repartition). */
static const char *const SUBMETER_CATEGORIES[] = {
    "heating", "hot_water", "appliance", "electronics", "lighting", "outdoor", "other"
};

static const char *submeter_category_valid(const cJSON *category)
{
    if (cJSON_IsString(category)) {
        for (size_t i = 0; i < sizeof(SUBMETER_CATEGORIES) / sizeof(SUBMETER_CATEGORIES[0]); i++) {
            if (!strcasecmp(category->valuestring, SUBMETER_CATEGORIES[i])) return SUBMETER_CATEGORIES[i];
        }
    }
    return "other";
}

static esp_err_t submeters_post_handler(httpd_req_t *req)
{
    cJSON *j = read_body_json(req);
    if (!j) return send_status(req, "400 Bad Request", "JSON invalide");
    const cJSON *ieee = cJSON_GetObjectItem(j, "IEEE");
    const cJSON *alias = cJSON_GetObjectItem(j, "alias");
    const cJSON *color = cJSON_GetObjectItem(j, "color");
    const cJSON *enabled = cJSON_GetObjectItem(j, "enabled");
    const cJSON *category = cJSON_GetObjectItem(j, "category");
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
    strlcpy(s->category, submeter_category_valid(category), sizeof(s->category));
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

// GET /api/energy/temperature?time=hour|day : courbes de temperature du graphe d'usage
static esp_err_t temperature_handler(httpd_req_t *req)
{
    char t[8];
    period_param(req, t, sizeof(t));
    return send_json_owned(req, temp_history_chart_json(t));
}

/**
 * GET /api/energy/history?gran=hour|day|week|month|year&from=aaaammjj&to=aaaammjj[&ieee=]
 * Page Energie > Historique (archive datee)
 */
static esp_err_t history_page_handler(httpd_req_t *req)
{
    char g[8], f[12], t[12];
    query_param(req, "gran", g, sizeof(g), "day");
    query_param(req, "from", f, sizeof(f), "0");
    query_param(req, "to", t, sizeof(t), "0");
    return send_json_owned(req, energy_history_page_json(g, (uint32_t)strtoul(f, NULL, 10),
                                                         (uint32_t)strtoul(t, NULL, 10), ieee_param(req)));
}

// GET /api/energy/history/days : jours archives (calendrier)
static esp_err_t history_days_handler(httpd_req_t *req)
{
    return send_json_owned(req, energy_archive_days_json());
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
        { "/api/energy/advice",             HTTP_GET,  advice_handler },
        { "/api/energy/zlinkys",            HTTP_GET,  zlinkys_handler },
        { "/api/energy/history/clear",      HTTP_POST, history_clear_handler },
        { "/api/energy/history",            HTTP_GET,  history_page_handler },
        { "/api/energy/history/days",       HTTP_GET,  history_days_handler },
        { "/api/energy/presence",           HTTP_GET,  presence_handler },
        { "/api/energy/temperature",        HTTP_GET,  temperature_handler },
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
