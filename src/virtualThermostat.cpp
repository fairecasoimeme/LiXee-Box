#include <Arduino.h>
#include "virtualThermostat.h"
#include "config.h"
#include "device.h"
#include "onoff.h"
#include "protocol.h"
#include "zigbee.h"
#include "presence.h"
#include "FS.h"
#include "LittleFS.h"
#include <ArduinoJson.h>

extern DeviceList devices;
extern String epochTime;
extern ConfigGeneralStruct ConfigGeneral;
int getCurrentTariffAttributeId();  // défini dans SimpleMeter.cpp
String tariffLabelForAttr(int attrId);  // défini dans lixee.cpp

// Globals (modèle de données déclaré extern dans config.h)
VirtualThermostat vThermostats[MAX_VTHERMOSTATS];
int vThermostatCount = 0;

// ---------- Helpers internes ----------

static DeviceData* findDevice(const char* ieee) {
  if (ieee == nullptr || ieee[0] == '\0') return nullptr;
  String id(ieee);
  for (size_t i = 0; i < devices.size(); i++) {
    if (devices[i]->getDeviceID() == id) return devices[i];
  }
  return nullptr;
}

static String jsonEscape(const char* s) {
  String r;
  for (const char* p = s; *p; p++) {
    if (*p == '"' || *p == '\\') r += '\\';
    r += *p;
  }
  return r;
}

// Au moins un capteur d'ouverture de la zone est-il ouvert ? (IAS Zone : cluster 0500 attr 2, bit 0)
static bool zoneWindowOpen(const VirtualThermostat& t) {
  for (int k = 0; k < t.openSensorCount; k++) {
    DeviceData* d = findDevice(t.openSensors[k]);
    if (!d) continue;
    String v = d->getValue("0500", "2");
    if (v.length() > 0) {
      long z = strtol(v.c_str(), nullptr, 16);
      if (z & 0x1) return true;  // bit alarme => ouvert
    }
  }
  return false;
}

// La zone est-elle occupée ? (true si aucun capteur de présence configuré)
static bool zoneOccupied(const VirtualThermostat& t) {
  if (strlen(t.presenceIEEE) == 0) return true;
  return getCurrentPresenceStatus(String(t.presenceIEEE));
}

// La zone est-elle autorisée à fonctionner maintenant ? (plage horaire / tarif Linky)
static bool zoneScheduleActive(const VirtualThermostat& t) {
  if (t.operMode == 0) return true;  // toujours

  if (t.operMode == 2) {  // tarif Linky : actif seulement pendant les périodes sélectionnées
    if (strlen(t.tariffPeriods) == 0) return true;  // aucune restriction définie
    int cur = getCurrentTariffAttributeId();        // 256/258/260/262/264/266
    String list = String(",") + t.tariffPeriods + ",";
    return list.indexOf("," + String(cur) + ",") >= 0;
  }

  // operMode == 1 : plages horaires "HH:MM-HH:MM,..."
  if (strlen(t.schedule) == 0) return true;
  int now = atoi(Hour) * 60 + atoi(Minute);
  String s(t.schedule);
  int start = 0;
  while (start < (int)s.length()) {
    int comma = s.indexOf(',', start);
    String seg = (comma < 0) ? s.substring(start) : s.substring(start, comma);
    seg.trim();
    int dash = seg.indexOf('-');
    if (dash > 0) {
      String a = seg.substring(0, dash);
      String b = seg.substring(dash + 1);
      int am = a.substring(0, 2).toInt() * 60 + a.substring(3).toInt();
      int bm = b.substring(0, 2).toInt() * 60 + b.substring(3).toInt();
      bool in = (am <= bm) ? (now >= am && now < bm) : (now >= am || now < bm);  // gère minuit
      if (in) return true;
    }
    if (comma < 0) break;
    start = comma + 1;
  }
  return false;
}

