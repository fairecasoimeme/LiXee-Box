// ============================================================================
// Matter Bridge - Stack Matter en mode Bridge (device type 0x000E)
// Utilise le SDK esp-matter d'Espressif
//
// Les sub-devices (endpoints Zigbee) sont crees comme des "bridged_node"
// (device type 0x0013) avec les clusters du type de device reel.
//
// Les endpoints des devices sont crees APRES esp_matter::start() : c'est le
// seul moment ou esp-matter connait les numeros deja attribues (compteur
// persistant relu au demarrage), ce qui permet de reprendre le meme endpoint
// (resume) d'un redemarrage a l'autre. Ils sont ensuite actives via
// endpoint::enable().
//
// Toute manipulation du modele de donnees apres start() se fait sous le verrou
// de la pile CHIP, qui tourne dans sa propre tache.
// ============================================================================

#include "matter_bridge.h"
#include "matter_device_manager.h"
#include "elec_power_delegate.h"
#include "zigate_devices.h"
#include "device_data.h"
#include "esp_log.h"
#include "esp_err.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// esp-matter SDK
#include <esp_matter.h>
#include <esp_matter_core.h>
#include <esp_matter_endpoint.h>
#include <esp_matter_feature.h>
#include <esp_matter_event.h>
#include <app/clusters/electrical-energy-measurement-server/ElectricalEnergyMeasurementCluster.h>
#include <app/clusters/meter-identification-server/meter-identification-server.h>
#include <app/clusters/commodity-metering-server/commodity-metering-server.h>
#include <app/reporting/reporting.h>
#include <app/server/Server.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <platform/DeviceInstanceInfoProvider.h>
#include <platform/ConnectivityManager.h>
#include <platform/CommissionableDataProvider.h>
#include <crypto/CHIPCryptoPAL.h>
#include <setup_payload/SetupPayload.h>
#include <esp_matter_providers.h>
#include "nvs.h"
#include "esp_random.h"

using namespace esp_matter;
using namespace chip::app::Clusters;

static const char *TAG = "MATTER_BRIDGE";

static node_t *s_matter_node = NULL;
static endpoint_t *s_aggregator_endpoint = NULL;
static uint16_t s_aggregator_ep_id = 0;
static bool s_matter_started = false;

// Codes d'appairage, calcules une fois apres start() (vides avant)
static char s_qr_code[64];       // "MT:..." (charge utile du QR code)
static char s_manual_code[32];   // 11 chiffres

// Vrai pendant qu'une valeur venue de Zigbee est ecrite dans Matter. Ecrit et
// lu sous le verrou de la pile, donc jamais vu a vrai par une ecriture venue
// d'un controleur (traitee par la tache CHIP, qui attend ce verrou).
static bool s_zigbee_update_in_progress = false;

// Delegates pour les endpoints energy meter (un par endpoint max)
static ElecPowerDelegate *s_elec_delegates[MATTER_BRIDGE_MAX_ENDPOINTS] = {};
static int s_elec_delegate_ep_ids[MATTER_BRIDGE_MAX_ENDPOINTS] = {};
static int s_elec_delegate_count = 0;

// Energie cumulee : un acces partage pour tous les endpoints compteur, et
// l'etat des index Zigbee par endpoint.
static ElectricalEnergyMeasurement::ElectricalEnergyMeasurementAttrAccess *s_eem_access = nullptr;

#define ENERGY_TIER_COUNT 10   // Tier1..Tier10 = attributs PAIRS 0x0100..0x0112

typedef struct {
    int      endpoint_id;
    bool     has_total;                 // 0x0000 (CurrentSummationDelivered) recu
    uint64_t total_wh;
    uint64_t tier_wh[ENERGY_TIER_COUNT];
    uint16_t tier_mask;
    int64_t  published_mwh;             // derniere valeur publiee, -1 si aucune
} energy_state_t;

static energy_state_t s_energy[MATTER_BRIDGE_MAX_ENDPOINTS];

// esp-matter 1.5.1 reference ce callback (cluster door_lock) sans plus le
// fournir ; la 1.4.2 en avait une version faible vide. Meme comportement.
void emberAfDoorLockClusterInitCallback(chip::EndpointId endpoint) {}
static int s_energy_count = 0;

// ============================================================================
// Verrou de la pile CHIP
// ============================================================================

// 'held' indique s'il faut relacher le verrou (faux si la tache courante le
// tenait deja). esp-matter 1.5 ne publie plus chip_stack_lock() : meme logique
// que lock::ScopedChipStackLock, avec PlatformMgr.
static bool stack_lock(bool *held)
{
    *held = lixee_chip_lock();
    return true;
}

static void stack_unlock(bool held)
{
    lixee_chip_unlock(held);
}

// ============================================================================
// Callback Matter: commande recue d'un controleur Matter
// ============================================================================

// Modules d'endpoints virtuels : chacun repond ESP_ERR_NOT_FOUND pour un
// endpoint qui n'est pas le sien
#define VIRTUAL_CB_MAX 4
static matter_virtual_write_cb_t s_virtual_cb[VIRTUAL_CB_MAX];

void matter_bridge_set_virtual_write_cb(matter_virtual_write_cb_t cb)
{
    for (int i = 0; i < VIRTUAL_CB_MAX; i++) {
        if (s_virtual_cb[i] == cb) return;
        if (!s_virtual_cb[i]) { s_virtual_cb[i] = cb; return; }
    }
    ESP_LOGE(TAG, "Trop de modules d'endpoints virtuels");
}

/** Valeur numerique d'un attribut Matter (entiers, enum, bitmap, nullable). */
static int32_t attr_number(const esp_matter_attr_val_t *v, bool *is_null)
{
    *is_null = false;
    switch (v->type) {
    case ESP_MATTER_VAL_TYPE_BOOLEAN:          return v->val.b ? 1 : 0;
    case ESP_MATTER_VAL_TYPE_INT8:             return v->val.i8;
    case ESP_MATTER_VAL_TYPE_INT16:            return v->val.i16;
    case ESP_MATTER_VAL_TYPE_NULLABLE_INT16:
        *is_null = chip::app::NumericAttributeTraits<int16_t>::IsNullValue(v->val.i16);
        return v->val.i16;
    case ESP_MATTER_VAL_TYPE_UINT8:
    case ESP_MATTER_VAL_TYPE_ENUM8:
    case ESP_MATTER_VAL_TYPE_BITMAP8:
    case ESP_MATTER_VAL_TYPE_NULLABLE_UINT8:
    case ESP_MATTER_VAL_TYPE_NULLABLE_ENUM8:   return v->val.u8;
    case ESP_MATTER_VAL_TYPE_UINT16:
    case ESP_MATTER_VAL_TYPE_BITMAP16:
    case ESP_MATTER_VAL_TYPE_ENUM16:           return v->val.u16;
    case ESP_MATTER_VAL_TYPE_INT32:            return v->val.i32;
    case ESP_MATTER_VAL_TYPE_UINT32:           return (int32_t)v->val.u32;
    default:                                   return 0;
    }
}

static esp_err_t matter_attribute_update_cb(attribute::callback_type_t type,
                                              uint16_t endpoint_id,
                                              uint32_t cluster_id,
                                              uint32_t attribute_id,
                                              esp_matter_attr_val_t *val,
                                              void *priv_data)
{
    // Ignorer les endpoints systeme (0 = root node, 1 = aggregator bridge)
    // Seuls les endpoints dynamiques (>=2) correspondent a des devices Zigbee
    if (endpoint_id <= 1) {
        return ESP_OK;
    }

    // Ecriture provoquee par un rapport Zigbee (matter_bridge_update_attribute) :
    // attribute::update() passe par ce callback, et la renvoyer vers Zigbee
    // creerait un echo, voire une boucle (l'appareil re-signale son etat, qui
    // est re-ecrit, qui est re-envoye...).
    if (s_zigbee_update_in_progress) {
        return ESP_OK;
    }

    if (type == attribute::PRE_UPDATE) {
        ESP_LOGD(TAG, "Matter PRE_UPDATE: ep=%d cluster=0x%04lX attr=0x%04lX",
                 endpoint_id, cluster_id, attribute_id);

        // Endpoint virtuel (thermostat du pont) : traite par son module
        if (val) {
            bool is_null;
            int32_t num = attr_number(val, &is_null);
            for (int i = 0; i < VIRTUAL_CB_MAX && s_virtual_cb[i]; i++) {
                esp_err_t r = s_virtual_cb[i](endpoint_id, cluster_id, attribute_id, num, is_null);
                if (r != ESP_ERR_NOT_FOUND) return r;
            }
        }

        // Propager la commande vers le device Zigbee
        return matter_device_manager_handle_matter_command(
            endpoint_id, cluster_id, attribute_id, val, sizeof(*val));
    }
    return ESP_OK;
}

static esp_err_t matter_identification_cb(identification::callback_type_t type,
                                            uint16_t endpoint_id,
                                            uint8_t effect_id,
                                            uint8_t effect_variant,
                                            void *priv_data)
{
    ESP_LOGI(TAG, "Identify: ep=%d effect=%d", endpoint_id, effect_id);
    return ESP_OK;
}

