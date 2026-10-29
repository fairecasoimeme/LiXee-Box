// ============================================================================
// Sauvegardes : archive tar de la configuration et des donnees.
//
// Portage de handleToolCreateBackup() (microtar) de la LiXee-Box :
//  - meme contenu : db/, config/, debug/, hst/, tp/ et bk/*.json. L'original
//    ne gardait de bk/ que les noms de 16 caracteres + ".json" (test sur
//    substring(19,24)) ; ici tous les .json de bk/ sont pris ;
//  - config/ contient les mots de passe (WiFi, MQTT, SMTP, WebPush, HTTP) :
//    ils restent dans l'archive, sinon une restauration laisserait un
//    appareil sans reseau. La page et l'API le signalent (containsSecrets) ;
//  - chaque fichier est copie par blocs de 4 Ko au lieu d'etre charge en
//    entier dans une String, et la taille ecrite est toujours celle annoncee
//    dans l'en-tete (fichier modifie pendant la copie : complete ou tronque) ;
//  - l'archive peut etre produite a la volee vers la reponse HTTP
//    (maint_backup_stream) sans occuper LittleFS, ou enregistree dans
//    bk/backup.tar apres controle de l'espace libre (ecriture provisoire +
//    renommage : une sauvegarde precedente n'est pas detruite par un echec).
// ============================================================================

#include "maint_internal.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "cJSON.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "MAINT_BK";

// Ordre de la LiXee-Box
static const char *const BACKUP_DIRS[] = { "db", "config", "debug", "hst", "tp", "bk" };

#define BK_TMP_SUFFIX "~w"

static void set_err(char *err, size_t len, const char *msg)
{
    if (err && len) strlcpy(err, msg, len);
}

static bool ends_with(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && strcasecmp(s + ls - lx, suffix) == 0;
}

