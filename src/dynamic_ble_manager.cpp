#include "dynamic_ble_manager.h"

// Headers BLE inclus SEULEMENT dans le .cpp
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>

// Headers WiFi nécessaires pour arrêter WiFi avant BLE
#include <WiFi.h>
#include <esp_wifi.h>

// Headers pour gestion fichiers
#include <LittleFS.h>

// UUIDs Nordic UART Service
#define BLE_SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// Instance globale pour callbacks
static DynamicBLEManager* g_bleManagerInstance = nullptr;

// Classes de callbacks (simplifiées pour éviter stack overflow)
class DynamicBLEServerCallbacks : public BLEServerCallbacks {
public:
    void onConnect(BLEServer* pServer) override {
        if (g_bleManagerInstance) {
            g_bleManagerInstance->setDeviceConnected(true);
            // Pas de logs ici - risque stack overflow
        }
    }
    
    void onDisconnect(BLEServer* pServer) override {
        if (g_bleManagerInstance) {
            g_bleManagerInstance->setDeviceConnected(false);
            // Pas de logs ici
        }
    }
};

class DynamicBLECharacteristicCallbacks : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic *pCharacteristic) override {
        if (!g_bleManagerInstance) {
            return;
        }
        
        std::string value = pCharacteristic->getValue();
        
        // Callback ULTRA-MINIMAL pour éviter stack overflow
        if (value.length() > 0) {
            g_bleManagerInstance->setReceivedData(value.c_str(), value.length());
        }
    }
};

// Constructeur
DynamicBLEManager::DynamicBLEManager() 
    : _isActive(false)
    , _deviceConnected(false)
    , _oldDeviceConnected(false)
    , _pServer(nullptr)
    , _pTxCharacteristic(nullptr)
    , _pRxCharacteristic(nullptr)
    , _pService(nullptr)
    , _newDataReceived(false)
    , _receivedDataLength(0)
    , _bleLibrariesLoaded(false)
    , _bluetoothInitialized(false)
    , _serverCallbacks(nullptr)
    , _characteristicCallbacks(nullptr)
{
    memset(_receivedData, 0, sizeof(_receivedData));
    g_bleManagerInstance = this;
}

// Destructeur
DynamicBLEManager::~DynamicBLEManager() {
    end();
    g_bleManagerInstance = nullptr;
}

// Détermine si BLE est nécessaire
bool DynamicBLEManager::isBLERequired() {
    // Vérifier si config WiFi existe et est valide
    File configFile = LittleFS.open("/config/configWifi.json", FILE_READ);
    if (!configFile) {
        Serial.println("🔍 No WiFi config found - BLE required");
        return true; // Pas de config = BLE requis
    }
    
    // Lire et vérifier la config
    String content = configFile.readString();
    configFile.close();
    
    if (content.indexOf("\"ssid\"") == -1 || content.length() < 50) {
        Serial.println("🔍 Invalid WiFi config - BLE required");
        return true; // Config invalide = BLE requis
    }
    
    // Extraire le SSID pour vérification basique
    int ssidStart = content.indexOf("\"ssid\":\"") + 8;
    int ssidEnd = content.indexOf("\"", ssidStart);
    
    if (ssidStart < 8 || ssidEnd <= ssidStart) {
        Serial.println("🔍 No SSID in config - BLE required");
        return true;
    }
    
    String ssid = content.substring(ssidStart, ssidEnd);
    if (ssid.length() == 0) {
        Serial.println("🔍 Empty SSID - BLE required");
        return true;
    }
    
    Serial.println("🔍 Valid WiFi config found - BLE not required initially");
    return false; // Config valide = pas besoin de BLE pour l'instant
}

// Génération du nom de device
String DynamicBLEManager::generateDeviceName(const String& prefix) {
    uint8_t baseMac[6];
    esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
    char macSuffix[5];
    sprintf(macSuffix, "%02X%02X", baseMac[4], baseMac[5]);
    return prefix + "-" + String(macSuffix);
}

