#include <Arduino.h>
#include "lixee.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>

extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;


void lixeeClusterManage(int shortaddr,int attribute,uint8_t datatype,int len, char* datas)
{
  String inifile;
  String tmp="";
  inifile = GetMacAdrr(shortaddr);
  if (inifile !="")
  {
    switch (attribute)
    {       
      case 535:
      {
        if (ini_exist(inifile))
        {
          int size = datas[0];
          char STGE[9];
          for(int i=0;i<size;i++)
          {
            STGE[i]=datas[i+1];
          }
          STGE[size]='\0';
          ini_write(inifile,"FF66", (String)attribute, (String)STGE);
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            String tmpvalue;
            tmpvalue = "{\"value_65382_"+String(attribute)+"\":";
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            tmpvalue +="}";
            String topic = ConfigGeneral.headerMQTT+ inifile.substring(0, 16)+"_65382_"+String(attribute)+"/state";
            mqttClient.publish(topic.c_str(), 0, true, tmpvalue.c_str());

          }
        }
      }
      break;
      case 768:
      {
        char value[4];
        for(int i=0;i<len;i++)
        {
            sprintf(value, "%02X",datas[i]);
            tmp+=value;
        }
        ConfigGeneral.LinkyMode = tmp.toInt();
        ini_write(inifile,"FF66", (String)attribute, (String)tmp);
        //MQTT
        if (ConfigSettings.enableMqtt)
        {
          String tmpvalue;
          tmpvalue = "{\"value_65382_"+String(attribute)+"\":";
          tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
          tmpvalue +="}";
          String topic = ConfigGeneral.headerMQTT+ inifile.substring(0, 16)+"_65382_"+String(attribute)+"/state";
          mqttClient.publish(topic.c_str(), 0, true, tmpvalue.c_str());

        }
      }
      break;
      default:
        if (ini_exist(inifile))
        {
          DEBUG_PRINT(" - datatype:");
          DEBUG_PRINTLN(datatype);
          if (datatype == 66)
          {
            int size = datas[0];
            for(int i=0;i<size;i++)
            {
              if(datas[i+1]>0)
              {
                tmp+= datas[i+1];
              }
            }
          }else{
            char value[4];
            for(int i=0;i<len;i++)
            {
              sprintf(value, "%02X",datas[i]);
              tmp+=value;
            }
          }
          ini_write(inifile,"FF66", (String)attribute, (String)tmp);

          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            String tmpvalue;
            tmpvalue = "{\"value_65382_"+String(attribute)+"\":";
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            tmpvalue +="}";
            String topic = ConfigGeneral.headerMQTT+ inifile.substring(0, 16)+"_65382_"+String(attribute)+"/state";
            mqttClient.publish(topic.c_str(), 0, true, tmpvalue.c_str());

          }
        }
      break;
    }
  }
}
