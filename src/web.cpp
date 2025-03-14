#include <stdio.h>
#include "rom/ets_sys.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include "driver/temp_sensor.h"
#include <esp_task_wdt.h>
#include <stddef.h>
#include <Arduino.h>
#include <TimeLib.h>
#include "AsyncJson.h"
#define ARDUINOJSON_USE_LONG_LONG 1
#define ARDUINOJSON_SLOT_ID_SIZE 2
#include <ArduinoJson.h>
#include "WiFi.h"

#include <HTTPClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncMqttClient.h>

#include "FS.h"
#include "LittleFS.h"
#include "SPIFFS_ini.h"
#include <Update.h>

#include "config.h"
#include "flash.h"
#include "log.h"
#include "protocol.h"
#include "zigbee.h"
#include "basic.h"
#include "rules.h"
#include "microtar.h"

extern SemaphoreHandle_t file_Mutex;

extern struct ZigbeeConfig ZConfig;
extern struct ConfigSettingsStruct ConfigSettings;
extern struct ConfigGeneralStruct ConfigGeneral;
extern AsyncMqttClient mqttClient;

extern unsigned long timeLog;
extern CircularBuffer<Packet, 100> *commandList;
extern CircularBuffer<Packet, 10> *PrioritycommandList;
extern CircularBuffer<Alert, 10> *alertList;
extern CircularBuffer<Device, 10> *deviceList;

int maxDayOfTheMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
String section[12] = { "0", "1", "256", "258" , "260", "262", "264" ,"266", "268", "270", "272", "274"};

extern String Hour;
extern String Day;
extern String Month;
extern String Year;
extern String FormattedDate;

HTTPClient clientWeb;

AsyncWebServer serverWeb(80);

#define UPD_FILE "https://github.com/fairecasoimeme/lixee-gateway/releases/latest/download/firmware.bin"

const char HTTP_SHELLY_EMULE[] PROGMEM = 

"{"
    "\"name\":null,"
    "\"id\":\"shellypro3em-ac1518778a1c\","
    "\"mac\":\"AC1518778A1C\","
    "\"slot\":0,"
    "\"model\":\"SPEM-003CEBEU\","
    "\"gen\":2,"
    "\"fw_id\":\"20241011-114455/1.4.4-g6d2a586\","
    "\"ver\":\"1.4.4\","
    "\"app\":\"Pro3EM\","
    "\"auth_en\":false,"
    "\"auth_domain\":null,"
    "\"profile\":\"triphase\""

"}";

const char HTTP_HELP[] PROGMEM = 
 "<h4>About</h4>"
    "<h5>Version : {{version}}</h5>"
    "<h5>Shop & description</h5>"
    "You can go to this url :</br>"
    "<a href=\"https://lixee.fr/\" target='_blank'>Shop </a></br>"

    "<h5>Firmware Source & Issues</h5>"
    "Please go here :</br>"
    "<a href=\"https://github.com/fairecasoimeme/LiXee-Gateway\" target='_blank'>Sources</a>"
    
    
    ;

const char HTTP_HEADER[] PROGMEM =
    "<head>"
    "<script type='text/javascript' src='web/js/jquery-min.js'></script>"
    "<script type='text/javascript' src='web/js/bootstrap.min.js'></script>"
    "<script type='text/javascript' src='web/js/functions.js'></script>"
    "<link href='web/css/bootstrap.min.css' rel='stylesheet' type='text/css' />"
    "<link href='web/css/style.css' rel='stylesheet' type='text/css' />"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    " </head>";

const char HTTP_HEADERGRAPH[] PROGMEM =
    "<head>"
    "<script type='text/javascript' src='web/js/jquery-min.js'></script>"
    "<script type='text/javascript' src='web/js/bootstrap.min.js'></script>"
    "<script type='text/javascript' src='web/js/raphael-min.js'></script>"
    "<script type='text/javascript' src='web/js/morris.min.js'></script>"
    "<script type='text/javascript' src='web/js/justgage.min.js'></script>"
    "<script type='text/javascript' src='web/js/functions.js'></script>"
    "<link href='web/css/bootstrap.min.css' rel='stylesheet' type='text/css' />"
    "<link href='web/css/style.css' rel='stylesheet' type='text/css' />"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    " </head>";

const char HTTP_MENU[] PROGMEM =
    "<body>"
    "<nav class='navbar navbar-expand-md rounded'>"
    "<div class='container-fluid' style=''>"
    "<button class='navbar-toggler' type='button' data-bs-toggle='collapse' data-bs-target='#navbarNavDropdown' aria-controls='navbarNavDropdown' aria-expanded='false' aria-label='Toggle navigation'>"
    "<span class='navbar-toggler-icon'></span>"
    "</button>"
    "<a class='navbar-brand p-0 me-0 me-lg-2' href='/dashboard' style='margin-right:0px;'>"
      "<div style='display:block-inline;float:left;'><img width='70px' src='web/img/logo.png'> </div>"
      "<div style='float:left;display:block-inline;font-size:12px;font-weight:bold;padding:13px 10px 10px 10px;'> Gateway</div>"
    "</a>"
    "<div id='navbarNavDropdown' class='collapse navbar-collapse justify-content-center'>"
    "<ul class='navbar-nav mc-auto mb-2 mb-lg-0'>"
    //"<li class='nav-item'>"
    //"<a class='nav-link' href='/'>Dashboard</a>"
    //"</li>"
    "<li class='nav-item dropdown'>"
    "<a class='nav-link dropdown-toggle' href='#' id='navbarDropdown' role='button' data-bs-toggle='dropdown'>"
    "<svg xmlns='http://www.w3.org/2000/svg' style='width:24px;' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' class='feather feather-home'>"
      "<path d='M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z'></path>"
      "<polyline points='9 22 9 12 15 12 15 22'></polyline>"
    "</svg>"
    " Status"
    "</a>"
    "<div class='dropdown-menu'>"
    //"<a class='dropdown-item' href='statusEnergy'>Energy</a>"
    "<a class='dropdown-item' href='dashboard'>"
    "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px; width='16' height='16' fill='currentColor' class='bi bi-speedometer' viewBox='0 0 16 16'>"
      "<path d='M8 2a.5.5 0 0 1 .5.5V4a.5.5 0 0 1-1 0V2.5A.5.5 0 0 1 8 2M3.732 3.732a.5.5 0 0 1 .707 0l.915.914a.5.5 0 1 1-.708.708l-.914-.915a.5.5 0 0 1 0-.707M2 8a.5.5 0 0 1 .5-.5h1.586a.5.5 0 0 1 0 1H2.5A.5.5 0 0 1 2 8m9.5 0a.5.5 0 0 1 .5-.5h1.5a.5.5 0 0 1 0 1H12a.5.5 0 0 1-.5-.5m.754-4.246a.39.39 0 0 0-.527-.02L7.547 7.31A.91.91 0 1 0 8.85 8.569l3.434-4.297a.39.39 0 0 0-.029-.518z'/>"
      "<path fill-rule='evenodd' d='M6.664 15.889A8 8 0 1 1 9.336.11a8 8 0 0 1-2.672 15.78zm-4.665-4.283A11.95 11.95 0 0 1 8 10c2.186 0 4.236.585 6.001 1.606a7 7 0 1 0-12.002 0'/>"
    "</svg>"
    " Dashboard"
    "</a>"
    "<a class='dropdown-item' href='statusDevices'>"
    "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px;' width='16' height='16' fill='currentColor' class='bi bi-app-indicator' viewBox='0 0 16 16'>"
      "<path d='M5.5 2A3.5 3.5 0 0 0 2 5.5v5A3.5 3.5 0 0 0 5.5 14h5a3.5 3.5 0 0 0 3.5-3.5V8a.5.5 0 0 1 1 0v2.5a4.5 4.5 0 0 1-4.5 4.5h-5A4.5 4.5 0 0 1 1 10.5v-5A4.5 4.5 0 0 1 5.5 1H8a.5.5 0 0 1 0 1z'/>"
      "<path d='M16 3a3 3 0 1 1-6 0 3 3 0 0 1 6 0'/>"
    "</svg>"
    " Devices"
    "</a>"
    "<a class='dropdown-item' href='statusNetwork'>"
    "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px;' width='16' height='16' fill='currentColor' class='bi bi-wifi' viewBox='0 0 16 16'>"
      "<path d='M15.384 6.115a.485.485 0 0 0-.047-.736A12.44 12.44 0 0 0 8 3C5.259 3 2.723 3.882.663 5.379a.485.485 0 0 0-.048.736.52.52 0 0 0 .668.05A11.45 11.45 0 0 1 8 4c2.507 0 4.827.802 6.716 2.164.205.148.49.13.668-.049'/>"
      "<path d='M13.229 8.271a.482.482 0 0 0-.063-.745A9.46 9.46 0 0 0 8 6c-1.905 0-3.68.56-5.166 1.526a.48.48 0 0 0-.063.745.525.525 0 0 0 .652.065A8.46 8.46 0 0 1 8 7a8.46 8.46 0 0 1 4.576 1.336c.206.132.48.108.653-.065m-2.183 2.183c.226-.226.185-.605-.1-.75A6.5 6.5 0 0 0 8 9c-1.06 0-2.062.254-2.946.704-.285.145-.326.524-.1.75l.015.015c.16.16.407.19.611.09A5.5 5.5 0 0 1 8 10c.868 0 1.69.201 2.42.56.203.1.45.07.61-.091zM9.06 12.44c.196-.196.198-.52-.04-.66A2 2 0 0 0 8 11.5a2 2 0 0 0-1.02.28c-.238.14-.236.464-.04.66l.706.706a.5.5 0 0 0 .707 0l.707-.707z'/>"
    "</svg>"
    " Network"
    "</a>"
    "</div>"
    "</li>"

    "<li class='nav-item dropdown'>"
    "<a class='nav-link dropdown-toggle' href='#' id='navbarDropdown' role='button' data-bs-toggle='dropdown'>"
    "<svg xmlns='http://www.w3.org/2000/svg' style='width:24px;' width='24' height='24' fill='currentColor' class='bi bi-router' viewBox='0 0 16 16'>"
      "<path d='M5.525 3.025a3.5 3.5 0 0 1 4.95 0 .5.5 0 1 0 .707-.707 4.5 4.5 0 0 0-6.364 0 .5.5 0 0 0 .707.707'/>"
      "<path d='M6.94 4.44a1.5 1.5 0 0 1 2.12 0 .5.5 0 0 0 .708-.708 2.5 2.5 0 0 0-3.536 0 .5.5 0 0 0 .707.707ZM2.5 11a.5.5 0 1 1 0-1 .5.5 0 0 1 0 1m4.5-.5a.5.5 0 1 0 1 0 .5.5 0 0 0-1 0m2.5.5a.5.5 0 1 1 0-1 .5.5 0 0 1 0 1m1.5-.5a.5.5 0 1 0 1 0 .5.5 0 0 0-1 0m2 0a.5.5 0 1 0 1 0 .5.5 0 0 0-1 0'/>"
      "<path d='M2.974 2.342a.5.5 0 1 0-.948.316L3.806 8H1.5A1.5 1.5 0 0 0 0 9.5v2A1.5 1.5 0 0 0 1.5 13H2a.5.5 0 0 0 .5.5h2A.5.5 0 0 0 5 13h6a.5.5 0 0 0 .5.5h2a.5.5 0 0 0 .5-.5h.5a1.5 1.5 0 0 0 1.5-1.5v-2A1.5 1.5 0 0 0 14.5 8h-2.306l1.78-5.342a.5.5 0 1 0-.948-.316L11.14 8H4.86zM14.5 9a.5.5 0 0 1 .5.5v2a.5.5 0 0 1-.5.5h-13a.5.5 0 0 1-.5-.5v-2a.5.5 0 0 1 .5-.5z'/>"
      "<path d='M8.5 5.5a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0'/>"
    "</svg>"
    " Network"
    "</a>"
    "<div class='dropdown-menu'>"
    "<a class='dropdown-item' href='configWiFi'>"
    "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px;' width='16' height='16' fill='currentColor' class='bi bi-wifi' viewBox='0 0 16 16'>"
      "<path d='M15.384 6.115a.485.485 0 0 0-.047-.736A12.44 12.44 0 0 0 8 3C5.259 3 2.723 3.882.663 5.379a.485.485 0 0 0-.048.736.52.52 0 0 0 .668.05A11.45 11.45 0 0 1 8 4c2.507 0 4.827.802 6.716 2.164.205.148.49.13.668-.049'/>"
      "<path d='M13.229 8.271a.482.482 0 0 0-.063-.745A9.46 9.46 0 0 0 8 6c-1.905 0-3.68.56-5.166 1.526a.48.48 0 0 0-.063.745.525.525 0 0 0 .652.065A8.46 8.46 0 0 1 8 7a8.46 8.46 0 0 1 4.576 1.336c.206.132.48.108.653-.065m-2.183 2.183c.226-.226.185-.605-.1-.75A6.5 6.5 0 0 0 8 9c-1.06 0-2.062.254-2.946.704-.285.145-.326.524-.1.75l.015.015c.16.16.407.19.611.09A5.5 5.5 0 0 1 8 10c.868 0 1.69.201 2.42.56.203.1.45.07.61-.091zM9.06 12.44c.196-.196.198-.52-.04-.66A2 2 0 0 0 8 11.5a2 2 0 0 0-1.02.28c-.238.14-.236.464-.04.66l.706.706a.5.5 0 0 0 .707 0l.707-.707z'/>"
    "</svg>"
    " WiFi"
    "</a>"
    "<a class='dropdown-item' href='configDevices'>"
    "<svg fill='currentColor' style='width:16px;' width='16' height='16' viewBox='0 0 24 24' role='img' xmlns='http://www.w3.org/2000/svg'>"
      "<path d='M11.988 0a11.85 11.85 0 00-8.617 3.696c7.02-.875 11.401-.583 13.289-.34 3.752.583 3.558 3.404 3.558 3.404L8.237 19.112c2.299.22 6.897.366 13.796-.631a11.86 11.86 0 001.912-6.469C23.945 5.374 18.595 0 11.988 0zm.232 4.31c-2.451-.014-5.772.146-9.963.723C.854 7.003.055 9.41.055 12.012.055 18.626 5.38 24 11.988 24c3.63 0 6.85-1.63 9.053-4.182-7.286.948-11.813.631-13.75.388-3.775-.56-3.557-3.404-3.557-3.404L15.691 4.474a38.635 38.635 0 00-3.471-.163Z'/>"
    "</svg>"
    " Zigbee"
    "</a>"
    "</div>"
    "</li>"


    "<li class='nav-item dropdown'>"
    "<a class='nav-link dropdown-toggle' href='#' id='navbarDropdown' role='button' data-bs-toggle='dropdown'>"
    "<svg style='width:24px;' width='24' height='24' viewBox='0 0 16 16' xmlns='http://www.w3.org/2000/svg' fill='currentColor'>"
      "<path fill='#000000' fill-rule='evenodd' d='M13.75.5a2.25 2.25 0 00-1.847 3.536l-.933.934a.752.752 0 00-.11.14c-.19-.071-.396-.11-.61-.11h-2.5A1.75 1.75 0 006 6.75v.5H4.372a2.25 2.25 0 100 1.5H6v.5c0 .966.784 1.75 1.75 1.75h2.5c.214 0 .42-.039.61-.11.03.05.067.097.11.14l.933.934a2.25 2.25 0 101.24-.881L12.03 9.97a.75.75 0 00-.14-.11c.071-.19.11-.396.11-.61v-2.5c0-.214-.039-.42-.11-.61a.75.75 0 00.14-.11l1.113-1.113A2.252 2.252 0 0016 2.75 2.25 2.25 0 0013.75.5zM13 2.75a.75.75 0 111.5 0 .75.75 0 01-1.5 0zM7.75 6.5a.25.25 0 00-.25.25v2.5c0 .138.112.25.25.25h2.5a.25.25 0 00.25-.25v-2.5a.25.25 0 00-.25-.25h-2.5zm6 6a.75.75 0 100 1.5.75.75 0 000-1.5zM1.5 8A.75.75 0 113 8a.75.75 0 01-1.5 0z' clip-rule='evenodd'/>"
    "</svg>"
    " Gateway"
    "</a>"
    "<div class='dropdown-menu'>"
    "<a class='dropdown-item' href='/configMQTT'>"
    "<svg role='img' viewBox='0 0 24 24' xmlns='http://www.w3.org/2000/svg' style='width:16px;' height='16' width='16'>"
      "<path d='M10.657 23.994h-9.45A1.212 1.212 0 0 1 0 22.788v-9.18h0.071c5.784 0 10.504 4.65 10.586 10.386Zm7.606 0h-4.045C14.135 16.246 7.795 9.977 0 9.942V6.038h0.071c9.983 0 18.121 8.044 18.192 17.956Zm4.53 0h-0.97C21.754 12.071 11.995 2.407 0 2.372v-1.16C0 0.55 0.544 0.006 1.207 0.006h7.64C15.733 2.49 21.257 7.789 24 14.508v8.291c0 0.663 -0.544 1.195 -1.207 1.195ZM16.713 0.006h6.092A1.19 1.19 0 0 1 24 1.2v5.914c-0.91 -1.242 -2.046 -2.65 -3.158 -3.762C19.588 2.11 18.122 0.987 16.714 0.005Z' fill='currentColor' stroke-width='1'></path>"
    "</svg>"
    " MQTT"
    "</a>"
    "<a class='dropdown-item' href='/configWebPush'>"
    "<svg style='width:16px;' width='16' height='16' viewBox='0 0 24 24' fill='currentColor' xmlns='http://www.w3.org/2000/svg'>"
      "<path fill-rule='evenodd' clip-rule='evenodd' d='M9.83824 18.4467C10.0103 18.7692 10.1826 19.0598 10.3473 19.3173C8.59745 18.9238 7.07906 17.9187 6.02838 16.5383C6.72181 16.1478 7.60995 15.743 8.67766 15.4468C8.98112 16.637 9.40924 17.6423 9.83824 18.4467ZM11.1618 17.7408C10.7891 17.0421 10.4156 16.1695 10.1465 15.1356C10.7258 15.0496 11.3442 15 12.0001 15C12.6559 15 13.2743 15.0496 13.8535 15.1355C13.5844 16.1695 13.2109 17.0421 12.8382 17.7408C12.5394 18.3011 12.2417 18.7484 12 19.0757C11.7583 18.7484 11.4606 18.3011 11.1618 17.7408ZM9.75 12C9.75 12.5841 9.7893 13.1385 9.8586 13.6619C10.5269 13.5594 11.2414 13.5 12.0001 13.5C12.7587 13.5 13.4732 13.5593 14.1414 13.6619C14.2107 13.1384 14.25 12.5841 14.25 12C14.25 11.4159 14.2107 10.8616 14.1414 10.3381C13.4732 10.4406 12.7587 10.5 12.0001 10.5C11.2414 10.5 10.5269 10.4406 9.8586 10.3381C9.7893 10.8615 9.75 11.4159 9.75 12ZM8.38688 10.0288C8.29977 10.6478 8.25 11.3054 8.25 12C8.25 12.6946 8.29977 13.3522 8.38688 13.9712C7.11338 14.3131 6.05882 14.7952 5.24324 15.2591C4.76698 14.2736 4.5 13.168 4.5 12C4.5 10.832 4.76698 9.72644 5.24323 8.74088C6.05872 9.20472 7.1133 9.68686 8.38688 10.0288ZM10.1465 8.86445C10.7258 8.95042 11.3442 9 12.0001 9C12.6559 9 13.2743 8.95043 13.8535 8.86447C13.5844 7.83055 13.2109 6.95793 12.8382 6.2592C12.5394 5.69894 12.2417 5.25156 12 4.92432C11.7583 5.25156 11.4606 5.69894 11.1618 6.25918C10.7891 6.95791 10.4156 7.83053 10.1465 8.86445ZM15.6131 10.0289C15.7002 10.6479 15.75 11.3055 15.75 12C15.75 12.6946 15.7002 13.3521 15.6131 13.9711C16.8866 14.3131 17.9412 14.7952 18.7568 15.2591C19.233 14.2735 19.5 13.1679 19.5 12C19.5 10.8321 19.233 9.72647 18.7568 8.74093C17.9413 9.20477 16.8867 9.6869 15.6131 10.0289ZM17.9716 7.46178C17.2781 7.85231 16.39 8.25705 15.3224 8.55328C15.0189 7.36304 14.5908 6.35769 14.1618 5.55332C13.9897 5.23077 13.8174 4.94025 13.6527 4.6827C15.4026 5.07623 16.921 6.08136 17.9716 7.46178ZM8.67765 8.55325C7.61001 8.25701 6.7219 7.85227 6.02839 7.46173C7.07906 6.08134 8.59745 5.07623 10.3472 4.6827C10.1826 4.94025 10.0103 5.23076 9.83823 5.5533C9.40924 6.35767 8.98112 7.36301 8.67765 8.55325ZM15.3224 15.4467C15.0189 16.637 14.5908 17.6423 14.1618 18.4467C13.9897 18.7692 13.8174 19.0598 13.6527 19.3173C15.4026 18.9238 16.921 17.9186 17.9717 16.5382C17.2782 16.1477 16.3901 15.743 15.3224 15.4467ZM12 21C16.9706 21 21 16.9706 21 12C21 7.02944 16.9706 3 12 3C7.02944 3 3 7.02944 3 12C3 16.9706 7.02944 21 12 21Z' fill='currentColor'/>"
    "</svg>"
    " WebPush"
    "</a>"
    "<a class='dropdown-item' href='/configMarstek'>"
    "<svg style='width:16px;' xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink' width='24px' height='20px' viewBox='0 0 23 20' version='1.1'>"
      "<g id='surface1'>"
        "<path style=' stroke:none;fill-rule:nonzero;fill:rgb(0%,0%,0%);fill-opacity:1;' d='M 20.652344 1.757812 C 20.03125 2.171875 19.339844 2.757812 19.09375 3.046875 C 18.28125 4.035156 18.304688 3.878906 18.328125 11.867188 L 18.363281 19.035156 L 19.503906 18.292969 C 20.125 17.878906 20.851562 17.304688 21.109375 17 C 21.96875 15.988281 21.945312 16.183594 21.945312 8.144531 C 21.945312 4.207031 21.910156 0.976562 21.863281 0.976562 C 21.804688 0.988281 21.261719 1.328125 20.652344 1.757812 Z M 20.652344 1.757812 '/>"
        "<path style=' stroke:none;fill-rule:nonzero;fill:rgb(0%,0%,0%);fill-opacity:1;' d='M 1.644531 8.269531 C 1.644531 16.195312 1.644531 16.132812 2.464844 17.097656 C 2.980469 17.707031 5.09375 19.207031 5.28125 19.085938 C 5.351562 19.035156 5.386719 16.425781 5.363281 11.855469 L 5.339844 4.695312 L 5.070312 4.085938 C 4.917969 3.742188 4.636719 3.292969 4.4375 3.085938 C 4.011719 2.632812 2.007812 1.21875 1.785156 1.21875 C 1.679688 1.21875 1.644531 2.867188 1.644531 8.269531 Z M 1.644531 8.269531 '/>"
        "<path style=' stroke:none;fill-rule:nonzero;fill:rgb(0%,0%,0%);fill-opacity:1;' d='M 15.019531 5.171875 C 13.800781 6 13.355469 6.476562 12.992188 7.316406 C 12.765625 7.816406 12.753906 8.144531 12.730469 11.476562 C 12.707031 13.464844 12.742188 15.160156 12.789062 15.242188 C 12.859375 15.367188 13.144531 15.242188 13.90625 14.742188 C 15.160156 13.925781 15.722656 13.390625 16.089844 12.609375 C 16.371094 12.023438 16.371094 11.953125 16.40625 8.195312 C 16.429688 5.132812 16.417969 4.390625 16.289062 4.390625 C 16.207031 4.402344 15.628906 4.742188 15.019531 5.171875 Z M 15.019531 5.171875 '/>"
        "<path style=' stroke:none;fill-rule:nonzero;fill:rgb(0%,0%,0%);fill-opacity:1;' d='M 7.15625 8.011719 C 7.15625 9.953125 7.21875 11.769531 7.289062 12.074219 C 7.511719 13.085938 8.074219 13.78125 9.457031 14.730469 C 10.152344 15.207031 10.761719 15.574219 10.820312 15.535156 C 10.867188 15.5 10.914062 13.890625 10.914062 11.964844 C 10.914062 8.097656 10.855469 7.644531 10.210938 6.71875 C 9.878906 6.257812 7.558594 4.511719 7.265625 4.511719 C 7.207031 4.511719 7.15625 6.011719 7.15625 8.011719 Z M 7.15625 8.011719 '/>"
      "</g>"
      "</svg>"
    " Marstek"
    "</a>"
    /*"<a class='dropdown-item' href='/configUdpClient'>"
    "<svg style='width:16px;' xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' class='bi bi-caret-right' viewBox='0 0 16 16'>"
      "<path d='M6 12.796V3.204L11.481 8zm.659.753 5.48-4.796a1 1 0 0 0 0-1.506L6.66 2.451C6.011 1.885 5 2.345 5 3.204v9.592a1 1 0 0 0 1.659.753'/>"
    "</svg>"
    " UDP client"
    "</a>"*/
    "</div>"
    "</li>"
    "<li class='nav-item dropdown'>"
    "<a class='nav-link dropdown-toggle' href='#' id='navbarDropdown' role='button' data-bs-toggle='dropdown'>"
    "<svg xmlns='http://www.w3.org/2000/svg' style='width:24px;' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' class='feather feather-settings'>"
      "<circle cx='12' cy='12' r='3'></circle>"
      "<path d='M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z'></path>"
    "</svg>"
    " Config"
    "</a>"
    "<div class='dropdown-menu'>"
    "<a class='dropdown-item' href='/configGeneral'>"
    "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px;' width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' class='feather feather-settings'>"
      "<circle cx='12' cy='12' r='3'></circle>"
      "<path d='M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z'></path>"
    "</svg>"
    " General"
    "</a>"
    "<a class='dropdown-item' href='/configHorloge'>"
    "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' style='width:16px;' class='bi bi-clock' viewBox='0 0 16 16'>"
      "<path d='M8 3.5a.5.5 0 0 0-1 0V9a.5.5 0 0 0 .252.434l3.5 2a.5.5 0 0 0 .496-.868L8 8.71z'></path><path d='M8 16A8 8 0 1 0 8 0a8 8 0 0 0 0 16m7-8A7 7 0 1 1 1 8a7 7 0 0 1 14 0'></path>"
    "</svg>"
    " Horloge"
    "</a>"
    "<a class='dropdown-item' href='/configHTTP'>"
    "<svg style='width:16px;' width='16' height='16' viewBox='0 0 24 24' fill='none' xmlns='http://www.w3.org/2000/svg'>"
      "<path d='M2 16C2 13.1716 2 11.7574 2.87868 10.8787C3.75736 10 5.17157 10 8 10H16C18.8284 10 20.2426 10 21.1213 10.8787C22 11.7574 22 13.1716 22 16C22 18.8284 22 20.2426 21.1213 21.1213C20.2426 22 18.8284 22 16 22H8C5.17157 22 3.75736 22 2.87868 21.1213C2 20.2426 2 18.8284 2 16Z' stroke='currentColor' stroke-width='1.5'/>"
      "<path d='M6 10V8C6 4.68629 8.68629 2 12 2C15.3137 2 18 4.68629 18 8V10' stroke='currentColor' stroke-width='1.5' stroke-linecap='round'/>"
    "</svg>"
    " Security"
    "</a>"
    "<a class='dropdown-item' href='/configRules'>"
    "<svg style='width:16px;' xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' class='bi bi-file-ruled' viewBox='0 0 16 16'>"
      "<path d='M2 2a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2zm2-1a1 1 0 0 0-1 1v4h10V2a1 1 0 0 0-1-1zm9 6H6v2h7zm0 3H6v2h7zm0 3H6v2h6a1 1 0 0 0 1-1zm-8 2v-2H3v1a1 1 0 0 0 1 1zm-2-3h2v-2H3zm0-3h2V7H3z'/>"
    "</svg>"
    " Rules"
    "</a>"
    "</div>"
    "</li>"
    "<li class='nav-item'>"
    "<a class='nav-link' href='/tools'>"
    "<svg viewBox='0 0 24 24' style='width:24px;' width='24' height='24' stroke='currentColor' stroke-width='2' fill='none' stroke-linecap='round' stroke-linejoin='round' class='css-i6dzq1'>"
      "<path d='M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94l-3.76 3.76z'></path>"
    "</svg>"
    " Tools"
    "</a>"
    "</li>"
    "<li class='nav-item'>"
    "<a class='nav-link' href='/help'>"
    "<svg xmlns='http://www.w3.org/2000/svg' style='width:24px;' width='24' height='24' fill='currentColor' class='bi bi-question-circle' viewBox='0 0 16 16'>"
      "<path d='M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16'/>"
      "<path d='M5.255 5.786a.237.237 0 0 0 .241.247h.825c.138 0 .248-.113.266-.25.09-.656.54-1.134 1.342-1.134.686 0 1.314.343 1.314 1.168 0 .635-.374.927-.965 1.371-.673.489-1.206 1.06-1.168 1.987l.003.217a.25.25 0 0 0 .25.246h.811a.25.25 0 0 0 .25-.25v-.105c0-.718.273-.927 1.01-1.486.609-.463 1.244-.977 1.244-2.056 0-1.511-1.276-2.241-2.673-2.241-1.267 0-2.655.59-2.75 2.286m1.557 5.763c0 .533.425.927 1.01.927.609 0 1.028-.394 1.028-.927 0-.552-.42-.94-1.029-.94-.584 0-1.009.388-1.009.94'/>"
    "</svg>"
    " About"
    "</a>"
    "</li>"
    "</ul></div></div>"
    "</nav>"
    "<div style='display:block;text-align:right;font-size:12px;'>"
          "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' style='width:16px;' class='bi bi-clock' viewBox='0 0 16 16'>"
                "<path d='M8 3.5a.5.5 0 0 0-1 0V9a.5.5 0 0 0 .252.434l3.5 2a.5.5 0 0 0 .496-.868L8 8.71z'/>"
                "<path d='M8 16A8 8 0 1 0 8 0a8 8 0 0 0 0 16m7-8A7 7 0 1 1 1 8a7 7 0 0 1 14 0'/>"
          "</svg> "
        "<span id='FormattedDate' style='padding:20 20 0 0;'>{{FormattedDate}}</span>"
    "</div>"
    
    "<div id='alert' style='display:none;' class='alert alert-success' role='alert'>"
    "</div>";

