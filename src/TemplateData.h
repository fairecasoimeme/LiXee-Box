#ifndef TEMPLATE_DATA_H
#define TEMPLATE_DATA_H

#include <Arduino.h>
#include <vector>
#include "config.h"
#include "PsramAllocator.h"
#include <ArduinoJson.h>

// Structures identiques à l'original
struct State {
    char name[50];
    unsigned int cluster;
    unsigned int attribute;
    uint16_t manufacturerCode;
    char mode[20];
    char mqtt_device_class[20];
    char mqtt_state_class[20];
    char mqtt_icon[20];
    char type[20];
    float coefficient;
    char unit[20];
    bool visible;
    bool writable;      
    int  typewritable;           // Indique si l'attribut est modifiable (défaut: false)
    char typeJauge[20];
    int jaugeMin;
    int jaugeMax;
    
    State() : cluster(0), attribute(0), coefficient(1.0f), visible(true),
              writable(false),typewritable(0), jaugeMin(0), jaugeMax(100) {
        memset(name, 0, sizeof(name));
        memset(mode, 0, sizeof(mode));
        memset(mqtt_device_class, 0, sizeof(mqtt_device_class));
        memset(mqtt_state_class, 0, sizeof(mqtt_state_class));
        memset(mqtt_icon, 0, sizeof(mqtt_icon));
        memset(type, 0, sizeof(type));
        memset(unit, 0, sizeof(unit));
        memset(typeJauge, 0, sizeof(typeJauge));
    }
};

struct Action {
    char name[50];
    unsigned int command;
    unsigned int endpoint;
    unsigned int value;
    unsigned int cluster;           // NOUVEAU: Cluster pour commandes cluster-specific
    uint16_t manufacturerCode;      // NOUVEAU: Manufacturer code (0 = standard)
    bool visible;
    
    Action() : command(0), endpoint(0), value(0), cluster(0), manufacturerCode(0), visible(true) {
        memset(name, 0, sizeof(name));
    }
};

// Classe pour gérer les templates
class TemplateData {
public:
    std::vector<State, PsramAllocator<State>> states;
    std::vector<Action, PsramAllocator<Action>> actions;
    
    TemplateData() {}
    
