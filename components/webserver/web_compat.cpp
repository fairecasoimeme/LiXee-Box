// ============================================================================
// Routes de compatibilite avec l'API publique de la LiXee-Box
//
// Clients existants (kiosque, scripts, domotique tierce) : memes chemins,
// memes noms de champs, memes types que web.cpp de la Box.
//   /api/system (/getSystem)                    etat reseau et passerelles
//   /api/network/wifi-signal (/api/wifiSignal)  RSSI, qualite, puissance
//   /api/devices (/getDevices), /api/device     fichiers bruts de /db
//   /api/device/energy, /api/device/power       historiques bruts de /hst
//   /api/templates (/getTemplates)              tous les templates
//   /api/energy/config                          roles, prix, seuils
//   /getPresenceSummary|History|Status|All      presence (presence.json)
//   /api/tariff, /getLinky, /exportEnergyChart, /api/debug  alias
//   POST /setConfigWiFi, /setResetDevice        WiFi (puis redemarrage)
// Les anciennes pages HTML (/statusEnergy...) sont redirigees vers l'interface
// par web_server.cpp.
// ============================================================================

#include "web_compat.h"
#include "web_server.h"
#include "config_manager.h"
#include "energy_service.h"
#include "energy_history.h"
#include "power_stats.h"
#include "thermostat.h"
#include "mqtt_manager.h"
#include "notifications.h"
#include "wifi_manager.h"
#include "eth_manager.h"
#include "device_data.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

static const char *TAG = "WEB_COMPAT";

#define DB_DIR     "/littlefs/db"
#define TP_DIR     "/littlefs/tp"
#define PRES_PATH  "/littlefs/hst/presence.json"
#define FILE_MAX   (256 * 1024)

// ----------------------------------------------------------------------------
// Outils
// ----------------------------------------------------------------------------

static esp_err_t send_json_str(httpd_req_t *req, const char *status, const char *json)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t send_cjson(httpd_req_t *req, cJSON *root)
{
    char *s = root ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (!s) return send_json_str(req, "500 Internal Server Error", "{}");
    esp_err_t e = send_json_str(req, "200 OK", s);
    free(s);
    return e;
}

static esp_err_t send_owned(httpd_req_t *req, char *json)
{
    if (!json) return send_json_str(req, "500 Internal Server Error", "{}");
    esp_err_t e = send_json_str(req, "200 OK", json);
    free(json);
    return e;
}

static bool query(httpd_req_t *req, const char *key, char *out, size_t len)
{
    char q[192];
    out[0] = '\0';
    return httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
           httpd_query_key_value(q, key, out, len) == ESP_OK && out[0];
}

/** IEEE de la requete : ieee, IEEE puis id (apiIeee de la Box), 16 chiffres hexa. */
static bool query_ieee(httpd_req_t *req, char *out)
{
    char v[24];
    if (!query(req, "ieee", v, sizeof(v)) && !query(req, "IEEE", v, sizeof(v)) && !query(req, "id", v, sizeof(v)))
        return false;
    if (strlen(v) < 16) return false;
    for (int i = 0; i < 16; i++) {
        char c = v[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) return false;
        out[i] = (char)((c >= 'a' && c <= 'f') ? c - 32 : c);
    }
    out[16] = '\0';
    return true;
}

/** Fichier entier en PSRAM (NULL si absent ou trop gros). */
static char *read_file(const char *path, size_t *len)
{
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= 0 || st.st_size > FILE_MAX) return NULL;
    char *buf = (char *)heap_caps_malloc(st.st_size + 1, MALLOC_CAP_SPIRAM);
    if (!buf) return NULL;
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(buf, 1, st.st_size, f) : 0;
    if (f) fclose(f);
    buf[n] = '\0';
    if (len) *len = n;
    return buf;
}

/** Contenu JSON objet plausible ("{ ... }"), sans espaces de fin. */
static bool json_object_text(const char *s, size_t n)
{
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) n--;
    return n >= 2 && s[0] == '{' && s[n - 1] == '}';
}

