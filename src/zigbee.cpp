#include <Arduino.h>
#include <esp_task_wdt.h>
#define ARDUINOJSON_USE_LONG_LONG 1
#define ARDUINOJSON_SLOT_ID_SIZE 2
#include <ArduinoJson.h>
#include "FS.h"
#include "LittleFS.h"
#include "SPIFFS_ini.h"
#include "protocol.h"
#include "config.h"
#include "basic.h"
#include "onoff.h"
#include "occupancy.h"
#include "doorlock.h"
#include "defaultCluster.h"
#include "lixee.h"
#include "SimpleMeter.h"
#include "ElectricalMeasurement.h"
#include "temperature.h"
#include "humidity.h"
#include "power.h"
#include "web.h"
#include "device.h"
#include "powerHistory.h"

extern std::vector<DeviceData*> devices;

extern struct ZigbeeConfig ZConfig;
extern struct ConfigGeneralStruct ConfigGeneral;


extern CircularBuffer<Packet, 100> *commandList;
//extern CircularBuffer<Packet, 10> commandTimedList;
extern String Day;
extern String Month;
extern String Yesterday;
extern String Year;
extern String Hour;
extern String Minute;

extern String section[12];

void SendActiveRequest(uint8_t shortAddr[2])
{
  Packet trame;
   
  trame.cmd=0x0045;
  trame.len=0x0002;
  memcpy(trame.datas,shortAddr,2);
  DEBUG_PRINTLN(F("Active Req")); 
  commandList->push(trame);
}

void  SendAction(int command, int ShortAddr, String tmpValue)
{
  switch (command)
    {
      case 146 :
        SendOnOffAction(ShortAddr,1,tmpValue);
      break;
      default:
      break;
    }
}
  
void SendSimpleDescriptionRequest(uint8_t shortAddr[2], uint8_t endpoint)
{
  
}

void SendDeleteDevice(uint64_t mac)
{
  Packet trame;
  trame.cmd=0x004c;
  trame.len=10;
  uint8_t datas[trame.len];
  datas[0]= (mac >> 56)& 0xFF;
  datas[1]= (mac >> 48)& 0xFF;
  datas[2]= (mac >> 40)& 0xFF;
  datas[3]= (mac >> 32)& 0xFF;
  datas[4]= (mac >> 24)& 0xFF;
  datas[5]= (mac >> 16)& 0xFF;
  datas[6]= (mac >> 8) &0xFF;
  datas[7]= mac & 0xFF;
  datas[8]=0;
  datas[9]=0; 

  memcpy(trame.datas,datas,trame.len);
  commandList->push(trame);
}

void SendBind(uint64_t mac, int cluster)
{
  Packet trame;
  trame.cmd=0x0030;
  trame.len=21;
  uint8_t datas[trame.len];
  datas[0]= (mac >> 56)& 0xFF;
  datas[1]= (mac >> 48)& 0xFF;
  datas[2]= (mac >> 40)& 0xFF;
  datas[3]= (mac >> 32)& 0xFF;
  datas[4]= (mac >> 24)& 0xFF;
  datas[5]= (mac >> 16)& 0xFF;
  datas[6]= (mac >> 8) &0xFF;
  datas[7]= mac & 0xFF;
  datas[8]=1;
  datas[9]= (cluster >>8) & 0xFF;
  datas[10]= cluster & 0xFF ;
  datas[11]=3;
  datas[12]=(ZConfig.zigbeeMac >> 56)& 0xFF;
  datas[13]=(ZConfig.zigbeeMac >> 48)& 0xFF;
  datas[14]=(ZConfig.zigbeeMac >> 40)& 0xFF;
  datas[15]=(ZConfig.zigbeeMac >> 32)& 0xFF;
  datas[16]=(ZConfig.zigbeeMac >> 24)& 0xFF;
  datas[17]=(ZConfig.zigbeeMac >> 16)& 0xFF;
  datas[18]=(ZConfig.zigbeeMac >> 8)& 0xFF;
  datas[19]=ZConfig.zigbeeMac & 0xFF;
  datas[20]=1;

  memcpy(trame.datas,datas,trame.len);
  commandList->push(trame);
}

void SendBasicDescription(uint8_t shortAddr[2], uint8_t endpoint)
{
    Packet trame;
    trame.cmd=0x0100;
    trame.len=20;
    uint8_t datas[trame.len];
    datas[0]=0x02;
    datas[1]=shortAddr[0];
    datas[2]=shortAddr[1];
    datas[3]= endpoint;
    datas[4]= 0x01;
    datas[5]= 0x00;
    datas[6]= 0x00;
    datas[7]= 0x00;
    datas[8]= 0x00;
    datas[9]= 0x00;
    datas[10]= 0x00;
    datas[11]= 0x04;
    datas[12]= 0x00;
    datas[13]= 0x04;
    datas[14]= 0x00;
    datas[15]= 0x05;
    datas[16]= 0x00;
    datas[17]= 0x01;
    datas[18]= 0x40;
    datas[19]= 0x00;
    memcpy(trame.datas,datas,trame.len);
    commandList->push(trame);
}

void SendConfigReport(uint8_t shortAddr[2], int cluster, int attribut, int type, int rmin, int rmax, int rtimeout, uint8_t rchange)
{
    Packet trame;
    trame.cmd=0x0120;
    if (type==0x25)
    {
      trame.len=28;
    }else if (type==0x21)
    {
      trame.len=24;
    }else{
      trame.len=23;
    }
    uint8_t datas[trame.len];
    datas[0]=0x02;
    datas[1]=shortAddr[0];
    datas[2]=shortAddr[1];
    datas[3]= 0x01;
    datas[4]= 0x01;
    datas[5]= (cluster >>8) & 0xFF;
    datas[6]= cluster & 0xFF ;
    datas[7]= 0x00;
    datas[8]= 0x00;
    datas[9]= 0x00;
    datas[10]= 0x00;
    datas[11]= 0x01;
    datas[12]= 0x00;
    datas[13]= type;
    datas[14]= (attribut >>8) & 0xFF;
    datas[15]= attribut & 0xFF ;
    datas[16]= (rmin >>8) & 0xFF;
    datas[17]= rmin & 0xFF ;
    datas[18]= (rmax >>8) & 0xFF;
    datas[19]= rmax & 0xFF ;
    datas[20]= (rtimeout >>8) & 0xFF;
    datas[21]= rtimeout & 0xFF ;
    if (type==0x25)
    {
      datas[22]= 0x00;
      datas[23]= 0x00;
      datas[24]= 0x00;
      datas[25]= 0x00;
      datas[26]= 0x00;
      datas[27]= rchange;
    }else if (type==0x21)
    {
      datas[22]= 0x00;
      datas[23]= rchange;
    }else{
      
      datas[22]= rchange ;
    }
    
    memcpy(trame.datas,datas,trame.len);
    commandList->push(trame);
}

void SendAttributeRead(int shortAddr, int cluster, int attribut)
{
   Packet trame;
    trame.cmd=0x0100;
    trame.len=14;
    uint8_t datas[trame.len];
    datas[0]=0x02;
    datas[1]= (shortAddr >>8) & 0xFF;
    datas[2]= shortAddr & 0xFF ;
    datas[3]= 0x01;
    datas[4]= 0x01;
    datas[5]= (cluster >>8) & 0xFF;
    datas[6]= cluster & 0xFF ;
    datas[7]= 0x00;
    datas[8]= 0x00;
    datas[9]= 0x00;
    datas[10]= 0x00;
    datas[11]= 0x01;
    datas[12]= (attribut >>8) & 0xFF;
    datas[13]= attribut & 0xFF ;
    
    memcpy(trame.datas,datas,trame.len);
    commandList->push(trame);
}

void SpecificTreatment(uint8_t shortAddr[2], String model)
{
  if (model == "ZLinky_TIC")
  {
    
    int SA = shortAddr[0]*256+shortAddr[1];
    String tmpMac = GetMacAdrr(SA).substring(0,16);
    strlcpy(ConfigGeneral.ZLinky, tmpMac.c_str(), sizeof(ConfigGeneral.ZLinky));
    config_write("configGeneral.json", "ZLinky",tmpMac);
    //save configGeneral

    //Linky mode
    SendAttributeRead(SA, 65382, 768);
    //Serial number
    SendAttributeRead(SA, 1794, 776);
    //Current tarif
    SendAttributeRead(SA, 65382, 512);
    SendAttributeRead(SA, 1794, 32);
    //OPTARIF / NGTF
    SendAttributeRead(SA, 65382, 0);
    //Puissance souscrite
    SendAttributeRead(SA, 2817, 13);
    
  }
}

String getLinkyMode(int mode)
{
  switch(mode)
  {
    case 0: 
    {
      return "Mode historique monophasé";
    }
    break;
    case 1: 
    {
      return "Mode standard monophasé";
    }
    break;
    case 2: 
    {
      return "Mode historique triphasé";
    }
    break;
    case 3: 
    {
      return "Mode standard  triphasé";
    }
    break;
    case 5: 
    {
      return "Mode standard monophasé producteur";
    }
    break;
    case 7: 
    {
      return "Mode standard triphasé  producteur";
    }
    break;
    default:
    {
      return "---";
    }
  }
}

float getTarif(int attribute, String Type)
{
  float tarif=0;
  if (Type == "energy")
  {
    switch (attribute) 
    {
      case 0:
        tarif=atof(ConfigGeneral.tarifIdx1);
      break; 
      case 256:
        tarif=atof(ConfigGeneral.tarifIdx2);
      break; 
      case 258:
        tarif=atof(ConfigGeneral.tarifIdx3);
      break; 
      case 260:
        tarif=atof(ConfigGeneral.tarifIdx4);
      break; 
      case 262:
        tarif=atof(ConfigGeneral.tarifIdx5);
      break; 
      case 264:
        tarif=atof(ConfigGeneral.tarifIdx6);
      break; 
      case 266:
        tarif=atof(ConfigGeneral.tarifIdx7);
      break; 
      case 268:
        tarif=atof(ConfigGeneral.tarifIdx8);
      break; 
      case 270:
        tarif=atof(ConfigGeneral.tarifIdx9);
      break; 
      case 272:
        tarif=atof(ConfigGeneral.tarifIdx10);
      break;
      default:
        tarif=0;
      break;
      
    }
  }else if (Type=="gaz")
  {
    tarif=atof(ConfigGeneral.tarifGaz);
  }else if (Type=="water")
  {
    tarif=atof(ConfigGeneral.tarifWater);
  }else if (Type=="production")
  {
    tarif=atof(ConfigGeneral.tarifIdxProd);
  }
  return tarif;
}

float getTarifPower(String IEEE, int power)
{
  DeviceEnergyHistory hist;
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == IEEE)
    {
      hist = device->energyHistory;
      break;
    }
  }

  long sum=0;
  for (const auto &graphEntry : hist.days.graph) {
    const PsString &Key = graphEntry.first;
    const ValueMap &valMap   = graphEntry.second;

    
    if (Day.c_str() == Key.c_str())
    {
      for (const auto &attrPair : valMap.attributes) {
          int attrId = attrPair.first;
          long attrVal = attrPair.second;
          sum += (attrVal*getTarif(attrId,"energy"))/1000 + ((attrVal/1000) * atof(ConfigGeneral.tarifCSPE));
      }
      break;
    }
  }  
  return sum;
}

