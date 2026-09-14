#include "actionGroups.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "log.h"
#include "actionPacer.h"

extern RulesManager rulesManager;

ActionGroupManager actionGroups;

// Lit un tableau d'actions au MEME format que rules.json (cf. RulesManager::loadFromFile).
// Garder ce format identique permet de reutiliser l'executeur et l'editeur d'actions.
void ActionGroupManager::parseActions(JsonArrayConst arr, ActionGroup& g) {
    g.actions.clear();
    for (JsonObjectConst a : arr) {
        if (g.actions.size() >= MAX_ACTIONS_PER_GROUP) break;
        ActionRule act;
        act.type       = PsString(a["type"]       | "", PsramAllocator<char>());
        act.IEEE       = PsString(a["IEEE"]       | "", PsramAllocator<char>());
        act.actionName = PsString(a["actionName"] | "", PsramAllocator<char>());
        act.endpoint   = a["endpoint"] | 1;
        act.command    = a.containsKey("command") ? (int)a["command"] : -1;
        act.value      = PsString(a["value"]   | "", PsramAllocator<char>());
        act.title      = PsString(a["title"]   | "", PsramAllocator<char>());
        act.message    = PsString(a["message"] | "", PsramAllocator<char>());
        act.sourceIEEE      = PsString(a["sourceIEEE"] | "", PsramAllocator<char>());
        act.sourceCluster   = a["sourceCluster"]   | 0;
        act.sourceAttribute = a["sourceAttribute"] | 0;
        act.coefficient     = a["coefficient"] | 1.0;
        act.offset          = a["offset"]      | 0.0;
        g.actions.push_back(std::move(act));
    }
}

bool ActionGroupManager::loadFromFile(const char* path) {
    groups_.clear();
    File f = LittleFS.open(path, FILE_READ);
    if (!f) return false;                       // absent au premier demarrage : normal

    SpiRamJsonDocument doc(16384);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        log_e("actiongroups.json illisible : %s", err.c_str());
        return false;
    }

    for (JsonObjectConst o : doc["groups"].as<JsonArrayConst>()) {
        if (groups_.size() >= MAX_ACTION_GROUPS) break;
        ActionGroup g;
        g.name    = PsString(o["name"]  | "", PsramAllocator<char>());
        g.icon    = PsString(o["icon"]  | "", PsramAllocator<char>());
        g.color   = PsString(o["color"] | "#0d6efd", PsramAllocator<char>());
        g.enabled = o["enabled"] | true;
        parseActions(o["actions"].as<JsonArrayConst>(), g);
        groups_.push_back(std::move(g));
    }
    return true;
}

bool ActionGroupManager::saveToFile(const char* path) {
    SpiRamJsonDocument doc(16384);
    JsonArray arr = doc.createNestedArray("groups");
    for (const auto& g : groups_) {
        JsonObject o = arr.createNestedObject();
        o["name"]    = g.name.c_str();
        o["icon"]    = g.icon.c_str();
        o["color"]   = g.color.c_str();
        o["enabled"] = g.enabled;
        JsonArray aa = o.createNestedArray("actions");
        for (const auto& a : g.actions) {
            JsonObject ao = aa.createNestedObject();
            ao["type"]       = a.type.c_str();
            ao["IEEE"]       = a.IEEE.c_str();
            ao["actionName"] = a.actionName.c_str();
            ao["endpoint"]   = a.endpoint;
            ao["command"]    = a.command;
            ao["value"]      = a.value.c_str();
            if (a.title.size())   ao["title"]   = a.title.c_str();
            if (a.message.size()) ao["message"] = a.message.c_str();
            if (a.sourceIEEE.size()) {
                ao["sourceIEEE"]      = a.sourceIEEE.c_str();
                ao["sourceCluster"]   = a.sourceCluster;
                ao["sourceAttribute"] = a.sourceAttribute;
                ao["coefficient"]     = a.coefficient;
                ao["offset"]          = a.offset;
            }
        }
    }

    // Ecriture atomique : un fichier tronque par une coupure de courant en pleine ecriture
    // ferait perdre TOUS les groupes. On ecrit a cote, puis on renomme.
    String tmp = String(path) + ".tmp";
    File f = LittleFS.open(tmp, FILE_WRITE);
    if (!f) { log_e("actiongroups : ouverture %s impossible", tmp.c_str()); return false; }
    if (serializeJson(doc, f) == 0) { f.close(); LittleFS.remove(tmp); return false; }
    f.close();
    LittleFS.remove(path);
    return LittleFS.rename(tmp, path);
}

