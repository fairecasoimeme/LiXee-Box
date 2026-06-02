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

extern NotificationManager notificationManager;
extern CircularBuffer<Notification, 10> *notifList;

// Déclaration externe de SendAction (définie dans zigbee.cpp)
extern void SendAction(int command, int ShortAddr, int endpoint, String tmpValue);
extern void SendClusterSpecificCommand(int shortAddr, int endpoint, int cluster,
                                        int commandId, uint16_t manufacturerCode, uint8_t value);
// ============================================================================
// HELPERS TEMPORELS
// ============================================================================

int RulesManager::getCurrentTimeMinutes() const {
    return atoi(Hour) * 60 + atoi(Minute);
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

    int currentDay = atoi(Day);
    int currentMonth = atoi(Month);
    int currentYear = atoi(Year);

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

    int currentDay = atoi(Day);
    int currentMonth = atoi(Month);
    int currentYear = atoi(Year);
    int currentHour = atoi(Hour);
    int currentMin = atoi(Minute);

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
        return false;
    }

    JsonArray arr = doc["rules"].as<JsonArray>();
    rules_.clear();
    rules_.reserve(arr.size());
    pendingSince_.clear();
    lastExecTime_.clear();
    execCounters_.clear();

    for (JsonObject r : arr) {
        Rule rule;

        rule.name = PsString(r["name"] | "", PsramAllocator<char>());
        rule.enabled = r["enabled"] | true;
        rule.duration = r["duration"] | 0;
        rule.repeat   = r["repeat"] | false;
        rule.cooldown = r["cooldown"] | 0;
        rule.maxExecPerDay = r["maxExecPerDay"] | 0;

        JsonObject triggerObj = r["trigger"].as<JsonObject>();
        rule.trigger.mode = PsString(triggerObj["mode"] | "timer", PsramAllocator<char>());
        rule.trigger.IEEE = PsString(triggerObj["IEEE"] | "", PsramAllocator<char>());
        rule.trigger.cluster = triggerObj["cluster"] | 0;
        rule.trigger.attribute = triggerObj["attribute"] | 0;

        // ===== MIGRATION : timeRanges → conditions =====
        // Si des anciennes timeRanges existent, les convertir en conditions time_range + weekday
        JsonArray timeRangesArr = r["timeRanges"].as<JsonArray>();
        if (timeRangesArr && timeRangesArr.size() > 0) {
            for (JsonObject tr : timeRangesArr) {
                // Convertir chaque plage en condition time_range
                Condition trCond;
                trCond.type  = PsString("time_range", PsramAllocator<char>());
                trCond.op    = PsString("in", PsramAllocator<char>());
                trCond.value = PsString(tr["startTime"] | "00:00", PsramAllocator<char>());
                trCond.value2= PsString(tr["endTime"]   | "23:59", PsramAllocator<char>());
                trCond.logic = PsString("AND", PsramAllocator<char>());
                rule.conditions.push_back(std::move(trCond));

                // Convertir les jours en condition weekday
                JsonArray daysArr = tr["days"].as<JsonArray>();
                if (daysArr && daysArr.size() > 0 && daysArr.size() < 7) {
                    String daysList;
                    for (JsonVariant d : daysArr) {
                        if (daysList.length() > 0) daysList += ",";
                        daysList += String(d.as<int>());
                    }
                    Condition wdCond;
                    wdCond.type  = PsString("weekday", PsramAllocator<char>());
                    wdCond.op    = PsString("in", PsramAllocator<char>());
                    wdCond.value = PsString(daysList.c_str(), PsramAllocator<char>());
                    wdCond.logic = PsString("AND", PsramAllocator<char>());
                    rule.conditions.push_back(std::move(wdCond));
                }
            }
            Serial.printf("Migration: règle '%s' - %d timeRanges convertis en conditions\n",
                          r["name"] | "?", timeRangesArr.size());
        }

        // Conditions
        JsonArray condArr = r["conditions"].as<JsonArray>();
        rule.conditions.reserve(rule.conditions.size() + condArr.size());
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
                snprintf(buf, sizeof(buf), "%d", c["value"].as<int>());
                cond.value = PsString(buf, PsramAllocator<char>());
            } else {
                cond.value = PsString("", PsramAllocator<char>());
            }
            cond.value2    = PsString(c["value2"]    | "", PsramAllocator<char>());
            cond.logic     = PsString(c["logic"]     | "", PsramAllocator<char>());

            // device_compare : 2e device
            cond.IEEE2     = PsString(c["IEEE2"]     | "", PsramAllocator<char>());
            cond.cluster2  = c["cluster2"]  | 0;
            cond.attribute2= c["attribute2"]| 0;

            rule.conditions.push_back(std::move(cond));
        }

        // Actions
        JsonArray actArr = r["actions"].as<JsonArray>();
        rule.actions.clear();
        rule.actions.reserve(actArr.size());
        for (JsonObject a : actArr) {
            ActionRule act;
            act.type       = PsString(a["type"]       | "", PsramAllocator<char>());
            act.IEEE       = PsString(a["IEEE"]       | "", PsramAllocator<char>());
            act.actionName = PsString(a["actionName"] | "", PsramAllocator<char>());
            act.endpoint   = a["endpoint"] | 1;
            act.command    = a.containsKey("command") ? (int)a["command"] : -1;
            act.value      = PsString(a["value"]   | "", PsramAllocator<char>());
            act.title      = PsString(a["title"]   | "", PsramAllocator<char>());
            act.message    = PsString(a["message"] | "", PsramAllocator<char>());
            // dynamic fields
            act.sourceIEEE      = PsString(a["sourceIEEE"]      | "", PsramAllocator<char>());
            act.sourceCluster   = a["sourceCluster"]   | 0;
            act.sourceAttribute = a["sourceAttribute"] | 0;
            act.coefficient     = a["coefficient"] | 1.0;
            act.offset          = a["offset"]      | 0.0;
            rule.actions.push_back(std::move(act));
        }

        // Actions ELSE
        JsonArray elseActArr = r["elseActions"].as<JsonArray>();
        rule.elseActions.clear();
        rule.elseActions.reserve(elseActArr.size());
        for (JsonObject a : elseActArr) {
            ActionRule act;
            act.type       = PsString(a["type"]       | "", PsramAllocator<char>());
            act.IEEE       = PsString(a["IEEE"]       | "", PsramAllocator<char>());
            act.actionName = PsString(a["actionName"] | "", PsramAllocator<char>());
            act.endpoint   = a["endpoint"] | 1;
            act.command    = a.containsKey("command") ? (int)a["command"] : -1;
            act.value      = PsString(a["value"]   | "", PsramAllocator<char>());
            act.title      = PsString(a["title"]   | "", PsramAllocator<char>());
            act.message    = PsString(a["message"] | "", PsramAllocator<char>());
            act.sourceIEEE      = PsString(a["sourceIEEE"]      | "", PsramAllocator<char>());
            act.sourceCluster   = a["sourceCluster"]   | 0;
            act.sourceAttribute = a["sourceAttribute"] | 0;
            act.coefficient     = a["coefficient"] | 1.0;
            act.offset          = a["offset"]      | 0.0;
            rule.elseActions.push_back(std::move(act));
        }
        rules_.push_back(std::move(rule));
    }

    return true;
}

