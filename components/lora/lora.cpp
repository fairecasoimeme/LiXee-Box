// ============================================================================
// LoRa 2.4 GHz - recepteur ZLinky LoRa, voir lora.h
//
// Portage de loraReceiver.cpp (LiXee-Box). La logique radio (appairage,
// dechiffrement, decodage TIC, lecture d'attribut a la demande, identification,
// mise a jour OTA des emetteurs) est reprise a l'identique ; protocole complet :
// doc/LORA_PROTOCOL.md du depot LiXee-Box.
//
// Differences avec la LiXee-Box :
//  - plus de RadioLib (pilote SX128x direct, lora_radio.cpp) ni d'aes128.h
//    (AES par mbedtls, CTR et CMAC RFC 4493 recodes au-dessus de l'ECB) ;
//  - readZigbeeDatas() n'existe pas : chaque valeur est rangee dans
//    device_data (hex, comme main.cpp pour le Zigbee) puis passe par
//    cluster_handlers / historique d'energie / MQTT, ou par le callback
//    enregistre par l'integrateur (lora_register_report_cb) ;
//  - chaines : le pont range les chaines Zigbee SANS octet de longueur (la
//    ZiGate donne la taille a part) ; les champs texte LoRa (STGE, PTEC,
//    LTARF, numero de serie...) suivent ce format, alors que la Box prefixait
//    certains d'une longueur pour ses handlers FF66 ;
//  - le ZLinky LoRa n'est designe compteur Linky que si aucun n'est configure
//    (meme regle que device_setup pour le ZLinky Zigbee ; la Box l'imposait) ;
//  - la tache "lora" remplace loop() : toutes les routes web posent des
//    demandes sous verrou, seule la tache touche la radio.
// ============================================================================

#include "lora.h"
#include "lora_radio.h"

#include "config_manager.h"
#include "device_data.h"
#include "cluster_handlers.h"
#include "energy_history.h"
#include "mqtt_manager.h"
#include "zigate_devices.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/aes.h"
#include "cJSON.h"

static const char *TAG = "LORA";

// ============================================================================
// Constantes du protocole (identiques a loraReceiver.cpp)
// ============================================================================

#define LORA_PAIR_CHANNEL   3       // 2440 MHz : rendez-vous d'appairage
#define LORA_OP_CHANNEL     4       // 2450 MHz : canal de donnees par defaut
#define LORA_PAIR_WINDOW_MS (LORA_PAIR_WINDOW_S * 1000UL)
// SF du rendez-vous : celui du firmware actuel du ZLinky LoRa. L'emetteur doit
// utiliser le MEME, sinon il n'est pas entendu (cf. commentaire de la Box).
#define LORA_PAIR_SF        11
// Preambule (symboles), le meme que le ZLinky LoRa
#define LORA_PREAMBLE       8
#define LORA_TX_POWER_DBM   10

#define T_ESSENTIAL     0x01
#define T_EXTENDED      0x02
#define T_PAIR_REQUEST  0x03
#define T_PAIR_CONFIRM  0x05
#define T_OTA_START     0x06
#define T_OTA_BLOCK     0x07
#define T_OTA_END       0x08
#define T_OTA_ACK       0x09
#define T_POLL_REQUEST  0x0B
#define T_POLL_RESPONSE 0x0C

#define POLL_OK           0x00
#define POLL_UNKNOWN_ATTR 0x01
#define POLL_BAD_REQUEST  0x02

#define MIC_SIZE  2
#define MAC_SIZE  8
#define KEY_SIZE  16

#define SZ_PAIR_REQUEST  11
#define SZ_PAIR_RESPONSE 21
#define SZ_PAIR_CONFIRM  15
#define SZ_ESSENTIAL     17

// Identite de repli : emetteur appaire avant l'identification par POLL, ou
// firmware qui ne connait pas le cluster Basic (forcement un ZLinky)
#define LORA_ZLINKY_DEVICE_ID "81"
#define LORA_ZLINKY_MODEL     "ZLinky_TIC"

#define POLL_CLUSTER_BASIC   0x0000
#define POLL_ATTR_DEVICE_ID  0xFF00
#define POLL_ATTR_MODEL      0x0005
#define POLL_ATTR_FW_VERSION 0xFF01
#define POLL_ATTR_SW_BUILD   0x4000
#define IDENT_RETRY_MS       300000UL
#define POLL_TX_DELAY_MS     20       // emission du POLL a T+20 ms (protocole sect. 8)

// OTA (protocole sect. 10)
#define OTA_BLOCK_SIZE          64
#define OTA_ACK_TIMEOUT_MS      5000UL
#define OTA_MAX_RETRIES         10
#define OTA_START_TRIES         36
#define OTA_WINDOW_DEFAULT      16
#define OTA_WINDOW_MAX          32
#define OTA_WIN_ACK_TIMEOUT_MS  2000UL
#define OTA_BLOCK_GAP_MS        100UL
#define OTA_WIN_MAX_FAILS       10
#define OTA_MAX_RESTARTS        3

#define OTA_STATUS_OK            0x00
#define OTA_STATUS_BUSY          0x01
#define OTA_STATUS_OUT_OF_RANGE  0x04
#define OTA_STATUS_NOT_STARTED   0x07

// Tache radio : pile INTERNE (elle ecrit en flash : lora.json, device_data),
// dimensionnee pour le chemin commun de main.cpp (process_attribute :
// cluster_handler_dispatch, regles, MQTT), Matter et cJSON.
#define LORA_TASK_STACK     7168
#define LORA_TASK_PRIO      6       // au-dessus de httpd (5) : fenetre RX de 300 ms
#define LORA_LOOP_MS        10
#define LORA_LOCK_WAIT_MS   3000

// LED d'appairage de la LiXee-Box (meme LED que le provisioning BLE). -1 pour
// la desactiver si un autre module la pilote.
#ifndef LORA_PIN_LED
#define LORA_PIN_LED        3
#endif

static const uint16_t CH_FREQ[LORA_CHANNEL_COUNT] = {2410, 2420, 2430, 2440, 2450, 2460, 2470, 2480};

// ============================================================================
// Etat
// ============================================================================

typedef struct {
    uint8_t  mac[MAC_SIZE];
    uint8_t  key[KEY_SIZE];
    bool     valid;
    char     device_id[8];      // nomme /littlefs/tp/<device_id>.json
    char     model[24];         // cle dans ce template
    uint8_t  linky_mode;        // bit0 Standard, bit1 triphase, bit2 production
    bool     mode_known;
    uint8_t  tariff_code;       // code tarif unifie (sect. 7.5.1)
    bool     tariff_known;
    bool     label_from_emitter;
    bool     serial_from_emitter;
    uint32_t rx_count;
    uint32_t missed;
    uint8_t  last_seq;
    bool     seq_init;
    float    last_rssi;
    float    last_snr;
    uint32_t last_seen_ms;
    time_t   last_seen_epoch;
    uint16_t short_addr;        // synthetique, unique (affichage)
    // Lecture d'attribut a la demande (sect. 8)
    bool     poll_pending;
    uint16_t poll_cluster;
    uint16_t poll_attr;
    uint8_t  poll_seq;
    uint8_t  poll_retries;
    bool     poll_resp_valid;
    uint8_t  poll_resp_status;
    uint8_t  poll_resp_type;
    uint8_t  poll_resp_len;
    uint8_t  poll_resp_value[24];
    // Identification (cluster Basic) : 0 = fait, 1 = device_id, 2 = model
    uint8_t  ident_step;
    uint32_t ident_next_ms;
    // Version du firmware : 0 = rien, 1 = 0xFF01, 2 = 0x4000
    uint8_t  ver_step;
    uint32_t ver_next_ms;
    uint16_t fw_version;
    bool     fw_known;
    char     version[32];
    // Endpoint Matter de l'appareil ponte (0 = aucun), persiste dans
    // lora.json ("matterEp") : Home Assistant identifie l'appareil par ce numero
    uint16_t matter_ep;
} emitter_t;

static emitter_t s_em[LORA_MAX_EMITTERS];

static SemaphoreHandle_t s_lock = NULL;
static TaskHandle_t      s_task = NULL;
static bool     s_inited     = false;
static bool     s_detected   = false;
static bool     s_ready      = false;
static uint16_t s_chip_ver   = 0;

static uint8_t  s_op_channel = LORA_OP_CHANNEL;
static uint8_t  s_op_sf      = 11;
static uint8_t  s_cur_channel = LORA_OP_CHANNEL;
static uint8_t  s_radio_sf   = 11;

static volatile bool s_pairing_requested = false;
static bool     s_pairing_mode   = false;
static uint32_t s_pairing_start  = 0;
static bool     s_awaiting_confirm = false;
static uint32_t s_confirm_deadline = 0;
static uint8_t  s_pending_mac[MAC_SIZE];
static uint8_t  s_pending_key[KEY_SIZE];
static char     s_pending_device_id[8];
static char     s_pending_model[24];
static uint32_t s_last_pair_rearm = 0;

static uint64_t s_paired_ieee = 0;
static char     s_paired_model[24] = "";
static bool     s_paired_pending = false;

static uint32_t s_last_radio_rx = 0;
static uint32_t s_rx_read_ms = 0;       // lecture de la derniere trame (delai du POLL)

static lora_removed_cb_t s_removed_cb = NULL;
static uint32_t s_last_wd_rearm = 0;

static uint32_t s_led_toggle = 0;
static bool     s_led_state  = false;

static lora_report_cb_t s_report_cb = NULL;

enum { OTA_IDLE = 0, OTA_WAITING, OTA_SENDING, OTA_ENDING, OTA_DONE, OTA_FAILED };

static struct {
    volatile bool requested;
    volatile bool cancel_requested;
    int      req_slot;
    uint16_t req_version;
    uint8_t  req_preamble;
    uint8_t  req_sf;
    uint8_t  req_block_size;
    uint8_t  req_window;
    uint8_t  block_size;
    uint8_t  tx_preamble;
    uint8_t  tx_sf;
    uint8_t  state;
    int      slot;
    FILE    *file;
    uint32_t size;
    uint32_t crc;
    uint16_t version;
    uint16_t total_blocks;
    uint16_t block;             // prochain bloc attendu par l'emetteur
    uint16_t tx_block;          // bloc de la trame en cours d'emission
    uint8_t  seq;
    uint16_t tries;
    uint32_t total_retries;
    bool     waiting_ack;
    uint32_t ack_deadline;
    bool     ack_got;
    uint16_t ack_block;
    uint8_t  ack_status;
    uint8_t  ack_window;
    uint8_t  window;
    uint16_t win_end;
    uint16_t win_next;
    bool     win_waiting;
    uint32_t next_tx_ms;
    uint8_t  win_fails;
    uint8_t  restarts;
    bool     preparing;         // ota_begin() calcule le CRC (fichier ouvert hors verrou)
    uint32_t start_ms;
    uint32_t end_ms;
    char     msg[112];
} s_ota;

// ============================================================================
// Outils
// ============================================================================

static uint32_t millis(void) { return (uint32_t)(esp_timer_get_time() / 1000ULL); }

static bool lock(uint32_t ms)
{
    if (!s_lock) return false;
    TickType_t t = (ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(ms);
    return xSemaphoreTakeRecursive(s_lock, t) == pdTRUE;
}
static void unlock(void) { xSemaphoreGiveRecursive(s_lock); }

static uint64_t mac_to_ieee(const uint8_t *mac)
{
    uint64_t v = 0;
    for (int i = 0; i < MAC_SIZE; i++) v = (v << 8) | mac[i];
    return v;
}

static const char HEXD[] = "0123456789ABCDEF";

static void to_hex(const uint8_t *d, size_t n, char *out, size_t out_len)
{
    size_t o = 0;
    for (size_t i = 0; i < n && o + 2 < out_len; i++) {
        out[o++] = HEXD[d[i] >> 4];
        out[o++] = HEXD[d[i] & 0x0F];
    }
    out[o] = '\0';
}

static void mac_hex(const uint8_t *mac, char out[17]) { to_hex(mac, MAC_SIZE, out, 17); }

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool parse_hex(const char *s, uint8_t *out, size_t n)
{
    if (!s || strlen(s) != n * 2) return false;
    for (size_t i = 0; i < n; i++) {
        int hi = hexval(s[2 * i]), lo = hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static uint16_t getU16(const uint8_t *b) { return (uint16_t)((b[0] << 8) | b[1]); }
static uint32_t getU32(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

static uint8_t clamp_channel(int v) { return (v < 0 || v > 7) ? LORA_OP_CHANNEL : (uint8_t)v; }
static uint8_t clamp_sf(int v)      { return (v < 7 || v > 12) ? 11 : (uint8_t)v; }

static void *psram_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(n);
}

// --- Petit constructeur de JSON en PSRAM -------------------------------------
// Les routes de liste (/api/lora/devices : un template ZLinky = ~60 etats)
// produiraient sinon des centaines de petits noeuds cJSON, alloues en RAM
// interne (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096).

typedef struct { char *buf; size_t len, cap; bool oom; } sb_t;

static void sb_init(sb_t *s, size_t cap)
{
    s->buf = (char *)psram_alloc(cap);
    s->len = 0;
    s->cap = s->buf ? cap : 0;
    s->oom = (s->buf == NULL);
    if (s->buf) s->buf[0] = '\0';
}

static bool sb_reserve(sb_t *s, size_t extra)
{
    if (s->oom) return false;
    if (s->len + extra + 1 <= s->cap) return true;
    size_t nc = s->cap * 2;
    while (nc < s->len + extra + 1) nc *= 2;
    char *nb = (char *)heap_caps_realloc(s->buf, nc, MALLOC_CAP_SPIRAM);
    if (!nb) nb = (char *)realloc(s->buf, nc);
    if (!nb) { s->oom = true; return false; }
    s->buf = nb;
    s->cap = nc;
    return true;
}

static void sb_add(sb_t *s, const char *str)
{
    size_t n = strlen(str);
    if (!sb_reserve(s, n)) return;
    memcpy(s->buf + s->len, str, n + 1);
    s->len += n;
}

static void sb_addf(sb_t *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sb_addf(sb_t *s, const char *fmt, ...)
{
    if (s->oom) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(s->buf + s->len, s->cap - s->len, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= s->cap - s->len) {
        if (!sb_reserve(s, (size_t)n)) return;
        va_start(ap, fmt);
        vsnprintf(s->buf + s->len, s->cap - s->len, fmt, ap);
        va_end(ap);
    }
    s->len += (size_t)n;
}

// Chaine JSON echappee (guillemets compris)
static void sb_str(sb_t *s, const char *str)
{
    sb_add(s, "\"");
    for (const unsigned char *p = (const unsigned char *)(str ? str : ""); *p; p++) {
        if (*p == '"' || *p == '\\') { char e[3] = { '\\', (char)*p, 0 }; sb_add(s, e); }
        else if (*p < 0x20) sb_addf(s, "\\u%04x", (unsigned)*p);
        else { char c[2] = { (char)*p, 0 }; sb_add(s, c); }
    }
    sb_add(s, "\"");
}

static char *sb_take(sb_t *s)
{
    if (s->oom) { free(s->buf); s->buf = NULL; return NULL; }
    return s->buf;
}

// ============================================================================
// Chiffrement : AES-128-CTR et CMAC (RFC 4493), identiques a aes128.h
// ============================================================================

static void aes_ctr(uint8_t *data, size_t len, const uint8_t key[16], const uint8_t nonce[16])
{
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 128);
    uint8_t ctr[16], ks[16];
    memcpy(ctr, nonce, 16);
    for (size_t off = 0; off < len; off += 16) {
        mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, ctr, ks);
        size_t n = (len - off < 16) ? len - off : 16;
        for (size_t i = 0; i < n; i++) data[off + i] ^= ks[i];
        for (int i = 15; i >= 0; i--) if (++ctr[i] != 0) break;   // compteur 128 bits big-endian
    }
    mbedtls_aes_free(&ctx);
}

static void cmac_shift(const uint8_t in[16], uint8_t out[16])
{
    uint8_t carry = 0;
    for (int i = 15; i >= 0; i--) {
        out[i] = (uint8_t)((in[i] << 1) | carry);
        carry = (in[i] >> 7) & 1;
    }
}

static void aes_cmac(const uint8_t *data, size_t len, const uint8_t key[16], uint8_t mac[16])
{
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 128);

    uint8_t zero[16] = {0}, L[16], K1[16], K2[16];
    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, zero, L);
    cmac_shift(L, K1);
    if (L[0] & 0x80) K1[15] ^= 0x87;
    cmac_shift(K1, K2);
    if (K1[0] & 0x80) K2[15] ^= 0x87;

    size_t n = (len + 15) / 16;
    bool complete;
    if (n == 0) { n = 1; complete = false; }
    else complete = (len % 16 == 0);

    uint8_t last[16];
    if (complete) {
        for (int i = 0; i < 16; i++) last[i] = data[(n - 1) * 16 + i] ^ K1[i];
    } else {
        size_t rem = len % 16;
        memset(last, 0, 16);
        if (rem) memcpy(last, &data[(n - 1) * 16], rem);
        last[rem] = 0x80;
        for (int i = 0; i < 16; i++) last[i] ^= K2[i];
    }

    uint8_t X[16] = {0}, Y[16];
    for (size_t b = 0; b + 1 < n; b++) {
        for (int j = 0; j < 16; j++) X[j] ^= data[b * 16 + j];
        mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, X, Y);
        memcpy(X, Y, 16);
    }
    for (int j = 0; j < 16; j++) X[j] ^= last[j];
    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, X, mac);
    mbedtls_aes_free(&ctx);
}

