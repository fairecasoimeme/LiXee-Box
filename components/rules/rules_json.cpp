// ============================================================================
// Outils communs du composant rules : chaine JSON en PSRAM, lecture JSON par
// element, actions (lecture/ecriture au format LiXee-Box), memoire, temps et
// acces aux appareils.
// ============================================================================

#include "rules_internal.h"
#include "zigate_devices.h"
#include "ntp_sync.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// ----------------------------------------------------------------------------
// Memoire : tout tampon de taille variable va en PSRAM (repli malloc)
// ----------------------------------------------------------------------------

void *rules_psram_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(size);
}

void *rules_psram_calloc(size_t n, size_t size)
{
    void *p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM);
    return p ? p : calloc(n, size);
}

void *rules_psram_realloc(void *p, size_t size)
{
    void *q = heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM);
    return q ? q : realloc(p, size);
}

char *rules_read_file(const char *path, size_t *out_len)
{
    if (out_len) *out_len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 512 * 1024) {      // borne de securite : un rules.json fait quelques dizaines de Ko
        fclose(f);
        return NULL;
    }
    char *buf = (char *)rules_psram_malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    if (out_len) *out_len = rd;
    return buf;
}

// ----------------------------------------------------------------------------
// Constructeur de chaine
// ----------------------------------------------------------------------------

static bool sb_reserve(sb_t *sb, size_t extra)
{
    if (sb->oom) return false;
    if (sb->len + extra + 1 <= sb->cap) return true;
    size_t ncap = sb->cap ? sb->cap : 256;
    while (ncap < sb->len + extra + 1) ncap *= 2;
    char *nb = (char *)rules_psram_realloc(sb->buf, ncap);
    if (!nb) {
        sb->oom = true;
        return false;
    }
    sb->buf = nb;
    sb->cap = ncap;
    return true;
}

void sb_init(sb_t *sb, size_t initial)
{
    memset(sb, 0, sizeof(*sb));
    if (initial) sb_reserve(sb, initial);
    if (sb->buf) sb->buf[0] = '\0';
}

void sb_free(sb_t *sb)
{
    free(sb->buf);
    memset(sb, 0, sizeof(*sb));
}

void sb_putc(sb_t *sb, char c)
{
    if (!sb_reserve(sb, 1)) return;
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
}

void sb_puts(sb_t *sb, const char *s)
{
    if (!s) return;
    size_t n = strlen(s);
    if (!sb_reserve(sb, n)) return;
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

void sb_printf(sb_t *sb, const char *fmt, ...)
{
    if (!sb_reserve(sb, 64)) return;
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(sb->buf + sb->len, sb->cap - sb->len, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return;
    }
    if ((size_t)n >= sb->cap - sb->len) {
        if (!sb_reserve(sb, (size_t)n)) {
            va_end(ap2);
            return;
        }
        vsnprintf(sb->buf + sb->len, sb->cap - sb->len, fmt, ap2);
    }
    va_end(ap2);
    sb->len += (size_t)n;
}

void sb_json_str(sb_t *sb, const char *s)
{
    sb_putc(sb, '"');
    if (s) {
        for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
            unsigned char c = *p;
            if (c == '"' || c == '\\') {
                sb_putc(sb, '\\');
                sb_putc(sb, (char)c);
            } else if (c == '\n') {
                sb_puts(sb, "\\n");
            } else if (c == '\r') {
                sb_puts(sb, "\\r");
            } else if (c == '\t') {
                sb_puts(sb, "\\t");
            } else if (c < 0x20 || c == 0x7F) {
                sb_printf(sb, "\\u%04x", (unsigned)c);
            } else {
                sb_putc(sb, (char)c);
            }
        }
    }
    sb_putc(sb, '"');
}

void sb_key(sb_t *sb, const char *key)
{
    sb_json_str(sb, key);
    sb_putc(sb, ':');
}

