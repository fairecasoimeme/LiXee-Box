#include "device.h"
#include <FS.h>
#include <LittleFS.h>       // ou #include <SPIFFS.h>, à adapter
#include <ArduinoJson.h>
#include "SPIFFS_ini.h"
#include "TemplateCache.h" 

extern TemplateCache templateCache;

//-------------------------------------
// Constructeur / Destructeur
//-------------------------------------
DeviceData::DeviceData(const String &filename, const String &deviceID)
    : _filename(filename), _deviceID(deviceID)
{
    // _info, _pollList, _values, _indexMem auto-init
}

DeviceData::~DeviceData() {
    // Rien à libérer (PSRAM free via delete)
    clearTemplate();
}

time_t DeviceData::getLastSeenEpoch() const {
    // Format réel produit par NTPClient::getFullFormattedTime() : "DD/MM/YYYY HH:MM"
    struct tm t = {};
    const char* s = _info.lastSeen.c_str();
    int len = _info.lastSeen.length();

    // Vérifier longueur minimale et format "DD/MM/YYYY HH:MM" (16 chars)
    if (len < 16 || s[2] != '/' || s[5] != '/') {
        return (time_t)-1;  // Format invalide
    }

    t.tm_mday = (s[0]-'0')*10 + (s[1]-'0');          // DD
    t.tm_mon  = (s[3]-'0')*10 + (s[4]-'0') - 1;      // MM (0-based)
    t.tm_year = (s[6]-'0')*1000 + (s[7]-'0')*100 + (s[8]-'0')*10 + (s[9]-'0') - 1900; // YYYY
    t.tm_hour = (s[11]-'0')*10 + (s[12]-'0');          // HH
    t.tm_min  = (s[14]-'0')*10 + (s[15]-'0');          // MM
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

    bool success = parseJsonToDevice(jsonContent);
    if (success) {
        loadTemplate();  // Charge le template (lazy)
    }

    return success;
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
    PsString c(cluster.c_str(), PsramAllocator<char>());
    auto itC = _values.find(c);
    if (itC == _values.end()) return String();
    PsString a(attrib.c_str(), PsramAllocator<char>());
    auto itA = itC->second.find(a);
    if (itA == itC->second.end()) return String();
    return String(itA->second.c_str());
}

void DeviceData::setValue(const std::string &cluster, const std::string &attrib, const std::string &val) {
    PsString c(cluster.c_str(), PsramAllocator<char>());
    PsString a(attrib.c_str(), PsramAllocator<char>());
    PsString v(val.c_str(), PsramAllocator<char>());
    _values[c][a] = v;
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
    if (_indexPos >= 0 && _indexMem[_indexPos].init) {
        return String((int)round(_indexMem[_indexPos].lastPowerW));
    }
    return String("---");
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
    auto err = deserializeJson(doc, jsonString);
    if (err) {
        log_e("Parse JSON: %s", err.c_str());
        return false;
    }
    _values.clear();
    _pollList.clear();
    // INFO
    JsonObject iobj = doc["INFO"].as<JsonObject>();
    if (!iobj.isNull()) {
        _info.shortAddr        = String(iobj["shortAddr"]        | "");
        _info.LQI              = String(iobj["LQI"]              | "");
        _info.device_id        = String(iobj["device_id"]        | "");
        _info.lastSeen         = String(iobj["lastSeen"]         | "");
        _info.Status           = String(iobj["Status"]           | "");
        _info.manufacturer     = String(iobj["manufacturer"]     | "");
        _info.model            = String(iobj["model"]            | "");
        _info.endpoint         = String(iobj["endpoint"]         | "1");
        _info.software_version = String(iobj["software_version"] | "");
        _info.alias            = String(iobj["alias"]            | "");
        _info.linkyMode        = String(iobj["linkyMode"]        | "0");
        _info.powerSocket      = String(iobj["powerSocket"]      | "0");
    }
    // poll
    JsonArray parr = doc["poll"].as<JsonArray>();
    for (auto it : parr) {
        PollItem p;
        p.cluster  = String(it["cluster"]  | "");
        p.attribut = it["attribut"] | 0;
        p.poll     = it["poll"]      | 0;
        p.last     = it["last"]      | 0;
        _pollList.push_back(p);
    }
    // clusters valeurs
    for (auto kv : doc.as<JsonObject>()) {
        const char* key = kv.key().c_str();
        if (!strcmp(key, "INFO") || !strcmp(key, "poll")) continue;
        PsString ckey(key, PsramAllocator<char>());
        JsonObject obj = kv.value().as<JsonObject>();
        for (auto kv2 : obj) {
            PsString akey(kv2.key().c_str(), PsramAllocator<char>());
            const char* vs = kv2.value().as<const char*>();
            PsString vstr(vs ? vs : "", PsramAllocator<char>());
            _values[ckey][akey] = vstr;
        }
    }
    log_d("%s charge (ID=%s)", _filename.c_str(), _deviceID.c_str());
    return true;
}