static void aes_ecb(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 128);
    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in, out);
    mbedtls_aes_free(&ctx);
}

// Nonce CTR : MAC(8) + 0x00(7) + seq(1), identique a l'emetteur
static void build_nonce(uint8_t *nonce, const uint8_t *mac, uint8_t seq)
{
    memcpy(nonce, mac, 8);
    memset(&nonce[8], 0, 7);
    nonce[15] = seq;
}

// Dechiffre en place et verifie le MIC. Entree [type][seq][chiffre...][MIC0][MIC1].
static bool decrypt_packet(uint8_t *pkt, int total_len, const uint8_t *key, const uint8_t *mac)
{
    if (total_len < 2 + MIC_SIZE) return false;
    int data_len = total_len - MIC_SIZE;
    uint8_t m0 = pkt[data_len], m1 = pkt[data_len + 1];
    uint8_t nonce[16];
    build_nonce(nonce, mac, pkt[1]);
    int payload_len = data_len - 2;
    if (payload_len > 0) aes_ctr(&pkt[2], (size_t)payload_len, key, nonce);
    uint8_t expected[16];
    aes_cmac(pkt, (size_t)data_len, key, expected);
    return expected[0] == m0 && expected[1] == m1;
}

// Chiffre en place et appose le MIC (2 octets libres requis apres clear_len)
static int encrypt_packet(uint8_t *pkt, int clear_len, const uint8_t *key, const uint8_t *mac)
{
    uint8_t mic[16];
    aes_cmac(pkt, (size_t)clear_len, key, mic);         // 1. MIC sur le clair complet
    uint8_t nonce[16];
    build_nonce(nonce, mac, pkt[1]);
    int payload_len = clear_len - 2;
    if (payload_len > 0) aes_ctr(&pkt[2], (size_t)payload_len, key, nonce);   // 2. chiffre
    pkt[clear_len]     = mic[0];                        // 3. MIC
    pkt[clear_len + 1] = mic[1];
    return clear_len + 2;
}

// ============================================================================
// Radio (tache LoRa uniquement)
// ============================================================================

// standby + effacement des IRQ avant toute reconfiguration (impose par le
// SX128x) : une IRQ residuelle du canal precedent empechait sur la Box un
// re-armement franc en reception.
static void set_channel(uint8_t ch)
{
    sx_standby();
    sx_set_frequency(CH_FREQ[ch]);
    s_cur_channel = ch;
}

static void set_sf(uint8_t sf)
{
    sx_standby();
    sx_set_sf(sf);
    s_radio_sf = sf;
}

static void start_rx(void) { sx_start_receive(); }

static void led_set(bool on)
{
#if LORA_PIN_LED >= 0
    static bool init = false;
    if (!init) {
        gpio_reset_pin((gpio_num_t)LORA_PIN_LED);
        gpio_set_direction((gpio_num_t)LORA_PIN_LED, GPIO_MODE_OUTPUT);
        init = true;
    }
    gpio_set_level((gpio_num_t)LORA_PIN_LED, on ? 1 : 0);
#else
    (void)on;
#endif
}

static void pairing_led_tick(void)
{
    if (millis() - s_led_toggle < 300) return;
    s_led_toggle = millis();
    s_led_state = !s_led_state;
    led_set(s_led_state);
}

static void pairing_led_off(void)
{
    s_led_state = false;
    led_set(false);
}

// ============================================================================
// Persistance : /littlefs/config/lora.json (format LiXee-Box)
// ============================================================================

static char *read_text_file(const char *path, size_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0 || (size_t)n > max) { fclose(f); return NULL; }
    rewind(f);
    char *b = (char *)psram_alloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[got] = '\0';
    return b;
}

static void load_config(void)
{
    memset(s_em, 0, sizeof(s_em));
    char *txt = read_text_file(LORA_CONFIG_PATH, 8192);
    if (!txt) return;
    cJSON *doc = cJSON_Parse(txt);
    free(txt);
    if (!doc) { ESP_LOGW(TAG, LORA_CONFIG_PATH " illisible"); return; }

    // Parametres radio globaux (absents des fichiers d'avant le bidirectionnel)
    const cJSON *ch = cJSON_GetObjectItem(doc, "opChannel");
    const cJSON *sf = cJSON_GetObjectItem(doc, "sf");
    s_op_channel = clamp_channel(cJSON_IsNumber(ch) ? ch->valueint : LORA_OP_CHANNEL);
    s_op_sf      = clamp_sf(cJSON_IsNumber(sf) ? sf->valueint : 11);

    int i = 0;
    const cJSON *o;
    cJSON_ArrayForEach(o, cJSON_GetObjectItem(doc, "emitters")) {
        if (i >= LORA_MAX_EMITTERS) break;
        const cJSON *mac = cJSON_GetObjectItem(o, "mac");
        const cJSON *key = cJSON_GetObjectItem(o, "key");
        if (!cJSON_IsString(mac) || !cJSON_IsString(key)) continue;
        emitter_t *e = &s_em[i];
        if (!parse_hex(mac->valuestring, e->mac, MAC_SIZE) || !parse_hex(key->valuestring, e->key, KEY_SIZE)) {
            memset(e, 0, sizeof(*e));
            continue;
        }
        // Cle absente : identite ZLinky (fichiers d'avant l'identification) ;
        // cle presente mais vide : identification par POLL en attente
        const cJSON *did = cJSON_GetObjectItem(o, "deviceId");
        const cJSON *mod = cJSON_GetObjectItem(o, "model");
        strlcpy(e->device_id, cJSON_IsString(did) ? did->valuestring : LORA_ZLINKY_DEVICE_ID, sizeof(e->device_id));
        strlcpy(e->model, cJSON_IsString(mod) ? mod->valuestring : LORA_ZLINKY_MODEL, sizeof(e->model));
        const cJSON *mep = cJSON_GetObjectItem(o, "matterEp");
        e->matter_ep = (cJSON_IsNumber(mep) && mep->valueint > 1 && mep->valueint < 0xFFFF) ? (uint16_t)mep->valueint : 0;
        e->valid = true;
        i++;
    }
    cJSON_Delete(doc);
    ESP_LOGI(TAG, "%d emetteur(s) appaire(s) charge(s), canal %d, SF%d", i, s_op_channel, s_op_sf);
}

static esp_err_t save_config(void)
{
    cJSON *doc = cJSON_CreateObject();
    if (!doc) return ESP_ERR_NO_MEM;
    cJSON_AddNumberToObject(doc, "opChannel", s_op_channel);
    cJSON_AddNumberToObject(doc, "sf", s_op_sf);
    cJSON *arr = cJSON_AddArrayToObject(doc, "emitters");
    for (int i = 0; i < LORA_MAX_EMITTERS; i++) {
        if (!s_em[i].valid) continue;
        cJSON *o = cJSON_CreateObject();
        char mac[17], key[33];
        mac_hex(s_em[i].mac, mac);
        to_hex(s_em[i].key, KEY_SIZE, key, sizeof(key));
        cJSON_AddStringToObject(o, "mac", mac);
        cJSON_AddStringToObject(o, "deviceId", s_em[i].device_id);
        cJSON_AddStringToObject(o, "model", s_em[i].model);
        cJSON_AddStringToObject(o, "key", key);
        if (s_em[i].matter_ep) cJSON_AddNumberToObject(o, "matterEp", s_em[i].matter_ep);
        cJSON_AddItemToArray(arr, o);
    }
    char *txt = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (!txt) return ESP_ERR_NO_MEM;
    esp_err_t err = cfg_write_file(LORA_CONFIG_PATH, txt);
    free(txt);
    if (err != ESP_OK) ESP_LOGE(TAG, "Ecriture de " LORA_CONFIG_PATH " impossible");
    return err;
}

// ============================================================================
// Injection des valeurs (equivalent de readZigbeeDatas())
// ============================================================================

static void push_value(emitter_t *e, uint16_t cluster, uint16_t attr, uint8_t type,
                       const uint8_t *data, uint16_t len)
{
    uint64_t ieee = mac_to_ieee(e->mac);
    char hex[DEVDATA_VALUE_LEN];
    to_hex(data, len, hex, sizeof(hex));
    device_data_set_value(ieee, cluster, attr, hex);

    if (s_report_cb) {
        lora_report_t r = {};
        r.ieee_addr = ieee;
        r.short_addr = e->short_addr;
        r.endpoint = 1;
        r.cluster_id = cluster;
        r.attr_id = attr;
        r.attr_type = type;
        r.data = data;
        r.data_len = len;
        r.hex_value = hex;
        s_report_cb(&r);
        return;
    }

    // Reste du chemin Zigbee de main.cpp. Statiques : seule la tache LoRa
    // passe ici, et ces deux structures font ~500 octets.
    static cluster_report_t cr;
    static cluster_interpreted_t interp;
    memset(&cr, 0, sizeof(cr));
    memset(&interp, 0, sizeof(interp));
    cr.ieee_addr = ieee;
    cr.short_addr = e->short_addr;
    cr.endpoint = 1;
    cr.cluster_id = cluster;
    cr.attr_id = attr;
    cr.attr_type = type;
    cr.data_len = ((size_t)len > sizeof(cr.data)) ? (uint16_t)sizeof(cr.data) : len;
    memcpy(cr.data, data, cr.data_len);
    cluster_handler_dispatch(&cr, &interp);

    int64_t num = 0;
    bool has_num = zcl_to_int64(data, len, type, &num);
    if (has_num && cluster == 0x0702 &&
        (attr == 0x0000 || attr == 0x0001 || (attr >= 0x0100 && attr <= 0x0112 && !(attr & 1)))) {
        int32_t v = (num > INT32_MAX) ? INT32_MAX : (num < INT32_MIN ? INT32_MIN : (int32_t)num);
        energy_history_add_measurement(ieee, attr, v);
    }

    if (mqtt_manager_is_connected()) {
        // Meme regle que main.cpp (format de la LiXee-Box)
        mqtt_manager_publish_attr(ieee, cluster, attr, hex, type, has_num, num);
    }
}

// Entier big-endian sur 1, 2 ou 4 octets (uint8 / uint16 / uint32)
static void push_num(emitter_t *e, uint16_t cluster, uint16_t attr, uint32_t value, uint8_t nbytes)
{
    uint8_t d[4];
    for (int i = 0; i < nbytes; i++) d[i] = (uint8_t)(value >> (8 * (nbytes - 1 - i)));
    uint8_t type = (nbytes == 1) ? 0x20 : (nbytes == 2) ? 0x21 : 0x23;
    push_value(e, cluster, attr, type, d, nbytes);
}

// Chaine (type 0x42), sans octet de longueur : format des chaines Zigbee du
// pont. Remplace a la fois pushZStr et pushZRawStr de la Box.
static void push_str(emitter_t *e, uint16_t cluster, uint16_t attr, const char *s)
{
    size_t n = strlen(s);
    if (n > 32) n = 32;
    push_value(e, cluster, attr, 0x42, (const uint8_t *)s, (uint16_t)n);
}

// Code tarif unifie (sect. 7.5.1) -> code PTEC de la TIC Historique
static const char *histo_ptec_code(uint8_t c)
{
    switch (c) {
    case 1:  return "TH..";
    case 2:  return "HP..";
    case 3:  return "HC..";
    case 4:  return "HN..";
    case 5:  return "PM..";
    case 6:  return "HCJB";
    case 7:  return "HPJB";
    case 8:  return "HCJW";
    case 9:  return "HPJW";
    case 10: return "HCJR";
    case 11: return "HPJR";
    default: return NULL;
    }
}

static inline bool is_standard(const emitter_t *e) { return (e->linky_mode & 0x01) != 0; }
static inline bool is_tri(const emitter_t *e)      { return (e->linky_mode & 0x02) != 0; }
static inline bool histo_base(const emitter_t *e)  { return e->tariff_known && e->tariff_code == 1; }
static inline bool histo_tempo(const emitter_t *e)
{
    return e->tariff_known && e->tariff_code >= 6 && e->tariff_code <= 11;
}

// Le mode du ZLinky configure comme compteur Energie suit celui de la TIC,
// comme le fait device_setup pour le ZLinky Zigbee
static void sync_cfg_linky_mode(const emitter_t *e)
{
    cfg_general_t *g = cfg_get_general_mut();
    if (!g->zlinky[0] || strtoull(g->zlinky, NULL, 16) != mac_to_ieee(e->mac)) return;
    if (g->linky_mode != (int)e->linky_mode) {
        g->linky_mode = e->linky_mode;
        // Correction : cfg_save_general() appele ici, depuis la tache LoRa,
        // serialisait cfg_general_t pendant que d'autres taches (web) le
        // modifiaient et l'enregistraient. La sauvegarde est demandee a la
        // boucle principale, qui l'effectue elle-meme.
        cfg_request_save_general();
    }
}

