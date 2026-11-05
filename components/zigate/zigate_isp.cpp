// ============================================================================
// Programmation du JN5189 par son bootloader ISP (voir zigate_isp.h)
//
// Portage de zigate_flasher.cpp de la LiXee-Box 2.x (desactive sur la
// ZiWiFi32, dont seul le FTDI pilote le reset et l'entree ISP).
// Trame : drapeaux(1) longueur totale(2, gros-boutiste) type(1) donnees
// CRC32(4, gros-boutiste, zlib, sur tout ce qui precede). Reponse : meme
// forme, le premier octet des donnees est le statut (0 = OK). Adresses et
// longueurs des commandes memoire en petit-boutiste.
// ============================================================================

#include "zigate_isp.h"
#include "zigate_driver.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ZIGATE_ISP";

enum {
    ISP_RESET_REQUEST      = 0x14,
    ISP_GET_CHIPID_REQUEST = 0x32,
    ISP_MEM_OPEN_REQUEST   = 0x40,
    ISP_MEM_ERASE_REQUEST  = 0x42,
    ISP_MEM_READ_REQUEST   = 0x46,
    ISP_MEM_WRITE_REQUEST  = 0x48,
    ISP_MEM_CLOSE_REQUEST  = 0x4A,
    ISP_MEM_INFO_REQUEST   = 0x4C,
    ISP_UNLOCK_REQUEST     = 0x4E,
};

#define ISP_BLOCK          512          // taille d'ecriture / lecture
#define ISP_FRAME_MAX      (ISP_BLOCK + 32)
#define ISP_FLASH_INDEX    0            // memoire 0 : flash interne
#define ISP_FLASH_DEFAULT  (632 * 1024)
#define ISP_ACCESS_RWE     0x07         // lecture, ecriture, effacement

// Trames en PSRAM : ~1 Ko chacune, hors de la pile de l'appelant
static uint8_t *s_tx = NULL;
static uint8_t *s_rx = NULL;

