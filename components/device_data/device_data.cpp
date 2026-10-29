// ============================================================================
// Device Data - Stockage des valeurs cluster/attribut par device Zigbee
//
// Port de DeviceData Arduino (device.h/cpp) vers ESP-IDF en C.
// Utilise cJSON pour la serialisation et VFS/LittleFS pour la persistance.
// ============================================================================

#include "device_data.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>   // fsync, unlink
#include <math.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "DEVDATA";

// ============================================================================
// Stockage: tableau de POINTEURS (512 bytes en RAM)
// Les device_data_t eux-memes sont alloues en PSRAM (~16 KB chacun)
//
// Correction (corruption de tas a la suppression d'un appareil) : une entree
// n'est plus JAMAIS liberee. D'autres taches gardent des pointeurs vers les
// device_data_t (poll_engine ecrit pi->last_poll, device_data_save garde dd
// pendant l'ecriture du fichier, les taches regles / notifications lisent le
// template et les valeurs) : un free() pendant ces acces ecrivait dans un bloc
// libere, voire deja realloue. Une suppression marque desormais le slot libre
// (ieee_addr = 0, valeurs effacees) et la structure est reutilisee par le
// prochain device_data_get_or_create(). Un pointeur perime designe au pire un
// slot vide ou un autre appareil, jamais de la memoire liberee.
// ============================================================================

static device_data_t *s_devices[DEVDATA_MAX_DEVICES] = {};
static int s_device_count = 0;
static bool s_initialized = false;

// Generation de chaque slot, incrementee a chaque suppression / reutilisation :
// permet a device_data_save() de savoir si l'appareil a ete supprime pendant
// son ecriture de fichier (sinon le fichier serait recree : appareil
// "ressuscite" au demarrage suivant).
static uint32_t s_slot_gen[DEVDATA_MAX_DEVICES] = {};

// Un seul verrou (recursif : set_value appelle get_or_create) pour find,
// get_or_create, remove, les modifications de valeurs et la construction du
// JSON des sauvegardes. Cree dans device_data_init ; un appel anterieur
// (demarrage, une seule tache) le cree a la volee.
static SemaphoreHandle_t s_mtx = NULL;

static void dd_lock(void)
{
    if (!s_mtx) s_mtx = xSemaphoreCreateRecursiveMutex();
    if (s_mtx) xSemaphoreTakeRecursive(s_mtx, portMAX_DELAY);
}

static void dd_unlock(void)
{
    if (s_mtx) xSemaphoreGiveRecursive(s_mtx);
}

/** Slot occupe : structure allouee et appareil present (ieee != 0). */
static inline bool slot_used(int i)
{
    return s_devices[i] && s_devices[i]->ieee_addr != 0;
}

/** Index du slot de l'appareil, -1 si absent. Verrou tenu par l'appelant. */
static int find_slot_locked(uint64_t ieee_addr)
{
    if (ieee_addr == 0) return -1;              // 0 = marque de slot libre
    for (int i = 0; i < DEVDATA_MAX_DEVICES; i++) {
        if (s_devices[i] && s_devices[i]->ieee_addr == ieee_addr) return i;
    }
    return -1;
}

/**
 * Alloue un device_data_t en PSRAM (fallback heap).
 */
static device_data_t* alloc_device_data(void)
{
    device_data_t *dd = (device_data_t *)heap_caps_calloc(1, sizeof(device_data_t), MALLOC_CAP_SPIRAM);
    if (!dd) {
        // Fallback sur la heap interne
        dd = (device_data_t *)calloc(1, sizeof(device_data_t));
    }
    return dd;
}

/**
 * Remet a vide le contenu d'une entree (valeurs, poll list, info), sans la
 * liberer. Les compteurs sont remis a zero AVANT l'effacement : un lecteur
 * concurrent sans verrou cesse de parcourir les tableaux au lieu de lire des
 * entrees a moitie effacees (les chaines restent toujours terminees).
 * Le template reste attache mais est marque non charge (loaded = false,
 * compteurs a zero) : tous les lecteurs testent 'loaded', et le pointeur
 * n'est jamais remis a NULL ni libere, si bien qu'un lecteur qui l'a deja
 * teste (poll_engine, pages web) ne dereference jamais un pointeur nul ou
 * libere. device_data_load_template() le reutilise. Verrou tenu.
 */
// Actions dynamiques par IEEE (PSRAM), independantes des slots : elles
// survivent a la suppression puis au nouvel appairage d'un appareil
#define DYN_MAX_DEV 8
typedef struct {
    uint64_t ieee;
    int      n;
    devdata_template_action_t a[DEVDATA_MAX_DYN_ACTIONS];
} dyn_entry_t;
static dyn_entry_t *s_dyn;

static dyn_entry_t *dyn_find_locked(uint64_t ieee, bool create)
{
    if (!s_dyn) {
        if (!create) return NULL;
        s_dyn = (dyn_entry_t *)heap_caps_calloc(DYN_MAX_DEV, sizeof(dyn_entry_t), MALLOC_CAP_SPIRAM);
        if (!s_dyn) return NULL;
    }
    dyn_entry_t *free_e = NULL;
    for (int i = 0; i < DYN_MAX_DEV; i++) {
        if (s_dyn[i].ieee == ieee) return &s_dyn[i];
        if (!s_dyn[i].ieee && !free_e) free_e = &s_dyn[i];
    }
    if (!create || !free_e) return NULL;
    memset(free_e, 0, sizeof(*free_e));
    free_e->ieee = ieee;
    return free_e;
}

/**
 * Ajoute les actions dynamiques de l'appareil apres celles de son template
 * (verrou tenu). Le nombre d'actions n'est publie qu'une fois les entrees
 * ecrites ; il baisse avant toute reecriture (lecteurs sans verrou).
 */
static void apply_dyn_locked(device_data_t *dd)
{
    dyn_entry_t *e = dyn_find_locked(dd->ieee_addr, false);
    int n_dyn = e ? e->n : 0;
    if (!dd->tmpl) {
        if (!n_dyn) return;
        dd->tmpl = (devdata_template_t *)heap_caps_calloc(1, sizeof(devdata_template_t), MALLOC_CAP_SPIRAM);
        if (!dd->tmpl) dd->tmpl = (devdata_template_t *)calloc(1, sizeof(devdata_template_t));
        if (!dd->tmpl) return;
    }
    devdata_template_t *t = dd->tmpl;
    if (!t->loaded || t->dyn_only) {
        // Pas de fichier de template : template des seules actions dynamiques
        t->action_count = 0;
        t->state_count = t->bind_count = t->report_count = 0;
        t->base_action_count = 0;
        if (!n_dyn) {
            t->dyn_only = false;
            t->loaded = false;
            return;
        }
        t->dyn_only = true;
    }
    int n = t->base_action_count;
    t->action_count = n;
    for (int k = 0; k < n_dyn && n < DEVDATA_MAX_TEMPLATE_ACTIONS; k++) t->actions[n++] = e->a[k];
    t->action_count = n;
    if (t->dyn_only) t->loaded = true;
}

static void clear_slot_locked(int slot)
{
    device_data_t *dd = s_devices[slot];
    if (!dd) return;
    dd->ieee_addr = 0;
    dd->cluster_count = 0;
    dd->poll_count = 0;
    dd->dirty = false;
    if (dd->tmpl) {
        dd->tmpl->loaded = false;
        dd->tmpl->dyn_only = false;
        dd->tmpl->state_count = 0;
        dd->tmpl->action_count = 0;
        dd->tmpl->base_action_count = 0;
        dd->tmpl->bind_count = 0;
        dd->tmpl->report_count = 0;
    }
    memset(&dd->info, 0, sizeof(dd->info));
    memset(dd->clusters, 0, sizeof(dd->clusters));
    memset(dd->poll_list, 0, sizeof(dd->poll_list));
    s_slot_gen[slot]++;
}

// ============================================================================
// Helpers internes
// ============================================================================

/**
 * Genere le chemin du fichier JSON pour un device.
 * Format: /littlefs/db/<IEEE_hex>.json
 */
static void make_filepath(uint64_t ieee_addr, char *out, size_t out_len)
{
    snprintf(out, out_len, DEVDATA_DB_DIR "/%016llX.json", (unsigned long long)ieee_addr);
}

