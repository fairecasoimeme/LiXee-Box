// ============================================================================
// Routes Zigbee - lot 3 du portage LiXee-Box (voir web_zigbee.h)
//
// Remplace les pages construites par le serveur de la LiXee-Box
// (/statusDevices, /dashboard, /configDevices, /configDevice, /configZigbee,
// /logs, /assistDevice) par une API JSON consommee par js/page_zigbee.js.
//
// Contrat (toutes les reponses en JSON ; erreur : {"status":"error","message"}) :
//
// GET  /api/zigbee/devices                 liste (format historique de web_api + champs)
// GET  /api/zigbee/devices/full            idem + valeurs brutes (compatibilite)
// GET  /api/zigbee/cards                   fiches : etats visibles formates, actions
// GET  /api/zigbee/values?since=S          valeurs formatees modifiees depuis S
// GET  /api/zigbee/device?ieee=X           fiche detaillee (tous les etats, valeurs brutes)
// GET  /api/zigbee/read_value?ieee=X&cluster=C&attr=A   valeur stockee d'un attribut
// GET  /api/zigbee/coordinator             etat de la ZiGate, appairage en cours
// GET  /api/zigbee/console?since=N&max=M   lignes de la console des trames
//
// POST /api/zigbee/action      {ieee, name} ou {ieee, command, endpoint, value, cluster, mfr}
// POST /api/zigbee/write       {ieee, cluster, attr, type, value, endpoint?, mfr?}
// POST /api/zigbee/read        {ieee, cluster, attr, endpoint?, mfr?}
// POST /api/zigbee/refresh     {ieee}  relit fabricant, modele, version logicielle
// POST /api/zigbee/configure   {ieee}  renvoie bind et reporting du template
// POST /api/zigbee/alias       {ieee, alias}
// POST /api/zigbee/delete      {ieee}  (alias historique : /api/zigbee/remove)
// POST /api/zigbee/discover    {ieee}  decouverte Home Assistant
// POST /api/zigbee/permit_join {duration}
// POST /api/zigbee/cmd         {cmd, value?}  commandes du coordinateur
//
// Clusters et attributs : nombres (decimal) ou chaines "0x...". Les cles de
// valeurs sont "<cluster hexa 4 chiffres>_<attribut decimal>", comme le
// stockage (device_data) et la publication MQTT.
//
// Les reponses sont construites dans un tampon en PSRAM (jw_t) et non avec
// cJSON : un arbre cJSON de plusieurs appareils compterait des milliers de
// petits noeuds, alloues en RAM interne (sous le seuil SPIRAM de malloc).
// ============================================================================

#include "web_zigbee.h"
#include "web_zigbee_remove.h"
#include "web_server.h"
#include "zigate_driver.h"
#include "zigate_devices.h"
#include "zigate_console.h"
#include "zigate_actions.h"
#include "zigate_ota.h"
#include "zigate_ir.h"
#include <sys/stat.h>
#include "device_data.h"
#include "device_setup.h"
#include "mqtt_manager.h"
#include "config_manager.h"
#include "matter_device_manager.h"
#include "lora.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "WEB_ZB";

// ============================================================================
// Ecriture JSON dans un tampon en PSRAM
// Les virgules sont placees automatiquement : avant une cle ou une valeur,
// sauf juste apres '{', '[', ':' ou ','.
// ============================================================================

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    bool   err;
} jw_t;

static bool jw_reserve(jw_t *w, size_t need)
{
    if (w->err) return false;
    if (w->len + need + 1 <= w->cap) return true;
    size_t nc = w->cap ? w->cap : 4096;
    while (nc < w->len + need + 1) nc *= 2;
    char *nb = (char *)heap_caps_realloc(w->buf, nc, MALLOC_CAP_SPIRAM);
    if (!nb) nb = (char *)realloc(w->buf, nc);
    if (!nb) { w->err = true; return false; }
    w->buf = nb;
    w->cap = nc;
    return true;
}

static void jw_raw(jw_t *w, const char *s)
{
    size_t n = strlen(s);
    if (!jw_reserve(w, n)) return;
    memcpy(w->buf + w->len, s, n);
    w->len += n;
    w->buf[w->len] = '\0';
}

static void jw_sep(jw_t *w)
{
    if (w->err || w->len == 0) return;
    char c = w->buf[w->len - 1];
    if (c != '{' && c != '[' && c != ':' && c != ',') jw_raw(w, ",");
}

static void jw_fmt(jw_t *w, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void jw_fmt(jw_t *w, const char *fmt, ...)
{
    if (!jw_reserve(w, 64)) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(w->buf + w->len, w->cap - w->len, fmt, ap);
    va_end(ap);
    if (n < 0) { w->err = true; return; }
    if ((size_t)n >= w->cap - w->len) {
        if (!jw_reserve(w, (size_t)n)) return;
        va_start(ap, fmt);
        vsnprintf(w->buf + w->len, w->cap - w->len, fmt, ap);
        va_end(ap);
    }
    w->len += (size_t)n;
}

/** Chaine JSON echappee (UTF-8 conserve, caracteres de controle en sequence unicode). */
static void jw_str(jw_t *w, const char *s)
{
    jw_raw(w, "\"");
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        unsigned char c = *p;
        if (c == '"')       jw_raw(w, "\\\"");
        else if (c == '\\') jw_raw(w, "\\\\");
        else if (c == '\n') jw_raw(w, "\\n");
        else if (c == '\r') jw_raw(w, "\\r");
        else if (c == '\t') jw_raw(w, "\\t");
        else if (c < 0x20)  jw_fmt(w, "\\u%04x", c);
        else {
            if (!jw_reserve(w, 1)) return;
            w->buf[w->len++] = (char)c;
            w->buf[w->len] = '\0';
        }
    }
    jw_raw(w, "\"");
}

static void jw_key(jw_t *w, const char *k)   { jw_sep(w); jw_str(w, k); jw_raw(w, ":"); }
static void jw_obj(jw_t *w)                  { jw_sep(w); jw_raw(w, "{"); }
static void jw_obj_end(jw_t *w)              { jw_raw(w, "}"); }
static void jw_arr(jw_t *w)                  { jw_sep(w); jw_raw(w, "["); }
static void jw_arr_end(jw_t *w)              { jw_raw(w, "]"); }
static void jw_vstr(jw_t *w, const char *s)  { jw_sep(w); jw_str(w, s); }
static void jw_kstr(jw_t *w, const char *k, const char *v) { jw_key(w, k); jw_str(w, v); }
static void jw_kint(jw_t *w, const char *k, long long v)   { jw_key(w, k); jw_fmt(w, "%lld", v); }
static void jw_kbool(jw_t *w, const char *k, bool v)       { jw_key(w, k); jw_raw(w, v ? "true" : "false"); }
static void jw_kdbl(jw_t *w, const char *k, double v)      { jw_key(w, k); jw_fmt(w, "%.9g", v); }

static void jw_khex4(jw_t *w, const char *k, unsigned v)
{
    char b[12];
    snprintf(b, sizeof(b), "%04X", v & 0xFFFFu);
    jw_kstr(w, k, b);
}

static void jw_kieee(jw_t *w, const char *k, uint64_t v)
{
    char b[20];
    snprintf(b, sizeof(b), "%016llX", (unsigned long long)v);
    jw_kstr(w, k, b);
}

static esp_err_t jw_send(httpd_req_t *req, jw_t *w)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err;
    if (w->err || !w->buf) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        err = httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Memoire insuffisante\"}");
    } else {
        err = httpd_resp_send(req, w->buf, w->len);
    }
    free(w->buf);
    w->buf = NULL;
    return err;
}

// ============================================================================
// Outils de requete / reponse
// ============================================================================

static esp_err_t send_status(httpd_req_t *req, const char *http_status, const char *msg)
{
    jw_t w = {};
    jw_obj(&w);
    jw_kstr(&w, "status", strncmp(http_status, "200", 3) == 0 ? "ok" : "error");
    if (msg) jw_kstr(&w, "message", msg);
    jw_obj_end(&w);
    httpd_resp_set_status(req, http_status);
    return jw_send(req, &w);
}

static esp_err_t send_ok(httpd_req_t *req)                       { return send_status(req, "200 OK", NULL); }
static esp_err_t send_bad(httpd_req_t *req, const char *msg)     { return send_status(req, "400 Bad Request", msg); }
static esp_err_t send_not_found(httpd_req_t *req, const char *m) { return send_status(req, "404 Not Found", m); }

static esp_err_t send_zigate_result(httpd_req_t *req, esp_err_t err)
{
    if (err == ESP_OK) return send_ok(req);
    if (err == ESP_ERR_INVALID_ARG) return send_bad(req, "Parametre invalide");
    if (err == ESP_ERR_NOT_SUPPORTED) return send_bad(req, "Commande non supportee");
    return send_status(req, "500 Internal Server Error", "Envoi a la ZiGate impossible");
}

static bool query_param(httpd_req_t *req, const char *key, char *out, size_t len)
{
    char q[256];
    out[0] = '\0';
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return false;
    return httpd_query_key_value(q, key, out, len) == ESP_OK && out[0];
}

/** Nombre en decimal ou en "0x..", ou valeur par defaut. */
static long query_long(httpd_req_t *req, const char *key, long def)
{
    char v[24];
    if (!query_param(req, key, v, sizeof(v))) return def;
    char *end;
    long r = strtol(v, &end, 0);
    return end == v ? def : r;
}

static bool parse_ieee(const char *s, uint64_t *out)
{
    if (!s) return false;
    size_t n = strlen(s);
    if (n == 0 || n > 16) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) return false;
    }
    *out = strtoull(s, NULL, 16);
    return *out != 0;
}

