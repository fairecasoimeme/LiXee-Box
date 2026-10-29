#pragma once

// ============================================================================
// Device Setup - Configuration d'un appareil Zigbee a partir de son template
//
// Portage de la sequence de la passerelle Arduino (protocol.cpp), lancee une
// fois le modele de l'appareil connu :
//   traitement specifique au modele -> bind -> config report -> liste de poll
//
// Le template (/littlefs/tp/<DeviceId>.json) decrit, pour chaque modele :
//   - "bind"   : clusters a lier au coordinateur ;
//   - "report" : reporting a configurer (filtre par mode Linky pour le ZLinky) ;
//   - "status" : attributs, dont ceux a scruter ("poll", en minutes).
//
// Tout se deroule dans une tache dediee : la sequence attend des reponses
// de l'appareil, ce qui ne doit jamais bloquer la tache de reception ZiGate.
// ============================================================================

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Cree la tache de configuration. A appeler apres l'init de la ZiGate et le
 * chargement de device_data.
 */
esp_err_t device_setup_init(void);

/**
 * Demande la configuration d'un appareil.
 * @param force  true : renvoyer bind et reporting meme si deja faits (nouvel
 *               appairage, appareil revenu sur le reseau). false : seulement
 *               s'il n'a jamais ete configure.
 */
void device_setup_request(uint64_t ieee_addr, bool force);

/**
 * Au demarrage : charge le template de chaque appareil connu (coefficients,
 * liste de poll) et configure ceux qui ne l'ont jamais ete -- par exemple
 * les appareils appaires avant que ce mecanisme existe.
 */
void device_setup_boot(void);

/**
 * Appele par la tache de configuration une fois le template de l'appareil
 * charge (ok = true) ou introuvable (ok = false), avant bind et reporting.
 */
typedef void (*device_setup_template_cb_t)(uint64_t ieee_addr, bool ok);

/**
 * Etapes de la configuration d'un appareil (bind, reporting, termine), pour
 * les bandeaux de l'interface. step : texte court ("Bind en cours...").
 */
typedef void (*device_setup_progress_cb_t)(uint64_t ieee_addr, const char *step);
void device_setup_register_progress_cb(device_setup_progress_cb_t cb);
void device_setup_register_template_cb(device_setup_template_cb_t cb);

/**
 * Charge tout de suite le template d'un appareil connu, dans la tache
 * appelante (lecture flash : pile en RAM interne). Sert a construire son
 * appareil Matter au demarrage, avant device_setup_boot().
 */
bool device_setup_load_template_now(uint64_t ieee_addr);

#ifdef __cplusplus
}
#endif