// ============================================================================
// Helper: configurer le parent endpoint pour un bridged node
// ============================================================================

static esp_err_t set_parent_endpoint(endpoint_t *child_ep, endpoint_t *parent_ep)
{
    if (!child_ep || !parent_ep) return ESP_ERR_INVALID_ARG;
    return endpoint::set_parent_endpoint(child_ep, parent_ep);
}

// ============================================================================
// Helper: identite de l'appareil ponte (Bridged Device Basic Information)
//
// Sans ces attributs, Home Assistant affichait des appareils generiques sans
// nom, et le parametre label etait recu puis ignore.
// ============================================================================

static void set_bridged_identity(endpoint_t *ep, uint64_t ieee, const char *label, const char *product)
{
    cluster_t *bdbi = cluster::get(ep, BridgedDeviceBasicInformation::Id);
    if (!bdbi) return;

    // UniqueID : identifiant stable de l'appareil ponte (son IEEE Zigbee).
    // esp-matter le cree vide.
    char uid[17];
    snprintf(uid, sizeof(uid), "%016llX", (unsigned long long)ieee);
    attribute_t *uid_attr = attribute::get(bdbi, BridgedDeviceBasicInformation::Attributes::UniqueID::Id);
    if (uid_attr) {
        esp_matter_attr_val_t v = esp_matter_char_str(uid, (uint16_t)strlen(uid));
        attribute::set_val(uid_attr, &v);
    }

    // NodeLabel : nom affiche par le controleur. Attribut persistant : un nom
    // donne depuis le controleur, deja en NVS, prend le pas sur celui-ci.
    char name[33];
    if (label && label[0]) {
        strlcpy(name, label, sizeof(name));
    } else {
        snprintf(name, sizeof(name), "Zigbee %016llX", (unsigned long long)ieee);
    }
    cluster::bridged_device_basic_information::attribute::create_node_label(
        bdbi, name, (uint16_t)strlen(name));

    // Fabricant et modele, connus de la decouverte Zigbee
    zigate_device_t *dev = product ? NULL : zigate_devices_find_by_ieee(ieee);
    if (product) {
        // Endpoint virtuel (appareil propre au pont)
        cluster::bridged_device_basic_information::attribute::create_vendor_name(
            bdbi, MATTER_BRIDGE_VENDOR_NAME, (uint16_t)strlen(MATTER_BRIDGE_VENDOR_NAME));
        char buf[33];
        strlcpy(buf, product, sizeof(buf));
        cluster::bridged_device_basic_information::attribute::create_product_name(
            bdbi, buf, (uint16_t)strlen(buf));
    } else if (dev) {
        char buf[33];
        // Fabricant/modele Zigbee : connus de l'interview s'ils sont arrives
        // avant la creation de cet endpoint, sinon rattrapes plus tard par
        // matter_bridge_update_bridged_identity() quand le rapport du cluster
        // Basic (0x0000, attributs 0x0004/0x0005) arrive - voir son appel dans
        // matter_device_manager.cpp. Mettre une valeur de remplacement ici
        // (ex. MATTER_BRIDGE_VENDOR_NAME) serait trompeur : l'attribut
        // afficherait un fabricant incorrect au lieu d'etre simplement absent
        // jusqu'a ce qu'il soit connu.
        if (dev->manufacturer[0]) {
            strlcpy(buf, dev->manufacturer, sizeof(buf));
            cluster::bridged_device_basic_information::attribute::create_vendor_name(
                bdbi, buf, (uint16_t)strlen(buf));
        }
        if (dev->model[0]) {
            strlcpy(buf, dev->model, sizeof(buf));
            cluster::bridged_device_basic_information::attribute::create_product_name(
                bdbi, buf, (uint16_t)strlen(buf));
        }
    }
}

// ============================================================================
// Helper: completer/mettre a jour VendorName et ProductName (BridgedDeviceBasic
// Information, 0x0039) apres coup, quand le fabricant/modele Zigbee arrive
// apres la creation de l'endpoint (set_bridged_identity() ne s'execute qu'une
// fois). Cree l'attribut s'il n'existait pas encore, sinon met a jour sa
// valeur. vendor/model peuvent etre NULL ou vides (aucune action pour ce
// champ dans ce cas).
// ============================================================================

esp_err_t matter_bridge_update_bridged_identity(int endpoint_id, const char *vendor, const char *model)
{
    if (!s_matter_node || !s_matter_started) return ESP_ERR_INVALID_STATE;
    bool held;
    if (!stack_lock(&held)) return ESP_FAIL;

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    endpoint_t *ep = endpoint::get(s_matter_node, endpoint_id);
    cluster_t *bdbi = ep ? cluster::get(ep, BridgedDeviceBasicInformation::Id) : NULL;
    if (bdbi) {
        ret = ESP_OK;
        char buf[33];
        if (vendor && vendor[0]) {
            strlcpy(buf, vendor, sizeof(buf));
            attribute_t *attr = attribute::get(bdbi, BridgedDeviceBasicInformation::Attributes::VendorName::Id);
            if (attr) {
                esp_matter_attr_val_t v = esp_matter_char_str(buf, (uint16_t)strlen(buf));
                s_zigbee_update_in_progress = true;
                attribute::update(endpoint_id, BridgedDeviceBasicInformation::Id,
                                  BridgedDeviceBasicInformation::Attributes::VendorName::Id, &v);
                s_zigbee_update_in_progress = false;
            } else {
                cluster::bridged_device_basic_information::attribute::create_vendor_name(
                    bdbi, buf, (uint16_t)strlen(buf));
            }
        }
        if (model && model[0]) {
            strlcpy(buf, model, sizeof(buf));
            attribute_t *attr = attribute::get(bdbi, BridgedDeviceBasicInformation::Attributes::ProductName::Id);
            if (attr) {
                esp_matter_attr_val_t v = esp_matter_char_str(buf, (uint16_t)strlen(buf));
                s_zigbee_update_in_progress = true;
                attribute::update(endpoint_id, BridgedDeviceBasicInformation::Id,
                                  BridgedDeviceBasicInformation::Attributes::ProductName::Id, &v);
                s_zigbee_update_in_progress = false;
            } else {
                cluster::bridged_device_basic_information::attribute::create_product_name(
                    bdbi, buf, (uint16_t)strlen(buf));
            }
        }
    }

    stack_unlock(held);
    return ret;
}

// ============================================================================
// Helper: ajouter les clusters specifiques au type de device
// sur un endpoint bridged_node deja cree
// ============================================================================

// Matter 1.5 : compteur du distributeur (device type Electrical Utility Meter)
// avec Meter Identification (n de serie, puissance souscrite) et Commodity
// Metering (index par tarif). Valeurs : linky_apply().
static void add_linky_clusters(endpoint_t *ep)
{
    endpoint::electrical_utility_meter::config_t um_cfg;
    endpoint::electrical_utility_meter::add(ep, &um_cfg);
    cluster_t *mi = cluster::get(ep, MeterIdentification::Id);
    if (mi) {
        cluster::meter_identification::feature::power_threshold::add(mi);
        // Attribut facultatif (TIC historique / standard), absent sans cela
        cluster::meter_identification::attribute::create_protocol_version(mi, NULL, 0);
    }
    cluster::commodity_metering::config_t cm_cfg;
    cluster::commodity_metering::create(ep, &cm_cfg, CLUSTER_FLAG_SERVER);
}

