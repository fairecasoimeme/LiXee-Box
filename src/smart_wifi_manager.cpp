#include <esp_task_wdt.h>
#include "smart_wifi_manager.h"
// Include BLE manager SEULEMENT dans le .cpp
#include "dynamic_ble_manager.h"

#include "SPIFFS_ini.h"
#include "config.h"

extern ConfigSettingsStruct ConfigSettings;
extern bool executeReboot;
// Instance statique pour callbacks WiFi
SmartWiFiManager* SmartWiFiManager::_instance = nullptr;

// Constructeur
SmartWiFiManager::SmartWiFiManager() 
    : _currentState(WIFI_STATE_DISCONNECTED)
    , _configPath("/config/configWifi.json")
    , _lastConnectionAttempt(0)
    , _connectionTimeout(30000)
    , _reconnectDelay(5000)
    , _connectionAttempts(0)
    , _maxConnectionAttempts(3)
    , _bleManager(nullptr)
    , _bleLoadAttempted(false)
    , _provisioningRequired(false)
{
    _instance = this;
}

// Destructeur
SmartWiFiManager::~SmartWiFiManager() {
    end();
    _instance = nullptr;
}

// === DÉMARRAGE INTELLIGENT ===
bool SmartWiFiManager::begin() {  
    // Configurer les événements WiFi
    WiFi.onEvent(staticWiFiEventHandler);
    
    // Déterminer le mode de démarrage
    if (!determineStartupMode()) {
        return false;
    }
    
    return true;
}

// Détermine le mode de démarrage
bool SmartWiFiManager::determineStartupMode() {
    
    
    // 1. Vérifier si BLE est requis d'emblée
    if (DynamicBLEManager::isBLERequired()) {

        _provisioningRequired = true;
        return startBLEProvisioning();
    }
    
    // 2. Config WiFi trouvée - charger et essayer
    if (loadConfig()) {
        return tryWiFiConnection();
    }
    
    // 3. Problème de chargement config - BLE requis
    Serial.println("⚠️ Config loading failed - falling back to BLE");
    _provisioningRequired = true;
    return startBLEProvisioning();
}

// Essaie la connexion WiFi
bool SmartWiFiManager::tryWiFiConnection() {
    
    Serial.println("🔄 Attempting WiFi connection...");
    setState(WIFI_STATE_CONNECTING);
    
    // Arrêter BLE si actif (libère la mémoire)
    if (_bleManager) {
        stopBLEProvisioning();
    }
    
    return connect(_connectionTimeout);
}

// Démarre le provisioning BLE (chargement dynamique)
bool SmartWiFiManager::startBLEProvisioning() {
    if (_currentState == WIFI_STATE_BLE_PROVISIONING) {
        //DEBUG_PRINTLN("⚠️ BLE provisioning already active");
        return true;
    }
    
    //DEBUG_PRINTLN("📱 Starting BLE provisioning...");
    
    // Charger le gestionnaire BLE dynamiquement
    if (!loadBLEManager()) {
        //DEBUG_PRINTLN("❌ Failed to load BLE manager");
        return false;
    }
    
    // Configurer les callbacks BLE
    _bleManager->onCredentialsReceived([this](const WiFiCredentials& creds) {
        onBLECredentialsReceived(creds.ssid, creds.password);
    });
    
    _bleManager->onStatusUpdate([this](const String& status) {
        onBLEStatusUpdate(status);
    });
    
    // Démarrer BLE
    String deviceName = generateDeviceName("LIXEEBOX");
    if (_bleManager->begin(deviceName)) {
        setState(WIFI_STATE_BLE_PROVISIONING);
        notifyEvent("BLE Provisioning started: " + deviceName);
        //Serial.printf("📊 Memory after BLE start - Heap: %u, PSRAM: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());
        return true;
    } else {
        //DEBUG_PRINTLN("❌ Failed to start BLE provisioning");
        unloadBLEManager();
        return false;
    }
}

// Arrête le provisioning BLE
void SmartWiFiManager::stopBLEProvisioning() {
    if (_currentState != WIFI_STATE_BLE_PROVISIONING || !_bleManager) {
        return;
    }
    
    //DEBUG_PRINTLN("📱 Stopping BLE provisioning...");
    
    unloadBLEManager();
    
    if (_currentState == WIFI_STATE_BLE_PROVISIONING) {
        setState(WIFI_STATE_DISCONNECTED);
    }
    
    //Serial.printf("📊 Memory after BLE stop - Heap: %u, PSRAM: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());
}

// === CHARGEMENT DYNAMIQUE BLE ===

