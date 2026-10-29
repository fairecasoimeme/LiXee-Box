// ============================================================================
// Groupes d'actions - portage de ActionGroupManager (actionGroups.cpp)
//
// Format de /littlefs/config/actiongroups.json identique a la LiXee-Box :
//   {"groups":[{"name","icon","color","enabled","actions":[{type,IEEE,actionName,
//     endpoint,command,value,(title),(message),(sourceIEEE...)}]}]}
// Les actions ont le format des regles et passent par le meme executeur et la meme
// file cadencee (rules_job_enqueue).
// ============================================================================

#include "action_groups.h"
#include "rules_internal.h"
#include "config_manager.h"
#include "zigate_devices.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_random.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "ACT_GROUPS";

typedef struct {
    char          name[RULES_NAME_LEN];
    char          icon[48];          // nom d'icone MDI (agicons.js) ou emoji d'un ancien groupe
    char          color[16];
    bool          enabled;
    rule_action_t actions[ACTION_GROUPS_MAX_ACTIONS];
    int           n;
    bool          matter;            // publie dans Matter (interrupteur virtuel)
    char          matter_off[RULES_NAME_LEN];   // groupe lance a l'extinction, vide : impulsion
    uint32_t      matter_uid;        // identifiant stable de l'appareil Matter
    int           matter_ep;         // endpoint memorise
} ag_t;

static action_groups_change_cb_t s_change_cb;
void action_groups_set_change_cb(action_groups_change_cb_t cb) { s_change_cb = cb; }
static void notify_change(void) { if (s_change_cb) s_change_cb(); }

static SemaphoreHandle_t s_ag_lock;
static ag_t             *s_groups;       // ACTION_GROUPS_MAX entrees, en PSRAM
static int               s_ag_count;
static bool              s_running;      // garde anti-imbrication

static bool ag_init_storage(void)
{
    if (!s_ag_lock) s_ag_lock = xSemaphoreCreateMutex();
    if (!s_groups) s_groups = (ag_t *)rules_psram_calloc(ACTION_GROUPS_MAX, sizeof(ag_t));
    return s_ag_lock && s_groups;
}

static bool ag_lock(void)
{
    return s_ag_lock && xSemaphoreTake(s_ag_lock, pdMS_TO_TICKS(3000)) == pdTRUE;
}

static void ag_unlock(void)
{
    xSemaphoreGive(s_ag_lock);
}

// ----------------------------------------------------------------------------
// Lecture d'un groupe (chaque action analysee seule)
// ----------------------------------------------------------------------------

typedef struct {
    ag_t *g;
    int   index;
    bool  ok;
} gp_ctx_t;

static bool gp_action(const char *e, size_t n, void *ctx)
{
    gp_ctx_t *c = (gp_ctx_t *)ctx;
    if (c->g->n >= ACTION_GROUPS_MAX_ACTIONS) return true;
    if (rule_action_from_text(e, n, &c->g->actions[c->g->n])) c->g->n++;
    else c->ok = false;
    return true;
}

static bool gp_member(const char *key, size_t klen, const char *val, size_t vlen, void *ctx)
{
    gp_ctx_t *c = (gp_ctx_t *)ctx;
    if (json_key_is(key, klen, "actions")) {
        if (!json_foreach_element(val, vlen, gp_action, c)) c->ok = false;
        return c->ok;
    }
    cJSON *v = cJSON_ParseWithLength(val, vlen);
    if (!v) {
        c->ok = false;
        return true;
    }
    if (json_key_is(key, klen, "name"))         jcopy(c->g->name, sizeof(c->g->name), jitem_str(v, ""));
    else if (json_key_is(key, klen, "icon"))    jcopy(c->g->icon, sizeof(c->g->icon), jitem_str(v, ""));
    else if (json_key_is(key, klen, "color"))   jcopy(c->g->color, sizeof(c->g->color), jitem_str(v, "#0d6efd"));
    else if (json_key_is(key, klen, "enabled")) c->g->enabled = jitem_bool(v, true);
    else if (json_key_is(key, klen, "index"))   c->index = (int)jitem_int(v, -1);
    else if (json_key_is(key, klen, "matter"))  c->g->matter = jitem_bool(v, false);
    else if (json_key_is(key, klen, "matterOff")) jcopy(c->g->matter_off, sizeof(c->g->matter_off), jitem_str(v, ""));
    else if (json_key_is(key, klen, "matterUid")) c->g->matter_uid = (uint32_t)jitem_int(v, 0);
    else if (json_key_is(key, klen, "matterEp"))  c->g->matter_ep = (int)jitem_int(v, 0);
    cJSON_Delete(v);
    return true;
}

