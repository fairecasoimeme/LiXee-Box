#pragma once
#include <Arduino.h>

/* Statistiques de puissance du ZLinky (v2.26), pour le kiosque LiXee-Assist.
 *
 * Trois niveaux, car un pic isole ne dit rien du risque de coupure : le Linky coupe sur un
 * depassement qui dure de l'ordre d'une minute.
 *  - pic instantane : la plus forte mesure de puissance apparente (0B04/1295) ;
 *  - moyenne sur 1 min glissante : ce qui fait couper le Linky (delestage, risque de coupure) ;
 *  - plateau sur 10 min : la charge durable, pour juger de l'abonnement.
 * En mode Standard s'y ajoute la valeur officielle du compteur : SMAXSN du jour (0B04/1293),
 * la veille etant cloturee par SMAXSN-1 (FF66/530).
 * Injection exclue, mesures superieures a 1,5 x la puissance souscrite ecartees.
 *
 * Triphase : le Linky coupe sur CHAQUE phase. Les trois niveaux et les tranches sont donc aussi
 * calcules par phase, rapportes a un tiers de la puissance souscrite. Standard : SINSTS1..3
 * (0B04/1295, 2319, 2575), total = leur somme ; SMAXSN-1 par phase (FF66/530..532).
 * Historique : PAPP totale (1295), phases d'apres IINST1..3 (1288, 2312, 2568) a 200 VA/A.
 *
 * Chaque minute mesuree est classee par tranche de charge (moyenne sur 1 min >= 50, 60, 70, 80,
 * 90, 100 % de la puissance souscrite) : minutes par jour, et par mois sur 24 mois pour voir les
 * periodes de l'annee. La frequence (tous les jours / semaines / mois / rarement) s'en deduit.
 *
 * Aussi : plancher nocturne (plus petite consommation horaire entre 1 h et 5 h, heures avec
 * production ignorees) et heures de bascule tarifaire observees (prochain changement HC/HP).
 *
 * Persistance : /hst/powerStats.json, ecrit au plus une fois par heure.
 */

void powerStatsBegin();
// A appeler a chaque tour de boucle (limite interne a un traitement toutes les 10 s).
void powerStatsLoop();
// Mesure du ZLinky configure : puissances apparentes 0B04/1295, 2319, 2575 (VA, injection deja
// ramenee a 0) et intensites 1288, 2312, 2568 (A). Le tri selon le mode Linky est fait ici.
void powerStatsOnAttribute(int attr, long value);

// Puissance souscrite totale en VA (0 si inconnue), et par phase en triphase (0 sinon).
long powerStatsSubscribedVA();
long powerStatsPhaseSubscribedVA();
// Moyenne de la puissance apparente totale sur la derniere minute, en VA (-1 si inconnue).
long powerStatsMinuteAvg();
// Charge sur la derniere minute, en % de la limite (-1 si inconnue) : total en monophase,
// phase la plus chargee en triphase. C'est ce qui decide d'une coupure du Linky.
int  powerStatsMinuteLoadPct();
// true si le ZLinky est en mode Standard (LinkyMode 1, 3, 5, 7) / triphase (2, 3, 7).
bool powerStatsIsStandard();
bool powerStatsIsTriphase();

// JSON des routes /api/power/peaks, /api/power/floor et /api/power/summary.
String powerStatsPeaksJson(int days);
String powerStatsFloorJson(int days);
String powerStatsSummaryJson();

// Attribut d'index en cours (256..272), 0 si inconnu.
int powerStatsCurrentIndexAttr();
// Prochain changement de tarif "HH:MM", ou "" si inconnu.
String powerStatsNextChange();
