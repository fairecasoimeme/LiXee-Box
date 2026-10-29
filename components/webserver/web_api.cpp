// ============================================================================
// Web API - Endpoints REST pour le controle du bridge
// ============================================================================

#include "web_api.h"
#include "web_server.h"
#include "web_files.h"
#include "web_energy.h"
#include "web_rules.h"
#include "web_zigbee.h"
#include "web_maint.h"
#include "web_tunnel.h"
#include "web_lora.h"
#include "web_thermostat.h"
#include "web_notif.h"
#include "tunnel.h"
#include "lora.h"
#include "notifications.h"
#include "config_manager.h"
#include "device_data.h"
#include "zigate_driver.h"
#include "zigate_devices.h"
#include "matter_bridge.h"
#include "matter_device_manager.h"
#include "wifi_manager.h"
#include "eth_manager.h"
#include "esp_netif.h"
#include "energy_history.h"
#include "mqtt_manager.h"
#include "web_compat.h"
#include "ntp_sync.h"
#include "esp_log.h"
#include "nvs.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "driver/temperature_sensor.h"
#include "cJSON.h"
#include <string.h>

#ifndef VERSION
#define VERSION "3.0"
#endif

// Fonctions definies dans main.cpp pour le parametre Matter enable/disable
extern "C" bool main_is_matter_enabled(void);
extern "C" esp_err_t main_set_matter_enabled(bool enabled);

static const char *TAG = "WEB_API";

// ============================================================================
// Lecture du corps JSON d'une requete
//
// Remplace le motif "un seul httpd_req_recv() dans un tampon fixe" qui etait
// repete dans les 12 handlers POST, avec trois defauts :
//   - httpd_req_recv() ne fait qu'UN recv() : un corps segmente par TCP
//     arrivait tronque et etait rejete en "Invalid JSON", de facon
//     intermittente ;
//   - un corps plus grand que le tampon etait tronque sans le dire ;
//   - cJSON_Parse() est recursif et sa limite d'imbrication (1000) est hors
//     de portee de la pile de 8 Ko de la tache HTTP : quelques centaines de
//     '[' suffisaient a faire planter l'appareil a distance.
// ============================================================================

#define JSON_MAX_DEPTH 16

/** Verifie la profondeur d'imbrication, sans compter le contenu des chaines. */
static bool json_depth_ok(const char *s, int max_depth)
{
    int depth = 0;
    bool in_str = false, esc = false;
    for (; *s; s++) {
        char c = *s;
        if (in_str) {
            if (esc)            esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"')  in_str = false;
            continue;
        }
        if (c == '"') {
            in_str = true;
        } else if (c == '{' || c == '[') {
            if (++depth > max_depth) return false;
        } else if ((c == '}' || c == ']') && depth > 0) {
            depth--;
        }
    }
    return true;
}

/**
 * Lit tout le corps (jusqu'a content_len) et le parse en JSON.
 * @return document a liberer par cJSON_Delete(), ou NULL : la reponse
 *         d'erreur a alors deja ete envoyee.
 */
static cJSON *read_json_body(httpd_req_t *req, size_t max_len)
{
    size_t len = req->content_len;
    if (len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");
        return NULL;
    }
    if (len > max_len) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Body too large");
        return NULL;
    }

    char *buf = (char *)malloc(len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return NULL;
    }

    size_t got = 0;
    int timeouts = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
        if (r <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete body");
            return NULL;
        }
        got += (size_t)r;
    }
    buf[len] = '\0';

    if (!json_depth_ok(buf, JSON_MAX_DEPTH)) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON too deeply nested");
        return NULL;
    }

    cJSON *doc = cJSON_Parse(buf);
    free(buf);
    if (!doc) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return NULL;
    }
    return doc;
}

/**
 * Repond au resultat d'une sauvegarde de configuration. Un echec part en 500 :
 * repondre 200 avec {"status":"error"} faisait afficher "enregistre" par le
 * frontend alors que rien n'etait persiste.
 */
static esp_err_t send_save_result(httpd_req_t *req, esp_err_t err)
{
    httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":"
                                "\"Echec d'ecriture de la configuration\"}");
        return ESP_OK;
    }
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/** Copie une chaine JSON dans un champ de taille fixe si la cle est presente. */
static void json_take_str(const cJSON *doc, const char *key, char *dst, size_t dst_size)
{
    const cJSON *it = cJSON_GetObjectItem(doc, key);
    if (it && cJSON_IsString(it)) strlcpy(dst, it->valuestring, dst_size);
}

/** Lit un nombre envoye en nombre ou en chaine (virgule decimale toleree). */
static bool json_take_num(const cJSON *doc, const char *key, double *out)
{
    const cJSON *it = cJSON_GetObjectItem(doc, key);
    if (!it) return false;
    if (cJSON_IsNumber(it)) { *out = it->valuedouble; return true; }
    if (cJSON_IsString(it) && it->valuestring[0]) {
        char tmp[24];
        strlcpy(tmp, it->valuestring, sizeof(tmp));
        for (char *p = tmp; *p; p++) if (*p == ',') *p = '.';
        char *end;
        double v = strtod(tmp, &end);
        if (end != tmp) { *out = v; return true; }
    }
    return false;
}

/** Reponse 400 portant un message lisible par le frontend. */
static esp_err_t send_bad_request(httpd_req_t *req, const char *message)
{
    char body[160];
    snprintf(body, sizeof(body), "{\"status\":\"error\",\"message\":\"%s\"}", message);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, body);
    return ESP_OK;
}

// ============================================================================
// GET /api/matter/status - Etat du bridge Matter
// ============================================================================

static esp_err_t api_matter_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", main_is_matter_enabled());
    cJSON_AddBoolToObject(root, "commissioned", matter_bridge_is_commissioned());
    cJSON_AddNumberToObject(root, "zigbeeDevices", zigate_devices_count());
    cJSON_AddBoolToObject(root, "started", matter_bridge_is_started());
    // Appareils pontes (Zigbee, LoRa, thermostats virtuels) et limite de la pile
    cJSON_AddNumberToObject(root, "bridgedDevices", matter_bridge_bridged_count());
    cJSON_AddNumberToObject(root, "maxDevices", matter_bridge_bridged_max());
    cJSON_AddNumberToObject(root, "fabrics", matter_bridge_fabric_count());
    cJSON_AddBoolToObject(root, "windowOpen", matter_bridge_commissioning_window_open());

    // Codes d'appairage : a saisir dans Home Assistant (ou tout controleur)
    const char *qr = NULL, *manual = NULL;
    if (matter_bridge_get_onboarding_codes(&qr, &manual)) {
        cJSON_AddStringToObject(root, "qrCode", qr);
        cJSON_AddStringToObject(root, "manualCode", manual);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ============================================================================
// POST /api/matter/open-window {"seconds":900} - fenetre d'appairage
// (ajout d'un second controleur avec le code habituel du pont)
// ============================================================================

static esp_err_t api_matter_open_window_handler(httpd_req_t *req)
{
    long secs = 900;
    if (req->content_len > 0) {
        cJSON *j = read_json_body(req, 256);
        if (!j) return ESP_FAIL;
        const cJSON *v = cJSON_GetObjectItem(j, "seconds");
        if (cJSON_IsNumber(v)) secs = (long)v->valuedouble;
        cJSON_Delete(j);
    }
    esp_err_t e = matter_bridge_open_commissioning_window((uint16_t)(secs > 0 && secs < 65536 ? secs : 900));
    httpd_resp_set_type(req, "application/json");
    if (e != ESP_OK) {
        httpd_resp_set_status(req, e == ESP_ERR_INVALID_STATE ? "409 Conflict" : "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Ouverture impossible (Matter non d\u00e9marr\u00e9 ?)\"}");
    }
    return httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Fen\u00eatre d'appairage ouverte\"}");
}

// ============================================================================
// POST /api/matter/config - Active/desactive Matter (necessite reboot)
// Body: {"enabled": true/false}
// ============================================================================

static esp_err_t api_matter_config_handler(httpd_req_t *req)
{
    cJSON *json = read_json_body(req, 1024);
    if (!json) return ESP_FAIL;

    cJSON *enabled_j = cJSON_GetObjectItem(json, "enabled");
    if (!enabled_j) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'enabled' field");
        return ESP_FAIL;
    }

    bool enabled = cJSON_IsTrue(enabled_j);
    cJSON_Delete(json);

    esp_err_t err = main_set_matter_enabled(enabled);
    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        httpd_resp_sendstr(req, enabled ?
            "{\"status\":\"ok\",\"enabled\":true,\"message\":\"Matter enabled. Reboot to apply.\"}" :
            "{\"status\":\"ok\",\"enabled\":false,\"message\":\"Matter disabled. Reboot to apply.\"}");
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS write failed");
    }
    return ESP_OK;
}

