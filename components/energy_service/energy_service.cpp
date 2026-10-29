// ============================================================================
// Energy Service - voir energy_service.h et docs/energy_api.md
//
// Portage des calculs de la LiXee-Box (zigbee.cpp getTotalEnergy,
// getPowerGaugeAbo, getTrendPower, getDatasPower ; web.cpp
// loadDistributionChart, buildEnergyChartJson, exports CSV, /api/tariff).
// Differences voulues avec l'original :
//  - la production est une serie positive a part, jamais un nombre negatif ;
//  - la part des sous-compteurs est retiree des index au prorata de chaque
//    index (l'historique d'un sous-compteur ne connait que son total) ;
//  - l'abonnement de l'annee en cours est compte sur les mois ecoules ;
//  - corrections des defauts releves dans l'original (cle du mois precedent,
//    debordement du tableau de couleurs, reponse "error" de la jauge).
// ============================================================================

#include "energy_service.h"
#include "power_stats.h"
#include "energy_history.h"
#include "device_data.h"
#include "config_manager.h"
#include "zigate_devices.h"
#include "ntp_sync.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

static const char *TAG = "NRG_SVC";

// ============================================================================
// Outils
// ============================================================================

/** Tampon texte extensible en PSRAM (gros JSON et CSV sans cJSON). */
typedef struct {
    char  *p;
    size_t len;
    size_t cap;
    bool   oom;
} sbuf_t;

static void sb_init(sbuf_t *b, size_t cap)
{
    b->p = (char *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!b->p) b->p = (char *)malloc(cap);
    b->len = 0;
    b->cap = b->p ? cap : 0;
    b->oom = (b->p == NULL);
    if (b->p) b->p[0] = '\0';
}

static void sb_printf(sbuf_t *b, const char *fmt, ...)
{
    if (b->oom) return;
    for (;;) {
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
        va_end(ap);
        if (n < 0) { b->oom = true; return; }
        if ((size_t)n < b->cap - b->len) { b->len += n; return; }
        size_t ncap = b->cap * 2 + n;
        char *np = (char *)heap_caps_realloc(b->p, ncap, MALLOC_CAP_SPIRAM);
        if (!np) np = (char *)realloc(b->p, ncap);
        if (!np) { b->oom = true; return; }
        b->p = np;
        b->cap = ncap;
    }
}

static char *sb_finish(sbuf_t *b)
{
    if (b->oom) { free(b->p); return NULL; }
    return b->p;
}

/** Nombre decimal "a la francaise" pour les CSV (virgule decimale). */
static void sb_dec(sbuf_t *b, double v, int decimals)
{
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%.*f", decimals, v);
    for (char *c = tmp; *c; c++) if (*c == '.') *c = ',';
    sb_printf(b, "%s", tmp);
}

static char *json_out(cJSON *root)
{
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

static uint64_t parse_ieee(const char *s)
{
    return (s && s[0]) ? strtoull(s, NULL, 16) : 0;
}

/** Prix saisi en texte, virgule decimale toleree. */
static double parse_num(const char *s)
{
    if (!s || !s[0]) return 0;
    char b[24];
    strlcpy(b, s, sizeof(b));
    for (char *c = b; *c; c++) if (*c == ',') *c = '.';
    return atof(b);
}

static double round2(double v) { return round(v * 100.0) / 100.0; }

// ============================================================================
// Configuration : roles, prix
// ============================================================================

typedef struct {
    uint64_t zl;      // ZLinky
    uint64_t prod;    // compteur de production (peut etre le ZLinky)
    uint64_t gaz;
    uint64_t water;
} roles_t;

static roles_t get_roles(void)
{
    const cfg_general_t *g = cfg_get_general();
    roles_t r = { parse_ieee(g->zlinky), parse_ieee(g->production),
                  parse_ieee(g->gaz), parse_ieee(g->water) };
    return r;
}

/** Prix au kWh d'un index : tarifIdx2 -> 256 ... tarifIdx10 -> 272 (274 sans prix). */
static double price_for_attr(uint16_t attr)
{
    const cfg_general_t *g = cfg_get_general();
    if (attr == 0) return parse_num(g->tarif_idx[1]);     // index total seul : prix BASE (tarifIdx2)
    if (attr >= 256 && attr <= 272 && !(attr & 1)) return parse_num(g->tarif_idx[(attr - 256) / 2 + 1]);
    return 0;
}

static double budget_month_eur(void)
{
    const cfg_notifications_t *n = cfg_get_notifications();
    return n->over_budget_threshold > 0 ? (double)n->over_budget_threshold : 0;
}

// ============================================================================
// Valeurs instantanees (device_data, chaine hexadecimale du dernier rapport)
// ============================================================================

static bool dd_num(uint64_t ieee, uint16_t cl, uint16_t at, int64_t *out)
{
    char buf[DEVDATA_VALUE_LEN];
    if (!ieee || device_data_get_value(ieee, cl, at, buf, sizeof(buf)) != ESP_OK || !buf[0]) return false;
    *out = (int64_t)strtoull(buf, NULL, 16);
    return true;
}

/** STGE (FF66/0x0217) : texte de 8 chiffres hexa, voir device_data_stge_value(). */
static bool dd_stge(uint64_t ieee, int64_t *out)
{
    char buf[DEVDATA_VALUE_LEN];
    uint32_t v;
    if (!ieee || device_data_get_value(ieee, 0xFF66, 0x0217, buf, sizeof(buf)) != ESP_OK ||
        !device_data_stge_value(buf, &v)) return false;
    *out = v;
    return true;
}

/** Attribut chaine : octets en hexadecimal -> texte, espaces de fin retires. */
static bool dd_str(uint64_t ieee, uint16_t cl, uint16_t at, char *out, size_t len)
{
    char buf[DEVDATA_VALUE_LEN];
    out[0] = '\0';
    if (!ieee || device_data_get_value(ieee, cl, at, buf, sizeof(buf)) != ESP_OK) return false;
    size_t n = 0;
    for (size_t i = 0; buf[i] && buf[i + 1] && n + 1 < len; i += 2) {
        char hx[3] = { buf[i], buf[i + 1], 0 };
        char c = (char)strtol(hx, NULL, 16);
        if (c == '\0') break;
        out[n++] = (c >= 32 && c < 127) ? c : ' ';
    }
    out[n] = '\0';
    while (n > 0 && out[n - 1] == ' ') out[--n] = '\0';
    return n > 0;
}

static int linky_mode(uint64_t zl)
{
    int64_t m;
    if (dd_num(zl, 0xFF66, 0x0300, &m) && m >= 0 && m <= 7) return (int)m;
    return cfg_get_general()->linky_mode;
}

static bool mode_is_tri(int mode)       { return mode == 2 || mode == 3 || mode == 7; }
static bool mode_is_histo(int mode)     { return mode == 0 || mode == 2; }

static const char *mode_label(int mode)
{
    switch (mode) {
    case 0: return "Historique monophas\xC3\xA9";
    case 1: return "Standard monophas\xC3\xA9";
    case 2: return "Historique triphas\xC3\xA9";
    case 3: return "Standard triphas\xC3\xA9";
    case 5: return "Standard monophas\xC3\xA9 producteur";
    case 7: return "Standard triphas\xC3\xA9 producteur";
    default: return "Inconnu";
    }
}

typedef struct {
    int     mode;
    bool    tri;
    bool    producer;
    bool    has_phase[3];
    int32_t phase[3];       // VA soutires par phase
    int32_t total;          // VA soutires (toutes phases)
    bool    has_total;
    int32_t inj;            // VA injectes (toutes phases)
    int32_t inj_phase[3];
    bool    has_inj;
    int32_t goal_total;     // puissance souscrite (VA), 0 si inconnue
    int32_t goal_phase;
} live_t;

static void read_live(uint64_t zl, live_t *L)
{
    memset(L, 0, sizeof(*L));
    L->mode = linky_mode(zl);
    L->tri = mode_is_tri(L->mode);
    L->producer = (L->mode == 5 || L->mode == 7) || cfg_get_general()->production[0];

    static const uint16_t PAPP[3] = { 1295, 2319, 2575 };
    static const uint16_t VOLT[3] = { 1285, 2309, 2565 };
    static const uint16_t CURR[3] = { 1288, 2312, 2568 };
    int nph = L->tri ? 3 : 1;
    int64_t v, u, i;
    for (int p = 0; p < nph; p++) {
        if (L->mode == 2) {
            // Historique triphase : pas de puissance par phase, IINST x 200 V
            if (dd_num(zl, 0x0B04, CURR[p], &i)) { L->phase[p] = (int32_t)(i * 200); L->has_phase[p] = true; }
        } else if (dd_num(zl, 0x0B04, PAPP[p], &v)) {
            L->phase[p] = (int32_t)v;
            L->has_phase[p] = true;
        }
    }
    if (L->tri && dd_num(zl, 0x0B04, 0x0306, &v)) {
        L->total = (int32_t)v;           // SINSTS total (standard triphase)
        L->has_total = true;
    } else {
        for (int p = 0; p < nph; p++) {
            if (L->has_phase[p]) { L->total += L->phase[p]; L->has_total = true; }
        }
    }

    // Puissance souscrite : ISOUSC x 200 VA (historique), PREF x 1000 (standard)
    int64_t isousc = 0, pref = 0;
    if (mode_is_histo(L->mode)) {
        if (dd_num(zl, 0x0B01, 13, &isousc)) L->goal_total = (int32_t)(isousc * 200);
    } else if (dd_num(zl, 0x0B01, 14, &pref)) {
        L->goal_total = (int32_t)(pref * 1000);
    }
    L->goal_phase = L->tri ? L->goal_total / 3 : L->goal_total;

    // Injection : SINSTI en standard producteur, sinon estimation quand la
    // puissance soutiree tombe a 0 (U x I, ou 200 V x I en historique), comme
    // la LiXee-Box. Seulement si une production est attendue.
    if (!L->producer) return;
    if ((L->mode == 5 || L->mode == 7) && dd_num(zl, 0xFF66, 519, &v)) {
        L->inj = (int32_t)v;
        L->inj_phase[0] = L->inj;
        L->has_inj = true;
        return;
    }
    for (int p = 0; p < nph; p++) {
        if (!L->has_phase[p] || L->phase[p] != 0) continue;
        if (!dd_num(zl, 0x0B04, CURR[p], &i)) continue;
        int32_t est = mode_is_histo(L->mode) ? (int32_t)(200 * i)
                    : (dd_num(zl, 0x0B04, VOLT[p], &u) ? (int32_t)(u * i) : 0);
        L->inj_phase[p] = est;
        L->inj += est;
        L->has_inj = true;
    }
}

// ============================================================================
// Index tarifaires : noms et couleurs
// ============================================================================

static const uint16_t INDEX_ATTRS[10] = { 256, 258, 260, 262, 264, 266, 268, 270, 272, 274 };

static const char *index_color(uint16_t attr)
{
    static const char *const C[10] = { "#2980b9", "#154360", "#7f8c8d", "#000000", "#e74c3c",
                                       "#c0392b", "#f5b041", "#d35400", "#8e44ad", "#6c3483" };
    if (attr >= 256 && attr <= 274) return C[(attr - 256) / 2];
    return "#2980b9";
}

static void index_name(uint64_t zl, int mode, uint16_t attr, char *out, size_t len)
{
    if (attr == 0) { strlcpy(out, "Total", len); return; }
    int n = (attr - 256) / 2;   // 0..9
    if (!mode_is_histo(mode)) { snprintf(out, len, "EASF%02d", n + 1); return; }
    char opt[16];
    dd_str(zl, 0xFF66, 0, opt, sizeof(opt));   // OPTARIF
    static const char *const BBR[6] = { "HCJB", "HPJB", "HCJW", "HPJW", "HCJR", "HPJR" };
    if (strncmp(opt, "BBR", 3) == 0 && n < 6) { strlcpy(out, BBR[n], len); return; }
    if (strncmp(opt, "EJP", 3) == 0) { strlcpy(out, n == 0 ? "HN" : (n == 1 ? "HPM" : "Index"), len); return; }
    if (strncmp(opt, "HC", 2) == 0) { strlcpy(out, n == 0 ? "HC" : (n == 1 ? "HP" : "Index"), len); return; }
    if (n == 0) { strlcpy(out, "BASE", len); return; }
    snprintf(out, len, "Index %d", n + 1);
}

/** Index du ZLinky presents dans l'historique ; a defaut, le total (attr 0). */
static int index_attrs(const energy_history_t *h, uint16_t *out)
{
    int n = 0;
    for (int i = 0; i < 10; i++) {
        if (energy_history_has_attr(h, INDEX_ATTRS[i])) out[n++] = INDEX_ATTRS[i];
    }
    if (n == 0 && energy_history_has_attr(h, 0)) out[n++] = 0;
    return n;
}

// ============================================================================
// Puissance active estimee : variation des index du ZLinky entre deux
// releves (getPowerW de la LiXee-Box), mise a jour chaque minute
// ============================================================================

static struct {
    uint64_t ieee;
    int64_t  sum;          // somme des index au dernier changement
    uint32_t t;            // epoch de ce releve
    int32_t  w;            // puissance estimee, -1 = inconnue
} s_pw_est = { 0, 0, 0, -1 };

static void power_estimate_tick(uint64_t zl)
{
    const energy_history_t *h = energy_history_find(zl);
    if (!h) return;
    uint16_t at[10];
    int n = index_attrs(h, at);
    int64_t sum = 0;
    uint32_t t = 0;
    for (int i = 0; i < n; i++) {
        for (int a = 0; a < h->attr_count; a++) {
            if (h->attr_ids[a] != at[i] || !(h->hours.last_mask & (1u << a))) continue;
            sum += h->hours.last.values[a];
            if (h->last_ts[a] > t) t = h->last_ts[a];
        }
    }
    if (!t) return;
    if (s_pw_est.ieee != zl) {
        s_pw_est.ieee = zl;
        s_pw_est.sum = sum;
        s_pw_est.t = t;
        s_pw_est.w = -1;
        return;
    }
    if (sum != s_pw_est.sum && t > s_pw_est.t) {
        int64_t dwh = sum - s_pw_est.sum;
        uint32_t dt = t - s_pw_est.t;
        s_pw_est.w = (dwh > 0 && dt > 0 && dt < 3 * 3600) ? (int32_t)(dwh * 3600 / dt) : -1;
        s_pw_est.sum = sum;
        s_pw_est.t = t;
    } else if (s_pw_est.w > 0 && (uint32_t)time(NULL) - s_pw_est.t > 1800) {
        s_pw_est.w = 0;      // plus d'un Wh par demi-heure : consommation quasi nulle
    }
}

// ============================================================================
// Periodes et creneaux
// ============================================================================

static energy_gran_t parse_gran(const char *period)
{
    if (period && strcmp(period, "day") == 0)   return ENERGY_GRAN_DAY;
    if (period && strcmp(period, "month") == 0) return ENERGY_GRAN_MONTH;
    if (period && strcmp(period, "year") == 0)  return ENERGY_GRAN_YEAR;
    return ENERGY_GRAN_HOUR;
}

static const char *gran_name(energy_gran_t g)
{
    static const char *const N[] = { "hour", "day", "month", "year" };
    return N[g];
}

/** Part de l'abonnement et de la CTA (mensuels) imputee a un creneau. */
static double gran_factor(energy_gran_t g, const struct tm *now)
{
    switch (g) {
    case ENERGY_GRAN_HOUR:  return 1.0 / 720.0;
    case ENERGY_GRAN_DAY:   return 1.0 / 30.0;
    case ENERGY_GRAN_MONTH: return 1.0;
    default:                (void)now; return 12.0;             // annee pleine, comme la LiXee-Box
    }
}

/** Creneau : granularite + cle (heure, jour du mois - 1, mois 0-11, annee civile). */
typedef struct {
    energy_gran_t g;
    int key;
} slot_ref_t;

static int resolve_slot(const energy_history_t *h, slot_ref_t s)
{
    if (s.g == ENERGY_GRAN_YEAR) {
        if (!h) return -1;
        int i = s.key - h->base_year;
        return (i >= 0 && i < ENERGY_YEARS) ? i : -1;
    }
    return s.key;
}

static int32_t slot_val(const energy_history_t *h, slot_ref_t s, uint16_t attr)
{
    int i = resolve_slot(h, s);
    return i < 0 ? 0 : energy_history_slot_value(h, s.g, i, attr);
}

static bool local_now(struct tm *t)
{
    time_t now = time(NULL);
    localtime_r(&now, t);
    return ntp_sync_is_synced();
}

static slot_ref_t current_slot(energy_gran_t g, const struct tm *t)
{
    slot_ref_t s = { g, 0 };
    switch (g) {
    case ENERGY_GRAN_HOUR:  s.key = t->tm_hour; break;
    case ENERGY_GRAN_DAY:   s.key = t->tm_mday - 1; break;
    case ENERGY_GRAN_MONTH: s.key = t->tm_mon; break;
    default:                s.key = t->tm_year + 1900; break;
    }
    return s;
}

static slot_ref_t previous_slot(energy_gran_t g, const struct tm *t)
{
    slot_ref_t s = { g, 0 };
    switch (g) {
    case ENERGY_GRAN_HOUR:  s.key = (t->tm_hour + 23) % 24; break;
    case ENERGY_GRAN_DAY: {
        time_t y = mktime((struct tm *)t) - 86400;
        struct tm ty;
        localtime_r(&y, &ty);
        s.key = ty.tm_mday - 1;
        break;
    }
    case ENERGY_GRAN_MONTH: s.key = (t->tm_mon + 11) % 12; break;
    default:                s.key = t->tm_year + 1900 - 1; break;
    }
    return s;
}

static int gran_slots(energy_gran_t g)
{
    switch (g) {
    case ENERGY_GRAN_HOUR:  return ENERGY_HOURS;
    case ENERGY_GRAN_DAY:   return ENERGY_DAYS;
    case ENERGY_GRAN_MONTH: return ENERGY_MONTHS;
    default:                return ENERGY_YEARS;
    }
}

// ============================================================================
// Agregats d'un creneau
// ============================================================================

#define MAX_SUB CFG_MAX_SUBMETERS

typedef struct {
    int      n_idx;
    uint16_t idx_attr[10];
    int64_t  idx_raw[10];     // Wh par index, sous-compteurs inclus
    int64_t  idx_adj[10];     // Wh par index, part des sous-compteurs retiree
    int64_t  idx_total;       // somme brute = consommation totale
    int      n_sub;
    int      sub_cfg[MAX_SUB];   // indice dans la config
    int64_t  sub_wh[MAX_SUB];
    int64_t  sub_total;
    int64_t  prod_wh;
    int64_t  gaz_wh;          // en Wh si l'unite du gaz est Wh, sinon 0
} slot_agg_t;

static const energy_history_t *prod_history(const roles_t *r)
{
    if (r->prod) return energy_history_find(r->prod);
    return energy_history_find(r->zl);
}

static int64_t sub_value(uint64_t ieee, slot_ref_t s)
{
    const energy_history_t *h = energy_history_find(ieee);
    if (!h) return 0;
    float coef = device_data_get_coefficient(ieee, 0x0702, 0);
    if (coef <= 0) coef = 1;
    int64_t v = (int64_t)(slot_val(h, s, 0) * coef);
    return v > 0 ? v : 0;
}

/** Gaz exprime en energie (Wh ou kWh) : facteur vers les Wh, 0 pour un volume. */
static double gaz_wh_factor(void)
{
    const char *u = cfg_get_general()->unit_gaz;
    if (strcasecmp(u, "Wh") == 0) return 1.0;
    if (strcasecmp(u, "kWh") == 0) return 1000.0;
    return 0;
}

static void aggregate(const roles_t *r, slot_ref_t s, slot_agg_t *a)
{
    memset(a, 0, sizeof(*a));
    const energy_history_t *zh = energy_history_find(r->zl);
    a->n_idx = index_attrs(zh, a->idx_attr);
    for (int i = 0; i < a->n_idx; i++) {
        int32_t v = slot_val(zh, s, a->idx_attr[i]);
        a->idx_raw[i] = v > 0 ? v : 0;
        a->idx_total += a->idx_raw[i];
    }

    const cfg_general_t *g = cfg_get_general();
    for (int i = 0; i < g->sub_meter_count && i < MAX_SUB; i++) {
        const cfg_submeter_t *sm = &g->sub_meters[i];
        if (!sm->enabled || !sm->IEEE[0]) continue;
        uint64_t ieee = parse_ieee(sm->IEEE);
        if (!ieee || ieee == r->zl) continue;
        a->sub_cfg[a->n_sub] = i;
        a->sub_wh[a->n_sub] = sub_value(ieee, s);
        a->sub_total += a->sub_wh[a->n_sub];
        a->n_sub++;
    }

    // Part des sous-compteurs retiree de chaque index, au prorata
    int64_t share = a->sub_total < a->idx_total ? a->sub_total : a->idx_total;
    for (int i = 0; i < a->n_idx; i++) {
        int64_t part = a->idx_total > 0 ? share * a->idx_raw[i] / a->idx_total : 0;
        a->idx_adj[i] = a->idx_raw[i] - part;
    }

    const energy_history_t *ph = prod_history(r);
    int32_t pv = slot_val(ph, s, 1);
    a->prod_wh = pv > 0 ? pv : 0;

    double gf = gaz_wh_factor();
    if (r->gaz && gf > 0) {
        const energy_history_t *gh = energy_history_find(r->gaz);
        int32_t gv = slot_val(gh, s, 0);
        double coeff = g->coeff_gaz > 0 ? g->coeff_gaz : 1;
        a->gaz_wh = gv > 0 ? (int64_t)(gv * coeff * gf) : 0;
    }
}

// Cumul de tous les creneaux d'une periode (24 dernieres heures, 30 jours,
// 12 mois, annees), comme la repartition de la LiXee-Box (donut et tableau) :
// le creneau courant seul est vide en debut d'heure. La part des
// sous-compteurs est retiree creneau par creneau (aggregate), et leur cout est
// celui du prix moyen du kWh de chaque creneau cote Linky.
typedef struct {
    slot_agg_t tot;              // cumul (idx_attr / sub_cfg du premier creneau)
    double idx_eur[10];
    double sub_eur[MAX_SUB];
} period_agg_t;

static void period_aggregate(const roles_t *r, energy_gran_t g, const struct tm *now, period_agg_t *p)
{
    memset(p, 0, sizeof(*p));
    slot_agg_t *tot = &p->tot;
    bool first = true;
    for (int i = 0; i < gran_slots(g); i++) {
        slot_ref_t s = { g, i };
        if (g == ENERGY_GRAN_YEAR) s.key = now->tm_year + 1900 - (ENERGY_YEARS - 1) + i;
        slot_agg_t a;
        aggregate(r, s, &a);
        if (first) {
            tot->n_idx = a.n_idx;
            memcpy(tot->idx_attr, a.idx_attr, sizeof(tot->idx_attr));
            tot->n_sub = a.n_sub;
            memcpy(tot->sub_cfg, a.sub_cfg, sizeof(tot->sub_cfg));
            first = false;
        }
        double slot_eur = 0;
        for (int k = 0; k < a.n_idx && k < tot->n_idx; k++) {
            double pr = price_for_attr(a.idx_attr[k]);
            tot->idx_raw[k] += a.idx_raw[k];
            tot->idx_adj[k] += a.idx_adj[k];
            p->idx_eur[k] += a.idx_adj[k] / 1000.0 * pr;
            slot_eur += a.idx_raw[k] / 1000.0 * pr;
        }
        tot->idx_total += a.idx_total;
        double eur_per_wh = a.idx_total > 0 ? slot_eur / (double)a.idx_total
                                            : price_for_attr(256) / 1000.0;
        for (int k = 0; k < a.n_sub && k < tot->n_sub; k++) {
            tot->sub_wh[k] += a.sub_wh[k];
            tot->sub_total += a.sub_wh[k];
            p->sub_eur[k] += a.sub_wh[k] * eur_per_wh;
        }
        tot->gaz_wh += a.gaz_wh;
        tot->prod_wh += a.prod_wh;
    }
}

typedef struct {
    double energy, taxes, abo, total;
} cost_t;

static cost_t cost_of(const slot_agg_t *a, double f)
{
    const cfg_general_t *g = cfg_get_general();
    double cspe = parse_num(g->tarif_cspe), cta = parse_num(g->tarif_cta), abo = parse_num(g->tarif_abo);
    cost_t c = { 0, 0, 0, 0 };
    for (int i = 0; i < a->n_idx; i++) {
        double kwh = a->idx_raw[i] / 1000.0;
        c.energy += kwh * price_for_attr(a->idx_attr[i]);
        c.taxes += kwh * cspe;
    }
    c.taxes += cta * f;
    c.abo = abo * f;
    c.total = c.energy + c.taxes + c.abo;
    return c;
}

/** Projection fin de mois : (energie + accise) / jours x 30, plus abonnement et CTA une fois. */
static double month_projection(const cost_t *c, int mday)
{
    double cta = parse_num(cfg_get_general()->tarif_cta);
    double variable = c->energy + (c->taxes - cta);
    return (mday > 0 ? variable / mday * 30.0 : variable) + c->abo + cta;
}

static double prod_revenue(int64_t wh)
{
    return wh / 1000.0 * parse_num(cfg_get_general()->tarif_idx_prod);
}

static void add_cost(cJSON *o, const cost_t *c)
{
    cJSON *j = cJSON_AddObjectToObject(o, "cost");
    cJSON_AddNumberToObject(j, "energy", round2(c->energy));
    cJSON_AddNumberToObject(j, "taxes", round2(c->taxes));
    cJSON_AddNumberToObject(j, "abo", round2(c->abo));
    cJSON_AddNumberToObject(j, "total", round2(c->total));
}

static cJSON *slot_json(const roles_t *r, slot_ref_t s, double f, const char *label, slot_agg_t *out)
{
    slot_agg_t a;
    aggregate(r, s, &a);
    cost_t c = cost_of(&a, f);
    cJSON *o = cJSON_CreateObject();
    if (label) cJSON_AddStringToObject(o, "label", label);
    cJSON_AddNumberToObject(o, "wh", (double)a.idx_total);
    add_cost(o, &c);
    cJSON_AddNumberToObject(o, "prodWh", (double)a.prod_wh);
    cJSON_AddNumberToObject(o, "prodEur", round2(prod_revenue(a.prod_wh)));
    if (out) *out = a;
    return o;
}

// ============================================================================
// Etat du ZLinky
// ============================================================================

/**
 * Joignable si une puissance a ete recue depuis moins de 10 min et si la radio
 * ne signale pas l'appareil injoignable (statut D4 de la LiXee-Box : route
 * perdue apres un envoi). Les emetteurs LoRa n'ont que le premier critere.
 */
static bool zlinky_connected(uint64_t zl)
{
    const zigate_device_t *zd = zigate_devices_find_by_ieee(zl);
    if (zd && zd->radio_status == 0xD4) return false;
    const device_data_t *dd = device_data_find(zl);
    if (!dd) return false;
    uint32_t now = (uint32_t)(esp_log_timestamp() / 1000);
    for (int c = 0; c < dd->cluster_count; c++) {
        if (dd->clusters[c].cluster_id != 0x0B04) continue;
        for (int a = 0; a < dd->clusters[c].attr_count; a++) {
            const devdata_attr_t *at = &dd->clusters[c].attrs[a];
            if ((at->attr_id == 1295 || at->attr_id == 0x0306) && at->last_update &&
                now - at->last_update < 600) {
                return true;
            }
        }
    }
    return false;
}

static const char *overload_alert(uint64_t zl, int mode)
{
    int64_t v;
    if (mode_is_histo(mode)) {
        if (dd_num(zl, 0xFF66, 5, &v) && v > 0) return "D\xC3\xA9passement de puissance souscrite";   // ADPS
    } else if (dd_stge(zl, &v) && (v & (1 << 7))) {
        return "D\xC3\xA9passement de puissance souscrite";   // STGE bit 7
    }
    return NULL;
}

static const char *label_class(int v)
{
    if (v < 50) return "A";
    if (v <= 90) return "B";
    if (v <= 150) return "C";
    if (v <= 230) return "D";
    if (v <= 330) return "E";
    if (v <= 450) return "F";
    return "G";
}

// ============================================================================
// /api/energy/summary
// ============================================================================

char *energy_summary_json(const char *period)
{
    roles_t r = get_roles();
    cJSON *root = cJSON_CreateObject();
    if (!r.zl) {
        cJSON_AddBoolToObject(root, "configured", false);
        return json_out(root);
    }
    energy_gran_t g = parse_gran(period);
    struct tm now;
    bool synced = local_now(&now);
    live_t L;
    read_live(r.zl, &L);

    cJSON_AddBoolToObject(root, "configured", true);
    cJSON_AddStringToObject(root, "period", gran_name(g));
    cJSON_AddNumberToObject(root, "linkyMode", L.mode);
    cJSON_AddStringToObject(root, "modeLabel", mode_label(L.mode));
    cJSON_AddBoolToObject(root, "tri", L.tri);
    cJSON_AddBoolToObject(root, "producer", L.producer);
    cJSON_AddBoolToObject(root, "connected", zlinky_connected(r.zl));
    const char *alert = overload_alert(r.zl, L.mode);
    if (alert) cJSON_AddStringToObject(root, "alert", alert);
    else cJSON_AddNullToObject(root, "alert");
    cJSON_AddBoolToObject(root, "timeSynced", synced);

    double f = gran_factor(g, &now);
    slot_agg_t cur, prev;
    cJSON *cur_j = slot_json(&r, current_slot(g, &now), f, NULL, &cur);
    if (g == ENERGY_GRAN_MONTH && now.tm_mday > 0) {
        cost_t cc0 = cost_of(&cur, f);
        cJSON_AddNumberToObject(cur_j, "projectionEur", round2(month_projection(&cc0, now.tm_mday)));
    }
    cJSON_AddItemToObject(root, "current", cur_j);
    cJSON_AddItemToObject(root, "previous", slot_json(&r, previous_slot(g, &now), f, NULL, &prev));
    if (prev.idx_total > 0) {
        cJSON_AddNumberToObject(root, "variationPct",
                                round(((double)cur.idx_total - prev.idx_total) * 1000.0 / prev.idx_total) / 10.0);
    } else {
        cJSON_AddNullToObject(root, "variationPct");
    }

    // Min/max et etiquette sur tous les creneaux de la periode
    int64_t min_wh = 0, max_wh = 0, all_wh = 0, all_prod = 0, all_gaz = 0;
    for (int i = 0; i < gran_slots(g); i++) {
        slot_ref_t s = { g, i };
        if (g == ENERGY_GRAN_YEAR) s.key = now.tm_year + 1900 - (ENERGY_YEARS - 1) + i;
        slot_agg_t a;
        aggregate(&r, s, &a);
        if (a.idx_total > 0 && (min_wh == 0 || a.idx_total < min_wh)) min_wh = a.idx_total;
        if (a.idx_total > max_wh) max_wh = a.idx_total;
        if (g != ENERGY_GRAN_YEAR || s.key == now.tm_year + 1900) {
            all_wh += a.idx_total;
            all_prod += a.prod_wh;
            all_gaz += a.gaz_wh;
        }
    }
    cJSON_AddNumberToObject(root, "minWh", (double)min_wh);
    cJSON_AddNumberToObject(root, "maxWh", (double)max_wh);

    // Etiquette energetique kWh/m2.an : 24 h x 365, 31 j x 12, 12 mois,
    // annee en cours ramenee a 12 mois
    int surface = cfg_get_general()->house_surface;
    double annual = 0;
    double e_kwh = (all_wh - all_prod + all_gaz) / 1000.0;
    switch (g) {
    case ENERGY_GRAN_HOUR:  annual = e_kwh * 365; break;
    case ENERGY_GRAN_DAY:   annual = e_kwh * 12; break;
    case ENERGY_GRAN_MONTH: annual = e_kwh; break;
    default:                annual = e_kwh * 12.0 / (now.tm_mon + 1); break;
    }
    if (surface > 0 && annual > 0) {
        cJSON *lab = cJSON_AddObjectToObject(root, "label");
        int v = (int)(annual / surface);
        cJSON_AddNumberToObject(lab, "value", v);
        cJSON_AddStringToObject(lab, "class", label_class(v));
    } else {
        cJSON_AddNullToObject(root, "label");
    }

    // Vue heure : J-1, mois et annee en cours
    if (g == ENERGY_GRAN_HOUR) {
        cJSON *rows = cJSON_AddArrayToObject(root, "rows");
        cJSON_AddItemToArray(rows, slot_json(&r, previous_slot(ENERGY_GRAN_DAY, &now), 1.0 / 30.0, "J-1", NULL));
        slot_agg_t m;
        cJSON *mo = slot_json(&r, current_slot(ENERGY_GRAN_MONTH, &now), 1.0, "Mois", &m);
        cost_t mc = cost_of(&m, 1.0);
        if (now.tm_mday > 0) cJSON_AddNumberToObject(mo, "projectionEur", round2(month_projection(&mc, now.tm_mday)));
        cJSON_AddItemToArray(rows, mo);
        cJSON_AddItemToArray(rows, slot_json(&r, current_slot(ENERGY_GRAN_YEAR, &now),
                                             12.0, "Ann\xC3\xA9" "e", NULL));
    }

    // Budget mensuel (alerte "depassement de budget")
    double month_eur = budget_month_eur();
    if (month_eur > 0) {
        cJSON *b = cJSON_AddObjectToObject(root, "budget");
        double period_eur = month_eur * f;
        cJSON_AddNumberToObject(b, "monthEur", month_eur);
        cJSON_AddNumberToObject(b, "periodEur", round2(period_eur));
        cost_t cc = cost_of(&cur, f);
        cJSON_AddNumberToObject(b, "pct", period_eur > 0 ? round(cc.total * 1000.0 / period_eur) / 10.0 : 0);
    } else {
        cJSON_AddNullToObject(root, "budget");
    }

    // Tableau de repartition (getDatasPower de la LiXee-Box) : toute la
    // periode, comme le donut. Index tarifaires du Linky non nuls (part des
    // sous-compteurs retiree), production (compteur distinct du ZLinky), gaz
    // (en Wh), puis sous-compteurs non nuls. Total = consommation Linky +
    // production + gaz : les sous-compteurs sont deja dans les index.
    period_agg_t pa;
    period_aggregate(&r, g, &now, &pa);
    const slot_agg_t &tot = pa.tot;
    cJSON *bd = cJSON_AddArrayToObject(root, "breakdown");
    double total_eur = 0;
    const cfg_general_t *gc = cfg_get_general();
    auto add_row = [&](const char *key, const char *name, double wh, double eur,
                       const char *color, const char *type) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "key", key);
        cJSON_AddStringToObject(o, "name", name);
        cJSON_AddNumberToObject(o, "wh", wh);
        cJSON_AddNumberToObject(o, "eur", round2(eur));
        cJSON_AddStringToObject(o, "color", color);
        cJSON_AddStringToObject(o, "type", type);
        cJSON_AddItemToArray(bd, o);
    };
    for (int i = 0; i < tot.n_idx; i++) {
        char key[8], name[24];
        snprintf(key, sizeof(key), "%u", tot.idx_attr[i]);
        index_name(r.zl, L.mode, tot.idx_attr[i], name, sizeof(name));
        if (tot.idx_adj[i] <= 0) continue;
        total_eur += pa.idx_eur[i];
        add_row(key, name, (double)tot.idx_adj[i], pa.idx_eur[i], index_color(tot.idx_attr[i]), "index");
    }
    if (r.prod && r.prod != r.zl) {
        double eur = prod_revenue(tot.prod_wh);
        total_eur += eur;
        add_row("prod", "Production", (double)tot.prod_wh, eur, "#27ae60", "prod");
    }
    if (r.gaz && gaz_wh_factor() > 0) {
        double eur = tot.gaz_wh / 1000.0 * parse_num(gc->tarif_gaz);
        total_eur += eur;
        add_row("gaz", "Gaz", (double)tot.gaz_wh, eur, "#e67e22", "gaz");
    }
    for (int i = 0; i < tot.n_sub; i++) {
        const cfg_submeter_t *sm = &gc->sub_meters[tot.sub_cfg[i]];
        char key[12];
        snprintf(key, sizeof(key), "sub_%d", tot.sub_cfg[i]);
        if (tot.sub_wh[i] <= 0) continue;
        total_eur += pa.sub_eur[i];
        add_row(key, sm->alias[0] ? sm->alias : sm->IEEE, (double)tot.sub_wh[i], pa.sub_eur[i],
                sm->color[0] ? sm->color : "#3498db", "submeter");
    }
    cJSON_AddNumberToObject(root, "totalWh", (double)(tot.idx_total + tot.prod_wh + tot.gaz_wh));
    cJSON_AddNumberToObject(root, "totalEur", round2(total_eur));

    // Derniere mesure recue du ZLinky (hist.lastUpdate de la LiXee-Box)
    char ts[24] = "";
    const energy_history_t *zh = energy_history_find(r.zl);
    uint32_t last = 0;
    for (int a = 0; zh && a < zh->attr_count; a++) if (zh->last_ts[a] > last) last = zh->last_ts[a];
    if (last) {
        time_t lt = (time_t)last;
        struct tm ltm;
        localtime_r(&lt, &ltm);
        strftime(ts, sizeof(ts), "%d/%m/%Y %H:%M", &ltm);
    } else if (synced) {
        strftime(ts, sizeof(ts), "%d/%m/%Y %H:%M", &now);
    }
    cJSON_AddStringToObject(root, "lastUpdate", ts);
    return json_out(root);
}

