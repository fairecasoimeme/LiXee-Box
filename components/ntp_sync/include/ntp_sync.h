#pragma once

// ============================================================================
// NTP Sync - Synchronisation de l'heure via SNTP (ESP-IDF natif)
//
// Remplace NTPClient Arduino par esp_sntp (lwIP SNTP intere a ESP-IDF).
// Configure le fuseau horaire via POSIX TZ string (setenv/tzset).
//
// Architecture:
//   - Utilise esp_sntp pour la synchro initiale + rafraichissement periodique
//   - Met a jour les variables globales (epoch, heure, date) pour compatibilite
//   - La config (serveur NTP, timezone, offset) vient de config_manager
// ============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// API publique
// ============================================================================

/**
 * Initialise et demarre la synchronisation NTP.
 * Utilise le serveur et le fuseau horaire depuis config_manager.
 * @return ESP_OK si demarre, ESP_FAIL si erreur
 */
esp_err_t ntp_sync_init(void);

/**
 * Force une resynchronisation immediate.
 */
esp_err_t ntp_sync_force_update(void);

/** Applique tout de suite le fuseau et le serveur NTP de la configuration. */
esp_err_t ntp_sync_apply_config(void);

/** Regle l'heure a la main (epoch UTC), en attendant ou a defaut de NTP. */
esp_err_t ntp_sync_set_time(time_t epoch);

/**
 * Retourne true si l'heure a ete synchronisee au moins une fois.
 */
bool ntp_sync_is_synced(void);

/**
 * Retourne l'epoch time courant (secondes depuis 1970-01-01 00:00:00 UTC).
 * Retourne 0 si pas encore synchronise.
 */
time_t ntp_sync_get_epoch(void);

/**
 * Retourne la structure tm locale (avec timezone applique).
 * @param out_tm  Structure tm en sortie
 * @return ESP_OK si l'heure est valide
 */
esp_err_t ntp_sync_get_local_time(struct tm *out_tm);

/**
 * Retourne une date formatee "YYYY-MM-DD HH:MM" dans le buffer.
 * @param buf     Buffer de sortie (minimum 17 chars)
 * @param buf_len Taille du buffer
 * @return ESP_OK si formatte
 */
esp_err_t ntp_sync_get_formatted_datetime(char *buf, size_t buf_len);

/**
 * Retourne l'heure formatee "HH:MM:SS" dans le buffer.
 */
esp_err_t ntp_sync_get_formatted_time(char *buf, size_t buf_len);

/**
 * Retourne la date formatee "YYYY-MM-DD" dans le buffer.
 */
esp_err_t ntp_sync_get_formatted_date(char *buf, size_t buf_len);

/**
 * Accesseurs individuels pour compatibilite Arduino.
 */
int ntp_sync_get_year(void);
int ntp_sync_get_month(void);   // 1-12
int ntp_sync_get_day(void);     // 1-31
int ntp_sync_get_hour(void);    // 0-23
int ntp_sync_get_minute(void);  // 0-59

#ifdef __cplusplus
}
#endif
