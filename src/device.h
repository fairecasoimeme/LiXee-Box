#pragma once

#include <Arduino.h>
#include <map>
#include <string>
#include <vector>
#include <stdlib.h> 
#include <time.h>  
#include "powerHistory.h"
#include "energyHistory.h"
#include "PsramAllocator.h"

using PsString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;

const uint16_t INDEX_ID_LIST[11] = {256, 258, 260, 262, 264,
    266, 268, 270, 272, 274, 1};

    const unsigned long MAX_INTERVAL_MS = 500UL * 1000;

class DeviceData {
public:

    // Override new/delete pour allouer l'objet en PSRAM
    static void* operator new(size_t size) {
        void* p = ps_malloc(size);
        return p;
    }
    static void operator delete(void* p) noexcept {
        free(p);
    }

    // Placement-new : on reçoit l'adresse et on la retourne
    static void* operator new(size_t /*size*/, void* ptr) noexcept {
        return ptr;
    }
    // Placement-delete correspondant (jamais appelé ici)
    static void  operator delete(void* /*p*/, void* /*ptr*/) noexcept {
        // noop
    }

    // Struct pour stocker la partie "INFO" du JSON
    struct Info {
        String shortAddr;
        String LQI;
        String device_id;
        String lastSeen;
        String Status;
        String manufacturer;
        String model;
        String software_version;
        String alias;
        String linkyMode;
    };

    // --- Structure pour un élément de "poll"
    struct PollItem {
        String cluster;
        uint32_t attribut;  // int/uint à adapter si besoin
        uint32_t poll;
        uint32_t last;
    };

    struct IndexData {
        float         lastWh      = 0.0f;
        unsigned long lastMillis  = 0;
        float         lastPowerW  = 0.0f;
        bool          init        = false;
    };

    // Conteneurs en PSRAM
    using PollList = std::vector<PollItem, PsramAllocator<PollItem>>;
    using InnerMap = std::map<PsString, PsString,
                         std::less<PsString>,
                         PsramAllocator<std::pair<const PsString, PsString>>>;
    using ValuesMap = std::map<PsString, InnerMap,
                         std::less<PsString>,
                         PsramAllocator<std::pair<const PsString, InnerMap>>>;

    PowerHistory powerHistory;
    DeviceEnergyHistory energyHistory;

    time_t getLastSeenEpoch() const;

    // Constructeur : on transmet le nom de fichier JSON + un "deviceID" si besoin
    DeviceData(const String &filename, const String &deviceID);
    ~DeviceData();

    // Charger / Sauvegarder depuis/vers le fichier
    bool loadFromFile();
    bool saveToFile();

    void setInfoShortAddr(const String &val);
    void setInfoLQI(const String &val);
    void setInfoDeviceID(const String &val);
    void setInfoManufacturer(const String &val);
    void setInfoModel(const String &val);
    void setInfoStatus(const String &val);
    void setInfoSoftVersion(const String &val);
    void setInfoLastseen(const String &val);
    void setInfoAlias(const String &val);
    void setInfoLinkyMode(const String &val);
  
    // Getter/Setter sur l'INFO
    Info &getInfo() { return _info; }
    const Info &getInfo() const { return _info; }

    // Accès aux autres clusters/attributs
    String getValue(const std::string &cluster, const std::string &attrib);
    void   setValue(const std::string &cluster, const std::string &attrib, const std::string &val);

    // Poll
    PollList &getPollList() { return _pollList; }
    const PollList &getPollList() const { return _pollList; }

    // Pour info, l'ID du device (issu du nom de fichier)
    String getDeviceID() const { return _deviceID; }

    float updateIndex(size_t logicalPos, float currentWh);
    String getPowerW();
    float getAveragePower() const;

private:
    // Parse un contenu JSON pour remplir _info et _values
    bool parseJsonToDevice(const String &jsonString);

    // Construit un JSON (sous forme de String) depuis _info et _values
    String buildJsonFromDevice();
    // Infos "INFO"
    Info _info;
    IndexData _indexMem[11];
    int    _indexPos = -1;

    PollList _pollList;
    ValuesMap _values;

    // Nom du fichier JSON (ex: "/devices/123.json")
    String _filename;

    // Identifiant du device (nom de fichier sans extension, ex: "123")
    String _deviceID;
};
