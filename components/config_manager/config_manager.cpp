// ============================================================================
// Config Manager - Implementation
//
// Port de loadConfigGeneral() / loadConfigWifi() / config_write() / config_read()
// depuis le code Arduino vers ESP-IDF.
//
// Utilise cJSON (inclus dans ESP-IDF) au lieu d'ArduinoJson.
// Stockage LittleFS via VFS (fopen/fclose standard C).
// ============================================================================

#include "config_manager.h"
#include "esp_log.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>   // strcasecmp
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>   // fsync, unlink
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Verrou des ecritures de configuration (recursif : cfg_save_general appelle
// write_file). Plusieurs taches enregistrent (HTTP, boucle principale, LoRa) :
// sans verrou, deux ecritures du meme fichier partageaient le meme .tmp et
// pouvaient laisser un fichier dechire. Cree par cfg_init(), avant les taches.
static SemaphoreHandle_t s_cfg_lock = NULL;
static volatile bool s_save_general_requested = false;

static void cfg_lock(void)   { if (s_cfg_lock) xSemaphoreTakeRecursive(s_cfg_lock, portMAX_DELAY); }
static void cfg_unlock(void) { if (s_cfg_lock) xSemaphoreGiveRecursive(s_cfg_lock); }

static const char *TAG = "CFG_MGR";

// ============================================================================
// Instances globales
// ============================================================================

static cfg_settings_t      g_settings;
static cfg_general_t       g_general;
static cfg_notifications_t g_notifications;
static cfg_zigbee_t        g_zigbee;

// ============================================================================
// Accesseurs
// ============================================================================

const cfg_settings_t*      cfg_get_settings(void)          { return &g_settings; }
cfg_settings_t*            cfg_get_settings_mut(void)      { return &g_settings; }
const cfg_general_t*       cfg_get_general(void)           { return &g_general; }
cfg_general_t*             cfg_get_general_mut(void)       { return &g_general; }
const cfg_notifications_t* cfg_get_notifications(void)     { return &g_notifications; }
cfg_notifications_t*       cfg_get_notifications_mut(void) { return &g_notifications; }
const cfg_zigbee_t*        cfg_get_zigbee(void)            { return &g_zigbee; }
cfg_zigbee_t*              cfg_get_zigbee_mut(void)        { return &g_zigbee; }

// ============================================================================
// Helpers JSON
// ============================================================================

/** Lit un fichier entier en memoire. Retourne NULL si erreur. L'appelant doit free(). */
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGW(TAG, "Cannot open %s: %s", path, strerror(errno));
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 64 * 1024) {
        ESP_LOGW(TAG, "File %s invalid size: %ld", path, size);
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc(size + 1);
    if (!buf) {
        ESP_LOGE(TAG, "malloc failed for %s (%ld bytes)", path, size);
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, size, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

/** Ecrit un buffer dans un fichier. */
static esp_err_t write_file_unlocked(const char *path, const char *data);

static esp_err_t write_file(const char *path, const char *data)
{
    cfg_lock();
    esp_err_t err = write_file_unlocked(path, data);
    cfg_unlock();
    return err;
}

static esp_err_t write_file_unlocked(const char *path, const char *data)
{
    // Ecriture atomique : fichier temporaire, puis rename.
    //
    // fopen(path, "w") tronquait la cible AVANT d'ecrire : une coupure
    // d'alimentation ou une partition pleine laissait un fichier vide ou
    // tronque, donc une perte definitive de donnees valides.
    // Le retour de fclose etait de plus ignore, alors que c'est a la fermeture
    // que LittleFS valide blocs et metadonnees : un echec d'ecriture etait
    // rapporte comme un succes, et l'appelant effacait son drapeau 'dirty'.
    //
    // rename() est atomique sur LittleFS (operation de metadonnees) : la cible
    // conserve son ancien contenu valide jusqu'au basculement. Un .tmp
    // residuel apres coupure est ignore par les parcours de repertoire, qui
    // exigent une terminaison exacte en ".json".
    char tmp[96];
    int plen = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (plen < 0 || plen >= (int)sizeof(tmp)) {
        ESP_LOGE(TAG, "Path too long: %s", path);
        return ESP_FAIL;
    }

    FILE *f = fopen(tmp, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s for write: %s", tmp, strerror(errno));
        return ESP_FAIL;
    }

    size_t len = strlen(data);
    bool ok = (fwrite(data, 1, len, f) == len);
    if (ok && fflush(f) != 0)        ok = false;
    if (ok && fsync(fileno(f)) != 0) ok = false;
    if (fclose(f) != 0)              ok = false;   // LittleFS valide ici

    if (!ok) {
        ESP_LOGE(TAG, "Write failed for %s: %s (ancien contenu conserve)",
                 path, strerror(errno));
        unlink(tmp);
        return ESP_FAIL;
    }

    if (rename(tmp, path) != 0) {
        ESP_LOGE(TAG, "Cannot commit %s: %s", path, strerror(errno));
        unlink(tmp);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/** Lit une string JSON, copie dans un buffer char[]. */
static void json_get_str(const cJSON *obj, const char *key, char *dst, size_t dst_size, const char *def)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        strlcpy(dst, item->valuestring, dst_size);
    } else {
        strlcpy(dst, def ? def : "", dst_size);
    }
}

/**
 * Valeur numerique d'un element, qu'il soit nombre, booleen ou chaine.
 *
 * config_write() de la LiXee-Box enregistre TOUT en chaine ("1", "4000",
 * "0,5") : sans ce repli, un configGeneral.json venu de la LiXee-Box se
 * chargeait avec les notifications desactivees et les seuils a 0. Virgule
 * decimale toleree. false si l'element est absent ou non numerique (chaine
 * vide comprise : la valeur par defaut s'applique, comme cfgInt() Arduino).
 */
static bool json_num(const cJSON *item, double *out)
{
    if (cJSON_IsNumber(item)) { *out = item->valuedouble; return true; }
    if (cJSON_IsBool(item))   { *out = cJSON_IsTrue(item) ? 1 : 0; return true; }
    if (!cJSON_IsString(item) || !item->valuestring) return false;
    char tmp[32];
    const char *s = item->valuestring;
    while (*s == ' ') s++;
    strlcpy(tmp, s, sizeof(tmp));
    for (char *p = tmp; *p; p++) if (*p == ',') *p = '.';
    if (strcasecmp(tmp, "true") == 0 || strcasecmp(tmp, "on") == 0)   { *out = 1; return true; }
    if (strcasecmp(tmp, "false") == 0 || strcasecmp(tmp, "off") == 0) { *out = 0; return true; }
    char *end = NULL;
    double v = strtod(tmp, &end);
    if (end == tmp) return false;
    *out = v;
    return true;
}

/** Lit un int JSON (nombre ou chaine). */
static int json_get_int(const cJSON *obj, const char *key, int def)
{
    double v;
    return json_num(cJSON_GetObjectItem(obj, key), &v) ? (int)v : def;
}

/** Lit un bool JSON (int 0/1, booleen, ou chaine "1"/"0" ecrite par la LiXee-Box). */
static bool json_get_bool(const cJSON *obj, const char *key, bool def)
{
    double v;
    return json_num(cJSON_GetObjectItem(obj, key), &v) ? (v != 0) : def;
}

/** Lit un float JSON (nombre ou chaine). */
static float json_get_float(const cJSON *obj, const char *key, float def)
{
    double v;
    return json_num(cJSON_GetObjectItem(obj, key), &v) ? (float)v : def;
}

/** Lit un long JSON (nombre ou chaine). */
static long json_get_long(const cJSON *obj, const char *key, long def)
{
    double v;
    return json_num(cJSON_GetObjectItem(obj, key), &v) ? (long)v : def;
}

/** Entier avec defaut si absent OU nul (semantique "x > 0 ? x : defaut" de la LiXee-Box). */
static int json_get_int_pos(const cJSON *obj, const char *key, int def)
{
    int v = json_get_int(obj, key, def);
    return v != 0 ? v : def;
}

// ----------------------------------------------------------------------------
// Ecriture : on repart du fichier existant et on remplace les cles connues.
//
// L'ancienne version reconstruisait un document vierge : toute cle non geree
// par ce firmware (reglages LiXee-Box d'autres lots non encore portes,
// delestage, tunnel...) etait effacee au premier enregistrement.
// ----------------------------------------------------------------------------

static void put_item(cJSON *doc, const char *key, cJSON *item)
{
    if (!item) return;
    if (cJSON_GetObjectItemCaseSensitive(doc, key)) {
        cJSON_ReplaceItemInObjectCaseSensitive(doc, key, item);
    } else {
        cJSON_AddItemToObject(doc, key, item);
    }
}
static void put_num(cJSON *doc, const char *key, double v)       { put_item(doc, key, cJSON_CreateNumber(v)); }
static void put_str(cJSON *doc, const char *key, const char *v)  { put_item(doc, key, cJSON_CreateString(v ? v : "")); }
static void put_flag(cJSON *doc, const char *key, bool v)        { put_item(doc, key, cJSON_CreateNumber(v ? 1 : 0)); }
static void put_bool(cJSON *doc, const char *key, bool v)        { put_item(doc, key, cJSON_CreateBool(v)); }

// ============================================================================
// cfg_init
// ============================================================================

esp_err_t cfg_init(void)
{
    if (!s_cfg_lock) s_cfg_lock = xSemaphoreCreateRecursiveMutex();
    memset(&g_settings, 0, sizeof(g_settings));
    memset(&g_general, 0, sizeof(g_general));
    memset(&g_notifications, 0, sizeof(g_notifications));
    memset(&g_zigbee, 0, sizeof(g_zigbee));

    // Valeurs par defaut
    g_settings.enable_wifi = true;
    g_settings.enable_dhcp = true;
    g_general.power_max_datas = 1000;
    g_general.coeff_gaz = 1.0f;
    g_general.coeff_water = 1.0f;
    g_general.coeff_production = 1.0f;
    g_general.time_offset = 1;
    g_general.enable_presence_graph = true;
    g_general.delestage_restore_min = 5;
    g_general.delestage_restore_pct = 80;
    strlcpy(g_general.ntp_server, "pool.ntp.org", sizeof(g_general.ntp_server));
    strlcpy(g_general.timezone, "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(g_general.timezone));
    strlcpy(g_general.unit_gaz, "m3", sizeof(g_general.unit_gaz));
    strlcpy(g_general.unit_water, "L", sizeof(g_general.unit_water));

    // Seuils par defaut des alertes avancees (cfgInt() de la LiXee-Box)
    g_notifications.over_power_threshold       = 6000;
    g_notifications.over_power_duration        = 60;
    g_notifications.over_power_cooldown        = 30;
    g_notifications.freeze_threshold           = 300;
    g_notifications.freeze_cooldown            = 60;
    g_notifications.daily_anomaly_percent      = 30;
    g_notifications.water_leak_threshold       = 5;
    g_notifications.night_water_leak_threshold = 1;

    ESP_LOGI(TAG, "Config manager initialized with defaults");
    return ESP_OK;
}

// ============================================================================
// cfg_load_general  —  equivalent de loadConfigGeneral() Arduino
// ============================================================================

esp_err_t cfg_load_general(void)
{
    char *json_str = read_file(CFG_PATH_GENERAL);
    if (!json_str) {
        ESP_LOGW(TAG, "No general config file, using defaults");
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *doc = cJSON_Parse(json_str);
    free(json_str);
    if (!doc) {
        ESP_LOGE(TAG, "Failed to parse %s", CFG_PATH_GENERAL);
        return ESP_ERR_INVALID_ARG;
    }

    // --- Settings (feature flags) ---
    g_settings.enable_hw_flow     = json_get_bool(doc, "enableHWFlow", false);
    g_settings.enable_debug       = json_get_bool(doc, "enableDebug", false);
    g_settings.enable_mqtt        = json_get_bool(doc, "enableMqtt", false);
    g_settings.enable_udp         = json_get_bool(doc, "enableUDP", false);
    g_settings.enable_marstek     = json_get_bool(doc, "enableMarstek", false);
    g_settings.enable_notif       = json_get_bool(doc, "enableNotif", false);
    g_settings.enable_secure_http = json_get_bool(doc, "enableSecureHttp", false);
    g_settings.enable_webpush     = json_get_bool(doc, "enableWebPush", false);
    g_settings.enable_history     = json_get_bool(doc, "enableHistory", false);

    // --- General ---
    g_general.first_start = json_get_int(doc, "firstStart", 0);

    // Devices principaux
    json_get_str(doc, "ZLinky",     g_general.zlinky,     sizeof(g_general.zlinky),     "");
    json_get_str(doc, "Production", g_general.production, sizeof(g_general.production), "");
    json_get_str(doc, "Gaz",        g_general.gaz,        sizeof(g_general.gaz),        "");
    json_get_str(doc, "Water",      g_general.water,      sizeof(g_general.water),      "");

    // Linky
    g_general.linky_mode     = json_get_int(doc, "LinkyMode", 0);
    g_general.house_surface  = json_get_int(doc, "shon", 0);
    g_general.power_max_datas = json_get_int(doc, "powerMaxDatas", 1000);

    // NTP / Temps
    json_get_str(doc, "ntpserver", g_general.ntp_server, sizeof(g_general.ntp_server), "pool.ntp.org");
    json_get_str(doc, "timezone",  g_general.timezone,   sizeof(g_general.timezone),   "CET-1CEST,M3.5.0,M10.5.0/3");
    g_general.time_offset = json_get_int(doc, "timeoffset", 1);
    g_general.epoch_time  = json_get_long(doc, "epoch", 0);

    // Tarifs (tarifIdx1..tarifIdx10)
    {
        const char *tarif_keys[] = {
            "tarifIdx1", "tarifIdx2", "tarifIdx3", "tarifIdx4", "tarifIdx5",
            "tarifIdx6", "tarifIdx7", "tarifIdx8", "tarifIdx9", "tarifIdx10"
        };
        for (int i = 0; i < 10; i++) {
            json_get_str(doc, tarif_keys[i], g_general.tarif_idx[i],
                         sizeof(g_general.tarif_idx[i]), "0");
        }
    }
    json_get_str(doc, "tarifAbo",     g_general.tarif_abo,      sizeof(g_general.tarif_abo),      "0");
    json_get_str(doc, "tarifCSPE",    g_general.tarif_cspe,     sizeof(g_general.tarif_cspe),     "0");
    json_get_str(doc, "tarifCTA",     g_general.tarif_cta,      sizeof(g_general.tarif_cta),      "0");
    json_get_str(doc, "tarifAboProd", g_general.tarif_abo_prod, sizeof(g_general.tarif_abo_prod), "0");
    json_get_str(doc, "tarifIdxProd", g_general.tarif_idx_prod, sizeof(g_general.tarif_idx_prod), "0");

    // SMTP
    json_get_str(doc, "servSMTP", g_general.serv_smtp, sizeof(g_general.serv_smtp), "");
    json_get_str(doc, "portSMTP", g_general.port_smtp, sizeof(g_general.port_smtp), "");
    json_get_str(doc, "userSMTP", g_general.user_smtp, sizeof(g_general.user_smtp), "");
    json_get_str(doc, "passSMTP", g_general.pass_smtp, sizeof(g_general.pass_smtp), "");
    json_get_str(doc, "toSMTP",   g_general.to_smtp,   sizeof(g_general.to_smtp),   "");
    json_get_str(doc, "fromSMTP", g_general.from_smtp, sizeof(g_general.from_smtp), "");
    g_general.security_smtp = json_get_int(doc, "securitySMTP", 0);
    g_general.min_type_smtp = json_get_int(doc, "minTypeSMTP", 0);

    // MQTT
    json_get_str(doc, "servMQTT",     g_general.serv_mqtt,      sizeof(g_general.serv_mqtt),      "");
    json_get_str(doc, "portMQTT",     g_general.port_mqtt,      sizeof(g_general.port_mqtt),      "");
    json_get_str(doc, "clientIDMQTT", g_general.client_id_mqtt, sizeof(g_general.client_id_mqtt), "");
    json_get_str(doc, "userMQTT",     g_general.user_mqtt,      sizeof(g_general.user_mqtt),      "");
    json_get_str(doc, "passMQTT",     g_general.pass_mqtt,      sizeof(g_general.pass_mqtt),      "");
    json_get_str(doc, "headerMQTT",   g_general.header_mqtt,    sizeof(g_general.header_mqtt),    "");
    g_general.ha_mqtt     = json_get_bool(doc, "HAMQTT", false);
    g_general.tb_mqtt     = json_get_bool(doc, "TBMQTT", false);
    g_general.custom_mqtt = json_get_bool(doc, "customMQTT", false);
    json_get_str(doc, "customMQTTJson", g_general.custom_mqtt_json, sizeof(g_general.custom_mqtt_json), "");

    // HTTP auth
    json_get_str(doc, "userHTTP", g_general.user_http, sizeof(g_general.user_http), "");
    json_get_str(doc, "passHTTP", g_general.pass_http, sizeof(g_general.pass_http), "");

    // WebPush
    json_get_str(doc, "servWebPush", g_general.serv_webpush, sizeof(g_general.serv_webpush), "");
    json_get_str(doc, "userWebPush", g_general.user_webpush, sizeof(g_general.user_webpush), "");
    json_get_str(doc, "passWebPush", g_general.pass_webpush, sizeof(g_general.pass_webpush), "");
    g_general.webpush_auth = json_get_bool(doc, "webPushAuth", false);

    // UDP
    json_get_str(doc, "servUDP", g_general.serv_udp, sizeof(g_general.serv_udp), "");
    json_get_str(doc, "portUDP", g_general.port_udp, sizeof(g_general.port_udp), "");
    json_get_str(doc, "customUDPJson", g_general.custom_udp_json, sizeof(g_general.custom_udp_json), "");

    // Marstek
    g_general.connected_marstek = json_get_bool(doc, "connectedMarstek", false);

    // Gaz / Water
    g_general.coeff_gaz   = json_get_float(doc, "coeffGaz", 1.0f);
    g_general.coeff_water = json_get_float(doc, "coeffWater", 1.0f);
    json_get_str(doc, "unitGaz",    g_general.unit_gaz,    sizeof(g_general.unit_gaz),    "m3");
    json_get_str(doc, "unitWater",  g_general.unit_water,  sizeof(g_general.unit_water),  "L");
    json_get_str(doc, "tarifGaz",   g_general.tarif_gaz,   sizeof(g_general.tarif_gaz),   "0");
    json_get_str(doc, "tarifWater", g_general.tarif_water,  sizeof(g_general.tarif_water), "0");

    // Developer
    g_general.developer_mode = json_get_bool(doc, "developerMode", false);

    // --- Notifications ---
    g_notifications.power_outage          = json_get_bool(doc, "PowerOutage", false);
    g_notifications.price_change          = json_get_bool(doc, "PriceChange", false);
    g_notifications.subscribed_power      = json_get_bool(doc, "SubscribedPower", false);
    g_notifications.prod_sup_conso        = json_get_bool(doc, "ProdSupConso", false);
    g_notifications.prod_zero             = json_get_bool(doc, "ProdZero", false);
    g_notifications.color_tomorrow        = json_get_bool(doc, "ColorTomorrow", false);
    g_notifications.over_budget           = json_get_bool(doc, "OverBudget", false);
    g_notifications.over_budget_threshold = json_get_int(doc, "OverBudgetThreshold", 0);
    g_notifications.over_voltage          = json_get_bool(doc, "OverVoltage", false);
    g_notifications.over_voltage_threshold = json_get_int(doc, "OverVoltageThreshold", 0);
    g_notifications.under_voltage         = json_get_bool(doc, "UnderVoltage", false);
    g_notifications.under_voltage_threshold = json_get_int(doc, "UnderVoltageThreshold", 0);
    g_notifications.pejp                  = json_get_bool(doc, "PEJP", false);
    g_notifications.red_color             = json_get_bool(doc, "RedColor", false);

    // Alertes avancees : cles et defauts de loadConfigGeneral() Arduino
    g_notifications.over_power            = json_get_bool(doc, "OverPower", false);
    g_notifications.over_power_threshold  = json_get_int_pos(doc, "OverPowerThreshold", 6000);
    g_notifications.over_power_duration   = json_get_int_pos(doc, "OverPowerDuration", 60);
    g_notifications.over_power_cooldown   = json_get_int_pos(doc, "OverPowerCooldown", 30);
    g_notifications.freeze                = json_get_bool(doc, "Freeze", false);
    g_notifications.freeze_threshold      = json_get_int_pos(doc, "FreezeThreshold", 300);
    json_get_str(doc, "FreezeSensorIEEE", g_notifications.freeze_sensor_ieee,
                 sizeof(g_notifications.freeze_sensor_ieee), "");
    g_notifications.freeze_cooldown       = json_get_int_pos(doc, "FreezeCooldown", 60);
    g_notifications.daily_anomaly         = json_get_bool(doc, "DailyAnomaly", false);
    g_notifications.daily_anomaly_percent = json_get_int_pos(doc, "DailyAnomalyPercent", 30);
    g_notifications.water_leak            = json_get_bool(doc, "WaterLeak", false);
    g_notifications.water_leak_threshold  = json_get_int_pos(doc, "WaterLeakThreshold", 5);
    g_notifications.night_water_leak      = json_get_bool(doc, "NightWaterLeak", false);
    g_notifications.night_water_leak_threshold = json_get_int_pos(doc, "NightWaterLeakThreshold", 1);
    g_notifications.daily_metrics         = json_get_bool(doc, "DailyMetrics", false);

    // --- Sous-compteurs ---
    g_general.sub_meter_count = 0;
    const cJSON *sub_array = cJSON_GetObjectItem(doc, "subMeters");
    if (cJSON_IsArray(sub_array)) {
        int idx = 0;
        const cJSON *sm = NULL;
        cJSON_ArrayForEach(sm, sub_array) {
            if (idx >= CFG_MAX_SUBMETERS) break;
            json_get_str(sm, "IEEE",  g_general.sub_meters[idx].IEEE,  sizeof(g_general.sub_meters[idx].IEEE),  "");
            json_get_str(sm, "alias", g_general.sub_meters[idx].alias, sizeof(g_general.sub_meters[idx].alias), "");
            json_get_str(sm, "color", g_general.sub_meters[idx].color, sizeof(g_general.sub_meters[idx].color), "#3498db");
            g_general.sub_meters[idx].enabled = json_get_bool(sm, "enabled", true);

            if (strlen(g_general.sub_meters[idx].IEEE) > 0) {
                idx++;
            }
        }
        g_general.sub_meter_count = idx;
    }

    // Presence
    json_get_str(doc, "Presence", g_general.presence, sizeof(g_general.presence), "");
    g_general.enable_presence_graph = json_get_bool(doc, "enablePresenceGraph", true);

    // Delestage
    json_get_str(doc, "delestage", g_general.delestage, sizeof(g_general.delestage), "");
    g_general.delestage_restore_min = json_get_int(doc, "delestageRestoreMin", 5);
    g_general.delestage_restore_pct = json_get_int(doc, "delestageRestorePct", 80);
    g_general.delestage_anticip_pct = json_get_int(doc, "delestageAnticipPct", 0);

    cJSON_Delete(doc);

    ESP_LOGI(TAG, "General config loaded: ZLinky='%s' MQTT=%s NTP='%s' submeters=%d",
             g_general.zlinky,
             g_settings.enable_mqtt ? "ON" : "OFF",
             g_general.ntp_server,
             g_general.sub_meter_count);

    return ESP_OK;
}

// ============================================================================
// cfg_load_wifi  —  equivalent de loadConfigWifi() Arduino
// ============================================================================

esp_err_t cfg_load_wifi(void)
{
    char *json_str = read_file(CFG_PATH_WIFI);
    if (!json_str) {
        ESP_LOGW(TAG, "No WiFi config file, using defaults");
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *doc = cJSON_Parse(json_str);
    free(json_str);
    if (!doc) {
        ESP_LOGE(TAG, "Failed to parse %s", CFG_PATH_WIFI);
        return ESP_ERR_INVALID_ARG;
    }

    g_settings.enable_wifi = true;  // toujours active
    g_settings.enable_dhcp = json_get_bool(doc, "enableDHCP", true);
    json_get_str(doc, "ssid", g_settings.ssid,       sizeof(g_settings.ssid),       "");
    json_get_str(doc, "pass", g_settings.password,    sizeof(g_settings.password),   "");
    json_get_str(doc, "ip",   g_settings.ip_address,  sizeof(g_settings.ip_address), "");
    json_get_str(doc, "mask", g_settings.ip_mask,     sizeof(g_settings.ip_mask),    "");
    json_get_str(doc, "gw",   g_settings.ip_gateway,  sizeof(g_settings.ip_gateway), "");

    cJSON_Delete(doc);

    ESP_LOGI(TAG, "WiFi config loaded: SSID='%s' DHCP=%s", g_settings.ssid,
             g_settings.enable_dhcp ? "ON" : "OFF");

    return ESP_OK;
}

// ============================================================================
// cfg_save_general
// ============================================================================

static esp_err_t save_general_unlocked(void);

esp_err_t cfg_save_general(void)
{
    cfg_lock();
    s_save_general_requested = false;
    esp_err_t err = save_general_unlocked();
    cfg_unlock();
    return err;
}

void cfg_request_save_general(void)
{
    s_save_general_requested = true;
}

void cfg_save_general_if_requested(void)
{
    if (s_save_general_requested) cfg_save_general();
}

static esp_err_t save_general_unlocked(void)
{
    // Document de depart : le fichier existant, pour conserver les cles que
    // ce firmware ne gere pas (voir put_item)
    cJSON *doc = NULL;
    char *existing = read_file(CFG_PATH_GENERAL);
    if (existing) {
        doc = cJSON_Parse(existing);
        free(existing);
        if (doc && !cJSON_IsObject(doc)) { cJSON_Delete(doc); doc = NULL; }
    }
    if (!doc) doc = cJSON_CreateObject();
    if (!doc) return ESP_ERR_NO_MEM;

    // Settings (feature flags)
    put_flag(doc, "enableHWFlow",     g_settings.enable_hw_flow);
    put_flag(doc, "enableDebug",      g_settings.enable_debug);
    put_flag(doc, "enableMqtt",       g_settings.enable_mqtt);
    put_flag(doc, "enableUDP",        g_settings.enable_udp);
    put_flag(doc, "enableMarstek",    g_settings.enable_marstek);
    put_flag(doc, "enableNotif",      g_settings.enable_notif);
    put_flag(doc, "enableSecureHttp", g_settings.enable_secure_http);
    put_flag(doc, "enableWebPush",    g_settings.enable_webpush);
    put_flag(doc, "enableHistory",    g_settings.enable_history);

    // General
    put_num(doc, "firstStart", g_general.first_start);
    put_str(doc, "ZLinky",     g_general.zlinky);
    put_num(doc, "LinkyMode",  g_general.linky_mode);
    put_str(doc, "Production", g_general.production);
    put_str(doc, "Gaz",        g_general.gaz);
    put_str(doc, "Water",      g_general.water);
    put_num(doc, "shon",           g_general.house_surface);
    put_num(doc, "powerMaxDatas",  g_general.power_max_datas);

    // NTP
    put_str(doc, "ntpserver",  g_general.ntp_server);
    put_str(doc, "timezone",   g_general.timezone);
    put_num(doc, "timeoffset", g_general.time_offset);
    put_num(doc, "epoch",      (double)g_general.epoch_time);

    // Tarifs
    {
        const char *tarif_keys[] = {
            "tarifIdx1", "tarifIdx2", "tarifIdx3", "tarifIdx4", "tarifIdx5",
            "tarifIdx6", "tarifIdx7", "tarifIdx8", "tarifIdx9", "tarifIdx10"
        };
        for (int i = 0; i < 10; i++) {
            put_str(doc, tarif_keys[i], g_general.tarif_idx[i]);
        }
    }
    put_str(doc, "tarifAbo",     g_general.tarif_abo);
    put_str(doc, "tarifCSPE",    g_general.tarif_cspe);
    put_str(doc, "tarifCTA",     g_general.tarif_cta);
    put_str(doc, "tarifAboProd", g_general.tarif_abo_prod);
    put_str(doc, "tarifIdxProd", g_general.tarif_idx_prod);

    // SMTP
    put_str(doc, "servSMTP", g_general.serv_smtp);
    put_str(doc, "portSMTP", g_general.port_smtp);
    put_str(doc, "userSMTP", g_general.user_smtp);
    put_str(doc, "passSMTP", g_general.pass_smtp);
    put_str(doc, "toSMTP",   g_general.to_smtp);
    put_str(doc, "fromSMTP", g_general.from_smtp);
    put_num(doc, "securitySMTP", g_general.security_smtp);
    put_num(doc, "minTypeSMTP",  g_general.min_type_smtp);

    // MQTT
    put_str(doc, "servMQTT",       g_general.serv_mqtt);
    put_str(doc, "portMQTT",       g_general.port_mqtt);
    put_str(doc, "clientIDMQTT",   g_general.client_id_mqtt);
    put_str(doc, "userMQTT",       g_general.user_mqtt);
    put_str(doc, "passMQTT",       g_general.pass_mqtt);
    put_str(doc, "headerMQTT",     g_general.header_mqtt);
    put_flag(doc, "HAMQTT",         g_general.ha_mqtt);
    put_flag(doc, "TBMQTT",         g_general.tb_mqtt);
    put_flag(doc, "customMQTT",     g_general.custom_mqtt);
    put_str(doc, "customMQTTJson", g_general.custom_mqtt_json);

    // HTTP
    put_str(doc, "userHTTP", g_general.user_http);
    put_str(doc, "passHTTP", g_general.pass_http);

    // WebPush
    put_str(doc, "servWebPush", g_general.serv_webpush);
    put_str(doc, "userWebPush", g_general.user_webpush);
    put_str(doc, "passWebPush", g_general.pass_webpush);
    put_flag(doc, "webPushAuth", g_general.webpush_auth);

    // UDP
    put_str(doc, "servUDP",       g_general.serv_udp);
    put_str(doc, "portUDP",       g_general.port_udp);
    put_str(doc, "customUDPJson", g_general.custom_udp_json);

    // Gaz / Water
    put_num(doc, "coeffGaz",   g_general.coeff_gaz);
    put_num(doc, "coeffWater",  g_general.coeff_water);
    put_str(doc, "unitGaz",    g_general.unit_gaz);
    put_str(doc, "unitWater",  g_general.unit_water);
    put_str(doc, "tarifGaz",   g_general.tarif_gaz);
    put_str(doc, "tarifWater", g_general.tarif_water);

    // Developer
    put_flag(doc, "developerMode", g_general.developer_mode);

    // Notifications
    put_flag(doc, "PowerOutage",          g_notifications.power_outage);
    put_flag(doc, "PriceChange",          g_notifications.price_change);
    put_flag(doc, "SubscribedPower",      g_notifications.subscribed_power);
    put_flag(doc, "ProdSupConso",         g_notifications.prod_sup_conso);
    put_flag(doc, "ProdZero",             g_notifications.prod_zero);
    put_flag(doc, "ColorTomorrow",        g_notifications.color_tomorrow);
    put_flag(doc, "OverBudget",           g_notifications.over_budget);
    put_num(doc, "OverBudgetThreshold",  g_notifications.over_budget_threshold);
    put_flag(doc, "OverVoltage",          g_notifications.over_voltage);
    put_num(doc, "OverVoltageThreshold", g_notifications.over_voltage_threshold);
    put_flag(doc, "UnderVoltage",         g_notifications.under_voltage);
    put_num(doc, "UnderVoltageThreshold", g_notifications.under_voltage_threshold);
    put_flag(doc, "PEJP",                 g_notifications.pejp);
    put_flag(doc, "RedColor",             g_notifications.red_color);

    // Alertes avancees
    put_flag(doc, "OverPower",            g_notifications.over_power);
    put_num(doc,  "OverPowerThreshold",   g_notifications.over_power_threshold);
    put_num(doc,  "OverPowerDuration",    g_notifications.over_power_duration);
    put_num(doc,  "OverPowerCooldown",    g_notifications.over_power_cooldown);
    put_flag(doc, "Freeze",               g_notifications.freeze);
    put_num(doc,  "FreezeThreshold",      g_notifications.freeze_threshold);
    put_str(doc,  "FreezeSensorIEEE",     g_notifications.freeze_sensor_ieee);
    put_num(doc,  "FreezeCooldown",       g_notifications.freeze_cooldown);
    put_flag(doc, "DailyAnomaly",         g_notifications.daily_anomaly);
    put_num(doc,  "DailyAnomalyPercent",  g_notifications.daily_anomaly_percent);
    put_flag(doc, "WaterLeak",            g_notifications.water_leak);
    put_num(doc,  "WaterLeakThreshold",   g_notifications.water_leak_threshold);
    put_flag(doc, "NightWaterLeak",       g_notifications.night_water_leak);
    put_num(doc,  "NightWaterLeakThreshold", g_notifications.night_water_leak_threshold);
    put_flag(doc, "DailyMetrics",         g_notifications.daily_metrics);

    // Sous-compteurs
    cJSON *sub_arr = cJSON_CreateArray();
    put_item(doc, "subMeters", sub_arr);
    for (int i = 0; i < g_general.sub_meter_count; i++) {
        cJSON *sm = cJSON_CreateObject();
        cJSON_AddStringToObject(sm, "IEEE",  g_general.sub_meters[i].IEEE);
        cJSON_AddStringToObject(sm, "alias", g_general.sub_meters[i].alias);
        cJSON_AddStringToObject(sm, "color", g_general.sub_meters[i].color);
        cJSON_AddBoolToObject(sm, "enabled", g_general.sub_meters[i].enabled);
        cJSON_AddItemToArray(sub_arr, sm);
    }

    // Presence
    put_str(doc, "Presence", g_general.presence);
    put_bool(doc, "enablePresenceGraph", g_general.enable_presence_graph);

    // Delestage
    put_str(doc, "delestage", g_general.delestage);
    put_num(doc, "delestageRestoreMin", g_general.delestage_restore_min);
    put_num(doc, "delestageRestorePct", g_general.delestage_restore_pct);
    put_num(doc, "delestageAnticipPct", g_general.delestage_anticip_pct);

    // Serialiser et ecrire
    char *json_out = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);

    if (!json_out) {
        ESP_LOGE(TAG, "cJSON_Print failed");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = write_file(CFG_PATH_GENERAL, json_out);
    free(json_out);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "General config saved to %s", CFG_PATH_GENERAL);
    }
    return ret;
}

// ============================================================================
// cfg_save_wifi
// ============================================================================

esp_err_t cfg_save_wifi(void)
{
    cJSON *doc = cJSON_CreateObject();
    if (!doc) return ESP_ERR_NO_MEM;

    cJSON_AddNumberToObject(doc, "enableDHCP", g_settings.enable_dhcp ? 1 : 0);
    cJSON_AddStringToObject(doc, "ssid", g_settings.ssid);
    cJSON_AddStringToObject(doc, "pass", g_settings.password);
    cJSON_AddStringToObject(doc, "ip",   g_settings.ip_address);
    cJSON_AddStringToObject(doc, "mask", g_settings.ip_mask);
    cJSON_AddStringToObject(doc, "gw",   g_settings.ip_gateway);

    char *json_out = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (!json_out) return ESP_ERR_NO_MEM;

    esp_err_t ret = write_file(CFG_PATH_WIFI, json_out);
    free(json_out);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi config saved to %s", CFG_PATH_WIFI);
    }
    return ret;
}

esp_err_t cfg_write_file(const char *path, const char *data)
{
    if (!path || !data) return ESP_ERR_INVALID_ARG;
    return write_file(path, data);
}

// ============================================================================
// cfg_write_key  —  equivalent de config_write() Arduino
// ============================================================================

esp_err_t cfg_write_key(const char *path, const char *key, const char *value)
{
    if (!path || !key || !value) return ESP_ERR_INVALID_ARG;

    // Lire le fichier existant
    cJSON *doc = NULL;
    char *json_str = read_file(path);
    if (json_str) {
        doc = cJSON_Parse(json_str);
        free(json_str);
    }
    if (!doc) {
        doc = cJSON_CreateObject();
    }

    // Supprimer l'ancienne cle si elle existe, puis ajouter la nouvelle
    cJSON_DeleteItemFromObject(doc, key);
    cJSON_AddStringToObject(doc, key, value);

    // Ecrire
    char *json_out = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (!json_out) return ESP_ERR_NO_MEM;

    esp_err_t ret = write_file(path, json_out);
    free(json_out);
    return ret;
}

// ============================================================================
// cfg_read_key  —  equivalent de config_read() Arduino
// ============================================================================

esp_err_t cfg_read_key(const char *path, const char *key, char *out, size_t out_len)
{
    if (!path || !key || !out || out_len == 0) return ESP_ERR_INVALID_ARG;

    char *json_str = read_file(path);
    if (!json_str) return ESP_ERR_NOT_FOUND;

    cJSON *doc = cJSON_Parse(json_str);
    free(json_str);
    if (!doc) return ESP_ERR_INVALID_ARG;

    const cJSON *item = cJSON_GetObjectItem(doc, key);
    esp_err_t ret = ESP_ERR_NOT_FOUND;

    if (cJSON_IsString(item) && item->valuestring) {
        strlcpy(out, item->valuestring, out_len);
        ret = ESP_OK;
    } else if (cJSON_IsNumber(item)) {
        snprintf(out, out_len, "%d", item->valueint);
        ret = ESP_OK;
    } else if (cJSON_IsBool(item)) {
        snprintf(out, out_len, "%d", cJSON_IsTrue(item) ? 1 : 0);
        ret = ESP_OK;
    }

    cJSON_Delete(doc);
    return ret;
}

// ============================================================================
// cfg_ensure_directories
// ============================================================================

esp_err_t cfg_ensure_directories(void)
{
    const char *dirs[] = {
        "/littlefs/config",
        "/littlefs/db",
        "/littlefs/hst",
        "/littlefs/debug",
        "/littlefs/bk",
        "/littlefs/rt",
        "/littlefs/ota",
    };

    for (int i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); i++) {
        struct stat st;
        if (stat(dirs[i], &st) != 0) {
            // Le repertoire n'existe pas, le creer
            if (mkdir(dirs[i], 0775) != 0 && errno != EEXIST) {
                ESP_LOGW(TAG, "mkdir %s failed: %s", dirs[i], strerror(errno));
                // LittleFS peut ne pas supporter mkdir explicitement, pas fatal
            } else {
                ESP_LOGI(TAG, "Created directory: %s", dirs[i]);
            }
        }
    }

    return ESP_OK;
}
