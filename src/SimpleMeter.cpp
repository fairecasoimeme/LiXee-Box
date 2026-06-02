#include <Arduino.h>
#include "SimpleMeter.h"
#include "config.h"
#include "log.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include <WebPush.h>
#include "mqtt.h"
#include "device.h"
#include "energyHistory.h"
#include <unordered_map>
#include <unordered_set>

// Déclarations extern (conservées)
extern DeviceList devices;
extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;
extern CircularBuffer<Device, 50> *deviceList;

// Cache pour éviter les recherches répétées
static PsUnorderedMap<DeviceData*> simpleMeterDeviceCache;
static bool simpleMeterCacheInitialized = false;

// Set des attributs d'énergie standard pour optimiser les comparaisons
static const std::unordered_set<int> ENERGY_ATTRIBUTES = {
    0, 256, 258, 260, 262, 264, 266, 268, 270, 272, 274
};

// Structure pour éviter la duplication
struct SimpleMeterData {
    String deviceId;
    String clusterId;
    String attributeStr;
    String value;
    String valueType;
    long numericValue;
    int attribute;
    bool isNumeric;
};

// Initialiser le cache des devices une seule fois
void initializeSimpleMeterDeviceCache() {
    if (simpleMeterCacheInitialized) return;
    
    simpleMeterDeviceCache.clear();
    for (size_t i = 0; i < devices.size(); i++) {
        DeviceData* device = devices[i];
        simpleMeterDeviceCache[device->getDeviceID().c_str()] = device;
    }
    simpleMeterCacheInitialized = true;
}

