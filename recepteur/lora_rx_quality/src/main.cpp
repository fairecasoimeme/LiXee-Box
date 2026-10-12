/*
 * Test de qualite de reception LoRa 2.4 GHz (SX1281) -- LiXee-Box
 *
 * Ecoute PASSIVE des emetteurs deja appaires (ZLinky LoRa), sur le canal et le SF
 * operationnels de la box. Rien n'est emis : le protocole et le firmware de l'emetteur
 * restent inchanges.
 *
 * Pour chaque trame : RSSI, SNR, numero de sequence, trames perdues (trous de sequence),
 * validite du MIC. Toutes les 30 s : bilan par emetteur (taux de reception, RSSI et SNR
 * moyens / min / max, intervalle entre trames, marge de SNR sur la limite de demodulation).
 *
 * LED (GPIO 3) : ALLUMEE tant que la reception est bonne, ETEINTE des qu'aucune trame valide
 * n'arrive pendant le delai de coupure. Trame valide = CRC bon ET MIC d'un emetteur appaire
 * (ou CRC bon seulement si aucun emetteur n'est connu). Delai automatique : 2 x l'intervalle
 * moyen mesure entre trames, borne entre 5 s et 2 min (30 s tant qu'il n'est pas connu).
 * Trame en erreur CRC : la LED CLIGNOTE (3 eclairs de 100 ms) puis reprend son etat. Le
 * clignotement inverse l'etat courant : visible LED allumee comme eteinte.
 *
 * Configuration lue dans /config/lora.json (ecrit par le firmware principal a l'appairage) :
 *   opChannel, sf, emitters[] {mac, key}. LittleFS est monte SANS formatage en cas d'echec :
 *   ce firmware ne peut jamais effacer les donnees de la box.
 *
 * Parametres radio identiques au firmware principal (src/loraReceiver.cpp) :
 *   BW 406.25 kHz, CR 4/5, syncword 0x12, preambule 8, CRC 2 octets, en-tete explicite.
 *
 * Commandes serie (terminer par Entree) :
 *   h        aide
 *   c<0-7>   changer de canal          (ex. c4)
 *   s<7-12>  changer de SF             (ex. s11)
 *   r        remettre les statistiques a zero
 *   v        afficher / masquer le detail trame par trame
 *   b        afficher le bilan tout de suite
 *   t<s>     delai de coupure de la LED en secondes (ex. t45), t0 = automatique
 */
#include <Arduino.h>
#include <SPI.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <RadioLib.h>
#include "aes128.h"

/* ===================== Materiel (identique a src/loraModule.h) ===================== */
#define PIN_DIO1   2
#define PIN_SCK    4
#define PIN_MOSI   5
#define PIN_MISO   6
#define PIN_NSS    7
#define PIN_BUSY   8
#define PIN_RESET  16
#define PIN_LED    3      // LED de la box (LED_PIN du firmware principal), allumee a l'etat haut

/* ===================== Protocole (identique a src/loraReceiver.cpp) ===================== */
static const float CH_FREQ[8] = {2410.0, 2420.0, 2430.0, 2440.0, 2450.0, 2460.0, 2470.0, 2480.0};
#define T_ESSENTIAL      0x01
#define T_EXTENDED       0x02
#define T_PAIR_REQUEST   0x03
#define T_PAIR_CONFIRM   0x05
#define T_POLL_RESPONSE  0x0C
#define MIC_SIZE         2
#define MAC_SIZE         8
#define KEY_SIZE         16
#define MAX_EMITTERS     4
#define DEFAULT_CHANNEL  4
#define DEFAULT_SF       11
#define SUMMARY_MS       30000UL

// Radio : le bus SPI et ses reglages sont definis AVANT le module (ordre d'initialisation des
// globales dans un meme fichier = ordre de declaration).
SPIClass loraSpi(FSPI);
static SPISettings spiSet(2000000, MSBFIRST, SPI_MODE0);
static SX1280 radio = new Module(PIN_NSS, PIN_DIO1, PIN_RESET, PIN_BUSY, loraSpi, spiSet);

