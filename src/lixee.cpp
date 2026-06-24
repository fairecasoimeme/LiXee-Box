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

extern DeviceList devices;
extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;
extern ConfigNotification ConfigNotif;
extern CircularBuffer<Device, 50> *deviceList;
extern CircularBuffer<Notification, 10> *notifList;

extern NotificationManager notificationManager;

// Cache pour éviter les recherches répétées (PSRAM)
static PsUnorderedMap<DeviceData*> deviceCache;
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
        device->setValue("FF66",
                        String(attribute).c_str(),
                        value.c_str());
    }
}

// Fonction pour gérer les notifications
void handleNotification(const String& title, const String& text, int priority = 0,
                        const char* alertType = nullptr, float value = 0, float threshold = 0) {
    if (!notifList->isFull()) {
        notifList->push(Notification{title, text, FormattedDate, priority, 0});
    } else {
        notifList->shift();
        notifList->push(Notification{title, text, FormattedDate, priority, 0});
    }
    notificationManager.addNotification(title, text, priority, alertType, value, threshold);
}

// Fonction pour gérer le délestage (éviter duplication)
void handleDelestage() {
    config_write("delestage.json", "state", "1");
    config_write("delestage.json", "dateOn", FormattedDate);
    
    String delestage = config_read("configGeneral.json", "delestage");
    if (delestage.length() > 0 && delestage == "null")  return;
    
    char* buf = strdup(delestage.c_str());
    if (!buf) return;
    char* pch = strtok(buf, ",");
    while (pch != NULL) {
        DeviceData* device = findDevice(String(pch));
        if (device) {
            String oldState = device->getValue("0006", "0");
            if (oldState != "") {
                config_write("delestage.json", device->getDeviceID(), oldState);
            }
            SendOnOffAction(device->getInfo().shortAddr.toInt(),
                          device->getInfo().endpoint.toInt(), "0");
        }
        pch = strtok(NULL, ",");
    }
    free(buf);
}

// Gestionnaires spécialisés pour chaque type d'attribut
void handleAttribute5(const String& inifile, uint8_t* datas, int len) {
    String tmp = "";
    char value[3]; // Taille optimisée
    
    for(int i = 0; i < len; i++) {
        snprintf(value, sizeof(value), "%02X", datas[i]);
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
                          " Dépassement de la puissance souscrite", 2, "subscribed_power");
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
    snprintf(value, sizeof(value), "%02X%02X", datas[1], datas[0]);
    String tmp = String(value);
    
    if (!ini_exist(inifile)) return;
    
    String deviceId = inifile.substring(0, 16);
    ProcessedData data = {deviceId, "65382", "519", tmp, "numeric", true};
    
    publishData(data);
    
    DeviceData* device = findDevice(deviceId);
    if (device) {
        device->setValue("FF66", "519", tmp.c_str());
        addMeasurement(device->powerHistory, 519, strtol(tmp.c_str(), NULL, 16));
        
        // Gestion production supérieure consommation
        if (ConfigNotif.ProdSupConso && 
            (strcmp(ConfigGeneral.Production, deviceId.c_str()) == 0)) {
            
            DeviceData* zlinkyDevice = findDevice(ConfigGeneral.ZLinky);
            if (zlinkyDevice) {
                int conso = strtol(zlinkyDevice->getValue("0B04",
                                  "1295").c_str(), NULL, 16);
                
                if (conso > 0) {
                    int production = strtol(tmp.c_str(), NULL, 16);
                    if (production > conso) {
                        if (!oldProdSupConso)
                        {
                            oldProdSupConso = true;
                            String text = "La puissance apparente injecté :" + String(production) +
                                        "VA est supérieure à la consommation : " + conso + " VA";
                            handleNotification("☀️➡️⚡Production > Consommation", text, 0,
                                "prod_sup_conso", (float)production, (float)conso);
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
    if (len < 1) return;
    int size = datas[0];
    if (size > 8 || size + 1 > len) {
        log_e("handleAttribute535: invalid size %d (len=%d)", size, len);
        return;
    }
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
                              "Il faut consommer le moins possible", 3, "red_day");
        }
        oldRed = status.tempo_jour;
    }
    
    // Gestion délestage si dépassement
    if (status.depassement_ref_pow) {
        if (ConfigNotif.SubscribedPower) {
            handleNotification("🚨⚡Surconsommation",
                              " Dépassement de la puissance souscrite", 2, "subscribed_power");
        }
        handleDelestage();
    }

    // Publier les tarifs/couleurs MQTT (STGE contient les couleurs en mode standard)
    publishLinkyTariffInfo(deviceId);
}

void handleAttribute768(const String& inifile, uint8_t* datas, int len) {
    char value[3];
    String tmp = "";
    
    for(int i = 0; i < len; i++) {
        snprintf(value, sizeof(value), "%02X", datas[i]);
        tmp += value;
    }
    String deviceId = inifile.substring(0, 16);

    if (ConfigGeneral.ZLinky == deviceId.c_str())
    {
        ConfigGeneral.LinkyMode = tmp.toInt();
    }
    
    ProcessedData data = {deviceId, "65382", "768", tmp, "numeric", true};
    
    publishData(data);
    
    DeviceData* device = findDevice(deviceId);
    if (device) {
        device->setValue("FF66", "768", tmp.c_str());
        device->setInfoLinkyMode(String(strtol(tmp.c_str(), NULL, 16)));
    }
}

void handleDefaultAttribute(const String& inifile, int attribute, uint8_t datatype, 
                           uint8_t* datas, int len) {
    String tmp = "";
    
    if (datatype == 66) {
        if (len < 1) return;
        int size = datas[0];
        if (size + 1 > len) size = len - 1;
        for(int i = 0; i < size; i++) {
            if(datas[i + 1] > 0) {
                tmp += (char)datas[i + 1];
            }
        }
    } else if ((datatype == 0x21) || (datatype == 0x29)) {
        if (len < 2) return;
        char value[5];
        snprintf(value, sizeof(value), "%02X%02X", datas[1], datas[0]);
        tmp = String(value);
    } else {
        char value[3];
        for(int i = 0; i < len; i++) {
            snprintf(value, sizeof(value), "%02X", datas[i]);
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
                              "Couleur : " + tmp, 1, "color_tomorrow");
        }
        oldColor = tmp.c_str();
    }

    // Attribut 1 (DEMAIN) : mettre à jour la couleur demain en mode historique
    if (attribute == 1) {
        publishLinkyTariffInfo(deviceId);
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
                              text, 1, "ejp");

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
                                text, 0, "price_change");
            }
            oldPriceChange = tmp.c_str();
        }

        // Attribut 16 (PTEC) : publier tarif/couleurs MQTT
        publishLinkyTariffInfo(deviceId);
    }
    // Autres notifications similaires...
}

