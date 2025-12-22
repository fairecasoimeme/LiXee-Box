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
#include "windowCovering.h"
#include "notificationManager.h"
#include "TemplateData.h"
#include <TimeLib.h>

extern String Hour;
extern String Minute;
extern String Day;
extern String Month;
extern String Year;
extern String FormattedDate;

extern NotificationManager notificationManager;
extern CircularBuffer<Notification, 10> *notifList;

// Déclaration externe de SendAction (définie dans zigbee.cpp)
extern void SendAction(int command, int ShortAddr, int endpoint, String tmpValue);

// ============================================================================
// HELPERS TEMPORELS
// ============================================================================

int RulesManager::getCurrentTimeMinutes() const {
    return Hour.toInt() * 60 + Minute.toInt();
}

int RulesManager::parseTimeToMinutes(const String& timeStr) const {
    if (timeStr.length() < 5) return -1;
    int h = timeStr.substring(0, 2).toInt();
    int m = timeStr.substring(3, 5).toInt();
    return h * 60 + m;
}

int RulesManager::getCurrentWeekdayISO() const {
    int wd = weekday();  // TimeLib: 1=Dimanche
    return (wd == 1) ? 7 : (wd - 1);
}

bool RulesManager::isInWeekdayList(const String& daysList) const {
    int currentDay = getCurrentWeekdayISO();
    if (daysList.length() == 1) {
        return daysList.toInt() == currentDay;
    }
    String list = daysList;
    int start = 0;
    while (start < (int)list.length()) {
        int end = list.indexOf(',', start);
        if (end == -1) end = list.length();
        String dayStr = list.substring(start, end);
        dayStr.trim();
        if (dayStr.toInt() == currentDay) return true;
        start = end + 1;
    }
    return false;
}

// ============================================================================
// ÉVALUATION DES CONDITIONS TEMPORELLES
// ============================================================================

bool RulesManager::evaluateTimeCondition(const Condition& cond) const {
    int currentMinutes = getCurrentTimeMinutes();
    int condMinutes = parseTimeToMinutes(String(cond.value.c_str()));
    if (condMinutes < 0) return false;
    
    String op = String(cond.op.c_str());
    if (op == "==") return currentMinutes == condMinutes;
    if (op == "!=") return currentMinutes != condMinutes;
    if (op == "<")  return currentMinutes <  condMinutes;
    if (op == "<=") return currentMinutes <= condMinutes;
    if (op == ">")  return currentMinutes >  condMinutes;
    if (op == ">=") return currentMinutes >= condMinutes;
    return false;
}

bool RulesManager::evaluateTimeRangeCondition(const Condition& cond) const {
    int currentMinutes = getCurrentTimeMinutes();
    int startMinutes = parseTimeToMinutes(String(cond.value.c_str()));
    int endMinutes = parseTimeToMinutes(String(cond.value2.c_str()));
    if (startMinutes < 0 || endMinutes < 0) return false;
    
    String op = String(cond.op.c_str());
    bool inRange;
    // Gestion du cas où la plage traverse minuit (ex: 22:00 - 06:00)
    if (startMinutes <= endMinutes) {
        inRange = (currentMinutes >= startMinutes && currentMinutes <= endMinutes);
    } else {
        inRange = (currentMinutes >= startMinutes || currentMinutes <= endMinutes);
    }
    
    if (op == "in" || op == "==") return inRange;
    if (op == "!=" || op == "not_in") return !inRange;
    return false;
}

bool RulesManager::evaluateWeekdayCondition(const Condition& cond) const {
    int currentDay = getCurrentWeekdayISO();
    String condValue = String(cond.value.c_str());
    String op = String(cond.op.c_str());
    
    if (op == "in") return isInWeekdayList(condValue);
    if (op == "not_in" || (op == "!=" && condValue.indexOf(',') >= 0)) {
        return !isInWeekdayList(condValue);
    }
    
    int condDay = condValue.toInt();
    if (op == "==") return currentDay == condDay;
    if (op == "!=") return currentDay != condDay;
    if (op == "<")  return currentDay <  condDay;
    if (op == "<=") return currentDay <= condDay;
    if (op == ">")  return currentDay >  condDay;
    if (op == ">=") return currentDay >= condDay;
    return false;
}