String RulesManager::getCurrentValueAsString(const char* type, int cluster, int attribute, const char* IEEE) const {
    if (strcmp(type, "device") == 0) {
        char tmpKey[5];
        snprintf(tmpKey, sizeof(tmpKey), "%04X", cluster);
        String path = String(IEEE) + ".json";
        String tmp = getZigbeeValue(path, tmpKey, String(attribute));
        if (tmp != nullptr && tmp.length()) {
            return tmp;
        }
    }
    return "";
}

// Vérifie si une string contient un nombre valide
bool RulesManager::isNumeric(const String& str) const {
    if (str.length() == 0) return false;
    if (str.startsWith("0x") || str.startsWith("0X")) return true;
    for (size_t i = 0; i < str.length(); i++) {
        char c = str.charAt(i);
        if (i == 0 && c == '-') continue;
        if (!isDigit(c) && !isHexadecimalDigit(c)) return false;
    }
    return true;
}

double RulesManager::parseNumber(const String& str, bool isFromZigbee) const {
    if (str.startsWith("0x") || str.startsWith("0X")) {
        return (double)strtol(str.c_str(), nullptr, 16);
    }
    if (isFromZigbee) {
        return (double)strtol(str.c_str(), nullptr, 16);
    }
    return str.toDouble();
}

