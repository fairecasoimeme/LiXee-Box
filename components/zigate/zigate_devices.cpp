// ============================================================================
// ZiGate Devices - Base de donnees des devices Zigbee appairies
// Persistance LittleFS en JSON
// ============================================================================

#include "zigate_devices.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

// LittleFS
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static const char *TAG = "ZIGATE_DEV";

// Table en PSRAM (17 Ko) : c'etait la plus grosse variable de la RAM interne,
// la ressource qui manque au pont. Lue et ecrite par des taches uniquement
// (jamais par une interruption), donc compatible avec la PSRAM ; mise a zero
// au demarrage comme un .bss ordinaire.
EXT_RAM_BSS_ATTR static zigate_device_t s_devices[ZIGATE_MAX_DEVICES];
static int s_device_count = 0;

#define DEVICES_FILE "/littlefs/zigbee_devices.json"

// Correction : add_or_update (tache ZiGate), remove (tache web, qui decalait
// le tableau), save (plusieurs taches, fopen "w" concurrents sur le meme
// fichier) et load s'executaient sans aucun verrou. Un verrou recursif les
// serialise (add_or_update et set_* appellent save). Les recherches restent
// sans verrou : elles rendent un pointeur dans la table, que l'API exposait
// deja ainsi ; les suppressions passent desormais par la tache ZiGate (voir
// zigate_run_on_proc), seule tache qui exploite ces pointeurs en continu.
// Le verrou est cree a la premiere utilisation (appel avant
// zigate_devices_init tolere : demarrage, une seule tache).
static SemaphoreHandle_t s_dev_mtx = NULL;

static void devs_lock(void)
{
    if (!s_dev_mtx) s_dev_mtx = xSemaphoreCreateRecursiveMutex();
    if (s_dev_mtx) xSemaphoreTakeRecursive(s_dev_mtx, portMAX_DELAY);
}

static void devs_unlock(void)
{
    if (s_dev_mtx) xSemaphoreGiveRecursive(s_dev_mtx);
}

// ============================================================================
// API
// ============================================================================

esp_err_t zigate_devices_init(void)
{
    devs_lock();
    memset(s_devices, 0, sizeof(s_devices));
    s_device_count = 0;
    devs_unlock();

    esp_err_t ret = zigate_devices_load();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No saved devices found, starting fresh");
    } else {
        ESP_LOGI(TAG, "Loaded %d devices from storage", s_device_count);
    }
    return ESP_OK;
}

int zigate_devices_add_or_update(const zigate_device_t *dev)
{
    devs_lock();
    // Chercher si le device existe deja
    for (int i = 0; i < s_device_count; i++) {
        if (s_devices[i].ieee_addr == dev->ieee_addr) {
            // Mise a jour
            s_devices[i].short_addr = dev->short_addr;
            s_devices[i].active = dev->active;
            if (dev->lqi) s_devices[i].lqi = dev->lqi;
            if (dev->device_id) s_devices[i].device_id = dev->device_id;
            if (dev->last_seen) s_devices[i].last_seen = dev->last_seen;
            if (dev->manufacturer[0]) strncpy(s_devices[i].manufacturer, dev->manufacturer, 32);
            if (dev->model[0]) strncpy(s_devices[i].model, dev->model, 32);
            if (dev->endpoint) s_devices[i].endpoint = dev->endpoint;
            ESP_LOGI(TAG, "Updated device %d: ieee=0x%016llX short=0x%04X",
                     i, dev->ieee_addr, dev->short_addr);
            zigate_devices_save();
            devs_unlock();
            return i;
        }
    }

    // Nouveau device
    if (s_device_count >= ZIGATE_MAX_DEVICES) {
        ESP_LOGE(TAG, "Device database full (%d)", ZIGATE_MAX_DEVICES);
        devs_unlock();
        return -1;
    }

    memcpy(&s_devices[s_device_count], dev, sizeof(zigate_device_t));
    s_devices[s_device_count].matter_endpoint = -1;
    int idx = s_device_count;
    s_device_count++;

    ESP_LOGI(TAG, "Added device %d: ieee=0x%016llX short=0x%04X",
             idx, dev->ieee_addr, dev->short_addr);
    zigate_devices_save();
    devs_unlock();
    return idx;
}

