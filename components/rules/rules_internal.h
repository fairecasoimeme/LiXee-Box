#pragma once

// ============================================================================
// Declarations internes au composant rules (partagees par rules.cpp,
// rules_exec.cpp, rules_json.cpp et action_groups.cpp). Pas d'API publique ici.
// ============================================================================

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "cJSON.h"
#include "device_data.h"
#include "rules.h"

#define RULES_IEEE_LEN     20
#define RULES_TYPE_LEN     16
#define RULES_ACTNAME_LEN  52     // >= DEVDATA_NAME_LEN (nom d'action de template ou de groupe)
#define RULES_TITLE_LEN    96
#define RULES_MSG_LEN      256    // message saisi (avant substitution des variables)
#define RULES_NOTIF_LEN    768    // message final (variables + texte des conditions)
#define RULES_ORIGIN_LEN   64

// Cadencement des actions radio (cf. actionPacer.cpp de la LiXee-Box)
#define RULES_PACE_NO_ACK_MS   400    // ecart fixe quand les retours ZiGate ne sont pas cables
#define RULES_ACK_WAIT_MS     1000    // attente maximale de l'accuse d'une action
#define RULES_ROUTE_WAIT_MS   4000    // apres un D4 (recherche de route en cours)
#define RULES_BUSY_WAIT_MS    3000    // apres un refus "ZiGate saturee"
#define RULES_MAX_BUSY_RETRY     5
#define RULES_JOB_MAX           32    // file des actions radio
#define RULES_JOB_MAX_AGE_MS 60000    // une action plus ancienne n'a plus de sens

// ----------------------------------------------------------------------------
// Action (meme structure pour les regles et les groupes d'actions)
// ----------------------------------------------------------------------------
typedef struct {
    char    type[RULES_TYPE_LEN];         // device, dynamic, onoff, notification, actiongroup
    char    IEEE[RULES_IEEE_LEN];
    char    actionName[RULES_ACTNAME_LEN];
    int32_t endpoint;
    int32_t command;                      // -1 : commande du template
    char    value[32];
    char    title[RULES_TITLE_LEN];
    char    message[RULES_MSG_LEN];
    char    sourceIEEE[RULES_IEEE_LEN];   // dynamic : appareil source
    int32_t sourceCluster;
    int32_t sourceAttribute;
    double  coefficient;
    double  offset;
} rule_action_t;

/** Style d'ecriture d'une action : celui de web.cpp (regles) ou d'actionGroups.cpp. */
typedef enum { ACT_STYLE_RULE, ACT_STYLE_GROUP_FILE, ACT_STYLE_GROUP_LIST } act_style_t;

void rule_action_from_json(const cJSON *a, rule_action_t *act);
/** Meme chose depuis le texte JSON d'une action (un seul petit arbre cJSON). */
bool rule_action_from_text(const char *text, size_t len, rule_action_t *act);

/** Vrai pour les actions qui emettent une commande Zigbee (file cadencee). */
bool rule_action_is_radio(const rule_action_t *a);

// ----------------------------------------------------------------------------
// Constructeur de chaine JSON en PSRAM (evite cJSON pour les sorties : ses noeuds
// iraient en RAM interne, rare)
// ----------------------------------------------------------------------------
typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    bool   oom;
} sb_t;