// Déclenche une action nommée du template de l'appareil (clim/fil pilote/etc.).
static bool fireDeviceAction(const char* ieee, const char* actionName) {
  if (ieee == nullptr || ieee[0] == '\0' || actionName == nullptr || actionName[0] == '\0') return false;
  DeviceData* d = findDevice(ieee);
  if (!d) return false;
  TemplateData* tpl = d->getTemplate();
  if (!tpl) return false;
  int sa = GetShortAddr(String(ieee) + ".json");
  String name(actionName);
  for (int i = 0; i < tpl->ActionSize(); i++) {
    if (name.equalsIgnoreCase(String(tpl->actions[i].name))) {
      Action& a = tpl->actions[i];
      if (a.command == 400) {
        SendActionEx(400, sa, a.endpoint, a.cluster, a.manufacturerCode, String(a.value));
      } else {
        SendAction(a.command, sa, a.endpoint, String(a.value));
      }
      return true;
    }
  }
  return false;
}

// Commande l'appareil de la zone vers l'état "on" (marche/chauffe/refroidit) ou "off" (repos).
// Si une action est configurée pour l'état visé, on la déclenche ; sinon on retombe sur on/off (cluster 0006).
// Envoie la commande à UN appareil donné (action si configurée, sinon on/off cluster 0006).
static void fireActuatorOne(const char* ieee, bool on, const char* name) {
  if (!ieee || ieee[0] == '\0') return;
  if (name[0] != '\0') {
    if (fireDeviceAction(ieee, name)) return;  // action déclenchée
  }
  // Repli : commande on/off classique
  String f = String(ieee) + ".json";
  int sa = GetShortAddr(f);
  int ep = GetEndpoint(f).toInt();
  if (ep <= 0) ep = 1;
  SendOnOffAction(sa, ep, on ? "1" : "0");
}

static void sendActuator(VirtualThermostat& t, bool on) {
  // En marche : action chaud ou froid selon le mode courant ; à l'arrêt : action arrêt.
  const char* name = on ? (t.heating ? t.actionHeat : t.actionCool) : t.actionOff;
  // Appareil principal + toutes les prises supplémentaires reçoivent la même commande
  fireActuatorOne(t.actuatorIEEE, on, name);
  for (int k = 0; k < t.actuatorsExtraCount; k++) {
    fireActuatorOne(t.actuatorsExtra[k], on, name);
  }
}

// Lit l'état réel de l'actionneur. Retourne 1 (actif), 0 (inactif) ou -1 (inconnu).
static int readActuatorState(const VirtualThermostat& t) {
  DeviceData* a = findDevice(t.actuatorIEEE);
  if (!a) return -1;
  // 1) Clim/thermostat HVAC : System Mode réel (cluster 0201, attr 0x001C stocké en "28").
  //    0=Off, 3=Cool, 4=Heat => reflète l'état réel même pour un appareil piloté par actions.
  String sm = a->getValue("0201", "28");
  if (sm.length() > 0) {
    return (strtol(sm.c_str(), nullptr, 16) != 0) ? 1 : 0;
  }
  // 2) Appareil piloté par actions sans System Mode lisible (fil pilote...) => état inconnu.
  if (t.actionHeat[0] != '\0' || t.actionCool[0] != '\0' || t.actionOff[0] != '\0') return -1;
  // 3) Prise / relais classique : on/off cluster 0006.
  String v = a->getValue("0006", "0");
  if (v.length() == 0) return -1;
  return (strtol(v.c_str(), nullptr, 16) != 0) ? 1 : 0;
}

