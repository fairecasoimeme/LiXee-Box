/*
 * ZLinky TIC Receiver — SX1280 LoRa 2.4GHz
 * Recoit les paquets DATA_ESSENTIAL (17+2B) et DATA_EXTENDED (9-19+2B)
 * envoyes par le ZLinky (JN5189 + SX1280)
 *
 * Appairage : 30s au boot en mode PAIR_LISTEN
 *   1) Recoit PAIR_REQUEST (MAC emetteur)
 *   2) Genere cle AES-128, envoie PAIR_RESPONSE
 *   3) Recoit PAIR_CONFIRM (verification preuve AES-ECB)
 *   4) Sauvegarde MAC+cle en EEPROM
 *
 * Apres appairage : dechiffrement AES-128-CTR + verification MIC CMAC
 *
 * Config LoRa : 2490 MHz, SF10, BW406.25, CR4/5
 * Preamble 16sym, CRC on, IQ normal, SyncWord 0x12
 * Max 20+2 octets par paquet (SF10 + MIC)
 *
 * RadioLib : https://github.com/jgromes/RadioLib
 *
 * Modes Linky :
 *   0 = Historique monophase
 *   1 = Standard monophase
 *   2 = Historique triphase
 *   3 = Standard triphase
 *   5 = Standard monophase producteur
 *   7 = Standard triphase producteur
 */

#include <SPI.h>
#include <RadioLib.h>
#include <EEPROM.h>

/* AES-128 software implementation (portable, no external deps) */
#include "aes128.h"

// Pins : NSS=7, DIO1=5, RESET=A0, BUSY=3
SX1280 radio = new Module(7, 5, A0, 3);

volatile bool rxFlag = false;
uint32_t rxCount = 0;
uint32_t rxErrors = 0;
uint32_t missedPackets = 0;
uint8_t  lastSeq = 0;
uint8_t  consecutiveCrcErrors = 0;
uint32_t rxSinceReset = 0;
uint32_t totalResets = 0;

#define CRC_ERROR_RESET_THRESHOLD  3
#define PREVENTIVE_RESET_INTERVAL  50
/* SF10 LoRa demod limit ≈ -15 dB SNR. En portée limite, le SNR est normalement
 * bas, ce n'est PAS un signe de drift. On set le seuil bien sous le demod limit
 * pour ne pas reset inutilement. Set à -100 pour effectivement desactiver. */
#define SNR_RESET_THRESHOLD       -25.0

/* ====== Types de paquets (byte[0] & 0x0F) ====== */
#define PKT_TYPE_ESSENTIAL      0x01
#define PKT_TYPE_EXTENDED       0x02
#define PKT_TYPE_PAIR_REQUEST   0x03
#define PKT_TYPE_PAIR_RESPONSE  0x04
#define PKT_TYPE_PAIR_CONFIRM   0x05

/* Tailles paquets DATA (sans MIC) */
#define PKT_ESSENTIAL_SIZE  17
#define PKT_EXT_IV_SIZE     15  /* Current/Voltage */
#define PKT_EXT_NRG_SIZE    19  /* Energy */
#define PKT_EXT_NRG2_SIZE   19  /* Energy 2 */
#define PKT_EXT_VSTAT_SIZE  15  /* Voltage Stats */
#define PKT_EXT_PMCFG_SIZE  18  /* Power Max Config */
#define PKT_EXT_DAILY_SIZE  19  /* Daily Energy */
#define PKT_EXT_COMP_SIZE    9  /* Complement */

/* MIC size */
#define CRYPT_MIC_SIZE  2

/* Tailles paquets pairing */
#define PAIR_REQUEST_SIZE   11
#define PAIR_RESPONSE_SIZE  20   /* +1 byte op_channel a la fin */
#define PAIR_CONFIRM_SIZE   15

/* ====== LoRa channel plan (must match emitter firmware) ====== */
#define LORA_NUM_CHANNELS  8
#define LORA_PAIR_CHANNEL  3   /* 2440 MHz - canal pour pairing handshake */
const float LORA_CHANNEL_FREQS_MHZ[LORA_NUM_CHANNELS] = {
  2410.0, 2420.0, 2430.0, 2440.0, 2450.0, 2460.0, 2470.0, 2480.0
};

/* === DEFAULT_OP_CHANNEL ===
 *
 * Choix du canal operationnel quand pas de pairing en EEPROM :
 *   0..7 : forcer ce canal (skip le CCA scan)
 *   -1   : auto-detection par CCA scan (le plus calme)
 *
 * Si tu connais ton environnement RF (e.g., WiFi fixe sur ch 6),
 * c'est preferable de FORCER un canal pour avoir un comportement
 * deterministe. Mets -1 pour laisser le firmware choisir.
 *
 * Note: une fois paire, le canal est sauvegarde en EEPROM et reste
 * actif pour tous les emetteurs futurs (jusqu'a un re-pairing complet).
 */
#define DEFAULT_OP_CHANNEL  4   /* 2450 MHz par defaut */
//#define DEFAULT_OP_CHANNEL  -1  /* Uncomment pour CCA scan auto */

uint8_t opChannel = LORA_PAIR_CHANNEL;
bool isChannelInitialized = false;

/* Sous-types extended (byte[2]) */
#define EXT_CURRENT_VOLTAGE  0x00
#define EXT_ENERGY           0x01
#define EXT_ENERGY_2         0x02
#define EXT_VOLTAGE_STATS    0x03
#define EXT_POWER_MAX_CFG    0x04
#define EXT_DAILY_ENERGY     0x05
#define EXT_COMPLEMENT       0x06

/* AES sizes */
#define AES_KEY_SIZE    16
#define AES_BLOCK_SIZE  16
#define MAC_SIZE         8

/* ====== Multi-emitter pairing ====== */
#define MAX_PAIRED_EMITTERS  4

struct PairedEmitter {
  bool     valid;
  uint8_t  mac[MAC_SIZE];
  uint8_t  key[AES_KEY_SIZE];
  /* Per-emitter stats (in RAM only, not in EEPROM) */
  uint32_t rxCount;
  uint32_t errCount;
  uint32_t missedPackets;
  uint8_t  lastSeq;
  bool     seqInit;
};
PairedEmitter emitters[MAX_PAIRED_EMITTERS];
int currentPairingSlot = -1;  /* slot used during a pairing handshake */

/* ====== EEPROM layout (multi-emitter) ====== */
/* Bumped magic: 0x4C505251 ("LPRQ") to invalidate single-emitter records */
#define EEPROM_SIZE        256   /* enough for headers + 4 emitter records */
#define EEPROM_MAGIC_ADDR    0   /* uint32 magic (4B) */
#define EEPROM_OPCH_ADDR     4   /* op_channel (1B) */
#define EEPROM_COUNT_ADDR    5   /* number of valid emitter records (1B) */
#define EEPROM_RESERVED_ADDR 6   /* 2B reserved */
#define EEPROM_EMITTERS_BASE 8   /* start of emitter records, 25 bytes each */
#define EEPROM_EMITTER_SIZE  25  /* 1 (valid) + 8 (mac) + 16 (key) */
#define EEPROM_MAGIC_VAL     0x4C505251  /* "LPRQ" */

/* ====== OTA Protocol (must match emitter app_lora_ota.h) ====== */
#define LORA_TIC_PKT_OTA_START   0x06
#define LORA_TIC_PKT_OTA_BLOCK   0x07
#define LORA_TIC_PKT_OTA_END     0x08
#define LORA_TIC_PKT_OTA_ACK     0x09
#define LORA_TIC_PKT_OTA_STATUS  0x0A

#define OTA_ACK_TIMEOUT_MS       5000

/* ====== Pairing state machine ====== */
typedef enum {
  RX_PAIR_LISTEN = 0,    /* 30s au boot, attend PAIR_REQUEST */
  RX_PAIRED,             /* Appaire, recoit donnees chiffrees */
  RX_IDLE,               /* Pas appaire, hors fenetre pairing */
  RX_OTA_GATEWAY         /* Mode gateway OTA : pilote par commandes serie */
} eRxPairState_t;

/* OTA gateway context */
uint8_t  otaGwTargetMAC[MAC_SIZE];
uint8_t  otaGwTargetKey[AES_KEY_SIZE];
bool     otaGwInitialized = false;

eRxPairState_t rxPairState = RX_IDLE;
uint32_t pairListenStart = 0;
#define PAIR_LISTEN_TIMEOUT_MS  30000

/* ====== Bouton USER (B1) sur STM32 Nucleo-64 = PC13 ====== */
#define USER_BUTTON_PIN  PC13
bool lastButtonState = HIGH;  /* Pull-up, actif LOW */
uint32_t buttonPressStart = 0;
bool buttonLongDetected = false;
#define BUTTON_LONG_PRESS_MS  3000  /* 3s appui long pour entrer en pairing */

/* Legacy "current pair in progress" - used during handshake (PAIR_REQUEST → PAIR_CONFIRM).
 * After PAIR_CONFIRM success, contents are copied into the emitters[] array. */
uint8_t netKey[AES_KEY_SIZE];
uint8_t emitterMAC[MAC_SIZE];
bool isPaired = false;  /* TRUE if at least one emitter in emitters[] is valid */

/* === Multi-emitter helpers === */

/* Count valid entries in emitters[] */
int countPairedEmitters() {
  int n = 0;
  for (int i = 0; i < MAX_PAIRED_EMITTERS; i++) if (emitters[i].valid) n++;
  return n;
}

/* Find emitter index by MAC, or -1 if not found */
int findEmitterByMAC(const uint8_t *mac) {
  for (int i = 0; i < MAX_PAIRED_EMITTERS; i++) {
    if (emitters[i].valid && memcmp(emitters[i].mac, mac, MAC_SIZE) == 0)
      return i;
  }
  return -1;
}

