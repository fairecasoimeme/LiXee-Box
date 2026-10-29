#pragma once

// ============================================================================
// Tunnel d'acces distant (portage de LiXeeBoxTunnel, tunnel.h de la LiXee-Box)
//
// Le pont ouvre une connexion WebSocket sur TLS vers remote.lixee-box.fr :
//   wss://remote.lixee-box.fr/tunnel?token=<jeton>&clientId=<id>
//   (sous-protocole "lixeebox", Origin "file://", comme la LiXee-Box)
// Le relais y pousse les requetes HTTP des navigateurs qui visitent
// https://<sous-domaine>.lixee-box.fr ; le pont les rejoue sur son propre
// serveur HTTP (127.0.0.1:80) et renvoie les reponses par le meme canal.
//
// Protocole (messages texte JSON, identiques a la LiXee-Box) :
//   relais -> pont : {"type":"welcome","deviceId":"..","subdomain":".."}
//                    {"type":"http_request","reqId":"..","method":"GET",
//                     "path":"/x?y","headers":{..},"body":"<base64>"}
//   pont -> relais : {"type":"info","data":{firmware,version,chip,freeHeap,localIP}}
//                    {"type":"http_response","reqId":"..","status":200,
//                     "headers":{..,"Content-Length":"n"},"body":"<base64>"}
//                    {"type":"heartbeat","uptime":s,"freeHeap":n,"freePsram":n}
//                    {"type":"notification",...} (voir tunnel_send_notification)
// Les gros messages sont emis en fragments WebSocket de 8 Ko (TEXT puis
// CONTINUATION), que le relais reassemble ; ping WebSocket toutes les 15 s,
// coupure apres 3 pongs manques (10 s chacun).
//
// Reglages : /littlefs/config/tunnel.json (enableTunnel, tunnelClientId,
// tunnelToken). Au premier demarrage, ils sont repris des cles de meme nom de
// configGeneral.json (LiXee-Box).
//
// Le tunnel publie l'interface sur Internet : il ne tourne que si l'acces HTTP
// est protege (enableSecureHttp ET identifiant ET mot de passe), voir
// tunnel_security_ok().
// ============================================================================

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TUNNEL_CFG_PATH        "/littlefs/config/tunnel.json"
#define TUNNEL_CLIENT_ID_MAX   64      // tailles de la LiXee-Box (config.h)
#define TUNNEL_TOKEN_MAX       128

typedef enum {
    TUNNEL_STATE_DISABLED = 0,   // desactive, ou aucun jeton
    TUNNEL_STATE_SUSPENDED,      // active mais acces HTTP non protege
    TUNNEL_STATE_CONNECTING,     // connexion TLS / WebSocket en cours
    TUNNEL_STATE_CONNECTED,      // WebSocket etabli
    TUNNEL_STATE_WAITING,        // echec, nouvel essai programme
} tunnel_state_t;

typedef enum {
    TUNNEL_ACT_IDLE = 0,
    TUNNEL_ACT_PROCESSING,
    TUNNEL_ACT_DONE,
} tunnel_act_status_t;

typedef struct {
    bool           enabled;          // reglage enableTunnel
    bool           has_token;
    bool           security_ok;      // voir tunnel_security_ok()
    bool           connected;
    tunnel_state_t state;
    char           client_id[TUNNEL_CLIENT_ID_MAX];
    char           device_id[TUNNEL_CLIENT_ID_MAX];   // annonce par le relais (welcome)
    char           subdomain[128];                    // ex. "abcd.lixee-box.fr"
    char           last_error[96];                    // derniere erreur de connexion
    int            upgrade_status;                    // statut HTTP de la poignee WebSocket
    uint32_t       connected_for_s;                   // duree de la connexion courante
    uint32_t       retry_in_s;                        // prochain essai (etat WAITING)
    uint32_t       reconnects;                        // connexions etablies depuis le boot
    uint32_t       requests;                          // requetes relayees depuis le boot
    uint32_t       queued;                            // requetes en attente d'un worker
    uint32_t       in_flight;                         // requetes en cours de traitement
} tunnel_status_t;

