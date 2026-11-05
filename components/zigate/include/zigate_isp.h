#pragma once

// ============================================================================
// Programmation du JN5189 par son bootloader ISP (protocole de NXP
// DK6Programmer), depuis l'ESP32-S3 : LiXee-Box Pro seulement, la seule carte
// qui pilote le reset (GPIO 15) et l'entree ISP (GPIO 21) du module.
// ============================================================================

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Plus grande image acceptee : la flash du JN5189 (632 Ko utiles) */
#define ZIGATE_ISP_IMAGE_MAX   (640 * 1024)

/** Avancement de la programmation : pourcentage et etape en clair. */
typedef void (*zigate_isp_progress_cb_t)(int percent, const char *step);

/**
 * Demarre le JN5189 en ISP, demande un deverrouillage puis rend la main au
 * firmware. true si le bootloader a repondu. UART non utilise par ailleurs
 * (zigate_init avant les taches, ou UART suspendu).
 */
bool zigate_isp_probe(void);

/**
 * Verifie qu'une image ressemble a un firmware JN5189 (pointeur de pile en
 * RAM, vecteur de reset dans l'image). err recoit la raison du refus.
 */
bool zigate_isp_image_valid(const uint8_t *img, size_t len, char *err, size_t err_len);

/**
 * Programme l'image a l'adresse 0 de la flash du JN5189 (effacement de la
 * seule zone de l'image : la PDM, en fin de flash, est conservee), relit
 * pour verifier, puis redemarre le module sur son firmware. Bloquant
 * (~1 min a 115200 bauds) ; suspend l'usage normal de l'UART pendant ce temps.
 */
esp_err_t zigate_isp_flash(const uint8_t *img, size_t len, zigate_isp_progress_cb_t cb,
                           char *err, size_t err_len);

#ifdef __cplusplus
}
#endif
