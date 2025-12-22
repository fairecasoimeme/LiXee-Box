#define BONJOUR_SUPPORT
#include <Arduino.h>
//#include "soc/rtc_wdt.h"
#include "esp32s3/rom/rtc.h"
#include "driver/temp_sensor.h"
#include <esp_task_wdt.h>
#include "WiFiProv.h"
#include <WiFi.h>
#ifdef BONJOUR_SUPPORT
  #include <ESPmDNS.h>
#endif
extern "C" {
	#include "freertos/FreeRTOS.h"
	#include "freertos/timers.h"
}
#include <AsyncMqttClient.h>
#include "tuya.h"
//#include <NTPClient_Generic.h>
#include <time.h>
#include "NTPClient.h"
#include <WiFiUdp.h>

#include <WiFiClient.h>
#include <esp_wifi.h>
#define ARDUINOJSON_USE_LONG_LONG 1
#define ARDUINOJSON_SLOT_ID_SIZE 2
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "SPIFFS_ini.h"
#include "config.h"
#include "web.h"
#include "log.h"
#include "flash.h"
#include "protocol.h"
#include "zigbee.h"
#include <driver/uart.h>
#include <lwip/ip_addr.h>
#include <esp_log.h>
#include "mail.h"
#include "AsyncUDP.h"

#include "rules.h"

#include <esp_heap_caps.h>
#include "device.h"
#include "powerHistory.h"
#include "energyHistory.h"

#include "smart_wifi_manager.h"
#include "mqtt.h"
#include "notificationManager.h"

#include "ConfigResetManager.h"
#define RESET_WINDOW_MS     3000    // Fenêtre de détection
#define RESET_BOOT_COUNT    3       // Nombre de reboots pour RAZ
ConfigResetManager resetManager(RESET_WINDOW_MS, RESET_BOOT_COUNT);

SmartWiFiManager smartWiFi;

// Gestionnaire de notifications
NotificationManager notificationManager;
bool oldProdZero = false;

bool wifiServicesStarted = false;
bool zigbeeInitialized = false;

std::vector<DeviceData*> devices;

bool executeReboot=false;
bool updatePending = false;

#include <TaskScheduler.h>


/*// ========== WEBSOCKET TUNNEL ==============
#include "WebSocketTunnel.h"
// Instance globale
WebSocketTunnel wsTunnel;

// ============================================
// CONFIGURATION OPTIMISÉE PSRAM
// ============================================
WebSocketTunnelConfig createOptimizedConfig() {
    WebSocketTunnelConfig config;
    
    // Paramètres tunnel
    config.deviceToken = "votre_token_ici";
    config.tunnelHost = "lixee-box.fr";
    config.tunnelPort = 80;
    config.tunnelPath = "/device";
    config.deviceName = "ESP32-LIXEEBOX";
    
    // Authentification HTTP locale
    config.localHttpUser = "-------";
    config.localHttpPass = "-------";
    
    // === OPTIMISATIONS PSRAM ===
    config.usePSRAM = true;                    // ACTIVER PSRAM
    config.jsonBufferSize = 24 * 1024;         // 24KB pour JSON (ajustez selon besoins)
    config.httpBufferSize = 48 * 1024;         // 48KB pour HTTP (réponses volumineuses)
    
    // Monitoring
    config.enableMemoryMonitoring = true;      // Surveiller la mémoire
    config.memoryMonitorInterval = 30000;      // Stats toutes les 30s
    config.debugEnabled = true;                // Logs détaillés
    
    // Timing
    config.reconnectInterval = 5000;
    config.heartbeatInterval = 30000;
    
    return config;
}

// ========== OPENAI CONFIGURATION ==========
#include "OpenAIAnalyzer.h"
// OpenAI
const char* OPENAI_API_KEY = "-------------------";

// Timing
#define ANALYSIS_INTERVAL_MS (24 * 60 * 60 * 1000)  // 1 fois par jour
unsigned long last_analysis = 0;

// Instance globale
OpenAIAnalyzer ai_analyzer;

LinkyData collectLinkyData() {
    LinkyData data;
    
    // Exemple avec vos données du document
    data.index_hc = 17893668;           // Wh
    data.index_hp = 33153177;           // Wh
    data.puissance_max_p1 = 8070;       // W
    data.puissance_max_p2 = 0;          // Monophasé
    data.puissance_max_p3 = 0;
    data.puissance_souscrite = 9;       // kVA
    data.nb_jours = 120;
    
    strcpy(data.horaires_hc, "00h00-08h00");
    strcpy(data.type_abonnement, "HCHP");
    
    // Calcul des statistiques
    LinkyUtils::calculateStats(data);
    
    return data;
}

void performDetailedAnalysis() {
    Serial.println("\n╔══════════════════════════════════════════╗");
    Serial.println("║   ANALYSE DÉTAILLÉE AVEC JSON COMPLET    ║");
    Serial.println("╚══════════════════════════════════════════╝\n");
    
    // 1. Préparer le résumé
    LinkyData summary;
    summary.index_hc = 17893668;
    summary.index_hp = 33153177;
    summary.puissance_max_totale = 2700;
    summary.puissance_souscrite = 6;
    summary.nb_jours = 365;
    strcpy(summary.type_abonnement, "HCHP");
    strcpy(summary.horaires_hc, "22h30-06h30");
    LinkyUtils::calculateStats(summary);
    
    // 2. Vos JSON complets (stockés en PSRAM ou SPIFFS)
    // Option A: En dur (si petit)
    //const char* json_hours = R"({"hours":{"trend":{"256":0,"258":0}...}})";
    //const char* json_minutes = R"({"datas":[{"y":"13:46","519":0...}]})";
    String r;
    r.reserve(MAXHEAP);
    String filename = "/hst/pwr_00158d0006204fcf.json";
    File file = LittleFS.open(filename, "r");
    if (!file || file.isDirectory())
    {
      file.close();
      return;
    }
    size_t fileSize = file.size();
    // Allocation d'un buffer dans la PSRAM avec heap_caps_malloc 
    char* buffer = (char*) heap_caps_malloc(fileSize + 1, MALLOC_CAP_SPIRAM); 
    if (!buffer) { 
      Serial.println("Erreur d'allocation dans la PSRAM"); 
      file.close(); 
    }

    size_t readBytes = file.readBytes(buffer, fileSize); 
    buffer[readBytes] = '\0';
    file.close();

    r = String(buffer,readBytes);
    heap_caps_free(buffer); 

    const char* json_minutes = r.c_str();
    const char* json_hours = r.c_str();
    // Option B: Lus depuis SPIFFS/SD (recommandé)
    // String json_hours = readFile("/linky_hours.json");
    // String json_minutes = readFile("/linky_minutes.json");
    
    // 3. Configuration analyse détaillée
    FullAnalysisConfig full_config;
    full_config.include_hourly_data = true;
    full_config.include_daily_data = true;
    full_config.include_monthly_data = true;
    full_config.use_gpt4 = true;  // true pour GPT-4 (meilleur mais +cher)
    full_config.max_tokens = 1500;  // Plus de détails
    
    // 4. Analyse
    AIAnalysisResult result;
    bool success = ai_analyzer.analyzeWithFullJSON(
        json_hours,
        json_minutes,
        summary,
        result,
        full_config
    );
    
    if (success) {
        Serial.println("\n╔═══════════════════════════════════════════════╗");
        Serial.println("║       ANALYSE DÉTAILLÉE - RÉSULTAT            ║");
        Serial.println("╠═══════════════════════════════════════════════╣");
        Serial.println(result.response_text);
        Serial.println("╠═══════════════════════════════════════════════╣");
        Serial.printf("║ Tokens: %u | Coût: $%.6f USD            ║\n",
            result.total_tokens, result.estimated_cost_usd);
        Serial.println("╚═══════════════════════════════════════════════╝\n");
        
        result.free();
    }
}
*/

// application config
unsigned long timeLog;
ConfigSettingsStruct ConfigSettings;
ConfigGeneralStruct ConfigGeneral;
ConfigNotification ConfigNotif;
ZigbeeConfig ZConfig;
ZiGateInfosStruct ZiGateInfos;

RulesManager rulesManager;

CircularBuffer<Packet, 100> *commandList = nullptr;
CircularBuffer<Packet, 70> *PrioritycommandList = nullptr;
CircularBuffer<SerialPacket, 30> *PriorityQueuePacket = nullptr;
CircularBuffer<Alert, 10> *alertList = nullptr;
CircularBuffer<Device, 50> *deviceList = nullptr;
CircularBuffer<Notification, 10> *notifList = nullptr;
CircularBuffer<SerialPacket, 300> *QueuePacket = nullptr;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

//OTA
uint8_t* au8OTAFile = nullptr;
byte u8OTAWaitForDataParamsPending = 0;
uint16_t u16OTAWaitForDataParamsTargetAddr;
byte u8OTAWaitForDataParamsSrcEndPoint;
uint32_t u32OTAWaitForDataParamsCurrentTime;
uint32_t u32OTAWaitForDataParamsRequestTime;
uint16_t u16OTAWaitForDataParamsBlockDelay;

unsigned long lastConnectionTest = 0;
const unsigned long CONNECTION_TEST_INTERVAL = 20000; // 20 secondes
bool connectionTestPending = false;
uint16_t lastTestPacketId = 0;
bool reallyConnected = false;

#define FORMAT_LittleFS_IF_FAILED true
//#define CONFIG_LITTLEFS_CACHE_SIZE 512

bool configOK=false;
String modeWiFi="STA";

extern int ZiGateMode;
extern int TimedFiFo;
extern String section[12];

#define BAUD_RATE 115200
#define TCP_LISTEN_PORT 9999

// if the bonjour support is turned on, then use the following as the name
#define DEVICE_NAME ""

// serial end ethernet buffer size
#define BUFFER_SIZE 512  // Buffer plus large pour flux importants

#define WL_MAC_ADDR_LENGTH 6

#ifdef BONJOUR_SUPPORT
// multicast DNS responder
MDNSResponder mdns;
#endif

unsigned long previousTime = 0; // Variable pour stocker le temps précédent
unsigned long interval = 60000; // Intervalle de 1 minute en millisecondes

//UDP Async
AsyncUDP UdpServer;
AsyncServer *TcpServer;
AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
TimerHandle_t WifiReconnectTimer;

#define NTP_UPDATE_INTERVAL_MS          60000L
String FormattedDate;
String Hour;
String Day;
String Month;
String Year;
String Minute;
String Yesterday;
String epochTime;
String firstStart;

