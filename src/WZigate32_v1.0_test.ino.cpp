/*
  ESP32 mDNS serial wifi bridge by Daniel Parnell 2nd of May 2015
 */
#define BONJOUR_SUPPORT

//#include "soc/rtc_wdt.h"
#include "esp32s3/rom/rtc.h"
#include "driver/temp_sensor.h"
#include <esp_task_wdt.h>
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

#include <TaskScheduler.h>

//modbus
#include <ModbusRTUSlave.h>
uint16_t holdingRegisters[24600];

ModbusRTUSlave modbus(Serial2, 14);

#include <ETH.h>
#ifdef ETH_CLK_MODE
#undef ETH_CLK_MODE
#endif
#define ETH_CLK_MODE  ETH_CLOCK_GPIO0_IN //ETH_CLOCK_GPIO17_OUT //

// Pin# of the enable signal for the external crystal oscillator (-1 to disable for internal APLL source)
#define ETH_POWER_PIN    5//-1//5

// Type of the Ethernet PHY (LAN8720 or TLK110)
#define ETH_TYPE        ETH_PHY_LAN8720

// I²C-address of Ethernet PHY (0 or 1 for LAN8720, 31 for TLK110)
#define ETH_ADDR      1

// Pin# of the I²C clock signal for the Ethernet PHY
#define ETH_MDC_PIN     23

// Pin# of the I²C IO signal for the Ethernet PHY
#define ETH_MDIO_PIN    18

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

bool ETH_ENABLE;

//#define CONFIG_ASYNC_TCP_RUNNING_CORE 0 //any available core
/*#define CONFIG_ASYNC_TCP_USE_WDT 1*/

SET_LOOP_TASK_STACK_SIZE(24*1024); // 32KB


//callback timer
void scanCallback();
void datasTreatment();
void sendPacket();

Task scan(60000, TASK_FOREVER, &scanCallback);
Scheduler runner;


void initCircularBuffer()
{
  commandList = (CircularBuffer<Packet, 100>*) ps_malloc(sizeof(CircularBuffer<Packet, 100>));
  PrioritycommandList = (CircularBuffer<Packet, 10>*) ps_malloc(sizeof(CircularBuffer<Packet, 10>));
  PriorityQueuePacket= (CircularBuffer<SerialPacket, 30>*) ps_malloc(sizeof(CircularBuffer<SerialPacket, 30>));
  alertList = (CircularBuffer<Alert, 10>*) ps_malloc(sizeof(CircularBuffer<Alert, 10>));
  QueuePacket = (CircularBuffer<SerialPacket,300>*) ps_malloc(sizeof(CircularBuffer<SerialPacket,300>));

  if (!QueuePacket || !commandList || !PrioritycommandList || !PriorityQueuePacket || !alertList) {
      DEBUG_PRINTLN("Erreur lors de l'allocation de CircularBuffer en PSRAM!");
      return;
  }
  // Initialiser le buffer en utilisant le constructeur de placement
  new (commandList) CircularBuffer<Packet, 100>();
  new (PrioritycommandList) CircularBuffer<Packet, 10>();
  new (PriorityQueuePacket) CircularBuffer<SerialPacket, 30>();
  new (alertList) CircularBuffer<Alert, 10>();
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
  //gestion des arrets de transmissions sur historisation
  DEBUG_PRINTLN(F("ScanDeviceToRAZ"));
  ScanDeviceToRAZ();


}


String uart_buffer = "";
// a pause of a half second in the UART transmission is considered the end of transmission.
const uint32_t communicationTimeout_ms = 500;

// Create a mutex for the access to uart_buffer
// only one task can read/write it at a certain time
SemaphoreHandle_t uart_buffer_Mutex = NULL;
SemaphoreHandle_t file_Mutex = NULL;
SemaphoreHandle_t inifile_Mutex = NULL;

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
      vTaskDelay(30);
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


