#include <Arduino.h>
#include <time.h>
#include "thermostat.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include "mqtt.h"
#include "device.h"
#include <unordered_map>

extern std::vector<DeviceData*> devices;
extern AsyncMqttClient mqttClient;
extern ConfigSettingsStruct ConfigSettings;
extern CircularBuffer<Device, 50> *deviceList;
extern CircularBuffer<Packet, 100> *commandList;

extern String FormattedDate;

// Séquence Tuya pour les transactions
static uint16_t tuyaSeqNumber = 0;

// ============================================================================
// CACHE POUR ÉVITER LES RECHERCHES RÉPÉTÉES (même schéma que lixee.cpp)
// ============================================================================
static std::unordered_map<std::string, DeviceData*> thermostatCache;
static bool thermostatCacheInitialized = false;

// Structure pour éviter la duplication (même schéma que lixee.cpp)
struct ThermostatProcessedData {
    String deviceId;
    String clusterId;
    String attributeStr;
    String value;
    String valueType;
    bool isNumeric;
};

// ============================================================================
// TABLES DE CORRESPONDANCE MANUFACTURER -> TYPE
// ============================================================================
static const char* TUYA_TRV_TYPE_A_MANUFACTURERS[] = {
    "_TZE200_a4bpgplm", "_TZE200_dv8abrrz", "_TZE200_z1tyspqw", "_TZE200_rtrmfadk",
    "_TZE200_d3z1ukqw", "_TZE200_yqgbrdyo", "_TZE200_cxakecfo", "_TZE200_hvaxb2tc",
    "_TZE204_a4bpgplm", "_TZE204_rtrmfadk", "_TZE204_qyr2m29i", "_TZE204_zljqtner",
    "_TZE204_eekpf0ft", "_TZE204_pcdmj88b", "_TZE204_ogx8u5z6", nullptr
};

static const char* TUYA_TRV_TYPE_B_MANUFACTURERS[] = {
    "_TZE200_kds0pmmv", "_TZE204_kds0pmmv", nullptr
};

static const char* TUYA_MOES_TRV_MANUFACTURERS[] = {
    "_TZE200_9mjy74mp", "_TZE204_9mjy74mp", "_TZE200_ztvwu4nk",
    "_TZE200_5toc8efa", "_TZE204_5toc8efa", "_TZE200_ye5jkfsb",
    "_TZE200_u9bfwha0", "_TZE204_u9bfwha0", nullptr
};

static const char* TUYA_MOES_WALL_MANUFACTURERS[] = {
    "_TZE200_aoclfnxz", "_TZE204_aoclfnxz", "_TZE200_edl8pz1k", "_TZE204_edl8pz1k", nullptr
};

static const char* TUYA_AVATTO_MANUFACTURERS[] = {
    "_TZE200_p3dbf6qs", "_TZE204_g2ki0ejr", nullptr
};

static const char* TUYA_FLOOR_HEATING_MANUFACTURERS[] = {
    "_TZE284_khah2lkr", nullptr
};

// ============================================================================
// FONCTIONS HELPER (même schéma que lixee.cpp)
// ============================================================================

void initializeThermostatCache() {
    if (thermostatCacheInitialized) return;
    
    thermostatCache.clear();
    for (size_t i = 0; i < devices.size(); i++) {
        DeviceData* device = devices[i];
        thermostatCache[device->getDeviceID().c_str()] = device;
    }
    thermostatCacheInitialized = true;
}

DeviceData* findThermostatDevice(const String& deviceId) {
    if (!thermostatCacheInitialized) {
        initializeThermostatCache();
    }
    
    auto it = thermostatCache.find(deviceId.c_str());
    return (it != thermostatCache.end()) ? it->second : nullptr;
}

// Recherche dans une table de manufacturers
bool isInManufacturerList(const String& manufacturer, const char* list[]) {
    for (int i = 0; list[i] != nullptr; i++) {
        if (manufacturer == list[i]) {
            return true;
        }
    }
    return false;
}

