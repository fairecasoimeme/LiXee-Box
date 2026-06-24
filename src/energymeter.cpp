#include <Arduino.h>
#include <time.h>
#include "energymeter.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include "mqtt.h"
#include "device.h"
#include <unordered_map>

extern DeviceList devices;
extern AsyncMqttClient mqttClient;
extern ConfigSettingsStruct ConfigSettings;
extern CircularBuffer<Device, 50> *deviceList;
extern CircularBuffer<Packet, 100> *commandList;

// ============================================================================
// CACHE POUR ÉVITER LES RECHERCHES RÉPÉTÉES
// ============================================================================
static PsUnorderedMap<DeviceData*> energyMeterCache;
static bool energyMeterCacheInitialized = false;

// Structure pour éviter la duplication
struct EnergyMeterProcessedData {
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

// SPM01 - Compteur monophasé Zemismart
static const char* TUYA_SPM01_MANUFACTURERS[] = {
    "_TZE200_bcusnqt8", "_TZE204_bcusnqt8",
    "_TZE200_qhlxve78", "_TZE204_qhlxve78",
    "_TZE200_iwn0gpzz", "_TZE204_iwn0gpzz",
    "_TZE284_iwn0gpzz",
    nullptr
};

// SPM02 - Compteur triphasé Zemismart (format RAW pour phases)
static const char* TUYA_SPM02_MANUFACTURERS[] = {
    "_TZE200_ves1ycwx", "_TZE204_ves1ycwx",
    "_TZE200_v9hkz2yn", "_TZE204_v9hkz2yn",
    nullptr
};

// SPM02 Détaillé - Certains firmwares WiFi avec DP séparés 101-111
static const char* TUYA_SPM02_DETAILED_MANUFACTURERS[] = {
    // Variantes avec DP 101-111 détaillés (généralement WiFi)
    nullptr
};

// DDS238 - Compteurs DIN rail
static const char* TUYA_DDS238_MANUFACTURERS[] = {
    "_TZE200_nslr42tt", "_TZE204_nslr42tt",
    "_TZE200_bkkmqmyo", "_TZE204_bkkmqmyo",
    nullptr
};

// HIKING - Compteur DIN power
static const char* TUYA_HIKING_MANUFACTURERS[] = {
    "_TZE200_lsanae15", "_TZE204_lsanae15",
    nullptr
};

// ============================================================================
// FONCTIONS HELPER
// ============================================================================

void initializeEnergyMeterCache() {
    if (energyMeterCacheInitialized) return;
    
    energyMeterCache.clear();
    for (size_t i = 0; i < devices.size(); i++) {
        DeviceData* device = devices[i];
        energyMeterCache[device->getDeviceID().c_str()] = device;
    }
    energyMeterCacheInitialized = true;
}

DeviceData* findEnergyMeterDevice(const String& deviceId) {
    if (!energyMeterCacheInitialized) {
        initializeEnergyMeterCache();
    }

    auto it = energyMeterCache.find(deviceId.c_str());
    if (it != energyMeterCache.end()) return it->second;

    // Cache miss — chercher dans la liste complète (device ajouté après init du cache)
    for (size_t i = 0; i < devices.size(); i++) {
        DeviceData* device = devices[i];
        if (device->getDeviceID() == deviceId) {
            energyMeterCache[device->getDeviceID().c_str()] = device;
            return device;
        }
    }
    return nullptr;
}

// Recherche dans une table de manufacturers
static bool isInEnergyMeterManufacturerList(const String& manufacturer, const char* list[]) {
    for (int i = 0; list[i] != nullptr; i++) {
        if (manufacturer == list[i]) {
            return true;
        }
    }
    return false;
}

TuyaEnergyMeterType getTuyaEnergyMeterType(const String& manufacturer) {
    if (isInEnergyMeterManufacturerList(manufacturer, TUYA_SPM01_MANUFACTURERS)) {
        return TUYA_METER_SPM01;
    }
    if (isInEnergyMeterManufacturerList(manufacturer, TUYA_SPM02_MANUFACTURERS)) {
        return TUYA_METER_SPM02;
    }
    if (isInEnergyMeterManufacturerList(manufacturer, TUYA_SPM02_DETAILED_MANUFACTURERS)) {
        return TUYA_METER_SPM02_DETAILED;
    }
    if (isInEnergyMeterManufacturerList(manufacturer, TUYA_DDS238_MANUFACTURERS)) {
        return TUYA_METER_DDS238;
    }
    if (isInEnergyMeterManufacturerList(manufacturer, TUYA_HIKING_MANUFACTURERS)) {
        return TUYA_METER_HIKING;
    }
    return TUYA_METER_UNKNOWN;
}

bool isTuyaEnergyMeter(const String& manufacturer) {
    return getTuyaEnergyMeterType(manufacturer) != TUYA_METER_UNKNOWN;
}

// ============================================================================
// DECODAGE DES DONNEES DE PHASE RAW
// Format: [voltage_hi][voltage_lo][current_hi][current_mid][current_lo][power_hi][power_mid][power_lo]
// ============================================================================

PhaseData decodePhaseRaw(uint8_t* data, int len) {
    PhaseData result = {0, 0, 0, false};
    
    if (len < 8) {
        log_e("Invalid phase data length: %d (expected 8)", len);
        return result;
    }
    
    // Voltage: bytes 0-1, diviser par 10 pour obtenir V
    uint16_t rawVoltage = (data[0] << 8) | data[1];
    result.voltage = rawVoltage / 10.0f;
    
    // Current: bytes 2-4, diviser par 1000 pour obtenir A
    uint32_t rawCurrent = ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 8) | data[4];
    result.current = rawCurrent / 1000.0f;
    
