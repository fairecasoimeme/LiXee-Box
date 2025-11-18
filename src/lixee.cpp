#include <Arduino.h>
#include "lixee.h"
#include "onoff.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include <WebPush.h>
#include "mqtt.h"
#include "device.h"
#include "notificationManager.h"
#include <unordered_map>

extern std::vector<DeviceData*> devices;
extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;
extern ConfigNotification ConfigNotif;
extern CircularBuffer<Device, 50> *deviceList;
extern CircularBuffer<Notification, 10> *notifList;

extern NotificationManager notificationManager;

// Cache pour éviter les recherches répétées
static std::unordered_map<std::string, DeviceData*> deviceCache;
static bool cacheInitialized = false;

// Structure pour éviter la duplication
struct ProcessedData {
    String deviceId;
    String clusterId;
    String attributeStr;
    String value;
    String valueType;
    bool isNumeric;
};

// Variables optimisées (éviter les allocations répétées)
static String oldPriceChange;
static String oldPEJP;
static String oldColor;
static String oldRed;
static bool oldProdSupConso = false;

// Initialiser le cache des devices une seule fois
void initializeDeviceCache() {
    if (cacheInitialized) return;
    
    deviceCache.clear();
    for (size_t i = 0; i < devices.size(); i++) {
        DeviceData* device = devices[i];
        deviceCache[device->getDeviceID().c_str()] = device;
    }
    cacheInitialized = true;
}

// Fonction helper pour trouver rapidement un device
DeviceData* findDevice(const String& deviceId) {
    if (!cacheInitialized) {
        initializeDeviceCache();
    }
    
    auto it = deviceCache.find(deviceId.c_str());
    return (it != deviceCache.end()) ? it->second : nullptr;
}

// Fonction centralisée pour publier les données
void publishData(const ProcessedData& data) {
    // MQTT
    if (ConfigSettings.enableMqtt) {
        mqttPublish(data.deviceId, data.clusterId, data.attributeStr, 
                   data.valueType, data.value);
    }
    
    // WebPush
    if (ConfigSettings.enableWebPush) {
        if (data.isNumeric) {
            String numValue = String(strtol(data.value.c_str(), NULL, 16));
            WebPush(data.deviceId, data.clusterId, data.attributeStr, numValue.c_str());
        } else {
            WebPush(data.deviceId, data.clusterId, data.attributeStr, String(data.value));
        }
    }
    
    // Device list update
    if (!deviceList->isFull()) {
        int shortaddr = GetShortAddr(data.deviceId + ".ini");
        if (data.isNumeric) {
            deviceList->push(Device{shortaddr, 65382, data.attributeStr.toInt(), 
                            String(strtol(data.value.c_str(), NULL, 16))});
        } else {
            deviceList->push(Device{shortaddr, 65382, data.attributeStr.toInt(), String(data.value)});
        }
    }
}

// Fonction centralisée pour mettre à jour les devices
void updateDeviceValue(const String& deviceId, int attribute, const String& value) {
    DeviceData* device = findDevice(deviceId);
    if (device) {
        device->setValue(std::string("FF66"), 
                        std::string(String(attribute).c_str()), 
                        std::string(value.c_str()));
    }
}

// Fonction pour gérer les notifications
void handleNotification(const String& title, const String& text, int priority = 0) {
    if (!notifList->isFull()) {
        notifList->push(Notification{title, text, FormattedDate, priority, 0});
    } else {
        notifList->shift();
        notifList->push(Notification{title, text, FormattedDate, priority, 0});
    }
    notificationManager.addNotification(title, text, priority);
}

// Fonction pour gérer le délestage (éviter duplication)
void handleDelestage() {
    config_write("delestage.json", "state", "1");
    config_write("delestage.json", "dateOn", FormattedDate);
    
    String delestage = config_read("configGeneral.json", "delestage");
    if (delestage.length() > 0 && delestage == "null")  return;
    
    char* pch = strtok((char*)delestage.c_str(), ",");
    while (pch != NULL) {
        DeviceData* device = findDevice(String(pch));
        if (device) {
            String oldState = device->getValue(std::string("0006"), std::string("0"));
            if (oldState != "") {
                config_write("delestage.json", device->getDeviceID(), oldState);
            }
            SendOnOffAction(device->getInfo().shortAddr.toInt(),
                          device->getInfo().endpoint.toInt(), "0");
        }
        pch = strtok(NULL, ",");
    }
}

