// ============================================================================
// ZiGate OTA - mise a jour des appareils Zigbee (voir zigate_ota.h)
// ============================================================================

#include "zigate_ota.h"
#include "zigate_devices.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "ZIGATE_OTA";

#define OTA_FILE_ID        0x0BEEF11E   // identifiant d'un fichier OTA Zigbee
#define OTA_HEADER_MIN     56           // en-tete jusqu'a "total image size"
#define OTA_BLOCK_MAX      128          // octets de donnees par bloc au plus

// Champs de l'en-tete (petit-boutiste dans le fichier)
typedef struct {
    uint32_t identifier;
    uint16_t header_version;
    uint16_t header_length;
    uint16_t control_field;
    uint16_t manu_code;
    uint16_t image_type;
    uint32_t file_version;
    uint16_t stack_version;
    uint8_t  header_string[32];
    uint32_t total_image;
    uint8_t  security_cred;
    uint64_t upgrade_dest;
    uint16_t min_hw;
    uint16_t max_hw;
} ota_header_t;

static SemaphoreHandle_t s_lock;
static uint8_t *s_image;            // fichier complet, en PSRAM
static size_t   s_image_len;
static ota_header_t s_hdr;
static zigate_ota_status_t s_st;
static uint32_t s_event_s;          // instant du dernier evenement
static uint64_t s_cancelled;        // appareil dont la mise a jour a ete annulee

void zigate_ota_prepare(void)
{
    if (!s_lock) {
        static StaticSemaphore_t buf;
        s_lock = xSemaphoreCreateMutexStatic(&buf);
    }
}

static bool lock(void)
{
    zigate_ota_prepare();                // deja cree par zigate_init() en temps normal
    return xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) == pdTRUE;
}

static void unlock(void) { xSemaphoreGive(s_lock); }

static void set_state(zigate_ota_state_t st, const char *msg)
{
    s_st.state = st;
    strlcpy(s_st.msg, msg ? msg : "", sizeof(s_st.msg));
    s_event_s = zigate_uptime_s();
}

static void free_image(void)
{
    if (s_image) heap_caps_free(s_image);
    s_image = NULL;
    s_image_len = 0;
}

