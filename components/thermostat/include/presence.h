#pragma once
// ============================================================================
// Historique de presence (portage de presence.cpp de la LiXee-Box)
//
// Pour chaque capteur de presence et chaque jour du mois (1..31), 24 indicateurs
// horaires : 1 si au moins une detection positive a eu lieu dans l'heure.
// Anneau mensuel : le creneau [jour][heure] est remis a 0 au debut de l'heure,
// les jours suivants gardent ceux du mois precedent (graphe sur 30 jours).
// Fichier : /littlefs/hst/presence.json (format de la LiXee-Box).
// Sert aux barres de presence du graphe "Usage d'electricite".
// ============================================================================

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Charge l'historique (tache principale : lecture de la flash). */
void presence_init(void);

/**
 * Detection d'occupation (0406/0000). Seuls les evenements positifs sont
 * enregistres, pour le capteur configure (Config Energie) ou un modele de
 * capteur de presence reconnu. RAM seulement : appelable depuis toute tache.
 */
void presence_note(uint64_t ieee, bool occupied);

/** Remise a zero horaire et enregistrement differe (tache des thermostats). */
void presence_tick(void);

/**
 * Donnees du graphe, a liberer par free() :
 *   time "hour" : {"enabled":b,"hour":[24 x 0|1]} (index 0 = heure courante + 1 de la veille)
 *   time "day"  : {"enabled":b,"day":{"1":0|1,...,"31":0|1}}
 * enabled : capteur configure et graphique de presence active (Config Energie).
 */
char *presence_chart_json(const char *time);

#ifdef __cplusplus
}
#endif
