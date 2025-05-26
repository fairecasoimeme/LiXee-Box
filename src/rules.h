// rules.h
#pragma once

#include "config.h"
#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>
#include "PsramAllocator.h"

using PsString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;

// Condition de règle, tout en PSRAM
struct Condition {
    PsString type;
    PsString IEEE;
    int      cluster;
    int      attribute;
    PsString op;
    int      value;
    PsString logic;
};

// Action de règle, tout en PSRAM
struct ActionRule {
    PsString type;
    PsString IEEE;
    int      endpoint;
    PsString value;
};

// Règle complète stockée en PSRAM
struct Rule {
    PsString                                        name;
    std::vector<Condition,   PsramAllocator<Condition>>   conditions;
    std::vector<ActionRule,  PsramAllocator<ActionRule>>  actions;
};

// Manager de règles, stocke toutes les règles en PSRAM
class RulesManager {
public:
    // Charge les règles depuis le fichier JSON (dans PSRAM)
    bool loadFromFile(const char* path);

    // Applique toutes les règles chargées
    void applyRules();

    // Statut et date de la dernière exécution d’une règle
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
    bool   evaluateCondition(const Condition& cond) const;

    // Toutes les règles stockées en PSRAM
    std::vector<Rule, PsramAllocator<Rule>> rules_;
};
