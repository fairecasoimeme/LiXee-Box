// ============================================================================
// LiXee MatterBridge - Point d'entree principal
// Pont Matter <-> Zigbee via ZiGate+
//
// Architecture:
//   1. Init hardware (PSRAM, UART, GPIO)
//   2. Init LittleFS (configs, templates devices, fichiers web)
//   3. Init WiFi (STA mode, necessaire pour Matter)
//   4. Init ZiGate driver (UART vers ZiGate+, demarrage reseau Zigbee)
//   5. Init Matter Bridge (stack Matter, root node type Bridge)
//   6. Init Web Server (interface config + API REST)
//   7. Boucle principale: dispatch evenements ZiGate <-> Matter
// ============================================================================

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <stdarg.h>
#include "cJSON.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_littlefs.h"   // joltwallet/littlefs component

#include "config_manager.h"
#include "device_data.h"
#include "cluster_handlers.h"
#include "poll_engine.h"
#include "device_setup.h"
#include "ntp_sync.h"
#include "mqtt_manager.h"
#include "energy_history.h"
#include "zigate_driver.h"
#include "zigate_actions.h"
#include "thermostat.h"
#include "delestage.h"
#include "web_zigbee_remove.h"
#include "zigate_devices.h"
#include "matter_bridge.h"
#include "matter_device_manager.h"
#include "web_server.h"
#include "wifi_manager.h"
#include "eth_manager.h"
#include "energy_service.h"
#include "power_stats.h"
#include "zigate_ir.h"
#include "group_matter.h"
#include "rules.h"
#include "notifications.h"
#include "lora.h"
#include "esp_timer.h"
#include <sys/time.h>
#include <time.h>
#include "tunnel.h"
#include "maintenance.h"

// Version du firmware (peut etre surchargee par -DVERSION=... dans CMake)
#ifndef VERSION
#define VERSION "3.0"
#endif

static const char *TAG = "MAIN";

// ============================================================================
// Parametre NVS: Matter enable/disable (defaut: desactive)
// ============================================================================

static bool matter_enabled = false;

static void load_matter_enabled(void)
{
    nvs_handle_t nvs;
    if (nvs_open("config", NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t val = 0;
        if (nvs_get_u8(nvs, "matter_on", &val) == ESP_OK) {
            matter_enabled = (val != 0);
        }
        nvs_close(nvs);
    }
    ESP_LOGI(TAG, "Matter enabled: %s", matter_enabled ? "YES" : "NO");
}

extern "C" bool main_is_matter_enabled(void)
{
    return matter_enabled;
}

extern "C" esp_err_t main_set_matter_enabled(bool enabled)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open("config", NVS_READWRITE, &nvs);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_u8(nvs, "matter_on", enabled ? 1 : 0);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (ret == ESP_OK) {
        matter_enabled = enabled;
        ESP_LOGI(TAG, "Matter enabled set to: %s (reboot needed)", enabled ? "YES" : "NO");
    }
    return ret;
}

// ============================================================================
// Callbacks ZiGate -> Matter
// ============================================================================

/**
 * Appele quand la ZiGate recoit un rapport d'attribut d'un device Zigbee
 * Propage la valeur vers l'endpoint Matter correspondant
 */
/**
 * Pont Matter d'un emetteur LoRa : endpoint repris de lora.json s'il existe,
 * nouveau sinon (alors memorise). Nom affiche : alias de la fiche, ou modele.
 */
static bool lora_map_to_matter(const lora_emitter_info_t *info)
{
    char label[sizeof(((device_data_t *)0)->info.alias)] = "";
    const device_data_t *dd = device_data_find(info->ieee);
    if (dd) strlcpy(label, dd->info.alias, sizeof(label));
    int ep = matter_device_manager_map_lora_device(info->ieee, info->model, label, info->matter_ep);
    if (ep < 0) return false;
    if ((uint16_t)ep != info->matter_ep) lora_set_matter_endpoint(info->ieee, (uint16_t)ep);
    return true;
}

/**
 * Appareil retire de Matter ou remis (page Passerelle -> Matter). Le retrait
 * d'un appareil Zigbee / LoRa est fait par le gestionnaire ; ici, la remise
 * (endpoint repris) et les thermostats virtuels.
 */
static void on_matter_exposure(uint64_t ieee, bool excluded)
{
    thermostat_matter_refresh();
    group_matter_refresh();
    if (excluded || !matter_bridge_is_started()) return;
    zigate_device_t *dev = zigate_devices_find_by_ieee(ieee);
    if (dev && dev->active) {
        zigate_device_t snap = *dev;            // copie : le tableau peut bouger
        device_setup_load_template_now(snap.ieee_addr);
        matter_device_manager_map_device(&snap);
        return;
    }
    lora_emitter_info_t lem[LORA_MAX_EMITTERS];
    int n = lora_get_emitters(lem, LORA_MAX_EMITTERS);
    for (int i = 0; i < n; i++) {
        if (lem[i].ieee == ieee && lem[i].identified) lora_map_to_matter(&lem[i]);
    }
}

/**
 * Chemin commun d'une valeur recue, Zigbee ou LoRa : device_data, regles,
 * interpretation (alertes), historique d'energie, MQTT. La propagation vers
 * Matter est faite par l'appelant (recherche du mapping differente).
 */
/** ZLinky en Historique option BASE : mode 0 / 2 et PTEC "TH.." ou "BASE" (isHistoBaseOption de la Box). */
static bool zlinky_histo_base(uint64_t ieee)
{
    char buf[DEVDATA_VALUE_LEN];
    int mode = cfg_get_general()->linky_mode;
    if (device_data_get_value(ieee, 0xFF66, 0x0300, buf, sizeof(buf)) == ESP_OK && buf[0])
        mode = (int)strtol(buf, NULL, 16);
    if (mode != 0 && mode != 2) return false;
    if (device_data_get_value(ieee, 0xFF66, 0x0010, buf, sizeof(buf)) != ESP_OK || !buf[0]) return false;
    char txt[24];
    size_t n = 0;
    for (size_t i = 0; buf[i] && buf[i + 1] && n + 1 < sizeof(txt); i += 2) {
        char hx[3] = { buf[i], buf[i + 1], 0 };
        char c = (char)strtol(hx, NULL, 16);
        if (c == ' ' && n == 0) continue;
        txt[n++] = (char)toupper((unsigned char)c);
    }
    txt[n] = '\0';
    return !strncmp(txt, "TH", 2) || !strncmp(txt, "BASE", 4);
}

