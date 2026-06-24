#ifndef TUYAIRRIGATION_H
#define TUYAIRRIGATION_H

#include <Arduino.h>
#include <stdint.h>

/**
 * @brief Gestionnaire principal cluster Tuya EF00 pour vannes d'irrigation
 *
 * DPs typiques (QT-05M / _TZE200_arge1ptm, GiEX QT06, etc.) :
 *   DP 1:   Mode irrigation (enum: 0=duration, 1=capacity)
 *   DP 2:   Etat vanne (bool: 0=OFF, 1=ON)
 *   DP 101: Heure debut irrigation
 *   DP 102: Heure fin irrigation
 *   DP 103: Nombre de cycles (0=cycle unique)
 *   DP 104: Objectif irrigation (minutes ou litres selon mode)
 *   DP 105: Intervalle entre cycles (minutes)
 *   DP 108: Batterie (%)
 *   DP 111: Eau consommee (litres)
 *   DP 114: Duree derniere irrigation
 */
void tuyaIrrigationManage(String inifile, int attribute, uint8_t datatype,
                          int len, char* datas);

/**
 * @brief Verifier si un manufacturer est une vanne d'irrigation Tuya connue
 */
bool isTuyaIrrigation(const String& manufacturer);

#endif // TUYAIRRIGATION_H