// Efface les index Tier1/Tier2 restes d'avant l'identification de l'option
// BASE (sinon l'index BASE resterait affiche en double)
static void clear_stale_tier_values(emitter_t *e)
{
    uint64_t ieee = mac_to_ieee(e->mac);
    bool changed = false;
    static const uint16_t attrs[2] = {256, 258};
    for (int i = 0; i < 2; i++) {
        const char *v = device_data_get_value_ptr(ieee, 0x0702, attrs[i]);
        if (v && v[0]) {
            device_data_set_value(ieee, 0x0702, attrs[i], "");
            changed = true;
        }
    }
    if (changed) {
        device_data_save(ieee);
        ESP_LOGI(TAG, "Option BASE : index tarifaires 256/258 effaces (doublon de BASE/EAST)");
    }
}

// ============================================================================
// Decodage des trames TIC (LORA_PROTOCOL.md sect. 6-7) - repris de la Box
// ============================================================================

static void map_essential(emitter_t *e, const uint8_t *b, int len)
{
    if (len != SZ_ESSENTIAL) return;

    // Le dernier octet porte le mode : seule trame qui le donne toutes les 5 s
    if (!e->mode_known || e->linky_mode != b[16]) {
        e->linky_mode = b[16];
        e->mode_known = true;
        ESP_LOGD(TAG, "Mode Linky = %u (%s, %s%s)", b[16],
                 is_standard(e) ? "Standard" : "Historique",
                 is_tri(e) ? "triphase" : "monophase", (b[16] & 0x04) ? ", production" : "");
        sync_cfg_linky_mode(e);
    }
    push_num(e, 0xFF66, 768, b[16], 1);                     // Mode

    if (is_tri(e)) {
        push_num(e, 0x0B04, 1295, getU16(&b[4]), 2);        // SINSTS1
        push_num(e, 0x0B04, 2319, getU16(&b[6]), 2);        // SINSTS2
        push_num(e, 0x0B04, 2575, getU16(&b[8]), 2);        // SINSTS3
    } else {
        push_num(e, 0x0B04, 1295, getU16(&b[2]), 2);        // SINSTS / PAPP
    }
    if (is_standard(e)) {
        // STGE : la TIC le donne en 8 caracteres hex, l'emetteur en binaire
        char stge[12];
        snprintf(stge, sizeof(stge), "%08lX", (unsigned long)getU32(&b[10]));
        push_str(e, 0xFF66, 535, stge);
    } else {
        push_num(e, 0xFF66, 5, getU16(&b[14]), 2);          // ADPS : Historique seul
    }
}

// Copie un libelle ASCII en rognant espaces et non-imprimables aux deux bouts
static int trim_label(const uint8_t *src, int n, char *out, size_t out_len)
{
    const uint8_t *p = src;
    int nb = n;
    while (nb > 0 && (*p == ' ' || *p < 0x20)) { p++; nb--; }
    while (nb > 0 && (p[nb - 1] == ' ' || p[nb - 1] < 0x20)) nb--;
    if (nb <= 0) { out[0] = '\0'; return 0; }
    if (nb > (int)out_len - 1) nb = (int)out_len - 1;
    memcpy(out, p, (size_t)nb);
    out[nb] = '\0';
    return nb;
}

static void map_extended(emitter_t *e, const uint8_t *b, int len)
{
    if (len < 3) return;
    // Sans le mode, ces trames sont ininterpretables, sauf POWER_MAX_CFG (0x04)
    // et TARIFF_LABEL (0x07) qui portent le leur
    if (!e->mode_known && b[2] != 0x04 && b[2] != 0x07) {
        ESP_LOGD(TAG, "EXT ignore : mode Linky pas encore connu");
        return;
    }
    const bool stdm = is_standard(e), tri = is_tri(e);

    switch (b[2]) {
    case 0x00:                                              // CURRENT_VOLTAGE (15)
        if (len != 15) return;
        push_num(e, 0x0B04, 1288, getU16(&b[3]), 2);        // IRMS1 / IINST
        if (tri) {
            push_num(e, 0x0B04, 2312, getU16(&b[5]), 2);    // IRMS2
            push_num(e, 0x0B04, 2568, getU16(&b[7]), 2);    // IRMS3
        }
        if (stdm) {
            push_num(e, 0x0B04, 1285, getU16(&b[9]), 2);    // URMS1
            if (tri) {
                push_num(e, 0x0B04, 2309, getU16(&b[11]), 2);
                push_num(e, 0x0B04, 2565, getU16(&b[13]), 2);
            }
        }
        break;

    case 0x01:                                              // ENERGY (19)
        if (len != 19) return;
        push_num(e, 0x0702, 0, getU32(&b[3]), 4);           // EAST / BASE
        if (stdm) push_num(e, 0x0702, 1, getU32(&b[7]), 4); // EAIT
        // En Historique, attendre de connaitre l'option tarifaire : en BASE le
        // protocole place l'index BASE dans Tier1 (sect. 7.2.1), qui serait affiche
        // en double
        if (stdm || (e->tariff_known && !histo_base(e))) {
            push_num(e, 0x0702, 256, getU32(&b[11]), 4);
            push_num(e, 0x0702, 258, getU32(&b[15]), 4);
        } else if (histo_base(e)) {
            clear_stale_tier_values(e);
        }
        break;

    case 0x02:                                              // ENERGY_2 (19)
        if (len != 19) return;
        if (!stdm && !histo_tempo(e)) return;               // Standard ou Tempo (sect. 7.3)
        push_num(e, 0x0702, 260, getU32(&b[3]),  4);
        push_num(e, 0x0702, 262, getU32(&b[7]),  4);
        push_num(e, 0x0702, 264, getU32(&b[11]), 4);
        push_num(e, 0x0702, 266, getU32(&b[15]), 4);
        break;

    case 0x03:                                              // VOLTAGE_STATS (15)
        if (len != 15) return;
        if (stdm) {
            push_num(e, 0x0B04, 1297, getU16(&b[3]), 2);    // UMOY1
            if (tri) {
                push_num(e, 0x0B04, 2321, getU16(&b[5]), 2);
                push_num(e, 0x0B04, 2577, getU16(&b[7]), 2);
            }
        } else {
            // [9-14] : IMAX (A) en Historique uniquement
            push_num(e, 0x0B04, 1290, getU16(&b[9]), 2);
            if (tri) {
                push_num(e, 0x0B04, 2314, getU16(&b[11]), 2);
                push_num(e, 0x0B04, 2570, getU16(&b[13]), 2);
            }
        }
        break;

    case 0x04: {                                            // POWER_MAX_CFG (18)
        if (len != 18) return;
        e->linky_mode = b[16];
        e->mode_known = true;
        sync_cfg_linky_mode(e);
        const bool cfg_std = (b[16] & 0x01) != 0;
        if (b[17] != 0 && e->tariff_code != b[17]) {
            e->tariff_code = b[17];
            e->tariff_known = true;
            const char *pt = histo_ptec_code(b[17]);
            ESP_LOGD(TAG, "Code tarif = %u (%s)", b[17], cfg_std ? "NTARF Standard" : (pt ? pt : "?"));
        }
        push_num(e, 0xFF66, 768, b[16], 1);
        // Repli tant qu'aucun TARIFF_LABEL n'a donne le vrai libelle
        if (!cfg_std && !e->label_from_emitter) {
            const char *ptec = histo_ptec_code(b[17]);
            if (ptec) {
                push_str(e, 0xFF66, 16, ptec);
                push_str(e, 0x0702, 32, ptec);              // "Tarif en cours Histo"
            }
        }
        push_num(e, 0x0B04, 1293, getU16(&b[3]), 2);        // SMAXSN / PMAX
        push_num(e, 0x0B01, 13, b[15], 1);                  // PREF / ISOUSC
        if (cfg_std) {
            push_num(e, 0xFF66, 520, getU16(&b[5]), 2);     // SMAXIN
            push_num(e, 0xFF66, 519, getU16(&b[7]), 2);     // SINSTI
            push_num(e, 0xFF66, 522, getU16(&b[9]), 2);     // CCAIN
            push_num(e, 0xFF66, 513, b[13], 1);             // NTARF
            push_num(e, 0x0B01, 14, b[14], 1);              // PCOUP
        }
        break;
    }

    case 0x05:                                              // DAILY_ENERGY (19)
        if (len != 19) return;
        if (!stdm) return;
        push_num(e, 0xFF66, 515, getU32(&b[3]),  4);
        push_num(e, 0xFF66, 516, getU32(&b[7]),  4);
        push_num(e, 0xFF66, 517, getU32(&b[11]), 4);
        push_num(e, 0xFF66, 518, getU32(&b[15]), 4);
        break;

    case 0x06:                                              // COMPLEMENT : aucun attribut cible
        break;

    case 0x07: {                                            // TARIFF_LABEL (6+N)
        if (len < 6) return;
        uint8_t n = b[5];
        if (n == 0 || 6 + n > len) return;
        const bool lbl_std = (b[3] & 0x01) != 0;
        e->linky_mode = b[3];
        e->mode_known = true;
        if (b[4] != 0) { e->tariff_code = b[4]; e->tariff_known = true; }
        char label[24];
        if (trim_label(&b[6], n, label, sizeof(label)) <= 0) return;
        push_str(e, 0xFF66, 16, label);
        if (lbl_std) push_str(e, 0xFF66, 512, label);       // LTARF
        else         push_str(e, 0x0702, 32, label);        // PTEC
        if (!e->label_from_emitter) {
            e->label_from_emitter = true;
            ESP_LOGD(TAG, "Libelle tarif recu de l'emetteur : \"%s\" (%s)", label, lbl_std ? "LTARF" : "PTEC");
        }
        break;
    }

    case 0x08: {                                            // METER_SERIAL (4+N)
        if (len < 4) return;
        uint8_t n = b[3];
        if (n == 0 || 4 + n > len) return;
        char serial[24];
        if (trim_label(&b[4], n, serial, sizeof(serial)) <= 0) return;
        push_str(e, 0x0702, 776, serial);
        if (!e->serial_from_emitter) {
            e->serial_from_emitter = true;
            ESP_LOGD(TAG, "Numero de serie compteur : %s", serial);
        }
        break;
    }

    case 0x09: {                                            // METER_DATE (4+N), Standard
        if (len < 4) return;
        uint8_t n = b[3];
        if (n == 0 || 4 + n > len) return;
        char date[20];
        if (n > (int)sizeof(date) - 1) n = (uint8_t)(sizeof(date) - 1);
        int nb = 0;
        for (uint8_t i = 0; i < n && b[4 + i] >= 0x20; i++) date[nb++] = (char)b[4 + i];
        date[nb] = '\0';
        if (nb > 0) push_str(e, 0xFF66, 514, date);
        break;
    }

    case 0x0A: {                                            // TARIFF_OPTION (16)
        if (len < 16) return;
        e->linky_mode = b[3];
        e->mode_known = true;
        char s[5];
        int on = 0;
        for (int i = 0; i < 4 && b[4 + i] >= 0x20; i++) s[on++] = (char)b[4 + i];
        s[on] = '\0';
        if (on > 0) push_str(e, 0xFF66, 0, s);              // OPTARIF
        int dn = 0;
        for (int i = 0; i < 4 && b[8 + i] >= 0x20; i++) s[dn++] = (char)b[8 + i];
        s[dn] = '\0';
        if (dn > 0) push_str(e, 0xFF66, 1, s);              // DEMAIN
        break;
    }

    case 0x0B:                                              // HISTO_ALERTS (12)
        if (len < 12) return;
        e->linky_mode = b[3];
        e->mode_known = true;
        if (b[3] & 0x01) break;                             // Standard : sans objet
        push_num(e, 0xFF66, 4, b[4], 1);                    // PEJP
        push_num(e, 0xFF66, 3, b[5], 1);                    // PPOT
        push_num(e, 0xFF66, 6, getU16(&b[6]), 2);           // ADIR1
        push_num(e, 0xFF66, 7, getU16(&b[8]), 2);           // ADIR2
        push_num(e, 0xFF66, 8, getU16(&b[10]), 2);          // ADIR3
        break;

    default:
        ESP_LOGD(TAG, "Sous-type EXT 0x%02X non mappe (len=%d)", b[2], len);
        break;
    }
}

// ============================================================================
// Appareil dans device_data
// ============================================================================

// Adresse 16 bits synthetique et unique (avec le Zigbee comme entre LoRa) :
// derivee des 2 derniers octets de la MAC, comme computeLoraShortAddr()
static uint16_t compute_short_addr(const uint8_t *mac, int self)
{
    uint16_t a = (uint16_t)((mac[6] << 8) | mac[7]);
    if (a == 0) a = (uint16_t)((mac[4] << 8) | mac[5]);
    if (a == 0) a = 1;
    for (int guard = 0; guard < 65535; guard++) {
        bool clash = (zigate_devices_find_by_short(a) != NULL) || a >= 0xFFF8;
        for (int i = 0; i < LORA_MAX_EMITTERS && !clash; i++) {
            if (i != self && s_em[i].valid && s_em[i].short_addr == a) clash = true;
        }
        if (!clash) return a;
        if (++a == 0 || a >= 0xFFF8) a = 1;
    }
    return a;
}

static void set_basic_string(uint64_t ieee, uint16_t attr, const char *s)
{
    char hex[DEVDATA_VALUE_LEN];
    to_hex((const uint8_t *)s, strlen(s), hex, sizeof(hex));
    device_data_set_value(ieee, 0x0000, attr, hex);
}

// Cree/complete la fiche device_data et charge le template. Le couple
// (deviceId, model) doit correspondre a un template, sinon la fiche restera
// vide : on le signale, c'est la seule erreur invisible dans les logs radio.
static void ensure_device(emitter_t *e)
{
    uint64_t ieee = mac_to_ieee(e->mac);
    device_data_t *dd = device_data_get_or_create(ieee);
    if (!dd) {
        ESP_LOGE(TAG, "Fiche device_data impossible pour %016llX", (unsigned long long)ieee);
        return;
    }
    set_basic_string(ieee, 0x0004, "LiXee");                // Manufacturer
    if (e->model[0]) set_basic_string(ieee, 0x0005, e->model);
    if (e->version[0]) set_basic_string(ieee, 0x4000, e->version);

    if (e->device_id[0]) {
        char file[16], path[48];
        snprintf(file, sizeof(file), "%s.json", e->device_id);
        snprintf(path, sizeof(path), DEVDATA_TP_DIR "/%s", file);
        struct stat st;
        if (stat(path, &st) != 0) {
            ESP_LOGW(TAG, "Template %s introuvable : la fiche de %016llX restera vide",
                     path, (unsigned long long)ieee);
        } else if (device_data_load_template(ieee, file, e->model) != ESP_OK) {
            ESP_LOGW(TAG, "Template %s : modele '%s' non charge", path, e->model);
        }
    } else {
        ESP_LOGI(TAG, "%016llX : identite pas encore lue, template applique apres le POLL",
                 (unsigned long long)ieee);
    }
    dd->dirty = true;
    device_data_save(ieee);
}