// "<a href='/configFiles' class='btn btn-primary mb-2'>Config Files</a>"
const char HTTP_TOOLS[] PROGMEM =
    "<h4>"
    "Advanced Tools"
    "</h4>"
    "<div class='btn-group-vertical'>"
    //"<a href='/logs' class='btn btn-primary mb-2'>Console</a>"
    "<a href='/debugFiles' class='btn btn-primary mb-2'>"
    "<svg viewBox='0 0 24 24'  width='24' height='24' stroke='currentColor' stroke-width='2' fill='none' stroke-linecap='round' stroke-linejoin='round'>"
      "<polyline points='4 17 10 11 4 5'></polyline>"
      "<line x1='12' y1='19' x2='20' y2='19'></line>"
    "</svg><br>"
    " Debug"
    "</a>"
    "<a href='/fsbrowser' class='btn btn-primary mb-2'>"
    "<svg xmlns='http://www.w3.org/2000/svg'  width='24' height='24' fill='currentColor' class='bi bi-app-indicator' viewBox='0 0 16 16'>"
      "<path d='M5.5 2A3.5 3.5 0 0 0 2 5.5v5A3.5 3.5 0 0 0 5.5 14h5a3.5 3.5 0 0 0 3.5-3.5V8a.5.5 0 0 1 1 0v2.5a4.5 4.5 0 0 1-4.5 4.5h-5A4.5 4.5 0 0 1 1 10.5v-5A4.5 4.5 0 0 1 5.5 1H8a.5.5 0 0 1 0 1z'/>"
      "<path d='M16 3a3 3 0 1 1-6 0 3 3 0 0 1 6 0'/>"
    "</svg><br>"
    " Device Files"
    "</a>"
    "<a href='/tp' class='btn btn-primary mb-2'>"
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='currentColor' class='bi bi-filetype-json' viewBox='0 0 16 16'>"
      "<path fill-rule='evenodd' d='M14 4.5V11h-1V4.5h-2A1.5 1.5 0 0 1 9.5 3V1H4a1 1 0 0 0-1 1v9H2V2a2 2 0 0 1 2-2h5.5zM4.151 15.29a1.2 1.2 0 0 1-.111-.449h.764a.58.58 0 0 0 .255.384q.105.073.25.114.142.041.319.041.245 0 .413-.07a.56.56 0 0 0 .255-.193.5.5 0 0 0 .084-.29.39.39 0 0 0-.152-.326q-.152-.12-.463-.193l-.618-.143a1.7 1.7 0 0 1-.539-.214 1 1 0 0 1-.352-.367 1.1 1.1 0 0 1-.123-.524q0-.366.19-.639.192-.272.528-.422.337-.15.777-.149.456 0 .779.152.326.153.5.41.18.255.2.566h-.75a.56.56 0 0 0-.12-.258.6.6 0 0 0-.246-.181.9.9 0 0 0-.37-.068q-.324 0-.512.152a.47.47 0 0 0-.185.384q0 .18.144.3a1 1 0 0 0 .404.175l.621.143q.326.075.566.211a1 1 0 0 1 .375.358q.135.222.135.56 0 .37-.188.656a1.2 1.2 0 0 1-.539.439q-.351.158-.858.158-.381 0-.665-.09a1.4 1.4 0 0 1-.478-.252 1.1 1.1 0 0 1-.29-.375m-3.104-.033a1.3 1.3 0 0 1-.082-.466h.764a.6.6 0 0 0 .074.27.5.5 0 0 0 .454.246q.285 0 .422-.164.137-.165.137-.466v-2.745h.791v2.725q0 .66-.357 1.005-.355.345-.985.345a1.6 1.6 0 0 1-.568-.094 1.15 1.15 0 0 1-.407-.266 1.1 1.1 0 0 1-.243-.39m9.091-1.585v.522q0 .384-.117.641a.86.86 0 0 1-.322.387.9.9 0 0 1-.47.126.9.9 0 0 1-.47-.126.87.87 0 0 1-.32-.387 1.55 1.55 0 0 1-.117-.641v-.522q0-.386.117-.641a.87.87 0 0 1 .32-.387.87.87 0 0 1 .47-.129q.265 0 .47.129a.86.86 0 0 1 .322.387q.117.255.117.641m.803.519v-.513q0-.565-.205-.973a1.46 1.46 0 0 0-.59-.63q-.38-.22-.916-.22-.534 0-.92.22a1.44 1.44 0 0 0-.589.628q-.205.407-.205.975v.513q0 .562.205.973.205.407.589.626.386.217.92.217.536 0 .917-.217.384-.22.589-.626.204-.41.205-.973m1.29-.935v2.675h-.746v-3.999h.662l1.752 2.66h.032v-2.66h.75v4h-.656l-1.761-2.676z'/>"
    "</svg><br>"
    " Templates"
    "</a>"
    "<a href='/rules' class='btn btn-primary mb-2'>"
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='currentColor' class='bi bi-file-ruled' viewBox='0 0 16 16'>"
      "<path d='M2 2a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2zm2-1a1 1 0 0 0-1 1v4h10V2a1 1 0 0 0-1-1zm9 6H6v2h7zm0 3H6v2h7zm0 3H6v2h6a1 1 0 0 0 1-1zm-8 2v-2H3v1a1 1 0 0 0 1 1zm-2-3h2v-2H3zm0-3h2V7H3z'/>"
    "</svg><br>"
    " Rules"
    "</a>"
    //"<a href='/javascript' class='btn btn-primary mb-2'>Javascript</a>"
    "<a href='/update' class='btn btn-primary mb-2'>"
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='currentColor' class='bi bi-download' viewBox='0 0 16 16'>"
    "  <path d='M.5 9.9a.5.5 0 0 1 .5.5v2.5a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-2.5a.5.5 0 0 1 1 0v2.5a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2v-2.5a.5.5 0 0 1 .5-.5'/>"
    "  <path d='M7.646 11.854a.5.5 0 0 0 .708 0l3-3a.5.5 0 0 0-.708-.708L8.5 10.293V1.5a.5.5 0 0 0-1 0v8.793L5.354 8.146a.5.5 0 1 0-.708.708z'/>"
    "</svg><br>"
    " Update"
    "</a>"
    "<a href='/backup' class='btn btn-primary mb-2'>"
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='currentColor' class='bi bi-cloud-arrow-up' viewBox='0 0 16 16'>"
    "  <path fill-rule='evenodd' d='M7.646 5.146a.5.5 0 0 1 .708 0l2 2a.5.5 0 0 1-.708.708L8.5 6.707V10.5a.5.5 0 0 1-1 0V6.707L6.354 7.854a.5.5 0 1 1-.708-.708z'/>"
    "  <path d='M4.406 3.342A5.53 5.53 0 0 1 8 2c2.69 0 4.923 2 5.166 4.579C14.758 6.804 16 8.137 16 9.773 16 11.569 14.502 13 12.687 13H3.781C1.708 13 0 11.366 0 9.318c0-1.763 1.266-3.223 2.942-3.593.143-.863.698-1.723 1.464-2.383m.653.757c-.757.653-1.153 1.44-1.153 2.056v.448l-.445.049C2.064 6.805 1 7.952 1 9.318 1 10.785 2.23 12 3.781 12h8.906C13.98 12 15 10.988 15 9.773c0-1.216-1.02-2.228-2.313-2.228h-.5v-.5C12.188 4.825 10.328 3 8 3a4.53 4.53 0 0 0-2.941 1.1z'/>"
    "</svg><br>"
    " Backup"
    "</a>"
    "<a href='/reboot' class='btn btn-primary mb-2'>"
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='currentColor' class='bi bi-bootstrap-reboot' viewBox='0 0 16 16'>"
    "  <path d='M1.161 8a6.84 6.84 0 1 0 6.842-6.84.58.58 0 1 1 0-1.16 8 8 0 1 1-6.556 3.412l-.663-.577a.58.58 0 0 1 .227-.997l2.52-.69a.58.58 0 0 1 .728.633l-.332 2.592a.58.58 0 0 1-.956.364l-.643-.56A6.8 6.8 0 0 0 1.16 8z'/>"
    "  <path d='M6.641 11.671V8.843h1.57l1.498 2.828h1.314L9.377 8.665c.897-.3 1.427-1.106 1.427-2.1 0-1.37-.943-2.246-2.456-2.246H5.5v7.352zm0-3.75V5.277h1.57c.881 0 1.416.499 1.416 1.32 0 .84-.504 1.324-1.386 1.324z'/>"
    "</svg><br>"
    " Reboot"
    "</a>"
    "</div>"; 

const char HTTP_BACKUP[] PROGMEM =
    "<h4>Backup datas</h4>"
    "<a href='#' class='btn btn-primary mb-2' onClick='createBackupFile()'>Create Backup</a>"
    "<div id='createBackupFile'>"
    "</div>"
    "<h4>Restore datas</h4>"
    "<div id='restoreBackupFile'>"
    "{{listBackupFiles}}"
    "</div>"
    "<form method='POST' action='/doRestore' enctype='multipart/form-data' id='upload_form'>"
    "<input type='file' name='update' id='file' onchange='sub(this)' style=display:none accept='.tar'>"
    "<label id='file-input' for='file'>Choose backup...</label>"
    "<input type='submit' class='btn btn-warning mb-2' value='Restore'>"
    "<br><br>"
    "<div id='prg'></div>"
    "<br><div id='prgbar'><div id='bar'></div></div><br></form>"

    "<script>"
    "function sub(obj){"
    "var fileName = obj.value.split('\\\\');"
    "document.getElementById('file-input').innerHTML = '   '+ fileName[fileName.length-1];"
    "};"
    "$('form').submit(function(e){"
    "e.preventDefault();"
    "var form = $('#upload_form')[0];"
    "var data = new FormData(form);"
    "$.ajax({"
    "url: '/doRestore',"
    "type: 'POST',"
    "data: data,"
    "contentType: false,"
    "processData:false,"
    "xhr: function() {"
    "var xhr = new window.XMLHttpRequest();"
    "xhr.upload.addEventListener('progress', function(evt) {"
    "if (evt.lengthComputable) {"
    "var per = evt.loaded / evt.total;"
    "$('#prg').html( Math.round(per*100) + '%');"
    "$('#bar').css('width',Math.round(per*100) + '%');"
    "}"
    "}, false);"
    "return xhr;"
    "},"
    "success:function(d, s) {"
    "console.log('success!');"
    "$('#prg').html('restore completed!<br>Rebooting!');"
    "window.location.href='/backup';"
    "},"
    "error: function (a, b, c) {"
    "}"
    "});"
    "});"
    "</script>";
    ;

const char HTTP_UPDATE[] PROGMEM =
    "<h4>Update firmware</h4>"
    "<div align='center'>"
    "<div id='update_info'>"
    "<h4>Latest version on GitHub</h4>"
    "<div id='onlineupdate' style='text-align:left;width:480px;'>"
    "<h5 id=releasehead></h5>"
    "<div style='clear:both;'>"
    "<br>"
    "</div>"  
    "<pre id=releasebody>Getting update information from GitHub...</pre>"
    "</div>"
    
    "You can download the last firmware here : "
    "<a style='margin-left: 40px;' class='pull-right' href='{{linkFirmware}}' >"
    "<button type='button' class='btn btn-success'>Download</button>"
    "</a>"
    "</div>"
    "<form method='POST' action='/doUpdate' enctype='multipart/form-data' id='upload_form'>"
    "<input type='file' name='update' id='file' onchange='sub(this)' style=display:none accept='.bin'>"
    "<label id='file-input' for='file'>   Choose file...</label>"
    "<input type='submit' class='btn btn-warning mb-2' value='Update'>"
    "<br><br>"
    "<div id='prg'></div>"
    "<br><div id='prgbar'><div id='bar'></div></div><br></form>"
    
  
    "<script language='javascript'>getLatestReleaseInfo();</script>"
    "<script>"
    "function sub(obj){"
    "var fileName = obj.value.split('\\\\');"
    "document.getElementById('file-input').innerHTML = '   '+ fileName[fileName.length-1];"
    "};"
    "$('form').submit(function(e){"
    "e.preventDefault();"
    "var form = $('#upload_form')[0];"
    "var data = new FormData(form);"
    "$.ajax({"
    "url: '/doUpdate',"
    "type: 'POST',"
    "data: data,"
    "contentType: false,"
    "processData:false,"
    "xhr: function() {"
    "var xhr = new window.XMLHttpRequest();"
    "xhr.upload.addEventListener('progress', function(evt) {"
    "if (evt.lengthComputable) {"
    "var per = evt.loaded / evt.total;"
    "$('#prg').html(Math.round(per*100) + '%');"
    "$('#bar').css('width',Math.round(per*100) + '%');"
    "}"
    "}, false);"
    "return xhr;"
    "},"
    "success:function(d, s) {"
    "console.log('success!');"
    "$('#prg').html('Update completed!<br>Rebooting!');"
    "window.location.href='/';"
    "},"
    "error: function (a, b, c) {"
    "}"
    "});"
    "});"
    "</script>";

const char HTTP_CONFIG_MENU_ZIGBEE[] PROGMEM =
    "<a href='/configDevices' style='width:100px;height:64px;' class='btn btn-primary mb-1 {{menu_config_devices}}' >"
    "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px;' width='16' height='16' fill='currentColor' class='bi bi-app-indicator' viewBox='0 0 16 16'>"
      "<path d='M5.5 2A3.5 3.5 0 0 0 2 5.5v5A3.5 3.5 0 0 0 5.5 14h5a3.5 3.5 0 0 0 3.5-3.5V8a.5.5 0 0 1 1 0v2.5a4.5 4.5 0 0 1-4.5 4.5h-5A4.5 4.5 0 0 1 1 10.5v-5A4.5 4.5 0 0 1 5.5 1H8a.5.5 0 0 1 0 1z'/>"
      "<path d='M16 3a3 3 0 1 1-6 0 3 3 0 0 1 6 0'/>"
    "</svg><br>"
    " Devices"
    "</a>&nbsp"
    "<a href='/configZigbee' style='width:100px;height:64px;' class='btn btn-primary mb-1 {{menu_config_zigbee}}' >"
    "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px;' width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' class='feather feather-settings'>"
      "<circle cx='12' cy='12' r='3'></circle>"
      "<path d='M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z'></path>"
    "</svg><br>"
    " Config"
    "</a>&nbsp"
    ;

const char HTTP_CONFIG_DEVICES_ZIGBEE[] PROGMEM =

    "<div class='row justify-content-md-center' >"
      "<div class='col-sm-2'>"
        "<div class='btn-group-horizontal'>"
          "{{menu_config_zigbee}}"
        "</div>"
      "</div>"
      "<div class='col-sm-10'>"
        "<h4>Config Zigbee Devices</h4>"
        "<div align='right'>"
          "<button type='button' onclick='cmd(\"PermitJoin\");' class='btn btn-primary'>"
          "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='currentColor' class='bi bi-plus-circle' viewBox='0 0 16 16'>"
            "<path d='M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16'/>"
            "<path d='M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4'/>"
          "</svg><br>"
          " Add Device"
          "</button> "
        "</div><br>"
        "<h5>List of devices</h5>"
        "<div class='row' style='font-size:12px;--bs-gutter-x: 0.1rem;'>"
          "{{devicesList}}"
        "</div>"
      "</div>"


;


const char HTTP_CONFIG_GENERAL[] PROGMEM =
    
    "<div class='row justify-content-md-center' >"
    "<div class='col col-md-6'>"
    "<h4>General</h4>"
    "<form method='POST' action='saveConfigGeneral'>" 
    "<div class='form-check'>"
    "<input class='form-check-input' id='debugSerial' type='checkbox' name='debugSerial' {{checkedDebug}}>"
    "<label class='form-check-label' for='debugSerial'>Debug</label>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form></div>"
    "</div>";

const char HTTP_CONFIG_ZIGBEE[] PROGMEM =  
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config_zigbee}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'>"
    "<h4>Config Zigbee</h4>"
    "<div align='right'>"
    "<button type='button' onclick='cmd(\"Reset\");' class='btn btn-primary'>"
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='#FFFFFF' class='bi bi-bootstrap-reboot' viewBox='0 0 16 16'>"
      "<path d='M1.161 8a6.84 6.84 0 1 0 6.842-6.84.58.58 0 1 1 0-1.16 8 8 0 1 1-6.556 3.412l-.663-.577a.58.58 0 0 1 .227-.997l2.52-.69a.58.58 0 0 1 .728.633l-.332 2.592a.58.58 0 0 1-.956.364l-.643-.56A6.8 6.8 0 0 0 1.16 8z'/>"
      "<path d='M6.641 11.671V8.843h1.57l1.498 2.828h1.314L9.377 8.665c.897-.3 1.427-1.106 1.427-2.1 0-1.37-.943-2.246-2.456-2.246H5.5v7.352zm0-3.75V5.277h1.57c.881 0 1.416.499 1.416 1.32 0 .84-.504 1.324-1.386 1.324z'/>"
    "</svg>"
    " Reset"
    "</button> "
    "<button type='button' onClick=\"if (confirm('Are you sure ?')==true){cmd('ErasePDM');}else{return false;};\" class='btn btn-danger'>"
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='currentColor' class='bi bi-trash' viewBox='0 0 16 16'>"
      "<path d='M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z'/>"
      "<path d='M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H6a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1h3.5a1 1 0 0 1 1 1zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z'/>"
    "</svg>"
    " RAZ"
    "</button> "
    "</div>"
    "<h5>Parameters</h5>"
    "<span> @MAC coordinator : </span>{{macCoordinator}}<br>"
    "<span> Version coordinator : </span>{{versionCoordinator}}<br>"
    "<span> Network : </span>{{networkCoordinator}}<br>"
    "<label for='SetMaskChannel'>Set channel mask</label>"
    "<input class='form-control' id='SetMaskChannel' type='text' name='SetMaskChannel' value='{{SetMaskChannel}}'>"
    "<button type='button' onclick='cmd(\"SetChannelMask\",document.getElementById(\"SetMaskChannel\").value);' class='btn btn-primary'>Set Channel</button><br> "
    /*"<h5>Console</h5>"
    "<button type='button' onclick='cmd(\"ClearConsole\");document.getElementById(\"console\").value=\"\";' class='btn btn-primary'>Clear Console</button> "
    "<button type='button' onclick='cmd(\"GetVersion\");' class='btn btn-primary'>Get Version</button> "
    
    "<br>Raw datas : <br><textarea id='console' rows='16' style='width:100%;'></textarea>"*/
    "</div>"
    "</div>"
   /*"<script language='javascript'>"
    "$(document).ready(function() {"
    "logRefresh();});"
    "</script>";*/
    ;
const char HTTP_CONFIG_HORLOGE[] PROGMEM =
    
    "<div class='row justify-content-md-center' >"
    "<div class='col col-md-6'>"
    "<h4>Time</h4>"
    "<form method='POST' action='saveConfigHorloge'>"
    "<div class='form-check'>"
    "<h5>NTP Server</h5>"
    "<span> Datetime : </span>{{FormattedDate}}"
    "<br><label for='ntpserver'>NTP server URL</label>"
    "<input class='form-control' id='ntpserver' type='text' name='ntpserver' value='{{ntpserver}}'>"
    "<label for='timeoffset'>Time Offset</label>"
    "<input class='form-control' id='timeoffset' type='text' name='timeoffset' value='{{timeoffset}}'>"
    "<label for='timezone'>Time Zone</label>"
    "<input class='form-control' id='timezone' type='text' name='timezone' value='{{timezone}}'>"
    "<label for='epochtime'>UTC epoch date</label>"
    "<input class='form-control' id='epochtime' type='text' name='epochtime' value='{{epochtime}}'>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form></div>"
    "</div>";
const char HTTP_CONFIG_RULES[] PROGMEM = 
    "<h4>Config Rules</h4>"
    "<div align='right'>"
     /* "<a href='/addRule' type='button' class='btn btn-primary'>"
      "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='currentColor' class='bi bi-plus-circle' viewBox='0 0 16 16'>"
        "<path d='M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16'/>"
        "<path d='M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4'/>"
      "</svg><br>"
      " Add Rule"
      "</a> "*/
    "</div><br>"
    "<div class='row justify-content-md-center' >"
    "<div class='col col-md-6'>"
    "{{rulesList}}"
    "</div>"
    "</div>";

const char HTTP_CONFIG_LINKY[] PROGMEM =
    "<h4>Config Linky</h4>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigLinky'>"
    "<div class='form-check'>"
    "<h5>Device</h5>"
    "{{selectDevices}}"
    "<h5>Tarifs</h5>"
    "<label for='tarifAbo'>Tarif abonnement (€)</label>"
    "<input class='form-control' id='tarifAbo' type='text' name='tarifAbo' value='{{tarifAbo}}'>"
    "<label for='tarifCSPE'>Contribution au Service Public d'Electricité (CSPE) (€)</label>"
    "<input class='form-control' id='tarifCSPE' type='text' name='tarifCSPE' value='{{tarifCSPE}}'>"
    "<label for='tarifCTA'>Contribution Tarifaire d'Acheminement Electricité (CTA) (€)</label>"
    "<input class='form-control' id='tarifCTA' type='text' name='tarifCTA' value='{{tarifCTA}}'>"
    "<label for='tarifIdx1'>Tarif BASE/EAST  (€)</label>"
    "<input class='form-control' id='tarifIdx1' type='text' name='tarifIdx1' value='{{tarifIdx1}}'>"
    "<label for='tarifIdx2'>Tarif HC/EJPHN/BBRHCJB/EASF01 (€)</label>"
    "<input class='form-control' id='tarifIdx2' type='text' name='tarifIdx2' value='{{tarifIdx2}}'>"
    "<label for='tarifIdx3'>Tarif HP/EJPHPM/BBRHPJB/EASF02 (€)</label>"
    "<input class='form-control' id='tarifIdx3' type='text' name='tarifIdx3' value='{{tarifIdx3}}'>"
    "<label for='tarifIdx4'>Tarif BBRHCJW/EASF03  (€)</label>"
    "<input class='form-control' id='tarifIdx4' type='text' name='tarifIdx4' value='{{tarifIdx4}}'>"
    "<label for='tarifIdx5'>Tarif BBRHPJW/EASF04 (€)</label>"
    "<input class='form-control' id='tarifIdx5' type='text' name='tarifIdx5' value='{{tarifIdx5}}'>"
    "<label for='tarifIdx6'>Tarif BBRHCJR/EASF05 (€)</label>"
    "<input class='form-control' id='tarifIdx6' type='text' name='tarifIdx6' value='{{tarifIdx6}}'>"
    "<label for='tarifIdx7'>Tarif BBRHPJR/EASF06  (€)</label>"
    "<input class='form-control' id='tarifIdx7' type='text' name='tarifIdx7' value='{{tarifIdx7}}'>"
    "<label for='tarifIdx8'>Tarif EASF07 (€)</label>"
    "<input class='form-control' id='tarifIdx8' type='text' name='tarifIdx8' value='{{tarifIdx8}}'>"
    "<label for='tarifIdx9'>Tarif EASF08 (€)</label>"
    "<input class='form-control' id='tarifIdx9' type='text' name='tarifIdx9' value='{{tarifIdx9}}'>"
    "<label for='tarifIdx10'>Tarif EASF09 (€)</label>"
    "<input class='form-control' id='tarifIdx10' type='text' name='tarifIdx10' value='{{tarifIdx10}}'>"
    "<br>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form></div>"
    "</div>";

const char HTTP_CONFIG_GAZ[] PROGMEM =
    "<h4>Config Gaz</h4>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigGaz'>"
    "<div class='form-check'>"
    "<h5>Device</h5>"
    "{{selectDevices}}"
    "<h5>Parameters</h5>"
    "<label for='coeffGaz'>Impulsion coefficient</label>"
    "<input class='form-control' id='coeffGaz' type='text' name='coeffGaz' value='{{coeffGaz}}'>"
    "<label for='unitGaz'>Unit</label>"
    "<input class='form-control' id='unitGaz' type='text' name='unitGaz' value='{{unitGaz}}'>"
    "<h5>Tarif</h5>"
    "<label for='tarifGaz'>Tarif (€)</label>"
    "<input class='form-control' id='tarifGaz' type='text' name='tarifGaz' value='{{tarifGaz}}'>"
    "<br>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form></div>"
    "</div>";

const char HTTP_CONFIG_WATER[] PROGMEM =
    "<h4>Config Water</h4>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigWater'>"
    "<div class='form-check'>"
    "<h5>Device</h5>"
    "{{selectDevices}}"
    "<h5>Parameters</h5>"
    "<label for='coeffWater'>Impulsion coefficient</label>"
    "<input class='form-control' id='coeffWater' type='text' name='coeffWater' value='{{coeffWater}}'>"
    "<label for='unitWater'>Unit</label>"
    "<input class='form-control' id='unitWater' type='text' name='unitWater' value='{{unitWater}}'>"
    "<h5>Tarif</h5>"
    "<label for='tarifWater'>Tarif (€)</label>"
    "<input class='form-control' id='tarifWater' type='text' name='tarifWater' value='{{tarifWater}}'>"
    "<br>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form></div>"
    "</div>";

const char HTTP_CONFIG_MQTT[] PROGMEM =
    
    "<div class='row justify-content-md-center' >"
    "<div class='col col-md-6'>"
    "<h4>MQTT</h4>"
    "<form method='POST' action='saveConfigMQTT'>"   
    "<div class='form-check'>"
      "<input class='form-check-input' id='enableMqtt' type='checkbox' name='enableMqtt' {{checkedMqtt}}>"
      "<label class='form-check-label' for='enableMqtt'>Enable MQTT</label>"
    "</div>"
    "<div class='form-check'>"
      "<label for='servMQTT'>MQTT server</label>"
      "<input class='form-control' id='servMQTT' type='text' name='servMQTT' value='{{servMQTT}}'>"
      "<label for='portMQTT'>MQTT port</label>"
      "<input class='form-control' id='portMQTT' type='text' name='portMQTT' value='{{portMQTT}}'>"
      "<label for='clientIDMQTT'>MQTT Client ID</label>"
      "<input class='form-control' id='clientIDMQTT' type='text' name='clientIDMQTT' value='{{clientIDMQTT}}'>"
      "<label for='userMQTT'>MQTT username</label>"
      "<input class='form-control' id='userMQTT' type='text' name='userMQTT' value='{{userMQTT}}'>"
      "<label for='passMQTT'>MQTT password</label>"
      "<input class='form-control' id='passMQTT' type='password' name='passMQTT' value='{{passMQTT}}'>"
      "<label for='headerMQTT'>MQTT topic header</label>"
      "<input class='form-control' id='headerMQTT' type='text' name='headerMQTT' value='{{headerMQTT}}'>"
    "</div>"
    "<div class='form-check'>"
      "<input class='form-check-input' id='ha' type='radio' name='appliMQTT' value='HA' {{checkedHA}} onClick='document.getElementById(\"displayCustomMQTT\").style.display=\"none\";document.getElementById(\"headerMQTT\").value=\"homeassistant/sensor/\";'>"
      "<label class='form-check-label' for='ha'>Home-Assistant</label>"
    "</div>"
    "<div class='form-check'>"
      "<input class='form-check-input' id='TB' type='radio' name='appliMQTT' value='TB' {{checkedTB}} onClick='document.getElementById(\"displayCustomMQTT\").style.display=\"none\";document.getElementById(\"headerMQTT\").value=\"v1/gateway/telemetry\";'>"
      "<label class='form-check-label' for='TB'>ThingsBoard</label>"
    "</div>"
    "<div class='form-check'>"
      "<input class='form-check-input' id='custom' type='radio' name='appliMQTT' value='custom' onClick='document.getElementById(\"displayCustomMQTT\").style.display=\"block\";' {{checkedCustom}}>"
      "<label class='form-check-label' for='custom'>Custom</label>"
    "</div>"
    "<div class='form-floating' id='displayCustomMQTT' style='{{displayCustomMQTT}}'>"
      "<textarea class='form-control' name='customMQTTJson' placeholder='' id='customMQTTJson' style='min-height:200px;'>{{customMQTTJson}}</textarea>"
      "<label for='customMQTTJson'>Custom JSON</label>"
    "</div>"
    "<br><Strong>Connected : </strong><span id='mqttStatus'><img src='web/img/wait.gif' /></span>"
    "<br>"
 
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form></div>"
    "</div>"
    " <script>"

    "   function fetchStatus(){"
    "      $.ajax({"
    "        url: '/getMQTTStatus',"
    "        type: 'GET',"
    "        success:function(data) {"
    "         if (data=='1')"
    "          {"
    "            $('#mqttStatus').html('<img src=\"web/img/ok.png\" />');"
    "          }else{"
    "            $('#mqttStatus').html('<img src=\"web/img/nok.png\" />');"
    "          }"
    "        }"
    "      });"
    "    }"

    " $(document).ready(function() {"
    "     setTimeout(function(){fetchStatus();setInterval(fetchStatus,2000);},5000);"
    "    "
    " });"
    "  </script>"
    ;

const char HTTP_CONFIG_HTTP[] PROGMEM =
    
    "<div class='row justify-content-md-center' >"
    "<div class='col col-md-6'>"
    "<h4>Security</h4>"
    "<form method='POST' action='saveConfigHTTP'>"
    "<div class='form-check'>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='enableSecureHttp' type='checkbox' name='enableSecureHttp' {{checkedHttp}}>"
    "<label class='form-check-label' for='enableSecureHttp'>Enable HTTP authentification</label>"
    "</div>"
    "<label for='userHTTP'>HTTP username</label>"
    "<input class='form-control' id='userHTTP' type='text' name='userHTTP' value='{{userHTTP}}' style='{{userborder}}'>"
    "<label for='passHTTP'>HTTP password</label>"
    "<input class='form-control' id='passHTTP' type='password' name='passHTTP' value='{{passHTTP}}' style='{{passborder}}'>"
    "<br>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form>"
    "<div style='color:red'>{{error}}</div>"
    "</div>"
    "</div>";

const char HTTP_CONFIG_WEBPUSH[] PROGMEM =
    
    "<div class='row justify-content-md-center' >"
    "<div class='col col-md-6'>"
    "<h4>WebPush</h4>"
    "<form method='POST' action='saveConfigWebPush'>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='enableWebPush' type='checkbox' name='enableWebPush' {{checkedWebPush}}>"
    "<label class='form-check-label' for='enableWebPush'>Enable WebPush</label>"
    "</div>"
    "<label for='servWebPush'>Server HTTP</label>"
    "<input class='form-control' id='servWebPush' type='text' name='servWebPush' value='{{servWebPush}}' style='{{urlborder}}'>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='webPushAuth' type='checkbox' name='webPushAuth' {{checkedWebPushAuth}} onClick='toggleDiv(\"authWebPush\");'>"
    "<label class='form-check-label' for='webPushAuth'>Enable Authentification</label>"
    "</div>"
    "<div id='authWebPush' style='{{displayWebPushAuth}}'>"
    "<h5>Authentification</h5>"
    "<label for='userWebPush'>Username</label>"
    "<input class='form-control' id='userWebPush' type='text' name='userWebPush' value='{{userWebPush}}' style='{{userborder}}'>"
    "<label for='passWebPush'>password</label>"
    "<input class='form-control' id='passWebPush' type='password' name='passWebPush' value='{{passWebPush}}' style='{{passborder}}'>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form>"
    "<div style='color:red'>{{error}}</div>"
    
    
    "<h5>HTTP datas sent example</h5>"
    "Header : POST"
    "<br>Content-Type : JSON"
    "<br>Content :"
    "<br><pre><code>"
    "{"
    "   <br>&nbsp;&nbsp;\"IEEE\" : \"@mac\","  
    "   <br>&nbsp;&nbsp;\"cluster\" : \"decimal\","  
    "   <br>&nbsp;&nbsp;\"attribute\" : \"decimal\","  
    "   <br>&nbsp;&nbsp;\"value\" : \"decimal / string\""  
    "<br>}"
    "</code></pre>"
    "</div>"
    "</div>"

    ;

const char HTTP_CONFIG_MARSTEK[] PROGMEM =
    
    "<div class='row justify-content-md-center' >"
    "<div class='col col-md-6'>"
    "<h4>Smartmeter Marstek</h4>"
    "<form method='POST' action='saveConfigMarstek'>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='enableMarstek' type='checkbox' name='enableMarstek' {{checkedMarstek}} {{disableMarstek}}>"
    "<label class='form-check-label' for='enableMarstek'> Enable Marstek</label>"
    "</div>"
    "Smart meter (ZLinky): <strong>{{ZLinky}}</strong>"
    "<br><br><button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form>"
    
    "</div>"
    "</div>"

    ;

const char HTTP_CONFIG_UDPCLIENT[] PROGMEM =
    
    "<div class='row justify-content-md-center' >"
    "<div class='col col-md-6'>"
    "<h4>Client UDP</h4>"
    "<form method='POST' action='saveConfigUDPClient'>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='enableUDP' type='checkbox' name='enableUDP' {{checkedUDP}}>"
    "<label class='form-check-label' for='enableUDP'>Enable UDP</label>"
    "</div>"
    "<label for='servUDP'>Server UDP</label>"
    "<input class='form-control' id='servUDP' type='text' name='servUDP' value='{{servUDP}}' style='{{urlborder}}'>"    
    "<label for='portUDP'>Port UDP</label>"
    "<input class='form-control' id='portUDP' type='text' name='portUDP' value='{{portUDP}}' style='{{portborder}}'>"
    "Datas :<br>"
    "<div class='form-floating' id='displayCustomUDP' style='{{displayCustomUDP}}'>"
      "<textarea class='form-control' name='customUDPJson' placeholder='' id='customUDPJson' style='min-height:200px;'>{{customUDPJson}}</textarea>"
      "<label for='customUDPJson'>Custom JSON</label>"
    "</div>"

    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form>"
    "<div style='color:red'>{{error}}</div>"

  
    "</div>"
    "</div>"

    ;

