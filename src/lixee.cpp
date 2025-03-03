#include <Arduino.h>
#include "lixee.h"
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


void lixeeClusterManage(String inifile,int attribute,uint8_t datatype,int len, char* datas)
{
  //String inifile;
  String tmp="";
  //inifile = GetMacAdrr(shortaddr);
  if (inifile !="")
  {
    switch (attribute)
    {   
      case 514:
      {
        String tmp="";
        for(int i=0;i<(len-1);i++)
        {
          if(datas[i+1]>0)
          {
            tmp+= datas[i+1];
          }
        }
        ini_write(inifile,"FF66", (String)attribute, (String)tmp);
        //MQTT
        if (ConfigSettings.enableMqtt)
        {
          mqttPublish(inifile.substring(0,16),"65382",String(attribute),"string",String(tmp));
        }
      //WebPush
        if (ConfigSettings.enableWebPush)
        {
          WebPush(inifile.substring(0,16),"65382",(String)attribute,String(tmp));
        }

        // Device update value;
          if (!deviceList->isFull())
          {
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,65382,attribute,String(tmp)});
          }
      } 
      break;
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
            mqttPublish(inifile.substring(0,16),"65382",String(attribute),"string",String(STGE));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"65382",(String)attribute,tmpvalue.c_str());
          }

          // Device update value;
          if (!deviceList->isFull())
          {
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,65382,attribute,tmp});
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
          mqttPublish(inifile.substring(0,16),"65382",String(attribute),"numeric",String(tmp));
        }
      //WebPush
        if (ConfigSettings.enableWebPush)
        {
          String tmpvalue;
          tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
          WebPush(inifile.substring(0,16),"65382",(String)attribute,tmpvalue.c_str());
        }

        // Device update value;
          if (!deviceList->isFull())
          {
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,65382,attribute,String(strtol(tmp.c_str(), NULL, 16))});
          }
      }
      break;
     
      default:
        if (ini_exist(inifile))
        {
          log_d(" - datatype: %d",datatype);
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
            if (datatype == 66)
            {
              mqttPublish(inifile.substring(0,16),"65382",String(attribute),"numeric",String(tmp));
            }else{
              mqttPublish(inifile.substring(0,16),"65382",String(attribute),"string",String(tmp));
            }
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"65382",(String)attribute,tmpvalue.c_str());
          }

          // Device update value;
          if (!deviceList->isFull())
          {
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,65382,attribute,String(strtol(tmp.c_str(), NULL, 16))});
          }
        }
      break;
    }
  }
}
