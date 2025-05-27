#include <Arduino.h>
#include <ArduinoJson.h>
#include "FS.h"
#include "SPIFFS_ini.h"
#include "LittleFS.h"
#include "config.h"
#include "powerHistory.h"

extern String Hour;
extern String Minute;

bool parsePowerHistory(const String IEEE, PowerHistory &history) {

    String path="/hst/pwr_"+IEEE+".json";

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
        if (root.isNull()) return false;

        // 1) Parser le tableau "datas"
        JsonArray datasArr = root["datas"].as<JsonArray>();
        if (!datasArr.isNull()) {
            for (JsonObject row : datasArr) {
                DataRecord rec;

                rec.timeStamp = row["y"].as<String>().c_str();
                // Pour chaque clé de row (ex: "2575","2319","1295"), sauf "y"
                if (!rec.timeStamp.empty())
                {
                    for (auto kv : row) {
                        String keyStr = kv.key().c_str();
                        if (keyStr == "y") continue; // c'est déjà géré
    
                        long val = kv.value().as<long>(); 
                        int attrId = keyStr.toInt(); // ex: "1295" → 1295
                        rec.values[attrId] = val;
                    }
                    history.datas.push_back(rec);
                }
                
            }
        }
        
        // 2) Lire le tableau "stats"
        //    Dans le JSON, "stats" est un array, dont le premier élément est un objet
        //    qui contient les stats par attribut : {"2575": {...}, "1295": {...}, ...}
        JsonArray statsArr = root["stats"].as<JsonArray>();
        if (!statsArr.isNull()  && !statsArr.isNull()) {
            // On suppose qu'il n'y a qu'un élément
            JsonObject statsObj = statsArr[0].as<JsonObject>();
            if (!statsObj.isNull()) {
                // Vider l'ancien contenu
                history.stats.clear();

                // Pour chaque attribut (ex: "2575", "2319", "1295", ...)
                for (auto kv : statsObj) {
                    const char* attrKey = kv.key().c_str(); // ex: "1295"
                    int attrId = atoi(attrKey);

                    JsonObject stObj = kv.value().as<JsonObject>();
                    if (stObj.isNull()) {
                        continue; // si jamais c'est pas un objet
                    }

                    // Extraire min, max, trend, last
                    AttributeStats st;
                    st.min   = strtol(stObj["min"]   | "0", nullptr, 10);
                    st.max   = strtol(stObj["max"]   | "0", nullptr, 10);
                    st.trend = strtol(stObj["trend"] | "0", nullptr, 10);
                    st.last  = strtol(stObj["last"]  | "0", nullptr, 10);

                    history.stats[attrId] = st;
                }
            }
        }

        return true;
    }
    return false;
}


static inline uint16_t tsToMinutes(const String &ts)
{
    // On suppose toujours le format "NN:NN"
    return 60 * ((ts[0] - '0') * 10 + (ts[1] - '0'))     // heures
         +       ((ts[3] - '0') * 10 + (ts[4] - '0'));   // minutes
}

String toJson(const PowerHistory&  history,   const String&  nowHM = "")
{
    /*----------- 1.  Collecte + tri chronologique ---------------------------*/
    std::vector<const DataRecord*> sorted;
    sorted.reserve(history.datas.size());
    for (const DataRecord& rec : history.datas)
    {
        if (!String(rec.timeStamp.c_str()).isEmpty())          // ← ignore les enregistrements sans horodatage
            sorted.push_back(&rec);
    }

    std::sort(sorted.begin(), sorted.end(),
              [](const DataRecord* a, const DataRecord* b)
              { return a->timeStamp < b->timeStamp; });   // "00:00" → "23:59"

    /*----------- 2.  Rotation pour que ‘nowHM’ arrive en dernier -------------*/
    std::vector<const DataRecord*> pivoted;
    pivoted.reserve(sorted.size());

    if (!nowHM.isEmpty() && !sorted.empty())
    {
        // Cherche le premier enregistrement dont timeStamp == nowHM
        size_t idx = 0;
        while (idx < sorted.size() &&
               String(sorted[idx]->timeStamp.c_str()) != nowHM) ++idx;

        // Si trouvé → pivot = idx + 1   (on veut nowHM en **dernier**)
        // Sinon aucun pivot : on gardera simplement le tri naturel
        if (idx < sorted.size())  idx = (idx + 1) % sorted.size();

        // Construit le vector pivoté
        for (size_t k = 0; k < sorted.size(); ++k)
            pivoted.push_back(sorted[(idx + k) % sorted.size()]);
    }
    else
    {
        pivoted = std::move(sorted);             // pas de rotation
    }

    /*----------- 3.  Construction du document JSON --------------------------*/
    SpiRamJsonDocument doc(100000);
    JsonObject root = doc.to<JsonObject>();

    /* 3‑a)  tableau “datas” */
    JsonArray arr = root.createNestedArray("datas");
    for (const DataRecord* prec : pivoted)
    {
        const DataRecord& rec = *prec;
        JsonObject row = arr.createNestedObject();
        row["y"] = rec.timeStamp;

        for (auto& kv : rec.values)               // attributs dynamiques
            row[String(kv.first)] = kv.second;
    }

    /* 3‑b)  objet “stats” (inchangé) */
    JsonObject statsObj = root.createNestedObject("stats");
    for (auto& kv : history.stats)
    {
        JsonObject attr = statsObj.createNestedObject(String(kv.first));
        attr["min"]   = kv.second.min;
        attr["max"]   = kv.second.max;
        attr["trend"] = kv.second.trend;
        attr["last"]  = kv.second.last;
    }

    /*----------- 4.  Sérialisation ------------------------------------------*/
    String out;
    if (serializeJson(doc, out) == 0)
        return "{}";

    return out;

}

