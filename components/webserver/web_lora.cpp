// ============================================================================
// Routes du module LoRa - voir web_lora.h
//
// Equivalences avec la LiXee-Box (pages HTML construites par le serveur et
// routes GET qui modifiaient l'etat) :
//   /loadLora, /configLora, /configLoraSettings -> GET  /api/lora/status
//   (fiches)                                    -> GET  /api/lora/devices
//   /loraPair, /cmdLoraPairAssist               -> POST /api/lora/pair
//   /loraPairStatus                             -> GET  /api/lora/pair/status
//   /loraRemove (+ /deleteDevice)               -> POST /api/lora/remove
//   /loraSetOpParams                            -> POST /api/lora/settings
//   /loraPoll, /loraPollStatus                  -> POST /api/lora/poll, GET /api/lora/poll/status
//   /otaInit|otaChunk|otaFinish?id=lora_<MAC>   -> POST /api/lora/ota/init|chunk|finish
//   /loraOtaStart, /loraOtaCancel               -> POST /api/lora/ota/start|cancel
//   /loraOtaStatus                              -> GET  /api/lora/ota/status
// Toute action qui modifie l'etat est en POST (la Box acceptait des GET,
// donc des liens forges depuis une page tierce).
// ============================================================================

#include "web_lora.h"
#include "web_server.h"
#include "lora.h"
#include "zigate_devices.h"
#include "zigate_ota.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "mbedtls/base64.h"

#include <ctype.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "WEB_LORA";

#define LORA_API_PREFIX      "/api/lora/"
#define JSON_BODY_MAX        512
#define CHUNK_RAW_MAX        16384                      // octets bruts par morceau
#define CHUNK_B64_MAX        (((CHUNK_RAW_MAX + 2) / 3) * 4 + 4)
#define UPLOAD_IDLE_US       (120LL * 1000 * 1000)      // depot abandonne apres 2 min

// ----------------------------------------------------------------------------
// Outils
// ----------------------------------------------------------------------------

static void query_param(httpd_req_t *req, const char *key, char *out, size_t len, const char *def)
{
    char q[160];
    strlcpy(out, def, len);
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        if (httpd_query_key_value(q, key, out, len) != ESP_OK) strlcpy(out, def, len);
    }
}

static int query_int(httpd_req_t *req, const char *key, int def)
{
    char v[16];
    query_param(req, key, v, sizeof(v), "");
    if (!v[0]) return def;
    char *end = NULL;
    long n = strtol(v, &end, 10);
    return (end && *end == '\0') ? (int)n : def;
}

static esp_err_t send_json_owned(httpd_req_t *req, char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (!json) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Module LoRa occupe ou memoire insuffisante\"}");
    }
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

static esp_err_t send_status(httpd_req_t *req, const char *status, const char *msg)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "status", strncmp(status, "200", 3) == 0 ? "ok" : "error");
    cJSON_AddStringToObject(o, "message", msg);
    char *txt = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, txt ? txt : "{}");
    free(txt);
    return err;
}

/** Corps de requete en PSRAM, termine par '\0'. NULL si absent ou trop gros. */
static char *read_body(httpd_req_t *req, size_t max, size_t *out_len)
{
    if (out_len) *out_len = 0;
    if (req->content_len == 0 || req->content_len > max) return NULL;
    size_t len = req->content_len;
    char *buf = (char *)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM);
    if (!buf) buf = (char *)malloc(len + 1);
    if (!buf) return NULL;
    size_t got = 0;
    int timeouts = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
        if (r <= 0) { free(buf); return NULL; }
        got += (size_t)r;
    }
    buf[got] = '\0';
    if (out_len) *out_len = got;
    return buf;
}

static cJSON *read_json(httpd_req_t *req)
{
    char *b = read_body(req, JSON_BODY_MAX, NULL);
    if (!b) return NULL;
    cJSON *j = cJSON_Parse(b);
    free(b);
    return j;
}

static int json_int(const cJSON *o, const char *key, int def)
{
    const cJSON *v = cJSON_GetObjectItem(o, key);
    if (cJSON_IsNumber(v)) return v->valueint;
    if (cJSON_IsString(v) && v->valuestring[0]) {
        char *end = NULL;
        long n = strtol(v->valuestring, &end, 0);
        if (end && *end == '\0') return (int)n;
    }
    return def;
}

