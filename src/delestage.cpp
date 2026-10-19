#include "delestage.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"
#include "device.h"
#include "onoff.h"
#include "powerStats.h"

extern DeviceList devices;
extern ConfigGeneralStruct ConfigGeneral;

#define DL_FILE        "/config/delestage.json"
#define DL_MAX         16
#define DL_RECUT_MS    60000UL   // re-coupure des prises rallumees a la main pendant le delestage

struct DlDevice {
  char ieee[20];
  char prev[8];     // etat 0006/0 avant la coupure, tel que stocke ("01", "00"...)
};

static DlDevice s_dev[DL_MAX];
static int      s_devCount = 0;
static bool     s_active = false;
static String   s_since, s_lastEnd;
static String   s_reason;          // "meter" (signal du compteur) ou "anticipation"
static uint32_t s_belowSince = 0, s_lastCut = 0, s_lastTick = 0;
static volatile bool s_restoreRequested = false;
static SemaphoreHandle_t s_mx = nullptr;

struct DlGuard {
  DlGuard()  { if (s_mx) xSemaphoreTake(s_mx, portMAX_DELAY); }
  ~DlGuard() { if (s_mx) xSemaphoreGive(s_mx); }
};

static DeviceData* deviceById(const String& id) {
  for (size_t i = 0; i < devices.size(); i++)
    if (devices[i]->getDeviceID() == id) return devices[i];
  return nullptr;
}

static bool isOn(const String& v) {
  return v.length() > 0 && strtol(v.c_str(), nullptr, 16) != 0;
}

static void sendOnOff(DeviceData* d, const char* value) {
  SendOnOffAction(d->getInfo().shortAddr.toInt(), d->getInfo().endpoint.toInt(), value);
}

// Liste "ieee1,ieee2" de la config -> appel de fn(ieee) pour chaque entree non vide.
template <typename F>
static void forEachConfigured(F fn) {
  const String& list = ConfigGeneral.delestage;
  int start = 0;
  while (start < (int)list.length()) {
    int comma = list.indexOf(',', start);
    if (comma < 0) comma = list.length();
    String id = list.substring(start, comma);
    id.trim();
    if (id.length() > 0 && id != "null") fn(id);
    start = comma + 1;
  }
}

static void saveFile() {
  SpiRamJsonDocument doc(4096);
  doc["state"]   = s_active ? "1" : "0";
  doc["dateOn"]  = s_since;
  doc["dateOff"] = s_lastEnd;
  doc["reason"]  = s_reason;
  for (int i = 0; i < s_devCount; i++) doc[s_dev[i].ieee] = s_dev[i].prev;
  File f = LittleFS.open(DL_FILE, "w");
  if (!f) { log_e("[delestage] ecriture %s impossible", DL_FILE); return; }
  serializeJson(doc, f);
  f.close();
}

void delestageBegin() {
  if (!s_mx) s_mx = xSemaphoreCreateMutex();
  File f = LittleFS.open(DL_FILE, "r");
  if (!f) return;
  SpiRamJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;
  DlGuard g;
  s_active  = String(doc["state"] | "0") == "1";
  s_since   = doc["dateOn"]  | "";
  s_lastEnd = doc["dateOff"] | "";
  s_reason  = doc["reason"]  | "";
  s_devCount = 0;
  for (JsonPair kv : doc.as<JsonObject>()) {
    const char* k = kv.key().c_str();
    if (!strcmp(k, "state") || !strcmp(k, "dateOn") || !strcmp(k, "dateOff") || !strcmp(k, "reason")) continue;
    if (s_devCount >= DL_MAX) break;
    strlcpy(s_dev[s_devCount].ieee, k, sizeof(s_dev[0].ieee));
    strlcpy(s_dev[s_devCount].prev, kv.value().as<String>().c_str(), sizeof(s_dev[0].prev));
    s_devCount++;
  }
  if (s_active) log_w("[delestage] delestage en cours depuis %s (restaure au demarrage)", s_since.c_str());
}

void delestageTrigger(const char* reason) {
  DlGuard g;
  if (s_active) {
    // Deja deleste : le compteur signale le depassement a chaque trame. On ne re-ecrit plus
    // le fichier (l'ancien code le faisait a chaque trame, et remplacait l'etat memorise par
    // "eteint", si bien qu'un rallumage aurait tout laisse eteint). On recoupe seulement, au
    // plus une fois par minute, une prise rallumee entre-temps.
    s_belowSince = 0;
    if (millis() - s_lastCut < DL_RECUT_MS) return;
    s_lastCut = millis();
    for (int i = 0; i < s_devCount; i++) {
      DeviceData* d = deviceById(s_dev[i].ieee);
      if (d && isOn(d->getValue("0006", "0"))) sendOnOff(d, "0");
    }
    return;
  }

  s_devCount = 0;
  forEachConfigured([](const String& id) {
    if (s_devCount >= DL_MAX) return;
    DeviceData* d = deviceById(id);
    if (!d) return;
    strlcpy(s_dev[s_devCount].ieee, id.c_str(), sizeof(s_dev[0].ieee));
    strlcpy(s_dev[s_devCount].prev, d->getValue("0006", "0").c_str(), sizeof(s_dev[0].prev));
    s_devCount++;
    sendOnOff(d, "0");
  });
  s_active = true;
  s_since = FormattedDate;
  s_reason = reason;
  s_belowSince = 0;
  s_lastCut = millis();
  saveFile();
  log_w("[delestage] %s : %d prise(s) coupee(s)", reason, s_devCount);
}