TuyaThermostatType getTuyaThermostatType(const String& manufacturer) {
    if (isInManufacturerList(manufacturer, TUYA_TRV_TYPE_A_MANUFACTURERS)) {
        return TUYA_TRV_TYPE_A;
    }
    if (isInManufacturerList(manufacturer, TUYA_TRV_TYPE_B_MANUFACTURERS)) {
        return TUYA_TRV_TYPE_B;
    }
    if (isInManufacturerList(manufacturer, TUYA_MOES_TRV_MANUFACTURERS)) {
        return TUYA_MOES_TRV;
    }
    if (isInManufacturerList(manufacturer, TUYA_MOES_WALL_MANUFACTURERS)) {
        return TUYA_MOES_WALL;
    }
    if (isInManufacturerList(manufacturer, TUYA_AVATTO_MANUFACTURERS)) {
        return TUYA_AVATTO;
    }
    if (isInManufacturerList(manufacturer, TUYA_FLOOR_HEATING_MANUFACTURERS)) {
        return TUYA_FLOOR_HEATING;
    }
    return TUYA_UNKNOWN;
}

bool isTuyaThermostat(const String& manufacturer) {
    return getTuyaThermostatType(manufacturer) != TUYA_UNKNOWN;
}

String getPresetName(uint8_t preset, TuyaThermostatType type) {
    switch (type) {
        case TUYA_TRV_TYPE_A:
            switch (preset) {
                case 0: return "auto";
                case 1: return "heat";
                case 2: return "off";
            }
            break;
        case TUYA_TRV_TYPE_B:
            switch (preset) {
                case 0: return "auto";
                case 1: return "manual";
                case 2: return "off";
                case 3: return "on";
            }
            break;
        case TUYA_MOES_TRV:
            switch (preset) {
                case 0: return "schedule";
                case 1: return "manual";
                case 2: return "boost";
                case 3: return "comfort";
                case 4: return "eco";
                case 5: return "away";
            }
            break;
        case TUYA_MOES_WALL:
            switch (preset) {
                case 0: return "program";
                case 1: return "manual";
            }
            break;
        case TUYA_AVATTO:
            switch (preset) {
                case 0: return "manual";
                case 1: return "schedule";
                case 2: return "eco";
                case 3: return "comfort";
                case 4: return "frost";
            }
            break;
        default:
            break;
    }
    return String(preset);
}

// ============================================================================
// PUBLICATION DES DONNÉES (même schéma que lixee.cpp)
// ============================================================================

// ============================================================================
// PUBLICATION DES DONNÉES (avec coefficient)
// ============================================================================

void publishThermostatData(const ThermostatProcessedData& data) {
    // Récupérer le device pour le coefficient
    DeviceData* device = findThermostatDevice(data.deviceId);
    
    // MQTT
    if (ConfigSettings.enableMqtt) {
        mqttPublish(data.deviceId, data.clusterId, data.attributeStr, 
                   data.valueType, data.value);
    }
    
    // Device list update
    if (!deviceList->isFull()) {
        int shortaddr = GetShortAddr(data.deviceId + ".ini");
        int clusterInt = strtol(data.clusterId.c_str(), NULL, 16);
        int attrInt = data.attributeStr.toInt();
        
        if (data.isNumeric) {
            // Récupérer la valeur brute
            long rawValue = strtol(data.value.c_str(), NULL, 16);
            
            // Appliquer le coefficient si device trouvé
            if (device != nullptr) {
                float coefficient = device->GetAttributeCoefficient(clusterInt, attrInt);
                float finalValue = rawValue * coefficient;
                deviceList->push(Device{shortaddr, clusterInt, attrInt, String(finalValue, 2)});
                log_d("Thermostat: cluster=%04X attr=%d raw=%ld coef=%.4f final=%.2f", 
                      clusterInt, attrInt, rawValue, coefficient, finalValue);
            } else {
                deviceList->push(Device{shortaddr, clusterInt, attrInt, String(rawValue)});
            }
        } else {
            deviceList->push(Device{shortaddr, clusterInt, attrInt, String(data.value)});
        }
    }
}

void updateThermostatValue(const String& deviceId, const String& cluster, 
                           int attribute, const String& value) {
    DeviceData* device = findThermostatDevice(deviceId);
    if (device) {
        device->setValue(std::string(cluster.c_str()), 
                        std::string(String(attribute).c_str()), 
                        std::string(value.c_str()));
    }
}

// ============================================================================
// GESTIONNAIRES HVAC THERMOSTAT (0x0201) - Attributs spécifiques
// ============================================================================

