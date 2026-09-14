#include "actionPacer.h"
#include "protocol.h"   // GetShortAddr

extern RulesManager rulesManager;
extern CircularBuffer<Packet, 100> *commandList;
extern CircularBuffer<Packet, 70>  *PrioritycommandList;

ActionPacer actionPacer;

namespace {
// Attente de l'issue une fois la trame partie. Un appareil sain accuse en quelques dizaines
// de ms ; au-dela, on considere qu'il ne repondra pas a temps et on passe a la suite. La
// ZiGate, elle, continue ses propres tentatives : l'action n'est pas annulee pour autant.
// Quand aucun accuse ne revient (observe : ZiGate qui ne prend plus en compte les accuses
// APS), c'est ce delai qui cadence le groupe : chaque trame garde sa place dans la table
// d'accuses de la ZiGate jusqu'a son A7 (~4-5 s). A 1,5 s, la table saturait deja parfois
// (0x9999 statut 0x83) ; 1 s reste un compromis -- plus court, on retombe sur le "tout d'un
// coup". Avec une ZiGate saine, ce delai n'intervient pas.
const uint32_t ACK_WAIT_MS     = 1000;
// Apres un D4 (pas de route, trame gardee par la ZiGate) : duree laissee a la recherche de
// route. Une route qui existe se trouve en bien moins d'une seconde ; au-dela, elle echouera
// probablement, mais on ne lance pas la suivante pendant qu'elle occupe encore la radio.
const uint32_t ROUTE_WAIT_MS   = 4000;
// Attente du DEPART de la trame, quand la file Zigbee est chargee (sondages, lectures...).
const uint32_t SEND_WAIT_MS    = 3000;
// File Zigbee vide et rien n'est parti : l'action n'a rien emis (appareil introuvable,
// position de volet manquante...). Inutile d'attendre davantage.
const uint32_t NOTHING_SENT_MS = 300;
// Une action restee plus longtemps en file n'a plus de sens (elle arriverait a contretemps).
// Couvre un groupe de 10 actions dont les recherches de route echoueraient toutes.
const uint32_t MAX_AGE_MS      = 60000;
// Borne de la file : 8 groupes de 10 actions en rafale, ou des regles qui s'emballent.
const size_t   MAX_QUEUE       = 32;
// Refus "ZiGate saturee" : attente d'une place libre dans sa table d'accuses. On repart des
// qu'un accuse ou un echec (0x8011 / 0x8702) en libere une, sinon au bout de BUSY_WAIT_MS.
// Une place ne se libere parfois qu'au A7, plusieurs secondes apres l'envoi.
const uint32_t BUSY_WAIT_MS    = 3000;
const uint8_t  MAX_BUSY_RETRIES = 5;

// Statuts 0x8000 signifiant "pas de place pour l'instant" et non un echec de l'action :
// 0x04 ZiGate occupee, 0x14 plus de tampon ZCL, 0x15 emission impossible (table d'accuses
// pleine : precede de 0x9999 statut 0x83).
bool isBusyStatus(uint8_t s) {
    return s == 0x04 || s == 0x14 || s == 0x15;
}

bool zigbeeQueuesEmpty() {
    return (commandList == nullptr || commandList->isEmpty()) &&
           (PrioritycommandList == nullptr || PrioritycommandList->isEmpty());
}

// Libelle des statuts les plus frequents, pour des traces lisibles.
const char* statusText(uint8_t s) {
    switch (s) {
        case 0xA7: return "pas d'accuse de l'appareil";
        case 0xD0: return "recherche de route echouee";
        case 0xD1: return "erreur de route";
        case 0xE9: return "pas d'accuse radio du premier saut";
        case 0x15: return "ZiGate saturee, commande refusee";
        default:   return "echec";
    }
}
}

void ActionPacer::begin() {
    if (!mutex_) mutex_ = xSemaphoreCreateMutex();
}

bool ActionPacer::isRadioAction(const ActionRule& act) {
    return act.type == "device" || act.type == "dynamic" || act.type == "onoff";
}

bool ActionPacer::sameAction(const ActionRule& a, const ActionRule& b) {
    return a.type == b.type && a.IEEE == b.IEEE && a.actionName == b.actionName &&
           a.endpoint == b.endpoint && a.command == b.command && a.value == b.value &&
           a.sourceIEEE == b.sourceIEEE;
}

