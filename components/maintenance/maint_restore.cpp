// ============================================================================
// Lecture d'une archive tar en flux (restauration, paquet de mise a jour).
//
// Portage de untarApplyAndRestore() de la LiXee-Box, avec ces differences :
//  - l'archive n'est jamais stockee : elle est analysee au fil de l'arrivee
//    des octets (bloc d'en-tete de 512 octets, donnees, bourrage). L'original
//    l'ecrivait d'abord dans /rt/upload.tar, ce qui doublait l'espace requis
//    et ne tenait pas dans les 1,9 Mo de LittleFS avec un firmware de 2-3 Mo ;
//  - les chemins de l'archive ne sont PAS utilises tels quels : seuls les
//    repertoires connus sont acceptes, chaque composante est limitee a
//    [A-Za-z0-9._-], ".." et les chemins absolus sont refuses. L'original
//    ecrivait "/" + nom de l'entree : une archive forgee pouvait ecrire
//    n'importe ou dans LittleFS. Une entree refusee est ignoree et comptee ;
//  - les fichiers sont ecrits sous un nom provisoire ("~r") et ne remplacent
//    les fichiers reels qu'au redemarrage (maintenance_early_init) : une
//    restauration interrompue ne laisse pas une configuration a moitie
//    ecrasee, et aucun module ne reecrit un fichier restaure avant le
//    redemarrage ;
//  - somme de controle de chaque en-tete verifiee, entree tronquee detectee ;
//  - en-tetes ustar (prefixe) acceptes, entrees speciales (liens, en-tetes
//    pax/GNU) ignorees.
// ============================================================================

#include "maint_internal.h"
#include "esp_log.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "MAINT_TAR";

enum { ST_HDR = 0, ST_DATA, ST_PAD, ST_END };
enum { TGT_SKIP = 0, TGT_FILE, TGT_OTA };

// Repertoires restaurables (premiere composante du chemin)
static const char *const RESTORE_DIRS[] = { "config", "db", "hst", "tp", "debug", "bk", "web" };

static void set_err(char *err, size_t len, const char *msg)
{
    if (err && len) strlcpy(err, msg, len);
}

static bool comp_valid(const char *s, size_t n)
{
    if (n == 0 || n > 48 || s[0] == '.') return false;   // ni ".", ni "..", ni fichier cache
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool maint_restore_rel_valid(const char *rel)
{
    size_t total = strlen(rel);
    if (total == 0 || total > MAINT_REL_MAX || rel[0] == '/' || strstr(rel, "..")) return false;

    // Decoupage en composantes : "dir/fichier", ou "web/sous-dir/fichier"
    const char *comps[4];
    size_t lens[4];
    int n = 0;
    const char *p = rel;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t l = slash ? (size_t)(slash - p) : strlen(p);
        if (n >= 3 || !comp_valid(p, l)) return false;
        comps[n] = p;
        lens[n] = l;
        n++;
        if (!slash) break;
        p = slash + 1;
        if (!*p) return false;   // "dir/" : pas un fichier
    }
    if (n < 2) return false;

    bool dir_ok = false;
    for (size_t i = 0; i < sizeof(RESTORE_DIRS) / sizeof(RESTORE_DIRS[0]); i++) {
        if (strlen(RESTORE_DIRS[i]) == lens[0] && strncmp(comps[0], RESTORE_DIRS[i], lens[0]) == 0) {
            dir_ok = true;
            break;
        }
    }
    if (!dir_ok) return false;
    if (n == 3 && !(lens[0] == 3 && strncmp(comps[0], "web", 3) == 0)) return false;   // sous-repertoires : web/ seul
    // Les suffixes provisoires ne peuvent pas venir de l'archive ('~' est
    // deja refuse par comp_valid) ; ".tmp" est celui de cfg_write_file()
    if (total > 4 && strcmp(rel + total - 4, ".tmp") == 0) return false;
    return true;
}

/** Cree les repertoires parents de rel (deja valide) sous /littlefs. */
static void make_parents(const char *rel)
{
    char dir[MAINT_REL_MAX + 16];
    const char *p = rel;
    const char *slash;
    while ((slash = strchr(p, '/')) != NULL) {
        snprintf(dir, sizeof(dir), MAINT_FS_ROOT "/%.*s", (int)(slash - rel), rel);
        if (mkdir(dir, 0775) != 0 && errno != EEXIST) {
            ESP_LOGW(TAG, "mkdir %s: %d", dir, errno);
        }
        p = slash + 1;
    }
}

// ----------------------------------------------------------------------------
// En-tete tar
// ----------------------------------------------------------------------------

static uint64_t parse_octal(const uint8_t *p, size_t n, bool *ok)
{
    uint64_t v = 0;
    size_t i = 0;
    *ok = true;
    if (n && (p[0] & 0x80)) { *ok = false; return 0; }   // codage base 256 (> 8 Go) : refuse
    while (i < n && (p[i] == ' ' || p[i] == '\0')) i++;
    bool digit = false;
    for (; i < n && p[i] >= '0' && p[i] <= '7'; i++) {
        v = (v << 3) | (uint64_t)(p[i] - '0');
        digit = true;
    }
    for (; i < n; i++) {
        if (p[i] != ' ' && p[i] != '\0') { *ok = false; break; }
    }
    if (!digit) v = 0;
    return v;
}

