// ============================================================================
// Cluster Handlers - Traitement des rapports d'attributs Zigbee par cluster
//
// Port des handlers Arduino (zigbee.cpp, temperature.cpp, ElectricalMeasurement.cpp,
// SimpleMeter.cpp, etc.) vers ESP-IDF.
//
// Chaque handler recoit les bytes bruts, les interprete, et stocke la valeur
// interpretee dans device_data. Les valeurs brutes hex sont deja stockees
// par main.cpp AVANT l'appel au handler.
// ============================================================================

#include "cluster_handlers.h"
#include "device_data.h"
#include "config_manager.h"
#include "notifications.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "esp_log.h"

static const char *TAG = "CLUSTER";

// ============================================================================
// Helpers
// ============================================================================

/**
 * Convertit les bytes bruts en hex string (format Arduino).
 */
static void bytes_to_hex(const uint8_t *data, int len, char *out, size_t out_size)
{
    int pos = 0;
    for (int i = 0; i < len && pos < (int)(out_size - 3); i++) {
        pos += snprintf(out + pos, out_size - pos, "%02X", data[i]);
    }
    out[pos] = '\0';
}

/**
 * Interprete les bytes bruts selon le type ZCL et retourne une valeur numerique.
 * Les donnees ZiGate sont en big-endian.
 */
static float interpret_numeric(const uint8_t *data, int len, uint8_t zcl_type)
{
    switch (zcl_type) {
    case ZCL_BOOL:
    case ZCL_UINT8:
    case ZCL_ENUM8:
        return (len >= 1) ? (float)data[0] : 0;

    case ZCL_UINT16:
    case ZCL_ENUM16:
        return (len >= 2) ? (float)zcl_uint16(data) : 0;

    case ZCL_INT16:
        return (len >= 2) ? (float)zcl_int16(data) : 0;

    case ZCL_UINT24:
        return (len >= 3) ? (float)zcl_uint24(data) : 0;

    case ZCL_UINT32:
        return (len >= 4) ? (float)zcl_uint32(data) : 0;

    case ZCL_INT8:
        return (len >= 1) ? (float)(int8_t)data[0] : 0;

    case ZCL_INT32:
        return (len >= 4) ? (float)(int32_t)zcl_uint32(data) : 0;

    case ZCL_UINT48:
        if (len >= 6) {
            uint64_t v = 0;
            for (int i = 0; i < 6; i++) v = (v << 8) | data[i];
            return (float)v;
        }
        return 0;

    default:
        // Fallback: traiter comme un entier big-endian
        if (len == 1) return (float)data[0];
        if (len == 2) return (float)zcl_uint16(data);
        if (len == 4) return (float)zcl_uint32(data);
        return 0;
    }
}

/**
 * Interprete les bytes bruts comme string (ZCL_STRING).
 */
static void interpret_string(const uint8_t *data, int len, char *out, size_t out_size)
{
    int copy_len = (len < (int)(out_size - 1)) ? len : (int)(out_size - 1);
    memcpy(out, data, copy_len);
    out[copy_len] = '\0';
}

// ============================================================================
// Handler: Basic Cluster (0x0000)
// Attributs: 0x0001 (HW version), 0x0004 (Manufacturer), 0x0005 (Model)
//            0x4000 (SW version ZLinky)
// ============================================================================

static esp_err_t handle_basic(const cluster_report_t *report, cluster_interpreted_t *out)
{
    switch (report->attr_id) {
    case 0x0001: // Application Version
        if (out && report->data_len >= 1) {
            out->has_float = true;
            out->float_value = (float)report->data[0];
            snprintf(out->str_value, sizeof(out->str_value), "%d", report->data[0]);
            out->has_string = true;
        }
        break;

    case 0x0004: // Manufacturer Name
    case 0x0005: // Model Identifier
        if (out && report->data_len > 0) {
            interpret_string(report->data, report->data_len, out->str_value, sizeof(out->str_value));
            out->has_string = true;
        }
        break;

    case 0x4000: // SW Build ID (ZLinky utilise cet attribut)
        if (out && report->data_len > 0) {
            interpret_string(report->data, report->data_len, out->str_value, sizeof(out->str_value));
            out->has_string = true;
        }
        break;

    default:
        break;
    }

    ESP_LOGD(TAG, "Basic: attr=0x%04X", report->attr_id);
    return ESP_OK;
}

