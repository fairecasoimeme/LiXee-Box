#include <Arduino.h>
#include "SimpleMeter.h"
#include "config.h"
#include "log.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include <WebPush.h>
#include "mqtt.h"
#include "device.h"
#include "energyHistory.h"

extern std::vector<DeviceData*> devices;

extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;

extern CircularBuffer<Device, 10> *deviceList;

void SimpleMeterManage(String inifile,int attribute,uint8_t datatype,int len, char* datas)
{
  //String inifile;
  char value[256];
  String tmp="";
  //inifile = GetMacAdrr(shortaddr);
  
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
    {
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
          mqttPublish(inifile.substring(0,16),"1794",String(attribute),"numeric",String(tmp));
        }
        //WebPush
        if (ConfigSettings.enableWebPush)
        {
          String tmpvalue;
          tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
          WebPush(inifile.substring(0,16),"1794",(String)attribute,tmpvalue.c_str());
        }

        // Device update value;
        if (!deviceList->isFull())
        {
          int shortaddr = GetShortAddr(inifile);
          deviceList->push(Device{shortaddr,1794,attribute,String(strtol(tmp.c_str(), NULL, 16))});
        }
        
        //ini_trendEnergy(inifile, (String)attribute, (String) tmp);
      }

      for (size_t i = 0; i < devices.size(); i++) 
      {
        DeviceData* device = devices[i];
        if (device->getDeviceID() == inifile.substring(0, 16))
        {
          if ((device->getInfo().model=="ZLinky_TIC") && (attribute == 0))
          {
            device->setValue(std::string("0702"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
          }else{
            device->setValue(std::string("0702"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
            addEnergyMeasurement(device->energyHistory,String(attribute),strtol(tmp.c_str(), NULL, 16));
            for (uint8_t i = 0; i < 11; ++i) {
              if (attribute == INDEX_ID_LIST[i]) {
                device->updateIndex(i, strtol(tmp.c_str(), NULL, 16));
                break;
              }
            }
          }
          
          break;
        }
      }
      break; 
    }
    case 1: 
      
      for(int i=0;i<len;i++)
      {
        sprintf(value, "%02X",datas[i]);
        tmp+=value;
      }
      tmp="-"+tmp;
      if (ini_exist(inifile))
      {
        //ini_write(inifile,"0702", (String)attribute, (String)tmp);
        //MQTT
        if (ConfigSettings.enableMqtt)
        {
          mqttPublish(inifile.substring(0,16),"1794",String(attribute),"numeric",String(tmp));
        }
        //WebPush
        if (ConfigSettings.enableWebPush)
        {
          String tmpvalue;
          tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
          WebPush(inifile.substring(0,16),"1794",(String)attribute,tmpvalue.c_str());
        }

        // Device update value;
        if (!deviceList->isFull())
        {
          int shortaddr = GetShortAddr(inifile);
          deviceList->push(Device{shortaddr,1794,attribute,String(strtol(tmp.c_str(), NULL, 16))});
        }
        /*ini_energy(inifile, (String)attribute, (String) tmp);
        ini_trendEnergy(inifile, (String)attribute, (String) tmp);*/
      }
      for (size_t i = 0; i < devices.size(); i++) 
      {
        DeviceData* device = devices[i];
        if (device->getDeviceID() == inifile.substring(0, 16))
        {
          device->setValue(std::string("0702"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
          addEnergyMeasurement(device->energyHistory,String(attribute),strtol(tmp.c_str(), NULL, 16));   
          for (uint8_t i = 0; i < 11; ++i) {
            if (attribute == INDEX_ID_LIST[i]) {
              device->updateIndex(i, strtol(tmp.c_str(), NULL, 16));
              break;
            }
          }
          break;
        }
      }
      break; 
    case 32:
      {
        String tmp;
        for(int i=0;i<len;i++)
        {
          if(datas[i]>0)
          {
            tmp+= datas[i];
          }
        }
        if (ini_exist(inifile))
        {
          //ini_write(inifile,"0702", (String)attribute, (String)tmp);
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            mqttPublish(inifile.substring(0,16),"1794",String(attribute),"numeric",String(tmp));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"1794",(String)attribute,tmpvalue.c_str());
          }

          // Device update value;
          if (!deviceList->isFull())
          {
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,1794,attribute,String(strtol(tmp.c_str(), NULL, 16))});
          }
        }
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue(std::string("0702"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
            break;
          }
        }
        break;
      }
      case 776:
      {
        String tmp;
        for(int i=0;i<len;i++)
        {
          if(datas[i]>0)
          {
            tmp+= datas[i];
          }
        }
        if (ini_exist(inifile))
        {
          //ini_write(inifile,"0702", (String)attribute, (String)tmp);
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            mqttPublish(inifile.substring(0,16),"1794",String(attribute),"string",String(tmp));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {

            WebPush(inifile.substring(0,16),"1794",(String)attribute,String(tmp));
          }

          // Device update value;
          if (!deviceList->isFull())
          {
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,1794,attribute,String(tmp)});
          }
        }
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue(std::string("0702"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
            break;
          }
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
        //ini_write(inifile,"0702", (String)attribute, (String)tmp);
        //MQTT
        if (ConfigSettings.enableMqtt)
        {
          mqttPublish(inifile.substring(0,16),"1794",String(attribute),"numeric",String(tmp));
        }
        //WebPush
        if (ConfigSettings.enableWebPush)
        {
          String tmpvalue;
          tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
          WebPush(inifile.substring(0,16),"1794",(String)attribute,tmpvalue.c_str());
        }

        // Device update value;
        if (!deviceList->isFull())
        {
          int shortaddr = GetShortAddr(inifile);
          deviceList->push(Device{shortaddr,1794,attribute,String(strtol(tmp.c_str(), NULL, 16))});
        }
      }
      for (size_t i = 0; i < devices.size(); i++) 
      {
        DeviceData* device = devices[i];
        if (device->getDeviceID() == inifile.substring(0, 16))
        {
          device->setValue(std::string("0702"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
          break;
        }
      }
      break;      
  }
  
}
