#pragma once

// ============================================================================
// Pilote SX1280/SX1281 (LoRa 2.4 GHz) - en-tete PRIVE du composant lora
//
// Remplace RadioLib (LiXee-Box) par les seules commandes SPI utiles au
// recepteur ZLinky LoRa : LoRa, BW 406,25 kHz, CR 4/5, en-tete explicite,
// CRC actif, IQ normal, mot de synchro 0x12. Pas de verrou ici : lora.cpp
// n'appelle ces fonctions que sous son propre verrou (tache LoRa, ou route
// web de changement de canal/SF qui le detient).
// ============================================================================

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Brochage du module LoRa de la LiXee-Box (loraModule.h). SCK (4) et MOSI (5)
// etaient les RTS/CTS de l'UART ZiGate sur la Box : le pont n'utilise pas le
// controle de flux materiel (UART_PIN_NO_CHANGE), aucune collision.
#ifndef LORA_PIN_DIO1
#define LORA_PIN_DIO1    2
#endif
#ifndef LORA_PIN_SCK
#define LORA_PIN_SCK     4
#endif
#ifndef LORA_PIN_MOSI
#define LORA_PIN_MOSI    5
#endif
#ifndef LORA_PIN_MISO
#define LORA_PIN_MISO    6
#endif
#ifndef LORA_PIN_NSS
#define LORA_PIN_NSS     7
#endif
#ifndef LORA_PIN_BUSY
#define LORA_PIN_BUSY    8
#endif
#ifndef LORA_PIN_RESET
#define LORA_PIN_RESET   16
#endif

// Horloge SPI : celle validee sur la LiXee-Box (2 MHz, mode 0)
#define LORA_SPI_HZ      2000000

// Plus grande trame manipulee (OTA_BLOCK : 4 + 64 + MIC 2 = 70 octets)
#define SX_MAX_PAYLOAD   80

/**
 * Detection du module : reset materiel puis lecture du registre de version
 * (0x0153). Si rien ne repond, le bus SPI2 et les broches sont relaches.
 * @param fw_version version lue (0 si absent)
 * @return true si un SX128x repond
 */
bool sx_detect(uint16_t *fw_version);

/** Configuration LoRa complete (apres sx_detect() reussi). */
esp_err_t sx_begin(uint32_t freq_mhz, uint8_t sf, uint16_t preamble, int8_t power_dbm);

/** Frequence porteuse (MHz entiers, plan de canaux 2410..2480). Radio en standby. */
esp_err_t sx_set_frequency(uint32_t freq_mhz);

/** Spreading factor 5..12. Radio en standby. */
esp_err_t sx_set_sf(uint8_t sf);

/** Longueur de preambule (symboles), appliquee a la prochaine emission/reception. */
void sx_set_preamble(uint16_t symbols);

/** Standby + effacement des IRQ (equivalent de RadioLib finishReceive()). */
esp_err_t sx_standby(void);

/** Passe en reception continue (DIO1 = RxDone / erreurs). */
esp_err_t sx_start_receive(void);

/**
 * Emission bloquante (temps d'antenne). La radio revient en standby :
 * appeler sx_start_receive() ensuite.
 * @param air_ms duree mesuree (peut etre NULL)
 */
esp_err_t sx_transmit(const uint8_t *data, uint8_t len, uint32_t *air_ms);

/** true si DIO1 est haut (une IRQ attend d'etre lue). */
bool sx_irq_pending(void);

/**
 * Lit la trame recue.
 * @return longueur (>0), 0 si rien n'a ete recu, -1 si trame en erreur
 *         (CRC, en-tete, longueur) ; les IRQ sont effacees dans tous les cas.
 */
int sx_read_packet(uint8_t *buf, int max_len, float *rssi, float *snr);

/** Interruption sur front montant de DIO1 (reveil de la tache LoRa). */
esp_err_t sx_attach_isr(void (*fn)(void *), void *arg);
