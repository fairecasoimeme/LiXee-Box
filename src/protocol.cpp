#include <esp_task_wdt.h>
#include <Arduino.h>

#include <esp_system.h>
#include "protocol.h"
#define ARDUINOJSON_USE_LONG_LONG 1
#define ARDUINOJSON_SLOT_ID_SIZE 2
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "SPIFFS_ini.h"
#include "config.h"
#include "log.h"
#include "zigbee.h"
#include "basic.h"



extern struct ZigbeeConfig ZConfig;
extern CircularBuffer<Packet, 100> *commandList;
extern CircularBuffer<Packet, 10> *PrioritycommandList;
extern CircularBuffer<SerialPacket, 300> *QueuePacket;
extern CircularBuffer<SerialPacket, 30> *PriorityQueuePacket;
//extern CircularBuffer<Packet, 10> commandTimedList;
extern CircularBuffer<Alert, 10> *alertList;

extern SemaphoreHandle_t Queue_Mutex ;
extern SemaphoreHandle_t QueuePrio_Mutex;

extern String FormattedDate;
extern String Minute;
extern String Hour;
extern String Day;
extern String Month;
extern String epochTime;
extern unsigned long timeLog;
extern String section[12];

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
    //String path = "/tp/"+(String)deviceId+".json";
    const char* path ="/tp/";
    const char* extension =".json";
    char name_with_extension[64];
    strcpy(name_with_extension,path);
    strcat(name_with_extension,String(deviceId).c_str());
    strcat(name_with_extension,extension);
    File tpFile = LittleFS.open(name_with_extension, FILE_READ);
    if (!tpFile || tpFile.isDirectory()) 
    {
      log_e("failed open");
      
    }else
    {
      DynamicJsonDocument temp(MAXHEAP);
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
            vTaskDelay(1);
          }
      }
          
    }  
    return "";  
}

String GetValueFromShortAddr(int shortAddr,int cluster, int attribute, String value)
{
  /*String inifile;
  inifile = GetMacAdrr(shortAddr);
  return GetValueStatus(inifile, cluster, attribute, String type, float coefficient)*/
}

String GetValueStatus(String inifile, int key, int attribut, String type, float coefficient)
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
  if (!root)
  {
    log_e("Erreur d'ouverture du répertoire db");
    root.close();
    return "";
  }
  if (!root.isDirectory())
  {
    log_e("db n'est pas un répertoire");
    root.close();
    return "";
  }
  File file = root.openNextFile();
  while (file) 
  {
      if (!file.isDirectory())
      {
        if (file.size()>50)
        {
          String tmp =  file.name();
          String Saddr= ini_read(tmp,"INFO", "shortAddr");

          if (shortAddr==atoi(Saddr.c_str()))
          {
            file.close();
            vTaskDelay(1);
            root.close();
            return tmp;
          }   
        }
      }
      file.close();
      vTaskDelay(1);
      file = root.openNextFile(); 
  }  
  root.close();

  // Search on backup
  scanFilesError();

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

