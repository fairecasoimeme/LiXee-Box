/*
 * Recepteur ZLinky LoRa 2.4 GHz (SX1281 + ESP32-S3) — portage fidele de ZLinky_TIC_Receiver.ino
 *
 * Appairage : ecoute PAIR_REQUEST sur canal 3 (2440) -> genere cle AES-128 -> envoie PAIR_RESPONSE
 * -> bascule sur op_channel (4) -> recoit PAIR_CONFIRM -> verifie preuve AES-ECB -> stocke MAC+cle.
 * Puis : recoit les trames TIC chiffrees -> AES-128-CTR + verif MIC CMAC -> parse et affiche.
 *
 * Cablage ESP32-S3 : SCK=4, MOSI=5, MISO=6, NSS=7, BUSY=8, RST=16, DIO1=2.
 */

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <esp_system.h>          // esp_random()
#include "aes128.h"              // aes128_ecb_encrypt / aes128_ctr_crypt / aes128_cmac

/* ---------- Brochage ---------- */
#define PIN_DIO1   2
#define PIN_SCK    4
#define PIN_MOSI   5
#define PIN_MISO   6
#define PIN_NSS    7
#define PIN_BUSY   8
#define PIN_RESET  16

SPIClass spi(FSPI);
SPISettings spiSet(2000000, MSBFIRST, SPI_MODE0);
SX1280 radio = new Module(PIN_NSS, PIN_DIO1, PIN_RESET, PIN_BUSY, spi, spiSet);

/* ---------- Plan de canaux (doit matcher l'emetteur) ---------- */
const float CH_FREQ[8] = {2410.0, 2420.0, 2430.0, 2440.0, 2450.0, 2460.0, 2470.0, 2480.0};
#define PAIR_CHANNEL   3          // 2440 MHz : handshake
#define OP_CHANNEL     4          // 2450 MHz : donnees (canal qu'on impose a l'emetteur)

/* ---------- Types & tailles ---------- */
#define T_ESSENTIAL      0x01
#define T_EXTENDED       0x02
#define T_PAIR_REQUEST   0x03
#define T_PAIR_RESPONSE  0x04
#define T_PAIR_CONFIRM   0x05

#define MIC_SIZE          2
#define MAC_SIZE          8
#define AES_KEY_SIZE      16
#define AES_BLOCK_SIZE    16
#define PAIR_REQUEST_SIZE 11
#define PAIR_RESPONSE_SIZE 20
#define PAIR_CONFIRM_SIZE 15

#define SZ_ESSENTIAL      17
#define SZ_EXT_IV         15
#define SZ_EXT_NRG        19
#define SZ_EXT_NRG2       19
#define SZ_EXT_VSTAT      15
#define SZ_EXT_PMCFG      18
#define SZ_EXT_DAILY      19
#define SZ_EXT_COMP        9

/* ---------- Etat ---------- */
volatile bool rxFlag = false;
enum RxState { ST_LISTEN, ST_WAIT_CONFIRM, ST_PAIRED };
RxState  rxState = ST_LISTEN;
uint8_t  curChannel = PAIR_CHANNEL;
uint8_t  emitterMAC[MAC_SIZE];
uint8_t  netKey[AES_KEY_SIZE];
bool     haveKey = false;
uint32_t pairRespMs = 0;
uint32_t rxCount = 0;

void IRAM_ATTR onRx() { rxFlag = true; }

/* ---------- Helpers ---------- */
uint16_t getU16(const uint8_t *b) { return ((uint16_t)b[0] << 8) | b[1]; }
uint32_t getU32(const uint8_t *b) {
  return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}
void hexDump(const uint8_t *b, int n) {
  for (int i = 0; i < n; i++) { if (b[i] < 0x10) Serial.print('0'); Serial.print(b[i], HEX); Serial.print(' '); }
  Serial.println();
}
const char *modeStr(uint8_t m) {
  switch (m) {
    case 0: return "Historique Mono";
    case 1: return "Standard Mono";
    case 2: return "Historique Tri";
    case 3: return "Standard Tri";
    case 5: return "Standard Mono Producteur";
    case 7: return "Standard Tri Producteur";
    default: return "Inconnu";
  }
}
bool isTri(uint8_t m) { return m == 2 || m == 3 || m == 7; }
void printStats(float rssi, float snr) {
  Serial.printf("  [RSSI=%.1f dBm  SNR=%.1f dB  #%lu]\n", rssi, snr, rxCount);
}