// Chargement dynamique des bibliothèques BLE
bool DynamicBLEManager::loadBLELibrary() {
    if (_bleLibrariesLoaded) {
        return true;
    }
    
    //DEBUG_PRINTLN("📚 Loading BLE libraries dynamically...");
    
    try {
        // Arrêter WiFi complètement pour libérer les ressources
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        esp_wifi_stop();
        esp_wifi_deinit();
        delay(1000);
        
        // CRITIQUE: Désactiver logs BLE pour éviter stack overflow
        esp_log_level_set("*", ESP_LOG_WARN);
        esp_log_level_set("BLE*", ESP_LOG_ERROR);
        esp_log_level_set("bt*", ESP_LOG_ERROR);
        esp_log_level_set("btc*", ESP_LOG_ERROR);
        esp_log_level_set("BLECharacteristic", ESP_LOG_NONE);
        esp_log_level_set("BLEUtils", ESP_LOG_NONE);
        
        //DEBUG_PRINTLN("✅ WiFi stopped, BLE logs reduced for stack safety");
        //Serial.printf("📊 Free heap before BLE init: %u bytes\n", ESP.getFreeHeap());
        //Serial.printf("📊 Free PSRAM before BLE init: %u bytes\n", ESP.getFreePsram());
        
        _bleLibrariesLoaded = true;
        return true;
        
    } catch (const std::exception& e) {
        //DEBUG_PRINTLN("❌ Failed to load BLE libraries: " + String(e.what()));
        return false;
    }
}

// Déchargement des bibliothèques BLE
void DynamicBLEManager::unloadBLELibrary() {
    if (!_bleLibrariesLoaded) {
        return;
    }
    
    //DEBUG_PRINTLN("📚 Unloading BLE libraries...");
    
    try {
        // Cleanup BLE complet
        cleanupBLE();
        
        _bleLibrariesLoaded = false;
        _bluetoothInitialized = false;
        
        // Réactiver logs après BLE
        esp_log_level_set("*", ESP_LOG_INFO);
        
        //Serial.printf("📊 Free heap after BLE cleanup: %u bytes\n", ESP.getFreeHeap());
        //Serial.printf("📊 Free PSRAM after BLE cleanup: %u bytes\n", ESP.getFreePsram());
        //DEBUG_PRINTLN("✅ BLE libraries unloaded, logs restored");
        
    } catch (...) {
        //DEBUG_PRINTLN("⚠️ Error during BLE library unload");
    }
}

// Initialisation BLE
bool DynamicBLEManager::begin(const String& deviceName) {
    if (_isActive) {
        //DEBUG_PRINTLN("⚠️ BLE already active");
        return true;
    }
    
    // Charger les bibliothèques BLE seulement maintenant
    if (!loadBLELibrary()) {
        //DEBUG_PRINTLN("❌ Failed to load BLE libraries");
        return false;
    }
    
    // Générer nom si vide
    if (deviceName.isEmpty()) {
        _deviceName = generateDeviceName("LIXEEBOX");
    } else {
        _deviceName = deviceName;
    }
    
    //DEBUG_PRINTLN("🔵 Starting BLE Provisioning: " + _deviceName);
    //DEBUG_PRINTLN("⚠️ Using POLLING method (callbacks may not work)");
    
    return setupBLE();
}