void SerialTask( void * pvParameters)
{
  size_t bytes_read;
  char output_sprintf[5];
  uint8_t packetRead[BUFFER_SIZE];
  while(true)
  {
    if (uartBufferLength>0)
    {
      /*if (xSemaphoreTake(uart_buffer_Mutex, portMAX_DELAY)) {
        packetRead= uart_buffer.c_str();
        uart_buffer="";
        xSemaphoreGive(uart_buffer_Mutex);
      }*/
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

        /*sprintf(output_sprintf,"%02x",packetRead[i]);
        if(packetRead[i]==0x01)
        {
          //String tmpTime; 
          String buff="";
          //timeLog = millis();
          //tmpTime = String(timeLog,DEC);
          logPush('[');
          for (int j =0;j<FormattedDate.length();j++)
          {
            
            logPush(FormattedDate[j]);
          }
          logPush(']');
          logPush('<');
          logPush('-');
        }
        logPush(' ');
        logPush(output_sprintf[0]);
        logPush(output_sprintf[1]);
        if (packetRead[i]==0x03)
        {
          logPush('\n');
        }*/
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
    mqttClient.connect();
  }

}

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {

    case ARDUINO_EVENT_WIFI_READY:
      addDebugLog(F("WiFi Ready"));
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      addDebugLog(F("WiFi Connected"));
      if (WifiReconnectTimer != NULL)
      {
        xTimerStop(WifiReconnectTimer, 0); 
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      addDebugLog(F("WiFi Disconnected"));
      if (mqttReconnectTimer != NULL)
      {
        xTimerStop(mqttReconnectTimer, 0); 
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      addDebugLog(F("WiFi LOST IP"));
      xTimerStart(WifiReconnectTimer, 0);
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      connectToMqtt();
      break;
    case ARDUINO_EVENT_ETH_START:
      DEBUG_PRINTLN(F("ETH Started"));
      //set eth hostname here
      ETH.setHostname("esp32-ethernet");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      //DEBUG_PRINTLN("ETH Connected");
      
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      DEBUG_PRINT(F("ETH MAC: "));
      DEBUG_PRINT(ETH.macAddress());
      DEBUG_PRINT(F(", IPv4: "));
      DEBUG_PRINT(ETH.localIP());
      if (ETH.fullDuplex()) {
        DEBUG_PRINT(F(", FULL_DUPLEX"));
      }
      DEBUG_PRINT(", ");
      DEBUG_PRINT(ETH.linkSpeed());
      DEBUG_PRINTLN(F("Mbps"));
      ConfigSettings.connectedEther=true;
      timeClient.forceUpdate();
      connectToMqtt();
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      //DEBUG_PRINTLN("ETH Disconnected");
      ConfigSettings.connectedEther=false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      DEBUG_PRINTLN(F("ETH Stopped"));
      ConfigSettings.connectedEther=false;
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

bool loadConfigEther() {
  const char * path = "/config/configEther.json";
  
  File configFile = LittleFS.open(path, FILE_READ);
  if (!configFile) {
    DEBUG_PRINTLN(F("failed open"));
    return false;
  }

  DynamicJsonDocument doc(10192);
  deserializeJson(doc,configFile);

  // affectation des valeurs , si existe pas on place une valeur par defaut
  ConfigSettings.enableEthernet = (int)doc["enable"];
  ConfigSettings.dhcp = (int)doc["dhcp"];
  strlcpy(ConfigSettings.ipAddress, doc["ip"] | "", sizeof(ConfigSettings.ipAddress));
  strlcpy(ConfigSettings.ipMask, doc["mask"] | "", sizeof(ConfigSettings.ipMask));
  strlcpy(ConfigSettings.ipGW, doc["gw"] | "", sizeof(ConfigSettings.ipGW));
  configFile.close();

  if (ConfigSettings.enableEthernet)
  {
    ETH_ENABLE=true;
  }else{
    ETH_ENABLE=false;
  }
  
  
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
  strlcpy(ConfigGeneral.portMQTT, doc["portMQTT"] | "", sizeof(ConfigGeneral.portMQTT));
  strlcpy(ConfigGeneral.userMQTT, doc["userMQTT"] | "", sizeof(ConfigGeneral.userMQTT));
  strlcpy(ConfigGeneral.passMQTT, doc["passMQTT"] | "", sizeof(ConfigGeneral.passMQTT));
  strlcpy(ConfigGeneral.headerMQTT, doc["headerMQTT"] | "", sizeof(ConfigGeneral.headerMQTT));

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

bool setupSTAWifi() {
  
  WiFi.mode(WIFI_STA);
  DEBUG_PRINTLN(F("WiFi.mode(WIFI_STA)"));
  WiFi.disconnect();
  DEBUG_PRINTLN(F("disconnect"));
  vTaskDelay(10);

  WiFi.begin(ConfigSettings.ssid, ConfigSettings.password);
  WiFi.setSleep(false);
  DEBUG_PRINTLN(F("WiFi.begin"));

  IPAddress ip_address = parse_ip_address(ConfigSettings.ipAddressWiFi);
  IPAddress gateway_address = parse_ip_address(ConfigSettings.ipGWWiFi);
  IPAddress netmask = parse_ip_address(ConfigSettings.ipMaskWiFi);
  IPAddress primaryDNS(8, 8, 8, 8);   //optional
  IPAddress secondaryDNS(8, 8, 4, 4); //optional
  if (!ConfigSettings.enableDHCP)
  {
    WiFi.config(ip_address, gateway_address, netmask, primaryDNS,secondaryDNS);
  }
  DEBUG_PRINTLN(F("WiFi.config"));


  int countDelay=20;
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
}

void reconnectWifi()
{
  DEBUG_PRINT(F("reconnect to WiFi..."));
  setupSTAWifi();
  
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
  delay(1000);
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
    loadConfigEther();
    
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
  
  WiFi.onEvent(WiFiEvent);
  WifiReconnectTimer = xTimerCreate("WifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(reconnectWifi));

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
    if (String(ConfigGeneral.userMQTT) !="")
    {
      mqttClient.setCredentials(ConfigGeneral.userMQTT, ConfigGeneral.passMQTT);
    }
    
    
  }


  if (ETH_ENABLE)
  { 
    bool ETHReady = ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_TYPE, ETH_CLK_MODE);

    if (ConfigSettings.enableWiFi || (!ETHReady))
    {
      if (configOK)
      {
        DEBUG_PRINTLN(F("configOK"));  
        if (!setupSTAWifi())
        {
          DEBUG_PRINTLN(F("AP"));
          setupWifiAP();
          modeWiFi="AP";
        }
        DEBUG_PRINTLN(F("setupSTAWifi"));   
      }else{
        
        setupWifiAP();
        modeWiFi="AP";
        DEBUG_PRINTLN(F("AP"));
      }
    }
  } else{
    if (configOK)
    {
      DEBUG_PRINTLN(F("configOK"));  
      if (!setupSTAWifi())
      {
        DEBUG_PRINTLN(F("AP"));
        setupWifiAP();
        modeWiFi="AP";
      }
      DEBUG_PRINTLN(F("setupSTAWifi"));   
    }else{
      
      setupWifiAP();
      modeWiFi="AP";
      DEBUG_PRINTLN(F("AP"));
    }
  }

  
  if (ETH_ENABLE)
  {  
    if (!ConfigSettings.dhcp)
    {
      ETH.config(parse_ip_address(ConfigSettings.ipAddress), parse_ip_address(ConfigSettings.ipGW),parse_ip_address(ConfigSettings.ipMask),parse_ip_address(ConfigSettings.ipGW));
      
    }

  }
   //Zeroconf
  String localdns = "lixee-box";
  if(!MDNS.begin(localdns.c_str())) {
     DEBUG_PRINTLN("Error starting mDNS");
     //return;
  }

  

  timeClient.setPoolServerName((const char*)ConfigGeneral.ntpserver);
  timeClient.setTimeOffset((3600 * ConfigGeneral.timeoffset));
  timeClient.setTimeZone(ConfigGeneral.timezone);
  timeClient.setUpdateInterval(NTP_UPDATE_INTERVAL_MS);

  
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

  timeClient.begin();
  bool NTPOK = timeClient.forceUpdate();
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
  
  //esp_task_wdt_init(15, true);
 // esp_task_wdt_add(NULL);

 // while (true)
 //   ;

  runner.init();
  runner.addTask(scan);
  scan.enableDelayed(60000);


 xTaskCreatePinnedToCore(
                    SerialTask,   // Function to implement the task 
                    "SerialTask", // Name of the task 
                    32*1024,      // Stack size in words 
                    NULL,       // Task input parameter 
                    17,          // Priority of the task 
                    NULL,       // Task handle. 
                    0);  

  //initMailClient();
  disableCore0WDT();
  pinMode(45,OUTPUT);
  digitalWrite(45, HIGH);

  //MODBUS
  /*Serial2.begin(9600,rxPinModbus, txPinModbus);
  ModbusRTUSlave modbus(Serial2, bufModbus, bufModbusSize,12);
  modbus.begin(id, baud);
  modbus.configureHoldingRegisters(numHoldingRegisters , (WordRead) holdingRegisterRead, (WordWrite) holdingRegisterWrite);*/
  Serial2.setPins(10,11);
  modbus.configureHoldingRegisters(holdingRegisters, 24600);
  modbus.begin(1, 9600);

}

WiFiClient client;

void loop(void)
{
  esp_task_wdt_reset();
  modbus.poll();
  holdingRegisters[0x4000]=666;
  holdingRegisters[0x4001]=33;


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
   




  while (!PriorityQueuePacket->isEmpty())
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
  
  sendPacket();
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


