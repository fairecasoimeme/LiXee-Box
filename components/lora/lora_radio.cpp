// ============================================================================
// Pilote SX1280/SX1281 minimal - voir lora_radio.h
//
// Differences avec la LiXee-Box : RadioLib n'existe pas sous ESP-IDF, les
// commandes du SX128x sont donc envoyees directement (datasheet SX1280 rev 3.2,
// chap. 11). Les valeurs reproduisent celles que RadioLib envoyait avec
// radio.begin(f, 406.25, sf, 5, 0x12, 10, 8), setCRC(2), explicitHeader(),
// invertIQ(false) : un emetteur ZLinky LoRa ne doit voir aucune difference.
//
// Bus SPI2 (FSPI) sans DMA : les tampons DMA devraient etre en RAM interne et
// alignes, alors qu'une transaction sans DMA est limitee a 64 octets. Le NSS
// est donc pilote a la main, ce qui permet de decouper une commande longue
// (ReadBuffer/WriteBuffer d'un bloc OTA, 70 octets) en plusieurs transactions
// sans relacher le NSS.
// ============================================================================

#include "lora_radio.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "LORA_SX";

#define LORA_SPI_HOST       SPI2_HOST
#define SX_SPI_CHUNK        64          // SOC_SPI_MAXIMUM_BUFFER_SIZE sans DMA

// --- Commandes SX128x ---
#define SX_CMD_SET_STANDBY          0x80
#define SX_CMD_SET_PACKET_TYPE      0x8A
#define SX_CMD_SET_RF_FREQUENCY     0x86
#define SX_CMD_SET_TX_PARAMS        0x8E
#define SX_CMD_SET_BUFFER_BASE      0x8F
#define SX_CMD_SET_MODULATION       0x8B
#define SX_CMD_SET_PACKET_PARAMS    0x8C
#define SX_CMD_SET_DIO_IRQ_PARAMS   0x8D
#define SX_CMD_GET_IRQ_STATUS       0x15
#define SX_CMD_CLR_IRQ_STATUS       0x97
#define SX_CMD_GET_RX_BUFFER_STATUS 0x17
#define SX_CMD_GET_PACKET_STATUS    0x1D
#define SX_CMD_SET_TX               0x83
#define SX_CMD_SET_RX               0x82
#define SX_CMD_WRITE_REGISTER       0x18
#define SX_CMD_READ_REGISTER        0x19
#define SX_CMD_WRITE_BUFFER         0x1A
#define SX_CMD_READ_BUFFER          0x1B

// --- Registres ---
#define SX_REG_FW_VERSION           0x0153
#define SX_REG_LORA_SF_CONFIG       0x0925
#define SX_REG_FREQ_ERR_CORRECTION  0x093C
#define SX_REG_LORA_SYNC_WORD       0x0944

// --- IRQ ---
#define SX_IRQ_TX_DONE              0x0001
#define SX_IRQ_RX_DONE              0x0002
#define SX_IRQ_HEADER_ERROR         0x0020
#define SX_IRQ_CRC_ERROR            0x0040
#define SX_IRQ_RX_TX_TIMEOUT        0x4000
#define SX_IRQ_ALL                  0xFFFF
#define SX_IRQ_RX_MASK  (SX_IRQ_RX_DONE | SX_IRQ_HEADER_ERROR | SX_IRQ_CRC_ERROR | SX_IRQ_RX_TX_TIMEOUT)
#define SX_IRQ_TX_MASK  (SX_IRQ_TX_DONE | SX_IRQ_RX_TX_TIMEOUT)

// --- Parametres LoRa ---
#define SX_PACKET_TYPE_LORA         0x01
#define SX_LORA_BW_406              0x26    // 406,25 kHz
#define SX_LORA_CR_4_5              0x01
#define SX_LORA_HEADER_EXPLICIT     0x00
#define SX_LORA_CRC_ON              0x20
#define SX_LORA_IQ_STANDARD         0x40
#define SX_PA_RAMP_10_US            0x80
#define SX_PERIOD_BASE_1_MS         0x02

#define SX_BUSY_TIMEOUT_MS          100
#define SX_TX_TIMEOUT_MS            4000    // SF12, 70 octets : ~0,9 s d'antenne

