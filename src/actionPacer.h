#ifndef ACTION_PACER_H
#define ACTION_PACER_H

#include <Arduino.h>
#include <deque>
#include "config.h"
#include "rules.h"      // ActionRule, PsString, PsramAllocator

/* ===================== Cadencement des actions radio (regles, groupes) =====================
 *
 * Un groupe d'actions, ou une regle a plusieurs actions, envoyait toutes ses commandes d'un
 * coup dans la file Zigbee. Celle-ci les transmet a 100 ms d'intervalle, ce qui protege la
 * liaison serie -- mais pas la ZiGate elle-meme : chaque commande envoyee avec demande
 * d'accuse occupe une place dans sa table d'attente d'accuses (APS) jusqu'a la reponse de
 * l'appareil. Un appareil sain repond en quelques dizaines de ms ; un appareil qui n'accuse
 * jamais (observe : une clim, meme avec un LQI de 233) ou injoignable (D4) garde sa place
 * plusieurs secondes. Quelques-uns suffisent a remplir la table (0x9999 statut 0x83,
 * NO_FREE_APS_ACK) : les commandes suivantes sont alors perdues, sans aucun retour.
 *
 * Ici, les actions radio passent une a une : chacune attend l'issue de la precedente --
 * accuse (0x8011), echec (0x8702), echec de recherche de route (0x8701), refus de la ZiGate
 * (0x8000) -- ou, au plus, ACK_WAIT_MS apres le depart de sa trame. Quand tout repond, le
 * groupe s'execute presque aussi vite qu'avant ; face a un appareil muet, on attend au plus
 * ACK_WAIT_MS sans rien empiler.
 *
 * D4 n'est PAS une issue : la ZiGate n'a pas de route, elle garde la trame et lance une
 * recherche de route, puis la renverra d'elle-meme. On attend alors l'issue reelle jusqu'a
 * ROUTE_WAIT_MS. Traiter D4 comme un echec lancait toutes les recherches de route d'un groupe
 * en meme temps (observe : 6 volets en moins d'une seconde) : la ZiGate saturait (0x9999
 * statut 0x85, plus de descripteur d'emission MAC libre) et TOUTES echouaient (0x8701 D0).
 *
 * Refus faute de place : la table d'accuses de la ZiGate est petite (8 places observees), et
 * ses propres reponses automatiques aux rapports des appareils y prennent place aussi. Quand
 * elle est pleine (0x9999 0x83), la ZiGate refuse la commande tout de suite (0x8000 statut
 * 0x15). Ce n'est pas un echec de l'action : on la remet en tete de file et on attend qu'une
 * place se libere (un 0x8011 ou 0x8702 quelconque) avant de reessayer. Auparavant ce refus
 * etait pris pour une issue : la file se vidait en une seconde et les actions etaient perdues.
 *
 * Seules les actions qui emettent une commande Zigbee (device, dynamic, onoff) sont
 * cadencees. Les notifications restent immediates (elles decrivent l'etat au moment du
 * declenchement), de meme que l'appel d'un groupe par une regle, qui ne fait que mettre les
 * actions du groupe en file.
 *
 * Fils d'execution : enqueue() est appele depuis les regles (tache serie ou boucle) et depuis
 * le serveur web (groupes) ; tick() et onSent() depuis la boucle principale ; les resultats
 * radio depuis la tache serie. La file est protegee par un mutex, l'etat d'attente par une
 * section critique (aucune allocation dedans).
 */
class ActionPacer {
public:
    // A appeler dans setup(), avant toute regle ou tout groupe.
    void begin();

    // Vrai pour les actions qui emettent une commande Zigbee : ce sont elles qui passent par
    // la file.
    static bool isRadioAction(const ActionRule& act);

    // Met une action en file. `origin` : nom de la regle, ou "groupe \"X\"" (traces [Action]).
    // Renvoie false -- et l'action n'est pas executee -- si une action identique attend deja
    // (regle en mode repetition, double clic sur un groupe) ou si la file est pleine.
    bool enqueue(const ActionRule& act, const char* origin);

    // Boucle principale : lance l'action suivante des que la precedente est terminee.
    void tick();

    // sendZigbeeCmd() : une trame vient de partir vers la ZiGate.
    void onSent(const Packet& p);
    // DecodePayload() : statut de commande de la ZiGate (0x8000).
    void onCommandStatus(uint8_t status, uint16_t packetType);
    // DecodePayload() : confirmation d'emission (0x8012), accuse (0x8011) ou echec (0x8702)
    // concernant l'adresse courte `sa`.
    void onRadioResult(uint16_t msgType, uint16_t sa, uint8_t status);

private:
    struct PacedAction {
        ActionRule act;
        PsString   origin;
        uint32_t   queuedAt = 0;
        uint8_t    busyRetries = 0;   // nouveaux essais apres un refus "ZiGate saturee"
        PacedAction() : origin(PsramAllocator<char>()) {}
    };
    std::deque<PacedAction, PsramAllocator<PacedAction>> queue_;
    SemaphoreHandle_t mutex_ = nullptr;

    // Action en cours : gardee pour pouvoir la remettre en file si la ZiGate la refuse faute
    // de place (0x8000 statut 0x15, apres 0x9999 0x83). Boucle principale uniquement.
    PacedAction current_;
    bool        hasCurrent_ = false;
    // Pause apres un tel refus : jusqu'a pauseUntil_, ou des qu'une place se libere dans la
    // table d'accuses de la ZiGate (un 0x8011 ou 0x8702 quelconque, cf. slotFreed_).
    uint32_t      pauseUntil_ = 0;
    volatile bool slotFreed_  = false;

    // Action en cours d'attente (partage avec la tache serie, sous mux_).
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    volatile bool waiting_ = false;
    uint16_t target_    = 0;      // adresse courte attendue
    bool     sent_      = false;  // sa trame est partie vers la ZiGate
    bool     done_      = false;  // issue connue (accuse, echec, refus)
    bool     noAck_     = false;  // envoyee sans demande d'accuse (mode 0x07)
    bool     routing_   = false;  // D4 recu : trame en attente d'une recherche de route
    uint32_t routeMs_   = 0;
    uint16_t sentCmd_   = 0;      // commande ZiGate de la trame (pour 0x8000)
    uint8_t  result_    = 0;      // statut de l'issue
    uint16_t resultMsg_ = 0;      // message porteur de l'issue (0x8011, 0x8702...)
    uint32_t startMs_   = 0;
    uint32_t sentMs_    = 0;
    char     label_[40] = {0};    // origine de l'action en cours, pour les traces

    static bool sameAction(const ActionRule& a, const ActionRule& b);
};

extern ActionPacer actionPacer;

#endif