//#define CONFIG_ASYNC_TCP_RUNNING_CORE 1 //any available core
/*#define CONFIG_ASYNC_TCP_USE_WDT 1*/

SET_LOOP_TASK_STACK_SIZE(16*1024); // 24KB


//callback timer
void scanCallback();
void delayRebootCallBack();

void sendPacket();

Task scan(60000, TASK_FOREVER, &scanCallback);
Task delayReboot(1000, TASK_ONCE, &delayRebootCallBack);
Scheduler runner;

void initCircularBuffer()
{

  commandList = (CircularBuffer<Packet,100>*)heap_caps_malloc(sizeof(*commandList), MALLOC_CAP_SPIRAM);
  new(commandList) CircularBuffer<Packet,100>();
  PrioritycommandList = (CircularBuffer<Packet,70>*)heap_caps_malloc(sizeof(*PrioritycommandList), MALLOC_CAP_SPIRAM);
  new(PrioritycommandList) CircularBuffer<Packet,70>();
  PriorityQueuePacket = (CircularBuffer<SerialPacket,30>*)heap_caps_malloc(sizeof(*PriorityQueuePacket), MALLOC_CAP_SPIRAM);
  new(PriorityQueuePacket) CircularBuffer<SerialPacket,30>();
  alertList = (CircularBuffer<Alert,10>*)heap_caps_malloc(sizeof(*alertList), MALLOC_CAP_SPIRAM);
  new(alertList) CircularBuffer<Alert,10>();
  notifList = (CircularBuffer<Notification,10>*)heap_caps_malloc(sizeof(*notifList), MALLOC_CAP_SPIRAM);
  new(notifList) CircularBuffer<Notification,10>();
  deviceList = (CircularBuffer<Device,50>*)heap_caps_malloc(sizeof(*deviceList), MALLOC_CAP_SPIRAM);
  new(deviceList) CircularBuffer<Device,50>();
  QueuePacket = (CircularBuffer<SerialPacket,300>*)heap_caps_malloc(sizeof(*QueuePacket), MALLOC_CAP_SPIRAM);
  new(QueuePacket) CircularBuffer<SerialPacket,300>();

}

void delayRebootCallBack()
{
  DEBUG_PRINTLN("reboot...");
  ESP.restart();
}

bool ScanDevicesToRAZ() {
  // Récupérer l’heure courante
  time_t now = time(nullptr);
  struct tm nowTm;
  localtime_r(&now, &nowTm);

  // Parcours de tous les devices
  for (DeviceData* device : devices) {
      String model = device->getInfo().model;
      
      // On ne gère que ZLinky_TIC et ZiPulses
      if (model != "ZLinky_TIC" && model != "ZiPulses") 
          continue;

      //POWER (RAZ de la minute suivante)
      String currentTime = Hour + ":" + String(Minute.toInt()+1);
      resetMeasurements(device->powerHistory, currentTime);

      // RAZ périodiques
      if (Minute == "00") 
      {

        //ENERGY
        for (const auto &graphEntry : device->energyHistory.hours.graph) {
          const PsString &Key = graphEntry.first;
          const ValueMap &valMap   = graphEntry.second;
          if (strcmp(Hour.c_str(),Key.c_str())==0)
          {
            for (const auto &attrPair : valMap.attributes) {
              int attrId   = attrPair.first;
              device->energyHistory.hours.graph[PsString(Hour.c_str())].attributes[attrId] = 0;
              device->energyHistory.hours.trend.attributes[attrId] = 0;
              device->energyHistory.hours.data[PsString(Hour.c_str())].attributes[attrId] = device->energyHistory.hours.last.attributes[attrId];
            }
          }
        }
      }

      if ((Hour == "00") && (Minute == "00")) {
        //Notification Journalière
        if (ConfigNotif.OverBudget)
        {
          int arrayLength = sizeof(section) / sizeof(section[0]);
          long TotalWh =0;
          for (auto &kv : device->energyHistory.hours.graph) {
            ValueMap &vm = kv.second;   
            for (size_t i = 2; i < arrayLength; ++i) {
              int attrId = section[i].toInt();
              auto itv = vm.attributes.find(attrId);
              if (itv != vm.attributes.end()) {       
                TotalWh += itv->second;
              }
            }
          }     
          if (ConfigNotif.OverBudgetThreshold)
          {
            int budgetkWh = getkWhBudget(String(ConfigGeneral.ZLinky), "day", ConfigNotif.OverBudgetThreshold);
            if ( TotalWh > budgetkWh)
            {
              String text = "Attention la consommation de ce jour dépasse votre budget prévu - consommé :"+String(TotalWh)+"Wh budget : "+String(budgetkWh)+ "Wh";
              if (!notifList->isFull()) {
                  notifList->push(Notification{"💸🚨Dépassement de budget",text , FormattedDate, 3, 0});
              } else {
                  notifList->shift();
                  notifList->push(Notification{"💸🚨Dépassement de budget", text, FormattedDate, 3, 0});
              }
              notificationManager.addNotification("💸🚨Dépassement de budget", text, 0);
            }
          }
        }

        for (const auto &graphEntry : device->energyHistory.days.graph) {
          const PsString &Key = graphEntry.first;
          const ValueMap &valMap   = graphEntry.second;
          if (strcmp(Day.c_str(),Key.c_str())==0)
          {
            for (const auto &attrPair : valMap.attributes) {
              int attrId   = attrPair.first;
              device->energyHistory.days.graph[PsString(Day.c_str())].attributes[attrId] = 0;
            }
          }
        }
      }

      if ((Day == "01") && (Hour == "00") && (Minute == "00")) {
        for (const auto &graphEntry : device->energyHistory.months.graph) {
          const PsString &Key = graphEntry.first;
          const ValueMap &valMap   = graphEntry.second;
          if (strcmp(Month.c_str(),Key.c_str())==0)
          {
            for (const auto &attrPair : valMap.attributes) {
              int attrId   = attrPair.first;
              device->energyHistory.months.graph[PsString(Month.c_str())].attributes[attrId] = 0;
            }
          }
        }
      }

      // Si lastSeen est trop vieux (>3600s), on remet à zéro le compteur courant
      time_t lastSeen = device->getLastSeenEpoch();

      if ((now - lastSeen) > 3600) {
        
        String textError = "device : "+device->getDeviceID()+" - Pas vu depuis plus de 1 heure";
        addDebugLog(textError);

        device->setValue(std::string("0B04"),std::string("1295"),std::string("0"));
        device->setValue(std::string("0B04"),std::string("2319"),std::string("0"));
        device->setValue(std::string("0B04"),std::string("2575"),std::string("0"));

        addMeasurement(device->powerHistory,1,0);
        addMeasurement(device->powerHistory,2,0);
        addMeasurement(device->powerHistory,3,0);
        addMeasurement(device->powerHistory,1295,0);
        addMeasurement(device->powerHistory,2319,0);
        addMeasurement(device->powerHistory,2575,0);
       
        if (!device->powerHistory.stats[1295].last) {
          device->powerHistory.stats[1295].trend = 0;
        } else {
          device->powerHistory.stats[1295].trend = 0 - device->powerHistory.stats[1295].last;
        }
        device->powerHistory.stats[1295].last = 0;

        if (!device->powerHistory.stats[2319].last) {
          device->powerHistory.stats[2319].trend = 0;
        } else {
          device->powerHistory.stats[2319].trend = 0 - device->powerHistory.stats[2319].last;
        }
        device->powerHistory.stats[2319].last = 0;

        if (!device->powerHistory.stats[2575].last) {
          device->powerHistory.stats[2575].trend = 0;
        } else {
          device->powerHistory.stats[2575].trend = 0 - device->powerHistory.stats[2575].last;
        }
        device->powerHistory.stats[2575].last = 0;
      }
  }

  return true;
}



void scanCallback()
{
  
  DEBUG_PRINTLN(F("Lancement du Poll"));

  ScanDeviceToPoll();
DEBUG_PRINTLN(F("ScanDeviceToPoll OK"));
  FormattedDate = timeClient.getFullFormattedTime();
  Hour = timeClient.getHour() < 10 ? "0" + String(timeClient.getHour()) : String(timeClient.getHour());
  Day = timeClient.getDate() < 10 ? "0" + String(timeClient.getDate()) : String(timeClient.getDate());
  Month = timeClient.getMonth() < 10 ? "0" + String(timeClient.getMonth()) : String(timeClient.getMonth());
  Year = String(timeClient.getYear());
  Minute = timeClient.getMinute() < 10 ? "0" + String(timeClient.getMinute()) : String(timeClient.getMinute()); 
  Yesterday = timeClient.getYesterday();
  epochTime = String(timeClient.getEpochTime());
  String path = "configGeneral.json";
DEBUG_PRINTLN(F("config_write"));
  config_write(path, "epoch", epochTime);
DEBUG_PRINTLN(F("config_write OK"));

  //rules
 DEBUG_PRINTLN(F("Rules -->")); 
  /*Rule rules[10];
  int ruleCount = 0;
  jsonToRules(rules, ruleCount);
  applyRules(rules, ruleCount);*/
  
  if (rulesManager.loadFromFile("/config/rules.json"))
  {
    rulesManager.applyRules();
  }

  // Notifications

  if (ConfigNotif.ProdZero)
  {
    //Production
    long sumProd=0;
    if (strcmp(ConfigGeneral.Production,"")!=0)
    {
      DeviceData* devProd = nullptr;
      for (auto* d : devices) {
        if (d == nullptr) {
          log_e("Warning: NULL pointer found in devices vector\n");
          continue;
        }
        if (d->getDeviceID() == ConfigGeneral.Production) { devProd = d; break; }
      }

      DeviceEnergyHistory& ehProd = devProd->energyHistory;
      PeriodData* pdProd = nullptr;
      pdProd=&ehProd.hours;
      
      for (auto &kv : pdProd->graph) {
        ValueMap &vm = kv.second;   
        int attrId = 1;
        auto itv = vm.attributes.find(attrId);
        if (itv != vm.attributes.end()) {       
          sumProd += itv->second;
        }
      }

    }

    if (sumProd == 0)
    {
      if (!oldProdZero)
      {
        oldProdZero=true;
        String text = "Pas de production photovoltaïque sur 24H";
        if (!notifList->isFull())
        {
          notifList->push(Notification{" ☀️⚡Production nulle",text,FormattedDate,3,0});
          notificationManager.addNotification(
            "☀️⚡Production nulle", 
            text, 
            1
          );
        }else{
          notifList->shift();
          notificationManager.addNotification(
            "☀️⚡Production nulle", 
            text, 
            1
          );
          notifList->push(Notification{"Production nulle",text,FormattedDate,3,0});
        } 
      }
    }else{
      oldProdZero=false;
    }

  }


  log_e("Sauvegarde de tous les devices");
  for (auto d : devices) {
      d->saveToFile();
      if (d->getInfo().model=="ZLinky_TIC")
      {
        savePowerHistory(d->getDeviceID(),d->powerHistory);  
        saveEnergyHistory(d->getDeviceID(),d->energyHistory);
      }  

      if (d->getInfo().model=="ZiPulses")
      {
        saveEnergyHistory(d->getDeviceID(),d->energyHistory);
      }
  } 
  log_e("ScanDevicesToRAZ");
  ScanDevicesToRAZ();
}


