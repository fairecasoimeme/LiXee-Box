#pragma once

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Web API - Endpoints REST pour le controle du bridge
// ============================================================================

// --- API Zigbee ---
// GET  /api/zigbee/devices       - Liste des devices appairies
// POST /api/zigbee/permit_join   - Active l'appairage (body: {"duration": 60})
// POST /api/zigbee/remove        - Supprime un device (body: {"ieee": "..."})
// GET  /api/zigbee/device/:ieee  - Details d'un device
// POST /api/zigbee/action        - Envoie une commande (body: {"ieee":"...", "cmd":...})

// --- API Matter ---
// GET  /api/matter/status        - Etat du bridge Matter (commissione, fabrics, etc.)
// POST /api/matter/factory_reset - Factory reset Matter
// GET  /api/matter/qrcode        - QR code pour commissioning

// --- API Config ---
// GET  /api/config               - Configuration actuelle
// POST /api/config               - Sauvegarde configuration
// GET  /api/config/wifi          - Config WiFi
// POST /api/config/wifi          - Sauvegarde config WiFi

// --- API Systeme ---
// GET  /api/system/info          - Infos systeme (RAM, flash, version, uptime)
// POST /api/system/reboot        - Redemarrage
// POST /api/system/update        - OTA firmware
// GET  /api/system/logs          - Logs recents

/**
 * Enregistre tous les handlers API sur le serveur web
 */
esp_err_t web_api_register_handlers(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