static spi_device_handle_t s_dev = NULL;
static bool    s_bus_ok   = false;
static uint8_t s_sf_code  = 0xB0;           // SF11
static uint8_t s_pre_code = 0x14;           // 8 symboles = 4 x 2^1
static bool    s_isr_ok   = false;

// ----------------------------------------------------------------------------
// SPI bas niveau
// ----------------------------------------------------------------------------

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000ULL); }

// BUSY haut = le SX128x traite la commande precedente : aucune commande avant
// qu'il retombe. Il ne dure que quelques microsecondes, sauf apres un reset ou
// un changement de mode : au-dela de 200 us on cede le CPU.
static bool wait_busy(uint32_t timeout_ms)
{
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level((gpio_num_t)LORA_PIN_BUSY)) {
        int64_t el = esp_timer_get_time() - t0;
        if (el > (int64_t)timeout_ms * 1000) return false;
        if (el > 200) vTaskDelay(1);
    }
    return true;
}

static esp_err_t xfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    if (!wait_busy(SX_BUSY_TIMEOUT_MS)) return ESP_ERR_TIMEOUT;
    gpio_set_level((gpio_num_t)LORA_PIN_NSS, 0);
    esp_err_t err = ESP_OK;
    size_t off = 0;
    while (off < len && err == ESP_OK) {
        size_t n = len - off;
        if (n > SX_SPI_CHUNK) n = SX_SPI_CHUNK;
        spi_transaction_t t = {};
        t.length = n * 8;
        t.tx_buffer = tx + off;
        t.rx_buffer = rx ? rx + off : NULL;
        err = spi_device_polling_transmit(s_dev, &t);
        off += n;
    }
    gpio_set_level((gpio_num_t)LORA_PIN_NSS, 1);
    return err;
}

static esp_err_t cmd(uint8_t op, const uint8_t *p, size_t n)
{
    uint8_t tx[1 + 8];
    if (n > sizeof(tx) - 1) return ESP_ERR_INVALID_SIZE;
    tx[0] = op;
    if (n) memcpy(&tx[1], p, n);
    return xfer(tx, NULL, n + 1);
}

// Commande de lecture : opcode, NOP (renvoie le statut), puis n octets.
static esp_err_t cmd_read(uint8_t op, uint8_t *out, size_t n)
{
    uint8_t tx[2 + 8] = {0};
    uint8_t rx[2 + 8] = {0};
    if (n > sizeof(tx) - 2) return ESP_ERR_INVALID_SIZE;
    tx[0] = op;
    esp_err_t err = xfer(tx, rx, n + 2);
    if (err == ESP_OK) memcpy(out, &rx[2], n);
    return err;
}

static esp_err_t write_reg(uint16_t addr, const uint8_t *d, size_t n)
{
    uint8_t tx[3 + 4];
    if (n > sizeof(tx) - 3) return ESP_ERR_INVALID_SIZE;
    tx[0] = SX_CMD_WRITE_REGISTER;
    tx[1] = (uint8_t)(addr >> 8);
    tx[2] = (uint8_t)(addr & 0xFF);
    memcpy(&tx[3], d, n);
    return xfer(tx, NULL, n + 3);
}

static esp_err_t read_reg(uint16_t addr, uint8_t *d, size_t n)
{
    uint8_t tx[4 + 4] = {0};
    uint8_t rx[4 + 4] = {0};
    if (n > sizeof(tx) - 4) return ESP_ERR_INVALID_SIZE;
    tx[0] = SX_CMD_READ_REGISTER;
    tx[1] = (uint8_t)(addr >> 8);
    tx[2] = (uint8_t)(addr & 0xFF);
    esp_err_t err = xfer(tx, rx, n + 4);
    if (err == ESP_OK) memcpy(d, &rx[4], n);
    return err;
}

static esp_err_t write_buf(uint8_t off, const uint8_t *d, size_t n)
{
    uint8_t tx[2 + SX_MAX_PAYLOAD];
    if (n > SX_MAX_PAYLOAD) return ESP_ERR_INVALID_SIZE;
    tx[0] = SX_CMD_WRITE_BUFFER;
    tx[1] = off;
    memcpy(&tx[2], d, n);
    return xfer(tx, NULL, n + 2);
}