// ============================================================================
// ÉVALUATION DES CONDITIONS
// ============================================================================

bool RulesManager::evaluateCondition(const Condition& cond) const {
    String type = String(cond.type.c_str());

    // ===== CONDITIONS TEMPORELLES =====
    if (type == "time") return evaluateTimeCondition(cond);
    if (type == "time_range") return evaluateTimeRangeCondition(cond);
    if (type == "weekday") return evaluateWeekdayCondition(cond);
    if (type == "date") return evaluateDateCondition(cond);
    if (type == "datetime") return evaluateDateTimeCondition(cond);

    if (type == "day") {
        int currentDay = atoi(Day);
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
        int currentMonth = atoi(Month);
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

    // ===== DEVICE COMPARE : comparaison entre deux capteurs =====
    if (type == "device_compare") {
        String curStr1 = getCurrentValueAsString("device", cond.cluster, cond.attribute, cond.IEEE.c_str());
        String curStr2 = getCurrentValueAsString("device", cond.cluster2, cond.attribute2, cond.IEEE2.c_str());
        if (curStr1 == "" || curStr2 == "") return false;

        // Coefficients des deux devices
        float coeff1 = 1.0, coeff2 = 1.0;
        DeviceData* dev1 = findDeviceByIEEE(cond.IEEE.c_str());
        if (dev1) coeff1 = dev1->GetAttributeCoefficient(cond.cluster, cond.attribute);
        DeviceData* dev2 = findDeviceByIEEE(cond.IEEE2.c_str());
        if (dev2) coeff2 = dev2->GetAttributeCoefficient(cond.cluster2, cond.attribute2);

        double val1 = parseNumber(curStr1, true) * coeff1;
        double val2 = parseNumber(curStr2, true) * coeff2;

        // Offset optionnel (stocké dans value)
        String offsetStr = String(cond.value.c_str());
        if (offsetStr.length() > 0) {
            val2 += offsetStr.toDouble();
        }

        String op = String(cond.op.c_str());
        if (op == "==") return val1 == val2;
        if (op == "!=") return val1 != val2;
        if (op == "<")  return val1 <  val2;
        if (op == "<=") return val1 <= val2;
        if (op == ">")  return val1 >  val2;
        if (op == ">=") return val1 >= val2;
        return false;
    }

    // ===== CONDITION DEVICE STANDARD =====
    String curStr = getCurrentValueAsString(cond.type.c_str(), cond.cluster, cond.attribute, cond.IEEE.c_str());
    if (curStr == "") return false;

    String condValueStr = String(cond.value.c_str());

    float coefficient = 1.0;
    if (strcmp(cond.type.c_str(), "device") == 0) {
        DeviceData* device = findDeviceByIEEE(cond.IEEE.c_str());
        if (device) {
            coefficient = device->GetAttributeCoefficient(cond.cluster, cond.attribute);
        }
    }

    bool currentIsNumber = isNumeric(curStr);
    bool condIsNumber = isNumeric(condValueStr);

    if (cond.op == "==") {
        if (!currentIsNumber || !condIsNumber) {
            String curTrimmed = curStr; String condTrimmed = condValueStr;
            curTrimmed.trim(); condTrimmed.trim();
            return curTrimmed.equalsIgnoreCase(condTrimmed);
        }
        return parseNumber(curStr, true) * coefficient == parseNumber(condValueStr, false);
    }

    if (cond.op == "!=") {
        if (!currentIsNumber || !condIsNumber) {
            String curTrimmed = curStr; String condTrimmed = condValueStr;
            curTrimmed.trim(); condTrimmed.trim();
            return !curTrimmed.equalsIgnoreCase(condTrimmed);
        }
        return parseNumber(curStr, true) * coefficient != parseNumber(condValueStr, false);
    }

    if (!currentIsNumber || !condIsNumber) {
        log_w("Comparaison numérique impossible: cur='%s' cond='%s'", curStr.c_str(), condValueStr.c_str());
        return false;
    }

    double curNum = parseNumber(curStr, true) * coefficient;
    double condValueNum = parseNumber(condValueStr, false);

    if (cond.op == "<")  return curNum <  condValueNum;
    if (cond.op == "<=") return curNum <= condValueNum;
    if (cond.op == ">")  return curNum >  condValueNum;
    if (cond.op == ">=") return curNum >= condValueNum;

    return false;
}

// Construit le texte des conditions avec les valeurs des capteurs
String RulesManager::buildConditionsText(const Rule& rule) const {
    String conditionsText = "";

    for (size_t i = 0; i < rule.conditions.size(); i++) {
        const auto& cond = rule.conditions[i];
        String condType = String(cond.type.c_str());

        if (condType != "device" && condType != "device_compare" && condType.length() > 0) {
            if (i > 0) conditionsText += "\n";
            conditionsText += condType + ": " + String(cond.value.c_str());
            continue;
        }

        DeviceData* device = findDeviceByIEEE(cond.IEEE.c_str());
        if (!device) continue;

        char clusterStr[10];
        snprintf(clusterStr, sizeof(clusterStr), "%04X", cond.cluster);
        char attributeStr[10];
        snprintf(attributeStr, sizeof(attributeStr), "%d", cond.attribute);

        String valueHex = device->getValue(clusterStr, attributeStr);
        if (valueHex.length() == 0) continue;

        String valueStr;
        float coefficient = device->GetAttributeCoefficient(cond.cluster, cond.attribute);

        bool isNumericVal = true;
        for (size_t k = 0; k < valueHex.length(); k++) {
            char c = valueHex[k];
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
                isNumericVal = false; break;
            }
        }

        if (isNumericVal && valueHex.length() > 0) {
            long valueDec = strtol(valueHex.c_str(), nullptr, 16);
            double valueWithCoeff = valueDec * coefficient;
            if (coefficient == 1.0) valueStr = String((int)valueWithCoeff);
            else valueStr = String(valueWithCoeff, 2);
        } else {
            valueStr = valueHex;
        }

        String deviceName = device->getInfo().alias;
        if (deviceName.length() == 0 || deviceName == "null") deviceName = device->getInfo().model;
        if (deviceName.length() == 0 || deviceName == "null") deviceName = String(cond.IEEE.c_str()).substring(0, 10);

        String attributeName = "";
        String unit = "";
        TemplateData* tpl = device->getTemplate();
        if (tpl != nullptr) {
            for (int k = 0; k < tpl->StateSize(); k++) {
                if (tpl->states[k].cluster == cond.cluster && tpl->states[k].attribute == cond.attribute) {
                    attributeName = String(tpl->states[k].name);
                    unit = String(tpl->states[k].unit);
                    break;
                }
            }
        }

        if (i > 0) conditionsText += "\n";
        conditionsText += deviceName;
        if (attributeName.length() > 0 && attributeName != "null") conditionsText += " - " + attributeName;
        conditionsText += ": " + valueStr;
        if (unit.length() > 0 && unit != "null") conditionsText += unit;
    }

    return conditionsText;
}

