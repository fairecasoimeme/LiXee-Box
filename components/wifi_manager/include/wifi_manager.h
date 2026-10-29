#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// WiFi Manager - Gestion WiFi STA avec BLE provisioning LiXee-Assist
//
// Boot flow:
//   NVS creds exist?
//     OUI -> WiFi connect (max retries avec backoff)
//       Connecte  -> continue (ZiGate + Matter + Web)
//       Echec     -> BLE provisioning -> creds recues -> restart
//     NON -> BLE provisioning immediat -> creds recues -> restart
// ============================================================================

// --- Configuration ---

#define WIFI_MGR_MAX_RETRIES_DEFAULT       5
#define WIFI_MGR_CONNECT_TIMEOUT_MS        30000   // DHCP observe a ~13 s sur certains reseaux
#define WIFI_MGR_BACKOFF_BASE_MS           5000
#define WIFI_MGR_BACKOFF_MAX_MS            30000

typedef struct {
    uint8_t  max_retries;           // Nb max de tentatives avant fallback BLE
    uint32_t connect_timeout_ms;    // Timeout par tentative de connexion
    uint32_t backoff_base_ms;       // Delai initial entre tentatives
    uint32_t backoff_max_ms;        // Delai max (cap du backoff exponentiel)
} wifi_mgr_config_t;

#define WIFI_MGR_CONFIG_DEFAULT() { \
    .max_retries        = WIFI_MGR_MAX_RETRIES_DEFAULT, \
    .connect_timeout_ms = WIFI_MGR_CONNECT_TIMEOUT_MS,  \
    .backoff_base_ms    = WIFI_MGR_BACKOFF_BASE_MS,     \
    .backoff_max_ms     = WIFI_MGR_BACKOFF_MAX_MS,      \
}

// --- Credentials WiFi ---

typedef struct {
    char ssid[33];
    char password[65];
    bool valid;
} wifi_mgr_credentials_t;

// --- API publique ---

/**
 * Initialise le WiFi driver en mode STA et charge les credentials NVS.
 * N'etablit PAS la connexion (wifi_mgr_start le fait).
 */
esp_err_t wifi_mgr_init(const wifi_mgr_config_t *config);

/**
 * Connexion WiFi au demarrage : attend la connexion (N essais).
 * ESP_OK : connecte. Sans identifiants NVS ou apres echec : provisioning BLE
 * lance en tache de fond et retour ESP_ERR_TIMEOUT (le WiFi est retente toutes
 * les 2 min, le BLE s'arrete des que le WiFi ou l'Ethernet a une adresse ;
 * redemarrage apres reception d'identifiants BLE).
 */
esp_err_t wifi_mgr_start(void);

/**
 * Secours WiFi sans attente (Ethernet perdu en mode automatique) : connexion
 * aux identifiants NVS, reconnexion illimitee ensuite. ESP_ERR_NOT_FOUND sans
 * identifiants.
 */
esp_err_t wifi_mgr_connect_background(void);

/** Coupe le WiFi (Ethernet retrouve) : plus de reconnexion jusqu'au prochain secours. */
void wifi_mgr_disconnect(void);

/**
 * Partage du mDNS avec Matter. A appeler avant wifi_mgr_start().
 *
 * Matter (CHIP) utilise le meme composant mDNS : il doit l'initialiser lui-meme
 * (son init echoue si le composant l'est deja) et lui impose son propre nom
 * d'hote. En mode partage, le mDNS du pont attend donc wifi_mgr_start_mdns(),
 * appele apres le demarrage de Matter, et publie LIXEEBOX-XXYY.local comme
 * nom d'hote delegue, sans toucher au nom d'hote de Matter.
 */
void wifi_mgr_set_mdns_shared(bool shared);

/**
 * Demarre le mDNS du pont s'il etait differe (mode partage). Sans effet sinon.
 */
void wifi_mgr_start_mdns(void);

/**
 * Retourne true si WiFi est actuellement connecte.
 */
bool wifi_mgr_is_connected(void);

/**
 * Retourne true si le BLE provisioning est actuellement actif.
 */
bool wifi_mgr_is_ble_active(void);

/**
 * Lit les credentials WiFi stockes en NVS.
 * @return ESP_OK si des credentials valides existent.
 */
esp_err_t wifi_mgr_get_credentials(wifi_mgr_credentials_t *creds);

/**
 * Sauvegarde des credentials WiFi en NVS.
 * Ne declenche PAS de reconnexion — appeler esp_restart() apres.
 */
esp_err_t wifi_mgr_set_credentials(const char *ssid, const char *password);

/**
 * Efface les credentials WiFi du NVS.
 */
esp_err_t wifi_mgr_clear_credentials(void);

/**
 * Adresse IP fixe du WiFi (configWifi.json, enableDHCP = 0), appliquee au
 * demarrage de l'interface. NULL ou adresses invalides : DHCP.
 */
void wifi_mgr_set_static_ip(const char *ip, const char *mask, const char *gw);

/**
 * Informations sur l'etat WiFi courant.
 */
esp_err_t wifi_mgr_get_info(char *ssid, size_t ssid_len,
                             char *ip, size_t ip_len,
                             int8_t *rssi);

/**
 * Retourne le nom BLE du device ("LIXEEBOX-XXYY").
 * Le buffer doit faire au moins 20 octets.
 */
esp_err_t wifi_mgr_get_ble_device_name(char *name, size_t len);

/**
 * Retourne le hostname mDNS du device ("LIXEEBOX-XXYY").
 * Identique au nom BLE — accessible via http://LIXEEBOX-XXYY.local
 * Le buffer doit faire au moins 20 octets.
 */
esp_err_t wifi_mgr_get_hostname(char *hostname, size_t len);

#ifdef __cplusplus
}
#endif