void zigate_ota_file_path(uint64_t ieee, char *out, size_t len)
{
    snprintf(out, len, "/littlefs/ota/%016llX.ota", (unsigned long long)ieee);
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static bool parse_header(const uint8_t *f, size_t len, ota_header_t *h)
{
    if (len < OTA_HEADER_MIN) return false;
    memset(h, 0, sizeof(*h));
    h->identifier     = le32(f + 0);
    h->header_version = le16(f + 4);
    h->header_length  = le16(f + 6);
    h->control_field  = le16(f + 8);
    h->manu_code      = le16(f + 10);
    h->image_type     = le16(f + 12);
    h->file_version   = le32(f + 14);
    h->stack_version  = le16(f + 18);
    memcpy(h->header_string, f + 20, 32);
    h->total_image    = le32(f + 52);
    // Champs facultatifs, presents selon le champ de controle et dans cet
    // ordre : bit 0 version de securite, bit 1 destinataire, bit 2 versions
    // materielles min / max. Absents : zeros, bit non annonce.
    size_t o = 56, end = h->header_length < len ? h->header_length : len;
    if ((h->control_field & 0x01) && o + 1 <= end) { h->security_cred = f[o]; o += 1; }
    if ((h->control_field & 0x02) && o + 8 <= end) {
        for (int i = 7; i >= 0; i--) h->upgrade_dest = (h->upgrade_dest << 8) | f[o + i];
        o += 8;
    }
    if ((h->control_field & 0x04) && o + 4 <= end) { h->min_hw = le16(f + o); h->max_hw = le16(f + o + 2); }
    return h->identifier == OTA_FILE_ID && h->header_length >= OTA_HEADER_MIN &&
           h->total_image >= h->header_length;
}

/** Charge le fichier d'un appareil en PSRAM (lecture flash : pile en RAM interne). */
static esp_err_t load_file(uint64_t ieee, char *err, size_t err_len)
{
    char path[48];
    zigate_ota_file_path(ieee, path, sizeof(path));
    struct stat stt;
    if (stat(path, &stt) != 0 || stt.st_size <= 0) {
        snprintf(err, err_len, "Aucun fichier OTA pour cet appareil");
        return ESP_ERR_NOT_FOUND;
    }
    if (stt.st_size > ZIGATE_OTA_MAX_FILE) {
        snprintf(err, err_len, "Fichier OTA trop gros (1 Mo max)");
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t *buf = (uint8_t *)heap_caps_malloc(stt.st_size, MALLOC_CAP_SPIRAM);
    if (!buf) { snprintf(err, err_len, "Memoire insuffisante"); return ESP_ERR_NO_MEM; }
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(buf, 1, stt.st_size, f) : 0;
    if (f) fclose(f);
    ota_header_t h;
    if (n != (size_t)stt.st_size || !parse_header(buf, n, &h)) {
        heap_caps_free(buf);
        snprintf(err, err_len, "Fichier OTA Zigbee invalide (en-tete 0x0BEEF11E attendu)");
        return ESP_ERR_INVALID_ARG;
    }
    if (h.total_image > n) {
        heap_caps_free(buf);
        snprintf(err, err_len, "Fichier OTA tronque (%u / %u octets)", (unsigned)n, (unsigned)h.total_image);
        return ESP_ERR_INVALID_SIZE;
    }
    free_image();
    s_image = buf;
    s_image_len = n;
    s_hdr = h;
    s_st.ieee = ieee;
    s_st.total = h.total_image;
    s_st.file_version = h.file_version;
    s_st.image_type = h.image_type;
    s_st.manu_code = h.manu_code;
    return ESP_OK;
}

static void put16(uint8_t *b, int *n, uint16_t v) { b[(*n)++] = v >> 8; b[(*n)++] = (uint8_t)v; }
static void put32(uint8_t *b, int *n, uint32_t v) { put16(b, n, v >> 16); put16(b, n, (uint16_t)v); }

/** 0x0500 Load New Image puis 0x0505 Image Notify (sendOtaLoadNewImage / sendOtaImageNotify). */
static void send_load_and_notify(uint16_t sa)
{
    uint8_t b[80];
    int n = 0;
    b[n++] = 0x02;                       // adressage court
    put16(b, &n, sa);
    put32(b, &n, s_hdr.identifier);
    put16(b, &n, s_hdr.header_version);
    put16(b, &n, s_hdr.header_length);
    put16(b, &n, s_hdr.control_field);
    put16(b, &n, s_hdr.manu_code);
    put16(b, &n, s_hdr.image_type);
    put32(b, &n, s_hdr.file_version);
    put16(b, &n, s_hdr.stack_version);
    memcpy(b + n, s_hdr.header_string, 32);
    n += 32;
    put32(b, &n, s_hdr.total_image);
    b[n++] = s_hdr.security_cred;
    put32(b, &n, (uint32_t)(s_hdr.upgrade_dest >> 32));
    put32(b, &n, (uint32_t)s_hdr.upgrade_dest);
    put16(b, &n, s_hdr.min_hw);
    put16(b, &n, s_hdr.max_hw);
    zigate_send_command(0x0500, b, (uint16_t)n);

    n = 0;
    b[n++] = 0x02;
    put16(b, &n, sa);
    b[n++] = 0x01;                       // endpoint source
    b[n++] = 0x01;                       // endpoint destination
    b[n++] = 0x00;                       // notification : jitter seul
    put32(b, &n, s_hdr.file_version);
    put16(b, &n, s_hdr.image_type);
    put16(b, &n, s_hdr.manu_code);
    b[n++] = 0x00;                       // jitter
    zigate_send_command(0x0505, b, (uint16_t)n);
}

esp_err_t zigate_ota_start(uint64_t ieee, char *err, size_t err_len)
{
    const zigate_device_t *d = zigate_devices_find_by_ieee(ieee);
    if (!d) { snprintf(err, err_len, "Appareil Zigbee inconnu"); return ESP_ERR_NOT_FOUND; }
    uint16_t sa = d->short_addr;
    if (!lock()) { snprintf(err, err_len, "OTA occupee"); return ESP_ERR_TIMEOUT; }
    if (s_st.state == ZIGATE_OTA_TRANSFER && s_st.ieee != ieee && zigate_uptime_s() - s_event_s < 120) {
        unlock();
        snprintf(err, err_len, "Une autre mise a jour est en cours");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t e = load_file(ieee, err, err_len);
    if (e == ESP_OK) {
        s_cancelled = 0;
        s_st.short_addr = sa;
        s_st.offset = 0;
        s_st.pct = 0;
        s_st.end_status = 0;
        set_state(ZIGATE_OTA_NOTIFIED, "Image annoncee a l'appareil");
        send_load_and_notify(sa);
        ESP_LOGI(TAG, "OTA %016llX : image v0x%08lX type 0x%04X fab. 0x%04X, %lu octets",
                 (unsigned long long)ieee, (unsigned long)s_hdr.file_version, s_hdr.image_type,
                 s_hdr.manu_code, (unsigned long)s_hdr.total_image);
    }
    unlock();
    return e;
}

void zigate_ota_cancel(void)
{
    if (!lock()) return;
    // Les demandes suivantes de cet appareil recoivent ABORT (sinon l'image
    // serait rechargee et le transfert reprendrait)
    if (s_st.state == ZIGATE_OTA_NOTIFIED || s_st.state == ZIGATE_OTA_TRANSFER) s_cancelled = s_st.ieee;
    free_image();
    set_state(ZIGATE_OTA_IDLE, "");
    unlock();
}

void zigate_ota_get_status(zigate_ota_status_t *st)
{
    if (!st) return;
    if (!lock()) { memset(st, 0, sizeof(*st)); return; }
    *st = s_st;
    st->since_s = zigate_uptime_s() - s_event_s;
    unlock();
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)be16(p) << 16) | be16(p + 2); }

/** 0x8501 Image Block Request -> 0x0502 Image Block Response (sendOtaBlock). */
static void handle_block_request(const zigate_packet_t *pkt)
{
    // sqn(1) ep(1) cluster(2) mode(1) adresse(2) ieee(8) offset(4) version(4)
    // type(2) fabricant(2) delai(2) taille max(1) controle(1)
    if (pkt->length < 30) return;
    const uint8_t *p = pkt->payload;
    uint8_t  sqn = p[0];
    uint16_t sa = be16(p + 5);
    uint32_t offset = be32(p + 15);
    uint32_t version = be32(p + 19);
    uint16_t type = be16(p + 23);
    uint16_t manu = be16(p + 25);
    uint8_t  max_size = p[29];
    // L'adresse IEEE de la trame n'est valable que si le bit 0 du champ de
    // controle est mis : l'appareil est retrouve par son adresse courte
    const zigate_device_t *zd = zigate_devices_find_by_short(sa);
    uint64_t ieee = zd ? zd->ieee_addr : 0;
    if (!ieee) return;

    if (!lock()) return;
    bool busy_other = s_image && s_st.ieee != ieee && s_st.state == ZIGATE_OTA_TRANSFER &&
                      zigate_uptime_s() - s_event_s < 120;
    if (ieee == s_cancelled || busy_other) {
        // Mise a jour annulee, ou image deja servie a un autre appareil : ABORT
        uint8_t b[24];
        int n = 0;
        b[n++] = 0x02;
        put16(b, &n, sa);
        b[n++] = 0x01;
        b[n++] = 0x01;
        b[n++] = sqn;
        b[n++] = 0x95;
        put32(b, &n, offset);
        put32(b, &n, version);
        put16(b, &n, type);
        put16(b, &n, manu);
        b[n++] = 0;
        unlock();
        zigate_send_command(0x0502, b, (uint16_t)n);
        return;
    }
    // Apres un redemarrage du pont : l'image de l'appareil est rechargee a la
    // demande (tache de traitement, pile en RAM interne)
    if (!s_image || s_st.ieee != ieee) {
        char err[64];
        if (load_file(ieee, err, sizeof(err)) != ESP_OK) {
            unlock();
            ESP_LOGW(TAG, "OTA 0x8501 0x%04X : %s", sa, err);
            return;
        }
        s_st.short_addr = sa;
    }
    uint8_t b[24 + OTA_BLOCK_MAX];
    int n = 0;
    b[n++] = 0x02;
    put16(b, &n, sa);
    b[n++] = 0x01;
    b[n++] = 0x01;
    b[n++] = sqn;
    if (offset >= s_hdr.total_image || version != s_hdr.file_version) {
        // Bloc hors image ou autre version : abandon cote appareil
        b[n++] = 0x95;                   // ABORT
        put32(b, &n, offset);
        put32(b, &n, version);
        put16(b, &n, type);
        put16(b, &n, manu);
        b[n++] = 0;
        zigate_send_command(0x0502, b, (uint16_t)n);
        unlock();
        ESP_LOGW(TAG, "OTA 0x%04X : bloc refuse (offset %lu, version 0x%08lX)", sa,
                 (unsigned long)offset, (unsigned long)version);
        return;
    }
    uint32_t left = s_hdr.total_image - offset;
    uint8_t nb = max_size ? max_size : 64;
    if (nb > OTA_BLOCK_MAX) nb = OTA_BLOCK_MAX;
    if (left < nb) nb = (uint8_t)left;
    b[n++] = 0x00;                       // SUCCESS
    put32(b, &n, offset);
    put32(b, &n, version);
    put16(b, &n, type);
    put16(b, &n, manu);
    b[n++] = nb;
    memcpy(b + n, s_image + offset, nb);
    n += nb;
    zigate_send_command(0x0502, b, (uint16_t)n);

    s_st.short_addr = sa;
    s_st.offset = offset + nb;
    s_st.pct = (uint8_t)((uint64_t)s_st.offset * 100 / s_hdr.total_image);
    if (s_st.state != ZIGATE_OTA_TRANSFER) {
        ESP_LOGI(TAG, "OTA 0x%04X : debut du transfert (blocs de %u octets)", sa, nb);
    }
    set_state(ZIGATE_OTA_TRANSFER, "Transfert en cours");
    unlock();
}

/** 0x8503 Upgrade End Request -> 0x0504 Upgrade End Response (sendOtaEndResponse). */
static void handle_upgrade_end(const zigate_packet_t *pkt)
{
    // sqn(1) ep(1) cluster(2) mode(1) adresse(2) version(4) type(2) fabricant(2) statut(1)
    if (pkt->length < 16) return;
    const uint8_t *p = pkt->payload;
    uint8_t  sqn = p[0];
    uint8_t  ep = p[1];
    uint16_t sa = be16(p + 5);
    uint32_t version = be32(p + 7);
    uint16_t type = be16(p + 11);
    uint16_t manu = be16(p + 13);
    uint8_t  status = p[15];

    if (!lock()) return;
    s_st.end_status = status;
    if (status == 0x00) {
        uint8_t b[24];
        int n = 0;
        b[n++] = 0x02;
        put16(b, &n, sa);
        b[n++] = 0x01;
        b[n++] = 0x01;
        b[n++] = sqn;
        put32(b, &n, 5);                 // heure de mise a jour
        put32(b, &n, 10);                // heure courante : redemarrage immediat
        put32(b, &n, version);
        put16(b, &n, type);
        put16(b, &n, manu);
        zigate_send_command(0x0504, b, (uint16_t)n);
        s_st.pct = 100;
        set_state(ZIGATE_OTA_DONE, "Mise a jour terminee, l'appareil redemarre");
        free_image();
        ESP_LOGI(TAG, "OTA 0x%04X : mise a jour terminee (v0x%08lX)", sa, (unsigned long)version);
    } else {
        char m[64];
        snprintf(m, sizeof(m), "Image refusee par l'appareil (statut 0x%02X)", status);
        set_state(ZIGATE_OTA_ERROR, m);
        ESP_LOGW(TAG, "OTA 0x%04X : %s", sa, m);
    }
    unlock();
    // Nouvelle version logicielle (Basic 0x4000), relue apres le redemarrage
    if (status == 0x00) zigate_read_attribute(sa, ep ? ep : 1, 0x0000, 0x4000);
}

void zigate_ota_handle_packet(const zigate_packet_t *pkt)
{
    if (!pkt) return;
    if (pkt->cmd == 0x8501) handle_block_request(pkt);
    else if (pkt->cmd == 0x8503) handle_upgrade_end(pkt);
}