// Aligne l'actionneur sur l'état désiré. La commande n'est envoyée QU'AU CHANGEMENT, jamais en double.
// Référence de comparaison selon le type d'appareil :
//  - Prise/relais "bête" (on/off cluster 0006) : on compare à l'ÉTAT RÉEL -> corrige un changement
//    manuel (renvoi une fois si l'état diverge).
//  - Clim/HVAC ou appareil piloté par actions : on compare au DERNIER ÉTAT COMMANDÉ (t.output).
//    On n'envoie donc que lorsque NOTRE décision change. On NE renvoie PAS la même commande pour
//    "corriger" l'état réel : une clim a son propre thermostat (elle se rallume/coupe seule) et on
//    se battrait avec elle en réémettant OFF/COOL en boucle. L'affichage (actualOn) reflète, lui,
//    l'état réel via le System Mode.
static void applyOutput(VirtualThermostat& t, bool desired, unsigned long nowMs) {
  bool actionBased = (t.actionHeat[0] != '\0' || t.actionCool[0] != '\0' || t.actionOff[0] != '\0');
  bool current;
  if (actionBased) {
    if (t.lastSwitchMs == 0) {
      // Aucune commande envoyée depuis le boot : t.output (runtime, non persisté) vaut false alors
      // que la clim peut déjà tourner. On adopte son ÉTAT RÉEL (System Mode) comme référence pour
      // ne PAS réémettre COOL/HEAT à chaque redémarrage de la box (sinon bip inutile). Si l'état réel
      // n'est pas encore connu (cache vide juste après boot), on suppose qu'on est déjà dans l'état
      // visé (pas d'envoi) en attendant le prochain report.
      int actual = readActuatorState(t);
      current = (actual >= 0) ? (actual != 0) : desired;
      t.output = current;
    } else {
      current = t.output;  // ensuite : on suit notre dernière décision (pas de bagarre avec la clim)
    }
  } else {
    int actual = readActuatorState(t);
    current = (actual >= 0) ? (actual != 0) : t.output;  // prise : état réel si lisible
  }

  if (desired != current) {
    unsigned long since = nowMs - t.lastSwitchMs;
    unsigned long minHold = current ? (unsigned long)t.minOnSec * 1000UL
                                     : (unsigned long)t.minOffSec * 1000UL;
    // Bloquer le basculement trop rapide (sauf au tout premier changement).
    // IMPORTANT : ne PAS toucher t.output ici. Pour un appareil piloté par action (état réel
    // inconnu), t.output sert de référence d'état ; le modifier ferait croire au tick suivant
    // que la bascule a eu lieu (desired==current) et la commande ne serait jamais envoyée
    // (ex : clim qui ne redémarre pas après refermeture d'une porte). On retentera au prochain tick.
    if (t.lastSwitchMs != 0 && since < minHold) {
      return;
    }
    sendActuator(t, desired);
    t.output = desired;
    t.lastSwitchMs = nowMs;
    t.lastAssertMs = nowMs;
  } else {
    // État déjà correct : on ne renvoie PAS la commande (évite les répétitions/bips)
    t.output = desired;
  }
}

// ---------- API ----------

void initThermostatDefaults(VirtualThermostat& t) {
  t.name[0] = '\0';
  t.sensorIEEE[0] = '\0';
  t.actuatorIEEE[0] = '\0';
  for (int k = 0; k < MAX_EXTRA_ACTUATORS; k++) t.actuatorsExtra[k][0] = '\0';
  t.actuatorsExtraCount = 0;
  t.actionHeat[0] = '\0';
  t.actionCool[0] = '\0';
  t.actionOff[0] = '\0';
  t.presenceIEEE[0] = '\0';
  for (int k = 0; k < MAX_OPEN_SENSORS; k++) t.openSensors[k][0] = '\0';
  t.openSensorCount = 0;
  t.enabled = true;
  t.heating = true;
  t.setpoint = 19.0f;
  t.frostTemp = 7.0f;
  t.frostMode = false;
  t.setpointSaved = 19.0f;
  t.tpiCycleSec = 900;
  t.tpiKp = 0.8f;
  t.tpiKi = 0.1f;
  t.minOnSec = 180;
  t.minOffSec = 180;
  t.sensorTimeoutSec = 3600;  // 60 min
  t.operMode = 0;
  t.schedule[0] = '\0';
  t.tariffPeriods[0] = '\0';
  t.priority = 0;
  t.nominalPowerW = 0;
  // runtime
  t.currentTemp = 0.0f;
  t.sensorValid = false;
  t.occupied = true;
  t.windowOpen = false;
  t.schedActive = true;
  t.forceMode = 0;
  t.duty = 0.0f;
  t.output = false;
  t.actualOn = false;
  t.integral = 0.0f;
  t.cycleStartMs = 0;
  t.lastSwitchMs = 0;
  t.lastSensorMs = 0;
  t.lastAssertMs = 0;
}

