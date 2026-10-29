#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Ethernet filaire par W5500 (SPI) -- portage du firmware LiXee-Box (v2.26)
//
// Pilote W5500 d'ESP-IDF, sur le bus SPI3.
//
// Politique reseau (identique a la LiXee-Box) :
//  - l'Ethernet est prioritaire : cable branche et adresse IP obtenue au
//    demarrage, le WiFi n'est pas connecte et le provisioning BLE n'est pas
//    lance ;
//  - sans cable au demarrage, le WiFi prend le relais (s'il est configure) ;
//    si les deux sont actifs, l'Ethernet reste la route par defaut ;
//  - une carte sans W5500 n'est pas affectee : la puce n'est pas detectee et
//    le pont fonctionne en WiFi comme avant.
//
// Configuration : /littlefs/config/configEthernet.json (meme format que la
// LiXee-Box) :
//   {"mode":0,"dhcp":1,"ip":"","mask":"","gw":"","dns":""}
// ============================================================================

#define ETH_W5500_CS       10
#define ETH_W5500_SCLK     12
#define ETH_W5500_MOSI     11
#define ETH_W5500_MISO     13
#define ETH_W5500_INT      14
#define ETH_W5500_RST      9
#define ETH_W5500_SPI_MHZ  20

#define ETH_CONFIG_PATH    "/littlefs/config/configEthernet.json"

// Mode de connexion
#define ETH_MODE_AUTO       0   // Ethernet prioritaire, WiFi en secours
#define ETH_MODE_ETH_ONLY   1   // Ethernet seul : le WiFi n'est jamais connecte
#define ETH_MODE_WIFI_ONLY  2   // WiFi seul : le W5500 est detecte mais pas utilise

typedef struct {
    uint8_t mode;
    bool    dhcp;
    char    ip[16];
    char    mask[16];
    char    gw[16];
    char    dns[16];
} eth_mgr_config_t;

typedef struct {
    bool     detected;     // puce detectee (meme en mode WiFi seul)
    bool     present;      // puce detectee et interface demarree
    bool     link_up;      // cable branche, lien etabli
    bool     has_ip;       // adresse IP obtenue (DHCP) ou appliquee (fixe) avec lien
    uint32_t ip, mask, gw, dns;   // ordre reseau (esp_ip4_addr_t.addr)
    uint8_t  mac[6];
    int      speed_mbps;   // 10 ou 100, 0 si inconnu
    bool     full_duplex;
} eth_mgr_status_t;

/** Lit la configuration (valeurs par defaut si le fichier est absent). */
esp_err_t eth_mgr_load_config(void);

/** Configuration courante (lecture seule). */
const eth_mgr_config_t *eth_mgr_get_config(void);

/** Enregistre une configuration (prise en compte au prochain demarrage). */
esp_err_t eth_mgr_save_config(const eth_mgr_config_t *cfg);

/**
 * Detecte le W5500 et demarre l'interface. A appeler apres esp_netif_init()
 * et esp_event_loop_create_default().
 * @return true si la puce est detectee et l'interface demarree ; false si la
 *         puce est absente ou en mode WiFi seul.
 */
bool eth_mgr_start(const char *hostname);

/**
 * Attend le lien (link_timeout_ms) puis l'adresse IP (ip_timeout_ms).
 * @return true si l'adresse IP est obtenue.
 */
bool eth_mgr_wait_for_ip(uint32_t link_timeout_ms, uint32_t ip_timeout_ms);

/** Etat courant de l'interface. */
void eth_mgr_get_status(eth_mgr_status_t *out);

bool eth_mgr_is_present(void);
bool eth_mgr_has_ip(void);

/** true si le WiFi ne doit pas etre connecte (Ethernet operationnel, ou mode Ethernet seul). */
bool eth_mgr_inhibits_wifi(void);

/** Serveur DNS remis s'il a ete efface (fin du bail DHCP du WiFi) : Ethernet avec adresse. */
void eth_mgr_ensure_dns(void);

#ifdef __cplusplus
}
#endif
