#pragma once

#include "esp_err.h"
#include <stdint.h>
#include "matter_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Matter Bridge - Couche pont entre Zigbee (ZiGate) et Matter
// Le device ESP32 se presente comme un "Bridge" Matter (device type 0x000E)
// Chaque device Zigbee appaire est expose comme un endpoint Matter
// ============================================================================

// --- Configuration du Bridge ---
#define MATTER_BRIDGE_VENDOR_ID       0xFFF1  // Test VID (a changer pour production)
#define MATTER_BRIDGE_PRODUCT_ID      0x8001
#define MATTER_BRIDGE_PRODUCT_NAME    "LiXee MatterBridge"
#define MATTER_BRIDGE_VENDOR_NAME     "LiXee"
#define MATTER_BRIDGE_HW_VERSION      1
#define MATTER_BRIDGE_SW_VERSION_STR  "3.3"

// Nombre max d'endpoints dynamiques (devices Zigbee exposes via Matter)
#define MATTER_BRIDGE_MAX_ENDPOINTS   32

// --- Types de devices Matter supportes ---
// Mapping depuis les Device IDs Zigbee vers Matter
typedef enum {
    MATTER_DEV_ON_OFF_LIGHT       = 0x0100,  // ZCL 0x0100
    MATTER_DEV_DIMMABLE_LIGHT     = 0x0101,  // ZCL 0x0101
    MATTER_DEV_COLOR_LIGHT        = 0x0102,  // ZCL 0x0102
    MATTER_DEV_ON_OFF_PLUG        = 0x010A,  // ZCL 0x0009 -> Matter Plug
    MATTER_DEV_CONTACT_SENSOR     = 0x0015,
    MATTER_DEV_OCCUPANCY_SENSOR   = 0x0107,
    MATTER_DEV_TEMPERATURE_SENSOR = 0x0302,
    MATTER_DEV_HUMIDITY_SENSOR    = 0x0307,
    MATTER_DEV_DOOR_LOCK          = 0x000A,
    MATTER_DEV_WINDOW_COVERING    = 0x0202,
    MATTER_DEV_THERMOSTAT         = 0x0301,
    MATTER_DEV_POWER_SOURCE       = 0x0011,
    MATTER_DEV_ENERGY_METER       = 0x0510,  // Electrical Sensor
} matter_device_type_t;

/**
 * Initialise le stack Matter en mode Bridge
 * Configure le root node, DAC, et demarre le commissioning
 */
esp_err_t matter_bridge_init(void);

/**
 * Demarre le stack Matter (apres init)
 */
esp_err_t matter_bridge_start(void);

/**
 * Ajoute un endpoint dynamique pour un device Zigbee
 * @param zigbee_ieee     Adresse IEEE du device Zigbee
 * @param device_type     Type de device Matter a creer
 * @param label           Nom/alias du device (NodeLabel)
 * @param preferred_ep    Endpoint deja attribue a ce device lors d'un demarrage
 *                        precedent, a reprendre ; -1 pour un nouveau device.
 *                        Garder le meme endpoint conserve l'appareil dans les
 *                        controleurs (Home Assistant) d'un redemarrage a l'autre.
 * @return Endpoint ID assigne, ou -1 en cas d'erreur
 */
int matter_bridge_add_endpoint(uint64_t zigbee_ieee, matter_device_type_t device_type,
                                const char *label, int preferred_ep);

/**
 * true une fois esp_matter::start() reussi (pile et verrou disponibles)
 */
bool matter_bridge_is_started(void);

/**
 * Codes d'appairage du pont (connus apres matter_bridge_start()).
 * @param qr_code      Recoit la charge utile du QR code ("MT:..."), "" si inconnue
 * @param manual_code  Recoit le code manuel (11 chiffres), "" si inconnu
 * @return true si au moins un des deux codes est connu
 */
bool matter_bridge_get_onboarding_codes(const char **qr_code, const char **manual_code);

