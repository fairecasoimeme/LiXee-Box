#include <Arduino.h>
#include "mqtt.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include "smart_wifi_manager.h"

extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;

extern SmartWiFiManager smartWiFi;

extern unsigned long lastConnectionTest;
extern const unsigned long CONNECTION_TEST_INTERVAL;
extern bool connectionTestPending;
extern uint16_t lastTestPacketId;
extern bool reallyConnected;

// Variables pour la gestion robuste de la connexion
static unsigned long lastMqttPublish = 0;
static unsigned long lastHealthCheck = 0;
static uint16_t mqttPublishCount = 0;
static uint16_t mqttPublishErrors = 0;
static bool mqttHealthy = true;

// ========== NOUVELLES VARIABLES POUR RECONNEXION ROBUSTE ==========
static unsigned long lastReconnectAttempt = 0;
static const unsigned long MQTT_RECONNECT_DELAY = 5000; // 5 secondes entre les tentatives
static uint8_t reconnectAttempts = 0;
static const uint8_t MAX_RECONNECT_ATTEMPTS = 10; // Avant d'augmenter le délai
static bool reconnectionInProgress = false;

void mqttHealthCheck() {
    unsigned long now = millis();
    
    // Vérification périodique toutes les 30 secondes
    if (now - lastHealthCheck > 30000) {
        lastHealthCheck = now;
        
        if (mqttClient.connected()) {
            // Test de publication légère pour vérifier la connexion
            String testTopic = String(ConfigGeneral.headerMQTT) + "heartbeat";
            String testPayload = "{\"timestamp\":" + String(now) + "}";
            
            uint16_t packetId = mqttClient.publish(testTopic.c_str(), 0, false, testPayload.c_str());
            
            if (packetId > 0) {
                lastConnectionTest = now;
                mqttHealthy = true;
                reconnectAttempts = 0; // Reset compteur si connexion OK
                log_d("MQTT heartbeat sent: %d", packetId);
            } else {
                mqttPublishErrors++;
                log_w("MQTT heartbeat failed");
                
                // Si trop d'erreurs, forcer reconnexion
                if (mqttPublishErrors > 3) {
                    log_e("MQTT unhealthy, forcing reconnection");
                    mqttClient.disconnect(true);
                    mqttHealthy = false;
                    mqttPublishErrors = 0;
                }
            }
        } else {
            // Si pas connecté, tenter une reconnexion
            mqttHealthy = false;
        }
    }
}

// ========== NOUVELLE FONCTION : RECONNEXION AUTOMATIQUE ==========
void mqttAutoReconnect() {
    // Ne rien faire si déjà connecté
    if (mqttClient.connected()) {
        reconnectAttempts = 0;
        reconnectionInProgress = false;
        return;
    }
    
    // Ne rien faire si MQTT désactivé
    if (!ConfigSettings.enableMqtt) {
        return;
    }
    
    // Ne rien faire si WiFi non connecté
    if (!smartWiFi.isConnected()) {
        log_w("MQTT: WiFi not connected, waiting...");
        return;
    }
    
    unsigned long now = millis();
    
    // Calculer le délai entre tentatives (exponentiel backoff)
    unsigned long reconnectDelay = MQTT_RECONNECT_DELAY;
    if (reconnectAttempts > MAX_RECONNECT_ATTEMPTS) {
        // Après 10 tentatives, augmenter le délai à 30 secondes
        reconnectDelay = 30000;
    }
    
    // Vérifier si assez de temps s'est écoulé depuis la dernière tentative
    if (now - lastReconnectAttempt < reconnectDelay) {
        return;
    }
    
    // Éviter les reconnexions multiples simultanées
    if (reconnectionInProgress) {
        log_w("MQTT: Reconnection already in progress");
        return;
    }
    
    lastReconnectAttempt = now;
    reconnectAttempts++;
    reconnectionInProgress = true;
    
    log_i("🔄 MQTT: Reconnection attempt %d/%d", reconnectAttempts, MAX_RECONNECT_ATTEMPTS);
    log_i("   Server: %s:%s", ConfigGeneral.servMQTT, ConfigGeneral.portMQTT);
    
    // Forcer une déconnexion propre avant de reconnecter
    mqttClient.disconnect(true);
    vTaskDelay(pdMS_TO_TICKS(100)); // Petit délai pour la déconnexion
    
    // Reconfigurer le client
    mqttClient.setServer(ConfigGeneral.servMQTT, atoi(ConfigGeneral.portMQTT));
    mqttClient.setClientId(ConfigGeneral.clientIDMQTT);
    
    if (String(ConfigGeneral.userMQTT) != "") {
        mqttClient.setCredentials(ConfigGeneral.userMQTT, ConfigGeneral.passMQTT);
    }
    
    // Tenter la connexion
    mqttClient.connect();
    
    // La reconnection sera marquée comme terminée dans le callback onConnect ou après timeout
    vTaskDelay(pdMS_TO_TICKS(500)); // Laisser le temps à la connexion de s'établir
    
    if (!mqttClient.connected()) {
        reconnectionInProgress = false;
        log_w("❌ MQTT: Connection failed, will retry in %lu ms", reconnectDelay);
    }
}

