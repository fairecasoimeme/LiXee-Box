// ============================================================================
// Statistiques de puissance du ZLinky - voir power_stats.h
// Portage de powerStats.cpp (LiXee-Box) : memes calculs, meme fichier, memes JSON.
// ============================================================================

#include "power_stats.h"
#include "energy_service.h"
#include "device_data.h"
#include "config_manager.h"
#include "ntp_sync.h"

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "PWR_STATS";

#define PS_DAYS            90
#define PS_MONTHS          24
#define PS_FILE            "/littlefs/hst/powerStats.json"
#define PS_TICK_MS         10000u
#define PS_SAVE_MS         3600000u     // au plus une ecriture par heure
#define PS_WINDOW_MS       60000u       // moyenne sur 1 min : la duree qui fait couper le Linky
#define PS_PLATEAU_MIN     10           // plateau : moyenne sur 10 min consecutives
#define PS_MAX_GAP_MS      120000u      // au-dela, la serie de mesures est rompue
#define PS_PHASE_FRESH_MS  60000u       // triphase Standard : age max d'une phase pour le total
#define PS_SAMPLES         128
#define PS_MAX_TRANSITIONS 8
#define PS_OBSERVED_MIN    60           // un jour compte comme observe a partir d'1 h de mesures
#define PS_BANDS           6
#define PS_CH              4            // voie 0 = total, 1..3 = phases (triphase)

static const uint8_t PS_BAND[PS_BANDS] = { 50, 60, 70, 80, 90, 100 };

typedef struct {
    int32_t  instVA, va1m, va10m;       // -1 si inconnu
    uint16_t min[PS_BANDS];             // minutes passees dans chaque tranche
} chan_day_t;

typedef struct {
    char       date[11];                // "YYYY-MM-DD"
    int32_t    meterVA[3];              // SMAXSN (Standard)
    int32_t    floorW;                  // plancher nocturne, -1 si inconnu
    int32_t    subVA;
    uint8_t    tri;
    uint16_t   obsMin;
    chan_day_t ch[PS_CH];
} day_stat_t;

typedef struct {
    int32_t  instVA, va1m, va10m;
    uint32_t min[PS_BANDS];
    uint16_t days[PS_BANDS];
} chan_month_t;

typedef struct {
    char         month[8];              // "YYYY-MM"
    int32_t      subVA;
    uint8_t      tri;
    uint16_t     obsDays;
    chan_month_t ch[PS_CH];
} month_stat_t;

typedef struct { uint32_t ms; int32_t va; } sample_t;

typedef struct {
    sample_t *samp;
    int       head, count;
    long      instPending;
    long      lastAvg;
    int32_t   plat[PS_PLATEAU_MIN];
    int       platHead, platCount;
} chan_rt_t;

static day_stat_t   *s_days;
static int           s_count;
static month_stat_t *s_months;
static int           s_mcount;
static chan_rt_t     s_rt[PS_CH];
static SemaphoreHandle_t s_mx;

static long     s_subVA;
static long     s_phaseLast[PS_CH];
static uint32_t s_phaseLastMs[PS_CH];
static uint32_t s_lastAvgMs, s_minuteMs;
static bool     s_dirty;
static uint32_t s_lastSave, s_lastTick;

static long s_stale1293 = -1;
static bool s_stale1293Active;
static long s_closeAtRollover[3] = { -1, -1, -1 };
static bool s_bootDone;

typedef struct { bool ok; int64_t cons, inj; } snap_t;
static snap_t s_snap[6];
static int    s_lastHour = -1;
static char   s_floorDoneDate[11];

static int16_t s_tr[PS_MAX_TRANSITIONS];
static int     s_trCount;
static int     s_lastAttr;

static void lock(void)   { if (s_mx) xSemaphoreTake(s_mx, portMAX_DELAY); }
static void unlock(void) { if (s_mx) xSemaphoreGive(s_mx); }
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// ----------------------------------------------------------------------------
// ZLinky : valeurs, mode, abonnement
// ----------------------------------------------------------------------------

static uint64_t zlinky(void)
{
    const char *z = cfg_get_general()->zlinky;
    return z[0] ? strtoull(z, NULL, 16) : 0;
}

static int64_t hexval(uint64_t ieee, uint16_t cl, uint16_t at)
{
    char buf[DEVDATA_VALUE_LEN];
    if (!ieee || device_data_get_value(ieee, cl, at, buf, sizeof(buf)) != ESP_OK || !buf[0]) return 0;
    return strtoll(buf, NULL, 16);
}

static int linky_mode(uint64_t zl)
{
    char buf[DEVDATA_VALUE_LEN];
    if (zl && device_data_get_value(zl, 0xFF66, 0x0300, buf, sizeof(buf)) == ESP_OK && buf[0]) {
        long m = strtol(buf, NULL, 16);
        if (m >= 0 && m <= 7) return (int)m;
    }
    return cfg_get_general()->linky_mode;
}

static bool is_standard(int m) { return m == 1 || m == 3 || m == 5 || m == 7; }
static bool is_tri(int m)      { return m == 2 || m == 3 || m == 7; }

/** Puissance souscrite totale (VA) : PCOUP/PREF en Standard, ISOUSC x 200 en Historique. */
static long subscribed_va(uint64_t z, int mode)
{
    if (!z) return 0;
    if (is_standard(mode)) {
        long kva = (long)hexval(z, 0x0B01, 14);
        if (kva <= 0) kva = (long)hexval(z, 0x0B01, 13);
        return kva > 0 ? kva * 1000 : 0;
    }
    long a = (long)hexval(z, 0x0B01, 13);
    return a > 0 ? a * 200 * (is_tri(mode) ? 3 : 1) : 0;
}

