#include "ethernet.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "config.h"

#define ETH_CONFIG_FILE "/config/configEthernet.json"

// Initialisation de la pile TCP/IP et de la boucle d'evenements par le core Arduino
// (WiFiGeneric.cpp) : la meme que pour le WiFi, pour que les deux interfaces cohabitent.
bool tcpipInit();

EthernetConfigStruct ConfigEthernet;

static esp_eth_handle_t    s_eth = nullptr;
static esp_netif_t*        s_netif = nullptr;
static spi_device_handle_t s_spi = nullptr;
static bool                s_present = false;
static bool                s_detected = false;
static volatile bool       s_link = false;
static volatile bool       s_gotIP = false;
static volatile uint32_t   s_ip = 0, s_mask = 0, s_gw = 0;
static uint8_t             s_mac[6] = {0};
static volatile int        s_staticResult = 0;
static bool                s_loggedLink = false, s_loggedIP = false;   // dernier etat annonce    // IP fixe : 0 = rien, 1 = appliquee, -1 = refusee

// Priorite de route au-dessus du WiFi STA (100) : l'IDF met l'Ethernet a 50 par defaut, ce qui
// laisserait la route par defaut au WiFi quand les deux interfaces sont actives.
static esp_netif_inherent_config_t s_inherent = ESP_NETIF_INHERENT_DEFAULT_ETH();

// ---------------------------------------------------------------- configuration

bool ethernetLoadConfig() {
  File f = LittleFS.open(ETH_CONFIG_FILE, "r");
  if (!f) return false;                         // pas de fichier : valeurs par defaut (actif, DHCP)
  SpiRamJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  // "enable" : format de la premiere version (actif / inactif)
  int mode = doc["mode"] | ((doc["enable"] | 1) ? ETH_MODE_AUTO : ETH_MODE_WIFI_ONLY);
  ConfigEthernet.mode = (mode >= ETH_MODE_AUTO && mode <= ETH_MODE_WIFI_ONLY) ? mode : ETH_MODE_AUTO;
  ConfigEthernet.dhcp   = (doc["dhcp"] | 1) != 0;
  strlcpy(ConfigEthernet.ip,   doc["ip"]   | "", sizeof(ConfigEthernet.ip));
  strlcpy(ConfigEthernet.mask, doc["mask"] | "", sizeof(ConfigEthernet.mask));
  strlcpy(ConfigEthernet.gw,   doc["gw"]   | "", sizeof(ConfigEthernet.gw));
  strlcpy(ConfigEthernet.dns,  doc["dns"]  | "", sizeof(ConfigEthernet.dns));
  return true;
}

bool ethernetSaveConfig() {
  SpiRamJsonDocument doc(1024);
  doc["mode"]   = ConfigEthernet.mode;
  doc["dhcp"]   = ConfigEthernet.dhcp ? 1 : 0;
  doc["ip"]     = ConfigEthernet.ip;
  doc["mask"]   = ConfigEthernet.mask;
  doc["gw"]     = ConfigEthernet.gw;
  doc["dns"]    = ConfigEthernet.dns;
  File f = LittleFS.open(ETH_CONFIG_FILE, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

// ---------------------------------------------------------------- evenements

// IP fixe, appliquee au demarrage de l'interface (ETHERNET_EVENT_START) : avant, l'interface
// lwIP n'existe pas encore. Silencieuse (tache des evenements) ; le resultat est journalise
// par ethernetLoop().
static bool applyStaticIP() {
  IPAddress ip, mask, gw, dns;
  if (!ip.fromString(ConfigEthernet.ip) || !mask.fromString(ConfigEthernet.mask) ||
      !gw.fromString(ConfigEthernet.gw)) {
    return false;                               // incomplete ou invalide : on reste en DHCP
  }
  esp_netif_dhcpc_stop(s_netif);
  esp_netif_ip_info_t info;
  info.ip.addr      = static_cast<uint32_t>(ip);
  info.netmask.addr = static_cast<uint32_t>(mask);
  info.gw.addr      = static_cast<uint32_t>(gw);
  if (esp_netif_set_ip_info(s_netif, &info) != ESP_OK) {
    esp_netif_dhcpc_start(s_netif);
    return false;
  }
  // DNS : celui saisi, a defaut la passerelle (cas le plus courant sur une box internet)
  if (!dns.fromString(ConfigEthernet.dns)) dns = gw;
  esp_netif_dns_info_t d;
  memset(&d, 0, sizeof(d));
  d.ip.type = ESP_IPADDR_TYPE_V4;
  d.ip.u_addr.ip4.addr = static_cast<uint32_t>(dns);
  esp_netif_set_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &d);
  s_ip = info.ip.addr;
  s_mask = info.netmask.addr;
  s_gw = info.gw.addr;
  return true;
}


// Execute dans la tache des evenements systeme (pile reduite) : on ne fait que mettre a jour
// l'etat. Les messages sont emis depuis la boucle principale (ethernetLoop).
static void onEthEvent(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == ETH_EVENT) {
    if (id == ETHERNET_EVENT_START) {
      if (!ConfigEthernet.dhcp && s_staticResult == 0) s_staticResult = applyStaticIP() ? 1 : -1;
    } else if (id == ETHERNET_EVENT_CONNECTED) {
      s_link = true;
    } else if (id == ETHERNET_EVENT_DISCONNECTED || id == ETHERNET_EVENT_STOP) {
      s_link = false;
      s_gotIP = false;
    }
  } else if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP) {
    ip_event_got_ip_t* e = (ip_event_got_ip_t*)data;
    if (e->esp_netif != s_netif) return;
    s_ip   = e->ip_info.ip.addr;
    s_mask = e->ip_info.netmask.addr;
    s_gw   = e->ip_info.gw.addr;
    s_gotIP = true;
  }
}

