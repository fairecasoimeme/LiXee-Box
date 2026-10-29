#pragma once

// ============================================================================
// Device Data - Stockage des valeurs cluster/attribut par device Zigbee
//
// Equivalent de DeviceData Arduino (device.h/cpp) porte en C pour ESP-IDF.
//
// Architecture:
//   - Table de pointeurs en RAM (leger: 64 x 8 bytes = 512 bytes)
//   - Chaque device_data_t alloue en PSRAM a la demande
//   - Valeurs stockees comme strings hex (cluster -> attribut -> valeur)
//   - Persistence JSON dans /littlefs/db/<IEEE>.json
//   - Templates depuis /littlefs/tp/<device_id>.json
// ============================================================================

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Constantes
// ============================================================================

#define DEVDATA_MAX_DEVICES      64
#define DEVDATA_MAX_CLUSTERS     16   // Clusters par device (plupart < 10)
#define DEVDATA_MAX_ATTRS        24   // Attributs par cluster
#define DEVDATA_VALUE_LEN        72   // Valeurs hex : 35 octets (libelles TIC de 32 caracteres, noms Tuya)
#define DEVDATA_NAME_LEN         50
#define DEVDATA_UNIT_LEN         20
#define DEVDATA_MODE_LEN         20
#define DEVDATA_ALIAS_LEN        33
#define DEVDATA_MAX_POLL_ITEMS   32   // le template ZLinky en declare 21

#define DEVDATA_DB_DIR           "/littlefs/db"
#define DEVDATA_TP_DIR           "/littlefs/tp"

// ============================================================================
// Structures - Attribut/Cluster/Device
// ============================================================================

/** Valeur d'un attribut Zigbee (stockee en hex string) */
typedef struct {
    uint16_t attr_id;
    char     value[DEVDATA_VALUE_LEN];
    uint32_t last_update;      // secondes depuis le demarrage (esp_log_timestamp / 1000)
    uint8_t  type;             // type ZCL du dernier rapport, 0 = inconnu
} devdata_attr_t;

/** Cluster avec ses attributs */
typedef struct {
    uint16_t       cluster_id;
    devdata_attr_t attrs[DEVDATA_MAX_ATTRS];
    uint8_t        attr_count;
} devdata_cluster_t;

/** Element de poll list */
typedef struct {
    uint16_t cluster_id;
    uint16_t attr_id;
    uint32_t interval_ms;
    uint32_t last_poll;
    uint16_t manufacturer_code;
} devdata_poll_item_t;

/** Etat d'un template (metadonnees d'un attribut) */
typedef struct {
    char     name[DEVDATA_NAME_LEN];
    uint16_t cluster;
    uint16_t attribute;
    uint16_t manufacturer_code;
    char     mode[DEVDATA_MODE_LEN];
    char     type[DEVDATA_MODE_LEN];
    float    coefficient;
    char     unit[DEVDATA_UNIT_LEN];
    bool     visible;
    bool     writable;
    int      type_writable;
    char     mqtt_device_class[DEVDATA_MODE_LEN];
    char     mqtt_state_class[DEVDATA_MODE_LEN];
    char     mqtt_icon[DEVDATA_MODE_LEN];
    char     type_jauge[DEVDATA_MODE_LEN];
    int      jauge_min;
    int      jauge_max;
    uint16_t poll;              // Periode de scrutation en MINUTES (0 = pas de poll)
    uint8_t  endpoint;          // Ligne propre a un endpoint (2..15), 0 = tout endpoint
} devdata_template_state_t;

/**
 * Ligne de template propre a un endpoint ("endpoint": N, epAttrKey de la
 * LiXee-Box) : la valeur est rangee sous une cle composee (N << 12) | attribut,
 * distincte de celle de l'endpoint 1 (interrupteurs multi-voies). Attributs
 * < 0x1000 et endpoints 2 a 15 ; sinon la cle reste l'attribut.
 */
static inline uint16_t devdata_ep_key(uint16_t attr, uint8_t ep)
{
    return (ep >= 2 && ep <= 15 && attr < 0x1000) ? (uint16_t)((ep << 12) | attr) : attr;
}

/** Action d'un template */
typedef struct {
    char     name[DEVDATA_NAME_LEN];
    uint16_t command;
    uint8_t  endpoint;
    int32_t  value;             // valeur de l'action (DP Tuya, commande de cluster : au-dela de 16 bits)
    uint16_t cluster;
    uint16_t manufacturer_code;
    bool     visible;
} devdata_template_action_t;

#define DEVDATA_MAX_TEMPLATE_STATES   64
#define DEVDATA_MAX_TEMPLATE_ACTIONS  48   // actions du fichier + actions dynamiques (codes IR)
#define DEVDATA_MAX_DYN_ACTIONS       32   // actions ajoutees hors fichier, par appareil
#define DEVDATA_MAX_TEMPLATE_BINDS     8
#define DEVDATA_MAX_TEMPLATE_REPORTS  48