static void ip_str(uint32_t addr, char *out, size_t len)
{
    esp_ip4_addr_t a = { .addr = addr };
    snprintf(out, len, IPSTR, IP2STR(&a));
}

// ----------------------------------------------------------------------------
// /api/system, /api/network/wifi-signal
// ----------------------------------------------------------------------------

static esp_err_t system_handler(httpd_req_t *req)
{
    const cfg_settings_t *s = cfg_get_settings();
    const cfg_general_t *g = cfg_get_general();
    cJSON *root = cJSON_CreateObject();
    cJSON *net = cJSON_AddObjectToObject(root, "network");

    // WiFi : valeurs 0/1 comme la Box ; "mode" y porte le drapeau DHCP
    cJSON *w = cJSON_AddObjectToObject(net, "wifi");
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t wi = {};
    bool wconn = sta && esp_netif_get_ip_info(sta, &wi) == ESP_OK && wi.ip.addr != 0;
    char a[16], m[16], gw[16];
    ip_str(wi.ip.addr, a, sizeof(a));
    ip_str(wi.netmask.addr, m, sizeof(m));
    ip_str(wi.gw.addr, gw, sizeof(gw));
    cJSON_AddNumberToObject(w, "enable", s->enable_wifi ? 1 : 0);
    cJSON_AddNumberToObject(w, "connected", wconn ? 1 : 0);
    cJSON_AddNumberToObject(w, "mode", s->enable_dhcp ? 1 : 0);
    cJSON_AddStringToObject(w, "ip", a);
    cJSON_AddStringToObject(w, "netmask", m);
    cJSON_AddStringToObject(w, "gateway", gw);

    eth_mgr_status_t es;
    eth_mgr_get_status(&es);
    const eth_mgr_config_t *ec = eth_mgr_get_config();
    cJSON *e = cJSON_AddObjectToObject(net, "ethernet");
    ip_str(es.has_ip ? es.ip : 0, a, sizeof(a));
    ip_str(es.has_ip ? es.mask : 0, m, sizeof(m));
    ip_str(es.has_ip ? es.gw : 0, gw, sizeof(gw));
    cJSON_AddNumberToObject(e, "present", es.present ? 1 : 0);
    cJSON_AddNumberToObject(e, "connected", es.has_ip ? 1 : 0);
    cJSON_AddNumberToObject(e, "dhcp", (ec && ec->dhcp) ? 1 : 0);
    cJSON_AddStringToObject(e, "ip", a);
    cJSON_AddStringToObject(e, "netmask", m);
    cJSON_AddStringToObject(e, "gateway", gw);
    cJSON_AddStringToObject(net, "active", es.has_ip ? "ethernet" : (wconn ? "wifi" : "none"));

    cJSON *sys = cJSON_AddObjectToObject(root, "system");
    cJSON *mq = cJSON_AddObjectToObject(sys, "mqtt");
    cJSON_AddNumberToObject(mq, "enable", s->enable_mqtt ? 1 : 0);
    cJSON_AddNumberToObject(mq, "connected", mqtt_manager_is_connected() ? 1 : 0);
    cJSON_AddStringToObject(mq, "url", g->serv_mqtt);
    cJSON_AddNumberToObject(mq, "port", g->port_mqtt[0] ? atoi(g->port_mqtt) : 0);
    cJSON *wp = cJSON_AddObjectToObject(sys, "webpush");
    cJSON_AddNumberToObject(wp, "enable", s->enable_webpush ? 1 : 0);
    cJSON_AddNumberToObject(wp, "auth", g->webpush_auth ? 1 : 0);
    cJSON_AddStringToObject(wp, "url", g->serv_webpush);
    cJSON *inf = cJSON_AddObjectToObject(sys, "infos");
    float tc = web_api_chip_temperature();
    cJSON_AddNumberToObject(inf, "t", tc > -100 ? tc : 0);
    return send_cjson(req, root);
}