/**
 * Lit un fichier complet en memoire (alloue avec malloc).
 * @return Buffer alloue (a liberer), ou NULL si erreur.
 */
static char* read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0) {
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc(fsize + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t rd = fread(buf, 1, fsize, f);
    fclose(f);
    buf[rd] = '\0';

    if (out_size) *out_size = rd;
    return buf;
}

/**
 * Ecrit un buffer dans un fichier.
 */
static esp_err_t write_file(const char *path, const char *data, size_t data_len)
{
    // Ecriture atomique : fichier temporaire, puis rename.
    //
    // fopen(path, "w") tronquait la cible AVANT d'ecrire : une coupure
    // d'alimentation ou une partition pleine laissait un fichier vide ou
    // tronque, donc une perte definitive de donnees valides.
    // Le retour de fclose etait de plus ignore, alors que c'est a la fermeture
    // que LittleFS valide blocs et metadonnees : un echec d'ecriture etait
    // rapporte comme un succes, et l'appelant effacait son drapeau 'dirty'.
    //
    // rename() est atomique sur LittleFS (operation de metadonnees) : la cible
    // conserve son ancien contenu valide jusqu'au basculement. Un .tmp
    // residuel apres coupure est ignore par les parcours de repertoire, qui
    // exigent une terminaison exacte en ".json".
    char tmp[96];
    int plen = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (plen < 0 || plen >= (int)sizeof(tmp)) {
        ESP_LOGE(TAG, "Path too long: %s", path);
        return ESP_FAIL;
    }

    FILE *f = fopen(tmp, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s for write: %s", tmp, strerror(errno));
        return ESP_FAIL;
    }

    size_t len = data_len;
    bool ok = (fwrite(data, 1, len, f) == len);
    if (ok && fflush(f) != 0)        ok = false;
    if (ok && fsync(fileno(f)) != 0) ok = false;
    if (fclose(f) != 0)              ok = false;   // LittleFS valide ici

    if (!ok) {
        ESP_LOGE(TAG, "Write failed for %s: %s (ancien contenu conserve)",
                 path, strerror(errno));
        unlink(tmp);
        return ESP_FAIL;
    }

    if (rename(tmp, path) != 0) {
        ESP_LOGE(TAG, "Cannot commit %s: %s", path, strerror(errno));
        unlink(tmp);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * Cherche un cluster dans un device_data_t.
 * @return Pointeur vers le cluster, ou NULL.
 */
static devdata_cluster_t* find_cluster(device_data_t *dd, uint16_t cluster_id)
{
    for (int i = 0; i < dd->cluster_count; i++) {
        if (dd->clusters[i].cluster_id == cluster_id) {
            return &dd->clusters[i];
        }
    }
    return NULL;
}

/**
 * Cherche ou cree un cluster dans un device_data_t.
 */
static devdata_cluster_t* find_or_create_cluster(device_data_t *dd, uint16_t cluster_id)
{
    devdata_cluster_t *cl = find_cluster(dd, cluster_id);
    if (cl) return cl;

    if (dd->cluster_count >= DEVDATA_MAX_CLUSTERS) {
        ESP_LOGW(TAG, "Max clusters reached for device 0x%016llX", (unsigned long long)dd->ieee_addr);
        return NULL;
    }

    cl = &dd->clusters[dd->cluster_count++];
    memset(cl, 0, sizeof(devdata_cluster_t));
    cl->cluster_id = cluster_id;
    return cl;
}

/**
 * Cherche un attribut dans un cluster.
 */
static devdata_attr_t* find_attr(devdata_cluster_t *cl, uint16_t attr_id)
{
    for (int i = 0; i < cl->attr_count; i++) {
        if (cl->attrs[i].attr_id == attr_id) {
            return &cl->attrs[i];
        }
    }
    return NULL;
}

/**
 * Cherche ou cree un attribut dans un cluster.
 */
static devdata_attr_t* find_or_create_attr(devdata_cluster_t *cl, uint16_t attr_id)
{
    devdata_attr_t *attr = find_attr(cl, attr_id);
    if (attr) return attr;

    if (cl->attr_count >= DEVDATA_MAX_ATTRS) {
        ESP_LOGW(TAG, "Max attrs reached for cluster 0x%04X", cl->cluster_id);
        return NULL;
    }

    attr = &cl->attrs[cl->attr_count++];
    memset(attr, 0, sizeof(devdata_attr_t));
    attr->attr_id = attr_id;
    return attr;
}

// ============================================================================
// Serialisation JSON - Sauvegarde
// ============================================================================

/**
 * Serialise un device_data_t en cJSON.
 * Format compatible avec l'Arduino:
 * {
 *   "INFO": { "linkyMode": "0", "powerSocket": "0", "alias": "...", "status": "..." },
 *   "poll": [ { "cluster": "0x0702", "attribut": 0, "poll": 30000, "last": 0 }, ... ],
 *   "0402": { "0": "0A2F", "1": "..." },
 *   "0B04": { "1285": "..." },
 *   ...
 * }
 */
static cJSON* device_to_json(device_data_t *dd)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    // INFO
    cJSON *info = cJSON_CreateObject();
    cJSON_AddStringToObject(info, "linkyMode", dd->info.linky_mode);
    cJSON_AddStringToObject(info, "powerSocket", dd->info.power_socket);
    cJSON_AddStringToObject(info, "alias", dd->info.alias);
    cJSON_AddStringToObject(info, "status", dd->info.status);
    cJSON_AddItemToObject(root, "INFO", info);

    // Poll list
    cJSON *poll_arr = cJSON_CreateArray();
    for (int i = 0; i < dd->poll_count; i++) {
        devdata_poll_item_t *pi = &dd->poll_list[i];
        cJSON *item = cJSON_CreateObject();

        char cluster_str[8];
        snprintf(cluster_str, sizeof(cluster_str), "0x%04X", pi->cluster_id);
        cJSON_AddStringToObject(item, "cluster", cluster_str);
        cJSON_AddNumberToObject(item, "attribut", pi->attr_id);
        cJSON_AddNumberToObject(item, "poll", pi->interval_ms);
        cJSON_AddNumberToObject(item, "last", pi->last_poll);
        if (pi->manufacturer_code != 0) {
            cJSON_AddNumberToObject(item, "manufacturerCode", pi->manufacturer_code);
        }
        cJSON_AddItemToArray(poll_arr, item);
    }
    cJSON_AddItemToObject(root, "poll", poll_arr);

    // Clusters et attributs
    for (int i = 0; i < dd->cluster_count; i++) {
        devdata_cluster_t *cl = &dd->clusters[i];

        char cluster_key[8];
        snprintf(cluster_key, sizeof(cluster_key), "%04X", cl->cluster_id);

        cJSON *cl_obj = cJSON_CreateObject();
        for (int j = 0; j < cl->attr_count; j++) {
            devdata_attr_t *attr = &cl->attrs[j];
            char attr_key[8];
            snprintf(attr_key, sizeof(attr_key), "%d", attr->attr_id);
            cJSON_AddStringToObject(cl_obj, attr_key, attr->value);
        }
        cJSON_AddItemToObject(root, cluster_key, cl_obj);
    }

    // Types ZCL connus : "_T": { "0402": { "0": 41 } }. Cle ignoree par les
    // versions precedentes (non hexa), donc fichier toujours relisible.
    cJSON *types = NULL;
    for (int i = 0; i < dd->cluster_count; i++) {
        devdata_cluster_t *cl = &dd->clusters[i];
        cJSON *t_cl = NULL;
        for (int j = 0; j < cl->attr_count; j++) {
            if (!cl->attrs[j].type) continue;
            if (!types) types = cJSON_AddObjectToObject(root, "_T");
            if (!types) break;
            if (!t_cl) {
                char cluster_key[8];
                snprintf(cluster_key, sizeof(cluster_key), "%04X", cl->cluster_id);
                t_cl = cJSON_AddObjectToObject(types, cluster_key);
                if (!t_cl) break;
            }
            char attr_key[8];
            snprintf(attr_key, sizeof(attr_key), "%d", cl->attrs[j].attr_id);
            cJSON_AddNumberToObject(t_cl, attr_key, cl->attrs[j].type);
        }
    }

    return root;
}

// ============================================================================
// Deserialisation JSON - Chargement
// ============================================================================

static esp_err_t json_to_device(device_data_t *dd, const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error for device 0x%016llX", (unsigned long long)dd->ieee_addr);
        return ESP_ERR_INVALID_ARG;
    }

    // INFO
    cJSON *info = cJSON_GetObjectItem(root, "INFO");
    if (info) {
        cJSON *v;
        v = cJSON_GetObjectItem(info, "linkyMode");
        if (v && v->valuestring) strncpy(dd->info.linky_mode, v->valuestring, sizeof(dd->info.linky_mode) - 1);

        v = cJSON_GetObjectItem(info, "powerSocket");
        if (v && v->valuestring) strncpy(dd->info.power_socket, v->valuestring, sizeof(dd->info.power_socket) - 1);

        v = cJSON_GetObjectItem(info, "alias");
        if (v && v->valuestring) strncpy(dd->info.alias, v->valuestring, sizeof(dd->info.alias) - 1);

        v = cJSON_GetObjectItem(info, "status");
        if (v && v->valuestring) strncpy(dd->info.status, v->valuestring, sizeof(dd->info.status) - 1);
    }

    // Poll list
    cJSON *poll_arr = cJSON_GetObjectItem(root, "poll");
    if (poll_arr && cJSON_IsArray(poll_arr)) {
        dd->poll_count = 0;
        cJSON *item;
        cJSON_ArrayForEach(item, poll_arr) {
            if (dd->poll_count >= DEVDATA_MAX_POLL_ITEMS) break;

            devdata_poll_item_t *pi = &dd->poll_list[dd->poll_count];
            memset(pi, 0, sizeof(devdata_poll_item_t));

            cJSON *v;
            v = cJSON_GetObjectItem(item, "cluster");
            if (v && v->valuestring) {
                pi->cluster_id = (uint16_t)strtoul(v->valuestring, NULL, 16);
            }
            v = cJSON_GetObjectItem(item, "attribut");
            if (v) pi->attr_id = (uint16_t)v->valuedouble;

            v = cJSON_GetObjectItem(item, "poll");
            if (v) pi->interval_ms = (uint32_t)v->valuedouble;

            v = cJSON_GetObjectItem(item, "last");
            if (v) pi->last_poll = (uint32_t)v->valuedouble;

            v = cJSON_GetObjectItem(item, "manufacturerCode");
            if (v) pi->manufacturer_code = (uint16_t)v->valuedouble;

            dd->poll_count++;
        }
    }

    // Clusters/attributs: toutes les cles sauf "INFO" et "poll"
    cJSON *entry;
    cJSON_ArrayForEach(entry, root) {
        const char *key = entry->string;
        if (!key) continue;
        if (strcmp(key, "INFO") == 0 || strcmp(key, "poll") == 0 || strcmp(key, "_T") == 0) continue;
        if (!cJSON_IsObject(entry)) continue;

        // Cle = cluster en hex (ex: "0402", "0B04", "FF66")
        uint16_t cluster_id = (uint16_t)strtoul(key, NULL, 16);
        if (cluster_id == 0 && strcmp(key, "0000") != 0) {
            // Cle non-hex, ignorer
            continue;
        }

        devdata_cluster_t *cl = find_or_create_cluster(dd, cluster_id);
        if (!cl) continue;

        // Parcourir les attributs
        cJSON *attr_entry;
        cJSON_ArrayForEach(attr_entry, entry) {
            const char *attr_key = attr_entry->string;
            if (!attr_key) continue;

            uint16_t attr_id = (uint16_t)strtoul(attr_key, NULL, 10);
            const char *val = NULL;
            char num_buf[32];                   // local (et non statique) : reentrant

            if (cJSON_IsString(attr_entry)) {
                val = attr_entry->valuestring;
            } else if (cJSON_IsNumber(attr_entry)) {
                // Cas rare: nombre stocke directement
                snprintf(num_buf, sizeof(num_buf), "%d", (int)attr_entry->valuedouble);
                val = num_buf;
            }

            if (val) {
                devdata_attr_t *attr = find_or_create_attr(cl, attr_id);
                if (attr) {
                    strncpy(attr->value, val, DEVDATA_VALUE_LEN - 1);
                    attr->value[DEVDATA_VALUE_LEN - 1] = '\0';
                }
            }
        }
    }

    // Types ZCL (apres les valeurs : ne concerne que des attributs connus)
    cJSON *types = cJSON_GetObjectItem(root, "_T");
    if (types && cJSON_IsObject(types)) {
        cJSON *t_cl;
        cJSON_ArrayForEach(t_cl, types) {
            if (!t_cl->string || !cJSON_IsObject(t_cl)) continue;
            devdata_cluster_t *cl = find_cluster(dd, (uint16_t)strtoul(t_cl->string, NULL, 16));
            if (!cl) continue;
            cJSON *t_at;
            cJSON_ArrayForEach(t_at, t_cl) {
                if (!t_at->string || !cJSON_IsNumber(t_at)) continue;
                devdata_attr_t *attr = find_attr(cl, (uint16_t)strtoul(t_at->string, NULL, 10));
                if (attr) attr->type = (uint8_t)t_at->valuedouble;
            }
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

// ============================================================================
// Template - Chargement depuis /littlefs/tp/
// ============================================================================

static esp_err_t parse_template_json(devdata_template_t *tmpl, const char *json_str, const char *model_name)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "Template JSON parse error");
        return ESP_ERR_INVALID_ARG;
    }

    // Chercher le modele : 1) model_name exact, 2) "default" -- comme la
    // passerelle Arduino. L'ancien repli sur "le premier modele trouve"
    // appliquait a un appareil la configuration d'un autre (bind et reporting
    // du ZLinky sur un appareil inconnu, par exemple).
    cJSON *model_array = NULL;

    if (model_name && model_name[0] != '\0') {
        model_array = cJSON_GetObjectItem(root, model_name);
    }
    if (!model_array || !cJSON_IsArray(model_array)) {
        model_array = cJSON_GetObjectItem(root, "default");
    }

    if (!model_array || !cJSON_IsArray(model_array) || cJSON_GetArraySize(model_array) == 0) {
        ESP_LOGW(TAG, "No model found in template");
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *model_obj = cJSON_GetArrayItem(model_array, 0);
    if (!model_obj) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    // Parser les "status" (= states)
    tmpl->state_count = 0;
    cJSON *status_arr = cJSON_GetObjectItem(model_obj, "status");
    if (status_arr && cJSON_IsArray(status_arr)) {
        cJSON *state_obj;
        cJSON_ArrayForEach(state_obj, status_arr) {
            if (tmpl->state_count >= DEVDATA_MAX_TEMPLATE_STATES) break;

            devdata_template_state_t *s = &tmpl->states[tmpl->state_count];
            memset(s, 0, sizeof(devdata_template_state_t));
            s->coefficient = 1.0f;
            s->visible = true;
            s->jauge_max = 100;

            cJSON *v;
            v = cJSON_GetObjectItem(state_obj, "name");
            if (v && v->valuestring) strncpy(s->name, v->valuestring, sizeof(s->name) - 1);

            // Cluster: peut etre string hex ou int
            v = cJSON_GetObjectItem(state_obj, "cluster");
            if (v) {
                if (cJSON_IsString(v)) {
                    s->cluster = (uint16_t)strtoul(v->valuestring, NULL, 16);
                } else {
                    s->cluster = (uint16_t)v->valuedouble;
                }
            }

            v = cJSON_GetObjectItem(state_obj, "attribut");
            if (v) s->attribute = (uint16_t)v->valuedouble;

            // Ligne propre a un endpoint : cle composee (epAttrKey de la LiXee-Box)
            v = cJSON_GetObjectItem(state_obj, "endpoint");
            if (cJSON_IsNumber(v) || cJSON_IsString(v)) {
                long ep = cJSON_IsNumber(v) ? (long)v->valuedouble : strtol(v->valuestring, NULL, 0);
                uint16_t key = devdata_ep_key(s->attribute, (uint8_t)(ep > 0 && ep < 256 ? ep : 0));
                if (key != s->attribute) {
                    s->endpoint = (uint8_t)ep;
                    s->attribute = key;
                }
            }

            // ManufacturerSpecific
            v = cJSON_GetObjectItem(state_obj, "manufacturerSpecific");
            if (v) {
                if (cJSON_IsString(v)) {
                    s->manufacturer_code = (uint16_t)strtoul(v->valuestring, NULL, 16);
                } else {
                    s->manufacturer_code = (uint16_t)v->valuedouble;
                }
            }

            v = cJSON_GetObjectItem(state_obj, "mode");
            if (v && v->valuestring) strncpy(s->mode, v->valuestring, sizeof(s->mode) - 1);

            v = cJSON_GetObjectItem(state_obj, "type");
            if (v && v->valuestring) strncpy(s->type, v->valuestring, sizeof(s->type) - 1);

            v = cJSON_GetObjectItem(state_obj, "coefficient");
            if (v) s->coefficient = (float)v->valuedouble;

            v = cJSON_GetObjectItem(state_obj, "unit");
            if (v && v->valuestring) strncpy(s->unit, v->valuestring, sizeof(s->unit) - 1);

            v = cJSON_GetObjectItem(state_obj, "visible");
            if (v) s->visible = cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valuedouble != 0);

            v = cJSON_GetObjectItem(state_obj, "writable");
            if (v) s->writable = cJSON_IsTrue(v);

            v = cJSON_GetObjectItem(state_obj, "typewritable");
            if (v) s->type_writable = (int)v->valuedouble;

            // MQTT metadata
            v = cJSON_GetObjectItem(state_obj, "mqtt_device_class");
            if (v && v->valuestring) strncpy(s->mqtt_device_class, v->valuestring, sizeof(s->mqtt_device_class) - 1);

            v = cJSON_GetObjectItem(state_obj, "mqtt_state_class");
            if (v && v->valuestring) strncpy(s->mqtt_state_class, v->valuestring, sizeof(s->mqtt_state_class) - 1);

            v = cJSON_GetObjectItem(state_obj, "mqtt_icon");
            if (v && v->valuestring) strncpy(s->mqtt_icon, v->valuestring, sizeof(s->mqtt_icon) - 1);

            // Jauge
            v = cJSON_GetObjectItem(state_obj, "jauge");
            if (v && v->valuestring) strncpy(s->type_jauge, v->valuestring, sizeof(s->type_jauge) - 1);

            v = cJSON_GetObjectItem(state_obj, "min");
            if (v) s->jauge_min = (int)v->valuedouble;

            v = cJSON_GetObjectItem(state_obj, "max");
            if (v) s->jauge_max = (int)v->valuedouble;

            // Periode de scrutation, en minutes (cycle de 60 s de l'Arduino)
            v = cJSON_GetObjectItem(state_obj, "poll");
            if (v && cJSON_IsNumber(v) && v->valuedouble > 0) {
                s->poll = (uint16_t)v->valuedouble;
            }

            tmpl->state_count++;
        }
    }

    // Parser les "action"
    tmpl->action_count = 0;
    cJSON *action_arr = cJSON_GetObjectItem(model_obj, "action");
    if (action_arr && cJSON_IsArray(action_arr)) {
        cJSON *act_obj;
        cJSON_ArrayForEach(act_obj, action_arr) {
            if (tmpl->action_count >= DEVDATA_MAX_TEMPLATE_ACTIONS) break;

            devdata_template_action_t *a = &tmpl->actions[tmpl->action_count];
            memset(a, 0, sizeof(devdata_template_action_t));
            a->visible = true;

            cJSON *v;
            v = cJSON_GetObjectItem(act_obj, "name");
            if (v && v->valuestring) strncpy(a->name, v->valuestring, sizeof(a->name) - 1);

            v = cJSON_GetObjectItem(act_obj, "command");
            if (v) a->command = (uint16_t)v->valuedouble;

            v = cJSON_GetObjectItem(act_obj, "endpoint");
            if (v) a->endpoint = (uint8_t)v->valuedouble;

            v = cJSON_GetObjectItem(act_obj, "value");
            if (v) a->value = (int32_t)v->valuedouble;

            v = cJSON_GetObjectItem(act_obj, "visible");
            if (v) a->visible = cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valuedouble != 0);

            v = cJSON_GetObjectItem(act_obj, "cluster");
            if (v) {
                if (cJSON_IsString(v)) {
                    a->cluster = (uint16_t)strtoul(v->valuestring, NULL, 16);
                } else {
                    a->cluster = (uint16_t)v->valuedouble;
                }
            }

            v = cJSON_GetObjectItem(act_obj, "manufacturerSpecific");
            if (v) {
                if (cJSON_IsString(v)) {
                    a->manufacturer_code = (uint16_t)strtoul(v->valuestring, NULL, 16);
                } else {
                    a->manufacturer_code = (uint16_t)v->valuedouble;
                }
            }

            tmpl->action_count++;
        }
    }

    // "bind" : clusters a lier au coordinateur, en DECIMAL, separes par ';'
    // (',' et espace toleres, comme l'Arduino). Ex : "1794;2820;65382".
    tmpl->bind_count = 0;
    cJSON *bind = cJSON_GetObjectItem(model_obj, "bind");
    if (bind && cJSON_IsString(bind) && bind->valuestring) {
        const char *p = bind->valuestring;
        while (*p && tmpl->bind_count < DEVDATA_MAX_TEMPLATE_BINDS) {
            while (*p == ';' || *p == ',' || *p == ' ') p++;
            if (!*p) break;
            char *end;
            unsigned long c = strtoul(p, &end, 10);
            if (end == p) break;
            if (c > 0 && c <= 0xFFFF) tmpl->bind_clusters[tmpl->bind_count++] = (uint16_t)c;
            p = end;
        }
    } else if (bind && cJSON_IsNumber(bind)) {
        tmpl->bind_clusters[tmpl->bind_count++] = (uint16_t)bind->valuedouble;
    }

    // "report" : configuration du reporting
    tmpl->report_count = 0;
    cJSON *report_arr = cJSON_GetObjectItem(model_obj, "report");
    if (report_arr && cJSON_IsArray(report_arr)) {
        cJSON *r;
        cJSON_ArrayForEach(r, report_arr) {
            if (tmpl->report_count >= DEVDATA_MAX_TEMPLATE_REPORTS) {
                ESP_LOGW(TAG, "Template: plus de %d entrees report, suivantes ignorees",
                         DEVDATA_MAX_TEMPLATE_REPORTS);
                break;
            }
            cJSON *c  = cJSON_GetObjectItem(r, "cluster");
            cJSON *a  = cJSON_GetObjectItem(r, "attribut");
            cJSON *t  = cJSON_GetObjectItem(r, "type");
            cJSON *mn = cJSON_GetObjectItem(r, "min");
            cJSON *mx = cJSON_GetObjectItem(r, "max");
            cJSON *ch = cJSON_GetObjectItem(r, "change");
            // Champs obligatoires, comme l'Arduino
            if (!c || !a || !t || !mn || !mx || !ch) continue;

            devdata_template_report_t *e = &tmpl->reports[tmpl->report_count];
            memset(e, 0, sizeof(*e));
            e->cluster = cJSON_IsString(c) ? (uint16_t)strtoul(c->valuestring, NULL, 16)
                                           : (uint16_t)c->valuedouble;
            e->attribute    = (uint16_t)a->valuedouble;
            e->type         = (uint8_t)t->valuedouble;
            e->min_interval = (uint16_t)mn->valuedouble;
            e->max_interval = (uint16_t)mx->valuedouble;
            e->change       = (uint32_t)ch->valuedouble;

            cJSON *to = cJSON_GetObjectItem(r, "timeout");
            if (to && cJSON_IsNumber(to)) e->timeout = (uint16_t)to->valuedouble;

            cJSON *mf = cJSON_GetObjectItem(r, "manufacturerSpecific");
            if (mf) {
                e->manufacturer_code = cJSON_IsString(mf) ? (uint16_t)strtoul(mf->valuestring, NULL, 16)
                                                          : (uint16_t)mf->valuedouble;
            }

            // "mode" : modes Linky concernes, separes par ';' (ex: "1;3;5;7").
            // Absent ou vide : l'entree vaut pour tous les modes.
            cJSON *md = cJSON_GetObjectItem(r, "mode");
            if (md && cJSON_IsString(md) && md->valuestring) {
                const char *p = md->valuestring;
                while (*p) {
                    while (*p == ';' || *p == ',' || *p == ' ') p++;
                    if (!*p) break;
                    char *end;
                    long m = strtol(p, &end, 10);
                    if (end == p) break;
                    if (m >= 0 && m <= 7) e->mode_mask |= (uint8_t)(1u << m);
                    p = end;
                }
            }

            tmpl->report_count++;
        }
    }

    tmpl->loaded = true;
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Template loaded: %d states, %d actions, %d bind, %d report",
             tmpl->state_count, tmpl->action_count, tmpl->bind_count, tmpl->report_count);
    return ESP_OK;
}

