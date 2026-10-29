// ============================================================================
// WiFi Manager - Gestion WiFi STA avec fallback BLE provisioning
//
// Etat machine:
//   1. Charge credentials NVS
//   2. Si pas de creds -> BLE provisioning immediat
//   3. Si creds -> boucle connexion avec backoff exponentiel
//   4. Apres N echecs -> BLE fallback
//   5. Callback BLE -> save NVS -> esp_restart()
// ============================================================================

#include "wifi_manager.h"
#include "ble_prov.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "WIFI_MGR";

// NVS namespace et cles
#define NVS_NAMESPACE   "config"
#define NVS_KEY_SSID    "wifi_ssid"
#define NVS_KEY_PASS    "wifi_pass"

// Event group bits
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

// ============================================================================
// Etat interne
// ============================================================================

static wifi_mgr_config_t s_config;
static EventGroupHandle_t s_wifi_event_group = NULL;
// Ecrits par la tache d'evenements, lus par la boucle de connexion
static volatile bool s_wifi_connected = false;
static volatile bool s_sta_associated = false;   // associe au point d'acces (IP ou non)
static bool s_ble_active = false;
static bool s_initialized = false;
static bool s_initial_connect_done = false;  // true apres premiere connexion reussie
static int s_retry_count = 0;
static esp_netif_t *s_sta_netif = NULL;
static volatile bool s_eth_ip = false;       // Ethernet a obtenu une adresse (arret du BLE)
static bool s_have_creds = false;            // identifiants NVS charges par wifi_mgr_start()
static volatile bool s_wifi_stopped = false; // coupe volontairement (Ethernet prioritaire)

// IP fixe (configWifi.json), appliquee au demarrage de l'interface STA
static char s_static_ip[16], s_static_mask[16], s_static_gw[16];

void wifi_mgr_set_static_ip(const char *ip, const char *mask, const char *gw)
{
    strlcpy(s_static_ip, ip ? ip : "", sizeof(s_static_ip));
    strlcpy(s_static_mask, mask ? mask : "", sizeof(s_static_mask));
    strlcpy(s_static_gw, gw ? gw : "", sizeof(s_static_gw));
}

static void apply_static_ip(void)
{
    if (!s_sta_netif || !s_static_ip[0]) return;
    esp_netif_ip_info_t info = {};
    if (esp_netif_str_to_ip4(s_static_ip, &info.ip) != ESP_OK ||
        esp_netif_str_to_ip4(s_static_mask, &info.netmask) != ESP_OK ||
        esp_netif_str_to_ip4(s_static_gw, &info.gw) != ESP_OK) {
        ESP_LOGW(TAG, "IP fixe WiFi incomplete ou invalide : DHCP");
        return;
    }
    esp_netif_dhcpc_stop(s_sta_netif);
    if (esp_netif_set_ip_info(s_sta_netif, &info) != ESP_OK) {
        esp_netif_dhcpc_start(s_sta_netif);
        return;
    }
    // DNS : la passerelle (cas le plus courant sur une box)
    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4 = info.gw;
    esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns);
    ESP_LOGI(TAG, "IP fixe WiFi : %s", s_static_ip);
}
static bool s_mdns_initialized = false;
static bool s_mdns_shared = false;    // mDNS partage avec Matter (voir wifi_manager.h)
static bool s_mdns_allowed = true;    // faux tant que Matter n'a pas initialise le mDNS

// ============================================================================
// mDNS: hostname LIXEEBOX-XXYY.local + service HTTP
// LiXee-Assist construit l'URL http://LIXEEBOX-XXYY.local apres provisioning
// ============================================================================