static void process_attribute(uint64_t ieee, const zigate_attribute_report_t *report)
{
    // Convertir les bytes en hex string (format identique a l'Arduino)
    char hex_value[DEVDATA_VALUE_LEN];
    int hex_len = 0;
    for (int i = 0; i < report->data_len && hex_len < (DEVDATA_VALUE_LEN - 3); i++) {
        hex_len += snprintf(hex_value + hex_len, sizeof(hex_value) - hex_len,
                           "%02X", report->data[i]);
    }
    hex_value[hex_len] = '\0';

    // Valeur et type ZCL : le type permet d'afficher correctement les
    // entiers signes (temperatures negatives) et les chaines
    device_data_set_value_typed(ieee, report->cluster_id,
                                report->attr_id, hex_value, report->attr_type);
    // Regles "evenement" surveillant cet attribut (ne bloque pas)
    rules_on_attribute(ieee, report->cluster_id, report->attr_id, hex_value);

    // Conversion numerique EXACTE, une seule fois, selon le type ZCL annonce.
    // Elle sert a la fois a l'historique et a la publication MQTT, pour que
    // les deux chemins ne puissent plus divergir.
    int64_t num_value = 0;
    bool has_num = zcl_to_int64(report->data, report->data_len,
                                report->attr_type, &num_value);
    // Statistiques de puissance (pics, tranches de charge) du ZLinky configure
    if (has_num && report->cluster_id == 0x0B04) {
        power_stats_on_attribute(ieee, report->cluster_id, report->attr_id, num_value);
    }

    // Dispatcher vers le cluster handler pour interpretation
    cluster_report_t cr = {};
    cr.ieee_addr  = ieee;
    cr.short_addr = report->short_addr;
    cr.endpoint   = report->endpoint;
    cr.cluster_id = report->cluster_id;
    cr.attr_id    = report->attr_id;
    cr.attr_type  = report->attr_type;
    cr.data_len   = report->data_len;
    if (report->data_len > 0) {
        memcpy(cr.data, report->data,
               (report->data_len > sizeof(cr.data)) ? sizeof(cr.data) : report->data_len);
    }

    cluster_interpreted_t interp = {};
    cluster_handler_dispatch(&cr, &interp);

    // Log la valeur interpretee si disponible
    if (interp.has_float) {
        ESP_LOGI(TAG, "  -> %.2f %s", interp.float_value, interp.unit);
    }
    if (interp.has_string) {
        ESP_LOGI(TAG, "  -> \"%s\"", interp.str_value);
    }
    // Alertes et WebPush : traites par notifications_on_report(), appele
    // depuis cluster_handler_dispatch()

    // Enregistrer dans l'historique d'energie si cluster pertinent
    // Cluster 0x0702 (Metering) ou 0x0B04 (Electrical Measurement)
    if (report->cluster_id == 0x0702 || report->cluster_id == 0x0B04) {
        // L'API d'historique est en int32_t : saturer plutot que replier.
        // Un index d'energie uint48 ne depasse INT32_MAX (2,147 GWh) qu'en
        // dehors de tout usage domestique realiste.
        int32_t int_value = 0;
        if (has_num) {
            if (num_value > INT32_MAX) {
                int_value = INT32_MAX;
                ESP_LOGW(TAG, "  value %lld saturee a INT32_MAX pour l'historique",
                         (long long)num_value);
            } else if (num_value < INT32_MIN) {
                int_value = INT32_MIN;
                ESP_LOGW(TAG, "  value %lld saturee a INT32_MIN pour l'historique",
                         (long long)num_value);
            } else {
                int_value = (int32_t)num_value;
            }
        } else {
            ESP_LOGW(TAG, "  donnees trop courtes (len=%u, type=0x%02X), historique ignore",
                     report->data_len, report->attr_type);
        }

        // Attributs d'energie cumulee (compteurs Wh) -> energy history
        // 0x0000 = CurrentSummationDelivered ; index tarifs aux adresses
        // PAIRES 0x0100..0x0112 (Tier1..Tier10) -- les impaires sont les
        // index "Received"
        // 0x0001 = energie injectee (EAIT, producteur) : courbe et revenu
        // de production de la page Energie
        if (has_num && report->cluster_id == 0x0702 &&
            (report->attr_id == 0x0000 || report->attr_id == 0x0001 ||
             (report->attr_id >= 0x0100 && report->attr_id <= 0x0112 && !(report->attr_id & 1)))) {
            // Lissage des trous pour le ZLinky et les sous-compteurs seulement :
            // un compteur a impulsions (gaz, eau, ZiPulses...) se tait tant que
            // rien n'est consomme, ce n'est pas une coupure (SimpleMeter.cpp de la Box)
            const cfg_general_t *gen = cfg_get_general();
            bool is_zl = strtoull(gen->zlinky, NULL, 16) == ieee;
            bool is_sub = false;
            for (int i = 0; i < gen->sub_meter_count && i < CFG_MAX_SUBMETERS && !is_sub; i++)
                is_sub = gen->sub_meters[i].enabled && strtoull(gen->sub_meters[i].IEEE, NULL, 16) == ieee;
            uint16_t hist_attr = report->attr_id;
            // Historique option BASE : seul l'index total (0) est publie ; range
            // sous le premier index tarifaire (256), prix et libelle BASE (Box)
            if (is_zl && report->attr_id == 0x0000 && zlinky_histo_base(ieee)) hist_attr = 256;
            energy_history_add_measurement_ex(ieee, hist_attr, int_value, is_zl || is_sub);
        }

        // Puissance : echantillonnee chaque minute par energy_service a
        // partir des dernieres valeurs (courbe des 24 h, voir
        // energy_service_minute_tick), et non plus a chaque rapport.
    }

    // Publier via MQTT si connecte
    if (mqtt_manager_is_connected()) {
        // Format de la LiXee-Box : cluster decimal, chaines en texte
        mqtt_manager_publish_attr(ieee, report->cluster_id, report->attr_id, hex_value,
                                  report->attr_type, has_num, num_value);

        // Entites tarif du Linky (tarif actuel, couleurs Tempo) : sur le tarif
        // en cours (PTEC / LTARF), DEMAIN ou STGE du ZLinky configure
        bool tariff_attr = (report->cluster_id == 0xFF66 &&
                            (report->attr_id == 16 || report->attr_id == 512 || report->attr_id == 1 ||
                             report->attr_id == 535)) ||
                           (report->cluster_id == 0x0702 && report->attr_id == 32);
        if (tariff_attr && ieee == strtoull(cfg_get_general()->zlinky, NULL, 16)) {
            energy_live_t L;
            if (energy_service_get_live(&L)) {
                mqtt_manager_publish_linky_tariff(ieee, L.tariff, L.tempo_today, L.tempo_tomorrow);
            }
        }
    }
}

/**
 * Boutons sans fil (sonoffButton.cpp de la LiXee-Box) : l'appui est range en
 * TEXTE sous un attribut, publie et soumis aux regles comme un rapport.
 */
static void on_zigbee_attribute_report(const zigate_attribute_report_t *report);

static void button_action(uint16_t short_addr, uint8_t endpoint, uint16_t cluster, uint16_t attr,
                          const char *action)
{
    zigate_attribute_report_t r = {};
    r.short_addr = short_addr;
    r.endpoint = endpoint;
    r.cluster_id = cluster;
    r.attr_id = attr;
    r.attr_type = 0x42;                          // chaine de caracteres
    size_t n = strlen(action);
    if (n > sizeof(r.data)) n = sizeof(r.data);
    memcpy(r.data, action, n);
    r.data_len = (uint16_t)n;
    ESP_LOGI(TAG, "[Bouton] 0x%04X : %s", short_addr, action);
    on_zigbee_attribute_report(&r);
}

/**
 * Commandes On/Off d'un bouton (0x8095). SONOFF SNZB-01P / SNZB-01 / WB01 :
 * Toggle = simple, On = double, Off = long ; autres : on / off / toggle.
 * Rangees sous 0006/0000, convention du bouton Aqara (24321.json).
 */
static void on_zigbee_onoff_command(uint16_t short_addr, uint8_t endpoint, uint8_t command)
{
    const zigate_device_t *d = zigate_devices_find_by_short(short_addr);
    if (!d) return;
    bool ewelink = strncmp(d->model, "SNZB-01", 7) == 0 || strcmp(d->model, "WB01") == 0;
    const char *action;
    switch (command) {
    case 0x00: action = ewelink ? "long" : "off"; break;
    case 0x01: action = ewelink ? "double" : "on"; break;
    case 0x02: action = ewelink ? "single" : "toggle"; break;
    default:
        ESP_LOGI(TAG, "[Bouton] 0x%04X : commande On/Off 0x%02X ignoree", short_addr, command);
        return;
    }
    button_action(short_addr, endpoint, 0x0006, 0x0000, action);
}

/**
 * Compteur d'energie Tuya (TS0601 dont le template est celui d'un compteur :
 * tension en EF00/100) : DP ramenes aux attributs du template, comme
 * energymeter.cpp de la LiXee-Box. Phases X/Y/Z (DP 6, 7, 8, brut 8 octets :
 * tension/10 sur 2, courant mA sur 3, puissance W sur 3) -> 100/101/102,
 * 110/111/112, 120/121/122 ; DDS238 (18 courant, 19 puissance, 20 tension) ;
 * variantes 103..123 ; 32 -> energie (1), 101 -> frequence (201), 111 -> 200.
 */
static bool tuya_is_meter(const zigate_device_t *d)
{
    if (!d || strcmp(d->model, "TS0601") != 0) return false;
    const devdata_template_t *t = device_data_get_template(d->ieee_addr);
    if (!t || !t->loaded) return false;
    for (int i = 0; i < t->state_count; i++) {
        if (t->states[i].cluster == 0xEF00 && t->states[i].attribute == 100) return true;
    }
    return false;
}

static int tuya_meter_attr(uint8_t dp)
{
    switch (dp) {
    case 18: case 104:            return 101;   // courant L1
    case 19: case 105:            return 102;   // puissance L1
    case 20: case 103:            return 100;   // tension L1
    case 32:                      return 1;     // energie totale (variante)
    case 101:                     return 201;   // frequence
    case 112:                     return 110;
    case 107: case 113:           return 111;
    case 108: case 114:           return 112;
    case 109: case 121:           return 120;
    case 110: case 122:           return 121;
    case 111:                     return 200;   // puissance totale
    case 123:                     return 122;
    default:                      return -1;
    }
}

/** true si le rapport a ete converti (et remonte sous ses nouveaux attributs). */
static bool tuya_meter_report(const zigate_attribute_report_t *report)
{
    static bool s_inside;                        // les rapports convertis ne repassent pas ici
    if (s_inside || report->cluster_id != 0xEF00 || report->attr_id > 0xFF) return false;
    const zigate_device_t *d = zigate_devices_find_by_short(report->short_addr);
    if (!tuya_is_meter(d)) return false;
    uint8_t dp = (uint8_t)report->attr_id;
    static zigate_attribute_report_t r;          // tache de traitement ZiGate seule
    s_inside = true;
    if (dp >= 6 && dp <= 8 && report->data_len >= 8) {
        const uint8_t *b = report->data;
        uint16_t base = (uint16_t)(100 + (dp - 6) * 10);
        r = *report;
        r.attr_id = base;                        // tension (dV)
        r.attr_type = 0x21;
        r.data_len = 2;
        r.data[0] = b[0]; r.data[1] = b[1];
        on_zigbee_attribute_report(&r);
        r = *report;
        r.attr_id = (uint16_t)(base + 1);        // courant (mA)
        r.attr_type = 0x23;
        r.data_len = 4;
        r.data[0] = 0; r.data[1] = b[2]; r.data[2] = b[3]; r.data[3] = b[4];
        on_zigbee_attribute_report(&r);
        r = *report;
        r.attr_id = (uint16_t)(base + 2);        // puissance (W)
        r.attr_type = 0x23;
        r.data_len = 4;
        r.data[0] = 0; r.data[1] = b[5]; r.data[2] = b[6]; r.data[3] = b[7];
        on_zigbee_attribute_report(&r);
        s_inside = false;
        return false;                            // DP brut garde aussi (diagnostic)
    }
    int to = tuya_meter_attr(dp);
    if (to < 0) { s_inside = false; return false; }
    r = *report;
    r.attr_id = (uint16_t)to;
    on_zigbee_attribute_report(&r);
    s_inside = false;
    return true;
}

