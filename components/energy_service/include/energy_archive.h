#pragma once
// ============================================================================
// Archive de l'historique d'energie (page Energie > Historique)
//
// Les anneaux d'energy_history ne gardent que 24 heures, 31 jours, 12 mois et
// 11 annees. L'archive en recopie le contenu, date, dans des fichiers de
// /littlefs/hst (repertoire sauvegarde et restaure tel quel) :
//   hr_aaaammjj.json : une journee heure par heure (gardee 400 jours)
//   dy_aaaamm.json   : un mois jour par jour (garde sans limite)
//   mo_aaaa.json     : une annee mois par mois
//   yr.json          : les annees
// Format : {"e":{"<IEEE>":{"<creneau>":{"<attribut>":Wh,...}}}} ; le creneau
// est l'heure (0-23), le jour du mois (1-31), le mois (1-12) ou l'annee.
// Valeurs brutes de chaque compteur : la page recalcule index, sous-compteurs
// et production comme le graphe d'usage.
//
// D'autres modules (temperature, presence) ajoutent leurs donnees aux memes
// fichiers par un fournisseur (energy_archive_register).
//
// Recopie toutes les 15 minutes et a chaque changement d'heure ; un fichier
// n'est reecrit que si son contenu change.
// ============================================================================

#include <stdint.h>
#include <stdbool.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fournisseur de donnees : appele pour la veille et le jour courant (aaaammjj),
 * avec le fichier de la journee (hr_) et celui de son mois (dy_), a completer
 * sous sa propre cle. Tache principale.
 */
typedef void (*energy_archive_provider_t)(uint32_t ymd, cJSON *hour_file, cJSON *day_file);

void energy_archive_register(energy_archive_provider_t fn);

/** Inventaire des jours archives (tache principale, apres energy_history_load_all). */
void energy_archive_init(void);

/** Chaque minute, tache principale, APRES energy_history_reset_hourly() (debut de periode). */
void energy_archive_tick(void);

/** Recopie immediate (avant un redemarrage). */
void energy_archive_flush(void);

/** Efface les donnees d'energie de l'archive (historique efface), a la prochaine minute. */
void energy_archive_request_clear(void);

/**
 * Fichier d'archive analyse, a liberer par cJSON_Delete ; NULL s'il n'existe pas.
 * kind : 'h' (key aaaammjj), 'd' (aaaamm), 'm' (aaaa), 'y' (key ignoree).
 */
cJSON *energy_archive_load(char kind, uint32_t key);

/**
 * Jours disponibles, a liberer par free() :
 * {"hour":[[du,au],...],"day":[[du,au],...]} (plages aaaammjj inclusives).
 */
char *energy_archive_days_json(void);

/** Numero de jour (jours depuis le 01/01/1970) d'une date aaaammjj, et inverse. */
int32_t energy_archive_daynum(uint32_t ymd);
uint32_t energy_archive_ymd(int32_t daynum);

#ifdef __cplusplus
}
#endif
