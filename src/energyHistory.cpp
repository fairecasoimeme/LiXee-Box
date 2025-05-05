#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>       // ou #include <SPIFFS.h>, à adapter
#include <ArduinoJson.h>
#include "SPIFFS_ini.h"
#include "energyHistory.h"

extern String FormattedDate;
extern String Hour;
extern String Day;
extern String Month;
extern String Minute;
extern String Year;
extern String Yesterday;


// Lit un objet JSON contenant { "256": x, "258": y, "512": z, ... }
// et le stocke dans valueMap.attributes[256] = x, etc.
static void parseValueMap(const JsonObject &obj, ValueMap &valueMap) {
    valueMap.attributes.clear();
    for (auto kv : obj) {
        // kv.key().c_str() est "256", "258", "512", etc.
        // kv.value() est la valeur associée (ex: 15780859).
        String keyStr = kv.key().c_str();
        long val = kv.value().as<long>(); // On suppose un long (ou int64_t)

        // Convertir la clé (string) en int
        // Si la clé n'est pas numérique, strtol renverra 0 ou un autre résultat inattendu
        int attrId = keyStr.toInt(); 

        valueMap.attributes[attrId] = val;
    }
}

static void parsePeriodData(const JsonObject &obj, PeriodData &pd) {
    // trend
    if (obj.containsKey("trend")) {
        parseValueMap(obj["trend"].as<JsonObject>(), pd.trend);
    }
    // last
    if (obj.containsKey("last")) {
        parseValueMap(obj["last"].as<JsonObject>(), pd.last);
    }

    // graph
    if (obj.containsKey("graph")) {
        JsonObject graphObj = obj["graph"].as<JsonObject>();
        for (auto kv : graphObj) {
            // kv.key() ex: "10", "11", ...
            PsString keyPS(kv.key().c_str()); 
            ValueMap vm;
            parseValueMap(kv.value().as<JsonObject>(), vm);
            pd.graph[keyPS] = vm;
        }
    }

    // Les autres clés (par ex: "10", "11", "03", etc.) => data
    // -> On exclut trend / last / graph qui sont déjà traités.
    for (auto kv : obj) {
        const char* rawKey = kv.key().c_str();
        PsString keyPS(rawKey);

        if (keyPS == PsString("trend") || 
            keyPS == PsString("last")  || 
            keyPS == PsString("graph")) {
            continue; // déjà traité
        }
        if (!kv.value().is<JsonObject>()) {
            continue; // ce n'est pas un objet => on ignore
        }

        ValueMap vm;
        parseValueMap(kv.value().as<JsonObject>(), vm);
        pd.data[keyPS] = vm;
    }
}


// parse la racine
bool parseDeviceHistory(String IEEE, DeviceEnergyHistory &hist) {
    String path="/hst/nrg_"+IEEE+".json";

    if (LittleFS.exists(path))
    {
        File file = LittleFS.open(path, "r");
        if (!file) {
            log_e("Impossible d'ouvrir le fichier JSON en lecture: ", path.c_str());
            return false;
        }

        SpiRamJsonDocument doc(MAXHEAP);
        DeserializationError err = deserializeJson(doc, file);
        if (err) {
            log_e("Erreur parse JSON: %s",err.c_str());
            return false;
        }
        file.close();

        JsonObject root = doc.as<JsonObject>();
        if (root.isNull()) {
            Serial.println("Root is null");
            return false;
        }

        if (root.containsKey("hours")) {
            parsePeriodData(root["hours"].as<JsonObject>(), hist.hours);
        }
        if (root.containsKey("days")) {
            parsePeriodData(root["days"].as<JsonObject>(), hist.days);
        }
        if (root.containsKey("months")) {
            parsePeriodData(root["months"].as<JsonObject>(), hist.months);
        }
        if (root.containsKey("years")) {
            parsePeriodData(root["years"].as<JsonObject>(), hist.years);
        }
        return true;
    }

    return false;
}

/**
 * saveEnergyHistory
 * Construit une chaîne JSON (outJsonString) à partir de l'objet DeviceEnergyHistory (hist).
 */
