// ============================================================================
// ZiGate Actions - voir zigate_actions.h
// ============================================================================

#include "zigate_actions.h"
#include "zigate_ir.h"
#include "zigate_devices.h"
#include "zigate_driver.h"
#include "zigate_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "ZIGATE_ACT";

// Numero de sequence Tuya, incremente a chaque trame (comme tuyaSeqNumber)
static uint16_t s_tuya_seq = 1;

bool zigate_action_is_setpoint(int command)
{
    return command == ZB_ACT_HVAC_SETPOINT_BOTH || command == ZB_ACT_HVAC_COOL_SETPOINT ||
           command == ZB_ACT_HVAC_HEAT_SETPOINT;
}

bool zigate_action_is_cover_position(int command)
{
    return command == ZB_ACT_COVER_POSITION || command == ZB_ACT_COVER_POSITION_INV;
}

// ----------------------------------------------------------------------------
// Types ZCL
// ----------------------------------------------------------------------------

int zigate_zcl_type_size(uint8_t t)
{
    if (t >= 0x08 && t <= 0x0F) return t - 0x07;          // data8 .. data64
    if (t == 0x10) return 1;                              // booleen
    if (t >= 0x18 && t <= 0x1F) return t - 0x17;          // bitmap8 .. bitmap64
    if (t >= 0x20 && t <= 0x27) return t - 0x1F;          // uint8 .. uint64
    if (t >= 0x28 && t <= 0x2F) return t - 0x27;          // int8 .. int64
    switch (t) {
    case 0x30: return 1;                                  // enum8
    case 0x31: return 2;                                  // enum16
    case 0x38: return 2;                                  // demi-precision
    case 0x39: return 4;                                  // flottant simple
    case 0x3A: return 8;                                  // flottant double
    case 0xE0: case 0xE1: case 0xE2: return 4;            // heure, date, UTC
    case 0xE8: case 0xE9: return 2;                       // id cluster / attribut
    case 0xEA: return 4;                                  // BACnet OID
    case 0xF0: return 8;                                  // IEEE
    default:   return 0;
    }
}

static bool zcl_type_signed(uint8_t t)
{
    return t >= 0x28 && t <= 0x2F;
}

int zigate_zcl_encode(uint8_t t, const char *text, uint8_t *out, size_t cap)
{
    if (!text || !out) return -1;
    while (*text == ' ') text++;

    // Chaines courtes : longueur sur un octet puis caracteres
    if (t == 0x41 || t == 0x42) {
        size_t n = strlen(text);
        if (n > 250 || n + 1 > cap) return -1;
        out[0] = (uint8_t)n;
        memcpy(out + 1, text, n);
        return (int)(n + 1);
    }

    int w = zigate_zcl_type_size(t);
    if (w <= 0 || (size_t)w > cap) return -1;

    if (t == 0x39) {                                      // flottant simple
        char tmp[32];
        strlcpy(tmp, text, sizeof(tmp));
        for (char *p = tmp; *p; p++) if (*p == ',') *p = '.';
        char *end;
        float f = strtof(tmp, &end);
        if (end == tmp) return -1;
        uint32_t bits;
        memcpy(&bits, &f, 4);
        for (int i = 0; i < 4; i++) out[i] = (uint8_t)(bits >> (24 - 8 * i));
        return 4;
    }
    if (t == 0x38 || t == 0x3A) return -1;                // non geres

    int64_t v;
    if (t == 0x10 && (!strcasecmp(text, "true") || !strcasecmp(text, "on"))) {
        v = 1;
    } else if (t == 0x10 && (!strcasecmp(text, "false") || !strcasecmp(text, "off"))) {
        v = 0;
    } else {
        char *end;
        errno = 0;
        long long ll = strtoll(text, &end, 0);            // decimal, ou hexa en "0x.."
        if (end == text || errno == ERANGE) return -1;
        while (*end == ' ') end++;
        if (*end) return -1;
        v = (int64_t)ll;
    }

    // Plage du type : refuser plutot que tronquer silencieusement
    if (w < 8) {
        int bits = 8 * w;
        if (zcl_type_signed(t)) {
            int64_t lo = -((int64_t)1 << (bits - 1)), hi = ((int64_t)1 << (bits - 1)) - 1;
            if (v < lo || v > hi) return -1;
        } else {
            if (v < 0 || v > (int64_t)(((uint64_t)1 << bits) - 1)) return -1;
        }
    }
    uint64_t u = (uint64_t)v;
    for (int i = 0; i < w; i++) out[i] = (uint8_t)(u >> (8 * (w - 1 - i)));
    return w;
}