// ============================================================================
// SUBSTITUTION DE VARIABLES DANS LES NOTIFICATIONS
// ============================================================================

String RulesManager::substituteVariables(const String& text, const Rule& rule) const {
    String result = text;
    result.replace("{rule}", String(rule.name.c_str()));
    result.replace("{date}", FormattedDate);
    result.replace("{time}", String(Hour) + ":" + String(Minute));

    int devIdx = 0;
    for (const auto& cond : rule.conditions) {
        String condType = String(cond.type.c_str());
        if (condType != "device" && condType != "device_compare") continue;
        devIdx++;

        // Lire la valeur actuelle avec coefficient
        String rawVal = getCurrentValueAsString("device", cond.cluster, cond.attribute, cond.IEEE.c_str());
        String displayVal = rawVal;
        String devName = String(cond.IEEE.c_str());
        String unitStr = "";

        DeviceData* dev = findDeviceByIEEE(cond.IEEE.c_str());
        if (dev) {
            String alias = dev->getInfo().alias;
            if (alias.length() > 0 && alias != "null") devName = alias;
            else {
                String model = dev->getInfo().model;
                if (model.length() > 0 && model != "null") devName = model;
            }

            float coeff = dev->GetAttributeCoefficient(cond.cluster, cond.attribute);
            if (rawVal.length() > 0 && isNumeric(rawVal)) {
                double numVal = parseNumber(rawVal, true) * coeff;
                displayVal = (coeff == 1.0) ? String((int)numVal) : String(numVal, 2);
            }

            TemplateData* tpl = dev->getTemplate();
            if (tpl) {
                for (int k = 0; k < tpl->StateSize(); k++) {
                    if (tpl->states[k].cluster == cond.cluster && tpl->states[k].attribute == cond.attribute) {
                        unitStr = String(tpl->states[k].unit);
                        break;
                    }
                }
            }
        }

        String valWithUnit = displayVal + (unitStr.length() > 0 && unitStr != "null" ? unitStr : "");

        if (devIdx == 1) {
            result.replace("{value}", valWithUnit);
            result.replace("{device}", devName);
            result.replace("{threshold}", String(cond.value.c_str()));
        }
        result.replace("{value_" + String(devIdx) + "}", valWithUnit);
        result.replace("{device_" + String(devIdx) + "}", devName);
    }

    return result;
}

