#pragma once

// ============================================================================
// LoRa 2.4 GHz (SX1281) - recepteur ZLinky LoRa
//
// Portage de loraModule.cpp + loraReceiver.cpp de la LiXee-Box.
//
// Principe : une fois appaire, l'emetteur (ZLinky LoRa) envoie ses trames TIC
// chiffrees (AES-128-CTR + MIC CMAC). Elles sont dechiffrees puis chaque champ
// est range dans device_data sur les MEMES clusters/attributs qu'un ZLinky
// Zigbee (0702/0B04/0B01/FF66) : l'appareil LoRa est un appareil normal pour
// les pages Energie, l'historique et MQTT.
//
// Identification d'un emetteur : sa MAC LoRa fait 8 octets (16 caracteres hex
// sur la LiXee-Box, cle "mac" de lora.json et nom /db/<MAC>.json). Elle est
// reprise TELLE QUELLE comme cle 64 bits de device_data (octet 0 = poids
// fort) : /littlefs/db/<MAC>.json garde le meme nom que sur la Box, et
// "%016llX" de la cle redonne la MAC. Le template est choisi comme en Zigbee :
// /littlefs/tp/<deviceId>.json, cle <model> (identite lue par POLL du cluster
// Basic apres l'appairage, ZLinky "81"/"ZLinky_TIC" par defaut).
//
// Persistance : /littlefs/config/lora.json, format identique a la LiXee-Box :
//   {"opChannel":4,"sf":11,"emitters":[{"mac":"..","deviceId":"81",
//    "model":"ZLinky_TIC","key":"<32 hex>"}]}
//
// Fils d'execution : la radio n'est pilotee que par la tache "lora". Les
// fonctions ci-dessous (appelees par les routes web) ne font que poser des
// demandes ou lire un etat, sous verrou.
// ============================================================================

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LORA_MAX_EMITTERS     4
#define LORA_PAIR_WINDOW_S    30
#define LORA_CHANNEL_COUNT    8
#define LORA_CONFIG_PATH      "/littlefs/config/lora.json"
#define LORA_OTA_DIR          "/littlefs/ota"
#define LORA_OTA_MAX_FILE     (1024u * 1024u)

// ----------------------------------------------------------------------------
// Demarrage / detection
// ----------------------------------------------------------------------------

/**
 * Detecte le module, charge lora.json, (re)cree les appareils deja appaires
 * dans device_data et demarre la tache radio. A appeler une fois, apres
 * device_data_load_all() et cluster_handlers_init().
 * @return ESP_OK si la radio tourne, ESP_ERR_NOT_FOUND sans module (echec
 *         propre : bus SPI2 et broches relaches), autre code si la radio ne
 *         s'initialise pas.
 */
esp_err_t lora_init(void);

/** true si un SX128x a repondu au demarrage (modules.lora de /api/system/info). */
bool lora_is_detected(void);

/** true si la radio est configuree et la tache demarree. */
bool lora_is_ready(void);

/** Version lue dans le registre 0x0153 du SX128x (0 si absent). */
uint16_t lora_chip_version(void);

// ----------------------------------------------------------------------------
// Emetteurs appaires
// ----------------------------------------------------------------------------

typedef struct {
    int      slot;
    uint64_t ieee;              // MAC LoRa = cle device_data
    char     device_id[8];      // nomme le template (vide tant que non identifie)
    char     model[24];
    bool     identified;
    bool     mode_known;
    uint8_t  linky_mode;        // bit0 Standard, bit1 triphase, bit2 production
    bool     fw_known;
    uint16_t fw_version;        // 0xMMmp
    char     version[32];       // texte affiche ("1.2.3", build, "< 1.0.0", "")
    uint32_t rx_count;
    uint32_t missed;
    float    rssi;
    float    snr;
    int32_t  age_s;             // -1 : jamais recu depuis le demarrage
    uint32_t last_seen;         // epoch (0 si inconnue)
    uint16_t short_addr;        // adresse 16 bits synthetique (affichage)
    uint16_t matter_ep;         // endpoint Matter attribue (0 = aucun)
} lora_emitter_info_t;

int  lora_count_emitters(void);
int  lora_get_emitters(lora_emitter_info_t *out, int max);
bool lora_get_emitter(int slot, lora_emitter_info_t *out);
/** Slot de l'emetteur de cette cle, ou -1. */
int  lora_find_slot_by_ieee(uint64_t ieee);
/** true si la cle device_data appartient a un emetteur LoRa (filtres Zigbee). */
bool lora_is_lora_device(uint64_t ieee);

/**
 * Supprime l'appairage (et, si demande, la fiche device_data). Remplace
 * l'enchainement /loraRemove + /deleteDevice de la LiXee-Box.
 */
esp_err_t lora_remove_emitter(int slot, bool remove_device_data);

// ----------------------------------------------------------------------------
// Appairage
// ----------------------------------------------------------------------------

