// ============================================================================
// Matter Device Manager
// Mapping bidirectionnel Zigbee <-> Matter endpoints
// Conversion des clusters ZCL vers clusters Matter
// ============================================================================

#include "matter_device_manager.h"
#include "matter_bridge.h"
#include "elec_power_delegate.h"
#include "zigate_driver.h"
#include "zigate_devices.h"
#include "zigate_actions.h"
#include "device_data.h"
#include "config_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <esp_matter_attribute_utils.h>
#include "cJSON.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>

static const char *TAG = "MATTER_DEV_MGR";

static bridge_device_mapping_t s_mappings[MATTER_BRIDGE_MAX_ENDPOINTS];
static int s_mapping_count = 0;

// Les appareils Zigbee sont mappes par la tache ZiGate, les emetteurs LoRa par
// la tache LoRa : l'ajout d'un mapping est serialise. Les entrees ne bougent
// jamais (ajout en fin de tableau), les pointeurs rendus restent valides.
static SemaphoreHandle_t s_map_lock = NULL;
static void map_lock(void)   { if (s_map_lock) xSemaphoreTakeRecursive(s_map_lock, portMAX_DELAY); }
static void map_unlock(void) { if (s_map_lock) xSemaphoreGiveRecursive(s_map_lock); }

// ============================================================================
// Appareils retires de Matter
// ============================================================================

#define EXCL_PATH   "/littlefs/config/matterExclude.json"
#define EXCL_TMP    "/littlefs/config/tmpMatterExclude.json"
#define EXCL_MAX    64

static uint64_t s_excl[EXCL_MAX];
static int s_excl_count;
static bool s_excl_loaded;
static portMUX_TYPE s_excl_mux = portMUX_INITIALIZER_UNLOCKED;
static matter_exposure_cb_t s_exposure_cb;

static void excl_load(void)
{
    if (s_excl_loaded) return;
    uint64_t list[EXCL_MAX];
    int n = 0;
    FILE *f = fopen(EXCL_PATH, "r");
    if (f) {
        char buf[EXCL_MAX * 20 + 32];
        size_t len = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[len] = '\0';
        cJSON *root = cJSON_Parse(buf);
        const cJSON *it;
        cJSON_ArrayForEach(it, cJSON_GetObjectItem(root, "exclude")) {
            if (n < EXCL_MAX && cJSON_IsString(it)) list[n++] = strtoull(it->valuestring, NULL, 16);
        }
        cJSON_Delete(root);
    }
    portENTER_CRITICAL(&s_excl_mux);
    if (!s_excl_loaded) {
        memcpy(s_excl, list, sizeof(uint64_t) * n);
        s_excl_count = n;
        s_excl_loaded = true;
    }
    portEXIT_CRITICAL(&s_excl_mux);
}

static bool excl_save(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "exclude");
    uint64_t list[EXCL_MAX];
    portENTER_CRITICAL(&s_excl_mux);
    int n = s_excl_count;
    memcpy(list, s_excl, sizeof(uint64_t) * n);
    portEXIT_CRITICAL(&s_excl_mux);
    for (int i = 0; i < n; i++) {
        char id[20];
        snprintf(id, sizeof(id), "%016llX", (unsigned long long)list[i]);
        cJSON_AddItemToArray(arr, cJSON_CreateString(id));
    }
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) return false;
    bool ok = false;
    FILE *f = fopen(EXCL_TMP, "w");
    if (f) {
        size_t len = strlen(txt);
        ok = fwrite(txt, 1, len, f) == len;
        ok = (fclose(f) == 0) && ok;
        if (ok) {
            remove(EXCL_PATH);
            ok = rename(EXCL_TMP, EXCL_PATH) == 0;
        }
    }
    free(txt);
    return ok;
}

bool matter_device_manager_is_excluded(uint64_t ieee)
{
    excl_load();
    bool found = false;
    portENTER_CRITICAL(&s_excl_mux);
    for (int i = 0; i < s_excl_count && !found; i++) found = (s_excl[i] == ieee);
    portEXIT_CRITICAL(&s_excl_mux);
    return found;
}

char *matter_device_manager_excluded_json(void)
{
    excl_load();
    cJSON *arr = cJSON_CreateArray();
    uint64_t list[EXCL_MAX];
    portENTER_CRITICAL(&s_excl_mux);
    int n = s_excl_count;
    memcpy(list, s_excl, sizeof(uint64_t) * n);
    portEXIT_CRITICAL(&s_excl_mux);
    for (int i = 0; i < n; i++) {
        char id[20];
        snprintf(id, sizeof(id), "%016llX", (unsigned long long)list[i]);
        cJSON_AddItemToArray(arr, cJSON_CreateString(id));
    }
    char *s = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return s;
}

void matter_device_manager_set_exposure_cb(matter_exposure_cb_t cb) { s_exposure_cb = cb; }

esp_err_t matter_device_manager_set_excluded(uint64_t ieee, bool excluded)
{
    if (!ieee) return ESP_ERR_INVALID_ARG;
    excl_load();
    bool changed = false, full = false;
    portENTER_CRITICAL(&s_excl_mux);
    int idx = -1;
    for (int i = 0; i < s_excl_count; i++) if (s_excl[i] == ieee) idx = i;
    if (excluded && idx < 0) {
        if (s_excl_count < EXCL_MAX) { s_excl[s_excl_count++] = ieee; changed = true; }
        else full = true;
    } else if (!excluded && idx >= 0) {
        s_excl[idx] = s_excl[--s_excl_count];
        changed = true;
    }
    portEXIT_CRITICAL(&s_excl_mux);
    if (full) return ESP_ERR_NO_MEM;
    if (changed && !excl_save()) ESP_LOGE(TAG, "Echec d'enregistrement de %s", EXCL_PATH);
    ESP_LOGI(TAG, "Appareil 0x%016llX %s Matter", (unsigned long long)ieee, excluded ? "retire de" : "remis dans");
    if (excluded) matter_device_manager_unmap_device(ieee);    // sans effet si non ponte
    if (s_exposure_cb) s_exposure_cb(ieee, excluded);
    return ESP_OK;
}

static void replay_stored_values(bridge_device_mapping_t *m);

// Role de l'index d'energie d'un appareil (config Energie), voir matter_profile.h
static matter_meter_role_t meter_role(uint64_t ieee)
{
    char id[20];
    snprintf(id, sizeof(id), "%016llX", (unsigned long long)ieee);
    const cfg_general_t *g = cfg_get_general();
    if (!strcasecmp(g->gaz, id) || !strcasecmp(g->water, id)) return MATTER_METER_OTHER;
    if (!strcasecmp(g->production, id)) return MATTER_METER_ELECTRIC;
    for (int i = 0; i < g->sub_meter_count && i < CFG_MAX_SUBMETERS; i++) {
        if (g->sub_meters[i].enabled && !strcasecmp(g->sub_meters[i].IEEE, id)) return MATTER_METER_ELECTRIC;
    }
    return MATTER_METER_AUTO;
}

// Entier non signe big-endian (ordre ZiGate) d'au moins min_len octets, lu sur
// au plus 8 octets. Refuse un rapport trop court au lieu de lire des octets
// residuels du tampon.
static bool be_uint(const zigate_attribute_report_t *r, int min_len, uint64_t *out)
{
    int n = r->data_len;
    if (n < min_len) return false;
    if (n > 8) n = 8;
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v = (v << 8) | r->data[i];
    *out = v;
    return true;
}

// ============================================================================
// Conversion ZCL device ID -> Matter device type
// ============================================================================

matter_device_type_t matter_device_manager_zcl_to_matter_type(uint16_t zcl_device_id)
{
    switch (zcl_device_id) {
    case 0x0000:  // On/Off Switch (ZCL)
    case 0x0100:  return MATTER_DEV_ON_OFF_LIGHT;
    case 0x0101:  return MATTER_DEV_DIMMABLE_LIGHT;
    case 0x0102:  return MATTER_DEV_COLOR_LIGHT;
    case 0x0009:  // Mains Power Outlet
    case 0x010A:  return MATTER_DEV_ON_OFF_PLUG;
    case 0x0302:  return MATTER_DEV_TEMPERATURE_SENSOR;
    case 0x0307:  return MATTER_DEV_HUMIDITY_SENSOR;
    case 0x000A:  return MATTER_DEV_DOOR_LOCK;
    case 0x0107:  return MATTER_DEV_OCCUPANCY_SENSOR;
    case 0x0202:  return MATTER_DEV_WINDOW_COVERING;
    case 0x0301:  return MATTER_DEV_THERMOSTAT;
    case 0x0015:  return MATTER_DEV_CONTACT_SENSOR;
    // Devices specifiques LiXee / energie
    case 0x0051:  // Smart Plug
    case 0x0053:  return MATTER_DEV_ON_OFF_PLUG;
    default:
        ESP_LOGW(TAG, "Unknown ZCL device ID 0x%04X, defaulting to ON_OFF_PLUG", zcl_device_id);
        return MATTER_DEV_ON_OFF_PLUG;
    }
}

// ============================================================================
// API
// ============================================================================

esp_err_t matter_device_manager_init(void)
{
    memset(s_mappings, 0, sizeof(s_mappings));
    s_mapping_count = 0;
    if (!s_map_lock) s_map_lock = xSemaphoreCreateRecursiveMutex();
    // Lu ici, dans la tache principale : les taches a pile en PSRAM (LoRa)
    // qui consultent la liste ne doivent jamais lire la flash
    excl_load();
    // TODO: charger les mappings depuis NVS
    ESP_LOGI(TAG, "Device manager initialized");
    return ESP_OK;
}

static int map_device_locked(const zigate_device_t *zigbee_dev);

int matter_device_manager_map_device(const zigate_device_t *zigbee_dev)
{
    if (!zigbee_dev) return -1;
    map_lock();
    int ep = map_device_locked(zigbee_dev);
    map_unlock();
    return ep;
}

static int map_device_locked(const zigate_device_t *zigbee_dev)
{
    if (matter_device_manager_is_excluded(zigbee_dev->ieee_addr)) return -1;   // retire par l'utilisateur

    // Verifier si deja mappe
    for (int i = 0; i < s_mapping_count; i++) {
        if (s_mappings[i].zigbee_ieee == zigbee_dev->ieee_addr && s_mappings[i].active) {
            ESP_LOGD(TAG, "Device 0x%016llX already mapped to endpoint %d",
                     zigbee_dev->ieee_addr, s_mappings[i].matter_endpoint);
            return s_mappings[i].matter_endpoint;
        }
    }

    if (s_mapping_count >= MATTER_BRIDGE_MAX_ENDPOINTS) {
        ESP_LOGE(TAG, "Max endpoints reached (%d)", MATTER_BRIDGE_MAX_ENDPOINTS);
        return -1;
    }

    // Determiner le type Matter
    // Detection d'energy meter par 2 methodes (clusters OU model name):
    //   1. Clusters: si le device possede 0x0702 (Metering) ou 0x0B04 (Electrical Measurement)
    //      -> disponible lors de la decouverte runtime (Simple Descriptor)
    //   2. Model: si le model commence par "ZLinky"
    //      -> disponible au boot depuis LittleFS (les clusters ne sont pas persistes)
    // Le ZLinky se declare en devId=0x0101 (Dimmable Light) a tort
    matter_device_type_t mtype;
    bool has_metering = false;

    // Methode 1: detection par clusters (runtime discovery)
    ESP_LOGI(TAG, "Checking %d in_clusters for energy metering on 0x%04X (devId=0x%04X model=%s):",
             zigbee_dev->in_cluster_count, zigbee_dev->short_addr, zigbee_dev->device_id,
             zigbee_dev->model[0] ? zigbee_dev->model : "(empty)");
    for (int i = 0; i < zigbee_dev->in_cluster_count; i++) {
        ESP_LOGI(TAG, "  in_cluster[%d] = 0x%04X", i, zigbee_dev->in_clusters[i]);
        if (zigbee_dev->in_clusters[i] == 0x0702 || zigbee_dev->in_clusters[i] == 0x0B04) {
            has_metering = true;
        }
    }

    // Methode 2: detection par model name (boot depuis LittleFS)
    if (!has_metering && zigbee_dev->model[0]) {
        if (strncmp(zigbee_dev->model, "ZLinky", 6) == 0) {
            ESP_LOGI(TAG, "ZLinky detected by model name, overriding to ENERGY_METER");
            has_metering = true;
        }
    }

    if (has_metering) {
        ESP_LOGI(TAG, "Energy metering device detected on 0x%04X, mapping as ENERGY_METER",
                 zigbee_dev->short_addr);
        mtype = MATTER_DEV_ENERGY_METER;
    } else {
        ESP_LOGI(TAG, "No metering detected, using ZCL devId 0x%04X", zigbee_dev->device_id);
        mtype = matter_device_manager_zcl_to_matter_type(zigbee_dev->device_id);
    }

    // Profil du template : types d'appareil et attributs Matter lies a ses
    // etats. Sans template (ou sans etat ayant un equivalent Matter), le
    // type deduit ci-dessus du DeviceId est garde.
    matter_profile_t *profile = matter_profile_build(device_data_get_template(zigbee_dev->ieee_addr),
                                                     zigbee_dev->model, zigbee_dev->device_id,
                                                     meter_role(zigbee_dev->ieee_addr));

    // Creer l'endpoint Matter
    const char *label = zigbee_dev->alias[0] ? zigbee_dev->alias : zigbee_dev->model;
    // matter_endpoint : numero attribue lors d'un demarrage precedent,
    // persiste dans la base Zigbee (-1 pour un appareil nouveau).
    int ep_id = profile
        ? matter_bridge_add_endpoint_profile(zigbee_dev->ieee_addr, profile, label,
                                             zigbee_dev->matter_endpoint)
        : matter_bridge_add_endpoint(zigbee_dev->ieee_addr, mtype, label,
                                     zigbee_dev->matter_endpoint);
    if (ep_id < 0) {
        ESP_LOGE(TAG, "Failed to create Matter endpoint");
        free(profile);
        return -1;
    }

    // Enregistrer le mapping
    bridge_device_mapping_t *m = &s_mappings[s_mapping_count];
    m->matter_endpoint = ep_id;
    m->zigbee_ieee = zigbee_dev->ieee_addr;
    m->zigbee_short_addr = zigbee_dev->short_addr;
    m->device_type = mtype;
    m->active = true;
    m->profile = profile;
    m->last_lift100 = -1;
    m->last_move_us = 0;
    s_mapping_count++;
    replay_stored_values(m);        // dernieres valeurs connues, sans attendre les rapports

    // Mettre a jour le device Zigbee avec l'endpoint Matter
    zigate_devices_set_matter_endpoint(zigbee_dev->ieee_addr, ep_id);

    ESP_LOGI(TAG, "Mapped Zigbee 0x%016llX -> Matter endpoint %d (type=0x%04X)",
             zigbee_dev->ieee_addr, ep_id, mtype);

    return ep_id;
}

int matter_device_manager_refresh_device(const zigate_device_t *zigbee_dev)
{
    if (!zigbee_dev) return -1;
    map_lock();
    int ep = -1;
    bridge_device_mapping_t *m = NULL;
    for (int i = 0; i < s_mapping_count; i++) {
        if (s_mappings[i].active && s_mappings[i].zigbee_ieee == zigbee_dev->ieee_addr) {
            m = &s_mappings[i];
            break;
        }
    }
    if (!m) {
        ep = map_device_locked(zigbee_dev);
    } else if (m->profile) {
        ep = m->matter_endpoint;                       // deja compose depuis son template
    } else {
        // Endpoint cree avec le type du DeviceId (template absent a
        // l'appairage, appareil endormi) : recompose sur le meme numero
        matter_profile_t *probe = matter_profile_build(device_data_get_template(zigbee_dev->ieee_addr),
                                                       zigbee_dev->model, zigbee_dev->device_id,
                                                       meter_role(zigbee_dev->ieee_addr));
        if (!probe) {
            ep = m->matter_endpoint;
        } else {
            free(probe);
            int old = m->matter_endpoint;
            matter_bridge_remove_endpoint(old);
            m->active = false;
            zigate_device_t snap = *zigbee_dev;
            snap.matter_endpoint = old;
            ep = map_device_locked(&snap);
            ESP_LOGI(TAG, "0x%016llX recompose depuis son template : endpoint %d -> %d",
                     (unsigned long long)zigbee_dev->ieee_addr, old, ep);
        }
    }
    map_unlock();
    return ep;
}

esp_err_t matter_device_manager_unmap_device(uint64_t ieee_addr)
{
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    map_lock();
    for (int i = 0; i < s_mapping_count; i++) {
        if (s_mappings[i].zigbee_ieee == ieee_addr && s_mappings[i].active) {
            matter_bridge_remove_endpoint(s_mappings[i].matter_endpoint);
            // Le profil n'est pas libere : un rapport en cours de traitement
            // par une autre tache peut encore le lire (quelques centaines
            // d'octets en PSRAM par appareil supprime)
            s_mappings[i].active = false;
            ESP_LOGI(TAG, "Unmapped device 0x%016llX from endpoint %d",
                     ieee_addr, s_mappings[i].matter_endpoint);
            ret = ESP_OK;
            break;
        }
    }
    map_unlock();
    return ret;
}

// ============================================================================
// Emetteurs LoRa (ZLinky LoRa)
// ============================================================================