    // Power: bytes 5-7, valeur directe en W
    uint32_t rawPower = ((uint32_t)data[5] << 16) | ((uint32_t)data[6] << 8) | data[7];
    result.power = (float)rawPower;
    
    result.valid = true;
    
    log_d("Phase decoded: V=%.1f A=%.3f W=%.1f", result.voltage, result.current, result.power);
    
    return result;
}

// ============================================================================
// PUBLICATION DES DONNÉES
// ============================================================================

static void publishEnergyMeterData(const EnergyMeterProcessedData& data) {
    DeviceData* device = findEnergyMeterDevice(data.deviceId);
    
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
            long rawValue = strtol(data.value.c_str(), NULL, 16);
            
            if (device != nullptr) {
                float coefficient = device->GetAttributeCoefficient(clusterInt, attrInt);
                float finalValue = rawValue * coefficient;
                deviceList->push(Device{shortaddr, clusterInt, attrInt, String(finalValue, 2)});
                log_d("EnergyMeter: cluster=%04X attr=%d raw=%ld coef=%.4f final=%.2f", 
                      clusterInt, attrInt, rawValue, coefficient, finalValue);
            } else {
                deviceList->push(Device{shortaddr, clusterInt, attrInt, String(rawValue)});
            }
        } else {
            deviceList->push(Device{shortaddr, clusterInt, attrInt, String(data.value)});
        }
    }
}

static void updateEnergyMeterValue(const String& deviceId, const String& cluster, 
                                   int attribute, const String& value) {
    DeviceData* device = findEnergyMeterDevice(deviceId);
    if (device) {
        device->setValue(cluster.c_str(),
                        String(attribute).c_str(),
                        value.c_str());
    }
}

// ============================================================================
// FONCTION POUR EXTRAIRE LA VALEUR TUYA (big-endian)
// ============================================================================

static uint32_t getEnergyDPValue(uint8_t* data, int dataLen) {
    uint32_t value = 0;
    for (int i = 0; i < dataLen && i < 4; i++) {
        value = (value << 8) | data[i];
    }
    return value;
}

// ============================================================================
// GESTIONNAIRES DE DATAPOINTS SPECIFIQUES SPM02
// ============================================================================

// DP 1: Total Forward Energy (énergie consommée)
static void handleTotalForwardEnergy(const String& inifile, uint32_t value, TuyaEnergyMeterType type) {
    if (!ini_exist(inifile)) return;
    
    // Diviseur selon le type (généralement 100 pour kWh, 1000 pour Hiking)
    float divisor = (type == TUYA_METER_HIKING) ? 1000.0f : 100.0f;
    float energyKwh = value / divisor;
    
    char hexBuf[10];
    snprintf(hexBuf, sizeof(hexBuf), "%08lX", value);

    String deviceId = inifile.substring(0, 16);
    EnergyMeterProcessedData data = {deviceId, "EF00", "1", String(hexBuf), "numeric", true};

    publishEnergyMeterData(data);
    updateEnergyMeterValue(deviceId, "EF00", 1, String(hexBuf));

    log_i("Total Forward Energy: %lu raw (%.2f kWh)", value, energyKwh);
}