static bool netif_ipv4(esp_netif_t *netif, mdns_ip_addr_t *out)
{
    esp_netif_ip_info_t info;
    if (!netif || esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->addr.type = ESP_IPADDR_TYPE_V4;
    out->addr.u_addr.ip4.addr = info.ip.addr;
    out->next = NULL;
    return true;
}

// Adresse IPv4 courante, pour le nom d'hote delegue : celle de l'interface
// par defaut (l'Ethernet quand il est actif, voir eth_manager), sinon le WiFi.
static bool get_sta_ipv4(mdns_ip_addr_t *out)
{
    return netif_ipv4(esp_netif_get_default_netif(), out) || netif_ipv4(s_sta_netif, out);
}

// Une interface (WiFi ou Ethernet) a-t-elle une adresse IPv4 ?
static bool any_ipv4(void)
{
    mdns_ip_addr_t addr;
    return get_sta_ipv4(&addr);
}

// Mode partage : LIXEEBOX-XXYY.local en nom d'hote delegue (adresse fournie
// par le pont), le nom d'hote principal restant celui de Matter.
static void start_mdns_shared(const char *hostname)
{
    mdns_ip_addr_t addr;
    if (!get_sta_ipv4(&addr)) {
        ESP_LOGW(TAG, "mDNS: pas d'adresse IPv4, nom %s.local non publie", hostname);
        return;
    }

    // Le composant mDNS refuse d'ajouter un service (ESP_ERR_INVALID_ARG) tant
    // qu'aucun nom d'hote principal n'est pose. Matter le pose a sa premiere
    // annonce : deja faite en WiFi, pas encore en Ethernet a ce stade. On pose
    // donc le meme nom que Matter (MAC de la station en hexadecimal), qu'il
    // reprendra a l'identique.
    char main_host[64] = "";
    if (mdns_hostname_get(main_host) != ESP_OK || main_host[0] == '\0') {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(main_host, sizeof(main_host), "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        mdns_hostname_set(main_host);
    }

    esp_err_t ret = mdns_delegate_hostname_add(hostname, &addr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mDNS delegate hostname add failed: %s", esp_err_to_name(ret));
        return;
    }

    // Meme nom d'instance que le nom BLE, attendu par LiXee-Assist.
    // L'ajout du nom delegue est traite de facon asynchrone par la tache
    // mDNS : tant qu'il ne l'est pas, l'ajout du service echoue
    // (ESP_ERR_INVALID_ARG, observe au demarrage en Ethernet). On reessaie.
    for (int i = 0; i < 20; i++) {
        ret = mdns_service_add_for_host(hostname, "_http", "_tcp", hostname, 80, NULL, 0);
        if (ret != ESP_ERR_INVALID_ARG) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "mDNS service add failed: %s", esp_err_to_name(ret));
    }

    s_mdns_initialized = true;
    ESP_LOGI(TAG, "mDNS started: %s.local (partage avec Matter)", hostname);
}

static void start_mdns(void)
{
    // Generer le hostname a partir du MAC (identique au nom BLE)
    char hostname[20];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(hostname, sizeof(hostname), "LIXEEBOX-%02X%02X", mac[4], mac[5]);

    if (!s_mdns_allowed) {
        ESP_LOGI(TAG, "mDNS differe jusqu'au demarrage de Matter");
        return;
    }

    if (s_mdns_initialized) {
        // Nouvelle IP apres une reconnexion : l'adresse du nom delegue suit
        mdns_ip_addr_t addr;
        if (s_mdns_shared && get_sta_ipv4(&addr)) {
            mdns_delegate_hostname_set_address(hostname, &addr);
        }
        return;
    }

    esp_err_t ret = mdns_init();
    if (ret == ESP_ERR_INVALID_STATE && s_mdns_shared) {
        ret = ESP_OK;   // deja initialise par Matter : attendu en mode partage
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(ret));
        return;
    }

    if (s_mdns_shared) {
        start_mdns_shared(hostname);
        return;
    }

    ret = mdns_hostname_set(hostname);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mDNS hostname set failed: %s", esp_err_to_name(ret));
        mdns_free();
        return;
    }

    // Instance name (visible dans les browsers mDNS)
    mdns_instance_name_set(hostname);

    // Annoncer le service HTTP sur port 80
    // Le nom de service DOIT etre le hostname (= nom BLE) pour que
    // LiXee-Assist puisse le trouver via mDNS-SD (_http._tcp.local)
    // L'app compare le PTR record avec le nom BLE (case-insensitive)
    ret = mdns_service_add(hostname, "_http", "_tcp", 80, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "mDNS service add failed: %s", esp_err_to_name(ret));
        // Pas fatal — le hostname fonctionne quand meme
    }

    s_mdns_initialized = true;
    ESP_LOGI(TAG, "mDNS started: %s.local", hostname);
}

