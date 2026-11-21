#pragma once
// ============================================================================
// Conseils et economies (ECONOMIES.md) : ce que coute l'electricite en ce
// moment, le risque de coupure, la consommation de fond, l'abonnement, le
// mois en cours, et la comparaison des options du tarif reglemente.
//
// Tout est calcule par la box, a partir de ce qu'elle mesure et de sa
// configuration ; la page web, LiXee-Assist et MQTT affichent le meme
// resultat. Portage des regles de LiXee-Assist (energy_advice.dart,
// tariff_compare.dart), avec les definitions validees dans ECONOMIES.md.
// ============================================================================
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * GET /api/energy/advice, sans les alertes de la box (ajoutees par le serveur
 * web). Une section non calculable est absente. A liberer par free().
 */
char *energy_advice_json(void);

/** Meteo de l'electricite et cout de l'instant seuls (bandeau du tableau de bord). */
char *energy_advice_weather_json(void);

/** Niveaux courts, pour MQTT : chaines vides si inconnus. */
typedef struct {
    char weather[12];       // surplus, cheap, flat, normal, expensive, peak
    char cut_risk[8];       // low, medium, high
    int  base_load_w;       // -1 si inconnu
} energy_advice_levels_t;

void energy_advice_levels(energy_advice_levels_t *out);

/**
 * Heures completes consecutives, jusqu'a la derniere, ou l'eau a coule sans
 * interruption, si la serie couvre une heure de nuit (1 h a 5 h) ; 0 sinon.
 * -1 sans compteur d'eau.
 */
int energy_advice_water_running_hours(void);

/** Chaque minute (tache principale) : couleur Tempo du jour enregistree. */
void energy_advice_minute_tick(void);

#ifdef __cplusplus
}
#endif