void sb_kstr(sb_t *sb, const char *key, const char *val)
{
    sb_key(sb, key);
    sb_json_str(sb, val ? val : "");
    sb_putc(sb, ',');
}

void sb_kint(sb_t *sb, const char *key, long long v)
{
    sb_key(sb, key);
    sb_printf(sb, "%lld,", v);
}

void sb_knum(sb_t *sb, const char *key, double v)
{
    sb_key(sb, key);
    if (!isfinite(v)) v = 0;
    sb_printf(sb, "%.10g,", v);
}

void sb_kbool(sb_t *sb, const char *key, bool v)
{
    sb_key(sb, key);
    sb_puts(sb, v ? "true," : "false,");
}

void sb_close(sb_t *sb, char c)
{
    if (!sb->oom && sb->len > 0 && sb->buf[sb->len - 1] == ',') {
        sb->buf[sb->len - 1] = c;
        return;
    }
    sb_putc(sb, c);
}

char *sb_take(sb_t *sb)
{
    if (sb->oom || !sb->buf) {
        sb_free(sb);
        return NULL;
    }
    char *b = sb->buf;
    sb->buf = NULL;
    sb->len = sb->cap = 0;
    return b;
}

// ----------------------------------------------------------------------------
// Lecture JSON element par element
// ----------------------------------------------------------------------------

static inline char ch_at(const char *t, size_t i, size_t n)
{
    return i < n ? t[i] : '\0';
}

static size_t skip_ws(const char *t, size_t i, size_t n)
{
    while (i < n && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || t[i] == '\n')) i++;
    return i;
}

/** t[i] == '"' : index qui suit le guillemet fermant, 0 si chaine non terminee. */
static size_t skip_string(const char *t, size_t i, size_t n)
{
    i++;
    while (i < n) {
        if (t[i] == '\\') {
            i += 2;
            continue;
        }
        if (t[i] == '"') return i + 1;
        i++;
    }
    return 0;
}

/** t[i] == '{' ou '[' : index qui suit la fermeture correspondante, 0 si erreur. */
static size_t skip_compound(const char *t, size_t i, size_t n)
{
    int depth = 0;
    while (i < n) {
        char c = t[i];
        if (c == '"') {
            size_t e = skip_string(t, i, n);
            if (!e) return 0;
            i = e;
            continue;
        }
        if (c == '{' || c == '[') {
            depth++;
        } else if (c == '}' || c == ']') {
            depth--;
            if (depth == 0) return i + 1;
            if (depth < 0) return 0;
        }
        i++;
    }
    return 0;
}

static size_t skip_value(const char *t, size_t i, size_t n)
{
    char c = ch_at(t, i, n);
    if (c == '"') return skip_string(t, i, n);
    if (c == '{' || c == '[') return skip_compound(t, i, n);
    size_t s = i;
    while (i < n && t[i] != ',' && t[i] != '}' && t[i] != ']' &&
           t[i] != ' ' && t[i] != '\t' && t[i] != '\r' && t[i] != '\n') i++;
    return i > s ? i : 0;
}

bool json_foreach_member(const char *t, size_t n,
                         bool (*cb)(const char *key, size_t klen, const char *val, size_t vlen, void *ctx),
                         void *ctx)
{
    if (!t) return false;
    size_t i = skip_ws(t, 0, n);
    if (ch_at(t, i, n) != '{') return false;
    i++;
    i = skip_ws(t, i, n);
    if (ch_at(t, i, n) == '}') return true;
    for (;;) {
        i = skip_ws(t, i, n);
        if (ch_at(t, i, n) != '"') return false;
        size_t ks = i + 1;
        size_t ke = skip_string(t, i, n);
        if (!ke) return false;
        size_t klen = ke - 1 - ks;
        i = skip_ws(t, ke, n);
        if (ch_at(t, i, n) != ':') return false;
        i = skip_ws(t, i + 1, n);
        size_t e = skip_value(t, i, n);
        if (!e) return false;
        if (!cb(t + ks, klen, t + i, e - i, ctx)) return false;
        i = skip_ws(t, e, n);
        char c = ch_at(t, i, n);
        if (c == ',') {
            i++;
            continue;
        }
        return c == '}';
    }
}