/**
 * Publie un index d'energie Zigbee (cluster Metering 0x0702) comme energie
 * cumulee importee Matter (ElectricalEnergyMeasurement).
 * @param endpoint_id  Endpoint Matter du compteur
 * @param attr_id      0x0000 (index total) ou index tarifaires, adresses paires
 *                     0x0100..0x0112 (Tier1..Tier10)
 * @param wh           Valeur de l'index, en Wh
 */
esp_err_t matter_bridge_report_energy_index(int endpoint_id, uint16_t attr_id, uint64_t wh);

/**
 * Publie le prix courant du kWh (cluster standard CommodityPrice, 0x0095) sur
 * l'EP Linky, d'apres la configuration tarifaire de la page Energie (voir
 * energy_service_get_live()). Sans effet si l'endpoint n'a pas la capacite
 * MCAP_LINKY_PRICE (voir matter_profile.h).
 * @param endpoint_id  Endpoint Matter du ZLinky
 * @param price_eur_per_kwh  Prix EUR/kWh de la periode tarifaire en cours ;
 *                           <= 0 efface le prix courant (valeur Nullable null,
 *                           tarif pas encore configure sur la page Energie)
 * @param tariff_name  Libelle de la periode en cours ("Heures Creuses"...),
 *                      peut etre NULL
 * @return ESP_ERR_INVALID_STATE si Matter n'est pas demarre ou si l'heure
 *         n'est pas encore synchronisee (rien n'est publie : periodStart
 *         serait faux), ESP_ERR_NOT_FOUND si l'EP n'a pas le cluster
 */
esp_err_t matter_bridge_report_tariff_price(int endpoint_id, double price_eur_per_kwh,
                                            const char *tariff_name);

/**
 * Supprime un endpoint dynamique
 */
esp_err_t matter_bridge_remove_endpoint(int endpoint_id);

/**
 * Met a jour une valeur d'attribut Matter (pousse par le Zigbee)
 * Appele quand on recoit un attribute report de la ZiGate
 */
esp_err_t matter_bridge_update_attribute(int endpoint_id, uint32_t cluster_id,
                                          uint32_t attr_id, const void *value, size_t size);

/**
 * Complete ou met a jour VendorName / ProductName (BridgedDeviceBasicInformation,
 * 0x0039) : cree l'attribut s'il n'existait pas encore (fabricant/modele
 * Zigbee pas encore connus a la creation de l'endpoint), sinon met a jour sa
 * valeur. vendor/model NULL ou vides : aucune action pour ce champ.
 */
esp_err_t matter_bridge_update_bridged_identity(int endpoint_id, const char *vendor,
                                                const char *model);

/**
 * Ajoute un endpoint dont les types d'appareil et clusters viennent du profil
 * du template (voir matter_profile.h). Memes regles que
 * matter_bridge_add_endpoint() pour preferred_ep.
 */
int matter_bridge_add_endpoint_profile(uint64_t ieee, const matter_profile_t *profile,
                                       const char *label, int preferred_ep);

// ----------------------------------------------------------------------------
// Endpoints virtuels : appareils propres au pont (thermostats virtuels), sans
// appareil Zigbee derriere
// ----------------------------------------------------------------------------

/**
 * Ecriture d'un controleur sur un endpoint virtuel (valeur numerique de
 * l'attribut ; is_null pour un attribut nullable mis a null).
 * @return ESP_ERR_NOT_FOUND si l'endpoint n'est pas virtuel : la commande
 *         suit alors le chemin des appareils Zigbee
 */
typedef esp_err_t (*matter_virtual_write_cb_t)(int endpoint_id, uint32_t cluster_id,
                                               uint32_t attr_id, int32_t value, bool is_null);
/** Ajoute un module d'endpoints virtuels (thermostats, groupes d'actions...) : 4 au plus. */
void matter_bridge_set_virtual_write_cb(matter_virtual_write_cb_t cb);

