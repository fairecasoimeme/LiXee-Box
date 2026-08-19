#ifndef MQTT_H
#define MQTT_H

void mqttPublish(String IEEE, String cluster, String attribute, String type, String value);
void mqttDiagnostics();
void mqttHealthCheck();

// ========== NOUVELLES FONCTIONS POUR RECONNEXION ROBUSTE ==========
void mqttAutoReconnect();  // À appeler dans la boucle principale
void mqttResetReconnectionFlag();  // À appeler dans onMqttConnect

// Vérifie que la cible configurée parle bien MQTT (sondage CONNACK, sans identifiants).
// Résultat mémorisé par couple hôte:port. Voir mqtt.cpp pour le détail.
bool mqttServerLooksValid();

// UNIQUE point d'entrée pour connecter le client MQTT : à utiliser partout à la place de
// mqttClient.connect(). Un serveur qui ne parle pas MQTT (ex. interface web sur 8123) renvoyait
// du HTTP, que la lib interprétait comme des trames MQTT jusqu'à déréférencer un pointeur nul
// (AsyncMqttClient.cpp:320) -> reboot en boucle. Renvoie false si la cible a été refusée.
bool mqttConnectChecked();

#endif