/** Appareil designe par ?ieee= (ou ?id=, nom de la LiXee-Box). */
static zigate_device_t *query_device(httpd_req_t *req)
{
    char v[24];
    uint64_t ieee;
    if (!query_param(req, "ieee", v, sizeof(v)) && !query_param(req, "id", v, sizeof(v))) return NULL;
    return parse_ieee(v, &ieee) ? zigate_devices_find_by_ieee(ieee) : NULL;
}

/** Profondeur d'imbrication d'un texte JSON (cJSON_Parse est recursif). */
static bool json_depth_ok(const char *s, int max_depth)
{
    int depth = 0;
    bool in_str = false, esc = false;
    for (; *s; s++) {
        char c = *s;
        if (in_str) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') in_str = true;
        else if (c == '{' || c == '[') { if (++depth > max_depth) return false; }
        else if ((c == '}' || c == ']') && depth > 0) depth--;
    }
    return true;
}

/**
 * Corps JSON de la requete (1 Ko au plus).
 * @return document a liberer, ou NULL : la reponse d'erreur est deja partie
 */
static cJSON *read_body_max(httpd_req_t *req, int max)
{
    int len = req->content_len;
    if (len <= 0) { send_bad(req, "Corps JSON attendu"); return NULL; }
    if (len > max) { send_status(req, "413 Payload Too Large", "Corps trop long"); return NULL; }
    // Gros corps (codes IR) en PSRAM, les autres en RAM interne
    char *buf = (char *)(len > 1024 ? heap_caps_malloc((size_t)len + 1, MALLOC_CAP_SPIRAM) : malloc((size_t)len + 1));
    if (!buf) { send_status(req, "500 Internal Server Error", "Memoire insuffisante"); return NULL; }
    int got = 0, timeouts = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
        if (r <= 0) { free(buf); send_bad(req, "Corps incomplet"); return NULL; }
        got += r;
    }
    buf[got] = '\0';
    cJSON *doc = json_depth_ok(buf, 8) ? cJSON_Parse(buf) : NULL;
    free(buf);
    if (!doc) send_bad(req, "JSON invalide");
    return doc;
}

static cJSON *read_body(httpd_req_t *req)
{
    return read_body_max(req, 1024);
}

/** Nombre JSON, ou chaine en decimal / "0x..". */
static bool json_long(const cJSON *j, const char *key, long *out)
{
    const cJSON *it = cJSON_GetObjectItem(j, key);
    if (cJSON_IsNumber(it)) { *out = (long)it->valuedouble; return true; }
    if (cJSON_IsString(it) && it->valuestring[0]) {
        char *end;
        long v = strtol(it->valuestring, &end, 0);
        if (end != it->valuestring && *end == '\0') { *out = v; return true; }
    }
    return false;
}

static const char *json_str(const cJSON *j, const char *key)
{
    const cJSON *it = cJSON_GetObjectItem(j, key);
    return cJSON_IsString(it) ? it->valuestring : NULL;
}

/** Appareil designe par "ieee" (ou "short" / "addr") dans un corps JSON. */
static zigate_device_t *body_device(const cJSON *j)
{
    uint64_t ieee;
    if (parse_ieee(json_str(j, "ieee"), &ieee)) return zigate_devices_find_by_ieee(ieee);
    long sa;
    if (json_long(j, "short", &sa) || json_long(j, "addr", &sa)) {
        if (sa >= 0 && sa <= 0xFFFF) return zigate_devices_find_by_short((uint16_t)sa);
    }
    return NULL;
}

static uint32_t now_s(void)
{
    return esp_log_timestamp() / 1000;       // horloge de devdata_attr_t.last_update
}

static bool is_zlinky(const zigate_device_t *d)
{
    return strncmp(d->model, "ZLinky", 6) == 0;
}

/** Mode TIC servant de filtre d'affichage, -1 = pas de filtre. */
static int display_mode(const zigate_device_t *d)
{
    if (!is_zlinky(d)) return -1;
    int m = device_data_linky_mode(d->ieee_addr);
    return m >= 0 ? m : cfg_get_general()->linky_mode;
}

static const devdata_template_t *device_template(const zigate_device_t *d)
{
    const devdata_template_t *t = device_data_get_template(d->ieee_addr);
    return (t && t->loaded) ? t : NULL;
}

static void state_key(const devdata_template_state_t *s, char *out, size_t len)
{
    snprintf(out, len, "%04X_%u", s->cluster, (unsigned)s->attribute);
}

/** Texte d'un attribut Basic (0x0000) stocke en hexa : fabricant, version... */
static void basic_text(uint64_t ieee, uint16_t attr, char *out, size_t len)
{
    out[0] = '\0';
    const devdata_attr_t *a = device_data_get_attr(ieee, 0x0000, attr);
    if (!a || !a->value[0]) return;
    char hex[DEVDATA_VALUE_LEN];
    strlcpy(hex, a->value, sizeof(hex));
    device_data_format_value(0x0000, attr, hex, a->type ? a->type : 0x42, NULL, 1.0f, out, len);
}

// ============================================================================
// Blocs JSON communs
// ============================================================================

/**
 * L'appareil a-t-il le cluster Occupancy (0x0406) ? Declare a la decouverte,
 * deja remonte (valeur stockee) ou decrit par son template : la liste des
 * clusters n'est pas memorisee d'un demarrage a l'autre.
 */
static bool has_occupancy(const zigate_device_t *d)
{
    for (int i = 0; i < d->in_cluster_count && i < 32; i++) if (d->in_clusters[i] == 0x0406) return true;
    if (device_data_get_attr(d->ieee_addr, 0x0406, 0x0000)) return true;
    const devdata_template_t *t = device_data_get_template(d->ieee_addr);
    for (int i = 0; t && i < t->state_count; i++) if (t->states[i].cluster == 0x0406) return true;
    return false;
}

/** Prise / relais pilotable en marche-arret (delestage) : prise declaree ou cluster 0x0006. */
static bool has_onoff(const zigate_device_t *d)
{
    const device_data_t *dd = device_data_find(d->ieee_addr);
    if (dd && !strcmp(dd->info.power_socket, "1")) return true;
    for (int i = 0; i < d->in_cluster_count && i < 32; i++) if (d->in_clusters[i] == 0x0006) return true;
    if (device_data_get_attr(d->ieee_addr, 0x0006, 0x0000)) return true;
    const devdata_template_t *t = device_data_get_template(d->ieee_addr);
    for (int i = 0; t && i < t->state_count; i++) if (t->states[i].cluster == 0x0006) return true;
    return false;
}

/** Telecommande infrarouge Zosung : clusters E004 / ED00, ou modele connu. */
static bool has_ir(const zigate_device_t *d)
{
    for (int i = 0; i < d->in_cluster_count && i < 32; i++)
        if (d->in_clusters[i] == 0xE004 || d->in_clusters[i] == 0xED00) return true;
    return !strcmp(d->model, "TS1201") || !strncmp(d->model, "ZS06", 4) || !strncmp(d->model, "UFO-R11", 7);
}

/** Identite et etat radio d'un appareil (champs d'un objet deja ouvert). */
static void write_device_header(jw_t *w, const zigate_device_t *d)
{
    jw_kieee(w, "ieee", d->ieee_addr);
    jw_kint(w, "short", d->short_addr);
    jw_kint(w, "shortAddr", d->short_addr);          // nom historique (web_api)
    jw_kint(w, "deviceId", d->device_id);
    jw_kint(w, "endpoint", d->endpoint);
    jw_kstr(w, "manufacturer", d->manufacturer);
    jw_kstr(w, "model", d->model);
    jw_kstr(w, "alias", d->alias);
    jw_kbool(w, "active", d->active);
    jw_kint(w, "lqi", d->lqi);
    jw_kint(w, "matterEndpoint", d->matter_endpoint);
    jw_kint(w, "radio", d->radio_status);
    jw_kint(w, "lastSeen", d->last_seen);
    jw_kint(w, "ago", d->last_rx_s ? (long long)(zigate_uptime_s() - d->last_rx_s) : -1);
    jw_kbool(w, "occupancy", has_occupancy(d));
    jw_kbool(w, "onoff", has_onoff(d));
    jw_kbool(w, "ir", has_ir(d));
}

static void write_actions(jw_t *w, uint64_t ieee, const devdata_template_t *t)
{
    jw_key(w, "actions");
    jw_arr(w);
    for (int i = 0; t && i < t->action_count; i++) {
        const devdata_template_action_t *a = &t->actions[i];
        if (!a->visible) continue;
        jw_obj(w);
        jw_kint(w, "i", i);
        jw_kstr(w, "n", a->name);
        jw_kint(w, "c", a->command);
        jw_kint(w, "e", a->endpoint);
        jw_kint(w, "v", a->value);
        if (a->cluster) jw_kint(w, "cl", a->cluster);
        if (a->manufacturer_code) jw_kint(w, "m", a->manufacturer_code);
        if (zigate_action_is_cover_position(a->command)) jw_kbool(w, "cover", true);
        if (zigate_action_is_setpoint(a->command)) {
            // Consigne : la fiche affiche un champ de temperature, prerempli
            // avec la consigne enregistree (0x0011 froid, 0x0012 chaud)
            jw_kbool(w, "temp", true);
            uint16_t at = (a->command == ZB_ACT_HVAC_COOL_SETPOINT) ? 0x0011 : 0x0012;
            const char *hv = device_data_get_value_ptr(ieee, 0x0201, at);
            if (hv && hv[0]) jw_kint(w, "tv", (int16_t)strtol(hv, NULL, 16));
        }
        jw_obj_end(w);
    }
    jw_arr_end(w);
}