static esp_err_t add_device_type_clusters(endpoint_t *ep, matter_device_type_t device_type)
{
    switch (device_type) {
    case MATTER_DEV_ON_OFF_LIGHT: {
        // Ajouter device type on_off_light + clusters
        endpoint::on_off_light::config_t cfg;
        endpoint::on_off_light::add(ep, &cfg);
        break;
    }
    case MATTER_DEV_DIMMABLE_LIGHT: {
        endpoint::dimmable_light::config_t cfg;
        endpoint::dimmable_light::add(ep, &cfg);
        break;
    }
    case MATTER_DEV_ON_OFF_PLUG: {
        endpoint::on_off_plug_in_unit::config_t cfg;
        endpoint::on_off_plug_in_unit::add(ep, &cfg);
        break;
    }
    case MATTER_DEV_TEMPERATURE_SENSOR: {
        endpoint::temperature_sensor::config_t cfg;
        endpoint::temperature_sensor::add(ep, &cfg);
        break;
    }
    case MATTER_DEV_HUMIDITY_SENSOR: {
        endpoint::humidity_sensor::config_t cfg;
        endpoint::humidity_sensor::add(ep, &cfg);
        break;
    }
    case MATTER_DEV_CONTACT_SENSOR: {
        endpoint::contact_sensor::config_t cfg;
        endpoint::contact_sensor::add(ep, &cfg);
        break;
    }
    case MATTER_DEV_OCCUPANCY_SENSOR: {
        endpoint::occupancy_sensor::config_t cfg;
        endpoint::occupancy_sensor::add(ep, &cfg);
        break;
    }
    case MATTER_DEV_DOOR_LOCK: {
        endpoint::door_lock::config_t cfg;
        endpoint::door_lock::add(ep, &cfg);
        break;
    }
    case MATTER_DEV_WINDOW_COVERING: {
        endpoint::window_covering::config_t cfg;
        endpoint::window_covering::add(ep, &cfg);
        break;
    }
    case MATTER_DEV_THERMOSTAT: {
        endpoint::thermostat::config_t cfg;
        endpoint::thermostat::add(ep, &cfg);
        break;
    }
    case MATTER_DEV_ENERGY_METER: {
        // Creer un delegate pour stocker les valeurs de mesure
        ElecPowerDelegate *delegate = new ElecPowerDelegate();
        endpoint::electrical_sensor::config_t cfg;
        cfg.electrical_power_measurement.feature_flags =
            cluster::electrical_power_measurement::feature::alternating_current::get_id();
        cfg.electrical_power_measurement.delegate = delegate;
        endpoint::electrical_sensor::add(ep, &cfg);

        // Attributs optionnels de puissance : le serveur n'expose que ceux qui
        // existent sur l'endpoint. Sans eux, seule la puissance active etait
        // visible ; tension, courant et puissance apparente recus du ZLinky
        // restaient invisibles pour les controleurs.
        cluster_t *epm = cluster::get(ep, ElectricalPowerMeasurement::Id);
        if (epm) {
            cluster::electrical_power_measurement::attribute::create_voltage(epm, nullable<int64_t>());
            cluster::electrical_power_measurement::attribute::create_active_current(epm, nullable<int64_t>());
            cluster::electrical_power_measurement::attribute::create_apparent_power(epm, nullable<int64_t>());
        }

        // Energie cumulee importee (index du compteur, en mWh). Absente du
        // device type electrical_sensor en esp-matter 1.4.2, alors que c'est la
        // donnee principale d'un Linky et celle du tableau Energie de Home
        // Assistant.
        cluster::electrical_energy_measurement::config_t eem_cfg;
        eem_cfg.feature_flags =
            cluster::electrical_energy_measurement::feature::imported_energy::get_id() |
            cluster::electrical_energy_measurement::feature::cumulative_energy::get_id();
        cluster::electrical_energy_measurement::create(ep, &eem_cfg, CLUSTER_FLAG_SERVER);

        add_linky_clusters(ep);

        // Stocker le delegate pour pouvoir le mettre a jour plus tard
        if (s_elec_delegate_count < MATTER_BRIDGE_MAX_ENDPOINTS) {
            s_elec_delegates[s_elec_delegate_count] = delegate;
            s_elec_delegate_ep_ids[s_elec_delegate_count] = endpoint::get_id(ep);
            s_elec_delegate_count++;
        }
        break;
    }
    default:
        ESP_LOGW(TAG, "Unsupported device type 0x%04X, creating as on_off_plugin_unit",
                 device_type);
        endpoint::on_off_plug_in_unit::config_t cfg;
        endpoint::on_off_plug_in_unit::add(ep, &cfg);
        break;
    }
    return ESP_OK;
}

// ============================================================================
// Helper: clusters d'un endpoint construit depuis le profil du template
//
// Plusieurs types d'appareil sur le meme endpoint ponte (capteur temperature
// + humidite, prise + compteur, thermostat + humidite...) : esp-matter ne
// recree pas un cluster deja present (Identify, Descriptor...).
// ============================================================================

static void add_electrical_clusters(endpoint_t *ep, bool with_power, bool with_energy)
{
    if (with_power) {
        // Delegate : stocke les valeurs de mesure (attributs geres par le serveur)
        ElecPowerDelegate *delegate = new ElecPowerDelegate();
        endpoint::electrical_sensor::config_t cfg;
        cfg.electrical_power_measurement.feature_flags =
            cluster::electrical_power_measurement::feature::alternating_current::get_id();
        cfg.electrical_power_measurement.delegate = delegate;
        endpoint::electrical_sensor::add(ep, &cfg);

        // Attributs optionnels : le serveur n'expose que ceux qui existent
        cluster_t *epm = cluster::get(ep, ElectricalPowerMeasurement::Id);
        if (epm) {
            cluster::electrical_power_measurement::attribute::create_voltage(epm, nullable<int64_t>());
            cluster::electrical_power_measurement::attribute::create_active_current(epm, nullable<int64_t>());
            cluster::electrical_power_measurement::attribute::create_apparent_power(epm, nullable<int64_t>());
        }
        if (s_elec_delegate_count < MATTER_BRIDGE_MAX_ENDPOINTS) {
            s_elec_delegates[s_elec_delegate_count] = delegate;
            s_elec_delegate_ep_ids[s_elec_delegate_count] = endpoint::get_id(ep);
            s_elec_delegate_count++;
        }
    } else if (with_energy) {
        endpoint::add_device_type(ep, endpoint::electrical_sensor::get_device_type_id(),
                                  endpoint::electrical_sensor::get_device_type_version());
    }
    if (with_energy) {
        cluster::electrical_energy_measurement::config_t eem_cfg;
        eem_cfg.feature_flags =
            cluster::electrical_energy_measurement::feature::imported_energy::get_id() |
            cluster::electrical_energy_measurement::feature::cumulative_energy::get_id();
        cluster::electrical_energy_measurement::create(ep, &eem_cfg, CLUSTER_FLAG_SERVER);
    }
}

static esp_err_t add_profile_clusters(endpoint_t *ep, const matter_profile_t *p)
{
    const uint32_t caps = p->caps;

    if (caps & MCAP_ONOFF) {
        if (p->light) {
            endpoint::on_off_light::config_t cfg;
            endpoint::on_off_light::add(ep, &cfg);
        } else {
            endpoint::on_off_plug_in_unit::config_t cfg;
            endpoint::on_off_plug_in_unit::add(ep, &cfg);
        }
    }
    if (caps & MCAP_TEMPERATURE) {
        endpoint::temperature_sensor::config_t cfg;
        endpoint::temperature_sensor::add(ep, &cfg);
    }
    if (caps & MCAP_HUMIDITY) {
        endpoint::humidity_sensor::config_t cfg;
        endpoint::humidity_sensor::add(ep, &cfg);
    }
    if (caps & MCAP_PRESSURE) {
        endpoint::pressure_sensor::config_t cfg;
        endpoint::pressure_sensor::add(ep, &cfg);
    }
    if (caps & MCAP_ILLUMINANCE) {
        endpoint::light_sensor::config_t cfg;
        endpoint::light_sensor::add(ep, &cfg);
    }
    if (caps & MCAP_OCCUPANCY) {
        // Une fonctionnalite de detection est obligatoire (conformance O.a+)
        endpoint::occupancy_sensor::config_t cfg;
        cfg.occupancy_sensing.occupancy_sensor_type = 0;          // PIR
        cfg.occupancy_sensing.occupancy_sensor_type_bitmap = 0x01;
        cfg.occupancy_sensing.feature_flags =
            cluster::occupancy_sensing::feature::passive_infrared::get_id();
        endpoint::occupancy_sensor::add(ep, &cfg);
    }
    if (caps & MCAP_LEAK) {
        endpoint::water_leak_detector::config_t cfg;
        endpoint::water_leak_detector::add(ep, &cfg);
    } else if (caps & MCAP_CONTACT) {
        endpoint::contact_sensor::config_t cfg;
        endpoint::contact_sensor::add(ep, &cfg);
    }
    if (caps & MCAP_COVER) {
        endpoint::window_covering::config_t cfg(0);
        cfg.window_covering.feature_flags =
            cluster::window_covering::feature::lift::get_id() |
            cluster::window_covering::feature::position_aware_lift::get_id();
        endpoint::window_covering::add(ep, &cfg);
    }
    if (caps & MCAP_THERMOSTAT) {
        endpoint::thermostat::config_t cfg;
        uint32_t f = 0;
        if (p->heat) f |= cluster::thermostat::feature::heating::get_id();
        if (p->cool) f |= cluster::thermostat::feature::cooling::get_id();
        cfg.thermostat.feature_flags = f;
        // ControlSequenceOfOperation : 2 chauffage seul, 0 froid seul, 4 les deux
        cfg.thermostat.control_sequence_of_operation = (p->heat && p->cool) ? 4 : (p->heat ? 2 : 0);
        cfg.thermostat.system_mode = p->heat ? 4 : 3;     // Heat / Cool
        endpoint::thermostat::add(ep, &cfg);
        cluster_t *th = cluster::get(ep, Thermostat::Id);
        bool running = false;
        for (int i = 0; i < p->link_count; i++)
            if (p->links[i].target == ML_TH_RUNNING) running = true;
        if (th && running) cluster::thermostat::attribute::create_thermostat_running_state(th, 0);
    }
    if (caps & MCAP_FAN) {
        endpoint::fan::config_t cfg;
        endpoint::fan::add(ep, &cfg);
    }
    if (caps & MCAP_SWITCH) {
        endpoint::generic_switch::config_t cfg;
        cfg.switch_cluster.feature_flags =
            cluster::switch_cluster::feature::momentary_switch::get_id() |
            cluster::switch_cluster::feature::momentary_switch_release::get_id() |
            cluster::switch_cluster::feature::momentary_switch_long_press::get_id() |
            cluster::switch_cluster::feature::momentary_switch_multi_press::get_id();
        cfg.switch_cluster.features.momentary_switch_multi_press.multi_press_max = 4;
        endpoint::generic_switch::add(ep, &cfg);
    }
    if (caps & (MCAP_ELECTRICAL | MCAP_ENERGY)) {
        add_electrical_clusters(ep, (caps & MCAP_ELECTRICAL) != 0, (caps & MCAP_ENERGY) != 0);
        if (p->zlinky && (caps & MCAP_ENERGY)) add_linky_clusters(ep);
    }
    if (caps & MCAP_BATTERY) {
        // Power Source sur l'endpoint ponte : etat de la pile de l'appareil
        cluster::power_source::config_t ps;
        ps.status = 1;                                  // Active
        strlcpy(ps.description, "Batterie", sizeof(ps.description));
        ps.feature_flags = cluster::power_source::feature::battery::get_id();
        cluster_t *psc = cluster::power_source::create(ep, &ps, CLUSTER_FLAG_SERVER);
        if (psc) {
            bool pct = false, volt = false;
            for (int i = 0; i < p->link_count; i++) {
                if (p->links[i].target == ML_BAT_PERCENT) pct = true;
                if (p->links[i].target == ML_BAT_VOLTAGE) volt = true;
            }
            if (pct) cluster::power_source::attribute::create_bat_percent_remaining(
                         psc, nullable<uint8_t>(), nullable<uint8_t>(0), nullable<uint8_t>(200));
            if (volt) cluster::power_source::attribute::create_bat_voltage(
                          psc, nullable<uint32_t>(), nullable<uint32_t>(0), nullable<uint32_t>(UINT32_MAX - 1));
        }
    }
    return ESP_OK;
}