/**
 * Codes IR enregistres d'une telecommande : une action par code (commande 201,
 * valeur = numero du code), apres les actions du template. Ils apparaissent
 * ainsi sur la fiche de l'appareil, dans les regles, les groupes d'actions,
 * les boutons Home Assistant (MQTT) et comme actionneur des thermostats.
 */
static void on_ir_codes_changed(uint64_t ieee)
{
    char (*names)[ZIGATE_IR_NAME_LEN] = (char (*)[ZIGATE_IR_NAME_LEN])
        heap_caps_calloc(ZIGATE_IR_MAX_CODES, ZIGATE_IR_NAME_LEN, MALLOC_CAP_SPIRAM);
    devdata_template_action_t *acts = (devdata_template_action_t *)
        heap_caps_calloc(ZIGATE_IR_MAX_CODES, sizeof(devdata_template_action_t), MALLOC_CAP_SPIRAM);
    if (!names || !acts) {
        heap_caps_free(names);
        heap_caps_free(acts);
        return;
    }
    int n = zigate_ir_code_names(ieee, names, ZIGATE_IR_MAX_CODES);
    const zigate_device_t *d = zigate_devices_find_by_ieee(ieee);
    for (int i = 0; i < n; i++) {
        strlcpy(acts[i].name, names[i], sizeof(acts[i].name));
        acts[i].command = ZB_ACT_IR_SEND;
        acts[i].endpoint = d ? d->endpoint : 1;
        acts[i].value = i;
        acts[i].visible = true;
    }
    esp_err_t e = device_data_set_dynamic_actions(ieee, acts, n);
    ESP_LOGI(TAG, "[IR] %016llX : %d code(s) en actions (%s)", (unsigned long long)ieee, n, esp_err_to_name(e));
    heap_caps_free(names);
    heap_caps_free(acts);
    // Boutons Home Assistant des nouveaux codes (sans effet si MQTT deconnecte)
    if (e == ESP_OK && n > 0 && cfg_get_general()->ha_mqtt) mqtt_manager_publish_discovery(ieee, NULL);
}

static void on_zigbee_attribute_report(const zigate_attribute_report_t *report)
{
    if (tuya_meter_report(report)) return;

    // SONOFF SNZB-01M (4 boutons) : FC12/0000 depuis l'endpoint du bouton, type
    // d'appui en valeur (1 simple, 2 double, 3 long, 4 triple). Action unique au
    // format de Zigbee2MQTT ("double_button_2"), rangee en texte sous FC12/0000
    if (report->cluster_id == 0xFC12 && report->attr_id == 0x0000 && report->attr_type != 0x42 &&
        report->data_len >= 1) {
        static const char *const PRESS[5] = { "unknown", "single", "double", "long", "triple" };
        uint8_t v = report->data[report->data_len - 1];
        char action[32];
        snprintf(action, sizeof(action), "%s_button_%u", PRESS[v <= 4 ? v : 0], report->endpoint);
        button_action(report->short_addr, report->endpoint, 0xFC12, 0x0000, action);
        return;
    }

    ESP_LOGI(TAG, "ZB attr report: short=0x%04X cluster=0x%04X attr=0x%04X",
             report->short_addr, report->cluster_id, report->attr_id);

    zigate_device_t *dev = zigate_devices_find_by_short(report->short_addr);
    if (dev) {
        uint64_t ieee = dev->ieee_addr;     // copie : le tableau peut bouger
        uint16_t key = report->endpoint >= 2
            ? device_data_storage_attr(ieee, report->cluster_id, report->attr_id, report->endpoint)
            : report->attr_id;
        bool is_float = report->attr_type == 0x39 && report->data_len >= 4;
        if (key != report->attr_id || is_float) {
            // Copie modifiee pour le stockage, MQTT et les regles ; Matter recoit
            // le rapport d'origine.
            //  - ligne de template propre a cet endpoint (interrupteur multi-voies) :
            //    rangee sous sa cle composee ;
            //  - flottant ZCL : entier x 100 (int32), comme la LiXee-Box
            //    (defaultCluster.cpp), les templates portant un coefficient 0,01
            static zigate_attribute_report_t ep_rep;   // tache de traitement ZiGate seule
            ep_rep = *report;
            ep_rep.attr_id = key;
            if (is_float) {
                uint32_t bits = ((uint32_t)report->data[0] << 24) | ((uint32_t)report->data[1] << 16) |
                                ((uint32_t)report->data[2] << 8) | report->data[3];
                float f;
                memcpy(&f, &bits, sizeof(f));
                double scaled = (double)f * 100.0;
                int32_t iv = (scaled != scaled) ? 0 : (scaled > 2147483647.0 ? INT32_MAX :
                             (scaled < -2147483648.0 ? INT32_MIN : (int32_t)lround(scaled)));
                ep_rep.attr_type = 0x2B;                 // int32
                ep_rep.data[0] = (uint8_t)(iv >> 24);
                ep_rep.data[1] = (uint8_t)(iv >> 16);
                ep_rep.data[2] = (uint8_t)(iv >> 8);
                ep_rep.data[3] = (uint8_t)iv;
                ep_rep.data_len = 4;
            }
            process_attribute(ieee, &ep_rep);
        } else {
            process_attribute(ieee, report);
        }

        // Detection de mouvement : presence des thermostats virtuels
        if (report->cluster_id == 0x0406 && report->attr_id == 0x0000 && report->data_len >= 1) {
            thermostat_note_occupancy(ieee, (report->data[report->data_len - 1] & 0x01) != 0);
        }

        // Modele recu apres la fin de la configuration (appareil endormi qui
        // n'a pas repondu dans les 10 s de l'appairage) : la configuration est
        // relancee, ce qui charge le template et recompose l'appareil Matter
        if (report->cluster_id == 0x0000 && report->attr_id == 0x0005) {
            const devdata_template_t *t = device_data_get_template(ieee);
            if (!t || !t->loaded || t->dyn_only) device_setup_request(ieee, false);
        }
    }

    // Propager vers Matter
    matter_device_manager_handle_zigbee_report(report);
}

/** Emetteur LoRa supprime (page LoRa) : son appareil Matter disparait aussi. */
static void on_lora_removed(uint64_t ieee)
{
    matter_device_manager_unmap_device(ieee);
}

/**
 * Valeur decodee d'une trame LoRa (tache LoRa). Meme chemin que le Zigbee,
 * puis Matter : l'emetteur est ponte des que son modele est connu.
 */
static void on_lora_report(const lora_report_t *r)
{
    // Tache LoRa seule : rapport statique (~270 octets hors pile)
    static zigate_attribute_report_t rep;
    memset(&rep, 0, sizeof(rep));
    rep.short_addr = r->short_addr;
    rep.cluster_id = r->cluster_id;
    rep.attr_id    = r->attr_id;
    rep.attr_type  = r->attr_type;
    rep.endpoint   = r->endpoint;
    rep.data_len   = (r->data_len > sizeof(rep.data)) ? (uint16_t)sizeof(rep.data) : r->data_len;
    if (r->data && rep.data_len) memcpy(rep.data, r->data, rep.data_len);

    process_attribute(r->ieee_addr, &rep);

    if (!matter_enabled || !matter_bridge_is_started()) return;
    int slot = lora_find_slot_by_ieee(r->ieee_addr);
    lora_emitter_info_t info;
    if (slot < 0 || !lora_get_emitter(slot, &info)) return;
    if (!matter_device_manager_get_mapping_by_ieee(r->ieee_addr)) {
        // Emetteur appaire apres le demarrage : endpoint cree des que son
        // modele est connu. Un echec n'est retente qu'apres 60 s.
        static uint32_t s_next_try_s[LORA_MAX_EMITTERS];
        uint32_t now_s = (uint32_t)(esp_timer_get_time() / 1000000);
        if (!info.identified || now_s < s_next_try_s[slot]) return;
        if (!lora_map_to_matter(&info)) {
            s_next_try_s[slot] = now_s + 60;
            return;
        }
    }
    matter_device_manager_handle_report_for_ieee(r->ieee_addr, info.model, &rep);
}

/**
 * Appele quand un nouveau device Zigbee s'annonce sur le reseau
 * La decouverte est lancee automatiquement par le protocol handler
 * (Active Endpoints -> Simple Descriptor -> Basic Attributes)
 */
// Bandeaux de l'interface (/getAlert) : nom affiche d'un appareil
static void device_name(uint64_t ieee, char *out, size_t len)
{
    const zigate_device_t *d = zigate_devices_find_by_ieee(ieee);
    if (d && d->alias[0]) strlcpy(out, d->alias, len);
    else if (d && d->model[0]) snprintf(out, len, "%s (%016llX)", d->model, (unsigned long long)ieee);
    else snprintf(out, len, "%016llX", (unsigned long long)ieee);
}

/** Etapes de configuration d'un appareil (bind, reporting) : bandeau. */
static void on_device_setup_progress(uint64_t ieee, const char *step)
{
    char name[64], msg[128];
    device_name(ieee, name, sizeof(name));
    snprintf(msg, sizeof(msg), "%s : %s", name, step);
    notifications_toast(strcmp(step, "Configuration OK") == 0 ? 0 : 2, msg);
}

/**
 * Accuses radio : regles, et bandeau d'erreur pour un paquet non remis a un
 * appareil (0x8702, comme la LiXee-Box), au plus un par appareil et par minute.
 */