// ============================================================================
// API publique - Gestion globale
// ============================================================================

esp_err_t device_data_init(void)
{
    if (s_initialized) return ESP_OK;

    // Verrou cree ici (ou deja cree par un appel anterieur a l'init). La table
    // n'est plus remise a zero : des entrees creees avant l'init seraient
    // perdues sans etre liberees.
    if (!s_mtx) s_mtx = xSemaphoreCreateRecursiveMutex();
    if (!s_mtx) return ESP_ERR_NO_MEM;

    // Creer le repertoire DB si necessaire
    struct stat st;
    if (stat(DEVDATA_DB_DIR, &st) != 0) {
        if (mkdir(DEVDATA_DB_DIR, 0755) != 0) {
            ESP_LOGW(TAG, "Cannot create " DEVDATA_DB_DIR ": %s", strerror(errno));
            // Non-fatal: le dir peut etre cree plus tard
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Device data manager initialized");
    return ESP_OK;
}

esp_err_t device_data_load_all(void)
{
    DIR *dir = opendir(DEVDATA_DB_DIR);
    if (!dir) {
        ESP_LOGI(TAG, "No device data directory, starting fresh");
        return ESP_OK;
    }

    int loaded = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Filtrer les fichiers .json
        const char *name = entry->d_name;
        size_t namelen = strlen(name);
        if (namelen < 6) continue; // Minimum: "X.json"
        if (strcmp(name + namelen - 5, ".json") != 0) continue;

        // Extraire l'adresse IEEE du nom de fichier (16 chars hex)
        if (namelen < 21) continue; // "0011223344556677.json" = 21 chars
        char ieee_str[17];
        strncpy(ieee_str, name, 16);
        ieee_str[16] = '\0';

        uint64_t ieee_addr = strtoull(ieee_str, NULL, 16);
        if (ieee_addr == 0) continue;

        // Charger le fichier
        char filepath[64];
        make_filepath(ieee_addr, filepath, sizeof(filepath));

        size_t fsize = 0;
        char *json = read_file(filepath, &fsize);
        if (!json) continue;

        dd_lock();
        device_data_t *dd = device_data_get_or_create(ieee_addr);
        if (dd) {
            json_to_device(dd, json);
            dd->dirty = false;
            loaded++;
        }
        dd_unlock();

        free(json);
    }
    closedir(dir);

    ESP_LOGI(TAG, "Loaded %d device data files from " DEVDATA_DB_DIR, loaded);
    return ESP_OK;
}

esp_err_t device_data_reload(uint64_t ieee_addr)
{
    char filepath[64];
    make_filepath(ieee_addr, filepath, sizeof(filepath));
    size_t fsize = 0;
    char *json = read_file(filepath, &fsize);
    if (!json) return ESP_ERR_NOT_FOUND;
    dd_lock();
    device_data_t *dd = device_data_get_or_create(ieee_addr);
    if (dd) {
        json_to_device(dd, json);
        dd->dirty = false;
    }
    dd_unlock();
    free(json);
    return dd ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t device_data_save_all(void)
{
    int saved = 0;
    for (int i = 0; i < DEVDATA_MAX_DEVICES; i++) {
        // Adresse relue sous verrou : le slot peut etre libere entre-temps
        dd_lock();
        uint64_t ieee = (slot_used(i) && s_devices[i]->dirty) ? s_devices[i]->ieee_addr : 0;
        dd_unlock();
        if (!ieee) continue;

        esp_err_t ret = device_data_save(ieee);
        if (ret == ESP_OK) saved++;
    }

    if (saved > 0) {
        ESP_LOGI(TAG, "Saved %d dirty device data files", saved);
    }
    return ESP_OK;
}

esp_err_t device_data_save(uint64_t ieee_addr)
{
    // Construction du JSON sous verrou (aucune suppression ni modification
    // concurrente), ecriture du fichier hors verrou (lente : ne pas bloquer
    // la tache ZiGate pendant ce temps).
    dd_lock();
    int slot = find_slot_locked(ieee_addr);
    if (slot < 0) { dd_unlock(); return ESP_ERR_NOT_FOUND; }
    device_data_t *dd = s_devices[slot];
    uint32_t gen = s_slot_gen[slot];

    cJSON *root = device_to_json(dd);
    char *json_str = root ? cJSON_PrintUnformatted(root) : NULL;
    if (root) cJSON_Delete(root);
    // Drapeau efface AVANT l'ecriture : une valeur recue pendant l'ecriture
    // remet 'dirty' et sera sauvegardee au tour suivant (l'effacer apres
    // perdait cette mise a jour).
    if (json_str) dd->dirty = false;
    dd_unlock();
    if (!json_str) return ESP_ERR_NO_MEM;

    char filepath[64];
    make_filepath(ieee_addr, filepath, sizeof(filepath));

    esp_err_t ret = write_file(filepath, json_str, strlen(json_str));
    free(json_str);

    // L'appareil a-t-il ete supprime (ou le slot reutilise) pendant l'ecriture ?
    // Si oui, le fichier qu'on vient d'ecrire recreerait l'appareil au
    // prochain demarrage : on l'efface. Controle et effacement sous verrou :
    // une suppression posterieure efface elle-meme le fichier.
    dd_lock();
    bool same = (s_slot_gen[slot] == gen && s_devices[slot] == dd && dd->ieee_addr == ieee_addr);
    if (!same) {
        if (ret == ESP_OK) unlink(filepath);
        ret = ESP_ERR_NOT_FOUND;
    } else if (ret != ESP_OK) {
        dd->dirty = true;                       // a refaire au prochain tour
    }
    dd_unlock();

    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Saved device 0x%016llX", (unsigned long long)ieee_addr);
    }
    return ret;
}

// ============================================================================
// API publique - Acces aux devices
// ============================================================================

device_data_t* device_data_get_or_create(uint64_t ieee_addr)
{
    if (ieee_addr == 0) return NULL;            // 0 reserve aux slots libres

    dd_lock();
    // Chercher existant
    int slot = find_slot_locked(ieee_addr);
    if (slot >= 0) {
        device_data_t *found = s_devices[slot];
        dd_unlock();
        return found;
    }

    // Slot libre : de preference une structure deja allouee (liberee par une
    // suppression), sinon une nouvelle allocation
    int free_slot = -1;
    for (int i = 0; i < DEVDATA_MAX_DEVICES; i++) {
        if (s_devices[i] && s_devices[i]->ieee_addr == 0) { free_slot = i; break; }
    }
    if (free_slot < 0) {
        for (int i = 0; i < DEVDATA_MAX_DEVICES; i++) {
            if (!s_devices[i]) {
                device_data_t *nd = alloc_device_data();
                if (!nd) {
                    ESP_LOGE(TAG, "Cannot allocate device_data (PSRAM OOM?)");
                    dd_unlock();
                    return NULL;
                }
                s_devices[i] = nd;              // ieee_addr = 0 : encore libre
                free_slot = i;
                break;
            }
        }
    }
    if (free_slot < 0) {
        ESP_LOGE(TAG, "Device data table full (%d/%d)", s_device_count, DEVDATA_MAX_DEVICES);
        dd_unlock();
        return NULL;
    }

    // Repartir d'un contenu vide : une ecriture tardive d'une tache qui
    // tenait l'ancien pointeur a pu y laisser des valeurs
    clear_slot_locked(free_slot);
    device_data_t *dd = s_devices[free_slot];

    // Valeurs par defaut
    strncpy(dd->info.linky_mode, "0", sizeof(dd->info.linky_mode) - 1);
    strncpy(dd->info.power_socket, "0", sizeof(dd->info.power_socket) - 1);
    strncpy(dd->info.status, "online", sizeof(dd->info.status) - 1);
    dd->dirty = true;
    dd->ieee_addr = ieee_addr;                  // publie en dernier

    s_device_count++;
    ESP_LOGD(TAG, "Created device_data for 0x%016llX in PSRAM (count=%d)",
             (unsigned long long)ieee_addr, s_device_count);
    dd_unlock();
    return dd;
}

device_data_t* device_data_find(uint64_t ieee_addr)
{
    dd_lock();
    int slot = find_slot_locked(ieee_addr);
    device_data_t *dd = slot >= 0 ? s_devices[slot] : NULL;
    dd_unlock();
    return dd;
}

esp_err_t device_data_remove(uint64_t ieee_addr)
{
    dd_lock();
    int slot = find_slot_locked(ieee_addr);
    if (slot < 0) { dd_unlock(); return ESP_ERR_NOT_FOUND; }
    // Slot marque libre, structure conservee (voir en tete de fichier)
    clear_slot_locked(slot);
    s_device_count--;

    // Fichier efface sous verrou : une sauvegarde concurrente controle la
    // generation du slot sous ce meme verrou apres son ecriture, et efface
    // elle-meme un fichier ecrit apres cette suppression.
    char filepath[64];
    make_filepath(ieee_addr, filepath, sizeof(filepath));
    unlink(filepath);
    dd_unlock();

    ESP_LOGI(TAG, "Removed device_data for 0x%016llX", (unsigned long long)ieee_addr);
    return ESP_OK;
}

int device_data_count(void)
{
    return s_device_count;
}

device_data_t* device_data_get_by_index(int index)
{
    dd_lock();
    int found = 0;
    device_data_t *res = NULL;
    for (int i = 0; i < DEVDATA_MAX_DEVICES; i++) {
        if (slot_used(i)) {
            if (found == index) { res = s_devices[i]; break; }
            found++;
        }
    }
    dd_unlock();
    return res;
}

// ============================================================================
// API publique - Valeurs cluster/attribut
// ============================================================================

esp_err_t device_data_set_value(uint64_t ieee_addr, uint16_t cluster_id,
                                 uint16_t attr_id, const char *value)
{
    // Sous verrou : l'ajout d'un cluster / attribut ne doit pas croiser une
    // suppression (slot vide puis reutilise) ni la serialisation d'une sauvegarde
    dd_lock();
    device_data_t *dd = device_data_get_or_create(ieee_addr);
    if (!dd) { dd_unlock(); return ESP_ERR_NO_MEM; }

    devdata_cluster_t *cl = find_or_create_cluster(dd, cluster_id);
    if (!cl) { dd_unlock(); return ESP_ERR_NO_MEM; }

    devdata_attr_t *attr = find_or_create_attr(cl, attr_id);
    if (!attr) { dd_unlock(); return ESP_ERR_NO_MEM; }

    strncpy(attr->value, value, DEVDATA_VALUE_LEN - 1);
    attr->value[DEVDATA_VALUE_LEN - 1] = '\0';

    // Timestamp
    extern uint32_t esp_log_timestamp(void);
    attr->last_update = esp_log_timestamp() / 1000; // En secondes

    dd->dirty = true;
    dd_unlock();
    return ESP_OK;
}

esp_err_t device_data_get_value(uint64_t ieee_addr, uint16_t cluster_id,
                                 uint16_t attr_id, char *out_value, size_t out_len)
{
    dd_lock();                                  // copie coherente de la valeur
    const char *ptr = device_data_get_value_ptr(ieee_addr, cluster_id, attr_id);
    if (!ptr) { dd_unlock(); return ESP_ERR_NOT_FOUND; }

    strncpy(out_value, ptr, out_len - 1);
    out_value[out_len - 1] = '\0';
    dd_unlock();
    return ESP_OK;
}

esp_err_t device_data_set_value_typed(uint64_t ieee_addr, uint16_t cluster_id,
                                       uint16_t attr_id, const char *value, uint8_t zcl_type)
{
    dd_lock();                                  // valeur et type ensemble
    esp_err_t err = device_data_set_value(ieee_addr, cluster_id, attr_id, value);
    if (err != ESP_OK || !zcl_type) { dd_unlock(); return err; }
    device_data_t *dd = device_data_find(ieee_addr);
    devdata_cluster_t *cl = dd ? find_cluster(dd, cluster_id) : NULL;
    devdata_attr_t *attr = cl ? find_attr(cl, attr_id) : NULL;
    if (attr) attr->type = zcl_type;
    dd_unlock();
    return ESP_OK;
}

const devdata_attr_t* device_data_get_attr(uint64_t ieee_addr, uint16_t cluster_id,
                                            uint16_t attr_id)
{
    device_data_t *dd = device_data_find(ieee_addr);
    if (!dd) return NULL;
    devdata_cluster_t *cl = find_cluster(dd, cluster_id);
    if (!cl) return NULL;
    return find_attr(cl, attr_id);
}

const char* device_data_get_value_ptr(uint64_t ieee_addr, uint16_t cluster_id,
                                       uint16_t attr_id)
{
    device_data_t *dd = device_data_find(ieee_addr);
    if (!dd) return NULL;

    devdata_cluster_t *cl = find_cluster(dd, cluster_id);
    if (!cl) return NULL;

    devdata_attr_t *attr = find_attr(cl, attr_id);
    if (!attr) return NULL;

    return attr->value;
}

// ============================================================================
// API publique - Poll list
// ============================================================================

esp_err_t device_data_add_poll_item(uint64_t ieee_addr, uint16_t cluster_id,
                                     uint16_t attr_id, uint32_t interval_ms,
                                     uint16_t manufacturer_code)
{
    dd_lock();
    device_data_t *dd = device_data_get_or_create(ieee_addr);
    if (!dd) { dd_unlock(); return ESP_ERR_NO_MEM; }

    // Verifier si deja present
    for (int i = 0; i < dd->poll_count; i++) {
        if (dd->poll_list[i].cluster_id == cluster_id &&
            dd->poll_list[i].attr_id == attr_id) {
            // Mettre a jour l'intervalle
            dd->poll_list[i].interval_ms = interval_ms;
            dd->poll_list[i].manufacturer_code = manufacturer_code;
            dd->dirty = true;
            dd_unlock();
            return ESP_OK;
        }
    }

    if (dd->poll_count >= DEVDATA_MAX_POLL_ITEMS) {
        dd_unlock();
        return ESP_ERR_NO_MEM;
    }

    // Element rempli avant d'etre compte (lecteurs sans verrou du poll_engine)
    devdata_poll_item_t *pi = &dd->poll_list[dd->poll_count];
    pi->cluster_id = cluster_id;
    pi->attr_id = attr_id;
    pi->interval_ms = interval_ms;
    pi->last_poll = 0;
    pi->manufacturer_code = manufacturer_code;
    dd->poll_count++;

    dd->dirty = true;
    dd_unlock();
    return ESP_OK;
}

const devdata_poll_item_t* device_data_get_poll_list(uint64_t ieee_addr, int *out_count)
{
    device_data_t *dd = device_data_find(ieee_addr);
    if (!dd) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    if (out_count) *out_count = dd->poll_count;
    return dd->poll_list;
}

esp_err_t device_data_update_poll_timestamp(uint64_t ieee_addr, uint16_t cluster_id,
                                             uint16_t attr_id, uint32_t timestamp)
{
    dd_lock();
    device_data_t *dd = device_data_find(ieee_addr);
    if (!dd) { dd_unlock(); return ESP_ERR_NOT_FOUND; }

    for (int i = 0; i < dd->poll_count; i++) {
        if (dd->poll_list[i].cluster_id == cluster_id &&
            dd->poll_list[i].attr_id == attr_id) {
            dd->poll_list[i].last_poll = timestamp;
            dd_unlock();
            return ESP_OK;
        }
    }
    dd_unlock();
    return ESP_ERR_NOT_FOUND;
}

// ============================================================================
// API publique - Templates
// ============================================================================

esp_err_t device_data_load_template(uint64_t ieee_addr,
                                     const char *device_id_file,
                                     const char *model_name)
{
    if (!device_data_find(ieee_addr)) return ESP_ERR_NOT_FOUND;

    // Construire le chemin du template
    char filepath[80];
    snprintf(filepath, sizeof(filepath), DEVDATA_TP_DIR "/%s", device_id_file);

    // Lecture du fichier hors verrou (acces flash)
    size_t fsize = 0;
    char *json = read_file(filepath, &fsize);
    if (!json) {
        ESP_LOGD(TAG, "Template file not found: %s", filepath);
        // Sans fichier : actions dynamiques seules (telecommande IR sans template)
        dd_lock();
        int s = find_slot_locked(ieee_addr);
        if (s >= 0) apply_dyn_locked(s_devices[s]);
        dd_unlock();
        return ESP_ERR_NOT_FOUND;
    }

    // Appareil recherche de nouveau sous verrou : il a pu etre supprime
    // pendant la lecture du fichier
    dd_lock();
    int slot = find_slot_locked(ieee_addr);
    if (slot < 0) {
        dd_unlock();
        free(json);
        return ESP_ERR_NOT_FOUND;
    }
    device_data_t *dd = s_devices[slot];

    // Allouer ou reutiliser le template (y compris celui d'un appareil
    // supprime de ce slot : jamais libere, voir clear_slot_locked)
    if (!dd->tmpl) {
        // ~17 Ko : en PSRAM de preference, la RAM interne est rare
        dd->tmpl = (devdata_template_t *)heap_caps_calloc(1, sizeof(devdata_template_t), MALLOC_CAP_SPIRAM);
        if (!dd->tmpl) dd->tmpl = (devdata_template_t *)calloc(1, sizeof(devdata_template_t));
        if (!dd->tmpl) {
            dd_unlock();
            free(json);
            return ESP_ERR_NO_MEM;
        }
    } else {
        memset(dd->tmpl, 0, sizeof(devdata_template_t));
    }

    esp_err_t ret = parse_template_json(dd->tmpl, json, model_name ? model_name : "");
    if (ret == ESP_OK) {
        dd->tmpl->base_action_count = dd->tmpl->action_count;
        dd->tmpl->dyn_only = false;
    }
    apply_dyn_locked(dd);
    dd_unlock();
    free(json);

    return ret;
}

esp_err_t device_data_set_dynamic_actions(uint64_t ieee_addr, const devdata_template_action_t *actions, int n)
{
    if (n < 0 || (n > 0 && !actions)) return ESP_ERR_INVALID_ARG;
    if (n > DEVDATA_MAX_DYN_ACTIONS) n = DEVDATA_MAX_DYN_ACTIONS;
    dd_lock();
    dyn_entry_t *e = dyn_find_locked(ieee_addr, n > 0);
    if (!e) {
        dd_unlock();
        return n > 0 ? ESP_ERR_NO_MEM : ESP_OK;
    }
    e->n = 0;
    for (int k = 0; k < n; k++) e->a[k] = actions[k];
    e->n = n;
    if (n == 0) e->ieee = 0;                 // entree liberee
    int slot = find_slot_locked(ieee_addr);
    if (slot >= 0) {
        if (n == 0) {
            // Retrait : le template revient a ses seules actions de fichier
            device_data_t *dd = s_devices[slot];
            if (dd->tmpl) {
                if (dd->tmpl->dyn_only) {
                    dd->tmpl->action_count = 0;
                    dd->tmpl->dyn_only = false;
                    dd->tmpl->loaded = false;
                } else if (dd->tmpl->loaded) {
                    dd->tmpl->action_count = dd->tmpl->base_action_count;
                }
            }
        } else {
            apply_dyn_locked(s_devices[slot]);
        }
    }
    dd_unlock();
    return ESP_OK;
}

const devdata_template_t* device_data_get_template(uint64_t ieee_addr)
{
    device_data_t *dd = device_data_find(ieee_addr);
    if (!dd) return NULL;
    return dd->tmpl;
}

uint16_t device_data_storage_attr(uint64_t ieee_addr, uint16_t cluster, uint16_t attr, uint8_t ep)
{
    uint16_t key = devdata_ep_key(attr, ep);
    if (key == attr) return attr;
    uint16_t r = attr;
    dd_lock();
    device_data_t *dd = device_data_find(ieee_addr);
    const devdata_template_t *t = dd ? dd->tmpl : NULL;
    for (int i = 0; t && i < t->state_count; i++) {
        if (t->states[i].cluster == cluster && t->states[i].endpoint == ep && t->states[i].attribute == key) {
            r = key;
            break;
        }
    }
    dd_unlock();
    return r;
}

uint16_t device_data_resolve_key(uint64_t ieee_addr, uint16_t cluster, uint16_t key, uint8_t *ep)
{
    if (ep) *ep = 0;
    if (key < 0x2000) return key;                // jamais composee (endpoint >= 2)
    uint16_t r = key;
    dd_lock();
    device_data_t *dd = device_data_find(ieee_addr);
    const devdata_template_t *t = dd ? dd->tmpl : NULL;
    for (int i = 0; t && i < t->state_count; i++) {
        const devdata_template_state_t *s = &t->states[i];
        if (s->endpoint && s->cluster == cluster && s->attribute == key) {
            if (ep) *ep = s->endpoint;
            r = key & 0x0FFF;
            break;
        }
    }
    dd_unlock();
    return r;
}

float device_data_get_coefficient(uint64_t ieee_addr, uint16_t cluster_id, uint16_t attr_id)
{
    device_data_t *dd = device_data_find(ieee_addr);
    if (!dd || !dd->tmpl) return 1.0f;

    for (int i = 0; i < dd->tmpl->state_count; i++) {
        devdata_template_state_t *s = &dd->tmpl->states[i];
        if (s->cluster == cluster_id && s->attribute == attr_id) {
            return s->coefficient;
        }
    }

    return 1.0f;
}

// ============================================================================
// API publique - Serialisation JSON (pour web API)
// ============================================================================

char* device_data_to_json(uint64_t ieee_addr)
{
    dd_lock();                                  // serialisation coherente
    device_data_t *dd = device_data_find(ieee_addr);
    cJSON *root = dd ? device_to_json(dd) : NULL;
    dd_unlock();
    if (!root) return NULL;

    // Ajouter l'adresse IEEE pour le contexte
    char ieee_str[20];
    snprintf(ieee_str, sizeof(ieee_str), "%016llX", (unsigned long long)ieee_addr);
    cJSON_AddStringToObject(root, "ieee", ieee_str);

    char *result = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return result;
}

char* device_data_all_to_json(void)
{
    cJSON *arr = cJSON_CreateArray();
    if (!arr) return NULL;

    dd_lock();
    for (int i = 0; i < DEVDATA_MAX_DEVICES; i++) {
        if (!slot_used(i)) continue;            // slot libre (appareil supprime)

        device_data_t *dd = s_devices[i];
        cJSON *obj = device_to_json(dd);
        if (!obj) continue;

        char ieee_str[20];
        snprintf(ieee_str, sizeof(ieee_str), "%016llX", (unsigned long long)dd->ieee_addr);
        cJSON_AddStringToObject(obj, "ieee", ieee_str);

        cJSON_AddItemToArray(arr, obj);
    }
    dd_unlock();

    char *result = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return result;
}

char* device_data_cluster_to_json(uint64_t ieee_addr, uint16_t cluster_id)
{
    dd_lock();
    device_data_t *dd = device_data_find(ieee_addr);
    devdata_cluster_t *cl = dd ? find_cluster(dd, cluster_id) : NULL;
    cJSON *obj = cl ? cJSON_CreateObject() : NULL;
    if (!obj) { dd_unlock(); return NULL; }

    for (int j = 0; j < cl->attr_count; j++) {
        devdata_attr_t *attr = &cl->attrs[j];
        char attr_key[8];
        snprintf(attr_key, sizeof(attr_key), "%d", attr->attr_id);
        cJSON_AddStringToObject(obj, attr_key, attr->value);
    }
    dd_unlock();

    char *result = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return result;
}

// ============================================================================
// API publique - Formatage des valeurs (portage de GetValueStatus de la
// LiXee-Box, protocol.cpp)
//
// Difference avec la LiXee-Box : celle-ci stockait les entiers signes deja
// etendus sur 32 bits ("FFFFFFE4") et les chaines en clair. Ici la valeur
// est l'image hexa exacte des octets recus : le signe et le decodage des
// chaines s'appuient donc sur le type ZCL memorise avec la valeur.
// ============================================================================

static bool zcl_type_is_signed_int(uint8_t t)
{
    return t >= 0x28 && t <= 0x2F;
}

static bool zcl_type_is_char_string(uint8_t t)
{
    return t == 0x42 || t == 0x44;           // chaine de caracteres (courte, longue)
}

/**
 * Largeur des attributs signes usuels, pour une valeur sans type connu
 * (recue avant que le type soit memorise). 0 = non signe.
 */
static int known_signed_width(uint16_t cl, uint16_t at)
{
    switch (cl) {
    case 0x0402:                             // temperature : mesure, min, max
    case 0x0403:                             // pression
        return at <= 0x0002 ? 2 : 0;
    case 0x0201:                             // thermostat
        if (at == 0x0010) return 1;          // calibration (int8)
        if (at == 0x0000 || (at >= 0x0011 && at <= 0x0014)) return 2;   // temperature, consignes
        return 0;
    case 0x0B04:                             // puissances actives / reactives (phases A, B, C)
        if (at == 0x050B || at == 0x050E || at == 0x090B || at == 0x090E ||
            at == 0x0A0B || at == 0x0A0E) return 2;
        if (at == 0x0510) return 1;          // facteur de puissance
        return 0;
    case 0x0702:
        return at == 0x0400 ? 3 : 0;         // InstantaneousDemand (int24)
    default:
        return 0;
    }
}

static bool is_hex_string(const char *s, size_t *len)
{
    size_t n = 0;
    for (; s[n]; n++) {
        char c = s[n];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) return false;
    }
    if (len) *len = n;
    return n > 0;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool device_data_hex_to_int64(const char *hex, uint8_t zcl_type,
                              uint16_t cluster_id, uint16_t attr_id, int64_t *out)
{
    size_t n = 0;
    if (!hex || !out || !is_hex_string(hex, &n) || n > 16) return false;

    uint64_t u = strtoull(hex, NULL, 16);
    int sw = 0;
    if (zcl_type_is_signed_int(zcl_type))  sw = zcl_type - 0x27;
    else if (zcl_type == 0)                sw = known_signed_width(cluster_id, attr_id);

    if (sw > 0 && sw < 8) {
        uint64_t mask = ((uint64_t)1 << (8 * sw)) - 1;
        uint64_t sign = (uint64_t)1 << (8 * sw - 1);
        u &= mask;
        if (u & sign) u |= ~mask;            // extension de signe
    }
    *out = (int64_t)u;
    return true;
}

/** Valeur hexa d'au moins 3 octets tous imprimables : probablement du texte. */
static bool hex_looks_like_text(const char *hex, size_t n)
{
    if (n < 6 || (n & 1)) return false;
    for (size_t i = 0; i < n; i += 2) {
        int b = (hex_nibble(hex[i]) << 4) | hex_nibble(hex[i + 1]);
        if (b == 0 && i >= 6) break;         // terminateur apres du texte
        if (b < 0x20 || b > 0x7E) return false;
    }
    return true;
}

static size_t hex_to_text(const char *hex, size_t n, char *out, size_t out_len)
{
    size_t o = 0;
    for (size_t i = 0; i + 1 < n && o + 1 < out_len; i += 2) {
        int b = (hex_nibble(hex[i]) << 4) | hex_nibble(hex[i + 1]);
        if (b == 0) break;
        if (o == 0 && b == ' ') continue;    // LTARF du ZLinky cadre a droite : "  Heure pleine"
        out[o++] = (b >= 0x20 && b <= 0x7E) ? (char)b : '?';
    }
    while (o > 0 && out[o - 1] == ' ') o--;  // champs TIC completes par des espaces
    out[o] = '\0';
    return o;
}

bool device_data_stge_value(const char *hex, uint32_t *out)
{
    size_t n = 0;
    if (!hex || !out || !is_hex_string(hex, &n) || n == 0) return false;
    if (n <= 8) {                            // deja numerique
        *out = (uint32_t)strtoul(hex, NULL, 16);
        return true;
    }
    char txt[12];
    size_t len = hex_to_text(hex, n, txt, sizeof(txt));
    if (len == 0 || len > 8) return false;
    for (size_t i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)txt[i])) return false;
    }
    *out = (uint32_t)strtoul(txt, NULL, 16);
    return true;
}