// ============================================================================
// Energie cumulee (ElectricalEnergyMeasurement)
// ============================================================================

// Appele verrou de la pile tenu, une fois l'endpoint active.
static void energy_endpoint_init(uint16_t ep_id)
{
    if (!s_eem_access) {
        chip::BitMask<ElectricalEnergyMeasurement::Feature> features;
        features.Set(ElectricalEnergyMeasurement::Feature::kImportedEnergy);
        features.Set(ElectricalEnergyMeasurement::Feature::kCumulativeEnergy);
        chip::BitMask<ElectricalEnergyMeasurement::OptionalAttributes> optional_attrs;

        s_eem_access = new ElectricalEnergyMeasurement::ElectricalEnergyMeasurementAttrAccess(
            features, optional_attrs);
        if (s_eem_access->Init() != CHIP_NO_ERROR) {
            ESP_LOGE(TAG, "ElectricalEnergyMeasurement: echec d'initialisation");
            delete s_eem_access;
            s_eem_access = nullptr;
            return;
        }
    }

    // Accuracy est obligatoire dans ce cluster, avec au moins une plage
    // (FixedMax : index au Wh pres). Le serveur garde un pointeur sur la plage.
    static ElectricalEnergyMeasurement::Structs::MeasurementAccuracyRangeStruct::Type s_energy_range;
    s_energy_range.rangeMin = 0;
    s_energy_range.rangeMax = INT64_MAX / 2;
    s_energy_range.fixedMax.SetValue(1000);

    ElectricalEnergyMeasurement::Structs::MeasurementAccuracyStruct::Type accuracy;
    accuracy.measurementType  = ElectricalEnergyMeasurement::MeasurementTypeEnum::kElectricalEnergy;
    accuracy.measured         = true;
    accuracy.minMeasuredValue = 0;
    accuracy.maxMeasuredValue = INT64_MAX / 2;
    accuracy.accuracyRanges   = chip::app::DataModel::List<
        const ElectricalEnergyMeasurement::Structs::MeasurementAccuracyRangeStruct::Type>(&s_energy_range, 1);
    ElectricalEnergyMeasurement::SetMeasurementAccuracy(ep_id, accuracy);

    if (s_energy_count < MATTER_BRIDGE_MAX_ENDPOINTS) {
        energy_state_t *st = &s_energy[s_energy_count++];
        memset(st, 0, sizeof(*st));
        st->endpoint_id   = ep_id;
        st->published_mwh = -1;
    }
}

static energy_state_t *energy_state_for(int endpoint_id)
{
    for (int i = 0; i < s_energy_count; i++) {
        if (s_energy[i].endpoint_id == endpoint_id) return &s_energy[i];
    }
    return NULL;
}

// ============================================================================
// Matter 1.5 : identification du compteur et index par tarif (ZLinky)
//
// Clusters provisoires de Matter 1.5 (Meter Identification 0x0B06, Commodity
// Metering 0x0B07) sur l'endpoint compteur d'un ZLinky. Les instances restent
// enregistrees pour la duree du programme : un endpoint retire puis remis
// (meme numero) reprend la sienne.
// ============================================================================

typedef struct {
    int      endpoint_id;
    uint64_t ieee;
    MeterIdentification::Instance *mi;
    CommodityMetering::Instance   *cm;
    uint16_t cm_mask;                       // index publies (bit = tarif)
    int64_t  cm_kwh[ENERGY_TIER_COUNT];
} linky_state_t;

static linky_state_t s_linky[MATTER_BRIDGE_MAX_ENDPOINTS];
static int s_linky_count = 0;

// Index EASF01..EASF10 publies sous les identifiants de composante tarifaire
// 1..10 (pas encore de cluster Commodity Tariff pour les decrire).
static const uint32_t k_tier_component_ids[ENERGY_TIER_COUNT] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

// Commodity Metering : Quantity est exprimee dans l'unite de TariffUnit (kWh)
#define LINKY_WH_PER_QUANTITY 1000

// Secondes entre l'epoch Unix et l'epoch Matter (01/01/2000 UTC)
#define MATTER_EPOCH_OFFSET_S 946684800

typedef struct {
    bool    histo;
    bool    has_serial;
    char    serial[24];
    bool    has_power;
    int64_t power_va;
} linky_info_t;

// Les entrees ne sont qu'ajoutees, sous le verrou de la pile
static linky_state_t *linky_state_for(int endpoint_id)
{
    for (int i = 0; i < s_linky_count; i++) {
        if (s_linky[i].endpoint_id == endpoint_id) return &s_linky[i];
    }
    return NULL;
}

// Appele verrou de la pile tenu, une fois l'endpoint active.
static void linky_endpoint_init(uint16_t ep_id, uint64_t ieee)
{
    linky_state_t *ls = linky_state_for(ep_id);
    if (!ls) {
        if (s_linky_count >= MATTER_BRIDGE_MAX_ENDPOINTS) return;
        ls = &s_linky[s_linky_count];
        memset(ls, 0, sizeof(*ls));
        ls->endpoint_id = ep_id;

        ls->mi = new MeterIdentification::Instance(ep_id,
            chip::BitMask<MeterIdentification::Feature>(MeterIdentification::Feature::kPowerThreshold));
        if (ls->mi->Init() != CHIP_NO_ERROR) {
            ESP_LOGE(TAG, "MeterIdentification ep=%u : echec d'initialisation", ep_id);
            delete ls->mi;
            ls->mi = nullptr;
        } else {
            ls->mi->SetMeterType(chip::app::DataModel::MakeNullable(MeterIdentification::MeterTypeEnum::kUtility));
        }

        ls->cm = new CommodityMetering::Instance(ep_id);
        if (ls->cm->Init() != CHIP_NO_ERROR) {
            ESP_LOGE(TAG, "CommodityMetering ep=%u : echec d'initialisation", ep_id);
            delete ls->cm;
            ls->cm = nullptr;
        } else {
            ls->cm->SetMaximumMeteredQuantities(chip::app::DataModel::MakeNullable<uint16_t>(ENERGY_TIER_COUNT));
            ls->cm->SetTariffUnit(chip::app::DataModel::MakeNullable(Globals::TariffUnitEnum::kKWh));
        }
        s_linky_count++;
    }
    ls->ieee    = ieee;
    ls->cm_mask = 0;   // republier les index au prochain rapport
}

static bool dd_hex_num(uint64_t ieee, uint16_t cl, uint16_t at, int64_t *out)
{
    char buf[DEVDATA_VALUE_LEN];
    if (device_data_get_value(ieee, cl, at, buf, sizeof(buf)) != ESP_OK || !buf[0]) return false;
    *out = (int64_t)strtoull(buf, NULL, 16);
    return true;
}

