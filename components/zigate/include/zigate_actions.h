#pragma once

// ============================================================================
// ZiGate Actions - Commandes "action" des templates et ecritures typees
//
// Portage de SendAction() / SendActionEx() (zigbee.cpp de la LiXee-Box) et
// de sendTuyaDatapointSet() / sendTuyaDatapointQuery() (thermostat.cpp).
// Le champ "command" d'une action de template garde la numerotation de la
// LiXee-Box, pour que les fichiers /littlefs/tp soient communs aux deux.
// ============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Numeros de commande des templates (LiXee-Box)
#define ZB_ACT_ONOFF                146   // value : 0 OFF, 1 ON, 2 TOGGLE
#define ZB_ACT_COVER                250   // value : 0 monter, 1 descendre, 2 stop
#define ZB_ACT_COVER_POSITION       251   // value : ouverture 0..100 %
#define ZB_ACT_COVER_POSITION_INV   252   // idem, appareil qui inverse le sens
#define ZB_ACT_TUYA_BOOL            300   // endpoint = numero de DP
#define ZB_ACT_TUYA_VALUE           301
#define ZB_ACT_TUYA_ENUM            302
#define ZB_ACT_TUYA_RAW             303
#define ZB_ACT_HVAC_SYSTEM_MODE     310
#define ZB_ACT_HVAC_SETPOINT_BOTH   311
#define ZB_ACT_HVAC_FAN_MODE        312
#define ZB_ACT_HVAC_LOUVER          313
#define ZB_ACT_HVAC_COOL_SETPOINT   314
#define ZB_ACT_HVAC_HEAT_SETPOINT   315
#define ZB_ACT_TUYA_QUERY           320
#define ZB_ACT_CLUSTER_CMD          400   // commande specifique (cluster + code fabricant)
#define ZB_ACT_IR_LEARN             200   // telecommande IR : value 1 apprendre, 0 arreter
#define ZB_ACT_IR_SEND              201   // telecommande IR : value = numero du code enregistre

// Types de datapoint Tuya
#define TUYA_DP_RAW     0
#define TUYA_DP_BOOL    1
#define TUYA_DP_VALUE   2
#define TUYA_DP_STRING  3
#define TUYA_DP_ENUM    4
#define TUYA_DP_BITMAP  5

/**
 * Source de la consigne enregistree d'un thermostat (attribut 0x0011 froid ou
 * 0x0012 chaud du cluster 0x0201), en 0,01 degC. Apres un passage en mode
 * chauffage ou froid (ZB_ACT_HVAC_SYSTEM_MODE), la consigne correspondante est
 * renvoyee : les telecommandes infrarouges (NodOn IRB-4-1-00) n'emettent le
 * code du mode qu'a la reception d'une consigne.
 */
typedef bool (*zigate_setpoint_source_t)(uint16_t short_addr, uint16_t attr, int16_t *out);
void zigate_actions_set_setpoint_source(zigate_setpoint_source_t fn);

/** true pour les consignes de climatisation (311, 314, 315) : action a parametre (0,01 degC). */
bool zigate_action_is_setpoint(int command);

/** true pour les commandes de position de volet, affichees en curseur. */
bool zigate_action_is_cover_position(int command);

/**
 * Execute une action de template.
 * @param cluster, manufacturer_code  utilises par ZB_ACT_CLUSTER_CMD uniquement
 * @return ESP_ERR_NOT_SUPPORTED pour une commande inconnue (dont l'infrarouge
 *         200/201 de la LiXee-Box, non porte)
 */
esp_err_t zigate_action_send(int command, uint16_t short_addr, uint8_t endpoint,
                             int32_t value, uint16_t cluster, uint16_t manufacturer_code);

/**
 * Comme zigate_action_send(), en rendant dans *zigate_cmd_out (si non nul)
 * l'opcode ZiGate de la trame reellement emise : 0x0092 (on/off), 0x00FA
 * (volet), 0x0530 (trame APS brute : Tuya, commande de cluster), 0x0110
 * (ecriture d'attribut : climatisation ; pour ZB_ACT_HVAC_SETPOINT_BOTH,
 * celui de la premiere des deux ecritures). C'est la valeur que la ZiGate
 * renvoie dans le champ "type du paquet" de son statut 0x8000 : elle permet
 * de reconnaitre l'accuse de cette commande. 0 si rien n'a ete emis (erreur,
 * commande inconnue).
 */
esp_err_t zigate_action_send_ex(int command, uint16_t short_addr, uint8_t endpoint,
                                int32_t value, uint16_t cluster, uint16_t manufacturer_code,
                                uint16_t *zigate_cmd_out);

/** Ecrit un datapoint Tuya (commande 0x00 du cluster 0xEF00). */
esp_err_t zigate_tuya_dp_set(uint16_t short_addr, uint8_t endpoint, uint8_t dp,
                             uint8_t dp_type, uint32_t value);

/** Demande a un appareil Tuya de renvoyer tous ses datapoints (commande 0x03). */
esp_err_t zigate_tuya_dp_query(uint16_t short_addr, uint8_t endpoint);

/** Taille en octets d'une valeur ZCL de type fixe, 0 si type variable ou inconnu. */
int zigate_zcl_type_size(uint8_t zcl_type);

/**
 * Encode une saisie texte en valeur ZCL du type donne (ordre big-endian
 * attendu par la ZiGate) : entiers en decimal ou "0x..", flottant simple,
 * chaines (prefixees de leur longueur).
 * @return nombre d'octets ecrits, ou -1 si la saisie est invalide
 */
int zigate_zcl_encode(uint8_t zcl_type, const char *text, uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif
