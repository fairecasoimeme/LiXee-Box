#pragma once

#ifndef CONFIG_H_
#define CONFIG_H_

#include <Arduino.h>
//#define CIRCULAR_BUFFER_INT_SAFE
#include <CircularBuffer.hpp>
#include <ArduinoJson.h>
#include <malloc.h>

#define VERSION "v2.19"

// hardware config64
#define RESET_ZIGATE 40//4
#define FLASH_ZIGATE 39//33
#define LED_PIN 3
#define PRODUCTION 1
#define FLASH 0

#define VOLTAGE 4

#define RXD2 17//17//17 
#define TXD2 18//18//16 

#define MAXHEAP 1000000//ESP.getMaxAllocHeap() //(ESP.getFreeHeap() / 2) //96000
extern String FormattedDate;


#define MAX_SUBMETERS 10

struct SubMeterConfig {
    char IEEE[20];          // Adresse IEEE du device (ex: "00158D0001234567")
    char alias[32];         // Nom affiché (ex: "Salon", "Cuisine")
    char color[10];         // Couleur hex pour le donut (ex: "#e74c3c")
    bool enabled;           // Activé/désactivé
};

// ma structure configCRC error
struct ConfigSettingsStruct {
  bool enableWiFi;
  bool enableDHCP;
  bool connectedWifiSta;
  int channelWifi;
  int RSSIWifi;
  char ssid[50];
  String bssid;
  char password[150];
  char ipAddressWiFi[18];
  char ipMaskWiFi[16];
  char ipGWWiFi[18];
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
  bool enableSecureHttp;
  bool enableMqtt;
  bool enableUDP;
  bool enableMarstek;
  bool enableHistory;
  bool enableHWFlow;
  
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

struct ConfigNotification{
  bool PriceChange;
  bool PEJP;
  bool ColorTomorrow;
  bool RedColor;
  bool OverVoltage;
  int  OverVoltageThreshold;
  bool UnderVoltage;
  int  UnderVoltageThreshold;
  bool OverBudget;
  int  OverBudgetThreshold;
  bool SubscribedPower;
  bool PowerOutage;
  bool ProdSupConso;
  bool ProdZero;

  // Alertes avancées
  bool OverPower;
  int  OverPowerThreshold;       // Watts
  int  OverPowerDuration;        // Secondes avant alerte
  int  OverPowerCooldown;        // Minutes entre alertes

  bool Freeze;
  int  FreezeThreshold;          // Centièmes °C (300 = 3.00°C)
  char FreezeSensorIEEE[20];
  int  FreezeCooldown;           // Minutes entre alertes

  bool DailyAnomaly;
  int  DailyAnomalyPercent;      // % au-dessus moyenne 7j

  bool WaterLeak;
  int  WaterLeakThreshold;       // Litres
  bool NightWaterLeak;
  int  NightWaterLeakThreshold;  // Litres (conso nocturne 00h-05h)

  bool DailyMetrics;
};

struct ConfigGeneralStruct {
  int firstStart;
  char ZLinky[20];
  char Production[20];
  char Gaz[20];
  char Water[20];
  int LinkyMode;
  int HouseSurface;
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
  char tarifAboProd[10];
  char tarifIdxProd[10];
  char servSMTP[50];
  char portSMTP[50];
  char userSMTP[50];
  char passSMTP[50];
  char servMQTT[50];
  char portMQTT[50];
  char clientIDMQTT[50];
  char userMQTT[50];
  char passMQTT[128];
  char headerMQTT[128];
  bool HAMQTT;
  bool TBMQTT;
  bool customMQTT;
  String customMQTTJson;
  char userHTTP[50];
  char passHTTP[50];
  char servWebPush[50];
  char userWebPush[50];
  char passWebPush[50];
  bool webPushAuth;
  bool connectedMarstek;
  char marstekIP[18];
  char servUDP[50];
  char portUDP[50];
  String customUDPJson;
  float coeffGaz;
  float coeffWater;
  float coeffProduction;
  char unitGaz[3];
  char unitWater[3];
  char tarifGaz[10];
  char tarifWater[10];
  int scanNumber;
  bool developerMode;
  SubMeterConfig subMeters[MAX_SUBMETERS];
  int subMeterCount;  
  char Presence[20] = "";          // IEEE du capteur de présence
  bool enablePresenceGraph = true; // Afficher sur le graphique

  // Tunnel (reverse proxy)
  bool enableTunnel = false;
  char tunnelToken[128] = "";
  char tunnelClientId[64] = "";
};

struct SerialBuffer {
    uint8_t data[512];
    size_t length;
};

typedef struct __attribute__((packed)) {
    uint16_t cmd;           // uint16_t au lieu de unsigned int (32 bits -> 16 bits)
    uint16_t len;           // uint16_t au lieu de unsigned int
    uint8_t datas[512];
} Packet;

typedef struct __attribute__((packed)) {
    uint16_t len;           // uint16_t au lieu de unsigned int
    uint8_t raw[256];
} SerialPacket;

typedef struct {
  String section;
  String key;
  String value;
} iniPacket;

typedef struct {
  int iniPacketSize;
  iniPacket i[10];
}WriteIni;


/*typedef struct {
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
    unsigned int endpoint;
    unsigned int value;
    bool visible;
} Action;

typedef struct {
  int StateSize;
  int ActionSize;
  State e[100];
  Action a[10];
}Template;*/

typedef struct {
  String message;
  int state;
} Alert;

typedef struct {
  int shortAddr;
  int cluster;
  int attribute;
  String value;
} Device;

typedef struct {
  String title;
  String message;
  String timeStamp;
  int type;
  int viewed;
} Notification;

typedef struct{
  String manufacturer;
  String model;
  int shortAddr;
  int deviceId;
  String sotfwareVersion;
  String lastSeen;
  String LQI;

} DeviceInfo;

struct StatusRegisterBreakout {
  String contact_sec;
  String organe_coupure;
  String cache_borne_dist;
  uint8_t surtension_phase;
  uint8_t depassement_ref_pow;
  String producteur;
  String sens_energie_active;
  String tarif_four;
  String tarif_dist;
  String horloge;
  String type_tic;
  String comm_euridis;
  String etat_cpl;
  String sync_cpl;
  String tempo_jour;
  String tempo_demain;
  String preavis_pointe_mobile;
  String pointe_mobile;
};

StatusRegisterBreakout parseStatusRegister(const String& hexVal);

//typedef CircularBuffer<char, 4096> LogConsoleType;

struct SpiRamAllocator {
  void* allocate(size_t size) {
    return ps_malloc(size);  // Allouer dans la PSRAM
  }

