// ============================================================================
// Ethernet W5500 - voir eth_manager.h
// Portage ESP-IDF du module ethernet.cpp du firmware LiXee-Box.
// ============================================================================

#include "eth_manager.h"
#include "config_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_eth.h"
#include "esp_eth_mac_spi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

static const char *TAG = "ETH_MGR";

static eth_mgr_config_t s_cfg = { ETH_MODE_AUTO, true, "", "", "", "" };

static esp_eth_handle_t  s_eth = NULL;
static esp_netif_t      *s_netif = NULL;
static esp_eth_mac_t    *s_mac_drv = NULL;
static esp_eth_phy_t    *s_phy_drv = NULL;
static bool              s_present = false;
static bool              s_detected = false;
static bool              s_bus_ready = false;
static volatile bool     s_link = false;
static volatile bool     s_got_ip = false;
static volatile uint32_t s_ip = 0, s_mask = 0, s_gw = 0;
static volatile int      s_static_result = 0;   // IP fixe : 0 = rien, 1 = appliquee, -1 = refusee
static uint8_t           s_mac[6] = {0};

// ----------------------------------------------------------------------------
// Configuration
// ----------------------------------------------------------------------------

static void copy_str(char *dst, size_t len, const cJSON *item)
{
    dst[0] = '\0';
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(dst, item->valuestring, len - 1);
        dst[len - 1] = '\0';
    }
}

esp_err_t eth_mgr_load_config(void)
{
    eth_mgr_config_t def = { ETH_MODE_AUTO, true, "", "", "", "" };
    s_cfg = def;

    FILE *f = fopen(ETH_CONFIG_PATH, "r");
    if (!f) return ESP_ERR_NOT_FOUND;   // pas de fichier : Automatique, DHCP

    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    cJSON *doc = cJSON_Parse(buf);
    if (!doc) return ESP_ERR_INVALID_ARG;

    // "enable" : format de la premiere version LiXee-Box (actif / inactif)
    int mode = ETH_MODE_AUTO;
    const cJSON *m = cJSON_GetObjectItem(doc, "mode");
    if (cJSON_IsNumber(m)) {
        mode = m->valueint;
    } else {
        const cJSON *en = cJSON_GetObjectItem(doc, "enable");
        if (cJSON_IsNumber(en) && en->valueint == 0) mode = ETH_MODE_WIFI_ONLY;
    }
    s_cfg.mode = (mode >= ETH_MODE_AUTO && mode <= ETH_MODE_WIFI_ONLY) ? (uint8_t)mode : ETH_MODE_AUTO;

    const cJSON *d = cJSON_GetObjectItem(doc, "dhcp");
    s_cfg.dhcp = !cJSON_IsNumber(d) || d->valueint != 0;
    copy_str(s_cfg.ip,   sizeof(s_cfg.ip),   cJSON_GetObjectItem(doc, "ip"));
    copy_str(s_cfg.mask, sizeof(s_cfg.mask), cJSON_GetObjectItem(doc, "mask"));
    copy_str(s_cfg.gw,   sizeof(s_cfg.gw),   cJSON_GetObjectItem(doc, "gw"));
    copy_str(s_cfg.dns,  sizeof(s_cfg.dns),  cJSON_GetObjectItem(doc, "dns"));

    cJSON_Delete(doc);
    return ESP_OK;
}

const eth_mgr_config_t *eth_mgr_get_config(void)
{
    return &s_cfg;
}