/** Identifiant ZCL : nombre, ou chaine hexadecimale ("FF66", "0x0300"). */
static long json_hex16(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItem(o, key);
    if (cJSON_IsNumber(v)) return (v->valuedouble >= 0 && v->valuedouble <= 0xFFFF) ? (long)v->valuedouble : -1;
    if (!cJSON_IsString(v) || !v->valuestring[0]) return -1;
    char *end = NULL;
    long n = strtol(v->valuestring, &end, 16);
    return (end && *end == '\0' && n >= 0 && n <= 0xFFFF) ? n : -1;
}

// ----------------------------------------------------------------------------
// Depot d'un firmware d'emetteur (/littlefs/ota/lora_<MAC>.ota)
// Meme protocole que /otaInit|otaChunk|otaFinish de la Box (morceaux en
// base64 pour traverser le tunnel), plus des morceaux bruts
// (application/octet-stream) pour l'acces local. Ecriture dans un .part,
// renomme a la fin : un depot interrompu n'ecrase pas le fichier precedent.
// ----------------------------------------------------------------------------

static struct {
    bool    active;
    FILE   *f;
    char    path[64];
    char    tmp[72];
    size_t  total;
    size_t  received;
    int     next_n;
    int64_t last_us;
    uint64_t zigbee;      // appareil Zigbee destinataire (0 : emetteur LoRa)
} s_up;

/**
 * Fichier OTA d'un appareil Zigbee : id = IEEE en hexa (16 chiffres), comme
 * /otaInit?id=<IEEE> de la LiXee-Box. Chemin rebati depuis l'appareil connu.
 */
static uint64_t zigbee_ota_path_for_id(const char *id, char *out, size_t len)
{
    if (!id || strlen(id) != 16) return 0;
    for (const char *c = id; *c; c++) if (!isxdigit((unsigned char)*c)) return 0;
    uint64_t ieee = strtoull(id, NULL, 16);
    const zigate_device_t *d = ieee ? zigate_devices_find_by_ieee(ieee) : NULL;
    if (!d) return 0;
    zigate_ota_file_path(d->ieee_addr, out, len);
    return d->ieee_addr;
}

static void upload_abort(void)
{
    if (s_up.f) { fclose(s_up.f); s_up.f = NULL; }
    if (s_up.active) remove(s_up.tmp);
    s_up.active = false;
}

static void upload_expire(void)
{
    if (s_up.active && esp_timer_get_time() - s_up.last_us > UPLOAD_IDLE_US) {
        ESP_LOGW(TAG, "Depot de %s abandonne (inactif)", s_up.path);
        upload_abort();
    }
}

static esp_err_t ota_init_handler(httpd_req_t *req)
{
    char id[32];
    query_param(req, "id", id, sizeof(id), "");
    cJSON *j = read_json(req);
    long total = j ? json_int(j, "totalSize", -1) : -1;
    if (!id[0] && j) {
        const cJSON *jid = cJSON_GetObjectItem(j, "id");
        if (cJSON_IsString(jid)) strlcpy(id, jid->valuestring, sizeof(id));
    }
    cJSON_Delete(j);

    char path[64];
    uint64_t zb = zigbee_ota_path_for_id(id, path, sizeof(path));
    if (!zb && !lora_ota_path_for_id(id, path, sizeof(path)))
        return send_status(req, "400 Bad Request", "Identifiant lora_<MAC> ou IEEE Zigbee inconnu");
    if (total <= 0 || (unsigned long)total > LORA_OTA_MAX_FILE)
        return send_status(req, "400 Bad Request", "Taille du firmware invalide (1 o a 1 Mo)");
    if (!zb && lora_ota_active())
        return send_status(req, "409 Conflict", "Une mise a jour est en cours");
    mkdir("/littlefs/ota", 0755);

    upload_abort();
    strlcpy(s_up.path, path, sizeof(s_up.path));
    snprintf(s_up.tmp, sizeof(s_up.tmp), "%s.part", path);
    s_up.f = fopen(s_up.tmp, "wb");
    if (!s_up.f) return send_status(req, "500 Internal Server Error", "Ouverture du fichier impossible");
    s_up.active = true;
    s_up.total = (size_t)total;
    s_up.received = 0;
    s_up.next_n = 0;
    s_up.last_us = esp_timer_get_time();
    s_up.zigbee = zb;
    ESP_LOGI(TAG, "Depot de %s : %u octets attendus", path, (unsigned)s_up.total);
    return send_status(req, "200 OK", "Depot initialise");
}