// Configuration BLE
bool DynamicBLEManager::setupBLE() {
    try {
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);
        
        // Initialiser BLE Device
        BLEDevice::init(_deviceName.c_str());
        _bluetoothInitialized = true;
        
        // Créer serveur
        BLEServer* pServer = BLEDevice::createServer();
        _pServer = (void*)pServer;
        
        // Créer callbacks serveur
        _serverCallbacks = new DynamicBLEServerCallbacks();
        pServer->setCallbacks((BLEServerCallbacks*)_serverCallbacks);
        
        // Créer service
        BLEService* pService = pServer->createService(BLE_SERVICE_UUID);
        _pService = (void*)pService;
        
        // TX Characteristic (ESP32 -> Phone)
        BLECharacteristic* pTxCharacteristic = pService->createCharacteristic(
            BLE_CHARACTERISTIC_UUID_TX,
            BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
        );
        pTxCharacteristic->addDescriptor(new BLE2902());
        _pTxCharacteristic = (void*)pTxCharacteristic;
        
        // RX Characteristic (Phone -> ESP32)
        BLECharacteristic* pRxCharacteristic = pService->createCharacteristic(
            BLE_CHARACTERISTIC_UUID_RX,
            BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
        );
        
        _characteristicCallbacks = new DynamicBLECharacteristicCallbacks();
        pRxCharacteristic->setCallbacks((BLECharacteristicCallbacks*)_characteristicCallbacks);
        _pRxCharacteristic = (void*)pRxCharacteristic;
        
        // IMPORTANT: Callback sur TX aussi (app écrit parfois sur TX)
        pTxCharacteristic->setCallbacks((BLECharacteristicCallbacks*)_characteristicCallbacks);
        
        // Message initial
        pTxCharacteristic->setValue("Ready: SSID|PASSWORD");
        
        // Démarrer service
        pService->start();
        
        // ✅ CONFIGURATION UNIVERSELLE COMPATIBLE iOS ET Android
        BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
        pAdvertising->setScanResponse(true);
        pAdvertising->setMinPreferred(0x06);
        pAdvertising->setMaxPreferred(0x12);
        
        // ✅ Utiliser les paramètres par défaut qui fonctionnent sur les deux plateformes
        BLEDevice::startAdvertising();
        
        _isActive = true;
        
        Serial.println("✅ BLE Provisioning active (compatible iOS + Android)");
        Serial.println("📱 Device name: " + _deviceName);
        
        return true;
        
    } catch (const std::exception& e) {
        Serial.println("❌ BLE setup failed: " + String(e.what()));
        cleanupBLE();
        return false;
    }
}

// Arrêt BLE
void DynamicBLEManager::end() {
    if (!_isActive) return;
    
    //DEBUG_PRINTLN("🔴 Stopping BLE Provisioning...");
    
    cleanupBLE();
    unloadBLELibrary();
    
    _isActive = false;
    //DEBUG_PRINTLN("✅ BLE Provisioning stopped completely");
}

