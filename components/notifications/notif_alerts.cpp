// ============================================================================
// Notifications - alertes energie (portage de alertChecks.cpp et des alertes
// des handlers ZLinky : lixee.cpp, ElectricalMeasurement.cpp, et du controle
// de budget de ScanDevicesToRAZ / des alertes "Production nulle" de la boucle
// principale Arduino).
//
// Tout s'execute dans la tache "notif" (pile en PSRAM) : ce fichier ne lit
// que la RAM (device_data, energy_history, energy_service, configuration).
// L'etat des transitions (anciennes valeurs) n'est pas persiste, comme sur la
// LiXee-Box : apres un redemarrage, la premiere valeur recue sert de reference.
//
// Ecarts voulus avec la LiXee-Box (corrections) :
//   - surconsommation (ADPS) : une alerte par rapport recu, soit plusieurs par
//     minute pendant un depassement ; ici 5 min minimum entre deux alertes ;
//   - puissance nulle : ignoree quand le site produit (en injection, la
//     puissance soutiree vaut normalement 0) ;
//   - seuils de tension a 0 : la LiXee-Box alertait en surtension a chaque
//     mesure (0 < tension) ; un seuil nul desactive maintenant l'alerte ;
//   - preavis EJP : la LiXee-Box affichait la valeur hexadecimale brute ;
//   - gel : la temperature (int16 signe) etait lue non signee, donc une
//     temperature negative (0xFF38 = -2 degC) etait vue comme 653 degC et
//     l'alerte gel ne se declenchait jamais sous 0 degC ;
//   - anomalie journaliere : moyenne calculee sur l'historique des 6 jours
//     precedents (persistant) au lieu d'un tampon RAM vide a chaque reboot ;
//   - production nulle : seulement si un compteur de production existe (la
//     LiXee-Box alertait aussi sans compteur configure) ;
//   - budget : cout complet de la veille (energie, taxes, part d'abonnement,
//     comme la page Energie) compare au budget mensuel / 30.
// ============================================================================

#include "notifications.h"
#include "notif_internal.h"
#include "config_manager.h"
#include "device_data.h"
#include "energy_history.h"
#include "energy_service.h"
#include "ntp_sync.h"

#include "esp_timer.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <time.h>

static const char *TAG = "NOTIF_AL";

// ----------------------------------------------------------------------------
// Outils
// ----------------------------------------------------------------------------

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

static bool dd_num(uint64_t ieee, uint16_t cl, uint16_t at, int64_t *out)
{
    const char *v = ieee ? device_data_get_value_ptr(ieee, cl, at) : NULL;
    if (!v || !v[0]) return false;
    *out = (int64_t)strtoull(v, NULL, 16);
    return true;
}

static float coeff_of(uint64_t ieee, uint16_t cl, uint16_t at)
{
    float c = device_data_get_coefficient(ieee, cl, at);
    return c > 0 ? c : 1.0f;
}

static const char *tempo_name(int c)
{
    switch (c) {
    case 1: return "Bleu";
    case 2: return "Blanc";
    case 3: return "Rouge";
    default: return "Inconnue";
    }
}

static const int64_t US_PER_S = 1000000LL;

// ----------------------------------------------------------------------------
// Etat des transitions (tache "notif" uniquement : pas de verrou)
// ----------------------------------------------------------------------------

static bool    s_over_v[3], s_under_v[3];
static int64_t s_last_papp = -1;
static bool    s_prod_sup = false;
static int64_t s_subscribed_last = 0;
static bool    s_overload_prev = false;
static char    s_tariff[24] = "";
static int     s_tempo_today = -1, s_tempo_tomorrow = -1;
static int     s_tomorrow_alerted = 0;
static int64_t s_tomorrow_alerted_at = 0;
static int64_t s_pejp = -1;

static bool    s_op_active = false;
static int64_t s_op_since = 0, s_op_last = 0;
static int64_t s_freeze_last = 0;
static bool    s_prod_zero_latched = false;
static int64_t s_last_minute = 0;