// Un ZLinky devient le compteur Linky de la configuration Energie s'il n'y en
// a pas encore (ecart voulu avec la Box, qui ecrasait la valeur)
static void register_linky_if_zlinky(const emitter_t *e)
{
    if (strcmp(e->model, LORA_ZLINKY_MODEL) != 0) return;
    cfg_general_t *g = cfg_get_general_mut();
    if (g->zlinky[0]) return;
    snprintf(g->zlinky, sizeof(g->zlinky), "%016llX", (unsigned long long)mac_to_ieee(e->mac));
    cfg_request_save_general();     // sauvegarde par la boucle principale (voir sync_cfg_linky_mode)
    ESP_LOGI(TAG, "%s designe comme compteur Linky (config Energie)", g->zlinky);
}

static void set_device_version(emitter_t *e, const char *ver)
{
    if (strcmp(e->version, ver) == 0) return;
    strlcpy(e->version, ver, sizeof(e->version));
    uint64_t ieee = mac_to_ieee(e->mac);
    set_basic_string(ieee, 0x4000, ver);                    // SWBuildID, comme le ZLinky Zigbee
    device_data_save(ieee);
}

// ============================================================================
// Appairage (sect. 4)
// ============================================================================

static void start_pairing_now(void)
{
    s_pairing_mode = true;
    s_pairing_start = millis();
    s_awaiting_confirm = false;
    s_led_toggle = 0;
    s_led_state = false;
    // Rendez-vous FIXE : canal 3 + SF11, quelle que soit la config operationnelle
    set_channel(LORA_PAIR_CHANNEL);
    set_sf(LORA_PAIR_SF);
    start_rx();
    s_last_pair_rearm = millis();
    ESP_LOGI(TAG, "Appairage ouvert %d s sur canal %d (%u MHz), SF%d",
             LORA_PAIR_WINDOW_S, LORA_PAIR_CHANNEL, CH_FREQ[LORA_PAIR_CHANNEL], LORA_PAIR_SF);
}

static int find_or_create_slot(const uint8_t *mac)
{
    for (int i = 0; i < LORA_MAX_EMITTERS; i++)
        if (s_em[i].valid && memcmp(s_em[i].mac, mac, MAC_SIZE) == 0) return i;
    for (int i = 0; i < LORA_MAX_EMITTERS; i++)
        if (!s_em[i].valid) return i;
    return -1;
}

// @return true si la radio a ete re-armee ici
static bool handle_pair_request(const uint8_t *buf, int len)
{
    if (!s_pairing_mode || s_awaiting_confirm || len < SZ_PAIR_REQUEST) return false;
    memcpy(s_pending_mac, &buf[2], MAC_SIZE);

    // Format de base (11 octets) : identite lue par POLL apres l'appairage.
    // Format etendu (14+N) : [11-12] device_id, [13] longueur, [14..] model.
    if (len >= 14 && (14 + buf[13]) <= len && buf[13] < (int)sizeof(s_pending_model)) {
        snprintf(s_pending_device_id, sizeof(s_pending_device_id), "%u", (unsigned)getU16(&buf[11]));
        memcpy(s_pending_model, &buf[14], buf[13]);
        s_pending_model[buf[13]] = '\0';
    } else {
        s_pending_device_id[0] = '\0';
        s_pending_model[0] = '\0';
    }

    esp_fill_random(s_pending_key, KEY_SIZE);
    char mh[17];
    mac_hex(s_pending_mac, mh);
    ESP_LOGI(TAG, "PAIR_REQUEST de %s (device_id=%s, model=%s)", mh,
             s_pending_device_id[0] ? s_pending_device_id : "a lire",
             s_pending_model[0] ? s_pending_model : "a lire");

    uint8_t pkt[SZ_PAIR_RESPONSE];
    pkt[0] = 0x14;                          // v1 | PAIR_RESPONSE
    pkt[1] = buf[1];                        // echo du seq
    memcpy(&pkt[2], s_pending_key, KEY_SIZE);
    pkt[18] = 0x00;                         // statut OK
    pkt[19] = s_op_channel;                 // canal assigne (sect. 4.2)
    pkt[20] = s_op_sf;                      // SF assigne (sect. 4.2)

    esp_err_t st = sx_transmit(pkt, SZ_PAIR_RESPONSE, NULL);
    if (st != ESP_OK) {
        ESP_LOGW(TAG, "TX PAIR_RESPONSE en echec (%s)", esp_err_to_name(st));
        start_rx();
        return true;
    }
    // Le PAIR_CONFIRM arrive sur la config OPERATIONNELLE : l'emetteur y bascule
    set_channel(s_op_channel);
    set_sf(s_op_sf);
    start_rx();
    s_awaiting_confirm = true;
    s_confirm_deadline = millis() + 4000;
    ESP_LOGI(TAG, "PAIR_RESPONSE envoye -> attente CONFIRM sur canal %d, SF%d", s_op_channel, s_op_sf);
    return true;
}

static void handle_pair_confirm(const uint8_t *buf, int len)
{
    if (!s_awaiting_confirm || len != SZ_PAIR_CONFIRM) return;
    if (memcmp(&buf[2], s_pending_mac, MAC_SIZE) != 0) return;

    // Preuve = AES-ECB(MAC(8) || 0x00*8, cle)[0..3]
    uint8_t in[16] = {0}, out[16];
    memcpy(in, s_pending_mac, MAC_SIZE);
    aes_ecb(s_pending_key, in, out);
    if (memcmp(&buf[10], out, 4) != 0) { ESP_LOGW(TAG, "Preuve d'appairage invalide"); return; }

    int slot = find_or_create_slot(s_pending_mac);
    if (slot < 0) { ESP_LOGW(TAG, "Aucun slot libre (max %d)", LORA_MAX_EMITTERS); return; }
    emitter_t *e = &s_em[slot];
    bool known = e->valid;
    if (!known) memset(e, 0, sizeof(*e));
    memcpy(e->mac, s_pending_mac, MAC_SIZE);
    memcpy(e->key, s_pending_key, KEY_SIZE);
    strlcpy(e->device_id, s_pending_device_id, sizeof(e->device_id));
    strlcpy(e->model, s_pending_model, sizeof(e->model));
    e->valid = true;
    e->seq_init = false;
    e->rx_count = 0;
    e->missed = 0;
    e->poll_pending = false;
    e->ident_step = s_pending_device_id[0] ? 0 : 1;     // identite inconnue : POLL
    e->ident_next_ms = millis();
    e->short_addr = compute_short_addr(e->mac, slot);
    save_config();
    ensure_device(e);
    if (s_pending_model[0]) register_linky_if_zlinky(e);

    s_awaiting_confirm = false;
    s_pairing_mode = false;
    char mh[17];
    mac_hex(s_pending_mac, mh);
    ESP_LOGI(TAG, "*** APPAIRAGE REUSSI *** %s (slot %d)", mh, slot);

    // Canal dedie a l'assistant (pas une alerte partagee que d'autres pages
    // consommeraient, cf. la Box)
    s_paired_ieee = mac_to_ieee(s_pending_mac);
    strlcpy(s_paired_model, s_pending_model[0] ? s_pending_model : "Appareil LoRa", sizeof(s_paired_model));
    s_paired_pending = true;
}

// ============================================================================
// Lecture d'attribut a la demande (POLL, sect. 8)
// ============================================================================

static bool queue_poll_locked(int slot, uint16_t cluster, uint16_t attr)
{
    if (slot < 0 || slot >= LORA_MAX_EMITTERS || !s_em[slot].valid) return false;
    emitter_t *e = &s_em[slot];
    e->poll_cluster = cluster;
    e->poll_attr = attr;
    e->poll_retries = 0;
    e->poll_resp_valid = false;
    e->poll_pending = true;
    ESP_LOGD(TAG, "POLL cluster=0x%04X attr=0x%04X en file pour slot %d", cluster, attr, slot);
    return true;
}

// Preambule allonge aux SF bas : le ZLinky ne detecte un preambule que vers
// T+40-60 ms apres son uplink. N = 0,040 x 406250 / 2^sf, jamais moins de 8.
static uint16_t poll_preamble(uint8_t sf)
{
    uint32_t n = 16250UL >> sf;
    return (n < LORA_PREAMBLE) ? LORA_PREAMBLE : (uint16_t)n;
}

static void send_poll_request(emitter_t *e)
{
    uint8_t pkt[16];
    pkt[0] = 0x10 | T_POLL_REQUEST;
    pkt[1] = e->poll_seq++;
    pkt[2] = (uint8_t)(e->poll_cluster >> 8);
    pkt[3] = (uint8_t)(e->poll_cluster & 0xFF);
    pkt[4] = (uint8_t)(e->poll_attr >> 8);
    pkt[5] = (uint8_t)(e->poll_attr & 0xFF);
    int total = encrypt_packet(pkt, 6, e->key, e->mac);

    uint16_t pre = poll_preamble(s_radio_sf);
    if (pre != LORA_PREAMBLE) sx_set_preamble(pre);
    // Chronologie du protocole (sect. 8) : le ZLinky passe en RX a T+5 ms et ne
    // detecte un preambule que vers T+40 a 60 ms ; le recepteur doit emettre
    // vers T+20 ms. Ce pilote repond en 1 ms (la LiXee-Box, plus lente, tombait
    // naturellement a ~20 ms) : le preambule de 8 symboles (~40 ms a SF11)
    // etait presque fini quand le ZLinky le cherchait, et les POLL restaient
    // sans reponse (identification, version, lectures a la demande).
    uint32_t since = millis() - s_rx_read_ms;
    if (since < POLL_TX_DELAY_MS) vTaskDelay(pdMS_TO_TICKS(POLL_TX_DELAY_MS - since));
    uint32_t delay_ms = millis() - s_rx_read_ms;     // depuis la lecture de l'uplink
    uint32_t air_ms = 0;
    esp_err_t st = sx_transmit(pkt, (uint8_t)total, &air_ms);
    if (pre != LORA_PREAMBLE) sx_set_preamble(LORA_PREAMBLE);
    start_rx();
    e->poll_retries++;
    ESP_LOGD(TAG, "POLL_REQUEST cluster=0x%04X attr=0x%04X envoye (essai %d, %s, %lu ms apres l'uplink, emission %lu ms)",
             e->poll_cluster, e->poll_attr, e->poll_retries, esp_err_to_name(st),
             (unsigned long)delay_ms, (unsigned long)air_ms);
}

static void i64_to_str(int64_t v, char *out, size_t len)
{
    snprintf(out, len, "%lld", (long long)v);
}

static void format_poll_value(uint8_t ztype, const uint8_t *v, uint8_t n, char *out, size_t len)
{
    if (ztype == 0x41 || ztype == 0x42) {
        size_t o = 0;
        for (uint8_t i = 0; i < n && o + 1 < len; i++)
            out[o++] = (v[i] >= 0x20 && v[i] < 0x7F) ? (char)v[i] : '.';
        out[o] = '\0';
        return;
    }
    int64_t val = 0;
    for (uint8_t i = 0; i < n && i < 8; i++) val = (val << 8) | v[i];
    if ((ztype == 0x28 && n == 1 && (v[0] & 0x80)) || (ztype == 0x29 && n == 2 && (v[0] & 0x80)))
        val -= (int64_t)1 << (8 * n);
    i64_to_str(val, out, len);
}

// ============================================================================
// Reception des donnees
// ============================================================================

// Retourne true si une requete POLL a ete emise : la radio est alors deja
// revenue en reception (send_poll_request) et la reponse peut arriver pendant
// le decodage de la trame, qui suit.
static bool handle_data(uint8_t *buf, int len, float rssi, float snr)
{
    // On essaie chaque cle : le bon emetteur est celui dont le MIC valide
    uint8_t backup[SX_MAX_PAYLOAD];
    if (len > (int)sizeof(backup)) return false;
    memcpy(backup, buf, (size_t)len);
    bool poll_sent = false;

    for (int i = 0; i < LORA_MAX_EMITTERS; i++) {
        if (!s_em[i].valid) continue;
        memcpy(buf, backup, (size_t)len);
        if (!decrypt_packet(buf, len, s_em[i].key, s_em[i].mac)) continue;

        emitter_t *e = &s_em[i];
        uint8_t seq = buf[1];
        if (e->seq_init) {
            uint8_t expected = (uint8_t)(e->last_seq + 1);
            if (seq != expected) e->missed += (uint8_t)(seq - expected);
        }
        e->last_seq = seq;
        e->seq_init = true;
        e->rx_count++;
        e->last_rssi = rssi;
        e->last_snr = snr;
        e->last_seen_ms = millis();
        e->last_seen_epoch = time(NULL);
        s_last_radio_rx = millis();

        // Identification puis version (cluster Basic), si rien d'autre n'attend
        if (e->ident_step && !e->poll_pending && (int32_t)(millis() - e->ident_next_ms) >= 0) {
            queue_poll_locked(i, POLL_CLUSTER_BASIC, e->ident_step == 1 ? POLL_ATTR_DEVICE_ID : POLL_ATTR_MODEL);
        }
        if (!e->ident_step && e->ver_step && !e->poll_pending && (int32_t)(millis() - e->ver_next_ms) >= 0) {
            queue_poll_locked(i, POLL_CLUSTER_BASIC, e->ver_step == 1 ? POLL_ATTR_FW_VERSION : POLL_ATTR_SW_BUILD);
        }

        // Fenetre descendante : emettre MAINTENANT, avant le decodage (lent :
        // fichiers + MQTT) qui ferait rater les ~20 ms. Best-effort borne.
        if (e->poll_pending) {
            if (e->poll_retries >= 8) {
                e->poll_pending = false;
                if (e->ident_step && e->poll_cluster == POLL_CLUSTER_BASIC) e->ident_next_ms = millis() + IDENT_RETRY_MS;
                if (e->ver_step && e->poll_cluster == POLL_CLUSTER_BASIC)   e->ver_next_ms = millis() + IDENT_RETRY_MS;
                ESP_LOGW(TAG, "POLL cluster=0x%04X attr=0x%04X abandonne apres %d essais",
                         e->poll_cluster, e->poll_attr, e->poll_retries);
            } else {
                send_poll_request(e);
                poll_sent = true;
            }
        }

        device_data_t *dd = device_data_find(mac_to_ieee(e->mac));
        if (!dd) ensure_device(e);                          // fiche supprimee entre-temps

        int data_len = len - MIC_SIZE;
        if ((buf[0] & 0x0F) == T_ESSENTIAL) map_essential(e, buf, data_len);
        else                                map_extended(e, buf, data_len);
        return poll_sent;
    }
    ESP_LOGD(TAG, "Trame chiffree non attribuee (MIC KO pour toutes les cles), len=%d", len);
    return false;
}

