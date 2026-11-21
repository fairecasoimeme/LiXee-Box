// ============================================================================
// Conseils et economies - voir energy_advice.h et ECONOMIES.md
//
// Portage des regles de LiXee-Assist (lib/services/energy_advice.dart et
// tariff_compare.dart, master 6fb6232) ; definitions communes validees :
//   - consommation de fond : l'heure complete la plus calme des 24 dernieres
//     heures, a n'importe quelle heure (hors heures de production en mode 5/7) ;
//   - hier compare aux 7 jours precedents ;
//   - budget journalier = budget du mois / nombre de jours du mois ;
//   - grille du tarif reglemente : /littlefs/tarifs/tarif_bleu.json, sinon
//     la grille integree ci-dessous ;
//   - couleurs Tempo passees enregistrees par la box (hst/tempo.json) ;
//   - part des heures creuses mesuree jour par jour sur 31 jours (archive
//     horaire), par index ou de 22 h a 6 h selon le contrat.
// ============================================================================
#include "energy_advice.h"
#include "energy_internal.h"
#include "energy_service.h"
#include "energy_archive.h"
#include "energy_history.h"
#include "power_stats.h"
#include "device_data.h"
#include "config_manager.h"
#include "ntp_sync.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

static const char *TAG = "ADVICE";

#define GRID_FILE     "/littlefs/tarifs/tarif_bleu.json"
#define TEMPO_FILE    "/littlefs/hst/tempo.json"
#define TEMPO_MAX     400          // jours gardes (une saison complete et la suivante)
#define CMP_DAYS      31           // jours complets compares
#define MIN_DAYS      7            // jours complets avant de comparer
#define SUB_MIN_DAYS  14           // jours observes avant de conseiller un abonnement

static SemaphoreHandle_t s_mx;
static void lock(void)   { if (!s_mx) s_mx = xSemaphoreCreateMutex(); xSemaphoreTake(s_mx, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mx); }

static double round2(double v) { return round(v * 100.0) / 100.0; }
static double round4(double v) { return round(v * 10000.0) / 10000.0; }

static bool local_now(struct tm *t)
{
    time_t now = time(NULL);
    localtime_r(&now, t);
    return ntp_sync_is_synced();
}

static uint32_t ymd_of(const struct tm *t)
{
    return (uint32_t)((t->tm_year + 1900) * 10000 + (t->tm_mon + 1) * 100 + t->tm_mday);
}

/** Jour au calendrier decale de delta jours, a midi (jours de 23 ou 25 h). */
static void shift_day(const struct tm *t, int delta, struct tm *out)
{
    *out = *t;
    out->tm_mday += delta;
    out->tm_hour = 12;
    out->tm_min = out->tm_sec = 0;
    out->tm_isdst = -1;
    mktime(out);
}

static int days_in_month(const struct tm *t)
{
    static const int D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int y = t->tm_year + 1900, m = t->tm_mon;
    if (m == 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return D[m];
}

static void upper(const char *in, char *out, size_t len)
{
    size_t n = 0;
    for (; in[n] && n + 1 < len; n++) out[n] = (char)toupper((unsigned char)in[n]);
    out[n] = '\0';
}

/** "22:00" -> "22 h", "06:30" -> "6 h 30" ; false si le format est inconnu. */
static bool clock_text(const char *hhmm, char *out, size_t len)
{
    int h, m;
    if (!hhmm || sscanf(hhmm, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return false;
    if (m == 0) snprintf(out, len, "%d h", h);
    else snprintf(out, len, "%d h %02d", h, m);
    return true;
}

static const char *const COLOR_KEY[4] = { "", "blue", "white", "red" };

// ============================================================================
// Grille du tarif reglemente
// ============================================================================

#define GRID_TIERS 9
static const int TIER_KVA[GRID_TIERS] = { 3, 6, 9, 12, 15, 18, 24, 30, 36 };

typedef struct {
    char   from[12];               // "2026-08-01"
    double base_abo[GRID_TIERS];   // 0 : option absente a cette puissance
    double base_kwh[GRID_TIERS];
    double hc_abo[GRID_TIERS];
    double hc, hp;
    double tempo_abo[GRID_TIERS];
    double tempo[4][2];            // [couleur 1..3][0 HC, 1 HP]
} grid_t;

// Tarif Bleu TTC au 1er aout 2026 (deliberation de la CRE du 15/07/2026),
// identique a data/tarifs/tarif_bleu.json
static const grid_t GRID_BUILTIN = {
    "2026-08-01",
    { 145.76, 190.81, 239.84, 286.86, 330.57, 0, 0, 0, 0 },
    { 0.2001, 0.2001, 0.1985, 0.1985, 0.1985, 0, 0, 0, 0 },
    { 145.76, 190.81, 239.84, 286.86, 330.57, 375.80, 473.16, 561.43, 650.69 },
    0.1589, 0.2142,
    { 0, 189.98, 237.34, 283.21, 325.28, 369.49, 460.41, 552.48, 649.02 },
    { { 0, 0 }, { 0.1356, 0.1654 }, { 0.1536, 0.1921 }, { 0.1615, 0.7295 } },
};

static int tier_index(int kva)
{
    for (int i = 0; i < GRID_TIERS; i++) if (TIER_KVA[i] == kva) return i;
    return -1;
}

static void grid_tiers(const cJSON *o, double *out)
{
    const cJSON *it;
    cJSON_ArrayForEach(it, o) {
        int i = tier_index(atoi(it->string));
        if (i >= 0 && cJSON_IsNumber(it)) out[i] = it->valuedouble;
    }
}

static double jnum(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : 0;
}

/** Grille du fichier si elle est au moins aussi recente que la grille integree. */
static void grid_load(grid_t *g)
{
    *g = GRID_BUILTIN;
    FILE *f = fopen(GRID_FILE, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 16384) { fclose(f); return; }
    char *txt = (char *)malloc((size_t)n + 1);
    if (!txt) { fclose(f); return; }
    size_t rd = fread(txt, 1, (size_t)n, f);
    fclose(f);
    txt[rd] = '\0';
    cJSON *root = cJSON_Parse(txt);
    free(txt);
    if (!root) { ESP_LOGW(TAG, "%s illisible, grille integree", GRID_FILE); return; }
    const cJSON *from = cJSON_GetObjectItem(root, "validFrom");
    if (cJSON_IsString(from) && strlen(from->valuestring) == 10 &&
        strcmp(from->valuestring, GRID_BUILTIN.from) >= 0) {
        grid_t x = {};
        strlcpy(x.from, from->valuestring, sizeof(x.from));
        const cJSON *b = cJSON_GetObjectItem(root, "base");
        const cJSON *h = cJSON_GetObjectItem(root, "hc");
        const cJSON *t = cJSON_GetObjectItem(root, "tempo");
        grid_tiers(cJSON_GetObjectItem(b, "abo"), x.base_abo);
        grid_tiers(cJSON_GetObjectItem(b, "kwh"), x.base_kwh);
        grid_tiers(cJSON_GetObjectItem(h, "abo"), x.hc_abo);
        x.hc = jnum(h, "hc");
        x.hp = jnum(h, "hp");
        grid_tiers(cJSON_GetObjectItem(t, "abo"), x.tempo_abo);
        for (int c = 1; c <= 3; c++) {
            const cJSON *p = cJSON_GetObjectItem(t, COLOR_KEY[c]);
            x.tempo[c][0] = jnum(p, "hc");
            x.tempo[c][1] = jnum(p, "hp");
        }
        if (x.hc > 0 && x.hp > 0 && x.tempo[3][1] > 0) *g = x;
        else ESP_LOGW(TAG, "%s incomplet, grille integree", GRID_FILE);
    }
    cJSON_Delete(root);
}

// ============================================================================
// Couleurs Tempo passees (vues par la box)
// ============================================================================

typedef struct { uint32_t ymd; uint8_t color; } tempo_day_t;

static tempo_day_t *s_tempo;       // trie par date
static int  s_tempo_n;
static bool s_tempo_loaded;

static void tempo_load(void)
{
    s_tempo_loaded = true;
    if (!s_tempo) {
        s_tempo = (tempo_day_t *)heap_caps_calloc(TEMPO_MAX, sizeof(tempo_day_t), MALLOC_CAP_SPIRAM);
        if (!s_tempo) s_tempo = (tempo_day_t *)calloc(TEMPO_MAX, sizeof(tempo_day_t));
        if (!s_tempo) return;
    }
    s_tempo_n = 0;
    FILE *f = fopen(TEMPO_FILE, "r");
    if (!f) return;
    char *txt = (char *)malloc(16384);
    if (!txt) { fclose(f); return; }
    size_t rd = fread(txt, 1, 16383, f);
    fclose(f);
    txt[rd] = '\0';
    cJSON *root = cJSON_Parse(txt);
    free(txt);
    const cJSON *it;
    cJSON_ArrayForEach(it, cJSON_GetObjectItem(root, "d")) {
        if (s_tempo_n >= TEMPO_MAX || !cJSON_IsNumber(it)) continue;
        uint32_t ymd = (uint32_t)strtoul(it->string, NULL, 10);
        int c = it->valueint;
        if (ymd < 20000101 || c < 1 || c > 3) continue;
        s_tempo[s_tempo_n++] = { ymd, (uint8_t)c };
    }
    cJSON_Delete(root);
    // Tri par date (le fichier est ecrit dans l'ordre, par securite)
    for (int i = 1; i < s_tempo_n; i++) {
        tempo_day_t x = s_tempo[i];
        int j = i - 1;
        while (j >= 0 && s_tempo[j].ymd > x.ymd) { s_tempo[j + 1] = s_tempo[j]; j--; }
        s_tempo[j + 1] = x;
    }
}

static void tempo_save(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *d = cJSON_AddObjectToObject(root, "d");
    for (int i = 0; i < s_tempo_n; i++) {
        char k[12];
        snprintf(k, sizeof(k), "%lu", (unsigned long)s_tempo[i].ymd);
        cJSON_AddNumberToObject(d, k, s_tempo[i].color);
    }
    char *js = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!js) return;
    FILE *f = fopen(TEMPO_FILE ".tmp", "w");
    if (f) {
        bool ok = fputs(js, f) >= 0;
        ok = (fclose(f) == 0) && ok;
        if (ok) { remove(TEMPO_FILE); rename(TEMPO_FILE ".tmp", TEMPO_FILE); }
    }
    free(js);
}

/** Couleur enregistree d'un jour, 0 si inconnue (verrou tenu). */
static int tempo_color(uint32_t ymd)
{
    for (int i = s_tempo_n - 1; i >= 0; i--) {
        if (s_tempo[i].ymd == ymd) return s_tempo[i].color;
        if (s_tempo[i].ymd < ymd) break;
    }
    return 0;
}

static void tempo_record(uint32_t ymd, int color)
{
    if (!s_tempo_loaded) tempo_load();
    if (!s_tempo || color < 1 || color > 3) return;
    if (s_tempo_n > 0 && s_tempo[s_tempo_n - 1].ymd == ymd) {
        if (s_tempo[s_tempo_n - 1].color == color) return;
        s_tempo[s_tempo_n - 1].color = (uint8_t)color;
    } else {
        if (s_tempo_n > 0 && s_tempo[s_tempo_n - 1].ymd > ymd) return;   // horloge en arriere
        if (s_tempo_n == TEMPO_MAX) {
            memmove(&s_tempo[0], &s_tempo[1], sizeof(tempo_day_t) * (TEMPO_MAX - 1));
            s_tempo_n--;
        }
        s_tempo[s_tempo_n++] = { ymd, (uint8_t)color };
    }
    tempo_save();
}

/**
 * Rouges et blancs de la derniere saison complete (1er septembre - 31 aout)
 * si la box en connait presque tous les jours (360 sur 365). false sinon.
 */
static bool tempo_season_counts(const struct tm *now, int *red, int *white)
{
    int start = now->tm_mon >= 8 ? now->tm_year + 1900 - 1 : now->tm_year + 1900 - 2;
    uint32_t from = (uint32_t)start * 10000 + 901, to = (uint32_t)(start + 1) * 10000 + 831;
    int r = 0, w = 0, known = 0;
    for (int i = 0; i < s_tempo_n; i++) {
        if (s_tempo[i].ymd < from || s_tempo[i].ymd > to) continue;
        known++;
        if (s_tempo[i].color == 3) r++;
        else if (s_tempo[i].color == 2) w++;
    }
    if (known < 360) return false;
    *red = r;
    *white = w;
    return true;
}

void energy_advice_minute_tick(void)
{
    struct tm t;
    // Le jour Tempo va de 6 h a 6 h : avant 6 h, la couleur est celle de la veille
    if (!local_now(&t) || t.tm_hour < 6) return;
    es_tariff_t tr;
    if (!es_tariff_now(&tr) || tr.today < 1 || tr.today > 3) return;
    lock();
    tempo_record(ymd_of(&t), tr.today);
    unlock();
}

// ============================================================================
// Meteo de l'electricite, cout de l'instant, risque de coupure
// ============================================================================

typedef enum { PL_SURPLUS, PL_CHEAP, PL_FLAT, PL_NORMAL, PL_EXPENSIVE, PL_PEAK } price_level_t;
static const char *const PL_KEY[6] = { "surplus", "cheap", "flat", "normal", "expensive", "peak" };

static const char *day_name(int c)
{
    return c == 3 ? "jour rouge" : c == 2 ? "jour blanc" : "jour bleu";
}

/** Meteo ; -1 sans tarif connu. */
static int weather(const es_tariff_t *tr, const energy_live_t *L, cJSON *root)
{
    if (!tr->known && (!tr->label[0] || !strcmp(tr->label, "---"))) return -1;
    char up[32];
    upper(tr->label, up, sizeof(up));
    bool peak_hours = strstr(up, "HP") || strstr(up, "PLEINE") || strstr(up, "POINTE");
    bool is_base = !strncmp(up, "BASE", 4) || !strncmp(up, "TH", 2) || tr->family == 0;
    bool ejp_peak = strstr(up, "POINTE") != NULL;
    int today = tr->today;                       // Tempo seulement
    char next[16] = "";
    bool has_next = clock_text(tr->next, next, sizeof(next));
    // Un surplus, c'est injecter plus qu'on ne tire du reseau
    bool injecting = L->valid && L->inj_va > 0 && L->inj_va > L->total_va;

    char title[48];
    if (is_base) strlcpy(title, "Tarif Base", sizeof(title));
    else if (ejp_peak) strlcpy(title, "Pointe mobile", sizeof(title));
    else if (today) snprintf(title, sizeof(title), "%s · %s", peak_hours ? "Heures pleines" : "Heures creuses", day_name(today));
    else if (strstr(up, "NORMALE")) strlcpy(title, "Heures normales", sizeof(title));
    else strlcpy(title, peak_hours ? "Heures pleines" : "Heures creuses", sizeof(title));

    price_level_t level;
    char text[200];
    if (ejp_peak || (today == 3 && peak_hours)) {
        level = PL_PEAK;
        if (ejp_peak)
            strlcpy(text, "L'électricité est très chère aujourd'hui : réduis au maximum, surtout le chauffage et le four.", sizeof(text));
        else
            snprintf(text, sizeof(text), "Jour rouge : l'électricité est très chère jusqu'à %s. Évite le four, le sèche-linge et le chauffage d'appoint.",
                     has_next ? next : "22 h");
    } else if (today == 2 && peak_hours) {
        level = PL_EXPENSIVE;
        snprintf(text, sizeof(text), "Jour blanc : l'électricité est plus chère. Décale ce qui peut attendre les heures creuses, à partir de %s.",
                 has_next ? next : "22 h");
    } else if (is_base) {
        level = PL_FLAT;
        strlcpy(text, "Le prix est le même à toute heure : ce qui compte, c'est ce que tu consommes au total.", sizeof(text));
    } else if (peak_hours) {
        level = PL_NORMAL;
        snprintf(text, sizeof(text), "Si tu peux, garde les gros appareils (lave-linge, lave-vaisselle, sèche-linge) pour les heures creuses%s%s.",
                 has_next ? ", dès " : "", has_next ? next : "");
    } else if (today == 3) {
        level = PL_CHEAP;
        strlcpy(text, "Jour rouge, mais heures creuses : profites-en pour ce qui ne peut pas attendre, car dès 6 h le prix grimpe fortement.", sizeof(text));
    } else {
        level = PL_CHEAP;
        strlcpy(text, "C'est le bon moment : lance la machine, le lave-vaisselle ou la recharge de la voiture.", sizeof(text));
    }
    // Du soleil en trop vaut mieux que n'importe quel tarif, sauf en jour rouge
    if (injecting && level != PL_PEAK) {
        level = PL_SURPLUS;
        strlcpy(text, "Tes panneaux produisent plus que tu ne consommes : c'est le moment de lancer un appareil.", sizeof(text));
    }

    if (root) {
        cJSON *w = cJSON_AddObjectToObject(root, "weather");
        char code[24];
        snprintf(code, sizeof(code), "weather.%s", PL_KEY[level]);
        cJSON_AddStringToObject(w, "code", code);
        cJSON_AddStringToObject(w, "level", PL_KEY[level]);
        cJSON_AddStringToObject(w, "title", title);
        cJSON_AddStringToObject(w, "text", text);
        if (tr->price > 0) cJSON_AddNumberToObject(w, "price", round4(tr->price));
        if (tr->next[0]) cJSON_AddStringToObject(w, "next", tr->next);
        if (today && tr->tomorrow >= 1 && tr->tomorrow <= 3) cJSON_AddStringToObject(w, "tomorrow", COLOR_KEY[tr->tomorrow]);
        if (today) cJSON_AddStringToObject(w, "today", COLOR_KEY[today]);
    }
    return level;
}

typedef enum { RL_LOW, RL_MEDIUM, RL_HIGH } risk_level_t;
static const char *const RL_KEY[3] = { "low", "medium", "high" };

static risk_level_t risk_of(double ratio)
{
    return ratio >= 0.9 ? RL_HIGH : ratio >= 0.7 ? RL_MEDIUM : RL_LOW;
}

/** Risque de coupure ; -1 sans abonnement ni charge connus. */
static int cut_risk(const energy_live_t *L, cJSON *root)
{
    if (!L->valid) return -1;
    // Charge de la derniere minute, deja rapportee a la limite de la phase la plus chargee
    int load = power_stats_minute_load_pct();
    double ratio;
    bool averaged = load >= 0;
    int phase = 0;
    if (L->tri) {
        long limit = L->goal_va > 0 ? L->goal_va / 3 : 0;
        if (limit <= 0) {
            if (load < 0) return -1;
            ratio = load / 100.0;
        } else {
            int worst = 0;
            for (int i = 1; i < 3; i++) if (L->phase_va[i] > L->phase_va[worst]) worst = i;
            ratio = load >= 0 ? load / 100.0 : (double)L->phase_va[worst] / limit;
            if (load < 0) phase = worst + 1;
        }
    } else if (load >= 0) {
        ratio = load / 100.0;
    } else {
        if (L->goal_va <= 0) return -1;
        ratio = (double)L->total_va / L->goal_va;
    }
    risk_level_t lv = risk_of(ratio);
    if (root) {
        static const char *const LABEL[3] = { "Faible", "À surveiller", "Élevé" };
        static const char *const TEXT[3] = {
            "Tu as de la marge avant que le disjoncteur saute.",
            "Évite d'allumer un autre gros appareil en même temps.",
            "Éteins un appareil maintenant : le disjoncteur risque de sauter.",
        };
        cJSON *o = cJSON_AddObjectToObject(root, "cutRisk");
        cJSON_AddStringToObject(o, "level", RL_KEY[lv]);
        cJSON_AddStringToObject(o, "label", LABEL[lv]);
        cJSON_AddNumberToObject(o, "ratio", round2(ratio));
        cJSON_AddBoolToObject(o, "averaged", averaged);
        if (phase) cJSON_AddNumberToObject(o, "phase", phase);
        else cJSON_AddNullToObject(o, "phase");
        cJSON_AddStringToObject(o, "text", TEXT[lv]);
    }
    return lv;
}

// ============================================================================
// Consommation de fond
// ============================================================================

/**
 * L'heure complete la plus calme des 24 dernieres heures, a n'importe quelle
 * heure : la nuit, c'est souvent la recharge de la voiture ou le ballon. Un
 * Linky en mode producteur (5 ou 7) mesure ce qui est tire du reseau : seules
 * les heures sans production comptent alors. Heures sans mesure ignorees ;
 * 12 heures mesurees au moins. -1 si inconnue.
 */
static int base_load(const energy_live_t *L, int *hour_out)
{
    bool self = L->mode == 5 || L->mode == 7;
    struct tm now;
    local_now(&now);
    int measured = 0, kept = 0, best = -1, best_hour = -1;
    for (int ago = 1; ago < ENERGY_HOURS; ago++) {
        es_slot_t s;
        if (!es_hour_slot(ago, &s) || s.wh <= 0) continue;
        measured++;
        if (self && s.prod_wh > 0) continue;
        kept++;
        if (best < 0 || s.wh < best) {
            best = (int)s.wh;
            best_hour = (now.tm_hour - ago + 48) % 24;
        }
    }
    if (measured < 12 || kept < 4 || best <= 0) return -1;
    if (hour_out) *hour_out = best_hour;
    return best;                                   // Wh en 1 h = W moyens
}

static void base_load_json(const energy_live_t *L, cJSON *root)
{
    int hour = -1;
    int w = base_load(L, &hour);
    if (w <= 0) return;
    double kwh = w * 8760.0 / 1000.0;
    double price = es_avg_price();
    cJSON *o = cJSON_AddObjectToObject(root, "baseLoad");
    cJSON_AddNumberToObject(o, "w", w);
    cJSON_AddNumberToObject(o, "hour", hour);
    cJSON_AddNumberToObject(o, "kwhYear", round(kwh));
    if (price > 0) cJSON_AddNumberToObject(o, "eurYear", round(kwh * price));
    // Graphe : les heures completes des dernieres 24 h, [heure, Wh, Wh produits]
    struct tm now;
    local_now(&now);
    cJSON *hours = cJSON_AddArrayToObject(o, "hours");
    for (int ago = ENERGY_HOURS - 1; ago >= 1; ago--) {
        es_slot_t s;
        if (!es_hour_slot(ago, &s)) continue;
        cJSON *h = cJSON_CreateArray();
        cJSON_AddItemToArray(h, cJSON_CreateNumber((now.tm_hour - ago + 48) % 24));
        cJSON_AddItemToArray(h, cJSON_CreateNumber((double)(s.wh > 0 ? s.wh : 0)));
        cJSON_AddItemToArray(h, cJSON_CreateNumber((double)(s.prod_wh > 0 ? s.prod_wh : 0)));
        cJSON_AddItemToArray(hours, h);
    }
    char text[200];
    snprintf(text, sizeof(text),
             "Même au plus calme, la maison consomme %d W : frigo, box internet, veilles... "
             "Sur un an, cela fait %.0f kWh.", w, kwh);
    cJSON_AddStringToObject(o, "text", text);
}

// ============================================================================
// Abonnement (d'apres /api/power/summary)
// ============================================================================

static int jint(const cJSON *v)
{
    if (cJSON_IsNumber(v)) return v->valueint;
    if (cJSON_IsObject(v)) return jint(cJSON_GetObjectItem(v, "va"));
    return -1;
}

static void subscription_json(cJSON *root)
{
    char *js = power_stats_summary_json();
    if (!js) return;
    cJSON *s = cJSON_Parse(js);
    free(js);
    if (!s) return;
    int sub = jint(cJSON_GetObjectItem(s, "subscribedVA"));
    if (sub <= 0) { cJSON_Delete(s); return; }
    bool tri = cJSON_IsTrue(cJSON_GetObjectItem(s, "triphase"));
    const cJSON *worst = cJSON_GetObjectItem(s, "worst");
    const cJSON *months = cJSON_GetObjectItem(s, "months");

    // Le pire des 90 derniers jours et de chaque mois connu (24 mois)
    int peak = jint(cJSON_GetObjectItem(worst, "va1m"));
    int phase_peak = -1;
    const cJSON *m, *p;
    cJSON_ArrayForEach(m, months) {
        int v = jint(cJSON_GetObjectItem(m, "va1m"));
        if (v > peak) peak = v;
        if (tri) cJSON_ArrayForEach(p, cJSON_GetObjectItem(m, "phases")) {
            int pv = jint(cJSON_GetObjectItem(p, "va1m"));
            if (pv > phase_peak) phase_peak = pv;
        }
    }
    if (tri) cJSON_ArrayForEach(p, cJSON_GetObjectItem(worst, "phases")) {
        int pv = jint(cJSON_GetObjectItem(p, "va1m"));
        if (pv > phase_peak) phase_peak = pv;
    }

    // Frequence des tranches 80, 90 et 100 %
    int observed = 0, days30_80 = -1;
    char rate80[12] = "", rate90[12] = "", rate100[12] = "";
    const cJSON *fq;
    cJSON_ArrayForEach(fq, cJSON_GetObjectItem(s, "frequency")) {
        int band = atoi(fq->string);
        const cJSON *r = cJSON_GetObjectItem(fq, "rate");
        int od = jint(cJSON_GetObjectItem(fq, "observedDays"));
        if (od > observed) observed = od;
        const char *rs = cJSON_IsString(r) ? r->valuestring : "";
        if (band == 80) { strlcpy(rate80, rs, sizeof(rate80)); days30_80 = jint(cJSON_GetObjectItem(fq, "days30")); }
        else if (band == 90) strlcpy(rate90, rs, sizeof(rate90));
        else if (band == 100) strlcpy(rate100, rs, sizeof(rate100));
    }

    auto often = [](const char *rate, bool monthly) {
        return !strcmp(rate, "daily") || !strcmp(rate, "weekly") || (monthly && !strcmp(rate, "monthly"));
    };
    int kva = (int)lround(sub / 1000.0);
    bool tight = often(rate90, false) || often(rate100, true);
    bool rarely_high = !often(rate80, true);
    // Triphase : la phase la plus chargee coupe a un tiers de l'abonnement
    int sizing = (tri && phase_peak > 0) ? phase_peak * 3 : peak;
    int suggested = 0;
    if (!tight && rarely_high && sizing > 0 && observed >= SUB_MIN_DAYS) {
        int enough = kva;
        for (int i = 0; i < GRID_TIERS; i++) {
            if (TIER_KVA[i] * 1000.0 >= sizing * 1.15) { enough = TIER_KVA[i]; break; }
        }
        if (enough < kva) suggested = enough;
    }

    cJSON *o = cJSON_AddObjectToObject(root, "subscription");
    cJSON_AddNumberToObject(o, "kva", kva);
    if (peak > 0) cJSON_AddNumberToObject(o, "peakVA", peak);
    else cJSON_AddNullToObject(o, "peakVA");
    cJSON_AddNumberToObject(o, "days", observed);
    cJSON_AddBoolToObject(o, "tight", tight);
    if (suggested) cJSON_AddNumberToObject(o, "suggestedKva", suggested);
    else cJSON_AddNullToObject(o, "suggestedKva");
    if (tri && phase_peak > 0) {
        cJSON_AddNumberToObject(o, "phasePeakVA", phase_peak);
        cJSON_AddNumberToObject(o, "phaseLimitVA", lround(sub / 3.0));
    }
    if (days30_80 >= 0) cJSON_AddNumberToObject(o, "days30Over80", days30_80);
    // Graphe : plus fort appel (moyenne 1 min) de chaque mois connu, et la
    // limite a laquelle le comparer (abonnement, ou phase en triphase)
    cJSON_AddStringToObject(o, "basis", tri ? "phase" : "total");
    cJSON_AddNumberToObject(o, "limitVA", tri ? lround(sub / 3.0) : sub);
    cJSON *mp = cJSON_AddArrayToObject(o, "months");
    cJSON_ArrayForEach(m, months) {
        const cJSON *mk = cJSON_GetObjectItem(m, "month");
        int v = -1;
        if (tri) {
            cJSON_ArrayForEach(p, cJSON_GetObjectItem(m, "phases")) {
                int pv = jint(cJSON_GetObjectItem(p, "va1m"));
                if (pv > v) v = pv;
            }
        } else {
            v = jint(cJSON_GetObjectItem(m, "va1m"));
        }
        if (!cJSON_IsString(mk) || v <= 0) continue;
        cJSON *e = cJSON_CreateArray();
        cJSON_AddItemToArray(e, cJSON_CreateString(mk->valuestring));
        cJSON_AddItemToArray(e, cJSON_CreateNumber(v));
        cJSON_AddItemToArray(mp, e);
    }
    char text[220];
    if (observed < SUB_MIN_DAYS)
        snprintf(text, sizeof(text), "Encore %d jours de mesures avant de juger l'abonnement.", SUB_MIN_DAYS - observed);
    else if (tight)
        snprintf(text, sizeof(text), "Ton abonnement de %d kVA est juste : la consommation frôle souvent le maximum. Étale les gros appareils pour éviter les coupures.", kva);
    else if (suggested)
        snprintf(text, sizeof(text), "Ton plus fort appel est de %.1f kVA : un abonnement de %d kVA suffirait au lieu de %d kVA, et coûterait moins cher.",
                 sizing / 1000.0, suggested, kva);
    else
        snprintf(text, sizeof(text), "Ton abonnement de %d kVA est bien taillé pour ta consommation.", kva);
    cJSON_AddStringToObject(o, "text", text);
    cJSON_Delete(s);
}

// ============================================================================
// Mois en cours, budget, hier
// ============================================================================

static double budget_eur(void)
{
    const cfg_notifications_t *n = cfg_get_notifications();
    return n->over_budget_threshold > 0 ? (double)n->over_budget_threshold : 0;
}

static void month_json(cJSON *root)
{
    struct tm now;
    if (!local_now(&now)) return;
    int64_t wh;
    double var, fixed;
    if (!es_month_cost(&wh, &var, &fixed) || wh <= 0) return;
    int len = days_in_month(&now);
    double elapsed = (now.tm_mday - 1) + (now.tm_hour + now.tm_min / 60.0) / 24.0;
    bool euros = var > 0;
    double so_far, projected;
    if (euros) {
        // Abonnement et CTA : au prorata jusqu'ici, une fois sur le mois entier
        so_far = var + fixed * (elapsed / len);
        projected = (elapsed < 1 ? var : var / elapsed * len) + fixed;
    } else {
        so_far = wh / 1000.0;
        projected = elapsed < 1 ? so_far : so_far / elapsed * len;
    }
    cJSON *o = cJSON_AddObjectToObject(root, "month");
    cJSON_AddNumberToObject(o, "soFar", round2(so_far));
    cJSON_AddNumberToObject(o, "projected", round2(projected));
    cJSON_AddStringToObject(o, "unit", euros ? "EUR" : "kWh");
    cJSON_AddNumberToObject(o, "days", len);
    double budget = budget_eur();
    if (budget > 0 && euros) {
        cJSON_AddNumberToObject(o, "budget", budget);
        // Budget rapporte a une heure, en Wh, au cout reel des dernieres 24 h
        double eur_h = budget / len / 24.0, wh24 = 0, eur24 = 0;
        for (int ago = 0; ago < ENERGY_HOURS; ago++) {
            es_slot_t s;
            if (es_hour_slot(ago, &s)) { wh24 += s.wh; eur24 += s.eur; }
        }
        double per_wh = (wh24 > 0 && eur24 > 0) ? eur24 / wh24 : es_avg_price() / 1000.0;
        if (per_wh > 0) cJSON_AddNumberToObject(o, "hourlyBudgetWh", round(eur_h / per_wh));
        cJSON_AddNumberToObject(o, "budgetDay", round2(budget / len));
    }
    // Graphe : chaque jour du mois jusqu'a aujourd'hui (en cours compris),
    // en EUR (energie, taxes, part d'abonnement) ou en kWh
    cJSON *daily = cJSON_AddArrayToObject(o, "daily");
    for (int d = 1; d <= now.tm_mday; d++) {
        es_slot_t s;
        double v = 0;
        if (es_day_slot(now.tm_mday - d, &s) && s.wh > 0) v = euros ? s.eur : s.wh / 1000.0;
        cJSON_AddItemToArray(daily, cJSON_CreateNumber(round2(v)));
    }
}

/** Hier compare aux 7 jours precedents ; NULL si l'ecart est faible ou l'historique trop court. */
static cJSON *yesterday_alert(void)
{
    es_slot_t y;
    if (!es_day_slot(1, &y) || y.wh <= 0) return NULL;
    int64_t sum = 0;
    int cnt = 0;
    for (int k = 2; k <= 8; k++) {
        es_slot_t d;
        if (es_day_slot(k, &d) && d.wh > 0) { sum += d.wh; cnt++; }
    }
    if (cnt < 3) return NULL;
    double usual = (double)sum / cnt;
    double change = (y.wh - usual) / usual * 100.0;
    int pct = cfg_get_notifications()->daily_anomaly_percent;
    if (pct <= 0) pct = 30;
    char title[80], text[200];
    const char *level, *code;
    if (change >= pct) {
        level = "warning";
        code = "alert.yesterday_high";
        snprintf(title, sizeof(title), "Hier, +%d %% par rapport à d'habitude", (int)lround(change));
        strlcpy(text, "Un appareil inhabituel, du chauffage en plus ? Regarde le graphe heure par heure pour trouver le moment.", sizeof(text));
    } else if (change <= -15) {
        level = "info";
        code = "alert.yesterday_low";
        snprintf(title, sizeof(title), "Bravo : hier, %d %% de moins que d'habitude", (int)lround(-change));
        strlcpy(text, "Continue comme ça : chaque jour comme celui-là allège la facture.", sizeof(text));
    } else {
        return NULL;
    }
    cJSON *a = cJSON_CreateObject();
    cJSON_AddStringToObject(a, "code", code);
    cJSON_AddStringToObject(a, "level", level);
    cJSON_AddStringToObject(a, "title", title);
    cJSON_AddStringToObject(a, "text", text);
    return a;
}

static void add_alert(cJSON *arr, const char *code, const char *level, const char *title, const char *text)
{
    cJSON *a = cJSON_CreateObject();
    cJSON_AddStringToObject(a, "code", code);
    cJSON_AddStringToObject(a, "level", level);
    cJSON_AddStringToObject(a, "title", title);
    cJSON_AddStringToObject(a, "text", text);
    cJSON_AddItemToArray(arr, a);
}

int energy_advice_water_running_hours(void)
{
    const cfg_general_t *g = cfg_get_general();
    if (!g->water[0]) return -1;
    const energy_history_t *h = energy_history_find(strtoull(g->water, NULL, 16));
    if (!h) return -1;
    struct tm now;
    local_now(&now);
    int count = 0;
    bool night = false;
    for (int ago = 1; ago < ENERGY_HOURS; ago++) {
        int hour = (now.tm_hour - ago + 48) % 24;
        if (energy_history_slot_value(h, ENERGY_GRAN_HOUR, hour, 0) <= 0) break;
        count++;
        if (hour >= 1 && hour <= 5) night = true;
    }
    return night ? count : 0;
}

/** Alertes calculees : puissance, eau qui coule, demain en Tempo, hier. */
static void alerts_json(const es_tariff_t *tr, bool have_tariff, const energy_live_t *L, cJSON *root)
{
    cJSON *arr = cJSON_AddArrayToObject(root, "alerts");
    const cfg_notifications_t *n = cfg_get_notifications();
    if (L->valid && L->total_va > 0) {
        long limit = (n->over_power && n->over_power_threshold > 0) ? n->over_power_threshold
                     : (L->goal_va > 0 ? L->goal_va * 9 / 10 : 0);
        if (limit > 0 && L->total_va >= limit)
            add_alert(arr, "alert.near_max_power", "danger", "Tu approches de ta puissance maximale",
                      "Le disjoncteur risque de sauter : éteins un gros appareil (four, radiateur, plaque) avant d'en allumer un autre.");
    }
    int running = energy_advice_water_running_hours();
    if (running >= 3) {
        char text[200];
        snprintf(text, sizeof(text), "L'eau coule sans s'arrêter depuis %d heures, même la nuit. "
                 "Vérifie les robinets, la chasse d'eau et le chauffe-eau.", running);
        add_alert(arr, "alert.water_continuous", "danger", "Fuite d'eau possible", text);
    }
    if (have_tariff && tr->today) {
        if (tr->tomorrow == 3)
            add_alert(arr, "alert.tomorrow_red", "warning", "Demain, jour rouge",
                      "L'électricité sera très chère de 6 h à 22 h. Ce soir, programme le chauffe-eau et les machines en heures creuses.");
        else if (tr->tomorrow == 2)
            add_alert(arr, "alert.tomorrow_white", "info", "Demain, jour blanc",
                      "L'électricité sera un peu plus chère de 6 h à 22 h : garde les machines pour les heures creuses.");
    }
    cJSON *y = yesterday_alert();
    if (y) cJSON_AddItemToArray(arr, y);
}

// ============================================================================
// Comparaison des options du tarif reglemente
// ============================================================================

typedef struct {
    uint32_t ymd;
    double   kwh;
    double   low;          // part des heures creuses (0..1)
    bool     low_by_index; // d'apres les index HC/HP du jour (sinon de 22 h a 6 h)
    int      color;        // 0 inconnue, 1 bleu, 2 blanc, 3 rouge
} cmp_day_t;

/**
 * Un jour de l'archive horaire : consommation, part des heures creuses (par
 * index avec un contrat HC ou Tempo, sinon de 22 h a 6 h) et couleur Tempo
 * deduite des index (contrat Tempo). false sans donnees.
 */
static bool archive_day(uint32_t ymd, const char *id, bool by_index, cmp_day_t *out)
{
    cJSON *root = energy_archive_load('h', ymd);
    if (!root) return false;
    const cJSON *dev = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "e"), id);
    double total = 0, night = 0, idx = 0, low = 0, color[3] = { 0, 0, 0 };
    const cJSON *hr, *at;
    cJSON_ArrayForEach(hr, dev) {
        int h = atoi(hr->string);
        double sum = 0, zero = 0;
        bool any_idx = false;
        cJSON_ArrayForEach(at, hr) {
            int a = atoi(at->string);
            double v = cJSON_IsNumber(at) ? at->valuedouble : 0;
            if (v <= 0) continue;
            if (a == 0) { zero += v; continue; }
            if (a < 256 || a > 274 || (a & 1)) continue;
            any_idx = true;
            sum += v;
            if (a <= 266) {
                int rank = (a - 256) / 2;
                idx += v;
                if (!(rank & 1)) low += v;
                color[rank / 2] += v;
            }
        }
        if (!any_idx) sum = zero;
        total += sum;
        if (h >= 22 || h < 6) night += sum;
    }
    cJSON_Delete(root);
    if (total <= 0) return false;
    out->ymd = ymd;
    out->kwh = total / 1000.0;
    if (by_index && idx >= total * 0.5) {
        out->low = low / idx;
        out->low_by_index = true;
    } else {
        out->low = night / total;
        out->low_by_index = false;
    }
    out->color = 0;
    if (by_index && idx > 0) {
        int best = 0;
        for (int c = 1; c < 3; c++) if (color[c] > color[best]) best = c;
        out->color = best + 1;
    }
    return true;
}

