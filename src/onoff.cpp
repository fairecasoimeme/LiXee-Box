#include <Arduino.h>
#include "onoff.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>
#include <WebPush.h>

extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;

extern CircularBuffer<Packet, 100> *commandList;
extern CircularBuffer<Packet, 10> *PrioritycommandList;

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
    //commandList->push(trame);
    PrioritycommandList->push(trame);
}

void OnoffManage(int shortaddr,int attribute,uint8_t datatype,int len, char* datas)
{
  String inifile;
  char value[256];
  String tmp="";
  inifile = GetMacAdrr(shortaddr);
  if (inifile != "")
  {
    switch (attribute)
    {
      case 0:
        //manufacturer   
        
        if (ini_exist(inifile))
        {
          for(int i=0;i<len;i++)
          {
            sprintf(value, "%02X",datas[i]);
            tmp+=value;
          }
    
          ini_write(inifile,"0006", "0", (String)tmp);
          if (ConfigSettings.enableMqtt)
          {
            String tmpvalue;
            tmpvalue = "{\"0006_"+String(attribute)+"\":";
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            tmpvalue +="}";
            String topic = ConfigGeneral.headerMQTT+ inifile.substring(0, 16)+"_0006_"+String(attribute)+"/state";
            mqttClient.publish(topic.c_str(), 0, true, tmpvalue.c_str());

          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"0006",(String)attribute,tmpvalue.c_str());
          }
        }
        break;       
      default:
        if (ini_exist(inifile))
        {
          for(int i=0;i<len;i++)
          {
            sprintf(value, "%02X",datas[i]);
            tmp+=value;
          }
          ini_write(inifile,"0000", (String)attribute, (String)tmp);
          if (ConfigSettings.enableMqtt)
          {
            String tmpvalue;
            tmpvalue = "{\"0006_"+String(attribute)+"\":";
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            tmpvalue +="}";
            String topic = ConfigGeneral.headerMQTT+ inifile.substring(0, 16)+"_0006_"+String(attribute)+"/state";
            mqttClient.publish(topic.c_str(), 0, true, tmpvalue.c_str());

          }
          //WebPush
          if (ConfigSettings.enableWebPush)
          {
            String tmpvalue;
            tmpvalue += String(strtol(tmp.c_str(), NULL, 16));
            WebPush(inifile.substring(0,16),"0006",(String)attribute,tmpvalue.c_str());
          }
        }
        break;
    }
  }
}