static void handle_ident_response(int slot, uint16_t attr, uint8_t status, uint8_t ztype,
                                  const uint8_t *val, uint8_t vlen)
{
    emitter_t *e = &s_em[slot];
    char mh[17];
    mac_hex(e->mac, mh);
    if (status == POLL_UNKNOWN_ATTR) {
        ESP_LOGI(TAG, "%s ne connait pas le cluster Basic : identite ZLinky par defaut", mh);
        strlcpy(e->device_id, LORA_ZLINKY_DEVICE_ID, sizeof(e->device_id));
        strlcpy(e->model, LORA_ZLINKY_MODEL, sizeof(e->model));
        e->ident_step = 0;
    } else if (status != POLL_OK) {
        e->ident_next_ms = millis() + IDENT_RETRY_MS;
        return;
    } else if (attr == POLL_ATTR_DEVICE_ID && e->ident_step == 1) {
        uint32_t id = 0;
        for (uint8_t k = 0; k < vlen && k < 4; k++) id = (id << 8) | val[k];
        snprintf(e->device_id, sizeof(e->device_id), "%u", (unsigned)(id % 10000000u));
        e->ident_step = 2;
        e->ident_next_ms = millis();
        ESP_LOGI(TAG, "%s : device_id = %s (type 0x%02X)", mh, e->device_id, ztype);
        return;
    } else if (attr == POLL_ATTR_MODEL && e->ident_step == 2) {
        uint8_t n = (vlen < (int)sizeof(e->model) - 1) ? vlen : (uint8_t)(sizeof(e->model) - 1);
        memcpy(e->model, val, n);
        e->model[n] = '\0';
        e->ident_step = 0;
    } else {
        return;                                             // hors sequence
    }
    ESP_LOGI(TAG, "%s identifie : device_id=%s, model=%s", mh, e->device_id, e->model);
    save_config();
    ensure_device(e);
    register_linky_if_zlinky(e);
    e->ver_step = 1;
    e->ver_next_ms = millis();
}

static void handle_version_response(int slot, uint16_t attr, uint8_t status,
                                    const uint8_t *val, uint8_t vlen)
{
    emitter_t *e = &s_em[slot];
    if (attr == POLL_ATTR_FW_VERSION && e->ver_step == 1) {
        if (status == POLL_UNKNOWN_ATTR) {
            e->fw_known = false;
            e->ver_step = 0;
            set_device_version(e, "< 1.0.0");
            return;
        }
        if (status != POLL_OK || vlen < 2) { e->ver_next_ms = millis() + IDENT_RETRY_MS; return; }
        e->fw_version = getU16(val);
        e->fw_known = true;
        e->ver_step = 2;
        e->ver_next_ms = millis();
        char v[40];
        snprintf(v, sizeof(v), "%u.%u.%u", (unsigned)(e->fw_version >> 8),
                 (unsigned)((e->fw_version >> 4) & 0x0F), (unsigned)(e->fw_version & 0x0F));
        set_device_version(e, v);
        ESP_LOGI(TAG, "Slot %d : firmware %s (0x%04X)", slot, v, e->fw_version);
    } else if (attr == POLL_ATTR_SW_BUILD && e->ver_step == 2) {
        e->ver_step = 0;
        if (status != POLL_OK || vlen == 0) return;         // la version courte reste
        char b[32];
        uint8_t n = (vlen < (int)sizeof(b) - 1) ? vlen : (uint8_t)(sizeof(b) - 1);
        uint8_t k = 0;
        for (; k < n && val[k] >= 0x20 && val[k] < 0x7F; k++) b[k] = (char)val[k];
        b[k] = '\0';
        if (k > 0) set_device_version(e, b);
    }
}

// POLL_RESPONSE : [cluster(2)][attr(2)][statut][type][len][valeur]
static void handle_poll_response(uint8_t *buf, int len)
{
    uint8_t backup[SX_MAX_PAYLOAD];
    if (len > (int)sizeof(backup)) return;
    memcpy(backup, buf, (size_t)len);
    for (int i = 0; i < LORA_MAX_EMITTERS; i++) {
        if (!s_em[i].valid) continue;
        memcpy(buf, backup, (size_t)len);
        if (!decrypt_packet(buf, len, s_em[i].key, s_em[i].mac)) continue;
        emitter_t *e = &s_em[i];
        int data_len = len - MIC_SIZE;
        if (data_len < 9) return;
        uint16_t cluster = getU16(&buf[2]);
        uint16_t attr = getU16(&buf[4]);
        uint8_t status = buf[6];
        uint8_t ztype = buf[7];
        uint8_t vlen = buf[8];
        if (9 + (int)vlen > data_len) vlen = (uint8_t)(data_len - 9);

        e->poll_pending = false;
        e->poll_resp_valid = true;
        e->poll_resp_status = status;
        e->poll_resp_type = ztype;
        e->poll_resp_len = (vlen > (int)sizeof(e->poll_resp_value)) ? (uint8_t)sizeof(e->poll_resp_value) : vlen;
        memcpy(e->poll_resp_value, &buf[9], e->poll_resp_len);
        ESP_LOGD(TAG, "POLL_RESPONSE cluster=0x%04X attr=0x%04X statut=0x%02X type=0x%02X len=%d",
                 cluster, attr, status, ztype, vlen);

        // Identite / version : pas des mesures, pas injectees
        if (cluster == POLL_CLUSTER_BASIC && e->ident_step) {
            handle_ident_response(i, attr, status, ztype, &buf[9], vlen);
            return;
        }
        if (cluster == POLL_CLUSTER_BASIC && e->ver_step &&
            (attr == POLL_ATTR_FW_VERSION || attr == POLL_ATTR_SW_BUILD)) {
            handle_version_response(i, attr, status, &buf[9], vlen);
            return;
        }
        if (status == POLL_OK && vlen > 0) {
            uint8_t n = vlen;
            if (ztype != 0x41 && ztype != 0x42 && n > 8) n = 8;   // numerique : 8 octets max
            push_value(e, cluster, attr, ztype, &buf[9], n);
        }
        return;
    }
    ESP_LOGD(TAG, "POLL_RESPONSE de %d octets : MIC invalide pour toutes les cles", len);
}

// ============================================================================
// Mise a jour OTA d'un emetteur (sect. 10) - machine a etats de la Box
//   OTA_START -> ACK ; OTA_BLOCK x N -> ACK par fenetre de W blocs ;
//   OTA_END -> ACK puis redemarrage de l'emetteur. Liaison a l'alternat : la
//   box n'emet jamais pendant qu'elle attend un ACK.
// ============================================================================

static const char *ota_status_text(uint8_t st)
{
    switch (st) {
    case 0x00: return "OK";
    case 0x01: return "emetteur occupe";
    case 0x02: return "bloc corrompu";
    case 0x03: return "erreur d'ecriture en flash";
    case 0x04: return "bloc hors sequence";
    case 0x05: return "firmware trop volumineux";
    case 0x06: return "CRC du firmware incorrect";
    case 0x07: return "l'emetteur n'est plus en mise a jour (il a redemarre ?)";
    default:   return "statut inconnu";
    }
}

static bool ota_path_locked(int slot, char *out, size_t len)
{
    if (slot < 0 || slot >= LORA_MAX_EMITTERS || !s_em[slot].valid) return false;
    char mh[17];
    mac_hex(s_em[slot].mac, mh);
    snprintf(out, len, LORA_OTA_DIR "/lora_%s.ota", mh);
    return true;
}

static bool ota_active_locked(void)
{
    return s_ota.requested || s_ota.state == OTA_WAITING || s_ota.state == OTA_SENDING ||
           s_ota.state == OTA_ENDING;
}

static void ota_finish(uint8_t state, const char *msg)
{
    if (s_ota.file) { fclose(s_ota.file); s_ota.file = NULL; }
    s_ota.state = state;
    s_ota.waiting_ack = false;
    s_ota.end_ms = millis();
    strlcpy(s_ota.msg, msg, sizeof(s_ota.msg));
    ESP_LOGI(TAG, "OTA %s : %s (bloc %u/%u, %lu re-emission(s))",
             state == OTA_DONE ? "TERMINEE" : "ECHEC", msg, s_ota.block, s_ota.total_blocks,
             (unsigned long)s_ota.total_retries);
    if (s_ota.tx_sf && s_ota.tx_sf != s_op_sf) set_sf(s_op_sf);   // retour au SF operationnel
    // Fichier supprime apres un succes, garde apres un echec (relance sans renvoi)
    if (state == OTA_DONE && s_ota.slot >= 0 && s_ota.slot < LORA_MAX_EMITTERS) {
        s_em[s_ota.slot].ver_step = 1;                      // version relue au redemarrage
        s_em[s_ota.slot].ver_next_ms = millis();
        char path[64];
        if (ota_path_locked(s_ota.slot, path, sizeof(path))) remove(path);
    }
    start_rx();
}

// Ouvre le fichier et calcule son CRC32 (zlib). Appelee par la tache SANS le
// verrou : le calcul sur 1 Mo dure plusieurs secondes et les routes web
// doivent pouvoir lire l'etat pendant ce temps.
static void ota_begin(void)
{
    char path[64];
    if (!lock(portMAX_DELAY)) return;
    // Correction : la tache teste s_ota.requested SANS le verrou avant
    // d'appeler ota_begin(). Une annulation (lora_ota_cancel, qui remet
    // requested a false sous verrou) arrivee entre ce test et la prise du
    // verrou etait ignoree : l'OTA annulee demarrait quand meme. Conditions
    // relues ici, verrou tenu.
    if (!s_ota.requested || s_pairing_mode || s_awaiting_confirm) {
        unlock();
        return;
    }
    // L'etat passe a "en attente" AVANT le calcul, sinon la page verrait encore
    // le resultat de la tentative precedente
    s_ota.state = OTA_WAITING;
    s_ota.msg[0] = '\0';
    s_ota.block = 0;
    s_ota.total_blocks = 0;
    s_ota.requested = false;
    s_ota.slot = s_ota.req_slot;
    s_ota.version = s_ota.req_version;
    s_ota.tx_block = 0; s_ota.seq = 0; s_ota.tries = 0; s_ota.total_retries = 0;
    s_ota.waiting_ack = false; s_ota.ack_got = false;
    s_ota.window = 1; s_ota.win_waiting = false; s_ota.win_fails = 0; s_ota.restarts = 0;
    s_ota.start_ms = 0; s_ota.end_ms = 0;
    s_ota.tx_sf = 0;
    s_ota.cancel_requested = false;
    bool ok = ota_path_locked(s_ota.slot, path, sizeof(path));
    if (ok) s_ota.file = fopen(path, "rb");
    if (!ok || !s_ota.file) {
        s_ota.state = OTA_FAILED;
        s_ota.end_ms = millis();
        strlcpy(s_ota.msg, "fichier firmware illisible", sizeof(s_ota.msg));
        unlock();
        return;
    }
    FILE *f = s_ota.file;
    s_ota.preparing = true;
    unlock();

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    uint32_t crc = 0xFFFFFFFFUL;
    uint8_t buf[256];
    size_t n, total = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            crc ^= buf[i];
            for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
        }
        total += n;
        if ((total & 0x3FFF) == 0) vTaskDelay(1);           // laisse vivre les autres taches
    }

    if (!lock(portMAX_DELAY)) return;
    s_ota.preparing = false;
    if (s_ota.cancel_requested) {                           // annulee pendant le calcul
        s_ota.cancel_requested = false;
        ota_finish(OTA_FAILED, "mise a jour annulee");
        unlock();
        return;
    }
    s_ota.size = (sz > 0) ? (uint32_t)sz : 0;
    s_ota.crc = ~crc;
    s_ota.block_size = s_ota.req_block_size ? s_ota.req_block_size : OTA_BLOCK_SIZE;
    // Nombre de blocs sur 16 bits dans le protocole : 1 Mo en blocs de 8
    // octets deborderait (non verifie sur la Box)
    uint32_t nblocks = (s_ota.size + s_ota.block_size - 1) / s_ota.block_size;
    if (nblocks == 0 || nblocks > 0xFFFF) {
        ota_finish(OTA_FAILED, "fichier trop volumineux pour cette taille de bloc");
        unlock();
        return;
    }
    s_ota.total_blocks = (uint16_t)nblocks;
    s_ota.tx_preamble = s_ota.req_preamble ? s_ota.req_preamble : LORA_PREAMBLE;
    s_ota.tx_sf = s_ota.req_sf ? s_ota.req_sf : s_op_sf;
    if (s_ota.tx_sf != s_op_sf) set_sf(s_ota.tx_sf);       // diagnostic : tout l'OTA sur ce SF
    start_rx();
    s_last_radio_rx = millis();     // l'emetteur en mode OTA n'emet plus ses trames habituelles
    ESP_LOGI(TAG, "OTA slot %d : %lu octets, %u blocs de %u, CRC32 0x%08lX, version 0x%04X, "
             "canal %d, SF%d, preambule %d, fenetre demandee %d",
             s_ota.slot, (unsigned long)s_ota.size, s_ota.total_blocks, s_ota.block_size,
             (unsigned long)s_ota.crc, s_ota.version, s_op_channel, s_ota.tx_sf,
             s_ota.tx_preamble, s_ota.req_window);
    unlock();
}

static void ota_send_current(void)
{
    emitter_t *e = &s_em[s_ota.slot];
    uint8_t pkt[4 + OTA_BLOCK_SIZE + MIC_SIZE];
    int clear_len = 0;

    if (s_ota.state == OTA_WAITING) {                       // OTA_START (15/16 octets)
        pkt[0] = 0x10 | T_OTA_START;
        pkt[1] = s_ota.seq;
        pkt[2] = (uint8_t)(s_ota.version >> 8);       pkt[3] = (uint8_t)(s_ota.version & 0xFF);
        pkt[4] = (uint8_t)(s_ota.size >> 24);         pkt[5] = (uint8_t)(s_ota.size >> 16);
        pkt[6] = (uint8_t)(s_ota.size >> 8);          pkt[7] = (uint8_t)(s_ota.size & 0xFF);
        pkt[8] = (uint8_t)(s_ota.crc >> 24);          pkt[9] = (uint8_t)(s_ota.crc >> 16);
        pkt[10] = (uint8_t)(s_ota.crc >> 8);          pkt[11] = (uint8_t)(s_ota.crc & 0xFF);
        pkt[12] = (uint8_t)(s_ota.total_blocks >> 8); pkt[13] = (uint8_t)(s_ota.total_blocks & 0xFF);
        pkt[14] = s_ota.block_size;
        clear_len = 15;
        if (s_ota.req_window >= 2) { pkt[15] = s_ota.req_window; clear_len = 16; }
    } else if (s_ota.state == OTA_SENDING) {                // OTA_BLOCK (4 + bloc)
        pkt[0] = 0x10 | T_OTA_BLOCK;
        pkt[1] = s_ota.seq;
        pkt[2] = (uint8_t)(s_ota.tx_block >> 8);
        pkt[3] = (uint8_t)(s_ota.tx_block & 0xFF);
        memset(&pkt[4], 0xFF, s_ota.block_size);            // dernier bloc complete par 0xFF
        if (s_ota.file) {
            fseek(s_ota.file, (long)s_ota.tx_block * s_ota.block_size, SEEK_SET);
            size_t got = fread(&pkt[4], 1, s_ota.block_size, s_ota.file);
            (void)got;
        }
        clear_len = 4 + s_ota.block_size;
    } else {                                                // OTA_END (7 octets)
        pkt[0] = 0x10 | T_OTA_END;
        pkt[1] = s_ota.seq;
        pkt[2] = (uint8_t)(s_ota.crc >> 24);  pkt[3] = (uint8_t)(s_ota.crc >> 16);
        pkt[4] = (uint8_t)(s_ota.crc >> 8);   pkt[5] = (uint8_t)(s_ota.crc & 0xFF);
        pkt[6] = 0x00;
        clear_len = 7;
    }

    bool trace = (s_ota.state != OTA_SENDING) || (s_ota.tx_block % 100 == 0) || (s_ota.tries > 0);
    char clear_hex[2 * 16 + 1] = "";
    if (trace) to_hex(pkt, clear_len < 16 ? (size_t)clear_len : 16, clear_hex, sizeof(clear_hex));

    int total = encrypt_packet(pkt, clear_len, e->key, e->mac);
    // L'emetteur vient peut-etre d'emettre son ACK : temps de repasser en RX
    vTaskDelay(pdMS_TO_TICKS(20));
    if (s_ota.tx_preamble != LORA_PREAMBLE) sx_set_preamble(s_ota.tx_preamble);
    uint32_t air = 0;
    esp_err_t st = sx_transmit(pkt, (uint8_t)total, &air);
    if (s_ota.tx_preamble != LORA_PREAMBLE) sx_set_preamble(LORA_PREAMBLE);
    if (trace) {
        const char *nm = (s_ota.state == OTA_WAITING) ? "OTA_START" :
                         (s_ota.state == OTA_SENDING) ? "OTA_BLOCK" : "OTA_END";
        ESP_LOGD(TAG, "OTA : %s emis (essai %u, seq %u, %d octets, %lu ms, %s) clair=%s%s",
                 nm, s_ota.tries + 1, s_ota.seq, total, (unsigned long)air, esp_err_to_name(st),
                 clear_hex, clear_len > 16 ? "..." : "");
    }
    start_rx();
    s_ota.tries++;
    s_ota.ack_got = false;
    s_ota.waiting_ack = true;
    s_ota.ack_deadline = millis() + OTA_ACK_TIMEOUT_MS;
}

