// ============================================================================
// Archive de l'historique d'energie - voir energy_archive.h
// ============================================================================

#include "energy_archive.h"
#include "energy_history.h"
#include "ntp_sync.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *TAG = "NRG_ARCH";

#define ARCH_DIR         "/littlefs/hst"
#define HR_KEEP_DAYS     400                       // 13 mois et quelques jours
#define WRITE_PERIOD_US  (15LL * 60 * 1000000)     // recopie au plus tous les 1/4 h
#define FILE_MAX         (256 * 1024)
#define MAX_DEV          16
#define MAX_PROV         4
#define MAX_DOCS         10
#define BITS_BASE        18262                     // 01/01/2020 en jours depuis 1970
#define BITS_SPAN        8192                      // ~22 ans

static SemaphoreHandle_t s_lock;                   // inventaire et fournisseurs
static energy_archive_provider_t s_prov[MAX_PROV];
static int s_nprov;
static uint8_t s_hr_bits[BITS_SPAN / 8];           // jours avec un fichier hr_
static uint8_t s_dy_bits[BITS_SPAN / 8];           // jours presents dans un dy_
static bool s_ready;
static int64_t s_last_write;
static int32_t s_last_hour_key = -1;               // jour x 24 + heure de la derniere recopie
static int32_t s_last_purge_day = -1;
static volatile bool s_clear_req;

// ----------------------------------------------------------------------------
// Dates
// ----------------------------------------------------------------------------

static int32_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int32_t energy_archive_daynum(uint32_t ymd)
{
    return days_from_civil((int)(ymd / 10000), (int)(ymd / 100 % 100), (int)(ymd % 100));
}

uint32_t energy_archive_ymd(int32_t z)
{
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const int doe = z - era * 146097;
    const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = yoe + era * 400;
    const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int mp = (5 * doy + 2) / 153;
    const int d = doy - (153 * mp + 2) / 5 + 1;
    const int m = mp + (mp < 10 ? 3 : -9);
    y += m <= 2;
    return (uint32_t)(y * 10000 + m * 100 + d);
}

static uint32_t ymd_of(const struct tm *t)
{
    return (uint32_t)((t->tm_year + 1900) * 10000 + (t->tm_mon + 1) * 100 + t->tm_mday);
}

static void bit_set(uint8_t *bits, uint32_t ymd, bool on)
{
    int32_t i = energy_archive_daynum(ymd) - BITS_BASE;
    if (i < 0 || i >= BITS_SPAN) return;
    if (on) bits[i >> 3] |= (uint8_t)(1u << (i & 7));
    else bits[i >> 3] &= (uint8_t)~(1u << (i & 7));
}

// ----------------------------------------------------------------------------
// Fichiers
// ----------------------------------------------------------------------------

