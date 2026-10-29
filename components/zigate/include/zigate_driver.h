#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// ZiGate+ UART Driver
// Communication serie avec le module ZiGate+ via protocole binaire
// Ref: https://zigate.fr/documentation/commandes-zigate/
// ============================================================================

// --- Configuration GPIO (via Kconfig / menuconfig) ---
// Les valeurs par defaut sont definies dans main/Kconfig.projbuild
#ifdef CONFIG_ZIGATE_UART_RXD
#define ZIGATE_RXD CONFIG_ZIGATE_UART_RXD
#else
#define ZIGATE_RXD 17
#endif

#ifdef CONFIG_ZIGATE_UART_TXD
#define ZIGATE_TXD CONFIG_ZIGATE_UART_TXD
#else
#define ZIGATE_TXD 18
#endif

#ifdef CONFIG_ZIGATE_RESET_GPIO
#define ZIGATE_RESET_PIN CONFIG_ZIGATE_RESET_GPIO
#else
#define ZIGATE_RESET_PIN 19
#endif

#ifdef CONFIG_ZIGATE_FLASH_GPIO
#define ZIGATE_FLASH_PIN CONFIG_ZIGATE_FLASH_GPIO
#else
#define ZIGATE_FLASH_PIN 40
#endif

#define ZIGATE_UART_NUM    UART_NUM_1
#define ZIGATE_BAUD_RATE   115200
#define ZIGATE_BUF_SIZE    2048

// --- Marqueurs du protocole ZiGate ---
#define ZIGATE_MSG_START   0x01
#define ZIGATE_MSG_END     0x03
#define ZIGATE_ESCAPE      0x02

// --- Commandes ZiGate principales ---
#define ZIGATE_CMD_GET_VERSION         0x0010
#define ZIGATE_CMD_RESET               0x0011
#define ZIGATE_CMD_ERASE_PDM           0x0012
#define ZIGATE_CMD_SET_CHANNEL_MASK    0x0021
#define ZIGATE_CMD_SET_SECURITY        0x0022
#define ZIGATE_CMD_SET_DEVICE_TYPE     0x0023
#define ZIGATE_CMD_START_NETWORK       0x0024
#define ZIGATE_CMD_PERMIT_JOIN         0x0049
#define ZIGATE_CMD_GET_DEVICES_LIST    0x0015
#define ZIGATE_CMD_ACTIVE_ENDPOINT     0x0045
#define ZIGATE_CMD_SIMPLE_DESC         0x0043
#define ZIGATE_CMD_READ_ATTRIBUTE      0x0100
#define ZIGATE_CMD_WRITE_ATTRIBUTE     0x0110
#define ZIGATE_CMD_CONFIG_REPORT       0x0120
#define ZIGATE_CMD_BIND                0x0030
#define ZIGATE_CMD_UNBIND              0x0031
#define ZIGATE_CMD_ONOFF               0x0092
#define ZIGATE_CMD_MOVE_TO_LEVEL       0x0081
#define ZIGATE_CMD_WINDOW_COVERING     0x00FA
#define ZIGATE_CMD_REMOVE_DEVICE       0x0026
#define ZIGATE_CMD_RAW_APS             0x0530
#define ZIGATE_CMD_NETWORK_STATE       0x0009
#define ZIGATE_CMD_GET_NETWORK_STATE   0x0025
#define ZIGATE_CMD_SET_RAW_MODE        0x0002
#define ZIGATE_CMD_SET_LED             0x0018
#define ZIGATE_CMD_LEAVE_REQUEST       0x004C
#define ZIGATE_CMD_NWK_RECOVERY_EXTRACT 0x0602
#define ZIGATE_CMD_NWK_RECOVERY_RESTORE 0x0603

// --- Reponses ZiGate ---
#define ZIGATE_RESP_STATUS             0x8000
#define ZIGATE_RESP_LOG                0x8001
#define ZIGATE_RESP_VERSION            0x8010
#define ZIGATE_RESP_DEVICE_ANNOUNCE    0x004D
#define ZIGATE_RESP_DEVICE_LIST        0x8015
#define ZIGATE_RESP_ACTIVE_ENDPOINT    0x8045
#define ZIGATE_RESP_SIMPLE_DESC        0x8043
#define ZIGATE_RESP_ATTRIBUTE_REPORT   0x8102
#define ZIGATE_RESP_READ_ATTRIBUTE     0x8100
#define ZIGATE_RESP_DEFAULT_RESPONSE   0x8101
#define ZIGATE_RESP_LEAVE_IND          0x8048
#define ZIGATE_RESP_NETWORK_STATE      0x8009
#define ZIGATE_RESP_PERMIT_JOIN_STATUS 0x8014
#define ZIGATE_RESP_NODE_DESC          0x8042

