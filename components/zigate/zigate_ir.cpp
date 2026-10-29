// ============================================================================
// ZiGate IR - telecommandes infrarouges Zosung (voir zigate_ir.h)
// ============================================================================

#include "zigate_ir.h"
#include "zigate_driver.h"
#include "zigate_devices.h"

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "ZIGATE_IR";

#define IR_DIR          "/littlefs/ir"
#define IR_MAX_DEV      8
#define IR_RX_MAX       2048        // code appris : quelques centaines d'octets
#define IR_FRAG_RX      0x38        // taille demandee par fragment (apprentissage)
#define IR_FRAG_TX      0x32        // taille d'un fragment emis
#define IR_CL_CONTROL   0xE004
#define IR_CL_TRANSMIT  0xED00
#define IR_NAME_LEN     ZIGATE_IR_NAME_LEN

typedef struct {
    char  name[IR_NAME_LEN];
    char *code;                     // base64, PSRAM
} ir_code_t;

typedef struct {
    uint64_t  ieee;
    bool      learning;
    uint32_t  learned_s;            // instant de l'apprentissage (uptime)
    char     *learned;              // dernier code appris (base64), PSRAM
    // reception d'un code appris
    uint8_t  *rx;
    uint32_t  rx_len, rx_pos;
    uint16_t  rx_seq;
    // emission
    uint8_t  *tx;
    uint32_t  tx_len;
    uint16_t  tx_seq;
    bool      tx_done;
    ir_code_t codes[ZIGATE_IR_MAX_CODES];
    int       n_codes;
} ir_dev_t;

static ir_dev_t         *s_dev;     // PSRAM, IR_MAX_DEV entrees
static SemaphoreHandle_t s_mx;
static uint16_t          s_seq = 1;
static uint8_t           s_zcl_seq;
static zigate_ir_change_cb_t s_change_cb;

void zigate_ir_set_change_cb(zigate_ir_change_cb_t cb) { s_change_cb = cb; }


void zigate_ir_prepare(void)
{
    if (!s_mx) {
        static StaticSemaphore_t buf;
        s_mx = xSemaphoreCreateMutexStatic(&buf);
    }
    if (!s_dev) s_dev = (ir_dev_t *)heap_caps_calloc(IR_MAX_DEV, sizeof(ir_dev_t), MALLOC_CAP_SPIRAM);
}

static bool lock(void)
{
    if (!s_mx || !s_dev) return false;   // zigate_ir_prepare() appele par zigate_init()
    return xSemaphoreTake(s_mx, pdMS_TO_TICKS(2000)) == pdTRUE;
}

static void unlock(void) { xSemaphoreGive(s_mx); }

static char *ps_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (d) memcpy(d, s, n);
    return d;
}

/** Entree d'un appareil (creee si besoin). Sous verrou. */
static ir_dev_t *dev_get(uint64_t ieee, bool create)
{
    ir_dev_t *free_slot = NULL;
    for (int i = 0; i < IR_MAX_DEV; i++) {
        if (s_dev[i].ieee == ieee) return &s_dev[i];
        if (!s_dev[i].ieee && !free_slot) free_slot = &s_dev[i];
    }
    if (!create || !free_slot) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->ieee = ieee;
    return free_slot;
}

// ----------------------------------------------------------------------------
// Trames
// ----------------------------------------------------------------------------

int zigate_ir_code_names(uint64_t ieee, char (*names)[ZIGATE_IR_NAME_LEN], int max)
{
    int n = 0;
    if (!names || max <= 0 || !lock()) return 0;
    ir_dev_t *d = dev_get(ieee, false);
    for (int i = 0; d && i < d->n_codes && n < max; i++) strlcpy(names[n++], d->codes[i].name, ZIGATE_IR_NAME_LEN);
    unlock();
    return n;
}

static void put16(uint8_t *b, int *n, uint16_t v) { b[(*n)++] = (uint8_t)v; b[(*n)++] = (uint8_t)(v >> 8); }
static void put32(uint8_t *b, int *n, uint32_t v) { put16(b, n, (uint16_t)v); put16(b, n, (uint16_t)(v >> 16)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)get16(p) | ((uint32_t)get16(p + 2) << 16); }