bool saveEnergyHistory(String IEEE,const DeviceEnergyHistory &hist) 
{
    SpiRamJsonDocument doc(MAXHEAP);

    // --------------------------------------------------------------------------------------
    // Sous-fonctions locales pour sérialiser un ValueMap et un PeriodData
    // --------------------------------------------------------------------------------------

    // buildValueMap : remplit un JsonObject (dest) à partir d'un ValueMap
    auto buildValueMap = [&](JsonObject dest, const ValueMap &valueMap) {
        // valueMap.attributes : map<int,long>
        for (auto &pair : valueMap.attributes) {
            int attrId = pair.first; 
            long val   = pair.second;

            // Convertir l'entier en chaîne pour la clé JSON
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", attrId);
            dest[buf] = val;
        }
    };

    // buildPeriodData : remplit un JsonObject (dest) à partir d'un PeriodData
    // ex: 
    //  {
    //    "trend": {...},
    //    "last":  {...},
    //    "graph": {
    //       "08": {...}, 
    //       "10": {...}, 
    //    },
    //    "08": {...}, 
    //    "10": {...}
    //  }
    auto buildPeriodData = [&](JsonObject dest, const PeriodData &pd) {
        // 1) trend
        {
            JsonObject trendObj = dest.createNestedObject("trend");
            buildValueMap(trendObj, pd.trend);
        }

        // 2) last
        {
            JsonObject lastObj  = dest.createNestedObject("last");
            buildValueMap(lastObj, pd.last);
        }

        // 3) graph
        {
            JsonObject graphObj = dest.createNestedObject("graph");
            for (auto &kv : pd.graph) {
                // kv.first : PsString, kv.second : ValueMap
                const PsString &keyPS = kv.first;
                const ValueMap &vm    = kv.second;

                // On crée un objet JSON pour cette clé (ex: "08", "10")
                JsonObject objSlot = graphObj.createNestedObject(keyPS.c_str());
                buildValueMap(objSlot, vm);
            }
        }

        // 4) data : toutes les clés hors "trend", "last", "graph"
        //    Dans la version parse, on exclut "trend"/"last"/"graph". 
        //    Ici on les a déjà traitées à part, donc on ne les range pas dans data.
        for (auto &kv : pd.data) {
            const PsString &keyPS = kv.first;
            const ValueMap &vm    = kv.second;

            // Crée un objet JSON portant la clé par ex: "03", "10", "08:30", etc.
            JsonObject objSlot = dest.createNestedObject(keyPS.c_str());
            buildValueMap(objSlot, vm);
        }
    };

    // --------------------------------------------------------------------------------------
    // 2) Construire la hiérarchie JSON 
    // --------------------------------------------------------------------------------------

    // Racine => hours, days, months, years
    {
        JsonObject hoursObj = doc.createNestedObject("hours");
        buildPeriodData(hoursObj, hist.hours);
    }
    {
        JsonObject daysObj = doc.createNestedObject("days");
        buildPeriodData(daysObj, hist.days);
    }
    {
        JsonObject monthsObj = doc.createNestedObject("months");
        buildPeriodData(monthsObj, hist.months);
    }
    {
        JsonObject yearsObj = doc.createNestedObject("years");
        buildPeriodData(yearsObj, hist.years);
    }

    // --------------------------------------------------------------------------------------
    // 3) Sérialiser en String
    // --------------------------------------------------------------------------------------
    
    String path= "/hst/nrg_"+IEEE+".json";

    File f = safeOpenFile(path.c_str(), "w+");
    if (!f) {     
        safeCloseFile(f,path.c_str());
        return false;
    }

    if (serializeJson(doc, f) == 0 )
    {
        safeCloseFile(f,path.c_str());
        return false;
    }
    safeCloseFile(f,path.c_str());

    return true;
}


