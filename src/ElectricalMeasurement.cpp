#include <Arduino.h>
#include "ElectricalMeasurement.h"
#include "config.h"
#include "log.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include "zigbee.h"
#include <AsyncMqttClient.h>
#include <WebPush.h>
#include "mqtt.h"
#include "udpclient.h"
#include "device.h"
#include "powerHistory.h"
#include "notificationManager.h"
#include <map>
#include <unordered_map>



// Déclarations extern (conservées)
extern DeviceList devices;
extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigNotification ConfigNotif;
extern ConfigSettingsStruct ConfigSettings;
extern CircularBuffer<Device, 50> *deviceList;
extern CircularBuffer<Notification, 10> *notifList;
extern NotificationManager notificationManager;

// Variables globales optimisées
static float oldPowerOutage = 0.0f;
static std::map<int, bool, std::less<int>, PsramAllocator<std::pair<const int, bool>>> oldOverTension = {
    {1285, false},
    {2309, false},
    {2565, false}
};
static std::map<int, bool, std::less<int>, PsramAllocator<std::pair<const int, bool>>> oldUnderTension = {
    {1285, false},
    {2309, false},
    {2565, false}
};

// Cache pour éviter les recherches répétées (PSRAM)
static PsUnorderedMap<DeviceData*> electricalDeviceCache;
static bool electricalCacheInitialized = false;

// Structure pour éviter la duplication
struct ElectricalMeasurementData {
    String deviceId;
    String clusterId;
    String attributeStr;
    String value;
    long numericValue;
    int attribute;
};

// Initialiser le cache des devices une seule fois
void initializeElectricalDeviceCache() {
    if (electricalCacheInitialized) return;
    
    electricalDeviceCache.clear();
    for (size_t i = 0; i < devices.size(); i++) {
        DeviceData* device = devices[i];
        electricalDeviceCache[device->getDeviceID().c_str()] = device;
    }
    electricalCacheInitialized = true;
}

// Fonction helper pour trouver rapidement un device
DeviceData* findElectricalDevice(const String& deviceId) {
    if (!electricalCacheInitialized) {
        initializeElectricalDeviceCache();
    }
    
    auto it = electricalDeviceCache.find(deviceId.c_str());
    return (it != electricalDeviceCache.end()) ? it->second : nullptr;
}

// Fonction helper pour obtenir le nom de phase
String getPhaseFromAttribute(int attribute) {
    switch (attribute) {
        case 1285: return "1";
        case 2309: return "2";
        case 2565: return "3";
        default: return "Unknown";
    }
}

// Fonction centralisée pour gérer les notifications électriques
void handleElectricalNotification(const String& title, const String& text, int priority = 1,
                                  const char* alertType = nullptr, float value = 0, float threshold = 0) {
    if (!notifList->isFull()) {
        notifList->push(Notification{title, text, FormattedDate, 1, 0});
    } else {
        notifList->shift();
        notifList->push(Notification{title, text, FormattedDate, 1, 0});
    }
    notificationManager.addNotification(title, text, priority, alertType, value, threshold);
}

// Fonction centralisée pour créer les données de mesure depuis les données brutes
ElectricalMeasurementData createMeasurementData(const String& inifile, int attribute,
                                               uint8_t* datas, int len, uint8_t datatype) {
    String tmp = "";
    char value[3]; // Taille optimisée

    for(int i = 0; i < len; i++) {
        snprintf(value, sizeof(value), "%02X", datas[i]);
        tmp += value;
    }

    // Issue #34 : ce cluster porte des grandeurs SIGNEES (Active Power int16, Power Factor int8,
    // courbes de charge...). Stockees telles quelles, elles etaient relues en non signe :
    // 0xFFE4 (-28 W) devenait 65508. Si le type ZCL est signe et la valeur negative, on etend le
    // signe sur 32 bits avant stockage -> "FFFFFFE4", que zclHexToSigned() relit comme -28.
    if (zclTypeIsSigned(datatype) && len > 0 && len < 4 && (datas[0] & 0x80)) {
        String ext = "";
        for (int i = len; i < 4; i++) ext += "FF";
        tmp = ext + tmp;
    }

    return {
        inifile.substring(0, 16),  // deviceId
        "2820",                    // clusterId
        String(attribute),         // attributeStr
        tmp,                       // value
        zclHexToSigned(tmp.c_str()),                    // numericValue (signe)
        attribute                  // attribute
    };
}