// ============================================================================
// Handler: Power Configuration (0x0001)
// Attribut 0x0020 (BatteryVoltage), 0x0021 (BatteryPercentageRemaining)
// ============================================================================

static esp_err_t handle_power_config(const cluster_report_t *report, cluster_interpreted_t *out)
{
    float raw = interpret_numeric(report->data, report->data_len, report->attr_type);
    float coeff = device_data_get_coefficient(report->ieee_addr, 0x0001, report->attr_id);
    float value = raw * coeff;

    if (out) {
        out->has_float = true;
        out->float_value = value;

        if (report->attr_id == 0x0020) {
            // Battery voltage en 0.1V
            out->float_value = value / 10.0f;
            strncpy(out->unit, "V", sizeof(out->unit));
        } else if (report->attr_id == 0x0021) {
            // Battery % remaining (0-200 = 0-100%)
            out->float_value = value / 2.0f;
            strncpy(out->unit, "%", sizeof(out->unit));
        }
    }

    ESP_LOGD(TAG, "Power: attr=0x%04X raw=%.0f coeff=%.2f val=%.1f",
             report->attr_id, raw, coeff, value);
    return ESP_OK;
}

// ============================================================================
// Handler: On/Off (0x0006)
// Attribut 0x0000 (OnOff state)
// ============================================================================

static esp_err_t handle_onoff(const cluster_report_t *report, cluster_interpreted_t *out)
{
    float raw = interpret_numeric(report->data, report->data_len, report->attr_type);

    if (out) {
        out->has_float = true;
        out->float_value = raw;
        out->has_string = true;
        snprintf(out->str_value, sizeof(out->str_value), "%s", raw > 0 ? "ON" : "OFF");
    }

    ESP_LOGD(TAG, "OnOff: attr=0x%04X state=%s", report->attr_id, raw > 0 ? "ON" : "OFF");
    return ESP_OK;
}

// ============================================================================
// Handler: Level Control (0x0008)
// Attribut 0x0000 (CurrentLevel, 0-254)
// ============================================================================

static esp_err_t handle_level(const cluster_report_t *report, cluster_interpreted_t *out)
{
    float raw = interpret_numeric(report->data, report->data_len, report->attr_type);

    if (out) {
        out->has_float = true;
        out->float_value = raw;
        strncpy(out->unit, "", sizeof(out->unit));
    }

    ESP_LOGD(TAG, "Level: attr=0x%04X level=%.0f", report->attr_id, raw);
    return ESP_OK;
}

// ============================================================================
// Handler: Door Lock (0x0101)
// ============================================================================

static esp_err_t handle_doorlock(const cluster_report_t *report, cluster_interpreted_t *out)
{
    float raw = interpret_numeric(report->data, report->data_len, report->attr_type);

    if (out) {
        out->has_float = true;
        out->float_value = raw;
        if (report->attr_id == 0x0000) {
            out->has_string = true;
            snprintf(out->str_value, sizeof(out->str_value), "%s",
                     raw == 1 ? "Locked" : "Unlocked");
        }
    }

    return ESP_OK;
}

// ============================================================================
// Handler: Window Covering (0x0102)
// ============================================================================

static esp_err_t handle_window_covering(const cluster_report_t *report, cluster_interpreted_t *out)
{
    float raw = interpret_numeric(report->data, report->data_len, report->attr_type);

    if (out) {
        out->has_float = true;
        out->float_value = raw;
        if (report->attr_id == 0x0008) {
            strncpy(out->unit, "%", sizeof(out->unit));
        }
    }

    return ESP_OK;
}

// ============================================================================
// Handler: Thermostat (0x0201)
// Attributs: 0x0000 (LocalTemperature), 0x0011 (CoolingSetpoint),
//            0x0012 (HeatingSetpoint), 0x001C (SystemMode)
// ============================================================================