// ============================================================================
// Event handler WiFi / IP
// ============================================================================

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi STA started");
            apply_static_ip();
            // Ne pas appeler esp_wifi_connect() ici — c'est try_wifi_connect() qui s'en charge
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected to AP");
            s_sta_associated = true;
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *disc =
                (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "WiFi disconnected (reason=%d)", disc->reason);

            s_wifi_connected = false;
            s_sta_associated = false;
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

            if (s_wifi_stopped) {
                // wifi_mgr_disconnect() : pas de reconnexion
                ESP_LOGI(TAG, "WiFi coupe (Ethernet prioritaire)");
            } else if (s_initial_connect_done) {
                // === Post-connexion : reconnexion illimitee ===
                // Le WiFi a deja fonctionne — on retente toujours
                ESP_LOGI(TAG, "WiFi lost — reconnecting (supervision mode)...");
                esp_wifi_connect();
            } else {
                // === Connexion initiale : retries limites ===
                // Reason 201 = AUTH_FAIL (mauvais mot de passe)
                // Reason 202 = ASSOC_FAIL
                if (disc->reason == WIFI_REASON_AUTH_FAIL ||
                    disc->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) {
                    ESP_LOGW(TAG, "Authentication failed — credentials may be wrong");
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                } else if (s_retry_count < s_config.max_retries) {
                    // Retry automatique
                    s_retry_count++;
                    ESP_LOGI(TAG, "Retrying WiFi connection (%d/%d)...",
                             s_retry_count, s_config.max_retries);
                    esp_wifi_connect();
                } else {
                    ESP_LOGW(TAG, "Max WiFi retries reached");
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                }
            }
            break;
        }

        default:
            break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            s_wifi_connected = true;
            s_initial_connect_done = true;  // Activer supervision post-connexion
            s_retry_count = 0;
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

            // Demarrer mDNS pour que LiXee-Assist puisse acceder au device
            // via http://LIXEEBOX-XXYY.local
            start_mdns();
        } else if (event_id == IP_EVENT_STA_LOST_IP) {
            ESP_LOGW(TAG, "Lost IP address");
            s_wifi_connected = false;
        } else if (event_id == IP_EVENT_ETH_GOT_IP) {
            // Ethernet (eth_manager) : LIXEEBOX-XXYY.local doit aussi y
            // repondre, y compris quand le WiFi n'est pas connecte.
            s_eth_ip = true;
            start_mdns();
        }
    }
}

// ============================================================================
// Callback BLE: credentials recus via LiXee-Assist
// ============================================================================

static void on_ble_credentials(const char *ssid, const char *password, void *ctx)
{
    ESP_LOGI(TAG, "Credentials received via BLE: SSID='%s'", ssid);

    // Sauvegarder en NVS
    esp_err_t err = wifi_mgr_set_credentials(ssid, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save WiFi credentials: %s", esp_err_to_name(err));
        ble_prov_send_message("Erreur: sauvegarde NVS");
        return;
    }

    ESP_LOGI(TAG, "WiFi credentials saved — restarting...");

    // Le "ok" est deja envoye par process_received_data() dans ble_prov.cpp
    // LiXee-Assist recoit le "ok", valide le provisioning, puis deconnecte.
    // On attend juste que la notification BLE soit transmise avant le reboot.
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Reboot pour appliquer les nouveaux credentials
    esp_restart();
}

// ============================================================================
// Chargement credentials NVS
// ============================================================================

