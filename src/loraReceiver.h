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

  // --- Lecture d'attribut a la demande (POLL, PROTOCOLE_LORA.md §8) ------------------------
  // Le ZLinky ouvre une fenetre RX de 300 ms apres CHAQUE uplink. On y depose une requete de
  // lecture (cluster, attribut) : modele ZCL Read Attributes. Une lecture est idempotente,
  // donc ni compteur anti-rejeu ni confirmation ; on rejoue au prochain uplink jusqu'a la
  // reponse (bornee). La valeur recue est injectee dans le meme pipeline que la reception
  // normale : elle apparait sur la fiche de l'appareil et les pages Energie sans code dedie.
  // Le SF et le canal ne se changent PAS par ce canal : ils se negocient a l'appairage (§4).
  bool     pollPending;      // une requete de lecture attend d'etre transmise a cet emetteur
  uint16_t pollCluster;      // cluster ZCL demande (ex 0xFF66)
  uint16_t pollAttr;         // attribut ZCL demande
  uint8_t  pollSeq;          // seq des POLL_REQUEST (entre dans le nonce CTR)
  uint8_t  pollRetries;      // nb de tentatives depuis la mise en file
  // Derniere reponse recue (pour l'UI de lecture manuelle). Valide si pollRespMs != 0.
  uint32_t pollRespMs;
  uint8_t  pollRespStatus;   // 0x00 OK, 0x01 UNKNOWN_ATTR, 0x02 BAD_REQUEST
  uint8_t  pollRespType;     // type ZCL de la valeur renvoyee
  uint8_t  pollRespLen;      // longueur de la valeur
  uint8_t  pollRespValue[24];// valeur brute (big-endian pour les numeriques)
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

// Met une lecture d'attribut en file pour l'émetteur `slot` (POLL, §8). Elle sera transmise
// dans la fenêtre RX du prochain uplink et rejouée jusqu'à la réponse. La valeur reçue est
// injectée dans le pipeline habituel (readZigbeeDatas). Renvoie false si le slot est invalide.
bool loraQueuePoll(int slot, uint16_t cluster, uint16_t attr);

// État de la dernière lecture d'attribut de l'émetteur `slot`, en JSON, pour l'UI :
//   {"state":"idle|pending|ok|unknown|bad","cluster":..,"attr":..,"type":..,"value":".."}
String loraPollStatusJson(int slot);

// Définit le canal (0..7) et le SF (7..12) OPÉRATIONNELS assignés aux émetteurs À
// L'APPAIRAGE (§4). Le récepteur bascule immédiatement sa radio dessus ; les émetteurs déjà
// appairés restent sur leur config jusqu'à un nouvel appairage (appui long) et sont muets
// d'ici là — c'est un paramètre RÉSEAU, pas par appareil. Persiste. false si hors bornes.
bool    loraSetOpParams(uint8_t channel, uint8_t sf);
uint8_t loraGetChannel();
uint8_t loraGetSF();

int  loraCountEmitters();

// Recupere le resultat du dernier appairage reussi (MAC, model) et l'efface. false si aucun
// appairage en attente. Canal DEDIE a l'assistant d'appairage LoRa : contrairement a
// l'alerte partagee (/getAlert), rien d'autre ne le consomme.
bool loraTakePairResult(String &mac, String &model);

// Slot de l'émetteur appairé portant cette MAC (16 caractères hex), ou -1. Sert à
// reconnaître, parmi tous les `devices`, ceux qui arrivent par LoRa et à retrouver leurs
// stats de lien (RSSI/SNR/PDR) pour les afficher sur leur fiche.
int  loraFindEmitterByMac(const String &macHex);
bool loadLoraConfig();
bool saveLoraConfig();

#endif