static volatile bool rxFlag = false;
static void IRAM_ATTR onRx() { rxFlag = true; }

/* ===================== Etat ===================== */
struct Emitter {
  bool     valid = false;
  uint8_t  mac[MAC_SIZE];
  uint8_t  key[KEY_SIZE];
  // statistiques
  uint32_t rx = 0, lost = 0;
  bool     seqInit = false;
  uint8_t  lastSeq = 0;
  float    rssiSum = 0, rssiMin = 0, rssiMax = 0;
  float    snrSum = 0, snrMin = 0, snrMax = 0;
  uint32_t lastMs = 0, intervalSum = 0, intervalCount = 0;
};
static Emitter emitters[MAX_EMITTERS];
static int     emitterCount = 0;

static uint8_t  curChannel = DEFAULT_CHANNEL;
static uint8_t  curSF      = DEFAULT_SF;
static bool     verbose    = true;
static uint32_t statsStartMs = 0, lastSummaryMs = 0;
static uint32_t crcErrors = 0, otherErrors = 0, unassigned = 0, otherTypes = 0, irqCount = 0;

// LED de reception
static bool     ledOn          = false;
static uint32_t lastGoodMs     = 0;     // derniere trame valide
static uint32_t ledTimeoutUser = 0;     // delai impose par la commande t (ms), 0 = automatique
#define LED_TIMEOUT_DEFAULT_MS  30000UL
#define LED_TIMEOUT_MIN_MS       5000UL
#define LED_TIMEOUT_MAX_MS     120000UL
// Clignotement sur trame en erreur CRC (non bloquant, pilote par ledTick()).
#define CRC_BLINK_FLASHES       3
#define CRC_BLINK_PHASE_MS      100UL
static bool     crcBlinking    = false;
static uint32_t crcBlinkStart  = 0;

/* ===================== Outils ===================== */
static String macToHex(const uint8_t *mac) {
  char b[17];
  for (int i = 0; i < MAC_SIZE; i++) snprintf(&b[i * 2], 3, "%02X", mac[i]);
  return String(b);
}

static void hexToBytes(const char *hex, uint8_t *out, int n) {
  for (int i = 0; i < n; i++) {
    char tmp[3] = {hex[i * 2], hex[i * 2 + 1], 0};
    out[i] = (uint8_t)strtol(tmp, nullptr, 16);
  }
}

// Nonce CTR : MAC(8) + 0x00(7) + seq(1), comme l'emetteur.
static void buildNonce(uint8_t *nonce, const uint8_t *mac, uint8_t seq) {
  memcpy(nonce, mac, 8);
  memset(&nonce[8], 0, 7);
  nonce[15] = seq;
}

// Verifie le MIC d'une trame [type][seq][chiffre...][MIC0][MIC1] pour une cle donnee.
// Travaille sur une COPIE : la trame d'origine reste intacte pour les cles suivantes.
static bool micMatches(const uint8_t *pkt, int len, const Emitter &e) {
  if (len < 2 + MIC_SIZE || len > 64) return false;
  uint8_t b[64];
  memcpy(b, pkt, len);
  int dataLen = len - MIC_SIZE;
  uint8_t nonce[16];
  buildNonce(nonce, e.mac, b[1]);
  if (dataLen - 2 > 0) aes128_ctr_crypt(&b[2], (uint16_t)(dataLen - 2), e.key, nonce);
  uint8_t expected[16];
  aes128_cmac(b, (uint16_t)dataLen, e.key, expected);
  return expected[0] == b[dataLen] && expected[1] == b[dataLen + 1];
}

