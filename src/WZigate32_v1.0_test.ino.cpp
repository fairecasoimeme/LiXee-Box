/*
  ESP32 mDNS serial wifi bridge by Daniel Parnell 2nd of May 2015
 */
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

#include <driver/uart.h>
#include <lwip/ip_addr.h>
#include <esp_log.h>
#include "mail.h"
#include "AsyncUDP.h"


#include <TaskScheduler.h>

// application config
unsigned long timeLog;
ConfigSettingsStruct ConfigSettings;
ConfigGeneralStruct ConfigGeneral;
ZigbeeConfig ZConfig;
ZiGateInfosStruct ZiGateInfos;

CircularBuffer<Packet, 100> *commandList = nullptr;
CircularBuffer<Packet, 10> *PrioritycommandList = nullptr;
CircularBuffer<SerialPacket, 30> *PriorityQueuePacket = nullptr;
CircularBuffer<Alert, 10> *alertList = nullptr;
CircularBuffer<Device, 10> *deviceList = nullptr;
CircularBuffer<SerialPacket, 300> *QueuePacket = nullptr;
/*CircularBuffer<Packet, 100> commandList;
CircularBuffer<Packet, 10> PrioritycommandList;
CircularBuffer<SerialPacket, 30> PriorityQueuePacket;
CircularBuffer<Alert, 10> alertList;
CircularBuffer<SerialPacket, 100> QueuePacket;*/

//new(QueuePacket) CircularBuffer<SerialPacket,100>();
//QueuePacket = new CircularBuffer<SerialPacket,100>();

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

#define FORMAT_LittleFS_IF_FAILED true
//#define CONFIG_LITTLEFS_CACHE_SIZE 512

bool configOK=false;
String modeWiFi="STA";

extern int ZiGateMode;
extern int TimedFiFo;

#define BAUD_RATE 115200
#define TCP_LISTEN_PORT 9999

// if the bonjour support is turned on, then use the following as the name
#define DEVICE_NAME ""

// serial end ethernet buffer size
#define BUFFER_SIZE 4092

#define WL_MAC_ADDR_LENGTH 6

#ifdef BONJOUR_SUPPORT
// multicast DNS responder
MDNSResponder mdns;
#endif

unsigned long previousTime = 0; // Variable pour stocker le temps précédent
unsigned long interval = 60000; // Intervalle de 1 minute en millisecondes

//char timeServer[]="";
//char timeServer[] = "51.38.81.135";

//UDP Async
AsyncUDP UdpServer;

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

//#define CONFIG_ASYNC_TCP_RUNNING_CORE 0 //any available core
/*#define CONFIG_ASYNC_TCP_USE_WDT 1*/

SET_LOOP_TASK_STACK_SIZE(24*1024); // 24KB

//callback timer
void scanCallback();

void sendPacket();

Task scan(60000, TASK_FOREVER, &scanCallback);
Scheduler runner;

void initCircularBuffer()
{
  commandList = (CircularBuffer<Packet, 100>*) ps_malloc(sizeof(CircularBuffer<Packet, 100>));
  PrioritycommandList = (CircularBuffer<Packet, 10>*) ps_malloc(sizeof(CircularBuffer<Packet, 10>));
  PriorityQueuePacket= (CircularBuffer<SerialPacket, 30>*) ps_malloc(sizeof(CircularBuffer<SerialPacket, 30>));
  alertList = (CircularBuffer<Alert, 10>*) ps_malloc(sizeof(CircularBuffer<Alert, 10>));
  deviceList = (CircularBuffer<Device, 10>*) ps_malloc(sizeof(CircularBuffer<Device, 10>));
  QueuePacket = (CircularBuffer<SerialPacket,300>*) ps_malloc(sizeof(CircularBuffer<SerialPacket,300>));

  if (!QueuePacket || !commandList || !PrioritycommandList || !PriorityQueuePacket || !alertList || !deviceList) {
      DEBUG_PRINTLN("Erreur lors de l'allocation de CircularBuffer en PSRAM!");
      return;
  }
  // Initialiser le buffer en utilisant le constructeur de placement
  new (commandList) CircularBuffer<Packet, 100>();
  new (PrioritycommandList) CircularBuffer<Packet, 10>();
  new (PriorityQueuePacket) CircularBuffer<SerialPacket, 30>();
  new (alertList) CircularBuffer<Alert, 10>();
  new (deviceList) CircularBuffer<Device, 10>();
  new (QueuePacket) CircularBuffer<SerialPacket,300>();
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



}