void loadThermostats() {
  vThermostatCount = 0;
  File f = LittleFS.open("/config/thermostats.json", FILE_READ);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    return;  // fichier pas encore créé : aucune zone
  }
  SpiRamJsonDocument doc(20000);
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) {
    log_e("thermostats.json: erreur de parsing %s", e.c_str());
    return;
  }
  JsonArray arr = doc["thermostats"].as<JsonArray>();
  for (JsonObject o : arr) {
    if (vThermostatCount >= MAX_VTHERMOSTATS) break;
    VirtualThermostat& t = vThermostats[vThermostatCount];
    initThermostatDefaults(t);
    strlcpy(t.name, o["name"] | "Zone", sizeof(t.name));
    strlcpy(t.sensorIEEE, o["sensorIEEE"] | "", sizeof(t.sensorIEEE));
    strlcpy(t.actuatorIEEE, o["actuatorIEEE"] | "", sizeof(t.actuatorIEEE));
    t.actuatorsExtraCount = 0;
    if (o["actuatorsExtra"].is<JsonArray>()) {
      for (JsonVariant v : o["actuatorsExtra"].as<JsonArray>()) {
        if (t.actuatorsExtraCount >= MAX_EXTRA_ACTUATORS) break;
        strlcpy(t.actuatorsExtra[t.actuatorsExtraCount], v.as<const char*>() ? v.as<const char*>() : "", 20);
        if (strlen(t.actuatorsExtra[t.actuatorsExtraCount]) > 0) t.actuatorsExtraCount++;
      }
    }
    strlcpy(t.actionHeat, o["actionHeat"] | (o["actionOn"] | ""), sizeof(t.actionHeat));  // migration actionOn->actionHeat
    strlcpy(t.actionCool, o["actionCool"] | "", sizeof(t.actionCool));
    strlcpy(t.actionOff, o["actionOff"] | "", sizeof(t.actionOff));
    strlcpy(t.presenceIEEE, o["presenceIEEE"] | "", sizeof(t.presenceIEEE));
    t.openSensorCount = 0;
    if (o["openSensors"].is<JsonArray>()) {
      for (JsonVariant v : o["openSensors"].as<JsonArray>()) {
        if (t.openSensorCount >= MAX_OPEN_SENSORS) break;
        strlcpy(t.openSensors[t.openSensorCount], v.as<const char*>() ? v.as<const char*>() : "", 20);
        if (strlen(t.openSensors[t.openSensorCount]) > 0) t.openSensorCount++;
      }
    }
    t.enabled = o["enabled"] | true;
    t.heating = o["heating"] | true;
    t.setpoint = o["setpoint"] | 19.0;
    t.frostTemp = o["frostTemp"] | 7.0;
    t.frostMode = o["frostMode"] | false;
    t.setpointSaved = o["setpointSaved"] | t.setpoint;
    t.tpiCycleSec = o["tpiCycleSec"] | 900;
    t.tpiKp = o["tpiKp"] | 0.8;
    t.tpiKi = o["tpiKi"] | 0.1;
    t.minOnSec = o["minOnSec"] | 180;
    t.minOffSec = o["minOffSec"] | 180;
    t.sensorTimeoutSec = o["sensorTimeoutSec"] | 3600;
    t.operMode = o["operMode"] | 0;
    strlcpy(t.schedule, o["schedule"] | "", sizeof(t.schedule));
    strlcpy(t.tariffPeriods, o["tariffPeriods"] | "", sizeof(t.tariffPeriods));
    t.priority = o["priority"] | 0;
    t.nominalPowerW = o["nominalPowerW"] | 0;
    vThermostatCount++;
  }
  log_d("Thermostats chargés: %d zone(s)", vThermostatCount);
}

