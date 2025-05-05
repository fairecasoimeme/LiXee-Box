#pragma once
#include <ArduinoJson.h>
#include "EnergyHistory.h"
#include <Arduino.h>
#include <map>
#include "PsramAllocator.h"

using PsString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;

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

    DeviceEnergyHistory() {}
    ~DeviceEnergyHistory() {}
};

bool parseDeviceHistory(String IEEE, DeviceEnergyHistory &hist);
bool saveEnergyHistory(String IEEE,const DeviceEnergyHistory &hist);
bool addEnergyMeasurement(DeviceEnergyHistory &hist, String section,long value);