// Charge le gestionnaire BLE
bool SmartWiFiManager::loadBLEManager() {
    if (_bleManager) {
        return true; // Déjà chargé
    }
    
    if (_bleLoadAttempted) {
        //DEBUG_PRINTLN("⚠️ BLE load already attempted and failed");
        return false;
    }
    
    _bleLoadAttempted = true;
    
    //DEBUG_PRINTLN("📚 Loading BLE manager dynamically...");
    //Serial.printf("📊 Memory before BLE load - Heap: %u, PSRAM: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());
    
    try {
        // Créer l'instance BLE (les headers sont inclus seulement dans le .cpp du BLE manager)
        _bleManager = new DynamicBLEManager();
        
        if (_bleManager) {
            //DEBUG_PRINTLN("✅ BLE manager loaded successfully");
            return true;
        } else {
            //DEBUG_PRINTLN("❌ Failed to create BLE manager instance");
            return false;
        }
        
    } catch (const std::exception& e) {
        //DEBUG_PRINTLN("❌ Exception loading BLE manager: " + String(e.what()));
        return false;
    } catch (...) {
        //DEBUG_PRINTLN("❌ Unknown exception loading BLE manager");
        return false;
    }
}

// Décharge le gestionnaire BLE
void SmartWiFiManager::unloadBLEManager() {
    if (!_bleManager) {
        return;
    }
    
    //DEBUG_PRINTLN("📚 Unloading BLE manager...");
    
    try {
        // Arrêter BLE complètement
        _bleManager->end();
        
        // Supprimer l'instance
        delete _bleManager;
        _bleManager = nullptr;
        
        //DEBUG_PRINTLN("✅ BLE manager unloaded");
        
    } catch (...) {
        //DEBUG_PRINTLN("⚠️ Exception during BLE manager unload");
        _bleManager = nullptr; // Force reset même en cas d'erreur
    }
}

// === CALLBACKS BLE ===

// Réception des identifiants via BLE
void SmartWiFiManager::onBLECredentialsReceived(const String& ssid, const String& password) {
    //DEBUG_PRINTLN("📱 WiFi credentials received via BLE");
    //DEBUG_PRINTLN("SSID: " + ssid);
    
    // Mettre à jour la configuration
    setCredentials(ssid, password);
    
    // Sauvegarder
    if (saveConfig()) {
        //Serial.println("✅ WiFi config saved");
    } else {
        //Serial.println("⚠️ Failed to save WiFi config");
    }
    
    // Informer l'utilisateur BLE
    if (_bleManager) {
        _bleManager->sendMessage("✅ Credentials saved! Connecting...");
        delay(1000);
    } 
    
    // Arrêter BLE et essayer WiFi
    stopBLEProvisioning();
    delay(1000);
    
    ESP.restart();
    /*if (!tryWiFiConnection()) {
        //DEBUG_PRINTLN("❌ WiFi connection failed after BLE provisioning");
        delay(2000);
        // Redémarrer BLE si la connexion échoue
        startBLEProvisioning();
    }*/
}

// Statut BLE
void SmartWiFiManager::onBLEStatusUpdate(const String& status) {
    notifyEvent("BLE: " + status);
}

// === GESTION CONFIGURATION ===

// Vérification config WiFi (statique)
bool SmartWiFiManager::isWiFiConfigValid() {
    return !DynamicBLEManager::isBLERequired(); // Inverse de isBLERequired
}

// Chargement configuration
bool SmartWiFiManager::loadConfig(const String& configPath) {
    _configPath = configPath;
  
    File configFile = LittleFS.open(_configPath, FILE_READ);
    if (!configFile) {
        configFile.close();
        return false;
    }

    SpiRamJsonDocument doc(10192);
    deserializeJson(doc,configFile);

    // affectation des valeurs , si existe pas on place une valeur par defaut
    //ConfigSettings.enableWiFi = (int)doc["enableWiFi"];
    ConfigSettings.enableWiFi = 1;
    if (doc["enableDHCP"].as<String>()!="null")
    {
        ConfigSettings.enableDHCP = (int)doc["enableDHCP"];
    }else{
        ConfigSettings.enableDHCP = 1;
    }
    strlcpy(ConfigSettings.ssid, doc["ssid"] | "", sizeof(ConfigSettings.ssid));
    strlcpy(ConfigSettings.password, doc["pass"] | "", sizeof(ConfigSettings.password));
    strlcpy(ConfigSettings.ipAddressWiFi, doc["ip"] | "", sizeof(ConfigSettings.ipAddressWiFi));
    strlcpy(ConfigSettings.ipMaskWiFi, doc["mask"] | "", sizeof(ConfigSettings.ipMaskWiFi));
    strlcpy(ConfigSettings.ipGWWiFi, doc["gw"] | "", sizeof(ConfigSettings.ipGWWiFi));

    configFile.close();
    return true;


}