static esp_err_t load_credentials(wifi_mgr_credentials_t *creds)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "NVS open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t ssid_len = sizeof(creds->ssid);
    size_t pass_len = sizeof(creds->password);

    ret = nvs_get_str(nvs, NVS_KEY_SSID, creds->ssid, &ssid_len);
    if (ret != ESP_OK) {
        nvs_close(nvs);
        return ret;
    }

    ret = nvs_get_str(nvs, NVS_KEY_PASS, creds->password, &pass_len);
    if (ret != ESP_OK) {
        // Pas de mot de passe = reseau ouvert (valide)
        creds->password[0] = '\0';
    }

    nvs_close(nvs);

    creds->valid = (strlen(creds->ssid) > 0);
    return creds->valid ? ESP_OK : ESP_ERR_NOT_FOUND;
}

// ============================================================================
// Demarrage BLE provisioning
// ============================================================================

static esp_err_t start_ble_provisioning(void)
{
    ESP_LOGI(TAG, "Starting BLE provisioning...");

    // Init BLE en mode standalone (avant Matter)
    esp_err_t ret = ble_prov_init(on_ble_credentials, NULL, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ble_prov_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_ble_active = true;

    char name[20];
    wifi_mgr_get_ble_device_name(name, sizeof(name));
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "  BLE Provisioning active: %s", name);
    ESP_LOGI(TAG, "  Open LiXee-Assist and send: SSID|PASSWORD");
    ESP_LOGI(TAG, "==============================================");

    return ESP_OK;
}

// ============================================================================
// Provisioning BLE en tache de fond : le reste du pont (ZiGate, regles,
// delestage, Matter, page web en Ethernet) demarre quand meme. Avant, un WiFi
// injoignable au demarrage (box Internet plus lente a redemarrer apres une
// coupure de courant) laissait le pont bloque sur le BLE jusqu'au redemarrage
// suivant, Zigbee compris. Le WiFi est retente toutes les 2 minutes ; le BLE
// s'arrete des qu'un reseau est disponible (WiFi ou Ethernet branche apres).
// ============================================================================

#define PROV_RETRY_MS (120 * 1000)

