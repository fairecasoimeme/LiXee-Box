// ============================================================================
// LED d'etat : LED simple (cartes a FTDI) ou WS2812 (Pro), voir status_led.h
// ============================================================================

#include "status_led.h"
#include "board.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"

#include <ctype.h>
#include <string.h>

static const char *TAG = "LED";

typedef struct {
    uint32_t half_period_ms;   // 0 : fixe
    uint8_t r, g, b;
} led_pattern_t;

// La WS2812 eclaire le logo a travers le boitier : couleurs systeme a 70/255,
// sous les 30 % conseilles. Utilisateur et test : couleur fournie.
static led_pattern_t s_pat[LED_STATE_COUNT] = {
    { 0,    0, 0, 0 },     // LED_STATE_USER : status_led_user()
    { 1000, 0, 0, 70 },    // LED_STATE_BLE_PROV : bleu
    { 300,  0, 70, 0 },    // LED_STATE_LORA_PAIRING : vert
    { 0,    0, 0, 0 },     // LED_STATE_TEST : couleur du pas en cours
};
static const char *const STATE_NAMES[LED_STATE_COUNT] = { "user", "ble", "lora", "test" };
static const uint8_t TEST_COLORS[4][3] = { {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255} };
static const uint8_t RESET_R = 70, RESET_G = 0, RESET_B = 0;   // rouge

static SemaphoreHandle_t s_mtx = NULL;
static led_strip_handle_t s_strip = NULL;
static bool s_rgb = false;
static esp_timer_handle_t s_timer = NULL;        // clignotement
static esp_timer_handle_t s_user_timer = NULL;   // extinction de l'etat utilisateur
static esp_timer_handle_t s_test_timer = NULL;   // pas du test (1 s)
static uint32_t s_active = 0;       // bits des led_state_t actifs
static int s_shown = -1;            // etat affiche, -1 : eteinte
static bool s_on = false;
static int64_t s_user_end_us = 0;
static bool s_test_gpio = false;    // test GPIO : la broche n'est plus a la WS2812
static uint32_t s_test_steps = 0, s_test_step = 0;
static int s_test_hold = -1;        // test GPIO a niveau fixe (0 ou 1), -1 : bascule

/** Prepare la sortie selon la carte. s_mtx tenu. */
static void hw_setup(void)
{
    if (s_test_gpio) return;
    if (board_type() == BOARD_PRO) {
        if (!s_strip) {
            gpio_reset_pin((gpio_num_t)BOARD_LED_GPIO);
            led_strip_config_t strip = {};
            strip.strip_gpio_num = BOARD_LED_GPIO;
            strip.max_leds = 1;
            strip.led_model = LED_MODEL_WS2812;
            strip.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
            led_strip_rmt_config_t rmt = {};
            rmt.resolution_hz = 10 * 1000 * 1000;
            esp_err_t e = led_strip_new_rmt_device(&strip, &rmt, &s_strip);
            if (e != ESP_OK) {
                ESP_LOGE(TAG, "WS2812 indisponible : %s", esp_err_to_name(e));
                s_strip = NULL;
            } else {
                ESP_LOGI(TAG, "LED WS2812 sur GPIO %d", BOARD_LED_GPIO);
            }
        }
        s_rgb = s_strip != NULL;
    } else {
        gpio_reset_pin((gpio_num_t)BOARD_LED_GPIO);
        gpio_set_direction((gpio_num_t)BOARD_LED_GPIO, GPIO_MODE_OUTPUT);
        s_rgb = false;
    }
}

/** s_mtx tenu. Une LED simple s'allume pour toute couleur non noire. */
static void hw_out(bool on, uint8_t r, uint8_t g, uint8_t b)
{
    if (s_test_gpio) return;
    if (s_rgb) {
        if (on) {
            led_strip_set_pixel(s_strip, 0, r, g, b);
            led_strip_refresh(s_strip);
        } else {
            led_strip_clear(s_strip);
        }
    } else if (board_type() != BOARD_PRO) {
        gpio_set_level((gpio_num_t)BOARD_LED_GPIO, on && (r || g || b) ? 1 : 0);
    }
}

/**
 * Affiche l'etat actif le plus prioritaire (le dernier de l'enum) ; force :
 * reaffiche meme s'il n'a pas change (nouvelle couleur, nouvelle sortie).
 * s_mtx tenu.
 */