/* Find empty slot, or -1 if all full */
int findEmptySlot() {
  for (int i = 0; i < MAX_PAIRED_EMITTERS; i++) {
    if (!emitters[i].valid) return i;
  }
  return -1;
}

/* Add or update a pairing in the array.
 * If MAC already known: update its key.
 * If new MAC: take an empty slot.
 * Returns index, or -1 if full. */
int addOrUpdateEmitter(const uint8_t *mac, const uint8_t *key) {
  int idx = findEmitterByMAC(mac);
  if (idx < 0) idx = findEmptySlot();
  if (idx < 0) return -1;  /* full */

  emitters[idx].valid = true;
  memcpy(emitters[idx].mac, mac, MAC_SIZE);
  memcpy(emitters[idx].key, key, AES_KEY_SIZE);
  emitters[idx].rxCount = 0;
  emitters[idx].errCount = 0;
  emitters[idx].missedPackets = 0;
  emitters[idx].seqInit = false;
  return idx;
}

// Helpers big-endian
uint16_t getU16(const uint8_t *b) { return ((uint16_t)b[0] << 8) | b[1]; }
uint32_t getU32(const uint8_t *b) { return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]; }

void rxDone(void) {
  rxFlag = true;
}

/* Mode Linky en texte */
const char* modeStr(uint8_t mode) {
  switch (mode) {
    case 0: return "Histo Mono";
    case 1: return "Std Mono";
    case 2: return "Histo Tri";
    case 3: return "Std Tri";
    case 5: return "Std Mono Prod";
    case 7: return "Std Tri Prod";
    default: return "Inconnu";
  }
}

bool isTri(uint8_t mode) {
  return (mode == 2 || mode == 3 || mode == 7);
}

/* Check if a channel index is in valid range */
bool APP_isValidChannel(uint8_t ch) {
  return (ch < LORA_NUM_CHANNELS);
}

/* Set RF frequency by channel index. Falls back to PAIR_CHANNEL if invalid. */
void setChannel(uint8_t ch) {
  if (ch >= LORA_NUM_CHANNELS) {
    Serial.print(F("[CHAN] Invalid ch "));
    Serial.print(ch);
    Serial.print(F(", fallback to PAIR_CHANNEL "));
    Serial.println(LORA_PAIR_CHANNEL);
    ch = LORA_PAIR_CHANNEL;
  }
  float freq = LORA_CHANNEL_FREQS_MHZ[ch];
  radio.standby();
  int s = radio.setFrequency(freq);
  if (s != RADIOLIB_ERR_NONE) {
    Serial.print(F("[CHAN] setFrequency FAILED: "));
    Serial.println(s);
  } else {
    Serial.print(F("[CHAN] Switched to ch "));
    Serial.print(ch);
    Serial.print(F(" = "));
    Serial.print(freq, 1);
    Serial.println(F(" MHz"));
  }
}

/* CCA scan : pour chaque canal, mesurer RSSI au repos pendant 100ms.
 * Retourne l'index du canal le plus calme (RSSI le plus bas). */
uint8_t ccaScan() {
  Serial.println(F("[CCA] Scanning channels..."));
  int16_t bestRssi = 1;  /* sentinel positif (RSSI sont negatifs) */
  uint8_t bestCh = LORA_PAIR_CHANNEL;

  /* Disable IRQ pendant le scan */
  radio.clearDio1Action();

  for (uint8_t ch = 0; ch < LORA_NUM_CHANNELS; ch++) {
    radio.standby();
    radio.setFrequency(LORA_CHANNEL_FREQS_MHZ[ch]);
    radio.startReceive();
    delay(80);
    int16_t rssi = (int16_t)radio.getRSSI(false);  /* RSSI courant du canal */
    Serial.print(F("  ch "));
    Serial.print(ch);
    Serial.print(F(" ("));
    Serial.print(LORA_CHANNEL_FREQS_MHZ[ch], 0);
    Serial.print(F(" MHz) RSSI = "));
    Serial.print(rssi);
    Serial.println(F(" dBm"));
    if (bestRssi > 0 || rssi < bestRssi) {
      bestRssi = rssi;
      bestCh = ch;
    }
  }
  Serial.print(F("[CCA] Best channel = "));
  Serial.print(bestCh);
  Serial.print(F(" ("));
  Serial.print(bestRssi);
  Serial.println(F(" dBm)"));

  radio.setDio1Action(rxDone);
  return bestCh;
}

/* Full reinit du SX1280 RX (sur le canal courant) */
void doFullReset(const char *reason) {
  Serial.print(F("[RX] *** Full reset: "));
  Serial.print(reason);
  Serial.print(F(" (reset #"));
  Serial.print(++totalResets);
  Serial.print(F(", after "));
  Serial.print(rxSinceReset);
  Serial.println(F(" pkts) ***"));

  consecutiveCrcErrors = 0;
  rxSinceReset = 0;

  radio.begin(LORA_CHANNEL_FREQS_MHZ[opChannel], 406.25, 11, 5, 0x12, 10, 16);  /* SF11 */
  radio.setCRC(2);
  radio.explicitHeader();
  radio.invertIQ(false);
  radio.setDio1Action(rxDone);
  radio.startReceive(0xFFFF);

  Serial.print(F("[RX] Reset complete on ch "));
  Serial.print(opChannel);
  Serial.println(F(", listening...\r\n"));
}

void hexDump(const uint8_t *buf, int len) {
  for (int i = 0; i < len && i < 40; i++) {
    if (buf[i] < 0x10) Serial.print('0');
    Serial.print(buf[i], HEX);
    Serial.print(' ');
  }
  if (len > 40) Serial.print(F("..."));
  Serial.println();
}

void trackSequence(uint8_t seq) {
  rxCount++;
  if (rxCount > 1) {
    uint8_t expected = (lastSeq + 1) & 0xFF;
    if (seq != expected) {
      uint8_t missed = (seq - expected) & 0xFF;
      missedPackets += missed;
    }
  }
  lastSeq = seq;
}

/* Per-emitter stats : passe l'index du slot pour avoir des stats par emetteur */
int currentSlotForStats = -1;  /* set par parseAndPrint avant d'appeler les parsers */

void printStats(float rssi, float snr) {
  if (currentSlotForStats >= 0 && currentSlotForStats < MAX_PAIRED_EMITTERS &&
      emitters[currentSlotForStats].valid) {
    PairedEmitter &em = emitters[currentSlotForStats];
    Serial.print(F("  Stats slot "));
    Serial.print(currentSlotForStats);
    Serial.print(F(" : rx="));
    Serial.print(em.rxCount);
    Serial.print(F(" miss="));
    Serial.print(em.missedPackets);
    if (em.rxCount + em.missedPackets > 0) {
      Serial.print(F(" PDR="));
      Serial.print(100.0 * em.rxCount / (em.rxCount + em.missedPackets), 0);
      Serial.print(F("%"));
    }
    Serial.print(F("  RSSI="));
    Serial.print(rssi, 0);
    Serial.print(F(" SNR="));
    Serial.println(snr, 1);
    return;
  }
  /* Fallback : stats globales si pas de slot identifie (legacy) */
  Serial.print(F("  Stats  : rx="));
  Serial.print(rxCount);
  Serial.print(F(" err="));
  Serial.print(rxErrors);
  Serial.print(F(" miss="));
  Serial.print(missedPackets);
  if (rxCount + missedPackets > 0) {
    Serial.print(F(" PDR="));
    Serial.print(100.0 * rxCount / (rxCount + missedPackets), 0);
    Serial.print(F("%"));
  }
  Serial.print(F("  RSSI="));
  Serial.print(rssi, 0);
  Serial.print(F(" SNR="));
  Serial.println(snr, 1);
}

/* ====================================================================== */
/* ====== EEPROM persistence ====== */
/* ====================================================================== */

void loadPairingFromEEPROM() {
  /* Reset RAM state */
  for (int i = 0; i < MAX_PAIRED_EMITTERS; i++) emitters[i].valid = false;

  uint32_t magic = 0;
  magic  = (uint32_t)EEPROM.read(EEPROM_MAGIC_ADDR)     << 24;
  magic |= (uint32_t)EEPROM.read(EEPROM_MAGIC_ADDR + 1) << 16;
  magic |= (uint32_t)EEPROM.read(EEPROM_MAGIC_ADDR + 2) << 8;
  magic |= (uint32_t)EEPROM.read(EEPROM_MAGIC_ADDR + 3);

  if (magic != EEPROM_MAGIC_VAL) {
    isPaired = false;
    Serial.println(F("[PAIR] No valid pairing in EEPROM"));
    return;
  }

  /* Load op_channel */
  uint8_t storedCh = EEPROM.read(EEPROM_OPCH_ADDR);
  if (storedCh < LORA_NUM_CHANNELS) {
    opChannel = storedCh;
  } else {
    Serial.print(F("[PAIR] Invalid op_channel in EEPROM ("));
    Serial.print(storedCh);
    Serial.println(F("), fallback PAIR_CHANNEL"));
    opChannel = LORA_PAIR_CHANNEL;
  }

  uint8_t count = EEPROM.read(EEPROM_COUNT_ADDR);
  if (count > MAX_PAIRED_EMITTERS) count = MAX_PAIRED_EMITTERS;

  /* Load each emitter record */
  int loaded = 0;
  for (int slot = 0; slot < MAX_PAIRED_EMITTERS; slot++) {
    int base = EEPROM_EMITTERS_BASE + slot * EEPROM_EMITTER_SIZE;
    uint8_t valid = EEPROM.read(base);
    if (valid != 0x01) continue;

    emitters[slot].valid = true;
    for (int i = 0; i < MAC_SIZE; i++)
      emitters[slot].mac[i] = EEPROM.read(base + 1 + i);
    for (int i = 0; i < AES_KEY_SIZE; i++)
      emitters[slot].key[i] = EEPROM.read(base + 1 + MAC_SIZE + i);
    emitters[slot].rxCount = 0;
    emitters[slot].errCount = 0;
    emitters[slot].missedPackets = 0;
    emitters[slot].seqInit = false;

    Serial.print(F("[PAIR] Slot "));
    Serial.print(slot);
    Serial.print(F(" : MAC="));
    for (int i = 0; i < MAC_SIZE; i++) {
      if (emitters[slot].mac[i] < 0x10) Serial.print('0');
      Serial.print(emitters[slot].mac[i], HEX);
    }
    Serial.println();
    loaded++;
  }

  isPaired = (loaded > 0);
  Serial.print(F("[PAIR] Loaded "));
  Serial.print(loaded);
  Serial.print(F(" emitters from EEPROM, op_channel="));
  Serial.println(opChannel);
}

