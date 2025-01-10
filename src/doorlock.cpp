#include <Arduino.h>
#include "doorlock.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include <WebPush.h>

extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;

void DoorlockManage(int shortaddr,int attribute,uint8_t datatype,int len, char* datas)
{
  String inifile;
  char value[4];
  String tmp="";
  inifile = GetMacAdrr(shortaddr);
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
            String tmpvalue;
            tmpvalue = "{\"257_"+String(attribute)+"\":";
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            tmpvalue +="}";
            String topic = ConfigGeneral.headerMQTT+ inifile.substring(0, 16)+"_257_"+String(attribute)+"/state";
            mqttClient.publish(topic.c_str(), 0, true, tmpvalue.c_str());

          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"257",(String)attribute,tmpvalue.c_str());
          }
        }
        break;
    }
  }
}