static bool checksum_ok(const uint8_t *h)
{
    bool ok;
    uint64_t want = parse_octal(h + 148, 8, &ok);
    if (!ok) return false;
    uint32_t us = 0;
    int32_t ss = 0;
    for (int i = 0; i < 512; i++) {
        uint8_t c = (i >= 148 && i < 156) ? ' ' : h[i];
        us += c;
        ss += (int8_t)c;   // certains anciens tar signaient la somme
    }
    return want == us || (int64_t)want == (int64_t)ss;
}

static bool block_is_zero(const uint8_t *h)
{
    for (int i = 0; i < 512; i++) {
        if (h[i]) return false;
    }
    return true;
}

/** Nom complet de l'entree (prefixe ustar + nom), "./" initial retire. */
static bool entry_name(const uint8_t *h, char *out, size_t out_len)
{
    char name[101], prefix[156];
    memcpy(name, h, 100);
    name[100] = '\0';
    prefix[0] = '\0';
    if (memcmp(h + 257, "ustar", 5) == 0) {
        memcpy(prefix, h + 345, 155);
        prefix[155] = '\0';
    }
    char full[260];
    if (prefix[0]) snprintf(full, sizeof(full), "%s/%s", prefix, name);
    else           snprintf(full, sizeof(full), "%s", name);
    const char *s = full;
    while (s[0] == '.' && s[1] == '/') s += 2;
    if (strlen(s) >= out_len) return false;
    strlcpy(out, s, out_len);
    return true;
}

static void skip_entry(maint_untar_t *t, const char *name, const char *why)
{
    t->target = TGT_SKIP;
    if (name[0] && why) {
        t->skipped++;
        if (!t->first_skipped[0]) strlcpy(t->first_skipped, name, sizeof(t->first_skipped));
        ESP_LOGW(TAG, "Entree ignoree '%s' : %s", name, why);
    }
}

static void close_file(maint_untar_t *t)
{
    if (t->f) {
        fclose(t->f);
        t->f = NULL;
    }
}

/** Fin des donnees de l'entree courante. */
static esp_err_t entry_done(maint_untar_t *t, char *err, size_t err_len)
{
    if (t->target == TGT_FILE && t->f) {
        bool ok = fflush(t->f) == 0 && fsync(fileno(t->f)) == 0;
        if (fclose(t->f) != 0) ok = false;   // LittleFS valide les blocs ici
        t->f = NULL;
        if (!ok) {
            unlink(t->path);
            set_err(err, err_len, "Erreur d'ecriture (espace LittleFS insuffisant ?)");
            return ESP_FAIL;
        }
        t->files++;
    }
    t->target = TGT_SKIP;
    t->state = t->pad ? ST_PAD : ST_HDR;
    t->hdr_len = 0;
    return ESP_OK;
}

static esp_err_t handle_header(maint_untar_t *t, char *err, size_t err_len)
{
    const uint8_t *h = t->hdr;
    if (block_is_zero(h)) {
        // Fin d'archive (deux blocs nuls ; le premier suffit)
        t->state = ST_END;
        return ESP_OK;
    }
    if (!checksum_ok(h)) {
        set_err(err, err_len, "Archive tar invalide (somme de controle d'en-tete)");
        return ESP_ERR_INVALID_CRC;
    }
    bool ok;
    uint64_t size = parse_octal(h + 124, 12, &ok);
    if (!ok) {
        set_err(err, err_len, "Archive tar invalide (taille d'entree)");
        return ESP_ERR_INVALID_SIZE;
    }
    char name[200];
    if (!entry_name(h, name, sizeof(name))) name[0] = '\0';
    char type = (char)h[156];

    t->remaining = size;
    t->pad = (size_t)((512 - (size % 512)) % 512);
    t->state = ST_DATA;

    if (type == '5') {
        skip_entry(t, name, NULL);   // repertoire : cree au besoin pour ses fichiers
    } else if (type != '0' && type != '\0' && type != '7') {
        skip_entry(t, name, "type d'entree non pris en charge");
    } else if (strcmp(name, "firmware.bin") == 0) {
        if (t->fw_seen || !t->ota) {
            set_err(err, err_len, "Archive avec plusieurs firmware.bin");
            return ESP_ERR_INVALID_STATE;
        }
        esp_err_t e = maint_ota_open(t->ota, (size_t)size, err, err_len);
        if (e != ESP_OK) return e;
        t->fw_seen = true;
        t->target = TGT_OTA;
        ESP_LOGI(TAG, "firmware.bin : %llu octets vers OTA", (unsigned long long)size);
    } else if (!name[0] || !maint_restore_rel_valid(name)) {
        skip_entry(t, name[0] ? name : "(nom illisible)", "chemin non autorise");
    } else {
        make_parents(name);
        snprintf(t->path, sizeof(t->path), MAINT_FS_ROOT "/%s" MAINT_STAGE_SUFFIX, name);
        t->f = fopen(t->path, "w");
        if (!t->f) {
            ESP_LOGE(TAG, "Ouverture %s impossible (errno %d)", t->path, errno);
            set_err(err, err_len, "Ecriture LittleFS impossible");
            return ESP_FAIL;
        }
        t->target = TGT_FILE;
        ESP_LOGI(TAG, "Restauration de %s (%llu octets)", name, (unsigned long long)size);
    }
    if (size == 0) return entry_done(t, err, err_len);
    return ESP_OK;
}