static bool parse_group(const char *text, size_t len, ag_t *g, int *index)
{
    memset(g, 0, sizeof(*g));
    jcopy(g->color, sizeof(g->color), "#0d6efd");
    g->enabled = true;
    gp_ctx_t c = { g, -1, true };
    bool ok = json_foreach_member(text, len, gp_member, &c) && c.ok;
    if (index) *index = c.index;
    return ok;
}

typedef struct {
    ag_t *arr;
    int   count;
} gl_ctx_t;

static bool gl_group(const char *e, size_t n, void *ctx)
{
    gl_ctx_t *c = (gl_ctx_t *)ctx;
    if (c->count >= ACTION_GROUPS_MAX) return true;
    if (parse_group(e, n, &c->arr[c->count], NULL)) c->count++;
    return true;
}

esp_err_t action_groups_load(void)
{
    if (!ag_init_storage()) return ESP_ERR_NO_MEM;
    size_t len = 0;
    char *text = rules_read_file(ACTION_GROUPS_PATH, &len);
    if (!text) return ESP_OK;                 // absent au premier demarrage : normal
    ag_t *tmp = (ag_t *)rules_psram_calloc(ACTION_GROUPS_MAX, sizeof(ag_t));
    if (!tmp) {
        free(text);
        return ESP_ERR_NO_MEM;
    }
    gl_ctx_t c = { tmp, 0 };
    bool ok = json_foreach_array_object(text, len, "groups", gl_group, &c);
    free(text);
    if (!ok) {
        ESP_LOGE(TAG, "actiongroups.json illisible");
        free(tmp);
        return ESP_FAIL;
    }
    if (ag_lock()) {
        memcpy(s_groups, tmp, sizeof(ag_t) * ACTION_GROUPS_MAX);
        s_ag_count = c.count;
        ag_unlock();
    }
    free(tmp);
    ESP_LOGI(TAG, "%d groupe(s) d'actions charge(s)", c.count);
    notify_change();
    return ESP_OK;
}

// ----------------------------------------------------------------------------
// Ecriture
// ----------------------------------------------------------------------------

static void ser_group(sb_t *sb, const ag_t *g, int index, act_style_t style)
{
    sb_putc(sb, '{');
    if (index >= 0) sb_kint(sb, "index", index);
    sb_kstr(sb, "name", g->name);
    sb_kstr(sb, "icon", g->icon);
    sb_kstr(sb, "color", g->color);
    sb_kbool(sb, "enabled", g->enabled);
    sb_kbool(sb, "matter", g->matter);
    sb_kstr(sb, "matterOff", g->matter_off);
    if (g->matter_uid) sb_kint(sb, "matterUid", g->matter_uid);
    if (g->matter_ep) sb_kint(sb, "matterEp", g->matter_ep);
    sb_key(sb, "actions");
    sb_putc(sb, '[');
    for (int i = 0; i < g->n; i++) {
        rule_action_to_json(sb, &g->actions[i], style);
        sb_putc(sb, ',');
    }
    sb_close(sb, ']');
    sb_close(sb, '}');
}