/** Position de levage brute (0x0102/0x0008, 0..100 = fermeture), -1 inconnue. */
static int cover_lift(uint64_t ieee)
{
    const char *v = device_data_get_value_ptr(ieee, 0x0102, 0x0008);
    if (!v || !v[0]) return -1;
    long l = strtol(v, NULL, 16);
    return l < 0 ? 0 : (l > 100 ? 100 : (int)l);
}

// ============================================================================
// GET
// ============================================================================

/** Liste des appareils, format de l'ancien /api/zigbee/devices (+ champs). */
static esp_err_t get_devices(httpd_req_t *req, bool full)
{
    jw_t w = {};
    jw_arr(&w);
    for (int i = 0; i < zigate_devices_count(); i++) {
        const zigate_device_t *d = zigate_devices_get(i);
        if (!d) continue;
        jw_obj(&w);
        write_device_header(&w, d);
        const devdata_template_t *t = device_template(d);
        jw_kbool(&w, "tmpl", t && !t->dyn_only);
        jw_kbool(&w, "configured", d->configured);
        char sw[40];
        basic_text(d->ieee_addr, 0x4000, sw, sizeof(sw));
        jw_kstr(&w, "swVersion", sw);
        if (full) {
            jw_key(&w, "inClusters");
            jw_arr(&w);
            for (int c = 0; c < d->in_cluster_count && c < 32; c++) {
                char cl[12];
                snprintf(cl, sizeof(cl), "0x%04X", d->in_clusters[c]);
                jw_vstr(&w, cl);
            }
            jw_arr_end(&w);
            const device_data_t *dd = device_data_find(d->ieee_addr);
            if (dd) {
                jw_key(&w, "values");
                jw_obj(&w);
                for (int c = 0; c < dd->cluster_count; c++) {
                    char cl[12];
                    snprintf(cl, sizeof(cl), "%04X", dd->clusters[c].cluster_id);
                    jw_key(&w, cl);
                    jw_obj(&w);
                    for (int a = 0; a < dd->clusters[c].attr_count; a++) {
                        char at[12];
                        snprintf(at, sizeof(at), "%u", (unsigned)dd->clusters[c].attrs[a].attr_id);
                        jw_kstr(&w, at, dd->clusters[c].attrs[a].value);
                    }
                    jw_obj_end(&w);
                }
                jw_obj_end(&w);
                jw_kstr(&w, "status", dd->info.status);
            }
        }
        jw_obj_end(&w);
    }
    jw_arr_end(&w);
    return jw_send(req, &w);
}

/**
 * Appareils connus de device_data seulement, avec un template : emetteurs
 * LoRa, dont les mesures suivent le meme chemin que le Zigbee. La page
 * Mesures de la LiXee-Box listait les deux radios ; leurs actions ne sont
 * pas proposees (l'emetteur ne recoit pas de commandes).
 */
static const device_data_t *other_device(int index)
{
    const device_data_t *dd = device_data_get_by_index(index);
    if (!dd || !dd->ieee_addr) return NULL;
    if (zigate_devices_find_by_ieee(dd->ieee_addr)) return NULL;   // deja liste
    // Emetteur LoRa : liste meme sans template (identite pas encore lue),
    // comme la LiXee-Box ; autres fiches : seulement avec un template
    if (lora_is_lora_device(dd->ieee_addr)) return dd;
    if (!dd->tmpl || !dd->tmpl->loaded) return NULL;
    return dd;
}

/** Filtre de mode TIC d'un appareil hors ZiGate : si son template en declare. */
static int display_mode_other(const device_data_t *dd)
{
    const devdata_template_t *t = dd->tmpl;
    bool has_mode = false;
    for (int i = 0; t && i < t->state_count && !has_mode; i++) has_mode = t->states[i].mode[0] != '\0';
    if (!has_mode) return -1;
    int m = device_data_linky_mode(dd->ieee_addr);
    return m >= 0 ? m : cfg_get_general()->linky_mode;
}

static void write_card_states(jw_t *w, uint64_t ieee, const devdata_template_t *t, int mode)
{
    jw_key(w, "states");
    jw_arr(w);
    for (int s = 0; t && s < t->state_count; s++) {
        const devdata_template_state_t *st8 = &t->states[s];
        if (!st8->visible || !device_data_state_in_mode(st8, mode)) continue;
        char key[24], v[96];
        state_key(st8, key, sizeof(key));
        device_data_format_state(ieee, st8, v, sizeof(v));
        jw_obj(w);
        jw_kstr(w, "k", key);
        jw_kstr(w, "n", st8->name);
        jw_kstr(w, "v", v);
        jw_kstr(w, "u", st8->unit);
        if (st8->type_jauge[0]) {
            jw_kstr(w, "j", st8->type_jauge);
            jw_kint(w, "min", st8->jauge_min);
            jw_kint(w, "max", st8->jauge_max);
        }
        jw_obj_end(w);
    }
    jw_arr_end(w);
}

/**
 * Fiches des pages Appareils et Tableau de bord (portage de handleStatusDevices
 * et handleDashboard) : etats VISIBLES du template, filtres par mode TIC pour
 * le ZLinky, valeurs deja formatees (coefficient, type), unite, jauge.
 */
static esp_err_t get_cards(httpd_req_t *req)
{
    const cfg_settings_t *st = cfg_get_settings();
    const cfg_general_t *g = cfg_get_general();
    jw_t w = {};
    jw_obj(&w);
    jw_kint(&w, "now", now_s());
    jw_kbool(&w, "ha", st->enable_mqtt && g->ha_mqtt);
    jw_key(&w, "devices");
    jw_arr(&w);
    for (int i = 0; i < zigate_devices_count() && !w.err; i++) {
        const zigate_device_t *d = zigate_devices_get(i);
        if (!d) continue;
        const devdata_template_t *t = device_template(d);
        int mode = display_mode(d);
        jw_obj(&w);
        write_device_header(&w, d);
        jw_kbool(&w, "tmpl", t && !t->dyn_only);
        jw_kint(&w, "linkyMode", mode);
        jw_kint(&w, "cover", cover_lift(d->ieee_addr));
        write_card_states(&w, d->ieee_addr, t, mode);
        write_actions(&w, d->ieee_addr, t);
        jw_obj_end(&w);
    }
    for (int i = 0; i < DEVDATA_MAX_DEVICES && !w.err; i++) {
        const device_data_t *dd = other_device(i);
        if (!dd) continue;
        int mode = display_mode_other(dd);
        // Identite de l'emetteur LoRa (icone de la carte : modele puis DeviceId)
        lora_emitter_info_t le = {};
        int lslot = lora_find_slot_by_ieee(dd->ieee_addr);
        if (lslot >= 0) lora_get_emitter(lslot, &le);
        jw_obj(&w);
        jw_kieee(&w, "ieee", dd->ieee_addr);
        jw_kstr(&w, "alias", dd->info.alias);
        jw_kstr(&w, "model", le.model);
        jw_kint(&w, "deviceId", atoi(le.device_id));
        jw_kbool(&w, "lora", true);
        jw_kbool(&w, "tmpl", dd->tmpl && dd->tmpl->loaded);
        jw_kint(&w, "radio", 0);
        jw_kint(&w, "linkyMode", mode);
        jw_kint(&w, "cover", -1);
        write_card_states(&w, dd->ieee_addr, dd->tmpl, mode);
        jw_key(&w, "actions");
        jw_raw(&w, "[]");
        jw_obj_end(&w);
    }
    jw_arr_end(&w);
    jw_obj_end(&w);
    return jw_send(req, &w);
}

/** Valeurs formatees modifiees depuis 'since' (0 = toutes). */
static void write_changed_values(jw_t *w, uint64_t ieee, const devdata_template_t *t,
                                 int mode, long since)
{
    jw_key(w, "v");
    jw_obj(w);
    for (int s = 0; t && s < t->state_count; s++) {
        const devdata_template_state_t *st8 = &t->states[s];
        if (!st8->visible || !device_data_state_in_mode(st8, mode)) continue;
        const devdata_attr_t *a = device_data_get_attr(ieee, st8->cluster, st8->attribute);
        if (!a || (since > 0 && (long)a->last_update < since)) continue;
        char key[24], v[96];
        state_key(st8, key, sizeof(key));
        device_data_format_state(ieee, st8, v, sizeof(v));
        jw_kstr(w, key, v);
    }
    jw_obj_end(w);
}

/**
 * Rafraichissement des fiches (remplace /getDeviceValue et /getDeviceAttrValues,
 * qui vidaient une file partagee : deux onglets ouverts se volaient les mises
 * a jour). Ici chaque client passe la date de sa derniere lecture.
 * ?ieee= limite la reponse a un appareil.
 */
