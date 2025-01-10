#include <Arduino.h>
#include "SimpleMeter.h"
#include "config.h"
#include "log.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include <WebPush.h>

extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;

void SimpleMeterManage(int shortaddr,int attribute,uint8_t datatype,int len, char* datas)
{
  String inifile;
  char value[256];
  String tmp="";
  inifile = GetMacAdrr(shortaddr);
  if (inifile!="")
  {
    switch (attribute)
    {
      case 0:   
      case 256:
      case 258:
      case 260:
      case 262:
      case 264:
      case 266:
      case 268:
      case 270:
      case 272:
      case 274:
        if (ini_exist(inifile))
        {
          for(int i=0;i<len;i++)
          {
            sprintf(value, "%02X",datas[i]);
            tmp+=value;
          }
          ini_write(inifile,"0702", (String)attribute, (String)tmp);
          if (!ini_energy(inifile, (String)attribute, (String) tmp))
          {
            String err ="PB ini_energy"+inifile+": 0x702/"+(String)attribute+" "+(String)tmp;
            addDebugLog(err);
          }

          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            String tmpvalue;
            tmpvalue = "{\"1794_"+String(attribute)+"\":";
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            tmpvalue +="}";
            String topic = ConfigGeneral.headerMQTT+ inifile.substring(0, 16)+"_1794_"+String(attribute)+"/state";
            mqttClient.publish(topic.c_str(), 0, true, tmpvalue.c_str());
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"1794",(String)attribute,tmpvalue.c_str());
          }
          ini_trendEnergy(inifile, (String)attribute, (String) tmp);
        }
        break; 
      case 1: 
        if (ini_exist(inifile))
        {
          for(int i=0;i<len;i++)
          {
            sprintf(value, "%02X",datas[i]);
            tmp+=value;
          }
          tmp="-"+tmp;
          ini_write(inifile,"0702", (String)attribute, (String)tmp);
          if (ConfigSettings.enableMqtt)
          {
            String tmpvalue;
            tmpvalue = "{\"1794_1\":";
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            tmpvalue +="}";
            String topic = ConfigGeneral.headerMQTT+ inifile.substring(0, 16)+"_1794_1/state";
            mqttClient.publish(topic.c_str(), 0, true, tmpvalue.c_str());

          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"1794",(String)attribute,tmpvalue.c_str());
          }
          ini_energy(inifile, (String)attribute, (String) tmp);
          ini_trendEnergy(inifile, (String)attribute, (String) tmp);
        }
        break; 
      case 32:
        if (ini_exist(inifile))
        {
          String tmp;
          for(int i=0;i<len;i++)
          {
            if(datas[i]>0)
            {
              tmp+= datas[i];
            }
          }
          ini_write(inifile,"0702", (String)attribute, (String)tmp);
          if (ConfigSettings.enableMqtt)
          {
            String tmpvalue;
            tmpvalue = "{\"1794_32\":";
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            tmpvalue +="}";
            String topic = ConfigGeneral.headerMQTT+ inifile.substring(0, 16)+"_1794_32/state";
            mqttClient.publish(topic.c_str(), 0, true, tmpvalue.c_str());

          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"1794",(String)attribute,tmpvalue.c_str());
          }
        }
      break;
      default:
        if (ini_exist(inifile))
        {
          for(int i=0;i<len;i++)
          {
            sprintf(value, "%02X",datas[i]);
            tmp+=value;
          }
          ini_write(inifile,"0702", (String)attribute, (String)tmp);
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            String tmpvalue;
            tmpvalue = "{\"1794_"+String(attribute)+"\":";
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            tmpvalue +="}";
            String topic = ConfigGeneral.headerMQTT+ inifile.substring(0, 16)+"_1794_"+String(attribute)+"/state";
            mqttClient.publish(topic.c_str(), 0, true, tmpvalue.c_str());
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"1794",(String)attribute,tmpvalue.c_str());
          }
        }
        break;      
    }
  }
}