static esp_err_t read_buf(uint8_t off, uint8_t *d, size_t n)
{
    uint8_t tx[3 + SX_MAX_PAYLOAD] = {0};
    uint8_t rx[3 + SX_MAX_PAYLOAD] = {0};
    if (n > SX_MAX_PAYLOAD) return ESP_ERR_INVALID_SIZE;
    tx[0] = SX_CMD_READ_BUFFER;
    tx[1] = off;
    esp_err_t err = xfer(tx, rx, n + 3);
    if (err == ESP_OK) memcpy(d, &rx[3], n);
    return err;
}

// ----------------------------------------------------------------------------
// Commandes radio
// ----------------------------------------------------------------------------

static esp_err_t set_standby_rc(void)
{
    uint8_t p = 0x00;                       // STDBY_RC
    return cmd(SX_CMD_SET_STANDBY, &p, 1);
}

static esp_err_t clear_irq(uint16_t mask)
{
    uint8_t p[2] = { (uint8_t)(mask >> 8), (uint8_t)(mask & 0xFF) };
    return cmd(SX_CMD_CLR_IRQ_STATUS, p, 2);
}

static uint16_t get_irq(void)
{
    uint8_t d[2] = {0};
    if (cmd_read(SX_CMD_GET_IRQ_STATUS, d, 2) != ESP_OK) return 0;
    return (uint16_t)((d[0] << 8) | d[1]);
}

static esp_err_t set_dio_irq(uint16_t mask)
{
    // IRQ actives = celles routees sur DIO1 ; DIO2/DIO3 inutilises
    uint8_t p[8] = { (uint8_t)(mask >> 8), (uint8_t)(mask & 0xFF),
                     (uint8_t)(mask >> 8), (uint8_t)(mask & 0xFF), 0, 0, 0, 0 };
    return cmd(SX_CMD_SET_DIO_IRQ_PARAMS, p, 8);
}

static esp_err_t set_buffer_base(void)
{
    uint8_t p[2] = { 0x00, 0x00 };          // TX et RX a l'adresse 0
    return cmd(SX_CMD_SET_BUFFER_BASE, p, 2);
}

static esp_err_t set_packet_params(uint8_t payload_len)
{
    uint8_t p[7] = { s_pre_code, SX_LORA_HEADER_EXPLICIT, payload_len,
                     SX_LORA_CRC_ON, SX_LORA_IQ_STANDARD, 0, 0 };
    return cmd(SX_CMD_SET_PACKET_PARAMS, p, 7);
}

// SetModulationParams, puis les deux registres que le datasheet (sect. 14.4.1)
// impose de mettre a jour selon le SF -- RadioLib fait de meme.
static esp_err_t apply_modulation(void)
{
    uint8_t p[3] = { s_sf_code, SX_LORA_BW_406, SX_LORA_CR_4_5 };
    esp_err_t err = cmd(SX_CMD_SET_MODULATION, p, 3);
    if (err != ESP_OK) return err;

    uint8_t sfcfg;
    if (s_sf_code == 0x50 || s_sf_code == 0x60)      sfcfg = 0x1E;   // SF5/SF6
    else if (s_sf_code == 0x70 || s_sf_code == 0x80) sfcfg = 0x37;   // SF7/SF8
    else                                             sfcfg = 0x32;   // SF9..SF12
    err = write_reg(SX_REG_LORA_SF_CONFIG, &sfcfg, 1);
    if (err != ESP_OK) return err;

    uint8_t fec = 0;
    err = read_reg(SX_REG_FREQ_ERR_CORRECTION, &fec, 1);
    if (err != ESP_OK) return err;
    fec |= 0x01;
    return write_reg(SX_REG_FREQ_ERR_CORRECTION, &fec, 1);
}

// ----------------------------------------------------------------------------
// API
// ----------------------------------------------------------------------------

