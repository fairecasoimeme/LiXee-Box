#include <Arduino.h>
#include "occupancy.h"
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
extern CircularBuffer<Device, 50> *deviceList;

void OccupancyManage(String inifile,int attribute,uint8_t datatype,int len, char* datas)
{
  //String inifile;
  char value[50];
  String tmp="";
  //inifile = GetMacAdrr(shortaddr);
  if (inifile !="")
  {
    switch (attribute)
    {
      case 0:
        //manufacturer   
        
          
        for(int i=0;i<len;i++)
        {
          sprintf(value, "%02X",datas[i]);
          tmp+=value;
        }
        if (ini_exist(inifile))
        {
          //ini_write(inifile,"0406", "0", (String)tmp);
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            mqttPublish(inifile.substring(0,16),"1030",String(attribute),"numeric",String(tmp));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"0406",(String)attribute,tmpvalue.c_str());
          }

          // Device update value;
          if (!deviceList->isFull())
          {
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,1030,attribute,String(strtol(tmp.c_str(), NULL, 16))});
          }
        }
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue(std::string("0406"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
            break;
          }
        }
        break;       
      default:
        
        for(int i=0;i<len;i++)
        {
          sprintf(value, "%02X",datas[i]);
          tmp+=value;
        }
        if (ini_exist(inifile))
        {
          //ini_write(inifile,"0406", (String)attribute, (String)tmp);

          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            mqttPublish(inifile.substring(0,16),"1030",String(attribute),"numeric",String(tmp));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"0406",(String)attribute,tmpvalue.c_str());
          }

          // Device update value;
          if (!deviceList->isFull())
          {
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,1030,attribute,String(strtol(tmp.c_str(), NULL, 16))});
          }
        }
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue(std::string("0406"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
            break;
          }
        }
        break;
    }
  }
}
