#pragma once

#include "esp_http_server.h"

/**
 * API de fichiers des outils developpeur (portage des editeurs LiXee-Box :
 * configFiles, fsbrowser, hst, tp, rules, debugFiles, javascript) :
 *   GET  /api/files?dir=          liste d'un repertoire autorise
 *   GET  /api/files/read?dir=&name=
 *   POST /api/files/write?dir=&name=   corps = contenu brut
 *   POST /api/files/delete?dir=&name=
 * Reservee au mode developpeur. Voir web_files.cpp.
 */
void web_files_register(httpd_handle_t server);