/** Commande specifique au cluster, client -> serveur, sans reponse par defaut. */
static esp_err_t send_cmd(uint16_t sa, uint8_t ep, uint16_t cluster, uint8_t cmd,
                          const uint8_t *payload, int plen)
{
    uint8_t z[100];
    if (plen + 3 > (int)sizeof(z)) return ESP_ERR_INVALID_SIZE;
    z[0] = 0x11;                                         // specifique cluster, pas de reponse par defaut
    z[1] = s_zcl_seq++;
    z[2] = cmd;
    memcpy(z + 3, payload, plen);
    return zigate_send_raw_aps(sa, 0x01, ep, cluster, 0x0104, 0x1E, z, (uint8_t)(plen + 3));
}

static bool dev_addr(uint64_t ieee, uint16_t *sa, uint8_t *ep)
{
    const zigate_device_t *d = zigate_devices_find_by_ieee(ieee);
    if (!d) return false;
    *sa = d->short_addr;
    *ep = d->endpoint ? d->endpoint : 1;
    return true;
}

static uint8_t sum8(const uint8_t *p, int n)
{
    uint32_t s = 0;
    for (int i = 0; i < n; i++) s += p[i];
    return (uint8_t)s;
}

// ----------------------------------------------------------------------------
// Apprentissage et emission
// ----------------------------------------------------------------------------

esp_err_t zigate_ir_learn(uint64_t ieee, bool on)
{
    uint16_t sa;
    uint8_t ep;
    if (!dev_addr(ieee, &sa, &ep)) return ESP_ERR_NOT_FOUND;
    // {"study":0} : debut d'apprentissage, {"study":1} : fin (zigbee2mqtt)
    const char *js = on ? "{\"study\":0}" : "{\"study\":1}";
    esp_err_t e = send_cmd(sa, ep, IR_CL_CONTROL, 0x00, (const uint8_t *)js, (int)strlen(js));
    if (e == ESP_OK && lock()) {
        ir_dev_t *d = dev_get(ieee, true);
        if (d) d->learning = on;
        unlock();
    }
    ESP_LOGI(TAG, "0x%04X : apprentissage %s", sa, on ? "demarre" : "arrete");
    return e;
}

esp_err_t zigate_ir_send_code(uint64_t ieee, const char *b64)
{
    if (!b64 || !b64[0] || strlen(b64) > 4096) return ESP_ERR_INVALID_ARG;
    uint16_t sa;
    uint8_t ep;
    if (!dev_addr(ieee, &sa, &ep)) return ESP_ERR_NOT_FOUND;
    // Message attendu par l'emetteur (zosung_ir_code_to_send de zigbee2mqtt)
    size_t n = strlen(b64) + 96;
    char *msg = (char *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!msg) return ESP_ERR_NO_MEM;
    int len = snprintf(msg, n, "{\"key_num\":1,\"delay\":300,\"key1\":{\"num\":1,\"freq\":38000,"
                               "\"type\":1,\"key_code\":\"%s\"}}", b64);
    if (!lock()) { heap_caps_free(msg); return ESP_ERR_TIMEOUT; }
    ir_dev_t *d = dev_get(ieee, true);
    if (!d) { unlock(); heap_caps_free(msg); return ESP_ERR_NO_MEM; }
    if (d->tx) heap_caps_free(d->tx);
    d->tx = (uint8_t *)msg;
    d->tx_len = (uint32_t)len;
    d->tx_seq = s_seq++;
    d->tx_done = false;
    uint16_t seq = d->tx_seq;
    unlock();

    uint8_t p[16];
    int k = 0;
    put16(p, &k, seq);
    put32(p, &k, (uint32_t)len);
    put32(p, &k, 0);                                     // unk1
    put16(p, &k, IR_CL_CONTROL);                         // unk2
    p[k++] = 0x01;                                       // unk3
    p[k++] = 0x02;                                       // cmd : emission
    put16(p, &k, 0);                                     // unk4
    ESP_LOGI(TAG, "0x%04X : emission d'un code (%d octets, seq %u)", sa, len, seq);
    return send_cmd(sa, ep, IR_CL_TRANSMIT, 0x00, p, k);
}

esp_err_t zigate_ir_send_saved(uint64_t ieee, int index)
{
    char *code = NULL;
    if (!lock()) return ESP_ERR_TIMEOUT;
    ir_dev_t *d = dev_get(ieee, false);
    if (d && index >= 0 && index < d->n_codes && d->codes[index].code) code = ps_strdup(d->codes[index].code);
    unlock();
    if (!code) return ESP_ERR_NOT_FOUND;
    esp_err_t e = zigate_ir_send_code(ieee, code);
    heap_caps_free(code);
    return e;
}