const char HTTP_CONFIG_NOTIFICATION[] PROGMEM =
    "<h4>Config Notification</h4>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigNotification'>"
    "<h5>General</h5>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='enableNotif' type='checkbox' name='enableNotif' {{checkedNotif}}>"
    "<label class='form-check-label' for='enableNotif'>Enable Notification</label>"
    "</div>"
    "<h5>EMail</h5>"
    "<label for='servSMTP'>Server SMTP</label>"
    "<input class='form-control' id='servSMTP' type='text' name='servSMTP' value='{{servSMTP}}'>"
    "<label for='portSMTP'>Port SMTP</label>"
    "<input class='form-control' id='portSMTP' type='text' name='portSMTP' value='{{portSMTP}}'>"
    "<label for='userSMTP'>Username SMTP</label>"
    "<input class='form-control' id='userSMTP' type='text' name='userSMTP' value='{{userSMTP}}'>"
    "<label for='passSMTP'>Password SMTP</label>"
    "<input class='form-control' id='passSMTP' type='password' name='passSMTP' value='{{passSMTP}}'>"
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form></div>"
    "</div>";

const char HTTP_NETWORK[] PROGMEM =
    
    "<div class='row justify-content-md-center justify-content-sm-center' style='--bs-gutter-x: 0.3rem;'>"
    "<div class='col col-md-6'>"
    "<h4>Network status</h4>"
    "<div class='card'>"
    "<div class='card-header'>"
    "<svg xmlns='http://www.w3.org/2000/svg' style='width:24px;' width='24' height='24' fill='currentColor' class='bi bi-wifi' viewBox='0 0 16 16'>"
      "<path d='M15.384 6.115a.485.485 0 0 0-.047-.736A12.44 12.44 0 0 0 8 3C5.259 3 2.723 3.882.663 5.379a.485.485 0 0 0-.048.736.52.52 0 0 0 .668.05A11.45 11.45 0 0 1 8 4c2.507 0 4.827.802 6.716 2.164.205.148.49.13.668-.049'/>"
      "<path d='M13.229 8.271a.482.482 0 0 0-.063-.745A9.46 9.46 0 0 0 8 6c-1.905 0-3.68.56-5.166 1.526a.48.48 0 0 0-.063.745.525.525 0 0 0 .652.065A8.46 8.46 0 0 1 8 7a8.46 8.46 0 0 1 4.576 1.336c.206.132.48.108.653-.065m-2.183 2.183c.226-.226.185-.605-.1-.75A6.5 6.5 0 0 0 8 9c-1.06 0-2.062.254-2.946.704-.285.145-.326.524-.1.75l.015.015c.16.16.407.19.611.09A5.5 5.5 0 0 1 8 10c.868 0 1.69.201 2.42.56.203.1.45.07.61-.091zM9.06 12.44c.196-.196.198-.52-.04-.66A2 2 0 0 0 8 11.5a2 2 0 0 0-1.02.28c-.238.14-.236.464-.04.66l.706.706a.5.5 0 0 0 .707 0l.707-.707z'/>"
    "</svg>"
    " Wifi"
    "</div>"
    "<div class='card-body'>"
    "<div id='wifiConfig'>"
    "<strong>Enable : </strong>{{enableWifi}}"
    "<br><strong>Connected : </strong>{{connectedWifi}}"
    "<br><strong>SSID : </strong>{{ssidWifi}}"
    "<br><strong>Mode : </strong>{{modeWifi}}"
    "<br><strong>@IP : </strong>{{ipWifi}}"
    "<br><strong>@Mask : </strong>{{maskWifi}}"
    "<br><strong>@GW : </strong>{{GWWifi}}"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "</div><br>"
    "<div class='row justify-content-md-center justify-content-sm-center' style='--bs-gutter-x: 0.3rem;'>"
    "<div class='col col-md-6'><div class='card'><div class='card-header'>"
    "<svg style='width:24px;' width='24' height='24' viewBox='0 0 24 24' fill='none' xmlns='http://www.w3.org/2000/svg'>"
      "<path d='M12 16.75C11.8019 16.7474 11.6126 16.6676 11.4725 16.5275C11.3324 16.3874 11.2526 16.1981 11.25 16V11C11.25 10.8011 11.329 10.6103 11.4697 10.4697C11.6103 10.329 11.8011 10.25 12 10.25C12.1989 10.25 12.3897 10.329 12.5303 10.4697C12.671 10.6103 12.75 10.8011 12.75 11V16C12.7474 16.1981 12.6676 16.3874 12.5275 16.5275C12.3874 16.6676 12.1981 16.7474 12 16.75Z' fill='#000000'/>"
      "<path d='M12 9.25C11.8019 9.24741 11.6126 9.16756 11.4725 9.02747C11.3324 8.88737 11.2526 8.69811 11.25 8.5V8C11.25 7.80109 11.329 7.61032 11.4697 7.46967C11.6103 7.32902 11.8011 7.25 12 7.25C12.1989 7.25 12.3897 7.32902 12.5303 7.46967C12.671 7.61032 12.75 7.80109 12.75 8V8.5C12.7474 8.69811 12.6676 8.88737 12.5275 9.02747C12.3874 9.16756 12.1981 9.24741 12 9.25Z' fill='#000000'/>"
      "<path d='M12 21C10.22 21 8.47991 20.4722 6.99987 19.4832C5.51983 18.4943 4.36628 17.0887 3.68509 15.4442C3.0039 13.7996 2.82567 11.99 3.17294 10.2442C3.5202 8.49836 4.37737 6.89472 5.63604 5.63604C6.89472 4.37737 8.49836 3.5202 10.2442 3.17294C11.99 2.82567 13.7996 3.0039 15.4442 3.68509C17.0887 4.36628 18.4943 5.51983 19.4832 6.99987C20.4722 8.47991 21 10.22 21 12C21 14.387 20.0518 16.6761 18.364 18.364C16.6761 20.0518 14.387 21 12 21ZM12 4.5C10.5166 4.5 9.0666 4.93987 7.83323 5.76398C6.59986 6.58809 5.63856 7.75943 5.07091 9.12988C4.50325 10.5003 4.35473 12.0083 4.64411 13.4632C4.9335 14.918 5.64781 16.2544 6.6967 17.3033C7.7456 18.3522 9.08197 19.0665 10.5368 19.3559C11.9917 19.6453 13.4997 19.4968 14.8701 18.9291C16.2406 18.3614 17.4119 17.4001 18.236 16.1668C19.0601 14.9334 19.5 13.4834 19.5 12C19.5 10.0109 18.7098 8.10323 17.3033 6.6967C15.8968 5.29018 13.9891 4.5 12 4.5Z' fill='#000000'/>"
    "</svg>"
    " System Infos"
    "</div>"
    "<div class='card-body'>"
    "{{MQTT card}}"
    "{{Marstek card}}"

    "<Strong>Box temperature :</strong> {{Temperature}} °C<br>"
    "</div></div></div>"
    
    "</div>";

const char HTTP_ROOT[] PROGMEM =
    "<h4>Dashboard</h4>"
    "<div class='row' style='--bs-gutter-x: 0.3rem;'>"
    "<div class='col-sm-12'>"
    "<Select class='form-select form-select-lg mb-3' aria-label='.form-select-lg example' name='time' onChange=\"window.location.href='?time='+this.value\">"
    "<option value='hour' {{selectedHour}}>Hour</option>"
    "<option value='day' {{selectedDay}}>Day</option>"
    "<option value='month' {{selectedMonth}}>Month</option>"
    "<option value='year' {{selectedYear}}>Year</option>"
    "</select>"
    "</div>"
    "</div>"
    "<div class='row'  style='--bs-gutter-x: 0.3rem;'>"
    "<div class='col col-md-6'>"
    "<div class='card' >"
    "<div class='card-header'>Energy gauge</div>"
    "<div class='card-body' style='min-height:272px;'>"
    "<div id='power_gauge_global' style='height:230px;'></div>"
    "</div>"
    "</div>"
    "</div>"
    "<div class='col col-md-6'>"
    "<div class='card'>"
    "<div class='card-header'>Energy trend</div>"
    "<div class='card-body' style='min-height:272px;'>"
    "<div id='power_trend'></div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "<div class='row'  style='--bs-gutter-x: 0.3rem;'>"
    "{{dashboard}}" 
    "</div>"
    "{{javascript}}";

const char HTTP_DASHBOARD[] PROGMEM =
    "<h4>Dashboard</h4>"
    "<div class='row justify-content-md-center justify-content-sm-center' style='--bs-gutter-x: 0.3rem;'>"
    "{{dashboard}}" 
    "</div>"
    "{{javascript}}";

const char HTTP_ENERGY[] PROGMEM =
    "<h4>Energy status</h4>" 
    "<div class='row'>"
    "<div class='col-sm-12'>"
    "<Select class='form-select form-select-lg mb-3' aria-label='.form-select-lg example' name='time' onChange=\"window.location.href='?time='+this.value\">"
    "<option value='hour' {{selectedHour}}>Hour</option>"
    "<option value='day' {{selectedDay}}>Day</option>"
    "<option value='month' {{selectedMonth}}>Month</option>"
    "<option value='year' {{selectedYear}}>Year</option>"
    "</select>"
    "</div>"
    "</div>";

const char HTTP_ENERGY_LINKY[] PROGMEM =
    "<div class='row'>"
      "<div class='col-sm-12'>"
        "{{LinkyStatus}}"
      "</div>"
    "</div>"
    "<div class='row' style='--bs-gutter-x: 0.3rem;'>"
      "{{power_gauge}}"
      "<div class='col-lg-2'>"
        "<div class='card'>"
          "<div class='card-header'>Energy trend</div>"
          "<div class='card-body' style='min-height:272px;'>"
            "<div id='power_trend'></div>"
          "</div>"
        "</div>"
      "</div>"
      "<div class='col-lg-4'>"
        "<div class='card'>"
          "<div class='card-header'>Linky Datas</div>"
          "<div class='card-body' style='min-height:272px;'>"
            "<div id='power_data'></div>"
          "</div>"
        "</div>"
      "</div>"
    "</div>"
    "<div class='row' style='display:{{stylePowerChart}}'>"
    "<div class='col-sm-12'>"
    "<div class='card'>"
    "<div class='card-header'>Real Time Power chart</div>"
    "<div class='card-body'>"
    "<div id='power-chart'></div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "<div class='row'  style='--bs-gutter-x: 0.3rem;'>"
    "<div class='col-sm-12'>"
    "<div class='card'>"
    "<div class='card-header'>Energy Usage</div>"
    "<div class='card-body'>"
    "<div id='energy-chart'></div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>";

const char HTTP_ENERGY_GAZ[] PROGMEM =
    "<div class='row'>"
    "<div class='col-sm-12'>"
    "<div class='card'>"
    "<div class='card-header'>Gaz consumption</div>"
    "<div class='card-body'>"
    "<div id='gaz-chart'></div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>";

const char HTTP_ENERGY_WATER[] PROGMEM =
    "<div class='row'>"
    "<div class='col-sm-12'>"
    "<div class='card'>"
    "<div class='card-header'>Water consumption</div>"
    "<div class='card-body'>"
    "<div id='water-chart'></div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>";
const char HTTP_ENERGY_JAVASCRIPT[] PROGMEM =
    "{{javascript}}";

const char HTTP_CONFIG_WIFI[] PROGMEM =
   
    "<div class='row justify-content-md-center' >"
    "<div class='col col-md-6'>"
    "<h4>Config WiFi</h4>"
    "<form method='POST' action='saveWifi'>"
    /*"<div class='form-check'>"
    "<input class='form-check-input' id='wifiEnable' type='checkbox' name='wifiEnable' {{checkedWiFi}}>"
    "<label class='form-check-label' for='wifiEnable'>Enable</label>"
    "</div>"*/
    "<div class='form-group'>"
    "<label for='ssid'>SSID</label>"
    "<input class='form-control' id='ssid' type='text' name='WIFISSID' value='{{ssid}}' style='{{ssidborder}}'> <a onclick='scanNetwork();' class='btn btn-primary mb-2'>Scan</a><div id='networks'></div>"
    "</div>"
    "<div class='form-group'>"
    "<label for='pass'>Password</label>"
    "<input class='form-control' id='pass' type='password' name='WIFIpassword' value='{{password}}' style='{{passborder}}'>"
    "</div>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='DHCPEnable' type='checkbox' name='DHCPEnable' {{checkedDHCP}} onClick='toggleDiv(\"static\");'>"
    "<label class='form-check-label' for='DHCPEnable'>DHCP</label>"
    "</div>"
    "<div id='static' style='display:{{static}}'>"
    "<div class='form-group'>"
    "<label for='ip'>@IP</label>"
    "<input class='form-control' id='ip' type='text' name='ipAddress' value='{{ip}}' style='{{ipborder}}'>"
    "</div>"
    "<div class='form-group'>"
    "<label for='mask'>@Mask</label>"
    "<input class='form-control' id='mask' type='text' name='ipMask' value='{{mask}}' style='{{ipmask}}'>"
    "</div>"
    "<div class='form-group'>"
    "<label for='gateway'>@Gateway</label>"
    "<input type='text' class='form-control' id='gateway' name='ipGW' value='{{gw}}' style='{{ipgw}}'>"
    "</div>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save' onclick='document.getElementById(\"reboot\").style.display=\"block\";'>Save</button>"
    "</form>"
    "<div style='color:red'>{{error}}</div>"
    "<div style='color:red'>{{ipError}}</div>"
    "<div id='reboot' style='display:none;'><img src='web/img/wait.gif' /> Rebooting ...</div>"
    ;
const char HTTP_CREATE_DEVICE[] PROGMEM =
    "<h4>Create device file</h4>"
    "<div class='row justify-content-md-center' >"
    "<div class='col col-md-6'><form method='POST' action='saveFileDevice'>"
    "<div class='form-group'>"
    "<label for='filename'>@ mac</label>"
    "<input class='form-control' id='filename' type='text' name='filename' value=''> "
    "</div>"
    "<div class='form-group'>"
    " <label for='file'>Content</label>"
    " <textarea class='form-control' id='file' name='file' rows='20'>"
    "</textarea>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2' name='save' value='save'>Save</button>"
    "</form>";
const char HTTP_CREATE_TEMPLATE[] PROGMEM =
    "<h4>Create tp file</h4>"
    "<div class='row justify-content-md-center' >"
    "<div class='col col-md-6'><form method='POST' action='saveFileTemplates'>"
    "<div class='form-group'>"
    "<label for='filename'>File name</label>"
    "<input class='form-control' id='filename' type='text' name='filename' value=''> "
    "</div>"
    "<div class='form-group'>"
    " <label for='file'>Content</label>"
    " <textarea class='form-control' id='file' name='file' rows='20'>"
    "</textarea>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2' name='save' value='save'>Save</button>"
    "</form>";

const char HTTP_DEVICE[] PROGMEM =
    "<div class='col-sm-3'><div class='card'><div class='card-header'>Socket num : {{numero}}"
    "</div>"
    "<div class='card-body'>"
    "<strong>ID : {{id}}</strong>"
    "</br><strong>IP : {{ip}}</strong>"
    "</br><strong>MAC : {{mac}}</strong>"
    "<br>"
    "<div id='{{mac}}'>"
    "<img src='web/img/wait.gif' />"
    "</div>"
    "</div></div></div>";

const char HTTP_FOOTER[] PROGMEM =
    "<script language='javascript'>"
    "getFormattedDate();"
    "getAlert();"
    "</script>";

String footer()
{
  String result="";
  
  result +="<br><hr>";
  result +="<div align='center' style='font-size:12px;'>";
  result +=    "Copyright : LiXee 2025 - version : "+ String(VERSION);
  result +=  "</div>";
  result+=FPSTR(HTTP_FOOTER);

  return result;
}

String getMenuGeneralZigbee(String tmp, String selected)
{
  
  tmp.replace("{{menu_config_zigbee}}", FPSTR(HTTP_CONFIG_MENU_ZIGBEE));
  if (selected=="devices")
  {
    tmp.replace("{{menu_config_devices}}", "disabled");
  }else{
    tmp.replace("{{menu_config_devices}}", "");
  }
  if (selected=="config")
  {
    tmp.replace("{{menu_config_zigbee}}", "disabled");
  }else{
    tmp.replace("{{menu_config_zigbee}}", "");
  }
  return tmp;
}

/* ESP32-DEV
float temperatureReadFixed()
{
  SET_PERI_REG_BITS(SENS_SAR_MEAS_WAIT2_REG, SENS_FORCE_XPD_SAR, 3, SENS_FORCE_XPD_SAR_S);
  SET_PERI_REG_BITS(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_CLK_DIV, 10, SENS_TSENS_CLK_DIV_S);
  CLEAR_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP);
  CLEAR_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_DUMP_OUT);
  SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP_FORCE);
  SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP);
  ets_delay_us(100);
  SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_DUMP_OUT);
  ets_delay_us(5);
  float temp_f = (float)GET_PERI_REG_BITS2(SENS_SAR_SLAVE_ADDR3_REG, SENS_TSENS_OUT, SENS_TSENS_OUT_S);
  float temp_c = (temp_f - 32) / 1.8;

  float temp_c = 60;
  return temp_c;
}*/

bool isValidIP(const String &ip) {
    int sections = 0;     // Compte les sections séparées par des points
    int start = 0;        // Indice de début de la section actuelle

    for (int i = 0; i <= ip.length(); i++) {
        // Vérifie si nous avons atteint la fin de la section
        if (i == ip.length() || ip[i] == '.') {
            if (i == start) {
                return false; // Aucun numéro entre les points
            }

            String part = ip.substring(start, i);
            if (part.length() > 1 && part[0] == '0') {
                return false; // Pas de partie avec zéro(s) en tête (ex: "01")
            }

            for (char c : part) {
                if (!isDigit(c)) {
                    return false; // Chaque caractère doit être un chiffre
                }
            }

            int num = part.toInt();
            if (num < 0 || num > 255) {
                return false; // Chaque partie doit être entre 0 et 255
            }

            sections++;
            start = i + 1; // Met à jour le début pour la prochaine section
        }
    }

    return sections == 4; // Une adresse IPv4 doit avoir exactement 4 sections
}

float temperatureReadFixed()
{
  float result = 0;
  temp_sensor_read_celsius(&result);

  return result;
}

bool TemplateExist(int deviceId)
{
  if (deviceId>0)
  {
    //String path = "/tp/" + (String)deviceId + ".json";
    const char* path ="/tp/";
    const char* extension =".json";
    char name_with_extension[64];
    strcpy(name_with_extension,path);
    strcat(name_with_extension,String(deviceId).c_str());
    strcat(name_with_extension,extension);
    File tpFile = LittleFS.open(name_with_extension, FILE_READ);
    if (!tpFile || tpFile.isDirectory())
    {
      return false;
    }
    tpFile.close();
    return true;
  }else{
    return false;
  }
}

Template * GetTemplate(int deviceId, String model)
{
  Template *t = (Template *) ps_malloc(sizeof(Template));
  if (deviceId>0)
  {
    //String path = "/tp/" + String(deviceId) + ".json";   
    const char* path ="/tp/";
    const char* extension =".json";
    char name_with_extension[64];
    strcpy(name_with_extension,path);
    strcat(name_with_extension,String(deviceId).c_str());
    strcat(name_with_extension,extension);
    File tpFile = LittleFS.open(name_with_extension, FILE_READ);
    if (!tpFile || tpFile.isDirectory())
    {
      DEBUG_PRINTLN(F("failed open"));
      return t;
    }
    else
    {
      DynamicJsonDocument temp(MAXHEAP);
      deserializeJson(temp, tpFile);
      tpFile.close();
      int i = 0;
      const char *tmp;

      if (temp.containsKey(model))
      {
        
        JsonArray StatusArray = temp[model][0][F("status")].as<JsonArray>();
        for (JsonVariant v : StatusArray)
        {
          
          tmp = temp[model][0][F("status")][i][F("name")];
          strlcpy(t->e[i].name, tmp, sizeof(t->e[i].name));
          t->e[i].cluster = (int)strtol(temp[model][0][F("status")][i][F("cluster")], 0, 16);
          t->e[i].attribute = (int)temp[model][0][F("status")][i][F("attribut")];
          if (temp[model][0][F("status")][i][F("type")])
          {
            strlcpy(t->e[i].type, temp[model][0][F("status")][i][F("type")], sizeof(t->e[i].type));
          }
          else
          {
            strlcpy(t->e[i].type, "", sizeof(t->e[i].type));
          }
          //MQTT
          if (temp[model][0][F("status")][i][F("mqtt_device_class")])
          {
            strlcpy(t->e[i].mqtt_device_class, temp[model][0][F("status")][i][F("mqtt_device_class")], sizeof(t->e[i].mqtt_device_class));
          }
          else
          {
            strlcpy(t->e[i].mqtt_device_class, "null", sizeof(t->e[i].mqtt_device_class));
          }
          if (temp[model][0][F("status")][i][F("mqtt_state_class")])
          {
            strlcpy(t->e[i].mqtt_state_class, temp[model][0][F("status")][i][F("mqtt_state_class")], sizeof(t->e[i].mqtt_state_class));
          }
          else
          {
            strlcpy(t->e[i].mqtt_state_class, "null", sizeof(t->e[i].mqtt_state_class));
          }
          if (temp[model][0][F("status")][i][F("mqtt_icon")])
          {
            strlcpy(t->e[i].mqtt_icon, temp[model][0][F("status")][i][F("mqtt_icon")], sizeof(t->e[i].mqtt_icon));
          }
          else
          {
            strlcpy(t->e[i].mqtt_icon, "", sizeof(t->e[i].mqtt_icon));
          }



          if (temp[model][0][F("status")][i][F("coefficient")])
          {
            t->e[i].coefficient = (float)temp[model][0][F("status")][i][F("coefficient")];
          }
          else
          {
            t->e[i].coefficient = 1;
          }
          if (temp[model][0][F("status")][i][F("unit")])
          {
            strlcpy(t->e[i].unit, temp[model][0][F("status")][i][F("unit")], sizeof(t->e[i].unit));
          }
          else
          {
            strlcpy(t->e[i].unit, "", sizeof(t->e[i].unit));
          }
        
          if (temp[model][0][F("status")][i][F("visible")].as<int>() == 1)
          {
            t->e[i].visible = 1;
          }
          else
          {
            t->e[i].visible = 0;
          }

          if (temp[model][0][F("status")][i][F("mode")])
          {
            strlcpy(t->e[i].mode, temp[model][0][F("status")][i][F("mode")], sizeof(t->e[i].mode));
          }
          else
          {
            strlcpy(t->e[i].mode, "", sizeof(t->e[i].mode));
          }

          if (temp[model][0][F("status")][i][F("jauge")])
          {
            strlcpy(t->e[i].typeJauge, temp[model][0][F("status")][i][F("jauge")], sizeof(t->e[i].typeJauge));
            t->e[i].jaugeMin = temp[model][0][F("status")][i][F("min")].as<int>();
            t->e[i].jaugeMax = temp[model][0][F("status")][i][F("max")].as<int>();
          }
          else
          {
            strlcpy(t->e[i].typeJauge, "", sizeof(t->e[i].typeJauge));
          }
          i++;
          vTaskDelay(1);
        }
        t->StateSize = i;
        i = 0;
        JsonArray ActionArray = temp[model][0][F("action")].as<JsonArray>();
        for (JsonVariant v : ActionArray)
        {

          tmp = temp[model][0][F("action")][i][F("name")];
          strlcpy(t->a[i].name, tmp, sizeof(t->a[i].name));
          t->a[i].command = (int)temp[model][0][F("action")][i][F("command")];
          t->a[i].value = (int)temp[model][0][F("action")][i][F("value")];
          if (temp[model][0][F("action")][i][F("visible")].as<int>() == 1)
          {
            t->a[i].visible = 1;
          }
          else
          {
            t->a[i].visible = 0;
          }
          i++;
          vTaskDelay(1);
        }
        t->ActionSize = i;
        // tmp = temp[model][0]["bind"];
        // strlcpy(t.bind,tmp,sizeof(50));
        return t;
      } else if (temp.containsKey("default"))       
      {
        JsonArray StatusArray = temp[F("default")][0][F("status")].as<JsonArray>();
        for (JsonVariant v : StatusArray)
        {

          tmp = temp[F("default")][0][F("status")][i][F("name")];
          strlcpy(t->e[i].name, tmp, sizeof(t->e[i].name));
          t->e[i].cluster = (int)strtol(temp[F("default")][0][F("status")][i][F("cluster")], 0, 16);
          t->e[i].attribute = (int)temp[F("default")][0][F("status")][i][F("attribut")];
          if (temp[F("default")][0][F("status")][i][F("type")])
          {
            strlcpy(t->e[i].type, temp[F("default")][0][F("status")][i][F("type")], sizeof(t->e[i].type));
          }
          else
          {
            strlcpy(t->e[i].type, "", sizeof(t->e[i].type));
          }

          //MQTT
          if (temp[F("default")][0][F("status")][i][F("mqtt_device_class")])
          {
            strlcpy(t->e[i].mqtt_device_class, temp[F("default")][0][F("status")][i][F("mqtt_device_class")], sizeof(t->e[i].mqtt_device_class));
          }
          else
          {
            strlcpy(t->e[i].mqtt_device_class, "", sizeof(t->e[i].mqtt_device_class));
          }
          if (temp[F("default")][0][F("status")][i][F("mqtt_state_class")])
          {
            strlcpy(t->e[i].mqtt_state_class, temp[F("default")][0][F("status")][i][F("mqtt_state_class")], sizeof(t->e[i].mqtt_state_class));
          }
          else
          {
            strlcpy(t->e[i].mqtt_state_class, "", sizeof(t->e[i].mqtt_state_class));
          }
          if (temp[F("default")][0][F("status")][i][F("mqtt_icon")])
          {
            strlcpy(t->e[i].mqtt_icon, temp[F("default")][0][F("status")][i][F("mqtt_icon")], sizeof(t->e[i].mqtt_icon));
          }
          else
          {
            strlcpy(t->e[i].mqtt_icon, "", sizeof(t->e[i].mqtt_icon));
          }



          if (temp[F("default")][0][F("status")][i][F("coefficient")])
          {
            t->e[i].coefficient = (float)temp[F("default")][0][F("status")][i][F("coefficient")];
          }
          else
          {
            t->e[i].coefficient = 1;
          }
          if (temp[F("default")][0][F("status")][i][F("unit")])
          {
            strlcpy(t->e[i].unit, temp[F("default")][0][F("status")][i][F("unit")], sizeof(t->e[i].unit));
          }
          else
          {
            strlcpy(t->e[i].unit, "", sizeof(t->e[i].unit));
          }
          if (temp[F("default")][0][F("status")][i][F("visible")].as<int>() == 1)
          {
            t->e[i].visible = 1;
          }
          else
          {
            t->e[i].visible = 0;
          }
          if (temp[F("default")][0][F("status")][i][F("jauge")])
          {
            strlcpy(t->e[i].typeJauge, temp[F("default")][0][F("status")][i][F("jauge")], sizeof(t->e[i].typeJauge));
            t->e[i].jaugeMin = temp[F("default")][0][F("status")][i][F("min")].as<int>();
            t->e[i].jaugeMax = temp[F("default")][0][F("status")][i][F("max")].as<int>();
          }
          else
          {
            strlcpy(t->e[i].typeJauge, "", sizeof(t->e[i].typeJauge));
          }
          i++;
          vTaskDelay(1);
        }
        t->StateSize = i;
        i = 0;
        JsonArray ActionArray = temp[F("default")][0][F("action")].as<JsonArray>();
        for (JsonVariant v : ActionArray)
        {

          tmp = temp[F("default")][0][F("action")][i][F("name")];
          strlcpy(t->a[i].name, tmp, sizeof(t->a[i].name));
          t->a[i].command = (int)temp[F("default")][0][F("action")][i][F("command")];
          t->a[i].value = (int)temp[F("default")][0][F("action")][i][F("value")];
          if (temp[F("default")][0][F("action")][i][F("visible")].as<int>() == 1)
          {
            t->a[i].visible = 1;
          }
          else
          {
            t->a[i].visible = 0;
          }
          i++;
          vTaskDelay(1);
        }
        t->ActionSize = i;
      }
      else
      {
        t->StateSize = 0;
        t->ActionSize = 0;
      }
      return t;
      
    }
    
  }
  return t;
}

bool existDashboard(String inifile)
{
  String tmp = ini_read(inifile, "dashboard", "enable");
  return tmp.toInt();
}

String getAliasDashboard(String inifile)
{
  String tmp = ini_read(inifile, "dashboard", "alias");
  return tmp;
}

String createGaugePower(String div, String min, String max, String label)
{
  String result = "";
  result += "var Gauge" + div + " = new JustGage({";
  result += "id: 'status_" + div + "',";
  result += F("value: 0,");
  result += "min: " + min + ",";
  result += "max: " + max + ",";
  result += F("title: 'Target',");
  result += "label:'" + label + "',";
  result += F("gaugeWidthScale: 0.6,");
  result += F("pointer: true,");
  result += F("pointerOptions: {");
  result += F("toplength: 10,");
  result += F("bottomlength: 10,");
  result += F("bottomwidth: 2");
  result += F("},");
  result += F("humanFriendly: true,");
  result += F("relativeGaugeSize: true,");
  result += F("refreshAnimationTime: 1000");
  result += F("});");

  return result;
}


String createGaugeDashboard(String div, String i, String min, String max, String label)
{
  String result = "";
  result += "var Gauge" + div  + i + " = new JustGage({";
  result += "id: 'gauge_" + div + i +"',";
  result += F("value: 0,");
  result += "min: " + min + ",";
  result += "max: " + max + ",";
  result += F("height: 200,");
  result += F("title: 'Target',");
  result += "label:'" + label + "',";
  result += F("gaugeWidthScale: 0.6,");
  result += F("pointer: true,");
  result += F("pointerOptions: {");
  result += F("toplength: 10,");
  result += F("bottomlength: 10,");
  result += F("bottomwidth: 2");
  result += F("},");
  result += F("humanFriendly: true,");
  result += F("relativeGaugeSize: true,");
  result += F("refreshAnimationTime: 1000");
  result += F("});");

  return result;
}

String createBaterryDashboard(String div, String i, String min, String max, String label)
{
  String result = "";
  result += "var Gauge" + div + i + " = new JustGage({";
  result += "id: 'gauge_" + div + i + "',";
  result += F("value: 0,");
  result += "min: " + min + ",";
  result += "max: " + max + ",";
  result += F("title: 'Target',");
  result += "label:'" + label + "',";
  result += F("gaugeWidthScale: 0.6,");
  result += F("pointer: true,");
  result += F("pointerOptions: {");
  result += F("toplength: 10,");
  result += F("bottomlength: 10,");
  result += F("bottomwidth: 2");
  result += F("},");
  result += F("customSectors: {");
  result +=    F(" percents: true,");
  result +=    F(" ranges: [{");
  result +=      F("   color : '#ff3b30',");
  result +=      F("   lo : 0,");
  result +=      F("   hi : 20");
  result +=      F(" },{");
  result +=      F("  color : '#f39c12',");
  result +=      F("  lo : 21,");
  result +=      F("  hi : 50");
  result +=      F(" },{");
  result +=      F("  color : '#43bf58',");
  result +=      F("  lo : 51,");
  result +=      F("  hi : 100");
  result +=    F(" }]");
  result += F(" },");
  result += F("humanFriendly: true,");
  result += F("relativeGaugeSize: true,");
  result += F("refreshAnimationTime: 1000");
  result += F("});");

  return result;
}
// String CreateTimeGauge(String div,String IEEE,String cluster, String attr,String type, String coefficient)
String CreateTimeGauge(String div)
{
  String result = "";
  result += "function refreshGauge" + div + "(IEEE,cluster,attr,type,coefficient)";
  result += F("{");
  result += F("var xhr = getXhr();");
  result += F("xhr.onreadystatechange = function(){");
  result += F("if(xhr.readyState == 4 ){");
  result += F("leselect = xhr.responseText;");
  result += "Gauge" + div + ".refresh(leselect);";
  result += "setTimeout(function(){ refreshGauge" + div + "(IEEE,cluster,attr,type,coefficient); }, 60000);";
  result += F("}");
  result += F("};");
  result += F("xhr.open('GET','loadGaugeDashboard?IEEE='+escape(IEEE)+'&cluster='+escape(cluster)+'&attribute='+escape(attr)+'&type='+escape(type)+'&coefficient='+escape(coefficient),true);");
  result += F("xhr.setRequestHeader('Content-Type','application/html');");
  result += F("xhr.send();");
  result += F("};");

  return result;
}

