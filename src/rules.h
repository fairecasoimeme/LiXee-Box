// rules.h
#pragma once

#include "config.h"
#include <Arduino.h>
#include <vector>
#include <map>
#include <ArduinoJson.h>
#include "PsramAllocator.h"
#include "device.h"

extern DeviceList devices;

// Condition de règle, tout en PSRAM
// Types supportés:
//   "device"         - Condition sur un device Zigbee
//   "device_compare" - Comparaison entre deux devices Zigbee
//   "time"           - Condition sur l'heure HH:MM
//   "time_range"     - Condition sur une plage horaire
//   "weekday"        - Condition sur le jour de semaine (1-7)
//   "date"           - Condition sur une date DD/MM ou DD/MM/YYYY
//   "day"            - Condition sur le jour du mois (1-31)
//   "month"          - Condition sur le mois (1-12)
//   "datetime"       - Condition sur date + heure
struct Condition {
    PsString type;
    PsString IEEE;
    int      cluster;
    int      attribute;
    PsString op;
    PsString value;
    PsString value2;    // Pour time_range: endTime
    PsString logic;     // AND / OR

    // Sous-champ d'un attribut composite (issue #31). Vide = attribut brut. Ex : STGE
    // (FF66/535) code ~16 etats sur ses bits ; comparer la chaine hex entiere casse des
    // qu'un autre bit change. subfield="contact_sec" -> on decode STGE et on ne compare que
    // ce champ ("Ouvert"/"Ferme"). Le decodage reutilise parseStatusRegister().
    PsString subfield;

    // Pour device_compare : 2e device
    PsString IEEE2;
    int      cluster2;
    int      attribute2;

    Condition() : cluster(0), attribute(0), cluster2(0), attribute2(0) {}
};

struct TriggerConfig {
    PsString mode;       // "timer" ou "event"
    PsString IEEE;       // Device à surveiller (pour event)
    int      cluster;    // Cluster à surveiller
    int      attribute;  // Attribut à surveiller
};

// Action de règle, tout en PSRAM
// Types supportés:
//   "device"       - Action template Zigbee
//   "onoff"        - Legacy ON/OFF (rétro-compatibilité backend uniquement)
//   "notification" - Notification avec variables {value} {device} etc.
//   "dynamic"      - Valeur dynamique : lire source, appliquer coeff+offset, envoyer
struct ActionRule {
    PsString type;

    // Pour device / onoff / dynamic (cible)
    PsString IEEE;
    PsString actionName;
    int      endpoint;

    // Pour override ou legacy
    int      command;       // -1 = utiliser le template
    PsString value;

    // Pour notification
    PsString title;
    PsString message;

    // Pour dynamic : source device
    PsString sourceIEEE;
    int      sourceCluster;
    int      sourceAttribute;
    double   coefficient;   // Multiplicateur (défaut 1.0)
    double   offset;        // Ajouté après multiplication (défaut 0)

    ActionRule() : endpoint(1), command(-1), sourceCluster(0), sourceAttribute(0),
                   coefficient(1.0), offset(0.0) {}
};

// Règle complète stockée en PSRAM
struct Rule {
    PsString      name;
    bool          enabled;
    int           duration;       // Durée de maintien en secondes (0 = immédiat)
    bool          repeat;         // true = actions à chaque évaluation
    int           cooldown;       // Secondes min entre 2 exécutions (0 = pas de cooldown)
    int           maxExecPerDay;  // Max exécutions par jour (0 = illimité)
    TriggerConfig trigger;
    std::vector<Condition,   PsramAllocator<Condition>>   conditions;
    std::vector<ActionRule,  PsramAllocator<ActionRule>>  actions;
    std::vector<ActionRule,  PsramAllocator<ActionRule>>  elseActions;

    Rule() : enabled(true), duration(0), repeat(false), cooldown(0), maxExecPerDay(0) {}
};

// Manager de règles, stocke toutes les règles en PSRAM
class RulesManager {
public:
    bool loadFromFile(const char* path);
    void applyRules();
    void applyRulesOnEvent(const char* IEEE, int cluster, int attribute);

    int    getStatusRule(const char* name) const;
    String getLastDateRule(const char* name)  const;

    const std::vector<Rule, PsramAllocator<Rule>>& getRules() const { return rules_; }
    size_t size() const { return rules_.size(); }

    const Rule* getRuleByIndex(size_t idx) const {
        return (idx < rules_.size()) ? &rules_[idx] : nullptr;
    }
    Rule* getRuleByIndex(size_t idx) {
        return (idx < rules_.size()) ? &rules_[idx] : nullptr;
    }

private:
    // subfield (issue #31) : si non vide, decode un attribut composite (STGE) et renvoie
    // seulement ce champ. Vide = valeur brute, comportement inchange.
    String getCurrentValueAsString(const char* type, int cluster, int attribute, const char* IEEE,
                                   const char* subfield = "") const;
    bool   evaluateCondition(const Condition& cond) const;

    // Conditions temporelles
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
    String substituteVariables(const String& text, const Rule& rule) const;
    void executeAction(const ActionRule& act, const Rule& rule);
    DeviceData* findDeviceByIEEE(const char* IEEE) const;

    // Toutes les règles stockées en PSRAM
    std::vector<Rule, PsramAllocator<Rule>> rules_;

    // Suivi de la durée de maintien
    std::map<PsString, unsigned long> pendingSince_;

    // Cooldown : dernière exécution par règle
    std::map<PsString, unsigned long> lastExecTime_;

    // Compteur d'exécutions quotidien
    struct ExecCounter { int count; int day; };
    std::map<PsString, ExecCounter> execCounters_;
};
