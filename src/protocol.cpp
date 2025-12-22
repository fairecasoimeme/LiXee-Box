#include <esp_task_wdt.h>
#include <Arduino.h>
#include <vector>
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
#include "Infrared.h"
#include "thermostat.h"

#include "device.h"

extern std::vector<DeviceData*> devices;

extern struct ZigbeeConfig ZConfig;
extern ConfigNotification ConfigNotif;


// Variables globales pour monitoring CRC
uint32_t lastCrcError = 0;
uint32_t crcErrorCount = 0;
extern CircularBuffer<Packet, 100> *commandList;
extern CircularBuffer<Packet, 70> *PrioritycommandList;
extern CircularBuffer<SerialPacket, 300> *QueuePacket;
extern CircularBuffer<SerialPacket, 30> *PriorityQueuePacket;

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


//OTA 
extern uint8_t* au8OTAFile;

const size_t OTA_BUFFER_SIZE = 500000;
uint32_t u32OtaFileIdentifier;
uint16_t u16OtaFileHeaderVersion;
uint16_t u16OtaFileHeaderLength;
uint16_t u16OtaFileHeaderControlField;
uint16_t u16OtaFileManufacturerCode;
uint16_t u16OtaFileImageType;
uint32_t u32OtaFileVersion;
uint16_t u16OtaFileStackVersion;
uint32_t u32OtaFileTotalImage;
uint8_t u8OtaFileSecurityCredVersion;
uint64_t u64OtaFileUpgradeFileDest;
uint16_t u16OtaFileMinimumHwVersion;
uint16_t u16OtaFileMaxHwVersion;
uint8_t au8OtaFileHeaderString[32];

// Structure pour stocker les informations OTA
struct OTAFileInfo {
    bool loaded;
    String filename;
    uint32_t fileSize;
    String headerString;
    uint32_t fileIdentifier;
    uint16_t headerVersion;
    uint16_t headerLength;
    uint16_t headerControlField;
    uint16_t manufacturerCode;
    uint16_t imageType;
    uint32_t fileVersion;
    uint16_t stackVersion;
    uint32_t totalImage;
    uint8_t securityCredVersion;
    uint64_t upgradeFileDest;
    uint16_t minimumHwVersion;
    uint16_t maxHwVersion;
};

OTAFileInfo currentOTAInfo = {false, "", 0, "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

extern byte u8OTAWaitForDataParamsPending;
extern uint16_t u16OTAWaitForDataParamsTargetAddr;
extern byte u8OTAWaitForDataParamsSrcEndPoint;
extern uint32_t u32OTAWaitForDataParamsCurrentTime;
extern uint32_t u32OTAWaitForDataParamsRequestTime;
extern uint16_t u16OTAWaitForDataParamsBlockDelay;
extern uint32_t u32OtaFileTotalImage;

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
      SpiRamJsonDocument temp(MAXHEAP);
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
  return "";
}

String GetValueStatus(String IEEE, int key, int attribut, String type, float coefficient)
{
  String tmp;
  char tmpKey[5];
  sprintf(tmpKey,"%04X",key);
  for (size_t i = 0; i < devices.size(); i++) 
  {
      DeviceData* device = devices[i];
      if (device->getDeviceID() == IEEE)
      {
        tmp = device->getValue(std::string(tmpKey),std::string(String(attribut).c_str()));
        break;
      }
  }

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


/*String GetValueStatus(String inifile, int key, int attribut, String type, float coefficient)
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
}*/

void lastSeen(int shortAddr)
{
  String path = GetMacAdrr(shortAddr);
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == path.substring(0,16))
    {
      device->setInfoLastseen(FormattedDate);
      break;
    }
  }
  //ini_write(path,"INFO", "lastSeen", FormattedDate);

}

DeviceData *getDeviceShortAddr(int shortAddr)
{
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getInfo().shortAddr.toInt() == shortAddr)
    {
      return device;
    }
  }
  return NULL;
}

String GetMacAdrr(int shortAddr)
{

  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getInfo().shortAddr.toInt() == shortAddr)
    {
      return device->getDeviceID()+".json";
    }
  }


 // char SAddr[20];
