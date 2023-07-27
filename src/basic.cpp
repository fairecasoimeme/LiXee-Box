#include <Arduino.h>
#include "basic.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"

String GetManufacturer(String inifile)
{
  // ini_open(inifile);
   String tmp= ini_read(inifile,"INFO", "manufacturer");  
   return tmp;
}

String GetModel(String inifile)
{
  // ini_open(inifile);
   String tmp= ini_read(inifile,"INFO", "model");  
   return tmp;
}

void BasicManage(int shortaddr,int attribute,uint8_t datatype,int len, char* datas)
{
  
  String inifile;
  char value[256];
  String tmp="";
  inifile = GetMacAdrr(shortaddr);
  switch (attribute)
  {
    case 4:
      //manufacturer         
      if (ini_exist(inifile))
      {
        char manufacturer[50];
        for(int i=0;i<len;i++)
        {
          manufacturer[i]=datas[i];
        }
        manufacturer[len]='\0';
        ini_write(inifile,"INFO", "manufacturer", (String)manufacturer);
        //ini_close();
      }
      break;       
     case 5:
      //model    
      if (ini_exist(inifile))
      {
        char model[50];
        for(int i=0;i<len;i++)
        {
          model[i]=datas[i];
        }
        model[len]='\0';
       // memcpy(model,datas,len);
        ini_write(inifile,"INFO", "model", (String)model);
        //ini_close();
      }
      break; 
    case 16384:
      //SoftVersion         
      if (ini_exist(inifile))
      {
        char soft[10];
        for(int i=0;i<len;i++)
        {
          soft[i]=datas[i];
        }
        soft[len]='\0';
        ini_write(inifile,"INFO", "software_version", (String)soft);

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
        //ini_close();
      }
      break;
  }
}