// ============================================================================
// GET  /api/matter/excluded - appareils retires de Matter (IEEE / UniqueID)
// POST /api/matter/expose   - {"ieee": "...", "expose": true|false}
// ============================================================================

static esp_err_t api_matter_excluded_handler(httpd_req_t *req)
{
    char *json = matter_device_manager_excluded_json();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json ? json : "[]");
    free(json);
    return ESP_OK;
}

static esp_err_t api_matter_expose_handler(httpd_req_t *req)
{
    cJSON *json = read_json_body(req, 256);
    if (!json) return ESP_FAIL;
    const cJSON *ieee_j = cJSON_GetObjectItem(json, "ieee");
    const cJSON *exp_j = cJSON_GetObjectItem(json, "expose");
    uint64_t ieee = cJSON_IsString(ieee_j) ? strtoull(ieee_j->valuestring, NULL, 16) : 0;
    bool expose = cJSON_IsTrue(exp_j);
    bool has_exp = cJSON_IsBool(exp_j);
    cJSON_Delete(json);
    if (!ieee || !has_exp) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ieee et expose requis");
        return ESP_FAIL;
    }
    esp_err_t err = matter_device_manager_set_excluded(ieee, !expose);
    httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Liste des appareils retires pleine\"}");
        return ESP_OK;
    }
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// ============================================================================
// POST /api/matter/factory_reset
// ============================================================================

static esp_err_t api_matter_factory_reset_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Factory reset requested via API");
    matter_bridge_factory_reset();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Rebooting...\"}");
    return ESP_OK;
}

// ============================================================================
// GET /api/system/info
// ============================================================================

// Capteur de temperature interne de l'ESP32-S3 (puce, pas l'ambiance),
// installe a la premiere demande. -1000 si indisponible.
static float read_chip_temperature(void)
{
    static temperature_sensor_handle_t s_temp = NULL;
    static bool s_temp_failed = false;
    if (!s_temp && !s_temp_failed) {
        temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
        if (temperature_sensor_install(&cfg, &s_temp) != ESP_OK ||
            temperature_sensor_enable(s_temp) != ESP_OK) {
            s_temp_failed = true;
            s_temp = NULL;
        }
    }
    float t = -1000.0f;
    if (s_temp && temperature_sensor_get_celsius(s_temp, &t) != ESP_OK) t = -1000.0f;
    return t;
}

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "poweron";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int_watchdog";
    case ESP_RST_TASK_WDT:  return "task_watchdog";
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    default:                return "other";
    }
}

static esp_err_t api_system_info_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", VERSION);
    cJSON_AddNumberToObject(root, "freeHeap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "minFreeHeap", esp_get_minimum_free_heap_size());

    // Puce et firmware
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    char chip_str[48];
    snprintf(chip_str, sizeof(chip_str), "%s rev %d.%d, %d coeurs", CONFIG_IDF_TARGET,
             chip.revision / 100, chip.revision % 100, chip.cores);
    cJSON_AddStringToObject(root, "chip", chip_str);
    // esp_get_idf_version() rend "-128-NOTFOUND" quand l'arbre ESP-IDF n'est
    // pas un depot git : les constantes de version sont toujours exactes
    char idf_ver[24];
    snprintf(idf_ver, sizeof(idf_ver), "ESP-IDF v%d.%d.%d", ESP_IDF_VERSION_MAJOR,
             ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
    cJSON_AddStringToObject(root, "sdkVersion", idf_ver);
    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        cJSON_AddNumberToObject(root, "flashSize", flash_size);
    }
    cJSON_AddNumberToObject(root, "uptime", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddStringToObject(root, "resetReason", reset_reason_str(esp_reset_reason()));

    // Memoire : la RAM interne est la ressource critique du pont
    cJSON_AddNumberToObject(root, "psramSize", heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(root, "psramFree", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(root, "internalFree", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "internalMinFree", heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    {
        // Occupation de la NVS (entrees de 32 octets) : WiFi, Matter (fabrics, etat)
        nvs_stats_t ns = {};
        const esp_partition_t *np = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
        if (nvs_get_stats(NULL, &ns) == ESP_OK) {
            cJSON *n = cJSON_AddObjectToObject(root, "nvs");
            cJSON_AddNumberToObject(n, "size", np ? np->size : 0);
            cJSON_AddNumberToObject(n, "usedEntries", ns.used_entries);
            cJSON_AddNumberToObject(n, "freeEntries", ns.free_entries);
            cJSON_AddNumberToObject(n, "totalEntries", ns.total_entries);
            cJSON_AddNumberToObject(n, "namespaces", ns.namespace_count);
        }
    }

    float temp = read_chip_temperature();
    if (temp > -100.0f) {
        cJSON_AddNumberToObject(root, "temperature", (int)(temp * 10.0f + (temp >= 0 ? 0.5f : -0.5f)) / 10.0);
    }

    // Reseau : interface utilisee (Ethernet prioritaire), IP et MAC
    eth_mgr_status_t eth;
    eth_mgr_get_status(&eth);
    char buf[24];
    if (eth.has_ip) {
        esp_ip4_addr_t a = { .addr = eth.ip };
        snprintf(buf, sizeof(buf), IPSTR, IP2STR(&a));
        cJSON_AddStringToObject(root, "ip", buf);
        cJSON_AddStringToObject(root, "interface", "ethernet");
    } else {
        esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ipi;
        if (sta && esp_netif_get_ip_info(sta, &ipi) == ESP_OK && ipi.ip.addr) {
            snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ipi.ip));
            cJSON_AddStringToObject(root, "ip", buf);
            cJSON_AddStringToObject(root, "interface", "wifi");
        }
    }
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        cJSON_AddStringToObject(root, "mac", buf);
    }
    char hostname[20];
    if (wifi_mgr_get_hostname(hostname, sizeof(hostname)) == ESP_OK) {
        cJSON_AddStringToObject(root, "hostname", hostname);
    }

    // WiFi info
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        cJSON_AddStringToObject(root, "ssid", (char *)ap_info.ssid);
        cJSON_AddNumberToObject(root, "rssi", ap_info.rssi);
    }

    // Modules presents : la page masque les menus des modules absents
    // (equivalent de /modules.js de la LiXee-Box)
    // Protection HTTP active : le menu propose "Deconnexion" (LiXee-Box)
    cJSON_AddBoolToObject(root, "secureHttp", cfg_get_settings()->enable_secure_http &&
                                              cfg_get_general()->user_http[0] && cfg_get_general()->pass_http[0]);
    cJSON *mods = cJSON_AddObjectToObject(root, "modules");
    cJSON_AddBoolToObject(mods, "zigbee", zigate_is_detected());
    cJSON_AddBoolToObject(mods, "ethernet", eth.detected);
    cJSON_AddBoolToObject(mods, "lora", lora_is_detected());
    cJSON_AddBoolToObject(mods, "matter", main_is_matter_enabled());

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ============================================================================
// POST /api/system/reboot
// ============================================================================

static esp_err_t api_system_reboot_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Rebooting...\"}");
    // Reboot differe pour que la reponse HTTP soit envoyee
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// ============================================================================
// GET /api/config/wifi - Etat WiFi courant
// ============================================================================

