#include "powerStats.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"
#include "device.h"

extern DeviceList devices;
extern ConfigGeneralStruct ConfigGeneral;
int tariffAttrFromPtec(String ptec);  // SimpleMeter.cpp
int getTariffFamily();               // lixee.cpp

#define PS_DAYS            90
#define PS_MONTHS          24
#define PS_FILE            "/hst/powerStats.json"
#define PS_TICK_MS         10000UL
#define PS_SAVE_MS         3600000UL      // au plus une ecriture par heure
#define PS_WINDOW_MS       60000UL        // moyenne sur 1 min : la duree qui fait couper le Linky
#define PS_PLATEAU_MIN     10             // plateau : moyenne sur 10 min consecutives
#define PS_MAX_GAP_MS      120000UL       // au-dela, la serie de mesures est rompue
#define PS_PHASE_FRESH_MS  60000UL        // triphase Standard : age max d'une phase pour le total
#define PS_SAMPLES         128
#define PS_MAX_TRANSITIONS 8
#define PS_OBSERVED_MIN    60             // un jour compte comme observe a partir d'1 h de mesures
#define PS_BANDS           6
#define PS_CH              4              // voie 0 = total, 1..3 = phases (triphase)
#define PS_JSON_DOC        131072

// Tranches de charge, en % de la puissance souscrite de la voie (moyenne sur 1 min >= tranche).
// Total : puissance souscrite ; phase : un tiers, c'est sur chaque phase que le Linky coupe.
static const uint8_t PS_BAND[PS_BANDS] = { 50, 60, 70, 80, 90, 100 };

struct ChanDay {
  int32_t  instVA;           // pic instantane (une mesure), -1 si inconnu
  int32_t  va1m;             // pic de la moyenne sur 1 min, -1 si inconnu
  int32_t  va10m;            // pic du plateau sur 10 min, -1 si inconnu
  uint16_t min[PS_BANDS];    // minutes passees dans chaque tranche
};

struct DayStat {
  char     date[11];         // "YYYY-MM-DD"
  int32_t  meterVA[3];       // SMAXSN (Standard) : [0] mono ou phase 1, [1..2] phases 2 et 3
  int32_t  floorW;           // plancher nocturne, -1 si inconnu
  int32_t  subVA;            // puissance souscrite ce jour-la
  uint8_t  tri;              // compteur triphase ce jour-la
  uint16_t obsMin;           // minutes mesurees
  ChanDay  ch[PS_CH];
};

struct ChanMonth {
  int32_t  instVA, va1m, va10m;
  uint32_t min[PS_BANDS];    // minutes cumulees dans chaque tranche
  uint16_t days[PS_BANDS];   // jours ayant touche chaque tranche
};

struct MonthStat {
  char      month[8];        // "YYYY-MM"
  int32_t   subVA;
  uint8_t   tri;
  uint16_t  obsDays;         // jours avec au moins PS_OBSERVED_MIN minutes mesurees
  ChanMonth ch[PS_CH];
};

struct Sample { uint32_t ms; int32_t va; };

// Etat de calcul d'une voie (non persiste)
struct ChanRt {
  Sample* samp = nullptr;    // file circulaire des mesures
  int     head = 0, count = 0;
  long    instPending = -1;  // pic instantane depuis le dernier traitement
  long    lastAvg = -1;      // derniere moyenne sur 1 min
  int32_t plat[PS_PLATEAU_MIN];
  int     platHead = 0, platCount = 0;
};

static DayStat*   s_days = nullptr;    // ordre chronologique, s_days[s_count-1] = aujourd'hui
static int        s_count = 0;
static MonthStat* s_months = nullptr;  // ordre chronologique
static int        s_mcount = 0;
static ChanRt     s_rt[PS_CH];
static SemaphoreHandle_t s_mx = nullptr;

static long     s_subVA = 0;
static long     s_phaseLast[PS_CH];    // triphase Standard : derniere puissance de chaque phase
static uint32_t s_phaseLastMs[PS_CH];
static uint32_t s_lastAvgMs = 0, s_minuteMs = 0;
static bool     s_dirty = false;
static uint32_t s_lastSave = 0, s_lastTick = 0;

// SMAXSN : au changement de jour, la valeur encore affichee est celle de la veille. On l'ignore
// tant qu'elle n'a pas change (le compteur repart de la puissance du moment a minuit).
static long s_stale1293 = -1;
static bool s_stale1293Active = false;
static long s_closeAtRollover[3] = { -1, -1, -1 };   // SMAXN-1 par phase (FF66/530..532)
static bool s_bootDone = false;

// Plancher : index (consommation, injection) releves au debut des heures 1 a 5.
struct Snap { bool ok; int64_t cons; int64_t inj; };
static Snap s_snap[6];
static int  s_lastHour = -1;
static char s_floorDoneDate[11] = "";

// Heures de bascule tarifaire observees, en minutes depuis minuit.
static int16_t s_tr[PS_MAX_TRANSITIONS];
static int     s_trCount = 0;
static int     s_lastAttr = 0;

struct PsGuard {
  PsGuard()  { if (s_mx) xSemaphoreTake(s_mx, portMAX_DELAY); }
  ~PsGuard() { if (s_mx) xSemaphoreGive(s_mx); }
};