// a pause of a half second in the UART transmission is considered the end of transmission.
const uint32_t communicationTimeout_ms = 500;

// Create a mutex for the access to uart_buffer
// only one task can read/write it at a certain time
SemaphoreHandle_t uart_buffer_Mutex = NULL;
SemaphoreHandle_t file_Mutex = NULL;
SemaphoreHandle_t inifile_Mutex = NULL;
SemaphoreHandle_t Queue_Mutex = NULL;
SemaphoreHandle_t QueuePrio_Mutex = NULL;


TaskHandle_t taskTCP;

void TcpTreatment(void * pvParameters)
{
  AsyncClient *client;
  client =  (AsyncClient *)pvParameters;
  log_d("TcpTreatment\n");
  while (1)
  {
    esp_task_wdt_reset();

    String datas = "HM:";
    datas +=String(strtol(getZigbeeValue(String(ConfigGeneral.ZLinky)+".json","0B04","1295").c_str(),0,16));
    datas +="|";
    datas +=String(strtol(getZigbeeValue(String(ConfigGeneral.ZLinky)+".json","0B04","2319").c_str(),0,16));
    datas +="|";
    datas +=String(strtol(getZigbeeValue(String(ConfigGeneral.ZLinky)+".json","0B04","2575").c_str(),0,16));
    
    if (client->space() > strlen(datas.c_str()) && client->canSend())
    {
      log_i("send to Marstek infos : %s\n",datas.c_str());
      client->add(datas.c_str(), strlen(datas.c_str()));
      client->send();
    }
    log_w("TcpTreatement : uxTaskGetStackHighWaterMark(NULL) : %d",uxTaskGetStackHighWaterMark(NULL));

    vTaskDelay(10000/portTICK_PERIOD_MS);
  }
}



static void handleData(void *arg, AsyncClient *client, void *data, size_t len)
{
	log_d("\n data received from client %s \n", client->remoteIP().toString().c_str());
  strncpy(ConfigGeneral.marstekIP,client->remoteIP().toString().c_str(),18);
  if (memcmp(data,"hello",5)==0)
  {
    log_d("get hello\n");
    ConfigGeneral.connectedMarstek = true;
    log_d("create thread : %d\n",taskTCP);
    if (taskTCP!=NULL)
    {
      vTaskDelete(taskTCP);
    }
    
    xTaskCreatePinnedToCore(
                TcpTreatment,   // Function to implement the task 
                "TcpTreatment", // Name of the task 
                8*1024,      // Stack size in words 
                (void *) client,       // Task input parameter 
                18,          // Priority of the task 
                &taskTCP,       // Task handle. 
                0);
  }
}

static void handleError(void *arg, AsyncClient *client, int8_t error)
{
	log_e("\n connection error %s from client %s \n", client->errorToString(error), client->remoteIP().toString().c_str());
  //ConfigGeneral.connectedMarstek = false;
}

static void handleDisconnect(void *arg, AsyncClient *client)
{
	log_e("\n client %s disconnected \n", client->remoteIP().toString().c_str());
  ConfigGeneral.connectedMarstek = false;
  strcpy(ConfigGeneral.marstekIP,"");
}

static void handleTimeOut(void *arg, AsyncClient *client, uint32_t time)
{
	log_e("\n client ACK timeout ip: %s \n", client->remoteIP().toString().c_str());
 // ConfigGeneral.connectedMarstek = false;
}

static void handleNewClient(void *arg, AsyncClient *client)
{
	// register events
	client->onData(&handleData, NULL);
	client->onError(&handleError, NULL);
	client->onDisconnect(&handleDisconnect, NULL);
	client->onTimeout(&handleTimeOut, NULL);
}

void tcpProcess()
{
  TcpServer = new AsyncServer(12345); 
	TcpServer->onClient(&handleNewClient, TcpServer);
	TcpServer->begin();
}

void udpProcess()
{
  if (UdpServer.listen(12345)) 
  {
    log_i("UDP listening on ip : %d - port : 12345",WiFi.localIP().toString());
    UdpServer.onPacket([](AsyncUDPPacket packet) 
    {
      log_i("receive data : %s",packet.data());
      if (memcmp(packet.data(), "hame", 4)==0)
      {
        log_i("send ACK");
        packet.print("ack");
      }else{
        log_i("send ACK");
        packet.print("ack");
      }
    });
  }

}