int matter_device_manager_map_lora_device(uint64_t ieee, const char *model,
                                          const char *label, int preferred_ep)
{
    // Profil du template de l'emetteur (charge par le composant LoRa). Sans
    // template, le ZLinky LoRa reste expose comme compteur d'energie, comme le
    // ZLinky Zigbee ; un autre modele n'est pas ponte.
    if (matter_device_manager_is_excluded(ieee)) return -1;    // retire par l'utilisateur
    matter_profile_t *profile = matter_profile_build(device_data_get_template(ieee), model, 0,
                                                     meter_role(ieee));
    bool zlinky = model && strncmp(model, "ZLinky", 6) == 0;
    if (!profile && !zlinky) return -1;

    map_lock();
    int ep_id = -1;
    const bridge_device_mapping_t *known = matter_device_manager_get_mapping_by_ieee(ieee);
    const char *name = (label && label[0]) ? label : model;
    if (known) {
        ep_id = known->matter_endpoint;
        free(profile);
    } else if (s_mapping_count >= MATTER_BRIDGE_MAX_ENDPOINTS) {
        ESP_LOGE(TAG, "Max endpoints reached (%d)", MATTER_BRIDGE_MAX_ENDPOINTS);
        free(profile);
    } else {
        ep_id = profile ? matter_bridge_add_endpoint_profile(ieee, profile, name, preferred_ep)
                        : matter_bridge_add_endpoint(ieee, MATTER_DEV_ENERGY_METER, name, preferred_ep);
        if (ep_id < 0) {
            free(profile);
            profile = NULL;
        }
        if (ep_id >= 0) {
            bridge_device_mapping_t *m = &s_mappings[s_mapping_count++];
            m->matter_endpoint = ep_id;
            m->zigbee_ieee = ieee;
            m->zigbee_short_addr = 0xFFFF;      // pas d'adresse Zigbee
            m->device_type = MATTER_DEV_ENERGY_METER;
            m->active = true;
            m->profile = profile;
            m->last_lift100 = -1;
            m->last_move_us = 0;
            replay_stored_values(m);
            ESP_LOGI(TAG, "Mapped LoRa %016llX (%s) -> Matter endpoint %d",
                     (unsigned long long)ieee, model, ep_id);
        } else {
            ESP_LOGE(TAG, "Endpoint Matter impossible pour LoRa %016llX", (unsigned long long)ieee);
        }
    }
    map_unlock();
    return ep_id;
}

// ============================================================================
// Conversion Zigbee -> Matter (rapport d'attribut)
// ============================================================================

static esp_err_t apply_report(const bridge_device_mapping_t *mapping, const char *model,
                              const zigate_attribute_report_t *report);

esp_err_t matter_device_manager_handle_zigbee_report(const zigate_attribute_report_t *report)
{
    // Trouver le device par adresse courte
    zigate_device_t *dev = zigate_devices_find_by_short(report->short_addr);
    if (!dev) {
        ESP_LOGW(TAG, "No device found for short addr 0x%04X", report->short_addr);
        return ESP_ERR_NOT_FOUND;
    }

    // Trouver le mapping
    const bridge_device_mapping_t *mapping = matter_device_manager_get_mapping_by_ieee(dev->ieee_addr);
    if (!mapping || !mapping->active) {
        ESP_LOGD(TAG, "No active mapping for device 0x%016llX", dev->ieee_addr);
        return ESP_ERR_NOT_FOUND;
    }
    char model[sizeof(dev->model)];
    strlcpy(model, dev->model, sizeof(model));
    return apply_report(mapping, model, report);
}

esp_err_t matter_device_manager_handle_report_for_ieee(uint64_t ieee, const char *model,
                                                       const zigate_attribute_report_t *report)
{
    const bridge_device_mapping_t *mapping = matter_device_manager_get_mapping_by_ieee(ieee);
    if (!mapping || !mapping->active) return ESP_ERR_NOT_FOUND;
    return apply_report(mapping, model ? model : "", report);
}

// ----------------------------------------------------------------------------
// Rapport -> Matter par le profil du template
// ----------------------------------------------------------------------------

static void upd(int ep, uint32_t cluster, uint32_t attr, esp_matter_attr_val_t v)
{
    matter_bridge_update_attribute(ep, cluster, attr, &v, sizeof(v));
}

static int64_t clamp64(double v, int64_t lo, int64_t hi)
{
    if (v != v) return lo;                      // NaN
    double r = round(v);
    if (r < (double)lo) return lo;
    if (r > (double)hi) return hi;
    return (int64_t)r;
}