String getLinkyDatas(String IEEE)
{
  String result,tmp; 
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == IEEE)
    {
      result+="<div id='datasLinky' style='display:inline-block;float:left;'>";
      tmp = String(strtol(device->getValue(std::string("0702"),std::string("776")).c_str(),0,16));
      if (tmp != "")
      {
        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+=F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-upc-scan' viewBox='0 0 16 16'>");
        result+=F("<path d='M1.5 1a.5.5 0 0 0-.5.5v3a.5.5 0 0 1-1 0v-3A1.5 1.5 0 0 1 1.5 0h3a.5.5 0 0 1 0 1zM11 .5a.5.5 0 0 1 .5-.5h3A1.5 1.5 0 0 1 16 1.5v3a.5.5 0 0 1-1 0v-3a.5.5 0 0 0-.5-.5h-3a.5.5 0 0 1-.5-.5M.5 11a.5.5 0 0 1 .5.5v3a.5.5 0 0 0 .5.5h3a.5.5 0 0 1 0 1h-3A1.5 1.5 0 0 1 0 14.5v-3a.5.5 0 0 1 .5-.5m15 0a.5.5 0 0 1 .5.5v3a1.5 1.5 0 0 1-1.5 1.5h-3a.5.5 0 0 1 0-1h3a.5.5 0 0 0 .5-.5v-3a.5.5 0 0 1 .5-.5M3 4.5a.5.5 0 0 1 1 0v7a.5.5 0 0 1-1 0zm2 0a.5.5 0 0 1 1 0v7a.5.5 0 0 1-1 0zm2 0a.5.5 0 0 1 1 0v7a.5.5 0 0 1-1 0zm2 0a.5.5 0 0 1 .5-.5h1a.5.5 0 0 1 .5.5v7a.5.5 0 0 1-.5.5h-1a.5.5 0 0 1-.5-.5zm3 0a.5.5 0 0 1 1 0v7a.5.5 0 0 1-1 0z'/>");
        result+="</svg><br><strong>"+tmp+"</strong><br><i style='font-size:12px;'>(Serial Number)</i></span>";
      }
      tmp = String(strtol(device->getValue(std::string("FF66"),std::string("768")).c_str(),0,16));
      if (tmp !="")
      {
        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+=F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-box-seam' viewBox='0 0 16 16'>");
        result+=F("<path d='M8.186 1.113a.5.5 0 0 0-.372 0L1.846 3.5l2.404.961L10.404 2zm3.564 1.426L5.596 5 8 5.961 14.154 3.5zm3.25 1.7-6.5 2.6v7.922l6.5-2.6V4.24zM7.5 14.762V6.838L1 4.239v7.923zM7.443.184a1.5 1.5 0 0 1 1.114 0l7.129 2.852A.5.5 0 0 1 16 3.5v8.662a1 1 0 0 1-.629.928l-7.185 2.874a.5.5 0 0 1-.372 0L.63 13.09a1 1 0 0 1-.63-.928V3.5a.5.5 0 0 1 .314-.464z'/>");
        result+="</svg><br><strong>"+getLinkyMode(tmp.toInt())+"</strong></span>";
      }
      tmp = String(device->getValue(std::string("FF66"),std::string("0")));
      if (tmp!="")
      {      
        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+tmp+"</strong><br><i style='font-size:12px;'>(Subscription)</i></span>";
      }
      tmp = String(device->getValue(std::string("FF66"),std::string("16")));
      if (tmp!="")
      {      
        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+tmp+"</strong><br><i style='font-size:12px;'>(Tariff Period)</i></span>";
      }
      tmp = String(device->getValue(std::string("FF66"),std::string("512")));
      if (tmp!="")
      {
        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F(" <svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-clock-history' viewBox='0 0 16 16'>");
        result+= F("<path d='M8.515 1.019A7 7 0 0 0 8 1V0a8 8 0 0 1 .589.022zm2.004.45a7 7 0 0 0-.985-.299l.219-.976q.576.129 1.126.342zm1.37.71a7 7 0 0 0-.439-.27l.493-.87a8 8 0 0 1 .979.654l-.615.789a7 7 0 0 0-.418-.302zm1.834 1.79a7 7 0 0 0-.653-.796l.724-.69q.406.429.747.91zm.744 1.352a7 7 0 0 0-.214-.468l.893-.45a8 8 0 0 1 .45 1.088l-.95.313a7 7 0 0 0-.179-.483m.53 2.507a7 7 0 0 0-.1-1.025l.985-.17q.1.58.116 1.17zm-.131 1.538q.05-.254.081-.51l.993.123a8 8 0 0 1-.23 1.155l-.964-.267q.069-.247.12-.501m-.952 2.379q.276-.436.486-.908l.914.405q-.24.54-.555 1.038zm-.964 1.205q.183-.183.35-.378l.758.653a8 8 0 0 1-.401.432z'/>");
        result+= F("<path d='M8 1a7 7 0 1 0 4.95 11.95l.707.707A8.001 8.001 0 1 1 8 0z'/>");
        result+= F("<path d='M7.5 3a.5.5 0 0 1 .5.5v5.21l3.248 1.856a.5.5 0 0 1-.496.868l-3.5-2A.5.5 0 0 1 7 9V3.5a.5.5 0 0 1 .5-.5'/>");      
        result+= "</svg><br><strong>"+tmp+"</strong><br><i style='font-size:12px;'>(Current price)</i></span>";
      }
      tmp = String(device->getValue(std::string("FF66"),std::string("32")));
      if (tmp!="")
      {
        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F(" <svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-clock-history' viewBox='0 0 16 16'>");
        result+= F("<path d='M8.515 1.019A7 7 0 0 0 8 1V0a8 8 0 0 1 .589.022zm2.004.45a7 7 0 0 0-.985-.299l.219-.976q.576.129 1.126.342zm1.37.71a7 7 0 0 0-.439-.27l.493-.87a8 8 0 0 1 .979.654l-.615.789a7 7 0 0 0-.418-.302zm1.834 1.79a7 7 0 0 0-.653-.796l.724-.69q.406.429.747.91zm.744 1.352a7 7 0 0 0-.214-.468l.893-.45a8 8 0 0 1 .45 1.088l-.95.313a7 7 0 0 0-.179-.483m.53 2.507a7 7 0 0 0-.1-1.025l.985-.17q.1.58.116 1.17zm-.131 1.538q.05-.254.081-.51l.993.123a8 8 0 0 1-.23 1.155l-.964-.267q.069-.247.12-.501m-.952 2.379q.276-.436.486-.908l.914.405q-.24.54-.555 1.038zm-.964 1.205q.183-.183.35-.378l.758.653a8 8 0 0 1-.401.432z'/>");
        result+= F("<path d='M8 1a7 7 0 1 0 4.95 11.95l.707.707A8.001 8.001 0 1 1 8 0z'/>");
        result+= F("<path d='M7.5 3a.5.5 0 0 1 .5.5v5.21l3.248 1.856a.5.5 0 0 1-.496.868l-3.5-2A.5.5 0 0 1 7 9V3.5a.5.5 0 0 1 .5-.5'/>");      
        result+= "</svg><br><strong>"+tmp+"</strong><br><i style='font-size:12px;'>(Current price)</i></span>";
      }
      tmp = String(strtol(device->getValue(std::string("0B01"),std::string("13")).c_str(),0,16));
      if (tmp!="")
      {
        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-lightning-charge-fill' viewBox='0 0 16 16'>");
        result+= F("<path d='M11.251.068a.5.5 0 0 1 .227.58L9.677 6.5H13a.5.5 0 0 1 .364.843l-8 8.5a.5.5 0 0 1-.842-.49L6.323 9.5H3a.5.5 0 0 1-.364-.843l8-8.5a.5.5 0 0 1 .615-.09z'/>");
        int modeTmp = strtol(device->getValue(std::string("FF66"),std::string("768")).c_str(),0,16);
        if ((modeTmp==0) || (modeTmp==2))
        {
          result+= "</svg><br><strong>"+tmp+" A</strong><br><i style='font-size:12px;'>(Subscribed intensity)</i></span>";
        }else{
          result+= "</svg><br><strong>"+tmp+" kVA</strong><br><i style='font-size:12px;'>(Subscribed power)</i></span>";
        }
        
      }
      tmp = String(device->getValue(std::string("FF66"),std::string("535")));
      if (tmp!="")
      {
        auto status = parseStatusRegister(tmp);

        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+status.contact_sec+"</strong><br><i style='font-size:12px;'>(Contact sec)</i></span>";

        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+status.organe_coupure+"</strong><br><i style='font-size:12px;'>(Organe coupure)</i></span>";

        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+status.cache_borne_dist +"</strong><br><i style='font-size:12px;'>(Cache borne distributeur)</i></span>";

        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+String(status.surtension_phase) +"</strong><br><i style='font-size:12px;'>(Surtension sur phase)</i></span>";

        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+String(status.depassement_ref_pow)+"</strong><br><i style='font-size:12px;'>(Dépassement puissance)</i></span>";

        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+String(status.producteur) +"</strong><br><i style='font-size:12px;'>(Mode producteur)</i></span>";

        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+status.sens_energie_active +"</strong><br><i style='font-size:12px;'>(Sens énergie active)</i></span>";

        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+status.tarif_four +"</strong><br><i style='font-size:12px;'>(Tarif fournisseur)</i></span>";

        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+status.tarif_dist +"</strong><br><i style='font-size:12px;'>(Tarif distributeur)</i></span>";

        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+status.horloge +"</strong><br><i style='font-size:12px;'>(Mode horloge)</i></span>";

        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+status.type_tic +"</strong><br><i style='font-size:12px;'>(Type TIC)</i></span>";

        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+status.comm_euridis +"</strong><br><i style='font-size:12px;'>(Communicateur Euridis)</i></span>";

        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+status.sync_cpl  +"</strong><br><i style='font-size:12px;'>(Synchro CPL)</i></span>";

        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+status.tempo_jour  +"</strong><br><i style='font-size:12px;'>(Couleur jour)</i></span>";

        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+status.tempo_demain +"</strong><br><i style='font-size:12px;'>(Couleur lendemain)</i></span>";

        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+status.preavis_pointe_mobile  +"</strong><br><i style='font-size:12px;'>(Préavis pointe mobile)</i></span>";

        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
        result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
        result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
        result+= "</svg><br><strong>"+status.pointe_mobile  +"</strong><br><i style='font-size:12px;'>(pointe mobile)</i></span>";
      }

      tmp = String(strtol(device->getValue(std::string("0702"),std::string("1")).c_str(),0,16));
      if (tmp!="")
      {
        if (tmp.toInt() != 0)
        {
          result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
          result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-speedometer' viewBox='0 0 16 16'>");
          result+= F("<path d='M8 2a.5.5 0 0 1 .5.5V4a.5.5 0 0 1-1 0V2.5A.5.5 0 0 1 8 2M3.732 3.732a.5.5 0 0 1 .707 0l.915.914a.5.5 0 1 1-.708.708l-.914-.915a.5.5 0 0 1 0-.707M2 8a.5.5 0 0 1 .5-.5h1.586a.5.5 0 0 1 0 1H2.5A.5.5 0 0 1 2 8m9.5 0a.5.5 0 0 1 .5-.5h1.5a.5.5 0 0 1 0 1H12a.5.5 0 0 1-.5-.5m.754-4.246a.39.39 0 0 0-.527-.02L7.547 7.31A.91.91 0 1 0 8.85 8.569l3.434-4.297a.39.39 0 0 0-.029-.518z'/>");
          result+= F("<path fill-rule='evenodd' d='M6.664 15.889A8 8 0 1 1 9.336.11a8 8 0 0 1-2.672 15.78zm-4.665-4.283A11.95 11.95 0 0 1 8 10c2.186 0 4.236.585 6.001 1.606a7 7 0 1 0-12.002 0'/>");
          result+= "</svg><br><strong>"+tmp+" Wh </strong><br><i style='font-size:12px;'>(Index 1)</i></span>";
        }
      }
      tmp = String(strtol(device->getValue(std::string("0702"),std::string("256")).c_str(),0,16));
      if (tmp!="")
      {
        if (tmp.toInt() > 0)
        {
          result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
          result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-speedometer' viewBox='0 0 16 16'>");
          result+= F("<path d='M8 2a.5.5 0 0 1 .5.5V4a.5.5 0 0 1-1 0V2.5A.5.5 0 0 1 8 2M3.732 3.732a.5.5 0 0 1 .707 0l.915.914a.5.5 0 1 1-.708.708l-.914-.915a.5.5 0 0 1 0-.707M2 8a.5.5 0 0 1 .5-.5h1.586a.5.5 0 0 1 0 1H2.5A.5.5 0 0 1 2 8m9.5 0a.5.5 0 0 1 .5-.5h1.5a.5.5 0 0 1 0 1H12a.5.5 0 0 1-.5-.5m.754-4.246a.39.39 0 0 0-.527-.02L7.547 7.31A.91.91 0 1 0 8.85 8.569l3.434-4.297a.39.39 0 0 0-.029-.518z'/>");
          result+= F("<path fill-rule='evenodd' d='M6.664 15.889A8 8 0 1 1 9.336.11a8 8 0 0 1-2.672 15.78zm-4.665-4.283A11.95 11.95 0 0 1 8 10c2.186 0 4.236.585 6.001 1.606a7 7 0 1 0-12.002 0'/>");
          result+= "</svg><br><strong>"+tmp+" Wh </strong><br><i style='font-size:12px;'>(Index 1)</i></span>";
        }
      }
      tmp = String(strtol(device->getValue(std::string("0702"),std::string("258")).c_str(),0,16));
      if (tmp!="")
      {
        if (tmp.toInt() > 0)
        {
            result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
            result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-speedometer' viewBox='0 0 16 16'>");
            result+= F("<path d='M8 2a.5.5 0 0 1 .5.5V4a.5.5 0 0 1-1 0V2.5A.5.5 0 0 1 8 2M3.732 3.732a.5.5 0 0 1 .707 0l.915.914a.5.5 0 1 1-.708.708l-.914-.915a.5.5 0 0 1 0-.707M2 8a.5.5 0 0 1 .5-.5h1.586a.5.5 0 0 1 0 1H2.5A.5.5 0 0 1 2 8m9.5 0a.5.5 0 0 1 .5-.5h1.5a.5.5 0 0 1 0 1H12a.5.5 0 0 1-.5-.5m.754-4.246a.39.39 0 0 0-.527-.02L7.547 7.31A.91.91 0 1 0 8.85 8.569l3.434-4.297a.39.39 0 0 0-.029-.518z'/>");
            result+= F("<path fill-rule='evenodd' d='M6.664 15.889A8 8 0 1 1 9.336.11a8 8 0 0 1-2.672 15.78zm-4.665-4.283A11.95 11.95 0 0 1 8 10c2.186 0 4.236.585 6.001 1.606a7 7 0 1 0-12.002 0'/>");
            result+= "</svg><br><strong>"+tmp+" Wh </strong><br><i style='font-size:12px;'>(Index 2)</i></span>";
        }
      }
      tmp = String(strtol(device->getValue(std::string("0702"),std::string("260")).c_str(),0,16));
      if (tmp!="")
      {
        if (tmp.toInt() > 0)
          result+="<br><strong>Index (BBRHCJW/EASF03): </strong>"+tmp+" Wh <span id='index4'></span>";
      }
      tmp = String(strtol(device->getValue(std::string("0702"),std::string("262")).c_str(),0,16));
      if (tmp!="")
      {
        if (tmp.toInt() > 0)
          result+="<br><strong>Index (BBRHPJW/EASF04): </strong>"+tmp+" Wh <span id='index5'></span>";
      }
      tmp = String(strtol(device->getValue(std::string("0702"),std::string("264")).c_str(),0,16));
      if (tmp!="")
      {
        if (tmp.toInt() > 0)
          result+="<br><strong>Index (BBRHCJR/EASF05): </strong>"+tmp+" Wh <span id='index6'></span>";
      }
      tmp = String(strtol(device->getValue(std::string("0702"),std::string("266")).c_str(),0,16));
      if (tmp!="")
      {
        if (tmp.toInt() > 0)
          result+="<br><strong>Index (BBRHPJR/EASF06): </strong>"+tmp+" Wh <span id='index7'></span>";
      }
      tmp = String(strtol(device->getValue(std::string("0702"),std::string("268")).c_str(),0,16));
      if (tmp!="")
      {
        if (tmp.toInt() > 0)
          result+="<br><strong>Index (EASF07): </strong>"+tmp+" Wh <span id='index8'></span>";
      }
      tmp = String(strtol(device->getValue(std::string("0702"),std::string("270")).c_str(),0,16));
      if (tmp!="")
      {
        if (tmp.toInt() > 0)
          result+="<br><strong>Index (EASF08): </strong>"+tmp+" Wh <span id='index9'></span>";
      }
      tmp = String(strtol(device->getValue(std::string("0702"),std::string("272")).c_str(),0,16));
      if (tmp!="")
      {
        if (tmp.toInt() > 0)
          result+="<br><strong>Index (EASF09): </strong>"+tmp+" Wh <span id='index10'></span>";
      }
      tmp = String(strtol(device->getValue(std::string("0702"),std::string("274")).c_str(),0,16));
      if (tmp!="")
      {
        if (tmp.toInt() > 0)
          result+="<br><strong>Index (EASF10): </strong>"+tmp+" Wh <span id='index11'></span>";
      }
      result+="</div>";
      return result;
    }
  }

  return "";

}