// Limite de demodulation du SX1280 (SNR minimal, fiche technique) par SF.
static float snrLimit(uint8_t sf) {
  switch (sf) {
    case 5:  return -2.5f;  case 6:  return -5.0f;  case 7:  return -7.5f;
    case 8:  return -10.0f; case 9:  return -12.5f; case 10: return -15.0f;
    case 11: return -17.5f; default: return -20.0f;
  }
}

static const char *typeName(uint8_t t) {
  switch (t) {
    case T_ESSENTIAL:     return "ESSENTIEL";
    case T_EXTENDED:      return "ETENDU";
    case T_PAIR_REQUEST:  return "PAIR_REQ";
    case T_PAIR_CONFIRM:  return "PAIR_CONF";
    case T_POLL_RESPONSE: return "POLL_RESP";
    default:              return "?";
  }
}

static void resetStats() {
  for (int i = 0; i < emitterCount; i++) {
    Emitter &e = emitters[i];
    e.rx = e.lost = 0; e.seqInit = false;
    e.rssiSum = e.snrSum = 0; e.lastMs = 0; e.intervalSum = e.intervalCount = 0;
  }
  crcErrors = otherErrors = unassigned = otherTypes = irqCount = 0;
  statsStartMs = lastSummaryMs = millis();
  Serial.println(F("[Test] Statistiques remises a zero"));
}

/* ===================== Configuration de la box ===================== */
static void loadBoxConfig() {
  // false = NE JAMAIS formater si le montage echoue : les donnees de la box sont intactes.
  if (!LittleFS.begin(false)) {
    Serial.println(F("[Test] LittleFS non monte : canal/SF par defaut, pas d'identification MIC"));
    return;
  }
  File f = LittleFS.open("/config/lora.json", "r");
  if (!f) {
    Serial.println(F("[Test] /config/lora.json absent : canal/SF par defaut, pas d'identification MIC"));
    return;
  }
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf("[Test] /config/lora.json illisible (%s)\r\n", err.c_str());
    return;
  }
  int ch = doc["opChannel"] | DEFAULT_CHANNEL;
  int sf = doc["sf"] | DEFAULT_SF;
  curChannel = (ch < 0 || ch > 7) ? DEFAULT_CHANNEL : (uint8_t)ch;
  curSF      = (sf < 5 || sf > 12) ? DEFAULT_SF : (uint8_t)sf;

  for (JsonObject o : doc["emitters"].as<JsonArray>()) {
    if (emitterCount >= MAX_EMITTERS) break;
    const char *mac = o["mac"] | "";
    const char *key = o["key"] | "";
    if (strlen(mac) != 16 || strlen(key) != 32) continue;
    Emitter &e = emitters[emitterCount++];
    hexToBytes(mac, e.mac, MAC_SIZE);
    hexToBytes(key, e.key, KEY_SIZE);
    e.valid = true;
  }
  Serial.printf("[Test] Configuration de la box : canal %u (%.0f MHz), SF%u, %d emetteur(s)\r\n",
                curChannel, CH_FREQ[curChannel], curSF, emitterCount);
  for (int i = 0; i < emitterCount; i++)
    Serial.printf("[Test]   emetteur %d : %s\r\n", i + 1, macToHex(emitters[i].mac).c_str());
}

/* ===================== Radio ===================== */
static bool radioBegin() {
  pinMode(PIN_NSS, OUTPUT);
  digitalWrite(PIN_NSS, HIGH);
  loraSpi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_NSS);
  int st = radio.begin(CH_FREQ[curChannel], 406.25, curSF, 5, 0x12, 10, 8);   // preambule 8, comme le ZLinky
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("[Test] radio.begin() = %d : module LoRa absent ou non repondant\r\n", st);
    return false;
  }
  radio.setCRC(2);
  radio.explicitHeader();
  radio.invertIQ(false);
  radio.setDio1Action(onRx);
  radio.startReceive();
  return true;
}