/**
 * Entree "report" d'un template : configuration du reporting d'un attribut.
 * Les mesures du ZLinky (index, puissance, tension, courant) n'arrivent QUE
 * par ce mecanisme : le module n'envoie rien tant qu'on ne le lui demande pas.
 */
typedef struct {
    uint16_t cluster;
    uint16_t attribute;
    uint8_t  type;              // Type ZCL de l'attribut (ex: 0x25 uint48)
    uint16_t min_interval;      // s
    uint16_t max_interval;      // s
    uint16_t timeout;
    uint32_t change;            // Variation minimale declenchant un rapport
    uint16_t manufacturer_code;
    uint8_t  mode_mask;         // Bit n = mode Linky n concerne ; 0 = tous modes
} devdata_template_report_t;

/** Template complet (alloue en heap/PSRAM a la demande) */
typedef struct {
    devdata_template_state_t  states[DEVDATA_MAX_TEMPLATE_STATES];
    int                       state_count;
    devdata_template_action_t actions[DEVDATA_MAX_TEMPLATE_ACTIONS];
    int                       action_count;
    uint16_t                  bind_clusters[DEVDATA_MAX_TEMPLATE_BINDS];
    int                       bind_count;
    devdata_template_report_t reports[DEVDATA_MAX_TEMPLATE_REPORTS];
    int                       report_count;
    bool                      loaded;
    // Actions du fichier : actions[0..base_action_count[ ; au-dela, actions
    // dynamiques (device_data_set_dynamic_actions). dyn_only : pas de fichier
    // de template, seulement des actions dynamiques (loaded vaut alors true
    // pour que fiche, regles, groupes et MQTT les proposent).
    int                       base_action_count;
    bool                      dyn_only;
} devdata_template_t;

/** Info device supplementaires */
typedef struct {
    char     linky_mode[8];
    char     power_socket[8];
    char     alias[DEVDATA_ALIAS_LEN];
    char     status[16];
} devdata_info_t;

/**
 * Donnees completes d'un device Zigbee.
 * Alloue en PSRAM via device_data_get_or_create().
 * Taille ~16 KB par device (raisonnable pour PSRAM).
 */
typedef struct {
    uint64_t            ieee_addr;
    devdata_info_t      info;
    devdata_cluster_t   clusters[DEVDATA_MAX_CLUSTERS];
    uint8_t             cluster_count;
    devdata_poll_item_t poll_list[DEVDATA_MAX_POLL_ITEMS];
    uint8_t             poll_count;
    devdata_template_t  *tmpl;       // Alloue separement en heap
    bool                dirty;
} device_data_t;

// ============================================================================
// API publique - Gestion globale
// ============================================================================

esp_err_t device_data_init(void);
esp_err_t device_data_load_all(void);
/** Relit le fichier d'un appareil (edition / import par l'editeur de fichiers). */
esp_err_t device_data_reload(uint64_t ieee_addr);
esp_err_t device_data_save_all(void);
esp_err_t device_data_save(uint64_t ieee_addr);

// ============================================================================
// API publique - Acces aux devices
// ============================================================================

device_data_t* device_data_get_or_create(uint64_t ieee_addr);
device_data_t* device_data_find(uint64_t ieee_addr);
/**
 * Supprime l'appareil et son fichier /littlefs/db/<IEEE>.json. L'entree n'est
 * jamais liberee : le slot est marque libre (ieee_addr = 0, valeurs effacees,
 * template marque non charge) et reutilise par le prochain appareil. Un
 * pointeur device_data_t garde par une autre tache reste donc valide en
 * memoire (il peut designer un slot vide ou un autre appareil).
 */
esp_err_t device_data_remove(uint64_t ieee_addr);
int device_data_count(void);
device_data_t* device_data_get_by_index(int index);

// ============================================================================
// API publique - Valeurs cluster/attribut
// ============================================================================

esp_err_t device_data_set_value(uint64_t ieee_addr, uint16_t cluster_id,
                                 uint16_t attr_id, const char *value);
esp_err_t device_data_get_value(uint64_t ieee_addr, uint16_t cluster_id,
                                 uint16_t attr_id, char *out_value, size_t out_len);
const char* device_data_get_value_ptr(uint64_t ieee_addr, uint16_t cluster_id,
                                       uint16_t attr_id);

/**
 * Comme device_data_set_value(), en memorisant aussi le type ZCL du rapport
 * (persiste dans le fichier de l'appareil, cle "_T"). Le type permet un
 * affichage exact : signe des entiers (temperature negative), chaines
 * decodees, flottants. Sans lui, le formatage se rabat sur des heuristiques.
 */
esp_err_t device_data_set_value_typed(uint64_t ieee_addr, uint16_t cluster_id,
                                       uint16_t attr_id, const char *value, uint8_t zcl_type);

