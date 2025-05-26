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
#include "device.h"
#include "powerHistory.h"

extern std::vector<DeviceData*> devices;
extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigNotification ConfigNotif;
extern ConfigSettingsStruct ConfigSettings;
extern CircularBuffer<Device, 50> *deviceList;
extern CircularBuffer<Notification, 10> *notifList;

float oldPowerOutage;

void ElectricalMeasurementManage(String inifile,int attribute,uint8_t datatype,int len, char* datas)
{
  //String inifile;
  char value[256];
  String tmp="";
  //inifile = GetMacAdrr(shortaddr);
  if (inifile!="")
  {
    switch (attribute)
    {
      case 1295:  
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
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,2820,attribute,String(strtol(tmp.c_str(), NULL, 16))});
          }
          
        }
        
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue(std::string("0B04"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
            addMeasurement(device->powerHistory, attribute,strtol(tmp.c_str(), NULL, 16));

            //Notification
            if (ConfigNotif.PowerOutage && (strcmp(ConfigGeneral.ZLinky,inifile.substring(0,16).c_str()) == 0 ))
            {
              if ((strtol(tmp.c_str(), NULL, 16)==0) && (oldPowerOutage != 0))
              {
                if (!notifList->isFull())
                {
                  notifList->push(Notification{"Coupure électrique ?","La puissance apparente de "+String(ConfigGeneral.ZLinky)+" est à 0 VA",FormattedDate,1});
                }else{
                  notifList->shift();
                  notifList->push(Notification{"Coupure électrique ?","La puissance apparente de "+String(ConfigGeneral.ZLinky)+" est à 0 VA",FormattedDate,1});
                }    
              }
              oldPowerOutage = strtol(tmp.c_str(), NULL, 16);
            }
            
            break;
          }
        }

        break;  
      case 2319:   
        
        for(int i=0;i<len;i++)
        {
          sprintf(value, "%02X",datas[i]);
          tmp+=value;
        }
        if (ini_exist(inifile))
        {
          //ini_write(inifile,"0B04", (String)attribute, (String)tmp);
          /*ini_trendPower(inifile, (String)attribute, (String) tmp);
          ini_power2(inifile, (String)attribute, (String) tmp);*/
          
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
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,2820,attribute,String(strtol(tmp.c_str(), NULL, 16))});
          }
          
        }

        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue(std::string("0B04"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
            addMeasurement(device->powerHistory, attribute,strtol(tmp.c_str(), NULL, 16));
            break;
          }
        }
        
        break; 
      case 2575:   
        
        for(int i=0;i<len;i++)
        {
          sprintf(value, "%02X",datas[i]);
          tmp+=value;
        }
        if (ini_exist(inifile))
        {
          //ini_write(inifile,"0B04", (String)attribute, (String)tmp);
          /*ini_trendPower(inifile, (String)attribute, (String) tmp);
          ini_power2(inifile, (String)attribute, (String) tmp);*/
          
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
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,2820,attribute,String(strtol(tmp.c_str(), NULL, 16))});
            
          }
          
        }

        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue(std::string("0B04"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
            addMeasurement(device->powerHistory, attribute,strtol(tmp.c_str(), NULL, 16));
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
          //ini_write(inifile,"0B04", (String)attribute, (String)tmp);
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
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,2820,attribute,String(strtol(tmp.c_str(), NULL, 16))});
          }
        }
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue(std::string("0B04"),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
            break;
          }
        }
        break;        
    }
  }
}