bool RulesManager::evaluateDateCondition(const Condition& cond) const {
    String condValue = String(cond.value.c_str());
    String op = String(cond.op.c_str());
    
    int condDay = 0, condMonth = 0, condYear = 0;
    if (condValue.length() >= 5) {
        condDay = condValue.substring(0, 2).toInt();
        condMonth = condValue.substring(3, 5).toInt();
        if (condValue.length() >= 10) {
            condYear = condValue.substring(6, 10).toInt();
        }
    }
    
    int currentDay = Day.toInt();
    int currentMonth = Month.toInt();
    int currentYear = Year.toInt();
    
    bool yearMatch = (condYear == 0) || (condYear == currentYear);
    bool monthMatch = (condMonth == currentMonth);
    bool dayMatch = (condDay == currentDay);
    
    if (op == "==") return yearMatch && monthMatch && dayMatch;
    if (op == "!=") return !(yearMatch && monthMatch && dayMatch);
    
    if (condYear == 0) {
        int currentVal = currentMonth * 100 + currentDay;
        int condVal = condMonth * 100 + condDay;
        if (op == "<")  return currentVal <  condVal;
        if (op == "<=") return currentVal <= condVal;
        if (op == ">")  return currentVal >  condVal;
        if (op == ">=") return currentVal >= condVal;
    } else {
        int currentVal = currentYear * 10000 + currentMonth * 100 + currentDay;
        int condVal = condYear * 10000 + condMonth * 100 + condDay;
        if (op == "<")  return currentVal <  condVal;
        if (op == "<=") return currentVal <= condVal;
        if (op == ">")  return currentVal >  condVal;
        if (op == ">=") return currentVal >= condVal;
    }
    return false;
}

bool RulesManager::evaluateDateTimeCondition(const Condition& cond) const {
    String condValue = String(cond.value.c_str());
    String op = String(cond.op.c_str());
    
    if (condValue.length() < 16) return false;
    
    int condDay = condValue.substring(0, 2).toInt();
    int condMonth = condValue.substring(3, 5).toInt();
    int condYear = condValue.substring(6, 10).toInt();
    int condHour = condValue.substring(11, 13).toInt();
    int condMin = condValue.substring(14, 16).toInt();
    
    int currentDay = Day.toInt();
    int currentMonth = Month.toInt();
    int currentYear = Year.toInt();
    int currentHour = Hour.toInt();
    int currentMin = Minute.toInt();
    
    long long currentTS = (long long)currentYear * 100000000LL + 
                          currentMonth * 1000000LL + 
                          currentDay * 10000LL + 
                          currentHour * 100LL + currentMin;
    long long condTS = (long long)condYear * 100000000LL + 
                       condMonth * 1000000LL + 
                       condDay * 10000LL + 
                       condHour * 100LL + condMin;
    
    if (op == "==") return currentTS == condTS;
    if (op == "!=") return currentTS != condTS;
    if (op == "<")  return currentTS <  condTS;
    if (op == "<=") return currentTS <= condTS;
    if (op == ">")  return currentTS >  condTS;
    if (op == ">=") return currentTS >= condTS;
    return false;
}

// ============================================================================
// CHARGEMENT DES RÈGLES
// ============================================================================