// Lu hors du verrou de la pile : device_data a son propre verrou.
static void linky_read_info(uint64_t ieee, linky_info_t *li)
{
    memset(li, 0, sizeof(*li));

    // ADSC (0702/0x0308) : octets BCD stockes en hexa ("022076206405"),
    // texte encode en hexa, ou texte en clair (fiche 2.x)
    char buf[DEVDATA_VALUE_LEN];
    if (device_data_get_value(ieee, 0x0702, 0x0308, buf, sizeof(buf)) == ESP_OK && buf[0]) {
        char txt[sizeof(li->serial)];
        size_t n = 0;
        bool text = device_data_value_is_hex(buf);
        for (size_t i = 0; text && buf[i] && buf[i + 1]; i += 2) {
            char hx[3] = { buf[i], buf[i + 1], 0 };
            long c = strtol(hx, NULL, 16);
            if (c < 0x20 || c > 0x7E || n + 1 >= sizeof(txt)) text = false;
            else txt[n++] = (char)c;
        }
        txt[n] = '\0';
        strlcpy(li->serial, (text && n) ? txt : buf, sizeof(li->serial));
        li->has_serial = li->serial[0] != '\0';
    }

    // Puissance souscrite, comme energy_service : ISOUSC x 200 VA par phase
    // (historique), PREF en kVA (standard, a defaut 0B01/13)
    int64_t mode = -1, v = 0;
    dd_hex_num(ieee, 0xFF66, 0x0300, &mode);
    li->histo = (mode == 0 || mode == 2);
    bool tri  = (mode == 2 || mode == 3 || mode == 7);
    if (li->histo) {
        if (dd_hex_num(ieee, 0x0B01, 13, &v) && v > 0) {
            li->has_power = true;
            li->power_va  = v * 200 * (tri ? 3 : 1);
        }
    } else if ((dd_hex_num(ieee, 0x0B01, 14, &v) && v > 0) || (dd_hex_num(ieee, 0x0B01, 13, &v) && v > 0)) {
        li->has_power = true;
        li->power_va  = v * 1000;
    }
}

// Appele verrou de la pile tenu. Les Set* du serveur ne signalent un
// changement aux abonnes que si la valeur change.
static void linky_apply(linky_state_t *ls, const linky_info_t *li, const energy_state_t *st)
{
    using chip::app::DataModel::List;
    using chip::app::DataModel::MakeNullable;
    using chip::app::DataModel::Nullable;

    if (ls->mi) {
        ls->mi->SetProtocolVersion(MakeNullable(
            chip::CharSpan::fromCharString(li->histo ? "TIC historique" : "TIC standard")));

        Nullable<chip::CharSpan> serial;
        if (li->has_serial) serial.SetNonNull(chip::CharSpan::fromCharString(li->serial));
        ls->mi->SetMeterSerialNumber(serial);

        Nullable<Globals::Structs::PowerThresholdStruct::Type> threshold;
        if (li->has_power) {
            Globals::Structs::PowerThresholdStruct::Type t;
            t.apparentPowerThreshold.SetValue(li->power_va * 1000);   // VA -> mVA
            t.powerThresholdSource.SetNonNull(Globals::PowerThresholdSourceEnum::kContract);
            threshold.SetNonNull(t);
        }
        ls->mi->SetPowerThreshold(threshold);
    }

    if (!ls->cm || !st || !st->tier_mask) return;

    int64_t kwh[ENERGY_TIER_COUNT] = {};
    bool same = (st->tier_mask == ls->cm_mask);
    for (int t = 0; t < ENERGY_TIER_COUNT; t++) {
        if (!(st->tier_mask & (1u << t))) continue;
        kwh[t] = (int64_t)(st->tier_wh[t] / LINKY_WH_PER_QUANTITY);
        if (kwh[t] != ls->cm_kwh[t]) same = false;
    }
    if (same) return;

    CommodityMetering::Structs::MeteredQuantityStruct::Type q[ENERGY_TIER_COUNT];
    size_t n = 0;
    for (int t = 0; t < ENERGY_TIER_COUNT; t++) {
        if (!(st->tier_mask & (1u << t))) continue;
        q[n].tariffComponentIDs = List<const uint32_t>(&k_tier_component_ids[t], 1);
        q[n].quantity           = kwh[t];
        n++;
    }
    if (ls->cm->SetMeteredQuantity(MakeNullable(
            List<CommodityMetering::Structs::MeteredQuantityStruct::Type>(q, n))) != CHIP_NO_ERROR) {
        return;
    }
    time_t now = time(NULL);
    if (now > 1600000000) {
        ls->cm->SetMeteredQuantityTimestamp(MakeNullable((uint32_t)(now - MATTER_EPOCH_OFFSET_S)));
    }
    ls->cm_mask = st->tier_mask;
    memcpy(ls->cm_kwh, kwh, sizeof(kwh));
}

esp_err_t matter_bridge_report_energy_index(int endpoint_id, uint16_t attr_id, uint64_t wh)
{
    if (!s_matter_started) return ESP_ERR_INVALID_STATE;

    // Donnees du compteur lues avant de prendre le verrou de la pile
    linky_info_t li;
    const linky_state_t *ls0 = linky_state_for(endpoint_id);
    uint64_t linky_ieee = ls0 ? ls0->ieee : 0;
    if (linky_ieee) linky_read_info(linky_ieee, &li);

    bool held;
    if (!stack_lock(&held)) return ESP_FAIL;

    esp_err_t ret = ESP_OK;
    energy_state_t *st = energy_state_for(endpoint_id);
    if (!st || !s_eem_access) {
        ret = ESP_ERR_NOT_FOUND;
        goto out;
    }

    if (attr_id == 0x0000) {
        st->has_total = true;
        st->total_wh  = wh;
    } else if (attr_id >= 0x0100 && attr_id <= 0x0112 && !(attr_id & 1)) {
        int t = (attr_id - 0x0100) / 2;
        st->tier_wh[t] = wh;
        st->tier_mask |= (uint16_t)(1u << t);
    } else {
        ret = ESP_ERR_INVALID_ARG;
        goto out;
    }

    if (linky_ieee) {
        linky_state_t *ls = linky_state_for(endpoint_id);
        if (ls) linky_apply(ls, &li, st);
    }

    {
        // Index total en priorite ; a defaut, somme des index tarifaires recus
        // (HC/HP, Tempo...).
        uint64_t total = 0;
        bool from_total = st->has_total && st->total_wh > 0;
        if (from_total) {
            total = st->total_wh;
        } else {
            for (int t = 0; t < ENERGY_TIER_COUNT; t++) {
                if (st->tier_mask & (1u << t)) total += st->tier_wh[t];
            }
        }
        if (total == 0) goto out;

        int64_t mwh = (int64_t)total * 1000;   // Wh -> mWh
        if (mwh == st->published_mwh) goto out;

        // Une somme d'index tarifaires peut etre transitoirement incomplete
        // (index recus un par un) : ne pas publier un cumul en recul dans ce
        // cas. Un index total en recul (compteur remplace) est publie tel quel.
        if (!from_total && st->published_mwh > 0 && mwh < st->published_mwh) goto out;

        ElectricalEnergyMeasurement::Structs::EnergyMeasurementStruct::Type energy;
        energy.energy = mwh;
        ElectricalEnergyMeasurement::NotifyCumulativeEnergyMeasured(
            (chip::EndpointId)endpoint_id, chip::MakeOptional(energy), chip::NullOptional);

        // NotifyCumulativeEnergyMeasured met la valeur a jour et emet
        // l'evenement, mais ne signale pas le changement d'attribut aux abonnes.
        MatterReportingAttributeChangeCallback((chip::EndpointId)endpoint_id,
            ElectricalEnergyMeasurement::Id,
            ElectricalEnergyMeasurement::Attributes::CumulativeEnergyImported::Id);

        st->published_mwh = mwh;
        ESP_LOGD(TAG, "Energie cumulee ep=%d : %llu Wh (%s)", endpoint_id,
                 (unsigned long long)total, from_total ? "index total" : "somme des index");
    }

out:
    stack_unlock(held);
    return ret;
}

// ============================================================================
// API publique
// ============================================================================

esp_err_t matter_bridge_init(void)
{
    ESP_LOGI(TAG, "Initializing Matter Bridge...");

    // Creer le node Matter
    node::config_t node_config;
    s_matter_node = node::create(&node_config, matter_attribute_update_cb,
                                  matter_identification_cb);
    if (!s_matter_node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return ESP_FAIL;
    }

    // Le root endpoint (endpoint 0) est cree automatiquement par esp-matter
    // On cree le endpoint "aggregator" (bridge, device type 0x000E)
    endpoint::aggregator::config_t agg_config;
    s_aggregator_endpoint = endpoint::aggregator::create(s_matter_node, &agg_config,
                                                          ENDPOINT_FLAG_NONE, NULL);
    if (!s_aggregator_endpoint) {
        ESP_LOGE(TAG, "Failed to create bridge aggregator endpoint");
        return ESP_FAIL;
    }

    s_aggregator_ep_id = endpoint::get_id(s_aggregator_endpoint);
    ESP_LOGI(TAG, "Matter Bridge node created (aggregator endpoint=%d)", s_aggregator_ep_id);

    return ESP_OK;
}