// ----------------------------------------------------------------------------
// Tuya
// ----------------------------------------------------------------------------

esp_err_t zigate_tuya_dp_set(uint16_t short_addr, uint8_t endpoint, uint8_t dp,
                             uint8_t dp_type, uint32_t value)
{
    // Taille de la valeur selon le type, comme sendTuyaDatapointSet()
    uint8_t dlen = 1;
    if (dp_type == TUYA_DP_VALUE) dlen = 4;
    else if (dp_type == TUYA_DP_BITMAP) dlen = value > 0xFFFF ? 4 : (value > 0xFF ? 2 : 1);

    uint8_t zcl[16];
    int i = 0;
    zcl[i++] = 0x01;                                      // specifique cluster, client -> serveur
    zcl[i++] = (uint8_t)s_tuya_seq;                       // sequence ZCL
    zcl[i++] = 0x00;                                      // Datapoint Set
    zcl[i++] = (uint8_t)(s_tuya_seq >> 8);                // sequence Tuya (big-endian)
    zcl[i++] = (uint8_t)s_tuya_seq;
    s_tuya_seq++;
    zcl[i++] = dp;
    zcl[i++] = dp_type;
    zcl[i++] = 0x00;                                      // longueur (big-endian)
    zcl[i++] = dlen;
    for (int k = dlen - 1; k >= 0; k--) zcl[i++] = (uint8_t)(value >> (8 * k));

    ESP_LOGI(TAG, "Tuya DP%u (type %u) = %lu -> 0x%04X", dp, dp_type, (unsigned long)value, short_addr);
    return zigate_send_raw_aps(short_addr, 0x01, endpoint, 0xEF00, 0x0104, 0x1E, zcl, (uint8_t)i);
}

esp_err_t zigate_tuya_dp_query(uint16_t short_addr, uint8_t endpoint)
{
    uint8_t zcl[5];
    zcl[0] = 0x01;
    zcl[1] = (uint8_t)s_tuya_seq;
    zcl[2] = 0x03;                                        // Datapoint Query
    zcl[3] = (uint8_t)(s_tuya_seq >> 8);
    zcl[4] = (uint8_t)s_tuya_seq;
    s_tuya_seq++;
    return zigate_send_raw_aps(short_addr, 0x01, endpoint, 0xEF00, 0x0104, 0x1E, zcl, sizeof(zcl));
}

// ----------------------------------------------------------------------------
// Actions de template
// ----------------------------------------------------------------------------

static esp_err_t write_u(uint16_t sa, uint8_t ep, uint16_t cluster, uint16_t attr,
                         uint8_t type, int64_t v)
{
    uint8_t buf[8];
    int w = zigate_zcl_type_size(type);
    if (w <= 0 || w > 8) return ESP_ERR_INVALID_ARG;
    uint64_t u = (uint64_t)v;
    for (int i = 0; i < w; i++) buf[i] = (uint8_t)(u >> (8 * (w - 1 - i)));
    return zigate_write_attribute(sa, ep, cluster, attr, type, buf, (uint16_t)w);
}

// ----------------------------------------------------------------------------
// Consigne renvoyee apres un changement de mode de climatisation
// ----------------------------------------------------------------------------

#define SP_RESEND_DELAY_US  (2000 * 1000)   // le mode doit etre applique avant
#define SP_RESEND_SLOTS     4

typedef struct {
    esp_timer_handle_t timer;
    bool     busy;
    uint16_t sa;
    uint8_t  ep;
    uint16_t attr;
    int16_t  sp;
} sp_resend_t;

static zigate_setpoint_source_t s_sp_source;
static sp_resend_t s_sp[SP_RESEND_SLOTS];
static portMUX_TYPE s_sp_mux = portMUX_INITIALIZER_UNLOCKED;

void zigate_actions_set_setpoint_source(zigate_setpoint_source_t fn) { s_sp_source = fn; }

static void sp_resend_cb(void *arg)
{
    sp_resend_t *r = (sp_resend_t *)arg;
    esp_err_t err = write_u(r->sa, r->ep, 0x0201, r->attr, 0x29, r->sp);
    ESP_LOGI(TAG, "Consigne %s %d renvoyee a 0x%04X ep %d : %s", r->attr == 0x0012 ? "chaud" : "froid",
             r->sp, r->sa, r->ep, esp_err_to_name(err));
    portENTER_CRITICAL(&s_sp_mux);
    r->busy = false;
    portEXIT_CRITICAL(&s_sp_mux);
}

