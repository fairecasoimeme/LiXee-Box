#ifndef THERMOSTAT_H
#define THERMOSTAT_H

#include <Arduino.h>
#include <stdint.h>

// ============================================================================
// TYPES TUYA DATAPOINT
// ============================================================================
#define TUYA_TYPE_RAW       0
#define TUYA_TYPE_BOOL      1
#define TUYA_TYPE_VALUE     2
#define TUYA_TYPE_STRING    3
#define TUYA_TYPE_ENUM      4
#define TUYA_TYPE_BITMAP    5

// ============================================================================
// TYPES DE THERMOSTATS TUYA
// ============================================================================
enum TuyaThermostatType {
    TUYA_TRV_TYPE_A,      // DP: 2=mode, 3=running, 4=setpoint, 5=temp
    TUYA_TRV_TYPE_B,      // DP: 1=preset, 2=setpoint, 3=temp
    TUYA_MOES_TRV,        // DP: 2=setpoint, 3=temp, 4=preset (with comfort/eco/away)
    TUYA_MOES_WALL,       // DP: 1=system_mode, 2=preset, 16=setpoint, 24=temp
    TUYA_AVATTO,          // DP: 2=preset, 4=setpoint, 5=temp
    TUYA_FLOOR_HEATING,   // DP: 1=on_off, 16=setpoint, 24=int_temp, 101=ext_temp
    TUYA_UNKNOWN
};

// ============================================================================
// FONCTIONS GESTIONNAIRES DE CLUSTERS
// ============================================================================

// Gestionnaire principal cluster HVAC Thermostat (0x0201)
void hvacThermostatManage(String inifile, int attribute, uint8_t datatype, 
                          int len, char* datas);

// Gestionnaire cluster Sonoff FC11 (attributs propriétaires TRVZB)
void sonoffThermostatManage(String inifile, int attribute, uint8_t datatype, 
                            int len, char* datas);

// Gestionnaire cluster Tuya EF00
void tuyaThermostatManage(String inifile, int attribute, uint8_t datatype, 
                          int len, char* datas);

// ============================================================================
// FONCTIONS UTILITAIRES
// ============================================================================

// Détermine le type de thermostat Tuya selon le manufacturer
TuyaThermostatType getTuyaThermostatType(const String& manufacturer);

// Vérifier si un manufacturer est un thermostat Tuya connu
bool isTuyaThermostat(const String& manufacturer);

// Obtenir le nom d'un preset selon le type
String getPresetName(uint8_t preset, TuyaThermostatType type);

// Envoi d'une réponse Time Sync Tuya
void sendTuyaTimeSync(int shortAddr, int endpoint);

// Envoi d'une commande Tuya datapoint set
void sendTuyaDatapointSet(int shortAddr, int endpoint, uint8_t dpId, 
                          uint8_t dpType, uint32_t value);

// Envoi d'une requête Tuya datapoint query
void sendTuyaDatapointQuery(int shortAddr, int endpoint);

#endif // THERMOSTAT_H