// Gestionnaires spécialisés pour chaque type d'attribut
void handleAttribute5(const String& inifile, uint8_t* datas, int len) {
    String tmp = "";
    char value[3]; // Taille optimisée
    
    for(int i = 0; i < len; i++) {
        sprintf(value, "%02X", datas[i]);
        tmp += value;
    }
    
    if (!ini_exist(inifile)) return;
    
    String deviceId = inifile.substring(0, 16);
    ProcessedData data = {deviceId, "65382", "5", tmp, "numeric", true};
    
    publishData(data);
    updateDeviceValue(deviceId, 5, tmp);
    
    // Gestion spécifique de la surconsommation
    if (ConfigNotif.SubscribedPower) {
        handleNotification("🚨⚡Surconsommation", 
                          " Dépassement de la puissance souscrite", 2);
        handleDelestage();
    }
}

void handleAttribute514(const String& inifile, uint8_t* datas, int len) {
    String tmp = "";
    
    for(int i = 0; i < (len - 1); i++) {
        if(datas[i + 1] > 0) {
            tmp += (char)datas[i + 1];
        }
    }
    
    if (!ini_exist(inifile)) return;
    
    String deviceId = inifile.substring(0, 16);
    ProcessedData data = {deviceId, "65382", "514", tmp, "string", false};
    
    publishData(data);
    updateDeviceValue(deviceId, 514, data.value);
}

void handleAttribute519(const String& inifile, uint8_t* datas, int len) {
    char value[5];
    sprintf(value, "%02X%02X", datas[1], datas[0]);
    String tmp = String(value);
    
    if (!ini_exist(inifile)) return;
    
    String deviceId = inifile.substring(0, 16);
    ProcessedData data = {deviceId, "65382", "519", tmp, "numeric", true};
    
    publishData(data);
    
    DeviceData* device = findDevice(deviceId);
    if (device) {
        device->setValue(std::string("FF66"), std::string("519"), std::string(tmp.c_str()));
        addMeasurement(device->powerHistory, 519, strtol(tmp.c_str(), NULL, 16));
        
        // Gestion production supérieure consommation
        if (ConfigNotif.ProdSupConso && 
            (strcmp(ConfigGeneral.Production, deviceId.c_str()) == 0)) {
            
            DeviceData* zlinkyDevice = findDevice(ConfigGeneral.ZLinky);
            if (zlinkyDevice) {
                int conso = strtol(zlinkyDevice->getValue(std::string("0B04"), 
                                  std::string("1295")).c_str(), NULL, 16);
                
                if (conso > 0) {
                    int production = strtol(tmp.c_str(), NULL, 16);
                    if (production > conso) {
                        if (!oldProdSupConso)
                        {
                            oldProdSupConso = true;
                            String text = "La puissance apparente injecté :" + String(production) +
                                        "VA est supérieure à la consommation : " + conso + " VA";
                            handleNotification("☀️➡️⚡Production > Consommation", text, 0);
                        }
                    } else {
                        oldProdSupConso = false;
                    }
                }
            }
        }
    }
}

void handleAttribute535(const String& inifile, uint8_t* datas, int len) {
    int size = datas[0];
    char STGE[9];
    
    for(int i = 0; i < size; i++) {
        STGE[i] = datas[i + 1];
    }
    STGE[size] = '\0';
    
    if (!ini_exist(inifile)) return;
    
    String deviceId = inifile.substring(0, 16);
    ProcessedData data = {deviceId, "65382", "535", String(STGE), "string", false};
    
    publishData(data);
    updateDeviceValue(deviceId, 535, String(STGE));
    
    auto status = parseStatusRegister(String(STGE));
    
    // Gestion des notifications tempo (code simplifié pour l'exemple)
    if (status.tempo_jour && ConfigNotif.RedColor && 
        (strcmp(ConfigGeneral.ZLinky, deviceId.c_str()) == 0)) {
        
        if ((oldRed != status.tempo_jour) && (oldRed != "") && 
            status.tempo_jour == "3") {
            
            handleNotification("⚠️🔴Journée Rouge !", 
                              "Il faut consommer le moins possible", 3);
        }
        oldRed = status.tempo_jour;
    }
    
    // Gestion délestage si dépassement
    if (status.depassement_ref_pow) {
        if (ConfigNotif.SubscribedPower) {
            handleNotification("🚨⚡Surconsommation", 
                              " Dépassement de la puissance souscrite", 2);
        }
        handleDelestage();
    }
}

