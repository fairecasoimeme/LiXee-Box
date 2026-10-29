// ============================================================================
// ZiGate Driver - Communication UART avec le module ZiGate+
// ============================================================================

#include "zigate_driver.h"
#include "zigate_ota.h"
#include "zigate_ir.h"
#include "zigate_protocol.h"
#include "zigate_console.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ZIGATE_DRV";

// --- Callbacks (non-static: accedes par zigate_protocol.cpp via extern) ---
zigate_attribute_cb_t             s_attr_cb = NULL;
zigate_device_announce_cb_t       s_announce_cb = NULL;
zigate_device_leave_cb_t          s_leave_cb = NULL;
zigate_device_discovery_done_cb_t s_discovery_done_cb = NULL;

// Etat du coordinateur : defini dans zigate_protocol.cpp, qui le renseigne
// a partir des reponses ; le pilote y note les commandes emises.
extern zigate_coord_info_t g_zigate_coord;

// --- Tache reception UART ---
static TaskHandle_t s_rx_task_handle = NULL;
static SemaphoreHandle_t s_tx_mutex = NULL;

// --- Tache de traitement des trames ---
// La reception et le traitement sont separes : le traitement (callbacks vers
// Matter, device_data, MQTT...) peut attendre le verrou de la pile CHIP,
// tenu longtemps pendant un commissioning. Fait dans la tache de reception,
// cette attente laissait deborder le tampon UART et perdre des trames.
#define ZIGATE_PROC_QUEUE_LEN  32
static TaskHandle_t  s_proc_task_handle = NULL;
static QueueHandle_t s_proc_queue = NULL;
static uint32_t      s_proc_dropped = 0;

// Element special de la file : appel d'une fonction sur la tache de
// traitement (zigate_run_on_proc). Reconnu par une longueur impossible pour
// une trame decodee (zigate_decode_packet refuse toute longueur > payload[]),
// donc aucune trame recue ne peut etre prise pour un appel.
#define ZIGATE_PROC_CALL_CMD   0xFFFF
#define ZIGATE_PROC_CALL_LEN   0xFFFF
typedef struct {
    void (*fn)(void *);
    void *arg;
} zigate_proc_call_t;

// --- Emission ---
// Correction (pile) : zigate_send_command gardait un paquet de ~528 octets sur
// la pile et zigate_send_packet y ajoutait encoded[2048], puis la console sa
// ligne : ~2,8 Ko pris sur la pile de chaque appelant (taches HTTP, regles,
// poll...). Paquet et tampon d'encodage sont desormais statiques (en PSRAM :
// lus et ecrits par des taches seulement, jamais par une interruption) et
// utilises uniquement sous s_tx_mutex, qui serialise deja l'ecriture UART.
EXT_RAM_BSS_ATTR static zigate_packet_t s_tx_pkt;
EXT_RAM_BSS_ATTR static uint8_t         s_tx_encoded[ZIGATE_BUF_SIZE];

// Buffer de reception pour le framing
static uint8_t s_rx_frame[ZIGATE_BUF_SIZE];
static size_t  s_rx_frame_pos = 0;
static bool    s_rx_in_frame = false;

// ============================================================================
// Traitement des trames - Tache FreeRTOS
// ============================================================================

static void zigate_proc_task(void *arg)
{
    // Statique : ~520 octets, inutile de les prendre sur la pile
    static zigate_packet_t pkt;

    while (1) {
        if (xQueueReceive(s_proc_queue, &pkt, portMAX_DELAY) == pdTRUE) {
            if (pkt.cmd == ZIGATE_PROC_CALL_CMD && pkt.length == ZIGATE_PROC_CALL_LEN) {
                // Appel poste par zigate_run_on_proc()
                zigate_proc_call_t call;
                memcpy(&call, pkt.payload, sizeof(call));
                if (call.fn) call.fn(call.arg);
                continue;
            }
            zigate_dispatch_packet(&pkt);
        }
    }
}

// ============================================================================
// Reception UART - Tache FreeRTOS
// ============================================================================