StatusRegisterBreakout parseStatusRegister(const String& hexVal) {
  // Conversion de la chaîne hexadécimale en entier non signé 32 bits
  unsigned long valhex = strtoul(hexVal.c_str(), nullptr, 16);

  StatusRegisterBreakout s;

  // bit 0 → contact sec
  s.contact_sec = (valhex & 0x1) ? "Ouvert" : "Fermé";

  // bits 1–3 → organe de coupure
  switch ((valhex >> 1) & 0x7) {
    case 0: s.organe_coupure = "Fermé"; break;
    case 1: s.organe_coupure = "Surpuissance"; break;
    case 2: s.organe_coupure = "Surtension"; break;
    case 3: s.organe_coupure = "Delestage"; break;
    case 4: s.organe_coupure = "Ordre_CPL_Euridis"; break;
    case 5: s.organe_coupure = "Surchauffe_surcourant"; break;
    case 6: s.organe_coupure = "Surchauffe_simple"; break;
    default: s.organe_coupure = "Inconnu"; break;
  }

  // bit 4 → état cache borne distributeur (0 = fermé, 1 = ouvert)
  s.cache_borne_dist = ((valhex >> 4) & 0x1) == 0 ? "Fermé" : "Ouvert";

  // bit 6 → surtension sur une des phases
  s.surtension_phase    = (valhex >> 6) & 0x1;
  // bit 7 → dépassement puissance de référence
  s.depassement_ref_pow = (valhex >> 7) & 0x1;
  // bit 8 → consommateur/producteur
  s.producteur          = ((valhex >> 8) & 0x1) == 0 ? "Consommateur" : "Producteur";

  // bit 9 → sens de l'énergie active
  s.sens_energie_active = ((valhex >> 9) & 0x1) == 0 ? "Positive" : "Negative";

  // bits 10–13 → tarif en cours (fourniture)
  s.tarif_four = "index_" + String( ((valhex >> 10) & 0xF) + 1 );

  // bits 14–15 → tarif en cours (distributeur)
  s.tarif_dist = "index_" + String( ((valhex >> 14) & 0x3) + 1 );

  // bit 16 → mode dégradé de l'horloge
  s.horloge = ((valhex >> 16) & 0x1) == 0 ? "Correcte" : "Dégradée";

  // bit 17 → type TIC
  s.type_tic = ((valhex >> 17) & 0x1) == 0 ? "Historique" : "Standard";

  // bits 19–20 → état sortie communicateur Euridis
  switch ((valhex >> 19) & 0x3) {
    case 0: s.comm_euridis = "Désactivée"; break;
    case 1: s.comm_euridis = "Activée sans sécurité"; break;
    case 3: s.comm_euridis = "Activée avec sécurité"; break;
    default: s.comm_euridis = "Inconnu"; break;
  }

  // bits 21–22 → état CPL
  switch ((valhex >> 21) & 0x3) {
    case 0: s.etat_cpl = "Nouveau déverrouillé"; break;
    case 1: s.etat_cpl = "Nouveau vérrouillé";  break;
    case 2: s.etat_cpl = "Enregistré";           break;
    default: s.etat_cpl = "Inconnu";            break;
  }

  // bit 23 → synchronisation CPL
  s.sync_cpl = ((valhex >> 23) & 0x1) == 0 ? "Non synchronisé" : "Synchronisé";

  // bits 24–25 → couleur du jour (Tempo)
  switch ((valhex >> 24) & 0x3) {
    case 1: s.tempo_jour = "BLEU";  break;
    case 2: s.tempo_jour = "BLANC"; break;
    case 3: s.tempo_jour = "ROUGE"; break;
    default: s.tempo_jour = "UNDEF"; break;
  }

  // bits 26–27 → couleur demain (Tempo)
  switch ((valhex >> 26) & 0x3) {
    case 1: s.tempo_demain = "BLEU";  break;
    case 2: s.tempo_demain = "BLANC"; break;
    case 3: s.tempo_demain = "ROUGE"; break;
    default: s.tempo_demain = "UNDEF"; break;
  }

  // bits 28–29 → préavis pointe mobile
  switch ((valhex >> 28) & 0x3) {
    case 1: s.preavis_pointe_mobile = "PM1"; break;
    case 2: s.preavis_pointe_mobile = "PM2"; break;
    case 3: s.preavis_pointe_mobile = "PM3"; break;
    default: s.preavis_pointe_mobile = "AUCUN"; break;
  }

  // bits 30–31 → pointe mobile
  switch ((valhex >> 30) & 0x3) {
    case 1: s.pointe_mobile = "PM1"; break;
    case 2: s.pointe_mobile = "PM2"; break;
    case 3: s.pointe_mobile = "PM3"; break;
    default: s.pointe_mobile = "AUCUN"; break;
  }

  return s;
}

