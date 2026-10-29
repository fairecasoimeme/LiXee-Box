#pragma once

// ============================================================================
// MQTT Manager - Publication des donnees Zigbee vers un broker MQTT
//
// Port du mqtt.cpp Arduino vers ESP-IDF natif (esp_mqtt_client).
//
// Architecture:
//   - Utilise directement esp_mqtt_client (pas de wrapper Arduino)
//   - 3 modes de publication: Home Assistant (HA), ThingsBoard (TB), Custom
//   - Reconnection automatique avec backoff exponentiel
//   - Health check / heartbeat toutes les 30s
//   - Souscription a "lixee/cmd/#" pour les commandes
//   - Publication declenchee par les rapports d'attributs Zigbee
//
// Configuration depuis config_manager:
//   - servMQTT, portMQTT, clientIDMQTT, userMQTT, passMQTT
//   - headerMQTT (prefix des topics)
//   - haMQTT / tbMQTT / customMQTT (modes)
//   - customMQTTJson (template JSON pour mode custom)
// ============================================================================

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration
// ============================================================================

#define MQTT_RECONNECT_DELAY_MS      5000     // Delai entre tentatives de reconnexion
#define MQTT_RECONNECT_MAX_DELAY_MS  30000    // Delai max apres N echecs
#define MQTT_MAX_RECONNECT_ATTEMPTS  10       // Seuil pour passer au delai max
#define MQTT_HEALTH_CHECK_INTERVAL_S 30       // Heartbeat toutes les 30s
#define MQTT_MAX_PUBLISH_ERRORS      3        // Erreurs avant deconnexion forcee

// ============================================================================
// API publique
// ============================================================================

/**
 * Initialise le client MQTT.
 * Configure le client avec les parametres de config_manager.
 * Ne se connecte pas encore (appeler mqtt_manager_start).
 */
esp_err_t mqtt_manager_init(void);

/**
 * Demarre la connexion au broker MQTT.
 * Le client se connectera en arriere-plan.
 */
esp_err_t mqtt_manager_start(void);

/**
 * Arrete le client MQTT proprement.
 */
esp_err_t mqtt_manager_stop(void);

/**
 * Applique une nouvelle configuration MQTT sans redemarrer (page MQTT) :
 * le client est arrete puis recree par mqtt_manager_loop().
 */
void mqtt_manager_request_reload(void);

/**
 * Retourne true si le client est connecte au broker.
 */
bool mqtt_manager_is_connected(void);

/**
 * Publie une valeur d'attribut Zigbee.
 * Le format du topic et du payload depend du mode (HA/TB/Custom).
 *
 * @param ieee_addr  Adresse IEEE du device (hex string, ex: "00158D0001234567")
 * @param cluster    Cluster ID (ex: "0402")
 * @param attribute  Attribute ID (ex: "0")
 * @param value      Valeur brute (hex string du device_data)
 * @param is_string  true si la valeur est un texte (pas un nombre hex)
 */
esp_err_t mqtt_manager_publish_value(const char *ieee_addr,
                                      const char *cluster,
                                      const char *attribute,
                                      const char *value,
                                      bool is_string,
                                      int64_t num_value);

/**
 * Publie un attribut recu (Zigbee ou LoRa) au format de la LiXee-Box :
 * cluster et attribut en DECIMAL dans le topic et la cle ("_1794_0"), texte
 * decode pour les chaines ZCL (0x41..0x44), nombre sinon.
 * @param hex       valeur brute en hexa (comme device_data)
 * @param zcl_type  type ZCL du rapport (0 si inconnu)
 */
/**
 * Entites tarif du Linky (publishLinkyTariffInfo de la LiXee-Box), en mode
 * Home Assistant : <en-tete><IEEE>_tarif_actuel / _couleur_jour /
 * _couleur_demain, messages retenus, publies au changement seulement.
 * today / tomorrow : 0 inconnu, 1 bleu, 2 blanc, 3 rouge.
 */
void mqtt_manager_publish_linky_tariff(uint64_t ieee_addr, const char *tarif, int today, int tomorrow);

esp_err_t mqtt_manager_publish_attr(uint64_t ieee_addr, uint16_t cluster, uint16_t attr,
                                    const char *hex, uint8_t zcl_type, bool has_num, int64_t num);

/**
 * Publie toutes les valeurs d'un device (apres un cycle de poll complet).
 * Itere les clusters/attributs du device_data et publie chacun.
 *
 * @param ieee_addr  Adresse IEEE du device (uint64_t)
 */
esp_err_t mqtt_manager_publish_device(uint64_t ieee_addr);

/**
 * Callback MQTT pour les commandes recues sur "lixee/cmd/#".
 * Enregistre un handler appele quand un message arrive sur ce topic.
 *
 * @param handler  Fonction appelee avec (ieee_str, cmd_type, payload)
 */
typedef void (*mqtt_cmd_handler_t)(const char *ieee_addr,
                                    const char *cmd_type,
                                    const char *payload);
void mqtt_manager_register_cmd_handler(mqtt_cmd_handler_t handler);

/**
 * Publie la decouverte automatique Home Assistant d'un appareil (portage de
 * handleSendMqttDiscover / bouton "MQTT Discover" de la LiXee-Box) :
 *   - un capteur par etat du template ayant une icone MQTT ("mqtt_icon"),
 *     filtre par mode TIC pour le ZLinky ;
 *   - un interrupteur pour une paire d'actions ON / OFF, et pour chaque paire
 *     <nom>_ON / <nom>_OFF ;
 *   - un bouton pour chacune des autres actions visibles.
 * Messages retenus (retain). Les topics d'etat suivent la publication de ce
 * pont ({header}{IEEE}_{cluster hexa}_{attribut}/state) ; topics de config
 * et unique_id reprennent ceux de la LiXee-Box pour remplacer proprement ses
 * entites lors d'une migration.
 * @param out_count  nombre de messages publies (peut etre NULL)
 * @return ESP_ERR_INVALID_STATE si non connecte, ESP_ERR_NOT_FOUND si
 *         l'appareil ou son template est inconnu
 */
esp_err_t mqtt_manager_publish_discovery(uint64_t ieee_addr, int *out_count);

/**
 * Comme mqtt_manager_publish_discovery(), pour un appareil qui n'est pas dans
 * la base Zigbee (emetteur LoRa) : modele, alias et fabricant fournis.
 */
esp_err_t mqtt_manager_publish_discovery_info(uint64_t ieee_addr, const char *model, const char *alias,
                                              const char *manufacturer, int *out_count);

/**
 * Doit etre appele periodiquement (ex: chaque seconde) depuis la boucle main.
 * Gere le health check et la reconnexion.
 */
void mqtt_manager_loop(void);

#ifdef __cplusplus
}
#endif
