#pragma once

#include "esp_http_server.h"

/**
 * Routes des chauffe-eau virtuels (voir components/water_heater/) :
 *   GET  /api/waterheaters                etat des zones
 *   GET  /api/waterheaters/config         configuration des zones
 *   GET  /api/waterheaters/options        appareils par role, periodes tarifaires
 *   POST /api/waterheaters/save           {id|"new", champs de la zone}
 *   POST /api/waterheaters/setpoint       {id, value}
 *   POST /api/waterheaters/mode           {id, mode}   0 arret, 1 marche forcee, 2 programme
 *   POST /api/waterheaters/toggle         {id}
 *   POST /api/waterheaters/boost          {id, durationSec[, oneShot, emergency, tempC]}
 *   POST /api/waterheaters/cancelboost    {id}
 *   POST /api/waterheaters/delete         {id}
 * Deux gestionnaires generiques (GET / POST), a enregistrer avant celui des
 * fichiers statiques.
 */
void web_waterheater_register(httpd_handle_t server);