static void show(bool force)
{
    int top = -1;
    for (int i = LED_STATE_COUNT - 1; i >= 0; i--) {
        if (s_active & (1u << i)) { top = i; break; }
    }
    if (top == s_shown && !force) return;
    s_shown = top;
    esp_timer_stop(s_timer);
    if (top < 0) {
        s_on = false;
        hw_out(false, 0, 0, 0);
        return;
    }
    const led_pattern_t *p = &s_pat[top];
    s_on = true;
    hw_out(true, p->r, p->g, p->b);
    if (p->half_period_ms) esp_timer_start_periodic(s_timer, (uint64_t)p->half_period_ms * 1000);
}

static void timer_cb(void *arg)
{
    (void)arg;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_shown >= 0 && s_pat[s_shown].half_period_ms) {
        s_on = !s_on;
        const led_pattern_t *p = &s_pat[s_shown];
        hw_out(s_on, p->r, p->g, p->b);
    }
    xSemaphoreGive(s_mtx);
}

static void user_timer_cb(void *arg)
{
    (void)arg;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_active &= ~(1u << LED_STATE_USER);
    s_user_end_us = 0;
    show(false);
    xSemaphoreGive(s_mtx);
}

/** Fin du test : broche rendue a la LED, etat courant reaffiche. s_mtx tenu. */
static void test_end(void)
{
    esp_timer_stop(s_test_timer);
    if (s_test_gpio) {
        gpio_set_level((gpio_num_t)BOARD_LED_GPIO, 0);
        s_test_gpio = false;
        hw_setup();
    }
    s_active &= ~(1u << LED_STATE_TEST);
    show(true);
}

static void test_timer_cb(void *arg)
{
    (void)arg;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (++s_test_step >= s_test_steps) {
        test_end();
    } else if (s_test_gpio) {
        if (s_test_hold < 0) gpio_set_level((gpio_num_t)BOARD_LED_GPIO, (s_test_step & 1) ? 0 : 1);
    } else {
        const uint8_t *c = TEST_COLORS[s_test_step % 4];
        s_pat[LED_STATE_TEST] = { 0, c[0], c[1], c[2] };
        show(true);
    }
    xSemaphoreGive(s_mtx);
}

void status_led_init(void)
{
    if (s_mtx) return;
    s_mtx = xSemaphoreCreateMutex();
    esp_timer_create_args_t a = {};
    a.callback = timer_cb;
    a.name = "status_led";
    esp_timer_create(&a, &s_timer);
    a.callback = user_timer_cb;
    a.name = "led_user";
    esp_timer_create(&a, &s_user_timer);
    a.callback = test_timer_cb;
    a.name = "led_test";
    esp_timer_create(&a, &s_test_timer);
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    hw_setup();
    hw_out(false, 0, 0, 0);
    xSemaphoreGive(s_mtx);
}

void status_led_set(led_state_t state, bool active)
{
    if (!s_mtx || state >= LED_STATE_COUNT || state == LED_STATE_TEST) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (active) s_active |= 1u << state;
    else s_active &= ~(1u << state);
    show(false);
    xSemaphoreGive(s_mtx);
}

void status_led_user(bool on, uint8_t r, uint8_t g, uint8_t b, uint32_t blink_ms, uint32_t duration_s)
{
    if (!s_mtx) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    esp_timer_stop(s_user_timer);
    s_user_end_us = 0;
    if (on) {
        s_pat[LED_STATE_USER] = { blink_ms, r, g, b };
        s_active |= 1u << LED_STATE_USER;
        if (duration_s) {
            esp_timer_start_once(s_user_timer, (uint64_t)duration_s * 1000000);
            s_user_end_us = esp_timer_get_time() + (int64_t)duration_s * 1000000;
        }
    } else {
        s_active &= ~(1u << LED_STATE_USER);
    }
    // Nouvelle couleur de l'etat affiche : reaffichage
    show(s_shown == LED_STATE_USER);
    xSemaphoreGive(s_mtx);
}

bool status_led_user_available(void)
{
    return board_type() == BOARD_PRO;
}