static esp_err_t api_config_wifi_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    // Etat connexion
    cJSON_AddBoolToObject(root, "connected", wifi_mgr_is_connected());
    cJSON_AddBoolToObject(root, "bleActive", wifi_mgr_is_ble_active());

    // Nom BLE
    char ble_name[20];
    if (wifi_mgr_get_ble_device_name(ble_name, sizeof(ble_name)) == ESP_OK) {
        cJSON_AddStringToObject(root, "bleDeviceName", ble_name);
    }

    // Infos WiFi si connecte
    char ssid[33] = {};
    char ip[16] = {};
    int8_t rssi = 0;
    if (wifi_mgr_get_info(ssid, sizeof(ssid), ip, sizeof(ip), &rssi) == ESP_OK) {
        cJSON_AddStringToObject(root, "ssid", ssid);
        cJSON_AddStringToObject(root, "ip", ip);
        cJSON_AddNumberToObject(root, "rssi", rssi);
    }

    // Adressage : DHCP ou IP fixe (configWifi.json)
    {
        const cfg_settings_t *st = cfg_get_settings();
        cJSON_AddBoolToObject(root, "enableDHCP", st->enable_dhcp);
        cJSON_AddStringToObject(root, "staticIp", st->ip_address);
        cJSON_AddStringToObject(root, "staticMask", st->ip_mask);
        cJSON_AddStringToObject(root, "staticGw", st->ip_gateway);
    }

    // SSID sauvegarde en NVS
    wifi_mgr_credentials_t creds = {};
    if (wifi_mgr_get_credentials(&creds) == ESP_OK) {
        cJSON_AddStringToObject(root, "savedSsid", creds.ssid);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ============================================================================
// POST /api/config/wifi - Sauvegarde credentials WiFi + restart
// Body: {"ssid": "...", "password": "..."}
// ============================================================================

/**
 * Applique des identifiants WiFi recus en JSON, puis redemarre.
 * Libere 'json' dans tous les cas.
 *
 * Commun a POST /api/config/wifi et a POST /saveWifi. Ce dernier ecrivait dans
 * configWifi.json, un fichier que la connexion WiFi ne lit jamais (elle utilise
 * exclusivement la NVS) : il repondait "ok" sans aucun effet, et recopiait au
 * passage le mot de passe WiFi en clair sur LittleFS.
 */
static esp_err_t apply_wifi_credentials(httpd_req_t *req, cJSON *json)
{
    // Noms actuels, puis noms historiques de l'ancien firmware
    cJSON *ssid_j = cJSON_GetObjectItem(json, "ssid");
    if (!ssid_j) ssid_j = cJSON_GetObjectItem(json, "WIFISSID");
    cJSON *pass_j = cJSON_GetObjectItem(json, "password");
    if (!pass_j) pass_j = cJSON_GetObjectItem(json, "pass");
    if (!pass_j) pass_j = cJSON_GetObjectItem(json, "WIFIpassword");

    const char *ssid     = (ssid_j && cJSON_IsString(ssid_j)) ? ssid_j->valuestring : "";
    const char *password = (pass_j && cJSON_IsString(pass_j)) ? pass_j->valuestring : "";
    size_t ssid_len = strlen(ssid);
    size_t pass_len = strlen(password);

    // Bornes 802.11 / WPA. Un SSID de plus de 32 octets etait accepte, ecrit
    // en NVS, puis illisible au redemarrage (tampon de 33 octets) : la
    // passerelle repartait en provisioning BLE, injoignable en WiFi.
    const char *err_msg = NULL;
    if (ssid_len == 0 || ssid_len > 32)     err_msg = "SSID invalide (1 a 32 caracteres)";
    else if (pass_len > 64)                 err_msg = "Mot de passe trop long (64 caracteres max)";
    else if (pass_len > 0 && pass_len < 8)  err_msg = "Mot de passe trop court (8 caracteres min)";

    // Adressage (facultatif) : DHCP ou IP fixe, verifiee avant tout enregistrement
    const cJSON *dhcp_j = cJSON_GetObjectItem(json, "dhcp");
    bool set_addr = cJSON_IsBool(dhcp_j);
    bool dhcp = !set_addr || cJSON_IsTrue(dhcp_j);
    const char *sip = "", *smask = "", *sgw = "";
    if (!err_msg && set_addr && !dhcp) {
        const cJSON *a = cJSON_GetObjectItem(json, "ip");
        const cJSON *m = cJSON_GetObjectItem(json, "mask");
        const cJSON *g = cJSON_GetObjectItem(json, "gw");
        sip = cJSON_IsString(a) ? a->valuestring : "";
        smask = cJSON_IsString(m) ? m->valuestring : "";
        sgw = cJSON_IsString(g) ? g->valuestring : "";
        esp_ip4_addr_t t;
        if (esp_netif_str_to_ip4(sip, &t) != ESP_OK || esp_netif_str_to_ip4(smask, &t) != ESP_OK ||
            esp_netif_str_to_ip4(sgw, &t) != ESP_OK) {
            err_msg = "Adresse IP, masque ou passerelle invalide";
        }
    }

    if (err_msg) {
        cJSON_Delete(json);
        return send_bad_request(req, err_msg);
    }

    if (set_addr) {
        cfg_settings_t *st = cfg_get_settings_mut();
        st->enable_dhcp = dhcp;
        if (!dhcp) {
            strlcpy(st->ip_address, sip, sizeof(st->ip_address));
            strlcpy(st->ip_mask, smask, sizeof(st->ip_mask));
            strlcpy(st->ip_gateway, sgw, sizeof(st->ip_gateway));
        }
        cfg_save_wifi();
    }

    // Mot de passe vide sur le MEME reseau = conserver l'actuel : la page ne
    // peut pas le reafficher, et l'effacer ferait basculer vers un reseau
    // ouvert inexistant, donc vers le provisioning BLE au redemarrage.
    wifi_mgr_credentials_t cur = {};
    bool keep_pass = (pass_len == 0 &&
                      wifi_mgr_get_credentials(&cur) == ESP_OK && cur.valid &&
                      strcmp(cur.ssid, ssid) == 0);

    ESP_LOGI(TAG, "WiFi credentials update via API: SSID='%s'%s",
             ssid, keep_pass ? " (mot de passe conserve)" : "");

    esp_err_t err = wifi_mgr_set_credentials(ssid, keep_pass ? cur.password : password);
    memset(&cur, 0, sizeof(cur));
    cJSON_Delete(json);

    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS write failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"WiFi saved. Rebooting...\"}");

    // Reboot differe
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return ESP_OK;
}

static esp_err_t api_config_wifi_post_handler(httpd_req_t *req)
{
    cJSON *json = read_json_body(req, 512);
    if (!json) return ESP_FAIL;
    return apply_wifi_credentials(req, json);
}

// ============================================================================
// GET /api/config/ethernet - Etat et configuration de l'Ethernet W5500
// ============================================================================

static void add_ip4(cJSON *o, const char *key, uint32_t addr)
{
    char buf[16];
    esp_ip4_addr_t a = { .addr = addr };
    snprintf(buf, sizeof(buf), IPSTR, IP2STR(&a));
    cJSON_AddStringToObject(o, key, addr ? buf : "");
}

static esp_err_t api_config_ethernet_get_handler(httpd_req_t *req)
{
    eth_mgr_status_t st;
    eth_mgr_get_status(&st);
    const eth_mgr_config_t *cfg = eth_mgr_get_config();

    cJSON *root = cJSON_CreateObject();
    // Etat
    cJSON_AddBoolToObject(root, "detected", st.detected);
    cJSON_AddBoolToObject(root, "present", st.present);
    cJSON_AddBoolToObject(root, "linkUp", st.link_up);
    cJSON_AddBoolToObject(root, "hasIp", st.has_ip);
    add_ip4(root, "ip", st.ip);
    add_ip4(root, "mask", st.mask);
    add_ip4(root, "gw", st.gw);
    add_ip4(root, "dns", st.dns);
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             st.mac[0], st.mac[1], st.mac[2], st.mac[3], st.mac[4], st.mac[5]);
    cJSON_AddStringToObject(root, "mac", st.detected ? mac : "");
    cJSON_AddNumberToObject(root, "speed", st.speed_mbps);
    cJSON_AddBoolToObject(root, "fullDuplex", st.full_duplex);
    cJSON_AddStringToObject(root, "interface",
                            st.has_ip ? "ethernet" : (wifi_mgr_is_connected() ? "wifi" : ""));
    // Configuration (appliquee au prochain demarrage)
    cJSON *c = cJSON_AddObjectToObject(root, "config");
    cJSON_AddNumberToObject(c, "mode", cfg->mode);
    cJSON_AddNumberToObject(c, "dhcp", cfg->dhcp ? 1 : 0);
    cJSON_AddStringToObject(c, "ip", cfg->ip);
    cJSON_AddStringToObject(c, "mask", cfg->mask);
    cJSON_AddStringToObject(c, "gw", cfg->gw);
    cJSON_AddStringToObject(c, "dns", cfg->dns);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ============================================================================
// POST /api/config/ethernet - Mode et adressage (pris en compte au redemarrage)
// Body: {"mode":0|1|2, "dhcp":1|0, "ip":"", "mask":"", "gw":"", "dns":""}
// ============================================================================

static bool ip4_valid(const char *s)
{
    esp_ip4_addr_t a;
    return s && s[0] && esp_netif_str_to_ip4(s, &a) == ESP_OK;
}