esp_err_t eth_mgr_save_config(const eth_mgr_config_t *cfg)
{
    if (!cfg || cfg->mode > ETH_MODE_WIFI_ONLY) return ESP_ERR_INVALID_ARG;

    cJSON *doc = cJSON_CreateObject();
    if (!doc) return ESP_ERR_NO_MEM;
    cJSON_AddNumberToObject(doc, "mode", cfg->mode);
    cJSON_AddNumberToObject(doc, "dhcp", cfg->dhcp ? 1 : 0);
    cJSON_AddStringToObject(doc, "ip",   cfg->ip);
    cJSON_AddStringToObject(doc, "mask", cfg->mask);
    cJSON_AddStringToObject(doc, "gw",   cfg->gw);
    cJSON_AddStringToObject(doc, "dns",  cfg->dns);
    char *json = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (!json) return ESP_ERR_NO_MEM;

    esp_err_t err = cfg_write_file(ETH_CONFIG_PATH, json);
    free(json);
    if (err == ESP_OK) s_cfg = *cfg;
    return err;
}

// ----------------------------------------------------------------------------
// Evenements
// ----------------------------------------------------------------------------

// Serveur DNS de l'Ethernet (DHCP, saisi, ou passerelle). Les serveurs DNS de
// lwIP sont communs a toutes les interfaces : la fin du bail DHCP du WiFi
// (coupe quand l'Ethernet revient) les effacait, et plus aucun nom ne se
// resolvait (activation du tunnel, NTP, WebPush : getaddrinfo 202).
static uint32_t s_dns;

// IP fixe, appliquee au demarrage de l'interface (ETHERNET_EVENT_START) :
// avant, l'interface lwIP n'existe pas encore.
static bool apply_static_ip(void)
{
    esp_netif_ip_info_t info = {};
    if (esp_netif_str_to_ip4(s_cfg.ip,   &info.ip)      != ESP_OK ||
        esp_netif_str_to_ip4(s_cfg.mask, &info.netmask) != ESP_OK ||
        esp_netif_str_to_ip4(s_cfg.gw,   &info.gw)      != ESP_OK) {
        return false;   // incomplete ou invalide : on reste en DHCP
    }
    esp_netif_dhcpc_stop(s_netif);
    if (esp_netif_set_ip_info(s_netif, &info) != ESP_OK) {
        esp_netif_dhcpc_start(s_netif);
        return false;
    }
    // DNS : celui saisi, a defaut la passerelle (cas le plus courant sur une box)
    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    if (esp_netif_str_to_ip4(s_cfg.dns, &dns.ip.u_addr.ip4) != ESP_OK) {
        dns.ip.u_addr.ip4 = info.gw;
    }
    esp_netif_set_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &dns);
    s_dns = dns.ip.u_addr.ip4.addr;
    s_ip = info.ip.addr;
    s_mask = info.netmask.addr;
    s_gw = info.gw.addr;
    return true;
}

static void on_eth_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == ETH_EVENT) {
        switch (id) {
        case ETHERNET_EVENT_START:
            if (!s_cfg.dhcp && s_static_result == 0) {
                s_static_result = apply_static_ip() ? 1 : -1;
                if (s_static_result < 0) {
                    ESP_LOGW(TAG, "IP fixe incomplete ou refusee : DHCP utilise");
                }
            }
            break;
        case ETHERNET_EVENT_CONNECTED:
            s_link = true;
            // Adresse IPv6 de lien local : indispensable a Matter, qui
            // fonctionne en IPv6 (la pile ne la cree que pour le WiFi).
            esp_netif_create_ip6_linklocal(s_netif);
            ESP_LOGI(TAG, "Lien Ethernet etabli");
            break;
        case ETHERNET_EVENT_DISCONNECTED:
        case ETHERNET_EVENT_STOP:
            s_link = false;
            s_got_ip = false;
            ESP_LOGW(TAG, "Lien Ethernet perdu");
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        if (e->esp_netif != s_netif) return;
        s_ip   = e->ip_info.ip.addr;
        s_mask = e->ip_info.netmask.addr;
        s_gw   = e->ip_info.gw.addr;
        s_got_ip = true;
        esp_netif_dns_info_t d = {};
        if (esp_netif_get_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &d) == ESP_OK && d.ip.u_addr.ip4.addr) {
            s_dns = d.ip.u_addr.ip4.addr;    // fourni par le DHCP
        } else if (!s_dns) {
            s_dns = s_gw;
        }
        ESP_LOGI(TAG, "Adresse IP Ethernet : " IPSTR " (passerelle " IPSTR ")",
                 IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.gw));
    }
}