// Nettoyage BLE
void DynamicBLEManager::cleanupBLE() {
    try {
        //DEBUG_PRINTLN("🧹 BLE cleanup starting...");
        
        // 1. Arrêter publicité AVANT déconnexion
        if (_bluetoothInitialized) {
            //DEBUG_PRINTLN("🛑 Stopping advertising...");
            try {
                BLEDevice::getAdvertising()->stop();
                delay(200); // Attendre arrêt complet
            } catch (...) {
                //DEBUG_PRINTLN("⚠️ Advertising stop failed - continuing");
            }
        }
        
        // 2. Déconnecter clients proprement
        if (getDeviceConnected()) {
            //DEBUG_PRINTLN("🔌 Disconnecting clients...");
            
            // Méthode douce de déconnexion
            try {
                if (_pServer) {
                    BLEServer* pServer = (BLEServer*)_pServer;
                    pServer->disconnect(0); // Disconnect client 0
                    delay(500); // Attendre déconnexion
                }
                
                // Force si nécessaire
                esp_ble_gatts_close(0, 0);
                delay(200);
            } catch (...) {
                //DEBUG_PRINTLN("⚠️ Client disconnect failed - continuing");
            }
        }
        
        // 3. Nettoyer callbacks AVANT deinit
        //DEBUG_PRINTLN("🧹 Cleaning callbacks...");
        if (_serverCallbacks) {
            delete (DynamicBLEServerCallbacks*)_serverCallbacks;
            _serverCallbacks = nullptr;
        }
        
        if (_characteristicCallbacks) {
            delete (DynamicBLECharacteristicCallbacks*)_characteristicCallbacks;
            _characteristicCallbacks = nullptr;
        }
        
        // 4. Reset pointeurs AVANT deinit BLE
        _pTxCharacteristic = nullptr;
        _pRxCharacteristic = nullptr;
        _pService = nullptr;
        _pServer = nullptr;
        
        // 5. Attendre stabilisation
        delay(500);
        
        // 6. Deinitialiser BLE SANS forcer la libération mémoire
        if (_bluetoothInitialized) {
            //DEBUG_PRINTLN("🔄 Deinitializing BLE stack...");
            
            try {
                // Deinit BLE Device (va appeler les destructeurs)
                BLEDevice::deinit(false); // PAS de force cleanup
                delay(300);
                
                // Vérifier l'état avant disable
                esp_bt_controller_status_t bt_state = esp_bt_controller_get_status();
                if (bt_state == ESP_BT_CONTROLLER_STATUS_ENABLED) {
                    // Arrêter Bluedroid proprement
                    esp_err_t ret = esp_bluedroid_disable();
                    if (ret == ESP_OK) {
                        delay(100);
                        ret = esp_bluedroid_deinit();
                        if (ret != ESP_OK) {
                            //Serial.printf("⚠️ Bluedroid deinit failed: %s\n", esp_err_to_name(ret));
                        }
                    } else {
                        //Serial.printf("⚠️ Bluedroid disable failed: %s\n", esp_err_to_name(ret));
                    }
                    
                    // Arrêter BT Controller seulement si encore actif
                    bt_state = esp_bt_controller_get_status();
                    if (bt_state == ESP_BT_CONTROLLER_STATUS_ENABLED) {
                        ret = esp_bt_controller_disable();
                        if (ret == ESP_OK) {
                            delay(100);
                            ret = esp_bt_controller_deinit();
                            if (ret != ESP_OK) {
                                //Serial.printf("⚠️ BT controller deinit failed: %s\n", esp_err_to_name(ret));
                            }
                        }
                    }
                } else {
                    //DEBUG_PRINTLN("ℹ️ BT Controller already disabled");
                }
                
                // NE PAS libérer la mémoire BT - cause des crashes
                // esp_bt_controller_mem_release(ESP_BT_MODE_BTDM); // COMMENTÉ
                //DEBUG_PRINTLN("ℹ️ BT memory NOT released to avoid crash");
                
            } catch (...) {
                //DEBUG_PRINTLN("⚠️ Exception during BLE deinit - continuing");
            }
        }
        
        // 7. Reset état final
        setDeviceConnected(false);
        setOldDeviceConnected(false);
        setNewDataReceived(false);
        _receivedDataLength = 0;
        memset(_receivedData, 0, sizeof(_receivedData));
        
        //DEBUG_PRINTLN("✅ BLE cleanup completed safely (memory retained)");
        
    } catch (...) {
        //DEBUG_PRINTLN("⚠️ Exception during BLE cleanup - continuing anyway");
        
        // Force reset même en cas d'erreur
        _pServer = nullptr;
        _pTxCharacteristic = nullptr;
        _pRxCharacteristic = nullptr;
        _pService = nullptr;
        _serverCallbacks = nullptr;
        _characteristicCallbacks = nullptr;
        setDeviceConnected(false);
        setOldDeviceConnected(false);
    }
}

// Envoi de message
void DynamicBLEManager::sendMessage(const String& message) {
    if (!_isActive || !getDeviceConnected() || !_pTxCharacteristic) {
        return;
    }
    
    try {
        BLECharacteristic* pTx = (BLECharacteristic*)_pTxCharacteristic;
        pTx->setValue(message.c_str());
        pTx->notify();
        //DEBUG_PRINTLN("📤 BLE Sent: " + message);
        delay(100);
    } catch (...) {
        //DEBUG_PRINTLN("❌ Failed to send BLE message");
    }
}

