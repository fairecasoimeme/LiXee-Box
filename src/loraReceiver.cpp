#include "loraReceiver.h"
#include <esp_task_wdt.h>
#include "loraModule.h"
#include "config.h"
#include "device.h"
#include "rules.h"        // extern DeviceList devices
#include "zigbee.h"
#include "protocol.h"     // SetInfoLastseen() : date de dernier contact, comme cote Zigbee
#include "lixee.h"        // invalidateDeviceCache() : le cache findDevice doit suivre les ajouts
#include "SPIFFS_ini.h"
#include "aes128.h"
#include <RadioLib.h>
#include <SPI.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_system.h>

// File d'alertes de l'IHM (définie dans le .ino) : l'assistant d'appairage la sonde via
// /getAlert. On y publie l'appareil trouvé, comme le fait le Zigbee dans protocol.cpp.
extern LockedBuffer<Alert, 10> *alertList;
extern ConfigGeneralStruct ConfigGeneral;

// Le firmware compile avec CORE_DEBUG_LEVEL=0 : log_i/log_w/log_e sont muets.
// On passe donc par Serial pour que le diagnostic LoRa reste visible.
#define LLOG(...)  Serial.printf(__VA_ARGS__)

/* ===================== Radio ===================== */
// On réutilise le BUS SPI de loraModule (déjà ouvert par detectLoRa) : une seule instance
// SPIClass par contrôleur FSPI, sinon les deux se marchent dessus. La référence à loraSpi
// est sûre (seule son adresse compte, il n'est déréférencé qu'à l'exécution).
//
// En revanche les SPISettings sont copiées PAR VALEUR par le constructeur de Module, qui
// s'exécute AVANT setup(). Or l'ordre d'initialisation des globales entre .cpp est indéfini :
// utiliser loraSpiSet (défini dans loraModule.cpp) risquait de copier un objet non encore
// construit -> horloge SPI à 0 Hz -> radio.begin() = -2 (CHIP_NOT_FOUND). On définit donc
// les settings ICI, avant radio : dans un même fichier, l'ordre suit les déclarations.
static SPISettings loraRxSpiSet(2000000, MSBFIRST, SPI_MODE0);   // SX128x : mode 0
static SX1280 radio = new Module(LORA_PIN_NSS, LORA_PIN_DIO1, LORA_PIN_RESET, LORA_PIN_BUSY, loraSpi, loraRxSpiSet);

// Plan de canaux (identique à l'émetteur ZLinky)
static const float CH_FREQ[8] = {2410.0, 2420.0, 2430.0, 2440.0, 2450.0, 2460.0, 2470.0, 2480.0};

/* ===================== Types de trames ===================== */
#define T_ESSENTIAL     0x01
#define T_EXTENDED      0x02
#define T_PAIR_REQUEST  0x03
#define T_PAIR_CONFIRM  0x05
#define T_POLL_REQUEST  0x0B   // box -> device : lecture d'attribut (§8)
#define T_POLL_RESPONSE 0x0C   // device -> box : reponse a la lecture

// Statuts POLL_RESPONSE (§8)
#define POLL_OK           0x00
#define POLL_UNKNOWN_ATTR 0x01
#define POLL_BAD_REQUEST  0x02

#define MIC_SIZE        2
#define MAC_SIZE        8
#define KEY_SIZE        16

#define SZ_PAIR_REQUEST  11
#define SZ_PAIR_RESPONSE 21   // §4.2 : +1 octet op_SF en [20] (etait 20 avant SF a l'appairage)
#define SZ_PAIR_CONFIRM  15
#define SZ_ESSENTIAL     17
// Rendez-vous d'appairage : canal 3 + SF fixes (§4), quelle que soit la config operationnelle.
// SF11 : c'est le SF de rendez-vous du firmware actuel du ZLinky LoRa. Un passage en SF10 (temps
// d'emission divise par deux, plus de tentatives dans la fenetre de 30 s) a ete essaye, mais il
// rendait impossible l'appairage des ZLinky non mis a jour : il est remis a plus tard.
// ATTENTION : l'emetteur doit utiliser le MEME SF pour le rendez-vous, sinon il n'est pas entendu.
#define LORA_PAIR_SF     11

// Preambule (en symboles), en emission comme en reception. DOIT etre le meme que celui du
// ZLinky LoRa (8 depuis son passage de 16 a 8). Seule exception : une requete de lecture
// d'attribut l'allonge aux SF bas (cf. pollPreamble) -- un preambule plus long que celui attendu
// reste recu.
#define LORA_PREAMBLE    8

// Identité d'un ZLinky : device_id nomme le template data/tp/81.json, model en est la clé.
// Utilisée UNIQUEMENT en repli : pour un émetteur appairé avant l'identification par POLL
// (lora.json sans deviceId), et pour un émetteur dont le firmware ne connaît pas le cluster
// Basic (réponse UNKNOWN_ATTR) -- un firmware aussi ancien est forcément celui du ZLinky.
#define LORA_ZLINKY_DEVICE_ID "81"
#define LORA_ZLINKY_MODEL     "ZLinky_TIC"

// Identification par POLL (cluster Basic, LORA_PROTOCOL.md §8)
#define POLL_CLUSTER_BASIC  0x0000
#define POLL_ATTR_DEVICE_ID 0xFF00   // uint16
#define POLL_ATTR_MODEL     0x0005   // character string (ModelIdentifier, comme en Zigbee)
#define POLL_ATTR_FW_VERSION 0xFF01  // uint16, format 0xMMmp
#define POLL_ATTR_SW_BUILD   0x4000  // character string (SWBuildID), ex. "1.0.0+abc1234"
#define IDENT_RETRY_MS      300000UL // après 8 essais sans réponse : nouvel essai 5 min plus tard

/* ===================== État ===================== */
LoraEmitter loraEmitters[LORA_MAX_EMITTERS];
bool        loraPairingMode    = false;
uint32_t    loraPairingStartMs = 0;

static volatile bool rxFlag = false;
static bool     radioReady   = false;
static uint8_t  curChannel   = LORA_PAIR_CHANNEL;
static uint8_t  pendingMAC[MAC_SIZE];
static uint8_t  pendingKey[KEY_SIZE];
static char     pendingDeviceId[8];   // type annoncé dans le PAIR_REQUEST étendu
static char     pendingModel[24];
static bool     awaitingConfirm = false;
static uint32_t confirmDeadline = 0;
// Chien de garde de la réception : date de la dernière trame radio valide. Cf. rxWatchdog().
static uint32_t lastRadioRxMs = 0;

// Parametres radio OPERATIONNELS du recepteur (globaux, persistes dans /config/lora.json). Ce
// sont les valeurs sur lesquelles le recepteur ecoute les donnees ET qu'il ASSIGNE aux
// emetteurs a l'appairage (§4). Ils se changent via la page de config (loraSetOpParams), pas
// par une commande radio. Doivent survivre au reboot, sinon le recepteur ecouterait le
// canal/SF par defaut alors que les emetteurs sont ailleurs.
static uint8_t g_opChannel = LORA_OP_CHANNEL;   // 0..7  (defaut LORA_OP_CHANNEL)
static uint8_t g_sf        = 11;                 // 7..12 (defaut SF11)

// Resultat du dernier appairage reussi, canal DEDIE a l'assistant (pas l'alerte partagee).
// loraTakePairResult() le lit et l'efface -> livre une seule fois, a un seul lecteur.
static char loraPairedMac[17]   = "";
static char loraPairedModel[24] = "";
static bool loraPairedPending   = false;

bool loraTakePairResult(String &mac, String &model) {
  if (!loraPairedPending) return false;
  loraPairedPending = false;
  mac   = loraPairedMac;
  model = loraPairedModel;
  return true;
}

static void IRAM_ATTR onLoraRx() { rxFlag = true; }

/* LED d'appairage (LED_PIN = GPIO 3), la meme que celle du provisioning BLE.
 * Aucun conflit : l'appairage LoRa se pilote depuis la page web, donc le WiFi est connecte
 * et SmartWiFiManager laisse la LED eteinte (il ne la touche qu'en WIFI_STATE_BLE_PROVISIONING).
 * On la fait clignoter pendant la fenetre, et on la restaure eteinte a la fermeture.
 */
static uint32_t ledLastToggle = 0;
static bool     ledState      = false;

static void pairingLedTick() {
  if (millis() - ledLastToggle < 300) return;
  ledLastToggle = millis();
  ledState = !ledState;
  digitalWrite(LED_PIN, ledState ? HIGH : LOW);
}

static void pairingLedOff() {
  ledState = false;
  digitalWrite(LED_PIN, LOW);
}

/* ===================== Helpers ===================== */
static String macToHex(const uint8_t *mac) {
  char b[17];
  for (int i = 0; i < MAC_SIZE; i++) snprintf(&b[i * 2], 3, "%02X", mac[i]);
  return String(b);
}

// Retune du canal. standby() AVANT toute reconfiguration (impose par le SX128x), puis
// clearIrqStatus() : sinon une IRQ residuelle du canal precedent (ex : DIO1 encore assertee
// apres une reception sur le canal op) empeche un re-armement franc en RX au canal suivant.
// C'est ce qui rendait l'appairage capricieux (bascule op->rendez-vous et rendez-vous->op).
static void setChannel(uint8_t ch) {
  radio.finishReceive();    // standby + clearIrqStatus (clearIrqStatus est protected)
  radio.setFrequency(CH_FREQ[ch]);
  curChannel = ch;
}

// SF reellement charge dans la radio (peut differer de g_sf pendant un changement ou un scan).
static uint8_t radioSF = 11;

// Change le spreading factor de la radio. Meme precaution que setChannel (standby + clear IRQ).
static void setSF(uint8_t sf) {
  radio.finishReceive();    // standby + clearIrqStatus
  radio.setSpreadingFactor(sf);
  radioSF = sf;
}

// Bornage des parametres radio (defense contre un fichier corrompu ou une commande hors bornes).
static uint8_t o_clampChannel(int v) { return (v < 0 || v > 7) ? LORA_OP_CHANNEL : (uint8_t)v; }
static uint8_t o_clampSF(int v)      { return (v < 7 || v > 12) ? 11 : (uint8_t)v; }

// Construit le nonce CTR : MAC(8) + 0x00(7) + seq(1) — identique à l'émetteur.
static void buildNonce(uint8_t *nonce, const uint8_t *mac, uint8_t seq) {
  memcpy(nonce, mac, 8);
  memset(&nonce[8], 0, 7);
  nonce[15] = seq;
}

// Déchiffre en place et vérifie le MIC. Entrée [type][seq][chiffré...][MIC0][MIC1].
static bool decryptPacket(uint8_t *pkt, int totalLen, const uint8_t *key, const uint8_t *mac) {
  if (totalLen < 2 + MIC_SIZE) return false;
  int dataLen = totalLen - MIC_SIZE;
  uint8_t m0 = pkt[dataLen], m1 = pkt[dataLen + 1];

  uint8_t nonce[16];
  buildNonce(nonce, mac, pkt[1]);
  int payloadLen = dataLen - 2;
  if (payloadLen > 0) aes128_ctr_crypt(&pkt[2], (uint16_t)payloadLen, key, nonce);

  uint8_t expected[16];
  aes128_cmac(pkt, (uint16_t)dataLen, key, expected);
  return (expected[0] == m0 && expected[1] == m1);
}

// Chiffre en place et appose le MIC (miroir de decryptPacket, ordre d'emission §3).
// pkt[0..clearLen-1] = trame en clair (header + payload). Renvoie la longueur totale
// (clearLen + 2). Le buffer doit avoir 2 octets libres apres clearLen pour le MIC.
static int encryptPacket(uint8_t *pkt, int clearLen, const uint8_t *key, const uint8_t *mac) {
  uint8_t mic[16];
  aes128_cmac(pkt, (uint16_t)clearLen, key, mic);       // 1. MIC sur le clair complet
  uint8_t nonce[16];
  buildNonce(nonce, mac, pkt[1]);                       // nonce = MAC + seq (header en clair)
  int payloadLen = clearLen - 2;
  if (payloadLen > 0) aes128_ctr_crypt(&pkt[2], (uint16_t)payloadLen, key, nonce);  // 2. chiffre
  pkt[clearLen]     = mic[0];                            // 3. appende MIC (2 octets)
  pkt[clearLen + 1] = mic[1];
  return clearLen + 2;
}