static void* psAlloc(size_t n) {
  void* p = ps_malloc(n);
  if (!p) p = malloc(n);
  if (p) memset(p, 0, n);
  return p;
}

static DeviceData* zlinky() {
  if (ConfigGeneral.ZLinky[0] == '\0') return nullptr;
  String id(ConfigGeneral.ZLinky);
  for (size_t i = 0; i < devices.size(); i++)
    if (devices[i]->getDeviceID() == id) return devices[i];
  return nullptr;
}

static DeviceData* deviceById(const char* ieee) {
  if (!ieee || ieee[0] == '\0') return nullptr;
  String id(ieee);
  for (size_t i = 0; i < devices.size(); i++)
    if (devices[i]->getDeviceID() == id) return devices[i];
  return nullptr;
}

static long hexVal(DeviceData* d, const char* cluster, const char* attr) {
  String v = d->getValue(cluster, attr);
  if (v.length() == 0) return 0;
  return strtol(v.c_str(), nullptr, 16);
}

static int64_t hexVal64(DeviceData* d, const char* cluster, const char* attr) {
  String v = d->getValue(cluster, attr);
  if (v.length() == 0) return 0;
  return strtoll(v.c_str(), nullptr, 16);
}

static bool todayKey(char* out) {
  if (Year[0] == '\0' || Month[0] == '\0' || Day[0] == '\0') return false;
  if (atoi(Year) < 2024) return false;                // heure pas encore synchronisee
  snprintf(out, 11, "%s-%s-%s", Year, Month, Day);
  return strlen(out) == 10;
}

bool powerStatsIsStandard() {
  int m = ConfigGeneral.LinkyMode;
  return m == 1 || m == 3 || m == 5 || m == 7;
}

bool powerStatsIsTriphase() {
  int m = ConfigGeneral.LinkyMode;
  return m == 2 || m == 3 || m == 7;
}

// Puissance souscrite TOTALE en VA.
//  - Standard : PCOUP (0B01/14, puissance de coupure), a defaut PREF (0B01/13), en kVA.
//  - Historique : ISOUSC (0B01/13), en A, a 200 VA/A comme le reste du firmware ; en triphase
//    l'intensite souscrite est celle d'une phase.
static long computeSubscribedVA(DeviceData* z) {
  if (!z) return 0;
  if (powerStatsIsStandard()) {
    long kva = hexVal(z, "0B01", "14");
    if (kva <= 0) kva = hexVal(z, "0B01", "13");     // ZLinky LoRa : PREF range en 13
    return kva > 0 ? kva * 1000 : 0;
  }
  long a = hexVal(z, "0B01", "13");
  if (a <= 0) return 0;
  return a * 200 * (powerStatsIsTriphase() ? 3 : 1);
}

static long chanSub(int c) {
  if (s_subVA <= 0) return 0;
  return c == 0 ? s_subVA : s_subVA / 3;
}

long powerStatsSubscribedVA() { return s_subVA; }
long powerStatsPhaseSubscribedVA() { return powerStatsIsTriphase() ? s_subVA / 3 : 0; }

static bool avgFresh() { return millis() - s_lastAvgMs <= 2 * PS_TICK_MS; }

long powerStatsMinuteAvg() {
  if (!avgFresh()) return -1;
  return s_rt[0].lastAvg;
}

int powerStatsMinuteLoadPct() {
  if (!avgFresh() || s_subVA <= 0) return -1;
  if (!powerStatsIsTriphase()) {
    long a = s_rt[0].lastAvg;
    return a < 0 ? -1 : (int)(a * 100 / s_subVA);
  }
  long best = -1;                                      // triphase : la phase la plus chargee
  for (int p = 1; p <= 3; p++) {
    long a = s_rt[p].lastAvg;
    if (a < 0 || chanSub(p) <= 0) continue;
    long pct = a * 100 / chanSub(p);
    if (pct > best) best = pct;
  }
  return (int)best;
}

static void resetChanDay(ChanDay& c) {
  c.instVA = c.va1m = c.va10m = -1;
  memset(c.min, 0, sizeof(c.min));
}

static void resetDay(DayStat& d) {
  d.meterVA[0] = d.meterVA[1] = d.meterVA[2] = -1;
  d.floorW = -1;
  d.subVA = 0;
  d.tri = 0;
  d.obsMin = 0;
  for (int c = 0; c < PS_CH; c++) resetChanDay(d.ch[c]);
}

static void resetMonth(MonthStat& m) {
  m.subVA = 0;
  m.tri = 0;
  m.obsDays = 0;
  for (int c = 0; c < PS_CH; c++) {
    m.ch[c].instVA = m.ch[c].va1m = m.ch[c].va10m = -1;
    memset(m.ch[c].min, 0, sizeof(m.ch[c].min));
    memset(m.ch[c].days, 0, sizeof(m.ch[c].days));
  }
}

static bool chanHasData(const ChanDay& c) {
  if (c.instVA >= 0 || c.va1m >= 0 || c.va10m >= 0) return true;
  for (int k = 0; k < PS_BANDS; k++) if (c.min[k]) return true;
  return false;
}