/** Ouvre la fenetre d'appairage (30 s). Traite par la tache radio. */
esp_err_t lora_start_pairing(void);
/** true si la fenetre est ouverte (ou demandee) ; secondes restantes. */
bool lora_pairing_state(int *seconds_left);
/** Dernier appairage reussi (livre une seule fois, a un seul lecteur). */
bool lora_take_pair_result(uint64_t *ieee, char *model, size_t model_len);

// ----------------------------------------------------------------------------
// Parametres reseau (assignes aux emetteurs a l'appairage)
// ----------------------------------------------------------------------------

esp_err_t lora_set_op_params(uint8_t channel, uint8_t sf);
uint8_t   lora_get_channel(void);
uint8_t   lora_get_sf(void);
uint16_t  lora_channel_mhz(uint8_t channel);

// ----------------------------------------------------------------------------
// Lecture d'attribut a la demande (POLL, LORA_PROTOCOL.md sect. 8)
// ----------------------------------------------------------------------------

esp_err_t lora_queue_poll(int slot, uint16_t cluster, uint16_t attr);
/** {"state":"idle|pending|ok|unknown|bad",...} ; a liberer par free(). */
char *lora_poll_status_json(int slot);

// ----------------------------------------------------------------------------
// Mise a jour du firmware d'un emetteur (OTA, LORA_PROTOCOL.md sect. 10)
// Fichier : /littlefs/ota/lora_<MAC>.ota
// ----------------------------------------------------------------------------

/**
 * Pose une demande de mise a jour (traitee par la tache radio).
 * preamble/sf/block_size : reglages de diagnostic, 0 = ceux de la box.
 * window : fenetre d'acquittement, 0 = 16, 1 = un ACK par bloc.
 */
esp_err_t lora_ota_start(int slot, uint16_t version, uint8_t preamble, uint8_t sf,
                         uint8_t block_size, uint8_t window, char *err, size_t err_len);
void lora_ota_cancel(void);
bool lora_ota_active(void);
/** Etat de la mise a jour en JSON (cf. LiXee-Box) ; a liberer par free(). */
char *lora_ota_status_json(int slot);
/** Chemin du fichier firmware d'un emetteur appaire. */
bool lora_ota_file_path(int slot, char *out, size_t len);
/** Chemin pour un identifiant "lora_<MAC>" (emetteur appaire uniquement). */
bool lora_ota_path_for_id(const char *id, char *out, size_t len);

// ----------------------------------------------------------------------------
// Integration avec le reste du pont
// ----------------------------------------------------------------------------

/** Valeur decodee d'une trame LoRa, au format d'un rapport d'attribut ZCL. */
typedef struct {
    uint64_t       ieee_addr;
    uint16_t       short_addr;  // synthetique
    uint8_t        endpoint;    // toujours 1
    uint16_t       cluster_id;
    uint16_t       attr_id;
    uint8_t        attr_type;   // 0x20/0x21/0x23 numerique, 0x42 chaine
    const uint8_t *data;        // big-endian / ASCII sans octet de longueur
    uint16_t       data_len;
    const char    *hex_value;   // valeur deja rangee dans device_data
} lora_report_t;

typedef void (*lora_report_cb_t)(const lora_report_t *report);

/**
 * Chaque valeur decodee est TOUJOURS rangee dans device_data (hex, comme le
 * Zigbee). Sans callback, le composant fait ensuite lui-meme le reste du
 * chemin Zigbee de main.cpp : cluster_handler_dispatch(), historique
 * d'energie (0702), publication MQTT. Avec un callback, c'est lui qui fait ce
 * reste (pour partager exactement le code de on_zigbee_attribute_report).
 * Appele depuis la tache LoRa.
 */
void lora_register_report_cb(lora_report_cb_t cb);

/**
 * Memorise l'endpoint Matter de l'appareil ponte d'un emetteur (lora.json),
 * pour le reprendre au demarrage suivant. Ecrit en flash : pile interne.
 */
esp_err_t lora_set_matter_endpoint(uint64_t ieee, uint16_t ep);

/** Appele apres la suppression d'un emetteur (endpoint Matter a retirer). */
typedef void (*lora_removed_cb_t)(uint64_t ieee);
void lora_register_removed_cb(lora_removed_cb_t cb);

/**
 * Etat du recepteur et des emetteurs (page configLora), JSON construit en
 * PSRAM ; a liberer par free(). NULL si memoire insuffisante ou verrou occupe.
 */
char *lora_status_json(void);

/**
 * Appareils LoRa au format de /api/zigbee/devices/full, plus "radio":"lora",
 * "lora":{...} (lien radio) et "states":[...] (etats du template qui ont une
 * valeur : nom, cluster, attr, coef, type, unit, value hex). A liberer par free().
 */
char *lora_devices_json(void);

struct cJSON;
/**
 * Ajoute a un tableau cJSON un objet par appareil LoRa, au format de
 * /api/zigbee/devices/full (+ "radio":"lora", "lora":{...}, "states":[...]),
 * pour fusionner les appareils LoRa dans la liste generale.
 */
void lora_add_devices_json(struct cJSON *array);

#ifdef __cplusplus
}
#endif
