#ifndef LORA_RECEIVER_H
#define LORA_RECEIVER_H

#include <Arduino.h>

/*
 * Récepteur ZLinky LoRa 2.4 GHz (SX1281).
 *
 * Principe : une fois appairé, l'émetteur ZLinky envoie ses trames TIC chiffrées
 * (AES-128-CTR + MIC CMAC). On les déchiffre puis on injecte chaque champ dans
 * readZigbeeDatas() sur les MÊMES clusters/attributs qu'un ZLinky Zigbee
 * (0702/0B04/0B01/FF66). Le ZLinky LoRa devient donc un appareil normal : les pages
 * Énergie, les historiques, l'export CSV et MQTT fonctionnent sans code spécifique.
 *
 * Protocole complet : voir recepteur/PROTOCOLE_LORA.md
 */

#define LORA_MAX_EMITTERS   4
#define LORA_PAIR_CHANNEL   3     // 2440 MHz : handshake d'appairage
#define LORA_OP_CHANNEL     4     // 2450 MHz : canal de données imposé à l'émetteur
#define LORA_PAIR_WINDOW_MS 30000 // fenêtre d'appairage (comme le récepteur de référence)

struct LoraEmitter {
  uint8_t  mac[8];
  uint8_t  key[16];
  bool     valid;
  // Type annoncé par l'émetteur lors de l'appairage (cf. PAIR_REQUEST étendu) : ils
  // désignent le template, exactement comme pour un appareil Zigbee.
  //   deviceId -> nomme le fichier data/tp/<deviceId>.json
  //   model    -> la clé à l'intérieur de ce fichier
  // Ainsi un nouvel objet LoRa ne demande aucun code : juste son template.
  char     deviceId[8];
  char     model[24];
  // Mode Linky (PROTOCOLE_LORA.md §6.1) : champ pivot du protocole. Il n'est porté que par
  // DATA_ESSENTIAL (dernier octet) et POWER_MAX_CFG ; les autres EXTENDED ne l'ont pas, il
  // faut donc le mémoriser par émetteur pour interpréter leurs champs.
  //   bit0 : 0=Historique, 1=Standard   bit1 : triphasé   bit2 : production
  uint8_t  linkyMode;
  bool     modeKnown;      // false tant qu'aucun ESSENTIAL n'a été reçu
  // Code tarif unifié (§7.5.1), porté par POWER_MAX_CFG uniquement (~toutes les 140 s).
  // En Historique il donne l'option tarifaire (BASE/HCHP/EJP/Tempo) donc le sens des index.
  uint8_t  tariffCode;
  bool     tariffKnown;
  // true dès qu'un EXTENDED LABELS (sous-type 0x07) a fourni le vrai libellé du tarif.
  // On cesse alors de le déduire du code tarif : le libellé réel prime, et surtout les deux
  // sources écriraient en alternance sur le même attribut.
  bool     labelFromEmitter;
  // true dès qu'un EXTENDED METER_SERIAL (0x08) a été reçu : sert à ne loguer le numéro
  // de série qu'une fois, alors qu'il est réémis périodiquement.
  bool     serialFromEmitter;
  // Statistiques de lien (RAM uniquement)
  uint32_t rxCount;
  uint32_t missed;
  uint8_t  lastSeq;
  bool     seqInit;
  float    lastRssi;
  float    lastSnr;
  uint32_t lastSeenMs;
};

extern LoraEmitter loraEmitters[LORA_MAX_EMITTERS];
extern bool        loraPairingMode;     // true pendant la fenêtre d'appairage
extern uint32_t    loraPairingStartMs;

// Initialise le radio (si un module a été détecté par detectLoRa()) et charge les
// émetteurs appairés depuis /config/lora.json. À appeler une fois au boot.
bool loraReceiverBegin();

// À appeler régulièrement (tâche) : traite les trames reçues et la fenêtre d'appairage.
void loraReceiverLoop();

// Ouvre la fenêtre d'appairage (écoute des PAIR_REQUEST sur le canal 3).
void loraStartPairing();

// Supprime un émetteur appairé (slot 0..3) et persiste.
bool loraRemoveEmitter(int slot);

int  loraCountEmitters();

// Slot de l'émetteur appairé portant cette MAC (16 caractères hex), ou -1. Sert à
// reconnaître, parmi tous les `devices`, ceux qui arrivent par LoRa et à retrouver leurs
// stats de lien (RSSI/SNR/PDR) pour les afficher sur leur fiche.
int  loraFindEmitterByMac(const String &macHex);
bool loadLoraConfig();
bool saveLoraConfig();

#endif
