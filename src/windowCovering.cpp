#include <Arduino.h>
#include "onoff.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include <WebPush.h>
#include "mqtt.h"
#include "device.h"
#include "windowCovering.h"

extern DeviceList devices;
extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;

extern CircularBuffer<Packet, 100> *commandList;
extern CircularBuffer<Packet, 70> *PrioritycommandList;
extern CircularBuffer<Device, 50> *deviceList;

void SendWindowCoveringAction(int shortaddr, int endpoint, String value)
{
    Packet trame;
    char sA[2];
    sA[0] = shortaddr /256;
    sA[1] = shortaddr % 256;
    trame.cmd=0x00fa;
    trame.len=6;
    uint8_t datas[18];
    datas[0]=0x02;
    datas[1]=sA[0];
    datas[2]=sA[1];
    datas[3]= 1;
    datas[4]= endpoint;
    datas[5]= value.toInt();
    
    memcpy(trame.datas,datas,6);
    PrioritycommandList->push(trame);
}

// Commande ZCL "Go To Lift Percentage" (0x05), transmise par la commande ZiGate 0x00FA suivie
// d'un octet de pourcentage (doc ZiGate : "5 = Go To Lift Percentage, extra cmd : 0-100").
// `openPercent` est TOUJOURS une ouverture (0 = ferme, 100 = ouvert) : c'est ce qu'affiche le
// curseur et ce que passent regles et groupes. En ZCL, la position de levage mesure au
// contraire la FERMETURE (0 = ouvert, 100 = ferme) : on convertit, sauf pour les appareils qui
// inversent deja le sens (voir windowCovering.h).
void SendWindowCoveringPosition(int shortaddr, int endpoint, int openPercent, bool inverted)
{
    if (openPercent < 0)   openPercent = 0;
    if (openPercent > 100) openPercent = 100;
    uint8_t lift = inverted ? (uint8_t)openPercent : (uint8_t)(100 - openPercent);

    Packet trame;
    trame.cmd = 0x00fa;
    trame.len = 7;
    uint8_t datas[7] = {
        0x02,                                           // adressage par adresse courte
        (uint8_t)(shortaddr >> 8), (uint8_t)(shortaddr & 0xFF),
        1,                                              // endpoint source (ZiGate)
        (uint8_t)endpoint,                              // endpoint destination
        0x05,                                           // Go To Lift Percentage
        lift                                            // pourcentage de levage ZCL
    };
    memcpy(trame.datas, datas, 7);
    PrioritycommandList->push(trame);
}

void WindowCoveringManage(String inifile,int attribute,uint8_t datatype,int len, char* datas)
{
  //String inifile;
  char value[256];
  String tmp="";
  //inifile = GetMacAdrr(shortaddr);
  if (inifile != "")
  {
    switch (attribute)
    {
      case 8:        
        for(int i=0;i<len;i++)
        {
          snprintf(value, sizeof(value), "%02X",datas[i]);
          tmp+=value;
        }
        if (ini_exist(inifile))
        {
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            mqttPublish(inifile.substring(0,16),"258",String(attribute),"numeric",String(tmp));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"258",(String)attribute,tmpvalue.c_str());
          }
          // Device update value;
          if (!deviceList->isFull())
          {
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,258,attribute,String(strtol(tmp.c_str(), NULL, 16))});
          }
        }
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue("0102",String(attribute).c_str(),tmp.c_str());
            break;
          }
        }
        break;       
      default:
        
        for(int i=0;i<len;i++)
        {
          snprintf(value, sizeof(value), "%02X",datas[i]);
          tmp+=value;
        }
        if (ini_exist(inifile))
        {
          //ini_write(inifile,"0006", (String)attribute, (String)tmp);
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            mqttPublish(inifile.substring(0,16),"258",String(attribute),"numeric",String(tmp));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"258",(String)attribute,tmpvalue.c_str());
          }

          // Device update value;
          if (!deviceList->isFull())
          {
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,258,attribute,String(strtol(tmp.c_str(), NULL, 16))});
          }
        }
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue("0102",String(attribute).c_str(),tmp.c_str());
            break;
          }
        }
        break;
    }
  }
}