// Fonction centralisée pour publier les données de mesure électrique
void publishElectricalData(const ElectricalMeasurementData& data, const String& inifile) {
    if (!ini_exist(inifile)) return;
    
    // MQTT
    if (ConfigSettings.enableMqtt) {
        mqttPublish(data.deviceId, data.clusterId, data.attributeStr, "numeric", data.value);
    }
    
    // WebPush
    if (ConfigSettings.enableWebPush) {
        
        WebPush(data.deviceId, data.clusterId, data.attributeStr, String(data.numericValue).c_str());
    }
    
    // UDP (uniquement pour certains attributs comme 1295)
    if (ConfigSettings.enableUDP && data.attribute == 1295) {
        UDPsend(String(data.numericValue));
    }
    
    // Device list update (avec coefficient pour l'affichage web)
    if (!deviceList->isFull()) {
        int shortaddr = GetShortAddr(inifile);
        DeviceData* dev = nullptr;
        for (size_t i = 0; i < devices.size(); i++) {
            if (devices[i]->getDeviceID() == data.deviceId) { dev = devices[i]; break; }
        }
        float coeff = (dev != nullptr) ? dev->GetAttributeCoefficient(2820, data.attribute) : 1.0;
        float adjustedValue = data.numericValue * coeff;
        deviceList->push(Device{shortaddr, 2820, data.attribute, String(adjustedValue)});
    }
}

