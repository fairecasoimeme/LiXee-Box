#pragma once
#include <Arduino.h>
#include <IPAddress.h>

/* Ethernet filaire par W5500 (SPI) -- v2.26.
 *
 * Pilote W5500 d'ESP-IDF (compile dans le core Arduino 2.0.17, CONFIG_ETH_SPI_ETHERNET_W5500),
 * sur le bus SPI3 : SPI2 (FSPI) est deja pris par le module LoRa.
 *
 * Politique reseau :
 *  - l'Ethernet est prioritaire : cable branche et adresse IP obtenue au demarrage, le WiFi n'est
 *    pas demarre du tout (~40 Ko de heap interne economises) ;
 *  - sans cable au demarrage, ou si le cable est debranche ensuite, le WiFi prend le relais
 *    (s'il est configure) ; si les deux sont actifs, l'Ethernet reste la route par defaut ;
 *  - tant que le cable est branche, le provisioning BLE (~64 Ko de heap) n'est jamais lance ;
 *  - une carte sans W5500 n'est pas affectee : la puce n'est pas detectee et le firmware
 *    fonctionne en WiFi comme avant.
 *
 * Configuration : /config/configEthernet.json
 *   {"mode":0,"dhcp":1,"ip":"","mask":"","gw":"","dns":""}  (mode : voir ETH_MODE_*)
 * Le RAZ par 3 redemarrages rapides efface ce fichier (retour en Automatique, DHCP).
 */

#define ETH_W5500_CS       10
#define ETH_W5500_SCLK     12
#define ETH_W5500_MOSI     11
#define ETH_W5500_MISO     13
#define ETH_W5500_INT      14
#define ETH_W5500_RST      9
#define ETH_W5500_SPI_MHZ  20

// Mode de connexion (page Config LAN)
#define ETH_MODE_AUTO       0   // Ethernet prioritaire, WiFi en secours
#define ETH_MODE_ETH_ONLY   1   // Ethernet seul : le WiFi n'est jamais demarre
#define ETH_MODE_WIFI_ONLY  2   // WiFi seul : le W5500 est detecte mais pas utilise

struct EthernetConfigStruct {
  uint8_t mode = ETH_MODE_AUTO;
  bool dhcp   = true;
  char ip[16]   = "";
  char mask[16] = "";
  char gw[16]   = "";
  char dns[16]  = "";
};
extern EthernetConfigStruct ConfigEthernet;

bool ethernetLoadConfig();
bool ethernetSaveConfig();

// Detecte le W5500 et demarre l'interface. false si puce absente ou mode WiFi seul.
bool ethernetBegin(const char* hostname);
// Attend le lien (linkTimeoutMs) puis l'adresse IP (ipTimeoutMs). true si l'IP est obtenue.
bool ethernetWaitForIP(uint32_t linkTimeoutMs, uint32_t ipTimeoutMs);
// A appeler dans la boucle principale : journalise les changements d'etat.
void ethernetLoop();

bool      ethernetDetected();    // puce detectee (meme en mode WiFi seul)
bool      ethernetPresent();     // puce detectee et interface demarree
bool      ethernetLinkUp();      // cable branche, lien etabli
bool      ethernetHasIP();       // adresse IP obtenue (DHCP) ou configuree (statique) avec lien
IPAddress ethernetLocalIP();
IPAddress ethernetSubnetMask();
IPAddress ethernetGatewayIP();
IPAddress ethernetDnsIP();
String    ethernetMacAddress();
int       ethernetLinkSpeed();   // 10 ou 100 (Mbit/s), 0 si inconnu
bool      ethernetFullDuplex();

// Reseau, toutes interfaces confondues
bool      netConnected();        // Ethernet avec IP ou WiFi connecte
IPAddress netLocalIP();          // IP Ethernet si disponible, sinon IP WiFi
const char* netInterfaceName();  // "ethernet", "wifi" ou "" (aucune)