// --- Structures de donnees ---

typedef struct {
    uint16_t cmd;
    uint16_t length;
    uint8_t  checksum;
    uint8_t  payload[512];
    uint8_t  lqi;       // Qualite du lien (pour les reponses)
    uint8_t  seq;       // Numero de sequence
} zigate_packet_t;

typedef struct {
    uint16_t short_addr;
    uint64_t ieee_addr;
    uint8_t  device_type;     // 0=coord, 1=router, 2=end-device
    uint8_t  lqi;
    bool     is_active;
} zigate_device_entry_t;

typedef struct {
    uint16_t short_addr;
    uint16_t cluster_id;
    uint16_t attr_id;
    uint8_t  attr_type;
    uint8_t  endpoint;
    uint8_t  data[256];
    uint16_t data_len;
} zigate_attribute_report_t;

// Callback pour les rapports d'attributs Zigbee
typedef void (*zigate_attribute_cb_t)(const zigate_attribute_report_t *report);

// Callback pour les annonces de nouveaux devices
typedef void (*zigate_device_announce_cb_t)(uint16_t short_addr, uint64_t ieee_addr);

// Callback pour les departs de devices
typedef void (*zigate_device_leave_cb_t)(uint64_t ieee_addr);

// --- API publique ---

/**
 * Initialise le driver UART ZiGate
 * Configure UART1, reset la ZiGate, et demarre la tache de reception
 */
esp_err_t zigate_init(void);

/**
 * Reset hardware de la ZiGate (toggle du pin RESET)
 */
esp_err_t zigate_reset(void);

/**
 * Envoie un paquet brut a la ZiGate
 */
esp_err_t zigate_send_packet(const zigate_packet_t *pkt);

/**
 * Envoie une commande avec payload
 */
esp_err_t zigate_send_command(uint16_t cmd, const uint8_t *data, uint16_t len);

/**
 * Execute fn(arg) sur la tache de traitement des trames ("zigate_proc"),
 * dans l'ordre des trames deja recues. Sert a serialiser avec les callbacks
 * ZiGate (depart d'un appareil, rapports d'attributs...) une operation qui
 * modifie les memes donnees, par exemple la suppression d'un appareil
 * demandee par l'interface web.
 * Asynchrone : rend la main des que l'appel est mis en file (arg doit rester
 * valide jusqu'a l'execution ; fn le libere au besoin). Appel direct si la
 * tache n'existe pas (ZiGate non initialisee) ou si l'appelant est deja la
 * tache de traitement.
 * @return ESP_OK, ESP_ERR_TIMEOUT si la file est restee pleine 2 s,
 *         ESP_ERR_NO_MEM, ESP_ERR_INVALID_ARG (fn nul). Hors ESP_OK, fn
 *         n'est pas appelee.
 */
esp_err_t zigate_run_on_proc(void (*fn)(void *), void *arg);

/**
 * Demande la version du firmware ZiGate
 */
esp_err_t zigate_get_version(void);

/**
 * Efface la PDM (base de donnees persistante ZiGate)
 */
esp_err_t zigate_erase_pdm(void);

/**
 * Demarre le reseau Zigbee
 */
esp_err_t zigate_start_network(void);

/**
 * Demande l'etat du reseau Zigbee
 */
esp_err_t zigate_get_network_state(void);

/**
 * Active/desactive le mode appairage (permit join)
 * @param duration_sec Duree en secondes (0 = fermer, 0xFF = permanent)
 */
esp_err_t zigate_permit_join(uint8_t duration_sec);

/**
 * Lit un attribut Zigbee
 */
esp_err_t zigate_read_attribute(uint16_t short_addr, uint8_t endpoint,
                                 uint16_t cluster, uint16_t attribute);

/**
 * Lit un attribut Zigbee avec manufacturer code (pour attributs proprietaires)
 * Si manufacturer_code == 0, identique a zigate_read_attribute()
 */
esp_err_t zigate_read_attribute_mfr(uint16_t short_addr, uint8_t endpoint,
                                     uint16_t cluster, uint16_t attribute,
                                     uint16_t manufacturer_code);

/**
 * Ecrit un attribut Zigbee (commande 0x0110).
 * @param value      Octets de la valeur, deja dans l'ordre attendu par la
 *                   ZiGate (big-endian pour les nombres), voir zigate_zcl_encode()
 */
esp_err_t zigate_write_attribute(uint16_t short_addr, uint8_t endpoint,
                                  uint16_t cluster, uint16_t attribute,
                                  uint8_t attr_type, const uint8_t *value, uint16_t value_len);

