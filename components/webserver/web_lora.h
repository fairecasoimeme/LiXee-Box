#pragma once

#include "esp_http_server.h"

/**
 * Routes du module LoRa (lot LoRa du portage LiXee-Box) : etat du recepteur,
 * appareils, appairage, suppression, parametres reseau (canal/SF), lecture
 * d'attribut a la demande (POLL), depot et lancement de la mise a jour OTA
 * d'un emetteur.
 *
 * Deux gestionnaires generiques seulement (prefixe /api/lora/, en GET et en POST),
 * aiguilles en interne, pour menager les max_uri_handlers du serveur. A
 * enregistrer AVANT le gestionnaire generique des fichiers statiques.
 */
void web_lora_register(httpd_handle_t server);
