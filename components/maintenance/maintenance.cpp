// ============================================================================
// Maintenance : etat des mises a jour, session d'envoi, configuration.
//
// Equivalent de la structure UpdateStatus de la LiXee-Box (statusManuel /
// statusAuto / progress* / rebootRequested), avec un etat explicite
// (idle/running/done/error) en plus du message, pour que la page n'ait pas a
// deviner la fin d'une operation a partir d'un texte.
//
// Session : une seule operation a la fois (envoi direct, envoi par morceaux
// du tunnel ou telechargement automatique). La LiXee-Box n'avait aucun
// verrou : deux envois simultanes ecrivaient dans le meme /rt/upload.tar.
// Une session par morceaux abandonnee (navigateur ferme) bloquerait toute
// nouvelle operation : elle est consideree perimee apres 2 minutes sans
// donnees, et POST /api/maint/abort l'annule explicitement.
// ============================================================================

#include "maint_internal.h"
#include "config_manager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "MAINT";

#define SESSION_STALE_US   (120LL * 1000000)
#define URL_MAX            256

void *maint_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

static void set_err(char *err, size_t len, const char *msg)
{
    if (err && len) strlcpy(err, msg, len);
}

// ----------------------------------------------------------------------------
// Etat des canaux (lu par la page toutes les 1-2 s)
// ----------------------------------------------------------------------------

typedef struct {
    maint_state_t state;
    int  progress;          // -1 : aucune operation (convention LiXee-Box)
    char msg[160];
} channel_t;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static channel_t s_chan[2] = { { MAINT_ST_IDLE, -1, "" }, { MAINT_ST_IDLE, -1, "" } };
static int  s_busy_kind = -1;          // maint_kind_t de la session active, -1 sinon
static int  s_busy_ch = -1;
static size_t s_busy_received, s_busy_total;
static bool s_reboot;
static esp_timer_handle_t s_reboot_timer;

static const char *const STATE_NAMES[] = { "idle", "running", "done", "error" };

void maint_status_set(maint_channel_t ch, maint_state_t st, int progress, const char *msg)
{
    portENTER_CRITICAL(&s_mux);
    s_chan[ch].state = st;
    s_chan[ch].progress = progress;
    strlcpy(s_chan[ch].msg, msg ? msg : "", sizeof(s_chan[ch].msg));
    portEXIT_CRITICAL(&s_mux);
}

void maint_status_progress(maint_channel_t ch, int progress)
{
    portENTER_CRITICAL(&s_mux);
    s_chan[ch].progress = progress;
    portEXIT_CRITICAL(&s_mux);
}

void maintenance_reset_status(void)
{
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < 2; i++) {
        if (s_chan[i].state != MAINT_ST_RUNNING) {   // une operation en cours garde son etat
            s_chan[i].state = MAINT_ST_IDLE;
            s_chan[i].progress = -1;
            s_chan[i].msg[0] = '\0';
        }
    }
    portEXIT_CRITICAL(&s_mux);
}

static void reboot_cb(void *arg)
{
    (void)arg;
    ESP_LOGW(TAG, "Redemarrage apres maintenance");
    esp_restart();
}

void maint_schedule_reboot(uint32_t ms)
{
    if (s_reboot) return;
    s_reboot = true;
    esp_timer_create_args_t a = {};
    a.callback = reboot_cb;
    a.name = "maint_reboot";
    if (esp_timer_create(&a, &s_reboot_timer) != ESP_OK ||
        esp_timer_start_once(s_reboot_timer, (uint64_t)ms * 1000) != ESP_OK) {
        esp_restart();   // sans minuterie : redemarrer tout de suite
    }
}

bool maint_reboot_pending(void)
{
    return s_reboot;
}

esp_err_t maint_mark_pending(void)
{
    mkdir(MAINT_FS_ROOT "/rt", 0775);
    FILE *f = fopen(MAINT_PENDING_MARK, "w");
    if (!f) return ESP_FAIL;
    bool ok = fputs("1\n", f) >= 0;
    if (fclose(f) != 0) ok = false;
    return ok ? ESP_OK : ESP_FAIL;
}

// ----------------------------------------------------------------------------
// Source des mises a jour : fixe, comme la LiXee-Box (UPD_FILE de web.cpp,
// depot renomme LiXee-Gateway -> LiXee-Box : URL directe, sans redirection)
//
// L'URL n'est plus un reglage (ancien config/maintenance.json, en mode
// developpeur) : qui la change choisit le firmware installe. Archive de la
// derniere release GitHub, meme format que l'update.tar de la Box (le pont
// refuse une image d'un autre projet, cf. maint_ota.cpp : un firmware Arduino
// publie sur la meme release n'est pas installe). Informations de version :
// API GitHub de la meme release, lue par la page.
// ----------------------------------------------------------------------------