// ---------------------------------------------------------------- persistance
// Jour : {"d","mt":[3],"f","sub","t","o","c":[[inst,1m,10m,[tranches]] x4]}
// Mois : {"m","sub","t","o","c":[[inst,1m,10m,[minutes],[jours]] x4]}
// Le format v2 (une seule voie : "m","i","s","p","b") est relu dans la voie 0.

static void loadFile() {
  File f = LittleFS.open(PS_FILE, "r");
  if (!f) return;
  SpiRamJsonDocument doc(PS_JSON_DOC);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { log_e("[powerStats] %s illisible : %s", PS_FILE, err.c_str()); return; }

  s_count = 0;
  for (JsonObject o : doc["days"].as<JsonArray>()) {
    if (s_count >= PS_DAYS) break;
    DayStat& d = s_days[s_count++];
    resetDay(d);
    strlcpy(d.date, o["d"] | "", sizeof(d.date));
    d.floorW = o["f"] | -1;
    d.subVA  = o["sub"] | 0;
    d.tri    = o["t"] | 0;
    d.obsMin = o["o"] | 0;
    if (o.containsKey("c")) {
      JsonArray mt = o["mt"];
      for (int k = 0; k < 3 && k < (int)mt.size(); k++) d.meterVA[k] = mt[k] | -1;
      JsonArray ca = o["c"];
      for (int c = 0; c < PS_CH && c < (int)ca.size(); c++) {
        JsonArray v = ca[c];
        if (v.size() < 4) continue;
        d.ch[c].instVA = v[0] | -1;
        d.ch[c].va1m   = v[1] | -1;
        d.ch[c].va10m  = v[2] | -1;
        JsonArray b = v[3];
        for (int k = 0; k < PS_BANDS && k < (int)b.size(); k++) d.ch[c].min[k] = b[k] | 0;
      }
    } else {                                          // format v2
      d.meterVA[0]   = o["m"] | -1;
      d.ch[0].instVA = o["i"] | -1;
      d.ch[0].va1m   = o["s"] | -1;
      d.ch[0].va10m  = o["p"] | -1;
      JsonArray b = o["b"];
      for (int k = 0; k < PS_BANDS && k < (int)b.size(); k++) d.ch[0].min[k] = b[k] | 0;
    }
  }

  s_mcount = 0;
  for (JsonObject o : doc["months"].as<JsonArray>()) {
    if (s_mcount >= PS_MONTHS) break;
    MonthStat& m = s_months[s_mcount++];
    resetMonth(m);
    strlcpy(m.month, o["m"] | "", sizeof(m.month));
    m.subVA   = o["sub"] | 0;
    m.tri     = o["t"] | 0;
    m.obsDays = o["o"] | 0;
    if (o.containsKey("c")) {
      JsonArray ca = o["c"];
      for (int c = 0; c < PS_CH && c < (int)ca.size(); c++) {
        JsonArray v = ca[c];
        if (v.size() < 5) continue;
        m.ch[c].instVA = v[0] | -1;
        m.ch[c].va1m   = v[1] | -1;
        m.ch[c].va10m  = v[2] | -1;
        JsonArray bm = v[3], bd = v[4];
        for (int k = 0; k < PS_BANDS && k < (int)bm.size(); k++) m.ch[c].min[k]  = bm[k] | 0u;
        for (int k = 0; k < PS_BANDS && k < (int)bd.size(); k++) m.ch[c].days[k] = bd[k] | 0;
      }
    } else {                                          // format v2
      m.ch[0].instVA = o["i"] | -1;
      m.ch[0].va1m   = o["s"] | -1;
      m.ch[0].va10m  = o["p"] | -1;
      JsonArray bm = o["bm"], bd = o["bd"];
      for (int k = 0; k < PS_BANDS && k < (int)bm.size(); k++) m.ch[0].min[k]  = bm[k] | 0u;
      for (int k = 0; k < PS_BANDS && k < (int)bd.size(); k++) m.ch[0].days[k] = bd[k] | 0;
    }
  }

  s_trCount = 0;
  for (JsonVariant v : doc["tr"].as<JsonArray>()) {
    if (s_trCount >= PS_MAX_TRANSITIONS) break;
    s_tr[s_trCount++] = v.as<int>();
  }
  strlcpy(s_floorDoneDate, doc["fd"] | "", sizeof(s_floorDoneDate));
}

