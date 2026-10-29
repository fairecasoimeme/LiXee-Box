#pragma once

// ============================================================================
// Notifications - centre de notifications, alertes energie, envoi email et
// WebPush (lot 4 du portage LiXee-Box : notificationManager.cpp,
// alertChecks.cpp, mail.cpp, WebPush.cpp et les alertes des handlers ZLinky
// lixee.cpp / ElectricalMeasurement.cpp).
//
// Architecture :
//   - Magasin : 100 notifications au plus, en PSRAM, persistees dans
//     /littlefs/config/notifications.json (meme format que la LiXee-Box,
//     plus les champs id / ts / alertType).
//   - notifications_add() est utilisable depuis n'importe quelle tache : elle
//     ne touche ni a la flash ni au reseau (ecriture differee, envoi email
//     confie a la tache "notif").
//   - Tache "notif" (pile en PSRAM : elle ne touche jamais a la flash) :
//     evaluation des alertes, envoi SMTP (esp-tls) et WebPush (esp_http_client).
//   - notifications_tick() (tache principale) ecrit le magasin en flash.
//
// Types : 0 info, 1 attention, 2 erreur, 3 critique (badges de la page).
// ============================================================================

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NOTIF_MAX            100     // capacite du magasin (maxNotifications Arduino)
#define NOTIF_TITLE_LEN      96
#define NOTIF_MSG_LEN        768     // = RULES_NOTIF_LEN : message d'une regle (conditions comprises) non tronque
#define NOTIF_ALERT_LEN      24
#define NOTIF_POLL_MAX       10      // file /poll (notifList Arduino)

typedef enum {
    NOTIF_INFO     = 0,
    NOTIF_WARNING  = 1,
    NOTIF_ERROR    = 2,
    NOTIF_CRITICAL = 3,
} notif_type_t;

// ----------------------------------------------------------------------------
// Cycle de vie
// ----------------------------------------------------------------------------

/**
 * Charge le magasin et demarre la tache "notif".
 * A appeler apres le montage de LittleFS et cfg_load_general(), tot au
 * demarrage (l'analyse du fichier utilise transitoirement de la RAM interne).
 */
esp_err_t notifications_init(void);

/**
 * A appeler depuis la boucle principale, idealement chaque seconde (une fois
 * par minute minimum) : ecrit en flash les modifications en attente.
 * Ne fait rien d'autre (les alertes sont evaluees par la tache "notif").
 */
void notifications_tick(void);

/**
 * Demande l'execution immediate des controles de fin de journee (budget de la
 * veille). Facultatif : ils sont aussi planifies par l'heure locale, et ne
 * s'executent qu'une fois par jour.
 */
void notifications_daily_hook(void);

/** Ecrit tout de suite le magasin s'il a change (appelable depuis une tache a pile interne). */
esp_err_t notifications_flush(void);

// ----------------------------------------------------------------------------
// Ajout (regles, cluster handlers, autres lots)
// ----------------------------------------------------------------------------

/**
 * Ajoute une notification (thread-safe, non bloquant). Equivalent de
 * NotificationManager::addNotification() + notifList->push() Arduino.
 * @param alert_type identifiant machine ("over_voltage", "rule"...), NULL accepte
 * @return id de la notification, 0 si refusee
 */
uint32_t notifications_add_alert(const char *title, const char *message, int type,
                                 const char *alert_type, float value, float threshold);

/** Forme courte (alert_type NULL). */
uint32_t notifications_add(const char *title, const char *message, int type);

/**
 * Point d'entree des rapports d'attributs (appele par cluster_handlers pour
 * chaque rapport) : alertes ZLinky et WebPush. Non bloquant : l'evenement est
 * mis en file pour la tache "notif" (perdu si la file est pleine).
 * @param is_str  true : str contient la valeur texte ; sinon num
 */
void notifications_on_report(uint64_t ieee, uint16_t cluster, uint16_t attr,
                             bool is_str, int64_t num, const char *str);

/**
 * Rappel optionnel appele a chaque notification ajoutee (tunnel / push
 * mobile d'un autre lot). Appele hors verrou, dans la tache de l'appelant de
 * notifications_add* : il doit etre rapide et non bloquant.
 */
typedef void (*notif_push_cb_t)(const char *title, const char *message, int type,
                                const char *alert_type, float value, float threshold);
void notifications_set_push_callback(notif_push_cb_t cb);

/** Message court affiche en bandeau par l'interface (/getAlert). state : 0 succes, 1 erreur, 2 info. */
void notifications_toast(int state, const char *msg);

// ----------------------------------------------------------------------------
// Gestion (routes web)
// ----------------------------------------------------------------------------

bool notifications_mark_read(uint32_t id);
int  notifications_mark_all_read(void);     // nombre de notifications marquees
bool notifications_delete(uint32_t id);
void notifications_clear(void);
int  notifications_count(void);
int  notifications_unread(void);

/**
 * JSON des notifications, plus recentes d'abord :
 * {"total":N,"unread":U,"offset":o,"limit":l,"page":p,"notifications":[{id,title,
 *  message,timeStamp,ts,type,viewed,alertType}]}
 * Chaine allouee (PSRAM) a liberer par free(), NULL si memoire insuffisante.
 */
char *notifications_list_json(int offset, int limit);
/** {"total","unread","read","maxCapacity","psramFree","psramTotal"} */
char *notifications_stats_json(void);

/**
 * Historique des 20 dernieres alertes (alertHistory de la LiXee-Box), en RAM :
 * {"alerts":[{id,ts,type,level,title,message,value,threshold}],"lastId":n}.
 * @param since seules les alertes d'id superieur (0 : toutes)
 */
char *notifications_alerts_json(uint32_t since);
/** {"total","psramFree","notifications":[{index,id,title,viewed}]} */
char *notifications_debug_json(void);
/** Etat des envois : {"mail":{...},"webpush":{...},"queue":{...}} */
char *notifications_send_status_json(void);

/**
 * Vide la file /poll (notifications ajoutees depuis le dernier appel) dans
 * root["notifications"] (tableau, eventuellement vide). root : cJSON objet.
 */
struct cJSON;
void notifications_poll_add_to(struct cJSON *root);

/** Retire le plus ancien bandeau en attente. false si aucun. */
bool notifications_toast_take(int *state, char *msg, size_t len);

// ----------------------------------------------------------------------------
// Envois de test (asynchrones : resultat dans notifications_send_status_json)
// ----------------------------------------------------------------------------

esp_err_t notifications_test_mail(void);
esp_err_t notifications_test_webpush(void);

/**
 * Verrou des reglages SMTP de cfg_general_t (serv/port/user/pass/to/from_smtp,
 * security_smtp). A prendre pour MODIFIER ces champs : l'envoi d'un mail en
 * copie le contenu sous ce verrou au debut de chaque envoi. Bref : ne rien
 * faire de long (ni ecriture flash) verrou tenu.
 */
void notifications_smtp_cfg_lock(void);
void notifications_smtp_cfg_unlock(void);

#ifdef __cplusplus
}
#endif
