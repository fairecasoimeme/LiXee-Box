#ifndef ACTION_GROUPS_H
#define ACTION_GROUPS_H

#include <Arduino.h>
#include "config.h"
#include "rules.h"      // ActionRule, PsString, PsramAllocator

/* ===================== Groupes d'actions =====================
 *
 * Un groupe = un bouton qui declenche PLUSIEURS actions, sur des appareils DIFFERENTS.
 * Affiches en premiere ligne de la page Appareils, configures dans Config -> Groupes d'actions.
 *
 * Pourquoi un stockage SEPARE des regles, alors que la structure ActionRule est reutilisee ?
 * Une regle est evaluee automatiquement par applyRules(). Y loger un groupe manuel imposerait
 * un garde explicite « ne pas declencher si mode manuel » dans l'evaluateur : un seul oubli et
 * un groupe « Tout eteindre » se declencherait tout seul. Un fichier distinct rend cela
 * structurellement impossible -- aucun code n'itere les groupes en dehors d'un clic utilisateur.
 *
 * En revanche l'EXECUTEUR est partage : RulesManager::executeAction() (resolution du template,
 * endpoint, valeurs dynamiques, on/off) est reutilise tel quel. Le format JSON des actions est
 * donc identique a celui de rules.json, ce qui permet aussi de reutiliser l'editeur d'actions.
 */

#define MAX_ACTION_GROUPS      8    // au-dela, la barre de boutons devient illisible
#define MAX_ACTIONS_PER_GROUP  10

struct ActionGroup {
    PsString name;
    PsString icon;      // nom d'icone (jeu Bootstrap) ou emoji
    PsString color;     // couleur du bouton, ex "#0d6efd"
    bool     enabled;
    std::vector<ActionRule, PsramAllocator<ActionRule>> actions;

    ActionGroup()
        : name(PsramAllocator<char>()), icon(PsramAllocator<char>()),
          color(PsramAllocator<char>()), enabled(true) {}
};

class ActionGroupManager {
public:
    bool loadFromFile(const char* path = "/config/actiongroups.json");
    bool saveToFile(const char* path = "/config/actiongroups.json");

    size_t size() const { return groups_.size(); }
    const std::vector<ActionGroup, PsramAllocator<ActionGroup>>& getGroups() const { return groups_; }
    const ActionGroup* get(size_t idx) const { return idx < groups_.size() ? &groups_[idx] : nullptr; }
    ActionGroup*       get(size_t idx)       { return idx < groups_.size() ? &groups_[idx] : nullptr; }

    // Declenche toutes les actions du groupe. Renvoie le nombre d'actions emises, -1 si l'index
    // est invalide ou le groupe desactive. Les commandes partent par la file Zigbee, deja
    // cadencee a 100 ms : aucune rafale a redouter meme avec 10 actions.
    int run(size_t idx);

    // Recherche par NOM. Une regle designe un groupe par son nom et non par son index :
    // supprimer ou reordonner un autre groupe decalerait l'index, et la regle declencherait
    // alors silencieusement le MAUVAIS groupe. Renvoie -1 si le nom est inconnu.
    int findByName(const char* name) const;

    // Declenche un groupe designe par son nom (utilise par les actions de regle de type
    // "actiongroup"). Memes retours que run().
    int runByName(const char* name);

    // Ajout / remplacement / suppression. `json` est un objet groupe (meme forme que le fichier).
    bool addFromJson(JsonObjectConst obj);
    bool replaceFromJson(size_t idx, JsonObjectConst obj);
    bool remove(size_t idx);

    // Serialise un groupe (pour l'IHM d'edition).
    void toJson(size_t idx, JsonObject out) const;

private:
    std::vector<ActionGroup, PsramAllocator<ActionGroup>> groups_;
    // Garde anti-recursion : un groupe declenche depuis une regle ne doit pas pouvoir en
    // declencher un autre (cf. run()).
    bool running_ = false;
    static void parseActions(JsonArrayConst arr, ActionGroup& g);
};

extern ActionGroupManager actionGroups;

#endif
