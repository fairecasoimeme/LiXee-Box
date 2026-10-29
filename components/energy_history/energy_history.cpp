// ============================================================================
// Energy History - Historique de consommation energetique
//
// Port de energyHistory + powerHistory Arduino vers ESP-IDF.
// Utilise des tableaux fixes (pas de std::map) et cJSON pour la persistence.
// ============================================================================

#include "energy_history.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>   // fsync, unlink
#include <time.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "ntp_sync.h"

static const char *TAG = "NRG_HST";

// ============================================================================
// Stockage: pointeurs en RAM, structures en PSRAM
// ============================================================================

#define MAX_HISTORY_DEVICES 16

typedef struct {
    uint64_t         ieee_addr;
    energy_history_t *energy;
    power_history_t  *power;
} history_entry_t;

static history_entry_t s_entries[MAX_HISTORY_DEVICES] = {};
static int s_entry_count = 0;
static bool s_initialized = false;

// Verrou de l'allocation des entrees. Correction : energy_history_get_or_create
// et power_history_get_or_create faisaient "idx = s_entry_count++" sans verrou ;
// appeles depuis deux taches (ZiGate, LoRa, web), ils pouvaient prendre le
// meme slot ou allouer deux fois la meme structure. Cree a la premiere
// utilisation (appel possible avant energy_history_init, au demarrage, quand
// une seule tache tourne).
static SemaphoreHandle_t s_entries_mtx = NULL;

static void entries_lock(void)
{
    if (!s_entries_mtx) s_entries_mtx = xSemaphoreCreateMutex();
    if (s_entries_mtx) xSemaphoreTake(s_entries_mtx, portMAX_DELAY);
}

static void entries_unlock(void)
{
    if (s_entries_mtx) xSemaphoreGive(s_entries_mtx);
}

// Verrou des DONNEES (mesures, sauvegarde, relecture). Cree par
// energy_history_init(), avant les taches qui ecrivent ; recursif.
static SemaphoreHandle_t s_data_mtx = NULL;
static void data_lock(void)   { if (s_data_mtx) xSemaphoreTakeRecursive(s_data_mtx, portMAX_DELAY); }
static void data_unlock(void) { if (s_data_mtx) xSemaphoreGiveRecursive(s_data_mtx); }

// ============================================================================
// Helpers internes
// ============================================================================

static int find_entry(uint64_t ieee_addr)
{
    for (int i = 0; i < s_entry_count; i++) {
        if (s_entries[i].ieee_addr == ieee_addr) return i;
    }
    return -1;
}

/** Trouve l'index d'un attribut dans la liste, ou l'ajoute */
static int find_or_add_attr(uint16_t *attr_ids, int *attr_count, uint16_t attr_id)
{
    for (int i = 0; i < *attr_count; i++) {
        if (attr_ids[i] == attr_id) return i;
    }
    if (*attr_count >= ENERGY_MAX_ATTRS) return -1;
    attr_ids[*attr_count] = attr_id;
    return (*attr_count)++;
}

static void make_energy_path(uint64_t ieee_addr, char *out, size_t len)
{
    snprintf(out, len, ENERGY_HST_DIR "/nrg_%016llX.json", (unsigned long long)ieee_addr);
}

static void make_power_path(uint64_t ieee_addr, char *out, size_t len)
{
    snprintf(out, len, ENERGY_HST_DIR "/pwr_%016llX.json", (unsigned long long)ieee_addr);
}

static char* read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc(fsize + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, fsize, f);
    fclose(f);
    buf[rd] = '\0';
    return buf;
}

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

// ============================================================================
// Allocation
// ============================================================================

static energy_history_t* alloc_energy_history(void)
{
    energy_history_t *h = (energy_history_t *)heap_caps_calloc(1, sizeof(energy_history_t), MALLOC_CAP_SPIRAM);
    if (!h) h = (energy_history_t *)calloc(1, sizeof(energy_history_t));
    if (h) {
        // calloc met a 0, ce qui designerait le slot 0 : -1 signifie
        // "aucune periode en cours", seul etat correct avant la 1re mesure.
        h->hours.ref_slot  = -1;
        h->days.ref_slot   = -1;
        h->months.ref_slot = -1;
        h->years.ref_slot  = -1;
    }
    return h;
}

static power_history_t* alloc_power_history(void)
{
    power_history_t *h = (power_history_t *)heap_caps_calloc(1, sizeof(power_history_t), MALLOC_CAP_SPIRAM);
    if (!h) h = (power_history_t *)calloc(1, sizeof(power_history_t));
    return h;
}

// ============================================================================
// Serialisation JSON - Energy History
// ============================================================================

// Generateurs de libelles de creneaux. Correction : ils ecrivaient dans un
// tampon statique partage (s_lbl) et lisaient l'annee de base dans une
// globale (s_base_year) : une sauvegarde (tache principale) et un chargement
// (tache ZiGate ou web) simultanes melangeaient leurs libelles, d'ou des
// creneaux ecrits sous la mauvaise cle. Chaque appelant fournit desormais son
// tampon local et l'annee de base de SON historique.
typedef void (*label_fn_t)(int i, int base_year, char *out, size_t len);
static void hour_label(int i, int, char *out, size_t len)  { snprintf(out, len, "%02d", (int)(i & 0xFF)); }
static void day_label(int i, int, char *out, size_t len)   { snprintf(out, len, "%02d", (int)((i + 1) & 0xFF)); }
static void month_label(int i, int, char *out, size_t len) { snprintf(out, len, "%02d", (int)((i + 1) & 0xFF)); }
static void year_label(int i, int base_year, char *out, size_t len) { snprintf(out, len, "%d", base_year + i); }

static cJSON* period_to_json(const energy_period_t *p, const uint16_t *attr_ids,
                              int attr_count, int slot_count, int base_year,
                              label_fn_t label_fn)
{
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return NULL;

    // trend
    cJSON *trend = cJSON_CreateObject();
    for (int a = 0; a < attr_count; a++) {
        char key[8]; snprintf(key, sizeof(key), "%d", attr_ids[a]);
        cJSON_AddNumberToObject(trend, key, p->trend.values[a]);
    }
    cJSON_AddItemToObject(obj, "trend", trend);

    // last
    cJSON *last = cJSON_CreateObject();
    for (int a = 0; a < attr_count; a++) {
        char key[8]; snprintf(key, sizeof(key), "%d", attr_ids[a]);
        cJSON_AddNumberToObject(last, key, p->last.values[a]);
    }
    cJSON_AddItemToObject(obj, "last", last);

    // ref : index absolu au debut de la periode courante.
    // C'est l'omission de cette reference qui faisait repartir les deltas de
    // zero a chaque redemarrage, en ecrasant le creneau en cours par 0.
    cJSON *ref = cJSON_CreateObject();
    for (int a = 0; a < attr_count; a++) {
        char key[8]; snprintf(key, sizeof(key), "%d", attr_ids[a]);
        cJSON_AddNumberToObject(ref, key, p->ref.values[a]);
    }
    cJSON_AddItemToObject(obj, "ref", ref);

    cJSON_AddNumberToObject(obj, "refSlot", p->ref_slot);
    cJSON_AddNumberToObject(obj, "refMask", p->ref_mask);
    cJSON_AddNumberToObject(obj, "lastMask", p->last_mask);

    // graph
    cJSON *graph = cJSON_CreateObject();
    for (int s = 0; s < slot_count; s++) {
        char lbl[16];
        label_fn(s, base_year, lbl, sizeof(lbl));
        bool has_data = false;
        for (int a = 0; a < attr_count; a++) {
            if (p->graph[s].values[a] != 0) { has_data = true; break; }
        }
        if (!has_data) continue;

        cJSON *slot = cJSON_CreateObject();
        for (int a = 0; a < attr_count; a++) {
            char key[8]; snprintf(key, sizeof(key), "%d", attr_ids[a]);
            cJSON_AddNumberToObject(slot, key, p->graph[s].values[a]);
        }
        cJSON_AddItemToObject(graph, lbl, slot);
    }
    cJSON_AddItemToObject(obj, "graph", graph);

    return obj;
}