// Attribut 0x0000 - Local Temperature
void handleHvacLocalTemperature(const String& inifile, uint8_t* datas, int len) {
    if (!ini_exist(inifile)) return;
    
    // Format: [len_hi][len_lo][value_hi][value_lo]
    // Sauter les 2 octets de longueur, lire en big-endian (format ZiGate)
    char hexBuf[5];
    int16_t rawTemp = (datas[0] << 8) | datas[1];
    sprintf(hexBuf, "%04X", (uint16_t)rawTemp);
        
    String deviceId = inifile.substring(0, 16);
    ThermostatProcessedData data = {deviceId, "0201", "0", String(hexBuf), "numeric", true};
    
    publishThermostatData(data);
    updateThermostatValue(deviceId, "0201", 0, String(hexBuf));
    
    log_d("HVAC Local Temp: %d (%.2f°C)", rawTemp, rawTemp / 100.0);
}

// Attribut 0x0010 - Local Temperature Calibration
void handleHvacTempCalibration(const String& inifile, uint8_t* datas, int len) {
    if (!ini_exist(inifile)) return;
    
    char hexBuf[3];
    int8_t calibration = (int8_t)datas[0];
    sprintf(hexBuf, "%02X", (uint8_t)calibration);
    
    String deviceId = inifile.substring(0, 16);
    ThermostatProcessedData data = {deviceId, "0201", "16", String(hexBuf), "numeric", true};
    
    publishThermostatData(data);
    updateThermostatValue(deviceId, "0201", 16, String(hexBuf));
    
    log_d("HVAC Temp Calibration: %d (%.1f°C)", calibration, calibration / 10.0);
}

// Attribut 0x0011 - Occupied Colling Setpoint
void handleHvacCoolingSetpoint(const String& inifile, uint8_t* datas, int len) {
    if (!ini_exist(inifile)) return;
    
    char hexBuf[5];
    int16_t setpoint = (datas[0] << 8) | datas[1];
    sprintf(hexBuf, "%04X", (uint16_t)setpoint);
    
    String deviceId = inifile.substring(0, 16);
    ThermostatProcessedData data = {deviceId, "0201", "17", String(hexBuf), "numeric", true};
    
    publishThermostatData(data);
    updateThermostatValue(deviceId, "0201", 17, String(hexBuf));
    
    log_d("HVAC Heating Setpoint: %d (%.2f°C)", setpoint, setpoint / 100.0);
    log_e("HVAC datas[]: %02X %02X %02X %02X (len=%d)", 
      datas[0], datas[1], datas[2], datas[3], len);
}

// Attribut 0x0012 - Occupied Heating Setpoint
void handleHvacHeatingSetpoint(const String& inifile, uint8_t* datas, int len) {
    if (!ini_exist(inifile)) return;
    
    char hexBuf[5];
    int16_t setpoint = (datas[0] << 8) | datas[1];
    sprintf(hexBuf, "%04X", (uint16_t)setpoint);
    
    String deviceId = inifile.substring(0, 16);
    ThermostatProcessedData data = {deviceId, "0201", "18", String(hexBuf), "numeric", true};
    
    publishThermostatData(data);
    updateThermostatValue(deviceId, "0201", 18, String(hexBuf));
    
    log_d("HVAC Heating Setpoint: %d (%.2f°C)", setpoint, setpoint / 100.0);
}

// Attribut 0x001C - System Mode
void handleHvacSystemMode(const String& inifile, uint8_t* datas, int len) {
    if (!ini_exist(inifile)) return;
    
    char hexBuf[3];
    uint8_t mode = datas[0];
    sprintf(hexBuf, "%02X", mode);
    
    String deviceId = inifile.substring(0, 16);
    ThermostatProcessedData data = {deviceId, "0201", "28", String(hexBuf), "numeric", true};
    
    publishThermostatData(data);
    updateThermostatValue(deviceId, "0201", 28, String(hexBuf));
    
    const char* modeStr = "unknown";
    switch (mode) {
        case 0: modeStr = "off"; break;
        case 1: modeStr = "auto"; break;
        case 4: modeStr = "heat"; break;
    }
    log_d("HVAC System Mode: %d (%s)", mode, modeStr);
}

// Attribut 0x0029 - Running State
void handleHvacRunningState(const String& inifile, uint8_t* datas, int len) {
    if (!ini_exist(inifile)) return;
    
    char hexBuf[5];
    int16_t setpoint = (datas[0] << 8) | datas[1];
    sprintf(hexBuf, "%04X", (uint16_t)setpoint);
    
    String deviceId = inifile.substring(0, 16);
    ThermostatProcessedData data = {deviceId, "0201", "41", String(hexBuf), "numeric", true};
    
    publishThermostatData(data);
    updateThermostatValue(deviceId, "0201", 41, String(hexBuf));
    
    log_d("HVAC Running State: %d (%s)", state, (state & 0x01) ? "heating" : "idle");
}