// Charge le JSON en PSRAM et construit le vector<Rule>
bool RulesManager::loadFromFile(const char* path) {

    if (!LittleFS.exists("/config/statusRules.json")) {
        File statusFile = LittleFS.open("/config/statusRules.json", FILE_WRITE);
        if (statusFile) {
            statusFile.print("{}");
            statusFile.close();
            Serial.println("statusRules.json créé");
        }
    }

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

        JsonObject triggerObj = r["trigger"].as<JsonObject>();
        rule.trigger.mode = PsString(triggerObj["mode"] | "timer", PsramAllocator<char>());
        rule.trigger.IEEE = PsString(triggerObj["IEEE"] | "", PsramAllocator<char>());
        rule.trigger.cluster = triggerObj["cluster"] | 0;
        rule.trigger.attribute = triggerObj["attribute"] | 0;

        // Plages horaires
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
            cond.type      = PsString(c["type"]      | "device", PsramAllocator<char>());
            cond.IEEE      = PsString(c["IEEE"]      | "", PsramAllocator<char>());
            cond.cluster   = c["cluster"]   | 0;
            cond.attribute = c["attribute"] | 0;
            cond.op        = PsString(c["operator"]  | "", PsramAllocator<char>());
            if (c["value"].is<const char*>()) {
                cond.value = PsString(c["value"].as<const char*>(), PsramAllocator<char>());
            } else if (c["value"].is<int>()) {
                char buf[32];
                sprintf(buf, "%d", c["value"].as<int>());
                cond.value = PsString(buf, PsramAllocator<char>());
            } else {
                cond.value = PsString("", PsramAllocator<char>());
            }
            // value2 pour time_range
            cond.value2    = PsString(c["value2"]    | "", PsramAllocator<char>());
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
            
            // actionName pour le nouveau format device
            act.actionName = PsString(a["actionName"] | "", PsramAllocator<char>());
            
            // endpoint obligatoire pour device et onoff
            act.endpoint = a["endpoint"] | 1;
            
            // command pour override (optionnel)
            act.command  = a.containsKey("command") ? (int)a["command"] : -1;
            
            // value pour legacy onoff
            act.value    = PsString(a["value"]   | "", PsramAllocator<char>());
            
            // Pour notifications
            act.title    = PsString(a["title"]   | "", PsramAllocator<char>());
            act.message  = PsString(a["message"] | "", PsramAllocator<char>());
            rule.actions.push_back(std::move(act));
        }


        // Actions ELSE (SINON)
        JsonArray elseActArr = r["elseActions"].as<JsonArray>();
        rule.elseActions.clear();
        rule.elseActions.reserve(elseActArr.size());
        for (JsonObject a : elseActArr) {
            ActionRule act;
            act.type     = PsString(a["type"]    | "", PsramAllocator<char>());
            act.IEEE     = PsString(a["IEEE"]    | "", PsramAllocator<char>());
            
            // actionName pour le nouveau format device
            act.actionName = PsString(a["actionName"] | "", PsramAllocator<char>());
            
            // endpoint obligatoire pour device et onoff
            act.endpoint = a["endpoint"] | 1;
            
            // command pour override (optionnel)
            act.command  = a.containsKey("command") ? (int)a["command"] : -1;
            
            // value pour legacy onoff
            act.value    = PsString(a["value"]   | "", PsramAllocator<char>());
            
            // Pour notifications
            act.title    = PsString(a["title"]   | "", PsramAllocator<char>());
            act.message  = PsString(a["message"] | "", PsramAllocator<char>());
            rule.elseActions.push_back(std::move(act));
        }
        rules_.push_back(std::move(rule));
    }

    return true;
}

// Récupère la valeur actuelle d'un attribut (simulateur Zigbee)
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

String RulesManager::getCurrentValueAsString(const char* type, int cluster, int attribute, const char* IEEE) const {
    if (strcmp(type, "device") == 0) {
        char tmpKey[5];
        sprintf(tmpKey, "%04X", cluster);
        String path = String(IEEE) + ".json";
        String tmp = getZigbeeValue(path, tmpKey, String(attribute));
        if (tmp != nullptr && tmp.length()) {
            return tmp;  // ← Retourne directement la valeur texte
        }
    }
    return "";  // Retourne string vide si pas trouvé
}

// Vérifie si une string contient un nombre valide
bool RulesManager::isNumeric(const String& str) const {
    if (str.length() == 0) return false;
    
    // Si commence par "0x" ou "0X", c'est un hex
    if (str.startsWith("0x") || str.startsWith("0X")) return true;
    
    // Vérifier si tous les caractères sont des chiffres (éventuellement avec -)
    for (size_t i = 0; i < str.length(); i++) {
        char c = str.charAt(i);
        if (i == 0 && c == '-') continue;  // Signe négatif au début OK
        if (!isDigit(c) && !isHexadecimalDigit(c)) return false;
    }
    return true;
}