// DP 2: Neutral Current (courant neutre)
static void handleNeutralCurrent(const String& inifile, uint32_t value) {
    if (!ini_exist(inifile)) return;
    
    float currentA = value / 1000.0f;
    
    char hexBuf[10];
    snprintf(hexBuf, sizeof(hexBuf), "%08lX", value);

    String deviceId = inifile.substring(0, 16);
    EnergyMeterProcessedData data = {deviceId, "EF00", "2", String(hexBuf), "numeric", true};
    
    publishEnergyMeterData(data);
    updateEnergyMeterValue(deviceId, "EF00", 2, String(hexBuf));
    
    log_i("Neutral Current: %lu raw (%.3f A)", value, currentA);
}

// DP 6, 7, 8: Phase Data RAW (phase_a/X, phase_b/Y, phase_c/Z)
static void handlePhaseDataRaw(const String& inifile, uint8_t dpId, uint8_t* dpData, int dataLen) {
    if (!ini_exist(inifile)) return;
    
    PhaseData phase = decodePhaseRaw(dpData, dataLen);
    if (!phase.valid) {
        log_e("Failed to decode phase data for DP%d", dpId);
        return;
    }
    
    String deviceId = inifile.substring(0, 16);
    
    // Déterminer le suffixe de phase (X=6, Y=7, Z=8)
    char phaseName = 'X' + (dpId - 6);  // X, Y, Z
    int phaseIndex = dpId - 6;  // 0, 1, 2
    
    // === Publier Voltage ===
    // Attribut: 100 + phaseIndex*10 = 100, 110, 120 pour phases X, Y, Z
    {
        char hexBuf[10];
        uint16_t rawV = (uint16_t)(phase.voltage * 10);
        snprintf(hexBuf, sizeof(hexBuf), "%04X", rawV);
        
        int attrVoltage = 100 + phaseIndex * 10;
        EnergyMeterProcessedData data = {deviceId, "EF00", String(attrVoltage), String(hexBuf), "numeric", true};
        publishEnergyMeterData(data);
        updateEnergyMeterValue(deviceId, "EF00", attrVoltage, String(hexBuf));
        log_i("Voltage_%c (attr %d): %.1f V", phaseName, attrVoltage, phase.voltage);
    }
    
    // === Publier Current ===
    // Attribut: 101 + phaseIndex*10 = 101, 111, 121 pour phases X, Y, Z
    {
        char hexBuf[10];
        uint32_t rawI = (uint32_t)(phase.current * 1000);
        snprintf(hexBuf, sizeof(hexBuf), "%08lX", rawI);
        
        int attrCurrent = 101 + phaseIndex * 10;
        EnergyMeterProcessedData data = {deviceId, "EF00", String(attrCurrent), String(hexBuf), "numeric", true};
        publishEnergyMeterData(data);
        updateEnergyMeterValue(deviceId, "EF00", attrCurrent, String(hexBuf));
        log_i("Current_%c (attr %d): %.3f A", phaseName, attrCurrent, phase.current);
    }
    
    // === Publier Power ===
    // Attribut: 102 + phaseIndex*10 = 102, 112, 122 pour phases X, Y, Z
    {
        char hexBuf[10];
        uint32_t rawP = (uint32_t)phase.power;
        snprintf(hexBuf, sizeof(hexBuf), "%08lX", rawP);
        
        int attrPower = 102 + phaseIndex * 10;
        EnergyMeterProcessedData data = {deviceId, "EF00", String(attrPower), String(hexBuf), "numeric", true};
        publishEnergyMeterData(data);
        updateEnergyMeterValue(deviceId, "EF00", attrPower, String(hexBuf));
        log_i("Power_%c (attr %d): %.1f W", phaseName, attrPower, phase.power);
    }
    
    // Stocker également le DP original brut pour debug
    String rawHex = "";
    char tmp[3];
    for (int i = 0; i < dataLen && i < 16; i++) {
        snprintf(tmp, sizeof(tmp), "%02X", dpData[i]);
        rawHex += tmp;
    }
    updateEnergyMeterValue(deviceId, "EF00", dpId, rawHex);
    log_d("Phase_%c raw (DP%d): %s", phaseName, dpId, rawHex.c_str());
}