static void schedule_setpoint(uint16_t sa, uint8_t ep, int32_t mode)
{
    if (!s_sp_source || (mode != 3 && mode != 4)) return;     // 3 froid, 4 chauffage
    uint16_t attr = (mode == 4) ? 0x0012 : 0x0011;
    int16_t sp;
    if (!s_sp_source(sa, attr, &sp)) return;
    sp_resend_t *r = NULL;
    portENTER_CRITICAL(&s_sp_mux);
    for (int i = 0; i < SP_RESEND_SLOTS && !r; i++) {
        if (!s_sp[i].busy) { r = &s_sp[i]; r->busy = true; }
    }
    portEXIT_CRITICAL(&s_sp_mux);
    if (!r) return;
    r->sa = sa; r->ep = ep; r->attr = attr; r->sp = sp;
    if (!r->timer) {
        esp_timer_create_args_t a = {};
        a.callback = sp_resend_cb;
        a.arg = r;
        a.name = "hvac_sp";
        if (esp_timer_create(&a, &r->timer) != ESP_OK) { r->busy = false; return; }
    }
    if (esp_timer_start_once(r->timer, SP_RESEND_DELAY_US) != ESP_OK) r->busy = false;
}

esp_err_t zigate_action_send(int command, uint16_t short_addr, uint8_t endpoint,
                             int32_t value, uint16_t cluster, uint16_t manufacturer_code)
{
    return zigate_action_send_ex(command, short_addr, endpoint, value, cluster,
                                 manufacturer_code, NULL);
}

