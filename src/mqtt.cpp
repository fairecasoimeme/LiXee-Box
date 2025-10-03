#include <Arduino.h>
#include "mqtt.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>

extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;

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
        }
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
        
        log_i("MQTT Stats:");
        log_i("  Connected: %s", mqttClient.connected() ? "YES" : "NO");
        log_i("  Publications: %d", mqttPublishCount);
        log_i("  Errors: %d", mqttPublishErrors);
        log_i("  Healthy: %s", mqttHealthy ? "YES" : "NO");
        log_i("  Last publish: %lu ms ago", now - lastMqttPublish);
        log_i("  Last test: %lu ms ago", now - lastConnectionTest);
    }
}