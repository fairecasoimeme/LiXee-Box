// ============================================================================
// Ecriture d'une image firmware dans la partition OTA inactive.
//
// Differences avec la LiXee-Box (Update.begin/write/end d'Arduino) :
//  - l'en-tete de l'image est controle AVANT d'ecrire quoi que ce soit :
//    octet magique, puce cible (ESP32-S3), description d'application et nom
//    de projet identique a celui qui tourne. Un firmware LiXee-Box (Arduino)
//    ou destine a une autre puce est donc refuse d'entree, au lieu d'etre
//    flashe puis de rendre l'appareil inutilisable ;
//  - effacement progressif (OTA_WITH_SEQUENTIAL_WRITES) : esp_ota_begin()
//    n'efface pas 3 Mo d'un coup, ce qui bloquerait le serveur web ~20 s ;
//  - la partition de demarrage ne change qu'apres esp_ota_end(), qui verifie
//    l'empreinte SHA-256 ajoutee a l'image (et la signature si Secure Boot).
// ============================================================================

#include "maint_internal.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "sdkconfig.h"

#include <string.h>

static const char *TAG = "MAINT_OTA";

static void set_err(char *err, size_t len, const char *msg)
{
    if (err && len) strlcpy(err, msg, len);
}

esp_err_t maint_ota_open(maint_ota_t *o, size_t expected, char *err, size_t err_len)
{
    memset(o, 0, sizeof(*o));
    o->part = esp_ota_get_next_update_partition(NULL);
    if (!o->part) {
        set_err(err, err_len, "Aucune partition OTA disponible");
        return ESP_ERR_NOT_FOUND;
    }
    if (expected && expected > o->part->size) {
        set_err(err, err_len, "Image plus grande que la partition OTA");
        return ESP_ERR_INVALID_SIZE;
    }
    o->expected = expected;
    o->open = true;
    ESP_LOGI(TAG, "OTA vers %s (0x%06lx, %lu octets), image annoncee %u octets",
             o->part->label, (unsigned long)o->part->address,
             (unsigned long)o->part->size, (unsigned)expected);
    return ESP_OK;
}

/** Controle de l'en-tete accumule (MAINT_OTA_HEAD_LEN octets). */
static esp_err_t check_head(maint_ota_t *o, char *err, size_t err_len)
{
    esp_image_header_t ih;
    esp_app_desc_t desc;
    memcpy(&ih, o->head, sizeof(ih));
    memcpy(&desc, o->head + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t),
           sizeof(desc));

    if (ih.magic != ESP_IMAGE_HEADER_MAGIC) {
        set_err(err, err_len, "Fichier non reconnu : ce n'est pas une image ESP32");
        return ESP_ERR_INVALID_VERSION;
    }
    if ((unsigned)ih.chip_id != (unsigned)CONFIG_IDF_FIRMWARE_CHIP_ID) {
        set_err(err, err_len, "Image compilee pour une autre puce que l'ESP32-S3");
        return ESP_ERR_INVALID_VERSION;
    }
    if (desc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
        set_err(err, err_len, "Image sans description ESP-IDF (firmware Arduino ?)");
        return ESP_ERR_INVALID_VERSION;
    }
    const esp_app_desc_t *running = esp_app_get_description();
    if (strncmp(desc.project_name, running->project_name, sizeof(desc.project_name)) != 0) {
        // Protection contre le firmware d'un autre produit (LiXee-Box, ZiGate...)
        ESP_LOGW(TAG, "Projet '%.32s' refuse (attendu '%.32s')", desc.project_name, running->project_name);
        set_err(err, err_len, "Image d'un autre produit (nom de projet different)");
        return ESP_ERR_INVALID_VERSION;
    }
    snprintf(o->version, sizeof(o->version), "%.31s", desc.version);   // champ non termine si plein
    ESP_LOGI(TAG, "Image valide : %.32s %.32s (%.16s %.16s, IDF %.32s)",
             desc.project_name, desc.version, desc.date, desc.time, desc.idf_ver);

    esp_err_t e = esp_ota_begin(o->part, OTA_WITH_SEQUENTIAL_WRITES, &o->handle);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(e));
        set_err(err, err_len, "Impossible de demarrer l'ecriture OTA");
        return e;
    }
    o->begun = true;
    e = esp_ota_write(o->handle, o->head, o->head_len);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(e));
        set_err(err, err_len, "Erreur d'ecriture en flash");
        return e;
    }
    o->written = o->head_len;
    return ESP_OK;
}

