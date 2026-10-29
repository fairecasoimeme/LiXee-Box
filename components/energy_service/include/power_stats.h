#pragma once

// ============================================================================
// Statistiques de puissance du ZLinky (powerStats.cpp de la LiXee-Box v2.26)
//
// Trois niveaux, un pic isole ne disant rien du risque de coupure (le Linky
// coupe sur un depassement d'environ une minute) :
//  - pic instantane : la plus forte puissance apparente mesuree ;
//  - moyenne sur 1 min glissante : ce qui fait couper le Linky ;
//  - plateau sur 10 min : la charge durable, pour juger de l'abonnement.
// En Standard s'y ajoute SMAXSN du jour (0B04/1293), la veille close par
// SMAXSN-1 (FF66/530..532). Triphase : tout aussi par phase, rapporte au tiers
// de la puissance souscrite. Minutes par tranche de charge (50..100 %), par
// jour (90 j) et par mois (24 mois), plancher nocturne (1 h - 5 h) et heures
// de bascule tarifaire observees.
// Persistance : /littlefs/hst/powerStats.json (format de la Box), une fois par heure.
// ============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Allocation (PSRAM) et relecture du fichier : tache principale. */
void power_stats_init(void);

/** Traitement toutes les 10 s : tache principale (ecrit la flash une fois par heure). */
void power_stats_loop(void);

/** Rapport d'un attribut du ZLinky configure (tache ZiGate / LoRa, RAM seule). */
void power_stats_on_attribute(uint64_t ieee, uint16_t cluster, uint16_t attr, int64_t value);

/** JSON de /api/power/peaks, /api/power/floor et /api/power/summary (a liberer par free()). */
char *power_stats_peaks_json(int days);
char *power_stats_floor_json(int days);
char *power_stats_summary_json(void);

/** Charge de la derniere minute en % de la limite (phase la plus chargee en triphase), -1 si inconnue. */
int power_stats_minute_load_pct(void);

/** Prochain changement de tarif observe "HH:MM" ; false si inconnu (Base, pas assez d'observations). */
bool power_stats_next_change(char *out, size_t len);

#ifdef __cplusplus
}
#endif
