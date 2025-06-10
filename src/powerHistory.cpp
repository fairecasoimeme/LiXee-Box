#include <Arduino.h>
#include <ArduinoJson.h>
#include "FS.h"
#include "SPIFFS_ini.h"
#include "LittleFS.h"
#include "config.h"
#include "powerHistory.h"

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef MAX  
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

#define ARDUINOJSON_USE_LONG_LONG 1
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#define ARDUINOJSON_ENABLE_STD_STRING 1

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

String toJson(const PowerHistory& history, const String& nowHM = "")
{
    /*----------- 1. Estimation de la taille nécessaire ------------------*/
    // Estimation approximative basée sur le nombre d'enregistrements
    size_t estimatedSize = 1024; // Base
    estimatedSize += history.datas.size() * 200; // ~200 bytes par enregistrement
    estimatedSize += history.stats.size() * 150; // ~150 bytes par stat
    
    // Minimum 64KB, maximum 512KB pour éviter les allocations excessives
    if (estimatedSize < 65536UL) estimatedSize = 65536UL;
    if (estimatedSize > 524288UL) estimatedSize = 524288UL;

    /*----------- 2. Collecte + tri chronologique (optimisé) -------------*/
    std::vector<const DataRecord*> sorted;
    sorted.reserve(history.datas.size());
    
    for (const DataRecord& rec : history.datas)
    {
        // Éviter la conversion String inutile
        if (!rec.timeStamp.empty())
            sorted.push_back(&rec);
    }

    std::sort(sorted.begin(), sorted.end(),
              [](const DataRecord* a, const DataRecord* b)
              { return a->timeStamp < b->timeStamp; });

    /*----------- 3. Rotation pour que 'nowHM' arrive en dernier ----------*/
    std::vector<const DataRecord*> pivoted;
    pivoted.reserve(sorted.size());

    if (!nowHM.isEmpty() && !sorted.empty())
    {
        size_t idx = 0;
        String nowHMStr = nowHM; // Conversion une seule fois
        
        while (idx < sorted.size() &&
               String(sorted[idx]->timeStamp.c_str()) != nowHMStr) ++idx;

        if (idx < sorted.size()) idx = (idx + 1) % sorted.size();

        for (size_t k = 0; k < sorted.size(); ++k)
            pivoted.push_back(sorted[(idx + k) % sorted.size()]);
    }
    else
    {
        pivoted = std::move(sorted);
    }

    /*----------- 4. Construction du document JSON (avec retry) -----------*/
    SpiRamJsonDocument* doc = nullptr;
    
    // Tentative avec plusieurs tailles si la première échoue
    for (int attempt = 0; attempt < 3; attempt++)
    {
        try 
        {
            doc = new SpiRamJsonDocument(estimatedSize);
            
            // Vérification simple de l'allocation (v6 n'a pas capacity())
            JsonObject testObj = doc->to<JsonObject>();
            if (testObj.isNull())
            {
                log_i("Échec allocation JSON, taille: %zu\n", estimatedSize);
                delete doc;
                doc = nullptr;
                estimatedSize *= 2;
                continue;
            }
            
            // Clear le document pour repartir proprement
            doc->clear();
            break; // Allocation réussie
        }
        catch (...)
        {
            log_i("Exception lors allocation JSON, taille: %zu\n", estimatedSize);
            if (doc) {
                delete doc;
                doc = nullptr;
            }
            estimatedSize *= 2;
            if (attempt == 2) return "{}"; // Échec définitif
        }
    }

    if (!doc) return "{}";

    JsonObject root = doc->to<JsonObject>();
    if (root.isNull())
    {
        DEBUG_PRINTLN("Échec création objet racine JSON");
        delete doc;
        return "{}";
    }

    /*----------- 5. Construction du tableau "datas" avec vérifications ---*/
    JsonArray arr = root.createNestedArray("datas");
    if (arr.isNull())
    {
        DEBUG_PRINTLN("Échec création array datas");
        delete doc;
        return "{}";
    }

    size_t addedRecords = 0;
    size_t memoryThreshold = (estimatedSize * 9) / 10; // 90% de la taille estimée
    
    for (const DataRecord* prec : pivoted)
    {
        // Vérification de la mémoire disponible (v6 utilise memoryUsage())
        if (doc->memoryUsage() > memoryThreshold)
        {
            log_i("Mémoire JSON presque pleine (%zu bytes), arrêt à %zu enregistrements\n", 
                         doc->memoryUsage(), addedRecords);
            break;
        }

        const DataRecord& rec = *prec;
        JsonObject row = arr.createNestedObject();
        
        if (row.isNull())
        {
            log_i("Échec création objet ligne %zu\n", addedRecords);
            break;
        }

        row["y"] = rec.timeStamp;

        // Ajout des valeurs dynamiques avec vérification
        for (const auto& kv : rec.values)
        {
            // ArduinoJSON v6 - conversion de la clé en String si nécessaire
            String key = String(kv.first);
            if (!row.containsKey(key))
            {
                row[key] = kv.second;
            }
        }
        
        addedRecords++;
        
        // Vérification périodique pour éviter la surcharge
        if (addedRecords % 50 == 0)
        {
            if (doc->overflowed())
            {
                log_i("Document JSON débordé à %zu enregistrements\n", addedRecords);
                break;
            }
        }
    }

    log_i("Ajouté %zu/%zu enregistrements\n", addedRecords, pivoted.size());

    /*----------- 6. Construction de l'objet "stats" ----------------------*/
    JsonObject statsObj = root.createNestedObject("stats");
    if (!statsObj.isNull() && !doc->overflowed())
    {
        size_t statsThreshold = (estimatedSize * 19) / 20; // 95% de la taille estimée
        
        for (const auto& kv : history.stats)
        {
            if (doc->memoryUsage() > statsThreshold || doc->overflowed())
            {
                DEBUG_PRINTLN("Mémoire JSON pleine, stats tronquées");
                break;
            }

            String statKey = String(kv.first);
            JsonObject attr = statsObj.createNestedObject(statKey);
            if (!attr.isNull())
            {
                attr["min"]   = kv.second.min;
                attr["max"]   = kv.second.max;
                attr["trend"] = kv.second.trend;
                attr["last"]  = kv.second.last;
            }
        }
    }

    /*----------- 7. Vérification finale avant sérialisation -------------*/
    if (doc->overflowed())
    {
        DEBUG_PRINTLN("Attention: Document JSON a débordé pendant la construction");
        // On peut continuer, ArduinoJSON v6 gère les débordements proprement
    }

    /*----------- 8. Sérialisation avec gestion d'erreur -----------------*/
    String out;
    
    // Estimation de la taille de sortie
    size_t jsonSize = measureJson(*doc);
    if (jsonSize == 0)
    {
        DEBUG_PRINTLN("Échec mesure JSON");
        delete doc;
        return "{}";
    }
    
    // Pre-réservation de la chaîne de sortie
    out.reserve(jsonSize + 100);
    
    size_t serializedSize = serializeJson(*doc, out);
    
    if (serializedSize == 0)
    {
        DEBUG_PRINTLN("Échec sérialisation JSON");
        delete doc;
        return "{}";
    }

    log_i("JSON généré: %zu bytes (mesuré: %zu), mémoire doc: %zu bytes\n", 
                  serializedSize, jsonSize, doc->memoryUsage());

    // Vérification de la validité du JSON généré
    if (out.length() < 10 || !out.startsWith("{") || !out.endsWith("}"))
    {
        Serial.println("JSON généré semble corrompu");
        delete doc; // Libération mémoire
        return "{}";
    }

    delete doc; // Libération mémoire
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
