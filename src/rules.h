// rules.h
#pragma once

#include "config.h"
#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>
#include "PsramAllocator.h"
#include "device.h"

extern std::vector<DeviceData*> devices;

using PsString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;

// Condition de règle, tout en PSRAM
// Types supportés:
//   "device"     - Condition sur un device Zigbee (existant)
//   "time"       - Condition sur l'heure HH:MM
//   "time_range" - Condition sur une plage horaire
//   "weekday"    - Condition sur le jour de semaine (1-7)
//   "date"       - Condition sur une date DD/MM ou DD/MM/YYYY
//   "day"        - Condition sur le jour du mois (1-31)
//   "month"      - Condition sur le mois (1-12)
//   "datetime"   - Condition sur date + heure
struct Condition {
    PsString type;      // "device", "time", "time_range", "weekday", "date", "day", "month", "datetime"
    PsString IEEE;
    int      cluster;
    int      attribute;
    PsString op;
    PsString value;
    PsString value2;    // Pour time_range: endTime
    PsString logic;
    
    Condition() : cluster(0), attribute(0) {}
};

struct TimeRange {
    PsString startTime;  // Format "HH:MM"
    PsString endTime;    // Format "HH:MM"
    std::vector<int, PsramAllocator<int>> days;  // 1=Lun, 2=Mar, ..., 7=Dim
};

struct TriggerConfig {
    PsString mode;       // "timer" ou "event"
    PsString IEEE;       // Device à surveiller (pour event)
    int      cluster;    // Cluster à surveiller
    int      attribute;  // Attribut à surveiller
};

// Action de règle, tout en PSRAM
// Format unifié pour supporter:
// - Les actions définies dans les templates (type="device")
// - Les actions legacy ON/OFF (type="onoff") 
// - Les notifications (type="notification")
struct ActionRule {
    PsString type;          // "device", "onoff" ou "notification"
    
    // Pour type="device" ou "onoff"
    PsString IEEE;          // IEEE du device cible
    PsString actionName;    // Nom de l'action dans le template (ex: "ON", "OFF", "TOGGLE", "UP", "DOWN")
    int      endpoint;      // Endpoint du device (obligatoire pour device et onoff)
    
    // Pour override ou legacy
    int      command;       // -1 = utiliser le template, sinon override
    PsString value;         // Pour legacy onoff: "0", "1", "2"
    
    // Pour type="notification"
    PsString title;   
    PsString message;
    
    ActionRule() : endpoint(1), command(-1) {}
};

// Règle complète stockée en PSRAM
struct Rule {
    PsString      name;
    TriggerConfig trigger; 
    std::vector<TimeRange,   PsramAllocator<TimeRange>>   timeRanges; 
    std::vector<Condition,   PsramAllocator<Condition>>   conditions;
    std::vector<ActionRule,  PsramAllocator<ActionRule>>  actions;
    std::vector<ActionRule,  PsramAllocator<ActionRule>>  elseActions;
};

// Manager de règles, stocke toutes les règles en PSRAM
class RulesManager {
public:
    // Charge les règles depuis le fichier JSON (dans PSRAM)
    bool loadFromFile(const char* path);

    // Applique toutes les règles chargées
    void applyRules();
    void applyRulesOnEvent(const char* IEEE, int cluster, int attribute);

    // Statut et date de la dernière exécution d'une règle
    int    getStatusRule(const char* name) const;
    String getLastDateRule(const char* name)  const;

    // Accès aux règles
    const std::vector<Rule, PsramAllocator<Rule>>& getRules() const { return rules_; }
    size_t                                       size()     const { return rules_.size(); }

    // Récupérer une règle par son index (const)
    const Rule* getRuleByIndex(size_t idx) const {
        return (idx < rules_.size()) ? &rules_[idx] : nullptr;
    }
    // Récupérer une règle par son index (modifiable)
    Rule* getRuleByIndex(size_t idx) {
        return (idx < rules_.size()) ? &rules_[idx] : nullptr;
    }

private:
    // Extraction et évaluation
    double getCurrentValue(const char* type, int cluster, int attribute, const char* IEEE) const;
    String getCurrentValueAsString(const char* type, int cluster, int attribute, const char* IEEE) const;
    bool   evaluateCondition(const Condition& cond) const;
    bool   isInTimeRange(const Rule& rule) const;

    // Évaluation des conditions temporelles
    bool evaluateTimeCondition(const Condition& cond) const;
    bool evaluateTimeRangeCondition(const Condition& cond) const;
    bool evaluateWeekdayCondition(const Condition& cond) const;
    bool evaluateDateCondition(const Condition& cond) const;
    bool evaluateDateTimeCondition(const Condition& cond) const;

    // Helpers temporels
    int  getCurrentTimeMinutes() const;
    int  parseTimeToMinutes(const String& timeStr) const;
    int  getCurrentWeekdayISO() const;
    bool isInWeekdayList(const String& daysList) const;

    bool   isNumeric(const String& str) const;
    double parseNumber(const String& str, bool isFromZigbee = false) const;

    void evaluateRule(const Rule& rule); 
    String buildConditionsText(const Rule& rule) const;
    
    // Exécute une action (device ou notification)
    void executeAction(const ActionRule& act, const Rule& rule);
    
    // Trouve un device par son IEEE
    DeviceData* findDeviceByIEEE(const char* IEEE) const;
    
    // Toutes les règles stockées en PSRAM
    std::vector<Rule, PsramAllocator<Rule>> rules_;
};