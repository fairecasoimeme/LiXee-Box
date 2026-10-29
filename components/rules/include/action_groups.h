#pragma once

// ============================================================================
// Groupes d'actions (portage de actionGroups.cpp de la LiXee-Box)
//
// Un groupe = un bouton qui declenche plusieurs actions, sur des appareils
// differents (barre de boutons de la page Appareils, Config -> Groupes d'actions).
// Stockage SEPARE des regles (/littlefs/config/actiongroups.json, format identique
// a la LiXee-Box) : aucun code n'itere les groupes en dehors d'un clic ou d'une
// action de regle "actiongroup", un groupe ne peut donc pas se declencher seul.
// L'executeur, lui, est celui des regles (meme format d'action, meme file cadencee).
// ============================================================================

#include <stdbool.h>
#include <stddef.h>
#include "rules.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ACTION_GROUPS_PATH        "/littlefs/config/actiongroups.json"
#define ACTION_GROUPS_MAX         8    // au-dela, la barre de boutons devient illisible
#define ACTION_GROUPS_MAX_ACTIONS 10

/** Charge actiongroups.json (appele par rules_init()). Absent : aucun groupe. */
esp_err_t action_groups_load(void);

int action_groups_count(void);

/**
 * Declenche le groupe d'index idx : actions radio mises dans la file cadencee des
 * regles, notifications postees. Renvoie le nombre d'actions lancees, -1 si
 * l'index est invalide ou le groupe desactive.
 */
int action_groups_run(int idx);

/** Meme chose par NOM (insensible a la casse) : utilise par les regles. */
int action_groups_run_by_name(const char *name);

/** {"groups":[{"index":i,"name","icon","color","enabled","actions":[...]}],"max":8} */
char *action_groups_list_json(void);

/**
 * Appareils actionnables pour l'editeur : [{"i":IEEE,"n":libelle,"a":[{"n":nom,"e":ep}]}]
 * (actions visibles du template, hors positionnement de volet).
 */
char *action_groups_devices_json(void);

/** Creation (index absent ou < 0) ou remplacement d'un groupe ; corps = objet groupe. */
rules_res_t action_groups_save_json(const char *body, size_t len, char *err, size_t errlen);

rules_res_t action_groups_remove(int idx);

// ----------------------------------------------------------------------------
// Groupes publies dans Matter (interrupteur virtuel). Champs du fichier, en
// plus du format de la LiXee-Box : "matter" (bool), "matterOff" (nom du groupe
// lance a l'extinction : interrupteur marche/arret ; vide : impulsion),
// "matterUid" (identifiant stable) et "matterEp" (endpoint memorise).
// ----------------------------------------------------------------------------

typedef struct {
    uint32_t uid;
    char     name[RULES_NAME_LEN];
    int      ep;                 // endpoint memorise, 0 si aucun
    bool     stateful;           // groupe d'extinction defini
} action_group_matter_t;

/** Groupes a publier dans Matter (au plus max). */
int action_groups_matter_list(action_group_matter_t *out, int max);

/** Memorise l'endpoint Matter d'un groupe (ecrit le fichier s'il change : pas de tache a pile PSRAM). */
void action_groups_set_matter_ep(uint32_t uid, int ep);

/** Interrupteur Matter du groupe : on -> le groupe, off -> son groupe d'extinction. */
int action_groups_run_matter(uint32_t uid, bool on);

/** Appele apres tout changement de la liste des groupes (chargement, enregistrement, suppression). */
typedef void (*action_groups_change_cb_t)(void);
void action_groups_set_change_cb(action_groups_change_cb_t cb);

#ifdef __cplusplus
}
#endif
