#include <Arduino.h>
#include "ElectricalMeasurement.h"
#include "config.h"
#include "log.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include <WebPush.h>

extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;

void ElectricalMeasurementManage(int shortaddr,int attribute,uint8_t datatype,int len, char* datas)
{
   String inifile;
  char value[256];
  String tmp="";
  inifile = GetMacAdrr(shortaddr);
  if (inifile!="")
  {
    switch (attribute)
    {
      case 1295:   
        if (ini_exist(inifile))
        {
          
          for(int i=0;i<len;i++)
          {
            sprintf(value, "%02X",datas[i]);
            tmp+=value;
          }
          log_d("0B04 / 1295 = %s",tmp);
          ini_write(inifile,"0B04", (String)attribute, (String)tmp);
          ini_trendPower(inifile, (String)attribute, (String) tmp);
          if (!ini_power2(inifile, (String)attribute, (String) tmp))
          {
            String err ="PB ini_pwer"+inifile+": 0xB04/"+(String)attribute+" "+(String)tmp;
            addDebugLog(err);
          }
          


          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            String tmpvalue;
            tmpvalue = "{\"2820_1295\":";
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            tmpvalue +="}";
            String topic = ConfigGeneral.headerMQTT+ inifile.substring(0, 16)+"_2820_1295/state";
            mqttClient.publish(topic.c_str(), 0, true, tmpvalue.c_str());
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"2820",(String)attribute,tmpvalue.c_str());
          }
          
        }
        break;  
      case 2319:   
        if (ini_exist(inifile))
        {
          log_d("%s",tmp);
          for(int i=0;i<len;i++)
          {
            sprintf(value, "%02X",datas[i]);
            tmp+=value;
          }
          ini_write(inifile,"0B04", (String)attribute, (String)tmp);
          ini_trendPower(inifile, (String)attribute, (String) tmp);
          ini_power2(inifile, (String)attribute, (String) tmp);
          
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            String tmpvalue;
            tmpvalue = "{\"2820_2319\":";
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            tmpvalue +="}";
            String topic = ConfigGeneral.headerMQTT+ inifile.substring(0, 16)+"_2820_2319/state";
            mqttClient.publish(topic.c_str(), 0, true, tmpvalue.c_str());
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"2820",(String)attribute,tmpvalue.c_str());
          }
          
        }
        
        break; 
      case 2575:   
        if (ini_exist(inifile))
        {
          log_d("%s",tmp);
          for(int i=0;i<len;i++)
          {
            sprintf(value, "%02X",datas[i]);
            tmp+=value;
          }
          ini_write(inifile,"0B04", (String)attribute, (String)tmp);
          ini_trendPower(inifile, (String)attribute, (String) tmp);
          ini_power2(inifile, (String)attribute, (String) tmp);
          
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            String tmpvalue;
            tmpvalue = "{\"2820_2575\":";
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            tmpvalue +="}";
            String topic = ConfigGeneral.headerMQTT+ inifile.substring(0, 16)+"_2820_2575/state";
            mqttClient.publish(topic.c_str(), 0, true, tmpvalue.c_str());
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"2820",(String)attribute,tmpvalue.c_str());
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
          ini_write(inifile,"0B04", (String)attribute, (String)tmp);
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            String tmpvalue;
            tmpvalue = "{\"2820_"+(String)attribute+"\":";
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            tmpvalue +="}";
            String topic = ConfigGeneral.headerMQTT+ inifile.substring(0, 16)+"_2820_"+(String)attribute+"/state";
            mqttClient.publish(topic.c_str(), 0, true, tmpvalue.c_str());
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"2820",(String)attribute,tmpvalue.c_str());
          }
        }
        break;        
    }
  }
}