String getPowerGaugeAbo(String IEEE, String Attribute, String Time)
{
  String result="error";
  
  if (Time=="hour")
  {
    for (size_t i = 0; i < devices.size(); i++) 
    {
      DeviceData* device = devices[i];
      if (device->getDeviceID() == IEEE)
      {
        if (Attribute != "519")
        {
          result = String(strtol(device->getValue(std::string("0B04"),std::string(String(Attribute).c_str())).c_str(),0,16));
          result += ";";
          result += String(strtol(device->getValue(std::string("0B01"),std::string("13")).c_str(),0,16)*230);
          result += ";";
          result += device->getPowerW();
        }else{
          result = String(strtol(device->getValue(std::string("FF66"),std::string(String(Attribute).c_str())).c_str(),0,16));
          result += ";";
          result += String(strtol(device->getValue(std::string("0B01"),std::string("14")).c_str(),0,16)*1000);
        }
        return result;
      }
    }

  }else
  {
    DeviceEnergyHistory hist;
    String mode;
    for (size_t i = 0; i < devices.size(); i++) 
    {
      DeviceData* device = devices[i];
      if (Attribute != "519")
      {
        mode="consumption";
        if (device->getDeviceID() == IEEE)
        {
          hist = device->energyHistory;
          break;
        }
      }else{
        mode="production";
        if (device->getDeviceID() == ConfigGeneral.Production)
        {
          hist = device->energyHistory;
          break;
        }

      }
      
    }

    long int tmp=0;
    long int maxVal =0;
    long int minVal = 0;
    if (Time=="day")
    {
      for (const auto &graphEntry : hist.days.graph) {
        const PsString &Key = graphEntry.first;
        const ValueMap &valMap   = graphEntry.second;

        long sum=0;
        for (const auto &attrPair : valMap.attributes) {
            long attrVal = attrPair.second;
            if (mode == "production")
            {
              if (attrVal < 0) sum +=attrVal;
            }else{
              if (attrVal > 0) sum +=attrVal;
              
            }
            
        }
        maxVal = max(sum,maxVal);
        minVal = min(sum,minVal);
        if (memcmp(Day.c_str(),Key.c_str(),2)==0)
        {
          tmp=sum;
        }
      }  

      if (tmp > 0)
      {
        result= String(tmp)+";"+String(maxVal);
        return result;
      }else if (tmp < 0)
      {
        result= String(tmp)+";"+String(minVal);
        return result;
      }     

    }else if (Time=="month")
    {
      for (const auto &graphEntry : hist.months.graph) {
        const PsString &Key = graphEntry.first;
        const ValueMap &valMap   = graphEntry.second;

        long sum=0;
        for (const auto &attrPair : valMap.attributes) {
            long attrVal = attrPair.second;
            if (mode =="production")
            {
              if (attrVal < 0) sum +=attrVal;
            }else{
              if (attrVal > 0) sum +=attrVal;
            }
        }
        maxVal = max(sum,maxVal);
        minVal = min(sum,minVal);
        if (memcmp(Month.c_str(),Key.c_str(),2)==0)
        {
          tmp=sum;
        }

      }  
         
      if (tmp > 0)
      {
        result= String(tmp)+";"+String(maxVal);
        return result;
      }else if (tmp < 0)
      {
        result= String(tmp)+";"+String(minVal);
        return result;
      }
    }else if (Time=="year")
    {
      for (const auto &graphEntry : hist.years.graph) {
        const PsString &Key = graphEntry.first;
        const ValueMap &valMap   = graphEntry.second;
        long sum=0;
        for (const auto &attrPair : valMap.attributes) {
            long attrVal = attrPair.second;
            if (mode =="production")
            {
              if (attrVal < 0) sum +=attrVal;
            }else{
              if (attrVal > 0) sum +=attrVal;
            }
        }
        maxVal = max(sum,maxVal);
        minVal = min(sum,minVal);
        if (memcmp(Year.c_str(),Key.c_str(),4)==0)
        {
          tmp=sum;
        }

      }  
         
      if (tmp > 0)
      {
        result= String(tmp)+";"+String(maxVal);
        return result;
      }else if (tmp < 0)
      {
        result= String(tmp)+";"+String(minVal);
        return result;
      }
    }
  }
  return result;
}