/** Attribut stocke (valeur, type, date), ou NULL. */
const devdata_attr_t* device_data_get_attr(uint64_t ieee_addr, uint16_t cluster_id,
                                            uint16_t attr_id);

// ============================================================================
// API publique - Formatage des valeurs (portage de GetValueStatus)
// ============================================================================

/**
 * Registre de statuts STGE (FF66/0x0217 du ZLinky, mode Standard). Le ZLinky
 * l'envoie en texte de 8 chiffres hexadecimaux ("000A0401"), range comme toute
 * chaine en hexa de ses caracteres ("3030304130343031") ; une valeur deja
 * numerique (au plus 8 chiffres) est acceptee telle quelle.
 * @return false si la valeur est vide ou illisible
 */
bool device_data_stge_value(const char *hex, uint32_t *out);

/**
 * Convertit une valeur hexa stockee en entier, signe selon le type ZCL (ou,
 * type inconnu, selon une table des attributs signes usuels).
 * @return false si la valeur est vide ou non numerique
 */
bool device_data_hex_to_int64(const char *hex, uint8_t zcl_type,
                              uint16_t cluster_id, uint16_t attr_id, int64_t *out);

/**
 * Formate une valeur brute pour l'affichage, comme GetValueStatus() de la
 * LiXee-Box : type de template "numeric" -> entier, "float" ou coefficient
 * different de 1 -> nombre a 2 decimales ; autre type -> texte decode pour
 * une chaine ZCL, sinon la valeur hexa telle quelle. Chaine vide si aucune
 * valeur n'a ete recue.
 * @return longueur ecrite
 */
size_t device_data_format_value(uint16_t cluster_id, uint16_t attr_id, const char *hex,
                                uint8_t zcl_type, const char *tmpl_type, float coefficient,
                                char *out, size_t out_len);

/** Formate la valeur courante d'un etat de template d'un appareil. */
size_t device_data_format_state(uint64_t ieee_addr, const devdata_template_state_t *state,
                                char *out, size_t out_len);

/**
 * Filtre par mode TIC du ZLinky : un etat dont le champ "mode" liste des
 * modes ("1;3;5;7") n'est affiche que dans ces modes.
 * @param linky_mode  mode courant, ou -1 pour ne pas filtrer (autre appareil,
 *                    mode inconnu)
 */
bool device_data_state_in_mode(const devdata_template_state_t *state, int linky_mode);

/** Mode TIC memorise pour l'appareil (INFO.linkyMode), -1 si inconnu. */
int device_data_linky_mode(uint64_t ieee_addr);

// ============================================================================
// API publique - Poll list
// ============================================================================

esp_err_t device_data_add_poll_item(uint64_t ieee_addr, uint16_t cluster_id,
                                     uint16_t attr_id, uint32_t interval_ms,
                                     uint16_t manufacturer_code);
const devdata_poll_item_t* device_data_get_poll_list(uint64_t ieee_addr, int *out_count);
esp_err_t device_data_update_poll_timestamp(uint64_t ieee_addr, uint16_t cluster_id,
                                             uint16_t attr_id, uint32_t timestamp);

// ============================================================================
// API publique - Templates
// ============================================================================

esp_err_t device_data_load_template(uint64_t ieee_addr,
                                     const char *device_id_file,
                                     const char *model_name);
const devdata_template_t* device_data_get_template(uint64_t ieee_addr);

/**
 * Actions propres a un appareil, ajoutees apres celles de son template (codes
 * IR appris : une action par code). Gardees par IEEE : reappliquees a chaque
 * chargement du template et a un nouvel appairage. Sans fichier de template,
 * un template ne portant que ces actions est cree (dyn_only). n = 0 les retire.
 */
esp_err_t device_data_set_dynamic_actions(uint64_t ieee_addr, const devdata_template_action_t *actions, int n);
float device_data_get_coefficient(uint64_t ieee_addr, uint16_t cluster_id, uint16_t attr_id);

/**
 * Cle de rangement d'un attribut recu de l'endpoint ep : la cle composee si
 * le template de l'appareil declare cette ligne sur cet endpoint, l'attribut
 * sinon (beaucoup d'appareils n'emettent pas depuis l'endpoint 1).
 */
uint16_t device_data_storage_attr(uint64_t ieee_addr, uint16_t cluster, uint16_t attr, uint8_t ep);

/**
 * Attribut ZCL reel d'une cle de rangement, pour lire ou ecrire. *ep recoit
 * l'endpoint de la ligne de template (0 pour une cle ordinaire).
 */
uint16_t device_data_resolve_key(uint64_t ieee_addr, uint16_t cluster, uint16_t key, uint8_t *ep);

// ============================================================================
// API publique - Serialisation JSON (pour web API)
// ============================================================================

char* device_data_to_json(uint64_t ieee_addr);
char* device_data_all_to_json(void);
char* device_data_cluster_to_json(uint64_t ieee_addr, uint16_t cluster_id);

#ifdef __cplusplus
}
#endif
