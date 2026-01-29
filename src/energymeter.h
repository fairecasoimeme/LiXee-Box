#ifndef ENERGYMETER_H
#define ENERGYMETER_H

#include <Arduino.h>
#include <stdint.h>

// ============================================================================
// TYPES TUYA DATAPOINT (définis dans thermostat.h, repris ici pour standalone)
// ============================================================================
#ifndef TUYA_TYPE_RAW
#define TUYA_TYPE_RAW       0
#define TUYA_TYPE_BOOL      1
#define TUYA_TYPE_VALUE     2
#define TUYA_TYPE_STRING    3
#define TUYA_TYPE_ENUM      4
#define TUYA_TYPE_BITMAP    5
#endif

// ============================================================================
// TYPES DE COMPTEURS D'ENERGIE TUYA
// ============================================================================
enum TuyaEnergyMeterType {
    TUYA_METER_SPM01,           // Monophasé Zemismart - DP: 1=energy, 6=phase_raw
    TUYA_METER_SPM02,           // Triphasé Zemismart - DP: 1=energy, 6/7/8=phase_raw, 15=reverse
    TUYA_METER_SPM02_DETAILED,  // Triphasé détaillé - DP: 101-111 (fréquence, V, I, P par phase)
    TUYA_METER_DDS238,          // Compteur DIN rail
    TUYA_METER_HIKING,          // Compteur DIN power (Hiking)
    TUYA_METER_UNKNOWN
};

// ============================================================================
// STRUCTURE POUR LES DONNEES DE PHASE DECODEES
// ============================================================================
struct PhaseData {
    float voltage;      // V
    float current;      // A  
    float power;        // W
    bool valid;
};

// ============================================================================
// FONCTIONS GESTIONNAIRES DE CLUSTERS
// ============================================================================

/**
 * @brief Gestionnaire principal cluster Tuya EF00 pour compteurs d'énergie
 * 
 * @param inifile Fichier INI du device (IEEE.ini)
 * @param attribute Attribut Tuya (généralement 0 pour status report)
 * @param datatype Type de données ZCL
 * @param len Longueur des données
 * @param datas Données brutes
 */
void tuyaEnergyMeterManage(String inifile, int attribute, uint8_t datatype, 
                           int len, char* datas);

// ============================================================================
// FONCTIONS UTILITAIRES
// ============================================================================

/**
 * @brief Détermine le type de compteur Tuya selon le manufacturer
 */
TuyaEnergyMeterType getTuyaEnergyMeterType(const String& manufacturer);

/**
 * @brief Vérifier si un manufacturer est un compteur d'énergie Tuya connu
 */
bool isTuyaEnergyMeter(const String& manufacturer);

/**
 * @brief Décodage des données de phase RAW (format buffer 8 octets)
 * Format: [V_hi][V_lo][I_hi][I_mid][I_lo][P_hi][P_mid][P_lo]
 */
PhaseData decodePhaseRaw(uint8_t* data, int len);

/**
 * @brief Envoi d'une requête Tuya datapoint query pour compteur
 */
void sendEnergyMeterQuery(int shortAddr, int endpoint);

#endif // ENERGYMETER_H