static int     s_today_key = -1;              // annee * 1000 + jour de l'annee
static bool    s_anomaly_done, s_metrics_done, s_water_done, s_night_done;
static int     s_budget_key = -1;             // date de la veille deja controlee

// ----------------------------------------------------------------------------
// Alertes sur rapport d'attribut
// ----------------------------------------------------------------------------

bool notif_alerts_wants(uint64_t ieee, uint16_t cluster)
{
    if (!ieee) return false;
    const cfg_general_t *g = cfg_get_general();
    if (ieee == parse_ieee(g->zlinky)) {
        return cluster == 0x0B04 || cluster == 0xFF66 || cluster == 0x0702;
    }
    if (ieee == parse_ieee(g->production)) return cluster == 0xFF66;
    return false;
}

static void subscribed_power_alert(void)
{
    int64_t now = esp_timer_get_time();
    if (s_subscribed_last && now - s_subscribed_last < 300 * US_PER_S) return;
    s_subscribed_last = now;
    notifications_add_alert("\U0001F6A8\u26A1Surconsommation",
                            " D\u00E9passement de la puissance souscrite", NOTIF_ERROR,
                            "subscribed_power", 0, 0);
    // Le delestage (composant delestage) surveille lui-meme le depassement
}

/** Tarif, couleurs Tempo et depassement, decodes par energy_service. */
static void check_tariff_state(void)
{
    const cfg_notifications_t *n = cfg_get_notifications();
    energy_live_t L;
    if (!energy_service_get_live(&L)) return;

    bool known = L.tariff[0] && strcmp(L.tariff, "---") != 0;
    if (known) {
        if (n->price_change && s_tariff[0] && strcmp(s_tariff, L.tariff) != 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "--> tarif : %s", L.tariff);
            notifications_add_alert("\U0001F553\U0001F4B5 Changement de tarif", msg, NOTIF_INFO,
                                    "price_change", (float)L.tariff_price, 0);
        }
        strlcpy(s_tariff, L.tariff, sizeof(s_tariff));
    }

    // Jour rouge : passage a rouge depuis une couleur deja connue
    if (n->red_color && L.tempo_today == 3 && s_tempo_today >= 0 && s_tempo_today != 3) {
        notifications_add_alert("\u26A0\uFE0F\U0001F534Journ\u00E9e Rouge !",
                                "Il faut consommer le moins possible", NOTIF_CRITICAL, "red_day", 3, 0);
    }
    s_tempo_today = L.tempo_today;

    // Couleur du lendemain : nouvelle couleur annoncee. Une meme couleur n'est
    // pas reannoncee dans les 6 h (valeur fugitivement inconnue entre deux trames).
    if (n->color_tomorrow && L.tempo_tomorrow > 0 && s_tempo_tomorrow >= 0 &&
        L.tempo_tomorrow != s_tempo_tomorrow) {
        int64_t now = esp_timer_get_time();
        if (L.tempo_tomorrow != s_tomorrow_alerted || now - s_tomorrow_alerted_at > 6 * 3600 * US_PER_S) {
            char msg[48];
            snprintf(msg, sizeof(msg), "Couleur : %s", tempo_name(L.tempo_tomorrow));
            notifications_add_alert("\U0001F553\U0001F4B5Couleur du lendemain", msg, NOTIF_WARNING,
                                    "color_tomorrow", (float)L.tempo_tomorrow, 0);
            s_tomorrow_alerted = L.tempo_tomorrow;
            s_tomorrow_alerted_at = now;
        }
    }
    s_tempo_tomorrow = L.tempo_tomorrow;

    // Depassement de puissance souscrite (ADPS en historique, STGE bit 7 en standard)
    if (n->subscribed_power && L.overload && !s_overload_prev) subscribed_power_alert();
    s_overload_prev = L.overload;
}

