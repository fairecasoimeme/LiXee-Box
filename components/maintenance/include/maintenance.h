#pragma once

// ============================================================================
// Maintenance : sauvegarde, restauration, mise a jour du firmware
// (lot 6 du portage LiXee-Box : /backup, /update, /doUpdate, /doRestore,
// variantes par morceaux, /downloadUpdate).
//
// Trois briques :
//  - sauvegarde : archive tar de db/, config/, debug/, hst/, tp/ et bk/*.json,
//    enregistree dans /littlefs/bk ou produite a la volee vers un "puits"
//    (reponse HTTP). ATTENTION : config/ contient les mots de passe (WiFi,
//    MQTT, SMTP, WebPush, HTTP). Rien n'est retire de l'archive : une
//    sauvegarde doit pouvoir tout restaurer. Elle se protege donc comme un
//    secret.
//  - session d'envoi : une archive tar (restauration ou paquet de mise a jour)
//    ou une image .bin, recue en flux (corps HTTP brut, morceaux base64 du
//    tunnel, telechargement HTTPS). Rien n'est jamais charge en entier en RAM.
//    L'entree "firmware.bin" d'une archive va dans la partition OTA ; les
//    autres entrees sont ecrites dans /littlefs apres validation du chemin,
//    sous un nom provisoire (suffixe "~r"). Elles ne remplacent les fichiers
//    reels qu'au redemarrage suivant (maintenance_early_init), avant que les
//    composants ne relisent leur configuration : aucun module ne peut ecraser
//    un fichier restaure avec son etat en memoire entre la fin de la
//    restauration et le redemarrage.
//  - mise a jour automatique : telechargement HTTPS (bundle de certificats)
//    depuis une URL configurable, vide par defaut (fonction masquee).
//
// Toutes les fonctions sont sures depuis plusieurs taches (mutex interne).
// ============================================================================

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Repertoires de /littlefs places dans une sauvegarde (bk : *.json seuls). */
#define MAINT_BACKUP_DIRS      "db, config, debug, hst, tp, bk/*.json"
/** Nom de la sauvegarde enregistree par maint_backup_create(). */
#define MAINT_BACKUP_NAME      "backup.tar"
/** Fichier de configuration du lot (URL de mise a jour). */
#define MAINT_CONFIG_PATH      "/littlefs/config/maintenance.json"
/** Taille d'un morceau base64 accepte (8 Ko bruts -> ~11 Ko, tunnel <= 15 Ko). */
#define MAINT_CHUNK_MAX_B64    16384

// ----------------------------------------------------------------------------
// Initialisation
// ----------------------------------------------------------------------------

/**
 * A appeler juste apres le montage de LittleFS, AVANT cfg_load_general() et
 * les autres chargements : applique une restauration en attente (renomme les
 * fichiers "~r" vers leur nom reel) ou nettoie les restes d'une restauration
 * interrompue.
 */
void maintenance_early_init(void);

/**
 * Partition du systeme de fichiers : "spiffs" (table de la LiXee-Box, v2.x et
 * v3 : LittleFS, donnees d'une Box migree reprises telles quelles) ou
 * "littlefs" (ancienne table du pont, cartes de test flashees avant
 * l'alignement sur la table v2.x).
 */
const char *maintenance_fs_label(void);

/**
 * A appeler une fois le reseau et le serveur web demarres : lit
 * config/maintenance.json et confirme l'image en cours si elle attend une
 * validation (anti-retour arriere, si CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE).
 */
esp_err_t maintenance_init(void);

// ----------------------------------------------------------------------------
// Etat (pages Mise a jour / Sauvegarde)
// ----------------------------------------------------------------------------

/**
 * Etat JSON (a liberer par free()) :
 * {"manual":{"state","status","progress"},"auto":{...},"reboot",
 *  "busy","app":{...},"auto_enabled",...}  -- voir docs dans web_maint.h.
 */
char *maintenance_status_json(void);

/** Remet a zero les messages des canaux (equivalent /resetUpdateStatus). */
void maintenance_reset_status(void);

// ----------------------------------------------------------------------------
// Sauvegardes
// ----------------------------------------------------------------------------

/** Puits d'ecriture : renvoie ESP_OK pour continuer. */
typedef esp_err_t (*maint_sink_fn)(void *ctx, const void *data, size_t len);

/** Liste JSON de /littlefs/bk (archives .tar et fichiers .json), a liberer. */
char *maint_backup_list_json(void);

/**
 * Cree /littlefs/bk/backup.tar (ecriture dans un fichier provisoire puis
 * renommage). Verifie avant l'espace libre de la partition.
 * @param out_size taille de l'archive (peut etre NULL)
 */