/**
 * Ecrit un attribut specifique fabricant (manufacturer_code == 0 : identique
 * a zigate_write_attribute()).
 */
esp_err_t zigate_write_attribute_mfr(uint16_t short_addr, uint8_t endpoint,
                                      uint16_t cluster, uint16_t attribute,
                                      uint8_t attr_type, const uint8_t *value, uint16_t value_len,
                                      uint16_t manufacturer_code);

/**
 * Envoie une commande OnOff (0x0092)
 * @param action 0 = OFF, 1 = ON, 2 = TOGGLE
 */
esp_err_t zigate_send_onoff(uint16_t short_addr, uint8_t endpoint, uint8_t action);

/**
 * Commande Window Covering (0x00FA) : cmd ZCL (0 monter, 1 descendre, 2 stop,
 * 5 aller a un pourcentage...) suivie de 'extra_len' octets de parametres.
 */
esp_err_t zigate_send_window_covering(uint16_t short_addr, uint8_t endpoint, uint8_t cmd,
                                      const uint8_t *extra, uint8_t extra_len);

/**
 * Trame APS brute (0x0530) : 'zcl' est la trame ZCL complete (en-tete compris).
 */
esp_err_t zigate_send_raw_aps(uint16_t short_addr, uint8_t src_ep, uint8_t dst_ep,
                              uint16_t cluster, uint16_t profile, uint8_t radius,
                              const uint8_t *zcl, uint8_t zcl_len);

/** Reset logiciel de la ZiGate (commande 0x0011), sans toucher aux GPIO. */
esp_err_t zigate_soft_reset(void);

/** LED de la ZiGate allumee ou eteinte (0x0018). */
esp_err_t zigate_set_led(bool on);

/**
 * Masque de canaux (0x0021) puis reset logiciel pour l'appliquer, comme la
 * LiXee-Box. Accepte un numero de canal (11..26) ou un masque de bits.
 * @return ESP_ERR_INVALID_ARG si aucun canal 11..26 n'est designe
 */
esp_err_t zigate_set_channel_mask(uint32_t channel_or_mask);

/** Mode brut de la ZiGate (0x0002) : trames APS non interpretees remontees en 0x8002. */
esp_err_t zigate_set_raw_mode(bool on);

/**
 * Demande a un appareil de quitter le reseau (Leave Request 0x004C), trame
 * utilisee par la LiXee-Box pour supprimer un appareil.
 */
esp_err_t zigate_leave_request(uint64_t ieee_addr);

/**
 * Nettoyage des appareils "fantomes" de la table de la ZiGate : extraction
 * (0x0602), mise a zero des entrees inconnues de zigate_devices, puis
 * restauration (0x0603). Asynchrone : resultat dans zigate_coord_info_t.
 */
esp_err_t zigate_clean_ghosts(void);

// --- Etat du coordinateur (renseigne par les reponses de la ZiGate) ---

typedef struct {
    bool     version_known;
    uint8_t  version[4];         // reponse 0x8010 brute (type, SDK, application x2)
    bool     state_known;        // reponse 0x8009 recue
    uint16_t short_addr;         // 0xFFFF : reseau non demarre
    uint64_t ieee;
    uint16_t pan_id;
    uint64_t ext_pan_id;
    uint8_t  channel;
    int16_t  start_status;       // reponse 0x8024 : 0/1 = reseau forme ou rejoint, -1 inconnu
    bool     raw_mode;
    int8_t   led;                // -1 inconnu, sinon derniere commande envoyee
    uint32_t permit_join_until;  // secondes depuis le demarrage (0 = ferme, UINT32_MAX = permanent)
    uint32_t join_seq;           // incremente a chaque annonce d'appareil (0x004D)
    uint64_t last_join_ieee;
    uint16_t last_join_short;
    uint32_t last_join_s;        // secondes depuis le demarrage
    bool     ghost_pending;
    int      ghost_result;       // -1 jamais lance, sinon nombre d'entrees effacees
    uint32_t frames_rx;
    uint32_t frames_tx;
} zigate_coord_info_t;

/** Copie de l'etat courant du coordinateur. */
void zigate_get_coord_info(zigate_coord_info_t *out);

/**
 * Vrai des qu'une trame valide (somme de controle correcte) a ete recue de
 * la ZiGate : la ZiGate est presente et repond. Sert a masquer les menus
 * Zigbee de l'interface quand la carte n'en a pas.
 */
bool zigate_is_detected(void);

/** Secondes depuis le demarrage (horloge commune des champs ..._s). */
uint32_t zigate_uptime_s(void);