// ============================================================================
// /api/energy/gauge
// ============================================================================

char *energy_gauge_json(const char *period)
{
    roles_t r = get_roles();
    cJSON *root = cJSON_CreateObject();
    if (!r.zl) {
        cJSON_AddBoolToObject(root, "configured", false);
        return json_out(root);
    }
    energy_gran_t g = parse_gran(period);
    live_t L;
    read_live(r.zl, &L);
    cJSON_AddStringToObject(root, "period", gran_name(g));

    if (g == ENERGY_GRAN_HOUR) {
        int32_t max_phase = L.goal_phase > 0 ? L.goal_phase : 9000;
        int32_t max_total = L.goal_total > 0 ? L.goal_total : 9000;
        cJSON_AddStringToObject(root, "unit", "VA");
        cJSON_AddBoolToObject(root, "tri", L.tri);
        cJSON_AddNumberToObject(root, "goal", L.goal_total);
        cJSON *ph = cJSON_AddArrayToObject(root, "phases");
        int nph = L.tri ? 3 : 1;
        for (int p = 0; p < nph; p++) {
            cJSON *o = cJSON_CreateObject();
            char lbl[16];
            if (L.tri) snprintf(lbl, sizeof(lbl), "Soutir\xC3\xA9" "e P.%d", p + 1);
            else strlcpy(lbl, "Soutir\xC3\xA9" "e", sizeof(lbl));
            cJSON_AddStringToObject(o, "label", lbl);
            cJSON_AddNumberToObject(o, "value", L.has_phase[p] ? L.phase[p] : 0);
            cJSON_AddNumberToObject(o, "max", max_phase);
            cJSON_AddItemToArray(ph, o);
        }
        if (L.producer) {
            // Compteur de production distinct du ZLinky : son SINSTI (FF66/519)
            // et sa PREF, comme la LiXee-Box
            int32_t inj = L.inj, inj_max = max_total;
            int64_t v;
            if (r.prod && r.prod != r.zl) {
                inj = dd_num(r.prod, 0xFF66, 519, &v) ? (int32_t)v : 0;
                if (dd_num(r.prod, 0x0B01, 14, &v) && v > 0) inj_max = (int32_t)(v * 1000);
            }
            cJSON *o = cJSON_AddObjectToObject(root, "prod");
            cJSON_AddStringToObject(o, "label", "Inject\xC3\xA9" "e");
            cJSON_AddNumberToObject(o, "value", inj);
            cJSON_AddNumberToObject(o, "max", inj_max);
        } else {
            cJSON_AddNullToObject(root, "prod");
        }
        if (s_pw_est.ieee == r.zl && s_pw_est.w >= 0) cJSON_AddNumberToObject(root, "powerW", s_pw_est.w);
        else cJSON_AddNullToObject(root, "powerW");
        return json_out(root);
    }

    // Autres periodes : energie du creneau courant, maximum de la periode
    struct tm now;
    local_now(&now);
    slot_agg_t cur;
    aggregate(&r, current_slot(g, &now), &cur);
    int64_t max_wh = 0, max_prod = 0;
    for (int i = 0; i < gran_slots(g); i++) {
        slot_ref_t s = { g, i };
        if (g == ENERGY_GRAN_YEAR) s.key = now.tm_year + 1900 - (ENERGY_YEARS - 1) + i;
        slot_agg_t a;
        aggregate(&r, s, &a);
        if (a.idx_total > max_wh) max_wh = a.idx_total;
        if (a.prod_wh > max_prod) max_prod = a.prod_wh;
    }
    cJSON_AddStringToObject(root, "unit", "kWh");
    cJSON *c = cJSON_AddObjectToObject(root, "conso");
    cJSON_AddNumberToObject(c, "value", round2(cur.idx_total / 1000.0));
    cJSON_AddNumberToObject(c, "max", round2((max_wh > 0 ? max_wh : 1000) / 1000.0));
    if (L.producer || cur.prod_wh > 0) {
        cJSON *p = cJSON_AddObjectToObject(root, "prod");
        cJSON_AddNumberToObject(p, "value", round2(cur.prod_wh / 1000.0));
        cJSON_AddNumberToObject(p, "max", round2((max_prod > 0 ? max_prod : 1000) / 1000.0));
    } else {
        cJSON_AddNullToObject(root, "prod");
    }
    return json_out(root);
}