bool saveThermostats() {
  const char* path = "/config/thermostats.json";
  const char* tmp = "/config/tmpThermostats.json";
  SpiRamJsonDocument doc(20000);
  JsonArray arr = doc.createNestedArray("thermostats");
  for (int i = 0; i < vThermostatCount; i++) {
    VirtualThermostat& t = vThermostats[i];
    if (strlen(t.name) == 0 && strlen(t.sensorIEEE) == 0 && strlen(t.actuatorIEEE) == 0) continue;
    JsonObject o = arr.createNestedObject();
    o["name"] = t.name;
    o["sensorIEEE"] = t.sensorIEEE;
    o["actuatorIEEE"] = t.actuatorIEEE;
    JsonArray ax = o.createNestedArray("actuatorsExtra");
    for (int k = 0; k < t.actuatorsExtraCount; k++) {
      if (strlen(t.actuatorsExtra[k]) > 0) ax.add(t.actuatorsExtra[k]);
    }
    o["actionHeat"] = t.actionHeat;
    o["actionCool"] = t.actionCool;
    o["actionOff"] = t.actionOff;
    o["presenceIEEE"] = t.presenceIEEE;
    JsonArray os = o.createNestedArray("openSensors");
    for (int k = 0; k < t.openSensorCount; k++) {
      if (strlen(t.openSensors[k]) > 0) os.add(t.openSensors[k]);
    }
    o["enabled"] = t.enabled;
    o["heating"] = t.heating;
    o["setpoint"] = t.setpoint;
    o["frostTemp"] = t.frostTemp;
    o["frostMode"] = t.frostMode;
    o["setpointSaved"] = t.setpointSaved;
    o["tpiCycleSec"] = t.tpiCycleSec;
    o["tpiKp"] = t.tpiKp;
    o["tpiKi"] = t.tpiKi;
    o["minOnSec"] = t.minOnSec;
    o["minOffSec"] = t.minOffSec;
    o["sensorTimeoutSec"] = t.sensorTimeoutSec;
    o["operMode"] = t.operMode;
    o["schedule"] = t.schedule;
    o["tariffPeriods"] = t.tariffPeriods;
    o["priority"] = t.priority;
    o["nominalPowerW"] = t.nominalPowerW;
  }
  File tf = LittleFS.open(tmp, FILE_WRITE);
  if (!tf) {
    log_e("thermostats: échec ouverture fichier temporaire");
    return false;
  }
  if (serializeJson(doc, tf) == 0) {
    log_e("thermostats: échec écriture JSON");
    tf.close();
    return false;
  }
  tf.close();
  if (!LittleFS.rename(tmp, path)) {
    log_e("thermostats: échec renommage");
    return false;
  }
  return true;
}