void  sb_init(sb_t *sb, size_t initial);
void  sb_free(sb_t *sb);
void  sb_putc(sb_t *sb, char c);
void  sb_puts(sb_t *sb, const char *s);
void  sb_printf(sb_t *sb, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void  sb_json_str(sb_t *sb, const char *s);                 // "texte echappe"
void  sb_kstr(sb_t *sb, const char *key, const char *val);  // "key":"val",
void  sb_kint(sb_t *sb, const char *key, long long v);      // "key":v,
void  sb_knum(sb_t *sb, const char *key, double v);         // "key":v, (%g)
void  sb_kbool(sb_t *sb, const char *key, bool v);
void  sb_key(sb_t *sb, const char *key);                    // "key":
/** Ferme un objet/tableau : remplace la virgule finale par c, sinon ajoute c. */
void  sb_close(sb_t *sb, char c);
/** Rend le tampon (a liberer par free), NULL si une allocation a echoue. */
char *sb_take(sb_t *sb);

void rule_action_to_json(sb_t *sb, const rule_action_t *a, act_style_t style);

// ----------------------------------------------------------------------------
// Lecture JSON
// ----------------------------------------------------------------------------

/**
 * Parcourt les objets du tableau "key" de l'objet racine de text, SANS construire
 * l'arbre complet : chaque element est passe a cb (debut, longueur) pour etre
 * analyse seul. Un rules.json de plusieurs dizaines de Ko donnerait sinon des
 * milliers de noeuds cJSON en RAM interne (allocations < 4 Ko).
 * Renvoie false si le texte n'est pas un objet JSON bien forme. Cle absente : true,
 * aucun element.
 */
bool json_foreach_array_object(const char *text, size_t len, const char *key,
                               bool (*cb)(const char *obj, size_t obj_len, void *ctx), void *ctx);

/** Membres de premier niveau d'un objet : cle (sans guillemets) et texte brut de la valeur. */
bool json_foreach_member(const char *text, size_t len,
                         bool (*cb)(const char *key, size_t klen, const char *val, size_t vlen, void *ctx),
                         void *ctx);

/** Elements d'un tableau (texte brut de chacun) ; null = tableau vide. */
bool json_foreach_element(const char *text, size_t len,
                          bool (*cb)(const char *elem, size_t elen, void *ctx), void *ctx);

bool json_key_is(const char *key, size_t klen, const char *want);

const char *jitem_str(const cJSON *v, const char *def);
long long   jitem_int(const cJSON *v, long long def);
double      jitem_num(const cJSON *v, double def);
bool        jitem_bool(const cJSON *v, bool def);
/** Chaine, ou nombre mis en texte, sinon "". */
void        jitem_text(const cJSON *v, char *out, size_t len);

const char *jstr(const cJSON *o, const char *key, const char *def);
long long   jint(const cJSON *o, const char *key, long long def);
double      jnum(const cJSON *o, const char *key, double def);
bool        jbool(const cJSON *o, const char *key, bool def);
bool        jhas(const cJSON *o, const char *key);
/** Copie bornee + terminee. */
void        jcopy(char *dst, size_t dstlen, const char *src);

// ----------------------------------------------------------------------------
// Memoire et fichiers
// ----------------------------------------------------------------------------
void *rules_psram_calloc(size_t n, size_t size);
void *rules_psram_malloc(size_t size);
void *rules_psram_realloc(void *p, size_t size);
/** Lit un fichier entier en PSRAM (termine par '\0'), NULL si absent. */
char *rules_read_file(const char *path, size_t *out_len);

// ----------------------------------------------------------------------------
// Temps
// ----------------------------------------------------------------------------
int64_t rules_now_ms(void);
/** Heure locale ; false si l'horloge n'est pas encore reglee (NTP). */
bool rules_local_time(struct tm *out);
/** "JJ/MM/AAAA HH:MM" (format FormattedDate de la LiXee-Box), "" si heure inconnue. */
void rules_formatted_date(char *out, size_t len);

// ----------------------------------------------------------------------------
// Appareils (device_data + zigate_devices)
// ----------------------------------------------------------------------------
uint64_t rules_parse_ieee(const char *s);
/** Etat de template (cluster, attribut) d'un appareil, NULL si absent. */
const devdata_template_state_t *rules_tpl_state(uint64_t ieee, int cluster, int attr);
/** Libelle : alias, sinon modele, sinon les 10 premiers caracteres de l'IEEE. */
void rules_device_label(const char *ieee_str, char *out, size_t len);
/** Decode une valeur hexadecimale d'attribut chaine en texte (espaces de fin retires). */
void rules_hex_to_text(const char *hex, char *out, size_t len);

// ----------------------------------------------------------------------------
// Execution (rules_exec.cpp)
// ----------------------------------------------------------------------------

/** Demarre la tache "rules" et les files. */
esp_err_t rules_exec_start(void);

/** Met une action radio en file cadencee. false : doublon deja en attente, ou file pleine. */
bool rules_job_enqueue(const rule_action_t *act, const char *origin);

/** Poste une notification (envoyee par la tache "rules", hors de tout verrou). */
void rules_notify_post(const char *title, const char *msg);

/** Reveille la tache "rules". */
void rules_wake_task(void);

/** Evenements consommes par la tache "rules" (rules.cpp). */
typedef enum { RULES_EVT_TICK = 1, RULES_EVT_ATTR = 2 } rules_evt_kind_t;
typedef struct {
    uint8_t  kind;
    uint16_t cluster;
    uint16_t attr;
    uint64_t ieee;
} rules_evt_t;

bool rules_post_event(const rules_evt_t *e);

/** Appele par la tache "rules" pour chaque evenement (rules.cpp). */
void rules_process_event(const rules_evt_t *e);