typedef enum { OPT_BASE, OPT_HC, OPT_TEMPO } option_t;
static const char *const OPT_KEY[3] = { "base", "offPeak", "tempo" };
static const char *const OPT_LABEL[3] = { "Base", "Heures creuses", "Tempo" };

static const double *opt_abo(const grid_t *g, option_t o)
{
    return o == OPT_BASE ? g->base_abo : o == OPT_HC ? g->hc_abo : g->tempo_abo;
}

/** Palier propose par l'option : la puissance, ou la plus proche au-dessus ; -1 sinon. */
static int opt_tier(const grid_t *g, option_t o, int kva)
{
    const double *abo = opt_abo(g, o);
    for (int i = 0; i < GRID_TIERS; i++) if (TIER_KVA[i] >= kva && abo[i] > 0) return i;
    return -1;
}

/** Prix Tempo d'un jour : HC et HP selon la couleur, ou moyenne de saison si elle est inconnue. */
static double tempo_day_price(const grid_t *g, int color, double low, int red, int white)
{
    if (color >= 1 && color <= 3) return low * g->tempo[color][0] + (1 - low) * g->tempo[color][1];
    double blue = 365 - red - white;
    double e = 0;
    for (int c = 1; c <= 3; c++) {
        double w = (c == 1 ? blue : c == 2 ? white : red) / 365.0;
        e += w * (low * g->tempo[c][0] + (1 - low) * g->tempo[c][1]);
    }
    return e;
}