static void check_voltage(int phase, uint16_t attr, int64_t raw)
{
    const cfg_notifications_t *n = cfg_get_notifications();
    uint64_t zl = parse_ieee(cfg_get_general()->zlinky);
    long v = lroundf((float)raw * coeff_of(zl, 0x0B04, attr));
    char msg[64];
    snprintf(msg, sizeof(msg), "Tension relev\u00E9e Ph%d : %ld V", phase + 1, v);

    if (n->over_voltage && n->over_voltage_threshold > 0) {
        if (v > n->over_voltage_threshold) {
            if (!s_over_v[phase]) {
                s_over_v[phase] = true;
                notifications_add_alert("\u26A1\u26A0\uFE0F Sur-tension", msg, NOTIF_WARNING, "over_voltage",
                                        (float)v, (float)n->over_voltage_threshold);
            }
        } else {
            s_over_v[phase] = false;
        }
    }
    if (n->under_voltage && n->under_voltage_threshold > 0 && v > 0) {
        if (v < n->under_voltage_threshold) {
            if (!s_under_v[phase]) {
                s_under_v[phase] = true;
                notifications_add_alert("\u26A1\u26A0\uFE0F Sous-tension", msg, NOTIF_WARNING, "under_voltage",
                                        (float)v, (float)n->under_voltage_threshold);
            }
        } else {
            s_under_v[phase] = false;
        }
    }
}

void notif_alerts_on_event(const notif_event_t *ev)
{
    const cfg_notifications_t *n = cfg_get_notifications();
    const cfg_general_t *g = cfg_get_general();
    uint64_t zl = parse_ieee(g->zlinky);
    uint64_t pr = parse_ieee(g->production);

    if (zl && ev->ieee == zl) {
        if (ev->cluster == 0x0B04 && !ev->is_str) {
            switch (ev->attr) {
            case 0x0505: check_voltage(0, ev->attr, ev->num); break;   // URMS / URMS1
            case 0x0905: check_voltage(1, ev->attr, ev->num); break;   // URMS2
            case 0x0A05: check_voltage(2, ev->attr, ev->num); break;   // URMS3
            case 0x050F:                                               // PAPP / SINSTS1
                if (n->power_outage && ev->num == 0 && s_last_papp > 0) {
                    energy_live_t L;
                    bool producer = energy_service_get_live(&L) && L.producer;
                    if (!producer) {
                        char msg[80];
                        snprintf(msg, sizeof(msg), "La puissance apparente de %s est \u00E0 0 VA", g->zlinky);
                        notifications_add_alert("\u26A10\uFE0F\u20E3Puissance apparente nulle", msg,
                                                NOTIF_WARNING, "power_outage", 0, 0);
                    }
                }
                s_last_papp = ev->num;
                break;
            default:
                break;
            }
        }
        if (ev->cluster == 0xFF66 && !ev->is_str) {
            if (ev->attr == 0x0005 && n->subscribed_power && ev->num > 0) {
                subscribed_power_alert();                              // ADPS (historique)
            } else if (ev->attr == 0x0004) {                           // PEJP (minutes)
                if (n->pejp && s_pejp >= 0 && ev->num != s_pejp && ev->num > 0) {
                    char msg[48];
                    snprintf(msg, sizeof(msg), "Pr\u00E9avis EJP : %lld min", (long long)ev->num);
                    notifications_add_alert("\u26A1Pr\u00E9avis d\u00E9but EJP", msg, NOTIF_WARNING, "ejp",
                                            (float)ev->num, 0);
                }
                s_pejp = ev->num;
            }
        }
        if (ev->cluster == 0xFF66 || (ev->cluster == 0x0702 && ev->attr == 0x0020)) {
            check_tariff_state();
        }
    }

    // Production > consommation : SINSTI (FF66/519) du compteur de production
    // (ou du ZLinky producteur sans compteur dedie) contre PAPP du ZLinky
    if (ev->cluster == 0xFF66 && ev->attr == 519 && !ev->is_str && n->prod_sup_conso &&
        ((pr && ev->ieee == pr) || (!pr && zl && ev->ieee == zl))) {
        int64_t conso = 0;
        if (dd_num(zl, 0x0B04, 0x050F, &conso) && conso > 0) {
            if (ev->num > conso) {
                if (!s_prod_sup) {
                    s_prod_sup = true;
                    char msg[160];
                    snprintf(msg, sizeof(msg),
                             "La puissance apparente inject\u00E9e : %lld VA est sup\u00E9rieure \u00E0 la consommation : %lld VA",
                             (long long)ev->num, (long long)conso);
                    notifications_add_alert("\u2600\uFE0F\u27A1\uFE0F\u26A1Production > Consommation", msg,
                                            NOTIF_INFO, "prod_sup_conso", (float)ev->num, (float)conso);
                }
            } else {
                s_prod_sup = false;
            }
        }
    }
}