/**
 * Configure un binding (commande ZiGate 0x0030) : les rapports du cluster
 * 'cluster' de l'appareil seront envoyes a dst_ieee / dst_ep.
 */
esp_err_t zigate_bind(uint64_t ieee_addr, uint8_t src_ep,
                       uint16_t cluster, uint64_t dst_ieee, uint8_t dst_ep);

/**
 * Configure le reporting d'un attribut (commande ZiGate 0x0120).
 * @param type    Type ZCL de l'attribut ; fixe la taille du champ 'change'
 * @param change  Variation minimale declenchant un rapport (types analogiques)
 * @param manufacturer_code  0 si l'attribut n'est pas specifique fabricant
 */
esp_err_t zigate_configure_reporting(uint16_t short_addr, uint8_t endpoint,
                                     uint16_t cluster, uint16_t attribute, uint8_t type,
                                     uint16_t min_interval, uint16_t max_interval,
                                     uint16_t timeout, uint32_t change,
                                     uint16_t manufacturer_code);

/**
 * Adresse IEEE du coordinateur ZiGate, apprise de la reponse 0x8009.
 * @return 0 tant qu'elle n'est pas connue
 */
uint64_t zigate_get_coordinator_ieee(void);

/**
 * Supprime un device du reseau
 */
esp_err_t zigate_remove_device(uint64_t ieee_addr);

/**
 * Demande la liste des endpoints actifs d'un device
 * Reponse attendue: 0x8045
 */
esp_err_t zigate_request_active_endpoints(uint16_t short_addr);

/**
 * Demande le descripteur simple d'un endpoint d'un device
 * Reponse attendue: 0x8043
 */
esp_err_t zigate_request_simple_descriptor(uint16_t short_addr, uint8_t endpoint);

/**
 * Lit l'attribut Basic Cluster (manufacturer, model) d'un device
 */
esp_err_t zigate_read_basic_attributes(uint16_t short_addr, uint8_t endpoint);

// --- Callbacks ---

// Callback quand la decouverte d'un device est complete (endpoints + descripteurs resolus)
typedef void (*zigate_device_discovery_done_cb_t)(uint16_t short_addr, uint64_t ieee_addr);

/**
 * Retours radio et statuts de commande, pour cadencer des envois sur les
 * accuses (signatures identiques a rules_on_zigate_status() et
 * rules_on_zigate_radio_result() du lot regles). Appeles depuis la tache de
 * traitement ZiGate.
 *   status : message 0x8000 (statut, type du paquet concerne)
 *   radio  : 0x8011 (accuse), 0x8012 (confirmation APS), 0x8701 (echec de
 *            route), 0x8702 (echec APS), avec l'adresse courte visee
 */
typedef void (*zigate_status_cb_t)(uint8_t status, uint16_t packet_type);
typedef void (*zigate_radio_cb_t)(uint16_t msg_type, uint16_t short_addr, uint8_t status);
void zigate_register_status_cb(zigate_status_cb_t cb);
void zigate_register_radio_cb(zigate_radio_cb_t cb);

/**
 * Appareil sur pile (endormi), d'apres les donnees connues de l'appareil :
 * fourni par l'application. Sert a taire les echecs des requetes reseau.
 */
typedef bool (*zigate_sleepy_cb_t)(uint64_t ieee);
void zigate_register_sleepy_cb(zigate_sleepy_cb_t cb);

/**
 * Pendant l'appel du callback radio d'un 0x8702 : vrai si l'echec vient d'une
 * requete reseau (ZDO, endpoint 0 : table de voisinage 0x004E envoyee par la
 * ZiGate elle-meme, adresse...) vers un appareil sur pile, qui dort et ne
 * l'entend pas. Rien a signaler a l'utilisateur dans ce cas ; un appareil sur
 * secteur reste signale (route reellement absente).
 */
bool zigate_radio_fail_quiet(void);

// --- Enregistrement de callbacks ---
void zigate_register_attribute_cb(zigate_attribute_cb_t cb);
void zigate_register_device_announce_cb(zigate_device_announce_cb_t cb);
void zigate_register_device_leave_cb(zigate_device_leave_cb_t cb);
void zigate_register_discovery_done_cb(zigate_device_discovery_done_cb_t cb);

/**
 * Commande du cluster On/Off recue d'un bouton ou d'une telecommande (message
 * 0x8095, decode par la ZiGate) : commande 0 Off, 1 On, 2 Toggle.
 */
typedef void (*zigate_onoff_cmd_cb_t)(uint16_t short_addr, uint8_t endpoint, uint8_t command);
void zigate_register_onoff_cmd_cb(zigate_onoff_cmd_cb_t cb);

#ifdef __cplusplus
}
#endif