void setChannel(uint8_t ch) {
  radio.standby();
  radio.setFrequency(CH_FREQ[ch]);
  curChannel = ch;
}

/* ---------- Crypto (identique au .ino) ---------- */
void buildNonce(uint8_t *nonce, const uint8_t *mac, uint8_t seq) {
  memcpy(nonce, mac, 8);
  memset(&nonce[8], 0, 7);
  nonce[15] = seq;
}

// Dechiffre en place + verifie MIC. Entree: [type][seq][chiffre...][MIC0][MIC1]. Retourne true si MIC OK.
bool decryptPacket(uint8_t *pkt, int totalLen, const uint8_t *key, const uint8_t *mac) {
  if (totalLen < 2 + MIC_SIZE) return false;
  int dataLen = totalLen - MIC_SIZE;
  uint8_t recvMIC0 = pkt[dataLen];
  uint8_t recvMIC1 = pkt[dataLen + 1];

  uint8_t nonce[16];
  buildNonce(nonce, mac, pkt[1]);
  int payloadLen = dataLen - 2;
  if (payloadLen > 0) aes128_ctr_crypt(&pkt[2], (uint16_t)payloadLen, key, nonce);

  uint8_t expectedMIC[16];
  aes128_cmac(pkt, (uint16_t)dataLen, key, expectedMIC);

  if (expectedMIC[0] == recvMIC0 && expectedMIC[1] == recvMIC1) return true;
  Serial.printf("[CRYPT] MIC KO: recu %02X%02X attendu %02X%02X\n",
                recvMIC0, recvMIC1, expectedMIC[0], expectedMIC[1]);
  return false;
}

void generateRandomKey(uint8_t *key) {
  for (int i = 0; i < AES_KEY_SIZE; i++) key[i] = (uint8_t)(esp_random() & 0xFF);
}

/* ---------- Appairage ---------- */
bool sendPairResponse(uint8_t seq) {
  uint8_t pkt[PAIR_RESPONSE_SIZE];
  pkt[0] = 0x14;                          // v1 | PAIR_RESPONSE
  pkt[1] = seq;
  memcpy(&pkt[2], netKey, AES_KEY_SIZE);
  pkt[18] = 0x00;                         // status OK
  pkt[19] = OP_CHANNEL;                   // canal op impose

  Serial.printf("[PAIR] TX PAIR_RESPONSE seq=%u op_channel=%u key=", seq, OP_CHANNEL);
  hexDump(netKey, AES_KEY_SIZE);

  radio.clearDio1Action();                // eviter que l'ISR se declenche sur TxDone
  int st = radio.transmit(pkt, PAIR_RESPONSE_SIZE);   // sur PAIR_CHANNEL
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("[PAIR] TX PAIR_RESPONSE echec=%d\n", st);
    radio.setDio1Action(onRx);
    setChannel(PAIR_CHANNEL);
    radio.startReceive();
    return false;
  }
  Serial.println(F("[PAIR] PAIR_RESPONSE envoye OK"));

  setChannel(OP_CHANNEL);                 // le PAIR_CONFIRM arrive sur l'op_channel
  radio.setDio1Action(onRx);
  radio.startReceive();
  Serial.printf("[PAIR] bascule canal %u (%.0f MHz), attente PAIR_CONFIRM...\n", OP_CHANNEL, CH_FREQ[OP_CHANNEL]);
  return true;
}

void handlePairRequest(uint8_t *buf, int len) {
  if (rxState != ST_LISTEN) return;
  if (len != PAIR_REQUEST_SIZE) { Serial.printf("[PAIR] REQUEST taille %d\n", len); radio.startReceive(); return; }
  memcpy(emitterMAC, &buf[2], MAC_SIZE);
  Serial.printf("[PAIR] PAIR_REQUEST MAC=");
  hexDump(emitterMAC, MAC_SIZE);
  generateRandomKey(netKey);
  if (sendPairResponse(buf[1])) {
    rxState = ST_WAIT_CONFIRM;
    pairRespMs = millis();
  }
}