static esp_err_t save_energy_history(energy_history_t *h)
{
    data_lock();
    cJSON *root = cJSON_CreateObject();
    if (!root) { data_unlock(); return ESP_ERR_NO_MEM; }

    cJSON_AddNumberToObject(root, "base_year", h->base_year);
    cJSON *lts = cJSON_AddObjectToObject(root, "lastTs");
    for (int i = 0; lts && i < h->attr_count; i++) {
        char key[8]; snprintf(key, sizeof(key), "%d", h->attr_ids[i]);
        cJSON_AddNumberToObject(lts, key, h->last_ts[i]);
    }

    // attr_ids
    cJSON *ids = cJSON_CreateArray();
    for (int i = 0; i < h->attr_count; i++) {
        cJSON_AddItemToArray(ids, cJSON_CreateNumber(h->attr_ids[i]));
    }
    cJSON_AddItemToObject(root, "attr_ids", ids);

    cJSON_AddItemToObject(root, "hours", period_to_json(&h->hours, h->attr_ids, h->attr_count, ENERGY_HOURS, h->base_year, hour_label));
    cJSON_AddItemToObject(root, "days", period_to_json(&h->days, h->attr_ids, h->attr_count, ENERGY_DAYS, h->base_year, day_label));
    cJSON_AddItemToObject(root, "months", period_to_json(&h->months, h->attr_ids, h->attr_count, ENERGY_MONTHS, h->base_year, month_label));
    cJSON_AddItemToObject(root, "years", period_to_json(&h->years, h->attr_ids, h->attr_count, ENERGY_YEARS, h->base_year, year_label));

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json) h->dirty = false;                  // une mesure arrivee pendant l'ecriture le remet
    data_unlock();
    if (!json) return ESP_ERR_NO_MEM;

    char path[64];
    make_energy_path(h->ieee_addr, path, sizeof(path));
    esp_err_t ret = write_file(path, json, strlen(json));
    free(json);

    if (ret != ESP_OK) h->dirty = true;          // nouvel essai a la prochaine sauvegarde
    return ret;
}

// ============================================================================
// Deserialisation JSON - Energy History
// ============================================================================

static void parse_slot(const cJSON *obj, energy_slot_t *slot,
                        const uint16_t *attr_ids, int attr_count)
{
    for (int a = 0; a < attr_count; a++) {
        char key[8]; snprintf(key, sizeof(key), "%d", attr_ids[a]);
        cJSON *v = cJSON_GetObjectItem(obj, key);
        if (v && cJSON_IsNumber(v)) {
            slot->values[a] = (int32_t)v->valuedouble;
        }
    }
}

static void parse_period(const cJSON *pobj, energy_period_t *p,
                          const uint16_t *attr_ids, int attr_count,
                          int slot_count, int base_year, label_fn_t label_fn)
{
    if (!pobj) return;

    cJSON *trend = cJSON_GetObjectItem(pobj, "trend");
    if (trend) parse_slot(trend, &p->trend, attr_ids, attr_count);

    cJSON *last = cJSON_GetObjectItem(pobj, "last");
    if (last) parse_slot(last, &p->last, attr_ids, attr_count);

    cJSON *ref = cJSON_GetObjectItem(pobj, "ref");
    if (ref) parse_slot(ref, &p->ref, attr_ids, attr_count);

    cJSON *rs = cJSON_GetObjectItem(pobj, "refSlot");
    p->ref_slot = (rs && cJSON_IsNumber(rs)) ? (int)rs->valuedouble : -1;
    if (p->ref_slot < 0 || p->ref_slot >= slot_count) p->ref_slot = -1;

    cJSON *rm = cJSON_GetObjectItem(pobj, "refMask");
    cJSON *lm = cJSON_GetObjectItem(pobj, "lastMask");
    p->ref_mask  = (rm && cJSON_IsNumber(rm)) ? (uint16_t)rm->valuedouble : 0;
    p->last_mask = (lm && cJSON_IsNumber(lm)) ? (uint16_t)lm->valuedouble : 0;

    // Fichier ecrit avant la persistance de la reference : 'last' y est present
    // mais pas les masques. On marque connus les attributs effectivement lus,
    // pour que period_update() puisse reconstituer la reference sans detruire
    // le graphique deja charge.
    if (!ref && last) {
        for (int a = 0; a < attr_count; a++) {
            char key[8]; snprintf(key, sizeof(key), "%d", attr_ids[a]);
            if (cJSON_GetObjectItem(last, key)) p->last_mask |= (uint16_t)(1u << a);
        }
        p->ref_slot = -1;
    }

    cJSON *graph = cJSON_GetObjectItem(pobj, "graph");
    if (graph) {
        for (int s = 0; s < slot_count; s++) {
            char lbl[16];
            label_fn(s, base_year, lbl, sizeof(lbl));
            cJSON *slot = cJSON_GetObjectItem(graph, lbl);
            if (slot) parse_slot(slot, &p->graph[s], attr_ids, attr_count);
        }
    }
}