void savePairingToEEPROM() {
  uint32_t magic = EEPROM_MAGIC_VAL;
  EEPROM.update(EEPROM_MAGIC_ADDR,     (magic >> 24) & 0xFF);
  EEPROM.update(EEPROM_MAGIC_ADDR + 1, (magic >> 16) & 0xFF);
  EEPROM.update(EEPROM_MAGIC_ADDR + 2, (magic >> 8)  & 0xFF);
  EEPROM.update(EEPROM_MAGIC_ADDR + 3,  magic        & 0xFF);

  EEPROM.update(EEPROM_OPCH_ADDR, opChannel);
  EEPROM.update(EEPROM_COUNT_ADDR, (uint8_t)countPairedEmitters());

  for (int slot = 0; slot < MAX_PAIRED_EMITTERS; slot++) {
    int base = EEPROM_EMITTERS_BASE + slot * EEPROM_EMITTER_SIZE;
    EEPROM.update(base, emitters[slot].valid ? 0x01 : 0x00);
    if (emitters[slot].valid) {
      for (int i = 0; i < MAC_SIZE; i++)
        EEPROM.update(base + 1 + i, emitters[slot].mac[i]);
      for (int i = 0; i < AES_KEY_SIZE; i++)
        EEPROM.update(base + 1 + MAC_SIZE + i, emitters[slot].key[i]);
    }
  }

  Serial.print(F("[PAIR] Saved "));
  Serial.print(countPairedEmitters());
  Serial.print(F(" emitters to EEPROM (op_channel="));
  Serial.print(opChannel);
  Serial.println(F(")"));
}

void erasePairingFromEEPROM() {
  for (int i = 0; i < EEPROM_SIZE; i++)
    EEPROM.update(i, 0xFF);

  isPaired = false;
  memset(netKey, 0, AES_KEY_SIZE);
  memset(emitterMAC, 0, MAC_SIZE);
  for (int i = 0; i < MAX_PAIRED_EMITTERS; i++) emitters[i].valid = false;

  Serial.println(F("[PAIR] Pairing erased from EEPROM"));
}

/* ====================================================================== */
/* ====== AES-128-CTR decryption + CMAC MIC (aes128.h) ====== */
/* ====================================================================== */

/**
 * Build 16-byte CTR nonce: MAC(8) + 0x00(7) + seq(1)
 * Identical to emitter vBuildNonce()
 */
void buildNonce(uint8_t *nonce, const uint8_t *mac, uint8_t seq) {
  memcpy(nonce, mac, 8);
  memset(&nonce[8], 0, 7);
  nonce[15] = seq;
}

/**
 * Decrypt a LoRa TIC packet in-place and verify MIC.
 *
 * Input: [type][seq][encrypted_payload...][MIC_hi][MIC_lo]
 * Output (if OK): [type][seq][decrypted_payload...] (MIC stripped)
 *
 * Returns true if MIC valid.
 */
bool decryptPacket(uint8_t *pkt, int totalLen, const uint8_t *key, const uint8_t *mac, bool verbose = true) {
  if (totalLen < 2 + CRYPT_MIC_SIZE) return false;

  int dataLen = totalLen - CRYPT_MIC_SIZE; /* length without MIC */
  uint8_t recvMIC0 = pkt[dataLen];
  uint8_t recvMIC1 = pkt[dataLen + 1];

  /* 1) Decrypt payload (skip type+seq = 2 bytes header) */
  uint8_t nonce[16];
  uint8_t seq = pkt[1];
  buildNonce(nonce, mac, seq);

  int payloadLen = dataLen - 2;
  if (payloadLen > 0) {
    aes128_ctr_crypt(&pkt[2], (uint16_t)payloadLen, key, nonce);
  }

  /* 2) Compute expected MIC over decrypted cleartext */
  uint8_t expectedMIC[16];
  aes128_cmac(pkt, (uint16_t)dataLen, key, expectedMIC);

  /* 3) Compare first 2 bytes */
  if (expectedMIC[0] == recvMIC0 && expectedMIC[1] == recvMIC1) {
    return true;
  }

  if (verbose) {
    Serial.print(F("[CRYPT] MIC mismatch: got "));
    if (recvMIC0 < 0x10) Serial.print('0');
    Serial.print(recvMIC0, HEX);
    if (recvMIC1 < 0x10) Serial.print('0');
    Serial.print(recvMIC1, HEX);
    Serial.print(F(" expected "));
    if (expectedMIC[0] < 0x10) Serial.print('0');
    Serial.print(expectedMIC[0], HEX);
    if (expectedMIC[1] < 0x10) Serial.print('0');
    Serial.println(expectedMIC[1], HEX);
  }

  return false;
}

/* ====================================================================== */
/* ====== Pairing protocol (receiver side) ====== */
/* ====================================================================== */

/**
 * Generate pseudo-random AES-128 key
 * Uses ADC noise + micros() as entropy source.
 * Acceptable: pairing key is sent in cleartext anyway (same model as Zigbee TC key)
 */
void generateRandomKey(uint8_t *key) {
  for (int i = 0; i < AES_KEY_SIZE; i++) {
    uint32_t entropy = 0;
    for (int b = 0; b < 8; b++) {
      entropy = (entropy << 1) | (analogRead(A0) & 1);
      delayMicroseconds(10);
    }
    entropy ^= micros();
    entropy ^= analogRead(A0);
    key[i] = (uint8_t)(entropy & 0xFF);
  }
}

/**
 * Send PAIR_RESPONSE packet (20 bytes):
 *   [0]     0x14 (v1, pair_response)
 *   [1]     seq (echo from request)
 *   [2-17]  netKey[0..15] AES-128 key
 *   [18]    status 0x00 = OK
 *   [19]    op_channel (0..7)  - emitter switches to this for data
 *
 * After TX, the radio is switched to op_channel to listen for PAIR_CONFIRM.
 */
bool sendPairResponse(uint8_t seq) {
  uint8_t pkt[PAIR_RESPONSE_SIZE];

  pkt[0] = 0x14; /* v1 | PAIR_RESPONSE */
  pkt[1] = seq;
  memcpy(&pkt[2], netKey, AES_KEY_SIZE);
  pkt[18] = 0x00;        /* status OK */
  pkt[19] = opChannel;   /* tell emitter which channel to use for ops */

  Serial.print(F("[PAIR] TX PAIR_RESPONSE seq="));
  Serial.print(seq);
  Serial.print(F(" op_channel="));
  Serial.print(opChannel);
  Serial.print(F(" Key="));
  for (int i = 0; i < AES_KEY_SIZE; i++) {
    if (netKey[i] < 0x10) Serial.print('0');
    Serial.print(netKey[i], HEX);
  }
  Serial.println();

  /* Switch to TX (still on PAIR_CHANNEL where we received PAIR_REQUEST) */
  radio.clearDio1Action();
  int state = radio.transmit(pkt, PAIR_RESPONSE_SIZE);

  if (state != RADIOLIB_ERR_NONE) {
    Serial.print(F("[PAIR] PAIR_RESPONSE TX failed: "));
    Serial.println(state);
    radio.setDio1Action(rxDone);
    radio.startReceive(0xFFFF);
    return false;
  }
  Serial.println(F("[PAIR] PAIR_RESPONSE sent OK"));

  /* Switch RF to op_channel : that's where the emitter will send PAIR_CONFIRM */
  setChannel(opChannel);
  radio.setDio1Action(rxDone);
  radio.startReceive(0xFFFF);
  return true;
}

/**
 * Handle incoming PAIR_REQUEST
 * Extract MAC, generate key, send PAIR_RESPONSE
 */
void handlePairRequest(uint8_t *buf, int len) {
  if (len != PAIR_REQUEST_SIZE) {
    Serial.print(F("[PAIR] PAIR_REQUEST wrong size: "));
    Serial.println(len);
    return;
  }

  uint8_t seq = buf[1];
  uint8_t channel = buf[10];

  /* Extract emitter MAC */
  memcpy(emitterMAC, &buf[2], MAC_SIZE);

  Serial.print(F("[PAIR] PAIR_REQUEST from MAC="));
  for (int i = 0; i < MAC_SIZE; i++) {
    if (emitterMAC[i] < 0x10) Serial.print('0');
    Serial.print(emitterMAC[i], HEX);
  }
  Serial.print(F(" seq="));
  Serial.print(seq);
  Serial.print(F(" ch="));
  Serial.println(channel);

  /* Generate new AES-128 key */
  generateRandomKey(netKey);

  /* Send PAIR_RESPONSE */
  sendPairResponse(seq);

  /* Now wait for PAIR_CONFIRM (will be handled in next rxFlag) */
  Serial.println(F("[PAIR] Waiting for PAIR_CONFIRM..."));
}