static void saveFile() {
  SpiRamJsonDocument doc(PS_JSON_DOC);
  doc["v"] = 3;
  JsonArray a = doc.createNestedArray("days");
  for (int i = 0; i < s_count; i++) {
    const DayStat& d = s_days[i];
    JsonObject o = a.createNestedObject();
    o["d"] = d.date;
    JsonArray mt = o.createNestedArray("mt");
    for (int k = 0; k < 3; k++) mt.add(d.meterVA[k]);
    if (d.floorW >= 0) o["f"] = d.floorW;
    if (d.subVA > 0) o["sub"] = d.subVA;
    if (d.tri) o["t"] = 1;
    o["o"] = d.obsMin;
    JsonArray ca = o.createNestedArray("c");
    for (int c = 0; c < PS_CH; c++) {
      JsonArray v = ca.createNestedArray();
      if (!chanHasData(d.ch[c])) continue;            // voie inutilisee : tableau vide
      v.add(d.ch[c].instVA); v.add(d.ch[c].va1m); v.add(d.ch[c].va10m);
      JsonArray b = v.createNestedArray();
      for (int k = 0; k < PS_BANDS; k++) b.add(d.ch[c].min[k]);
    }
  }
  JsonArray ma = doc.createNestedArray("months");
  for (int i = 0; i < s_mcount; i++) {
    const MonthStat& m = s_months[i];
    JsonObject o = ma.createNestedObject();
    o["m"] = m.month;
    if (m.subVA > 0) o["sub"] = m.subVA;
    if (m.tri) o["t"] = 1;
    o["o"] = m.obsDays;
    JsonArray ca = o.createNestedArray("c");
    for (int c = 0; c < PS_CH; c++) {
      JsonArray v = ca.createNestedArray();
      if (m.ch[c].instVA < 0 && m.ch[c].va1m < 0 && m.ch[c].va10m < 0) continue;
      v.add(m.ch[c].instVA); v.add(m.ch[c].va1m); v.add(m.ch[c].va10m);
      JsonArray bm = v.createNestedArray();
      JsonArray bd = v.createNestedArray();
      for (int k = 0; k < PS_BANDS; k++) { bm.add(m.ch[c].min[k]); bd.add(m.ch[c].days[k]); }
    }
  }
  JsonArray t = doc.createNestedArray("tr");
  for (int i = 0; i < s_trCount; i++) t.add(s_tr[i]);
  doc["fd"] = s_floorDoneDate;

  File f = LittleFS.open(PS_FILE, "w");
  if (!f) { log_e("[powerStats] ecriture %s impossible", PS_FILE); return; }
  serializeJson(doc, f);
  f.close();
  s_dirty = false;
  s_lastSave = millis();
}

// ---------------------------------------------------------------- jours et mois

// Entree du jour, creee si besoin (rotation sur PS_DAYS). Appelee sous mutex.
static DayStat* today(bool* created = nullptr) {
  char key[11];
  if (!todayKey(key)) return nullptr;
  if (s_count > 0 && strcmp(s_days[s_count - 1].date, key) == 0) return &s_days[s_count - 1];
  if (s_count > 0 && strcmp(s_days[s_count - 1].date, key) > 0) return nullptr; // horloge en arriere
  if (s_count >= PS_DAYS) {
    memmove(&s_days[0], &s_days[1], sizeof(DayStat) * (PS_DAYS - 1));
    s_count = PS_DAYS - 1;
  }
  DayStat& d = s_days[s_count++];
  resetDay(d);
  strlcpy(d.date, key, sizeof(d.date));
  s_dirty = true;
  if (created) *created = true;
  return &d;
}

// Entree du mois de `d`, creee si besoin (rotation sur PS_MONTHS). Appelee sous mutex.
static MonthStat* monthOf(const DayStat* d) {
  char key[8];
  strlcpy(key, d->date, sizeof(key));                   // "YYYY-MM"
  if (s_mcount > 0 && strcmp(s_months[s_mcount - 1].month, key) == 0) return &s_months[s_mcount - 1];
  if (s_mcount > 0 && strcmp(s_months[s_mcount - 1].month, key) > 0) return nullptr;
  if (s_mcount >= PS_MONTHS) {
    memmove(&s_months[0], &s_months[1], sizeof(MonthStat) * (PS_MONTHS - 1));
    s_mcount = PS_MONTHS - 1;
  }
  MonthStat& m = s_months[s_mcount++];
  resetMonth(m);
  strlcpy(m.month, key, sizeof(m.month));
  return &m;
}

static void keepMax(int32_t& dst, long v) { if (v > dst) { dst = v; s_dirty = true; } }

// Une minute ecoulee sur la voie c : moyenne `avg` (-1 si la minute n'est pas couverte).
static void processMinute(DayStat* d, MonthStat* m, int c, long avg) {
  ChanRt& r = s_rt[c];
  if (avg < 0) { r.platCount = 0; return; }             // plateau interrompu

  r.plat[r.platHead] = avg;
  r.platHead = (r.platHead + 1) % PS_PLATEAU_MIN;
  if (r.platCount < PS_PLATEAU_MIN) r.platCount++;
  if (r.platCount == PS_PLATEAU_MIN) {
    long sum = 0;
    for (int k = 0; k < PS_PLATEAU_MIN; k++) sum += r.plat[k];
    long p = sum / PS_PLATEAU_MIN;
    keepMax(d->ch[c].va10m, p);
    if (m) keepMax(m->ch[c].va10m, p);
  }

  long sub = chanSub(c);
  if (sub > 0) {
    long pct = avg * 100 / sub;
    for (int k = 0; k < PS_BANDS; k++) {
      if (pct < PS_BAND[k]) break;
      if (d->ch[c].min[k] == 0 && m) m->ch[c].days[k]++;
      if (d->ch[c].min[k] < 0xFFFF) d->ch[c].min[k]++;
      if (m) m->ch[c].min[k]++;
    }
  }
  s_dirty = true;
}

// ---------------------------------------------------------------- mesures

