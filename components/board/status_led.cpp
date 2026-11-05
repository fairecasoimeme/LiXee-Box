// ============================================================================
// LED d'etat : LED simple (ZiWiFi32) ou WS2812 (Pro), voir status_led.h
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

static const char *TAG = "LED";

// La WS2812 eclaire le logo a travers le boitier : 70/255, sous les 30 %
// conseilles
typedef struct {
    uint32_t half_period_ms;
    uint8_t r, g, b;
} led_pattern_t;

static const led_pattern_t PATTERNS[LED_STATE_COUNT] = {
    { 1000, 0, 0, 70 },   // LED_STATE_BLE_PROV : bleu
    { 300,  0, 70, 0 },   // LED_STATE_LORA_PAIRING : vert
};
static const uint8_t RESET_R = 70, RESET_G = 0, RESET_B = 0;   // rouge

static SemaphoreHandle_t s_mtx = NULL;
static led_strip_handle_t s_strip = NULL;
static bool s_rgb = false;
static esp_timer_handle_t s_timer = NULL;
static uint32_t s_active = 0;       // bits des led_state_t actifs
static int s_shown = -1;            // etat affiche, -1 : eteinte
static bool s_on = false;

/** Prepare la sortie selon la carte. s_mtx tenu. */
static void hw_setup(void)
{
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
            }
        }
        s_rgb = s_strip != NULL;
    } else {
        gpio_reset_pin((gpio_num_t)BOARD_LED_GPIO);
        gpio_set_direction((gpio_num_t)BOARD_LED_GPIO, GPIO_MODE_OUTPUT);
        s_rgb = false;
    }
}

/** s_mtx tenu. */
static void hw_out(bool on, uint8_t r, uint8_t g, uint8_t b)
{
    if (s_rgb) {
        if (on) {
            led_strip_set_pixel(s_strip, 0, r, g, b);
            led_strip_refresh(s_strip);
        } else {
            led_strip_clear(s_strip);
        }
    } else if (board_type() != BOARD_PRO) {
        gpio_set_level((gpio_num_t)BOARD_LED_GPIO, on ? 1 : 0);
    }
}

static void timer_cb(void *arg)
{
    (void)arg;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_shown >= 0) {
        s_on = !s_on;
        const led_pattern_t *p = &PATTERNS[s_shown];
        hw_out(s_on, p->r, p->g, p->b);
    }
    xSemaphoreGive(s_mtx);
}

/** Affiche l'etat actif le plus prioritaire (le dernier de l'enum). s_mtx tenu. */
static void update(void)
{
    int top = -1;
    for (int i = LED_STATE_COUNT - 1; i >= 0; i--) {
        if (s_active & (1u << i)) { top = i; break; }
    }
    if (top == s_shown) return;
    s_shown = top;
    esp_timer_stop(s_timer);
    if (top < 0) {
        s_on = false;
        hw_out(false, 0, 0, 0);
        return;
    }
    const led_pattern_t *p = &PATTERNS[top];
    s_on = true;
    hw_out(true, p->r, p->g, p->b);
    esp_timer_start_periodic(s_timer, (uint64_t)p->half_period_ms * 1000);
}

void status_led_init(void)
{
    if (s_mtx) return;
    s_mtx = xSemaphoreCreateMutex();
    esp_timer_create_args_t a = {};
    a.callback = timer_cb;
    a.name = "status_led";
    esp_timer_create(&a, &s_timer);
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    hw_setup();
    hw_out(false, 0, 0, 0);
    xSemaphoreGive(s_mtx);
}

void status_led_set(led_state_t state, bool active)
{
    if (!s_mtx || state >= LED_STATE_COUNT) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (active) s_active |= 1u << state;
    else s_active &= ~(1u << state);
    update();
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
    s_shown = -1;
    update();
    xSemaphoreGive(s_mtx);
}

void status_led_board_changed(void)
{
    if (!s_mtx) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    hw_setup();
    int shown = s_shown;
    s_shown = -1;          // reaffiche l'etat courant sur la nouvelle sortie
    if (shown < 0) hw_out(false, 0, 0, 0);
    update();
    xSemaphoreGive(s_mtx);
}