// ============================================================================
// /api/energy/distribution (donut Morris)
// ============================================================================

char *energy_distribution_json(const char *period, bool euro)
{
    roles_t r = get_roles();
    cJSON *arr = cJSON_CreateArray();
    if (!r.zl) return json_out(arr);
    energy_gran_t g = parse_gran(period);
    struct tm now;
    local_now(&now);
    int mode = linky_mode(r.zl);
    const cfg_general_t *gc = cfg_get_general();

    period_agg_t pa;
    period_aggregate(&r, g, &now, &pa);
    const slot_agg_t &tot = pa.tot;
    const double *idx_eur = pa.idx_eur, *sub_eur = pa.sub_eur;

    auto add = [&](const char *label, double wh, double eur, const char *color) {
        double v = euro ? round2(eur) : wh;
        if (v <= 0) return;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "label", label);
        cJSON_AddNumberToObject(o, "value", v);
        cJSON_AddStringToObject(o, "color", color);
        cJSON_AddStringToObject(o, "unit", euro ? "\xE2\x82\xAC" : "Wh");
        cJSON_AddItemToArray(arr, o);
    };
    for (int i = 0; i < tot.n_idx; i++) {
        char name[24];
        index_name(r.zl, mode, tot.idx_attr[i], name, sizeof(name));
        add(name, (double)tot.idx_adj[i], idx_eur[i], index_color(tot.idx_attr[i]));
    }
    for (int i = 0; i < tot.n_sub; i++) {
        const cfg_submeter_t *sm = &gc->sub_meters[tot.sub_cfg[i]];
        add(sm->alias[0] ? sm->alias : sm->IEEE, (double)tot.sub_wh[i], sub_eur[i],
            sm->color[0] ? sm->color : "#3498db");
    }
    add("Gaz", (double)tot.gaz_wh, tot.gaz_wh / 1000.0 * parse_num(gc->tarif_gaz), "#e67e22");
    add("Injection", (double)tot.prod_wh, tot.prod_wh / 1000.0 * parse_num(gc->tarif_idx_prod), "#27ae60");
    return json_out(arr);
}

// ============================================================================
// /api/energy/chart et son export CSV
// ============================================================================

#define CHART_MAX_POINTS 31
#define CHART_MAX_SERIES (10 + MAX_SUB + 1)

typedef struct {
    char    key[12];
    char    name[40];
    char    color[10];
    const char *type;    // index, submeter, prod, meter
    double  price;
    int64_t data[CHART_MAX_POINTS];
    double  slot_price[CHART_MAX_POINTS];   // sous-compteurs : prix moyen du creneau
    bool    has_slot_price;
    bool    nonzero;
} series_t;

/** Prix moyen du kWh d'un creneau cote Linky (index ponderes), tarif de base a defaut. */
static double slot_avg_price(const slot_agg_t *a)
{
    double eur = 0;
    for (int k = 0; k < a->n_idx; k++) eur += a->idx_raw[k] / 1000.0 * price_for_attr(a->idx_attr[k]);
    return a->idx_total > 0 ? eur * 1000.0 / (double)a->idx_total : price_for_attr(256);
}

typedef struct {
    energy_gran_t g;
    int      n;                                  // nombre de points
    char     labels[CHART_MAX_POINTS][24];
    slot_ref_t slots[CHART_MAX_POINTS];
    int      ns;
    series_t s[CHART_MAX_SERIES];
    bool     meter;                              // compteur gaz/eau (serie "0")
    double   coeff;
    const char *display_unit;
    double   meter_price;
} chart_t;

static void chart_labels(chart_t *c, const struct tm *now)
{
    switch (c->g) {
    case ENERGY_GRAN_HOUR:
        c->n = 24;
        for (int i = 0; i < 24; i++) {
            int h = (now->tm_hour + 1 + i) % 24;
            snprintf(c->labels[i], sizeof(c->labels[i]), "%02dh", h);
            c->slots[i] = (slot_ref_t){ ENERGY_GRAN_HOUR, h };
        }
        break;
    case ENERGY_GRAN_DAY: {
        c->n = 30;
        time_t t0 = mktime((struct tm *)now);
        for (int i = 0; i < 30; i++) {
            time_t t = t0 - (time_t)(29 - i) * 86400;
            struct tm d;
            localtime_r(&t, &d);
            snprintf(c->labels[i], sizeof(c->labels[i]), "%02d/%02d", d.tm_mday, d.tm_mon + 1);
            c->slots[i] = (slot_ref_t){ ENERGY_GRAN_DAY, d.tm_mday - 1 };
        }
        break;
    }
    case ENERGY_GRAN_MONTH:
        c->n = 12;
        for (int i = 0; i < 12; i++) {
            int m = now->tm_mon - 11 + i, y = now->tm_year + 1900;
            while (m < 0) { m += 12; y--; }
            snprintf(c->labels[i], sizeof(c->labels[i]), "%02d/%04d", m + 1, y);
            c->slots[i] = (slot_ref_t){ ENERGY_GRAN_MONTH, m };
        }
        break;
    default:
        c->n = ENERGY_YEARS;
        for (int i = 0; i < ENERGY_YEARS; i++) {
            int y = now->tm_year + 1900 - (ENERGY_YEARS - 1) + i;
            snprintf(c->labels[i], sizeof(c->labels[i]), "%04d", y);
            c->slots[i] = (slot_ref_t){ ENERGY_GRAN_YEAR, y };
        }
        break;
    }
}

static series_t *chart_add_series(chart_t *c, const char *key, const char *name, const char *color,
                                  const char *type, double price)
{
    if (c->ns >= CHART_MAX_SERIES) return NULL;
    series_t *s = &c->s[c->ns++];
    memset(s, 0, sizeof(*s));
    strlcpy(s->key, key, sizeof(s->key));
    strlcpy(s->name, name, sizeof(s->name));
    strlcpy(s->color, color, sizeof(s->color));
    s->type = type;
    s->price = price;
    return s;
}

