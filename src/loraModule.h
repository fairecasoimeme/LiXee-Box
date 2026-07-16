#ifndef LORA_MODULE_H
#define LORA_MODULE_H

#include <Arduino.h>

/*
 * Module LoRa 2.4 GHz (SX1281) de la LiXee-Box — détection de présence.
 *
 * Brochage :
 *   DIO1=IO2, SCK=IO4, MOSI=IO5, MISO=IO6, NSS=IO7, BUSY=IO8, RESET=IO16
 *
 * NB : SCK (4) et MOSI (5) étaient auparavant assignées au RTS/CTS de l'UART ZiGate
 * (Serial1.setPins(RX, TX, 5, 4)). Le contrôle de flux matériel ayant été abandonné
 * (setHwFlowCtrlMode resté commenté), ces broches ont été libérées côté UART pour que
 * le Zigbee et le LoRa puissent coexister sur la même carte.
 */

#define LORA_PIN_DIO1   2
#define LORA_PIN_SCK    4
#define LORA_PIN_MOSI   5
#define LORA_PIN_MISO   6
#define LORA_PIN_NSS    7
#define LORA_PIN_BUSY   8
#define LORA_PIN_RESET  16

#include <SPI.h>

extern bool     loraDetected;    // true si un SX1281 répond en SPI
extern uint16_t loraFwVersion;   // version firmware lue (0 si absent) — ex. 0xA9B7

// Bus SPI du module, PARTAGÉ : détection (loraModule) et récepteur (loraReceiver) doivent
// utiliser la MÊME instance. Deux SPIClass sur le même contrôleur FSPI se marchent dessus.
// Initialisé par detectLoRa() ; reste ouvert si un module a répondu.
extern SPIClass    loraSpi;
extern SPISettings loraSpiSet;

// Détecte la présence du module LoRa : reset matériel puis lecture du registre
// "firmware version" (0x0153) en SPI brut. N'utilise pas RadioLib (détection seule).
// Si aucun module ne répond, le bus SPI est relâché pour laisser les broches libres.
bool detectLoRa();

#endif
