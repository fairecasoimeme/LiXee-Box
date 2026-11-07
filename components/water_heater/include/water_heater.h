#pragma once
// ============================================================================
// Chauffe-eau virtuels : meme principe que les thermostats virtuels
// (components/thermostat/), simplifie pour un ballon d'eau chaude pilote par
// un contacteur/relais Zigbee (on/off, cluster 0x0006) :
//   - sonde de temperature optionnelle (beaucoup de ballons n'en ont pas) :
//     regulation TPI en boucle fermee si presente, sinon pilotage "ouvert"
//     (le contacteur suit juste le mode/la plage horaire) ;
//   - 3 modes fixes Off / Marche forcee / Programme (heures creuses, tarif
//     Linky), qui correspondent 1:1 aux 3 valeurs CurrentMode exposees par le
//     cluster Matter Water Heater Mode (0x009E) cote pont (voir
//     components/matter_bridge/include/water_heater_delegate.h : WH_MODE_*
//     doit rester en phase avec WATERHEATER_MODE_* ci-dessous) ;
//   - boost temporaire (cluster Water Heater Management 0x0094, commande
//     Boost) : marche forcee pour une duree donnee, retour automatique au
//     mode precedent a l'echeance.
// Configuration : /littlefs/config/waterheaters.json.
// ============================================================================

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATERHEATER_MAX_ZONES   4
#define WATERHEATER_MAX_EXTRA   4

// Valeurs CurrentMode (doivent rester synchronisees avec WH_MODE_* de
// water_heater_delegate.h, cote pont Matter)
#define WATERHEATER_MODE_OFF     0
#define WATERHEATER_MODE_MANUAL  1
#define WATERHEATER_MODE_TIMED   2

/** Charge la configuration et demarre la regulation (premier passage a 30 s). */
esp_err_t water_heater_init(void);

/** Pont Matter demarre : chaque zone devient un endpoint Water Heater (0x050F). */
void water_heater_matter_start(void);

/** Republie les zones dans Matter (zone retiree ou remise). */
void water_heater_matter_refresh(void);

/** Etat des zones (page web), a liberer par free(). */
char *water_heater_status_json(void);

/** Configuration des zones (page Config), a liberer par free(). */
char *water_heater_config_json(void);

/** Appareils proposables par role (formulaire), a liberer par free(). */
char *water_heater_options_json(void);

/**
 * Cree (id < 0) ou met a jour une zone depuis le formulaire. @return id de la
 * zone, ou -1 (zones pleines, id invalide)
 */
int water_heater_save_zone(int id, const cJSON *form);

bool water_heater_set_setpoint(int id, float value);
bool water_heater_set_mode(int id, int mode);     // WATERHEATER_MODE_*
bool water_heater_toggle(int id);                 // bascule Off <-> Manual

/**
 * Boost (Water Heater Management / Boost) : marche forcee pour duration_s
 * secondes. one_shot : fin anticipee des que la consigne est atteinte (sans
 * effet si la zone n'a pas de sonde). has_temp : temperature cible propre au
 * boost (sinon la consigne normale de la zone).
 */
bool water_heater_boost(int id, uint32_t duration_s, bool one_shot, bool emergency,
                        bool has_temp, float temp_c);
bool water_heater_cancel_boost(int id);

bool water_heater_delete(int id);

#ifdef __cplusplus
}
#endif
