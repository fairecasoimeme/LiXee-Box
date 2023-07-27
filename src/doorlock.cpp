#include <Arduino.h>
#include "doorlock.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"

void DoorlockManage(int shortaddr,int attribute,uint8_t datatype,int len, char* datas)
{
  String inifile;
  char value[4];
  String tmp="";
  inifile = GetMacAdrr(shortaddr);
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
        ini_write(inifile,"0101", (String)attribute, (String)tmp);
        //ini_close();
      }
      break;
  }
}