size_t device_data_format_value(uint16_t cluster_id, uint16_t attr_id, const char *hex,
                                uint8_t zcl_type, const char *tmpl_type, float coefficient,
                                char *out, size_t out_len)
{
    if (!out || out_len == 0) return 0;
    out[0] = '\0';
    if (!hex || !hex[0]) return 0;

    size_t n = 0;
    bool hexok = is_hex_string(hex, &n);
    bool t_float   = tmpl_type && strcmp(tmpl_type, "float") == 0;
    bool t_numeric = tmpl_type && strcmp(tmpl_type, "numeric") == 0;

    if ((t_float || t_numeric) && hexok) {
        double v;
        bool decimals = t_float || coefficient != 1.0f;
        if (zcl_type == 0x39 && n == 8) {    // flottant simple ZCL
            uint32_t bits = (uint32_t)strtoul(hex, NULL, 16);
            float f;
            memcpy(&f, &bits, sizeof(f));
            v = f;
            decimals = true;
        } else {
            int64_t iv = 0;
            if (!device_data_hex_to_int64(hex, zcl_type, cluster_id, attr_id, &iv)) {
                strlcpy(out, hex, out_len);
                return strlen(out);
            }
            if (!decimals) {
                int r = snprintf(out, out_len, "%lld", (long long)iv);
                return r < 0 ? 0 : ((size_t)r < out_len ? (size_t)r : out_len - 1);
            }
            v = (double)iv;
        }
        if (coefficient != 1.0f) v *= (double)coefficient;
        if (isnan(v) || isinf(v)) { strlcpy(out, "-", out_len); return 1; }
        int r = snprintf(out, out_len, "%.2f", v);
        return r < 0 ? 0 : ((size_t)r < out_len ? (size_t)r : out_len - 1);
    }

    // Texte : chaine ZCL, ou valeur sans type (ou chaine d'octets) qui en a
    // l'apparence. Le ZLinky envoie STGE en chaine d'octets (0x41) de
    // caracteres ASCII : "000A0401" et non 3030304130343031.
    bool octets = (zcl_type == 0x41 || zcl_type == 0x43);
    if (hexok && (zcl_type_is_char_string(zcl_type) ||
                  ((zcl_type == 0 || octets) && hex_looks_like_text(hex, n)))) {
        return hex_to_text(hex, n, out, out_len);
    }

    // Sinon la valeur brute, comme la LiXee-Box
    strlcpy(out, hex, out_len);
    return strlen(out);
}