void datasManage(char packet[256],int count)
{
  uint8_t CRC=0;
  ZiGateProtocol protocol = {};

  if (count >=6)
  {
    protocol.type = int(packet[0])<<8 ;
    CRC=CRC ^ uint8_t(packet[0]);
    protocol.type |= int(packet[1]) ;
    CRC=CRC ^ uint8_t(packet[1]);
    protocol.ln = int(packet[2])<<8 ;
    CRC=CRC ^ uint8_t(packet[2]);
    protocol.ln |= int(packet[3]) ;
    CRC=CRC ^ uint8_t(packet[3]);
    protocol.chksum = uint8_t(packet[4]);    

    for (int i=5; i< (5+protocol.ln); i++)
    {
      CRC=CRC ^ uint8_t(packet[i]);
      protocol.payload[(i-5)]=packet[i];
    }

    if (protocol.chksum == CRC)
    {
      DecodePayload(protocol,count); 
      memset(packet,0,0);
    }else{   
      for (int i=0; i< count; i++)
      {
        char tmpP[4];
        snprintf(tmpP,3, "%02X",packet[i]);
        DEBUG_PRINT(tmpP);
        DEBUG_PRINT(F(" "));
      }
      memset(packet,0,0);
      addDebugLog(F("CRC error"));
      log_w("CRC error : ");
    }
    
  }else{
    memset(packet,0,0);
    addDebugLog(F("Packet < 6"));
    log_w("Packet < 6");
  }

  size_t HeapSize = ESP.getHeapSize();
  size_t freeMemory = ESP.getFreeHeap();
  size_t freeMemoryPS = ESP.getFreePsram();
  log_d("Core : %d - Heap size : %ld - Free heap : %ld - Free PSRAM: %ld - uxTaskGetStackHighWaterMark: %ld",xPortGetCoreID(),HeapSize,freeMemory,freeMemoryPS,uxTaskGetStackHighWaterMark(NULL));
}