static cJSON *s_cmp_cache;
static uint32_t s_cmp_key;

static cJSON *tariffs_compute(const energy_live_t *L, int family)
{
    struct tm now;
    if (!local_now(&now)) return NULL;
    if (L->goal_va <= 0) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddNumberToObject(p, "needDays", MIN_DAYS);
        cJSON_AddStringToObject(p, "text", "Comparaison impossible pour l'instant : le compteur n'a pas encore donné la puissance souscrite.");
        return p;
    }
    uint64_t zl = es_zlinky();
    if (!zl) return NULL;
    char id[20];
    snprintf(id, sizeof(id), "%016llX", (unsigned long long)zl);
    // Option du contrat : 0 Base, 1 HC/HP, 3 Tempo ; EJP et autres : inconnue
    int current = family == 0 ? OPT_BASE : family == 1 ? OPT_HC : family == 3 ? OPT_TEMPO : -1;
    bool by_index = family == 1 || family == 3;

    cmp_day_t *days = (cmp_day_t *)heap_caps_calloc(CMP_DAYS, sizeof(cmp_day_t), MALLOC_CAP_SPIRAM);
    if (!days) days = (cmp_day_t *)calloc(CMP_DAYS, sizeof(cmp_day_t));
    if (!days) return NULL;
    int nd = 0;
    for (int ago = 1; ago <= CMP_DAYS; ago++) {
        struct tm d;
        shift_day(&now, -ago, &d);
        if (archive_day(ymd_of(&d), id, by_index, &days[nd])) nd++;
    }
    if (nd < MIN_DAYS) {
        // Historique trop court : la carte dit quand la comparaison sera prete
        free(days);
        cJSON *p = cJSON_CreateObject();
        cJSON_AddNumberToObject(p, "days", nd);
        cJSON_AddNumberToObject(p, "needDays", MIN_DAYS);
        char text[160];
        int left = MIN_DAYS - nd;
        snprintf(text, sizeof(text), "Comparaison disponible dans %d jour%s : il faut %d jours complets de mesures heure par heure (%d pour l'instant).",
                 left, left > 1 ? "s" : "", MIN_DAYS, nd);
        cJSON_AddStringToObject(p, "text", text);
        return p;
    }

    grid_t g;
    grid_load(&g);
    int red_year = 22, white_year = 43;
    bool season = false;
    lock();
    if (!s_tempo_loaded) tempo_load();
    for (int i = 0; i < nd; i++) {
        int c = s_tempo ? tempo_color(days[i].ymd) : 0;
        if (c) days[i].color = c;                  // vue par la box : prioritaire
    }
    if (s_tempo) season = tempo_season_counts(&now, &red_year, &white_year);
    unlock();

    double kwh = 0, low_w = 0;
    int red = 0, white = 0, known = 0, by_idx = 0;
    for (int i = 0; i < nd; i++) {
        kwh += days[i].kwh;
        low_w += days[i].kwh * days[i].low;
        if (days[i].color) known++;
        if (days[i].color == 3) red++;
        if (days[i].color == 2) white++;
        if (days[i].low_by_index) by_idx++;
    }
    if (kwh <= 0) { free(days); return NULL; }
    double low = low_w / kwh;
    int kva = (int)lround(L->goal_va / 1000.0);

    cJSON *o = cJSON_CreateObject();
    if (current >= 0) cJSON_AddStringToObject(o, "current", OPT_KEY[current]);
    else cJSON_AddNullToObject(o, "current");
    cJSON_AddNumberToObject(o, "days", nd);
    cJSON_AddNumberToObject(o, "offPeakShare", round2(low));
    cJSON_AddStringToObject(o, "offPeakSource", by_idx * 2 >= nd ? "index" : "hours");
    cJSON_AddNumberToObject(o, "redDays", red);
    cJSON_AddNumberToObject(o, "whiteDays", white);
    cJSON_AddNumberToObject(o, "colorsKnown", known);
    cJSON_AddStringToObject(o, "gridFrom", g.from);
    {
        int y, m, dd;
        if (sscanf(g.from, "%d-%d-%d", &y, &m, &dd) == 3) {
            int age = (now.tm_year + 1900 - y) * 12 + (now.tm_mon + 1 - m);
            cJSON_AddBoolToObject(o, "gridOutdated", age > 7);
        }
    }
    cJSON_AddBoolToObject(o, "seasonMeasured", season);

    cJSON *period = cJSON_AddArrayToObject(o, "period");
    cJSON *year = cJSON_AddArrayToObject(o, "year");
    double avg = kwh / nd;
    double best_year = -1, cur_year = -1;
    int best_opt = -1;
    for (int opt = 0; opt < 3; opt++) {
        int ti = opt_tier(&g, (option_t)opt, kva);
        if (ti < 0) continue;
        double abo = opt_abo(&g, (option_t)opt)[ti];
        double ep = 0, ey = 0;
        switch (opt) {
        case OPT_BASE:
            ep = kwh * g.base_kwh[ti];
            ey = avg * 365 * g.base_kwh[ti];
            break;
        case OPT_HC: {
            double pr = low * g.hc + (1 - low) * g.hp;
            ep = kwh * pr;
            ey = avg * 365 * pr;
            break;
        }
        default: {
            for (int i = 0; i < nd; i++) ep += days[i].kwh * tempo_day_price(&g, days[i].color, days[i].low, red_year, white_year);
            int blue_year = 365 - red_year - white_year;
            ey = avg * (red_year * tempo_day_price(&g, 3, low, 0, 0) + white_year * tempo_day_price(&g, 2, low, 0, 0) +
                        blue_year * tempo_day_price(&g, 1, low, 0, 0));
            break;
        }
        }
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "option", OPT_KEY[opt]);
        cJSON_AddStringToObject(p, "label", OPT_LABEL[opt]);
        cJSON_AddNumberToObject(p, "kva", TIER_KVA[ti]);
        cJSON_AddNumberToObject(p, "energy", round2(ep));
        cJSON_AddNumberToObject(p, "subscription", round2(abo * nd / 365.0));
        cJSON_AddNumberToObject(p, "total", round2(ep + abo * nd / 365.0));
        cJSON_AddItemToArray(period, p);
        cJSON *q = cJSON_CreateObject();
        cJSON_AddStringToObject(q, "option", OPT_KEY[opt]);
        cJSON_AddStringToObject(q, "label", OPT_LABEL[opt]);
        cJSON_AddNumberToObject(q, "kva", TIER_KVA[ti]);
        cJSON_AddNumberToObject(q, "energy", round2(ey));
        cJSON_AddNumberToObject(q, "subscription", round2(abo));
        cJSON_AddNumberToObject(q, "total", round2(ey + abo));
        cJSON_AddItemToArray(year, q);
        double ty = ey + abo;
        if (best_year < 0 || ty < best_year) { best_year = ty; best_opt = opt; }
        if (opt == current) cur_year = ty;
    }
    free(days);
    if (best_opt >= 0) cJSON_AddStringToObject(o, "cheapest", OPT_KEY[best_opt]);
    char text[220];
    if (best_opt >= 0 && cur_year > 0 && best_opt != current && cur_year - best_year >= 1) {
        cJSON_AddNumberToObject(o, "savingYear", round(cur_year - best_year));
        snprintf(text, sizeof(text), "Au tarif réglementé, l'option %s t'aurait coûté environ %.0f € de moins sur un an.",
                 OPT_LABEL[best_opt], cur_year - best_year);
    } else if (best_opt >= 0 && best_opt == current) {
        snprintf(text, sizeof(text), "Au tarif réglementé, ton option actuelle (%s) est la moins chère pour ta consommation.",
                 OPT_LABEL[best_opt]);
    } else if (best_opt >= 0) {
        snprintf(text, sizeof(text), "Au tarif réglementé, l'option %s serait la moins chère pour ta consommation.", OPT_LABEL[best_opt]);
    } else {
        text[0] = '\0';
    }
    if (text[0]) cJSON_AddStringToObject(o, "text", text);
    return o;
}

