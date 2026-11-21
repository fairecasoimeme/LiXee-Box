#pragma once
// ============================================================================
// Historique de temperature du graphe "Usage d'electricite"
//
// Sondes Zigbee (0x0402) choisies dans Config Energie > Temperature (4 au plus).
// Chaque minute, la derniere temperature connue de chaque sonde est relevee
// (une sonde muette depuis plus de 3 h est ignoree) ; la moyenne des releves
// d'une heure donne la valeur de l'heure. Une courbe par sonde.
//
// Anneau mensuel par sonde : 31 jours x 24 h, chaque jour date (aaaammjj) pour
// ne jamais afficher une valeur du mois precedent.
// Fichier : /littlefs/hst/temperature.json.
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

/** Charge l'historique (tache principale : lecture de la flash). */
void temp_history_init(void);

/** Releve minute, passage d'heure et enregistrement differe (tache des thermostats). */
void temp_history_tick(void);

/**
 * Donnees du graphe, a liberer par free() :
 *   time "hour" : {"enabled":b,"sensors":[{"ieee","name","hour":[24 x °C|null]}]}
 *                 (index 0 = heure courante + 1 de la veille, 23 = heure courante)
 *   time "day"  : {"enabled":b,"sensors":[{"ieee","name","day":{"jj/mm":{"avg","min","max"}}}]}
 *                 (30 derniers jours ; min et max des moyennes horaires)
 * enabled : au moins une sonde configuree et graphique active (Config Energie).
 */
char *temp_history_chart_json(const char *time);

#ifdef __cplusplus
}
#endif
