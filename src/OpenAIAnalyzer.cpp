/**
 * OpenAIAnalyzer.cpp
 * Implémentation avec allocation PSRAM exclusive
 */

#include "OpenAIAnalyzer.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// Endpoint OpenAI
static const char* OPENAI_ENDPOINT = "https://api.openai.com/v1/chat/completions";

// Certificat racine pour OpenAI (DigiCert Global Root CA)
// Valide jusqu'en 2031
static const char* OPENAI_ROOT_CA = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh\n" \
"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n" \
"d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD\n" \
"QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT\n" \
"MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n" \
"b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG\n" \
"9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB\n" \
"CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97\n" \
"nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt\n" \
"43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P\n" \
"T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4\n" \
"gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO\n" \
"BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR\n" \
"TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw\n" \
"DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr\n" \
"hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg\n" \
"06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF\n" \
"PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls\n" \
"YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk\n" \
"CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=\n" \
"-----END CERTIFICATE-----\n";

// ========== CONSTRUCTEUR / DESTRUCTEUR ==========

OpenAIAnalyzer::OpenAIAnalyzer() : 
    client_(nullptr),
    total_requests_(0),
    failed_requests_(0),
    total_tokens_used_(0),
    total_cost_usd_(0.0f) {
}

OpenAIAnalyzer::~OpenAIAnalyzer() {
    if (client_) {
        freePSRAM(client_);
        client_ = nullptr;
    }
}

// ========== INITIALISATION ==========

bool OpenAIAnalyzer::begin(const OpenAIConfig& config) {
    config_ = config;
    
    if (!config_.api_key || strlen(config_.api_key) == 0) {
        Serial.println("[AI] Erreur: clé API manquante");
        return false;
    }
    
    // Vérification PSRAM disponible
    if (!checkPSRAMAvailable(8192)) {
        Serial.println("[AI] Erreur: PSRAM insuffisante");
        return false;
    }
    
    // Allocation du client WiFi en PSRAM
    client_ = (WiFiClientSecure*)allocPSRAM(sizeof(WiFiClientSecure));
    if (!client_) {
        Serial.println("[AI] Erreur: allocation client PSRAM");
        return false;
    }
    
    // Construction in-place
    new (client_) WiFiClientSecure();
    
    // Mode non sécurisé (simple et rapide)
    // Pour production, activez le certificat + NTP (voir plus bas)
    client_->setInsecure();
    
    Serial.println("[AI] Initialisé avec succès");
    Serial.println("[AI] SSL: mode non sécurisé (OK pour usage local)");
    printMemoryStats();
    
    return true;
}

// ========== ANALYSE PRINCIPALE ==========

bool OpenAIAnalyzer::analyzeSubscription(const LinkyData& data, AIAnalysisResult& result) {
    uint32_t start_time = millis();
    result.success = false;
    result.response_text = nullptr;
    result.response_length = 0;
    
    total_requests_++;
    
    Serial.println("[AI] Démarrage analyse...");
    
    // Construction du prompt
    size_t prompt_size = 0;
    char* prompt = buildPrompt(data, config_.use_compact_mode, prompt_size);
    if (!prompt) {
        Serial.println("[AI] Erreur: allocation prompt");
        failed_requests_++;
        return false;
    }
    
    Serial.printf("[AI] Prompt généré: %d bytes\n", prompt_size);
    
    // Construction requête JSON
    size_t json_size = 0;
    char* json_body = buildJSONRequest(prompt, json_size);
    freePSRAM(prompt); // Libération prompt
    
    if (!json_body) {
        Serial.println("[AI] Erreur: allocation JSON");
        failed_requests_++;
        return false;
    }
    
    Serial.printf("[AI] JSON généré: %d bytes\n", json_size);
    
    // Envoi requête
    bool success = sendRequest(json_body, json_size, result);
    freePSRAM(json_body); // Libération JSON
    
    result.duration_ms = millis() - start_time;
    
    if (success) {
        Serial.printf("[AI] Analyse terminée en %d ms\n", result.duration_ms);
        total_tokens_used_ += result.total_tokens;
        total_cost_usd_ += result.estimated_cost_usd;
    } else {
        Serial.println("[AI] Échec analyse");
        failed_requests_++;
    }
    
    printMemoryStats();
    
    return success;
}

