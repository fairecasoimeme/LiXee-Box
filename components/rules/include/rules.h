#pragma once

// ============================================================================
// Rules - moteur de regles (portage de RulesManager, rules.cpp de la LiXee-Box)
//
// Fichiers (formats IDENTIQUES a la LiXee-Box, un rules.json existant est repris
// tel quel) :
//   /littlefs/config/rules.json        {"rules":[{name, enabled, duration, repeat,
//                                        cooldown, maxExecPerDay, trigger{mode,IEEE,
//                                        cluster,attribute}, conditions[], actions[],
//                                        elseActions[]}]}
//   /littlefs/config/statusRules.json  {"<nom>":"<etat>|<JJ/MM/AAAA HH:MM>"}
//                                        etat : 0 faux, 1 vrai, 2 attente du maintien
//
// Fils d'execution :
//   - rules_on_attribute() et rules_on_zigate_*() sont appelables depuis la tache de
//     traitement ZiGate : ils ne font que poster un evenement (aucune attente, aucun
//     acces flash) ;
//   - l'evaluation et l'envoi des commandes se font dans la tache "rules" (pile en
//     PSRAM, aucun acces flash) ;
//   - les lectures/ecritures de fichiers se font dans l'appelant : rules_init()
//     (main), rules_tick() (boucle principale), routes web (tache httpd).
// ============================================================================

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RULES_FILE_PATH        "/littlefs/config/rules.json"
#define RULES_STATUS_PATH      "/littlefs/config/statusRules.json"

#define RULES_MAX              64    // regles chargees au plus
#define RULES_MAX_CONDITIONS   16    // par regle
#define RULES_MAX_ACTIONS      16    // par liste (actions / elseActions)
#define RULES_NAME_LEN         48

/** Resultat des operations d'edition (traduit en code HTTP par web_rules.cpp). */
typedef enum {
    RULES_RES_OK = 0,
    RULES_RES_BAD_REQUEST,   // 400 : JSON invalide, nom vide ou interdit...
    RULES_RES_NOT_FOUND,     // 404
    RULES_RES_CONFLICT,      // 409 : nom deja utilise
    RULES_RES_FULL,          // 400 : nombre maximal atteint
    RULES_RES_NO_MEM,        // 500
    RULES_RES_IO,            // 500 : ecriture en flash impossible
} rules_res_t;

// ----------------------------------------------------------------------------
// Cycle de vie et points d'entree pour l'integrateur (main.cpp)
// ----------------------------------------------------------------------------

/**
 * Charge rules.json, statusRules.json et actiongroups.json, puis demarre la tache
 * "rules". A appeler une fois, apres le montage de LittleFS, cfg_init(),
 * device_data_load_all() et zigate_devices_init(), depuis une tache dont la pile
 * est en RAM interne (lecture flash).
 */
esp_err_t rules_init(void);

/**
 * Rapport d'attribut recu. A appeler APRES device_data_set_value() (la regle relit
 * la valeur dans device_data). Ne fait que poster un evenement si une regle en mode
 * "event" surveille ce triplet : cout negligeable, jamais bloquant.
 * @param value  valeur hexadecimale du rapport (informative, peut etre NULL)
 */
void rules_on_attribute(uint64_t ieee, uint16_t cluster, uint16_t attr, const char *value);

/**
 * A appeler une fois par minute (boucle principale) : evalue les regles "timer"
 * (equivalent de applyRules() de la LiXee-Box) et enregistre statusRules.json si un
 * etat a change. L'ecriture flash a lieu dans l'appelant.
 */
void rules_tick(void);

/**
 * Optionnel : retours de la ZiGate, pour cadencer les actions sur les accuses
 * (comme actionPacer.cpp de la LiXee-Box). Sans ces appels, les actions radio sont
 * simplement espacees de RULES_PACE_NO_ACK_MS. Appelables depuis la tache ZiGate.
 *   rules_on_zigate_status       : message 0x8000 (statut, type de paquet)
 *   rules_on_zigate_radio_result : 0x8011 (accuse), 0x8012 (confirmation), 0x8701
 *                                  (echec de route), 0x8702 (echec APS)
 */
void rules_on_zigate_status(uint8_t status, uint16_t packet_type);
void rules_on_zigate_radio_result(uint16_t msg_type, uint16_t short_addr, uint8_t status);