//-------------------------------------
// buildJsonFromDevice
// Construit une String JSON depuis _info et _values
//-------------------------------------
String DeviceData::buildJsonFromDevice() {
    SpiRamJsonDocument doc(MAXHEAP);
    // INFO
    JsonObject iobj = doc.createNestedObject("INFO");
    iobj["shortAddr"]        = _info.shortAddr.c_str();
    iobj["LQI"]              = _info.LQI.c_str();
    iobj["device_id"]        = _info.device_id.c_str();
    iobj["lastSeen"]         = _info.lastSeen.c_str();
    iobj["Status"]           = _info.Status.c_str();
    iobj["manufacturer"]     = _info.manufacturer.c_str();
    iobj["model"]            = _info.model.c_str();
    iobj["endpoint"]         = _info.endpoint.c_str();
    iobj["software_version"] = _info.software_version.c_str();
    iobj["alias"]            = _info.alias.c_str();
    iobj["linkyMode"]        = _info.linkyMode.c_str();
    iobj["powerSocket"]      = _info.powerSocket.c_str();
    // poll
    JsonArray parr = doc.createNestedArray("poll");
    for (auto &p : _pollList) {
        JsonObject item = parr.createNestedObject();
        item["cluster"]   = p.cluster.c_str();
        item["attribut"]  = p.attribut;
        item["poll"]      = p.poll;
        item["last"]      = p.last;
    }
    // clusters
    for (auto &pc : _values) {
        JsonObject cobj = doc.createNestedObject(pc.first.c_str());
        for (auto &pa : pc.second) {
            cobj[pa.first.c_str()] = pa.second.c_str();
        }
    }
    String out;
    serializeJson(doc, out);
    return out;
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

void DeviceData::setInfoEndpoint(const String &val) {
    _info.endpoint = val;
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

void DeviceData::setInfoPowerSocket(const String &val) {
    _info.powerSocket = val;
}

bool DeviceData::loadTemplate() {
    // Si déjà chargé, ne rien faire
    if (_template != nullptr) {
        return true;
    }
    
    // Vérifier que device_id et model sont remplis
    if (_info.device_id.isEmpty()) {
        log_w("Device ID vide, impossible de charger le template");
        return false;
    }
    
    // Nom du fichier template: device_id.json
    String filename = _info.device_id + ".json";
    
    // Model pour sélection dans le JSON multi-modèles
    String model = _info.model;
    if (model.isEmpty()) {
        model = "default";
    }
    
    // Obtenir depuis le cache global
    _template = templateCache.get(filename, model);
    
    if (_template) {
        log_d("Template chargé pour device %s (model: %s): %d states, %d actions",
              _info.device_id.c_str(), 
              model.c_str(),
              _template->StateSize(),
              _template->ActionSize());
        return true;
    } else {
        log_w("Template introuvable pour device %s (fichier: %s, model: %s)",
              _info.device_id.c_str(),
              filename.c_str(),
              model.c_str());
        return false;
    }
}

TemplateData* DeviceData::getTemplate() {
    // Si pas encore chargé, charger maintenant
    if (_template == nullptr) {
        loadTemplate();
    }
    
    return _template;
}

bool DeviceData::reloadTemplate() {
    // Libérer l'ancien
    _template = nullptr;  // Le cache gère la mémoire
    
    // Recharger
    return loadTemplate();
}

void DeviceData::clearTemplate() {
    // Note: Le template est géré par le cache global,
    // on se contente de retirer notre référence
    _template = nullptr;
}

float DeviceData::GetAttributeCoefficient(int cluster, int attribute)
{
  float coefficient = 1.0; // Valeur par défaut
  if (_template != nullptr)
  {
    for (int i = 0; i < _template->StateSize(); i++)
    {
      if (_template->states[i].cluster == cluster && _template->states[i].attribute == attribute)
      {
        coefficient = _template->states[i].coefficient;
        break;
      }
    }

  }
  return coefficient;
}

bool DeviceData::hasCluster(int cluster, int attribute) const {
    if (_template == nullptr) return false;
    
    for (int i = 0; i < _template->StateSize(); i++) {
        if (_template->states[i].cluster == cluster) {
            // Si attribute == -1, on vérifie juste le cluster
            if (attribute == -1 || _template->states[i].attribute == attribute) {
                return true;
            }
        }
    }
    return false;
}