// Ajoute une mesure a la voie c. Appelee sous mutex.
static void pushSample(int c, long va) {
  ChanRt& r = s_rt[c];
  if (!r.samp) return;
  if (va < 0) va = 0;                                   // injection : pas une consommation
  long sub = chanSub(c);
  if (sub > 0 && va > sub * 3 / 2) return;              // valeur aberrante
  if (va > r.instPending) r.instPending = va;
  uint32_t now = millis();
  if (r.count > 0) {
    Sample& last = r.samp[(r.head + PS_SAMPLES - 1) % PS_SAMPLES];
    if (now - last.ms > PS_MAX_GAP_MS) {
      r.count = 0;                                      // serie rompue
    } else if (now - last.ms < 1000) {
      last.va = va;                                     // rafale : on garde la derniere valeur
      return;
    }
  }
  r.samp[r.head] = { now, (int32_t)va };
  r.head = (r.head + 1) % PS_SAMPLES;
  if (r.count < PS_SAMPLES) r.count++;
}

void powerStatsOnAttribute(int attr, long value) {
  if (!s_days) return;
  if (value < 0) value = 0;
  PsGuard g;
  if (!powerStatsIsTriphase()) {
    if (attr == 1295) pushSample(0, value);            // PAPP / SINSTS
    return;
  }
  if (powerStatsIsStandard()) {
    // Standard triphase : SINSTS1..3 par phase, total = leur somme
    int ph = (attr == 1295) ? 1 : (attr == 2319) ? 2 : (attr == 2575) ? 3 : 0;
    if (!ph) return;
    pushSample(ph, value);
    uint32_t now = millis();
    s_phaseLast[ph] = value;
    s_phaseLastMs[ph] = now ? now : 1;
    long sum = 0;
    for (int p = 1; p <= 3; p++) {
      if (s_phaseLastMs[p] == 0 || now - s_phaseLastMs[p] > PS_PHASE_FRESH_MS) return;
      sum += s_phaseLast[p];
    }
    pushSample(0, sum);
  } else {
    // Historique triphase : PAPP totale, phases d'apres IINST1..3 (A, 200 VA/A)
    if (attr == 1295) { pushSample(0, value); return; }
    int ph = (attr == 1288) ? 1 : (attr == 2312) ? 2 : (attr == 2568) ? 3 : 0;
    if (ph) pushSample(ph, value * 200);
  }
}

// Moyenne ponderee par le temps sur [now - 60 s, now], -1 si la fenetre n'est pas couverte.
static long windowAverage(const ChanRt& r, uint32_t now) {
  if (!r.samp || r.count < 2) return -1;
  int first = (r.head + PS_SAMPLES - r.count) % PS_SAMPLES;
  const Sample& newest = r.samp[(r.head + PS_SAMPLES - 1) % PS_SAMPLES];
  if (now - newest.ms > PS_MAX_GAP_MS) return -1;       // plus de mesures : rien de fiable
  if (now - r.samp[first].ms < PS_WINDOW_MS) return -1; // fenetre pas encore couverte
  uint32_t winStart = now - PS_WINDOW_MS;
  double acc = 0;
  for (int k = 0; k < r.count; k++) {
    const Sample& s = r.samp[(first + k) % PS_SAMPLES];
    uint32_t segEnd = (k + 1 < r.count) ? r.samp[(first + k + 1) % PS_SAMPLES].ms : now;
    uint32_t a = (s.ms < winStart) ? winStart : s.ms;
    if (segEnd <= a) continue;
    acc += (double)s.va * (segEnd - a);
  }
  return (long)(acc / PS_WINDOW_MS);
}

static bool readIndexes(DeviceData* z, int64_t& cons, int64_t& inj) {
  cons = 0;
  char a[6];
  for (int attr = 256; attr <= 272; attr += 2) {
    snprintf(a, sizeof(a), "%d", attr);
    cons += hexVal64(z, "0702", a);
  }
  if (cons == 0) cons = hexVal64(z, "0702", "0");       // Historique BASE : index total seul
  inj = hexVal64(z, "0702", "1");
  DeviceData* p = deviceById(ConfigGeneral.Production);
  if (p && p != z) inj += hexVal64(p, "0702", "1");
  return cons > 0;
}

static void recordTransition(int minuteOfDay) {
  for (int i = 0; i < s_trCount; i++) {
    int d = abs(s_tr[i] - minuteOfDay);
    if (d <= 10 || d >= 1430) { s_tr[i] = minuteOfDay; s_dirty = true; return; }
  }
  if (s_trCount >= PS_MAX_TRANSITIONS) {
    memmove(&s_tr[0], &s_tr[1], sizeof(int16_t) * (PS_MAX_TRANSITIONS - 1));
    s_trCount--;
  }
  s_tr[s_trCount++] = minuteOfDay;
  s_dirty = true;
}

void powerStatsBegin() {
  if (!s_mx) s_mx = xSemaphoreCreateMutex();
  if (!s_days)   s_days   = (DayStat*)psAlloc(sizeof(DayStat) * PS_DAYS);
  if (!s_months) s_months = (MonthStat*)psAlloc(sizeof(MonthStat) * PS_MONTHS);
  for (int c = 0; c < PS_CH; c++)
    if (!s_rt[c].samp) s_rt[c].samp = (Sample*)psAlloc(sizeof(Sample) * PS_SAMPLES);
  if (!s_days || !s_months || !s_rt[0].samp) { log_e("[powerStats] allocation impossible"); return; }
  PsGuard g;
  loadFile();
  s_lastSave = millis();
}