static esp_err_t api_config_ethernet_post_handler(httpd_req_t *req)
{
    cJSON *json = read_json_body(req, 512);
    if (!json) return ESP_FAIL;

    eth_mgr_config_t cfg = *eth_mgr_get_config();
    const cJSON *m = cJSON_GetObjectItem(json, "mode");
    if (cJSON_IsNumber(m)) cfg.mode = (uint8_t)m->valueint;
    const cJSON *d = cJSON_GetObjectItem(json, "dhcp");
    if (cJSON_IsNumber(d)) cfg.dhcp = d->valueint != 0;
    else if (cJSON_IsBool(d)) cfg.dhcp = cJSON_IsTrue(d);
    json_take_str(json, "ip",   cfg.ip,   sizeof(cfg.ip));
    json_take_str(json, "mask", cfg.mask, sizeof(cfg.mask));
    json_take_str(json, "gw",   cfg.gw,   sizeof(cfg.gw));
    json_take_str(json, "dns",  cfg.dns,  sizeof(cfg.dns));
    cJSON_Delete(json);

    // Une IP fixe incomplete laisserait le pont injoignable en Ethernet seul
    const char *err_msg = NULL;
    if (cfg.mode > ETH_MODE_WIFI_ONLY) {
        err_msg = "Mode invalide";
    } else if (!cfg.dhcp && (!ip4_valid(cfg.ip) || !ip4_valid(cfg.mask) || !ip4_valid(cfg.gw))) {
        err_msg = "IP fixe : adresse, masque et passerelle valides requis";
    } else if (!cfg.dhcp && cfg.dns[0] && !ip4_valid(cfg.dns)) {
        err_msg = "DNS invalide";
    }
    if (err_msg) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "400 Bad Request");
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"status\":\"error\",\"message\":\"%s\"}", err_msg);
        httpd_resp_sendstr(req, buf);
        return ESP_OK;
    }

    // Comme la LiXee-Box : la nouvelle configuration s'applique au redemarrage,
    // lance automatiquement une fois la reponse partie
    esp_err_t err = eth_mgr_save_config(&cfg);
    if (err != ESP_OK) return send_save_result(req, err);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"reboot\":true,\"message\":\"Configuration Ethernet enregistr\u00e9e, red\u00e9marrage\"}");
    ESP_LOGI(TAG, "Configuration Ethernet enregistree : redemarrage");
    xTaskCreate([](void *) { vTaskDelay(pdMS_TO_TICKS(1500)); esp_restart(); }, "eth_reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

// ============================================================================
// GET /poll - Endpoint de sante pour LiXee-Assist
// L'app mobile fait GET /poll pour verifier si le device est en ligne
// (iOS: test direct .local, Android: apres resolution mDNS)
// ============================================================================

static esp_err_t api_poll_handler(httpd_req_t *req)
{
    // Retourner le nom du device — LiXee-Assist verifie que la reponse
    // contient le nom du device pour confirmer l'identite
    char hostname[20];
    wifi_mgr_get_hostname(hostname, sizeof(hostname));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", hostname);
    cJSON_AddStringToObject(root, "status", "online");
    cJSON_AddStringToObject(root, "version", VERSION);
    // Dernieres notifications pour les widgets LiXee-Assist (la file est
    // videe par cette lecture, comme sur la LiXee-Box)
    notifications_poll_add_to(root);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ============================================================================
// GET /api/config/general - Retourne la config generale
// ============================================================================

static esp_err_t api_config_general_get_handler(httpd_req_t *req)
{
    const cfg_general_t  *g = cfg_get_general();
    const cfg_settings_t *s = cfg_get_settings();
    const cfg_notifications_t *n = cfg_get_notifications();

    cJSON *root = cJSON_CreateObject();

    // Devices principaux
    cJSON_AddStringToObject(root, "ZLinky",     g->zlinky);
    cJSON_AddStringToObject(root, "Production", g->production);
    cJSON_AddStringToObject(root, "Gaz",        g->gaz);
    cJSON_AddStringToObject(root, "Water",      g->water);

    // NTP
    cJSON_AddStringToObject(root, "ntpserver", g->ntp_server);
    cJSON_AddNumberToObject(root, "timeoffset", g->time_offset);
    cJSON_AddStringToObject(root, "timezone",  g->timezone);
    cJSON_AddNumberToObject(root, "epoch",     (double)g->epoch_time);

    // Surface
    cJSON_AddNumberToObject(root, "shon", g->house_surface);

    // Feature flags
    cJSON_AddBoolToObject(root, "enableDebug",      s->enable_debug);
    cJSON_AddBoolToObject(root, "enableHWFlow",     s->enable_hw_flow);
    cJSON_AddBoolToObject(root, "developerMode",    g->developer_mode);
    cJSON_AddBoolToObject(root, "enableHistory",    s->enable_history);

    // Tarifs
    const char *tarif_keys[] = {
        "tarifIdx1","tarifIdx2","tarifIdx3","tarifIdx4","tarifIdx5",
        "tarifIdx6","tarifIdx7","tarifIdx8","tarifIdx9","tarifIdx10"
    };
    for (int i = 0; i < 10; i++) {
        cJSON_AddStringToObject(root, tarif_keys[i], g->tarif_idx[i]);
    }
    cJSON_AddStringToObject(root, "tarifAbo",  g->tarif_abo);
    cJSON_AddStringToObject(root, "tarifCSPE", g->tarif_cspe);
    cJSON_AddStringToObject(root, "tarifCTA",  g->tarif_cta);
    cJSON_AddStringToObject(root, "tarifIdxProd", g->tarif_idx_prod);
    cJSON_AddStringToObject(root, "tarifAboProd", g->tarif_abo_prod);

    // Gaz / Eau / Presence : persistes, pas encore exploites par le firmware
    cJSON_AddNumberToObject(root, "coeffGaz",   g->coeff_gaz);
    cJSON_AddStringToObject(root, "unitGaz",    g->unit_gaz);
    cJSON_AddStringToObject(root, "tarifGaz",   g->tarif_gaz);
    cJSON_AddNumberToObject(root, "coeffWater", g->coeff_water);
    cJSON_AddStringToObject(root, "unitWater",  g->unit_water);
    cJSON_AddStringToObject(root, "tarifWater", g->tarif_water);
    cJSON_AddStringToObject(root, "Presence",   g->presence);
    cJSON_AddBoolToObject(root,   "enablePresenceGraph", g->enable_presence_graph);
    cJSON_AddStringToObject(root, "delestage",           g->delestage);
    cJSON_AddNumberToObject(root, "delestageRestoreMin", g->delestage_restore_min);
    cJSON_AddNumberToObject(root, "delestageRestorePct", g->delestage_restore_pct);
    cJSON_AddNumberToObject(root, "delestageAnticipPct", g->delestage_anticip_pct);

    // Notifications
    cJSON_AddBoolToObject(root, "PowerOutage",     n->power_outage);
    cJSON_AddBoolToObject(root, "PriceChange",     n->price_change);
    cJSON_AddBoolToObject(root, "SubscribedPower", n->subscribed_power);
    cJSON_AddBoolToObject(root, "ProdSupConso",    n->prod_sup_conso);
    cJSON_AddBoolToObject(root, "ProdZero",        n->prod_zero);
    cJSON_AddBoolToObject(root, "OverBudget",      n->over_budget);
    cJSON_AddNumberToObject(root, "OverBudgetThreshold", n->over_budget_threshold);
    cJSON_AddBoolToObject(root, "OverVoltage",     n->over_voltage);
    cJSON_AddNumberToObject(root, "OverVoltageThreshold", n->over_voltage_threshold);
    cJSON_AddBoolToObject(root,   "UnderVoltage",    n->under_voltage);
    cJSON_AddNumberToObject(root, "UnderVoltageThreshold", n->under_voltage_threshold);
    cJSON_AddBoolToObject(root,   "PEJP",            n->pejp);
    cJSON_AddBoolToObject(root,   "RedColor",        n->red_color);
    cJSON_AddBoolToObject(root,   "ColorTomorrow",   n->color_tomorrow);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ============================================================================
// POST /api/config/general - Sauvegarde la config generale
// ============================================================================

static esp_err_t api_config_general_post_handler(httpd_req_t *req)
{
    cJSON *doc = read_json_body(req, 4096);
    if (!doc) return ESP_FAIL;

    cfg_general_t  *g = cfg_get_general_mut();
    cfg_settings_t *s = cfg_get_settings_mut();

    // Mettre a jour les champs presents dans le JSON
    cJSON *item;
    if ((item = cJSON_GetObjectItem(doc, "ZLinky")) && cJSON_IsString(item))
        strlcpy(g->zlinky, item->valuestring, sizeof(g->zlinky));
    if ((item = cJSON_GetObjectItem(doc, "Production")) && cJSON_IsString(item))
        strlcpy(g->production, item->valuestring, sizeof(g->production));
    double num;
    if (json_take_num(doc, "shon", &num)) g->house_surface = (int)num;

    // Champs de la page Config Energie. Ils existaient dans la configuration et
    // etaient persistes, mais aucun endpoint ne savait les ecrire.
    json_take_str(doc, "Gaz",      g->gaz,      sizeof(g->gaz));
    json_take_str(doc, "Water",    g->water,    sizeof(g->water));
    json_take_str(doc, "Presence", g->presence, sizeof(g->presence));

    // Delestage (bornes de la LiXee-Box)
    json_take_str(doc, "delestage", g->delestage, sizeof(g->delestage));
    {
        double v;
        if (json_take_num(doc, "delestageRestoreMin", &v)) g->delestage_restore_min = v < 0 ? 0 : (v > 240 ? 240 : (int)v);
        if (json_take_num(doc, "delestageRestorePct", &v)) g->delestage_restore_pct = v < 10 ? 10 : (v > 100 ? 100 : (int)v);
        if (json_take_num(doc, "delestageAnticipPct", &v)) {
            int a = v < 0 ? 0 : (v > 100 ? 100 : (int)v);
            // Au-dessus du seuil de rallumage, sinon les prises seraient coupees
            // et rallumees en boucle
            if (a > 0 && a <= g->delestage_restore_pct) a = g->delestage_restore_pct + 5 > 100 ? 100 : g->delestage_restore_pct + 5;
            g->delestage_anticip_pct = a;
        }
    }
    if ((item = cJSON_GetObjectItem(doc, "enablePresenceGraph")))
        g->enable_presence_graph = cJSON_IsTrue(item) || (cJSON_IsNumber(item) && item->valueint);

    static const char *const tarif_keys[10] = {
        "tarifIdx1","tarifIdx2","tarifIdx3","tarifIdx4","tarifIdx5",
        "tarifIdx6","tarifIdx7","tarifIdx8","tarifIdx9","tarifIdx10"
    };
    for (int i = 0; i < 10; i++) {
        json_take_str(doc, tarif_keys[i], g->tarif_idx[i], sizeof(g->tarif_idx[i]));
    }
    json_take_str(doc, "tarifAbo",     g->tarif_abo,      sizeof(g->tarif_abo));
    json_take_str(doc, "tarifCSPE",    g->tarif_cspe,     sizeof(g->tarif_cspe));
    json_take_str(doc, "tarifCTA",     g->tarif_cta,      sizeof(g->tarif_cta));
    json_take_str(doc, "tarifIdxProd", g->tarif_idx_prod, sizeof(g->tarif_idx_prod));
    json_take_str(doc, "tarifAboProd", g->tarif_abo_prod, sizeof(g->tarif_abo_prod));

    if (json_take_num(doc, "coeffGaz",   &num)) g->coeff_gaz   = (float)num;
    if (json_take_num(doc, "coeffWater", &num)) g->coeff_water = (float)num;
    json_take_str(doc, "unitGaz",    g->unit_gaz,    sizeof(g->unit_gaz));
    json_take_str(doc, "unitWater",  g->unit_water,  sizeof(g->unit_water));
    json_take_str(doc, "tarifGaz",   g->tarif_gaz,   sizeof(g->tarif_gaz));
    json_take_str(doc, "tarifWater", g->tarif_water, sizeof(g->tarif_water));
    if ((item = cJSON_GetObjectItem(doc, "enableDebug")))
        s->enable_debug = cJSON_IsTrue(item) || (cJSON_IsNumber(item) && item->valueint);
    if ((item = cJSON_GetObjectItem(doc, "enableHWFlow")))
        s->enable_hw_flow = cJSON_IsTrue(item) || (cJSON_IsNumber(item) && item->valueint);
    if ((item = cJSON_GetObjectItem(doc, "developerMode")))
        g->developer_mode = cJSON_IsTrue(item) || (cJSON_IsNumber(item) && item->valueint);

    cJSON_Delete(doc);

    // Sauvegarder
    esp_err_t err = cfg_save_general();

    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Save failed");
    }
    return ESP_OK;
}

// ============================================================================
// GET /api/config/mqtt - Retourne la config MQTT
// ============================================================================

static esp_err_t api_config_mqtt_get_handler(httpd_req_t *req)
{
    const cfg_general_t  *g = cfg_get_general();
    const cfg_settings_t *s = cfg_get_settings();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enableMqtt",    s->enable_mqtt);
    cJSON_AddStringToObject(root, "servMQTT",     g->serv_mqtt);
    cJSON_AddStringToObject(root, "portMQTT",     g->port_mqtt);
    cJSON_AddStringToObject(root, "clientIDMQTT", g->client_id_mqtt);
    cJSON_AddStringToObject(root, "userMQTT",     g->user_mqtt);
    // Le mot de passe du broker n'est JAMAIS renvoye : cet endpoint le livrait
    // en clair, et le frontend ne l'a jamais lu. On expose seulement s'il est
    // renseigne, ce qui suffit a afficher un etat dans l'interface.
    cJSON_AddBoolToObject(root, "hasPassMQTT",    g->pass_mqtt[0] != '\0');
    cJSON_AddStringToObject(root, "headerMQTT",   g->header_mqtt);
    cJSON_AddBoolToObject(root, "HAMQTT",         g->ha_mqtt);
    cJSON_AddBoolToObject(root, "TBMQTT",         g->tb_mqtt);
    cJSON_AddBoolToObject(root, "customMQTT",     g->custom_mqtt);
    cJSON_AddStringToObject(root, "customMQTTJson", g->custom_mqtt_json);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ============================================================================
// POST /api/config/mqtt - Sauvegarde la config MQTT
// ============================================================================

static esp_err_t api_config_mqtt_post_handler(httpd_req_t *req)
{
    // 4 Ko : le gabarit personnalise fait jusqu'a 511 caracteres, et son
    // echappement JSON peut le doubler ; l'ancien tampon de 1 Ko le tronquait.
    cJSON *doc = read_json_body(req, 4096);
    if (!doc) return ESP_FAIL;

    cfg_general_t  *g = cfg_get_general_mut();
    cfg_settings_t *s = cfg_get_settings_mut();
    cJSON *item;

    if ((item = cJSON_GetObjectItem(doc, "enableMqtt")))
        s->enable_mqtt = cJSON_IsTrue(item) || (cJSON_IsNumber(item) && item->valueint);
    if ((item = cJSON_GetObjectItem(doc, "servMQTT")) && cJSON_IsString(item))
        strlcpy(g->serv_mqtt, item->valuestring, sizeof(g->serv_mqtt));
    if ((item = cJSON_GetObjectItem(doc, "portMQTT"))) {
        // Accepter le port en nombre comme en chaine : un cJSON_IsString
        // strict ignorait silencieusement un port envoye en nombre.
        if (cJSON_IsString(item))
            strlcpy(g->port_mqtt, item->valuestring, sizeof(g->port_mqtt));
        else if (cJSON_IsNumber(item))
            snprintf(g->port_mqtt, sizeof(g->port_mqtt), "%d", item->valueint);
    }
    if ((item = cJSON_GetObjectItem(doc, "clientIDMQTT")) && cJSON_IsString(item))
        strlcpy(g->client_id_mqtt, item->valuestring, sizeof(g->client_id_mqtt));
    if ((item = cJSON_GetObjectItem(doc, "userMQTT")) && cJSON_IsString(item))
        strlcpy(g->user_mqtt, item->valuestring, sizeof(g->user_mqtt));
    // Une chaine vide signifie "conserver l'existant" : le GET ne renvoyant
    // plus le secret, le formulaire ne peut pas le reafficher pour le renvoyer
    // tel quel, et sans cette regle chaque enregistrement l'effacerait.
    // Pour l'effacer volontairement, envoyer clearPassMQTT.
    if ((item = cJSON_GetObjectItem(doc, "passMQTT")) && cJSON_IsString(item)
        && item->valuestring[0] != '\0')
        strlcpy(g->pass_mqtt, item->valuestring, sizeof(g->pass_mqtt));
    if ((item = cJSON_GetObjectItem(doc, "clearPassMQTT")) &&
        (cJSON_IsTrue(item) || (cJSON_IsNumber(item) && item->valueint)))
        g->pass_mqtt[0] = '\0';
    if ((item = cJSON_GetObjectItem(doc, "headerMQTT")) && cJSON_IsString(item))
        strlcpy(g->header_mqtt, item->valuestring, sizeof(g->header_mqtt));
    if ((item = cJSON_GetObjectItem(doc, "HAMQTT")))
        g->ha_mqtt = cJSON_IsTrue(item) || (cJSON_IsNumber(item) && item->valueint);
    if ((item = cJSON_GetObjectItem(doc, "TBMQTT")))
        g->tb_mqtt = cJSON_IsTrue(item) || (cJSON_IsNumber(item) && item->valueint);
    if ((item = cJSON_GetObjectItem(doc, "customMQTT")))
        g->custom_mqtt = cJSON_IsTrue(item) || (cJSON_IsNumber(item) && item->valueint);
    if ((item = cJSON_GetObjectItem(doc, "customMQTTJson")) && cJSON_IsString(item))
        strlcpy(g->custom_mqtt_json, item->valuestring, sizeof(g->custom_mqtt_json));

    cJSON_Delete(doc);
    esp_err_t err = cfg_save_general();
    if (err == ESP_OK) mqtt_manager_request_reload();   // appliquee sans redemarrer

    return send_save_result(req, err);
}

// ============================================================================
// GET /api/config/webpush
// ============================================================================

static esp_err_t api_config_webpush_get_handler(httpd_req_t *req)
{
    const cfg_general_t  *g = cfg_get_general();
    const cfg_settings_t *s = cfg_get_settings();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enableWebPush", s->enable_webpush);
    cJSON_AddStringToObject(root, "servWebPush",  g->serv_webpush);
    cJSON_AddBoolToObject(root, "webPushAuth",    g->webpush_auth);
    cJSON_AddStringToObject(root, "userWebPush",  g->user_webpush);
    cJSON_AddBoolToObject(root, "hasPassWebPush", g->pass_webpush[0] != '\0');

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ============================================================================
// POST /api/config/webpush
// ============================================================================

static esp_err_t api_config_webpush_post_handler(httpd_req_t *req)
{
    cJSON *doc = read_json_body(req, 1024);
    if (!doc) return ESP_FAIL;

    cfg_general_t  *g = cfg_get_general_mut();
    cfg_settings_t *s = cfg_get_settings_mut();
    cJSON *item;

    // Etat final calcule a part, valide (comme handleSaveConfigWebPush de la
    // LiXee-Box), puis seulement enregistre
    bool enable = s->enable_webpush, auth = g->webpush_auth;
    char url[sizeof(g->serv_webpush)], user[sizeof(g->user_webpush)], pass[sizeof(g->pass_webpush)];
    strlcpy(url, g->serv_webpush, sizeof(url));
    strlcpy(user, g->user_webpush, sizeof(user));
    strlcpy(pass, g->pass_webpush, sizeof(pass));

    if ((item = cJSON_GetObjectItem(doc, "enableWebPush")))
        enable = cJSON_IsTrue(item) || (cJSON_IsNumber(item) && item->valueint);
    if ((item = cJSON_GetObjectItem(doc, "servWebPush")) && cJSON_IsString(item))
        strlcpy(url, item->valuestring, sizeof(url));
    if ((item = cJSON_GetObjectItem(doc, "webPushAuth")))
        auth = cJSON_IsTrue(item) || (cJSON_IsNumber(item) && item->valueint);
    if ((item = cJSON_GetObjectItem(doc, "userWebPush")) && cJSON_IsString(item))
        strlcpy(user, item->valuestring, sizeof(user));
    if ((item = cJSON_GetObjectItem(doc, "passWebPush")) && cJSON_IsString(item)
        && item->valuestring[0] != '\0')
        strlcpy(pass, item->valuestring, sizeof(pass));
    if ((item = cJSON_GetObjectItem(doc, "clearPassWebPush")) &&
        (cJSON_IsTrue(item) || (cJSON_IsNumber(item) && item->valueint)))
        pass[0] = '\0';
    cJSON_Delete(doc);

    const char *err_msg = NULL;
    if (enable && !url[0])                   err_msg = "Adresse du serveur WebPush requise";
    else if (auth && !user[0])               err_msg = "Utilisateur requis pour l'authentification WebPush";
    else if (auth && strlen(pass) < 4)       err_msg = "Mot de passe WebPush : 4 caracteres au moins";
    if (err_msg) {
        memset(pass, 0, sizeof(pass));
        return send_bad_request(req, err_msg);
    }

    s->enable_webpush = enable;
    g->webpush_auth = auth;
    strlcpy(g->serv_webpush, url, sizeof(g->serv_webpush));
    strlcpy(g->user_webpush, user, sizeof(g->user_webpush));
    strlcpy(g->pass_webpush, pass, sizeof(g->pass_webpush));
    memset(pass, 0, sizeof(pass));
    esp_err_t err = cfg_save_general();

    return send_save_result(req, err);
}

// ============================================================================
// POST /saveConfigHorloge - Sauvegarde config horloge (NTP)
// ============================================================================

static esp_err_t api_save_config_horloge_handler(httpd_req_t *req)
{
    cJSON *doc = read_json_body(req, 1024);
    if (!doc) return ESP_FAIL;

    cfg_general_t *g = cfg_get_general_mut();
    cJSON *item;

    if ((item = cJSON_GetObjectItem(doc, "ntpserver")) && cJSON_IsString(item))
        strlcpy(g->ntp_server, item->valuestring, sizeof(g->ntp_server));
    if ((item = cJSON_GetObjectItem(doc, "timeoffset"))) {
        if (cJSON_IsNumber(item))      g->time_offset = item->valueint;
        else if (cJSON_IsString(item)) g->time_offset = atoi(item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(doc, "timezone"))) {
        if (cJSON_IsString(item))
            strlcpy(g->timezone, item->valuestring, sizeof(g->timezone));
        else if (cJSON_IsNumber(item))
            snprintf(g->timezone, sizeof(g->timezone), "%d", item->valueint);
    }

    cJSON_Delete(doc);
    esp_err_t err = cfg_save_general();
    if (err == ESP_OK) ntp_sync_apply_config();   // fuseau et serveur appliques sans redemarrer

    return send_save_result(req, err);
}

// ============================================================================
// POST /api/system/time {"epoch": N} - reglage manuel de l'heure
// GET  /getFormattedDate                - heure de la box "JJ/MM/AAAA HH:MM"
// ============================================================================

static esp_err_t api_system_time_handler(httpd_req_t *req)
{
    cJSON *doc = read_json_body(req, 256);
    if (!doc) return ESP_FAIL;
    double v = 0;
    bool ok = json_take_num(doc, "epoch", &v);
    cJSON_Delete(doc);
    if (!ok || ntp_sync_set_time((time_t)v) != ESP_OK) return send_bad_request(req, "Date invalide");
    cfg_get_general_mut()->epoch_time = (time_t)v;   // repli au prochain demarrage sans NTP
    cfg_save_general();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Heure reglee\"}");
}

static esp_err_t api_formatted_date_handler(httpd_req_t *req)
{
    char buf[48] = "";
    time_t now = time(NULL);
    if (now > 1577836800) {
        struct tm lt;
        localtime_r(&now, &lt);
        snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d", lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900,
                 lt.tm_hour, lt.tm_min);
    }
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, buf);
}

// ============================================================================
// POST /saveConfigHTTP - Sauvegarde config HTTP auth
// ============================================================================

static esp_err_t api_save_config_http_handler(httpd_req_t *req)
{
    cJSON *doc = read_json_body(req, 1024);
    if (!doc) return ESP_FAIL;

    cfg_general_t  *g = cfg_get_general_mut();
    cfg_settings_t *s = cfg_get_settings_mut();
    cJSON *item;

    // Calculer l'etat final dans des variables locales, le valider, PUIS le
    // commettre. On ne doit jamais activer l'authentification avec des
    // identifiants vides : auth_check() laisserait alors l'acces ouvert.
    bool enable = s->enable_secure_http;
    char new_user[sizeof(g->user_http)];
    char new_pass[sizeof(g->pass_http)];
    strlcpy(new_user, g->user_http, sizeof(new_user));
    strlcpy(new_pass, g->pass_http, sizeof(new_pass));

    if ((item = cJSON_GetObjectItem(doc, "enableSecureHttp")))
        enable = cJSON_IsTrue(item) || (cJSON_IsNumber(item) && item->valueint);
    if ((item = cJSON_GetObjectItem(doc, "userHTTP")) && cJSON_IsString(item))
        strlcpy(new_user, item->valuestring, sizeof(new_user));
    // Chaine vide = conserver le mot de passe actuel : le GET ne le renvoie
    // jamais, le formulaire ne peut donc pas le reafficher. clearPassHTTP
    // pour l'effacer sciemment.
    bool pass_short = false;
    if ((item = cJSON_GetObjectItem(doc, "passHTTP")) && cJSON_IsString(item)
        && item->valuestring[0] != '\0') {
        pass_short = strlen(item->valuestring) < 4;   // 4 caracteres au moins (LiXee-Box)
        strlcpy(new_pass, item->valuestring, sizeof(new_pass));
    }
    if ((item = cJSON_GetObjectItem(doc, "clearPassHTTP")) &&
        (cJSON_IsTrue(item) || (cJSON_IsNumber(item) && item->valueint)))
        new_pass[0] = '\0';

    cJSON_Delete(doc);
    httpd_resp_set_type(req, "application/json");

    // Tunnel distant actif : la securite HTTP reste obligatoire (case grisee
    // dans la page, donc jamais envoyee : son absence ne vaut pas "desactiver")
    if (tunnel_security_locked()) enable = true;

    if (pass_short) {
        memset(new_pass, 0, sizeof(new_pass));
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":"
                                "\"Le mot de passe doit faire au moins 4 caracteres\"}");
        return ESP_OK;
    }
    if (enable && (new_user[0] == '\0' || new_pass[0] == '\0')) {
        memset(new_pass, 0, sizeof(new_pass));
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":"
                                "\"Identifiant et mot de passe requis pour activer l'authentification\"}");
        return ESP_OK;
    }

    s->enable_secure_http = enable;
    strlcpy(g->user_http, new_user, sizeof(g->user_http));
    strlcpy(g->pass_http, new_pass, sizeof(g->pass_http));
    memset(new_pass, 0, sizeof(new_pass));

    esp_err_t err = cfg_save_general();
    if (err != ESP_OK) {
        // Repondre en erreur HTTP : avec un 200, le frontend affichait
        // "enregistree" alors que rien n'etait persiste.
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":"
                                "\"Echec d'ecriture de la configuration\"}");
        return ESP_OK;
    }
    tunnel_on_security_changed();
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// ============================================================================
// GET /api/config/http - Etat de l'authentification, SANS le mot de passe
//
// Le formulaire de securite appelait deja cet endpoint pour pre-remplir la
// case et l'utilisateur, mais il n'existait pas : la requete tombait sur le
// handler statique et recevait un 404. Le mot de passe n'est jamais renvoye ;
// hasPassword suffit a afficher un etat et a appliquer "vide = conserver".
// ============================================================================