static esp_err_t load_energy_history(energy_history_t *h)
{
    char path[64];
    make_energy_path(h->ieee_addr, path, sizeof(path));

    char *json = read_file(path);
    if (!json) return ESP_ERR_NOT_FOUND;

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) return ESP_ERR_INVALID_ARG;

    cJSON *by = cJSON_GetObjectItem(root, "base_year");
    if (by) h->base_year = (int)by->valuedouble;

    // attr_ids
    cJSON *ids = cJSON_GetObjectItem(root, "attr_ids");
    bool box_format = !(ids && cJSON_IsArray(ids));
    if (!box_format) {
        h->attr_count = 0;
        cJSON *item;
        cJSON_ArrayForEach(item, ids) {
            if (h->attr_count >= ENERGY_MAX_ATTRS) break;
            h->attr_ids[h->attr_count++] = (uint16_t)item->valuedouble;
        }
    } else {
        // Fichier nrg_<IEEE>.json de la LiXee-Box : attributs deduits des cles
        // ("last" et creneaux du graphe de chaque periode), annees absolues
        h->attr_count = 0;
        static const char *const PER[4] = { "hours", "days", "months", "years" };
        for (int pi = 0; pi < 4; pi++) {
            cJSON *po = cJSON_GetObjectItem(root, PER[pi]);
            cJSON *srcs[2] = { cJSON_GetObjectItem(po, "last"), cJSON_GetObjectItem(po, "graph") };
            for (int si = 0; si < 2; si++) {
                cJSON *obj = srcs[si], *e;
                cJSON_ArrayForEach(e, obj) {
                    cJSON *inner = cJSON_IsObject(e) ? e : NULL;   // graph : creneau -> {attr:val}
                    cJSON *it2 = inner ? inner->child : e;
                    for (; it2; it2 = inner ? it2->next : NULL) {
                        if (it2->string && it2->string[0] >= '0' && it2->string[0] <= '9') {
                            uint16_t a = (uint16_t)atoi(it2->string);
                            find_or_add_attr(h->attr_ids, &h->attr_count, a);
                        }
                        if (!inner) break;
                    }
                }
            }
        }
        if (h->attr_count == 0) {
            // Ni le format du pont ni celui de la Box : rien n'est remplace
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }
        if (!by) {
            int y = ntp_sync_get_year();
            h->base_year = (y < 2024 ? 2024 : y) - (ENERGY_YEARS - 1);
        }
    }

    cJSON *lts = cJSON_GetObjectItem(root, "lastTs");
    for (int i = 0; lts && i < h->attr_count; i++) {
        char key[8]; snprintf(key, sizeof(key), "%d", h->attr_ids[i]);
        cJSON *v = cJSON_GetObjectItem(lts, key);
        h->last_ts[i] = cJSON_IsNumber(v) && v->valuedouble > 0 ? (uint32_t)v->valuedouble : 0;
    }

    parse_period(cJSON_GetObjectItem(root, "hours"), &h->hours, h->attr_ids, h->attr_count, ENERGY_HOURS, h->base_year, hour_label);
    parse_period(cJSON_GetObjectItem(root, "days"), &h->days, h->attr_ids, h->attr_count, ENERGY_DAYS, h->base_year, day_label);
    parse_period(cJSON_GetObjectItem(root, "months"), &h->months, h->attr_ids, h->attr_count, ENERGY_MONTHS, h->base_year, month_label);
    parse_period(cJSON_GetObjectItem(root, "years"), &h->years, h->attr_ids, h->attr_count, ENERGY_YEARS, h->base_year, year_label);

    if (box_format) {
        // La Box range la production (attribut 1) en negatif
        for (int a = 0; a < h->attr_count; a++) {
            if (h->attr_ids[a] != 1) continue;
            energy_period_t *ps[4] = { &h->hours, &h->days, &h->months, &h->years };
            for (int k = 0; k < 4; k++) {
                for (int sl = 0; sl < 31; sl++) if (ps[k]->graph[sl].values[a] < 0) ps[k]->graph[sl].values[a] = -ps[k]->graph[sl].values[a];
                if (ps[k]->trend.values[a] < 0) ps[k]->trend.values[a] = -ps[k]->trend.values[a];
                if (ps[k]->last.values[a] < 0) ps[k]->last.values[a] = -ps[k]->last.values[a];
            }
        }
        ESP_LOGI(TAG, "Historique au format LiXee-Box importe (%d attribut(s))", h->attr_count);
    }

    h->loaded = true;
    h->dirty = false;
    cJSON_Delete(root);
    return ESP_OK;
}

// ============================================================================
// Power History - Save / Load
// ============================================================================

static esp_err_t save_power_history(uint64_t ieee_addr, power_history_t *p)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    // attr_ids
    cJSON *ids = cJSON_CreateArray();
    for (int i = 0; i < p->attr_count; i++) {
        cJSON_AddItemToArray(ids, cJSON_CreateNumber(p->attr_ids[i]));
    }
    cJSON_AddItemToObject(root, "attr_ids", ids);

    // datas — iterer en ordre chronologique (ring buffer)
    cJSON *datas = cJSON_CreateArray();
    cJSON_AddNumberToObject(root, "write_idx", p->write_idx);
    cJSON_AddNumberToObject(root, "record_count", p->record_count);
    int start = 0;
    if (p->record_count >= POWER_HISTORY_MAX) {
        // Ring buffer plein: le plus ancien est a write_idx
        start = p->write_idx;
    }
    for (int n = 0; n < p->record_count; n++) {
        int r = (start + n) % POWER_HISTORY_MAX;
        power_record_t *rec = &p->records[r];
        if (!rec->valid) continue;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "y", rec->timestamp);
        for (int a = 0; a < p->attr_count; a++) {
            char key[8]; snprintf(key, sizeof(key), "%d", p->attr_ids[a]);
            cJSON_AddNumberToObject(item, key, rec->values[a]);
        }
        cJSON_AddItemToArray(datas, item);
    }
    cJSON_AddItemToObject(root, "datas", datas);

    // stats
    cJSON *stats = cJSON_CreateObject();
    for (int a = 0; a < p->attr_count; a++) {
        char key[8]; snprintf(key, sizeof(key), "%d", p->attr_ids[a]);
        cJSON *st = cJSON_CreateObject();
        cJSON_AddNumberToObject(st, "min", p->stats[a].min);
        cJSON_AddNumberToObject(st, "max", p->stats[a].max);
        cJSON_AddNumberToObject(st, "trend", p->stats[a].trend);
        cJSON_AddNumberToObject(st, "last", p->stats[a].last);
        cJSON_AddItemToObject(stats, key, st);
    }
    cJSON_AddItemToObject(root, "stats", stats);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return ESP_ERR_NO_MEM;

    char path[64];
    make_power_path(ieee_addr, path, sizeof(path));
    esp_err_t ret = write_file(path, json_str, strlen(json_str));
    free(json_str);
    return ret;
}

