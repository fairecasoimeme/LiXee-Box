#pragma once
// ============================================================================
// Acces internes au composant energy_service (conseils : energy_advice.cpp).
// Implementes dans energy_service.cpp, qui garde les roles, les prix et les
// anneaux d'historique.
// ============================================================================
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/** Tarif en cours du ZLinky configure. */
typedef struct {
    int      family;        // 0 Base, 1 HC/HP, 2 EJP, 3 Tempo
    bool     known;         // tarif en cours identifie
    uint16_t index;         // 256 HC/Base/HN, 258 HP/PM, 260..266 Tempo blanc/rouge
    char     label[24];     // libelle du compteur (PTEC / LTARF)
    double   price;         // EUR/kWh de l'index en cours, 0 si non saisi
    int      today;         // Tempo : 0 inconnu, 1 bleu, 2 blanc, 3 rouge
    int      tomorrow;
    char     next[8];       // prochain changement "22:00", "" si inconnu
} es_tariff_t;

/** false sans ZLinky configure et connu. */
bool es_tariff_now(es_tariff_t *out);

/** Un creneau (heure ou jour) du ZLinky configure. */
typedef struct {
    int64_t wh;             // consommation (somme des index, sous-compteurs inclus)
    int64_t prod_wh;        // production
    int64_t low_wh;         // part rangee sous un index d'heures creuses (256, 260, 264)
    int64_t idx_wh;         // part rangee sous un index tarifaire 256..266
    int64_t color_wh[3];    // Tempo : bleu (256, 258), blanc (260, 262), rouge (264, 266)
    double  eur;            // cout total : energie, taxes, part d'abonnement
} es_slot_t;

/** ago : 0 creneau en cours, 1 le precedent... (heures 0..23, jours 0..30). */
bool es_hour_slot(int ago, es_slot_t *out);
bool es_day_slot(int ago, es_slot_t *out);

/**
 * Mois en cours : consommation, part variable (energie et accise) et part fixe
 * (abonnement et CTA du mois). false sans ZLinky.
 */
bool es_month_cost(int64_t *wh, double *variable_eur, double *fixed_eur);

/** Prix saisi d'un index (256...), 0 si absent. */
double es_price(uint16_t attr);

/** Moyenne des prix saisis des index de l'abonnement, 0 si aucun. */
double es_avg_price(void);

/** IEEE du ZLinky configure, 0 sans ZLinky. */
uint64_t es_zlinky(void);
