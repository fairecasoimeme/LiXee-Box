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
    tarif=atof(ConfigGeneral.tarifProd);
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
          sum += (attrVal*getTarif(String(section[attrId]).toInt(),"energy"))/1000 + ((attrVal/1000) * atof(ConfigGeneral.tarifCSPE));
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
        result+= "</svg><br><strong>"+tmp+" kVA</strong><br><i style='font-size:12px;'>(Subscribed Power)</i></span>";
      }
      tmp = String(strtol(device->getValue(std::string("0702"),std::string("0")).c_str(),0,16));
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
        //String result = String(strtol(device->getValue(std::string("0B04"),std::string(String(Attribute).c_str())).c_str(),0,16));
        result = String(strtol(device->getValue(std::string("0B04"),std::string(String(Attribute).c_str())).c_str(),0,16));
        result += ";";
        result += String(strtol(device->getValue(std::string("0B01"),std::string("13")).c_str(),0,16)*230);
        result += ";";
        result += device->getPowerW();
        
        return result;
      }
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

    long int tmp=0;
    long int maxVal =0;
    if (Time=="day")
    {
      for (const auto &graphEntry : hist.days.graph) {
        const PsString &Key = graphEntry.first;
        const ValueMap &valMap   = graphEntry.second;

        long sum=0;
        for (const auto &attrPair : valMap.attributes) {
            long attrVal = attrPair.second;
            sum +=attrVal;
        }
        maxVal = max(sum,maxVal);

        if (memcmp(Day.c_str(),Key.c_str(),2)==0)
        {
          tmp=sum;
        }
      }  

      if (tmp>0)
      {
        result= String(tmp)+";"+String(maxVal);
        return result;
      }

      return result;
      

    }else if (Time=="month")
    {
      for (const auto &graphEntry : hist.months.graph) {
        const PsString &Key = graphEntry.first;
        const ValueMap &valMap   = graphEntry.second;

        long sum=0;
        for (const auto &attrPair : valMap.attributes) {
            long attrVal = attrPair.second;
            sum +=attrVal;
        }
        maxVal = max(sum,maxVal);

        if (memcmp(Month.c_str(),Key.c_str(),2)==0)
        {
          tmp=sum;
        }

      }  
         
      if (tmp>0)
      {
        result= String(tmp)+";"+String(maxVal);
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
            sum +=attrVal;
        }
        maxVal = max(sum,maxVal);

        if (memcmp(Year.c_str(),Key.c_str(),4)==0)
        {
          tmp=sum;
        }

      }  
         
      if (tmp>0)
      {
        result= String(tmp)+";"+String(maxVal);
        return result;
      }
    }
  }
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
            tarifEuros+=attrVal * getTarif(String(section[attrId]).toInt(),"energy")/1000;
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
          result +=String(tarifEuros) +" €";
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
          sum +=attrVal;
          if (attrId>0)
          {          
            tmpEuros+=attrVal * getTarif(String(section[attrId]).toInt(),"energy")/1000;
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
          sum +=attrVal;
          if (attrId>0)
          {
            tmpEuros+=attrVal * getTarif(String(section[attrId]).toInt(),"energy")/1000;
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
          sum +=attrVal;
          if (attrId>0)
          {
            tmpEuros+=attrVal * getTarif(String(section[attrId]).toInt(),"energy")/1000;
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
      result +=op+trend+ " VA ";
    result+="</span>";
    result+="<br><span style='font-size:12px;'><strong>Min:</strong> ";
      result+=minVal;
      result+=" VA <br> <strong>Max :</strong> ";
      result+=maxVal;
      result+=" VA";
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
        String result = String(strtol(device->getValue(std::string("0B04"),std::string(String(Attribute).c_str())).c_str(),0,16));
        result +=";";
        result += String(strtol(device->getValue(std::string("0B01"),std::string("13")).c_str(),0,16)*230);
        result +=";0;"; 
        result += device->getPowerW()+" W";
        return result;
      }
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
              sum +=attrVal;
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
              sum +=attrVal;
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
              sum +=attrVal;
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