// ============================================================================
// ÉVALUATION D'UNE RÈGLE (machine à états)
// ============================================================================

void RulesManager::evaluateRule(const Rule& rule) {

    bool result = (rule.conditions.size() > 0);

    for (const auto& cond : rule.conditions) {
        bool ok = evaluateCondition(cond);
        if (cond.logic == "AND") result &= ok;
        else if (cond.logic == "OR") result |= ok;
        if ((cond.logic == "AND" && !result) || (cond.logic == "OR" && result)) break;
    }

    // État précédent (0=FAUX, 1=VRAI déclenché, 2=en attente durée, -1=inconnu)
    String hist = config_read("statusRules.json", rule.name.c_str());
    int    oldSt = -1;
    if (hist.length() > 0 && hist != "null")  {
        char *pch = strtok((char*)hist.c_str(), "|");
        oldSt = pch ? atoi(pch) : -1;
    }

    if (result) {
        if (oldSt == 1 && !rule.repeat) return;

        if (rule.duration > 0) {
            auto it = pendingSince_.find(rule.name);
            if (it == pendingSince_.end()) {
                pendingSince_[rule.name] = millis();
                if (oldSt != 2) {
                    config_write("statusRules.json", rule.name.c_str(),
                        (String("2|") + FormattedDate).c_str());
                }
            } else if (millis() - it->second >= (unsigned long)(rule.duration * 1000)) {
                pendingSince_.erase(it);

                // Cooldown check
                if (rule.cooldown > 0) {
                    auto cit = lastExecTime_.find(rule.name);
                    if (cit != lastExecTime_.end() && millis() - cit->second < (unsigned long)(rule.cooldown * 1000)) return;
                }
                // Max exec/day check
                if (rule.maxExecPerDay > 0) {
                    auto& ctr = execCounters_[rule.name];
                    int today = day();
                    if (ctr.day != today) { ctr.count = 0; ctr.day = today; }
                    if (ctr.count >= rule.maxExecPerDay) return;
                    ctr.count++;
                }

                if (oldSt != 1) {
                    config_write("statusRules.json", rule.name.c_str(),
                        (String("1|") + FormattedDate).c_str());
                }
                lastExecTime_[rule.name] = millis();
                for (const auto& act : rule.actions) executeAction(act, rule);
            }
        } else {
            // Cooldown check
            if (rule.cooldown > 0) {
                auto cit = lastExecTime_.find(rule.name);
                if (cit != lastExecTime_.end() && millis() - cit->second < (unsigned long)(rule.cooldown * 1000)) return;
            }
            // Max exec/day check
            if (rule.maxExecPerDay > 0) {
                auto& ctr = execCounters_[rule.name];
                int today = day();
                if (ctr.day != today) { ctr.count = 0; ctr.day = today; }
                if (ctr.count >= rule.maxExecPerDay) return;
                ctr.count++;
            }

            if (oldSt != 1) {
                config_write("statusRules.json", rule.name.c_str(),
                    (String("1|") + FormattedDate).c_str());
            }
            lastExecTime_[rule.name] = millis();
            for (const auto& act : rule.actions) executeAction(act, rule);
        }
    } else {
        pendingSince_.erase(rule.name);
        if (oldSt != 0) {
            config_write("statusRules.json", rule.name.c_str(),
                (String("0|") + FormattedDate).c_str());
            if (oldSt == 1) {
                for (const auto& act : rule.elseActions) executeAction(act, rule);
            }
        }
    }
}