/**
 * Handle incoming PAIR_CONFIRM
 * Verify AES-ECB proof, save to EEPROM if valid
 */
void handlePairConfirm(uint8_t *buf, int len) {
  if (len != PAIR_CONFIRM_SIZE) {
    Serial.print(F("[PAIR] PAIR_CONFIRM wrong size: "));
    Serial.println(len);
    return;
  }

  uint8_t seq = buf[1];
  uint8_t rxMAC[MAC_SIZE];
  memcpy(rxMAC, &buf[2], MAC_SIZE);
  uint8_t rxProof[4];
  memcpy(rxProof, &buf[10], 4);
  uint8_t status = buf[14];

  Serial.print(F("[PAIR] PAIR_CONFIRM seq="));
  Serial.print(seq);
  Serial.print(F(" status=0x"));
  Serial.print(status, HEX);
  Serial.print(F(" proof="));
  for (int i = 0; i < 4; i++) {
    if (rxProof[i] < 0x10) Serial.print('0');
    Serial.print(rxProof[i], HEX);
  }
  Serial.println();

  /* Verify MAC matches */
  if (memcmp(rxMAC, emitterMAC, MAC_SIZE) != 0) {
    Serial.println(F("[PAIR] PAIR_CONFIRM MAC mismatch — rejected"));
    return;
  }

  /* Verify AES-ECB proof: ECB(MAC padded to 16B, netKey) */
  uint8_t ecbInput[AES_BLOCK_SIZE];
  uint8_t ecbOutput[AES_BLOCK_SIZE];

  memset(ecbInput, 0, AES_BLOCK_SIZE);
  memcpy(ecbInput, emitterMAC, MAC_SIZE);

  aes128_ecb_encrypt(ecbInput, netKey, ecbOutput);

  /* Compare first 4 bytes */
  if (memcmp(rxProof, ecbOutput, 4) != 0) {
    Serial.print(F("[PAIR] Proof mismatch! Expected: "));
    for (int i = 0; i < 4; i++) {
      if (ecbOutput[i] < 0x10) Serial.print('0');
      Serial.print(ecbOutput[i], HEX);
    }
    Serial.println();
    return;
  }

  /* SUCCESS — add this pair to the array */
  int idx = addOrUpdateEmitter(emitterMAC, netKey);
  if (idx < 0) {
    Serial.print(F("[PAIR] No free slot! Maximum "));
    Serial.print(MAX_PAIRED_EMITTERS);
    Serial.println(F(" emitters reached. Use 'E' to erase or 'R N' to remove a slot."));
    return;
  }

  Serial.print(F("[PAIR] *** PAIRING SUCCESSFUL *** (slot "));
  Serial.print(idx);
  Serial.println(F(")"));
  isPaired = true;
  rxPairState = RX_PAIRED;
  savePairingToEEPROM();

  Serial.print(F("[PAIR] Paired with MAC="));
  for (int i = 0; i < MAC_SIZE; i++) {
    if (emitterMAC[i] < 0x10) Serial.print('0');
    Serial.print(emitterMAC[i], HEX);
  }
  Serial.print(F(" (total emitters: "));
  Serial.print(countPairedEmitters());
  Serial.print(F("/"));
  Serial.print(MAX_PAIRED_EMITTERS);
  Serial.println(F(")"));
}

/* ====================================================================== */
/* ====== Parse DATA_ESSENTIAL (17B cleartext, 19B with MIC) ====== */
/* ====================================================================== */
void parseEssential(uint8_t *buf, int len, float rssi, float snr) {
  if (len != PKT_ESSENTIAL_SIZE) {
    Serial.print(F("[RX] Essential taille inattendue: "));
    Serial.print(len);
    Serial.print(F("B  HEX: "));
    hexDump(buf, len);
    return;
  }

  uint8_t  seq     = buf[1];
  uint16_t sinsts  = getU16(&buf[2]);
  uint16_t sinsts1 = getU16(&buf[4]);
  uint16_t sinsts2 = getU16(&buf[6]);
  uint16_t sinsts3 = getU16(&buf[8]);
  /* STGE = 4 bytes binaire [10-13] */
  uint16_t adps    = getU16(&buf[14]);
  uint8_t  mode    = buf[16];

  trackSequence(seq);

  Serial.println(F("========== LINKY ESSENTIAL =========="));

  Serial.print(F("  #"));
  Serial.print(rxCount);
  Serial.print(F(" seq="));
  Serial.print(seq);
  Serial.print(F("  Mode: "));
  Serial.println(modeStr(mode));

  Serial.print(F("  SINSTS : "));
  Serial.print(sinsts);
  Serial.println(F(" VA (total)"));

  if (isTri(mode)) {
    Serial.print(F("  SINSTS1: "));
    Serial.print(sinsts1);
    Serial.print(F("  SINSTS2: "));
    Serial.print(sinsts2);
    Serial.print(F("  SINSTS3: "));
    Serial.print(sinsts3);
    Serial.println(F(" VA"));
  } else {
    Serial.print(F("  SINSTS1: "));
    Serial.print(sinsts1);
    Serial.println(F(" VA"));
  }

  /* STGE (4 bytes binaire -> affichage hex) */
  Serial.print(F("  STGE   : "));
  for (int i = 10; i < 14; i++) {
    if (buf[i] < 0x10) Serial.print('0');
    Serial.print(buf[i], HEX);
  }
  Serial.println();

  if (adps > 0) {
    Serial.print(F("  ADPS   : "));
    Serial.print(adps);
    Serial.println(F(" VA ***ALERTE***"));
  }

  printStats(rssi, snr);
  Serial.println(F("=====================================\r\n"));
}

/* ====== Parse EXT 0x00 : CURRENT_VOLTAGE (15B) ====== */
void parseExtCurrentVoltage(uint8_t *buf, int len, float rssi, float snr) {
  if (len != PKT_EXT_IV_SIZE) {
    Serial.print(F("[EXT] CurrentVoltage taille: "));
    Serial.println(len);
    return;
  }

  uint8_t seq = buf[1];
  trackSequence(seq);

  uint16_t irms1 = getU16(&buf[3]);
  uint16_t irms2 = getU16(&buf[5]);
  uint16_t irms3 = getU16(&buf[7]);
  uint16_t urms1 = getU16(&buf[9]);
  uint16_t urms2 = getU16(&buf[11]);
  uint16_t urms3 = getU16(&buf[13]);

  Serial.println(F("---------- EXT: CURRENT_VOLTAGE ----------"));
  Serial.print(F("  #"));
  Serial.print(rxCount);
  Serial.print(F(" seq="));
  Serial.println(seq);

  Serial.print(F("  IRMS1  : "));
  Serial.print(irms1);
  Serial.print(F("  IRMS2: "));
  Serial.print(irms2);
  Serial.print(F("  IRMS3: "));
  Serial.print(irms3);
  Serial.println(F(" mA"));

  Serial.print(F("  URMS1  : "));
  Serial.print(urms1);
  Serial.print(F("  URMS2: "));
  Serial.print(urms2);
  Serial.print(F("  URMS3: "));
  Serial.print(urms3);
  Serial.println(F(" V"));

  printStats(rssi, snr);
  Serial.println(F("------------------------------------------\r\n"));
}

/* ====== Parse EXT 0x01 : ENERGY (19B) ====== */
void parseExtEnergy(uint8_t *buf, int len, float rssi, float snr) {
  if (len != PKT_EXT_NRG_SIZE) {
    Serial.print(F("[EXT] Energy taille: "));
    Serial.println(len);
    return;
  }

  uint8_t seq = buf[1];
  trackSequence(seq);

  uint32_t east   = getU32(&buf[3]);
  uint32_t eait   = getU32(&buf[7]);
  uint32_t easf01 = getU32(&buf[11]);
  uint32_t easf02 = getU32(&buf[15]);

  Serial.println(F("---------- EXT: ENERGY ----------"));
  Serial.print(F("  #"));
  Serial.print(rxCount);
  Serial.print(F(" seq="));
  Serial.println(seq);

  Serial.print(F("  EAST   : "));
  Serial.print(east);
  Serial.print(F(" Wh ("));
  Serial.print(east / 1000.0, 3);
  Serial.println(F(" kWh)"));

  Serial.print(F("  EAIT   : "));
  Serial.print(eait);
  Serial.print(F(" Wh ("));
  Serial.print(eait / 1000.0, 3);
  Serial.println(F(" kWh) [injection]"));

  Serial.print(F("  EASF01 : "));
  Serial.print(easf01);
  Serial.print(F(" Wh ("));
  Serial.print(easf01 / 1000.0, 3);
  Serial.println(F(" kWh)"));

  Serial.print(F("  EASF02 : "));
  Serial.print(easf02);
  Serial.print(F(" Wh ("));
  Serial.print(easf02 / 1000.0, 3);
  Serial.println(F(" kWh)"));

  printStats(rssi, snr);
  Serial.println(F("---------------------------------\r\n"));
}

