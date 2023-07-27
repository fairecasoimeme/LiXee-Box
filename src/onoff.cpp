#include <Arduino.h>
#include "onoff.h"
#include "config.h"
#include "protocol.h"
#include "SPIFFS_ini.h"

extern CircularBuffer<Packet, 20> commandList;

void SendOnOffAction(int shortaddr, int endpoint, String value)
{
    Packet trame;
    char sA[2];
    sA[0] = shortaddr /256;
    sA[1] = shortaddr % 256;
    trame.cmd=0x0092;
    trame.len=6;
    uint8_t datas[18];
    datas[0]=0x02;
    datas[1]=sA[0];
    datas[2]=sA[1];
    datas[3]= 1;
    datas[4]= endpoint;
    datas[5]= value.toInt();
    
    memcpy(trame.datas,datas,6);
    commandList.push(trame);
}

void OnoffManage(int shortaddr,int attribute,uint8_t datatype,int len, char* datas)
{
  String inifile;
  char value[256];
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
  
        ini_write(inifile,"0006", "0", (String)tmp);
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
        ini_write(inifile,"0000", (String)attribute, (String)tmp);
        //ini_close();
      }
      break;
  }
}