static void release_bus(void)
{
    if (s_dev) { spi_bus_remove_device(s_dev); s_dev = NULL; }
    if (s_bus_ok) { spi_bus_free(LORA_SPI_HOST); s_bus_ok = false; }
    // Broches rendues (entrees) : une carte sans module ne garde rien de pilote
    gpio_reset_pin((gpio_num_t)LORA_PIN_NSS);
    gpio_reset_pin((gpio_num_t)LORA_PIN_RESET);
    gpio_reset_pin((gpio_num_t)LORA_PIN_BUSY);
    gpio_reset_pin((gpio_num_t)LORA_PIN_DIO1);
    gpio_reset_pin((gpio_num_t)LORA_PIN_SCK);
    gpio_reset_pin((gpio_num_t)LORA_PIN_MOSI);
    gpio_reset_pin((gpio_num_t)LORA_PIN_MISO);
}

bool sx_detect(uint16_t *fw_version)
{
    if (fw_version) *fw_version = 0;

    gpio_config_t out = {};
    out.pin_bit_mask = (1ULL << LORA_PIN_NSS) | (1ULL << LORA_PIN_RESET);
    out.mode = GPIO_MODE_OUTPUT;
    gpio_config(&out);
    gpio_set_level((gpio_num_t)LORA_PIN_NSS, 1);

    // Pull-up sur BUSY : sans module, la broche flottait sur la Box et pouvait
    // passer pour "pret" ; tiree au niveau haut, l'attente expire proprement.
    gpio_config_t in = {};
    in.pin_bit_mask = (1ULL << LORA_PIN_BUSY) | (1ULL << LORA_PIN_DIO1);
    in.mode = GPIO_MODE_INPUT;
    in.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&in);
    gpio_pullup_en((gpio_num_t)LORA_PIN_BUSY);

    spi_bus_config_t bus = {};
    bus.mosi_io_num = LORA_PIN_MOSI;
    bus.miso_io_num = LORA_PIN_MISO;
    bus.sclk_io_num = LORA_PIN_SCK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.data4_io_num = -1;
    bus.data5_io_num = -1;
    bus.data6_io_num = -1;
    bus.data7_io_num = -1;
    bus.max_transfer_sz = SX_SPI_CHUNK;
    esp_err_t err = spi_bus_initialize(LORA_SPI_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Bus SPI2 indisponible (%s) : pas de LoRa", esp_err_to_name(err));
        release_bus();
        return false;
    }
    s_bus_ok = true;
    // MISO tire au niveau haut : sans module on lit 0xFFFF, jamais une valeur
    // flottante prise pour une version
    gpio_pullup_en((gpio_num_t)LORA_PIN_MISO);

    spi_device_interface_config_t dev = {};
    dev.mode = 0;
    dev.clock_speed_hz = LORA_SPI_HZ;
    dev.spics_io_num = -1;                  // NSS pilote a la main (cf. en-tete)
    dev.queue_size = 1;
    err = spi_bus_add_device(LORA_SPI_HOST, &dev, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Ajout du SX128x sur SPI2 impossible: %s", esp_err_to_name(err));
        release_bus();
        return false;
    }

    // Reset materiel (actif bas), comme detectLoRa()
    gpio_set_level((gpio_num_t)LORA_PIN_RESET, 1); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)LORA_PIN_RESET, 0); vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level((gpio_num_t)LORA_PIN_RESET, 1); vTaskDelay(pdMS_TO_TICKS(20));

    uint16_t v = 0;
    bool found = false;
    if (wait_busy(300)) {                   // timeout court : boot non ralenti
        uint8_t d[2] = {0};
        if (read_reg(SX_REG_FW_VERSION, d, 2) == ESP_OK) {
            v = (uint16_t)((d[0] << 8) | d[1]);
            // 0x0000 / 0xFFFF = pas de reponse (MISO au plancher ou au rail)
            found = (v != 0x0000 && v != 0xFFFF);
        }
    }
    if (!found) {
        ESP_LOGI(TAG, "Aucun module LoRa detecte (BUSY=%d, version lue 0x%04X)",
                 gpio_get_level((gpio_num_t)LORA_PIN_BUSY), v);
        release_bus();
        return false;
    }
    ESP_LOGI(TAG, "SX128x detecte (firmware 0x%04X)", v);
    if (fw_version) *fw_version = v;
    return true;
}

