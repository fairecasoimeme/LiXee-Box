#pragma once

// ============================================================================
// Mise a jour OTA des appareils Zigbee (cluster 0x0019, serveur dans la ZiGate)
//
// Reprise de la LiXee-Box (loadOTAFile / sendOta* / cases 0x8501 et 0x8503) :
//  - le fichier /littlefs/ota/<IEEE>.ota est charge en PSRAM, son en-tete est
//    confie a la ZiGate (0x0500 Load New Image) puis l'appareil est prevenu
//    (0x0505 Image Notify) ;
//  - l'appareil demande ensuite l'image bloc par bloc : la ZiGate remonte
//    chaque demande (0x8501) et le pont repond avec les octets (0x0502) ;
//  - a la fin (0x8503) le pont autorise le redemarrage (0x0504) et relit la
//    version logicielle de l'appareil.
// Un seul fichier charge a la fois, comme la LiXee-Box.
// ============================================================================

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "zigate_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZIGATE_OTA_MAX_FILE   (1024 * 1024)   // 1 Mo (PSRAM)

typedef enum {
    ZIGATE_OTA_IDLE = 0,
    ZIGATE_OTA_NOTIFIED,      // image annoncee, en attente des demandes de l'appareil
    ZIGATE_OTA_TRANSFER,      // blocs en cours d'envoi
    ZIGATE_OTA_DONE,          // appareil mis a jour (Upgrade End OK)
    ZIGATE_OTA_ERROR,
} zigate_ota_state_t;

typedef struct {
    zigate_ota_state_t state;
    uint64_t ieee;
    uint16_t short_addr;
    uint32_t offset;          // dernier offset demande
    uint32_t total;           // taille de l'image
    uint8_t  pct;
    uint32_t file_version;
    uint16_t image_type;
    uint16_t manu_code;
    uint8_t  end_status;      // statut de l'Upgrade End (0 = OK)
    uint32_t since_s;         // secondes depuis le dernier evenement
    char     msg[64];
} zigate_ota_status_t;

/** Cree le verrou (zigate_init, avant les taches de la ZiGate). */
void zigate_ota_prepare(void);

/** Chemin du fichier OTA d'un appareil : /littlefs/ota/<IEEE>.ota */
void zigate_ota_file_path(uint64_t ieee, char *out, size_t len);

/**
 * Charge /littlefs/ota/<IEEE>.ota et lance la mise a jour (0x0500 + 0x0505).
 * Lit la flash : a appeler depuis une tache a pile en RAM interne (HTTP).
 */
esp_err_t zigate_ota_start(uint64_t ieee, char *err, size_t err_len);

/** Abandonne la mise a jour en cours et libere l'image. */
void zigate_ota_cancel(void);

/** Etat courant (appareil concerne dans st->ieee). */
void zigate_ota_get_status(zigate_ota_status_t *st);

/** Messages 0x8501 / 0x8503 (tache de traitement ZiGate). */
void zigate_ota_handle_packet(const zigate_packet_t *pkt);

#ifdef __cplusplus
}
#endif