// ----------------------------------------------------------------------------
// Historique : totaux journaliers
// ----------------------------------------------------------------------------

/** Energie consommee (Wh) par le ZLinky dans un creneau jour : somme des index, a defaut attr 0. */
static int64_t zlinky_day_wh(const energy_history_t *h, int slot)
{
    static const uint16_t IDX[10] = { 256, 258, 260, 262, 264, 266, 268, 270, 272, 274 };
    int64_t sum = 0;
    bool any = false;
    for (int i = 0; i < 10; i++) {
        if (!energy_history_has_attr(h, IDX[i])) continue;
        int32_t v = energy_history_slot_value(h, ENERGY_GRAN_DAY, slot, IDX[i]);
        if (v > 0) sum += v;
        any = true;
    }
    if (!any) {
        int32_t v = energy_history_slot_value(h, ENERGY_GRAN_DAY, slot, 0);
        sum = v > 0 ? v : 0;
    }
    return sum;
}

/** Creneau "jour" de la date courante - k jours. */
static int day_slot(int k)
{
    time_t t = time(NULL) - (time_t)k * 86400;
    struct tm lt;
    localtime_r(&t, &lt);
    return lt.tm_mday - 1;
}

static void check_daily_anomaly(void)
{
    const cfg_notifications_t *n = cfg_get_notifications();
    if (!n->daily_anomaly) return;
    const energy_history_t *h = energy_history_find(parse_ieee(cfg_get_general()->zlinky));
    if (!h) return;

    int64_t today = zlinky_day_wh(h, day_slot(0));
    int64_t sum = 0;
    int cnt = 0;
    for (int k = 1; k <= 6; k++) {
        int64_t v = zlinky_day_wh(h, day_slot(k));
        if (v > 0) { sum += v; cnt++; }          // jour sans donnees : ignore
    }
    if (cnt < 2) return;                           // comme la LiXee-Box : 3 jours minimum
    int64_t avg = sum / cnt;
    int pct = n->daily_anomaly_percent > 0 ? n->daily_anomaly_percent : 30;
    int64_t thr = avg + avg * pct / 100;
    if (today > thr) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "Consommation du jour %.1f kWh d\u00E9passe la moyenne de %.1f kWh (+%d%%)",
                 today / 1000.0, avg / 1000.0, pct);
        notifications_add_alert("\U0001F4CA Consommation anormale", msg, NOTIF_WARNING, "daily_anomaly",
                                today / 1000.0f, avg / 1000.0f);
    }
}

/** Litres d'eau d'un ensemble de creneaux (attr 0 x coefficient de la configuration). */
static long water_litres(energy_gran_t gran, int first, int last)
{
    const cfg_general_t *g = cfg_get_general();
    const energy_history_t *h = energy_history_find(parse_ieee(g->water));
    if (!h) return -1;
    double coeff = g->coeff_water > 0 ? g->coeff_water : 1.0;
    double sum = 0;
    for (int s = first; s <= last; s++) {
        int32_t v = energy_history_slot_value(h, gran, s, 0);
        if (v > 0) sum += v * coeff;
    }
    return lround(sum);
}

