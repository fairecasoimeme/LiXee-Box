// ============================================================================
// BLE Provisioning - NimBLE Nordic UART Service (NUS)
// Compatible LiXee-Assist (iOS + Android)
//
// Utilise l'API NimBLE native de ESP-IDF (pas la couche Arduino BLE).
// Sur ESP32-S3, WiFi et BLE coexistent nativement.
// ============================================================================

#include "ble_prov.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_err.h"

#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "BLE_PROV";

// ============================================================================
// UUIDs Nordic UART Service (identiques au legacy pour compatibilite)
// ============================================================================

// NUS Service UUID: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
static const ble_uuid128_t gatt_svr_svc_nus_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);

// RX Characteristic UUID: 6E400002-... (client ecrit ici)
static const ble_uuid128_t gatt_svr_chr_rx_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);

// TX Characteristic UUID: 6E400003-... (device notifie ici)
static const ble_uuid128_t gatt_svr_chr_tx_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E);

// ============================================================================
// Etat interne
// ============================================================================

static bool s_ble_active = false;
static bool s_client_connected = false;
static bool s_tx_subscribed = false;     // true quand le client a active les notifications TX
static bool s_standalone = false;
static uint16_t s_conn_handle = 0;
static uint16_t s_tx_attr_handle = 0;

static ble_prov_credentials_cb_t s_credentials_cb = NULL;
static void *s_credentials_ctx = NULL;

static char s_device_name[20] = "LIXEEBOX-0000";

// Buffer pour les credentials recues — utilise pour deferer le callback
// hors du contexte GATT (sinon vTaskDelay bloque la tache NimBLE host
// et empeche la notification "ok" d'etre transmise)
static char s_pending_ssid[33] = {0};
static char s_pending_pass[65] = {0};
static volatile bool s_credentials_pending = false;

// Pas de welcome messages — LiXee-Assist souscrit aux notifications TX
// juste avant d'envoyer les credentials, puis attend le "ok" comme ACK.
// Envoyer des messages non sollicites brouillerait le protocole ACK.

// ============================================================================
// Prototypes internes
// ============================================================================

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg);
static void ble_on_sync(void);
static void ble_on_reset(int reason);
static void ble_host_task(void *param);
static int gatt_svr_chr_rx_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_chr_tx_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg);
static void start_advertising(void);
static void process_received_data(const char *data, uint16_t len);
static void generate_device_name(void);

// ============================================================================
// Table GATT
// ============================================================================

// Table GATT: ordre des champs = uuid, access_cb, arg, descriptors, flags, min_key_size, val_handle, cpfd
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        // NUS Service
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_nus_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // RX Characteristic (client -> device)
                .uuid = &gatt_svr_chr_rx_uuid.u,
                .access_cb = gatt_svr_chr_rx_cb,
                .arg = NULL,
                .descriptors = NULL,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .min_key_size = 0,
                .val_handle = NULL,
                .cpfd = NULL,
            },
            {
                // TX Characteristic (device -> client, notify)
                .uuid = &gatt_svr_chr_tx_uuid.u,
                .access_cb = gatt_svr_chr_tx_cb,
                .arg = NULL,
                .descriptors = NULL,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .min_key_size = 0,
                .val_handle = &s_tx_attr_handle,
                .cpfd = NULL,
            },
            {
                // Terminateur
                .uuid = NULL,
            }
        },
    },
    {
        // Terminateur
        .type = 0,
    },
};

// ============================================================================
// Generation du nom de device "LIXEEBOX-XXYY" (2 derniers octets MAC)
// ============================================================================

static void generate_device_name(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_name, sizeof(s_device_name), "LIXEEBOX-%02X%02X", mac[4], mac[5]);
}

// ============================================================================
// Callback RX: reception de donnees du client BLE
// ============================================================================

static int gatt_svr_chr_rx_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > 0 && len < 256) {
            char buf[256];
            uint16_t copied = 0;
            int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, &copied);
            if (rc == 0 && copied > 0) {
                buf[copied] = '\0';
                ESP_LOGI(TAG, "RX data (%d bytes): %s", copied, buf);
                process_received_data(buf, copied);
            }
        }
    }
    return 0;
}

// ============================================================================
// Callback TX: lecture de la characteristic TX (optionnel)
// ============================================================================

static int gatt_svr_chr_tx_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        // Valeur initiale identique au legacy (ligne 268 dynamic_ble_manager.cpp)
        const char *msg = "Ready: SSID|PASSWORD";
        os_mbuf_append(ctxt->om, msg, strlen(msg));
    }
    return 0;
}

