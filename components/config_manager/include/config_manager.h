#pragma once

// ============================================================================
// Config Manager - Gestion de la configuration persistante LiXee-Box
//
// Port des structures Arduino (config.h) et des fonctions de chargement
// (loadConfigGeneral / loadConfigWifi) vers ESP-IDF.
//
// Stockage: JSON dans LittleFS (/littlefs/config/configGeneral.json)
// ============================================================================

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Constantes
// ============================================================================

#define CFG_MAX_SUBMETERS       10
#define CFG_PATH_GENERAL        "/littlefs/config/configGeneral.json"
#define CFG_PATH_WIFI           "/littlefs/config/configWifi.json"
#define CFG_PATH_NOTIFICATIONS  "/littlefs/config/notifications.json"
#define CFG_PATH_RULES          "/littlefs/config/rules.json"

// ============================================================================
// Structures de configuration
// ============================================================================

typedef struct {
    char IEEE[20];          // Adresse IEEE du device (ex: "00158D0001234567")
    char alias[32];         // Nom affiche (ex: "Salon", "Cuisine")
    char color[10];         // Couleur hex pour le donut (ex: "#e74c3c")
    bool enabled;           // Active/desactive
} cfg_submeter_t;

/**
 * Parametres systeme et feature flags
 * (correspond a ConfigSettingsStruct Arduino)
 */
typedef struct {
    // WiFi
    bool enable_wifi;
    bool enable_dhcp;
    bool connected_wifi_sta;
    int  channel_wifi;
    int  rssi_wifi;
    char ssid[50];
    char bssid[24];
    char password[150];
    char ip_address[18];
    char ip_mask[16];
    char ip_gateway[18];

    // Feature flags
    bool enable_debug;
    bool enable_notif;
    bool enable_webpush;
    bool enable_secure_http;
    bool enable_mqtt;
    bool enable_udp;
    bool enable_marstek;
    bool enable_history;
    bool enable_hw_flow;
} cfg_settings_t;

/**
 * Configuration Zigbee / ZiGate
 * (correspond a ZigbeeConfig + ZiGateInfosStruct Arduino)
 */
typedef struct {
    char     application[9];
    char     type;
    char     sdk;
    int      channel;
    uint8_t  network;
    uint64_t zigbee_mac;
    // ZiGate infos (remplies par les reponses ZiGate)
    char     device[8];
    char     mac[8];
    char     flash[8];
} cfg_zigbee_t;

/**
 * Configuration des notifications / alertes
 * (correspond a ConfigNotification Arduino)
 */
typedef struct {
    bool price_change;
    bool pejp;
    bool color_tomorrow;
    bool red_color;
    bool over_voltage;
    int  over_voltage_threshold;
    bool under_voltage;
    int  under_voltage_threshold;
    bool over_budget;
    int  over_budget_threshold;
    bool subscribed_power;
    bool power_outage;
    bool prod_sup_conso;
    bool prod_zero;

    // Alertes avancees (lot 4) : memes cles que configGeneral.json de la
    // LiXee-Box (OverPower, Freeze, DailyAnomaly, WaterLeak...). Les valeurs
    // par defaut sont celles de cfgInt() Arduino quand la cle est absente.
    bool over_power;
    int  over_power_threshold;          // VA (libelle W dans la page), defaut 6000
    int  over_power_duration;           // s au-dessus du seuil avant alerte, defaut 60
    int  over_power_cooldown;           // min entre deux alertes, defaut 30
    bool freeze;
    int  freeze_threshold;              // centiemes de degC (300 = 3,00 degC)
    char freeze_sensor_ieee[20];        // capteur 0x0402 surveille
    int  freeze_cooldown;               // min entre deux alertes, defaut 60
    bool daily_anomaly;
    int  daily_anomaly_percent;         // % au-dessus de la moyenne 7 j, defaut 30
    bool water_leak;
    int  water_leak_threshold;          // L consommes de 00h a 06h, defaut 5
    bool night_water_leak;
    int  night_water_leak_threshold;    // L consommes de 00h a 05h59, defaut 1
    bool daily_metrics;                 // resume quotidien a 23h50
} cfg_notifications_t;

/**
 * Configuration generale
 * (correspond a ConfigGeneralStruct Arduino)
 */
