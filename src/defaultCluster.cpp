#include <Arduino.h>
#include "defaultCluster.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include <WebPush.h>

extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;

void defaultClusterManage(int shortaddr,int cluster, int attribute,uint8_t datatype,int len, char* datas)
{
  String inifile;
  char value[4];
  String tmp="";
  inifile = GetMacAdrr(shortaddr);
  if (inifile!="")
  {
    switch (attribute)
    {       
      default:
        if (ini_exist(inifile))
        {
          for(int i=0;i<len;i++)
          {
            sprintf(value, "%02X",datas[i]);
            tmp+=value;
          }
          char clusterHex[5];
          snprintf(clusterHex,5,"%04X",cluster);
          ini_write(inifile,clusterHex, (String)attribute, (String)tmp);

          //MQTT
          if (ConfigSettings.enableMqtt)
          {
            String tmpvalue;
            tmpvalue = "{\""+String(cluster)+"_"+String(attribute)+"\":";
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            tmpvalue +="}";
            String topic = ConfigGeneral.headerMQTT+ inifile.substring(0, 16)+"_"+String(cluster)+"_"+String(attribute)+"/state";
            mqttClient.publish(topic.c_str(), 0, true, tmpvalue.c_str());

          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),String(cluster),(String)attribute,tmpvalue.c_str());
          }
          
        }
        break;
    }
  }
}