String getDatasPower(String IEEE,String Attribute, String Time)
{
  // Trouver le device
  DeviceData* dev = nullptr;
  for (auto* d : devices) {
    if (d->getDeviceID() == IEEE) { dev = d; break; }
  }
  if (!dev) {
    return "";
  }

  // Sélectionner la période
  DeviceEnergyHistory& eh = dev->energyHistory;
  PeriodData* pd = nullptr;
  if      (Time=="hour")  pd=&eh.hours;
  else if (Time=="day")   pd=&eh.days;
  else if (Time=="month") pd=&eh.months;
  else if (Time=="year")  pd=&eh.years;
  else                      return "";

  int arrayLength = sizeof(section) / sizeof(section[0]);

  long TotalWh =0;
  float TotalEuros = 0;
  // Calcul de la somme par section
  std::map<int, long> sums;
  std::map<int, int> attrib;
  for (auto &kv : pd->graph) {
    ValueMap &vm = kv.second;   
    for (size_t i = 2; i < arrayLength; ++i) {
      int attrId = section[i].toInt();
      auto itv = vm.attributes.find(attrId);
      if (itv != vm.attributes.end()) {       
        sums[(i-1)] += itv->second;
        TotalWh += itv->second;
        attrib[(i-1)] = attrId;
      }
    }
  }

  //Production
  long sumProd=0;
  if ((strcmp(ConfigGeneral.Production,"")!=0) && (strcmp(ConfigGeneral.Production,dev->getDeviceID().c_str())!=0))
  {
    DeviceData* devProd = nullptr;
    for (auto* d : devices) {
      if (d->getDeviceID() == ConfigGeneral.Production) { devProd = d; break; }
    }

    DeviceEnergyHistory& ehProd = devProd->energyHistory;
    PeriodData* pdProd = nullptr;
    if      (Time=="hour")  pdProd=&ehProd.hours;
    else if (Time=="day")   pdProd=&ehProd.days;
    else if (Time=="month") pdProd=&ehProd.months;
    else if (Time=="year")  pdProd=&ehProd.years;
    else                      return "";

    
    for (auto &kv : pdProd->graph) {
      ValueMap &vm = kv.second;   
      int attrId = 1;
      auto itv = vm.attributes.find(attrId);
      if (itv != vm.attributes.end()) {       
        sumProd += itv->second;
      }
    }

  }

  //GAZ
  long sumGaz=0;
  if ((strcmp(ConfigGeneral.Gaz,"")!=0))
  {
    DeviceData* devGaz = nullptr;
    for (auto* d : devices) {
      if (d->getDeviceID() == ConfigGeneral.Gaz) { devGaz = d; break; }
    }

    DeviceEnergyHistory& ehGaz = devGaz->energyHistory;
    PeriodData* pdGaz = nullptr;
    if      (Time=="hour")  pdGaz=&ehGaz.hours;
    else if (Time=="day")   pdGaz=&ehGaz.days;
    else if (Time=="month") pdGaz=&ehGaz.months;
    else if (Time=="year")  pdGaz=&ehGaz.years;
    else                      return "";

    
    for (auto &kv : pdGaz->graph) {
      ValueMap &vm = kv.second;   
      int attrId = 0;
      auto itv = vm.attributes.find(attrId);
      if (itv != vm.attributes.end()) {       
        sumGaz += itv->second * ConfigGeneral.coeffGaz;
      }
    }

  }



  String color[9] = { "#d35400","#2980b9","#154360","#7f8c8d","#000000","#e74c3c","#c0392b","#f5b041","#145a32"};
  String result = "<h5>Legend</h5>";
  for (auto &p : sums) {
    result += "<div class='row'><div class='col-1'><div style=\"border:1px solid grey;width:10px;height:15px;background-color:";
    if (p.first <= 10)
    {
      result += color[p.first];
    }
    result +="\"></div></div><div class='col-11'> ";
    result += "<svg fill='#000000' style='width:16px;' width='24px' height='24px' viewBox='-3.2 -3.2 38.40 38.40' version='1.1' xmlns='http://www.w3.org/2000/svg' stroke='#000000'><g id='SVGRepo_bgCarrier' stroke-width='0'></g><g id='SVGRepo_tracerCarrier' stroke-linecap='round' stroke-linejoin='round' stroke='#CCCCCC' stroke-width='0.384'></g><g id='SVGRepo_iconCarrier'> <path d='M18.605 2.022v0zM18.605 2.022l-2.256 11.856 8.174 0.027-11.127 16.072 2.257-13.043-8.174-0.029zM18.606 0.023c-0.054 0-0.108 0.002-0.161 0.006-0.353 0.028-0.587 0.147-0.864 0.333-0.154 0.102-0.295 0.228-0.419 0.373-0.037 0.043-0.071 0.088-0.103 0.134l-11.207 14.832c-0.442 0.607-0.508 1.407-0.168 2.076s1.026 1.093 1.779 1.099l5.773 0.042-1.815 10.694c-0.172 0.919 0.318 1.835 1.18 2.204 0.257 0.11 0.527 0.163 0.793 0.163 0.629 0 1.145-0.294 1.533-0.825l11.22-16.072c0.442-0.607 0.507-1.408 0.168-2.076-0.34-0.669-1.026-1.093-1.779-1.098l-5.773-0.010 1.796-9.402c0.038-0.151 0.057-0.308 0.057-0.47 0-1.082-0.861-1.964-1.939-1.999-0.024-0.001-0.047-0.001-0.071-0.001v0z'></path> </g></svg> ";
    result += String(p.second);
    result += " Wh ";
    result+=" <svg style='width:16px;' width='24px' height='24px' viewBox='0 0 1024 1024' class='icon' version='1.1' xmlns='http://www.w3.org/2000/svg' fill='#000000'>";
    result+="<g id='SVGRepo_bgCarrier' stroke-width='0'/>";
    result+="<g id='SVGRepo_tracerCarrier' stroke-linecap='round' stroke-linejoin='round'/>";
    result+="<g id='SVGRepo_iconCarrier'>";
    result+="<path d='M951.87 253.86c0-82.18-110.05-144.14-256-144.14s-256 61.96-256 144.14c0 0.73 0.16 1.42 0.18 2.14h-0.18v109.71h73.14v-9.06c45.77 25.81 109.81 41.33 182.86 41.33 67.39 0 126.93-13.33 171.71-35.64 6.94 7.18 11.15 14.32 11.15 20.58 0 28.25-72.93 70.98-182.86 70.98h-73.12v73.14h73.12c67.4 0 126.96-13.33 171.74-35.65 6.95 7.17 11.11 14.31 11.11 20.6 0 28.27-72.93 71-182.86 71l-25.89 0.12c-15.91 0.14-31.32 0.29-46.34-0.11l-1.79 73.11c8.04 0.2 16.18 0.27 24.48 0.27 7.93 0 16-0.05 24.2-0.12l25.34-0.12c67.44 0 127.02-13.35 171.81-35.69 6.97 7.23 11.04 14.41 11.04 20.62 0 28.27-72.93 71-182.86 71h-73.12v73.14h73.12c67.44 0 127.01-13.35 171.81-35.69 6.98 7.22 11.05 14.4 11.05 20.62 0 28.27-72.93 71-182.86 71h-73.12v73.14h73.12c145.95 0 256-61.96 256-144.14 0-0.68-0.09-1.45-0.11-2.14h0.11V256h-0.18c0.03-0.72 0.2-1.42 0.2-2.14z m-438.86 0c0-28.27 72.93-71 182.86-71s182.86 42.73 182.86 71c0 28.25-72.93 70.98-182.86 70.98s-182.86-42.73-182.86-70.98z' fill='currentColor'/>";
    result+="<path d='M330.15 365.71c-145.95 0-256 61.96-256 144.14 0 0.73 0.16 1.42 0.18 2.14h-0.18v256c0 82.18 110.05 144.14 256 144.14s256-61.96 256-144.14V512h-0.18c0.02-0.72 0.18-1.42 0.18-2.14 0-82.18-110.05-144.15-256-144.15zM147.29 638.93c0-6.32 4.13-13.45 11.08-20.62 44.79 22.33 104.36 35.67 171.78 35.67 67.39 0 126.93-13.33 171.71-35.64 6.94 7.18 11.15 14.32 11.15 20.58 0 28.25-72.93 70.98-182.86 70.98s-182.86-42.72-182.86-70.97z m182.86-200.07c109.93 0 182.86 42.73 182.86 71 0 28.25-72.93 70.98-182.86 70.98s-182.86-42.73-182.86-70.98c0-28.27 72.93-71 182.86-71z m0 400.14c-109.93 0-182.86-42.73-182.86-71 0-6.29 4.17-13.43 11.11-20.6 44.79 22.32 104.34 35.66 171.75 35.66 67.4 0 126.96-13.33 171.74-35.65 6.95 7.17 11.11 14.31 11.11 20.6 0.01 28.26-72.92 70.99-182.85 70.99z' fill='currentColor'/>";
    result+="</g>";
    result+="</svg>  ";
    TotalEuros += p.second * getTarif(attrib[p.first],"energy")/1000;
    result += String(p.second * getTarif(attrib[p.first],"energy")/1000);

    result += " € ";
    result +="</div></div>";
  }

  if ((strcmp(ConfigGeneral.Production,"")!=0) && (strcmp(ConfigGeneral.Production,dev->getDeviceID().c_str())!=0))
  {
    result += "<div class='row'>";
    result +=       "<div class='col-1'>";
    result +=            "<div style=\"border:1px solid grey;width:10px;height:15px;background-color:#27ae60\"></div>";
    result +=       "</div>";
    result +=       "<div class='col-11'> ";
    result +=         "<svg fill='#000000' style='width:16px;' width='24px' height='24px' viewBox='0 -64 640 640' xmlns='http://www.w3.org/2000/svg'><g id='SVGRepo_bgCarrier' stroke-width='0'></g><g id='SVGRepo_tracerCarrier' stroke-linecap='round' stroke-linejoin='round'></g><g id='SVGRepo_iconCarrier'><path d='M431.98 448.01l-47.97.05V416h-128v32.21l-47.98.05c-8.82.01-15.97 7.16-15.98 15.99l-.05 31.73c-.01 8.85 7.17 16.03 16.02 16.02l223.96-.26c8.82-.01 15.97-7.16 15.98-15.98l.04-31.73c.01-8.85-7.17-16.03-16.02-16.02zM585.2 26.74C582.58 11.31 568.99 0 553.06 0H86.93C71 0 57.41 11.31 54.79 26.74-3.32 369.16.04 348.08.03 352c-.03 17.32 14.29 32 32.6 32h574.74c18.23 0 32.51-14.56 32.59-31.79.02-4.08 3.35 16.95-54.76-325.47zM259.83 64h120.33l9.77 96H250.06l9.77-96zm-75.17 256H71.09L90.1 208h105.97l-11.41 112zm16.29-160H98.24l16.29-96h96.19l-9.77 96zm32.82 160l11.4-112h149.65l11.4 112H233.77zm195.5-256h96.19l16.29 96H439.04l-9.77-96zm26.06 256l-11.4-112H549.9l19.01 112H455.33z'></path></g></svg> ";
    result +=           String(sumProd);
    result +=           " Wh ";
    result+=" <svg style='width:16px;' width='24px' height='24px' viewBox='0 0 1024 1024' class='icon' version='1.1' xmlns='http://www.w3.org/2000/svg' fill='#000000'>";
    result+="<g id='SVGRepo_bgCarrier' stroke-width='0'/>";
    result+="<g id='SVGRepo_tracerCarrier' stroke-linecap='round' stroke-linejoin='round'/>";
    result+="<g id='SVGRepo_iconCarrier'>";
    result+="<path d='M951.87 253.86c0-82.18-110.05-144.14-256-144.14s-256 61.96-256 144.14c0 0.73 0.16 1.42 0.18 2.14h-0.18v109.71h73.14v-9.06c45.77 25.81 109.81 41.33 182.86 41.33 67.39 0 126.93-13.33 171.71-35.64 6.94 7.18 11.15 14.32 11.15 20.58 0 28.25-72.93 70.98-182.86 70.98h-73.12v73.14h73.12c67.4 0 126.96-13.33 171.74-35.65 6.95 7.17 11.11 14.31 11.11 20.6 0 28.27-72.93 71-182.86 71l-25.89 0.12c-15.91 0.14-31.32 0.29-46.34-0.11l-1.79 73.11c8.04 0.2 16.18 0.27 24.48 0.27 7.93 0 16-0.05 24.2-0.12l25.34-0.12c67.44 0 127.02-13.35 171.81-35.69 6.97 7.23 11.04 14.41 11.04 20.62 0 28.27-72.93 71-182.86 71h-73.12v73.14h73.12c67.44 0 127.01-13.35 171.81-35.69 6.98 7.22 11.05 14.4 11.05 20.62 0 28.27-72.93 71-182.86 71h-73.12v73.14h73.12c145.95 0 256-61.96 256-144.14 0-0.68-0.09-1.45-0.11-2.14h0.11V256h-0.18c0.03-0.72 0.2-1.42 0.2-2.14z m-438.86 0c0-28.27 72.93-71 182.86-71s182.86 42.73 182.86 71c0 28.25-72.93 70.98-182.86 70.98s-182.86-42.73-182.86-70.98z' fill='currentColor'/>";
    result+="<path d='M330.15 365.71c-145.95 0-256 61.96-256 144.14 0 0.73 0.16 1.42 0.18 2.14h-0.18v256c0 82.18 110.05 144.14 256 144.14s256-61.96 256-144.14V512h-0.18c0.02-0.72 0.18-1.42 0.18-2.14 0-82.18-110.05-144.15-256-144.15zM147.29 638.93c0-6.32 4.13-13.45 11.08-20.62 44.79 22.33 104.36 35.67 171.78 35.67 67.39 0 126.93-13.33 171.71-35.64 6.94 7.18 11.15 14.32 11.15 20.58 0 28.25-72.93 70.98-182.86 70.98s-182.86-42.72-182.86-70.97z m182.86-200.07c109.93 0 182.86 42.73 182.86 71 0 28.25-72.93 70.98-182.86 70.98s-182.86-42.73-182.86-70.98c0-28.27 72.93-71 182.86-71z m0 400.14c-109.93 0-182.86-42.73-182.86-71 0-6.29 4.17-13.43 11.11-20.6 44.79 22.32 104.34 35.66 171.75 35.66 67.4 0 126.96-13.33 171.74-35.65 6.95 7.17 11.11 14.31 11.11 20.6 0.01 28.26-72.92 70.99-182.85 70.99z' fill='currentColor'/>";
    result+="</g>";
    result+="</svg>  ";
    TotalEuros += sumProd* getTarif(0,"production")/1000;
    result += String(sumProd * getTarif(0,"production")/1000);
    result += " € ";
    result +=       "</div>";
    result += "</div>";
  }

  if ((strcmp(ConfigGeneral.Gaz,"")!=0) && (strcmp(ConfigGeneral.unitGaz,"Wh")==0))
  {
    result += "<div class='row'>";
    result +=       "<div class='col-1'>";
    result +=            "<div style=\"border:1px solid grey;width:10px;height:15px;background-color:#e67e22\"></div>";
    result +=       "</div>";
    result +=       "<div class='col-11'> ";
    result +=         "<svg fill='#000000' style='width:16px;' width='24px' height='24px' viewBox='0 0 32 32' version='1.1' xmlns='http://www.w3.org/2000/svg'><g id='SVGRepo_bgCarrier' stroke-width='0'></g><g id='SVGRepo_tracerCarrier' stroke-linecap='round' stroke-linejoin='round'></g><g id='SVGRepo_iconCarrier'> <title>flame-symbol</title> <path d='M10.375 7.562c0 5.625 5.625 6.563 5.625 11.25 0 1.875-1.875 4.687-4.687 4.687s-4.687-2.813-2.813-7.5c-2.813 1.875-3.75 3.75-3.75 5.625 0 4.688 4.687 9.375 11.25 9.375s11.25-2.812 11.25-8.438c0.042-8.32-9.587-11.1-12.188-15-1.875-2.813-0.937-4.688 0.937-6.563-3.75 0.938-5.625 3.563-5.625 6.563v0z'></path> </g></svg> ";
    result +=           String(sumGaz);
    result +=           " Wh";
    result+=" <svg style='width:16px;' width='24px' height='24px' viewBox='0 0 1024 1024' class='icon' version='1.1' xmlns='http://www.w3.org/2000/svg' fill='#000000'>";
    result+="<g id='SVGRepo_bgCarrier' stroke-width='0'/>";
    result+="<g id='SVGRepo_tracerCarrier' stroke-linecap='round' stroke-linejoin='round'/>";
    result+="<g id='SVGRepo_iconCarrier'>";
    result+="<path d='M951.87 253.86c0-82.18-110.05-144.14-256-144.14s-256 61.96-256 144.14c0 0.73 0.16 1.42 0.18 2.14h-0.18v109.71h73.14v-9.06c45.77 25.81 109.81 41.33 182.86 41.33 67.39 0 126.93-13.33 171.71-35.64 6.94 7.18 11.15 14.32 11.15 20.58 0 28.25-72.93 70.98-182.86 70.98h-73.12v73.14h73.12c67.4 0 126.96-13.33 171.74-35.65 6.95 7.17 11.11 14.31 11.11 20.6 0 28.27-72.93 71-182.86 71l-25.89 0.12c-15.91 0.14-31.32 0.29-46.34-0.11l-1.79 73.11c8.04 0.2 16.18 0.27 24.48 0.27 7.93 0 16-0.05 24.2-0.12l25.34-0.12c67.44 0 127.02-13.35 171.81-35.69 6.97 7.23 11.04 14.41 11.04 20.62 0 28.27-72.93 71-182.86 71h-73.12v73.14h73.12c67.44 0 127.01-13.35 171.81-35.69 6.98 7.22 11.05 14.4 11.05 20.62 0 28.27-72.93 71-182.86 71h-73.12v73.14h73.12c145.95 0 256-61.96 256-144.14 0-0.68-0.09-1.45-0.11-2.14h0.11V256h-0.18c0.03-0.72 0.2-1.42 0.2-2.14z m-438.86 0c0-28.27 72.93-71 182.86-71s182.86 42.73 182.86 71c0 28.25-72.93 70.98-182.86 70.98s-182.86-42.73-182.86-70.98z' fill='currentColor'/>";
    result+="<path d='M330.15 365.71c-145.95 0-256 61.96-256 144.14 0 0.73 0.16 1.42 0.18 2.14h-0.18v256c0 82.18 110.05 144.14 256 144.14s256-61.96 256-144.14V512h-0.18c0.02-0.72 0.18-1.42 0.18-2.14 0-82.18-110.05-144.15-256-144.15zM147.29 638.93c0-6.32 4.13-13.45 11.08-20.62 44.79 22.33 104.36 35.67 171.78 35.67 67.39 0 126.93-13.33 171.71-35.64 6.94 7.18 11.15 14.32 11.15 20.58 0 28.25-72.93 70.98-182.86 70.98s-182.86-42.72-182.86-70.97z m182.86-200.07c109.93 0 182.86 42.73 182.86 71 0 28.25-72.93 70.98-182.86 70.98s-182.86-42.73-182.86-70.98c0-28.27 72.93-71 182.86-71z m0 400.14c-109.93 0-182.86-42.73-182.86-71 0-6.29 4.17-13.43 11.11-20.6 44.79 22.32 104.34 35.66 171.75 35.66 67.4 0 126.96-13.33 171.74-35.65 6.95 7.17 11.11 14.31 11.11 20.6 0.01 28.26-72.92 70.99-182.85 70.99z' fill='currentColor'/>";
    result+="</g>";
    result+="</svg>  ";
    TotalEuros += sumGaz * getTarif(0,"gaz")/1000;
    result += String(sumGaz * getTarif(0,"gaz")/1000);
    result += " € ";
    result +=       "</div>";
    result += "</div>";
  }

  result += "<br><div class='position-absolute bottom-0 start-0'><h5>Total</h5>";
  result += "<svg fill='#000000' style='width:16px;' width='24px' height='24px' viewBox='-3.2 -3.2 38.40 38.40' version='1.1' xmlns='http://www.w3.org/2000/svg' stroke='#000000'><g id='SVGRepo_bgCarrier' stroke-width='0'></g><g id='SVGRepo_tracerCarrier' stroke-linecap='round' stroke-linejoin='round' stroke='#CCCCCC' stroke-width='0.384'></g><g id='SVGRepo_iconCarrier'> <path d='M18.605 2.022v0zM18.605 2.022l-2.256 11.856 8.174 0.027-11.127 16.072 2.257-13.043-8.174-0.029zM18.606 0.023c-0.054 0-0.108 0.002-0.161 0.006-0.353 0.028-0.587 0.147-0.864 0.333-0.154 0.102-0.295 0.228-0.419 0.373-0.037 0.043-0.071 0.088-0.103 0.134l-11.207 14.832c-0.442 0.607-0.508 1.407-0.168 2.076s1.026 1.093 1.779 1.099l5.773 0.042-1.815 10.694c-0.172 0.919 0.318 1.835 1.18 2.204 0.257 0.11 0.527 0.163 0.793 0.163 0.629 0 1.145-0.294 1.533-0.825l11.22-16.072c0.442-0.607 0.507-1.408 0.168-2.076-0.34-0.669-1.026-1.093-1.779-1.098l-5.773-0.010 1.796-9.402c0.038-0.151 0.057-0.308 0.057-0.47 0-1.082-0.861-1.964-1.939-1.999-0.024-0.001-0.047-0.001-0.071-0.001v0z'></path> </g></svg> ";
  result += String((TotalWh+sumProd+sumGaz));
  result += " Wh ";
  result+=" <svg style='width:16px;' width='24px' height='24px' viewBox='0 0 1024 1024' class='icon' version='1.1' xmlns='http://www.w3.org/2000/svg' fill='#000000'>";
  result+="<g id='SVGRepo_bgCarrier' stroke-width='0'/>";
  result+="<g id='SVGRepo_tracerCarrier' stroke-linecap='round' stroke-linejoin='round'/>";
  result+="<g id='SVGRepo_iconCarrier'>";
  result+="<path d='M951.87 253.86c0-82.18-110.05-144.14-256-144.14s-256 61.96-256 144.14c0 0.73 0.16 1.42 0.18 2.14h-0.18v109.71h73.14v-9.06c45.77 25.81 109.81 41.33 182.86 41.33 67.39 0 126.93-13.33 171.71-35.64 6.94 7.18 11.15 14.32 11.15 20.58 0 28.25-72.93 70.98-182.86 70.98h-73.12v73.14h73.12c67.4 0 126.96-13.33 171.74-35.65 6.95 7.17 11.11 14.31 11.11 20.6 0 28.27-72.93 71-182.86 71l-25.89 0.12c-15.91 0.14-31.32 0.29-46.34-0.11l-1.79 73.11c8.04 0.2 16.18 0.27 24.48 0.27 7.93 0 16-0.05 24.2-0.12l25.34-0.12c67.44 0 127.02-13.35 171.81-35.69 6.97 7.23 11.04 14.41 11.04 20.62 0 28.27-72.93 71-182.86 71h-73.12v73.14h73.12c67.44 0 127.01-13.35 171.81-35.69 6.98 7.22 11.05 14.4 11.05 20.62 0 28.27-72.93 71-182.86 71h-73.12v73.14h73.12c145.95 0 256-61.96 256-144.14 0-0.68-0.09-1.45-0.11-2.14h0.11V256h-0.18c0.03-0.72 0.2-1.42 0.2-2.14z m-438.86 0c0-28.27 72.93-71 182.86-71s182.86 42.73 182.86 71c0 28.25-72.93 70.98-182.86 70.98s-182.86-42.73-182.86-70.98z' fill='currentColor'/>";
  result+="<path d='M330.15 365.71c-145.95 0-256 61.96-256 144.14 0 0.73 0.16 1.42 0.18 2.14h-0.18v256c0 82.18 110.05 144.14 256 144.14s256-61.96 256-144.14V512h-0.18c0.02-0.72 0.18-1.42 0.18-2.14 0-82.18-110.05-144.15-256-144.15zM147.29 638.93c0-6.32 4.13-13.45 11.08-20.62 44.79 22.33 104.36 35.67 171.78 35.67 67.39 0 126.93-13.33 171.71-35.64 6.94 7.18 11.15 14.32 11.15 20.58 0 28.25-72.93 70.98-182.86 70.98s-182.86-42.72-182.86-70.97z m182.86-200.07c109.93 0 182.86 42.73 182.86 71 0 28.25-72.93 70.98-182.86 70.98s-182.86-42.73-182.86-70.98c0-28.27 72.93-71 182.86-71z m0 400.14c-109.93 0-182.86-42.73-182.86-71 0-6.29 4.17-13.43 11.11-20.6 44.79 22.32 104.34 35.66 171.75 35.66 67.4 0 126.96-13.33 171.74-35.65 6.95 7.17 11.11 14.31 11.11 20.6 0.01 28.26-72.92 70.99-182.85 70.99z' fill='currentColor'/>";
  result+="</g>";
  result+="</svg>  ";

  result += TotalEuros;
  result += " € ";
  result += "</div>";

  return result;

}

