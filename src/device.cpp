#include "device.h"
#include <FS.h>
#include <LittleFS.h>       // ou #include <SPIFFS.h>, à adapter
#include <ArduinoJson.h>
#include "SPIFFS_ini.h"

//-------------------------------------
// Constructeur
//-------------------------------------
DeviceData::DeviceData(const String &filename, const String &deviceID)
    : _filename(filename), _deviceID(deviceID)
{
    // On peut laisser le struct _info à vide au départ
}

time_t DeviceData::getLastSeenEpoch() const {
    // on suppose le format EXACT "YYYY-MM-DD HH:MM"
    struct tm t = {};
    const char* s = _info.lastSeen.c_str();
    t.tm_year = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + (s[3]-'0') - 1900;
    t.tm_mon  = (s[5]-'0')*10 + (s[6]-'0') - 1;
    t.tm_mday = (s[8]-'0')*10 + (s[9]-'0');
    t.tm_hour = (s[11]-'0')*10 + (s[12]-'0');
    t.tm_min  = (s[14]-'0')*10 + (s[15]-'0');
    t.tm_sec  = 0;
    t.tm_isdst = -1;
    return mktime(&t);
}

//-------------------------------------
// Charger le JSON depuis _filename
//-------------------------------------
bool DeviceData::loadFromFile() {

    File file = LittleFS.open(_filename, "r");
    if (!file) {
        log_e("Impossible d'ouvrir le fichier JSON en lecture: ", _filename.c_str());
        return false;
    }

    String jsonContent = file.readString();

    file.close();

    return parseJsonToDevice(jsonContent);
}

//-------------------------------------
// Sauvegarder le JSON dans _filename
//-------------------------------------
bool DeviceData::saveToFile() {
    String jsonContent = buildJsonFromDevice();

    File file = safeOpenFile(_filename.c_str(), "w");
    if (!file) {
        log_e("Impossible d'ouvrir le fichier JSON en écriture: %s", _filename.c_str());
        return false;
    }

    file.print(jsonContent);
    safeCloseFile(file,_filename.c_str());

    log_d("Fichier %s sauvegardé avec succès.",_filename.c_str());
    return true;
}

//-------------------------------------
// getValue / setValue pour les clusters
//-------------------------------------
String DeviceData::getValue(const std::string &cluster, const std::string &attrib) {
    auto itCluster = _values.find(cluster);
    if (itCluster == _values.end()) {
        return String("");
    }
    auto itAttrib = itCluster->second.find(attrib);
    if (itAttrib == itCluster->second.end()) {
        return String("");
    }
    return String(itAttrib->second.c_str());
}

void DeviceData::setValue(const std::string &cluster, const std::string &attrib, const std::string &val) {
    _values[cluster][attrib] = val;
}

float DeviceData::updateIndex(size_t logicalPos, float currentWh) {
    if (logicalPos >= 11) return 0.0f;  // hors limites
    IndexData &d = _indexMem[logicalPos];
    unsigned long now = millis();

    if (!d.init) {
        d.lastWh     = currentWh;
        d.lastMillis = now;
        d.lastPowerW = 0.0f;
        d.init       = true;
        return d.lastPowerW;
    }

    unsigned long deltaMs = now - d.lastMillis;
    if (deltaMs == 0 || deltaMs > MAX_INTERVAL_MS) {
        d.lastMillis = now;
        d.lastWh     = currentWh;
        d.lastPowerW = 0.0f;
        return d.lastPowerW;
    }

    float deltaWh = currentWh - d.lastWh;

    if (deltaWh <= 0) {
        deltaWh = 0;
    }else{
        _indexPos = logicalPos;
    }

    // puissance en W = (deltaWh [Wh] * 3600 [s/h]) / (deltaMs/1000 [s])
    d.lastPowerW  = (deltaWh * 3600.0f) / (deltaMs / 1000.0f);
    d.lastWh      = currentWh;
    d.lastMillis  = now;

    // debug
    log_e("Index[%u] deltaMs:%lu deltaWh:%.3f => Power:%.2f W", logicalPos, deltaMs, deltaWh, d.lastPowerW);
    return d.lastPowerW;
}

String DeviceData::getPowerW()
{
    if (_indexPos>=0)
    {
        if (_indexMem[_indexPos].init) {
            return String(static_cast<int>(round(_indexMem[_indexPos].lastPowerW)));
        }
    }
    return "---";
}

float DeviceData::getAveragePower() const {
    float sum = 0.0f;
    size_t count = 0;
    for (size_t i = 0; i < 10; ++i) {
        if (_indexMem[i].init) {
            sum += _indexMem[i].lastPowerW;
            ++count;
        }
    }
    return (count > 0) ? (sum / count) : 0.0f;
}