// ---------------------------------------------------------------- demarrage

// Detection du W5500. Le pilote d'ESP-IDF 4.4 ne la fait PAS : il lit le registre de version
// et se contente de l'afficher, si bien que son installation reussit aussi sur une carte sans
// W5500 ("W5500 detecte" a tort). VERSIONR (0x0039, bloc commun) vaut toujours 0x04 sur un
// W5500 ; une ligne MISO en l'air donne 0x00 ou 0xFF.
static bool w5500Probe() {
  gpio_num_t rst = (gpio_num_t)ETH_W5500_RST;
  gpio_reset_pin(rst);
  gpio_set_direction(rst, GPIO_MODE_OUTPUT);
  gpio_set_level(rst, 0);
  delay(2);                       // RSTn bas au moins 500 us
  gpio_set_level(rst, 1);
  delay(50);                      // verrouillage de la PLL interne
  for (int i = 0; i < 3; i++) {
    spi_transaction_t t = {};
    t.cmd = 0x0039;               // phase d'adresse : VERSIONR
    t.addr = 0x00;                // phase de controle : bloc commun, lecture, longueur variable
    t.length = 8;
    t.rxlength = 8;
    t.flags = SPI_TRANS_USE_RXDATA;
    if (spi_device_polling_transmit(s_spi, &t) == ESP_OK && t.rx_data[0] == 0x04) return true;
    delay(5);
  }
  gpio_reset_pin(rst);            // carte sans W5500 : on ne laisse pas la broche pilotee
  return false;
}

static void releaseSpi() {
  if (s_spi) { spi_bus_remove_device(s_spi); s_spi = nullptr; }
  spi_bus_free(SPI3_HOST);
}