/** Construit les series ; false si aucun compteur n'est disponible. */
static bool chart_build(chart_t *c, energy_gran_t g, uint64_t ieee)
{
    memset(c, 0, sizeof(*c));
    c->g = g;
    c->coeff = 1;
    c->display_unit = "kWh";
    roles_t r = get_roles();
    if (!ieee) ieee = r.zl;
    if (!ieee) return false;
    struct tm now;
    local_now(&now);
    chart_labels(c, &now);
    const cfg_general_t *gc = cfg_get_general();

    if (ieee != r.zl) {
        // Compteur de gaz, d'eau ou autre : son total (attribut 0)
        c->meter = true;
        const energy_history_t *h = energy_history_find(ieee);
        const char *name = "Consommation";
        const char *color = "#e67e22";               // gaz (createEnergyGraph de la LiXee-Box)
        if (ieee == r.gaz) {
            c->coeff = gc->coeff_gaz > 0 ? gc->coeff_gaz : 1;
            c->display_unit = gc->unit_gaz;
            c->meter_price = parse_num(gc->tarif_gaz);
            name = "Gaz";
        } else if (ieee == r.water) {
            c->coeff = gc->coeff_water > 0 ? gc->coeff_water : 1;
            c->display_unit = gc->unit_water;
            c->meter_price = parse_num(gc->tarif_water);
            name = "Eau";
            color = "#2e86c1";
        }
        series_t *s = chart_add_series(c, "0", name, color, "meter", c->meter_price);
        for (int i = 0; s && i < c->n; i++) {
            int32_t v = slot_val(h, c->slots[i], 0);
            s->data[i] = v > 0 ? v : 0;
            if (s->data[i]) s->nonzero = true;
        }
        return true;
    }

    int mode = linky_mode(r.zl);
    // ~10 Ko : en PSRAM, la pile de la tache HTTP n'en a que 8
    slot_agg_t *a = (slot_agg_t *)heap_caps_malloc(sizeof(slot_agg_t) * CHART_MAX_POINTS, MALLOC_CAP_SPIRAM);
    if (!a) a = (slot_agg_t *)malloc(sizeof(slot_agg_t) * CHART_MAX_POINTS);
    if (!a) return false;
    for (int i = 0; i < c->n; i++) aggregate(&r, c->slots[i], &a[i]);
    int n_idx = a[0].n_idx;
    for (int k = 0; k < n_idx; k++) {
        char key[8], name[24];
        snprintf(key, sizeof(key), "%u", a[0].idx_attr[k]);
        index_name(r.zl, mode, a[0].idx_attr[k], name, sizeof(name));
        series_t *s = chart_add_series(c, key, name, index_color(a[0].idx_attr[k]), "index",
                                       price_for_attr(a[0].idx_attr[k]));
        for (int i = 0; s && i < c->n; i++) {
            s->data[i] = a[i].idx_adj[k];
            if (s->data[i]) s->nonzero = true;
        }
    }
    for (int k = 0; k < a[0].n_sub; k++) {
        const cfg_submeter_t *sm = &gc->sub_meters[a[0].sub_cfg[k]];
        char key[12];
        snprintf(key, sizeof(key), "sub_%d", a[0].sub_cfg[k]);
        series_t *s = chart_add_series(c, key, sm->alias[0] ? sm->alias : sm->IEEE,
                                       sm->color[0] ? sm->color : "#3498db", "submeter", price_for_attr(256));
        for (int i = 0; s && i < c->n; i++) {
            s->data[i] = a[i].sub_wh[k];
            s->slot_price[i] = slot_avg_price(&a[i]);
            if (s->data[i]) s->nonzero = true;
        }
        if (s) s->has_slot_price = true;
    }
    series_t *p = chart_add_series(c, "prod", "Production", "#27ae60", "prod", parse_num(gc->tarif_idx_prod));
    for (int i = 0; p && i < c->n; i++) {
        p->data[i] = a[i].prod_wh;
        if (p->data[i]) p->nonzero = true;
    }
    free(a);
    return true;
}

char *energy_chart_json(const char *period, uint64_t ieee)
{
    chart_t *c = (chart_t *)heap_caps_calloc(1, sizeof(chart_t), MALLOC_CAP_SPIRAM);
    if (!c) c = (chart_t *)calloc(1, sizeof(chart_t));
    if (!c) return NULL;
    energy_gran_t g = parse_gran(period);
    cJSON *root = cJSON_CreateObject();
    if (!chart_build(c, g, ieee)) {
        cJSON_AddBoolToObject(root, "configured", false);
        free(c);
        return json_out(root);
    }
    const cfg_general_t *gc = cfg_get_general();
    struct tm now;
    local_now(&now);
    double f = gran_factor(g, &now);

    cJSON_AddBoolToObject(root, "configured", true);
    cJSON_AddStringToObject(root, "period", gran_name(g));
    cJSON_AddStringToObject(root, "unit", "Wh");
    cJSON_AddStringToObject(root, "device", c->meter ? "meter" : "zlinky");
    cJSON *labels = cJSON_AddArrayToObject(root, "labels");
    for (int i = 0; i < c->n; i++) cJSON_AddItemToArray(labels, cJSON_CreateString(c->labels[i]));
    cJSON *series = cJSON_AddArrayToObject(root, "series");
    double sum_wh = 0, sum_eur = 0;
    for (int k = 0; k < c->ns; k++) {
        const series_t *s = &c->s[k];
        if (!s->nonzero) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "key", s->key);
        cJSON_AddStringToObject(o, "name", s->name);
        cJSON_AddStringToObject(o, "type", s->type);
        cJSON_AddStringToObject(o, "color", s->color);
        cJSON_AddNumberToObject(o, "price", s->price);
        if (s->has_slot_price) {
            cJSON *pr = cJSON_AddArrayToObject(o, "prices");
            for (int i = 0; i < c->n; i++) cJSON_AddItemToArray(pr, cJSON_CreateNumber(round(s->slot_price[i] * 10000.0) / 10000.0));
        }
        cJSON *d = cJSON_AddArrayToObject(o, "data");
        for (int i = 0; i < c->n; i++) {
            cJSON_AddItemToArray(d, cJSON_CreateNumber((double)s->data[i]));
            if (strcmp(s->type, "index") == 0) {
                sum_wh += s->data[i];
                sum_eur += s->data[i] / 1000.0 * s->price;
            }
        }
        cJSON_AddItemToArray(series, o);
    }
    cJSON_AddNumberToObject(root, "coeff", c->coeff);
    cJSON_AddStringToObject(root, "displayUnit", c->display_unit);
    cJSON *costs = cJSON_AddObjectToObject(root, "costs");
    cJSON_AddNumberToObject(costs, "abo", parse_num(gc->tarif_abo));
    cJSON_AddNumberToObject(costs, "cspe", parse_num(gc->tarif_cspe));
    cJSON_AddNumberToObject(costs, "cta", parse_num(gc->tarif_cta));
    cJSON_AddNumberToObject(costs, "prodPrice", parse_num(gc->tarif_idx_prod));
    cJSON_AddNumberToObject(costs, "periodFactor", f);

    // Budget converti en Wh au prix moyen observe sur la fenetre
    double month_eur = budget_month_eur();
    double budget_wh = 0;
    if (!c->meter && month_eur > 0 && sum_wh > 0 && sum_eur > 0) {
        budget_wh = month_eur * f * sum_wh / sum_eur;
    }
    cJSON_AddNumberToObject(root, "budgetWh", round(budget_wh));
    free(c);
    return json_out(root);
}

char *energy_chart_legacy_json(const char *period, uint64_t ieee)
{
    chart_t *c = (chart_t *)heap_caps_calloc(1, sizeof(chart_t), MALLOC_CAP_SPIRAM);
    if (!c) c = (chart_t *)calloc(1, sizeof(chart_t));
    if (!c) return NULL;
    cJSON *rows = cJSON_CreateArray();
    if (!chart_build(c, parse_gran(period), ieee)) {
        free(c);
        return json_out(rows);
    }
    for (int i = 0; i < c->n; i++) {
        cJSON *o = cJSON_CreateObject();
        char y[24];
        strlcpy(y, c->labels[i], sizeof(y));
        if (c->g == ENERGY_GRAN_HOUR) {            // "14h" -> "14H"
            size_t l = strlen(y);
            if (l && y[l - 1] == 'h') y[l - 1] = 'H';
        }
        cJSON_AddStringToObject(o, "y", y);
        double psub = -1;
        for (int k = 0; k < c->ns; k++) {
            const series_t *s = &c->s[k];
            if (!s->data[i]) continue;
            if (strcmp(s->type, "submeter") == 0 && s->has_slot_price && psub < 0) psub = s->slot_price[i];
        }
        if (psub >= 0) cJSON_AddNumberToObject(o, "_psub", round(psub * 1e6) / 1e6);
        for (int k = 0; k < c->ns; k++) {
            const series_t *s = &c->s[k];
            if (!s->data[i]) continue;
            if (strcmp(s->type, "prod") == 0) {
                cJSON_AddNumberToObject(o, "1", -(double)llabs(s->data[i]));
            } else if (strcmp(s->type, "meter") == 0) {
                cJSON_AddNumberToObject(o, "0", (double)s->data[i]);
            } else {
                cJSON_AddNumberToObject(o, s->key, (double)s->data[i]);   // "256".."274", "sub_<n>"
            }
        }
        cJSON_AddItemToArray(rows, o);
    }
    free(c);
    return json_out(rows);
}

char *energy_chart_csv(const char *period, uint64_t ieee)
{
    chart_t *c = (chart_t *)heap_caps_calloc(1, sizeof(chart_t), MALLOC_CAP_SPIRAM);
    if (!c) c = (chart_t *)calloc(1, sizeof(chart_t));
    if (!c) return NULL;
    energy_gran_t g = parse_gran(period);
    sbuf_t b;
    sb_init(&b, 4096);
    sb_printf(&b, "\xEF\xBB\xBF");   // BOM UTF-8 (Excel)
    if (!chart_build(c, g, ieee)) {
        sb_printf(&b, "Aucun compteur configure\r\n");
        free(c);
        return sb_finish(&b);
    }
    const cfg_general_t *gc = cfg_get_general();
    struct tm now;
    local_now(&now);
    double f = gran_factor(g, &now);
    double cspe = parse_num(gc->tarif_cspe), cta = parse_num(gc->tarif_cta), abo = parse_num(gc->tarif_abo);
    bool has_prod = false;

    if (c->meter) {
        // Compteur de gaz / d'eau : quantite x coefficient, dans son unite
        const series_t *s = c->ns ? &c->s[0] : NULL;
        sb_printf(&b, "Periode;%s (%s);Cout (EUR)\r\n", s ? s->name : "Consommation", c->display_unit);
        for (int i = 0; s && i < c->n; i++) {
            double q = s->data[i] * c->coeff;
            sb_printf(&b, "%s;", c->labels[i]);
            sb_dec(&b, q, 3);
            sb_printf(&b, ";");
            sb_dec(&b, q * c->meter_price, 4);
            sb_printf(&b, "\r\n");
        }
        free(c);
        return sb_finish(&b);
    }
    sb_printf(&b, "Periode");
    for (int k = 0; k < c->ns; k++) {
        if (!c->s[k].nonzero) continue;
        if (strcmp(c->s[k].type, "prod") == 0) has_prod = true;
        sb_printf(&b, ";%s (Wh)", c->s[k].name);
    }
    sb_printf(&b, ";Consommation totale (Wh);Conso - Energie (EUR);Conso - Abonnement (EUR);"
                  "Conso - Taxes (EUR);Conso - Cout total (EUR)");
    if (has_prod) sb_printf(&b, ";Production totale (Wh);Production - Revenu (EUR);Net (Wh);Facture nette (EUR)");
    sb_printf(&b, "\r\n");

    for (int i = 0; i < c->n; i++) {
        double conso = 0, energy = 0, taxes = 0, prod = 0;
        sb_printf(&b, "%s", c->labels[i]);
        for (int k = 0; k < c->ns; k++) {
            const series_t *s = &c->s[k];
            if (!s->nonzero) continue;
            sb_printf(&b, ";%lld", (long long)s->data[i]);
            double kwh = s->data[i] / 1000.0;
            if (strcmp(s->type, "prod") == 0) {
                prod += s->data[i];
            } else {
                conso += s->data[i];
                energy += kwh * (s->has_slot_price ? s->slot_price[i] : s->price);
                // Accise sur toute la consommation electrique, sous-compteurs compris
                if (!c->meter) taxes += kwh * cspe;
            }
        }
        double a = 0;
        if (conso > 0 && !c->meter) {
            a = abo * f;
            taxes += cta * f;
        }
        double total = energy + a + taxes;
        sb_printf(&b, ";%lld;", (long long)conso);
        sb_dec(&b, energy, 4); sb_printf(&b, ";");
        sb_dec(&b, a, 4);      sb_printf(&b, ";");
        sb_dec(&b, taxes, 4);  sb_printf(&b, ";");
        sb_dec(&b, total, 4);
        if (has_prod) {
            double rev = prod / 1000.0 * parse_num(gc->tarif_idx_prod);
            sb_printf(&b, ";%lld;", (long long)prod);
            sb_dec(&b, rev, 4);
            sb_printf(&b, ";%lld;", (long long)(conso - prod));
            sb_dec(&b, total - rev, 4);
        }
        sb_printf(&b, "\r\n");
    }
    free(c);
    return sb_finish(&b);
}

