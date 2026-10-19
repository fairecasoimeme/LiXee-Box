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
 * Protocole complet : voir doc/LORA_PROTOCOL.md
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
  // Mode Linky (LORA_PROTOCOL.md §6.1) : champ pivot du protocole. Il n'est porté que par
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

  // --- Lecture d'attribut a la demande (POLL, LORA_PROTOCOL.md §8) ------------------------
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

  // --- Identification de l'objet par POLL (LORA_PROTOCOL.md §8, cluster Basic 0x0000) ----
  // Le PAIR_REQUEST (11 octets) ne dit pas quel objet s'appaire : apres l'appairage, la box
  // lit 0x0000/0xFF00 (device_id, nomme data/tp/<device_id>.json) puis 0x0000/0x0005 (model,
  // la cle dans ce fichier). Tant que ce n'est pas fait, deviceId/model sont vides et l'appareil
  // n'a pas de template. RAM seulement : au boot, une identite incomplete relance la lecture.
  uint8_t  identStep;        // 0 = identifie, 1 = attend le device_id, 2 = attend le model
  uint32_t identNextMs;      // prochaine tentative (apres un abandon, on espace les essais)

  // --- Version du firmware de l'emetteur (cluster Basic, PROTOCOLE §8) ----------------------
  // 0x0000/0xFF01 : version sur 16 bits, format 0xMMmp (1.2.3 -> 0x0123), le meme que le champ
  // version d'OTA_START. 0x0000/0x4000 : la meme en clair, avec le commit du build. Lue une
  // fois l'objet identifie, a chaque demarrage de la box, et apres une mise a jour OTA.
  uint8_t  verStep;          // 0 = rien a lire, 1 = attend 0xFF01, 2 = attend 0x4000
  uint32_t verNextMs;
  uint16_t fwVersion;        // valable si fwKnown
  bool     fwKnown;
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

// --- Mise a jour du firmware d'un emetteur par LoRa (OTA, doc/LORA_PROTOCOL.md §10) ------------
// Le fichier .bin doit etre dans /ota/lora_<MAC>.ota (depose par la page /loraOta). L'emetteur
// doit etre en mode "OTA recovery" : bouton maintenu pendant la mise sous tension.
// loraOtaStart() ne touche pas la radio : il pose une demande, traitee dans loraReceiverLoop().
// Renvoie false et renseigne `err` si la demande est refusee.
// `preamble` et `sf` : reglages de DIAGNOSTIC, 0 = ceux de la box (preambule 8, SF operationnel).
// Le mode OTA de l'emetteur peut ecouter avec d'autres parametres que son fonctionnement normal.
// `blockSize` : octets de firmware par bloc (8..64), 0 = 64. Annonce dans l'OTA_START.
// `window` : fenetre d'acquittement demandee (protocole §10). 0 = 16 (recommande), 1 = un ACK
// par bloc, 2..32 sinon. L'emetteur peut la reduire : c'est la valeur de son ACK qui fait foi.
bool   loraOtaStart(int slot, uint16_t version, String &err, uint8_t preamble = 0, uint8_t sf = 0,
                    uint8_t blockSize = 0, uint8_t window = 0);
void   loraOtaCancel();
bool   loraOtaActive();
// Etat pour l'UI : {"state":"idle|waiting|sending|ending|done|error","slot":..,"block":..,
//                   "total":..,"pct":..,"retries":..,"elapsed":..,"eta":..,"msg":"..","file":..}
// `slot` >= 0 : ajoute la taille du fichier deja depose pour cet emetteur (0 = aucun).
String loraOtaStatusJson(int slot);
// Chemin du fichier firmware d'un emetteur ("" si slot invalide).
String loraOtaFilePath(int slot);

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
