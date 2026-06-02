#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>       // ou #include <SPIFFS.h>, à adapter
#include <ArduinoJson.h>
#include "SPIFFS_ini.h"
#include "energyHistory.h"

#include "config.h"

// Seuil anti-spike : delta maximum acceptable (en Wh) entre deux mesures consécutives.
// Au-delà, on considère que l'appareil s'est reconnecté après une période hors-ligne
// et on ne calcule pas de delta pour le graphe (évite les pics impossibles).
// 100 kWh/h = impossible en résidentiel (max abonnement France = 36 kVA)
#define MAX_ENERGY_DELTA_WH 100000


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
        if (root.containsKey("lastUpdate")) {
            hist.lastUpdate = root["lastUpdate"].as<String>();
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

    if (hist.lastUpdate.length() > 0) {
        doc["lastUpdate"] = hist.lastUpdate;
    }

    // --------------------------------------------------------------------------------------
    // 3) Sérialiser en String
    // --------------------------------------------------------------------------------------
    
    String path= "/hst/nrg_"+IEEE+".json";
    return atomicWriteJson(path.c_str(), doc);
}



// Fonction spéciale pour les sous-compteurs
// - Calcule le delta par rapport à la dernière valeur totale (attribut 0)
// - Accumule ce delta dans l'attribut tarifaire dans le graph
bool addSubMeterMeasurement(DeviceEnergyHistory &hist,
                            int tariffAttrId,
                            long totalValue)
{
    if (totalValue == 0 || Year[0] == '\0') {
        return false;
    }
    
    // Récupérer la dernière valeur totale (stockée dans attribut 0)
    long lastTotal = hist.hours.last.attributes[0];
    
    // Calculer le delta
    long delta = 0;
    if (lastTotal > 0 && totalValue > lastTotal) {
        delta = totalValue - lastTotal;
    }

    // Mettre à jour la valeur totale dans l'attribut 0
    hist.hours.data[PsString(Hour)].attributes[0] = totalValue;
    hist.days.data[PsString(Day)].attributes[0] = totalValue;
    hist.months.data[PsString(Month)].attributes[0] = totalValue;
    hist.years.data[PsString(Year)].attributes[0] = totalValue;
    hist.hours.last.attributes[0] = totalValue;
    hist.days.last.attributes[0] = totalValue;
    hist.months.last.attributes[0] = totalValue;
    hist.years.last.attributes[0] = totalValue;

    // Si pas de delta, rien à accumuler dans le tarif
    if (delta <= 0) {
        hist.lastUpdate = FormattedDate;
        return true;
    }

    // Anti-spike avec lissage pour sous-compteurs
    if (delta > MAX_ENERGY_DELTA_WH) {
        // Calculer le gap depuis lastUpdate
        int lastDay = 0, lastMonth = 0, lastYear = 0;
        if (hist.lastUpdate.length() >= 16) {
            lastDay   = hist.lastUpdate.substring(0, 2).toInt();
            lastMonth = hist.lastUpdate.substring(3, 5).toInt();
            lastYear  = hist.lastUpdate.substring(6, 10).toInt();
        }

        int curDay   = atoi(Day);
        int curMonth = atoi(Month);
        int curYear  = atoi(Year);
        int curHour  = atoi(Hour);

        int gapDays = 1;
        if (lastYear > 0) {
            long lastTotalDays = lastYear * 365L + lastMonth * 30L + lastDay;
            long curTotalDays  = curYear * 365L + curMonth * 30L + curDay;
            gapDays = (int)(curTotalDays - lastTotalDays);
            if (gapDays < 1) gapDays = 1;
        }

        long deltaPerDay = delta / gapDays;

        log_w("SubMeter spike smoothed: delta=%ld over %d days (%ld/day)", delta, gapDays, deltaPerDay);

        // DAILY : distribuer sur les jours vides du mois courant
        int startDay = (lastMonth == curMonth && lastYear == curYear) ? lastDay + 1 : 1;
        for (int d = startDay; d <= curDay; d++) {
            char dayKey[3];
            snprintf(dayKey, sizeof(dayKey), "%02d", d);
            hist.days.graph[PsString(dayKey)].attributes[tariffAttrId] += deltaPerDay;
        }
        hist.days.trend.attributes[tariffAttrId] = deltaPerDay;

        // HOURLY : distribuer la part du jour sur les heures
        int hoursToday = curHour + 1;
        long deltaPerHour = deltaPerDay / hoursToday;
        for (int h = 0; h <= curHour; h++) {
            char hourKey[3];
            snprintf(hourKey, sizeof(hourKey), "%02d", h);
            hist.hours.graph[PsString(hourKey)].attributes[tariffAttrId] += deltaPerHour;
        }
        hist.hours.trend.attributes[tariffAttrId] = deltaPerHour;

        // MONTHLY
        hist.months.graph[PsString(Month)].attributes[tariffAttrId] += delta;
        hist.months.trend.attributes[tariffAttrId] = delta;

        // YEARLY
        hist.years.graph[PsString(Year)].attributes[tariffAttrId] += delta;
        hist.years.trend.attributes[tariffAttrId] = delta;

        hist.lastUpdate = FormattedDate;
        return true;
    }

    // --- Fonctionnement normal (pas de spike) ---
    // Accumuler le delta dans l'attribut tarifaire (graph seulement)
    // Hours
    hist.hours.graph[PsString(Hour)].attributes[tariffAttrId] += delta;
    hist.hours.trend.attributes[tariffAttrId] = delta;

    // Days
    hist.days.graph[PsString(Day)].attributes[tariffAttrId] += delta;
    hist.days.trend.attributes[tariffAttrId] += delta;

    // Months
    hist.months.graph[PsString(Month)].attributes[tariffAttrId] += delta;
    hist.months.trend.attributes[tariffAttrId] += delta;

    // Years
    hist.years.graph[PsString(Year)].attributes[tariffAttrId] += delta;
    hist.years.trend.attributes[tariffAttrId] += delta;

    hist.lastUpdate = FormattedDate;
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
        if (Year[0] != '\0')
        {
            hist.hours.data[PsString(Hour)].attributes[attrId] = value;
            hist.days.data[PsString(Day)].attributes[attrId] = value;
            hist.months.data[PsString(Month)].attributes[attrId] = value;
            hist.years.data[PsString(Year)].attributes[attrId] = value;

            // --- Anti-spike avec lissage ---
            // Si la valeur a sauté de plus de MAX_ENERGY_DELTA_WH depuis la dernière mesure,
            // c'est une reconnexion après période hors-ligne.
            // Au lieu de perdre les données, on distribue le delta uniformément
            // sur la période où les données étaient absentes.
            if (hist.hours.last.attributes[attrId] != 0) {
                long previousLast = hist.hours.last.attributes[attrId];
                if (value > previousLast && (value - previousLast) > MAX_ENERGY_DELTA_WH) {
                    long totalDelta = value - previousLast;

                    // Calculer le gap depuis lastUpdate
                    int lastDay = 0, lastMonth = 0, lastYear = 0, lastHour = 0;
                    if (hist.lastUpdate.length() >= 16) {
                        lastDay   = hist.lastUpdate.substring(0, 2).toInt();
                        lastMonth = hist.lastUpdate.substring(3, 5).toInt();
                        lastYear  = hist.lastUpdate.substring(6, 10).toInt();
                        lastHour  = hist.lastUpdate.substring(11, 13).toInt();
                    }

                    int curDay   = atoi(Day);
                    int curMonth = atoi(Month);
                    int curYear  = atoi(Year);
                    int curHour  = atoi(Hour);

                    // Gap en jours (approximatif si cross-mois)
                    int gapDays = 1;
                    if (lastYear > 0) {
                        long lastTotalDays  = lastYear * 365L + lastMonth * 30L + lastDay;
                        long curTotalDays   = curYear * 365L + curMonth * 30L + curDay;
                        gapDays = (int)(curTotalDays - lastTotalDays);
                        if (gapDays < 1) gapDays = 1;
                    }

                    long deltaPerDay = totalDelta / gapDays;

                    log_w("Energy spike smoothed: attr=%ld delta=%ld over %d days (%ld/day)",
                          attrId, totalDelta, gapDays, deltaPerDay);

                    // --- DAILY : distribuer sur les jours vides du mois courant ---
                    int startDay = (lastMonth == curMonth && lastYear == curYear) ? lastDay + 1 : 1;
                    for (int d = startDay; d <= curDay; d++) {
                        char dayKey[3];
                        snprintf(dayKey, sizeof(dayKey), "%02d", d);
                        hist.days.graph[PsString(dayKey)].attributes[attrId] = deltaPerDay;
                        hist.days.data[PsString(dayKey)].attributes[attrId] =
                            previousLast + deltaPerDay * (d - startDay + 1);
                    }
                    hist.days.trend.attributes[attrId] = deltaPerDay;
                    hist.days.last.attributes[attrId] = value;

                    // --- HOURLY : distribuer la part du jour courant sur les heures ---
                    int hoursToday = curHour + 1;
                    long deltaPerHour = deltaPerDay / hoursToday;
                    for (int h = 0; h <= curHour; h++) {
                        char hourKey[3];
                        snprintf(hourKey, sizeof(hourKey), "%02d", h);
                        hist.hours.graph[PsString(hourKey)].attributes[attrId] = deltaPerHour;
                    }
                    hist.hours.trend.attributes[attrId] = deltaPerHour;
                    hist.hours.last.attributes[attrId] = value;

                    // --- MONTHLY : distribuer si gap multi-mois ---
                    int gapMonths = 1;
                    if (lastYear > 0 && lastMonth > 0) {
                        gapMonths = (curYear - lastYear) * 12 + (curMonth - lastMonth);
                        if (gapMonths < 1) gapMonths = 1;
                    }
                    long deltaPerMonth = totalDelta / gapMonths;
                    hist.months.graph[PsString(Month)].attributes[attrId] = deltaPerMonth;
                    hist.months.trend.attributes[attrId] = deltaPerMonth;
                    hist.months.last.attributes[attrId] = value;

                    // --- YEARLY ---
                    hist.years.graph[PsString(Year)].attributes[attrId] += totalDelta;
                    hist.years.trend.attributes[attrId] = totalDelta;
                    hist.years.last.attributes[attrId] = value;

                    hist.lastUpdate = FormattedDate;
                    return true;
                }
            }

            // --- Fonctionnement normal (pas de spike) ---

            // hour
            if (hist.hours.last.attributes[attrId]!=0)
            {
              signed int result;
              int tmpHour = (atoi(Hour) - 1);
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
                hist.hours.graph[PsString(Hour)].attributes[attrId] = result;
              }
              hist.hours.trend.attributes[attrId] = result;
            }
            hist.hours.last.attributes[attrId] = value;

            // day
            if (hist.days.last.attributes[attrId]!=0)
            {
              signed int result;
              int tmpDay = atoi(Yesterday);
              String daytmp = tmpDay < 10 ? "0" + String(tmpDay) : String(tmpDay);
              int tmp = hist.days.last.attributes[attrId];

              if (hist.days.data[PsString(daytmp.c_str())].attributes[attrId]==0)
              {
                hist.days.data[PsString(daytmp.c_str())].attributes[attrId] = hist.days.last.attributes[attrId];
              }
              result = tmp - hist.days.data[PsString(daytmp.c_str())].attributes[attrId];

              if (hist.days.data[PsString(daytmp.c_str())].attributes[attrId]!=0)
              {
                hist.days.graph[PsString(Day)].attributes[attrId] = result;
              }
              hist.days.trend.attributes[attrId] = result;
            }
            hist.days.last.attributes[attrId] = value;

            // month
            if (hist.months.last.attributes[attrId]!=0)
            {
              signed int result;

              int tmpMonth = (atoi(Month) - 1);
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
                hist.months.graph[PsString(Month)].attributes[attrId] = result;
              }
              hist.months.trend.attributes[attrId] = result;
            }
            hist.months.last.attributes[attrId] = value;

            // year
            if (hist.years.last.attributes[attrId]!=0)
            {

              signed int result;
              int tmpYear = (atoi(Year) - 1);
              String yeartmp = tmpYear < 10 ? "0" + String(tmpYear) : String(tmpYear);
              int tmp = hist.years.last.attributes[attrId];
              if (hist.years.data[PsString(yeartmp.c_str())].attributes[attrId]==0)
              {
                hist.years.data[PsString(yeartmp.c_str())].attributes[attrId] = hist.years.last.attributes[attrId];
              }

              result = tmp - hist.years.data[PsString(yeartmp.c_str())].attributes[attrId];
              if (hist.years.data[PsString(yeartmp.c_str())].attributes[attrId]!=0)
              {
                hist.years.graph[PsString(Year)].attributes[attrId] = result;
              }

              hist.years.trend.attributes[attrId] = result;
            }
            hist.years.last.attributes[attrId] = value;

            hist.lastUpdate = FormattedDate;
          }
    }

    return true;
}