// ============================================================================
// Fenetre d'appairage toujours ouverte tant que le pont n'est associe a
// aucun controleur. Sans cela, elle se fermait 15 min apres le demarrage, ou
// apres qu'un controleur avait retire le pont (RemoveFabric), et il fallait
// redemarrer le pont pour pouvoir l'ajouter de nouveau.
// ============================================================================

static void reopen_window_if_unpaired(chip::System::Layer *, void *)
{
    chip::Server &srv = chip::Server::GetInstance();
    if (srv.GetFabricTable().FabricCount() != 0) return;
    chip::CommissioningWindowManager &cwm = srv.GetCommissioningWindowManager();
    if (cwm.IsCommissioningWindowOpen()) return;
    CHIP_ERROR err = cwm.OpenBasicCommissioningWindow();
    ESP_LOGI(TAG, "Aucun controleur associe : fenetre d'appairage rouverte (%" CHIP_ERROR_FORMAT ")",
             err.Format());
}

// Appele dans la tache CHIP, verrou de la pile tenu
static void matter_app_event_cb(const ChipDeviceEvent *event, intptr_t)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        // Differe : laisser la pile terminer la fermeture ou la suppression
        chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds32(2),
                                                    reopen_window_if_unpaired, nullptr);
        break;
    default:
        break;
    }
}

// ----------------------------------------------------------------------------
// Code d'appairage propre a chaque passerelle
//
// Le code de test du SDK (20202021, discriminateur 3840 : 3497-011-2332)
// etait le meme sur toutes les Box ; la fenetre d'appairage s'ouvrant seule
// tant qu'aucun controleur n'est associe, n'importe qui sur le reseau local
// pouvait s'approprier la passerelle. Code, discriminateur et sel tires au
// hasard au premier demarrage, gardes en NVS (espace "lx_matter") ; le
// verificateur SPAKE2+ en est recalcule a chaque demarrage. Les controleurs
// deja associes ne sont pas concernes (identifiants operationnels).
// ----------------------------------------------------------------------------

class LxCommissionableData : public chip::DeviceLayer::CommissionableDataProvider
{
public:
    esp_err_t init()
    {
        nvs_handle_t h;
        esp_err_t e = nvs_open("lx_matter", NVS_READWRITE, &h);
        if (e != ESP_OK) return e;
        size_t sl = sizeof(m_salt);
        bool have = nvs_get_u32(h, "pin", &m_pin) == ESP_OK && nvs_get_u16(h, "disc", &m_disc) == ESP_OK &&
                    nvs_get_blob(h, "salt", m_salt, &sl) == ESP_OK && sl == sizeof(m_salt) &&
                    chip::SetupPayload::IsValidSetupPIN(m_pin) && m_disc <= 0xFFF;
        if (!have) {
            do {
                m_pin = 1 + esp_random() % 99999998u;          // 00000001 .. 99999998
            } while (!chip::SetupPayload::IsValidSetupPIN(m_pin));
            m_disc = (uint16_t)(esp_random() & 0xFFF);
            esp_fill_random(m_salt, sizeof(m_salt));
            e = nvs_set_u32(h, "pin", m_pin);
            if (e == ESP_OK) e = nvs_set_u16(h, "disc", m_disc);
            if (e == ESP_OK) e = nvs_set_blob(h, "salt", m_salt, sizeof(m_salt));
            if (e == ESP_OK) e = nvs_commit(h);
            ESP_LOGI(TAG, "Code d'appairage Matter propre a la passerelle cree (%s)", esp_err_to_name(e));
        }
        nvs_close(h);
        if (e != ESP_OK) return e;

        chip::Crypto::Spake2pVerifier v;
        if (v.Generate(kIterations, chip::ByteSpan(m_salt), m_pin) != CHIP_NO_ERROR) return ESP_FAIL;
        chip::MutableByteSpan out(m_verifier);
        if (v.Serialize(out) != CHIP_NO_ERROR) return ESP_FAIL;
        m_verifier_len = out.size();
        return ESP_OK;
    }

    CHIP_ERROR GetSetupDiscriminator(uint16_t &d) override { d = m_disc; return CHIP_NO_ERROR; }
    CHIP_ERROR SetSetupDiscriminator(uint16_t) override { return CHIP_ERROR_NOT_IMPLEMENTED; }
    CHIP_ERROR GetSpake2pIterationCount(uint32_t &c) override { c = kIterations; return CHIP_NO_ERROR; }
    CHIP_ERROR GetSpake2pSalt(chip::MutableByteSpan &salt) override { return chip::CopySpanToMutableSpan(chip::ByteSpan(m_salt), salt); }
    CHIP_ERROR GetSpake2pVerifier(chip::MutableByteSpan &buf, size_t &len) override
    {
        len = m_verifier_len;
        return chip::CopySpanToMutableSpan(chip::ByteSpan(m_verifier, m_verifier_len), buf);
    }
    CHIP_ERROR GetSetupPasscode(uint32_t &p) override { p = m_pin; return CHIP_NO_ERROR; }
    CHIP_ERROR SetSetupPasscode(uint32_t) override { return CHIP_ERROR_NOT_IMPLEMENTED; }

private:
    static constexpr uint32_t kIterations = 1000;   // minimum Matter : calcul rapide au demarrage
    uint32_t m_pin = 0;
    uint16_t m_disc = 0;
    uint8_t  m_salt[32] = {};
    uint8_t  m_verifier[chip::Crypto::kSpake2p_VerifierSerialized_Length] = {};
    size_t   m_verifier_len = 0;
};

static LxCommissionableData s_commissionable;

esp_err_t matter_bridge_start(void)
{
    ESP_LOGI(TAG, "Starting Matter stack...");
    // Avant esp_matter::start() : la pile lit le code des son initialisation
    if (s_commissionable.init() == ESP_OK) {
        esp_matter::set_custom_commissionable_data_provider(&s_commissionable);
    } else {
        ESP_LOGE(TAG, "Code d'appairage propre indisponible : code de test du SDK");
    }
    esp_err_t ret = esp_matter::start(matter_app_event_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_matter::start() failed: %s", esp_err_to_name(ret));
    } else {
        s_matter_started = true;

        // QR code et code d'appairage manuel : dans le journal, et gardes pour
        // la page web. Ils ne changent pas tant que le firmware et ses
        // identifiants de commissioning sont les memes : calcul unique.
        bool held;
        if (stack_lock(&held)) {
#if CONFIG_ENABLE_WIFI_STATION
            // WiFi gere par le pont seul (wifi_manager : Ethernet prioritaire,
            // WiFi en secours). Sinon CHIP, voyant une station configuree, s'y
            // connectait de lui-meme et la reconnectait sans fin : WiFi actif en
            // parallele de l'Ethernet
            chip::DeviceLayer::ConnectivityMgr().SetWiFiStationMode(
                chip::DeviceLayer::ConnectivityManager::kWiFiStationMode_ApplicationControlled);
#endif
            chip::RendezvousInformationFlags rendezvous;
#if CONFIG_ENABLE_CHIPOBLE
            // Annonce le Bluetooth seulement s'il est compile : sinon le QR
            // code ferait chercher le pont en BLE par le telephone.
            rendezvous.Set(chip::RendezvousInformationFlag::kBLE);
#endif
            rendezvous.Set(chip::RendezvousInformationFlag::kOnNetwork);
            PrintOnboardingCodes(rendezvous);

            chip::MutableCharSpan qr(s_qr_code, sizeof(s_qr_code) - 1);
            if (GetQRCode(qr, rendezvous) == CHIP_NO_ERROR) {
                s_qr_code[qr.size()] = '\0';
            } else {
                s_qr_code[0] = '\0';
            }
            chip::MutableCharSpan manual(s_manual_code, sizeof(s_manual_code) - 1);
            if (GetManualPairingCode(manual, rendezvous) == CHIP_NO_ERROR) {
                s_manual_code[manual.size()] = '\0';
            } else {
                s_manual_code[0] = '\0';
            }
            stack_unlock(held);
        }
    }
    return ret;
}

bool matter_bridge_is_started(void)
{
    return s_matter_started;
}

bool matter_bridge_get_onboarding_codes(const char **qr_code, const char **manual_code)
{
    if (qr_code) *qr_code = s_qr_code;
    if (manual_code) *manual_code = s_manual_code;
    return s_qr_code[0] != '\0' || s_manual_code[0] != '\0';
}

static int add_endpoint_impl(uint64_t zigbee_ieee, matter_device_type_t device_type,
                             const matter_profile_t *profile, const char *label, int preferred_ep,
                             const char *product = NULL);

int matter_bridge_add_endpoint(uint64_t zigbee_ieee, matter_device_type_t device_type,
                                const char *label, int preferred_ep)
{
    return add_endpoint_impl(zigbee_ieee, device_type, NULL, label, preferred_ep);
}