// ----------------------------------------------------------------------------
// Demarrage
// ----------------------------------------------------------------------------

static void release_driver(void)
{
    if (s_eth) { esp_eth_driver_uninstall(s_eth); s_eth = NULL; }
    if (s_mac_drv) { s_mac_drv->del(s_mac_drv); s_mac_drv = NULL; }
    if (s_phy_drv) { s_phy_drv->del(s_phy_drv); s_phy_drv = NULL; }
    if (s_bus_ready) { spi_bus_free(SPI3_HOST); s_bus_ready = false; }
}

bool eth_mgr_start(const char *hostname)
{
    eth_mgr_load_config();

    spi_bus_config_t bus = {};
    bus.miso_io_num = ETH_W5500_MISO;
    bus.mosi_io_num = ETH_W5500_MOSI;
    bus.sclk_io_num = ETH_W5500_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGW(TAG, "Bus SPI3 indisponible");
        return false;
    }
    s_bus_ready = true;

    // Service d'interruptions GPIO (broche INT du W5500). Deja installe par
    // un autre module : l'IDF le signale, sans consequence.
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Service d'interruptions GPIO indisponible: %s", esp_err_to_name(err));
        release_driver();
        return false;
    }

    // Le pilote ajoute lui-meme le peripherique sur le bus
    spi_device_interface_config_t dev = {};
    dev.mode = 0;
    dev.clock_speed_hz = ETH_W5500_SPI_MHZ * 1000 * 1000;
    dev.spics_io_num = ETH_W5500_CS;
    dev.queue_size = 20;

    eth_w5500_config_t w5500 = ETH_W5500_DEFAULT_CONFIG(SPI3_HOST, &dev);
    w5500.int_gpio_num = ETH_W5500_INT;
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr = 1;
    phy_cfg.reset_gpio_num = ETH_W5500_RST;

    s_mac_drv = esp_eth_mac_new_w5500(&w5500, &mac_cfg);
    s_phy_drv = esp_eth_phy_new_w5500(&phy_cfg);
    if (!s_mac_drv || !s_phy_drv) {
        ESP_LOGW(TAG, "Creation du pilote W5500 impossible");
        release_driver();
        return false;
    }

    // L'installation lit le registre de version de la puce : elle echoue si
    // aucun W5500 ne repond.
    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(s_mac_drv, s_phy_drv);
    if (esp_eth_driver_install(&eth_cfg, &s_eth) != ESP_OK || !s_eth) {
        ESP_LOGI(TAG, "Aucun W5500 detecte : fonctionnement en WiFi");
        s_eth = NULL;
        release_driver();
        return false;
    }
    s_detected = true;

    if (s_cfg.mode == ETH_MODE_WIFI_ONLY) {
        ESP_LOGI(TAG, "W5500 detecte, non utilise (mode WiFi seul)");
        release_driver();
        return false;
    }

    // Le W5500 n'a pas d'adresse MAC propre : celle reservee a l'Ethernet de l'ESP32
    esp_read_mac(s_mac, ESP_MAC_ETH);
    esp_eth_ioctl(s_eth, ETH_CMD_S_MAC_ADDR, s_mac);

    // Cle "ETH_DEF" (configuration par defaut) : attendue par le mDNS et par
    // Matter pour reconnaitre l'interface Ethernet. Priorite de route au-dessus
    // du WiFi STA (100) pour garder la route par defaut sur l'Ethernet.
    static esp_netif_inherent_config_t inherent = ESP_NETIF_INHERENT_DEFAULT_ETH();
    inherent.route_prio = 150;
    esp_netif_config_t net_cfg = {};
    net_cfg.base = &inherent;
    net_cfg.driver = NULL;
    net_cfg.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
    s_netif = esp_netif_new(&net_cfg);
    if (!s_netif) {
        ESP_LOGE(TAG, "Creation de l'interface reseau impossible");
        release_driver();
        return false;
    }
    if (hostname && hostname[0]) esp_netif_set_hostname(s_netif, hostname);
    esp_netif_attach(s_netif, esp_eth_new_netif_glue(s_eth));

    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &on_eth_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_eth_event, NULL);

    if (esp_eth_start(s_eth) != ESP_OK) {
        ESP_LOGE(TAG, "Demarrage de l'interface impossible");
        return false;
    }
    s_present = true;
    ESP_LOGI(TAG, "W5500 detecte, MAC %02X:%02X:%02X:%02X:%02X:%02X, %s",
             s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5],
             s_cfg.dhcp ? "DHCP" : "IP fixe");
    return true;
}