bool json_foreach_element(const char *t, size_t n,
                          bool (*cb)(const char *elem, size_t elen, void *ctx), void *ctx)
{
    if (!t) return false;
    size_t i = skip_ws(t, 0, n);
    char c = ch_at(t, i, n);
    if (c == 'n') return true;                      // null : tableau vide
    if (c != '[') return false;
    i = skip_ws(t, i + 1, n);
    if (ch_at(t, i, n) == ']') return true;
    for (;;) {
        i = skip_ws(t, i, n);
        size_t e = skip_value(t, i, n);
        if (!e) return false;
        if (!cb(t + i, e - i, ctx)) return false;
        i = skip_ws(t, e, n);
        c = ch_at(t, i, n);
        if (c == ',') {
            i++;
            continue;
        }
        return c == ']';
    }
}

typedef struct {
    const char *key;
    bool (*cb)(const char *, size_t, void *);
    void *ctx;
    bool  ok;
} arr_find_t;

static bool arr_find_cb(const char *key, size_t klen, const char *val, size_t vlen, void *ctx)
{
    arr_find_t *f = (arr_find_t *)ctx;
    if (klen == strlen(f->key) && memcmp(key, f->key, klen) == 0) {
        f->ok = json_foreach_element(val, vlen, f->cb, f->ctx);
        return f->ok;
    }
    return true;
}

bool json_foreach_array_object(const char *t, size_t n, const char *key,
                               bool (*cb)(const char *obj, size_t obj_len, void *ctx), void *ctx)
{
    arr_find_t f = { key, cb, ctx, true };
    return json_foreach_member(t, n, arr_find_cb, &f) && f.ok;
}

bool json_key_is(const char *key, size_t klen, const char *want)
{
    return klen == strlen(want) && memcmp(key, want, klen) == 0;
}

// ----------------------------------------------------------------------------
// Acces aux champs cJSON (tolerants sur les types, comme l'operateur | d'ArduinoJson)
// ----------------------------------------------------------------------------

const char *jitem_str(const cJSON *v, const char *def)
{
    return (cJSON_IsString(v) && v->valuestring) ? v->valuestring : def;
}

long long jitem_int(const cJSON *v, long long def)
{
    if (cJSON_IsNumber(v)) return (long long)v->valuedouble;
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v) ? 1 : 0;
    if (cJSON_IsString(v) && v->valuestring && v->valuestring[0]) {
        char *end;
        long long r = strtoll(v->valuestring, &end, 10);
        return (end != v->valuestring) ? r : def;
    }
    return def;
}

double jitem_num(const cJSON *v, double def)
{
    if (cJSON_IsNumber(v)) return v->valuedouble;
    if (cJSON_IsString(v) && v->valuestring && v->valuestring[0]) {
        char *end;
        double r = strtod(v->valuestring, &end);
        return (end != v->valuestring) ? r : def;
    }
    return def;
}

bool jitem_bool(const cJSON *v, bool def)
{
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v);
    if (cJSON_IsNumber(v)) return v->valuedouble != 0;
    return def;
}

const char *jstr(const cJSON *o, const char *key, const char *def)
{
    return jitem_str(cJSON_GetObjectItemCaseSensitive(o, key), def);
}

long long jint(const cJSON *o, const char *key, long long def)
{
    return jitem_int(cJSON_GetObjectItemCaseSensitive(o, key), def);
}

double jnum(const cJSON *o, const char *key, double def)
{
    return jitem_num(cJSON_GetObjectItemCaseSensitive(o, key), def);
}