// Applique toutes les règles timer
void RulesManager::applyRules() {
    for (const auto& rule : rules_) {
        if (!rule.enabled) continue;
        if (rule.trigger.mode != "timer" && rule.trigger.mode.length() > 0) continue;
        evaluateRule(rule);
    }
}

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
        if (!rule.enabled) continue;
        if (rule.trigger.mode != "event") continue;

        if (rule.trigger.IEEE == IEEE &&
            rule.trigger.cluster == cluster &&
            rule.trigger.attribute == attribute) {
            evaluateRule(rule);
        }
    }
}

String RulesManager::getLastDateRule(const char* name) const {
    String hist = config_read("statusRules.json", String(name));
    if (hist.length() > 0 && hist != "null")  {
        strtok((char*)hist.c_str(), "|");
        char* p = strtok(nullptr, "|");
        return p ? String(p) : String();
    }
    return String();
}

DeviceData* RulesManager::findDeviceByIEEE(const char* IEEE) const {
    String ieeeStr = String(IEEE);
    for (size_t i = 0; i < devices.size(); i++) {
        if (devices[i]->getDeviceID() == ieeeStr) {
            return devices[i];
        }
    }
    return nullptr;
}

// ============================================================================
// EXÉCUTION DES ACTIONS
// ============================================================================