static esp_err_t wifi_signal_handler(httpd_req_t *req)
{
    wifi_ap_record_t ap;
    char buf[80];
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        int rssi = ap.rssi;
        int q = rssi >= -50 ? 100 : (rssi <= -100 ? 0 : 2 * (rssi + 100));
        int8_t pw = 0;
        esp_wifi_get_max_tx_power(&pw);
        snprintf(buf, sizeof(buf), "{\"rssi\":%d,\"quality\":%d,\"txPower\":%.1f}", rssi, q, pw / 4.0);
    } else {
        strlcpy(buf, "{\"rssi\":0,\"quality\":0,\"txPower\":0}", sizeof(buf));
    }
    return send_json_str(req, "200 OK", buf);
}

// ----------------------------------------------------------------------------
// Appareils et templates : fichiers bruts, envoyes par morceaux
// ----------------------------------------------------------------------------

/** {"<cle>" : <fichier>, ...} pour les fichiers JSON d'un repertoire. */
static esp_err_t send_dir_object(httpd_req_t *req, const char *dir, bool db)
{
    DIR *d = opendir(dir);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "{");
    int n = 0;
    struct dirent *de;
    while (d && (de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        size_t nl = strlen(name);
        if (nl < 6 || strcasecmp(name + nl - 5, ".json") != 0) continue;
        // /db : nrg_, pwr_... ecartes (4e caractere '_'), cle = IEEE (16 caracteres)
        if (db && (nl < 4 || name[3] == '_' || nl < 21)) continue;
        char path[300];
        snprintf(path, sizeof(path), "%s/%s", dir, name);
        size_t len = 0;
        char *buf = read_file(path, &len);
        if (!buf) continue;
        if (json_object_text(buf, len)) {
            char key[48];
            size_t kl = db ? 16 : nl - 5;
            if (kl >= sizeof(key)) kl = sizeof(key) - 1;
            memcpy(key, name, kl);
            key[kl] = '\0';
            char head[64];
            snprintf(head, sizeof(head), "%s\"%s\" : ", n++ ? "," : "", key);
            httpd_resp_sendstr_chunk(req, head);
            httpd_resp_send_chunk(req, buf, len);
        }
        heap_caps_free(buf);
    }
    if (d) closedir(d);
    httpd_resp_sendstr_chunk(req, "}");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t devices_handler(httpd_req_t *req)   { return send_dir_object(req, DB_DIR, true); }
static esp_err_t templates_handler(httpd_req_t *req) { return send_dir_object(req, TP_DIR, false); }

/** {"<IEEE>" : <fichier>} ; ieee absent : {} (comme la Box). */
static esp_err_t send_one_file(httpd_req_t *req, const char *fmt)
{
    char ieee[20];
    if (!query_ieee(req, ieee)) return send_json_str(req, "200 OK", "{}");
    char path[96];
    snprintf(path, sizeof(path), fmt, ieee);
    size_t len = 0;
    char *buf = read_file(path, &len);
    if (!buf) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "Echec de l'ouverture du fichier");
    }
    char head[40];
    snprintf(head, sizeof(head), "{\"%s\" : ", ieee);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, head);
    httpd_resp_send_chunk(req, buf, len);
    heap_caps_free(buf);
    httpd_resp_sendstr_chunk(req, "}");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t device_handler(httpd_req_t *req)        { return send_one_file(req, DB_DIR "/%s.json"); }
// La Box cherchait ces historiques dans /db au lieu de /hst (toujours 500) :
// ici le bon repertoire
static esp_err_t device_energy_handler(httpd_req_t *req) { return send_one_file(req, ENERGY_HST_DIR "/nrg_%s.json"); }
static esp_err_t device_power_handler(httpd_req_t *req)  { return send_one_file(req, ENERGY_HST_DIR "/pwr_%s.json"); }

