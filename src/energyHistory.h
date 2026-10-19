#pragma once
#include <ArduinoJson.h>
#include "EnergyHistory.h"
#include <Arduino.h>
#include <map>
#include "PsramAllocator.h"

struct ValueMap {
    std::map<int, long, std::less<int>,
             PsramAllocator<std::pair<const int, long>>> attributes;
};

struct PeriodData {
    // data : map<clé (ex: "03", "10"), ValueMap>
    std::map<
        PsString,          // clé
        ValueMap,          // valeur
        std::less<PsString>,
        PsramAllocator< std::pair<const PsString, ValueMap> >
    > data;

    // trend : un ValueMap
    ValueMap trend;

    // last : un ValueMap
    ValueMap last;

    // graph : map<clé (ex: "03", "10"), ValueMap>
    std::map<
        PsString,
        ValueMap,
        std::less<PsString>,
        PsramAllocator< std::pair<const PsString, ValueMap> >
    > graph;
};

class DeviceEnergyHistory {
public:
    PeriodData hours;
    PeriodData days;
    PeriodData months;
    PeriodData years;
    String lastUpdate; // "DD/MM/YYYY HH:MM" - date de la dernière mesure d'énergie

    DeviceEnergyHistory() {}
    ~DeviceEnergyHistory() {}
};

bool parseDeviceHistory(String IEEE, DeviceEnergyHistory &hist);
bool saveEnergyHistory(String IEEE,const DeviceEnergyHistory &hist);
// `gapSmoothing` : lisser le delta sur les heures ecoulees quand aucune mesure n'est arrivee
// depuis longtemps (coupure). A laisser a true pour un compteur qui emet en continu (Linky) ;
// a mettre a false pour un compteur a impulsions (eau, gaz), qui reste legitimement silencieux
// tant que rien n'est consomme.
bool addEnergyMeasurement(DeviceEnergyHistory &hist, String section,long value, bool gapSmoothing = true);
bool addSubMeterMeasurement(DeviceEnergyHistory &hist, int tariffAttrId, long totalValue);