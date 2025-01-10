#include <Arduino.h>
#include "flash.h"
#include "config.h"

#define ZIGATE_CHIP_ID 0x10408686
#define ZIGATE_FLASH_START  0x00000000
#define ZIGATE_FLASH_END  0x00040000

#define CMD_CHIP_ID 0x32

int ZiGateMode;
extern struct ZiGateInfosStruct ZiGateInfos;

byte CRCcheksum(byte dataString[])
{
  byte xorTemp=dataString[0];
  for(int i = 1; i < (int)dataString[0] ; i++){
    xorTemp ^= dataString[i];
  }
  return xorTemp;
}

void sendCommand(char command, char dataString[])
{
  /*char *tmp;
  strcpy(tmp,command);
  strcpy(tmp,dataString);
  Serial1.write(tmp.size()+1);
  Serial1.write(command);
  Serial1.write(CRCcheksum((tmp.size()+1),dataString);*/
}

void setFlashMode()
{
  ZiGateMode=FLASH;
  digitalWrite(RESET_ZIGATE,0);
  digitalWrite(FLASH_ZIGATE,0);
  digitalWrite(RESET_ZIGATE,1);
  Serial1.end();
  Serial1.begin(38400, SERIAL_8N1, RXD2, TXD2);
}

void setProdMode()
{
  
  digitalWrite(RESET_ZIGATE,0);
  digitalWrite(FLASH_ZIGATE,1);
  digitalWrite(RESET_ZIGATE,1);
  Serial1.end();
  Serial1.begin(115200, SERIAL_8N1, RXD2, TXD2);
  ZiGateMode=PRODUCTION;
}

void ResultTreatment(byte datas[512]){

  byte command;
  //int datasSize;

 // datasSize = (int)datas[0];
  command = (byte)datas[1];
  switch (command) {
    case 0x33:
      
      strcpy(ZiGateInfos.device, (char *)datas);
      log_i("chip_id : %s",ZiGateInfos.device);
      break;
    default:
      break;
  }
}

void check_chip_id()
{
  setFlashMode();
  vTaskDelay(250);
  
  byte tmp;
  byte datas[2];
  datas[0]=0x02;
  datas[1]=CMD_CHIP_ID;
  Serial1.write(datas[0]);
  Serial1.write(datas[1]);
  tmp = CRCcheksum(datas);
  Serial1.write(tmp);
  
}
