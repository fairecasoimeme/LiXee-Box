#include <Arduino.h>
#include "humidity.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include <WebPush.h>
#include "mqtt.h"
#include "device.h"

extern DeviceList devices;
extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;
extern CircularBuffer<Device, 50> *deviceList;

void humidityManage(String inifile,int attribute,uint8_t datatype,int len, char* datas)
{
  //String inifile;
  char value[4];
  String tmp="";
  //inifile = GetMacAdrr(shortaddr);
  if (inifile !="")
  {
    switch (attribute)
    {   
      case 0:
        
        for(int i=0;i<len;i++)
        {
          snprintf(value, sizeof(value), "%02X",datas[i]);
          tmp+=value;
        }
        if (ini_exist(inifile))
        {
          //ini_write(inifile,"1029", (String)attribute, (String)tmp);
          
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            mqttPublish(inifile.substring(0,16),"1029",String(attribute),"numeric",String(tmp));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"1029",(String)attribute,tmpvalue.c_str());
          }

        }
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue("0405",String(attribute).c_str(),tmp.c_str());

            if (!deviceList->isFull())
            {
              float adjustedValue = strtol(tmp.c_str(), NULL, 16) * device->GetAttributeCoefficient(1029,attribute);
              deviceList->push(Device{device->getInfo().shortAddr.toInt(),1029,attribute,String(adjustedValue)});
            }


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
          //ini_write(inifile,"1029", (String)attribute, (String)tmp);
          
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            mqttPublish(inifile.substring(0,16),"1029",String(attribute),"string",String(tmp));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"1029",(String)attribute,tmpvalue.c_str());
          }

          // Device update value (avec coefficient pour l'affichage web)
          if (!deviceList->isFull())
          {
            int shortaddr = GetShortAddr(inifile);
            DeviceData* dev = nullptr;
            for (size_t j = 0; j < devices.size(); j++) {
              if (devices[j]->getDeviceID() == inifile.substring(0, 16)) { dev = devices[j]; break; }
            }
            float coeff = (dev != nullptr) ? dev->GetAttributeCoefficient(1029, attribute) : 1.0;
            float adjustedValue = strtol(tmp.c_str(), NULL, 16) * coeff;
            deviceList->push(Device{shortaddr,1029,attribute,String(adjustedValue)});
          }
        }
        for (size_t i = 0; i < devices.size(); i++)
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue("0405",String(attribute).c_str(),tmp.c_str());
            break;
          }
        }
        break;
    }
  }
}
