#ifndef TEMPLATE_CACHE_H
#define TEMPLATE_CACHE_H

#include <Arduino.h>
#include <map>
#include "FS.h"
#include "LittleFS.h"
#include "PsramAllocator.h"
#include "TemplateData.h"

using PsString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;

class TemplateCache {
private:
    struct Template {
        char* jsonData;           // JSON brut en PSRAM
        size_t jsonSize;
        std::map<String, TemplateData*> parsedDataByModel; // Multiple parsedData par modèle
        bool loaded;
        
        Template() : jsonData(nullptr), jsonSize(0), loaded(false) {}
        
        ~Template() {
            if (jsonData) free(jsonData);
            for (auto& pair : parsedDataByModel) {
                if (pair.second) delete pair.second;
            }
        }
    };
    
    using TemplateMap = std::map<
        PsString, 
        Template*,
        std::less<PsString>,
        PsramAllocator<std::pair<const PsString, Template*>>
    >;
    
    TemplateMap templates;
    char* cachedJson;
    size_t cachedJsonSize;
    bool isDirty;
    bool lazyMode;
    
    char* allocPSRAM(size_t size) {
        char* ptr = (char*)ps_malloc(size);
        if (!ptr) {
            Serial.printf("WARN: PSRAM allocation failed (%d bytes), using heap\n", size);
            ptr = (char*)malloc(size);
        }
        return ptr;
    }
    
    bool loadTemplate(const PsString& name, Template* tpl) {
        if (tpl->loaded) return true;
        
        String path = "/tp/" + String(name.c_str());
        File file = LittleFS.open(path, FILE_READ);
        
        if (!file || file.isDirectory()) {
            Serial.printf("Template introuvable: %s\n", path.c_str());
            return false;
        }
        
        size_t size = file.size();
        if (size == 0) {
            file.close();
            return false;
        }
        
        if (tpl->jsonData) free(tpl->jsonData);
        
        tpl->jsonData = allocPSRAM(size + 1);
        if (!tpl->jsonData) {
            file.close();
            return false;
        }
        
        tpl->jsonSize = file.readBytes(tpl->jsonData, size);
        tpl->jsonData[tpl->jsonSize] = '\0';
        tpl->loaded = true;
        file.close();
        
        isDirty = true;
        return true;
    }
    
    void rebuildCache() {
        if (!isDirty && cachedJson) return;
        
        if (lazyMode) {
            for (auto& pair : templates) {
                if (!pair.second->loaded) {
                    loadTemplate(pair.first, pair.second);
                }
            }
        }
        
        size_t totalSize = 2;
        for (auto& pair : templates) {
            if (pair.second->loaded) {
                totalSize += pair.first.length() + 5;
                totalSize += pair.second->jsonSize;
                totalSize += 1;
            }
        }
        
        if (cachedJson) free(cachedJson);
        
        cachedJson = allocPSRAM(totalSize + 1);
        if (!cachedJson) return;
        
        char* ptr = cachedJson;
        *ptr++ = '{';
        
        bool first = true;
        for (auto& pair : templates) {
            if (!pair.second->loaded) continue;
            
            if (!first) *ptr++ = ',';
            first = false;
            
            *ptr++ = '"';
            memcpy(ptr, pair.first.c_str(), pair.first.length());
            ptr += pair.first.length();
            *ptr++ = '"';
            *ptr++ = ':';
            
            memcpy(ptr, pair.second->jsonData, pair.second->jsonSize);
            ptr += pair.second->jsonSize;
        }
        *ptr++ = '}';
        *ptr = '\0';
        
        cachedJsonSize = ptr - cachedJson;
        isDirty = false;
    }

public:
    TemplateCache(bool lazy = true) 
        : cachedJson(nullptr), cachedJsonSize(0), isDirty(true), lazyMode(lazy) {}
    
    ~TemplateCache() {
        clear();
    }
    
    bool indexTemplates() {
        clear();
        
        File root = LittleFS.open("/tp");
        if (!root) {
            Serial.println("Erreur ouverture /tp");
            return false;
        }
        
        File file = root.openNextFile();
        int count = 0;
        
        while (file) {
            if (!file.isDirectory()) {
                String name = file.name();
                PsString psName(name.c_str(), PsramAllocator<char>());
                
                Template* tpl = new(ps_malloc(sizeof(Template))) Template();
                
                if (!lazyMode) {
                    size_t size = file.size();
                    if (size > 0) {
                        tpl->jsonData = allocPSRAM(size + 1);
                        if (tpl->jsonData) {
                            tpl->jsonSize = file.readBytes(tpl->jsonData, size);
                            tpl->jsonData[tpl->jsonSize] = '\0';
                            tpl->loaded = true;
                            count++;
                        }
                    }
                } else {
                    tpl->loaded = false;
                    count++;
                }
                
                templates[psName] = tpl;
            }
            
            file.close();
            file = root.openNextFile();
            vTaskDelay(1);
        }
        
        root.close();
        isDirty = true;
        
        Serial.printf("Templates indexés: %d (%s)\n", count, lazyMode ? "lazy" : "all loaded");
        return true;
    }
    
    void loadAll() {
        for (auto& pair : templates) {
            if (!pair.second->loaded) {
                loadTemplate(pair.first, pair.second);
            }
        }
    }
    
    const char* getJson() {
        rebuildCache();
        return cachedJson;
    }
    