static esp_err_t load_power_history(uint64_t ieee_addr, power_history_t *p)
{
    char path[64];
    make_power_path(ieee_addr, path, sizeof(path));

    char *json = read_file(path);
    if (!json) return ESP_ERR_NOT_FOUND;

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) return ESP_ERR_INVALID_ARG;

    // attr_ids
    cJSON *ids = cJSON_GetObjectItem(root, "attr_ids");
    p->attr_count = 0;
    if (ids && cJSON_IsArray(ids)) {
        cJSON *item;
        cJSON_ArrayForEach(item, ids) {
            if (p->attr_count >= ENERGY_MAX_ATTRS) break;
            p->attr_ids[p->attr_count++] = (uint16_t)item->valuedouble;
        }
    }
    // Format de la LiXee-Box (powerHistory.cpp) : pas d'attr_ids, attributs
    // deduits des cles des lignes {"y":"HH:MM","1295":VA,...}. Sans cela les
    // valeurs etaient ignorees puis le fichier reecrit vide (courbe perdue).
    cJSON *datas0 = cJSON_GetObjectItem(root, "datas");
    if (p->attr_count == 0 && cJSON_IsArray(datas0)) {
        cJSON *row, *kv;
        cJSON_ArrayForEach(row, datas0) {
            cJSON_ArrayForEach(kv, row) {
                if (!kv->string || !isdigit((unsigned char)kv->string[0])) continue;
                if (find_or_add_attr(p->attr_ids, &p->attr_count, (uint16_t)atoi(kv->string)) < 0) break;
            }
        }
    }

    // stats : objet {attr:{min,...}} (pont) ou tableau [{attr:{min,...}}] aux
    // valeurs en texte (LiXee-Box)
    cJSON *stats = cJSON_GetObjectItem(root, "stats");
    if (cJSON_IsArray(stats)) stats = cJSON_GetArrayItem(stats, 0);
    if (stats) {
        for (int a = 0; a < p->attr_count; a++) {
            char key[8]; snprintf(key, sizeof(key), "%d", p->attr_ids[a]);
            cJSON *st = cJSON_GetObjectItem(stats, key);
            if (!st) continue;
            auto num = [](const cJSON *v) -> int32_t {
                return cJSON_IsNumber(v) ? (int32_t)v->valuedouble
                     : (cJSON_IsString(v) ? (int32_t)strtol(v->valuestring, NULL, 10) : 0);
            };
            cJSON *v;
            v = cJSON_GetObjectItem(st, "min");  if (v) p->stats[a].min = num(v);
            v = cJSON_GetObjectItem(st, "max");  if (v) p->stats[a].max = num(v);
            v = cJSON_GetObjectItem(st, "trend"); if (v) p->stats[a].trend = num(v);
            v = cJSON_GetObjectItem(st, "last"); if (v) p->stats[a].last = num(v);
        }
    }

    // write_idx et record_count sauvegardees
    cJSON *wi = cJSON_GetObjectItem(root, "write_idx");
    cJSON *rc = cJSON_GetObjectItem(root, "record_count");

    // datas — les records ont ete sauvegardes en ordre chronologique
    cJSON *datas = cJSON_GetObjectItem(root, "datas");
    if (datas && cJSON_IsArray(datas)) {
        int count = cJSON_GetArraySize(datas);
        if (count > POWER_HISTORY_MAX) count = POWER_HISTORY_MAX;

        // On restaure dans l'ordre chronologique
        p->record_count = 0;
        p->write_idx = 0;
        for (int n = 0; n < count; n++) {
            cJSON *item = cJSON_GetArrayItem(datas, n);
            if (!item) continue;

            power_record_t *rec = &p->records[p->write_idx];
            memset(rec, 0, sizeof(power_record_t));

            cJSON *y = cJSON_GetObjectItem(item, "y");
            if (y && cJSON_IsString(y)) {
                strncpy(rec->timestamp, y->valuestring, 5);
                rec->timestamp[5] = '\0';
            }
            for (int a = 0; a < p->attr_count; a++) {
                char key[8]; snprintf(key, sizeof(key), "%d", p->attr_ids[a]);
                cJSON *v = cJSON_GetObjectItem(item, key);
                if (v && cJSON_IsNumber(v)) rec->values[a] = (int32_t)v->valuedouble;
            }
            rec->valid = true;
            p->record_count++;
            p->write_idx = (p->write_idx + 1) % POWER_HISTORY_MAX;
        }
    } else if (wi && rc) {
        // Fallback: utiliser les valeurs sauvegardees
        p->write_idx = (int)wi->valuedouble;
        p->record_count = (int)rc->valuedouble;
    }

    p->loaded = true;
    cJSON_Delete(root);
    ESP_LOGD(TAG, "Loaded power history for %016llX: %d records",
             (unsigned long long)ieee_addr, p->record_count);
    return ESP_OK;
}

// ============================================================================
// API publique - Gestion globale
// ============================================================================