// ============================================================================
// Courbe de puissance des dernieres 24 h (minute par minute)
// ============================================================================

#define PW_SLOTS      1440
#define PW_ABSENT     INT32_MIN
#define PW_FILE       ENERGY_HST_DIR "/pw24.bin"
#define PW_MAGIC      0x50573234u   // "PW24"
#define PW_SAVE_EVERY 15            // minutes

typedef struct {
    uint32_t minute;     // minutes depuis l'epoch de la mesure (0 = vide)
    int32_t  v[6];       // VA : soutire ph1..3, injecte ph1..3
} pw_slot_t;

static pw_slot_t *s_pw = NULL;
static uint64_t   s_pw_ieee = 0;
static int        s_pw_since_save = 0;

static void pw_save(void)
{
    if (!s_pw || !s_pw_ieee) return;
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s.tmp", PW_FILE);
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    uint32_t magic = PW_MAGIC;
    bool ok = fwrite(&magic, sizeof(magic), 1, f) == 1 &&
              fwrite(&s_pw_ieee, sizeof(s_pw_ieee), 1, f) == 1 &&
              fwrite(s_pw, sizeof(pw_slot_t), PW_SLOTS, f) == PW_SLOTS;
    ok = (fflush(f) == 0) && ok;
    ok = (fsync(fileno(f)) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
    if (!ok || rename(tmp, PW_FILE) != 0) {
        unlink(tmp);
        ESP_LOGW(TAG, "Courbe de puissance non sauvegardee");
    }
}

static void pw_load(void)
{
    FILE *f = fopen(PW_FILE, "rb");
    if (!f) return;
    uint32_t magic = 0;
    uint64_t ieee = 0;
    if (fread(&magic, sizeof(magic), 1, f) == 1 && magic == PW_MAGIC &&
        fread(&ieee, sizeof(ieee), 1, f) == 1 &&
        fread(s_pw, sizeof(pw_slot_t), PW_SLOTS, f) == PW_SLOTS) {
        s_pw_ieee = ieee;
        ESP_LOGI(TAG, "Courbe de puissance 24 h rechargee (ZLinky %016llX)", (unsigned long long)ieee);
    } else {
        memset(s_pw, 0, sizeof(pw_slot_t) * PW_SLOTS);
    }
    fclose(f);
}

esp_err_t energy_service_init(void)
{
    if (s_pw) return ESP_OK;
    s_pw = (pw_slot_t *)heap_caps_calloc(PW_SLOTS, sizeof(pw_slot_t), MALLOC_CAP_SPIRAM);
    if (!s_pw) {
        ESP_LOGE(TAG, "Courbe de puissance : PSRAM insuffisante");
        return ESP_ERR_NO_MEM;
    }
    pw_load();
    return ESP_OK;
}

/**
 * Courbe de la LiXee-Box Arduino (hst/pwr_<ZLinky>.json, sans pw24.bin) :
 * {"datas":[{"y":"HH:MM","1295":VA,"2319":..,"2575":..,"1":inj,"2":..,"3":..}]}.
 * Points dates d'apres la date du fichier (la Box n'en garde que l'heure) :
 * chaque "HH:MM" est sa derniere occurrence avant cette date. Une courbe de
 * plus de 24 h reste masquee par pw_walk. Heure locale requise (apres NTP).
 */
static void pw_import_box(uint64_t zl)
{
    char path[64];
    snprintf(path, sizeof(path), ENERGY_HST_DIR "/pwr_%016llX.json", (unsigned long long)zl);
    struct stat st;
    if (stat(path, &st) != 0 || st.st_mtime < 1600000000 || st.st_size <= 0 || st.st_size > 200 * 1024) return;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char *txt = (char *)heap_caps_malloc((size_t)st.st_size + 1, MALLOC_CAP_SPIRAM);
    size_t got = txt ? fread(txt, 1, (size_t)st.st_size, f) : 0;
    fclose(f);
    if (!txt) return;
    txt[got] = '\0';
    cJSON *root = cJSON_Parse(txt);
    heap_caps_free(txt);
    const cJSON *datas = root ? cJSON_GetObjectItem(root, "datas") : NULL;
    if (!cJSON_IsArray(datas)) {      // meme lignes "y" + cle par attribut, format Box ou pont
        cJSON_Delete(root);
        return;
    }
    struct tm at;
    time_t mt = st.st_mtime;
    localtime_r(&mt, &at);
    const int anchor = at.tm_hour * 60 + at.tm_min;
    const uint32_t anchor_min = (uint32_t)(mt / 60);
    static const char *const KEYS[6] = { "1295", "2319", "2575", "1", "2", "3" };
    int n = 0;
    const cJSON *row;
    cJSON_ArrayForEach(row, datas) {
        const cJSON *y = cJSON_GetObjectItem(row, "y");
        int hh, mm;
        if (!cJSON_IsString(y) || sscanf(y->valuestring, "%d:%d", &hh, &mm) != 2 ||
            hh < 0 || hh > 23 || mm < 0 || mm > 59) continue;
        int idx = hh * 60 + mm;
        pw_slot_t *s = &s_pw[idx];
        s->minute = anchor_min - (uint32_t)((anchor - idx + PW_SLOTS) % PW_SLOTS);
        bool any = false;
        for (int p = 0; p < 6; p++) {
            const cJSON *v = cJSON_GetObjectItem(row, KEYS[p]);
            s->v[p] = cJSON_IsNumber(v) ? (int32_t)v->valuedouble : PW_ABSENT;
            any |= cJSON_IsNumber(v);
        }
        if (!any) s->minute = 0;
        else n++;
    }
    cJSON_Delete(root);
    if (n) {
        ESP_LOGI(TAG, "Courbe de puissance de la LiXee-Box reprise : %d minute(s), fichier du %02d/%02d %02d:%02d",
                 n, at.tm_mday, at.tm_mon + 1, at.tm_hour, at.tm_min);
        pw_save();
    }
}

void energy_service_minute_tick(void)
{
    if (!s_pw || !ntp_sync_is_synced()) return;
    roles_t r = get_roles();
    if (!r.zl) return;
    if (!s_pw_ieee) {
        // Pas de pw24.bin (premiere mise en route, LiXee-Box mise a jour) : reprise unique
        s_pw_ieee = r.zl;
        pw_import_box(r.zl);
    }
    if (r.zl != s_pw_ieee) {
        // Autre ZLinky : la courbe precedente ne le concerne pas
        memset(s_pw, 0, sizeof(pw_slot_t) * PW_SLOTS);
        s_pw_ieee = r.zl;
    }
    power_estimate_tick(r.zl);
    live_t L;
    read_live(r.zl, &L);
    if (!L.has_total) return;

    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    pw_slot_t *s = &s_pw[t.tm_hour * 60 + t.tm_min];
    s->minute = (uint32_t)(now / 60);
    for (int p = 0; p < 3; p++) {
        s->v[p] = L.has_phase[p] ? L.phase[p] : PW_ABSENT;
        s->v[3 + p] = L.has_inj ? L.inj_phase[p] : PW_ABSENT;
    }
    if (++s_pw_since_save >= PW_SAVE_EVERY) {
        s_pw_since_save = 0;
        pw_save();
    }
}

/** Parcourt les 1440 minutes, de la plus ancienne a la plus recente. */
typedef void (*pw_visit_fn)(void *ctx, int hh, int mm, const pw_slot_t *s);

static void pw_walk(void *ctx, pw_visit_fn fn)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    uint32_t now_min = (uint32_t)(now / 60);
    int cur = t.tm_hour * 60 + t.tm_min;
    for (int i = 1; i <= PW_SLOTS; i++) {
        int idx = (cur + i) % PW_SLOTS;
        const pw_slot_t *s = &s_pw[idx];
        bool fresh = s->minute && now_min >= s->minute && now_min - s->minute < PW_SLOTS;
        fn(ctx, idx / 60, idx % 60, fresh ? s : NULL);
    }
}

typedef struct {
    sbuf_t *b;
    int     part;       // 0 libelles, 1..3 phases, 4..6 injection
    bool    first;
    int32_t min, max, last, prev;
    bool    any;
} pw_ctx_t;

static void pw_emit(void *vctx, int hh, int mm, const pw_slot_t *s)
{
    pw_ctx_t *c = (pw_ctx_t *)vctx;
    sb_printf(c->b, c->first ? "" : ",");
    c->first = false;
    if (c->part == 0) { sb_printf(c->b, "\"%02d:%02d\"", hh, mm); return; }
    int32_t v = s ? s->v[c->part - 1] : PW_ABSENT;
    if (v == PW_ABSENT) sb_printf(c->b, "null");
    else sb_printf(c->b, "%ld", (long)v);
}

static void pw_stats(void *vctx, int, int, const pw_slot_t *s)
{
    pw_ctx_t *c = (pw_ctx_t *)vctx;
    if (!s) return;
    int32_t tot = 0;
    bool has = false;
    for (int p = 0; p < 3; p++) if (s->v[p] != PW_ABSENT) { tot += s->v[p]; has = true; }
    if (!has) return;
    if (!c->any || tot < c->min) c->min = tot;
    if (!c->any || tot > c->max) c->max = tot;
    c->prev = c->any ? c->last : tot;
    c->last = tot;
    c->any = true;
}

static void pw_has_inj(void *vctx, int, int, const pw_slot_t *s)
{
    pw_ctx_t *c = (pw_ctx_t *)vctx;
    if (s && ((s->v[3] != PW_ABSENT && s->v[3]) || (s->v[4] != PW_ABSENT && s->v[4]) ||
              (s->v[5] != PW_ABSENT && s->v[5]))) {
        c->any = true;
    }
}

char *energy_power_json(void)
{
    roles_t r = get_roles();
    sbuf_t b;
    sb_init(&b, 64 * 1024);
    if (!r.zl || !s_pw) {
        sb_printf(&b, "{\"configured\":false}");
        return sb_finish(&b);
    }
    live_t L;
    read_live(r.zl, &L);
    int nph = L.tri ? 3 : 1;

    pw_ctx_t st = {};
    pw_walk(&st, pw_stats);
    pw_ctx_t inj = {};
    pw_walk(&inj, pw_has_inj);

    sb_printf(&b, "{\"configured\":true,\"tri\":%s,\"goal\":%ld,\"labels\":[",
              L.tri ? "true" : "false", (long)L.goal_total);
    pw_ctx_t c = {};
    c.b = &b;
    c.part = 0; c.first = true; pw_walk(&c, pw_emit);
    sb_printf(&b, "],\"consumption\":[");
    for (int p = 0; p < nph; p++) {
        sb_printf(&b, "%s[", p ? "," : "");
        c.part = 1 + p; c.first = true; pw_walk(&c, pw_emit);
        sb_printf(&b, "]");
    }
    sb_printf(&b, "],\"injection\":[");
    if (inj.any) {
        for (int p = 0; p < nph; p++) {
            sb_printf(&b, "%s[", p ? "," : "");
            c.part = 4 + p; c.first = true; pw_walk(&c, pw_emit);
            sb_printf(&b, "]");
        }
    }
    // Prix du kWh de l'index en cours : "(~x EUR/j)" de la tendance (getTrendPower)
    sb_printf(&b, "],\"priceKwh\":");
    sb_printf(&b, "%.5f", price_for_attr(energy_tariff_current_index()));
    {
        const cfg_general_t *gc = cfg_get_general();
        sb_printf(&b, ",\"cspe\":%.5f,\"aboDay\":%.5f", parse_num(gc->tarif_cspe),
                  (parse_num(gc->tarif_abo) + parse_num(gc->tarif_cta)) / 30.0);
    }
    sb_printf(&b, ",\"stats\":");
    if (st.any) {
        sb_printf(&b, "{\"min\":%ld,\"max\":%ld,\"last\":%ld,\"trend\":%ld}}",
                  (long)st.min, (long)st.max, (long)st.last, (long)(st.last - st.prev));
    } else {
        sb_printf(&b, "null}");
    }
    return sb_finish(&b);
}

