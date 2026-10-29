// ============================================================================
// Mise a jour automatique (equivalent /downloadUpdate + launchUpdateTask de
// la LiXee-Box).
//
// L'original telechargeait update.tar depuis une URL GitHub figee dans le
// code (UPD_FILE) vers /bk/update.tar.gz, avec un tampon de 400 Ko en PSRAM,
// puis le depaquetait. Le MatterBridge n'a pas encore de canal de
// publication : l'URL est un reglage (config/maintenance.json), vide par
// defaut, et la page masque la fonction tant qu'elle est vide.
//
// Le fichier telecharge passe directement dans la session de maintenance,
// sans etape sur LittleFS : archive .tar (firmware.bin + fichiers web/...,
// meme format que l'update.tar de la LiXee-Box) ou image .bin seule si l'URL
// se termine par ".bin". HTTPS obligatoire, serveur verifie par le bundle de
// certificats d'ESP-IDF (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE) ; les redirections
// (GitHub "releases/latest/download" -> objects.githubusercontent.com) sont
// suivies. L'authenticite du firmware repose sur TLS et, si active, sur
// Secure Boot ; sans Secure Boot, quiconque controle l'URL configuree
// controle le firmware.
// ============================================================================

#include "maint_internal.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "MAINT_AUTO";

#define AUTO_TASK_STACK   8192     // poignee de main TLS (mbedtls) ; pile interne car la tache ecrit en flash
#define AUTO_MAX_REDIRECT 5
#define AUTO_MAX_EAGAIN   10

static bool url_is_bin(const char *url)
{
    size_t n = strcspn(url, "?#");
    return n >= 4 && strncasecmp(url + n - 4, ".bin", 4) == 0;
}

static bool is_redirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// Parametres de la tache : URL et identifiant de LA session ouverte pour ce
// telechargement. Correction : la tache alimentait / terminait / annulait
// "la session courante" ; si la sienne avait ete annulee (bouton Annuler,
// session jugee abandonnee) puis remplacee par un envoi manuel, elle
// alimentait ce dernier avec ses octets, le terminait (redemarrage sur une
// image melangee refusee au mieux) ou l'annulait. Tous ses appels portent
// desormais l'identifiant de sa session et sont refuses pour une autre.
typedef struct {
    uint32_t sess_id;
    char     url[256];
} auto_job_t;

/** Telecharge et alimente la session ; renvoie ESP_OK si tout est recu. */
static esp_err_t download(uint32_t sess_id, const char *url, uint8_t *buf, char *err, size_t err_len)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 15000;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 2048;          // URL signees de GitHub : plusieurs centaines d'octets
    cfg.user_agent = "LiXee-MatterBridge";
    cfg.disable_auto_redirect = true;   // redirections traitees ci-dessous (lecture en flux)
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        strlcpy(err, "Client HTTP indisponible (memoire ?)", err_len);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t e = ESP_OK;
    int status = 0;
    for (int redirects = 0;; redirects++) {
        e = esp_http_client_open(c, 0);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "Connexion impossible : %s", esp_err_to_name(e));
            strlcpy(err, "Connexion au serveur impossible (reseau, DNS ou certificat)", err_len);
            break;
        }
        if (esp_http_client_fetch_headers(c) < 0) {
            strlcpy(err, "Reponse du serveur illisible", err_len);
            e = ESP_FAIL;
            break;
        }
        status = esp_http_client_get_status_code(c);
        if (!is_redirect(status)) break;
        if (redirects >= AUTO_MAX_REDIRECT) {
            strlcpy(err, "Trop de redirections", err_len);
            e = ESP_FAIL;
            break;
        }
        esp_http_client_flush_response(c, NULL);
        e = esp_http_client_set_redirection(c);
        if (e != ESP_OK) {
            strlcpy(err, "Redirection invalide", err_len);
            break;
        }
    }
    if (e == ESP_OK && status != 200) {
        snprintf(err, err_len, "Le serveur a repondu HTTP %d", status);
        e = ESP_FAIL;
    }

    if (e == ESP_OK) {
        int64_t len = esp_http_client_get_content_length(c);
        ESP_LOGI(TAG, "Telechargement : %lld octets", (long long)len);
        if (len > 0) maint_session_set_total(sess_id, (size_t)len);
        int eagain = 0;
        while (true) {
            int r = esp_http_client_read(c, (char *)buf, MAINT_IO_BUF);
            if (r == -ESP_ERR_HTTP_EAGAIN && ++eagain <= AUTO_MAX_EAGAIN) continue;
            if (r < 0) {
                strlcpy(err, "Connexion interrompue pendant le telechargement", err_len);
                e = ESP_FAIL;
                break;
            }
            if (r == 0) {
                if (!esp_http_client_is_complete_data_received(c)) {
                    strlcpy(err, "Telechargement incomplet", err_len);
                    e = ESP_FAIL;
                }
                break;
            }
            eagain = 0;
            e = maint_session_feed(sess_id, buf, (size_t)r, err, err_len);
            if (e != ESP_OK) break;   // session deja annulee par feed
        }
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return e;
}

static void auto_task(void *arg)
{
    auto_job_t *job = (auto_job_t *)arg;
    char err[160] = "";
    uint8_t *buf = (uint8_t *)maint_alloc(MAINT_IO_BUF);
    esp_err_t e = buf ? download(job->sess_id, job->url, buf, err, sizeof(err)) : ESP_ERR_NO_MEM;
    if (!buf) strlcpy(err, "Memoire insuffisante", sizeof(err));
    if (e == ESP_OK) {
        e = maint_session_finish(job->sess_id, err, sizeof(err));   // annule la session si echec
    } else {
        // Sans effet si la session est deja annulee ou remplacee
        maint_session_abort(job->sess_id, err[0] ? err : "Echec du telechargement");
    }
    ESP_LOGI(TAG, "Mise a jour automatique : %s (%s)", e == ESP_OK ? "reussie" : "echec", err);
    free(buf);
    free(job);
    vTaskDelete(NULL);
}

esp_err_t maint_auto_start(char *err, size_t err_len)
{
    char dummy[8];
    if (!err || !err_len) { err = dummy; err_len = sizeof(dummy); }
    auto_job_t *job = (auto_job_t *)maint_alloc(sizeof(auto_job_t));
    if (!job) {
        strlcpy(err, "Memoire insuffisante", err_len);
        return ESP_ERR_NO_MEM;
    }
    maint_get_update_url(job->url, sizeof(job->url));
    if (!job->url[0]) {
        free(job);
        strlcpy(err, "Mise a jour automatique non configuree", err_len);
        return ESP_ERR_NOT_SUPPORTED;
    }
    maint_kind_t kind = url_is_bin(job->url) ? MAINT_KIND_FIRMWARE : MAINT_KIND_RESTORE;
    esp_err_t e = maint_session_begin(kind, MAINT_CH_AUTO, 0, &job->sess_id, err, err_len);
    if (e != ESP_OK) {
        free(job);
        return e;
    }
    maint_status_set(MAINT_CH_AUTO, MAINT_ST_RUNNING, 0, "T\xC3\xA9l\xC3\xA9" "chargement...");
    ESP_LOGI(TAG, "Telechargement de %s", job->url);   // avant la tache : elle libere job
    uint32_t id = job->sess_id;
    if (xTaskCreate(auto_task, "maint_auto", AUTO_TASK_STACK, job, 4, NULL) != pdPASS) {
        free(job);
        maint_session_abort(id, "Tache de telechargement impossible (memoire)");
        strlcpy(err, "Memoire insuffisante", err_len);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
