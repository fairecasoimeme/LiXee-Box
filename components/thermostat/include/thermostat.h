#pragma once
// ============================================================================
// Thermostats virtuels (portage de virtualThermostat.cpp de la LiXee-Box v2.25)
//
// Jusqu'a 8 zones. Chaque zone lit un capteur de temperature Zigbee et pilote
// un appareil principal (prise on/off, clim, radiateur fil pilote) plus des
// prises supplementaires. Regulation toutes les 30 s :
//   - prise on/off : TPI (temps de chauffe proportionnel sur un cycle) ;
//   - appareil pilote par actions de template (HEAT/COOL/OFF, CONFORT/ECO...) :
//     hysteresis de 0,3 degC, commande envoyee au seul changement ;
//   - hors-gel prioritaire (chauffage), marche / arret forces, suspension
//     fenetre ouverte, absence (presence) ou hors plage horaire / tarif Linky ;
//   - anti court-cycle (temps ON / OFF minimaux).
// Configuration : /littlefs/config/thermostats.json (format de la LiXee-Box).
// ============================================================================

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define THERMO_MAX_ZONES    8
#define THERMO_MAX_OPEN     4
#define THERMO_MAX_EXTRA    4

/** Charge la configuration et demarre la regulation (premier passage a 30 s). */
esp_err_t thermostat_init(void);

/**
 * Pont Matter demarre : chaque zone devient un thermostat ponte (temperature,
 * consigne, mode arret / chaud / froid), pilotable depuis le controleur.
 */
void thermostat_matter_start(void);

/** Republie les zones dans Matter (zone retiree ou remise). */
void thermostat_matter_refresh(void);

/** Rapport d'occupation (0406/0000) : alimente la presence des zones. */
void thermostat_note_occupancy(uint64_t ieee, bool occupied);

/**
 * Presence vue dans les 30 dernieres minutes (detection positive), pour un
 * capteur ou, ieee = 0, pour n'importe lequel (/getPresenceStatus de la Box).
 */
bool thermostat_presence_recent(uint64_t ieee);

/** Etat des zones (/loadThermostats de la LiXee-Box), a liberer par free(). */
char *thermostat_status_json(void);

/** Configuration des zones (pages Config), a liberer par free(). */
char *thermostat_config_json(void);

/** Appareils proposables par role, periodes tarifaires (formulaire), a liberer par free(). */
char *thermostat_options_json(void);

/** Noms des actions du template d'un appareil (/deviceActions), a liberer par free(). */
char *thermostat_device_actions_json(uint64_t ieee);

/**
 * Cree (id < 0) ou met a jour une zone depuis le formulaire (cles du fichier
 * de configuration). @return id de la zone, ou -1 (zones pleines, id invalide)
 */
int thermostat_save_zone(int id, const cJSON *form);

bool thermostat_set_setpoint(int id, float value);
bool thermostat_set_force(int id, int mode);       // 0 auto, 1 marche, 2 arret
bool thermostat_set_mode(int id, bool heating);
bool thermostat_set_frost(int id, bool on);
bool thermostat_toggle(int id);
bool thermostat_delete(int id);

#ifdef __cplusplus
}
#endif