String uart_buffer = "";
// a pause of a half second in the UART transmission is considered the end of transmission.
const uint32_t communicationTimeout_ms = 500;

// Create a mutex for the access to uart_buffer
// only one task can read/write it at a certain time
SemaphoreHandle_t uart_buffer_Mutex = NULL;
SemaphoreHandle_t file_Mutex = NULL;
SemaphoreHandle_t inifile_Mutex = NULL;
SemaphoreHandle_t Queue_Mutex = NULL;
SemaphoreHandle_t QueuePrio_Mutex = NULL;

void sendPacket()
{
  //DEBUG_PRINTLN("sendPacket: ");
  if (!PrioritycommandList->isEmpty())
  {
    log_d("Priority send");
    sendZigbeeCmd(PrioritycommandList->shift());
    vTaskDelay(30);
  }else{
    if (!commandList->isEmpty())
    {
      sendZigbeeCmd(commandList->shift());
      vTaskDelay(100);
    }
  }
  
}

// UART_RX_IRQ will be executed as soon as data is received by the UART
// This is a callback function executed from a high priority
// task created when onReceive() is used
uint8_t uartBuffer[BUFFER_SIZE];
size_t uartBufferLength;
void SERIAL_RX_CB() {
  // take the mutex, waits forever until loop() finishes its processing

  if (xSemaphoreTake(uart_buffer_Mutex, portMAX_DELAY)) {
    //while(Serial1.available() && (uart_buffer.length() < (BUFFER_SIZE /2)))

    if (Serial1.available())
    {
      uartBufferLength = Serial1.readBytes(uartBuffer,BUFFER_SIZE);
      /*for (size_t i = 0; i < bytesRead; i++) {
        uart_buffer+=buffer[i];
      }
      bytesRead = 0;*/
    }else if (Serial1.getWriteError()) {
      log_e("Erreur de lecture série !");
      Serial1.clearWriteError();
   }
   /*while(Serial1.available())
    {
      char tmp = (char)Serial1.read();
      uart_buffer += tmp;
    }
    Serial1.flush();*/
    xSemaphoreGive(uart_buffer_Mutex);
   
  }
}

TaskHandle_t taskTCP;

void TcpTreatment(void * pvParameters)
{
  AsyncClient *client;
  client =  (AsyncClient *)pvParameters;
  log_d("TcpTreatment\n");
  while (1)
  {
    esp_task_wdt_reset();

    const char* path ="/db/";
    const char* extension =".json";
    char name_with_extension[64];
    strcpy(name_with_extension,path);
    strcat(name_with_extension,ConfigGeneral.ZLinky);
    strcat(name_with_extension,extension);
    File DeviceFile = LittleFS.open(name_with_extension, FILE_READ);
    if (!DeviceFile|| DeviceFile.isDirectory()) {
      log_e("failed open");
      vTaskDelete(taskTCP);
    }else{
      JsonObject root;
      StaticJsonDocument<1024> filter;
      filter["0B04"]= true;
      DynamicJsonDocument temp(MAXHEAP);
      deserializeJson(temp,DeviceFile, DeserializationOption::Filter(filter));
      DeviceFile.close();
      root = temp["0B04"] ;
       String datas = "HM:";

      datas +=String(strtol(root["1295"].as<String>().c_str(),0,16));
      datas +="|";
      datas +=String(strtol(root["2319"].as<String>().c_str(),0,16));
      datas +="|";
      datas +=String(strtol(root["2575"].as<String>().c_str(),0,16));
      
      if (client->space() > strlen(datas.c_str()) && client->canSend())
      {
        log_i("send to Marstek infos : %s\n",datas);
        client->add(datas.c_str(), strlen(datas.c_str()));
        client->send();
      }
     
    }
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
    if (taskTCP!=NULL)
    {
      vTaskDelete(taskTCP);
    }
    log_d("create thread : %d\n",taskTCP);
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
  AsyncServer *TcpServer = new AsyncServer(12345); // start listening on tcp port 7050
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
      }
    });
  }

}