String createPowerGraph(String IEEE)
{
  String result = "";
  result += F("powerChart = Morris.Bar({");
  result += F(" element: 'power-chart',");
  result += F("data: [],");
  result += F("xkey: 'y',");

  if ((ConfigGeneral.LinkyMode == 2 ) || (ConfigGeneral.LinkyMode == 3 ) || (ConfigGeneral.LinkyMode == 7 ))
  {
    result += F(" ykeys: ['1295','2319','2575'],");
    result += F(" labels: ['Power Ph1(VA)','Power Ph2(VA)','Power Ph3(VA)'],");
  }else{
    result += F(" ykeys: ['1295'],");
    result += F(" labels: ['Power (VA)'],");
  }
  result += F(" barColors: ['#1e88e5','#5dade2','#85c1e9'],");
  result += F(" resize: true,");
  result += F(" xLabelAngle: 70,");
  result += F(" stacked: true,");
  result += F(" redraw: true,");
  result += F(" dataLabels: false,");
  result += F(" showZero: false,");
  result += F(" animate: false,");
  result += F(" });");

  return result;
}

String createEnergyGraph(String IEEE, String Type, String barColor)
{
  String result = "";
  String sep = "";
  result = Type;
  result += F("Chart = Morris.Bar({element: '");
  result += Type;
  result += F("-chart',data: [],xkey: 'y',");
  // list attr
  result += F("ykeys: [");
  String JsonEuros;
  JsonEuros= "{";
  int cntsection;
  int arrayLength = sizeof(section) / sizeof(section[0]);
  if (Type=="energy")
  {
    int i = 0;
    for (cntsection=0 ; cntsection <arrayLength; cntsection++)
    {
      if (i > 0)
      {
        sep = ",";
      }
      else
      {
        sep = "";
      }
      JsonEuros += sep + "\"" + String(section[cntsection]) + "\":{\"name\":\"" + GetNameStatus(97, "0702", String(section[cntsection]).toInt(), "ZLinky_TIC") + "\",\"coeff\":\"1\",\"price\":" + getTarif(String(section[cntsection]).toInt(),"energy") +",\"abo\":"+ConfigGeneral.tarifAbo+",\"taxe\":"+ConfigGeneral.tarifCSPE+",\"unit\":\"Wh\"}";
      result += sep + String(section[cntsection]);
      i++;
    }
    
  }else if (Type=="gaz")
  {
      JsonEuros += "\"0\":{\"name\":\"Gaz\",\"coeff\":\""+String(ConfigGeneral.coeffGaz)+"\",\"price\":" + getTarif(0,"gaz") + ",\"unit\":\""+String(ConfigGeneral.unitGaz)+"\"}";
      result += "0"; 
  }else if (Type=="water")
  {
    JsonEuros += sep + "\"0\":{\"name\":\"Water\",\"coeff\":\""+String(ConfigGeneral.coeffWater)+"\",\"price\":" + getTarif(0,"water") + ",\"unit\":\""+String(ConfigGeneral.unitWater)+"\"}";
    result += "0";
  }
  JsonEuros += "}";
  result += F("],");
  // list name
  result += F("labels: [");
  result += F("],");
  result += F("barColors: ");
  result += barColor;
  result += F(",");
  result += F("barWidth: '3px',");
  result += F("resize: true,");
  result += F("redraw: true,");
  result += F(" xLabelAngle: 70,");
  result += F("stacked: true,");
  result += F(" dataLabels: false,");
  result += F(" animate: false,");
  result += F("hoverCallback: function (index, options, content, row) {");
  result += F("return getLabelEnergy('");
  result += JsonEuros;
  result += F("',row,");
  result += barColor;
  result += F(",options,index);");
  result += F("}");
  result += F("});");
  
  return result;
}

void handleNotFound(AsyncWebServerRequest *request)
{

  String message = F("File Not Found\n\n");
  message += F("URI: ");
  // message += serverWeb.uri();
  message += request->url();
  message += F("\nMethod: ");
  // message += (serverWeb.method() == HTTP_GET) ? "GET" : "POST";
  message += (request->method() == HTTP_GET) ? "GET" : "POST";
  message += F("\nArguments: ");
  // message += serverWeb.args();
  message += request->args();
  message += F("\n");

  for (uint8_t i = 0; i < request->args(); i++)
  {
    message += " " + request->argName(i) + ": " + request->arg(i) + "\n";
  }

  request->send(404, F("text/plain"), message);
  // serverWeb.send(404, F("text/plain"), message);
  /*for (uint8_t i = 0; i < serverWeb.args(); i++) {
    message += " " + serverWeb.argName(i) + ": " + serverWeb.arg(i) + "\n";
  }

  serverWeb.send(404, F("text/plain"), message);*/
}

/*void handleRoot(AsyncWebServerRequest *request)
{
  AsyncResponseStream *response = request->beginResponseStream("text/html");
  String result;
  result = F("<html>");
  result += FPSTR(HTTP_HEADERGRAPH);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_ROOT);
  result.replace("{{FormattedDate}}", FormattedDate);
  
  int i = 0;
  String time;
  int paramsNr = request->params();
  if (paramsNr > 0)
  {
    time = request->arg(i);
  }
  else
  {
    time = "hour";
  }

  if (time == "hour")
  {
    result.replace("{{selectedHour}}", F("selected"));
    result.replace("{{selectedDay}}", F(""));
    result.replace("{{selectedMonth}}", F(""));
    result.replace("{{selectedYear}}", F(""));
  }
  else if (time == "day")
  {
    result.replace("{{selectedHour}}", F(""));
    result.replace("{{selectedDay}}", F("selected"));
    result.replace("{{selectedMonth}}", F(""));
    result.replace("{{selectedYear}}", F(""));
  }
  else if (time == "month")
  {
    result.replace("{{selectedHour}}", F(""));
    result.replace("{{selectedDay}}", F(""));
    result.replace("{{selectedMonth}}", F("selected"));
    result.replace("{{selectedYear}}", F(""));
  }
  else if (time == "year")
  {
    result.replace("{{selectedHour}}", F(""));
    result.replace("{{selectedDay}}", F(""));
    result.replace("{{selectedMonth}}", F(""));
    result.replace("{{selectedYear}}", F("selected"));
  }
  response->print(result);
  String dashboard = "";
  String js = "";
  File root = LittleFS.open("/db");
  File file = root.openNextFile();
  while (file)
  {
    String tmp = file.name();
    if (tmp.substring(16) == ".json")
    {
      if (existDashboard(tmp))
      {
        int ShortAddr = GetShortAddr(file.name());
        int DeviceId = GetDeviceId(file.name());
        String model;
        model = GetModel(file.name());
        dashboard += F("<div class='col-sm-3'><div class='card'><div class='card-header'>");
        String alias = getAliasDashboard(file.name());

        if (alias != "null")
        {
          dashboard += F("<strong>");
          dashboard += alias;
          dashboard += F("</strong>");
          dashboard += F("<br>(@Mac : ");
          dashboard += tmp.substring(0, 16);
          dashboard += F(")");
        }
        else
        {
          dashboard += F("@Mac : ");
          dashboard += tmp.substring(0, 16);
        }
        dashboard += F("</div>");
        dashboard += F("<div class='card-body'>");
        // Get status and action from json

        if (TemplateExist(DeviceId))
        {
          Template t;
          t = GetTemplate(DeviceId, model);
          // toutes les propiétés
          dashboard += F("<div id='status_");
          dashboard += (String)ShortAddr;
          dashboard += F("'>");

          for (int i = 0; i < t->StateSize; i++)
          {
            if (t->e[i].visible)
            {
              if (String(t->e[i].typeJauge) == "gauge")
              {
                js += createGaugeDashboard((String)ShortAddr, (String)i, String(t->e[i].jaugeMin), String(t->e[i].jaugeMax), t->e[i].unit);
                js += CreateTimeGauge((String)ShortAddr + (String)i);
                js += "refreshGauge" + (String)ShortAddr + (String)i + "('" + tmp.substring(0, 16) + "'," + t->e[i].cluster + "," + t->e[i].attribute + ",'" + t->e[i].type + "'," + t->e[i].coefficient + ");";
              }
              else
              {
                dashboard += t->e[i].name;
                dashboard += " : <span id='";
                dashboard += F("label_");
                dashboard += (String)ShortAddr;
                dashboard += F("_");
                dashboard += t->e[i].cluster;
                dashboard += F("_");
                dashboard += t->e[i].attribute;
                dashboard += F("'>");
                dashboard += GetValueStatus(file.name(), t->e[i].cluster, t->e[i].attribute, (String)t->e[i].type, t->e[i].coefficient, (String)t->e[i].unit);
                dashboard += F("</span><br>");
                js += "refreshLabel('"+String(file.name())+"','"+(String)ShortAddr+"',"+t->e[i].cluster+","+t->e[i].attribute+",'"+(String)t->e[i].type+"',"+t->e[i].coefficient+",'"+(String)t->e[i].unit+"');";
              }
            }
          }
          dashboard += F("</div>");
          dashboard += F("<div id='action_");
          dashboard += (String)ShortAddr;
          dashboard += F("'>");
          // toutes les actions

          for (int i = 0; i < t->ActionSize; i++)
          {
            if (t->a[i].visible)
            {
              dashboard += F("<button onclick=\"ZigbeeAction(");
              dashboard += ShortAddr;
              dashboard += ",";
              dashboard += t->a[i].command;
              dashboard += ",";
              dashboard += t->a[i].value;
              dashboard += ");\" class='btn btn-primary mb-2'>";
              dashboard += t->a[i].name;
              dashboard += F("</button>");
            }
          }
          dashboard += F("</div>");
        }
        dashboard += F("</div></div></div>");
      }
    }
    file = root.openNextFile();
  }
  file.close();
  root.close();
  result=F("<div class='row'>");
  result+=dashboard; 
  result+=F("</div>");
  response->print(result);

  String javascript = "";
  javascript = F("<script language='javascript'>");
  javascript += F("$(document).ready(function() {");
  javascript += F("loadPowerGaugeAbo('");
  javascript += String(ConfigGeneral.ZLinky);
  javascript += F("','1295','");
  javascript += time;
  javascript += F("');");
  javascript += F("refreshDashboard('");
  javascript += String(ConfigGeneral.ZLinky);
  javascript += F("','1295','");
  javascript += time;
  javascript += F("');");
  javascript += js;
  javascript += F("});");

  javascript += F("</script>");
  response->print(javascript);

  result = FPSTR(HTTP_FOOTER);
  result += F("</html>");
  response->print(result);
  request->send(response);

}*/



void handleRoot(AsyncWebServerRequest *request)
{

  if (!ConfigGeneral.firstStart)
  {
    String path = "configGeneral.json";
    ConfigGeneral.firstStart=1;
    config_write(path, "firstStart", "1");
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/configWiFi"));
    request->send(response);
  }else{
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/dashboard"));
    request->send(response);
  }

}

void handleDashboard(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADERGRAPH);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_DASHBOARD);
  result +=footer();
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);

  String time;

  String dashboard = "";
  String js = "";
  File root = LittleFS.open("/db");
  File file = root.openNextFile();
  int exist = 0;
  while (file)
  {
    String tmp = file.name();
    if (tmp.substring(16) == ".json")
    {
      int ShortAddr = GetShortAddr(file.name());
      int DeviceId = GetDeviceId(file.name());
      String model;
      model = GetModel(file.name());
      dashboard += F("<div class='col-md-auto col-sm-auto'>");
      dashboard += F("<div class='card' style='min-width:380px;'>");
      dashboard += F("<div class='card-header' style='font-size:12px;font-weight:bold;color:#FFF;background-color:#007bc6;'>");
      String alias = getAliasDashboard(file.name());

      if (alias != "null")
      {
        dashboard += F("<strong>");
        dashboard += alias;
        dashboard += F("</strong>");
        dashboard += F("<br>(@Mac : ");
        dashboard += tmp.substring(0, 16);
        dashboard += F(")");
      }
      else
      {
        dashboard += F("@Mac : ");
        dashboard += tmp.substring(0, 16);
      }
      dashboard += F("</div>");
      dashboard += F("<div class='card-body'>");
      // Get status and action from json

      if (TemplateExist(DeviceId))
      {
        Template *t;
        t = GetTemplate(DeviceId, model);
        // toutes les propiétés
        dashboard += F("<div id='status_");
        dashboard += (String)ShortAddr;
        dashboard += F("'>");

        for (int i = 0; i < t->StateSize; i++)
        {
          if (t->e[i].visible)
          {

            if (String(t->e[i].typeJauge) == "gauge")
            {
              exist++;
              dashboard += "<div id='gauge_";
              dashboard += (String)ShortAddr+String(i);
              dashboard += F("' style='height:150px;'>");
              dashboard += F("<div align='center' style='font-size:12px;margin-top:-70px;'>");
              dashboard += String(t->e[i].name);
              dashboard += F("</div>");
              dashboard += F("</div>");
              js += createGaugeDashboard((String)ShortAddr, (String)i, String(t->e[i].jaugeMin), String(t->e[i].jaugeMax), t->e[i].unit);
              js += CreateTimeGauge((String)ShortAddr + (String)i);
              js += "refreshGauge" + (String)ShortAddr + (String)i + "('" + tmp.substring(0, 16) + "'," + t->e[i].cluster + "," + t->e[i].attribute + ",'" + t->e[i].type + "'," + t->e[i].coefficient + ");";
            }
            else if(String(t->e[i].typeJauge) == "battery")
            {
              exist++;
              dashboard += "<div id='gauge_";
              dashboard += (String)ShortAddr+String(i);
              dashboard += F("' style='height:150px;'>");
              dashboard += F("<div align='center' style='font-size:12px;margin-top:-70px;'>");
              dashboard += String(t->e[i].name);
              dashboard += F("</div>");
              dashboard += F("</div>");
              js += createBaterryDashboard((String)ShortAddr, (String)i, String(t->e[i].jaugeMin), String(t->e[i].jaugeMax), t->e[i].unit);
              js += CreateTimeGauge((String)ShortAddr + (String)i);
              js += "refreshGauge" + (String)ShortAddr + (String)i + "('" + tmp.substring(0, 16) + "'," + t->e[i].cluster + "," + t->e[i].attribute + ",'" + t->e[i].type + "'," + t->e[i].coefficient + ");";
            }else if(String(t->e[i].typeJauge) == "text")
            {
              exist++;
              dashboard +=F("<div id='text_");
              dashboard += (String)ShortAddr+String(i);
              dashboard += F("' style='text-align:center;font-size:12px;'>");
              dashboard += t->e[i].name;
              dashboard += F("<br>");
              dashboard += "<span id='";
              dashboard += F("label_");
              dashboard += (String)ShortAddr;
              dashboard += F("_");
              dashboard += t->e[i].cluster;
              dashboard += F("_");
              dashboard += t->e[i].attribute;
              dashboard += F("' style='font-size:24px;font-family :\"Courier New\", Courier, monospace;'>");
              dashboard += GetValueStatus(file.name(), t->e[i].cluster, t->e[i].attribute, (String)t->e[i].type, t->e[i].coefficient);             
              dashboard += F("</span>&nbsp;");
              dashboard += String(t->e[i].unit);
              dashboard += F("</div><br>");
              js += "refreshLabel('"+String(file.name())+"','"+(String)ShortAddr+"',"+t->e[i].cluster+","+t->e[i].attribute+",'"+(String)t->e[i].type+"',"+t->e[i].coefficient+",'"+(String)t->e[i].unit+"');";

            }
          }
        }
        dashboard += F("</div>");
        dashboard += F("<div id='action_");
        dashboard += (String)ShortAddr;
        dashboard += F("'>");
        // toutes les actions

        for (int i = 0; i < t->ActionSize; i++)
        {
          if (t->a[i].visible)
          {
            exist++;
            dashboard += F("<button onclick=\"ZigbeeAction(");
            dashboard += ShortAddr;
            dashboard += ",";
            dashboard += t->a[i].command;
            dashboard += ",";
            dashboard += t->a[i].value;
            dashboard += ");\" class='btn btn-primary mb-2'>";
            dashboard += t->a[i].name;
            dashboard += F("</button>&nbsp;");
          }
        }
        dashboard += F("</div>");
      }
      dashboard += F("</div></div></div>");
      
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  file.close();
  root.close();

  if (exist>0)
  {
    result.replace("{{dashboard}}", dashboard);
  }else{
    result.replace("{{dashboard}}", "<div align='center' style='font-size:28px;font-weight:bold;'>No dashboard datas yet</div>");
  }
  

  String javascript = "";
  javascript = F("<script language='javascript'>");
  javascript += F("$(document).ready(function() {");

  javascript += js;
  javascript += F("});");

  javascript += F("</script>");
  result.replace("{{javascript}}", javascript);

  request->send(200, "text/html", result);
}

void handleStatusNetwork(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_NETWORK);
  result += footer();
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);

  result.replace("{{enableWifi}}", F("<img src='/web/img/ok.png'>"));
  result.replace("{{ssidWifi}}", String(ConfigSettings.ssid));
  if (ConfigSettings.enableDHCP)
  {
    result.replace("{{modeWifi}}", "DHCP");
    result.replace("{{ipWifi}}", WiFi.localIP().toString());
    result.replace("{{maskWifi}}", WiFi.subnetMask().toString());
    result.replace("{{GWWifi}}", WiFi.gatewayIP().toString());
  }else{
    result.replace("{{modeWifi}}", "Static");
    result.replace("{{ipWifi}}", ConfigSettings.ipAddressWiFi);
    result.replace("{{maskWifi}}", ConfigSettings.ipMaskWiFi);
    result.replace("{{GWWifi}}", ConfigSettings.ipGWWiFi);
  }
  

  if (ConfigSettings.connectedWifiSta)
  {
    result.replace("{{connectedWifi}}", F("<img src='/web/img/ok.png'>"));
  }
  else
  {
    result.replace("{{connectedWifi}}", F("<img src='/web/img/nok.png'>"));
  }
  
  if (ConfigSettings.enableMqtt)
  {
    String MqttCard = F("<i>MQTT Infos :</i>");
    MqttCard +=F("<br>");
    MqttCard +=F("<Strong>MQTT connected :</strong> ");
    if (mqttClient.connected())
    {
      MqttCard +=F("<img src='/web/img/ok.png'>");
    }else{
      MqttCard +=F("<img src='/web/img/nok.png'>");
    }
    MqttCard +=F(" <br>");
    MqttCard +=F("<Strong>MQTT serv :</strong> ");
    MqttCard +=ConfigGeneral.servMQTT;
    MqttCard +=F(" <br>");
    MqttCard +=F("<Strong>MQTT port :</strong> ");
    MqttCard +=ConfigGeneral.portMQTT;
    MqttCard +=F(" <br><br>");

    result.replace("{{MQTT card}}", MqttCard);
  }else{
    result.replace("{{MQTT card}}", "");
  }

  if (ConfigSettings.enableMarstek)
  {
    String MarstekCard =F("<i>Marstek Infos :</i>");
    MarstekCard +=F("<br>");
    MarstekCard +=F("<Strong>Smart meter connected :</strong> ");
    if (ConfigGeneral.connectedMarstek)
    {
      MarstekCard +=F("<img src='/web/img/ok.png'>");
    }else{
      MarstekCard +=F("<img src='/web/img/nok.png'>");
    }
    MarstekCard +="<br>";
    MarstekCard +="<Strong>Marstek @IP :</strong> ";
    MarstekCard += ConfigGeneral.marstekIP;
    MarstekCard +=F(" <br><br>");
    result.replace("{{Marstek card}}", MarstekCard);
  }else{
     result.replace("{{Marstek card}}", "");
  }

  float val;
  float Voltage = 0.0;
  val = analogRead(VOLTAGE);
  Voltage = (val * 5300) / 4095;
  result.replace("{{Voltage}}", String(Voltage / 1000));

  float temperature = 0;
  temperature = temperatureReadFixed();
  result.replace("{{Temperature}}", String(temperature));

  request->send(200, "text/html", result);

  request->send(200, "text/html", result);
}

void handleStatusEnergy(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADERGRAPH);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_ENERGY);
  if (strcmp(ConfigGeneral.ZLinky,"")!=0)
  {
    result += FPSTR(HTTP_ENERGY_LINKY);
  }
  if (strcmp(ConfigGeneral.Gaz,"")!=0)
  {
     result += FPSTR(HTTP_ENERGY_GAZ);
  }
  if (strcmp(ConfigGeneral.Water,"")!=0)
  {
     result += FPSTR(HTTP_ENERGY_WATER);
  }
  result += FPSTR(HTTP_ENERGY_JAVASCRIPT);
  result+=footer();
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);
  String LinkyStatus;
  String tmpStatus= ini_read(String(ConfigGeneral.ZLinky)+".json","INFO","Status");
  if (tmpStatus =="d4")
  {
    LinkyStatus="<div class='alert alert-danger' role='alert'>Device Offline</div>";
  }else{
    LinkyStatus="";
  }
  result.replace("{{LinkyStatus}}",LinkyStatus);

  int i = 0;
  String time;

  int paramsNr = request->params();
  if (paramsNr > 0)
  {
    time = request->arg(i);
  }
  else
  {
    time = "hour";
  }

  if (time == "hour")
  {
    result.replace("{{selectedHour}}", F("selected"));
    result.replace("{{selectedDay}}", F(""));
    result.replace("{{selectedMonth}}", F(""));
    result.replace("{{selectedYear}}", F(""));
    result.replace("{{stylePowerChart}}", F("block"));
  }
  else if (time == "day")
  {
    result.replace("{{selectedHour}}", F(""));
    result.replace("{{selectedDay}}", F("selected"));
    result.replace("{{selectedMonth}}", F(""));
    result.replace("{{selectedYear}}", F(""));
    result.replace("{{stylePowerChart}}", F("none"));
  }
  else if (time == "month")
  {
    result.replace("{{selectedHour}}", F(""));
    result.replace("{{selectedDay}}", F(""));
    result.replace("{{selectedMonth}}", F("selected"));
    result.replace("{{selectedYear}}", F(""));
    result.replace("{{stylePowerChart}}", F("none"));
  }
  else if (time == "year")
  {
    result.replace("{{selectedHour}}", F(""));
    result.replace("{{selectedDay}}", F(""));
    result.replace("{{selectedMonth}}", F(""));
    result.replace("{{selectedYear}}", F("selected"));
    result.replace("{{stylePowerChart}}", F("none"));
  }
  ConfigGeneral.LinkyMode = ini_read(String(ConfigGeneral.ZLinky)+".json","FF66", "768").toInt();
  String powerGauge="";
  if ((ConfigGeneral.LinkyMode == 2 ) || (ConfigGeneral.LinkyMode == 3 ) || (ConfigGeneral.LinkyMode == 7 ))
  {
     powerGauge=F("<div class='col-lg-6'>");
          powerGauge +=F("<div class='card'>");
            powerGauge +=F("<div class='card-header'>Energy gauge</div>");
            powerGauge +=F("<div class='card-body' style='min-height:272px;'>");
              powerGauge +=F("<div id='power_gauge_global' style='width:30%;display:inline-block;'></div>");
              powerGauge +=F("<div id='power_gauge_global2' style='width:30%;display:inline-block;'></div>");
              powerGauge +=F("<div id='power_gauge_global3' style='width:30%;display:inline-block;'></div>");
            powerGauge +=F("</div>");
          powerGauge +=F("</div>");
        powerGauge +=F("</div>");
  }else{
    powerGauge =F("<div class='col-lg-6'>");
          powerGauge +=F("<div class='card'>");
            powerGauge +=F("<div class='card-header'>Energy gauge</div>");
            powerGauge +=F("<div class='card-body' style='min-height:272px;'>");
              powerGauge +=F("<div id='power_gauge_global' style='height:230px;'></div>");
            powerGauge +=F("</div>");
          powerGauge +=F("</div>");
        powerGauge +=F("</div>");
  }
  result.replace("{{power_gauge}}",powerGauge);

  String javascript = "";

  javascript = F("<script language='javascript'>");
  javascript += F("$(document).ready(function() {");
  if (strcmp(ConfigGeneral.ZLinky,"")!=0)
  {
    if (time == "hour")
    {
      javascript += createPowerGraph(ConfigGeneral.ZLinky);
      if ((ConfigGeneral.LinkyMode == 2 ) || (ConfigGeneral.LinkyMode == 3 ) || (ConfigGeneral.LinkyMode == 7 ))
      {
        javascript += F("loadPowerGaugeAbo(2");
        javascript += F(",'");
        javascript += String(ConfigGeneral.ZLinky);
        javascript += F("','2319','");
        javascript += time;
        javascript += F("');");
        javascript += F("loadPowerGaugeAbo(3");
        javascript += F(",'");
        javascript += String(ConfigGeneral.ZLinky);
        javascript += F("','2575','");
        javascript += time;
        javascript += F("');");
      }
    }
    
    javascript += createEnergyGraph(ConfigGeneral.ZLinky,"energy","['#d7dbdd','#85929e','#273746','#17202a','#c96100', '#c90000','#00c6c9', '#a700c9', '#c90043','#373737']");
    javascript += F("loadPowerGaugeAbo(1");
    javascript += F(",'");
    javascript += String(ConfigGeneral.ZLinky);
    javascript += F("','1295','");
    javascript += time;
    javascript += F("');");
    
    javascript += F("refreshStatusEnergy('");
    javascript += String(ConfigGeneral.ZLinky);
    javascript += F("','1295','");
    javascript += time;
    javascript += F("');");
  }
  if (strcmp(ConfigGeneral.Gaz,"")!=0)
  {
    javascript += createEnergyGraph(ConfigGeneral.Gaz, "gaz","['#e67e22','#2785c7','#00c967','#c9c600','#c96100', '#c90000','#00c6c9', '#a700c9', '#c90043','#373737']");
    javascript += F("refreshStatusGaz('");
    javascript += String(ConfigGeneral.Gaz);
    javascript += F("','");
    javascript += time;
    javascript += F("');");
  }
  if (strcmp(ConfigGeneral.Water,"")!=0)
  {
    javascript += createEnergyGraph(ConfigGeneral.Water, "water","['#2e86c1','#2785c7','#00c967','#c9c600','#c96100', '#c90000','#00c6c9', '#a700c9', '#c90043','#373737']");
    javascript += F("refreshStatusWater('");
    javascript += String(ConfigGeneral.Water);
    javascript += F("','");
    javascript += time;
    javascript += F("');");
  }

  javascript += F("});");
  javascript += F("</script>");

  result.replace("{{javascript}}", javascript);

  request->send(200, "text/html", result);
}

void handleStatusDevices(AsyncWebServerRequest *request)
{
  if(!deviceList->isEmpty())
  {
    deviceList->clear();
  }
  String result;

  result = F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  
  //result += F("<h5>List of devices</h5>");
  result += F("<div class='row justify-content-sm-center' style='--bs-gutter-x: 0.3rem;'>");
  result += F("<h4>Status Devices</h4>");
  String str = "";
  File root = LittleFS.open("/db");
  File file = root.openNextFile();
  int exist = 0;
  int i=0;
  while (file)
  {
    String tmp = file.name();
    // tmp = tmp.substring(10);
    if (tmp.substring(16) == ".json")
    {
      exist++;
      DeviceInfo di;
      di = getDeviceInfo(file.name());
      result += F("<div class='col-md-auto col-sm-auto'><div class='card' style='min-width:380px;'><div class='card-header' style='font-size:12px;font-weight:bold;color:#FFF;background-color:#007bc6;'>@Mac : ");
      result += tmp.substring(0, 16);
      result += F("</div>");
      result += F("<div class='card-body'>");
      result += F("<a data-toggle='collapse' data-target='#infoDevice");
      result += String(i);
      result +=F("' role='button' aria-expanded='true' aria-controls='infoDevice");
      result += String(i);
      result += F("' onclick=\"toggleDiv('infoDevice");
      result += String(i);
      result += F("')\">+ Info</a>");
      result += F("<div id='infoDevice");
      result += String(i);
      result += F("' style='display:none;'>");
      result += "<table width='100%' style='font-size:12px;'><tr>";
      result += F("<td style='font-weight:bold;color:#555;width:60%;'>Manufacturer </td><td style='font-family :\"Courier New\", Courier, monospace;text-align:right;'>");
      String manufacturer;
      manufacturer = di.manufacturer;
      result += manufacturer;
      result += F("</td></tr><tr><td style='font-weight:bold;color:#555;width:60%;'>Model </td><td style='font-family :\"Courier New\", Courier, monospace;text-align:right;'>");
      String model;
      model = di.model;
      result += model;
      result += F("</td></tr><tr><td style='font-weight:bold;color:#555;width:60%;'>Short Address </td><td style='font-family :\"Courier New\", Courier, monospace;text-align:right;'>");
      char SAddr[5];
      int ShortAddr =di.shortAddr;
      snprintf(SAddr,5,"%04X", ShortAddr);
      result += SAddr;
      result += F("</td></tr><tr><td style='font-weight:bold;color:#555;width:60%;'>Device Id </td><td style='font-family :\"Courier New\", Courier, monospace;text-align:right;'>");
      char devId[5];
      int DeviceId = di.deviceId;
      snprintf(devId,5, "%04X", DeviceId);
      result += devId;
      result += F("</td></tr><tr><td style='font-weight:bold;color:#555;width:60%;'>Soft Version </td><td style='font-family :\"Courier New\", Courier, monospace;text-align:right;'>");
      String SoftVer = di.sotfwareVersion;
      result += SoftVer;
      result += F("</td></tr><tr><td style='font-weight:bold;color:#555;width:60%;'>Last seen </td><td style='font-family :\"Courier New\", Courier, monospace;text-align:right;'>");
      String lastseen = di.lastSeen;
      result += lastseen;
      result += F("</td></tr><tr><td style='font-weight:bold;color:#555;width:60%;'>LQI </td><td style='font-family :\"Courier New\", Courier, monospace;text-align:right;'>");
      result += di.LQI;
      result += "</td></tr></table></div><hr>";

      // Get status and action from json    
      if (TemplateExist(DeviceId))
      {
        Template *t;
        t = GetTemplate(DeviceId, model);
        // toutes les propiétés
        result += F("<div id='status_");
        result += (String)ShortAddr;
        result += F("'>");
        result += F("<table width='100%' style='font-size:12px;'>");
        for (int i = 0; i < t->StateSize; i++)
        {
          if (t->e[i].visible)
          {
            if (model == "ZLinky_TIC")
            {
              const char *tmp;
              bool afficheOK = false;
              tmp = t->e[i].mode;
              if ((tmp != NULL) && (tmp[0] != '\0')) 
              {
                char * pch;
                pch = strtok ((char*)tmp,";");
                while (pch != NULL)
                {
                  if (atoi(pch) == ConfigGeneral.LinkyMode)
                  {
                    afficheOK=true;
                    break;
                  }
                  pch = strtok (NULL, " ;");
                }
              }else{
                afficheOK=true;
              }

              if (afficheOK){
                result += F("<tr><td style='width:55%;font-weight:bold;color:#555;'>");
                result += t->e[i].name;
                result += F("</td><td style='width:35%;font-family :\"Courier New\", Courier, monospace;text-align:right;'>");
                result += "<span id='";
                result += String(ShortAddr)+"_"+String(t->e[i].cluster)+"_"+String(t->e[i].attribute);
                result +="'>";
                result += GetValueStatus(file.name(), t->e[i].cluster, t->e[i].attribute, (String)t->e[i].type, t->e[i].coefficient);
                result += "</span></td>";
                result +="<td style='font-family :\"Courier New\", Courier, monospace;text-align:right;'>"+(String)t->e[i].unit;
                result += F("</td>");
                result +="</td></tr>";
              }
            }else{
              result += F("<tr><td style='width:55%;font-weight:bold;color:#555;'>");
              result += t->e[i].name;
              result += F("</td><td style='width:35%;font-family :\"Courier New\", Courier, monospace;text-align:right;'>");
              result += "<span id='";
              result += String(ShortAddr)+"_"+String(t->e[i].cluster)+"_"+String(t->e[i].attribute);
              result +="'>";
              result += GetValueStatus(file.name(), t->e[i].cluster, t->e[i].attribute, (String)t->e[i].type, t->e[i].coefficient);
              result +="<td style='font-family :\"Courier New\", Courier, monospace;text-align:right;'>"+(String)t->e[i].unit;
              result += F("</td>");
              result +="</td></tr>";
            }
          }
        }
        result += F("</table></div><br>");
        result += F("<div id='action_");
        result += (String)ShortAddr;
        result += F("'>");
        // toutes les actions
        for (int i = 0; i < t->ActionSize; i++)
        {

          result += F("<button onclick=\"ZigbeeAction(");
          result += ShortAddr;
          result += ",";
          result += t->a[i].command;
          result += ",";
          result += t->a[i].value;
          result += ");\" class='btn btn-primary mb-2'>";
          result += t->a[i].name;
          result += F("</button>");
        }
        result += F("</div>");
        free(t);
      }
      result += F("</div></div></div>");
    }
    i++;
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  

  if (exist>0)
  {
    result +="<script>getDeviceValue();</script>";
  }else{
    result += "<div align='center' style='height:100px;font-size:28px;font-weight:bold;'>No devices yet</div> <br>";
  }
  result+=footer();
  result += F("</html>");
  file.close();
  root.close();
  request->send(200, F("text/html"), result);
}

void handleConfigGeneral(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_GENERAL);
  result+=footer();
  result += F("</html>");

  //result = getMenuGeneral(result, "general");

  result.replace("{{FormattedDate}}", FormattedDate);

  if (ConfigSettings.enableDebug)
  {
    result.replace("{{checkedDebug}}", "Checked");
  }
  else
  {
    result.replace("{{checkedDebug}}", "");
  }

  request->send(200, "text/html", result);
}