static void apply_link(bridge_device_mapping_t *m, const matter_link_t *l, double raw)
{
    const int ep = m->matter_endpoint;
    if (l->cluster == 0x0500) raw = (double)((uint64_t)raw & 1);   // zone IAS : alarme 1
    // Valeur physique dans l'unite de base de la cible (V, A, W, Wh, degC, %...)
    const double v = raw * (double)l->scale * (double)l->unit_mul;

    switch (l->target) {
    case ML_ONOFF:
        upd(ep, 0x0006, 0x0000, esp_matter_bool(raw != 0));
        break;
    case ML_TEMPERATURE:
        upd(ep, 0x0402, 0x0000, esp_matter_nullable_int16((int16_t)clamp64(v * 100, -27315, 32767)));
        break;
    case ML_HUMIDITY:
        upd(ep, 0x0405, 0x0000, esp_matter_nullable_uint16((uint16_t)clamp64(v * 100, 0, 10000)));
        break;
    case ML_PRESSURE:       // MeasuredValue en 0,1 kPa = hPa
        upd(ep, 0x0403, 0x0000, esp_matter_nullable_int16((int16_t)clamp64(v, -32767, 32767)));
        break;
    case ML_ILLUMINANCE: {  // 10000 x log10(lux) + 1, 0 = trop sombre
        uint16_t mv = v < 1.0 ? 0 : (uint16_t)clamp64(10000.0 * log10(v) + 1, 1, 0xFFFE);
        upd(ep, 0x0400, 0x0000, esp_matter_nullable_uint16(mv));
        break;
    }
    case ML_ILLUMINANCE_ZCL:
        upd(ep, 0x0400, 0x0000, esp_matter_nullable_uint16((uint16_t)clamp64(raw, 0, 0xFFFE)));
        break;
    case ML_OCCUPANCY:
        upd(ep, 0x0406, 0x0000, esp_matter_bitmap8(raw != 0 ? 1 : 0));
        break;
    case ML_CONTACT: {
        bool st = raw != 0;
        upd(ep, 0x0045, 0x0000, esp_matter_bool(l->invert ? !st : st));
        break;
    }
    case ML_LEAK:
        upd(ep, 0x0045, 0x0000, esp_matter_bool(raw != 0));
        break;
    case ML_BAT_PERCENT: {  // BatPercentRemaining en demi-pourcents
        upd(ep, 0x002F, 0x000C, esp_matter_nullable_uint8((uint8_t)clamp64(v * 2, 0, 200)));
        uint8_t level = v < 10 ? 2 : (v < 20 ? 1 : 0);   // critique / faible / ok
        upd(ep, 0x002F, 0x000E, esp_matter_enum8(level));
        break;
    }
    case ML_BAT_VOLTAGE:    // BatVoltage en mV
        upd(ep, 0x002F, 0x000B, esp_matter_nullable_uint32((uint32_t)clamp64(v * 1000, 0, UINT32_MAX - 1)));
        break;
    case ML_COVER_LIFT: {
        // Fermeture Matter (0 = ouvert) ; un appareil "inverse" remonte
        // l'ouverture
        int64_t pct = clamp64(v, 0, 100);
        if (m->profile->cover_inverted) pct = 100 - pct;
        m->last_lift100 = (int32_t)(pct * 100);
        upd(ep, 0x0102, 0x000E, esp_matter_nullable_uint16((uint16_t)(pct * 100)));
        upd(ep, 0x0102, 0x0008, esp_matter_nullable_uint8((uint8_t)pct));
        break;
    }
    case ML_TH_LOCAL_TEMP:
        upd(ep, 0x0201, 0x0000, esp_matter_nullable_int16((int16_t)clamp64(v * 100, -27315, 32767)));
        break;
    case ML_TH_HEAT_SP:
        upd(ep, 0x0201, 0x0012, esp_matter_int16((int16_t)clamp64(v * 100, -27315, 32767)));
        break;
    case ML_TH_COOL_SP:
        upd(ep, 0x0201, 0x0011, esp_matter_int16((int16_t)clamp64(v * 100, -27315, 32767)));
        break;
    case ML_TH_SYSTEM_MODE:  // enumeres ZCL et Matter identiques
        upd(ep, 0x0201, 0x001C, esp_matter_enum8((uint8_t)clamp64(raw, 0, 9)));
        break;
    case ML_TH_RUNNING:
        upd(ep, 0x0201, 0x0029, esp_matter_bitmap16((uint16_t)clamp64(raw, 0, 0xFFFF)));
        break;
    case ML_FAN_MODE:        // enumeres ZCL et Matter identiques
        upd(ep, 0x0202, 0x0000, esp_matter_enum8((uint8_t)clamp64(raw, 0, 6)));
        break;
    case ML_EL_ACTIVE_POWER:
    case ML_EL_APPARENT_POWER:
    case ML_EL_TOTAL_APPARENT:
    case ML_EL_VOLTAGE:
    case ML_EL_CURRENT: {
        ElecPowerDelegate *elec = matter_bridge_get_elec_delegate(ep);
        if (!elec) break;
        int64_t milli = clamp64(v * 1000, INT64_MIN / 2, INT64_MAX / 2);
        if (l->target == ML_EL_ACTIVE_POWER) elec->SetActivePower(milli);
        else if (l->target == ML_EL_APPARENT_POWER) elec->SetApparentPower(milli);
        else if (l->target == ML_EL_TOTAL_APPARENT) elec->SetTotalApparentPower(milli);
        else if (l->target == ML_EL_CURRENT) elec->SetRMSCurrent(milli);
        // 0 V : tension non transmise (TIC historique), "inconnu" plutot que 0
        else if (milli != 0) elec->SetRMSVoltage(milli);
        break;
    }
    case ML_EL_ENERGY:
        if (v >= 0) matter_bridge_report_energy_index(ep, l->energy_attr, (uint64_t)clamp64(v, 0, INT64_MAX / 2000));
        break;
    case ML_SWITCH_PRESS:
        matter_bridge_switch_event(ep, 1);
        break;
    case ML_SWITCH_MULTI: {  // nombre d'appuis, 0 = appui long ; autres valeurs ignorees
        int64_t n = clamp64(raw, -1, 255);
        if (n >= 0 && n <= 8) matter_bridge_switch_event(ep, (uint8_t)n);
        break;
    }
    default:
        break;
    }
}

/**
 * Publie les dernieres valeurs connues (device_data) des attributs lies, des
 * la creation de l'endpoint. Sans cela, apres un redemarrage du pont, Matter
 * restait sans valeur jusqu'au rapport suivant de l'appareil (index du ZLinky
 * rapportes toutes les 5 min, et pas du tout s'ils ne changent pas).
 */
static void replay_stored_values(bridge_device_mapping_t *m)
{
    const matter_profile_t *p = m->profile;
    if (!p) return;
    for (int i = 0; i < p->link_count; i++) {
        const matter_link_t *l = &p->links[i];
        // Evenements de bouton : un appui passe ne se rejoue pas
        if (l->target == ML_SWITCH_PRESS || l->target == ML_SWITCH_MULTI) continue;
        const devdata_attr_t *a = device_data_get_attr(m->zigbee_ieee, l->cluster, l->attr);
        if (!a || !a->value[0]) continue;
        // Valeur rangee en hexadecimal, octets dans l'ordre des rapports
        uint8_t data[16];
        size_t n = strlen(a->value) / 2;
        if (n == 0 || n > sizeof(data)) continue;
        bool ok = true;
        for (size_t k = 0; k < n && ok; k++) {
            char hx[3] = { a->value[2 * k], a->value[2 * k + 1], 0 };
            char *end;
            data[k] = (uint8_t)strtoul(hx, &end, 16);
            ok = (*end == '\0');
        }
        double raw;
        if (!ok || !matter_profile_raw_value(data, (uint16_t)n, a->type, &raw)) continue;
        apply_link(m, l, raw);
    }
}

/**
 * Appui d'un bouton range en TEXTE par le pont (SONOFF SNZB-01P / SNZB-01 /
 * WB01 : "single", "double", "long" ; telecommandes : "on", "off",
 * "toggle") : nombre d'appuis, 0 = appui long, -1 si le texte n'en est pas un.
 */
