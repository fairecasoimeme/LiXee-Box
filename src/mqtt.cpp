#include <Arduino.h>
#include "mqtt.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <AsyncMqttClient.h>

extern AsyncMqttClient mqttClient;
extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;

extern unsigned long lastConnectionTest;
extern const unsigned long CONNECTION_TEST_INTERVAL;
extern bool connectionTestPending ;
extern uint16_t lastTestPacketId ;
extern bool reallyConnected ;

void mqttPublish(String IEEE, String cluster, String attribute, String type, String value)
{
    unsigned long now = millis();
    if (mqttClient.connected()) 
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
            mqttClient.publish(topic.c_str(), 1, false, tmpvalue.c_str());
            

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
            mqttClient.publish(ConfigGeneral.headerMQTT, 1, false, tmpvalue.c_str());
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

            mqttClient.publish(ConfigGeneral.headerMQTT, 1, false, tmpvalue.c_str());
        }
        vTaskDelay(10);
    }

    // Timeout du test de connexion
    
    int16_t time = (now - lastConnectionTest);
    if (time > 10000) 
    {
        // Forcer la reconnexion
        mqttClient.disconnect(true);
    }
    
}