// Attribut par défaut HVAC
void handleHvacDefaultAttribute(const String& inifile, int attribute, uint8_t* datas, int len) {
    if (!ini_exist(inifile)) return;
    
    String tmp = "";
    char value[3];
    for (int i = 0; i < len && i < 4; i++) {
        sprintf(value, "%02X", datas[i]);
        tmp += value;
    }
    
    String deviceId = inifile.substring(0, 16);
    ThermostatProcessedData data = {deviceId, "0201", String(attribute), tmp, "numeric", true};
    
    publishThermostatData(data);
    updateThermostatValue(deviceId, "0201", attribute, tmp);
    
    log_d("HVAC Attribute %d: %s", attribute, tmp.c_str());
}

// ============================================================================
// GESTIONNAIRES SONOFF FC11 - Attributs spécifiques
// ============================================================================

// Attribut 0x0000 - Child Lock
void handleSonoffChildLock(const String& inifile, uint8_t* datas, int len) {
    if (!ini_exist(inifile)) return;
    
    bool locked = datas[0] != 0;
    String value = locked ? "01" : "00";
    
    String deviceId = inifile.substring(0, 16);
    ThermostatProcessedData data = {deviceId, "FC11", "0", value, "numeric", true};
    
    publishThermostatData(data);
    updateThermostatValue(deviceId, "FC11", 0, value);
    
    log_d("Sonoff Child Lock: %s", locked ? "ON" : "OFF");
}

// Attribut 0x0002 - Frost Protection Temperature
void handleSonoffFrostProtection(const String& inifile, uint8_t* datas, int len) {
    if (!ini_exist(inifile)) return;
    
    char hexBuf[5];
    int16_t frostTemp = (datas[1] << 8) | datas[0];
    sprintf(hexBuf, "%04X", (uint16_t)frostTemp);
    
    String deviceId = inifile.substring(0, 16);
    ThermostatProcessedData data = {deviceId, "FC11", "2", String(hexBuf), "numeric", true};
    
    publishThermostatData(data);
    updateThermostatValue(deviceId, "FC11", 2, String(hexBuf));
    
    log_d("Sonoff Frost Protection: %.2f°C", frostTemp / 100.0);
}

// Attribut 0x0006 - Open Window
void handleSonoffOpenWindow(const String& inifile, uint8_t* datas, int len) {
    if (!ini_exist(inifile)) return;
    
    bool openWindow = datas[0] != 0;
    String value = openWindow ? "01" : "00";
    
    String deviceId = inifile.substring(0, 16);
    ThermostatProcessedData data = {deviceId, "FC11", "6", value, "numeric", true};
    
    publishThermostatData(data);
    updateThermostatValue(deviceId, "FC11", 6, value);
    
    log_d("Sonoff Open Window: %s", openWindow ? "ON" : "OFF");
}

// Attribut 0x000D - Valve Opening Degree
void handleSonoffValveOpening(const String& inifile, uint8_t* datas, int len) {
    if (!ini_exist(inifile)) return;
    
    char hexBuf[3];
    uint8_t opening = datas[0];
    sprintf(hexBuf, "%02X", opening);
    
    String deviceId = inifile.substring(0, 16);
    ThermostatProcessedData data = {deviceId, "FC11", "13", String(hexBuf), "numeric", true};
    
    publishThermostatData(data);
    updateThermostatValue(deviceId, "FC11", 13, String(hexBuf));
    
    log_d("Sonoff Valve Opening: %d%%", opening);
}

// Attribut par défaut Sonoff
void handleSonoffDefaultAttribute(const String& inifile, int attribute, uint8_t* datas, int len) {
    if (!ini_exist(inifile)) return;
    
    String tmp = "";
    char value[3];
    for (int i = 0; i < len && i < 4; i++) {
        sprintf(value, "%02X", datas[i]);
        tmp += value;
    }
    
    String deviceId = inifile.substring(0, 16);
    ThermostatProcessedData data = {deviceId, "FC11", String(attribute), tmp, "numeric", true};
    
    publishThermostatData(data);
    updateThermostatValue(deviceId, "FC11", attribute, tmp);
    
    log_d("Sonoff FC11 Attribute %d: %s", attribute, tmp.c_str());
}

// ============================================================================
// GESTIONNAIRES TUYA EF00 - Datapoints
// ============================================================================