void RulesManager::executeAction(const ActionRule& act, const Rule& rule) {

    // ===== TYPE NOTIFICATION (avec substitution de variables) =====
    if (act.type == "notification") {
        String titleStr = substituteVariables(String(act.title.c_str()), rule);
        String baseMessage = substituteVariables(String(act.message.c_str()), rule);
        String conditionsText = buildConditionsText(rule);
        String fullMessage = baseMessage;

        if (conditionsText.length() > 0) {
            if (baseMessage.length() > 0) fullMessage += "\n\n";
            fullMessage += conditionsText;
        }
        notifList->push(Notification{titleStr.c_str(), fullMessage.c_str(), FormattedDate, 0, 0});
        notificationManager.addNotification(titleStr, fullMessage, 0, "rule");
        log_w("Action exec: notification - title='%s'", titleStr.c_str());
        return;
    }

    // ===== TYPE DYNAMIC (valeur proportionnelle) =====
    if (act.type == "dynamic") {
        // 1. Lire la valeur source
        String rawVal = getCurrentValueAsString("device", act.sourceCluster, act.sourceAttribute, act.sourceIEEE.c_str());
        if (rawVal == "") {
            log_e("Action dynamic: source non trouvée IEEE=%s", act.sourceIEEE.c_str());
            return;
        }
        double srcVal = parseNumber(rawVal, true);

        // Appliquer le coefficient du template source
        DeviceData* srcDev = findDeviceByIEEE(act.sourceIEEE.c_str());
        if (srcDev) {
            float srcCoeff = srcDev->GetAttributeCoefficient(act.sourceCluster, act.sourceAttribute);
            srcVal *= srcCoeff;
        }

        // 2. Appliquer coefficient + offset
        double computedVal = srcVal * act.coefficient + act.offset;
        int intResult = (int)round(computedVal);
        String strResult = String(intResult);

        log_w("Action dynamic: src=%.2f * %.2f + %.2f = %d", srcVal, act.coefficient, act.offset, intResult);

        // 3. Envoyer au device cible via le template action
        DeviceData* targetDev = findDeviceByIEEE(act.IEEE.c_str());
        if (!targetDev) {
            log_e("Action dynamic: target non trouvé IEEE=%s", act.IEEE.c_str());
            return;
        }
        TemplateData* tpl = targetDev->getTemplate();
        if (!tpl) {
            log_e("Action dynamic: template non trouvé pour IEEE=%s", act.IEEE.c_str());
            return;
        }

        String actionNameStr = String(act.actionName.c_str());
        for (int i = 0; i < tpl->ActionSize(); i++) {
            if (actionNameStr.equalsIgnoreCase(String(tpl->actions[i].name))) {
                int command  = (act.command >= 0) ? act.command : (int)tpl->actions[i].command;
                int endpoint = (act.endpoint > 0) ? act.endpoint : (int)tpl->actions[i].endpoint;
                String shortAddr = String(GetShortAddr(String(act.IEEE.c_str()) + ".json"));

                // Envoyer avec la valeur calculée au lieu de la valeur template
                SendAction(command, shortAddr.toInt(), endpoint, strResult);
                log_w("Action dynamic: target=%s cmd=%d ep=%d val=%s", act.IEEE.c_str(), command, endpoint, strResult.c_str());
                break;
            }
        }
        return;
    }

    // ===== TYPE DEVICE (action depuis template) =====
    if (act.type == "device") {
        DeviceData* device = findDeviceByIEEE(act.IEEE.c_str());
        if (!device) { log_e("Action exec: Device non trouvé IEEE=%s", act.IEEE.c_str()); return; }

        TemplateData* tpl = device->getTemplate();
        if (!tpl) { log_e("Action exec: Template non trouvé pour IEEE=%s", act.IEEE.c_str()); return; }

        String actionNameStr = String(act.actionName.c_str());
        bool actionFound = false;

        for (int i = 0; i < tpl->ActionSize(); i++) {
            if (actionNameStr.equalsIgnoreCase(String(tpl->actions[i].name))) {
                int command  = (act.command >= 0) ? act.command : (int)tpl->actions[i].command;
                int endpoint = (act.endpoint > 0) ? act.endpoint : (int)tpl->actions[i].endpoint;
                String value = String(tpl->actions[i].value);
                String shortAddr = String(GetShortAddr(String(act.IEEE.c_str()) + ".json"));

                if (command == 400) {
                    int cluster = tpl->actions[i].cluster;
                    uint16_t mfrCode = tpl->actions[i].manufacturerCode;
                    uint8_t val = (uint8_t)value.toInt();
                    SendClusterSpecificCommand(shortAddr.toInt(), endpoint, cluster, 0x00, mfrCode, val);
                } else {
                    SendAction(command, shortAddr.toInt(), endpoint, value);
                }
                actionFound = true;
                break;
            }
        }

        if (!actionFound) {
            log_e("Action exec: Action '%s' non trouvée dans le template de %s", actionNameStr.c_str(), act.IEEE.c_str());
        }
        return;
    }

    // ===== RÉTROCOMPATIBILITÉ: ancien type "onoff" =====
    if (act.type == "onoff") {
        String shortAddr = String(GetShortAddr(String(act.IEEE.c_str()) + ".json"));
        int endpoint = (act.endpoint > 0) ? act.endpoint : 1;
        SendOnOffAction(shortAddr.toInt(), endpoint, act.value.c_str());
        return;
    }

    log_w("Action exec: type inconnu '%s'", act.type.c_str());
}
