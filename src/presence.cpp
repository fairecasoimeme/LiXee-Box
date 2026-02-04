#include "presence.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "log.h"
#include "device.h"

extern std::vector<DeviceData*> devices;
extern String FormattedDate;
extern String Hour;
extern String Day;
extern String Month;
extern String Year;

// Fichier unique de présence
static const char* PRESENCE_FILE = "/hst/presence.json";

// Cache du statut actuel de présence par device
struct PresenceStatus {
    String deviceId;
    bool occupied;
    unsigned long lastUpdate;
};

static std::vector<PresenceStatus> presenceCache;

// ============================================================================
// INITIALISATION
// ============================================================================

void presenceInit() {
    log_i("Presence system initialized");
}

// ============================================================================
// DÉTECTION DES CAPTEURS DE PRÉSENCE
// ============================================================================

bool isPresenceSensor(const String& model) {
    // Aqara FP1 (mmWave)
    if (model == "lumi.motion.ac01") return true;
    
    // Sonoff SNZB-06P (mmWave)
    if (model == "SNZB-06P") return true;
    
    // Autres capteurs PIR/présence courants
    if (model.indexOf("motion") >= 0) return true;
    if (model.indexOf("presence") >= 0) return true;
    if (model.indexOf("occupancy") >= 0) return true;
    
    // Aqara motion sensors
    if (model.startsWith("lumi.sensor_motion")) return true;
    
    // Philips Hue motion
    if (model.indexOf("SML") >= 0) return true;
    
    // IKEA motion
    if (model.indexOf("TRADFRI motion") >= 0) return true;
    
    return false;
}

// ============================================================================
// GESTION DU FICHIER UNIQUE
// ============================================================================

static bool loadPresenceFile(JsonDocument& doc) {
    if (!LittleFS.exists(PRESENCE_FILE)) {
        // Créer un nouveau document vide
        doc["devices"].to<JsonObject>();
        return false;
    }
    
    File file = LittleFS.open(PRESENCE_FILE, "r");
    if (!file) {
        log_e("Failed to open presence file");
        return false;
    }
    
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        log_e("Failed to parse presence file: %s", error.c_str());
        return false;
    }
    
    return true;
}

static bool savePresenceFile(JsonDocument& doc) {
    File file = LittleFS.open(PRESENCE_FILE, "w");
    if (!file) {
        log_e("Failed to create presence file");
        return false;
    }
    
    serializeJson(doc, file);
    file.close();
    
    return true;
}

// ============================================================================
// ENREGISTREMENT DES ÉVÉNEMENTS
// ============================================================================

void presenceRecordEvent(const String& deviceId, bool occupied) {
    int currentDay = Day.toInt();   // 1-31
    int currentHour = Hour.toInt(); // 0-23
    String dayKey = String(currentDay);
    String devKey = deviceId.substring(0, 16);
    
    log_i("Presence event: device=%s, occupied=%d, day=%d, hour=%d", 
          devKey.c_str(), occupied, currentDay, currentHour);
    
    // Mettre à jour le cache
    bool found = false;
    for (auto& status : presenceCache) {
        if (status.deviceId == devKey) {
            status.occupied = occupied;
            status.lastUpdate = millis();
            found = true;
            break;
        }
    }
    if (!found) {
        presenceCache.push_back({devKey, occupied, millis()});
    }
    
    // Charger le fichier
    DynamicJsonDocument doc(8192);
    loadPresenceFile(doc);
    
    // S'assurer que l'objet "devices" existe
    if (!doc.containsKey("devices")) {
        doc["devices"].to<JsonObject>();
    }
    
    JsonObject devicesObj = doc["devices"].as<JsonObject>();
    
    // S'assurer que le device existe
    if (!devicesObj.containsKey(devKey)) {
        devicesObj.createNestedObject(devKey);
    }
    
    JsonObject deviceObj = devicesObj[devKey].as<JsonObject>();
    
    // S'assurer que l'objet "days" existe pour ce device
    if (!deviceObj.containsKey("days")) {
        deviceObj["days"].to<JsonObject>();
    }
    
    JsonObject days = deviceObj["days"].as<JsonObject>();
    
    // S'assurer que le jour existe avec un tableau de 24 heures
    if (!days.containsKey(dayKey)) {
        JsonArray dayArray = days.createNestedArray(dayKey);
        for (int i = 0; i < 24; i++) {
            dayArray.add(0);
        }
    }
    
    // Mettre à jour l'heure si présence détectée
    if (occupied) {
        JsonArray dayArray = days[dayKey].as<JsonArray>();
        if (currentHour >= 0 && currentHour < 24) {
            dayArray[currentHour] = 1;
        }
    }
    
    // Mettre à jour le dernier état
    deviceObj["lastState"] = occupied ? 1 : 0;
    deviceObj["lastUpdate"] = FormattedDate;
    
    // Sauvegarder
    savePresenceFile(doc);
    
    log_d("Presence event recorded for %s on day %d", devKey.c_str(), currentDay);
}

