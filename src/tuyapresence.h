#ifndef TUYAPRESENCE_H
#define TUYAPRESENCE_H

#include <Arduino.h>
#include <stdint.h>

#ifndef TUYA_TYPE_RAW
#define TUYA_TYPE_RAW       0
#define TUYA_TYPE_BOOL      1
#define TUYA_TYPE_VALUE     2
#define TUYA_TYPE_STRING    3
#define TUYA_TYPE_ENUM      4
#define TUYA_TYPE_BITMAP    5
#endif

/**
 * @brief Gestionnaire principal cluster Tuya EF00 pour capteurs de presence
 *
 * DPs typiques (ZY-M100 / _TZE204_qasjif9e) :
 *   DP 1:   Presence (enum: 0=none, 1=presence)
 *   DP 2:   Sensibilite mouvement (int 0-9)
 *   DP 3:   Distance min detection (int, 0.01m)
 *   DP 4:   Distance max detection (int, 0.01m)
 *   DP 9:   Distance cible mesuree (int, 0.01m)
 *   DP 101: Fade time (int)
 *   DP 104: Luminosite (lux)
 */
void tuyaPresenceSensorManage(String inifile, int attribute, uint8_t datatype,
                              int len, char* datas);

/**
 * @brief Verifier si un manufacturer est un capteur de presence Tuya connu
 */
bool isTuyaPresenceSensor(const String& manufacturer);

#endif // TUYAPRESENCE_H
