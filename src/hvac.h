#ifndef HVAC_H
#define HVAC_H

#include <Arduino.h>

// ============================================================================
// HVAC Zigbee Actions - Cluster 0x0201 (Thermostat) & 0x0202 (Fan Control)
// Pour contrôleurs IR climatisation type OWON AC201, NodOn, etc.
// ============================================================================

// --- Définitions des clusters ---
#define CLUSTER_HVAC_THERMOSTAT   0x0201
#define CLUSTER_HVAC_FAN_CONTROL  0x0202

// --- Attributs Thermostat (0x0201) ---
#define ATTR_LOCAL_TEMPERATURE          0x0000  // int16, °C × 100
#define ATTR_OCCUPIED_COOLING_SETPOINT  0x0011  // int16, °C × 100
#define ATTR_OCCUPIED_HEATING_SETPOINT  0x0012  // int16, °C × 100
#define ATTR_SYSTEM_MODE                0x001C  // enum8
#define ATTR_AC_LOUVER_POSITION         0x0042  // enum8

// --- Attributs Fan Control (0x0202) ---
#define ATTR_FAN_MODE                   0x0000  // enum8

// --- Modes Système HVAC (attribut 0x001C) ---
#define HVAC_MODE_OFF         0   // Éteint
#define HVAC_MODE_AUTO        1   // Automatique
#define HVAC_MODE_COOL        3   // Climatisation (froid)
#define HVAC_MODE_HEAT        4   // Chauffage
#define HVAC_MODE_EMERGENCY   5   // Chauffage d'urgence
#define HVAC_MODE_PRECOOLING  6   // Pré-refroidissement
#define HVAC_MODE_FAN_ONLY    7   // Ventilation seule
#define HVAC_MODE_DRY         8   // Déshumidification
#define HVAC_MODE_SLEEP       9   // Mode nuit

// --- Modes Ventilateur (cluster 0x0202, attribut 0x0000) ---
#define FAN_MODE_OFF          0   // Éteint
#define FAN_MODE_LOW          1   // Vitesse basse
#define FAN_MODE_MEDIUM       2   // Vitesse moyenne
#define FAN_MODE_HIGH         3   // Vitesse haute
#define FAN_MODE_ON           4   // Allumé (vitesse par défaut)
#define FAN_MODE_AUTO         5   // Automatique
#define FAN_MODE_SMART        6   // Intelligent

// --- Positions Volet AC (attribut 0x0042) ---
#define LOUVER_CLOSED           0   // Fermé
#define LOUVER_FULL_OPEN        1   // Complètement ouvert
#define LOUVER_QUARTER_OPEN     2   // 1/4 ouvert
#define LOUVER_HALF_OPEN        3   // 1/2 ouvert
#define LOUVER_THREE_QUARTER    4   // 3/4 ouvert

// --- Codes de commande pour SendAction() ---
#define ACTION_HVAC_SYSTEM_MODE       310   // Changer le mode système
#define ACTION_HVAC_SETPOINT_BOTH     311   // Consigne heating + cooling
#define ACTION_HVAC_FAN_MODE          312   // Mode ventilateur
#define ACTION_HVAC_LOUVER_POSITION   313   // Position volet
#define ACTION_HVAC_COOLING_SETPOINT  314   // Consigne froid uniquement
#define ACTION_HVAC_HEATING_SETPOINT  315   // Consigne chaud uniquement

// ============================================================================
// Prototypes des fonctions
// ============================================================================

/**
 * @brief Envoie une commande de changement de mode HVAC
 * @param shortAddr Adresse courte du device
 * @param endpoint Endpoint (généralement 1)
 * @param mode Mode HVAC (HVAC_MODE_OFF, HVAC_MODE_COOL, etc.)
 */
void SendHVACSystemMode(int shortAddr, int endpoint, uint8_t mode);

/**
 * @brief Envoie une consigne de température (heating et cooling)
 * @param shortAddr Adresse courte du device
 * @param endpoint Endpoint (généralement 1)
 * @param setpoint Température en centièmes de degré (2200 = 22.00°C)
 */
void SendHVACSetpointBoth(int shortAddr, int endpoint, int16_t setpoint);

/**
 * @brief Envoie une consigne de température spécifique
 * @param shortAddr Adresse courte du device
 * @param endpoint Endpoint (généralement 1)
 * @param setpoint Température en centièmes de degré (2200 = 22.00°C)
 * @param cooling true pour cooling setpoint, false pour heating setpoint
 */
void SendHVACSetpoint(int shortAddr, int endpoint, int16_t setpoint, bool cooling);

/**
 * @brief Envoie une commande de vitesse de ventilateur
 * @param shortAddr Adresse courte du device
 * @param endpoint Endpoint (généralement 1)
 * @param fanMode Mode ventilateur (FAN_MODE_OFF, FAN_MODE_AUTO, etc.)
 */
void SendHVACFanMode(int shortAddr, int endpoint, uint8_t fanMode);

/**
 * @brief Envoie une commande de position du volet AC
 * @param shortAddr Adresse courte du device
 * @param endpoint Endpoint (généralement 1)
 * @param position Position (LOUVER_CLOSED, LOUVER_FULL_OPEN, etc.)
 */
void SendHVACLouverPosition(int shortAddr, int endpoint, uint8_t position);

/**
 * @brief Action HVAC dispatcher (appelée depuis SendAction)
 * @param shortAddr Adresse courte du device
 * @param endpoint Endpoint
 * @param command Commande (310-315)
 * @param value Valeur
 */
void SendHVACAction(int shortAddr, int endpoint, int command, int value);

/**
 * @brief Convertit une température en degrés vers centièmes
 * @param tempCelsius Température en degrés (ex: 22.5)
 * @return Température en centièmes (ex: 2250)
 */
inline int16_t TempToSetpoint(float tempCelsius) {
    return (int16_t)(tempCelsius * 100.0f);
}

/**
 * @brief Convertit une valeur d'attribut en température
 * @param setpoint Valeur en centièmes (ex: 2250)
 * @return Température en degrés (ex: 22.5)
 */
inline float SetpointToTemp(int16_t setpoint) {
    return (float)setpoint / 100.0f;
}

#endif // HVAC_H