static void retune() {
  radio.standby();
  radio.setFrequency(CH_FREQ[curChannel]);
  radio.setSpreadingFactor(curSF);
  rxFlag = false;
  radio.startReceive();
  Serial.printf("[Test] Ecoute : canal %u (%.0f MHz), SF%u\r\n", curChannel, CH_FREQ[curChannel], curSF);
}

/* ===================== LED de reception ===================== */
// Delai sans trame valide au-dela duquel la reception est jugee perdue. Automatique : 2 fois
// l'intervalle moyen le plus long parmi les emetteurs -- coupure des qu'aucune trame n'est
// arrivee sur 2 intervalles --, borne pour eviter un clignotement si l'intervalle est tres court.
static uint32_t ledTimeoutMs() {
  if (ledTimeoutUser) return ledTimeoutUser;
  uint32_t worst = 0;
  for (int i = 0; i < emitterCount; i++) {
    const Emitter &e = emitters[i];
    if (e.intervalCount) worst = max(worst, (uint32_t)(e.intervalSum / e.intervalCount));
  }
  if (worst == 0) return LED_TIMEOUT_DEFAULT_MS;
  uint32_t t = 2 * worst;
  if (t < LED_TIMEOUT_MIN_MS) t = LED_TIMEOUT_MIN_MS;
  if (t > LED_TIMEOUT_MAX_MS) t = LED_TIMEOUT_MAX_MS;
  return t;
}

// Sortie physique : etat de reception (ledOn), sauf pendant un clignotement CRC ou les phases
// paires INVERSENT cet etat -- l'eclair se voit donc LED allumee comme eteinte.
static void applyLed() {
  bool level = ledOn;
  if (crcBlinking) {
    uint32_t phase = (millis() - crcBlinkStart) / CRC_BLINK_PHASE_MS;
    if (phase >= 2 * CRC_BLINK_FLASHES) crcBlinking = false;
    else if (phase % 2 == 0)            level = !ledOn;
  }
  digitalWrite(PIN_LED, level ? HIGH : LOW);
}

static void setLed(bool on, const char *why) {
  if (on == ledOn) return;
  ledOn = on;
  applyLed();
  Serial.printf("[LED] %s : %s\r\n", on ? "allumee" : "eteinte", why);
}

static void goodFrame(uint32_t now) {
  lastGoodMs = now;
  setLed(true, "reception OK");
}

// Trame en erreur CRC : clignotement (relance si un clignotement est deja en cours).
static void crcBlink() {
  crcBlinking   = true;
  crcBlinkStart = millis();
  applyLed();
}

static void ledTick() {
  if (ledOn) {
    uint32_t t = ledTimeoutMs();
    if (millis() - lastGoodMs > t) {
      char why[64];
      snprintf(why, sizeof(why), "aucune trame valide depuis %lu s", (unsigned long)(t / 1000));
      setLed(false, why);
    }
  }
  applyLed();
}