// ============================================================================
// Traitement des donnees recues
// ============================================================================

static void process_received_data(const char *data, uint16_t len)
{
    // Copie locale pour manipulation
    char buf[256];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, data, len);
    buf[len] = '\0';

    // Trim whitespace
    char *start = buf;
    while (*start == ' ' || *start == '\r' || *start == '\n' || *start == '\t') start++;
    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t')) {
        *end = '\0';
        end--;
    }

    ESP_LOGI(TAG, "Processing: '%s'", start);

    // Commande PING
    if (strcasecmp(start, "PING") == 0) {
        ESP_LOGI(TAG, "PING received");
        ble_prov_send_message("ok");
        return;
    }

    // Commande TEST
    if (strcasecmp(start, "TEST") == 0) {
        ESP_LOGI(TAG, "TEST received");
        ble_prov_send_message("BLE Provisioning OK!");
        return;
    }

    // Commande STATUS
    if (strcasecmp(start, "STATUS") == 0) {
        ESP_LOGI(TAG, "STATUS received");
        char status_msg[128];
        snprintf(status_msg, sizeof(status_msg),
                 "BLE: Active, Device: %s", s_device_name);
        ble_prov_send_message(status_msg);
        return;
    }

    // Parse credentials: "SSID|PASSWORD"
    char *sep = strchr(start, '|');
    if (sep && sep > start && *(sep + 1) != '\0') {
        *sep = '\0';
        const char *ssid = start;
        const char *password = sep + 1;

        // Validation
        if (strlen(ssid) == 0 || strlen(ssid) > 32) {
            ESP_LOGW(TAG, "Invalid SSID length: %d", (int)strlen(ssid));
            ble_prov_send_message("Erreur: SSID invalide");
            return;
        }
        if (strlen(password) > 64) {
            ESP_LOGW(TAG, "Password too long: %d", (int)strlen(password));
            ble_prov_send_message("Erreur: mot de passe trop long");
            return;
        }

        ESP_LOGI(TAG, "Credentials received: SSID='%s' (pass len=%d)",
                 ssid, (int)strlen(password));

        // Repondre "ok" au client (protocole LiXee-Assist)
        ble_prov_send_message("ok");

        // Stocker les credentials — le callback sera appele depuis la boucle
        // principale (wifi_mgr_start) pour ne pas bloquer la tache NimBLE host.
        // Si on appelle le callback ici (contexte GATT), le vTaskDelay dans
        // on_ble_credentials bloquerait NimBLE et empecherait le "ok" d'etre transmis.
        strncpy(s_pending_ssid, ssid, sizeof(s_pending_ssid) - 1);
        strncpy(s_pending_pass, password, sizeof(s_pending_pass) - 1);
        s_credentials_pending = true;
        return;
    }

    // Donnee non reconnue
    ESP_LOGW(TAG, "Unknown command: '%s'", start);
    ble_prov_send_message("Invalid format. Use: SSID|PASSWORD");
    // Petit delai puis message exemple (comme le legacy)
    vTaskDelay(pdMS_TO_TICKS(300));
    ble_prov_send_message("Example: MyWiFi|mypassword123");
}

// ============================================================================
// Advertising BLE
// ============================================================================

static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp_fields;
    int rc;

    memset(&fields, 0, sizeof(fields));
    memset(&rsp_fields, 0, sizeof(rsp_fields));

    // Advertising data: flags + UUID NUS
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t[]) { gatt_svr_svc_nus_uuid };
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return;
    }

    // Scan response: nom du device
    rsp_fields.name = (uint8_t *)s_device_name;
    rsp_fields.name_len = strlen(s_device_name);
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_rsp_set_fields failed: %d", rc);
        return;
    }

    // Parametres advertising: connectable, undirected
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    // Intervalle standard compatible iOS + Android
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(100);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(150);

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                            &adv_params, ble_gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
    } else {
        ESP_LOGI(TAG, "BLE advertising started: %s", s_device_name);
    }
}