// ----------------------------------------------------------------------------
// Trames recues
// ----------------------------------------------------------------------------

static void learned_complete(ir_dev_t *d)
{
    size_t olen = 0;
    mbedtls_base64_encode(NULL, 0, &olen, d->rx, d->rx_len);
    char *b = (char *)heap_caps_malloc(olen + 1, MALLOC_CAP_SPIRAM);
    if (b && mbedtls_base64_encode((unsigned char *)b, olen + 1, &olen, d->rx, d->rx_len) == 0) {
        b[olen] = '\0';
        if (d->learned) heap_caps_free(d->learned);
        d->learned = b;
        d->learned_s = zigate_uptime_s();
        d->learning = false;
        ESP_LOGI(TAG, "Code appris : %lu octets", (unsigned long)d->rx_len);
    } else if (b) {
        heap_caps_free(b);
    }
    heap_caps_free(d->rx);
    d->rx = NULL;
}

void zigate_ir_handle_frame(uint16_t sa, uint8_t ep, uint16_t cluster, const uint8_t *zcl, int len)
{
    if (cluster != IR_CL_TRANSMIT && cluster != IR_CL_CONTROL) return;
    if (len < 3) return;
    int i = 0;
    uint8_t fc = zcl[i++];
    if (fc & 0x04) i += 2;                               // code fabricant
    if (i + 2 > len) return;
    i++;                                                 // sequence ZCL
    uint8_t cmd = zcl[i++];
    const uint8_t *p = zcl + i;
    int pl = len - i;
    if (cluster == IR_CL_CONTROL) {
        ESP_LOGI(TAG, "0x%04X : accuse d'apprentissage (cmd 0x%02X)", sa, cmd);
        return;
    }
    const zigate_device_t *zd = zigate_devices_find_by_short(sa);
    if (!zd || !lock()) return;
    ir_dev_t *d = dev_get(zd->ieee_addr, true);
    uint8_t out[80];
    int k = 0;
    bool send = false;
    uint8_t out_cmd = 0;

    switch (cmd) {
    case 0x00:                                           // code appris annonce : seq length unk1 unk2 unk3 cmd unk4
        if (pl < 16 || !d) break;
        {
            uint16_t seq = get16(p);
            uint32_t total = get32(p + 2);
            if (total == 0 || total > IR_RX_MAX) { ESP_LOGW(TAG, "Code annonce de %lu octets refuse", (unsigned long)total); break; }
            if (d->rx) heap_caps_free(d->rx);
            d->rx = (uint8_t *)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
            if (!d->rx) break;
            d->rx_len = total;
            d->rx_pos = 0;
            d->rx_seq = seq;
            // Accuse (0x01) : zero puis l'annonce recopiee, puis demande du premier fragment
            out[k++] = 0x00;
            memcpy(out + k, p, 16);
            k += 16;
            unlock();
            send_cmd(sa, ep, IR_CL_TRANSMIT, 0x01, out, k);
            k = 0;
            put16(out, &k, seq);
            put32(out, &k, 0);
            out[k++] = IR_FRAG_RX;
            send_cmd(sa, ep, IR_CL_TRANSMIT, 0x02, out, k);
            return;
        }

    case 0x01:                                           // accuse de notre annonce
        ESP_LOGI(TAG, "0x%04X : emission acceptee", sa);
        break;

    case 0x02:                                           // l'appareil demande un fragment de notre code
        if (pl < 7 || !d || !d->tx) break;
        {
            uint16_t seq = get16(p);
            uint32_t pos = get32(p + 2);
            uint8_t max = p[6];
            if (seq != d->tx_seq || pos >= d->tx_len) break;
            uint32_t n = d->tx_len - pos;
            uint32_t lim = (max && max < IR_FRAG_TX) ? max : IR_FRAG_TX;
            if (n > lim) n = lim;
            out[k++] = 0x00;
            put16(out, &k, seq);
            put32(out, &k, pos);
            out[k++] = (uint8_t)n;                       // chaine d'octets : longueur puis donnees
            memcpy(out + k, d->tx + pos, n);
            k += n;
            out[k++] = sum8(d->tx + pos, (int)n);
            out_cmd = 0x03;
            send = true;
        }
        break;

    case 0x03:                                           // fragment du code appris
        if (pl < 9 || !d || !d->rx) break;
        {
            uint16_t seq = get16(p + 1);
            uint32_t pos = get32(p + 3);
            uint8_t n = p[7];
            if (seq != d->rx_seq || 8 + n + 1 > pl) break;
            const uint8_t *frag = p + 8;
            if (sum8(frag, n) != p[8 + n]) {
                ESP_LOGW(TAG, "0x%04X : fragment %lu corrompu, redemande", sa, (unsigned long)pos);
            } else if (pos == d->rx_pos && n <= d->rx_len - d->rx_pos) {
                // Fragment attendu seulement (position venue de la radio : bornee
                // sans debordement possible, pas de trou dans le code)
                memcpy(d->rx + pos, frag, n);
                d->rx_pos += n;
            }
            if (d->rx_pos < d->rx_len) {
                put16(out, &k, seq);
                put32(out, &k, d->rx_pos);
                out[k++] = IR_FRAG_RX;
                out_cmd = 0x02;
            } else {
                learned_complete(d);
                out[k++] = 0x00;
                put16(out, &k, seq);
                put16(out, &k, 0);
                out_cmd = 0x04;
            }
            send = true;
        }
        break;

    case 0x04:                                           // fin de notre emission
        if (pl < 3 || !d) break;
        {
            uint16_t seq = get16(p + 1);
            put16(out, &k, seq);
            put16(out, &k, 0);
            out_cmd = 0x05;
            send = true;
            d->tx_done = true;
            if (d->tx) { heap_caps_free(d->tx); d->tx = NULL; }
            ESP_LOGI(TAG, "0x%04X : code emis", sa);
        }
        break;

    case 0x05:                                           // fin du transfert du code appris
        break;

    default:
        ESP_LOGD(TAG, "0x%04X : commande IR 0x%02X ignoree", sa, cmd);
        break;
    }
    unlock();
    if (send) send_cmd(sa, ep, IR_CL_TRANSMIT, out_cmd, out, k);
}