void handlePairConfirm(uint8_t *buf, int len) {
  if (len != PAIR_CONFIRM_SIZE) { Serial.printf("[PAIR] CONFIRM taille %d\n", len); return; }
  uint8_t rxMAC[MAC_SIZE];   memcpy(rxMAC, &buf[2], MAC_SIZE);
  uint8_t rxProof[4];        memcpy(rxProof, &buf[10], 4);

  if (memcmp(rxMAC, emitterMAC, MAC_SIZE) != 0) { Serial.println(F("[PAIR] CONFIRM MAC != REQUEST -> rejete")); return; }

  // Preuve = AES-ECB( MAC(8)||0x00*8 , netKey )[0..3]
  uint8_t ecbIn[AES_BLOCK_SIZE], ecbOut[AES_BLOCK_SIZE];
  memset(ecbIn, 0, AES_BLOCK_SIZE);
  memcpy(ecbIn, emitterMAC, MAC_SIZE);
  aes128_ecb_encrypt(ecbIn, netKey, ecbOut);
  if (memcmp(rxProof, ecbOut, 4) != 0) {
    Serial.printf("[PAIR] Preuve KO : recu %02X%02X%02X%02X attendu %02X%02X%02X%02X\n",
                  rxProof[0], rxProof[1], rxProof[2], rxProof[3], ecbOut[0], ecbOut[1], ecbOut[2], ecbOut[3]);
    return;
  }
  haveKey = true;
  rxState = ST_PAIRED;
  Serial.print(F("\n*** APPAIRAGE REUSSI *** MAC="));
  hexDump(emitterMAC, MAC_SIZE);
  Serial.print(F("    cle="));
  hexDump(netKey, AES_KEY_SIZE);
  Serial.printf("    -> ecoute des donnees sur canal %u (%.0f MHz)\n\n", OP_CHANNEL, CH_FREQ[OP_CHANNEL]);
}

/* ---------- Parsers TIC (dataLen = clair sans MIC) ---------- */
void parseEssential(uint8_t *b, int len, float rssi, float snr) {
  if (len != SZ_ESSENTIAL) { Serial.printf("[ESS] taille %d\n", len); return; }
  uint8_t mode = b[16];
  Serial.println(F("========== LINKY ESSENTIAL =========="));
  Serial.printf("  seq=%u  Mode: %s\n", b[1], modeStr(mode));
  Serial.printf("  SINSTS : %u VA (total)\n", getU16(&b[2]));
  if (isTri(mode))
    Serial.printf("  SINSTS1: %u  SINSTS2: %u  SINSTS3: %u VA\n", getU16(&b[4]), getU16(&b[6]), getU16(&b[8]));
  else
    Serial.printf("  SINSTS1: %u VA\n", getU16(&b[4]));
  Serial.printf("  STGE   : %02X%02X%02X%02X\n", b[10], b[11], b[12], b[13]);
  uint16_t adps = getU16(&b[14]);
  if (adps > 0) Serial.printf("  ADPS   : %u VA ***ALERTE***\n", adps);
  printStats(rssi, snr);
  Serial.println(F("=====================================\n"));
}

void parseExtCurrentVoltage(uint8_t *b, int len, float rssi, float snr) {
  if (len != SZ_EXT_IV) { Serial.printf("[EXT IV] taille %d\n", len); return; }
  Serial.println(F("---------- EXT: CURRENT_VOLTAGE ----------"));
  Serial.printf("  seq=%u\n", b[1]);
  Serial.printf("  IRMS1: %u  IRMS2: %u  IRMS3: %u mA\n", getU16(&b[3]), getU16(&b[5]), getU16(&b[7]));
  Serial.printf("  URMS1: %u  URMS2: %u  URMS3: %u V\n",  getU16(&b[9]), getU16(&b[11]), getU16(&b[13]));
  printStats(rssi, snr);
  Serial.println(F("------------------------------------------\n"));
}

