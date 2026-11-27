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

#define ZIGATE_MAX_DEVICES   64
#define ZIGATE_MAX_EPS       8      // endpoints applicatifs retenus par appareil
#define ZIGATE_MAX_CLUSTERS  48     // clusters retenus (tous endpoints confondus)
#define ZIGATE_MAX_MATTER_WAYS 4    // voies exposees en Matter par leur propre endpoint

/** Endpoint applicatif d'un appareil (Simple Descriptor, profil HA ou ZLL) */
typedef struct {
    uint8_t  ep;
    uint16_t profile_id;
    uint16_t device_id;
} zigate_ep_info_t;

/** Voie d'un appareil multi-voies exposee par son propre endpoint Matter */
typedef struct {
    uint8_t  zb_ep;            // endpoint Zigbee de la voie
    uint16_t matter_ep;        // endpoint Matter attribue
} zigate_matter_way_t;

typedef struct {
    uint64_t ieee_addr;
    uint16_t short_addr;
    // Endpoint PRINCIPAL : premier endpoint de profil HA (0x0104), sinon ZLL
    // (0xC05E). Son Device ID et son profil sont ceux de l'appareil. Avant la
    // v3.4, c'etait le dernier endpoint decrit : un appareil a plusieurs
    // endpoints (routeur Ztriac 1/3/4...) gardait celui du dernier.
    uint16_t device_id;        // ZCL device ID (ex: 0x0100 = On/Off Light)
    uint16_t profile_id;       // Profile ID (0x0104 = Home Automation)
    uint8_t  endpoint;
    uint8_t  device_type;      // Router/EndDevice
    uint8_t  lqi;              // Derniere qualite de lien
    char     manufacturer[33]; // Nom fabricant
    char     model[33];        // Modele
    char     alias[33];        // Alias utilisateur
    bool     active;           // Device present sur le reseau
    uint32_t last_seen;        // Epoch derniere trame (zigbee_seen.json, voir zigate_devices_save_seen)
    // Endpoints applicatifs decrits (ordre de l'Active Endpoint Response)
    zigate_ep_info_t eps[ZIGATE_MAX_EPS];
    uint8_t  ep_count;
    // Clusters de TOUS les endpoints applicatifs, chacun avec son endpoint
    // (in_cluster_eps[i]) : un meme cluster peut figurer pour plusieurs
    // endpoints (deux sondes 0x0402). Enregistres dans zigbee_devices.json
    // depuis la v3.4 (perdus a chaque redemarrage avant).
    uint16_t in_clusters[ZIGATE_MAX_CLUSTERS];
    uint8_t  in_cluster_eps[ZIGATE_MAX_CLUSTERS];
    uint8_t  in_cluster_count;
    uint16_t out_clusters[ZIGATE_MAX_CLUSTERS];
    uint8_t  out_cluster_eps[ZIGATE_MAX_CLUSTERS];
    uint8_t  out_cluster_count;
    // Matter endpoint assigne (-1 = pas encore mappe)
    int      matter_endpoint;
    // Endpoints Matter des voies secondaires (deux voies On/Off, deux sondes
    // sur deux endpoints Zigbee : un endpoint ponte par voie, issue #120).
    // Numeros gardes d'un demarrage a l'autre, comme matter_endpoint.
    zigate_matter_way_t matter_ways[ZIGATE_MAX_MATTER_WAYS];
    uint8_t  matter_way_count;

    // Bind et reporting du template deja envoyes a l'appareil. L'appareil
    // les conserve : inutile (et usant pour sa flash) de les renvoyer a
    // chaque demarrage du pont.
    bool     configured;

    // Etat radio, en RAM seulement (jamais sauvegarde : une ecriture flash a
    // chaque trame userait la flash). radio_status : dernier code d'echec
    // APS (0x8702, ex. 0xE9 pas d'accuse, 0xD4 injoignable), 0 = joignable.
    uint8_t  radio_status;
    uint32_t last_rx_s;        // Derniere trame recue, secondes depuis le demarrage (0 = jamais)
    // Derniere redecouverte des endpoints tentee (appareil decouvert avant la
    // v3.4), secondes depuis le demarrage, 0 = jamais ; en RAM seulement
    uint32_t ep_refresh_s;
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

/**
 * Endpoint Matter de la voie zb_ep (appareil multi-voies), -1 si inconnu
 */
int zigate_devices_get_matter_way(uint64_t ieee_addr, uint8_t zb_ep);

/**
 * Enregistre l'endpoint Matter de la voie zb_ep (sauvegarde si change)
 */
esp_err_t zigate_devices_set_matter_way(uint64_t ieee_addr, uint8_t zb_ep, uint16_t matter_ep);

/**
 * Note la date de la derniere trame d'un appareil (enregistree plus tard par
 * zigate_devices_save_seen)
 */
void zigate_devices_mark_seen(zigate_device_t *dev, uint32_t epoch);

/**
 * Enregistre la derniere activite des appareils si elle a change : au plus
 * toutes les 10 min, ou tout de suite avec force (redemarrage prevu). Fichier
 * a part de zigbee_devices.json : une ecriture interrompue n'y perd que des
 * dates. Appele par la boucle principale avec la sauvegarde des fiches.
 */
void zigate_devices_save_seen(bool force);

/** true si l'appareil declare le cluster serveur (in) sur l'un de ses endpoints */
bool zigate_device_has_cluster(const zigate_device_t *dev, uint16_t cluster);

/**
 * Endpoint ou adresser le cluster serveur 'cluster' : l'endpoint principal
 * s'il le porte (ou si les clusters sont inconnus), sinon le premier endpoint
 * qui le declare. Jamais 0 (1 par defaut).
 */
uint8_t zigate_device_cluster_ep(const zigate_device_t *dev, uint16_t cluster);

/**
 * Enregistre le Simple Descriptor d'un endpoint applicatif : remplace ce que
 * l'appareil avait pour cet endpoint et recalcule l'endpoint principal
 * (device_id, profile_id, endpoint). Verrou de la base pris par l'appelant ou
 * appareil hors iteration concurrente (tache du protocole).
 */
void zigate_device_set_endpoint_desc(zigate_device_t *dev, uint8_t ep, uint16_t profile_id,
                                     uint16_t device_id, const uint16_t *in, int n_in,
                                     const uint16_t *out, int n_out);

/** Oublie les endpoints et clusters (nouvelle decouverte complete) */
void zigate_device_clear_endpoints(zigate_device_t *dev);

#ifdef __cplusplus
}
#endif
