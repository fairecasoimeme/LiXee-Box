#pragma once

#ifndef CONFIG_H_
#define CONFIG_H_

#include <Arduino.h>
//#define CIRCULAR_BUFFER_INT_SAFE
#include <CircularBuffer.h>

#define VERSION "v1.0a"

// hardware config64
#define RESET_ZIGATE 19//4
#define FLASH_ZIGATE 40//33
#define LED1 40
#define PRODUCTION 1
#define FLASH 0

#define VOLTAGE 4

#define RXD2 17//17//17 
#define TXD2 18//18//16 




#define MAXHEAP 1000000//ESP.getMaxAllocHeap() //(ESP.getFreeHeap() / 2) //96000
extern String FormattedDate;

// ma structure configCRC error
struct ConfigSettingsStruct {
  bool enableWiFi;
  bool enableDHCP;
  bool connectedWifiSta;
  int channelWifi;
  int RSSIWifi;
  char ssid[50];
  String bssid;
  char password[50];
  char ipAddressWiFi[18];
  char ipMaskWiFi[16];
  char ipGWWiFi[18];
  
  
  bool enableEthernet;
  bool connectedEther; 
  bool dhcp;
  char ipAddress[18];
  char ipMask[16];
  char ipGW[18];
  int serialSpeed;
  char radioType[20];
  char dataFlow[20];
  int  tcpListenPort;
  bool disableWeb;
  bool enableHeartBeat;
  double refreshLogs;
  bool enableDebug;
  bool enableNotif;
  bool enableWebPush;
  bool enableModbus;
  bool enableSecureHttp;
  bool enableMqtt;
  bool enableHistory;
  
};

struct ZigbeeConfig {
  char application[9];
  char type;
  char sdk;
  int channel;
  uint8_t network;
  uint64_t zigbeeMac;
};

struct ZiGateInfosStruct {
  char device[8];
  char mac[8];
  char flash[8];  
};

struct ConfigGeneralStruct {
  int firstStart;
  char ZLinky[20];
  char Gaz[20];
  char Water[20];
  int LinkyMode;
  int powerMaxDatas;
  char ntpserver[50];
  int timeoffset;
  long epochTime;
  char timezone[50];
  char tarifIdx1[10];
  char tarifIdx2[10];
  char tarifIdx3[10];
  char tarifIdx4[10];
  char tarifIdx5[10];
  char tarifIdx6[10];
  char tarifIdx7[10];
  char tarifIdx8[10];
  char tarifIdx9[10];
  char tarifIdx10[10];
  char tarifAbo[10];
  char tarifCSPE[10];
  char tarifCTA[10];
  char servSMTP[50];
  char portSMTP[50];
  char userSMTP[50];
  char passSMTP[50];
  char servMQTT[50];
  char portMQTT[50];
  char userMQTT[50];
  char passMQTT[128];
  char headerMQTT[128];
  char userHTTP[50];
  char passHTTP[50];
  char servWebPush[50];
  char userWebPush[50];
  char passWebPush[50];
  bool webPushAuth;
  float coeffGaz;
  float coeffWater;
  char unitGaz[3];
  char unitWater[3];
  char tarifGaz[10];
  char tarifWater[10];
};


typedef struct {
    unsigned int cmd;
    unsigned int len;
    uint8_t datas[512];
} Packet;

typedef struct {
    unsigned int len;
    uint8_t raw[256];
} SerialPacket;


typedef struct {
    char name[50];
    unsigned int cluster;
    unsigned int attribute;
    char mode[20];
    char mqtt_device_class[20];
    char mqtt_state_class[20];
    char mqtt_icon[20];
    char type[20];
    float coefficient;
    char unit[20];
    bool visible;
    char typeJauge[20];
    int jaugeMin;
    int jaugeMax;
    
} State;

typedef struct {
    char name[50];
    unsigned int command;
    unsigned int value;
    bool visible;
} Action;

typedef struct {
  int StateSize;
  int ActionSize;
  State e[100];
  Action a[10];
}Template;

typedef struct {
  String message;
  int state;
} Alert;

typedef CircularBuffer<char, 4096> LogConsoleType;

#define DEBUG_ON

#ifdef DEBUG_ON
#define DEBUG_PRINT(x) Serial.print(x); 
#define DEBUG_PRINTLN(x) Serial.println(x);
#else
#define DEBUG_PRINT(x) 
#define DEBUG_PRINTLN(x) 
#endif
#endif

String encodeBase64(const String &input);