static esp_err_t ota_chunk_handler(httpd_req_t *req)
{
    upload_expire();
    if (!s_up.active || !s_up.f) return send_status(req, "409 Conflict", "Depot non initialise");
    int n = query_int(req, "n", s_up.next_n);
    if (n != s_up.next_n) return send_status(req, "400 Bad Request", "Morceau hors sequence");

    char ctype[48] = "";
    httpd_req_get_hdr_value_str(req, "Content-Type", ctype, sizeof(ctype));
    bool raw = strstr(ctype, "octet-stream") != NULL;

    size_t blen = 0;
    char *body = read_body(req, raw ? CHUNK_RAW_MAX : CHUNK_B64_MAX, &blen);
    if (!body) return send_status(req, "400 Bad Request", "Morceau absent ou trop gros");

    uint8_t *data = (uint8_t *)body;
    size_t dlen = blen;
    uint8_t *decoded = NULL;
    if (!raw) {
        size_t need = 0;
        mbedtls_base64_decode(NULL, 0, &need, (const unsigned char *)body, blen);
        decoded = (uint8_t *)heap_caps_malloc(need ? need : 1, MALLOC_CAP_SPIRAM);
        if (!decoded) decoded = (uint8_t *)malloc(need ? need : 1);
        if (!decoded) { free(body); return send_status(req, "500 Internal Server Error", "Memoire insuffisante"); }
        if (mbedtls_base64_decode(decoded, need, &dlen, (const unsigned char *)body, blen) != 0) {
            free(body);
            free(decoded);
            return send_status(req, "400 Bad Request", "Base64 invalide");
        }
        data = decoded;
    }

    esp_err_t res;
    if (s_up.received + dlen > s_up.total) {
        upload_abort();
        res = send_status(req, "400 Bad Request", "Taille annoncee depassee");
    } else if (fwrite(data, 1, dlen, s_up.f) != dlen) {
        upload_abort();
        res = send_status(req, "500 Internal Server Error", "Ecriture impossible (espace insuffisant ?)");
    } else {
        s_up.received += dlen;
        s_up.next_n++;
        s_up.last_us = esp_timer_get_time();
        char msg[48];
        snprintf(msg, sizeof(msg), "%u/%u", (unsigned)s_up.received, (unsigned)s_up.total);
        res = send_status(req, "200 OK", msg);
    }
    free(body);
    free(decoded);
    return res;
}

static esp_err_t ota_finish_handler(httpd_req_t *req)
{
    upload_expire();
    if (!s_up.active || !s_up.f) return send_status(req, "409 Conflict", "Depot non initialise");
    fclose(s_up.f);
    s_up.f = NULL;
    if (s_up.received != s_up.total) {
        upload_abort();
        return send_status(req, "400 Bad Request", "Fichier incomplet");
    }
    if (!s_up.zigbee && lora_ota_active()) {
        upload_abort();
        return send_status(req, "409 Conflict", "Une mise a jour est en cours");
    }
    remove(s_up.path);
    if (rename(s_up.tmp, s_up.path) != 0) {
        upload_abort();
        return send_status(req, "500 Internal Server Error", "Renommage du fichier impossible");
    }
    s_up.active = false;
    ESP_LOGI(TAG, "Firmware d'emetteur depose : %s (%u octets)", s_up.path, (unsigned)s_up.received);
    if (s_up.zigbee) {
        // Appareil Zigbee : la mise a jour part aussitot (loadOTAFile de la Box)
        char err[80] = "";
        esp_err_t oe = zigate_ota_start(s_up.zigbee, err, sizeof(err));
        if (oe != ESP_OK) {
            // Fichier invalide : supprime ; OTA occupee ou appareil absent : garde
            // (relance possible depuis la page Zigbee)
            if (oe == ESP_ERR_INVALID_ARG || oe == ESP_ERR_INVALID_SIZE) remove(s_up.path);
            return send_status(req, oe == ESP_ERR_TIMEOUT || oe == ESP_ERR_INVALID_STATE ? "409 Conflict" : "400 Bad Request", err);
        }
        return send_status(req, "200 OK", "Fichier depose, image annoncee a l'appareil");
    }
    return send_status(req, "200 OK", "Fichier depose");
}