#define MAINT_UPDATE_URL  "https://github.com/fairecasoimeme/LiXee-Box/releases/latest/download/update.tar"
#define MAINT_INFO_URL    "https://api.github.com/repos/fairecasoimeme/LiXee-Box/releases/latest"

static const char s_update_url[] = MAINT_UPDATE_URL;
static const char s_info_url[]   = MAINT_INFO_URL;

static void load_config(void) {}

void maint_get_update_url(char *out, size_t len)
{
    strlcpy(out, s_update_url, len);
}

char *maint_auto_config_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "updateUrl", s_update_url);
    cJSON_AddStringToObject(root, "releaseInfoUrl", s_info_url);
    cJSON_AddBoolToObject(root, "enabled", true);
    cJSON_AddBoolToObject(root, "editable", false);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

esp_err_t maint_auto_set_config(const char *update_url, const char *info_url, char *err, size_t err_len)
{
    (void)update_url;
    (void)info_url;
    set_err(err, err_len, "Source des mises a jour fixe : non modifiable");
    return ESP_ERR_NOT_SUPPORTED;
}

// ----------------------------------------------------------------------------
// Session
// ----------------------------------------------------------------------------

typedef struct {
    uint32_t        id;       // generation (voir MAINT_SESSION_CURRENT)
    maint_kind_t    kind;
    maint_channel_t ch;
    size_t   total;
    size_t   received;
    uint32_t next_chunk;
    int64_t  last_us;
    maint_ota_t   ota;
    maint_untar_t tar;
} session_t;

static session_t *s_sess;   // protege par session_lock()
static uint32_t   s_sess_gen; // derniere generation attribuee, protege par session_lock()

/** La session active est-elle celle designee par 'id' ? Verrou tenu. */
static bool sess_match(uint32_t id)
{
    return s_sess && (id == MAINT_SESSION_CURRENT || s_sess->id == id);
}

static SemaphoreHandle_t session_lock(void)
{
    // Initialisation d'un statique local : protegee par le runtime C++
    static SemaphoreHandle_t lock = xSemaphoreCreateMutex();
    return lock;
}

static void set_busy(int kind, int ch)
{
    portENTER_CRITICAL(&s_mux);
    s_busy_kind = kind;
    s_busy_ch = ch;
    s_busy_received = 0;
    s_busy_total = s_sess ? s_sess->total : 0;
    portEXIT_CRITICAL(&s_mux);
}

/** Annule la session (verrou tenu). */
static void abort_locked(const char *reason)
{
    if (!s_sess) return;
    if (s_sess->kind == MAINT_KIND_RESTORE) {
        maint_untar_abort(&s_sess->tar);
        unsigned n = maint_staged_apply(false);
        unlink(MAINT_PENDING_MARK);
        if (n) ESP_LOGI(TAG, "%u fichiers provisoires effaces", n);
    }
    maint_ota_abort(&s_sess->ota);
    ESP_LOGW(TAG, "Session annulee : %s", reason ? reason : "-");
    maint_status_set(s_sess->ch, MAINT_ST_ERROR, -1, reason ? reason : "Annule");
    free(s_sess);
    s_sess = NULL;
    set_busy(-1, -1);
}