// Fonction pour extraire la valeur Tuya (big-endian)
uint32_t getTuyaDPValue(uint8_t* data, int dataLen) {
    uint32_t value = 0;
    for (int i = 0; i < dataLen && i < 4; i++) {
        value = (value << 8) | data[i];
    }
    return value;
}

// Gestionnaire générique pour un datapoint Tuya
void handleTuyaDatapoint(const String& inifile, uint8_t dpId, uint8_t dpType, 
                         uint8_t* dpData, int dataLen, TuyaThermostatType type) {
    if (!ini_exist(inifile)) return;
    
    uint32_t value = getTuyaDPValue(dpData, dataLen);
    
    char hexBuf[10];
    if (dataLen == 1) {
        sprintf(hexBuf, "%02X", (uint8_t)value);
    } else if (dataLen == 2) {
        sprintf(hexBuf, "%04X", (uint16_t)value);
    } else {
        sprintf(hexBuf, "%08lX", value);
    }
    
    String deviceId = inifile.substring(0, 16);
    ThermostatProcessedData data = {deviceId, "EF00", String(dpId), String(hexBuf), "numeric", true};
    
    publishThermostatData(data);
    updateThermostatValue(deviceId, "EF00", dpId, String(hexBuf));
    
    // Log spécifique selon le type et le DP
    switch (type) {
        case TUYA_TRV_TYPE_A:
            switch (dpId) {
                case 2: log_i("TRV-A System Mode: %s", getPresetName(value, type).c_str()); break;
                case 3: log_i("TRV-A Running State: %s", value == 0 ? "heating" : "idle"); break;
                case 4: log_i("TRV-A Setpoint: %.1f°C", value / 10.0); break;
                case 5: log_i("TRV-A Temperature: %.1f°C", value / 10.0); break;
                case 6: log_i("TRV-A Battery: %d%%", value); break;
                case 7: log_i("TRV-A Child Lock: %s", value ? "ON" : "OFF"); break;
                default: log_d("TRV-A DP%d: %d", dpId, value); break;
            }
            break;
            
        case TUYA_TRV_TYPE_B:
            switch (dpId) {
                case 1: log_i("TRV-B Preset: %s", getPresetName(value, type).c_str()); break;
                case 2: log_i("TRV-B Setpoint: %.1f°C", value / 10.0); break;
                case 3: log_i("TRV-B Temperature: %.1f°C", value / 10.0); break;
                case 4: log_i("TRV-B Boost: %s", value ? "ON" : "OFF"); break;
                case 5: log_i("TRV-B Boost Time: %d min", value); break;
                case 14: log_i("TRV-B Battery: %d%%", value); break;
                default: log_d("TRV-B DP%d: %d", dpId, value); break;
            }
            break;
            
        case TUYA_MOES_TRV:
            switch (dpId) {
                case 2: log_i("MOES TRV Setpoint: %.1f°C", value / 10.0); break;
                case 3: log_i("MOES TRV Temperature: %.1f°C", value / 10.0); break;
                case 4: log_i("MOES TRV Preset: %s", getPresetName(value, type).c_str()); break;
                case 7: log_i("MOES TRV Child Lock: %s", value ? "ON" : "OFF"); break;
                case 8: log_i("MOES TRV Open Window: %s", value ? "detected" : "closed"); break;
                case 21: log_i("MOES TRV Battery: %d%%", value); break;
                case 101: log_i("MOES TRV Valve Position: %d%%", value); break;
                default: log_d("MOES TRV DP%d: %d", dpId, value); break;
            }
            break;
            
        case TUYA_MOES_WALL:
            switch (dpId) {
                case 1: log_i("MOES Wall System: %s", value ? "ON" : "OFF"); break;
                case 2: log_i("MOES Wall Preset: %s", getPresetName(value, type).c_str()); break;
                case 3: log_i("MOES Wall Running: %s", value == 0 ? "idle" : "heating"); break;
                case 16: log_i("MOES Wall Setpoint: %d°C", value); break;
                case 24: log_i("MOES Wall Temperature: %d (raw)", value); break;
                case 40: log_i("MOES Wall Child Lock: %s", value ? "ON" : "OFF"); break;
                case 43: log_i("MOES Wall Sensor: %s", value == 0 ? "IN" : (value == 1 ? "AL" : "OU")); break;
                default: log_d("MOES Wall DP%d: %d", dpId, value); break;
            }
            break;
            
        case TUYA_AVATTO:
            switch (dpId) {
                case 2: log_i("AVATTO Preset: %s", getPresetName(value, type).c_str()); break;
                case 4: log_i("AVATTO Setpoint: %.1f°C", value / 10.0); break;
                case 5: log_i("AVATTO Temperature: %.1f°C", value / 10.0); break;
                case 6: log_i("AVATTO Battery: %d%%", value); break;
                case 7: log_i("AVATTO Child Lock: %s", value ? "ON" : "OFF"); break;
                default: log_d("AVATTO DP%d: %d", dpId, value); break;
            }
            break;
            
        case TUYA_FLOOR_HEATING:
            switch (dpId) {
                case 1: log_i("Floor System: %s", value ? "ON" : "OFF"); break;
                case 16: log_i("Floor Setpoint: %.1f°C", value / 10.0); break;
                case 24: log_i("Floor Int Temp: %.1f°C", value / 10.0); break;
                case 101: log_i("Floor Ext Temp: %.1f°C", value / 10.0); break;
                case 129: log_i("Floor Child Lock: %s", value ? "ON" : "OFF"); break;
                default: log_d("Floor DP%d: %d", dpId, value); break;
            }
            break;
            
        default:
            log_d("Tuya DP%d (type:%d): %d", dpId, dpType, value);
            break;
    }
}

