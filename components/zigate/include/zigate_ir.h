#pragma once

// ============================================================================
// Telecommandes infrarouges Zosung (Moes UFO-R11, TS1201 / ZS06...)
//
// Clusters fabricant 0xE004 (apprentissage) et 0xED00 (transfert). Un code
// voyage par fragments, demandes par le recepteur (commandes 0x02 / 0x03),
// clos par 0x04 / 0x05 (protocole decrit par zigbee2mqtt, zosung.ts) :
//  - apprentissage : E004/00 {"study":0}, l'appareil annonce le code (ED00/00),
//    le pont le lit fragment par fragment ; code rendu en base64 ;
//  - emission : ED00/00 avec la longueur d'un JSON {"key_num":1,...
//    "key_code":"<base64>"}, que l'appareil lit fragment par fragment.
// Remplace Infrared.cpp de la LiXee-Box (actions 200 apprendre / 201 emettre),
// dont l'echange n'etait pas termine (code appris jamais enregistre).
// Codes nommes : /littlefs/ir/<IEEE>.json, en cache PSRAM.
// ============================================================================

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZIGATE_IR_MAX_CODES 32
#define ZIGATE_IR_NAME_LEN  32

/**
 * Codes d'un appareil changes (lecture au demarrage, enregistrement,
 * suppression) : appele hors verrou, dans la tache de l'appelant.
 */
typedef void (*zigate_ir_change_cb_t)(uint64_t ieee);
void zigate_ir_set_change_cb(zigate_ir_change_cb_t cb);

/** Noms des codes enregistres (dans l'ordre des numeros) ; renvoie leur nombre. */
int zigate_ir_code_names(uint64_t ieee, char (*names)[ZIGATE_IR_NAME_LEN], int max);

/** Verrou et table des appareils (zigate_init, avant les taches de la ZiGate). */
void zigate_ir_prepare(void);

/** Relit les codes enregistres (tache principale : lecture flash). */
void zigate_ir_init(void);

/** Mode apprentissage (marche / arret). */
esp_err_t zigate_ir_learn(uint64_t ieee, bool on);

/** Emet un code IR (base64, tel que rendu par l'apprentissage). */
esp_err_t zigate_ir_send_code(uint64_t ieee, const char *b64);

/** Emet le code enregistre n (0..), depuis les actions et les regles (RAM seule). */
esp_err_t zigate_ir_send_saved(uint64_t ieee, int index);

/** Trame des clusters E004 / ED00 recue (0x8002, tache de traitement ZiGate). */
void zigate_ir_handle_frame(uint16_t short_addr, uint8_t endpoint, uint16_t cluster,
                            const uint8_t *zcl, int len);

/** Enregistre (ou remplace, meme nom) un code ; ecrit la flash (tache HTTP). */
esp_err_t zigate_ir_save(uint64_t ieee, const char *name, const char *b64);

/** Supprime le code n ; ecrit la flash (tache HTTP). */
esp_err_t zigate_ir_delete(uint64_t ieee, int index);

/**
 * Etat d'un appareil, a liberer par free() :
 * {"learning":b,"learned":"<base64>"|null,"learnedAgo":s,"sending":b,
 *  "lastSend":"ok"|"pending"|"",
 *  "codes":[{"i":0,"name":"..","code":".."}]}
 */
char *zigate_ir_status_json(uint64_t ieee);

#ifdef __cplusplus
}
#endif