esp_err_t maint_session_begin(maint_kind_t kind, maint_channel_t ch, size_t total,
                              uint32_t *id_out, char *err, size_t err_len)
{
    if (id_out) *id_out = 0;
    if (xSemaphoreTake(session_lock(), pdMS_TO_TICKS(5000)) != pdTRUE) {
        set_err(err, err_len, "Une operation de maintenance est deja en cours");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = ESP_OK;
    if (s_reboot) {
        set_err(err, err_len, "Redemarrage en cours");
        ret = ESP_ERR_INVALID_STATE;
    } else if (s_sess) {
        if (esp_timer_get_time() - s_sess->last_us > SESSION_STALE_US) {
            abort_locked("Operation precedente abandonnee (plus de donnees)");
        } else {
            set_err(err, err_len, "Une operation de maintenance est deja en cours");
            ret = ESP_ERR_INVALID_STATE;
        }
    }
    if (ret == ESP_OK) {
        s_sess = (session_t *)maint_alloc(sizeof(session_t));
        if (!s_sess) {
            set_err(err, err_len, "Memoire insuffisante");
            ret = ESP_ERR_NO_MEM;
        }
    }
    if (ret == ESP_OK) {
        memset(s_sess, 0, sizeof(*s_sess));
        if (++s_sess_gen == MAINT_SESSION_CURRENT) ++s_sess_gen;   // 0 reserve
        s_sess->id = s_sess_gen;
        s_sess->kind = kind;
        s_sess->ch = ch;
        s_sess->total = total;
        s_sess->last_us = esp_timer_get_time();
        if (kind == MAINT_KIND_RESTORE) {
            // Restes d'une restauration non appliquee : remplaces par celle-ci
            maint_staged_apply(false);
            unlink(MAINT_PENDING_MARK);
            maint_untar_init(&s_sess->tar, &s_sess->ota);
        } else {
            ret = maint_ota_open(&s_sess->ota, total, err, err_len);
            if (ret != ESP_OK) {
                maint_status_set(ch, MAINT_ST_ERROR, -1, err);
                free(s_sess);
                s_sess = NULL;
            }
        }
    }
    if (ret == ESP_OK) {
        if (id_out) *id_out = s_sess->id;
        set_busy(kind, ch);
        maint_status_set(ch, MAINT_ST_RUNNING, 0,
                         kind == MAINT_KIND_RESTORE ? "R\xC3\xA9" "ception de l'archive..." : "R\xC3\xA9" "ception du firmware...");
        ESP_LOGI(TAG, "Session %s (%s), %u octets annonces", kind == MAINT_KIND_RESTORE ? "restauration" : "firmware",
                 ch == MAINT_CH_AUTO ? "auto" : "manuel", (unsigned)total);
    }
    xSemaphoreGive(session_lock());
    return ret;
}

void maint_session_set_total(uint32_t id, size_t total)
{
    xSemaphoreTake(session_lock(), portMAX_DELAY);
    if (sess_match(id)) {
        s_sess->total = total;
        portENTER_CRITICAL(&s_mux);
        s_busy_total = total;
        portEXIT_CRITICAL(&s_mux);
    }
    xSemaphoreGive(session_lock());
}

esp_err_t maint_session_feed(uint32_t id, const uint8_t *data, size_t len, char *err, size_t err_len)
{
    xSemaphoreTake(session_lock(), portMAX_DELAY);
    if (!sess_match(id)) {
        xSemaphoreGive(session_lock());
        set_err(err, err_len, s_sess ? "Operation remplacee par une autre" : "Aucune operation en cours");
        return ESP_ERR_INVALID_STATE;
    }
    char local[160] = "";
    esp_err_t e = (s_sess->kind == MAINT_KIND_RESTORE)
                  ? maint_untar_feed(&s_sess->tar, data, len, local, sizeof(local))
                  : maint_ota_write(&s_sess->ota, data, len, local, sizeof(local));
    if (e != ESP_OK) {
        if (!local[0]) strlcpy(local, "Erreur de traitement", sizeof(local));
        set_err(err, err_len, local);
        abort_locked(local);
    } else {
        s_sess->received += len;
        s_sess->last_us = esp_timer_get_time();
        int pct = s_sess->total ? (int)((uint64_t)s_sess->received * 90 / s_sess->total) : 0;
        if (pct > 90) pct = 90;
        maint_status_progress(s_sess->ch, pct);
        portENTER_CRITICAL(&s_mux);
        s_busy_received = s_sess->received;
        portEXIT_CRITICAL(&s_mux);
    }
    xSemaphoreGive(session_lock());
    return e;
}

esp_err_t maint_session_finish(uint32_t id, char *err, size_t err_len)
{
    xSemaphoreTake(session_lock(), portMAX_DELAY);
    if (!sess_match(id)) {
        xSemaphoreGive(session_lock());
        set_err(err, err_len, s_sess ? "Operation remplacee par une autre" : "Aucune operation en cours");
        return ESP_ERR_INVALID_STATE;
    }
    session_t *s = s_sess;
    maint_status_set(s->ch, MAINT_ST_RUNNING, 95, "V\xC3\xA9rification...");
    char local[160] = "";
    esp_err_t e = ESP_OK;
    bool fw = s->kind == MAINT_KIND_FIRMWARE || s->tar.fw_seen;

    if (s->kind == MAINT_KIND_RESTORE) e = maint_untar_end(&s->tar, local, sizeof(local));
    if (e == ESP_OK && fw) e = maint_ota_close(&s->ota, local, sizeof(local));
    if (e == ESP_OK && s->kind == MAINT_KIND_RESTORE && s->tar.files) {
        e = maint_mark_pending();
        if (e != ESP_OK) strlcpy(local, "Ecriture du marqueur de restauration impossible", sizeof(local));
    }
    // En dernier : apres ce point, le redemarrage execute la nouvelle image
    if (e == ESP_OK && fw) e = maint_ota_activate(&s->ota, local, sizeof(local));

    if (e != ESP_OK) {
        if (!local[0]) strlcpy(local, "Erreur de finalisation", sizeof(local));
        set_err(err, err_len, local);
        abort_locked(local);
        xSemaphoreGive(session_lock());
        return e;
    }

    char msg[160];
    if (s->kind == MAINT_KIND_RESTORE) {
        snprintf(msg, sizeof(msg), "%u fichier(s) restaur\xC3\xA9(s), %u ignor\xC3\xA9(s)%s%.31s. Red\xC3\xA9marrage...",
                 s->tar.files, s->tar.skipped, fw ? ", firmware " : "", fw ? s->ota.version : "");
    } else {
        snprintf(msg, sizeof(msg), "Firmware %.31s install\xC3\xA9. Red\xC3\xA9marrage...", s->ota.version);
    }
    if (s->kind == MAINT_KIND_RESTORE && s->tar.skipped) {
        ESP_LOGW(TAG, "%u entree(s) ignoree(s), dont '%s'", s->tar.skipped, s->tar.first_skipped);
    }
    ESP_LOGI(TAG, "%s", msg);
    maint_status_set(s->ch, MAINT_ST_DONE, 100, msg);
    free(s_sess);
    s_sess = NULL;
    set_busy(-1, -1);
    maint_schedule_reboot(2000);
    xSemaphoreGive(session_lock());
    set_err(err, err_len, msg);
    return ESP_OK;
}

void maint_session_abort(uint32_t id, const char *reason)
{
    xSemaphoreTake(session_lock(), portMAX_DELAY);
    if (sess_match(id)) abort_locked(reason);   // jamais la session d'un autre
    xSemaphoreGive(session_lock());
}

esp_err_t maint_idle_lock(char *err, size_t err_len)
{
    if (xSemaphoreTake(session_lock(), pdMS_TO_TICKS(5000)) != pdTRUE) {
        set_err(err, err_len, "Une operation de maintenance est deja en cours");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_reboot) {
        xSemaphoreGive(session_lock());
        set_err(err, err_len, "Redemarrage en cours");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_sess) {
        if (esp_timer_get_time() - s_sess->last_us > SESSION_STALE_US) {
            abort_locked("Operation precedente abandonnee (plus de donnees)");
        } else {
            xSemaphoreGive(session_lock());
            set_err(err, err_len, "Une operation de maintenance est deja en cours");
            return ESP_ERR_INVALID_STATE;
        }
    }
    return ESP_OK;
}

void maint_idle_unlock(void)
{
    xSemaphoreGive(session_lock());
}

bool maint_session_expected_chunk(maint_kind_t kind, uint32_t *next, uint32_t *id_out)
{
    xSemaphoreTake(session_lock(), portMAX_DELAY);
    bool ok = s_sess && s_sess->kind == kind && s_sess->ch == MAINT_CH_MANUAL;
    if (ok && next) *next = s_sess->next_chunk;
    if (id_out) *id_out = ok ? s_sess->id : 0;
    xSemaphoreGive(session_lock());
    return ok;
}

void maint_session_chunk_done(void)
{
    xSemaphoreTake(session_lock(), portMAX_DELAY);
    if (s_sess) s_sess->next_chunk++;
    xSemaphoreGive(session_lock());
}

// ----------------------------------------------------------------------------
// Etat JSON
// ----------------------------------------------------------------------------

static void add_channel(cJSON *root, const char *key, const channel_t *c)
{
    cJSON *o = cJSON_AddObjectToObject(root, key);
    cJSON_AddStringToObject(o, "state", STATE_NAMES[c->state]);
    cJSON_AddStringToObject(o, "status", c->msg);     // nom LiXee-Box
    cJSON_AddNumberToObject(o, "progress", c->progress);
}

char *maintenance_status_json(void)
{
    channel_t ch[2];
    int kind, bch;
    size_t rec, tot;
    char upd[URL_MAX], info[URL_MAX];
    portENTER_CRITICAL(&s_mux);
    memcpy(ch, s_chan, sizeof(ch));
    kind = s_busy_kind;
    bch = s_busy_ch;
    rec = s_busy_received;
    tot = s_busy_total;
    strlcpy(upd, s_update_url, sizeof(upd));
    strlcpy(info, s_info_url, sizeof(info));
    portEXIT_CRITICAL(&s_mux);

    cJSON *root = cJSON_CreateObject();
    add_channel(root, "manual", &ch[0]);
    add_channel(root, "auto", &ch[1]);
    cJSON_AddBoolToObject(root, "reboot", s_reboot);
    // Restauration preparee (marqueur pose) en attente du prochain demarrage :
    // signalee a la page, qui previent l'utilisateur
    struct stat pst;
    cJSON_AddBoolToObject(root, "restorePending", stat(MAINT_PENDING_MARK, &pst) == 0);
    cJSON_AddStringToObject(root, "busy", kind < 0 ? "none" : kind == MAINT_KIND_RESTORE ? "restore" : "firmware");
    if (kind >= 0) {
        cJSON_AddStringToObject(root, "busyChannel", bch == MAINT_CH_AUTO ? "auto" : "manual");
        cJSON_AddNumberToObject(root, "received", (double)rec);
        cJSON_AddNumberToObject(root, "total", (double)tot);
    }
    cJSON_AddBoolToObject(root, "autoEnabled", upd[0] != '\0');
    cJSON_AddStringToObject(root, "updateUrl", upd);
    cJSON_AddStringToObject(root, "releaseInfoUrl", info);
    cJSON_AddBoolToObject(root, "developerMode", cfg_get_general()->developer_mode);
    cJSON_AddNumberToObject(root, "chunkSize", 8192);   // morceau brut conseille (tunnel)

    const esp_app_desc_t *d = esp_app_get_description();
    cJSON *app = cJSON_AddObjectToObject(root, "app");
    cJSON_AddStringToObject(app, "project", d->project_name);
    cJSON_AddStringToObject(app, "version", d->version);
    cJSON_AddStringToObject(app, "date", d->date);
    cJSON_AddStringToObject(app, "time", d->time);
    // idf_ver vient de "git describe" d'ESP-IDF ("-128-NOTFOUND" hors depot
    // git) : version tiree des macros, toujours exacte
    char idf[24];
    snprintf(idf, sizeof(idf), "v%d.%d.%d", ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
    cJSON_AddStringToObject(app, "idf", idf);
    // Empreinte de l'image : change a chaque compilation, contrairement a la
    // date de esp_app_desc (recompile seulement quand ce fichier change)
    char build[12] = "";
    esp_app_get_elf_sha256(build, sizeof(build));
    cJSON_AddStringToObject(app, "build", build);
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (run) cJSON_AddStringToObject(app, "running", run->label);
    if (boot) cJSON_AddStringToObject(app, "boot", boot->label);
    if (next) {
        cJSON_AddStringToObject(app, "next", next->label);
        cJSON_AddNumberToObject(app, "maxImageSize", (double)next->size);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

// ----------------------------------------------------------------------------
// Initialisation
// ----------------------------------------------------------------------------

const char *maintenance_fs_label(void)
{
    static const char *s_label = NULL;
    if (!s_label) {
        s_label = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "littlefs")
                  ? "littlefs" : "spiffs";
    }
    return s_label;
}

void maintenance_early_init(void)
{
    session_lock();   // creation du mutex avant toute tache concurrente
    struct stat st;
    if (stat(MAINT_PENDING_MARK, &st) == 0) {
        unsigned n = maint_staged_apply(true);
        unlink(MAINT_PENDING_MARK);
        ESP_LOGW(TAG, "Restauration appliquee : %u fichier(s) remplace(s)", n);
    } else {
        unsigned n = maint_staged_apply(false);
        if (n) ESP_LOGW(TAG, "Restauration interrompue : %u fichier(s) provisoire(s) efface(s)", n);
    }
    unlink(MAINT_BK_DIR "/" MAINT_BACKUP_NAME "~w");   // sauvegarde interrompue
}

esp_err_t maintenance_init(void)
{
    load_config();
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (run && esp_ota_get_state_partition(run, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        // Seulement avec CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE : l'image a
        // demarre jusqu'au serveur web, on la confirme
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "Nouvelle image confirmee (anti-retour arriere)");
    }
    ESP_LOGI(TAG, "Firmware %s sur %s, mise a jour automatique %s",
             esp_app_get_description()->version, run ? run->label : "?",
             s_update_url[0] ? "configuree" : "desactivee");
    return ESP_OK;
}