/**
 * Un preset personnalise de thermostat (issue #80), en plus des deux presets
 * fixes Confort/Hors-gel. handle : 0x03.. (0x01/0x02 reserves aux fixes).
 * heating_setpoint en centiemes de degre. scenario : valeur numerique de
 * chip::app::Clusters::Thermostat::PresetScenarioEnum (pas de dependance aux
 * headers Matter ici, cote C pur).
 */
#define MATTER_PRESET_NAME_LEN     32
#define MATTER_PRESET_MAX_CUSTOM   3
typedef struct {
    uint8_t  handle;
    char     name[MATTER_PRESET_NAME_LEN];
    int32_t  heating_setpoint;
    uint8_t  scenario;
} matter_preset_t;

/**
 * Invoque quand un controleur Matter selectionne un preset (commande
 * SetActivePresetRequest du cluster Thermostat, feature Presets) sur un
 * thermostat virtuel. handle : 0x01 "Confort", 0x02 "Hors-gel", 0x03+ un
 * preset personnalise (issue #80). Ne concerne que les endpoints crees avec
 * matter_profile_t.presets (voir matter_bridge_add_virtual_endpoint).
 */
typedef esp_err_t (*matter_thermostat_preset_cb_t)(int endpoint_id, uint8_t handle);
/** Un seul module gere les presets (les thermostats virtuels). */
void matter_bridge_set_thermostat_preset_cb(matter_thermostat_preset_cb_t cb);

/**
 * Invoque quand un controleur Matter valide l'edition des presets
 * personnalises d'un thermostat virtuel (AtomicRequest CommitWrite sur
 * l'attribut Presets, issue #80). Notification seule : le contenu final est
 * a relire via matter_bridge_get_thermostat_custom_presets().
 */
typedef esp_err_t (*matter_thermostat_preset_commit_cb_t)(int endpoint_id);
/** Un seul module gere les presets (les thermostats virtuels). */
void matter_bridge_set_thermostat_preset_commit_cb(matter_thermostat_preset_commit_cb_t cb);

/**
 * Lit la liste de presets personnalises courante d'un endpoint, telle que
 * retenue par le delegate Matter apres un commit (voir
 * matter_thermostat_preset_commit_cb_t). A appeler depuis la tache des
 * thermostats (prend lixee_chip_lock() en interne : ordre de verrou
 * identique a matter_sync_all()/SyncFromZone, jamais l'inverse).
 * @return false si l'endpoint n'a pas de delegate Presets.
 */
bool matter_bridge_get_thermostat_custom_presets(int endpoint_id, matter_preset_t *out,
                                                 int max_out, int *out_count);

/**
 * Debug (triage issue #80, edition des presets perso) : identifiants des
 * endpoints ayant un delegate Presets (thermostats virtuels avec
 * matter_profile_t.presets actif). out rempli jusqu'a max_out ; retourne le
 * nombre ecrit. Comme matter_bridge_get_thermostat_custom_presets(), peut
 * s'appeler depuis n'importe quelle tache qui ne detient pas s_lock
 * (thermostat.cpp) : seul lixee_chip_lock() est pris en interne, jamais
 * s_lock, donc pas de risque d'inversion de l'ordre des verrous (voir
 * commentaire en tete de thermostat_presets_delegate.h) -- utilise par
 * l'API web de debug (web_api.cpp), qui tourne sur la tache du serveur HTTP.
 */
int matter_bridge_list_thermostat_preset_endpoints(int *out, int max_out);

/**
 * Met a jour le contenu des presets Matter "Confort"/"Hors-gel" et des
 * presets personnalises (issue #80) d'une zone de thermostat virtuel, ainsi
 * que le handle actif courant, pour qu'un controleur Matter les lise a jour.
 * Consignes en centiemes de degre (comme matter_bridge_set_number sur
 * 0x0201/0x0012). active_custom_handle : 0 si Confort/Hors-gel est actif
 * (voir frost_active), sinon le handle du preset personnalise actif. Sans
 * effet si l'endpoint n'a pas la feature Presets.
 */
