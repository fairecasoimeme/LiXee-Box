// ============================================================================
// Carte materielle : identification (voir board.h)
// ============================================================================

#include "board.h"
#include "status_led.h"

#include "driver/gpio.h"
#include "esp_efuse.h"
#include "esp_log.h"

static const char *TAG = "BOARD";

// eFuse BLOCK_USR_DATA : "LX", produit (BOARD_*), revision materielle
#define EFUSE_MAGIC0        'L'
#define EFUSE_MAGIC1        'X'

static board_type_t s_type = BOARD_ZIWIFI32;
static board_source_t s_source = BOARD_SRC_DEFAULT;

static bool read_efuse(board_type_t *type)
{
    uint8_t d[4] = {};
    if (esp_efuse_read_block(EFUSE_BLK_USER_DATA, d, 0, sizeof(d) * 8) != ESP_OK) return false;
    if (d[0] != EFUSE_MAGIC0 || d[1] != EFUSE_MAGIC1) return false;
    if (d[2] != BOARD_ZIWIFI32 && d[2] != BOARD_PRO) {
        ESP_LOGW(TAG, "eFuse : produit inconnu %u", d[2]);
        return false;
    }
    *type = (board_type_t)d[2];
    ESP_LOGI(TAG, "eFuse : produit %u, revision %u", d[2], d[3]);
    return true;
}

void board_init(void)
{
    board_type_t t;
    if (read_efuse(&t)) {
        s_type = t;
        s_source = BOARD_SRC_EFUSE;
    }

    // Presence de l'USB 5 V : entree sans tirage (diviseur 120 k / 200 k)
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << BOARD_USB_SENSE_GPIO;
    io.mode = GPIO_MODE_INPUT;
    gpio_config(&io);

    ESP_LOGI(TAG, "Carte : %s (%s)", board_name(), board_source_name());
}

board_type_t board_type(void) { return s_type; }
board_source_t board_source(void) { return s_source; }

const char *board_id(void) { return s_type == BOARD_PRO ? "pro" : "ziwifi32"; }
const char *board_name(void) { return s_type == BOARD_PRO ? "LiXee-Box Pro" : "ZiWiFi32"; }

const char *board_source_name(void)
{
    switch (s_source) {
    case BOARD_SRC_EFUSE:    return "efuse";
    case BOARD_SRC_DETECTED: return "detected";
    default:                 return "default";
    }
}

bool board_usb_powered(void)
{
    if (s_type != BOARD_PRO) return true;
    return gpio_get_level((gpio_num_t)BOARD_USB_SENSE_GPIO) != 0;
}

void board_set_zigate_isp(bool responded)
{
    if (!responded) {
        if (s_type == BOARD_PRO)
            ESP_LOGW(TAG, "Bootloader ISP du JN5189 muet sur une %s", board_name());
        return;
    }
    if (s_type == BOARD_PRO) return;
    s_type = BOARD_PRO;
    s_source = BOARD_SRC_DETECTED;
    ESP_LOGI(TAG, "Bootloader ISP du JN5189 joint par GPIO %d/%d : carte %s",
             BOARD_ZIGATE_RESET_GPIO, BOARD_ZIGATE_ISP_GPIO, board_name());
    status_led_board_changed();
}

bool board_has_zigate_isp(void)
{
    return s_type == BOARD_PRO;
}