static esp_err_t get_values(httpd_req_t *req)
{
    long since = query_long(req, "since", 0);
    char only_s[24];
    uint64_t only = 0;
    if (query_param(req, "ieee", only_s, sizeof(only_s))) parse_ieee(only_s, &only);
    uint32_t now = now_s();

    jw_t w = {};
    jw_obj(&w);
    jw_kint(&w, "now", now);
    jw_key(&w, "devices");
    jw_obj(&w);
    for (int i = 0; i < zigate_devices_count() && !w.err; i++) {
        const zigate_device_t *d = zigate_devices_get(i);
        if (!d || (only && only != d->ieee_addr)) continue;
        char ieee[20];
        snprintf(ieee, sizeof(ieee), "%016llX", (unsigned long long)d->ieee_addr);
        jw_key(&w, ieee);
        jw_obj(&w);
        jw_kint(&w, "radio", d->radio_status);
        jw_kint(&w, "lqi", d->lqi);
        jw_kint(&w, "lastSeen", d->last_seen);           // infobulle de l'etat radio
        jw_kint(&w, "ago", d->last_rx_s ? (long long)(zigate_uptime_s() - d->last_rx_s) : -1);
        jw_kint(&w, "cover", cover_lift(d->ieee_addr));
        write_changed_values(&w, d->ieee_addr, device_template(d), display_mode(d), since);
        jw_obj_end(&w);
    }
    for (int i = 0; i < DEVDATA_MAX_DEVICES && !w.err; i++) {
        const device_data_t *dd = other_device(i);
        if (!dd || (only && only != dd->ieee_addr)) continue;
        char ieee[20];
        snprintf(ieee, sizeof(ieee), "%016llX", (unsigned long long)dd->ieee_addr);
        jw_key(&w, ieee);
        jw_obj(&w);
        jw_kint(&w, "radio", 0);
        jw_kint(&w, "cover", -1);
        write_changed_values(&w, dd->ieee_addr, dd->tmpl, display_mode_other(dd), since);
        jw_obj_end(&w);
    }
    jw_obj_end(&w);
    jw_obj_end(&w);
    return jw_send(req, &w);
}

/** Fiche detaillee (portage de handleConfigDevice). */
/**
 * Partie commune de la fiche (Zigbee et LoRa) : etats du template, valeurs
 * brutes recues et liste de scrutation.
 */
static void write_device_body(jw_t *w, uint64_t ieee, const devdata_template_t *t,
                              const device_data_t *dd, int mode, uint32_t now)
{
    // Tous les etats du template (visibles ou non), filtre de mode TIC
    // indique par "inMode" plutot qu'applique : la fiche sert au diagnostic
    jw_key(w, "states");
    jw_arr(w);
    for (int s = 0; t && s < t->state_count; s++) {
        const devdata_template_state_t *st8 = &t->states[s];
        const devdata_attr_t *a = device_data_get_attr(ieee, st8->cluster, st8->attribute);
        char key[24], v[96];
        state_key(st8, key, sizeof(key));
        device_data_format_state(ieee, st8, v, sizeof(v));
        jw_obj(w);
        jw_kint(w, "i", s);
        jw_kstr(w, "k", key);
        jw_kstr(w, "n", st8->name);
        jw_kint(w, "cl", st8->cluster);
        jw_kint(w, "at", st8->attribute);
        jw_kint(w, "mfr", st8->manufacturer_code);
        jw_kstr(w, "v", v);
        jw_kstr(w, "raw", a ? a->value : "");
        jw_kint(w, "zt", a ? a->type : 0);
        jw_kint(w, "age", a ? (long long)(now - a->last_update) : -1);
        jw_kstr(w, "u", st8->unit);
        jw_kstr(w, "ty", st8->type);
        jw_kdbl(w, "coef", st8->coefficient);
        jw_kbool(w, "vis", st8->visible);
        jw_kbool(w, "inMode", device_data_state_in_mode(st8, mode));
        jw_kbool(w, "w", st8->writable);
        jw_kint(w, "wt", st8->type_writable);
        jw_kint(w, "poll", st8->poll);
        jw_obj_end(w);
    }
    jw_arr_end(w);
    write_actions(w, ieee, t);

    // Toutes les valeurs stockees, avec ou sans entree de template
    jw_key(w, "raw");
    jw_arr(w);
    for (int c = 0; dd && c < dd->cluster_count; c++) {
        const devdata_cluster_t *cl = &dd->clusters[c];
        for (int a = 0; a < cl->attr_count; a++) {
            const devdata_attr_t *at = &cl->attrs[a];
            char hex[DEVDATA_VALUE_LEN], txt[96];
            strlcpy(hex, at->value, sizeof(hex));
            device_data_format_value(cl->cluster_id, at->attr_id, hex, at->type, "", 1.0f,
                                     txt, sizeof(txt));
            jw_obj(w);
            jw_kint(w, "cl", cl->cluster_id);
            jw_kint(w, "at", at->attr_id);
            jw_kstr(w, "v", hex);
            jw_kstr(w, "txt", txt);
            jw_kint(w, "zt", at->type);
            jw_kint(w, "age", (long long)(now - at->last_update));
            jw_obj_end(w);
        }
    }
    jw_arr_end(w);

    jw_key(w, "poll");
    jw_arr(w);
    for (int p = 0; dd && p < dd->poll_count; p++) {
        jw_obj(w);
        jw_kint(w, "cl", dd->poll_list[p].cluster_id);
        jw_kint(w, "at", dd->poll_list[p].attr_id);
        jw_kint(w, "ms", dd->poll_list[p].interval_ms);
        jw_obj_end(w);
    }
    jw_arr_end(w);
}

/**
 * Fiche d'un emetteur LoRa (configDevice de la LiXee-Box pour un appareil
 * LoRa) : pas d'adresse courte, d'endpoint ni de LQI, mais RSSI / SNR ; ni
 * ecriture ni action (rien n'est commandable), lecture a la demande par POLL.
 */
static esp_err_t get_lora_device(httpd_req_t *req, uint64_t ieee, int slot)
{
    lora_emitter_info_t e;
    if (!lora_get_emitter(slot, &e)) return send_not_found(req, "Appareil inconnu");
    const cfg_settings_t *cs = cfg_get_settings();
    const cfg_general_t *g = cfg_get_general();
    const device_data_t *dd = device_data_find(ieee);
    const devdata_template_t *t = (dd && dd->tmpl && dd->tmpl->loaded) ? dd->tmpl : NULL;
    int mode = t ? display_mode_other(dd) : -1;
    uint32_t now = now_s();

    jw_t w = {};
    jw_obj(&w);
    jw_kieee(&w, "ieee", ieee);
    jw_kbool(&w, "lora", true);
    jw_kint(&w, "slot", slot);
    jw_kstr(&w, "alias", dd ? dd->info.alias : "");
    jw_kstr(&w, "manufacturer", "LiXee");
    jw_kstr(&w, "model", e.model);
    jw_kint(&w, "deviceId", atoi(e.device_id));
    jw_kbool(&w, "identified", e.identified);
    jw_kstr(&w, "swVersion", e.version);
    jw_kint(&w, "lastSeen", e.last_seen);
    jw_kint(&w, "ago", e.age_s);
    jw_kdbl(&w, "rssi", e.rssi);
    jw_kdbl(&w, "snr", e.snr);
    jw_kint(&w, "rx", e.rx_count);
    jw_kint(&w, "missed", e.missed);
    const bridge_device_mapping_t *m = matter_device_manager_get_mapping_by_ieee(ieee);
    jw_kint(&w, "matterEndpoint", m ? m->matter_endpoint : -1);
    jw_kbool(&w, "zlinky", strncmp(e.model, "ZLinky", 6) == 0);
    jw_kint(&w, "linkyMode", e.mode_known ? e.linky_mode : -1);
    jw_kbool(&w, "tmpl", t && !t->dyn_only);
    jw_kbool(&w, "ha", cs->enable_mqtt && g->ha_mqtt);
    jw_kstr(&w, "status", dd ? dd->info.status : "");
    write_device_body(&w, ieee, t, dd, mode, now);
    jw_key(&w, "actions");
    jw_raw(&w, "[]");
    jw_obj_end(&w);
    return jw_send(req, &w);
}

static esp_err_t get_device(httpd_req_t *req)
{
    const zigate_device_t *d = query_device(req);
    if (!d) {
        char v[24];
        uint64_t ieee;
        if ((query_param(req, "ieee", v, sizeof(v)) || query_param(req, "id", v, sizeof(v))) &&
            parse_ieee(v, &ieee)) {
            int slot = lora_find_slot_by_ieee(ieee);
            if (slot >= 0) return get_lora_device(req, ieee, slot);
        }
        return send_not_found(req, "Appareil inconnu");
    }

    const cfg_settings_t *cs = cfg_get_settings();
    const cfg_general_t *g = cfg_get_general();
    const devdata_template_t *t = device_template(d);
    const device_data_t *dd = device_data_find(d->ieee_addr);
    int mode = display_mode(d);
    uint32_t now = now_s();

    jw_t w = {};
    jw_obj(&w);
    write_device_header(&w, d);
    jw_kint(&w, "profile", d->profile_id);
    jw_kbool(&w, "configured", d->configured);
    jw_kbool(&w, "zlinky", is_zlinky(d));
    jw_kint(&w, "linkyMode", mode);
    jw_kbool(&w, "tmpl", t && !t->dyn_only);
    jw_kbool(&w, "ha", cs->enable_mqtt && g->ha_mqtt);
    jw_kbool(&w, "mqttConnected", mqtt_manager_is_connected());
    jw_kint(&w, "cover", cover_lift(d->ieee_addr));
    char sw[40];
    basic_text(d->ieee_addr, 0x4000, sw, sizeof(sw));
    jw_kstr(&w, "swVersion", sw);
    jw_kstr(&w, "status", dd ? dd->info.status : "");

    jw_key(&w, "inClusters");
    jw_arr(&w);
    for (int c = 0; c < d->in_cluster_count && c < 32; c++) {
        char cl[12];
        snprintf(cl, sizeof(cl), "%04X", d->in_clusters[c]);
        jw_vstr(&w, cl);
    }
    jw_arr_end(&w);
    jw_key(&w, "outClusters");
    jw_arr(&w);
    for (int c = 0; c < d->out_cluster_count && c < 32; c++) {
        char cl[12];
        snprintf(cl, sizeof(cl), "%04X", d->out_clusters[c]);
        jw_vstr(&w, cl);
    }
    jw_arr_end(&w);

    write_device_body(&w, d->ieee_addr, t, dd, mode, now);
    jw_obj_end(&w);
    return jw_send(req, &w);
}

