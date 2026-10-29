#pragma once

// ============================================================================
// Routes du centre de notifications et de la configuration email (lot 4 du
// portage LiXee-Box). Contrat : voir web_notif.cpp (en-tete).
// ============================================================================

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Enregistre les 6 routes du lot (a appeler avant le handler des fichiers statiques). */
void web_notif_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