size_t device_data_format_state(uint64_t ieee_addr, const devdata_template_state_t *state,
                                char *out, size_t out_len)
{
    if (!state || !out || out_len == 0) return 0;
    const devdata_attr_t *a = device_data_get_attr(ieee_addr, state->cluster, state->attribute);
    if (!a) { out[0] = '\0'; return 0; }
    // Copie locale : la valeur peut etre reecrite par la tache ZiGate pendant le formatage
    char hex[DEVDATA_VALUE_LEN];
    strlcpy(hex, a->value, sizeof(hex));
    return device_data_format_value(state->cluster, state->attribute, hex, a->type,
                                    state->type, state->coefficient, out, out_len);
}

bool device_data_state_in_mode(const devdata_template_state_t *state, int linky_mode)
{
    if (!state || linky_mode < 0 || state->mode[0] == '\0') return true;
    const char *p = state->mode;
    while (*p) {
        while (*p == ';' || *p == ',' || *p == ' ') p++;
        if (!*p) break;
        char *end;
        long m = strtol(p, &end, 10);
        if (end == p) break;
        if (m == linky_mode) return true;
        p = end;
    }
    return false;
}

int device_data_linky_mode(uint64_t ieee_addr)
{
    device_data_t *dd = device_data_find(ieee_addr);
    if (!dd || dd->info.linky_mode[0] < '0' || dd->info.linky_mode[0] > '9') return -1;
    return atoi(dd->info.linky_mode);
}