/** Enregistre le fichier (appele sans le verrou). */
static rules_res_t ag_save_file(void)
{
    if (!ag_lock()) return RULES_RES_NO_MEM;
    sb_t sb;
    sb_init(&sb, 2048);
    sb_puts(&sb, "{\"groups\":[");
    for (int i = 0; i < s_ag_count; i++) {
        ser_group(&sb, &s_groups[i], -1, ACT_STYLE_GROUP_FILE);
        sb_putc(&sb, ',');
    }
    sb_close(&sb, ']');
    sb_putc(&sb, '}');
    ag_unlock();
    char *txt = sb_take(&sb);
    if (!txt) return RULES_RES_NO_MEM;
    // Ecriture atomique (fichier temporaire + renommage) : un fichier tronque
    // par une coupure ferait perdre tous les groupes.
    esp_err_t e = cfg_write_file(ACTION_GROUPS_PATH, txt);
    free(txt);
    return e == ESP_OK ? RULES_RES_OK : RULES_RES_IO;
}

// ----------------------------------------------------------------------------
// API
// ----------------------------------------------------------------------------

int action_groups_count(void)
{
    return s_ag_count;
}

/** Variables d'une notification de groupe : {rule} (origine), {date}, {time}. */
static void ag_subst(const char *in, const char *origin, char *out, size_t cap)
{
    size_t o = 0;
    for (const char *p = in; *p && o + 1 < cap;) {
        const char *rep = NULL;
        size_t skip = 0;
        char tmp[24];
        if (!strncmp(p, "{rule}", 6)) {
            rep = origin;
            skip = 6;
        } else if (!strncmp(p, "{date}", 6)) {
            rules_formatted_date(tmp, sizeof(tmp));
            rep = tmp;
            skip = 6;
        } else if (!strncmp(p, "{time}", 6)) {
            rules_formatted_date(tmp, sizeof(tmp));
            rep = strlen(tmp) >= 16 ? tmp + 11 : tmp;   // "HH:MM" de "JJ/MM/AAAA HH:MM"
            skip = 6;
        }
        if (rep) {
            size_t l = strlen(rep);
            if (o + l + 1 >= cap) l = cap - o - 1;
            memcpy(out + o, rep, l);
            o += l;
            p += skip;
            continue;
        }
        out[o++] = *p++;
    }
    out[o] = '\0';
}

static int run_locked(int idx)
{
    if (idx < 0 || idx >= s_ag_count) return -1;
    ag_t *g = &s_groups[idx];
    if (!g->enabled) return -1;
    // Garde anti-imbrication : un groupe appele depuis une regle ne declenche pas
    // d'autre groupe (deux groupes se renvoyant l'un a l'autre boucleraient).
    if (s_running) {
        ESP_LOGW(TAG, "Groupe d'actions : imbrication refusee ('%s')", g->name);
        return -1;
    }
    s_running = true;
    char origin[RULES_ORIGIN_LEN];
    snprintf(origin, sizeof(origin), "groupe \"%.48s\"", g->name);
    int sent = 0;
    for (int i = 0; i < g->n; i++) {
        const rule_action_t *a = &g->actions[i];
        if (rule_action_is_radio(a)) {
            if (rules_job_enqueue(a, origin)) sent++;
        } else if (!strcmp(a->type, "notification")) {
            char title[RULES_TITLE_LEN + 64];
            char *msg = (char *)rules_psram_malloc(RULES_NOTIF_LEN);
            ag_subst(a->title, origin, title, sizeof(title));
            if (msg) {
                ag_subst(a->message, origin, msg, RULES_NOTIF_LEN);
                rules_notify_post(title, msg);
                free(msg);
            }
            sent++;
        } else if (!strcmp(a->type, "actiongroup")) {
            ESP_LOGW(TAG, "Groupe '%s' : action \"actiongroup\" imbriquee ignoree", g->name);
        } else {
            ESP_LOGW(TAG, "Groupe '%s' : action de type inconnu '%s'", g->name, a->type);
        }
    }
    s_running = false;
    ESP_LOGI(TAG, "Groupe d'actions '%s' declenche : %d action(s)", g->name, sent);
    return sent;
}

int action_groups_run(int idx)
{
    if (!ag_lock()) return -1;
    int r = run_locked(idx);
    ag_unlock();
    return r;
}