/* ====== Parse EXT 0x02 : ENERGY_2 (19B) ====== */
void parseExtEnergy2(uint8_t *buf, int len, float rssi, float snr) {
  if (len != PKT_EXT_NRG2_SIZE) {
    Serial.print(F("[EXT] Energy2 taille: "));
    Serial.println(len);
    return;
  }

  uint8_t seq = buf[1];
  trackSequence(seq);

  uint32_t easf03 = getU32(&buf[3]);
  uint32_t easf04 = getU32(&buf[7]);
  uint32_t easf05 = getU32(&buf[11]);
  uint32_t easf06 = getU32(&buf[15]);

  Serial.println(F("---------- EXT: ENERGY_2 ----------"));
  Serial.print(F("  #"));
  Serial.print(rxCount);
  Serial.print(F(" seq="));
  Serial.println(seq);

  Serial.print(F("  EASF03 : "));
  Serial.print(easf03);
  Serial.print(F(" Wh ("));
  Serial.print(easf03 / 1000.0, 3);
  Serial.println(F(" kWh)"));

  Serial.print(F("  EASF04 : "));
  Serial.print(easf04);
  Serial.print(F(" Wh ("));
  Serial.print(easf04 / 1000.0, 3);
  Serial.println(F(" kWh)"));

  Serial.print(F("  EASF05 : "));
  Serial.print(easf05);
  Serial.print(F(" Wh ("));
  Serial.print(easf05 / 1000.0, 3);
  Serial.println(F(" kWh)"));

  Serial.print(F("  EASF06 : "));
  Serial.print(easf06);
  Serial.print(F(" Wh ("));
  Serial.print(easf06 / 1000.0, 3);
  Serial.println(F(" kWh)"));

  printStats(rssi, snr);
  Serial.println(F("------------------------------------\r\n"));
}

/* ====== Parse EXT 0x03 : VOLTAGE_STATS (15B) ====== */
void parseExtVoltageStats(uint8_t *buf, int len, float rssi, float snr) {
  if (len != PKT_EXT_VSTAT_SIZE) {
    Serial.print(F("[EXT] VoltageStats taille: "));
    Serial.println(len);
    return;
  }

  uint8_t seq = buf[1];
  trackSequence(seq);

  uint16_t umoy1 = getU16(&buf[3]);
  uint16_t umoy2 = getU16(&buf[5]);
  uint16_t umoy3 = getU16(&buf[7]);
  uint16_t imax1 = getU16(&buf[9]);
  uint16_t imax2 = getU16(&buf[11]);
  uint16_t imax3 = getU16(&buf[13]);

  Serial.println(F("---------- EXT: VOLTAGE_STATS ----------"));
  Serial.print(F("  #"));
  Serial.print(rxCount);
  Serial.print(F(" seq="));
  Serial.println(seq);

  Serial.print(F("  UMOY1  : "));
  Serial.print(umoy1);
  Serial.print(F("  UMOY2: "));
  Serial.print(umoy2);
  Serial.print(F("  UMOY3: "));
  Serial.print(umoy3);
  Serial.println(F(" V"));

  Serial.print(F("  IMAX1  : "));
  Serial.print(imax1);
  Serial.print(F("  IMAX2: "));
  Serial.print(imax2);
  Serial.print(F("  IMAX3: "));
  Serial.print(imax3);
  Serial.println(F(" mA"));

  printStats(rssi, snr);
  Serial.println(F("----------------------------------------\r\n"));
}

/* ====== Parse EXT 0x04 : POWER_MAX_CFG (18B) ====== */
void parseExtPowerMaxCfg(uint8_t *buf, int len, float rssi, float snr) {
  if (len != PKT_EXT_PMCFG_SIZE) {
    Serial.print(F("[EXT] PowerMaxCfg taille: "));
    Serial.println(len);
    return;
  }

  uint8_t seq = buf[1];
  trackSequence(seq);

  uint16_t smaxsn = getU16(&buf[3]);
  uint16_t smaxin = getU16(&buf[5]);
  uint16_t sinsti = getU16(&buf[7]);
  uint16_t ccain  = getU16(&buf[9]);
  uint16_t relais = getU16(&buf[11]);
  uint8_t  ntarf  = buf[13];
  uint8_t  pcoup  = buf[14];
  uint8_t  pref   = buf[15];
  uint8_t  mode   = buf[16];

  Serial.println(F("---------- EXT: POWER_MAX_CFG ----------"));
  Serial.print(F("  #"));
  Serial.print(rxCount);
  Serial.print(F(" seq="));
  Serial.println(seq);

  Serial.print(F("  SMAXSN : "));
  Serial.print(smaxsn);
  Serial.println(F(" VA (soutire max jour)"));

  Serial.print(F("  SMAXIN : "));
  Serial.print(smaxin);
  Serial.println(F(" VA (injecte max jour)"));

  Serial.print(F("  SINSTI : "));
  Serial.print(sinsti);
  Serial.println(F(" VA (instantane)"));

  Serial.print(F("  CCAIN  : "));
  Serial.print(ccain);
  Serial.println(F(" VA (courbe charge)"));

  Serial.print(F("  RELAIS : 0x"));
  if (relais < 0x1000) Serial.print('0');
  if (relais < 0x100)  Serial.print('0');
  if (relais < 0x10)   Serial.print('0');
  Serial.println(relais, HEX);

  Serial.print(F("  NTARF  : "));
  Serial.println(ntarf);

  Serial.print(F("  PCOUP  : "));
  Serial.print(pcoup);
  Serial.println(F(" kVA"));

  Serial.print(F("  PREF   : "));
  Serial.print(pref);
  Serial.println(F(" kVA"));

  Serial.print(F("  Mode   : "));
  Serial.println(modeStr(mode));

  printStats(rssi, snr);
  Serial.println(F("----------------------------------------\r\n"));
}

/* ====== Parse EXT 0x05 : DAILY_ENERGY (19B) ====== */
void parseExtDailyEnergy(uint8_t *buf, int len, float rssi, float snr) {
  if (len != PKT_EXT_DAILY_SIZE) {
    Serial.print(F("[EXT] DailyEnergy taille: "));
    Serial.println(len);
    return;
  }

  uint8_t seq = buf[1];
  trackSequence(seq);

  uint32_t easd01 = getU32(&buf[3]);
  uint32_t easd02 = getU32(&buf[7]);
  uint32_t easd03 = getU32(&buf[11]);
  uint32_t easd04 = getU32(&buf[15]);

  Serial.println(F("---------- EXT: DAILY_ENERGY ----------"));
  Serial.print(F("  #"));
  Serial.print(rxCount);
  Serial.print(F(" seq="));
  Serial.println(seq);

  Serial.print(F("  EASD01 : "));
  Serial.print(easd01);
  Serial.print(F(" Wh ("));
  Serial.print(easd01 / 1000.0, 3);
  Serial.println(F(" kWh) [jour J]"));

  Serial.print(F("  EASD02 : "));
  Serial.print(easd02);
  Serial.print(F(" Wh ("));
  Serial.print(easd02 / 1000.0, 3);
  Serial.println(F(" kWh) [jour J-1]"));

  Serial.print(F("  EASD03 : "));
  Serial.print(easd03);
  Serial.print(F(" Wh ("));
  Serial.print(easd03 / 1000.0, 3);
  Serial.println(F(" kWh) [jour J-2]"));

  Serial.print(F("  EASD04 : "));
  Serial.print(easd04);
  Serial.print(F(" Wh ("));
  Serial.print(easd04 / 1000.0, 3);
  Serial.println(F(" kWh) [jour J-3]"));

  printStats(rssi, snr);
  Serial.println(F("---------------------------------------\r\n"));
}

/* ====== Parse EXT 0x06 : COMPLEMENT (9B) ====== */
void parseExtComplement(uint8_t *buf, int len, float rssi, float snr) {
  if (len != PKT_EXT_COMP_SIZE) {
    Serial.print(F("[EXT] Complement taille: "));
    Serial.println(len);
    return;
  }

  uint8_t seq = buf[1];
  trackSequence(seq);

  uint8_t dpm1 = buf[3];
  uint8_t fpm1 = buf[4];
  uint8_t dpm2 = buf[5];
  uint8_t fpm2 = buf[6];
  uint8_t dpm3 = buf[7];
  uint8_t fpm3 = buf[8];

  Serial.println(F("---------- EXT: COMPLEMENT ----------"));
  Serial.print(F("  #"));
  Serial.print(rxCount);
  Serial.print(F(" seq="));
  Serial.println(seq);

  Serial.print(F("  DPM1   : "));
  Serial.print(dpm1);
  Serial.print(F("  FPM1: "));
  Serial.println(fpm1);

  Serial.print(F("  DPM2   : "));
  Serial.print(dpm2);
  Serial.print(F("  FPM2: "));
  Serial.println(fpm2);

  Serial.print(F("  DPM3   : "));
  Serial.print(dpm3);
  Serial.print(F("  FPM3: "));
  Serial.println(fpm3);

  printStats(rssi, snr);
  Serial.println(F("-------------------------------------\r\n"));
}