esp_err_t maint_backup_create(size_t *out_size, char *err, size_t err_len);

/** Produit la meme archive a la volee, sans rien ecrire en flash. */
esp_err_t maint_backup_stream(maint_sink_fn sink, void *ctx);

/**
 * Nom de fichier de /littlefs/bk valide ([A-Za-z0-9._-], sans ".."),
 * termine par .tar ou .json ; construit le chemin complet dans 'path'.
 */
bool maint_backup_path(const char *name, char *path, size_t path_len);

esp_err_t maint_backup_delete(const char *name);

/** Copie bk/<name>.json vers db/<name>.json (contenu JSON verifie). */
esp_err_t maint_backup_restore_json(const char *name, char *err, size_t err_len);

// ----------------------------------------------------------------------------
// Session d'envoi (restauration / mise a jour). Une seule a la fois.
// ----------------------------------------------------------------------------

typedef enum {
    MAINT_KIND_RESTORE = 0,   ///< archive tar (fichiers + firmware.bin eventuel)
    MAINT_KIND_FIRMWARE,      ///< image .bin seule
} maint_kind_t;

typedef enum {
    MAINT_CH_MANUAL = 0,      ///< envoi depuis la page (statusManuel)
    MAINT_CH_AUTO,            ///< telechargement automatique (statusAuto)
} maint_channel_t;

/**
 * Identifiant de session (generation), rendu par maint_session_begin() et
 * repasse a feed / finish / abort. Correction : ces fonctions agissaient sur
 * "la session courante", quelle qu'elle soit ; la tache de mise a jour
 * automatique pouvait ainsi alimenter, terminer (et faire redemarrer sur) ou
 * annuler une session ouverte entre-temps par un autre (envoi manuel apres
 * abandon de la sienne, par exemple). Avec un identifiant, un appel pour une
 * session qui n'existe plus est refuse (ESP_ERR_INVALID_STATE) ou sans effet.
 * MAINT_SESSION_CURRENT (0) designe la session active, quelle qu'elle soit
 * (bouton "Annuler" de la page).
 */
#define MAINT_SESSION_CURRENT 0u

/**
 * Ouvre une session. Refuse (ESP_ERR_INVALID_STATE) si une autre est active
 * et n'est pas abandonnee depuis plus de 2 minutes.
 * @param total taille annoncee (progression, controle de la taille d'une
 *              image) ; 0 si inconnue
 * @param id_out identifiant de la session ouverte (jamais 0), peut etre NULL
 */
esp_err_t maint_session_begin(maint_kind_t kind, maint_channel_t ch, size_t total,
                              uint32_t *id_out, char *err, size_t err_len);

/** Ajoute des octets au flux. En cas d'erreur la session est annulee. */
esp_err_t maint_session_feed(uint32_t id, const uint8_t *data, size_t len, char *err, size_t err_len);

/**
 * Termine : verifie l'image, prepare les fichiers, change la partition de
 * demarrage et programme le redemarrage (2 s). En cas d'erreur la session
 * est annulee et rien n'est applique.
 */
esp_err_t maint_session_finish(uint32_t id, char *err, size_t err_len);

/**
 * Annule la session 'id' (fichiers provisoires effaces, OTA abandonnee).
 * Sans effet si cette session n'est plus active.
 */
void maint_session_abort(uint32_t id, const char *reason);

/**
 * Numero de morceau attendu par la session par morceaux (tunnel).
 * @param id_out identifiant de cette session (peut etre NULL), a repasser a
 *               maint_session_feed / maint_session_finish
 * @return false si aucune session n'est active
 */
bool maint_session_expected_chunk(maint_kind_t kind, uint32_t *next, uint32_t *id_out);
/** Marque un morceau comme recu (apres maint_session_feed reussi). */
void maint_session_chunk_done(void);

// ----------------------------------------------------------------------------
// Mise a jour automatique
// ----------------------------------------------------------------------------

/** {"updateUrl":"","releaseInfoUrl":"","enabled":false}, a liberer. */
char *maint_auto_config_json(void);

/**
 * Enregistre les URL (https:// obligatoire, ou vide pour desactiver).
 * update_url : archive .tar (comme update.tar de la LiXee-Box) ou image .bin.
 * info_url   : JSON de version facultatif (format GitHub releases/latest),
 *              lu par le navigateur pour afficher la version disponible.
 */
esp_err_t maint_auto_set_config(const char *update_url, const char *info_url,
                                char *err, size_t err_len);

/** Lance le telechargement + installation dans une tache dediee. */
esp_err_t maint_auto_start(char *err, size_t err_len);

#ifdef __cplusplus
}
#endif