bool OpenAIAnalyzer::analyzeSubscriptionCompact(const LinkyData& data, AIAnalysisResult& result) {
    // Forcer le mode compact
    bool old_mode = config_.use_compact_mode;
    config_.use_compact_mode = true;
    
    bool success = analyzeSubscription(data, result);
    
    config_.use_compact_mode = old_mode;
    return success;
}

// ========== CONSTRUCTION PROMPT ==========

char* OpenAIAnalyzer::buildPrompt(const LinkyData& data, bool compact_mode, size_t& prompt_size) {
    char* buffer = nullptr;
    
    if (compact_mode) {
        // Mode compact: ~300 caractères
        buffer = (char*)allocPSRAM(512);
        if (!buffer) return nullptr;
        
        int written = snprintf(buffer, 512,
            "Analyse abonnement électrique français:\n"
            "Conso %dj: HC=%dkWh HP=%dkWh (total=%dkWh)\n"
            "Pmax observée: %dW | Abonnement: %dkVA (%dW)\n"
            "Ratio HC/HP: %.1f%%\n"
            "Taux utilisation: %.1f%%\n"
            "Verdict + 2 conseils concrets (100 mots max)",
            data.nb_jours,
            data.index_hc / 1000,
            data.index_hp / 1000,
            data.conso_totale_kwh,
            data.puissance_max_totale,
            data.puissance_souscrite,
            data.puissance_souscrite * 1000,
            data.ratio_hc_hp,
            data.taux_utilisation_abo
        );
        
        if (written < 0 || written >= 512) {
            Serial.println("[AI] Erreur: overflow prompt compact");
            freePSRAM(buffer);
            return nullptr;
        }
    } else {
        // Mode complet: ~800 caractères
        buffer = (char*)allocPSRAM(1536);
        if (!buffer) return nullptr;
        
        // Construction par parties pour éviter overflow
        char horaires_line[64] = "";
        if (data.horaires_hc[0] != '\0') {
            snprintf(horaires_line, sizeof(horaires_line), "- Horaires HC: %s\n", data.horaires_hc);
        }
        
        int written = snprintf(buffer, 1536,
            "Tu es un expert en analyse énergétique. Analyse ces données de consommation "
            "électrique d'un compteur Linky français:\n\n"
            "DONNÉES SUR %d JOURS:\n"
            "- Type abonnement: %s\n"
            "- Index Heures Creuses: %u Wh (%u kWh)\n"
            "- Index Heures Pleines: %u Wh (%u kWh)\n"
            "- Consommation totale: %u kWh\n"
            "- Ratio HC/HP: %.1f%% / %.1f%%\n"
            "%s"
            "- Puissance max observée: %u W (%.2f kVA)\n"
            "- Puissance souscrite: %u kVA (%u W)\n"
            "- Taux d'utilisation: %.1f%%\n\n"
            "ANALYSE DEMANDÉE:\n"
            "1. L'abonnement %ukVA est-il adapté? Risque de dépassement?\n"
            "2. L'option HP/HC est-elle rentable (ratio actuel %.1f%% HC)?\n"
            "3. Trois recommandations concrètes d'optimisation\n\n"
            "Réponds de manière structurée et concise (250 mots max).",
            data.nb_jours,
            data.type_abonnement,
            data.index_hc, data.index_hc / 1000,
            data.index_hp, data.index_hp / 1000,
            data.conso_totale_kwh,
            data.ratio_hc_hp, 100.0f - data.ratio_hc_hp,
            horaires_line,
            data.puissance_max_totale,
            data.puissance_max_totale / 1000.0f,
            data.puissance_souscrite,
            data.puissance_souscrite * 1000,
            data.taux_utilisation_abo,
            data.puissance_souscrite,
            data.ratio_hc_hp
        );
        
        if (written < 0 || written >= 1536) {
            Serial.println("[AI] Erreur: overflow prompt complet");
            freePSRAM(buffer);
            return nullptr;
        }
    }
    
    prompt_size = strlen(buffer);
    return buffer;
}

// ========== CONSTRUCTION JSON ==========