// ============================================================================
// FONCTIONS PRINCIPALES (même schéma que lixeeClusterManage)
// ============================================================================

void hvacThermostatManage(String inifile, int attribute, uint8_t datatype, 
                          int len, char* datas) {
    uint8_t* data = reinterpret_cast<uint8_t*>(datas);
    
    switch (attribute) {
        case 0:     // 0x0000 - Local Temperature
            handleHvacLocalTemperature(inifile, data, len);
            break;
        case 16:    // 0x0010 - Local Temp Calibration
            handleHvacTempCalibration(inifile, data, len);
            break;
        case 17:    // 0x0011 - Occupied Cooling setpoint
            handleHvacCoolingSetpoint(inifile, data, len);
            break;
        case 18:    // 0x0012 - Occupied Heating Setpoint
            handleHvacHeatingSetpoint(inifile, data, len);
            break;
        case 28:    // 0x001C - System Mode
            handleHvacSystemMode(inifile, data, len);
            break;
        case 41:    // 0x0029 - Running State
            handleHvacRunningState(inifile, data, len);
            break;
        default:
            handleHvacDefaultAttribute(inifile, attribute, data, len);
            break;
    }
}

void sonoffThermostatManage(String inifile, int attribute, uint8_t datatype, 
                            int len, char* datas) {
    uint8_t* data = reinterpret_cast<uint8_t*>(datas);
    
    switch (attribute) {
        case 0:     // 0x0000 - Child Lock
            handleSonoffChildLock(inifile, data, len);
            break;
        case 2:     // 0x0002 - Frost Protection Temperature
            handleSonoffFrostProtection(inifile, data, len);
            break;
        case 6:     // 0x0006 - Open Window
            handleSonoffOpenWindow(inifile, data, len);
            break;
        case 13:    // 0x000D - Valve Opening Degree
            handleSonoffValveOpening(inifile, data, len);
            break;
        default:
            handleSonoffDefaultAttribute(inifile, attribute, data, len);
            break;
    }
}

void tuyaThermostatManage(String inifile, int attribute, uint8_t datatype, 
                          int len, char* datas) {
    if (!ini_exist(inifile)) return;
    
    String deviceId = inifile.substring(0, 16);
    uint8_t* data = reinterpret_cast<uint8_t*>(datas);
    
    // Récupérer le manufacturer pour déterminer le type
    DeviceData* device = findThermostatDevice(deviceId);
    if (!device) {
        log_e("Device not found: %s", deviceId.c_str());
        return;
    }
    
    String manufacturer = device->getInfo().manufacturer;
    TuyaThermostatType type = getTuyaThermostatType(manufacturer);
    
    if (type == TUYA_UNKNOWN) {
        log_w("Unknown Tuya thermostat manufacturer: %s", manufacturer.c_str());
        type = TUYA_MOES_WALL;  // Fallback par défaut
    }
    
    log_d("Tuya thermostat from %s (type %d), attr=%d, len=%d", 
          manufacturer.c_str(), type, attribute, len);
    
    // Format Tuya EF00: [seq:2][dpId:1][dpType:1][dataLen:2][data:N]...
    int offset = 0;
    
    // Skip sequence number si présent (attribut 0 = status report)
    if (attribute == 0 && len > 2) {
        offset = 2;
    }
    
    // Parser les datapoints
    while (offset < len - 4) {
        uint8_t dpId = data[offset];
        uint8_t dpType = data[offset + 1];
        uint16_t dataLen = (data[offset + 2] << 8) | data[offset + 3];
        
        if (dataLen > 256 || offset + 4 + dataLen > len) {
            log_e("Invalid Tuya datapoint length: %d at offset %d", dataLen, offset);
            break;
        }
        
        handleTuyaDatapoint(inifile, dpId, dpType, &data[offset + 4], dataLen, type);
        
        // Avancer au prochain datapoint
        offset += 4 + dataLen;
    }
}

