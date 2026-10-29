#pragma once
// ============================================================================
// Delestage (portage de delestage.cpp de la LiXee-Box v2.26)
//
// Sur depassement de la puissance souscrite signale par le compteur (ADPS en
// mode historique, bit 7 de STGE en mode standard), les prises choisies dans
// Config Energie sont coupees et leur etat precedent (0006/0000) memorise dans
// /littlefs/config/delestage.json (format de la LiXee-Box).
//   - re-coupure, au plus une fois par minute, d'une prise rallumee a la main ;
//   - delestage anticipe (option) : coupure des que la charge moyenne sur
//     1 minute atteint delestageAnticipPct % de la puissance souscrite (en
//     triphase : la phase la plus chargee) ;
//   - rallumage automatique apres delestageRestoreMin minutes sous
//     delestageRestorePct % (0 minute : jamais), ou manuel. Seules les prises
//     allumees avant la coupure sont rallumees.
// ============================================================================

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Relit l'etat (delestage en cours au redemarrage) et demarre la tache. */
esp_err_t delestage_init(void);

/** Demande de rallumage manuel (route web), executee par la tache. false si aucun delestage. */
bool delestage_request_restore(void);

/** Etat pour GET /api/delestage, a liberer par free(). */
char *delestage_json(void);

#ifdef __cplusplus
}
#endif