esp_err_t maint_ota_write(maint_ota_t *o, const uint8_t *data, size_t len, char *err, size_t err_len)
{
    if (!o->open) {
        set_err(err, err_len, "Session OTA non ouverte");
        return ESP_ERR_INVALID_STATE;
    }
    size_t fed = o->begun ? o->written : o->head_len;   // octets deja recus
    if (fed + len > o->part->size) {
        set_err(err, err_len, "Image plus grande que la partition OTA");
        return ESP_ERR_INVALID_SIZE;
    }
    if (!o->begun) {
        // Accumuler l'en-tete : il peut arriver en plusieurs morceaux
        size_t n = MAINT_OTA_HEAD_LEN - o->head_len;
        if (n > len) n = len;
        memcpy(o->head + o->head_len, data, n);
        o->head_len += n;
        data += n;
        len -= n;
        if (o->head_len < MAINT_OTA_HEAD_LEN) return ESP_OK;
        esp_err_t e = check_head(o, err, err_len);
        if (e != ESP_OK) return e;
    }
    if (len == 0) return ESP_OK;
    esp_err_t e = esp_ota_write(o->handle, data, len);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(e));
        set_err(err, err_len, "Erreur d'ecriture en flash");
        return e;
    }
    o->written += len;
    return ESP_OK;
}

esp_err_t maint_ota_close(maint_ota_t *o, char *err, size_t err_len)
{
    if (!o->begun) {
        set_err(err, err_len, "Image incomplete (en-tete absent)");
        return ESP_ERR_INVALID_SIZE;
    }
    if (o->expected && o->written != o->expected) {
        set_err(err, err_len, "Image incomplete (taille differente de celle annoncee)");
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t e = esp_ota_end(o->handle);
    o->begun = false;   // le handle est libere par esp_ota_end, meme en echec
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(e));
        set_err(err, err_len, e == ESP_ERR_OTA_VALIDATE_FAILED
                ? "Image corrompue (verification de l'empreinte en echec)"
                : "Echec de la finalisation OTA");
        return e;
    }
    // Relecture de la description depuis la flash : confirme ce qui a ete ecrit
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(o->part, &desc) != ESP_OK ||
        strncmp(desc.project_name, esp_app_get_description()->project_name, sizeof(desc.project_name)) != 0) {
        set_err(err, err_len, "Description de l'image illisible apres ecriture");
        return ESP_ERR_INVALID_VERSION;
    }
    o->ended = true;
    ESP_LOGI(TAG, "Image %s verifiee (%u octets) dans %s", o->version, (unsigned)o->written, o->part->label);
    return ESP_OK;
}

esp_err_t maint_ota_activate(maint_ota_t *o, char *err, size_t err_len)
{
    if (!o->ended) {
        set_err(err, err_len, "Image non verifiee");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t e = esp_ota_set_boot_partition(o->part);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(e));
        set_err(err, err_len, "Impossible de changer la partition de demarrage");
        return e;
    }
    ESP_LOGI(TAG, "Partition de demarrage : %s", o->part->label);
    return ESP_OK;
}

void maint_ota_abort(maint_ota_t *o)
{
    if (o->begun) {
        esp_ota_abort(o->handle);
        ESP_LOGW(TAG, "OTA abandonnee apres %u octets", (unsigned)o->written);
    }
    o->begun = false;
    o->open = false;
    o->ended = false;
}
