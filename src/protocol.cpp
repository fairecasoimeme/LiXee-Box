#include <esp_task_wdt.h>
#include <Arduino.h>

#include <esp_system.h>
#include "protocol.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "SPIFFS_ini.h"
#include "config.h"
#include "log.h"
#include "zigbee.h"
#include "basic.h"

extern struct ZigbeeConfig ZConfig;
extern CircularBuffer<Packet, 40> commandList;
extern CircularBuffer<Packet, 10> commandTimedList;
extern CircularBuffer<Alert, 10> alertList;

extern String FormattedDate;
extern String epochTime;
extern unsigned long timeLog;
extern String section[12];

char packet[128];
int TimedFiFo;

IPAddress parse_ip_address(const char *str) {
    IPAddress result;    
    int index = 0;

    result[0] = 0;
    while (*str) {
        if (isdigit((unsigned char)*str)) {
            result[index] *= 10;
            result[index] += *str - '0';
        } else {
            index++;
            if(index<4) {
              result[index] = 0;
            }
        }
        str++;
    }
    
    return result;
}

String GetNameStatus(int deviceId,String cluster, int attribut, String model)
{
    String path = "/tp/"+(String)deviceId+".json";

    File tpFile = LittleFS.open(path, FILE_READ);
    if (!tpFile || tpFile.isDirectory()) 
    {
      DEBUG_PRINTLN(F("failed open"));
      
    }else
    {
      DynamicJsonDocument temp(10192);
      deserializeJson(temp,tpFile);

      tpFile.close();
      
      int i=0;     
      if (temp.containsKey(model))
      {
          JsonArray StatusArray = temp[model][0]["status"].as<JsonArray>();
          for(JsonVariant v : StatusArray) 
          {
            if ((temp[model][0]["status"][i]["cluster"].as<String>()==cluster) && (temp[model][0]["status"][i]["attribut"].as<int>()==attribut))
            {
              return temp[model][0]["status"][i]["name"].as<String>();
            }
            i++;
          }
      }
          
    }  
    return "";  
}

String GetValueStatus(String inifile, int key, int attribut, String type, float coefficient, String unit)
{
   char tmpKey[5];
   sprintf(tmpKey,"%04X",key);
   String tmp= ini_read(inifile, tmpKey, (String)attribut);  

   if (type =="float")
   {
       float tmpint = strtol(tmp.c_str(), NULL, 16);
       if (coefficient != 1)
       {
        tmpint = tmpint * coefficient;
       }
       tmp=String(tmpint); 
   }else if (type =="numeric"){
      int tmpint = strtol(tmp.c_str(), NULL, 16);
      if (coefficient != 1)
       {
        tmpint = tmpint * coefficient;
       }
       tmp=String(tmpint); 
   }

   tmp= tmp+" "+unit;
   
   return tmp;
}

void lastSeen(int shortAddr)
{
  String path = GetMacAdrr(shortAddr);
  String tmpTime; 


  ini_write(path,"INFO", "lastSeen", FormattedDate);

}

String GetMacAdrr(int shortAddr)
{
 // char SAddr[20];
  File root = LittleFS.open("/db");
  File file = root.openNextFile();
  while (file) 
  {
      
      String tmp =  file.name();
      String Saddr= ini_read(tmp,"INFO", "shortAddr");

      if (shortAddr==atoi(Saddr.c_str()))
      {
        return tmp;
      }   
      file = root.openNextFile();
      
  }
  file.close();
  root.close();
  return "";
}

String GetLastSeen(String inifile)
{
   String tmp= ini_read(inifile,"INFO", "lastSeen");  
   return tmp;
}

String GetLQI(String inifile)
{
   String tmp= ini_read(inifile,"INFO", "LQI");  
   int tmp2 = (int) strtol(tmp.c_str(), 0, 16);
   return String(tmp2);
}


int GetShortAddr(String inifile)
{
   //ini_open(inifile);
   String tmp= ini_read(inifile,"INFO", "shortAddr");  
   return tmp.toInt();
}

int GetDeviceId(String inifile)
{
   String tmp= ini_read(inifile,"INFO", "device_id");  
   return tmp.toInt();
}

String GetSoftwareVersion(String inifile)
{
  String tmp= ini_read(inifile,"INFO", "software_version"); 
  return tmp;
}