bool jbool(const cJSON *o, const char *key, bool def)
{
    return jitem_bool(cJSON_GetObjectItemCaseSensitive(o, key), def);
}

void jitem_text(const cJSON *v, char *out, size_t len)
{
    if (cJSON_IsString(v) && v->valuestring) {
        jcopy(out, len, v->valuestring);
    } else if (cJSON_IsNumber(v)) {
        snprintf(out, len, "%.10g", v->valuedouble);
    } else {
        jcopy(out, len, "");
    }
}

bool jhas(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return v && !cJSON_IsNull(v);
}

void jcopy(char *dst, size_t dstlen, const char *src)
{
    if (!dstlen) return;
    strlcpy(dst, src ? src : "", dstlen);
}

/** Valeur texte ou numerique ("value" peut etre un nombre dans un fichier ecrit a la main). */
static void jvalue_text(const cJSON *o, const char *key, char *out, size_t len)
{
    jitem_text(cJSON_GetObjectItemCaseSensitive(o, key), out, len);
}

// ----------------------------------------------------------------------------
// Actions
// ----------------------------------------------------------------------------

// Meme lecture que RulesManager::loadFromFile et ActionGroupManager::parseActions.
// Ecart voulu : une "value" numerique est conservee (ArduinoJson la remplacait par "").
void rule_action_from_json(const cJSON *a, rule_action_t *act)
{
    memset(act, 0, sizeof(*act));
    jcopy(act->type, sizeof(act->type), jstr(a, "type", ""));
    jcopy(act->IEEE, sizeof(act->IEEE), jstr(a, "IEEE", ""));
    jcopy(act->actionName, sizeof(act->actionName), jstr(a, "actionName", ""));
    act->endpoint = (int32_t)jint(a, "endpoint", 1);
    act->command  = jhas(a, "command") ? (int32_t)jint(a, "command", -1) : -1;
    jvalue_text(a, "value", act->value, sizeof(act->value));
    jcopy(act->title, sizeof(act->title), jstr(a, "title", ""));
    jcopy(act->message, sizeof(act->message), jstr(a, "message", ""));
    jcopy(act->sourceIEEE, sizeof(act->sourceIEEE), jstr(a, "sourceIEEE", ""));
    act->sourceCluster   = (int32_t)jint(a, "sourceCluster", 0);
    act->sourceAttribute = (int32_t)jint(a, "sourceAttribute", 0);
    act->coefficient     = jnum(a, "coefficient", 1.0);
    act->offset          = jnum(a, "offset", 0.0);
}

bool rule_action_from_text(const char *text, size_t len, rule_action_t *act)
{
    cJSON *j = cJSON_ParseWithLength(text, len);
    if (!cJSON_IsObject(j)) {
        cJSON_Delete(j);
        return false;
    }
    rule_action_from_json(j, act);
    cJSON_Delete(j);
    return true;
}

void rule_action_to_json(sb_t *sb, const rule_action_t *a, act_style_t style)
{
    sb_putc(sb, '{');
    sb_kstr(sb, "type", a->type);
    sb_kstr(sb, "IEEE", a->IEEE);
    sb_kstr(sb, "actionName", a->actionName);
    sb_kint(sb, "endpoint", a->endpoint);
    if (style != ACT_STYLE_RULE) {
        sb_kint(sb, "command", a->command);
    } else if (a->command >= 0) {
        // Surcharge de commande ecrite a la main : conservee (l'editeur ne l'envoie pas)
        sb_kint(sb, "command", a->command);
    }
    sb_kstr(sb, "value", a->value);
    if (style != ACT_STYLE_GROUP_FILE || a->title[0])   sb_kstr(sb, "title", a->title);
    if (style != ACT_STYLE_GROUP_FILE || a->message[0]) sb_kstr(sb, "message", a->message);
    bool dyn = a->sourceIEEE[0] != '\0' || (style == ACT_STYLE_RULE && strcmp(a->type, "dynamic") == 0);
    // Aussi dans la liste des groupes (ecart avec toJson de la LiXee-Box) : l'editeur
    // renvoie l'action telle quelle, ses champs dynamiques ne doivent pas se perdre.
    if (dyn) {
        sb_kstr(sb, "sourceIEEE", a->sourceIEEE);
        sb_kint(sb, "sourceCluster", a->sourceCluster);
        sb_kint(sb, "sourceAttribute", a->sourceAttribute);
        sb_knum(sb, "coefficient", a->coefficient);
        sb_knum(sb, "offset", a->offset);
    }
    sb_close(sb, '}');
}

