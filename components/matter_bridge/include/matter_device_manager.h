#pragma once

#include "esp_err.h"
#include "matter_bridge.h"
#include "zigate_devices.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Matter Device Manager
// Gere le mapping bidirectionnel entre devices Zigbee et endpoints Matter
// Convertit les clusters ZCL <-> clusters Matter
// ============================================================================

typedef struct {
    int      matter_endpoint;
    uint64_t zigbee_ieee;
    uint16_t zigbee_short_addr;
    matter_device_type_t device_type;   // sans profil : type deduit du DeviceId
    bool     active;
    // Profil du template (NULL : appareil sans template, conversion historique)
    matter_profile_t *profile;
    int32_t  last_lift100;              // derniere fermeture publiee (volet), -1 inconnue
    int64_t  last_move_us;              // dernier ordre de mouvement envoye au volet (0 : aucun)
} bridge_device_mapping_t;

/**
 * Initialise le device manager
 * Recharge les mappings depuis NVS
 */
esp_err_t matter_device_manager_init(void);

/**
 * Mappe un device Zigbee appaire vers un endpoint Matter
 * Cree automatiquement le bon type d'endpoint Matter selon le device_id Zigbee
 * @return matter endpoint ID, ou -1 si echec
 */
int matter_device_manager_map_device(const zigate_device_t *zigbee_dev);

/**
 * Template d'un appareil disponible apres son appairage (appareil endormi dont
 * le modele arrive tard) : un endpoint cree avec le type deduit du DeviceId est
 * recree sur le meme numero, compose depuis le template. Sans effet si
 * l'endpoint a deja un profil ; cree l'endpoint s'il n'existe pas.
 * @return endpoint Matter, -1 en cas d'echec
 */
int matter_device_manager_refresh_device(const zigate_device_t *zigbee_dev);

/**
 * Supprime le mapping d'un device
 */
esp_err_t matter_device_manager_unmap_device(uint64_t ieee_addr);

/**
 * Traite un rapport d'attribut Zigbee et le propage vers Matter
 * Conversion ZCL -> Matter clusters/attributs
 */
esp_err_t matter_device_manager_handle_zigbee_report(const zigate_attribute_report_t *report);

/**
 * Mappe un emetteur LoRa (ZLinky LoRa) vers un endpoint Matter compteur
 * d'energie. preferred_ep : endpoint d'un demarrage precedent (0 = nouveau).
 * @return endpoint Matter (celui deja attribue si l'emetteur est deja mappe),
 *         -1 si le modele n'est pas ponte ou en cas d'echec
 */
int matter_device_manager_map_lora_device(uint64_t ieee, const char *model,
                                          const char *label, int preferred_ep);

/**
 * Propage vers Matter un rapport d'un appareil identifie par sa cle 64 bits
 * (emetteur LoRa : pas d'adresse courte Zigbee). model : modele de l'appareil.
 */
esp_err_t matter_device_manager_handle_report_for_ieee(uint64_t ieee, const char *model,
                                                       const zigate_attribute_report_t *report);

/**
 * Traite une commande Matter et l'envoie au device Zigbee
 * Conversion Matter command -> ZCL command
 * Appele par le callback write du stack Matter
 */
esp_err_t matter_device_manager_handle_matter_command(int matter_endpoint,
                                                       uint32_t cluster_id,
                                                       uint32_t command_id,
                                                       const void *data, size_t data_len);

/**
 * Retourne le mapping pour un endpoint Matter donne
 */
const bridge_device_mapping_t* matter_device_manager_get_mapping(int matter_endpoint);

/**
 * Retourne le mapping pour une adresse IEEE donnee
 */
const bridge_device_mapping_t* matter_device_manager_get_mapping_by_ieee(uint64_t ieee_addr);

/**
 * Convertit un device_id ZCL en type de device Matter
 */
matter_device_type_t matter_device_manager_zcl_to_matter_type(uint16_t zcl_device_id);

/**
 * Sauvegarde les mappings en NVS
 */
esp_err_t matter_device_manager_save(void);

// ----------------------------------------------------------------------------
// Appareils retires de Matter (choix de l'utilisateur, page Passerelle ->
// Matter). Liste persistante (/littlefs/config/matterExclude.json) d'IEEE,
// ou d'identifiants d'endpoints virtuels (thermostats).
// ----------------------------------------------------------------------------

/** true si l'appareil est retire de Matter (aucun endpoint cree). */
bool matter_device_manager_is_excluded(uint64_t ieee);

/** Liste des appareils retires, a liberer par free() (tableau JSON d'IEEE). */
char *matter_device_manager_excluded_json(void);

/**
 * Retire (excluded = true) ou remet un appareil dans Matter. Un appareil
 * Zigbee / LoRa retire perd son endpoint tout de suite ; la remise et les
 * endpoints virtuels sont faits par le rappel ci-dessous.
 */
esp_err_t matter_device_manager_set_excluded(uint64_t ieee, bool excluded);

/** Rappel apres un changement : republier l'appareil, mettre a jour les thermostats. */
typedef void (*matter_exposure_cb_t)(uint64_t ieee, bool excluded);
void matter_device_manager_set_exposure_cb(matter_exposure_cb_t cb);

#ifdef __cplusplus
}
#endif