  void deallocate(void* pointer) {
    free(pointer);  // Libérer l'allocation
  }

  void* reallocate(void* pointer, size_t new_size) {
    // heap_caps_realloc connaît la taille de l'ancien bloc en interne
    void* new_ptr = heap_caps_realloc(pointer, new_size, MALLOC_CAP_SPIRAM);
    if (!new_ptr) new_ptr = realloc(pointer, new_size);
    return new_ptr;
  }
};

struct UpdateStatus {
      String statusManuel = "";
      String statusAuto = "";
      int progressAuto = -1;  // -1 = pas de MAJ, 0-100 = progression
      int progressManuel = -1;
      bool rebootRequested = false;
    };

// Définir un type de document JSON qui utilise la PSRAM
using SpiRamJsonDocument = BasicJsonDocument<SpiRamAllocator>;

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

// ===== CLASSE STRING PSRAM SIMPLE =====
class PSRAMString {
private:
    char* buffer;
    size_t capacity;
    size_t len;
    
public:
    PSRAMString(size_t size = 100000) {
        capacity = size;
        buffer = (char*)ps_malloc(capacity);
        if (!buffer) {
            Serial.println("Erreur allocation PSRAM, utilisation heap");
            buffer = (char*)malloc(capacity);
        }
        clear();
    }
    
    ~PSRAMString() {
        if (buffer) free(buffer);
    }
    
    // Opérateur += comme String normale
    PSRAMString& operator+=(const String& str) {
        append(str.c_str());
        return *this;
    }
    
    PSRAMString& operator+=(const char* str) {
        append(str);
        return *this;
    }
    
    PSRAMString& operator+=(const __FlashStringHelper* str) {
        append(reinterpret_cast<const char*>(str));
        return *this;
    }
    
    // Opérateur = pour affecter
    PSRAMString& operator=(const String& str) {
        clear();
        append(str.c_str());
        return *this;
    }
    
    PSRAMString& operator=(const __FlashStringHelper* str) {
        clear();
        append(reinterpret_cast<const char*>(str));
        return *this;
    }
    
    // Fonction replace native — travaille directement sur le buffer PSRAM
    // sans copie intermédiaire vers un String heap
    void replace(const String& find, const String& replaceWith) {
        const char* f = find.c_str();
        const char* r = replaceWith.c_str();
        size_t fLen = find.length();
        size_t rLen = replaceWith.length();
        if (fLen == 0) return;

        // Comptage des occurrences pour calculer la taille finale
        size_t count = 0;
        const char* pos = buffer;
        while ((pos = strstr(pos, f)) != nullptr) { count++; pos += fLen; }
        if (count == 0) return;

        // Calcul de la nouvelle taille (cast signé pour éviter underflow si rLen < fLen)
        size_t newLen = (size_t)((ssize_t)len + (ssize_t)count * ((ssize_t)rLen - (ssize_t)fLen));

        // Réécriture dans un buffer temporaire PSRAM de la taille exacte
        char* tmp = (char*)ps_malloc(newLen + 1);
        if (!tmp) tmp = (char*)malloc(newLen + 1);
        if (!tmp) return;

        char* dst = tmp;
        const char* src = buffer;
        const char* found;
        while ((found = strstr(src, f)) != nullptr) {
            size_t chunk = found - src;
            memcpy(dst, src, chunk);
            dst += chunk;
            memcpy(dst, r, rLen);
            dst += rLen;
            src = found + fLen;
        }
        // Copier le reste
        size_t tail = len - (src - buffer);
        memcpy(dst, src, tail);
        dst[tail] = '\0';

        // Si le résultat est plus grand que le buffer actuel, on réalloue en PSRAM
        if (newLen + 1 > capacity) {
            size_t newCap = (newLen + 1) * 2;
            char* newBuf = (char*)ps_malloc(newCap);
            if (!newBuf) newBuf = (char*)malloc(newCap);
            if (!newBuf) { free(tmp); return; }
            free(buffer);
            buffer = newBuf;
            capacity = newCap;
        }

        memcpy(buffer, tmp, newLen + 1);
        len = newLen;
        free(tmp);
    }
    
    // Fonctions utiles
    const char* c_str() const { return buffer; }
    size_t length() const { return len; }
    void clear() { buffer[0] = '\0'; len = 0; }
    
private:
    void append(const char* str) {
        if (!str) return;
        size_t strLen = strlen(str);
        if (len + strLen >= capacity) {
            size_t newCap = (len + strLen + 1) * 2;
            char* newBuf = (char*)ps_malloc(newCap);
            if (!newBuf) newBuf = (char*)malloc(newCap);
            if (!newBuf) return;
            memcpy(newBuf, buffer, len);
            free(buffer);
            buffer = newBuf;
            capacity = newCap;
        }
        strcpy(buffer + len, str);
        len += strLen;
    }
};

