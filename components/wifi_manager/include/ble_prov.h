#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// BLE Provisioning - Nordic UART Service (NUS) compatible LiXee-Assist
//
// UUIDs NUS:
//   Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
//   RX (ecriture client): 6E400002-B5A3-F393-E0A9-E50E24DCCA9E
//   TX (notification):    6E400003-B5A3-F393-E0A9-E50E24DCCA9E
//
// Protocole LiXee-Assist:
//   Client envoie: "SSID|PASSWORD"
//   Device repond: "ok" puis callback
//   Commandes: PING -> "ok", TEST -> message, STATUS -> infos
// ============================================================================

/**
 * Callback appele quand des credentials WiFi sont recus via BLE.
 * @param ssid     SSID recu
 * @param password Mot de passe recu
 * @param ctx      Contexte utilisateur
 */
typedef void (*ble_prov_credentials_cb_t)(const char *ssid, const char *password, void *ctx);

/**
 * Initialise le BLE provisioning avec NimBLE + GATT NUS.
 *
 * @param cb         Callback credentials (appele quand SSID|PASSWORD est recu)
 * @param ctx        Contexte passe au callback
 * @param standalone Si true, initialise NimBLE nous-memes.
 *                   Si false, assume que NimBLE est deja initialise (ex: par Matter).
 */
esp_err_t ble_prov_init(ble_prov_credentials_cb_t cb, void *ctx, bool standalone);

/**
 * Demarre l'advertising BLE (le device devient visible).
 */
esp_err_t ble_prov_start_advertising(void);

/**
 * Arrete le BLE provisioning.
 * @param full_deinit Si true, deinitialise completement NimBLE.
 */
esp_err_t ble_prov_stop(bool full_deinit);

/**
 * Envoie un message au client BLE connecte (notification TX).
 */
esp_err_t ble_prov_send_message(const char *msg);

/**
 * Retourne true si le BLE provisioning est actif (advertising ou connecte).
 */
bool ble_prov_is_active(void);

/**
 * Retourne true si un client BLE est connecte.
 */
bool ble_prov_is_client_connected(void);

/**
 * Verifie si des credentials sont en attente de traitement.
 * Si oui, appelle le callback credentials dans le contexte de la tache appelante.
 * Doit etre appele regulierement depuis la boucle principale (pas depuis NimBLE).
 * @return true si des credentials ont ete traitees
 */
bool ble_prov_process_pending(void);

#ifdef __cplusplus
}
#endif
