#include <Arduino.h>
#include <esp_task_wdt.h>
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
#include "web.h"

extern struct ZigbeeConfig ZConfig;
extern struct ConfigGeneralStruct ConfigGeneral;

extern CircularBuffer<Packet, 20> commandList;
extern CircularBuffer<Packet, 10> commandTimedList;
extern String Day;
extern String Month;
extern String Yesterday;
extern String Year;
extern String Hour;
extern String Minute;

void SendActiveRequest(uint8_t shortAddr[2])
{
  Packet trame;
   
  trame.cmd=0x0045;
  trame.len=0x0002;
  memcpy(trame.datas,shortAddr,2);
  DEBUG_PRINTLN(F("Active Req")); 
  commandList.push(trame);
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
  commandList.push(trame);
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
  commandList.push(trame);
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
    commandList.push(trame);
}

void SendConfigReport(uint8_t shortAddr[2], int cluster, int attribut, int type, int rmin, int rmax, int rtimeout, uint8_t rchange)
{
    Packet trame;
    trame.cmd=0x0120;
    trame.len=23;
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
    datas[22]= rchange ;
    memcpy(trame.datas,datas,trame.len);
    commandList.push(trame);
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
    commandList.push(trame);
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

float getTarif(int attribute)
{
  float tarif;
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
  return tarif;
}

float getTarifPower(String IEEE, int power)
{
  String path = "/database/energy_"+IEEE+".json";

  File DeviceFile = LittleFS.open(path, FILE_READ);
  if (!DeviceFile|| DeviceFile.isDirectory()) {
    DEBUG_PRINTLN(F("failed open"));
  }else{
    DynamicJsonDocument temp(5096);
    deserializeJson(temp,DeviceFile);
    JsonObject root = temp.as<JsonObject>();
    DeviceFile.close();
    DEBUG_PRINTLN(power);
    int i=0;
    for (JsonPair kv : root)
    {   
      if (temp[kv.key().c_str()]["hour"]["trend"].as<int>()>0)
      {       
        DEBUG_PRINTLN(kv.key().c_str());
        float euro = (power*getTarif(String(kv.key().c_str()).toInt())/1000);
        return euro;
      }
    }
  }
  return 0;
}

String getTrendEnergyEuros(String IEEE)
{
  String path = "/database/energy_"+IEEE+".json";
  String result="";
  String sep="";
  File DeviceFile = LittleFS.open(path, FILE_READ);
  if (!DeviceFile|| DeviceFile.isDirectory()) {
    DEBUG_PRINTLN(F("failed open"));
  }else{
    DynamicJsonDocument temp(5096);
    deserializeJson(temp,DeviceFile);
    JsonObject root = temp.as<JsonObject>();
    DeviceFile.close();
    
    result += F("{");
    int i=0;
    for (JsonPair kv : root)
    {   
      if (temp[kv.key().c_str()]["hour"]["trend"].as<int>()>0)
      {       
        float euro = (temp[kv.key().c_str()]["hour"]["trend"].as<int>()*getTarif(String(kv.key().c_str()).toInt()))/1000;
        if (i>0){sep=",";}else{sep="";}
        result+=sep+String(kv.key().c_str())+":"+temp[kv.key().c_str()]["hour"]["trend"].as<String>()+" Wh ("+String(euro)+"€)";
        i++;
      }
    }
    result +=F("}");
  }
  return result;
}



String getLinkyDatas(String IEEE)
{
  String path = "/database/"+(String)IEEE+".json";
  String result="";
  File DeviceFile = LittleFS.open(path, FILE_READ);
  if (!DeviceFile|| DeviceFile.isDirectory()) {
    DEBUG_PRINTLN(F("failed open"));
    return result;
  }else{
    DynamicJsonDocument temp(4096);
    deserializeJson(temp,DeviceFile);

    if (temp["FF66"]["768"])
    {
      result+="<strong>Mode :</strong>"+getLinkyMode((int)temp["FF66"]["768"]);
    }
    if (temp["FF66"]["0"])
    {
      String abo = temp["FF66"]["0"].as<String>();
      result+="<br><strong>Abonnement :</strong>"+abo;
    }
    if (temp["FF66"]["512"]!="")
    {
      String currentTarif = temp["FF66"]["512"].as<String>();
      result+="<br><strong>Tarif en cours :</strong>"+currentTarif;
    }
    if (temp["0702"]["32"]!="")
    {
      String currentTarif =temp["0702"]["32"].as<String>();
      result+="<br><strong>Tarif en cours :</strong>"+currentTarif;
    }
    if (temp["0B01"]["13"])
    {
      result+="<br><strong>Puissance souscrite :</strong>"+String((int)strtol(temp["0B01"]["13"],0,16));
    }

    int index;
    if (temp["0702"]["0"])
    {
      index = (int)strtol(temp["0702"]["0"],0,16);
      if (index > 0)
      result+="<br><strong>Index (BASE/EAST): </strong>"+String(index)+" Wh <span id='index1'></span>";
    }
    if (temp["0702"]["256"])
    {
      index = (int)strtol(temp["0702"]["256"],0,16);
      if (index > 0)
       result+="<br><strong>Index (HC/EJPHN/BBRHCJB/EASF01): </strong>"+String(index)+" Wh <span id='index2'></span>";
    }
    if (temp["0702"]["258"])
    {
     index = (int)strtol(temp["0702"]["258"],0,16);
     if (index > 0)
      result+="<br><strong>Index (HP/EJPHPM/BBRHPJB/EASF02): </strong>"+String(index)+" Wh <span id='index3'></span>";
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
    
    return result;
  }
  return result;
}


String getPowerGaugeAbo(String IEEE, String Attribute, String Time)
{
  String result="error";
  
  if (Time=="hour")
  {
    String path = "/database/"+(String)IEEE+".json";
    File DeviceFile = LittleFS.open(path, FILE_READ);
    if (!DeviceFile|| DeviceFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      return "error";
    }else{
  
      DynamicJsonDocument temp(4096);
      deserializeJson(temp,DeviceFile);
      DeviceFile.close();
      const char *tmp; 
      if ((temp["0B04"][Attribute]) && ( temp["0B01"]["13"]))
      {
        result = String(strtol(temp["0B04"][Attribute],0,16))+";"+String(strtol(temp["0B01"]["13"],0,16)*230);
      }
    }
  }else
  {
    String path = "/database/energy_"+IEEE+".json";
    String sep="";
    File DeviceFile = LittleFS.open(path, FILE_READ);
    if (!DeviceFile|| DeviceFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      return "error";
    }else{
      DynamicJsonDocument temp(MAXHEAP);
      deserializeJson(temp,DeviceFile);
      JsonObject root = temp.as<JsonObject>();
      DeviceFile.close();
      
      int energyTemp=0;
      long int tmp;
      long int maxJauge=0;
      for (JsonPair kv : root)
      { 
        long int maxVal =0;
        JsonObject graph = temp[kv.key().c_str()][Time]["graph"].as<JsonObject>();
        for (JsonPair j : graph)
        {
          maxVal = max(j.value().as<long int>(),maxVal);
        }     
        maxJauge += maxVal;
        if (Time=="day")  
        {       
          tmp=temp[kv.key().c_str()][Time]["graph"][Day].as<int>();
        }else if (Time=="month")  
        {       
          tmp=temp[kv.key().c_str()][Time]["graph"][Month].as<int>();
          
        }else if (Time=="year")  
        {       
          tmp=temp[kv.key().c_str()][Time]["graph"][Year].as<int>();
        }
        if (tmp>0)
        {
          energyTemp += tmp;
        }
      }
      result= String(energyTemp)+";"+String(maxJauge);
    }
  }
  return result;
}


String getTrendPower(String IEEE,String Attribute, String Time)
{
  String result="";

  if (Time=="hour")
  {
    String path = "/database/power_"+Attribute+"_"+IEEE+".json";
    String op;
    File DeviceFile = LittleFS.open(path, FILE_READ);
    if (!DeviceFile|| DeviceFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      return "";
    }else
    {
      DynamicJsonDocument temp(MAXHEAP);
      deserializeJson(temp,DeviceFile);
      DeviceFile.close();
      const char *tmp; 
      if ((temp[Attribute]["trend"]))
      {
        if (temp[Attribute]["trend"].as<int>()>0)
        {
          result = "<svg xmlns='http://www.w3.org/2000/svg' width='96' height='96' fill='currentColor' class='bi bi-0-circle' style='color:orange;' viewBox='0 0 20 20'>";
          result +="    <path fill-rule='evenodd' d='M14 2.5a.5.5 0 0 0-.5-.5h-6a.5.5 0 0 0 0 1h4.793L2.146 13.146a.5.5 0 0 0 .708.708L13 3.707V8.5a.5.5 0 0 0 1 0v-6z'/>";
          result +="</svg>";
          op="+";
        }else if (temp[Attribute]["trend"].as<int>()<0){
          result = "<svg xmlns='http://www.w3.org/2000/svg' width='96' height='96' fill='currentColor' class='bi bi-0-circle' style='color:green;' viewBox='0 0 20 20'>";
          result +="  <path fill-rule='evenodd' d='M14 13.5a.5.5 0 0 1-.5.5h-6a.5.5 0 0 1 0-1h4.793L2.146 2.854a.5.5 0 1 1 .708-.708L13 12.293V7.5a.5.5 0 0 1 1 0v6z'/>";
          result +="</svg>";
          op="";
        }else{
          result = "<svg xmlns='http://www.w3.org/2000/svg' width='96' height='96' fill='currentColor' class='bi bi-0-circle' style='color:grey;' viewBox='0 0 20 20'>";
          result +="  <path fill-rule='evenodd' d='M1 8a.5.5 0 0 1 .5-.5h11.793l-3.147-3.146a.5.5 0 0 1 .708-.708l4 4a.5.5 0 0 1 0 .708l-4 4a.5.5 0 0 1-.708-.708L13.293 8.5H1.5A.5.5 0 0 1 1 8z'/>";
          result +="</svg>";
          op="";
        }
        result +="<div style='text-align:center;'>";
          result +="<div style='color:#fab600;display:inline-block;width:78px;'><svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-lightning-fill' viewBox='0 0 16 16'>";
            result +="<path d='M5.52.359A.5.5 0 0 1 6 0h4a.5.5 0 0 1 .474.658L8.694 6H12.5a.5.5 0 0 1 .395.807l-7 9a.5.5 0 0 1-.873-.454L6.823 9.5H3.5a.5.5 0 0 1-.48-.641l2.5-8.5z'/>";
          result +="</svg></div>";
          result+="<span style='font-size:24px;'>";
            result +=op+temp[Attribute]["trend"].as<String>()+ " VA ";
          result+="</span>";
          result+="<span style='font-size:12px;'>(<strong>Min:</strong> ";
            result+=temp[Attribute]["min"].as<String>();
            result+=" VA / <strong>Max :</strong> ";
            result+=temp[Attribute]["max"].as<String>();
            result+=" VA)";
          result+="</span>";   
          result+="<br><div style='color:#239b56;display:inline-block;width:78px;'><svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-cash-stack' viewBox='0 0 16 16'>";
            result+="<path d='M1 3a1 1 0 0 1 1-1h12a1 1 0 0 1 1 1H1zm7 8a2 2 0 1 0 0-4 2 2 0 0 0 0 4z'/>";
            result+="<path d='M0 5a1 1 0 0 1 1-1h14a1 1 0 0 1 1 1v8a1 1 0 0 1-1 1H1a1 1 0 0 1-1-1V5zm3 0a2 2 0 0 1-2 2v4a2 2 0 0 1 2 2h10a2 2 0 0 1 2-2V7a2 2 0 0 1-2-2H3z'/>";
            result+="</svg>";       
          result+="</div>";
          result+="<span style='font-size:24px;'>";
            result +=String(getTarifPower(IEEE,temp[Attribute]["last"].as<int>())) +"€/h";
          result+="</span>";
        result += "</div>";
        
      }
      
    }
  }else
  {
    String path = "/database/energy_"+IEEE+".json";
    String sep="";
    File DeviceFile = LittleFS.open(path, FILE_READ);
    if (!DeviceFile|| DeviceFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      return "error";
    }else{
      DynamicJsonDocument temp(MAXHEAP);
      deserializeJson(temp,DeviceFile);
      JsonObject root = temp.as<JsonObject>();
      DeviceFile.close();
      
      int energyTemp=0;
      long int tmp=0;
      long int lastTime=0;
      long int maxJauge=0;
      float tarifEuro=0;
      
      for (JsonPair kv : root)
      {
        long int maxVal =0;
        JsonObject graph = temp[kv.key().c_str()][Time]["graph"].as<JsonObject>();
        for (JsonPair j : graph)
        {
          maxVal = max(j.value().as<long int>(),maxVal);
        }     
        maxJauge += maxVal;          
        if (Time=="day")
        {
          tmp=temp[kv.key().c_str()][Time]["trend"].as<int>();
          lastTime+=temp[kv.key().c_str()][Time]["graph"][Yesterday].as<int>();
        }else if (Time=="month")
        {
          tmp=temp[kv.key().c_str()][Time]["trend"].as<int>();
          int monthDec = Month.toInt()-1;
          String monthtmp = monthDec < 10 ? "0" + String(monthDec) : String(monthDec);         
          lastTime+=temp[kv.key().c_str()][Time]["graph"][monthtmp].as<int>();
        }else if (Time=="year")
        {
          int yearDec = Year.toInt()-1;
          String yeartmp = yearDec < 10 ? "0" + String(yearDec) : String(yearDec);
          tmp=temp[kv.key().c_str()][Time]["trend"].as<int>();
          lastTime+=temp[kv.key().c_str()][Time]["graph"][yeartmp].as<int>();
        }
        if (tmp>0)
        {
          tarifEuro+=(tmp * getTarif(String(kv.key().c_str()).toInt()))/1000;
          energyTemp += tmp;
        }
      }
      
      long int trend=0;
      String op="";
      if ( energyTemp > lastTime)
      {
        result = "<svg xmlns='http://www.w3.org/2000/svg' width='96' height='96' fill='currentColor' class='bi bi-0-circle' style='color:orange;' viewBox='0 0 20 20'>";
        result +="    <path fill-rule='evenodd' d='M14 2.5a.5.5 0 0 0-.5-.5h-6a.5.5 0 0 0 0 1h4.793L2.146 13.146a.5.5 0 0 0 .708.708L13 3.707V8.5a.5.5 0 0 0 1 0v-6z'/>";
        result +="</svg>";
        op="+";
        trend=energyTemp - lastTime;
      }else if ( energyTemp < lastTime)
      {
        result = "<svg xmlns='http://www.w3.org/2000/svg' width='96' height='96' fill='currentColor' class='bi bi-0-circle' style='color:green;' viewBox='0 0 20 20'>";
        result +="  <path fill-rule='evenodd' d='M14 13.5a.5.5 0 0 1-.5.5h-6a.5.5 0 0 1 0-1h4.793L2.146 2.854a.5.5 0 1 1 .708-.708L13 12.293V7.5a.5.5 0 0 1 1 0v6z'/>";
        result +="</svg>";
        op="-";
        trend=lastTime - energyTemp;
      }else{
        result = "<svg xmlns='http://www.w3.org/2000/svg' width='96' height='96' fill='currentColor' class='bi bi-0-circle' style='color:grey;' viewBox='0 0 20 20'>";
        result +="  <path fill-rule='evenodd' d='M1 8a.5.5 0 0 1 .5-.5h11.793l-3.147-3.146a.5.5 0 0 1 .708-.708l4 4a.5.5 0 0 1 0 .708l-4 4a.5.5 0 0 1-.708-.708L13.293 8.5H1.5A.5.5 0 0 1 1 8z'/>";
        result +="</svg>";
        op="";
        trend=0;
      }
       result +="<div style='text-align:center;'>";
          result +="<div style='color:#fab600;display:inline-block;width:78px;'><svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-lightning-fill' viewBox='0 0 16 16'>";
            result +="<path d='M5.52.359A.5.5 0 0 1 6 0h4a.5.5 0 0 1 .474.658L8.694 6H12.5a.5.5 0 0 1 .395.807l-7 9a.5.5 0 0 1-.873-.454L6.823 9.5H3.5a.5.5 0 0 1-.48-.641l2.5-8.5z'/>";
          result +="</svg></div>";
          result+="<span style='font-size:24px;'>";
            result +=op+String(trend)+ " Wh ";
          result+="</span>";
          
          result+="<br><div style='color:#239b56;display:inline-block;width:78px;'><svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' class='bi bi-cash-stack' viewBox='0 0 16 16'>";
            result+="<path d='M1 3a1 1 0 0 1 1-1h12a1 1 0 0 1 1 1H1zm7 8a2 2 0 1 0 0-4 2 2 0 0 0 0 4z'/>";
            result+="<path d='M0 5a1 1 0 0 1 1-1h14a1 1 0 0 1 1 1v8a1 1 0 0 1-1 1H1a1 1 0 0 1-1-1V5zm3 0a2 2 0 0 1-2 2v4a2 2 0 0 1 2 2h10a2 2 0 0 1 2-2V7a2 2 0 0 1-2-2H3z'/>";
            result+="</svg>";       
          result+="</div>";
          result+="<span style='font-size:24px;'>";
            result +=String(tarifEuro) +"€";
          result+="</span>";
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
    String path = "/database/"+(String)IEEE+".json";
    
    File DeviceFile = LittleFS.open(path, FILE_READ);
    if (!DeviceFile|| DeviceFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      return "";
    }else{
      DynamicJsonDocument temp(4096);
      deserializeJson(temp,DeviceFile);
      DeviceFile.close();
      const char *tmp; 
      if ((temp["0B04"][Attribute]))
      {
        result = String(strtol(temp["0B04"][Attribute],0,16));
      }     
    }
  }else
  {
    String path = "/database/energy_"+IEEE+".json";

    File DeviceFile = LittleFS.open(path, FILE_READ);
    if (!DeviceFile|| DeviceFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      return "error";
    }else{
      DynamicJsonDocument temp(MAXHEAP);
      deserializeJson(temp,DeviceFile);
      JsonObject root = temp.as<JsonObject>();
      DeviceFile.close();
      
      int energyTemp=0;
      long int tmp;
      for (JsonPair kv : root)
      {          
        if (Time=="day")
        {
          tmp=temp[kv.key().c_str()][Time]["graph"][Day].as<int>();
        }else if (Time=="month")
        {
          tmp=temp[kv.key().c_str()][Time]["graph"][Month].as<int>();
        }else if (Time=="year")
        {
          tmp=temp[kv.key().c_str()][Time]["graph"][Year].as<int>();
        }
        if (tmp>0)
        {
          energyTemp += tmp;
        }
      }
      result= String(energyTemp);
    }
  }
  return result;
}


String getPowerGaugeTimeDay(String IEEE, String Attribute)
{
  String path = "/database/power_"+Attribute+"_"+IEEE+".json";
  String result="";
  File DeviceFile = LittleFS.open(path, FILE_READ);
  if (!DeviceFile|| DeviceFile.isDirectory()) {
    DEBUG_PRINTLN(F("failed open"));
    return "";
  }else{
    DynamicJsonDocument temp(MAXHEAP);
    deserializeJson(temp,DeviceFile);
    const char *tmp; 
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
  String path = "/database/"+type+"_"+Attribute+"_"+IEEE+".json";
  String result="";
  File DeviceFile = LittleFS.open(path, FILE_READ);
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
    Template t;
    String path = "/templates/"+(String)device_id+".json";

    File templateFile = LittleFS.open(path, FILE_READ);
    if (!templateFile|| templateFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      return false;
    }else
    {
      DynamicJsonDocument temp(5096);
      deserializeJson(temp,templateFile);
      const char *tmp; 
      if (temp.containsKey(model))
      {
        tmp = temp[model][0]["status"];
        if (tmp !="")
        {
          String cluster;
          int attribut;
          int poll,last;
          int i=0;
          int j=0;
          
          String inifile;
          int SA = shortAddr[0]*256+shortAddr[1];
          inifile = GetMacAdrr(SA);

          File file = LittleFS.open("/database/"+inifile, "r+");
          if (!file) {
            LittleFS.open("/database/"+path, "w+");
            DEBUG_PRINTLN(F("Erreur lors de l'ouverture du fichier "));
          }
          size_t filesize = file.size();
          
          DynamicJsonDocument doc(5096);
          if (filesize>0)
          {
            DeserializationError error = deserializeJson(doc, file);
            if (error) {
              //DEBUG_PRINTLN(F("Erreur lors de la désérialisation du fichier"));
              file.close();
              return "Error";
            }
          }
          file.close();
          
          JsonArray StatusArray = temp[model][0]["status"].as<JsonArray>();
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
                  doc["poll"][j]["last"]=poll;                 
                  j++;
              }
              
              i++;
          }

          if (j>0)
          {
            file = LittleFS.open("/database/"+inifile, "w+");
            if (!file|| file.isDirectory()) {
              DEBUG_PRINTLN(F("Erreur lors de l'ouverture du fichier "));
              return false;
            }
            if (!doc.isNull())
            {
              if (serializeJson(doc, file) == 0) {
                DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier"));
                file.close();
                return false;
              }
            }
          
            // Fermer le fichier
            file.close();
            return true;
          }
        }
        return false;
      }
      templateFile.close();
      return false;
    }
    return false;
  }
  return false;
}

void getConfigReport(uint8_t shortAddr[2], int device_id, String model)
{
  if (TemplateExist(device_id))
  {
    Template t;
    String path = "/templates/"+(String)device_id+".json";

    File templateFile = LittleFS.open(path, FILE_READ);
    if (!templateFile|| templateFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      
    }else
    {
      DynamicJsonDocument temp(5096);
      deserializeJson(temp,templateFile);
      const char *tmp; 
      if (temp.containsKey(model))
      {
        tmp = temp[model][0]["report"];
        if (tmp !="")
        {
          int cluster, attribut;
          uint8_t rType,rMin,rMax,rTimeout,rChange;
          int i=0;
          JsonArray StatusArray = temp[model][0]["report"].as<JsonArray>();
          for(JsonVariant v : StatusArray) 
          {
              cluster = (int)strtol(temp[model][0]["report"][i]["cluster"],0,16);
              attribut = (int)temp[model][0]["report"][i]["attribut"];
              rType = (uint8_t)temp[model][0]["report"][i]["type"];
              rMin = (int)temp[model][0]["report"][i]["min"];
              rMax = (int)temp[model][0]["report"][i]["max"];
              rTimeout = (int)temp[model][0]["report"][i]["timeout"];
              rChange = (uint8_t)temp[model][0]["report"][i]["change"];
              SendConfigReport(shortAddr,cluster,attribut,rType,rMin,rMax,rTimeout,rChange);
              i++;
              
          }
        }
      }
      templateFile.close();
    } 
  }
}


void getBind(uint64_t mac, int device_id, String model)
{
  if (TemplateExist(device_id))
  {
    Template t;
    String path = "/templates/"+(String)device_id+".json";

    File templateFile = LittleFS.open(path, FILE_READ);
    if (!templateFile || templateFile.isDirectory()) {
      DEBUG_PRINTLN(F("failed open"));
      
    }else
    {
      DynamicJsonDocument temp(5096);
      deserializeJson(temp,templateFile);
      const char *tmp; 
      if (temp.containsKey(model))
      {
        tmp = temp[model][0]["bind"];
        if (tmp !="")
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
      templateFile.close();
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
    /*case 1794: // 0702 SimpleMetering
      SMManage(shortaddr,attribute,DataType,len,datas);
      break; */
    default:
      char tmp[4];
      sprintf(tmp,"%04X",cluster);
      defaultClusterManage(shortaddr,tmp,attribute,DataType,len,datas);
      break;
  
  }
}