char* OpenAIAnalyzer::buildJSONRequest(const char* prompt, size_t& json_size) {
    // Utilisation de ArduinoJson avec allocateur PSRAM pour gérer l'échappement
    struct PSRAMAllocator {
        void* allocate(size_t size) {
            return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        }
        void deallocate(void* pointer) {
            heap_caps_free(pointer);
        }
    };
    
    // Document JSON en PSRAM
    BasicJsonDocument<PSRAMAllocator> doc(2048 + strlen(prompt));
    
    doc["model"] = config_.model;
    doc["max_tokens"] = config_.max_tokens;
    doc["temperature"] = config_.temperature;
    
    JsonArray messages = doc.createNestedArray("messages");
    JsonObject message = messages.createNestedObject();
    message["role"] = "user";
    message["content"] = prompt;  // ArduinoJson gère l'échappement automatiquement
    
    // Calcul de la taille nécessaire
    json_size = measureJson(doc);
    
    // Allocation du buffer de sortie en PSRAM
    char* buffer = (char*)allocPSRAM(json_size + 1);
    if (!buffer) {
        Serial.println("[AI] Erreur: allocation buffer JSON");
        return nullptr;
    }
    
    // Sérialisation
    size_t written = serializeJson(doc, buffer, json_size + 1);
    if (written == 0) {
        Serial.println("[AI] Erreur: sérialisation JSON");
        freePSRAM(buffer);
        return nullptr;
    }
    
    json_size = written;
    return buffer;
}


bool OpenAIAnalyzer::analyzeWithFullJSON(
    const char* json_hours, 
    const char* json_minutes,
    const LinkyData& summary_data, 
    AIAnalysisResult& result,
    const FullAnalysisConfig& full_config) {
    
    uint32_t start_time = millis();
    result.success = false;
    result.response_text = nullptr;
    total_requests_++;
    
    Serial.println("[AI] Analyse détaillée avec JSON complet...");
    
    // Calcul taille approximative
    size_t json_hours_size = strlen(json_hours);
    size_t json_minutes_size = strlen(json_minutes);
    size_t total_json_size = json_hours_size + json_minutes_size;
    
    Serial.printf("[AI] JSON Heures: %d bytes\n", json_hours_size);
    Serial.printf("[AI] JSON Minutes: %d bytes\n", json_minutes_size);
    Serial.printf("[AI] Total: %d bytes (~%d tokens estimés)\n", 
        total_json_size, total_json_size / 4);
    
    // Vérification PSRAM
    if (!checkPSRAMAvailable(total_json_size + 8192)) {
        Serial.println("[AI] Erreur: PSRAM insuffisante pour JSON complet");
        failed_requests_++;
        return false;
    }
    
    // Construction du prompt enrichi
    size_t prompt_size = 0;
    char* prompt = buildFullJSONPrompt(json_hours, json_minutes, summary_data, 
                                       full_config, prompt_size);
    if (!prompt) {
        Serial.println("[AI] Erreur: allocation prompt");
        failed_requests_++;
        return false;
    }
    
    Serial.printf("[AI] Prompt généré: %d bytes\n", prompt_size);
    
    // Construction requête JSON
    size_t json_size = 0;
    char* json_body = buildFullJSONRequest(prompt, full_config, json_size);
    freePSRAM(prompt);
    
    if (!json_body) {
        Serial.println("[AI] Erreur: allocation JSON");
        failed_requests_++;
        return false;
    }
    
    Serial.printf("[AI] Requête JSON: %d bytes\n", json_size);
    
    // Envoi requête
    bool success = sendRequest(json_body, json_size, result);
    freePSRAM(json_body);
    
    result.duration_ms = millis() - start_time;
    
    if (success) {
        Serial.printf("[AI] Analyse détaillée terminée en %d ms\n", result.duration_ms);
        total_tokens_used_ += result.total_tokens;
        total_cost_usd_ += result.estimated_cost_usd;
    } else {
        Serial.println("[AI] Échec analyse détaillée");
        failed_requests_++;
    }
    
    printMemoryStats();
    return success;
}

// ========== CONSTRUCTION PROMPT AVEC JSON ==========