static void path_of(char kind, uint32_t key, char *out, size_t len)
{
    switch (kind) {
    case 'h': snprintf(out, len, ARCH_DIR "/hr_%08lu.json", (unsigned long)key); break;
    case 'd': snprintf(out, len, ARCH_DIR "/dy_%06lu.json", (unsigned long)key); break;
    case 'm': snprintf(out, len, ARCH_DIR "/mo_%04lu.json", (unsigned long)key); break;
    default:  snprintf(out, len, ARCH_DIR "/yr.json"); break;
    }
}

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    struct stat st;
    long size = (fstat(fileno(f), &st) == 0) ? (long)st.st_size : -1;
    if (size <= 0 || size > FILE_MAX) { fclose(f); return NULL; }
    char *buf = (char *)heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (char *)malloc(size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, size, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

/** Ecriture atomique (fichier temporaire puis rename, comme energy_history). */
static bool write_text(const char *path, const char *txt)
{
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) { ESP_LOGW(TAG, "Ouverture de %s impossible : %s", tmp, strerror(errno)); return false; }
    size_t len = strlen(txt);
    bool ok = fwrite(txt, 1, len, f) == len;
    if (ok && fflush(f) != 0) ok = false;
    if (ok && fsync(fileno(f)) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (ok && rename(tmp, path) != 0) ok = false;
    if (!ok) {
        ESP_LOGW(TAG, "Ecriture de %s impossible : %s", path, strerror(errno));
        unlink(tmp);
    }
    return ok;
}

cJSON *energy_archive_load(char kind, uint32_t key)
{
    char path[48];
    path_of(kind, key, path, sizeof(path));
    char *txt = read_text(path);
    if (!txt) return NULL;
    cJSON *root = cJSON_Parse(txt);
    free(txt);
    if (root && !cJSON_IsObject(root)) { cJSON_Delete(root); root = NULL; }
    return root;
}

static cJSON *obj_child(cJSON *parent, const char *key)
{
    cJSON *o = cJSON_GetObjectItem(parent, key);
    if (cJSON_IsObject(o)) return o;
    if (o) cJSON_DeleteItemFromObject(parent, key);
    return cJSON_AddObjectToObject(parent, key);
}

/** Retire les objets vides de "e" (appareils sans creneau). */
static void prune(cJSON *root)
{
    cJSON *e = cJSON_GetObjectItem(root, "e");
    if (!e) return;
    cJSON *dev = e->child;
    while (dev) {
        cJSON *next = dev->next;
        if (!dev->child) cJSON_Delete(cJSON_DetachItemViaPointer(e, dev));
        dev = next;
    }
    if (!e->child) cJSON_DeleteItemFromObject(root, "e");
}

// ----------------------------------------------------------------------------
// Documents en cours de recopie
// ----------------------------------------------------------------------------

typedef struct {
    char   kind;
    uint32_t key;
    cJSON *root;
    char  *old;                                    // contenu lu, pour ne reecrire que les changements
} doc_t;

static doc_t s_docs[MAX_DOCS];
static int s_ndocs;

static cJSON *doc_get(char kind, uint32_t key)
{
    for (int i = 0; i < s_ndocs; i++) {
        if (s_docs[i].kind == kind && s_docs[i].key == key) return s_docs[i].root;
    }
    if (s_ndocs >= MAX_DOCS) return NULL;
    doc_t *d = &s_docs[s_ndocs];
    char path[48];
    path_of(kind, key, path, sizeof(path));
    d->kind = kind;
    d->key = key;
    d->old = read_text(path);
    d->root = d->old ? cJSON_Parse(d->old) : NULL;
    if (d->root && !cJSON_IsObject(d->root)) { cJSON_Delete(d->root); d->root = NULL; }
    if (!d->root) d->root = cJSON_CreateObject();
    if (!d->root) { free(d->old); return NULL; }
    s_ndocs++;
    return d->root;
}

/** Inventaire d'un fichier recopie. */
static void note_inventory(char kind, uint32_t key, const cJSON *root)
{
    if (kind == 'h') {
        // Journee utile : energie, temperature, ou au moins une heure de presence
        bool any = cJSON_GetObjectItem(root, "e") || cJSON_GetObjectItem(root, "t");
        const cJSON *p;
        cJSON_ArrayForEach(p, cJSON_GetObjectItem(root, "p")) if (cJSON_IsNumber(p) && p->valueint) any = true;
        bit_set(s_hr_bits, key, any);
    } else if (kind == 'd') {
        const cJSON *dev;
        cJSON_ArrayForEach(dev, cJSON_GetObjectItem(root, "e")) {
            const cJSON *day;
            cJSON_ArrayForEach(day, dev) {
                int dd = day->string ? atoi(day->string) : 0;
                if (dd >= 1 && dd <= 31 && day->child) bit_set(s_dy_bits, key * 100 + dd, true);
            }
        }
    }
}

static void docs_commit(void)
{
    for (int i = 0; i < s_ndocs; i++) {
        doc_t *d = &s_docs[i];
        prune(d->root);
        char path[48];
        path_of(d->kind, d->key, path, sizeof(path));
        if (!d->root->child) {
            if (d->old) unlink(path);                 // plus rien a garder
        } else {
            char *txt = cJSON_PrintUnformatted(d->root);
            if (txt && (!d->old || strcmp(txt, d->old) != 0)) write_text(path, txt);
            free(txt);
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        note_inventory(d->kind, d->key, d->root);
        xSemaphoreGive(s_lock);
        cJSON_Delete(d->root);
        free(d->old);
    }
    s_ndocs = 0;
}

// ----------------------------------------------------------------------------
// Recopie des anneaux
// ----------------------------------------------------------------------------

/** Ecrit les valeurs non nulles d'un creneau sous dev[slot] (remplace l'existant). */
static void put_slot(cJSON *dev, int slot_label, const energy_history_t *h, const energy_period_t *p, int slot)
{
    char k[8];
    snprintf(k, sizeof(k), "%d", slot_label);
    cJSON_DeleteItemFromObject(dev, k);
    cJSON *o = NULL;
    for (int a = 0; a < h->attr_count; a++) {
        int32_t v = p->graph[slot].values[a];
        if (!v) continue;
        if (!o) o = cJSON_AddObjectToObject(dev, k);
        if (!o) return;
        char ak[8];
        snprintf(ak, sizeof(ak), "%u", h->attr_ids[a]);
        cJSON_AddNumberToObject(o, ak, v);
    }
}

/** Jour (tm a midi) decale de delta jours. */
static void shift_day(const struct tm *t, int delta, struct tm *out)
{
    *out = *t;
    out->tm_mday += delta;
    out->tm_hour = 12;
    out->tm_min = out->tm_sec = 0;
    out->tm_isdst = -1;
    mktime(out);
}

static void mirror(void)
{
    struct tm now;
    if (!ntp_sync_is_synced() || ntp_sync_get_local_time(&now) != ESP_OK) return;
    energy_history_expire_stale_all();

    struct tm yest;
    shift_day(&now, -1, &yest);
    const uint32_t today = ymd_of(&now), yesterday = ymd_of(&yest);
    const int hc = now.tm_hour, year = now.tm_year + 1900;

    // Jours de l'anneau (31 creneaux) : date la plus recente de chaque creneau,
    // au plus 30 jours en arriere ; le plus ancien (jour + 1 du mois precedent)
    // n'est pas recopie : archive a la fin de sa periode, et vide par avance
    // par les versions anterieures (ecraserait l'archive juste apres une mise a jour)
    uint32_t day_date[31] = {};
    const int day_excl = now.tm_mday % 31;
    for (int b = 0; b <= 30; b++) {
        struct tm d;
        shift_day(&now, -b, &d);
        int i = d.tm_mday - 1;
        if (i != day_excl && !day_date[i]) day_date[i] = ymd_of(&d);
    }
    // Mois : (annee, mois) le plus recent de chaque creneau ; le plus ancien
    // (mois suivant, l'an dernier) est ignore, comme pour les jours
    const int mon_excl = (now.tm_mon + 1) % 12;

    // Documents charges avant de prendre le verrou des mesures (lecture flash)
    if (!doc_get('h', today) || !doc_get('h', yesterday) || !doc_get('y', 0) ||
        !doc_get('m', (uint32_t)year) || !doc_get('m', (uint32_t)year - 1)) {
        docs_commit();
        return;
    }
    for (int i = 0; i < 31; i++) if (day_date[i]) doc_get('d', day_date[i] / 100);

    uint64_t ids[MAX_DEV];
    int nd = energy_history_list(ids, MAX_DEV);
    energy_history_lock();
    for (int k = 0; k < nd; k++) {
        const energy_history_t *h = energy_history_find(ids[k]);
        if (!h || !h->attr_count) continue;
        char id[20];
        snprintf(id, sizeof(id), "%016llX", (unsigned long long)ids[k]);

        // Heures : 0..hc du jour, hc+2..23 de la veille (hc+1, le plus ancien,
        // est deja archive : voir les jours)
        cJSON *ht = obj_child(obj_child(doc_get('h', today), "e"), id);
        cJSON *hy = obj_child(obj_child(doc_get('h', yesterday), "e"), id);
        for (int s = 0; s < 24; s++) {
            if (s == (hc + 1) % 24) continue;
            if (s <= hc) { if (ht) put_slot(ht, s, h, &h->hours, s); }
            else if (hy) put_slot(hy, s, h, &h->hours, s);
        }
        // Jours
        for (int i = 0; i < 31; i++) {
            if (!day_date[i]) continue;
            cJSON *dd = obj_child(obj_child(doc_get('d', day_date[i] / 100), "e"), id);
            if (dd) put_slot(dd, i + 1, h, &h->days, i);
        }
        // Mois
        for (int m = 0; m < 12; m++) {
            if (m == mon_excl) continue;
            int y = m <= now.tm_mon ? year : year - 1;
            cJSON *mo = obj_child(obj_child(doc_get('m', (uint32_t)y), "e"), id);
            if (mo) put_slot(mo, m + 1, h, &h->months, m);
        }
        // Annees
        cJSON *yr = obj_child(obj_child(doc_get('y', 0), "e"), id);
        for (int i = 0; yr && i < ENERGY_YEARS; i++) {
            int y = h->base_year + i;
            if (y > year) continue;
            put_slot(yr, y, h, &h->years, i);
        }
    }
    energy_history_unlock();

    // Temperature, presence...
    energy_archive_provider_t prov[MAX_PROV];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int np = s_nprov;
    memcpy(prov, s_prov, sizeof(prov));
    xSemaphoreGive(s_lock);
    const uint32_t dates[2] = { yesterday, today };
    for (int k = 0; k < 2; k++) {
        cJSON *hf = doc_get('h', dates[k]);
        cJSON *df = doc_get('d', dates[k] / 100);
        for (int i = 0; hf && df && i < np; i++) prov[i](dates[k], hf, df);
    }
    docs_commit();
}

// ----------------------------------------------------------------------------
// Purge, effacement, inventaire
// ----------------------------------------------------------------------------

static bool parse_name(const char *name, char *kind, uint32_t *key)
{
    size_t l = strlen(name);
    if (l < 7 || strcmp(name + l - 5, ".json") != 0) return false;
    if (strcmp(name, "yr.json") == 0) { *kind = 'y'; *key = 0; return true; }
    if (name[2] != '_') return false;
    char *end = NULL;
    unsigned long v = strtoul(name + 3, &end, 10);
    if (!end || strcmp(end, ".json") != 0) return false;
    size_t digits = (size_t)(end - (name + 3));
    if (!strncmp(name, "hr", 2) && digits == 8) *kind = 'h';
    else if (!strncmp(name, "dy", 2) && digits == 6) *kind = 'd';
    else if (!strncmp(name, "mo", 2) && digits == 4) *kind = 'm';
    else return false;
    *key = (uint32_t)v;
    return true;
}

/** Fichiers horaires de plus de HR_KEEP_DAYS jours supprimes (une fois par jour). */
static void purge(uint32_t today)
{
    int32_t limit = energy_archive_daynum(today) - HR_KEEP_DAYS;
    DIR *dir = opendir(ARCH_DIR);
    if (!dir) return;
    struct dirent *e;
    int n = 0;
    char path[300];
    while ((e = readdir(dir)) != NULL) {
        char kind;
        uint32_t key;
        if (!parse_name(e->d_name, &kind, &key) || kind != 'h') continue;
        if (energy_archive_daynum(key) >= limit) continue;
        snprintf(path, sizeof(path), ARCH_DIR "/%s", e->d_name);
        if (unlink(path) == 0) {
            n++;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            bit_set(s_hr_bits, key, false);
            xSemaphoreGive(s_lock);
        }
    }
    closedir(dir);
    if (n) ESP_LOGI(TAG, "%d journee(s) horaire(s) de plus de %d jours supprimee(s)", n, HR_KEEP_DAYS);
}

/** Retire l'energie de tous les fichiers (historique efface) ; temperature et presence restent. */
static void clear_energy(void)
{
    DIR *dir = opendir(ARCH_DIR);
    if (!dir) return;
    // Noms lus d'abord : le repertoire change pendant les ecritures
    int cap = 64, n = 0;
    char (*names)[24] = (char (*)[24])heap_caps_malloc(cap * 24, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    struct dirent *e;
    while (names && (e = readdir(dir)) != NULL) {
        char kind;
        uint32_t key;
        if (!parse_name(e->d_name, &kind, &key) || strlen(e->d_name) >= 24) continue;
        if (n == cap) {
            cap *= 2;
            char (*nn)[24] = (char (*)[24])heap_caps_realloc(names, cap * 24, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!nn) break;
            names = nn;
        }
        strlcpy(names[n++], e->d_name, 24);
    }
    closedir(dir);
    for (int i = 0; i < n; i++) {
        char kind;
        uint32_t key;
        parse_name(names[i], &kind, &key);
        cJSON *root = energy_archive_load(kind, key);
        if (!root) continue;
        cJSON_DeleteItemFromObject(root, "e");
        char path[48];
        path_of(kind, key, path, sizeof(path));
        if (!root->child) {
            unlink(path);
        } else {
            char *txt = cJSON_PrintUnformatted(root);
            if (txt) write_text(path, txt);
            free(txt);
        }
        cJSON_Delete(root);
    }
    free(names);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_dy_bits, 0, sizeof(s_dy_bits));
    xSemaphoreGive(s_lock);
    ESP_LOGW(TAG, "Energie retiree de %d fichier(s) d'archive", n);
}

static void scan_inventory(void)
{
    DIR *dir = opendir(ARCH_DIR);
    if (!dir) return;
    struct dirent *e;
    int nh = 0, nd = 0;
    while ((e = readdir(dir)) != NULL) {
        char kind;
        uint32_t key;
        if (!parse_name(e->d_name, &kind, &key)) continue;
        if (kind == 'h') {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            bit_set(s_hr_bits, key, true);
            xSemaphoreGive(s_lock);
            nh++;
        } else if (kind == 'd') {
            cJSON *root = energy_archive_load('d', key);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            note_inventory('d', key, root);
            xSemaphoreGive(s_lock);
            cJSON_Delete(root);
            nd++;
        }
    }
    closedir(dir);
    ESP_LOGI(TAG, "Archive : %d journee(s) horaire(s), %d mois", nh, nd);
}

// ----------------------------------------------------------------------------
// API
// ----------------------------------------------------------------------------

void energy_archive_register(energy_archive_provider_t fn)
{
    if (!fn) return;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool dup = false;
    for (int i = 0; i < s_nprov; i++) if (s_prov[i] == fn) dup = true;
    if (!dup && s_nprov < MAX_PROV) s_prov[s_nprov++] = fn;
    xSemaphoreGive(s_lock);
}

void energy_archive_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock || s_ready) return;
    scan_inventory();
    s_ready = true;
}