zigate_device_t* zigate_devices_find_by_ieee(uint64_t ieee_addr)
{
    for (int i = 0; i < s_device_count; i++) {
        if (s_devices[i].ieee_addr == ieee_addr) {
            return &s_devices[i];
        }
    }
    return NULL;
}

zigate_device_t* zigate_devices_find_by_short(uint16_t short_addr)
{
    for (int i = 0; i < s_device_count; i++) {
        if (s_devices[i].short_addr == short_addr) {
            return &s_devices[i];
        }
    }
    return NULL;
}

esp_err_t zigate_devices_remove(uint64_t ieee_addr)
{
    devs_lock();
    for (int i = 0; i < s_device_count; i++) {
        if (s_devices[i].ieee_addr == ieee_addr) {
            // Compteur reduit AVANT le decalage : un parcours concurrent sans
            // verrou ne lit jamais au-dela des entrees valides
            int n = s_device_count;
            s_device_count = n - 1;
            // Decaler les elements suivants
            for (int j = i; j < n - 1; j++) {
                memcpy(&s_devices[j], &s_devices[j + 1], sizeof(zigate_device_t));
            }
            memset(&s_devices[n - 1], 0, sizeof(zigate_device_t));
            ESP_LOGI(TAG, "Removed device ieee=0x%016llX", ieee_addr);
            zigate_devices_save();
            devs_unlock();
            return ESP_OK;
        }
    }
    devs_unlock();
    return ESP_ERR_NOT_FOUND;
}

int zigate_devices_count(void)
{
    return s_device_count;
}

zigate_device_t* zigate_devices_get(int index)
{
    if (index < 0 || index >= s_device_count) return NULL;
    return &s_devices[index];
}

esp_err_t zigate_devices_set_configured(uint64_t ieee_addr, bool configured)
{
    devs_lock();
    zigate_device_t *dev = zigate_devices_find_by_ieee(ieee_addr);
    if (!dev) { devs_unlock(); return ESP_ERR_NOT_FOUND; }
    dev->configured = configured;
    esp_err_t err = zigate_devices_save();
    devs_unlock();
    return err;
}

esp_err_t zigate_devices_set_matter_endpoint(uint64_t ieee_addr, int matter_ep)
{
    devs_lock();
    zigate_device_t *dev = zigate_devices_find_by_ieee(ieee_addr);
    if (!dev) { devs_unlock(); return ESP_ERR_NOT_FOUND; }
    dev->matter_endpoint = matter_ep;
    esp_err_t err = zigate_devices_save();
    devs_unlock();
    return err;
}

// ============================================================================
// Persistance JSON / LittleFS
// ============================================================================