/* ===================== Bilan ===================== */
static void printSummary() {
  uint32_t now = millis();
  Serial.printf("\r\n===== Bilan sur %lu s -- canal %u (%.0f MHz), SF%u =====\r\n",
                (unsigned long)((now - statsStartMs) / 1000), curChannel, CH_FREQ[curChannel], curSF);
  if (emitterCount == 0) Serial.println(F(" (aucun emetteur connu : trames comptees sans identification)"));

  for (int i = 0; i < emitterCount; i++) {
    Emitter &e = emitters[i];
    Serial.printf(" %s : ", macToHex(e.mac).c_str());
    if (e.rx == 0) { Serial.println(F("aucune trame recue")); continue; }
    uint32_t expected = e.rx + e.lost;
    float pdr = expected ? 100.0f * e.rx / expected : 0;
    Serial.printf("%lu recues, %lu perdues -> %.1f %%\r\n",
                  (unsigned long)e.rx, (unsigned long)e.lost, pdr);
    Serial.printf("   RSSI moy %.1f dBm (min %.1f / max %.1f)\r\n", e.rssiSum / e.rx, e.rssiMin, e.rssiMax);
    Serial.printf("   SNR  moy %.1f dB  (min %.1f / max %.1f)\r\n", e.snrSum / e.rx, e.snrMin, e.snrMax);
    // Marge = SNR le plus faible observe - limite de demodulation du SF courant.
    float margin = e.snrMin - snrLimit(curSF);
    const char *verdict = margin >= 10 ? "excellente" : margin >= 5 ? "bonne"
                        : margin >= 0 ? "limite" : "insuffisante";
    Serial.printf("   Marge SNR (pire trame) : %.1f dB -> liaison %s (estimation, limite SF%u = %.1f dB)\r\n",
                  margin, verdict, curSF, snrLimit(curSF));
    if (e.intervalCount)
      Serial.printf("   Intervalle moyen entre trames : %.1f s\r\n",
                    e.intervalSum / 1000.0f / e.intervalCount);
    Serial.printf("   Derniere trame il y a %lu s\r\n", (unsigned long)((now - e.lastMs) / 1000));
  }
  Serial.printf(" Trames CRC KO : %lu | autres erreurs radio : %lu | non attribuees (MIC KO) : %lu"
                " | autres types : %lu | interruptions : %lu\r\n",
                (unsigned long)crcErrors, (unsigned long)otherErrors, (unsigned long)unassigned,
                (unsigned long)otherTypes, (unsigned long)irqCount);
  Serial.printf(" LED : %s (coupure apres %lu s sans trame valide, %s)\r\n\r\n",
                ledOn ? "allumee" : "eteinte", (unsigned long)(ledTimeoutMs() / 1000),
                ledTimeoutUser ? "delai impose" : "delai automatique");
}

/* ===================== Reception ===================== */
static void handlePacket() {
  irqCount++;
  uint8_t buf[64];
  int len = radio.getPacketLength();
  if (len <= 0 || len > (int)sizeof(buf)) { otherErrors++; radio.startReceive(); return; }
  int st = radio.readData(buf, len);
  float rssi = radio.getRSSI(), snr = radio.getSNR();
  uint32_t now = millis();

  if (st == RADIOLIB_ERR_CRC_MISMATCH) {
    crcErrors++;
    crcBlink();
    if (verbose) Serial.printf("[RX] CRC KO    len=%d RSSI=%.1f dBm SNR=%.1f dB\r\n", len, rssi, snr);
    radio.startReceive();
    return;
  }
  if (st != RADIOLIB_ERR_NONE) {
    otherErrors++;
    if (verbose) Serial.printf("[RX] erreur %d  RSSI=%.1f dBm SNR=%.1f dB\r\n", st, rssi, snr);
    radio.startReceive();
    return;
  }

  uint8_t type = buf[0] & 0x0F;
  uint8_t seq  = (len > 1) ? buf[1] : 0;
  bool carriesMic = (type == T_ESSENTIAL || type == T_EXTENDED || type == T_POLL_RESPONSE);

  // Identification de l'emetteur par son MIC (trames de donnees uniquement).
  int who = -1;
  if (carriesMic) {
    for (int i = 0; i < emitterCount && who < 0; i++)
      if (micMatches(buf, len, emitters[i])) who = i;
  }

  if (who < 0) {
    if (carriesMic && emitterCount > 0) unassigned++; else otherTypes++;
    if (emitterCount == 0) goodFrame(now);   // pas de cle connue : CRC bon suffit
    if (verbose)
      Serial.printf("[RX] %-9s len=%d seq=%3u RSSI=%.1f dBm SNR=%.1f dB  (%s)\r\n", typeName(type), len,
                    seq, rssi, snr, carriesMic && emitterCount ? "MIC KO : autre reseau ou trame alteree"
                                                              : "non identifiee");
    radio.startReceive();
    return;
  }

  Emitter &e = emitters[who];
  uint32_t gapLost = 0;
  if (e.seqInit) {
    uint8_t diff = (uint8_t)(seq - (uint8_t)(e.lastSeq + 1));
    // Au-dela de 128, c'est un doublon ou un redemarrage de l'emetteur, pas des pertes.
    if (diff < 128) gapLost = diff;
  }
  e.lost += gapLost;
  e.lastSeq = seq; e.seqInit = true;
  if (e.rx == 0) {
    e.rssiMin = e.rssiMax = rssi;
    e.snrMin = e.snrMax = snr;
  } else {
    e.rssiMin = min(e.rssiMin, rssi); e.rssiMax = max(e.rssiMax, rssi);
    e.snrMin = min(e.snrMin, snr);    e.snrMax = max(e.snrMax, snr);
    e.intervalSum += now - e.lastMs; e.intervalCount++;
  }
  e.rx++; e.rssiSum += rssi; e.snrSum += snr; e.lastMs = now;
  goodFrame(now);

  if (verbose)
    Serial.printf("[RX] %-9s len=%d seq=%3u RSSI=%.1f dBm SNR=%.1f dB  %s%s\r\n", typeName(type), len, seq,
                  rssi, snr, macToHex(e.mac).c_str(),
                  gapLost ? (String("  <- ") + gapLost + " trame(s) perdue(s)").c_str() : "");
  radio.startReceive();
}

