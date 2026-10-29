// ============================================================================
// Profil Matter d'un appareil - voir matter_profile.h
// ============================================================================

#include "matter_profile.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "MATTER_PROFILE";

// Numeros de commande des templates (zigate_actions.h)
#define CMD_ONOFF          146
#define CMD_COVER          250
#define CMD_COVER_POS      251
#define CMD_COVER_POS_INV  252
#define CMD_TUYA_BOOL      300
#define CMD_HVAC_MODE      310
#define CMD_HVAC_SP_BOTH   311
#define CMD_HVAC_FAN       312
#define CMD_HVAC_COOL_SP   314
#define CMD_HVAC_HEAT_SP   315

// ----------------------------------------------------------------------------
// Outils
// ----------------------------------------------------------------------------

static bool icontains(const char *s, const char *needle)
{
    if (!s || !needle) return false;
    size_t n = strlen(needle);
    for (; *s; s++) {
        if (strncasecmp(s, needle, n) == 0) return true;
    }
    return false;
}

static bool ieq(const char *a, const char *b) { return a && b && strcasecmp(a, b) == 0; }

// "°C" (UTF-8 ou Latin-1 selon l'editeur du template) : se termine par 'C',
// precede d'un octet non ASCII
static bool unit_is_celsius(const char *u)
{
    size_t n = u ? strlen(u) : 0;
    return n >= 2 && n <= 4 && u[n - 1] == 'C' && (unsigned char)u[n - 2] >= 0x80;
}

// Facteur de l'unite du template vers l'unite de base de la cible
static float unit_factor(const char *u)
{
    if (!u || !u[0]) return 1.0f;
    if (!strcmp(u, "mA") || !strcmp(u, "mV") || !strcmp(u, "mW")) return 0.001f;
    if (!strcmp(u, "kW") || !strcmp(u, "kVA") || !strcmp(u, "kWh") || !strcmp(u, "kV")) return 1000.0f;
    if (!strcmp(u, "kPa")) return 10.0f;     // -> hPa
    return 1.0f;
}

static bool unit_is_energy(const char *u)
{
    return !u || !u[0] || !strcmp(u, "Wh") || !strcmp(u, "kWh");
}

static bool is_onoff_action(const devdata_template_action_t *a)
{
    return a->command == CMD_ONOFF || a->command == CMD_TUYA_BOOL;
}

// Coefficient a utiliser : celui du template, sauf coefficient neutre sur un
// attribut ZCL standard dont l'echelle est fixee par la norme (un template
// qui n'indique pas 0.01 pour 0402 affiche la valeur brute, mais la valeur
// Matter doit rester juste).
static float spec_scale(uint16_t cluster, uint16_t attr, float coef)
{
    if (coef != 1.0f && coef != 0.0f) return coef;
    switch (cluster) {
    case 0x0402: case 0x0405: return 0.01f;
    case 0x0201: return (attr == 0x0000 || attr == 0x0011 || attr == 0x0012) ? 0.01f : 1.0f;
    case 0x0001: return attr == 0x0020 ? 0.1f : (attr == 0x0021 ? 0.5f : 1.0f);
    default: return coef == 0.0f ? 1.0f : coef;
    }
}

// ----------------------------------------------------------------------------
// Classification d'un etat
// ----------------------------------------------------------------------------

typedef struct {
    uint8_t  target;
    bool     invert;
    uint16_t energy_attr;
} cls_t;

static bool name_is_setpoint(const char *n)
{
    return icontains(n, "setpoint") || icontains(n, "consigne");
}

static bool name_is_cooling(const char *n)
{
    return icontains(n, "cool") || icontains(n, "froid");
}

static bool name_is_export(const char *n)
{
    return icontains(n, "reverse") || icontains(n, "inject") || icontains(n, "export") ||
           icontains(n, "produc");
}