static void check_water_leak(void)
{
    const cfg_notifications_t *n = cfg_get_notifications();
    if (!n->water_leak || !cfg_get_general()->water[0]) return;
    int slot = day_slot(0);
    long litres = water_litres(ENERGY_GRAN_DAY, slot, slot);   // de 00h a 06h
    int thr = n->water_leak_threshold > 0 ? n->water_leak_threshold : 5;
    if (litres > thr) {
        char msg[120];
        snprintf(msg, sizeof(msg), "Consommation d'eau nocturne de %ldL d\u00E9passe le seuil de %dL", litres, thr);
        notifications_add_alert("\U0001F4A7 Fuite d'eau possible", msg, NOTIF_ERROR, "water_leak",
                                (float)litres, (float)thr);
    }
}

static void check_night_water_leak(void)
{
    const cfg_notifications_t *n = cfg_get_notifications();
    if (!n->night_water_leak || !cfg_get_general()->water[0]) return;
    long litres = water_litres(ENERGY_GRAN_HOUR, 0, 5);          // creneaux 00h..05h du jour
    int thr = n->night_water_leak_threshold > 0 ? n->night_water_leak_threshold : 1;
    if (litres > thr) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "Consommation d'eau nocturne (00h-05h) de %ldL d\u00E9passe le seuil de %dL", litres, thr);
        notifications_add_alert("\U0001F4A7\U0001F319 Fuite d'eau nocturne", msg, NOTIF_ERROR,
                                "night_water_leak", (float)litres, (float)thr);
    }
}

static void send_daily_metrics(void)
{
    const cfg_notifications_t *n = cfg_get_notifications();
    if (!n->daily_metrics) return;
    const cfg_general_t *g = cfg_get_general();
    nbuf_t b;
    nbuf_init(&b, 512);
    double total = 0;
    int slot = day_slot(0);

    int64_t wh = 0, prod = 0;
    double eur = 0;
    if (g->zlinky[0] && energy_service_period_cost(ENERGY_COST_TODAY, &wh, &eur, &prod)) {
        if (wh > 0) {
            nbuf_printf(&b, "\u26A1 Conso: %.1f kWh", wh / 1000.0);
            if (eur > 0) nbuf_printf(&b, " (%.2f \u20AC)", eur);
            nbuf_str(&b, "\n");
            total += eur;
        }
        if (prod > 0) {
            double rev = prod / 1000.0 * parse_num(g->tarif_idx_prod);
            nbuf_printf(&b, "\u2600\uFE0F Prod: %.1f kWh", prod / 1000.0);
            if (rev > 0) nbuf_printf(&b, " (+%.2f \u20AC)", rev);
            nbuf_str(&b, "\n");
        }
    }

    if (g->gaz[0]) {
        const energy_history_t *h = energy_history_find(parse_ieee(g->gaz));
        int32_t raw = h ? energy_history_slot_value(h, ENERGY_GRAN_DAY, slot, 0) : 0;
        if (raw > 0) {
            double coeff = g->coeff_gaz > 0 ? g->coeff_gaz : 1.0;
            double v = raw * coeff;
            if (strcasecmp(g->unit_gaz, "Wh") == 0) {
                double e = v / 1000.0 * parse_num(g->tarif_gaz);
                nbuf_printf(&b, "\U0001F525 Gaz: %.1f kWh", v / 1000.0);
                if (e > 0) { nbuf_printf(&b, " (%.2f \u20AC)", e); total += e; }
            } else {
                // Unite de volume : pas de prix au kWh applicable
                nbuf_printf(&b, "\U0001F525 Gaz: %.2f %s", v, g->unit_gaz);
            }
            nbuf_str(&b, "\n");
        }
    }

    if (g->water[0]) {
        long litres = water_litres(ENERGY_GRAN_DAY, slot, slot);
        if (litres > 0) {
            double e = litres * parse_num(g->tarif_water) / 1000.0;   // tarif au m3
            nbuf_printf(&b, "\U0001F4A7 Eau: %ld L", litres);
            if (e > 0) { nbuf_printf(&b, " (%.2f \u20AC)", e); total += e; }
            nbuf_str(&b, "\n");
        }
    }

    if (b.len == 0) nbuf_str(&b, "Aucune donn\u00E9e disponible pour aujourd'hui.");
    else if (total > 0) nbuf_printf(&b, "\U0001F4B0 Total: %.2f \u20AC", total);
    char *msg = nbuf_finish(&b);
    if (!msg) return;
    notifications_add_alert("\U0001F4CA R\u00E9sum\u00E9 quotidien", msg, NOTIF_INFO, "daily_metrics",
                            (float)total, 0);
    free(msg);
}

