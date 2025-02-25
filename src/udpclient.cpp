#include <Arduino.h>

#include "udpclient.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"
#include <WiFiUdp.h>
#include "AsyncUDP.h"

AsyncUDP Udp;

extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;
extern String epochTime;

void UDPsend(String value)
{
    String tmpvalue;
    
    if (Udp.listen(1010)) {

    }
    
    log_d("send UDP send\n");
    if (ConfigGeneral.customUDPJson!= "")
    {
        tmpvalue = ConfigGeneral.customUDPJson;
        tmpvalue.replace("<date>",epochTime);
        tmpvalue.replace("<value>",value);
        tmpvalue.replace("\r\n","");
        tmpvalue.replace("  ","");
        
        Udp.writeTo((uint8_t *)tmpvalue.c_str(),tmpvalue.length(),parse_ip_address(ConfigGeneral.servUDP),atoi(ConfigGeneral.portUDP));
        log_e("%s\n",tmpvalue.c_str());
    }

    Udp.close();

}