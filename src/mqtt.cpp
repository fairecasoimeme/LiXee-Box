#include <Arduino.h>
#include "mqtt.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>

extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;

void mqttPublish(String IEEE, String cluster, String attribute, String type, String value)
{
    String tmpvalue;
    if (ConfigGeneral.HAMQTT)
    { 
        tmpvalue = "{\"value_"+cluster+"_"+attribute+"\":";
        if (type=="string")
        {
            tmpvalue += "\""+value+"\"";
        }else if (type=="numeric")
        {
            tmpvalue += String(strtol(value.c_str(), NULL, 16));
        }
        
        tmpvalue +="}";
        String topic = ConfigGeneral.headerMQTT+ IEEE+"_"+cluster+"_"+attribute+"/state";
        mqttClient.publish(topic.c_str(), 0, true, tmpvalue.c_str());
    }else if (ConfigGeneral.TBMQTT)
    {
        tmpvalue ="{\""+IEEE+"\":[";
        tmpvalue +="{";
            tmpvalue+="\""+cluster+"_"+attribute+"\" : ";
            if (type=="string")
            {
                tmpvalue += "\""+value+"\"";
            }else if (type=="numeric")
            {
                tmpvalue += String(strtol(value.c_str(), NULL, 16));
            }
        tmpvalue +="}";
        tmpvalue +="]}";
        mqttClient.publish(ConfigGeneral.headerMQTT, 0, true, tmpvalue.c_str());
    }else if (ConfigGeneral.customMQTT)
    {
        tmpvalue = ConfigGeneral.customMQTTJson;
        tmpvalue.replace("<IEEE>",IEEE);
        tmpvalue.replace("<cluster>",cluster);
        tmpvalue.replace("<attribute>",attribute);
        if (type=="string")
        {
            tmpvalue.replace("<value>","\""+value+"\"");
        }else if (type=="numeric")
        {
            tmpvalue.replace("<value>",String(strtol(value.c_str(), NULL, 16)));
        }

        mqttClient.publish(ConfigGeneral.headerMQTT, 0, true, tmpvalue.c_str());
    }
    vTaskDelay(10);
    
}