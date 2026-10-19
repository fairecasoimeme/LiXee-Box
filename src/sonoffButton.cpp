#include <Arduino.h>
#include "sonoffButton.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <WebPush.h>
#include "mqtt.h"
#include "device.h"
#include "rules.h"

extern DeviceList devices;
extern ConfigSettingsStruct ConfigSettings;
extern LockedBuffer<Device, 50> *deviceList;
extern RulesManager rulesManager;

// Range une action de bouton (valeur TEXTE) sous cluster/attribut, la publie et rafraichit
// l'affichage en direct. Affichage, MQTT et regles ("Action == single", comparaison texte) la
// traitent sans autre adaptation.
static void storeButtonAction(const String& inifile, int cluster, int attribute,
                              const char* clusterHex, const String& action)
{
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
      device->setValue(clusterHex, String(attribute).c_str(), action.c_str());
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

/* SONOFF SNZB-01M "Orb 4-in-1" : bouton quadruple sur pile.
 *
 * Chaque bouton envoie l'attribut 0x0000 du cluster proprietaire 0xFC12 depuis SON endpoint
 * (1 a 4), avec le type d'appui en valeur : 1 simple, 2 double, 3 long, 4 triple
 * (source : zigbee-herdsman-converters, convertisseur key_action_event).
 *
 * Le numero du bouton n'existe QUE dans l'endpoint, que la box ignorait jusqu'ici : les quatre
 * boutons auraient ecrit au meme endroit. On compose donc une action unique, au format de
 * Zigbee2MQTT ("double_button_2") que connaissent les utilisateurs de Home Assistant, et on la
 * stocke comme une valeur TEXTE sous FC12/0000. Les regles etant evaluees a chaque rapport, deux
 * appuis identiques successifs declenchent bien deux fois.
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
  storeButtonAction(inifile, 0xFC12, attribute, "FC12", action);
}

/* Boutons qui envoient des COMMANDES du cluster On/Off, et non des rapports d'attribut.
 *
 * SONOFF SNZB-01P (et ses predecesseurs eWeLink SNZB-01 / WB01) : device_id 0x0000 (On/Off
 * Switch). Le type d'appui est code par la commande emise (zigbee-herdsman-converters,
 * convertisseur ewelink_action) : Toggle = simple, On = double, Off = long.
 *
 * Pour tout autre appareil (telecommande On/Off classique), la commande garde son sens :
 * "on", "off", "toggle".
 *
 * Aucun attribut ZCL ne porte cette information : l'action est rangee sous 0006/0000
 * (ONOFF_ACTION_ATTR), la convention deja utilisee par le bouton Aqara (24321.json). Un bouton
 * ne remonte pas d'etat On/Off, il n'y a donc rien a recouvrir. Comme ces trames ne passent pas
 * par readZigbeeDatas(), les regles sont declenchees ici.
 */
void onOffCommandActionManage(String inifile, uint8_t endpoint, uint8_t command)
{
  if (inifile == "") return;
  String ieee = inifile.substring(0, 16);

  String model;
  for (size_t i = 0; i < devices.size(); i++) {
    if (devices[i]->getDeviceID() == ieee) { model = devices[i]->getInfo().model; break; }
  }
  bool ewelink = model.startsWith("SNZB-01") || model == "WB01";

  const char* action;
  switch (command) {
    case 0x00: action = ewelink ? "long"   : "off";    break;
    case 0x01: action = ewelink ? "double" : "on";     break;
    case 0x02: action = ewelink ? "single" : "toggle"; break;
    default:
      Serial.printf("[Bouton] %s : commande On/Off 0x%02X ignoree (ep %u)\n",
                    ieee.c_str(), command, endpoint);
      return;
  }
  storeButtonAction(inifile, 0x0006, ONOFF_ACTION_ATTR, "0006", String(action));
  rulesManager.applyRulesOnEvent(ieee.c_str(), 0x0006, ONOFF_ACTION_ATTR);
}