static void prov_task(void *arg)
{
    (void)arg;
    TickType_t last_try = xTaskGetTickCount();
    while (!s_wifi_connected && !s_eth_ip) {
        // Identifiants recus : sauvegarde NVS puis redemarrage (pile interne)
        ble_prov_process_pending();
        if (s_have_creds && !s_sta_associated &&
            xTaskGetTickCount() - last_try >= pdMS_TO_TICKS(PROV_RETRY_MS)) {
            last_try = xTaskGetTickCount();
            ESP_LOGI(TAG, "Nouvel essai de connexion WiFi (provisioning BLE actif)");
            s_retry_count = 0;
            esp_wifi_connect();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "Reseau disponible (%s) : arret du provisioning BLE",
             s_wifi_connected ? "WiFi" : "Ethernet");
    ble_prov_stop(true);
    s_ble_active = false;
    vTaskDelete(NULL);
}

static esp_err_t start_ble_background(void)
{
    esp_err_t ret = start_ble_provisioning();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start BLE provisioning");
        return ret;
    }
    // Pile interne : on_ble_credentials() ecrit en NVS (flash)
    if (xTaskCreate(prov_task, "wifi_prov", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Tache de provisioning impossible");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Waiting for WiFi credentials via BLE (en tache de fond)...");
    return ESP_ERR_TIMEOUT;
}

// ============================================================================
// Tentative de connexion WiFi avec backoff exponentiel
// ============================================================================

static esp_err_t try_wifi_connect(const wifi_mgr_credentials_t *creds)
{
    ESP_LOGI(TAG, "Connecting to WiFi: '%s'", creds->ssid);

    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, creds->ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, creds->password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = strlen(creds->password) > 0 ?
                                           WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    uint32_t backoff_ms = s_config.backoff_base_ms;

    // WIFI_CONNECTED_BIT n'est jamais efface ici : il ne l'est qu'a la perte
    // de la connexion (event handler). L'ancienne boucle l'effacait au debut de
    // chaque tentative : une IP obtenue juste apres l'expiration d'une
    // tentative (DHCP lent) etait alors oubliee, les tentatives suivantes
    // echouaient toutes ("sta is connected") et le pont basculait en
    // provisioning BLE au lieu de demarrer.
    for (int attempt = 1; attempt <= s_config.max_retries; attempt++) {
        if (s_wifi_connected) {
            ESP_LOGI(TAG, "WiFi connected successfully!");
            return ESP_OK;
        }

        s_retry_count = 0;  // Reset le compteur interne de l'event handler
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);

        ESP_LOGI(TAG, "WiFi connection attempt %d/%d...", attempt, s_config.max_retries);
        if (s_sta_associated) {
            // Associe au point d'acces, IP pas encore attribuee : relancer
            // la connexion la couperait. On attend simplement le DHCP.
            ESP_LOGI(TAG, "Associated to AP, waiting for IP address...");
        } else {
            esp_wifi_connect();
        }

        // Attendre resultat
        EventBits_t bits = xEventGroupWaitBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,    // WIFI_CONNECTED_BIT reste pose tant que connecte
            pdFALSE,    // wait for any bit
            pdMS_TO_TICKS(s_config.connect_timeout_ms));

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi connected successfully!");
            return ESP_OK;
        }

        if (bits & WIFI_FAIL_BIT) {
            ESP_LOGW(TAG, "WiFi connection failed (auth or max internal retries)");
        } else {
            ESP_LOGW(TAG, "WiFi connection timeout");
        }

        if (attempt < s_config.max_retries) {
            ESP_LOGI(TAG, "Next attempt in %lu ms...", (unsigned long)backoff_ms);
            // Attente interrompue si l'IP arrive entre-temps
            bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                       pdFALSE, pdFALSE, pdMS_TO_TICKS(backoff_ms));
            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG, "WiFi connected successfully!");
                return ESP_OK;
            }

            // Backoff exponentiel
            backoff_ms = backoff_ms * 2;
            if (backoff_ms > s_config.backoff_max_ms) {
                backoff_ms = s_config.backoff_max_ms;
            }
        }
    }

    ESP_LOGE(TAG, "All WiFi connection attempts failed");
    return ESP_FAIL;
}

// ============================================================================
// API publique
// ============================================================================

esp_err_t wifi_mgr_init(const wifi_mgr_config_t *config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    if (config) {
        s_config = *config;
    } else {
        wifi_mgr_config_t def = WIFI_MGR_CONFIG_DEFAULT();
        s_config = def;
    }

    // Event group pour synchronisation
    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    // Init TCP/IP et event loop (idempotent dans ESP-IDF)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Creer l'interface STA
    s_sta_netif = esp_netif_create_default_wifi_sta();

    // Init WiFi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Enregistrer event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    // Mode STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_initialized = true;
    ESP_LOGI(TAG, "WiFi manager initialized");
    return ESP_OK;
}

esp_err_t wifi_mgr_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized — call wifi_mgr_init first");
        return ESP_ERR_INVALID_STATE;
    }

    // 1. Charger les credentials NVS
    wifi_mgr_credentials_t creds = {};
    esp_err_t ret = load_credentials(&creds);

    if (ret != ESP_OK || !creds.valid) {
        // Pas de credentials -> BLE provisioning immediat (tache de fond)
        ESP_LOGW(TAG, "No WiFi credentials found — starting BLE provisioning");
        return start_ble_background();
    }
    s_have_creds = true;

    // 2. Credentials trouves -> tenter connexion WiFi
    ESP_LOGI(TAG, "WiFi credentials found for SSID: '%s'", creds.ssid);

    ret = try_wifi_connect(&creds);

    if (ret == ESP_OK) {
        // Connecte!
        ESP_LOGI(TAG, "WiFi connection established");
        return ESP_OK;
    }

    // 3. Echec WiFi -> fallback BLE
    ESP_LOGW(TAG, "WiFi connection failed after %d attempts — BLE fallback",
             s_config.max_retries);

    return start_ble_background();
}