/** Comparaison mise en cache une heure (l'archive ne change qu'au fil des jours). */
static void tariffs_json(const energy_live_t *L, int family, cJSON *root)
{
    struct tm now;
    local_now(&now);
    uint32_t key = ymd_of(&now) * 100 + (uint32_t)now.tm_hour;
    lock();
    bool fresh = s_cmp_cache && s_cmp_key == key;
    cJSON *copy = fresh ? cJSON_Duplicate(s_cmp_cache, true) : NULL;
    unlock();
    if (!fresh) {
        cJSON *c = tariffs_compute(L, family);
        lock();
        if (s_cmp_cache) cJSON_Delete(s_cmp_cache);
        s_cmp_cache = c;
        s_cmp_key = key;
        copy = c ? cJSON_Duplicate(c, true) : NULL;
        unlock();
    }
    if (copy) cJSON_AddItemToObject(root, "tariffs", copy);
}

// ============================================================================
// API
// ============================================================================

char *energy_advice_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "at", (double)time(NULL));
    energy_live_t L;
    memset(&L, 0, sizeof(L));
    bool live = energy_service_get_live(&L);
    es_tariff_t tr;
    bool have_tariff = es_tariff_now(&tr);
    if (live && have_tariff) weather(&tr, &L, root);
    if (live && L.valid && have_tariff && tr.price > 0 && L.total_va >= 0)
        cJSON_AddNumberToObject(root, "costPerHour", round2(L.total_va / 1000.0 * tr.price));
    if (live) cut_risk(&L, root);
    if (live) base_load_json(&L, root);
    if (live) subscription_json(root);
    if (live) month_json(root);
    alerts_json(&tr, have_tariff, &L, root);
    if (live && have_tariff) tariffs_json(&L, tr.family, root);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

char *energy_advice_weather_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "at", (double)time(NULL));
    energy_live_t L;
    memset(&L, 0, sizeof(L));
    es_tariff_t tr;
    if (energy_service_get_live(&L) && es_tariff_now(&tr)) {
        weather(&tr, &L, root);
        if (L.valid && tr.price > 0) cJSON_AddNumberToObject(root, "costPerHour", round2(L.total_va / 1000.0 * tr.price));
    }
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

void energy_advice_levels(energy_advice_levels_t *out)
{
    memset(out, 0, sizeof(*out));
    out->base_load_w = -1;
    energy_live_t L;
    if (!energy_service_get_live(&L)) return;
    es_tariff_t tr;
    if (es_tariff_now(&tr)) {
        int w = weather(&tr, &L, NULL);
        if (w >= 0) strlcpy(out->weather, PL_KEY[w], sizeof(out->weather));
    }
    int r = cut_risk(&L, NULL);
    if (r >= 0) strlcpy(out->cut_risk, RL_KEY[r], sizeof(out->cut_risk));
    out->base_load_w = base_load(&L, NULL);
}