// Parse un nombre (décimal ou hexadécimal)
// isFromZigbee = true si c'est une valeur lue depuis Zigbee (toujours en hex)
// isFromZigbee = false si c'est une valeur de condition (toujours en décimal)
double RulesManager::parseNumber(const String& str, bool isFromZigbee) const {
    // Si commence par "0x", c'est explicitement de l'hexadécimal
    if (str.startsWith("0x") || str.startsWith("0X")) {
        return (double)strtol(str.c_str(), nullptr, 16);
    }
    
    // Si c'est une valeur Zigbee, c'est toujours en hexadécimal
    if (isFromZigbee) {
        return (double)strtol(str.c_str(), nullptr, 16);
    }
    
    // Sinon c'est une valeur de condition, toujours en décimal
    return str.toDouble();
}

// ============================================================================
// ÉVALUATION DES CONDITIONS
// ============================================================================

bool RulesManager::evaluateCondition(const Condition& cond) const {
    String type = String(cond.type.c_str());
    
    // ===== CONDITIONS TEMPORELLES =====
    if (type == "time") {
        return evaluateTimeCondition(cond);
    }
    if (type == "time_range") {
        return evaluateTimeRangeCondition(cond);
    }
    if (type == "weekday") {
        return evaluateWeekdayCondition(cond);
    }
    if (type == "date") {
        return evaluateDateCondition(cond);
    }
    if (type == "datetime") {
        return evaluateDateTimeCondition(cond);
    }
    if (type == "day") {
        int currentDay = Day.toInt();
        int condDay = String(cond.value.c_str()).toInt();
        String op = String(cond.op.c_str());
        if (op == "==") return currentDay == condDay;
        if (op == "!=") return currentDay != condDay;
        if (op == "<")  return currentDay <  condDay;
        if (op == "<=") return currentDay <= condDay;
        if (op == ">")  return currentDay >  condDay;
        if (op == ">=") return currentDay >= condDay;
        return false;
    }
    if (type == "month") {
        int currentMonth = Month.toInt();
        int condMonth = String(cond.value.c_str()).toInt();
        String op = String(cond.op.c_str());
        if (op == "==") return currentMonth == condMonth;
        if (op == "!=") return currentMonth != condMonth;
        if (op == "<")  return currentMonth <  condMonth;
        if (op == "<=") return currentMonth <= condMonth;
        if (op == ">")  return currentMonth >  condMonth;
        if (op == ">=") return currentMonth >= condMonth;
        return false;
    }
    
    // ===== CONDITION DEVICE (code existant) =====
    String curStr = getCurrentValueAsString(cond.type.c_str(), cond.cluster, cond.attribute, cond.IEEE.c_str());
    
    if (curStr == "") return false;  // Valeur non trouvée
    
    String condValueStr = String(cond.value.c_str());
    
    // Détecter si on a affaire à des NOMBRES ou du TEXTE
    bool currentIsNumber = isNumeric(curStr);
    bool condIsNumber = isNumeric(condValueStr);
    
    // Pour == et !=, on peut comparer texte OU nombres
    if (cond.op == "==") {
        // Si les deux sont du texte, comparaison textuelle
        if (!currentIsNumber || !condIsNumber) {
            String curTrimmed = curStr;
            String condTrimmed = condValueStr;
            curTrimmed.trim();
            condTrimmed.trim();
            return curTrimmed.equalsIgnoreCase(condTrimmed);
        }
        // Si les deux sont des nombres, comparaison numérique
        double curNum = parseNumber(curStr, true);        // Valeur Zigbee = hex
        double condValueNum = parseNumber(condValueStr, false);  // Valeur condition = décimal
        return curNum == condValueNum;
    }
    
    if (cond.op == "!=") {
        // Si les deux sont du texte, comparaison textuelle
        if (!currentIsNumber || !condIsNumber) {
            String curTrimmed = curStr;
            String condTrimmed = condValueStr;
            curTrimmed.trim();
            condTrimmed.trim();
            return !curTrimmed.equalsIgnoreCase(condTrimmed);
        }
        // Si les deux sont des nombres, comparaison numérique
        double curNum = parseNumber(curStr, true);        // Valeur Zigbee = hex
        double condValueNum = parseNumber(condValueStr, false);  // Valeur condition = décimal
        return curNum != condValueNum;
    }
    
    // Pour <, <=, >, >= : UNIQUEMENT numérique (pas de sens avec du texte)
    if (!currentIsNumber || !condIsNumber) {
        log_w("Comparaison numérique impossible avec du texte : cur='%s' cond='%s'", 
              curStr.c_str(), condValueStr.c_str());
        return false;  // Impossible de comparer du texte avec <, >, etc.
    }
    
    double curNum = parseNumber(curStr, true);        // Valeur Zigbee = hex
    double condValueNum = parseNumber(condValueStr, false);  // Valeur condition = décimal
    
    log_d("Comparaison: curStr='%s' -> %.0f | condStr='%s' -> %.0f | op='%s'",
          curStr.c_str(), curNum, condValueStr.c_str(), condValueNum, cond.op.c_str());
    
    if (cond.op == "<")  return curNum <  condValueNum;
    if (cond.op == "<=") return curNum <= condValueNum;
    if (cond.op == ">")  return curNum >  condValueNum;
    if (cond.op == ">=") return curNum >= condValueNum;
    
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

// Construit le texte des conditions avec les valeurs des capteurs
String RulesManager::buildConditionsText(const Rule& rule) const {
    String conditionsText = "";
    
    for (size_t i = 0; i < rule.conditions.size(); i++) {
        const auto& cond = rule.conditions[i];
        
        // Pour les conditions temporelles, on n'affiche pas de détails device
        String condType = String(cond.type.c_str());
        if (condType != "device" && condType.length() > 0) {
            if (i > 0) conditionsText += "\n";
            conditionsText += "⏰ " + condType + ": " + String(cond.value.c_str());
            continue;
        }
        
        // Chercher le device par IEEE
        DeviceData* device = nullptr;
        String ieeeStr = String(cond.IEEE.c_str());
        
        for (size_t j = 0; j < devices.size(); j++) {
            if (devices[j]->getDeviceID() == ieeeStr) {
                device = devices[j];
                break;
            }
        }
        
        if (device) {
            // Convertir cluster en string hexa (ex: "0402")
            char clusterStr[10];
            sprintf(clusterStr, "%04X", cond.cluster);
            
            // Convertir attribute en string decimal (ex: "0")
            char attributeStr[10];
            sprintf(attributeStr, "%d", cond.attribute);
            
            // Récupérer la valeur (en hexadécimal)
            String valueHex = device->getValue(std::string(clusterStr), std::string(attributeStr));
            
            // Si pas de valeur, passer à la condition suivante
            if (valueHex.length() == 0) {
                continue;
            }
            
            // Convertir hex vers decimal et appliquer le coefficient
            String valueStr;
            float coefficient = device->GetAttributeCoefficient(cond.cluster, cond.attribute);
            
            // Vérifier si c'est une valeur numérique (commence par des chiffres hex)
            bool isNumericVal = true;
            for (size_t k = 0; k < valueHex.length(); k++) {
                char c = valueHex[k];
                if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
                    isNumericVal = false;
                    break;
                }
            }
            
            if (isNumericVal && valueHex.length() > 0) {
                // Conversion hexadécimal vers décimal
                long valueDec = strtol(valueHex.c_str(), nullptr, 16);
                double valueWithCoeff = valueDec * coefficient;
                
                // Formater selon le coefficient (entier ou décimal)
                if (coefficient == 1.0) {
                    valueStr = String((int)valueWithCoeff);
                } else {
                    valueStr = String(valueWithCoeff, 2);  // 2 décimales
                }
            } else {
                // Valeur texte (ex: "ON", "OFF")
                valueStr = valueHex;
            }
            
            // Récupérer le nom du device (alias ou model)
            String deviceName = device->getInfo().alias;
            if (deviceName.length() == 0 || deviceName == "null") {
                deviceName = device->getInfo().model;
            }
            if (deviceName.length() == 0 || deviceName == "null") {
                deviceName = ieeeStr.substring(0, 10);
            }
            
            // Récupérer le nom de l'attribut et l'unité depuis le template
            String attributeName = "";
            String unit = "";
            TemplateData* tpl = device->getTemplate();
            if (tpl != nullptr) {
                for (int k = 0; k < tpl->StateSize(); k++) {
                    if (tpl->states[k].cluster == cond.cluster && 
                        tpl->states[k].attribute == cond.attribute) {
                        attributeName = String(tpl->states[k].name);
                        unit = String(tpl->states[k].unit);
                        break;
                    }
                }
            }
            
            // Construire la ligne
            if (i > 0) {
                conditionsText += "\n";
            }
            
            // Format: "Device - Attribut: valeur unité"
            conditionsText += deviceName;
            if (attributeName.length() > 0 && attributeName != "null") {
                conditionsText += " - " + attributeName;
            }
            conditionsText += ": " + valueStr;
            if (unit.length() > 0 && unit != "null") {
                conditionsText += unit;
            }
        }
    }
    
    return conditionsText;
}