// ----------------------------------------------------------------------------
// Codes enregistres : /littlefs/ir/<IEEE>.json = [{"name":"..","code":".."}]
// ----------------------------------------------------------------------------

static void file_path(uint64_t ieee, char *out, size_t len)
{
    snprintf(out, len, IR_DIR "/%016llX.json", (unsigned long long)ieee);
}

static void free_codes(ir_dev_t *d)
{
    for (int i = 0; i < d->n_codes; i++) if (d->codes[i].code) heap_caps_free(d->codes[i].code);
    d->n_codes = 0;
}

/** Ecrit les codes d'un appareil (hors verrou : copie JSON preparee sous verrou). */
static esp_err_t write_codes(uint64_t ieee, const char *json)
{
    mkdir(IR_DIR, 0755);
    char path[48], tmp[56];
    file_path(ieee, path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return ESP_FAIL;
    size_t n = strlen(json);
    bool ok = fwrite(json, 1, n, f) == n;
    fclose(f);
    if (!ok) { remove(tmp); return ESP_FAIL; }
    remove(path);
    return rename(tmp, path) == 0 ? ESP_OK : ESP_FAIL;
}

static char *codes_json(const ir_dev_t *d)
{
    cJSON *a = cJSON_CreateArray();
    for (int i = 0; i < d->n_codes; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", d->codes[i].name);
        cJSON_AddStringToObject(o, "code", d->codes[i].code ? d->codes[i].code : "");
        cJSON_AddItemToArray(a, o);
    }
    char *s = cJSON_PrintUnformatted(a);
    cJSON_Delete(a);
    return s;
}

void zigate_ir_init(void)
{
    zigate_ir_prepare();
    DIR *dir = opendir(IR_DIR);
    if (!dir) return;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        size_t nl = strlen(de->d_name);
        if (nl != 21 || strcmp(de->d_name + 16, ".json") != 0) continue;
        uint64_t ieee = strtoull(de->d_name, NULL, 16);
        char path[300];
        snprintf(path, sizeof(path), IR_DIR "/%s", de->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = (n > 0 && n < 256 * 1024) ? (char *)heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM) : NULL;
        size_t got = buf ? fread(buf, 1, n, f) : 0;
        fclose(f);
        if (!buf) continue;
        buf[got] = '\0';
        cJSON *a = cJSON_Parse(buf);
        heap_caps_free(buf);
        if (cJSON_IsArray(a) && lock()) {
            ir_dev_t *d = dev_get(ieee, true);
            if (d) {
                free_codes(d);
                const cJSON *o;
                cJSON_ArrayForEach(o, a) {
                    if (d->n_codes >= ZIGATE_IR_MAX_CODES) break;
                    const cJSON *nm = cJSON_GetObjectItem(o, "name"), *cd = cJSON_GetObjectItem(o, "code");
                    if (!cJSON_IsString(nm) || !cJSON_IsString(cd)) continue;
                    ir_code_t *c = &d->codes[d->n_codes];
                    strlcpy(c->name, nm->valuestring, sizeof(c->name));
                    c->code = ps_strdup(cd->valuestring);
                    if (c->code) d->n_codes++;
                }
                ESP_LOGI(TAG, "%016llX : %d code(s) IR", (unsigned long long)ieee, d->n_codes);
            }
            unlock();
            if (d && s_change_cb) s_change_cb(ieee);
        }
        cJSON_Delete(a);
    }
    closedir(dir);
}