/* ====================================================================== */
/* ====== Dispatch principal ====== */
/* ====================================================================== */
void parseAndPrint(uint8_t *buf, int len, float rssi, float snr) {
  if (len < 2) {
    Serial.print(F("[RX] Paquet trop court: "));
    Serial.print(len);
    Serial.print(F("B  HEX: "));
    hexDump(buf, len);
    return;
  }

  uint8_t version = (buf[0] >> 4) & 0x0F;
  uint8_t type    = buf[0] & 0x0F;

  if (version != 0x01) {
    Serial.print(F("[RX] Version inconnue: 0x"));
    Serial.println(buf[0], HEX);
    rxErrors++;
    return;
  }

  /* ---- Pairing packets (not encrypted) ---- */
  if (type == PKT_TYPE_PAIR_REQUEST) {
    if (rxPairState == RX_PAIR_LISTEN) {
      handlePairRequest(buf, len);
    } else {
      Serial.println(F("[RX] PAIR_REQUEST ignored (long press USER button to enter pairing)"));
    }
    return;
  }

  if (type == PKT_TYPE_PAIR_CONFIRM) {
    if (rxPairState == RX_PAIR_LISTEN) {
      handlePairConfirm(buf, len);
    } else {
      Serial.println(F("[RX] PAIR_CONFIRM ignored (not in listen mode)"));
    }
    return;
  }

  /* ---- Data packets (must be paired + decrypted) ---- */
  if (!isPaired) {
    Serial.print(F("[RX] Data packet ignored (not paired). type=0x"));
    Serial.print(type, HEX);
    Serial.print(F(" len="));
    Serial.print(len);
    Serial.print(F(" HEX: "));
    hexDump(buf, len);
    return;
  }

  /* Decrypt + verify MIC */
  if (len < 4) { /* minimum: type + seq + 0 payload + 2 MIC */
    Serial.print(F("[RX] Packet too short for decrypt: "));
    Serial.println(len);
    rxErrors++;
    return;
  }

  /* Try decrypt with each known emitter (MIC validates the right pair). */
  int matched = -1;
  uint8_t backupBuf[256];
  if (len > (int)sizeof(backupBuf)) {
    Serial.println(F("[RX] Packet too large for backup buffer"));
    rxErrors++;
    return;
  }
  memcpy(backupBuf, buf, len);  /* save original ciphertext for retries */

  for (int i = 0; i < MAX_PAIRED_EMITTERS; i++) {
    if (!emitters[i].valid) continue;
    /* Restore ciphertext for this attempt */
    memcpy(buf, backupBuf, len);
    /* Silent trial: pas de print MIC mismatch pour ce slot, on essaie le suivant */
    if (decryptPacket(buf, len, emitters[i].key, emitters[i].mac, false)) {
      matched = i;
      break;
    }
  }

  if (matched < 0) {
    Serial.print(F("[RX] Decrypt/MIC failed for all emitters! len="));
    Serial.print(len);
    Serial.print(F(" HEX: "));
    hexDump(backupBuf, len);
    rxErrors++;
    return;
  }

  /* Update per-emitter stats */
  emitters[matched].rxCount++;
  uint8_t curSeq = buf[1];
  if (emitters[matched].seqInit) {
    uint8_t expected = (emitters[matched].lastSeq + 1) & 0xFF;
    if (curSeq != expected) {
      emitters[matched].missedPackets += (uint8_t)(curSeq - expected);
    }
  }
  emitters[matched].lastSeq = curSeq;
  emitters[matched].seqInit = true;

  /* Print which emitter sent this packet (compact) */
  Serial.print(F("[RX] from slot "));
  Serial.print(matched);
  Serial.print(F(" MAC="));
  for (int i = 4; i < MAC_SIZE; i++) {  /* show last 4 bytes of MAC for compactness */
    if (emitters[matched].mac[i] < 0x10) Serial.print('0');
    Serial.print(emitters[matched].mac[i], HEX);
  }
  Serial.print(F("  rx="));
  Serial.print(emitters[matched].rxCount);
  Serial.print(F(" miss="));
  Serial.println(emitters[matched].missedPackets);

  /* Set le slot pour les printStats en aval */
  currentSlotForStats = matched;

  /* After decryption: len without MIC = actual data length */
  int dataLen = len - CRYPT_MIC_SIZE;

  if (type == PKT_TYPE_ESSENTIAL) {
    parseEssential(buf, dataLen, rssi, snr);
  }
  else if (type == PKT_TYPE_EXTENDED) {
    if (dataLen < 3) {
      Serial.print(F("[RX] Extended trop court: "));
      Serial.println(dataLen);
      rxErrors++;
      return;
    }
    uint8_t subType = buf[2];
    switch (subType) {
      case EXT_CURRENT_VOLTAGE: parseExtCurrentVoltage(buf, dataLen, rssi, snr); break;
      case EXT_ENERGY:          parseExtEnergy(buf, dataLen, rssi, snr);         break;
      case EXT_ENERGY_2:        parseExtEnergy2(buf, dataLen, rssi, snr);        break;
      case EXT_VOLTAGE_STATS:   parseExtVoltageStats(buf, dataLen, rssi, snr);   break;
      case EXT_POWER_MAX_CFG:   parseExtPowerMaxCfg(buf, dataLen, rssi, snr);    break;
      case EXT_DAILY_ENERGY:    parseExtDailyEnergy(buf, dataLen, rssi, snr);   break;
      case EXT_COMPLEMENT:      parseExtComplement(buf, dataLen, rssi, snr);    break;
      default:
        Serial.print(F("[RX] Extended sub-type inconnu: 0x"));
        Serial.println(subType, HEX);
        Serial.print(F("  HEX: "));
        hexDump(buf, dataLen);
        trackSequence(buf[1]);
        break;
    }
  }
  else {
    Serial.print(F("[RX] Type inconnu: 0x"));
    Serial.println(buf[0], HEX);
    Serial.print(F("  HEX: "));
    hexDump(buf, dataLen);
    rxErrors++;
  }
}

/* ====================================================================== */
/* ====== OTA Gateway mode ====== */
/* ====================================================================== */