void RulesManager::evaluateRule(const Rule& rule) {

    bool result = (rule.conditions.size() > 0);
    
    for (const auto& cond : rule.conditions) {
        bool ok = evaluateCondition(cond);
        if (cond.logic == "AND") result &= ok;
        else if (cond.logic == "OR") result |= ok;
        // optimisation: sortie anticipée
        if ((cond.logic == "AND" && !result) || (cond.logic == "OR" && result)) break;
    }

    // État précédent de la règle
    String hist = config_read("statusRules.json", rule.name.c_str());
    int    oldSt = -1;
    String oldDt;
    if (hist.length() > 0 && hist != "null")  {
        char *pch = strtok((char*)hist.c_str(), "|");
        oldSt = pch ? atoi(pch) : -1;
        pch = strtok(nullptr, "|");
        oldDt = pch ? String(pch) : String();
    }
    
    // Changement d'état: exécuter les actions appropriées
    if (result && oldSt != 1) {
        String newVal = "1|" + FormattedDate;
        config_write("statusRules.json", rule.name.c_str(), newVal);
        for (const auto& act : rule.actions) {
            executeAction(act, rule);
        }
    }
    else if (!result && oldSt != 0) {
        String newVal = "0|" + FormattedDate;
        config_write("statusRules.json", rule.name.c_str(), newVal);
        for (const auto& act : rule.elseActions) {
            executeAction(act, rule);
        }
    }    
}