char* OpenAIAnalyzer::buildFullJSONPrompt(
    const char* json_hours,
    const char* json_minutes, 
    const LinkyData& summary_data,
    const FullAnalysisConfig& config,
    size_t& prompt_size) {
    
    // Allocation buffer (estimé à 2x la taille des JSON + prompt)
    size_t estimated_size = strlen(json_hours) + strlen(json_minutes) + 2048;
    char* buffer = (char*)allocPSRAM(estimated_size);
    if (!buffer) return nullptr;
    
    // Construction du prompt structuré
    int pos = 0;
    
    pos += snprintf(buffer + pos, estimated_size - pos,
        "Tu es un expert en analyse énergétique spécialisé dans les compteurs Linky français. "
        "Analyse ces données COMPLÈTES de consommation sur %d jours.\n\n"
        "=== RÉSUMÉ GLOBAL ===\n"
        "Type: %s | Puissance souscrite: %d kVA\n"
        "Consommation: HC=%u kWh | HP=%u kWh | Total=%u kWh\n"
        "Puissance max observée: %u W\n"
        "Horaires HC: %s\n\n",
        summary_data.nb_jours,
        summary_data.type_abonnement,
        summary_data.puissance_souscrite,
        summary_data.index_hc / 1000,
        summary_data.index_hp / 1000,
        summary_data.conso_totale_kwh,
        summary_data.puissance_max_totale,
        summary_data.horaires_hc
    );
    
    if (config.include_hourly_data) {
        pos += snprintf(buffer + pos, estimated_size - pos,
            "=== DONNÉES HORAIRES ET JOURNALIÈRES ===\n"
            "%s\n\n",
            json_hours
        );
    }
    
    if (config.include_daily_data) {
        pos += snprintf(buffer + pos, estimated_size - pos,
            "=== DONNÉES MINUTE PAR MINUTE (DÉTAIL) ===\n"
            "%s\n\n",
            json_minutes
        );
    }
    
    pos += snprintf(buffer + pos, estimated_size - pos,
        "=== ANALYSE DEMANDÉE ===\n"
        "1. ABONNEMENT: L'abonnement %dkVA est-il optimal? Risques de dépassement?\n"
        "2. PATTERNS: Identifie les patterns de consommation (heures de pointe, jours atypiques)\n"
        "3. OPTION TARIFAIRE: L'option HP/HC est-elle rentable avec ce profil?\n"
        "4. ANOMALIES: Détecte les anomalies ou consommations inhabituelles\n"
        "5. SAISONNALITÉ: Analyse les variations mensuelles/saisonnières\n"
        "6. OPTIMISATION: 5 recommandations concrètes et chiffrées\n\n"
        "Réponds de manière structurée avec des données chiffrées précises. "
        "Utilise les données détaillées pour des insights pertinents."
    );
    
    if (pos >= estimated_size - 1) {
        Serial.println("[AI] ATTENTION: Prompt tronqué!");
    }
    
    prompt_size = pos;
    return buffer;
}

// ========== CONSTRUCTION REQUÊTE JSON ==========

char* OpenAIAnalyzer::buildFullJSONRequest(
    const char* prompt,
    const FullAnalysisConfig& full_config,
    size_t& json_size) {
    
    // Allocateur PSRAM pour ArduinoJson
    struct PSRAMAllocator {
        void* allocate(size_t size) {
            return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        }
        void deallocate(void* pointer) {
            heap_caps_free(pointer);
        }
    };
    
    // Document JSON en PSRAM (taille augmentée pour JSON volumineux)
    BasicJsonDocument<PSRAMAllocator> doc(strlen(prompt) + 4096);
    
    // Modèle selon config
    doc["model"] = full_config.use_gpt4 ? "gpt-4o" : config_.model;
    doc["max_tokens"] = full_config.max_tokens;
    doc["temperature"] = 0.5;  // Plus précis pour analyse de données
    
    JsonArray messages = doc.createNestedArray("messages");
    
    // Message système pour guider l'analyse
    JsonObject system_msg = messages.createNestedObject();
    system_msg["role"] = "system";
    system_msg["content"] = "Tu es un expert en analyse de données énergétiques. "
                           "Analyse les données JSON fournies avec précision. "
                           "Base tes recommandations sur des calculs concrets.";
    
    // Message utilisateur avec le prompt complet
    JsonObject user_msg = messages.createNestedObject();
    user_msg["role"] = "user";
    user_msg["content"] = prompt;
    
    // Calcul taille
    json_size = measureJson(doc);
    
    // Allocation buffer
    char* buffer = (char*)allocPSRAM(json_size + 1);
    if (!buffer) {
        Serial.println("[AI] Erreur: allocation buffer JSON");
        return nullptr;
    }
    
    // Sérialisation
    size_t written = serializeJson(doc, buffer, json_size + 1);
    if (written == 0) {
        Serial.println("[AI] Erreur: sérialisation JSON");
        freePSRAM(buffer);
        return nullptr;
    }
    
    json_size = written;
    return buffer;
}

