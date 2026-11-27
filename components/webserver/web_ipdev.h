#pragma once

#include "esp_http_server.h"

/**
 * Routes des appareils IP (IP_DEVICES.md) : liste, modeles, detection, ajout,
 * modification, suppression, releve, activation du module. Deux
 * gestionnaires generiques (/api/ip/ en GET et en POST), comme le LoRa. A
 * enregistrer AVANT le gestionnaire generique des fichiers statiques.
 */
void web_ipdev_register(httpd_handle_t server);