esp_err_t energy_history_init(void)
{
    if (s_initialized) return ESP_OK;
    if (!s_data_mtx) s_data_mtx = xSemaphoreCreateRecursiveMutex();

    memset(s_entries, 0, sizeof(s_entries));
    s_entry_count = 0;

    struct stat st;
    if (stat(ENERGY_HST_DIR, &st) != 0) {
        if (mkdir(ENERGY_HST_DIR, 0755) != 0) {
            ESP_LOGW(TAG, "Cannot create " ENERGY_HST_DIR ": %s", strerror(errno));
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Energy history initialized");
    return ESP_OK;
}

/**
 * Index de l'entree de l'appareil, creee si besoin. Verrou tenu par l'appelant.
 * L'entree est remplie AVANT que s_entry_count ne l'expose : les parcours sans
 * verrou (remises a zero periodiques, sauvegarde) ne voient jamais un slot
 * a moitie initialise.
 */
static int find_or_add_entry_locked(uint64_t ieee_addr)
{
    int idx = find_entry(ieee_addr);
    if (idx >= 0) return idx;
    if (s_entry_count >= MAX_HISTORY_DEVICES) {
        ESP_LOGE(TAG, "Max history devices reached");
        return -1;
    }
    idx = s_entry_count;
    s_entries[idx].ieee_addr = ieee_addr;
    s_entries[idx].energy = NULL;
    s_entries[idx].power = NULL;
    __atomic_store_n(&s_entry_count, idx + 1, __ATOMIC_RELEASE);
    return idx;
}

energy_history_t* energy_history_get_or_create(uint64_t ieee_addr)
{
    entries_lock();
    int idx = find_or_add_entry_locked(ieee_addr);
    if (idx < 0) { entries_unlock(); return NULL; }

    if (!s_entries[idx].energy) {
        // Structure preparee puis publiee : un lecteur sans verrou ne voit pas
        // un historique encore vide avant son chargement depuis le fichier
        energy_history_t *h = alloc_energy_history();
        if (!h) { entries_unlock(); return NULL; }
        h->ieee_addr = ieee_addr;
        h->base_year = ntp_sync_get_year();
        if (h->base_year < 2024) h->base_year = 2024;

        // Tenter de charger depuis le fichier
        load_energy_history(h);
        __atomic_store_n(&s_entries[idx].energy, h, __ATOMIC_RELEASE);
    }

    energy_history_t *res = s_entries[idx].energy;
    entries_unlock();
    return res;
}

power_history_t* power_history_get_or_create(uint64_t ieee_addr)
{
    entries_lock();
    int idx = find_or_add_entry_locked(ieee_addr);
    if (idx < 0) { entries_unlock(); return NULL; }

    if (!s_entries[idx].power) {
        power_history_t *p = alloc_power_history();
        if (!p) { entries_unlock(); return NULL; }

        // Tenter de charger depuis le fichier
        load_power_history(ieee_addr, p);
        __atomic_store_n(&s_entries[idx].power, p, __ATOMIC_RELEASE);
    }

    power_history_t *res = s_entries[idx].power;
    entries_unlock();
    return res;
}

esp_err_t energy_history_reload(uint64_t ieee_addr)
{
    energy_history_t *tmp = alloc_energy_history();
    if (!tmp) return ESP_ERR_NO_MEM;
    tmp->ieee_addr = ieee_addr;
    tmp->base_year = ntp_sync_get_year();
    if (tmp->base_year < 2024) tmp->base_year = 2024;
    esp_err_t e = load_energy_history(tmp);
    if (e == ESP_OK) {
        // Ordre des verrous : donnees puis entrees, comme une mesure
        // (energy_history_get_or_create sous le verrou des donnees)
        data_lock();
        entries_lock();
        int idx = find_or_add_entry_locked(ieee_addr);
        if (idx < 0) {
            e = ESP_ERR_NO_MEM;
        } else if (s_entries[idx].energy) {
            // Copie en place : les lecteurs gardent un pointeur valide, aucune
            // mesure ni sauvegarde n'est a mi-chemin
            memcpy(s_entries[idx].energy, tmp, sizeof(*tmp));
        } else {
            __atomic_store_n(&s_entries[idx].energy, tmp, __ATOMIC_RELEASE);
            tmp = NULL;
        }
        entries_unlock();
        data_unlock();
    }
    free(tmp);
    ESP_LOGI(TAG, "Historique %016llX relu : %s", (unsigned long long)ieee_addr, esp_err_to_name(e));
    return e;
}

const energy_history_t* energy_history_find(uint64_t ieee_addr)
{
    entries_lock();
    int idx = find_entry(ieee_addr);
    const energy_history_t *h = idx >= 0 ? s_entries[idx].energy : NULL;
    entries_unlock();
    return h;
}

bool energy_history_has_attr(const energy_history_t *h, uint16_t attr_id)
{
    if (!h) return false;
    for (int i = 0; i < h->attr_count; i++) {
        if (h->attr_ids[i] == attr_id) return true;
    }
    return false;
}

int32_t energy_history_slot_value(const energy_history_t *h, energy_gran_t gran,
                                  int slot, uint16_t attr_id)
{
    if (!h) return 0;
    const energy_period_t *p;
    int n;
    switch (gran) {
    case ENERGY_GRAN_HOUR:  p = &h->hours;  n = ENERGY_HOURS;  break;
    case ENERGY_GRAN_DAY:   p = &h->days;   n = ENERGY_DAYS;   break;
    case ENERGY_GRAN_MONTH: p = &h->months; n = ENERGY_MONTHS; break;
    default:                p = &h->years;  n = ENERGY_YEARS;  break;
    }
    if (slot < 0 || slot >= n) return 0;
    for (int i = 0; i < h->attr_count; i++) {
        if (h->attr_ids[i] == attr_id) return p->graph[slot].values[i];
    }
    return 0;
}

esp_err_t energy_history_remove(uint64_t ieee_addr)
{
    // Sous le verrou des entrees : le decalage ci-dessous ne doit pas croiser
    // une creation concurrente. (Fonction appelee par personne a ce jour ; les
    // pointeurs deja rendus aux appelants deviendraient invalides.)
    entries_lock();
    int idx = find_entry(ieee_addr);
    if (idx < 0) { entries_unlock(); return ESP_ERR_NOT_FOUND; }

    if (s_entries[idx].energy) { free(s_entries[idx].energy); s_entries[idx].energy = NULL; }
    if (s_entries[idx].power)  { free(s_entries[idx].power);  s_entries[idx].power = NULL; }

    // Shift entries
    for (int i = idx; i < s_entry_count - 1; i++) {
        s_entries[i] = s_entries[i + 1];
    }
    s_entry_count--;
    memset(&s_entries[s_entry_count], 0, sizeof(history_entry_t));
    entries_unlock();

    // Delete files
    char path[64];
    make_energy_path(ieee_addr, path, sizeof(path));
    remove(path);
    make_power_path(ieee_addr, path, sizeof(path));
    remove(path);

    return ESP_OK;
}

// ============================================================================
// API publique - Ajout de mesures
// ============================================================================

/**
 * Met a jour une granularite avec un nouvel index absolu.
 *
 * Le delta affiche pour la periode courante vaut simplement value - ref, ou
 * 'ref' est l'index releve au debut de cette periode.
 *
 * L'ancien code derivait le delta de slots[periode precedente], une reference
 * qui n'etait ni persistee ni invalidee d'un cycle sur l'autre :
 *   - apres un redemarrage elle valait 0, et le creneau en cours etait ecrase
 *     par 0 puis sauvegarde (mois courant a 0 Wh jusqu'au mois suivant) ;
 *   - sur un releve manquant elle pointait le cycle precedent, donnant un
 *     delta couvrant ~25 h (heures) ou ~31 jours (jours) ;
 *   - le repli prev_day = 30 le 1er du mois designait le 31 du mois d'avant,
 *     donc fevrier/avril/juin... produisaient un delta d'un a deux mois.
 * Aucun voisin n'est desormais indexe : ces trois cas disparaissent.
 */
static void period_update(energy_period_t *p, int cur_slot, int ai, int32_t value)
{
    const uint16_t bit = (uint16_t)(1u << ai);

    if (p->ref_slot < 0) {
        // Premiere mesure apres un chargement sans periode connue : adopter la
        // periode courante sans toucher au graphique deja charge.
        p->ref_slot = cur_slot;
    } else if (p->ref_slot != cur_slot) {
        // Changement de periode : la nouvelle part du dernier index observe,
        // et son creneau repart de zero.
        p->ref      = p->last;
        p->ref_mask = p->last_mask;
        p->ref_slot = cur_slot;
        memset(&p->graph[cur_slot], 0, sizeof(p->graph[cur_slot]));
    }

    if (!(p->ref_mask & bit)) {
        // Pas encore de reference pour cet attribut. On la recule de ce qui est
        // deja compte dans ce creneau, pour ne pas perdre le debut de periode
        // lors d'une reprise sur un fichier anterieur.
        // L'ancre est 'last' (index au moment ou le graphique a ete ecrit), et
        // non la valeur courante : sinon la consommation ecoulee entre le
        // dernier releve d'avant coupure et maintenant ecraserait le total.
        int32_t already = p->graph[cur_slot].values[ai];
        int32_t anchor  = (p->last_mask & bit) ? p->last.values[ai] : value;
        int32_t base    = anchor - already;
        if (already <= 0 || base < 0 || base > value) base = value;
        p->ref.values[ai] = base;
        p->ref_mask |= bit;
    } else if (value < p->ref.values[ai]) {
        // Index en recul : compteur remis a zero, remplace, ou releve aberrant.
        // Rebaser tout de suite plutot que de figer le delta a 0 pour toute la
        // periode (et les suivantes).
        ESP_LOGW(TAG, "Index en recul (%ld < %ld), reference rebasee",
                 (long)value, (long)p->ref.values[ai]);
        p->ref.values[ai] = value;
    }

    p->graph[cur_slot].values[ai] = value - p->ref.values[ai];
    p->trend.values[ai] = p->graph[cur_slot].values[ai];
    p->last.values[ai]  = value;
    p->last_mask |= bit;
}

// ----------------------------------------------------------------------------
// Trous et pics (energyHistory.cpp de la LiXee-Box)
// ----------------------------------------------------------------------------

#define ENERGY_GAP_THRESHOLD_SEC (45 * 60)   // index rapporte au plus toutes les 5 min
#define MAX_ENERGY_DELTA_WH      100000      // 100 kWh d'un releve a l'autre : impossible

typedef enum { G_HOUR, G_DAY, G_MONTH, G_YEAR } hist_gran_t;

/** Debut (heure locale) du creneau contenant t. */
static time_t slot_start(time_t t, hist_gran_t g)
{
    struct tm tm;
    localtime_r(&t, &tm);
    tm.tm_sec = 0;
    tm.tm_min = 0;
    if (g >= G_DAY) tm.tm_hour = 0;
    if (g >= G_MONTH) tm.tm_mday = 1;
    if (g >= G_YEAR) tm.tm_mon = 0;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

/** Nombre de frontieres de creneau franchies entre a et b (b >= a). */
static int slots_between(time_t a, time_t b, hist_gran_t g)
{
    struct tm ta, tb;
    localtime_r(&a, &ta);
    localtime_r(&b, &tb);
    switch (g) {
    case G_HOUR:  return (int)((slot_start(b, G_HOUR) - slot_start(a, G_HOUR) + 1800) / 3600);
    case G_DAY:   return (int)((slot_start(b, G_DAY) - slot_start(a, G_DAY) + 43200) / 86400);
    case G_MONTH: return (tb.tm_year - ta.tm_year) * 12 + tb.tm_mon - ta.tm_mon;
    default:      return tb.tm_year - ta.tm_year;
    }
}

/**
 * Repartit 'delta' (energie consommee depuis lu, la derniere mesure) entre le
 * creneau courant, au prorata du temps, et au plus back_cap creneaux
 * precedents a parts egales. Les creneaux entierement compris dans le trou
 * sont remplaces (ils portaient la periode precedente, jamais remise a zero),
 * celui de la derniere mesure est complete.
 */
static void period_spread(energy_period_t *p, int cur_slot, int ai, int32_t value, int32_t delta,
                          time_t lu, time_t now, hist_gran_t g, int back_cap, int nslots)
{
    int k = slots_between(lu, now, g);
    if (k <= 0) {                                  // trou dans le creneau courant
        period_update(p, cur_slot, ai, value);
        return;
    }
    const uint16_t bit = (uint16_t)(1u << ai);
    if (p->ref_slot != cur_slot) {                 // changement de periode, comme period_update
        p->ref      = p->last;
        p->ref_mask = p->last_mask;
        p->ref_slot = cur_slot;
        memset(&p->graph[cur_slot], 0, sizeof(p->graph[cur_slot]));
    }
    double span = (double)(now - lu);
    int32_t cur_part = span > 0 ? (int32_t)((double)delta * (double)(now - slot_start(now, g)) / span) : delta;
    if (cur_part < 0) cur_part = 0;
    if (cur_part > delta) cur_part = delta;
    int nb = k < back_cap ? k : back_cap;
    if (nb > 0) {
        // Part de chaque creneau d'apres la duree reelle du trou (k creneaux) ;
        // ce qui depasse la fenetre affichee n'est pas reporte sur les creneaux
        // recents (la granularite superieure le garde)
        int32_t per = (delta - cur_part) / k;
        for (int i = 1; i <= nb; i++) {
            int sl = cur_slot - i;
            if (sl < 0) sl += nslots;
            if (sl < 0 || sl >= nslots) break;
            if (i == k) p->graph[sl].values[ai] += per;   // creneau de la derniere mesure
            else p->graph[sl].values[ai] = per;
        }
    }
    p->graph[cur_slot].values[ai] = cur_part;
    p->ref.values[ai] = value - cur_part;
    p->ref_mask |= bit;
    p->trend.values[ai] = cur_part;
    p->last.values[ai]  = value;
    p->last_mask |= bit;
}

/** Releve aberrant : l'index est suivi sans compter le saut. */
static void period_rebase(energy_period_t *p, int cur_slot, int ai, int32_t value, int32_t delta)
{
    period_update(p, cur_slot, ai, value);
    p->ref.values[ai] += delta;
    p->graph[cur_slot].values[ai] -= delta;
    if (p->graph[cur_slot].values[ai] < 0) {
        p->ref.values[ai] += p->graph[cur_slot].values[ai];
        p->graph[cur_slot].values[ai] = 0;
    }
    p->trend.values[ai] = p->graph[cur_slot].values[ai];
}

/** Fenetre glissante des annees : decalage quand l'annee courante la depasse. */
static void years_roll(energy_history_t *h, int year)
{
    while (year - h->base_year >= ENERGY_YEARS) {
        energy_period_t *p = &h->years;
        memmove(&p->graph[0], &p->graph[1], sizeof(p->graph[0]) * (ENERGY_YEARS - 1));
        memset(&p->graph[ENERGY_YEARS - 1], 0, sizeof(p->graph[0]));
        if (p->ref_slot >= 0) p->ref_slot--;
        h->base_year++;
    }
}

esp_err_t energy_history_add_measurement(uint64_t ieee_addr,
                                          uint16_t attr_id,
                                          int32_t value)
{
    return energy_history_add_measurement_ex(ieee_addr, attr_id, value, true);
}

static esp_err_t add_measurement_locked(uint64_t ieee_addr, uint16_t attr_id,
                                        int32_t value, bool gap_smoothing);

esp_err_t energy_history_add_measurement_ex(uint64_t ieee_addr, uint16_t attr_id,
                                             int32_t value, bool gap_smoothing)
{
    data_lock();
    esp_err_t e = add_measurement_locked(ieee_addr, attr_id, value, gap_smoothing);
    data_unlock();
    return e;
}

static esp_err_t add_measurement_locked(uint64_t ieee_addr, uint16_t attr_id,
                                        int32_t value, bool gap_smoothing)
{
    energy_history_t *h = energy_history_get_or_create(ieee_addr);
    if (!h) return ESP_ERR_NO_MEM;

    int ai = find_or_add_attr(h->attr_ids, &h->attr_count, attr_id);
    if (ai < 0) return ESP_ERR_NO_MEM;

    // L'heure doit etre valide. Sans cette garde, les getters renvoyaient 0 en
    // sentinelle d'erreur et toute mesure partait dans l'heure 00, le jour 01
    // et janvier : les valeurs reellement consommees de ces creneaux etaient
    // ecrasees puis persistees. Lecture atomique via struct tm, pour ne pas
    // melanger deux instants si l'on franchit une frontiere entre deux appels.
    struct tm now_tm;
    if (ntp_sync_get_local_time(&now_tm) != ESP_OK) {
        ESP_LOGW(TAG, "Heure non synchronisee, mesure d'energie ignoree");
        return ESP_ERR_INVALID_STATE;
    }

    years_roll(h, now_tm.tm_year + 1900);
    int hour     = now_tm.tm_hour;                        // 0-23
    int day      = now_tm.tm_mday - 1;                    // 0-30
    int month    = now_tm.tm_mon;                         // 0-11
    int year_idx = (now_tm.tm_year + 1900) - h->base_year;

    if (hour < 0 || hour >= ENERGY_HOURS) return ESP_ERR_INVALID_STATE;
    if (day < 0) day = 0;
    else if (day >= ENERGY_DAYS) day = ENERGY_DAYS - 1;
    if (month < 0) month = 0;
    else if (month >= ENERGY_MONTHS) month = ENERGY_MONTHS - 1;
    if (year_idx < 0) year_idx = 0;
    else if (year_idx >= ENERGY_YEARS) year_idx = ENERGY_YEARS - 1;

    time_t now = time(NULL);
    const uint16_t bit = (uint16_t)(1u << ai);
    bool known = (h->hours.last_mask & bit) != 0;
    int32_t delta = known ? value - h->hours.last.values[ai] : 0;
    time_t lu = (time_t)h->last_ts[ai];
    bool gap = gap_smoothing && lu > 0 && now > lu && now - lu > ENERGY_GAP_THRESHOLD_SEC;
    // Plafond plausible : 100 kWh par heure ecoulee (compteur change, releve
    // aberrant pendant un trou : ecarte plutot que reparti)
    int64_t hours_gap = (lu > 0 && now > lu) ? (now - lu + 3599) / 3600 : 1;
    if (hours_gap < 1) hours_gap = 1;
    bool jump = known && (int64_t)delta > (int64_t)MAX_ENERGY_DELTA_WH * hours_gap;

    if (known && delta > 0 && gap && !jump) {
        // Trou de mesures : energie repartie sur les creneaux manques
        ESP_LOGW(TAG, "Trou de %ld min (attr %u) : %ld Wh repartis",
                 (long)((now - lu) / 60), attr_id, (long)delta);
        period_spread(&h->hours,  hour,     ai, value, delta, lu, now, G_HOUR,  ENERGY_HOURS - 1, ENERGY_HOURS);
        period_spread(&h->days,   day,      ai, value, delta, lu, now, G_DAY,   day,      ENERGY_DAYS);
        period_spread(&h->months, month,    ai, value, delta, lu, now, G_MONTH, month,    ENERGY_MONTHS);
        period_spread(&h->years,  year_idx, ai, value, delta, lu, now, G_YEAR,  year_idx, ENERGY_YEARS);
    } else if (jump) {
        // Saut impossible sans trou : index suivi, saut ecarte
        ESP_LOGW(TAG, "Saut d'index ecarte (attr %u, +%ld Wh)", attr_id, (long)delta);
        period_rebase(&h->hours,  hour,     ai, value, delta);
        period_rebase(&h->days,   day,      ai, value, delta);
        period_rebase(&h->months, month,    ai, value, delta);
        period_rebase(&h->years,  year_idx, ai, value, delta);
    } else {
        period_update(&h->hours,  hour,     ai, value);
        period_update(&h->days,   day,      ai, value);
        period_update(&h->months, month,    ai, value);
        period_update(&h->years,  year_idx, ai, value);
    }

    h->last_ts[ai] = (uint32_t)now;
    h->dirty = true;
    return ESP_OK;
}

esp_err_t power_history_add_measurement(uint64_t ieee_addr,
                                         uint16_t attr_id,
                                         int32_t value)
{
    power_history_t *p = power_history_get_or_create(ieee_addr);
    if (!p) return ESP_ERR_NO_MEM;

    int ai = find_or_add_attr(p->attr_ids, &p->attr_count, attr_id);
    if (ai < 0) return ESP_ERR_NO_MEM;

    // Mettre a jour les stats
    if (p->stats[ai].min == 0 && p->stats[ai].max == 0) {
        p->stats[ai].min = value;
        p->stats[ai].max = value;
    } else {
        if (value < p->stats[ai].min) p->stats[ai].min = value;
        if (value > p->stats[ai].max) p->stats[ai].max = value;
    }
    p->stats[ai].trend = value - p->stats[ai].last;
    p->stats[ai].last = value;

    // Ajouter un record avec le timestamp courant
    char ts[12];  // ntp_sync_get_formatted_time retourne "HH:MM:SS" (9 chars)
    ntp_sync_get_formatted_time(ts, sizeof(ts));
    ts[5] = '\0';  // Tronquer a "HH:MM"

    // Verifier si le dernier record a le meme timestamp
    if (p->record_count > 0) {
        int last_idx = (p->write_idx == 0) ? POWER_HISTORY_MAX - 1 : p->write_idx - 1;
        if (p->records[last_idx].valid &&
            strncmp(p->records[last_idx].timestamp, ts, 5) == 0) {
            // Meme minute: mettre a jour la valeur existante
            p->records[last_idx].values[ai] = value;
            return ESP_OK;
        }
    }

    // Nouveau record
    power_record_t *rec = &p->records[p->write_idx];
    if (!rec->valid) {
        // Nouveau slot
        memset(rec, 0, sizeof(power_record_t));
        rec->valid = true;
        p->record_count++;
        if (p->record_count > POWER_HISTORY_MAX) p->record_count = POWER_HISTORY_MAX;
    } else {
        // Ecraser un ancien (ring buffer plein)
        memset(rec->values, 0, sizeof(rec->values));
    }

    strncpy(rec->timestamp, ts, 5);
    rec->timestamp[5] = '\0';
    rec->values[ai] = value;

    p->write_idx = (p->write_idx + 1) % POWER_HISTORY_MAX;

    return ESP_OK;
}

// ============================================================================
// API publique - Reset periodique
// ============================================================================

esp_err_t energy_history_reset_hourly(int hour)
{
    int next_hour = (hour + 1) % ENERGY_HOURS;
    for (int i = 0; i < s_entry_count; i++) {
        energy_history_t *h = s_entries[i].energy;
        if (!h) continue;
        memset(&h->hours.graph[next_hour], 0, sizeof(energy_slot_t));
        h->dirty = true;
    }
    ESP_LOGD(TAG, "Hourly reset: cleared slot %d", next_hour);
    return ESP_OK;
}

esp_err_t energy_history_reset_daily(int day)
{
    int next_day = day % ENERGY_DAYS; // day est 1-31, index 0-30
    for (int i = 0; i < s_entry_count; i++) {
        energy_history_t *h = s_entries[i].energy;
        if (!h) continue;
        memset(&h->days.graph[next_day], 0, sizeof(energy_slot_t));
        h->dirty = true;
    }
    ESP_LOGD(TAG, "Daily reset: cleared slot %d", next_day);
    return ESP_OK;
}

esp_err_t energy_history_reset_monthly(int month)
{
    int next_month = month % ENERGY_MONTHS; // month 1-12, index 0-11
    for (int i = 0; i < s_entry_count; i++) {
        energy_history_t *h = s_entries[i].energy;
        if (!h) continue;
        memset(&h->months.graph[next_month], 0, sizeof(energy_slot_t));
        h->dirty = true;
    }
    ESP_LOGD(TAG, "Monthly reset: cleared slot %d", next_month);
    return ESP_OK;
}

// ============================================================================
// API publique - Persistence
// ============================================================================

esp_err_t energy_history_save_all(void)
{
    int saved = 0;
    for (int i = 0; i < s_entry_count; i++) {
        if (s_entries[i].energy && s_entries[i].energy->dirty) {
            if (save_energy_history(s_entries[i].energy) == ESP_OK) saved++;
        }
        if (s_entries[i].power && s_entries[i].power->record_count > 0) {
            save_power_history(s_entries[i].ieee_addr, s_entries[i].power);
        }
    }
    if (saved > 0) {
        ESP_LOGI(TAG, "Saved %d energy history files", saved);
    }
    return ESP_OK;
}

esp_err_t energy_history_load_all(void)
{
    DIR *dir = opendir(ENERGY_HST_DIR);
    if (!dir) {
        ESP_LOGI(TAG, "No history directory, starting fresh");
        return ESP_OK;
    }

    int loaded_energy = 0, loaded_power = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        size_t namelen = strlen(name);
        if (namelen < 25) continue; // prefix(4) + 16hex + .json(5) = 25
        if (strcmp(name + namelen - 5, ".json") != 0) continue;

        // Determiner le type: nrg_ ou pwr_
        bool is_energy = (strncmp(name, "nrg_", 4) == 0);
        bool is_power  = (strncmp(name, "pwr_", 4) == 0);
        if (!is_energy && !is_power) continue;

        char ieee_str[17];
        strncpy(ieee_str, name + 4, 16);
        ieee_str[16] = '\0';

        uint64_t ieee_addr = strtoull(ieee_str, NULL, 16);
        if (ieee_addr == 0) continue;

        if (is_energy) {
            energy_history_t *h = energy_history_get_or_create(ieee_addr);
            if (h && h->loaded) loaded_energy++;
        } else {
            power_history_t *p = power_history_get_or_create(ieee_addr);
            if (p && p->loaded) loaded_power++;
        }
    }
    closedir(dir);

    ESP_LOGI(TAG, "Loaded %d energy + %d power history files from " ENERGY_HST_DIR,
             loaded_energy, loaded_power);
    return ESP_OK;
}

// ============================================================================
// API publique - Serialisation JSON pour frontend
// ============================================================================

char* energy_history_to_json(uint64_t ieee_addr, const char *period)
{
    int idx = find_entry(ieee_addr);
    if (idx < 0 || !s_entries[idx].energy) return NULL;

    energy_history_t *h = s_entries[idx].energy;
    const energy_period_t *p = NULL;
    int count = 0;
    const char *suffix = "";

    if (strcmp(period, "hour") == 0) {
        p = &h->hours; count = ENERGY_HOURS; suffix = "H";
    } else if (strcmp(period, "day") == 0) {
        p = &h->days; count = ENERGY_DAYS; suffix = "";
    } else if (strcmp(period, "month") == 0) {
        p = &h->months; count = ENERGY_MONTHS; suffix = "";
    } else if (strcmp(period, "year") == 0) {
        p = &h->years; count = ENERGY_YEARS; suffix = "";
    } else {
        return NULL;
    }

    // Format Morris.js: [{"y":"08H","256":1500}, ...]
    cJSON *arr = cJSON_CreateArray();
    if (!arr) return NULL;

    for (int s = 0; s < count; s++) {
        cJSON *item = cJSON_CreateObject();

        // Label "y"
        char label[16];
        if (strcmp(period, "hour") == 0) {
            snprintf(label, sizeof(label), "%02d%s", s, suffix);
        } else if (strcmp(period, "day") == 0) {
            snprintf(label, sizeof(label), "%02d", s + 1);
        } else if (strcmp(period, "month") == 0) {
            snprintf(label, sizeof(label), "%02d", s + 1);
        } else {
            snprintf(label, sizeof(label), "%d", h->base_year + s);
        }
        cJSON_AddStringToObject(item, "y", label);

        for (int a = 0; a < h->attr_count; a++) {
            char key[8]; snprintf(key, sizeof(key), "%d", h->attr_ids[a]);
            cJSON_AddNumberToObject(item, key, p->graph[s].values[a]);
        }

        cJSON_AddItemToArray(arr, item);
    }

    char *result = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return result;
}

char* power_history_to_chartjs_json(uint64_t ieee_addr)
{
    int idx = find_entry(ieee_addr);
    if (idx < 0 || !s_entries[idx].power) {
        // Retourner structure vide
        return strdup("{\"labels\":[],\"datasets\":[{\"data\":[]},{\"data\":[]}]}");
    }

    power_history_t *p = s_entries[idx].power;
    if (p->record_count == 0) {
        return strdup("{\"labels\":[],\"datasets\":[{\"data\":[]},{\"data\":[]}]}");
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *labels = cJSON_CreateArray();

    // Creer un dataset par attribut (max 2 pour Chart.js standard)
    int ds_count = (p->attr_count > 2) ? 2 : p->attr_count;
    cJSON *datasets = cJSON_CreateArray();
    cJSON *ds_data[2] = {};
    for (int d = 0; d < ds_count; d++) {
        cJSON *ds = cJSON_CreateObject();
        ds_data[d] = cJSON_CreateArray();
        cJSON_AddItemToObject(ds, "data", ds_data[d]);
        cJSON_AddItemToArray(datasets, ds);
    }
    // Ajouter datasets vides si moins de 2
    for (int d = ds_count; d < 2; d++) {
        cJSON *ds = cJSON_CreateObject();
        cJSON_AddItemToObject(ds, "data", cJSON_CreateArray());
        cJSON_AddItemToArray(datasets, ds);
    }

    // Parcourir les records dans l'ordre chronologique
    int start = 0;
    if (p->record_count >= POWER_HISTORY_MAX) {
        start = p->write_idx; // Ring buffer plein: commencer au plus ancien
    }

    int total = (p->record_count < POWER_HISTORY_MAX) ? p->record_count : POWER_HISTORY_MAX;
    for (int r = 0; r < total; r++) {
        int ri = (start + r) % POWER_HISTORY_MAX;
        power_record_t *rec = &p->records[ri];
        if (!rec->valid) continue;

        cJSON_AddItemToArray(labels, cJSON_CreateString(rec->timestamp));
        for (int d = 0; d < ds_count; d++) {
            cJSON_AddItemToArray(ds_data[d], cJSON_CreateNumber(rec->values[d]));
        }
    }

    cJSON_AddItemToObject(root, "labels", labels);
    cJSON_AddItemToObject(root, "datasets", datasets);

    char *result = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return result;
}

char* energy_history_to_chartjs_json(uint64_t ieee_addr, const char *period)
{
    int idx = find_entry(ieee_addr);
    if (idx < 0 || !s_entries[idx].energy) {
        return strdup("{\"labels\":[],\"datasets\":[{\"data\":[]},{\"data\":[]}]}");
    }

    energy_history_t *h = s_entries[idx].energy;
    const energy_period_t *p = NULL;
    int count = 0;

    if (strcmp(period, "hour") == 0) { p = &h->hours; count = ENERGY_HOURS; }
    else if (strcmp(period, "day") == 0) { p = &h->days; count = ENERGY_DAYS; }
    else if (strcmp(period, "month") == 0) { p = &h->months; count = ENERGY_MONTHS; }
    else if (strcmp(period, "year") == 0) { p = &h->years; count = ENERGY_YEARS; }
    else return strdup("{\"labels\":[],\"datasets\":[{\"data\":[]},{\"data\":[]}]}");

    cJSON *root = cJSON_CreateObject();
    cJSON *labels = cJSON_CreateArray();

    int ds_count = (h->attr_count > 2) ? 2 : h->attr_count;
    if (ds_count == 0) ds_count = 1;
    cJSON *datasets = cJSON_CreateArray();
    cJSON *ds_data[2] = {};

    for (int d = 0; d < ds_count; d++) {
        cJSON *ds = cJSON_CreateObject();
        ds_data[d] = cJSON_CreateArray();
        cJSON_AddItemToObject(ds, "data", ds_data[d]);
        cJSON_AddItemToArray(datasets, ds);
    }
    for (int d = ds_count; d < 2; d++) {
        cJSON *ds = cJSON_CreateObject();
        cJSON_AddItemToObject(ds, "data", cJSON_CreateArray());
        cJSON_AddItemToArray(datasets, ds);
    }

    for (int s = 0; s < count; s++) {
        char label[16];
        if (strcmp(period, "hour") == 0) snprintf(label, sizeof(label), "%02dH", s);
        else if (strcmp(period, "day") == 0) snprintf(label, sizeof(label), "%02d", s + 1);
        else if (strcmp(period, "month") == 0) snprintf(label, sizeof(label), "%02d", s + 1);
        else snprintf(label, sizeof(label), "%d", h->base_year + s);

        cJSON_AddItemToArray(labels, cJSON_CreateString(label));
        for (int d = 0; d < ds_count; d++) {
            int ai = (d < h->attr_count) ? d : 0;
            cJSON_AddItemToArray(ds_data[d], cJSON_CreateNumber(p->graph[s].values[ai]));
        }
    }

    cJSON_AddItemToObject(root, "labels", labels);
    cJSON_AddItemToObject(root, "datasets", datasets);

    char *result = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return result;
}
