// rules.cpp
#include "rules.h"
#include "protocol.h"
#include "config.h"
#include "log.h"
#include "AsyncJson.h"
#define ARDUINOJSON_USE_LONG_LONG 1
#define ARDUINOJSON_SLOT_ID_SIZE 2
#include <ArduinoJson.h>
#include "SPIFFS_ini.h"
#include "zigbee.h"
#include "onoff.h"
#include "notificationManager.h"
#include <TimeLib.h>

extern String Hour;
extern String Minute;

extern NotificationManager notificationManager;
extern CircularBuffer<Notification, 10> *notifList;

// Charge le JSON en PSRAM et construit le vector<Rule>
bool RulesManager::loadFromFile(const char* path) {
    File file = LittleFS.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        Serial.printf("RulesManager: impossible d'ouvrir %s\n", path);
        return false;
    }

    SpiRamJsonDocument doc(MAXHEAP);
    auto err = deserializeJson(doc, file);
    file.close();
    if (err) {
        //Serial.printf("RulesManager JSON parse error: %s\n", err.f_str());
        return false;
    }

    JsonArray arr = doc["rules"].as<JsonArray>();
    rules_.clear();
    rules_.reserve(arr.size());

    for (JsonObject r : arr) {
        Rule rule;

        // Nom
        rule.name = PsString(r["name"] | "", PsramAllocator<char>());

        // ← NOUVEAU: Plages horaires
        JsonArray timeRangesArr = r["timeRanges"].as<JsonArray>();
        rule.timeRanges.clear();
        rule.timeRanges.reserve(timeRangesArr.size());
        for (JsonObject tr : timeRangesArr) {
            TimeRange timeRange;
            timeRange.startTime = PsString(tr["startTime"] | "", PsramAllocator<char>());
            timeRange.endTime   = PsString(tr["endTime"]   | "", PsramAllocator<char>());
            
            JsonArray daysArr = tr["days"].as<JsonArray>();
            timeRange.days.clear();
            timeRange.days.reserve(daysArr.size());
            for (JsonVariant d : daysArr) {
                timeRange.days.push_back(d.as<int>());
            }
            rule.timeRanges.push_back(std::move(timeRange));
        }

        // Conditions
        JsonArray condArr = r["conditions"].as<JsonArray>();
        rule.conditions.clear();
        rule.conditions.reserve(condArr.size());
        for (JsonObject c : condArr) {
            Condition cond;
            cond.type      = PsString(c["type"]      | "", PsramAllocator<char>());
            cond.IEEE      = PsString(c["IEEE"]      | "", PsramAllocator<char>());
            cond.cluster   = c["cluster"]   | 0;
            cond.attribute = c["attribute"] | 0;
            cond.op        = PsString(c["operator"]  | "", PsramAllocator<char>());
            cond.value     = c["value"]     | 0;
            cond.logic     = PsString(c["logic"]     | "", PsramAllocator<char>());
            rule.conditions.push_back(std::move(cond));
        }

        // Actions
        JsonArray actArr = r["actions"].as<JsonArray>();
        rule.actions.clear();
        rule.actions.reserve(actArr.size());
        for (JsonObject a : actArr) {
            ActionRule act;
            act.type     = PsString(a["type"]    | "", PsramAllocator<char>());
            act.IEEE     = PsString(a["IEEE"]    | "", PsramAllocator<char>());
            act.endpoint = a["endpoint"] | 0;
            act.value    = PsString(a["value"]   | "", PsramAllocator<char>());
            act.title    = PsString(a["title"]   | "", PsramAllocator<char>());
            act.message  = PsString(a["message"] | "", PsramAllocator<char>());
            rule.actions.push_back(std::move(act));
        }

        rules_.push_back(std::move(rule));
    }

    return true;
}

// Récupère la valeur actuelle d’un attribut (simulateur Zigbee)
double RulesManager::getCurrentValue(const char* type, int cluster, int attribute, const char* IEEE) const {
    if (strcmp(type, "device") == 0) {
        char tmpKey[5];
        sprintf(tmpKey, "%04X", cluster);
        String path = String(IEEE) + ".json";
        String tmp = getZigbeeValue(path, tmpKey, String(attribute));
        if (tmp != nullptr && tmp.length()) {
            return strtol(tmp.c_str(), nullptr, 16);
        }
        return -9999999;
    }
    return -1;
}

// Évalue une condition
bool RulesManager::evaluateCondition(const Condition& cond) const {
    double cur = getCurrentValue(cond.type.c_str(), cond.cluster, cond.attribute, cond.IEEE.c_str());
    if (cur == -9999999) return false;

    if (cond.op == "==") return cur == cond.value;
    if (cond.op == "!=") return cur != cond.value;
    if (cond.op == "<")  return cur <  cond.value;
    if (cond.op == "<=") return cur <= cond.value;
    if (cond.op == ">")  return cur >  cond.value;
    if (cond.op == ">=") return cur >= cond.value;
    return false;
}