// OTA_ACK : [seq][index(2)][statut][fenetre][reserve]
static void handle_ota_ack(uint8_t *buf, int len)
{
    if (!ota_active_locked() || s_ota.requested) return;
    if (s_ota.slot < 0 || s_ota.slot >= LORA_MAX_EMITTERS) return;
    emitter_t *e = &s_em[s_ota.slot];
    if (!decrypt_packet(buf, len, e->key, e->mac)) return;
    int clear_len = len - MIC_SIZE;
    if (clear_len < 5) return;
    s_ota.ack_block = getU16(&buf[2]);
    s_ota.ack_status = buf[4];
    s_ota.ack_window = (clear_len >= 6) ? buf[5] : 0;
    s_ota.ack_got = true;
    if (s_ota.state != OTA_SENDING || s_ota.ack_status != OTA_STATUS_OK)
        ESP_LOGD(TAG, "OTA : OTA_ACK recu (index 0x%04X, statut 0x%02X %s, fenetre %u)",
                 s_ota.ack_block, s_ota.ack_status, ota_status_text(s_ota.ack_status), s_ota.ack_window);
    s_last_radio_rx = millis();
    e->last_seen_ms = millis();
}

static void ota_advance(void)
{
    s_ota.seq++;
    s_ota.tries = 0;
    s_ota.waiting_ack = false;
}

static void ota_retry_or_fail(const char *why)
{
    s_ota.waiting_ack = false;
    uint16_t max_tries = (s_ota.state == OTA_WAITING) ? OTA_START_TRIES : OTA_MAX_RETRIES;
    if (s_ota.tries >= max_tries) {
        char m[112];
        if (s_ota.state == OTA_WAITING)
            snprintf(m, sizeof(m), "l'emetteur ne repond pas : est-il en mode mise a jour ? (%.40s)", why);
        else if (s_ota.state == OTA_SENDING)
            snprintf(m, sizeof(m), "bloc %u abandonne apres %u essais (%.50s)", s_ota.block, s_ota.tries, why);
        else
            snprintf(m, sizeof(m), "fin de mise a jour refusee (%.60s)", why);
        ota_finish(OTA_FAILED, m);
        return;
    }
    if (s_ota.state != OTA_WAITING) s_ota.total_retries++;
}

// NOT_STARTED : l'emetteur n'est plus en reception -> reprise a OTA_START
static void ota_restart(void)
{
    if (++s_ota.restarts > OTA_MAX_RESTARTS) {
        ota_finish(OTA_FAILED, "l'emetteur a quitte la mise a jour a plusieurs reprises");
        return;
    }
    ESP_LOGW(TAG, "OTA : l'emetteur n'est plus en reception -> reprise a OTA_START (%u/%u)",
             s_ota.restarts, OTA_MAX_RESTARTS);
    s_ota.state = OTA_WAITING;
    s_ota.block = 0; s_ota.tx_block = 0;
    s_ota.seq++;
    s_ota.tries = 0; s_ota.waiting_ack = false; s_ota.ack_got = false;
    s_ota.window = 1; s_ota.win_waiting = false; s_ota.win_fails = 0;
}

static void ota_window_plan(uint16_t next)
{
    uint32_t end = ((uint32_t)(next / s_ota.window) + 1) * s_ota.window - 1;
    if (end > (uint32_t)(s_ota.total_blocks - 1)) end = s_ota.total_blocks - 1;
    s_ota.win_end = (uint16_t)end;
    s_ota.win_next = next;
    s_ota.win_waiting = false;
    s_ota.next_tx_ms = millis() + OTA_BLOCK_GAP_MS;
}

static void ota_window_tick(void)
{
    // --- un OTA_ACK est arrive ---
    if (s_ota.ack_got) {
        s_ota.ack_got = false;
        if (s_ota.ack_status == OTA_STATUS_NOT_STARTED) { ota_restart(); return; }
        if (s_ota.ack_status == OTA_STATUS_BUSY) {
            if (++s_ota.win_fails > OTA_WIN_MAX_FAILS) { ota_finish(OTA_FAILED, "l'emetteur reste occupe"); return; }
            s_ota.win_next = s_ota.win_end;
            s_ota.win_waiting = false;
            s_ota.next_tx_ms = millis() + 500;
            return;
        }
        if (s_ota.ack_status != OTA_STATUS_OK) {
            char m[112];
            snprintf(m, sizeof(m), "bloc refuse par l'emetteur : %s", ota_status_text(s_ota.ack_status));
            ota_finish(OTA_FAILED, m);
            return;
        }
        uint16_t next = s_ota.ack_block;
        if (next > s_ota.total_blocks) next = s_ota.total_blocks;
        if (next > s_ota.block) {
            s_ota.block = next;
            s_ota.win_fails = 0;
        } else {
            s_ota.total_retries++;
            if (++s_ota.win_fails > OTA_WIN_MAX_FAILS) {
                char m[112];
                snprintf(m, sizeof(m), "aucune progression au bloc %u apres %u essais",
                         s_ota.block, (unsigned)(s_ota.win_fails - 1));
                ota_finish(OTA_FAILED, m);
                return;
            }
            ESP_LOGW(TAG, "OTA : fenetre a reprendre au bloc %u (essai %u)", next, s_ota.win_fails);
        }
        if (s_ota.block >= s_ota.total_blocks) {
            s_ota.state = OTA_ENDING;
            s_ota.tries = 0;
            s_ota.waiting_ack = false;
            return;
        }
        ota_window_plan(s_ota.block);
        return;
    }

    // --- attente de l'ACK de fin de fenetre : aucune emission ---
    if (s_ota.win_waiting) {
        if ((int32_t)(millis() - s_ota.ack_deadline) < 0) return;
        // Pas d'ACK : on ne renvoie QUE le bloc de fin de fenetre
        s_ota.total_retries++;
        if (++s_ota.win_fails > OTA_WIN_MAX_FAILS) {
            char m[112];
            snprintf(m, sizeof(m), "pas de reponse a la fenetre du bloc %u apres %u essais",
                     s_ota.block, (unsigned)(s_ota.win_fails - 1));
            ota_finish(OTA_FAILED, m);
            return;
        }
        ESP_LOGW(TAG, "OTA : pas d'ACK pour la fenetre %u..%u -> renvoi du bloc %u (essai %u)",
                 s_ota.block, s_ota.win_end, s_ota.win_end, s_ota.win_fails);
        s_ota.win_next = s_ota.win_end;
        s_ota.win_waiting = false;
        s_ota.next_tx_ms = millis();
        return;
    }

    // --- emission des blocs de la fenetre, espaces de OTA_BLOCK_GAP_MS ---
    if ((int32_t)(millis() - s_ota.next_tx_ms) < 0) return;
    s_ota.tx_block = s_ota.win_next;
    s_ota.tries = 0;
    ota_send_current();
    s_ota.seq++;                                            // un seq par trame (nonce)
    s_ota.tries = 0;
    s_ota.waiting_ack = false;
    if (s_ota.win_next >= s_ota.win_end) {
        s_ota.win_waiting = true;
        s_ota.ack_deadline = millis() + OTA_WIN_ACK_TIMEOUT_MS;
    } else {
        s_ota.win_next++;
        s_ota.next_tx_ms = millis() + OTA_BLOCK_GAP_MS;
    }
}

static void ota_tick(void)
{
    if (s_ota.state != OTA_WAITING && s_ota.state != OTA_SENDING && s_ota.state != OTA_ENDING) return;
    if (s_ota.preparing) return;                            // ota_begin() calcule le CRC

    if (s_ota.cancel_requested) {
        s_ota.cancel_requested = false;
        ota_finish(OTA_FAILED, "mise a jour annulee");
        return;
    }
    if (s_ota.slot < 0 || s_ota.slot >= LORA_MAX_EMITTERS || !s_em[s_ota.slot].valid) {
        ota_finish(OTA_FAILED, "emetteur supprime");
        return;
    }

    if (s_ota.state == OTA_SENDING && s_ota.window >= 2) { ota_window_tick(); return; }

    if (!s_ota.waiting_ack) {
        s_ota.tx_block = s_ota.block;                       // sans fenetre : un bloc, un ACK
        ota_send_current();
        return;
    }
    if (!s_ota.ack_got) {
        if ((int32_t)(millis() - s_ota.ack_deadline) >= 0) ota_retry_or_fail("pas de reponse");
        return;
    }

    s_ota.ack_got = false;
    if (s_ota.state == OTA_WAITING) {
        if (s_ota.ack_status == OTA_STATUS_OK) {
            ota_advance();
            s_ota.state = OTA_SENDING;
            if (!s_ota.start_ms) s_ota.start_ms = millis();
            // Fenetre RETENUE par l'emetteur : c'est elle qui fait foi
            s_ota.window = (s_ota.req_window >= 2 && s_ota.ack_window >= 2 && s_ota.ack_window <= OTA_WINDOW_MAX)
                           ? s_ota.ack_window : 1;
            s_ota.win_fails = 0;
            if (s_ota.window >= 2) ota_window_plan(0);
            ESP_LOGI(TAG, "OTA : emetteur pret, envoi de %u blocs, %s (fenetre %u)", s_ota.total_blocks,
                     s_ota.window >= 2 ? "un ACK par fenetre" : "un ACK par bloc", s_ota.window);
        } else {
            char m[112];
            snprintf(m, sizeof(m), "mise a jour refusee par l'emetteur : %s", ota_status_text(s_ota.ack_status));
            ota_finish(OTA_FAILED, m);
        }
    } else if (s_ota.state == OTA_SENDING) {                // sans fenetre
        if (s_ota.ack_status == OTA_STATUS_NOT_STARTED) { ota_restart(); return; }
        if (s_ota.ack_status == OTA_STATUS_OK && s_ota.ack_block == s_ota.block) {
            ota_advance();
            s_ota.block++;
        } else if (s_ota.ack_status == OTA_STATUS_OUT_OF_RANGE && s_ota.ack_block > s_ota.block &&
                   s_ota.ack_block < s_ota.total_blocks) {
            ESP_LOGW(TAG, "OTA : emetteur en avance (bloc %u attendu, %u envoye)", s_ota.ack_block, s_ota.block);
            ota_advance();
            s_ota.block = s_ota.ack_block;
        } else {
            ota_retry_or_fail(ota_status_text(s_ota.ack_status));
            return;
        }
        if (s_ota.block >= s_ota.total_blocks) s_ota.state = OTA_ENDING;
    } else {                                                // OTA_ENDING
        if (s_ota.ack_status == OTA_STATUS_OK) {
            ota_finish(OTA_DONE, "mise a jour terminee, l'emetteur redemarre");
        } else if (s_ota.ack_status == OTA_STATUS_NOT_STARTED) {
            ota_restart();
        } else {
            char m[112];
            snprintf(m, sizeof(m), "firmware rejete par l'emetteur : %s", ota_status_text(s_ota.ack_status));
            ota_finish(OTA_FAILED, m);
        }
    }
}

// ============================================================================
// Boucle radio (equivalent de loraReceiverLoop)
// ============================================================================

// Chien de garde de la reception : un emetteur parle toutes les 5 s, un
// silence d'une minute signifie une radio sortie de RX. Re-armer est sans
// effet de bord si tout va bien.
static void rx_watchdog(void)
{
    if (s_pairing_mode || s_awaiting_confirm) return;
    if (ota_active_locked()) return;
    if (lora_count_emitters() == 0) return;
    if (s_last_radio_rx == 0) return;
    uint32_t now = millis();
    if (now - s_last_radio_rx < 60000) return;
    if (now - s_last_wd_rearm < 15000) return;
    s_last_wd_rearm = now;
    set_channel(s_op_channel);
    set_sf(s_op_sf);
    start_rx();
    ESP_LOGW(TAG, "Silence de %lu s -> re-armement RX (canal %d, SF%d)",
             (unsigned long)((now - s_last_radio_rx) / 1000), s_op_channel, s_op_sf);
}