// Parse credentials
WiFiCredentials DynamicBLEManager::parseCredentials(const String& data) {
    WiFiCredentials creds;
    
    //DEBUG_PRINTLN("🔍 Parsing credentials from: '" + data + "'");
    
    int sepPos = data.indexOf('|');
    //Serial.printf("📍 Separator position: %d\n", sepPos);
    
    if (sepPos > 0 && sepPos < data.length() - 1) {
        String ssid = data.substring(0, sepPos);
        String password = data.substring(sepPos + 1);
        
        // Nettoyer les données
        ssid.trim();
        password.trim();
        
        //DEBUG_PRINTLN("📝 Parsed SSID: '" + ssid + "'");
        //Serial.printf("📝 Parsed password length: %d\n", password.length());
        //DEBUG_PRINTLN("📝 Password preview: '" + password.substring(0, min(3, (int)password.length())) + "...'");
        
        if (ssid.length() > 0 && ssid.length() < sizeof(creds.ssid) &&
            password.length() < sizeof(creds.password)) {
            
            strncpy(creds.ssid, ssid.c_str(), sizeof(creds.ssid) - 1);
            strncpy(creds.password, password.c_str(), sizeof(creds.password) - 1);
            creds.ssid[sizeof(creds.ssid) - 1] = '\0';
            creds.password[sizeof(creds.password) - 1] = '\0';
            creds.valid = true;
            
            //DEBUG_PRINTLN("✅ Credentials successfully parsed and validated");
        } else {
            //Serial.printf("❌ Credential length validation failed:\n");
            //Serial.printf("   SSID length: %d (max: %zu)\n", ssid.length(), sizeof(creds.ssid) - 1);
            //Serial.printf("   Password length: %d (max: %zu)\n", password.length(), sizeof(creds.password) - 1);
        }
    } else {
        //DEBUG_PRINTLN("❌ Separator '|' not found or in wrong position");
        if (sepPos == -1) {
            //DEBUG_PRINTLN("💡 No '|' character found in data");
        } else if (sepPos == 0) {
            //DEBUG_PRINTLN("💡 Empty SSID (separator at beginning)");
        } else if (sepPos == data.length() - 1) {
            //DEBUG_PRINTLN("💡 Empty password (separator at end)");
        }
    }
    
    return creds;
}

// Validation credentials
bool DynamicBLEManager::validateCredentials(const WiFiCredentials& creds) {
    if (!creds.valid) return false;
    if (strlen(creds.ssid) == 0) return false;
    if (strlen(creds.ssid) > 32) return false;
    if (strlen(creds.password) > 64) return false;
    return true;
}

// Traitement des données reçues
void DynamicBLEManager::processReceivedData() {
    if (!_newDataReceived) return;
    
    _newDataReceived = false;
    
    //DEBUG_PRINTLN("🎉 ===== BLE DATA PROCESSING =====");
    //Serial.printf("   Length: %zu bytes\n", _receivedDataLength);
    //Serial.printf("   Raw data: '%s'\n", _receivedData);
    
    // //DEBUG hex
    //Serial.print("   Hex: ");
    for (size_t i = 0; i < _receivedDataLength; i++) {
        //Serial.printf("%02X ", (uint8_t)_receivedData[i]);
    }
    //DEBUG_PRINTLN();
    
    String data = String(_receivedData);
    data.trim();
    
    //DEBUG_PRINTLN("   Cleaned: '" + data + "'");
    
    // Commandes de test
    if (data.equalsIgnoreCase("PING")) {
        //DEBUG_PRINTLN("🏓 PING received");
        sendMessage("ok");
        return;
    }
    
    if (data.equalsIgnoreCase("TEST")) {
        //DEBUG_PRINTLN("🧪 TEST received");
        sendMessage("🧪 Callback is working correctly!");
        return;
    }
    
    if (data.equalsIgnoreCase("STATUS")) {
        //DEBUG_PRINTLN("📊 STATUS received");
        sendMessage("📡 BLE: Active, Callback: Working!");
        return;
    }
    
    // Parse credentials WiFi
    WiFiCredentials creds = parseCredentials(data);
    
    if (validateCredentials(creds)) {
        //DEBUG_PRINTLN("✅ CREDENTIALS PARSING SUCCESS!");
        //Serial.printf("   SSID: '%s'\n", creds.ssid);
        //Serial.printf("   Password length: %zu\n", strlen(creds.password));
        
        sendMessage("ok");
        delay(500);
        //sendMessage("💾 Saving and connecting...");
        
        // Notifier callback
        if (_credentialsCallback) {
            //DEBUG_PRINTLN("📞 Calling credentials callback...");
            _credentialsCallback(creds);
        } else {
            //DEBUG_PRINTLN("⚠️ No credentials callback configured!");
        }
        
    } else {
        //DEBUG_PRINTLN("❌ CREDENTIALS PARSING FAILED");
        sendMessage("❌ Invalid format. Use: SSID|PASSWORD");
        delay(500);
        sendMessage("💡 Example: MyWiFi|mypassword123");
    }
    
    //DEBUG_PRINTLN("================================");
}

