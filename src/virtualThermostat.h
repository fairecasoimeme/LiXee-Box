#pragma once

#include <Arduino.h>
#include "config.h"   // définit struct VirtualThermostat + globals (vThermostats / vThermostatCount)

// Thermostat virtuel — Phase 0 (socle)
// Régulation TPI multi-zone : capteur de température (0402) + actionneur on/off (0006).
// Le modèle de données (struct VirtualThermostat + tableau global) est défini dans config.h.

// Initialise une zone avec les valeurs par défaut (config + état runtime).
void initThermostatDefaults(VirtualThermostat& t);

// Charge les zones depuis /config/thermostats.json (appelé au boot).
void loadThermostats();

// Sauvegarde les zones dans /config/thermostats.json (écriture atomique).
bool saveThermostats();

// Boucle de régulation — à appeler périodiquement (Task 30 s).
void regulationTick();

// Sérialise l'état des zones en JSON pour l'UI (endpoint /loadThermostats).
String thermostatsToJson();

// Modifie la consigne d'une zone et persiste. Retourne false si id invalide.
bool setThermostatSetpoint(int id, float value);

// Active/désactive une zone et persiste. À la désactivation, coupe l'actionneur par sécurité.
bool setThermostatEnabled(int id, bool enabled);

// Forçage manuel d'une zone : 0=auto, 1=marche forcée, 2=arrêt forcé (runtime, non persisté).
bool setThermostatForce(int id, int mode);

// Change le mode chaud/froid d'une zone (clim réversible) et persiste.
bool setThermostatMode(int id, bool heating);

// Active/désactive le mode hors-gel (toggle) : bascule la consigne sur frostTemp et la restaure ensuite.
bool setThermostatFrost(int id, bool on);
