#include <Arduino.h>
#include "onoff.h"
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

extern CircularBuffer<Packet, 100> *commandList;
extern CircularBuffer<Packet, 10> *PrioritycommandList;
extern CircularBuffer<Device, 50> *deviceList;

void SendOnOffAction(int shortaddr, int endpoint, String value)
{
    Packet trame;
    char sA[2];
    sA[0] = shortaddr /256;
    sA[1] = shortaddr % 256;
    trame.cmd=0x0092;
    trame.len=6;
    uint8_t datas[18];
    datas[0]=0x02;
    datas[1]=sA[0];
    datas[2]=sA[1];
    datas[3]= 1;
    datas[4]= endpoint;
    datas[5]= value.toInt();
    
    memcpy(trame.datas,datas,6);
    PrioritycommandList->push(trame);
}

void OnoffManage(String inifile,int attribute,uint8_t datatype,int len, char* datas)
{
  //String inifile;
  char value[256];
  String tmp="";
  //inifile = GetMacAdrr(shortaddr);
  if (inifile != "")
  {
    switch (attribute)
    {
      case 0:        
        for(int i=0;i<len;i++)
        {
          sprintf(value, "%02X",datas[i]);
          tmp+=value;
        }
        if (ini_exist(inifile))
        {
          //ini_write(inifile,"0006", "0", (String)tmp);
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            mqttPublish(inifile.substring(0,16),"0006",String(attribute),"numeric",String(tmp));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"0006",(String)attribute,tmpvalue.c_str());
          }
          // Device update value;
          if (!deviceList->isFull())
          {
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,6,attribute,String(strtol(tmp.c_str(), NULL, 16))});
          }
        }
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue(std::string("0006"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
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
          //ini_write(inifile,"0006", (String)attribute, (String)tmp);
          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            mqttPublish(inifile.substring(0,16),"0006",String(attribute),"numeric",String(tmp));
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"0006",(String)attribute,tmpvalue.c_str());
          }

          // Device update value;
          if (!deviceList->isFull())
          {
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,6,attribute,String(strtol(tmp.c_str(), NULL, 16))});
          }
        }
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue(std::string("0006"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
            break;
          }
        }
        break;
    }
  }
}