//-------------------------------------
// parseJsonToDevice
// Lit la String JSON et remplit _info et _values
//-------------------------------------
bool DeviceData::parseJsonToDevice(const String &jsonString) {
    SpiRamJsonDocument doc(MAXHEAP);

    DeserializationError err = deserializeJson(doc, jsonString);
    if (err) {
        log_e("Erreur de parsing JSON : %s",err.c_str());
       return false;
    }

    // Vider la map avant de la re-remplir
    _values.clear();
    _pollList.clear();

    // Lire la section INFO, si elle existe
    JsonObject infoObj = doc["INFO"].as<JsonObject>();
    if (!infoObj.isNull()) {
        _info.shortAddr        = infoObj["shortAddr"]        | "";
        _info.LQI              = infoObj["LQI"]              | "";
        _info.device_id        = infoObj["device_id"]        | "";
        _info.lastSeen         = infoObj["lastSeen"]         | "";
        _info.Status           = infoObj["Status"]           | "";
        _info.manufacturer     = infoObj["manufacturer"]     | "";
        _info.model            = infoObj["model"]            | "";
        _info.software_version = infoObj["software_version"] | "";
        _info.alias            = infoObj["alias"]            | "";
        _info.linkyMode        = infoObj["linkyMode"]        | "0";
    }

    // --- Lire la liste "poll"
    // doc["poll"] doit être un tableau
    JsonArray pollArray = doc["poll"].as<JsonArray>();
    if (!pollArray.isNull()) {
        for (auto item : pollArray) {
            // item est un objet => { "cluster":"65382", "attribut":768, ...}
            PollItem p;
            p.cluster   = item["cluster"]   | "";
            p.attribut  = item["attribut"]  | 0;
            p.poll      = item["poll"]      | 0;
            p.last      = item["last"]      | 0;
            _pollList.push_back(p);
        }
    }

    // Parcourir les autres clés (clusters)
    for (auto kv : doc.as<JsonObject>()) {
        const char* clusterName = kv.key().c_str();
        // Sauter INFO et poll (qu'on a déjà traité)
        if (strcmp(clusterName, "INFO") == 0) continue;
        if (strcmp(clusterName, "poll") == 0) continue;

        // clusterName = "1", "242", "FF66", etc. => c'est un objet
        JsonObject clusterObj = kv.value().as<JsonObject>();
        if (!clusterObj.isNull()) {
            for (auto kv2 : clusterObj) {
                const char* attribName = kv2.key().c_str();
                const char* valStr     = kv2.value().as<const char*>();
                if (!valStr) valStr = "";
                _values[clusterName][attribName] = valStr;
            }
        }
    }

    log_d("Fichier %s chargé. (DeviceID = %s)",_filename.c_str(),_deviceID.c_str());
    return true;
}

//-------------------------------------
// buildJsonFromDevice
// Construit une String JSON depuis _info et _values
//-------------------------------------
String DeviceData::buildJsonFromDevice() {
    SpiRamJsonDocument doc(MAXHEAP);

    // Section INFO
    JsonObject infoObj = doc.createNestedObject("INFO");
    infoObj["shortAddr"]        = _info.shortAddr;
    infoObj["LQI"]              = _info.LQI;
    infoObj["device_id"]        = _info.device_id;
    infoObj["lastSeen"]         = _info.lastSeen;
    infoObj["Status"]           = _info.Status;
    infoObj["manufacturer"]     = _info.manufacturer;
    infoObj["model"]            = _info.model;
    infoObj["software_version"] = _info.software_version;
    infoObj["alias"]            = _info.alias;
    infoObj["linkyMode"]        = _info.linkyMode;

    // --- Partie poll (tableau)
    JsonArray pollArray = doc.createNestedArray("poll");
    for (auto &p : _pollList) {
        JsonObject item = pollArray.createNestedObject();
        item["cluster"]   = p.cluster;
        item["attribut"]  = p.attribut;
        item["poll"]      = p.poll;
        item["last"]      = p.last;
    }

    // Autres clusters
    for (auto &pairCluster : _values) {
        const std::string &cluster = pairCluster.first;
        JsonObject cObj = doc.createNestedObject(cluster.c_str());

        for (auto &pairAttrib : pairCluster.second) {
            const std::string &attrib = pairAttrib.first;
            const std::string &val    = pairAttrib.second;
            cObj[attrib.c_str()] = val.c_str();
        }
    }

    // Conversion en String
    String output;
    serializeJson(doc, output);
    return output;
}




//-------------------------------------
// Setters pour l'INFO
//-------------------------------------
void DeviceData::setInfoShortAddr(const String &val) {
    _info.shortAddr = val;
}

void DeviceData::setInfoDeviceID(const String &val) {
    _info.device_id = val;
}

void DeviceData::setInfoLastseen(const String &val) {
    _info.lastSeen = val;
}

void DeviceData::setInfoLQI(const String &val) {
    _info.LQI = val;
}

void DeviceData::setInfoManufacturer(const String &val) {
    _info.manufacturer = val;
}

void DeviceData::setInfoModel(const String &val) {
    _info.model = val;
}

void DeviceData::setInfoSoftVersion(const String &val) {
    _info.software_version = val;
}

void DeviceData::setInfoStatus(const String &val) {
    _info.Status = val;
}

void DeviceData::setInfoAlias(const String &val) {
    _info.alias = val;
}

void DeviceData::setInfoLinkyMode(const String &val) {
    _info.linkyMode = val;
}
