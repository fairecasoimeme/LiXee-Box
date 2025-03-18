#include <Arduino.h>
#include "rules.h"
#include "config.h"
#include "log.h"
#include "AsyncJson.h"
#define ARDUINOJSON_USE_LONG_LONG 1
#define ARDUINOJSON_SLOT_ID_SIZE 2
#include <ArduinoJson.h>
#include "SPIFFS_ini.h"
#include "onoff.h"


void jsonToRules(Rule* rules, int& ruleCount) {
    // Adjust the size according to the expected JSON size.

    const char* path ="/config/rules.json";
    File RulesFile = LittleFS.open(path, FILE_READ);
    if (!RulesFile || RulesFile.isDirectory())
    {
        DEBUG_PRINTLN(F("failed open"));
    }
    else
    {
        SpiRamJsonDocument doc(MAXHEAP);
    
        // Parse the JSON string
        DeserializationError error = deserializeJson(doc, RulesFile);
        if (error) {
            DEBUG_PRINT(F("deserializeJson() failed: "));
            DEBUG_PRINTLN(error.f_str());
            return;
        }

        // Extract the array of rules
        JsonArray rulesArray = doc["rules"];
        ruleCount = rulesArray.size();
        for (int i = 0; i < ruleCount; i++) {
            JsonObject ruleObject = rulesArray[i];
            Rule& rule = rules[i];
            
            // Set the rule name
            strlcpy(rule.name, ruleObject["name"], sizeof(rule.name));

            // Process conditions
            JsonArray conditionsArray = ruleObject["conditions"];
            rule.conditionSize = conditionsArray.size();
            for (int j = 0; j < rule.conditionSize; j++) {
                JsonObject condition = conditionsArray[j];
                strlcpy(rule.rc[j].type, condition["type"].as<String>().c_str(), sizeof(rule.rc[j].type));
                strlcpy(rule.rc[j].IEEE, condition["IEEE"].as<String>().c_str(), sizeof(rule.rc[j].IEEE));
                rule.rc[j].cluster = condition["cluster"].as<int>();
                rule.rc[j].attribute = condition["attribute"].as<int>();
                strlcpy(rule.rc[j].op, condition["operator"].as<String>().c_str(), sizeof(rule.rc[j].op));
                rule.rc[j].value = condition["value"].as<int>();
                strlcpy(rule.rc[j].logic, condition["logic"].as<String>().c_str(), sizeof(rule.rc[j].logic));
                rule.rc[j].occurences = condition["occurences"].as<int>();
            }
            
            // Process actions
            JsonArray actionsArray = ruleObject["actions"];
            rule.actionSize = actionsArray.size();
            for (int j = 0; j < rule.actionSize; j++) {
                JsonObject action = actionsArray[j];
                strlcpy(rule.ra[j].type, action["type"].as<String>().c_str(), sizeof(rule.ra[j].type));
                strlcpy(rule.ra[j].IEEE, action["IEEE"].as<String>().c_str(), sizeof(rule.ra[j].IEEE));
                strlcpy(rule.ra[j].value, action["value"].as<String>().c_str(), sizeof(rule.ra[j].value));
                rule.ra[j].endpoint = action["endpoint"].as<int>();
            }
        }
    }
}

double getCurrentValue(const char* type,int cluster, int attribute, const char* IEEE) {
    // Simulate device value retrieval
    if (strcmp(type, "device") == 0) {
        char tmpKey[5];
        sprintf(tmpKey,"%04X",cluster);
        String path = String(IEEE)+".json";
        String tmp= ini_read(path, tmpKey, (String)attribute);  
        if ((strcmp(tmp.c_str(),"Error")==0) || (tmp == NULL))
        {
            return -9999999;
        }else{
           double tmpdbl = strtol(tmp.c_str(), NULL, 16); 
           return tmpdbl; // Example value     
        }
    }
    return -1;
}


