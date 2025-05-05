#include <Arduino.h>
#include "lixee.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include <WebPush.h>
#include "mqtt.h"
#include "device.h"

extern std::vector<DeviceData*> devices;
extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;
extern CircularBuffer<Device, 10> *deviceList;


void lixeeClusterManage(String inifile,int attribute,uint8_t datatype,int len, char* datas)
{
  //String inifile;
  char value[256];
  String tmp="";
  //inifile = GetMacAdrr(shortaddr);
  
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
      if (ini_exist(inifile))
      {
        //ini_write(inifile,"FF66", (String)attribute, (String)tmp);
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

      for (size_t i = 0; i < devices.size(); i++) 
      {
        DeviceData* device = devices[i];
        if (device->getDeviceID() == inifile.substring(0, 16))
        {
          device->setValue(std::string("FF66"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
          break;
        }
      }
    }    
    break;
    case 519: 

      for(int i=0;i<len;i++)
      {
        sprintf(value, "%02X",datas[i]);
        tmp+=value;
      }

      if (ini_exist(inifile))
      {
        //MQTT
        if (ConfigSettings.enableMqtt)
        {
          mqttPublish(inifile.substring(0,16),"65382",String(attribute),"numeric",String(tmp));
        }
        //WebPush
        if (ConfigSettings.enableWebPush)
        {
          String tmpvalue;
          tmpvalue = String(strtol(tmp.c_str(), NULL, 16));
          WebPush(inifile.substring(0,16),"65382",(String)attribute,tmpvalue.c_str());
        }

        // Device update value;
        if (!deviceList->isFull())
        {
          int shortaddr = GetShortAddr(inifile);
          deviceList->push(Device{shortaddr,65382,attribute,String(strtol(tmp.c_str(), NULL, 16))});
        }
        
      }
      
      for (size_t i = 0; i < devices.size(); i++) 
      {
        DeviceData* device = devices[i];
        if (device->getDeviceID() == inifile.substring(0, 16))
        {
          device->setValue(std::string("FF66"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
          //addMeasurement(device->powerHistory, attribute,strtol(tmp.c_str(), NULL, 16));
          break;
        }
      }

      break;
    case 535:
    {
      
      int size = datas[0];
      char STGE[9];
      for(int i=0;i<size;i++)
      {
        STGE[i]=datas[i+1];
      }
      STGE[size]='\0';
      if (ini_exist(inifile))
      {
        //ini_write(inifile,"FF66", (String)attribute, (String)STGE);
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
          deviceList->push(Device{shortaddr,65382,attribute,STGE});
        }
      }
      for (size_t i = 0; i < devices.size(); i++) 
      {
        DeviceData* device = devices[i];
        if (device->getDeviceID() == inifile.substring(0, 16))
        {
          device->setValue(std::string("FF66"),std::string(String(attribute).c_str()),std::string(STGE));
          break;
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
      //ini_write(inifile,"FF66", (String)attribute, (String)tmp);
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
      for (size_t i = 0; i < devices.size(); i++) 
      {
        DeviceData* device = devices[i];
        if (device->getDeviceID() == inifile.substring(0, 16))
        {
          device->setValue(std::string("FF66"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
          device->setInfoLinkyMode(String(strtol(tmp.c_str(), NULL, 16)));
          break;
        }
      }
    }
    break;
    
    default:
      
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
      if (ini_exist(inifile))
      {
        //ini_write(inifile,"FF66", (String)attribute, (String)tmp);

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
      for (size_t i = 0; i < devices.size(); i++) 
      {
        DeviceData* device = devices[i];
        if (device->getDeviceID() == inifile.substring(0, 16))
        {
          device->setValue(std::string("FF66"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
          break;
        }
      }
        
    break;
  }
  
}
