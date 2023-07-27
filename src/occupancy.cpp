#include <Arduino.h>
#include "occupancy.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"

void OccupancyManage(int shortaddr,int attribute,uint8_t datatype,int len, char* datas)
{
  String inifile;
  char value[50];
  String tmp="";
  inifile = GetMacAdrr(shortaddr);
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
  
        ini_write(inifile,"0406", "0", (String)tmp);
        //ini_close();
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
        ini_write(inifile,"0406", (String)attribute, (String)tmp);
        //ini_close();
      }
      break;
  }
}