void DecodePayload(struct ZiGateProtocol protocol, int packetSize)
{
  esp_task_wdt_reset();
  switch(protocol.type){
    case 0x8702:
    {
      uint8_t ShortAddr[2];
      ShortAddr[0]=protocol.payload[4];
      ShortAddr[1]=protocol.payload[5];
      String inifile;
      int SA = (int)(ShortAddr[0] * 256)+ShortAddr[1];
      inifile = GetMacAdrr(SA);
      char tmpStatus[4];
      sprintf(tmpStatus,"%02x",protocol.payload[0]);
      ini_write(inifile,"INFO","Status",String(tmpStatus));    
      log_e("8702 - Status : %02X - %02X%02X - %s",protocol.payload[0],protocol.payload[4],protocol.payload[5],inifile.c_str());
      char error[200];
      sprintf(error,"Error Packet : %02x - Device : %02X%02X",protocol.payload[0],protocol.payload[4],protocol.payload[5]);
      alertList->push(Alert{String(error), 1});
      
    }
    break;
    case 0x8000:
      log_d("Status :");
      switch(protocol.payload[0]){
        case 0x0:
          log_d("Success");
          break;
        case 0x1:
          log_d("Inc Param");
          break;
        case 0x2:
          log_d("Unhandled cmd");
          break;
        case 0x3:
          log_d("Cmd Failed");
          break;
        case 0x4:
          log_d("Busy");
          break;
        default:
          log_d("Error");
          break;
      }
      log_d("( %02X )",protocol.payload[0]);
      break;
    case 0x8010:
      ZConfig.type = protocol.payload[0];
      ZConfig.sdk = protocol.payload[1];
      sprintf(ZConfig.application,"%02x%02x",protocol.payload[2],protocol.payload[3]);
      log_d("Version - SDK: %d%d - APP: %s",int(ZConfig.type),int(ZConfig.sdk),ZConfig.application);
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
      ZConfig.zigbeeMac = tmpmac; 
      log_d("Mac coordinator: %08X",tmpmac);
                
      tmp  = (protocol.payload[0]*256)+protocol.payload[1];
      log_d("Short addr: %d",tmp);
      if (tmp == 0xFFFF)
      {
        //network not started
        commandList->push(Packet{0x0024, 0x0000,0});
      }
     }
     break;
     case 0x8024:   
      {

        uint8_t statusNetwork = protocol.payload[0];
        ZConfig.network = statusNetwork;

        uint8_t channel = protocol.payload[11];
        ZConfig.channel=(int)channel;

        log_d("Network: %d - Channel: %d",statusNetwork,int(channel));
      }
      break;
      case 0x8030:
      {
        uint8_t statusBind = protocol.payload[1];
        log_d("Bind Response: %d",statusBind);
        if (statusBind ==0)
        {
          int tmp  = (protocol.payload[3]*256)+protocol.payload[4];
          log_d("Short addr: %02X",tmp);
          //alertList->push(Alert{"BIND OK", 2});
        }
        
      }
      break;
      case 0x8043:   
      {
        log_d("Simple descriptor");
        int shortAddr;
        int device_id;
        int endpoint;
        int nbIN, nbOUT;
        int l;
        int r;
        r = (uint8_t)protocol.payload[1];
        shortAddr = (uint8_t)protocol.payload[2]*256+(uint8_t)protocol.payload[3];
        l = (uint8_t)protocol.payload[4];
        endpoint = (uint8_t)protocol.payload[5];
        device_id = (uint8_t)protocol.payload[8]*256+(uint8_t)protocol.payload[9];

        String path = GetMacAdrr(shortAddr);
        
        if (ini_exist(path))
        {
          WriteIni ini;
          ini.i[0].section ="INFO";
          ini.i[0].key = "device_id";
          ini.i[0].value = (String)device_id;

          //ini_write(path,"INFO", "device_id", (String)device_id);
        
          nbIN= (uint8_t)protocol.payload[11];
          nbOUT=(uint8_t)protocol.payload[11+(nbIN*2)+1];
  
          String tmpIN="";
          String tmpOUT="";
          int i;
          for (i=12;i<(12+(nbIN*2));i=i+2)
          {
            int cluster;
            cluster = (uint8_t)protocol.payload[i]*256+(uint8_t)protocol.payload[i+1];
            if (i>12){tmpIN+=",";}
            tmpIN+=String(cluster);
          
          }
          
          ini.i[1].section =(String)endpoint;
          ini.i[1].key = "IN";
          ini.i[1].value = tmpIN;

          //ini_write(path,(String)endpoint, "IN", tmpIN);
      
          for (i=((12+(nbIN*2))+1);i<(((12+(nbIN*2))+1)+(nbOUT*2));i=i+2)
          {
            int cluster;
            cluster = (uint8_t)protocol.payload[i]*256+(uint8_t)protocol.payload[i+1];
            if (i>((12+(nbIN*2))+1)){tmpOUT+=",";}
            tmpOUT+=(String)cluster;
          }

          ini.i[2].section =(String)endpoint;
          ini.i[2].key = "OUT";
          ini.i[2].value = tmpOUT;

          ini.iniPacketSize = 3;

          ini_writes(path, ini);

          
          //ini_write(path,(String)endpoint, "OUT", tmpOUT);
        }

        //get info basic (Appli version / Manuf / model)
        if (endpoint ==1)
        {
          uint8_t shrtAddr[2];
          shrtAddr[0]=protocol.payload[2];
          shrtAddr[1]=protocol.payload[3];
          SendBasicDescription(shrtAddr,1);
        }

        // backup du fichier
        if (copyFile(path) )
        {
            log_d("File copied successfully.");
        } else {
            log_e("Failed to copy file.");
        }

      }
      break;
      case 0x8045:   
      {
        log_d("Active Response : ");
        uint8_t ShortAddr[2];
        int i;
        if (protocol.payload[1] ==0)
        {
          for (i=0;i<2;i++)
          {
            ShortAddr[i]=(uint8_t)protocol.payload[i+2];
            Serial.print(protocol.payload[i+2], HEX);
          }
          uint8_t nbEndpoint = (uint8_t)protocol.payload[4];
          log_d("%d",nbEndpoint);
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

            
            //TimedFiFo=10000;
            //TimedFiFo=1;
            //commandTimedList.push(trame);
            PrioritycommandList->push(trame);
            //commandList->push(trame);
    //Save endpoint to device
            //insertEndpoint(((protocol.payload[2]*256) + protocol.payload[3]),nbEndpoint);
          }
          log_d("Simple desc : %02x%02x",ShortAddr[0],ShortAddr[1]);

          
        }
      }
      break;
      case 0x8401:
      {
        log_d("Zone status change notification : ");
        uint8_t ShortAddr[4];
        int i;
        uint8_t Cluster[4];
        
        for (i=0;i<2;i++)
        {
          Cluster[i]=(uint8_t)protocol.payload[i+1];
          char tmp[4];
          snprintf(tmp,3,"%02X",protocol.payload[i+1]);

        }
        
        for (i=0;i<2;i++)
        {
          ShortAddr[i]=(uint8_t)protocol.payload[i+4];
          char tmp[4];
          snprintf(tmp,3,"%02X",protocol.payload[i+4]);

        }


        
      }
      break;
      case 0x8002:
      {
        log_d("RAW response : ");
        int i=0;
        /*for (i=0;i<(packetSize -6);i++)
        {
          
        }*/
        
        uint8_t Cluster[2];
        for (i=0;i<2;i++)
        {
          Cluster[i]=(uint8_t)protocol.payload[i+3];
        }
        uint8_t endpoint = (uint8_t)protocol.payload[5];
        uint8_t addressMode = (uint8_t)protocol.payload[7];
        uint8_t ShortAddr[2]; 
        if (addressMode == 2)
        {
           for (i=0;i<2;i++)
           {
              ShortAddr[i]=(uint8_t)protocol.payload[i+8];
           }
        }
        uint8_t DataType;
        uint8_t Command;
        Command = (uint8_t)protocol.payload[15];

        String inifile;
        int SA = (ShortAddr[0] * 256)+ShortAddr[1];
        inifile = GetMacAdrr(SA);
        char tmpStatus[4];
        sprintf(tmpStatus,"%02x",protocol.payload[0]);
        ini_write(inifile,"INFO","Status",String(tmpStatus));


        if ((Command == 10) || (Command == 1))
        {
          uint8_t Attribute[2];
          char tmp[4];

          Attribute[0]=(uint8_t)protocol.payload[17];
          Attribute[1]=(uint8_t)protocol.payload[16];


          int offset, ln;
          if (Command ==10)
          {
            DataType = (uint8_t)protocol.payload[18];
            ln = packetSize-19-6;
            offset = 19;
          }else if (Command ==1)
          {
            DataType = (uint8_t)protocol.payload[19];
            ln = packetSize-20-6;
            offset = 20;
          }

          //Traitement données
          readZigbeeDatas(inifile,Cluster,Attribute,DataType,ln,&protocol.payload[offset]);
        }
        

      }
      break;
      case 0x8100:  
      case 0x8102:   
      {
        log_d("Individual Attribute response : ");
        uint8_t ShortAddr[2];
        int i;
      
        for (i=0;i<2;i++)
        {
          ShortAddr[i]=(uint8_t)protocol.payload[i+1];
        }
              
        uint8_t endpoint = (uint8_t)protocol.payload[3];
        uint8_t Cluster[2];
        for (i=0;i<2;i++)
        {
          Cluster[i]=(uint8_t)protocol.payload[i+4];
        }

        uint8_t Attribute[2];
        for (i=0;i<2;i++)
        {
          Attribute[i]=(uint8_t)protocol.payload[i+6];
        }
        log_d("ShortAddr: %02X%02X - Cluster: %02X%02X - Attribut: %02X%02X",ShortAddr[0],ShortAddr[1],Cluster[0],Cluster[1],Attribute[0],Attribute[1]); 
        uint8_t DataType;
        int ln;
        String inifile;
        int SA = (ShortAddr[0] * 256)+ShortAddr[1];
        inifile = GetMacAdrr(SA);

       
        //ini_write(inifile,"INFO", "lastSeen", FormattedDate);
        if (protocol.payload[8] == 0)
        {
          DataType = (uint8_t)protocol.payload[9];
          ln = (uint8_t)protocol.payload[10]*256 +(uint8_t)protocol.payload[11];
          char lqi[4];
          snprintf(lqi,3, "%02X",protocol.payload[ln+12]);
           
          WriteIni ini;
          ini.i[0].section ="INFO";
          ini.i[0].key = "lastSeen";
          ini.i[0].value = FormattedDate;

          ini.i[1].section ="INFO";
          ini.i[1].key = "LQI";
          ini.i[1].value = String(lqi);

          ini.i[2].section ="INFO";
          ini.i[2].key = "Status";
          ini.i[2].value = "00";

          ini.iniPacketSize = 3;
          log_d("before ini_writes");

          ini_writes(inifile, ini);

          log_d("after ini_writes");
          // ini_write(inifile,"INFO","LQI",String(lqi));
          // ini_write(inifile,"INFO","Status","00");

           //Traitement données
          readZigbeeDatas(inifile,Cluster,Attribute,DataType,ln,&protocol.payload[12]);
          
        }

        //traitement bind lors de la réception du model
        int tmpcluster = Cluster[0]*256+Cluster[1];
        int tmpattribute = Attribute[0]*256+Attribute[1];
        if ((tmpcluster == 0) && (tmpattribute == 5))
        {  
          String tmpMac;
          String model;
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

          //Traitement spécifique seloin modèle
          SpecificTreatment(ShortAddr,model);
          log_d("SpecificTreatment");
         
          vTaskDelay(100);
          alertList->push(Alert{"Bind waiting...", 2});
          getBind(macInt,DeviceId,model);
          log_d("getBind");
          vTaskDelay(100);
          // Traitement config report
          alertList->push(Alert{"Config Report waiting...", 2});
          getConfigReport(ShortAddr,DeviceId,model);
          log_d("getConfigReport");
          vTaskDelay(100);
          // Traitement polling
          getPollingDevice(ShortAddr,DeviceId,model);
          log_d("getPollingDevice");
          vTaskDelay(100);
          alertList->push(Alert{"Config OK", 2});
          //Afficher la find de la config
          
          // backup du fichier
          if (copyFile(inifile) )
          {
              log_d("File copied successfully.");
          } else {
              log_e("Failed to copy file.");
          }
          
          
        }
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
         log_d("Config report response : ");
         char configReport[200];
         sprintf(configReport,"Config Report (%02X%02X) : Cluster : %02X%02X - Status : %02X",protocol.payload[1],protocol.payload[2],protocol.payload[4],protocol.payload[5],protocol.payload[6]);
         //alertList->push(Alert{configReport, 2});
         
      }
      break;
      case 0x8048:   
      {
        
        char mac[20];
        int i;
        for (i=0;i<9;i++)
        {
          mac[i]=protocol.payload[i];
        }
        char adMac[20];
        sprintf(adMac,"%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],mac[6],mac[7]);
        log_d("Leave Network : %02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],mac[6],mac[7]);
        alertList->push(Alert{"Device leaved : "+String(adMac), 1});
        //Supprimer dans la base
      }
      break;
      case 0x004d:   
      {
        int ShortAddr;
        uint8_t mac[9];
        int i;
        ShortAddr=(protocol.payload[0]*256) + protocol.payload[1];
        
        char tmp[6];
        snprintf(tmp,5, "%02X%02X",protocol.payload[0],protocol.payload[1]);
        alertList->push(Alert{"Device Joined : "+String(tmp), 0});
        log_d("Node Joined : %02X%02X- ",protocol.payload[0],protocol.payload[1]);

        for (i=2;i<10;i++)
        {
          mac[i-2]=protocol.payload[i];

        }

        char lqi[4];
        for (i=11;i<12;i++)
        {
          snprintf(lqi, 3,"%02X",protocol.payload[i]);
        }
       
        //Add dans la base
        String adMac;
        adMac = getMacAddress(mac);
     
        String path = adMac+".json";

        if (ini_exist(path))
        {
          WriteIni ini ;
          ini.i[0].section ="INFO";
          ini.i[0].key = "shortAddr";
          ini.i[0].value = String(ShortAddr);

          ini.i[1].section ="INFO";
          ini.i[1].key = "LQI";
          ini.i[1].value = String(lqi);

          ini.iniPacketSize = 2;

          ini_writes(path, ini);

        }

        //ini_write(path,"INFO","shortAddr",String(ShortAddr));
        //ini_write(path,"INFO","LQI",String(lqi));
    
        //Demande Active Request
        if (protocol.payload[11]>0)
        {
          Packet trame;
          trame.cmd=0x0045;
          trame.len=0x0002;
          memcpy(trame.datas,protocol.payload,2);
          log_d("Active Req : %s",String(ShortAddr)); 
          commandList->push(trame);
          //PrioritycommandList->push(trame);
        }

      }
      break;
    default:
      log_d("Packet Unknow : %02X",protocol.type);

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
  char output_sprintf[3];
 
  if (c > 10)
  {
    Serial1.write(c);
    logPush(' ');
    sprintf(output_sprintf,"%02x",c);
    logPush(output_sprintf[0]);
    logPush(output_sprintf[1]);
  }else{
    Serial1.write(0x02);
    logPush(' ');
    logPush('0');
    logPush('1');
    Serial1.write((c ^ 0x10));
    logPush(' ');
    sprintf(output_sprintf,"%02x",(c ^ 0x10));
    logPush(output_sprintf[0]);
    logPush(output_sprintf[1]);
  }
}

void sendPacket(Packet p){


  logPush('[');
  for (int j =0;j<FormattedDate.length();j++)
  {
    logPush(FormattedDate[j]);
  }
  logPush(']');
  logPush('-');
  logPush('>');
    
  Serial1.write(0x01);
  logPush(' ');
  logPush('0');
  logPush('1');

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
  Serial1.write(0x03);
  logPush(' ');
  logPush('0');
  logPush('3');
  logPush('\n');

  Serial1.flush();
}

void sendZigbeeCmd(Packet p){
    char buff16[6];
    char buff8[4];
    
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
void sendZigbeePacket(int cmd, int len, uint8_t datas[512])
{
  int i;
  Serial1.write(0x01);

  transcode(cmd << 8);
  transcode(cmd);

  transcode(len << 8);
  transcode(len);

  if (len >0)
  {
    Serial1.write(getChecksum(cmd, len, datas));
    for (i=0;i<len;i++)
    {
      transcode(datas[i]);
    }  
  }else{
    Serial1.write(getChecksum(cmd, len, datas));
  }
  Serial1.write(0x03);
  Serial1.flush();
}

uint8_t getChecksum(int type, int len, uint8_t datas[512])
{
  uint8_t CRC=0;
  CRC= CRC ^ (type / 256) ^ (type % 256) ^ (len / 256) ^ (len % 256);
  for (int i=0; i<len; i++)
  {
    CRC=CRC ^ datas[i];
  }
  return CRC;
}

//void protocolDatas(String sp)
void protocolDatas(uint8_t sp[4092], size_t len)
{ 
  int count;
  int i=0;
  int j=0;
  bool stx=false;
  bool transcodage=false;
  char packet[256];
  int numPacket = 0;
 
  //for (count=0; count<sp.length(); count++)
  for (count=0; count<len; count++)
  {
    j++;
    if (sp[count]==0x01)
    {
      //Début de trame
      i=0;
      memset(packet,0,0);
      stx = true;
    }else if (sp[count]==0x03)
    {
      if (stx)
      {
        stx=false;
        //log_d("core : %d - numPacket: %d - i: %d - j : %d - Total: %d",xPortGetCoreID(),numPacket,i,j,sp.length());
        log_d("core : %d - numPacket: %d - i: %d - j : %d - Total: %d",xPortGetCoreID(),numPacket,i,j,len);
        SerialPacket sp;
        sp.len=i;
        memcpy(sp.raw,packet,i);
        int type;
        type = int(packet[0])<<8 ;
        type |= int(packet[1]) ;

        if ((type == 0x4d) || (type == 0x8043) || (type == 0x8045))
        {
          if (!PriorityQueuePacket->isFull())
          {
            xSemaphoreTake(QueuePrio_Mutex, portMAX_DELAY);
            PriorityQueuePacket->push(sp);
            xSemaphoreGive(QueuePrio_Mutex);
          }
        }else if((type == 0x8011) || (type == 0x8012))
        {
            //Ignore packet
        }else{
          if (!QueuePacket->isFull())
          {
            xSemaphoreTake(Queue_Mutex, portMAX_DELAY);
            QueuePacket->push(sp);
            xSemaphoreGive(Queue_Mutex);
          }else{
            addDebugLog("QueuePacket FULL !");
            while (!QueuePacket->isEmpty())
            {
              DEBUG_PRINTLN("Packet shift : protocol datas");
              SerialPacket packet;
              xSemaphoreTake(Queue_Mutex, portMAX_DELAY);
              packet = (SerialPacket)QueuePacket->shift();
              xSemaphoreGive(Queue_Mutex);
              datasManage((char *)packet.raw,packet.len);
              vTaskDelay(10);
            }
            xSemaphoreTake(Queue_Mutex, portMAX_DELAY);
            QueuePacket->push(sp);
            xSemaphoreGive(Queue_Mutex);
          }
        }
        memset(packet,0,0);
        //datasManage(packet,i);
        i=0;
        numPacket++;
      }
    }else if (sp[count]==0x02)
    {
      transcodage= true;
    }else{
      if (transcodage)
      {
        transcodage=false;
        char temp = (int(sp[count]) ^0x10);
        packet[i]=temp;
      }else{
        packet[i]=sp[count];
      }
      i++;
    }
  }
}



bool ScanDeviceToPoll()
{
  File root = LittleFS.open("/db");
  if (!root)
  {
    log_e("Failed to open dir");
    root.close();
    return false;
  }

  if (!root.isDirectory())
  {
    log_e("not a dir");
    root.close();
    return false;
  }
  File filedevice = root.openNextFile();
  while (filedevice) 
  {
      
      if (!filedevice.isDirectory())
      {
        if (filedevice.size()>0)
        {
          const char* inifile =  filedevice.name();
          const char* path ="/db/";
          char name_with_extension[64];
          strcpy(name_with_extension,path);
          strcat(name_with_extension,inifile);
          File file = LittleFS.open(name_with_extension, FILE_READ);
          if (!file|| file.isDirectory()) {
            log_e("Erreur lors de l'ouverture du fichier ini_read %s",inifile);
            file.close();
            break;
          }
          
          // Analyser le contenu JSON du fichier
          DynamicJsonDocument temp(MAXHEAP);
          DeserializationError error = deserializeJson(temp, file);
          file.close();
          if (error) 
          {
            log_e("ERROR : deserializeJson (ScanDeviceToPoll) %s : %s",inifile,error.c_str() );
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
                    log_d("Lancement de la requête / sht: %d - cluster: %d - attr: %d",shortAddr,cluster,attribut);
                    //send packet
                    SendAttributeRead(shortAddr, cluster, attribut);
                    temp["poll"][i]["last"]=temp["poll"][i]["poll"];
                  }
                  i++;
              }

              file = safeOpenFile(name_with_extension, "w+");
              if (!file|| file.isDirectory()) {
                log_e("Erreur lors de l'ouverture du fichier ini_read %s",inifile);
                safeCloseFile(file,name_with_extension);

              }
              if (!temp.isNull())
              {
                if (serializeJson(temp, file) == 0) {
                  log_e("Erreur lors de l'écriture dans le fichier ini_read");
                  safeCloseFile(file,name_with_extension);
                  break;
                }
              }
              safeCloseFile(file,name_with_extension);      
            }
          }
        }
      }
      filedevice.close();
      vTaskDelay(1);
      filedevice = root.openNextFile();    
  }
  root.close();
  return true;
}