// ----------------------------------------------------------------------------
// /api/energy/config
// ----------------------------------------------------------------------------

static double num(const char *s) { return s && s[0] ? atof(s) : 0; }

static esp_err_t energy_config_handler(httpd_req_t *req)
{
    const cfg_general_t *g = cfg_get_general();
    const cfg_notifications_t *n = cfg_get_notifications();
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "zlinky", g->zlinky);
    cJSON_AddStringToObject(r, "production", g->production);
    cJSON_AddStringToObject(r, "gaz", g->gaz);
    cJSON_AddStringToObject(r, "water", g->water);
    cJSON_AddNumberToObject(r, "linkyMode", g->linky_mode);
    cJSON *pr = cJSON_AddObjectToObject(r, "prices");
    for (int i = 1; i < 10; i++) {                 // tarifIdx2..10 -> 256..272
        double p = num(g->tarif_idx[i]);
        if (p <= 0) continue;
        char k[8];
        snprintf(k, sizeof(k), "%d", 256 + 2 * (i - 1));
        cJSON_AddNumberToObject(pr, k, p);
    }
    cJSON *sub = cJSON_AddObjectToObject(r, "subscription");
    cJSON_AddNumberToObject(sub, "abo", num(g->tarif_abo));
    cJSON_AddNumberToObject(sub, "cspe", num(g->tarif_cspe));
    cJSON_AddNumberToObject(sub, "cta", num(g->tarif_cta));
    cJSON *ga = cJSON_AddObjectToObject(r, "gas");
    cJSON_AddNumberToObject(ga, "price", num(g->tarif_gaz));
    cJSON_AddNumberToObject(ga, "coeff", g->coeff_gaz);
    cJSON_AddStringToObject(ga, "unit", g->unit_gaz);
    cJSON *wa = cJSON_AddObjectToObject(r, "water");
    cJSON_AddNumberToObject(wa, "price", num(g->tarif_water));
    cJSON_AddNumberToObject(wa, "coeff", g->coeff_water);
    cJSON_AddStringToObject(wa, "unit", g->unit_water);
    cJSON *sm = cJSON_AddArrayToObject(r, "subMeters");
    for (int i = 0; i < g->sub_meter_count && i < CFG_MAX_SUBMETERS; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "index", i);
        cJSON_AddStringToObject(o, "ieee", g->sub_meters[i].IEEE);
        cJSON_AddStringToObject(o, "alias", g->sub_meters[i].alias);
        cJSON_AddStringToObject(o, "color", g->sub_meters[i].color);
        cJSON_AddBoolToObject(o, "enabled", g->sub_meters[i].enabled);
        cJSON_AddItemToArray(sm, o);
    }
    cJSON *th = cJSON_AddObjectToObject(r, "thresholds");
    auto thr = [&](const char *key, bool en, const char *field, double v) {
        cJSON *o = cJSON_AddObjectToObject(th, key);
        cJSON_AddBoolToObject(o, "enabled", en);
        cJSON_AddNumberToObject(o, field, v);
        return o;
    };
    thr("budget", n->over_budget, "monthEur", n->over_budget_threshold);
    thr("overPower", n->over_power, "w", n->over_power_threshold);
    thr("dailyAnomaly", n->daily_anomaly, "percent", n->daily_anomaly_percent);
    thr("nightWaterLeak", n->night_water_leak, "litres", n->night_water_leak_threshold);
    thr("waterLeak", n->water_leak, "litres", n->water_leak_threshold);
    cJSON *fr = thr("freeze", n->freeze, "centiDeg", n->freeze_threshold);
    cJSON_AddStringToObject(fr, "sensor", n->freeze_sensor_ieee);
    cJSON *dl = cJSON_AddArrayToObject(r, "delestage");
    char tmp[sizeof(g->delestage)];
    strlcpy(tmp, g->delestage, sizeof(tmp));
    for (char *save = NULL, *t = strtok_r(tmp, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
        while (*t == ' ') t++;
        size_t l = strlen(t);
        while (l > 0 && t[l - 1] == ' ') t[--l] = '\0';
        if (l) cJSON_AddItemToArray(dl, cJSON_CreateString(t));
    }
    return send_cjson(req, r);
}

