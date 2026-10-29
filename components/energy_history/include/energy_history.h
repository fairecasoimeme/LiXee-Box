#pragma once

// ============================================================================
// Energy History - Historique de consommation energetique
//
// Port de energyHistory.h/cpp + powerHistory.h/cpp Arduino vers ESP-IDF.
//
// Architecture simplifiee par rapport a Arduino (pas de std::map en PSRAM):
//   - PowerHistory: ring buffer de POWER_HISTORY_MAX lectures (60 min = 1h)
//   - EnergyHistory: tableaux fixes pour hours(24), days(31), months(12), years(5)
//   - Persistence JSON dans /littlefs/hst/
//   - Attributs supportes: jusqu'a ENERGY_MAX_ATTRS par device
//
// Les valeurs stockees sont des deltas (consommation par periode),
// sauf "last" et "ref" qui stockent des index absolus du compteur.
//
// Modele de calcul des deltas :
//   ref  = index absolu releve au DEBUT de la periode courante
//   last = dernier index absolu observe
//   delta affiche pour la periode courante = valeur_courante - ref
// 'ref' et son slot sont persistes, de sorte qu'un redemarrage ne perd pas
// la reference et n'ecrase pas le creneau en cours.
// ============================================================================

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Constantes
// ============================================================================

#define ENERGY_MAX_ATTRS         12    // 0x0000 + 10 index tarifaires + marge
#define POWER_HISTORY_MAX        60    // Lectures de puissance (1 par minute = 1h)
#define ENERGY_HOURS             24
#define ENERGY_DAYS              31
#define ENERGY_MONTHS            12
#define ENERGY_YEARS             11      // comme la LiXee-Box
#define ENERGY_HST_DIR           "/littlefs/hst"

// ============================================================================
// Structures - Power History (puissance instantanee)
// ============================================================================

/** Un point de mesure de puissance */
typedef struct {
    char     timestamp[6];     // "HH:MM"
    int32_t  values[ENERGY_MAX_ATTRS];   // Valeurs par attribut
    bool     valid;
} power_record_t;

/** Statistiques par attribut */
typedef struct {
    int32_t min;
    int32_t max;
    int32_t trend;
    int32_t last;
} power_stats_t;

/** Historique de puissance d'un device */
typedef struct {
    power_record_t records[POWER_HISTORY_MAX];
    int            record_count;
    int            write_idx;           // Index circulaire d'ecriture
    power_stats_t  stats[ENERGY_MAX_ATTRS];
    uint16_t       attr_ids[ENERGY_MAX_ATTRS];  // IDs des attributs trackes
    int            attr_count;
    bool           loaded;
} power_history_t;

// ============================================================================
// Structures - Energy History (consommation accumulee)
// ============================================================================

/** Valeurs d'attributs pour une periode */
typedef struct {
    int32_t values[ENERGY_MAX_ATTRS];   // Delta consomme pour cette periode
} energy_slot_t;

/** Donnees d'une granularite de temps (ex: heures) */
typedef struct {
    energy_slot_t graph[31];            // Deltas pour le graphique
    energy_slot_t trend;                // Tendance (dernier delta)
    energy_slot_t last;                 // Dernier index absolu observe
    energy_slot_t ref;                  // Index absolu au DEBUT de la periode courante
    int           slot_count;           // Nombre de slots utilises
    int           ref_slot;             // Slot de la periode courante, -1 si inconnu
    uint16_t      ref_mask;             // Bit a : ref.values[a] exploitable
    uint16_t      last_mask;            // Bit a : last.values[a] renseigne
} energy_period_t;

/** Historique d'energie complet d'un device */
typedef struct {
    uint64_t       ieee_addr;
    energy_period_t hours;              // 24 slots (0-23)
    energy_period_t days;               // 31 slots (1-31, index 0-30)
    energy_period_t months;             // 12 slots (1-12, index 0-11)
    energy_period_t years;              // ENERGY_YEARS slots a partir de base_year
    uint16_t        attr_ids[ENERGY_MAX_ATTRS]; // IDs des attributs trackes
    int             attr_count;
    int             base_year;          // Premiere annee (pour indexer years)
    uint32_t        last_ts[ENERGY_MAX_ATTRS]; // epoch de la derniere mesure par attribut (0 = inconnue)
    bool            dirty;
    bool            loaded;
} energy_history_t;

// ============================================================================
// API publique - Gestion globale
// ============================================================================

/**
 * Initialise le module energy history.
 * Cree le repertoire /littlefs/hst/ si necessaire.
 */
esp_err_t energy_history_init(void);

/**
 * Obtient ou cree l'historique d'energie d'un device.
 * @param ieee_addr Adresse IEEE du device
 * @return Pointeur vers l'historique (alloue en PSRAM si nouveau)
 */
energy_history_t* energy_history_get_or_create(uint64_t ieee_addr);

/**
 * Obtient l'historique de puissance d'un device.
 */
power_history_t* power_history_get_or_create(uint64_t ieee_addr);

/**
 * Supprime l'historique d'un device (RAM + fichiers).
 */
esp_err_t energy_history_remove(uint64_t ieee_addr);

// ============================================================================
// API publique - Lecture (service energie, pages web)
// ============================================================================