esp_err_t wifi_mgr_connect_background(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_wifi_connected || s_sta_associated) return ESP_OK;
    wifi_mgr_credentials_t creds = {};
    if (load_credentials(&creds) != ESP_OK || !creds.valid) return ESP_ERR_NOT_FOUND;
    wifi_config_t wc = {};
    strncpy((char *)wc.sta.ssid, creds.ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, creds.password, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = creds.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (e != ESP_OK) return e;
    s_initial_connect_done = true;           // reconnexion illimitee (supervision)
    s_wifi_stopped = false;
    ESP_LOGI(TAG, "Secours WiFi : connexion a '%s'", creds.ssid);
    return esp_wifi_connect();
}

void wifi_mgr_disconnect(void)
{
    if (!s_initialized) return;
    s_wifi_stopped = true;
    esp_wifi_disconnect();
}

void wifi_mgr_set_mdns_shared(bool shared)
{
    s_mdns_shared = shared;
    s_mdns_allowed = !shared;
}

void wifi_mgr_start_mdns(void)
{
    if (s_mdns_allowed) return;
    s_mdns_allowed = true;
    if (any_ipv4()) {
        start_mdns();
    }
}

bool wifi_mgr_is_connected(void)
{
    return s_wifi_connected;
}

bool wifi_mgr_is_ble_active(void)
{
    return s_ble_active;
}

esp_err_t wifi_mgr_get_credentials(wifi_mgr_credentials_t *creds)
{
    if (!creds) return ESP_ERR_INVALID_ARG;
    return load_credentials(creds);
}

esp_err_t wifi_mgr_set_credentials(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0) return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(nvs, NVS_KEY_SSID, ssid);
    if (ret != ESP_OK) { nvs_close(nvs); return ret; }

    if (password && strlen(password) > 0) {
        ret = nvs_set_str(nvs, NVS_KEY_PASS, password);
    } else {
        ret = nvs_set_str(nvs, NVS_KEY_PASS, "");
    }
    if (ret != ESP_OK) { nvs_close(nvs); return ret; }

    ret = nvs_commit(nvs);
    nvs_close(nvs);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials saved: SSID='%s'", ssid);
    }
    return ret;
}

esp_err_t wifi_mgr_clear_credentials(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) return ret;

    nvs_erase_key(nvs, NVS_KEY_SSID);
    nvs_erase_key(nvs, NVS_KEY_PASS);
    ret = nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "WiFi credentials cleared");
    return ret;
}

esp_err_t wifi_mgr_get_info(char *ssid, size_t ssid_len,
                             char *ip, size_t ip_len,
                             int8_t *rssi)
{
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret != ESP_OK) return ret;

    if (ssid && ssid_len > 0) {
        strncpy(ssid, (char *)ap_info.ssid, ssid_len - 1);
        ssid[ssid_len - 1] = '\0';
    }

    if (rssi) {
        *rssi = ap_info.rssi;
    }

    if (ip && ip_len > 0 && s_sta_netif) {
        esp_netif_ip_info_t ip_info;
        ret = esp_netif_get_ip_info(s_sta_netif, &ip_info);
        if (ret == ESP_OK) {
            snprintf(ip, ip_len, IPSTR, IP2STR(&ip_info.ip));
        } else {
            strncpy(ip, "0.0.0.0", ip_len - 1);
        }
    }

    return ESP_OK;
}

esp_err_t wifi_mgr_get_ble_device_name(char *name, size_t len)
{
    if (!name || len < 16) return ESP_ERR_INVALID_ARG;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(name, len, "LIXEEBOX-%02X%02X", mac[4], mac[5]);
    return ESP_OK;
}

esp_err_t wifi_mgr_get_hostname(char *hostname, size_t len)
{
    if (!hostname || len < 16) return ESP_ERR_INVALID_ARG;

    // Hostname identique au nom BLE
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(hostname, len, "LIXEEBOX-%02X%02X", mac[4], mac[5]);
    return ESP_OK;
}
