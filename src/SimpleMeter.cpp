#include <Arduino.h>
#include "SimpleMeter.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"

void SimpleMeterManage(int shortaddr,int attribute,uint8_t datatype,int len, char* datas)
{
   String inifile;
  char value[256];
  String tmp="";
  inifile = GetMacAdrr(shortaddr);
  switch (attribute)
  {
    case 0:   
    case 256:
    case 258:
      if (ini_exist(inifile))
      {
        for(int i=0;i<len;i++)
        {
           sprintf(value, "%02X",datas[i]);
           tmp+=value;
        }
        ini_write(inifile,"0702", (String)attribute, (String)tmp);
        ini_energy("energy_"+inifile, (String)attribute, (String) tmp);
        ini_trendEnergy(inifile, (String)attribute, (String) tmp);
      }
      break; 
    case 32:
      if (ini_exist(inifile))
      {
        String tmp;
        for(int i=0;i<len;i++)
        {
          if(datas[i]>0)
          {
            tmp+= datas[i];
          }
        }
        ini_write(inifile,"0702", (String)attribute, (String)tmp);
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
        ini_write(inifile,"0702", (String)attribute, (String)tmp);
      }
      break;      
  }
}