esp_err_t sx_begin(uint32_t freq_mhz, uint8_t sf, uint16_t preamble, int8_t power_dbm)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    esp_err_t err = set_standby_rc();
    if (err != ESP_OK) return err;
    uint8_t pt = SX_PACKET_TYPE_LORA;
    if ((err = cmd(SX_CMD_SET_PACKET_TYPE, &pt, 1)) != ESP_OK) return err;
    if ((err = sx_set_frequency(freq_mhz)) != ESP_OK) return err;
    if ((err = set_buffer_base()) != ESP_OK) return err;
    s_sf_code = (uint8_t)((sf < 5 ? 5 : (sf > 12 ? 12 : sf)) << 4);
    if ((err = apply_modulation()) != ESP_OK) return err;
    sx_set_preamble(preamble);
    if ((err = set_packet_params(0xFF)) != ESP_OK) return err;

    // Mot de synchro 0x12 (prive) : registres 0x944/0x945 = 0x14 0x24, avec
    // les bits de controle 0x44 que RadioLib ajoute par defaut.
    uint8_t sync[2] = { (uint8_t)((0x12 & 0xF0) | 0x04), (uint8_t)(((0x12 & 0x0F) << 4) | 0x04) };
    if ((err = write_reg(SX_REG_LORA_SYNC_WORD, sync, 2)) != ESP_OK) return err;

    // Puissance : -18..+13 dBm codee 0..31
    int p = power_dbm + 18;
    if (p < 0) p = 0;
    if (p > 31) p = 31;
    uint8_t txp[2] = { (uint8_t)p, SX_PA_RAMP_10_US };
    if ((err = cmd(SX_CMD_SET_TX_PARAMS, txp, 2)) != ESP_OK) return err;

    if ((err = set_dio_irq(SX_IRQ_RX_MASK)) != ESP_OK) return err;
    return clear_irq(SX_IRQ_ALL);
}

esp_err_t sx_set_frequency(uint32_t freq_mhz)
{
    // Pas de synthese : 52 MHz / 2^18
    uint32_t frf = (uint32_t)(((uint64_t)freq_mhz * 262144ULL * 1000000ULL) / 52000000ULL);
    uint8_t p[3] = { (uint8_t)(frf >> 16), (uint8_t)(frf >> 8), (uint8_t)(frf & 0xFF) };
    return cmd(SX_CMD_SET_RF_FREQUENCY, p, 3);
}

esp_err_t sx_set_sf(uint8_t sf)
{
    if (sf < 5 || sf > 12) return ESP_ERR_INVALID_ARG;
    s_sf_code = (uint8_t)(sf << 4);
    return apply_modulation();
}

void sx_set_preamble(uint16_t symbols)
{
    // Longueur = mantisse x 2^exposant, la premiere >= demande (meme calcul
    // que RadioLib, exposant a partir de 1 : 8 symboles -> 0x14)
    uint32_t len = 0;
    uint8_t e = 1, m = 1;
    for (e = 1; e <= 15; e++) {
        for (m = 1; m <= 15; m++) {
            len = (uint32_t)m << e;
            if (len >= symbols) break;
        }
        if (len >= symbols) break;
    }
    if (e > 15) { e = 15; m = 15; }
    s_pre_code = (uint8_t)((e << 4) | m);
}

esp_err_t sx_standby(void)
{
    esp_err_t err = set_standby_rc();
    if (err != ESP_OK) return err;
    return clear_irq(SX_IRQ_ALL);
}

esp_err_t sx_start_receive(void)
{
    esp_err_t err = set_standby_rc();
    if (err == ESP_OK) err = set_buffer_base();
    if (err == ESP_OK) err = set_packet_params(0xFF);   // en-tete explicite : longueur max
    if (err == ESP_OK) err = set_dio_irq(SX_IRQ_RX_MASK);
    if (err == ESP_OK) err = clear_irq(SX_IRQ_ALL);
    if (err == ESP_OK) {
        uint8_t p[3] = { SX_PERIOD_BASE_1_MS, 0xFF, 0xFF };   // 0xFFFF = reception continue
        err = cmd(SX_CMD_SET_RX, p, 3);
    }
    if (err != ESP_OK) ESP_LOGW(TAG, "Passage en reception impossible: %s", esp_err_to_name(err));
    return err;
}

