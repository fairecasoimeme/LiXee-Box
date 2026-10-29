#pragma once

#include <stdbool.h>
#include "esp_http_server.h"

/**
 * Routes de la page Tunnel (acces distant, portage LiXee-Box) :
 *   GET  /api/tunnel/status            etat + configuration (jeton masque)
 *   POST /api/tunnel/config            interrupteur et identifiants (saveConfigTunnel)
 *   POST /api/tunnel/activate          activation par code a 6 chiffres
 *   GET  /api/tunnel/activate/status   resultat de l'activation
 *   GET  /api/tunnel/credentials       clientId + jeton en clair (refuse via le tunnel)
 *   POST /api/tunnel/credentials       mise a jour clientId / jeton
 *   GET  /api/tunnelStatus, /api/tunnelActivateStatus : anciens noms (lecture seule)
 * Contrat detaille : en-tete de web_tunnel.cpp.
 */
void web_tunnel_register(httpd_handle_t server);

/**
 * Vrai si la requete arrive par le tunnel : le pair est 127.0.0.1 (ou
 * ::ffff:127.0.0.1 / ::1), seule origine possible des requetes rejouees par
 * le composant tunnel. Equivalent de
 * request->client()->remoteIP() == IPAddress(127,0,0,1) sur la LiXee-Box.
 * Fiable : un client du reseau local ne peut pas etablir une connexion TCP
 * avec une adresse source de bouclage (la reponse SYN-ACK ne lui parviendrait
 * pas). A utiliser par web_server.cpp (auth_check / csrf_ok).
 */
bool web_tunnel_req_is_tunnel(httpd_req_t *req);
