// ============================================================================
// HVAC Zigbee Actions
// Cluster 0x0201 (Thermostat) & 0x0202 (Fan Control)
// Pour contrôleurs IR climatisation type OWON AC201, NodOn, etc.
// ============================================================================

#include "hvac.h"

// Déclaration externe de la fonction d'écriture d'attribut
extern void SendAttributeWrite(int shortAddr, int endpoint, int cluster, int attribut, uint8_t attrType, uint32_t value);

// ============================================================================
// Implémentation des fonctions HVAC
// ============================================================================

void SendHVACSystemMode(int shortAddr, int endpoint, uint8_t mode)
{
    // SystemMode = attribut 0x001C, type enum8 (0x30)
    SendAttributeWrite(shortAddr, endpoint, CLUSTER_HVAC_THERMOSTAT, ATTR_SYSTEM_MODE, 0x30, mode);
    log_i("HVAC: SystemMode=%d to %04X ep%d", mode, shortAddr, endpoint);
}

void SendHVACSetpoint(int shortAddr, int endpoint, int16_t setpoint, bool cooling)
{
    // OccupiedCoolingSetpoint = 0x0011, OccupiedHeatingSetpoint = 0x0012
    // Type int16 (0x29)
    uint16_t attr = cooling ? ATTR_OCCUPIED_COOLING_SETPOINT : ATTR_OCCUPIED_HEATING_SETPOINT;
    SendAttributeWrite(shortAddr, endpoint, CLUSTER_HVAC_THERMOSTAT, attr, 0x29, (uint32_t)(setpoint & 0xFFFF));
    log_i("HVAC: %sSetpoint=%d (%.1f°C) to %04X ep%d", 
          cooling ? "Cooling" : "Heating", 
          setpoint, 
          SetpointToTemp(setpoint),
          shortAddr, endpoint);
}

void SendHVACSetpointBoth(int shortAddr, int endpoint, int16_t setpoint)
{
    // Mettre à jour les deux setpoints en même temps
    SendAttributeWrite(shortAddr, endpoint, CLUSTER_HVAC_THERMOSTAT, ATTR_OCCUPIED_COOLING_SETPOINT, 0x29, (uint32_t)(setpoint & 0xFFFF));
    SendAttributeWrite(shortAddr, endpoint, CLUSTER_HVAC_THERMOSTAT, ATTR_OCCUPIED_HEATING_SETPOINT, 0x29, (uint32_t)(setpoint & 0xFFFF));
    log_i("HVAC: BothSetpoint=%d (%.1f°C) to %04X ep%d", 
          setpoint, 
          SetpointToTemp(setpoint),
          shortAddr, endpoint);
}

void SendHVACFanMode(int shortAddr, int endpoint, uint8_t fanMode)
{
    // FanMode = cluster 0x0202, attribut 0x0000, type enum8 (0x30)
    SendAttributeWrite(shortAddr, endpoint, CLUSTER_HVAC_FAN_CONTROL, ATTR_FAN_MODE, 0x30, fanMode);
    log_i("HVAC: FanMode=%d to %04X ep%d", fanMode, shortAddr, endpoint);
}

void SendHVACLouverPosition(int shortAddr, int endpoint, uint8_t position)
{
    // ACLouverPosition = attribut 0x0042, type enum8 (0x30)
    SendAttributeWrite(shortAddr, endpoint, CLUSTER_HVAC_THERMOSTAT, ATTR_AC_LOUVER_POSITION, 0x30, position);
    log_i("HVAC: LouverPosition=%d to %04X ep%d", position, shortAddr, endpoint);
}

void SendHVACAction(int shortAddr, int endpoint, int command, int value)
{
    switch(command)
    {
        case ACTION_HVAC_SYSTEM_MODE:       // 310
            SendHVACSystemMode(shortAddr, endpoint, (uint8_t)value);
            break;
            
        case ACTION_HVAC_SETPOINT_BOTH:     // 311
            SendHVACSetpointBoth(shortAddr, endpoint, (int16_t)value);
            break;
            
        case ACTION_HVAC_FAN_MODE:          // 312
            SendHVACFanMode(shortAddr, endpoint, (uint8_t)value);
            break;
            
        case ACTION_HVAC_LOUVER_POSITION:   // 313
            SendHVACLouverPosition(shortAddr, endpoint, (uint8_t)value);
            break;
            
        case ACTION_HVAC_COOLING_SETPOINT:  // 314
            SendHVACSetpoint(shortAddr, endpoint, (int16_t)value, true);
            break;
            
        case ACTION_HVAC_HEATING_SETPOINT:  // 315
            SendHVACSetpoint(shortAddr, endpoint, (int16_t)value, false);
            break;
            
        default:
            log_w("Unknown HVAC command: %d", command);
            break;
    }
}

// ============================================================================
// Fonctions utilitaires HVAC
// ============================================================================

/**
 * @brief Obtient le nom du mode système en français
 */
const char* GetHVACModeName(uint8_t mode)
{
    switch(mode)
    {
        case HVAC_MODE_OFF:       return "Éteint";
        case HVAC_MODE_AUTO:      return "Auto";
        case HVAC_MODE_COOL:      return "Froid";
        case HVAC_MODE_HEAT:      return "Chaud";
        case HVAC_MODE_FAN_ONLY:  return "Ventilo";
        case HVAC_MODE_DRY:       return "Déshumid";
        case HVAC_MODE_SLEEP:     return "Nuit";
        default:                  return "Inconnu";
    }
}

/**
 * @brief Obtient le nom du mode ventilateur en français
 */
const char* GetFanModeName(uint8_t mode)
{
    switch(mode)
    {
        case FAN_MODE_OFF:     return "Éteint";
        case FAN_MODE_LOW:     return "Bas";
        case FAN_MODE_MEDIUM:  return "Moyen";
        case FAN_MODE_HIGH:    return "Fort";
        case FAN_MODE_ON:      return "On";
        case FAN_MODE_AUTO:    return "Auto";
        case FAN_MODE_SMART:   return "Intelligent";
        default:               return "Inconnu";
    }
}

/**
 * @brief Obtient le nom de la position volet en français
 */
const char* GetLouverPositionName(uint8_t position)
{
    switch(position)
    {
        case LOUVER_CLOSED:         return "Fermé";
        case LOUVER_FULL_OPEN:      return "Ouvert";
        case LOUVER_QUARTER_OPEN:   return "1/4";
        case LOUVER_HALF_OPEN:      return "1/2";
        case LOUVER_THREE_QUARTER:  return "3/4";
        default:                    return "Inconnu";
    }
}