// Clusters standard : liaison par attribut. *handled = true si le cluster est
// connu (ses autres attributs ne sont alors pas exposes).
static cls_t classify_standard(const devdata_template_state_t *s, bool has_onoff_action,
                               bool zlinky, matter_meter_role_t role, bool *handled)
{
    cls_t c = {};
    *handled = true;
    const uint16_t a = s->attribute;
    const char *dc = s->mqtt_device_class;

    switch (s->cluster) {
    case 0x0006:
        if (a == 0x0000) c.target = has_onoff_action ? ML_ONOFF : ML_SWITCH_PRESS;
        break;
    case 0x0402: if (a == 0x0000) c.target = ML_TEMPERATURE; break;
    case 0x0405: if (a == 0x0000) c.target = ML_HUMIDITY; break;
    case 0x0403: if (a == 0x0000) c.target = ML_PRESSURE; break;
    case 0x0400: if (a == 0x0000) c.target = ML_ILLUMINANCE_ZCL; break;
    case 0x0406: if (a == 0x0000) c.target = ML_OCCUPANCY; break;
    case 0x0500:
        // ZoneStatus, bit 0 (alarme 1) : sens selon la classe de l'etat
        if (a != 0x0002) break;
        if (ieq(dc, "moisture") || icontains(s->name, "leak") || icontains(s->name, "fuite")) {
            c.target = ML_LEAK;
        } else if (ieq(dc, "motion") || ieq(dc, "occupancy") || ieq(dc, "presence")) {
            c.target = ML_OCCUPANCY;
        } else if (!dc[0] || ieq(dc, "door") || ieq(dc, "window") || ieq(dc, "opening") ||
                   ieq(dc, "garage_door")) {
            c.target = ML_CONTACT;
            c.invert = true;
        }
        break;
    case 0x000F: if (a == 0x0055) c.target = ML_CONTACT; break;
    case 0x0012: if (a == 0x0055) c.target = ML_SWITCH_MULTI; break;
    case 0x0000: if (a == 0x8000) c.target = ML_SWITCH_MULTI; break;   // multi-clic Aqara
    case 0x0001:
        if (a == 0x0020) c.target = ML_BAT_VOLTAGE;
        else if (a == 0x0021) c.target = ML_BAT_PERCENT;
        break;
    case 0x0102: if (a == 0x0008) c.target = ML_COVER_LIFT; break;
    case 0x0201:
        if (a == 0x0000) c.target = ML_TH_LOCAL_TEMP;
        else if (a == 0x0011) c.target = ML_TH_COOL_SP;
        else if (a == 0x0012) c.target = ML_TH_HEAT_SP;
        else if (a == 0x001C) c.target = ML_TH_SYSTEM_MODE;
        else if (a == 0x0029) c.target = ML_TH_RUNNING;
        break;
    case 0x0202: if (a == 0x0000) c.target = ML_FAN_MODE; break;
    case 0x0702:
        if (a == 0x0000 || (a >= 0x0100 && a <= 0x0112 && !(a & 1))) {
            // Index : le role de la configuration Energie prime sur le template
            // (un ZiPulses declare en eau peut compter de l'electricite)
            bool elec = (role == MATTER_METER_ELECTRIC) ||
                        (role == MATTER_METER_AUTO && unit_is_energy(s->unit) &&
                         !ieq(dc, "water") && !ieq(dc, "gas"));
            if (elec) {
                c.target = ML_EL_ENERGY;
                c.energy_attr = a;
            }
        } else if (a == 0x0400) {
            c.target = ML_EL_ACTIVE_POWER;
        }
        break;
    case 0x0B04:
        if (a == 0x0505) c.target = ML_EL_VOLTAGE;
        else if (a == 0x0508) c.target = ML_EL_CURRENT;
        // ZLinky : 0x050B est CCASN (courbe de charge moyenne), pas une puissance
        else if (a == 0x050B && !zlinky) c.target = ML_EL_ACTIVE_POWER;
        else if (a == 0x050F) c.target = ML_EL_APPARENT_POWER;
        else if (a == 0x0306) c.target = ML_EL_TOTAL_APPARENT;
        break;
    case 0x0B01:        // Meter Identification (ZLinky)
    case 0xFF66:        // TIC du ZLinky : pas d'equivalent Matter
        break;
    default:
        *handled = false;
        break;
    }
    return c;
}