static void run(bool force)
{
    if (!s_ready || !ntp_sync_is_synced()) return;
    struct tm now;
    if (ntp_sync_get_local_time(&now) != ESP_OK) return;
    uint32_t today = ymd_of(&now);
    int32_t dn = energy_archive_daynum(today);
    if (s_clear_req) {
        s_clear_req = false;
        clear_energy();
        force = true;
    }
    int32_t hour_key = dn * 24 + now.tm_hour;
    int64_t t = esp_timer_get_time();
    if (force || hour_key != s_last_hour_key || t - s_last_write >= WRITE_PERIOD_US) {
        s_last_hour_key = hour_key;
        s_last_write = t;
        mirror();
    }
    if (dn != s_last_purge_day) {
        s_last_purge_day = dn;
        purge(today);
    }
}

void energy_archive_tick(void)       { run(false); }
void energy_archive_flush(void)      { run(true); }
void energy_archive_request_clear(void) { s_clear_req = true; }

/** Plages de jours consecutifs d'un inventaire. */
static void add_ranges(cJSON *arr, const uint8_t *bits)
{
    int start = -1;
    for (int i = 0; i <= BITS_SPAN; i++) {
        bool on = i < BITS_SPAN && (bits[i >> 3] & (1u << (i & 7)));
        if (on && start < 0) start = i;
        if (!on && start >= 0) {
            cJSON *r = cJSON_CreateArray();
            cJSON_AddItemToArray(r, cJSON_CreateNumber(energy_archive_ymd(BITS_BASE + start)));
            cJSON_AddItemToArray(r, cJSON_CreateNumber(energy_archive_ymd(BITS_BASE + i - 1)));
            cJSON_AddItemToArray(arr, r);
            start = -1;
        }
    }
}

char *energy_archive_days_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *h = cJSON_AddArrayToObject(root, "hour");
    cJSON *d = cJSON_AddArrayToObject(root, "day");
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        add_ranges(h, s_hr_bits);
        add_ranges(d, s_dy_bits);
        xSemaphoreGive(s_lock);
    }
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}