typedef struct {
    tunnel_act_status_t status;
    bool  success;
    char  device_id[TUNNEL_CLIENT_ID_MAX];
    char  error[192];
} tunnel_activation_t;

typedef enum {
    TUNNEL_ACT_START_OK = 0,
    TUNNEL_ACT_START_BUSY,          // une activation est deja en cours
    TUNNEL_ACT_START_BAD_CODE,      // code different de 6 chiffres
    TUNNEL_ACT_START_NO_SECURITY,   // acces HTTP non protege
    TUNNEL_ACT_START_FAILED,        // tunnel non demarre ou memoire insuffisante
} tunnel_act_start_t;

/**
 * Charge /littlefs/config/tunnel.json, ou migre les cles enableTunnel /
 * tunnelClientId / tunnelToken de configGeneral.json s'il est absent.
 * A appeler juste apres cfg_load_general(), AVANT tout cfg_save_general()
 * (qui reecrit configGeneral.json sans ces cles). Ne demarre rien.
 */
esp_err_t tunnel_init(void);

/**
 * Cree les taches du tunnel (WebSocket + workers). A appeler apres
 * web_server_init() : les requetes sont rejouees sur 127.0.0.1:80.
 * La connexion n'est ouverte que si le tunnel est active, qu'un jeton existe
 * et que tunnel_security_ok() ; sinon les taches attendent.
 */
esp_err_t tunnel_start(void);

/**
 * Condition d'activation (tunnelSecurityOk() de la LiXee-Box) : securite HTTP
 * activee ET identifiant ET mot de passe renseignes.
 */
bool tunnel_security_ok(void);

/**
 * Vrai si la securite HTTP ne doit pas pouvoir etre desactivee : tunnel active
 * et securite active (verrou de handleSaveConfigHTTP sur la LiXee-Box).
 */
bool tunnel_security_locked(void);

/**
 * A appeler apres toute modification de la securite HTTP (enableSecureHttp,
 * userHTTP, passHTTP). Securite perdue : le tunnel est desactive (et le
 * reglage enregistre, comme la LiXee-Box). Securite retrouvee : un tunnel
 * active mais suspendu reprend. Ecrit en flash : appeler depuis une tache a
 * pile interne (handler HTTP, tache principale).
 */
void tunnel_on_security_changed(void);

/** Copie de la configuration courante (jeton en clair : ne pas l'exposer). */
void tunnel_get_config(bool *enabled, char *client_id, size_t client_id_len,
                       char *token, size_t token_len);

/**
 * Modifie et enregistre la configuration, puis (re)demarre ou arrete la
 * connexion. client_id / token NULL = inchange. Aucune validation metier ici
 * (voir web_tunnel.cpp) hormis les caracteres et longueurs.
 * @param restart_delay_ms delai avant de couper la connexion courante, pour
 *        laisser partir la reponse HTTP d'une requete arrivee par le tunnel.
 * Ecrit en flash : appeler depuis une tache a pile interne.
 * @return ESP_ERR_INVALID_ARG (caracteres/longueur), ESP_FAIL (ecriture).
 */
esp_err_t tunnel_set_config(bool enabled, const char *client_id, const char *token,
                            uint32_t restart_delay_ms);

/** Etat de la connexion, pour la page et l'API. */
void tunnel_get_status(tunnel_status_t *out);

/**
 * Lance l'activation par code (GET https://remote.lixee-box.fr/api/activate
 * ?code=XXXXXX). En cas de succes, deviceId et token recus deviennent
 * tunnelClientId / tunnelToken, le tunnel est active et redemarre.
 */
tunnel_act_start_t tunnel_start_activation(const char *code);

/** Resultat de la derniere activation (conserve jusqu'a la suivante). */
void tunnel_get_activation(tunnel_activation_t *out);

/**
 * Pousse une notification vers l'application mobile via le relais
 * (sendNotification de la LiXee-Box). Sans effet si le tunnel n'est pas
 * connecte. alert_type peut etre NULL ; value / threshold a 0 sont omis.
 * @return true si le message a ete mis en file d'emission.
 */
bool tunnel_send_notification(const char *title, const char *message, int type,
                              const char *alert_type, float value, float threshold);

#ifdef __cplusplus
}
#endif