// ============================================================================
// RÉCUPÉRATION DES DONNÉES
// ============================================================================

String getPresenceSummary(const String& deviceId, const String& dayNum) {
    DynamicJsonDocument doc(8192);
    
    // Initialiser avec des zéros
    int hours[24] = {0};
    
    String devKey = deviceId.substring(0, 16);
    
    if (loadPresenceFile(doc)) {
        if (doc.containsKey("devices") && 
            doc["devices"].containsKey(devKey) &&
            doc["devices"][devKey].containsKey("days") &&
            doc["devices"][devKey]["days"].containsKey(dayNum)) {
            
            JsonArray dayArray = doc["devices"][devKey]["days"][dayNum].as<JsonArray>();
            for (int i = 0; i < 24 && i < (int)dayArray.size(); i++) {
                hours[i] = dayArray[i].as<int>();
            }
        }
    }
    
    // Construire le JSON manuellement
    String output = "[";
    for (int i = 0; i < 24; i++) {
        if (i > 0) output += ",";
        output += String(hours[i]);
    }
    output += "]";
    
    return output;
}

String getPresenceSummaryAll(const String& dayNum) {
    DynamicJsonDocument doc(8192);
    
    // Initialiser avec des zéros
    int combined[24] = {0};
    
    if (loadPresenceFile(doc) && doc.containsKey("devices")) {
        JsonObject devicesObj = doc["devices"].as<JsonObject>();
        
        // Parcourir tous les devices
        for (JsonPair kv : devicesObj) {
            JsonObject deviceObj = kv.value().as<JsonObject>();
            
            if (deviceObj.containsKey("days") && deviceObj["days"].containsKey(dayNum)) {
                JsonArray dayArray = deviceObj["days"][dayNum].as<JsonArray>();
                for (int i = 0; i < 24 && i < (int)dayArray.size(); i++) {
                    if (dayArray[i].as<int>() == 1) {
                        combined[i] = 1;
                    }
                }
            }
        }
    }
    
    // Construire le JSON manuellement (plus léger)
    String output = "[";
    for (int i = 0; i < 24; i++) {
        if (i > 0) output += ",";
        output += String(combined[i]);
    }
    output += "]";
    
    return output;
}

// ============================================================================
// NOUVELLE FONCTION : 24H GLISSANTES
// ============================================================================
// Fusionne les données de hier et aujourd'hui pour couvrir les 24h glissantes
// Le graphe d'énergie affiche de (heure actuelle - 23h) à (heure actuelle)
// Exemple : à 9h30 le 20, on affiche de 10h le 19 à 9h le 20

String getPresenceSummary24hSliding() {
    DynamicJsonDocument doc(8192);
    
    int currentDay = Day.toInt();    // 1-31
    int currentHour = Hour.toInt();  // 0-23
    
    // Calcul du jour précédent (gestion du passage de mois)
    // Note: simplifié - on prend juste le jour -1, si 1 alors 31
    int previousDay = (currentDay == 1) ? 31 : currentDay - 1;
    
    String todayKey = String(currentDay);
    String yesterdayKey = String(previousDay);
    
    // Résultat : tableau de 24 heures aligné sur le graphe d'énergie
    // Index 0 = heure la plus ancienne (currentHour + 1 d'hier)
    // Index 23 = heure actuelle (currentHour d'aujourd'hui)
    int sliding[24] = {0};
    
    if (!loadPresenceFile(doc) || !doc.containsKey("devices")) {
        // Retourner un tableau de zéros
        String output = "[";
        for (int i = 0; i < 24; i++) {
            if (i > 0) output += ",";
            output += "0";
        }
        output += "]";
        return output;
    }
    
    JsonObject devicesObj = doc["devices"].as<JsonObject>();
    
    // Parcourir tous les devices pour fusionner les présences
    for (JsonPair kv : devicesObj) {
        JsonObject deviceObj = kv.value().as<JsonObject>();
        
        if (!deviceObj.containsKey("days")) continue;
        
        JsonObject days = deviceObj["days"].as<JsonObject>();
        
        // Données d'hier (de currentHour+1 à 23h)
        if (days.containsKey(yesterdayKey)) {
            JsonArray yesterdayArray = days[yesterdayKey].as<JsonArray>();
            
            // Heures d'hier : de (currentHour + 1) à 23
            // Ces heures vont aux positions 0 à (23 - currentHour - 1) du résultat
            for (int h = currentHour + 1; h < 24; h++) {
                int slidingIndex = h - currentHour - 1;  // Position dans le tableau résultat
                if (slidingIndex >= 0 && slidingIndex < 24 && h < (int)yesterdayArray.size()) {
                    if (yesterdayArray[h].as<int>() == 1) {
                        sliding[slidingIndex] = 1;
                    }
                }
            }
        }
        
        // Données d'aujourd'hui (de 0h à currentHour)
        if (days.containsKey(todayKey)) {
            JsonArray todayArray = days[todayKey].as<JsonArray>();
            
            // Heures d'aujourd'hui : de 0 à currentHour
            // Ces heures vont aux positions (24 - currentHour - 1) à 23 du résultat
            for (int h = 0; h <= currentHour; h++) {
                int slidingIndex = (24 - currentHour - 1) + h;  // Position dans le tableau résultat
                if (slidingIndex >= 0 && slidingIndex < 24 && h < (int)todayArray.size()) {
                    if (todayArray[h].as<int>() == 1) {
                        sliding[slidingIndex] = 1;
                    }
                }
            }
        }
    }
    
    // Construire le JSON
    String output = "[";
    for (int i = 0; i < 24; i++) {
        if (i > 0) output += ",";
        output += String(sliding[i]);
    }
    output += "]";
    
    log_d("Presence 24h sliding: day=%d, hour=%d, result=%s", 
          currentDay, currentHour, output.c_str());
    
    return output;
}