/*File root = LittleFS.open("/db");
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
    esp_task_wdt_reset();
    if (!file.isDirectory())
    {
      if (file.size()>0)
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
*/
  // Search on backup
  scanFilesError();

  return "";
}

String GetLastSeen(String inifile)
{
   /*String tmp= ini_read(inifile,"INFO", "lastSeen");  
   return tmp;*/
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == inifile.substring(0,16))
    {
      return device->getInfo().lastSeen;
    }
  }
  return String("");
}

String GetLQI(String inifile)
{
   /*String tmp= ini_read(inifile,"INFO", "LQI");  
   int tmp2 = (int) strtol(tmp.c_str(), 0, 16);
   return String(tmp2);*/
   for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == inifile.substring(0,16))
    {
      return device->getInfo().LQI;
    }
  }
  return String("");
}

String GetEndpoint(String inifile)
{
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == inifile.substring(0, 16))
    {
      return device->getInfo().endpoint;
    }
  }
  return String("1");
}

int GetShortAddr(String inifile)
{
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == inifile.substring(0,16))
    {
      return device->getInfo().shortAddr.toInt();
    }
  }
  return 0;
}

int GetDeviceId(String inifile)
{
   /*String tmp= ini_read(inifile,"INFO", "device_id");  
   return tmp.toInt();*/
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == inifile.substring(0,16))
    {
      return device->getInfo().device_id.toInt();
    }
  }
  return 0;
}

String GetSoftwareVersion(String inifile)
{
  /*String tmp= ini_read(inifile,"INFO", "software_version"); 
  return tmp;*/
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == inifile.substring(0,16))
    {
      return device->getInfo().software_version;
    }
  }
  return String("");
}

void SetInfoStatus( String inifile, String val)
{
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == inifile.substring(0, 16))
    {
      device->setInfoStatus(val);
      break;
    }
  }
}

void SetInfoDeviceId( String inifile, String val)
{
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == inifile.substring(0, 16))
    {
      device->setInfoDeviceID(val);
      break;
    }
  }
}

void SetInfoPowerSocket( String inifile, String val)
{
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == inifile.substring(0, 16))
    {
      device->setInfoPowerSocket(val);
      break;
    }
  }
}

void SetInfoEndpoint( String inifile, String val)
{
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == inifile.substring(0, 16))
    {
      device->setInfoEndpoint(val);
      break;
    }
  }
}

void SetInfoLastseen( String inifile, String val)
{
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == inifile.substring(0, 16))
    {
      device->setInfoLastseen(val);
      break;
    }
  }
}

void SetInfoLQI( String inifile, String val)
{
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == inifile.substring(0, 16))
    {
      device->setInfoLQI(val);
      break;
    }
  }
}

bool deviceExist(String mac)
{
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == mac)
    {
      return true;
    }
  }
  return false;
}


