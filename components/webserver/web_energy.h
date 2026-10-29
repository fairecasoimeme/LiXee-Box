#pragma once

#include "esp_http_server.h"

/**
 * Routes de la page Energie (lot 2 du portage LiXee-Box), contrat dans
 * docs/energy_api.md : summary, gauge, distribution, chart (+ export CSV),
 * power (+ export CSV), tariff, linky, sous-compteurs.
 */
void web_energy_register(httpd_handle_t server);