void datasTreatment(void * pvParameters)
{
  
  while(true)
  {
    esp_task_wdt_reset();

    // Vérifier la pile disponible
    UBaseType_t stackRemaining = uxTaskGetStackHighWaterMark(NULL);
    if (stackRemaining < 1000) { // Seuil d'alerte
        log_e("Stack critique: %d bytes restants", stackRemaining);
    }
   
    sendPacket();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  vTaskDelete(NULL);

}

extern uint32_t crcErrorCount;
extern uint32_t lastCrcError;

void printCrcStats() {
    static uint32_t last_check = 0;
    static uint32_t lastCrcCount = 0;
    uint32_t now = millis();
    
    if (now - last_check > 30000) { // Toutes les 30 secondes
        uint32_t crcErrorsThisPeriod = crcErrorCount - lastCrcCount;
        
        log_e("📊 Serial Stats:");
        log_e("   QueuePacket size: %d/%d", QueuePacket->size(), 300);
        log_e("   PriorityQueue size: %d/%d", PriorityQueuePacket->size(), 30);
        log_e("   🔴 CRC Errors: %d total (%d dernières 30s)", crcErrorCount, crcErrorsThisPeriod);
        
        // Alert si taux d'erreur élevé
        if (crcErrorsThisPeriod > 5) {
            log_e("   ⚠️  TAUX D'ERREUR CRC ÉLEVÉ - Vérifier connexions");
        }
        
        // Stats mémoire
        log_e("   Free Heap: %d bytes", ESP.getFreeHeap());
        log_e("   Free PSRAM: %d bytes", ESP.getFreePsram());
        
        lastCrcCount = crcErrorCount;
        last_check = now;
    }
}



void printHexData(const uint8_t* data, size_t length) {
    // Buffer statique pour éviter les allocations répétées
    static char hexBuffer[16]; // 2 chars + space + null pour chaque byte
    
    for (size_t i = 0; i < length; i++) {
        snprintf(hexBuffer, sizeof(hexBuffer), "%02X ", data[i]);
        DEBUG_PRINT(hexBuffer);

        // Éviter de surcharger le port série
        if (i % 32 == 31) { // Pause tous les 32 bytes
            vTaskDelay(1);
        }
    }
}

void processCompleteFrame(uint8_t frame_buffer[], size_t frame_len) {
    if (frame_len < 5) {
        log_w("Trame trop courte: %d bytes", frame_len);
        //addDebugLog(F("Trame trop courte"));
        return;
    }
    
    // Parser l'en-tête
    uint16_t type = (frame_buffer[0] << 8) | frame_buffer[1];
    uint16_t declared_len = (frame_buffer[2] << 8) | frame_buffer[3];
    uint8_t received_crc = frame_buffer[4];
    
    // DOUBLE vérification de cohérence (sécurité)
    if (declared_len != (frame_len - 5)) {
        log_e("🔴 Longueur incohérente: déclarée=%d, frame=%d", declared_len, frame_len - 5);
        //addDebugLog(F("Longueur incohérente"));
        return;
    }
    
    // Calculer CRC sur la longueur DÉCLARÉE uniquement
    uint8_t calculated_crc = 0;
    for (size_t i = 0; i < (5 + declared_len); i++) {
        if (i != 4) {  // Skip checksum position
            calculated_crc ^= frame_buffer[i];
        }
    }
    
    if (calculated_crc == received_crc) {
        // ✅ CRC OK - construire le protocole
        ZiGateProtocol protocol = {};
        protocol.type = type;
        protocol.ln = declared_len;
        protocol.chksum = received_crc;
        
        // Copier SEULEMENT la longueur déclarée
        memcpy(protocol.payload, &frame_buffer[5], declared_len);
        
        // Traitement direct
        log_v("✅ Trame OK: type=0x%04X, len=%d", type, declared_len);
        DecodePayload(protocol, 5 + declared_len);
        
    } else {
        // ❌ CRC ERROR avec plus de détails
        log_e("🔴 CRC ERROR: type=0x%04X, calc=0x%02X, reçu=0x%02X", 
              type, calculated_crc, received_crc);
        log_e("    Frame: len=%d, declared=%d", frame_len, declared_len);
        
        // Dump complet pour debug
        log_e("    Hex: ");
        for (size_t i = 0; i < frame_len && i < 30; i++) {
            Serial.printf("%02X ", frame_buffer[i]);
        }
        Serial.println();
        
        addDebugLog(F("CRC error"));
        // Statistiques d'erreur
        extern uint32_t crcErrorCount;
        crcErrorCount++;
    }
}

// Variables globales pour maintenir l'état entre les appels
static uint8_t g_frame_buffer[2048];
static size_t g_frame_pos = 0;
static bool g_in_frame = false;
static bool g_escape_next = false;

void parseRawDataPersistent(uint8_t raw_data[], size_t data_len) {
    for (size_t i = 0; i < data_len; i++) {
        uint8_t byte = raw_data[i];
        
        if (byte == 0x01) {
            // STX - Nouveau début de trame
            if (g_in_frame && g_frame_pos > 0) {
                log_w("⚠️ STX inattendu, abandon trame (%d bytes)", g_frame_pos);
               // addDebugLog(F("STX inattendu"));
            }
            g_frame_pos = 0;
            g_in_frame = true;
            g_escape_next = false;
            log_v("🟢 STX détecté");
            
        } else if (byte == 0x03 && g_in_frame) {
            // ETX - Fin de trame
            if (g_frame_pos >= 5) {
                // Vérification de cohérence
                uint16_t declared_len = (g_frame_buffer[2] << 8) | g_frame_buffer[3];
                uint16_t actual_payload = g_frame_pos - 5;
                
                if (declared_len == actual_payload) {
                    // ✅ Trame parfaite
                    log_v("✅ Trame complète: %d bytes", g_frame_pos);
                    processCompleteFrame(g_frame_buffer, g_frame_pos);
                    
                    if (ConfigSettings.enableDebug) {
                        printHexData(g_frame_buffer, g_frame_pos);
                        DEBUG_PRINTLN();
                    }
                } else {
                    // ❌ Trame corrompue
                    log_e("🔴 CORRUPTION - Déclaré:%d, Réel:%d", declared_len, actual_payload);
                    //addDebugLog(F("CORRUPTION"));
                    log_e("    Type: 0x%02X%02X, Checksum: 0x%02X", 
                          g_frame_buffer[0], g_frame_buffer[1], g_frame_buffer[4]);
                    
                    // Dump détaillé
                    Serial.print("    Frame: ");
                    for (int j = 0; j < g_frame_pos && j < 30; j++) {
                        Serial.printf("%02X ", g_frame_buffer[j]);
                    }
                    Serial.println();
                    
                    // DIAGNOSTIC des escape manqués
                    log_e("    État escape_next: %s", g_escape_next ? "TRUE" : "FALSE");
                    
                    // Chercher des patterns suspects dans la trame
                    bool has_delimiters = false;
                    for (size_t k = 0; k < g_frame_pos; k++) {
                        if (g_frame_buffer[k] == 0x01 || g_frame_buffer[k] == 0x02 || g_frame_buffer[k] == 0x03) {
                            has_delimiters = true;
                            log_e("    ⚠️ Délimiteur 0x%02X trouvé à position %d", g_frame_buffer[k], k);
                            //addDebugLog(F("Délimiteur"));
                        }
                    }
                    
                    if (!has_delimiters) {
                        log_e("    💡 Corruption probable: escape sequence coupée");
                       // addDebugLog(F("Corruption probable"));
                    }
                }
            }
            
            // Reset OBLIGATOIRE
            g_frame_pos = 0;
            g_in_frame = false;
            g_escape_next = false;
            
        } else if (byte == 0x02 && g_in_frame) {
            // Escape byte - CRITICAL: garder l'état pour le prochain appel
            g_escape_next = true;
            log_v("🔄 Escape détecté, attente du byte suivant...");
            
        } else if (g_in_frame) {
            // Données dans la trame
            if (g_escape_next) {
                // Appliquer le décodage
                uint8_t decoded = byte ^ 0x10;
                g_frame_buffer[g_frame_pos++] = decoded;
                g_escape_next = false;
                log_v("🔓 Décodé: 0x%02X -> 0x%02X", byte, decoded);
            } else {
                // Byte normal
                g_frame_buffer[g_frame_pos++] = byte;
            }
            
            // Protection overflow
            if (g_frame_pos >= sizeof(g_frame_buffer) - 1) {
                log_e("🔴 OVERFLOW! Reset forcé");
                addDebugLog(F("OVERFLOW"));
                g_frame_pos = 0;
                g_in_frame = false;
                g_escape_next = false;
            }
        }
        // Ignorer bytes hors trame
    }
}

void SerialTask(void *pvParameters) {
    // Buffer en DRAM pour performance
    static uint8_t* rxBuffer = (uint8_t*)heap_caps_malloc(2048, MALLOC_CAP_INTERNAL);
    if (!rxBuffer) {
        log_e("❌ Erreur allocation rxBuffer");
        vTaskDelete(NULL);
        return;
    }
    
    log_i("✅ SerialTask démarrée avec buffer DRAM");
    
    while (true) {
        esp_task_wdt_reset();
        
        // LECTURE PLUS AGRESSIVE
        size_t available = Serial1.available();
        if (available > 0) {
            // Limiter pour éviter monopole CPU mais être plus réactif
            size_t toRead = (available > 1024) ? 1024 : available;
            size_t bytesRead = Serial1.readBytes(rxBuffer, toRead);
            
            if (bytesRead > 0) {
                // Parser avec la nouvelle fonction
                parseRawDataPersistent(rxBuffer, bytesRead);
                
                if (ConfigSettings.enableDebug && bytesRead < 50) {
                    log_v("📥 Lu %d bytes", bytesRead);
                }
            }
        }
        
        // DÉLAI RÉDUIT pour plus de réactivité
        vTaskDelay(pdMS_TO_TICKS(2)); // AU LIEU DE 10ms
        
        // NOUVEAU : Monitoring périodique
        static uint32_t lastMonitor = 0;
        if (millis() - lastMonitor > 30000) {
            UBaseType_t stackFree = uxTaskGetStackHighWaterMark(NULL);
            log_i("📊 SerialTask - Stack libre: %d, Available: %d", stackFree, Serial1.available());
            
            if (stackFree < 2048) {
                log_e("⚠️ Stack SerialTask critique!");
            }
            
            lastMonitor = millis();
        }
    }
}

void connectToMqtt() {
  
  if (ConfigSettings.enableMqtt)
  {
    DEBUG_PRINT(F("Connecting to MQTT..."));
    
    DEBUG_PRINT(ConfigGeneral.servMQTT);
    DEBUG_PRINT(F(":"));
    DEBUG_PRINTLN(ConfigGeneral.portMQTT);
    mqttClient.disconnect();
    mqttClient.setServer(ConfigGeneral.servMQTT, atoi(ConfigGeneral.portMQTT));
    mqttClient.setClientId(ConfigGeneral.clientIDMQTT);
    if (String(ConfigGeneral.userMQTT) !="")
    {
      mqttClient.setCredentials(ConfigGeneral.userMQTT, ConfigGeneral.passMQTT);
    }
    mqttClient.connect();
  }

}

/*void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  addDebugLog(F("WiFi Disconnected"));
    log_d("%d",info.wifi_sta_disconnected.reason);
  // added in V2.20:  call disconnect before reconnecting to reset wifi stack
    if (mqttReconnectTimer != NULL)
    {
      xTimerStop(mqttReconnectTimer, 0); 
    }
    if (info.wifi_sta_disconnected.reason != 201)
    {
      xTimerStart(WifiReconnectTimer, 0);
    }else{
      xTimerStop(WifiReconnectTimer,0);
      WiFi.mode(WIFI_AP);
      WiFi.disconnect();
      
      uint8_t mac[WL_MAC_ADDR_LENGTH];
      WiFi.softAPmacAddress(mac);
      String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                    String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
      macID.toUpperCase();
      String AP_NameString = "LIXEEBOX-" + macID;

      char AP_NameChar[AP_NameString.length() + 1];
      memset(AP_NameChar, 0, AP_NameString.length() + 1);

      for (int i=0; i<AP_NameString.length(); i++)
        AP_NameChar[i] = AP_NameString.charAt(i);

      String WIFIPASSSTR = "admin"+macID;
      char WIFIPASS[WIFIPASSSTR.length()+1];
      memset(WIFIPASS,0,WIFIPASSSTR.length()+1);
      for (int i=0; i<WIFIPASSSTR.length(); i++)
        WIFIPASS[i] = WIFIPASSSTR.charAt(i);

      WiFi.softAP(AP_NameChar,WIFIPASS );
      WiFi.setSleep(false);

    }
}*/

/*const char * pop = ""; // Proof of possession - otherwise called a PIN - string provided by the device, entered by the user in the phone app
const char * service_name = "LIXEE_GW"; // Name of your device (the Espressif apps expects by default device name starting with "Prov_")
const char * service_key = NULL; // Password used for SofAP method (NULL = no password needed)
bool reset_provisioned = true; // When true the library will automatically delete previously provisioned data.*/

/*void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {

    case ARDUINO_EVENT_WIFI_READY:
      addDebugLog(F("WiFi Ready"));
      break;
    case ARDUINO_EVENT_WIFI_STA_START:
      ConfigGeneral.scanNumber = -1;
    break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
      ConfigGeneral.scanNumber = info.wifi_scan_done.number;
    break;
    case ARDUINO_EVENT_WIFI_AP_START: 
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      addDebugLog(F("WiFi Connected"));
      if (WifiReconnectTimer != NULL)
      {
        xTimerStop(WifiReconnectTimer, 0); 
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      addDebugLog(F("ARDUINO_EVENT_WIFI_STA_DISCONNECTED"));
          
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      addDebugLog(F("WiFi LOST IP"));
      if (mqttReconnectTimer != NULL)
      {
        xTimerStop(mqttReconnectTimer, 0); 
      }
      xTimerStart(WifiReconnectTimer, 0);
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      connectToMqtt();
      break;
    
    default:
      break;
  }
}*/



void onMqttConnect(bool sessionPresent) {
  DEBUG_PRINTLN(F("Connected to MQTT."));
  DEBUG_PRINT(F("Session present: "));
  DEBUG_PRINTLN(sessionPresent);
  extern unsigned long lastConnectionTest;
  lastConnectionTest = millis();
  mqttResetReconnectionFlag();

}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
 log_e("⚠️ Disconnected from MQTT - Reason: %d", (uint8_t)reason);
    // La reconnexion sera gérée par mqttAutoReconnect() dans loop()
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  DEBUG_PRINTLN(F("Subscribe acknowledged."));
  DEBUG_PRINT(F("  packetId: "));
  DEBUG_PRINTLN(packetId);
  DEBUG_PRINT(F("  qos: "));
  DEBUG_PRINTLN(qos);
}

void onMqttUnsubscribe(uint16_t packetId) {
  DEBUG_PRINTLN(F("Unsubscribe acknowledged."));
  DEBUG_PRINT(F("  packetId: "));
  DEBUG_PRINTLN(packetId);
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  DEBUG_PRINTLN(F("Publish received."));
  DEBUG_PRINT(F("  topic: "));
  DEBUG_PRINTLN(topic);
  DEBUG_PRINT(F("  qos: "));
  DEBUG_PRINTLN(properties.qos);
  DEBUG_PRINT(F("  dup: "));
  DEBUG_PRINTLN(properties.dup);
  DEBUG_PRINT(F("  retain: "));
  DEBUG_PRINTLN(properties.retain);
  DEBUG_PRINT(F("  len: "));
  DEBUG_PRINTLN(len);
  DEBUG_PRINT(F("  index: "));
  DEBUG_PRINTLN(index);
  DEBUG_PRINT(F("  total: "));
  DEBUG_PRINTLN(total);
}

void onMqttPublish(uint16_t packetId) {
  DEBUG_PRINTLN(F("Publish acknowledged."));
  DEBUG_PRINT(F("  packetId: "));
  DEBUG_PRINTLN(packetId);

  if (packetId > 0) {
      lastConnectionTest = millis();
  }
  
}