// Lecture seule des capteurs d'une zone (température, validité, présence, ouvertures).
// Ne touche ni à la régulation (duty/intégrale) ni à l'actionneur — utilisable depuis le web.
static void refreshZoneSensors(VirtualThermostat& t) {
  bool valid = false;
  float tC = t.currentTemp;
  DeviceData* s = findDevice(t.sensorIEEE);
  if (s) {
    // Sonde 0x0402, sinon température locale HVAC/clim 0x0201 attr 0
    int tempCluster = 1026;
    String raw = s->getValue("0402", "0");
    if (raw.length() == 0) raw = s->getValue("0402", "0000");
    if (raw.length() == 0) { raw = s->getValue("0201", "0"); if (raw.length() == 0) raw = s->getValue("0201", "0000"); if (raw.length() > 0) tempCluster = 513; }
    if (raw.length() > 0) {
      int16_t r = (int16_t)strtol(raw.c_str(), nullptr, 16);
      float coef = s->GetAttributeCoefficient(tempCluster, 0);
      if (coef == 0.0f) coef = 0.01f;  // sécurité si template sans coefficient
      tC = r * coef;
      bool online = getDeviceStatus(String(t.sensorIEEE) + ".json") != "d4";
      bool fresh = true;
      time_t now = (time_t)epochTime.toInt();
      if (now > 0) {
        time_t ls = s->getLastSeenEpoch();
        long timeout = (t.sensorTimeoutSec > 0) ? t.sensorTimeoutSec : 3600;
        if (ls > 0 && (now - ls) > timeout) fresh = false;  // mesure trop ancienne => périmé
      }
      valid = online && fresh;
    }
  }
  t.currentTemp = tC;
  t.sensorValid = valid;
  t.occupied = zoneOccupied(t);
  t.windowOpen = zoneWindowOpen(t);
  t.schedActive = zoneScheduleActive(t);
  // État affiché de l'actionneur = ÉTAT RÉEL lu sur l'appareil (reflète la réalité physique) :
  //  - Clim/HVAC : System Mode réel (0201/0x001C), désormais à jour grâce au bind/report corrigé.
  //  - Prise/relais : on/off réel (cluster 0006).
  //  - Si l'état réel n'est pas lisible : repli sur l'état commandé (t.output).
  // NB : la DÉCISION d'envoi (applyOutput) reste basée sur l'état commandé pour ne pas se battre
  // avec le thermostat interne de la clim ni réémettre de commandes identiques.
  int act = readActuatorState(t);
  t.actualOn = (act >= 0) ? (act != 0) : t.output;
}

void regulationTick() {
  unsigned long nowMs = millis();
  for (int i = 0; i < vThermostatCount; i++) {
    VirtualThermostat& t = vThermostats[i];
    if (!t.enabled) {
      // Sécurité : si une zone vient d'être désactivée, s'assurer que l'actionneur est coupé
      if (t.output) {
        sendActuator(t, false);
        t.output = false;
        t.lastSwitchMs = nowMs;
      }
      continue;
    }

    // 1) Lecture des capteurs (température, présence, ouvertures)
    refreshZoneSensors(t);

    // Au boot (aucune commande encore envoyée), adopter l'ÉTAT RÉEL de la clim comme référence
    // AVANT toute décision : t.output (runtime) repart à false au redémarrage de la box alors que la
    // clim peut déjà tourner. Sans ça, on réémet COOL/HEAT à chaque reboot (bip inutile).
    if (t.lastSwitchMs == 0 && (t.actionHeat[0] || t.actionCool[0] || t.actionOff[0])) {
      int aReal = readActuatorState(t);
      if (aReal >= 0) t.output = (aReal != 0);
    }

    float tC = t.currentTemp;
    bool valid = t.sensorValid;
    bool occupied = t.occupied;
    bool windowOpen = t.windowOpen;
    if (valid) t.lastSensorMs = nowMs;

    // 2) Failsafe : capteur HS => couper par sécurité
    bool desired;
    if (!valid) {
      desired = false;
      t.duty = 0.0f;
      t.integral = 0.0f;
    } else {
      // Hors-gel : sécurité prioritaire, ignore présence/ouverture/horaire
      bool frostOverride = (t.heating && tC < t.frostTemp);
      // Inhibition : fenêtre ouverte, zone inoccupée, ou hors plage/tarif => pas de chauffe/froid
      bool inhibit = (windowOpen || !occupied || !t.schedActive) && !frostOverride;

      if (frostOverride) {
        desired = true;
      } else if (t.forceMode == 1) {
        desired = true;   // marche forcée
      } else if (t.forceMode == 2) {
        desired = false;  // arrêt forcé
      } else if (inhibit) {
        // Suspension : on gèle l'intégrale (pas d'accumulation) pour éviter le windup
        desired = false;
      } else if (t.actionHeat[0] || t.actionCool[0] || t.actionOff[0]) {
        // Appareil piloté par actions (clim/fil pilote) : HYSTÉRÉSIS (pas de PWM).
        // La commande n'est envoyée qu'au franchissement du seuil -> pas de répétition / bips.
        float db = 0.3f;  // bande morte (°C)
        float error = t.heating ? (t.setpoint - tC) : (tC - t.setpoint);
        if (error > db) desired = true;
        else if (error < -db) desired = false;
        else desired = t.output;  // dans la bande morte : on garde l'état courant
        t.duty = desired ? 1.0f : 0.0f;
      } else {
        // TPI (PWM lent) pour les prises/relais tout-ou-rien : recalcul du duty à chaque fin de cycle
        if (t.cycleStartMs == 0 ||
            (nowMs - t.cycleStartMs) >= (unsigned long)t.tpiCycleSec * 1000UL) {
          t.cycleStartMs = nowMs;
          float error = t.heating ? (t.setpoint - tC) : (tC - t.setpoint);
          t.integral += error;
          // Anti-windup : borner l'intégrale pour que Ki*integral reste dans [0,1]
          if (t.tpiKi > 0.0f) {
            float iMax = 1.0f / t.tpiKi;
            if (t.integral > iMax) t.integral = iMax;
            if (t.integral < 0.0f) t.integral = 0.0f;
          } else {
            t.integral = 0.0f;
          }
          float duty = t.tpiKp * error + t.tpiKi * t.integral;
          if (duty < 0.0f) duty = 0.0f;
          if (duty > 1.0f) duty = 1.0f;
          t.duty = duty;
        }
        // PWM lent sur le cycle
        unsigned long elapsed = nowMs - t.cycleStartMs;
        unsigned long onMs = (unsigned long)(t.duty * (float)t.tpiCycleSec * 1000.0f);
        desired = (t.duty > 0.0f) && (elapsed < onMs);
      }
    }

    // 4) Anti-court-cycle + commande de l'actionneur
    applyOutput(t, desired, nowMs);
  }
}