bool ethernetBegin(const char* hostname) {
  ethernetLoadConfig();
  if (!tcpipInit()) {
    Serial.println("[ETH] pile TCP/IP indisponible");
    return false;
  }

  // Bus SPI3 dedie au W5500 (SPI2/FSPI : module LoRa)
  spi_bus_config_t bus = {};
  bus.miso_io_num = ETH_W5500_MISO;
  bus.mosi_io_num = ETH_W5500_MOSI;
  bus.sclk_io_num = ETH_W5500_SCLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
    Serial.println("[ETH] bus SPI3 indisponible");
    return false;
  }
  spi_device_interface_config_t dev = {};
  dev.command_bits = 16;           // phase d'adresse de la trame W5500
  dev.address_bits = 8;            // phase de controle de la trame W5500
  dev.mode = 0;
  dev.clock_speed_hz = ETH_W5500_SPI_MHZ * 1000 * 1000;
  dev.spics_io_num = ETH_W5500_CS;
  dev.queue_size = 20;
  if (spi_bus_add_device(SPI3_HOST, &dev, &s_spi) != ESP_OK) {
    Serial.println("[ETH] ajout du W5500 sur le bus SPI impossible");
    releaseSpi();
    return false;
  }

  if (!w5500Probe()) {
    Serial.println("[ETH] aucun W5500 sur cette carte : fonctionnement en WiFi");
    releaseSpi();
    return false;
  }

  // Service d'interruptions GPIO (broche INT du W5500), avec les memes options que le core
  // Arduino : attachInterrupt() (LoRa) accepte qu'il soit deja installe. S'il l'est deja (module
  // LoRa initialise avant), l'IDF le signale par une erreur sans consequence : on la masque.
  esp_log_level_set("gpio", ESP_LOG_NONE);
  gpio_install_isr_service((int)ARDUINO_ISR_FLAG);
  esp_log_level_set("gpio", ESP_LOG_ERROR);

  eth_w5500_config_t w5500 = ETH_W5500_DEFAULT_CONFIG(s_spi);
  w5500.int_gpio_num = ETH_W5500_INT;
  eth_mac_config_t macCfg = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phyCfg = ETH_PHY_DEFAULT_CONFIG();
  phyCfg.phy_addr = 1;
  phyCfg.reset_gpio_num = ETH_W5500_RST;
  esp_eth_mac_t* mac = esp_eth_mac_new_w5500(&w5500, &macCfg);
  esp_eth_phy_t* phy = esp_eth_phy_new_w5500(&phyCfg);
  if (!mac || !phy) {
    Serial.println("[ETH] creation du pilote W5500 impossible");
    if (mac) mac->del(mac);
    if (phy) phy->del(phy);
    releaseSpi();
    return false;
  }

  // L'installation lit le registre de version de la puce : elle echoue si aucun W5500 ne repond.
  esp_eth_config_t ethCfg = ETH_DEFAULT_CONFIG(mac, phy);
  if (esp_eth_driver_install(&ethCfg, &s_eth) != ESP_OK || !s_eth) {
    Serial.println("[ETH] aucun W5500 detecte : fonctionnement en WiFi");
    s_eth = nullptr;
    mac->del(mac);
    phy->del(phy);
    releaseSpi();
    return false;
  }

  s_detected = true;
  // Mode WiFi seul : la puce est detectee (la page Config LAN propose alors de la reactiver),
  // mais l'interface n'est pas demarree et ses ressources sont rendues.
  if (ConfigEthernet.mode == ETH_MODE_WIFI_ONLY) {
    Serial.println("[ETH] W5500 detecte, non utilise (mode WiFi seul)");
    esp_eth_driver_uninstall(s_eth);
    s_eth = nullptr;
    mac->del(mac);
    phy->del(phy);
    releaseSpi();
    return false;
  }

  // Le W5500 n'a pas d'adresse MAC propre : on lui donne celle reservee a l'Ethernet de l'ESP32
  esp_read_mac(s_mac, ESP_MAC_ETH);
  esp_eth_ioctl(s_eth, ETH_CMD_S_MAC_ADDR, s_mac);

  s_inherent.route_prio = 150;
  esp_netif_config_t netCfg = {};
  netCfg.base = &s_inherent;
  netCfg.driver = nullptr;
  netCfg.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
  s_netif = esp_netif_new(&netCfg);
  if (!s_netif) {
    Serial.println("[ETH] creation de l'interface reseau impossible");
    esp_eth_driver_uninstall(s_eth);
    s_eth = nullptr;
    releaseSpi();
    return false;
  }
  if (hostname && hostname[0]) esp_netif_set_hostname(s_netif, hostname);
  esp_netif_attach(s_netif, esp_eth_new_netif_glue(s_eth));

  esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &onEthEvent, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &onEthEvent, nullptr);

  if (esp_eth_start(s_eth) != ESP_OK) {
    Serial.println("[ETH] demarrage de l'interface impossible");
    return false;
  }
  s_present = true;
  Serial.printf("[ETH] W5500 detecte, MAC %s, %s\n", ethernetMacAddress().c_str(),
                ConfigEthernet.dhcp ? "DHCP" : "IP fixe");
  return true;
}