bool savePowerHistory(String IEEE, const PowerHistory &history) {
    
    SpiRamJsonDocument doc(MAXHEAP);
    JsonObject root = doc.to<JsonObject>();

    // 1) datas: un tableau
    JsonArray arr = root.createNestedArray("datas");
    for (auto &rec : history.datas) {
        JsonObject row = arr.createNestedObject();
        row["y"] = rec.timeStamp;
        // Pour chaque attribut
        for (auto &kv : rec.values) {
            // kv.first = attributId, kv.second = valeur
            String key = String(kv.first); 
            row[key] = kv.second;
        }
    }

    // 2) stats
    // Pour chaque attribut connu dans history.stats
    JsonArray statsArr = root.createNestedArray("stats");
    JsonObject statsObj = statsArr.createNestedObject();
    for (auto &kv : history.stats) {
        int attrId = kv.first;
        const AttributeStats &as = kv.second;

        String attrStr = String(attrId);

        JsonObject attrObj  = statsObj.createNestedObject(attrStr);

        attrObj["min"]   = String(as.min);
        attrObj["max"]   = String(as.max);
        attrObj["trend"] = String(as.trend);
        attrObj["last"]  = String(as.last);
    }

    String path= "/hst/pwr_"+IEEE+".json";

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

void addMeasurement(PowerHistory &history, int attrId,long newValue) {
    // 1) Chercher s'il existe déjà un DataRecord pour hourMinute
    DataRecord *found = nullptr;
    PsString hourMinute;
    for (auto &rec : history.datas) {
        String tmp = Hour+":"+Minute;
        hourMinute = PsString(tmp.c_str());
        if (rec.timeStamp == hourMinute) {
            found = &rec;
            break;
        }
    }

    // 2) Si pas trouvé, on crée un nouveau DataRecord dans le vecteur
    
    if (!found) {
        DataRecord newRec;
        // On suppose que PsString accepte une affectation depuis un const char* ou un String
        newRec.timeStamp = hourMinute;
        // Dans ses values, on affecte la paire (attrId -> newValue)
        newRec.values[attrId] = newValue;

        // On l'ajoute à l'historique
        history.datas.push_back(std::move(newRec));

        // found pointe sur le tout nouveau record (back de la vector)
        found = &history.datas.back();
    }
    else {
        // 3) Si on a trouvé un record existant, on met simplement à jour la valeur
        found->values[attrId] = newValue;
    }

    // 4) Mettre à jour les stats => history.stats[attrId]
    AttributeStats &st = history.stats[attrId];  // insère si besoin

    // Si c'est la première fois qu'on enregistre un relevé pour cet attribut
    // (ex: st.last == 0 et st.min ==0 et st.max==0 => "non-initialisé")
    if (st.min == 0 && st.max == 0 && st.last == 0) {
        st.min   = newValue;
        st.max   = newValue;
        st.last  = newValue;
        st.trend = 0; // aucune tendance si c'est la toute première valeur
    }
    else {
        // sinon on met à jour
        if (newValue < st.min) st.min = newValue;
        if (newValue > st.max) st.max = newValue;

        // Par exemple, la tendance = (nouvelleValeur - ancienneValeur)
        st.trend = newValue - st.last;
        st.last  = newValue;
    }
}