void handleConfigZigbee(AsyncWebServerRequest *request)
{
  String result;

  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_ZIGBEE);
  result+=footer();
  result = getMenuGeneralZigbee(result, "config");
  
  result.replace("{{macCoordinator}}", String(ZConfig.zigbeeMac,HEX));
  result.replace("{{versionCoordinator}}", "SDK: "+String(ZConfig.sdk, DEC)+" Ver: "+String(ZConfig.application));
  result.replace("{{networkCoordinator}}", String(ZConfig.network));

  result.replace("{{FormattedDate}}", FormattedDate);
  result.replace("{{SetMaskChannel}}", String(ZConfig.channel));

  request->send(200, F("text/html"), result);
}

void handleConfigHorloge(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_HORLOGE);
  result+=footer();
  result += F("</html>");

  //result = getMenuGeneral(result, "horloge");

  result.replace("{{FormattedDate}}", FormattedDate);

  result.replace("{{ntpserver}}", ConfigGeneral.ntpserver);
  result.replace("{{timeoffset}}", String(ConfigGeneral.timeoffset));
  result.replace("{{timezone}}", String(ConfigGeneral.timezone));
  String path = "configGeneral.json";
  String value = config_read(path,"epoch");
  /*unsigned long t = atol(value.c_str());
  char manualDate[32];
  sprintf(manualDate, "%02d/%02d/%02d %02d:%02d:%02d", day(t), month(t), year(t), hour(t), minute(t), second(t));*/
  result.replace("{{epochtime}}",value);

  request->send(200, "text/html", result);
}

void handleGetMQTTStatus(AsyncWebServerRequest *request)
{
  String result="0";
  if (ConfigSettings.enableMqtt)
  {
    if (mqttClient.connected())
    {
      result = "1";
    }else{
      result = "0";
    }
  }else{
    result="0";
  }
  request->send(200, "text/html", result);
}

void handleConfigMQTT(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_MQTT);
  result+=footer();
  result += F("</html>");
  if (ConfigSettings.enableMqtt)
  {
    result.replace("{{checkedMqtt}}", "Checked");
  }
  else
  {
    result.replace("{{checkedMqtt}}", "");
  }
  //result = getMenuGeneral(result, "mqtt");

  result.replace("{{FormattedDate}}", FormattedDate);

  result.replace("{{servMQTT}}", String(ConfigGeneral.servMQTT));
  result.replace("{{portMQTT}}", String(ConfigGeneral.portMQTT));
  result.replace("{{userMQTT}}", String(ConfigGeneral.userMQTT));
  result.replace("{{clientIDMQTT}}", String(ConfigGeneral.clientIDMQTT));
  if (String(ConfigGeneral.passMQTT) !="")
  {
    result.replace("{{passMQTT}}", "********");
  }else{
    result.replace("{{passMQTT}}", "");
  }
  //result.replace("{{passMQTT}}", String(ConfigGeneral.passMQTT));
  result.replace("{{headerMQTT}}", String(ConfigGeneral.headerMQTT));
  result.replace("{{displayCustomMQTT}}", "display:none;");

  if (ConfigGeneral.HAMQTT)
  {
    result.replace("{{checkedHA}}", "checked");
    result.replace("{{checkedTB}}", "");
    result.replace("{{checkedCustom}}", "");
    result.replace("{{displayCustomMQTT}}", "display:none;");
  }

  if (ConfigGeneral.TBMQTT)
  {
    result.replace("{{checkedTB}}", "checked");
    result.replace("{{checkedHA}}", "");
    result.replace("{{checkedCustom}}", "");
    result.replace("{{displayCustomMQTT}}", "display:none;");
  }

  if (ConfigGeneral.customMQTT)
  {
    result.replace("{{checkedCustom}}", "checked");
    result.replace("{{checkedHA}}", "");
    result.replace("{{checkedCustom}}", "");
    result.replace("{{displayCustomMQTT}}", "display:block;");
  }


  if (ConfigGeneral.customMQTTJson != "null")
  {
    result.replace("{{customMQTTJson}}", ConfigGeneral.customMQTTJson);
  }else{
    result.replace("{{customMQTTJson}}", "");
  }

  request->send(200, "text/html", result);
}

void handleConfigHTTP(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_HTTP);
  result+=footer();
  result += F("</html>");
  //result = getMenuGeneral(result, "http");

  if (ConfigSettings.enableSecureHttp)
  {
    result.replace("{{checkedHttp}}", "Checked");
  }else{
    result.replace("{{checkedHttp}}", "");
  }
  
  if (request->arg("error").toInt() > 0)
  {
    result.replace("{{error}}", "Error : please verify the HTTP username and/or your password length >= 4 characters");
    if ((request->arg("error").toInt() & 1) == 1)
    {
      result.replace("{{passborder}}", "border:1px solid red;");
    }else{
      result.replace("{{passborder}}", "");
    }
    if ((request->arg("error").toInt() & 2) == 2)
    {
      result.replace("{{userborder}}", "border:1px solid red;");
    }else{
      result.replace("{{userborder}}", "");
    }
  }else{
    result.replace("{{error}}", "");
  }

  result.replace("{{FormattedDate}}", FormattedDate);

  result.replace("{{userHTTP}}", String(ConfigGeneral.userHTTP));

  if (String(ConfigGeneral.passHTTP) !="")
  {
    result.replace("{{passHTTP}}", "********");
  }else{
    result.replace("{{passHTTP}}", "");
  }

  request->send(200, "text/html", result);
}

void handleConfigRules(AsyncWebServerRequest *request)
{
  String result;
  result = "<html>";
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_RULES);
  result += footer();
  result += "</html>";

  Rule rules[10];
  int ruleCount = 0;
  jsonToRules(rules, ruleCount);

  String rulesList="<table class='table table-striped table-hover'>";
  rulesList+="<thead>";
    rulesList+="<tr>";
      rulesList+="<th scope='col'>Name</th>";
      rulesList+="<th scope='col' width='50px;'>Status</th>";
      rulesList+="<th scope='col' width='150px;'>Last Date</th>";
      rulesList+="<th scope='col' width='100px;'>Actions</th>";
    rulesList+="</tr>";
  rulesList+="</thead>";

  int exist=0;
  String js="";
  for (int i=0;i<ruleCount;i++)
  {
    exist++;
    rulesList+="<tr>";
      rulesList+="<td scope='row'>";
        rulesList+=rules[i].name;
      rulesList+="</td>";
      rulesList+="<td>";
        int status = getStatusRule(rules[i].name);
        js += "getRuleStatus('";
        js +=rules[i].name;
        js +="');";
        rulesList+="<span id='status_";
        rulesList+=rules[i].name;
        rulesList+="'>";
        if (status)
        {
          rulesList+="<svg xmlns='http://www.w3.org/2000/svg' width='32' height='32' fill='#1bc600' class='bi bi-bookmark-check-fill' viewBox='0 0 16 16'>";
            rulesList+="<path fill-rule='evenodd' d='M2 15.5V2a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v13.5a.5.5 0 0 1-.74.439L8 13.069l-5.26 2.87A.5.5 0 0 1 2 15.5m8.854-9.646a.5.5 0 0 0-.708-.708L7.5 7.793 6.354 6.646a.5.5 0 1 0-.708.708l1.5 1.5a.5.5 0 0 0 .708 0z'/>";
          rulesList+="</svg>";
        }else{
          rulesList+="<svg xmlns='http://www.w3.org/2000/svg' width='32' height='32' fill='#c60000' class='bi bi-bookmark-x-fill' viewBox='0 0 16 16'>";
            rulesList+="<path fill-rule='evenodd' d='M2 15.5V2a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v13.5a.5.5 0 0 1-.74.439L8 13.069l-5.26 2.87A.5.5 0 0 1 2 15.5M6.854 5.146a.5.5 0 1 0-.708.708L7.293 7 6.146 8.146a.5.5 0 1 0 .708.708L8 7.707l1.146 1.147a.5.5 0 1 0 .708-.708L8.707 7l1.147-1.146a.5.5 0 0 0-.708-.708L8 6.293z'/>";
          rulesList+="</svg>";
        }
      rulesList+="</span>";
      rulesList+="</td>";
      rulesList+="<td>";
        rulesList+="<span id='dateStatus_";
          rulesList+=rules[i].name;
        rulesList+="'>";  
        rulesList+=getLastDateRule(rules[i].name);
      rulesList+="</span>";
      rulesList+="</td>";
      rulesList+="<td>";
        /*rulesList+="<button type='button' class='btn btn-warning'>";
          rulesList+="<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' class='bi bi-pencil-square' viewBox='0 0 16 16'>";
            rulesList+="<path d='M15.502 1.94a.5.5 0 0 1 0 .706L14.459 3.69l-2-2L13.502.646a.5.5 0 0 1 .707 0l1.293 1.293zm-1.75 2.456-2-2L4.939 9.21a.5.5 0 0 0-.121.196l-.805 2.414a.25.25 0 0 0 .316.316l2.414-.805a.5.5 0 0 0 .196-.12l6.813-6.814z'/>";
            rulesList+="<path fill-rule='evenodd' d='M1 13.5A1.5 1.5 0 0 0 2.5 15h11a1.5 1.5 0 0 0 1.5-1.5v-6a.5.5 0 0 0-1 0v6a.5.5 0 0 1-.5.5h-11a.5.5 0 0 1-.5-.5v-11a.5.5 0 0 1 .5-.5H9a.5.5 0 0 0 0-1H2.5A1.5 1.5 0 0 0 1 2.5z'/>";
          rulesList+="</svg>";
        rulesList+="</button>";
        rulesList+="<button type='button' class='btn btn-danger'>";
          rulesList+="<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' class='bi bi-x-square' viewBox='0 0 16 16'>";
            rulesList+="<path d='M14 1a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H2a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1zM2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2z'/>";
            rulesList+="<path d='M4.646 4.646a.5.5 0 0 1 .708 0L8 7.293l2.646-2.647a.5.5 0 0 1 .708.708L8.707 8l2.647 2.646a.5.5 0 0 1-.708.708L8 8.707l-2.646 2.647a.5.5 0 0 1-.708-.708L7.293 8 4.646 5.354a.5.5 0 0 1 0-.708'/>";
          rulesList+="</svg>";
        rulesList+="</button>";*/
      rulesList+="</td>";
    rulesList+="</tr>";
  }
  rulesList+="</table>";
  result.replace("{{rulesList}}",rulesList);

  if (exist>0)
  {
    result +="<script>"+js+"</script>";
  }else{
    result += "<div align='center' style='height:100px;font-size:28px;font-weight:bold;'>No rules yet</div> <br>";
  }

  request->send(200, "text/html", result);
}

void handleConfigLinky(AsyncWebServerRequest *request)
{
  String result,list;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_LINKY);
  result+=footer();
  result += F("</html>");

  //result = getMenuGeneral(result, "linky");

  result.replace("{{FormattedDate}}", FormattedDate);

  list="<Select name='linkyDevice' class='form-select form-select-lg mb-3' aria-label='.form-select-lg example'><OPTION value=''>--Choice--</OPTION>";
  String str = "";
  File root = LittleFS.open("/db");
  File file = root.openNextFile();
  while (file)
  {
    String tmp = file.name();
    String mac = tmp.substring(0, 16);
    if (tmp.substring(16) == ".json")
    {
      String model;
      model = GetModel(file.name());
      if (model == "ZLinky_TIC")
      { 
        list += F("<OPTION value='");
        list += mac;
        list += F("' ");
        if (strcmp(mac.c_str(),ConfigGeneral.ZLinky)==0)
        {
          list +="Selected";
        }
        list += F(">");
        list += F("ZLinky (");
        list += mac;
        list += F(")");
        list += F("</OPTION>");
      }
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  file.close();
  list +="</select>";

  result.replace("{{selectDevices}}", list);
  result.replace("{{tarifAbo}}", String(ConfigGeneral.tarifAbo));
  result.replace("{{tarifCSPE}}", String(ConfigGeneral.tarifCSPE));
  result.replace("{{tarifCTA}}", String(ConfigGeneral.tarifCTA));
  result.replace("{{tarifIdx1}}", String(ConfigGeneral.tarifIdx1));
  result.replace("{{tarifIdx2}}", String(ConfigGeneral.tarifIdx2));
  result.replace("{{tarifIdx3}}", String(ConfigGeneral.tarifIdx3));
  result.replace("{{tarifIdx4}}", String(ConfigGeneral.tarifIdx4));
  result.replace("{{tarifIdx5}}", String(ConfigGeneral.tarifIdx5));
  result.replace("{{tarifIdx6}}", String(ConfigGeneral.tarifIdx6));
  result.replace("{{tarifIdx7}}", String(ConfigGeneral.tarifIdx7));
  result.replace("{{tarifIdx8}}", String(ConfigGeneral.tarifIdx8));
  result.replace("{{tarifIdx9}}", String(ConfigGeneral.tarifIdx9));
  result.replace("{{tarifIdx10}}", String(ConfigGeneral.tarifIdx10));

  request->send(200, "text/html", result);
}



void handleConfigGaz(AsyncWebServerRequest *request)
{
  String result,list;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_GAZ);
  result+=footer();
  result += F("</html>");

  //result = getMenuGeneral(result, "gaz");

  result.replace("{{FormattedDate}}", FormattedDate);

  list="<Select name='gazDevice' class='form-select form-select-lg mb-3' aria-label='.form-select-lg example'><OPTION value=''>--Choice--</OPTION>";
  String str = "";
  File root = LittleFS.open("/db");
  File file = root.openNextFile();
  while (file)
  {
    String tmp = file.name();
    String mac = tmp.substring(0, 16);
    if (tmp.substring(16) == ".json")
    {
      String model;
      model = GetModel(file.name());
      if (model == "ZiPulses")
      { 
        list += F("<OPTION value='");
        list += mac;
        list += F("' ");
        if (strcmp(mac.c_str(),ConfigGeneral.Gaz)==0)
        {
          list +="Selected";
        }
        list += F(">");
        list += F("ZiPulses (");
        list += mac;
        list += F(")");
        list += F("</OPTION>");
      }
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  file.close();
  list +="</select>";

  result.replace("{{selectDevices}}", list);

  result.replace("{{tarifGaz}}", String(ConfigGeneral.tarifGaz));
  result.replace("{{coeffGaz}}", String(ConfigGeneral.coeffGaz));
  result.replace("{{unitGaz}}", String(ConfigGeneral.unitGaz));

  request->send(200, "text/html", result);
}

void handleConfigWater(AsyncWebServerRequest *request)
{
  String result,list;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_WATER);
  result+=footer();
  result += F("</html>");

  //result = getMenuGeneral(result, "water");

  result.replace("{{FormattedDate}}", FormattedDate);

  list="<Select name='waterDevice' class='form-select form-select-lg mb-3' aria-label='.form-select-lg example'><OPTION value=''>--Choice--</OPTION>";
  String str = "";
  File root = LittleFS.open("/db");
  File file = root.openNextFile();
  while (file)
  {
    String tmp = file.name();
    String mac = tmp.substring(0, 16);
    if (tmp.substring(16) == ".json")
    {
      String model;
      model = GetModel(file.name());
      if (model == "ZiPulses")
      { 
        list += F("<OPTION value='");
        list += mac;
        list += F("' ");
        if (strcmp(mac.c_str(),ConfigGeneral.Water)==0)
        {
          list +="Selected";
        }
        list += F(">");
        list += F("ZiPulses (");
        list += mac;
        list += F(")");
        list += F("</OPTION>");
      }
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  file.close();
  list +="</select>";

  result.replace("{{selectDevices}}", list);

  result.replace("{{tarifWater}}", String(ConfigGeneral.tarifWater));
  result.replace("{{coeffWater}}", String(ConfigGeneral.coeffWater));
  result.replace("{{unitWater}}", String(ConfigGeneral.unitWater));
  request->send(200, "text/html", result);
}

void handleConfigNotification(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_NOTIFICATION);
  result+=footer();
  result += F("</html>");

  //result = getMenuGeneral(result, "notif");

  result.replace("{{FormattedDate}}", FormattedDate);
  if (ConfigSettings.enableNotif)
  {
    result.replace("{{checkedNotif}}", "Checked");
  }
  else
  {
    result.replace("{{checkedNotif}}", "");
  }
  result.replace("{{servSMTP}}", String(ConfigGeneral.servSMTP));
  result.replace("{{portSMTP}}", String(ConfigGeneral.portSMTP));
  result.replace("{{userSMTP}}", String(ConfigGeneral.userSMTP));
  result.replace("{{passSMTP}}", String(ConfigGeneral.passSMTP));

  request->send(200, "text/html", result);
}



void handleConfigWebPush(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_WEBPUSH);
  result+=footer();
  result += F("</html>");

  //result = getMenuGeneral(result, "webpush");

  result.replace("{{FormattedDate}}", FormattedDate);
  if (ConfigSettings.enableWebPush)
  {
    result.replace("{{checkedWebPush}}", "Checked");
    
  }
  else
  {
    result.replace("{{checkedWebPush}}", "");
    
  }
  if (ConfigGeneral.webPushAuth)
  {
    result.replace("{{checkedWebPushAuth}}", "Checked");
    result.replace("{{displayWebPushAuth}}","");
  }
  else
  {
    result.replace("{{checkedWebPushAuth}}", "");
    result.replace("{{displayWebPushAuth}}","display:none;");
  }

  String error ="Error : ";
  if (request->arg("error").toInt() > 0)
  {
    if ((request->arg("error").toInt() & 4) == 4)
    {
      result.replace("{{urlborder}}", "border:1px solid red;");
      error = error+"Please add an url to the server HTTP. <br>";
      
    }else{
      result.replace("{{urlborder}}", "");
    }

    if ((request->arg("error").toInt() & 2) == 2)
    {
      result.replace("{{userborder}}", "border:1px solid red;");
      error = error+"Please add an username.<br>";
    }else{
      result.replace("{{userborder}}", "");
    }

    if ((request->arg("error").toInt() & 1) == 1)
    {
      result.replace("{{passborder}}", "border:1px solid red;");
      error = error + "Please verify the password length >= 4 characters.<br>";
    }else{
      result.replace("{{passborder}}", "");
    }
    
   

    result.replace("{{error}}", error);
  }else{
    result.replace("{{error}}", "");
  }


  result.replace("{{servWebPush}}", String(ConfigGeneral.servWebPush));
  result.replace("{{userWebPush}}", String(ConfigGeneral.userWebPush));
  if (String(ConfigGeneral.passWebPush) !="")
  {
    result.replace("{{passWebPush}}", "********");
  }else{
    result.replace("{{passWebPush}}", "");
  }

  request->send(200, "text/html", result);
}
void handleConfigMarstek(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_MARSTEK);
  
  result+=footer();
  result += F("</html>");

  if (strlen(ConfigGeneral.ZLinky) >0)
  {
    result.replace("{{disableMarstek}}","");
    result.replace("{{ZLinky}}",ConfigGeneral.ZLinky);
  }else{
    result.replace("{{disableMarstek}}","disabled");
    result.replace("{{ZLinky}}","-- None -- Must be paired");
  }

  if (ConfigSettings.enableMarstek)
  {
    result.replace("{{checkedMarstek}}", "Checked"); 
  }else{
    result.replace("{{checkedMarstek}}", "");
  }

  request->send(200, "text/html", result);
}

void handleConfigUdpClient(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_UDPCLIENT);
  
  result+=footer();
  result += F("</html>");

  result.replace("{{FormattedDate}}", FormattedDate);
  if (ConfigSettings.enableUDP)
  {
    result.replace("{{checkedUDP}}", "Checked"); 
  }
  else
  {
    result.replace("{{checkedUDP}}", "");
  }

  if (ConfigGeneral.customUDPJson != "")
  {
    result.replace("{{customUDPJson}}", ConfigGeneral.customUDPJson);
  }else{
    result.replace("{{customUDPJson}}", "");
  }

  String error ="Error : ";
  if (request->arg("error").toInt() > 0)
  {
    if ((request->arg("error").toInt() & 2) == 2)
    {
      result.replace("{{urlborder}}", "border:1px solid red;");
      error = error+"Please add an url to the server UDP. <br>";
      
    }else{
      result.replace("{{urlborder}}", "");
    }

    if ((request->arg("error").toInt() & 1) == 1)
    {
      result.replace("{{portborder}}", "border:1px solid red;");
      error = error+"Please add an UDP port.<br>";
    }else{
      result.replace("{{portborder}}", "");
    }
    result.replace("{{error}}", error);
  }else{
    result.replace("{{error}}", "");
  }
  result.replace("{{servUDP}}", String(ConfigGeneral.servUDP));
  result.replace("{{portUDP}}", String(ConfigGeneral.portUDP));
  result.replace("{{customUDPJson}}", String(ConfigGeneral.customUDPJson));

  request->send(200, "text/html", result);

}


void handleConfigWifi(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_WIFI);
  result+=footer();
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);

  if (request->arg("error").toInt() > 0)
  {
    result.replace("{{error}}", "STA Error : please verify the SSID and/or your password length >= 8 characters");
    if ((request->arg("error").toInt() & 1) == 1)
    {
      result.replace("{{passborder}}", "border:1px solid red;");
    }else{
      result.replace("{{passborder}}", "");
    }
    if ((request->arg("error").toInt() & 2) == 2)
    {
      result.replace("{{ssidborder}}", "border:1px solid red;");
    }else{
      result.replace("{{ssidborder}}", "");
    }


  }else{
    result.replace("{{error}}", "");
  }

  if (request->arg("ipError").toInt() >0 )
  {
    result.replace("{{ipError}}", "IP Error : Ip Address format not ok");
    if ((request->arg("ipError").toInt() & 1) == 1)
    {
      result.replace("{{ipborder}}", "border:1px solid red;");
    }else{
      result.replace("{{ipborder}}", "");
    }
    
    if ((request->arg("ipError").toInt() & 2) == 2)
    {
      result.replace("{{ipmask}}", "border:1px solid red;");
    }else{
      result.replace("{{ipmask}}", "");
    }
    
    if ((request->arg("ipError").toInt() & 4) == 4)
    {
      result.replace("{{ipgw}}", "border:1px solid red;");
    }else{
      result.replace("{{ipgw}}", "");
    }
  }else{
    result.replace("{{ipError}}", "");
  }

  if (ConfigSettings.enableDHCP)
  {
    result.replace("{{checkedDHCP}}", "Checked");
    result.replace("{{static}}", "none");
  }
  else
  {
    result.replace("{{checkedDHCP}}", "");
    result.replace("{{static}}", "block");
  }

  result.replace("{{ssid}}", String(ConfigSettings.ssid));
  if (String(ConfigSettings.password)!="")
  {
    result.replace("{{password}}", "********");
  }else{
    result.replace("{{password}}", "");
  }

  result.replace("{{ip}}", ConfigSettings.ipAddressWiFi);
  result.replace("{{mask}}", ConfigSettings.ipMaskWiFi);
  result.replace("{{gw}}", ConfigSettings.ipGWWiFi);

  request->send(200, "text/html", result);
}

void handleLogs(AsyncWebServerRequest *request)
{
  String result;

  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);

  result += F("<h4>Console</h4>");
  result += F("<div class='row justify-content-md-center'>");
  result += F("<div class='col col-md-6'>");
  result += F("<button type='button' onclick='cmd(\"ClearConsole\");document.getElementById(\"console\").value=\"\";' class='btn btn-primary'>Clear Console</button> ");
  result += F("<button type='button' onclick='cmd(\"GetVersion\");' class='btn btn-primary'>Get Version</button> ");
  result += F("<button type='button' onclick='cmd(\"ErasePDM\");' class='btn btn-primary'>Erase PDM</button> ");
  result += F("<button type='button' onclick='cmd(\"Reset\");' class='btn btn-primary'>Reset</button> ");
  result += F("<button type='button' onclick='cmd(\"StartNwk\");' class='btn btn-primary'>StartNwk</button> ");
  result += F("<button type='button' onclick='cmd(\"PermitJoin\");' class='btn btn-primary'>Permit Join</button> ");
  result += F("<button type='button' onclick='cmd(\"RawMode\");' class='btn btn-primary'>RAW mode</button> ");
  result += F("<button type='button' onclick='cmd(\"RawModeOff\");' class='btn btn-primary'>RAW mode Off</button> ");
  result += F("<button type='button' onclick='cmd(\"ActiveReq\");' class='btn btn-primary'>ActiveReq</button> ");
  result += F("</div></div>");
  result += F("<div class='row justify-content-md-center' >");
  result += F("<div class='col col-md-6'>");

  result += F("Raw datas : <textarea id='console' rows='16' cols='100'>");

  result += F("</textarea></div></div>");
  // result += F("</div>");
  result += F("</body>");
  result += F("<script language='javascript'>");
  result += F("$(document).ready(function() {");
  result += F("logRefresh();});");
  result += F("</script>");

  result+=footer();
  result += F("</html>");
  request->send(200, F("text/html"), result);
}

void handleTools(AsyncWebServerRequest *request)
{

  
 /* AsyncWebServerResponse *response = request->beginChunkedResponse("text/plain",[](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
    
    String result; 
    result = F("<html>");
    result += FPSTR(HTTP_HEADER);
    result += FPSTR(HTTP_MENU);
    result.replace("{{FormattedDate}}", FormattedDate);
    result += FPSTR(HTTP_TOOLS);
    result+=footer();
    result += F("</html>");
    memcpy(buffer,result.c_str(),result.length());

    return result.length();
  });
  request->send(response);*/

  /*String result;
  
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += FPSTR(HTTP_TOOLS);
  result+=footer();
  result += F("</html>");

  request->send(200, F("text/html"), result);*/
  
  
  String result;
  //AsyncResponseStream *response = request->beginResponseStream("text/html");
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);

  result += FPSTR(HTTP_TOOLS);
  result+=footer();
  result += F("</html>");
 
  request->send(200,"text/html", result);

  /*AsyncWebServerResponse* response = request->beginChunkedResponse(contentType,
                                       [](uint8_t* buffer, size_t maxLen, size_t index) -> size_t 
    {

    });

  request->send(response);*/

}


void handleShelly(AsyncWebServerRequest * request) {
  String result;
  result = FPSTR(HTTP_SHELLY_EMULE);
  
  request->send(200,"application/json", result);
  
}

void handleHelp(AsyncWebServerRequest * request) {
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += FPSTR(HTTP_HELP);
  result+=footer();
  result += F("</html>");
  result.replace("{{version}}", VERSION);
  
  request->send(200,"text/html", result);
  
}

void hard_restart()
{
  esp_task_wdt_init(1, true);
  esp_task_wdt_add(NULL);
  while (true)
    ;
}

void handleReboot(AsyncWebServerRequest *request)
{
  String result;

  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h4>Reboot ...</h4>");
  result = result + F("</body>");
  result+=footer();
  result += F("</html>");
  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/"));
  request->send(response);

  hard_restart();
}

size_t content_len;
#define U_PART U_SPIFFS

void handleDoUpdate(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index){
    DEBUG_PRINTLN("Update");
    content_len = request->contentLength();
    // if filename includes spiffs, update the spiffs partition
    int cmd = (filename.indexOf("spiffs") > -1) ? U_PART : U_FLASH;
    if (!Update.begin(content_len, cmd)) {
      Update.printError(Serial);
    }
  }

  if (Update.write(data, len) != len) {
    Update.printError(Serial);
  }

  if (final) {
    AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "Please wait while the device reboots");
    response->addHeader("Refresh", "20");  
    response->addHeader("Location", "/");
    request->send(response);
    if (!Update.end(true)){
      Update.printError(Serial);
    } else {
      DEBUG_PRINTLN("Update complete");
      Serial.flush();
      ESP.restart();
    }
  }
}

void printProgress(size_t prg, size_t sz) {
  Serial.printf("%d%%\n", (prg*100)/content_len);
}

void handleDoRestore(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
  String logmessage="";
  if (!index){
    DEBUG_PRINTLN(F("Restore start..."));
    request->_tempFile = LittleFS.open("/rt/" + filename, "w+");
  }

  if (len) {
    // stream the incoming chunk to the opened file
    request->_tempFile.write(data, len);
    
    logmessage = "Writing file: " + String(filename) + " index=" + String(index) + " len=" + String(len);
    DEBUG_PRINTLN(logmessage);
  }

  if (final) {
    // close the file handle as the upload is now done
    logmessage = "Upload Complete: " + String(filename) + ",size: " + String(index + len);
    request->_tempFile.close();
    DEBUG_PRINTLN(logmessage);

    mtar_t tar;
    mtar_header_t h;
    char *p;

    /* Open archive for reading */
    String path = "/rt/"+filename;
    DEBUG_PRINTLN(path);
    int err;
    err = mtar_open(&tar, path.c_str(), "r");
    DEBUG_PRINTLN(mtar_strerror(err));

    while ( (err=mtar_read_header(&tar, &h)) != MTAR_ENULLRECORD ) {
      DEBUG_PRINTLN(mtar_strerror(err));
      
      mtar_find(&tar, h.name, &h);
      p = (char *)calloc(1, h.size + 1);
      mtar_read_data(&tar, p, h.size);
      
      String path = "/";
      path +=h.name;

      File file = LittleFS.open(path.c_str(),"w");
      file.print(p);
      free(p);
      file.close();
      mtar_next(&tar);
    }

    /* Close archive */
    mtar_close(&tar);
    request->redirect("/backup");
  }
}