String thermostatsToJson() {
  String j = "[";
  for (int i = 0; i < vThermostatCount; i++) {
    VirtualThermostat& t = vThermostats[i];
    // Rafraîchir les états de capteurs en direct pour que la fiche reflète tout changement immédiatement
    if (t.enabled) refreshZoneSensors(t);
    if (i) j += ",";
    j += "{\"id\":" + String(i);
    j += ",\"name\":\"" + jsonEscape(t.name) + "\"";
    j += ",\"enabled\":" + String(t.enabled ? 1 : 0);
    j += ",\"heating\":" + String(t.heating ? 1 : 0);
    j += ",\"setpoint\":" + String(t.setpoint, 1);
    j += ",\"frostTemp\":" + String(t.frostTemp, 1);
    if (t.sensorValid) j += ",\"temp\":" + String(t.currentTemp, 1);
    else               j += ",\"temp\":null";
    j += ",\"sensorValid\":" + String(t.sensorValid ? 1 : 0);
    j += ",\"occupied\":" + String(t.occupied ? 1 : 0);
    j += ",\"windowOpen\":" + String(t.windowOpen ? 1 : 0);
    j += ",\"hasPresence\":" + String(strlen(t.presenceIEEE) > 0 ? 1 : 0);
    j += ",\"openCount\":" + String(t.openSensorCount);
    j += ",\"actuatorCount\":" + String((strlen(t.actuatorIEEE) > 0 ? 1 : 0) + t.actuatorsExtraCount);
    j += ",\"operMode\":" + String(t.operMode);
    j += ",\"schedActive\":" + String(t.schedActive ? 1 : 0);
    if (t.operMode == 2) {
      String nm = tariffLabelForAttr(getCurrentTariffAttributeId());
      j += ",\"tariffNow\":\"" + jsonEscape(nm.c_str()) + "\"";
    } else {
      j += ",\"tariffNow\":\"\"";
    }
    j += ",\"output\":" + String(t.output ? 1 : 0);
    j += ",\"actualOn\":" + String(t.actualOn ? 1 : 0);
    j += ",\"forceMode\":" + String(t.forceMode);
    j += ",\"reversible\":" + String((t.actionHeat[0] && t.actionCool[0]) ? 1 : 0);
    j += ",\"frostMode\":" + String(t.frostMode ? 1 : 0);
    j += ",\"duty\":" + String((int)(t.duty * 100.0f));
    j += ",\"sensorIEEE\":\"" + String(t.sensorIEEE) + "\"";
    j += ",\"actuatorIEEE\":\"" + String(t.actuatorIEEE) + "\"";
    j += "}";
  }
  j += "]";
  return j;
}