static int press_from_text(const uint8_t *data, uint16_t len)
{
    char t[24];
    uint16_t n = len < sizeof(t) - 1 ? len : (uint16_t)(sizeof(t) - 1);
    for (uint16_t i = 0; i < n; i++) t[i] = (char)tolower(data[i]);
    t[n] = '\0';
    if (strstr(t, "long") || strstr(t, "hold")) return 0;
    if (strstr(t, "quadruple")) return 4;
    if (strstr(t, "triple")) return 3;
    if (strstr(t, "double")) return 2;
    if (strstr(t, "single") || !strcmp(t, "on") || !strcmp(t, "off") || !strcmp(t, "toggle")) return 1;
    return -1;
}

static esp_err_t apply_profile(bridge_device_mapping_t *m, const zigate_attribute_report_t *report)
{
    const matter_profile_t *p = m->profile;
    double raw = 0;
    bool have_raw = false, found = false;
    for (int i = 0; i < p->link_count; i++) {
        const matter_link_t *l = &p->links[i];
        if (l->cluster != report->cluster_id || l->attr != report->attr_id) continue;
        found = true;
        if (!have_raw && (l->target == ML_SWITCH_PRESS || l->target == ML_SWITCH_MULTI) &&
            report->attr_type >= 0x41 && report->attr_type <= 0x44) {
            // Bouton : appui en texte -> evenements du cluster Switch (sinon
            // ignore comme valeur non numerique : le bouton restait muet)
            int pr = press_from_text(report->data, report->data_len);
            if (pr < 0) return ESP_ERR_INVALID_ARG;
            matter_bridge_switch_event(m->matter_endpoint, (uint8_t)pr);
            return ESP_OK;
        }
        if (!have_raw) {
            if (!matter_profile_raw_value(report->data, report->data_len, report->attr_type, &raw)) {
                ESP_LOGD(TAG, "%04X/%04X : valeur non numerique", report->cluster_id, report->attr_id);
                return ESP_ERR_INVALID_ARG;
            }
            have_raw = true;
        }
        apply_link(m, l, raw);
    }
    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

// Conversion du rapport (cluster/attribut ZCL) vers l'endpoint Matter mappe
static esp_err_t apply_report(const bridge_device_mapping_t *mapping, const char *model,
                              const zigate_attribute_report_t *report)
{
    // Profil du template : liaisons etat -> attribut Matter. Les entrees du
    // tableau des mappings ne bougent jamais : ecriture de last_lift100 sure.
    if (mapping->profile) return apply_profile((bridge_device_mapping_t *)mapping, report);

    // Convertir le cluster/attribut ZCL -> Matter et mettre a jour
    // Les clusters ZCL et Matter sont souvent identiques (meme heritage CHIP)
    esp_matter_attr_val_t val = {};

    switch (report->cluster_id) {
    case 0x0006:  // OnOff cluster
        if (report->attr_id == 0x0000) {  // OnOff attribute
            val.type = ESP_MATTER_VAL_TYPE_BOOLEAN;
            val.val.b = report->data[0] ? true : false;
            matter_bridge_update_attribute(mapping->matter_endpoint, 0x0006, 0x0000, &val, sizeof(val));
        }
        break;

    case 0x0008:  // Level Control
        if (report->attr_id == 0x0000) {  // CurrentLevel
            val.type = ESP_MATTER_VAL_TYPE_UINT8;
            val.val.u8 = report->data[0];
            matter_bridge_update_attribute(mapping->matter_endpoint, 0x0008, 0x0000, &val, sizeof(val));
        }
        break;

    case 0x0402:  // Temperature Measurement
        if (report->attr_id == 0x0000) {  // MeasuredValue (int16, 0.01 degres)
            val.type = ESP_MATTER_VAL_TYPE_INT16;
            val.val.i16 = (report->data[0] << 8) | report->data[1];
            matter_bridge_update_attribute(mapping->matter_endpoint, 0x0402, 0x0000, &val, sizeof(val));
        }
        break;

    case 0x0405:  // Relative Humidity
        if (report->attr_id == 0x0000) {
            val.type = ESP_MATTER_VAL_TYPE_UINT16;
            val.val.u16 = (report->data[0] << 8) | report->data[1];
            matter_bridge_update_attribute(mapping->matter_endpoint, 0x0405, 0x0000, &val, sizeof(val));
        }
        break;

    case 0x0406:  // Occupancy Sensing
        if (report->attr_id == 0x0000) {
            val.type = ESP_MATTER_VAL_TYPE_UINT8;
            val.val.u8 = report->data[0];
            matter_bridge_update_attribute(mapping->matter_endpoint, 0x0406, 0x0000, &val, sizeof(val));
        }
        break;

    case 0x0101:  // Door Lock
        if (report->attr_id == 0x0000) {  // LockState
            val.type = ESP_MATTER_VAL_TYPE_UINT8;
            val.val.u8 = report->data[0];
            matter_bridge_update_attribute(mapping->matter_endpoint, 0x0101, 0x0000, &val, sizeof(val));
        }
        break;

    case 0x0102:  // Window Covering
        if (report->attr_id == 0x0008) {  // CurrentPositionLiftPercentage
            val.type = ESP_MATTER_VAL_TYPE_UINT8;
            val.val.u8 = report->data[0];
            matter_bridge_update_attribute(mapping->matter_endpoint, 0x0102, 0x0008, &val, sizeof(val));
        }
        break;

    // ---- ZLinky / Energy Metering ----
    // Clusters ZCL 0x0702/0x0B04 -> delegate ElectricalPowerMeasurement
    // Les attributs sont MANAGED_INTERNALLY par le framework, on met a jour via le delegate
    case 0x0702: {
        ElecPowerDelegate *elec = matter_bridge_get_elec_delegate(mapping->matter_endpoint);
        if (!elec) break;
        uint64_t v;
        if (report->attr_id == 0x0400) {  // InstantaneousDemand (W, int24 signe)
            if (!be_uint(report, 3, &v)) break;
            int32_t demand = (int32_t)(v & 0xFFFFFF);
            if (demand & 0x800000) demand |= (int32_t)0xFF000000;
            elec->SetActivePower((int64_t)demand * 1000);  // W -> mW
            ESP_LOGI(TAG, "ZLinky InstantaneousDemand: %ld W", (long)demand);
        } else if (report->attr_id == 0x0000 ||
                   (report->attr_id >= 0x0100 && report->attr_id <= 0x0112 &&
                    !(report->attr_id & 1))) {
            // Index d'energie (uint48, Wh) : CurrentSummationDelivered (0x0000)
            // ou index tarifaires. En ZCL, les index "Delivered" sont aux
            // adresses PAIRES 0x0100..0x0112 (Tier1..Tier10) ; les impaires
            // sont les index "Received".
            if (!be_uint(report, 1, &v)) break;
            ESP_LOGI(TAG, "ZLinky index 0x%04X : %llu Wh", report->attr_id, (unsigned long long)v);
            matter_bridge_report_energy_index(mapping->matter_endpoint, report->attr_id, v);
        }
        break;
    }

    case 0x0B04: {
        ElecPowerDelegate *elec = matter_bridge_get_elec_delegate(mapping->matter_endpoint);
        if (!elec) break;
        uint64_t v;
        if (!be_uint(report, 2, &v)) break;
        if (report->attr_id == 0x0505) {  // RMSVoltage (V, uint16)
            uint16_t voltage = (uint16_t)v;
            // 0 V : pas de tension disponible. La TIC historique n'en transmet
            // pas, et l'attribut configure en reporting vaut alors 0. Publier
            // 0 V ferait afficher une tension nulle au lieu d'"inconnu".
            if (voltage == 0) break;
            elec->SetRMSVoltage((int64_t)voltage * 1000);  // V -> mV
            ESP_LOGI(TAG, "ZLinky RMSVoltage: %u V", voltage);
        } else if (report->attr_id == 0x0508) {  // RMSCurrent (A, uint16)
            uint16_t current = (uint16_t)v;
            elec->SetRMSCurrent((int64_t)current * 1000);  // A -> mA
            ESP_LOGI(TAG, "ZLinky RMSCurrent: %u A", current);
        } else if (report->attr_id == 0x050B) {  // ActivePower (W, int16)
            int16_t power = (int16_t)(uint16_t)v;
            // ZLinky : 0x050B contient CCASN, un point de la courbe de charge
            // moyenne sur la periode d'integration, et non une puissance
            // instantanee. Il ne doit pas remplacer la puissance affichee.
            // Pour un autre compteur (prise mesuree...), c'est une vraie
            // puissance active instantanee.
            if (strncmp(model, "ZLinky", 6) == 0) {
                ESP_LOGD(TAG, "ZLinky CCASN: %d W (non publie comme puissance)", power);
                break;
            }
            elec->SetActivePower((int64_t)power * 1000);  // W -> mW
            ESP_LOGI(TAG, "ActivePower: %d W", power);
        } else if (report->attr_id == 0x0306) {  // TotalApparentPower (VA, uint32)
            // SINSTS total : prioritaire sur 0x050F, qui ne contient que la
            // phase 1 en triphase standard.
            elec->SetTotalApparentPower((int64_t)v * 1000);  // VA -> mVA
            ESP_LOGI(TAG, "ZLinky TotalApparentPower: %llu VA", (unsigned long long)v);
        } else if (report->attr_id == 0x050F) {  // ApparentPower (VA, uint16)
            uint16_t apparent = (uint16_t)v;
            elec->SetApparentPower((int64_t)apparent * 1000);  // VA -> mVA
            ESP_LOGI(TAG, "ZLinky ApparentPower: %u VA", apparent);
        }
        break;
    }

    default:
        ESP_LOGD(TAG, "Unhandled ZCL cluster 0x%04X for Matter conversion", report->cluster_id);
        break;
    }

    return ESP_OK;
}

// ============================================================================
// Conversion Matter -> Zigbee (commande du controleur)
// ============================================================================

// ----------------------------------------------------------------------------
// Commande Matter -> Zigbee par le profil
// ----------------------------------------------------------------------------

static const matter_link_t *find_link(const matter_profile_t *p, uint8_t target)
{
    for (int i = 0; i < p->link_count; i++)
        if (p->links[i].target == target) return &p->links[i];
    return NULL;
}

static esp_err_t send_cmd(const matter_cmd_t *c, uint16_t short_addr, uint8_t dev_ep, int32_t value)
{
    return zigate_action_send(c->command, short_addr, c->endpoint ? c->endpoint : dev_ep,
                              value, c->cluster, c->mfr);
}

// Ecrit la valeur physique 'phys' (unite de base de la cible) dans l'etat lie :
// datapoint Tuya pour EF00, ecriture d'attribut ZCL sinon
static esp_err_t write_link(const matter_link_t *l, uint16_t short_addr, uint8_t dev_ep,
                            double phys, uint8_t default_type)
{
    double div = (double)l->scale * (double)l->unit_mul;
    if (div == 0) div = 1;
    int64_t raw = (int64_t)llround(phys / div);
    if (l->cluster == 0xEF00) {
        uint8_t dp_type = (l->write_type >= 1 && l->write_type <= 5) ? l->write_type : TUYA_DP_VALUE;
        return zigate_tuya_dp_set(short_addr, 1, (uint8_t)l->attr, dp_type, (uint32_t)raw);
    }
    uint8_t type = l->write_type ? l->write_type : default_type;
    char txt[24];
    snprintf(txt, sizeof(txt), "%lld", (long long)raw);
    uint8_t buf[8];
    int n = zigate_zcl_encode(type, txt, buf, sizeof(buf));
    if (n <= 0) return ESP_ERR_INVALID_ARG;
    return zigate_write_attribute_mfr(short_addr, dev_ep, l->cluster, l->attr, type, buf, (uint16_t)n,
                                      l->mfr);
}

static esp_err_t profile_command(bridge_device_mapping_t *m, uint32_t cluster_id, uint32_t attr_id,
                                 const esp_matter_attr_val_t *val)
{
    const matter_profile_t *p = m->profile;
    if (m->zigbee_short_addr == 0xFFFF) return ESP_OK;     // emetteur LoRa : pas de commande

    // Adresse et endpoint courants (copies : la base peut bouger)
    zigate_device_t *dev = zigate_devices_find_by_ieee(m->zigbee_ieee);
    if (!dev) return ESP_ERR_NOT_FOUND;
    const uint16_t sa = dev->short_addr;
    const uint8_t dep = dev->endpoint;
    esp_err_t err = ESP_OK;

    switch (cluster_id) {
    case 0x0006:    // OnOff
        if (attr_id == 0x0000) {
            bool on = val->val.b;
            const matter_cmd_t *c = on ? &p->on : &p->off;
            err = c->valid ? send_cmd(c, sa, dep, c->value) : zigate_send_onoff(sa, dep, on ? 1 : 0);
        }
        break;

    case 0x0102:    // WindowCovering : la cible change sur UpOrOpen / DownOrClose / GoTo / Stop
        if (attr_id == 0x000B) {
            if (val->val.u16 > 10000) break;           // nul
            int closure = val->val.u16 / 100;
            const int64_t now = esp_timer_get_time();
            // StopMotion : la cible devient la position courante. Seulement si
            // le pont a mis le volet en mouvement recemment : une position
            // memorisee perimee (volet manoeuvre a la main, rapport perdu)
            // transformait un UpOrOpen en Stop, et le volet ne bougeait pas.
            bool moving = m->last_move_us && now - m->last_move_us < 120LL * 1000000;
            if ((int32_t)val->val.u16 == m->last_lift100 && moving && p->cover_stop.valid) {
                err = send_cmd(&p->cover_stop, sa, dep, p->cover_stop.value);       // Stop
                m->last_move_us = 0;
            } else if (closure == 0 && p->cover_up.valid) {
                // Ouverture / fermeture complete : commandes UP / DOWN du template,
                // celles des boutons de la fiche. Le positionnement (Go To Lift)
                // n'est execute que par un volet calibre (SONOFF MINI-ZBRBS...).
                err = send_cmd(&p->cover_up, sa, dep, p->cover_up.value);
                m->last_move_us = now;
            } else if (closure == 100 && p->cover_down.valid) {
                err = send_cmd(&p->cover_down, sa, dep, p->cover_down.value);
                m->last_move_us = now;
            } else if (p->cover_pos.valid) {
                err = send_cmd(&p->cover_pos, sa, dep, 100 - closure);             // ouverture
                m->last_move_us = now;
            } else {
                uint8_t lift = (uint8_t)(p->cover_inverted ? 100 - closure : closure);
                err = zigate_send_window_covering(sa, dep, 0x05, &lift, 1);         // Go To Lift %
                m->last_move_us = now;
            }
        }
        break;

    case 0x0201:    // Thermostat
        if (attr_id == 0x0012 || attr_id == 0x0011) {
            bool heat = (attr_id == 0x0012);
            int16_t sp = val->val.i16;                  // 0,01 degC
            const matter_cmd_t *c = heat ? &p->heat_sp : &p->cool_sp;
            if (!c->valid) c = &p->both_sp;
            const matter_link_t *l = find_link(p, heat ? ML_TH_HEAT_SP : ML_TH_COOL_SP);
            if (c->valid) err = send_cmd(c, sa, dep, sp);
            else if (l) err = write_link(l, sa, dep, sp / 100.0, 0x29);
        } else if (attr_id == 0x001C) {
            uint8_t mode = val->val.u8;
            const matter_link_t *l = find_link(p, ML_TH_SYSTEM_MODE);
            if (p->sys_mode.valid) err = send_cmd(&p->sys_mode, sa, dep, mode);
            else if (l && l->cluster == 0x0201) err = write_link(l, sa, dep, mode, 0x30);
        }
        break;

    case 0x0202:    // FanControl
        if (attr_id == 0x0000) {
            uint8_t mode = val->val.u8;
            const matter_link_t *l = find_link(p, ML_FAN_MODE);
            if (p->fan_mode.valid) err = send_cmd(&p->fan_mode, sa, dep, mode);
            else if (l) err = write_link(l, sa, dep, mode, 0x30);
        }
        break;

    default:
        break;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Commande Matter ep=%d %04lX/%04lX -> Zigbee : %s", m->matter_endpoint,
                 (unsigned long)cluster_id, (unsigned long)attr_id, esp_err_to_name(err));
    }
    return ESP_OK;
}

