#include <Arduino.h>
#include "lixee.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"

void lixeeClusterManage(int shortaddr,int attribute,uint8_t datatype,int len, char* datas)
{
  String inifile;
  String tmp="";
  inifile = GetMacAdrr(shortaddr);
  switch (attribute)
  {       
    case 535:
    {
      if (ini_exist(inifile))
      {
        int size = datas[0];
        char STGE[9];
        for(int i=0;i<size;i++)
        {
          STGE[i]=datas[i+1];
        }
        STGE[size]='\0';
        ini_write(inifile,"FF66", (String)attribute, (String)STGE);
      }
    }
    break;
    default:
      if (ini_exist(inifile))
      {
        DEBUG_PRINT(" - datatype:");
        DEBUG_PRINTLN(datatype);
        char value[4];
        if (datatype == 66)
        {
          int size = datas[0];
          for(int i=0;i<size;i++)
          {
            if(datas[i+1]>0)
            {
              tmp+= datas[i+1];
            }
          }
        }else{
          char value[4];
          for(int i=0;i<len;i++)
          {
             sprintf(value, "%02X",datas[i]);
             tmp+=value;
          }
        }
        ini_write(inifile,"FF66", (String)attribute, (String)tmp);
      }
     break;
  }
}