static void on_zigate_radio(uint16_t msg_type, uint16_t short_addr, uint8_t status)
{
    rules_on_zigate_radio_result(msg_type, short_addr, status);
    if (msg_type != 0x8702 || status == 0 || zigate_radio_fail_quiet()) return;
    static uint16_t last_sa;
    static int64_t last_us;
    int64_t now = esp_timer_get_time();
    if (short_addr == last_sa && now - last_us < 60LL * 1000000) return;
    last_sa = short_addr;
    last_us = now;
    // Adresse inconnue du pont (ancien appareil, voisin d'un routeur, requete
    // de la ZiGate elle-meme) : alerte sans nom d'appareil, inutile et
    // inquietante pour l'utilisateur
    const zigate_device_t *d = zigate_devices_find_by_short(short_addr);
    if (!d) {
        ESP_LOGI(TAG, "Erreur paquet %02x vers 0x%04X (inconnue du pont) : pas d'alerte", status, short_addr);
        return;
    }
    char name[64], msg[128];
    device_name(d->ieee_addr, name, sizeof(name));
    if (!name[0]) return;
    snprintf(msg, sizeof(msg), "Erreur paquet : %02x - Appareil : %s", status, name);
    notifications_toast(1, msg);
}

/**
 * Appareil sur pile : niveau ou tension de batterie connus, ou declares par
 * son template (cluster 0x0001), meme avant le premier rapport.
 */
static bool device_on_battery(uint64_t ieee)
{
    // Source d'alimentation lue (Basic 0x0007) : elle fait foi
    // (bits 0-6 : 3 = pile ; 1, 2, 4 = secteur ; bit 7 : pile de secours)
    const char *v = device_data_get_value_ptr(ieee, 0x0000, 0x0007);
    if (v && v[0]) return (strtol(v, NULL, 16) & 0x7F) == 0x03;
    v = device_data_get_value_ptr(ieee, 0x0001, 0x0021);
    if (v && v[0]) return true;
    v = device_data_get_value_ptr(ieee, 0x0001, 0x0020);
    if (v && v[0]) return true;
    const devdata_template_t *t = device_data_get_template(ieee);
    for (int i = 0; t && t->loaded && i < t->state_count; i++) {
        if (t->states[i].cluster == 0x0001 && (t->states[i].attribute == 0x0020 || t->states[i].attribute == 0x0021))
            return true;
    }
    return false;
}

static void on_zigbee_device_announce(uint16_t short_addr, uint64_t ieee_addr)
{
    ESP_LOGI(TAG, "ZB device announce: short=0x%04X ieee=0x%016llX", short_addr, ieee_addr);
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "Appareil détecté : %016llX", (unsigned long long)ieee_addr);
        notifications_toast(2, msg);
    }
    // La decouverte (Active Endpoints + Simple Desc) est lancee automatiquement
    // par zigate_protocol.cpp apres un delai de 1500ms
    // Le callback on_zigbee_discovery_done sera appele quand c'est fini
}

/**
 * Appele quand la decouverte d'un device est terminee
 * (Active Endpoints + Simple Descriptor ont ete recus)
 * Cree l'endpoint Matter correspondant
 */
static void on_zigbee_discovery_done(uint16_t short_addr, uint64_t ieee_addr)
{
    ESP_LOGI(TAG, "ZB discovery complete: short=0x%04X ieee=0x%016llX", short_addr, ieee_addr);

    // Recuperer le device avec ses infos completes
    zigate_device_t *dev = zigate_devices_find_by_ieee(ieee_addr);
    if (!dev) {
        ESP_LOGE(TAG, "Device 0x%016llX not found after discovery!", ieee_addr);
        return;
    }

    ESP_LOGI(TAG, "Device info: devId=0x%04X ep=%d profile=0x%04X",
             dev->device_id, dev->endpoint, dev->profile_id);

    // Creer/recup le device_data associe pour stocker les valeurs
    device_data_t *dd = device_data_get_or_create(ieee_addr);
    if (dd) {
        // Copier l'alias depuis zigate_device_t si device_data n'en a pas
        if (dd->info.alias[0] == '\0' && dev->alias[0] != '\0') {
            strncpy(dd->info.alias, dev->alias, sizeof(dd->info.alias) - 1);
        }

        // Template, bind, config report et liste de poll : dans la tache de
        // configuration, qui attend que le modele soit connu (il est lu juste
        // apres la decouverte). Force : un appareil qui (re)joint le reseau
        // peut avoir perdu sa configuration de reporting.
        device_setup_request(ieee_addr, true);
    }

    // L'endpoint Matter est cree par on_device_template_ready(), une fois le
    // template charge par la tache de configuration : il en fixe le contenu
}

/**
 * Template d'un appareil Zigbee charge (ou introuvable) par la tache de
 * configuration : creation de son endpoint Matter s'il n'existe pas encore.
 */
static void on_device_template_ready(uint64_t ieee_addr, bool ok)
{
    if (!matter_enabled || !matter_bridge_is_started()) return;
    const bridge_device_mapping_t *m = matter_device_manager_get_mapping_by_ieee(ieee_addr);
    if (m && (m->profile || !ok)) return;       // deja compose, ou rien de nouveau

    zigate_device_t *dev = zigate_devices_find_by_ieee(ieee_addr);
    if (!dev) return;
    zigate_device_t snap = *dev;                // copie : le tableau peut bouger
    // Endpoint existant sans profil : recompose depuis le template arrive
    int ep = m ? matter_device_manager_refresh_device(&snap) : matter_device_manager_map_device(&snap);
    if (ep >= 0) {
        ESP_LOGI(TAG, "Appareil 0x%016llX -> endpoint Matter %d (%s)",
                 (unsigned long long)ieee_addr, ep, ok ? "template" : "sans template");
    } else {
        ESP_LOGE(TAG, "Endpoint Matter impossible pour 0x%016llX", (unsigned long long)ieee_addr);
    }
}

/**
 * Appele quand un device Zigbee quitte le reseau
 * Supprime l'endpoint Matter correspondant
 */
static void on_zigbee_device_leave(uint64_t ieee_addr)
{
    ESP_LOGW(TAG, "ZB device leave: ieee=0x%016llX", ieee_addr);
    {
        char name[64], msg[96];
        device_name(ieee_addr, name, sizeof(name));
        snprintf(msg, sizeof(msg), "Appareil parti : %s", name);
        notifications_toast(1, msg);
    }
    // Comme la LiXee-Box : une alerte seulement. Un appareil qui quitte le
    // reseau le rejoint souvent aussitot (changement de parent, remise sous
    // tension) : le supprimer effacait son nom, ses donnees, son template et son
    // appareil Matter. La suppression reste un choix de la page Zigbee.
}

/**
 * Regles et groupes d'actions : commandes de template executees par le module
 * Zigbee (zigate_actions), plus complet que l'executeur interne des regles.
 * Appele depuis la tache "rules" (pile en PSRAM) : aucune ecriture en flash.
 */
static esp_err_t rules_zigbee_exec(uint64_t ieee, uint16_t short_addr, uint8_t endpoint,
                                   int command, uint16_t cluster, uint16_t manufacturer_code,
                                   const char *value, uint16_t *zigate_cmd_out)
{

    int32_t v = (value && value[0]) ? (int32_t)strtol(value, NULL, 10) : 0;
    // _ex : rend la commande ZiGate emise, pour que les regles reconnaissent
    // leur propre statut 0x8000 et pas celui d une autre commande
    return zigate_action_send_ex(command, short_addr, endpoint, v, cluster, manufacturer_code, zigate_cmd_out);
}

/** Consigne enregistree d'un thermostat (renvoi apres changement de mode). */
static bool hvac_setpoint_source(uint16_t short_addr, uint16_t attr, int16_t *out)
{
    const zigate_device_t *d = zigate_devices_find_by_short(short_addr);
    if (!d) return false;
    const devdata_attr_t *a = device_data_get_attr(d->ieee_addr, 0x0201, attr);
    int64_t v;
    if (!a || !a->value[0] || !device_data_hex_to_int64(a->value, a->type, 0x0201, attr, &v)) return false;
    *out = (int16_t)v;
    return true;
}

/** Action "notification" d'une regle : mise en file, sans acces flash. */
static void rules_notify(const char *title, const char *msg, int level)
{
    notifications_add_alert(title, msg, level, "rule", 0, 0);
}

/** Notifications relayees au service distant par le tunnel (si connecte). */
static void push_notification_to_tunnel(const char *title, const char *message, int type,
                                        const char *alert_type, float value, float threshold)
{
    tunnel_send_notification(title, message, type, alert_type, value, threshold);
}

// ============================================================================
// Init sous-systemes
// ============================================================================

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS: erasing and reinit");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

// ============================================================================
// Reinitialisation de secours (ConfigResetManager de la LiXee-Box) : trois
// demarrages en moins de quelques secondes chacun effacent la config WiFi,
// remettent l'Ethernet en automatique / DHCP et desactivent la securite HTTP
// (mot de passe oublie, IP fixe erronee, Ethernet seul sans cable).
// ============================================================================

#define BOOT_RESET_NS        "boot"
#define BOOT_RESET_KEY       "count"
#define BOOT_RESET_COUNT     3
#define BOOT_RESET_WINDOW_US (4 * 1000000LL)   // demarrage stable apres 4 s

