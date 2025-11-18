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

        JsonObject triggerObj = r["trigger"].as<JsonObject>();
        rule.trigger.mode = PsString(triggerObj["mode"] | "timer", PsramAllocator<char>());
        rule.trigger.IEEE = PsString(triggerObj["IEEE"] | "", PsramAllocator<char>());
        rule.trigger.cluster = triggerObj["cluster"] | 0;
        rule.trigger.attribute = triggerObj["attribute"] | 0;

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
            if (c["value"].is<const char*>()) {
                cond.value = PsString(c["value"].as<const char*>(), PsramAllocator<char>());
            } else if (c["value"].is<int>()) {
                char buf[32];
                sprintf(buf, "%d", c["value"].as<int>());
                cond.value = PsString(buf, PsramAllocator<char>());
            } else {
                cond.value = PsString("", PsramAllocator<char>());
            }
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


        // ← NOUVEAU: Actions ELSE (SINON)
        JsonArray elseActArr = r["elseActions"].as<JsonArray>();
        rule.elseActions.clear();
        rule.elseActions.reserve(elseActArr.size());
        for (JsonObject a : elseActArr) {
            ActionRule act;
            act.type     = PsString(a["type"]    | "", PsramAllocator<char>());
            act.IEEE     = PsString(a["IEEE"]    | "", PsramAllocator<char>());
            act.endpoint = a["endpoint"] | 0;
            act.value    = PsString(a["value"]   | "", PsramAllocator<char>());
            act.title    = PsString(a["title"]   | "", PsramAllocator<char>());
            act.message  = PsString(a["message"] | "", PsramAllocator<char>());
            rule.elseActions.push_back(std::move(act));
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
double RulesManager::parseNumber(const String& str) const {
    // Si commence par "0x", c'est de l'hexadécimal
    if (str.startsWith("0x") || str.startsWith("0X")) {
        return (double)strtol(str.c_str(), nullptr, 16);
    }
    
    // Sinon, vérifier si c'est de l'hex sans préfixe (tous les caractères sont hex)
    bool allHex = true;
    for (size_t i = 0; i < str.length(); i++) {
        if (!isHexadecimalDigit(str.charAt(i))) {
            allHex = false;
            break;
        }
    }
    
    if (allHex && str.length() <= 4) {
        // Probablement de l'hex sans préfixe (comme "0019")
        return (double)strtol(str.c_str(), nullptr, 16);
    }
    
    // Sinon, c'est du décimal
    return str.toDouble();
}

// Évalue une condition
bool RulesManager::evaluateCondition(const Condition& cond) const {
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
        double curNum = parseNumber(curStr);
        double condValueNum = parseNumber(condValueStr);
        return curNum == condValueNum;
    }
    
    if (cond.op == "!=") {
        // Si les deux sont du texte, comparaison textuelle
        if (!currentIsNumber || !condIsNumber) {
            String curTrimmed = curStr;
            String condTrimmed = condValueStr;
            curTrimmed.trim();
            condTrimmed.trim();
            Serial.printf("Comparaison texte : cur='%s' vs cond='%s'\n", 
                     curStr.c_str(), condValueStr.c_str());
            return curTrimmed.equalsIgnoreCase(condTrimmed);
        }
        // Si les deux sont des nombres, comparaison numérique
        double curNum = parseNumber(curStr);
        double condValueNum = parseNumber(condValueStr);
        return curNum != condValueNum;
    }
    
    // Pour <, <=, >, >= : UNIQUEMENT numérique (pas de sens avec du texte)
    if (!currentIsNumber || !condIsNumber) {
        log_w("Comparaison numérique impossible avec du texte : cur='%s' cond='%s'", 
              curStr.c_str(), condValueStr.c_str());
        return false;  // Impossible de comparer du texte avec <, >, etc.
    }
    
    double curNum = parseNumber(curStr);
    double condValueNum = parseNumber(condValueStr);
    
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
            
            // Récupérer la valeur
            String value = device->getValue(std::string(clusterStr), std::string(attributeStr));
            
            // Si pas de valeur, passer à la condition suivante
            if (value.length() == 0) {
                continue;
            }
            
            // Récupérer le nom du device (alias ou model)
            String deviceName = device->getInfo().alias;
            if (deviceName.length() == 0 || deviceName == "null") {
                deviceName = device->getInfo().model;
            }
            if (deviceName.length() == 0 || deviceName == "null") {
                deviceName = ieeeStr.substring(0, 10);  // Afficher début de l'IEEE
            }
            
            // Récupérer l'unité depuis le template (si disponible)
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
            conditionsText += deviceName;
            if (attributeName.length() > 0 && attributeName != "null") {
                conditionsText += " - " + attributeName;
            }
            conditionsText += ": " + value;

            if (unit.length() > 0 && unit != "null") {
                conditionsText += " "+unit;
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
    // oldSt = -1 : règle jamais évaluée (première exécution)
    // oldSt = 0  : dernière évaluation était FALSE
    // oldSt = 1  : dernière évaluation était TRUE
    // état précédent
    String hist = config_read("statusRules.json", rule.name.c_str());
    int    oldSt = -1;
    String oldDt;
    if (hist.length() > 0 && hist != "null")  {
        char *pch = strtok((char*)hist.c_str(), "|");
        oldSt = pch ? atoi(pch) : -1;
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
                String baseMessage = String(act.message.c_str());
                String conditionsText = buildConditionsText(rule);
                String fullMessage = baseMessage;
                
                if (conditionsText.length() > 0) {
                    if (baseMessage.length() > 0) {
                        fullMessage += "\n\n";  // Double saut de ligne pour séparer
                    }
                    fullMessage += conditionsText;
                }
                notifList->push(Notification{act.title.c_str(),fullMessage.c_str(),FormattedDate,0,0});
                notificationManager.addNotification(
                    String(act.title.c_str()), 
                    String(fullMessage.c_str()), 
                    0 
                );
                log_w("Action exec: notification - title='%s'", act.title.c_str());
            }
        }
    }
    else if (!result && oldSt != 0) {
        String newVal = "0|" + FormattedDate;
        config_write("statusRules.json", rule.name.c_str(), newVal);
        // ← NOUVEAU: Exécuter les actions SINON
        for (auto& act : rule.elseActions) {
            if (act.type == "onoff") {
                String shortAddr = String(GetShortAddr(String(act.IEEE.c_str()) + ".json"));
                SendOnOffAction(shortAddr.toInt(), act.endpoint, act.value.c_str());
                log_w("ElseAction exec: %s ep=%d val=%s",
                        act.type.c_str(), act.endpoint, act.value.c_str());
            }else if (act.type == "notification") {
                String baseMessage = String(act.message.c_str());
                String conditionsText = buildConditionsText(rule);
                String fullMessage = baseMessage;
                
                if (conditionsText.length() > 0) {
                    if (baseMessage.length() > 0) {
                        fullMessage += "\n\n";  // Double saut de ligne pour séparer
                    }
                    fullMessage += conditionsText;
                }
                notifList->push(Notification{act.title.c_str(),fullMessage.c_str(),FormattedDate,0,0});
                notificationManager.addNotification(
                    String(act.title.c_str()), 
                    String(fullMessage.c_str()), 
                    0 
                );
                log_w("ElseAction exec: notification - title='%s'", act.title.c_str());
            }
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

// Récupère le statut (0 ou 1) d’une règle nommée
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