// ============================================================================
// FONCTION TIME SYNC TUYA
// ============================================================================

void sendTuyaTimeSync(int shortAddr, int endpoint)
{
    // Réponse Time Sync (Command 0x24)
    // Format réponse: [seq:2][utc_time:4][local_time:4]
    
    Packet trame;
    trame.cmd = 0x0530;  // Raw APS Data Request
    
    uint8_t payload[40];
    int idx = 0;
    
    // Address mode (short address)
    payload[idx++] = 0x02;
    payload[idx++] = (shortAddr >> 8) & 0xFF;
    payload[idx++] = shortAddr & 0xFF;
    
    // Source endpoint
    payload[idx++] = 0x01;
    // Destination endpoint
    payload[idx++] = endpoint;
    
    // Cluster ID (0xEF00 - Tuya)
    payload[idx++] = 0xEF;
    payload[idx++] = 0x00;
    
    // Profile ID (0x0104 = Home Automation)
    payload[idx++] = 0x01;
    payload[idx++] = 0x04;
    
    // Security mode
    payload[idx++] = 0x02;
    
    // Radius
    payload[idx++] = 0x1E;
    
    // Data length placeholder (sera mis à jour)
    int lenIdx = idx;
    payload[idx++] = 0x00;
    
    // === ZCL Frame ===
    // Frame control: cluster-specific, server to client, disable default response
    payload[idx++] = 0x09;  // 0x09 = cluster-specific (bit 0), server-to-client (bit 3)
    
    // Transaction sequence number
    payload[idx++] = tuyaSeqNumber & 0xFF;
    
    // Command ID (0x24 = Time Sync Response)
    payload[idx++] = 0x24;
    
    // === Tuya Payload ===
    // Tuya sequence number (2 bytes, big-endian)
    payload[idx++] = (tuyaSeqNumber >> 8) & 0xFF;
    payload[idx++] = tuyaSeqNumber & 0xFF;
    tuyaSeqNumber++;
    
    // UTC Time (4 bytes, big-endian)
    // Tuya utilise l'epoch 2000-01-01 00:00:00 (offset = 946684800 secondes)
    time_t now;
    time(&now);
    uint32_t utcTime = (uint32_t)(now - 946684800);
    
    payload[idx++] = (utcTime >> 24) & 0xFF;
    payload[idx++] = (utcTime >> 16) & 0xFF;
    payload[idx++] = (utcTime >> 8) & 0xFF;
    payload[idx++] = utcTime & 0xFF;
    
    // Local Time (4 bytes, big-endian)
    // Pour simplifier, on utilise UTC+1 (France)
    // Ajuster selon le fuseau horaire configuré
    uint32_t localTime = utcTime + 3600;  // +1 heure
    
    payload[idx++] = (localTime >> 24) & 0xFF;
    payload[idx++] = (localTime >> 16) & 0xFF;
    payload[idx++] = (localTime >> 8) & 0xFF;
    payload[idx++] = localTime & 0xFF;
    
    // Mettre à jour la longueur des données ZCL
    payload[lenIdx] = idx - lenIdx - 1;
    
    trame.len = idx;
    memcpy(trame.datas, payload, idx);
    
    commandList->push(trame);
    log_i("Sent Tuya Time Sync Response to %04X (UTC: %lu)", shortAddr, utcTime);
}

// ============================================================================
// FONCTION TUYA DATAPOINT SET (pour écriture depuis l'interface web)
// ============================================================================