typedef struct {
    sbuf_t *b;
    int     nph;
    int32_t goal;
} pw_csv_ctx_t;

static void pw_csv_row(void *vctx, int hh, int mm, const pw_slot_t *s)
{
    pw_csv_ctx_t *c = (pw_csv_ctx_t *)vctx;
    if (!s) return;
    int32_t conso = 0, inj = 0;
    sb_printf(c->b, "%02d:%02d", hh, mm);
    for (int p = 0; p < c->nph; p++) {
        if (s->v[p] == PW_ABSENT) sb_printf(c->b, ";");
        else { sb_printf(c->b, ";%ld", (long)s->v[p]); conso += s->v[p] > 0 ? s->v[p] : 0; }
    }
    for (int p = 0; p < c->nph; p++) {
        int32_t v = s->v[3 + p];
        if (v == PW_ABSENT) sb_printf(c->b, ";");
        else { sb_printf(c->b, ";%ld", (long)v); inj += v > 0 ? v : 0; }
    }
    sb_printf(c->b, ";%ld;%ld;%ld", (long)conso, (long)inj, (long)(conso - inj));
    if (c->goal > 0) {
        sb_printf(c->b, ";");
        sb_dec(c->b, conso * 100.0 / c->goal, 1);
        sb_printf(c->b, ";%ld", (long)(c->goal - conso));
    }
    sb_printf(c->b, "\r\n");
}

char *energy_power_csv(void)
{
    roles_t r = get_roles();
    sbuf_t b;
    sb_init(&b, 64 * 1024);
    sb_printf(&b, "\xEF\xBB\xBF");
    if (!r.zl || !s_pw) {
        sb_printf(&b, "Aucun ZLinky configure\r\n");
        return sb_finish(&b);
    }
    live_t L;
    read_live(r.zl, &L);
    int nph = L.tri ? 3 : 1;
    sb_printf(&b, "Heure");
    if (L.tri) {
        sb_printf(&b, ";Puissance Ph1 (VA);Puissance Ph2 (VA);Puissance Ph3 (VA)"
                      ";Injection Ph1 (VA);Injection Ph2 (VA);Injection Ph3 (VA)");
    } else {
        sb_printf(&b, ";Puissance (VA);Injection (VA)");
    }
    sb_printf(&b, ";Consommation totale (VA);Injection totale (VA);Puissance nette (VA)");
    if (L.goal_total > 0) sb_printf(&b, ";Utilisation limite (%%);Marge limite (VA)");
    sb_printf(&b, "\r\n");
    pw_csv_ctx_t c = { &b, nph, L.goal_total };
    pw_walk(&c, pw_csv_row);
    return sb_finish(&b);
}

// ============================================================================
// /api/energy/tariff (carte tarif)
// ============================================================================

static uint16_t ptec_to_index(const char *ptec)
{
    static const struct { const char *p; uint16_t a; } MAP[] = {
        { "HCJB", 256 }, { "HPJB", 258 }, { "HCJW", 260 }, { "HPJW", 262 },
        { "HCJR", 264 }, { "HPJR", 266 }, { "HP", 258 }, { "PM", 258 },
    };
    for (size_t i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++) {
        if (strncmp(ptec, MAP[i].p, strlen(MAP[i].p)) == 0) return MAP[i].a;
    }
    return 256;   // TH.., HC.., HN.., inconnu
}

static void tempo_badge(cJSON *parent, const char *key, int color, const char *undef_icon)
{
    static const char *const CLS[4] = { "tempo-undef", "tempo-bleu", "tempo-blanc", "tempo-rouge" };
    static const char *const LBL[4] = { "Non d\xC3\xA9" "fini", "Jour Bleu", "Jour Blanc", "Jour Rouge" };
    static const char *const ICO[4] = { "?", "B", "W", "R" };
    if (color < 1 || color > 3) color = 0;
    cJSON *o = cJSON_AddObjectToObject(parent, key);
    cJSON_AddStringToObject(o, "class", CLS[color]);
    cJSON_AddStringToObject(o, "label", LBL[color]);
    cJSON_AddStringToObject(o, "icon", color ? ICO[color] : undef_icon);
}

typedef struct {
    char     tariff[24];
    const char *bg;
    int      today;
    int      tomorrow;
    uint16_t index;
} tariff_info_t;

/** Tarif en cours, couleurs Tempo et index, en historique comme en standard. */
static void tariff_info(uint64_t zl, int mode, tariff_info_t *ti)
{
    strlcpy(ti->tariff, "---", sizeof(ti->tariff));
    ti->bg = "#2980b9";
    ti->today = ti->tomorrow = 0;
    ti->index = 256;

    if (mode_is_histo(mode)) {
        char ptec[16];
        dd_str(zl, 0x0702, 32, ptec, sizeof(ptec));
        ti->index = ptec_to_index(ptec);
        static const struct { const char *p; const char *name; const char *bg; int tempo; } T[] = {
            { "HCJB", "HC Bleu", "#2980b9", 1 },  { "HPJB", "HP Bleu", "#154360", 1 },
            { "HCJW", "HC Blanc", "#7f8c8d", 2 }, { "HPJW", "HP Blanc", "#000000", 2 },
            { "HCJR", "HC Rouge", "#e74c3c", 3 }, { "HPJR", "HP Rouge", "#c0392b", 3 },
            { "TH", "Base", "#2980b9", 0 },       { "HC", "Heures Creuses", "#2980b9", 0 },
            { "HP", "Heures Pleines", "#154360", 0 }, { "HN", "Heures Normales", "#2980b9", 0 },
            { "PM", "Pointe Mobile", "#c0392b", 0 },
        };
        for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
            if (strncmp(ptec, T[i].p, strlen(T[i].p)) == 0) {
                strlcpy(ti->tariff, T[i].name, sizeof(ti->tariff));
                ti->bg = T[i].bg;
                ti->today = T[i].tempo;
                break;
            }
        }
        if (ti->today) {
            char demain[8];
            dd_str(zl, 0xFF66, 1, demain, sizeof(demain));   // DEMAIN : BLEU / BLAN / ROUG
            if (strncmp(demain, "BLEU", 4) == 0) ti->tomorrow = 1;
            else if (strncmp(demain, "BLAN", 4) == 0) ti->tomorrow = 2;
            else if (strncmp(demain, "ROUG", 4) == 0) ti->tomorrow = 3;
        }
        return;
    }

    char ltarf[24];
    if (!dd_str(zl, 0xFF66, 16, ltarf, sizeof(ltarf))) dd_str(zl, 0xFF66, 512, ltarf, sizeof(ltarf));
    if (ltarf[0]) strlcpy(ti->tariff, ltarf, sizeof(ti->tariff));
    // Le ZLinky en Standard envoie "Heure pleine" : comparaison sans la casse
    char up[sizeof(ltarf)];
    size_t ui = 0;
    for (; ltarf[ui] && ui + 1 < sizeof(up); ui++) up[ui] = (char)toupper((unsigned char)ltarf[ui]);
    up[ui] = '\0';
    bool is_hp = strstr(up, "HP") || strstr(up, "PLEINE");
    int64_t ntarf, stge;
    if (dd_num(zl, 0xFF66, 513, &ntarf) && ntarf >= 1 && ntarf <= 10) ti->index = (uint16_t)(256 + 2 * (ntarf - 1));
    if (dd_stge(zl, &stge)) {
        ti->today = (int)((stge >> 24) & 3);
        ti->tomorrow = (int)((stge >> 26) & 3);
    }
    static const char *const HP_BG[4] = { "#154360", "#154360", "#000000", "#c0392b" };
    static const char *const HC_BG[4] = { "#2980b9", "#2980b9", "#7f8c8d", "#e74c3c" };
    ti->bg = is_hp ? HP_BG[ti->today] : HC_BG[ti->today];
}

// Famille de l'abonnement d'apres le tarif en cours (getTariffFamily de la
// LiXee-Box) : 0 Base, 1 HC/HP (defaut), 2 EJP, 3 Tempo
static int tariff_family(uint64_t zl)
{
    char p[24] = "";
    if (mode_is_histo(linky_mode(zl))) {
        dd_str(zl, 0x0702, 32, p, sizeof(p));                          // PTEC
    } else if (!dd_str(zl, 0xFF66, 16, p, sizeof(p))) {
        dd_str(zl, 0xFF66, 512, p, sizeof(p));                         // LTARF
    }
    char u[sizeof(p)];
    size_t n = 0, s = 0;
    while (p[s] == ' ') s++;
    for (; p[s] && n + 1 < sizeof(u); s++) u[n++] = (char)toupper((unsigned char)p[s]);
    while (n > 0 && u[n - 1] == ' ') n--;
    u[n] = '\0';
    if (!n) return 1;
    if (strstr(u, "JB") || strstr(u, "JW") || strstr(u, "JR") || strstr(u, "BBR") || strstr(u, "BLEU") ||
        strstr(u, "BLANC") || strstr(u, "ROUGE") || strstr(u, "TEMPO")) return 3;
    if (!strncmp(u, "EJP", 3) || !strcmp(u, "HN..") || !strcmp(u, "PM..") ||
        strstr(u, "POINTE") || strstr(u, "NORMALE")) return 2;
    if (!strncmp(u, "TH", 2) || !strncmp(u, "BASE", 4) || !strncmp(u, "TOUTE", 5)) return 0;
    return 1;
}

bool energy_tariff_available(void)
{
    roles_t r = get_roles();
    return r.zl && device_data_find(r.zl);
}

uint16_t energy_tariff_current_index(void)
{
    if (!energy_tariff_available()) return 256;
    roles_t r = get_roles();
    tariff_info_t ti;
    tariff_info(r.zl, linky_mode(r.zl), &ti);
    return ti.index;
}

int energy_tariff_periods(uint16_t *out, int max)
{
    static const uint16_t BASE[] = { 256 }, HCHP[] = { 256, 258 }, TEMPO[] = { 256, 258, 260, 262, 264, 266 };
    roles_t r = get_roles();
    int f = r.zl ? tariff_family(r.zl) : 1;
    const uint16_t *src = f == 0 ? BASE : (f == 3 ? TEMPO : HCHP);
    int n = f == 0 ? 1 : (f == 3 ? 6 : 2);
    int k = 0;
    for (; k < n && k < max; k++) out[k] = src[k];
    return k;
}

void energy_tariff_label(uint16_t attr, char *out, size_t len)
{
    roles_t r = get_roles();
    int f = r.zl ? tariff_family(r.zl) : 1;
    const char *l = NULL;
    if (f == 3) {
        static const char *const T[] = { "HC Bleu", "HP Bleu", "HC Blanc", "HP Blanc", "HC Rouge", "HP Rouge" };
        if (attr >= 256 && attr <= 266 && !(attr & 1)) l = T[(attr - 256) / 2];
    } else if (f == 2) {
        l = attr == 256 ? "Heures Normales" : (attr == 258 ? "Pointe Mobile" : NULL);
    } else if (f == 0) {
        l = attr == 256 ? "Base" : NULL;
    } else {
        l = attr == 256 ? "Heures Creuses" : (attr == 258 ? "Heures Pleines" : NULL);
    }
    if (l) strlcpy(out, l, len);
    else snprintf(out, len, "P%u", attr);
}