static void loop_once(void)
{
    // Demande d'appairage d'une route web : traitee ici, seul contexte qui pilote la radio
    if (s_pairing_requested) {
        s_pairing_requested = false;
        if (ota_active_locked()) ESP_LOGW(TAG, "Appairage refuse : mise a jour OTA en cours");
        else                     start_pairing_now();
    }

    if (!s_pairing_mode && !s_awaiting_confirm) ota_tick();

    rx_watchdog();

    // Re-armement de la RX toutes les 2 s pendant l'attente d'un PAIR_REQUEST :
    // sur ce montage un armement rate rendait la fenetre de 30 s muette
    if (s_pairing_mode && !s_awaiting_confirm && !sx_irq_pending() && millis() - s_last_pair_rearm > 2000) {
        s_last_pair_rearm = millis();
        set_channel(LORA_PAIR_CHANNEL);
        set_sf(LORA_PAIR_SF);
        start_rx();
    }

    if (s_pairing_mode) pairing_led_tick();
    else if (s_led_state) pairing_led_off();

    // Fin de la fenetre -> retour a la config operationnelle
    if (s_pairing_mode && (millis() - s_pairing_start > LORA_PAIR_WINDOW_MS)) {
        s_pairing_mode = false;
        s_awaiting_confirm = false;
        pairing_led_off();
        set_channel(s_op_channel);
        set_sf(s_op_sf);
        start_rx();
        ESP_LOGI(TAG, "Fenetre d'appairage fermee");
    }
    // Pas de CONFIRM : retour a l'ecoute du rendez-vous
    if (s_awaiting_confirm && (int32_t)(millis() - s_confirm_deadline) > 0) {
        s_awaiting_confirm = false;
        ESP_LOGW(TAG, "Pas de PAIR_CONFIRM recu (timeout)");
        if (s_pairing_mode) {
            set_channel(LORA_PAIR_CHANNEL);
            set_sf(LORA_PAIR_SF);
            start_rx();
        } else {
            set_channel(s_op_channel);
            set_sf(s_op_sf);
            start_rx();
        }
    }

    if (!sx_irq_pending()) return;

    // s_last_radio_rx n'est PAS mis a jour ici : DIO1 monte aussi sur du bruit
    uint8_t buf[SX_MAX_PAYLOAD];
    float rssi = 0, snr = 0;
    int len = sx_read_packet(buf, sizeof(buf), &rssi, &snr);
    if (len <= 0) { start_rx(); return; }
    s_rx_read_ms = millis();
    if ((buf[0] & 0x0F) != T_ESSENTIAL && (buf[0] & 0x0F) != T_EXTENDED) {
        ESP_LOGD(TAG, "RX trame type 0x%02X, %d octets, RSSI %.0f dBm, SNR %.1f dB",
                 buf[0], len, rssi, snr);
    }

    switch (buf[0] & 0x0F) {
    case T_PAIR_REQUEST:
        if (!handle_pair_request(buf, len)) start_rx();
        break;
    case T_PAIR_CONFIRM:  handle_pair_confirm(buf, len); start_rx(); break;
    case T_ESSENTIAL:
    case T_EXTENDED:
        // Apres une requete POLL, ne PAS relancer la reception : la radio y
        // est deja, et la reponse du ZLinky (~100 ms d'emission a SF11) arrive
        // pendant le decodage de la trame (chemin commun : device_data,
        // regles, MQTT, Matter, plusieurs centaines de ms). Relancer la
        // reception effacait la reponse recue ou en cours : l'identification
        // et les lectures a la demande n'aboutissaient jamais.
        if (!handle_data(buf, len, rssi, snr)) start_rx();
        break;
    case T_POLL_RESPONSE: handle_poll_response(buf, len); start_rx(); break;
    case T_OTA_ACK:       handle_ota_ack(buf, len); start_rx(); break;
    default:              start_rx(); break;
    }
}

static void dio1_isr(void *arg)
{
    (void)arg;
    BaseType_t hp = pdFALSE;
    if (s_task) vTaskNotifyGiveFromISR(s_task, &hp);
    if (hp) portYIELD_FROM_ISR();
}

static void lora_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!sx_irq_pending()) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LORA_LOOP_MS));
        if (s_ota.requested && !s_pairing_mode && !s_awaiting_confirm) ota_begin();
        if (lock(portMAX_DELAY)) {
            loop_once();
            unlock();
        }
    }
}

// ============================================================================
// API publique
// ============================================================================

esp_err_t lora_init(void)
{
    if (s_inited) return s_ready ? ESP_OK : (s_detected ? ESP_FAIL : ESP_ERR_NOT_FOUND);
    s_inited = true;
    s_lock = xSemaphoreCreateRecursiveMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    s_ota.slot = -1;                // s_ota est deja a zero (stockage statique)

    // lora.json est lu meme sans module : les fiches LoRa de /littlefs/db
    // restent reconnues (filtres Zigbee) si le module est retire
    load_config();

    if (!sx_detect(&s_chip_ver)) return ESP_ERR_NOT_FOUND;
    s_detected = true;

    // Config identique a l'emetteur : BW 406,25, CR 4/5, synchro 0x12, preambule 8
    esp_err_t err = sx_begin(CH_FREQ[s_op_channel], s_op_sf, LORA_PREAMBLE, LORA_TX_POWER_DBM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Initialisation radio en echec: %s", esp_err_to_name(err));
        return err;
    }
    s_cur_channel = s_op_channel;
    s_radio_sf = s_op_sf;

    // Les emetteurs appaires reapparaissent comme appareils ; une
    // identification interrompue est relancee, la version relue
    for (int i = 0; i < LORA_MAX_EMITTERS; i++) {
        emitter_t *e = &s_em[i];
        if (!e->valid) continue;
        e->ident_step = (e->device_id[0] && e->model[0]) ? 0 : (e->device_id[0] ? 2 : 1);
        e->ident_next_ms = millis();
        e->ver_step = 1;
        e->ver_next_ms = millis();
        e->short_addr = compute_short_addr(e->mac, i);
        ensure_device(e);
    }

    if (xTaskCreate(lora_task, "lora", LORA_TASK_STACK, NULL, LORA_TASK_PRIO, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Creation de la tache LoRa impossible");
        return ESP_ERR_NO_MEM;
    }
    err = sx_attach_isr(dio1_isr, NULL);
    if (err != ESP_OK) ESP_LOGW(TAG, "IRQ DIO1 indisponible (%s) : scrutation seule", esp_err_to_name(err));

    if (lock(portMAX_DELAY)) {
        start_rx();
        s_last_radio_rx = millis();     // arme le chien de garde des le demarrage
        unlock();
    }
    s_ready = true;
    ESP_LOGI(TAG, "Recepteur pret (canal %d, SF%d, %d emetteur(s))", s_op_channel, s_op_sf, lora_count_emitters());
    return ESP_OK;
}

bool lora_is_detected(void) { return s_detected; }
bool lora_is_ready(void) { return s_ready; }
uint16_t lora_chip_version(void) { return s_chip_ver; }

int lora_count_emitters(void)
{
    int n = 0;
    for (int i = 0; i < LORA_MAX_EMITTERS; i++) if (s_em[i].valid) n++;
    return n;
}

static void fill_info(int slot, lora_emitter_info_t *o)
{
    const emitter_t *e = &s_em[slot];
    memset(o, 0, sizeof(*o));
    o->slot = slot;
    o->ieee = mac_to_ieee(e->mac);
    strlcpy(o->device_id, e->device_id, sizeof(o->device_id));
    strlcpy(o->model, e->model, sizeof(o->model));
    o->identified = (e->ident_step == 0 && e->device_id[0] && e->model[0]);
    o->mode_known = e->mode_known;
    o->linky_mode = e->linky_mode;
    o->fw_known = e->fw_known;
    o->fw_version = e->fw_version;
    strlcpy(o->version, e->version, sizeof(o->version));
    o->rx_count = e->rx_count;
    o->missed = e->missed;
    o->rssi = e->last_rssi;
    o->snr = e->last_snr;
    o->age_s = e->rx_count ? (int32_t)((millis() - e->last_seen_ms) / 1000) : -1;
    o->last_seen = (uint32_t)e->last_seen_epoch;
    o->short_addr = e->short_addr;
    o->matter_ep = e->matter_ep;
}

int lora_get_emitters(lora_emitter_info_t *out, int max)
{
    if (!out || max <= 0 || !lock(LORA_LOCK_WAIT_MS)) return 0;
    int n = 0;
    for (int i = 0; i < LORA_MAX_EMITTERS && n < max; i++)
        if (s_em[i].valid) fill_info(i, &out[n++]);
    unlock();
    return n;
}

bool lora_get_emitter(int slot, lora_emitter_info_t *out)
{
    if (!out || slot < 0 || slot >= LORA_MAX_EMITTERS || !lock(LORA_LOCK_WAIT_MS)) return false;
    bool ok = s_em[slot].valid;
    if (ok) fill_info(slot, out);
    unlock();
    return ok;
}

int lora_find_slot_by_ieee(uint64_t ieee)
{
    // Lecture sans verrou : mac/valid ne changent qu'a l'appairage ou a la
    // suppression, et cette fonction sert de filtre dans les listes Zigbee
    for (int i = 0; i < LORA_MAX_EMITTERS; i++)
        if (s_em[i].valid && mac_to_ieee(s_em[i].mac) == ieee) return i;
    return -1;
}

bool lora_is_lora_device(uint64_t ieee) { return lora_find_slot_by_ieee(ieee) >= 0; }

esp_err_t lora_remove_emitter(int slot, bool remove_device_data)
{
    if (slot < 0 || slot >= LORA_MAX_EMITTERS) return ESP_ERR_INVALID_ARG;
    if (!lock(LORA_LOCK_WAIT_MS)) return ESP_ERR_TIMEOUT;
    if (!s_em[slot].valid) { unlock(); return ESP_ERR_NOT_FOUND; }
    if (ota_active_locked() && (s_ota.slot == slot || s_ota.req_slot == slot)) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t ieee = mac_to_ieee(s_em[slot].mac);
    char path[64];
    bool has_path = ota_path_locked(slot, path, sizeof(path));
    memset(&s_em[slot], 0, sizeof(s_em[slot]));
    esp_err_t err = save_config();
    unlock();

    if (has_path) remove(path);
    // Desappairer AVANT de supprimer la fiche : sinon la trame suivante la
    // recreerait (la Box enchainait deux requetes pour cela)
    if (remove_device_data) device_data_remove(ieee);
    ESP_LOGI(TAG, "Emetteur %016llX supprime (slot %d)", (unsigned long long)ieee, slot);
    if (s_removed_cb) s_removed_cb(ieee);
    return err;
}

esp_err_t lora_set_matter_endpoint(uint64_t ieee, uint16_t ep)
{
    if (!lock(LORA_LOCK_WAIT_MS)) return ESP_ERR_TIMEOUT;
    esp_err_t err = ESP_ERR_NOT_FOUND;
    for (int i = 0; i < LORA_MAX_EMITTERS; i++) {
        if (!s_em[i].valid || mac_to_ieee(s_em[i].mac) != ieee) continue;
        err = ESP_OK;
        if (s_em[i].matter_ep != ep) {
            s_em[i].matter_ep = ep;
            err = save_config();
        }
        break;
    }
    unlock();
    return err;
}

void lora_register_removed_cb(lora_removed_cb_t cb) { s_removed_cb = cb; }

esp_err_t lora_start_pairing(void)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (lora_ota_active()) return ESP_ERR_INVALID_STATE;
    s_pairing_requested = true;
    if (s_task) xTaskNotifyGive(s_task);
    return ESP_OK;
}

bool lora_pairing_state(int *seconds_left)
{
    if (seconds_left) *seconds_left = 0;
    if (!lock(LORA_LOCK_WAIT_MS)) return s_pairing_requested;
    bool active = s_pairing_requested || s_pairing_mode;
    if (s_pairing_mode && seconds_left) {
        long left = (long)LORA_PAIR_WINDOW_MS - (long)(millis() - s_pairing_start);
        *seconds_left = left > 0 ? (int)(left / 1000) : 0;
    } else if (s_pairing_requested && seconds_left) {
        *seconds_left = LORA_PAIR_WINDOW_S;
    }
    unlock();
    return active;
}

bool lora_take_pair_result(uint64_t *ieee, char *model, size_t model_len)
{
    if (!lock(LORA_LOCK_WAIT_MS)) return false;
    bool got = s_paired_pending;
    if (got) {
        s_paired_pending = false;
        if (ieee) *ieee = s_paired_ieee;
        if (model && model_len) strlcpy(model, s_paired_model, model_len);
    }
    unlock();
    return got;
}

esp_err_t lora_set_op_params(uint8_t channel, uint8_t sf)
{
    if (channel > 7 || sf < 7 || sf > 12) return ESP_ERR_INVALID_ARG;
    if (!lock(LORA_LOCK_WAIT_MS)) return ESP_ERR_TIMEOUT;
    s_op_channel = channel;
    s_op_sf = sf;
    // Bascule immediate du recepteur. Les emetteurs deja appaires restent sur
    // l'ancienne config jusqu'a un nouvel appairage (parametre RESEAU, sect. 4).
    // Le verrou garantit que la tache radio ne l'utilise pas en meme temps.
    if (s_ready && !s_pairing_mode && !s_awaiting_confirm && !ota_active_locked()) {
        set_channel(s_op_channel);
        set_sf(s_op_sf);
        start_rx();
        s_last_radio_rx = millis();
        ESP_LOGI(TAG, "Parametres reseau : canal %d, SF%d (re-appairer les emetteurs)", s_op_channel, s_op_sf);
    }
    esp_err_t err = save_config();
    unlock();
    return err;
}

uint8_t lora_get_channel(void) { return s_op_channel; }
uint8_t lora_get_sf(void) { return s_op_sf; }
uint16_t lora_channel_mhz(uint8_t channel) { return channel < LORA_CHANNEL_COUNT ? CH_FREQ[channel] : 0; }

esp_err_t lora_queue_poll(int slot, uint16_t cluster, uint16_t attr)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (!lock(LORA_LOCK_WAIT_MS)) return ESP_ERR_TIMEOUT;
    bool ok = queue_poll_locked(slot, cluster, attr);
    unlock();
    return ok ? ESP_OK : ESP_ERR_NOT_FOUND;
}

char *lora_poll_status_json(int slot)
{
    sb_t s;
    sb_init(&s, 192);
    if (!lock(LORA_LOCK_WAIT_MS)) { free(s.buf); return NULL; }
    if (slot < 0 || slot >= LORA_MAX_EMITTERS || !s_em[slot].valid) {
        sb_add(&s, "{\"state\":\"idle\"}");
    } else {
        const emitter_t *e = &s_em[slot];
        if (e->poll_pending) {
            sb_addf(&s, "{\"state\":\"pending\",\"cluster\":%u,\"attr\":%u,\"tries\":%u}",
                    e->poll_cluster, e->poll_attr, e->poll_retries);
        } else if (!e->poll_resp_valid) {
            sb_add(&s, "{\"state\":\"idle\"}");
        } else {
            const char *st = (e->poll_resp_status == POLL_OK) ? "ok" :
                             (e->poll_resp_status == POLL_UNKNOWN_ATTR) ? "unknown" : "bad";
            sb_addf(&s, "{\"state\":\"%s\",\"cluster\":%u,\"attr\":%u", st, e->poll_cluster, e->poll_attr);
            if (e->poll_resp_status == POLL_OK) {
                char v[40];
                format_poll_value(e->poll_resp_type, e->poll_resp_value, e->poll_resp_len, v, sizeof(v));
                sb_addf(&s, ",\"type\":%u,\"value\":", e->poll_resp_type);
                sb_str(&s, v);
            }
            sb_add(&s, "}");
        }
    }
    unlock();
    return sb_take(&s);
}

esp_err_t lora_ota_start(int slot, uint16_t version, uint8_t preamble, uint8_t sf,
                         uint8_t block_size, uint8_t window, char *err, size_t err_len)
{
    const char *why = NULL;
    char path[64];
    if (!s_ready) why = "radio LoRa non prete";
    else if (!lock(LORA_LOCK_WAIT_MS)) why = "radio LoRa occupee";
    else {
        if (ota_active_locked())                         why = "une mise a jour est deja en cours";
        else if (s_pairing_mode || s_pairing_requested)  why = "appairage en cours";
        else if (!ota_path_locked(slot, path, sizeof(path))) why = "emetteur inconnu";
        else {
            struct stat st;
            if (stat(path, &st) != 0)                    why = "aucun fichier firmware depose";
            else if (st.st_size == 0)                    why = "fichier firmware vide";
            else if ((uint32_t)st.st_size > LORA_OTA_MAX_FILE) why = "fichier firmware trop volumineux";
        }
        if (!why) {
            s_ota.req_slot = slot;
            s_ota.req_version = version;
            s_ota.req_preamble = (preamble >= 4 && preamble <= 64) ? preamble : 0;
            s_ota.req_sf = (sf >= 7 && sf <= 12) ? sf : 0;
            s_ota.req_block_size = (block_size >= 8 && block_size <= OTA_BLOCK_SIZE) ? block_size : 0;
            s_ota.req_window = (window == 0) ? OTA_WINDOW_DEFAULT : (window > OTA_WINDOW_MAX ? OTA_WINDOW_MAX : window);
            s_ota.cancel_requested = false;
            s_ota.requested = true;
        }
        unlock();
    }
    if (why) {
        if (err && err_len) strlcpy(err, why, err_len);
        return ESP_FAIL;
    }
    if (s_task) xTaskNotifyGive(s_task);
    return ESP_OK;
}