// ========== ENVOI REQUÊTE ==========

bool OpenAIAnalyzer::sendRequest(const char* json_body, size_t json_size, AIAnalysisResult& result) {
    if (!client_ || WiFi.status() != WL_CONNECTED) {
        Serial.println("[AI] WiFi non connecté");
        return false;
    }
    
    // Debug: afficher le JSON (optionnel, commentez en production)
    #ifdef DEBUG_AI_JSON
    Serial.println("[AI] === JSON ENVOYÉ ===");
    Serial.println(json_body);
    Serial.println("[AI] ====================");
    #endif
    
    HTTPClient http;
    http.begin(*client_, OPENAI_ENDPOINT);
    http.addHeader("Content-Type", "application/json");
    
    // Préparation header Authorization en PSRAM
    size_t auth_size = strlen(config_.api_key) + 32;
    char* auth_header = (char*)allocPSRAM(auth_size);
    if (!auth_header) return false;
    
    snprintf(auth_header, auth_size, "Bearer %s", config_.api_key);
    http.addHeader("Authorization", auth_header);
    freePSRAM(auth_header);
    
    http.setTimeout(config_.timeout_ms);
    
    Serial.println("[AI] Envoi requête HTTP...");
    Serial.printf("[AI] Taille JSON: %d bytes\n", json_size);
    
    int http_code = http.POST((uint8_t*)json_body, json_size);
    
    Serial.printf("[AI] Code HTTP reçu: %d\n", http_code);
    
    if (http_code != HTTP_CODE_OK) {
        Serial.printf("[AI] Erreur HTTP: %d\n", http_code);
        if (http_code > 0) {
            String error = http.getString();
            Serial.println("[AI] Réponse d'erreur:");
            Serial.println(error);
        } else {
            // Code négatif = erreur de connexion
            Serial.println("[AI] Erreur de connexion réseau");
            Serial.println("[AI] Vérifiez: WiFi, SSL, timeout");
        }
        http.end();
        return false;
    }
    
    // Récupération réponse - getSize() peut retourner -1 si pas de Content-Length
    int content_length = http.getSize();
    Serial.printf("[AI] Content-Length: %d bytes\n", content_length);
    
    char* response_buffer = nullptr;
    size_t bytes_read = 0;
    
    if (content_length > 0 && content_length < 32768) {
        // Méthode 1: Taille connue (Content-Length fourni)
        response_buffer = (char*)allocPSRAM(content_length + 1);
        if (!response_buffer) {
            Serial.println("[AI] Erreur: allocation réponse PSRAM");
            http.end();
            return false;
        }
        
        WiFiClient* stream = http.getStreamPtr();
        bytes_read = stream->readBytes(response_buffer, content_length);
        response_buffer[bytes_read] = '\0';
        
        Serial.printf("[AI] Lu: %d bytes\n", bytes_read);
        
    } else {
        // Méthode 2: Taille inconnue ou invalide - utiliser getString() puis copier
        Serial.println("[AI] Lecture réponse (taille inconnue)...");
        
        String response_str = http.getString();
        bytes_read = response_str.length();
        
        Serial.printf("[AI] Reçu: %d bytes\n", bytes_read);
        
        if (bytes_read == 0) {
            Serial.println("[AI] Erreur: réponse vide");
            http.end();
            return false;
        }
        
        // Copie en PSRAM
        response_buffer = (char*)allocPSRAM(bytes_read + 1);
        if (!response_buffer) {
            Serial.println("[AI] Erreur: allocation réponse PSRAM");
            http.end();
            return false;
        }
        
        memcpy(response_buffer, response_str.c_str(), bytes_read + 1);
    }
    
    http.end();
    
    // Vérification
    if (!response_buffer || bytes_read == 0) {
        Serial.println("[AI] Erreur: réponse invalide");
        if (response_buffer) freePSRAM(response_buffer);
        return false;
    }
    
    // Parse réponse
    bool success = parseResponse(response_buffer, bytes_read, result);
    freePSRAM(response_buffer);
    
    return success;
}

// ========== PARSE RÉPONSE ==========