/* ===================== Commandes serie ===================== */
static void printHelp() {
  Serial.println(F("Commandes : h aide | c<0-7> canal | s<7-12> SF | r remise a zero | "
                   "v detail on/off | b bilan | t<s> delai LED (t0 = auto)"));
}

static void handleSerial() {
  static String line;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch != '\n' && ch != '\r') { if (line.length() < 16) line += ch; continue; }
    line.trim();
    if (line.length() == 0) continue;
    char cmd = line.charAt(0);
    int val = line.substring(1).toInt();
    if (cmd == 'h') printHelp();
    else if (cmd == 'r') resetStats();
    else if (cmd == 'b') printSummary();
    else if (cmd == 'v') { verbose = !verbose; Serial.printf("[Test] Detail trame par trame : %s\r\n", verbose ? "oui" : "non"); }
    else if (cmd == 'c' && val >= 0 && val <= 7 && line.length() > 1) { curChannel = val; retune(); resetStats(); }
    else if (cmd == 's' && val >= 5 && val <= 12) { curSF = val; retune(); resetStats(); }
    else if (cmd == 't' && line.length() > 1 && val >= 0 && val <= 3600) {
      ledTimeoutUser = (uint32_t)val * 1000UL;
      Serial.printf("[LED] delai de coupure : %s\r\n",
                    val ? (String(val) + " s").c_str() : "automatique");
    }
    else { Serial.printf("[Test] Commande inconnue : %s\r\n", line.c_str()); printHelp(); }
    line = "";
  }
}

/* ===================== Arduino ===================== */
void setup() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);           // eteinte tant qu'aucune trame valide n'est recue
  Serial.begin(115200);
  delay(1500);
  Serial.println(F("\r\n===== Test de qualite de reception LoRa 2.4 GHz -- LiXee-Box ====="));
  Serial.println(F("Ecoute passive : rien n'est emis, le ZLinky LoRa n'est pas modifie."));
  loadBoxConfig();
  if (!radioBegin()) {
    Serial.println(F("[Test] Arret : verifier le module LoRa."));
    while (true) delay(1000);
  }
  Serial.printf("[Test] Ecoute : canal %u (%.0f MHz), SF%u, BW 406.25 kHz, CR 4/5\r\n",
                curChannel, CH_FREQ[curChannel], curSF);
  printHelp();
  statsStartMs = lastSummaryMs = millis();
}

void loop() {
  if (rxFlag) { rxFlag = false; handlePacket(); }
  handleSerial();
  ledTick();
  if (millis() - lastSummaryMs >= SUMMARY_MS) {
    lastSummaryMs = millis();
    printSummary();
  }
}