/* Parse hex string into byte buffer. Returns number of bytes decoded. */
static int parseHexBuf(const char *hex, uint8_t *buf, int maxLen) {
  int len = 0;
  while (*hex && *hex != '\r' && *hex != '\n' && len < maxLen) {
    /* skip spaces */
    while (*hex == ' ') hex++;
    if (!hex[0] || !hex[1]) break;
    char hi = hex[0], lo = hex[1];
    uint8_t hiv = (hi >= '0' && hi <= '9') ? hi - '0' :
                  (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10 :
                  (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10 : 0xFF;
    uint8_t lov = (lo >= '0' && lo <= '9') ? lo - '0' :
                  (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10 :
                  (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10 : 0xFF;
    if (hiv == 0xFF || lov == 0xFF) break;
    buf[len++] = (hiv << 4) | lov;
    hex += 2;
  }
  return len;
}

static void printHex(const uint8_t *buf, int len) {
  for (int i = 0; i < len; i++) {
    if (buf[i] < 0x10) Serial.print('0');
    Serial.print(buf[i], HEX);
  }
}

/* Encrypt + add MIC (same scheme as decryptPacket but reverse).
 * Inputs : pkt[0..len-1] = cleartext (type+seq+payload)
 * Output : pkt[0..len+1] = type+seq+ciphered_payload + MIC(2)
 * Returns: total length including MIC */
static int encryptOtaPacket(uint8_t *pkt, int len, const uint8_t *key, const uint8_t *mac) {
  if (len < 2) return 0;

  /* 1) Compute MIC over cleartext */
  uint8_t expectedMIC[16];
  aes128_cmac(pkt, (uint16_t)len, key, expectedMIC);

  /* 2) Encrypt payload (skip type+seq = 2 bytes header) */
  uint8_t nonce[16];
  buildNonce(nonce, mac, pkt[1]);
  int payloadLen = len - 2;
  if (payloadLen > 0) {
    aes128_ctr_crypt(&pkt[2], (uint16_t)payloadLen, key, nonce);
  }

  /* 3) Append MIC bytes [0..1] */
  pkt[len]     = expectedMIC[0];
  pkt[len + 1] = expectedMIC[1];
  return len + CRYPT_MIC_SIZE;
}

/* Send a raw OTA packet via LoRa, wait for OTA_ACK, decrypt + return payload.
 * Returns length of decrypted ACK payload (without MIC) or 0 on timeout. */
static int otaTxAndWaitAck(uint8_t *pktClear, int clearLen, uint8_t *ackOut, int ackMaxLen) {
  if (!otaGwInitialized) {
    Serial.println(F("OTA_ERROR not_initialized"));
    return 0;
  }

  /* Encrypt + MIC */
  uint8_t txBuf[256];
  if (clearLen + CRYPT_MIC_SIZE > (int)sizeof(txBuf)) {
    Serial.println(F("OTA_ERROR pkt_too_big"));
    return 0;
  }
  memcpy(txBuf, pktClear, clearLen);
  int txLen = encryptOtaPacket(txBuf, clearLen, otaGwTargetKey, otaGwTargetMAC);

  /* Switch to TX */
  radio.clearDio1Action();
  int s = radio.transmit(txBuf, txLen);
  if (s != RADIOLIB_ERR_NONE) {
    Serial.print(F("OTA_TX_FAIL "));
    Serial.println(s);
    radio.setDio1Action(rxDone);
    radio.startReceive(0xFFFF);
    return 0;
  }

  /* Switch to RX, wait for ACK with timeout */
  radio.setDio1Action(rxDone);
  rxFlag = false;
  radio.startReceive(0xFFFF);

  unsigned long deadline = millis() + OTA_ACK_TIMEOUT_MS;
  while (millis() < deadline) {
    if (rxFlag) {
      rxFlag = false;
      uint8_t buf[128];     /* assez pour OTA_BLOCK (70B) + paquets TIC voisins */
      uint8_t backupBuf[128];
      int len = radio.getPacketLength();
      if (len > 0 && len <= (int)sizeof(buf)) {
        int rs = radio.readData(buf, len);
        if (rs == RADIOLIB_ERR_NONE) {
          /* Capture RSSI/SNR avant que readData ne les invalide */
          float pktRssi = radio.getRSSI();
          float pktSnr  = radio.getSNR();

          /* Sauvegarde le ciphertext pour pouvoir reessayer (decryptPacket
           * fait du AES-CTR in-place, donc le buffer est modifie meme
           * quand le MIC echoue). */
          memcpy(backupBuf, buf, len);

          /* 1) Tente d'abord la cle de la cible OTA — silencieux */
          if (decryptPacket(buf, len, otaGwTargetKey, otaGwTargetMAC, false)) {
            int plainLen = len - CRYPT_MIC_SIZE;
            if (plainLen <= ackMaxLen) {
              memcpy(ackOut, buf, plainLen);
              return plainLen;
            }
          }

          /* 2) Pas un ACK OTA : c'est peut-etre une trame TIC d'un autre
           *    emitter appaire. On restaure le ciphertext et on passe au
           *    dispatch normal, qui essaie toutes les cles et forwarde sur
           *    Serial si match. Cela permet aux autres devices de continuer
           *    a etre servis pendant la mise a jour OTA. */
          memcpy(buf, backupBuf, len);
          parseAndPrint(buf, len, pktRssi, pktSnr);
        }
      }
      /* mauvaise donnee, paquet voisin traite, ou rien : on continue d'attendre l'ACK OTA */
      radio.startReceive(0xFFFF);
    }
  }
  return 0;  /* timeout */
}

/* Handle a Serial command starting with 'O' :
 *   "OTA_GW_INIT <mac_hex>"  → load key for mac, return OTA_GW_READY
 *   "OTA_TX <hex_packet>"    → encrypt + tx + wait ack → "OTA_RX <hex>" or "OTA_TIMEOUT"
 */
static void handleOtaGatewayCommand() {
  /* Read remainder of line "TA_..." */
  char buf[400];
  int n = 0;
  unsigned long deadline = millis() + 100;
  while (millis() < deadline && n < (int)sizeof(buf) - 1) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') break;
      buf[n++] = c;
    }
  }
  buf[n] = 0;

  if (strncmp(buf, "TA_GW_INIT", 10) == 0) {
    const char *p = buf + 10;
    while (*p == ' ') p++;
    uint8_t mac[8];
    int ml = parseHexBuf(p, mac, 8);
    if (ml != 8) {
      Serial.println(F("OTA_GW_ERROR bad_mac"));
      return;
    }
    /* Find emitter slot for this MAC */
    int idx = findEmitterByMAC(mac);
    if (idx < 0) {
      Serial.println(F("OTA_GW_ERROR mac_not_paired"));
      return;
    }
    memcpy(otaGwTargetMAC, emitters[idx].mac, MAC_SIZE);
    memcpy(otaGwTargetKey, emitters[idx].key, AES_KEY_SIZE);
    otaGwInitialized = true;
    rxPairState = RX_OTA_GATEWAY;
    radio.setDio1Action(rxDone);
    radio.startReceive(0xFFFF);
    Serial.println(F("OTA_GW_READY"));
  }
  else if (strncmp(buf, "TA_TX", 5) == 0) {
    if (!otaGwInitialized) {
      Serial.println(F("OTA_GW_ERROR not_initialized"));
      return;
    }
    const char *p = buf + 5;
    while (*p == ' ') p++;
    uint8_t pkt[200];
    int len = parseHexBuf(p, pkt, sizeof(pkt) - CRYPT_MIC_SIZE);
    if (len < 2) {
      Serial.println(F("OTA_GW_ERROR bad_packet"));
      return;
    }

    uint8_t ack[64];
    int ackLen = otaTxAndWaitAck(pkt, len, ack, sizeof(ack));
    if (ackLen > 0) {
      Serial.print(F("OTA_RX "));
      printHex(ack, ackLen);
      Serial.println();
    } else {
      Serial.println(F("OTA_TIMEOUT"));
    }
  }
  else if (strncmp(buf, "TA_GW_EXIT", 10) == 0) {
    otaGwInitialized = false;
    rxPairState = isPaired ? RX_PAIRED : RX_IDLE;
    radio.startReceive(0xFFFF);
    Serial.println(F("OTA_GW_EXITED"));
  }
  else {
    Serial.print(F("OTA_GW_ERROR unknown_cmd: O"));
    Serial.println(buf);
  }
}

/* ====================================================================== */
/* ====== Setup ====== */
/* ====================================================================== */
void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println(F("\r\n[RX] ZLinky TIC Receiver SX1280 (multi-channel)"));
  Serial.println(F("[RX] 8 canaux ISM 2410-2480 MHz, SF10 BW406 CR4/5"));
  Serial.println(F("[RX] Essential 1s + Extended piggyback 10s"));
  Serial.println(F("[RX] AES-128-CTR + CMAC MIC enabled"));
  Serial.println(F("[RX] Max 22 octets/paquet (SF10 + MIC)\r\n"));

  /* Load pairing from EEPROM (sets opChannel if previously paired) */
  loadPairingFromEEPROM();

  /* Init radio with PAIR_CHANNEL frequency for the CCA scan, SF11 BW406 */
  int state = radio.begin(LORA_CHANNEL_FREQS_MHZ[LORA_PAIR_CHANNEL],
                         406.25, 11, 5, 0x12, 10, 16);  /* SF11 */
  Serial.print(F("Init: "));
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print(F("FAIL code "));
    Serial.println(state);
    while (true) delay(1000);
  }
  Serial.println(F("OK"));

  radio.setCRC(2);
  radio.explicitHeader();
  radio.invertIQ(false);

  /* Determine operational channel:
   *   1. If already paired (EEPROM magic OK): use saved op_channel
   *   2. Else if EEPROM_OPCH_ADDR has a valid value (set via 'C N' command): use it
   *   3. Else if DEFAULT_OP_CHANNEL is a valid index (0..7): use it
   *   4. Else: do CCA scan to pick the cleanest channel
   */
  if (isPaired) {
    Serial.print(F("[BOOT] Paired, using saved op_channel = "));
    Serial.println(opChannel);
  } else {
    /* Cas non-paired : verifier d'abord si l'utilisateur a set un canal via 'C N' */
    uint8_t storedCh = EEPROM.read(EEPROM_OPCH_ADDR);
    if (APP_isValidChannel(storedCh)) {
      opChannel = storedCh;
      Serial.print(F("[BOOT] Not paired, using user-set op_channel = "));
      Serial.print(opChannel);
      Serial.print(F(" ("));
      Serial.print(LORA_CHANNEL_FREQS_MHZ[opChannel], 0);
      Serial.println(F(" MHz)"));
    } else if (DEFAULT_OP_CHANNEL >= 0 && DEFAULT_OP_CHANNEL < LORA_NUM_CHANNELS) {
      opChannel = DEFAULT_OP_CHANNEL;
      Serial.print(F("[BOOT] Not paired, using DEFAULT_OP_CHANNEL = "));
      Serial.print(opChannel);
      Serial.print(F(" ("));
      Serial.print(LORA_CHANNEL_FREQS_MHZ[opChannel], 0);
      Serial.println(F(" MHz)"));
    } else {
      uint8_t scanned = ccaScan();
      opChannel = scanned;
      Serial.print(F("[BOOT] Not paired, CCA-selected op_channel = "));
      Serial.println(opChannel);
    }
  }

  setChannel(opChannel);
  radio.setDio1Action(rxDone);

  state = radio.startReceive(0xFFFF);
  Serial.print(F("startReceive(continuous): "));
  Serial.println(state == RADIOLIB_ERR_NONE ? F("OK") : String(state));

  /* Determine initial state (pairing only via long press on USER button) */
  if (isPaired) {
    rxPairState = RX_PAIRED;
    Serial.println(F("[PAIR] Already paired — receiving encrypted data"));
  } else {
    rxPairState = RX_IDLE;
    Serial.println(F("[PAIR] Not paired — long press USER button (3s) to enter pairing mode"));
  }

  /* Init bouton USER (B1 = PC13, pull-up interne, actif LOW) */
  pinMode(USER_BUTTON_PIN, INPUT_PULLUP);
  Serial.println(F("[BTN] Bouton USER (B1) : appui long 3s = entrer mode pairing 30s"));

  Serial.println(F("[CMD] Serial commands :"));
  Serial.println(F("       'C N'           set op_channel = N (0..7), erase ALL"));
  Serial.println(F("       'S'             show status"));
  Serial.println(F("       'P'             enter PAIR_LISTEN (30s)"));
  Serial.println(F("       'R N'           remove emitter at slot N"));
  Serial.println(F("       'E'             erase ALL pairings"));
  Serial.println(F("       'OTA_GW_INIT M' enter OTA gateway mode for MAC=M"));
  Serial.println(F("       'OTA_TX <hex>'  send OTA packet, get ack"));
  Serial.println(F("       'OTA_GW_EXIT'   leave OTA gateway mode"));

  Serial.println(F("Attente paquets...\r\n"));
}