/** Valeur stockee d'un attribut (portage de /zigbeeReadValue). */
static esp_err_t get_read_value(httpd_req_t *req)
{
    const zigate_device_t *d = query_device(req);
    long cl = query_long(req, "cluster", -1), at = query_long(req, "attr", -1);
    if (!d) return send_not_found(req, "Appareil inconnu");
    if (cl < 0 || cl > 0xFFFF || at < 0 || at > 0xFFFF) return send_bad(req, "cluster / attr invalides");

    const devdata_attr_t *a = device_data_get_attr(d->ieee_addr, (uint16_t)cl, (uint16_t)at);
    jw_t w = {};
    jw_obj(&w);
    if (a) {
        char hex[DEVDATA_VALUE_LEN], txt[96];
        strlcpy(hex, a->value, sizeof(hex));
        // Lecture generique : nombre (signe selon le type) ou texte
        int64_t iv;
        // Chaines (y compris d'octets : STGE du ZLinky) : formatage de la fiche
        bool text = (a->type >= 0x41 && a->type <= 0x44);
        if (!text && device_data_hex_to_int64(hex, a->type, (uint16_t)cl, (uint16_t)at, &iv) && strlen(hex) <= 16) {
            snprintf(txt, sizeof(txt), "%lld", (long long)iv);
        } else {
            device_data_format_value((uint16_t)cl, (uint16_t)at, hex, a->type, "", 1.0f, txt, sizeof(txt));
        }
        jw_kstr(&w, "value", hex);
        jw_kstr(&w, "text", txt);
        jw_kint(&w, "type", a->type);
        jw_kint(&w, "age", (long long)(now_s() - a->last_update));
    } else {
        jw_kstr(&w, "value", "");
    }
    jw_obj_end(&w);
    return jw_send(req, &w);
}

/** Etat du coordinateur (portage de handleConfigZigbee) et de l'appairage. */
static esp_err_t get_coordinator(httpd_req_t *req)
{
    zigate_coord_info_t ci;
    zigate_get_coord_info(&ci);
    uint32_t up = zigate_uptime_s();

    jw_t w = {};
    jw_obj(&w);
    jw_kbool(&w, "versionKnown", ci.version_known);
    if (ci.version_known) {
        char v[48];
        // Meme presentation que la LiXee-Box : "SDK: <octet 1> Ver: <octets 2-3>"
        snprintf(v, sizeof(v), "SDK: %u Ver: %02x%02x", ci.version[1], ci.version[2], ci.version[3]);
        jw_kstr(&w, "version", v);
        snprintf(v, sizeof(v), "%02X%02X %02X%02X", ci.version[0], ci.version[1], ci.version[2], ci.version[3]);
        jw_kstr(&w, "versionRaw", v);
    }
    jw_kbool(&w, "stateKnown", ci.state_known);
    jw_kieee(&w, "ieee", ci.ieee);
    jw_khex4(&w, "shortAddr", ci.short_addr);
    jw_khex4(&w, "panId", ci.pan_id);
    char ext[20];
    snprintf(ext, sizeof(ext), "%016llX", (unsigned long long)ci.ext_pan_id);
    jw_kstr(&w, "extPanId", ext);
    jw_kint(&w, "channel", ci.channel);
    bool up_net = (ci.state_known && ci.short_addr != 0xFFFF) ||
                  ci.start_status == 0 || ci.start_status == 1;
    jw_kbool(&w, "network", up_net);
    jw_kint(&w, "startStatus", ci.start_status);
    jw_kbool(&w, "rawMode", ci.raw_mode);
    jw_kint(&w, "led", ci.led);
    long long pj = 0;
    if (ci.permit_join_until == UINT32_MAX) pj = -1;
    else if (ci.permit_join_until > up) pj = ci.permit_join_until - up;
    jw_kint(&w, "permitJoin", pj);
    jw_kbool(&w, "ghostPending", ci.ghost_pending);
    jw_kint(&w, "ghostResult", ci.ghost_result);
    jw_kint(&w, "framesRx", ci.frames_rx);
    jw_kint(&w, "framesTx", ci.frames_tx);
    jw_kint(&w, "devices", zigate_devices_count());
    jw_kint(&w, "joinSeq", ci.join_seq);
    if (ci.join_seq) {
        jw_key(&w, "lastJoin");
        jw_obj(&w);
        jw_kieee(&w, "ieee", ci.last_join_ieee);
        jw_kint(&w, "short", ci.last_join_short);
        jw_kint(&w, "since", up - ci.last_join_s);
        const zigate_device_t *d = zigate_devices_find_by_ieee(ci.last_join_ieee);
        if (d) {
            jw_kstr(&w, "model", d->model);
            jw_kstr(&w, "manufacturer", d->manufacturer);
            jw_kstr(&w, "alias", d->alias);
            jw_kint(&w, "deviceId", d->device_id);
            jw_kbool(&w, "tmpl", device_template(d) != NULL);
        }
        jw_obj_end(&w);
    }
    jw_obj_end(&w);
    return jw_send(req, &w);
}

typedef struct { jw_t *w; } console_ctx_t;

static void console_line_cb(uint32_t seq, const char *line, void *ctx)
{
    jw_vstr(((console_ctx_t *)ctx)->w, line);
}

/** Console des trames (portage de /logs + /getLogBuffer). */
static esp_err_t get_console(httpd_req_t *req)
{
    long since = query_long(req, "since", 0);
    long max = query_long(req, "max", 200);
    if (since < 0) since = 0;
    if (max <= 0 || max > ZIGATE_CONSOLE_LINES) max = ZIGATE_CONSOLE_LINES;

    jw_t w = {};
    jw_obj(&w);
    jw_key(&w, "lines");
    jw_arr(&w);
    console_ctx_t ctx = { &w };
    uint32_t last = zigate_console_foreach((uint32_t)since, (size_t)max, console_line_cb, &ctx);
    jw_arr_end(&w);
    jw_kint(&w, "last", last);
    zigate_coord_info_t ci;
    zigate_get_coord_info(&ci);
    jw_kbool(&w, "rawMode", ci.raw_mode);
    jw_obj_end(&w);
    return jw_send(req, &w);
}

// ============================================================================
// POST
// ============================================================================

static uint8_t device_endpoint(const zigate_device_t *d, const cJSON *j)
{
    long ep;
    if (json_long(j, "endpoint", &ep) && ep > 0 && ep <= 240) return (uint8_t)ep;
    return d->endpoint ? d->endpoint : 1;
}

/**
 * Action de template (portage de /ZigbeeAction). Par nom ({"name"}) : le
 * serveur reprend commande, endpoint et valeur du template. Explicite : la
 * page envoie les champs d'une action, la valeur pouvant etre remplacee
 * (curseur de volet).
 */
static esp_err_t post_action(httpd_req_t *req, cJSON *j)
{
    zigate_device_t *d = body_device(j);
    if (!d) return send_not_found(req, "Appareil inconnu");

    long command = 0, value = 0, cluster = 0, mfr = 0, ep = 0;
    const char *name = json_str(j, "name");
    long idx = -1;
    bool by_tmpl = (name && name[0]) || json_long(j, "index", &idx);
    if (by_tmpl) {
        const devdata_template_t *t = device_template(d);
        const devdata_template_action_t *a = NULL;
        for (int i = 0; t && i < t->action_count; i++) {
            if ((idx >= 0 && i == idx) || (idx < 0 && strcasecmp(t->actions[i].name, name) == 0)) {
                a = &t->actions[i];
                break;
            }
        }
        if (!a) return send_not_found(req, "Action absente du template");
        command = a->command; value = a->value; cluster = a->cluster;
        mfr = a->manufacturer_code; ep = a->endpoint;
        json_long(j, "value", &value);                 // curseur : valeur choisie
    } else {
        if (!json_long(j, "command", &command)) return send_bad(req, "command requis");
        json_long(j, "value", &value);
        json_long(j, "cluster", &cluster);
        json_long(j, "mfr", &mfr);
        json_long(j, "endpoint", &ep);
    }
    if (ep < 0 || ep > 255 || cluster < 0 || cluster > 0xFFFF || mfr < 0 || mfr > 0xFFFF) {
        return send_bad(req, "Parametre hors plage");
    }
    if (ep == 0) ep = d->endpoint ? d->endpoint : 1;

    ESP_LOGI(TAG, "Action web : cmd=%ld adr=0x%04X ep=%ld val=%ld", command, d->short_addr, ep, value);
    return send_zigate_result(req, zigate_action_send((int)command, d->short_addr, (uint8_t)ep,
                                                      (int32_t)value, (uint16_t)cluster, (uint16_t)mfr));
}

/** Code fabricant d'un attribut d'apres le template, 0 sinon. */
static uint16_t template_mfr(const zigate_device_t *d, uint16_t cl, uint16_t at)
{
    const devdata_template_t *t = device_template(d);
    for (int i = 0; t && i < t->state_count; i++) {
        if (t->states[i].cluster == cl && t->states[i].attribute == at) return t->states[i].manufacturer_code;
    }
    return 0;
}