// ============================================================================
// PUBLICATION MQTT DES TARIFS ET COULEURS LINKY
// ============================================================================
// Publie 3 entités MQTT dédiées : tarif_actuel, couleur_jour, couleur_demain
// Source principale : cluster FF66 attribut 16 (PTEC, compatible config report)
// Sources complémentaires : STGE (attr 535) pour couleurs standard, DEMAIN (attr 1)

static String lastPublishedTarif;
static String lastPublishedCouleurJour;
static String lastPublishedCouleurDemain;

// Famille de contrat tarifaire déduite du PTEC courant (historique ou standard) :
// 0=Base, 1=Heures Creuses/Pleines, 2=EJP, 3=Tempo. Défaut HC/HP si indéterminé.
int getTariffFamily() {
  DeviceData* lk = findDevice(String(ConfigGeneral.ZLinky));
  if (!lk) return 1;
  String p = lk->getValue("FF66", "16");
  p.trim();
  p.toUpperCase();
  if (p.length() == 0) return 1;
  if (p.indexOf("JB") >= 0 || p.indexOf("JW") >= 0 || p.indexOf("JR") >= 0 ||
      p.indexOf("BBR") >= 0 || p.indexOf("BLEU") >= 0 || p.indexOf("BLANC") >= 0 ||
      p.indexOf("ROUGE") >= 0 || p.indexOf("TEMPO") >= 0) return 3;  // Tempo
  if (p.startsWith("EJP") || p == "HN.." || p == "PM.." ||
      p.indexOf("POINTE") >= 0 || p.indexOf("NORMALE") >= 0) return 2;  // EJP
  if (p.startsWith("TH") || p.startsWith("BASE") || p.startsWith("TOUTE")) return 0;  // Base
  return 1;  // HC/HP par défaut
}

// IDs des périodes disponibles selon la famille de contrat (terminé par -1).
const int* getTariffAvailablePeriods() {
  static const int base[]  = {256, -1};
  static const int hchp[]  = {256, 258, -1};
  static const int tempo[] = {256, 258, 260, 262, 264, 266, -1};
  switch (getTariffFamily()) {
    case 0: return base;
    case 3: return tempo;
    default: return hchp;  // HC/HP et EJP : 2 périodes
  }
}

// Libellé d'une période tarifaire (ID d'attribut index 256/258/...), selon le contrat réel.
// Mêmes libellés que publishLinkyTariffInfo.
String tariffLabelForAttr(int attrId) {
  switch (getTariffFamily()) {
    case 3:  // Tempo
      switch (attrId) {
        case 256: return "HC Bleu";
        case 258: return "HP Bleu";
        case 260: return "HC Blanc";
        case 262: return "HP Blanc";
        case 264: return "HC Rouge";
        case 266: return "HP Rouge";
      }
      break;
    case 2:  // EJP
      if (attrId == 256) return "Heures Normales";
      if (attrId == 258) return "Pointe Mobile";
      break;
    case 0:  // Base
      if (attrId == 256) return "Base";
      break;
    default:  // HC/HP
      if (attrId == 256) return "Heures Creuses";
      if (attrId == 258) return "Heures Pleines";
      break;
  }
  return "P" + String(attrId);
}