static esp_err_t handle_thermostat(const cluster_report_t *report, cluster_interpreted_t *out)
{
    float raw = interpret_numeric(report->data, report->data_len, report->attr_type);
    float coeff = device_data_get_coefficient(report->ieee_addr, 0x0201, report->attr_id);

    if (out) {
        out->has_float = true;

        switch (report->attr_id) {
        case 0x0000: // LocalTemperature (0.01 degC)
        case 0x0011: // OccupiedCoolingSetpoint
        case 0x0012: // OccupiedHeatingSetpoint
            out->float_value = (raw * coeff) / 100.0f;
            strncpy(out->unit, "\xC2\xB0""C", sizeof(out->unit)); // °C UTF-8
            break;
        case 0x001C: // SystemMode
            out->float_value = raw;
            out->has_string = true;
            switch ((int)raw) {
            case 0: strncpy(out->str_value, "Off", sizeof(out->str_value)); break;
            case 1: strncpy(out->str_value, "Auto", sizeof(out->str_value)); break;
            case 3: strncpy(out->str_value, "Cool", sizeof(out->str_value)); break;
            case 4: strncpy(out->str_value, "Heat", sizeof(out->str_value)); break;
            default: snprintf(out->str_value, sizeof(out->str_value), "Mode %d", (int)raw); break;
            }
            break;
        default:
            out->float_value = raw * coeff;
            break;
        }
    }

    ESP_LOGD(TAG, "Thermostat: attr=0x%04X raw=%.0f", report->attr_id, raw);
    return ESP_OK;
}

// ============================================================================
// Handler: Temperature Measurement (0x0402)
// Attribut 0x0000 (MeasuredValue, int16 en 0.01 degC)
// ============================================================================

static esp_err_t handle_temperature(const cluster_report_t *report, cluster_interpreted_t *out)
{
    float raw = interpret_numeric(report->data, report->data_len, report->attr_type);
    float coeff = device_data_get_coefficient(report->ieee_addr, 0x0402, report->attr_id);
    float temp_c = (raw * coeff) / 100.0f;

    if (out) {
        out->has_float = true;
        out->float_value = temp_c;
        strncpy(out->unit, "\xC2\xB0""C", sizeof(out->unit)); // °C UTF-8
    }

    ESP_LOGI(TAG, "Temperature: %.2f C (raw=%.0f coeff=%.2f)", temp_c, raw, coeff);
    return ESP_OK;
}

// ============================================================================
// Handler: Relative Humidity (0x0405)
// Attribut 0x0000 (MeasuredValue, uint16 en 0.01 %)
// ============================================================================

static esp_err_t handle_humidity(const cluster_report_t *report, cluster_interpreted_t *out)
{
    float raw = interpret_numeric(report->data, report->data_len, report->attr_type);
    float coeff = device_data_get_coefficient(report->ieee_addr, 0x0405, report->attr_id);
    float humidity = (raw * coeff) / 100.0f;

    if (out) {
        out->has_float = true;
        out->float_value = humidity;
        strncpy(out->unit, "%", sizeof(out->unit));
    }

    ESP_LOGI(TAG, "Humidity: %.1f %% (raw=%.0f)", humidity, raw);
    return ESP_OK;
}

// ============================================================================
// Handler: Occupancy Sensing (0x0406)
// Attribut 0x0000 (Occupancy bitmap)
// ============================================================================

static esp_err_t handle_occupancy(const cluster_report_t *report, cluster_interpreted_t *out)
{
    float raw = interpret_numeric(report->data, report->data_len, report->attr_type);
    bool occupied = (raw > 0);

    if (out) {
        out->has_float = true;
        out->float_value = occupied ? 1.0f : 0.0f;
        out->has_string = true;
        strncpy(out->str_value, occupied ? "Occupied" : "Unoccupied", sizeof(out->str_value));
    }

    // Verifier si c'est le capteur de presence configure
    const cfg_general_t *g = cfg_get_general();
    if (g->presence[0] != '\0') {
        char ieee_str[20];
        snprintf(ieee_str, sizeof(ieee_str), "%016llX", (unsigned long long)report->ieee_addr);
        if (strstr(ieee_str, g->presence) != NULL) {
            ESP_LOGI(TAG, "Presence sensor: %s", occupied ? "OCCUPIED" : "EMPTY");
        }
    }

    ESP_LOGD(TAG, "Occupancy: %s (raw=%.0f)", occupied ? "OCC" : "UNOCC", raw);
    return ESP_OK;
}

