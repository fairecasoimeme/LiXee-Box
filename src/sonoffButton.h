#pragma once
#include <Arduino.h>

// SONOFF SNZB-01M (Orb 4-in-1) : compose l'action d'un appui (ex. "double_button_2") a partir
// de l'endpoint source (= numero du bouton) et de la valeur de l'attribut FC12/0000.
void sonoffKeyActionManage(String inifile, uint8_t endpoint, int attribute, int len, char* datas);
