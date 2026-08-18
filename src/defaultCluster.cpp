#include <Arduino.h>
#include "defaultCluster.h"
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
          snprintf(value, sizeof(value), "%02X",datas[i]);
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
            // Issue #37 : les clusters sans handler dedie publiaient TOUJOURS la chaine hexa
            // brute en type "string" (ex. pression SNZB-02M -> "03E2"). Cote Home Assistant,
            // une entite a device_class numerique lit "03E2" comme 3x10^2 = 300 (notation
            // scientifique) au lieu de 994 hPa : valeurs aberrantes et fluctuantes.
            // On publie desormais en "numeric" des que le TEMPLATE declare l'attribut comme tel
            // -- mqttPublish convertit alors l'hexa en decimal, comme le font deja les clusters
            // dedies (cf. power.cpp). Les attributs reellement textuels restent en "string".
            String attrType = "";
            for (size_t k = 0; k < devices.size(); k++) {
              if (devices[k]->getDeviceID() == inifile.substring(0, 16)) {
                attrType = devices[k]->GetAttributeType(cluster, attribute);
                break;
              }
            }
            bool isNumericAttr = (attrType == "numeric" || attrType == "float");

            if (((cluster==2817) && (attribute==13)) || isNumericAttr)
            {
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
          
        }
        for (size_t i = 0; i < devices.size(); i++) 
        {
          DeviceData* device = devices[i];
          if (device->getDeviceID() == inifile.substring(0, 16))
          {
            device->setValue(clusterHex,String(attribute).c_str(),tmp.c_str());

            if (!deviceList->isFull())
            {
              float adjustedValue = strtol(tmp.c_str(), NULL, 16) * device->GetAttributeCoefficient(cluster,attribute);
              deviceList->push(Device{device->getInfo().shortAddr.toInt(),cluster,attribute,String(adjustedValue)});
            }

            break;
          }
        }
        break;
    }
  }
}