static esp_err_t api_config_http_get_handler(httpd_req_t *req)
{
    const cfg_settings_t *s = cfg_get_settings();
    const cfg_general_t  *g = cfg_get_general();

    cJSON *root = cJSON_CreateObject();
    if (!root) { httpd_resp_send_500(req); return ESP_FAIL; }
    cJSON_AddBoolToObject(root,   "enabled",     s->enable_secure_http);
    cJSON_AddStringToObject(root, "user",        g->user_http);
    cJSON_AddBoolToObject(root,   "hasPassword", g->pass_http[0] != '\0');
    cJSON_AddBoolToObject(root,   "lockedByTunnel", tunnel_security_locked());

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ============================================================================
// GET /api/config/notifications - Retourne la config notifications
// ============================================================================

static esp_err_t api_config_notifications_get_handler(httpd_req_t *req)
{
    const cfg_notifications_t *n = cfg_get_notifications();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "PowerOutage",     n->power_outage);
    cJSON_AddBoolToObject(root, "PriceChange",     n->price_change);
    cJSON_AddBoolToObject(root, "SubscribedPower", n->subscribed_power);
    cJSON_AddBoolToObject(root, "ProdSupConso",    n->prod_sup_conso);
    cJSON_AddBoolToObject(root, "ProdZero",        n->prod_zero);
    cJSON_AddBoolToObject(root, "ColorTomorrow",   n->color_tomorrow);
    cJSON_AddBoolToObject(root, "OverBudget",      n->over_budget);
    cJSON_AddNumberToObject(root, "OverBudgetThreshold",  n->over_budget_threshold);
    cJSON_AddBoolToObject(root, "OverVoltage",     n->over_voltage);
    cJSON_AddNumberToObject(root, "OverVoltageThreshold", n->over_voltage_threshold);
    cJSON_AddBoolToObject(root, "UnderVoltage",    n->under_voltage);
    cJSON_AddNumberToObject(root, "UnderVoltageThreshold", n->under_voltage_threshold);
    cJSON_AddBoolToObject(root, "PEJP",            n->pejp);
    cJSON_AddBoolToObject(root, "RedColor",        n->red_color);

    // Alertes avancees (lot 4, page configNotifications) : memes cles que
    // configGeneral.json de la LiXee-Box
    cJSON_AddBoolToObject(root,   "OverPower",               n->over_power);
    cJSON_AddNumberToObject(root, "OverPowerThreshold",      n->over_power_threshold);
    cJSON_AddNumberToObject(root, "OverPowerDuration",       n->over_power_duration);
    cJSON_AddNumberToObject(root, "OverPowerCooldown",       n->over_power_cooldown);
    cJSON_AddBoolToObject(root,   "Freeze",                  n->freeze);
    cJSON_AddNumberToObject(root, "FreezeThreshold",         n->freeze_threshold);
    cJSON_AddStringToObject(root, "FreezeSensorIEEE",        n->freeze_sensor_ieee);
    cJSON_AddNumberToObject(root, "FreezeCooldown",          n->freeze_cooldown);
    cJSON_AddBoolToObject(root,   "DailyAnomaly",            n->daily_anomaly);
    cJSON_AddNumberToObject(root, "DailyAnomalyPercent",     n->daily_anomaly_percent);
    cJSON_AddBoolToObject(root,   "WaterLeak",               n->water_leak);
    cJSON_AddNumberToObject(root, "WaterLeakThreshold",      n->water_leak_threshold);
    cJSON_AddBoolToObject(root,   "NightWaterLeak",          n->night_water_leak);
    cJSON_AddNumberToObject(root, "NightWaterLeakThreshold", n->night_water_leak_threshold);
    cJSON_AddBoolToObject(root,   "DailyMetrics",            n->daily_metrics);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ============================================================================
// POST /api/config/notifications - Sauvegarde la config notifications
// Seules les cles presentes sont modifiees (la page Config Energie n'envoie
// qu'une partie des reglages). Booleens acceptes en true/false, 0/1 ou "on".
// ============================================================================

static esp_err_t api_config_notifications_post_handler(httpd_req_t *req)
{
    cJSON *doc = read_json_body(req, 2048);
    if (!doc) return ESP_FAIL;

    // Capteur de gel : adresse IEEE hexadecimale (16 caracteres) ou vide
    char freeze_ieee[sizeof(((cfg_notifications_t *)0)->freeze_sensor_ieee)] = "";
    const cJSON *fz = cJSON_GetObjectItem(doc, "FreezeSensorIEEE");
    if (fz) {
        bool ok = cJSON_IsString(fz) && strlen(fz->valuestring) <= 16;
        for (const char *p = ok ? fz->valuestring : ""; ok && *p; p++) {
            ok = (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F');
        }
        if (!ok) {
            cJSON_Delete(doc);
            return send_bad_request(req, "Adresse IEEE du capteur de gel invalide");
        }
        strlcpy(freeze_ieee, fz->valuestring, sizeof(freeze_ieee));
    }

    cfg_notifications_t *n = cfg_get_notifications_mut();
    // Booleen : true/false, 0/1, "1"/"on"/"true" (formulaires et fichiers LiXee-Box)
    auto take_flag = [&](const char *key, bool *dst) {
        const cJSON *it = cJSON_GetObjectItem(doc, key);
        if (!it) return;
        if (cJSON_IsBool(it))        *dst = cJSON_IsTrue(it);
        else if (cJSON_IsNumber(it)) *dst = it->valuedouble != 0;
        else if (cJSON_IsString(it)) *dst = strcmp(it->valuestring, "1") == 0 ||
                                            strcmp(it->valuestring, "on") == 0 ||
                                            strcmp(it->valuestring, "true") == 0;
    };
    // Entier borne (nombre ou chaine) ; hors bornes : valeur conservee
    auto take_int = [&](const char *key, int *dst, int lo, int hi) {
        double v;
        if (json_take_num(doc, key, &v) && v >= lo && v <= hi) *dst = (int)v;
    };

    take_flag("PowerOutage",     &n->power_outage);
    take_flag("PriceChange",     &n->price_change);
    take_flag("SubscribedPower", &n->subscribed_power);
    take_flag("ProdSupConso",    &n->prod_sup_conso);
    take_flag("ProdZero",        &n->prod_zero);
    take_flag("ColorTomorrow",   &n->color_tomorrow);
    take_flag("OverBudget",      &n->over_budget);
    take_int("OverBudgetThreshold", &n->over_budget_threshold, 0, 100000);
    take_flag("OverVoltage",     &n->over_voltage);
    take_int("OverVoltageThreshold", &n->over_voltage_threshold, 0, 1000);
    take_flag("UnderVoltage",    &n->under_voltage);
    take_int("UnderVoltageThreshold", &n->under_voltage_threshold, 0, 1000);
    take_flag("PEJP",            &n->pejp);
    take_flag("RedColor",        &n->red_color);

    // Alertes avancees
    take_flag("OverPower",       &n->over_power);
    take_int("OverPowerThreshold", &n->over_power_threshold, 1, 100000);
    take_int("OverPowerDuration",  &n->over_power_duration, 1, 86400);
    take_int("OverPowerCooldown",  &n->over_power_cooldown, 1, 10080);
    take_flag("Freeze",          &n->freeze);
    take_int("FreezeThreshold",  &n->freeze_threshold, -5000, 5000);
    if (fz) strlcpy(n->freeze_sensor_ieee, freeze_ieee, sizeof(n->freeze_sensor_ieee));
    take_int("FreezeCooldown",   &n->freeze_cooldown, 1, 10080);
    take_flag("DailyAnomaly",    &n->daily_anomaly);
    take_int("DailyAnomalyPercent", &n->daily_anomaly_percent, 1, 1000);
    take_flag("WaterLeak",       &n->water_leak);
    take_int("WaterLeakThreshold", &n->water_leak_threshold, 1, 100000);
    take_flag("NightWaterLeak",  &n->night_water_leak);
    take_int("NightWaterLeakThreshold", &n->night_water_leak_threshold, 1, 100000);
    take_flag("DailyMetrics",    &n->daily_metrics);

    cJSON_Delete(doc);
    esp_err_t err = cfg_save_general();

    return send_save_result(req, err);
}

// ============================================================================
// GET /api/mqtt/status - Etat de la connexion MQTT (JSON, interface)
// GET /getMQTTStatus   - "1" connecte, "0" sinon (texte, comme la LiXee-Box)
// ============================================================================

float web_api_chip_temperature(void) { return read_chip_temperature(); }

static esp_err_t api_mqtt_status_text_handler(httpd_req_t *req)
{
    bool on = cfg_get_settings()->enable_mqtt && mqtt_manager_is_connected();
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, on ? "1" : "0");
}

static esp_err_t api_mqtt_status_handler(httpd_req_t *req)
{
    const cfg_settings_t *s = cfg_get_settings();
    bool connected = mqtt_manager_is_connected();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", s->enable_mqtt);
    cJSON_AddBoolToObject(root, "connected", connected);
    const char *status_str = !s->enable_mqtt ? "disabled" : (connected ? "connected" : "disconnected");
    cJSON_AddStringToObject(root, "status", status_str);
    const cfg_general_t *g = cfg_get_general();
    cJSON_AddStringToObject(root, "server", g->serv_mqtt);
    cJSON_AddStringToObject(root, "port",   g->port_mqtt);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ============================================================================
// POST /saveWifi - Sauvegarde config WiFi (appele par pageWifi)
// ============================================================================

static esp_err_t api_save_wifi_handler(httpd_req_t *req)
{
    // Alias historique de POST /api/config/wifi : voir apply_wifi_credentials().
    // L'IP fixe n'est pas implementee par le firmware ; elle n'est plus acceptee
    // pour etre enregistree puis ignoree.
    cJSON *json = read_json_body(req, 512);
    if (!json) return ESP_FAIL;
    return apply_wifi_credentials(req, json);
}

// ============================================================================
// Enregistrement de tous les handlers
// ============================================================================

// ============================================================================
// GET /api/config/horloge - Reglages d'horloge et heure reelle de l'appareil
//
// Appele par la page Horloge mais absent : la requete tombait sur le handler
// statique (404) et la page affichait l'heure du navigateur.
// ============================================================================

static esp_err_t api_config_horloge_get_handler(httpd_req_t *req)
{
    const cfg_general_t *g = cfg_get_general();

    cJSON *root = cJSON_CreateObject();
    if (!root) { httpd_resp_send_500(req); return ESP_FAIL; }
    cJSON_AddStringToObject(root, "ntpserver",  g->ntp_server);
    cJSON_AddNumberToObject(root, "timeoffset", g->time_offset);
    cJSON_AddStringToObject(root, "timezone",   g->timezone);
    cJSON_AddBoolToObject(root,   "synced",     ntp_sync_is_synced());
    // Heure REELLE de l'appareil, et non epoch_time de la configuration, qui
    // n'est jamais remise a jour.
    cJSON_AddNumberToObject(root, "epoch", (double)ntp_sync_get_epoch());
    char dt[32];
    if (ntp_sync_get_formatted_datetime(dt, sizeof(dt)) == ESP_OK) {
        cJSON_AddStringToObject(root, "datetime", dt);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ============================================================================
// GET /scanNetwork - Reseaux WiFi visibles (bouton "Scan" de la page WiFi)
// ============================================================================

static esp_err_t api_scan_network_handler(httpd_req_t *req)
{
    // Scan bloquant (~2 s) : acceptable pour une action manuelle, et possible
    // en mode STA sans couper la connexion en cours.
    wifi_scan_config_t sc = {};
    esp_err_t err = esp_wifi_scan_start(&sc, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Scan WiFi impossible: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan failed");
        return ESP_FAIL;
    }

    uint16_t count = 20;
    wifi_ap_record_t *aps = (wifi_ap_record_t *)calloc(count, sizeof(wifi_ap_record_t));
    if (!aps) {
        esp_wifi_clear_ap_list();   // sinon la liste du scan fuit
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (esp_wifi_scan_get_ap_records(&count, aps) != ESP_OK) {
        esp_wifi_clear_ap_list();
        count = 0;
    }

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; arr && i < count; i++) {
        if (aps[i].ssid[0] == '\0') continue;   // reseaux masques
        cJSON *o = cJSON_CreateObject();
        if (!o) break;
        cJSON_AddStringToObject(o, "ssid", (const char *)aps[i].ssid);
        cJSON_AddNumberToObject(o, "rssi", aps[i].rssi);
        cJSON_AddNumberToObject(o, "channel", aps[i].primary);
        cJSON_AddBoolToObject(o, "secure", aps[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(arr, o);
    }
    free(aps);

    char *json = arr ? cJSON_PrintUnformatted(arr) : NULL;
    cJSON_Delete(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json ? json : "[]");
    free(json);
    return ESP_OK;
}

esp_err_t web_api_register_handlers(httpd_handle_t server)
{
    // --- Zigbee API ---
    httpd_uri_t uri;

    // --- Matter API ---
    uri = { .uri = "/api/matter/status", .method = HTTP_GET,
            .handler = api_matter_status_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/matter/open-window", .method = HTTP_POST,
            .handler = api_matter_open_window_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/matter/factory_reset", .method = HTTP_POST,
            .handler = api_matter_factory_reset_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/matter/config", .method = HTTP_POST,
            .handler = api_matter_config_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/matter/excluded", .method = HTTP_GET,
            .handler = api_matter_excluded_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/matter/expose", .method = HTTP_POST,
            .handler = api_matter_expose_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    // --- System API ---
    uri = { .uri = "/api/system/info", .method = HTTP_GET,
            .handler = api_system_info_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/system/reboot", .method = HTTP_POST,
            .handler = api_system_reboot_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    // --- WiFi Config API ---
    uri = { .uri = "/api/config/wifi", .method = HTTP_GET,
            .handler = api_config_wifi_get_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/config/wifi", .method = HTTP_POST,
            .handler = api_config_wifi_post_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    // --- Poll endpoint (LiXee-Assist health check) ---
    uri = { .uri = "/poll", .method = HTTP_GET,
            .handler = api_poll_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    // --- Energy API : web_energy.cpp (lot 2 du portage LiXee-Box) ---
    web_energy_register(server);
    web_rules_register(server);
    web_zigbee_register(server);
    web_maint_register(server);
    web_tunnel_register(server);
    web_lora_register(server);
    web_notif_register(server);
    web_thermostat_register(server);
    web_compat_register(server);

    // --- Config API ---
    uri = { .uri = "/api/config/general", .method = HTTP_GET,
            .handler = api_config_general_get_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/config/general", .method = HTTP_POST,
            .handler = api_config_general_post_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/config/mqtt", .method = HTTP_GET,
            .handler = api_config_mqtt_get_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/config/mqtt", .method = HTTP_POST,
            .handler = api_config_mqtt_post_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/config/webpush", .method = HTTP_GET,
            .handler = api_config_webpush_get_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/config/webpush", .method = HTTP_POST,
            .handler = api_config_webpush_post_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/config/notifications", .method = HTTP_GET,
            .handler = api_config_notifications_get_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/config/notifications", .method = HTTP_POST,
            .handler = api_config_notifications_post_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/system/time", .method = HTTP_POST,
            .handler = api_system_time_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/getFormattedDate", .method = HTTP_GET,
            .handler = api_formatted_date_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/saveConfigHorloge", .method = HTTP_POST,
            .handler = api_save_config_horloge_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/saveConfigHTTP", .method = HTTP_POST,
            .handler = api_save_config_http_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/config/http", .method = HTTP_GET,
            .handler = api_config_http_get_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/config/horloge", .method = HTTP_GET,
            .handler = api_config_horloge_get_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/scanNetwork", .method = HTTP_GET,
            .handler = api_scan_network_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/mqtt/status", .method = HTTP_GET,
            .handler = api_mqtt_status_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/getMQTTStatus", .method = HTTP_GET,
            .handler = api_mqtt_status_text_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/saveWifi", .method = HTTP_POST,
            .handler = api_save_wifi_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    // --- Device Data API ---
    // Note: /api/devices/data/<ieee> utilise un wildcard via le dernier segment
    // Pour esp_http_server qui ne supporte pas les wildcards natifs,
    // on enregistre un chemin avec suffixe '*'
    uri = { .uri = "/api/config/ethernet", .method = HTTP_GET,
            .handler = api_config_ethernet_get_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    uri = { .uri = "/api/config/ethernet", .method = HTTP_POST,
            .handler = api_config_ethernet_post_handler, .user_ctx = NULL };
    web_server_register_uri(server, &uri);

    // Outils developpeur : fichiers de config, base, historique, templates...
    web_files_register(server);

    ESP_LOGI(TAG, "API handlers registered (41 endpoints)");
    return ESP_OK;
}