void datasManage(char packet[128],int count)
{
  int i=0;
  uint8_t CRC=0;
  ZiGateProtocol protocol;
  if (count >=6)
  {
    protocol.type = int(packet[0])<<8 ;
    CRC=CRC ^ int(packet[0]);
    protocol.type |= int(packet[1]) ;
    CRC=CRC ^ int(packet[1]);
    protocol.ln = int(packet[2])<<8 ;
    CRC=CRC ^ int(packet[2]);
    protocol.ln |= int(packet[3]) ;
    CRC=CRC ^ int(packet[3]);
    protocol.chksum = int(packet[4]);
          
    for (i=5; i< (5+protocol.ln); i++)
    {
      CRC=CRC ^ int(packet[i]);
      protocol.payload[(i-5)]=packet[i];
    }

    if (protocol.chksum == CRC)
    {
      DecodePayload(protocol,count); 
    }else{
      addDebugLog(F("CRC error"));
      DEBUG_PRINTLN(F("CRC error"));
    }
    
  }
}

void DecodePayload(struct ZiGateProtocol protocol, int packetSize)
{
  
  switch(protocol.type){
    case 0x8702:
    {
      uint8_t ShortAddr[2];
      DEBUG_PRINT(F("8702 - Status :"));
      Serial.print(protocol.payload[0], HEX);
      DEBUG_PRINT(F(" - "));
      ShortAddr[0]=protocol.payload[4];
      ShortAddr[1]=protocol.payload[5];
      DEBUG_PRINT(ShortAddr[0]);
      DEBUG_PRINT(ShortAddr[1]);
      DEBUG_PRINT(F(" - "));
      String inifile;
      int SA = (int)(ShortAddr[0] * 256)+ShortAddr[1];
      inifile = GetMacAdrr(SA);
      DEBUG_PRINT(inifile);
      char tmpStatus[2];
      sprintf(tmpStatus,"%02x",protocol.payload[0]);
      ini_write(inifile,"INFO","Status",String(tmpStatus));
      DEBUG_PRINTLN();
      
    }
    break;
    case 0x8000:
      Serial.print(F("Status :"));
      switch(protocol.payload[0]){
        case 0x0:
          DEBUG_PRINT(F("Success"));
          break;
        case 0x1:
          DEBUG_PRINT(F("Inc Param"));
          break;
        case 0x2:
          DEBUG_PRINT(F("Unhandled cmd"));
          break;
        case 0x3:
          DEBUG_PRINT(F("Cmd Failed"));
          break;
        case 0x4:
          DEBUG_PRINT(F("Busy"));
          break;
        default:
          DEBUG_PRINT(F("Error"));
          break;
      }
      DEBUG_PRINT(" (");
      Serial.print(protocol.payload[0], HEX);
      DEBUG_PRINTLN(")");
      break;
    case 0x8010:
      ZConfig.type = protocol.payload[0];
      ZConfig.sdk = protocol.payload[1];
      sprintf(ZConfig.application,"%02x%02x",protocol.payload[2],protocol.payload[3]);
      DEBUG_PRINT(F("Version - "));
      DEBUG_PRINT(F("SDK: "));
      DEBUG_PRINT(int(ZConfig.type));
      DEBUG_PRINT(int(ZConfig.sdk));
      DEBUG_PRINT(F(" APP: "));
      DEBUG_PRINTLN(ZConfig.application);
     //SAVE CONFIG JSON
      break;
    case 0x8009:
     {
      int tmp;
      String mac;      
      uint64_t tmpmac = (uint64_t)protocol.payload[2] << 56 |
                        (uint64_t)protocol.payload[3] << 48 |
                        (uint64_t)protocol.payload[4] << 40 |
                        (uint64_t)protocol.payload[5] << 32 |
                        (uint64_t)protocol.payload[6] << 24 |
                        (uint64_t)protocol.payload[7] << 16 |
                        (uint64_t)protocol.payload[8] << 8 |
                        (uint64_t)protocol.payload[9];
      DEBUG_PRINT(F("Mac coordinator: "));
      ZConfig.zigbeeMac = tmpmac; 
      Serial.println(tmpmac, HEX);
                
      tmp  = (protocol.payload[0]*256)+protocol.payload[1];
      
      DEBUG_PRINT(F("Short addr: "));
      Serial.println(tmp, HEX);

      if (tmp == 0xFFFF)
      {
        //network not started
        commandList.push(Packet{0x0024, 0x0000,0});
      }
     }
     break;
     case 0x8024:   
      {
        DEBUG_PRINT(F("Network : "));
        uint8_t statusNetwork = protocol.payload[0];
        ZConfig.network = statusNetwork;
        DEBUG_PRINTLN(statusNetwork);
        DEBUG_PRINT(F("Channel: "));
        uint8_t channel = protocol.payload[11];
        ZConfig.channel=(int)channel;
        DEBUG_PRINTLN(channel);
      }
      break;
      case 0x8030:
      {
        uint8_t statusBind = protocol.payload[1];
        DEBUG_PRINTLN();
        DEBUG_PRINT(F("Bind Response: "));
        DEBUG_PRINTLN(F(statusBind));
        if (statusBind ==0)
        {
          int tmp  = (protocol.payload[3]*256)+protocol.payload[4];
          DEBUG_PRINT(F("Short addr: "));
          Serial.println(tmp, HEX);
        }
      }
      break;
      case 0x8043:   
      {
        DEBUG_PRINTLN(F("Simple descriptor "));
        int shortAddr;
        int device_id;
        int endpoint;
        int nbIN, nbOUT;
        int l;
        int r;
        r = protocol.payload[1];
        shortAddr = protocol.payload[2]*256+protocol.payload[3];
        l = protocol.payload[4];
        endpoint = protocol.payload[5];
        device_id = protocol.payload[8]*256+protocol.payload[9];

        String path = GetMacAdrr(shortAddr);
        
        if (ini_exist(path))
        {
          ini_write(path,"INFO", "device_id", (String)device_id);
        
          nbIN= protocol.payload[11];
          nbOUT=protocol.payload[11+(nbIN*2)+1];
  
          String tmpIN="";
          String tmpOUT="";
          int i;
          for (i=12;i<(12+(nbIN*2));i=i+2)
          {
            int cluster;
            cluster = protocol.payload[i]*256+protocol.payload[i+1];
            if (i>12){tmpIN+=",";}
            tmpIN+=String(cluster);
          
          }
          
          ini_write(path,(String)endpoint, "IN", (String)tmpIN);
      
          for (i=((12+(nbIN*2))+1);i<(((12+(nbIN*2))+1)+(nbOUT*2));i=i+2)
          {
            int cluster;
            cluster = protocol.payload[i]*256+protocol.payload[i+1];
            if (i>((12+(nbIN*2))+1)){tmpOUT+=",";}
            tmpOUT+=(String)cluster;
          }
          
          ini_write(path,(String)endpoint, "OUT", (String)tmpOUT);
        }
        //ini_close();

        //get info basic (Appli version / Manuf / model)
        if (endpoint ==1)
        {
          uint8_t shrtAddr[2];
          shrtAddr[0]=protocol.payload[2];
          shrtAddr[1]=protocol.payload[3];
          SendBasicDescription(shrtAddr,1);
        }
        
      }
      break;
      case 0x8045:   
      {
        DEBUG_PRINT(F("Active Response : "));
        uint8_t ShortAddr[2];
        int i;
        if (protocol.payload[1] ==0)
        {
          for (i=0;i<2;i++)
          {
            ShortAddr[i]=protocol.payload[i+2];
            Serial.print(protocol.payload[i+2], HEX);
          }
          uint8_t nbEndpoint = protocol.payload[4];
          DEBUG_PRINTLN(nbEndpoint);
          for(i=0;i<nbEndpoint;i++)
          {
            //Send Simple Description
            Packet trame;
            trame.cmd=0x0043;
            trame.len=0x0003;
            uint8_t datas[3];
            datas[0]=ShortAddr[0];
            datas[1]=ShortAddr[1];
            datas[2]= protocol.payload[5+i];
            memcpy(trame.datas,datas,3);
            DEBUG_PRINT(F("Simple desc :")); 
            DEBUG_PRINTLN(datas[2]); 
            TimedFiFo=10000;
            //TimedFiFo=1;
            //commandTimedList.push(trame);
            commandList.push(trame);
    //Save endpoint to device
            //insertEndpoint(((protocol.payload[2]*256) + protocol.payload[3]),nbEndpoint);
          }

          
        }
      }
      break;
      case 0x8401:
      {
        DEBUG_PRINT(F("Zone status change notification : "));
        uint8_t ShortAddr[2];
        int i;
        uint8_t endpoint = protocol.payload[0];
        uint8_t Cluster[2];
        DEBUG_PRINT(F(" - Cluster : ")); 
        
        for (i=0;i<2;i++)
        {
          Cluster[i]=protocol.payload[i+1];
          char tmp[5];
          snprintf(tmp,3,"%02X",protocol.payload[i+1]);
          DEBUG_PRINT(tmp);
        }
        
        for (i=0;i<2;i++)
        {
          ShortAddr[i]=protocol.payload[i+4];
          char tmp[2];
          snprintf(tmp,3,"%02X",protocol.payload[i+4]);
          DEBUG_PRINT(tmp);
        }

        
      }
      break;
      case 0x8002:
      {
        DEBUG_PRINT(F("RAW response : "));
        DEBUG_PRINTLN();
        int i=0;
        for (i=0;i<(packetSize -6);i++)
        {
          char tmp[3];
          snprintf(tmp,3, "%02X",protocol.payload[i]);
          DEBUG_PRINT(tmp);
          DEBUG_PRINT(" ");
        }
        
        DEBUG_PRINT(F(" Status :"));
        Serial.print(protocol.payload[0], HEX);
        DEBUG_PRINT(F(" Taille: "));
        DEBUG_PRINT(String(packetSize));

        uint8_t Cluster[2];
        DEBUG_PRINT(F(" - Cluster : ")); 
        for (i=0;i<2;i++)
        {
          Cluster[i]=protocol.payload[i+3];
          char tmp[3];
          snprintf(tmp,3, "%02X",protocol.payload[i+3]);
          DEBUG_PRINT(tmp);
        }
        uint8_t endpoint = protocol.payload[5];
        uint8_t addressMode = protocol.payload[7];
        uint8_t ShortAddr[2]; 
        if (addressMode == 2)
        {
           DEBUG_PRINT(F(" - Short : ")); 
           for (i=0;i<2;i++)
           {
              ShortAddr[i]=protocol.payload[i+8];
              char tmp[3];
              snprintf(tmp,3, "%02X",protocol.payload[i+8]);
              DEBUG_PRINT(tmp);
           }
        }
        uint8_t DataType;
        uint8_t Command;
        Command = protocol.payload[15];
        DEBUG_PRINT(F(" - Cmd: ")); 
        DEBUG_PRINT(Command);
        if ((Command == 10) || (Command == 1))
        {
          uint8_t Attribute[2];
          char tmp[3];
          DEBUG_PRINT(F(" - Attr : ")); 
          Attribute[0]=protocol.payload[17];
          snprintf(tmp,3, "%02X",protocol.payload[17]);
          DEBUG_PRINT(tmp);
          Attribute[1]=protocol.payload[16];
          snprintf(tmp,3, "%02X",protocol.payload[16]);
          DEBUG_PRINT(tmp);

          int offset, ln;
          if (Command ==10)
          {
            DataType = protocol.payload[18];
            ln = packetSize-19-6;
            offset = 19;
          }else if (Command ==1)
          {
            DataType = protocol.payload[19];
            ln = packetSize-20-6;
            offset = 20;
          }

          //Traitement données
          readZigbeeDatas(ShortAddr,Cluster,Attribute,DataType,ln,&protocol.payload[offset]);
        }
        String inifile;
        int SA = (int)(ShortAddr[0] * 256)+ShortAddr[1];
        inifile = GetMacAdrr(SA);
        DEBUG_PRINT(inifile);
        char tmpStatus[2];
        sprintf(tmpStatus,"%02x",protocol.payload[0]);
        ini_write(inifile,"INFO","Status",String(tmpStatus));
        DEBUG_PRINTLN();
      }
      break;
      case 0x8100:  
      case 0x8102:   
      {
        DEBUG_PRINTLN(F("Individual Attribute response : "));
        uint8_t ShortAddr[2];
        int i;
      
        for (i=0;i<2;i++)
        {
          ShortAddr[i]=protocol.payload[i+1];
          char tmp[3];
          snprintf(tmp,3,"%02X",protocol.payload[i+1]);
          DEBUG_PRINT(tmp);
        }

        lastSeen((int)(ShortAddr[0] * 256)+ShortAddr[1]);
                
        uint8_t endpoint = protocol.payload[3];
        uint8_t Cluster[2];
        DEBUG_PRINT(F(" - Cluster : ")); 
        for (i=0;i<2;i++)
        {
          Cluster[i]=protocol.payload[i+4];
          char tmp[3];
          snprintf(tmp,3,"%02X",protocol.payload[i+4]);
          DEBUG_PRINT(tmp);
        }

        uint8_t Attribute[2];
        DEBUG_PRINT(F(" - Attr : ")); 
        for (i=0;i<2;i++)
        {
          Attribute[i]=protocol.payload[i+6];
          char tmp[3];
          snprintf(tmp,3,"%02X",protocol.payload[i+6]);
          DEBUG_PRINT(tmp);
        }
        
        uint8_t DataType;
        int ln;
        if (protocol.payload[8] == 0)
        {
           
           DEBUG_PRINT(F(" - type : ")); 
           DataType = protocol.payload[9];
           DEBUG_PRINT(DataType);
           DEBUG_PRINT(F(" - size : ")); 
           ln = protocol.payload[10]*256 +protocol.payload[11];
           DEBUG_PRINT(ln);          
           DEBUG_PRINT(F(" - Datas : ")); 
           for (i=0;i<ln;i++)
           {
             char tmp[3];
             snprintf(tmp,3,"%02X",protocol.payload[i+12]);
             DEBUG_PRINT(tmp);
           }
           DEBUG_PRINTLN();
           DEBUG_PRINT("LQI :");
           char lqi[3];
           snprintf(lqi,3, "%02X",protocol.payload[ln+12]);
           DEBUG_PRINTLN(lqi);
           String inifile;
           int SA = (int)(ShortAddr[0] * 256)+ShortAddr[1];
           inifile = GetMacAdrr(SA);
           ini_write(inifile,"INFO","LQI",String(lqi));
           //Traitement données
           readZigbeeDatas(ShortAddr,Cluster,Attribute,DataType,ln,&protocol.payload[12]);
           
        }

        //traitement bind lors de la réception du model
        int tmpcluster = Cluster[0]*256+Cluster[1];
        int tmpattribute = Attribute[0]*256+Attribute[1];
        if ((tmpcluster == 0) && (tmpattribute == 5))
        {  
          String inifile,tmpMac;
          String model;
          int SA = (int)(ShortAddr[0] * 256)+ShortAddr[1];
          inifile = GetMacAdrr(SA);
          tmpMac = inifile.substring(0,16);
          uint8_t mac[9];
          sscanf(tmpMac.c_str(), "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5], &mac[6], &mac[7]);
          uint64_t macInt = (uint64_t)mac[0] << 56 |
                            (uint64_t)mac[1] << 48 |
                            (uint64_t)mac[2] << 40 |
                            (uint64_t)mac[3] << 32 |
                            (uint64_t)mac[4] << 24 |
                            (uint64_t)mac[5] << 16 |
                            (uint64_t)mac[6] << 8 |
                            (uint64_t)mac[7];
          int DeviceId = GetDeviceId(inifile);
          model = GetModel(inifile);    
          getBind(macInt,DeviceId,model);
          DEBUG_PRINTLN(F("getBind"));
          // Traitement config report
          getConfigReport(ShortAddr,DeviceId,model);
          DEBUG_PRINTLN(F("getConfigReport"));
          // Traitement polling
          getPollingDevice(ShortAddr,DeviceId,model);
          DEBUG_PRINTLN(F("getPollingDevice"));
          
          //Traitement spécifique seloin modèle
          SpecificTreatment(ShortAddr,model);
          DEBUG_PRINTLN(F("SpecificTreatment"));
          
          
        }
        
        size_t HeapSize = ESP.getHeapSize();
        DEBUG_PRINT(F("Heap size : "));
        DEBUG_PRINTLN(HeapSize);
        size_t freeMemory = ESP.getFreeHeap();
        DEBUG_PRINT(F("Free heap : "));
        DEBUG_PRINTLN(freeMemory);
        size_t minMemory = ESP.getMinFreeHeap();
        DEBUG_PRINT(F("lowest level of free heap : "));
        DEBUG_PRINTLN(minMemory);
        size_t maxMemory = ESP.getMaxAllocHeap();
        DEBUG_PRINT(F("largest block of heap : "));
        DEBUG_PRINTLN(maxMemory);
      }
      break;
      case 0x8120:   
      {
         
         DEBUG_PRINT(F("Config report response : "));
         DEBUG_PRINT(F(" Size : "));
         DEBUG_PRINT(protocol.ln);
         DEBUG_PRINT(F(" Short : "));
         Serial.print(protocol.payload[1], HEX);
         Serial.print(protocol.payload[2], HEX);
         DEBUG_PRINT(F(" Cluster : "));
         char cluster[5];
         snprintf(cluster,5,"%02X%02X",protocol.payload[4],protocol.payload[5]);      
         DEBUG_PRINT(cluster);
         DEBUG_PRINT(F(" Status : "));
         Serial.println(protocol.payload[6], HEX);
         
      }
      break;
      case 0x8048:   
      {
        DEBUG_PRINT(F("Leave Network : "));
        char mac[20];
        int i;
        for (i=0;i<9;i++)
        {
          mac[i]=protocol.payload[i];
          Serial.print(protocol.payload[i], HEX);
        }
        snprintf(mac,20,"%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],mac[6],mac[7]);
        alertList.push(Alert{"Device leaved : "+String(mac), 1});
        DEBUG_PRINTLN(String(mac)
        );
        //Supprimer dans la base
      }
      break;
      case 0x004d:   
      {
        DEBUG_PRINT(F("Node Joined : - "));
        int ShortAddr;
        uint8_t ShortAddr_c[2];
        uint8_t mac[9];
        int i;
        DEBUG_PRINT(F("Short Addr : "));
        ShortAddr=(protocol.payload[0]*256) + protocol.payload[1];
        ShortAddr_c[0]=protocol.payload[0];
        ShortAddr_c[1]=protocol.payload[1];
        
        char tmp[5];
        snprintf(tmp,5, "%02X%02X",protocol.payload[0],protocol.payload[1]);
        alertList.push(Alert{"Device Joined : "+String(tmp), 0});
        
        DEBUG_PRINT( ShortAddr);
        DEBUG_PRINT(F(" - Mac Addr : "));
        for (i=2;i<10;i++)
        {
          mac[i-2]=protocol.payload[i];
          Serial.print(protocol.payload[i], HEX);
        }
        DEBUG_PRINT(F(" - Cap : "));
        for (i=10;i<11;i++)
        {
          Serial.print(protocol.payload[i], HEX);
        }
        DEBUG_PRINT(F(" - LQI : "));
        char lqi[3];
        for (i=11;i<12;i++)
        {
          Serial.print(protocol.payload[i], HEX);
          snprintf(lqi, 3,"%02X",protocol.payload[i]);

        }
        DEBUG_PRINTLN();
        //Add dans la base
        String adMac;
        adMac = getMacAddress(mac);
     
        String path = adMac+".json";
        ini_write(path,"INFO","shortAddr",String(ShortAddr));
        ini_write(path,"INFO","LQI",String(lqi));
       
     
        //Demande Active Request
        if (protocol.payload[11]>0)
        {
          Packet trame;
          trame.cmd=0x0045;
          trame.len=0x0002;
          memcpy(trame.datas,protocol.payload,2);
          DEBUG_PRINT(F("Active Req :")); 
          DEBUG_PRINTLN(ShortAddr);
          TimedFiFo=1000;
          commandList.push(trame);

        }

      }
      break;
    default:
      DEBUG_PRINT(F("Packet Unknow : "));
      Serial.println(protocol.type, HEX);

      break;
  }
  
}

String getMacAddress(uint8_t mac[9])
{
  char tmp[20];
  snprintf(tmp,20,"%02x%02x%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],mac[6],mac[7]);
  return String(tmp);
}


void transcode(uint8_t c)
{
  if (c > 10)
  {
    Serial2.write(c);
  }else{
    Serial2.write(0x02);
    Serial2.write((c ^ 0x10));
  }
}

void sendPacket(Packet p){
  Serial2.write(0x01);
  transcode((uint8_t)(p.cmd / 256));
  transcode((uint8_t)(p.cmd % 256));
  transcode((uint8_t)(p.len / 256));
  transcode((uint8_t)(p.len % 256));
  if (p.len >0)
  {
    transcode(getChecksum(p.cmd, p.len, p.datas));
    for (int i=0;i<p.len;i++)
    {
      transcode(p.datas[i]);
    }
  }else{
    transcode(getChecksum(p.cmd, p.len, p.datas));
  }
  Serial2.write(0x03);
  Serial2.flush();
}

void sendZigbeeCmd(Packet p){
    char buff16[5];
    char buff8[3];
    
    DEBUG_PRINT("Trame : 01");
    DEBUG_PRINT(" ");

    snprintf(buff16,5,"%04X", p.cmd);
    DEBUG_PRINT(buff16);
    
    DEBUG_PRINT(" ");
    snprintf(buff16,5,"%04X", p.len);
    DEBUG_PRINT(buff16);
    
    if (p.len >0)
    {
      DEBUG_PRINT(F(" "));
      snprintf(buff8,3,"%02X", getChecksum(p.cmd, p.len, p.datas));
      DEBUG_PRINT(buff8);
      DEBUG_PRINT(F(" "));
      
      for (int i=0;i<p.len;i++)
      {
        snprintf(buff8,3, "%02X",p.datas[i]);
        DEBUG_PRINT(buff8);
      }  
    }else{
      DEBUG_PRINT(F(" "));
      snprintf(buff8,3, "%02X", getChecksum(p.cmd, p.len, p.datas));
      DEBUG_PRINT(buff8);
    }
    DEBUG_PRINT(F(" 03"));
    DEBUG_PRINTLN();
    sendPacket(p);
    
    
}
void sendZigbeePacket(int cmd, int len, uint8_t datas[128])
{
  int i;
  Serial2.write(0x01);
  //DEBUG_PRINT("0x01 ");
  transcode(cmd << 8);
  transcode(cmd);

  transcode(len << 8);
  transcode(len);

  if (len >0)
  {
    Serial2.write(getChecksum(cmd, len, datas));
    //DEBUG_PRINT(getChecksum(cmd, len, datas), HEX);
    for (i=0;i<len;i++)
    {
      transcode(datas[i]);
    }  
  }else{
    Serial2.write(getChecksum(cmd, len, datas));
  }
  Serial2.write(0x03);
  //DEBUG_PRINT("0x03 ");
  Serial2.flush();
}

uint8_t getChecksum(int type, int len, uint8_t datas[256])
{
  uint8_t CRC=0;
  CRC= CRC ^ (type / 256) ^ (type % 256) ^ (len / 256) ^ (len % 256);
  for (int i=0; i<len; i++)
  {
    CRC=CRC ^ datas[i];
  }
  return CRC;
}

void protocolDatas(uint8_t raw[128], int DatasSize)
{ 
  int count;
  int i=0;
  bool stx=false;
  bool transcodage=false;
  for (count=0; count<DatasSize; count++)
  {
   
    if (raw[count]==0x01)
    {
      //Début de trame
      i=0;
      memset(packet,0,0);
      stx = true;
    }else if (raw[count]==0x03)
    {
      if (stx)
      {
        datasManage(packet,i);
      }
    }else if (raw[count]==0x02)
    {
      transcodage= true;
    }else{
      if (transcodage)
      {
        char temp = (int(raw[count]) ^0x10);
        packet[i]=temp;
      }else{
        packet[i]=raw[count];
      }
      transcodage=false;
      i++;
    }
  }
}



bool ScanDeviceToPoll()
{
  File root = LittleFS.open("/db");
  if (!root)
  {
    DEBUG_PRINTLN(F("Failed to open dir"));
    return false;
  }

  if (!root.isDirectory())
  {
    DEBUG_PRINTLN(F("not a dir"));
    return false;
  }
  File filedevice = root.openNextFile();
  while (filedevice) 
  {
      
      if (!filedevice.isDirectory())
      {
        String inifile =  filedevice.name();
        File file = LittleFS.open("/db/"+inifile, FILE_READ);
        if (!file|| file.isDirectory()) {
          DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier ini_read "));
          DEBUG_PRINTLN(inifile);
          file.close();
          break;
        }
        
        // Analyser le contenu JSON du fichier
        DynamicJsonDocument temp(ESP.getMaxAllocHeap());
        DeserializationError error = deserializeJson(temp, file);
        file.close();
        if (error) 
        {
          DEBUG_PRINT(F("ERROR : deserializeJson (ScanDeviceToPoll) "));
          DEBUG_PRINT(inifile);
          DEBUG_PRINTLN(error.c_str());
          break;
        }else
        {
          int shortAddr;
          int cluster;
          int attribut;
          int poll;
          int last;
          int i=0;
          if (temp.containsKey("poll"))
          {
            JsonArray PollArray = temp["poll"].as<JsonArray>();
            for(JsonVariant v : PollArray) 
            {      
                
                shortAddr = (int)temp["INFO"]["shortAddr"];    
                cluster = (int)temp["poll"][i]["cluster"];
                attribut = (int)temp["poll"][i]["attribut"];
                last = (int)temp["poll"][i]["last"];
                temp["poll"][i]["last"] = last - 1;
    
                if (temp["poll"][i]["last"]  <= 0)
                {
                  //lancement de la requête
                  DEBUG_PRINT(F("Lancement de la requête : "));
                  DEBUG_PRINT(F(" sht :"));
                  DEBUG_PRINT(shortAddr);
                  DEBUG_PRINT(F(" cluster :"));
                  DEBUG_PRINT(cluster);
                  DEBUG_PRINT(F(" attr :"));
                  DEBUG_PRINTLN(attribut);
                  //send packet
                  SendAttributeRead(shortAddr, cluster, attribut);
                  temp["poll"][i]["last"]=temp["poll"][i]["poll"];
                }
                i++;
            }
    
            file = LittleFS.open("/db/"+inifile, "w+");
            if (!file|| file.isDirectory()) {
              DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier ini_read "));
              DEBUG_PRINTLN(inifile);
              //return false;
            }
            if (!temp.isNull())
            {
              if (serializeJson(temp, file) == 0) {
                DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier ini_read"));
                file.close();
                break;
              }
            }
            file.close();       
          }
        }
      }
      filedevice = root.openNextFile();    
  }
  root.close();
  return true;
}

bool ScanDeviceToRAZ()
{
  File root = LittleFS.open("/db");
  if (!root)
  {
    DEBUG_PRINTLN(F("Failed to open dir"));
    return false;
  }

  if (!root.isDirectory())
  {
    DEBUG_PRINTLN(F("not a dir"));
    return false;
  }
  File filedevice = root.openNextFile();
  while (filedevice) 
  {
      
      if (!filedevice.isDirectory())
      {
        String inifile =  filedevice.name();
        File file = LittleFS.open("/db/"+inifile, FILE_READ);
        if (!file|| file.isDirectory()) {
          DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier ini_read "));
          DEBUG_PRINTLN(inifile);
          file.close();
          break;
        }
        
        // Analyser le contenu JSON du fichier
        DynamicJsonDocument temp(ESP.getMaxAllocHeap());
        DeserializationError error = deserializeJson(temp, file);
        file.close();
        if (error) 
        {
          DEBUG_PRINT(F("ERROR : deserializeJson (ScanDeviceToRAZ) "));
          DEBUG_PRINT(inifile);
          DEBUG_PRINTLN(error.c_str());
          break;
        }else
        {
          String model = GetModel(inifile);
          if (model=="ZLinky_TIC")
          {
            String lastSeen = ini_read(inifile,"INFO","lastSeen");
            struct tm t;
            time_t t_of_time;
            int y = lastSeen.c_str()[0]*1000 + lastSeen.c_str()[1]*100 + lastSeen.c_str()[2] * 10 + lastSeen.c_str()[3] - 53328;
            int m = lastSeen.c_str()[5]*10 + lastSeen.c_str()[6] - 528;
            int d = lastSeen.c_str()[8]*10 + lastSeen.c_str()[9] - 528;
            int h = lastSeen.c_str()[11]*10 + lastSeen.c_str()[12] - 528;
            int mn = lastSeen.c_str()[14]*10 + lastSeen.c_str()[15] - 528;
            t.tm_hour= h;
            t.tm_min= mn;
            t.tm_mday= d;
            t.tm_mon= m - 1;
            t.tm_year= y - 1900;
            t_of_time = mktime(&t);
            long diff = (epochTime.toDouble() - (long)t_of_time);
            if (diff > 3600)
            {
              //RAZ NRJ
              //hour
              ini_write(inifile,"0B04", "1295", "0");
              ini_write(inifile,"0B04", "2319", "0");
              ini_write(inifile,"0B04", "2575", "0");
              ini_power2(inifile, "1295", "0");
              ini_power2(inifile, "2319", "0");
              ini_power2(inifile, "2575", "0");
              ini_trendPower(inifile,"1295", "0");
              ini_trendPower(inifile,"2319", "0");
              ini_trendPower(inifile,"2575", "0");

              init_raz_energy(inifile, "hour");
            }
            if (diff > 86400)
            {
              init_raz_energy(inifile, "day");
            }
            if (diff > 2678400)
            {
              init_raz_energy(inifile, "month");
            }
          }else if (model == "ZiPulses")
          {
            String lastSeen = ini_read(inifile,"INFO","lastSeen");
            struct tm t;
            time_t t_of_time;
            int y = lastSeen.c_str()[0]*1000 + lastSeen.c_str()[1]*100 + lastSeen.c_str()[2] * 10 + lastSeen.c_str()[3] - 53328;
            int m = lastSeen.c_str()[5]*10 + lastSeen.c_str()[6] - 528;
            int d = lastSeen.c_str()[8]*10 + lastSeen.c_str()[9] - 528;
            int h = lastSeen.c_str()[11]*10 + lastSeen.c_str()[12] - 528;
            int mn = lastSeen.c_str()[14]*10 + lastSeen.c_str()[15] - 528;
            t.tm_hour= h;
            t.tm_min= mn;
            t.tm_mday= d;
            t.tm_mon= m - 1;
            t.tm_year= y - 1900;
            t_of_time = mktime(&t);
            long diff = (epochTime.toDouble() - (long)t_of_time);
            if (diff > 3600)
            {
              init_raz_energy(inifile, "hour");
            }
            if (diff > 86400)
            {
              init_raz_energy(inifile, "day");
            }
            if (diff > 2678400)
            {
              init_raz_energy(inifile, "month");
            }
          }
        }
      }
      filedevice = root.openNextFile();    
  }
  root.close();
  return true;
}