String getTrendPower(String IEEE,String Attribute, String Time)
{
  String result="";
  String trendIcon="";
  String trendColor="";

  if (Time=="hour")
  {
    String min,max,trend,last;
    DeviceEnergyHistory hist;
    
    for (size_t i = 0; i < devices.size(); i++) 
    {
      DeviceData* device = devices[i];
      if (device->getDeviceID() == IEEE)
      {
        AttributeStats &st = device->powerHistory.stats[Attribute.toInt()];
        trend = st.trend;
        min = st.min;
        max = st.max;
        last = st.last;
        hist = device->energyHistory;
        break;
      }
    }
    float tarifEuros=0;
    for (const auto &graphEntry : hist.hours.graph) {
      const PsString &Key = graphEntry.first;
      const ValueMap &valMap   = graphEntry.second;
      if (Hour.c_str() == Key.c_str())
      {
        for (const auto &attrPair : valMap.attributes) {
          int attrId   = attrPair.first;
          long attrVal = attrPair.second;
          if (attrId>0)
          {
            tarifEuros+=attrVal * getTarif(attrId,"energy")/1000;
            tarifEuros+=(attrVal/1000) * atof(ConfigGeneral.tarifCSPE);
          }  
        }
        break;
      }
    }
    

    String op="";
    if (trend !="")
    {
      if (trend.toInt()>0)
      {
        trendColor="orange";
        op="+";
      }else if (trend.toInt()<0){
        trendColor="green";
        op="";
      }else{
        trendColor="grey";
        op="";
      }
      result +="<div style='text-align:center;'>";
        result +="<div style='float:left;display:inline-block;width:64px;'>";
        result += "<svg width='100' height='100' viewBox='0 0 100 100' xmlns='http://www.w3.org/2000/svg'>";
          result += "<rect width='100' height='100' rx='20' ry='20' fill='"+trendColor+"'/>";
          result += "<path d='M55 10 L30 55 H48 L40 90 L70 45 H52 Z' fill='#FFFFFF'/>";
        result += "</svg>";
        result +="</div>";
        result+="<div style='display:inline-box;height:100px;padding-top:15px;'><span style='font-size:24px;'>";
          result +=op+trend+ " VA ";
        result+="</span>";
        result+="<br><span style='font-size:12px;'><strong>Min:</strong> ";
          result+=min;
          result+=" VA <br> <strong>Max :</strong> ";
          result+=max;
          result+=" VA <br> <strong>";
        result+="</span></div>";   
      result += "</div>";
    }
  }else
  {
    DeviceEnergyHistory hist;
    for (size_t i = 0; i < devices.size(); i++) 
    {
      DeviceData* device = devices[i];
      if (device->getDeviceID() == IEEE)
      {
        hist = device->energyHistory;
        break;
      }
    }

    int energyTemp=0;
    long int tmp=0;
    long int lastTime=0;
    float tarifEuro=0;
    long int maxVal =0;
    long int minVal =0;

    if (Time=="day")
    {
      for (const auto &graphEntry : hist.days.graph) {
        const PsString &Key = graphEntry.first;
        const ValueMap &valMap   = graphEntry.second;

        long int sum=0;
        float tmpEuros=0;

        for (const auto &attrPair : valMap.attributes) {
          int attrId   = attrPair.first;
          long attrVal = attrPair.second;
          if (attrVal > 0 ) sum +=attrVal;

          if (attrId>0)
          {          
            tmpEuros+=attrVal * getTarif(attrId,"energy")/1000;
            tmpEuros+=(attrVal/1000) * atof(ConfigGeneral.tarifCSPE);
          }
        }
        

        maxVal = max(sum,maxVal);
        minVal = min(sum,minVal);
        if (minVal ==0){minVal=maxVal;}  

        if (memcmp(Day.c_str(),Key.c_str(),2)==0)
        {
          tmp=sum;
          tarifEuro=tmpEuros;
        }
        if (memcmp(Yesterday.c_str(),Key.c_str(),2)==0)
        {
          lastTime=sum;
        }
      }
    }else if (Time=="month")
    {
      for (const auto &graphEntry : hist.months.graph) {
        const PsString &Key = graphEntry.first;
        const ValueMap &valMap   = graphEntry.second;

        long int sum=0;
        float tmpEuros=0;

        for (const auto &attrPair : valMap.attributes) {
          int attrId   = attrPair.first;
          long attrVal = attrPair.second;
          if (attrVal > 0 ) sum +=attrVal;
          if (attrId>0)
          {
            tmpEuros+=attrVal * getTarif(attrId,"energy")/1000;
            tmpEuros+=(attrVal/1000) * atof(ConfigGeneral.tarifCSPE);
          }
        }

        maxVal = max(sum,maxVal);
        minVal = min(sum,minVal);
        if (minVal ==0){minVal=maxVal;}  
        
        //tmpEuros+= String(ConfigGeneral.tarifAbo).toFloat();
        if (memcmp(Month.c_str(),Key.c_str(),2)==0)
        {
          tmp=sum;
          tarifEuro=tmpEuros;
        }
        if (memcmp(String(Month.toInt()-1).c_str(),Key.c_str(),4)==0)
        {
          lastTime=sum;
        }
      }  

    }else if (Time=="year")
    {
      for (const auto &graphEntry : hist.years.graph) {
        const PsString &Key = graphEntry.first;
        const ValueMap &valMap   = graphEntry.second;

        long int sum=0;
        float tmpEuros=0;

        for (const auto &attrPair : valMap.attributes) {
          int attrId   = attrPair.first;
          long attrVal = attrPair.second;
          if (attrVal > 0 ) sum +=attrVal;
          if (attrId>0)
          {
            tmpEuros+=attrVal * getTarif(attrId,"energy")/1000;
            tmpEuros+=(attrVal/1000) * atof(ConfigGeneral.tarifCSPE);
          }
        }
        maxVal = max(sum,maxVal);
        minVal = min(sum,minVal);
        if (minVal ==0){minVal=maxVal;}  
        

        if (memcmp(Year.c_str(),Key.c_str(),4)==0)
        {
          tmp=sum;
          tarifEuro=tmpEuros;
        }
        if (memcmp(String(Year.toInt()-1).c_str(),Key.c_str(),4)==0)
        {
          lastTime=sum;
        }
      }  
    }   

    if (tmp>0)
    {
      energyTemp = tmp;
    }

    long int trend=0;
    String op="";
    if ( energyTemp > lastTime)
    {
      trendColor="orange";
      op="+";
      trend=energyTemp - lastTime;
    }else if ( energyTemp < lastTime)
    {
      trendColor="green";
      op="-";
      trend=lastTime - energyTemp;
    }else{
      trendColor="grey";
      op="";
      trend=0;
    }
    result +="<div style='text-align:center;'>";
    result +="<div style='float:left;display:inline-block;width:64px;'>";
    result += "<svg width='100' height='100' viewBox='0 0 100 100' xmlns='http://www.w3.org/2000/svg'>";
      result += "<rect width='100' height='100' rx='20' ry='20' fill='"+trendColor+"'/>";
      result += "<path d='M55 10 L30 55 H48 L40 90 L70 45 H52 Z' fill='#FFFFFF'/>";
    result += "</svg>";
    result +="</div>";
    result+="<div style='display:inline-box;height:100px;padding-top:15px;'><span style='font-size:24px;'>";
      result +=op+trend+ " Wh ";
    result+="</span>";
    result+="<br><span style='font-size:12px;'><strong>Min:</strong> ";
      result+=minVal;
      result+=" Wh <br> <strong>Max :</strong> ";
      result+=maxVal;
      result+=" Wh";
    result+="</span></div>";   
    result+="<br><div style='display:inline-block;width:64px;float:left;'>";

      result+=" <svg width='64px' height='64px' viewBox='0 0 1024 1024' class='icon' version='1.1' xmlns='http://www.w3.org/2000/svg' fill='#000000'>";
        result+="<g id='SVGRepo_bgCarrier' stroke-width='0'/>";
        result+="<g id='SVGRepo_tracerCarrier' stroke-linecap='round' stroke-linejoin='round'/>";
        result+="<g id='SVGRepo_iconCarrier'>";
        result+="<path d='M951.87 253.86c0-82.18-110.05-144.14-256-144.14s-256 61.96-256 144.14c0 0.73 0.16 1.42 0.18 2.14h-0.18v109.71h73.14v-9.06c45.77 25.81 109.81 41.33 182.86 41.33 67.39 0 126.93-13.33 171.71-35.64 6.94 7.18 11.15 14.32 11.15 20.58 0 28.25-72.93 70.98-182.86 70.98h-73.12v73.14h73.12c67.4 0 126.96-13.33 171.74-35.65 6.95 7.17 11.11 14.31 11.11 20.6 0 28.27-72.93 71-182.86 71l-25.89 0.12c-15.91 0.14-31.32 0.29-46.34-0.11l-1.79 73.11c8.04 0.2 16.18 0.27 24.48 0.27 7.93 0 16-0.05 24.2-0.12l25.34-0.12c67.44 0 127.02-13.35 171.81-35.69 6.97 7.23 11.04 14.41 11.04 20.62 0 28.27-72.93 71-182.86 71h-73.12v73.14h73.12c67.44 0 127.01-13.35 171.81-35.69 6.98 7.22 11.05 14.4 11.05 20.62 0 28.27-72.93 71-182.86 71h-73.12v73.14h73.12c145.95 0 256-61.96 256-144.14 0-0.68-0.09-1.45-0.11-2.14h0.11V256h-0.18c0.03-0.72 0.2-1.42 0.2-2.14z m-438.86 0c0-28.27 72.93-71 182.86-71s182.86 42.73 182.86 71c0 28.25-72.93 70.98-182.86 70.98s-182.86-42.73-182.86-70.98z' fill='currentColor'/>";
        result+="<path d='M330.15 365.71c-145.95 0-256 61.96-256 144.14 0 0.73 0.16 1.42 0.18 2.14h-0.18v256c0 82.18 110.05 144.14 256 144.14s256-61.96 256-144.14V512h-0.18c0.02-0.72 0.18-1.42 0.18-2.14 0-82.18-110.05-144.15-256-144.15zM147.29 638.93c0-6.32 4.13-13.45 11.08-20.62 44.79 22.33 104.36 35.67 171.78 35.67 67.39 0 126.93-13.33 171.71-35.64 6.94 7.18 11.15 14.32 11.15 20.58 0 28.25-72.93 70.98-182.86 70.98s-182.86-42.72-182.86-70.97z m182.86-200.07c109.93 0 182.86 42.73 182.86 71 0 28.25-72.93 70.98-182.86 70.98s-182.86-42.73-182.86-70.98c0-28.27 72.93-71 182.86-71z m0 400.14c-109.93 0-182.86-42.73-182.86-71 0-6.29 4.17-13.43 11.11-20.6 44.79 22.32 104.34 35.66 171.75 35.66 67.4 0 126.96-13.33 171.74-35.65 6.95 7.17 11.11 14.31 11.11 20.6 0.01 28.26-72.92 70.99-182.85 70.99z' fill='currentColor'/>";
        result+="</g>";
        result+="</svg>";   
    result+="</div>";
    result+="<div style='display:inline-block;line-height:72px;'><span style='font-size:24px;'>";
      result +=String(tarifEuro) +" €";
    result+="</span></div>";
    result += "</div>";
  }
  return result;
}