void powerStatsLoop() {
  if (!s_days || !s_months) return;
  uint32_t nowMs = millis();
  if (nowMs - s_lastTick < PS_TICK_MS) return;
  s_lastTick = nowMs;

  DeviceData* z = zlinky();
  if (!z) return;
  bool standard = powerStatsIsStandard();
  bool tri = powerStatsIsTriphase();
  long subVA = computeSubscribedVA(z);
  long v1293 = hexVal(z, "0B04", "1293");
  long vClose[3] = { hexVal(z, "FF66", "530"),
                     tri ? hexVal(z, "FF66", "531") : 0,
                     tri ? hexVal(z, "FF66", "532") : 0 };
  int  attr  = powerStatsCurrentIndexAttr();
  int  hour  = atoi(Hour);
  int  minute = atoi(Minute);

  int64_t cons = 0, inj = 0;
  bool idxOk = false;
  bool hourChanged = (s_lastHour >= 0 && hour != s_lastHour);
  if (hourChanged && hour >= 1 && hour <= 5) idxOk = readIndexes(z, cons, inj);

  PsGuard g;
  s_subVA = subVA;
  bool created = false;
  DayStat* d = today(&created);
  if (!d) return;
  MonthStat* m = monthOf(d);

  if (!s_bootDone) {
    s_bootDone = true;
    for (int k = 0; k < 3; k++) s_closeAtRollover[k] = vClose[k];
    s_minuteMs = nowMs;
    // Redemarrage dans la journee : SMAXSN ne peut que croitre, une valeur au moins egale a
    // celle deja retenue pour aujourd'hui est donc bien celle du jour. Sinon (premier
    // demarrage du jour), la valeur restituee peut dater d'hier : on attend qu'elle change.
    s_stale1293 = v1293;
    s_stale1293Active = !(d->meterVA[0] > 0 && v1293 >= d->meterVA[0]);
    memset(s_snap, 0, sizeof(s_snap));
  } else if (created) {
    s_stale1293 = v1293;
    s_stale1293Active = true;
    for (int k = 0; k < 3; k++) s_closeAtRollover[k] = vClose[k];
    memset(s_snap, 0, sizeof(s_snap));
  }

  // --- Pic officiel (Standard) : SMAXSN du jour (phase 1 en triphase), SMAXN-1 par phase
  if (standard) {
    if (s_stale1293Active && v1293 != s_stale1293) s_stale1293Active = false;
    if (!s_stale1293Active && v1293 > 0) keepMax(d->meterVA[0], v1293);
    if (s_count >= 2) {
      DayStat& y = s_days[s_count - 2];
      for (int k = 0; k < 3; k++) {
        if (vClose[k] > 0 && vClose[k] != s_closeAtRollover[k]) {
          if (y.meterVA[k] != vClose[k]) { y.meterVA[k] = vClose[k]; s_dirty = true; }
          s_closeAtRollover[k] = vClose[k];
        }
      }
    }
  }

  // --- Par voie : pic instantane, moyenne sur 1 min (ce qui fait couper le Linky), minutes
  bool minuteDue = (nowMs - s_minuteMs >= PS_WINDOW_MS);
  if (minuteDue) s_minuteMs = nowMs;
  bool anyMeasured = false;
  int lastCh = tri ? 3 : 0;
  for (int c = 0; c <= lastCh; c++) {
    ChanRt& r = s_rt[c];
    if (r.instPending > 0) {
      keepMax(d->ch[c].instVA, r.instPending);
      if (m) keepMax(m->ch[c].instVA, r.instPending);
    }
    r.instPending = -1;

    long avg = windowAverage(r, nowMs);
    r.lastAvg = avg;
    long sub = chanSub(c);
    if (avg > 0 && (sub <= 0 || avg <= sub * 3 / 2)) {
      keepMax(d->ch[c].va1m, avg);
      if (m) keepMax(m->ch[c].va1m, avg);
    }
    if (minuteDue) {
      if (avg >= 0) anyMeasured = true;
      processMinute(d, m, c, avg);
    }
  }
  s_lastAvgMs = nowMs;
  if (minuteDue && anyMeasured) {
    d->obsMin++;
    if (d->obsMin == PS_OBSERVED_MIN && m) m->obsDays++;
    if (s_subVA > 0) { d->subVA = s_subVA; if (m) m->subVA = s_subVA; }
    d->tri = tri;
    if (m) m->tri = tri;
  }

  // --- Plancher nocturne
  if (hourChanged && hour >= 1 && hour <= 5) s_snap[hour] = { idxOk, cons, inj };
  if (hour >= 5 && strcmp(s_floorDoneDate, d->date) != 0) {
    long best = -1;
    for (int h = 1; h <= 4; h++) {
      const Snap& a = s_snap[h];
      const Snap& b = s_snap[h + 1];
      if (!a.ok || !b.ok) continue;
      if (b.inj != a.inj) continue;                     // production pendant l'heure
      int64_t wh = b.cons - a.cons;                     // Wh en 1 h = puissance moyenne en W
      if (wh <= 0 || wh > 100000) continue;
      if (best < 0 || wh < best) best = (long)wh;
    }
    if (best >= 0) d->floorW = best;
    strlcpy(s_floorDoneDate, d->date, sizeof(s_floorDoneDate));
    s_dirty = true;
  }
  s_lastHour = hour;

  // --- Heures de bascule tarifaire
  if (attr > 0) {
    if (s_lastAttr > 0 && attr != s_lastAttr) recordTransition(hour * 60 + minute);
    s_lastAttr = attr;
  }

  if (s_dirty && (nowMs - s_lastSave >= PS_SAVE_MS)) saveFile();
}