int ActionGroupManager::findByName(const char* name) const {
    if (!name || !*name) return -1;
    for (size_t i = 0; i < groups_.size(); i++) {
        if (strcasecmp(groups_[i].name.c_str(), name) == 0) return (int)i;
    }
    return -1;
}

int ActionGroupManager::runByName(const char* name) {
    int idx = findByName(name);
    return (idx < 0) ? -1 : run((size_t)idx);
}

int ActionGroupManager::run(size_t idx) {
    ActionGroup* g = get(idx);
    if (!g || !g->enabled) return -1;

    // Garde anti-recursion. Une regle peut declencher un groupe (action de type
    // "actiongroup") ; si ce groupe contenait a son tour une action "actiongroup" -- ce que
    // l'editeur ne propose pas, mais qu'un fichier edite a la main pourrait contenir -- deux
    // groupes se renvoyant l'un a l'autre boucleraient jusqu'au debordement de pile.
    // L'imbrication est donc refusee, et signalee plutot que silencieusement ignoree.
    if (running_) {
        log_w("Groupe d'actions : imbrication refusee ('%s')", g->name.c_str());
        return -1;
    }
    running_ = true;
    int sent = 0;
    // Actions radio : en file cadencee (chacune attend l'accuse de la precedente, cf.
    // actionPacer.h). Les autres (notification) : tout de suite.
    String origin = String("groupe \"") + g->name.c_str() + "\"";
    for (const auto& act : g->actions) {
        if (ActionPacer::isRadioAction(act)) {
            if (actionPacer.enqueue(act, origin.c_str())) sent++;
        } else {
            rulesManager.runAction(act, g->name.c_str());
            sent++;
        }
    }
    running_ = false;
    log_i("Groupe d'actions '%s' declenche : %d action(s)", g->name.c_str(), sent);
    return sent;
}

bool ActionGroupManager::addFromJson(JsonObjectConst obj) {
    if (groups_.size() >= MAX_ACTION_GROUPS) return false;
    ActionGroup g;
    g.name    = PsString(obj["name"]  | "", PsramAllocator<char>());
    g.icon    = PsString(obj["icon"]  | "", PsramAllocator<char>());
    g.color   = PsString(obj["color"] | "#0d6efd", PsramAllocator<char>());
    g.enabled = obj["enabled"] | true;
    if (g.name.size() == 0) return false;             // un bouton sans nom est inutilisable
    parseActions(obj["actions"].as<JsonArrayConst>(), g);
    groups_.push_back(std::move(g));
    return saveToFile();
}

bool ActionGroupManager::replaceFromJson(size_t idx, JsonObjectConst obj) {
    if (idx >= groups_.size()) return false;
    ActionGroup g;
    g.name    = PsString(obj["name"]  | "", PsramAllocator<char>());
    g.icon    = PsString(obj["icon"]  | "", PsramAllocator<char>());
    g.color   = PsString(obj["color"] | "#0d6efd", PsramAllocator<char>());
    g.enabled = obj["enabled"] | true;
    if (g.name.size() == 0) return false;
    parseActions(obj["actions"].as<JsonArrayConst>(), g);
    groups_[idx] = std::move(g);
    return saveToFile();
}

bool ActionGroupManager::remove(size_t idx) {
    if (idx >= groups_.size()) return false;
    groups_.erase(groups_.begin() + idx);
    return saveToFile();
}

void ActionGroupManager::toJson(size_t idx, JsonObject out) const {
    const ActionGroup* g = get(idx);
    if (!g) return;
    out["name"]    = g->name.c_str();
    out["icon"]    = g->icon.c_str();
    out["color"]   = g->color.c_str();
    out["enabled"] = g->enabled;
    JsonArray aa = out.createNestedArray("actions");
    for (const auto& a : g->actions) {
        JsonObject ao = aa.createNestedObject();
        ao["type"]       = a.type.c_str();
        ao["IEEE"]       = a.IEEE.c_str();
        ao["actionName"] = a.actionName.c_str();
        ao["endpoint"]   = a.endpoint;
        ao["command"]    = a.command;
        ao["value"]      = a.value.c_str();
        ao["title"]      = a.title.c_str();
        ao["message"]    = a.message.c_str();
    }
}