int matter_bridge_add_endpoint_profile(uint64_t ieee, const matter_profile_t *profile,
                                       const char *label, int preferred_ep)
{
    if (!profile) return -1;
    return add_endpoint_impl(ieee, (matter_device_type_t)0, profile, label, preferred_ep);
}

int matter_bridge_add_virtual_endpoint(uint64_t uid, const matter_profile_t *profile,
                                       const char *label, const char *product, int preferred_ep)
{
    if (!profile) return -1;
    return add_endpoint_impl(uid, (matter_device_type_t)0, profile, label, preferred_ep,
                             product ? product : "");
}

static int add_endpoint_impl(uint64_t zigbee_ieee, matter_device_type_t device_type,
                             const matter_profile_t *profile, const char *label, int preferred_ep,
                             const char *product)
{
    if (!s_matter_node || !s_aggregator_endpoint) return -1;

    ESP_LOGI(TAG, "Creating bridged endpoint for 0x%016llX type=0x%04X caps=0x%04lX label=%s (ep connu=%d)",
             zigbee_ieee, device_type, profile ? (unsigned long)profile->caps : 0UL,
             label ? label : "(none)", preferred_ep);

    // Apres start(), le modele de donnees est parcouru par la tache CHIP :
    // creation, activation et initialisation se font sous le verrou.
    bool held = false;
    if (s_matter_started && !stack_lock(&held)) {
        ESP_LOGE(TAG, "Verrou de la pile Matter indisponible");
        return -1;
    }

    int ret_ep = -1;
    esp_err_t err;
    endpoint::bridged_node::config_t bridged_config;
    const uint8_t flags = ENDPOINT_FLAG_DESTROYABLE | ENDPOINT_FLAG_BRIDGE;
    endpoint_t *ep = NULL;

    // 1. Reprendre l'endpoint deja connu de ce device, sinon en creer un.
    //    Un controleur identifie l'appareil ponte par son endpoint : en changer
    //    le fait disparaitre puis reapparaitre comme un nouvel appareil.
    //    resume() n'accepte qu'un numero deja attribue, ce qu'esp-matter ne
    //    sait qu'apres start().
    if (preferred_ep > 1 && s_matter_started) {
        ep = endpoint::bridged_node::resume(s_matter_node, &bridged_config, flags,
                                            (uint16_t)preferred_ep, NULL);
        if (!ep) {
            ESP_LOGW(TAG, "Endpoint %d non repris, creation d'un nouvel endpoint", preferred_ep);
        }
    }
    if (!ep) {
        ep = endpoint::bridged_node::create(s_matter_node, &bridged_config, flags, NULL);
    }
    if (!ep) {
        ESP_LOGE(TAG, "Failed to create bridged_node endpoint");
        goto out;
    }

    // 2. Identite de l'appareil ponte (UniqueID, nom, fabricant, modele)
    set_bridged_identity(ep, zigbee_ieee, label, product);

    // 3. Ajouter les clusters : profil du template, a defaut type deduit du
    //    DeviceId Zigbee
    err = profile ? add_profile_clusters(ep, profile) : add_device_type_clusters(ep, device_type);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device type clusters");
        endpoint::destroy(s_matter_node, ep);
        goto out;
    }

    // 4. Definir le parent endpoint (l'aggregator)
    err = set_parent_endpoint(ep, s_aggregator_endpoint);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "set_parent_endpoint returned 0x%X (may be OK)", err);
        // Ne pas echouer sur ca, certains SDKs n'ont pas cette API
    }

    // 5. Si le stack Matter est deja demarre, il faut activer l'endpoint manuellement
    //    (les endpoints crees avant start() sont actives via enable_all())
    if (s_matter_started) {
        err = endpoint::enable(ep);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "endpoint::enable() failed: %s", esp_err_to_name(err));
            endpoint::destroy(s_matter_node, ep);
            goto out;
        }

        // 6. Delegate ElectricalPowerMeasurement : deja initialise par
        //    endpoint::enable(), qui appelle les init callbacks des clusters
        //    quand le stack est demarre (esp-matter 1.5). Un second appel
        //    creait une deuxieme instance dont Init() echouait (interface
        //    d'acces deja enregistree pour cet endpoint) : instance perdue.
        //    Reste a initialiser l'energie cumulee et les attributs Linky.
        uint16_t new_ep_id = endpoint::get_id(ep);
        if (profile ? (profile->caps & MCAP_ENERGY) != 0 : device_type == MATTER_DEV_ENERGY_METER) {
            energy_endpoint_init(new_ep_id);
        }
        if (profile ? (profile->zlinky && (profile->caps & MCAP_ENERGY)) : device_type == MATTER_DEV_ENERGY_METER) {
            linky_endpoint_init(new_ep_id, zigbee_ieee);
        }
    }

    ret_ep = endpoint::get_id(ep);
    ESP_LOGI(TAG, "Created Matter bridged endpoint %d for Zigbee 0x%016llX (type=0x%04X)",
             ret_ep, zigbee_ieee, device_type);

out:
    stack_unlock(held);
    return ret_ep;
}

esp_err_t matter_bridge_remove_endpoint(int endpoint_id)
{
    if (!s_matter_node) return ESP_ERR_INVALID_STATE;

    bool held = false;
    if (s_matter_started && !stack_lock(&held)) return ESP_FAIL;

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    endpoint_t *ep = endpoint::get(s_matter_node, endpoint_id);
    if (ep) {
        ret = endpoint::destroy(s_matter_node, ep);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Removed Matter endpoint %d", endpoint_id);
        }
    }

    stack_unlock(held);
    return ret;
}

esp_err_t matter_bridge_update_attribute(int endpoint_id, uint32_t cluster_id,
                                          uint32_t attr_id, const void *value, size_t size)
{
    if (!s_matter_node) return ESP_ERR_INVALID_STATE;

    // Avant start(), le verrou de la pile n'existe pas et attribute::update()
    // le prendrait. Les endpoints des devices n'existent d'ailleurs pas encore.
    if (!s_matter_started) return ESP_ERR_INVALID_STATE;

    bool held;
    if (!stack_lock(&held)) return ESP_FAIL;

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    endpoint_t *ep = endpoint::get(s_matter_node, endpoint_id);
    cluster_t *cluster = ep ? cluster::get(ep, cluster_id) : NULL;
    attribute_t *attr = cluster ? attribute::get(cluster, attr_id) : NULL;
    if (attr) {
        s_zigbee_update_in_progress = true;
        ret = attribute::update(endpoint_id, cluster_id, attr_id,
                                (esp_matter_attr_val_t *)value);
        s_zigbee_update_in_progress = false;
    }

    stack_unlock(held);
    return ret;
}

esp_err_t matter_bridge_set_number(int endpoint_id, uint32_t cluster_id, uint32_t attr_id,
                                   int32_t value, bool is_null)
{
    if (!s_matter_node || !s_matter_started) return ESP_ERR_INVALID_STATE;
    bool held;
    if (!stack_lock(&held)) return ESP_FAIL;

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    endpoint_t *ep = endpoint::get(s_matter_node, endpoint_id);
    cluster_t *cluster = ep ? cluster::get(ep, cluster_id) : NULL;
    attribute_t *attr = cluster ? attribute::get(cluster, attr_id) : NULL;
    esp_matter_attr_val_t cur = {};
    if (attr && attribute::get_val(attr, &cur) == ESP_OK) {
        esp_matter_attr_val_t v = cur;
        ret = ESP_OK;
        switch (cur.type) {
        case ESP_MATTER_VAL_TYPE_BOOLEAN:  v.val.b = value != 0; break;
        case ESP_MATTER_VAL_TYPE_INT8:     v.val.i8 = (int8_t)value; break;
        case ESP_MATTER_VAL_TYPE_INT16:    v.val.i16 = (int16_t)value; break;
        case ESP_MATTER_VAL_TYPE_NULLABLE_INT16:
            if (is_null) chip::app::NumericAttributeTraits<int16_t>::SetNull(v.val.i16);
            else v.val.i16 = (int16_t)value;
            break;
        case ESP_MATTER_VAL_TYPE_UINT8:
        case ESP_MATTER_VAL_TYPE_ENUM8:
        case ESP_MATTER_VAL_TYPE_BITMAP8:  v.val.u8 = (uint8_t)value; break;
        case ESP_MATTER_VAL_TYPE_UINT16:
        case ESP_MATTER_VAL_TYPE_BITMAP16:
        case ESP_MATTER_VAL_TYPE_ENUM16:   v.val.u16 = (uint16_t)value; break;
        case ESP_MATTER_VAL_TYPE_INT32:    v.val.i32 = value; break;
        case ESP_MATTER_VAL_TYPE_UINT32:   v.val.u32 = (uint32_t)value; break;
        default:                           ret = ESP_ERR_NOT_SUPPORTED; break;
        }
        if (ret == ESP_OK) {
            s_zigbee_update_in_progress = true;     // pas d'echo vers le callback
            ret = attribute::update(endpoint_id, cluster_id, attr_id, &v);
            s_zigbee_update_in_progress = false;
        }
    }
    stack_unlock(held);
    return ret;
}

