#pragma once

// ============================================================================
// Energy Service - calculs de la page Energie (portage statusEnergy V2 de la
// LiXee-Box) : couts (tarifs, abonnement, CSPE, CTA), revenu de production,
// etiquette kWh/m2.an, repartition, jauges, carte tarif, courbe de puissance
// des dernieres 24 h et exports CSV.
//
// Les valeurs instantanees sont lues dans device_data (dernier rapport de
// chaque attribut), sans dependre de Matter. Le contrat JSON est decrit dans
// docs/energy_api.md.
//
// Toutes les fonctions *_json / *_csv renvoient une chaine allouee (PSRAM de
// preference) a liberer par free(), ou NULL en cas de memoire insuffisante.
// ============================================================================

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Alloue la courbe de puissance 24 h (PSRAM) et la recharge depuis la flash. */
esp_err_t energy_service_init(void);

/**
 * A appeler une fois par minute : echantillonne la puissance du ZLinky dans
 * la courbe 24 h, et la sauvegarde periodiquement.
 */
void energy_service_minute_tick(void);

// ----------------------------------------------------------------------------
// Valeurs pour les autres modules (alertes, regles, MQTT...)
// ----------------------------------------------------------------------------

typedef struct {
    bool    valid;              // ZLinky configure et au moins une puissance connue
    bool    connected;          // puissance recue depuis moins de 10 min
    int     mode;               // mode TIC (0 histo mono ... 7 std tri prod)
    bool    tri;
    bool    producer;
    bool    has_phase[3];
    int32_t phase_va[3];        // VA soutires par phase
    int32_t total_va;           // VA soutires (toutes phases)
    int32_t inj_va;             // VA injectes (estimation ou SINSTI)
    bool    has_voltage[3];
    int32_t voltage[3];         // V par phase (URMS)
    int32_t goal_va;            // puissance souscrite, 0 si inconnue
    bool    overload;           // depassement de puissance souscrite (ADPS / STGE)
    char    tariff[24];         // tarif en cours (PTEC / LTARF)
    uint16_t tariff_index;      // attribut d'index en cours (256...)
    double  tariff_price;       // EUR/kWh de l'index en cours
    int     tempo_today;        // 0 aucun, 1 bleu, 2 blanc, 3 rouge
    int     tempo_tomorrow;
} energy_live_t;

/** Dernieres valeurs du ZLinky configure. false si aucun ZLinky. */
bool energy_service_get_live(energy_live_t *out);

typedef enum {
    ENERGY_COST_TODAY = 0,
    ENERGY_COST_YESTERDAY,
    ENERGY_COST_MONTH,
    ENERGY_COST_YEAR,
} energy_cost_period_t;

/**
 * Consommation (Wh, sous-compteurs inclus), cout total (EUR : energie, taxes
 * et part d'abonnement) et production (Wh) d'une periode.
 * Les pointeurs peuvent etre NULL. false si aucun ZLinky.
 */
bool energy_service_period_cost(energy_cost_period_t period, int64_t *wh, double *eur, int64_t *prod_wh);

char *energy_summary_json(const char *period);
char *energy_gauge_json(const char *period);
char *energy_distribution_json(const char *period, bool euro);
/** ieee = 0 : ZLinky configure. */
char *energy_chart_json(const char *period, uint64_t ieee);
char *energy_chart_csv(const char *period, uint64_t ieee);

/**
 * Format historique de /loadEnergyChart (LiXee-Box) : tableau de lignes
 * {"y":"14H","256":120,...,"sub_0":45,"_psub":0.1834,"1":-200}. Index en Wh
 * (part des sous-compteurs retiree), production en negatif, "0" pour un
 * compteur de gaz ou d'eau ; seules les valeurs non nulles figurent.
 */
char *energy_chart_legacy_json(const char *period, uint64_t ieee);
char *energy_power_json(void);
char *energy_power_csv(void);
char *energy_tariff_json(void);

// ----------------------------------------------------------------------------
// Periodes tarifaires du Linky (thermostats : fonctionnement "selon tarif")
// Une periode est designee par l'attribut de son index : 256, 258... 266.
// ----------------------------------------------------------------------------

/** true si un ZLinky est configure et connu. */
bool energy_tariff_available(void);

/** Periode en cours (getCurrentTariffAttributeId de la LiXee-Box), 256 sans ZLinky. */
uint16_t energy_tariff_current_index(void);

/** Periodes de l'abonnement : Base {256}, HC/HP et EJP {256,258}, Tempo {256..266}. */
int energy_tariff_periods(uint16_t *out, int max);

/** Libelle d'une periode selon l'abonnement ("Heures Creuses", "HP Blanc"...), "P<id>" sinon. */
void energy_tariff_label(uint16_t attr, char *out, size_t len);
char *energy_linky_json(void);

#ifdef __cplusplus
}
#endif