static bool frames_alloc(void)
{
    if (!s_tx) s_tx = (uint8_t *)heap_caps_malloc(ISP_FRAME_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_rx) s_rx = (uint8_t *)heap_caps_malloc(ISP_FRAME_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_tx && s_rx;
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/**
 * Envoie une requete et attend la reponse (type + 1). Renvoie le statut du
 * bootloader (0 = OK), -1 sans reponse valide. data/data_len : donnees de la
 * reponse apres le statut.
 */
static int isp_request(uint8_t type, const uint8_t *hdr, size_t hdr_len,
                       const uint8_t *payload, size_t payload_len, uint32_t timeout_ms,
                       const uint8_t **data, size_t *data_len)
{
    size_t n = 4 + hdr_len + payload_len + 4;
    if (n > ISP_FRAME_MAX) return -1;
    s_tx[0] = 0x00;
    s_tx[1] = n >> 8;
    s_tx[2] = n;
    s_tx[3] = type;
    if (hdr_len) memcpy(s_tx + 4, hdr, hdr_len);
    if (payload_len) memcpy(s_tx + 4 + hdr_len, payload, payload_len);
    uint32_t crc = esp_rom_crc32_le(0, s_tx, n - 4);
    s_tx[n - 4] = crc >> 24; s_tx[n - 3] = crc >> 16; s_tx[n - 2] = crc >> 8; s_tx[n - 1] = crc;

    uart_flush_input(ZIGATE_UART_NUM);
    uart_write_bytes(ZIGATE_UART_NUM, s_tx, n);

    int64_t end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    size_t got = 0, want = 3;
    while (got < want) {
        int64_t left_us = end - esp_timer_get_time();
        if (left_us <= 0) return -1;
        int r = uart_read_bytes(ZIGATE_UART_NUM, s_rx + got, want - got,
                                pdMS_TO_TICKS(left_us / 1000 + 1));
        if (r > 0) got += r;
        if (got == 3 && want == 3) {
            want = ((size_t)s_rx[1] << 8) | s_rx[2];
            if (s_rx[0] != 0x00 || want < 9 || want > ISP_FRAME_MAX) return -1;
        }
    }
    uint32_t rx_crc = ((uint32_t)s_rx[want - 4] << 24) | ((uint32_t)s_rx[want - 3] << 16) |
                      ((uint32_t)s_rx[want - 2] << 8) | s_rx[want - 1];
    if (rx_crc != esp_rom_crc32_le(0, s_rx, want - 4)) {
        ESP_LOGW(TAG, "Reponse 0x%02X : CRC invalide", s_rx[3]);
        return -1;
    }
    if (s_rx[3] == (uint8_t)(type | 0x80)) {
        // Refus du bootloader : type de la requete | 0x80, puis le code
        ESP_LOGW(TAG, "Requete 0x%02X refusee (code 0x%02X)", type, s_rx[4]);
        return s_rx[4] ? s_rx[4] : 0xF0;
    }
    if (s_rx[3] != (uint8_t)(type + 1)) {
        ESP_LOGW(TAG, "Reponse 0x%02X inattendue (requete 0x%02X)", s_rx[3], type);
        return -1;
    }
    if (data) *data = s_rx + 5;
    if (data_len) *data_len = want - 9;
    return s_rx[4];
}

/** Reset du JN5189, entree ISP au niveau bas (bootloader) ou relachee (firmware). */
static void jn5189_reset(bool isp)
{
    if (isp) {
        gpio_set_level((gpio_num_t)ZIGATE_FLASH_PIN, 0);
        gpio_set_direction((gpio_num_t)ZIGATE_FLASH_PIN, GPIO_MODE_OUTPUT);
    } else {
        gpio_set_direction((gpio_num_t)ZIGATE_FLASH_PIN, GPIO_MODE_INPUT);
    }
    gpio_set_level((gpio_num_t)ZIGATE_RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)ZIGATE_RESET_PIN, 1);
    if (isp) {
        vTaskDelay(pdMS_TO_TICKS(100));      // entree ISP lue au relachement du reset
        gpio_set_direction((gpio_num_t)ZIGATE_FLASH_PIN, GPIO_MODE_INPUT);
        vTaskDelay(pdMS_TO_TICKS(150));      // bootloader pret
    }
}

/**
 * Deverrouillage : mode 0 (presence du bootloader) suffit pour la sonde ;
 * les commandes memoire demandent le mode 1 avec la cle par defaut de NXP
 * (meme sequence que le flasheur de la 2.x).
 */
static bool isp_unlock(int attempts)
{
    const uint8_t mode = 0x00;
    for (int attempt = 0; attempt < attempts; attempt++) {
        if (isp_request(ISP_UNLOCK_REQUEST, &mode, 1, NULL, 0, 300, NULL, NULL) >= 0) return true;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return false;
}

static bool isp_unlock_key(void)
{
    static const uint8_t key[16] = {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    };
    const uint8_t mode = 0x01;
    int st = isp_request(ISP_UNLOCK_REQUEST, &mode, 1, key, sizeof(key), 500, NULL, NULL);
    ESP_LOGI(TAG, "Deverrouillage avec cle : %d", st);
    return st == 0;
}

bool zigate_isp_probe(void)
{
    if (!frames_alloc()) return false;
    jn5189_reset(true);
    bool ok = isp_unlock(2);    // ~0,6 s de plus au demarrage d'une ZiWiFi32
    ESP_LOGI(TAG, "Sonde ISP du JN5189 : %s", ok ? "bootloader present" : "pas de reponse");
    return ok;
}

bool zigate_isp_image_valid(const uint8_t *img, size_t len, char *err, size_t err_len)
{
    if (!img || len < 1024 || len > ZIGATE_ISP_IMAGE_MAX) {
        snprintf(err, err_len, "Taille de firmware invalide (%u octets)", (unsigned)len);
        return false;
    }
    // Table des vecteurs Cortex-M4 : pile initiale en RAM (0x04000000),
    // vecteur de reset en mode Thumb, dans l'image
    uint32_t sp = get_le32(img), reset = get_le32(img + 4);
    if ((sp & 0xFF000000u) != 0x04000000u || !(reset & 1) || (reset & ~1u) >= len) {
        snprintf(err, err_len, "Ce fichier n'est pas un firmware JN5189 (.bin)");
        return false;
    }
    return true;
}

static void report(zigate_isp_progress_cb_t cb, int percent, const char *step)
{
    if (cb) cb(percent, step);
}

esp_err_t zigate_isp_flash(const uint8_t *img, size_t len, zigate_isp_progress_cb_t cb,
                           char *err, size_t err_len)
{
    if (!zigate_isp_image_valid(img, len, err, err_len)) return ESP_ERR_INVALID_ARG;
    if (!frames_alloc()) {
        snprintf(err, err_len, "Memoire insuffisante");
        return ESP_ERR_NO_MEM;
    }
    esp_err_t e = zigate_uart_suspend(true);
    if (e != ESP_OK) {
        snprintf(err, err_len, "Liaison Zigbee occupee");
        return e;
    }

    esp_err_t ret = ESP_FAIL;
    uint8_t h[10];
    const uint8_t *d = NULL;
    size_t dl = 0;
    int st;
    uint32_t base = 0, size = ISP_FLASH_DEFAULT, sector = ISP_BLOCK;
    uint8_t handle = ISP_FLASH_INDEX;
    bool opened = false, verify = true;

    report(cb, 1, "Passage du module en programmation");
    jn5189_reset(true);
    if (!isp_unlock(3)) {
        snprintf(err, err_len, "Le bootloader du module Zigbee ne repond pas");
        goto out;
    }
    isp_unlock_key();            // sans effet si le mode 0 suffit deja
    if (isp_request(ISP_GET_CHIPID_REQUEST, NULL, 0, NULL, 0, 500, &d, &dl) == 0 && dl >= 8)
        ESP_LOGI(TAG, "Puce 0x%08lX, bootloader 0x%08lX",
                 (unsigned long)((d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3]),
                 (unsigned long)((d[4] << 24) | (d[5] << 16) | (d[6] << 8) | d[7]));

    // Memoire 0 : base, taille, secteur (valeurs par defaut si non fournies)
    h[0] = ISP_FLASH_INDEX;
    if (isp_request(ISP_MEM_INFO_REQUEST, h, 1, NULL, 0, 1000, &d, &dl) == 0 && dl >= 13) {
        base = get_le32(d + 1);
        size = get_le32(d + 5);
        sector = get_le32(d + 9);
        if (!sector || sector > 64 * 1024) sector = ISP_BLOCK;
        ESP_LOGI(TAG, "Flash : base 0x%08lX, %lu octets, secteur %lu",
                 (unsigned long)base, (unsigned long)size, (unsigned long)sector);
    }
    if (len > size) {
        snprintf(err, err_len, "Firmware trop grand pour le module (%u > %lu octets)",
                 (unsigned)len, (unsigned long)size);
        goto out;
    }

    h[0] = ISP_FLASH_INDEX;
    h[1] = ISP_ACCESS_RWE;
    st = isp_request(ISP_MEM_OPEN_REQUEST, h, 2, NULL, 0, 1000, &d, &dl);
    if (st != 0) {
        h[1] = 0x06;                          // ecriture + effacement, sans relecture
        verify = false;
        st = isp_request(ISP_MEM_OPEN_REQUEST, h, 2, NULL, 0, 1000, &d, &dl);
    }
    if (st != 0) {
        snprintf(err, err_len, "Ouverture de la flash du module refusee (0x%02X)", st & 0xFF);
        goto out;
    }
    if (dl >= 1) handle = d[0];
    opened = true;

    // Effacement de la zone de l'image seulement (la PDM reste en place)
    report(cb, 3, "Effacement");
    {
        uint32_t erase_len = (len + sector - 1) / sector * sector;
        h[0] = handle; h[1] = 0;
        put_le32(h + 2, base);
        put_le32(h + 6, erase_len);
        st = isp_request(ISP_MEM_ERASE_REQUEST, h, 10, NULL, 0, 20000, NULL, NULL);
        if (st != 0) {
            snprintf(err, err_len, "Effacement refuse (%d)", st);
            goto out;
        }
    }

    for (size_t off = 0; off < len; off += ISP_BLOCK) {
        size_t n = len - off < ISP_BLOCK ? len - off : ISP_BLOCK;
        h[0] = handle; h[1] = 0;
        put_le32(h + 2, base + off);
        put_le32(h + 6, n);
        st = isp_request(ISP_MEM_WRITE_REQUEST, h, 10, img + off, n, 1000, NULL, NULL);
        if (st != 0) st = isp_request(ISP_MEM_WRITE_REQUEST, h, 10, img + off, n, 1000, NULL, NULL);
        if (st != 0) {
            snprintf(err, err_len, "Ecriture refusee a 0x%05X (%d)", (unsigned)off, st);
            goto out;
        }
        if ((off / ISP_BLOCK) % 16 == 0) report(cb, 5 + (int)(off * 65 / len), "Ecriture");
    }

    if (verify) {
        for (size_t off = 0; off < len; off += ISP_BLOCK) {
            size_t n = len - off < ISP_BLOCK ? len - off : ISP_BLOCK;
            h[0] = handle; h[1] = 0;
            put_le32(h + 2, base + off);
            put_le32(h + 6, n);
            st = isp_request(ISP_MEM_READ_REQUEST, h, 10, NULL, 0, 1000, &d, &dl);
            if (st == 0xFF && off == 0) {        // relecture non prise en charge
                ESP_LOGW(TAG, "Relecture non prise en charge : pas de verification");
                break;
            }
            if (st != 0 || dl < n || memcmp(d, img + off, n) != 0) {
                snprintf(err, err_len, "Verification echouee a 0x%05X", (unsigned)off);
                goto out;
            }
            if ((off / ISP_BLOCK) % 16 == 0) report(cb, 70 + (int)(off * 28 / len), "Verification");
        }
    }
    ret = ESP_OK;

out:
    if (opened) {
        h[0] = handle;
        isp_request(ISP_MEM_CLOSE_REQUEST, h, 1, NULL, 0, 500, NULL, NULL);
    }
    report(cb, 99, "Redemarrage du module");
    jn5189_reset(false);                          // firmware, ISP relachee
    vTaskDelay(pdMS_TO_TICKS(1000));
    zigate_uart_suspend(false);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Firmware du module Zigbee programme (%u octets)", (unsigned)len);
        report(cb, 100, "Termine");
    } else {
        ESP_LOGE(TAG, "Programmation echouee : %s", err);
    }
    return ret;
}
