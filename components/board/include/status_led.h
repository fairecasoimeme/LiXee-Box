#pragma once

// ============================================================================
// LED d'etat (GPIO 3) : LED simple des cartes a FTDI ou LED RGB WS2812 de la Pro.
// Les modules demandent un etat ; la LED montre le plus prioritaire des etats
// actifs (le dernier de l'enum), avec la couleur de cet etat sur la Pro (meme
// rythme sur les deux cartes ; une LED simple s'allume pour toute couleur).
// ============================================================================

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_STATE_USER = 0,       // API / regles : couleur et rythme choisis (le moins prioritaire)
    LED_STATE_BLE_PROV,       // provisioning BLE en attente : 1 s allumee / 1 s eteinte, bleu
    LED_STATE_LORA_PAIRING,   // fenetre d'appairage LoRa : 300 ms, vert
    LED_STATE_TEST,           // test materiel (status_led_test)
    LED_STATE_COUNT
} led_state_t;

/** Etat courant, pour l'API. */
typedef struct {
    bool rgb;                 // WS2812 pilotee (Pro)
    const char *shown;        // "off", "user", "ble", "lora", "test"
    bool user_on;             // etat utilisateur actif
    uint8_t r, g, b;          // couleur utilisateur (luminosite appliquee)
    uint32_t blink_ms;        // demi-periode utilisateur, 0 : fixe
    uint32_t remaining_s;     // extinction automatique de l'etat utilisateur, 0 : aucune
} status_led_info_t;

/**
 * Configure la LED selon la carte et l'eteint (une WS2812 garde sa couleur
 * apres un redemarrage logiciel). Apres board_init().
 */
void status_led_init(void);

/** Active ou desactive un etat systeme. */
void status_led_set(led_state_t state, bool active);

/**
 * Etat utilisateur (API, regles) : couleur r/g/b deja ponderee par la
 * luminosite, clignotement si blink_ms (demi-periode) > 0, extinction
 * automatique apres duration_s si > 0. on = false l'eteint.
 */
void status_led_user(bool on, uint8_t r, uint8_t g, uint8_t b, uint32_t blink_ms, uint32_t duration_s);

/**
 * Commande de l'etat utilisateur, commune a POST /api/led et aux actions "led"
 * des regles : mode "off", "on" ou "blink" ; color "#RRGGBB" (NULL ou "" :
 * blanc) ; brightness 0-100 ; period_ms periode complete du clignotement
 * (< 100 : 1000) ; duration_s extinction automatique (0 : sans fin).
 * Renvoie false si le mode ou la couleur est invalide (rien n'est change).
 */
bool status_led_command(const char *mode, const char *color, int brightness,
                        uint32_t period_ms, uint32_t duration_s);

/**
 * Test materiel pendant duration_s secondes, prioritaire sur tout le reste :
 * gpio = false : rouge, vert, bleu, blanc a pleine luminosite, 1 s chacun ;
 * gpio = true : GPIO 3 en sortie simple, 3,3 V / 0 V toutes les secondes,
 * pour verifier la liaison GPIO 3 - R9 - DIN au multimetre.
 */
void status_led_test(bool gpio, uint32_t duration_s);

/**
 * Test materiel : GPIO 3 maintenu a 0 V (level 0) ou 3,3 V (level 1) pendant
 * duration_s secondes, pour des mesures en continu au multimetre.
 */
void status_led_gpio_hold(int level, uint32_t duration_s);

void status_led_get(status_led_info_t *out);

/** Confirmation de la reinitialisation de secours : 10 eclats rapides (rouge). Bloquant ~1 s. */
void status_led_flash_reset(void);

/** La carte vient d'etre identifiee comme Pro : passage de la LED en WS2812. */
void status_led_board_changed(void);

#ifdef __cplusplus
}
#endif
