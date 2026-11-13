// ============================================================================
// NTP Sync - Synchronisation de l'heure via SNTP natif ESP-IDF
//
// Utilise esp_sntp (lwIP) pour la synchronisation.
// Configure le fuseau horaire via POSIX TZ string.
// ============================================================================

#include "ntp_sync.h"

#include <string.h>
#include <stdlib.h>
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
static ntp_time_source_t s_source = NTP_SRC_NONE;
static time_t s_last_ntp = 0;
static time_t s_last_rejected = 0;

/**
 * Plancher des heures acceptees : la date de compilation du firmware, moins
 * un jour. Un serveur NTP qui vient de redemarrer (box Internet, point
 * d'acces OpenWrt) peut servir une heure non encore synchronisee, souvent
 * celle de sa derniere mise a jour : elle etait prise telle quelle et
 * l'historique d'energie se remplissait a une date fausse (issue #65).
 */
static time_t build_floor(void)
{
    static time_t floor_s;
    if (floor_s) return floor_s;
    static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = __DATE__;                       // "Nov 13 2026"
    int mon = 0;
    for (int i = 0; i < 12; i++) if (strncmp(d, MONTHS + 3 * i, 3) == 0) mon = i + 1;
    int day = atoi(d + 4), year = atoi(d + 7);
    if (!mon || year < 2020) return floor_s = 1577836800;   // 2020-01-01
    // Jours depuis 1970 (calendrier civil, algorithme de H. Hinnant)
    int y = year - (mon <= 2);
    int era = y / 400;
    int yoe = y - era * 400;
    int doy = (153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + doe - 719468;
    floor_s = (time_t)(days - 1) * 86400;
    return floor_s;
}

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
    s_source = NTP_SRC_NTP;
    s_last_ntp = tv->tv_sec;

    time_t now = tv->tv_sec;
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "Time synchronized: %s (epoch=%ld)", buf, (long)now);
}

/**
 * Reponse d'un serveur NTP (remplace la fonction faible de lwIP) : une heure
 * anterieure au firmware est refusee et redemandee chaque minute, jusqu'a ce
 * que le serveur soit lui-meme a l'heure ; sinon heure appliquee et
 * synchronisation suivante dans une heure.
 */
extern "C" void sntp_sync_time(struct timeval *tv)
{
    if (tv->tv_sec < build_floor()) {
        if (s_last_rejected == 0 || tv->tv_sec != s_last_rejected) {
            struct tm t;
            gmtime_r(&tv->tv_sec, &t);
            ESP_LOGW(TAG, "Heure NTP refusee : %04d-%02d-%02d %02d:%02d UTC, anterieure au firmware "
                     "(serveur pas encore a l'heure ?). Nouvel essai dans 1 min",
                     t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
        }
        s_last_rejected = tv->tv_sec;
        sntp_set_sync_interval(60000);
        return;
    }
    settimeofday(tv, NULL);
    sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
    sntp_set_sync_interval(CONFIG_LWIP_SNTP_UPDATE_DELAY);
    ntp_sync_notification_cb(tv);
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
    // Notification : appelee par sntp_sync_time() ci-dessus

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

        // Heure memorisee (au plus une heure de retard, plus la duree de la
        // coupure) : de quoi afficher une heure approximative, mais pas une
        // heure fiable (s_synced reste faux : pas d'historique d'energie, ni
        // de courbe de puissance, a une date fausse)
        if (gen->epoch_time > 1700000000) {  // Plausible (apres 2023)
            struct timeval tv = { .tv_sec = gen->epoch_time, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            s_source = NTP_SRC_SAVED;
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
    s_source = NTP_SRC_MANUAL;
    ESP_LOGI(TAG, "Heure reglee a la main : %ld", (long)epoch);
    return ESP_OK;
}

ntp_time_source_t ntp_sync_source(void) { return s_source; }
time_t ntp_sync_last_ntp(void)          { return s_last_ntp; }
time_t ntp_sync_last_rejected(void)     { return s_last_rejected; }

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