// Clusters fabricant et Analog Input : liaison par classe, unite ou nom
static cls_t classify_generic(const devdata_template_state_t *s)
{
    cls_t c = {};
    const char *dc = s->mqtt_device_class;
    const char *u = s->unit;
    const char *n = s->name;

    if (ieq(dc, "temperature") || (!dc[0] && unit_is_celsius(u))) {
        if (name_is_setpoint(n)) c.target = name_is_cooling(n) ? ML_TH_COOL_SP : ML_TH_HEAT_SP;
        else if (icontains(n, "local")) c.target = ML_TH_LOCAL_TEMP;
        else c.target = ML_TEMPERATURE;
    } else if (ieq(dc, "humidity")) {
        c.target = ML_HUMIDITY;
    } else if (ieq(dc, "pressure") || (!dc[0] && (!strcmp(u, "hPa") || !strcmp(u, "kPa")))) {
        c.target = ML_PRESSURE;
    } else if (ieq(dc, "illuminance") || (!dc[0] && (ieq(u, "lux") || ieq(u, "lx")))) {
        c.target = ML_ILLUMINANCE;
    } else if (ieq(dc, "occupancy") || ieq(dc, "motion") || ieq(dc, "presence") ||
               (!dc[0] && !u[0] && (icontains(n, "presence") || icontains(n, "occupancy")))) {
        c.target = ML_OCCUPANCY;
    } else if (ieq(dc, "moisture")) {
        c.target = ML_LEAK;
    } else if (ieq(dc, "door") || ieq(dc, "window") || ieq(dc, "garage_door")) {
        c.target = ML_CONTACT;
    } else if (ieq(dc, "battery")) {
        c.target = ML_BAT_PERCENT;
    } else if (ieq(dc, "voltage") || (!dc[0] && (!strcmp(u, "V") || !strcmp(u, "mV")))) {
        c.target = ML_EL_VOLTAGE;
    } else if (ieq(dc, "current") || (!dc[0] && (!strcmp(u, "A") || !strcmp(u, "mA")))) {
        c.target = ML_EL_CURRENT;
    } else if (ieq(dc, "power") || (!dc[0] && (!strcmp(u, "W") || !strcmp(u, "kW")))) {
        c.target = ML_EL_ACTIVE_POWER;
    } else if (ieq(dc, "apparent_power") || (!dc[0] && (!strcmp(u, "VA") || !strcmp(u, "kVA")))) {
        c.target = ML_EL_APPARENT_POWER;
    } else if ((ieq(dc, "energy") || (!dc[0] && (!strcmp(u, "Wh") || !strcmp(u, "kWh")))) &&
               !name_is_export(n)) {
        c.target = ML_EL_ENERGY;
        c.energy_attr = 0x0000;
    }

    // Etat inscriptible : un reglage (calibration, seuil, temporisation...),
    // sauf une consigne de thermostat
    if (s->writable && c.target != ML_TH_HEAT_SP && c.target != ML_TH_COOL_SP) c.target = ML_NONE;
    return c;
}

// ----------------------------------------------------------------------------
// Actions du template
// ----------------------------------------------------------------------------

static void set_cmd(matter_cmd_t *cmd, const devdata_template_action_t *a)
{
    if (cmd->valid) return;     // premiere action retenue (appareil multi-voies : voie 1)
    cmd->valid = true;
    cmd->command = a->command;
    cmd->endpoint = a->endpoint;
    cmd->value = a->value;
    cmd->cluster = a->cluster;
    cmd->mfr = a->manufacturer_code;
}

static void scan_actions(const devdata_template_t *t, matter_profile_t *p)
{
    for (int i = 0; i < t->action_count; i++) {
        const devdata_template_action_t *a = &t->actions[i];
        switch (a->command) {
        case CMD_ONOFF:
        case CMD_TUYA_BOOL:
            if (a->value == 1) set_cmd(&p->on, a);
            else if (a->value == 0) set_cmd(&p->off, a);
            break;
        case CMD_COVER: {
            // Valeurs propres a chaque template (TS130F : 1 = monter) : le nom fait foi
            const char *n = a->name;
            if (icontains(n, "stop")) set_cmd(&p->cover_stop, a);
            else if (icontains(n, "up") || icontains(n, "ouvr") || icontains(n, "mont") ||
                     icontains(n, "open")) set_cmd(&p->cover_up, a);
            else if (icontains(n, "down") || icontains(n, "ferm") || icontains(n, "desc") ||
                     icontains(n, "clos")) set_cmd(&p->cover_down, a);
            break;
        }
        case CMD_COVER_POS:
        case CMD_COVER_POS_INV:
            set_cmd(&p->cover_pos, a);
            p->cover_inverted = (a->command == CMD_COVER_POS_INV);
            break;
        case CMD_HVAC_MODE:    set_cmd(&p->sys_mode, a); break;
        case CMD_HVAC_SP_BOTH: set_cmd(&p->both_sp, a); break;
        case CMD_HVAC_COOL_SP: set_cmd(&p->cool_sp, a); break;
        case CMD_HVAC_HEAT_SP: set_cmd(&p->heat_sp, a); break;
        case CMD_HVAC_FAN:     set_cmd(&p->fan_mode, a); break;
        default: break;
        }
    }
}

// ----------------------------------------------------------------------------
// Construction
// ----------------------------------------------------------------------------