// NOUVELLE MÉTHODE : Polling des caractéristiques BLE
void DynamicBLEManager::pollBLEData() {
    if (!_isActive || !getDeviceConnected()) {
        return;
    }
    
    try {
        // Polling de la characteristic RX
        BLECharacteristic* pRx = (BLECharacteristic*)_pRxCharacteristic;
        BLECharacteristic* pTx = (BLECharacteristic*)_pTxCharacteristic;
        
        if (pRx && pTx) {
            std::string rxValue = pRx->getValue();
            std::string txValue = pTx->getValue();
            
            String rxData = String(rxValue.c_str());
            String txData = String(txValue.c_str());
            
            // Variables statiques pour détecter les changements
            static String lastRxData = "";
            static String lastTxData = "";
            static unsigned long lastPollTime = 0;
            
            // Vérifier s'il y a de nouvelles données (toutes les 500ms minimum)
            if (millis() - lastPollTime > 500) {
                lastPollTime = millis();
                
                // Vérifier RX characteristic
                if (rxData.length() > 0 && rxData != lastRxData) {
                    //DEBUG_PRINTLN("🔍 POLLING: New data on RX characteristic");
                    setReceivedData(rxData.c_str(), rxData.length());
                    lastRxData = rxData;
                }
                
                // Vérifier TX characteristic (l'app écrit parfois sur TX)
                else if (txData.length() > 0 && 
                         !txData.startsWith("Ready:") && 
                         !txData.startsWith("✅") && 
                         !txData.startsWith("❌") && 
                         !txData.startsWith("💡") && 
                         !txData.startsWith("📡") &&
                         txData != lastTxData) {
                    //DEBUG_PRINTLN("🔍 POLLING: New data on TX characteristic");
                    setReceivedData(txData.c_str(), txData.length());
                    lastTxData = txData;
                }
            }
        }
        
    } catch (...) {
        // Ignore polling errors silencieusement
    }
}

// Gestion événements avec polling
void DynamicBLEManager::handleEvents() {
    if (!_isActive) return;
    
    // POLLING des données BLE (méthode principale)
    pollBLEData();
    
    // Traiter données reçues (par callback OU polling)
    if (_newDataReceived) {
        //DEBUG_PRINTLN("🔄 New BLE data detected, processing...");
        processReceivedData();
    }
    
    // Gestion reconnexions avec logs sécurisés (pas dans callbacks BLE)
    if (!getDeviceConnected() && getOldDeviceConnected()) {
        //DEBUG_PRINTLN("📱 BLE Device disconnected, restarting advertising...");
        delay(500);
        if (_pServer && _bluetoothInitialized) {
            try {
                BLEDevice::getAdvertising()->start();
                //DEBUG_PRINTLN("📡 BLE Advertising restarted successfully");
            } catch (...) {
                //DEBUG_PRINTLN("❌ Failed to restart BLE advertising");
            }
        }
        setOldDeviceConnected(getDeviceConnected());
    }
    
    if (getDeviceConnected() && !getOldDeviceConnected()) {
        //DEBUG_PRINTLN("📱 BLE Device connected - polling active");
        setOldDeviceConnected(getDeviceConnected());
        
        // Message de bienvenue APRÈS la connexion complète
        delay(1000); // Laisser le temps à la stack BLE de se stabiliser
        sendMessage("✅ Connected! Send: SSID|PASSWORD");
        delay(500);
        sendMessage("💡 Example: MyWiFi|mypassword123");
    }
    
    // //DEBUG périodique réduit
    static unsigned long lastDEBUG = 0;
    if (getDeviceConnected() && (millis() - lastDEBUG) > 20000) { // Toutes les 20s
        lastDEBUG = millis();
        //DEBUG_PRINTLN("📱 BLE connected, polling for data...");
        sendMessage("📡 Polling active - send credentials");
    }
}