// DP 15: Total Reverse Energy (énergie produite/injectée - pour panneaux solaires)
static void handleTotalReverseEnergy(const String& inifile, uint32_t value, TuyaEnergyMeterType type) {
    if (!ini_exist(inifile)) return;
    
    float divisor = (type == TUYA_METER_HIKING) ? 1000.0f : 100.0f;
    float energyKwh = value / divisor;
    
    char hexBuf[10];
    snprintf(hexBuf, sizeof(hexBuf), "%08lX", value);

    String deviceId = inifile.substring(0, 16);
    EnergyMeterProcessedData data = {deviceId, "EF00", "15", String(hexBuf), "numeric", true};
    
    publishEnergyMeterData(data);
    updateEnergyMeterValue(deviceId, "EF00", 15, String(hexBuf));
    
    log_i("Total Reverse Energy: %lu raw (%.2f kWh)", value, energyKwh);
}

// ============================================================================
// GESTIONNAIRES DE DATAPOINTS SPECIFIQUES SPM02 DETAILLE (DP 101-111)
// ============================================================================

// DP 101: Frequency (fréquence réseau)
static void handleFrequency(const String& inifile, uint32_t value) {
    if (!ini_exist(inifile)) return;
    
    float freqHz = value / 100.0f;
    
    char hexBuf[10];
    snprintf(hexBuf, sizeof(hexBuf), "%04X", (uint16_t)value);

    String deviceId = inifile.substring(0, 16);
    EnergyMeterProcessedData data = {deviceId, "EF00", "201", String(hexBuf), "numeric", true};
    
    publishEnergyMeterData(data);
    updateEnergyMeterValue(deviceId, "EF00", 201, String(hexBuf));
    
    log_i("Frequency (attr 201): %lu raw (%.2f Hz)", value, freqHz);
}

// DP 102, 105, 108: Voltage phases (version détaillée OpenBeken)
static void handleVoltageDetailed(const String& inifile, uint8_t dpId, uint32_t value) {
    if (!ini_exist(inifile)) return;
    
    float voltageV = value / 10.0f;
    int phaseNum = (dpId - 102) / 3 + 1;  // 1, 2, 3
    
    char hexBuf[10];
    snprintf(hexBuf, sizeof(hexBuf), "%04X", (uint16_t)value);

    // Mapper vers attributs cohérents: 100, 110, 120
    int attrId = 100 + (phaseNum - 1) * 10;
    
    String deviceId = inifile.substring(0, 16);
    EnergyMeterProcessedData data = {deviceId, "EF00", String(attrId), String(hexBuf), "numeric", true};
    
    publishEnergyMeterData(data);
    updateEnergyMeterValue(deviceId, "EF00", attrId, String(hexBuf));
    
    log_i("Voltage L%d (DP%d -> attr %d): %lu raw (%.1f V)", phaseNum, dpId, attrId, value, voltageV);
}

// DP 103, 106, 109: Current phases (version détaillée OpenBeken)
static void handleCurrentDetailed(const String& inifile, uint8_t dpId, uint32_t value) {
    if (!ini_exist(inifile)) return;
    
    float currentA = value / 100.0f;  // diviseur 100 pour version détaillée
    int phaseNum = (dpId - 103) / 3 + 1;  // 1, 2, 3
    
    char hexBuf[10];
    snprintf(hexBuf, sizeof(hexBuf), "%08lX", value);

    // Mapper vers attributs cohérents: 101, 111, 121
    int attrId = 101 + (phaseNum - 1) * 10;
    
    String deviceId = inifile.substring(0, 16);
    EnergyMeterProcessedData data = {deviceId, "EF00", String(attrId), String(hexBuf), "numeric", true};
    
    publishEnergyMeterData(data);
    updateEnergyMeterValue(deviceId, "EF00", attrId, String(hexBuf));
    
    log_i("Current L%d (DP%d -> attr %d): %lu raw (%.2f A)", phaseNum, dpId, attrId, value, currentA);
}