// Fournisseur d'identite du pont : celui de la pile, sauf la version
// materielle (nom de la box, comme son nom mDNS)
class HwNameInstanceInfo : public chip::DeviceLayer::DeviceInstanceInfoProvider {
public:
    chip::DeviceLayer::DeviceInstanceInfoProvider *base = nullptr;
    char hw[33] = "";
    CHIP_ERROR GetVendorName(char *b, size_t n) override { return base->GetVendorName(b, n); }
    CHIP_ERROR GetVendorId(uint16_t &v) override { return base->GetVendorId(v); }
    CHIP_ERROR GetProductName(char *b, size_t n) override { return base->GetProductName(b, n); }
    CHIP_ERROR GetProductId(uint16_t &v) override { return base->GetProductId(v); }
    CHIP_ERROR GetPartNumber(char *b, size_t n) override { return base->GetPartNumber(b, n); }
    CHIP_ERROR GetProductURL(char *b, size_t n) override { return base->GetProductURL(b, n); }
    CHIP_ERROR GetProductLabel(char *b, size_t n) override { return base->GetProductLabel(b, n); }
    CHIP_ERROR GetSerialNumber(char *b, size_t n) override { return base->GetSerialNumber(b, n); }
    CHIP_ERROR GetManufacturingDate(uint16_t &y, uint8_t &m, uint8_t &d) override { return base->GetManufacturingDate(y, m, d); }
    CHIP_ERROR GetHardwareVersion(uint16_t &v) override { return base->GetHardwareVersion(v); }
    CHIP_ERROR GetHardwareVersionString(char *b, size_t n) override
    {
        if (!hw[0]) return base->GetHardwareVersionString(b, n);
        if (strlen(hw) + 1 > n) return CHIP_ERROR_BUFFER_TOO_SMALL;
        strcpy(b, hw);
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR GetRotatingDeviceIdUniqueId(chip::MutableByteSpan &s) override { return base->GetRotatingDeviceIdUniqueId(s); }
    CHIP_ERROR GetProductFinish(chip::app::Clusters::BasicInformation::ProductFinishEnum *f) override { return base->GetProductFinish(f); }
    CHIP_ERROR GetProductPrimaryColor(chip::app::Clusters::BasicInformation::ColorEnum *c) override { return base->GetProductPrimaryColor(c); }
    CHIP_ERROR GetJointFabricMode(uint8_t &m) override { return base->GetJointFabricMode(m); }
};
static HwNameInstanceInfo s_hw_info;

void matter_bridge_set_hardware_version_string(const char *hw)
{
    if (!s_matter_started || !hw) return;
    bool held;
    if (!stack_lock(&held)) return;
    strlcpy(s_hw_info.hw, hw, sizeof(s_hw_info.hw));
    if (!s_hw_info.base) {
        s_hw_info.base = chip::DeviceLayer::GetDeviceInstanceInfoProvider();
        if (s_hw_info.base) chip::DeviceLayer::SetDeviceInstanceInfoProvider(&s_hw_info);
    }
    stack_unlock(held);
    ESP_LOGI(TAG, "Version materielle Matter : %s", hw);
}

int matter_bridge_bridged_count(void)
{
    if (!s_matter_node || !s_matter_started) return 0;
    bool held;
    if (!stack_lock(&held)) return 0;
    int n = 0;
    for (endpoint_t *ep = endpoint::get_first(s_matter_node); ep; ep = endpoint::get_next(ep)) {
        if (endpoint::get_id(ep) > 1) n++;          // hors racine (0) et agregateur (1)
    }
    stack_unlock(held);
    return n;
}

int matter_bridge_bridged_max(void)
{
    // La limite de la pile compte aussi la racine et l'agregateur
    return CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT - 2;
}

esp_err_t matter_bridge_switch_event(int endpoint_id, uint8_t presses)
{
    if (!s_matter_started) return ESP_ERR_INVALID_STATE;
    bool held;
    if (!stack_lock(&held)) return ESP_FAIL;

    // Sequence d'un bouton poussoir (position 1 = appuye) : appui, puis
    // relachement court suivi du nombre d'appuis, ou appui long
    chip::EndpointId ep = (chip::EndpointId)endpoint_id;
    cluster::switch_cluster::event::send_initial_press(ep, 1);
    if (presses == 0) {
        cluster::switch_cluster::event::send_long_press(ep, 1);
        cluster::switch_cluster::event::send_long_release(ep, 1);
    } else {
        cluster::switch_cluster::event::send_short_release(ep, 1);
        cluster::switch_cluster::event::send_multi_press_complete(ep, 1, presses);
    }
    ESP_LOGI(TAG, "Bouton ep=%d : %s", endpoint_id, presses ? "appui(s)" : "appui long");

    stack_unlock(held);
    return ESP_OK;
}

bool matter_bridge_is_commissioned(void)
{
    if (!s_matter_started) return false;

    bool held;
    if (!stack_lock(&held)) return false;
    bool commissioned = chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
    stack_unlock(held);
    return commissioned;
}

int matter_bridge_fabric_count(void)
{
    if (!s_matter_started) return -1;
    bool held;
    if (!stack_lock(&held)) return -1;
    int n = (int)chip::Server::GetInstance().GetFabricTable().FabricCount();
    stack_unlock(held);
    return n;
}

esp_err_t matter_bridge_open_commissioning_window(uint16_t seconds)
{
    if (!s_matter_started) return ESP_ERR_INVALID_STATE;
    if (seconds < 180) seconds = 180;               // bornes de la specification Matter
    if (seconds > 900) seconds = 900;
    bool held;
    if (!stack_lock(&held)) return ESP_ERR_TIMEOUT;
    chip::CommissioningWindowManager &cwm = chip::Server::GetInstance().GetCommissioningWindowManager();
    CHIP_ERROR err = CHIP_NO_ERROR;
    if (!cwm.IsCommissioningWindowOpen()) {
        err = cwm.OpenBasicCommissioningWindow(chip::System::Clock::Seconds16(seconds));
    }
    stack_unlock(held);
    ESP_LOGI(TAG, "Fenetre d'appairage ouverte %u s (%" CHIP_ERROR_FORMAT ")", seconds, err.Format());
    return err == CHIP_NO_ERROR ? ESP_OK : ESP_FAIL;
}

bool matter_bridge_commissioning_window_open(void)
{
    if (!s_matter_started) return false;
    bool held;
    if (!stack_lock(&held)) return false;
    bool open = chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen();
    stack_unlock(held);
    return open;
}

esp_err_t matter_bridge_factory_reset(void)
{
    ESP_LOGW(TAG, "Factory reset requested");
    esp_matter::factory_reset();
    return ESP_OK;
}

ElecPowerDelegate* matter_bridge_get_elec_delegate(int endpoint_id)
{
    for (int i = 0; i < s_elec_delegate_count; i++) {
        if (s_elec_delegate_ep_ids[i] == endpoint_id) {
            return s_elec_delegates[i];
        }
    }
    return nullptr;
}

// ============================================================================
// Recupere les donnees energy du premier ZLinky/energy meter trouve
// Expose via C linkage pour que web_api.cpp puisse l'appeler sans inclure
// les headers Matter SDK
// ============================================================================

esp_err_t matter_bridge_get_energy_data(matter_energy_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    // Chercher le premier device de type ENERGY_METER
    int count = zigate_devices_count();
    for (int i = 0; i < count; i++) {
        zigate_device_t *dev = zigate_devices_get(i);
        if (!dev) continue;
        const bridge_device_mapping_t *map = matter_device_manager_get_mapping_by_ieee(dev->ieee_addr);
        if (!map || map->device_type != MATTER_DEV_ENERGY_METER) continue;

        ElecPowerDelegate *elec = matter_bridge_get_elec_delegate(map->matter_endpoint);
        if (!elec) continue;

        // Les valeurs sont ecrites par la tache ZiGate sous le verrou de la
        // pile : les lire sous ce meme verrou (appel depuis la tache HTTP).
        bool held = false;
        if (s_matter_started && !stack_lock(&held)) return ESP_FAIL;

        out->valid = true;

        auto active = elec->GetActivePower();
        if (!active.IsNull()) {
            out->has_active_power = true;
            out->active_power = (int32_t)(active.Value() / 1000);  // mW -> W
        }

        auto apparent = elec->GetApparentPower();
        if (!apparent.IsNull()) {
            out->has_apparent_power = true;
            out->apparent_power = (int32_t)(apparent.Value() / 1000);  // mVA -> VA
        }

        auto voltage = elec->GetRMSVoltage();
        if (!voltage.IsNull()) {
            out->has_rms_voltage = true;
            out->rms_voltage = (int32_t)(voltage.Value() / 1000);  // mV -> V
        }

        auto current = elec->GetRMSCurrent();
        if (!current.IsNull()) {
            out->has_rms_current = true;
            out->rms_current = (int32_t)(current.Value());  // already in mA
        }

        stack_unlock(held);
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}