// Applique toutes les règles et exécute les actions si besoin
void RulesManager::applyRules() {
    for (const auto& rule : rules_) {
        if (rule.trigger.mode != "timer" && rule.trigger.mode.length() > 0) {
            continue; // Ignorer EVENT
        }
        if (!isInTimeRange(rule)) {
            continue;
        }
        evaluateRule(rule);
    }
}

// Récupère le statut (0 ou 1) d'une règle nommée
int RulesManager::getStatusRule(const char* name) const {
    String hist = config_read("statusRules.json", String(name));
    if (hist.length() > 0 && hist != "null")  {
        char* p = strtok((char*)hist.c_str(), "|");
        return p ? atoi(p) : 0;
    }
    return 0;
}

void RulesManager::applyRulesOnEvent(const char* IEEE, int cluster, int attribute) {
    for (const auto& rule : rules_) {
        if (rule.trigger.mode != "event") continue;
        
        if (rule.trigger.IEEE == IEEE &&
            rule.trigger.cluster == cluster &&
            rule.trigger.attribute == attribute) {
            
            log_d("Règle '%s' déclenchée par EVENT", rule.name.c_str());
            evaluateRule(rule);
        }
    }
}

// Récupère la date de la dernière exécution
String RulesManager::getLastDateRule(const char* name) const {
    String hist = config_read("statusRules.json", String(name));
    if (hist.length() > 0 && hist != "null")  {
        strtok((char*)hist.c_str(), "|");       // saute le status
        char* p = strtok(nullptr, "|");         // date
        return p ? String(p) : String();
    }
    return String();
}