String getLastValuePower(String IEEE,String Attribute, String Time)
{

  String result="";
  if (Time=="hour")
  {
    for (size_t i = 0; i < devices.size(); i++) 
    {
      DeviceData* device = devices[i];
      if (device->getDeviceID() == IEEE)
      {
        String result="";
        if (Attribute != "519")
        {
          result = String(strtol(device->getValue(std::string("0B04"),std::string(String(Attribute).c_str())).c_str(),0,16));
          result +=";";
          result += String(strtol(device->getValue(std::string("0B01"),std::string("13")).c_str(),0,16)*230);
          result +=";0;"; 
          result += device->getPowerW()+" W";
        }else{
          result = String(strtol(device->getValue(std::string("FF66"),std::string(String(Attribute).c_str())).c_str(),0,16));
          result +=";";
          result += String(strtol(device->getValue(std::string("0B01"),std::string("14")).c_str(),0,16)*1000);
          result +=";0"; 
        }

        return result;
      }
    }
  }else
  {
    DeviceEnergyHistory hist;
     String mode;
    for (size_t i = 0; i < devices.size(); i++) 
    {
      DeviceData* device = devices[i];
      if (Attribute != "519")
      {
        mode="consumption";
        if (device->getDeviceID() == IEEE)
        {
          hist = device->energyHistory;
          break;
        }
      }else{
        mode="production";
        if (device->getDeviceID() == ConfigGeneral.Production)
        {
          hist = device->energyHistory;
          break;
        }
      }
      
    }

    long sum = 0;
    if (Time=="day")
    {
      for (const auto &graphEntry : hist.days.graph) {
        const PsString &Key = graphEntry.first;
        const ValueMap &valMap   = graphEntry.second;

        if (memcmp(Day.c_str(),Key.c_str(),2)==0)
        {
          for (const auto &attrPair : valMap.attributes) {
              long attrVal = attrPair.second;
              if (mode == "production")
              {
                if (attrVal < 0) sum +=attrVal;
              }else{
                if (attrVal > 0) sum +=attrVal;
                
              }
          }
          break;
        }
      }
    }else if (Time=="month")
    {
      for (const auto &graphEntry : hist.months.graph) {
        const PsString &Key = graphEntry.first;
        const ValueMap &valMap   = graphEntry.second;

        if (memcmp(Month.c_str(),Key.c_str(),2)==0)
        {
          for (const auto &attrPair : valMap.attributes) {
              long attrVal = attrPair.second;
              if (mode == "production")
              {
                if (attrVal < 0) sum +=attrVal;
              }else{
                if (attrVal > 0) sum +=attrVal;
                
              }
          }
          break;
        }
      }
    }else if (Time=="year")
    {
      for (const auto &graphEntry : hist.years.graph) {
        const PsString &Key = graphEntry.first;
        const ValueMap &valMap   = graphEntry.second;

        if (memcmp(Year.c_str(),Key.c_str(),2)==0)
        {
          for (const auto &attrPair : valMap.attributes) {
              long attrVal = attrPair.second;
              if (mode == "production")
              {
                if (attrVal < 0) sum +=attrVal;
              }else{
                if (attrVal > 0) sum +=attrVal;
                
              }
          }
          break;
        }
      }
    }
    result= String(sum);
  }
  return result;
}


String getPowerGaugeTimeDay(String IEEE, String Attribute)
{
  //String path = "/db/"+Attribute+"_"+IEEE+".json";
  const char* path ="/db/";
  const char* extension =".json";
  char name_with_extension[64];
  strcpy(name_with_extension,path);
  strcat(name_with_extension,Attribute.c_str());
  strcat(name_with_extension,"_");
  strcat(name_with_extension,IEEE.c_str());
  strcat(name_with_extension,extension);
  String result="";
  File DeviceFile = LittleFS.open(name_with_extension, FILE_READ);
  if (!DeviceFile|| DeviceFile.isDirectory()) {
    DEBUG_PRINTLN(F("failed open"));
    return "";
  }else{
    SpiRamJsonDocument temp(MAXHEAP);
    deserializeJson(temp,DeviceFile);
    if ((temp[Attribute]["minute"][Hour+":"+Minute]) && (temp[Attribute]["min"]) && (temp[Attribute]["max"]))
    {
      String t = Hour+":"+Minute;
      result =String((int)temp[Attribute]["minute"][t])+";"+String((int)temp[Attribute]["min"])+";"+String((int)temp[Attribute]["max"]);
      return result;
    }
    DeviceFile.close();
  }
  return result;
}


String getPowerDatas( String IEEE, String type, String Attribute, String time)
{
  //String path = "/db/"+type+"_"+Attribute+"_"+IEEE+".json";
  const char* path ="/db/";
  const char* extension =".json";
  char name_with_extension[64];
  strcpy(name_with_extension,path);
  strcat(name_with_extension,type.c_str());
  strcat(name_with_extension,"_");
  strcat(name_with_extension,Attribute.c_str());
  strcat(name_with_extension,"_");
  strcat(name_with_extension,IEEE.c_str());
  strcat(name_with_extension,extension);
  String result="";
  File DeviceFile = LittleFS.open(name_with_extension, FILE_READ);
  if (!DeviceFile|| DeviceFile.isDirectory()) {
    DEBUG_PRINTLN(F("failed open"));
    return "";
  }else
  {
    while (DeviceFile.available()) {
      result += (char) DeviceFile.read();
    }
    
    DeviceFile.close();

    return result;
  }
  return "";
}

String getZigbeeValue(String IEEE, String cluster, String attribute)
{
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == IEEE.substring(0, 16))
    {
      return device->getValue(cluster.c_str(),attribute.c_str());
    }
  }
  return String("");
}

String getDeviceStatus(String IEEE)
{
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == IEEE.substring(0, 16))
    {
      return device->getInfo().Status;
    }
  }
  return String("");
}