typedef enum {
    ENERGY_GRAN_HOUR = 0,
    ENERGY_GRAN_DAY,
    ENERGY_GRAN_MONTH,
    ENERGY_GRAN_YEAR,
} energy_gran_t;

/** Historique d'energie deja en memoire, sans le creer. NULL si absent. */
const energy_history_t* energy_history_find(uint64_t ieee_addr);

/**
 * Energie consommee (Wh) dans un creneau pour un attribut.
 * Creneaux : heure 0-23, jour 0-30 (jour du mois - 1), mois 0-11, annee
 * 0-4 (annee - base_year). 0 si l'attribut ou le creneau n'existe pas.
 */
int32_t energy_history_slot_value(const energy_history_t *h, energy_gran_t gran,
                                  int slot, uint16_t attr_id);

/** true si l'attribut est suivi par cet historique. */
bool energy_history_has_attr(const energy_history_t *h, uint16_t attr_id);

// ============================================================================
// API publique - Ajout de mesures
// ============================================================================

/**
 * Ajoute une mesure d'energie (valeur absolue du compteur).
 * Calcule automatiquement les deltas pour chaque granularite.
 *
 * @param ieee_addr  Adresse IEEE du device
 * @param attr_id    ID de l'attribut (ex: 0, 256, 258...)
 * @param value      Valeur absolue du compteur (Wh)
 */
/**
 * Comme energy_history_add_measurement, avec le lissage des trous choisi :
 * apres plus de 45 min sans mesure (coupure, pont eteint), l'energie du trou
 * est repartie sur les creneaux manques au lieu d'etre comptee en bloc au
 * retour. A desactiver pour les compteurs a impulsions (gaz, eau), comme la
 * LiXee-Box. Un saut de plus de 100 kWh sans trou est ecarte (releve aberrant).
 */
/**
 * Relit le fichier d'historique d'energie d'un appareil (apres son edition ou
 * son import par l'editeur de fichiers), sinon la sauvegarde periodique le
 * remplacerait par la copie en memoire. Lecture flash : tache HTTP ou principale.
 */
esp_err_t energy_history_reload(uint64_t ieee_addr);

esp_err_t energy_history_add_measurement_ex(uint64_t ieee_addr, uint16_t attr_id,
                                             int32_t value, bool gap_smoothing);

esp_err_t energy_history_add_measurement(uint64_t ieee_addr,
                                          uint16_t attr_id,
                                          int32_t value);

/**
 * Ajoute une mesure de puissance instantanee.
 *
 * @param ieee_addr  Adresse IEEE du device
 * @param attr_id    ID de l'attribut (ex: 1295=active_power, 2575=apparent)
 * @param value      Valeur en W (ou mW selon le cluster)
 */
esp_err_t power_history_add_measurement(uint64_t ieee_addr,
                                         uint16_t attr_id,
                                         int32_t value);

// ============================================================================
// API publique - Reset periodique (equivalent ScanDevicesToRAZ)
// ============================================================================

/**
 * Reset horaire: remet a zero les deltas de l'heure suivante.
 * Appele quand Minute == 0.
 * @param hour Heure courante (0-23)
 */
esp_err_t energy_history_reset_hourly(int hour);

/**
 * Reset journalier: remet a zero les deltas du jour suivant.
 * Appele quand Hour == 0 && Minute == 0.
 * @param day Jour courant (1-31)
 */
esp_err_t energy_history_reset_daily(int day);

/**
 * Reset mensuel: remet a zero les deltas du mois suivant.
 * Appele quand Day == 1 && Hour == 0.
 * @param month Mois courant (1-12)
 */
esp_err_t energy_history_reset_monthly(int month);

// ============================================================================
// API publique - Persistence
// ============================================================================

/**
 * Sauvegarde tous les historiques modifies.
 */
esp_err_t energy_history_save_all(void);

/**
 * Charge tous les historiques depuis le filesystem.
 */
esp_err_t energy_history_load_all(void);

// ============================================================================
// API publique - Serialisation JSON (pour web API)
// ============================================================================

/**
 * Serialise l'historique d'energie en JSON pour le frontend.
 * Format compatible avec loadEnergyChart (Morris.js):
 * [{"y":"08H","256":1500,"258":200}, ...]
 *
 * @param ieee_addr Adresse IEEE
 * @param period    "hour", "day", "month", "year"
 * @return JSON string (caller doit free)
 */
char* energy_history_to_json(uint64_t ieee_addr, const char *period);

/**
 * Serialise la puissance en JSON pour le frontend.
 * Format Chart.js: {"labels":["08:00",...], "datasets":[{"data":[100,...]},{"data":[...]}]}
 *
 * @param ieee_addr Adresse IEEE
 * @return JSON string (caller doit free)
 */
char* power_history_to_chartjs_json(uint64_t ieee_addr);

/**
 * Serialise l'historique d'usage en JSON Chart.js.
 * Format: {"labels":["08H",...], "datasets":[{"data":[1500,...]},{"data":[200,...]}]}
 *
 * @param ieee_addr Adresse IEEE
 * @param period    "hour", "day", "month", "year"
 * @return JSON string (caller doit free)
 */
char* energy_history_to_chartjs_json(uint64_t ieee_addr, const char *period);

#ifdef __cplusplus
}
#endif