// Sauvegarde configuration
bool SmartWiFiManager::saveConfig(const String& configPath) {
    if (!configPath.isEmpty()) {
        _configPath = configPath;
    }
    
    config_write(configPath,"ssid",ConfigSettings.ssid);
    config_write(configPath,"pass",ConfigSettings.password);

    return true;

}

// Validation configuration
bool SmartWiFiManager::validateConfig() const {
    return (strlen(ConfigSettings.ssid) > 0 && strlen(ConfigSettings.ssid) <= 32);
}

// Configuration identifiants
void SmartWiFiManager::setCredentials(const String& ssid, const String& password) {
    strncpy(ConfigSettings.ssid, ssid.c_str(), sizeof(ConfigSettings.ssid) - 1);
    strncpy(ConfigSettings.password, password.c_str(), sizeof(ConfigSettings.password) - 1);
    ConfigSettings.ssid[sizeof(ConfigSettings.ssid) - 1] = '\0';
    ConfigSettings.password[sizeof(ConfigSettings.password) - 1] = '\0';
   
    
    //DEBUG_PRINTLN("📝 WiFi credentials updated: " + ssid);
}

// === CONNEXION WIFI ===

// Connexion WiFi
bool SmartWiFiManager::connect(uint32_t timeoutMs) {
   
    _connectionTimeout = timeoutMs;
    _lastConnectionAttempt = millis();
    _connectionAttempts++;
    
    //DEBUG_PRINTLN("🔄 Connecting to WiFi: " + String(ConfigSettings.ssid));
    setState(WIFI_STATE_CONNECTING);
    
    // Arrêter BLE si actif
    if (_currentState == WIFI_STATE_BLE_PROVISIONING) {
        stopBLEProvisioning();
        delay(1000); // Laisser le temps au BLE de se libérer
    }
    
    // Configurer WiFi
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    // IP statique si configurée
    if (!ConfigSettings.enableDHCP) {
        if (!configureStaticIP()) {
            Serial.println("❌ Failed to configure static IP");
            setState(WIFI_STATE_FAILED);
            return false;
        }
    }
    Serial.printf("SSID : %s\n",ConfigSettings.ssid);
    Serial.printf("pass : %s\n",ConfigSettings.password);
    Serial.printf("ip : %s\n",ConfigSettings.ipAddressWiFi);
    Serial.printf("gw : %s\n",ConfigSettings.ipGWWiFi);
    // Démarrer connexion
    WiFi.begin(ConfigSettings.ssid, ConfigSettings.password);
    
    // Attendre connexion
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeoutMs) {
        delay(500);
        //Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        setState(WIFI_STATE_CONNECTED);
        notifyEvent("Connected to " + String(ConfigSettings.ssid));
        
        //DEBUG_PRINTLN();
        //DEBUG_PRINTLN("✅ WiFi connected successfully!");
        //DEBUG_PRINTLN("IP: " + WiFi.localIP().toString());
        //DEBUG_PRINTLN("RSSI: " + String(WiFi.RSSI()) + " dBm");
        
        _connectionAttempts = 0;
        _provisioningRequired = false;
        return true;
    } else {
        setState(WIFI_STATE_FAILED);
        notifyEvent("Connection failed to " + String(ConfigSettings.ssid));
        
        //DEBUG_PRINTLN();
        //DEBUG_PRINTLN("❌ WiFi connection failed");
        
        // Si trop d'échecs, redémarrer BLE
        if (_connectionAttempts >= _maxConnectionAttempts) {
            //DEBUG_PRINTLN("🔄 Too many failures, starting BLE provisioning...");
            delay(1000);
            return startBLEProvisioning();
        }
        
        return false;
    }
}

// Configuration IP statique
bool SmartWiFiManager::configureStaticIP() {
    IPAddress staticIP = parseIPAddress(ConfigSettings.ipAddressWiFi);
    IPAddress gateway = parseIPAddress(ConfigSettings.ipGWWiFi);
    IPAddress subnet = parseIPAddress(ConfigSettings.ipMaskWiFi);
    
    if (staticIP == IPAddress(0, 0, 0, 0)) {
        Serial.println("❌ Invalid static IP address");
        return false;
    }
    
    return WiFi.config(staticIP, gateway, subnet, parseIPAddress("8.8.8.8"), parseIPAddress("8.8.4.4"));
}

// Déconnexion
void SmartWiFiManager::disconnect() {
    if (_currentState != WIFI_STATE_DISCONNECTED) {
        //DEBUG_PRINTLN("🔌 Disconnecting WiFi...");
        WiFi.disconnect();
        setState(WIFI_STATE_DISCONNECTED);
    }
}