static void printWh(const char *name, uint32_t v) {
  Serial.printf("  %-7s: %lu Wh (%.3f kWh)\n", name, v, v / 1000.0);
}
void parseExtEnergy(uint8_t *b, int len, float rssi, float snr) {
  if (len != SZ_EXT_NRG) { Serial.printf("[EXT NRG] taille %d\n", len); return; }
  Serial.println(F("---------- EXT: ENERGY ----------"));
  Serial.printf("  seq=%u\n", b[1]);
  printWh("EAST",   getU32(&b[3]));
  printWh("EAIT",   getU32(&b[7]));   // injection
  printWh("EASF01", getU32(&b[11]));
  printWh("EASF02", getU32(&b[15]));
  printStats(rssi, snr);
  Serial.println(F("---------------------------------\n"));
}
void parseExtEnergy2(uint8_t *b, int len, float rssi, float snr) {
  if (len != SZ_EXT_NRG2) { Serial.printf("[EXT NRG2] taille %d\n", len); return; }
  Serial.println(F("---------- EXT: ENERGY_2 ----------"));
  Serial.printf("  seq=%u\n", b[1]);
  printWh("EASF03", getU32(&b[3]));
  printWh("EASF04", getU32(&b[7]));
  printWh("EASF05", getU32(&b[11]));
  printWh("EASF06", getU32(&b[15]));
  printStats(rssi, snr);
  Serial.println(F("------------------------------------\n"));
}
void parseExtVoltageStats(uint8_t *b, int len, float rssi, float snr) {
  if (len != SZ_EXT_VSTAT) { Serial.printf("[EXT VSTAT] taille %d\n", len); return; }
  Serial.println(F("---------- EXT: VOLTAGE_STATS ----------"));
  Serial.printf("  seq=%u\n", b[1]);
  Serial.printf("  UMOY1: %u  UMOY2: %u  UMOY3: %u V\n",  getU16(&b[3]), getU16(&b[5]), getU16(&b[7]));
  Serial.printf("  IMAX1: %u  IMAX2: %u  IMAX3: %u mA\n", getU16(&b[9]), getU16(&b[11]), getU16(&b[13]));
  printStats(rssi, snr);
  Serial.println(F("----------------------------------------\n"));
}
void parseExtPowerMaxCfg(uint8_t *b, int len, float rssi, float snr) {
  if (len != SZ_EXT_PMCFG) { Serial.printf("[EXT PMCFG] taille %d\n", len); return; }
  Serial.println(F("---------- EXT: POWER_MAX_CFG ----------"));
  Serial.printf("  seq=%u\n", b[1]);
  Serial.printf("  SMAXSN: %u  SMAXIN: %u  SINSTI: %u  CCAIN: %u VA\n",
                getU16(&b[3]), getU16(&b[5]), getU16(&b[7]), getU16(&b[9]));
  Serial.printf("  RELAIS: 0x%04X  NTARF: %u  PCOUP: %u kVA  PREF: %u kVA  mode: %s\n",
                getU16(&b[11]), b[13], b[14], b[15], modeStr(b[16]));
  printStats(rssi, snr);
  Serial.println(F("----------------------------------------\n"));
}
void parseExtDailyEnergy(uint8_t *b, int len, float rssi, float snr) {
  if (len != SZ_EXT_DAILY) { Serial.printf("[EXT DAILY] taille %d\n", len); return; }
  Serial.println(F("---------- EXT: DAILY_ENERGY ----------"));
  Serial.printf("  seq=%u\n", b[1]);
  printWh("EASD01(J)",   getU32(&b[3]));
  printWh("EASD02(J-1)", getU32(&b[7]));
  printWh("EASD03(J-2)", getU32(&b[11]));
  printWh("EASD04(J-3)", getU32(&b[15]));
  printStats(rssi, snr);
  Serial.println(F("---------------------------------------\n"));
}
void parseExtComplement(uint8_t *b, int len, float rssi, float snr) {
  if (len != SZ_EXT_COMP) { Serial.printf("[EXT COMP] taille %d\n", len); return; }
  Serial.println(F("---------- EXT: COMPLEMENT ----------"));
  Serial.printf("  seq=%u  DPM1=%u FPM1=%u DPM2=%u FPM2=%u DPM3=%u FPM3=%u\n",
                b[1], b[3], b[4], b[5], b[6], b[7], b[8]);
  printStats(rssi, snr);
  Serial.println(F("-------------------------------------\n"));
}