bool OpenAIAnalyzer::parseResponse(const char* response_str, size_t response_size, AIAnalysisResult& result) {
    // Utilisation de ArduinoJson avec allocateur PSRAM
    struct PSRAMAllocator {
        void* allocate(size_t size) {
            return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        }
        void deallocate(void* pointer) {
            heap_caps_free(pointer);
        }
    };
    
    BasicJsonDocument<PSRAMAllocator> doc(8192);
    
    DeserializationError error = deserializeJson(doc, response_str, response_size);
    if (error) {
        Serial.printf("[AI] Erreur JSON: %s\n", error.c_str());
        return false;
    }
    
    // Extraction du contenu
    const char* content = doc["choices"][0]["message"]["content"];
    if (!content) {
        Serial.println("[AI] Erreur: contenu manquant");
        return false;
    }
    
    // Copie réponse en PSRAM
    size_t content_len = strlen(content);
    result.response_text = (char*)allocPSRAM(content_len + 1);
    if (!result.response_text) {
        Serial.println("[AI] Erreur: allocation texte PSRAM");
        return false;
    }
    
    memcpy(result.response_text, content, content_len + 1);
    result.response_length = content_len;
    
    // Extraction statistiques
    result.prompt_tokens = doc["usage"]["prompt_tokens"] | 0;
    result.completion_tokens = doc["usage"]["completion_tokens"] | 0;
    result.total_tokens = doc["usage"]["total_tokens"] | 0;
    result.estimated_cost_usd = calculateCost(result.total_tokens, config_.model);
    result.success = true;
    
    return true;
}

// ========== CALCUL COÛT ==========

float OpenAIAnalyzer::calculateCost(uint32_t total_tokens, const char* model) {
    // Tarifs OpenAI (au 01/2025)
    float cost_per_1k = 0.0f;
    
    if (strstr(model, "gpt-4o-mini")) {
        cost_per_1k = 0.00015f; // Input + Output moyen
    } else if (strstr(model, "gpt-3.5-turbo")) {
        cost_per_1k = 0.002f;
    } else if (strstr(model, "gpt-4o")) {
        cost_per_1k = 0.0075f;
    }
    
    return (total_tokens / 1000.0f) * cost_per_1k;
}

// ========== GESTION MÉMOIRE PSRAM ==========

void* OpenAIAnalyzer::allocPSRAM(size_t size) {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (ptr) {
        memset(ptr, 0, size); // Clear
    }
    return ptr;
}

void OpenAIAnalyzer::freePSRAM(void* ptr) {
    if (ptr) {
        heap_caps_free(ptr);
    }
}

bool OpenAIAnalyzer::checkPSRAMAvailable(size_t required_size) {
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    return free_psram >= required_size;
}

void OpenAIAnalyzer::printMemoryStats() {
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t used_psram = total_psram - free_psram;
    
    size_t free_ram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t total_ram = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    
    Serial.println("\n[MEM] === Statistiques Mémoire ===");
    Serial.printf("[MEM] PSRAM: %d/%d KB utilisés (%.1f%%)\n", 
        used_psram/1024, total_psram/1024, (used_psram*100.0f)/total_psram);
    Serial.printf("[MEM] RAM interne: %d KB libres\n", free_ram/1024);
    Serial.println("[MEM] ==============================\n");
}