void initTempSensor(){
    temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
    temp_sensor.dac_offset = TSENS_DAC_L2;  // TSENS_DAC_L2 is default; L4(-40°C ~ 20°C), L2(-10°C ~ 80°C), L1(20°C ~ 100°C), L0(50°C ~ 125°C)
    temp_sensor_set_config(temp_sensor);
    temp_sensor_start();
}



//-------------------------------------
// Fonction : Parcourir un répertoire
// pour trouver tous les .json
//-------------------------------------
void loadAllDevices(const char* dirPath) {
    // On ouvre le répertoire
    File root = LittleFS.open(dirPath);
    if (!root || !root.isDirectory()) {
        Serial.printf("Le chemin '%s' n'est pas un dossier ou est inaccessible.\n", dirPath);
        return;
    }

    // Parcourt le contenu
    while (true) {
        File entry = root.openNextFile();
        if (!entry) {
            // Plus de fichiers
            break;
        }

        // On ignore si c'est un sous-dossier
        if (entry.isDirectory()) {
            entry.close();
            continue;
        }

        String filename = entry.name(); // ex: "/devices/19869.json"
        entry.close();

        // Vérifier que ça se termine par .json
        if (!filename.endsWith(".json")) {
            continue;
        }

        // Extraire le deviceID (nom du fichier sans .json)
        // Par exemple, si filename = "/devices/19869.json"
        // On veut "19869" comme deviceID
        int lastSlash = filename.lastIndexOf('/');
        int lastDot   = filename.lastIndexOf('.');
        String deviceID = filename.substring(lastSlash + 1, lastDot);

        // On crée un DeviceData pour ce fichier
        // (filename = "/devices/19869.json", deviceID = "19869")
        // --- Allocation en PSRAM ---
        size_t sz = sizeof(DeviceData);
        void* mem = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
        if (!mem) {
          log_e("Erreur ps_malloc pour %s", filename.c_str());
          continue; // ou break
        }
        log_w("avant  Core : %d - Heap size : %ld - Free heap : %ld - Free PSRAM: %ld - uxTaskGetStackHighWaterMark: %ld",xPortGetCoreID(),ESP.getHeapSize(),ESP.getFreeHeap(),ESP.getFreePsram(),uxTaskGetStackHighWaterMark(NULL));

        
        DeviceData *dev = new (mem) DeviceData("/db/"+filename, deviceID);
        
        // On charge
        if (dev->loadFromFile()) {
         
          if (dev->getInfo().model =="ZLinky_TIC")
          {
            parsePowerHistory(dev->getDeviceID(),dev->powerHistory);
            parseDeviceHistory(dev->getDeviceID(),dev->energyHistory);
          }    
          
          if (dev->getInfo().model =="ZiPulses")
          {
            parseDeviceHistory(dev->getDeviceID(),dev->energyHistory);
          }  
          devices.push_back(dev);
        }
        log_w("apres Core : %d - Heap size : %ld - Free heap : %ld - Free PSRAM: %ld - uxTaskGetStackHighWaterMark: %ld",xPortGetCoreID(),ESP.getHeapSize(),ESP.getFreeHeap(),ESP.getFreePsram(),uxTaskGetStackHighWaterMark(NULL));
        
    }
}


bool loadConfigWifi() {
  const char * path = "/config/configWifi.json";
  
  File configFile = LittleFS.open(path, FILE_READ);
  if (!configFile) {
    DEBUG_PRINTLN(F("failed open"));
    configFile.close();
    return false;
  }

  SpiRamJsonDocument doc(10192);
  deserializeJson(doc,configFile);

  // affectation des valeurs , si existe pas on place une valeur par defaut
  //ConfigSettings.enableWiFi = (int)doc["enableWiFi"];
  ConfigSettings.enableWiFi = 1;
  ConfigSettings.enableDHCP = (int)doc["enableDHCP"];
  strlcpy(ConfigSettings.ssid, doc["ssid"] | "", sizeof(ConfigSettings.ssid));
  strlcpy(ConfigSettings.password, doc["pass"] | "", sizeof(ConfigSettings.password));
  strlcpy(ConfigSettings.ipAddressWiFi, doc["ip"] | "", sizeof(ConfigSettings.ipAddressWiFi));
  strlcpy(ConfigSettings.ipMaskWiFi, doc["mask"] | "", sizeof(ConfigSettings.ipMaskWiFi));
  strlcpy(ConfigSettings.ipGWWiFi, doc["gw"] | "", sizeof(ConfigSettings.ipGWWiFi));

  configFile.close();
  return true;
}