bool delestageRestore(bool manual) {
  DlGuard g;
  if (!s_active) return false;
  int n = 0;
  for (int i = 0; i < s_devCount; i++) {
    if (!isOn(s_dev[i].prev)) continue;              // eteinte avant la coupure : on la laisse
    DeviceData* d = deviceById(s_dev[i].ieee);
    if (!d) continue;
    sendOnOff(d, "1");
    n++;
  }
  s_active = false;
  s_lastEnd = FormattedDate;
  s_belowSince = 0;
  saveFile();
  log_w("[delestage] fin du delestage (%s) : %d prise(s) rallumee(s)", manual ? "manuelle" : "automatique", n);
  return true;
}

void delestageLoop() {
  if (millis() - s_lastTick < 1000) return;
  s_lastTick = millis();

  if (s_restoreRequested) {        // demande de la route web, executee ici (envois Zigbee)
    s_restoreRequested = false;
    delestageRestore(true);
    return;
  }

  long sub = powerStatsSubscribedVA();
  DeviceData* z = deviceById(String(ConfigGeneral.ZLinky));
  if (sub <= 0 || !z) return;      // sans puissance souscrite connue, pas de decision automatique
  // Le Linky coupe sur un depassement qui dure de l'ordre d'une minute, et en triphase sur
  // chaque phase : les decisions se prennent sur la charge de la derniere minute (total en
  // monophase, phase la plus chargee en triphase), pas sur une mesure isolee. A defaut (mesures
  // trop espacees), on se rabat sur la derniere mesure du total.
  long load = powerStatsMinuteLoadPct();
  if (load < 0) {
    long va = strtol(z->getValue("0B04", "1295").c_str(), nullptr, 16);
    load = (va < 0 ? 0 : va) * 100 / sub;
  }

  // Delestage anticipe (option) : couper avant que le compteur ne le fasse.
  if (!s_active) {
    int pct = ConfigGeneral.delestageAnticipPct;
    if (pct > 0 && load >= pct) delestageTrigger("anticipation");
    return;
  }
  if (ConfigGeneral.delestageRestoreMin <= 0) return;

  if (load >= ConfigGeneral.delestageRestorePct) { s_belowSince = 0; return; }
  if (s_belowSince == 0) { s_belowSince = millis(); return; }
  if (millis() - s_belowSince >= (uint32_t)ConfigGeneral.delestageRestoreMin * 60000UL) {
    delestageRestore(false);
  }
}

void delestageRequestRestore() { s_restoreRequested = true; }

String delestageJson() {
  SpiRamJsonDocument doc(4096);
  {
    DlGuard g;
    doc["active"] = s_active;
    if (s_since.length())   doc["since"]   = s_since;   else doc["since"]   = nullptr;
    if (s_lastEnd.length()) doc["lastEnd"] = s_lastEnd; else doc["lastEnd"] = nullptr;
    doc["restoreMinutes"] = ConfigGeneral.delestageRestoreMin;
    doc["restorePercent"] = ConfigGeneral.delestageRestorePct;
    doc["anticipationPercent"] = ConfigGeneral.delestageAnticipPct;
    if (s_reason.length()) doc["reason"] = s_reason; else doc["reason"] = nullptr;
    doc["minuteVA"] = powerStatsMinuteAvg();
    doc["minuteLoadPercent"] = powerStatsMinuteLoadPct();
    doc["subscribedVA"] = powerStatsSubscribedVA();
    JsonArray a = doc.createNestedArray("devices");
    auto add = [&](const String& id, bool cut) {
      for (JsonObject o : a) if (id == o["ieee"].as<const char*>()) return;
      DeviceData* d = deviceById(id);
      JsonObject o = a.createNestedObject();
      o["ieee"] = id;
      String name = d ? d->getInfo().alias : String("");
      if (name.length() == 0 && d) name = d->getInfo().model;
      o["name"] = name;
      o["cut"]  = cut;
    };
    if (s_active) for (int i = 0; i < s_devCount; i++) add(String(s_dev[i].ieee), true);
    forEachConfigured([&](const String& id) { add(id, false); });
  }
  String out;
  serializeJson(doc, out);
  return out;
}