void notif_alerts_daily(void)
{
    if (!ntp_sync_is_synced()) return;
    time_t y = time(NULL) - 86400;
    struct tm ty;
    localtime_r(&y, &ty);
    int key = (ty.tm_year + 1900) * 1000 + ty.tm_yday;
    if (key == s_budget_key) return;              // veille deja controlee
    s_budget_key = key;

    const cfg_notifications_t *n = cfg_get_notifications();
    if (!n->over_budget || n->over_budget_threshold <= 0) return;
    int64_t wh = 0;
    double eur = 0;
    if (!energy_service_period_cost(ENERGY_COST_YESTERDAY, &wh, &eur, NULL) || wh <= 0) return;
    double budget_day = n->over_budget_threshold / 30.0;
    if (eur > budget_day) {
        char msg[200];
        snprintf(msg, sizeof(msg),
                 "Attention la consommation d'hier d\u00E9passe votre budget pr\u00E9vu - consomm\u00E9 : %lld Wh "
                 "(%.2f \u20AC) budget : %.2f \u20AC",
                 (long long)wh, eur, budget_day);
        notifications_add_alert("\U0001F4B8\U0001F6A8D\u00E9passement de budget", msg, NOTIF_WARNING,
                                "over_budget", (float)eur, (float)budget_day);
    }
}

// ----------------------------------------------------------------------------
// Controles periodiques
// ----------------------------------------------------------------------------

static void check_over_power(void)
{
    const cfg_notifications_t *n = cfg_get_notifications();
    energy_live_t L;
    if (!n->over_power || !energy_service_get_live(&L) || !L.valid || !L.connected) {
        s_op_active = false;
        return;
    }
    int thr = n->over_power_threshold > 0 ? n->over_power_threshold : 6000;
    int dur = n->over_power_duration > 0 ? n->over_power_duration : 60;
    int cool = n->over_power_cooldown > 0 ? n->over_power_cooldown : 30;
    int64_t now = esp_timer_get_time();
    if (L.total_va <= thr) {
        s_op_active = false;
        return;
    }
    if (!s_op_active) {
        s_op_active = true;
        s_op_since = now;
        return;
    }
    if (now - s_op_since < dur * US_PER_S) return;
    if (s_op_last && now - s_op_last < cool * 60 * US_PER_S) return;
    s_op_last = now;
    char msg[120];
    snprintf(msg, sizeof(msg), "Puissance %ldW d\u00E9passe le seuil de %dW depuis %ds",
             (long)L.total_va, thr, dur);
    notifications_add_alert("\u26A1\U0001F534 Surpuissance soutenue", msg, NOTIF_ERROR, "over_power",
                            (float)L.total_va, (float)thr);
}

