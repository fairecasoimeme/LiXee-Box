#pragma once
#include <Arduino.h>

/* Historique des 20 dernieres alertes, lisible sans etre consomme (v2.26).
 *
 * notifList se vide a la lecture (/getAlert) : le kiosque LiXee-Assist ne peut pas s'en servir
 * sans priver le telephone. Cet historique est alimente en parallele, depuis
 * NotificationManager::addNotification() par lequel passent toutes les alertes, sans rien
 * changer au flux existant (file + push). Chaque alerte recoit un id croissant, conserve d'un
 * redemarrage a l'autre (/hst/alerts.json).
 */

void alertHistoryBegin();
void alertHistoryAdd(const String& title, const String& message, int level,
                     const char* alertType, float value, float threshold);
// {"alerts":[...],"lastId":N} : alertes d'id strictement superieur a `since`.
String alertHistoryJson(uint32_t since);