esp_err_t zigate_action_send_ex(int command, uint16_t short_addr, uint8_t endpoint,
                                int32_t value, uint16_t cluster, uint16_t manufacturer_code,
                                uint16_t *zigate_cmd_out)
{
    // Opcode ZiGate de la trame emise, rendu a l'appelant pour qu'il
    // reconnaisse SON accuse 0x8000 (champ "type du paquet") : les regles
    // prenaient jusqu'ici n'importe quel statut pour le leur.
    uint16_t opcode = 0;
    if (zigate_cmd_out) *zigate_cmd_out = 0;
    esp_err_t err;
    switch (command) {
    case ZB_ACT_ONOFF:
        if (value < 0 || value > 2) return ESP_ERR_INVALID_ARG;
        opcode = ZIGATE_CMD_ONOFF;
        err = zigate_send_onoff(short_addr, endpoint, (uint8_t)value);
        break;

    case ZB_ACT_COVER: {
        if (value < 0 || value > 2) return ESP_ERR_INVALID_ARG;
        opcode = ZIGATE_CMD_WINDOW_COVERING;
        err = zigate_send_window_covering(short_addr, endpoint, (uint8_t)value, NULL, 0);
        break;
    }

    case ZB_ACT_COVER_POSITION:
    case ZB_ACT_COVER_POSITION_INV: {
        // 'value' est TOUJOURS une ouverture (0 ferme, 100 ouvert) ; en ZCL la
        // position de levage mesure la FERMETURE, sauf appareils inverses.
        int open = value < 0 ? 0 : (value > 100 ? 100 : (int)value);
        uint8_t lift = (command == ZB_ACT_COVER_POSITION_INV) ? (uint8_t)open : (uint8_t)(100 - open);
        opcode = ZIGATE_CMD_WINDOW_COVERING;
        err = zigate_send_window_covering(short_addr, endpoint, 0x05, &lift, 1);   // Go To Lift Percentage
        break;
    }

    // Tuya : le champ endpoint du template porte le numero de DP, la trame
    // part vers l'endpoint 1 (convention de la LiXee-Box)
    case ZB_ACT_TUYA_BOOL:  opcode = ZIGATE_CMD_RAW_APS; err = zigate_tuya_dp_set(short_addr, 1, endpoint, TUYA_DP_BOOL,  (uint32_t)value); break;
    case ZB_ACT_TUYA_VALUE: opcode = ZIGATE_CMD_RAW_APS; err = zigate_tuya_dp_set(short_addr, 1, endpoint, TUYA_DP_VALUE, (uint32_t)value); break;
    case ZB_ACT_TUYA_ENUM:  opcode = ZIGATE_CMD_RAW_APS; err = zigate_tuya_dp_set(short_addr, 1, endpoint, TUYA_DP_ENUM,  (uint32_t)value); break;
    case ZB_ACT_TUYA_RAW:   opcode = ZIGATE_CMD_RAW_APS; err = zigate_tuya_dp_set(short_addr, 1, endpoint, TUYA_DP_RAW,   (uint32_t)value); break;
    case ZB_ACT_TUYA_QUERY: opcode = ZIGATE_CMD_RAW_APS; err = zigate_tuya_dp_query(short_addr, endpoint); break;

    // Climatisation (hvac.cpp) : ecritures d'attributs des clusters 0x0201 / 0x0202
    case ZB_ACT_HVAC_SYSTEM_MODE:
        opcode = ZIGATE_CMD_WRITE_ATTRIBUTE;
        err = write_u(short_addr, endpoint, 0x0201, 0x001C, 0x30, value & 0xFF);
        if (err == ESP_OK) schedule_setpoint(short_addr, endpoint, value);
        break;
    case ZB_ACT_HVAC_FAN_MODE:      opcode = ZIGATE_CMD_WRITE_ATTRIBUTE; err = write_u(short_addr, endpoint, 0x0202, 0x0000, 0x30, value & 0xFF); break;
    case ZB_ACT_HVAC_LOUVER:        opcode = ZIGATE_CMD_WRITE_ATTRIBUTE; err = write_u(short_addr, endpoint, 0x0201, 0x0045, 0x30, value & 0xFF); break;   // ACLouverPosition (0x0042 dans la LiXee-Box : ACRefrigerantType)
    case ZB_ACT_HVAC_COOL_SETPOINT: opcode = ZIGATE_CMD_WRITE_ATTRIBUTE; err = write_u(short_addr, endpoint, 0x0201, 0x0011, 0x29, (int16_t)value); break;
    case ZB_ACT_HVAC_HEAT_SETPOINT: opcode = ZIGATE_CMD_WRITE_ATTRIBUTE; err = write_u(short_addr, endpoint, 0x0201, 0x0012, 0x29, (int16_t)value); break;
    case ZB_ACT_HVAC_SETPOINT_BOTH:
        // Deux ecritures : l'accuse attendu est celui de la premiere
        opcode = ZIGATE_CMD_WRITE_ATTRIBUTE;
        err = write_u(short_addr, endpoint, 0x0201, 0x0011, 0x29, (int16_t)value);
        if (err == ESP_OK) err = write_u(short_addr, endpoint, 0x0201, 0x0012, 0x29, (int16_t)value);
        break;

    case ZB_ACT_IR_LEARN:
    case ZB_ACT_IR_SEND: {
        // Infrared.cpp de la LiXee-Box : 200 apprentissage, 201 emission
        const zigate_device_t *d = zigate_devices_find_by_short(short_addr);
        if (!d) return ESP_ERR_NOT_FOUND;
        opcode = ZIGATE_CMD_RAW_APS;
        err = (command == ZB_ACT_IR_LEARN) ? zigate_ir_learn(d->ieee_addr, value != 0)
                                           : zigate_ir_send_saved(d->ieee_addr, (int)value);
        break;
    }

    case ZB_ACT_CLUSTER_CMD: {
        // Commande 0x00 du cluster 'cluster' avec un octet de parametre (ex.
        // fil pilote NodOn, cluster 0xFC00, code 0x128B). Sans code fabricant,
        // trame specifique cluster ordinaire (la LiXee-Box posait toujours le
        // bit fabricant, meme avec un code nul).
        if (!cluster) return ESP_ERR_INVALID_ARG;
        uint8_t zcl[6];
        int i = 0;
        if (manufacturer_code) {
            zcl[i++] = 0x05;                              // specifique cluster + fabricant
            zcl[i++] = manufacturer_code & 0xFF;          // code fabricant little-endian (ZCL)
            zcl[i++] = (manufacturer_code >> 8) & 0xFF;
        } else {
            zcl[i++] = 0x01;
        }
        zcl[i++] = 0x00;                                  // sequence (remplacee par la ZiGate)
        zcl[i++] = 0x00;                                  // commande
        zcl[i++] = (uint8_t)value;
        opcode = ZIGATE_CMD_RAW_APS;
        err = zigate_send_raw_aps(short_addr, 0x01, endpoint, cluster, 0x0104, 0x00, zcl, (uint8_t)i);
        break;
    }

    default:
        ESP_LOGW(TAG, "Commande d'action %d non supportee", command);
        return ESP_ERR_NOT_SUPPORTED;
    }

    zigate_console_printf("Action %d -> 0x%04X ep %u valeur %ld : %s", command, short_addr,
                          endpoint, (long)value, err == ESP_OK ? "envoyee" : esp_err_to_name(err));
    if (err == ESP_OK && zigate_cmd_out) *zigate_cmd_out = opcode;
    return err;
}
