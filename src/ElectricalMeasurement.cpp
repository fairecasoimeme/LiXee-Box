#include <Arduino.h>
#include "ElectricalMeasurement.h"
#include "config.h"
#include "log.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include <WebPush.h>
#include "mqtt.h"
#include "udpclient.h"

extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;
extern CircularBuffer<Device, 10> *deviceList;

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
            mqttPublish(inifile.substring(0,16),"2820",String(attribute),"numeric",String(tmp));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue = String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"2820",(String)attribute,tmpvalue.c_str());
          }

          //UDP client
          if (ConfigSettings.enableUDP)
          {
            String tmpvalue;
            tmpvalue = String(strtol(tmp.c_str(), NULL, 16));
            UDPsend(tmpvalue);
          }

          // Device update value;
          if (!deviceList->isFull())
          {
            deviceList->push(Device{shortaddr,2820,attribute,String(strtol(tmp.c_str(), NULL, 16))});
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
            mqttPublish(inifile.substring(0,16),"2820",String(attribute),"numeric",String(tmp));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"2820",(String)attribute,tmpvalue.c_str());
          }

          // Device update value;
          if (!deviceList->isFull())
          {
            deviceList->push(Device{shortaddr,2820,attribute,String(strtol(tmp.c_str(), NULL, 16))});
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
            mqttPublish(inifile.substring(0,16),"2820",String(attribute),"numeric",String(tmp));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"2820",(String)attribute,tmpvalue.c_str());
          }

          // Device update value;
          if (!deviceList->isFull())
          {
            deviceList->push(Device{shortaddr,2820,attribute,String(strtol(tmp.c_str(), NULL, 16))});
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
            mqttPublish(inifile.substring(0,16),"2820",String(attribute),"numeric",String(tmp));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"2820",(String)attribute,tmpvalue.c_str());
          }

          // Device update value;
          if (!deviceList->isFull())
          {
            deviceList->push(Device{shortaddr,2820,attribute,String(strtol(tmp.c_str(), NULL, 16))});
          }
        }
        break;        
    }
  }
}