static uint32_t cap_of(uint8_t target)
{
    switch (target) {
    case ML_ONOFF:             return MCAP_ONOFF;
    case ML_TEMPERATURE:       return MCAP_TEMPERATURE;
    case ML_HUMIDITY:          return MCAP_HUMIDITY;
    case ML_PRESSURE:          return MCAP_PRESSURE;
    case ML_ILLUMINANCE:
    case ML_ILLUMINANCE_ZCL:   return MCAP_ILLUMINANCE;
    case ML_OCCUPANCY:         return MCAP_OCCUPANCY;
    case ML_CONTACT:           return MCAP_CONTACT;
    case ML_LEAK:              return MCAP_LEAK;
    case ML_BAT_PERCENT:
    case ML_BAT_VOLTAGE:       return MCAP_BATTERY;
    case ML_COVER_LIFT:        return MCAP_COVER;
    case ML_TH_LOCAL_TEMP:
    case ML_TH_HEAT_SP:
    case ML_TH_COOL_SP:
    case ML_TH_SYSTEM_MODE:
    case ML_TH_RUNNING:        return MCAP_THERMOSTAT;
    case ML_FAN_MODE:          return MCAP_FAN;
    case ML_EL_ACTIVE_POWER:
    case ML_EL_APPARENT_POWER:
    case ML_EL_TOTAL_APPARENT:
    case ML_EL_VOLTAGE:
    case ML_EL_CURRENT:        return MCAP_ELECTRICAL;
    case ML_EL_ENERGY:         return MCAP_ENERGY;
    case ML_SWITCH_PRESS:
    case ML_SWITCH_MULTI:      return MCAP_SWITCH;
    default:                   return 0;
    }
}

static bool has_target(const matter_profile_t *p, uint8_t target)
{
    for (int i = 0; i < p->link_count; i++)
        if (p->links[i].target == target) return true;
    return false;
}

static void retarget(matter_profile_t *p, uint8_t from, uint8_t to)
{
    for (int i = 0; i < p->link_count; i++)
        if (p->links[i].target == from) p->links[i].target = to;
}

static void drop_target(matter_profile_t *p, uint8_t target)
{
    int j = 0;
    for (int i = 0; i < p->link_count; i++)
        if (p->links[i].target != target) p->links[j++] = p->links[i];
    p->link_count = j;
}

