#pragma once

#include "esp_http_server.h"

// Routes des regles et des groupes d'actions (lot 5 du portage LiXee-Box).
// Quatre gestionnaires generiques seulement (max_uri_handlers est compte) :
//   GET  /api/rules/{op}         op = list, get?name=, status?name=, devices, raw
//   POST /api/rules/{op}         op = add, edit, delete, toggle, raw
//   GET  /api/actiongroups/{op}  op = list, devices
//   POST /api/actiongroups/{op}  op = run, delete, save
// A enregistrer AVANT le gestionnaire generique des fichiers statiques.
void web_rules_register(httpd_handle_t server);