// DP 104, 107, 110: Power phases (version détaillée OpenBeken)
static void handlePowerDetailed(const String& inifile, uint8_t dpId, uint32_t value) {
    if (!ini_exist(inifile)) return;
    
    int phaseNum = (dpId - 104) / 3 + 1;  // 1, 2, 3
    
    char hexBuf[10];
    snprintf(hexBuf, sizeof(hexBuf), "%08lX", value);

    // Mapper vers attributs cohérents: 102, 112, 122
    int attrId = 102 + (phaseNum - 1) * 10;
    
    String deviceId = inifile.substring(0, 16);
    EnergyMeterProcessedData data = {deviceId, "EF00", String(attrId), String(hexBuf), "numeric", true};
    
    publishEnergyMeterData(data);
    updateEnergyMeterValue(deviceId, "EF00", attrId, String(hexBuf));
    
    log_i("Power L%d (DP%d -> attr %d): %lu W", phaseNum, dpId, attrId, value);
}

// DP 111: Total Power (puissance totale instantanée)
static void handleTotalPower(const String& inifile, uint32_t value) {
    if (!ini_exist(inifile)) return;
    
    char hexBuf[10];
    snprintf(hexBuf, sizeof(hexBuf), "%08lX", value);

    String deviceId = inifile.substring(0, 16);
    EnergyMeterProcessedData data = {deviceId, "EF00", "200", String(hexBuf), "numeric", true};
    
    publishEnergyMeterData(data);
    updateEnergyMeterValue(deviceId, "EF00", 200, String(hexBuf));
    
    log_i("Total Power (attr 200): %lu W", value);
}

// Gestionnaire générique pour un datapoint inconnu
static void handleEnergyMeterGenericDP(const String& inifile, uint8_t dpId, uint8_t dpType, 
                                        uint8_t* dpData, int dataLen) {
    if (!ini_exist(inifile)) return;
    
    uint32_t value = getEnergyDPValue(dpData, dataLen);
    
    char hexBuf[10];
    if (dataLen == 1) {
        snprintf(hexBuf, sizeof(hexBuf), "%02X", (uint8_t)value);
    } else if (dataLen == 2) {
        snprintf(hexBuf, sizeof(hexBuf), "%04X", (uint16_t)value);
    } else {
        snprintf(hexBuf, sizeof(hexBuf), "%08lX", value);
    }
    
    String deviceId = inifile.substring(0, 16);
    EnergyMeterProcessedData data = {deviceId, "EF00", String(dpId), String(hexBuf), "numeric", true};
    
    publishEnergyMeterData(data);
    updateEnergyMeterValue(deviceId, "EF00", dpId, String(hexBuf));
    
    log_d("Energy Meter DP%d (type:%d len:%d): %lu (0x%s)", dpId, dpType, dataLen, value, hexBuf);
}

// ============================================================================
// GESTIONNAIRE DE DATAPOINT SELON LE TYPE DE COMPTEUR
// ============================================================================

// DP 32 = Total energy (variante alternative avec valeur ex: 5000 = 50.00 kWh)
static void handleEnergyDP32(const String& inifile, uint32_t value) {
    if (!ini_exist(inifile)) return;
    
    float energyKwh = value / 100.0f;
    
    char hexBuf[10];
    snprintf(hexBuf, sizeof(hexBuf), "%08lX", value);

    String deviceId = inifile.substring(0, 16);
    EnergyMeterProcessedData data = {deviceId, "EF00", "1", String(hexBuf), "numeric", true};

    publishEnergyMeterData(data);
    updateEnergyMeterValue(deviceId, "EF00", 1, String(hexBuf));

    log_i("Total Energy (DP32 -> attr 1): %lu raw (%.2f kWh)", value, energyKwh);
}