// ----------------------------------------------------------------------------
// Presence (presence.json, format de la Box)
// ----------------------------------------------------------------------------

static cJSON *presence_doc(void)
{
    char *buf = read_file(PRES_PATH, NULL);
    cJSON *doc = buf ? cJSON_Parse(buf) : NULL;
    if (buf) heap_caps_free(buf);
    if (!doc) {
        doc = cJSON_CreateObject();
        cJSON_AddObjectToObject(doc, "devices");
    }
    return doc;
}

/** Heure h du jour d (1..31) d'un appareil, 0 si absente. */
static int pres_bit(const cJSON *dev, int day, int h)
{
    char k[4];
    snprintf(k, sizeof(k), "%d", day);
    const cJSON *arr = cJSON_GetObjectItem(cJSON_GetObjectItem(dev, "days"), k);
    const cJSON *v = cJSON_GetArrayItem(arr, h);
    return cJSON_IsNumber(v) && v->valueint ? 1 : 0;
}

static esp_err_t presence_summary_handler(httpd_req_t *req)
{
    char sl[8] = "", dv[24] = "all", dy[4] = "";
    query(req, "sliding", sl, sizeof(sl));
    query(req, "device", dv, sizeof(dv));
    query(req, "day", dy, sizeof(dy));
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    int day = dy[0] ? atoi(dy) : t.tm_mday;
    bool sliding = strcmp(sl, "true") == 0 || strcmp(sl, "1") == 0;
    cJSON *doc = presence_doc();
    const cJSON *devs = cJSON_GetObjectItem(doc, "devices");
    char out[24 * 2 + 4] = "[";
    for (int i = 0; i < 24; i++) {
        int d = day, h = i;
        if (sliding) {
            // index 0 = heure courante + 1 de la veille, 23 = heure courante
            h = (t.tm_hour + 1 + i) % 24;
            d = (t.tm_hour + 1 + i) < 24 ? (t.tm_mday == 1 ? 31 : t.tm_mday - 1) : t.tm_mday;
        }
        int v = 0;
        const cJSON *dev;
        cJSON_ArrayForEach(dev, devs) {
            if (!dev->string) continue;
            if (strcasecmp(dv, "all") != 0 && strncasecmp(dev->string, dv, 16) != 0) continue;
            v |= pres_bit(dev, d, h);
        }
        size_t n = strlen(out);
        snprintf(out + n, sizeof(out) - n, "%s%d", i ? "," : "", v);
    }
    strlcat(out, "]", sizeof(out));
    cJSON_Delete(doc);
    return send_json_str(req, "200 OK", out);
}

static esp_err_t presence_history_handler(httpd_req_t *req)
{
    char dv[24];
    if (!query(req, "device", dv, sizeof(dv)))
        return send_json_str(req, "400 Bad Request", "{\"error\":\"Missing device parameter\"}");
    cJSON *doc = presence_doc();
    const cJSON *dev = NULL, *it;
    cJSON_ArrayForEach(it, cJSON_GetObjectItem(doc, "devices")) {
        if (it->string && strncasecmp(it->string, dv, 16) == 0) { dev = it; break; }
    }
    if (!dev) {
        cJSON_Delete(doc);
        return send_json_str(req, "200 OK", "{\"error\":\"Device not found\"}");
    }
    char *s = cJSON_PrintUnformatted(dev);
    cJSON_Delete(doc);
    return send_owned(req, s);
}