// Fonction centralisée pour mettre à jour les valeurs des devices
void updateElectricalDeviceValue(const ElectricalMeasurementData& data) {
    DeviceData* device = findElectricalDevice(data.deviceId);
    if (!device) return;

    String value = data.value;

    //GESTION INJECTION AUTOCONSOMMATION
    // Timestamps pour synchronisation PAPP/URMS/IRMS
    // Les données remontent toutes les ~1 minute ; on attend que URMS/IRMS
    // soient mis à jour APRES que PAPP soit passé à 0 avant de calculer V*I
    static unsigned long lastUrmsIrmsUpdate = 0;  // millis() du dernier update URMS ou IRMS
    static unsigned long pappZeroSince = 0;        // millis() du premier PAPP==0 (0 = pas en état zéro)
    static bool wasInjecting = false;              // true si PAPP <= 0 lors de la dernière lecture

    if (strcmp(ConfigGeneral.ZLinky,data.deviceId.c_str()) == 0 )
    {
        // Tracker les mises à jour URMS/IRMS (attrs 1285,1288,2309,2312,2565,2568)
        if (data.attribute == 1285 || data.attribute == 1288 ||
            data.attribute == 2309 || data.attribute == 2312 ||
            data.attribute == 2565 || data.attribute == 2568)
        {
            lastUrmsIrmsUpdate = millis();
        }

        long tmpValue = data.numericValue;
        long injection = 0 ;

        if ((data.attribute == 1295) ||(data.attribute == 2319) ||(data.attribute == 2575))
        {
            if (tmpValue > 0)
            {
                // Consommation normale : RAZ injection si on était en injection
                if (wasInjecting) {
                    int injAttr = (data.attribute == 1295) ? 1 : (data.attribute == 2319) ? 2 : 3;
                    addMeasurement(device->powerHistory, injAttr, 0);
                    wasInjecting = false;
                }
                pappZeroSince = 0;
            }
            // PAPP négatif = injection en cours : utiliser |PAPP| comme puissance d'injection
            else if (tmpValue < 0)
            {
                wasInjecting = true;
                pappZeroSince = 0;
                injection = tmpValue; // valeur négative
                int injAttr = (data.attribute == 1295) ? 1 : (data.attribute == 2319) ? 2 : 3;
                addMeasurement(device->powerHistory, injAttr, injection);
                value = "0"; // consommation affichée = 0
            }
            // PAPP == 0 : estimer l'injection depuis URMS * IRMS
            // Attendre que URMS/IRMS aient été mis à jour depuis le passage à PAPP==0
            else // tmpValue == 0
            {
                if (pappZeroSince == 0) {
                    pappZeroSince = millis();
                }

                // URMS/IRMS synchronisés si :
                // 1) Mis à jour récemment (< 15s = même burst de données), OU
                // 2) Mis à jour après que PAPP soit passé à 0, OU
                // 3) Timeout de 2 minutes (sécurité)
                bool recentUpdate = (lastUrmsIrmsUpdate > 0) && (millis() - lastUrmsIrmsUpdate < 15000);
                bool synced = recentUpdate ||
                              (lastUrmsIrmsUpdate > pappZeroSince) ||
                              (millis() - pappZeroSince > 120000);

                if (!synced)
                {
                    // Transition conso→injection : URMS/IRMS pas encore rafraîchis
                    Serial.printf("[Energy] PAPP=0 : attente sync URMS/IRMS (depuis %lus)\n",
                                  (millis() - pappZeroSince) / 1000);
                }
                else
                {
                    wasInjecting = true;
                    // URMS/IRMS synchronisés, calculer injection depuis V*I
                    switch (ConfigGeneral.LinkyMode)
                    {
                        case 1 :
                        case 5 :
                        {
                            //mode STANDARD MONOPHASE
                            if (data.attribute == 1295)
                            {
                                long URMS1 =  strtol(device->getValue("0B04", "1285").c_str(),0,16);
                                long IRMS1 =  strtol(device->getValue("0B04", "1288").c_str(),0,16);
                                injection = -URMS1 * IRMS1;
                                value = String(injection, HEX);
                                value.toUpperCase();
                                addMeasurement(device->powerHistory, 1, injection);
                            }
                        }
                        break;
                        case 3:
                        case 7:
                        {
                            //mode STANDARD TRIPHASE
                            if (data.attribute == 1295)
                            {
                                long URMS1 =  strtol(device->getValue("0B04", "1285").c_str(),0,16);
                                long IRMS1 =  strtol(device->getValue("0B04", "1288").c_str(),0,16);
                                injection = -URMS1 * IRMS1;
                                value = String(injection, HEX);
                                value.toUpperCase();
                                addMeasurement(device->powerHistory, 1, injection);
                            }else if (data.attribute == 2319)
                            {
                                long URMS2 =  strtol(device->getValue("0B04", "2309").c_str(),0,16);
                                long IRMS2 =  strtol(device->getValue("0B04", "2312").c_str(),0,16);
                                injection = -URMS2 * IRMS2;
                                value = String(injection, HEX);
                                value.toUpperCase();
                                addMeasurement(device->powerHistory, 2, injection);
                            }else if (data.attribute == 2575)
                            {
                                long URMS3 =  strtol(device->getValue("0B04", "2565").c_str(),0,16);
                                long IRMS3 =  strtol(device->getValue("0B04", "2568").c_str(),0,16);
                                injection = -URMS3 * IRMS3;
                                value = String(injection, HEX);
                                value.toUpperCase();
                                addMeasurement(device->powerHistory, 3, injection);
                            }
                        }
                        break;
                        case 0:
                        {
                            //mode HISTORIQUE monophasé
                            if (data.attribute == 1295)
                            {
                                long voltage = 200;
                                long IINST =  strtol(device->getValue("0B04", "1288").c_str(),0,16);
                                injection = -voltage * IINST;
                                value = String(injection, HEX);
                                value.toUpperCase();
                                addMeasurement(device->powerHistory, 1, injection);
                            }
                        }
                        break;
                        case 2:
                        {
                            //mode HISTORIQUE triphasé
                            long voltage = 200;
                            if (data.attribute == 1295)
                            {
                                long IINST1 =  strtol(device->getValue("0B04", "1288").c_str(),0,16);
                                injection = -voltage * IINST1;
                                value = String(injection, HEX);
                                value.toUpperCase();
                                addMeasurement(device->powerHistory, 1, injection);
                            }else if (data.attribute == 2319)
                            {
                                long IINST2 =  strtol(device->getValue("0B04", "2312").c_str(),0,16);
                                injection = -voltage * IINST2;
                                value = String(injection, HEX);
                                value.toUpperCase();
                                addMeasurement(device->powerHistory, 2, injection);
                            }else if (data.attribute == 2575)
                            {
                                long IINST3 =  strtol(device->getValue("0B04", "2568").c_str(),0,16);
                                injection = -voltage * IINST3;
                                value = String(injection, HEX);
                                value.toUpperCase();
                                addMeasurement(device->powerHistory, 3, injection);
                            }
                        }
                        break;

                        default:
                        break;
                    }
                }
            }
        }
    }
    

    device->setValue("0B04",
                    data.attributeStr.c_str(),
                    value.c_str());
    
    if ((ConfigGeneral.Production == device->getDeviceID().c_str()) && (data.attribute == 519))
    {
        addMeasurement(device->powerHistory, data.attribute, data.numericValue);
    }

    // Stocker la consommation dans powerHistory (0 si injection, sinon valeur réelle)
    long powerValue = (data.numericValue < 0) ? 0 : data.numericValue;
    if ((ConfigGeneral.LinkyMode == 2) || (ConfigGeneral.LinkyMode == 3) || (ConfigGeneral.LinkyMode == 7))
    {
        if ((data.attribute == 1295) ||(data.attribute == 2319) ||(data.attribute == 2575))
        {
            addMeasurement(device->powerHistory, data.attribute, powerValue);
        }
    }else{
        if ((data.attribute == 1295) )
        {
            addMeasurement(device->powerHistory, data.attribute, powerValue);
        }
    }    
}