esp_err_t status_led_command(const char *mode, const char *color, int brightness,
                             uint32_t period_ms, uint32_t duration_s)
{
    if (!status_led_user_available()) return ESP_ERR_NOT_SUPPORTED;
    if (!mode) return ESP_ERR_INVALID_ARG;
    if (!strcmp(mode, "off")) {
        status_led_user(false, 0, 0, 0, 0, 0);
        return ESP_OK;
    }
    bool blink = !strcmp(mode, "blink");
    if (!blink && strcmp(mode, "on")) return ESP_ERR_INVALID_ARG;
    uint32_t rgb = 0xFFFFFF;
    if (color && color[0]) {
        const char *s = color[0] == '#' ? color + 1 : color;
        if (strlen(s) != 6) return ESP_ERR_INVALID_ARG;
        rgb = 0;
        for (int i = 0; i < 6; i++) {
            if (!isxdigit((unsigned char)s[i])) return ESP_ERR_INVALID_ARG;
            char c = (char)tolower((unsigned char)s[i]);
            rgb = (rgb << 4) | (uint32_t)(c <= '9' ? c - '0' : c - 'a' + 10);
        }
    }
    if (brightness < 0) brightness = 0;
    if (brightness > 100) brightness = 100;
    uint32_t half = blink ? (period_ms >= 100 ? period_ms : 1000) / 2 : 0;
    status_led_user(true, ((rgb >> 16) & 0xFF) * brightness / 100, ((rgb >> 8) & 0xFF) * brightness / 100,
                    (rgb & 0xFF) * brightness / 100, half, duration_s);
    return ESP_OK;
}

static void test_start(bool gpio, int hold, uint32_t duration_s);

void status_led_test(bool gpio, uint32_t duration_s)
{
    test_start(gpio, -1, duration_s);
}

void status_led_gpio_hold(int level, uint32_t duration_s)
{
    test_start(true, level ? 1 : 0, duration_s);
}

static void test_start(bool gpio, int hold, uint32_t duration_s)
{
    if (!s_mtx) return;
    if (duration_s < 1) duration_s = 1;
    if (duration_s > 120) duration_s = 120;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_active & (1u << LED_STATE_TEST)) test_end();
    s_test_steps = duration_s;
    s_test_step = 0;
    s_test_hold = gpio ? hold : -1;
    s_active |= 1u << LED_STATE_TEST;
    if (gpio) {
        // Broche reprise a la WS2812 : sortie simple, niveau haut d'abord
        if (s_strip) {
            led_strip_clear(s_strip);
            led_strip_del(s_strip);
            s_strip = NULL;
        }
        s_rgb = false;
        s_test_gpio = true;
        gpio_reset_pin((gpio_num_t)BOARD_LED_GPIO);
        gpio_set_direction((gpio_num_t)BOARD_LED_GPIO, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)BOARD_LED_GPIO, hold == 0 ? 0 : 1);
        esp_timer_stop(s_timer);
        s_shown = LED_STATE_TEST;
        if (hold >= 0)
            ESP_LOGW(TAG, "Test GPIO %d : maintenu a %s pendant %lu s", BOARD_LED_GPIO,
                     hold ? "3,3 V" : "0 V", (unsigned long)duration_s);
        else
            ESP_LOGW(TAG, "Test GPIO %d : 3,3 V / 0 V toutes les secondes pendant %lu s",
                     BOARD_LED_GPIO, (unsigned long)duration_s);
    } else {
        s_pat[LED_STATE_TEST] = { 0, TEST_COLORS[0][0], TEST_COLORS[0][1], TEST_COLORS[0][2] };
        show(true);
        ESP_LOGI(TAG, "Test des couleurs pendant %lu s", (unsigned long)duration_s);
    }
    esp_timer_start_periodic(s_test_timer, 1000 * 1000);
    xSemaphoreGive(s_mtx);
}

void status_led_get(status_led_info_t *out)
{
    if (!out) return;
    *out = {};
    out->shown = "off";
    if (!s_mtx) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    out->rgb = s_rgb || s_test_gpio;
    if (s_shown >= 0) out->shown = STATE_NAMES[s_shown];
    out->user_on = (s_active & (1u << LED_STATE_USER)) != 0;
    out->r = s_pat[LED_STATE_USER].r;
    out->g = s_pat[LED_STATE_USER].g;
    out->b = s_pat[LED_STATE_USER].b;
    out->blink_ms = s_pat[LED_STATE_USER].half_period_ms;
    if (s_user_end_us) {
        int64_t left = s_user_end_us - esp_timer_get_time();
        out->remaining_s = left > 0 ? (uint32_t)((left + 999999) / 1000000) : 0;
    }
    xSemaphoreGive(s_mtx);
}

void status_led_flash_reset(void)
{
    if (!s_mtx) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    esp_timer_stop(s_timer);
    for (int i = 0; i < 10; i++) {
        hw_out(true, RESET_R, RESET_G, RESET_B);
        vTaskDelay(pdMS_TO_TICKS(50));
        hw_out(false, 0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    show(true);
    xSemaphoreGive(s_mtx);
}

void status_led_board_changed(void)
{
    if (!s_mtx) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    hw_setup();
    show(true);              // reaffiche l'etat courant sur la nouvelle sortie
    xSemaphoreGive(s_mtx);
}
