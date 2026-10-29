#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Web Server - Interface de configuration et monitoring
// Sert les fichiers statiques (HTML/CSS/JS) depuis LittleFS
// Expose une API REST pour le parametrage et le controle
// ============================================================================

#define WEB_SERVER_PORT 80

/**
 * Initialise et demarre le serveur web HTTP
 * Enregistre tous les handlers (pages + API)
 */
esp_err_t web_server_init(void);

/**
 * Arrete le serveur web
 */
esp_err_t web_server_stop(void);

/**
 * Retourne le handle du serveur (pour ajouter des handlers custom)
 */
/**
 * Enregistre un handler d'URI DERRIERE le controle d'authentification.
 *
 * A utiliser a la place de httpd_register_uri_handler() pour tout endpoint :
 * le handler reel est place dans user_ctx et un trampoline commun verifie les
 * identifiants avant de l'appeler. Oublier un endpoint devient donc impossible
 * sans contourner explicitement cette fonction.
 */
esp_err_t web_server_register_uri(httpd_handle_t server, const httpd_uri_t *uri);

httpd_handle_t web_server_get_handle(void);

/** Requete du pont lui-meme (Origin / Referer absent ou egal a l'hote) : pour une route GET qui agit. */
bool web_server_origin_ok(httpd_req_t *req);

#ifdef __cplusplus
}
#endif
