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
  Serial2.write(tmp.size()+1);
  Serial2.write(command);
  Serial2.write(CRCcheksum((tmp.size()+1),dataString);*/
}

void setFlashMode()
{
  ZiGateMode=FLASH;
  digitalWrite(RESET_ZIGATE,0);
  digitalWrite(FLASH_ZIGATE,0);
  digitalWrite(RESET_ZIGATE,1);
  Serial2.end();
  Serial2.begin(38400, SERIAL_8N1, RXD2, TXD2);
}

void setProdMode()
{
  
  digitalWrite(RESET_ZIGATE,0);
  digitalWrite(FLASH_ZIGATE,1);
  digitalWrite(RESET_ZIGATE,1);
  Serial2.end();
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);
  ZiGateMode=PRODUCTION;
}

void ResultTreatment(byte datas[256]){

  byte command;
  //int datasSize;

 // datasSize = (int)datas[0];
  command = (byte)datas[1];
  switch (command) {
    case 0x33:
      DEBUG_PRINTLN("chip_id");
      strcpy(ZiGateInfos.device, (char *)datas);
      DEBUG_PRINTLN(ZiGateInfos.device);
      break;
    default:
      break;
  }
}

void check_chip_id()
{
  setFlashMode();
  delay(250);
  
  byte tmp;
  byte datas[2];
  datas[0]=0x02;
  datas[1]=CMD_CHIP_ID;
  Serial2.write(datas[0]);
  Serial2.write(datas[1]);
  tmp = CRCcheksum(datas);
  Serial2.write(tmp);
  
}
