#pragma once

#include "esp_http_server.h"

/**
 * Routes des thermostats virtuels (portage LiXee-Box) :
 *   GET  /api/thermostats                 etat des zones (/loadThermostats)
 *   GET  /api/thermostats/config          configuration des zones
 *   GET  /api/thermostats/options         appareils par role, periodes tarifaires
 *   GET  /api/thermostats/actions?ieee=X  actions du template d'un appareil
 *   POST /api/thermostats/save            {id|"new", champs de la zone}
 *   POST /api/thermostats/setpoint        {id, value}
 *   POST /api/thermostats/force           {id, mode}   0 auto, 1 marche, 2 arret
 *   POST /api/thermostats/mode            {id, heat}
 *   POST /api/thermostats/frost           {id, on}
 *   POST /api/thermostats/toggle          {id}
 *   POST /api/thermostats/delete          {id}
 * Deux gestionnaires generiques (GET / POST), a enregistrer avant celui des
 * fichiers statiques.
 */
void web_thermostat_register(httpd_handle_t server);
