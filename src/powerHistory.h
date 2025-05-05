#pragma once

#include <Arduino.h>
#include <map>
#include <string>
#include <vector>
#include "PsramAllocator.h"

using PsString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;
// Représente un enregistrement ponctuel dans "datas"
struct DataRecord {
    PsString timeStamp;            // le champ "y"
    std::map<int, long, std::less<int>, PsramAllocator<std::pair<const int, long>>> values;   // ex: values[1295] = 900, values[2319] = 0, etc.
};

struct AttributeStats {
    long min = 0;
    long max = 0;
    long trend = 0;   // c’est la variation ? ex: -50
    long last = 0;
};


class PowerHistory {
public:
    // Tableau de DataRecord
    std::vector<DataRecord, PsramAllocator<DataRecord>> datas;

    // Stocke les stats pour chaque attribut
    // ex: stats[1295].min, stats[1295].max, etc.
     // On force un allocateur PSRAM pour la map
    std::map<int, AttributeStats,
             std::less<int>,
             PsramAllocator<std::pair<const int, AttributeStats>>> stats;

    PowerHistory() {}
    ~PowerHistory() {}
    
    // Méthodes pour:
    //  - Charger depuis JSON
    //  - Sauvegarder en JSON
    //  - Mettre à jour
};

bool parsePowerHistory(const String IEEE, PowerHistory &history);
bool savePowerHistory(String IEEE, const PowerHistory &hist);
String toJson(const PowerHistory&  history,   const String&  hourFilter);
void addMeasurement(PowerHistory &history, int attrId,long newValue);