bool ethernetWaitForIP(uint32_t linkTimeoutMs, uint32_t ipTimeoutMs) {
  if (!s_present) return false;
  uint32_t start = millis();
  while (!s_link && millis() - start < linkTimeoutMs) { esp_task_wdt_reset(); delay(50); }
  if (!s_link) {
    Serial.println("[ETH] pas de lien (cable debranche ?)");
    return false;
  }
  start = millis();
  while (!ethernetHasIP() && millis() - start < ipTimeoutMs) { esp_task_wdt_reset(); delay(50); }
  if (!ethernetHasIP()) {
    Serial.println("[ETH] lien etabli mais pas d'adresse IP (DHCP sans reponse ?)");
    return false;
  }
  Serial.printf("[ETH] lien %d Mbit/s %s, adresse IP %s (passerelle %s)\n", ethernetLinkSpeed(),
                ethernetFullDuplex() ? "full duplex" : "half duplex",
                ethernetLocalIP().toString().c_str(), ethernetGatewayIP().toString().c_str());
  s_loggedLink = s_loggedIP = true;
  return true;
}

void ethernetLoop() {
  if (!s_present) return;
  static bool staticLogged = false;
  bool& lastLink = s_loggedLink;
  bool& lastIP = s_loggedIP;
  if (!staticLogged && s_staticResult != 0) {
    staticLogged = true;
    if (s_staticResult < 0) Serial.println("[ETH] IP fixe incomplete ou refusee : DHCP utilise");
  }
  bool link = s_link, ip = ethernetHasIP();
  if (link != lastLink) {
    if (link) Serial.printf("[ETH] lien etabli : %d Mbit/s, %s\n", ethernetLinkSpeed(),
                            ethernetFullDuplex() ? "full duplex" : "half duplex");
    else      Serial.println("[ETH] lien perdu");
    lastLink = link;
  }
  if (ip != lastIP) {
    if (ip) Serial.printf("[ETH] adresse IP : %s (passerelle %s)\n",
                          ethernetLocalIP().toString().c_str(), ethernetGatewayIP().toString().c_str());
    lastIP = ip;
  }
}

// ---------------------------------------------------------------- etat

bool ethernetDetected() { return s_detected; }
bool ethernetPresent() { return s_present; }
bool ethernetLinkUp()  { return s_present && s_link; }

bool ethernetHasIP() {
  if (!s_present || !s_link) return false;
  // IP fixe : l'adresse est valable des que le lien est etabli
  if (s_staticResult == 1) return true;
  return s_gotIP;
}

IPAddress ethernetLocalIP()    { return ethernetHasIP() ? IPAddress((uint32_t)s_ip) : IPAddress(); }
IPAddress ethernetSubnetMask() { return ethernetHasIP() ? IPAddress((uint32_t)s_mask) : IPAddress(); }
IPAddress ethernetGatewayIP()  { return ethernetHasIP() ? IPAddress((uint32_t)s_gw) : IPAddress(); }

IPAddress ethernetDnsIP() {
  if (!s_netif) return IPAddress();
  esp_netif_dns_info_t d;
  if (esp_netif_get_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &d) != ESP_OK) return IPAddress();
  return IPAddress(d.ip.u_addr.ip4.addr);
}

String ethernetMacAddress() {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5]);
  return String(buf);
}

int ethernetLinkSpeed() {
  if (!ethernetLinkUp()) return 0;
  eth_speed_t speed;
  if (esp_eth_ioctl(s_eth, ETH_CMD_G_SPEED, &speed) != ESP_OK) return 0;
  return speed == ETH_SPEED_100M ? 100 : 10;
}

bool ethernetFullDuplex() {
  if (!ethernetLinkUp()) return false;
  eth_duplex_t duplex;
  if (esp_eth_ioctl(s_eth, ETH_CMD_G_DUPLEX_MODE, &duplex) != ESP_OK) return false;
  return duplex == ETH_DUPLEX_FULL;
}

// ---------------------------------------------------------------- reseau

bool netConnected() {
  return ethernetHasIP() || WiFi.isConnected();
}

IPAddress netLocalIP() {
  if (ethernetHasIP()) return ethernetLocalIP();
  return WiFi.localIP();
}

const char* netInterfaceName() {
  if (ethernetHasIP()) return "ethernet";
  if (WiFi.isConnected()) return "wifi";
  return "";
}