/** Presence en cours : detection positive dans les 30 dernieres minutes (presenceCache de la Box). */
static esp_err_t presence_status_handler(httpd_req_t *req)
{
    char dv[24] = "all";
    query(req, "device", dv, sizeof(dv));
    uint64_t ieee = strcasecmp(dv, "all") == 0 ? 0 : strtoull(dv, NULL, 16);
    bool p = (strcasecmp(dv, "all") == 0 || ieee) && thermostat_presence_recent(ieee);
    return send_json_str(req, "200 OK", p ? "{\"presence\":true}" : "{\"presence\":false}");
}

static esp_err_t presence_all_handler(httpd_req_t *req)
{
    cJSON *doc = presence_doc();
    return send_cjson(req, doc);
}

// ----------------------------------------------------------------------------
// Alias d'API et WiFi
// ----------------------------------------------------------------------------

static esp_err_t tariff_handler(httpd_req_t *req) { return send_owned(req, energy_tariff_json()); }

// Statistiques de puissance du ZLinky (kiosque LiXee-Assist)
static int query_days(httpd_req_t *req)
{
    char v[8];
    return query(req, "days", v, sizeof(v)) ? atoi(v) : 0;
}
static esp_err_t power_peaks_handler(httpd_req_t *req)   { return send_owned(req, power_stats_peaks_json(query_days(req))); }
static esp_err_t power_floor_handler(httpd_req_t *req)   { return send_owned(req, power_stats_floor_json(query_days(req))); }
static esp_err_t power_summary_handler(httpd_req_t *req) { return send_owned(req, power_stats_summary_json()); }
static esp_err_t linky_handler(httpd_req_t *req)  { return send_owned(req, energy_linky_json()); }
static esp_err_t debug_handler(httpd_req_t *req)  { return send_owned(req, notifications_debug_json()); }