/** Nom simple : [A-Za-z0-9._-], sans ".." ni separateur. */
static bool name_valid(const char *name)
{
    size_t n = strlen(name);
    if (n == 0 || n > 64 || strstr(name, "..")) return false;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool maint_backup_path(const char *name, char *path, size_t path_len)
{
    if (!name || !name_valid(name) || !(ends_with(name, ".tar") || ends_with(name, ".json"))) return false;
    int n = snprintf(path, path_len, MAINT_BK_DIR "/%s", name);
    return n > 0 && (size_t)n < path_len;
}

/** Fichier provisoire ou temporaire, jamais sauvegarde ni liste. */
static bool is_temp_name(const char *name)
{
    return ends_with(name, ".tmp") || ends_with(name, MAINT_STAGE_SUFFIX) || ends_with(name, BK_TMP_SUFFIX);
}

// ----------------------------------------------------------------------------
// Ecriture tar (format ustar, lisible par tar, 7-Zip et microtar)
// ----------------------------------------------------------------------------

typedef struct {
    maint_sink_fn sink;
    void   *ctx;
    uint8_t *buf;        // MAINT_IO_BUF octets de donnees
    uint8_t *hdr;        // 512 octets
    size_t  total;       // octets produits (ou estimes)
    unsigned files;
} tar_writer_t;

/** Champ octal de 'width' octets : width-1 chiffres puis NUL. */
static void put_octal(uint8_t *dst, size_t width, uint64_t v)
{
    dst[width - 1] = '\0';
    for (size_t i = width - 1; i-- > 0;) {
        dst[i] = (uint8_t)('0' + (v & 7));
        v >>= 3;
    }
}

static void make_header(uint8_t *h, const char *name, size_t size, time_t mtime)
{
    memset(h, 0, 512);
    memcpy(h, name, strlen(name));          // <= 99 octets, verifie par l'appelant
    put_octal(h + 100, 8, 0644);            // mode
    put_octal(h + 108, 8, 0);               // uid
    put_octal(h + 116, 8, 0);               // gid
    put_octal(h + 124, 12, size);
    put_octal(h + 136, 12, mtime > 0 ? (uint64_t)mtime : 0);
    h[156] = '0';                           // fichier ordinaire
    memcpy(h + 257, "ustar", 6);            // magic "ustar\0"
    memcpy(h + 263, "00", 2);               // version
    memset(h + 148, ' ', 8);
    uint32_t sum = 0;
    for (int i = 0; i < 512; i++) sum += h[i];
    put_octal(h + 148, 7, sum);             // 6 chiffres + NUL, puis l'espace deja present
}

static esp_err_t emit(tar_writer_t *w, const void *data, size_t len)
{
    w->total += len;
    return w->sink ? w->sink(w->ctx, data, len) : ESP_OK;
}

/** Ajoute un fichier ; sans puits (estimation), compte seulement sa taille. */
static esp_err_t add_file(tar_writer_t *w, const char *path, const char *arcname, size_t size, time_t mtime)
{
    size_t padded = (size + 511) & ~(size_t)511;
    w->files++;
    if (!w->sink) {
        w->total += 512 + padded;
        return ESP_OK;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGW(TAG, "%s illisible, ignore", path);
        w->files--;
        return ESP_OK;
    }
    make_header(w->hdr, arcname, size, mtime);
    esp_err_t e = emit(w, w->hdr, 512);
    size_t left = size;
    while (e == ESP_OK && left > 0) {
        size_t n = left < MAINT_IO_BUF ? left : MAINT_IO_BUF;
        size_t r = fread(w->buf, 1, n, f);
        if (r < n) memset(w->buf + r, 0, n - r);   // fichier raccourci pendant la copie
        e = emit(w, w->buf, n);
        left -= n;
    }
    fclose(f);
    if (e == ESP_OK && padded > size) {
        memset(w->buf, 0, padded - size);
        e = emit(w, w->buf, padded - size);
    }
    return e;
}

/** Ajoute un fichier construit en memoire. */
static esp_err_t add_mem(tar_writer_t *w, const char *arcname, const char *data, size_t size)
{
    size_t padded = (size + 511) & ~(size_t)511;
    w->files++;
    if (!w->sink) {
        w->total += 512 + padded;
        return ESP_OK;
    }
    make_header(w->hdr, arcname, size, time(NULL));
    esp_err_t e = emit(w, w->hdr, 512);
    if (e == ESP_OK) e = emit(w, data, size);
    if (e == ESP_OK && padded > size) {
        memset(w->buf, 0, padded - size);
        e = emit(w, w->buf, padded - size);
    }
    return e;
}

/**
 * config/configWifi.json de l'archive, avec les identifiants WiFi : le pont
 * les garde en NVS (le fichier n'a que l'adressage), la LiXee-Box dans ce
 * fichier. Une restauration les reporte en NVS (main, au demarrage).
 */
static esp_err_t add_wifi_config(tar_writer_t *w)
{
    char *txt = NULL;
    FILE *f = fopen(MAINT_FS_ROOT "/config/configWifi.json", "r");
    cJSON *doc = NULL;
    if (f) {
        char buf[512];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[n] = '\0';
        doc = cJSON_Parse(buf);
    }
    if (!doc) doc = cJSON_CreateObject();
    if (!doc) return ESP_ERR_NO_MEM;
    wifi_mgr_credentials_t c = {};
    if (wifi_mgr_get_credentials(&c) == ESP_OK && c.ssid[0]) {
        cJSON_DeleteItemFromObject(doc, "ssid");
        cJSON_DeleteItemFromObject(doc, "pass");
        cJSON_AddStringToObject(doc, "ssid", c.ssid);
        cJSON_AddStringToObject(doc, "pass", c.password);
    }
    memset(&c, 0, sizeof(c));
    if (cJSON_GetArraySize(doc) > 0) txt = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (!txt) return ESP_OK;                 // rien a sauvegarder
    esp_err_t e = add_mem(w, "config/configWifi.json", txt, strlen(txt));
    memset(txt, 0, strlen(txt));
    free(txt);
    return e;
}

static esp_err_t walk_backup(tar_writer_t *w)
{
    char dir[48], path[320], arc[300];   // d_name peut atteindre 255 octets
    for (size_t i = 0; i < sizeof(BACKUP_DIRS) / sizeof(BACKUP_DIRS[0]); i++) {
        bool only_json = strcmp(BACKUP_DIRS[i], "bk") == 0;
        snprintf(dir, sizeof(dir), MAINT_FS_ROOT "/%s", BACKUP_DIRS[i]);
        DIR *d = opendir(dir);
        if (!d) continue;
        struct dirent *e;
        esp_err_t err = ESP_OK;
        while (err == ESP_OK && (e = readdir(d)) != NULL) {
            if (e->d_type == DT_DIR || e->d_name[0] == '.' || is_temp_name(e->d_name)) continue;
            if (only_json && !ends_with(e->d_name, ".json")) continue;
            snprintf(arc, sizeof(arc), "%s/%s", BACKUP_DIRS[i], e->d_name);
            if (strcmp(arc, "config/configWifi.json") == 0) continue;   // add_wifi_config()
            if (strlen(arc) > 99) {
                ESP_LOGW(TAG, "Nom trop long pour tar, ignore : %s", arc);
                continue;
            }
            snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
            struct stat st;
            if (stat(path, &st) != 0) continue;
            err = add_file(w, path, arc, (size_t)st.st_size, st.st_mtime);
        }
        closedir(d);
        if (err == ESP_OK && strcmp(BACKUP_DIRS[i], "config") == 0) err = add_wifi_config(w);
        if (err != ESP_OK) return err;
    }
    // Fin d'archive : deux blocs nuls
    if (!w->sink) {
        w->total += 1024;
        return ESP_OK;
    }
    memset(w->buf, 0, 1024);
    return emit(w, w->buf, 1024);
}

static size_t estimate_size(unsigned *files)
{
    tar_writer_t w = {};
    walk_backup(&w);
    if (files) *files = w.files;
    return w.total;
}

static esp_err_t run_writer(maint_sink_fn sink, void *ctx, size_t *out_total)
{
    uint8_t *mem = (uint8_t *)maint_alloc(MAINT_IO_BUF + 512);
    if (!mem) return ESP_ERR_NO_MEM;
    tar_writer_t w = {};
    w.sink = sink;
    w.ctx = ctx;
    w.buf = mem;
    w.hdr = mem + MAINT_IO_BUF;
    esp_err_t e = walk_backup(&w);
    free(mem);
    if (out_total) *out_total = w.total;
    ESP_LOGI(TAG, "Archive : %u fichiers, %u octets (%s)", w.files, (unsigned)w.total, esp_err_to_name(e));
    return e;
}

esp_err_t maint_backup_stream(maint_sink_fn sink, void *ctx)
{
    if (!sink) return ESP_ERR_INVALID_ARG;
    return run_writer(sink, ctx, NULL);
}

static esp_err_t file_sink(void *ctx, const void *data, size_t len)
{
    return fwrite(data, 1, len, (FILE *)ctx) == len ? ESP_OK : ESP_FAIL;
}

esp_err_t maint_backup_create(size_t *out_size, char *err, size_t err_len)
{
    size_t total_fs = 0, used_fs = 0;
    size_t need = estimate_size(NULL);
    if (esp_littlefs_info(MAINT_FS_LABEL, &total_fs, &used_fs) == ESP_OK) {
        size_t free_fs = total_fs > used_fs ? total_fs - used_fs : 0;
        // Marge : metadonnees LittleFS et fichiers ecrits pendant la copie
        if (need + 32 * 1024 > free_fs) {
            if (err && err_len) snprintf(err, err_len, "Espace insuffisant : %u Ko requis, %u Ko libres",
                     (unsigned)(need / 1024), (unsigned)(free_fs / 1024));
            return ESP_ERR_NO_MEM;
        }
    }
    mkdir(MAINT_BK_DIR, 0775);
    const char *final_path = MAINT_BK_DIR "/" MAINT_BACKUP_NAME;
    const char *tmp_path = MAINT_BK_DIR "/" MAINT_BACKUP_NAME BK_TMP_SUFFIX;
    FILE *f = fopen(tmp_path, "w");
    if (!f) {
        set_err(err, err_len, "Creation du fichier impossible");
        return ESP_FAIL;
    }
    size_t total = 0;
    esp_err_t e = run_writer(file_sink, f, &total);
    bool ok = e == ESP_OK && fflush(f) == 0 && fsync(fileno(f)) == 0;
    if (fclose(f) != 0) ok = false;
    if (!ok || rename(tmp_path, final_path) != 0) {
        unlink(tmp_path);
        set_err(err, err_len, e == ESP_ERR_NO_MEM ? "Memoire insuffisante" : "Erreur d'ecriture de la sauvegarde");
        return e != ESP_OK ? e : ESP_FAIL;
    }
    if (out_size) *out_size = total;
    return ESP_OK;
}

// ----------------------------------------------------------------------------
// Liste, suppression, restauration d'un fichier d'appareil
// ----------------------------------------------------------------------------

char *maint_backup_list_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *tars = cJSON_AddArrayToObject(root, "backups");
    cJSON *jsons = cJSON_AddArrayToObject(root, "files");
    DIR *d = opendir(MAINT_BK_DIR);
    if (d) {
        struct dirent *e;
        char path[300];
        while ((e = readdir(d)) != NULL) {
            if (e->d_type == DT_DIR || is_temp_name(e->d_name) || !name_valid(e->d_name)) continue;
            bool is_tar = ends_with(e->d_name, ".tar");
            if (!is_tar && !ends_with(e->d_name, ".json")) continue;
            snprintf(path, sizeof(path), MAINT_BK_DIR "/%s", e->d_name);
            struct stat st;
            if (stat(path, &st) != 0) continue;
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "name", e->d_name);
            cJSON_AddNumberToObject(o, "size", (double)st.st_size);
            cJSON_AddNumberToObject(o, "mtime", (double)st.st_mtime);
            cJSON_AddItemToArray(is_tar ? tars : jsons, o);
        }
        closedir(d);
    }
    unsigned files = 0;
    cJSON_AddNumberToObject(root, "estimate", (double)estimate_size(&files));
    cJSON_AddNumberToObject(root, "estimateFiles", files);
    cJSON_AddStringToObject(root, "dirs", MAINT_BACKUP_DIRS);
    // Les fichiers de config/ contiennent les mots de passe en clair
    cJSON_AddBoolToObject(root, "containsSecrets", true);
    // Sans authentification HTTP, n'importe quel poste du reseau peut les telecharger
    const cfg_general_t *gen = cfg_get_general();
    cJSON_AddBoolToObject(root, "httpAuth", cfg_get_settings()->enable_secure_http &&
                                            gen->user_http[0] && gen->pass_http[0]);
    size_t total_fs = 0, used_fs = 0;
    if (esp_littlefs_info(MAINT_FS_LABEL, &total_fs, &used_fs) == ESP_OK) {
        cJSON_AddNumberToObject(root, "fsTotal", (double)total_fs);
        cJSON_AddNumberToObject(root, "fsUsed", (double)used_fs);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

esp_err_t maint_backup_delete(const char *name)
{
    char path[128];
    if (!maint_backup_path(name, path, sizeof(path))) return ESP_ERR_INVALID_ARG;
    if (unlink(path) != 0) return ESP_ERR_NOT_FOUND;
    ESP_LOGI(TAG, "Supprime : %s", path);
    return ESP_OK;
}

esp_err_t maint_backup_restore_json(const char *name, char *err, size_t err_len)
{
    char src[128];
    // <= 56 : le nom provisoire "<nom>~r.tmp" doit tenir dans LFS_NAME_MAX (63)
    if (!maint_backup_path(name, src, sizeof(src)) || !ends_with(name, ".json") || strlen(name) > 56) {
        set_err(err, err_len, "Nom de fichier invalide");
        return ESP_ERR_INVALID_ARG;
    }
    FILE *f = fopen(src, "r");
    if (!f) {
        set_err(err, err_len, "Sauvegarde introuvable");
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 256 * 1024) {
        fclose(f);
        set_err(err, err_len, "Fichier vide ou trop grand");
        return ESP_ERR_INVALID_SIZE;
    }
    char *buf = (char *)maint_alloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        set_err(err, err_len, "Memoire insuffisante");
        return ESP_ERR_NO_MEM;
    }
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';
    cJSON *doc = cJSON_Parse(buf);
    if (!doc) {
        free(buf);
        set_err(err, err_len, "Contenu JSON invalide");
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_Delete(doc);

    // Comme une restauration d'archive : fichier provisoire applique au
    // redemarrage, pour que device_data ne le reecrive pas avec son etat en
    // memoire. L'original copiait directement dans /db/ sans redemarrer.
    //
    // Correction : le marqueur "restauration en attente" vaut pour TOUS les
    // fichiers provisoires "~r". Pose sans precaution, il faisait appliquer
    // au demarrage suivant les fichiers d'une archive en cours de reception,
    // ou laisses par une archive jamais terminee : une restauration partielle
    // que personne n'avait validee. Comportement retenu (le plus simple) :
    //  - refus pendant une session de maintenance active (verrou de session
    //    tenu pendant toute la preparation : aucune session ne peut s'ouvrir) ;
    //  - sans marqueur, les fichiers provisoires presents sont des restes non
    //    valides : effaces avant de preparer celui-ci ; avec marqueur, ce sont
    //    des restaurations deja demandees (autre fichier d'appareil) : gardes ;
    //  - la restauration reste en attente jusqu'au prochain demarrage, quelle
    //    qu'en soit la cause ; /api/maint/status la signale ("restorePending")
    //    et la page previent l'utilisateur. Une restauration d'archive lancee
    //    entre-temps la remplace (maint_session_begin efface les provisoires).
    esp_err_t e = maint_idle_lock(err, err_len);
    if (e != ESP_OK) {
        free(buf);
        return e;
    }
    struct stat st;
    if (stat(MAINT_PENDING_MARK, &st) != 0) {
        unsigned stale = maint_staged_apply(false);
        if (stale) ESP_LOGW(TAG, "%u fichier(s) provisoire(s) non valide(s) efface(s)", stale);
    }
    char dst[160];
    snprintf(dst, sizeof(dst), MAINT_FS_ROOT "/db/%s" MAINT_STAGE_SUFFIX, name);
    mkdir(MAINT_FS_ROOT "/db", 0775);
    e = cfg_write_file(dst, buf);
    free(buf);
    if (e == ESP_OK) e = maint_mark_pending();
    if (e != ESP_OK) unlink(dst);
    maint_idle_unlock();
    if (e != ESP_OK) {
        set_err(err, err_len, "Ecriture dans /db impossible");
        return e;
    }
    ESP_LOGI(TAG, "%s prepare pour /db (applique au redemarrage)", name);
    return ESP_OK;
}