bool getPollingDevice(uint8_t shortAddr[2], int device_id, String model)
{
  if (TemplateExist(device_id))
  {
    //String path = "/tp/"+(String)device_id+".json";
    const char* path ="/tp/";
    const char* extension =".json";
    char name_with_extensionTP[64];
    strcpy(name_with_extensionTP,path);
    strcat(name_with_extensionTP,String(device_id).c_str());
    strcat(name_with_extensionTP,extension);

    File tpFile = LittleFS.open(name_with_extensionTP, FILE_READ);
    if (!tpFile|| tpFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      tpFile.close();
      return false;
    }else
    {
      SpiRamJsonDocument temp(MAXHEAP);
      deserializeJson(temp,tpFile);
      if (temp.containsKey(model))
      {
        JsonArray StatusArray = temp[model][0]["status"].as<JsonArray>();
        if (!StatusArray.isNull())
        {
          int i=0;
          int SA = shortAddr[0]*256+shortAddr[1];
          String inifile = GetMacAdrr(SA);
          for (size_t k = 0; k < devices.size(); k++) 
          {
            DeviceData* device = devices[k];
            if (device->getDeviceID() == inifile.substring(0,16))
            {
              device->getPollList().clear();
              for(JsonVariant v : StatusArray) 
              {
                if (temp[model][0]["status"][i]["poll"])
                {
                  // Conversion de la valeur cluster (hexadécimal) en entier
                  int cluster = strtol(temp[model][0]["status"][i]["cluster"], 0, 16);
                  int attribut = (int)temp[model][0]["status"][i]["attribut"];
                  int poll = (int)temp[model][0]["status"][i]["poll"];

                  // Mise à jour de la liste poll de l'objet DeviceData
                  DeviceData::PollItem newItem;
                  newItem.cluster = String(cluster); // conversion de l'entier en String
                  newItem.attribut = attribut;
                  newItem.poll = poll;
                  newItem.last = 1;
                  device->getPollList().push_back(newItem);

                }
                i++;
              }
              return true;
            } 
          }
        }
        return false;
      }
      tpFile.close();
      return false;
    }
  }
  return false;
}

/*bool getPollingDevice(uint8_t shortAddr[2], int device_id, String model)
{
  if (TemplateExist(device_id))
  {
    //String path = "/tp/"+(String)device_id+".json";
    const char* path ="/tp/";
    const char* extension =".json";
    char name_with_extensionTP[64];
    strcpy(name_with_extensionTP,path);
    strcat(name_with_extensionTP,String(device_id).c_str());
    strcat(name_with_extensionTP,extension);

    File tpFile = LittleFS.open(name_with_extensionTP, FILE_READ);
    if (!tpFile|| tpFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      tpFile.close();
      return false;
    }else
    {
      SpiRamJsonDocument temp(MAXHEAP);
      deserializeJson(temp,tpFile);
      if (temp.containsKey(model))
      {
        JsonArray StatusArray = temp[model][0]["status"].as<JsonArray>();
        if (!StatusArray.isNull())
        {
          String cluster;
          int attribut;
          int poll;
          int i=0;
          int j=0;
          
          int SA = shortAddr[0]*256+shortAddr[1];
          String inifile = GetMacAdrr(SA);
          const char* path ="/db/";
          char name_with_extension[64];
          strcpy(name_with_extension,path);
          strcat(name_with_extension,inifile.c_str());

          File file = LittleFS.open(name_with_extension, "r+");
          if (!file) {
            file = safeOpenFile(name_with_extension, "w+");
            DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
            DEBUG_PRINTLN(path);
            if (!file)
            {
              DEBUG_PRINTLN(F("Impossible de créer le fichier (getPollingDevice) "));
              safeCloseFile(file,name_with_extension);
              return false;
            }

          }
          size_t filesize = file.size();
          
          SpiRamJsonDocument doc(MAXHEAP);
          if (filesize>0)
          {
            DeserializationError error = deserializeJson(doc, file);
            file.close();
            if (error) {
              //DEBUG_PRINTLN(F("Erreur lors de la désérialisation du fichier"));
    
              return "Error";
            }
          }
          
          for(JsonVariant v : StatusArray) 
          {
              
              if (temp[model][0]["status"][i]["poll"])
              {
                  cluster = strtol(temp[model][0]["status"][i]["cluster"],0,16);
                  attribut = (int)temp[model][0]["status"][i]["attribut"];
                  poll = (int)temp[model][0]["status"][i]["poll"];

                  doc["poll"][j]["cluster"]=cluster;
                  doc["poll"][j]["attribut"]=attribut;
                  doc["poll"][j]["poll"]=poll;
                  doc["poll"][j]["last"]=1;                 
                  j++;
              }
              
              i++;
          }

          if (j>0)
          {
            file = safeOpenFile(name_with_extension, "w+");
            if (!file|| file.isDirectory()) {
              DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
              DEBUG_PRINTLN(path);
              safeCloseFile(file,name_with_extension);
              return false;
            }
            if (!doc.isNull())
            {
              if (serializeJson(doc, file) == 0) {
                DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier"));
                safeCloseFile(file,name_with_extension);
                return false;
              }
            }
          
            // Fermer le fichier
            safeCloseFile(file,name_with_extension);
            return true;
          }
        }
        return false;
      }
      safeCloseFile(tpFile,name_with_extensionTP);
      return false;
    }
  }
  return false;
}*/

void getConfigReport(uint8_t shortAddr[2], int device_id, String model)
{
  if (TemplateExist(device_id))
  {
    //String path = "/tp/"+(String)device_id+".json";
    const char* path ="/tp/";
    const char* extension =".json";
    char name_with_extension[64];
    strcpy(name_with_extension,path);
    strcat(name_with_extension,String(device_id).c_str());
    strcat(name_with_extension,extension);

    File tpFile = LittleFS.open(name_with_extension, FILE_READ);
    if (!tpFile|| tpFile.isDirectory()) {
      tpFile.close();
      DEBUG_PRINTLN(F("failed open"));
      
    }else
    {
      SpiRamJsonDocument temp(MAXHEAP);
      deserializeJson(temp,tpFile);
       
      if (temp.containsKey(model))
      {
        JsonArray ReportArray = temp[model][0]["report"].as<JsonArray>();
        if (!ReportArray.isNull()) 
        {
          int cluster, attribut;
          uint8_t rType;
          int rMin,rMax,rTimeout;
          uint8_t rChange;
          bool sendReport ;
          int i=0;
          for(JsonVariant v : ReportArray) 
          {
              sendReport=false;
              cluster = (int)strtol(temp[model][0]["report"][i]["cluster"],0,16);
              attribut = (int)temp[model][0]["report"][i]["attribut"];
              rType = (uint8_t)temp[model][0]["report"][i]["type"];
              rMin = (int)temp[model][0]["report"][i]["min"];
              rMax = (int)temp[model][0]["report"][i]["max"];
              rTimeout = (int)temp[model][0]["report"][i]["timeout"];
              rChange = (uint8_t)temp[model][0]["report"][i]["change"];

              if (model =="ZLinky_TIC")
              {
                const char *tmpMode; 
                tmpMode = temp[model][0]["report"][i]["mode"];
                if ((tmpMode != NULL) && (tmpMode[0] != '\0')) 
                {
                  char * pch;
                  pch = strtok ((char*)tmpMode,";");
                  while (pch != NULL)
                  {
                    if (atoi(pch) == ConfigGeneral.LinkyMode)
                    {
                      sendReport=true;
                      break;
                    }
                    pch = strtok (NULL, " ;");
                  }
                }else{
                  sendReport = true;
                }
              }else{
                sendReport = true;
              }

              if (sendReport)
              {
                SendConfigReport(shortAddr,cluster,attribut,rType,rMin,rMax,rTimeout,rChange);
              }
              
              i++; 
          }
        }
      }
      tpFile.close();
    } 
  }
}


void getBind(uint64_t mac, int device_id, String model)
{
  if (TemplateExist(device_id))
  {
    //String path = "/tp/"+(String)device_id+".json";
    const char* path ="/tp/";
    const char* extension =".json";
    char name_with_extension[64];
    strcpy(name_with_extension,path);
    strcat(name_with_extension,String(device_id).c_str());
    strcat(name_with_extension,extension);  

    File tpFile = LittleFS.open(name_with_extension, FILE_READ);
    if (!tpFile || tpFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      tpFile.close();
    }else
    {
      SpiRamJsonDocument temp(MAXHEAP);
      deserializeJson(temp,tpFile);
      const char *tmp; 
      
      if (temp.containsKey(model))
      {
        tmp = temp[model][0]["bind"];
        if ((tmp != NULL) && (tmp[0] != '\0')) 
        {
          char * pch;
          pch = strtok ((char*)tmp,";");
          while (pch != NULL)
          {
            SendBind(mac,atoi(pch));
            pch = strtok (NULL, " ;");
          }
        }
      }
      tpFile.close();
    }
  }
}

/*void readZigbeeDatas(uint8_t ShortAddr[2],uint8_t Cluster[2],uint8_t Attribute[2], uint8_t DataType, int len, char* datas)
{
  int cluster;
  int attribute;
  int shortaddr;
  
  cluster = Cluster[0]*256+Cluster[1];
  shortaddr = ShortAddr[0]*256+ShortAddr[1];
  attribute = Attribute[0]*256+Attribute[1];
  
  switch (cluster) 
  {
    case 0:     
      BasicManage(shortaddr,attribute,DataType,len,datas);
      break;
    case 1:     
      powerManage(shortaddr,attribute,DataType,len,datas);
      break;
    case 1026: //402 temperature
      temperatureManage(shortaddr,attribute,DataType,len,datas);
      break;
    case 1029: //402 humidity
      humidityManage(shortaddr,attribute,DataType,len,datas);
      break;
    case 1030: //406 occupancy
      OccupancyManage(shortaddr,attribute,DataType,len,datas);
      break;
    case 257: //0101 doorlock
      DoorlockManage(shortaddr,attribute,DataType,len,datas);
      break;
    case 6: // 0006 onoff
      OnoffManage(shortaddr,attribute,DataType,len,datas);
      break;
    case 65382: //FF66
      lixeeClusterManage(shortaddr,attribute,DataType,len,datas);
      break;
    case 1794: //0702 SM
      SimpleMeterManage(shortaddr,attribute,DataType,len,datas);
      break;
     case 2820: //0B04 ElectricalMeasurement
      ElectricalMeasurementManage(shortaddr,attribute,DataType,len,datas);
      break;
    default:
      defaultClusterManage(shortaddr,cluster,attribute,DataType,len,datas);
      break;
  
  }
}*/
void readZigbeeDatas(String filename,uint8_t Cluster[2],uint8_t Attribute[2], uint8_t DataType, int len, char* datas)
{
  int cluster;
  int attribute;
  
  cluster = Cluster[0]*256+Cluster[1];
  attribute = Attribute[0]*256+Attribute[1];
  
  switch (cluster) 
  {
    case 0:     
      BasicManage(filename,attribute,DataType,len,datas);
      break;
    case 1:     
      powerManage(filename,attribute,DataType,len,datas);
      break;
    case 1026: //402 temperature
      temperatureManage(filename,attribute,DataType,len,datas);
      break;
    case 1029: //402 humidity
      humidityManage(filename,attribute,DataType,len,datas);
      break;
    case 1030: //406 occupancy
      OccupancyManage(filename,attribute,DataType,len,datas);
      break;
    case 257: //0101 doorlock
      DoorlockManage(filename,attribute,DataType,len,datas);
      break;
    case 6: // 0006 onoff
      OnoffManage(filename,attribute,DataType,len,datas);
      break;
    case 65382: //FF66
      lixeeClusterManage(filename,attribute,DataType,len,datas);
      break;
    case 1794: //0702 SM
      SimpleMeterManage(filename,attribute,DataType,len,datas);
      break;
     case 2820: //0B04 ElectricalMeasurement
      ElectricalMeasurementManage(filename,attribute,DataType,len,datas);
      break;
    default:
      defaultClusterManage(filename,cluster,attribute,DataType,len,datas);
      break;
  
  }
}