void handleToolCreateBackup(AsyncWebServerRequest *request)
{

  mtar_t tar;
  int error;
  error = mtar_open(&tar, "/bk/backup.tar", "w");
  DEBUG_PRINTLN(mtar_strerror(error));

  //backup database
  File root = LittleFS.open("/db");
  File file = root.openNextFile();
  esp_task_wdt_reset();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = F("db/");
      tmp += file.name();
      DEBUG_PRINT("mtar_write_file_header : ");
      DEBUG_PRINTLN(tmp.c_str());
      error = mtar_write_file_header(&tar, tmp.c_str(), file.size());
      DEBUG_PRINTLN(mtar_strerror(error));
      String buff="";
      while (file.available())
      { 
        buff+=(char)file.read();
      }
      DEBUG_PRINT("mtar_write_data : ");
      error = mtar_write_data(&tar, buff.c_str(), strlen(buff.c_str()));
      DEBUG_PRINTLN(mtar_strerror(error));
      file.close(); 
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  
  root.close();
  file.close();

  //backup config
  root = LittleFS.open("/config");
  file = root.openNextFile();
  esp_task_wdt_reset();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = F("config/");
      tmp += file.name();
      DEBUG_PRINT("mtar_write_file_header : ");
      DEBUG_PRINTLN(tmp.c_str());
      error = mtar_write_file_header(&tar, tmp.c_str(), file.size());
      DEBUG_PRINTLN(mtar_strerror(error));
      String buff="";
      while (file.available())
      { 
        buff+=(char)file.read();
      }
      DEBUG_PRINT("mtar_write_data : ");
      error = mtar_write_data(&tar, buff.c_str(), strlen(buff.c_str()));
      DEBUG_PRINTLN(mtar_strerror(error));
      file.close(); 
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  
  root.close();
  file.close();

  //backup debug
  root = LittleFS.open("/debug");
  file = root.openNextFile();
  esp_task_wdt_reset();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = F("debug/");
      tmp += file.name();
      DEBUG_PRINT("mtar_write_file_header : ");
      DEBUG_PRINTLN(tmp.c_str());
      error = mtar_write_file_header(&tar, tmp.c_str(), file.size());
      DEBUG_PRINTLN(mtar_strerror(error));
      String buff="";
      while (file.available())
      { 
        buff+=(char)file.read();
      }
      DEBUG_PRINT("mtar_write_data : ");
      error = mtar_write_data(&tar, buff.c_str(), strlen(buff.c_str()));
      DEBUG_PRINTLN(mtar_strerror(error));
      file.close(); 
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  
  root.close();
  file.close();

  //backup template
  root = LittleFS.open("/tp");
  file = root.openNextFile();
  esp_task_wdt_reset();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = F("tp/");
      tmp += file.name();
      DEBUG_PRINT("mtar_write_file_header : ");
      DEBUG_PRINTLN(tmp.c_str());
      error = mtar_write_file_header(&tar, tmp.c_str(), file.size());
      DEBUG_PRINTLN(mtar_strerror(error));
      String buff="";
      while (file.available())
      { 
        buff+=(char)file.read();
      }
      DEBUG_PRINT("mtar_write_data : ");
      error = mtar_write_data(&tar, buff.c_str(), strlen(buff.c_str()));
      DEBUG_PRINTLN(mtar_strerror(error));
      file.close(); 
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  
  root.close();
  file.close();

  //backup backup
  root = LittleFS.open("/bk");
  file = root.openNextFile();
  esp_task_wdt_reset();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = F("bk/");
      tmp += file.name();
      if (strcmp(tmp.substring(19,24).c_str(),".json")==0)
      {
        DEBUG_PRINT("mtar_write_file_header : ");
        DEBUG_PRINTLN(tmp.c_str());
        error = mtar_write_file_header(&tar, tmp.c_str(), file.size());
        DEBUG_PRINTLN(mtar_strerror(error));
        String buff="";
        while (file.available())
        { 
          buff+=(char)file.read();
        }
        DEBUG_PRINT("mtar_write_data : ");
        error = mtar_write_data(&tar, buff.c_str(), strlen(buff.c_str()));
        DEBUG_PRINTLN(mtar_strerror(error));
      }
      
      file.close(); 
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  
  root.close();
  file.close();

  error =mtar_finalize(&tar);
  DEBUG_PRINTLN(mtar_strerror(error));
  error =mtar_close(&tar);
  DEBUG_PRINTLN(mtar_strerror(error));

 
  root = LittleFS.open("/bk");
  file = root.openNextFile();
  String listFiles="";
  esp_task_wdt_reset();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      if (tmp.substring((tmp.length()-3),tmp.length()) == "tar")
      {
        listFiles += F("<li><a href='web/");
        listFiles += tmp;
        listFiles += F("'>");
        listFiles += tmp;
        listFiles += F(" ( ");
        listFiles += file.size();
        listFiles += F(" o)</a></li>");
      } 
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  file.close();
  root.close();
  
  request->send(200, F("text/html"), listFiles);


}

void handleToolBackup(AsyncWebServerRequest *request)
{
  String result, listFiles;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += FPSTR(HTTP_BACKUP);
  File root = LittleFS.open("/rt");
  File file = root.openNextFile();

  listFiles="";
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      listFiles += F("<li><a href='web/");
      listFiles += tmp;
      listFiles += F("'>");
      listFiles += tmp;
      listFiles += F(" ( ");
      listFiles += file.size();
      listFiles += F(" o)</a></li>");
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  root.close();
  file.close();
  result.replace("{{listBackupFiles}}", listFiles);
  result+=footer();
  result += F("</html>");

  request->send(200, F("text/html"), result);
}


int totalLength;       //total size of firmware
int currentLength = 0; //current size of written firmware

void progressFunc(unsigned int progress,unsigned int total) {
  Serial.printf("%u of %u\r", progress, total);
};

void runUpdateFirmware(uint8_t *data, size_t len)
{
  Update.write(data, len);
  currentLength += len;
  // Print dots while waiting for update to finish
  Serial.print('.');
  // if current length of written firmware is not equal to total firmware size, repeat
  if(currentLength != totalLength) return;
  Update.end(true);
  Serial.printf("\nUpdate Success, Total Size: %u\nRebooting...\n", currentLength);
  // Restart ESP32 to see changes 
  ESP.restart();
}

void checkUpdateFirmware()
{
  clientWeb.begin(UPD_FILE);
  clientWeb.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); 
  // Get file, just to check if each reachable
  int resp = clientWeb.GET();
  DEBUG_PRINT("Response: ");
  DEBUG_PRINTLN(resp);
  // If file is reachable, start downloading
  if(resp == HTTP_CODE_OK) 
  {   
      // get length of document (is -1 when Server sends no Content-Length header)
      totalLength = clientWeb.getSize();
      // transfer to local variable
      int len = totalLength;
      // this is required to start firmware update process
      Update.begin(UPDATE_SIZE_UNKNOWN);
      Update.onProgress(progressFunc);
      DEBUG_PRINT("FW Size: ");
      
      DEBUG_PRINTLN(totalLength);
      // create buffer for read
      uint8_t buff[128] = { 0 };
      // get tcp stream
      WiFiClient * stream = clientWeb.getStreamPtr();
      // read all data from server
      DEBUG_PRINTLN("Updating firmware...");
      while(clientWeb.connected() && (len > 0 || len == -1)) {
          // get available data size
          size_t size = stream->available();
          if(size) {
            // read up to 128 byte
            int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
            // pass to function
            runUpdateFirmware(buff, c);
            if(len > 0) {
                len -= c;
            }
          }
          //DEBUG_PRINT("Bytes left to flash ");
          //DEBUG_PRINTLN(len);
           //delay(1);
      }
  }
  else
  {
    DEBUG_PRINTLN("Cannot download firmware file. Only HTTP response 200: OK is supported. Double check firmware location #defined in UPD_FILE.");
  }
  clientWeb.end();
}

void handleToolUpdate(AsyncWebServerRequest *request)
{
    String result;
    result += F("<html>");
    result += FPSTR(HTTP_HEADER);
    result += FPSTR(HTTP_MENU);
    result.replace("{{FormattedDate}}", FormattedDate);
    
    result += FPSTR(HTTP_UPDATE);
    result+=footer();
    result.replace("{{linkFirmware}}", UPD_FILE);
    result += F("</html>");

    request->send(200, F("text/html"), result);
    //checkUpdateFirmware();
}



void handleConfigFiles(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h4>Config files</h4>");
  result += F("<nav id='navbar-custom' class='navbar navbar-default navbar-fixed-left'>");
  result += F("      <div class='navbar-header'>");
  result += F("        <!--<a class='navbar-brand' href='#'>Brand</a>-->");
  result += F("      </div>");
  result += F("<ul class='nav navbar-nav'>");

  String str = "";
  File root = LittleFS.open("/config");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      // tmp = tmp.substring(10);
      result += F("<li><a href='#' onClick=\"readfile('");
      result += tmp;
      result += F("','config');document.getElementById('actions').style.display='block';\">");
      result += tmp;
      result += F(" ( ");
      result += file.size();
      result += F(" o)</a></li>");
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  result += F("</ul></nav>");
  result += F("<div class='container-fluid' >");
  result += F("  <div class='app-main-content'>");
  result += F("<form method='POST' action='saveFileConfig'>");
  result += F("<div class='form-group'>");
  result += F(" <label for='file'>File : <span id='title'></span></label>");
  result += F("<input type='hidden' name='filename' id='filename' value=''>");
  result += F(" <textarea class='form-control' id='file' name='file' rows='10'>");
  result += F("</textarea>");
  result += F("</div>");
  result += F("<div id='actions' style='display:none;'>");
  result += F("<button type='submit' value='save' name='action' class='btn btn-warning mb-2'>Save</button>");
   result += F("</div>");

  result += F("</Form>");
  result += F("</div>");
  result += F("</div>");
  result += F("</body>");
  result+=footer();
  result += F("</html>");
  file.close();
  root.close();
  request->send(200, F("text/html"), result);
}

void handleDebugFiles(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h4>Debug files</h4>");
  result += F("<nav id='navbar-custom' class='navbar navbar-default navbar-fixed-left'>");
  result += F("      <div class='navbar-header'>");
  result += F("        <!--<a class='navbar-brand' href='#'>Brand</a>-->");
  result += F("      </div>");
  result += F("<ul class='nav navbar-nav'>");

  String str = "";
  File root = LittleFS.open("/debug");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      // tmp = tmp.substring(10);
      result += F("<li><a href='#' onClick=\"readfile('");
      result += tmp;
      result += F("','debug');document.getElementById('actions').style.display='block';\">");
      result += tmp;
      result += F(" ( ");
      result += file.size();
      result += F(" o)</a></li>");
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  result += F("</ul></nav>");
  result += F("<div class='container-fluid' >");
  result += F("  <div class='app-main-content'>");
  result += F("<form method='POST' action='saveDebug'>");
  result += F("<div class='form-group'>");
  result += F(" <label for='file'>File : <span id='title'></span></label>");
  result += F("<input type='hidden' name='filename' id='filename' value=''>");
  result += F(" <textarea class='form-control' id='file' name='file' rows='10'>");
  result += F("</textarea>");
  result += F("</div>");
  result += F("<div id='actions' style='display:none;'>");
  result += F("<button type='submit' class='btn btn-danger mb-2' name='delete' value='delete' onClick=\"if (confirm('Are you sure ?')==true){return true;}else{return false;};\">Delete</button>");
  result += F("</div>");
  result += F("<button type='submit' class='btn btn-danger mb-2' name='deleteAll' value='deleteAll' onClick=\"if (confirm('Are you sure ?')==true){return true;}else{return false;};\">Delete ALL</button>");

  result += F("</Form>");
  result += F("</div>");
  result += F("</div>");
  result += F("</body>");
  result+=footer();
  result += F("</html>");
  file.close();
  root.close();
  request->send(200, F("text/html"), result);
}


void handleFSbrowserBackup(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h4>backup list files</h4>");
  result += F("<nav id='navbar-custom' class='navbar navbar-default navbar-fixed-left'>");
  result += F("      <div class='navbar-header'>");
  result += F("        <!--<a class='navbar-brand' href='#'>Brand</a>-->");
  result += F("      </div>");
  result += F("<ul class='nav navbar-nav'>");

  String str = "";
  File root = LittleFS.open("/bk");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      // tmp = tmp.substring(10);
      result += F("<li><a href='#' onClick=\"readfile('");
      result += tmp;
      result += F("','bk');document.getElementById('actions').style.display='block';\">");
      result += tmp;
      result += F(" ( ");
      result += file.size();
      result += F(" o)</a></li>");
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  result += F("</ul></nav>");
  result += F("<div class='container-fluid' >");
  result += F("  <div class='app-main-content'>");
  result += F("<form method='POST' action='saveFileDatabase'>");
  result += F("<div class='form-group'>");
  result += F(" <label for='file'>File : <span id='title'></span></label>");
  result += F("<input type='hidden' name='filename' id='filename' value=''>");
  result += F(" <textarea class='form-control' id='file' name='file' rows='10'>");
  result += F("</textarea>");
  result += F("</div>");
  result += F("<div id='actions' style='display:none;'>");
  result += F("<button type='submit' class='btn btn-warning mb-2' name='save' value='save'>Save</button>&nbsp;");
  result += F("<button type='submit' class='btn btn-danger mb-2' name='delete' value='delete' onClick=\"if (confirm('Are you sure ?')==true){return true;}else{return false;};\">Delete</button>");
  result += F("</div>");
  result += F("</Form>");
  result += F("</div>");
  result += F("</div>");
  result += F("</body>");
  result+=footer();
  result += F("</html>");
  file.close();
  root.close();
  request->send(200, F("text/html"), result);
}

void handleFSbrowser(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h4>Devices list files</h4>");
  result += F("<div align='right'><a href='/createDevice' class='btn btn-primary mb-2'>+ New</a></div>");
  result += F("<nav id='navbar-custom' class='navbar navbar-default navbar-fixed-left'>");
  result += F("      <div class='navbar-header'>");
  result += F("        <!--<a class='navbar-brand' href='#'>Brand</a>-->");
  result += F("      </div>");
  result += F("<ul class='nav navbar-nav'>");

  String str = "";
  File root = LittleFS.open("/db");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      // tmp = tmp.substring(10);
      result += F("<li><a href='#' onClick=\"readfile('");
      result += tmp;
      result += F("','db');document.getElementById('actions').style.display='block';\">");
      result += tmp;
      result += F(" ( ");
      result += file.size();
      result += F(" o)</a></li>");
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  result += F("</ul></nav>");
  result += F("<div class='container-fluid' >");
  result += F("  <div class='app-main-content'>");
  result += F("<form method='POST' action='saveFileDatabase'>");
  result += F("<div class='form-group'>");
  result += F(" <label for='file'>File : <span id='title'></span></label>");
  result += F("<input type='hidden' name='filename' id='filename' value=''>");
  result += F(" <textarea class='form-control' id='file' name='file' rows='10'>");
  result += F("</textarea>");
  result += F("</div>");
  result += F("<div id='actions' style='display:none;'>");
  result += F("<button type='submit' class='btn btn-warning mb-2' name='save' value='save'>Save</button>&nbsp;");
  result += F("<button type='submit' class='btn btn-danger mb-2' name='delete' value='delete' onClick=\"if (confirm('Are you sure ?')==true){return true;}else{return false;};\">Delete</button>");
  result += F("</div>");
  result += F("</Form>");
  result += F("</div>");
  result += F("</div>");
  result += F("</body>");
  result+=footer();
  result += F("</html>");
  file.close();
  root.close();
  request->send(200, F("text/html"), result);
}

void handleCreateDevice(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CREATE_DEVICE);
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);
  request->send(200, "text/html", result);
}

void handleCreateTemplate(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CREATE_TEMPLATE);
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);
  request->send(200, "text/html", result);
}
void handleTemplates(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h4>Templates</h4>");
  result += F("<div align='right'><a href='/createTemplate' class='btn btn-primary mb-2'>+ New</a></div>");
  result += F("<nav id='navbar-custom' class='navbar navbar-default navbar-fixed-left'>");
  result += F("      <div class='navbar-header'>");
  result += F("        <!--<a class='navbar-brand' href='#'>Brand</a>-->");
  result += F("      </div>");
  result += F("<ul class='nav navbar-nav'>");

  String str = "";
  File root = LittleFS.open("/tp");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      // tmp = tmp.substring(11);
      result += F("<li><a href='#' onClick=\"readfile('");
      result += tmp;
      result += F("','tp');document.getElementById('actions').style.display = 'block';\">");
      result += tmp;
      result += F(" ( ");
      result += file.size();
      result += F(" o)</a></li>");
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  result += F("</ul></nav>");
  result += F("<div class='container-fluid' >");
  result += F("  <div class='app-main-content'>");
  result += F("<form method='POST' action='saveFileTemplates'>");
  result += F("<div class='form-group'>");
  result += F(" <label for='file'>File : <span id='title'></span></label>");
  result += F("<input type='hidden' name='filename' id='filename' value=''>");
  result += F(" <textarea class='form-control' id='file' name='file' rows='10'>");
  result += F("</textarea>");
  result += F("</div>");
  result += F("<div id='actions' style='display:none;'>");
  result += F("<button type='submit' name='save' value='save' class='btn btn-warning mb-2'>Save</button>&nbsp;");
  result += F("<button type='submit' name='delete' value='delete' class='btn btn-danger mb-2' onClick=\"if (confirm('Are you sure ?')==true){return true;}else{return false;};\">Delete</button>");
  result += F("</div>");
  result += F("</Form>");

  result += F("</div>");
  result += F("</div>");
  result += F("</body>");
  result+=footer();
  result += F("</html>");
  file.close();
  root.close();
  request->send(200, F("text/html"), result);
}

void handleRules(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h4>Rules</h4>");
  result += F("<nav id='navbar-custom' class='navbar navbar-default navbar-fixed-left'>");
  result += F("      <div class='navbar-header'>");
  result += F("        <!--<a class='navbar-brand' href='#'>Brand</a>-->");
  result += F("      </div>");
  result += F("<ul class='nav navbar-nav'>");
  String str = "";
  File root = LittleFS.open("/config");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      if (strcmp(file.name(),"rules.json")==0)
      {
        // tmp = tmp.substring(11);
        result += F("<li><a href='#' onClick=\"readfile('");
        result += tmp;
        result += F("','config');document.getElementById('actions').style.display = 'block';\">");
        result += tmp;
        result += F(" ( ");
        result += file.size();
        result += F(" o)</a></li>");
      }
      
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  file.close();
  root.close();

  result += F("</ul></nav>");
  result += F("<div class='container-fluid' >");
  result += F("  <div class='app-main-content'>");
  result += F("<form method='POST' action='saveFileRules'>");
  result += F("<div class='form-group'>");
  result += F(" <label for='file'>File : <span id='title'></span></label>");
  result += F("<input type='hidden' name='filename' id='filename' value=''>");
  result += F(" <textarea class='form-control' id='file' name='file' rows='10'>");
  result += F("</textarea>");
  result += F("</div>");
  result += F("<div id='actions' style='display:none;'>");
  result += F("<button type='submit' name='save' value='save' class='btn btn-warning mb-2'>Save</button>&nbsp;");
  result += F("</div>");
  result += F("</Form>");

  result += F("</div>");
  result += F("</div>");
  result += F("</body>");
  result+=footer();
  result += F("</html>");
  
  request->send(200, F("text/html"), result);
}

void handleSaveDevice(AsyncWebServerRequest *request)
{
  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
  }
  else
  {
    uint8_t i = 0;

    String filename = "/db/" + request->arg(i) +".json";
    String content = request->arg(1);
    String action = request->arg(2);

    if (action == "save")
    {
      File file = LittleFS.open(filename.c_str(), "w+");
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Failed to open file for reading\r\n"));
        file.close();
        return;
      }
      
      int bytesWritten = file.print(content);

      if (bytesWritten > 0)
      {
        DEBUG_PRINTLN(F("File was written"));
        DEBUG_PRINTLN(bytesWritten);
      }
      else
      {
        DEBUG_PRINTLN(F("File write failed"));
      }

      file.close();
    }
    else if (action == "delete")
    {
      LittleFS.remove(filename);
    }
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/fsbrowser"));
    request->send(response);
  }
}

void handleSaveTemplates(AsyncWebServerRequest *request)
{
  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
  }
  else
  {
    uint8_t i = 0;

    String filename = "/tp/" + request->arg(i);
    String content = request->arg(1);
    String action = request->arg(2);

    if (action == "save")
    {
      File file = LittleFS.open(filename.c_str(), "w+");
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Failed to open file for reading\r\n"));
        file.close();
        return;
      }
      
      int bytesWritten = file.print(content);

      if (bytesWritten > 0)
      {
        DEBUG_PRINTLN(F("File was written"));
        DEBUG_PRINTLN(bytesWritten);
      }
      else
      {
        DEBUG_PRINTLN(F("File write failed"));
      }

      file.close();
    }
    else if (action == "delete")
    {
      LittleFS.remove(filename);
    }
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/tp"));
    request->send(response);
  }
}

void handleSaveRules(AsyncWebServerRequest *request)
{
  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
  }
  else
  {
    uint8_t i = 0;

    String filename = "/config/" + request->arg(i);
    String content = request->arg(1);
    String action = request->arg(2);

    if (action == "save")
    {
      File file = LittleFS.open(filename.c_str(), "w+");
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Failed to open file for reading\r\n"));
        file.close();
        return;
      }
      
      int bytesWritten = file.print(content);

      if (bytesWritten > 0)
      {
        DEBUG_PRINTLN(F("File was written"));
        DEBUG_PRINTLN(bytesWritten);
      }
      else
      {
        DEBUG_PRINTLN(F("File write failed"));
      }

      file.close();
    }
    else if (action == "delete")
    {
      LittleFS.remove(filename);
    }
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/rules"));
    request->send(response);
  }
}


void handleJavascript(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h4>Javascript</h4>");
  result += F("<nav id='navbar-custom' class='navbar navbar-default navbar-fixed-left'>");
  result += F("      <div class='navbar-header'>");
  result += F("        <!--<a class='navbar-brand' href='#'>Brand</a>-->");
  result += F("      </div>");
  result += F("<ul class='nav navbar-nav'>");

  String str = "";
  File root = LittleFS.open("/web/js");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      DEBUG_PRINTLN(tmp);
      if (tmp == "functions.js")
      {
        // tmp = tmp.substring(11);
        result += F("<li><a href='#' onClick=\"readfile('");
        result += tmp;
        result += F("','web/js');document.getElementById('actions').style.display = 'block';\">");
        result += tmp;
        result += F(" ( ");
        result += file.size();
        result += F(" o)</a></li>");
      }
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  result += F("</ul></nav>");
  result += F("<div class='container-fluid' >");
  result += F("  <div class='app-main-content'>");
  result += F("<form method='POST' action='saveFileJavascript'>");
  result += F("<div class='form-group'>");
  result += F(" <label for='file'>File : <span id='title'></span></label>");
  result += F("<input type='hidden' name='filename' id='filename' value=''>");
  result += F(" <textarea class='form-control' id='file' name='file' rows='10'>");
  result += F("</textarea>");
  result += F("</div>");
  result += F("<div id='actions' style='display:none;'>");
  result += F("<button type='submit' name='save' value='save' class='btn btn-warning mb-2'>Save</button>");
  result += F("</div>");
  result += F("</Form>");
  result += F("</div>");
  result += F("</div>");
  result += F("</body>");
  result+=footer();
  result += F("</html>");
  file.close();
  root.close();
  request->send(200, F("text/html"), result);
}

void handleSaveJavascript(AsyncWebServerRequest *request)
{
  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
  }
  else
  {
    uint8_t i = 0;

    String filename = "/web/js/" + request->arg(i);
    String content = request->arg(1);
    String action = request->arg(2);

    if (action == "save")
    {
      File file = LittleFS.open(filename, "w+");
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Failed to open file for reading\r\n"));
        file.close();
        return;
      }

      int bytesWritten = file.print(content);

      if (bytesWritten > 0)
      {
        DEBUG_PRINTLN(F("File was written"));
        DEBUG_PRINTLN(bytesWritten);
      }
      else
      {
        DEBUG_PRINTLN(F("File write failed"));
      }

      file.close();
    }
    else if (action == "delete")
    {
      LittleFS.remove(filename);
    }
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/javascript"));
    request->send(response);
  }
}


void handleSaveDebug(AsyncWebServerRequest *request)
{
  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
  }
  else
  {
    uint8_t i = 0;
    String filename = "/debug/" + request->arg(i);
    String content = request->arg(1);
    String action = request->arg(2);
    if (action == "delete")
    {
      LittleFS.remove(filename);
    }else if (action == "deleteAll")
    {
      String str = "";
      File root = LittleFS.open("/debug");
      File file = root.openNextFile();
      while (file)
      {
          if (!file.isDirectory())
          {
            String tmp = file.name();
            file.close();
            LittleFS.remove("/debug/"+tmp);
          }
          file.close();
          vTaskDelay(1);
          file = root.openNextFile();
      }
    }
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/debugFiles"));
    request->send(response);
  }
}

void handleSaveConfig(AsyncWebServerRequest *request)
{
  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
  }
  else
  {
    uint8_t i = 0;
    String filename = "/config/" + request->arg(i);
    String content = request->arg(1);
    String action = request->arg(2);
    if (action == "save")
    {
      File file = LittleFS.open(filename, "w+");
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Failed to open file for reading\r\n"));
        file.close();
        return;
      }

      int bytesWritten = file.print(content);

      if (bytesWritten > 0)
      {
        DEBUG_PRINTLN(F("File was written"));
        DEBUG_PRINTLN(bytesWritten);
      }
      else
      {
        DEBUG_PRINTLN(F("File write failed"));
      }

      file.close();
    }
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/configFiles"));
    request->send(response);
  }
}
void handleSaveDatabase(AsyncWebServerRequest *request)
{
  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
  }
  else
  {
    uint8_t i = 0;
    String filename = "/db/" + request->arg(i);
    String content = request->arg(1);
    String action = request->arg(2);

    if (action == "save")
    {
      File file = LittleFS.open(filename, "w+");
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Failed to open file for reading\r\n"));
        file.close();
        return;
      }

      int bytesWritten = file.print(content);

      if (bytesWritten > 0)
      {
        DEBUG_PRINT(F("File was written : "));
        DEBUG_PRINTLN(bytesWritten);
      }
      else
      {
        DEBUG_PRINT(F("File write failed : "));
        DEBUG_PRINTLN(filename);
        
      }

      file.close();
    }
    else if (action == "delete")
    {
      LittleFS.remove(filename);
    }
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/fsbrowser"));
    request->send(response);
  }
}

void handleReadfile(AsyncWebServerRequest *request)
{
  String result;
  int i = 0;
  String repertory = request->arg(i);
  String filename = "/" + repertory + "/" + request->arg(1);
  DEBUG_PRINTLN(filename);
  File file = LittleFS.open(filename, "r");

  if (!file || file.isDirectory())
  {
    file.close();
    return;
  }

  while (file.available())
  {
    result += (char)file.read();
  }
  file.close();
  request->send(200, F("text/html"), result);
}

void handleLogBuffer(AsyncWebServerRequest *request)
{
  String result;
  result = logPrint();
  request->send(200, F("text/html"), result);
}

void handleScanNetwork(AsyncWebServerRequest * request)
{
   String result="";
  
   int n = WiFi.scanNetworks();

   if (n == 0) {
      result = " <label for='ssid'>SSID</label>";
      result += "<input class='form-control' id='ssid' type='text' name='WIFISSID' value='{{ssid}}'> <a onclick='scanNetwork();' class='btn btn-primary mb-2'>Scan</a><div id='networks'></div>";
    } else {
      
       result = "<select name='WIFISSID' onChange='updateSSID(this.value);'>";
       result += "<OPTION value=''>--Choose SSID--</OPTION>";
       for (int i = 0; i < n; ++i) {
            result += "<OPTION value='";
            result +=WiFi.SSID(i);
            result +="'>";
            result +=WiFi.SSID(i)+" ("+WiFi.RSSI(i)+")";
            result+="</OPTION>";
        }
        result += "</select>";
    }  
    request->send(200, F("text/html"), String(n)+"|"+result);
}

void handleClearConsole(AsyncWebServerRequest *request)
{
  logClear();

  request->send(200, F("text/html"), "");
}

void handleGetVersion(AsyncWebServerRequest *request)
{
  commandList->push(Packet{0x0010, 0x0000, 0});
  request->send(200, F("text/html"), "");
}

void handleErasePDM(AsyncWebServerRequest *request)
{
  commandList->push(Packet{0x0012, 0x0000, 0});
  request->send(200, F("text/html"), "");
}

void handleStartNwk(AsyncWebServerRequest *request)
{
  commandList->push(Packet{0x0024, 0x0000, 0});
  request->send(200, F("text/html"), "");
}

void handleSetChannelMask(AsyncWebServerRequest *request)
{

  if (request->arg("param") != "")
  {
    uint8_t datas[4];
    int channel;

    Packet trame;
    channel = request->arg("param").toInt();
    datas[0] = (channel >> 24) & 0xFF;
    datas[1] = (channel >> 16) & 0xFF;
    datas[2] = (channel >> 8) & 0xFF;
    datas[3] = channel & 0xFF;
    trame.cmd = 0x0021;
    trame.len = 0x0004;
    memcpy(trame.datas, datas, 4);
    commandList->push(trame);
    DEBUG_PRINTLN(F("add networkState"));
    commandList->push(Packet{0x0011, 0x0000, 0});
  }

  request->send(200, F("text/html"), "");
}

void handlePermitJoin(AsyncWebServerRequest *request)
{
  uint8_t datas[4];
  Packet trame;
  datas[0] = 0xFF;
  datas[1] = 0xFC;
  datas[2] = 0x1E;
  datas[3] = 0x00;

  trame.cmd = 0x0049;
  trame.len = 0x0004;
  memcpy(trame.datas, datas, 4);

  PrioritycommandList->push(trame);
  //commandList->push(trame);
  alertList->push(Alert{"Permit Join : 30 sec", 2});
  request->send(200, F("text/html"), "");
}

void handleReset(AsyncWebServerRequest *request)
{
  commandList->push(Packet{0x0011, 0x0000, 0});
  request->send(200, F("text/html"), "");
}

void handleRawMode(AsyncWebServerRequest *request)
{
  uint8_t datas[1];
  Packet trame;
  datas[0] = 0x01;

  trame.cmd = 0x0002;
  trame.len = 0x0001;
  memcpy(trame.datas, datas, 1);
  DEBUG_PRINTLN(F("RawMode"));
  commandList->push(trame);

  request->send(200, F("text/html"), "");
}

void handleRawModeOff(AsyncWebServerRequest *request)
{
  uint8_t datas[1];
  Packet trame;
  datas[0] = 0x00;

  trame.cmd = 0x0002;
  trame.len = 0x0001;
  memcpy(trame.datas, datas, 1);
  DEBUG_PRINTLN(F("RawMode off"));
  commandList->push(trame);

  request->send(200, F("text/html"), "");
}

void handleActiveReq(AsyncWebServerRequest *request)
{
  uint8_t shrtAddr[2];
  shrtAddr[0] = 0x1F;
  shrtAddr[1] = 0x7A;
  /*SendActiveRequest(shrtAddr);*/
  SendBasicDescription(shrtAddr, 1);

  request->send(200, F("text/html"), "");
}

void handleSaveConfigGeneral(AsyncWebServerRequest *request)
{
  String path = "configGeneral.json";

  String enableDebug;
  if (request->arg("debugSerial") == "on")
  {
    enableDebug = "1";
    ConfigSettings.enableDebug = true;
  }
  else
  {
    enableDebug = "0";
    ConfigSettings.enableDebug = false;
  }
  config_write(path, "enableDebug", enableDebug);

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configGeneral"));
  request->send(response);
}

void handleSaveConfigHorloge(AsyncWebServerRequest *request)
{
  String path = "configGeneral.json";
  if (request->arg("ntpserver") != "")
  {
    strlcpy(ConfigGeneral.ntpserver, request->arg("ntpserver").c_str(), sizeof(ConfigGeneral.ntpserver));
    config_write(path, "ntpserver", String(request->arg("ntpserver")));
  }

  if (request->arg("timezone") != "")
  {
    strlcpy(ConfigGeneral.timezone, request->arg("timezone").c_str(), sizeof(ConfigGeneral.timezone));
    config_write(path, "timezone", String(request->arg("timezone")));
  }

  if (request->arg("timeoffset").toInt() < 10)
  {
    ConfigGeneral.timeoffset = request->arg("timeoffset").toInt();
    config_write(path, "timeoffset", String(request->arg("timeoffset")));
  }

  if (request->arg("epochtime").toDouble()>=0)
  {
    config_write(path, "epoch", String(request->arg("epochtime")));
    ConfigGeneral.epochTime = atol(request->arg("epochtime").c_str());

  }

  request->send(200, "text/html", "Save config OK ! <br><form method='GET' action='reboot'><input type='submit' name='reboot' value='Reboot'></form>");
  /*AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configHorloge"));
  request->send(response);*/
}