esp_err_t matter_device_manager_handle_matter_command(int matter_endpoint,
                                                       uint32_t cluster_id,
                                                       uint32_t command_id,
                                                       const void *data, size_t data_len)
{
    const bridge_device_mapping_t *mapping = matter_device_manager_get_mapping(matter_endpoint);
    if (!mapping || !mapping->active) return ESP_ERR_NOT_FOUND;
    if (mapping->profile) {
        return profile_command((bridge_device_mapping_t *)mapping, cluster_id, command_id,
                               (const esp_matter_attr_val_t *)data);
    }

    zigate_device_t *dev = zigate_devices_find_by_ieee(mapping->zigbee_ieee);
    if (!dev) return ESP_ERR_NOT_FOUND;

    esp_matter_attr_val_t *val = (esp_matter_attr_val_t *)data;

    switch (cluster_id) {
    case 0x0006:  // OnOff
        if (command_id == 0x0000) {  // OnOff attribute write
            uint8_t action = val->val.b ? 1 : 0;
            zigate_send_onoff(dev->short_addr, dev->endpoint, action);
        }
        break;

    // Appareil sans template : seul le marche/arret est relaye

    default:
        ESP_LOGW(TAG, "Unhandled Matter cluster 0x%04lX -> Zigbee", cluster_id);
        break;
    }

    return ESP_OK;
}

// ============================================================================
// Lookups
// ============================================================================

const bridge_device_mapping_t* matter_device_manager_get_mapping(int matter_endpoint)
{
    for (int i = 0; i < s_mapping_count; i++) {
        if (s_mappings[i].matter_endpoint == matter_endpoint && s_mappings[i].active) {
            return &s_mappings[i];
        }
    }
    return NULL;
}

const bridge_device_mapping_t* matter_device_manager_get_mapping_by_ieee(uint64_t ieee_addr)
{
    for (int i = 0; i < s_mapping_count; i++) {
        if (s_mappings[i].zigbee_ieee == ieee_addr && s_mappings[i].active) {
            return &s_mappings[i];
        }
    }
    return NULL;
}

esp_err_t matter_device_manager_save(void)
{
    // TODO: sauvegarder en NVS
    return ESP_OK;
}