static void boot_counter_clear(void *arg)
{
    (void)arg;
    nvs_handle_t h;
    if (nvs_open(BOOT_RESET_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, BOOT_RESET_KEY);
        nvs_commit(h);
        nvs_close(h);
    }
}

/** A appeler une fois la configuration chargee : RAZ si 3 demarrages rapides. */
static void boot_reset_check(void)
{
    nvs_handle_t h;
    if (nvs_open(BOOT_RESET_NS, NVS_READWRITE, &h) != ESP_OK) return;
    uint8_t count = 0;
    nvs_get_u8(h, BOOT_RESET_KEY, &count);
    count++;
    nvs_set_u8(h, BOOT_RESET_KEY, count);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Demarrages rapides : %u/%d", count, BOOT_RESET_COUNT);

    if (count >= BOOT_RESET_COUNT) {
        ESP_LOGW(TAG, "!!! REINITIALISATION DE SECOURS (WiFi, Ethernet, securite HTTP) !!!");
        boot_counter_clear(NULL);
        wifi_mgr_clear_credentials();
        remove("/littlefs/config/configWifi.json");
        remove(ETH_CONFIG_PATH);
        cfg_get_settings_mut()->enable_secure_http = false;
        cfg_save_general();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }

    // Compteur remis a zero une fois le demarrage stable
    static esp_timer_handle_t t;
    esp_timer_create_args_t a = {};
    a.callback = boot_counter_clear;
    a.name = "boot_rst";
    if (esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_once(t, BOOT_RESET_WINDOW_US);
}

// ============================================================================
// Journal de debogage (addDebugLog de la LiXee-Box) : avec l'option Debug de
// Config General, les avertissements et erreurs du firmware sont ecrits dans
// /littlefs/debug/debug_JJ.log, prefixes de la date et de la memoire libre.
// Le crochet de log ne fait que copier en RAM (il tourne dans toutes les
// taches, dont celles a pile en PSRAM) ; la tache principale ecrit en flash.
// ============================================================================

#define DBG_BUF_SIZE   4096
#define DBG_FILE_MAX   20000

static char *s_dbg_buf;
static size_t s_dbg_len;
static portMUX_TYPE s_dbg_mux = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_prev_vprintf;

static int debug_log_vprintf(const char *fmt, va_list ap)
{
    va_list cp;
    va_copy(cp, ap);
    int r = s_prev_vprintf ? s_prev_vprintf(fmt, ap) : vprintf(fmt, ap);
    if (s_dbg_buf && cfg_get_settings()->enable_debug) {
        char line[200];
        int n = vsnprintf(line, sizeof(line), fmt, cp);
        // Niveau : premiere lettre apres une eventuelle sequence de couleur
        const char *p = line;
        if (*p == '\033') { while (*p && *p != 'm') p++; if (*p) p++; }
        if (n > 0 && (*p == 'E' || *p == 'W') && p[1] == ' ') {
            size_t len = strnlen(p, sizeof(line) - (p - line));
            portENTER_CRITICAL(&s_dbg_mux);
            if (s_dbg_len + len + 1 < DBG_BUF_SIZE) {
                memcpy(s_dbg_buf + s_dbg_len, p, len);
                s_dbg_len += len;
            }
            portEXIT_CRITICAL(&s_dbg_mux);
        }
    }
    va_end(cp);
    return r;
}

static void debug_log_init(void)
{
    s_dbg_buf = (char *)heap_caps_malloc(DBG_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_dbg_buf) s_prev_vprintf = esp_log_set_vprintf(debug_log_vprintf);
}

/** Ecrit les lignes en attente (tache principale, toutes les 10 s). */
static void debug_log_flush(void)
{
    if (!s_dbg_buf || !s_dbg_len) return;
    // Sans heure (NTP) : lignes gardees en memoire, sinon rangees dans le
    // fichier du 1er janvier 1970 avec une date fausse
    if (!ntp_sync_is_synced()) return;
    static char chunk[DBG_BUF_SIZE];
    portENTER_CRITICAL(&s_dbg_mux);
    size_t n = s_dbg_len;
    memcpy(chunk, s_dbg_buf, n);
    s_dbg_len = 0;
    portEXIT_CRITICAL(&s_dbg_mux);
    if (!cfg_get_settings()->enable_debug) return;

    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    char path[48], stamp[48];
    snprintf(path, sizeof(path), "/littlefs/debug/debug_%02d.log", lt.tm_mday);
    snprintf(stamp, sizeof(stamp), "%02d/%02d/%04d %02d:%02d", lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900,
             lt.tm_hour, lt.tm_min);
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > DBG_FILE_MAX) remove(path);   // fichier du jour plein
    FILE *f = fopen(path, "a");
    if (!f) return;
    // Une ligne par message : "[date] : Free Mem RAM : x / Free Mem PSRAM : y / Err :message"
    size_t i = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && chunk[j] != '\n') j++;
        if (j > i) {
            fprintf(f, "[%s] : Free Mem RAM : %u / Free Mem PSRAM : %u / Err :%.*s\n", stamp,
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (int)(j - i), chunk + i);
        }
        i = j + 1;
    }
    fclose(f);
}

// ----------------------------------------------------------------------------
// Reprise des fichiers d'une LiXee-Box Arduino (mise a jour par son tar)
// ----------------------------------------------------------------------------

/**
 * La Box nomme ses fichiers par l'IEEE en minuscules ("%02x" :
 * db/00158d0006203457.json, hst/nrg_00158d0006203457.json), le pont en
 * majuscules ; LittleFS distingue la casse. Renommage une fois pour toutes
 * (sauf si le fichier du pont existe deja) : <dir>/<prefix><16 hexa>.json.
 */
/**
 * IEEE en minuscules dans un fichier de configuration de la LiXee-Box
 * (ZLinky, sous-compteurs, regles, thermostats, delestage...) : passes en
 * majuscules, comme le pont les ecrit et les propose dans ses listes (sans
 * quoi les appareils choisis n'apparaissaient plus selectionnes). Seules les
 * suites d'exactement 16 chiffres hexadecimaux sont touchees ; fichier
 * reecrit (tmp + renommage) seulement s'il change.
 */
static void normalize_ieee_in_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 256 * 1024) { fclose(f); return; }
    char *b = (char *)heap_caps_malloc((size_t)n + 1, MALLOC_CAP_SPIRAM);
    if (!b) { fclose(f); return; }
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[got] = '\0';
    int changed = 0;
    for (size_t i = 0; i < got;) {
        if (!isxdigit((unsigned char)b[i])) { i++; continue; }
        size_t j = i;
        bool lower = false;
        while (j < got && isxdigit((unsigned char)b[j])) { lower |= (b[j] >= 'a' && b[j] <= 'f'); j++; }
        bool bounded = (i == 0 || !isalnum((unsigned char)b[i - 1])) && (j >= got || !isalnum((unsigned char)b[j]));
        if (j - i == 16 && lower && bounded) {
            for (size_t k = i; k < j; k++) b[k] = (char)toupper((unsigned char)b[k]);
            changed++;
        }
        i = j;
    }
    if (changed) {
        char tmp[96];
        snprintf(tmp, sizeof(tmp), "%s.tmp", path);
        FILE *w = fopen(tmp, "wb");
        bool ok = w && fwrite(b, 1, got, w) == got;
        if (w) fclose(w);
        if (ok) {
            remove(path);
            ok = rename(tmp, path) == 0;
        } else {
            remove(tmp);
        }
        ESP_LOGI(TAG, "LiXee-Box : %d IEEE passe(s) en majuscules dans %s%s", changed, path, ok ? "" : " (ECHEC)");
    }
    heap_caps_free(b);
}

static void normalize_ieee_files(const char *dir, const char *prefix)
{
    const size_t pl = strlen(prefix);
    char (*names)[48] = (char (*)[48])heap_caps_calloc(160, 48, MALLOC_CAP_SPIRAM);
    if (!names) return;
    int n = 0;
    DIR *d = opendir(dir);
    if (!d) { heap_caps_free(names); return; }
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < 160) {
        const char *nm = e->d_name;
        if (strlen(nm) != pl + 21 || strncmp(nm, prefix, pl) != 0 || strcmp(nm + pl + 16, ".json") != 0) continue;
        bool hex = true, lower = false;
        for (size_t i = pl; i < pl + 16 && hex; i++) {
            hex = isxdigit((unsigned char)nm[i]) != 0;
            lower |= (nm[i] >= 'a' && nm[i] <= 'f');
        }
        if (hex && lower) strlcpy(names[n++], nm, 48);
    }
    closedir(d);   // pas de renommage pendant la lecture du repertoire
    int done = 0;
    for (int k = 0; k < n; k++) {
        char up[48], from[96], to[96];
        strlcpy(up, names[k], sizeof(up));
        for (size_t i = pl; i < pl + 16; i++) up[i] = (char)toupper((unsigned char)up[i]);
        snprintf(from, sizeof(from), "%s/%s", dir, names[k]);
        snprintf(to, sizeof(to), "%s/%s", dir, up);
        struct stat st;
        if (stat(to, &st) == 0) continue;            // fichier du pont deja present : garde
        if (rename(from, to) == 0) done++;
    }
    if (done) ESP_LOGI(TAG, "LiXee-Box : %d fichier(s) %s/%s* renomme(s) en majuscules", done, dir, prefix);
    heap_caps_free(names);
}

