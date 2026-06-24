#include <Arduino.h>
#include "tuyapresence.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include "mqtt.h"
#include "device.h"
#include "presence.h"

extern DeviceList devices;
extern AsyncMqttClient mqttClient;
extern ConfigSettingsStruct ConfigSettings;
extern CircularBuffer<Device, 50> *deviceList;
extern CircularBuffer<Packet, 100> *commandList;

// ============================================================================
// TABLES DE CORRESPONDANCE MANUFACTURER -> CAPTEUR DE PRESENCE
// ============================================================================

// Capteurs de presence radar Tuya (ZY-M100, M100-S, etc.)
static const char* TUYA_PRESENCE_MANUFACTURERS[] = {
    "_TZE204_qasjif9e",
    "_TZE200_qasjif9e",
    nullptr
};

static bool isInPresenceManufacturerList(const String& manufacturer, const char* list[]) {
    for (int i = 0; list[i] != nullptr; i++) {
        if (manufacturer == list[i]) {
            return true;
        }
    }
    return false;
}

bool isTuyaPresenceSensor(const String& manufacturer) {
    return isInPresenceManufacturerList(manufacturer, TUYA_PRESENCE_MANUFACTURERS);
}

// ============================================================================
// CACHE DEVICE
// ============================================================================
static PsUnorderedMap<DeviceData*> presenceSensorCache;
static bool presenceSensorCacheInitialized = false;

static void initializePresenceSensorCache() {
    if (presenceSensorCacheInitialized) return;
    presenceSensorCache.clear();
    for (size_t i = 0; i < devices.size(); i++) {
        DeviceData* device = devices[i];
        presenceSensorCache[device->getDeviceID().c_str()] = device;
    }
    presenceSensorCacheInitialized = true;
}

static DeviceData* findPresenceSensorDevice(const String& deviceId) {
    if (!presenceSensorCacheInitialized) {
        initializePresenceSensorCache();
    }
    auto it = presenceSensorCache.find(deviceId.c_str());
    if (it != presenceSensorCache.end()) return it->second;

    // Cache miss - chercher dans la liste complete (device ajoute apres init du cache)
    for (size_t i = 0; i < devices.size(); i++) {
        DeviceData* device = devices[i];
        if (device->getDeviceID() == deviceId) {
            presenceSensorCache[device->getDeviceID().c_str()] = device;
            return device;
        }
    }
    return nullptr;
}

// ============================================================================
// PUBLICATION ET STOCKAGE DES DONNEES
// ============================================================================

struct PresenceSensorProcessedData {
    String deviceId;
    String clusterId;
    String attributeStr;
    String value;
    String valueType;
    bool isNumeric;
};

static void publishPresenceSensorData(const PresenceSensorProcessedData& data) {
    DeviceData* device = findPresenceSensorDevice(data.deviceId);

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

static void updatePresenceSensorValue(const String& deviceId, const String& cluster,
                                      int attribute, const String& value) {
    DeviceData* device = findPresenceSensorDevice(deviceId);
    if (device) {
        device->setValue(cluster.c_str(),
                        String(attribute).c_str(),
                        value.c_str());
    }
}

// ============================================================================
// EXTRACTION DE VALEUR TUYA (big-endian)
// ============================================================================

static uint32_t getPresenceDPValue(uint8_t* data, int dataLen) {
    uint32_t value = 0;
    for (int i = 0; i < dataLen && i < 4; i++) {
        value = (value << 8) | data[i];
    }
    return value;
}

// ============================================================================
// GESTIONNAIRE DE DATAPOINTS
// ============================================================================

static void handlePresenceSensorDatapoint(const String& inifile, uint8_t dpId, uint8_t dpType,
                                           uint8_t* dpData, int dataLen) {
    if (!ini_exist(inifile)) return;

    uint32_t value = getPresenceDPValue(dpData, dataLen);
    String deviceId = inifile.substring(0, 16);

    char hexBuf[10];
    if (dataLen == 1) {
        snprintf(hexBuf, sizeof(hexBuf), "%02X", (uint8_t)value);
    } else if (dataLen == 2) {
        snprintf(hexBuf, sizeof(hexBuf), "%04X", (uint16_t)value);
    } else {
        snprintf(hexBuf, sizeof(hexBuf), "%08lX", value);
    }

    // Stocker la valeur brute sur l'attribut = dpId (mapping direct)
    PresenceSensorProcessedData data = {deviceId, "EF00", String(dpId), String(hexBuf), "numeric", true};
    publishPresenceSensorData(data);
    updatePresenceSensorValue(deviceId, "EF00", dpId, String(hexBuf));

    switch (dpId) {
        case 1:  // Presence (enum: 0=none, 1=presence)
        {
            bool occupied = (value != 0);
            log_i("Presence (DP1): %s", occupied ? "DETECTED" : "none");

            // Integrer avec le systeme de presence existant
            DeviceData* device = findPresenceSensorDevice(deviceId);
            if (device) {
                String model = device->getInfo().model;
                handleOccupancyChange(deviceId, model, occupied ? 1 : 0);
            }
            break;
        }
        case 2:  // Sensibilite mouvement (0-9)
            log_i("Motion sensitivity (DP2): %lu", value);
            break;

        case 3:  // Distance min detection (0.01m)
            log_i("Min range (DP3): %lu (%.2f m)", value, value / 100.0f);
            break;

        case 4:  // Distance max detection (0.01m)
            log_i("Max range (DP4): %lu (%.2f m)", value, value / 100.0f);
            break;

        case 9:  // Distance cible mesuree (0.01m)
            log_i("Target distance (DP9): %lu (%.2f m)", value, value / 100.0f);
            break;

        case 101:  // Fade time
            log_i("Fade time (DP101): %lu", value);
            break;

        case 102:  // Detection delay
            log_i("Detection delay (DP102): %lu", value);
            break;

        case 104:  // Luminosite (lux)
            log_i("Illuminance (DP104): %lu lux", value);
            break;

        default:
            log_d("Presence sensor DP%d (type:%d len:%d): %lu (0x%s)", dpId, dpType, dataLen, value, hexBuf);
            break;
    }
}

// ============================================================================
// GESTIONNAIRE PRINCIPAL
// ============================================================================

void tuyaPresenceSensorManage(String inifile, int attribute, uint8_t datatype,
                              int len, char* datas) {
    if (!ini_exist(inifile)) return;

    String deviceId = inifile.substring(0, 16);
    uint8_t* data = reinterpret_cast<uint8_t*>(datas);

    log_d("Presence sensor from %s, attr=%d, len=%d", deviceId.c_str(), attribute, len);

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
            log_e("Invalid Tuya presence datapoint: DP%d len=%d at offset %d (total len=%d)",
                  dpId, dataLen, offset, len);
            break;
        }

        handlePresenceSensorDatapoint(inifile, dpId, dpType, &data[offset + 4], dataLen);

        offset += 4 + dataLen;
        dpCount++;
    }

    log_d("Parsed %d datapoints from presence sensor", dpCount);
}
