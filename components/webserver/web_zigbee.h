#pragma once

// ============================================================================
// Routes Zigbee (lot 3 du portage LiXee-Box) : appareils, valeurs formatees
// par template, actions, lecture / ecriture d'attributs, coordinateur ZiGate,
// console des trames, decouverte Home Assistant.
//
// Deux gestionnaires generiques seulement ("/api/zigbee/*" en GET et en
// POST) : la table des URI du serveur est partagee par tous les lots et
// limitee (max_uri_handlers). Le contrat des routes est decrit en tete de
// web_zigbee.cpp.
// ============================================================================

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

void web_zigbee_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