bool loadConfigGeneral() {
  const char * path = "/config/configGeneral.json";
  
  File configFile = LittleFS.open(path, FILE_READ);
  if (!configFile) {
    DEBUG_PRINTLN(F("failed open"));
    return false;
  }

  SpiRamJsonDocument doc(10192);
  deserializeJson(doc,configFile);

  ConfigSettings.enableHWFlow = (int)doc["enableHWFlow"];

  ConfigGeneral.firstStart = (int)doc["firstStart"];
  ConfigSettings.enableDebug = (int)doc["enableDebug"];
  if (ConfigSettings.enableDebug)
  {
    //#define DEBUG_PRINT(x)  Serial.print(x) 
    //#define DEBUG_PRINTLN(x)  Serial.println(x)
    #define DEBUG_ON
  }else{
    #undef DEBUG_ON
   /* #define DEBUG_PRINT(x) 
    #define DEBUG_PRINTLN(x) */
  }
  strlcpy(ConfigGeneral.ZLinky, doc["ZLinky"] | "", sizeof(ConfigGeneral.ZLinky));
  strlcpy(ConfigGeneral.Production, doc["Production"] | "", sizeof(ConfigGeneral.Production));
  strlcpy(ConfigGeneral.Gaz, doc["Gaz"] | "", sizeof(ConfigGeneral.Gaz));
  strlcpy(ConfigGeneral.tarifGaz, doc["tarifGaz"] | "0", sizeof(ConfigGeneral.tarifGaz));
  strlcpy(ConfigGeneral.unitGaz, doc["unitGaz"] | "m3", sizeof(ConfigGeneral.unitGaz));
  if (!doc["coeffGaz"])
  {
    ConfigGeneral.coeffGaz=1;
  }else{
    ConfigGeneral.coeffGaz=doc["coeffGaz"].as<int>();
  }

  strlcpy(ConfigGeneral.Water, doc["Water"] | "", sizeof(ConfigGeneral.Water));
  strlcpy(ConfigGeneral.tarifWater, doc["tarifWater"] | "0", sizeof(ConfigGeneral.tarifWater));
  strlcpy(ConfigGeneral.unitWater, doc["unitWater"] | "L", sizeof(ConfigGeneral.unitWater));
  if (!doc["coeffWater"])
  {
    ConfigGeneral.coeffWater=1;
  }else{
    ConfigGeneral.coeffWater=doc["coeffWater"].as<int>();
  }

  ConfigGeneral.LinkyMode = ini_read(String(ConfigGeneral.ZLinky)+".json","FF66", "768").toInt();
  if (!doc["powerMaxDatas"])
  {
    ConfigGeneral.powerMaxDatas=1000;
  }else{
    ConfigGeneral.powerMaxDatas=doc["powerMaxDatas"].as<int>();
  }
  strlcpy(ConfigGeneral.ntpserver, doc["ntpserver"] | "51.38.81.135", sizeof(ConfigGeneral.ntpserver));
  strlcpy(ConfigGeneral.timezone, doc["timezone"] | "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(ConfigGeneral.timezone));
  
  if (doc["timeoffset"].as<String>()!="")
  {
    ConfigGeneral.timeoffset = doc["timeoffset"].as<int>();
  }else{
    ConfigGeneral.timeoffset = 1;   
  }

  ConfigGeneral.epochTime = doc["epoch"].as<long>();

  ConfigGeneral.HouseSurface=(int)doc["shon"];
 
  strlcpy(ConfigGeneral.tarifAbo, doc["tarifAbo"] | "0", sizeof(ConfigGeneral.tarifAbo));
  strlcpy(ConfigGeneral.tarifCTA, doc["tarifCTA"] | "0", sizeof(ConfigGeneral.tarifCTA));
  strlcpy(ConfigGeneral.tarifCSPE, doc["tarifCSPE"] | "0", sizeof(ConfigGeneral.tarifCSPE));

  strlcpy(ConfigGeneral.tarifIdx1, doc["tarifIdx1"] | "0", sizeof(ConfigGeneral.tarifIdx1));
  strlcpy(ConfigGeneral.tarifIdx2, doc["tarifIdx2"] | "0", sizeof(ConfigGeneral.tarifIdx2));
  strlcpy(ConfigGeneral.tarifIdx3, doc["tarifIdx3"] | "0", sizeof(ConfigGeneral.tarifIdx3));
  strlcpy(ConfigGeneral.tarifIdx4, doc["tarifIdx4"] | "0", sizeof(ConfigGeneral.tarifIdx4));
  strlcpy(ConfigGeneral.tarifIdx5, doc["tarifIdx5"] | "0", sizeof(ConfigGeneral.tarifIdx5));
  strlcpy(ConfigGeneral.tarifIdx6, doc["tarifIdx6"] | "0", sizeof(ConfigGeneral.tarifIdx6));
  strlcpy(ConfigGeneral.tarifIdx7, doc["tarifIdx7"] | "0", sizeof(ConfigGeneral.tarifIdx7));
  strlcpy(ConfigGeneral.tarifIdx8, doc["tarifIdx8"] | "0", sizeof(ConfigGeneral.tarifIdx8));
  strlcpy(ConfigGeneral.tarifIdx9, doc["tarifIdx9"] | "0", sizeof(ConfigGeneral.tarifIdx9));
  strlcpy(ConfigGeneral.tarifIdx10, doc["tarifIdx10"] | "0", sizeof(ConfigGeneral.tarifIdx10));

  strlcpy(ConfigGeneral.tarifAboProd, doc["tarifAboProd"] | "0", sizeof(ConfigGeneral.tarifAboProd));
  strlcpy(ConfigGeneral.tarifIdxProd, doc["tarifIdxProd"] | "0", sizeof(ConfigGeneral.tarifIdxProd));

  ConfigSettings.enableMqtt = (int)doc["enableMqtt"];
  strlcpy(ConfigGeneral.servMQTT, doc["servMQTT"] | "", sizeof(ConfigGeneral.servMQTT));
  strlcpy(ConfigGeneral.clientIDMQTT, doc["clientIDMQTT"] | "", sizeof(ConfigGeneral.clientIDMQTT));
  strlcpy(ConfigGeneral.portMQTT, doc["portMQTT"] | "", sizeof(ConfigGeneral.portMQTT));
  strlcpy(ConfigGeneral.userMQTT, doc["userMQTT"] | "", sizeof(ConfigGeneral.userMQTT));
  strlcpy(ConfigGeneral.passMQTT, doc["passMQTT"] | "", sizeof(ConfigGeneral.passMQTT));
  strlcpy(ConfigGeneral.headerMQTT, doc["headerMQTT"] | "", sizeof(ConfigGeneral.headerMQTT));
  ConfigGeneral.HAMQTT = (int)doc["HAMQTT"];
  ConfigGeneral.TBMQTT = (int)doc["TBMQTT"];
  ConfigGeneral.customMQTT = (int)doc["customMQTT"];
  ConfigGeneral.customMQTTJson =doc["customMQTTJson"].as<String>();

  ConfigSettings.enableUDP = (int)doc["enableUDP"];
  strlcpy(ConfigGeneral.servUDP, doc["servUDP"] | "", sizeof(ConfigGeneral.servUDP));
  strlcpy(ConfigGeneral.portUDP, doc["portUDP"] | "", sizeof(ConfigGeneral.portUDP));
  ConfigGeneral.customUDPJson =doc["customUDPJson"].as<String>();

  ConfigSettings.enableMarstek = (int)doc["enableMarstek"];

  ConfigSettings.enableNotif = (int)doc["enableNotif"];
  strlcpy(ConfigGeneral.servSMTP, doc["servSMTP"] | "", sizeof(ConfigGeneral.servSMTP));
  strlcpy(ConfigGeneral.portSMTP, doc["portSMTP"] | "", sizeof(ConfigGeneral.portSMTP));
  strlcpy(ConfigGeneral.userSMTP, doc["userSMTP"] | "", sizeof(ConfigGeneral.userSMTP));
  strlcpy(ConfigGeneral.passSMTP, doc["passSMTP"] | "", sizeof(ConfigGeneral.passSMTP));

  ConfigSettings.enableSecureHttp = (int)doc["enableSecureHttp"];
  strlcpy(ConfigGeneral.userHTTP, doc["userHTTP"] | "", sizeof(ConfigGeneral.userHTTP));
  strlcpy(ConfigGeneral.passHTTP, doc["passHTTP"] | "", sizeof(ConfigGeneral.passHTTP));

  ConfigSettings.enableWebPush = (int)doc["enableWebPush"];
  ConfigGeneral.webPushAuth = (int)doc["webPushAuth"];
  strlcpy(ConfigGeneral.servWebPush, doc["servWebPush"] | "", sizeof(ConfigGeneral.servWebPush));
  strlcpy(ConfigGeneral.userWebPush, doc["userWebPush"] | "", sizeof(ConfigGeneral.userWebPush));
  strlcpy(ConfigGeneral.passWebPush, doc["passWebPush"] | "", sizeof(ConfigGeneral.passWebPush));

  ConfigSettings.enableHistory = (int)doc["enableHistory"];
  ConfigGeneral.developerMode = (int)doc["developerMode"];


  ConfigNotif.PowerOutage = (int)doc["PowerOutage"];
  ConfigNotif.PriceChange = (int)doc["PriceChange"];
  ConfigNotif.SubscribedPower = (int)doc["SubscribedPower"];
  ConfigNotif.ProdSupConso = (int)doc["ProdSupConso"];
  ConfigNotif.ProdZero = (int)doc["ProdZero"];
  ConfigNotif.ColorTomorrow = (int)doc["ColorTomorrow"];
  ConfigNotif.OverBudget = (int)doc["OverBudget"];
  ConfigNotif.OverBudgetThreshold = (int)doc["OverBudgetThreshold"];
  ConfigNotif.OverVoltage = (int)doc["OverVoltage"];
  ConfigNotif.OverVoltageThreshold = (int)doc["OverVoltageThreshold"];
  ConfigNotif.UnderVoltage = (int)doc["UnderVoltage"];
  ConfigNotif.UnderVoltageThreshold = (int)doc["UnderVoltageThreshold"];
  ConfigNotif.PEJP = (int)doc["PEJP"];
  ConfigNotif.RedColor = (int)doc["RedColor"];

  configFile.close();
  return true;
}

String getIDWifi()
{
  uint8_t baseMac[6];
  esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
  char macSuffix[5];
  sprintf(macSuffix, "%02X%02X", baseMac[4], baseMac[5]);
  return String(macSuffix);
}


bool setupSTAWifi() {
  
  
  vTaskDelay(10);
  if (strlen(ConfigSettings.ssid)>0)
  {
    WiFi.mode(WIFI_STA);
    DEBUG_PRINTLN(F("WiFi.mode(WIFI_STA)"));
    WiFi.disconnect();
    DEBUG_PRINTLN(F("disconnect"));
    WiFi.begin(ConfigSettings.ssid, ConfigSettings.password);
    DEBUG_PRINTLN(ConfigSettings.ssid);
    DEBUG_PRINTLN(ConfigSettings.password);
    //WiFi.setSleep(false);
    DEBUG_PRINTLN(F("WiFi.begin"));

    IPAddress ip_address = parse_ip_address(ConfigSettings.ipAddressWiFi);
    IPAddress gateway_address = parse_ip_address(ConfigSettings.ipGWWiFi);
    IPAddress netmask = parse_ip_address(ConfigSettings.ipMaskWiFi);
    IPAddress primaryDNS(8, 8, 8, 8);   //optional
    IPAddress secondaryDNS(8, 8, 4, 4); //optional
    if (!ConfigSettings.enableDHCP)
    {
      DEBUG_PRINTLN(F("WiFi.config"));
      WiFi.config(ip_address, gateway_address, netmask, primaryDNS,secondaryDNS);
    }
    int countDelay=50;
    while (WiFi.status() != WL_CONNECTED) {
      DEBUG_PRINT(F("."));
      countDelay--;
      if (countDelay==0)
      {
        ConfigSettings.connectedWifiSta=false;
        return false;
      }
      vTaskDelay(250);
    }
    uint8_t primaryChan;
    wifi_second_chan_t secondChan;
    esp_wifi_get_channel(&primaryChan, &secondChan);
    ConfigSettings.channelWifi = primaryChan;
    ConfigSettings.RSSIWifi = WiFi.RSSI();
    ConfigSettings.bssid = WiFi.BSSIDstr(0);  
    ConfigSettings.connectedWifiSta=true;
    return true;

  }else{
    return false;
  }
    
}

uint32_t prevheap = 0;
void monitor_heap(void) {
    uint32_t curheap = ESP.getFreeHeap();
    bool IsChanged = false;
    if (prevheap != curheap) {
        static uint32_t heap_highwater_mark = 0;
        static uint32_t heap_lowwater_mark = UINT32_MAX;

        if (curheap < heap_lowwater_mark) {IsChanged = true; heap_lowwater_mark = curheap;} 
        if (curheap > heap_highwater_mark) {IsChanged = true; heap_highwater_mark = curheap;}

        if(IsChanged) {
            uint32_t maxblock = ESP.getMaxAllocHeap();
            log_d("Heap   Free %u   Min %u   Max %u   Contig %u [%i]",
                curheap, heap_lowwater_mark, heap_highwater_mark, maxblock, xPortGetCoreID());
        }
        prevheap = curheap;
    }
}

void initWiFiServices() {
  // Cette fonction contient tous vos services WiFi actuels
  // MQTT, mDNS, serveur web, etc.
  
  //WiFi.onEvent(WiFiEvent);
  //WiFi.onEvent(WiFiStationDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  
  // mDNS
  String localdns = "LIXEEBOX-" + getIDWifi();
  if (!MDNS.begin(localdns.c_str())) {
    Serial.println("Error starting mDNS");
  }
  Serial.printf("📡mDNS : %s\n",localdns.c_str());
  
  // MQTT si activé
  if (ConfigSettings.enableMqtt) {

    
    mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
    
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onSubscribe(onMqttSubscribe);
    mqttClient.onUnsubscribe(onMqttUnsubscribe);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.onPublish(onMqttPublish);
    
    mqttClient.setServer(ConfigGeneral.servMQTT, atoi(ConfigGeneral.portMQTT));
    mqttClient.setClientId(ConfigGeneral.clientIDMQTT);
    if (String(ConfigGeneral.userMQTT) != "") {
      mqttClient.setCredentials(ConfigGeneral.userMQTT, ConfigGeneral.passMQTT);
    }
    mqttClient.connect();
  }
  
  // NTP
  timeClient.setPoolServerName((const char*)ConfigGeneral.ntpserver);
  timeClient.setTimeOffset((3600 * ConfigGeneral.timeoffset));
  timeClient.setTimeZone(ConfigGeneral.timezone);
  timeClient.setUpdateInterval(NTP_UPDATE_INTERVAL_MS);
  timeClient.begin();
  bool NTPOK = timeClient.forceUpdate();
  log_e("NTPOK : %d\r\n",NTPOK);

  log_w("datasTreatment - Core : %d - Heap size : %ld - Free heap : %ld - Free PSRAM: %ld - uxTaskGetStackHighWaterMark: %ld",xPortGetCoreID(),ESP.getHeapSize(),ESP.getFreeHeap(),ESP.getFreePsram(),uxTaskGetStackHighWaterMark(NULL));


  if (NTPOK)
  {  
   FormattedDate = timeClient.getFullFormattedTime();

   Hour = timeClient.getHour() < 10 ? "0" + String(timeClient.getHour()) : String(timeClient.getHour());
   Day = timeClient.getDate() < 10 ? "0" + String(timeClient.getDate()) : String(timeClient.getDate());
   Month = timeClient.getMonth() < 10 ? "0" + String(timeClient.getMonth()) : String(timeClient.getMonth());
   Year = String(timeClient.getYear());
   Minute = timeClient.getMinute() < 10 ? "0" + String(timeClient.getMinute()) : String(timeClient.getMinute()); 
   Yesterday =  timeClient.getYesterday();
   String path = "configGeneral.json";
   config_write(path, "epoch", String(timeClient.getEpochTime()));

  }else{
      timeClient.setEpochTime(ConfigGeneral.epochTime);
      FormattedDate = timeClient.getFullFormattedTime();

      Hour = timeClient.getHour() < 10 ? "0" + String(timeClient.getHour()) : String(timeClient.getHour());
      Day = timeClient.getDate() < 10 ? "0" + String(timeClient.getDate()) : String(timeClient.getDate());
      Month = timeClient.getMonth() < 10 ? "0" + String(timeClient.getMonth()) : String(timeClient.getMonth());
      Year = String(timeClient.getYear());
      Minute = timeClient.getMinute() < 10 ? "0" + String(timeClient.getMinute()) : String(timeClient.getMinute()); 
      Yesterday =  timeClient.getYesterday();
  }
  
  addDebugLog(verbose_print_reset_reason(rtc_get_reset_reason(0)));
  addDebugLog(verbose_print_reset_reason(rtc_get_reset_reason(1)));
  
  // Serveur web
  initWebServer();
  MDNS.addService("http", "tcp", 80);
  
  // Autres services...
  if (ConfigSettings.enableMarstek && strcmp(ConfigGeneral.ZLinky, "") != 0) {
    udpProcess();
    tcpProcess();
  }
}

void setupZigbeeAndTasks() {
  //Verification empty files
  scanFilesError();

  log_w("datasTreatment - Core : %d - Heap size : %ld - Free heap : %ld - Free PSRAM: %ld - uxTaskGetStackHighWaterMark: %ld",xPortGetCoreID(),ESP.getHeapSize(),ESP.getFreeHeap(),ESP.getFreePsram(),uxTaskGetStackHighWaterMark(NULL));


  pinMode(RESET_ZIGATE, OUTPUT);
  pinMode(FLASH_ZIGATE, OUTPUT);

  digitalWrite(FLASH_ZIGATE, 1);
  digitalWrite(RESET_ZIGATE, 0);
  digitalWrite(RESET_ZIGATE, 1);

  commandList->push(Packet{0x0011, 0x0000,0});
  
  commandList->push(Packet{0x0009, 0x0000,0});
  esp_task_wdt_reset();
  vTaskDelay(200);
  DEBUG_PRINTLN(F("start network"));
  commandList->push(Packet{0x0024, 0x0000,0});
  vTaskDelay(200);
  DEBUG_PRINTLN(F("add getversion"));
  commandList->push(Packet{0x0010, 0x0000,0});
  vTaskDelay(200);
  DEBUG_PRINTLN(F("add networkState"));
  commandList->push(Packet{0x0025, 0x0000,0});
  vTaskDelay(2000);

  log_w("datasTreatment - Core : %d - Heap size : %ld - Free heap : %ld - Free PSRAM: %ld - uxTaskGetStackHighWaterMark: %ld",xPortGetCoreID(),ESP.getHeapSize(),ESP.getFreeHeap(),ESP.getFreePsram(),uxTaskGetStackHighWaterMark(NULL));

  runner.init();
  runner.addTask(scan);
  scan.enableDelayed(60000);
  esp_task_wdt_reset();



  xTaskCreatePinnedToCore(
                      SerialTask,   // Function to implement the task 
                      "SerialTask", // Name of the task 
                      12*1024,      // Stack size in words 
                      NULL,       // Task input parameter 
                      22,//17,          // Priority of the task 
                      NULL,       // Task handle. 
                      1);  

  log_w("📊 - Core : %d - Heap size : %ld - Free heap : %ld - Free PSRAM: %ld - uxTaskGetStackHighWaterMark: %ld",xPortGetCoreID(),ESP.getHeapSize(),ESP.getFreeHeap(),ESP.getFreePsram(),uxTaskGetStackHighWaterMark(NULL));

  disableCore0WDT();

  if (rulesManager.loadFromFile("/config/rules.json"))
  {
    rulesManager.applyRules();
  }
  esp_task_wdt_init(30, true);
}


void clearWifiConfig() {
    if (LittleFS.exists("/config/configWifi.json")) {
        String path="configWifi.json";
        
        config_write(path,"ssid","");
        config_write(path,"pass","");
        config_write(path,"enableDHCP","1");
        Serial.println("[RESET] Config WiFi effacée");
    } else {
        Serial.println("[RESET] Pas de config WiFi à effacer");
    }
}

void blinkLed(uint8_t times, uint16_t delayMs) {
    for (uint8_t i = 0; i < times; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(delayMs);
        digitalWrite(LED_PIN, LOW);
        delay(delayMs);
    }
}

void setup(void)
{  
  ZiGateMode=PRODUCTION;
  initTempSensor();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
 
  Serial.begin(115200, SERIAL_8N1);
  DEBUG_PRINTLN(F("Start"));
  initCircularBuffer();


 /*Serial1.setRxBufferSize(BUFFER_SIZE);
  Serial1.begin(BAUD_RATE, SERIAL_8N1, RXD2, TXD2);*/
    
  // Configuration série optimisée pour réduire les erreurs CRC
  Serial1.setRxBufferSize(4096);  // Buffer hardware plus grand
  Serial1.setTimeout(1);  
  //Serial1.begin(BAUD_RATE, SERIAL_8N1, RXD2, TXD2);
  // Timeout plus court pour réactivité
  if (!Serial1.setPins(RXD2, TXD2, 5, 4)) {
    log_e("Failed setting UART pins!");
  }
  Serial1.begin(BAUD_RATE, SERIAL_8N1); 
  delay(100);
  
  
   //if (!Serial1.setPins(-1,-1,20,19))log_e("Failed setting RTS pin!");
    // Activer le contrôle de flux matériel
 // if (!Serial1.setHwFlowCtrlMode(HW_FLOWCTRL_CTS_RTS,64))log_e("Failed changing Flow Control from Software to Hardware RTS!");
  
  // creates a mutex object to control access to uart_buffer
  uart_buffer_Mutex = xSemaphoreCreateMutex();
  if (uart_buffer_Mutex == NULL) {
    DEBUG_PRINTLN(F("Error creating Mutex. Sketch will fail."));
    while (true) {
      DEBUG_PRINTLN(F("Mutex error (NULL). Program halted."));
      delay(1000);
    }
  }
  //Serial1.onReceive(SERIAL_RX_CB,true);  // sets the callback function
  delay(2000);
  DEBUG_PRINTLN(F("Send data to UART0 in order to activate the RX callback"));

  // creates a mutex object to control access to files
  Queue_Mutex = xSemaphoreCreateMutex();
  if (Queue_Mutex == NULL) {
    DEBUG_PRINTLN(F("Error creating Mutex. Sketch will fail."));
    while (true) {
      DEBUG_PRINTLN(F("Mutex error (NULL). Program halted."));
      delay(1000);
    }
  }

  // creates a mutex object to control access to files
  QueuePrio_Mutex = xSemaphoreCreateMutex();
  if (QueuePrio_Mutex == NULL) {
    DEBUG_PRINTLN(F("Error creating Mutex. Sketch will fail."));
    while (true) {
      DEBUG_PRINTLN(F("Mutex error (NULL). Program halted."));
      delay(1000);
    }
  }


  //if (!LittleFS.begin(FORMAT_LittleFS_IF_FAILED, "/lfs2",20)) {
  if (!LittleFS.begin(FORMAT_LittleFS_IF_FAILED)){
    DEBUG_PRINTLN(F("Erreur LittleFS"));
    return;
  }
  DEBUG_PRINTLN(F("LittleFS OK"));

  // Vérifier si RAZ demandée
  if (resetManager.checkForReset()) {
      
      blinkLed(10, 50);
      // Effacer la config
      clearWifiConfig();
      
      Serial.println("[RESET] Redémarrage...");
      delay(500);
      ESP.restart();
  }

  //if ( (!loadConfigWifi()) || (!loadConfigGeneral())) {
  if (!loadConfigGeneral()) {
      DEBUG_PRINTLN(F("Erreur Loadconfig LittleFS"));
  } else {
    configOK=true;    
    DEBUG_PRINTLN(F("Conf ok LittleFS"));
  }

  if (ConfigSettings.enableHWFlow)
  {
    log_e("Hardware Serial Flow control ON");
    if (!Serial1.setHwFlowCtrlMode(UART_HW_FLOWCTRL_CTS_RTS, 64)) {
        log_e("Failed enabling hardware flow control!");
    }
    // Test du contrôle de flux
    if (Serial1.availableForWrite() < 10) {
        log_e("UART buffer nearly full - flow control should activate");
    }
  }else{
    log_e("No Hardware Serial Flow control");
  }


  //Création des répertoire LittleFS (si nécessaire)
  if (!LittleFS.exists("/db"))
  {
    LittleFS.mkdir("/db");
  }
   if (!LittleFS.exists("/hst"))
  {
    LittleFS.mkdir("/hst");
  }
  if (!LittleFS.exists("/debug"))
  {
    LittleFS.mkdir("/debug");
  }
  if (!LittleFS.exists("/bk"))
  {
    LittleFS.mkdir("/bk");
  }
  if (!LittleFS.exists("/rt"))
  {
    LittleFS.mkdir("/rt");
  }
   if (!LittleFS.exists("/ota"))
  {
    LittleFS.mkdir("/ota");
  }
  
  //WifiReconnectTimer = xTimerCreate("WifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(reconnectWifi));

  log_w("datasTreatment - Core : %d - Heap size : %ld - Free heap : %ld - Free PSRAM: %ld - uxTaskGetStackHighWaterMark: %ld",xPortGetCoreID(),ESP.getHeapSize(),ESP.getFreeHeap(),ESP.getFreePsram(),uxTaskGetStackHighWaterMark(NULL));


  loadAllDevices("/db");

  log_w("datasTreatment - Core : %d - Heap size : %ld - Free heap : %ld - Free PSRAM: %ld - uxTaskGetStackHighWaterMark: %ld",xPortGetCoreID(),ESP.getHeapSize(),ESP.getFreeHeap(),ESP.getFreePsram(),uxTaskGetStackHighWaterMark(NULL));


  for (size_t i = 0; i < devices.size(); i++) {
    DeviceData* device = devices[i];
    String sa = device->getInfo().shortAddr;
    String power = device->getValue("0B04","1295");

    Serial.println("Device " + String(i) + " shortAddr = " + sa +" power = " + power);
  }

  
  // === NOUVEAU : VÉRIFICATION MÉMOIRE INITIALE ===
    Serial.println("📊 Initial Memory State:");
    Serial.printf("   Free Heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("   Free PSRAM: %u bytes\n", ESP.getFreePsram());
    Serial.printf("   Largest free block: %u bytes\n", ESP.getMaxAllocHeap());
    
    // === NOUVEAU : CONFIGURATION SMARTWIFI ===
    
    // Configurer les callbacks pour surveiller les changements d'état
    smartWiFi.onStateChange([](WiFiState state) {
        switch (state) {
            case WIFI_STATE_CONNECTED:
                Serial.println("🌐 ✅ WiFi connected - Services will start");
                addDebugLog(F("WiFi Connected"));
                wifiServicesStarted = false; // Trigger init in loop

                // Reconnexion MQTT automatique
                if (ConfigSettings.enableMqtt) {
                    Serial.println("📡 Reconnexion MQTT suite à WiFi...");
                    connectToMqtt();
                }

                break;
                
            case WIFI_STATE_BLE_PROVISIONING:
                Serial.println("📱 🔵 BLE Provisioning active");
                Serial.println("📱 Use mobile app to configure WiFi");
                break;
                
            case WIFI_STATE_DISCONNECTED:
                Serial.println("🌐 ❌ WiFi disconnected");
                addDebugLog(F("WiFi disconnected"));
                wifiServicesStarted = false;

                if (mqttClient.connected()) {
                    Serial.println("📡 Arrêt MQTT suite à perte WiFi");
                    mqttClient.disconnect();
                }
                break;
                
            case WIFI_STATE_FAILED:
                Serial.println("🌐 ⚠️ WiFi connection failed");
                break;
                
            case WIFI_STATE_CONNECTING:
                Serial.println("🌐 🔄 Connecting to WiFi...");
                break;
        }
    });
    
    smartWiFi.onEvent([](const String& event) {
        Serial.println("📡 Event: " + event);
    });

    // === DÉMARRAGE INTELLIGENT ===
    Serial.println("🔍 Starting Smart WiFi Manager...");
    Serial.println("    → Will check WiFi config automatically");
    Serial.println("    → Will load BLE only if needed");
    
    if (smartWiFi.begin()) {
        
        Serial.println("✅ Smart WiFi Manager started");

        smartWiFi.setAutoReconnect(true);           // Activer la reconnexion auto
        smartWiFi.setMaxReconnectAttempts(10);      // 10 tentatives avant BLE
        smartWiFi.setReconnectDelay(5000);          // 5 secondes entre tentatives
        
        // Afficher l'état après démarrage
        Serial.println("📊 Current state: " + smartWiFi.getStatusString());
        
        if (smartWiFi.isProvisioning()) {
            Serial.println("📱 Device in BLE provisioning mode");
            Serial.println("📱 Device name: " + smartWiFi.generateDeviceName("LIXEEBOX"));
            Serial.println("📱 Use Nordic UART app to connect");
            Serial.println("📱 Send: SSID|PASSWORD");


        } else if (smartWiFi.isConnected()) {
            Serial.println("🌐 WiFi already connected!");
            Serial.println("🌐 IP: " + smartWiFi.getLocalIP());
            Serial.println("🌐 SSID: " + smartWiFi.getSSID());
        }
        
    } else {
        Serial.println("❌ Smart WiFi Manager failed to start");
        return;
    }

    Serial.println("📊 Memory after WiFi Manager init:");
    Serial.printf("   Free Heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("   Free PSRAM: %u bytes\n", ESP.getFreePsram());
    
    Serial.println("✅ Setup complete - entering main loop");

    // Initialisation du manager de notifications
  if (!notificationManager.begin()) {
    Serial.println("Erreur: initialisation NotificationManager échouée");
    return;
  }

  // Configuration WebSocket tunnel
  // === CONFIGURATION WEBSOCKET PSRAM ===
  /*WebSocketTunnelConfig configWSTunnel = createOptimizedConfig();
  wsTunnel.onConnectionChange([](bool connected) {
      if (connected) {
          Serial.println("✅ Tunnel WebSocket actif");
      } else {
          Serial.println("❌ Tunnel WebSocket inactif");
      }
  });

  // URL du tunnel disponible
  wsTunnel.onConnected([](const String& deviceId, const String& url) {
      Serial.println("\n╔════════════════════════════════════════╗");
      Serial.println("║      TUNNEL ACTIF - ACCÈS DISTANT      ║");
      Serial.println("╠════════════════════════════════════════╣");
      Serial.printf("║ Device ID: %-28s ║\n", deviceId.c_str());
      Serial.printf("║ URL: https://%-28s ║\n", url.c_str());
      Serial.println("╚════════════════════════════════════════╝");
      
      // Optionnel: sauvegarder l'URL
      // File f = LittleFS.open("/tunnel_url.txt", "w");
      // f.println(url);
      // f.close();
  });
  
  // === DÉMARRER LE TUNNEL ===
  if (wsTunnel.begin(configWSTunnel)) {
      Serial.println("\n✅ WebSocket Tunnel initialisé");
      wsTunnel.printMemoryStats();
  } else {
      Serial.println("\n❌ Erreur initialisation tunnel");
      while(1) delay(1000);
  }*/

  // Configuration OpenAI
  /*OpenAIConfig configAI;
  configAI.api_key = OPENAI_API_KEY;
  configAI.model = "gpt-3.5-turbo";      // Ou "gpt-4o-mini" pour plus de qualité
  configAI.max_tokens = 500;
  configAI.temperature = 0.7;
  configAI.timeout_ms = 30000;
  configAI.use_compact_mode = false;     // true pour économiser des tokens
  
  // Initialisation de l'analyseur
  if (!ai_analyzer.begin(configAI)) {
      Serial.println("[ERREUR] Impossible d'initialiser l'analyseur IA");
      while(1) delay(1000);
  }
  
  // Test de connectivité OpenAI
  Serial.println("\n=== TEST CONNECTIVITÉ ===");
  if (!ai_analyzer.testConnection()) {
      Serial.println("\n[ATTENTION] Test de connexion échoué");
      Serial.println("L'analyse risque de ne pas fonctionner");
      Serial.println("Vérifiez votre clé API et la connexion réseau\n");
      // Vous pouvez choisir de continuer ou d'arrêter ici
  }*/
  
  //Serial.println("\n[OK] Système initialisé avec succès!\n");
  
  // Première analyse au démarrage
  //delay(2000);
  //performDetailedAnalysis();
 
}

void sendTreatment()
{
  if (!PrioritycommandList->isEmpty())
  {
    log_d("Priority send");
    sendZigbeeCmd(PrioritycommandList->shift());
    vTaskDelay(50);
  }
  if (!commandList->isEmpty())
  {
    sendZigbeeCmd(commandList->shift());
    vTaskDelay(100);
  }
  vTaskDelay(10 / portTICK_PERIOD_MS);

}

void monitorSerialSimple() {
    static uint32_t lastCheck = 0;
    uint32_t now = millis();
    
    if (now - lastCheck > 60000) { // Toutes les minutes
        size_t available = Serial1.available();
        
        log_i("📊 État série:");
        log_i("   Bytes en attente: %d", available);
        log_i("   Flow control: %s", ConfigSettings.enableHWFlow ? "ON" : "OFF");
        
        // Alerte si buffer série plein
        if (available > 3000) {
            log_e("🔴 Buffer série presque plein: %d bytes!", available);
            addDebugLog(F("Buffer série plein"));
            
            // ACTION D'URGENCE : forcer lecture
            uint8_t tempBuffer[100];
            while (available > 0 && available < 4000) {
                size_t read = Serial1.readBytes(tempBuffer, 100);
                if (read > 0) {
                    parseRawDataPersistent(tempBuffer, read);
                }
                available = Serial1.available();
            }
        }
        
        lastCheck = now;
    }
}

static uint32_t maxLoopTime = 0;

void loop(void)
{
  unsigned long loopStart = millis();
  esp_task_wdt_reset();

  // Gestion du compteur de boot
  resetManager.tick();

  smartWiFi.handleEvents();

  static uint32_t lastStatusPrint = 0;
  if (smartWiFi.isReconnecting() && (millis() - lastStatusPrint > 5000)) {
      lastStatusPrint = millis();
      addDebugLog("WIFI : Reconnexion en cours ...");
      Serial.printf("🔄 Reconnexion en cours: tentative %d/%d\n", 
                    smartWiFi.getReconnectAttempts(), 10);
  }

  if (smartWiFi.isConnected() && !wifiServicesStarted) {
      initWiFiServices();
      wifiServicesStarted = true;
  }

  // === INITIALISATION ZIGBEE (après services WiFi) ===
  if (wifiServicesStarted && !zigbeeInitialized) {
      setupZigbeeAndTasks();
      zigbeeInitialized = true;
  }

  // === VOTRE CODE PRINCIPAL (seulement si système initialisé) ===
  if (zigbeeInitialized) {
      #define min(a,b) ((a)<(b)?(a):(b))

      runner.execute();

      if (executeReboot)
      {
        executeReboot=false;
        runner.addTask(delayReboot);
        delayReboot.enableDelayed(1000);
      }

      if (updatePending) {
        updatePending = false;       // on n'exécute qu'une fois
        launchUpdateTask();
      }

      sendTreatment();

      printCrcStats();
      monitorSerialSimple();

      mqttAutoReconnect();

      monitor_heap();
  }

  /*// Appel OBLIGATOIRE (gère heartbeat + monitoring)
    wsTunnel.loop();
    
    // === MONITORING PÉRIODIQUE MANUEL (optionnel) ===
    static uint32_t lastCheck = 0;
    if (millis() - lastCheck > 60000) {
        lastCheck = millis();
        
        Serial.println("\n📊 Status:");
        Serial.printf("   Tunnel: %s\n", wsTunnel.isConnected() ? "✅" : "❌");
        Serial.printf("   PSRAM usage: %d KB\n", wsTunnel.getPSRAMUsage()/1024);
        Serial.printf("   DRAM usage: %d KB\n", wsTunnel.getDRAMUsage()/1024);
        
        // Optimiser si nécessaire
        if (wsTunnel.getPSRAMUsage() > 6*1024*1024) { // > 6MB
            Serial.println("🔧 Optimisation mémoire...");
            wsTunnel.optimizeMemory();
        }
    }
  */
  // DÉLAI ADAPTATIF pour éviter la surcharge CPU
  unsigned long loopTime = millis() - loopStart;
  
  // Surveillance du temps de boucle
  if (loopTime > maxLoopTime) {
      maxLoopTime = loopTime;
  }
  
  if (loopTime > 100) {
      log_w("Slow loop detected: %lu ms", loopTime);
  }
  
  // Délai adaptatif basé sur la charge système
  uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 100000) {
      vTaskDelay(pdMS_TO_TICKS(50)); // Plus de délai si mémoire faible
  } else if (loopTime > 50) {
      vTaskDelay(pdMS_TO_TICKS(20)); // Délai moyen si boucle lente
  } else {
      vTaskDelay(pdMS_TO_TICKS(10)); // Délai normal
  }

}