static void check_freeze(void)
{
    const cfg_notifications_t *n = cfg_get_notifications();
    if (!n->freeze || !n->freeze_sensor_ieee[0]) return;
    uint64_t ieee = parse_ieee(n->freeze_sensor_ieee);
    const char *hex = ieee ? device_data_get_value_ptr(ieee, 0x0402, 0x0000) : NULL;
    if (!hex || !hex[0]) return;
    long raw = strtol(hex, NULL, 16);
    if (strlen(hex) <= 4) raw = (int16_t)(uint16_t)raw;     // MeasuredValue : int16 signe
    if (raw == -32768) return;                               // 0x8000 : mesure invalide (ZCL)
    float temp = raw * coeff_of(ieee, 0x0402, 0x0000) / 100.0f;
    int thr = n->freeze_threshold != 0 ? n->freeze_threshold : 300;
    int cool = n->freeze_cooldown > 0 ? n->freeze_cooldown : 60;
    if (temp * 100.0f >= thr) return;
    int64_t now = esp_timer_get_time();
    if (s_freeze_last && now - s_freeze_last < cool * 60 * US_PER_S) return;
    s_freeze_last = now;
    char msg[100];
    snprintf(msg, sizeof(msg), "Temp\u00E9rature %.1f\u00B0C en dessous du seuil de %.1f\u00B0C",
             temp, thr / 100.0f);
    notifications_add_alert("\U0001F976 Alerte gel", msg, NOTIF_ERROR, "freeze", temp, thr / 100.0f);
}

static void check_prod_zero(void)
{
    const cfg_notifications_t *n = cfg_get_notifications();
    if (!n->prod_zero) { s_prod_zero_latched = false; return; }
    // Laisser l'historique se remplir apres le demarrage
    if (esp_timer_get_time() < 3600 * US_PER_S) return;
    const cfg_general_t *g = cfg_get_general();
    const energy_history_t *h = NULL;
    if (g->production[0]) {
        h = energy_history_find(parse_ieee(g->production));
    } else {
        energy_live_t L;
        if (energy_service_get_live(&L) && (L.mode == 5 || L.mode == 7)) {
            h = energy_history_find(parse_ieee(g->zlinky));    // ZLinky en standard producteur
        }
    }
    if (!h || !energy_history_has_attr(h, 1)) return;
    int64_t sum = 0;
    for (int s = 0; s < ENERGY_HOURS; s++) {
        int32_t v = energy_history_slot_value(h, ENERGY_GRAN_HOUR, s, 1);
        if (v > 0) sum += v;
    }
    if (sum == 0) {
        if (!s_prod_zero_latched) {
            s_prod_zero_latched = true;
            notifications_add_alert("\u2600\uFE0F\u26A1Production nulle",
                                    "Pas de production photovolta\u00EFque sur 24H", NOTIF_WARNING,
                                    "prod_zero", 0, 0);
        }
    } else {
        s_prod_zero_latched = false;
    }
}

/** Controles a heure fixe (une fois par jour), comme alertChecksLoop(). */
static void check_scheduled(void)
{
    if (!ntp_sync_is_synced()) return;
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    int key = (t.tm_year + 1900) * 1000 + t.tm_yday;
    if (key != s_today_key) {
        if (s_today_key != -1) {
            ESP_LOGI(TAG, "Nouveau jour : controles de la veille");
            notif_alerts_daily();
        }
        s_today_key = key;
        s_anomaly_done = s_metrics_done = s_water_done = s_night_done = false;
    }
    if (t.tm_hour == 23 && t.tm_min >= 50) {
        if (!s_anomaly_done) { s_anomaly_done = true; check_daily_anomaly(); }
        if (!s_metrics_done) { s_metrics_done = true; send_daily_metrics(); }
    }
    if (t.tm_hour == 6 && t.tm_min <= 9) {
        if (!s_water_done) { s_water_done = true; check_water_leak(); }
        if (!s_night_done) { s_night_done = true; check_night_water_leak(); }
    }
}

void notif_alerts_periodic(void)
{
    check_over_power();                            // toutes les 10 s, comme la LiXee-Box
    int64_t now = esp_timer_get_time();
    if (s_last_minute == 0 || now - s_last_minute >= 60 * US_PER_S) {
        s_last_minute = now;
        check_freeze();
        check_prod_zero();
        check_scheduled();
    }
}