    size_t getJsonSize() {
        rebuildCache();
        return cachedJsonSize;
    }
    
    // Obtenir le JSON brut
    const char* getJsonRaw(const String& name) {
        PsString psName(name.c_str(), PsramAllocator<char>());
        auto it = templates.find(psName);
        
        if (it == templates.end()) return nullptr;
        
        if (!it->second->loaded) {
            if (!loadTemplate(it->first, it->second)) {
                return nullptr;
            }
        }
        
        return it->second->jsonData;
    }
    
    // MODIFIÉ: Obtenir TemplateData* parsé avec support du modelName
    TemplateData* get(const String& name, const String& modelName = "") {
        PsString psName(name.c_str(), PsramAllocator<char>());
        auto it = templates.find(psName);
        
        if (it == templates.end()) return nullptr;
        
        // Charger le JSON si pas encore fait
        if (!it->second->loaded) {
            if (!loadTemplate(it->first, it->second)) {
                return nullptr;
            }
        }
        
        // Vérifier si ce modèle est déjà parsé
        auto parsedIt = it->second->parsedDataByModel.find(modelName);
        if (parsedIt != it->second->parsedDataByModel.end()) {
            return parsedIt->second; // Déjà en cache
        }
        
        // Parser pour ce modèle spécifique
        void* mem = ps_malloc(sizeof(TemplateData));
        if (!mem) mem = malloc(sizeof(TemplateData));
        TemplateData* parsedData = new (mem) TemplateData();
        if (!parsedData->parseFromJson(it->second->jsonData, modelName)) {
            delete parsedData;
            return nullptr;
        }
        
        // Mettre en cache
        it->second->parsedDataByModel[modelName] = parsedData;
        
        return parsedData;
    }
    
    // Version optimisée pour device - DEPRECATED, utiliser get() avec modelName
    TemplateData* getForModel(const char* modelId) {
        if (!modelId) return nullptr;
        
        char filename[64];
        snprintf(filename, sizeof(filename), "%s.json", modelId);
        
        // Appeler get() sans modelName (comportement par défaut)
        return get(String(filename), "");
    }
    
    bool exists(const String& name) {
        PsString psName(name.c_str(), PsramAllocator<char>());
        return templates.find(psName) != templates.end();
    }
    
    bool reload(const String& name) {
        Serial.printf("TemplateCache::reload('%s')\n", name.c_str());
        
        PsString psName(name.c_str(), PsramAllocator<char>());
        auto it = templates.find(psName);
        
        if (it == templates.end()) {
            // Template pas encore indexé, on le crée
            Serial.println("  -> Nouveau template, création entrée");
            Template* tpl = new(ps_malloc(sizeof(Template))) Template();
            templates[psName] = tpl;
            it = templates.find(psName);
        } else {
            Serial.printf("  -> Template existant, %d parsedData en cache\n", 
                        it->second->parsedDataByModel.size());
            
            // Supprimer tous les parsedData (ils ont des pointeurs vers l'ancien JSON)
            for (auto& pair : it->second->parsedDataByModel) {
                if (pair.second) delete pair.second;
            }
            it->second->parsedDataByModel.clear();
            
            // Libérer l'ancien JSON
            if (it->second->jsonData) {
                free(it->second->jsonData);
                it->second->jsonData = nullptr;
            }
            it->second->jsonSize = 0;
            it->second->loaded = false;  // *** IMPORTANT: Forcer le rechargement ***
        }
        
        // Recharger depuis le fichier
        bool result = loadTemplate(it->first, it->second);
        
        // Marquer le cache JSON global comme dirty
        isDirty = true;
        
        Serial.printf("  -> Rechargement: %s (size: %u bytes)\n", 
                    result ? "OK" : "ECHEC", 
                    it->second->jsonSize);
        
        return result;
    }
    
    void clear() {
        for (auto& pair : templates) {
            delete pair.second;
        }
        templates.clear();
        
        if (cachedJson) {
            free(cachedJson);
            cachedJson = nullptr;
        }
        cachedJsonSize = 0;
        isDirty = true;
    }
    
    size_t count() const { 
        return templates.size(); 
    }
    
    size_t countLoaded() const {
        size_t n = 0;
        for (auto& pair : templates) {
            if (pair.second->loaded) n++;
        }
        return n;
    }
    
    size_t getMemoryUsed() const {
        size_t total = cachedJsonSize;
        for (auto& pair : templates) {
            if (pair.second->loaded) {
                total += pair.second->jsonSize;
            }
            // Compter tous les parsedData
            for (auto& modelPair : pair.second->parsedDataByModel) {
                if (modelPair.second) {
                    total += sizeof(TemplateData);
                    total += modelPair.second->StateSize() * sizeof(State);
                    total += modelPair.second->ActionSize() * sizeof(Action);
                }
            }
        }
        return total;
    }
    
    void printStats() const {
        Serial.println("=== TEMPLATE CACHE STATS ===");
        Serial.printf("Mode: %s\n", lazyMode ? "Lazy Loading" : "All Loaded");
        Serial.printf("Templates indexés: %u\n", count());
        Serial.printf("Templates chargés: %u\n", countLoaded());
        Serial.printf("Mémoire PSRAM: %u bytes\n", getMemoryUsed());
        Serial.printf("PSRAM libre: %u bytes\n", ESP.getFreePsram());
        Serial.printf("RAM libre: %u bytes\n", ESP.getFreeHeap());
    }
};

#endif