// Reconnexion
void SmartWiFiManager::reconnect() {
    //DEBUG_PRINTLN("🔄 Reconnecting WiFi...");
    disconnect();
    delay(1000);
    connect();
}

// Force le provisioning
bool SmartWiFiManager::forceProvisioning() {
    //DEBUG_PRINTLN("🔄 Forcing BLE provisioning...");
    
    disconnect();
    _provisioningRequired = true;
    return startBLEProvisioning();
}

// === GESTION ÉVÉNEMENTS ===

// Gestionnaire événements principal
void SmartWiFiManager::handleEvents() {
    // Gérer BLE si actif
    if (_bleManager && _currentState == WIFI_STATE_BLE_PROVISIONING) {
        _bleManager->handleEvents();
    }
    
    // Gestion reconnexion automatique
    if (_currentState == WIFI_STATE_FAILED || 
        (_currentState == WIFI_STATE_CONNECTING && 
         (millis() - _lastConnectionAttempt) > _connectionTimeout)) {
        
        if ((millis() - _lastConnectionAttempt) > _reconnectDelay) {
            if (_connectionAttempts < _maxConnectionAttempts) {
                //DEBUG_PRINTLN("🔄 Auto-reconnecting...");
                connect();
            } else {
                //DEBUG_PRINTLN("🔄 Max attempts reached, starting BLE...");
                startBLEProvisioning();
            }
        }
    }
}

// Arrêt complet
void SmartWiFiManager::end() {
    stopBLEProvisioning();
    disconnect();
    
    // L'instance sera mise à nullptr dans le destructeur
    // Les callbacks vérifieront _instance avant d'agir
}

// === CALLBACKS WIFI ===

// Gestionnaire événements WiFi statique
void SmartWiFiManager::staticWiFiEventHandler(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (_instance) {
        _instance->handleWiFiEvent(event, info);
    }
}

// Gestionnaire événements WiFi
void SmartWiFiManager::handleWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            notifyEvent("WiFi connected");
            break;
            
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            setState(WIFI_STATE_DISCONNECTED);
            notifyEvent("WiFi disconnected (reason: " + String(info.wifi_sta_disconnected.reason) + ")");
            ConfigSettings.connectedWifiSta = false;
            // Auth failed = redémarrer provisioning
            if (info.wifi_sta_disconnected.reason == 201) {
                //DEBUG_PRINTLN("🔐 Authentication failed, starting provisioning...");
                delay(1000);
                startBLEProvisioning();
            }
            break;
            
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            setState(WIFI_STATE_CONNECTED);
            notifyEvent("IP obtained: " + WiFi.localIP().toString());
            ConfigSettings.connectedWifiSta = true;
            break;
            
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            setState(WIFI_STATE_FAILED);
            notifyEvent("IP lost");
            ConfigSettings.connectedWifiSta = false;
            break;
            
        default:
            break;
    }
}

// === UTILITAIRES ET GETTERS ===

// Changement d'état
void SmartWiFiManager::setState(WiFiState newState) {
    if (_currentState != newState) {
        _currentState = newState;
        if (_stateCallback) {
            _stateCallback(newState);
        }
        
        //DEBUG_PRINTLN("📊 WiFi State: " + getStatusString());
    }
}

// Notification événement
void SmartWiFiManager::notifyEvent(const String& event) {
    if (_eventCallback) {
        _eventCallback(event);
    }
}

// Getters
String SmartWiFiManager::getSSID() const {
    return WiFi.SSID();
}

String SmartWiFiManager::getLocalIP() const {
    return WiFi.localIP().toString();
}

String SmartWiFiManager::getMacAddress() const {
    return WiFi.macAddress();
}

int32_t SmartWiFiManager::getRSSI() const {
    return WiFi.RSSI();
}

String SmartWiFiManager::getStatusString() const {
    switch (_currentState) {
        case WIFI_STATE_DISCONNECTED: return "Disconnected";
        case WIFI_STATE_CONNECTING: return "Connecting";
        case WIFI_STATE_CONNECTED: return "Connected";
        case WIFI_STATE_FAILED: return "Failed";
        case WIFI_STATE_BLE_PROVISIONING: return "BLE Provisioning";
        default: return "Unknown";
    }
}

// Utilitaires statiques
String SmartWiFiManager::generateDeviceName(const String& prefix) {
    uint8_t baseMac[6];
    esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
    char macSuffix[5];
    sprintf(macSuffix, "%02X%02X", baseMac[4], baseMac[5]);
    return prefix + "-" + String(macSuffix);
}

IPAddress SmartWiFiManager::parseIPAddress(const String& ip) {
    IPAddress result;
    result.fromString(ip);
    return result;
}