void lora_ota_cancel(void)
{
    if (!lock(LORA_LOCK_WAIT_MS)) return;
    if (s_ota.requested) {                                  // pas encore commence
        s_ota.requested = false;
        s_ota.state = OTA_FAILED;
        s_ota.slot = s_ota.req_slot;
        s_ota.end_ms = millis();
        strlcpy(s_ota.msg, "mise a jour annulee", sizeof(s_ota.msg));
    } else if (ota_active_locked()) {
        // Pris en compte par ota_tick(), ou par ota_begin() a la fin du calcul
        // du CRC (le fichier ne doit pas etre ferme pendant sa lecture)
        s_ota.cancel_requested = true;
    }
    unlock();
}

bool lora_ota_active(void)
{
    if (!lock(LORA_LOCK_WAIT_MS)) return true;
    bool a = ota_active_locked();
    unlock();
    return a;
}

bool lora_ota_file_path(int slot, char *out, size_t len)
{
    if (!lock(LORA_LOCK_WAIT_MS)) return false;
    bool ok = ota_path_locked(slot, out, len);
    unlock();
    return ok;
}

bool lora_ota_path_for_id(const char *id, char *out, size_t len)
{
    // "lora_" + 16 hex : jamais de chemin construit depuis le texte recu, on
    // repart de la MAC de l'emetteur appaire correspondant
    uint8_t mac[MAC_SIZE];
    if (!id || strncmp(id, "lora_", 5) != 0 || !parse_hex(id + 5, mac, MAC_SIZE)) return false;
    int slot = lora_find_slot_by_ieee(mac_to_ieee(mac));
    return slot >= 0 && lora_ota_file_path(slot, out, len);
}

char *lora_ota_status_json(int slot)
{
    static const char *names[] = {"idle", "waiting", "sending", "ending", "done", "error"};
    sb_t s;
    sb_init(&s, 384);
    if (!lock(LORA_LOCK_WAIT_MS)) { free(s.buf); return NULL; }
    uint8_t st = s_ota.requested ? (uint8_t)OTA_WAITING : s_ota.state;
    int cur_slot = (st == OTA_IDLE) ? -1 : (s_ota.requested ? s_ota.req_slot : s_ota.slot);
    uint32_t ref = (st == OTA_DONE || st == OTA_FAILED) ? s_ota.end_ms : millis();
    uint32_t elapsed = s_ota.start_ms ? (ref - s_ota.start_ms) / 1000 : 0;
    long eta = -1;
    if (st == OTA_SENDING && s_ota.block > 0 && elapsed > 0)
        eta = (long)(((uint64_t)(s_ota.total_blocks - s_ota.block) * elapsed) / s_ota.block);
    long since = (st == OTA_DONE || st == OTA_FAILED) ? (long)((millis() - s_ota.end_ms) / 1000) : -1L;
    sb_addf(&s, "{\"state\":\"%s\",\"slot\":%d,\"block\":%u,\"total\":%u,\"pct\":%d,\"tries\":%u,"
                "\"retries\":%lu,\"window\":%u,\"elapsed\":%lu,\"eta\":%ld,\"since\":%ld,\"msg\":",
            names[st <= OTA_FAILED ? st : 0], cur_slot, s_ota.block, s_ota.total_blocks,
            s_ota.total_blocks ? (int)((100UL * s_ota.block) / s_ota.total_blocks) : 0,
            s_ota.tries, (unsigned long)s_ota.total_retries, s_ota.window,
            (unsigned long)elapsed, eta, since);
    sb_str(&s, s_ota.msg);
    long fsz = 0;
    char path[64];
    if (ota_path_locked(slot, path, sizeof(path))) {
        struct stat fst;
        if (stat(path, &fst) == 0) fsz = (long)fst.st_size;
    }
    sb_addf(&s, ",\"file\":%ld}", fsz);
    unlock();
    return sb_take(&s);
}

void lora_register_report_cb(lora_report_cb_t cb) { s_report_cb = cb; }

// ----------------------------------------------------------------------------
// Exports JSON
// ----------------------------------------------------------------------------

static int pdr_of(const emitter_t *e)
{
    uint32_t tot = e->rx_count + e->missed;
    return tot ? (int)((100ULL * e->rx_count) / tot) : 0;
}

static void sb_emitter(sb_t *s, int slot)
{
    const emitter_t *e = &s_em[slot];
    uint64_t ieee = mac_to_ieee(e->mac);
    char mh[17];
    mac_hex(e->mac, mh);
    const device_data_t *dd = device_data_find(ieee);
    long age = e->rx_count ? (long)((millis() - e->last_seen_ms) / 1000) : -1;
    sb_addf(s, "{\"slot\":%d,\"ieee\":\"%s\",\"mac\":\"%s\",\"shortAddr\":%u,\"deviceId\":", slot, mh, mh, e->short_addr);
    sb_str(s, e->device_id);
    sb_add(s, ",\"model\":");
    sb_str(s, e->model);
    sb_add(s, ",\"alias\":");
    sb_str(s, dd ? dd->info.alias : "");
    sb_add(s, ",\"version\":");
    sb_str(s, e->version);
    sb_addf(s, ",\"identified\":%s,\"fwKnown\":%s,\"fwVersion\":%u,\"rx\":%lu,\"missed\":%lu,\"pdr\":%d,"
               "\"rssi\":%.0f,\"snr\":%.1f,\"age\":%ld,\"lastSeen\":%lu,\"modeKnown\":%s,\"linkyMode\":%u,"
               "\"pollPending\":%s,\"hasData\":%s}",
            (e->ident_step == 0 && e->device_id[0] && e->model[0]) ? "true" : "false",
            e->fw_known ? "true" : "false", e->fw_version,
            (unsigned long)e->rx_count, (unsigned long)e->missed, pdr_of(e),
            (double)e->last_rssi, (double)e->last_snr, age, (unsigned long)e->last_seen_epoch,
            e->mode_known ? "true" : "false", e->linky_mode,
            e->poll_pending ? "true" : "false", dd ? "true" : "false");
}

char *lora_status_json(void)
{
    sb_t s;
    sb_init(&s, 1024);
    if (!lock(LORA_LOCK_WAIT_MS)) { free(s.buf); return NULL; }
    int left = 0;
    bool pairing = s_pairing_requested || s_pairing_mode;
    if (s_pairing_mode) {
        long l = (long)LORA_PAIR_WINDOW_MS - (long)(millis() - s_pairing_start);
        left = l > 0 ? (int)(l / 1000) : 0;
    } else if (s_pairing_requested) {
        left = LORA_PAIR_WINDOW_S;
    }
    sb_addf(&s, "{\"detected\":%s,\"ready\":%s,\"chip\":\"%04X\",\"channel\":%u,\"sf\":%u,"
                "\"pairing\":%s,\"pairLeft\":%d,\"otaActive\":%s,\"maxEmitters\":%d,\"count\":%d,"
                "\"pairChannel\":%d,\"pairWindow\":%d,\"channels\":[",
            s_detected ? "true" : "false", s_ready ? "true" : "false", s_chip_ver,
            s_op_channel, s_op_sf, pairing ? "true" : "false", left,
            ota_active_locked() ? "true" : "false", LORA_MAX_EMITTERS, lora_count_emitters(),
            LORA_PAIR_CHANNEL, LORA_PAIR_WINDOW_S);
    for (int c = 0; c < LORA_CHANNEL_COUNT; c++) sb_addf(&s, "%s%u", c ? "," : "", CH_FREQ[c]);
    sb_add(&s, "],\"emitters\":[");
    bool first = true;
    for (int i = 0; i < LORA_MAX_EMITTERS; i++) {
        if (!s_em[i].valid) continue;
        if (!first) sb_add(&s, ",");
        first = false;
        sb_emitter(&s, i);
    }
    sb_add(&s, "]}");
    unlock();
    return sb_take(&s);
}

// Valeurs brutes (hex) d'une fiche, au format de /api/zigbee/devices/full
static void sb_values(sb_t *s, const device_data_t *dd)
{
    sb_add(s, "{");
    for (int ci = 0; ci < dd->cluster_count; ci++) {
        const devdata_cluster_t *cl = &dd->clusters[ci];
        sb_addf(s, "%s\"%04X\":{", ci ? "," : "", cl->cluster_id);
        for (int ai = 0; ai < cl->attr_count; ai++) {
            sb_addf(s, "%s\"%u\":", ai ? "," : "", cl->attrs[ai].attr_id);
            sb_str(s, cl->attrs[ai].value);
        }
        sb_add(s, "}");
    }
    sb_add(s, "}");
}

// Etats du template qui ont une valeur : la page n'a plus qu'a appliquer
// coefficient et unite (memes metadonnees que pour un appareil Zigbee)
static void sb_states(sb_t *s, uint64_t ieee)
{
    const devdata_template_t *t = device_data_get_template(ieee);
    sb_add(s, "[");
    bool first = true;
    if (t && t->loaded) {
        for (int i = 0; i < t->state_count; i++) {
            const devdata_template_state_t *st = &t->states[i];
            const char *v = device_data_get_value_ptr(ieee, st->cluster, st->attribute);
            if (!v || !v[0]) continue;
            if (!first) sb_add(s, ",");
            first = false;
            sb_add(s, "{\"name\":");
            sb_str(s, st->name);
            sb_addf(s, ",\"cluster\":\"%04X\",\"attr\":%u,\"coef\":%g,\"visible\":%s,\"type\":",
                    st->cluster, st->attribute, (double)st->coefficient, st->visible ? "true" : "false");
            sb_str(s, st->type);
            sb_add(s, ",\"unit\":");
            sb_str(s, st->unit);
            sb_add(s, ",\"value\":");
            sb_str(s, v);
            sb_add(s, "}");
        }
    }
    sb_add(s, "]");
}

char *lora_devices_json(void)
{
    sb_t s;
    sb_init(&s, 4096);
    if (!lock(LORA_LOCK_WAIT_MS)) { free(s.buf); return NULL; }
    sb_add(&s, "[");
    bool first = true;
    for (int i = 0; i < LORA_MAX_EMITTERS; i++) {
        const emitter_t *e = &s_em[i];
        if (!e->valid) continue;
        uint64_t ieee = mac_to_ieee(e->mac);
        const device_data_t *dd = device_data_find(ieee);
        char mh[17];
        mac_hex(e->mac, mh);
        long age = e->rx_count ? (long)((millis() - e->last_seen_ms) / 1000) : -1;
        if (!first) sb_add(&s, ",");
        first = false;
        sb_addf(&s, "{\"ieee\":\"%s\",\"shortAddr\":%u,\"deviceId\":%d,\"manufacturer\":\"LiXee\",\"model\":",
                mh, e->short_addr, atoi(e->device_id));
        sb_str(&s, e->model);
        sb_add(&s, ",\"alias\":");
        sb_str(&s, dd ? dd->info.alias : "");
        // Endpoint Matter memorise (lora.json), -1 si l'emetteur n'a jamais ete ponte
        sb_addf(&s, ",\"active\":%s,\"lqi\":0,\"endpoint\":1,\"matterEndpoint\":%d,\"radio\":\"lora\","
                    "\"inClusters\":[],\"lora\":",
                (age >= 0 && age < 300) ? "true" : "false", e->matter_ep ? (int)e->matter_ep : -1);
        sb_emitter(&s, i);
        if (dd) {
            sb_add(&s, ",\"status\":");
            sb_str(&s, dd->info.status);
            sb_add(&s, ",\"values\":");
            sb_values(&s, dd);
            sb_add(&s, ",\"states\":");
            sb_states(&s, ieee);
        }
        sb_add(&s, "}");
    }
    sb_add(&s, "]");
    unlock();
    return sb_take(&s);
}

void lora_add_devices_json(struct cJSON *array)
{
    if (!array || !lock(LORA_LOCK_WAIT_MS)) return;
    for (int i = 0; i < LORA_MAX_EMITTERS; i++) {
        const emitter_t *e = &s_em[i];
        if (!e->valid) continue;
        uint64_t ieee = mac_to_ieee(e->mac);
        const device_data_t *dd = device_data_find(ieee);
        char mh[17];
        mac_hex(e->mac, mh);
        long age = e->rx_count ? (long)((millis() - e->last_seen_ms) / 1000) : -1;
        cJSON *o = cJSON_CreateObject();
        if (!o) break;
        cJSON_AddStringToObject(o, "ieee", mh);
        cJSON_AddNumberToObject(o, "shortAddr", e->short_addr);
        cJSON_AddNumberToObject(o, "deviceId", atoi(e->device_id));
        cJSON_AddStringToObject(o, "manufacturer", "LiXee");
        cJSON_AddStringToObject(o, "model", e->model);
        cJSON_AddStringToObject(o, "alias", dd ? dd->info.alias : "");
        cJSON_AddBoolToObject(o, "active", age >= 0 && age < 300);
        cJSON_AddNumberToObject(o, "lqi", 0);
        cJSON_AddNumberToObject(o, "endpoint", 1);
        cJSON_AddNumberToObject(o, "matterEndpoint", e->matter_ep ? (int)e->matter_ep : -1);
        cJSON_AddStringToObject(o, "radio", "lora");
        cJSON_AddItemToObject(o, "inClusters", cJSON_CreateArray());
        cJSON *lo = cJSON_AddObjectToObject(o, "lora");
        if (lo) {
            cJSON_AddNumberToObject(lo, "slot", i);
            cJSON_AddNumberToObject(lo, "rssi", e->last_rssi);
            cJSON_AddNumberToObject(lo, "snr", e->last_snr);
            cJSON_AddNumberToObject(lo, "pdr", pdr_of(e));
            cJSON_AddNumberToObject(lo, "age", age);
            cJSON_AddStringToObject(lo, "version", e->version);
        }
        if (dd) {
            cJSON *values = cJSON_AddObjectToObject(o, "values");
            for (int ci = 0; values && ci < dd->cluster_count; ci++) {
                const devdata_cluster_t *cl = &dd->clusters[ci];
                char key[12];
                snprintf(key, sizeof(key), "%04X", (unsigned)cl->cluster_id);
                cJSON *co = cJSON_AddObjectToObject(values, key);
                for (int ai = 0; co && ai < cl->attr_count; ai++) {
                    char ak[12];
                    snprintf(ak, sizeof(ak), "%u", (unsigned)cl->attrs[ai].attr_id);
                    cJSON_AddStringToObject(co, ak, cl->attrs[ai].value);
                }
            }
            if (dd->info.alias[0]) cJSON_AddStringToObject(o, "dataAlias", dd->info.alias);
            cJSON_AddStringToObject(o, "status", dd->info.status);
        }
        cJSON_AddItemToArray(array, o);
    }
    unlock();
}