static void handleEnergyMeterDatapoint(const String& inifile, uint8_t dpId, uint8_t dpType, 
                                       uint8_t* dpData, int dataLen, TuyaEnergyMeterType type) {
    if (!ini_exist(inifile)) return;
    
    uint32_t value = getEnergyDPValue(dpData, dataLen);
    
    // Log tous les DP pour debug
    log_d(">>> DP%d (0x%02X) type=%d len=%d value=%lu (0x%08lX)", 
          dpId, dpId, dpType, dataLen, value, value);
    
    // Traitement spécifique selon le DP
    switch (dpId) {
        // === DPs communs à tous les compteurs ===
        case 1:  // Total Forward Energy (variante 1)
            handleTotalForwardEnergy(inifile, value, type);
            break;
            
        case 2:  // Neutral Current (SPM02)
            handleNeutralCurrent(inifile, value);
            break;
            
        // === DPs RAW pour phases (SPM01/SPM02 standard) ===
        case 6:  // Phase A (X) - RAW data
        case 7:  // Phase B (Y) - RAW data  
        case 8:  // Phase C (Z) - RAW data
            if (dpType == TUYA_TYPE_RAW && dataLen >= 8) {
                handlePhaseDataRaw(inifile, dpId, dpData, dataLen);
            } else {
                handleEnergyMeterGenericDP(inifile, dpId, dpType, dpData, dataLen);
            }
            break;
            
        case 15:  // Total Reverse Energy (bidirectionnel)
            handleTotalReverseEnergy(inifile, value, type);
            break;

        // === DPs DDS238-2 / Hiking (compteurs DIN monophasés) ===
        case 16:  // Switch state (on/off)
        {
            char hexBuf[4];
            snprintf(hexBuf, sizeof(hexBuf), "%02X", (uint8_t)value);
            String deviceId = inifile.substring(0, 16);
            EnergyMeterProcessedData data = {deviceId, "EF00", "16", String(hexBuf), "numeric", true};
            publishEnergyMeterData(data);
            updateEnergyMeterValue(deviceId, "EF00", 16, String(hexBuf));
            log_i("Switch State (DP16): %s", value ? "ON" : "OFF");
        }
        break;

        case 18:  // Current (DDS238: value / 1000 = A)
        {
            char hexBuf[10];
            snprintf(hexBuf, sizeof(hexBuf), "%08lX", value);
            String deviceId = inifile.substring(0, 16);
            EnergyMeterProcessedData data = {deviceId, "EF00", "101", String(hexBuf), "numeric", true};
            publishEnergyMeterData(data);
            updateEnergyMeterValue(deviceId, "EF00", 101, String(hexBuf));
            log_i("Current L1 (DP18 -> attr 101): %lu raw (%.3f A)", value, value / 1000.0f);
        }
        break;

        case 19:  // Power (DDS238: direct W)
        {
            char hexBuf[10];
            snprintf(hexBuf, sizeof(hexBuf), "%08lX", value);
            String deviceId = inifile.substring(0, 16);
            EnergyMeterProcessedData data = {deviceId, "EF00", "102", String(hexBuf), "numeric", true};
            publishEnergyMeterData(data);
            updateEnergyMeterValue(deviceId, "EF00", 102, String(hexBuf));
            log_i("Power L1 (DP19 -> attr 102): %lu W", value);
        }
        break;

        case 20:  // Voltage (DDS238: value / 10 = V)
        {
            char hexBuf[10];
            snprintf(hexBuf, sizeof(hexBuf), "%04X", (uint16_t)value);
            String deviceId = inifile.substring(0, 16);
            EnergyMeterProcessedData data = {deviceId, "EF00", "100", String(hexBuf), "numeric", true};
            publishEnergyMeterData(data);
            updateEnergyMeterValue(deviceId, "EF00", 100, String(hexBuf));
            log_i("Voltage L1 (DP20 -> attr 100): %lu raw (%.1f V)", value, value / 10.0f);
        }
        break;

        // === DP 32 = Energy total (variante alternative) ===
        case 32:  // 0x20
            handleEnergyDP32(inifile, value);
            break;
            
        // === Frequency ===
        case 101:  // Frequency
            handleFrequency(inifile, value);
            break;
            
        // === Phase 1 (L1) - DP 102-105 ===
        case 103:  // Voltage L1
        {
            float voltageV = value / 10.0f;
            char hexBuf[10];
            snprintf(hexBuf, sizeof(hexBuf), "%04X", (uint16_t)value);
            String deviceId = inifile.substring(0, 16);
            EnergyMeterProcessedData data = {deviceId, "EF00", "100", String(hexBuf), "numeric", true};
            publishEnergyMeterData(data);
            updateEnergyMeterValue(deviceId, "EF00", 103, String(hexBuf));
            log_e("Voltage L1 (DP%d -> attr 100): %.1f V", dpId, voltageV);
        }
        break;
            
        case 104:  // Current L1
        {
            float currentA = value / 1000.0f;
            char hexBuf[10];
            snprintf(hexBuf, sizeof(hexBuf), "%08lX", value);
            String deviceId = inifile.substring(0, 16);
            EnergyMeterProcessedData data = {deviceId, "EF00", "101", String(hexBuf), "numeric", true};
            publishEnergyMeterData(data);
            updateEnergyMeterValue(deviceId, "EF00", 104, String(hexBuf));
            log_e("Current L1 (DP%d -> attr 101): %.3f A", dpId, currentA);
        }
        break;
            
        case 105:  // Power L1
        {
            char hexBuf[10];
            snprintf(hexBuf, sizeof(hexBuf), "%08lX", value);
            String deviceId = inifile.substring(0, 16);
            EnergyMeterProcessedData data = {deviceId, "EF00", "102", String(hexBuf), "numeric", true};
            publishEnergyMeterData(data);
            updateEnergyMeterValue(deviceId, "EF00", 105, String(hexBuf));
            log_e("Power L1 (DP%d -> attr 102): %lu W", dpId, value);
        }
        break;
        
        case 106:  // Power Factor L1 ou autre
            handleEnergyMeterGenericDP(inifile, dpId, dpType, dpData, dataLen);
            break;
            
        // === Phase 2 (L2) - DP 106-114 ===
        case 112:  // Voltage L2 (autres firmwares)
        {
            float voltageV = value / 10.0f;
            char hexBuf[10];
            snprintf(hexBuf, sizeof(hexBuf), "%04X", (uint16_t)value);
            String deviceId = inifile.substring(0, 16);
            EnergyMeterProcessedData data = {deviceId, "EF00", "110", String(hexBuf), "numeric", true};
            publishEnergyMeterData(data);
            updateEnergyMeterValue(deviceId, "EF00", 110, String(hexBuf));
            log_i("Voltage L2 (DP%d -> attr 110): %.1f V", dpId, voltageV);
        }
        break;
            
        case 107:  // Current L2 (certains firmwares)
        case 113:  // Current L2 (autres firmwares)
        {
            float currentA = value / 1000.0f;
            char hexBuf[10];
            snprintf(hexBuf, sizeof(hexBuf), "%08lX", value);
            String deviceId = inifile.substring(0, 16);
            EnergyMeterProcessedData data = {deviceId, "EF00", "111", String(hexBuf), "numeric", true};
            publishEnergyMeterData(data);
            updateEnergyMeterValue(deviceId, "EF00", 111, String(hexBuf));
            log_i("Current L2 (DP%d -> attr 111): %.3f A", dpId, currentA);
        }
        break;
            
        case 108:  // Power L2 (certains firmwares)
        case 114:  // Power L2 (autres firmwares)
        {
            char hexBuf[10];
            snprintf(hexBuf, sizeof(hexBuf), "%08lX", value);
            String deviceId = inifile.substring(0, 16);
            EnergyMeterProcessedData data = {deviceId, "EF00", "112", String(hexBuf), "numeric", true};
            publishEnergyMeterData(data);
            updateEnergyMeterValue(deviceId, "EF00", 112, String(hexBuf));
            log_i("Power L2 (DP%d -> attr 112): %lu W", dpId, value);
        }
        break;
            
        // === Phase 3 (L3) - DP 109-123 ===
        case 109:  // Voltage L3 (certains firmwares)
        case 121:  // Voltage L3 (autres firmwares)
        {
            float voltageV = value / 10.0f;
            char hexBuf[10];
            snprintf(hexBuf, sizeof(hexBuf), "%04X", (uint16_t)value);
            String deviceId = inifile.substring(0, 16);
            EnergyMeterProcessedData data = {deviceId, "EF00", "120", String(hexBuf), "numeric", true};
            publishEnergyMeterData(data);
            updateEnergyMeterValue(deviceId, "EF00", 120, String(hexBuf));
            log_i("Voltage L3 (DP%d -> attr 120): %.1f V", dpId, voltageV);
        }
        break;
            
        case 110:  // Current L3 (certains firmwares)
        case 122:  // Current L3 (autres firmwares)
        {
            float currentA = value / 1000.0f;
            char hexBuf[10];
            snprintf(hexBuf, sizeof(hexBuf), "%08lX", value);
            String deviceId = inifile.substring(0, 16);
            EnergyMeterProcessedData data = {deviceId, "EF00", "121", String(hexBuf), "numeric", true};
            publishEnergyMeterData(data);
            updateEnergyMeterValue(deviceId, "EF00", 121, String(hexBuf));
            log_i("Current L3 (DP%d -> attr 121): %.3f A", dpId, currentA);
        }
        break;
            
        case 111:  // Total Power
            handleTotalPower(inifile, value);
            break;
            
        case 123:  // Power L3
        {
            char hexBuf[10];
            snprintf(hexBuf, sizeof(hexBuf), "%08lX", value);
            String deviceId = inifile.substring(0, 16);
            EnergyMeterProcessedData data = {deviceId, "EF00", "122", String(hexBuf), "numeric", true};
            publishEnergyMeterData(data);
            updateEnergyMeterValue(deviceId, "EF00", 122, String(hexBuf));
            log_i("Power L3 (DP%d -> attr 122): %lu W", dpId, value);
        }
        break;
            
        default:
            // Log les DP non gérés pour analyse
            handleEnergyMeterGenericDP(inifile, dpId, dpType, dpData, dataLen);
            break;
    }
}