static void zigate_rx_task(void *arg)
{
    uint8_t buf[256];

    while (1) {
        int len = uart_read_bytes(ZIGATE_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(50));
        if (len <= 0) continue;

        for (int i = 0; i < len; i++) {
            uint8_t c = buf[i];

            if (c == ZIGATE_MSG_START) {
                // Debut d'une nouvelle trame
                s_rx_frame_pos = 0;
                s_rx_in_frame = true;
                continue;
            }

            if (c == ZIGATE_MSG_END && s_rx_in_frame) {
                // Fin de trame: decoder et dispatcher
                s_rx_in_frame = false;
                if (s_rx_frame_pos > 4) {
                    static zigate_packet_t pkt;   // tache unique : statique sans risque
                    esp_err_t ret = zigate_decode_packet(s_rx_frame, s_rx_frame_pos, &pkt);
                    if (ret == ESP_OK) {
                        // Ne jamais bloquer ici : si le traitement est en
                        // retard au point de remplir la file, perdre cette
                        // trame entiere vaut mieux que des octets au milieu
                        // des suivantes.
                        if (xQueueSend(s_proc_queue, &pkt, 0) != pdTRUE) {
                            s_proc_dropped++;
                            ESP_LOGW(TAG, "File de traitement pleine : trame 0x%04X perdue (%lu au total)",
                                     pkt.cmd, (unsigned long)s_proc_dropped);
                        }
                    } else {
                        ESP_LOGW(TAG, "Decode error (CRC?), dropping frame");
                    }
                }
                s_rx_frame_pos = 0;
                continue;
            }

            if (s_rx_in_frame && s_rx_frame_pos < sizeof(s_rx_frame)) {
                s_rx_frame[s_rx_frame_pos++] = c;
            }
        }
    }
}

// ============================================================================
// API publique
// ============================================================================