// ---------------------------------------------------------------- lecture

static int clampDays(int days, int def) {
  if (days <= 0) days = def;
  return days > PS_DAYS ? PS_DAYS : days;
}

static void setOrNull(JsonObject o, const char* k, long v) {
  if (v >= 0) o[k] = v; else o[k] = nullptr;
}

// Minutes (ou jours) par tranche, tranches vides omises : {"80":12,"90":3}
template <typename T>
static void addBands(JsonObject parent, const char* key, const T* v) {
  JsonObject o = parent.createNestedObject(key);
  char k[4];
  for (int i = 0; i < PS_BANDS; i++) {
    if (v[i] == 0) continue;
    snprintf(k, sizeof(k), "%u", PS_BAND[i]);
    o[k] = v[i];
  }
}

static void chanDayJson(JsonObject o, const ChanDay& c) {
  setOrNull(o, "instVA", c.instVA);
  setOrNull(o, "va1m", c.va1m);
  setOrNull(o, "va10m", c.va10m);
  addBands(o, "minutes", c.min);
}

static void chanMonthJson(JsonObject o, const ChanMonth& c) {
  setOrNull(o, "instVA", c.instVA);
  setOrNull(o, "va1m", c.va1m);
  setOrNull(o, "va10m", c.va10m);
  addBands(o, "minutes", c.min);
  addBands(o, "days", c.days);
}

String powerStatsPeaksJson(int days) {
  days = clampDays(days, PS_DAYS);
  bool standard = powerStatsIsStandard();
  SpiRamJsonDocument doc(PS_JSON_DOC);
  {
    PsGuard g;
    bool tri = powerStatsIsTriphase();
    doc["subscribedVA"] = s_subVA;
    doc["triphase"] = tri;
    if (tri) doc["phaseSubscribedVA"] = s_subVA / 3;
    // En triphase, SMAXSN est donne par phase : pas de valeur officielle du total
    doc["source"] = (standard && !tri) ? "meter" : "sampled";
    JsonArray a = doc.createNestedArray("peaks");
    int start = s_count > days ? s_count - days : 0;
    for (int i = start; i < s_count; i++) {
      const DayStat& d = s_days[i];
      JsonObject o = a.createNestedObject();
      o["date"] = d.date;
      if (standard && !d.tri && d.meterVA[0] > 0) {
        o["va"] = d.meterVA[0];
      } else if (d.ch[0].va1m > 0) {
        o["va"] = d.ch[0].va1m;
        if (standard && !d.tri) o["source"] = "sampled";   // jour sans SMAXSN : repli signale
      } else {
        o["va"] = nullptr;
      }
      chanDayJson(o, d.ch[0]);
      setOrNull(o, "floorW", d.floorW);
      if (d.subVA > 0 && d.subVA != s_subVA) o["subscribedVA"] = d.subVA;  // abonnement change
      o["measuredMin"] = d.obsMin;
      if (d.tri) {
        JsonArray ph = o.createNestedArray("phases");
        for (int p = 1; p <= 3; p++) {
          JsonObject x = ph.createNestedObject();
          x["phase"] = p;
          if (d.meterVA[p - 1] > 0) x["meterVA"] = d.meterVA[p - 1];
          chanDayJson(x, d.ch[p]);
        }
      }
    }
  }
  String out;
  serializeJson(doc, out);
  return out;
}

String powerStatsFloorJson(int days) {
  days = clampDays(days, 30);
  SpiRamJsonDocument doc(8192);
  {
    PsGuard g;
    JsonArray a = doc.createNestedArray("floor");
    int start = s_count > days ? s_count - days : 0;
    for (int i = start; i < s_count; i++) {
      JsonObject o = a.createNestedObject();
      o["date"] = s_days[i].date;
      setOrNull(o, "w", s_days[i].floorW);
    }
  }
  String out;
  serializeJson(doc, out);
  return out;
}

// Frequence d'apparition d'une tranche, d'apres la part des jours observes ou elle est atteinte.
static const char* rateLabel(int events, int observed) {
  if (observed < 7) return "unknown";                 // pas assez de recul
  if (events == 0) return "never";
  float r = (float)events / observed;
  if (r >= 0.7f)   return "daily";                    // la plupart des jours
  if (r >= 0.1f)   return "weekly";                   // au moins 1 jour sur 10
  if (r >= 0.025f) return "monthly";                  // au moins 1 jour sur 40
  return "rare";
}