matter_profile_t *matter_profile_build(const devdata_template_t *tmpl, const char *model,
                                       uint16_t zcl_device_id, matter_meter_role_t meter_role)
{
    if (!tmpl || !tmpl->loaded || tmpl->dyn_only) return NULL;   // dyn_only : codes IR seuls

    matter_profile_t *p = (matter_profile_t *)heap_caps_calloc(1, sizeof(*p),
                                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = (matter_profile_t *)calloc(1, sizeof(*p));
    if (!p) return NULL;

    p->zlinky = model && strncmp(model, "ZLinky", 6) == 0;
    scan_actions(tmpl, p);
    bool has_onoff_action = false;
    for (int i = 0; i < tmpl->action_count; i++)
        if (is_onoff_action(&tmpl->actions[i])) has_onoff_action = true;

    for (int i = 0; i < tmpl->state_count; i++) {
        const devdata_template_state_t *s = &tmpl->states[i];
        bool handled = false;
        cls_t c = classify_standard(s, has_onoff_action, p->zlinky, meter_role, &handled);
        if (!handled) c = classify_generic(s);
        if (c.target == ML_NONE) continue;

        // Un meme attribut n'est lie qu'une fois (deux voies "On/Off L1/L2"
        // partagent 0006/0000 : device_data ne les distingue pas)
        bool dup = false;
        for (int k = 0; k < p->link_count; k++)
            if (p->links[k].cluster == s->cluster && p->links[k].attr == s->attribute) dup = true;
        if (dup) continue;
        if (p->link_count >= MATTER_PROFILE_MAX_LINKS) {
            ESP_LOGW(TAG, "Plus de %d liaisons : '%s' ignore", MATTER_PROFILE_MAX_LINKS, s->name);
            continue;
        }

        matter_link_t *l = &p->links[p->link_count++];
        l->cluster = s->cluster;
        l->attr = s->attribute;
        l->target = c.target;
        l->invert = c.invert;
        l->energy_attr = c.energy_attr;
        l->writable = s->writable;
        l->write_type = (uint8_t)(s->type_writable > 0 ? s->type_writable : 0);
        l->mfr = s->manufacturer_code;
        l->scale = spec_scale(s->cluster, s->attribute, s->coefficient);
        l->unit_mul = (c.target == ML_EL_ENERGY && !unit_is_energy(s->unit)) ? 1.0f : unit_factor(s->unit);
    }

    // Coherence de l'endpoint :
    //  - un thermostat suppose une consigne ; sinon la temperature locale est
    //    un simple capteur ;
    //  - une seule fonction BooleanState par endpoint : la fuite l'emporte ;
    //  - un bouton qui remonte le nombre d'appuis ne compte pas deux fois
    p->heat = has_target(p, ML_TH_HEAT_SP);
    p->cool = has_target(p, ML_TH_COOL_SP);
    if (!p->heat && !p->cool) {
        retarget(p, ML_TH_LOCAL_TEMP, ML_TEMPERATURE);
        drop_target(p, ML_TH_SYSTEM_MODE);
        drop_target(p, ML_TH_RUNNING);
    }
    if (has_target(p, ML_LEAK)) drop_target(p, ML_CONTACT);
    if (has_target(p, ML_SWITCH_MULTI)) drop_target(p, ML_SWITCH_PRESS);

    for (int i = 0; i < p->link_count; i++) p->caps |= cap_of(p->links[i].target);

    // Lampe : DeviceId d'eclairage ZCL (le ZLinky s'annonce a tort en 0x0101)
    p->light = (p->caps & MCAP_ONOFF) && !p->zlinky &&
               (zcl_device_id == 0x0100 || zcl_device_id == 0x0101 || zcl_device_id == 0x0102 ||
                zcl_device_id == 0x010C || zcl_device_id == 0x010D);

    if (!p->caps) {
        free(p);
        return NULL;
    }
    ESP_LOGI(TAG, "Profil '%s' : %d liaison(s), capacites 0x%04lX", model ? model : "",
             p->link_count, (unsigned long)p->caps);
    for (int i = 0; i < p->link_count; i++) {
        ESP_LOGI(TAG, "  %04X/%04X -> %s (x%g)", p->links[i].cluster, p->links[i].attr,
                 matter_link_target_name(p->links[i].target),
                 (double)(p->links[i].scale * p->links[i].unit_mul));
    }
    return p;
}

// ----------------------------------------------------------------------------
// Valeurs
// ----------------------------------------------------------------------------

bool matter_profile_raw_value(const uint8_t *data, uint16_t len, uint8_t t, double *out)
{
    if (!data || !out || len == 0) return false;
    if (t == 0x39 && len >= 4) {                       // flottant simple precision
        uint32_t u = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                     ((uint32_t)data[2] << 8) | data[3];
        float f;
        memcpy(&f, &u, sizeof(f));
        if (isnan(f) || isinf(f)) return false;
        *out = f;
        return true;
    }
    if (t == 0x41 || t == 0x42 || t == 0x43 || t == 0x44) return false;   // chaines
    int n = len > 8 ? 8 : len;
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v = (v << 8) | data[i];
    if (t >= 0x28 && t <= 0x2F && n < 8) {             // entier signe : extension du signe
        uint64_t sign = 1ULL << (n * 8 - 1);
        if (v & sign) v |= ~((sign << 1) - 1);
    }
    *out = (t >= 0x28 && t <= 0x2F) ? (double)(int64_t)v : (double)v;
    return true;
}

const char *matter_link_target_name(uint8_t t)
{
    switch (t) {
    case ML_ONOFF:             return "OnOff";
    case ML_TEMPERATURE:       return "Temperature";
    case ML_HUMIDITY:          return "Humidite";
    case ML_PRESSURE:          return "Pression";
    case ML_ILLUMINANCE:
    case ML_ILLUMINANCE_ZCL:   return "Eclairement";
    case ML_OCCUPANCY:         return "Occupation";
    case ML_CONTACT:           return "Contact";
    case ML_LEAK:              return "Fuite";
    case ML_BAT_PERCENT:       return "Batterie %";
    case ML_BAT_VOLTAGE:       return "Batterie V";
    case ML_COVER_LIFT:        return "Volet";
    case ML_TH_LOCAL_TEMP:     return "Thermostat temp";
    case ML_TH_HEAT_SP:        return "Consigne chaud";
    case ML_TH_COOL_SP:        return "Consigne froid";
    case ML_TH_SYSTEM_MODE:    return "Mode thermostat";
    case ML_TH_RUNNING:        return "Etat thermostat";
    case ML_FAN_MODE:          return "Ventilation";
    case ML_EL_ACTIVE_POWER:   return "Puissance active";
    case ML_EL_APPARENT_POWER: return "Puissance apparente";
    case ML_EL_TOTAL_APPARENT: return "Puissance apparente totale";
    case ML_EL_VOLTAGE:        return "Tension";
    case ML_EL_CURRENT:        return "Courant";
    case ML_EL_ENERGY:         return "Energie";
    case ML_SWITCH_PRESS:      return "Bouton";
    case ML_SWITCH_MULTI:      return "Bouton multi";
    default:                   return "?";
    }
}
