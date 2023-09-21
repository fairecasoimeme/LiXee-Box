#include <Arduino.h>
#include "ElectricalMeasurement.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"

void ElectricalMeasurementManage(int shortaddr,int attribute,uint8_t datatype,int len, char* datas)
{
   String inifile;
  char value[256];
  String tmp="";
  inifile = GetMacAdrr(shortaddr);
  switch (attribute)
  {
    case 1295:   
      if (ini_exist(inifile))
      {
        DEBUG_PRINTLN(tmp);
        for(int i=0;i<len;i++)
        {
           sprintf(value, "%02X",datas[i]);
           tmp+=value;
        }
        ini_write(inifile,"0B04", (String)attribute, (String)tmp);
        ini_power(inifile, (String)attribute, (String) tmp);
        ini_trendPower(inifile, (String)attribute, (String) tmp);
      }
      break;  
    case 2319:   
      if (ini_exist(inifile))
      {
        DEBUG_PRINTLN(tmp);
        for(int i=0;i<len;i++)
        {
           sprintf(value, "%02X",datas[i]);
           tmp+=value;
        }
        ini_write(inifile,"0B04", (String)attribute, (String)tmp);
        ini_power(inifile, (String)attribute, (String) tmp);
        ini_trendPower(inifile, (String)attribute, (String) tmp);
      }
      break; 
    case 2575:   
      if (ini_exist(inifile))
      {
        DEBUG_PRINTLN(tmp);
        for(int i=0;i<len;i++)
        {
           sprintf(value, "%02X",datas[i]);
           tmp+=value;
        }
        ini_write(inifile,"0B04", (String)attribute, (String)tmp);
        ini_power(inifile, (String)attribute, (String) tmp);
        ini_trendPower(inifile, (String)attribute, (String) tmp);
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
        ini_write(inifile,"0B04", (String)attribute, (String)tmp);
      }
      break;        
  }
}