int action_groups_run_by_name(const char *name)
{
    if (!name || !name[0] || !ag_lock()) return -1;
    int idx = -1;
    for (int i = 0; i < s_ag_count; i++) {
        if (strcasecmp(s_groups[i].name, name) == 0) {
            idx = i;
            break;
        }
    }
    int r = run_locked(idx);
    ag_unlock();
    return r;
}

char *action_groups_list_json(void)
{
    if (!ag_lock()) return NULL;
    sb_t sb;
    sb_init(&sb, 2048);
    sb_puts(&sb, "{\"groups\":[");
    for (int i = 0; i < s_ag_count; i++) {
        ser_group(&sb, &s_groups[i], i, ACT_STYLE_GROUP_LIST);
        sb_putc(&sb, ',');
    }
    sb_close(&sb, ']');
    sb_putc(&sb, ',');
    sb_kint(&sb, "max", ACTION_GROUPS_MAX);
    sb_kint(&sb, "maxActions", ACTION_GROUPS_MAX_ACTIONS);
    sb_close(&sb, '}');
    ag_unlock();
    return sb_take(&sb);
}

char *action_groups_devices_json(void)
{
    sb_t sb;
    sb_init(&sb, 4096);
    sb_putc(&sb, '[');
    int n = zigate_devices_count();
    for (int i = 0; i < n; i++) {
        const zigate_device_t *z = zigate_devices_get(i);
        if (!z || !z->ieee_addr) continue;
        const devdata_template_t *t = device_data_get_template(z->ieee_addr);
        if (!t || !t->loaded || t->action_count == 0) continue;
        char ieee[20], label[48];
        snprintf(ieee, sizeof(ieee), "%016llX", (unsigned long long)z->ieee_addr);
        if (z->alias[0]) jcopy(label, sizeof(label), z->alias);
        else rules_device_label(ieee, label, sizeof(label));
        sb_putc(&sb, '{');
        sb_kstr(&sb, "i", ieee);
        sb_kstr(&sb, "n", label);
        sb_key(&sb, "a");
        sb_putc(&sb, '[');
        for (int k = 0; k < t->action_count; k++) {
            const devdata_template_action_t *a = &t->actions[k];
            // Positionnement de volet : curseur, aucune position fixe a proposer
            if (!a->visible || a->command == 251 || a->command == 252) continue;
            sb_putc(&sb, '{');
            sb_kstr(&sb, "n", a->name);
            sb_kint(&sb, "e", a->endpoint);
            sb_close(&sb, '}');
            sb_putc(&sb, ',');
        }
        sb_close(&sb, ']');
        sb_close(&sb, '}');
        sb_putc(&sb, ',');
    }
    sb_close(&sb, ']');
    return sb_take(&sb);
}

/** Couleur de bouton : #rgb a #rrggbbaa, sinon couleur par defaut (inseree dans un style CSS). */
static void sanitize_color(char *c, size_t len)
{
    size_t l = strlen(c);
    bool ok = l >= 4 && l <= 9 && c[0] == '#';
    for (size_t i = 1; ok && i < l; i++) ok = isxdigit((unsigned char)c[i]) != 0;
    if (!ok) jcopy(c, len, "#0d6efd");
}

