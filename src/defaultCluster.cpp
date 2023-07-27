#include <Arduino.h>
#include "defaultCluster.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"

void defaultClusterManage(int shortaddr,String cluster, int attribute,uint8_t datatype,int len, char* datas)
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
        ini_write(inifile,cluster, (String)attribute, (String)tmp);
        
      }
      break;
  }
}