bool evaluateCondition(const RuleCondition& condition) {
    double currentValue = getCurrentValue(condition.type,condition.cluster,condition.attribute, condition.IEEE);
    if (currentValue != -9999999)
    {
        if (strcmp(condition.op, "==") == 0) {
            return currentValue == condition.value;
        } else if (strcmp(condition.op, "!=") == 0) {
            return currentValue != condition.value;
        } else if (strcmp(condition.op, "<") == 0) {
            return currentValue < condition.value;
        } else if (strcmp(condition.op, "<=") == 0) {
            return currentValue <= condition.value;
        } else if (strcmp(condition.op, ">") == 0) {
            return currentValue > condition.value;
        } else if (strcmp(condition.op, ">=") == 0) {
            return currentValue >= condition.value;
        }
    }
    return false;
}

void applyRules(Rule* rules, int ruleCount) {
    for (int i = 0; i < ruleCount; i++) {
        Rule& rule = rules[i];
        bool allConditionsMet = true;

        for (int j = 0; j < rule.conditionSize; j++) {
            bool result = evaluateCondition(rule.rc[j]);
            
            if (strcmp(rule.rc[j].logic, "AND") == 0) {
                allConditionsMet &= result;
            } else if (strcmp(rule.rc[j].logic, "OR") == 0) {
                allConditionsMet |= result;
            }
            
            if ((strcmp(rule.rc[j].logic, "AND") == 0 && !result) ||
                (strcmp(rule.rc[j].logic, "OR") == 0 && result)) {
                break;
            }
        }


        //historique
        String oldRule = config_read("statusRules.json",String(rule.name));
        int oldStatusRule=-1;
        String oldDateRule;
        
        if ((oldRule != NULL) && (oldRule[0] != '\0')) 
        {
            char * pch;
            pch = strtok ((char *)oldRule.c_str(),"|");
            if (pch != NULL)
            {
                oldStatusRule = atoi(pch);
            }
            pch = strtok (NULL, "|");
            if (pch != NULL)
            {
                oldDateRule = String(pch);
            }
        }

        if (allConditionsMet) 
        {
            if (oldStatusRule !=1)
            {
                String tmp = "1";
                tmp += "|"+FormattedDate;
                config_write("statusRules.json",String(rule.name),tmp);

                for (int k = 0; k < rule.actionSize; k++) 
                {
                    const RuleAction& action = rule.ra[k];
                    if (strcmp(action.type,"onoff")==0)
                    {
                        String inifile = String(action.IEEE)+".json";
                        String shortAddr= ini_read(inifile,"INFO", "shortAddr");  
                        SendOnOffAction(shortAddr.toInt(),action.endpoint,action.value);
                        log_w("Executing action: %s - endpoint : %d value : %s",action.type,action.endpoint,action.value);
                    }
                }
            }
        }else{
            if (oldStatusRule != 0)
            {
                String tmp ="0";
                tmp +="|"+FormattedDate; 
                config_write("statusRules.json",String(rule.name),tmp);
            }
            
        }
    }
}

int getStatusRule(const char* name)
{
    String value;
    int statusRule=0;
    value = config_read("statusRules.json", String(name));
    if ((value != NULL) && (value[0] != '\0')) 
    {
        char * pch;
        pch = strtok ((char *)value.c_str(),"|");
        if (pch != NULL)
        {
            statusRule = atoi(pch);
        }
    }else{
        statusRule=0;
    }

    return statusRule;
}

String getLastDateRule(const char* name)
{
    String value;
    String lastDateRule="";
    value = config_read("statusRules.json", String(name));
    if ((value != NULL) && (value[0] != '\0')) 
    {
        char * pch;
        pch = strtok ((char *)value.c_str(),"|");
        pch = strtok (NULL, "|");
        if (pch != NULL)
        {
            lastDateRule = String(pch);
        }
    }
    return lastDateRule;
}