char *energy_tariff_json(void)
{
    roles_t r = get_roles();
    cJSON *root = cJSON_CreateObject();
    if (!r.zl || !device_data_find(r.zl)) {
        cJSON_AddBoolToObject(root, "show", false);
        cJSON_AddStringToObject(root, "tariff", "---");
        cJSON_AddStringToObject(root, "bgColor", "#2980b9");
        cJSON_AddBoolToObject(root, "isTempo", false);
        tempo_badge(root, "today", 0, "?");
        tempo_badge(root, "tomorrow", 0, "-");
        cJSON_AddNullToObject(root, "index");
        cJSON_AddNullToObject(root, "price");
        cJSON_AddNullToObject(root, "nextChange");
        return json_out(root);
    }
    tariff_info_t ti;
    tariff_info(r.zl, linky_mode(r.zl), &ti);
    cJSON_AddBoolToObject(root, "show", true);
    cJSON_AddStringToObject(root, "tariff", ti.tariff);
    cJSON_AddStringToObject(root, "bgColor", ti.bg);
    cJSON_AddBoolToObject(root, "isTempo", ti.today != 0);
    tempo_badge(root, "today", ti.today, "?");
    tempo_badge(root, "tomorrow", ti.tomorrow, "-");
    double pr = ti.index ? price_for_attr(ti.index) : 0;
    if (ti.index) cJSON_AddNumberToObject(root, "index", ti.index);
    else cJSON_AddNullToObject(root, "index");
    if (pr > 0) cJSON_AddNumberToObject(root, "price", round(pr * 10000.0) / 10000.0);
    else cJSON_AddNullToObject(root, "price");
    // Prochain changement (powerStatsNextChange de la Box) : aucun en Base,
    // 06:00 / 22:00 en Tempo, sinon d'apres les bascules observees
    int fam = tariff_family(r.zl);
    char nc[8];
    if (fam == 3 || ti.today) {
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        cJSON_AddStringToObject(root, "nextChange", (t.tm_hour >= 6 && t.tm_hour < 22) ? "22:00" : "06:00");
    } else if (fam != 0 && power_stats_next_change(nc, sizeof(nc))) {
        cJSON_AddStringToObject(root, "nextChange", nc);
    } else {
        cJSON_AddNullToObject(root, "nextChange");
    }
    return json_out(root);
}

// ============================================================================
// API pour les autres modules (alertes, regles, MQTT...)
// ============================================================================

bool energy_service_get_live(energy_live_t *out)
{
    memset(out, 0, sizeof(*out));
    roles_t r = get_roles();
    if (!r.zl) return false;
    live_t L;
    read_live(r.zl, &L);
    out->valid = L.has_total;
    out->connected = zlinky_connected(r.zl);
    out->mode = L.mode;
    out->tri = L.tri;
    out->producer = L.producer;
    for (int p = 0; p < 3; p++) {
        out->has_phase[p] = L.has_phase[p];
        out->phase_va[p] = L.phase[p];
    }
    out->total_va = L.total;
    out->inj_va = L.inj;
    out->goal_va = L.goal_total;
    static const uint16_t VOLT[3] = { 1285, 2309, 2565 };
    int64_t v;
    for (int p = 0; p < (L.tri ? 3 : 1); p++) {
        if (dd_num(r.zl, 0x0B04, VOLT[p], &v) && v > 0) {
            out->has_voltage[p] = true;
            out->voltage[p] = (int32_t)v;
        }
    }
    out->overload = overload_alert(r.zl, L.mode) != NULL;
    tariff_info_t ti;
    tariff_info(r.zl, L.mode, &ti);
    strlcpy(out->tariff, ti.tariff, sizeof(out->tariff));
    out->tariff_index = ti.index;
    out->tariff_price = price_for_attr(ti.index);
    out->tempo_today = ti.today;
    out->tempo_tomorrow = ti.tomorrow;
    return true;
}

bool energy_service_period_cost(energy_cost_period_t period, int64_t *wh, double *eur, int64_t *prod_wh)
{
    roles_t r = get_roles();
    if (!r.zl) return false;
    struct tm now;
    local_now(&now);
    slot_ref_t s;
    double f;
    switch (period) {
    case ENERGY_COST_YESTERDAY: s = previous_slot(ENERGY_GRAN_DAY, &now); f = 1.0 / 30.0; break;
    case ENERGY_COST_MONTH:     s = current_slot(ENERGY_GRAN_MONTH, &now); f = 1.0; break;
    case ENERGY_COST_YEAR:      s = current_slot(ENERGY_GRAN_YEAR, &now); f = gran_factor(ENERGY_GRAN_YEAR, &now); break;
    default:                    s = current_slot(ENERGY_GRAN_DAY, &now); f = 1.0 / 30.0; break;
    }
    slot_agg_t a;
    aggregate(&r, s, &a);
    cost_t c = cost_of(&a, f);
    if (wh) *wh = a.idx_total;
    if (eur) *eur = c.total;
    if (prod_wh) *prod_wh = a.prod_wh;
    return true;
}

// ============================================================================
// /api/energy/linky (fenetre "Donnees Linky")
// ============================================================================

static bool mode_listed(const char *modes, int mode)
{
    if (!modes || !modes[0]) return true;
    char m[4];
    snprintf(m, sizeof(m), "%d", mode);
    const char *p = modes;
    while (*p) {
        while (*p == ';' || *p == ',' || *p == ' ') p++;
        if (!*p) break;
        if ((int)strtol(p, NULL, 10) == mode) return true;
        while (*p && *p != ';' && *p != ',') p++;
    }
    return false;
}

char *energy_linky_json(void)
{
    roles_t r = get_roles();
    cJSON *root = cJSON_CreateObject();
    const devdata_template_t *t = r.zl ? device_data_get_template(r.zl) : NULL;
    if (!t || !t->loaded) {
        cJSON_AddBoolToObject(root, "configured", false);
        return json_out(root);
    }
    int mode = linky_mode(r.zl);
    cJSON_AddBoolToObject(root, "configured", true);
    cJSON_AddNumberToObject(root, "mode", mode);
    cJSON_AddStringToObject(root, "modeLabel", mode_label(mode));
    cJSON *items = cJSON_AddArrayToObject(root, "items");
    for (int i = 0; i < t->state_count; i++) {
        const devdata_template_state_t *s = &t->states[i];
        if (!mode_listed(s->mode, mode)) continue;
        if (!s->visible) {
            // Hors tableau, mais present dans la forme plate de la Box
            char flat[16], txt[40];
            int64_t v;
            snprintf(flat, sizeof(flat), "%u_%u", (unsigned)s->cluster, (unsigned)s->attribute);
            if (cJSON_GetObjectItem(root, flat)) continue;
            if (strcmp(s->type, "numeric") == 0 && dd_num(r.zl, s->cluster, s->attribute, &v)) {
                cJSON_AddNumberToObject(root, flat, v * (s->coefficient > 0 ? s->coefficient : 1));
            } else if (dd_str(r.zl, s->cluster, s->attribute, txt, sizeof(txt))) {
                cJSON_AddStringToObject(root, flat, txt);
            }
            continue;
        }
        cJSON *o = cJSON_CreateObject();
        char key[16];
        snprintf(key, sizeof(key), "%04X_%u", s->cluster, s->attribute);
        cJSON_AddStringToObject(o, "key", key);
        cJSON_AddStringToObject(o, "name", s->name);
        int64_t v;
        char txt[24];
        if (strcmp(s->type, "numeric") == 0 && dd_num(r.zl, s->cluster, s->attribute, &v)) {
            double coef = s->coefficient > 0 ? s->coefficient : 1;
            cJSON_AddNumberToObject(o, "value", v * coef);
        } else if (dd_str(r.zl, s->cluster, s->attribute, txt, sizeof(txt))) {
            cJSON_AddStringToObject(o, "value", txt);
        } else {
            cJSON_Delete(o);
            continue;
        }
        cJSON_AddStringToObject(o, "unit", s->unit);
        // Forme plate de la LiXee-Box (APIgetLinky) : "<cluster>_<attribut>" en decimal
        char flat[16];
        snprintf(flat, sizeof(flat), "%u_%u", (unsigned)s->cluster, (unsigned)s->attribute);
        if (!cJSON_GetObjectItem(root, flat)) {
            const cJSON *val = cJSON_GetObjectItem(o, "value");
            if (cJSON_IsNumber(val)) cJSON_AddNumberToObject(root, flat, val->valuedouble);
            else if (cJSON_IsString(val)) cJSON_AddStringToObject(root, flat, val->valuestring);
        }
        cJSON_AddItemToArray(items, o);
    }

    // Registre de statuts STGE decode (parseStatusRegister de la LiXee-Box)
    int64_t st;
    if (!mode_is_histo(mode) && dd_stge(r.zl, &st)) {
        uint32_t v = (uint32_t)st;
        cJSON *sg = cJSON_AddArrayToObject(root, "stge");
        auto add = [&](const char *name, const char *val) {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "name", name);
            cJSON_AddStringToObject(o, "value", val);
            cJSON_AddItemToArray(sg, o);
        };
        static const char *const COUPURE[8] = { "Ferm\xC3\xA9", "Surpuissance", "Surtension", "D\xC3\xA9lestage",
                                                "Ordre CPL / Euridis", "Surchauffe (surcourant)",
                                                "Surchauffe (simple)", "Inconnu" };
        static const char *const EURIDIS[4] = { "D\xC3\xA9sactiv\xC3\xA9" "e", "Activ\xC3\xA9" "e sans s\xC3\xA9" "curit\xC3\xA9",
                                                "Inconnu", "Activ\xC3\xA9" "e avec s\xC3\xA9" "curit\xC3\xA9" };
        static const char *const CPL[4] = { "Nouveau d\xC3\xA9verrouill\xC3\xA9", "Nouveau verrouill\xC3\xA9",
                                            "Enregistr\xC3\xA9", "Inconnu" };
        static const char *const TEMPO[4] = { "Ind\xC3\xA9" "fini", "BLEU", "BLANC", "ROUGE" };
        static const char *const PM[4] = { "Aucun", "PM1", "PM2", "PM3" };
        char buf[16];
        add("Contact sec", (v & 1) ? "Ouvert" : "Ferm\xC3\xA9");
        add("Organe de coupure", COUPURE[(v >> 1) & 7]);
        add("Cache-borne distributeur", ((v >> 4) & 1) ? "Ouvert" : "Ferm\xC3\xA9");
        add("Surtension sur une phase", ((v >> 6) & 1) ? "Oui" : "Non");
        add("D\xC3\xA9passement puissance de r\xC3\xA9" "f\xC3\xA9rence", ((v >> 7) & 1) ? "Oui" : "Non");
        add("Fonctionnement", ((v >> 8) & 1) ? "Producteur" : "Consommateur");
        add("Sens de l'\xC3\xA9nergie active", ((v >> 9) & 1) ? "N\xC3\xA9gative" : "Positive");
        snprintf(buf, sizeof(buf), "Index %u", (unsigned)(((v >> 10) & 0xF) + 1));
        add("Tarif fournisseur en cours", buf);
        snprintf(buf, sizeof(buf), "Index %u", (unsigned)(((v >> 14) & 0x3) + 1));
        add("Tarif distributeur en cours", buf);
        add("Horloge", ((v >> 16) & 1) ? "D\xC3\xA9grad\xC3\xA9" "e" : "Correcte");
        add("Type de TIC", ((v >> 17) & 1) ? "Standard" : "Historique");
        add("Communicateur Euridis", EURIDIS[(v >> 19) & 3]);
        add("\xC3\x89tat CPL", CPL[(v >> 21) & 3]);
        add("Synchronisation CPL", ((v >> 23) & 1) ? "Synchronis\xC3\xA9" : "Non synchronis\xC3\xA9");
        add("Couleur Tempo du jour", TEMPO[(v >> 24) & 3]);
        add("Couleur Tempo du lendemain", TEMPO[(v >> 26) & 3]);
        add("Pr\xC3\xA9" "avis pointe mobile", PM[(v >> 28) & 3]);
        add("Pointe mobile", PM[(v >> 30) & 3]);
    }
    return json_out(root);
}