void mqttPublish(String IEEE, String cluster, String attribute, String type, String value)
{
    unsigned long now = millis();
    lastMqttPublish = now;
    
    // Vérification de santé MQTT
    mqttHealthCheck();
    
    if (!mqttClient.connected()) {
        log_w("MQTT not connected, skipping publish");
        return;
    }
    
    String tmpvalue;
    String topic;
    uint16_t packetId = 0;
    
    try {
        if (ConfigGeneral.HAMQTT) { 
            tmpvalue = "{\"value_" + cluster + "_" + attribute + "\":";
            if (type == "string") {
                tmpvalue += "\"" + value + "\"";
            } else if (type == "numeric") {
                tmpvalue += String(strtol(value.c_str(), NULL, 16));
            }
            tmpvalue += "}";
            topic = ConfigGeneral.headerMQTT + IEEE + "_" + cluster + "_" + attribute + "/state";
            
            packetId = mqttClient.publish(topic.c_str(), 1, false, tmpvalue.c_str());
            
        } else if (ConfigGeneral.TBMQTT) {
            tmpvalue = "{\"" + IEEE + "\":[";
            tmpvalue += "{";
            tmpvalue += "\"" + cluster + "_" + attribute + "\" : ";
            if (type == "string") {
                tmpvalue += "\"" + value + "\"";
            } else if (type == "numeric") {
                tmpvalue += String(strtol(value.c_str(), NULL, 16));
            }
            tmpvalue += "}";
            tmpvalue += "]}";
            
            packetId = mqttClient.publish(ConfigGeneral.headerMQTT, 1, false, tmpvalue.c_str());
            
        } else if (ConfigGeneral.customMQTT) {
            tmpvalue = ConfigGeneral.customMQTTJson;
            tmpvalue.replace("<IEEE>", IEEE);
            tmpvalue.replace("<cluster>", cluster);
            tmpvalue.replace("<attribute>", attribute);
            if (type == "string") {
                tmpvalue.replace("<value>", "\"" + value + "\"");
            } else if (type == "numeric") {
                tmpvalue.replace("<value>", String(strtol(value.c_str(), NULL, 16)));
            }
            
            packetId = mqttClient.publish(ConfigGeneral.headerMQTT, 1, false, tmpvalue.c_str());
        }
        
        if (packetId > 0) {
            mqttPublishCount++;
            mqttPublishErrors = 0; // Reset erreurs sur succès
            log_v("MQTT publish success: %d", packetId);
        } else {
            mqttPublishErrors++;
            log_w("MQTT publish failed for %s", IEEE.c_str());
        }
        
    } catch (...) {
        log_e("Exception in mqttPublish");
        mqttPublishErrors++;
    }
    
    // Petit délai pour éviter la surcharge
    vTaskDelay(pdMS_TO_TICKS(10));
}

// Fonction de diagnostic MQTT
void mqttDiagnostics() {
    static unsigned long lastDiag = 0;
    unsigned long now = millis();
    
    if (now - lastDiag > 300000) { // Toutes les 5 minutes
        lastDiag = now;
        
        log_i("📊 MQTT Stats:");
        log_i("   Connected: %s", mqttClient.connected() ? "YES" : "NO");
        log_i("   Publications: %d", mqttPublishCount);
        log_i("   Errors: %d", mqttPublishErrors);
        log_i("   Healthy: %s", mqttHealthy ? "YES" : "NO");
        log_i("   Last publish: %lu ms ago", now - lastMqttPublish);
        log_i("   Last test: %lu ms ago", now - lastConnectionTest);
        log_i("   Reconnect attempts: %d", reconnectAttempts);
    }
}

// ========== FONCTION À AJOUTER DANS mqtt.h ==========
void mqttResetReconnectionFlag() {
    reconnectionInProgress = false;
    reconnectAttempts = 0;
}