void sendTuyaDatapointSet(int shortAddr, int endpoint, uint8_t dpId, 
                          uint8_t dpType, uint32_t value)
{
    // Calculer la taille des données selon le type
    uint8_t dataLen = 1;
    if (dpType == TUYA_TYPE_VALUE) {
        dataLen = 4;
    } else if (dpType == TUYA_TYPE_BITMAP) {
        if (value > 0xFFFF) dataLen = 4;
        else if (value > 0xFF) dataLen = 2;
        else dataLen = 1;
    }
    
    Packet trame;
    trame.cmd = 0x0530;  // Raw APS Data Request
    
    uint8_t payload[40];
    int idx = 0;
    
    // Address mode (short address)
    payload[idx++] = 0x02;
    payload[idx++] = (shortAddr >> 8) & 0xFF;
    payload[idx++] = shortAddr & 0xFF;
    
    // Endpoints
    payload[idx++] = 0x01;      // Source endpoint
    payload[idx++] = endpoint;  // Dest endpoint
    
    // Cluster ID (0xEF00 - Tuya)
    payload[idx++] = 0xEF;
    payload[idx++] = 0x00;
    
    // Profile ID (0x0104 = Home Automation)
    payload[idx++] = 0x01;
    payload[idx++] = 0x04;
    
    // Security mode
    payload[idx++] = 0x02;
    
    // Radius
    payload[idx++] = 0x1E;
    
    // Data length placeholder
    int lenIdx = idx;
    payload[idx++] = 0x00;
    
    // === ZCL Frame ===
    // Frame control: cluster-specific, client to server
    payload[idx++] = 0x01;  // 0x01 = cluster-specific only (sans disable default response)
    
    // Transaction sequence number
    payload[idx++] = tuyaSeqNumber & 0xFF;
    
    // Command ID (0x00 = Datapoint Set)
    payload[idx++] = 0x00;
    
    // === Tuya Payload ===
    // Tuya sequence number (2 bytes, big-endian)
    payload[idx++] = (tuyaSeqNumber >> 8) & 0xFF;
    payload[idx++] = tuyaSeqNumber & 0xFF;
    tuyaSeqNumber++;
    
    // Datapoint ID
    payload[idx++] = dpId;
    
    // Datapoint type
    payload[idx++] = dpType;
    
    // Data length (2 bytes, big-endian)
    payload[idx++] = 0x00;
    payload[idx++] = dataLen;
    
    // Value (big-endian pour Tuya)
    for (int i = dataLen - 1; i >= 0; i--) {
        payload[idx++] = (value >> (8 * i)) & 0xFF;
    }
    
    // Mettre à jour la longueur des données ZCL
    payload[lenIdx] = idx - lenIdx - 1;
    
    trame.len = idx;
    memcpy(trame.datas, payload, idx);
    
    commandList->push(trame);
    log_i("Sent Tuya DP%d (type:%d) = %lu to %04X", dpId, dpType, value, shortAddr);
}

// ============================================================================
// FONCTION TUYA DATAPOINT QUERY (pour lecture depuis l'interface web)
// ============================================================================

void sendTuyaDatapointQuery(int shortAddr, int endpoint)
{
    Packet trame;
    trame.cmd = 0x0530;  // Raw APS Data Request
    
    uint8_t payload[30];
    int idx = 0;
    
    // Address mode (short address)
    payload[idx++] = 0x02;
    payload[idx++] = (shortAddr >> 8) & 0xFF;
    payload[idx++] = shortAddr & 0xFF;
    
    // Endpoints
    payload[idx++] = 0x01;      // Source endpoint
    payload[idx++] = endpoint;  // Dest endpoint
    
    // Cluster ID (0xEF00 - Tuya)
    payload[idx++] = 0xEF;
    payload[idx++] = 0x00;
    
    // Profile ID (0x0104 = Home Automation)
    payload[idx++] = 0x01;
    payload[idx++] = 0x04;
    
    // Security mode
    payload[idx++] = 0x02;
    
    // Radius
    payload[idx++] = 0x1E;
    
    // Data length placeholder
    int lenIdx = idx;
    payload[idx++] = 0x00;
    
    // === ZCL Frame ===
    // Frame control: cluster-specific, client to server
    payload[idx++] = 0x01;
    
    // Transaction sequence number
    payload[idx++] = tuyaSeqNumber & 0xFF;
    
    // Command ID (0x03 = Datapoint Query)
    payload[idx++] = 0x03;
    
    // === Tuya Payload (empty for query all) ===
    payload[idx++] = (tuyaSeqNumber >> 8) & 0xFF;
    payload[idx++] = tuyaSeqNumber & 0xFF;
    tuyaSeqNumber++;
    
    // Mettre à jour la longueur
    payload[lenIdx] = idx - lenIdx - 1;
    
    trame.len = idx;
    memcpy(trame.datas, payload, idx);
    
    commandList->push(trame);
    log_i("Sent Tuya Datapoint Query to %04X", shortAddr);
}