esp_err_t zigate_ir_save(uint64_t ieee, const char *name, const char *b64)
{
    if (!name || !name[0] || !b64 || !b64[0] || strlen(b64) > 4096) return ESP_ERR_INVALID_ARG;
    if (!lock()) return ESP_ERR_TIMEOUT;
    ir_dev_t *d = dev_get(ieee, true);
    if (!d) { unlock(); return ESP_ERR_NO_MEM; }
    int idx = -1;
    for (int i = 0; i < d->n_codes; i++) if (strcmp(d->codes[i].name, name) == 0) idx = i;
    if (idx < 0) {
        if (d->n_codes >= ZIGATE_IR_MAX_CODES) { unlock(); return ESP_ERR_NO_MEM; }
        idx = d->n_codes++;
        memset(&d->codes[idx], 0, sizeof(d->codes[idx]));
        strlcpy(d->codes[idx].name, name, sizeof(d->codes[idx].name));
    }
    char *c = ps_strdup(b64);
    if (!c) { unlock(); return ESP_ERR_NO_MEM; }
    if (d->codes[idx].code) heap_caps_free(d->codes[idx].code);
    d->codes[idx].code = c;
    char *js = codes_json(d);
    unlock();
    esp_err_t e = js ? write_codes(ieee, js) : ESP_ERR_NO_MEM;
    free(js);
    if (s_change_cb) s_change_cb(ieee);
    return e;
}

esp_err_t zigate_ir_delete(uint64_t ieee, int index)
{
    if (!lock()) return ESP_ERR_TIMEOUT;
    ir_dev_t *d = dev_get(ieee, false);
    if (!d || index < 0 || index >= d->n_codes) { unlock(); return ESP_ERR_NOT_FOUND; }
    if (d->codes[index].code) heap_caps_free(d->codes[index].code);
    memmove(&d->codes[index], &d->codes[index + 1], sizeof(ir_code_t) * (d->n_codes - index - 1));
    d->n_codes--;
    char *js = codes_json(d);
    unlock();
    esp_err_t e = js ? write_codes(ieee, js) : ESP_ERR_NO_MEM;
    free(js);
    if (s_change_cb) s_change_cb(ieee);
    return e;
}

char *zigate_ir_status_json(uint64_t ieee)
{
    cJSON *r = cJSON_CreateObject();
    if (lock()) {
        ir_dev_t *d = dev_get(ieee, false);
        cJSON_AddBoolToObject(r, "learning", d && d->learning);
        if (d && d->learned) {
            cJSON_AddStringToObject(r, "learned", d->learned);
            cJSON_AddNumberToObject(r, "learnedAgo", zigate_uptime_s() - d->learned_s);
        } else {
            cJSON_AddNullToObject(r, "learned");
        }
        cJSON_AddBoolToObject(r, "sending", d && d->tx != NULL);
        cJSON_AddStringToObject(r, "lastSend", !d ? "" : (d->tx ? "pending" : (d->tx_done ? "ok" : "")));
        cJSON *a = cJSON_AddArrayToObject(r, "codes");
        for (int i = 0; d && i < d->n_codes; i++) {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "i", i);
            cJSON_AddStringToObject(o, "name", d->codes[i].name);
            cJSON_AddStringToObject(o, "code", d->codes[i].code ? d->codes[i].code : "");
            cJSON_AddItemToArray(a, o);
        }
        unlock();
    }
    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    return s;
}
