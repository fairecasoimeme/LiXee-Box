// ============================================================================
// ZiGate Devices - Base de donnees des devices Zigbee appairies
// Persistance LittleFS en JSON
// ============================================================================

#include "zigate_devices.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
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
// Derniere activite des appareils : sans elle, la fiche et la page Zigbee
// n'affichaient plus de dernier contact apres un redemarrage
#define SEEN_FILE    "/littlefs/zigbee_seen.json"
#define SEEN_SAVE_MS (10UL * 60 * 1000)

static bool     s_seen_dirty;
static uint32_t s_seen_saved_ms;

static void zigate_devices_load_seen(void);

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
        zigate_devices_load_seen();
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
    s_devices[s_device_count].matter_way_count = 0;
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

int zigate_devices_get_matter_way(uint64_t ieee_addr, uint8_t zb_ep)
{
    int r = -1;
    devs_lock();
    zigate_device_t *dev = zigate_devices_find_by_ieee(ieee_addr);
    for (int i = 0; dev && i < dev->matter_way_count && i < ZIGATE_MAX_MATTER_WAYS; i++) {
        if (dev->matter_ways[i].zb_ep == zb_ep) r = dev->matter_ways[i].matter_ep;
    }
    devs_unlock();
    return r;
}

esp_err_t zigate_devices_set_matter_way(uint64_t ieee_addr, uint8_t zb_ep, uint16_t matter_ep)
{
    devs_lock();
    zigate_device_t *dev = zigate_devices_find_by_ieee(ieee_addr);
    if (!dev) { devs_unlock(); return ESP_ERR_NOT_FOUND; }
    int slot = -1;
    for (int i = 0; i < dev->matter_way_count && i < ZIGATE_MAX_MATTER_WAYS; i++) {
        if (dev->matter_ways[i].zb_ep == zb_ep) slot = i;
    }
    if (slot >= 0 && dev->matter_ways[slot].matter_ep == matter_ep) { devs_unlock(); return ESP_OK; }
    if (slot < 0) {
        if (dev->matter_way_count >= ZIGATE_MAX_MATTER_WAYS) { devs_unlock(); return ESP_ERR_NO_MEM; }
        slot = dev->matter_way_count++;
    }
    dev->matter_ways[slot].zb_ep = zb_ep;
    dev->matter_ways[slot].matter_ep = matter_ep;
    esp_err_t err = zigate_devices_save();
    devs_unlock();
    return err;
}

// ============================================================================
// Endpoints multiples
// ============================================================================

bool zigate_device_has_cluster(const zigate_device_t *dev, uint16_t cluster)
{
    if (!dev) return false;
    for (int i = 0; i < dev->in_cluster_count && i < ZIGATE_MAX_CLUSTERS; i++) {
        if (dev->in_clusters[i] == cluster) return true;
    }
    return false;
}

uint8_t zigate_device_cluster_ep(const zigate_device_t *dev, uint16_t cluster)
{
    if (!dev) return 1;
    uint8_t main_ep = dev->endpoint ? dev->endpoint : 1;
    uint8_t first = 0;
    for (int i = 0; i < dev->in_cluster_count && i < ZIGATE_MAX_CLUSTERS; i++) {
        if (dev->in_clusters[i] != cluster) continue;
        uint8_t ep = dev->in_cluster_eps[i] ? dev->in_cluster_eps[i] : main_ep;
        if (ep == main_ep) return main_ep;
        if (!first) first = ep;
    }
    return first ? first : main_ep;
}

void zigate_device_clear_endpoints(zigate_device_t *dev)
{
    if (!dev) return;
    dev->ep_count = 0;
    dev->in_cluster_count = 0;
    dev->out_cluster_count = 0;
}

/** Retire de la liste (clusters, endpoints) les entrees de l'endpoint ep */
static uint8_t drop_ep(uint16_t *cl, uint8_t *eps, uint8_t count, uint8_t ep, uint8_t main_ep)
{
    uint8_t j = 0;
    for (uint8_t i = 0; i < count && i < ZIGATE_MAX_CLUSTERS; i++) {
        uint8_t e = eps[i] ? eps[i] : main_ep;
        if (e == ep) continue;
        cl[j] = cl[i];
        eps[j] = e;
        j++;
    }
    return j;
}

/** Ajoute les clusters de l'endpoint ep (tronque au-dela de ZIGATE_MAX_CLUSTERS) */
static uint8_t add_ep(uint16_t *cl, uint8_t *eps, uint8_t count, uint8_t ep,
                      const uint16_t *src, int n)
{
    for (int i = 0; i < n; i++) {
        if (count >= ZIGATE_MAX_CLUSTERS) {
            ESP_LOGW(TAG, "Plus de %d clusters : endpoint %u tronque", ZIGATE_MAX_CLUSTERS, ep);
            break;
        }
        cl[count] = src[i];
        eps[count] = ep;
        count++;
    }
    return count;
}

