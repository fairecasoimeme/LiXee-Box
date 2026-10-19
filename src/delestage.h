#pragma once
#include <Arduino.h>

/* Delestage (v2.26).
 *
 * Sur depassement de la puissance souscrite (STGE ou ADPS), les prises listees dans la config
 * Energie (cle "delestage" de configGeneral.json) sont coupees et leur etat precedent (0006/0)
 * memorise dans /config/delestage.json (format historique conserve : "state", "dateOn",
 * "<ieee>": etat, plus "dateOff").
 *
 * Nouveau : rallumage automatique quand la puissance apparente est restee
 * ConfigGeneral.delestageRestoreMin minutes sous ConfigGeneral.delestageRestorePct % de la
 * puissance souscrite (0 minute = pas de rallumage automatique), et rallumage manuel.
 * Seules les prises qui etaient allumees avant la coupure sont rallumees.
 * Option : delestage anticipe des que la moyenne sur 1 min atteint
 * ConfigGeneral.delestageAnticipPct % de la puissance souscrite (0 = seulement sur signal du
 * compteur). Le rallumage se juge aussi sur la moyenne sur 1 min.
 */

void delestageBegin();
// Depassement detecte : coupe les prises (une seule fois tant que le delestage est actif).
// reason : "meter" (signal du compteur) ou "anticipation" (moyenne sur 1 min trop haute).
void delestageTrigger(const char* reason = "meter");
// A appeler a chaque tour de boucle (traitement limite a une fois par seconde).
void delestageLoop();
// Rallume les prises coupees. false si aucun delestage n'etait actif.
bool delestageRestore(bool manual);
// Demande de rallumage depuis une route web : executee au prochain delestageLoop().
void delestageRequestRestore();
// {"active":..,"since":..,"lastEnd":..,"devices":[...]} pour GET /api/delestage.
String delestageJson();