esp_err_t sx_transmit(const uint8_t *data, uint8_t len, uint32_t *air_ms)
{
    if (air_ms) *air_ms = 0;
    if (!data || len == 0 || len > SX_MAX_PAYLOAD) return ESP_ERR_INVALID_ARG;
    esp_err_t err = set_standby_rc();
    if (err == ESP_OK) err = set_buffer_base();
    if (err == ESP_OK) err = set_packet_params(len);
    if (err == ESP_OK) err = write_buf(0, data, len);
    if (err == ESP_OK) err = set_dio_irq(SX_IRQ_TX_MASK);
    if (err == ESP_OK) err = clear_irq(SX_IRQ_ALL);
    if (err != ESP_OK) return err;

    uint8_t p[3] = { SX_PERIOD_BASE_1_MS, 0x00, 0x00 };      // pas de timeout materiel
    uint32_t t0 = now_ms();
    if ((err = cmd(SX_CMD_SET_TX, p, 3)) != ESP_OK) return err;

    // Attente de TxDone sur DIO1 (scrutation a 1 ms : la tache LoRa est la
    // seule a utiliser la radio, et l'emission doit rester bloquante comme
    // radio.transmit() pour respecter l'alternat du protocole)
    while (!gpio_get_level((gpio_num_t)LORA_PIN_DIO1)) {
        if (now_ms() - t0 > SX_TX_TIMEOUT_MS) break;
        vTaskDelay(1);
    }
    uint16_t irq = get_irq();
    if (air_ms) *air_ms = now_ms() - t0;
    sx_standby();
    return (irq & SX_IRQ_TX_DONE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool sx_irq_pending(void)
{
    return s_dev && gpio_get_level((gpio_num_t)LORA_PIN_DIO1) != 0;
}

int sx_read_packet(uint8_t *buf, int max_len, float *rssi, float *snr)
{
    uint16_t irq = get_irq();
    if (!(irq & SX_IRQ_RX_MASK)) return 0;
    if (!(irq & SX_IRQ_RX_DONE) || (irq & (SX_IRQ_CRC_ERROR | SX_IRQ_HEADER_ERROR))) {
        // Trame abimee (collision, signal faible...) : visible pour le
        // diagnostic des reponses POLL
        ESP_LOGD(TAG, "RX en erreur : irq=0x%04X%s%s", irq,
                 (irq & SX_IRQ_CRC_ERROR) ? " CRC" : "", (irq & SX_IRQ_HEADER_ERROR) ? " en-tete" : "");
        clear_irq(SX_IRQ_ALL);
        return -1;
    }
    uint8_t st[2] = {0};
    if (cmd_read(SX_CMD_GET_RX_BUFFER_STATUS, st, 2) != ESP_OK) { clear_irq(SX_IRQ_ALL); return -1; }
    int len = st[0];
    if (len <= 0 || len > max_len || len > SX_MAX_PAYLOAD) { clear_irq(SX_IRQ_ALL); return -1; }
    if (read_buf(st[1], buf, (size_t)len) != ESP_OK) { clear_irq(SX_IRQ_ALL); return -1; }

    // Statut LoRa : [0] = rssiSync (-dBm x 2), [1] = SNR signe (x 4)
    uint8_t ps[5] = {0};
    if (cmd_read(SX_CMD_GET_PACKET_STATUS, ps, 5) == ESP_OK) {
        float s = (float)(int8_t)ps[1] / 4.0f;
        float r = -(float)ps[0] / 2.0f;
        if (s < 0) r += s;
        if (rssi) *rssi = r;
        if (snr) *snr = s;
    }
    clear_irq(SX_IRQ_ALL);
    return len;
}

esp_err_t sx_attach_isr(void (*fn)(void *), void *arg)
{
    if (s_isr_ok) return ESP_OK;
    // Service deja installe par eth_manager (W5500) : l'IDF le signale, sans
    // consequence
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    gpio_set_intr_type((gpio_num_t)LORA_PIN_DIO1, GPIO_INTR_POSEDGE);
    err = gpio_isr_handler_add((gpio_num_t)LORA_PIN_DIO1, fn, arg);
    if (err == ESP_OK) {
        gpio_intr_enable((gpio_num_t)LORA_PIN_DIO1);
        s_isr_ok = true;
    }
    return err;
}