/* ---------- Reception des donnees chiffrees ---------- */
void handleData(uint8_t *buf, int len, float rssi, float snr) {
  uint8_t type = buf[0] & 0x0F;
  if (!haveKey) {
    Serial.printf("[DATA] trame chiffree type=0x%02X len=%d recue mais pas encore appaire\n", type, len);
    return;
  }
  if (!decryptPacket(buf, len, netKey, emitterMAC)) return;   // MIC KO deja logue
  rxCount++;
  int dataLen = len - MIC_SIZE;                               // clair sans MIC

  if (type == T_ESSENTIAL) { parseEssential(buf, dataLen, rssi, snr); return; }

  // EXTENDED : sous-type = buf[2]
  uint8_t sub = buf[2];
  switch (sub) {
    case 0x00: parseExtCurrentVoltage(buf, dataLen, rssi, snr); break;
    case 0x01: parseExtEnergy(buf, dataLen, rssi, snr);         break;
    case 0x02: parseExtEnergy2(buf, dataLen, rssi, snr);        break;
    case 0x03: parseExtVoltageStats(buf, dataLen, rssi, snr);   break;
    case 0x04: parseExtPowerMaxCfg(buf, dataLen, rssi, snr);    break;
    case 0x05: parseExtDailyEnergy(buf, dataLen, rssi, snr);    break;
    case 0x06: parseExtComplement(buf, dataLen, rssi, snr);     break;
    default:
      Serial.printf("[EXT] sous-type 0x%02X inconnu (len=%d) : ", sub, dataLen);
      hexDump(buf, dataLen);
      break;
  }
}

/* ---------- setup / loop ---------- */
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println(F("\n========================================"));
  Serial.println(F("  Recepteur ZLinky LoRa 2.4GHz (SX1281)"));
  Serial.println(F("========================================"));

  spi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_NSS);
  int st = radio.begin(CH_FREQ[PAIR_CHANNEL], 406.25, 11, 5, 0x12, 10, 16);   // meme config que l'emetteur
  if (st != RADIOLIB_ERR_NONE) { Serial.printf("[X] radio.begin()=%d\n", st); while (1) delay(1000); }
  radio.setCRC(2);
  radio.explicitHeader();
  radio.invertIQ(false);
  radio.setDio1Action(onRx);
  radio.startReceive();
  curChannel = PAIR_CHANNEL;
  rxState = ST_LISTEN;
  Serial.printf("[OK] Ecoute PAIR_REQUEST sur canal %u (%.0f MHz).\n", PAIR_CHANNEL, CH_FREQ[PAIR_CHANNEL]);
  Serial.println(F("(Mets ton ZLinky en mode appairage.)"));
}

void loop() {
  // Timeout d'attente du PAIR_CONFIRM : retour a l'ecoute d'appairage sur canal 3.
  if (rxState == ST_WAIT_CONFIRM && millis() - pairRespMs > 4000) {
    Serial.println(F("[PAIR] pas de CONFIRM -> retour ecoute canal 3"));
    rxState = ST_LISTEN;
    setChannel(PAIR_CHANNEL);
    radio.startReceive();
  }

  if (!rxFlag) return;
  rxFlag = false;

  uint8_t buf[64];
  int len = radio.getPacketLength();
  int st = radio.readData(buf, len);
  if (st != RADIOLIB_ERR_NONE) {
    if (st == RADIOLIB_ERR_CRC_MISMATCH) Serial.println(F("[RX] CRC KO"));
    else Serial.printf("[RX] readData err=%d\n", st);
    radio.startReceive();
    return;
  }
  float rssi = radio.getRSSI();
  float snr  = radio.getSNR();
  if (len < 1) { radio.startReceive(); return; }

  uint8_t type = buf[0] & 0x0F;
  switch (type) {
    case T_PAIR_REQUEST:
      handlePairRequest(buf, len);        // gere son propre re-arm (bascule canal)
      break;
    case T_PAIR_CONFIRM:
      handlePairConfirm(buf, len);
      radio.startReceive();
      break;
    case T_ESSENTIAL:
    case T_EXTENDED:
      handleData(buf, len, rssi, snr);
      radio.startReceive();
      break;
    default:
      Serial.printf("[RX] trame type=0x%02X len=%d ignoree\n", type, len);
      radio.startReceive();
      break;
  }
}