// Version avec device spécifique
String getPresenceSummary24hSliding(const String& deviceId) {
    DynamicJsonDocument doc(8192);
    
    int currentDay = Day.toInt();
    int currentHour = Hour.toInt();
    int previousDay = (currentDay == 1) ? 31 : currentDay - 1;
    
    String todayKey = String(currentDay);
    String yesterdayKey = String(previousDay);
    String devKey = deviceId.substring(0, 16);
    
    int sliding[24] = {0};
    
    if (!loadPresenceFile(doc) || !doc.containsKey("devices")) {
        String output = "[";
        for (int i = 0; i < 24; i++) {
            if (i > 0) output += ",";
            output += "0";
        }
        output += "]";
        return output;
    }
    
    JsonObject devicesObj = doc["devices"].as<JsonObject>();
    
    if (!devicesObj.containsKey(devKey)) {
        String output = "[";
        for (int i = 0; i < 24; i++) {
            if (i > 0) output += ",";
            output += "0";
        }
        output += "]";
        return output;
    }
    
    JsonObject deviceObj = devicesObj[devKey].as<JsonObject>();
    
    if (!deviceObj.containsKey("days")) {
        String output = "[";
        for (int i = 0; i < 24; i++) {
            if (i > 0) output += ",";
            output += "0";
        }
        output += "]";
        return output;
    }
    
    JsonObject days = deviceObj["days"].as<JsonObject>();
    
    // Données d'hier
    if (days.containsKey(yesterdayKey)) {
        JsonArray yesterdayArray = days[yesterdayKey].as<JsonArray>();
        for (int h = currentHour + 1; h < 24; h++) {
            int slidingIndex = h - currentHour - 1;
            if (slidingIndex >= 0 && slidingIndex < 24 && h < (int)yesterdayArray.size()) {
                if (yesterdayArray[h].as<int>() == 1) {
                    sliding[slidingIndex] = 1;
                }
            }
        }
    }
    
    // Données d'aujourd'hui
    if (days.containsKey(todayKey)) {
        JsonArray todayArray = days[todayKey].as<JsonArray>();
        for (int h = 0; h <= currentHour; h++) {
            int slidingIndex = (24 - currentHour - 1) + h;
            if (slidingIndex >= 0 && slidingIndex < 24 && h < (int)todayArray.size()) {
                if (todayArray[h].as<int>() == 1) {
                    sliding[slidingIndex] = 1;
                }
            }
        }
    }
    
    String output = "[";
    for (int i = 0; i < 24; i++) {
        if (i > 0) output += ",";
        output += String(sliding[i]);
    }
    output += "]";
    
    return output;
}

String getPresenceHistory(const String& deviceId) {
    DynamicJsonDocument doc(8192);
    StaticJsonDocument<2048> result;
    
    String devKey = deviceId.substring(0, 16);
    
    if (loadPresenceFile(doc)) {
        if (doc.containsKey("devices") && doc["devices"].containsKey(devKey)) {
            // Copier les données du device
            result.set(doc["devices"][devKey]);
            
            String output;
            serializeJson(result, output);
            return output;
        }
    }
    
    return "{\"error\":\"Device not found\"}";
}

String getPresenceAll() {
    DynamicJsonDocument doc(8192);
    
    if (loadPresenceFile(doc)) {
        String output;
        serializeJson(doc, output);
        return output;
    }
    
    return "{\"devices\":{}}";
}

