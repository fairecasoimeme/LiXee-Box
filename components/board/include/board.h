#pragma once

// ============================================================================
// Carte materielle : un meme firmware pour la cle ZiWiFi32 et la LiXee-Box Pro
//
// Broches communes : UART ZiGate (17/18), W5500, SX1280/1281, LED sur GPIO 3.
// Differences :
//  - ZiWiFi32 (et prototypes v3-box) : LED simple ; reset et entree ISP du
//    JN5189 pilotes par le FTDI seulement (GPIO 15 et 21 non relies) ;
//  - Pro (ZiGate WiFi v3-prod, v3-poe, v4) : LED RGB WS2812 ; reset du JN5189
//    sur GPIO 15, entree ISP sur GPIO 21 ; bouton SW4 sur GPIO 47 ; USB natif
//    sur GPIO 19/20 (jamais pilotes) ; presence de l'USB 5 V sur GPIO 45.
//
// Identification, par ordre de priorite :
//  1. eFuse BLOCK_USR_DATA ecrit en production : octets "LX", produit, revision ;
//  2. detection a chaque demarrage (zigate_init) : GPIO 21 bas pendant un
//     reset par GPIO 15 fait demarrer le JN5189 dans son bootloader ISP, qui
//     repond a une demande de deverrouillage. Seule une Pro pilote ces deux
//     signaux ; la reponse vient de la ROM du JN5189, module vierge compris ;
//  3. par defaut : ZiWiFi32.
// ============================================================================

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_ZIWIFI32 = 1,     // cle USB ZiWiFi32, prototypes v3-box
    BOARD_PRO      = 2,     // LiXee-Box Pro
} board_type_t;

typedef enum {
    BOARD_SRC_DEFAULT = 0,  // rien d'identifie : ZiWiFi32
    BOARD_SRC_EFUSE,        // eFuse de production
    BOARD_SRC_DETECTED,     // bootloader ISP du JN5189 joint par GPIO 15/21
} board_source_t;

// Broches propres a la Pro, sans liaison sur la ZiWiFi32
#define BOARD_ZIGATE_RESET_GPIO  15   // /RESET du JN5189, actif bas
#define BOARD_ZIGATE_ISP_GPIO    21   // SPIMISO du JN5189 : bas pendant le reset = ISP
#define BOARD_USB_SENSE_GPIO     45   // diviseur sur le 5 V de l'USB
#define BOARD_LED_GPIO           3    // LED simple ou WS2812 selon la carte

/** Identification de la carte par l'eFuse de production. */
void board_init(void);

board_type_t board_type(void);
board_source_t board_source(void);

/** "ziwifi32" ou "pro" */
const char *board_id(void);
/** Nom affiche : "ZiWiFi32" ou "LiXee-Box Pro" */
const char *board_name(void);
/** "default", "efuse" ou "detected" */
const char *board_source_name(void);

/**
 * Alimentation par l'USB (GPIO 45). Toujours vrai sur la ZiWiFi32 ; sur la
 * Pro, faux = PoE seul (le PoE n'a pas de detection propre).
 */
bool board_usb_powered(void);

/**
 * Resultat de la sonde ISP du JN5189 (zigate_init) : true si le bootloader a
 * repondu, la carte pilote alors reset et ISP, c'est une Pro.
 */
void board_set_zigate_isp(bool responded);

/** La carte pilote le reset et l'entree ISP du JN5189 (Pro identifiee). */
bool board_has_zigate_isp(void);

#ifdef __cplusplus
}
#endif