void handleSaveConfigLinky(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";

  if (request->arg("linkyDevice") != "")
  {
    strlcpy(ConfigGeneral.ZLinky, request->arg("linkyDevice").c_str(), sizeof(ConfigGeneral.ZLinky));
    config_write(path, "ZLinky", String(request->arg("linkyDevice")));
  }
  if (request->arg("tarifAbo").toFloat() >= 0)
  {
    // ConfigGeneral.tarifAbo = request->arg("tarifAbo");
    strlcpy(ConfigGeneral.tarifAbo, request->arg("tarifAbo").c_str(), sizeof(ConfigGeneral.tarifAbo));
    config_write(path, "tarifAbo", String(request->arg("tarifAbo")));
  }

  if (request->arg("tarifCSPE").toFloat() >= 0)
  {
    // ConfigGeneral.tarifCSPE = request->arg("tarifCSPE");
    strlcpy(ConfigGeneral.tarifCSPE, request->arg("tarifCSPE").c_str(), sizeof(ConfigGeneral.tarifCSPE));
    config_write(path, "tarifCSPE", String(request->arg("tarifCSPE")));
  }

  if (request->arg("tarifCTA").toFloat() >= 0)
  {
    // ConfigGeneral.tarifCTA = request->arg("tarifCTA");
    strlcpy(ConfigGeneral.tarifCTA, request->arg("tarifCTA").c_str(), sizeof(ConfigGeneral.tarifCTA));
    config_write(path, "tarifCTA", String(request->arg("tarifCTA")));
  }

  if (request->arg("tarifIdx1").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx1, request->arg("tarifIdx1").c_str(), sizeof(ConfigGeneral.tarifIdx1));
    config_write(path, "tarifIdx1", String(request->arg("tarifIdx1")));
  }

  if (request->arg("tarifIdx2").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx2, request->arg("tarifIdx2").c_str(), sizeof(ConfigGeneral.tarifIdx2));
    config_write(path, "tarifIdx2", String(request->arg("tarifIdx2")));
  }

  if (request->arg("tarifIdx3").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx3, request->arg("tarifIdx3").c_str(), sizeof(ConfigGeneral.tarifIdx3));
    config_write(path, "tarifIdx3", String(request->arg("tarifIdx3")));
  }

  if (request->arg("tarifIdx4").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx4, request->arg("tarifIdx4").c_str(), sizeof(ConfigGeneral.tarifIdx4));
    config_write(path, "tarifIdx4", String(request->arg("tarifIdx4")));
  }

  if (request->arg("tarifIdx5").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx5, request->arg("tarifIdx5").c_str(), sizeof(ConfigGeneral.tarifIdx5));
    config_write(path, "tarifIdx5", String(request->arg("tarifIdx5")));
  }

  if (request->arg("tarifIdx6").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx6, request->arg("tarifIdx6").c_str(), sizeof(ConfigGeneral.tarifIdx6));
    config_write(path, "tarifIdx6", String(request->arg("tarifIdx6")));
  }

  if (request->arg("tarifIdx7").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx7, request->arg("tarifIdx7").c_str(), sizeof(ConfigGeneral.tarifIdx7));
    config_write(path, "tarifIdx7", String(request->arg("tarifIdx7")));
  }

  if (request->arg("tarifIdx8").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx8, request->arg("tarifIdx8").c_str(), sizeof(ConfigGeneral.tarifIdx8));
    config_write(path, "tarifIdx8", String(request->arg("tarifIdx8")));
  }

  if (request->arg("tarifIdx9").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx9, request->arg("tarifIdx9").c_str(), sizeof(ConfigGeneral.tarifIdx9));
    config_write(path, "tarifIdx9", String(request->arg("tarifIdx9")));
  }

  if (request->arg("tarifIdx10").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx10, request->arg("tarifIdx10").c_str(), sizeof(ConfigGeneral.tarifIdx10));
    config_write(path, "tarifIdx10", String(request->arg("tarifIdx10")));
  }

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configLinky"));
  request->send(response);
}

void handleSaveConfigGaz(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";

  strlcpy(ConfigGeneral.Gaz, request->arg("gazDevice").c_str(), sizeof(ConfigGeneral.Gaz));
  config_write(path, "Gaz", String(request->arg("gazDevice")));

  if (request->arg("tarifGaz").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifGaz, request->arg("tarifGaz").c_str(), sizeof(ConfigGeneral.tarifGaz));
    config_write(path, "tarifGaz", String(request->arg("tarifGaz")));
  }

  if (request->arg("coeffGaz").toFloat() >= 0)
  {
    ConfigGeneral.coeffGaz= request->arg("coeffGaz").toFloat();
    config_write(path, "coeffGaz", String(request->arg("coeffGaz")));
  }

  if (request->arg("unitGaz") != "")
  {
    strlcpy(ConfigGeneral.unitGaz, request->arg("unitGaz").c_str(), sizeof(ConfigGeneral.unitGaz));
    config_write(path, "unitGaz", String(request->arg("unitGaz")));
  }

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configGaz"));
  request->send(response);
}

void handleSaveConfigWater(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";

  strlcpy(ConfigGeneral.Water, request->arg("waterDevice").c_str(), sizeof(ConfigGeneral.Water));
  config_write(path, "Water", String(request->arg("waterDevice")));

  if (request->arg("tarifWater").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifWater, request->arg("tarifWater").c_str(), sizeof(ConfigGeneral.tarifWater));
    config_write(path, "tarifWater", String(request->arg("tarifWater")));
  }

  if (request->arg("coeffWater").toFloat() >= 0)
  {
    ConfigGeneral.coeffWater= request->arg("coeffWater").toFloat();
    config_write(path, "coeffWater", String(request->arg("coeffWater")));
  }

  if (request->arg("unitWater") != "")
  {
    strlcpy(ConfigGeneral.unitWater, request->arg("unitWater").c_str(), sizeof(ConfigGeneral.unitWater));
    config_write(path, "unitWater", String(request->arg("unitWater")));
  }

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configWater"));
  request->send(response);
}

void handleSaveConfigMQTT(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";
  String enableMqtt;
  if (request->arg("enableMqtt") == "on")
  {
    enableMqtt = "1";
    ConfigSettings.enableMqtt = true;
  }
  else
  {
    enableMqtt = "0";
    ConfigSettings.enableMqtt = false;
  }
  config_write(path, "enableMqtt", enableMqtt);
  if (request->arg("servMQTT"))
  {
    strlcpy(ConfigGeneral.servMQTT, request->arg("servMQTT").c_str(), sizeof(ConfigGeneral.servMQTT));
    config_write(path, "servMQTT", String(request->arg("servMQTT")));
  }

  if (request->arg("portMQTT"))
  {
    strlcpy(ConfigGeneral.portMQTT, request->arg("portMQTT").c_str(), sizeof(ConfigGeneral.portMQTT));
    config_write(path, "portMQTT", String(request->arg("portMQTT")));
  }

  if (request->arg("clientIDMQTT"))
  {
    strlcpy(ConfigGeneral.clientIDMQTT, request->arg("clientIDMQTT").c_str(), sizeof(ConfigGeneral.clientIDMQTT));
    config_write(path, "clientIDMQTT", String(request->arg("clientIDMQTT")));
  }

  if (request->arg("userMQTT"))
  {
    strlcpy(ConfigGeneral.userMQTT, request->arg("userMQTT").c_str(), sizeof(ConfigGeneral.userMQTT));
    config_write(path, "userMQTT", String(request->arg("userMQTT")));
  }

  if (request->arg("passMQTT"))
  {
    if (String(request->arg("passMQTT"))!="********")
    {
      strlcpy(ConfigGeneral.passMQTT, request->arg("passMQTT").c_str(), sizeof(ConfigGeneral.passMQTT));
      config_write(path, "passMQTT", String(request->arg("passMQTT")));
    }
  }

  if (request->arg("headerMQTT"))
  {
    strlcpy(ConfigGeneral.headerMQTT, request->arg("headerMQTT").c_str(), sizeof(ConfigGeneral.headerMQTT));
    config_write(path, "headerMQTT", String(request->arg("headerMQTT")));
  }

  if (String(request->arg("appliMQTT")) == "HA")
  {
    ConfigGeneral.HAMQTT = true;
    ConfigGeneral.TBMQTT = false;
    ConfigGeneral.customMQTT = false;
    config_write(path, "HAMQTT", "1");
    config_write(path, "TBMQTT", "0");
    config_write(path, "customMQTT", "0");
  }

  if (String(request->arg("appliMQTT")) == "TB")
  {
    ConfigGeneral.HAMQTT = false;
    ConfigGeneral.TBMQTT = true;
    ConfigGeneral.customMQTT = false;
    config_write(path, "HAMQTT", "0");
    config_write(path, "TBMQTT", "1");
    config_write(path, "customMQTT", "0");
  }
  if (String(request->arg("appliMQTT")) == "custom")
  {
    ConfigGeneral.HAMQTT = false;
    ConfigGeneral.TBMQTT = false;
    ConfigGeneral.customMQTT = true;
    config_write(path, "HAMQTT", "0");
    config_write(path, "TBMQTT", "0");
    config_write(path, "customMQTT", "1");
    config_write(path,"customMQTTJson",String(request->arg("customMQTTJson")));
  }
  
  //MQTT connection process
  if (ConfigSettings.enableMqtt)
  {
    mqttClient.disconnect();
    mqttClient.setServer(ConfigGeneral.servMQTT, atoi(ConfigGeneral.portMQTT));
    mqttClient.setClientId(ConfigGeneral.clientIDMQTT);
    if (String(ConfigGeneral.userMQTT) !="")
    {
      mqttClient.setCredentials(ConfigGeneral.userMQTT, ConfigGeneral.passMQTT);
    }
    mqttClient.connect();
    
  }else{
    mqttClient.disconnect();
  }
  
  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configMQTT"));
  request->send(response);
}

void handleSaveConfigHTTP(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";
  String enableHTTP;
  if (request->arg("enableSecureHttp") == "on")
  {
    enableHTTP = "1";
    ConfigSettings.enableSecureHttp = true;
  }
  else
  {
    enableHTTP = "0";
    ConfigSettings.enableSecureHttp = false;
  }
  config_write(path, "enableSecureHttp", enableHTTP);

  String user = request->arg("userHTTP");
  String pass = request->arg("passHTTP");
  bool saveOk=true;
  uint8_t error=0;
  
  if (request->arg("enableSecureHttp") == "on")
  {

    if (strlen(pass.c_str())<4)
    {
      saveOk=saveOk & false;  
      error=error+1;
    }  

    if (user == "")
    {
      saveOk=saveOk & false;  
      error=error+2;
    }
  }

  if (saveOk)
  {
     if (request->arg("userHTTP"))
    {
      strlcpy(ConfigGeneral.userHTTP, request->arg("userHTTP").c_str(), sizeof(ConfigGeneral.userHTTP));
      config_write(path, "userHTTP", String(request->arg("userHTTP")));
    }

    if (pass=="********")
    {
      pass = ConfigGeneral.passHTTP;
      strlcpy(ConfigGeneral.passHTTP, pass.c_str(), sizeof(ConfigGeneral.passHTTP));
      config_write(path, "passHTTP", pass);
    }else{
      strlcpy(ConfigGeneral.passHTTP, pass.c_str(), sizeof(ConfigGeneral.passHTTP));
      config_write(path, "passHTTP", pass);
    }

    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/configHTTP"));
    request->send(response);
  }else{
    AsyncWebServerResponse *response = request->beginResponse(303);
    String url="/configHTTP?error="+String(error);
    response->addHeader(F("Location"), url);
    request->send(response);
  }
}

void handleSaveConfigWebPush(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";
  String enableWebPush,webPushAuth;
  
  
  String user = request->arg("userWebPush");
  String pass = request->arg("passWebPush");
  String url = request->arg("servWebPush");
  bool saveOk=true;
  uint8_t error=0;
  
  if (request->arg("webPushAuth") == "on")
  {

    if (strlen(pass.c_str())<4)
    {
      saveOk=false;  
      error=error+1;
    }  

    if (user == "")
    {
      saveOk=false;  
      error=error+2;
    }
  }

  if (request->arg("enableWebPush") == "on")
  {
    if (url == "")
    {
      saveOk = false;
      error=error+4;
    }
  }

  if (saveOk)
  {
    if (request->arg("enableWebPush") == "on")
    {
      enableWebPush = "1";
      ConfigSettings.enableWebPush = true;
    }
    else
    {
      enableWebPush = "0";
      ConfigSettings.enableWebPush = false;
    }
    if (request->arg("webPushAuth") == "on")
    {
      webPushAuth = "1";
      ConfigGeneral.webPushAuth = true;
    }
    else
    {
      enableWebPush = "0";
      ConfigGeneral.webPushAuth = false;
    }
    config_write(path, "enableWebPush", enableWebPush);
    config_write(path, "webPushAuth", webPushAuth);

    if (request->arg("servWebPush"))
    {
      strlcpy(ConfigGeneral.servWebPush, request->arg("servWebPush").c_str(), sizeof(ConfigGeneral.servWebPush));
      config_write(path, "servWebPush", String(request->arg("servWebPush")));
    }

    if (request->arg("userWebPush"))
    {
      strlcpy(ConfigGeneral.userWebPush, request->arg("userWebPush").c_str(), sizeof(ConfigGeneral.userWebPush));
      config_write(path, "userWebPush", String(request->arg("userWebPush")));
    }

    if (pass=="********")
    {
      pass = ConfigGeneral.passWebPush;
      strlcpy(ConfigGeneral.passWebPush, pass.c_str(), sizeof(ConfigGeneral.passWebPush));
      config_write(path, "passWebPush", pass);
    }else{
      strlcpy(ConfigGeneral.passWebPush, pass.c_str(), sizeof(ConfigGeneral.passWebPush));
      config_write(path, "passWebPush", pass);
    }

    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/configWebPush"));
    request->send(response);

  }else{
    AsyncWebServerResponse *response = request->beginResponse(303);
     String url="/configWebPush?error="+String(error);
    response->addHeader(F("Location"), url);
    request->send(response); 
  }
  
}

void handleSaveConfigMarstek(AsyncWebServerRequest *request)
{
  String path = "configGeneral.json";
  String enableMarstek;
  if (request->arg("enableMarstek") == "on")
  {
    enableMarstek = "1";
    ConfigSettings.enableMarstek = true;
  }
  else
  {
    enableMarstek = "0";
    ConfigSettings.enableMarstek = false;
  }
  config_write(path, "enableMarstek", enableMarstek);
  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configMarstek"));
  request->send(response);
}


void handleSaveConfigUDPClient(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";
  String enableUDP;
  
  String port = request->arg("portUDP");
  String url = request->arg("servUDP");
  bool saveOk=true;
  uint8_t error=0;
  
  if (request->arg("enableUDP") == "on")
  {
    if (url == "")
    {
      saveOk = false;
      error=error+2;
    }

    if (port == "")
    {
      saveOk = false;
      error=error+1;
    }
  }

  if (saveOk)
  {
    if (request->arg("enableUDP") == "on")
    {
      enableUDP = "1";
      ConfigSettings.enableUDP = true;
    }
    else
    {
      enableUDP = "0";
      ConfigSettings.enableUDP = false;
    }
    
    config_write(path, "enableUDP", enableUDP);

    if (request->arg("servUDP"))
    {
      strlcpy(ConfigGeneral.servUDP, request->arg("servUDP").c_str(), sizeof(ConfigGeneral.servUDP));
      config_write(path, "servUDP", String(request->arg("servUDP")));
    }

    if (request->arg("portUDP"))
    {
      strlcpy(ConfigGeneral.portUDP, request->arg("portUDP").c_str(), sizeof(ConfigGeneral.portUDP));
      config_write(path, "portUDP", String(request->arg("portUDP")));
    }

    if (request->arg("customUDPJson"))
    {
      ConfigGeneral.customUDPJson = String(request->arg("customUDPJson"));
      config_write(path, "customUDPJson", String(request->arg("customUDPJson")));
    }

   
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/configUdpClient"));
    request->send(response);

  }else{
    AsyncWebServerResponse *response = request->beginResponse(303);
     String url="/configUdpClient?error="+String(error);
    response->addHeader(F("Location"), url);
    request->send(response); 
  }
  
}


void handleSaveConfigNotification(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";
  String enableNotif;
  if (request->arg("enableNotif") == "on")
  {
    enableNotif = "1";
    ConfigSettings.enableNotif = true;
  }
  else
  {
    enableNotif = "0";
    ConfigSettings.enableNotif = false;
  }
  config_write(path, "enableNotif", enableNotif);
  if (request->arg("servSMTP"))
  {
    strlcpy(ConfigGeneral.servSMTP, request->arg("servSMTP").c_str(), sizeof(ConfigGeneral.servSMTP));
    config_write(path, "servSMTP", String(request->arg("servSMTP")));
  }

  if (request->arg("portSMTP"))
  {
    strlcpy(ConfigGeneral.portSMTP, request->arg("portSMTP").c_str(), sizeof(ConfigGeneral.portSMTP));
    config_write(path, "portSMTP", String(request->arg("portSMTP")));
  }

  if (request->arg("userSMTP"))
  {
    strlcpy(ConfigGeneral.userSMTP, request->arg("userSMTP").c_str(), sizeof(ConfigGeneral.userSMTP));
    config_write(path, "userSMTP", String(request->arg("userSMTP")));
  }

  if (request->arg("passSMTP"))
  {
    strlcpy(ConfigGeneral.passSMTP, request->arg("passSMTP").c_str(), sizeof(ConfigGeneral.passSMTP));
    config_write(path, "passSMTP", String(request->arg("passSMTP")));
  }

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configNotif"));
  request->send(response);
}

void handleSaveWifi(AsyncWebServerRequest *request)
{
  
  String StringConfig;
  String enableDHCP;
  if (request->arg("DHCPEnable") == "on")
  {
    enableDHCP = "1";
  }
  else
  {
    enableDHCP = "0";
  }
  String ssid = request->arg("WIFISSID");
  String pass = request->arg("WIFIpassword");
  String ipAddress = request->arg("ipAddress");
  String ipMask = request->arg("ipMask");
  String ipGW = request->arg("ipGW");
  String tcpListenPort = request->arg("tcpListenPort");
  bool saveOk=true;
  uint8_t error=0;
  uint8_t ipError=0;

  if (strlen(pass.c_str())<8)
  {
    saveOk=saveOk & false;  
    error=error+1;
  }  

  if (ssid == "")
  {
    saveOk=saveOk & false;  
    error=error+2;
  }

  if (request->arg("DHCPEnable") != "on")
  {
  
    if (!isValidIP(ipAddress))
    {
      saveOk=saveOk & false;
      ipError=1;
    }
    if (!isValidIP(ipMask))
    {
      saveOk=saveOk & false;
      ipError=ipError + 2;
    }

    if (!isValidIP(ipGW))
    {
      saveOk=saveOk & false;
      ipError=ipError + 4;
    }

  }

  if (saveOk)
  {
    if (pass=="********")
    {
      pass = ConfigSettings.password;
    }

    const char *path = "/config/configWifi.json";
    StringConfig = "{\"enableDHCP\":" + enableDHCP + ",\"ssid\":\"" + ssid + "\",\"pass\":\"" + pass + "\",\"ip\":\"" + ipAddress + "\",\"mask\":\"" + ipMask + "\",\"gw\":\"" + ipGW + "\",\"tcpListenPort\":\"" + tcpListenPort + "\"}";
    StaticJsonDocument<512> jsonBuffer;
    DynamicJsonDocument doc(MAXHEAP);
    deserializeJson(doc, StringConfig);

    File configFile = LittleFS.open(path, FILE_WRITE);
    if (!configFile)
    {
      DEBUG_PRINTLN(F("failed open"));
    }
    else
    {
      if (!doc.isNull())
      {
        serializeJson(doc, configFile);
      }
    }
    configFile.close();
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/reboot"));
    request->send(response);
    //request->send(200, "text/html", "Save config OK ! <br><form method='GET' action='reboot'><input type='submit' name='reboot' value='Reboot'></form>");
  }else{
    AsyncWebServerResponse *response = request->beginResponse(303);
    String url="/configWiFi?error="+String(error)+"&ipError="+String(ipError);
    response->addHeader(F("Location"), url);
    request->send(response);
  }
}

void handleConfigDevices(AsyncWebServerRequest *request)
{
  String result;

  result = F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_DEVICES_ZIGBEE);
  result+=footer();
  result = getMenuGeneralZigbee(result, "devices");
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("</html>");

  String str = "";
  String devices="";
  File root = LittleFS.open("/db");
  File file = root.openNextFile();
  int i=0;
  int exist = 0;
  while (file)
  {
    String tmp = file.name();
    // tmp = tmp.substring(10);
    if (tmp.substring(16) == ".json")
    {
      exist++;
      devices += F("<div class='col-md-auto col-sm-auto'><div class='card' style='min-width:380px;' ><div class='card-header'  style='font-size:12px;font-weight:bold;color:#FFF;background-color:#007bc6;' >@Mac : ");
      devices += tmp.substring(0, 16);
      devices += F("</div>");
      devices += F("<div class='card-body'>");
      devices += "<table width='100%' style='font-size:12px;'><tr>";
      devices += F("<td style='font-weight:bold;color:#555;width:60%;'>Manufacturer </td><td style='font-family :\"Courier New\", Courier, monospace;text-align:right;'>");
      String manufacturer;
      manufacturer = GetManufacturer(file.name());
      devices += manufacturer;
      devices += F("</td></tr><tr><td style='font-weight:bold;color:#555;width:60%;'>Model </td><td style='font-family :\"Courier New\", Courier, monospace;text-align:right;'>");
      String model;
      model = GetModel(file.name());
      devices += model;
      devices += F("</td></tr><tr><td style='font-weight:bold;color:#555;width:60%;'>Short Address </td><td style='font-family :\"Courier New\", Courier, monospace;text-align:right;'>");
      char SAddr[5];
      int ShortAddr = GetShortAddr(file.name());
      snprintf(SAddr,5, "%04X", ShortAddr);
      devices += SAddr;
      devices += F("</td></tr><tr><td style='font-weight:bold;color:#555;width:60%;'>Device Id </td><td style='font-family :\"Courier New\", Courier, monospace;text-align:right;'>");
      char devId[5];
      int DeviceId = GetDeviceId(file.name());
      snprintf(devId,5, "%04X", DeviceId);
      devices += devId;
      devices += F("</td></tr><tr><td style='font-weight:bold;color:#555;width:60%;'>Soft Version </td><td style='font-family :\"Courier New\", Courier, monospace;text-align:right;'>");
      String SoftVer = GetSoftwareVersion(file.name());
      devices += SoftVer;
      devices += F("</td></tr><tr><td style='font-weight:bold;color:#555;width:60%;'>Last seen </td><td style='font-family :\"Courier New\", Courier, monospace;text-align:right;'>");
      String lastseen = GetLastSeen(file.name());
      devices += lastseen;
      devices += F("</td></tr><tr><td style='font-weight:bold;color:#555;width:60%;'>LQI </td><td style='font-family :\"Courier New\", Courier, monospace;text-align:right;'>");
      devices += GetLQI(file.name());
      devices += "</td></tr></table>";
     
      // Paramétrages
      devices += F("<hr>");
        //infos
        
        devices += F("<button onclick=\"ZigbeeSendRequest(");
        devices += ShortAddr;
        devices += ",";
        if (model=="ZLinky_TIC")
        {
          devices += "0,16384";
        }else{
          devices += "0,1";
        }
        
        devices += ");\" class='btn btn-info mb-2'>";
        devices += "Version";
        devices += F("</button>");


        //modif
        devices +="<div align='right'>";
        if (ConfigSettings.enableMqtt && ConfigGeneral.HAMQTT )
        {
          devices += F("<button onclick=\"sendMqttDiscover('");
          devices += ShortAddr;
          devices += "');\" class='btn btn-warning mb-2'>";
          devices +="<svg role='img' viewBox='0 0 24 24' xmlns='http://www.w3.org/2000/svg' style='width:24px;' height='24' width='24'>";
          devices +=  "<path d='M10.657 23.994h-9.45A1.212 1.212 0 0 1 0 22.788v-9.18h0.071c5.784 0 10.504 4.65 10.586 10.386Zm7.606 0h-4.045C14.135 16.246 7.795 9.977 0 9.942V6.038h0.071c9.983 0 18.121 8.044 18.192 17.956Zm4.53 0h-0.97C21.754 12.071 11.995 2.407 0 2.372v-1.16C0 0.55 0.544 0.006 1.207 0.006h7.64C15.733 2.49 21.257 7.789 24 14.508v8.291c0 0.663 -0.544 1.195 -1.207 1.195ZM16.713 0.006h6.092A1.19 1.19 0 0 1 24 1.2v5.914c-0.91 -1.242 -2.046 -2.65 -3.158 -3.762C19.588 2.11 18.122 0.987 16.714 0.005Z' fill='currentColor' stroke-width='1'></path>";
          devices +="</svg>";
          //devices += "MQTT Discover";
          devices += F("</button>");
        }

        devices += F("<button onclick=\"ZigbeeSendRequest(");
        devices += ShortAddr;
        devices += ",";
        devices += "0,5";
        devices += ");\" class='btn btn-warning mb-2'>";
        devices +="<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='currentColor' class='bi bi-arrow-clockwise' viewBox='0 0 16 16'>";
        devices +=  "<path fill-rule='evenodd' d='M8 3a5 5 0 1 0 4.546 2.914.5.5 0 0 1 .908-.417A6 6 0 1 1 8 2z'/>";
        devices +=  "<path d='M8 4.466V.534a.25.25 0 0 1 .41-.192l2.36 1.966c.12.1.12.284 0 .384L8.41 4.658A.25.25 0 0 1 8 4.466'/>";
        devices +="</svg>";
        //devices += " Refresh";
        devices += F("</button>");
      
        devices += F("<button onclick=\"deleteDevice('");
        devices += tmp.substring(0, 16);
        devices += "');\" class='btn btn-danger mb-2'>";
        devices +="<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='currentColor' class='bi bi-trash' viewBox='0 0 16 16'>";
        devices +=  "<path d='M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z'/>";
        devices +=  "<path d='M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H6a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1h3.5a1 1 0 0 1 1 1zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z'/>";
        devices +="</svg>";
        //devices += " Delete";
        devices += F("</button>");
      devices += F("</div>");
      devices += F("</div>");
      devices += F("</div></div><br>");
      
    }
    
    i++;
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }

  if (exist>0)
  {
    result.replace("{{devicesList}}", devices);
  }else{
    result.replace("{{devicesList}}", "<div align='center' style='height:100px;font-size:28px;font-weight:bold;'>No devices yet</div> ");
  }

  file.close();
  root.close();
  request->send(200, F("text/html"), result);
}

void handleZigbeeAction(AsyncWebServerRequest *request)
{

  int ShortAddr, command;
  String tmpValue;
  int i = 0;
  ShortAddr = request->arg(i).toInt();
  command = request->arg(1).toInt();
  tmpValue = request->arg(2);
  SendAction(command, ShortAddr, tmpValue);

  request->send(200, F("text/html"), "");
}

void handleZigbeeSendRequest(AsyncWebServerRequest *request)
{

  int ShortAddr, cluster, attribute;
  int i = 0;
  ShortAddr = request->arg(i).toInt();
  cluster = request->arg(1).toInt();
  attribute = request->arg(2).toInt();
  SendAttributeRead(ShortAddr, cluster, attribute);

  request->send(200, F("text/html"), "");
}

void handleLoadPowerGaugeAbo(AsyncWebServerRequest *request)
{

  String IEEE, Attribute, Time, result;
  int i = 0;
  IEEE = request->arg(i);
  Attribute = request->arg(1);
  Time = request->arg(2);
  result = getPowerGaugeAbo(IEEE, Attribute, Time);

  request->send(200, F("text/html"), result);
}

void handleLoadGaugeDashboard(AsyncWebServerRequest *request)
{

  String IEEE, Cluster, Attribute, Type, Coefficient, result;
  int i = 0;
  IEEE = request->arg(i);
  Cluster = request->arg(1);
  Attribute = request->arg(2);
  Type = request->arg(3);
  Coefficient = request->arg(4);
  result = GetValueStatus(IEEE + ".json", Cluster.toInt(), Attribute.toInt(), (String)Type, Coefficient.toFloat());

  request->send(200, F("text/html"), result);
}

void handleLoadPowerGaugeTimeDay(AsyncWebServerRequest *request)
{

  String IEEE, Attribute, result;
  int i = 0;
  IEEE = request->arg(i);
  Attribute = request->arg(1);

  result = getPowerGaugeTimeDay(IEEE, Attribute);

  request->send(200, F("text/html"), result);
}

void handleLoadLinkyDatas(AsyncWebServerRequest *request)
{

  String IEEE, result;
  int i = 0;
  IEEE = request->arg(i);

  result = getLinkyDatas(IEEE);

  request->send(200, F("text/html"), result);
}

void handleRefreshLabel(AsyncWebServerRequest *request)
{

  String file,Shortaddr,Cluster, Attribute, Type, Coefficient, Unit, result;
  int i = 0;
  file = request->arg(i);
  Cluster = request->arg(1);
  Attribute = request->arg(2);
  Type = request->arg(3);
  Coefficient = request->arg(4);
  Unit = request->arg(5);

  result = GetValueStatus(file, Cluster.toInt(), Attribute.toInt(), Type, Coefficient.toFloat());

  request->send(200, F("text/html"), result);
}

void handleRefreshGaugeAbo(AsyncWebServerRequest *request)
{

  String IEEE, Attribute, Time, result;
  int i = 0;
  IEEE = request->arg(i);
  Attribute = request->arg(1);
  Time = request->arg(2);

  result = getLastValuePower(IEEE, Attribute, Time);

  request->send(200, F("text/html"), result);
}
void handleLoadPowerTrend(AsyncWebServerRequest *request)
{

  String IEEE, Attribute, Time, result;
  int i = 0;
  IEEE = request->arg(i);
  Attribute = request->arg(1);
  Time = request->arg(2);

  result = getTrendPower(IEEE, Attribute, Time);

  request->send(200, F("text/html"), result);
}

void handleLoadPowerChart(AsyncWebServerRequest *request)
{

  String IEEE, Attribute, result;

  int i = 0;
  IEEE = request->arg(i);
  Attribute = request->arg(1);
  
  //result = getPowerDatas(IEEE, "power", Attribute, "minute");
 
  //request->send(200, F("application/json"), result);



  //request->send(LittleFS, "/db/"+Attribute+"_"+IEEE+".json", "application/json");
  request->send(LittleFS, "/db/pwr_"+IEEE+".json", "application/json");

  /*String path = "/db/"+Attribute+"_"+IEEE+".json";
  File DeviceFile = LittleFS.open(path, FILE_READ);
  if (!DeviceFile|| DeviceFile.isDirectory()) {
    DEBUG_PRINTLN(F("failed open"));
  }else
  {
    AsyncResponseStream *response = request->beginResponseStream("application/json",[](uint8_t *buffer, size_t maxLen, size_t index)->size_t {
      while (DeviceFile.available()) {
        response->print(DeviceFile.read());
      }  
      DeviceFile.close();
    });
    request->send(response);
  }*/

 
}