void publishLinkyTariffInfo(const String& deviceId) {
    // Vérifier que c'est le Linky configuré
    if (strcmp(ConfigGeneral.ZLinky, deviceId.c_str()) != 0) return;

    // Vérifier que MQTT HA est activé et connecté
    if (!ConfigSettings.enableMqtt || !ConfigGeneral.HAMQTT || !mqttClient.connected()) return;

    // Trouver le device
    DeviceData* device = findDevice(deviceId);
    if (!device) return;

    String tarif = "";
    String couleurJour = "";
    String couleurDemain = "";

    // Lire l'attribut 16 (PTEC) — source principale
    String ptec = device->getValue("FF66", "16");
    ptec.trim();

    if (ptec.length() == 0) return; // Pas encore de donnée

    if ((ConfigGeneral.LinkyMode == 0) || (ConfigGeneral.LinkyMode == 2)) {
        // =====================================================
        // Mode Historique : décoder le code PTEC
        // =====================================================
        String ptecUpper = ptec;
        ptecUpper.toUpperCase();

        if (ptecUpper.startsWith("TH")) {
            tarif = "Base";
        }
        else if (ptecUpper == "HC.." || (ptecUpper.startsWith("HC") && ptecUpper.indexOf("JB") < 0 && ptecUpper.indexOf("JW") < 0 && ptecUpper.indexOf("JR") < 0)) {
            tarif = "Heures Creuses";
        }
        else if (ptecUpper == "HP.." || (ptecUpper.startsWith("HP") && ptecUpper.indexOf("JB") < 0 && ptecUpper.indexOf("JW") < 0 && ptecUpper.indexOf("JR") < 0)) {
            tarif = "Heures Pleines";
        }
        else if (ptecUpper == "HN.." || ptecUpper.startsWith("EJPHN")) {
            tarif = "Heures Normales";
        }
        else if (ptecUpper == "PM.." || ptecUpper.startsWith("EJPHPM")) {
            tarif = "Pointe Mobile";
        }
        // Tempo
        else if (ptecUpper.startsWith("HCJB")) {
            tarif = "HC Bleu";
            couleurJour = "BLEU";
        }
        else if (ptecUpper.startsWith("HPJB")) {
            tarif = "HP Bleu";
            couleurJour = "BLEU";
        }
        else if (ptecUpper.startsWith("HCJW")) {
            tarif = "HC Blanc";
            couleurJour = "BLANC";
        }
        else if (ptecUpper.startsWith("HPJW")) {
            tarif = "HP Blanc";
            couleurJour = "BLANC";
        }
        else if (ptecUpper.startsWith("HCJR")) {
            tarif = "HC Rouge";
            couleurJour = "ROUGE";
        }
        else if (ptecUpper.startsWith("HPJR")) {
            tarif = "HP Rouge";
            couleurJour = "ROUGE";
        }
        else {
            tarif = ptec; // Fallback : valeur brute
        }

        // Couleur demain : lire attribut 1 (DEMAIN) — mode historique Tempo
        if (couleurJour.length() > 0) {
            String demain = device->getValue("FF66", "1");
            demain.trim();
            demain.toUpperCase();
            if (demain.startsWith("BLEU")) {
                couleurDemain = "BLEU";
            } else if (demain.startsWith("BLAN")) {
                couleurDemain = "BLANC";
            } else if (demain.startsWith("ROUG")) {
                couleurDemain = "ROUGE";
            }
        }
    }
    else {
        // =====================================================
        // Mode Standard : attribut 16 = libellé texte
        // =====================================================
        tarif = ptec;

        // Couleurs via STGE (attribut 535)
        String stge = device->getValue("FF66", "535");
        if (stge.length() >= 4) {
            auto status = parseStatusRegister(stge);

            if (status.tempo_jour != "UNDEF") {
                couleurJour = status.tempo_jour;
            }
            if (status.tempo_demain != "UNDEF") {
                couleurDemain = status.tempo_demain;
            }
        }
    }

    // Anti-spam : ne publier que si une valeur a changé
    bool changed = false;
    if (tarif != lastPublishedTarif) changed = true;
    if (couleurJour != lastPublishedCouleurJour) changed = true;
    if (couleurDemain != lastPublishedCouleurDemain) changed = true;

    if (!changed) return;

    // Publier les 3 entités MQTT
    String header = String(ConfigGeneral.headerMQTT);

    if (tarif != lastPublishedTarif) {
        String topic = header + deviceId + "_tarif_actuel/state";
        String payload = "{\"tarif_actuel\":\"" + tarif + "\"}";
        mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
        lastPublishedTarif = tarif;
        log_d("MQTT Tarif: %s", tarif.c_str());
    }

    if (couleurJour != lastPublishedCouleurJour) {
        String topic = header + deviceId + "_couleur_jour/state";
        String payload = "{\"couleur_jour\":\"" + couleurJour + "\"}";
        mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
        lastPublishedCouleurJour = couleurJour;
        log_d("MQTT Couleur jour: %s", couleurJour.c_str());
    }

    if (couleurDemain != lastPublishedCouleurDemain) {
        String topic = header + deviceId + "_couleur_demain/state";
        String payload = "{\"couleur_demain\":\"" + couleurDemain + "\"}";
        mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
        lastPublishedCouleurDemain = couleurDemain;
        log_d("MQTT Couleur demain: %s", couleurDemain.c_str());
    }
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