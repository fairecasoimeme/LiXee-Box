#ifndef SMART_WIFI_MANAGER_H
#define SMART_WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// Forward declaration pour éviter l'include BLE
class DynamicBLEManager;

// États WiFi
enum WiFiState {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED,
    WIFI_STATE_BLE_PROVISIONING
};

// Callbacks
typedef std::function<void(WiFiState)> WiFiStateCallback;
typedef std::function<void(const String&)> WiFiEventCallback;

class SmartWiFiManager {
public:
    SmartWiFiManager();
    ~SmartWiFiManager();
    
    // === MÉTHODES PRINCIPALES ===
    bool begin();                                    // Démarrage intelligent
    void end();                                      // Arrêt complet
    void handleEvents();                             // Gestion événements (dans loop)
    
    // === GESTION CONFIGURATION ===
    bool loadConfig(const String& configPath = "/config/configWifi.json");
    bool saveConfig(const String& configPath = "configWifi.json");
    //bool hasValidConfig() const { return ConfigSettings.valid; }
    void setCredentials(const String& ssid, const String& password);
    void setStaticIP(const String& ip, const String& gateway, const String& subnet);
    
    // === CONTRÔLE CONNEXION ===
    bool connect(uint32_t timeoutMs = 30000);        // Connexion WiFi
    void disconnect();                               // Déconnexion
    void reconnect();                                // Reconnexion
    bool forceProvisioning();                        // Force le mode BLE
    
    // === ÉTAT ET INFORMATIONS ===
    WiFiState getState() const { return _currentState; }
    bool isConnected() const { return _currentState == WIFI_STATE_CONNECTED; }
    bool isProvisioning() const { return _currentState == WIFI_STATE_BLE_PROVISIONING; }
    String getSSID() const;
    String getLocalIP() const;
    String getMacAddress() const;
    int32_t getRSSI() const;
    String getStatusString() const;

    // === CONTRÔLE RECONNEXION ===
    void setAutoReconnect(bool enable);
    void setMaxReconnectAttempts(uint8_t attempts);
    void setReconnectDelay(unsigned long delayMs);
    uint8_t getReconnectAttempts() const;
    bool isReconnecting() const;
    
    // === CALLBACKS ===
    void onStateChange(WiFiStateCallback callback) { _stateCallback = callback; }
    void onEvent(WiFiEventCallback callback) { _eventCallback = callback; }
    
    // === UTILITAIRES ===
    static String generateDeviceName(const String& prefix = "LIXEEGW");
    static IPAddress parseIPAddress(const String& ip);
    static bool isWiFiConfigValid();                 // Vérifie si config existe
    
private:
    // === VARIABLES D'ÉTAT ===
    WiFiState _currentState;
    String _configPath;
    
    // === GESTION RECONNEXION AVANCÉE ===
    bool _autoReconnect;                             // Reconnexion auto activée
    uint8_t _reconnectAttempts;                      // Tentatives actuelles
    uint8_t _maxReconnectAttempts;                   // Max avant BLE (défaut: 10)
    unsigned long _reconnectDelayMs;                 // Délai entre tentatives
    unsigned long _lastReconnectTime;                // Dernier essai
    bool _reconnecting;                              // Flag reconnexion en cours

    // === GESTION CONNEXION ===
    unsigned long _lastConnectionAttempt;
    unsigned long _connectionTimeout;
    uint32_t _reconnectDelay;
    uint8_t _connectionAttempts;
    uint8_t _maxConnectionAttempts;
    
    // === BLE PROVISIONING (chargé dynamiquement) ===
    DynamicBLEManager* _bleManager;                  // Pointeur dynamique
    bool _bleLoadAttempted;                          // Flag pour éviter les rechargements
    bool _provisioningRequired;                      // BLE requis ?
    
    // === CALLBACKS ===
    WiFiStateCallback _stateCallback;
    WiFiEventCallback _eventCallback;
    
    // === MÉTHODES PRIVÉES ===
    // Méthode de reconnexion
    void attemptReconnection();
    
    // Logique principale
    bool determineStartupMode();                     // Détermine le mode de démarrage
    bool tryWiFiConnection();                        // Essaie la connexion WiFi
    bool startBLEProvisioning();                     // Démarre BLE si nécessaire
    void stopBLEProvisioning();                      // Arrête BLE
    
    // Gestion WiFi
    bool validateConfig() const;
    bool configureStaticIP();
    void handleWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
    
    // Gestion BLE
    bool loadBLEManager();                           // Charge BLE dynamiquement
    void unloadBLEManager();                         // Décharge BLE
    void onBLECredentialsReceived(const String& ssid, const String& password);
    void onBLEStatusUpdate(const String& status);
    
    // État et événements
    void setState(WiFiState newState);
    void notifyEvent(const String& event);
    void resetConnectionState();
    
    // Instance statique pour callbacks WiFi
    static SmartWiFiManager* _instance;
    static void staticWiFiEventHandler(WiFiEvent_t event, WiFiEventInfo_t info);
};

#endif // SMART_WIFI_MANAGER_H