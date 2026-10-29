#pragma once
// ============================================================================
// Profil Matter d'un appareil, deduit de son template LiXee-Box
//
// Chaque etat ("status") du template designe un attribut Zigbee (cluster,
// attribut, coefficient, unite, mqtt_device_class). Le profil relie chacun de
// ceux qui ont un equivalent Matter a sa cible (cluster/attribut Matter) et
// en deduit les capacites de l'endpoint : un capteur temperature + humidite +
// batterie devient UN endpoint avec les types d'appareil correspondants.
//
// Liaison :
//   - clusters ZCL standard (0402, 0405, 0406, 0500, 0001, 0102, 0201, 0202,
//     0702, 0B04...) : par (cluster, attribut), les autres attributs de ces
//     clusters ne sont pas exposes (ex. tension phase 2 du ZLinky : Matter n'a
//     qu'une tension par endpoint) ;
//   - clusters fabricant (EF00 Tuya, FCxx) et Analog Input (000C) : par
//     mqtt_device_class, a defaut par l'unite ou le nom de l'etat. Un etat
//     inscriptible n'y est expose que s'il s'agit d'une consigne (les autres
//     sont des reglages : calibration, seuils...).
//
// Valeur : valeur physique du template (brute x coefficient, dans l'unite du
// template), ramenee a l'unite de base de la cible (V, A, W, VA, Wh, degC, %,
// lux, hPa) puis convertie dans l'unite Matter.
//
// Commandes Matter -> Zigbee : par les actions du template (marche/arret,
// volet, modes et consignes de climatisation) ; a defaut, par ecriture de
// l'attribut lie (ou du datapoint Tuya).
// ============================================================================

#include <stdbool.h>
#include <stdint.h>
#include "device_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ML_NONE = 0,
    ML_ONOFF,
    ML_TEMPERATURE,
    ML_HUMIDITY,
    ML_PRESSURE,
    ML_ILLUMINANCE,         // lux
    ML_ILLUMINANCE_ZCL,     // 0400/0000 : deja au format Matter (10000 log10(lux) + 1)
    ML_OCCUPANCY,
    ML_CONTACT,             // BooleanState : vrai = contact ferme
    ML_LEAK,                // BooleanState : vrai = fuite
    ML_BAT_PERCENT,
    ML_BAT_VOLTAGE,
    ML_COVER_LIFT,          // fermeture en %, convention ZCL (0 = ouvert)
    ML_TH_LOCAL_TEMP,
    ML_TH_HEAT_SP,
    ML_TH_COOL_SP,
    ML_TH_SYSTEM_MODE,
    ML_TH_RUNNING,
    ML_FAN_MODE,
    ML_EL_ACTIVE_POWER,
    ML_EL_APPARENT_POWER,
    ML_EL_TOTAL_APPARENT,
    ML_EL_VOLTAGE,
    ML_EL_CURRENT,
    ML_EL_ENERGY,           // energie cumulee importee
    ML_SWITCH_PRESS,        // bouton : un appui par rapport
    ML_SWITCH_MULTI,        // bouton : valeur = nombre d'appuis (0 = appui long)
} matter_link_target_t;

// Capacites de l'endpoint (types d'appareil / clusters Matter a creer)
#define MCAP_ONOFF        (1u << 0)
#define MCAP_TEMPERATURE  (1u << 1)
#define MCAP_HUMIDITY     (1u << 2)
#define MCAP_PRESSURE     (1u << 3)
#define MCAP_ILLUMINANCE  (1u << 4)
#define MCAP_OCCUPANCY    (1u << 5)
#define MCAP_CONTACT      (1u << 6)
#define MCAP_LEAK         (1u << 7)
#define MCAP_BATTERY      (1u << 8)
#define MCAP_COVER        (1u << 9)
#define MCAP_THERMOSTAT   (1u << 10)
#define MCAP_FAN          (1u << 11)
#define MCAP_ELECTRICAL   (1u << 12)   // puissance, tension, courant
#define MCAP_ENERGY       (1u << 13)   // energie cumulee importee
#define MCAP_SWITCH       (1u << 14)

#define MATTER_PROFILE_MAX_LINKS  32

typedef struct {
    uint16_t cluster;
    uint16_t attr;
    uint8_t  target;        // matter_link_target_t
    bool     invert;        // ML_CONTACT : etat Zigbee 1 = ouvert (zone IAS)
    bool     writable;      // etat inscriptible du template
    uint8_t  write_type;    // type ZCL d'ecriture (type de DP pour EF00), 0 = defaut
    uint16_t mfr;           // code fabricant de l'etat
    uint16_t energy_attr;   // ML_EL_ENERGY : attribut 0702 transmis (0x0000 hors 0702)
    float    scale;         // physique = brute x scale (coefficient du template)
    float    unit_mul;      // unite du template -> unite de base de la cible
} matter_link_t;

/** Action de template retenue pour une commande Matter. */
typedef struct {
    bool     valid;
    uint16_t command;
    uint8_t  endpoint;      // 0 : endpoint de l'appareil
    int32_t  value;
    uint16_t cluster;
    uint16_t mfr;
} matter_cmd_t;

typedef struct {
    uint32_t      caps;
    bool          light;            // MCAP_ONOFF : lampe (sinon prise)
    bool          heat;             // MCAP_THERMOSTAT : consigne de chauffage
    bool          cool;             //                   consigne de refroidissement
    bool          cover_inverted;   // volet dont la position remontee est l'ouverture (252)
    bool          zlinky;
    int           link_count;
    matter_link_t links[MATTER_PROFILE_MAX_LINKS];
    matter_cmd_t  on, off;
    matter_cmd_t  cover_up, cover_down, cover_stop, cover_pos;
    matter_cmd_t  sys_mode;         // 310 : valeur = mode
    matter_cmd_t  heat_sp;          // 315
    matter_cmd_t  cool_sp;          // 314
    matter_cmd_t  both_sp;          // 311
    matter_cmd_t  fan_mode;         // 312
} matter_profile_t;

/**
 * Role de l'index d'energie (0702) d'un appareil, d'apres la configuration
 * Energie : un compteur d'impulsions (ZiPulses) compte l'eau, le gaz ou
 * l'electricite selon son branchement, et son template ne peut pas le dire.
 */
typedef enum {
    MATTER_METER_AUTO = 0,      // non affecte : l'unite du template decide
    MATTER_METER_ELECTRIC,      // sous-compteur electrique / production : energie Matter
    MATTER_METER_OTHER,         // compteur de gaz ou d'eau : rien en Matter 1.4
} matter_meter_role_t;

/**
 * Construit le profil d'un appareil (alloue en PSRAM, a liberer par free()).
 * A appeler par la tache qui vient de charger le template (pas d'acces
 * concurrent) : le profil est une copie, il ne relit plus le template.
 * @return NULL si le template est absent ou si aucun etat n'a d'equivalent Matter
 */
matter_profile_t *matter_profile_build(const devdata_template_t *tmpl, const char *model,
                                       uint16_t zcl_device_id, matter_meter_role_t meter_role);

/**
 * Valeur numerique brute d'un rapport (entiers signes ou non, booleen, enum,
 * bitmap, flottant simple precision), octets dans l'ordre de la ZiGate
 * (big-endian). false pour une chaine ou une donnee trop courte.
 */
bool matter_profile_raw_value(const uint8_t *data, uint16_t len, uint8_t zcl_type, double *out);

/** Nom court d'une cible (journal). */
const char *matter_link_target_name(uint8_t target);

#ifdef __cplusplus
}
#endif