bool OpenAIAnalyzer::testConnection() {
    Serial.println("\n[TEST] === Test connectivité OpenAI ===");
    
    if (!client_) {
        Serial.println("[TEST] ❌ Client non initialisé");
        return false;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[TEST] ❌ WiFi non connecté");
        return false;
    }
    
    HTTPClient http;
    http.begin(*client_, "https://api.openai.com/v1/models");
    
    char* auth_header = (char*)allocPSRAM(strlen(config_.api_key) + 32);
    if (!auth_header) {
        Serial.println("[TEST] ❌ Erreur allocation");
        return false;
    }
    
    snprintf(auth_header, strlen(config_.api_key) + 32, "Bearer %s", config_.api_key);
    http.addHeader("Authorization", auth_header);
    freePSRAM(auth_header);
    
    http.setTimeout(15000);
    
    Serial.println("[TEST] Connexion à api.openai.com...");
    int http_code = http.GET();
    
    Serial.printf("[TEST] Code HTTP: %d\n", http_code);
    
    bool success = false;
    
    if (http_code == 200) {
        Serial.println("[TEST] ✅ Connexion OpenAI OK!");
        Serial.println("[TEST] ✅ Clé API valide");
        Serial.println("[TEST] ✅ SSL fonctionnel");
        success = true;
    } else if (http_code == 401) {
        Serial.println("[TEST] ❌ Clé API invalide ou expirée");
        Serial.println("[TEST] Vérifiez votre OPENAI_API_KEY");
    } else if (http_code < 0) {
        Serial.println("[TEST] ❌ Erreur de connexion réseau");
        Serial.println("[TEST] Causes possibles:");
        Serial.println("[TEST]   - Problème SSL/certificat");
        Serial.println("[TEST]   - Timeout réseau");
        Serial.println("[TEST]   - DNS non résolu");
        Serial.printf("[TEST]   - Code erreur: %d\n", http_code);
    } else {
        Serial.printf("[TEST] ⚠️  Code HTTP inattendu: %d\n", http_code);
        if (http_code > 0) {
            Serial.println("[TEST] Réponse:");
            Serial.println(http.getString());
        }
    }
    
    http.end();
    
    Serial.println("[TEST] ===========================\n");
    
    return success;
}

void OpenAIAnalyzer::resetStats() {
    total_requests_ = 0;
    failed_requests_ = 0;
    total_tokens_used_ = 0;
    total_cost_usd_ = 0.0f;
}

// ========== FONCTIONS UTILITAIRES ==========

namespace LinkyUtils {

void calculateStats(LinkyData& data) {
    // Consommation totale
    uint32_t total_wh = data.index_hc + data.index_hp;
    data.conso_totale_kwh = total_wh / 1000;
    
    // Ratio HC/HP
    if (total_wh > 0) {
        data.ratio_hc_hp = (data.index_hc * 100.0f) / total_wh;
    } else {
        data.ratio_hc_hp = 0.0f;
    }
    
    // Puissance max totale
    data.puissance_max_totale = data.puissance_max_p1 + 
                                 data.puissance_max_p2 + 
                                 data.puissance_max_p3;
    
    // Taux utilisation abonnement
    uint16_t puissance_abo_w = data.puissance_souscrite * 1000;
    if (puissance_abo_w > 0) {
        data.taux_utilisation_abo = (data.puissance_max_totale * 100.0f) / puissance_abo_w;
    } else {
        data.taux_utilisation_abo = 0.0f;
    }
}

void printLinkyData(const LinkyData& data) {
    Serial.println("\n=== DONNÉES LINKY ===");
    Serial.printf("Type: %s | Période: %d jours\n", data.type_abonnement, data.nb_jours);
    Serial.printf("HC: %u kWh | HP: %u kWh | Total: %u kWh\n",
        data.index_hc/1000, data.index_hp/1000, data.conso_totale_kwh);
    Serial.printf("Ratio HC/HP: %.1f%% / %.1f%%\n",
        data.ratio_hc_hp, 100.0f - data.ratio_hc_hp);
    if (data.horaires_hc[0] != '\0') {
        Serial.printf("Horaires HC: %s\n", data.horaires_hc);
    }
    Serial.printf("Puissance max: %u W (%.2f kVA)\n",
        data.puissance_max_totale, data.puissance_max_totale/1000.0f);
    Serial.printf("Abonnement: %u kVA | Utilisation: %.1f%%\n",
        data.puissance_souscrite, data.taux_utilisation_abo);
    Serial.println("====================\n");
}

void printAIResult(const AIAnalysisResult& result) {
    if (!result.success) {
        Serial.println("[AI] Analyse échouée");
        return;
    }
    
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║       ANALYSE OPENAI - RÉSULTAT        ║");
    Serial.println("╠════════════════════════════════════════╣");
    Serial.println(result.response_text);
    Serial.println("╠════════════════════════════════════════╣");
    Serial.printf("║ Tokens: %u (prompt:%u + réponse:%u) ║\n",
        result.total_tokens, result.prompt_tokens, result.completion_tokens);
    Serial.printf("║ Coût estimé: $%.6f USD                ║\n", result.estimated_cost_usd);
    Serial.printf("║ Durée: %u ms                          ║\n", result.duration_ms);
    Serial.println("╚════════════════════════════════════════╝\n");
}

} // namespace LinkyUtils