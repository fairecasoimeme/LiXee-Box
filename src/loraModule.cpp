#include "loraModule.h"
#include <SPI.h>

bool     loraDetected  = false;
uint16_t loraFwVersion = 0;

// Bus SPI dédié : SPI2 (FSPI) est libre sur l'ESP32-S3 (SPI0/1 servent la flash).
// NON static : partagé avec loraReceiver.cpp (une seule instance par contrôleur SPI).
SPIClass    loraSpi(FSPI);
SPISettings loraSpiSet(2000000, MSBFIRST, SPI_MODE0);  // SX128x : mode 0

// Attend que BUSY retombe (module prêt). false si timeout.
static bool loraWaitBusyLow(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (digitalRead(LORA_PIN_BUSY) == HIGH) {
    if (millis() - t0 > timeoutMs) return false;
    delay(1);
  }
  return true;
}

bool detectLoRa() {
  loraDetected  = false;
  loraFwVersion = 0;

  pinMode(LORA_PIN_NSS, OUTPUT);
  digitalWrite(LORA_PIN_NSS, HIGH);
  pinMode(LORA_PIN_BUSY, INPUT);
  pinMode(LORA_PIN_RESET, OUTPUT);

  loraSpi.begin(LORA_PIN_SCK, LORA_PIN_MISO, LORA_PIN_MOSI, LORA_PIN_NSS);

  // Reset matériel du SX1281
  digitalWrite(LORA_PIN_RESET, HIGH); delay(20);
  digitalWrite(LORA_PIN_RESET, LOW);  delay(50);
  digitalWrite(LORA_PIN_RESET, HIGH); delay(20);

  // Sans module, BUSY flotte : le timeout court évite de ralentir le boot.
  if (loraWaitBusyLow(300)) {
    // ReadRegister (0x19) : opcode, addr[15:8], addr[7:0], 1 octet dummy, puis les données.
    uint8_t v0 = 0, v1 = 0;
    loraSpi.beginTransaction(loraSpiSet);
    digitalWrite(LORA_PIN_NSS, LOW);
    loraSpi.transfer(0x19);
    loraSpi.transfer(0x01);          // registre 0x0153 = version firmware
    loraSpi.transfer(0x53);
    loraSpi.transfer(0x00);          // dummy
    v0 = loraSpi.transfer(0x00);
    v1 = loraSpi.transfer(0x00);
    digitalWrite(LORA_PIN_NSS, HIGH);
    loraSpi.endTransaction();

    loraFwVersion = ((uint16_t)v0 << 8) | v1;
    // 0x0000 / 0xFFFF = pas de réponse (MISO au plancher ou au rail).
    loraDetected = (loraFwVersion != 0x0000 && loraFwVersion != 0xFFFF);
  }

  // NB : Serial.printf et pas log_i — le firmware compile avec CORE_DEBUG_LEVEL=0,
  // qui rend log_i/log_w/log_e muets. Ce diagnostic doit rester visible.
  if (loraDetected) {
    Serial.printf("[LoRa] SX1281 detecte (firmware 0x%04X)\r\n", loraFwVersion);
  } else {
    Serial.printf("[LoRa] aucun module detecte (BUSY=%d, version lue 0x%04X)\r\n",
                  digitalRead(LORA_PIN_BUSY), loraFwVersion);
    loraSpi.end();                   // relâche SCK/MOSI/MISO/NSS si pas de module
  }
  return loraDetected;
}