// ============================================================================
// Handler: Simple Metering (0x0702) - Energie
// Attributs: 0x0000 (CurrentSummationDelivered), 0x0001 (CurrentSummationReceived)
//            0x0100..0x010A (tarif indices)
// ============================================================================

static esp_err_t handle_simple_meter(const cluster_report_t *report, cluster_interpreted_t *out)
{
    float raw = interpret_numeric(report->data, report->data_len, report->attr_type);
    float coeff = device_data_get_coefficient(report->ieee_addr, 0x0702, report->attr_id);
    float value = raw * coeff;

    if (out) {
        out->has_float = true;
        out->float_value = value;
        strncpy(out->unit, "Wh", sizeof(out->unit));
    }

    ESP_LOGD(TAG, "SimpleMeter: attr=0x%04X val=%.0f Wh", report->attr_id, value);
    return ESP_OK;
}

// ============================================================================
// Handler: Electrical Measurement (0x0B04) - Puissance, Tension, Courant
// Attributs clefs:
//   0x0505 (RMS Voltage)         - Phase 1
//   0x0508 (RMS Current)         - Phase 1
//   0x050B (Active Power)        - Phase 1
//   0x050F (Apparent Power)      - Phase 1
//   0x0905/0x0A05 (RMS Voltage)  - Phase 2/3
//   0x0906/0x0A06 (RMS Current)  - Phase 2/3
//   0x090B/0x0A0B (Active Power) - Phase 2/3
// ============================================================================

static esp_err_t handle_electrical_measurement(const cluster_report_t *report, cluster_interpreted_t *out)
{
    float raw = interpret_numeric(report->data, report->data_len, report->attr_type);
    float coeff = device_data_get_coefficient(report->ieee_addr, 0x0B04, report->attr_id);
    float value = raw * coeff;

    if (out) {
        out->has_float = true;
        out->float_value = value;

        // Determiner l'unite selon l'attribut
        switch (report->attr_id) {
        case 0x0505: case 0x0905: case 0x0A05:  // RMS Voltage
            strncpy(out->unit, "V", sizeof(out->unit));
            break;
        case 0x0508: case 0x0906: case 0x0A06:  // RMS Current (mA)
            strncpy(out->unit, "mA", sizeof(out->unit));
            break;
        case 0x050B: case 0x090B: case 0x0A0B:  // Active Power
            strncpy(out->unit, "W", sizeof(out->unit));
            break;
        case 0x050F: case 0x090F: case 0x0A0F:  // Apparent Power
            strncpy(out->unit, "VA", sizeof(out->unit));
            break;
        case 0x0510: case 0x0910:  // Power Factor
            strncpy(out->unit, "", sizeof(out->unit));
            break;
        default:
            break;
        }
    }

    // Surtension / sous-tension : alertes levees par le composant
    // notifications (rapport transmis par cluster_handler_dispatch), avec
    // memorisation par phase pour n'alerter qu'au franchissement du seuil.
    // Ce handler ne faisait que journaliser, a chaque mesure hors seuil.

    ESP_LOGD(TAG, "ElecMeas: attr=0x%04X val=%.1f%s",
             report->attr_id, value, out ? out->unit : "");
    return ESP_OK;
}

// ============================================================================
// Handler: LiXee Custom Cluster (0xFF66) - ZLinky
// Attributs clefs:
//   0x0000 (OPTARIF/NGTF - type abonnement)
//   0x0005 (Surconsommation alert)
//   0x0010 (Current tariff period)
//   0x0200 (Current tariff)
//   0x0202 (Price change notification)
//   0x0300 (Linky mode)
// ============================================================================