    // Parser depuis JSON - Format réel: multi-modèles avec "status" et "action"
    // Le modelName permet de sélectionner le bon modèle dans un JSON multi-modèles
    bool parseFromJson(const char* jsonStr, const String& modelName = "", bool exactOnly = false) {
        if (!jsonStr) return false;

        SpiRamJsonDocument doc(100000);  // Plus grand pour templates complexes
        DeserializationError err = deserializeJson(doc, jsonStr);

        if (err) {
            Serial.printf("TemplateData parse error: %s\n", err.c_str());
            return false;
        }

        states.clear();
        actions.clear();

        JsonObject root = doc.as<JsonObject>();
        JsonArray modelArray;

        // 1. Chercher le modèle spécifique
        if (!modelName.isEmpty() && root.containsKey(modelName)) {
            modelArray = root[modelName].as<JsonArray>();
            Serial.printf("Modèle '%s' trouvé\n", modelName.c_str());
        }
        // Si exactOnly, ne pas faire de fallback (utilisé pour matching manufacturer)
        else if (exactOnly) {
            return false;
        }
        // 2. Fallback sur "default"
        else if (root.containsKey("default")) {
            modelArray = root["default"].as<JsonArray>();
            if (!modelName.isEmpty()) {
                Serial.printf("Utilisation du modèle 'default' (model '%s' non trouvé)\n", modelName.c_str());
            } else {
                Serial.println("Utilisation du modèle 'default'");
            }
        }
        // 3. Prendre le premier modèle disponible
        else {
            for (JsonPair kv : root) {
                modelArray = kv.value().as<JsonArray>();
                if (!modelArray.isNull()) {
                    Serial.printf("Utilisation du premier modèle: '%s'\n", kv.key().c_str());
                    break;
                }
            }
        }
        
        if (modelArray.isNull() || modelArray.size() == 0) {
            Serial.println("TemplateData: Aucun modèle trouvé dans le JSON");
            return false;
        }
        
        JsonObject modelObj = modelArray[0].as<JsonObject>();
        
        // Parser les "status" (= states)
        JsonArray statusArray = modelObj["status"].as<JsonArray>();
        if (!statusArray.isNull()) {
            for (JsonObject stateObj : statusArray) {
                State s;
                memset(&s, 0, sizeof(s));  // Init à zéro
                
                strncpy(s.name, stateObj["name"] | "", sizeof(s.name) - 1);
                
                // Cluster: peut être string hex ou int
                if (stateObj["cluster"].is<const char*>()) {
                    s.cluster = strtoul(stateObj["cluster"], nullptr, 16);
                } else {
                    s.cluster = stateObj["cluster"] | 0;
                }
                
                // Attribut (avec 't' pas 'te')
                s.attribute = stateObj["attribut"] | 0;

                // ManufacturerSpecific: peut être string hex "0x128B" ou int 4747
                if (stateObj.containsKey("manufacturerSpecific")) {
                    if (stateObj["manufacturerSpecific"].is<const char*>()) {
                        s.manufacturerCode = strtoul(stateObj["manufacturerSpecific"], nullptr, 16);
                    } else {
                        s.manufacturerCode = stateObj["manufacturerSpecific"] | 0;
                    }
                } else {
                    s.manufacturerCode = 0;  // Standard ZCL (pas manufacturer specific)
                }
                
                strncpy(s.mode, stateObj["mode"] | "", sizeof(s.mode) - 1);
                strncpy(s.mqtt_device_class, stateObj["mqtt_device_class"] | "", sizeof(s.mqtt_device_class) - 1);
                strncpy(s.mqtt_state_class, stateObj["mqtt_state_class"] | "", sizeof(s.mqtt_state_class) - 1);
                strncpy(s.mqtt_icon, stateObj["mqtt_icon"] | "", sizeof(s.mqtt_icon) - 1);
                strncpy(s.type, stateObj["type"] | "", sizeof(s.type) - 1);
                
                s.coefficient = stateObj["coefficient"] | 1.0f;
                strncpy(s.unit, stateObj["unit"] | "", sizeof(s.unit) - 1);
                s.visible = stateObj["visible"] | 1;
                s.writable = stateObj["writable"] | false;  // Par défaut readonly
                s.typewritable = stateObj["typewritable"] | 0; 
                
                // Jauge
                const char* jauge = stateObj["jauge"] | "";
                strncpy(s.typeJauge, jauge, sizeof(s.typeJauge) - 1);
                s.jaugeMin = stateObj["min"] | 0;
                s.jaugeMax = stateObj["max"] | 100;
                
                states.push_back(s);
            }
        }
        
        // Parser les "action" (= actions)
        JsonArray actionArray = modelObj["action"].as<JsonArray>();
        if (!actionArray.isNull()) {
            for (JsonObject actionObj : actionArray) {
                Action a;
                memset(&a, 0, sizeof(a));
                
                strncpy(a.name, actionObj["name"] | "", sizeof(a.name) - 1);
                a.command = actionObj["command"] | 0;
                a.endpoint = actionObj["endpoint"] | 0;
                a.value = actionObj["value"] | 0;
                a.visible = actionObj["visible"] | 1;
                
                // NOUVEAU: Cluster pour commandes cluster-specific
                if (actionObj.containsKey("cluster")) {
                    if (actionObj["cluster"].is<const char*>()) {
                        a.cluster = strtoul(actionObj["cluster"], nullptr, 16);
                    } else {
                        a.cluster = actionObj["cluster"] | 0;
                    }
                } else {
                    a.cluster = 0;
                }
                
                // NOUVEAU: ManufacturerSpecific
                if (actionObj.containsKey("manufacturerSpecific")) {
                    if (actionObj["manufacturerSpecific"].is<const char*>()) {
                        a.manufacturerCode = strtoul(actionObj["manufacturerSpecific"], nullptr, 16);
                    } else {
                        a.manufacturerCode = actionObj["manufacturerSpecific"] | 0;
                    }
                } else {
                    a.manufacturerCode = 0;
                }
                
                actions.push_back(a);
            }
        }
        
        return true;
    }
    
    // Compatibilité avec ancien code (sécurisé)
    int StateSize() const { 
        return states.size(); 
    }
    
    int ActionSize() const { 
        return actions.size(); 
    }
    
    // Accès tableau-like (compatibilité) - sécurisé
    State* getState(int index) {
        if (index < 0 || index >= (int)states.size()) {
            return nullptr;
        }
        return &states[index];
    }
    
    Action* getAction(int index) {
        if (index < 0 || index >= (int)actions.size()) {
            return nullptr;
        }
        return &actions[index];
    }
    
    // Accès direct (nouveau style) - avec vérification
    State& operator[](size_t idx) { 
        if (idx >= states.size()) {
            static State dummy;
            Serial.println("ERROR: State index out of bounds!");
            return dummy;
        }
        return states[idx];
    }
};

#endif // TEMPLATE_DATA_H