// Pires valeurs d'une voie sur les 90 jours
static void worstJson(JsonObject parent, int c) {
  struct { const char* key; int32_t ChanDay::*field; } W[] = {
    { "instVA", &ChanDay::instVA }, { "va1m", &ChanDay::va1m }, { "va10m", &ChanDay::va10m },
  };
  for (auto& w : W) {
    int best = -1;
    for (int i = 0; i < s_count; i++) {
      int32_t v = s_days[i].ch[c].*w.field;
      if (v > 0 && (best < 0 || v > s_days[best].ch[c].*w.field)) best = i;
    }
    JsonObject o = parent.createNestedObject(w.key);
    if (best >= 0) { o["va"] = s_days[best].ch[c].*w.field; o["date"] = s_days[best].date; }
    else           { o["va"] = nullptr; o["date"] = nullptr; }
  }
}

String powerStatsSummaryJson() {
  SpiRamJsonDocument doc(65536);
  {
    PsGuard g;
    bool tri = powerStatsIsTriphase();
    doc["subscribedVA"] = s_subVA;
    doc["triphase"] = tri;
    if (tri) doc["phaseSubscribedVA"] = s_subVA / 3;
    JsonArray bands = doc.createNestedArray("bands");
    for (int k = 0; k < PS_BANDS; k++) bands.add(PS_BAND[k]);

    JsonObject worst = doc.createNestedObject("worst");
    worstJson(worst, 0);
    if (tri) {
      JsonArray ph = worst.createNestedArray("phases");
      for (int p = 1; p <= 3; p++) {
        JsonObject x = ph.createNestedObject();
        x["phase"] = p;
        worstJson(x, p);
      }
    }

    // Frequence par tranche. Monophase : la puissance totale. Triphase : un jour compte des
    // qu'UNE phase a atteint la tranche de sa propre limite (c'est la phase qui fait couper).
    doc["frequencyBasis"] = tri ? "phases" : "total";
    JsonObject freq = doc.createNestedObject("frequency");
    for (int k = 0; k < PS_BANDS; k++) {
      int d7 = 0, d30 = 0, d90 = 0, obs = 0;
      const char* last = nullptr;
      for (int i = s_count - 1, age = 0; i >= 0; i--, age++) {
        const DayStat& d = s_days[i];
        if (d.obsMin >= PS_OBSERVED_MIN) obs++;
        bool hit = false;
        if (d.tri) { for (int p = 1; p <= 3; p++) if (d.ch[p].min[k]) hit = true; }
        else hit = d.ch[0].min[k] != 0;
        if (!hit) continue;
        if (!last) last = d.date;
        if (age < 7) d7++;
        if (age < 30) d30++;
        d90++;
      }
      char key[4];
      snprintf(key, sizeof(key), "%u", PS_BAND[k]);
      JsonObject o = freq.createNestedObject(key);
      o["days7"] = d7;
      o["days30"] = d30;
      o["days90"] = d90;
      o["observedDays"] = obs;
      if (last) o["last"] = last; else o["last"] = nullptr;
      o["rate"] = rateLabel(d90, obs);
    }

    // Vue annuelle, mois par mois (24 mois)
    JsonArray months = doc.createNestedArray("months");
    for (int i = 0; i < s_mcount; i++) {
      const MonthStat& m = s_months[i];
      JsonObject o = months.createNestedObject();
      o["month"] = m.month;
      o["observedDays"] = m.obsDays;
      if (m.subVA > 0) o["subscribedVA"] = m.subVA;
      chanMonthJson(o, m.ch[0]);
      if (m.tri) {
        JsonArray ph = o.createNestedArray("phases");
        for (int p = 1; p <= 3; p++) {
          JsonObject x = ph.createNestedObject();
          x["phase"] = p;
          chanMonthJson(x, m.ch[p]);
        }
      }
    }
  }
  String out;
  serializeJson(doc, out);
  return out;
}

int powerStatsCurrentIndexAttr() {
  DeviceData* z = zlinky();
  if (!z) return 0;
  if (powerStatsIsStandard()) {
    long n = hexVal(z, "FF66", "513");                // NTARF : numero de l'index en cours
    if (n >= 1 && n <= 9) return 256 + 2 * (int)(n - 1);
  }
  String ptec = z->getValue("FF66", "16");
  ptec.trim();
  if (ptec.length() == 0) {
    ptec = z->getValue("0702", "32");                 // PTEC Historique (cluster Metering)
    ptec.trim();
    if (ptec.length() == 0) return 0;
  }
  return tariffAttrFromPtec(ptec);
}

String powerStatsNextChange() {
  if (Hour[0] == '\0') return "";
  int now = atoi(Hour) * 60 + atoi(Minute);
  int family = getTariffFamily();
  if (family == 0) return "";                         // Base : pas de changement
  if (family == 3) return (now >= 360 && now < 1320) ? "22:00" : "06:00";   // Tempo

  PsGuard g;
  if (s_trCount < 2) return "";                       // pas encore assez observe
  int best = -1, first = -1;
  for (int i = 0; i < s_trCount; i++) {
    int t = s_tr[i];
    if (first < 0 || t < first) first = t;
    if (t > now && (best < 0 || t < best)) best = t;
  }
  if (best < 0) best = first;                         // prochain changement demain
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", best / 60, best % 60);
  return String(buf);
}