/**
 * Endpoint principal : premier endpoint de profil HA (0x0104), sinon ZLL
 * (0xC05E), dans l'ordre des numeros. Un capteur Hue (ZLL sur 1, HA sur 2)
 * garde ainsi le 2 ; un Ztriac (1, 3, 4) le 1.
 */
static const zigate_ep_info_t *main_endpoint(const zigate_device_t *dev)
{
    const zigate_ep_info_t *ha = NULL, *zll = NULL;
    for (int i = 0; i < dev->ep_count && i < ZIGATE_MAX_EPS; i++) {
        const zigate_ep_info_t *e = &dev->eps[i];
        if (e->profile_id == 0x0104) { if (!ha || e->ep < ha->ep) ha = e; }
        else if (!zll || e->ep < zll->ep) zll = e;
    }
    return ha ? ha : zll;
}

void zigate_device_set_endpoint_desc(zigate_device_t *dev, uint8_t ep, uint16_t profile_id,
                                     uint16_t device_id, const uint16_t *in, int n_in,
                                     const uint16_t *out, int n_out)
{
    if (!dev || !ep) return;
    devs_lock();
    uint8_t old_main = dev->endpoint ? dev->endpoint : ep;

    // Endpoint : mis a jour s'il est connu, ajoute sinon
    int slot = -1;
    for (int i = 0; i < dev->ep_count && i < ZIGATE_MAX_EPS; i++) {
        if (dev->eps[i].ep == ep) slot = i;
    }
    if (slot < 0 && dev->ep_count < ZIGATE_MAX_EPS) slot = dev->ep_count++;
    if (slot < 0) {
        ESP_LOGW(TAG, "0x%016llX : plus de %d endpoints, %u ignore",
                 (unsigned long long)dev->ieee_addr, ZIGATE_MAX_EPS, ep);
        devs_unlock();
        return;
    }
    dev->eps[slot].ep = ep;
    dev->eps[slot].profile_id = profile_id;
    dev->eps[slot].device_id = device_id;

    // Clusters de cet endpoint remplaces (entrees sans endpoint, d'avant la
    // v3.4 : celles de l'ancien endpoint principal)
    dev->in_cluster_count = drop_ep(dev->in_clusters, dev->in_cluster_eps,
                                    dev->in_cluster_count, ep, old_main);
    dev->out_cluster_count = drop_ep(dev->out_clusters, dev->out_cluster_eps,
                                     dev->out_cluster_count, ep, old_main);
    dev->in_cluster_count = add_ep(dev->in_clusters, dev->in_cluster_eps,
                                   dev->in_cluster_count, ep, in, n_in);
    dev->out_cluster_count = add_ep(dev->out_clusters, dev->out_cluster_eps,
                                    dev->out_cluster_count, ep, out, n_out);

    const zigate_ep_info_t *m = main_endpoint(dev);
    if (m) {
        dev->endpoint = m->ep;
        dev->device_id = m->device_id;
        dev->profile_id = m->profile_id;
    }
    devs_unlock();
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
        if (s_devices[i].matter_way_count) {
            // Voies exposees par leur propre endpoint Matter : [endpoint Zigbee, endpoint Matter]
            cJSON *a = cJSON_AddArrayToObject(obj, "matter_ways");
            for (int k = 0; a && k < s_devices[i].matter_way_count && k < ZIGATE_MAX_MATTER_WAYS; k++) {
                int v[2] = { s_devices[i].matter_ways[k].zb_ep, s_devices[i].matter_ways[k].matter_ep };
                cJSON_AddItemToArray(a, cJSON_CreateIntArray(v, 2));
            }
        }
        cJSON_AddBoolToObject(obj, "cfg", s_devices[i].configured);
        cJSON_AddNumberToObject(obj, "lqi", s_devices[i].lqi);
        // Endpoints [ep, profil, deviceId] et clusters [cluster, ep] (v3.4)
        const zigate_device_t *d = &s_devices[i];
        if (d->ep_count) {
            cJSON *a = cJSON_AddArrayToObject(obj, "eps");
            for (int k = 0; a && k < d->ep_count && k < ZIGATE_MAX_EPS; k++) {
                int v[3] = { d->eps[k].ep, d->eps[k].profile_id, d->eps[k].device_id };
                cJSON_AddItemToArray(a, cJSON_CreateIntArray(v, 3));
            }
        }
        static const char *const KEYS[2] = { "in", "out" };
        for (int s = 0; s < 2; s++) {
            const uint16_t *cl = s ? d->out_clusters : d->in_clusters;
            const uint8_t *ce = s ? d->out_cluster_eps : d->in_cluster_eps;
            int n = s ? d->out_cluster_count : d->in_cluster_count;
            if (!n) continue;
            cJSON *a = cJSON_AddArrayToObject(obj, KEYS[s]);
            for (int k = 0; a && k < n && k < ZIGATE_MAX_CLUSTERS; k++) {
                int v[2] = { cl[k], ce[k] ? ce[k] : d->endpoint };
                cJSON_AddItemToArray(a, cJSON_CreateIntArray(v, 2));
            }
        }
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

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void zigate_devices_mark_seen(zigate_device_t *dev, uint32_t epoch)
{
    if (!dev) return;
    dev->last_seen = epoch;
    s_seen_dirty = true;
}

void zigate_devices_save_seen(bool force)
{
    if (!s_seen_dirty) return;
    if (!force && s_seen_saved_ms && now_ms() - s_seen_saved_ms < SEEN_SAVE_MS) return;
    devs_lock();
    cJSON *root = cJSON_CreateObject();
    for (int i = 0; root && i < s_device_count; i++) {
        if (!s_devices[i].last_seen) continue;
        char ieee_str[20];
        snprintf(ieee_str, sizeof(ieee_str), "%016llX", s_devices[i].ieee_addr);
        cJSON_AddNumberToObject(root, ieee_str, (double)s_devices[i].last_seen);
    }
    s_seen_dirty = false;
    char *json_str = root ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    bool ok = false;
    if (json_str) {
        FILE *f = fopen(SEEN_FILE, "w");
        if (f) {
            ok = fputs(json_str, f) >= 0;
            if (fclose(f) != 0) ok = false;
        }
        free(json_str);
    }
    if (!ok) {
        s_seen_dirty = true;
        ESP_LOGE(TAG, "Write failed for %s", SEEN_FILE);
    }
    s_seen_saved_ms = now_ms() ? now_ms() : 1;
    devs_unlock();
}

static void zigate_devices_load_seen(void)
{
    FILE *f = fopen(SEEN_FILE, "r");
    if (!f) return;
    char *buf = (char *)malloc(4096);
    size_t n = buf ? fread(buf, 1, 4095, f) : 0;
    fclose(f);
    if (!buf) return;
    buf[n] = '\0';
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) { ESP_LOGW(TAG, SEEN_FILE " illisible"); return; }
    devs_lock();
    for (int i = 0; i < s_device_count; i++) {
        char ieee_str[20];
        snprintf(ieee_str, sizeof(ieee_str), "%016llX", s_devices[i].ieee_addr);
        const cJSON *v = cJSON_GetObjectItem(root, ieee_str);
        if (cJSON_IsNumber(v) && !s_devices[i].last_seen)
            s_devices[i].last_seen = (uint32_t)v->valuedouble;
    }
    devs_unlock();
    cJSON_Delete(root);
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

    // Depuis la v3.4 le fichier porte aussi les endpoints et les clusters :
    // ~0,7 Ko par appareil, soit ~45 Ko pour 64 appareils (32 Ko avant)
    if (fsize <= 0 || fsize > 160 * 1024) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    char *buf = (char *)heap_caps_malloc(fsize + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (char *)malloc(fsize + 1);
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

        // Endpoints et clusters (v3.4 ; absents des fichiers plus anciens)
        cJSON *e;
        cJSON *ways = cJSON_GetObjectItem(obj, "matter_ways");
        cJSON_ArrayForEach(e, ways) {
            if (dev->matter_way_count >= ZIGATE_MAX_MATTER_WAYS || cJSON_GetArraySize(e) < 2) break;
            zigate_matter_way_t *w = &dev->matter_ways[dev->matter_way_count++];
            w->zb_ep = (uint8_t)cJSON_GetArrayItem(e, 0)->valueint;
            w->matter_ep = (uint16_t)cJSON_GetArrayItem(e, 1)->valueint;
        }
        cJSON *eps = cJSON_GetObjectItem(obj, "eps");
        cJSON_ArrayForEach(e, eps) {
            if (dev->ep_count >= ZIGATE_MAX_EPS || cJSON_GetArraySize(e) < 3) break;
            zigate_ep_info_t *x = &dev->eps[dev->ep_count++];
            x->ep = (uint8_t)cJSON_GetArrayItem(e, 0)->valueint;
            x->profile_id = (uint16_t)cJSON_GetArrayItem(e, 1)->valueint;
            x->device_id = (uint16_t)cJSON_GetArrayItem(e, 2)->valueint;
        }
        for (int s = 0; s < 2; s++) {
            uint16_t *cl = s ? dev->out_clusters : dev->in_clusters;
            uint8_t *ce = s ? dev->out_cluster_eps : dev->in_cluster_eps;
            uint8_t *cnt = s ? &dev->out_cluster_count : &dev->in_cluster_count;
            cJSON *arr = cJSON_GetObjectItem(obj, s ? "out" : "in");
            cJSON_ArrayForEach(e, arr) {
                if (*cnt >= ZIGATE_MAX_CLUSTERS || cJSON_GetArraySize(e) < 2) break;
                cl[*cnt] = (uint16_t)cJSON_GetArrayItem(e, 0)->valueint;
                ce[*cnt] = (uint8_t)cJSON_GetArrayItem(e, 1)->valueint;
                (*cnt)++;
            }
        }

        s_device_count++;
    }

    cJSON_Delete(root);
    return ESP_OK;
}
