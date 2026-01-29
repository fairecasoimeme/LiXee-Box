#ifndef PRESENCE_H_
#define PRESENCE_H_

#include <Arduino.h>

// Initialisation du système de présence
void presenceInit();

// Enregistrer un événement de présence
// deviceId: IEEE du capteur
// occupied: true si présence détectée, false sinon
void presenceRecordEvent(const String& deviceId, bool occupied);

// Vérifier si un device est un capteur de présence
bool isPresenceSensor(const String& model);

// Obtenir le résumé de présence pour un appareil et un jour (1-31)
// deviceId: IEEE du capteur
// dayNum: numéro du jour (1-31)
String getPresenceSummary(const String& deviceId, const String& dayNum);

// Obtenir le résumé combiné de tous les capteurs pour un jour (1-31)
String getPresenceSummaryAll(const String& dayNum);

// ============================================================================
// NOUVELLES FONCTIONS : 24H GLISSANTES
// ============================================================================

// Obtenir le résumé de présence sur 24h glissantes (tous les capteurs)
// Fusionne automatiquement les données d'hier et aujourd'hui
// Le tableau retourné est aligné sur le graphe d'énergie :
// - Index 0 = heure la plus ancienne (hier, heure actuelle + 1)
// - Index 23 = heure actuelle (aujourd'hui)
String getPresenceSummary24hSliding();

// Obtenir le résumé de présence sur 24h glissantes (capteur spécifique)
String getPresenceSummary24hSliding(const String& deviceId);

// ============================================================================

// Obtenir l'historique complet d'un appareil (jours 1-31)
String getPresenceHistory(const String& deviceId);

// Obtenir tout le fichier de présence
String getPresenceAll();

// Obtenir le statut de présence actuel
// deviceId: IEEE du capteur (ou "all" pour tous)
bool getCurrentPresenceStatus(const String& deviceId = "all");

// Réinitialiser un jour (appelé au changement de jour à minuit)
void presenceResetDay(int dayNum);

// Fonction d'intégration avec Zigbee
void handleOccupancyChange(const String& deviceId, const String& model, uint8_t occupancy);

#endif