void datasTreatment(void * pvParameters)
{
  
  while(true)
  {
    esp_task_wdt_reset();
    while (!PriorityQueuePacket->isEmpty())
    {
      DEBUG_PRINTLN("Priority Packet shift : protocol datas");
      SerialPacket packet;
      xSemaphoreTake(QueuePrio_Mutex, portMAX_DELAY);
      packet = (SerialPacket)PriorityQueuePacket->shift();
      xSemaphoreGive(QueuePrio_Mutex);
      datasManage((char *)packet.raw,packet.len);
      vTaskDelay(30);
    }
    
    if (!QueuePacket->isEmpty())
    {   
      DEBUG_PRINTLN("Packet shift : protocol datas");
      SerialPacket packet;
      xSemaphoreTake(Queue_Mutex, portMAX_DELAY);
      packet = (SerialPacket)QueuePacket->shift();
      xSemaphoreGive(Queue_Mutex);
      datasManage((char *)packet.raw,packet.len);
      vTaskDelay(100);
    }
    
    sendPacket();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  vTaskDelete(NULL);

}


void SerialTask( void * pvParameters)
{
  size_t bytes_read;
  char output_sprintf[5];
  uint8_t packetRead[BUFFER_SIZE];
  while(true)
  {
    esp_task_wdt_reset();
    if (uartBufferLength>0)
    {
      //protocolDatas(packetRead);
      bytes_read=0;
      if (xSemaphoreTake(uart_buffer_Mutex, portMAX_DELAY)) {
        memcpy(packetRead,uartBuffer,BUFFER_SIZE);
        bytes_read = uartBufferLength; 
        memset(uartBuffer,0,0);
        uartBufferLength=0;
        xSemaphoreGive(uart_buffer_Mutex);
      }  
      protocolDatas(packetRead,bytes_read);
      
      for (int i=0;i<bytes_read;i++)
      {
        char tmpP[4];
        snprintf(tmpP,3, "%02X",packetRead[i]);
        DEBUG_PRINT(tmpP);
        DEBUG_PRINT(F(" "));  
        if (packetRead[i] == 0x03){DEBUG_PRINTLN();}

      }                                                  
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);

  }
  
  vTaskDelete(NULL);
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

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
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
      String AP_NameString = "LIXEEGW-" + macID;

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
}

const char * pop = ""; // Proof of possession - otherwise called a PIN - string provided by the device, entered by the user in the phone app
const char * service_name = "LIXEE_GW"; // Name of your device (the Espressif apps expects by default device name starting with "Prov_")
const char * service_key = NULL; // Password used for SofAP method (NULL = no password needed)
bool reset_provisioned = true; // When true the library will automatically delete previously provisioned data.

void SysProvEvent(arduino_event_t *sys_event)
{
  switch (sys_event->event_id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("\nConnected IP address : ");
      Serial.println(IPAddress(sys_event->event_info.got_ip.ip_info.ip.addr));
      ConfigSettings.connectedWifiSta = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: Serial.println("\nDisconnected. Connecting to the AP again... "); ConfigSettings.connectedWifiSta = false;break;
    case ARDUINO_EVENT_PROV_START:            Serial.println("\nProvisioning started\nGive Credentials of your access point using smartphone app"); break;
    case ARDUINO_EVENT_PROV_CRED_RECV:
    {
      Serial.println("\nReceived Wi-Fi credentials");
      Serial.print("\tSSID : ");
      Serial.println((const char *)sys_event->event_info.prov_cred_recv.ssid);
      Serial.print("\tPassword : ");
      Serial.println((char const *)sys_event->event_info.prov_cred_recv.password);
      const char *path = "configWifi.json";
      strcpy(ConfigSettings.ssid,(const char *)sys_event->event_info.prov_cred_recv.ssid);
      config_write(path,"ssid",String(ConfigSettings.ssid));
      strcpy(ConfigSettings.password,(const char *)sys_event->event_info.prov_cred_recv.password);
      config_write(path,"pass",String(ConfigSettings.password));
      
      
      break;
    }
    case ARDUINO_EVENT_PROV_CRED_FAIL:
    {
      Serial.println("\nProvisioning failed!\nPlease reset to factory and retry provisioning\n");
      if (sys_event->event_info.prov_fail_reason == WIFI_PROV_STA_AUTH_ERROR) {
        Serial.println("\nWi-Fi AP password incorrect");
      } else {
        Serial.println("\nWi-Fi AP not found....Add API \" nvs_flash_erase() \" before beginProvision()");
      }
      ConfigSettings.connectedWifiSta = false;
      break;
    }
    case ARDUINO_EVENT_PROV_CRED_SUCCESS: Serial.println("\nProvisioning Successful");  break;
    case ARDUINO_EVENT_PROV_END:          Serial.println("\nProvisioning Ends"); break;
    default:                              break;
  }
}

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {

    case ARDUINO_EVENT_WIFI_READY:
      addDebugLog(F("WiFi Ready"));
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
}



void onMqttConnect(bool sessionPresent) {
  DEBUG_PRINTLN(F("Connected to MQTT."));
  DEBUG_PRINT(F("Session present: "));
  DEBUG_PRINTLN(sessionPresent);

}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  DEBUG_PRINT(F("Disconnected from MQTT."));
  DEBUG_PRINTLN((uint8_t)reason);
  addDebugLog("Disconnected from mqtt : "+String((uint8_t)reason));

  if ((uint8_t)reason==0)
  {
    esp_restart();
  }

  if (WiFi.isConnected()) {
    xTimerStart(mqttReconnectTimer, 0);
  }
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
  DEBUG_PRINT(packetId);
}