// ----------------------------------------------------------------------------
// Crochets (cablage par l'integrateur, les autres lots etant ecrits en parallele)
// ----------------------------------------------------------------------------

/**
 * Notification d'une action de type "notification" (regle ou groupe). Appelee
 * depuis la tache "rules", dont la pile est en PSRAM : le crochet ne doit PAS
 * acceder a la flash directement (mettre le travail en file). level : 0 (comme
 * notificationManager.addNotification(..., 0, "rule") de la LiXee-Box).
 */
typedef void (*rules_notify_cb_t)(const char *title, const char *msg, int level);
void rules_set_notify_cb(rules_notify_cb_t cb);

/**
 * Execution d'une commande de template (equivalent de SendActionEx() de la
 * LiXee-Box) : command = code du template (146 on/off, 250 volet, 251/252 position,
 * 300-303 Tuya, 310-315 HVAC, 400 cluster specifique...), value = valeur texte.
 * zigate_cmd_out (peut etre NULL) : commande ZiGate emise, pour rapprocher le
 * statut 0x8000 ; 0 si inconnue (les statuts 0x8000 sont alors ignores :
 * l'issue vient des retours radio ou du delai maximal).
 * Retour : ESP_OK si une trame est partie, ESP_ERR_NOT_SUPPORTED si la commande
 * n'est pas geree.
 */
typedef esp_err_t (*rules_command_fn_t)(uint64_t ieee, uint16_t short_addr, uint8_t endpoint,
                                        int command, uint16_t cluster, uint16_t manufacturer_code,
                                        const char *value, uint16_t *zigate_cmd_out);

/** Remplace l'executeur (NULL : executeur par defaut ci-dessous). */
void rules_set_command_executor(rules_command_fn_t fn);

/** Executeur par defaut, construit les trames avec zigate_send_command(). */
esp_err_t rules_default_command_executor(uint64_t ieee, uint16_t short_addr, uint8_t endpoint,
                                         int command, uint16_t cluster, uint16_t manufacturer_code,
                                         const char *value, uint16_t *zigate_cmd_out);

// ----------------------------------------------------------------------------
// API pour les routes web (web_rules.cpp). Les chaines renvoyees sont allouees
// (PSRAM de preference) et a liberer par free() ; NULL = memoire insuffisante.
// ----------------------------------------------------------------------------

/** {"rules":[{...regle..., "status":n, "lastDate":"..."}], "max":RULES_MAX} */
char *rules_list_json(void);

/** Une regle au format de rules.json, NULL si inconnue (*found = false). */
char *rules_get_json(const char *name, bool *found);

/** Etat et derniere date d'une regle (getStatusRule / getLastDateRule). */
bool rules_get_status(const char *name, int *status, char *last_date, size_t len);

/** Contenu brut de rules.json (editeur JSON), "{\"rules\":[]}" si absent. */
char *rules_raw_get(void);

/**
 * Ajout / modification d'une regle depuis le corps JSON envoye par l'editeur
 * (meme forme que /api/rules/add|edit de la LiXee-Box ; "oldName" pour edit).
 * err : message lisible en cas d'echec.
 */
rules_res_t rules_add_json(const char *body, size_t len, char *err, size_t errlen);
rules_res_t rules_edit_json(const char *body, size_t len, char *err, size_t errlen);
rules_res_t rules_delete(const char *name);
rules_res_t rules_set_enabled(const char *name, bool enabled);

/** Remplace rules.json entier (editeur JSON) apres validation. */
rules_res_t rules_raw_set(const char *text, size_t len, char *err, size_t errlen);

/**
 * Catalogue pour l'editeur de regles : appareils avec leurs etats et actions de
 * template et les valeurs courantes.
 * {"<IEEE>":{"INFO":{alias,model,manufacturer,device_id,endpoint},
 *            "status":[{name,cluster:"0006",attribut,unit,coefficient,type}],
 *            "action":[{name,endpoint,command,visible}],
 *            "values":{"0006":{"0":"01"}}}}
 * Les attributs declares "string" sont renvoyes en texte (decodes de l'hexa).
 */
char *rules_devices_json(void);

#ifdef __cplusplus
}
#endif
