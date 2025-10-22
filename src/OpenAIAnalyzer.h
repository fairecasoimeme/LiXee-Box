/**
 * OpenAIAnalyzer.h
 * Analyseur IA pour données Linky avec allocation PSRAM exclusive
 * LiXee Box - ESP32S3
 */

#ifndef OPENAI_ANALYZER_H
#define OPENAI_ANALYZER_H

#include <Arduino.h>
#include <WiFiClientSecure.h>

// Structure de données Linky optimisée
struct LinkyData {
    uint32_t index_hc;              // Index heures creuses (Wh)
    uint32_t index_hp;              // Index heures pleines (Wh)
    uint16_t puissance_max_p1;      // Puissance max phase 1 (W)
    uint16_t puissance_max_p2;      // Puissance max phase 2 (W)
    uint16_t puissance_max_p3;      // Puissance max phase 3 (W)
    uint16_t puissance_souscrite;   // Puissance souscrite (kVA)
    uint16_t nb_jours;              // Nombre de jours d'historique
    char horaires_hc[32];           // Ex: "22h30-6h30"
    char type_abonnement[16];       // Ex: "BASE", "HCHP", "TEMPO"
    
    // Statistiques calculées
    uint32_t conso_totale_kwh;      // Consommation totale (kWh)
    float ratio_hc_hp;              // Ratio HC/HP (%)
    uint16_t puissance_max_totale;  // Puissance max observée (W)
    float taux_utilisation_abo;     // % utilisation de l'abonnement
};

// Structure de réponse IA
struct AIAnalysisResult {
    bool success;
    char* response_text;            // Alloué en PSRAM
    uint16_t response_length;
    uint32_t prompt_tokens;
    uint32_t completion_tokens;
    uint32_t total_tokens;
    float estimated_cost_usd;
    uint32_t duration_ms;
    
    // Libération mémoire
    void free() {
        if (response_text) {
            heap_caps_free(response_text);
            response_text = nullptr;
        }
    }
};

struct FullAnalysisConfig {
    bool include_hourly_data;       // Inclure données horaires
    bool include_daily_data;        // Inclure données journalières
    bool include_monthly_data;      // Inclure données mensuelles
    bool use_gpt4;                  // Utiliser GPT-4 (meilleur pour JSON complexes)
    uint16_t max_tokens;            // Tokens max pour réponse
    
    FullAnalysisConfig() :
        include_hourly_data(true),
        include_daily_data(true),
        include_monthly_data(true),
        use_gpt4(false),
        max_tokens(1500) {}
};

// Configuration OpenAI
struct OpenAIConfig {
    const char* api_key;
    const char* model;              // "gpt-3.5-turbo", "gpt-4o-mini", etc.
    uint16_t max_tokens;
    float temperature;
    uint16_t timeout_ms;
    bool use_compact_mode;          // Mode économique en tokens
    
    // Constructeur avec valeurs par défaut
    OpenAIConfig() : 
        api_key(nullptr),
        model("gpt-4o-mini"),
        max_tokens(500),
        temperature(0.7),
        timeout_ms(30000),
        use_compact_mode(false) {}
};

class OpenAIAnalyzer {
public:
    OpenAIAnalyzer();
    ~OpenAIAnalyzer();
    
    // Initialisation
    bool begin(const OpenAIConfig& config);
    
    // Analyse principale
    bool analyzeSubscription(const LinkyData& data, AIAnalysisResult& result);
    
    // Analyse compacte (moins de tokens)
    bool analyzeSubscriptionCompact(const LinkyData& data, AIAnalysisResult& result);

    // Méthode publique
    bool analyzeWithFullJSON(const char* json_hours, 
                        const char* json_minutes, 
                        const LinkyData& summary_data, 
                        AIAnalysisResult& result,
                        const FullAnalysisConfig& config = FullAnalysisConfig());
    
    // Vérification mémoire PSRAM
    void printMemoryStats();
    
    // Test de connectivité OpenAI
    bool testConnection();
    
    // Récupération des statistiques
    uint32_t getTotalRequests() const { return total_requests_; }
    uint32_t getFailedRequests() const { return failed_requests_; }
    uint32_t getTotalTokensUsed() const { return total_tokens_used_; }
    float getTotalCostUSD() const { return total_cost_usd_; }
    
    // Reset des statistiques
    void resetStats();

private:
    OpenAIConfig config_;
    WiFiClientSecure* client_;      // Alloué en PSRAM
    
    // Statistiques
    uint32_t total_requests_;
    uint32_t failed_requests_;
    uint32_t total_tokens_used_;
    float total_cost_usd_;
    
    // Méthodes internes
    char* buildPrompt(const LinkyData& data, bool compact_mode, size_t& prompt_size);
    char* buildFullJSONPrompt(const char* json_hours, const char* json_minutes, const LinkyData& summary_data, const FullAnalysisConfig& config, size_t& prompt_size);
    char* buildJSONRequest(const char* prompt, size_t& json_size);
    char* buildFullJSONRequest(const char* prompt,  const FullAnalysisConfig& full_config, size_t& json_size);
    bool sendRequest(const char* json_body, size_t json_size, AIAnalysisResult& result);
    bool parseResponse(const char* response_str, size_t response_size, AIAnalysisResult& result);
    float calculateCost(uint32_t total_tokens, const char* model);
    
    // Allocation PSRAM
    void* allocPSRAM(size_t size);
    void freePSRAM(void* ptr);
    bool checkPSRAMAvailable(size_t required_size);
};

// Fonctions utilitaires globales
namespace LinkyUtils {
    // Calcul des statistiques
    void calculateStats(LinkyData& data);
    
    // Extraction depuis JSON (votre format)
    bool parseFromJson(const char* json_str, LinkyData& data);
    
    // Affichage des données
    void printLinkyData(const LinkyData& data);
    
    // Affichage du résultat IA
    void printAIResult(const AIAnalysisResult& result);
}

#endif // OPENAI_ANALYZER_H