/**
 * Pas de liste d'appareils du pont (zigbee_devices.json) mais des fiches
 * db/ de la LiXee-Box : liste reconstruite depuis leur section INFO
 * (shortAddr et device_id en decimal, endpoint, fabricant, modele, alias).
 * La ZiGate garde son reseau ; les appareils sont deja configures (bind et
 * reporting faits par la Box).
 */
static void import_box_devices(void)
{
    if (zigate_devices_count() > 0) return;
    // Emetteurs LoRa (config/lora.json) : fiche db/ aussi, mais pas Zigbee
    uint64_t lora_ieee[8];
    int n_lora = 0;
    {
        FILE *lf = fopen("/littlefs/config/lora.json", "rb");
        if (lf) {
            static char lb[2048];
            size_t lg = fread(lb, 1, sizeof(lb) - 1, lf);
            fclose(lf);
            lb[lg] = '\0';
            cJSON *ld = cJSON_Parse(lb);
            const cJSON *em;
            cJSON_ArrayForEach(em, cJSON_GetObjectItem(ld, "emitters")) {
                const cJSON *mac = cJSON_GetObjectItem(em, "mac");
                if (cJSON_IsString(mac) && n_lora < 8) lora_ieee[n_lora++] = strtoull(mac->valuestring, NULL, 16);
            }
            cJSON_Delete(ld);
        }
    }
    DIR *d = opendir("/littlefs/db");
    if (!d) return;
    int imported = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *nm = e->d_name;
        if (strlen(nm) != 21 || strcmp(nm + 16, ".json") != 0) continue;
        uint64_t ieee = strtoull(nm, NULL, 16);
        if (!ieee) continue;
        bool lora = false;
        for (int k = 0; k < n_lora && !lora; k++) lora = (lora_ieee[k] == ieee);
        if (lora) continue;
        char path[300];   // d_name peut atteindre 255 octets
        snprintf(path, sizeof(path), "/littlefs/db/%s", nm);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        static char buf[2048];                    // INFO est en tete du fichier
        size_t got = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[got] = '\0';
        // Section INFO seule (le fichier entier peut depasser le tampon)
        char *info = strstr(buf, "\"INFO\"");
        char *brace = info ? strchr(info, '{') : NULL;
        char *end = brace ? strchr(brace, '}') : NULL;
        if (!end) continue;
        end[1] = '\0';
        cJSON *o = cJSON_Parse(brace);
        if (!o) continue;
        const cJSON *sa = cJSON_GetObjectItem(o, "shortAddr");
        const cJSON *id = cJSON_GetObjectItem(o, "device_id");
        const cJSON *ep = cJSON_GetObjectItem(o, "endpoint");
        const cJSON *mf = cJSON_GetObjectItem(o, "manufacturer");
        const cJSON *md = cJSON_GetObjectItem(o, "model");
        const cJSON *al = cJSON_GetObjectItem(o, "alias");
        auto num = [](const cJSON *j) -> long {
            if (cJSON_IsNumber(j)) return (long)j->valuedouble;
            return cJSON_IsString(j) ? strtol(j->valuestring, NULL, 10) : 0;
        };
        zigate_device_t dev = {};
        dev.ieee_addr = ieee;
        dev.short_addr = (uint16_t)num(sa);
        dev.device_id = (uint16_t)num(id);
        dev.profile_id = 0x0104;
        long epn = num(ep);
        dev.endpoint = (uint8_t)(epn > 0 && epn < 241 ? epn : 1);
        if (cJSON_IsString(mf)) strlcpy(dev.manufacturer, mf->valuestring, sizeof(dev.manufacturer));
        if (cJSON_IsString(md)) strlcpy(dev.model, md->valuestring, sizeof(dev.model));
        if (cJSON_IsString(al)) strlcpy(dev.alias, al->valuestring, sizeof(dev.alias));
        dev.active = true;
        dev.configured = true;
        dev.matter_endpoint = -1;
        cJSON_Delete(o);
        if (!dev.short_addr && !dev.model[0]) continue;   // fiche sans identite (emetteur LoRa...)
        if (zigate_devices_add_or_update(&dev) >= 0) imported++;
    }
    closedir(d);
    if (imported) ESP_LOGI(TAG, "LiXee-Box : %d appareil(s) Zigbee repris de /littlefs/db", imported);
}

static esp_err_t init_littlefs(void)
{
    // Table du pont : "littlefs". LiXee-Box Arduino mise a jour vers ce
    // firmware : "spiffs", en LittleFS, avec la configuration, les appareils et
    // les historiques de la Box, relus tels quels. Ce systeme-la n'est formate
    // qu'en dernier recours (sinon le pont ne demarrerait pas du tout).
    const char *label = maintenance_fs_label();
    bool box_layout = strcmp(label, "littlefs") != 0;
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = label,
        .format_if_mount_failed = !box_layout,
        .dont_mount = false,
    };
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK && box_layout) {
        ESP_LOGE(TAG, "LittleFS de la LiXee-Box illisible (%s) : formatage", esp_err_to_name(ret));
        conf.format_if_mount_failed = true;
        ret = esp_vfs_littlefs_register(&conf);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_littlefs_info(label, &total, &used);
    ESP_LOGI(TAG, "LittleFS (%s%s): total=%u, used=%u", label, box_layout ? ", table LiXee-Box" : "", total, used);
    return ESP_OK;
}

// init_wifi() supprime — remplace par wifi_manager component

// ============================================================================
// Entry point
// ============================================================================

/**
 * Allocations de cJSON en PSRAM de preference. Sans cela, les milliers de
 * petits noeuds d'un document (historiques, graphiques, notifications.json)
 * allaient en RAM interne (malloc <= 4 Ko), la ressource qui manque au pont.
 */
static void *cjson_malloc_prefer_psram(size_t size)
{
    return heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
}