static bool body_cluster_attr(const cJSON *j, long *cl, long *at)
{
    return json_long(j, "cluster", cl) && json_long(j, "attr", at) &&
           *cl >= 0 && *cl <= 0xFFFF && *at >= 0 && *at <= 0xFFFF;
}

/**
 * Ecriture d'attribut (portage de /ZigbeeWriteAttribut). Cluster 0xEF00 :
 * datapoint Tuya (attr = numero de DP, type = type de DP du template).
 * La valeur est la valeur BRUTE (la page divise deja par le coefficient).
 */
static esp_err_t post_write(httpd_req_t *req, cJSON *j)
{
    zigate_device_t *d = body_device(j);
    if (!d) return send_not_found(req, "Appareil inconnu");
    long cl, at, type;
    if (!body_cluster_attr(j, &cl, &at)) return send_bad(req, "cluster / attr invalides");
    if (!json_long(j, "type", &type) || type < 0 || type > 0xFF) return send_bad(req, "type requis");

    // Valeur : nombre JSON ou texte
    char text[96];
    const cJSON *v = cJSON_GetObjectItem(j, "value");
    if (cJSON_IsNumber(v)) {
        double dv = v->valuedouble;
        if (type == 0x39) snprintf(text, sizeof(text), "%g", dv);
        else snprintf(text, sizeof(text), "%lld", (long long)(dv < 0 ? dv - 0.5 : dv + 0.5));
    } else if (cJSON_IsString(v)) {
        strlcpy(text, v->valuestring, sizeof(text));
    } else if (cJSON_IsBool(v)) {
        strlcpy(text, cJSON_IsTrue(v) ? "1" : "0", sizeof(text));
    } else {
        return send_bad(req, "value requis");
    }

    if (cl == 0xEF00) {
        long ep = 1;
        json_long(j, "endpoint", &ep);
        char *end;
        long long iv = strtoll(text, &end, 0);
        if (end == text || type > TUYA_DP_BITMAP) return send_bad(req, "Valeur Tuya invalide");
        ESP_LOGI(TAG, "Ecriture Tuya DP%ld type %ld = %lld", at, type, iv);
        return send_zigate_result(req, zigate_tuya_dp_set(d->short_addr, (uint8_t)(ep > 0 ? ep : 1),
                                                          (uint8_t)at, (uint8_t)type, (uint32_t)iv));
    }

    uint8_t buf[72];
    int n = zigate_zcl_encode((uint8_t)type, text, buf, sizeof(buf));
    if (n <= 0) return send_bad(req, "Valeur incompatible avec le type de l'attribut");
    long mfr;
    if (!json_long(j, "mfr", &mfr)) mfr = template_mfr(d, (uint16_t)cl, (uint16_t)at);
    // Ligne propre a un endpoint : attribut reel, ecrit sur cet endpoint
    uint8_t kep = 0;
    uint16_t zat = device_data_resolve_key(d->ieee_addr, (uint16_t)cl, (uint16_t)at, &kep);
    ESP_LOGI(TAG, "Ecriture 0x%04X cl=0x%04lX at=0x%04X type=0x%02lX val=%s",
             d->short_addr, cl, zat, type, text);
    return send_zigate_result(req, zigate_write_attribute_mfr(d->short_addr, kep ? kep : device_endpoint(d, j),
                                                              (uint16_t)cl, zat, (uint8_t)type,
                                                              buf, (uint16_t)n, (uint16_t)mfr));
}

/** Lecture d'attribut (portage de /ZigbeeReadAttribut, /ZigbeeSendRequest). */
static esp_err_t post_read(httpd_req_t *req, cJSON *j)
{
    zigate_device_t *d = body_device(j);
    if (!d) return send_not_found(req, "Appareil inconnu");
    long cl, at;
    if (!body_cluster_attr(j, &cl, &at)) return send_bad(req, "cluster / attr invalides");
    if (cl == 0xEF00) {
        // Tuya : pas de lecture d'un DP isole, demande de tous les DP
        return send_zigate_result(req, zigate_tuya_dp_query(d->short_addr, device_endpoint(d, j)));
    }
    long mfr;
    if (!json_long(j, "mfr", &mfr)) mfr = template_mfr(d, (uint16_t)cl, (uint16_t)at);
    uint8_t kep = 0;
    uint16_t zat = device_data_resolve_key(d->ieee_addr, (uint16_t)cl, (uint16_t)at, &kep);
    return send_zigate_result(req, zigate_read_attribute_mfr(d->short_addr, kep ? kep : device_endpoint(d, j),
                                                             (uint16_t)cl, zat, (uint16_t)mfr));
}

/** Relecture des attributs Basic (bouton "Refresh" de /configDevices). */
static esp_err_t post_refresh(httpd_req_t *req, cJSON *j)
{
    zigate_device_t *d = body_device(j);
    if (!d) return send_not_found(req, "Appareil inconnu");
    uint16_t sa = d->short_addr;
    uint8_t ep = device_endpoint(d, j);
    esp_err_t err = zigate_read_attribute(sa, ep, 0x0000, 0x0004);   // fabricant
    vTaskDelay(pdMS_TO_TICKS(150));
    if (err == ESP_OK) err = zigate_read_attribute(sa, ep, 0x0000, 0x0005);   // modele
    vTaskDelay(pdMS_TO_TICKS(150));
    if (err == ESP_OK) err = zigate_read_attribute(sa, ep, 0x0000, 0x4000);   // version logicielle
    vTaskDelay(pdMS_TO_TICKS(150));
    if (err == ESP_OK) err = zigate_read_attribute(sa, ep, 0x0000, 0x0007);   // source d'alimentation
    return send_zigate_result(req, err);
}

static esp_err_t post_configure(httpd_req_t *req, cJSON *j)
{
    zigate_device_t *d = body_device(j);
    if (!d) return send_not_found(req, "Appareil inconnu");
    device_setup_request(d->ieee_addr, true);
    return send_status(req, "200 OK", "Configuration (bind, reporting) relancee");
}

/**
 * Alias (portage de /setAlias). Accepte aussi un appareil connu de
 * device_data seulement (emetteurs LoRa, page_lora.js), comme l'ancienne
 * route de web_api.
 */
static esp_err_t post_alias(httpd_req_t *req, cJSON *j)
{
    zigate_device_t *d = body_device(j);
    uint64_t ieee = 0;
    if (d) ieee = d->ieee_addr;
    else parse_ieee(json_str(j, "ieee"), &ieee);
    device_data_t *dd = ieee ? device_data_find(ieee) : NULL;
    const char *alias = json_str(j, "alias");
    if (!d && !dd) return send_not_found(req, "Appareil inconnu");
    if (!alias) return send_bad(req, "alias requis");

    // Copie sans caracteres de controle (affiche dans les pages et en MQTT)
    char clean[DEVDATA_ALIAS_LEN];
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)alias; *p && o + 1 < sizeof(clean); p++) {
        if (*p >= 0x20) clean[o++] = (char)*p;
    }
    clean[o] = '\0';
    // Ne pas couper un caractere UTF-8 multi-octets en fin de chaine
    while (o > 0 && ((unsigned char)clean[o - 1] & 0xC0) == 0x80) o--;
    if (o > 0 && ((unsigned char)clean[o - 1] & 0x80)) {
        unsigned char lead = (unsigned char)clean[o - 1];
        size_t need = (lead >= 0xF0) ? 4 : (lead >= 0xE0) ? 3 : 2;
        size_t have = strlen(clean) - (o - 1);
        if (have < need) clean[o - 1] = '\0';
    }

    esp_err_t err = ESP_OK;
    if (d) {
        strlcpy(d->alias, clean, sizeof(d->alias));
        err = zigate_devices_save();
    }
    if (dd) {
        strlcpy(dd->info.alias, clean, sizeof(dd->info.alias));
        dd->dirty = true;
        esp_err_t e2 = device_data_save(ieee);
        if (!d) err = e2;
    }
    if (err != ESP_OK) return send_status(req, "500 Internal Server Error", "Echec d'enregistrement");
    return send_ok(req);
}

/**
 * Suppression locale d'un appareil Zigbee : endpoint Matter, donnees,
 * fichiers, entree de la base ZiGate. Voir web_zigbee_remove.h : a appeler
 * sur la tache zigate_proc uniquement (route web et depart 0x8048).
 */
void web_zigbee_remove_device_local(uint64_t ieee)
{
    if (!ieee) return;
    matter_device_manager_unmap_device(ieee);
    device_data_remove(ieee);                    // supprime aussi /littlefs/db/<IEEE>.json
    char path[48];
    snprintf(path, sizeof(path), "/littlefs/bk/%016llX.json", (unsigned long long)ieee);
    unlink(path);                                // copie de sauvegarde eventuelle
    zigate_devices_remove(ieee);
    ESP_LOGI(TAG, "Appareil %016llX supprime", (unsigned long long)ieee);
}

/** Execute par zigate_proc (zigate_run_on_proc) ; arg : uint64_t alloue. */
static void remove_on_proc(void *arg)
{
    uint64_t ieee = *(uint64_t *)arg;
    free(arg);
    web_zigbee_remove_device_local(ieee);
}

/**
 * Suppression (portage de /deleteDevice) : demande de depart du reseau, puis
 * suppression locale sans attendre la reponse (un appareil absent ou
 * endormi ne repondrait jamais) : endpoint Matter, donnees, fichiers.
 *
 * Correction (corruption de tas) : la suppression locale tournait ici, sur la
 * tache HTTP, pendant que la reponse 0x8048 de l'appareil faisait executer la
 * meme suppression par la tache zigate_proc (callback de depart), et que
 * cette tache traitait des rapports d'attributs du meme appareil. Elle est
 * desormais postee sur zigate_proc : les deux chemins s'executent l'un apres
 * l'autre (le second ne trouve plus rien), dans l'ordre des trames.
 */