void initTempSensor(){
    temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
    temp_sensor.dac_offset = TSENS_DAC_L2;  // TSENS_DAC_L2 is default; L4(-40°C ~ 20°C), L2(-10°C ~ 80°C), L1(20°C ~ 100°C), L0(50°C ~ 125°C)
    temp_sensor_set_config(temp_sensor);
    temp_sensor_start();
}

bool loadConfigWifi() {
  const char * path = "/config/configWifi.json";
  
  File configFile = LittleFS.open(path, FILE_READ);
  if (!configFile) {
    DEBUG_PRINTLN(F("failed open"));
    configFile.close();
    return false;
  }

  DynamicJsonDocument doc(10192);
  deserializeJson(doc,configFile);

  // affectation des valeurs , si existe pas on place une valeur par defaut
  ConfigSettings.enableWiFi = (int)doc["enableWiFi"];
  ConfigSettings.enableDHCP = (int)doc["enableDHCP"];
  strlcpy(ConfigSettings.ssid, doc["ssid"] | "", sizeof(ConfigSettings.ssid));
  strlcpy(ConfigSettings.password, doc["pass"] | "", sizeof(ConfigSettings.password));
  strlcpy(ConfigSettings.ipAddressWiFi, doc["ip"] | "", sizeof(ConfigSettings.ipAddressWiFi));
  strlcpy(ConfigSettings.ipMaskWiFi, doc["mask"] | "", sizeof(ConfigSettings.ipMaskWiFi));
  strlcpy(ConfigSettings.ipGWWiFi, doc["gw"] | "", sizeof(ConfigSettings.ipGWWiFi));
  ConfigSettings.tcpListenPort = TCP_LISTEN_PORT;
  ConfigSettings.enableWiFi = (int)doc["enableWiFi"];

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

  DynamicJsonDocument doc(10192);
  deserializeJson(doc,configFile);

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
  
  configFile.close();
  return true;
}