static long chan_sub(int c)
{
    if (s_subVA <= 0) return 0;
    return c == 0 ? s_subVA : s_subVA / 3;
}

static bool today_key(char *out, struct tm *t)
{
    if (!ntp_sync_is_synced()) return false;
    time_t now = time(NULL);
    localtime_r(&now, t);
    if (t->tm_year + 1900 < 2024) return false;
    char tmp[40];
    snprintf(tmp, sizeof(tmp), "%04d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
    strlcpy(out, tmp, 11);
    return true;
}

// ----------------------------------------------------------------------------
// Jours et mois
// ----------------------------------------------------------------------------

static void reset_chan_day(chan_day_t *c)
{
    c->instVA = c->va1m = c->va10m = -1;
    memset(c->min, 0, sizeof(c->min));
}

static void reset_day(day_stat_t *d)
{
    memset(d, 0, sizeof(*d));
    d->meterVA[0] = d->meterVA[1] = d->meterVA[2] = -1;
    d->floorW = -1;
    for (int c = 0; c < PS_CH; c++) reset_chan_day(&d->ch[c]);
}

static void reset_month(month_stat_t *m)
{
    memset(m, 0, sizeof(*m));
    for (int c = 0; c < PS_CH; c++) m->ch[c].instVA = m->ch[c].va1m = m->ch[c].va10m = -1;
}

static bool chan_has_data(const chan_day_t *c)
{
    if (c->instVA >= 0 || c->va1m >= 0 || c->va10m >= 0) return true;
    for (int k = 0; k < PS_BANDS; k++) if (c->min[k]) return true;
    return false;
}

static day_stat_t *today(const char *key, bool *created)
{
    if (s_count > 0 && strcmp(s_days[s_count - 1].date, key) == 0) return &s_days[s_count - 1];
    if (s_count > 0 && strcmp(s_days[s_count - 1].date, key) > 0) return NULL;   // horloge en arriere
    if (s_count >= PS_DAYS) {
        memmove(&s_days[0], &s_days[1], sizeof(day_stat_t) * (PS_DAYS - 1));
        s_count = PS_DAYS - 1;
    }
    day_stat_t *d = &s_days[s_count++];
    reset_day(d);
    strlcpy(d->date, key, sizeof(d->date));
    s_dirty = true;
    if (created) *created = true;
    return d;
}

static month_stat_t *month_of(const day_stat_t *d)
{
    char key[8];
    strlcpy(key, d->date, sizeof(key));
    if (s_mcount > 0 && strcmp(s_months[s_mcount - 1].month, key) == 0) return &s_months[s_mcount - 1];
    if (s_mcount > 0 && strcmp(s_months[s_mcount - 1].month, key) > 0) return NULL;
    if (s_mcount >= PS_MONTHS) {
        memmove(&s_months[0], &s_months[1], sizeof(month_stat_t) * (PS_MONTHS - 1));
        s_mcount = PS_MONTHS - 1;
    }
    month_stat_t *m = &s_months[s_mcount++];
    reset_month(m);
    strlcpy(m->month, key, sizeof(m->month));
    return m;
}

static void keep_max(int32_t *dst, long v)
{
    if (v > *dst) { *dst = (int32_t)v; s_dirty = true; }
}

static void process_minute(day_stat_t *d, month_stat_t *m, int c, long avg)
{
    chan_rt_t *r = &s_rt[c];
    if (avg < 0) { r->platCount = 0; return; }           // plateau interrompu
    r->plat[r->platHead] = (int32_t)avg;
    r->platHead = (r->platHead + 1) % PS_PLATEAU_MIN;
    if (r->platCount < PS_PLATEAU_MIN) r->platCount++;
    if (r->platCount == PS_PLATEAU_MIN) {
        long sum = 0;
        for (int k = 0; k < PS_PLATEAU_MIN; k++) sum += r->plat[k];
        long p = sum / PS_PLATEAU_MIN;
        keep_max(&d->ch[c].va10m, p);
        if (m) keep_max(&m->ch[c].va10m, p);
    }
    long sub = chan_sub(c);
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

// ----------------------------------------------------------------------------
// Mesures
// ----------------------------------------------------------------------------

static void push_sample(int c, long va)
{
    chan_rt_t *r = &s_rt[c];
    if (!r->samp) return;
    if (va < 0) va = 0;                                  // injection : pas une consommation
    long sub = chan_sub(c);
    if (sub > 0 && va > sub * 3 / 2) return;             // valeur aberrante
    if (va > r->instPending) r->instPending = va;
    uint32_t now = now_ms();
    if (r->count > 0) {
        sample_t *last = &r->samp[(r->head + PS_SAMPLES - 1) % PS_SAMPLES];
        if (now - last->ms > PS_MAX_GAP_MS) {
            r->count = 0;                                // serie rompue
        } else if (now - last->ms < 1000) {
            last->va = (int32_t)va;                      // rafale : la derniere valeur
            return;
        }
    }
    r->samp[r->head].ms = now;
    r->samp[r->head].va = (int32_t)va;
    r->head = (r->head + 1) % PS_SAMPLES;
    if (r->count < PS_SAMPLES) r->count++;
}

void power_stats_on_attribute(uint64_t ieee, uint16_t cluster, uint16_t attr, int64_t value)
{
    if (!s_days || cluster != 0x0B04) return;
    uint64_t z = zlinky();
    if (!z || ieee != z) return;
    if (attr != 1295 && attr != 2319 && attr != 2575 && attr != 1288 && attr != 2312 && attr != 2568) return;
    if (value < 0) value = 0;
    int mode = linky_mode(z);
    lock();
    if (!is_tri(mode)) {
        if (attr == 1295) push_sample(0, (long)value);   // PAPP / SINSTS
    } else if (is_standard(mode)) {
        // Standard triphase : SINSTS1..3 par phase, total = leur somme
        int ph = (attr == 1295) ? 1 : (attr == 2319) ? 2 : (attr == 2575) ? 3 : 0;
        if (ph) {
            push_sample(ph, (long)value);
            uint32_t now = now_ms();
            s_phaseLast[ph] = (long)value;
            s_phaseLastMs[ph] = now ? now : 1;
            long sum = 0;
            bool fresh = true;
            for (int p = 1; p <= 3; p++) {
                if (s_phaseLastMs[p] == 0 || now - s_phaseLastMs[p] > PS_PHASE_FRESH_MS) { fresh = false; break; }
                sum += s_phaseLast[p];
            }
            if (fresh) push_sample(0, sum);
        }
    } else {
        // Historique triphase : PAPP totale, phases d'apres IINST1..3 (200 VA/A)
        if (attr == 1295) push_sample(0, (long)value);
        int ph = (attr == 1288) ? 1 : (attr == 2312) ? 2 : (attr == 2568) ? 3 : 0;
        if (ph) push_sample(ph, (long)value * 200);
    }
    unlock();
}

/** Moyenne ponderee par le temps sur [now - 60 s, now], -1 si la fenetre n'est pas couverte. */
static long window_average(const chan_rt_t *r, uint32_t now)
{
    if (!r->samp || r->count < 2) return -1;
    int first = (r->head + PS_SAMPLES - r->count) % PS_SAMPLES;
    const sample_t *newest = &r->samp[(r->head + PS_SAMPLES - 1) % PS_SAMPLES];
    if (now - newest->ms > PS_MAX_GAP_MS) return -1;
    if (now - r->samp[first].ms < PS_WINDOW_MS) return -1;
    // Ages (now - ms) plutot que dates absolues : juste apres le rebouclage du
    // compteur de millisecondes (49 jours)
    double acc = 0;
    for (int k = 0; k < r->count; k++) {
        const sample_t *s = &r->samp[(first + k) % PS_SAMPLES];
        uint32_t age_start = now - s->ms;
        uint32_t age_end = (k + 1 < r->count) ? now - r->samp[(first + k + 1) % PS_SAMPLES].ms : 0;
        if (age_start > PS_WINDOW_MS) age_start = PS_WINDOW_MS;
        if (age_end >= age_start) continue;
        acc += (double)s->va * (age_start - age_end);
    }
    return (long)(acc / PS_WINDOW_MS);
}

static bool read_indexes(uint64_t z, int64_t *cons, int64_t *inj)
{
    *cons = 0;
    for (int a = 256; a <= 272; a += 2) *cons += hexval(z, 0x0702, (uint16_t)a);
    if (*cons == 0) *cons = hexval(z, 0x0702, 0);        // Historique BASE : index total seul
    *inj = hexval(z, 0x0702, 1);
    const char *pr = cfg_get_general()->production;
    uint64_t p = pr[0] ? strtoull(pr, NULL, 16) : 0;
    if (p && p != z) *inj += hexval(p, 0x0702, 1);
    return *cons > 0;
}

static void record_transition(int minute_of_day)
{
    for (int i = 0; i < s_trCount; i++) {
        int d = abs(s_tr[i] - minute_of_day);
        if (d <= 10 || d >= 1430) { s_tr[i] = (int16_t)minute_of_day; s_dirty = true; return; }
    }
    if (s_trCount >= PS_MAX_TRANSITIONS) {
        memmove(&s_tr[0], &s_tr[1], sizeof(int16_t) * (PS_MAX_TRANSITIONS - 1));
        s_trCount--;
    }
    s_tr[s_trCount++] = (int16_t)minute_of_day;
    s_dirty = true;
}

// ----------------------------------------------------------------------------
// Persistance (format v3 de la Box)
// Jour : {"d","mt":[3],"f","sub","t","o","c":[[inst,1m,10m,[tranches]] x4]}
// Mois : {"m","sub","t","o","c":[[inst,1m,10m,[minutes],[jours]] x4]}
// ----------------------------------------------------------------------------

static int jint(const cJSON *o, const char *k, int def)
{
    const cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(v) ? (int)v->valuedouble : def;
}

static int aint(const cJSON *a, int i, int def)
{
    const cJSON *v = cJSON_GetArrayItem(a, i);
    return cJSON_IsNumber(v) ? (int)v->valuedouble : def;
}

static void load_file(void)
{
    FILE *f = fopen(PS_FILE, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (n > 0 && n < 512 * 1024) ? (char *)heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM) : NULL;
    size_t got = buf ? fread(buf, 1, n, f) : 0;
    fclose(f);
    if (!buf) return;
    buf[got] = '\0';
    cJSON *doc = cJSON_Parse(buf);
    heap_caps_free(buf);
    if (!doc) { ESP_LOGE(TAG, "%s illisible", PS_FILE); return; }

    s_count = 0;
    const cJSON *o;
    cJSON_ArrayForEach(o, cJSON_GetObjectItem(doc, "days")) {
        if (s_count >= PS_DAYS) break;
        day_stat_t *d = &s_days[s_count++];
        reset_day(d);
        const cJSON *ds = cJSON_GetObjectItem(o, "d");
        if (cJSON_IsString(ds)) strlcpy(d->date, ds->valuestring, sizeof(d->date));
        d->floorW = jint(o, "f", -1);
        d->subVA  = jint(o, "sub", 0);
        d->tri    = (uint8_t)jint(o, "t", 0);
        d->obsMin = (uint16_t)jint(o, "o", 0);
        const cJSON *ca = cJSON_GetObjectItem(o, "c");
        if (ca) {
            const cJSON *mt = cJSON_GetObjectItem(o, "mt");
            for (int k = 0; k < 3; k++) d->meterVA[k] = aint(mt, k, -1);
            for (int c = 0; c < PS_CH; c++) {
                const cJSON *v = cJSON_GetArrayItem(ca, c);
                if (cJSON_GetArraySize(v) < 4) continue;
                d->ch[c].instVA = aint(v, 0, -1);
                d->ch[c].va1m   = aint(v, 1, -1);
                d->ch[c].va10m  = aint(v, 2, -1);
                const cJSON *b = cJSON_GetArrayItem(v, 3);
                for (int k = 0; k < PS_BANDS; k++) d->ch[c].min[k] = (uint16_t)aint(b, k, 0);
            }
        } else {                                         // format v2 : une seule voie
            d->meterVA[0]   = jint(o, "m", -1);
            d->ch[0].instVA = jint(o, "i", -1);
            d->ch[0].va1m   = jint(o, "s", -1);
            d->ch[0].va10m  = jint(o, "p", -1);
            const cJSON *b = cJSON_GetObjectItem(o, "b");
            for (int k = 0; k < PS_BANDS; k++) d->ch[0].min[k] = (uint16_t)aint(b, k, 0);
        }
    }

    s_mcount = 0;
    cJSON_ArrayForEach(o, cJSON_GetObjectItem(doc, "months")) {
        if (s_mcount >= PS_MONTHS) break;
        month_stat_t *m = &s_months[s_mcount++];
        reset_month(m);
        const cJSON *ms = cJSON_GetObjectItem(o, "m");
        if (cJSON_IsString(ms)) strlcpy(m->month, ms->valuestring, sizeof(m->month));
        m->subVA   = jint(o, "sub", 0);
        m->tri     = (uint8_t)jint(o, "t", 0);
        m->obsDays = (uint16_t)jint(o, "o", 0);
        const cJSON *ca = cJSON_GetObjectItem(o, "c");
        if (ca) {
            for (int c = 0; c < PS_CH; c++) {
                const cJSON *v = cJSON_GetArrayItem(ca, c);
                if (cJSON_GetArraySize(v) < 5) continue;
                m->ch[c].instVA = aint(v, 0, -1);
                m->ch[c].va1m   = aint(v, 1, -1);
                m->ch[c].va10m  = aint(v, 2, -1);
                const cJSON *bm = cJSON_GetArrayItem(v, 3), *bd = cJSON_GetArrayItem(v, 4);
                for (int k = 0; k < PS_BANDS; k++) {
                    m->ch[c].min[k]  = (uint32_t)aint(bm, k, 0);
                    m->ch[c].days[k] = (uint16_t)aint(bd, k, 0);
                }
            }
        } else {
            m->ch[0].instVA = jint(o, "i", -1);
            m->ch[0].va1m   = jint(o, "s", -1);
            m->ch[0].va10m  = jint(o, "p", -1);
            const cJSON *bm = cJSON_GetObjectItem(o, "bm"), *bd = cJSON_GetObjectItem(o, "bd");
            for (int k = 0; k < PS_BANDS; k++) {
                m->ch[0].min[k]  = (uint32_t)aint(bm, k, 0);
                m->ch[0].days[k] = (uint16_t)aint(bd, k, 0);
            }
        }
    }

    s_trCount = 0;
    cJSON_ArrayForEach(o, cJSON_GetObjectItem(doc, "tr")) {
        if (s_trCount >= PS_MAX_TRANSITIONS) break;
        if (cJSON_IsNumber(o)) s_tr[s_trCount++] = (int16_t)o->valuedouble;
    }
    const cJSON *fd = cJSON_GetObjectItem(doc, "fd");
    if (cJSON_IsString(fd)) strlcpy(s_floorDoneDate, fd->valuestring, sizeof(s_floorDoneDate));
    cJSON_Delete(doc);
    ESP_LOGI(TAG, "Statistiques relues : %d jour(s), %d mois", s_count, s_mcount);
}

/** Copie des donnees sous verrou, serialisation et ecriture hors verrou. */
static void save_file(void)
{
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddNumberToObject(doc, "v", 3);
    cJSON *a = cJSON_AddArrayToObject(doc, "days");
    for (int i = 0; i < s_count; i++) {
        const day_stat_t *d = &s_days[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "d", d->date);
        cJSON *mt = cJSON_AddArrayToObject(o, "mt");
        for (int k = 0; k < 3; k++) cJSON_AddItemToArray(mt, cJSON_CreateNumber(d->meterVA[k]));
        if (d->floorW >= 0) cJSON_AddNumberToObject(o, "f", d->floorW);
        if (d->subVA > 0) cJSON_AddNumberToObject(o, "sub", d->subVA);
        if (d->tri) cJSON_AddNumberToObject(o, "t", 1);
        cJSON_AddNumberToObject(o, "o", d->obsMin);
        cJSON *ca = cJSON_AddArrayToObject(o, "c");
        for (int c = 0; c < PS_CH; c++) {
            cJSON *v = cJSON_CreateArray();
            cJSON_AddItemToArray(ca, v);
            if (!chan_has_data(&d->ch[c])) continue;
            cJSON_AddItemToArray(v, cJSON_CreateNumber(d->ch[c].instVA));
            cJSON_AddItemToArray(v, cJSON_CreateNumber(d->ch[c].va1m));
            cJSON_AddItemToArray(v, cJSON_CreateNumber(d->ch[c].va10m));
            cJSON *b = cJSON_CreateArray();
            for (int k = 0; k < PS_BANDS; k++) cJSON_AddItemToArray(b, cJSON_CreateNumber(d->ch[c].min[k]));
            cJSON_AddItemToArray(v, b);
        }
        cJSON_AddItemToArray(a, o);
    }
    cJSON *ma = cJSON_AddArrayToObject(doc, "months");
    for (int i = 0; i < s_mcount; i++) {
        const month_stat_t *m = &s_months[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "m", m->month);
        if (m->subVA > 0) cJSON_AddNumberToObject(o, "sub", m->subVA);
        if (m->tri) cJSON_AddNumberToObject(o, "t", 1);
        cJSON_AddNumberToObject(o, "o", m->obsDays);
        cJSON *ca = cJSON_AddArrayToObject(o, "c");
        for (int c = 0; c < PS_CH; c++) {
            cJSON *v = cJSON_CreateArray();
            cJSON_AddItemToArray(ca, v);
            if (m->ch[c].instVA < 0 && m->ch[c].va1m < 0 && m->ch[c].va10m < 0) continue;
            cJSON_AddItemToArray(v, cJSON_CreateNumber(m->ch[c].instVA));
            cJSON_AddItemToArray(v, cJSON_CreateNumber(m->ch[c].va1m));
            cJSON_AddItemToArray(v, cJSON_CreateNumber(m->ch[c].va10m));
            cJSON *bm = cJSON_CreateArray(), *bd = cJSON_CreateArray();
            for (int k = 0; k < PS_BANDS; k++) {
                cJSON_AddItemToArray(bm, cJSON_CreateNumber(m->ch[c].min[k]));
                cJSON_AddItemToArray(bd, cJSON_CreateNumber(m->ch[c].days[k]));
            }
            cJSON_AddItemToArray(v, bm);
            cJSON_AddItemToArray(v, bd);
        }
        cJSON_AddItemToArray(ma, o);
    }
    cJSON *t = cJSON_AddArrayToObject(doc, "tr");
    for (int i = 0; i < s_trCount; i++) cJSON_AddItemToArray(t, cJSON_CreateNumber(s_tr[i]));
    cJSON_AddStringToObject(doc, "fd", s_floorDoneDate);
    char *js = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    s_dirty = false;
    s_lastSave = now_ms();
    unlock();                                            // ecriture flash hors verrou
    if (js) {
        FILE *f = fopen(PS_FILE ".tmp", "wb");
        bool ok = f && fwrite(js, 1, strlen(js), f) == strlen(js);
        if (f) fclose(f);
        if (ok) {
            remove(PS_FILE);
            ok = rename(PS_FILE ".tmp", PS_FILE) == 0;
        }
        if (!ok) ESP_LOGE(TAG, "Ecriture de %s impossible", PS_FILE);
        free(js);
    }
    lock();
}

// ----------------------------------------------------------------------------
// Boucle
// ----------------------------------------------------------------------------

void power_stats_init(void)
{
    if (!s_mx) s_mx = xSemaphoreCreateMutex();
    if (!s_days)   s_days   = (day_stat_t *)heap_caps_calloc(PS_DAYS, sizeof(day_stat_t), MALLOC_CAP_SPIRAM);
    if (!s_months) s_months = (month_stat_t *)heap_caps_calloc(PS_MONTHS, sizeof(month_stat_t), MALLOC_CAP_SPIRAM);
    for (int c = 0; c < PS_CH; c++) {
        if (!s_rt[c].samp) s_rt[c].samp = (sample_t *)heap_caps_calloc(PS_SAMPLES, sizeof(sample_t), MALLOC_CAP_SPIRAM);
        s_rt[c].instPending = -1;
        s_rt[c].lastAvg = -1;
    }
    if (!s_days || !s_months || !s_rt[0].samp) {
        ESP_LOGE(TAG, "Allocation impossible");
        free(s_days);
        s_days = NULL;
        return;
    }
    lock();
    load_file();
    s_lastSave = now_ms();
    unlock();
}

void power_stats_loop(void)
{
    if (!s_days || !s_months) return;
    uint32_t now = now_ms();
    if (now - s_lastTick < PS_TICK_MS) return;
    s_lastTick = now;

    uint64_t z = zlinky();
    if (!z || !device_data_find(z)) return;
    char key[11];
    struct tm t;
    if (!today_key(key, &t)) return;
    int mode = linky_mode(z);
    bool standard = is_standard(mode), tri = is_tri(mode);
    long sub = subscribed_va(z, mode);
    long v1293 = (long)hexval(z, 0x0B04, 1293);
    long vclose[3] = { (long)hexval(z, 0xFF66, 530), tri ? (long)hexval(z, 0xFF66, 531) : 0,
                       tri ? (long)hexval(z, 0xFF66, 532) : 0 };
    int attr = energy_tariff_available() ? energy_tariff_current_index() : 0;
    int hour = t.tm_hour, minute = t.tm_min;

    int64_t cons = 0, inj = 0;
    bool idx_ok = false;
    bool hour_changed = (s_lastHour >= 0 && hour != s_lastHour);
    if (hour_changed && hour >= 1 && hour <= 5) idx_ok = read_indexes(z, &cons, &inj);

    lock();
    s_subVA = sub;
    bool created = false;
    day_stat_t *d = today(key, &created);
    if (!d) { unlock(); return; }
    month_stat_t *m = month_of(d);

    if (!s_bootDone) {
        s_bootDone = true;
        for (int k = 0; k < 3; k++) s_closeAtRollover[k] = vclose[k];
        s_minuteMs = now;
        // Redemarrage dans la journee : SMAXSN ne fait que croitre, une valeur au
        // moins egale a celle retenue est bien celle du jour ; sinon elle peut
        // dater d'hier : on attend qu'elle change
        s_stale1293 = v1293;
        s_stale1293Active = !(d->meterVA[0] > 0 && v1293 >= d->meterVA[0]);
        memset(s_snap, 0, sizeof(s_snap));
    } else if (created) {
        s_stale1293 = v1293;
        s_stale1293Active = true;
        for (int k = 0; k < 3; k++) s_closeAtRollover[k] = vclose[k];
        memset(s_snap, 0, sizeof(s_snap));
    }

    // Pic officiel (Standard) : SMAXSN du jour, SMAXSN-1 par phase pour la veille
    if (standard) {
        if (s_stale1293Active && v1293 != s_stale1293) s_stale1293Active = false;
        if (!s_stale1293Active && v1293 > 0) keep_max(&d->meterVA[0], v1293);
        if (s_count >= 2) {
            day_stat_t *y = &s_days[s_count - 2];
            for (int k = 0; k < 3; k++) {
                if (vclose[k] > 0 && vclose[k] != s_closeAtRollover[k]) {
                    if (y->meterVA[k] != vclose[k]) { y->meterVA[k] = (int32_t)vclose[k]; s_dirty = true; }
                    s_closeAtRollover[k] = vclose[k];
                }
            }
        }
    }

    // Par voie : pic instantane, moyenne sur 1 min, minutes par tranche
    bool minute_due = (now - s_minuteMs >= PS_WINDOW_MS);
    if (minute_due) s_minuteMs = now;
    bool any = false;
    int last_ch = tri ? 3 : 0;
    for (int c = 0; c <= last_ch; c++) {
        chan_rt_t *r = &s_rt[c];
        if (r->instPending > 0) {
            keep_max(&d->ch[c].instVA, r->instPending);
            if (m) keep_max(&m->ch[c].instVA, r->instPending);
        }
        r->instPending = -1;
        long avg = window_average(r, now);
        r->lastAvg = avg;
        long cs = chan_sub(c);
        if (avg > 0 && (cs <= 0 || avg <= cs * 3 / 2)) {
            keep_max(&d->ch[c].va1m, avg);
            if (m) keep_max(&m->ch[c].va1m, avg);
        }
        if (minute_due) {
            if (avg >= 0) any = true;
            process_minute(d, m, c, avg);
        }
    }
    s_lastAvgMs = now;
    if (minute_due && any) {
        d->obsMin++;
        if (d->obsMin == PS_OBSERVED_MIN && m) m->obsDays++;
        if (s_subVA > 0) { d->subVA = (int32_t)s_subVA; if (m) m->subVA = (int32_t)s_subVA; }
        d->tri = tri;
        if (m) m->tri = tri;
    }

    // Plancher nocturne : plus petite consommation horaire entre 1 h et 5 h
    if (hour_changed && hour >= 1 && hour <= 5) s_snap[hour] = (snap_t){ idx_ok, cons, inj };
    if (hour >= 5 && strcmp(s_floorDoneDate, d->date) != 0) {
        long best = -1;
        for (int h = 1; h <= 4; h++) {
            const snap_t *a = &s_snap[h], *b = &s_snap[h + 1];
            if (!a->ok || !b->ok || b->inj != a->inj) continue;   // production pendant l'heure
            int64_t wh = b->cons - a->cons;                      // Wh en 1 h = W moyens
            if (wh <= 0 || wh > 100000) continue;
            if (best < 0 || wh < best) best = (long)wh;
        }
        if (best >= 0) d->floorW = (int32_t)best;
        strlcpy(s_floorDoneDate, d->date, sizeof(s_floorDoneDate));
        s_dirty = true;
    }
    s_lastHour = hour;

    // Heures de bascule tarifaire
    if (attr > 0) {
        if (s_lastAttr > 0 && attr != s_lastAttr) record_transition(hour * 60 + minute);
        s_lastAttr = attr;
    }

    if (s_dirty && (now - s_lastSave >= PS_SAVE_MS)) save_file();
    unlock();
}

// ----------------------------------------------------------------------------
// Lecture
// ----------------------------------------------------------------------------

int power_stats_minute_load_pct(void)
{
    if (!s_days) return -1;
    lock();
    int r = -1;
    if (now_ms() - s_lastAvgMs <= 2 * PS_TICK_MS && s_subVA > 0) {
        if (!is_tri(linky_mode(zlinky()))) {
            long a = s_rt[0].lastAvg;
            r = a < 0 ? -1 : (int)(a * 100 / s_subVA);
        } else {
            for (int p = 1; p <= 3; p++) {
                long a = s_rt[p].lastAvg;
                if (a < 0 || chan_sub(p) <= 0) continue;
                int pct = (int)(a * 100 / chan_sub(p));
                if (pct > r) r = pct;
            }
        }
    }
    unlock();
    return r;
}

static void set_or_null(cJSON *o, const char *k, long v)
{
    if (v >= 0) cJSON_AddNumberToObject(o, k, v);
    else cJSON_AddNullToObject(o, k);
}

static void add_bands16(cJSON *parent, const char *key, const uint16_t *v)
{
    cJSON *o = cJSON_AddObjectToObject(parent, key);
    for (int i = 0; i < PS_BANDS; i++) {
        if (!v[i]) continue;
        char k[4];
        snprintf(k, sizeof(k), "%u", PS_BAND[i]);
        cJSON_AddNumberToObject(o, k, v[i]);
    }
}

static void add_bands32(cJSON *parent, const char *key, const uint32_t *v)
{
    cJSON *o = cJSON_AddObjectToObject(parent, key);
    for (int i = 0; i < PS_BANDS; i++) {
        if (!v[i]) continue;
        char k[4];
        snprintf(k, sizeof(k), "%u", PS_BAND[i]);
        cJSON_AddNumberToObject(o, k, v[i]);
    }
}

static void chan_day_json(cJSON *o, const chan_day_t *c)
{
    set_or_null(o, "instVA", c->instVA);
    set_or_null(o, "va1m", c->va1m);
    set_or_null(o, "va10m", c->va10m);
    add_bands16(o, "minutes", c->min);
}

static void chan_month_json(cJSON *o, const chan_month_t *c)
{
    set_or_null(o, "instVA", c->instVA);
    set_or_null(o, "va1m", c->va1m);
    set_or_null(o, "va10m", c->va10m);
    add_bands32(o, "minutes", c->min);
    add_bands16(o, "days", c->days);
}

static char *json_out(cJSON *root)
{
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

static int clamp_days(int days, int def)
{
    if (days <= 0) days = def;
    return days > PS_DAYS ? PS_DAYS : days;
}

char *power_stats_peaks_json(int days)
{
    days = clamp_days(days, PS_DAYS);
    int mode = linky_mode(zlinky());
    bool standard = is_standard(mode), tri = is_tri(mode);
    cJSON *root = cJSON_CreateObject();
    if (!s_days) return json_out(root);
    lock();
    cJSON_AddNumberToObject(root, "subscribedVA", s_subVA);
    cJSON_AddBoolToObject(root, "triphase", tri);
    if (tri) cJSON_AddNumberToObject(root, "phaseSubscribedVA", s_subVA / 3);
    cJSON_AddStringToObject(root, "source", (standard && !tri) ? "meter" : "sampled");
    cJSON *a = cJSON_AddArrayToObject(root, "peaks");
    int start = s_count > days ? s_count - days : 0;
    for (int i = start; i < s_count; i++) {
        const day_stat_t *d = &s_days[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "date", d->date);
        if (standard && !d->tri && d->meterVA[0] > 0) {
            cJSON_AddNumberToObject(o, "va", d->meterVA[0]);
        } else if (d->ch[0].va1m > 0) {
            cJSON_AddNumberToObject(o, "va", d->ch[0].va1m);
            if (standard && !d->tri) cJSON_AddStringToObject(o, "source", "sampled");
        } else {
            cJSON_AddNullToObject(o, "va");
        }
        chan_day_json(o, &d->ch[0]);
        set_or_null(o, "floorW", d->floorW);
        if (d->subVA > 0 && d->subVA != s_subVA) cJSON_AddNumberToObject(o, "subscribedVA", d->subVA);
        cJSON_AddNumberToObject(o, "measuredMin", d->obsMin);
        if (d->tri) {
            cJSON *ph = cJSON_AddArrayToObject(o, "phases");
            for (int p = 1; p <= 3; p++) {
                cJSON *x = cJSON_CreateObject();
                cJSON_AddNumberToObject(x, "phase", p);
                if (d->meterVA[p - 1] > 0) cJSON_AddNumberToObject(x, "meterVA", d->meterVA[p - 1]);
                chan_day_json(x, &d->ch[p]);
                cJSON_AddItemToArray(ph, x);
            }
        }
        cJSON_AddItemToArray(a, o);
    }
    unlock();
    return json_out(root);
}

char *power_stats_floor_json(int days)
{
    days = clamp_days(days, 30);
    cJSON *root = cJSON_CreateObject();
    cJSON *a = cJSON_AddArrayToObject(root, "floor");
    if (!s_days) return json_out(root);
    lock();
    int start = s_count > days ? s_count - days : 0;
    for (int i = start; i < s_count; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "date", s_days[i].date);
        set_or_null(o, "w", s_days[i].floorW);
        cJSON_AddItemToArray(a, o);
    }
    unlock();
    return json_out(root);
}

static const char *rate_label(int events, int observed)
{
    if (observed < 7) return "unknown";
    if (events == 0) return "never";
    float r = (float)events / observed;
    if (r >= 0.7f)   return "daily";
    if (r >= 0.1f)   return "weekly";
    if (r >= 0.025f) return "monthly";
    return "rare";
}

static int32_t chan_field(const chan_day_t *c, int f) { return f == 0 ? c->instVA : (f == 1 ? c->va1m : c->va10m); }

static void worst_json(cJSON *parent, int c)
{
    static const char *const K[3] = { "instVA", "va1m", "va10m" };
    for (int f = 0; f < 3; f++) {
        int best = -1;
        for (int i = 0; i < s_count; i++) {
            int32_t v = chan_field(&s_days[i].ch[c], f);
            if (v > 0 && (best < 0 || v > chan_field(&s_days[best].ch[c], f))) best = i;
        }
        cJSON *o = cJSON_AddObjectToObject(parent, K[f]);
        if (best >= 0) {
            cJSON_AddNumberToObject(o, "va", chan_field(&s_days[best].ch[c], f));
            cJSON_AddStringToObject(o, "date", s_days[best].date);
        } else {
            cJSON_AddNullToObject(o, "va");
            cJSON_AddNullToObject(o, "date");
        }
    }
}

char *power_stats_summary_json(void)
{
    bool tri = is_tri(linky_mode(zlinky()));
    cJSON *root = cJSON_CreateObject();
    if (!s_days) return json_out(root);
    lock();
    cJSON_AddNumberToObject(root, "subscribedVA", s_subVA);
    cJSON_AddBoolToObject(root, "triphase", tri);
    if (tri) cJSON_AddNumberToObject(root, "phaseSubscribedVA", s_subVA / 3);
    cJSON *bands = cJSON_AddArrayToObject(root, "bands");
    for (int k = 0; k < PS_BANDS; k++) cJSON_AddItemToArray(bands, cJSON_CreateNumber(PS_BAND[k]));

    cJSON *worst = cJSON_AddObjectToObject(root, "worst");
    worst_json(worst, 0);
    if (tri) {
        cJSON *ph = cJSON_AddArrayToObject(worst, "phases");
        for (int p = 1; p <= 3; p++) {
            cJSON *x = cJSON_CreateObject();
            cJSON_AddNumberToObject(x, "phase", p);
            worst_json(x, p);
            cJSON_AddItemToArray(ph, x);
        }
    }

    // Frequence par tranche ; triphase : un jour compte des qu'UNE phase atteint la tranche
    cJSON_AddStringToObject(root, "frequencyBasis", tri ? "phases" : "total");
    cJSON *freq = cJSON_AddObjectToObject(root, "frequency");
    for (int k = 0; k < PS_BANDS; k++) {
        int d7 = 0, d30 = 0, d90 = 0, obs = 0;
        const char *last = NULL;
        for (int i = s_count - 1, age = 0; i >= 0; i--, age++) {
            const day_stat_t *d = &s_days[i];
            if (d->obsMin >= PS_OBSERVED_MIN) obs++;
            bool hit = false;
            if (d->tri) { for (int p = 1; p <= 3; p++) if (d->ch[p].min[k]) hit = true; }
            else hit = d->ch[0].min[k] != 0;
            if (!hit) continue;
            if (!last) last = d->date;
            if (age < 7) d7++;
            if (age < 30) d30++;
            d90++;
        }
        char key[4];
        snprintf(key, sizeof(key), "%u", PS_BAND[k]);
        cJSON *o = cJSON_AddObjectToObject(freq, key);
        cJSON_AddNumberToObject(o, "days7", d7);
        cJSON_AddNumberToObject(o, "days30", d30);
        cJSON_AddNumberToObject(o, "days90", d90);
        cJSON_AddNumberToObject(o, "observedDays", obs);
        if (last) cJSON_AddStringToObject(o, "last", last);
        else cJSON_AddNullToObject(o, "last");
        cJSON_AddStringToObject(o, "rate", rate_label(d90, obs));
    }

    cJSON *months = cJSON_AddArrayToObject(root, "months");
    for (int i = 0; i < s_mcount; i++) {
        const month_stat_t *m = &s_months[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "month", m->month);
        cJSON_AddNumberToObject(o, "observedDays", m->obsDays);
        if (m->subVA > 0) cJSON_AddNumberToObject(o, "subscribedVA", m->subVA);
        chan_month_json(o, &m->ch[0]);
        if (m->tri) {
            cJSON *ph = cJSON_AddArrayToObject(o, "phases");
            for (int p = 1; p <= 3; p++) {
                cJSON *x = cJSON_CreateObject();
                cJSON_AddNumberToObject(x, "phase", p);
                chan_month_json(x, &m->ch[p]);
                cJSON_AddItemToArray(ph, x);
            }
        }
        cJSON_AddItemToArray(months, o);
    }
    unlock();
    return json_out(root);
}

bool power_stats_next_change(char *out, size_t len)
{
    if (!s_days || s_trCount < 2) return false;
    time_t t = time(NULL);
    struct tm lt;
    localtime_r(&t, &lt);
    int now = lt.tm_hour * 60 + lt.tm_min;
    lock();
    int best = -1, first = -1;
    for (int i = 0; i < s_trCount; i++) {
        int v = s_tr[i];
        if (first < 0 || v < first) first = v;
        if (v > now && (best < 0 || v < best)) best = v;
    }
    unlock();
    if (best < 0) best = first;                          // prochain changement demain
    snprintf(out, len, "%02d:%02d", best / 60, best % 60);
    return true;
}