static esp_err_t post_delete(httpd_req_t *req, cJSON *j)
{
    zigate_device_t *d = body_device(j);
    if (!d) return send_not_found(req, "Appareil inconnu");
    const uint64_t ieee = d->ieee_addr;         // d n'est plus utilise ensuite

    zigate_leave_request(ieee);

    uint64_t *arg = (uint64_t *)malloc(sizeof(*arg));
    if (!arg) return send_status(req, "500 Internal Server Error", "Memoire insuffisante");
    *arg = ieee;
    esp_err_t err = zigate_run_on_proc(remove_on_proc, arg);
    if (err != ESP_OK) {
        free(arg);                               // remove_on_proc ne sera pas appelee
        return send_status(req, "503 Service Unavailable", "ZiGate occupee, reessayer");
    }

    // Attente bornee de la suppression (quelques ms d'ordinaire, plus si la
    // tache ZiGate est dans une decouverte) : la page recharge la liste juste
    // apres la reponse et ne doit plus y voir l'appareil.
    for (int i = 0; i < 30 && zigate_devices_find_by_ieee(ieee); i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return send_ok(req);
}

static esp_err_t post_discover(httpd_req_t *req, cJSON *j)
{
    zigate_device_t *d = body_device(j);
    int count = 0;
    esp_err_t err;
    uint64_t ieee;
    if (d) {
        err = mqtt_manager_publish_discovery(d->ieee_addr, &count);
    } else if (parse_ieee(json_str(j, "ieee"), &ieee) && lora_find_slot_by_ieee(ieee) >= 0) {
        // Emetteur LoRa : hors de la base Zigbee, modele connu du recepteur
        lora_emitter_info_t e;
        if (!lora_get_emitter(lora_find_slot_by_ieee(ieee), &e)) return send_not_found(req, "Appareil inconnu");
        const device_data_t *dd = device_data_find(ieee);
        err = mqtt_manager_publish_discovery_info(ieee, e.model, dd ? dd->info.alias : "", "LiXee", &count);
    } else {
        return send_not_found(req, "Appareil inconnu");
    }
    if (err == ESP_ERR_INVALID_STATE) return send_status(req, "409 Conflict", "MQTT non connecte");
    if (err == ESP_ERR_NOT_FOUND) return send_not_found(req, "Aucune entite a publier (template absent ?)");
    if (err != ESP_OK) return send_status(req, "500 Internal Server Error", "Echec de publication MQTT");
    char msg[64];
    snprintf(msg, sizeof(msg), "%d entite(s) publiee(s)", count);
    return send_status(req, "200 OK", msg);
}

static esp_err_t post_permit_join(httpd_req_t *req, cJSON *j)
{
    // Duree bornee a 254 s : 255 ouvrirait l'appairage sans limite
    long dur = 60;
    json_long(j, "duration", &dur);
    if (dur < 0) dur = 0;
    if (dur > 254) dur = 254;
    return send_zigate_result(req, zigate_permit_join((uint8_t)dur));
}

/**
 * Commandes du coordinateur (portage de /cmdGetVersion, /cmdErasePDM,
 * /cmdStartNwk, /cmdNetwork, /cmdReset, /cmdSetLed, /cmdSetChannelMask,
 * /cmdPermitJoin, /cmdRawMode(Off), /cmdActiveReq, /cmdCleanGhosts,
 * /cmdClearConsole). Toutes en POST : la LiXee-Box les exposait en GET,
 * declenchables par un simple lien (RAZ du reseau comprise).
 */
static esp_err_t post_cmd(httpd_req_t *req, cJSON *j)
{
    const char *cmd = json_str(j, "cmd");
    if (!cmd) return send_bad(req, "cmd requis");
    long value = 0;
    bool has_value = json_long(j, "value", &value);
    esp_err_t err;

    ESP_LOGI(TAG, "Commande coordinateur '%s'", cmd);
    if (!strcasecmp(cmd, "GetVersion")) {
        err = zigate_get_version();
    } else if (!strcasecmp(cmd, "ErasePDM")) {
        zigate_console_printf("Effacement de la PDM demande depuis l'interface");
        err = zigate_erase_pdm();
    } else if (!strcasecmp(cmd, "StartNwk")) {
        err = zigate_start_network();
    } else if (!strcasecmp(cmd, "Network")) {
        // Etat du reseau : 0x0009 (adresse, PAN, canal) et 0x0025
        err = zigate_send_command(ZIGATE_CMD_NETWORK_STATE, NULL, 0);
        if (err == ESP_OK) err = zigate_get_network_state();
    } else if (!strcasecmp(cmd, "Reset")) {
        err = zigate_soft_reset();
    } else if (!strcasecmp(cmd, "HardReset")) {
        err = zigate_reset();
    } else if (!strcasecmp(cmd, "SetLed")) {
        if (!has_value) return send_bad(req, "value requis (0 ou 1)");
        err = zigate_set_led(value != 0);
    } else if (!strcasecmp(cmd, "SetChannelMask")) {
        if (!has_value || value <= 0) return send_bad(req, "Canal (11 a 26) ou masque requis");
        err = zigate_set_channel_mask((uint32_t)value);
        if (err == ESP_ERR_INVALID_ARG) return send_bad(req, "Aucun canal 11 a 26 designe");
    } else if (!strcasecmp(cmd, "PermitJoin")) {
        err = zigate_permit_join(has_value && value > 0 && value <= 254 ? (uint8_t)value : 30);
    } else if (!strcasecmp(cmd, "RawMode")) {
        err = zigate_set_raw_mode(true);
    } else if (!strcasecmp(cmd, "RawModeOff")) {
        err = zigate_set_raw_mode(false);
    } else if (!strcasecmp(cmd, "ActiveReq")) {
        // La LiXee-Box visait une adresse figee dans le code : ici celle saisie
        if (!has_value || value < 0 || value > 0xFFFF) return send_bad(req, "Adresse courte requise");
        err = zigate_request_active_endpoints((uint16_t)value);
    } else if (!strcasecmp(cmd, "CleanGhosts")) {
        err = zigate_clean_ghosts();
    } else if (!strcasecmp(cmd, "ClearConsole")) {
        zigate_console_clear();
        err = ESP_OK;
    } else {
        return send_bad(req, "Commande inconnue");
    }
    return send_zigate_result(req, err);
}

// ============================================================================
// Mise a jour OTA d'un appareil (LiXee-Box : /ota, /OTAUpdateBar)
// Le fichier est depose par /api/lora/ota/init|chunk|finish?id=<IEEE>, qui
// lance aussi la mise a jour, comme la LiXee-Box.
// ============================================================================

static const char *ota_state_name(zigate_ota_state_t s)
{
    switch (s) {
    case ZIGATE_OTA_NOTIFIED: return "notified";
    case ZIGATE_OTA_TRANSFER: return "transfer";
    case ZIGATE_OTA_DONE:     return "done";
    case ZIGATE_OTA_ERROR:    return "error";
    default:                  return "idle";
    }
}

static esp_err_t get_ota_status(httpd_req_t *req)
{
    zigate_ota_status_t st;
    zigate_ota_get_status(&st);
    jw_t w = {};
    jw_obj(&w);
    jw_kstr(&w, "state", ota_state_name(st.state));
    if (st.ieee) jw_kieee(&w, "ieee", st.ieee); else jw_kstr(&w, "ieee", "");
    jw_kint(&w, "pct", st.pct);
    jw_kint(&w, "offset", st.offset);
    jw_kint(&w, "total", st.total);
    char v[12];
    snprintf(v, sizeof(v), "0x%08lX", (unsigned long)st.file_version);
    jw_kstr(&w, "fileVersion", v);
    jw_khex4(&w, "imageType", st.image_type);
    jw_khex4(&w, "manufacturerCode", st.manu_code);
    jw_kint(&w, "since", st.state == ZIGATE_OTA_IDLE ? -1 : (long long)st.since_s);
    jw_kstr(&w, "msg", st.msg);
    // Fichier deja depose pour l'appareil demande (?ieee=)
    zigate_device_t *d = query_device(req);
    if (d) {
        char path[48];
        struct stat s;
        zigate_ota_file_path(d->ieee_addr, path, sizeof(path));
        jw_kbool(&w, "file", stat(path, &s) == 0 && s.st_size > 0);
    }
    jw_obj_end(&w);
    return jw_send(req, &w);
}

static esp_err_t post_ota_start(httpd_req_t *req, cJSON *j)
{
    zigate_device_t *d = body_device(j);
    if (!d) return send_not_found(req, "Appareil inconnu");
    char err[80] = "";
    esp_err_t e = zigate_ota_start(d->ieee_addr, err, sizeof(err));
    if (e == ESP_OK) return send_status(req, "200 OK", "Image annoncee : l'appareil va la telecharger");
    return send_status(req, e == ESP_ERR_INVALID_STATE ? "409 Conflict" : "400 Bad Request", err);
}

static esp_err_t post_ota_cancel(httpd_req_t *req, cJSON *j)
{
    (void)j;
    zigate_ota_cancel();
    return send_status(req, "200 OK", "Mise a jour abandonnee");
}

/**
 * GET /ZigbeeAction?<adresse>&<commande>&<endpoint>&<valeur>[&<cluster>&<fabricant>]
 * (handleZigbeeAction de la LiXee-Box) : parametres positionnels, adresse
 * courte en decimal ; commande 200/201 : telecommande IR (valeur base64 pour
 * 201). Toujours 200 et corps vide, comme la Box.
 */
static esp_err_t legacy_zigbee_action(httpd_req_t *req)
{
    // GET qui agit : refuse depuis une page d'un autre site (<img src=...>)
    if (!web_server_origin_ok(req)) {
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_sendstr(req, "Forbidden");
    }
    static char q[640];                      // tache HTTP seule : hors de sa pile
    static char arg[6][512];
    q[0] = 0;
    memset(arg, 0, sizeof(arg));
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        char *save = NULL;
        int k = 0;
        for (char *tok = strtok_r(q, "&", &save); tok && k < 6; tok = strtok_r(NULL, "&", &save), k++) {
            char *eq = strchr(tok, '=');
            const char *v = eq ? eq + 1 : tok;
            size_t o = 0;
            for (size_t i = 0; v[i] && o + 1 < sizeof(arg[k]); i++) {
                if (v[i] == '%' && isxdigit((unsigned char)v[i + 1]) && isxdigit((unsigned char)v[i + 2])) {
                    char hx[3] = { v[i + 1], v[i + 2], 0 };
                    arg[k][o++] = (char)strtol(hx, NULL, 16);
                    i += 2;
                } else {
                    arg[k][o++] = v[i];               // '+' garde tel quel : base64
                }
            }
        }
    }
    long sa = strtol(arg[0], NULL, 10), cmd = strtol(arg[1], NULL, 10), ep = strtol(arg[2], NULL, 10);
    const zigate_device_t *d = (sa > 0 && sa <= 0xFFFF) ? zigate_devices_find_by_short((uint16_t)sa) : NULL;
    if (d && cmd == 200) {
        zigate_ir_learn(d->ieee_addr, strcmp(arg[3], "1") == 0 || strcasecmp(arg[3], "on") == 0);
    } else if (d && cmd == 201) {
        if (arg[3][0]) zigate_ir_send_code(d->ieee_addr, arg[3]);
    } else if (d && cmd > 0) {
        long cl = arg[4][0] ? strtol(arg[4], NULL, 10) : 0, mfr = arg[5][0] ? strtol(arg[5], NULL, 10) : 0;
        zigate_action_send((int)cmd, d->short_addr, (uint8_t)(ep > 0 ? ep : 1), (int32_t)strtol(arg[3], NULL, 10),
                           (uint16_t)cl, (uint16_t)mfr);
    }
    ESP_LOGI(TAG, "ZigbeeAction (LiXee-Box) : adr=%ld cmd=%ld ep=%ld%s", sa, cmd, ep, d ? "" : " (appareil inconnu)");
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, NULL, 0);
}