extern "C" void app_main(void)
{
    cJSON_Hooks cjson_hooks = { cjson_malloc_prefer_psram, free };
    cJSON_InitHooks(&cjson_hooks);

    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "  LiXee MatterBridge " VERSION);
    ESP_LOGI(TAG, "  Pont Matter <-> Zigbee (ZiGate+)");
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "Free heap: %lu (dont RAM interne: %lu)",
             esp_get_free_heap_size(), esp_get_free_internal_heap_size());

    // 1. NVS (requis par WiFi et Matter)
    ESP_ERROR_CHECK(init_nvs());

    // 2. LittleFS (configs, templates, web)
    ESP_ERROR_CHECK(init_littlefs());
    maintenance_early_init();   // applique une restauration en attente avant toute lecture de config
    {
        // Configuration d'une LiXee-Box Arduino : IEEE en majuscules (une fois,
        // sans effet ensuite). Pas tunnel.json ni configWifi.json (secrets).
        static const char *const BOX_CFG[] = {
            "/littlefs/config/configGeneral.json", "/littlefs/config/delestage.json",
            "/littlefs/config/rules.json", "/littlefs/config/statusRules.json",
            "/littlefs/config/thermostats.json", "/littlefs/config/actiongroups.json",
            "/littlefs/config/notifications.json", "/littlefs/hst/presence.json",
        };
        for (size_t i = 0; i < sizeof(BOX_CFG) / sizeof(BOX_CFG[0]); i++) normalize_ieee_in_file(BOX_CFG[i]);
    }

    // 2b. Config Manager (structures de config + chargement JSON)
    ESP_ERROR_CHECK(cfg_init());
    cfg_ensure_directories();
    cfg_load_general();   // Charge /littlefs/config/configGeneral.json (non-fatal si absent)
    tunnel_init();        // avant tout cfg_save_general() : migre les cles tunnel de configGeneral.json
    cfg_load_wifi();      // Charge /littlefs/config/configWifi.json (non-fatal si absent)
    {
        // Sauvegarde restauree (pont ou LiXee-Box) : identifiants WiFi de
        // configWifi.json reportes en NVS, seule source de la connexion, puis
        // mot de passe retire du fichier (LittleFS est lisible en mode dev)
        cfg_settings_t *s = cfg_get_settings_mut();
        wifi_mgr_credentials_t cur = {};
        bool known = wifi_mgr_get_credentials(&cur) == ESP_OK && cur.ssid[0];
        if (s->ssid[0] && (s->password[0] || !known || strcmp(cur.ssid, s->ssid) != 0)) {
            if (!known || strcmp(cur.ssid, s->ssid) != 0 || strcmp(cur.password, s->password) != 0) {
                esp_err_t we = wifi_mgr_set_credentials(s->ssid, s->password);
                ESP_LOGI(TAG, "Identifiants WiFi de configWifi.json reportes en NVS : %s", esp_err_to_name(we));
            }
            if (s->password[0]) {
                memset(s->password, 0, sizeof(s->password));
                cfg_save_wifi();
            }
        }
        memset(&cur, 0, sizeof(cur));
    }
    boot_reset_check();   // 3 demarrages rapides : RAZ WiFi / Ethernet / securite HTTP
    debug_log_init();     // journal /debug (option Debug de Config General)
    {
        // IP fixe du WiFi (configWifi.json), appliquee au demarrage de l'interface
        const cfg_settings_t *s = cfg_get_settings();
        if (!s->enable_dhcp) wifi_mgr_set_static_ip(s->ip_address, s->ip_mask, s->ip_gateway);
    }

    {
        const cfg_settings_t *s = cfg_get_settings();
        const cfg_general_t  *g = cfg_get_general();
        ESP_LOGI(TAG, "Config: debug=%d mqtt=%d history=%d ZLinky='%s'",
                 s->enable_debug, s->enable_mqtt, s->enable_history, g->zlinky);
    }

    // 3. WiFi Manager (bloquant: attend connexion ou lance BLE provisioning)
    // Matter partage le mDNS du pont et doit l'initialiser le premier : avec
    // Matter actif, le mDNS du pont attend la fin de matter_bridge_start().
    load_matter_enabled();
    wifi_mgr_set_mdns_shared(matter_enabled);

    // wifi_mgr_init() cree la pile TCP/IP et la boucle d'evenements, communes
    // au WiFi et a l'Ethernet ; la connexion WiFi n'est faite que par
    // wifi_mgr_start().
    wifi_mgr_config_t wifi_cfg = WIFI_MGR_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(wifi_mgr_init(&wifi_cfg));

    // 3a. Ethernet W5500, prioritaire (politique de la LiXee-Box) : cable
    // branche et adresse obtenue, ni connexion WiFi ni provisioning BLE.
    // Sans W5500 sur la carte, eth_mgr_start() echoue proprement.
    {
        char hostname[20];
        wifi_mgr_get_hostname(hostname, sizeof(hostname));
        if (eth_mgr_start(hostname)) {
            eth_mgr_wait_for_ip(3000, 10000);
        }
    }

    if (eth_mgr_has_ip()) {
        eth_mgr_status_t st;
        eth_mgr_get_status(&st);
        ESP_LOGI(TAG, "Reseau : Ethernet " IPSTR " (%d Mbit/s) - WiFi non connecte",
                 IP2STR((esp_ip4_addr_t *)&st.ip), st.speed_mbps);
    } else if (eth_mgr_inhibits_wifi()) {
        // Mode Ethernet seul sans cable : le reseau demarrera au branchement
        ESP_LOGW(TAG, "Reseau : Ethernet seul, en attente du cable / DHCP - WiFi non demarre");
    } else {
        // Sans WiFi : provisioning BLE en tache de fond, le pont demarre quand
        // meme (Zigbee, regles, delestage ; Ethernet branche plus tard)
        esp_err_t we = wifi_mgr_start();
        if (we != ESP_OK) ESP_LOGW(TAG, "Reseau : WiFi non connecte (%s), provisioning BLE actif",
                                   esp_err_to_name(we));
    }

    // 3b. NTP Sync (synchronisation de l'heure, necessaire avant MQTT/logs)
    ntp_sync_init();  // Non-fatal si NTP echoue (retry en arriere-plan)
    {
        char time_buf[20];
        if (ntp_sync_get_formatted_datetime(time_buf, sizeof(time_buf)) == ESP_OK) {
            ESP_LOGI(TAG, "Current time: %s", time_buf);
        }
    }

    // 3c. MQTT Manager (initialise le client, ne se connecte pas encore)
    mqtt_manager_init();  // Non-fatal si MQTT desactive ou pas configure

    // 4. ZiGate driver (UART + reseau Zigbee)
    ESP_ERROR_CHECK(zigate_init());
    ESP_ERROR_CHECK(zigate_devices_init());

    // 4b. Device Data (valeurs cluster/attribut, templates, persistence)
    ESP_ERROR_CHECK(device_data_init());
    // Fichiers d'une LiXee-Box Arduino : noms en majuscules, liste des appareils
    normalize_ieee_files("/littlefs/db", "");
    normalize_ieee_files("/littlefs/hst", "nrg_");
    normalize_ieee_files("/littlefs/hst", "pwr_");
    import_box_devices();
    device_data_load_all();  // Charge /littlefs/db/ (non-fatal si vide)
    ESP_LOGI(TAG, "Device data loaded: %d devices", device_data_count());

    // 4c. Cluster Handlers (interpretation des attributs ZCL)
    ESP_ERROR_CHECK(cluster_handlers_init());

    // 4d. Poll Engine (interrogation periodique des attributs)
    ESP_ERROR_CHECK(poll_engine_init());

    // 4e. Energy History (historique consommation energetique)
    ESP_ERROR_CHECK(energy_history_init());
    energy_history_load_all();  // Charge /littlefs/hst/ (non-fatal si vide)
    energy_service_init();      // Courbe de puissance 24 h, calculs de la page Energie
    power_stats_init();         // Pics, plateaux et tranches de charge (/api/power/*)
    zigate_ir_set_change_cb(on_ir_codes_changed);   // codes appris = actions de l'appareil
    zigate_ir_init();           // Codes des telecommandes infrarouges (/littlefs/ir)
    notifications_init();       // centre de notifications, alertes, envois WebPush / email
    rules_init();               // regles et groupes d'actions (lit rules.json, actiongroups.json)
    rules_set_command_executor(rules_zigbee_exec);
    zigate_actions_set_setpoint_source(hvac_setpoint_source);
    rules_set_notify_cb(rules_notify);
    thermostat_init();          // thermostats virtuels (lit thermostats.json, regulation 30 s)
    delestage_init();           // delestage des prises sur depassement de puissance souscrite
    {
        // Module LoRa : absent sur la plupart des cartes (ESP_ERR_NOT_FOUND, sans gravite)
        // Callbacks avant lora_init() : la tache radio demarre dans lora_init
        lora_register_report_cb(on_lora_report);
        lora_register_removed_cb(on_lora_removed);
        esp_err_t le = lora_init();
        if (le != ESP_OK && le != ESP_ERR_NOT_FOUND) ESP_LOGW(TAG, "LoRa: %s", esp_err_to_name(le));
    }

    // Enregistrer les callbacks ZiGate
    zigate_register_attribute_cb(on_zigbee_attribute_report);
    zigate_register_device_announce_cb(on_zigbee_device_announce);
    zigate_register_device_leave_cb(on_zigbee_device_leave);
    zigate_register_onoff_cmd_cb(on_zigbee_onoff_command);
    zigate_register_discovery_done_cb(on_zigbee_discovery_done);
    // Cadencement des actions des regles sur les accuses de la ZiGate
    zigate_register_status_cb(rules_on_zigate_status);
    zigate_register_radio_cb(on_zigate_radio);
    zigate_register_sleepy_cb(device_on_battery);
    device_setup_register_progress_cb(on_device_setup_progress);

    // Sequence d'init ZiGate identique au code de reference (ino.cpp setupZigbeeAndTasks):
    // 1. Reset logiciel (0x0011)
    ESP_LOGI(TAG, "ZiGate: sending soft reset (0x0011)...");
    zigate_send_command(ZIGATE_CMD_RESET, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 2. Get network state (0x0009)
    ESP_LOGI(TAG, "ZiGate: requesting network state (0x0009)...");
    zigate_send_command(ZIGATE_CMD_NETWORK_STATE, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(200));

    // 3. Start network (0x0024)
    ESP_LOGI(TAG, "ZiGate: starting Zigbee network (0x0024)...");
    zigate_start_network();
    vTaskDelay(pdMS_TO_TICKS(200));

    // 4. Get version (0x0010)
    ESP_LOGI(TAG, "ZiGate: requesting firmware version (0x0010)...");
    zigate_get_version();
    vTaskDelay(pdMS_TO_TICKS(200));

    // 5. Network state details (0x0025)
    ESP_LOGI(TAG, "ZiGate: requesting network state details (0x0025)...");
    zigate_get_network_state();
    vTaskDelay(pdMS_TO_TICKS(2000));
    if (!zigate_is_detected()) {
        ESP_LOGW(TAG, "ZiGate: aucune reponse, menus Zigbee masques (nouvel essai toutes les 30 s)");
    }

    ESP_LOGI(TAG, "ZiGate initialization sequence complete");

    // Configuration des appareils a partir de leur template. Au demarrage :
    // chargement du template de chaque appareil connu (coefficients, liste de
    // poll), et bind + config report pour ceux jamais configures -- dont ceux
    // appaires avant que ce mecanisme existe.
    ESP_ERROR_CHECK(device_setup_init());
    device_setup_register_template_cb(on_device_template_ready);

    // 5. Config Matter : lue avant le WiFi (partage du mDNS)

    // 6. Matter Bridge (conditionne a matter_enabled)
    if (matter_enabled) {
        ESP_LOGI(TAG, "Matter is ENABLED — initializing bridge...");
        // L'initialisation de la pile CHIP remet l'horloge au 01/01/2000
        // (InitClock_RealTime, sans condition) : l'heure NTP deja obtenue
        // etait perdue jusqu'a la synchronisation suivante, et l'historique
        // d'energie ignorait les mesures. Elle est restauree apres le demarrage.
        struct timeval tv_before;
        gettimeofday(&tv_before, NULL);
        const int64_t us_before = esp_timer_get_time();

        ESP_ERROR_CHECK(matter_bridge_init());
        ESP_ERROR_CHECK(matter_device_manager_init());
        ESP_ERROR_CHECK(matter_bridge_start());
        // CHIP a pu se connecter au WiFi enregistre avant de passer la main au
        // pont : coupe tout de suite si l'Ethernet a deja son adresse
        if (eth_mgr_has_ip()) wifi_mgr_disconnect();

        const time_t valid_epoch = 1577836800;  // 2020-01-01
        if (tv_before.tv_sec > valid_epoch && time(NULL) < valid_epoch) {
            int64_t us = (int64_t)tv_before.tv_sec * 1000000 + tv_before.tv_usec +
                         (esp_timer_get_time() - us_before);
            struct timeval tv = { (time_t)(us / 1000000), (suseconds_t)(us % 1000000) };
            settimeofday(&tv, NULL);
            ESP_LOGI(TAG, "Heure NTP restauree apres le demarrage de Matter");
        }

        // Matter a initialise le mDNS : le pont y ajoute LIXEEBOX-XXYY.local
        wifi_mgr_start_mdns();

        // Version materielle affichee par les controleurs : LiXeeBox_XXYY,
        // meme suffixe que le nom mDNS (au lieu de TEST_VERSION)
        {
            char hn[24] = "", hw[48];
            wifi_mgr_get_hostname(hn, sizeof(hn));
            const char *dash = strrchr(hn, '-');
            snprintf(hw, sizeof(hw), "LiXeeBox_%s", dash ? dash + 1 : hn);
            matter_bridge_set_hardware_version_string(hw);
        }

        // Re-creer les endpoints Matter des devices Zigbee deja connus APRES
        // start() : esp-matter ne relit qu'au demarrage le compteur des
        // endpoints deja attribues, et c'est ce qui permet de reprendre pour
        // chaque device le meme endpoint qu'avant (un controleur comme Home
        // Assistant identifie l'appareil ponte par ce numero).
        // Le template est charge d'abord : il fixe les types d'appareil et les
        // attributs Matter de l'endpoint (voir matter_profile.h).
        for (int i = 0; i < zigate_devices_count(); i++) {
            zigate_device_t *dev = zigate_devices_get(i);
            if (!dev || !dev->active) continue;
            zigate_device_t snap = *dev;        // copie : le tableau peut bouger
            device_setup_load_template_now(snap.ieee_addr);
            matter_device_manager_map_device(&snap);
        }

        // Puis les emetteurs LoRa identifies (ZLinky LoRa), sur leur endpoint
        // memorise dans lora.json ; les autres le seront a leur premiere trame
        lora_emitter_info_t lem[LORA_MAX_EMITTERS];
        int nlem = lora_get_emitters(lem, LORA_MAX_EMITTERS);
        for (int i = 0; i < nlem; i++) {
            if (lem[i].identified) lora_map_to_matter(&lem[i]);
        }

        // Thermostats virtuels : un thermostat ponte par zone
        thermostat_matter_start();
        // Groupes d'actions publies : un interrupteur ponte par groupe
        group_matter_start();
        matter_device_manager_set_exposure_cb(on_matter_exposure);
    } else {
        ESP_LOGI(TAG, "Matter is DISABLED — skipping bridge init");
        ESP_LOGI(TAG, "Enable Matter via web interface and reboot");
    }

    // Configuration des appareils a partir de leur template (apres les
    // endpoints Matter : la tache de configuration recharge les templates
    // que le pont Matter vient de lire)
    device_setup_boot();

    // 7. Web Server
    ESP_ERROR_CHECK(web_server_init());
    maintenance_init();         // config de mise a jour, confirmation de l'image OTA
    tunnel_start();             // rejoue les requetes du relais sur 127.0.0.1:80
    notifications_set_push_callback(push_notification_to_tunnel);

    // 8. Demarrer le Poll Engine (apres tout le reste pour laisser le reseau se stabiliser)
    ESP_ERROR_CHECK(poll_engine_start(0));  // 0 = intervalle par defaut (60s)

    // 9. Demarrer MQTT (apres ZiGate init pour pouvoir publier les devices)
    mqtt_manager_start();

    ESP_LOGI(TAG, "All subsystems initialized. Bridge is running.");

    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "  Bridge ready!");
    ESP_LOGI(TAG, "  Matter: %s", matter_enabled ? "ON" : "OFF");
    ESP_LOGI(TAG, "  MQTT:   %s", mqtt_manager_is_connected() ? "ON" : "OFF (connecting...)");
    ESP_LOGI(TAG, "  NTP:    %s", ntp_sync_is_synced() ? "SYNCED" : "PENDING");
    // RAM interne : son epuisement fait echouer Bluetooth, chiffrement et
    // sockets. Le minimum atteint depuis le demarrage donne la vraie marge.
    ESP_LOGI(TAG, "  RAM interne: libre %u, min %u, plus grand bloc %u, DMA %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));
    ESP_LOGI(TAG, "  PSRAM:       libre %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "==========================================");

    // Boucle principale - les evenements sont geres par les taches FreeRTOS
    // (tache UART ZiGate, tache Matter, tache HTTP server, tache Poll Engine)
    uint32_t save_counter = 0;
    int last_reset_hour = -1;
    int last_reset_day  = -1;
    int last_reset_month = -1;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // MQTT: health check + reconnexion (chaque seconde)
        mqtt_manager_loop();

        // Statistiques de puissance (traitement toutes les 10 s, ecriture horaire)
        power_stats_loop();

        // Mode automatique, Ethernet prioritaire (LiXee-Box) : WiFi de secours
        // apres 30 s sans adresse Ethernet (alors qu'il en avait une), coupe
        // apres 30 s d'Ethernet retrouve (ou branche apres un demarrage en WiFi).
        // Le WiFi ne tourne donc jamais en parallele de l'Ethernet.
        {
            static int eth_down_s, eth_up_s;
            static bool eth_was_up, wifi_backup;
            if (eth_mgr_has_ip()) {
                eth_was_up = true;
                eth_down_s = 0;
                if ((wifi_backup || wifi_mgr_is_connected()) && ++eth_up_s >= 30) {
                    eth_up_s = 0;
                    wifi_backup = false;
                    wifi_mgr_disconnect();
                    ESP_LOGI(TAG, "Ethernet retrouve depuis 30 s : WiFi coupe");
                }
                // DNS commun aux interfaces : remis s'il a ete efface (toutes les 5 s)
                static int dns_tick;
                if (++dns_tick >= 5) { dns_tick = 0; eth_mgr_ensure_dns(); }
            } else {
                eth_up_s = 0;
                if (eth_was_up && !wifi_backup && !eth_mgr_inhibits_wifi() &&
                    !wifi_mgr_is_connected() && ++eth_down_s >= 30) {
                    eth_down_s = 0;
                    wifi_backup = true;          // reconnexion WiFi ensuite illimitee
                    esp_err_t fe = wifi_mgr_connect_background();
                    ESP_LOGW(TAG, "Ethernet perdu depuis 30 s : secours WiFi (%s)", esp_err_to_name(fe));
                    if (fe == ESP_ERR_NOT_FOUND) wifi_backup = false;   // pas d'identifiants
                }
            }
        }

        // Regles horaires : a chaque changement de minute de l'horloge (et une
        // fois au demarrage), pour qu'aucune minute ne soit sautee par une
        // iteration de boucle plus longue qu'une seconde ; 60 s sans heure
        {
            static int last_min = -2;
            static int no_time_tick;
            if (ntp_sync_is_synced()) {
                int m = ntp_sync_get_minute();
                if (m != last_min) { last_min = m; rules_tick(); }
            } else if (last_min == -2 || ++no_time_tick >= 60) {
                last_min = -1;
                no_time_tick = 0;
                rules_tick();
            }
        }

        // Notifications : verifications d'alertes, ecriture differee en flash
        notifications_tick();

        // Heure memorisee chaque heure : repli au demarrage sans NTP
        {
            static int epoch_tick;
            if (++epoch_tick >= 3600) {
                epoch_tick = 0;
                time_t now = time(NULL);
                if (ntp_sync_is_synced() && now > 1700000000) {
                    cfg_get_general_mut()->epoch_time = now;
                    cfg_save_general();
                }
            }
        }

        // Journal de debogage : ecriture groupee toutes les 10 s
        {
            static int dbg_tick;
            if (++dbg_tick >= 10) { dbg_tick = 0; debug_log_flush(); }
        }

        // Config generale modifiee par une tache qui n'ecrit pas en flash (LoRa)
        cfg_save_general_if_requested();

        // ZiGate muette au demarrage (pas encore alimentee, absente) : la
        // demande de version est renvoyee toutes les 30 s ; la premiere
        // reponse rend les menus Zigbee a l'interface
        static uint32_t zigate_probe_counter = 0;
        if (!zigate_is_detected() && ++zigate_probe_counter >= 30) {
            zigate_probe_counter = 0;
            zigate_get_version();
        }

        // Sauvegarde periodique des device_data + energy_history (toutes les 60s)
        save_counter++;
        if (save_counter >= 60) {
            save_counter = 0;
            device_data_save_all();
            energy_history_save_all();
            energy_service_minute_tick();

            // Reset periodique de l'historique d'energie (equivalent ScanDevicesToRAZ)
            if (ntp_sync_is_synced()) {
                int cur_hour  = ntp_sync_get_hour();
                int cur_day   = ntp_sync_get_day();
                int cur_month = ntp_sync_get_month();
                int cur_minute = ntp_sync_get_minute();

                // Reset horaire: quand minute == 0 et heure differente
                if (cur_minute == 0 && cur_hour != last_reset_hour) {
                    energy_history_reset_hourly(cur_hour);
                    last_reset_hour = cur_hour;

                    // Reset journalier: quand heure == 0
                    if (cur_hour == 0 && cur_day != last_reset_day) {
                        energy_history_reset_daily(cur_day);
                        last_reset_day = cur_day;

                        // Reset mensuel: quand jour == 1
                        if (cur_day == 1 && cur_month != last_reset_month) {
                            energy_history_reset_monthly(cur_month);
                            last_reset_month = cur_month;
                        }
                    }
                }
            }
        }
    }
}