// ============================================================================
// FONCTION PRINCIPALE - GESTIONNAIRE CLUSTER EF00 POUR COMPTEURS D'ENERGIE
// ============================================================================

void tuyaEnergyMeterManage(String inifile, int attribute, uint8_t datatype, 
                           int len, char* datas) {
    if (!ini_exist(inifile)) return;
    
    String deviceId = inifile.substring(0, 16);
    uint8_t* data = reinterpret_cast<uint8_t*>(datas);
    
    // Récupérer le manufacturer pour déterminer le type
    DeviceData* device = findEnergyMeterDevice(deviceId);
    if (!device) {
        log_e("Energy Meter device not found: %s", deviceId.c_str());
        return;
    }
    
    String manufacturer = device->getInfo().manufacturer;
    TuyaEnergyMeterType type = getTuyaEnergyMeterType(manufacturer);
    
    if (type == TUYA_METER_UNKNOWN) {
        log_w("Unknown Tuya energy meter manufacturer: %s - using SPM02 as default", manufacturer.c_str());
        type = TUYA_METER_SPM02;  // Fallback par défaut
    }
    
    log_d("Energy meter from %s (type %d), attr=%d, len=%d", 
          manufacturer.c_str(), type, attribute, len);
    
    // Format Tuya EF00: [seq:2][dpId:1][dpType:1][dataLen:2][data:N]...
    // Le séquence number est présent pour attribut 0 (status report)
    int offset = 0;
    
    // Skip sequence number si présent (attribut 0 = status report)
    if (attribute == 0 && len > 2) {
        offset = 2;
        log_d("Skipping Tuya sequence number, starting at offset %d", offset);
    }
    
    // Parser tous les datapoints dans le message
    int dpCount = 0;
    while (offset < len - 4) {
        uint8_t dpId = data[offset];
        uint8_t dpType = data[offset + 1];
        uint16_t dataLen = (data[offset + 2] << 8) | data[offset + 3];
        
        // Validation
        if (dataLen > 256 || offset + 4 + dataLen > len) {
            log_e("Invalid Tuya energy datapoint: DP%d len=%d at offset %d (total len=%d)", 
                  dpId, dataLen, offset, len);
            break;
        }
        
        log_d("Parsing DP%d type=%d len=%d at offset %d", dpId, dpType, dataLen, offset);
        
        handleEnergyMeterDatapoint(inifile, dpId, dpType, &data[offset + 4], dataLen, type);
        
        // Avancer au prochain datapoint
        offset += 4 + dataLen;
        dpCount++;
    }
    
    log_d("Parsed %d datapoints from energy meter", dpCount);
}

// ============================================================================
// FONCTION QUERY POUR INTERROGER LE COMPTEUR
// ============================================================================

void sendEnergyMeterQuery(int shortAddr, int endpoint) {
    // Utilise la même fonction que thermostat
    extern void sendTuyaDatapointQuery(int shortAddr, int endpoint);
    sendTuyaDatapointQuery(shortAddr, endpoint);
    log_i("Sent Energy Meter Query to 0x%04X endpoint %d", shortAddr, endpoint);
}