bool setThermostatSetpoint(int id, float value) {
  if (id < 0 || id >= vThermostatCount) return false;
  if (value < 0.0f) value = 0.0f;
  if (value > 40.0f) value = 40.0f;
  vThermostats[id].setpoint = value;
  vThermostats[id].frostMode = false;  // réglage manuel => sort du mode hors-gel
  // Forcer un recalcul TPI immédiat au prochain tick
  vThermostats[id].integral = 0.0f;
  vThermostats[id].cycleStartMs = 0;
  return saveThermostats();
}

// Active/désactive le mode hors-gel (toggle) : mémorise la consigne puis la bascule sur frostTemp,
// et la restaure à la désactivation.
bool setThermostatFrost(int id, bool on) {
  if (id < 0 || id >= vThermostatCount) return false;
  VirtualThermostat& t = vThermostats[id];
  if (on) {
    if (!t.frostMode) { t.setpointSaved = t.setpoint; t.frostMode = true; }
    t.setpoint = t.frostTemp;
    t.forceMode = 0;
  } else {
    if (t.frostMode) { t.setpoint = t.setpointSaved; t.frostMode = false; }
  }
  t.integral = 0.0f;
  t.cycleStartMs = 0;
  return saveThermostats();
}

bool setThermostatMode(int id, bool heating) {
  if (id < 0 || id >= vThermostatCount) return false;
  VirtualThermostat& t = vThermostats[id];
  if (t.frostMode) { t.setpoint = t.setpointSaved; t.frostMode = false; }  // le hors-gel ne concerne que le chauffage
  t.heating = heating;
  t.integral = 0.0f;       // repartir sur une régulation propre
  t.cycleStartMs = 0;
  // Si en marche, ré-appliquer immédiatement l'action correspondant au nouveau mode
  if (t.output) { sendActuator(t, true); t.lastSwitchMs = millis(); }
  return saveThermostats();
}

bool setThermostatForce(int id, int mode) {
  if (id < 0 || id >= vThermostatCount) return false;
  VirtualThermostat& t = vThermostats[id];
  if (!t.enabled) return false;  // zone désactivée : forçage ignoré
  t.forceMode = mode;
  // Application immédiate de l'état forcé (le hors-gel restera prioritaire au prochain tick)
  if (mode == 1) { sendActuator(t, true);  t.output = true;  t.lastSwitchMs = millis(); }
  else if (mode == 2) { sendActuator(t, false); t.output = false; t.lastSwitchMs = millis(); }
  return true;  // état runtime, non persisté
}

bool setThermostatEnabled(int id, bool enabled) {
  if (id < 0 || id >= vThermostatCount) return false;
  VirtualThermostat& t = vThermostats[id];
  t.enabled = enabled;
  t.forceMode = 0;  // toute bascule active/inactive annule un forçage en cours
  if (!enabled) {
    // Sécurité : couper l'actionneur immédiatement
    if (t.output) {
      sendActuator(t, false);
      t.output = false;
      t.lastSwitchMs = millis();
    }
    t.duty = 0.0f;
    t.integral = 0.0f;
  } else {
    // Réactivation : repartir sur un cycle propre
    t.integral = 0.0f;
    t.cycleStartMs = 0;
  }
  return saveThermostats();
}