static esp_err_t handle_lixee_custom(const cluster_report_t *report, cluster_interpreted_t *out)
{
    // Les attributs string
    if (report->attr_type == ZCL_STRING || report->attr_type == ZCL_LONG_STRING) {
        if (out) {
            interpret_string(report->data, report->data_len, out->str_value, sizeof(out->str_value));
            out->has_string = true;
        }
        ESP_LOGD(TAG, "LiXee: attr=0x%04X str='%.*s'",
                 report->attr_id, report->data_len, report->data);
        return ESP_OK;
    }

    // Les attributs numeriques
    float raw = interpret_numeric(report->data, report->data_len, report->attr_type);

    if (out) {
        out->has_float = true;
        out->float_value = raw;

        switch (report->attr_id) {
        case 0x0300: // Linky mode
            out->has_string = true;
            snprintf(out->str_value, sizeof(out->str_value), "mode%d", (int)raw);
            // Mettre a jour le mode Linky dans device_data info
            {
                device_data_t *dd = device_data_find(report->ieee_addr);
                if (dd) {
                    snprintf(dd->info.linky_mode, sizeof(dd->info.linky_mode), "%d", (int)raw);
                }
            }
            break;

        case 0x0005: // Surconsommation (ADPS) : notification levee par le composant notifications
            if (raw > 0) {
                ESP_LOGW(TAG, "SURCONSOMMATION (ADPS=%.0f A)", raw);
            }
            break;

        default:
            break;
        }
    }

    ESP_LOGD(TAG, "LiXee: attr=0x%04X val=%.0f", report->attr_id, raw);
    return ESP_OK;
}

// ============================================================================
// Handler par defaut (clusters non geres)
// ============================================================================

static esp_err_t handle_default(const cluster_report_t *report, cluster_interpreted_t *out)
{
    float raw = interpret_numeric(report->data, report->data_len, report->attr_type);

    if (out) {
        out->has_float = true;
        out->float_value = raw;
    }

    ESP_LOGD(TAG, "Default: cluster=0x%04X attr=0x%04X raw=%.0f",
             report->cluster_id, report->attr_id, raw);
    return ESP_OK;
}

// ============================================================================
// API publique
// ============================================================================

esp_err_t cluster_handlers_init(void)
{
    ESP_LOGI(TAG, "Cluster handlers initialized (13 clusters)");
    return ESP_OK;
}

/**
 * Transmet le rapport au composant notifications (alertes ZLinky, WebPush),
 * avec la valeur decodee : entier exact selon le type ZCL, ou texte.
 * L'appel est non bloquant (mise en file pour la tache "notif").
 */
static void report_to_notifications(const cluster_report_t *r)
{
    bool is_str = (r->attr_type == ZCL_STRING || r->attr_type == ZCL_LONG_STRING);
    int64_t num = 0;
    char str[48];
    str[0] = '\0';
    if (is_str) {
        interpret_string(r->data, r->data_len, str, sizeof(str));
    } else if (!zcl_to_int64(r->data, r->data_len, r->attr_type, &num)) {
        return;                         // donnees trop courtes pour le type annonce
    }
    notifications_on_report(r->ieee_addr, r->cluster_id, r->attr_id, is_str, num, str);
}

esp_err_t cluster_handler_dispatch(const cluster_report_t *report,
                                    cluster_interpreted_t *out)
{
    // Initialiser la sortie
    if (out) {
        memset(out, 0, sizeof(cluster_interpreted_t));
    }

    esp_err_t ret;
    switch (report->cluster_id) {
    case 0x0000: ret = handle_basic(report, out); break;
    case 0x0001: ret = handle_power_config(report, out); break;
    case 0x0006: ret = handle_onoff(report, out); break;
    case 0x0008: ret = handle_level(report, out); break;
    case 0x0101: ret = handle_doorlock(report, out); break;
    case 0x0102: ret = handle_window_covering(report, out); break;
    case 0x0201: ret = handle_thermostat(report, out); break;
    case 0x0402: ret = handle_temperature(report, out); break;
    case 0x0405: ret = handle_humidity(report, out); break;
    case 0x0406: ret = handle_occupancy(report, out); break;
    case 0x0702: ret = handle_simple_meter(report, out); break;
    case 0x0B04: ret = handle_electrical_measurement(report, out); break;
    case 0xFF66: ret = handle_lixee_custom(report, out); break;
    default:     ret = handle_default(report, out); break;
    }

    // Alertes et WebPush (la valeur brute est deja dans device_data)
    report_to_notifications(report);
    return ret;
}