static uint16_t getU16(const uint8_t *b) { return ((uint16_t)b[0] << 8) | b[1]; }
static uint32_t getU32(const uint8_t *b) {
  return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

/* ===================== Injection dans le pipeline Zigbee =====================
 * On synthétise les octets bruts (big-endian) puis on appelle readZigbeeDatas() :
 * le MÊME dispatcher que les trames Zigbee. Il fait setValue (hexa) + addMeasurement
 * (historique) + MQTT + WebPush + UDP. Le ZLinky LoRa devient donc un appareil normal.
 */
static void pushZ(const String &inifile, uint16_t cluster, uint16_t attr, uint32_t value, uint8_t nbytes) {
  uint8_t c[2] = {(uint8_t)(cluster >> 8), (uint8_t)(cluster & 0xFF)};
  uint8_t a[2] = {(uint8_t)(attr >> 8), (uint8_t)(attr & 0xFF)};
  char d[4];
  for (int i = 0; i < nbytes; i++) d[i] = (char)(uint8_t)(value >> (8 * (nbytes - 1 - i)));
  uint8_t dtype = (nbytes == 1) ? 0x20 : (nbytes == 2) ? 0x21 : 0x23;   // uint8/uint16/uint32
  readZigbeeDatas(inifile, c, a, dtype, nbytes, d);
}

// Chaîne de caractères Zigbee (type 0x42) : premier octet = longueur, puis l'ASCII.
// Plusieurs attributs FF66 sont des chaînes côté TIC (STGE en hex ASCII, PTEC...) et leurs
// handlers lisent datas[0] comme la longueur : y pousser du binaire donne une valeur vide.
static void pushZStr(const String &inifile, uint16_t cluster, uint16_t attr, const char *s) {
  uint8_t c[2] = {(uint8_t)(cluster >> 8), (uint8_t)(cluster & 0xFF)};
  uint8_t a[2] = {(uint8_t)(attr >> 8), (uint8_t)(attr & 0xFF)};
  char d[32];
  size_t n = strlen(s);
  if (n > sizeof(d) - 1) n = sizeof(d) - 1;
  d[0] = (char)n;
  memcpy(&d[1], s, n);
  readZigbeeDatas(inifile, c, a, 0x42, (int)n + 1, d);
}

// Chaîne SANS octet de longueur. Tous les handlers de chaîne ne se valent pas :
// lixee.cpp (FF66) lit datas[0] comme une longueur, alors que simpleMeter.cpp (0702/776,
// via createTextMeterData) consomme les octets bruts. Y envoyer une chaîne préfixée
// mettrait l'octet de longueur en premier caractère de la valeur.
static void pushZRawStr(const String &inifile, uint16_t cluster, uint16_t attr, const char *s) {
  uint8_t c[2] = {(uint8_t)(cluster >> 8), (uint8_t)(cluster & 0xFF)};
  uint8_t a[2] = {(uint8_t)(attr >> 8), (uint8_t)(attr & 0xFF)};
  char d[32];
  size_t n = strlen(s);
  if (n > sizeof(d)) n = sizeof(d);
  memcpy(d, s, n);
  readZigbeeDatas(inifile, c, a, 0x42, (int)n, d);
}

// Code tarif unifié (§7.5.1) -> code PTEC de la TIC Historique, format attendu par
// l'attribut FF66/16 (« Tarif en cours »), d'où publishLinkyTariffInfo() tire le libellé
// du tarif et la couleur Tempo du jour.
static const char *histoPtecCode(uint8_t tariffCode) {
  switch (tariffCode) {
    case 1:  return "TH..";   // Base
    case 2:  return "HP..";   // Heures Pleines
    case 3:  return "HC..";   // Heures Creuses
    case 4:  return "HN..";   // EJP heures normales
    case 5:  return "PM..";   // EJP pointe mobile
    case 6:  return "HCJB";   // Tempo Bleu HC
    case 7:  return "HPJB";   // Tempo Bleu HP
    case 8:  return "HCJW";   // Tempo Blanc HC
    case 9:  return "HPJW";   // Tempo Blanc HP
    case 10: return "HCJR";   // Tempo Rouge HC
    case 11: return "HPJR";   // Tempo Rouge HP
    default: return nullptr;  // 0 = inconnu (pas encore de POWER_MAX_CFG)
  }
}

/* ===================== Mode Linky =====================
 * Cf. LORA_PROTOCOL.md §6.1 et §10. Le mode conditionne l'existence ET l'unité de la
 * plupart des champs : un champ « absent » arrive à 0 dans la trame, et pousser ce 0
 * afficherait une fausse mesure. On ne pousse donc un attribut que s'il a un sens ici.
 */
static inline bool isStandard(const LoraEmitter &e) { return  (e.linkyMode & 0x01); }
static inline bool isHisto(const LoraEmitter &e)    { return !(e.linkyMode & 0x01); }
static inline bool isTri(const LoraEmitter &e)      { return  (e.linkyMode & 0x02); }

// Option tarifaire Historique, déduite du code tarif unifié (§7.2.1). Elle seule dit si les
// index Tier1..Tier6 ont un sens : en BASE l'index total suffit, seul Tempo utilise 3..6.
static inline bool histoBase(const LoraEmitter &e)  { return e.tariffKnown && e.tariffCode == 1; }
static inline bool histoTempo(const LoraEmitter &e) { return e.tariffKnown && e.tariffCode >= 6 && e.tariffCode <= 11; }

// Efface les index tarifaires Tier1/Tier2 s'ils portent encore une valeur alors que l'option
// souscrite est BASE (ou ils n'existent pas). Cas typique : valeur ecrite pendant les ~3 min qui
// precedent l'identification de l'option, qui resterait ensuite affichee en double de BASE/EAST.
// Ne fait rien si les attributs sont deja vides : pas d'ecriture inutile a chaque trame.
static void clearStaleTierValues(const String &inifile) {
  String id = inifile.substring(0, 16);
  for (size_t i = 0; i < devices.size(); i++) {
    if (devices[i]->getDeviceID() != id) continue;
    bool changed = false;
    for (const char *attr : {"256", "258"}) {
      if (devices[i]->getValue("0702", attr).length() > 0) {
        devices[i]->setValue("0702", attr, "");
        changed = true;
      }
    }
    if (changed) {
      devices[i]->saveToFile();   // sinon la valeur reviendrait au prochain redemarrage
      LLOG("[LoRa] option BASE : index tarifaires 256/258 effaces (doublon de BASE/EAST)\r\n");
    }
    return;
  }
}

/* ===================== Mapping des trames TIC ===================== */
static void mapEssential(const String &inifile, const uint8_t *b, int len, LoraEmitter &e) {
  if (len != SZ_ESSENTIAL) return;

  // Le dernier octet porte le mode : c'est la seule trame qui le donne toutes les 5 s.
  // On le mémorise pour les EXTENDED, et on l'expose comme le fait un ZLinky Zigbee.
  if (!e.modeKnown || e.linkyMode != b[16]) {
    e.linkyMode = b[16];
    e.modeKnown = true;
    LLOG("[LoRa] mode Linky = %u (%s, %s%s)\r\n", b[16],
         isStandard(e) ? "Standard" : "Historique",
         isTri(e) ? "triphase" : "monophase",
         (b[16] & 0x04) ? ", production" : "");
  }
  pushZ(inifile, 0xFF66, 768, b[16], 1);              // Mode

  if (isTri(e)) {
    pushZ(inifile, 0x0B04, 1295, getU16(&b[4]), 2);   // SINSTS1
    pushZ(inifile, 0x0B04, 2319, getU16(&b[6]), 2);   // SINSTS2
    pushZ(inifile, 0x0B04, 2575, getU16(&b[8]), 2);   // SINSTS3
  } else {
    pushZ(inifile, 0x0B04, 1295, getU16(&b[2]), 2);   // SINSTS / PAPP (total = mono)
  }
  if (isStandard(e)) {
    // STGE : la TIC le donne en 8 caractères hex et handleAttribute535() attend cette
    // chaîne (il en tire la couleur Tempo et le délestage via parseStatusRegister()).
    // L'émetteur le transmet en binaire (§6) : on refait le chemin inverse.
    char stge[9];
    snprintf(stge, sizeof(stge), "%08lX", (unsigned long)getU32(&b[10]));
    pushZStr(inifile, 0xFF66, 535, stge);
  } else {
    pushZ(inifile, 0xFF66, 5, getU16(&b[14]), 2);                     // ADPS : Historique seul
  }
}

static void mapExtended(const String &inifile, const uint8_t *b, int len, LoraEmitter &e) {
  // Sans le mode on ne sait pas interpréter ces trames : mieux vaut les ignorer que de
  // publier des valeurs fausses. Un ESSENTIAL arrive toutes les 5 s, l'attente est courte.
  // POWER_MAX_CFG (0x04) et TARIFF_LABEL (0x07) font exception : ils portent leur propre
  // mode (§10.1) et se suffisent donc à eux-mêmes.
  if (!e.modeKnown && b[2] != 0x04 && b[2] != 0x07) {
    LLOG("[LoRa] EXT ignore : mode Linky pas encore connu\r\n");
    return;
  }
  const bool std = isStandard(e), tri = isTri(e);

  switch (b[2]) {                                     // sous-type
    case 0x00:                                        // CURRENT_VOLTAGE (15B)
      if (len != 15) return;
      pushZ(inifile, 0x0B04, 1288, getU16(&b[3]),  2);  // IRMS1 / IINST
      if (tri) {
        pushZ(inifile, 0x0B04, 2312, getU16(&b[5]),  2);  // IRMS2
        pushZ(inifile, 0x0B04, 2568, getU16(&b[7]),  2);  // IRMS3
      }
      if (std) {                                      // URMS : Standard seul
        pushZ(inifile, 0x0B04, 1285, getU16(&b[9]),  2);  // URMS1
        if (tri) {
          pushZ(inifile, 0x0B04, 2309, getU16(&b[11]), 2);  // URMS2
          pushZ(inifile, 0x0B04, 2565, getU16(&b[13]), 2);  // URMS3
        }
      }
      break;
    case 0x01:                                        // ENERGY (19B)
      if (len != 19) return;
      pushZ(inifile, 0x0702, 0, getU32(&b[3]), 4);      // EAST / BASE
      if (std) pushZ(inifile, 0x0702, 1, getU32(&b[7]), 4);   // EAIT : Standard seul
      // Tier1/2 -> mêmes attributs qu'en Zigbee (le template les nomme déjà
      // « HC / EJPHN / BBRHCJB / EASF01 »). En Historique option BASE ils n'ont pas de sens :
      // le protocole y place l'index BASE dans Tier1 (§7.2.1), donc les publier afficherait
      // DEUX FOIS le meme index (une fois en « BASE / EAST », une fois en « HC / ... »).
      //
      // En Historique on attend donc de CONNAITRE l'option tarifaire avant d'ecrire quoi que ce
      // soit : tariffKnown ne devient vrai qu'a la reception de POWER_MAX_CFG ou TARIFF_LABEL,
      // soit jusqu'a ~3 min 40 apres le demarrage. Sans cette attente, histoBase() etait faux
      // pendant toute cette fenetre, l'attribut 256 recevait l'index BASE... et la valeur
      // restait ensuite affichee indefiniment, meme apres l'identification de l'option.
      if (std || (e.tariffKnown && !histoBase(e))) {
        pushZ(inifile, 0x0702, 256, getU32(&b[11]), 4);  // EASF01 / HCHC / EJPHN / BBRHCJB
        pushZ(inifile, 0x0702, 258, getU32(&b[15]), 4);  // EASF02 / HCHP / EJPHPM / BBRHPJB
      } else if (histoBase(e)) {
        // Reparation : efface une valeur ecrite avant que l'option soit connue (ou par une
        // version anterieure), sinon l'index resterait duplique sur une box deja en service.
        clearStaleTierValues(inifile);
      }
      break;
    case 0x02:                                        // ENERGY_2 (19B)
      if (len != 19) return;
      // Index 3..6 : Standard, ou Historique Tempo uniquement (§7.3).
      if (!std && !histoTempo(e)) return;
      pushZ(inifile, 0x0702, 260, getU32(&b[3]),  4);   // EASF03 / BBRHCJW
      pushZ(inifile, 0x0702, 262, getU32(&b[7]),  4);   // EASF04 / BBRHPJW
      pushZ(inifile, 0x0702, 264, getU32(&b[11]), 4);   // EASF05 / BBRHCJR
      pushZ(inifile, 0x0702, 266, getU32(&b[15]), 4);   // EASF06 / BBRHPJR
      break;
    case 0x03:                                        // VOLTAGE_STATS (15B)
      if (len != 15) return;
      if (std) {                                      // UMOY : Standard seul
        pushZ(inifile, 0x0B04, 1297, getU16(&b[3]), 2);   // UMOY1
        if (tri) {
          pushZ(inifile, 0x0B04, 2321, getU16(&b[5]), 2);   // UMOY2
          pushZ(inifile, 0x0B04, 2577, getU16(&b[7]), 2);   // UMOY3
        }
      } else {
        // [9-14] : IMAX (A) en Historique, SMAXSN1..3 (VA) en Standard — même octets, sens
        // et unité différents. L'attribut cible est IMAX (unité A) : n'y écrire que l'IMAX.
        pushZ(inifile, 0x0B04, 1290, getU16(&b[9]), 2);   // IMAX1 / IMAX
        if (tri) {
          pushZ(inifile, 0x0B04, 2314, getU16(&b[11]), 2);  // IMAX2
          pushZ(inifile, 0x0B04, 2570, getU16(&b[13]), 2);  // IMAX3
        }
      }
      break;
    case 0x04: {                                      // POWER_MAX_CFG (18B)
      if (len != 18) return;
      // Porte son propre mode et son propre tarif : on les rafraîchit, et on les lit ICI
      // plutôt que via `std` (qui reflète le mode mémorisé, antérieur à cette trame).
      e.linkyMode = b[16]; e.modeKnown = true;
      const bool cfgStd = (b[16] & 0x01);
      if (b[17] != 0 && e.tariffCode != b[17]) {
        e.tariffCode = b[17];
        e.tariffKnown = true;
        LLOG("[LoRa] code tarif = %u (%s)\r\n", b[17],
             cfgStd ? "NTARF Standard" : (histoPtecCode(b[17]) ? histoPtecCode(b[17]) : "?"));
      }
      pushZ(inifile, 0xFF66, 768, b[16], 1);            // Mode
      // Repli tant qu'aucun TARIFF_LABEL n'a donné le vrai libellé : en Historique le code
      // tarif se traduit en code PTEC. En Standard c'est impossible (NTARF seul ne dit pas
      // si l'index 1 est « Base », « HC » ou « HC Bleu ») -> on attend le libellé.
      if (!cfgStd && !e.labelFromEmitter) {
        const char *ptec = histoPtecCode(b[17]);
        if (ptec) {
          pushZStr(inifile, 0xFF66, 16, ptec);
          // 0702/32 « Tarif en cours Histo » : c'est CET attribut que lit la banniere de la
          // page Energie en mode Historique (handleAPITariff), et non FF66/16. Sans lui la
          // banniere restait vide. Chaine BRUTE : handleAttribute32 passe par
          // createTextMeterData, qui consomme les octets sans prefixe de longueur.
          pushZRawStr(inifile, 0x0702, 32, ptec);
        }
      }
      pushZ(inifile, 0x0B04, 1293, getU16(&b[3]), 2);   // SMAXSN / PMAX
      pushZ(inifile, 0x0B01, 13,   b[15], 1);           // PREF (kVA) / ISOUSC (A)
      if (cfgStd) {                                   // le reste est Standard seul (§7.5)
        pushZ(inifile, 0xFF66, 520, getU16(&b[5]), 2);  // SMAXIN
        pushZ(inifile, 0xFF66, 519, getU16(&b[7]), 2);  // SINSTI
        pushZ(inifile, 0xFF66, 522, getU16(&b[9]), 2);  // CCAIN
        pushZ(inifile, 0xFF66, 513, b[13], 1);          // NTARF
        pushZ(inifile, 0x0B01, 14,  b[14], 1);          // PCOUP
      }
      break;
    }
    case 0x05:                                        // DAILY_ENERGY (19B)
      if (len != 19) return;
      if (!std) return;                               // EASD01..04 : Standard seul (§7.6)
      pushZ(inifile, 0xFF66, 515, getU32(&b[3]),  4);   // EASD01
      pushZ(inifile, 0xFF66, 516, getU32(&b[7]),  4);   // EASD02
      pushZ(inifile, 0xFF66, 517, getU32(&b[11]), 4);   // EASD03
      pushZ(inifile, 0xFF66, 518, getU32(&b[15]), 4);   // EASD04
      break;
    case 0x06:                                        // COMPLEMENT (9B) : DPM1..3 / FPM1..3
      // Volontairement ignoré : aucun des templates ZLinky (81/97/257) ne déclare
      // d'attribut pour les début/fin de pointe mobile — le ZLinky *Zigbee* ne les remonte
      // pas non plus. Il n'existe donc aucun attribut cible où les écrire. On l'acte ici
      // pour ne pas polluer le log toutes les ~160 s avec un « non mappé » trompeur.
      break;
    case 0x07: {                                      // TARIFF_LABEL (6+N octets, §7.8)
      // Libellé texte du tarif en cours : LTARF en Standard, PTEC en Historique. C'est la
      // seule source possible en Standard (NTARF ne donne qu'un index, dont le sens dépend
      // du contrat). Trame auto-descriptive : elle porte son propre mode et code tarif.
      //   [3] mode, [4] code tarif, [5] longueur du libellé (N), [6..] libellé ASCII
      if (len < 6) return;
      uint8_t n = b[5];
      if (n == 0 || 6 + n > len) return;

      // Le mode vient de la trame, pas du contexte mémorisé : la variable `std` calculée en
      // tête de fonction peut être antérieure à un changement de mode annoncé ici.
      const bool lblStd = (b[3] & 0x01);
      e.linkyMode = b[3]; e.modeKnown = true;
      if (b[4] != 0) { e.tariffCode = b[4]; e.tariffKnown = true; }

      // La TIC pade LTARF à 16 caractères et l'émetteur ne retire que les espaces de FIN :
      // il reste ceux de tête (« ␣␣Heure creuse »), qui s'afficheraient tels quels sur la
      // fiche. On rogne les deux bouts. Les non-imprimables sont traités comme du padding.
      const uint8_t *p = &b[6];
      int nb = n;
      while (nb > 0 && (*p == ' ' || *p < 0x20)) { p++; nb--; }
      while (nb > 0 && (p[nb - 1] == ' ' || p[nb - 1] < 0x20)) nb--;
      if (nb <= 0) return;                            // libellé vide : rien à publier

      char label[24];
      if (nb > (int)sizeof(label) - 1) nb = sizeof(label) - 1;
      memcpy(label, p, nb);
      label[nb] = '\0';

      // FF66/16 « Tarif en cours (report) » : lu par publishLinkyTariffInfo() dans les deux
      // modes. FF66/512 « Tarif en cours Standard » : c'est là que le ZLinky Zigbee met
      // LTARF, donc on l'y remet aussi pour que la fiche soit identique.
      pushZStr(inifile, 0xFF66, 16, label);
      if (lblStd) pushZStr(inifile, 0xFF66, 512, label);
      // En Historique, la banniere de la page Energie lit 0702/32 (« Tarif en cours Histo »),
      // la ou le ZLinky Zigbee publie PTEC. Chaine BRUTE (cf. createTextMeterData).
      if (!lblStd) pushZRawStr(inifile, 0x0702, 32, label);

      if (!e.labelFromEmitter) {
        e.labelFromEmitter = true;
        LLOG("[LoRa] libelle tarif recu de l'emetteur : \"%s\" (%s)\r\n",
             label, lblStd ? "LTARF" : "PTEC");
      }
      break;
    }
    case 0x08: {                                      // METER_SERIAL (4+N octets, §7.9)
      // Numéro de série du compteur : ADSC en Standard, ADCO en Historique -> attribut
      // Zigbee « Serial Number » (0702/776). Statique, mais envoyé périodiquement comme les
      // autres sous-types plutôt qu'une seule fois à l'appairage : ainsi un compteur
      // remplacé se voit sans ré-appairer.
      //   [3] longueur (N), [4..] numéro ASCII
      if (len < 4) return;
      uint8_t n = b[3];
      if (n == 0 || 4 + n > len) return;

      const uint8_t *p = &b[4];
      int nb = n;
      while (nb > 0 && (*p == ' ' || *p < 0x20)) { p++; nb--; }
      while (nb > 0 && (p[nb - 1] == ' ' || p[nb - 1] < 0x20)) nb--;
      if (nb <= 0) return;

      char serial[24];
      if (nb > (int)sizeof(serial) - 1) nb = sizeof(serial) - 1;
      memcpy(serial, p, nb);
      serial[nb] = '\0';
      pushZRawStr(inifile, 0x0702, 776, serial);      // sans octet de longueur (cf. plus haut)

      if (!e.serialFromEmitter) {
        e.serialFromEmitter = true;
        LLOG("[LoRa] numero de serie compteur : %s\r\n", serial);
      }
      break;
    }
    case 0x09: {                                      // METER_DATE (4+N octets, §7.10)
      // Horodate interne du compteur (DATE, Standard). Historique : N=0 -> a ignorer.
      // FF66/514 attend une chaine prefixee (handleAttribute514 lit datas[0]=longueur).
      if (len < 4) return;
      uint8_t n = b[3];
      if (n == 0 || 4 + n > len) return;              // absent (Historique) ou tronque
      char date[20];
      if (n > sizeof(date) - 1) n = sizeof(date) - 1;
      int nb = 0;
      for (uint8_t i = 0; i < n && b[4 + i] >= 0x20; i++) date[nb++] = b[4 + i];
      date[nb] = '\0';
      if (nb > 0) pushZStr(inifile, 0xFF66, 514, date);
      break;
    }
    case 0x0A: {                                      // TARIFF_OPTION (16 octets, §7.11)
      // OPTARIF/DEMAIN (Historique) : champs 4 ASCII completes par 0x00 (absents en Standard).
      // CCASN/CCASN-1 [12-15] : aucun attribut dans le template ZLinky -> ignores.
      if (len < 16) return;
      e.linkyMode = b[3]; e.modeKnown = true;         // auto-descriptif (porte son mode)
      char s[5];
      int on = 0;
      for (int i = 0; i < 4 && b[4 + i] >= 0x20; i++) s[on++] = b[4 + i];
      s[on] = '\0';
      if (on > 0) pushZStr(inifile, 0xFF66, 0, s);    // OPTARIF
      int dn = 0;
      for (int i = 0; i < 4 && b[8 + i] >= 0x20; i++) s[dn++] = b[8 + i];
      s[dn] = '\0';
      if (dn > 0) pushZStr(inifile, 0xFF66, 1, s);    // DEMAIN (couleur Tempo du lendemain)
      break;
    }
    case 0x0B: {                                      // HISTO_ALERTS (12 octets, §7.12)
      // Alertes du mode Historique : preavis EJP, presence des potentiels, depassements par
      // phase. Memes attributs que le firmware Zigbee (cluster FF66, §8). Sans objet en
      // Standard, ou ces etiquettes n'existent pas. 0 = pas d'alerte en cours.
      if (len < 12) return;
      e.linkyMode = b[3]; e.modeKnown = true;         // auto-descriptif (porte son mode)
      if (b[3] & 0x01) break;                         // Standard : rien a ranger
      pushZ(inifile, 0xFF66, 4, b[4], 1);             // PEJP
      pushZ(inifile, 0xFF66, 3, b[5], 1);             // PPOT
      pushZ(inifile, 0xFF66, 6, getU16(&b[6]), 2);    // ADIR1
      pushZ(inifile, 0xFF66, 7, getU16(&b[8]), 2);    // ADIR2
      pushZ(inifile, 0xFF66, 8, getU16(&b[10]), 2);   // ADIR3
      break;
    }
    default:
      LLOG("[LoRa] sous-type EXT 0x%02X non mappe (len=%d)\r\n", b[2], len);
      break;
  }
}

/* ===================== Persistance ===================== */
bool loadLoraConfig() {
  for (int i = 0; i < LORA_MAX_EMITTERS; i++) loraEmitters[i] = LoraEmitter{};
  File f = LittleFS.open("/config/lora.json", "r");
  if (!f) return false;
  SpiRamJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { LLOG("[LoRa] /config/lora.json illisible: %s\r\n", err.c_str()); return false; }

  // Parametres radio globaux (defaut si absents : anciens fichiers d'avant le bidirectionnel).
  g_opChannel = o_clampChannel(doc["opChannel"] | LORA_OP_CHANNEL);
  g_sf        = o_clampSF(doc["sf"] | 11);

  int i = 0;
  for (JsonObject o : doc["emitters"].as<JsonArray>()) {
    if (i >= LORA_MAX_EMITTERS) break;
    const char *mac = o["mac"] | "";
    const char *key = o["key"] | "";
    if (strlen(mac) != 16 || strlen(key) != 32) continue;
    for (int k = 0; k < MAC_SIZE; k++)
      loraEmitters[i].mac[k] = (uint8_t)strtol(String(mac).substring(k * 2, k * 2 + 2).c_str(), nullptr, 16);
    for (int k = 0; k < KEY_SIZE; k++)
      loraEmitters[i].key[k] = (uint8_t)strtol(String(key).substring(k * 2, k * 2 + 2).c_str(), nullptr, 16);
    strlcpy(loraEmitters[i].deviceId, o["deviceId"] | LORA_ZLINKY_DEVICE_ID, sizeof(loraEmitters[i].deviceId));
    strlcpy(loraEmitters[i].model,    o["model"]    | LORA_ZLINKY_MODEL,     sizeof(loraEmitters[i].model));
    loraEmitters[i].valid = true;
    i++;
  }
  LLOG("[LoRa] %d emetteur(s) appaire(s) charge(s)\r\n", i);
  return true;
}

bool saveLoraConfig() {
  SpiRamJsonDocument doc(4096);
  doc["opChannel"] = g_opChannel;   // parametres radio operationnels globaux (§4)
  doc["sf"]        = g_sf;
  JsonArray arr = doc.createNestedArray("emitters");
  for (int i = 0; i < LORA_MAX_EMITTERS; i++) {
    if (!loraEmitters[i].valid) continue;
    JsonObject o = arr.createNestedObject();
    o["mac"] = macToHex(loraEmitters[i].mac);
    o["deviceId"] = loraEmitters[i].deviceId;   // type annonce a l'appairage -> template
    o["model"]    = loraEmitters[i].model;
    char kb[33];
    for (int k = 0; k < KEY_SIZE; k++) snprintf(&kb[k * 2], 3, "%02X", loraEmitters[i].key[k]);
    o["key"] = String(kb);
  }
  File f = LittleFS.open("/config/lora.tmp", "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  LittleFS.remove("/config/lora.json");
  return LittleFS.rename("/config/lora.tmp", "/config/lora.json");
}

int loraFindEmitterByMac(const String &macHex) {
  for (int i = 0; i < LORA_MAX_EMITTERS; i++)
    if (loraEmitters[i].valid && macToHex(loraEmitters[i].mac).equalsIgnoreCase(macHex)) return i;
  return -1;
}

int loraCountEmitters() {
  int n = 0;
  for (int i = 0; i < LORA_MAX_EMITTERS; i++) if (loraEmitters[i].valid) n++;
  return n;
}

bool loraRemoveEmitter(int slot) {
  if (slot < 0 || slot >= LORA_MAX_EMITTERS || !loraEmitters[slot].valid) return false;
  loraEmitters[slot] = LoraEmitter{};
  return saveLoraConfig();
}

// Met une lecture d'attribut en file (appelee depuis un handler web, tache async). Ne touche
// PAS la radio : sendPollRequest() s'en charge dans loop(), au prochain uplink (§8). Une
// nouvelle requete remplace celle en attente pour cet emetteur (file d'une seule entree).
bool loraQueuePoll(int slot, uint16_t cluster, uint16_t attr) {
  if (slot < 0 || slot >= LORA_MAX_EMITTERS || !loraEmitters[slot].valid) return false;
  LoraEmitter &e = loraEmitters[slot];
  e.pollCluster = cluster;
  e.pollAttr    = attr;
  e.pollRetries = 0;
  e.pollRespMs  = 0;         // efface la reponse precedente : on attend celle de cette requete
  e.pollPending = true;
  LLOG("[LoRa] POLL cluster=0x%04X attr=0x%04X en file pour slot %d (prochain uplink)\r\n",
       cluster, attr, slot);
  return true;
}

// Conversion int64 -> texte decimal (String(long) est 32 bits sur ESP32 : un index uint32 au
// dela de 2^31 s'afficherait negatif ; snprintf %lld n'est pas garanti par la libc nano).
static String i64ToStr(int64_t v) {
  bool neg = v < 0;
  uint64_t u = neg ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;   // -MIN sans overflow
  char buf[24];
  int i = sizeof(buf);
  buf[--i] = '\0';
  do { buf[--i] = (char)('0' + (int)(u % 10)); u /= 10; } while (u > 0 && i > 0);
  if (neg && i > 0) buf[--i] = '-';
  return String(&buf[i]);
}

// Formatte la valeur brute d'une reponse POLL en texte lisible selon son type ZCL.
static String formatPollValue(uint8_t ztype, const uint8_t *v, uint8_t n) {
  if (ztype == 0x41 || ztype == 0x42) {           // octet / character string
    String s;
    for (uint8_t i = 0; i < n; i++) s += (v[i] >= 0x20 && v[i] < 0x7F) ? (char)v[i] : '.';
    return s;
  }
  // Numeriques big-endian. int8/int16 signes ; le reste non signe.
  int64_t val = 0;
  for (uint8_t i = 0; i < n; i++) val = (val << 8) | v[i];
  if ((ztype == 0x28 && n == 1 && (v[0] & 0x80)) ||   // int8 negatif
      (ztype == 0x29 && n == 2 && (v[0] & 0x80))) {    // int16 negatif
    val -= (int64_t)1 << (8 * n);
  }
  return i64ToStr(val);
}

String loraPollStatusJson(int slot) {
  if (slot < 0 || slot >= LORA_MAX_EMITTERS || !loraEmitters[slot].valid)
    return F("{\"state\":\"idle\"}");
  const LoraEmitter &e = loraEmitters[slot];
  if (e.pollPending) return F("{\"state\":\"pending\"}");
  if (e.pollRespMs == 0) return F("{\"state\":\"idle\"}");
  const char *state = (e.pollRespStatus == POLL_OK)           ? "ok"
                    : (e.pollRespStatus == POLL_UNKNOWN_ATTR) ? "unknown"
                                                              : "bad";
  String j = "{\"state\":\"" + String(state) + "\"";
  j += ",\"cluster\":" + String(e.pollCluster);
  j += ",\"attr\":" + String(e.pollAttr);
  if (e.pollRespStatus == POLL_OK) {
    j += ",\"type\":" + String(e.pollRespType);
    j += ",\"value\":\"" + formatPollValue(e.pollRespType, e.pollRespValue, e.pollRespLen) + "\"";
  }
  j += "}";
  return j;
}

bool loraSetOpParams(uint8_t channel, uint8_t sf) {
  if (channel > 7 || sf < 7 || sf > 12) return false;
  g_opChannel = channel;
  g_sf        = sf;
  // Bascule immediate du recepteur sur les nouveaux parametres reseau. Les emetteurs deja
  // appaires restent sur l'ancienne config jusqu'a un nouvel appairage (§4) : ils seront muets
  // d'ici la. C'est le fonctionnement voulu (parametres reseau, pas par appareil).
  if (radioReady && !loraPairingMode && !awaitingConfirm) {
    setChannel(g_opChannel);
    setSF(g_sf);
    radio.startReceive();
    lastRadioRxMs = millis();  // laisse le temps aux emetteurs re-appaires de revenir
    LLOG("[LoRa] parametres reseau : canal %d, SF%d (re-appairer les emetteurs pour les suivre)\r\n",
         g_opChannel, g_sf);
  }
  saveLoraConfig();
  return true;
}

uint8_t loraGetChannel() { return g_opChannel; }
uint8_t loraGetSF()      { return g_sf; }

/* ===================== Création du device (appareil "normal") ===================== */
// Rappel : device_id N'EST PAS la MAC. C'est l'identifiant de type (décimal) qui NOMME le
// fichier template — DeviceData::loadTemplate() cherche "<device_id>.json" puis la clé [model].
// (Le chemin Zigbee fait pareil : SetInfoDeviceId(path, String(device_id)).) L'émetteur LoRa
// les annonce dans le PAIR_REQUEST étendu, donc un nouvel objet ne demande aucun code ici.
//
// Crée /db/<MAC>.json et l'ajoute à `devices` pour que l'objet LoRa apparaisse dans
// Mesures -> Appareils (avec ses attributs) et soit sélectionnable comme ZLinky dans
// Config -> Energie. Les données suivent via readZigbeeDatas().
// Le couple (device_id, model) annoncé par l'émetteur doit correspondre à un template
// existant, sinon la fiche de l'appareil restera vide ("Aucun template disponible").
// On le signale bruyamment ici : c'est la seule erreur qui ne se voit pas dans les logs
// de réception (les trames arrivent et sont déchiffrées normalement).
static void warnIfNoTemplate(const char *deviceId, const char *model) {
  if (!deviceId[0]) { LLOG("[LoRa] identite pas encore lue : template applique apres le POLL\r\n"); return; }
  String path = "/tp/" + String(deviceId) + ".json";
  if (!LittleFS.exists(path)) {
    LLOG("[LoRa] ATTENTION : template %s introuvable -> la fiche de l'appareil sera vide.\r\n"
         "                  L'emetteur annonce device_id=%s ; aucun data/tp/%s.json n'existe.\r\n",
         path.c_str(), deviceId, deviceId);
    return;
  }
  LLOG("[LoRa] template %s present (model attendu : %s)\r\n", path.c_str(), model);
}

// Attribue une short address 16 bits UNIQUE a un appareil LoRa. Les appareils LoRa n'ont pas de
// short address Zigbee ; or les pages (Appareils, fiche) indexent le rafraichissement live par
// short address (id DOM 'status_<addr>' et '<addr>_cluster_attr'). Avec la meme valeur (0) pour
// tous les LoRa, getElementById ne mettait a jour que le premier. On derive une valeur des 2
// derniers octets de la MAC, puis on resout les collisions (avec le Zigbee comme entre LoRa).
static uint16_t computeLoraShortAddr(const uint8_t *mac, const String &excludeId) {
  uint16_t addr = ((uint16_t)mac[6] << 8) | mac[7];
  if (addr == 0) addr = ((uint16_t)mac[4] << 8) | mac[5];
  if (addr == 0) addr = 1;
  for (int guard = 0; guard < 65535; guard++) {
    bool clash = false;
    for (size_t i = 0; i < devices.size(); i++) {
      if (devices[i]->getDeviceID() == excludeId) continue;   // ne pas se compter soi-meme
      if ((uint16_t)devices[i]->getInfo().shortAddr.toInt() == addr) { clash = true; break; }
    }
    if (!clash) return addr;
    if (++addr == 0) addr = 1;
  }
  return addr;
}

static void ensureLoraDevice(const uint8_t *mac, const char *deviceId, const char *model) {
  String id = macToHex(mac);
  warnIfNoTemplate(deviceId, model);
  for (size_t i = 0; i < devices.size(); i++) {
    if (devices[i]->getDeviceID() != id) continue;
    // Déjà présent : corriger un device_id/model obsolète (appareils créés par une version
    // antérieure, où device_id valait la MAC -> template introuvable).
    bool changed = false;
    if (devices[i]->getInfo().device_id != deviceId || devices[i]->getInfo().model != model) {
      devices[i]->setInfoDeviceID(deviceId);
      devices[i]->setInfoModel(model);
      devices[i]->reloadTemplate();
      changed = true;
      LLOG("[LoRa] device %s corrige (device_id=%s, model=%s)\r\n", id.c_str(), deviceId, model);
    }
    // Rattrape les appareils LoRa crees par une version anterieure avec short addr 0 (partagee).
    String sa = devices[i]->getInfo().shortAddr;
    if (sa.length() == 0 || sa.toInt() == 0) {
      uint16_t na = computeLoraShortAddr(mac, id);
      devices[i]->setInfoShortAddr(String(na));
      changed = true;
      LLOG("[LoRa] device %s : short addr attribuee = %u\r\n", id.c_str(), na);
    }
    if (changed) devices[i]->saveToFile();
    return;
  }

  void *mem = ps_malloc(sizeof(DeviceData));
  if (!mem) { LLOG("[LoRa] ps_malloc device KO\r\n"); return; }
  DeviceData *dev = new (mem) DeviceData("/db/" + id + ".json", id);
  dev->setInfoDeviceID(deviceId);   // -> nomme le template data/tp/<deviceId>.json
  dev->setInfoModel(model);         // -> la clé dans ce fichier
  dev->setInfoManufacturer("LiXee");
  dev->setInfoEndpoint("1");
  dev->setInfoStatus("00");
  dev->setInfoShortAddr(String(computeLoraShortAddr(mac, id)));   // short addr 16 bits unique
  dev->setInfoLastseen(FormattedDate);
  dev->saveToFile();
  devices.push_back(dev);
  invalidateDeviceCache();   // sinon findDevice() ne verrait pas ce nouvel appareil (mode FF66 KO)
  LLOG("[LoRa] device cree : %s (device_id=%s, model=%s)\r\n", id.c_str(), deviceId, model);
}

// Modele identifie : un ZLinky devient le compteur Linky de la configuration Energie, comme a
// l'appairage Zigbee (SpecificTreatment). Le mode Linky (FF66/768) suit avec la trame
// ESSENTIAL suivante, que handleAttribute768 n'applique qu'au ZLinky configure.
static void registerLinkyIfZLinky(const uint8_t *mac, const char *model) {
  if (strcmp(model, LORA_ZLINKY_MODEL) != 0) return;
  String id = macToHex(mac);
  if (id == ConfigGeneral.ZLinky) return;
  strlcpy(ConfigGeneral.ZLinky, id.c_str(), sizeof(ConfigGeneral.ZLinky));
  config_write("configGeneral.json", "ZLinky", id);
  LLOG("[LoRa] %s declare comme compteur Linky (config Energie)\r\n", id.c_str());
}

/* ===================== Appairage ===================== */
// Demande d'appairage posée par un handler web. La radio ne DOIT PAS être touchée depuis la
// tâche async (les handlers y tournent), sinon on entre en conflit SPI avec loraReceiverLoop()
// qui lit/écrit la radio dans loop() : c'est ce qui écrasait le canal d'appairage (3) par le
// canal de données (4) et laissait la fenêtre "ouverte" mais sur le mauvais canal.
static volatile bool loraPairingRequested = false;

// Publique : appelée par les routes /loraPair et /cmdLoraPairAssist (tâche async).
// Ne fait que signaler ; le vrai démarrage a lieu dans loraReceiverLoop() (contexte loop()).
void loraStartPairing() {
  loraPairingRequested = true;
}

// Interne : exécutée UNIQUEMENT depuis loop(), donc seule à piloter la radio.
static void loraStartPairingNow() {
  if (!radioReady) { LLOG("[LoRa] appairage impossible : radio non prete\r\n"); return; }
  loraPairingMode    = true;
  loraPairingStartMs = millis();
  awaitingConfirm    = false;
  pinMode(LED_PIN, OUTPUT);           // deja fait au boot, mais on ne depend pas de l'ordre d'init
  ledLastToggle = 0; ledState = false;   // demarre le clignotement des le prochain loop
  // Rendez-vous d'appairage FIXE : canal 3 + SF11 (§4), quelle que soit la config operationnelle.
  // C'est ce qui permet a un ZLinky tournant sur un autre canal/SF de revenir se faire entendre.
  setChannel(LORA_PAIR_CHANNEL);
  setSF(LORA_PAIR_SF);
  rxFlag = false;                     // ignore une IRQ DIO1 residuelle du canal operationnel
  radio.setDio1Action(onLoraRx);
  radio.startReceive();
  LLOG("[LoRa] appairage ouvert %d s sur canal %d (%.0f MHz), SF%d\r\n",
       LORA_PAIR_WINDOW_MS / 1000, LORA_PAIR_CHANNEL, CH_FREQ[LORA_PAIR_CHANNEL], LORA_PAIR_SF);
}

static int findOrCreateSlot(const uint8_t *mac) {
  for (int i = 0; i < LORA_MAX_EMITTERS; i++)
    if (loraEmitters[i].valid && memcmp(loraEmitters[i].mac, mac, MAC_SIZE) == 0) return i;
  for (int i = 0; i < LORA_MAX_EMITTERS; i++) if (!loraEmitters[i].valid) return i;
  return -1;
}

static void handlePairRequest(const uint8_t *buf, int len) {
  if (!loraPairingMode || awaitingConfirm || len < SZ_PAIR_REQUEST) return;
  memcpy(pendingMAC, &buf[2], MAC_SIZE);

  // Type de l'objet (device_id + model, qui désignent le template comme en Zigbee).
  // Format de base (11 octets, celui du firmware actuel) : rien n'est annoncé -> identité
  // VIDE, lue par POLL du cluster Basic une fois l'appairage confirmé (cf. handlePairConfirm).
  // Format étendu (14+N octets, firmwares intermédiaires) : [11-12] device_id (uint16 BE),
  // [13] longueur du model, [14..] model -> pris tel quel, pas de POLL.
  if (len >= 14 && (14 + buf[13]) <= len && buf[13] < sizeof(pendingModel)) {
    snprintf(pendingDeviceId, sizeof(pendingDeviceId), "%u", getU16(&buf[11]));
    memcpy(pendingModel, &buf[14], buf[13]);
    pendingModel[buf[13]] = '\0';
  } else {
    pendingDeviceId[0] = '\0';
    pendingModel[0]    = '\0';
  }

  for (int i = 0; i < KEY_SIZE; i++) pendingKey[i] = (uint8_t)(esp_random() & 0xFF);
  LLOG("[LoRa] PAIR_REQUEST de %s (device_id=%s, model=%s)\r\n",
       macToHex(pendingMAC).c_str(),
       pendingDeviceId[0] ? pendingDeviceId : "a lire", pendingModel[0] ? pendingModel : "a lire");

  uint8_t pkt[SZ_PAIR_RESPONSE];
  pkt[0] = 0x14;                       // v1 | PAIR_RESPONSE
  pkt[1] = buf[1];                     // écho du seq
  memcpy(&pkt[2], pendingKey, KEY_SIZE);
  pkt[18] = 0x00;                      // status OK
  pkt[19] = g_opChannel;   // op_channel assigne a l'emetteur (§4.2)
  pkt[20] = g_sf;          // op_SF assigne a l'emetteur (§4.2)

  radio.clearDio1Action();
  int st = radio.transmit(pkt, SZ_PAIR_RESPONSE);
  radio.setDio1Action(onLoraRx);
  if (st != RADIOLIB_ERR_NONE) {
    LLOG("[LoRa] TX PAIR_RESPONSE echec=%d\r\n", st);
    radio.startReceive();
    return;
  }
  // Le PAIR_CONFIRM arrive sur la config OPERATIONNELLE (canal + SF), pas sur le rendez-vous :
  // l'emetteur bascule dessus des reception de PAIR_RESPONSE, on le suit. clearIrqStatus (dans
  // setChannel/setSF) + reset de rxFlag pour ne pas rater le CONFIRM a cause d'un etat residuel.
  setChannel(g_opChannel);
  setSF(g_sf);
  rxFlag = false;
  radio.setDio1Action(onLoraRx);
  radio.startReceive();
  awaitingConfirm = true;
  confirmDeadline = millis() + 4000;
  LLOG("[LoRa] PAIR_RESPONSE envoye -> attente CONFIRM sur canal %d, SF%d\r\n", g_opChannel, g_sf);
}

static void handlePairConfirm(const uint8_t *buf, int len) {
  if (!awaitingConfirm || len != SZ_PAIR_CONFIRM) return;
  if (memcmp(&buf[2], pendingMAC, MAC_SIZE) != 0) return;

  // Preuve = AES-ECB( MAC(8) || 0x00*8 , clé )[0..3]
  uint8_t in[16], out[16];
  memset(in, 0, 16);
  memcpy(in, pendingMAC, MAC_SIZE);
  aes128_ecb_encrypt(in, pendingKey, out);
  if (memcmp(&buf[10], out, 4) != 0) { LLOG("[LoRa] preuve d'appairage invalide\r\n"); return; }

  int slot = findOrCreateSlot(pendingMAC);
  if (slot < 0) { LLOG("[LoRa] aucun slot libre (max %d)\r\n", LORA_MAX_EMITTERS); return; }
  memcpy(loraEmitters[slot].mac, pendingMAC, MAC_SIZE);
  memcpy(loraEmitters[slot].key, pendingKey, KEY_SIZE);
  strlcpy(loraEmitters[slot].deviceId, pendingDeviceId, sizeof(loraEmitters[slot].deviceId));
  strlcpy(loraEmitters[slot].model,    pendingModel,    sizeof(loraEmitters[slot].model));
  loraEmitters[slot].valid    = true;
  loraEmitters[slot].seqInit  = false;
  loraEmitters[slot].rxCount  = 0;
  loraEmitters[slot].missed   = 0;
  loraEmitters[slot].pollPending = false;
  // Identité inconnue (PAIR_REQUEST de base) : la lire par POLL dès le prochain uplink.
  loraEmitters[slot].identStep   = pendingDeviceId[0] ? 0 : 1;
  loraEmitters[slot].identNextMs = millis();
  saveLoraConfig();
  ensureLoraDevice(pendingMAC, pendingDeviceId, pendingModel);
  // Identite deja donnee par le PAIR_REQUEST : le modele est connu des maintenant
  if (pendingModel[0]) registerLinkyIfZLinky(pendingMAC, pendingModel);

  awaitingConfirm = false;
  loraPairingMode = false;
  LLOG("[LoRa] *** APPAIRAGE REUSSI *** %s (slot %d)\r\n", macToHex(pendingMAC).c_str(), slot);

  // Signaler l'appareil a l'assistant d'appairage. On N'utilise PAS l'alerte partagee
  // (alertList / /getAlert) : elle est destructive et GLOBALE, donc n'importe quelle autre
  // page ouverte qui sonde /getAlert consomme l'evenement avant l'assistant (observe :
  // l'assistant recevait toujours une reponse vide). On expose un etat dedie a l'appairage
  // LoRa, lu via /loraPairStatus par l'assistant, que rien d'autre ne consomme.
  strlcpy(loraPairedMac,   macToHex(pendingMAC).c_str(), sizeof(loraPairedMac));
  strlcpy(loraPairedModel, pendingModel[0] ? pendingModel : "Appareil LoRa", sizeof(loraPairedModel));
  loraPairedPending = true;
}

/* ===================== Réception des données ===================== */
// Longueur de preambule (en symboles) d'une requete de lecture, pour qu'elle dure ~40 ms meme a
// bas SF. Tsym = 2^sf / 406250 s ; N = 0,040 x 406250 / 2^sf = 16250 >> sf. Jamais moins que
// LORA_PREAMBLE : SF7 -> 126, SF8 -> 63, SF9 -> 31, SF10 -> 15, SF11 et SF12 -> 8.
static uint16_t pollPreamble(uint8_t sf) {
  uint32_t n = 16250UL >> sf;
  return (n < LORA_PREAMBLE) ? LORA_PREAMBLE : (uint16_t)n;
}

// Injecte la valeur d'une reponse POLL dans le pipeline habituel (readZigbeeDatas). La forme
// des `datas` depend du type ZCL : une chaine doit etre prefixee de sa longueur (format
// attendu par les handlers FF66) ; un numerique passe ses octets bruts big-endian tels quels.
static void injectPollValue(const String &inifile, uint16_t cluster, uint16_t attr,
                            uint8_t ztype, const uint8_t *val, uint8_t vlen) {
  uint8_t c[2] = {(uint8_t)(cluster >> 8), (uint8_t)(cluster & 0xFF)};
  uint8_t a[2] = {(uint8_t)(attr >> 8), (uint8_t)(attr & 0xFF)};
  if (ztype == 0x41 || ztype == 0x42) {           // octet / character string
    char d[34];
    uint8_t n = (vlen > sizeof(d) - 1) ? sizeof(d) - 1 : vlen;
    d[0] = (char)n;
    memcpy(&d[1], val, n);
    readZigbeeDatas(inifile, c, a, ztype, (int)n + 1, d);
  } else {                                          // numerique big-endian
    char d[8];
    uint8_t n = (vlen > sizeof(d)) ? sizeof(d) : vlen;
    memcpy(d, val, n);
    readZigbeeDatas(inifile, c, a, ztype, (int)n, d);
  }
}

// Emet une requete de lecture (POLL_REQUEST, §8) DANS la fenetre RX ouverte par l'uplink qu'on
// vient de recevoir. A appeler le plus tot possible apres le RxDone (fenetre ~300 ms, TX attendu
// vers T+20 ms), donc AVANT le mapping qui est lent. La radio revient en RX pour capter la
// reponse. Une lecture est idempotente : ni compteur ni confirmation.
static void sendPollRequest(LoraEmitter &e) {
  uint8_t pkt[16];
  pkt[0] = 0x10 | T_POLL_REQUEST;          // version 1 | type (0x1B)
  pkt[1] = e.pollSeq++;                    // seq -> nonce cote emetteur
  pkt[2] = (uint8_t)(e.pollCluster >> 8);
  pkt[3] = (uint8_t)(e.pollCluster & 0xFF);
  pkt[4] = (uint8_t)(e.pollAttr >> 8);
  pkt[5] = (uint8_t)(e.pollAttr & 0xFF);
  int total = encryptPacket(pkt, 6, e.key, e.mac);

  // Preambule ALLONGE aux SF bas. Le ZLinky ouvre sa fenetre RX apres son uplink et ne detecte
  // un preambule que vers T+40-60 ms (protocole §8) : a SF7, 8 symboles ne durent que ~2,5 ms et
  // sont deja passes. On vise ~40 ms de preambule. Un preambule plus long que celui attendu par
  // le recepteur (8) reste recu : seul l'inverse pose probleme. A SF11/SF12 il reste a 8.
  uint16_t pre = pollPreamble(radioSF);
  radio.clearDio1Action();
  if (pre != LORA_PREAMBLE) radio.setPreambleLength(pre);
  int st = radio.transmit(pkt, total);                 // bloquant (time-on-air)
  if (pre != LORA_PREAMBLE) radio.setPreambleLength(LORA_PREAMBLE);
  radio.setDio1Action(onLoraRx);
  radio.startReceive();                      // re-ecoute : la reponse arrive comme une trame normale
  e.pollRetries++;
  LLOG("[LoRa] POLL_REQUEST cluster=0x%04X attr=0x%04X envoye (essai %d, tx=%d)\r\n",
       e.pollCluster, e.pollAttr, e.pollRetries, st);
}

static void handleData(uint8_t *buf, int len, float rssi, float snr) {
  // On essaie chaque clé : le bon émetteur est celui dont le MIC valide.
  uint8_t backup[64];
  if (len > (int)sizeof(backup)) return;
  memcpy(backup, buf, len);

  for (int i = 0; i < LORA_MAX_EMITTERS; i++) {
    if (!loraEmitters[i].valid) continue;
    memcpy(buf, backup, len);
    if (!decryptPacket(buf, len, loraEmitters[i].key, loraEmitters[i].mac)) continue;

    LoraEmitter &e = loraEmitters[i];
    uint8_t seq = buf[1];
    if (e.seqInit) {
      uint8_t expected = (uint8_t)(e.lastSeq + 1);
      if (seq != expected) e.missed += (uint8_t)(seq - expected);
    }
    e.lastSeq = seq; e.seqInit = true;
    e.rxCount++; e.lastRssi = rssi; e.lastSnr = snr; e.lastSeenMs = millis();
    lastRadioRxMs = millis();      // trame VALIDE de notre device (base du watchdog)

    // Identification en cours (cluster Basic) : la mettre en file si rien d'autre n'attend.
    if (e.identStep && !e.pollPending && (int32_t)(millis() - e.identNextMs) >= 0) {
      loraQueuePoll(i, POLL_CLUSTER_BASIC,
                    e.identStep == 1 ? POLL_ATTR_DEVICE_ID : POLL_ATTR_MODEL);
    }
    // Version du firmware : apres l'identification, et si rien d'autre n'attend.
    if (!e.identStep && e.verStep && !e.pollPending && (int32_t)(millis() - e.verNextMs) >= 0) {
      loraQueuePoll(i, POLL_CLUSTER_BASIC,
                    e.verStep == 1 ? POLL_ATTR_FW_VERSION : POLL_ATTR_SW_BUILD);
    }

    // Fenetre descendante (§8) : si une lecture d'attribut attend, l'emettre MAINTENANT, avant
    // le mapping (readZigbeeDatas = MQTT + fichiers, plusieurs ms) qui ferait rater les ~20 ms.
    // Best-effort borne : au-dela de 8 uplinks sans reponse, on abandonne (perte durable).
    if (e.pollPending) {
      if (e.pollRetries >= 8) {
        e.pollPending = false;
        // Identification sans réponse : on ne renonce pas (sans identité, pas de template),
        // on réessaiera plus tard plutôt qu'à chaque uplink.
        if (e.identStep && e.pollCluster == POLL_CLUSTER_BASIC) e.identNextMs = millis() + IDENT_RETRY_MS;
        if (e.verStep   && e.pollCluster == POLL_CLUSTER_BASIC) e.verNextMs   = millis() + IDENT_RETRY_MS;
        LLOG("[LoRa] POLL cluster=0x%04X attr=0x%04X abandonne (pas de reponse apres %d essais)\r\n",
             e.pollCluster, e.pollAttr, e.pollRetries);
      } else {
        sendPollRequest(e);
      }
    }

    String inifile = macToHex(e.mac) + ".json";
    // Trame valide : on date le contact, comme le fait le Zigbee a la reception. Sans cela la
    // date restait celle de l'appairage ("Last seen" fige dans l'IHM), et ScanDevicesToRAZ()
    // croyait l'appareil muet depuis plus d'une heure : journal de debug rempli chaque minute
    // et puissances remises a zero (issue #42).
    SetInfoLastseen(inifile, FormattedDate);
    int dataLen = len - MIC_SIZE;
    if ((buf[0] & 0x0F) == T_ESSENTIAL) mapEssential(inifile, buf, dataLen, e);
    else                                mapExtended(inifile, buf, dataLen, e);
    return;
  }
  LLOG("[LoRa] trame chiffree non attribuee (MIC KO pour toutes les cles), len=%d\r\n", len);
}

// Réponse à la lecture d'identité (cluster Basic). device_id d'abord, puis model ; une fois les
// deux connus, l'appareil reçoit son template. Un firmware qui ne connaît pas le cluster Basic
// (UNKNOWN_ATTR) est un ancien ZLinky : on retombe sur son identité.
static void handleIdentResponse(int slot, uint16_t attr, uint8_t status, uint8_t ztype,
                                const uint8_t *val, uint8_t vlen) {
  LoraEmitter &e = loraEmitters[slot];
  if (status == POLL_UNKNOWN_ATTR) {
    LLOG("[LoRa] %s ne connait pas le cluster Basic : firmware ancien, identite ZLinky par defaut\r\n",
         macToHex(e.mac).c_str());
    strlcpy(e.deviceId, LORA_ZLINKY_DEVICE_ID, sizeof(e.deviceId));
    strlcpy(e.model,    LORA_ZLINKY_MODEL,     sizeof(e.model));
    e.identStep = 0;
  } else if (status != POLL_OK) {
    e.identNextMs = millis() + IDENT_RETRY_MS;   // requête refusée : nouvel essai plus tard
    return;
  } else if (attr == POLL_ATTR_DEVICE_ID && e.identStep == 1) {
    uint32_t id = 0;
    for (uint8_t k = 0; k < vlen && k < 4; k++) id = (id << 8) | val[k];   // big-endian
    snprintf(e.deviceId, sizeof(e.deviceId), "%u", (unsigned)id);
    e.identStep   = 2;                           // puis le model
    e.identNextMs = millis();
    LLOG("[LoRa] %s : device_id = %s (type 0x%02X)\r\n", macToHex(e.mac).c_str(), e.deviceId, ztype);
    return;                                      // rien à créer tant que le model manque
  } else if (attr == POLL_ATTR_MODEL && e.identStep == 2) {
    uint8_t n = (vlen < sizeof(e.model) - 1) ? vlen : sizeof(e.model) - 1;
    memcpy(e.model, val, n);
    e.model[n] = '\0';
    e.identStep = 0;
  } else {
    return;                                      // réponse hors séquence : on attend la bonne
  }
  LLOG("[LoRa] %s identifie : device_id=%s, model=%s\r\n", macToHex(e.mac).c_str(), e.deviceId, e.model);
  saveLoraConfig();
  ensureLoraDevice(e.mac, e.deviceId, e.model);  // met à jour la fiche et charge le template
  registerLinkyIfZLinky(e.mac, e.model);
  e.verStep   = 1;                               // puis la version du firmware
  e.verNextMs = millis();
}

// Range la version dans la fiche de l'appareil (champ "Version logicielle", comme en Zigbee).
static void setLoraDeviceVersion(const uint8_t *mac, const String &ver) {
  String id = macToHex(mac);
  for (size_t i = 0; i < devices.size(); i++) {
    if (devices[i]->getDeviceID() != id) continue;
    if (devices[i]->getInfo().software_version != ver) {
      devices[i]->setInfoSoftVersion(ver);
      devices[i]->saveToFile();
    }
    return;
  }
}

// Version 0xMMmp en clair : 0x0123 -> "1.2.3".
static String fwVersionText(uint16_t v) {
  return String(v >> 8) + "." + String((v >> 4) & 0x0F) + "." + String(v & 0x0F);
}

// Réponse à la lecture de la version (cluster Basic, 0xFF01 puis 0x4000).
static void handleVersionResponse(int slot, uint16_t attr, uint8_t status,
                                  const uint8_t *val, uint8_t vlen) {
  LoraEmitter &e = loraEmitters[slot];
  if (attr == POLL_ATTR_FW_VERSION && e.verStep == 1) {
    if (status == POLL_UNKNOWN_ATTR) {
      // Firmware anterieur a 1.0.0 : il ne connait ni 0xFF01 ni 0x4000.
      e.fwKnown = false;
      e.verStep = 0;
      setLoraDeviceVersion(e.mac, "< 1.0.0");
      LLOG("[LoRa] %s : firmware anterieur a 1.0.0 (version non exposee)\r\n", macToHex(e.mac).c_str());
      return;
    }
    if (status != POLL_OK || vlen < 2) { e.verNextMs = millis() + IDENT_RETRY_MS; return; }
    e.fwVersion = getU16(val);
    e.fwKnown   = true;
    e.verStep   = 2;                             // puis le libelle complet
    e.verNextMs = millis();
    setLoraDeviceVersion(e.mac, fwVersionText(e.fwVersion));
    LLOG("[LoRa] %s : firmware %s (0x%04X)\r\n", macToHex(e.mac).c_str(),
         fwVersionText(e.fwVersion).c_str(), e.fwVersion);
  } else if (attr == POLL_ATTR_SW_BUILD && e.verStep == 2) {
    e.verStep = 0;
    if (status != POLL_OK || vlen == 0) return;  // la version courte reste affichee
    char b[32];
    uint8_t n = (vlen < sizeof(b) - 1) ? vlen : sizeof(b) - 1;
    uint8_t k = 0;
    for (; k < n && val[k] >= 0x20 && val[k] < 0x7F; k++) b[k] = (char)val[k];
    b[k] = '\0';
    if (k > 0) {
      setLoraDeviceVersion(e.mac, String(b));
      LLOG("[LoRa] %s : build %s\r\n", macToHex(e.mac).c_str(), b);
    }
  }
}

// Reçoit le POLL_RESPONSE (type 0x0C, chiffré, §8). Valide le MIC via la clé de l'émetteur,
// memorise le resultat pour l'UI et, si OK, injecte la valeur dans le pipeline habituel comme
// une donnee recue normalement. Format clair : [cluster(2)][attr(2)][statut][type][len][valeur].
static void handlePollResponse(uint8_t *buf, int len) {
  uint8_t backup[64];
  if (len > (int)sizeof(backup)) return;
  memcpy(backup, buf, len);
  for (int i = 0; i < LORA_MAX_EMITTERS; i++) {
    if (!loraEmitters[i].valid) continue;
    memcpy(buf, backup, len);
    if (!decryptPacket(buf, len, loraEmitters[i].key, loraEmitters[i].mac)) continue;
    LoraEmitter &e = loraEmitters[i];
    int dataLen = len - MIC_SIZE;
    if (dataLen < 9) return;                  // en-tete = 9 octets (cluster,attr,statut,type,len)
    uint16_t cluster = getU16(&buf[2]);
    uint16_t attr    = getU16(&buf[4]);
    uint8_t  status  = buf[6];
    uint8_t  ztype   = buf[7];
    uint8_t  vlen    = buf[8];
    if (9 + (int)vlen > dataLen) vlen = (uint8_t)(dataLen - 9);   // borne sur la trame reelle

    e.pollPending    = false;                 // reponse recue : fin du best-effort
    e.pollRespMs     = millis();
    e.pollRespStatus = status;
    e.pollRespType   = ztype;
    e.pollRespLen    = (vlen > sizeof(e.pollRespValue)) ? sizeof(e.pollRespValue) : vlen;
    memcpy(e.pollRespValue, &buf[9], e.pollRespLen);
    LLOG("[LoRa] POLL_RESPONSE cluster=0x%04X attr=0x%04X statut=0x%02X type=0x%02X len=%d\r\n",
         cluster, attr, status, ztype, vlen);

    // Réponse d'identification (cluster Basic) : traitée ici, PAS injectée dans le pipeline
    // de données -- c'est l'identité de l'objet, pas une mesure.
    if (cluster == POLL_CLUSTER_BASIC && e.identStep) {
      handleIdentResponse(i, attr, status, ztype, &buf[9], vlen);
      return;
    }
    if (cluster == POLL_CLUSTER_BASIC && e.verStep
        && (attr == POLL_ATTR_FW_VERSION || attr == POLL_ATTR_SW_BUILD)) {
      handleVersionResponse(i, attr, status, &buf[9], vlen);
      return;
    }
    if (status == POLL_OK) {
      String inifile = macToHex(e.mac) + ".json";
      injectPollValue(inifile, cluster, attr, ztype, &buf[9], vlen);
    }
    return;
  }
}

/* ===================== Mise a jour OTA d'un emetteur (doc/LORA_PROTOCOL.md §10) =====================
 *   OTA_START (version, taille, CRC32, nb de blocs, fenetre W)  -> OTA_ACK (W retenue)
 *   OTA_BLOCK x N (64 octets, dans l'ordre)                     -> OTA_ACK par FENETRE de W blocs
 *   OTA_END (CRC32)                                             -> OTA_ACK, puis redemarrage
 * L'emetteur n'ecoute ces trames qu'en mode "OTA recovery" (bouton maintenu a la mise sous
 * tension), sur le canal et le SF operationnels : la radio de la box y est deja.
 *
 * Acquittement par fenetre : acquitter chaque bloc ferait emettre l'emetteur toutes les ~0,7 s,
 * ce que l'alimentation TIC ne supporte pas. Il n'emet donc que sur un bloc de fin de fenetre
 * ((index + 1) % W == 0, ou dernier bloc), et son ACK porte l'index du PROCHAIN bloc attendu.
 * Un bloc hors sequence est ignore en silence. Si l'emetteur renvoie W = 0 (firmware sans
 * fenetre), on retombe sur l'ancien mode : un ACK par bloc, qui porte l'index du bloc acquitte.
 *
 * Machine a etats NON bloquante, executee dans loraReceiverLoop() : seule l'emission d'une
 * trame bloque (temps d'antenne). L'attente de l'ACK passe par la reception normale, donc les
 * autres emetteurs restent recus pendant la mise a jour. La liaison est a l'alternat : la box
 * n'emet JAMAIS pendant qu'elle attend un ACK.
 */
#define T_OTA_START  0x06
#define T_OTA_BLOCK  0x07
#define T_OTA_END    0x08
#define T_OTA_ACK    0x09

#define OTA_BLOCK_SIZE          64        // taille par defaut, et maximum (tampon d'emission)
#define OTA_ACK_TIMEOUT_MS      5000UL    // attente de l'ACK de START / END / d'un bloc seul
#define OTA_MAX_RETRIES         10        // essais d'un bloc (mode sans fenetre) ou de OTA_END
#define OTA_START_TRIES         36        // 36 x 5 s = 3 min pour mettre l'emetteur en mode OTA
#define OTA_MAX_FILE_SIZE       (1024UL * 1024UL)

#define OTA_WINDOW_DEFAULT      16        // fenetre demandee (recommandee par le protocole)
#define OTA_WINDOW_MAX          32
#define OTA_WIN_ACK_TIMEOUT_MS  2000UL    // attente de l'ACK de fin de fenetre
#define OTA_BLOCK_GAP_MS        100UL     // espacement minimal entre deux blocs d'une fenetre
#define OTA_WIN_MAX_FAILS       10        // echecs consecutifs sur une meme fenetre
#define OTA_MAX_RESTARTS        3         // reprises a OTA_START apres un NOT_STARTED

#define OTA_STATUS_OK            0x00
#define OTA_STATUS_BUSY          0x01
#define OTA_STATUS_OUT_OF_RANGE  0x04
#define OTA_STATUS_NOT_STARTED   0x07

enum { OTA_IDLE = 0, OTA_WAITING, OTA_SENDING, OTA_ENDING, OTA_DONE, OTA_FAILED };

static struct {
  // demande posee par la tache web, consommee dans loop()
  volatile bool requested;
  volatile bool cancelRequested;
  int      reqSlot;
  uint16_t reqVersion;
  uint8_t  reqPreamble;      // diagnostic : 0 = LORA_PREAMBLE
  uint8_t  reqSF;            // diagnostic : 0 = SF operationnel
  uint8_t  reqBlockSize;     // diagnostic : 0 = OTA_BLOCK_SIZE
  uint8_t  reqWindow;        // fenetre demandee dans l'OTA_START (1 = un ACK par bloc)
  uint8_t  blockSize;        // taille de bloc annoncee dans l'OTA_START
  uint8_t  txPreamble;       // valeurs retenues pour la mise a jour en cours
  uint8_t  txSF;
  // etat courant
  uint8_t  state;
  int      slot;
  File     file;
  uint32_t size;
  uint32_t crc;
  uint16_t version;
  uint16_t totalBlocks;
  uint16_t block;            // prochain bloc attendu par l'emetteur (tout ce qui precede est acquis)
  uint16_t txBlock;          // bloc porte par la trame OTA_BLOCK en cours d'emission
  uint8_t  seq;              // seq de la prochaine trame (entre dans le nonce)
  uint16_t tries;            // essais de la trame en cours (START, END, bloc seul)
  uint32_t totalRetries;
  bool     waitingAck;
  uint32_t ackDeadline;
  bool     ackGot;
  uint16_t ackBlock;
  uint8_t  ackStatus;
  uint8_t  ackWindow;        // octet 5 de l'ACK : fenetre retenue par l'emetteur (0 = sans fenetre)
  // acquittement par fenetre
  uint8_t  window;           // fenetre en vigueur (1 = un ACK par bloc)
  uint16_t winEnd;           // dernier bloc de la fenetre en cours
  uint16_t winNext;          // prochain bloc a emettre dans cette fenetre
  bool     winWaiting;       // true : fenetre emise, on attend son ACK
  uint32_t nextTxMs;         // pas d'emission avant cet instant (espacement des blocs)
  uint8_t  winFails;         // echecs consecutifs sans progression
  uint8_t  restarts;         // reprises a OTA_START
  uint32_t startMs;          // debut de l'envoi des blocs (pour le debit / l'estimation)
  uint32_t endMs;
  char     msg[112];
} ota;

static const char *otaStatusText(uint8_t st) {
  switch (st) {
    case 0x00: return "OK";
    case 0x01: return "emetteur occupe";
    case 0x02: return "bloc corrompu";
    case 0x03: return "erreur d'ecriture en flash";
    case 0x04: return "bloc hors sequence";
    case 0x05: return "firmware trop volumineux";
    case 0x06: return "CRC du firmware incorrect";
    case 0x07: return "l'emetteur n'est plus en mise a jour (il a redemarre ?)";
    default:   return "statut inconnu";
  }
}

String loraOtaFilePath(int slot) {
  if (slot < 0 || slot >= LORA_MAX_EMITTERS || !loraEmitters[slot].valid) return String("");
  return "/ota/lora_" + macToHex(loraEmitters[slot].mac) + ".ota";
}

bool loraOtaActive() {
  return ota.requested || ota.state == OTA_WAITING || ota.state == OTA_SENDING || ota.state == OTA_ENDING;
}

bool loraOtaStart(int slot, uint16_t version, String &err, uint8_t preamble, uint8_t sf,
                  uint8_t blockSize, uint8_t window) {
  if (!radioReady)                 { err = "radio LoRa non prete"; return false; }
  if (loraOtaActive())             { err = "une mise a jour est deja en cours"; return false; }
  if (loraPairingMode)             { err = "appairage en cours"; return false; }
  String path = loraOtaFilePath(slot);
  if (!path.length())              { err = "emetteur inconnu"; return false; }
  File f = LittleFS.open(path, "r");
  if (!f)                          { err = "aucun fichier firmware depose"; return false; }
  size_t sz = f.size();
  f.close();
  if (sz == 0)                     { err = "fichier firmware vide"; return false; }
  if (sz > OTA_MAX_FILE_SIZE)      { err = "fichier firmware trop volumineux"; return false; }
  ota.reqSlot         = slot;
  ota.reqVersion      = version;
  ota.reqPreamble     = (preamble >= 4 && preamble <= 64) ? preamble : 0;
  ota.reqSF           = (sf >= 7 && sf <= 12) ? sf : 0;
  ota.reqBlockSize    = (blockSize >= 8 && blockSize <= OTA_BLOCK_SIZE) ? blockSize : 0;
  ota.reqWindow       = (window == 0) ? OTA_WINDOW_DEFAULT
                      : (window > OTA_WINDOW_MAX ? OTA_WINDOW_MAX : window);
  ota.cancelRequested = false;
  ota.requested       = true;      // pris en compte au prochain passage de loop()
  return true;
}

void loraOtaCancel() {
  if (loraOtaActive()) ota.cancelRequested = true;
}

static void otaFinish(uint8_t state, const char *msg) {
  if (ota.file) ota.file.close();
  ota.state      = state;
  ota.waitingAck = false;
  ota.endMs      = millis();
  strlcpy(ota.msg, msg, sizeof(ota.msg));
  LLOG("[LoRa] OTA %s : %s (bloc %u/%u, %lu re-emission(s))\r\n",
       state == OTA_DONE ? "TERMINEE" : "ECHEC", msg, ota.block, ota.totalBlocks,
       (unsigned long)ota.totalRetries);
  if (ota.txSF && ota.txSF != g_sf) setSF(g_sf);    // diagnostic : retour au SF operationnel
  // Fichier supprime apres un succes ; conserve apres un echec, pour relancer sans le renvoyer.
  if (state == OTA_DONE) {
    // L'emetteur redemarre sur le nouveau firmware : sa version sera relue a ses premieres
    // trames, ce qui permet de verifier que la mise a jour a pris (protocole §10).
    loraEmitters[ota.slot].verStep   = 1;
    loraEmitters[ota.slot].verNextMs = millis();
    String path = loraOtaFilePath(ota.slot);
    if (path.length()) LittleFS.remove(path);
  }
  radio.startReceive();
}

// Ouvre le fichier, calcule son CRC32 (standard, celui de zlib) et prepare l'OTA_START.
static void otaBegin() {
  // L'etat passe a "en attente" AVANT tout le reste : le calcul du CRC qui suit dure un moment,
  // et l'etat de la tentative PRECEDENTE (souvent un echec) restait visible entre-temps. La page,
  // qui interroge la box a ce moment-la, affichait cet ancien echec et cessait de suivre.
  ota.state       = OTA_WAITING;
  ota.msg[0]      = '\0';
  ota.block       = 0;
  ota.totalBlocks = 0;
  ota.requested   = false;
  ota.slot        = ota.reqSlot;
  ota.version     = ota.reqVersion;
  ota.txBlock = 0; ota.seq = 0; ota.tries = 0; ota.totalRetries = 0;
  ota.waitingAck = false; ota.ackGot = false;
  ota.window = 1; ota.winWaiting = false; ota.winFails = 0; ota.restarts = 0;
  ota.startMs = 0; ota.endMs = 0;

  String path = loraOtaFilePath(ota.slot);
  ota.file = LittleFS.open(path, "r");
  if (!ota.file) { ota.state = OTA_FAILED; strlcpy(ota.msg, "fichier firmware illisible", sizeof(ota.msg)); return; }
  ota.size = ota.file.size();
  ota.blockSize   = ota.reqBlockSize ? ota.reqBlockSize : OTA_BLOCK_SIZE;
  ota.totalBlocks = (uint16_t)((ota.size + ota.blockSize - 1) / ota.blockSize);

  uint32_t crc = 0xFFFFFFFFUL;
  uint8_t  buf[256];
  int n;
  while ((n = ota.file.read(buf, sizeof(buf))) > 0) {
    for (int i = 0; i < n; i++) {
      crc ^= buf[i];
      for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
    esp_task_wdt_reset();
  }
  ota.crc = ~crc;

  ota.txPreamble = ota.reqPreamble ? ota.reqPreamble : LORA_PREAMBLE;
  ota.txSF       = ota.reqSF ? ota.reqSF : g_sf;
  if (ota.txSF != g_sf) setSF(ota.txSF);            // diagnostic : toute la mise a jour sur ce SF
  LLOG("[LoRa] OTA : radio canal %d (%.0f MHz), SF%d, preambule %d symboles\r\n",
       g_opChannel, CH_FREQ[g_opChannel], ota.txSF, ota.txPreamble);
  LLOG("[LoRa] OTA : blocs de %d octets, fenetre demandee %d\r\n", ota.blockSize, ota.reqWindow);

  lastRadioRxMs = millis();        // l'emetteur en mode OTA n'emet plus ses trames habituelles
  LLOG("[LoRa] OTA slot %d : %lu octets, %u blocs, CRC32 0x%08lX, version 0x%04X\r\n",
       ota.slot, (unsigned long)ota.size, ota.totalBlocks, (unsigned long)ota.crc, ota.version);
}

// Emet la trame correspondant a l'etat courant (OTA_START, OTA_BLOCK du bloc ota.txBlock,
// OTA_END) et arme l'attente de l'ACK.
static void otaSendCurrent() {
  LoraEmitter &e = loraEmitters[ota.slot];
  uint8_t pkt[4 + OTA_BLOCK_SIZE + MIC_SIZE];
  int clearLen = 0;

  if (ota.state == OTA_WAITING) {                    // OTA_START, 15 octets (16 avec la fenetre)
    pkt[0] = 0x10 | T_OTA_START;
    pkt[1] = ota.seq;
    pkt[2] = (uint8_t)(ota.version >> 8);      pkt[3] = (uint8_t)(ota.version & 0xFF);
    pkt[4] = (uint8_t)(ota.size >> 24);        pkt[5] = (uint8_t)(ota.size >> 16);
    pkt[6] = (uint8_t)(ota.size >> 8);         pkt[7] = (uint8_t)(ota.size & 0xFF);
    pkt[8] = (uint8_t)(ota.crc >> 24);         pkt[9] = (uint8_t)(ota.crc >> 16);
    pkt[10] = (uint8_t)(ota.crc >> 8);         pkt[11] = (uint8_t)(ota.crc & 0xFF);
    pkt[12] = (uint8_t)(ota.totalBlocks >> 8); pkt[13] = (uint8_t)(ota.totalBlocks & 0xFF);
    pkt[14] = ota.blockSize;
    clearLen = 15;
    if (ota.reqWindow >= 2) { pkt[15] = ota.reqWindow; clearLen = 16; }   // fenetre d'acquittement
  } else if (ota.state == OTA_SENDING) {             // OTA_BLOCK, 4 + 64 octets en clair
    pkt[0] = 0x10 | T_OTA_BLOCK;
    pkt[1] = ota.seq;
    pkt[2] = (uint8_t)(ota.txBlock >> 8);
    pkt[3] = (uint8_t)(ota.txBlock & 0xFF);
    memset(&pkt[4], 0xFF, ota.blockSize);            // dernier bloc complete par des 0xFF
    ota.file.seek((uint32_t)ota.txBlock * ota.blockSize);
    ota.file.read(&pkt[4], ota.blockSize);
    clearLen = 4 + ota.blockSize;
  } else {                                           // OTA_END, 7 octets en clair
    pkt[0] = 0x10 | T_OTA_END;
    pkt[1] = ota.seq;
    pkt[2] = (uint8_t)(ota.crc >> 24);         pkt[3] = (uint8_t)(ota.crc >> 16);
    pkt[4] = (uint8_t)(ota.crc >> 8);          pkt[5] = (uint8_t)(ota.crc & 0xFF);
    pkt[6] = 0x00;
    clearLen = 7;
  }

  // Trace de la trame EN CLAIR (avant chiffrement) : a comparer avec ce que l'emetteur attend.
  bool trace = (ota.state != OTA_SENDING) || (ota.txBlock % 100 == 0) || (ota.tries > 0);
  char clearHex[2 * 16 + 1] = "";
  if (trace) {
    int n = clearLen < 16 ? clearLen : 16;
    for (int k = 0; k < n; k++) snprintf(&clearHex[k * 2], 3, "%02X", pkt[k]);
  }

  int total = encryptPacket(pkt, clearLen, e.key, e.mac);
  // L'emetteur vient peut-etre d'emettre son ACK : lui laisser le temps de repasser en reception.
  delay(20);
  radio.clearDio1Action();
  if (ota.txPreamble != LORA_PREAMBLE) radio.setPreambleLength(ota.txPreamble);
  uint32_t t0 = millis();
  int st = radio.transmit(pkt, total);               // bloquant (temps d'antenne)
  uint32_t airMs = millis() - t0;
  if (ota.txPreamble != LORA_PREAMBLE) radio.setPreambleLength(LORA_PREAMBLE);
  if (trace) {
    const char *nm = (ota.state == OTA_WAITING) ? "OTA_START" : (ota.state == OTA_SENDING) ? "OTA_BLOCK" : "OTA_END";
    LLOG("[LoRa] OTA : %s emis (essai %u, seq %u, %d octets, %lu ms, tx=%d) clair=%s%s\r\n",
         nm, ota.tries + 1, ota.seq, total, (unsigned long)airMs, st, clearHex, clearLen > 16 ? "..." : "");
  }
  rxFlag = false;
  radio.setDio1Action(onLoraRx);
  radio.startReceive();
  ota.tries++;
  ota.ackGot      = false;
  ota.waitingAck  = true;
  ota.ackDeadline = millis() + OTA_ACK_TIMEOUT_MS;
  if (st != RADIOLIB_ERR_NONE) LLOG("[LoRa] OTA : emission en echec (%d)\r\n", st);
}

// OTA_ACK recu (type 0x09, chiffre) : [seq][index(2)][statut][fenetre][reserve]. Appele par la
// boucle de reception. Un ACK peut arriver a tout moment (ex. NOT_STARTED) : toujours memorise.
static void handleOtaAck(uint8_t *buf, int len) {
  if (!loraOtaActive() || ota.requested) return;
  LoraEmitter &e = loraEmitters[ota.slot];
  if (!decryptPacket(buf, len, e.key, e.mac)) return;      // pas pour nous / pas de cet emetteur
  int clearLen = len - MIC_SIZE;
  if (clearLen < 5) return;
  ota.ackBlock  = getU16(&buf[2]);
  ota.ackStatus = buf[4];
  ota.ackWindow = (clearLen >= 6) ? buf[5] : 0;
  ota.ackGot    = true;
  if (ota.state != OTA_SENDING || ota.ackStatus != OTA_STATUS_OK)
    LLOG("[LoRa] OTA : OTA_ACK recu (index 0x%04X, statut 0x%02X %s, fenetre %u)\r\n",
         ota.ackBlock, ota.ackStatus, otaStatusText(ota.ackStatus), ota.ackWindow);
  lastRadioRxMs = millis();
  e.lastSeenMs  = millis();
}

// Trame acceptee : nouveau seq, compteur d'essais remis a zero.
static void otaAdvance() {
  ota.seq++;
  ota.tries      = 0;
  ota.waitingAck = false;
}

// Essai sans succes de la trame en cours (pas de reponse, ou reponse negative).
static void otaRetryOrFail(const char *why) {
  ota.waitingAck = false;
  uint16_t maxTries = (ota.state == OTA_WAITING) ? OTA_START_TRIES : OTA_MAX_RETRIES;
  if (ota.tries >= maxTries) {
    char m[112];
    if (ota.state == OTA_WAITING)
      snprintf(m, sizeof(m), "l'emetteur ne repond pas : est-il en mode mise a jour ? (%s)", why);
    else if (ota.state == OTA_SENDING)
      snprintf(m, sizeof(m), "bloc %u abandonne apres %u essais (%s)", ota.block, ota.tries, why);
    else
      snprintf(m, sizeof(m), "fin de mise a jour refusee (%s)", why);
    otaFinish(OTA_FAILED, m);
    return;
  }
  if (ota.state != OTA_WAITING) ota.totalRetries++;
}

// L'emetteur repond NOT_STARTED : il n'est plus en reception (redemarrage en cours de
// transfert, OTA_START jamais accepte). On reprend a OTA_START, depuis le bloc 0.
static void otaRestart() {
  if (++ota.restarts > OTA_MAX_RESTARTS) {
    otaFinish(OTA_FAILED, "l'emetteur a quitte la mise a jour a plusieurs reprises");
    return;
  }
  LLOG("[LoRa] OTA : l'emetteur n'est plus en reception -> reprise a OTA_START (%u/%u)\r\n",
       ota.restarts, OTA_MAX_RESTARTS);
  ota.state = OTA_WAITING;
  ota.block = 0; ota.txBlock = 0;
  ota.seq++;
  ota.tries = 0; ota.waitingAck = false; ota.ackGot = false;
  ota.window = 1; ota.winWaiting = false; ota.winFails = 0;
}

// Planifie l'emission de la fenetre qui commence au bloc `next` (prochain attendu par l'emetteur).
static void otaWindowPlan(uint16_t next) {
  uint32_t end = ((uint32_t)(next / ota.window) + 1) * ota.window - 1;   // fin de fenetre
  if (end > (uint32_t)(ota.totalBlocks - 1)) end = ota.totalBlocks - 1;  // ou dernier bloc
  ota.winEnd     = (uint16_t)end;
  ota.winNext    = next;
  ota.winWaiting = false;
  ota.nextTxMs   = millis() + OTA_BLOCK_GAP_MS;
}

// Envoi des blocs avec acquittement par fenetre (protocole §10, "Acquittement par fenetre").
static void otaWindowTick() {
  // --- un OTA_ACK est arrive (normalement en fin de fenetre, mais possible a tout moment) ---
  if (ota.ackGot) {
    ota.ackGot = false;
    if (ota.ackStatus == OTA_STATUS_NOT_STARTED) { otaRestart(); return; }
    if (ota.ackStatus == OTA_STATUS_BUSY) {
      // Emetteur occupe : on patiente puis on resollicite un ACK avec le bloc de fin de fenetre.
      if (++ota.winFails > OTA_WIN_MAX_FAILS) { otaFinish(OTA_FAILED, "l'emetteur reste occupe"); return; }
      ota.winNext = ota.winEnd; ota.winWaiting = false; ota.nextTxMs = millis() + 500;
      return;
    }
    if (ota.ackStatus != OTA_STATUS_OK) {
      char m[112];
      snprintf(m, sizeof(m), "bloc refuse par l'emetteur : %s", otaStatusText(ota.ackStatus));
      otaFinish(OTA_FAILED, m);
      return;
    }
    uint16_t next = ota.ackBlock;                    // prochain bloc attendu par l'emetteur
    if (next > ota.totalBlocks) next = ota.totalBlocks;
    if (next > ota.block) {                          // progression
      ota.block    = next;
      ota.winFails = 0;
    } else {                                         // aucun bloc de plus : fenetre a refaire
      ota.totalRetries++;
      if (++ota.winFails > OTA_WIN_MAX_FAILS) {
        char m[112];
        snprintf(m, sizeof(m), "aucune progression au bloc %u apres %u essais", ota.block, ota.winFails - 1);
        otaFinish(OTA_FAILED, m);
        return;
      }
      LLOG("[LoRa] OTA : fenetre a reprendre au bloc %u (essai %u)\r\n", next, ota.winFails);
    }
    if (ota.block >= ota.totalBlocks) {              // tous les blocs sont recus
      ota.state = OTA_ENDING;
      ota.tries = 0; ota.waitingAck = false;
      return;
    }
    otaWindowPlan(ota.block);                        // fenetre suivante, ou reprise au bloc perdu
    return;
  }

  // --- attente de l'ACK de fin de fenetre : aucune emission pendant ce temps ---
  if (ota.winWaiting) {
    if ((int32_t)(millis() - ota.ackDeadline) < 0) return;
    // Pas d'ACK : l'emetteur a peut-etre tout recu et c'est l'ACK qui s'est perdu. On ne renvoie
    // QUE le bloc de fin de fenetre, qui sollicite un nouvel ACK.
    ota.totalRetries++;
    if (++ota.winFails > OTA_WIN_MAX_FAILS) {
      char m[112];
      snprintf(m, sizeof(m), "pas de reponse a la fenetre du bloc %u apres %u essais", ota.block, ota.winFails - 1);
      otaFinish(OTA_FAILED, m);
      return;
    }
    LLOG("[LoRa] OTA : pas d'ACK pour la fenetre %u..%u -> renvoi du bloc %u (essai %u)\r\n",
         ota.block, ota.winEnd, ota.winEnd, ota.winFails);
    ota.winNext    = ota.winEnd;
    ota.winWaiting = false;
    ota.nextTxMs   = millis();
    return;
  }

  // --- emission des blocs de la fenetre, espaces d'au moins OTA_BLOCK_GAP_MS ---
  if ((int32_t)(millis() - ota.nextTxMs) < 0) return;
  ota.txBlock = ota.winNext;
  ota.tries   = 0;
  otaSendCurrent();
  ota.seq++;                                         // un seq par trame emise (nonce)
  ota.tries      = 0;
  ota.waitingAck = false;
  if (ota.winNext >= ota.winEnd) {                   // bloc de fin de fenetre : place a l'ecoute
    ota.winWaiting  = true;
    ota.ackDeadline = millis() + OTA_WIN_ACK_TIMEOUT_MS;
  } else {
    ota.winNext++;
    ota.nextTxMs = millis() + OTA_BLOCK_GAP_MS;
  }
}

// A appeler a chaque passage de loraReceiverLoop().
static void loraOtaTick() {
  if (ota.requested) otaBegin();
  if (ota.state != OTA_WAITING && ota.state != OTA_SENDING && ota.state != OTA_ENDING) return;

  if (ota.cancelRequested) {
    ota.cancelRequested = false;
    otaFinish(OTA_FAILED, "mise a jour annulee");
    return;
  }

  if (ota.state == OTA_SENDING && ota.window >= 2) { otaWindowTick(); return; }

  if (!ota.waitingAck) {
    ota.txBlock = ota.block;                         // mode sans fenetre : un bloc, un ACK
    otaSendCurrent();
    return;
  }

  if (!ota.ackGot) {
    if ((int32_t)(millis() - ota.ackDeadline) >= 0) otaRetryOrFail("pas de reponse");
    return;
  }

  // --- un OTA_ACK est arrive ---
  ota.ackGot = false;
  if (ota.state == OTA_WAITING) {
    if (ota.ackStatus == OTA_STATUS_OK) {
      otaAdvance();
      ota.state   = OTA_SENDING;
      if (!ota.startMs) ota.startMs = millis();
      // Fenetre RETENUE par l'emetteur (il la borne) : c'est elle qui fait foi. 0 = firmware
      // qui ignore la fenetre -> un ACK par bloc, portant l'index du bloc acquitte.
      ota.window = (ota.reqWindow >= 2 && ota.ackWindow >= 2 && ota.ackWindow <= OTA_WINDOW_MAX)
                   ? ota.ackWindow : 1;
      ota.winFails = 0;
      if (ota.window >= 2) otaWindowPlan(0);
      LLOG("[LoRa] OTA : emetteur pret, envoi de %u blocs, %s (fenetre %u)\r\n", ota.totalBlocks,
           ota.window >= 2 ? "un ACK par fenetre" : "un ACK par bloc", ota.window);
    } else {
      // Refus explicite (firmware trop gros, occupe...) : inutile d'insister.
      char m[112];
      snprintf(m, sizeof(m), "mise a jour refusee par l'emetteur : %s", otaStatusText(ota.ackStatus));
      otaFinish(OTA_FAILED, m);
    }
  } else if (ota.state == OTA_SENDING) {             // mode sans fenetre
    if (ota.ackStatus == OTA_STATUS_NOT_STARTED) { otaRestart(); return; }
    if (ota.ackStatus == OTA_STATUS_OK && ota.ackBlock == ota.block) {
      otaAdvance();
      ota.block++;
    } else if (ota.ackStatus == OTA_STATUS_OUT_OF_RANGE && ota.ackBlock > ota.block
               && ota.ackBlock < ota.totalBlocks) {
      // L'emetteur a deja ce bloc (son ACK s'etait perdu) : on reprend a celui qu'il attend.
      LLOG("[LoRa] OTA : emetteur en avance (bloc %u attendu, %u envoye)\r\n", ota.ackBlock, ota.block);
      otaAdvance();
      ota.block = ota.ackBlock;
    } else {
      otaRetryOrFail(otaStatusText(ota.ackStatus));
      return;
    }
    if (ota.block >= ota.totalBlocks) ota.state = OTA_ENDING;
  } else {                                           // OTA_ENDING
    if (ota.ackStatus == OTA_STATUS_OK) otaFinish(OTA_DONE, "mise a jour terminee, l'emetteur redemarre");
    else if (ota.ackStatus == OTA_STATUS_NOT_STARTED) otaRestart();
    else {
      char m[112];
      snprintf(m, sizeof(m), "firmware rejete par l'emetteur : %s", otaStatusText(ota.ackStatus));
      otaFinish(OTA_FAILED, m);
    }
  }
}

String loraOtaStatusJson(int slot) {
  static const char *names[] = {"idle", "waiting", "sending", "ending", "done", "error"};
  uint8_t st = ota.requested ? (uint8_t)OTA_WAITING : ota.state;
  String j = "{\"state\":\"";
  j += names[st <= OTA_FAILED ? st : 0];
  j += "\",\"slot\":" + String(st == OTA_IDLE ? -1 : (ota.requested ? ota.reqSlot : ota.slot));
  j += ",\"block\":" + String(ota.block);
  j += ",\"total\":" + String(ota.totalBlocks);
  j += ",\"pct\":" + String(ota.totalBlocks ? (int)((100UL * ota.block) / ota.totalBlocks) : 0);
  j += ",\"tries\":" + String(ota.tries);
  j += ",\"retries\":" + String(ota.totalRetries);
  j += ",\"window\":" + String(ota.window);
  uint32_t ref = (st == OTA_DONE || st == OTA_FAILED) ? ota.endMs : millis();
  uint32_t elapsed = ota.startMs ? (ref - ota.startMs) / 1000 : 0;
  j += ",\"elapsed\":" + String(elapsed);
  long eta = -1;
  if (st == OTA_SENDING && ota.block > 0 && elapsed > 0)
    eta = (long)(((uint64_t)(ota.totalBlocks - ota.block) * elapsed) / ota.block);
  j += ",\"eta\":" + String(eta);
  // Anciennete du resultat (s), pour qu'un "termine" ou un "echec" ne reste pas affiche
  // indefiniment sur la fiche de l'appareil. -1 tant que la mise a jour n'est pas finie.
  j += ",\"since\":" + String((st == OTA_DONE || st == OTA_FAILED) ? (long)((millis() - ota.endMs) / 1000) : -1L);
  String m = ota.msg;
  m.replace("\\", "/"); m.replace("\"", "'");
  j += ",\"msg\":\"" + m + "\"";
  size_t fsz = 0;
  String path = loraOtaFilePath(slot);
  if (path.length()) { File f = LittleFS.open(path, "r"); if (f) { fsz = f.size(); f.close(); } }
  j += ",\"file\":" + String((unsigned long)fsz);
  j += "}";
  return j;
}

/* ===================== API ===================== */
bool loraReceiverBegin() {
  if (!loraDetected) return false;      // detectLoRa() n'a rien vu : pas de radio à init
  loadLoraConfig();

  // Le bus SPI (loraSpi) est déjà ouvert par detectLoRa() : ne pas le ré-initialiser.
  // Config identique à l'émetteur : BW 406.25, SF11, CR4/5, syncword 0x12, préambule 8
  int st = radio.begin(CH_FREQ[g_opChannel], 406.25, g_sf, 5, 0x12, 10, LORA_PREAMBLE);
  if (st != RADIOLIB_ERR_NONE) { LLOG("[LoRa] radio.begin()=%d\r\n", st); return false; }
  radio.setCRC(2);
  radio.explicitHeader();
  radio.invertIQ(false);
  radio.setDio1Action(onLoraRx);
  radio.startReceive();
  curChannel = g_opChannel;
  radioSF    = g_sf;         // aligner le SF suivi sur celui passe a radio.begin()
  radioReady = true;
  // Armer le chien de garde dès maintenant : sans ça, une radio qui n'entre jamais en RX au
  // boot ne serait jamais ré-armée (le watchdog attend une 1re trame pour se déclencher).
  lastRadioRxMs = millis();

  // Les émetteurs déjà appairés doivent réapparaître comme appareils au boot.
  // Une identification interrompue (reboot avant les réponses) est relancée.
  for (int i = 0; i < LORA_MAX_EMITTERS; i++)
    if (loraEmitters[i].valid) {
      loraEmitters[i].identStep   = (loraEmitters[i].deviceId[0] && loraEmitters[i].model[0]) ? 0
                                  : (loraEmitters[i].deviceId[0] ? 2 : 1);
      loraEmitters[i].identNextMs = millis();
      loraEmitters[i].verStep     = 1;           // version relue a chaque demarrage
      loraEmitters[i].verNextMs   = millis();
      ensureLoraDevice(loraEmitters[i].mac, loraEmitters[i].deviceId, loraEmitters[i].model);
    }

  LLOG("[LoRa] recepteur pret (canal %d, SF%d, %d emetteur(s))\r\n", g_opChannel, g_sf, loraCountEmitters());
  return true;
}

/* Chien de garde de la réception.
 *
 * Un émetteur appairé parle toutes les 5 s : un silence prolongé n'est pas un creux de
 * trafic, c'est une radio sortie du mode réception. On ré-arme après chaque trame via
 * radio.startReceive(), mais sur ce montage startReceive() n'entre pas toujours réellement
 * en RX (c'est ce qui avait imposé un SetRx brut pendant le bring-up) : un seul échec
 * silencieux et la réception est morte jusqu'au prochain reboot.
 *
 * On ne peut pas distinguer « radio muette » de « émetteur éteint » — mais ré-armer est sans
 * effet de bord si tout va bien, alors qu'un stall dure indéfiniment. On le loggue pour
 * savoir lequel des deux on a.
 */
static uint32_t lastWatchdogRearmMs = 0;

static void rxWatchdog() {
  if (loraPairingMode || awaitingConfirm) return;   // séquences qui pilotent déjà la radio
  if (loraOtaActive()) return;                      // idem : la mise a jour OTA gere sa reception
  if (loraCountEmitters() == 0) return;             // rien à écouter
  if (lastRadioRxMs == 0) return;                   // aucune trame depuis le boot

  uint32_t now = millis();
  if (now - lastRadioRxMs < 60000) return;          // reception nominale

  // Silence prolonge : la radio est probablement sortie du mode RX (cf. bring-up : startReceive()
  // n'entre pas toujours reellement en RX). On la re-arme sur la config operationnelle (le SF et
  // le canal sont autoritaires depuis l'appairage/config, on ne balaie PAS : un balayage
  // adopterait le SF d'un emetteur pas encore re-appaire et annulerait un changement voulu).
  if (now - lastWatchdogRearmMs < 15000) return;    // ne pas re-armer en rafale
  lastWatchdogRearmMs = now;
  setChannel(g_opChannel);
  setSF(g_sf);
  radio.startReceive();
  LLOG("[LoRa] silence %lu s -> re-armement RX (canal %d, SF%d)\r\n",
       (unsigned long)((now - lastRadioRxMs) / 1000), g_opChannel, g_sf);
}

void loraReceiverLoop() {
  if (!radioReady) return;

  // Demande d'appairage venue d'un handler web : on la traite ICI (dans loop()), avant tout
  // le reste, pour être le seul contexte à piloter la radio. Sinon rxWatchdog() ci-dessous
  // pourrait ré-armer le canal de données juste après que le web ait armé le canal d'appairage.
  if (loraPairingRequested) {
    loraPairingRequested = false;
    if (loraOtaActive()) LLOG("[LoRa] appairage refuse : mise a jour OTA en cours\r\n");
    else                 loraStartPairingNow();
  }

  // Mise a jour OTA d'un emetteur : emission de la trame suivante / gestion des delais.
  // Jamais pendant un appairage, qui deplace la radio sur le canal de rendez-vous.
  if (!loraPairingMode && !awaitingConfirm) loraOtaTick();

  rxWatchdog();

  // Re-armement periodique de la RX pendant la fenetre d'appairage. Sur ce montage
  // startReceive() n'entre pas toujours reellement en RX (cf. rxWatchdog) : pour les donnees le
  // watchdog rattrape au bout de 60 s, mais la fenetre d'appairage ne dure que 30 s et aucun
  // trafic ne permet de detecter l'echec -> sans ca, un armement rate = fenetre entierement
  // morte (symptome : le device emet ses PAIR_REQUEST, le recepteur n'en voit aucun). On re-arme
  // toutes les 2 s tant qu'on attend un PAIR_REQUEST (pas pendant l'attente du CONFIRM, qui a sa
  // propre fenetre courte). Le device emet toutes les 300 ms : un re-arme reussi capte vite.
  static uint32_t lastPairRearmMs = 0;
  if (loraPairingMode && !awaitingConfirm && !rxFlag && millis() - lastPairRearmMs > 2000) {
    lastPairRearmMs = millis();
    setChannel(LORA_PAIR_CHANNEL);
    setSF(LORA_PAIR_SF);
    radio.setDio1Action(onLoraRx);
    radio.startReceive();
  }

  // LED d'appairage : clignote tant que la fenêtre est ouverte, éteinte sinon.
  if (loraPairingMode) pairingLedTick(); else if (ledState) pairingLedOff();

  // Fin de la fenêtre d'appairage -> retour à l'écoute des données sur la config operationnelle
  // (le rendez-vous forcait canal 3 + SF11, il faut restaurer canal/SF op).
  if (loraPairingMode && (millis() - loraPairingStartMs > LORA_PAIR_WINDOW_MS)) {
    loraPairingMode = false; awaitingConfirm = false;
    pairingLedOff();
    setChannel(g_opChannel); setSF(g_sf); radio.startReceive();
    LLOG("[LoRa] fenetre d'appairage fermee\r\n");
  }
  // Pas de CONFIRM : on retourne écouter les PAIR_REQUEST sur le rendez-vous (canal 3, SF11).
  if (awaitingConfirm && millis() > confirmDeadline) {
    awaitingConfirm = false;
    LLOG("[LoRa] pas de PAIR_CONFIRM recu (timeout)\r\n");
    if (loraPairingMode) { setChannel(LORA_PAIR_CHANNEL); setSF(LORA_PAIR_SF); radio.startReceive(); }
  }

  if (!rxFlag) return;
  rxFlag = false;
  // lastRadioRxMs n'est PAS mis a jour ici : DIO1 declenche aussi sur du bruit (MIC KO), ce
  // qui arreterait le balayage SF a tort. Il l'est dans handleData, sur trame VALIDE seulement.

  uint8_t buf[64];
  int len = radio.getPacketLength();
  if (len <= 0 || len > (int)sizeof(buf)) { radio.startReceive(); return; }
  if (radio.readData(buf, len) != RADIOLIB_ERR_NONE) { radio.startReceive(); return; }

  float rssi = radio.getRSSI(), snr = radio.getSNR();
  switch (buf[0] & 0x0F) {
    case T_PAIR_REQUEST: handlePairRequest(buf, len); break;   // gère son propre re-arm
    case T_PAIR_CONFIRM: handlePairConfirm(buf, len); radio.startReceive(); break;
    case T_ESSENTIAL:
    case T_EXTENDED:      handleData(buf, len, rssi, snr); radio.startReceive(); break;
    case T_POLL_RESPONSE: handlePollResponse(buf, len); radio.startReceive(); break;
    case T_OTA_ACK:       handleOtaAck(buf, len); radio.startReceive(); break;
    default:              radio.startReceive(); break;
  }
}