bool ActionPacer::enqueue(const ActionRule& act, const char* origin) {
    if (!mutex_) {
        // begin() non appele : on execute directement plutot que de perdre l'action.
        rulesManager.runQueuedAction(act, origin);
        return true;
    }
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.printf("[Cadence] file occupee, action ignoree : %s -> %s\n",
                      act.IEEE.c_str(), act.actionName.c_str());
        return false;
    }
    bool ok = true;
    for (const auto& q : queue_) {
        if (sameAction(q.act, act)) { ok = false; break; }
    }
    if (!ok) {
        xSemaphoreGive(mutex_);
        // Deja en attente : la seconde demande n'apporterait qu'un doublon.
        Serial.printf("[Cadence] action deja en file, doublon ignore : %s -> %s\n",
                      act.IEEE.c_str(), act.actionName.c_str());
        return false;
    }
    if (queue_.size() >= MAX_QUEUE) {
        xSemaphoreGive(mutex_);
        Serial.printf("[Cadence] file pleine (%u), action ignoree : %s -> %s\n",
                      (unsigned)MAX_QUEUE, act.IEEE.c_str(), act.actionName.c_str());
        return false;
    }
    queue_.emplace_back();
    PacedAction& p = queue_.back();
    p.act      = act;
    p.origin   = PsString(origin ? origin : "", PsramAllocator<char>());
    p.queuedAt = millis();
    xSemaphoreGive(mutex_);
    return true;
}

void ActionPacer::tick() {
    if (!mutex_) return;
    uint32_t now = millis();

    // --- 1. Action en cours : attendre son issue, ou le delai maximal.
    if (waiting_) {
        portENTER_CRITICAL(&mux_);
        bool     done = done_, sent = sent_, routing = routing_;
        uint32_t sentMs = sentMs_, routeMs = routeMs_;
        uint8_t  res = result_;
        uint16_t msg = resultMsg_, target = target_;
        portEXIT_CRITICAL(&mux_);

        if (done) {
            if (msg == 0x8000 && isBusyStatus(res) && hasCurrent_ &&
                current_.busyRetries < MAX_BUSY_RETRIES) {
                // ZiGate saturee : l'action n'est pas partie. On la remet en tete de file et on
                // attend qu'une place se libere.
                current_.busyRetries++;
                bool requeued = false;
                if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
                    queue_.push_front(current_);
                    requeued = true;
                    xSemaphoreGive(mutex_);
                }
                slotFreed_  = false;
                pauseUntil_ = now + BUSY_WAIT_MS;
                if (requeued) {
                    Serial.printf("[Cadence] %04X : ZiGate saturee (%02X), nouvel essai %u/%u des "
                                  "qu'une place se libere (%s)\n", target, res,
                                  (unsigned)current_.busyRetries, (unsigned)MAX_BUSY_RETRIES, label_);
                } else {
                    Serial.printf("[Cadence] %04X : ZiGate saturee (%02X), action perdue (%s)\n",
                                  target, res, label_);
                }
            } else if (res != 0x00) {
                Serial.printf("[Cadence] %04X : %s (%02X, 0x%04X), action suivante (%s)\n",
                              target, statusText(res), res, msg, label_);
            }
        } else if (routing) {
            // D4 : la ZiGate cherche une route. On attend son issue, sans lancer d'autre
            // recherche en parallele.
            if (now - routeMs < ROUTE_WAIT_MS) return;
            Serial.printf("[Cadence] %04X : toujours pas de route apres %lu ms, action suivante (%s)\n",
                          target, (unsigned long)ROUTE_WAIT_MS, label_);
        } else if (sent && now - sentMs >= ACK_WAIT_MS) {
            Serial.printf("[Cadence] %04X : pas d'accuse apres %lu ms, action suivante (%s)\n",
                          target, (unsigned long)ACK_WAIT_MS, label_);
        } else if (!sent && now - startMs_ >= SEND_WAIT_MS) {
            Serial.printf("[Cadence] %04X : trame toujours en file apres %lu ms, action suivante (%s)\n",
                          target, (unsigned long)SEND_WAIT_MS, label_);
        } else if (!sent && now - startMs_ >= NOTHING_SENT_MS && zigbeeQueuesEmpty()) {
            // L'action n'a rien emis : rien a attendre.
        } else {
            return;   // toujours en attente
        }
        portENTER_CRITICAL(&mux_);
        waiting_ = false;
        portEXIT_CRITICAL(&mux_);
    }

    // --- 2. Pause apres un refus "ZiGate saturee" : jusqu'a ce qu'une place se libere.
    if (pauseUntil_ != 0) {
        if (!slotFreed_ && (int32_t)(pauseUntil_ - now) > 0) return;
        pauseUntil_ = 0;
    }

    // --- 3. Action suivante. Pas d'attente du mutex : la boucle principale ne doit pas bloquer.
    PacedAction item;
    bool have = false;
    size_t left = 0;
    if (xSemaphoreTake(mutex_, 0) != pdTRUE) return;
    while (!queue_.empty() && now - queue_.front().queuedAt > MAX_AGE_MS) {
        Serial.printf("[Cadence] action abandonnee apres %lu s d'attente : %s -> %s\n",
                      (unsigned long)(MAX_AGE_MS / 1000), queue_.front().act.IEEE.c_str(),
                      queue_.front().act.actionName.c_str());
        queue_.pop_front();
    }
    if (!queue_.empty()) {
        item = std::move(queue_.front());
        queue_.pop_front();
        have = true;
        left = queue_.size();
    }
    xSemaphoreGive(mutex_);
    if (!have) return;

    // Adresse courte de la cible : c'est elle qu'on reconnaitra dans les trames de retour.
    int sa = GetShortAddr(String(item.act.IEEE.c_str()) + ".json");
    if (sa > 0 && sa < 0xFFF8) {
        strlcpy(label_, item.origin.c_str(), sizeof(label_));
        portENTER_CRITICAL(&mux_);
        target_  = (uint16_t)sa;
        sent_    = done_ = noAck_ = routing_ = false;
        sentCmd_ = 0;
        result_  = 0;
        resultMsg_ = 0;
        startMs_ = now;
        waiting_ = true;
        portEXIT_CRITICAL(&mux_);
    }
    if (left > 0) {
        Serial.printf("[Cadence] %u action(s) encore en file\n", (unsigned)left);
    }
    // Garde une copie : en cas de refus "ZiGate saturee", l'action sera remise en file.
    current_    = std::move(item);
    hasCurrent_ = true;
    rulesManager.runQueuedAction(current_.act, current_.origin.c_str());
}

