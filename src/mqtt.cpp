#include <Arduino.h>
#include "mqtt.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include <WiFiClient.h>
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

/* ===================== Verification prealable du serveur (anti boucle de reboot) =============
 *
 * Symptome traite : un port MQTT errone pointant sur un serveur WEB (typiquement 8123 = Home
 * Assistant, ou 80) provoquait un REBOOT EN BOUCLE de la box. La reponse "HTTP/1.1 400..." est
 * interpretee octet par octet par AsyncMqttClient comme des trames MQTT ('H' = 0x48 -> type 4 =
 * PUBACK, packetId = "TP" = 21584), jusqu'a tomber sur un type invalide : la lib fait alors
 * disconnect() mais CONTINUE de parser le buffer avec _currentParsedPacket resté NULL
 * -> LoadProhibited (EXCVADDR=0). Le seul message prevu, log_i("PROTOCOL VIOLATION"), est
 * neutralise par CORE_DEBUG_LEVEL=0 : l'utilisateur n'avait aucune information.
 *
 * On verifie donc AVANT de confier la connexion a la lib que le pair parle bien MQTT.
 * Le sondage envoie un CONNECT *sans identifiants* : on ne teste que le TYPE du paquet de
 * reponse (CONNACK), jamais le code retour. Un broker qui exige une authentification repond
 * CONNACK "not authorized" -- ce qui prouve deja que c'est un broker. Les identifiants ne sont
 * donc JAMAIS transmis a un serveur inconnu.
 */
#define MQTT_PROBE_TIMEOUT_MS 3000

// true si l'hote repond en MQTT. `err` recoit un motif lisible en cas d'echec.
static bool probeMqttServer(const char *host, uint16_t port, String &err) {
    WiFiClient probe;
    probe.setTimeout(MQTT_PROBE_TIMEOUT_MS / 1000);
    if (!probe.connect(host, port, MQTT_PROBE_TIMEOUT_MS)) {
        err = "hote injoignable sur " + String(host) + ":" + String(port);
        return false;
    }

    // CONNECT MQTT 3.1.1 minimal, client id dedie pour ne pas perturber la session reelle.
    static const char kProbeId[] = "lixee-probe";
    const uint8_t idLen = sizeof(kProbeId) - 1;
    uint8_t pkt[32];
    uint8_t n = 0;
    pkt[n++] = 0x10;                      // CONNECT
    pkt[n++] = (uint8_t)(10 + 2 + idLen); // remaining length
    pkt[n++] = 0x00; pkt[n++] = 0x04;
    pkt[n++] = 'M'; pkt[n++] = 'Q'; pkt[n++] = 'T'; pkt[n++] = 'T';
    pkt[n++] = 0x04;                      // niveau 4 (3.1.1)
    pkt[n++] = 0x02;                      // clean session, pas de credentials
    pkt[n++] = 0x00; pkt[n++] = 0x3C;     // keepalive 60 s
    pkt[n++] = 0x00; pkt[n++] = idLen;
    memcpy(&pkt[n], kProbeId, idLen); n += idLen;
    probe.write(pkt, n);
    probe.flush();

    uint32_t start = millis();
    while (!probe.available() && probe.connected() && (millis() - start) < MQTT_PROBE_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!probe.available()) {
        probe.stop();
        err = "aucune reponse du serveur (pas un broker MQTT ?)";
        return false;
    }

    int first = probe.read();
    probe.stop();

    // Seul le TYPE compte : 0x2x = CONNACK.
    if ((first >> 4) == 2) return true;

    if (first == 'H') {                   // "HTTP/..." : cas le plus frequent
        err = "le serveur repond en HTTP, ce n'est pas un broker MQTT "
              "(port " + String(port) + " : interface web ?)";
    } else {
        err = "reponse non MQTT (premier octet 0x" + String(first, HEX) + ")";
    }
    return false;
}

/* Verifie (une seule fois par couple hote:port) que la cible parle bien MQTT.
 * Le resultat est memorise : une cible validee n'est plus sondee, et si l'utilisateur corrige
 * sa configuration le couple change, donc la sonde est rejouee automatiquement.
 * DOIT etre consultee par TOUS les chemins de connexion : il en existe plusieurs (demarrage,
 * reconnexion automatique, sauvegarde de la config web) et n'en proteger qu'un ne sert a rien.
 */
bool mqttServerLooksValid() {
    static String probedTarget = "";
    static bool   probedOk     = false;

    String target = String(ConfigGeneral.servMQTT) + ":" + String(ConfigGeneral.portMQTT);
    if (target == probedTarget) return probedOk;

    String err;
    probedOk     = probeMqttServer(ConfigGeneral.servMQTT, atoi(ConfigGeneral.portMQTT), err);
    probedTarget = target;

    if (probedOk) {
        Serial.printf("[MQTT] Serveur %s valide (CONNACK recu)\n", target.c_str());
    } else {
        // Serial.printf et non log_i : ce dernier est neutralise par CORE_DEBUG_LEVEL=0,
        // ce qui laissait l'utilisateur sans le moindre indice avant le reboot.
        Serial.printf("[MQTT] Connexion refusee vers %s : %s\n", target.c_str(), err.c_str());
        Serial.println("[MQTT] Verifiez l'adresse et le port : un broker MQTT ecoute en general "
                       "sur 1883, PAS sur 8123/80/443 qui sont des interfaces web.");
    }
    return probedOk;
}

// Unique point d'entree pour etablir la connexion MQTT : refuse une cible qui ne parle pas MQTT.
bool mqttConnectChecked() {
    if (!mqttServerLooksValid()) return false;
    mqttClient.connect();
    return true;
}

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
    
    if (!mqttServerLooksValid()) {
        reconnectionInProgress = false;   // on retentera : le serveur peut revenir
        return;
    }

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
    mqttConnectChecked();
    
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
                tmpvalue += String(zclHexToSigned(value.c_str()));   // #34 : complement a deux 32 bits
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
                tmpvalue += String(zclHexToSigned(value.c_str()));   // #34 : complement a deux 32 bits
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
                tmpvalue.replace("<value>", String(zclHexToSigned(value.c_str())));   // #34
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