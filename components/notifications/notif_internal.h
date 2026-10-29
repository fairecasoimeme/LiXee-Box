#pragma once

// ============================================================================
// Notifications - interfaces internes au composant (magasin, alertes, envois)
// ============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include "esp_err.h"

// ----------------------------------------------------------------------------
// Tampon de texte extensible en PSRAM (JSON, corps de mail)
// ----------------------------------------------------------------------------

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    bool   oom;         // une allocation a echoue : le resultat est inutilisable
} nbuf_t;

void  nbuf_init(nbuf_t *b, size_t cap);
void  nbuf_add(nbuf_t *b, const char *s, size_t n);
void  nbuf_str(nbuf_t *b, const char *s);
void  nbuf_printf(nbuf_t *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/** Chaine JSON entre guillemets, echappee (UTF-8 conserve). */
void  nbuf_json_str(nbuf_t *b, const char *s);
/** Rend la chaine (a liberer par free()), NULL si une allocation a echoue. */
char *nbuf_finish(nbuf_t *b);
void  nbuf_free(nbuf_t *b);

/** malloc en PSRAM, repli en RAM interne. */
void *notif_psram_alloc(size_t n);

// ----------------------------------------------------------------------------
// Magasin (notif_store.cpp)
// ----------------------------------------------------------------------------

/** Copie d'une notification (pour l'envoi par mail). false si elle n'existe plus. */
bool notif_store_get(uint32_t id, char *title, size_t tlen, char *msg, size_t mlen,
                     char *stamp, size_t slen, int *type);

/** Date au format LiXee-Box "JJ/MM/AAAA HH:MM" (vide si l'heure n'est pas synchronisee). */
void notif_format_stamp(char *out, size_t len);

// ----------------------------------------------------------------------------
// Tache d'envoi (notif_send.cpp)
// ----------------------------------------------------------------------------

/** Demarre la tache "notif" et sa file. */
esp_err_t notif_worker_start(void);
/** Met en file l'envoi par mail d'une notification (si l'email est actif). */
void notif_worker_queue_mail(uint32_t id, int type);
/** Demande les controles de fin de journee. */
void notif_worker_queue_daily(void);

// ----------------------------------------------------------------------------
// Alertes (notif_alerts.cpp), executees dans la tache "notif"
// ----------------------------------------------------------------------------

typedef struct {
    uint64_t ieee;
    uint16_t cluster;
    uint16_t attr;
    bool     is_str;
    int64_t  num;
    char     str[40];
} notif_event_t;

/** true si un rapport de cet appareil peut interesser les alertes (filtre rapide). */
bool notif_alerts_wants(uint64_t ieee, uint16_t cluster);
/** Traite un rapport d'attribut (transitions ZLinky, tensions, production...). */
void notif_alerts_on_event(const notif_event_t *ev);
/** Controles periodiques, appele toutes les 10 s par la tache "notif". */
void notif_alerts_periodic(void);
/** Controles de fin de journee (budget de la veille), une fois par date. */
void notif_alerts_daily(void);