rules_res_t action_groups_save_json(const char *body, size_t len, char *err, size_t errlen)
{
    if (!ag_init_storage()) return RULES_RES_NO_MEM;
    ag_t *g = (ag_t *)rules_psram_calloc(1, sizeof(ag_t));
    if (!g) return RULES_RES_NO_MEM;
    int idx = -1;
    if (!parse_group(body, len, g, &idx)) {
        free(g);
        jcopy(err, errlen, "JSON invalide");
        return RULES_RES_BAD_REQUEST;
    }
    if (!g->name[0]) {
        free(g);
        jcopy(err, errlen, "nom vide");   // un bouton sans nom est inutilisable
        return RULES_RES_BAD_REQUEST;
    }
    sanitize_color(g->color, sizeof(g->color));
    rules_res_t res = RULES_RES_OK;
    if (!ag_lock()) {
        free(g);
        return RULES_RES_NO_MEM;
    }
    if (idx >= 0 && idx < s_ag_count) {
        // Identite Matter conservee : l'appareil reste le meme pour les controleurs
        if (!g->matter_uid) g->matter_uid = s_groups[idx].matter_uid;
        if (!g->matter_ep) g->matter_ep = s_groups[idx].matter_ep;
    }
    if (g->matter && !g->matter_uid) g->matter_uid = (esp_random() & 0x7FFFFFFF) | 1;
    if (!strcasecmp(g->matter_off, g->name)) g->matter_off[0] = '\0';   // pas de boucle sur lui-meme
    if (idx >= 0) {
        if (idx >= s_ag_count) {
            jcopy(err, errlen, "index invalide");
            res = RULES_RES_NOT_FOUND;
        } else {
            s_groups[idx] = *g;
        }
    } else if (s_ag_count >= ACTION_GROUPS_MAX) {
        snprintf(err, errlen, "limite atteinte (%d groupes)", ACTION_GROUPS_MAX);
        res = RULES_RES_FULL;
    } else {
        s_groups[s_ag_count++] = *g;
    }
    ag_unlock();
    free(g);
    if (res == RULES_RES_OK) {
        res = ag_save_file();
        if (res != RULES_RES_OK) jcopy(err, errlen, "ecriture impossible");
        notify_change();
    }
    return res;
}

rules_res_t action_groups_remove(int idx)
{
    if (!ag_lock()) return RULES_RES_NO_MEM;
    if (idx < 0 || idx >= s_ag_count) {
        ag_unlock();
        return RULES_RES_NOT_FOUND;
    }
    for (int i = idx; i < s_ag_count - 1; i++) s_groups[i] = s_groups[i + 1];
    s_ag_count--;
    memset(&s_groups[s_ag_count], 0, sizeof(ag_t));
    ag_unlock();
    rules_res_t r = ag_save_file();
    notify_change();
    return r;
}

// ----------------------------------------------------------------------------
// Groupes publies dans Matter
// ----------------------------------------------------------------------------

int action_groups_matter_list(action_group_matter_t *out, int max)
{
    if (!out || !ag_lock()) return 0;
    int n = 0;
    for (int i = 0; i < s_ag_count && n < max; i++) {
        const ag_t *g = &s_groups[i];
        if (!g->matter || !g->matter_uid) continue;
        out[n].uid = g->matter_uid;
        jcopy(out[n].name, sizeof(out[n].name), g->name);
        out[n].ep = g->matter_ep;
        out[n].stateful = g->matter_off[0] != '\0';
        n++;
    }
    ag_unlock();
    return n;
}

void action_groups_set_matter_ep(uint32_t uid, int ep)
{
    if (!uid || !ag_lock()) return;
    bool changed = false;
    for (int i = 0; i < s_ag_count; i++) {
        if (s_groups[i].matter_uid == uid && s_groups[i].matter_ep != ep) {
            s_groups[i].matter_ep = ep;
            changed = true;
        }
    }
    ag_unlock();
    if (changed) ag_save_file();
}

int action_groups_run_matter(uint32_t uid, bool on)
{
    if (!uid || !ag_lock()) return -1;
    int idx = -1;
    char off[RULES_NAME_LEN] = "";
    for (int i = 0; i < s_ag_count; i++) {
        if (s_groups[i].matter_uid == uid) {
            idx = i;
            jcopy(off, sizeof(off), s_groups[i].matter_off);
            break;
        }
    }
    if (idx >= 0 && !on) {
        // Extinction : groupe d'extinction designe par son nom
        int k = -1;
        for (int i = 0; off[0] && i < s_ag_count; i++) if (!strcasecmp(s_groups[i].name, off)) k = i;
        idx = k;
    }
    int r = idx >= 0 ? run_locked(idx) : -1;
    ag_unlock();
    return r;
}
