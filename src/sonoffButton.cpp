#include <Arduino.h>
#include "sonoffButton.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <WebPush.h>
#include "mqtt.h"
#include "device.h"

extern DeviceList devices;
extern ConfigSettingsStruct ConfigSettings;
extern CircularBuffer<Device, 50> *deviceList;

/* SONOFF SNZB-01M "Orb 4-in-1" : bouton quadruple sur pile.
 *
 * Chaque bouton envoie l'attribut 0x0000 du cluster proprietaire 0xFC12 depuis SON endpoint
 * (1 a 4), avec le type d'appui en valeur : 1 simple, 2 double, 3 long, 4 triple
 * (source : zigbee-herdsman-converters, convertisseur key_action_event).
 *
 * Le numero du bouton n'existe QUE dans l'endpoint, que la box ignorait jusqu'ici : les quatre
 * boutons auraient ecrit au meme endroit. On compose donc une action unique, au format de
 * Zigbee2MQTT ("double_button_2") que connaissent les utilisateurs de Home Assistant, et on la
 * stocke comme une valeur TEXTE sous FC12/0000. Affichage, mise a jour en direct, MQTT et regles
 * ("Action == double_button_2", comparaison texte) la traitent sans autre adaptation. Les regles
 * etant evaluees a chaque rapport, deux appuis identiques successifs declenchent bien deux fois.
 */
void sonoffKeyActionManage(String inifile, uint8_t endpoint, int attribute, int len, char* datas)
{
  if (inifile == "" || attribute != 0 || len < 1) return;

  const char* press;
  switch ((uint8_t)datas[0]) {
    case 1:  press = "single";  break;
    case 2:  press = "double";  break;
    case 3:  press = "long";    break;
    case 4:  press = "triple";  break;
    default: press = "unknown"; break;
  }
  String action = String(press) + "_button_" + String(endpoint);
  const int cluster = 0xFC12;
  String ieee = inifile.substring(0, 16);

  if (ini_exist(inifile)) {
    if (ConfigSettings.enableMqtt) {
      mqttPublish(ieee, String(cluster), String(attribute), "string", action);
    }
    if (ConfigSettings.enableWebPush) {
      WebPush(ieee, String(cluster), String(attribute), action.c_str());
    }
  }
  for (size_t i = 0; i < devices.size(); i++) {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == ieee) {
      device->setValue("FC12", "0", action.c_str());
      if (!deviceList->isFull()) {
        deviceList->push(Device{device->getInfo().shortAddr.toInt(), cluster, attribute, action});
      }
      break;
    }
  }
  Serial.print("[Bouton] ");
  Serial.print(ieee);
  Serial.print(" : ");
  Serial.println(action);
}