bool eth_mgr_wait_for_ip(uint32_t link_timeout_ms, uint32_t ip_timeout_ms)
{
    if (!s_present) return false;

    for (uint32_t t = 0; !s_link && t < link_timeout_ms; t += 50) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!s_link) {
        ESP_LOGI(TAG, "Pas de lien (cable debranche ?)");
        return false;
    }
    for (uint32_t t = 0; !eth_mgr_has_ip() && t < ip_timeout_ms; t += 50) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!eth_mgr_has_ip()) {
        ESP_LOGW(TAG, "Lien etabli mais pas d'adresse IP (DHCP sans reponse ?)");
        return false;
    }
    return true;
}

// ----------------------------------------------------------------------------
// Etat
// ----------------------------------------------------------------------------

bool eth_mgr_is_present(void) { return s_present; }

bool eth_mgr_has_ip(void)
{
    if (!s_present || !s_link) return false;
    if (s_static_result == 1) return true;   // IP fixe : valable des que le lien est la
    return s_got_ip;
}

void eth_mgr_ensure_dns(void)
{
    if (!eth_mgr_has_ip() || !s_netif) return;
    uint32_t want = s_dns ? s_dns : s_gw;
    if (!want) return;
    esp_netif_dns_info_t d = {};
    if (esp_netif_get_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &d) == ESP_OK && d.ip.u_addr.ip4.addr) return;
    d.ip.type = ESP_IPADDR_TYPE_V4;
    d.ip.u_addr.ip4.addr = want;
    esp_netif_set_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &d);
    ESP_LOGW(TAG, "Serveur DNS perdu : " IPSTR " remis (Ethernet)", IP2STR(&d.ip.u_addr.ip4));
}

bool eth_mgr_inhibits_wifi(void)
{
    return eth_mgr_has_ip() || (s_present && s_cfg.mode == ETH_MODE_ETH_ONLY);
}

void eth_mgr_get_status(eth_mgr_status_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->detected = s_detected;
    out->present  = s_present;
    out->link_up  = s_present && s_link;
    out->has_ip   = eth_mgr_has_ip();
    memcpy(out->mac, s_mac, sizeof(s_mac));

    if (out->has_ip) {
        out->ip = s_ip;
        out->mask = s_mask;
        out->gw = s_gw;
        esp_netif_dns_info_t d;
        if (s_netif && esp_netif_get_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &d) == ESP_OK) {
            out->dns = d.ip.u_addr.ip4.addr;
        }
    }
    if (out->link_up) {
        eth_speed_t speed;
        if (esp_eth_ioctl(s_eth, ETH_CMD_G_SPEED, &speed) == ESP_OK) {
            out->speed_mbps = (speed == ETH_SPEED_100M) ? 100 : 10;
        }
        eth_duplex_t duplex;
        if (esp_eth_ioctl(s_eth, ETH_CMD_G_DUPLEX_MODE, &duplex) == ESP_OK) {
            out->full_duplex = (duplex == ETH_DUPLEX_FULL);
        }
    }
}