// Gestion des notifications de puissance pour l'attribut 1295
void handlePowerOutageNotification(const ElectricalMeasurementData& data) {
    if (!ConfigNotif.PowerOutage || 
        strcmp(ConfigGeneral.ZLinky, data.deviceId.c_str()) != 0) {
        return;
    }
    
    if (data.numericValue == 0 && oldPowerOutage != 0) {
        String text = "La puissance apparente de " + String(ConfigGeneral.ZLinky) + " est à 0 VA";
        handleElectricalNotification("⚡0️⃣Puissance apparente nulle", text, 1, "power_outage");
    }
    oldPowerOutage = data.numericValue;
}

// Gestion des notifications de tension (surtension et sous-tension)
void handleVoltageNotifications(const ElectricalMeasurementData& data) {
    // Vérifier si c'est un attribut de tension valide
    if (data.attribute != 1285 && data.attribute != 2309 && data.attribute != 2565) {
        return;
    }
    
    if (strcmp(ConfigGeneral.ZLinky, data.deviceId.c_str()) != 0) {
        return;
    }
    
    String phase = getPhaseFromAttribute(data.attribute);
    long tension = data.numericValue;
    
    // Gestion surtension
    if (ConfigNotif.OverVoltage) {
        if (ConfigNotif.OverVoltageThreshold < tension) {
            if (!oldOverTension[data.attribute]) {
                oldOverTension[data.attribute] = true;
                String text = "Tension relevée Ph" + phase + " : " + String(tension) + " V";
                handleElectricalNotification("⚡⚠️ Sur-tension", text, 1,
                    "over_voltage", (float)tension, (float)ConfigNotif.OverVoltageThreshold);
            }
        } else {
            oldOverTension[data.attribute] = false;
        }
    }

    // Gestion sous-tension
    if (ConfigNotif.UnderVoltage) {
        if (ConfigNotif.UnderVoltageThreshold > tension) {
            if (!oldUnderTension[data.attribute]) {
                oldUnderTension[data.attribute] = true;
                String phase = getPhaseFromAttribute(data.attribute);
                String text = "Tension relevée Ph" + phase + " : " + String(tension) + " V";
                handleElectricalNotification("⚡⚠️ Sous-tension", text, 1,
                    "under_voltage", (float)tension, (float)ConfigNotif.UnderVoltageThreshold);
            }
        } else {
            oldUnderTension[data.attribute] = false;
        }
    }
}

// Gestionnaire pour l'attribut 1295 (puissance apparente avec notifications spéciales)
void handleAttribute1295(const String& inifile, uint8_t* datas, int len, uint8_t datatype) {
    auto data = createMeasurementData(inifile, 1295, datas, len, datatype);
    
    publishElectricalData(data, inifile);
    updateElectricalDeviceValue(data);
    handlePowerOutageNotification(data);
}

// Gestionnaire pour les attributs 2319 et 2575 (mesures standard)
void handleStandardMeasurement(const String& inifile, int attribute, uint8_t* datas, int len, uint8_t datatype) {
    auto data = createMeasurementData(inifile, attribute, datas, len, datatype);
    
    publishElectricalData(data, inifile);
    updateElectricalDeviceValue(data);
}

// Gestionnaire pour les attributs par défaut (avec gestion des tensions)
void handleDefaultMeasurement(const String& inifile, int attribute, uint8_t* datas, int len, uint8_t datatype) {
    auto data = createMeasurementData(inifile, attribute, datas, len, datatype);
    
    publishElectricalData(data, inifile);
    updateElectricalDeviceValue(data);
    handleVoltageNotifications(data);
}

// Fonction principale optimisée
void ElectricalMeasurementManage(String inifile, int attribute, uint8_t datatype, 
                                int len, char* datas) {
    // Vérification de base
    if (inifile.isEmpty()) return;
    
    // Cast vers uint8_t* pour plus de clarté
    uint8_t* data = reinterpret_cast<uint8_t*>(datas);
    
    // Dispatch vers les gestionnaires spécialisés
    switch (attribute) {
        case 1295:
            handleAttribute1295(inifile, data, len, datatype);
            break;
            
        case 2319:
        case 2575:
            handleStandardMeasurement(inifile, attribute, data, len, datatype);
            break;
            
        default:
            handleDefaultMeasurement(inifile, attribute, data, len, datatype);
            break;
    }
}

// Fonction utilitaire pour invalidar le cache si les devices changent
void invalidateElectricalDeviceCache() {
    electricalCacheInitialized = false;
    electricalDeviceCache.clear();
}

// Fonction utilitaire pour obtenir les statistiques du cache
size_t getElectricalCacheSize() {
    return electricalDeviceCache.size();
}