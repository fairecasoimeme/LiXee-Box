#include "alertHistory.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

#define AH_MAX  20
#define AH_FILE "/hst/alerts.json"

struct AlertEntry {
  uint32_t id;
  String   ts;
  String   type;
  int      level;
  String   title;
  String   message;
  float    value;
  float    threshold;
};

static AlertEntry* s_items = nullptr;   // file circulaire, s_items[(s_head - 1) % AH_MAX] = la plus recente
static int      s_head = 0, s_count = 0;
static uint32_t s_lastId = 0;
static SemaphoreHandle_t s_mx = nullptr;

struct AhGuard {
  AhGuard()  { if (s_mx) xSemaphoreTake(s_mx, portMAX_DELAY); }
  ~AhGuard() { if (s_mx) xSemaphoreGive(s_mx); }
};

static void push(const AlertEntry& e) {
  s_items[s_head] = e;
  s_head = (s_head + 1) % AH_MAX;
  if (s_count < AH_MAX) s_count++;
  if (e.id > s_lastId) s_lastId = e.id;
}

static void fill(JsonArray a, uint32_t since) {
  int first = (s_head + AH_MAX - s_count) % AH_MAX;
  for (int k = 0; k < s_count; k++) {
    const AlertEntry& e = s_items[(first + k) % AH_MAX];
    if (e.id <= since) continue;
    JsonObject o = a.createNestedObject();
    o["id"]        = e.id;
    o["ts"]        = e.ts;
    o["type"]      = e.type;
    o["level"]     = e.level;
    o["title"]     = e.title;
    o["message"]   = e.message;
    o["value"]     = e.value;
    o["threshold"] = e.threshold;
  }
}

static void saveFile() {
  SpiRamJsonDocument doc(16384);
  doc["lastId"] = s_lastId;
  fill(doc.createNestedArray("alerts"), 0);
  File f = LittleFS.open(AH_FILE, "w");
  if (!f) { log_e("[alertHistory] ecriture %s impossible", AH_FILE); return; }
  serializeJson(doc, f);
  f.close();
}

void alertHistoryBegin() {
  if (!s_mx) s_mx = xSemaphoreCreateMutex();
  if (!s_items) {
    void* mem = ps_malloc(sizeof(AlertEntry) * AH_MAX);
    if (!mem) mem = malloc(sizeof(AlertEntry) * AH_MAX);
    if (!mem) { log_e("[alertHistory] allocation impossible"); return; }
    s_items = static_cast<AlertEntry*>(mem);
    for (int i = 0; i < AH_MAX; i++) new (&s_items[i]) AlertEntry();
  }
  File f = LittleFS.open(AH_FILE, "r");
  if (!f) return;
  SpiRamJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { log_e("[alertHistory] %s illisible : %s", AH_FILE, err.c_str()); return; }
  AhGuard g;
  for (JsonObject o : doc["alerts"].as<JsonArray>()) {
    push({ o["id"] | 0u, o["ts"] | "", o["type"] | "", o["level"] | 0,
           o["title"] | "", o["message"] | "", o["value"] | 0.0f, o["threshold"] | 0.0f });
  }
  uint32_t last = doc["lastId"] | 0u;
  if (last > s_lastId) s_lastId = last;
}

void alertHistoryAdd(const String& title, const String& message, int level,
                     const char* alertType, float value, float threshold) {
  if (!s_items) return;
  AhGuard g;
  // Certaines alertes se repetent a chaque trame tant que la condition dure (depassement de
  // puissance signale par STGE...). Une repetition identique dans les 10 minutes n'occupe pas une
  // nouvelle place : sans cela, 20 copies chasseraient toutes les autres alertes.
  static uint32_t lastAddMs = 0;
  if (s_count > 0) {
    const AlertEntry& last = s_items[(s_head + AH_MAX - 1) % AH_MAX];
    if (last.title == title && last.message == message && millis() - lastAddMs < 600000UL) return;
  }
  lastAddMs = millis();
  AlertEntry e;
  e.id        = s_lastId + 1;
  e.ts        = FormattedDate;
  e.type      = (alertType && alertType[0]) ? alertType : "info";
  e.level     = level < 0 ? 0 : (level > 3 ? 3 : level);
  e.title     = title;
  e.message   = message;
  e.value     = value;
  e.threshold = threshold;
  push(e);
  // Les alertes sont rares : une ecriture chacune, comme notifications.json.
  saveFile();
}

String alertHistoryJson(uint32_t since) {
  SpiRamJsonDocument doc(16384);
  {
    AhGuard g;
    JsonArray a = doc.createNestedArray("alerts");
    if (s_items) fill(a, since);
    doc["lastId"] = s_lastId;
  }
  String out;
  serializeJson(doc, out);
  return out;
}
