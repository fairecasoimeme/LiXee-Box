#pragma once

// ============================================================================
// LED d'etat (GPIO 3) : LED simple de la ZiWiFi32 ou LED RGB WS2812 de la Pro.
// Les modules demandent un etat ; la LED montre le plus prioritaire des etats
// actifs, avec la couleur de cet etat sur la Pro (meme rythme sur les deux).
// ============================================================================

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_STATE_BLE_PROV = 0,   // provisioning BLE en attente : 1 s allumee / 1 s eteinte, bleu
    LED_STATE_LORA_PAIRING,   // fenetre d'appairage LoRa : 300 ms, vert
    LED_STATE_COUNT
} led_state_t;

/**
 * Configure la LED selon la carte et l'eteint (une WS2812 garde sa couleur
 * apres un redemarrage logiciel). Apres board_init().
 */
void status_led_init(void);

/** Active ou desactive un etat. */
void status_led_set(led_state_t state, bool active);

/** Confirmation de la reinitialisation de secours : 10 eclats rapides (rouge). Bloquant ~1 s. */
void status_led_flash_reset(void);

/** La carte vient d'etre identifiee comme Pro : passage de la LED en WS2812. */
void status_led_board_changed(void);

#ifdef __cplusplus
}
#endif