esp_err_t zigate_devices_save(void)
{
    // Verrou tenu jusqu'a la fin de l'ecriture : deux sauvegardes simultanees
    // (fopen "w" sur le meme fichier) le corrompaient
    devs_lock();
    esp_err_t err = ESP_OK;
    cJSON *root = cJSON_CreateArray();
    if (!root) { devs_unlock(); return ESP_ERR_NO_MEM; }

    for (int i = 0; i < s_device_count; i++) {
        cJSON *obj = cJSON_CreateObject();
        char ieee_str[20];
        snprintf(ieee_str, sizeof(ieee_str), "%016llX", s_devices[i].ieee_addr);
        cJSON_AddStringToObject(obj, "ieee", ieee_str);
        cJSON_AddNumberToObject(obj, "short", s_devices[i].short_addr);
        cJSON_AddNumberToObject(obj, "device_id", s_devices[i].device_id);
        cJSON_AddNumberToObject(obj, "profile", s_devices[i].profile_id);
        cJSON_AddNumberToObject(obj, "endpoint", s_devices[i].endpoint);
        cJSON_AddStringToObject(obj, "manufacturer", s_devices[i].manufacturer);
        cJSON_AddStringToObject(obj, "model", s_devices[i].model);
        cJSON_AddStringToObject(obj, "alias", s_devices[i].alias);
        cJSON_AddBoolToObject(obj, "active", s_devices[i].active);
        cJSON_AddNumberToObject(obj, "matter_ep", s_devices[i].matter_endpoint);
        cJSON_AddBoolToObject(obj, "cfg", s_devices[i].configured);
        cJSON_AddNumberToObject(obj, "lqi", s_devices[i].lqi);
        cJSON_AddItemToArray(root, obj);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) { devs_unlock(); return ESP_ERR_NO_MEM; }

    FILE *f = fopen(DEVICES_FILE, "w");
    if (!f) {
        free(json_str);
        ESP_LOGE(TAG, "Failed to open %s for writing", DEVICES_FILE);
        devs_unlock();
        return ESP_FAIL;
    }
    if (fputs(json_str, f) < 0) err = ESP_FAIL;
    if (fclose(f) != 0) err = ESP_FAIL;
    free(json_str);

    if (err == ESP_OK) ESP_LOGD(TAG, "Saved %d devices to %s", s_device_count, DEVICES_FILE);
    else ESP_LOGE(TAG, "Write failed for %s", DEVICES_FILE);
    devs_unlock();
    return err;
}

static esp_err_t zigate_devices_load_locked(void);

esp_err_t zigate_devices_load(void)
{
    devs_lock();
    esp_err_t err = zigate_devices_load_locked();
    devs_unlock();
    return err;
}

static esp_err_t zigate_devices_load_locked(void)
{
    FILE *f = fopen(DEVICES_FILE, "r");
    if (!f) return ESP_ERR_NOT_FOUND;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 32768) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    char *buf = (char *)malloc(fsize + 1);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }
    fread(buf, 1, fsize, f);
    buf[fsize] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return ESP_ERR_INVALID_ARG;

    s_device_count = 0;
    int arr_size = cJSON_GetArraySize(root);
    for (int i = 0; i < arr_size && i < ZIGATE_MAX_DEVICES; i++) {
        cJSON *obj = cJSON_GetArrayItem(root, i);
        zigate_device_t *dev = &s_devices[s_device_count];
        memset(dev, 0, sizeof(zigate_device_t));

        cJSON *ieee_j = cJSON_GetObjectItem(obj, "ieee");
        if (ieee_j && ieee_j->valuestring) {
            sscanf(ieee_j->valuestring, "%016llX", &dev->ieee_addr);
        }

        cJSON *j;
        if ((j = cJSON_GetObjectItem(obj, "short")))       dev->short_addr = j->valueint;
        if ((j = cJSON_GetObjectItem(obj, "device_id")))    dev->device_id = j->valueint;
        if ((j = cJSON_GetObjectItem(obj, "profile")))      dev->profile_id = j->valueint;
        if ((j = cJSON_GetObjectItem(obj, "endpoint")))     dev->endpoint = j->valueint;
        if ((j = cJSON_GetObjectItem(obj, "active")))       dev->active = cJSON_IsTrue(j);
        if ((j = cJSON_GetObjectItem(obj, "matter_ep")))    dev->matter_endpoint = j->valueint;
        if ((j = cJSON_GetObjectItem(obj, "cfg")))          dev->configured = cJSON_IsTrue(j);
        if ((j = cJSON_GetObjectItem(obj, "lqi")))          dev->lqi = j->valueint;

        if ((j = cJSON_GetObjectItem(obj, "manufacturer")) && j->valuestring)
            strncpy(dev->manufacturer, j->valuestring, 32);
        if ((j = cJSON_GetObjectItem(obj, "model")) && j->valuestring)
            strncpy(dev->model, j->valuestring, 32);
        if ((j = cJSON_GetObjectItem(obj, "alias")) && j->valuestring)
            strncpy(dev->alias, j->valuestring, 32);

        s_device_count++;
    }

    cJSON_Delete(root);
    return ESP_OK;
}
