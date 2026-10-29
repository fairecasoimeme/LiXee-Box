#pragma once

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Routes de compatibilite avec l'API publique de la LiXee-Box (web_compat.cpp). */
void web_compat_register(httpd_handle_t server);

/** Temperature de la puce (degC, -1000 si indisponible), pour /api/system. */
float web_api_chip_temperature(void);

#ifdef __cplusplus
}
#endif