// ----------------------------------------------------------------------------
// API
// ----------------------------------------------------------------------------

void maint_untar_init(maint_untar_t *t, maint_ota_t *ota)
{
    memset(t, 0, sizeof(*t));
    t->state = ST_HDR;
    t->ota = ota;
}

esp_err_t maint_untar_feed(maint_untar_t *t, const uint8_t *data, size_t len, char *err, size_t err_len)
{
    while (len > 0) {
        switch (t->state) {
        case ST_HDR: {
            size_t n = 512 - t->hdr_len;
            if (n > len) n = len;
            memcpy(t->hdr + t->hdr_len, data, n);
            t->hdr_len += n;
            data += n;
            len -= n;
            if (t->hdr_len == 512) {
                esp_err_t e = handle_header(t, err, err_len);
                if (e != ESP_OK) return e;
            }
            break;
        }
        case ST_DATA: {
            size_t n = (t->remaining < len) ? (size_t)t->remaining : len;
            if (t->target == TGT_FILE) {
                if (fwrite(data, 1, n, t->f) != n) {
                    close_file(t);
                    unlink(t->path);
                    set_err(err, err_len, "Erreur d'ecriture (espace LittleFS insuffisant ?)");
                    return ESP_FAIL;
                }
            } else if (t->target == TGT_OTA) {
                esp_err_t e = maint_ota_write(t->ota, data, n, err, err_len);
                if (e != ESP_OK) return e;
            }
            t->remaining -= n;
            data += n;
            len -= n;
            if (t->remaining == 0) {
                esp_err_t e = entry_done(t, err, err_len);
                if (e != ESP_OK) return e;
            }
            break;
        }
        case ST_PAD: {
            size_t n = (t->pad < len) ? t->pad : len;
            t->pad -= n;
            data += n;
            len -= n;
            if (t->pad == 0) {
                t->state = ST_HDR;
                t->hdr_len = 0;
            }
            break;
        }
        default:   // ST_END : le reste (second bloc nul, bourrage) est ignore
            return ESP_OK;
        }
    }
    return ESP_OK;
}

esp_err_t maint_untar_end(maint_untar_t *t, char *err, size_t err_len)
{
    // Archive sans blocs de fin (certains outils) : acceptee si elle s'arrete
    // proprement entre deux entrees
    bool clean = t->state == ST_END || (t->state == ST_HDR && t->hdr_len == 0) ||
                 (t->state == ST_PAD);
    if (!clean) {
        set_err(err, err_len, "Archive tronquee");
        return ESP_ERR_INVALID_SIZE;
    }
    if (t->files == 0 && !t->fw_seen) {
        set_err(err, err_len, "Archive vide ou sans fichier restaurable");
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

void maint_untar_abort(maint_untar_t *t)
{
    if (t->f) {
        close_file(t);
        unlink(t->path);
    }
    t->state = ST_END;
}

// ----------------------------------------------------------------------------
// Fichiers provisoires
// ----------------------------------------------------------------------------

static unsigned walk_dir(const char *dir, int depth, bool commit)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    unsigned count = 0;
    char path[300];
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (e->d_type == DT_DIR) {
            if (depth > 0) count += walk_dir(path, depth - 1, commit);
            continue;
        }
        size_t l = strlen(path), ls = strlen(MAINT_STAGE_SUFFIX);
        if (l <= ls || strcmp(path + l - ls, MAINT_STAGE_SUFFIX) != 0) continue;
        if (commit) {
            char final_path[300];
            snprintf(final_path, sizeof(final_path), "%.*s", (int)(l - ls), path);
            // rename() remplace atomiquement la cible sur LittleFS
            if (rename(path, final_path) == 0) {
                count++;
            } else {
                ESP_LOGE(TAG, "Renommage %s impossible (errno %d)", path, errno);
                unlink(path);
            }
        } else {
            unlink(path);
            count++;
        }
    }
    closedir(d);
    return count;
}

unsigned maint_staged_apply(bool commit)
{
    unsigned n = 0;
    char dir[48];
    for (size_t i = 0; i < sizeof(RESTORE_DIRS) / sizeof(RESTORE_DIRS[0]); i++) {
        snprintf(dir, sizeof(dir), MAINT_FS_ROOT "/%s", RESTORE_DIRS[i]);
        n += walk_dir(dir, strcmp(RESTORE_DIRS[i], "web") == 0 ? 1 : 0, commit);
    }
    return n;
}