void setupWifiAP()
{
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
   
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.softAPmacAddress(mac);
  String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                 String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  macID.toUpperCase();
  String AP_NameString = "LIXEEGW-" + macID;

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

void reconnectWifi()
{
  DEBUG_PRINT(F("reconnect to WiFi..."));
  DEBUG_PRINTLN(ConfigSettings.ssid);
  addDebugLog("reconnect to WiFi...");
  WiFi.disconnect();
  if (WiFi.getMode() == WIFI_MODE_STA)
  {
    if (ConfigSettings.ssid != "")
    {
      WiFi.begin(ConfigSettings.ssid,ConfigSettings.password);
      addDebugLog("WiFi begin...");
    }
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

void setup(void)
{  
  
  ZiGateMode=PRODUCTION;
  initTempSensor();
 
  Serial.begin(115200, SERIAL_8N1);
  DEBUG_PRINTLN(F("Start"));
  Serial1.setRxBufferSize(4092);
  //Serial1.setRxFIFOFull(128);
  //Serial1.setTimeout(0x03);
  Serial1.begin(BAUD_RATE, SERIAL_8N1, RXD2, TXD2);
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
  Serial1.onReceive(SERIAL_RX_CB);  // sets the callback function
  delay(2000);
  DEBUG_PRINTLN(F("Send data to UART0 in order to activate the RX callback"));

  initCircularBuffer();
 /* delay(1000);
// creates a mutex object to control access to files
 file_Mutex = xSemaphoreCreateMutex();
  if (file_Mutex == NULL) {
    DEBUG_PRINTLN(F("Error creating Mutex. Sketch will fail."));
    while (true) {
      DEBUG_PRINTLN(F("Mutex error (NULL). Program halted."));
      delay(1000);
    }
  }

  // creates a mutex object to control access to files
  inifile_Mutex = xSemaphoreCreateMutex();
  if (inifile_Mutex == NULL) {
    DEBUG_PRINTLN(F("Error creating Mutex. Sketch will fail."));
    while (true) {
      DEBUG_PRINTLN(F("Mutex error (NULL). Program halted."));
      delay(1000);
    }
  }*/

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
  if ( (!loadConfigWifi()) || (!loadConfigGeneral())) {
      DEBUG_PRINTLN(F("Erreur Loadconfig LittleFS"));
  } else {
    configOK=true;    
    DEBUG_PRINTLN(F("Conf ok LittleFS"));
  }

  //Création des répertoire LittleFS (si nécessaire)
  if (!LittleFS.exists("/db"))
  {
    LittleFS.mkdir("/db");
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
  
  WifiReconnectTimer = xTimerCreate("WifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(reconnectWifi));
 
  if (configOK)
  {
    DEBUG_PRINTLN(F("configOK"));  
    if (!setupSTAWifi())
    {
      DEBUG_PRINTLN(F("AP"));
      setupWifiAP();
      modeWiFi="AP";
      /* BLE PROVISIONING */
      /*#if CONFIG_BLUEDROID_ENABLED && !defined(USE_SOFT_AP)
        Serial.println("Begin Provisioning using BLE");

        // Sample uuid that user can pass during provisioning using BLE
        uint8_t uuid[16] = {0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf, 0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02};
        WiFiProv.beginProvision(
          WIFI_PROV_SCHEME_BLE, WIFI_PROV_SCHEME_HANDLER_FREE_BLE, WIFI_PROV_SECURITY_1, pop, service_name, service_key, uuid, reset_provisioned
        );
      #endif*/
    }
    DEBUG_PRINTLN(F("setupSTAWifi"));   
  }else{
    
    setupWifiAP();
    modeWiFi="AP";
    DEBUG_PRINTLN(F("AP"));
  }
  
  WiFi.onEvent(WiFiEvent);

  /* BLE PROVISIONING */
 // WiFi.onEvent(SysProvEvent);
  WiFi.onEvent(WiFiStationDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  
   //Zeroconf
  String localdns = "lixee-gw";
  if(!MDNS.begin(localdns.c_str())) {
     DEBUG_PRINTLN("Error starting mDNS");
     //return;
  }

  if (ConfigSettings.enableMqtt)
  {
    DEBUG_PRINTLN(F("enableMqtt"));
    mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onSubscribe(onMqttSubscribe);
    mqttClient.onUnsubscribe(onMqttUnsubscribe);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.onPublish(onMqttPublish);

    mqttClient.setServer(ConfigGeneral.servMQTT, atoi(ConfigGeneral.portMQTT));
    mqttClient.setClientId(ConfigGeneral.clientIDMQTT);
    if (String(ConfigGeneral.userMQTT) !="")
    {
      mqttClient.setCredentials(ConfigGeneral.userMQTT, ConfigGeneral.passMQTT);
    } 

    mqttClient.connect();
  }

  esp_task_wdt_reset();
  
  timeClient.setPoolServerName((const char*)ConfigGeneral.ntpserver);
  timeClient.setTimeOffset((3600 * ConfigGeneral.timeoffset));
  timeClient.setTimeZone(ConfigGeneral.timezone);
  timeClient.setUpdateInterval(NTP_UPDATE_INTERVAL_MS);
  vTaskDelay(2000);
  
  initWebServer();
  //server.begin();
  vTaskDelay(2000);

  //Verification empty files
  scanFilesError();

  pinMode(RESET_ZIGATE, OUTPUT);
  pinMode(FLASH_ZIGATE, OUTPUT);
  pinMode(LED1, OUTPUT);
  digitalWrite(LED1, 1);
  digitalWrite(FLASH_ZIGATE, 1);
  digitalWrite(RESET_ZIGATE, 0);
  digitalWrite(RESET_ZIGATE, 1);
  DEBUG_PRINTLN(F("add networkState"));
  commandList->push(Packet{0x0011, 0x0000,0});
  commandList->push(Packet{0x0009, 0x0000,0});
  vTaskDelay(2000);
  DEBUG_PRINTLN(F("start network"));
  commandList->push(Packet{0x0024, 0x0000,0});
  DEBUG_PRINTLN(F("add getversion"));
  commandList->push(Packet{0x0010, 0x0000,0});
  DEBUG_PRINTLN(F("add networkState"));
  commandList->push(Packet{0x0025, 0x0000,0});
 esp_task_wdt_reset();
  timeClient.begin();
  bool NTPOK = timeClient.forceUpdate();
  log_e("NTPOK : %d\r\n",NTPOK);

 

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
  
  if ((ConfigSettings.enableMarstek) && (ConfigGeneral.ZLinky!=""))
  {
    udpProcess();
    tcpProcess();
  }

  //esp_task_wdt_init(15, true);
 // esp_task_wdt_add(NULL);

 // while (true)
 //   ;

runner.init();
runner.addTask(scan);
scan.enableDelayed(60000);
esp_task_wdt_reset();

 xTaskCreatePinnedToCore(
                    SerialTask,   // Function to implement the task 
                    "SerialTask", // Name of the task 
                    32*1024,      // Stack size in words 
                    NULL,       // Task input parameter 
                    17,          // Priority of the task 
                    NULL,       // Task handle. 
                    1);  

  xTaskCreatePinnedToCore(
                    datasTreatment,   // Function to implement the task 
                    "datasTreatment", // Name of the task 
                    64*1024,      // Stack size in words 
                    NULL,       // Task input parameter 
                    18,          // Priority of the task 
                    NULL,       // Task handle. 
                    0);



  //initMailClient();
  disableCore0WDT();


}

WiFiClient client;

void loop(void)
{
  esp_task_wdt_reset();

  runner.execute();
  size_t bytes_read;
  unsigned long currentTime = millis();
  //char output_sprintf[5];
  //String packetRead;
  uint8_t packetRead[BUFFER_SIZE];
  
  #define min(a,b) ((a)<(b)?(a):(b))
  
  /*if (uartBufferLength>0)
  {
    bytes_read=0;
    if (xSemaphoreTake(uart_buffer_Mutex, portMAX_DELAY)) {
      memcpy(packetRead,uartBuffer,BUFFER_SIZE);
      bytes_read = uartBufferLength; 
      memset(uartBuffer,0,0);
      uartBufferLength=0;
      xSemaphoreGive(uart_buffer_Mutex);
    }  
    protocolDatas(packetRead,bytes_read);
    
    for (int i=0;i<bytes_read;i++)
    {
      char tmpP[4];
      snprintf(tmpP,3, "%02X",packetRead[i]);
      DEBUG_PRINT(tmpP);
      DEBUG_PRINT(F(" "));  
      if (packetRead[i] == 0x03){DEBUG_PRINTLN();}
    }                                                  
  }*/

  /*while (!PriorityQueuePacket->isEmpty())
  {
    DEBUG_PRINTLN("Priority Packet shift : protocol datas");
    SerialPacket packet;
    packet = (SerialPacket)PriorityQueuePacket->shift();
    datasManage((char *)packet.raw,packet.len);
    vTaskDelay(30);
  }
  
  if (!QueuePacket->isEmpty())
  {   
    DEBUG_PRINTLN("Packet shift : protocol datas");
    SerialPacket packet;
    packet = (SerialPacket)QueuePacket->shift();
    datasManage((char *)packet.raw,packet.len);
    vTaskDelay(100);
  }
  
  sendPacket();*/
  monitor_heap();

  /*if (currentTime - previousTime >= interval) 
  {
    previousTime = currentTime; // Mettre à jour le temps précédent
  
    xTaskCreatePinnedToCore(
                    scanCallback,   // Function to implement the task 
                    "scanCallback", // Name of the task 
                    8*1024,      // Stack size in words 
                    NULL,       // Task input parameter 
                    5,          // Priority of the task 
                    NULL,       // Task handle. 
                    0);  
    
  }*/

  //vTaskDelay(10);
}