static esp_err_t export_chart_handler(httpd_req_t *req)
{
    char t[8] = "hour", ie[20];
    query(req, "time", t, sizeof(t));
    uint64_t ieee = query_ieee(req, ie) ? strtoull(ie, NULL, 16) : 0;
    char *csv = energy_chart_csv(t, ieee);
    if (!csv) return send_json_str(req, "500 Internal Server Error", "{}");
    // Nom de fichier de la LiXee-Box : usage_electricite_<periode>_<AAAA-MM-JJ>.csv
    static char disp[96];                   // tache HTTP seule
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    snprintf(disp, sizeof(disp), "attachment; filename=\"usage_electricite_%s_%04d-%02d-%02d.csv\"", t,
             lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    httpd_resp_set_type(req, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    esp_err_t e = httpd_resp_sendstr(req, csv);
    free(csv);
    return e;
}

/** Corps de formulaire (ou requete) : champ decode (+, %xx). */
static bool form_field(const char *body, const char *key, char *out, size_t len)
{
    if (httpd_query_key_value(body, key, out, len) != ESP_OK) return false;
    char *o = out;
    for (char *s = out; *s; s++) {
        if (*s == '+') { *o++ = ' '; continue; }
        if (*s == '%' && s[1] && s[2]) {
            char hx[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(hx, NULL, 16);
            s += 2;
            continue;
        }
        *o++ = *s;
    }
    *o = '\0';
    return true;
}

static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static void reboot_later(void)
{
    xTaskCreate(reboot_task, "compat_reboot", 2048, NULL, 5, NULL);
}

/** POST /setConfigWiFi (ssid, password) : {"result":true|false}, puis redemarrage. */
static esp_err_t set_config_wifi_handler(httpd_req_t *req)
{
    static char body[768];                  // tache HTTP seule
    body[0] = '\0';
    int len = req->content_len;
    if (len > 0 && len < (int)sizeof(body)) {
        int got = 0;
        while (got < len) {
            int r = httpd_req_recv(req, body + got, len - got);
            if (r <= 0) break;
            got += r;
        }
        body[got > 0 ? got : 0] = '\0';
    }
    if (!body[0]) httpd_req_get_url_query_str(req, body, sizeof(body));
    // Champs encodes jusqu'a 3 fois plus longs ; un champ tronque est refuse
    static char ssid[3 * 33], pass[3 * 65];
    ssid[0] = pass[0] = '\0';
    bool okf = form_field(body, "ssid", ssid, sizeof(ssid)) && form_field(body, "password", pass, sizeof(pass));
    memset(body, 0, sizeof(body));
    bool ok = okf && ssid[0] && pass[0] && strlen(ssid) <= 32 && strlen(pass) <= 64 &&
              strcmp(ssid, "null") != 0 && strcmp(pass, "null") != 0 &&
              wifi_mgr_set_credentials(ssid, pass) == ESP_OK;
    memset(pass, 0, sizeof(pass));
    esp_err_t e = send_json_str(req, "200 OK", ok ? "{\"result\": true}" : "{\"result\" : false}");
    if (ok) {
        ESP_LOGI(TAG, "WiFi configure par /setConfigWiFi : redemarrage");
        reboot_later();
    }
    return e;
}

/** POST /setResetDevice : WiFi efface, DHCP, redemarrage. */
static esp_err_t set_reset_device_handler(httpd_req_t *req)
{
    wifi_mgr_clear_credentials();
    cfg_get_settings_mut()->enable_dhcp = true;
    cfg_save_wifi();
    esp_err_t e = send_json_str(req, "200 OK", "{\"result\": true}");
    ESP_LOGW(TAG, "WiFi reinitialise par /setResetDevice : redemarrage");
    reboot_later();
    return e;
}

// ----------------------------------------------------------------------------
// Enregistrement
// ----------------------------------------------------------------------------

void web_compat_register(httpd_handle_t server)
{
    static const struct {
        const char    *uri;
        httpd_method_t method;
        esp_err_t    (*fn)(httpd_req_t *);
    } routes[] = {
        { "/api/system",               HTTP_GET,  system_handler },
        { "/getSystem",                HTTP_GET,  system_handler },
        { "/api/network/wifi-signal",  HTTP_GET,  wifi_signal_handler },
        { "/api/wifiSignal",           HTTP_GET,  wifi_signal_handler },
        { "/api/devices",              HTTP_GET,  devices_handler },
        { "/getDevices",               HTTP_GET,  devices_handler },
        { "/api/device",               HTTP_GET,  device_handler },
        { "/getDevice",                HTTP_GET,  device_handler },
        { "/api/device/energy",        HTTP_GET,  device_energy_handler },
        { "/getEnergyDevice",          HTTP_GET,  device_energy_handler },
        { "/api/device/power",         HTTP_GET,  device_power_handler },
        { "/getPowerDevice",           HTTP_GET,  device_power_handler },
        { "/api/templates",            HTTP_GET,  templates_handler },
        { "/getTemplates",             HTTP_GET,  templates_handler },
        { "/api/energy/config",        HTTP_GET,  energy_config_handler },
        { "/getPresenceSummary",       HTTP_GET,  presence_summary_handler },
        { "/getPresenceHistory",       HTTP_GET,  presence_history_handler },
        { "/getPresenceStatus",        HTTP_GET,  presence_status_handler },
        { "/getPresenceAll",           HTTP_GET,  presence_all_handler },
        { "/api/tariff",               HTTP_GET,  tariff_handler },
        { "/api/power/peaks",          HTTP_GET,  power_peaks_handler },
        { "/api/power/floor",          HTTP_GET,  power_floor_handler },
        { "/api/power/summary",        HTTP_GET,  power_summary_handler },
        { "/getLinky",                 HTTP_GET,  linky_handler },
        { "/exportEnergyChart",        HTTP_GET,  export_chart_handler },
        { "/api/debug",                HTTP_GET,  debug_handler },
        { "/setConfigWiFi",            HTTP_POST, set_config_wifi_handler },
        { "/setResetDevice",           HTTP_POST, set_reset_device_handler },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_uri_t u = {};
        u.uri = routes[i].uri;
        u.method = routes[i].method;
        u.handler = routes[i].fn;
        web_server_register_uri(server, &u);
    }
}
