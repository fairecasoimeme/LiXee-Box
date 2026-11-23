#pragma once
// ============================================================================
// Appareils IP (Shelly, Tasmota...) : voir IP_DEVICES.md
//
// Appareils WiFi/Ethernet qui ont une API HTTP locale, pilotes par des
// templates (tp/ip.json : format des templates Zigbee, plus des champs
// "http"). Comme un emetteur LoRa, un appareil IP a une cle de type IEEE
// (EUI-64 tire de sa MAC) et ses valeurs suivent le chemin Zigbee
// (process_attribute de main.cpp) : fiche, regles, historique, MQTT.
//
// Une tache "ipdev" interroge les appareils l'un apres l'autre et execute
// les commandes, mises en file : aucun appel bloquant pour l'appelant.
// ============================================================================
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IPDEV_MAX            32
#define IPDEV_TEMPLATE_FILE  "ip.json"

typedef struct {
    uint64_t ieee;
    char     ip[40];
    uint16_t port;
    char     model[32];          // cle du template
    char     user[32];
    bool     has_pass;
    uint16_t interval_s;
    bool     online;             // dernier releve reussi
    int32_t  age_s;              // depuis le dernier releve reussi, -1 jamais
    uint32_t polls, errors;
    char     last_error[48];
    uint16_t matter_ep;
} ipdev_info_t;

/** Valeur relevee, au format d'un rapport d'attribut ZCL (donnees big-endian). */
typedef struct {
    uint64_t ieee;
    uint16_t cluster;
    uint16_t attr;
    uint8_t  type;               // 0x10 booleen, 0x2B entier signe 32 bits
    uint8_t  len;
    uint8_t  data[4];
} ipdev_report_t;

typedef void (*ipdev_report_cb_t)(const ipdev_report_t *r);

/** Charge la liste et les templates ; demarre la tache si le module est actif. */
esp_err_t ip_devices_init(void);

/** Chemin Zigbee des valeurs (main.cpp). Appele depuis la tache "ipdev". */
void ip_devices_register_report_cb(ipdev_report_cb_t cb);

bool ip_devices_enabled(void);
esp_err_t ip_devices_set_enabled(bool on);

/** true si la cle device_data est celle d'un appareil IP. */
bool ip_devices_is_ip(uint64_t ieee);

int  ip_devices_count(void);
bool ip_devices_get(int index, ipdev_info_t *out);
bool ip_devices_find(uint64_t ieee, ipdev_info_t *out);

/**
 * Commande de template (numero de commande Zigbee, 146 = On/Off) : l'action
 * du template qui a cette commande et cette valeur est appelee par HTTP, puis
 * l'appareil est relu. Mise en file : ESP_OK si acceptee.
 */
esp_err_t ip_devices_send_action(uint64_t ieee, int command, uint8_t endpoint, int32_t value);

/** Releve immediat (file de la tache). */
void ip_devices_poll_now(uint64_t ieee);

/** GET /api/ip/devices. A liberer par free(). */
char *ip_devices_list_json(void);

/** Modeles connus (cle, libelle) pour le choix manuel. A liberer par free(). */
char *ip_devices_models_json(void);

/**
 * Detection (POST /api/ip/probe) : modele propose, MAC, nom. Bloquant
 * (quelques requetes HTTP de 3 s au plus). A liberer par free().
 */
char *ip_devices_probe_json(const char *ip, uint16_t port, const char *user, const char *pass);

/**
 * Ajout. model vide : detection. pass NULL ou vide : sans mot de passe.
 * err : message pour l'utilisateur.
 */
esp_err_t ip_devices_add(const char *ip, uint16_t port, const char *model, const char *user,
                         const char *pass, int interval_s, const char *alias,
                         uint64_t *ieee_out, char *err, size_t err_len);

/** Modification ; pass NULL : inchange ; "" : efface. */
esp_err_t ip_devices_update(uint64_t ieee, const char *ip, uint16_t port, const char *model,
                            const char *user, const char *pass, int interval_s,
                            char *err, size_t err_len);

esp_err_t ip_devices_remove(uint64_t ieee, bool remove_data);

/** Endpoint Matter memorise (lot 2). */
esp_err_t ip_devices_set_matter_endpoint(uint64_t ieee, uint16_t ep);

#ifdef __cplusplus
}
#endif
