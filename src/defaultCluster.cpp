#include <Arduino.h>
#include "defaultCluster.h"
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

void defaultClusterManage(String inifile,int cluster, int attribute,uint8_t datatype,int len, char* datas)
{
  //String inifile;
  char value[4];
  String tmp="";
  //inifile = GetMacAdrr(shortaddr);
  if (inifile!="")
  {
    switch (attribute)
    {       
      default:
        
        for(int i=0;i<len;i++)
        {
          sprintf(value, "%02X",datas[i]);
          tmp+=value;
        }
        char clusterHex[5];
        snprintf(clusterHex,5,"%04X",cluster);
        if (ini_exist(inifile))
        {
        
          //ini_write(inifile,clusterHex, (String)attribute, (String)tmp);

          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            if ((cluster==2817) && (attribute==13))
            {
              String tmpvalue;
              tmpvalue = String(strtol(tmp.c_str(), NULL, 16));
              mqttPublish(inifile.substring(0,16),String(cluster),String(attribute),"numeric",String(tmp));
            }else{
              mqttPublish(inifile.substring(0,16),String(cluster),String(attribute),"string",String(tmp));
            }
            
          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),String(cluster),(String)attribute,tmpvalue.c_str());
          }

          // Device update value;
          if (!deviceList->isFull())
          {
            int shortaddr = GetShortAddr(inifile);
            deviceList->push(Device{shortaddr,cluster,attribute,tmp});
          }
          
        }
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue(std::string(clusterHex),std::string(String(attribute).c_str()),std::string(tmp.c_str()));
            break;
          }
        }
        break;
    }
  }
}
