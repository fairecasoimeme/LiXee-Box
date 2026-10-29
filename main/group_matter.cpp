// ============================================================================
// Groupes d'actions publies dans Matter
//
// Un groupe marque "matter" devient un interrupteur Matter (prise On/Off
// virtuelle) :
//  - impulsion (pas de groupe d'extinction) : allumer lance le groupe, puis
//    l'interrupteur revient seul sur "eteint" apres 0,3 s (bouton poussoir) ;
//  - marche/arret : allumer lance le groupe, eteindre lance le groupe
//    d'extinction ; l'etat reste celui demande.
// Comme les thermostats virtuels : UniqueID stable, endpoint memorise,
// retirable / remettable depuis la page Matter (liste d'exclusion).
// Creation des endpoints et lancement des groupes dans une tache dediee
// (pile en RAM interne : ecriture du fichier des groupes).
// ============================================================================

#include "group_matter.h"
#include "action_groups.h"
#include "matter_bridge.h"
#include "matter_device_manager.h"
#include "matter_profile.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "GROUP_MATTER";

#define GM_UID_BASE   0x4C58414700000000ULL      // UniqueID Matter : "LXAG" + uid du groupe
#define GM_PULSE_MS   300       // assez pour que le controleur voie l'etat, sans paraitre allume

typedef struct {
    uint32_t uid;
    int      ep;
    bool     stateful;
    char     name[RULES_NAME_LEN];
} gm_entry_t;

typedef struct {
    uint8_t kind;          // 0 : synchronisation, 1 : ecriture
    int     ep;
    int32_t value;
} gm_msg_t;

static gm_entry_t     s_e[ACTION_GROUPS_MAX];   // endpoints crees
static int            s_n;
static volatile int   s_eps[ACTION_GROUPS_MAX]; // copie lue par la tache CHIP
static QueueHandle_t  s_q;

static void eps_rebuild(void)
{
    for (int i = 0; i < ACTION_GROUPS_MAX; i++) s_eps[i] = i < s_n ? s_e[i].ep : 0;
}

/** Ecriture d'un controleur (tache CHIP) : OnOff de l'un de nos endpoints. */
static esp_err_t write_cb(int ep, uint32_t cluster, uint32_t attr, int32_t value, bool is_null)
{
    bool ours = false;
    for (int i = 0; i < ACTION_GROUPS_MAX && !ours; i++) ours = (s_eps[i] == ep && ep > 1);
    if (!ours) return ESP_ERR_NOT_FOUND;
    if (cluster == 0x0006 && attr == 0x0000 && !is_null && s_q) {
        gm_msg_t m = { 1, ep, value };
        xQueueSend(s_q, &m, 0);
    }
    return ESP_OK;
}

static void sync(void)
{
    if (!matter_bridge_is_started()) return;
    action_group_matter_t *want = (action_group_matter_t *)calloc(ACTION_GROUPS_MAX, sizeof(*want));
    if (!want) return;
    int nw = action_groups_matter_list(want, ACTION_GROUPS_MAX);

    // 1. Endpoints dont le groupe a disparu, n'est plus publie, a ete retire
    //    de Matter ou renomme (libelle fixe a la creation : recree, meme endpoint)
    for (int i = 0; i < s_n;) {
        const action_group_matter_t *w = NULL;
        for (int k = 0; k < nw; k++) if (want[k].uid == s_e[i].uid) w = &want[k];
        bool keep = w && !matter_device_manager_is_excluded(GM_UID_BASE | s_e[i].uid) &&
                    !strcmp(w->name, s_e[i].name);
        if (keep) {
            s_e[i].stateful = w->stateful;
            i++;
            continue;
        }
        ESP_LOGI(TAG, "groupe \"%s\" : endpoint %d retire", s_e[i].name, s_e[i].ep);
        matter_bridge_remove_endpoint(s_e[i].ep);
        memmove(&s_e[i], &s_e[i + 1], sizeof(gm_entry_t) * (s_n - i - 1));
        s_n--;
    }

    // 2. Groupes a publier qui n'ont pas encore d'endpoint
    for (int k = 0; k < nw && s_n < ACTION_GROUPS_MAX; k++) {
        bool have = false;
        for (int i = 0; i < s_n; i++) if (s_e[i].uid == want[k].uid) have = true;
        if (have || matter_device_manager_is_excluded(GM_UID_BASE | want[k].uid)) continue;
        matter_profile_t *p = (matter_profile_t *)calloc(1, sizeof(matter_profile_t));
        if (!p) break;
        p->caps = MCAP_ONOFF;
        p->light = false;                          // prise On/Off : un interrupteur partout
        int ep = matter_bridge_add_virtual_endpoint(GM_UID_BASE | want[k].uid, p, want[k].name,
                                                    "Groupe d'actions", want[k].ep);
        free(p);
        if (ep <= 1) {
            ESP_LOGW(TAG, "groupe \"%s\" : endpoint Matter non cree", want[k].name);
            continue;
        }
        gm_entry_t *e = &s_e[s_n++];
        e->uid = want[k].uid;
        e->ep = ep;
        e->stateful = want[k].stateful;
        strlcpy(e->name, want[k].name, sizeof(e->name));
        matter_bridge_set_number(ep, 0x0006, 0x0000, 0, false);
        if (ep != want[k].ep) action_groups_set_matter_ep(want[k].uid, ep);
        ESP_LOGI(TAG, "groupe \"%s\" : interrupteur Matter sur l'endpoint %d (%s)", e->name, ep,
                 e->stateful ? "marche/arret" : "impulsion");
    }
    eps_rebuild();
    free(want);
}

static void apply_write(int ep, int32_t value)
{
    const gm_entry_t *e = NULL;
    for (int i = 0; i < s_n; i++) if (s_e[i].ep == ep) e = &s_e[i];
    if (!e) return;
    gm_entry_t c = *e;                             // copie : sync() peut deplacer le tableau
    if (value) {
        int n = action_groups_run_matter(c.uid, true);
        ESP_LOGI(TAG, "groupe \"%s\" lance par Matter (%d action(s))", c.name, n);
        if (!c.stateful) {
            vTaskDelay(pdMS_TO_TICKS(GM_PULSE_MS));
            matter_bridge_set_number(c.ep, 0x0006, 0x0000, 0, false);   // retour a "eteint"
        }
    } else if (c.stateful) {
        int n = action_groups_run_matter(c.uid, false);
        ESP_LOGI(TAG, "groupe \"%s\" : extinction par Matter (%d action(s))", c.name, n);
    }
}

static void task(void *arg)
{
    (void)arg;
    gm_msg_t m;
    for (;;) {
        if (xQueueReceive(s_q, &m, portMAX_DELAY) != pdTRUE) continue;
        if (m.kind == 0) sync();
        else apply_write(m.ep, m.value);
    }
}

void group_matter_refresh(void)
{
    if (!s_q) return;
    gm_msg_t m = { 0, 0, 0 };
    xQueueSend(s_q, &m, 0);
}

void group_matter_start(void)
{
    if (s_q) return;
    s_q = xQueueCreate(8, sizeof(gm_msg_t));
    if (!s_q || xTaskCreate(task, "group_matter", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Tache non creee (RAM interne insuffisante)");
        return;
    }
    matter_bridge_set_virtual_write_cb(write_cb);
    action_groups_set_change_cb(group_matter_refresh);
    group_matter_refresh();
}