/* ====================================================================== */
/* ====== Serial command handler ====== */
/* ====================================================================== */
void handleSerialCommand() {
  if (!Serial.available()) return;

  char cmd = Serial.read();
  /* skip whitespace and CR/LF */
  while (cmd == ' ' || cmd == '\r' || cmd == '\n' || cmd == '\t') {
    if (!Serial.available()) return;
    cmd = Serial.read();
  }

  if (cmd == 'C' || cmd == 'c') {
    /* "C N" : set op_channel to N */
    while (!Serial.available()) { delay(1); }
    int n = Serial.parseInt();
    if (n < 0 || n >= LORA_NUM_CHANNELS) {
      Serial.print(F("[CMD] Invalid channel "));
      Serial.print(n);
      Serial.print(F(", must be 0.."));
      Serial.println(LORA_NUM_CHANNELS - 1);
      return;
    }
    Serial.print(F("[CMD] Setting op_channel = "));
    Serial.print(n);
    Serial.print(F(" ("));
    Serial.print(LORA_CHANNEL_FREQS_MHZ[n], 0);
    Serial.println(F(" MHz)"));
    Serial.println(F("[CMD] Erasing pairing — re-pair all emitters required"));
    erasePairingFromEEPROM();
    opChannel = n;
    setChannel(opChannel);
    /* Save the new channel even without pair (so it persists on reboot) */
    /* On l'ecrit directement, le magic reste invalide tant que pas paire */
    EEPROM.update(EEPROM_OPCH_ADDR, opChannel);
    rxPairState = RX_IDLE;
    radio.startReceive(0xFFFF);
    Serial.println(F("[CMD] Done. Long press USER button to enter pairing mode."));
  }
  else if (cmd == 'S' || cmd == 's') {
    Serial.println(F("\r\n========== STATUS =========="));
    Serial.print(F("op_channel = "));
    Serial.print(opChannel);
    Serial.print(F(" ("));
    Serial.print(LORA_CHANNEL_FREQS_MHZ[opChannel], 0);
    Serial.println(F(" MHz)"));
    Serial.print(F("Paired emitters : "));
    Serial.print(countPairedEmitters());
    Serial.print(F("/"));
    Serial.println(MAX_PAIRED_EMITTERS);
    for (int i = 0; i < MAX_PAIRED_EMITTERS; i++) {
      Serial.print(F("  Slot "));
      Serial.print(i);
      Serial.print(F(": "));
      if (!emitters[i].valid) {
        Serial.println(F("(empty)"));
        continue;
      }
      Serial.print(F("MAC="));
      for (int b = 0; b < MAC_SIZE; b++) {
        if (emitters[i].mac[b] < 0x10) Serial.print('0');
        Serial.print(emitters[i].mac[b], HEX);
      }
      Serial.print(F(" rx="));
      Serial.print(emitters[i].rxCount);
      Serial.print(F(" miss="));
      Serial.println(emitters[i].missedPackets);
    }
    Serial.print(F("State : "));
    switch (rxPairState) {
      case RX_PAIR_LISTEN: Serial.println(F("PAIR_LISTEN")); break;
      case RX_PAIRED:      Serial.println(F("PAIRED")); break;
      case RX_IDLE:        Serial.println(F("IDLE")); break;
    }
    Serial.println(F("============================\r\n"));
  }
  else if (cmd == 'R' || cmd == 'r') {
    /* "R N" : remove emitter at slot N */
    while (!Serial.available()) { delay(1); }
    int n = Serial.parseInt();
    if (n < 0 || n >= MAX_PAIRED_EMITTERS) {
      Serial.print(F("[CMD] Invalid slot "));
      Serial.println(n);
      return;
    }
    if (!emitters[n].valid) {
      Serial.print(F("[CMD] Slot "));
      Serial.print(n);
      Serial.println(F(" already empty"));
      return;
    }
    Serial.print(F("[CMD] Removing slot "));
    Serial.println(n);
    emitters[n].valid = false;
    if (countPairedEmitters() == 0) isPaired = false;
    savePairingToEEPROM();
  }
  else if (cmd == 'O' || cmd == 'o') {
    /* "OTA_GW_INIT <mac_hex>"  ou  "OTA_TX <hex_packet>" */
    handleOtaGatewayCommand();
  }
  else if (cmd == 'P' || cmd == 'p') {
    Serial.print(F("[CMD] Forcing PAIR_LISTEN mode (30s) - existing emitters: "));
    Serial.print(countPairedEmitters());
    Serial.print(F("/"));
    Serial.println(MAX_PAIRED_EMITTERS);
    rxPairState = RX_PAIR_LISTEN;
    pairListenStart = millis();
    /* Note: on garde les emitters deja paired */
    setChannel(LORA_PAIR_CHANNEL);
    radio.setDio1Action(rxDone);
    radio.startReceive(0xFFFF);
  }
  else if (cmd == 'E' || cmd == 'e') {
    Serial.println(F("[CMD] Erasing pairing data..."));
    erasePairingFromEEPROM();
    rxPairState = RX_IDLE;
    Serial.println(F("[CMD] Pairing erased."));
  }
  else {
    Serial.print(F("[CMD] Unknown command '"));
    Serial.print(cmd);
    Serial.println(F("'. Use C N / S / P / E"));
  }

  /* drain rest of line */
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') break;
  }
}

/* ====================================================================== */
/* ====== Loop ====== */
/* ====================================================================== */
void loop() {
  /* ---- Serial commands (C/S/P/E) ---- */
  handleSerialCommand();

  /* ---- Bouton USER : appui long (3s) = entrer mode PAIR_LISTEN 30s ---- */
  bool btnState = digitalRead(USER_BUTTON_PIN);

  if (btnState == LOW && lastButtonState == HIGH) {
    /* Front descendant : debut appui */
    buttonPressStart = millis();
    buttonLongDetected = false;
  }

  if (btnState == LOW && !buttonLongDetected) {
    /* Bouton maintenu : verifier duree */
    if ((millis() - buttonPressStart) >= BUTTON_LONG_PRESS_MS) {
      buttonLongDetected = true;

      if (rxPairState != RX_PAIR_LISTEN) {
        Serial.println(F("[BTN] Appui long detecte — passage en PAIR_LISTEN (30s)"));
        Serial.print(F("[BTN] Existing emitters : "));
        Serial.print(countPairedEmitters());
        Serial.print(F("/"));
        Serial.println(MAX_PAIRED_EMITTERS);
        rxPairState = RX_PAIR_LISTEN;
        pairListenStart = millis();
        /* Note: on NE met PAS isPaired=false : on garde les emetteurs deja paired */

        /* Garder le opChannel courant.
         * Il a deja ete defini soit:
         *   - par l'EEPROM au boot (si paired)
         *   - par DEFAULT_OP_CHANNEL au boot (si vide + DEFAULT defini)
         *   - par CCA scan au boot (si vide + DEFAULT == -1)
         *   - par la commande Serial 'C N'
         * On NE le re-evalue PAS ici sinon on ecrase le choix utilisateur. */
        if (!APP_isValidChannel(opChannel)) {
          /* Cas defensif : opChannel invalide → fallback */
          if (DEFAULT_OP_CHANNEL >= 0 && DEFAULT_OP_CHANNEL < LORA_NUM_CHANNELS) {
            opChannel = DEFAULT_OP_CHANNEL;
          } else {
            opChannel = ccaScan();
          }
        }
        Serial.print(F("[PAIR] op_channel for next pairings = "));
        Serial.print(opChannel);
        Serial.print(F(" ("));
        Serial.print(LORA_CHANNEL_FREQS_MHZ[opChannel], 0);
        Serial.println(F(" MHz)"));

        /* Switch radio to PAIR_CHANNEL pour ecouter PAIR_REQUEST */
        setChannel(LORA_PAIR_CHANNEL);
        radio.setDio1Action(rxDone);
        radio.startReceive(0xFFFF);
      } else {
        Serial.println(F("[BTN] Deja en mode PAIR_LISTEN"));
      }
    }
  }

  lastButtonState = btnState;

  /* Check pairing timeout */
  if (rxPairState == RX_PAIR_LISTEN) {
    if (millis() - pairListenStart >= PAIR_LISTEN_TIMEOUT_MS) {
      if (isPaired) {
        rxPairState = RX_PAIRED;
        Serial.println(F("[PAIR] Pairing window expired (30s). Returning to PAIRED mode."));
        /* Switch back to op_channel for data RX */
        setChannel(opChannel);
        radio.setDio1Action(rxDone);
        radio.startReceive(0xFFFF);
      } else {
        rxPairState = RX_IDLE;
        Serial.println(F("[PAIR] Pairing window expired (30s). Not paired."));
        Serial.println(F("[PAIR] Press USER button or reboot to re-enter pairing mode.\r\n"));
        /* Stay on op_channel anyway (chosen by CCA scan) for future emitters */
        setChannel(opChannel);
        radio.setDio1Action(rxDone);
        radio.startReceive(0xFFFF);
      }
    }
  }

  /* En mode OTA gateway, l'ACK est consume directement par otaTxAndWaitAck().
   * On ne traite pas les data packets normaux ici. */
  if (rxPairState == RX_OTA_GATEWAY) {
    return;
  }

  if (rxFlag) {
    rxFlag = false;

    uint8_t buffer[255];
    int len = radio.getPacketLength();
    float rssi = radio.getRSSI();
    float snr = radio.getSNR();
    int state = radio.readData(buffer, len);

    if (state == RADIOLIB_ERR_NONE) {
      consecutiveCrcErrors = 0;
      rxSinceReset++;
      parseAndPrint(buffer, len, rssi, snr);

      if (snr < SNR_RESET_THRESHOLD) {
        doFullReset("SNR drift");
        return;
      }

      if (rxSinceReset >= PREVENTIVE_RESET_INTERVAL) {
        doFullReset("preventive");
        return;
      }

    } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
      rxErrors++;
      consecutiveCrcErrors++;
      Serial.print(F("[CRC ERROR] len="));
      Serial.print(len);
      Serial.print(F(" RSSI="));
      Serial.print(rssi, 0);
      Serial.print(F(" SNR="));
      Serial.print(snr, 1);
      Serial.print(F(" ("));
      Serial.print(consecutiveCrcErrors);
      Serial.print(F("/"));
      Serial.print(CRC_ERROR_RESET_THRESHOLD);
      Serial.print(F(") HEX: "));
      hexDump(buffer, len);
    } else {
      rxErrors++;
      consecutiveCrcErrors++;
      Serial.print(F("[RX ERROR] code "));
      Serial.println(state);
    }

    if (consecutiveCrcErrors >= CRC_ERROR_RESET_THRESHOLD) {
      doFullReset("CRC errors");
      return;
    }

    radio.startReceive(0xFFFF);
  }
}
