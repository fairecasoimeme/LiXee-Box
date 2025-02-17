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
  }
  return tarif;
}

float getTarifPower(String IEEE, int power)
{
  //String path = "/db/nrg_"+IEEE+".json";
  const char* path ="/db/nrg_";
  const char* extension =".json";
  char name_with_extension[64];
  strcpy(name_with_extension,path);
  strcat(name_with_extension,IEEE.c_str());
  strcat(name_with_extension,extension);

  File DeviceFile = LittleFS.open(name_with_extension, FILE_READ);
  if (!DeviceFile|| DeviceFile.isDirectory()) {
    DeviceFile.close();
    DEBUG_PRINTLN(F("failed open"));
  }else{
    DynamicJsonDocument temp(MAXHEAP);
    deserializeJson(temp,DeviceFile);
    //JsonObject root = temp["hours"].as<JsonObject>();
    DeviceFile.close();
    
    int cntsection;
    int arrayLength = sizeof(section) / sizeof(section[0]);
    float euro=0;
    for (cntsection=0 ; cntsection <arrayLength; cntsection++)
    {   
      if (temp["days"]["graph"][Day][section[cntsection]].as<int>()>0)
      {       
        euro += (temp["days"]["graph"][Day][section[cntsection]].as<int>()*getTarif(String(section[cntsection]).toInt(),"energy")/1000) + ((temp["days"]["graph"][Day][section[cntsection]].as<int>()/1000) * atof(ConfigGeneral.tarifCSPE));
      }

    }
    return euro;
  }
  return 0;
}

String getTrendEnergyEuros(String IEEE)
{
  //String path = "/db/nrg_"+IEEE+".json";
  const char* path ="/db/nrg_";
  const char* extension =".json";
  char name_with_extension[64];
  strcpy(name_with_extension,path);
  strcat(name_with_extension,IEEE.c_str());
  strcat(name_with_extension,extension);
  String result="";
  String sep="";
  File DeviceFile = LittleFS.open(name_with_extension, FILE_READ);
  if (!DeviceFile|| DeviceFile.isDirectory()) {
    DEBUG_PRINTLN(F("failed open"));
  }else{
    DynamicJsonDocument temp(MAXHEAP);
    deserializeJson(temp,DeviceFile);
    //JsonObject root = temp["hours"].as<JsonObject>();
    DeviceFile.close();
    
    result += F("{");
    int i=0;
    int cntsection;
    int arrayLength = sizeof(section) / sizeof(section[0]);
    for (cntsection=0 ; cntsection <arrayLength; cntsection++)
    {   
      if (temp["trend"][section[cntsection]].as<int>()>0)
      {       
        float euro = (temp["trend"][section[cntsection]].as<int>()*getTarif(String(section[cntsection]).toInt(),"energy"))/1000;
        if (i>0){sep=",";}else{sep="";}
        result+=sep+String(section[cntsection])+":"+temp["trend"][section[cntsection]].as<String>()+" Wh ("+String(euro)+"€)";
        i++;
      }
    }
    result +=F("}");
  }
  return result;
}