void matter_bridge_set_thermostat_presets(int endpoint_id, int32_t comfort_setpoint,
                                          int32_t frost_setpoint, bool frost_active,
                                          const matter_preset_t *custom, int custom_count,
                                          uint8_t active_custom_handle);

/**
 * Cree un endpoint virtuel. uid : identifiant stable (UniqueID), product :
 * nom de produit affiche (fabricant : LiXee). Memes regles que
 * matter_bridge_add_endpoint() pour preferred_ep.
 */
int matter_bridge_add_virtual_endpoint(uint64_t uid, const matter_profile_t *profile,
                                       const char *label, const char *product, int preferred_ep);

/**
 * Ecrit une valeur numerique dans un attribut, convertie au type de
 * l'attribut (entiers, enum, bitmap, nullable). Pas d'echo vers le callback
 * d'ecriture.
 */
esp_err_t matter_bridge_set_number(int endpoint_id, uint32_t cluster_id, uint32_t attr_id,
                                   int32_t value, bool is_null);

/**
 * Version materielle affichee par les controleurs (HardwareVersionString du
 * cluster Basic Information), a la place de la valeur de compilation. A
 * appeler apres matter_bridge_start().
 */
void matter_bridge_set_hardware_version_string(const char *hw);

/** Nombre d'appareils pontes (endpoints dynamiques hors racine et agregateur). */
int matter_bridge_bridged_count(void);

/** Nombre maximal d'appareils pontes (limite d'endpoints de la pile Matter). */
int matter_bridge_bridged_max(void);

/**
 * Evenements d'un bouton poussoir (cluster Switch) : presses = nombre
 * d'appuis (1 simple, 2 double...), 0 = appui long.
 */
esp_err_t matter_bridge_switch_event(int endpoint_id, uint8_t presses);

/**
 * Verifie si le bridge est commissione
 */
bool matter_bridge_is_commissioned(void);

/** Nombre de controleurs (fabrics) associes, -1 si la pile n'est pas demarree. */
int matter_bridge_fabric_count(void);

/**
 * Ouvre la fenetre d'appairage (methode de base, code habituel du pont) pour
 * `seconds` secondes : ajout d'un second controleur (Apple Maison, Google...)
 * sans passer par le partage du premier.
 */
esp_err_t matter_bridge_open_commissioning_window(uint16_t seconds);

/** Fenetre d'appairage ouverte. */
bool matter_bridge_commissioning_window_open(void);

/**
 * Force le factory reset du bridge Matter
 */
esp_err_t matter_bridge_factory_reset(void);

/**
 * Efface les donnees Matter de la NVS (fabrics, ACL, compteurs, etat des
 * endpoints) sans la pile : pour une pile qui n'a pas pu demarrer (NVS
 * pleine). Le code d'appairage (lx_matter) est garde. Redemarrage a faire.
 */
esp_err_t matter_bridge_erase_storage(void);

/**
 * Structure pour exporter les donnees energy du ZLinky via C linkage
 * Permet a web_api.cpp de lire les valeurs sans inclure les headers Matter SDK
 */
typedef struct {
    bool valid;              // true si un delegate a ete trouve
    int32_t active_power;    // W (0 si non disponible)
    int32_t apparent_power;  // VA (0 si non disponible)
    int32_t rms_voltage;     // V (0 si non disponible)
    int32_t rms_current;     // mA (0 si non disponible)
    bool has_active_power;
    bool has_apparent_power;
    bool has_rms_voltage;
    bool has_rms_current;
} matter_energy_data_t;

/**
 * Recupere les donnees energy du premier ZLinky trouve
 * Cherche un device de type MATTER_DEV_ENERGY_METER et lit son delegate
 */
esp_err_t matter_bridge_get_energy_data(matter_energy_data_t *out);

#ifdef __cplusplus
}

// C++ only: access au delegate ElectricalPowerMeasurement
class ElecPowerDelegate;
ElecPowerDelegate* matter_bridge_get_elec_delegate(int endpoint_id);

#endif