// ----------------------------------------------------------------------------
// Actions
// ----------------------------------------------------------------------------

static esp_err_t pair_handler(httpd_req_t *req)
{
    if (!lora_is_ready()) return send_status(req, "409 Conflict", "Module LoRa absent");
    if (lora_ota_active()) return send_status(req, "409 Conflict", "Mise a jour OTA en cours");
    if (lora_start_pairing() != ESP_OK) return send_status(req, "409 Conflict", "Appairage impossible");
    return send_status(req, "200 OK", "Fenetre d'appairage ouverte");
}

static esp_err_t pair_status_handler(httpd_req_t *req)
{
    uint64_t ieee = 0;
    char model[24] = "";
    cJSON *o = cJSON_CreateObject();
    if (lora_take_pair_result(&ieee, model, sizeof(model))) {
        char s[20];
        snprintf(s, sizeof(s), "%016llX", (unsigned long long)ieee);
        cJSON_AddBoolToObject(o, "found", true);
        cJSON_AddStringToObject(o, "ieee", s);
        cJSON_AddStringToObject(o, "model", model);
    } else {
        int left = 0;
        bool active = lora_pairing_state(&left);
        cJSON_AddBoolToObject(o, "found", false);
        cJSON_AddBoolToObject(o, "pairing", active);
        cJSON_AddNumberToObject(o, "left", left);
    }
    char *txt = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return send_json_owned(req, txt);
}

static esp_err_t remove_handler(httpd_req_t *req)
{
    cJSON *j = read_json(req);
    if (!j) return send_status(req, "400 Bad Request", "JSON invalide");
    int slot = json_int(j, "slot", -1);
    const cJSON *keep = cJSON_GetObjectItem(j, "keepData");
    bool keep_data = cJSON_IsTrue(keep);
    cJSON_Delete(j);
    esp_err_t err = lora_remove_emitter(slot, !keep_data);
    if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_ARG)
        return send_status(req, "404 Not Found", "Emetteur inconnu");
    if (err == ESP_ERR_INVALID_STATE)
        return send_status(req, "409 Conflict", "Mise a jour OTA en cours sur cet emetteur");
    if (err != ESP_OK) return send_status(req, "500 Internal Server Error", "Echec de la suppression");
    return send_status(req, "200 OK", "Emetteur supprime");
}

static esp_err_t settings_handler(httpd_req_t *req)
{
    cJSON *j = read_json(req);
    if (!j) return send_status(req, "400 Bad Request", "JSON invalide");
    int ch = json_int(j, "channel", -1);
    int sf = json_int(j, "sf", -1);
    cJSON_Delete(j);
    if (ch < 0 || ch > 7 || sf < 7 || sf > 12)
        return send_status(req, "400 Bad Request", "Canal 0..7 et SF 7..12 requis");
    esp_err_t err = lora_set_op_params((uint8_t)ch, (uint8_t)sf);
    if (err != ESP_OK) return send_status(req, "500 Internal Server Error", "Enregistrement impossible");
    return send_status(req, "200 OK", "Parametres enregistres");
}

static esp_err_t poll_handler(httpd_req_t *req)
{
    cJSON *j = read_json(req);
    if (!j) return send_status(req, "400 Bad Request", "JSON invalide");
    int slot = json_int(j, "slot", -1);
    long cluster = json_hex16(j, "cluster");
    long attr = json_hex16(j, "attr");
    cJSON_Delete(j);
    if (cluster < 0 || attr < 0) return send_status(req, "400 Bad Request", "Cluster et attribut 0..FFFF requis");
    esp_err_t err = lora_queue_poll(slot, (uint16_t)cluster, (uint16_t)attr);
    if (err == ESP_ERR_INVALID_STATE) return send_status(req, "409 Conflict", "Module LoRa absent");
    if (err != ESP_OK) return send_status(req, "404 Not Found", "Emetteur inconnu");
    return send_status(req, "200 OK", "Lecture en file");
}