void handleLoadEnergyChart(AsyncWebServerRequest *request)
{

  String IEEE, result, type, time;
  String sep = "";
  int i = 0;
  IEEE = request->arg(i);
  time = request->arg(1);
  
  String path = "/db/nrg_" + IEEE + ".json";

  File DeviceFile = LittleFS.open(path, FILE_READ);
  if (!DeviceFile || DeviceFile.isDirectory())
  {
    DEBUG_PRINTLN(F("failed open"));
  }
  else
  {
    JsonObject root;
    DeserializationError error;
    if (time == "hour")
    {
      StaticJsonDocument<32> filter;
      filter["hours"]["graph"] = true;
      DynamicJsonDocument temp(MAXHEAP /2);
      error = deserializeJson(temp, DeviceFile, DeserializationOption::Filter(filter));
      root = temp["hours"]["graph"];    

    }else if (time == "day")
    {
      
      StaticJsonDocument<32> filter;
      filter["days"]["graph"] = true;
      DynamicJsonDocument temp(MAXHEAP /2);
      error = deserializeJson(temp, DeviceFile, DeserializationOption::Filter(filter));
      root = temp["days"]["graph"]; 
    }else if (time == "month")
    {
      StaticJsonDocument<32> filter;
      filter["months"]["graph"] = true;
      DynamicJsonDocument temp(MAXHEAP /2);
      error = deserializeJson(temp, DeviceFile, DeserializationOption::Filter(filter));
      root = temp["months"]["graph"]; 
      
    }else if (time == "year")
    {
      StaticJsonDocument<32> filter;
      filter["years"]["graph"] = true;
      DynamicJsonDocument temp(MAXHEAP /2);
      error = deserializeJson(temp, DeviceFile, DeserializationOption::Filter(filter));
      root = temp["years"]["graph"]; 
    }

    DeviceFile.close();
    int cntsection;
    int arrayLength = sizeof(section) / sizeof(section[0]);
    result = F("[");

    if (time == "hour")
    {
      int now = Hour.toInt();
      int i = 0;
      while (i < 24)
      {
        if (i > 0)
        {
          sep = ",";
        }
        now++;
        if (now > 23)
        {
          now = 0;
        }
        String tmpi = now < 10 ? "0" + String(now) : String(now);
        result += sep + F("{\"y\":\"") + tmpi + F("H\"");
        int j = 0;
        String sep2 = "";
        
        for (cntsection=0 ; cntsection <arrayLength; cntsection++)
        {
          if (root[tmpi][section[cntsection]].as<int>() !=0)
          { 
            if (j > 0)
            {
              sep2 = ",";
            }else{
              result += ",";
            }
            result += sep2 + "\"" + String(section[cntsection]) + "\":" + root[tmpi][section[cntsection]].as<String>();
            j++;
          }
        }
        result += F("}");
        i++;
      }
    }else if (time == "day")
    {
      int now = Day.toInt();
      int reste = 30 - now;
      int lastNbDayMonth;
      String  m;
      if (Month.toInt()-2>0)
      {
        lastNbDayMonth=maxDayOfTheMonth[(Month.toInt()-2)];
      }else{
        lastNbDayMonth=31;
      }
      if (reste>0)
      {
        now = (lastNbDayMonth - reste)+1;   
      }else if (reste<0){
        now=2;
      }else if (reste==0){
        now=reste+1;
      }

      int i = 0;
      while (i < 30)
      {
        if (i > 0)
        {
          sep = ",";
        }

        if (reste>0)
        {
          if (now > lastNbDayMonth)
          {
            now = 1;
          }
        }
        String tmpi = now < 10 ? "0" + String(now) : String(now);
        String tmpm;
        if (i>now)
        {
          tmpm = Month;
        }else{
          if (reste<=0)
          {
            tmpm = Month;
          }else{
            if ((Month.toInt()-1)>0)
            {
              tmpm = (Month.toInt()-1) < 10 ? "0" + String((Month.toInt()-1)) : String((Month.toInt()-1));
            }else{
              tmpm = "12";
            }
          }
        }
        //String tmpm = m.toInt() < 10 ? "0" + String(m) : String(m);
        
        result += sep + F("{\"y\":\"") + tmpi + F("/")+ tmpm +F("\"");
        int j = 0;
        String sep2 = "";
        for (cntsection=0 ; cntsection <arrayLength; cntsection++)
        {
          if (root[tmpi][section[cntsection]].as<int>() !=0)
          {
            if (j > 0)
            {
              sep2 = ",";
            }else{
              result += ",";
            }
            result += sep2 + "\"" + String(section[cntsection]) + "\":" + root[tmpi][section[cntsection]].as<String>();
            j++;
          }
        }
        result += F("}");
        now++;
        i++;
      }
    }else if (time == "month")
    {
      int now = Month.toInt();
      String  y;
      int i = 0;
      while (i < 12)
      {
        if (i > 0)
        {
          sep = ",";
        }
        
        now++;
        if (now > 12)
        {
          now = 1;
        }
        String tmpi = now < 10 ? "0" + String(now) : String(now);
        if (i<now)
        {
          y = String((Year.toInt()-1));
        }else{
          y = Year;
        }
        result += sep + F("{\"y\":\"") + tmpi + F("/")+ y +F("\"");
        int j = 0;
        String sep2 = "";
        for (cntsection=0 ; cntsection <arrayLength; cntsection++)
        {
          if (root[tmpi][section[cntsection]].as<int>() !=0)
          {
            if (j > 0)
            {
              sep2 = ",";
            }else{
              result += ",";
            }
            result += sep2 + "\"" + String(section[cntsection]) + "\":" + root[tmpi][section[cntsection]].as<String>();
            j++;
          }
        }
        result += F("}");
        i++;
      }
    }else if (time == "year")
    {
      int now = Year.toInt() - 10;
      int i = 0;
      while (i < 11)
      {
        if (i > 0)
        {
          sep = ",";
        }

        result += sep + F("{\"y\":\"") + String(now) + F("\"");
        int j = 0;
        String sep2 = "";
        for (cntsection=0 ; cntsection <arrayLength; cntsection++)
        {
          if (root[String(now)][section[cntsection]].as<int>() !=0)
          {
            if (j > 0)
            {
              sep2 = ",";
            }else{
              result += ",";
            }
            result += sep2 + "\"" + String(section[cntsection]) + "\":" + root[String(now)][section[cntsection]].as<String>();
            j++;
          }
        }
        result += F("}");
        now++;
        i++;
      }
    }
    result += F("]");
  }

  request->send(200, F("application/json"), result);
}


void handleLoadTrendEnergyEuros(AsyncWebServerRequest *request)
{

  String IEEE, result;
  int i = 0;
  IEEE = request->arg(i);

  result = getTrendEnergyEuros(IEEE);
  // result = createEnergyGraph(IEEE);

  request->send(200, F("application/json"), result);
}

void handleLoadLabelEnergy(AsyncWebServerRequest *request)
{

  String IEEE, datas, result;
  int i = 0;
  IEEE = request->arg(i);
  datas = request->arg(1);

  result = datas;

  request->send(200, F("text/html"), result);
}

void handleGetAlert(AsyncWebServerRequest *request)
{

  String result = "";

  if (!alertList->isEmpty())
  {
    Alert a = alertList->shift();
    result = String(a.state);
    result += F(";");
    result += a.message;
    DEBUG_PRINTLN(result);
  }

  request->send(200, F("text/html"), result);
}

void handleGetRuleStatus(AsyncWebServerRequest *request)
{

  int i = 0;
  String name = request->arg(i);
  int value = getStatusRule(name.c_str());
  String lastDate = getLastDateRule(name.c_str());
  String result = String(value)+"|"+lastDate;

  request->send(200, F("text/html"), result);
}

void handleGetDeviceValue(AsyncWebServerRequest *request)
{

  String result = "";
  if (!deviceList->isEmpty())
  {
    int i=0;
    result="[";
    while (!deviceList->isEmpty())
    {
      Device d = deviceList->shift();
      if (i>0){result+=",";}
      result += "\""+String(d.shortAddr)+"_"+String(d.cluster)+"_"+String(d.attribute);
      result += F(";");
      result += d.value+"\"";
      i++;
    }
    result +="]";
    
  }
  request->send(200, F("text/html"), result);
}

void handleSendMqttDiscover(AsyncWebServerRequest *request)
{
  String IEEE,ShortAddr, datas, result;
  int i = 0;
  ShortAddr = request->arg(i);
  IEEE = GetMacAdrr(ShortAddr.toInt());
  IEEE = IEEE.substring(0, 16);
  String model;
  model = GetModel(IEEE+".json");

  File DeviceFile = LittleFS.open("/db/"+IEEE+".json" ,"r");

  if (!DeviceFile || DeviceFile.isDirectory())
  {
    DEBUG_PRINTLN(F("failed open"));
  }
  else
  {
    int DeviceId = GetDeviceId(IEEE+".json");
    if (TemplateExist(DeviceId))
    {
      Template *t;
      t = GetTemplate(DeviceId, model);
      for (int i = 0; i < t->StateSize; i++)
      {
        if (strlen(t->e[i].mqtt_icon)>0)
        {
          const char* PROGMEM HA_discovery_msg = "{"
              "\"name\":\"{{name_prop}}\","
              "\"unique_id\":\"{{unique_id}}\","
              "\"device_class\":{{device_class}},"
              "\"state_class\":{{state_class}},"
              "{{unit}}"
              "\"icon\":\"mdi:{{mqtt_icon}}\","
              "\"state_topic\":\"{{state_topic}}/state\","
              "\"value_template\":\"{{value}}\","
              "\"device\": {"
                  "\"name\":\"LiXee-GW_{{device_name}}\","
                  "\"sw_version\":\"2.0\","
                  "\"model\":\"HW V2\","
                  "\"manufacturer\":\"LiXee\","
                  "\"identifiers\":[\"LiXee-GW{{device_name}}\"]"
              "}"
          "}";

          datas = FPSTR(HA_discovery_msg);
          
          datas.replace("{{name_prop}}", t->e[i].name);
          datas.replace("{{unique_id}}", IEEE+"_"+String(t->e[i].cluster)+"_"+String(t->e[i].attribute));
          if (memcmp(t->e[i].mqtt_device_class,"null",4)==0)
          {
            datas.replace("{{device_class}}", t->e[i].mqtt_device_class);
          }else{
            String tmp="\""+String(t->e[i].mqtt_device_class)+"\"";
            datas.replace("{{device_class}}", tmp); 
          }
          if (memcmp(t->e[i].mqtt_state_class,"null",4)==0)
          {
            datas.replace("{{state_class}}", t->e[i].mqtt_state_class);
          }else{
            String tmp="\""+String(t->e[i].mqtt_state_class)+"\"";
            datas.replace("{{state_class}}", tmp); 
          }
          datas.replace("{{mqtt_icon}}", t->e[i].mqtt_icon);
          if (strlen(t->e[i].unit)>0)
          {
            String tmp = "\"unit_of_measurement\":\""+String(t->e[i].unit)+"\",";
            datas.replace("{{unit}}", tmp);
          }else{
            datas.replace("{{unit}}", "");
          }
          
          datas.replace("{{state_topic}}", ConfigGeneral.headerMQTT+ IEEE+"_"+String(t->e[i].cluster)+"_"+String(t->e[i].attribute));
          if ((String(t->e[i].type)=="numeric") || (String(t->e[i].type)=="float"))
          {
            if (t->e[i].coefficient!=1)
            {
              datas.replace("{{value}}", "{{value_json.value_"+String(t->e[i].cluster)+"_"+String(t->e[i].attribute)+" | float * "+String(t->e[i].coefficient)+"}}");
            }else{
              datas.replace("{{value}}", "{{value_json.value_"+String(t->e[i].cluster)+"_"+String(t->e[i].attribute)+"}}");
            }
          }else{
            datas.replace("{{value}}", "{{value_json.value_"+String(t->e[i].cluster)+"_"+String(t->e[i].attribute)+"}}");
          }
          datas.replace("{{device_name}}", model+"_"+IEEE);
          String topic = ConfigGeneral.headerMQTT+ IEEE+"_"+String(t->e[i].cluster)+"_"+String(t->e[i].attribute)+"/config";
          if (model=="ZLinky_TIC")
          {
            const char *tmp;
            bool discoverOk = false;
            tmp = t->e[i].mode;
            if ((tmp != NULL) && (tmp[0] != '\0')) 
            {
              char * pch;
              pch = strtok ((char*)tmp,";");
              while (pch != NULL)
              {
                if (atoi(pch) == ConfigGeneral.LinkyMode)
                {
                  discoverOk=true;
                  break;
                }
                pch = strtok (NULL, " ;");
              }
            }else{
              discoverOk=true;
            }

            if (discoverOk)
            {
              mqttClient.publish(topic.c_str(),1,true,datas.c_str());
            }
          }else{
            mqttClient.publish(topic.c_str(),1,true,datas.c_str());
          }
          
        }
      }
      // toutes les actions
    }
  }
  DeviceFile.close();

  result="";

  request->send(200, F("text/html"), result);
}

void handleDeleteDevice(AsyncWebServerRequest *request)
{

  String tmpMac;
  int i = 0;
  tmpMac = request->arg(i);
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
  SendDeleteDevice(macInt);

  // on efface le fichier json
  String filename = "/db/" + tmpMac + ".json";
  int res;
  res = LittleFS.remove(filename);

  String filenamebk = "/bk/" + tmpMac + ".json";
  int resbk;
  resbk = LittleFS.remove(filenamebk);

  if ((res == 0) && (resbk == 0))
  {
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/configDevices"));
    request->send(response);
  }
  else
  {
    request->send(200, F("text/html"), "Error");
  }
}

void handleGetFormattedDate(AsyncWebServerRequest *request)
{

  String result;
  result = FormattedDate;
  request->send(200, F("text/html"), result);
}

void APIgetConfig(AsyncWebServerRequest *request)
{
  String result;


  request->send(200, F("application/json"), result);
}

void APIgetSystem(AsyncWebServerRequest *request)
{
  const char JSON_GET_SYSTEM[] PROGMEM = 
  "{"
  "    \"network\" :"
   "    {"
   "     \"wifi\":"
   "     {"
   "       \"enable\" : {{wifienable}},"
   "       \"connected\" : {{wificonnected}},"
   "       \"mode\" : {{wifimode}},"
   "       \"ip\" : \"{{wifiip}}\","
   "       \"netmask\" : \"{{wifimask}}\","
   "       \"gateway\" : \"{{wifigateway}}\""
   "     }"
  "    },"
  "    \"system\" :"
  "    {"
  "      \"mqtt\" : "
  "      {"
  "        \"enable\" : {{mqttenable}},"
  "        \"connected\" : {{mqttconected}},"
   "       \"url\" : \"{{mqtturl}}\","
  "        \"port\" : {{mqttport}}"
  "      },"
  "      \"webpush\" :"
  "      {"
  "        \"enable\" : {{webpushenable}},"
  "        \"auth\" : {{webpushauth}},"
  "        \"url\" : \"{{webpushurl}}\""
  "      },"
  "      \"marstek\" :"
  "      {"
  "        \"enable\" : {{marstekenable}},"
  "        \"connected\" :{{marstekconnected}},"
  "        \"ip\" : \"{{marstekip}}\""
  "      },"
  "      \"infos\" :"
  "      {"
  "        \"t\" : {{Temperature}}"
  "      }"
  "    }"
  "  }";

  String result;

  result = FPSTR(JSON_GET_SYSTEM);

  result.replace("{{wifienable}}",String(ConfigSettings.enableWiFi));
  result.replace("{{wificonnected}}",String(ConfigSettings.connectedWifiSta));
  result.replace("{{wifimode}}",String(ConfigSettings.dhcp));
  result.replace("{{wifiip}}",ConfigSettings.ipAddressWiFi);
  result.replace("{{wifimask}}",ConfigSettings.ipMaskWiFi);
  result.replace("{{wifigateway}}",ConfigSettings.ipGWWiFi);
  
  result.replace("{{mqttenable}}",String(ConfigSettings.enableMqtt));
  result.replace("{{mqttconected}}",String(mqttClient.connected()));
  result.replace("{{mqtturl}}",ConfigGeneral.servMQTT);
  result.replace("{{mqttport}}",String(ConfigGeneral.portMQTT));

  result.replace("{{webpushenable}}",String(ConfigSettings.enableWebPush));
  result.replace("{{webpushauth}}",String(ConfigGeneral.webPushAuth));
  result.replace("{{webpushurl}}",ConfigGeneral.servWebPush);

  result.replace("{{marstekenable}}",String(ConfigSettings.enableMarstek));
  result.replace("{{marstekconnected}}",String(ConfigGeneral.connectedMarstek));
  result.replace("{{marstekip}}",ConfigGeneral.marstekIP);

  float temperature = 0;
  temperature = temperatureReadFixed();
  result.replace("{{Temperature}}", String(temperature));

  request->send(200, F("application/json"), result);
}

void APIgetDevices(AsyncWebServerRequest *request)
{
  String result;

  File root = LittleFS.open("/db");
  File filedevice = root.openNextFile();
  result = "{";
  int i = 0;
  while (filedevice)
  {

    String inifile = filedevice.name();

    File file = LittleFS.open("/db/" + inifile, FILE_READ);
    if (!file || file.isDirectory())
    {
      DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier ini_read "));
      DEBUG_PRINTLN(inifile);
      file.close();
      xSemaphoreGive(file_Mutex);
    }
    size_t filesize = file.size();
    if (inifile.substring(3,4)!="_")
    {
      if (filesize > 0)
      {
        if (i > 0)
        {
          result += ",";
        }
        result += "\""+inifile.substring(0,16)+"\" : ";
        while (file.available())
        {
          result += (char)file.read();
        }
        i++;
      }
    }
    file.close();
    filedevice.close();
    vTaskDelay(1);
    filedevice = root.openNextFile();
  }
  result += "}";
  filedevice.close();
  root.close();

  request->send(200, F("application/json"), result);
}

void APIgetDevice(AsyncWebServerRequest *request)
{
  String IEEE;
  int args = request->args();
  String result;
  if ((args >0) && (request->hasArg("IEEE")) )
  {
    IEEE = request->arg("IEEE");
  
    result = "{";
    String inifile = IEEE+".json";
    File file = LittleFS.open("/db/" + inifile, FILE_READ);
    if (!file || file.isDirectory())
    {
      result = "Échec de l'ouverture du fichier : "+ inifile;
      request->send(500, "text/plain", result);
      file.close();
    }
    size_t filesize = file.size();
    if (filesize > 0)
    {
      result += "\""+inifile.substring(0,16)+"\" : ";
      while (file.available())
      {
        result += (char)file.read();
      }
    }  
    file.close();
    result += "}";       
  }else{
    result="{}";
  }

  request->send(200, F("application/json"), result);
}

void APIgetLinky(AsyncWebServerRequest *request)
{
  String IEEE, result;
  IEEE = String(ConfigGeneral.ZLinky);
  if (IEEE.length()>0)
  {
    String model;
    model = GetModel(IEEE+".json");
    int DeviceId = GetDeviceId(IEEE+".json");
    result ="{";
    if (TemplateExist(DeviceId))
    {
      Template *t;
      t = GetTemplate(DeviceId, model);
      for (int i = 0; i < t->StateSize; i++)
      {      
        const char *tmp;
        bool discoverOk = false;
        tmp = t->e[i].mode;
        if ((tmp != NULL) && (tmp[0] != '\0')) 
        {
          char * pch;
          pch = strtok ((char*)tmp,";");
          while (pch != NULL)
          {
            if (atoi(pch) == ConfigGeneral.LinkyMode)
            {
              discoverOk=true;
              break;
            }
            pch = strtok (NULL, " ;");
          }
        }else{
          discoverOk=true;
        }

        if (discoverOk)
        {
          if (i>0){result+=",";}
          result += "\"";
          result += (String)t->e[i].cluster+"_"+(String)t->e[i].attribute;
          result += "\" :";
          String inifile =IEEE+".json";
          if ((memcmp(t->e[i].type,"numeric",7)==0) || (memcmp(t->e[i].type,"float",5)==0) )
          {
            result +=  GetValueStatus(inifile, t->e[i].cluster, t->e[i].attribute, (String)t->e[i].type, t->e[i].coefficient);
          }else{
            result +="\"";
            result +=  GetValueStatus(inifile, t->e[i].cluster, t->e[i].attribute, (String)t->e[i].type, t->e[i].coefficient);
            result +="\"";
          }
          
        }

      }

    }
    result +="}";
  }else{
    result = "{}";
  }

  request->send(200, F("application/json"), result);
}

void APIgetEnergyDevice(AsyncWebServerRequest *request)
{
  String IEEE;
  int args = request->args();
  String result;
  if ((args >0) && (request->hasArg("IEEE")) )
  {
    IEEE = request->arg("IEEE");
    result = "{";
    String inifile = "nrg_"+IEEE+".json";
    File file = LittleFS.open("/db/" + inifile, FILE_READ);
    if (!file || file.isDirectory())
    {
      result = "Échec de l'ouverture du fichier : "+ inifile;
      request->send(500, "text/plain", result);
      file.close();
    }
    size_t filesize = file.size();
    if (filesize > 0)
    {
      result += "\""+inifile.substring(4,20)+"\" : ";
      while (file.available())
      {
        result += (char)file.read();
      }
    }  
    file.close();
    result += "}";    
  }else{
    result="{}";
  }

  request->send(200, F("application/json"), result);
}

void APIgetPowerDevice(AsyncWebServerRequest *request)
{
  String IEEE;
  int args = request->args();
  String result;
  if ((args >0) && (request->hasArg("IEEE")) )
  {
    IEEE = request->arg("IEEE");
  
    result = "{";
    String inifile = "pwr_"+IEEE+".json";
    File file = LittleFS.open("/db/" + inifile, FILE_READ);
    if (!file || file.isDirectory())
    {
      result = "Échec de l'ouverture du fichier : "+ inifile;
      request->send(500, "text/plain", result);
      file.close();
    }
    size_t filesize = file.size();
    if (filesize > 0)
    {
      result += "\""+inifile.substring(4,20)+"\" : ";
      while (file.available())
      {
        result += (char)file.read();
      }
    }  
    file.close();
    result += "}";
    
    
  }else{
    result="{}";
  }

  request->send(200, F("application/json"), result);
}

void APIgetTemplates(AsyncWebServerRequest *request)
{
  
  String result;

  File root = LittleFS.open("/tp");
  File filedevice = root.openNextFile();
  result = "{";
  int i = 0;
  while (filedevice)
  {

    String inifile = filedevice.name();
    File file = LittleFS.open("/tp/" + inifile, FILE_READ);
    if (!file || file.isDirectory())
    {
      DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier ini_read "));
      DEBUG_PRINTLN(inifile);
      file.close();
    }
    size_t filesize = file.size();

    if (filesize > 0)
    {
      if (i > 0)
      {
        result += ",";
      }
      result += "\""+inifile+"\" : ";
      while (file.available())
      {
        result += (char)file.read();
      }
      i++;
    }
    
    file.close();
    filedevice.close();
    vTaskDelay(1);
    filedevice = root.openNextFile();
  }
  result += "}";
  filedevice.close();
  root.close();

  request->send(200, F("application/json"), result);
}

void initWebServer()
{

  // serverWeb.on("/", handleRoot);
  serverWeb.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleRoot(request); 
  });
  serverWeb.on("/dashboard", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    //request->client()->setRxTimeout(15);
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleDashboard(request);
  });
  serverWeb.on("/statusEnergy", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleStatusEnergy(request); 
  });
  serverWeb.on("/statusNetwork", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleStatusNetwork(request);
  });
  serverWeb.on("/statusDevices", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleStatusDevices(request); 
  });
  serverWeb.on("/configGeneral", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigGeneral(request); 
  });
  serverWeb.on("/configZigbee", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigZigbee(request); 
  });
  serverWeb.on("/configHorloge", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigHorloge(request); 
  });
  serverWeb.on("/configLinky", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigLinky(request); 
  });
  serverWeb.on("/configGaz", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigGaz(request); 
  });
  serverWeb.on("/configWater", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigWater(request); 
  });
  serverWeb.on("/getMQTTStatus", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleGetMQTTStatus(request); 
  });
  serverWeb.on("/configMQTT", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigMQTT(request); 
  });
  serverWeb.on("/configHTTP", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigHTTP(request); 
  });
  serverWeb.on("/configRules", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigRules(request); 
  });
  serverWeb.on("/configWebPush", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigWebPush(request); 
  });
  serverWeb.on("/configMarstek", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigMarstek(request); 
  });
  
  serverWeb.on("/configUdpClient", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigUdpClient(request); 
  });
  serverWeb.on("/configNotif", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigNotification(request); 
  });
  serverWeb.on("/configWiFi", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigWifi(request); 
  });
  
  serverWeb.on("/configDevices", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigDevices(request); 
  });
  serverWeb.on("/update", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleToolUpdate(request); 
  });
  serverWeb.on("/backup", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleToolBackup(request); 
  });
  serverWeb.on("/createBackupFile", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleToolCreateBackup(request); 
  });
  serverWeb.on("/saveDebug", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveDebug(request);  
  });
  serverWeb.on("/saveFileConfig", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveConfig(request);  
  });
  serverWeb.on("/saveFileDevice", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveDevice(request); 
  });
  serverWeb.on("/saveFileTemplates", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveTemplates(request); 
  });
  serverWeb.on("/saveFileRules", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveRules(request); 
  });
  
  serverWeb.on("/saveFileJavascript", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveJavascript(request); 
  });
  serverWeb.on("/saveFileDatabase", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveDatabase(request); 
  });
  serverWeb.on("/saveConfigGeneral", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveConfigGeneral(request); 
  });
  serverWeb.on("/saveConfigHorloge", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveConfigHorloge(request); 
  });
  serverWeb.on("/saveConfigLinky", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveConfigLinky(request); 
  });
  serverWeb.on("/saveConfigGaz", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveConfigGaz(request); 
  });
  serverWeb.on("/saveConfigWater", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveConfigWater(request); 
  });
  serverWeb.on("/saveConfigMQTT", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveConfigMQTT(request); 
  });
  serverWeb.on("/saveConfigHTTP", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveConfigHTTP(request); 
  });
  serverWeb.on("/saveConfigWebPush", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveConfigWebPush(request); 
  });

  serverWeb.on("/saveConfigMarstek", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveConfigMarstek(request); 
  });
  serverWeb.on("/saveConfigUDPClient", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveConfigUDPClient(request); 
  });

  serverWeb.on("/saveConfigNotification", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveConfigNotification(request); 
  });
  serverWeb.on("/saveWifi", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveWifi(request); 
  });
  serverWeb.on("/tools", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleTools(request); 
  });
  serverWeb.on("/logs", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleLogs(request); 
  });
  serverWeb.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleReboot(request); 
  });
  serverWeb.on("/update", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleToolUpdate(request); 
  });
  serverWeb.on("/doUpdate", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data,
                  size_t len, bool final) 
        {
          if (ConfigSettings.enableSecureHttp)
          {
            if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
              return request->requestAuthentication();
          }
          handleDoUpdate(request, filename, index, data, len, final);
        }
  );
  serverWeb.on("/doRestore", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data,
                  size_t len, bool final) 
        {
          if (ConfigSettings.enableSecureHttp)
          {
            if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
              return request->requestAuthentication();
          }
          handleDoRestore(request, filename, index, data, len, final);
        }
  );
  serverWeb.on("/readFile", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleReadfile(request); 
  });
  serverWeb.on("/getLogBuffer", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleLogBuffer(request); 
  });
  serverWeb.on("/scanNetwork", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleScanNetwork(request); 
  });
  serverWeb.on("/cmdClearConsole", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleClearConsole(request); 
  });
  serverWeb.on("/cmdGetVersion", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleGetVersion(request); 
  });

  serverWeb.on("/cmdErasePDM", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleErasePDM(request); 
  });
  serverWeb.on("/cmdStartNwk", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleStartNwk(request); 
  });
  serverWeb.on("/cmdSetChannelMask", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSetChannelMask(request); 
  });
  serverWeb.on("/cmdPermitJoin", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handlePermitJoin(request); 
  });
  serverWeb.on("/cmdRawMode", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleRawMode(request); 
  });
  serverWeb.on("/cmdRawModeOff", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleRawModeOff(request); 
  });
  serverWeb.on("/cmdActiveReq", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleActiveReq(request); 
  });
  serverWeb.on("/cmdReset", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleReset(request); 
  });
  serverWeb.on("/configFiles", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigFiles(request); 
  });
  serverWeb.on("/debugFiles", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleDebugFiles(request); 
  });
  serverWeb.on("/fsbrowser", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleFSbrowser(request); 
  });
  serverWeb.on("/fsbrowserBackup", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleFSbrowserBackup(request); 
  });
  serverWeb.on("/tp", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleTemplates(request); 
  });
   serverWeb.on("/rules", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleRules(request); 
  });
  serverWeb.on("/javascript", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleJavascript(request); 
  });
  serverWeb.on("/createDevice", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleCreateDevice(request); 
  });
  serverWeb.on("/createTemplate", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleCreateTemplate(request); 
  });
  serverWeb.on("/ZigbeeAction", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleZigbeeAction(request); 
  });
  serverWeb.on("/ZigbeeSendRequest", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleZigbeeSendRequest(request); 
  });
  serverWeb.on("/loadLinkyDatas", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleLoadLinkyDatas(request); 
  });
  serverWeb.on("/loadGaugeDashboard", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleLoadGaugeDashboard(request); 
  });
  serverWeb.on("/loadPowerGaugeAbo", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleLoadPowerGaugeAbo(request); 
  });
  serverWeb.on("/loadPowerGaugeTimeDay", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleLoadPowerGaugeTimeDay(request); 
  });
  serverWeb.on("/refreshGaugeAbo", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleRefreshGaugeAbo(request); 
  });
  serverWeb.on("/refreshLabel", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleRefreshLabel(request);
  });             
  serverWeb.on("/loadPowerTrend", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleLoadPowerTrend(request); 
  });
  serverWeb.on("/loadPowerChart", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleLoadPowerChart(request); 
  });
  serverWeb.on("/loadEnergyChart", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleLoadEnergyChart(request); 
  });
  serverWeb.on("/loadTrendEnergyEuros", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleLoadTrendEnergyEuros(request); 
  });
  serverWeb.on("/loadLabelEnergy", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleLoadLabelEnergy(request); 
  });
  serverWeb.on("/deleteDevice", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleDeleteDevice(request); 
  });
  serverWeb.on("/getDeviceValue", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleGetDeviceValue(request); 
  });
  serverWeb.on("/getRuleStatus", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleGetRuleStatus(request); 
  });
  serverWeb.on("/getAlert", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleGetAlert(request); 
  });
  serverWeb.on("/getFormattedDate", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleGetFormattedDate(request); 
  });
  serverWeb.on("/sendMqttDiscover", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSendMqttDiscover(request); 
  });
  serverWeb.on("/help", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleHelp(request); 
  });

  serverWeb.on("/shelly", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleShelly(request); 
  });


  serverWeb.on("/getSystem", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    APIgetSystem(request); 
    
  });
  serverWeb.on("/getConfig", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    APIgetConfig(request); 
    
  });

  serverWeb.on("/getDevices", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    APIgetDevices(request); 
    
  });
  serverWeb.on("/getDevice", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    APIgetDevice(request); 
    
  });
  serverWeb.on("/getEnergyDevice", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    APIgetEnergyDevice(request); 
    
  });
  serverWeb.on("/getPowerDevice", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    APIgetPowerDevice(request); 
    
  });
  
  serverWeb.on("/getLinky", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    APIgetLinky(request); 
    
  });

  serverWeb.on("/getTemplates", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    APIgetTemplates(request); 
    
  });

  serverWeb.serveStatic("/web/js/jquery-min.js", LittleFS, "/web/js/jquery-min.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/functions.js", LittleFS, "/web/js/functions.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/raphael-min.js", LittleFS, "/web/js/raphael-min.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/morris.min.js", LittleFS, "/web/js/morris.min.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/justgage.min.js", LittleFS, "/web/js/justgage.min.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/bootstrap.min.js", LittleFS, "/web/js/bootstrap.min.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/bootstrap.bundle.min.js.map", LittleFS, "/web/js/bootstrap.map").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/css/bootstrap.min.css", LittleFS, "/web/css/bootstrap.min.css").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/css/style.css", LittleFS, "/web/css/style.css").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/img/logo.png", LittleFS, "/web/img/logo.png").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/img/wait.gif", LittleFS, "/web/img/wait.gif").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/img/", LittleFS, "/web/img/").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/backup.tar", LittleFS, "/bk/backup.tar");
  serverWeb.onNotFound(handleNotFound);

  serverWeb.begin();

  Update.onProgress(printProgress);
}