String getLinkyDatas(String IEEE)
{
  //String path = "/db/"+(String)IEEE+".json";
  const char* path ="/db/";
  const char* extension =".json";
  char name_with_extension[64];
  strcpy(name_with_extension,path);
  strcat(name_with_extension,IEEE.c_str());
  strcat(name_with_extension,extension);
  String result="";
  File DeviceFile = LittleFS.open(name_with_extension, FILE_READ);
  if (!DeviceFile|| DeviceFile.isDirectory()) {
    DEBUG_PRINTLN(F("failed open"));
    return result;
  }else{
    DynamicJsonDocument temp(4096);
    deserializeJson(temp,DeviceFile);
    result+="<div id='datasLinky' style='display:inline-block;float:left;'>";
    if (temp["0702"]["776"])
    {
      result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
      result+=F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-upc-scan' viewBox='0 0 16 16'>");
       result+=F("<path d='M1.5 1a.5.5 0 0 0-.5.5v3a.5.5 0 0 1-1 0v-3A1.5 1.5 0 0 1 1.5 0h3a.5.5 0 0 1 0 1zM11 .5a.5.5 0 0 1 .5-.5h3A1.5 1.5 0 0 1 16 1.5v3a.5.5 0 0 1-1 0v-3a.5.5 0 0 0-.5-.5h-3a.5.5 0 0 1-.5-.5M.5 11a.5.5 0 0 1 .5.5v3a.5.5 0 0 0 .5.5h3a.5.5 0 0 1 0 1h-3A1.5 1.5 0 0 1 0 14.5v-3a.5.5 0 0 1 .5-.5m15 0a.5.5 0 0 1 .5.5v3a1.5 1.5 0 0 1-1.5 1.5h-3a.5.5 0 0 1 0-1h3a.5.5 0 0 0 .5-.5v-3a.5.5 0 0 1 .5-.5M3 4.5a.5.5 0 0 1 1 0v7a.5.5 0 0 1-1 0zm2 0a.5.5 0 0 1 1 0v7a.5.5 0 0 1-1 0zm2 0a.5.5 0 0 1 1 0v7a.5.5 0 0 1-1 0zm2 0a.5.5 0 0 1 .5-.5h1a.5.5 0 0 1 .5.5v7a.5.5 0 0 1-.5.5h-1a.5.5 0 0 1-.5-.5zm3 0a.5.5 0 0 1 1 0v7a.5.5 0 0 1-1 0z'/>");
      result+="</svg><br><strong>"+String((int)strtol(temp["0702"]["776"],0,16))+"</strong><br><i style='font-size:12px;'>(Serial Number)</i></span>";
    }
    if (temp["FF66"]["768"])
    {
      result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
      result+=F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-box-seam' viewBox='0 0 16 16'>");
       result+=F("<path d='M8.186 1.113a.5.5 0 0 0-.372 0L1.846 3.5l2.404.961L10.404 2zm3.564 1.426L5.596 5 8 5.961 14.154 3.5zm3.25 1.7-6.5 2.6v7.922l6.5-2.6V4.24zM7.5 14.762V6.838L1 4.239v7.923zM7.443.184a1.5 1.5 0 0 1 1.114 0l7.129 2.852A.5.5 0 0 1 16 3.5v8.662a1 1 0 0 1-.629.928l-7.185 2.874a.5.5 0 0 1-.372 0L.63 13.09a1 1 0 0 1-.63-.928V3.5a.5.5 0 0 1 .314-.464z'/>");
      result+="</svg><br><strong>"+getLinkyMode((int)temp["FF66"]["768"])+"</strong></span>";
    }
    if (temp["FF66"]["0"])
    {
      String abo = temp["FF66"]["0"].as<String>();
      
      result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
      result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-layout-text-window' viewBox='0 0 16 16'>");
      result+= F("<path d='M3 6.5a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m0 3a.5.5 0 0 1 .5-.5h5a.5.5 0 0 1 0 1h-5a.5.5 0 0 1-.5-.5m.5 2.5a.5.5 0 0 0 0 1h5a.5.5 0 0 0 0-1z'/>");
      result+= F("<path d='M2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2zm12 1a1 1 0 0 1 1 1v1H1V2a1 1 0 0 1 1-1zm1 3v10a1 1 0 0 1-1 1h-2V4zm-4 0v11H2a1 1 0 0 1-1-1V4z'/>");
      result+= "</svg><br><strong>"+abo+"</strong><br><i style='font-size:12px;'>(Subscription)</i></span>";
    }
    if (temp["FF66"]["512"]!="")
    {

      String currentTarif = temp["FF66"]["512"].as<String>();
      result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
      result+= F(" <svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-clock-history' viewBox='0 0 16 16'>");
      result+= F("<path d='M8.515 1.019A7 7 0 0 0 8 1V0a8 8 0 0 1 .589.022zm2.004.45a7 7 0 0 0-.985-.299l.219-.976q.576.129 1.126.342zm1.37.71a7 7 0 0 0-.439-.27l.493-.87a8 8 0 0 1 .979.654l-.615.789a7 7 0 0 0-.418-.302zm1.834 1.79a7 7 0 0 0-.653-.796l.724-.69q.406.429.747.91zm.744 1.352a7 7 0 0 0-.214-.468l.893-.45a8 8 0 0 1 .45 1.088l-.95.313a7 7 0 0 0-.179-.483m.53 2.507a7 7 0 0 0-.1-1.025l.985-.17q.1.58.116 1.17zm-.131 1.538q.05-.254.081-.51l.993.123a8 8 0 0 1-.23 1.155l-.964-.267q.069-.247.12-.501m-.952 2.379q.276-.436.486-.908l.914.405q-.24.54-.555 1.038zm-.964 1.205q.183-.183.35-.378l.758.653a8 8 0 0 1-.401.432z'/>");
      result+= F("<path d='M8 1a7 7 0 1 0 4.95 11.95l.707.707A8.001 8.001 0 1 1 8 0z'/>");
      result+= F("<path d='M7.5 3a.5.5 0 0 1 .5.5v5.21l3.248 1.856a.5.5 0 0 1-.496.868l-3.5-2A.5.5 0 0 1 7 9V3.5a.5.5 0 0 1 .5-.5'/>");      
      result+= "</svg><br><strong>"+currentTarif+"</strong><br><i style='font-size:12px;'>(Current price)</i></span>";
    }
    if (temp["0702"]["32"]!="")
    {
      String currentTarif =temp["0702"]["32"].as<String>();
      result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
      result+= F(" <svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-clock-history' viewBox='0 0 16 16'>");
      result+= F("<path d='M8.515 1.019A7 7 0 0 0 8 1V0a8 8 0 0 1 .589.022zm2.004.45a7 7 0 0 0-.985-.299l.219-.976q.576.129 1.126.342zm1.37.71a7 7 0 0 0-.439-.27l.493-.87a8 8 0 0 1 .979.654l-.615.789a7 7 0 0 0-.418-.302zm1.834 1.79a7 7 0 0 0-.653-.796l.724-.69q.406.429.747.91zm.744 1.352a7 7 0 0 0-.214-.468l.893-.45a8 8 0 0 1 .45 1.088l-.95.313a7 7 0 0 0-.179-.483m.53 2.507a7 7 0 0 0-.1-1.025l.985-.17q.1.58.116 1.17zm-.131 1.538q.05-.254.081-.51l.993.123a8 8 0 0 1-.23 1.155l-.964-.267q.069-.247.12-.501m-.952 2.379q.276-.436.486-.908l.914.405q-.24.54-.555 1.038zm-.964 1.205q.183-.183.35-.378l.758.653a8 8 0 0 1-.401.432z'/>");
      result+= F("<path d='M8 1a7 7 0 1 0 4.95 11.95l.707.707A8.001 8.001 0 1 1 8 0z'/>");
      result+= F("<path d='M7.5 3a.5.5 0 0 1 .5.5v5.21l3.248 1.856a.5.5 0 0 1-.496.868l-3.5-2A.5.5 0 0 1 7 9V3.5a.5.5 0 0 1 .5-.5'/>");
      result+= "</svg><br><strong>"+currentTarif+"</strong><br><i style='font-size:12px;'>(Current price)</i></span>";
    }
    if (temp["0B01"]["13"])
    {
      result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
      result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-lightning-charge-fill' viewBox='0 0 16 16'>");
      result+= F("<path d='M11.251.068a.5.5 0 0 1 .227.58L9.677 6.5H13a.5.5 0 0 1 .364.843l-8 8.5a.5.5 0 0 1-.842-.49L6.323 9.5H3a.5.5 0 0 1-.364-.843l8-8.5a.5.5 0 0 1 .615-.09z'/>");
      result+= "</svg><br><strong>"+String((int)strtol(temp["0B01"]["13"],0,16))+" kVA</strong><br><i style='font-size:12px;'>(Subscribed Power)</i></span>";
    }

    int index;
    if (temp["0702"]["0"])
    {
      index = (int)strtol(temp["0702"]["0"],0,16);
      if (index > 0)
      {
        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-speedometer' viewBox='0 0 16 16'>");
        result+= F("<path d='M8 2a.5.5 0 0 1 .5.5V4a.5.5 0 0 1-1 0V2.5A.5.5 0 0 1 8 2M3.732 3.732a.5.5 0 0 1 .707 0l.915.914a.5.5 0 1 1-.708.708l-.914-.915a.5.5 0 0 1 0-.707M2 8a.5.5 0 0 1 .5-.5h1.586a.5.5 0 0 1 0 1H2.5A.5.5 0 0 1 2 8m9.5 0a.5.5 0 0 1 .5-.5h1.5a.5.5 0 0 1 0 1H12a.5.5 0 0 1-.5-.5m.754-4.246a.39.39 0 0 0-.527-.02L7.547 7.31A.91.91 0 1 0 8.85 8.569l3.434-4.297a.39.39 0 0 0-.029-.518z'/>");
        result+= F("<path fill-rule='evenodd' d='M6.664 15.889A8 8 0 1 1 9.336.11a8 8 0 0 1-2.672 15.78zm-4.665-4.283A11.95 11.95 0 0 1 8 10c2.186 0 4.236.585 6.001 1.606a7 7 0 1 0-12.002 0'/>");
        result+= "</svg><br><strong>"+String(index)+" Wh </strong><br><i style='font-size:12px;'>(Index 1)</i></span>";
      }
    }
    if (temp["0702"]["256"])
    {
      index = (int)strtol(temp["0702"]["256"],0,16);
      if (index > 0)
      {
        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-speedometer' viewBox='0 0 16 16'>");
        result+= F("<path d='M8 2a.5.5 0 0 1 .5.5V4a.5.5 0 0 1-1 0V2.5A.5.5 0 0 1 8 2M3.732 3.732a.5.5 0 0 1 .707 0l.915.914a.5.5 0 1 1-.708.708l-.914-.915a.5.5 0 0 1 0-.707M2 8a.5.5 0 0 1 .5-.5h1.586a.5.5 0 0 1 0 1H2.5A.5.5 0 0 1 2 8m9.5 0a.5.5 0 0 1 .5-.5h1.5a.5.5 0 0 1 0 1H12a.5.5 0 0 1-.5-.5m.754-4.246a.39.39 0 0 0-.527-.02L7.547 7.31A.91.91 0 1 0 8.85 8.569l3.434-4.297a.39.39 0 0 0-.029-.518z'/>");
        result+= F("<path fill-rule='evenodd' d='M6.664 15.889A8 8 0 1 1 9.336.11a8 8 0 0 1-2.672 15.78zm-4.665-4.283A11.95 11.95 0 0 1 8 10c2.186 0 4.236.585 6.001 1.606a7 7 0 1 0-12.002 0'/>");
        result+= "</svg><br><strong>"+String(index)+" Wh </strong><br><i style='font-size:12px;'>(Index 1)</i></span>";
      }
    }
    if (temp["0702"]["258"])
    {
     index = (int)strtol(temp["0702"]["258"],0,16);
     if (index > 0)
     {
        result+=F("<span style='display:inline-block;float:left;width:150px;text-align:center;height:120px;'>");
        result+= F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-speedometer' viewBox='0 0 16 16'>");
        result+= F("<path d='M8 2a.5.5 0 0 1 .5.5V4a.5.5 0 0 1-1 0V2.5A.5.5 0 0 1 8 2M3.732 3.732a.5.5 0 0 1 .707 0l.915.914a.5.5 0 1 1-.708.708l-.914-.915a.5.5 0 0 1 0-.707M2 8a.5.5 0 0 1 .5-.5h1.586a.5.5 0 0 1 0 1H2.5A.5.5 0 0 1 2 8m9.5 0a.5.5 0 0 1 .5-.5h1.5a.5.5 0 0 1 0 1H12a.5.5 0 0 1-.5-.5m.754-4.246a.39.39 0 0 0-.527-.02L7.547 7.31A.91.91 0 1 0 8.85 8.569l3.434-4.297a.39.39 0 0 0-.029-.518z'/>");
        result+= F("<path fill-rule='evenodd' d='M6.664 15.889A8 8 0 1 1 9.336.11a8 8 0 0 1-2.672 15.78zm-4.665-4.283A11.95 11.95 0 0 1 8 10c2.186 0 4.236.585 6.001 1.606a7 7 0 1 0-12.002 0'/>");
        result+= "</svg><br><strong>"+String(index)+" Wh </strong><br><i style='font-size:12px;'>(Index 2)</i></span>";
     }
    }
    if (temp["0702"]["260"])
    {
      index = (int)strtol(temp["0702"]["260"],0,16);
      if (index > 0)
      result+="<br><strong>Index (BBRHCJW/EASF03): </strong>"+String(index)+" Wh <span id='index4'></span>";
    }
    if (temp["0702"]["262"])
    {
      index = (int)strtol(temp["0702"]["262"],0,16);
      if (index > 0)
      result+="<br><strong>Index (BBRHPJW/EASF04): </strong>"+String(index)+" Wh <span id='index5'></span>";
    }
    if (temp["0702"]["264"])
    {
      index = (int)strtol(temp["0702"]["264"],0,16);
      if (index > 0)
      result+="<br><strong>Index (BBRHCJR/EASF05): </strong>"+String(index)+" Wh <span id='index6'></span>";
    }
    if (temp["0702"]["266"])
    {
      index = (int)strtol(temp["0702"]["266"],0,16);
      if (index > 0)
      result+="<br><strong>Index (BBRHPJR/EASF06): </strong>"+String(index)+" Wh <span id='index7'></span>";
    }
    if (temp["0702"]["268"])
    {
      index = (int)strtol(temp["0702"]["268"],0,16);
      if (index > 0)
      result+="<br><strong>Index (EASF07): </strong>"+String(index)+" Wh <span id='index8'></span>";
    }
    if (temp["0702"]["270"])
    {
      index = (int)strtol(temp["0702"]["270"],0,16);
      if (index > 0)
      result+="<br><strong>Index (EASF08): </strong>"+String(index)+" Wh <span id='index9'></span>";
    }
    if (temp["0702"]["272"])
    {
      index = (int)strtol(temp["0702"]["272"],0,16);
      if (index > 0)
      result+="<br><strong>Index (EASF09): </strong>"+String(index)+" Wh <span id='index10'></span>";
    }
    if (temp["0702"]["274"])
    {
      index = (int)strtol(temp["0702"]["274"],0,16);
      if (index > 0)
      result+="<br><strong>Index (EASF10): </strong>"+String(index)+" Wh <span id='index11'></span>";
    }
    result+="</div>";
    return result;
  }
  return result;
}