typedef struct {
    int  first_start;

    // Devices principaux (adresse IEEE)
    char zlinky[20];
    char production[20];
    char gaz[20];
    char water[20];

    // Linky
    int  linky_mode;
    int  house_surface;         // SHON en m2
    int  power_max_datas;

    // NTP / Temps
    char ntp_server[50];
    int  time_offset;
    long epoch_time;
    char timezone[50];

    // Tarifs energie (10 index + abo + taxes)
    char tarif_idx[10][10];     // tarifIdx1..tarifIdx10
    char tarif_abo[10];
    char tarif_cspe[10];
    char tarif_cta[10];
    char tarif_abo_prod[10];
    char tarif_idx_prod[10];

    // SMTP (email). Tailles portees a 64 (50 sur la LiXee-Box) : adresses et
    // mots de passe d'application des fournisseurs actuels.
    char serv_smtp[64];
    char port_smtp[8];
    char user_smtp[64];
    char pass_smtp[64];
    char to_smtp[64];           // destinataire ("toSMTP"), userSMTP si vide
    char from_smtp[64];         // expediteur ("fromSMTP"), userSMTP si vide
    int  security_smtp;         // "securitySMTP" : 0 auto, 1 SSL/TLS, 2 STARTTLS, 3 aucune
    int  min_type_smtp;         // "minTypeSMTP" : type minimal envoye (0 info ... 3 critique)

    // MQTT
    char serv_mqtt[50];
    char port_mqtt[50];
    char client_id_mqtt[50];
    char user_mqtt[50];
    char pass_mqtt[128];
    char header_mqtt[128];
    bool ha_mqtt;               // Mode Home Assistant
    bool tb_mqtt;               // Mode ThingsBoard
    bool custom_mqtt;           // Mode Custom JSON
    char custom_mqtt_json[512]; // Template JSON custom

    // HTTP auth
    char user_http[50];
    char pass_http[50];

    // WebPush (URL complete : 128 au lieu de 50 sur la LiXee-Box)
    char serv_webpush[128];
    char user_webpush[50];
    char pass_webpush[50];
    bool webpush_auth;

    // Marstek
    bool connected_marstek;
    char marstek_ip[18];

    // UDP
    char serv_udp[50];
    char port_udp[50];
    char custom_udp_json[512];

    // Gaz / Water coefficients
    float coeff_gaz;
    float coeff_water;
    float coeff_production;
    char  unit_gaz[12];       // texte libre (Wh, kWh, m3, L...), comme la LiXee-Box
    char  unit_water[12];
    char  tarif_gaz[10];
    char  tarif_water[10];

    // WiFi scan
    int  scan_number;

    // Developer
    bool developer_mode;

    // Sous-compteurs
    cfg_submeter_t sub_meters[CFG_MAX_SUBMETERS];
    int  sub_meter_count;

    // Presence
    char presence[20];
    bool enable_presence_graph;

    // Delestage (prises coupees sur depassement de la puissance souscrite)
    char delestage[18 * 16];           // IEEE des prises, separes par des virgules
    int  delestage_restore_min;        // rallumage apres N minutes sous le seuil (0 = jamais)
    int  delestage_restore_pct;        // seuil de rallumage, % de la puissance souscrite
    int  delestage_anticip_pct;        // delestage anticipe : moyenne 1 min >= N % (0 = non)
} cfg_general_t;

// ============================================================================
// Instance globale (accessible par tous les composants)
// ============================================================================

/**
 * Acces aux instances globales de configuration.
 * Initialisees par cfg_init(), remplies par cfg_load_general() / cfg_load_wifi().
 */
const cfg_settings_t*      cfg_get_settings(void);
cfg_settings_t*            cfg_get_settings_mut(void);
const cfg_general_t*       cfg_get_general(void);
cfg_general_t*             cfg_get_general_mut(void);
const cfg_notifications_t* cfg_get_notifications(void);
cfg_notifications_t*       cfg_get_notifications_mut(void);
const cfg_zigbee_t*        cfg_get_zigbee(void);
cfg_zigbee_t*              cfg_get_zigbee_mut(void);

// ============================================================================
// API publique
// ============================================================================

/**
 * Initialise le config manager (met a zero les structures).
 * Doit etre appele au demarrage avant cfg_load_*().
 */
esp_err_t cfg_init(void);

/**
 * Charge la configuration generale depuis LittleFS.
 * Remplit cfg_settings, cfg_general, cfg_notifications.
 * @return ESP_OK si charge, ESP_ERR_NOT_FOUND si fichier absent (valeurs par defaut)
 */
esp_err_t cfg_load_general(void);

/**
 * Charge la configuration WiFi depuis LittleFS.
 * Remplit les champs WiFi de cfg_settings.
 * @return ESP_OK si charge, ESP_ERR_NOT_FOUND si fichier absent
 */
esp_err_t cfg_load_wifi(void);

/**
 * Sauvegarde la configuration generale dans LittleFS.
 * Serialise cfg_settings + cfg_general + cfg_notifications en JSON, en partant
 * du fichier existant : les cles inconnues de ce firmware sont conservees.
 */
esp_err_t cfg_save_general(void);

/**
 * Demande d'enregistrement differe de la config generale, pour les taches qui
 * ne doivent pas ecrire en flash elles-memes (pile en PSRAM, tache radio) :
 * la boucle principale l'execute via cfg_save_general_if_requested().
 */
void cfg_request_save_general(void);
void cfg_save_general_if_requested(void);

/**
 * Sauvegarde la configuration WiFi dans LittleFS.
 */
esp_err_t cfg_save_wifi(void);

/**
 * Ecrit une seule cle dans un fichier de config JSON.
 * Equivalent de config_write() Arduino.
 * @param path Chemin complet du fichier (ex: CFG_PATH_GENERAL)
 * @param key  Cle JSON a modifier
 * @param value Valeur (string)
 */
esp_err_t cfg_write_key(const char *path, const char *key, const char *value);

/**
 * Ecrit un fichier complet de facon atomique (fichier temporaire + rename).
 * @param path Chemin complet du fichier
 * @param data Contenu (chaine terminee par '\0')
 */
esp_err_t cfg_write_file(const char *path, const char *data);

/**
 * Lit une seule cle depuis un fichier de config JSON.
 * Equivalent de config_read() Arduino.
 * @param path Chemin complet du fichier
 * @param key  Cle JSON a lire
 * @param out  Buffer de sortie
 * @param out_len Taille du buffer
 * @return ESP_OK si trouve, ESP_ERR_NOT_FOUND si absent
 */
esp_err_t cfg_read_key(const char *path, const char *key, char *out, size_t out_len);

/**
 * Cree les repertoires LittleFS necessaires s'ils n'existent pas.
 * (/config, /db, /hst, /debug, /bk, /rt, /ota)
 */
esp_err_t cfg_ensure_directories(void);

#ifdef __cplusplus
}
#endif