bool rule_action_is_radio(const rule_action_t *a)
{
    return strcmp(a->type, "device") == 0 || strcmp(a->type, "dynamic") == 0 ||
           strcmp(a->type, "onoff") == 0;
}

// ----------------------------------------------------------------------------
// Temps
// ----------------------------------------------------------------------------

int64_t rules_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

bool rules_local_time(struct tm *out)
{
    return ntp_sync_get_local_time(out) == ESP_OK;
}

void rules_formatted_date(char *out, size_t len)
{
    struct tm t;
    if (!rules_local_time(&t)) {
        jcopy(out, len, "");
        return;
    }
    snprintf(out, len, "%02d/%02d/%04d %02d:%02d", t.tm_mday % 100, (t.tm_mon + 1) % 100,
             (t.tm_year + 1900) % 10000, t.tm_hour % 100, t.tm_min % 100);
}

// ----------------------------------------------------------------------------
// Appareils
// ----------------------------------------------------------------------------

uint64_t rules_parse_ieee(const char *s)
{
    if (!s || !s[0]) return 0;
    return strtoull(s, NULL, 16);
}

const devdata_template_state_t *rules_tpl_state(uint64_t ieee, int cluster, int attr)
{
    const devdata_template_t *t = device_data_get_template(ieee);
    if (!t || !t->loaded) return NULL;
    // Cle de rangement : attribut, ou cle composee d'une ligne propre a un
    // endpoint (devdata_ep_key, convertie depuis "attribut + endpoint x 65536"
    // de la LiXee-Box au chargement des regles)
    uint16_t a = (uint16_t)(attr & 0xFFFF);
    for (int i = 0; i < t->state_count; i++) {
        if (t->states[i].cluster == (uint16_t)cluster && t->states[i].attribute == a) return &t->states[i];
    }
    return NULL;
}

static bool usable(const char *s)
{
    return s && s[0] && strcmp(s, "null") != 0;
}

void rules_device_label(const char *ieee_str, char *out, size_t len)
{
    uint64_t ieee = rules_parse_ieee(ieee_str);
    const zigate_device_t *z = ieee ? zigate_devices_find_by_ieee(ieee) : NULL;
    const device_data_t *dd = ieee ? device_data_find(ieee) : NULL;
    if (z && usable(z->alias)) {
        jcopy(out, len, z->alias);
    } else if (dd && usable(dd->info.alias)) {
        jcopy(out, len, dd->info.alias);
    } else if (z && usable(z->model)) {
        jcopy(out, len, z->model);
    } else {
        char tmp[12];
        jcopy(tmp, sizeof(tmp), ieee_str ? ieee_str : "?");
        tmp[10] = '\0';
        jcopy(out, len, tmp);
    }
}

void rules_hex_to_text(const char *hex, char *out, size_t len)
{
    size_t n = 0;
    if (!len) return;
    for (size_t i = 0; hex && hex[i] && hex[i + 1] && n + 1 < len; i += 2) {
        char hx[3] = { hex[i], hex[i + 1], 0 };
        char c = (char)strtol(hx, NULL, 16);
        if (c == '\0') break;
        out[n++] = (c >= 32 && c < 127) ? c : ' ';
    }
    out[n] = '\0';
    while (n > 0 && out[n - 1] == ' ') out[--n] = '\0';
}