// Fonction helper pour vérifier si un device est un sous-compteur
bool isSubMeter(const String& deviceId) {  
    for (int i = 0; i < ConfigGeneral.subMeterCount; i++) {     
        if (ConfigGeneral.subMeters[i].enabled && 
            strcmp(ConfigGeneral.subMeters[i].IEEE, deviceId.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

// Fonction helper pour trouver rapidement un device
DeviceData* findSimpleMeterDevice(const String& deviceId) {
    if (!simpleMeterCacheInitialized) {
        initializeSimpleMeterDeviceCache();
    }
    
    auto it = simpleMeterDeviceCache.find(deviceId.c_str());
    return (it != simpleMeterDeviceCache.end()) ? it->second : nullptr;
}

// Fonction centralisée pour créer les données depuis les données brutes (format hex)
SimpleMeterData createHexMeterData(const String& inifile, int attribute, 
                                   uint8_t* datas, int len, const String& prefix = "") {
    String tmp = "";
    char value[3]; // Taille optimisée
    
    for(int i = 0; i < len; i++) {
        snprintf(value, sizeof(value), "%02X", datas[i]);
        tmp += value;
    }
    
    // Appliquer le préfixe si nécessaire (pour l'attribut 1)
    String finalValue = prefix + tmp;
    
    return {
        inifile.substring(0, 16),  // deviceId
        "1794",                    // clusterId
        String(attribute),         // attributeStr
        finalValue,                // value
        "numeric",                 // valueType
        strtol(finalValue.c_str(), NULL, 16), // numericValue
        attribute,                 // attribute
        true                       // isNumeric
    };
}

// Fonction centralisée pour créer les données depuis les données brutes (format texte)
SimpleMeterData createTextMeterData(const String& inifile, int attribute, 
                                    uint8_t* datas, int len, bool isString = false) {
    String tmp = "";
    
    for(int i = 0; i < len; i++) {
        if(datas[i] > 0) {
            tmp += (char)datas[i];
        }
    }
    
    return {
        inifile.substring(0, 16),  // deviceId
        "1794",                    // clusterId
        String(attribute),         // attributeStr
        tmp,                       // value
        isString ? "string" : "numeric", // valueType
        isString ? 0 : strtol(tmp.c_str(), NULL, 16), // numericValue
        attribute,                 // attribute
        !isString                  // isNumeric
    };
}

// Fonction centralisée pour publier les données de mesure
void publishSimpleMeterData(const SimpleMeterData& data, const String& inifile) {
    if (!ini_exist(inifile)) return;
    
    // MQTT
    if (ConfigSettings.enableMqtt) {
        mqttPublish(data.deviceId, data.clusterId, data.attributeStr, data.valueType, data.value);
    }
    
    // WebPush
    if (ConfigSettings.enableWebPush) {
        if (data.isNumeric) {
            String numValue = String(data.numericValue);
            WebPush(data.deviceId, data.clusterId, data.attributeStr, numValue.c_str());
        } else {
            WebPush(data.deviceId, data.clusterId, data.attributeStr, data.value);
        }
    }
    
    // Device list update
    if (!deviceList->isFull()) {
        int shortaddr = GetShortAddr(inifile);
        if (data.isNumeric) {
            deviceList->push(Device{shortaddr, 1794, data.attribute, String(data.numericValue)});
        } else {
            deviceList->push(Device{shortaddr, 1794, data.attribute, data.value});
        }
    }
}

// Fonction pour récupérer l'attribut ID correspondant à la période tarifaire actuelle
int getCurrentTariffAttributeId() {
    // Récupérer le device ZLinky
    DeviceData* zlinkyDevice = findSimpleMeterDevice(ConfigGeneral.ZLinky);
    if (!zlinkyDevice) {
        return 256;  // Par défaut, attribut BASE/HC
    }
    
    // Récupérer la période tarifaire (cluster FF66, attribut 16 = 0x0010)
    String ptec = zlinkyDevice->getValue("FF66", "16");
    if (ptec.length() == 0) {
        return 256;  // Par défaut
    }
    
    ptec.trim();
    ptec.toUpperCase();
    
    // Mapping PTEC -> Attribut ID
    // BASE / HC / Heures Creuses / HN (EJP) -> 256
    if (ptec.startsWith("BASE") || ptec.startsWith("TH") || 
        ptec == "HC.." || ptec.startsWith("HEURE") && ptec.indexOf("CREUSE") >= 0 ||
        ptec == "HN.." || ptec.startsWith("HCJB") || ptec.startsWith("EJPHN")) {
        return 256;
    }
    
    // HP / Heures Pleines / PM (EJP Pointe Mobile) -> 258
    if (ptec == "HP.." || ptec.startsWith("HEURE") && ptec.indexOf("PLEINE") >= 0 ||
        ptec == "PM.." || ptec.startsWith("HPJB") || ptec.startsWith("EJPHPM")) {
        return 258;
    }
    
    // Tempo Blanc HC -> 260
    if (ptec.startsWith("HCJW")) {
        return 260;
    }
    
    // Tempo Blanc HP -> 262
    if (ptec.startsWith("HPJW")) {
        return 262;
    }
    
    // Tempo Rouge HC -> 264
    if (ptec.startsWith("HCJR")) {
        return 264;
    }
    
    // Tempo Rouge HP -> 266
    if (ptec.startsWith("HPJR")) {
        return 266;
    }
    
    // Par défaut
    return 256;
}

// Fonction centralisée pour mettre à jour les valeurs des devices avec gestion des index
void updateSimpleMeterDeviceValue(const SimpleMeterData& data) {
    DeviceData* device = findSimpleMeterDevice(data.deviceId);
    if (!device) return;
    
    // Cas spécial pour ZLinky_TIC avec attribut 0
    if (device->getInfo().model == "ZLinky_TIC" && data.attribute == 0) {
        device->setValue("0702",
                        data.attributeStr.c_str(),
                        data.value.c_str());
        return;
    }

    // Cas général
    device->setValue("0702",
                    data.attributeStr.c_str(),
                    data.value.c_str());
    
    // Ajout aux mesures d'énergie si numérique
    if (data.attribute == 0 && data.isNumeric) {
      
        bool isZLinky = (strcmp(ConfigGeneral.ZLinky, data.deviceId.c_str()) == 0);
        bool isSubMeterDevice = isSubMeter(data.deviceId);
       
        if (isZLinky) {
            // ZLinky : stocker dans attribut 0 (comportement original)
            addEnergyMeasurement(device->energyHistory, data.attributeStr, data.numericValue);
        }
        else if (isSubMeterDevice) {
            // Sous-compteur : stocker dans l'attribut correspondant à la période tarifaire
            int tariffAttrId = getCurrentTariffAttributeId();
            //float coef = device->GetAttributeCoefficient(0x0702, data.attribute);
            //long correctedValue = (long)(data.numericValue * coef);
            addSubMeterMeasurement(device->energyHistory, tariffAttrId,  data.numericValue);
        }
    }
    
    // === NOUVEAU : Cas Production (attribut 1) ===
    if (data.attribute == 1 && data.isNumeric) {
        bool isProduction = (strcmp(ConfigGeneral.Production, data.deviceId.c_str()) == 0);
        if (isProduction) {
            addEnergyMeasurement(device->energyHistory, data.attributeStr, data.numericValue);
        }
    }
        
    // Cas spécial ZLinky avec index multiples (existant, inchangé)
    if (strcmp(ConfigGeneral.ZLinky, data.deviceId.c_str()) == 0) {
        if (data.isNumeric && data.attribute != 0) {
            addEnergyMeasurement(device->energyHistory, data.attributeStr, data.numericValue);
            
            // Mise à jour des index
            for (uint8_t i = 0; i < 11; ++i) {
                if (data.attribute == INDEX_ID_LIST[i]) {
                    device->updateIndex(i, data.numericValue);
                    break;
                }
            }
        }
    }
}

// Fonction pour gérer la logique spéciale de l'attribut 1 (ZLinky vs Production)
void handleSpecialAttribute1Logic(const SimpleMeterData& data) {
    // Gestion spéciale si ZLinky != Production
    if ((ConfigGeneral.ZLinky != ConfigGeneral.Production) && 
        (strcmp(ConfigGeneral.Production, "") != 0)) {
        
        DeviceData* zlinkyDevice = findSimpleMeterDevice(ConfigGeneral.ZLinky);
        if (zlinkyDevice) {
            zlinkyDevice->setValue("0702",
                                  data.attributeStr.c_str(),
                                  data.value.c_str());
            
            if (data.isNumeric) {
                addEnergyMeasurement(zlinkyDevice->energyHistory, data.attributeStr, data.numericValue);
                
                for (uint8_t i = 0; i < 11; ++i) {
                    if (data.attribute == INDEX_ID_LIST[i]) {
                        zlinkyDevice->updateIndex(i, data.numericValue);
                        break;
                    }
                }
            }
        }
    }
}

// Gestionnaire pour les attributs d'énergie standard (0, 256, 258, etc.)
void handleEnergyAttributes(const String& inifile, int attribute, uint8_t* datas, int len) {
    auto data = createHexMeterData(inifile, attribute, datas, len);
    
    publishSimpleMeterData(data, inifile);
    updateSimpleMeterDeviceValue(data);
}

// Gestionnaire pour l'attribut 1 (avec préfixe négatif et logique spéciale)
void handleAttribute1(const String& inifile, uint8_t* datas, int len) {
    auto data = createHexMeterData(inifile, 1, datas, len);
    
    publishSimpleMeterData(data, inifile);
    
    // Modifier la valeur pour ajouter le préfixe négatif après publication MQTT
    data.value = "-" + data.value;
    data.numericValue = strtol(data.value.c_str(), NULL, 16);
    
    updateSimpleMeterDeviceValue(data);
    handleSpecialAttribute1Logic(data);
}

// Gestionnaire pour l'attribut 32 (données texte numériques)
void handleAttribute32(const String& inifile, uint8_t* datas, int len) {
    auto data = createTextMeterData(inifile, 32, datas, len, false);
    
    publishSimpleMeterData(data, inifile);
    
    // Pour l'attribut 32, on met à jour sans les fonctionnalités d'énergie
    DeviceData* device = findSimpleMeterDevice(data.deviceId);
    if (device) {
        device->setValue("0702",
                        data.attributeStr.c_str(),
                        data.value.c_str());
    }
}

// Gestionnaire pour l'attribut 776 (données string)
void handleAttribute776(const String& inifile, uint8_t* datas, int len) {
    auto data = createTextMeterData(inifile, 776, datas, len, true);

    publishSimpleMeterData(data, inifile);

    // Pour l'attribut 776, on met à jour sans les fonctionnalités d'énergie
    DeviceData* device = findSimpleMeterDevice(data.deviceId);
    if (device) {
        device->setValue("0702",
                        data.attributeStr.c_str(),
                        data.value.c_str());
    }
}

// Gestionnaire pour les attributs par défaut
void handleDefaultAttribute(const String& inifile, int attribute, uint8_t* datas, int len) {
    auto data = createHexMeterData(inifile, attribute, datas, len);

    publishSimpleMeterData(data, inifile);

    // Pour les attributs par défaut, mise à jour simple sans fonctionnalités d'énergie
    DeviceData* device = findSimpleMeterDevice(data.deviceId);
    if (device) {
        device->setValue("0702",
                        data.attributeStr.c_str(),
                        data.value.c_str());
    }
}

// Fonction principale optimisée
void SimpleMeterManage(String inifile, int attribute, uint8_t datatype, 
                      int len, char* datas) {
    // Cast vers uint8_t* pour plus de clarté
    uint8_t* data = reinterpret_cast<uint8_t*>(datas);
    
    // Dispatch optimisé vers les gestionnaires spécialisés
    if (ENERGY_ATTRIBUTES.find(attribute) != ENERGY_ATTRIBUTES.end()) {
        // Attributs d'énergie standard (0, 256, 258, etc.)
        handleEnergyAttributes(inifile, attribute, data, len);
    } else {
        // Switch pour les cas spéciaux
        switch (attribute) {
            case 1:
                handleAttribute1(inifile, data, len);
                break;
                
            case 32:
                handleAttribute32(inifile, data, len);
                break;
                
            case 776:
                handleAttribute776(inifile, data, len);
                break;
                
            default:
                handleDefaultAttribute(inifile, attribute, data, len);
                break;
        }
    }
}

// Fonction utilitaire pour invalider le cache si les devices changent
void invalidateSimpleMeterDeviceCache() {
    simpleMeterCacheInitialized = false;
    simpleMeterDeviceCache.clear();
}

// Fonction utilitaire pour obtenir les statistiques du cache
size_t getSimpleMeterCacheSize() {
    return simpleMeterDeviceCache.size();
}

// Fonction utilitaire pour vérifier si un attribut est un attribut d'énergie
bool isEnergyAttribute(int attribute) {
    return ENERGY_ATTRIBUTES.find(attribute) != ENERGY_ATTRIBUTES.end();
}