void datasManage(char packet[256],int count)
{
  // Vérification des bounds AVANT traitement
  if (count > 256 || count < 6) {
      log_e("Invalid packet size: %d", count);
      return;
  }
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

    if (protocol.ln > (256 - 5)) {
        log_e("Payload too large: %d", protocol.ln);
        return;
    }
    
    // Protection supplémentaire
    for (int i = 5; i < (5 + protocol.ln) && i < count; i++) {
      if ((i-5) >= sizeof(protocol.payload)) {
          log_e("Payload buffer overflow prevented");
          break;
      }
      CRC = CRC ^ uint8_t(packet[i]);
      protocol.payload[(i-5)] = packet[i];
    }

    if (protocol.chksum == CRC)
    {
      DecodePayload(protocol,count); 
      memset(packet,0,0);
    }else{   
      crcErrorCount++;
      uint32_t now = millis();
      uint32_t interval = now - lastCrcError;
      lastCrcError = now;
      
      log_e("🔴 CRC ERROR #%d - Intervalle: %dms", crcErrorCount, interval);
      log_e("   Type: 0x%04X, Len: %d, CRC calc: 0x%02X, CRC reçu: 0x%02X", 
            protocol.type, protocol.ln, CRC, protocol.chksum);
      log_e("   Core: %d, Task: %s, Free heap: %d", 
            xPortGetCoreID(), pcTaskGetName(NULL), ESP.getFreeHeap());      
      // Afficher les premiers bytes pour pattern analysis
      log_e("   Packet: %02X %02X %02X %02X %02X [%d bytes total]",
            packet[0], packet[1], packet[2], packet[3], packet[4], count);
      
      // Auto-recovery si trop d'erreurs consécutives
      if (crcErrorCount > 10 && interval < 5000) {
        log_e("🔥 TROP D'ERREURS CRC - TENTATIVE DE RECOVERY");
        Serial1.flush(); // Vider le buffer série
        vTaskDelay(100);  // Pause pour resync
        crcErrorCount = 0; // Reset compteur
      }
      
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
  Serial.printf("📊 - Core : %d - Heap size : %ld - Free heap : %ld - Free PSRAM: %ld - uxTaskGetStackHighWaterMark: %ld \r\n",xPortGetCoreID(),HeapSize,freeMemory,freeMemoryPS,uxTaskGetStackHighWaterMark(NULL));

}

bool loadOTAFile(const char* filename) {
    
    au8OTAFile = (uint8_t*)ps_malloc(OTA_BUFFER_SIZE);
    if (!au8OTAFile) {
        Serial.println("Erreur: Impossible d'allouer la mémoire PSRAM");
        return false;
    }
  
    // Ouvrir le fichier OTA
    String path = "/ota/" + String(filename);
    DEBUG_PRINTLN(path);
    File otaFile = LittleFS.open(path, "r");
    if (!otaFile) {
        Serial.println("Erreur: Impossible d'ouvrir le fichier OTA");
        return false;
    }
    
    // Vérifier la taille du fichier
    size_t fileSize = otaFile.size();
    if (fileSize > OTA_BUFFER_SIZE) {
        Serial.println("Erreur: Fichier trop volumineux");
        otaFile.close();
        return false;
    }
    
    // Lire le fichier dans le buffer
    size_t bytesRead = otaFile.read(au8OTAFile, fileSize);
    otaFile.close();
    
    if (bytesRead != fileSize) {
        Serial.println("Erreur: Lecture incomplète du fichier");
        return false;
    }
    
    // Extraire les informations du header (Little-endian pour ESP32)
    u32OtaFileIdentifier = (uint32_t)au8OTAFile[0] | 
                          ((uint32_t)au8OTAFile[1] << 8) | 
                          ((uint32_t)au8OTAFile[2] << 16) | 
                          ((uint32_t)au8OTAFile[3] << 24);
    
    u16OtaFileHeaderVersion = (uint16_t)au8OTAFile[4] | 
                             ((uint16_t)au8OTAFile[5] << 8);
    
    u16OtaFileHeaderLength = (uint16_t)au8OTAFile[6] | 
                            ((uint16_t)au8OTAFile[7] << 8);
    
    u16OtaFileHeaderControlField = (uint16_t)au8OTAFile[8] | 
                                  ((uint16_t)au8OTAFile[9] << 8);
    
    u16OtaFileManufacturerCode = (uint16_t)au8OTAFile[10] | 
                                ((uint16_t)au8OTAFile[11] << 8);
    
    u16OtaFileImageType = (uint16_t)au8OTAFile[12] | 
                         ((uint16_t)au8OTAFile[13] << 8);
    
    u32OtaFileVersion = (uint32_t)au8OTAFile[14] | 
                       ((uint32_t)au8OTAFile[15] << 8) | 
                       ((uint32_t)au8OTAFile[16] << 16) | 
                       ((uint32_t)au8OTAFile[17] << 24);
    
    u16OtaFileStackVersion = (uint16_t)au8OTAFile[18] | 
                            ((uint16_t)au8OTAFile[19] << 8);
    
    // Extraire le header string (32 bytes à partir de l'offset 20)
    for (uint8_t i = 0; i < 32; i++) {
        au8OtaFileHeaderString[i] = au8OTAFile[20 + i];
    }
    
    u32OtaFileTotalImage = (uint32_t)au8OTAFile[52] | 
                          ((uint32_t)au8OTAFile[53] << 8) | 
                          ((uint32_t)au8OTAFile[54] << 16) | 
                          ((uint32_t)au8OTAFile[55] << 24);
    
    u8OtaFileSecurityCredVersion = au8OTAFile[56];
    
    u64OtaFileUpgradeFileDest = (uint64_t)au8OTAFile[57] | 
                               ((uint64_t)au8OTAFile[58] << 8) | 
                               ((uint64_t)au8OTAFile[59] << 16) | 
                               ((uint64_t)au8OTAFile[60] << 24) | 
                               ((uint64_t)au8OTAFile[61] << 32) | 
                               ((uint64_t)au8OTAFile[62] << 40) | 
                               ((uint64_t)au8OTAFile[63] << 48) | 
                               ((uint64_t)au8OTAFile[64] << 56);
    
    u16OtaFileMinimumHwVersion = (uint16_t)au8OTAFile[65] | 
                                ((uint16_t)au8OTAFile[66] << 8);
    
    u16OtaFileMaxHwVersion = (uint16_t)au8OTAFile[67] | 
                            ((uint16_t)au8OTAFile[68] << 8);
    
    // Mettre à jour la structure d'informations
    currentOTAInfo.loaded = true;
    currentOTAInfo.filename = String(filename);
    currentOTAInfo.fileSize = fileSize;
    currentOTAInfo.fileIdentifier = u32OtaFileIdentifier;
    currentOTAInfo.headerVersion = u16OtaFileHeaderVersion;
    currentOTAInfo.headerLength = u16OtaFileHeaderLength;
    currentOTAInfo.headerControlField = u16OtaFileHeaderControlField;
    currentOTAInfo.manufacturerCode = u16OtaFileManufacturerCode;
    currentOTAInfo.imageType = u16OtaFileImageType;
    currentOTAInfo.fileVersion = u32OtaFileVersion;
    currentOTAInfo.stackVersion = u16OtaFileStackVersion;
    currentOTAInfo.totalImage = u32OtaFileTotalImage;
    currentOTAInfo.securityCredVersion = u8OtaFileSecurityCredVersion;
    currentOTAInfo.upgradeFileDest = u64OtaFileUpgradeFileDest;
    currentOTAInfo.minimumHwVersion = u16OtaFileMinimumHwVersion;
    currentOTAInfo.maxHwVersion = u16OtaFileMaxHwVersion;
    
    // Convertir le header string en String
    currentOTAInfo.headerString = "";
    for (int i = 0; i < 32; i++) {
        if (au8OtaFileHeaderString[i] != 0) {
            currentOTAInfo.headerString += (char)au8OtaFileHeaderString[i];
        }
    }

    DeviceInfo di;
    String tmpName = String(filename).substring(0,16) + ".json";
    DEBUG_PRINTLN("---------------");
    DEBUG_PRINTLN(tmpName);
    di =getDeviceInfo(tmpName);

    sendOtaLoadNewImage(di.shortAddr, // u16ShortAddr - modifiez selon vos besoins
                            u32OtaFileIdentifier,
                            u16OtaFileHeaderVersion,
                            u16OtaFileHeaderLength,
                            u16OtaFileHeaderControlField,
                            u16OtaFileManufacturerCode,
                            u16OtaFileImageType,
                            u32OtaFileVersion,
                            u16OtaFileStackVersion,
                            au8OtaFileHeaderString,
                            u32OtaFileTotalImage,
                            u8OtaFileSecurityCredVersion,
                            u64OtaFileUpgradeFileDest,
                            u16OtaFileMinimumHwVersion,
                            u16OtaFileMaxHwVersion);

    sendOtaImageNotify(di.shortAddr,0,u32OtaFileVersion,u16OtaFileImageType,u16OtaFileManufacturerCode,0);

    return true;
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
      //ini_write(inifile,"INFO","Status",String(tmpStatus));  
      SetInfoStatus(inifile,String(tmpStatus));
      
      log_e("8702 - Status : %02X - %02X%02X - %s",protocol.payload[0],protocol.payload[4],protocol.payload[5],inifile.c_str());
      char error[200];
      sprintf(error,"Error Packet : %02x - Device : %02X%02X",protocol.payload[0],protocol.payload[4],protocol.payload[5]);
      alertList->push(Alert{String(error), 1});
      
    }
    break;
    //OTA
    case 0x8501:
    {
        byte u8Offset = 0;
        byte u8SQN;
        byte u8SrcEndpoint;
        uint16_t u16ClusterId;
        uint16_t u16SrcAddr;
        byte u8SrcAddrMode;
        uint64_t u64RequestNodeAddress;
        uint32_t u32FileOffset;
        uint32_t u32FileVersion;
        uint16_t u16ImageType;
        uint16_t u16ManufactureCode;
        uint16_t u16BlockRequestDelay;
        byte u8MaxDataSize;
        byte u8FieldControl;

        u8SQN = protocol.payload[u8Offset++];

        u8SrcEndpoint = protocol.payload[u8Offset++];

        u16ClusterId = protocol.payload[u8Offset++];
        u16ClusterId <<= 8;
        u16ClusterId |= protocol.payload[u8Offset++];

        u8SrcAddrMode = protocol.payload[u8Offset++];

        u16SrcAddr = protocol.payload[u8Offset++];
        u16SrcAddr <<= 8;
        u16SrcAddr |= protocol.payload[u8Offset++];

        u64RequestNodeAddress = protocol.payload[u8Offset++];
        u64RequestNodeAddress <<= 8;
        u64RequestNodeAddress |= protocol.payload[u8Offset++];
        u64RequestNodeAddress <<= 8;
        u64RequestNodeAddress |= protocol.payload[u8Offset++];
        u64RequestNodeAddress <<= 8;
        u64RequestNodeAddress |= protocol.payload[u8Offset++];
        u64RequestNodeAddress <<= 8;
        u64RequestNodeAddress |= protocol.payload[u8Offset++];
        u64RequestNodeAddress <<= 8;
        u64RequestNodeAddress |= protocol.payload[u8Offset++];
        u64RequestNodeAddress <<= 8;
        u64RequestNodeAddress |= protocol.payload[u8Offset++];
        u64RequestNodeAddress <<= 8;
        u64RequestNodeAddress |= protocol.payload[u8Offset++];

        u32FileOffset = protocol.payload[u8Offset++];
        u32FileOffset <<= 8;
        u32FileOffset |= protocol.payload[u8Offset++];
        u32FileOffset <<= 8;
        u32FileOffset |= protocol.payload[u8Offset++];
        u32FileOffset <<= 8;
        u32FileOffset |= protocol.payload[u8Offset++];

        u32FileVersion = protocol.payload[u8Offset++];
        u32FileVersion <<= 8;
        u32FileVersion |= protocol.payload[u8Offset++];
        u32FileVersion <<= 8;
        u32FileVersion |= protocol.payload[u8Offset++];
        u32FileVersion <<= 8;
        u32FileVersion |= protocol.payload[u8Offset++];

        u16ImageType = protocol.payload[u8Offset++];
        u16ImageType <<= 8;
        u16ImageType |= protocol.payload[u8Offset++];

        u16ManufactureCode = protocol.payload[u8Offset++];
        u16ManufactureCode <<= 8;
        u16ManufactureCode |= protocol.payload[u8Offset++];

        u16BlockRequestDelay = protocol.payload[u8Offset++];
        u16BlockRequestDelay <<= 8;
        u16BlockRequestDelay |= protocol.payload[u8Offset++];

        u8MaxDataSize = protocol.payload[u8Offset++];

        u8FieldControl = protocol.payload[u8Offset++];

        DeviceData *device = getDeviceShortAddr((int)u16SrcAddr);

        // Send response 
        if (u8OTAWaitForDataParamsPending == 0)
        {
            byte u8NbrBytes = 0;

            if ((u32FileOffset + u8MaxDataSize) > u32OtaFileTotalImage)
            {
                u8NbrBytes = (byte)(u32OtaFileTotalImage - u32FileOffset);
            }
            else
            {
                u8NbrBytes = u8MaxDataSize;
            }
            if (au8OTAFile == NULL)
            {
              String path = device->getDeviceID()+".ota";
              loadOTAFile(path.c_str());
            }
            Serial.printf("🌐 ----offset : %ld / u8NbrBytes : %ld\n",u32FileOffset,u8NbrBytes);
            sendOtaBlock( u16SrcAddr, u8SQN, 0, u32FileOffset, u32FileVersion, u16ImageType, u16ManufactureCode, u8NbrBytes, au8OTAFile);
        }
        else
        {
            sendOtaSetWaitForDataParams( u16SrcAddr, u8SQN, 0x97, u32OTAWaitForDataParamsCurrentTime, u32OTAWaitForDataParamsRequestTime, u16OTAWaitForDataParamsBlockDelay);
            u8OTAWaitForDataParamsPending = 0;
        }

        if (device->otaInProgress != 1)
        {           
            device->otaInProgress=1;
            device->otaPercentage=0;

        }
        else
        {
            uint32_t u32PercentComplete = (u32FileOffset * 100) / u32OtaFileTotalImage;
            device->otaPercentage=u32PercentComplete;

        }
    }
    break;
    case 0x8503:
    {
      byte u8Offset = 0;
      byte u8SQN;
      byte u8SrcEndpoint;
      uint16_t u16ClusterId;
      uint16_t u16SrcAddr;
      byte u8SrcAddrMode;
      uint32_t u32FileVersion;
      uint16_t u16ImageType;
      uint16_t u16ManufactureCode;
      byte u8Status;

      u8SQN = protocol.payload[u8Offset++];

      u8SrcEndpoint = protocol.payload[u8Offset++];

      u16ClusterId = protocol.payload[u8Offset++];
      u16ClusterId <<= 8;
      u16ClusterId |= protocol.payload[u8Offset++];

      u8SrcAddrMode = protocol.payload[u8Offset++];

      u16SrcAddr = protocol.payload[u8Offset++];
      u16SrcAddr <<= 8;
      u16SrcAddr |= protocol.payload[u8Offset++];

      u32FileVersion = protocol.payload[u8Offset++];
      u32FileVersion <<= 8;
      u32FileVersion |= protocol.payload[u8Offset++];
      u32FileVersion <<= 8;
      u32FileVersion |= protocol.payload[u8Offset++];
      u32FileVersion <<= 8;
      u32FileVersion |= protocol.payload[u8Offset++];

      u16ImageType = protocol.payload[u8Offset++];
      u16ImageType <<= 8;
      u16ImageType |= protocol.payload[u8Offset++];

      u16ManufactureCode = protocol.payload[u8Offset++];
      u16ManufactureCode <<= 8;
      u16ManufactureCode |= protocol.payload[u8Offset++];

      u8Status = protocol.payload[u8Offset++];

      sendOtaEndResponse(u16SrcAddr, u8SQN, 5, 10, u32FileVersion, u16ImageType, u16ManufactureCode);
      SendAttributeRead((int)u16SrcAddr,1,0,5);

      DeviceData *device = getDeviceShortAddr((int)u16SrcAddr);
      device->otaInProgress=0;
      
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
        
        if (endpoint != 0xf2)
        {
          String path = GetMacAdrr(shortAddr);
          
          log_e("DEVICE_ID : %d \n",device_id);
          SetInfoDeviceId(path,String(device_id));
        
          nbIN= (uint8_t)protocol.payload[11];
          nbOUT=(uint8_t)protocol.payload[11+(nbIN*2)+1];

          String tmpIN="";
          String tmpOUT="";
          int i;
          for (i=12;i<(12+(nbIN*2));i=i+2)
          {
            int cluster;
            cluster = (uint8_t)protocol.payload[i]*256+(uint8_t)protocol.payload[i+1];
            if (cluster == 6)
            {
              SetInfoPowerSocket(path,"1");
            }
            if (i>12){tmpIN+=",";}
            tmpIN+=String(cluster);
          
          }
      
          for (i=((12+(nbIN*2))+1);i<(((12+(nbIN*2))+1)+(nbOUT*2));i=i+2)
          {
            int cluster;
            cluster = (uint8_t)protocol.payload[i]*256+(uint8_t)protocol.payload[i+1];
            if (i>((12+(nbIN*2))+1)){tmpOUT+=",";}
            tmpOUT+=(String)cluster;
          }
          
          
          //get info basic (Appli version / Manuf / model)
            SetInfoEndpoint(path,String(endpoint));
            uint8_t shrtAddr[2];
            shrtAddr[0]=protocol.payload[2];
            shrtAddr[1]=protocol.payload[3];
            SendBasicDescription(shrtAddr,1);

          // backup du fichier
          if (copyFile(path) )
          {
              log_d("File copied successfully.");
          } else {
              log_e("Failed to copy file.");
          }
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

            PrioritycommandList->push(trame);

          }
          log_d("Simple desc : %02x%02x",ShortAddr[0],ShortAddr[1]);

          
        }
      }
      break;
      case 0x8401:
      {
        log_d("Zone status change notification : ");
        
        int i;

        for (i=0;i<2;i++)
        {
          uint8_t Cluster[4];
          Cluster[i]=(uint8_t)protocol.payload[i+1];
          char tmp[4];
          snprintf(tmp,3,"%02X",protocol.payload[i+1]);

        }
        
        for (i=0;i<2;i++)
        {
          uint8_t ShortAddr[4];
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
        
        
        uint8_t Cluster[2];
        for (i=0;i<2;i++)
        {
          Cluster[i]=(uint8_t)protocol.payload[i+3];
        }

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

        SetInfoStatus(inifile,String(tmpStatus));

        uint16_t clusterId = (Cluster[0] << 8) | Cluster[1];
        
        if (clusterId == 0xE004 || clusterId == 0xED00)
        {
          // Clusters IR Zosung - traiter comme commande cluster-specific
          log_i("IR Cluster 0x%04X - Command 0x%02X from %s", clusterId, Command, inifile.c_str());
          
          // Extraire les données de la commande
          // Format RAW: ... [15]=Command, [16...]=payload de la commande
          int dataOffset = 16;
          int dataLen = protocol.ln - dataOffset;
          
          if (dataLen > 0 && dataLen < 200)
          {
            // Mettre à jour lastSeen
            SetInfoLastseen(inifile, FormattedDate);
            
            // Appeler le gestionnaire IR
            zosungIRManage(inifile, clusterId, Command, 
                          (uint8_t*)&protocol.payload[dataOffset], dataLen);
          }
          else
          {
            log_e("IR command with invalid data length: %d", dataLen);
          }
        }else if (clusterId == 0xEF00)
        {
            log_i("Tuya cluster 0xEF00 - Command 0x%02X from %s", Command, inifile.c_str());
            
            if (Command == 0x24)
            {
                // Time Sync Request - Répondre avec l'heure actuelle
                log_i("Tuya Time Sync Request from %04X", SA);
                sendTuyaTimeSync(SA, 1);
            }
            else if (Command == 0x01 || Command == 0x02)
            {
                // Datapoint Report - Extraire les datapoints
                int dataOffset = 16;
                int dataLen = protocol.ln - dataOffset;
                
                if (dataLen > 0)
                {
                    SetInfoLastseen(inifile, FormattedDate);
                    tuyaThermostatManage(inifile, 0, 0, dataLen, 
                                        (char*)&protocol.payload[dataOffset]);
                }
            }
            else if (Command == 0x00)
            {
                log_d("Tuya Datapoint Set Response from %04X", SA);
            }
            else
            {
                log_w("Tuya unknown command 0x%02X from %04X", Command, SA);
            }
        }

        
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
        }else{
          log_e("Cluster-specific command: cluster=0x%04X, cmd=0x%02X", clusterId, Command);
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
              
        //uint8_t endpoint = (uint8_t)protocol.payload[3];
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
           
          SetInfoLastseen(inifile,FormattedDate);

          SetInfoLQI(inifile,String(lqi));

          SetInfoStatus(inifile,String("00"));

           //Traitement données
          readZigbeeDatas(inifile,Cluster,Attribute,DataType,ln,&protocol.payload[12]);
          
        }

        //traitement bind lors de la réception du model
        int tmpcluster = Cluster[0]*256+Cluster[1];
        int tmpattribute = Attribute[0]*256+Attribute[1];
        if ((tmpcluster == 0) && (tmpattribute == 5))
        {  
          String tmpMac;
          String model, endpoint;
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
          endpoint = GetEndpoint(inifile);  

          //Traitement spécifique seloin modèle
          SpecificTreatment(ShortAddr,endpoint.toInt(), model);
          log_d("SpecificTreatment");
         
          vTaskDelay(100);
          alertList->push(Alert{"Bind waiting...", 0});
          getBind(macInt,DeviceId,model);
          log_d("getBind");
          vTaskDelay(100);
          // Traitement config report
          alertList->push(Alert{"Config Report waiting...", 0});
          getConfigReport(ShortAddr,DeviceId,model);
          log_d("getConfigReport");
          vTaskDelay(100);
          // Traitement polling
          getPollingDevice(ShortAddr,DeviceId,model);
          log_d("getPollingDevice");
          vTaskDelay(100);
          //get software version
          SendAttributeRead(SA,endpoint.toInt(),0,16384);
          alertList->push(Alert{"Config OK", 0});
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
        //alertList->push(Alert{"Device Joined : "+String(tmp), 1});
        
        log_d("Node Joined : %02X%02X ",protocol.payload[0],protocol.payload[1]);

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

        String alertMsg = "<div align='center'><strong>"+String(tmp)+"</strong><br>(<span id='newDevice'>"+adMac+"</span>)</div>";
        alertList->push(Alert{alertMsg, 3});
        String path = adMac+".json";
        
        WriteIni ini ;
        ini.i[0].section ="INFO";
        ini.i[0].key = "shortAddr";
        ini.i[0].value = String(ShortAddr);

        ini.i[1].section ="INFO";
        ini.i[1].key = "LQI";
        ini.i[1].value = String(lqi);

        ini.iniPacketSize = 2;

        ini_writes(path, ini, true);

        if (deviceExist(adMac))
        {
          for (size_t i = 0; i < devices.size(); i++) 
          {
            DeviceData* device = devices[i];
            if (device->getDeviceID() == adMac)
            {
               device->setInfoShortAddr(String(ShortAddr));
               device->setInfoLQI(String(lqi));
               break;
            }
          }
        }else{
          void* mem = ps_malloc(sizeof(DeviceData));
          if (!mem) {
            log_e("Erreur ps_malloc pour %s", path.c_str());
          }
          DeviceData* dev = new (mem) DeviceData("/db/" + path, adMac);
          if (dev->loadFromFile()) {
            devices.push_back(dev);
          }
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

  //Serial1.flush();
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
  //Serial1.flush();
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
/*void protocolDatas(uint8_t sp[4092], size_t len)
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
    // PROTECTION CRITIQUE
    if (i >= (sizeof(packet) - 1)) {
        log_e("Packet buffer overflow, resetting");
        i = 0;
        stx = false;
        continue;
    }
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
        if (type == 0x8501)
        {
          datasManage((char *)sp.raw,sp.len);
        }
        else if ((type == 0x4d) || (type == 0x8043) || (type == 0x8045)|| (type == 0x8503))
        {
          if (!PriorityQueuePacket->isFull())
          {
            xSemaphoreTake(QueuePrio_Mutex, portMAX_DELAY);
            PriorityQueuePacket->push(sp);
            xSemaphoreGive(QueuePrio_Mutex);
            log_w("ProtocolDatas - uxTaskGetStackHighWaterMark(NULL) : %d",uxTaskGetStackHighWaterMark(NULL));
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
            log_w("ProtocolDatas - uxTaskGetStackHighWaterMark(NULL) : %d",uxTaskGetStackHighWaterMark(NULL));
          }else{
            addDebugLog("QueuePacket FULL !");
            while (!QueuePacket->isEmpty())
            {
              //DEBUG_PRINTLN("Packet shift : protocol datas");
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
}*/

bool ScanDeviceToPoll() {
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];

    auto &pollList = device->getPollList();
    int shortAddr;
    int cluster;
    int attribut;
    int last;

    for (size_t j = 0; j < pollList.size(); j++) 
    {
      auto &p = pollList[j];

      shortAddr = device->getInfo().shortAddr.toInt();    
      cluster = p.cluster.toInt();
      attribut = p.attribut;
      last = p.last;
      p.last = last - 1;

      if (last <= 0) 
      {
        // Lancement de la requête
        log_d("Lancement de la requête / sht: %d - cluster: %d - attr: %d", shortAddr, cluster, attribut);
        // Envoi du paquet
        SendAttributeRead(shortAddr,device->getInfo().endpoint.toInt(), cluster, attribut);
        p.last = p.poll;
      }
    }
  }
  return true;
}


