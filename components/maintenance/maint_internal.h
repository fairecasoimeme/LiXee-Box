#pragma once

// ============================================================================
// Interfaces internes du composant maintenance (non exportees).
// ============================================================================

#include "maintenance.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include <stdio.h>

#define MAINT_FS_ROOT        "/littlefs"
#define MAINT_FS_LABEL       maintenance_fs_label()
#define MAINT_BK_DIR         MAINT_FS_ROOT "/bk"
#define MAINT_STAGE_SUFFIX   "~r"        // fichier restaure en attente du redemarrage
#define MAINT_PENDING_MARK   MAINT_FS_ROOT "/rt/restore.pending"
#define MAINT_IO_BUF         4096        // tampon de flux (PSRAM)
#define MAINT_REL_MAX        56          // "dir/fichier" : + "/littlefs/" + "~r" < 64 (LITTLEFS_OBJ_NAME_LEN)

/** Allocation en PSRAM, repli en RAM interne. */
void *maint_alloc(size_t n);

// ----------------------------------------------------------------------------
// Etat des canaux
// ----------------------------------------------------------------------------

typedef enum {
    MAINT_ST_IDLE = 0,
    MAINT_ST_RUNNING,
    MAINT_ST_DONE,
    MAINT_ST_ERROR,
} maint_state_t;

void maint_status_set(maint_channel_t ch, maint_state_t st, int progress, const char *msg);
void maint_status_progress(maint_channel_t ch, int progress);
/** Programme esp_restart() dans 'ms' millisecondes (une seule fois). */
void maint_schedule_reboot(uint32_t ms);
bool maint_reboot_pending(void);
/** Ecrit le marqueur : les fichiers "~r" seront appliques au prochain demarrage. */
esp_err_t maint_mark_pending(void);
/** Taille totale apprise apres l'ouverture (en-tetes HTTP du telechargement). */
void maint_session_set_total(uint32_t id, size_t total);   // sans effet si 'id' n'est plus active
/**
 * Verrou de session pris hors de toute session : refuse (ESP_ERR_INVALID_STATE)
 * si une session est active (une session abandonnee depuis 2 min est annulee)
 * ou un redemarrage programme. Aucune session ne peut s'ouvrir avant
 * maint_idle_unlock(). Sert a preparer une restauration de fichier seul.
 */
esp_err_t maint_idle_lock(char *err, size_t err_len);
void maint_idle_unlock(void);
/** URL de mise a jour configuree (copie). */
void maint_get_update_url(char *out, size_t len);

// ----------------------------------------------------------------------------
// Ecriture OTA avec controle de l'image (maint_ota.cpp)
// ----------------------------------------------------------------------------

// En-tete image (24) + premier segment (8) + esp_app_desc_t (256)
#define MAINT_OTA_HEAD_LEN (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))

typedef struct {
    const esp_partition_t *part;
    esp_ota_handle_t handle;
    bool    open;          // maint_ota_open() appele
    bool    begun;         // esp_ota_begin() fait (apres controle de l'en-tete)
    bool    ended;         // esp_ota_end() reussi
    size_t  expected;      // taille annoncee, 0 si inconnue
    size_t  written;
    uint8_t head[MAINT_OTA_HEAD_LEN];
    size_t  head_len;
    char    version[32];
} maint_ota_t;

esp_err_t maint_ota_open(maint_ota_t *o, size_t expected, char *err, size_t err_len);
esp_err_t maint_ota_write(maint_ota_t *o, const uint8_t *data, size_t len, char *err, size_t err_len);
/** esp_ota_end() (verification SHA-256 / signature) + relecture de la description. */
esp_err_t maint_ota_close(maint_ota_t *o, char *err, size_t err_len);
/** esp_ota_set_boot_partition(), uniquement apres maint_ota_close() reussi. */
esp_err_t maint_ota_activate(maint_ota_t *o, char *err, size_t err_len);
void maint_ota_abort(maint_ota_t *o);

// ----------------------------------------------------------------------------
// Lecture d'archive tar en flux (maint_restore.cpp)
// ----------------------------------------------------------------------------

typedef struct {
    int      state;            // voir maint_restore.cpp
    uint8_t  hdr[512];
    size_t   hdr_len;
    uint64_t remaining;        // octets de donnees restants de l'entree courante
    size_t   pad;              // octets de bourrage jusqu'au bloc suivant
    int      target;           // 0 = ignorer, 1 = fichier, 2 = OTA
    FILE    *f;
    char     path[224];        // chemin provisoire complet (large : -Wformat-truncation)
    maint_ota_t *ota;          // destination de firmware.bin
    bool     fw_seen;
    unsigned files;
    unsigned skipped;
    char     first_skipped[64];
} maint_untar_t;

void      maint_untar_init(maint_untar_t *t, maint_ota_t *ota);
esp_err_t maint_untar_feed(maint_untar_t *t, const uint8_t *data, size_t len, char *err, size_t err_len);
/** Verifie que l'archive est complete (pas d'entree tronquee). */
esp_err_t maint_untar_end(maint_untar_t *t, char *err, size_t err_len);
void      maint_untar_abort(maint_untar_t *t);

/** Chemin relatif d'archive acceptable pour une restauration. */
bool maint_restore_rel_valid(const char *rel);
/**
 * Parcourt les repertoires restaurables : commit=true renomme les fichiers
 * "~r" vers leur nom reel, commit=false les supprime.
 * @return nombre de fichiers traites
 */
unsigned maint_staged_apply(bool commit);