// Trouve un device par son IEEE
DeviceData* RulesManager::findDeviceByIEEE(const char* IEEE) const {
    String ieeeStr = String(IEEE);
    for (size_t i = 0; i < devices.size(); i++) {
        if (devices[i]->getDeviceID() == ieeeStr) {
            return devices[i];
        }
    }
    return nullptr;
}

// Exécute une action (device ou notification)
void RulesManager::executeAction(const ActionRule& act, const Rule& rule) {
    
    // ===== TYPE NOTIFICATION =====
    if (act.type == "notification") {
        String baseMessage = String(act.message.c_str());
        String conditionsText = buildConditionsText(rule);
        String fullMessage = baseMessage;
        
        if (conditionsText.length() > 0) {
            if (baseMessage.length() > 0) {
                fullMessage += "\n\n";
            }
            fullMessage += conditionsText;
        }
        notifList->push(Notification{act.title.c_str(), fullMessage.c_str(), FormattedDate, 0, 0});
        notificationManager.addNotification(
            String(act.title.c_str()), 
            String(fullMessage.c_str()), 
            0 
        );
        log_w("Action exec: notification - title='%s'", act.title.c_str());
        return;
    }
    
    // ===== TYPE DEVICE (action depuis template) =====
    if (act.type == "device") {
        // Trouver le device par son IEEE
        DeviceData* device = findDeviceByIEEE(act.IEEE.c_str());
        if (!device) {
            log_e("Action exec: Device non trouvé IEEE=%s", act.IEEE.c_str());
            return;
        }
        
        // Récupérer le template du device
        TemplateData* tpl = device->getTemplate();
        if (!tpl) {
            log_e("Action exec: Template non trouvé pour IEEE=%s", act.IEEE.c_str());
            return;
        }
        
        // Chercher l'action par son nom dans le template
        String actionNameStr = String(act.actionName.c_str());
        bool actionFound = false;
        
        for (int i = 0; i < tpl->ActionSize(); i++) {
            if (actionNameStr.equalsIgnoreCase(String(tpl->actions[i].name))) {
                // Action trouvée dans le template
                int command  = (act.command >= 0) ? act.command : (int)tpl->actions[i].command;
                
                // Utiliser l'endpoint de la règle (prioritaire) ou celui du template
                int endpoint = (act.endpoint > 0) ? act.endpoint : (int)tpl->actions[i].endpoint;
                
                // Utiliser la value du template
                String value = String(tpl->actions[i].value);
                
                // Récupérer l'adresse courte du device
                String shortAddr = String(GetShortAddr(String(act.IEEE.c_str()) + ".json"));
                
                // Exécuter l'action via SendAction
                SendAction(command, shortAddr.toInt(), endpoint, value);
                
                log_w("Action exec: device=%s action='%s' cmd=%d ep=%d val=%s",
                      act.IEEE.c_str(), actionNameStr.c_str(), command, endpoint, value.c_str());
                
                actionFound = true;
                break;
            }
        }
        
        if (!actionFound) {
            log_e("Action exec: Action '%s' non trouvée dans le template de %s", 
                  actionNameStr.c_str(), act.IEEE.c_str());
        }
        return;
    }
    
    // ===== RÉTROCOMPATIBILITÉ: ancien type "onoff" =====
    if (act.type == "onoff") {
        String shortAddr = String(GetShortAddr(String(act.IEEE.c_str()) + ".json"));
        int endpoint = (act.endpoint > 0) ? act.endpoint : 1;
        SendOnOffAction(shortAddr.toInt(), endpoint, act.value.c_str());
        log_w("Action exec (legacy): onoff IEEE=%s ep=%d val=%s", 
              act.IEEE.c_str(), endpoint, act.value.c_str());
        return;
    }
    
    log_w("Action exec: type inconnu '%s'", act.type.c_str());
}