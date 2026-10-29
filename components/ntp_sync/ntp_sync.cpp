// ============================================================================
// NTP Sync - Synchronisation de l'heure via SNTP natif ESP-IDF
//
// Utilise esp_sntp (lwIP) pour la synchronisation.
// Configure le fuseau horaire via POSIX TZ string.
// ============================================================================

#include "ntp_sync.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_sntp.h"

#include "config_manager.h"

static const char *TAG = "NTP_SYNC";

// ============================================================================
// Etat interne
// ============================================================================

static bool s_initialized = false;
static bool s_synced = false;

// ============================================================================
// Callback de synchronisation SNTP
// ============================================================================

/** Fuseau horaire POSIX de la configuration (ou offset simple, ou UTC). */
static void apply_tz(void)
{
    const cfg_general_t *gen = cfg_get_general();
    if (gen->timezone[0] != '\0') {
        setenv("TZ", gen->timezone, 1);
    } else if (gen->time_offset != 0) {
        char tz_buf[16];
        snprintf(tz_buf, sizeof(tz_buf), "UTC%+d", -gen->time_offset);   // signe POSIX inverse
        setenv("TZ", tz_buf, 1);
    } else {
        setenv("TZ", "UTC0", 1);
    }
    tzset();
}

static const char *server_name(void)
{
    const cfg_general_t *gen = cfg_get_general();
    return gen->ntp_server[0] ? gen->ntp_server : "pool.ntp.org";
}

static void ntp_sync_notification_cb(struct timeval *tv)
{
    s_synced = true;

    time_t now = tv->tv_sec;
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "Time synchronized: %s (epoch=%ld)", buf, (long)now);
}

// ============================================================================
// API publique
// ============================================================================

esp_err_t ntp_sync_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    const cfg_general_t *gen = cfg_get_general();
    apply_tz();

    // Determiner le serveur NTP
    const char *ntp_server = server_name();
    ESP_LOGI(TAG, "NTP server: %s", ntp_server);

    // Configurer SNTP
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntp_server);
    sntp_set_time_sync_notification_cb(ntp_sync_notification_cb);

    // Intervalle de synchro: defaut SNTP = 3600s (1h)
    // Peut etre ajuste avec CONFIG_LWIP_SNTP_UPDATE_DELAY dans sdkconfig

    esp_sntp_init();

    s_initialized = true;
    ESP_LOGI(TAG, "SNTP initialized, waiting for sync...");

    // Attendre la premiere synchro (max 10s, non-bloquant apres)
    int retry = 0;
    const int max_retry = 10;
    while (!s_synced && ++retry <= max_retry) {
        ESP_LOGD(TAG, "Waiting for NTP sync... (%d/%d)", retry, max_retry);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (s_synced) {
        ESP_LOGI(TAG, "NTP synced on first attempt");
    } else {
        ESP_LOGW(TAG, "NTP not yet synced (will retry in background)");

        // Si on a un epoch time sauvegarde dans la config, l'utiliser comme fallback
        if (gen->epoch_time > 1700000000) {  // Plausible (apres 2023)
            struct timeval tv = { .tv_sec = gen->epoch_time, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            ESP_LOGI(TAG, "Using saved epoch time as fallback: %ld", gen->epoch_time);
        }
    }

    return ESP_OK;
}

esp_err_t ntp_sync_apply_config(void)
{
    apply_tz();
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    esp_sntp_stop();
    esp_sntp_setservername(0, server_name());
    esp_sntp_init();
    ESP_LOGI(TAG, "Configuration NTP appliquee (%s)", server_name());
    return ESP_OK;
}

esp_err_t ntp_sync_set_time(time_t epoch)
{
    if (epoch < 1577836800) return ESP_ERR_INVALID_ARG;     // avant 2020 : refuse
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    // Heure valide pour les modules qui attendent une heure fiable
    // (historiques, plages horaires) ; NTP la corrigera s'il repond
    s_synced = true;
    ESP_LOGI(TAG, "Heure reglee a la main : %ld", (long)epoch);
    return ESP_OK;
}

esp_err_t ntp_sync_force_update(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    esp_sntp_restart();
    ESP_LOGI(TAG, "Forced NTP resync");
    return ESP_OK;
}

bool ntp_sync_is_synced(void)
{
    return s_synced;
}

time_t ntp_sync_get_epoch(void)
{
    time_t now;
    time(&now);
    // Retourner 0 si pas synchronise et time < 2020 (epoch fictif)
    if (!s_synced && now < 1577836800) {
        return 0;
    }
    return now;
}

esp_err_t ntp_sync_get_local_time(struct tm *out_tm)
{
    if (!out_tm) return ESP_ERR_INVALID_ARG;

    time_t now;
    time(&now);
    localtime_r(&now, out_tm);

    // Verifier que l'heure est plausible
    if (out_tm->tm_year < (2020 - 1900)) {
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

esp_err_t ntp_sync_get_formatted_datetime(char *buf, size_t buf_len)
{
    struct tm timeinfo;
    esp_err_t ret = ntp_sync_get_local_time(&timeinfo);
    if (ret != ESP_OK) {
        strncpy(buf, "--:--", buf_len);
        return ret;
    }
    strftime(buf, buf_len, "%Y-%m-%d %H:%M", &timeinfo);
    return ESP_OK;
}

esp_err_t ntp_sync_get_formatted_time(char *buf, size_t buf_len)
{
    struct tm timeinfo;
    esp_err_t ret = ntp_sync_get_local_time(&timeinfo);
    if (ret != ESP_OK) {
        strncpy(buf, "--:--:--", buf_len);
        return ret;
    }
    strftime(buf, buf_len, "%H:%M:%S", &timeinfo);
    return ESP_OK;
}

esp_err_t ntp_sync_get_formatted_date(char *buf, size_t buf_len)
{
    struct tm timeinfo;
    esp_err_t ret = ntp_sync_get_local_time(&timeinfo);
    if (ret != ESP_OK) {
        strncpy(buf, "----/--/--", buf_len);
        return ret;
    }
    strftime(buf, buf_len, "%Y-%m-%d", &timeinfo);
    return ESP_OK;
}

int ntp_sync_get_year(void)
{
    struct tm t;
    if (ntp_sync_get_local_time(&t) != ESP_OK) return 0;
    return t.tm_year + 1900;
}

int ntp_sync_get_month(void)
{
    struct tm t;
    if (ntp_sync_get_local_time(&t) != ESP_OK) return 0;
    return t.tm_mon + 1;
}

int ntp_sync_get_day(void)
{
    struct tm t;
    if (ntp_sync_get_local_time(&t) != ESP_OK) return 0;
    return t.tm_mday;
}

int ntp_sync_get_hour(void)
{
    struct tm t;
    if (ntp_sync_get_local_time(&t) != ESP_OK) return 0;
    return t.tm_hour;
}

int ntp_sync_get_minute(void)
{
    struct tm t;
    if (ntp_sync_get_local_time(&t) != ESP_OK) return 0;
    return t.tm_min;
}