static esp_err_t ota_start_handler(httpd_req_t *req)
{
    cJSON *j = read_json(req);
    if (!j) return send_status(req, "400 Bad Request", "JSON invalide");
    int slot = json_int(j, "slot", -1);
    int ver = json_int(j, "version", -1);
    // Reglages de diagnostic, absents de la page (cf. /loraOtaStart de la Box)
    int pre = json_int(j, "pre", 0);
    int sf = json_int(j, "sf", 0);
    int bs = json_int(j, "bs", 0);
    int win = json_int(j, "win", 0);
    cJSON_Delete(j);
    if (ver < 0 || ver > 0xFFFF) return send_status(req, "400 Bad Request", "Version 0..65535 requise");
    char err[64] = "";
    if (lora_ota_start(slot, (uint16_t)ver,
                       (uint8_t)(pre < 0 || pre > 255 ? 0 : pre), (uint8_t)(sf < 0 || sf > 255 ? 0 : sf),
                       (uint8_t)(bs < 0 || bs > 255 ? 0 : bs), (uint8_t)(win < 0 || win > 255 ? 0 : win),
                       err, sizeof(err)) != ESP_OK) {
        return send_status(req, "409 Conflict", err[0] ? err : "Demarrage refuse");
    }
    return send_status(req, "200 OK", "Mise a jour lancee");
}

// ----------------------------------------------------------------------------
// Aiguillage
// ----------------------------------------------------------------------------

/** Partie de l'URI apres "/api/lora/", sans la chaine de requete. */
static bool sub_is(httpd_req_t *req, const char *name)
{
    const char *p = req->uri + strlen(LORA_API_PREFIX);
    size_t n = strcspn(p, "?");
    return strlen(name) == n && strncmp(p, name, n) == 0;
}

static esp_err_t lora_get_handler(httpd_req_t *req)
{
    if (strncmp(req->uri, LORA_API_PREFIX, strlen(LORA_API_PREFIX)) != 0)
        return send_status(req, "404 Not Found", "Route inconnue");

    if (sub_is(req, "status"))      return send_json_owned(req, lora_status_json());
    if (sub_is(req, "devices"))     return send_json_owned(req, lora_devices_json());
    if (sub_is(req, "pair/status")) return pair_status_handler(req);
    if (sub_is(req, "poll/status")) return send_json_owned(req, lora_poll_status_json(query_int(req, "slot", -1)));
    if (sub_is(req, "ota/status"))  return send_json_owned(req, lora_ota_status_json(query_int(req, "slot", -1)));
    return send_status(req, "404 Not Found", "Route inconnue");
}

static esp_err_t lora_post_handler(httpd_req_t *req)
{
    if (strncmp(req->uri, LORA_API_PREFIX, strlen(LORA_API_PREFIX)) != 0)
        return send_status(req, "404 Not Found", "Route inconnue");

    if (sub_is(req, "pair"))       return pair_handler(req);
    if (sub_is(req, "remove"))     return remove_handler(req);
    if (sub_is(req, "settings"))   return settings_handler(req);
    if (sub_is(req, "poll"))       return poll_handler(req);
    if (sub_is(req, "ota/init"))   return ota_init_handler(req);
    if (sub_is(req, "ota/chunk"))  return ota_chunk_handler(req);
    if (sub_is(req, "ota/finish")) return ota_finish_handler(req);
    if (sub_is(req, "ota/abort"))  { upload_abort(); return send_status(req, "200 OK", "Depot annule"); }
    if (sub_is(req, "ota/start"))  return ota_start_handler(req);
    if (sub_is(req, "ota/cancel")) { lora_ota_cancel(); return send_status(req, "200 OK", "Annulation demandee"); }
    return send_status(req, "404 Not Found", "Route inconnue");
}

void web_lora_register(httpd_handle_t server)
{
    httpd_uri_t g = {};
    g.uri = LORA_API_PREFIX "*";
    g.method = HTTP_GET;
    g.handler = lora_get_handler;
    web_server_register_uri(server, &g);

    httpd_uri_t p = {};
    p.uri = LORA_API_PREFIX "*";
    p.method = HTTP_POST;
    p.handler = lora_post_handler;
    web_server_register_uri(server, &p);
}
