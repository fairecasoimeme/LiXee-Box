#ifndef MQTT_H
#define MQTT_H

void mqttPublish(String IEEE, String cluster, String attribute, String type, String value);
void mqttDiagnostics();
void mqttHealthCheck();

// ========== NOUVELLES FONCTIONS POUR RECONNEXION ROBUSTE ==========
void mqttAutoReconnect();  // À appeler dans la boucle principale
void mqttResetReconnectionFlag();  // À appeler dans onMqttConnect

#endif