/*
  ESP32 mDNS serial wifi bridge by Daniel Parnell 2nd of May 2015
 */
#define BONJOUR_SUPPORT

#include "soc/rtc_wdt.h"
#include <esp_task_wdt.h>
#include <WiFi.h>
#ifdef BONJOUR_SUPPORT
#include <ESPmDNS.h>
#endif
//#include <NTPClient_Generic.h>
#include <time.h>
#include "NTPClient.h"
#include <WiFiUdp.h>

#include <WiFiClient.h>
#include <esp_wifi.h>

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


#include <ETH.h>
#ifdef ETH_CLK_MODE
#undef ETH_CLK_MODE
#endif
#define ETH_CLK_MODE  ETH_CLOCK_GPIO0_IN //ETH_CLOCK_GPIO17_OUT //
/* 
   * ETH_CLOCK_GPIO0_IN   - default: external clock from crystal oscillator
   * ETH_CLOCK_GPIO0_OUT  - 50MHz clock from internal APLL output on GPIO0 - possibly an inverter is needed for LAN8720
   * ETH_CLOCK_GPIO16_OUT - 50MHz clock from internal APLL output on GPIO16 - possibly an inverter is needed for LAN8720
   * ETH_CLOCK_GPIO17_OUT - 50MHz clock from internal APLL inverted output on GPIO17 - tested with LAN8720
*/
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

static bool eth_connected = false;


// application config
unsigned long timeLog;
ConfigSettingsStruct ConfigSettings;
ConfigGeneralStruct ConfigGeneral;
ZigbeeConfig ZConfig;
ZiGateInfosStruct ZiGateInfos;
//sqlite3 *ZiGateDb;

CircularBuffer<Packet, 20> commandList;
CircularBuffer<Packet, 10> commandTimedList;
CircularBuffer<Alert, 10> alertList;

//CircularBuffer<SQLReq, 5> SQLReqList;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

#define FORMAT_LittleFS_IF_FAILED true
#define CONFIG_LITTLEFS_CACHE_SIZE 512

bool configOK=false;
String modeWiFi="STA";

extern int ZiGateMode;
extern int TimedFiFo;

#define BAUD_RATE 115200
#define TCP_LISTEN_PORT 9999

// if the bonjour support is turned on, then use the following as the name
#define DEVICE_NAME ""

// serial end ethernet buffer size
#define BUFFER_SIZE 1024

#define WL_MAC_ADDR_LENGTH 6

#ifdef BONJOUR_SUPPORT
// multicast DNS responder
MDNSResponder mdns;
#endif

unsigned long previousTime = 0; // Variable pour stocker le temps précédent
unsigned long interval = 60000; // Intervalle de 1 minute en millisecondes

//char timeServer[]="";
//char timeServer[] = "51.38.81.135";


#define NTP_UPDATE_INTERVAL_MS          60000L



String FormattedDate;
String Hour;
String Day;
String Month;
String Year;
String Minute;
String Yesterday;

bool ETH_ENABLE;

/*#define CONFIG_ASYNC_TCP_RUNNING_CORE 1 //any available core
#define CONFIG_ASYNC_TCP_USE_WDT 0
#define configMINIMAL_STACK_SIZE                1024*/
SET_LOOP_TASK_STACK_SIZE(16*1024); // 16KB

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
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

//WiFiServer server(TCP_LISTEN_PORT);

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