void handleAttribute768(const String& inifile, uint8_t* datas, int len) {
    char value[3];
    String tmp = "";
    
    for(int i = 0; i < len; i++) {
        sprintf(value, "%02X", datas[i]);
        tmp += value;
    }
    
    ConfigGeneral.LinkyMode = tmp.toInt();
    
    String deviceId = inifile.substring(0, 16);
    ProcessedData data = {deviceId, "65382", "768", tmp, "numeric", true};
    
    publishData(data);
    
    DeviceData* device = findDevice(deviceId);
    if (device) {
        device->setValue(std::string("FF66"), std::string("768"), std::string(tmp.c_str()));
        device->setInfoLinkyMode(String(strtol(tmp.c_str(), NULL, 16)));
    }
}

void handleDefaultAttribute(const String& inifile, int attribute, uint8_t datatype, 
                           uint8_t* datas, int len) {
    String tmp = "";
    
    if (datatype == 66) {
        int size = datas[0];
        for(int i = 0; i < size; i++) {
            if(datas[i + 1] > 0) {
                tmp += (char)datas[i + 1];
            }
        }
    } else if ((datatype == 0x21) || (datatype == 0x29)) {
        char value[5];
        sprintf(value, "%02X%02X", datas[1], datas[0]);
        tmp = String(value);
    } else {
        char value[3];
        for(int i = 0; i < len; i++) {
            sprintf(value, "%02X", datas[i]);
            tmp += value;
        }
    }
    
    if (!ini_exist(inifile)) return;
    
    String deviceId = inifile.substring(0, 16);
    String valueType = (datatype == 66) ? "string" : "numeric";
    ProcessedData data = {deviceId, "65382", String(attribute), tmp, valueType, datatype != 66};
    
    publishData(data);
    updateDeviceValue(deviceId, attribute, tmp);
    
    // Gestions spécifiques selon l'attribut
    if (attribute == 1 && ConfigNotif.ColorTomorrow && 
        (strcmp(ConfigGeneral.ZLinky, deviceId.c_str()) == 0)) {
        
        if ((oldColor != tmp.c_str()) && (oldColor != "")) {
            handleNotification("🕓💵Couleur du lendemain", 
                              "Couleur : " + tmp, 1);
        }
        oldColor = tmp.c_str();
    }
    //Notification PEJP
    if (attribute == 4)
    {
        if (ConfigNotif.PEJP && (strcmp(ConfigGeneral.ZLinky,inifile.substring(0,16).c_str()) == 0 ))
        {
            if ((oldPEJP != tmp.c_str()) && (oldPEJP!=""))
            { 

            String text ="Préavis EJP : "+tmp+" min";
            handleNotification("⚡Préavis début EJP", 
                              text, 1);
               
            }
            oldPEJP = tmp.c_str();
        }
    }
    //Notification PriceChange
    if (attribute == 16)
    {
        if (ConfigNotif.PriceChange && (strcmp(ConfigGeneral.ZLinky,inifile.substring(0,16).c_str()) == 0 ))
        {
            if ((oldPriceChange != tmp.c_str()) && (oldPriceChange!=""))
            {
                String text ="--> tarif : "+tmp;
                handleNotification("🕓💵 Changement de tarif", 
                                text,0);
            }
            oldPriceChange = tmp.c_str();
        }
    }
    // Autres notifications similaires...
}

// Fonction principale optimisée
void lixeeClusterManage(String inifile, int attribute, uint8_t datatype, 
                       int len, char* datas) {
    // Cast vers uint8_t* pour plus de clarté
    uint8_t* data = reinterpret_cast<uint8_t*>(datas);
    
    // Dispatch vers les gestionnaires spécialisés
    switch (attribute) {
        case 5:
            handleAttribute5(inifile, data, len);
            break;
        case 514:
            handleAttribute514(inifile, data, len);
            break;
        case 519:
            handleAttribute519(inifile, data, len);
            break;
        case 535:
            handleAttribute535(inifile, data, len);
            break;
        case 768:
            handleAttribute768(inifile, data, len);
            break;
        default:
            handleDefaultAttribute(inifile, attribute, datatype, data, len);
            break;
    }
}