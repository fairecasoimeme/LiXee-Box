#include <Arduino.h>
#include "doorlock.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include <WebPush.h>
#include "mqtt.h"

extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;
extern CircularBuffer<Device, 10> *deviceList;

void DoorlockManage(String inifile,int attribute,uint8_t datatype,int len, char* datas)
{
  //String inifile;
  char value[4];
  String tmp="";
  //inifile = GetMacAdrr(shortaddr);
  if (inifile !="")
  {
    switch (attribute)
    {       
      default:
        if (ini_exist(inifile))
        {
          for(int i=0;i<len;i++)
          {
            sprintf(value, "%02X",datas[i]);
            tmp+=value;
          }
          ini_write(inifile,"0101", (String)attribute, (String)tmp);
          
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            mqttPublish(inifile.substring(0,16),"0101",String(attribute),"string",String(tmp));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"257",(String)attribute,tmpvalue.c_str());
          }

          // Device update value;
          if (!deviceList->isFull())
          {
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,257,attribute,tmp});
          }
        }
        break;
    }
  }
}