bool getCurrentPresenceStatus(const String& deviceId) {
    if (deviceId == "all") {
        // Vérifier tous les capteurs
        for (const auto& status : presenceCache) {
            if (status.occupied) {
                // Vérifier que le statut n'est pas trop ancien (30 minutes max)
                if (millis() - status.lastUpdate < 1800000) {
                    return true;
                }
            }
        }
        return false;
    } else {
        // Vérifier un capteur spécifique
        String devKey = deviceId.substring(0, 16);
        for (const auto& status : presenceCache) {
            if (status.deviceId == devKey) {
                if (millis() - status.lastUpdate < 1800000) {
                    return status.occupied;
                }
            }
        }
        return false;
    }
}

// ============================================================================
// RESET D'UN JOUR (appelé au changement de jour pour effacer les anciennes données)
// ============================================================================

void presenceResetDay(int dayNum) {
    String dayKey = String(dayNum);
    
    DynamicJsonDocument doc(8192);
    if (!loadPresenceFile(doc) || !doc.containsKey("devices")) {
        return;
    }
    
    JsonObject devicesObj = doc["devices"].as<JsonObject>();
    bool modified = false;
    
    // Parcourir tous les devices
    for (JsonPair kv : devicesObj) {
        JsonObject deviceObj = kv.value().as<JsonObject>();
        
        if (deviceObj.containsKey("days")) {
            JsonObject daysObj = deviceObj["days"].as<JsonObject>();
            
            // Supprimer l'ancien jour s'il existe
            if (daysObj.containsKey(dayKey)) {
                daysObj.remove(dayKey);
            }
            
            // Créer un nouveau tableau avec 24 zéros
            JsonArray dayArray = daysObj.createNestedArray(dayKey);
            for (int i = 0; i < 24; i++) {
                dayArray.add(0);
            }
            
            modified = true;
            log_i("Reset presence day %d for device %s", dayNum, kv.key().c_str());
        }
    }
    
    if (modified) {
        savePresenceFile(doc);
    }
}

void presenceResetHour(int dayNum, int hourNum) {
    if (hourNum < 0 || hourNum > 23) return;
    if (dayNum < 1 || dayNum > 31) return;
    
    String dayKey = String(dayNum);
    
    DynamicJsonDocument doc(8192);
    if (!loadPresenceFile(doc)) {
        return;
    }
    
    // Créer la structure "devices" si elle n'existe pas
    if (!doc.containsKey("devices")) {
        doc.createNestedObject("devices");
    }
    
    JsonObject devicesObj = doc["devices"].as<JsonObject>();
    bool modified = false;
    
    // Parcourir tous les devices
    for (JsonPair kv : devicesObj) {
        JsonObject deviceObj = kv.value().as<JsonObject>();
        
        // Créer "days" si n'existe pas
        if (!deviceObj.containsKey("days")) {
            deviceObj.createNestedObject("days");
        }
        
        JsonObject daysObj = deviceObj["days"].as<JsonObject>();
        
        // Si le jour n'existe pas, le créer avec 24 zéros
        if (!daysObj.containsKey(dayKey)) {
            JsonArray dayArray = daysObj.createNestedArray(dayKey);
            for (int i = 0; i < 24; i++) {
                dayArray.add(0);
            }
            modified = true;
            log_i("Created presence day %d for device %s", dayNum, kv.key().c_str());
        } 
        else {
            JsonArray dayArray = daysObj[dayKey].as<JsonArray>();
            
            // Si le tableau n'a pas 24 éléments, le recréer proprement
            if (dayArray.size() != 24) {
                // Sauvegarder les valeurs existantes
                int existingValues[24] = {0};
                int existingSize = dayArray.size();
                for (int i = 0; i < existingSize && i < 24; i++) {
                    existingValues[i] = dayArray[i].as<int>();
                }
                
                // Supprimer et recréer
                daysObj.remove(dayKey);
                JsonArray newDayArray = daysObj.createNestedArray(dayKey);
                for (int i = 0; i < 24; i++) {
                    if (i == hourNum) {
                        newDayArray.add(0);  // RAZ de l'heure demandée
                    } else {
                        newDayArray.add(existingValues[i]);  // Conserver les autres
                    }
                }
                modified = true;
                log_i("Fixed and reset presence hour %d for day %d, device %s", hourNum, dayNum, kv.key().c_str());
            }
            else {
                // Tableau OK, juste remettre l'heure à zéro
                dayArray[hourNum] = 0;
                modified = true;
            }
        }
    }
    
    if (modified) {
        savePresenceFile(doc);
        log_i("Reset presence hour %d for day %d", hourNum, dayNum);
    }
}


// ============================================================================
// FONCTION D'INTÉGRATION AVEC ZIGBEE
// ============================================================================

void handleOccupancyChange(const String& deviceId, const String& model, uint8_t occupancy) {
    if (!isPresenceSensor(model)) {
        return;
    }
    
    bool occupied = (occupancy & 0x01) != 0;
    presenceRecordEvent(deviceId, occupied);
}