// ============================================================================
// GAP event handler
// ============================================================================

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE client %s (handle=%d)",
                 event->connect.status == 0 ? "connected" : "connect failed",
                 event->connect.conn_handle);
        if (event->connect.status == 0) {
            s_client_connected = true;
            s_conn_handle = event->connect.conn_handle;
            s_tx_subscribed = false;
            // PAS de welcome timer ici — on attend le subscribe TX
            // LiXee-Assist fait d'abord service discovery + MTU + subscribe
            // avant d'etre pret a recevoir des notifications
        } else {
            // Echec de connexion, re-advertise
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE client disconnected (reason=%d)",
                 event->disconnect.reason);
        s_client_connected = false;
        s_tx_subscribed = false;
        s_conn_handle = 0;
        // Re-demarrer l'advertising pour accepter un nouveau client
        start_advertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGD(TAG, "Advertising complete");
        // Re-demarrer si pas connecte
        if (!s_client_connected) {
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: conn=%d, mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe event: attr_handle=%d, cur_notify=%d",
                 event->subscribe.attr_handle, event->subscribe.cur_notify);
        // Detecter quand le client active les notifications sur TX
        if (event->subscribe.attr_handle == s_tx_attr_handle &&
            event->subscribe.cur_notify == 1) {
            s_tx_subscribed = true;
            ESP_LOGI(TAG, "TX notifications enabled — ready to send ACK");
            // Pas de welcome message — l'app envoie les credentials immediatement
            // apres le subscribe et attend le "ok" comme ACK
        }
        break;

    default:
        break;
    }

    return 0;
}

// ============================================================================
// NimBLE host callbacks
// ============================================================================

static void ble_on_sync(void)
{
    int rc;

    // Utiliser l'adresse publique
    uint8_t addr_type = 0;
    rc = ble_hs_id_infer_auto(0, &addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }

    uint8_t addr[6];
    ble_hs_id_copy_addr(addr_type, addr, NULL);
    ESP_LOGI(TAG, "BLE addr: %02X:%02X:%02X:%02X:%02X:%02X (type=%d)",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], addr_type);

    // Demarrer advertising
    start_advertising();
}

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host reset, reason=%d", reason);
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ============================================================================
// API publique
// ============================================================================

esp_err_t ble_prov_init(ble_prov_credentials_cb_t cb, void *ctx, bool standalone)
{
    ESP_LOGI(TAG, "Initializing BLE provisioning (standalone=%d)", standalone);

    s_credentials_cb = cb;
    s_credentials_ctx = ctx;
    s_standalone = standalone;

    // Generer le nom du device
    generate_device_name();
    ESP_LOGI(TAG, "Device name: %s", s_device_name);

    if (standalone) {
        // Initialiser NimBLE nous-memes (avant Matter)
        esp_err_t ret = nimble_port_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    // Configurer le host NimBLE
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    // Configurer le nom du device
    ble_svc_gap_device_name_set(s_device_name);

    // Initialiser les services GAP et GATT
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Enregistrer notre table GATT (NUS)
    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return ESP_FAIL;
    }

    if (standalone) {
        // Demarrer la tache NimBLE host
        nimble_port_freertos_init(ble_host_task);
    }

    s_ble_active = true;
    ESP_LOGI(TAG, "BLE provisioning initialized");
    return ESP_OK;
}

esp_err_t ble_prov_start_advertising(void)
{
    if (!s_ble_active) {
        return ESP_ERR_INVALID_STATE;
    }

    start_advertising();
    return ESP_OK;
}

esp_err_t ble_prov_stop(bool full_deinit)
{
    ESP_LOGI(TAG, "Stopping BLE provisioning (full_deinit=%d)", full_deinit);

    if (s_client_connected) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // Arreter advertising
    ble_gap_adv_stop();

    s_ble_active = false;
    s_client_connected = false;
    s_tx_subscribed = false;

    if (full_deinit && s_standalone) {
        int rc = nimble_port_stop();
        if (rc == 0) {
            nimble_port_deinit();
        }
        ESP_LOGI(TAG, "NimBLE stack deinitialized");
    }

    ESP_LOGI(TAG, "BLE provisioning stopped");
    return ESP_OK;
}

esp_err_t ble_prov_send_message(const char *msg)
{
    if (!s_ble_active || !s_client_connected || !s_tx_subscribed || s_tx_attr_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, strlen(msg));
    if (!om) {
        ESP_LOGE(TAG, "Failed to allocate mbuf");
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_attr_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gatts_notify_custom failed: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "TX: %s", msg);
    return ESP_OK;
}

bool ble_prov_is_active(void)
{
    return s_ble_active;
}

bool ble_prov_is_client_connected(void)
{
    return s_client_connected;
}

bool ble_prov_process_pending(void)
{
    if (!s_credentials_pending) return false;
    s_credentials_pending = false;

    // Appeler le callback dans le contexte de la tache appelante
    // (pas dans le contexte NimBLE host)
    if (s_credentials_cb) {
        s_credentials_cb(s_pending_ssid, s_pending_pass, s_credentials_ctx);
    }
    return true;
}