String getPowerGaugeAbo(String IEEE, String Attribute, String Time)
{
  String result="error";
  
  if (Time=="hour")
  {
    //String path = "/db/"+(String)IEEE+".json";
    const char* path ="/db/";
    const char* extension =".json";
    char name_with_extension[64];
    strcpy(name_with_extension,path);
    strcat(name_with_extension,IEEE.c_str());
    strcat(name_with_extension,extension);
    File DeviceFile = LittleFS.open(name_with_extension, FILE_READ);
    if (!DeviceFile|| DeviceFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      return "error";
    }else{
  
      DynamicJsonDocument temp(MAXHEAP/2);
      deserializeJson(temp,DeviceFile);
      DeviceFile.close();
      if ((temp["0B04"][Attribute]) && ( temp["0B01"]["13"]))
      {
        result = String(strtol(temp["0B04"][Attribute],0,16))+";"+String(strtol(temp["0B01"]["13"],0,16)*230);
      }
    }
  }else
  {
    //String path = "/db/nrg_"+IEEE+".json";
    const char* path ="/db/nrg_";
    const char* extension =".json";
    char name_with_extension[64];
    strcpy(name_with_extension,path);
    strcat(name_with_extension,IEEE.c_str());
    strcat(name_with_extension,extension);
    String sep="";
    File DeviceFile = LittleFS.open(name_with_extension, FILE_READ);
    if (!DeviceFile|| DeviceFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      return "error";
    }else{
      DynamicJsonDocument temp(MAXHEAP);
      deserializeJson(temp,DeviceFile);
      JsonObject root;
      if (Time =="day")
      {
        root = temp["days"]["graph"].as<JsonObject>();
      }else if (Time =="month")
      {
        root = temp["months"]["graph"].as<JsonObject>();
      }else if (Time =="year")
      {
        root = temp["years"]["graph"].as<JsonObject>();
      }

      DeviceFile.close();
      
      int energyTemp=0;
      long int tmp=0;
      int cntsection;
      int arrayLength = sizeof(section) / sizeof(section[0]);
       
      long int maxVal =0;
      for (JsonPair j : root)
      {
        long int sum=0;

        for (cntsection=0 ; cntsection <arrayLength; cntsection++)
        {
          if (j.value()[section[cntsection]].as<long int>() >0)
          {   
            sum+=j.value()[section[cntsection]].as<long int>();
          }
        }
        maxVal = max(sum,maxVal);

        if (Time=="day")  
        {     
          if (Day.toInt() == atoi(j.key().c_str()))
          {
            tmp=sum;
          }
          DEBUG_PRINTLN();
        }else if (Time=="month")  
        {   
          if (Month.toInt() == atoi(j.key().c_str()))
          {  
            tmp=sum;
          }
        }else if (Time=="year")  
        {  
          if (Year.toInt() == atoi(j.key().c_str()))
          {    
            tmp=sum;
          }
        }
      }     
      if (tmp>0)
      {
        energyTemp += tmp;
      }
      result= String(energyTemp)+";"+String(maxVal);
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
    //String path = "/db/pwr_"+IEEE+".json";
    const char* path ="/db/pwr_";
    const char* extension =".json";
    char name_with_extension[64];
    strcpy(name_with_extension,path);
    strcat(name_with_extension,IEEE.c_str());
    strcat(name_with_extension,extension);
    String op;
    File DeviceFile = LittleFS.open(name_with_extension, FILE_READ);
    if (!DeviceFile|| DeviceFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      return "";
    }else
    {
      DynamicJsonDocument temp(MAXHEAP);
      deserializeJson(temp,DeviceFile);
      DeviceFile.close();
      //DEBUG_PRINTLN(temp[Attribute]["trend"].as<int>());
      if (!(temp[Attribute]["trend"]).isNull())
      {
        //result ="<div id='trendIcon' style='width:33%;padding-top:15px;height:200px;display:inline-box;float:left;'>";
        if (temp[Attribute]["trend"].as<int>()>0)
        {
          trendColor="style='color:orange;'";
          trendIcon += "<svg xmlns='http://www.w3.org/2000/svg' width='96' height='96' fill='currentColor' class='bi bi-0-circle' style='color:orange;' viewBox='0 0 20 20'>";
          trendIcon +="    <path fill-rule='evenodd' d='M14 2.5a.5.5 0 0 0-.5-.5h-6a.5.5 0 0 0 0 1h4.793L2.146 13.146a.5.5 0 0 0 .708.708L13 3.707V8.5a.5.5 0 0 0 1 0v-6z'/>";
          trendIcon +="</svg>";
          op="+";
        }else if (temp[Attribute]["trend"].as<int>()<0){
          trendColor="style='color:green;'";
          trendIcon += "<svg xmlns='http://www.w3.org/2000/svg' width='96' height='96' fill='currentColor' class='bi bi-0-circle' style='color:green;' viewBox='0 0 20 20'>";
          trendIcon +="  <path fill-rule='evenodd' d='M14 13.5a.5.5 0 0 1-.5.5h-6a.5.5 0 0 1 0-1h4.793L2.146 2.854a.5.5 0 1 1 .708-.708L13 12.293V7.5a.5.5 0 0 1 1 0v6z'/>";
          trendIcon +="</svg>";
          op="";
        }else{
          trendColor="style='color:grey;'";
          trendIcon += "<svg xmlns='http://www.w3.org/2000/svg' width='96' height='96' fill='currentColor' class='bi bi-0-circle' style='color:grey;' viewBox='0 0 20 20'>";
          trendIcon +="  <path fill-rule='evenodd' d='M1 8a.5.5 0 0 1 .5-.5h11.793l-3.147-3.146a.5.5 0 0 1 .708-.708l4 4a.5.5 0 0 1 0 .708l-4 4a.5.5 0 0 1-.708-.708L13.293 8.5H1.5A.5.5 0 0 1 1 8z'/>";
          trendIcon +="</svg>";
          op="";
        }
       // result+= "</div>";
        result +="<div style='text-align:left;'>";
          result +="<div style='float:left;display:inline-block;width:72px;'><svg xmlns='http://www.w3.org/2000/svg' width='72px' height='72px' "+trendColor+" fill='currentColor' class='bi bi-lightning-fill' viewBox='0 0 16 16'>";
            result +="<path d='M5.52.359A.5.5 0 0 1 6 0h4a.5.5 0 0 1 .474.658L8.694 6H12.5a.5.5 0 0 1 .395.807l-7 9a.5.5 0 0 1-.873-.454L6.823 9.5H3.5a.5.5 0 0 1-.48-.641l2.5-8.5z'/>";
          result +="</svg></div>";
          result+="<div style='display:inline-box;height:100px;'><span style='font-size:24px;'>";
            result +=op+temp[Attribute]["trend"].as<String>()+ " VA ";
          result+="</span>";
          result+="<br><span style='font-size:12px;'>(<strong>Min:</strong> ";
            result+=temp[Attribute]["min"].as<String>();
            result+=" VA / <strong>Max :</strong> ";
            result+=temp[Attribute]["max"].as<String>();
            result+=" VA)";
          result+="</span></div>";   
          result+="<br><div style='display:inline-block;width:72px;float:left;'><svg xmlns='http://www.w3.org/2000/svg' width='72px' height='72px' fill='currentColor' class='bi bi-cash-stack' viewBox='0 0 16 16'>";
            result+="<path d='M1 3a1 1 0 0 1 1-1h12a1 1 0 0 1 1 1H1zm7 8a2 2 0 1 0 0-4 2 2 0 0 0 0 4z'/>";
            result+="<path d='M0 5a1 1 0 0 1 1-1h14a1 1 0 0 1 1 1v8a1 1 0 0 1-1 1H1a1 1 0 0 1-1-1V5zm3 0a2 2 0 0 1-2 2v4a2 2 0 0 1 2 2h10a2 2 0 0 1 2-2V7a2 2 0 0 1-2-2H3z'/>";
            result+="</svg>";       
          result+="</div>";
          result+="<div style='display:inline-block;line-height:100px;vertical-align:middle'><span style='font-size:24px;padding-left:20px;'>";
            result +=String(getTarifPower(IEEE,temp[Attribute]["last"].as<int>())) +"€";
          result+="</span></div>";
        result += "</div>";
        
      }
      
    }
  }else
  {
    //String path = "/db/nrg_"+IEEE+".json";
    const char* path ="/db/nrg_";
    const char* extension =".json";
    char name_with_extension[64];
    strcpy(name_with_extension,path);
    strcat(name_with_extension,IEEE.c_str());
    strcat(name_with_extension,extension);
    String sep="";
    File DeviceFile = LittleFS.open(name_with_extension, FILE_READ);
    if (!DeviceFile|| DeviceFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      return "error";
    }else{
      DynamicJsonDocument temp(MAXHEAP);
      deserializeJson(temp,DeviceFile);
      JsonObject root;
      if (Time =="day")
      {
        root = temp["days"]["graph"].as<JsonObject>();
      }else if (Time =="month")
      {
        root = temp["months"]["graph"].as<JsonObject>();
      }else if (Time =="year")
      {
        root = temp["years"]["graph"].as<JsonObject>();
      }

      DeviceFile.close();
      
      int energyTemp=0;
      long int tmp=0;
      long int lastTime=0;
      float tarifEuro=0;
      long int maxVal =0;

      int cntsection;
      int arrayLength = sizeof(section) / sizeof(section[0]);
      for (JsonPair j : root)
      {
        long int sum=0;
        float tmpEuros=0;
        for (cntsection=0 ; cntsection <arrayLength; cntsection++)
        {
          if (j.value()[section[cntsection]].as<long int>() !=0)
          {   
            sum+=j.value()[section[cntsection]].as<long int>();
            tmpEuros+=(j.value()[section[cntsection]].as<long int>() * getTarif(String(section[cntsection]).toInt(),"energy"))/1000;
            tmpEuros+=(j.value()[section[cntsection]].as<long int>()/1000) * atof(ConfigGeneral.tarifCSPE);
          }
        }
        maxVal = max(sum,maxVal);

        if (Time=="day")  
        {     
          if (Day.toInt() == atoi(j.key().c_str()))
          {
            tmp=sum;
            tarifEuro=tmpEuros;
          }
          if (Yesterday.toInt() == atoi(j.key().c_str()))
          {
            lastTime=sum;
          }
        }else if (Time=="month")  
        {   
          if (Month.toInt() == atoi(j.key().c_str()))
          {  
            tmp=sum;
            tarifEuro=tmpEuros;
          }
          if ((Month.toInt()-1) == atoi(j.key().c_str()))
          {  
            lastTime=sum;
          }
        }else if (Time=="year")  
        {  
          if (Year.toInt() == atoi(j.key().c_str()))
          {    
            tmp=sum;
            tarifEuro=tmpEuros;
          }
          if ((Year.toInt()-1) == atoi(j.key().c_str()))
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
        trendColor="style='color:orange;'";
        trendIcon += "<svg xmlns='http://www.w3.org/2000/svg' width='96' height='96' fill='currentColor' class='bi bi-0-circle' style='color:orange;' viewBox='0 0 20 20'>";
        trendIcon +="    <path fill-rule='evenodd' d='M14 2.5a.5.5 0 0 0-.5-.5h-6a.5.5 0 0 0 0 1h4.793L2.146 13.146a.5.5 0 0 0 .708.708L13 3.707V8.5a.5.5 0 0 0 1 0v-6z'/>";
        trendIcon +="</svg>";
        op="+";
        trend=energyTemp - lastTime;
      }else if ( energyTemp < lastTime)
      {
        trendColor="style='color:green;'";
        trendIcon += "<svg xmlns='http://www.w3.org/2000/svg' width='96' height='96' fill='currentColor' class='bi bi-0-circle' style='color:green;' viewBox='0 0 20 20'>";
        trendIcon +="  <path fill-rule='evenodd' d='M14 13.5a.5.5 0 0 1-.5.5h-6a.5.5 0 0 1 0-1h4.793L2.146 2.854a.5.5 0 1 1 .708-.708L13 12.293V7.5a.5.5 0 0 1 1 0v6z'/>";
        trendIcon +="</svg>";
        op="-";
        trend=lastTime - energyTemp;
      }else{
        trendColor="style='color:grey;'";
        trendIcon += "<svg xmlns='http://www.w3.org/2000/svg' width='96' height='96' fill='currentColor' class='bi bi-0-circle' style='color:grey;' viewBox='0 0 20 20'>";
        trendIcon +="  <path fill-rule='evenodd' d='M1 8a.5.5 0 0 1 .5-.5h11.793l-3.147-3.146a.5.5 0 0 1 .708-.708l4 4a.5.5 0 0 1 0 .708l-4 4a.5.5 0 0 1-.708-.708L13.293 8.5H1.5A.5.5 0 0 1 1 8z'/>";
        trendIcon +="</svg>";
        op="";
        trend=0;
      }
       result +="<div style='text-align:left;'>";
          result +="<div style='float:left;display:inline-block;width:72px;'><svg xmlns='http://www.w3.org/2000/svg' width='72px' height='72px' "+trendColor+" fill='currentColor' class='bi bi-lightning-fill' viewBox='0 0 16 16'>";
            result +="<path d='M5.52.359A.5.5 0 0 1 6 0h4a.5.5 0 0 1 .474.658L8.694 6H12.5a.5.5 0 0 1 .395.807l-7 9a.5.5 0 0 1-.873-.454L6.823 9.5H3.5a.5.5 0 0 1-.48-.641l2.5-8.5z'/>";
          result +="</svg></div>";
          result+="<div style='display:inline-box;height:100px;'><span style='font-size:24px;'>";
            result +=op+String(trend)+ " Wh ";
          result+="</span></div>";
          
          result+="<br><div style='display:inline-block;width:72px;float:left;'><svg xmlns='http://www.w3.org/2000/svg' width='72px' height='72px' fill='currentColor' class='bi bi-cash-stack' viewBox='0 0 16 16'>";
            result+="<path d='M1 3a1 1 0 0 1 1-1h12a1 1 0 0 1 1 1H1zm7 8a2 2 0 1 0 0-4 2 2 0 0 0 0 4z'/>";
            result+="<path d='M0 5a1 1 0 0 1 1-1h14a1 1 0 0 1 1 1v8a1 1 0 0 1-1 1H1a1 1 0 0 1-1-1V5zm3 0a2 2 0 0 1-2 2v4a2 2 0 0 1 2 2h10a2 2 0 0 1 2-2V7a2 2 0 0 1-2-2H3z'/>";
            result+="</svg>";       
          result+="</div>";
          result+="<div style='display:inline-block;line-height:100px;vertical-align:middle'><span style='font-size:24px;padding-left:20px;'>";
            result +=String(tarifEuro) +"€";
          result+="</span></div>";
        result += "</div>";
    }
  }
  return result;
}

String getLastValuePower(String IEEE,String Attribute, String Time)
{

  String result="";
  if (Time=="hour")
  {
    //String path = "/db/"+(String)IEEE+".json";
    const char* path ="/db/";
    const char* extension =".json";
    char name_with_extension[64];
    strcpy(name_with_extension,path);
    strcat(name_with_extension,IEEE.c_str());
    strcat(name_with_extension,extension);
    File DeviceFile = LittleFS.open(name_with_extension, FILE_READ);
    if (!DeviceFile|| DeviceFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      return "";
    }else{
      DynamicJsonDocument temp(4096);
      deserializeJson(temp,DeviceFile);
      DeviceFile.close();
      if ((temp["0B04"][Attribute]))
      {
        result = String(strtol(temp["0B04"][Attribute],0,16));
      }     
    }
  }else
  {
    //String path = "/db/nrg_"+IEEE+".json";
    const char* path ="/db/nrg_";
    const char* extension =".json";
    char name_with_extension[64];
    strcpy(name_with_extension,path);
    strcat(name_with_extension,IEEE.c_str());
    strcat(name_with_extension,extension);
    File DeviceFile = LittleFS.open(name_with_extension, FILE_READ);
    if (!DeviceFile|| DeviceFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      return "error";
    }else{
      DynamicJsonDocument temp(MAXHEAP);
      deserializeJson(temp,DeviceFile);
      JsonObject root;
      if (Time=="day")
      {
        root = temp["days"]["graph"].as<JsonObject>();
      }else if (Time=="month")
      {
        root = temp["months"]["graph"].as<JsonObject>();
      }else if (Time=="year")
      {
        root = temp["years"]["graph"].as<JsonObject>();
      }
      DeviceFile.close();

      int cntsection;
      int arrayLength = sizeof(section) / sizeof(section[0]);
      int energyTemp=0;
      int tmp=0;

      for (JsonPair j : root)
      {
        long sum=0;

        for (cntsection=0 ; cntsection <arrayLength; cntsection++)
        {
          if (j.value()[section[cntsection]].as<long int>() >0)
          {   
            sum+=j.value()[section[cntsection]].as<long int>();
          }
        }

        if (Time=="day")  
        {     
          if (Day.toInt() == atoi(j.key().c_str()))
          {
            tmp=sum;
          }
        }else if (Time=="month")  
        {   
          if (Month.toInt() == atoi(j.key().c_str()))
          {  
            tmp=sum;
          }
        }else if (Time=="year")  
        {  
          if (Year.toInt() == atoi(j.key().c_str()))
          {    
            tmp=sum;
          }
        }
      }

      if (tmp>0)
      {
        energyTemp += tmp;
      }
     
      result= String(energyTemp);
    }
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
    DynamicJsonDocument temp(MAXHEAP);
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

    File tpFile = safeOpenFile(name_with_extensionTP, FILE_READ);
    if (!tpFile|| tpFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      safeCloseFile(tpFile,name_with_extensionTP);
      return false;
    }else
    {
      DynamicJsonDocument temp(MAXHEAP);
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

          File file = safeOpenFile(name_with_extension, "r+");
          if (!file) {
            safeOpenFile(name_with_extension, "w+");
            DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
            DEBUG_PRINTLN(path);
          }
          size_t filesize = file.size();
          
          DynamicJsonDocument doc(MAXHEAP);
          if (filesize>0)
          {
            DeserializationError error = deserializeJson(doc, file);
            if (error) {
              //DEBUG_PRINTLN(F("Erreur lors de la désérialisation du fichier"));
              safeCloseFile(file,name_with_extension);
              return "Error";
            }
          }
          safeCloseFile(file,name_with_extension);
          
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
}

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
      DynamicJsonDocument temp(MAXHEAP);
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
      DynamicJsonDocument temp(MAXHEAP);
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

void readZigbeeDatas(uint8_t ShortAddr[2],uint8_t Cluster[2],uint8_t Attribute[2], uint8_t DataType, int len, char* datas)
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
    case 257: //0101 dorlock
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
}