/** GET /OTAUpdateBar?id=<IEEE> (LiXee-Box) : pourcentage, -1 sans OTA, -2 inconnu. */
static esp_err_t legacy_ota_bar(httpd_req_t *req)
{
    zigate_device_t *d = query_device(req);
    zigate_ota_status_t st;
    zigate_ota_get_status(&st);
    int r = -2;
    if (d) {
        bool running = st.ieee == d->ieee_addr &&
                       (st.state == ZIGATE_OTA_NOTIFIED || st.state == ZIGATE_OTA_TRANSFER);
        r = running ? st.pct : -1;
    }
    char b[8];
    snprintf(b, sizeof(b), "%d", r);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, b);
}

// ============================================================================
// Telecommande infrarouge : GET /api/zigbee/ir?ieee=, POST /api/zigbee/ir
//   {"ieee", "op":"learn"|"stop"|"send"|"save"|"delete", "code", "name", "index"}
// ============================================================================

static esp_err_t get_ir(httpd_req_t *req)
{
    zigate_device_t *d = query_device(req);
    if (!d) return send_not_found(req, "Appareil inconnu");
    char *js = zigate_ir_status_json(d->ieee_addr);
    if (!js) return send_status(req, "500 Internal Server Error", "Memoire insuffisante");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_sendstr(req, js);
    free(js);
    return e;
}

static esp_err_t ir_result(httpd_req_t *req, esp_err_t e, const char *ok_msg)
{
    if (e == ESP_OK) return send_status(req, "200 OK", ok_msg);
    if (e == ESP_ERR_NOT_FOUND) return send_not_found(req, "Code ou appareil introuvable");
    if (e == ESP_ERR_INVALID_ARG) return send_bad(req, "Parametre invalide");
    if (e == ESP_ERR_NO_MEM) return send_status(req, "507 Insufficient Storage", "Liste des codes pleine");
    return send_status(req, "500 Internal Server Error", "Echec");
}

static esp_err_t post_ir(httpd_req_t *req, cJSON *j)
{
    zigate_device_t *d = body_device(j);
    if (!d) return send_not_found(req, "Appareil inconnu");
    const char *op = json_str(j, "op");
    const char *code = json_str(j, "code");
    long idx = -1;
    json_long(j, "index", &idx);
    if (!op) return send_bad(req, "op requis");
    if (!strcmp(op, "learn")) return ir_result(req, zigate_ir_learn(d->ieee_addr, true), "Apprentissage : visez le recepteur et appuyez sur la touche");
    if (!strcmp(op, "stop"))  return ir_result(req, zigate_ir_learn(d->ieee_addr, false), "Apprentissage arrete");
    if (!strcmp(op, "send")) {
        esp_err_t e = (code && code[0]) ? zigate_ir_send_code(d->ieee_addr, code)
                                        : zigate_ir_send_saved(d->ieee_addr, (int)idx);
        return ir_result(req, e, "Code envoye");
    }
    if (!strcmp(op, "save"))   return ir_result(req, zigate_ir_save(d->ieee_addr, json_str(j, "name"), code), "Code enregistre");
    if (!strcmp(op, "delete")) return ir_result(req, zigate_ir_delete(d->ieee_addr, (int)idx), "Code supprime");
    return send_bad(req, "op inconnu");
}

// ============================================================================
// Aiguillage
// ============================================================================

/** Partie de l'URI apres "/api/zigbee/", sans la query. */
static void route_name(httpd_req_t *req, char *out, size_t len)
{
    const char *p = req->uri + strlen("/api/zigbee/");
    size_t n = strcspn(p, "?");
    if (n >= len) n = len - 1;
    memcpy(out, p, n);
    out[n] = '\0';
}

static esp_err_t zigbee_get_handler(httpd_req_t *req)
{
    char r[32];
    route_name(req, r, sizeof(r));
    if (!strcmp(r, "devices"))      return get_devices(req, false);
    if (!strcmp(r, "devices/full")) return get_devices(req, true);
    if (!strcmp(r, "cards"))        return get_cards(req);
    if (!strcmp(r, "values"))       return get_values(req);
    if (!strcmp(r, "device"))       return get_device(req);
    if (!strcmp(r, "read_value"))   return get_read_value(req);
    if (!strcmp(r, "coordinator"))  return get_coordinator(req);
    if (!strcmp(r, "console"))      return get_console(req);
    if (!strcmp(r, "ota/status"))   return get_ota_status(req);
    if (!strcmp(r, "ir"))           return get_ir(req);
    return send_not_found(req, "Route inconnue");
}

static esp_err_t zigbee_post_handler(httpd_req_t *req)
{
    char r[32];
    route_name(req, r, sizeof(r));

    static const struct { const char *name; esp_err_t (*fn)(httpd_req_t *, cJSON *); } routes[] = {
        { "action",      post_action },
        { "write",       post_write },
        { "read",        post_read },
        { "refresh",     post_refresh },
        { "configure",   post_configure },
        { "alias",       post_alias },
        { "delete",      post_delete },
        { "remove",      post_delete },          // nom historique (web_api)
        { "discover",    post_discover },
        { "permit_join", post_permit_join },
        { "cmd",         post_cmd },
        { "ota/start",   post_ota_start },
        { "ota/cancel",  post_ota_cancel },
        { "ir",          post_ir },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        if (strcmp(r, routes[i].name) != 0) continue;
        // Codes infrarouges : jusqu'a ~5,5 Ko de base64
        cJSON *j = read_body_max(req, strcmp(r, "ir") == 0 ? 8192 : 1024);
        if (!j) return ESP_OK;                   // reponse d'erreur deja envoyee
        esp_err_t err = routes[i].fn(req, j);
        cJSON_Delete(j);
        return err;
    }
    return send_not_found(req, "Route inconnue");
}

void web_zigbee_register(httpd_handle_t server)
{
    // Deux entrees de la table des URI pour tout le lot. Les routes exactes
    // de web_api.cpp (/api/zigbee/devices, permit_join, remove, alias,
    // devices/full) restent servies a l'identique ici : l'ordre
    // d'enregistrement n'a donc pas d'importance.
    httpd_uri_t get = {};
    get.uri = "/api/zigbee/*";
    get.method = HTTP_GET;
    get.handler = zigbee_get_handler;
    web_server_register_uri(server, &get);

    httpd_uri_t post = {};
    post.uri = "/api/zigbee/*";
    post.method = HTTP_POST;
    post.handler = zigbee_post_handler;
    web_server_register_uri(server, &post);

    httpd_uri_t za = {};
    za.uri = "/ZigbeeAction";
    za.method = HTTP_GET;
    za.handler = legacy_zigbee_action;
    web_server_register_uri(server, &za);

    httpd_uri_t bar = {};
    bar.uri = "/OTAUpdateBar";
    bar.method = HTTP_GET;
    bar.handler = legacy_ota_bar;
    web_server_register_uri(server, &bar);
}