bool loadConfigWifi() {
  const char * path = "/config/configWifi.json";
  
  File configFile = LittleFS.open(path, FILE_READ);
  if (!configFile) {
    DEBUG_PRINTLN(F("failed open"));
    return false;
  }

  DynamicJsonDocument doc(1024);
  deserializeJson(doc,configFile);

  // affectation des valeurs , si existe pas on place une valeur par defaut
  ConfigSettings.enableWiFi = (int)doc["enableWiFi"];
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

  DynamicJsonDocument doc(1024);
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

  DynamicJsonDocument doc(5096);
  deserializeJson(doc,configFile);

  ConfigSettings.enableDebug = (int)doc["enableDebug"];
  if (ConfigSettings.enableDebug)
  {
    #define DEBUG_PRINT(x)  Serial.print(x) 
    #define DEBUG_PRINTLN(x)  Serial.println(x)
    #define DEBUG_ON
  }else{
    #undef DEBUG_ON
   /* #define DEBUG_PRINT(x) 
    #define DEBUG_PRINTLN(x) */
  }
  strlcpy(ConfigGeneral.ZLinky, doc["ZLinky"] | "", sizeof(ConfigGeneral.ZLinky));

  ConfigGeneral.LinkyMode = ini_read(String(ConfigGeneral.ZLinky)+".json","FF06", "768").toInt();
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

  ConfigSettings.enableNotif = (int)doc["enableNotif"];
  strlcpy(ConfigGeneral.servSMTP, doc["servSMTP"] | "", sizeof(ConfigGeneral.servSMTP));
  strlcpy(ConfigGeneral.portSMTP, doc["portSMTP"] | "", sizeof(ConfigGeneral.portSMTP));
  strlcpy(ConfigGeneral.userSMTP, doc["userSMTP"] | "", sizeof(ConfigGeneral.userSMTP));
  strlcpy(ConfigGeneral.passSMTP, doc["passSMTP"] | "0", sizeof(ConfigGeneral.passSMTP));
  
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
  String AP_NameString = "LIXEE-" + macID;

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
  delay(100);

  WiFi.begin(ConfigSettings.ssid, ConfigSettings.password);
  WiFi.setSleep(false);
  DEBUG_PRINTLN(F("WiFi.begin"));

  IPAddress ip_address = parse_ip_address(ConfigSettings.ipAddressWiFi);
  IPAddress gateway_address = parse_ip_address(ConfigSettings.ipGWWiFi);
  IPAddress netmask = parse_ip_address(ConfigSettings.ipMaskWiFi);
  
  WiFi.config(ip_address, gateway_address, netmask);
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
    delay(250);
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


void setup(void)
{  
  

  ZiGateMode=PRODUCTION;
  
  Serial.begin(115200);
  DEBUG_PRINTLN(F("Start"));
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

  if (!LittleFS.begin(FORMAT_LittleFS_IF_FAILED, "/lfs2",20)) {
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
  
  if (ETH_ENABLE)
  {
    WiFi.onEvent(WiFiEvent);
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
      ETH.config(parse_ip_address(ConfigSettings.ipAddress), parse_ip_address(ConfigSettings.ipGW),parse_ip_address(ConfigSettings.ipMask));
    }
  }
   //Zeroconf
  String localdns = "lixee-box";
  if(!MDNS.begin(localdns.c_str())) {
     Serial.println("Error starting mDNS");
     //return;
  }

  timeClient.setPoolServerName((const char*)ConfigGeneral.ntpserver);
  timeClient.setTimeOffset((3600 * ConfigGeneral.timeoffset));
  timeClient.setTimeZone(ConfigGeneral.timezone);
  timeClient.setUpdateInterval(NTP_UPDATE_INTERVAL_MS);

  
 /*WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);*/
  
  initWebServer();
  //server.begin();
  delay(2000);
  pinMode(RESET_ZIGATE, OUTPUT);
  pinMode(FLASH_ZIGATE, OUTPUT);
  pinMode(LED1, OUTPUT);
  digitalWrite(LED1, 1);
  digitalWrite(FLASH_ZIGATE, 1);
  digitalWrite(RESET_ZIGATE, 0);
  digitalWrite(RESET_ZIGATE, 1);
  DEBUG_PRINTLN(F("add networkState"));
  commandList.push(Packet{0x0011, 0x0000,0});
  commandList.push(Packet{0x0009, 0x0000,0});
  delay(2000);
  DEBUG_PRINTLN(F("start network"));
  commandList.push(Packet{0x0024, 0x0000,0});
  DEBUG_PRINTLN(F("add getversion"));
  commandList.push(Packet{0x0010, 0x0000,0});
  DEBUG_PRINTLN(F("add networkState"));
  commandList.push(Packet{0x0025, 0x0000,0});

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

  disableCore0WDT();
}



WiFiClient client;

void loop(void)
{
  size_t bytes_read;
  uint8_t net_buf[BUFFER_SIZE];
  uint8_t serial_buf[BUFFER_SIZE];
  unsigned long currentTime = millis();
  
  if (ZiGateMode == FLASH)
  {
     bytes_read = 0;
     bool buffOK=false;
     byte tmpCRC;
     while(Serial2.available() && bytes_read < BUFFER_SIZE) 
     {
        
        buffOK=true;
        serial_buf[bytes_read] = Serial2.read();
        bytes_read++;
        
     }
      if (buffOK)
      {
        buffOK=false;
        tmpCRC = CRCcheksum(serial_buf);      
        if (tmpCRC == byte(serial_buf[serial_buf[0]]))
        {
          ResultTreatment(serial_buf);
        } 
      }
      
  }  else if (ZiGateMode == PRODUCTION)
  {
    #define min(a,b) ((a)<(b)?(a):(b))
    char output_sprintf[2];
    if(client.connected()) 
    {
      int count = client.available();
      if(count > 0) 
      {      
        bytes_read = client.read(net_buf, min(count, BUFFER_SIZE));
        Serial2.write(net_buf, bytes_read);  
        Serial2.flush();
        
        if (bytes_read>0)
        {
         // String tmpTime; 
          String buff="";
          //timeLog = millis();
          //tmpTime = String(timeLog,DEC);
          logPush('[');
          for (int j =0;j<FormattedDate.length();j++)
          {
           
            logPush(FormattedDate[j]);
          }
          logPush(']');
          logPush('-');
          logPush('>');
  
          for (int i=0;i<bytes_read;i++)
          {
           
            sprintf(output_sprintf,"%02x",net_buf[i]);
            logPush(' ');
            logPush(output_sprintf[0]);
            logPush(output_sprintf[1]);
          }
          logPush('\n');
          //Serial.write(net_buf, bytes_read);
        }
        
      }
      
    } else {
       client.stop();
    }
      // now check the swSer for any bytes to send to the network
      bytes_read = 0;
      bool buffOK=false;
      
      while(Serial2.available() && bytes_read < BUFFER_SIZE) {
       
        buffOK=true;
        serial_buf[bytes_read] = Serial2.read();
       // Serial.print(serial_buf[bytes_read]);
        bytes_read++;
        
      }
     
      if (buffOK)
      {
       //  Serial.println();
       // Serial.println((String)bytes_read);
        protocolDatas(serial_buf,bytes_read);
        for (int i=0;i<bytes_read;i++)
        {
          
          sprintf(output_sprintf,"%02x",serial_buf[i]);
          if(serial_buf[i]==0x01)
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
          if (serial_buf[i]==0x03)
          {
            logPush('\n');
          }
        }
        
        buffOK=false;
      }
      
      
      if(bytes_read > 0) {  
        client.write((const uint8_t*)serial_buf, bytes_read);
        client.flush();
      }

    

    
    //Ordonnanceur
    if (commandList.isFull() || commandTimedList.isFull() )
    {
      DEBUG_PRINTLN(F("Buffer is Full"));
    }
    if (!commandList.isEmpty())
    {
      sendZigbeeCmd(commandList.shift());
      delay(100);
    }
    
    TimedFiFo--;
    if (!commandTimedList.isEmpty() && (TimedFiFo==0))
    {
      DEBUG_PRINTLN(F("TimedFifo"));
      sendZigbeeCmd(commandTimedList.shift());
      delay(100);
    }

    // Exec toutes les minutes
    if (currentTime - previousTime >= interval) 
    {
      previousTime = currentTime; // Mettre à jour le temps précédent
  
      // Écrire sur le port série
      DEBUG_PRINTLN(F("Lancement du Poll"));

      ScanDeviceToPoll();

      FormattedDate = timeClient.getFullFormattedTime();
      Hour = timeClient.getHour() < 10 ? "0" + String(timeClient.getHour()) : String(timeClient.getHour());
      Day = timeClient.getDate() < 10 ? "0" + String(timeClient.getDate()) : String(timeClient.getDate());
      Month = timeClient.getMonth() < 10 ? "0" + String(timeClient.getMonth()) : String(timeClient.getMonth());
      Year = String(timeClient.getYear());
      Minute = timeClient.getMinute() < 10 ? "0" + String(timeClient.getMinute()) : String(timeClient.getMinute()); 
      Yesterday = timeClient.getYesterday();
      String path = "configGeneral.json";
      config_write(path, "epoch", String(timeClient.getEpochTime()));
    }
    
  }
  delay(10);
}