// Exemple de fonction
// - "year", "month", "day", "hour" : chaines (par ex "2023","09","01","13") 
// - "section" : par ex "256", "1295", etc. 
// - "value" : la valeur numérique à stocker
bool addEnergyMeasurement(DeviceEnergyHistory &hist,
                          String section,
                          long value)
{
    long attrId = strtol(section.c_str(), nullptr, 10);
    if (attrId < 0) {
        return false;
    }

    if (value != 0)
    {
        if (Year != "")
        {
            hist.hours.data[PsString(Hour.c_str())].attributes[attrId] = value;
            hist.days.data[PsString(Day.c_str())].attributes[attrId] = value;
            hist.months.data[PsString(Month.c_str())].attributes[attrId] = value;
            hist.years.data[PsString(Year.c_str())].attributes[attrId] = value;
                
            // hour
            if (hist.hours.last.attributes[attrId]!=0)
            {
              signed int result;
              int tmpHour = (Hour.toInt() - 1);
              if (tmpHour < 0)
              {
                tmpHour = 23;
              }
              String hourtmp = tmpHour < 10 ? "0" + String(tmpHour) : String(tmpHour);
              int tmp = hist.hours.last.attributes[attrId];
              if (hist.hours.data[PsString(hourtmp.c_str())].attributes[attrId] == 0)
              {
                hist.hours.data[PsString(hourtmp.c_str())].attributes[attrId] = hist.hours.last.attributes[attrId];
              }
              result = tmp - hist.hours.data[PsString(hourtmp.c_str())].attributes[attrId];
              if (hist.hours.data[PsString(hourtmp.c_str())].attributes[attrId]!=0)
              {
                hist.hours.graph[PsString(Hour.c_str())].attributes[attrId] = result;
              }
              hist.hours.trend.attributes[attrId] = result;
            }
            hist.hours.last.attributes[attrId] = value;

            // day
            if (hist.days.last.attributes[attrId]!=0)
            {
              signed int result;
              int tmpDay = Yesterday.toInt();
              String daytmp = tmpDay < 10 ? "0" + String(tmpDay) : String(tmpDay);
              int tmp = hist.days.last.attributes[attrId];

              if (hist.days.data[PsString(daytmp.c_str())].attributes[attrId]==0)
              {
                hist.days.data[PsString(daytmp.c_str())].attributes[attrId] = hist.days.last.attributes[attrId];
              }    
              result = tmp - hist.days.data[PsString(daytmp.c_str())].attributes[attrId];

              if (hist.days.data[PsString(daytmp.c_str())].attributes[attrId]!=0)
              {
                hist.days.graph[PsString(Day.c_str())].attributes[attrId] = result;
              }
              hist.days.trend.attributes[attrId] = result;
            }
            hist.days.last.attributes[attrId] = value;

            // month
            if (hist.months.last.attributes[attrId]!=0)
            {
              signed int result;

              int tmpMonth = (Month.toInt() - 1);
              if (tmpMonth < 1)
              {
                tmpMonth = 12;
              }

              String monthtmp = tmpMonth < 10 ? "0" + String(tmpMonth) : String(tmpMonth);
              int tmp = hist.months.last.attributes[attrId];
              if (hist.months.data[PsString(monthtmp.c_str())].attributes[attrId]==0)
              {
                hist.months.data[PsString(monthtmp.c_str())].attributes[attrId] = hist.months.last.attributes[attrId];
              }
              
              result = tmp - hist.months.data[PsString(monthtmp.c_str())].attributes[attrId];
              if (hist.months.data[PsString(monthtmp.c_str())].attributes[attrId]!=0)
              {
                hist.months.graph[PsString(Month.c_str())].attributes[attrId] = result;
              }
              hist.months.trend.attributes[attrId] = result;
            }
            hist.months.last.attributes[attrId] = value;

            // year
            if (hist.years.last.attributes[attrId]!=0)
            {

              signed int result;
              int tmpYear = (Year.toInt() - 1);
              String yeartmp = tmpYear < 10 ? "0" + String(tmpYear) : String(tmpYear);
              int tmp = hist.years.last.attributes[attrId];
              if (hist.years.data[PsString(yeartmp.c_str())].attributes[attrId]==0)
              {
                hist.years.data[PsString(yeartmp.c_str())].attributes[attrId] = hist.years.last.attributes[attrId];
              }
              
              result = tmp - hist.years.data[PsString(yeartmp.c_str())].attributes[attrId];
              if (hist.years.data[PsString(yeartmp.c_str())].attributes[attrId]!=0)
              {
                hist.years.graph[PsString(Year.c_str())].attributes[attrId] = result;
              }

              hist.years.trend.attributes[attrId] = result;
            }
            hist.years.last.attributes[attrId] = value;
          }
    }
    
    return true;
}