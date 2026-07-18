#pragma once

#include <Arduino.h>
#include <map>
#include <string>
#include <vector>
#include "PsramAllocator.h"
#include "config.h"   // SpiRamJsonDocument (buildPowerChartDoc), PsString
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

// ---------------------------------------------------------------------------------------
// Bascule du rendu de /loadPowerChart.
//
//   0 = String (defaut) : le JSON est construit dans une String, passee a request->send().
//   1 = streaming       : le JSON est serialise directement dans la reponse HTTP.
//
// MESURE (1225 enregistrements, JSON ~37 Ko) : le streaming s'est revele PLUS LENT --
// 84-91 ms contre 60-65 ms -- pour un heap libre inchange (133-143 Ko contre 131-151).
//
// Pourquoi il ne gagne rien : le cbuf d'AsyncResponseStream vit dans le MEME heap interne
// que la String. On n'economise que la copie transitoire de request->send() (~37 Ko pendant
// quelques ms), au prix de +25 ms a chaque appel -- le PrintWriter d'ArduinoJson ecrit
// caractere par caractere, et chaque octet traverse AsyncResponseStream::write().
//
// Conserve a 1 uniquement si le pic transitoire de heap devient le facteur limitant.
// ---------------------------------------------------------------------------------------
#define POWER_CHART_STREAM 0

bool parsePowerHistory(const String IEEE, PowerHistory &history);
bool savePowerHistory(String IEEE, const PowerHistory &hist);
String toJson(const PowerHistory&  history,   const String&  hourFilter);

// Construit le document JSON de l'historique ; l'appelant doit le delete. nullptr si echec.
// Expose pour permettre la serialisation en streaming : celle-ci doit connaitre la taille
// finale (measureJson) AVANT de creer la reponse, donc disposer du document.
SpiRamJsonDocument* buildPowerChartDoc(const PowerHistory& history, const String& nowHM);
void addMeasurement(PowerHistory &history, int attrId,long newValue);

void resetMeasurements(PowerHistory &history, const String &timeStamp);