void ActionPacer::onSent(const Packet& p) {
    if (!waiting_ || p.len < 3) return;
    // Commandes adressees de la ZiGate : <mode d'adresse><adresse courte u16>... 0x02 = courte
    // avec demande d'accuse, 0x07 = courte sans accuse.
    uint8_t mode = p.datas[0];
    if (mode != 0x02 && mode != 0x07) return;
    uint16_t addr = ((uint16_t)p.datas[1] << 8) | p.datas[2];
    portENTER_CRITICAL(&mux_);
    if (waiting_ && !sent_ && addr == target_) {
        sent_    = true;
        sentMs_  = millis();
        noAck_   = (mode == 0x07);
        sentCmd_ = p.cmd;
    }
    portEXIT_CRITICAL(&mux_);
}

void ActionPacer::onCommandStatus(uint8_t status, uint16_t packetType) {
    if (status == 0x00 || !waiting_) return;
    // La ZiGate a refuse la commande : aucun accuse ne viendra.
    portENTER_CRITICAL(&mux_);
    if (waiting_ && sent_ && !done_ && packetType == sentCmd_) {
        done_ = true;
        result_ = status;
        resultMsg_ = 0x8000;
    }
    portEXIT_CRITICAL(&mux_);
}

void ActionPacer::onRadioResult(uint16_t msgType, uint16_t sa, uint8_t status) {
    // Tout accuse ou echec, quel que soit l'appareil, libere une place dans la table d'accuses
    // de la ZiGate : c'est le signal de reprise apres un refus "ZiGate saturee".
    if (msgType == 0x8011 || msgType == 0x8702) slotFreed_ = true;
    if (!waiting_) return;
    portENTER_CRITICAL(&mux_);
    if (waiting_ && sent_ && !done_ && sa == target_) {
        // D4 (0x8702) : pas de route, trame gardee par la ZiGate le temps d'une recherche de
        // route -- ce n'est pas une issue, l'issue reelle viendra ensuite (0x8011 ou 0x8701).
        // 0x8012 ne dit que "trame remise au premier saut" : c'est l'issue finale seulement
        // pour une trame sans accuse, ou si cette remise meme a echoue.
        if (msgType == 0x8702 && status == 0xD4) {
            if (!routing_) { routing_ = true; routeMs_ = millis(); }
        } else if (msgType == 0x8011 || msgType == 0x8702 || msgType == 0x8701 ||
                   (msgType == 0x8012 && (noAck_ || status != 0x00))) {
            done_ = true;
            result_ = status;
            resultMsg_ = msgType;
        }
    }
    portEXIT_CRITICAL(&mux_);
}