// Vérifie si l'heure actuelle est dans une des plages horaires définies
bool RulesManager::isInTimeRange(const Rule& rule) const {
    // Si pas de plages horaires définies, la règle est toujours valide
    if (rule.timeRanges.size() == 0) {
        return true;
    }
    
    // Obtenir l'heure actuelle
    int currentHour = Hour.toInt();
    int currentMinute = Minute.toInt();
    int currentTimeMinutes = currentHour * 60 + currentMinute;
    
    // Obtenir le jour de la semaine (1=Lun, 2=Mar, ..., 7=Dim)
    int currentDay = weekday();  // 1-7 (1=Dimanche selon TimeLib)
    // Conversion: TimeLib (Dim=1) → ISO (Lun=1, Dim=7)
    int dayISO = (currentDay == 1) ? 7 : (currentDay - 1);
    
    // Vérifier chaque plage
    for (const auto& timeRange : rule.timeRanges) {
        // Vérifier si le jour actuel est dans la plage
        bool dayMatch = false;
        for (int day : timeRange.days) {
            if (day == dayISO) {
                dayMatch = true;
                break;
            }
        }
        
        if (!dayMatch) continue;  // Jour non valide pour cette plage
        
        // Parser startTime et endTime
        String startStr = String(timeRange.startTime.c_str());
        String endStr = String(timeRange.endTime.c_str());
        
        int startHour = startStr.substring(0, 2).toInt();
        int startMin = startStr.substring(3, 5).toInt();
        int startMinutes = startHour * 60 + startMin;
        
        int endHour = endStr.substring(0, 2).toInt();
        int endMin = endStr.substring(3, 5).toInt();
        int endMinutes = endHour * 60 + endMin;
        
        // Vérifier si l'heure actuelle est dans la plage
        if (currentTimeMinutes >= startMinutes && currentTimeMinutes <= endMinutes) {
            return true;  // On est dans une plage valide
        }
    }
    
    return false;  // Aucune plage valide
}

// Applique toutes les règles et exécute les actions si besoin
void RulesManager::applyRules() {
    for (const auto& rule : rules_) {
        if (!isInTimeRange(rule)) {
            continue;
        }
        bool result = (rule.conditions.size() > 0);
        
        for (const auto& cond : rule.conditions) {
            bool ok = evaluateCondition(cond);
            if (cond.logic == "AND") result &= ok;
            else if (cond.logic == "OR") result |= ok;
            // optimisation: sortie anticipée
            if ((cond.logic == "AND" && !result) || (cond.logic == "OR" && result)) break;
        }

        // état précédent
        String hist = config_read("statusRules.json", rule.name.c_str());
        int    oldSt = 0;
        String oldDt;
        if (hist.length()) {
            char *pch = strtok((char*)hist.c_str(), "|");
            oldSt = pch ? atoi(pch) : 0;
            pch = strtok(nullptr, "|");
            oldDt = pch ? String(pch) : String();
        }

        // changement d’état
        if (result && oldSt != 1) {
            String newVal = "1|" + FormattedDate;
            config_write("statusRules.json", rule.name.c_str(), newVal);
            for (auto& act : rule.actions) {
                if (act.type == "onoff") {
                    String shortAddr = String(GetShortAddr(String(act.IEEE.c_str()) + ".json"));
                    SendOnOffAction(shortAddr.toInt(), act.endpoint, act.value.c_str());
                    log_w("Action exec: %s ep=%d val=%s",
                          act.type.c_str(), act.endpoint, act.value.c_str());
                }else if (act.type == "notification") {
                    notifList->push(Notification{act.title.c_str(),act.message.c_str(),FormattedDate,0,0});
                    notificationManager.addNotification(
                        String(act.title.c_str()), 
                        String(act.message.c_str()), 
                        0 
                    );
                    log_w("Action exec: notification - title='%s'", act.title.c_str());
                }
            }
        }
        else if (!result && oldSt != 0) {
            String newVal = "0|" + FormattedDate;
            config_write("statusRules.json", rule.name.c_str(), newVal);
        }



    }
}

// Récupère le statut (0 ou 1) d’une règle nommée
int RulesManager::getStatusRule(const char* name) const {
    String hist = config_read("statusRules.json", String(name));
    if (hist.length()) {
        char* p = strtok((char*)hist.c_str(), "|");
        return p ? atoi(p) : 0;
    }
    return 0;
}

// Récupère la date de la dernière exécution
String RulesManager::getLastDateRule(const char* name) const {
    String hist = config_read("statusRules.json", String(name));
    if (hist.length()) {
        strtok((char*)hist.c_str(), "|");       // saute le status
        char* p = strtok(nullptr, "|");         // date
        return p ? String(p) : String();
    }
    return String();
}
