#pragma once

#include "zigate_driver.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// ZiGate Devices - Gestion de la base de donnees des devices Zigbee appairies
// Stockage en RAM + persistance LittleFS
// ============================================================================

#define ZIGATE_MAX_DEVICES 64

typedef struct {
    uint64_t ieee_addr;
    uint16_t short_addr;
    uint16_t device_id;        // ZCL device ID (ex: 0x0100 = On/Off Light)
    uint16_t profile_id;       // Profile ID (0x0104 = Home Automation)
    uint8_t  endpoint;
    uint8_t  device_type;      // Router/EndDevice
    uint8_t  lqi;              // Derniere qualite de lien
    char     manufacturer[33]; // Nom fabricant
    char     model[33];        // Modele
    char     alias[33];        // Alias utilisateur
    bool     active;           // Device present sur le reseau
    uint32_t last_seen;        // Epoch timestamp derniere communication
    // Clusters supportes
    uint16_t in_clusters[32];
    uint8_t  in_cluster_count;
    uint16_t out_clusters[32];
    uint8_t  out_cluster_count;
    // Matter endpoint assigne (-1 = pas encore mappe)
    int      matter_endpoint;

    // Bind et reporting du template deja envoyes a l'appareil. L'appareil
    // les conserve : inutile (et usant pour sa flash) de les renvoyer a
    // chaque demarrage du pont.
    bool     configured;

    // Etat radio, en RAM seulement (jamais sauvegarde : une ecriture flash a
    // chaque trame userait la flash). radio_status : dernier code d'echec
    // APS (0x8702, ex. 0xE9 pas d'accuse, 0xD4 injoignable), 0 = joignable.
    uint8_t  radio_status;
    uint32_t last_rx_s;        // Derniere trame recue, secondes depuis le demarrage (0 = jamais)
} zigate_device_t;

/**
 * Initialise la base de donnees des devices
 * Charge depuis LittleFS si disponible
 */
esp_err_t zigate_devices_init(void);

/**
 * Ajoute ou met a jour un device
 * @return Index du device, ou -1 si plein
 */
int zigate_devices_add_or_update(const zigate_device_t *dev);

/**
 * Cherche un device par adresse IEEE
 * @return Pointeur vers le device, ou NULL
 */
zigate_device_t* zigate_devices_find_by_ieee(uint64_t ieee_addr);

/**
 * Cherche un device par adresse courte
 */
zigate_device_t* zigate_devices_find_by_short(uint16_t short_addr);

/**
 * Supprime un device par adresse IEEE
 */
esp_err_t zigate_devices_remove(uint64_t ieee_addr);

/**
 * Retourne le nombre de devices actifs
 */
int zigate_devices_count(void);

/**
 * Retourne un device par index (pour iteration)
 */
zigate_device_t* zigate_devices_get(int index);

/**
 * Marque un appareil comme configure (bind + reporting envoyes) et sauvegarde
 */
esp_err_t zigate_devices_set_configured(uint64_t ieee_addr, bool configured);

/**
 * Sauvegarde la base dans LittleFS
 */
esp_err_t zigate_devices_save(void);

/**
 * Charge la base depuis LittleFS
 */
esp_err_t zigate_devices_load(void);

/**
 * Assigne un endpoint Matter a un device Zigbee
 */
esp_err_t zigate_devices_set_matter_endpoint(uint64_t ieee_addr, int matter_ep);

#ifdef __cplusplus
}
#endif
