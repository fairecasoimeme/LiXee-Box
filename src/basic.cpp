#include <Arduino.h>
#include "basic.h"
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


String GetManufacturer(String inifile)
{
  // ini_open(inifile);
   //String tmp= ini_read(inifile,"INFO", "manufacturer");  
   //return tmp;
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == inifile.substring(0, 16))
    {
      return device->getInfo().manufacturer;
    }
  }
  return String("");
}

String GetModel(String inifile)
{
  // ini_open(inifile);
  // String tmp= ini_read(inifile,"INFO", "model");  
  // return tmp;
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == inifile.substring(0, 16))
    {
      return device->getInfo().model;
    }
  }
  return String("");
}

void BasicManage(String inifile,int attribute,uint8_t datatype,int len, char* datas)
{
  
  //String inifile;
  char value[256];
  String tmp="";
  //inifile = GetMacAdrr(shortaddr);
  if (inifile!="")
  {
    switch (attribute)
    {
      case 4:
        //manufacturer         
        
        char manufacturer[50];
        for(int i=0;i<len;i++)
        {
          manufacturer[i]=datas[i];
        }
        manufacturer[len]='\0';
        if (ini_exist(inifile))
        {
          //ini_write(inifile,"INFO", "manufacturer", (String)manufacturer);
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            mqttPublish(inifile.substring(0,16),"0000",String(attribute),"string",String(manufacturer));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"0000",(String)attribute,tmpvalue.c_str());
          }

         
        }
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setInfoManufacturer(manufacturer);
            break;
          }
        }
        break;       
      case 5:
        //model    
        
        char model[50];
        for(int i=0;i<len;i++)
        {
          model[i]=datas[i];
        }
        model[len]='\0';
        if (ini_exist(inifile))
        {
          //ini_write(inifile,"INFO", "model", (String)model);
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            mqttPublish(inifile.substring(0,16),"0000",String(attribute),"string",String(model));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"0000",(String)attribute,tmpvalue.c_str());
          }
        }
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setInfoModel(model);
            break;
          }
        }
        break; 
      case 1:
        
        for(int i=0;i<len;i++)
        {
          sprintf(value, "%02X",datas[i]);
          tmp+=value;
        }
        if (ini_exist(inifile))
        {
          //ini_write(inifile,"INFO", "software_version", (String)strtol(tmp.c_str(), NULL, 16));
        }
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue(std::string("0000"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
            break;
          }
        }
        break;
      case 16384:
        //SoftVersion  ZLinky        
        
        char soft[10];
        for(int i=0;i<len;i++)
        {
          soft[i]=datas[i];
        }
        soft[len]='\0';
        if (ini_exist(inifile))
        {
          //ini_write(inifile,"INFO", "software_version", (String)soft);
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            mqttPublish(inifile.substring(0,16),"0000",String(attribute),"string",String(soft));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"0000",(String)attribute,tmpvalue.c_str());
          }
        }
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setInfoSoftVersion(soft);
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
          //ini_write(inifile,"0000", (String)attribute, (String)tmp);
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            mqttPublish(inifile.substring(0,16),"0000",String(attribute),"string",String(tmp));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"0000",(String)attribute,tmpvalue.c_str());
          }
        }
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue(std::string("0000"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
            break;
          }
        }
        break;
    }
  }
}
