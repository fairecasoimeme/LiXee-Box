#ifndef DYNAMIC_BLE_MANAGER_H
#define DYNAMIC_BLE_MANAGER_H

#include <Arduino.h>

// Forward declarations pour éviter d'inclure les headers BLE par défaut
class BLEServer;
class BLECharacteristic;
class BLEService;

// Structure pour les credentials WiFi
struct WiFiCredentials {
    char ssid[64];
    char password[64];
    bool valid;
    
    WiFiCredentials() : valid(false) {
        memset(ssid, 0, sizeof(ssid));
        memset(password, 0, sizeof(password));
    }
};

// Callbacks
typedef std::function<void(const WiFiCredentials&)> CredentialsCallback;
typedef std::function<void(const String&)> StatusCallback;

class DynamicBLEManager {
public:
    DynamicBLEManager();
    ~DynamicBLEManager();
    
    // Méthodes principales
    bool begin(const String& deviceName = "");
    void end();
    bool isActive() const { return _isActive; }
    bool isConnected() const { return getDeviceConnected(); }
    
    // Communication
    void sendMessage(const String& message);
    void handleEvents();
    
    // Callbacks
    void onCredentialsReceived(CredentialsCallback callback) { _credentialsCallback = callback; }
    void onStatusUpdate(StatusCallback callback) { _statusCallback = callback; }
    
    // Utilitaires
    static String generateDeviceName(const String& prefix = "LIXEEBOX");
    static bool isBLERequired(); // Détermine si BLE est nécessaire

private:
    // État
    bool _isActive;
    bool _deviceConnected;
    bool _oldDeviceConnected;
    String _deviceName;
    
    // Callbacks
    CredentialsCallback _credentialsCallback;
    StatusCallback _statusCallback;
    
    // Pointeurs vers objets BLE (allocation dynamique)
    void* _pServer;           // BLEServer* mais en void* pour éviter l'include
    void* _pTxCharacteristic; // BLECharacteristic*
    void* _pRxCharacteristic; // BLECharacteristic*
    void* _pService;          // BLEService*
    
    // Buffer thread-safe
    volatile bool _newDataReceived;
    char _receivedData[128];
    volatile size_t _receivedDataLength;
    
    // Méthodes privées
    bool loadBLELibrary();     // Charge dynamiquement les libs BLE
    void unloadBLELibrary();   // Décharge les libs BLE
    bool setupBLE();
    void cleanupBLE();
    void processReceivedData();
    void pollBLEData();        // NOUVEAU: Polling des données BLE
    WiFiCredentials parseCredentials(const String& data);
    bool validateCredentials(const WiFiCredentials& creds);
    
    // Flags pour suivi du chargement
    bool _bleLibrariesLoaded;
    bool _bluetoothInitialized;
    
    // Callbacks classes (pointeurs dynamiques)
    void* _serverCallbacks;
    void* _characteristicCallbacks;

public:
    // Méthodes pour les callbacks (accès depuis les classes friend)
    void setDeviceConnected(bool connected) { _deviceConnected = connected; }
    void setNewDataReceived(bool received) { _newDataReceived = received; }
    void setReceivedData(const char* data, size_t length) {
        if (length < sizeof(_receivedData) - 1) {
            _receivedDataLength = length;
            memcpy(_receivedData, data, length);
            _receivedData[length] = '\0';
            _newDataReceived = true;
        }
    }
    
    // Getters pour accès interne
    bool getDeviceConnected() const { return _deviceConnected; }
    bool getOldDeviceConnected() const { return _oldDeviceConnected; }
    void setOldDeviceConnected(bool old) { _oldDeviceConnected = old; }
    
private:
    friend void bleServerCallback_onConnect(void* server);
    friend void bleServerCallback_onDisconnect(void* server);
    friend void bleCharCallback_onWrite(void* characteristic);
};

// Fonctions globales pour les callbacks (évite les includes BLE)
void bleServerCallback_onConnect(void* server);
void bleServerCallback_onDisconnect(void* server);
void bleCharCallback_onWrite(void* characteristic);

#endif // DYNAMIC_BLE_MANAGER_H