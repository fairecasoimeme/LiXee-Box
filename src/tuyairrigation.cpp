#include <Arduino.h>
#include "tuyairrigation.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include "mqtt.h"
#include "device.h"

extern DeviceList devices;
extern AsyncMqttClient mqttClient;
extern ConfigSettingsStruct ConfigSettings;
extern CircularBuffer<Device, 50> *deviceList;
extern CircularBuffer<Packet, 100> *commandList;

// ============================================================================
// TABLES DE CORRESPONDANCE MANUFACTURER -> VANNE IRRIGATION
// ============================================================================

static const char* TUYA_IRRIGATION_MANUFACTURERS[] = {
    "_TZE200_arge1ptm",
    "_TZE200_sh1btabb",
    "_TZE200_a7sghmms",
    "_TZE200_7ytb3h8u",
    nullptr
};

static bool isInIrrigationManufacturerList(const String& manufacturer, const char* list[]) {
    for (int i = 0; list[i] != nullptr; i++) {
        if (manufacturer == list[i]) {
            return true;
        }
    }
    return false;
}

bool isTuyaIrrigation(const String& manufacturer) {
    return isInIrrigationManufacturerList(manufacturer, TUYA_IRRIGATION_MANUFACTURERS);
}

// ============================================================================
// CACHE DEVICE
// ============================================================================
static PsUnorderedMap<DeviceData*> irrigationCache;
static bool irrigationCacheInitialized = false;

static void initializeIrrigationCache() {
    if (irrigationCacheInitialized) return;
    irrigationCache.clear();
    for (size_t i = 0; i < devices.size(); i++) {
        DeviceData* device = devices[i];
        irrigationCache[device->getDeviceID().c_str()] = device;
    }
    irrigationCacheInitialized = true;
}

static DeviceData* findIrrigationDevice(const String& deviceId) {
    if (!irrigationCacheInitialized) {
        initializeIrrigationCache();
    }
    auto it = irrigationCache.find(deviceId.c_str());
    if (it != irrigationCache.end()) return it->second;

    // Cache miss - chercher dans la liste complete
    for (size_t i = 0; i < devices.size(); i++) {
        DeviceData* device = devices[i];
        if (device->getDeviceID() == deviceId) {
            irrigationCache[device->getDeviceID().c_str()] = device;
            return device;
        }
    }
    return nullptr;
}

// ============================================================================
// PUBLICATION ET STOCKAGE DES DONNEES
// ============================================================================

struct IrrigationProcessedData {
    String deviceId;
    String clusterId;
    String attributeStr;
    String value;
    String valueType;
    bool isNumeric;
};

static void publishIrrigationData(const IrrigationProcessedData& data) {
    DeviceData* device = findIrrigationDevice(data.deviceId);

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
            } else {
                deviceList->push(Device{shortaddr, clusterInt, attrInt, String(rawValue)});
            }
        } else {
            deviceList->push(Device{shortaddr, clusterInt, attrInt, String(data.value)});
        }
    }
}

static void updateIrrigationValue(const String& deviceId, const String& cluster,
                                   int attribute, const String& value) {
    DeviceData* device = findIrrigationDevice(deviceId);
    if (device) {
        device->setValue(cluster.c_str(),
                        String(attribute).c_str(),
                        value.c_str());
    }
}

// ============================================================================
// EXTRACTION DE VALEUR TUYA (big-endian)
// ============================================================================

static uint32_t getIrrigationDPValue(uint8_t* data, int dataLen) {
    uint32_t value = 0;
    for (int i = 0; i < dataLen && i < 4; i++) {
        value = (value << 8) | data[i];
    }
    return value;
}

// ============================================================================
// GESTIONNAIRE DE DATAPOINTS
// ============================================================================

static void handleIrrigationDatapoint(const String& inifile, uint8_t dpId, uint8_t dpType,
                                       uint8_t* dpData, int dataLen) {
    if (!ini_exist(inifile)) return;

    uint32_t value = getIrrigationDPValue(dpData, dataLen);
    String deviceId = inifile.substring(0, 16);

    char hexBuf[10];
    if (dataLen == 1) {
        snprintf(hexBuf, sizeof(hexBuf), "%02X", (uint8_t)value);
    } else if (dataLen == 2) {
        snprintf(hexBuf, sizeof(hexBuf), "%04X", (uint16_t)value);
    } else {
        snprintf(hexBuf, sizeof(hexBuf), "%08lX", (unsigned long)value);
    }

    // Stocker la valeur brute sur l'attribut = dpId (mapping direct)
    IrrigationProcessedData data = {deviceId, "EF00", String(dpId), String(hexBuf), "numeric", true};
    publishIrrigationData(data);
    updateIrrigationValue(deviceId, "EF00", dpId, String(hexBuf));

    switch (dpId) {
        case 2:  // Vanne avec auto-fermeture (value: 0-100%)
            log_i("Valve auto-shutdown (DP2): %lu%%", value);
            break;

        case 3:  // Debit eau (%)
            log_i("Water flow (DP3): %lu%%", value);
            break;

        case 11:  // Timer fermeture automatique (secondes, 0-14400)
            log_i("Shutdown timer (DP11): %lu s", value);
            break;

        case 101:  // Temps d'arrosage restant (secondes)
            log_i("Remaining watering time (DP101): %lu s", value);
            break;

        case 102:  // Etat vanne (value: 0-100%)
            log_i("Valve state (DP102): %lu%%", value);
            break;

        case 107:  // Duree derniere irrigation (secondes)
            log_i("Last watering time (DP107): %lu s", value);
            break;

        case 110:  // Batterie (%)
            log_i("Battery (DP110): %lu%%", value);
            break;

        default:
            log_d("Irrigation DP%d (type:%d len:%d): %lu (0x%s)", dpId, dpType, dataLen, value, hexBuf);
            break;
    }
}

// ============================================================================
// GESTIONNAIRE PRINCIPAL
// ============================================================================

void tuyaIrrigationManage(String inifile, int attribute, uint8_t datatype,
                           int len, char* datas) {
    if (!ini_exist(inifile)) return;

    String deviceId = inifile.substring(0, 16);
    uint8_t* data = reinterpret_cast<uint8_t*>(datas);

    log_d("Irrigation from %s, attr=%d, len=%d", deviceId.c_str(), attribute, len);

    // Format Tuya EF00: [seq:2][dpId:1][dpType:1][dataLen:2][data:N]...
    int offset = 0;

    // Skip sequence number si present (attribut 0 = status report)
    if (attribute == 0 && len > 2) {
        offset = 2;
    }

    // Parser tous les datapoints dans le message
    int dpCount = 0;
    while (offset < len - 4) {
        uint8_t dpId = data[offset];
        uint8_t dpType = data[offset + 1];
        uint16_t dataLen = (data[offset + 2] << 8) | data[offset + 3];

        // Validation
        if (dataLen > 256 || offset + 4 + dataLen > len) {
            log_e("Invalid Tuya irrigation datapoint: DP%d len=%d at offset %d (total len=%d)",
                  dpId, dataLen, offset, len);
            break;
        }

        handleIrrigationDatapoint(inifile, dpId, dpType, &data[offset + 4], dataLen);

        offset += 4 + dataLen;
        dpCount++;
    }

    log_d("Parsed %d datapoints from irrigation device", dpCount);
}