esp_err_t zigate_init(void)
{
    ESP_LOGI(TAG, "Initializing ZiGate driver (RX=%d, TX=%d, RST=%d, FLASH=%d)",
             ZIGATE_RXD, ZIGATE_TXD, ZIGATE_RESET_PIN, ZIGATE_FLASH_PIN);
    // Verrous de l'OTA et de l'IR crees avant les taches qui les utilisent
    zigate_ota_prepare();
    zigate_ir_prepare();

    // Config GPIO reset et flash
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << ZIGATE_RESET_PIN) | (1ULL << ZIGATE_FLASH_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Sequence identique au code de reference (ino.cpp):
    // FLASH=HIGH (mode normal, pas flash), puis RESET pulse LOW->HIGH
    gpio_set_level((gpio_num_t)ZIGATE_FLASH_PIN, 1);
    gpio_set_level((gpio_num_t)ZIGATE_RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level((gpio_num_t)ZIGATE_RESET_PIN, 1);

    // Config UART - buffer RX 4096 comme dans le code de reference
    uart_config_t uart_config = {
        .baud_rate = ZIGATE_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };

    ESP_ERROR_CHECK(uart_param_config(ZIGATE_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ZIGATE_UART_NUM, ZIGATE_TXD, ZIGATE_RXD,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(ZIGATE_UART_NUM, 4096, 4096, 0, NULL, 0));

    // Console des trames (tampon en PSRAM), avant tout echange
    zigate_console_init();

    // Mutex pour l'envoi
    s_tx_mutex = xSemaphoreCreateMutex();
    if (!s_tx_mutex) return ESP_ERR_NO_MEM;

    // Attendre le boot de la ZiGate apres reset
    ESP_LOGI(TAG, "Waiting for ZiGate boot...");
    vTaskDelay(pdMS_TO_TICKS(1500));

    // File entre reception et traitement (~17 Ko : en PSRAM si possible)
    s_proc_queue = xQueueCreateWithCaps(ZIGATE_PROC_QUEUE_LEN, sizeof(zigate_packet_t),
                                        MALLOC_CAP_SPIRAM);
    if (!s_proc_queue) {
        s_proc_queue = xQueueCreate(ZIGATE_PROC_QUEUE_LEN, sizeof(zigate_packet_t));
    }
    if (!s_proc_queue) return ESP_ERR_NO_MEM;

    // Traitement : les callbacks (Matter, device_data, MQTT...) tournent ici,
    // avec la pile de 8 Ko qu'avait la tache de reception. Pile en RAM
    // interne : cette tache ecrit en flash (zigate_devices_save, device_data).
    // Correction : le resultat de la creation etait ignore ; sans cette tache,
    // la file se remplissait et toute trame recue etait perdue sans bruit.
    if (xTaskCreatePinnedToCore(zigate_proc_task, "zigate_proc", 8192, NULL, 10,
                                &s_proc_task_handle, 1) != pdPASS) {
        ESP_LOGE(TAG, "Cannot create zigate_proc task (RAM interne insuffisante)");
        s_proc_task_handle = NULL;
        vQueueDelete(s_proc_queue);
        s_proc_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    // Reception : priorite haute sur core 1, ne fait que decouper et decoder.
    // zigate_decode_packet() garde un tampon de ZIGATE_BUF_SIZE sur la pile.
    // Pile en PSRAM (la RAM interne manque) : possible car cette tache ne
    // touche jamais a la flash, seule restriction d'une pile en PSRAM.
    if (xTaskCreatePinnedToCoreWithCaps(zigate_rx_task, "zigate_rx", 6144, NULL, 18,
                                        &s_rx_task_handle, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
        if (xTaskCreatePinnedToCore(zigate_rx_task, "zigate_rx", 6144, NULL, 18,
                                    &s_rx_task_handle, 1) != pdPASS) {
            ESP_LOGE(TAG, "Cannot create zigate_rx task");
            s_rx_task_handle = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "ZiGate driver initialized");
    return ESP_OK;
}

esp_err_t zigate_reset(void)
{
    ESP_LOGI(TAG, "Resetting ZiGate...");
    gpio_set_level((gpio_num_t)ZIGATE_RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level((gpio_num_t)ZIGATE_RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(1500));  // Attendre le boot de la ZiGate
    ESP_LOGI(TAG, "ZiGate reset complete");
    return ESP_OK;
}

/** Encode et emet un paquet. s_tx_mutex tenu par l'appelant (tampon statique). */
static esp_err_t send_packet_locked(const zigate_packet_t *pkt)
{
    size_t encoded_len = 0;
    esp_err_t ret = zigate_encode_packet(pkt, s_tx_encoded, sizeof(s_tx_encoded), &encoded_len);
    if (ret != ESP_OK) return ret;

    int written = uart_write_bytes(ZIGATE_UART_NUM, s_tx_encoded, encoded_len);

    g_zigate_coord.frames_tx++;
    // Console appelee verrou tenu : elle n'utilise que son propre verrou
    // (jamais s_tx_mutex), pas d'interblocage possible
    zigate_console_frame('>', pkt);

    return (written == (int)encoded_len) ? ESP_OK : ESP_FAIL;
}

esp_err_t zigate_send_packet(const zigate_packet_t *pkt)
{
    if (!pkt) return ESP_ERR_INVALID_ARG;
    if (!s_tx_mutex) return ESP_ERR_INVALID_STATE;   // zigate_init() pas encore fait

    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    esp_err_t ret = send_packet_locked(pkt);
    xSemaphoreGive(s_tx_mutex);
    return ret;
}

esp_err_t zigate_send_command(uint16_t cmd, const uint8_t *data, uint16_t len)
{
    // Rejeter plutot que de construire un paquet annoncant un payload
    // plus grand que celui reellement copie.
    if (len > sizeof(s_tx_pkt.payload)) {
        ESP_LOGE(TAG, "Command 0x%04X payload too long: %u bytes (max %u)",
                 cmd, (unsigned)len, (unsigned)sizeof(s_tx_pkt.payload));
        return ESP_ERR_INVALID_ARG;
    }
    if (len > 0 && !data) return ESP_ERR_INVALID_ARG;
    if (!s_tx_mutex) return ESP_ERR_INVALID_STATE;

    // Paquet statique (voir s_tx_pkt), rempli et emis sous le verrou d'emission
    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    zigate_packet_t *pkt = &s_tx_pkt;
    pkt->cmd = cmd;
    pkt->length = len;
    pkt->lqi = 0;
    pkt->seq = 0;
    if (len > 0) {
        memcpy(pkt->payload, data, len);
    }
    pkt->checksum = zigate_compute_checksum(cmd, len, pkt->payload);
    esp_err_t ret = send_packet_locked(pkt);
    xSemaphoreGive(s_tx_mutex);
    return ret;
}

esp_err_t zigate_run_on_proc(void (*fn)(void *), void *arg)
{
    if (!fn) return ESP_ERR_INVALID_ARG;

    // Pas de tache de traitement (ZiGate absente, init echouee) : aucune
    // course possible avec elle, appel direct. Depuis la tache elle-meme :
    // appel direct aussi (attendre sa propre file serait un interblocage).
    if (!s_proc_queue || !s_proc_task_handle ||
        xTaskGetCurrentTaskHandle() == s_proc_task_handle) {
        fn(arg);
        return ESP_OK;
    }

    // Element de file construit dans le tas (~520 octets), pas sur la pile
    // de l'appelant (tache HTTP) ; xQueueSend en fait une copie.
    zigate_packet_t *item = (zigate_packet_t *)heap_caps_calloc(1, sizeof(*item), MALLOC_CAP_SPIRAM);
    if (!item) item = (zigate_packet_t *)calloc(1, sizeof(*item));
    if (!item) return ESP_ERR_NO_MEM;
    item->cmd = ZIGATE_PROC_CALL_CMD;
    item->length = ZIGATE_PROC_CALL_LEN;
    zigate_proc_call_t call = { fn, arg };
    memcpy(item->payload, &call, sizeof(call));

    // Attente bornee : la file peut etre momentanement pleine de trames
    BaseType_t ok = xQueueSend(s_proc_queue, item, pdMS_TO_TICKS(2000));
    free(item);
    if (ok != pdTRUE) {
        ESP_LOGW(TAG, "zigate_run_on_proc: file de traitement pleine");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t zigate_get_version(void)
{
    return zigate_send_command(ZIGATE_CMD_GET_VERSION, NULL, 0);
}

esp_err_t zigate_erase_pdm(void)
{
    return zigate_send_command(ZIGATE_CMD_ERASE_PDM, NULL, 0);
}

esp_err_t zigate_start_network(void)
{
    return zigate_send_command(ZIGATE_CMD_START_NETWORK, NULL, 0);
}

esp_err_t zigate_get_network_state(void)
{
    return zigate_send_command(ZIGATE_CMD_GET_NETWORK_STATE, NULL, 0);
}

esp_err_t zigate_permit_join(uint8_t duration_sec)
{
    // Target: 0xFFFC (broadcast routers), interval
    uint8_t data[4];
    data[0] = 0xFF;           // Target addr MSB (broadcast)
    data[1] = 0xFC;           // Target addr LSB
    data[2] = duration_sec;   // Duree
    data[3] = 0x00;           // TC significance
    esp_err_t err = zigate_send_command(ZIGATE_CMD_PERMIT_JOIN, data, 4);
    if (err == ESP_OK) {
        // Echeance memorisee pour l'interface (assistant d'appairage)
        g_zigate_coord.permit_join_until = duration_sec == 0 ? 0
            : (duration_sec == 0xFF ? UINT32_MAX : zigate_uptime_s() + duration_sec);
    }
    return err;
}

esp_err_t zigate_read_attribute(uint16_t short_addr, uint8_t endpoint,
                                 uint16_t cluster, uint16_t attribute)
{
    uint8_t data[14];
    data[0] = 0x02;                          // Addr mode: short
    data[1] = (short_addr >> 8) & 0xFF;
    data[2] = short_addr & 0xFF;
    data[3] = 0x01;                          // Src endpoint
    data[4] = endpoint;                      // Dst endpoint
    data[5] = (cluster >> 8) & 0xFF;
    data[6] = cluster & 0xFF;
    data[7] = 0x00;                          // Direction
    data[8] = 0x00;                          // Manufacturer specific
    data[9] = 0x00;                          // Manufacturer code MSB
    data[10] = 0x00;                         // Manufacturer code LSB
    data[11] = 0x01;                         // Nombre d'attributs
    data[12] = (attribute >> 8) & 0xFF;
    data[13] = attribute & 0xFF;
    return zigate_send_command(ZIGATE_CMD_READ_ATTRIBUTE, data, 14);
}

esp_err_t zigate_read_attribute_mfr(uint16_t short_addr, uint8_t endpoint,
                                     uint16_t cluster, uint16_t attribute,
                                     uint16_t manufacturer_code)
{
    uint8_t data[14];
    data[0] = 0x02;                          // Addr mode: short
    data[1] = (short_addr >> 8) & 0xFF;
    data[2] = short_addr & 0xFF;
    data[3] = 0x01;                          // Src endpoint
    data[4] = endpoint;                      // Dst endpoint
    data[5] = (cluster >> 8) & 0xFF;
    data[6] = cluster & 0xFF;
    data[7] = 0x00;                          // Direction
    data[8] = (manufacturer_code != 0) ? 0x01 : 0x00; // Manufacturer specific flag
    data[9] = (manufacturer_code >> 8) & 0xFF;         // Manufacturer code MSB
    data[10] = manufacturer_code & 0xFF;               // Manufacturer code LSB
    data[11] = 0x01;                         // Nombre d'attributs
    data[12] = (attribute >> 8) & 0xFF;
    data[13] = attribute & 0xFF;
    return zigate_send_command(ZIGATE_CMD_READ_ATTRIBUTE, data, 14);
}

esp_err_t zigate_send_onoff(uint16_t short_addr, uint8_t endpoint, uint8_t action)
{
    // Commande 0x0092 (On/Off sans effet) : 6 octets, l'action en dernier.
    // L'ancienne version envoyait 0x0092 + action sur 5 octets : OFF partait
    // tronque et ON devenait la commande 0x0093 (On/Off temporise), d'ou des
    // actions Matter sans effet. Trame identique a SendOnOffAction (LiXee-Box).
    if (action > 2) return ESP_ERR_INVALID_ARG;
    uint8_t data[6];
    data[0] = 0x02;                          // Addr mode: short
    data[1] = (short_addr >> 8) & 0xFF;
    data[2] = short_addr & 0xFF;
    data[3] = 0x01;                          // Src endpoint
    data[4] = endpoint;                      // Dst endpoint
    data[5] = action;                        // 0=OFF, 1=ON, 2=TOGGLE
    return zigate_send_command(ZIGATE_CMD_ONOFF, data, sizeof(data));
}

esp_err_t zigate_remove_device(uint64_t ieee_addr)
{
    uint8_t data[8];
    for (int i = 0; i < 8; i++) {
        data[7 - i] = (ieee_addr >> (i * 8)) & 0xFF;
    }
    return zigate_send_command(ZIGATE_CMD_REMOVE_DEVICE, data, 8);
}

esp_err_t zigate_request_active_endpoints(uint16_t short_addr)
{
    uint8_t data[2];
    data[0] = (short_addr >> 8) & 0xFF;
    data[1] = short_addr & 0xFF;
    ESP_LOGI(TAG, "Requesting active endpoints for 0x%04X", short_addr);
    return zigate_send_command(ZIGATE_CMD_ACTIVE_ENDPOINT, data, 2);
}

esp_err_t zigate_request_simple_descriptor(uint16_t short_addr, uint8_t endpoint)
{
    uint8_t data[3];
    data[0] = (short_addr >> 8) & 0xFF;
    data[1] = short_addr & 0xFF;
    data[2] = endpoint;
    ESP_LOGI(TAG, "Requesting simple descriptor for 0x%04X ep=%d", short_addr, endpoint);
    return zigate_send_command(ZIGATE_CMD_SIMPLE_DESC, data, 3);
}

esp_err_t zigate_read_basic_attributes(uint16_t short_addr, uint8_t endpoint)
{
    // Lire Manufacturer Name (0x0004) du Basic Cluster (0x0000)
    esp_err_t ret = zigate_read_attribute(short_addr, endpoint, 0x0000, 0x0004);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(200));
    // Lire Model Identifier (0x0005)
    return zigate_read_attribute(short_addr, endpoint, 0x0000, 0x0005);
}

// --- Binding et reporting ---

esp_err_t zigate_bind(uint64_t ieee_addr, uint8_t src_ep,
                       uint16_t cluster, uint64_t dst_ieee, uint8_t dst_ep)
{
    // Declaree jusqu'ici sans implementation : aucun bind n'etait possible.
    // Trame identique a la passerelle Arduino (SendBind).
    uint8_t data[21];
    for (int i = 0; i < 8; i++) data[i] = (uint8_t)(ieee_addr >> (56 - 8 * i));
    data[8]  = src_ep;
    data[9]  = (cluster >> 8) & 0xFF;
    data[10] = cluster & 0xFF;
    data[11] = 0x03;                         // Destination : adresse IEEE
    for (int i = 0; i < 8; i++) data[12 + i] = (uint8_t)(dst_ieee >> (56 - 8 * i));
    data[20] = dst_ep;
    return zigate_send_command(ZIGATE_CMD_BIND, data, sizeof(data));
}

// Taille ZCL du champ "reportable change" pour un type analogique, 0 pour un
// type discret (booleen, bitmap, enum, chaine...) qui n'en a pas.
static int zcl_reportable_change_size(uint8_t type)
{
    if (type >= 0x20 && type <= 0x27) return type - 0x1F;   // uint8 .. uint64
    if (type >= 0x28 && type <= 0x2F) return type - 0x27;   // int8  .. int64
    switch (type) {
    case 0x38: return 2;                                     // demi-precision
    case 0x39: return 4;                                     // flottant simple
    case 0x3A: return 8;                                     // flottant double
    case 0xE0: case 0xE1: case 0xE2: return 4;               // heure, date, UTC
    default:   return 0;
    }
}

esp_err_t zigate_configure_reporting(uint16_t short_addr, uint8_t endpoint,
                                     uint16_t cluster, uint16_t attribute, uint8_t type,
                                     uint16_t min_interval, uint16_t max_interval,
                                     uint16_t timeout, uint32_t change,
                                     uint16_t manufacturer_code)
{
    // Champ "change" a la taille ZCL du type. La passerelle Arduino
    // l'envoyait sur 1 octet pour tout type autre que uint48/16 bits, donc
    // trop court pour un uint32 (EASD01..04). Pour un type discret, le ZCL
    // ne prevoit pas ce champ : on garde 1 octet a 0 comme l'Arduino, format
    // eprouve avec la ZiGate.
    int csize = zcl_reportable_change_size(type);
    if (csize == 0) csize = 1;

    uint8_t data[22 + 8];
    data[0]  = 0x02;                         // Adressage court
    data[1]  = (short_addr >> 8) & 0xFF;
    data[2]  = short_addr & 0xFF;
    data[3]  = 0x01;                         // Endpoint source (ZiGate)
    data[4]  = endpoint;                     // Endpoint destination
    data[5]  = (cluster >> 8) & 0xFF;
    data[6]  = cluster & 0xFF;
    data[7]  = 0x00;                         // Direction
    data[8]  = manufacturer_code ? 0x01 : 0x00;
    data[9]  = (manufacturer_code >> 8) & 0xFF;
    data[10] = manufacturer_code & 0xFF;
    data[11] = 0x01;                         // Nombre d'attributs
    data[12] = 0x00;                         // Direction de l'attribut
    data[13] = type;
    data[14] = (attribute >> 8) & 0xFF;
    data[15] = attribute & 0xFF;
    data[16] = (min_interval >> 8) & 0xFF;
    data[17] = min_interval & 0xFF;
    data[18] = (max_interval >> 8) & 0xFF;
    data[19] = max_interval & 0xFF;
    data[20] = (timeout >> 8) & 0xFF;
    data[21] = timeout & 0xFF;
    // Big-endian sur csize octets
    uint64_t c = change;
    for (int i = 0; i < csize; i++) {
        data[22 + i] = (uint8_t)(c >> (8 * (csize - 1 - i)));
    }
    return zigate_send_command(ZIGATE_CMD_CONFIG_REPORT, data, (uint16_t)(22 + csize));
}

// --- Ecriture d'attribut ---

esp_err_t zigate_write_attribute_mfr(uint16_t short_addr, uint8_t endpoint,
                                      uint16_t cluster, uint16_t attribute,
                                      uint8_t attr_type, const uint8_t *value, uint16_t value_len,
                                      uint16_t manufacturer_code)
{
    // Declaree depuis l'origine sans implementation. Meme en-tete que la
    // lecture (0x0100), suivi de l'attribut, de son type et de sa valeur.
    if (!value || value_len == 0 || value_len > 64) return ESP_ERR_INVALID_ARG;
    uint8_t data[15 + 64];
    data[0]  = 0x02;                         // Adressage court
    data[1]  = (short_addr >> 8) & 0xFF;
    data[2]  = short_addr & 0xFF;
    data[3]  = 0x01;                         // Endpoint source
    data[4]  = endpoint;
    data[5]  = (cluster >> 8) & 0xFF;
    data[6]  = cluster & 0xFF;
    data[7]  = 0x00;                         // Direction
    data[8]  = manufacturer_code ? 0x01 : 0x00;
    data[9]  = (manufacturer_code >> 8) & 0xFF;
    data[10] = manufacturer_code & 0xFF;
    data[11] = 0x01;                         // Nombre d'attributs
    data[12] = (attribute >> 8) & 0xFF;
    data[13] = attribute & 0xFF;
    data[14] = attr_type;
    memcpy(&data[15], value, value_len);
    return zigate_send_command(ZIGATE_CMD_WRITE_ATTRIBUTE, data, (uint16_t)(15 + value_len));
}

esp_err_t zigate_write_attribute(uint16_t short_addr, uint8_t endpoint,
                                  uint16_t cluster, uint16_t attribute,
                                  uint8_t attr_type, const uint8_t *value, uint16_t value_len)
{
    return zigate_write_attribute_mfr(short_addr, endpoint, cluster, attribute,
                                      attr_type, value, value_len, 0);
}

// --- Commandes de cluster ---

esp_err_t zigate_send_window_covering(uint16_t short_addr, uint8_t endpoint, uint8_t cmd,
                                      const uint8_t *extra, uint8_t extra_len)
{
    if (extra_len > 8 || (extra_len && !extra)) return ESP_ERR_INVALID_ARG;
    uint8_t data[6 + 8];
    data[0] = 0x02;
    data[1] = (short_addr >> 8) & 0xFF;
    data[2] = short_addr & 0xFF;
    data[3] = 0x01;
    data[4] = endpoint;
    data[5] = cmd;
    if (extra_len) memcpy(&data[6], extra, extra_len);
    return zigate_send_command(ZIGATE_CMD_WINDOW_COVERING, data, (uint16_t)(6 + extra_len));
}

esp_err_t zigate_send_raw_aps(uint16_t short_addr, uint8_t src_ep, uint8_t dst_ep,
                              uint16_t cluster, uint16_t profile, uint8_t radius,
                              const uint8_t *zcl, uint8_t zcl_len)
{
    if (!zcl || zcl_len == 0 || zcl_len > 100) return ESP_ERR_INVALID_ARG;
    uint8_t data[12 + 100];
    data[0]  = 0x02;                         // Adressage court
    data[1]  = (short_addr >> 8) & 0xFF;
    data[2]  = short_addr & 0xFF;
    data[3]  = src_ep;
    data[4]  = dst_ep;
    data[5]  = (cluster >> 8) & 0xFF;
    data[6]  = cluster & 0xFF;
    data[7]  = (profile >> 8) & 0xFF;
    data[8]  = profile & 0xFF;
    data[9]  = 0x02;                         // Mode de securite
    data[10] = radius;
    data[11] = zcl_len;
    memcpy(&data[12], zcl, zcl_len);
    return zigate_send_command(ZIGATE_CMD_RAW_APS, data, (uint16_t)(12 + zcl_len));
}

// --- Coordinateur ---

esp_err_t zigate_soft_reset(void)
{
    return zigate_send_command(ZIGATE_CMD_RESET, NULL, 0);
}

esp_err_t zigate_set_led(bool on)
{
    uint8_t v = on ? 1 : 0;
    esp_err_t err = zigate_send_command(ZIGATE_CMD_SET_LED, &v, 1);
    if (err == ESP_OK) g_zigate_coord.led = (int8_t)v;
    return err;
}

esp_err_t zigate_set_channel_mask(uint32_t channel_or_mask)
{
    // Numero de canal -> masque d'un seul bit. La LiXee-Box transmettait la
    // saisie telle quelle ; la ZiGate accepte les deux formes, mais un masque
    // explicite evite toute ambiguite.
    uint32_t mask = channel_or_mask;
    if (mask >= 11 && mask <= 26) mask = 1UL << mask;
    mask &= 0x07FFF800UL;                    // canaux 11 a 26 uniquement
    if (mask == 0) return ESP_ERR_INVALID_ARG;

    uint8_t data[4] = { (uint8_t)(mask >> 24), (uint8_t)(mask >> 16),
                        (uint8_t)(mask >> 8), (uint8_t)mask };
    esp_err_t err = zigate_send_command(ZIGATE_CMD_SET_CHANNEL_MASK, data, 4);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(100));
    return zigate_soft_reset();              // le masque n'est applique qu'au redemarrage
}

esp_err_t zigate_set_raw_mode(bool on)
{
    uint8_t v = on ? 1 : 0;
    esp_err_t err = zigate_send_command(ZIGATE_CMD_SET_RAW_MODE, &v, 1);
    if (err == ESP_OK) g_zigate_coord.raw_mode = on;
    return err;
}

esp_err_t zigate_leave_request(uint64_t ieee_addr)
{
    uint8_t data[10];
    for (int i = 0; i < 8; i++) data[i] = (uint8_t)(ieee_addr >> (56 - 8 * i));
    data[8] = 0x00;                          // pas de re-appairage
    data[9] = 0x00;                          // enfants conserves
    return zigate_send_command(ZIGATE_CMD_LEAVE_REQUEST, data, sizeof(data));
}

esp_err_t zigate_clean_ghosts(void)
{
    // La reponse 0x8602 n'est traitee que si une demande est en cours
    g_zigate_coord.ghost_pending = true;
    esp_err_t err = zigate_send_command(ZIGATE_CMD_NWK_RECOVERY_EXTRACT, NULL, 0);
    if (err != ESP_OK) g_zigate_coord.ghost_pending = false;
    return err;
}

uint32_t zigate_uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000LL);
}

// --- Enregistrement callbacks ---
void zigate_register_attribute_cb(zigate_attribute_cb_t cb)       { s_attr_cb = cb; }
void zigate_register_device_announce_cb(zigate_device_announce_cb_t cb) { s_announce_cb = cb; }
void zigate_register_device_leave_cb(zigate_device_leave_cb_t cb) { s_leave_cb = cb; }

zigate_onoff_cmd_cb_t s_onoff_cmd_cb = NULL;
void zigate_register_onoff_cmd_cb(zigate_onoff_cmd_cb_t cb) { s_onoff_cmd_cb = cb; }
void zigate_register_discovery_done_cb(zigate_device_discovery_done_cb_t cb) { s_discovery_done_cb = cb; }
