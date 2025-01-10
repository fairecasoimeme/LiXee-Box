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
#include <ETH.h>
#include "WiFi.h"

// #include <WebServer.h>
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

int maxDayOfTheMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
String section[12] = { "0", "1", "256", "258" , "260", "262", "264" ,"266", "268", "270", "272", "274"};

extern String Hour;
extern String Day;
extern String Month;
extern String Year;
extern String FormattedDate;

HTTPClient clientWeb;

AsyncWebServer serverWeb(80);

#define UPD_FILE "https://github.com/fairecasoimeme/lixee-box/releases/latest/download/lixee-box.bin"

const char HTTP_HELP[] PROGMEM = 
 "<h1>Help !</h1>"
    "<h3>Version : {{version}}</h3>"
    "<h3>Shop & description</h3>"
    "You can go to this url :</br>"
    "<a href=\"https://lixee.fr/\" target='_blank'>Shop </a></br>"

    "<h3>Firmware Source & Issues</h3>"
    "Please go here :</br>"
    "<a href=\"https://github.com/fairecasoimeme/LiXee-Box\" target='_blank'>Sources</a>"
    
    
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
    "<nav class='navbar navbar-expand-lg navbar-light bg-light rounded'><div class='container-fluid'><a class='navbar-brand' href='/statusNetwork'>"
    "<div style='display:block-inline;float:left;'><img src='web/img/logo.png'> </div>"
    "<div style='float:left;display:block-inline;font-weight:bold;padding:18px 10px 10px 10px;'> WEBAPI-MQTT Gateway</div>"
    //"<div id='FormattedDate' style='display:block;padding-top:58px;font-size:12px;'>%FormattedDate%</div>"
    "</a>"
    "<button class='navbar-toggler' type='button' data-bs-toggle='collapse' data-bs-target='#navbarNavDropdown' aria-controls='navbarNavDropdown' aria-expanded='false' aria-label='Toggle navigation'>"
    "<span class='navbar-toggler-icon'></span>"
    "</button>"
    "<div id='navbarNavDropdown' class='collapse navbar-collapse justify-content-md-center'>"
    "<ul class='navbar-nav me-auto mb-2 mb-lg-0'>"
    //"<li class='nav-item'>"
    //"<a class='nav-link' href='/'>Dashboard</a>"
    //"</li>"
    "<li class='nav-item dropdown'>"
    "<a class='nav-link dropdown-toggle' href='#' id='navbarDropdown' role='button' data-bs-toggle='dropdown'>Status</a>"
    "<div class='dropdown-menu'>"
    //"<a class='dropdown-item' href='statusEnergy'>Energy</a>"
    "<a class='dropdown-item' href='statusNetwork'>Network</a>"
    "<a class='dropdown-item' href='statusDevices'>Devices</a>"
    "</div>"
    "</li>"
    "<li class='nav-item dropdown'>"
    "<a class='nav-link dropdown-toggle' href='#' id='navbarDropdown' role='button' data-bs-toggle='dropdown'>Config</a>"
    "<div class='dropdown-menu'>"
    "<a class='dropdown-item' href='configGeneral'>Gateway</a>"
    "<a class='dropdown-item' href='configZigbee'>Zigbee</a>"
    "<a class='dropdown-item' href='configWiFi'>WiFi</a>"
    //"<a class='dropdown-item' href='configEthernet'>Ethernet</a>"
    "<a class='dropdown-item' href='configDevices'>Devices</a>"
    "</div>"
    "</li>"
    "<li class='nav-item'>"
    "<a class='nav-link' href='/tools'>Tools</a>"
    "</li>"
    "<li class='nav-item'>"
    "<a class='nav-link' href='/help'>Help</a>"
    "</li>"
    "</ul></div></div>"
    "</nav>"
    "<div id='alert' style='display:none;' class='alert alert-success' role='alert'>"
    "</div>";

// "<a href='/configFiles' class='btn btn-primary mb-2'>Config Files</a>"
const char HTTP_TOOLS[] PROGMEM =
    "<h1>Tools</h1>"
    "<div class='btn-group-vertical'>"
    //"<a href='/logs' class='btn btn-primary mb-2'>Console</a>"
    "<a href='/debugFiles' class='btn btn-primary mb-2'>Debug Files</a>"
    //"<a href='/fsbrowser' class='btn btn-primary mb-2'>Device Files</a>"
    "<a href='/tp' class='btn btn-primary mb-2'>Templates</a>"
    //"<a href='/javascript' class='btn btn-primary mb-2'>Javascript</a>"
    "<a href='/update' class='btn btn-primary mb-2'>Update</a>"
    "<a href='/backup' class='btn btn-primary mb-2'>Backup</a>"
    "<a href='/reboot' class='btn btn-primary mb-2'>Reboot</a>"
    "</div>";

const char HTTP_BACKUP[] PROGMEM =
    "<h1>Backup datas</h1>"
    "<a href='#' class='btn btn-primary mb-2' onClick='createBackupFile()'>Create Backup</a>"
    "<div id='createBackupFile'>"
    "</div>"
    "<h1>Restore datas</h1>"
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
    "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
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
    "<h1>Update firmware</h1>"
    "<div id='update_info'>"
    "<h4>Latest version on GitHub</h4>"
    "<div id='onlineupdate'>"
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
    "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
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

const char HTTP_CONFIG_MENU[] PROGMEM =
    //"<a href='/configGeneral' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_general}}' >General</a>&nbsp"
    //"<a href='/configHorloge' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_horloge}}' >Horloge</a>&nbsp"
    //"<a href='/configLinky' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_linky}}' >Linky</a>&nbsp"
    //"<a href='/configGaz' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_gaz}}' >Gaz</a>&nbsp"
    //"<a href='/configWater' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_water}}' >Water</a>&nbsp"
    "<a href='/configHTTP' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_http}}' >HTTP</a>&nbsp"
    "<a href='/configMQTT' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_mqtt}}' >MQTT</a>&nbsp"
    "<a href='/configWebPush' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_webpush}}' >WebPush</a>&nbsp"
    //"<a href='/configModbus' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_modbus}}' >Modbus</a>&nbsp"
    //"<a href='/configNotif' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_notif}}' >Notif</a>&nbsp"
    ;
const char HTTP_CONFIG_GENERAL[] PROGMEM =
    "<h1>Config general</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigGeneral'>"
    "<h2>General</h2>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='debugSerial' type='checkbox' name='debugSerial' {{checkedDebug}}>"
    "<label class='form-check-label' for='debugSerial'>Debug on Serial</label>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form></div>"
    "</div>";

const char HTTP_CONFIG_ZIGBEE[] PROGMEM =
    "<h1>Config zigbee</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-6'>"
    "<h2>Parameters</h2>"
    "<span> @MAC coordinator : </span>{{macCoordinator}}<br>"
    "<span> Version coordinator : </span>{{versionCoordinator}}<br>"
    "<span> Network : </span>{{networkCoordinator}}<br>"
    "<label for='SetMaskChannel'>Set channel mask</label>"
    "<input class='form-control' id='SetMaskChannel' type='text' name='SetMaskChannel' value='{{SetMaskChannel}}'>"
    "<button type='button' onclick='cmd(\"SetChannelMask\",document.getElementById(\"SetMaskChannel\").value);' class='btn btn-primary'>Set Channel</button><br> "
    /*"<h2>Console</h2>"
    "<button type='button' onclick='cmd(\"ClearConsole\");document.getElementById(\"console\").value=\"\";' class='btn btn-primary'>Clear Console</button> "
    "<button type='button' onclick='cmd(\"GetVersion\");' class='btn btn-primary'>Get Version</button> "
    "<button type='button' onclick='cmd(\"ErasePDM\");' class='btn btn-primary'>Erase PDM</button> "
    "<button type='button' onclick='cmd(\"Reset\");' class='btn btn-primary'>Reset</button> "
    "<button type='button' onclick='cmd(\"StartNwk\");' class='btn btn-primary'>StartNwk</button> "
    "<button type='button' onclick='cmd(\"PermitJoin\");' class='btn btn-primary'>Permit Join</button> "
    "<br>Raw datas : <br><textarea id='console' rows='16' cols='100'></textarea>"*/
    "</div>"
    "</div>"
    /*"<script language='javascript'>"
    "$(document).ready(function() {"
    "logRefresh();});"
    "</script>";*/
    ;
const char HTTP_CONFIG_HORLOGE[] PROGMEM =
    "<h1>Config horloge</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigHorloge'>"
    "<div class='form-check'>"
    "<h2>NTP Server</h2>"
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

const char HTTP_CONFIG_LINKY[] PROGMEM =
    "<h1>Config Linky</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigLinky'>"
    "<div class='form-check'>"
    "<h2>Device</h2>"
    "{{selectDevices}}"
    "<h2>Tarifs</h2>"
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
    "<h1>Config Gaz</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigGaz'>"
    "<div class='form-check'>"
    "<h2>Device</h2>"
    "{{selectDevices}}"
    "<h2>Parameters</h2>"
    "<label for='coeffGaz'>Impulsion coefficient</label>"
    "<input class='form-control' id='coeffGaz' type='text' name='coeffGaz' value='{{coeffGaz}}'>"
    "<label for='unitGaz'>Unit</label>"
    "<input class='form-control' id='unitGaz' type='text' name='unitGaz' value='{{unitGaz}}'>"
    "<h2>Tarif</h2>"
    "<label for='tarifGaz'>Tarif (€)</label>"
    "<input class='form-control' id='tarifGaz' type='text' name='tarifGaz' value='{{tarifGaz}}'>"
    "<br>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form></div>"
    "</div>";

const char HTTP_CONFIG_WATER[] PROGMEM =
    "<h1>Config Water</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigWater'>"
    "<div class='form-check'>"
    "<h2>Device</h2>"
    "{{selectDevices}}"
    "<h2>Parameters</h2>"
    "<label for='coeffWater'>Impulsion coefficient</label>"
    "<input class='form-control' id='coeffWater' type='text' name='coeffWater' value='{{coeffWater}}'>"
    "<label for='unitWater'>Unit</label>"
    "<input class='form-control' id='unitWater' type='text' name='unitWater' value='{{unitWater}}'>"
    "<h2>Tarif</h2>"
    "<label for='tarifWater'>Tarif (€)</label>"
    "<input class='form-control' id='tarifWater' type='text' name='tarifWater' value='{{tarifWater}}'>"
    "<br>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form></div>"
    "</div>";

const char HTTP_CONFIG_MQTT[] PROGMEM =
    "<h1>Config MQTT</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigMQTT'>"
    "<div class='form-check'>"
    "<h2>MQTT client</h2>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='enableMqtt' type='checkbox' name='enableMqtt' {{checkedMqtt}}>"
    "<label class='form-check-label' for='enableMqtt'>Enable MQTT</label>"
    "</div>"
    "<label for='servMQTT'>MQTT server</label>"
    "<input class='form-control' id='servMQTT' type='text' name='servMQTT' value='{{servMQTT}}'>"
    "<label for='portMQTT'>MQTT port</label>"
    "<input class='form-control' id='portMQTT' type='text' name='portMQTT' value='{{portMQTT}}'>"
    "<label for='userMQTT'>MQTT username</label>"
    "<input class='form-control' id='userMQTT' type='text' name='userMQTT' value='{{userMQTT}}'>"
    "<label for='passMQTT'>MQTT password</label>"
    "<input class='form-control' id='passMQTT' type='password' name='passMQTT' value='{{passMQTT}}'>"
    "<label for='headerMQTT'>MQTT topic header</label>"
    "<input class='form-control' id='headerMQTT' type='text' name='headerMQTT' value='{{headerMQTT}}'>"
    "<br><Strong>Connected : </strong><span id='mqttStatus'><img src='web/img/wait.gif' /></span>"
    "<br>"
    "</div>"
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
    "<h1>Config HTTP</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigHTTP'>"
    "<div class='form-check'>"
    "<h2>HTTP security</h2>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='enableSecureHttp' type='checkbox' name='enableSecureHttp' {{checkedHttp}}>"
    "<label class='form-check-label' for='enableSecureHttp'>Enable HTTP Security</label>"
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
    "<h1>Config WebPush</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigWebPush'>"
    "<h2>General</h2>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='enableWebPush' type='checkbox' name='enableWebPush' {{checkedWebPush}}>"
    "<label class='form-check-label' for='enableWebPush'>Enable WebPush</label>"
    "</div>"
    "<h2>Web Push</h2>"
    "<label for='servWebPush'>Server HTTP</label>"
    "<input class='form-control' id='servWebPush' type='text' name='servWebPush' value='{{servWebPush}}' style='{{urlborder}}'>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='webPushAuth' type='checkbox' name='webPushAuth' {{checkedWebPushAuth}} onClick='toggleDiv(\"authWebPush\");'>"
    "<label class='form-check-label' for='webPushAuth'>Enable Authentification</label>"
    "</div>"
    "<div id='authWebPush' style='{{displayWebPushAuth}}'>"
    "<h3>Authentification</h3>"
    "<label for='userWebPush'>Username</label>"
    "<input class='form-control' id='userWebPush' type='text' name='userWebPush' value='{{userWebPush}}' style='{{userborder}}'>"
    "<label for='passWebPush'>password</label>"
    "<input class='form-control' id='passWebPush' type='password' name='passWebPush' value='{{passWebPush}}' style='{{passborder}}'>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form>"
    "<div style='color:red'>{{error}}</div>"
    
    
    "<h3>HTTP datas sent example</h3>"
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

const char HTTP_CONFIG_MODBUS[] PROGMEM =
    "<h1>Config Modbus RTU RS485</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigModbus'>"
    "<h2>General</h2>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='enableModbus' type='checkbox' name='enableModbus' {{checkedModbus}}>"
    "<label class='form-check-label' for='enableModbus'>Enable Modbus</label>"
    "</div>"
    "<h2>Modbus</h2>"
    
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form></div>"
    "</div>";

const char HTTP_CONFIG_NOTIFICATION[] PROGMEM =
    "<h1>Config Notification</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigNotification'>"
    "<h2>General</h2>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='enableNotif' type='checkbox' name='enableNotif' {{checkedNotif}}>"
    "<label class='form-check-label' for='enableNotif'>Enable Notification</label>"
    "</div>"
    "<h2>EMail</h2>"
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
    "<h1>Network status</h1>"
    "<div class='row' style='--bs-gutter-x: 0.3rem;'>"
    /*"<div class='col-sm-3'>"
    "<div class='card'>"
    "<div class='card-header'>Ethernet</div>"
    "<div class='card-body'>"
    "<div id='ethConfig'>"
    "<strong>Enable : </strong>{{enableEther}}"
    "<br><strong>Connected : </strong>{{connectedEther}}"
    "<br><strong>Mode : </strong>{{modeEther}}"
    "<br><strong>@IP : </strong>{{ipEther}}"
    "<br><strong>@Mask : </strong>{{maskEther}}"
    "<br><strong>@GW : </strong>{{GWEther}}"
    "</div>"
    "</div>"
    "</div>"
    "</div>"*/
    "<div class='col-sm-3'>"
    "<div class='card'>"
    "<div class='card-header'>Wifi</div>"
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
    "</div>"
    "<div class='row' style='--bs-gutter-x: 0.3rem;'>"
    "<div class='col-sm-3'><div class='card'><div class='card-header'>System Infos"
    "</div>"
    "<div class='card-body'>"
    "{{MQTT card}}"
    //"<i>System :</i><br><Strong>Box voltage :</strong> {{Voltage}} V<br>"
    "<Strong>Box temperature :</strong> {{Temperature}} °C<br>"
    "</div></div></div>"
    
    "</div>";

const char HTTP_ROOT[] PROGMEM =
    "<h1>Dashboard</h1>"
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
    "<div class='col-sm-6'>"
    "<div class='card' >"
    "<div class='card-header'>Energy gauge</div>"
    "<div class='card-body' style='min-height:272px;'>"
    "<div id='power_gauge_global' style='height:230px;'></div>"
    "</div>"
    "</div>"
    "</div>"
    "<div class='col-sm-6'>"
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
    "<div class='row' style='--bs-gutter-x: 0.3rem;'>"
    "<div class='col' style='padding:0'>"
    "<div class='card'>"
    "<div class='card-header'>Energy gauge</div>"
    "<div id='power_gauge_global' style='height:210px;'></div>"
    "</div>"
    "</div>"
    "<div class='col' style='padding:0'>"
    "<div class='card'>"
    "<div class='card-header'>Energy trend</div>"
    "<div id='power_trend' style='height:210px;'></div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "<div class='row'  style='--bs-gutter-x: 0.3rem;'>"
    "{{dashboard}}" 
    "</div>"
    "{{javascript}}";

const char HTTP_ENERGY[] PROGMEM =
    "<h1>Energy status</h1>" 
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

const char HTTP_ETHERNET[] PROGMEM =
    "<h1>Config Ethernet</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-6'><form method='POST' action='saveEther'>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='etherEnable' type='checkbox' name='etherEnable' {{checkedEthernet}}>"
    "<label class='form-check-label' for='etherEnable'>Enable</label>"
    "</div>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='dhcp' type='checkbox' name='dhcp' {{modeEther}}>"
    "<label class='form-check-label' for='dhcp'>DHCP</label>"
    "</div>"
    "<div class='form-group'>"
    "<label for='ip'>@IP</label>"
    "<input class='form-control' id='ip' type='text' name='ipAddress' value='{{ipEther}}'>"
    "</div>"
    "<div class='form-group'>"
    "<label for='mask'>@Mask</label>"
    "<input class='form-control' id='mask' type='text' name='ipMask' value='{{maskEther}}'>"
    "</div>"
    "<div class='form-group'>"
    "<label for='gateway'>@Gateway</label>"
    "<input type='text' class='form-control' id='gateway' name='ipGW' value='{{GWEther}}'>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form>";

const char HTTP_CONFIG_WIFI[] PROGMEM =
    "<h1>Config WiFi</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-6'><form method='POST' action='saveWifi'>"
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

const char HTTP_CREATE_TEMPLATE[] PROGMEM =
    "<h1>Create tp file</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-6'><form method='POST' action='saveFileTemplates'>"
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
    //"getFormattedDate();"
    "getAlert();"
    "</script>";

String getMenuGeneral(String tmp, String selected)
{
  
  tmp.replace("{{menu_config}}", FPSTR(HTTP_CONFIG_MENU));
  if (selected=="general")
  {
    tmp.replace("{{menu_config_general}}", "disabled");
  }else{
    tmp.replace("{{menu_config_general}}", "");
  }
  if (selected=="horloge")
  {
    tmp.replace("{{menu_config_horloge}}", "disabled");
  }else{
    tmp.replace("{{menu_config_horloge}}", "");
  }
  if (selected=="linky")
  {
    tmp.replace("{{menu_config_linky}}", "disabled");
  }else{
    tmp.replace("{{menu_config_linky}}", "");
  }
  if (selected=="gaz")
  {
    tmp.replace("{{menu_config_gaz}}", "disabled");
  }else{
    tmp.replace("{{menu_config_gaz}}", "");
  }
  if (selected=="water")
  {
    tmp.replace("{{menu_config_water}}", "disabled");
  }else{
    tmp.replace("{{menu_config_water}}", "");
  }
  if (selected=="http")
  {
    tmp.replace("{{menu_config_http}}", "disabled");
  }else{
    tmp.replace("{{menu_config_http}}", "");
  }
  if (selected=="mqtt")
  {
    tmp.replace("{{menu_config_mqtt}}", "disabled");
  }else{
    tmp.replace("{{menu_config_mqtt}}", "");
  }
  if (selected=="webpush")
  {
    tmp.replace("{{menu_config_webpush}}", "disabled");
  }else{
    tmp.replace("{{menu_config_webpush}}", "");
  }
  if (selected=="modbus")
  {
    tmp.replace("{{menu_config_modbus}}", "disabled");
  }else{
    tmp.replace("{{menu_config_modbus}}", "");
  }
  if (selected=="notif")
  {
    tmp.replace("{{menu_config_notif}}", "disabled");
  }else{
    tmp.replace("{{menu_config_notif}}", "");
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
            strlcpy(t->e[i].mqtt_device_class, "", sizeof(t->e[i].mqtt_device_class));
          }
          if (temp[model][0][F("status")][i][F("mqtt_state_class")])
          {
            strlcpy(t->e[i].mqtt_state_class, temp[model][0][F("status")][i][F("mqtt_state_class")], sizeof(t->e[i].mqtt_state_class));
          }
          else
          {
            strlcpy(t->e[i].mqtt_state_class, "", sizeof(t->e[i].mqtt_state_class));
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
        }
        t->ActionSize = i;
        // tmp = temp[model][0]["bind"];
        // strlcpy(t.bind,tmp,sizeof(50));
        return t;
      }
      else
      {
        if (temp.containsKey("default"))
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
    response->addHeader(F("Location"), F("/statusNetwork"));
    request->send(response);
  }

}

void handleDashboard(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADERGRAPH);
  result += FPSTR(HTTP_DASHBOARD);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

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
        dashboard += F("<div class='col-sm-6' style='padding:0;'><div class='card'><div class='card-header'>");
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
                dashboard += "<div id='gauge_";
                dashboard += (String)ShortAddr+String(i);
                dashboard += F("' style='height:200px;'>");
                dashboard += F("</div>");
                js += createGaugeDashboard((String)ShortAddr, (String)i, String(t->e[i].jaugeMin), String(t->e[i].jaugeMax), t->e[i].unit);
                js += CreateTimeGauge((String)ShortAddr + (String)i);
                js += "refreshGauge" + (String)ShortAddr + (String)i + "('" + tmp.substring(0, 16) + "'," + t->e[i].cluster + "," + t->e[i].attribute + ",'" + t->e[i].type + "'," + t->e[i].coefficient + ");";
              }
              else if(String(t->e[i].typeJauge) == "battery")
              {
                dashboard += "<div id='gauge_";
                dashboard += (String)ShortAddr+String(i);
                dashboard += F("' style='height:200px;'>");
                dashboard += F("</div>");
                js += createBaterryDashboard((String)ShortAddr, (String)i, String(t->e[i].jaugeMin), String(t->e[i].jaugeMax), t->e[i].unit);
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
    file.close();
    file = root.openNextFile();
  }
  file.close();
  root.close();
  result.replace("{{dashboard}}", dashboard);

  String javascript = "";
  javascript = F("<script language='javascript'>");
  javascript += F("$(document).ready(function() {");
  javascript += F("loadPowerGaugeAbo(1");
  javascript += F(",'");
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
  result.replace("{{javascript}}", javascript);

  request->send(200, "text/html", result);
}

void handleStatusNetwork(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADERGRAPH);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_NETWORK);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);

  /*if (ConfigSettings.enableEthernet)
  {
    result.replace("{{enableEther}}", F("<img src='/web/img/ok.png'>"));
  }
  else
  {
    result.replace("{{enableEther}}", F("<img src='/web/img/nok.png'>"));
  }

  if (ConfigSettings.enableWiFi)
  {
    result.replace("{{enableWifi}}", F("<img src='/web/img/ok.png'>"));
  }
  else
  {
    result.replace("{{enableWifi}}", F("<img src='/web/img/nok.png'>"));
  }*/
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
  

  if (ConfigSettings.dhcp)
  {
    result.replace("{{modeEther}}", "DHCP");
    result.replace("{{ipEther}}", ETH.localIP().toString());
    result.replace("{{maskEther}}", ETH.subnetMask().toString());
    result.replace("{{GWEther}}", ETH.gatewayIP().toString());
  }
  else
  {
    result.replace("{{modeEther}}", "STATIC");
    result.replace("{{ipEther}}", ConfigSettings.ipAddress);
    result.replace("{{maskEther}}", ConfigSettings.ipMask);
    result.replace("{{GWEther}}", ConfigSettings.ipGW);
  }
  if (ConfigSettings.connectedWifiSta)
  {
    result.replace("{{connectedWifi}}", F("<img src='/web/img/ok.png'>"));
  }
  else
  {
    result.replace("{{connectedWifi}}", F("<img src='/web/img/nok.png'>"));
  }
  if (ConfigSettings.connectedEther)
  {
    result.replace("{{connectedEther}}", F("<img src='/web/img/ok.png'>"));
  }
  else
  {
    result.replace("{{connectedEther}}", F("<img src='/web/img/nok.png'>"));
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
  result += FPSTR(HTTP_FOOTER);
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
  String result;

  result = F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h1>Status Devices</h1>");
  result += F("<h2>List of devices</h2>");
  result += F("<div class='row'>");

  String str = "";
  File root = LittleFS.open("/db");
  File file = root.openNextFile();

  int i=0;
  while (file)
  {
    String tmp = file.name();
    // tmp = tmp.substring(10);
    if (tmp.substring(16) == ".json")
    {
      result += F("<div class='col-sm-3'><div class='card'><div class='card-header'>@Mac : ");
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
      result += F("' style='display:none'>");
      result += F("<strong>Manufacturer: </strong>");
      String manufacturer;
      manufacturer = GetManufacturer(file.name());
      result += manufacturer;
      result += F("<br><strong>Model: </strong>");
      String model;
      model = GetModel(file.name());
      result += model;
      result += F("<br><strong>Short Address: </strong>");
      char SAddr[5];
      int ShortAddr = GetShortAddr(file.name());
      snprintf(SAddr,5,"%04X", ShortAddr);
      result += SAddr;
      result += F("<br><strong>Device Id: </strong>");
      char devId[5];
      int DeviceId = GetDeviceId(file.name());
      snprintf(devId,5, "%04X", DeviceId);
      result += devId;
      result += F("<br><strong>Soft Version: </strong>");
      String SoftVer = GetSoftwareVersion(file.name());
      result += SoftVer;
      result += F("<br><strong>Last seen: </strong>");
      String lastseen = GetLastSeen(file.name());
      result += lastseen;
      result += F("<br><strong>LQI: </strong>");
      result += GetLQI(file.name());
      result += "</div>";
      // Get status and action from json
      
      if (TemplateExist(DeviceId))
      {
        Template *t;
        t = GetTemplate(DeviceId, model);
        // toutes les propiétés
        result += F("<div id='status_");
        result += (String)ShortAddr;
        result += F("'>");

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
                result += t->e[i].name;
                result += " : ";
                result += GetValueStatus(file.name(), t->e[i].cluster, t->e[i].attribute, (String)t->e[i].type, t->e[i].coefficient, (String)t->e[i].unit);
                result += F("<br>");
              }
            }else{
              result += t->e[i].name;
              result += " : ";
              result += GetValueStatus(file.name(), t->e[i].cluster, t->e[i].attribute, (String)t->e[i].type, t->e[i].coefficient, (String)t->e[i].unit);
              result += F("<br>");
            }
          }
        }
        result += F("</div>");
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
        
      }
      result += F("</div></div></div>");
    }
    i++;
    file.close();
    file = root.openNextFile();
  }
  result += FPSTR(HTTP_FOOTER);
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
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

  result = getMenuGeneral(result, "general");

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

  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_ZIGBEE);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
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
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

  result = getMenuGeneral(result, "horloge");

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
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
  if (ConfigSettings.enableMqtt)
  {
    result.replace("{{checkedMqtt}}", "Checked");
  }
  else
  {
    result.replace("{{checkedMqtt}}", "");
  }
  result = getMenuGeneral(result, "mqtt");

  result.replace("{{FormattedDate}}", FormattedDate);

  result.replace("{{servMQTT}}", String(ConfigGeneral.servMQTT));
  result.replace("{{portMQTT}}", String(ConfigGeneral.portMQTT));
  result.replace("{{userMQTT}}", String(ConfigGeneral.userMQTT));
  if (String(ConfigGeneral.passMQTT) !="")
  {
    result.replace("{{passMQTT}}", "********");
  }else{
    result.replace("{{passMQTT}}", "");
  }
  //result.replace("{{passMQTT}}", String(ConfigGeneral.passMQTT));
  result.replace("{{headerMQTT}}", String(ConfigGeneral.headerMQTT));

  

  request->send(200, "text/html", result);
}

void handleConfigHTTP(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_HTTP);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
  result = getMenuGeneral(result, "http");

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

void handleConfigLinky(AsyncWebServerRequest *request)
{
  String result,list;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_LINKY);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

  result = getMenuGeneral(result, "linky");

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
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

  result = getMenuGeneral(result, "gaz");

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
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

  result = getMenuGeneral(result, "water");

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
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

  result = getMenuGeneral(result, "notif");

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

void handleConfigModbus(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_MODBUS);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

  result = getMenuGeneral(result, "modbus");

  result.replace("{{FormattedDate}}", FormattedDate);
  if (ConfigSettings.enableModbus)
  {
    result.replace("{{checkedModbus}}", "Checked");
  }
  else
  {
    result.replace("{{checkedModbus}}", "");
  }

  request->send(200, "text/html", result);
}

void handleConfigWebPush(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_WEBPUSH);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

  result = getMenuGeneral(result, "webpush");

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

void handleConfigWifi(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_WIFI);
  result += FPSTR(HTTP_FOOTER);
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

  result += F("<h1>Console</h1>");
  result += F("<div class='row justify-content-md-center'>");
  result += F("<div class='col-sm-6'>");
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
  result += F("<div class='col-sm-6'>");

  result += F("Raw datas : <textarea id='console' rows='16' cols='100'>");

  result += F("</textarea></div></div>");
  // result += F("</div>");
  result += F("</body>");
  result += F("<script language='javascript'>");
  result += F("$(document).ready(function() {");
  result += F("logRefresh();});");
  result += F("</script>");

  result += FPSTR(HTTP_FOOTER);
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
    result += FPSTR(HTTP_FOOTER);
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
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

  request->send(200, F("text/html"), result);*/
  
  
  String result;
  AsyncResponseStream *response = request->beginResponseStream("text/html");
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  response->print(result);
  result = FPSTR(HTTP_TOOLS);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
  response->print(result);
  request->send(response);

  /*AsyncWebServerResponse* response = request->beginChunkedResponse(contentType,
                                       [](uint8_t* buffer, size_t maxLen, size_t index) -> size_t 
    {

    });

  request->send(response);*/

}

void handleHelp(AsyncWebServerRequest * request) {
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += FPSTR(HTTP_HELP);
  result += FPSTR(HTTP_FOOTER);
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
  result += F("<h1>Reboot ...</h1>");
  result = result + F("</body>");
  result += FPSTR(HTTP_FOOTER);
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
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
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
  Serial.printf("Progress: %d%%\n", (prg*100)/content_len);
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
    file = root.openNextFile();
  }
  
  root.close();
  file.close();

  //backup config
  root = LittleFS.open("/config");
  file = root.openNextFile();

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
    file = root.openNextFile();
  }
  
  root.close();
  file.close();

  //backup debug
  root = LittleFS.open("/debug");
  file = root.openNextFile();

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
    file = root.openNextFile();
  }
  
  root.close();
  file.close();

  //backup template
  root = LittleFS.open("/tp");
  file = root.openNextFile();

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
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      // tmp = tmp.substring(10);
      listFiles += F("<li><a href='web/");
      listFiles += tmp;
      listFiles += F("'>");
      listFiles += tmp;
      listFiles += F(" ( ");
      listFiles += file.size();
      listFiles += F(" o)</a></li>");
    }
    file.close();
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
    file = root.openNextFile();
  }
  root.close();
  file.close();
  result.replace("{{listBackupFiles}}", listFiles);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

  request->send(200, F("text/html"), result);
}


int totalLength;       //total size of firmware
int currentLength = 0; //current size of written firmware

void progressFunc(unsigned int progress,unsigned int total) {
  Serial.printf("Progress: %u of %u\r", progress, total);
};

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
           // runUpdateFirmware(buff, c);
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
    result += FPSTR(HTTP_FOOTER);
    result.replace("{{linkFirmware}}", UPD_FILE);
    result += F("</html>");

    request->send(200, F("text/html"), result);
    checkUpdateFirmware();
}



void handleConfigFiles(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h1>Config files</h1>");
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
  result += FPSTR(HTTP_FOOTER);
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
  result += F("<h1>Debug files</h1>");
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
  result += FPSTR(HTTP_FOOTER);
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
  result += F("<h1>backup list files</h1>");
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
  result += FPSTR(HTTP_FOOTER);
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
  result += F("<h1>Devices list files</h1>");
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
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
  file.close();
  root.close();
  request->send(200, F("text/html"), result);
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
  result += F("<h1>Templates</h1>");
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
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
  file.close();
  root.close();
  request->send(200, F("text/html"), result);
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

void handleJavascript(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h1>Javascript</h1>");
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
  result += FPSTR(HTTP_FOOTER);
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

void handleConfigEthernet(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_ETHERNET);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);

  if (ConfigSettings.enableEthernet)
  {
    result.replace("{{checkedEthernet}}", "Checked");
  }
  else
  {
    result.replace("{{checkedEthernet}}", "");
  }
  if (ConfigSettings.dhcp)
  {
    result.replace("{{modeEther}}", "Checked");
  }
  else
  {
    result.replace("{{modeEther}}", "");
  }
  result.replace("{{ipEther}}", ConfigSettings.ipAddress);
  result.replace("{{maskEther}}", ConfigSettings.ipMask);
  result.replace("{{GWEther}}", ConfigSettings.ipGW);

  request->send(200, "text/html", result);
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
  
  //MQTT connection process
  if (ConfigSettings.enableMqtt)
  {
    mqttClient.disconnect();
    mqttClient.setServer(ConfigGeneral.servMQTT, atoi(ConfigGeneral.portMQTT));
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

void handleSaveConfigModbus(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";
  String enableModbus;
  if (request->arg("enableModbus") == "on")
  {
    enableModbus = "1";
    ConfigSettings.enableModbus = true;
  }
  else
  {
    enableModbus = "0";
    ConfigSettings.enableModbus = false;
  }
 
  config_write(path, "enableModbus", enableModbus);

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configModbus"));
  request->send(response);
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

void handleSaveEther(AsyncWebServerRequest *request)
{
  if (!request->hasArg("ipAddress"))
  {
    request->send(500, "text/plain", "BAD ARGS");
    return;
  }

  String StringConfig;
  String dhcp;
  if (request->arg("dhcp") == "on")
  {
    dhcp = "1";
  }
  else
  {
    dhcp = "0";
  }

  String enableEthernet;
  if (request->arg("etherEnable") == "on")
  {
    enableEthernet = "1";
  }
  else
  {
    enableEthernet = "0";
  }

  String ipAddress = request->arg("ipAddress");
  String ipMask = request->arg("ipMask");
  String ipGW = request->arg("ipGW");

  const char *path = "/config/configEther.json";

  StringConfig = "{\"enable\":" + enableEthernet + ",\"dhcp\":" + dhcp + ",\"ip\":\"" + ipAddress + "\",\"mask\":\"" + ipMask + "\",\"gw\":\"" + ipGW + "\"}";
  DEBUG_PRINTLN(StringConfig);
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
  request->send(200, "text/html", "Save config OK ! <br><form method='GET' action='reboot'><input type='submit' name='reboot' value='Reboot'></form>");
}

void handleConfigDevices(AsyncWebServerRequest *request)
{
  String result;

  result = F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h1>Config</h1>");
  result += F("<div align='right'>");
  result += F("<button type='button' onclick='cmd(\"PermitJoin\");' class='btn btn-primary'>Add Device</button> ");
  result += F("<button type='button' onclick='cmd(\"Reset\");' class='btn btn-primary'>Reset</button> ");
  result += F("<br><button type='button' onClick=\"if (confirm('Are you sure ?')==true){cmd('ErasePDM');}else{return false;};\" class='btn btn-danger'>RAZ</button> ");
  
  result += F("</div><br>");
  result += F("<h2>List of devices</h2>");
  result += F("<div class='row'>");

  String str = "";
  File root = LittleFS.open("/db");
  File file = root.openNextFile();
  int i=0;
  while (file)
  {
    String tmp = file.name();
    // tmp = tmp.substring(10);
    if (tmp.substring(16) == ".json")
    {
      result += F("<div class='col-sm-3'><div class='card'><div class='card-header'>@Mac : ");
      result += tmp.substring(0, 16);
      result += F("</div>");
      result += F("<div class='card-body'>");
      
      result += F("<strong>Manufacturer: </strong>");
      String manufacturer;
      manufacturer = GetManufacturer(file.name());
      result += manufacturer;
      result += F("<br><strong>Model: </strong>");
      String model;
      model = GetModel(file.name());
      result += model;
      result += F("<br><strong>Short Address: </strong>");
      char SAddr[5];
      int ShortAddr = GetShortAddr(file.name());
      snprintf(SAddr,5, "%04X", ShortAddr);
      result += SAddr;
      result += F("<br><strong>Device Id: </strong>");
      char devId[5];
      int DeviceId = GetDeviceId(file.name());
      snprintf(devId,5, "%04X", DeviceId);
      result += devId;
      result += F("<br><strong>Soft Version: </strong>");
      String SoftVer = GetSoftwareVersion(file.name());
      result += SoftVer;
      //result += F("<br><strong>Last seen: </strong>");
      //String lastseen = GetLastSeen(file.name());
     // result += lastseen;
      result += F("<br><strong>LQI: </strong>");
      result += GetLQI(file.name());
      
     
      // Paramétrages
      result += F("<div>");

        if (ConfigSettings.enableMqtt)
        {
          result += F("<button onclick=\"sendMqttDiscover('");
          result += ShortAddr;
          result += "');\" class='btn btn-warning mb-2'>";
          result += "MQTT Discover";
          result += F("</button>");
        }

        result += F("<button onclick=\"ZigbeeSendRequest(");
        result += ShortAddr;
        result += ",";
        result += "0,5";
        result += ");\" class='btn btn-warning mb-2'>";
        result += "Reinit";
        result += F("</button>");
      
        result += F("<button onclick=\"deleteDevice('");
        result += ShortAddr;
        result += "');\" class='btn btn-danger mb-2'>";
        result += "Delete";
        result += F("</button>");
      result += F("</div>");
      result += F("</div></div></div>");
      
    }
    
    i++;
    file.close();
    file = root.openNextFile();
  }
  result += F("</div>");
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
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
  result = GetValueStatus(IEEE + ".json", Cluster.toInt(), Attribute.toInt(), (String)Type, Coefficient.toFloat(), "");

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

  result = GetValueStatus(file, Cluster.toInt(), Attribute.toInt(), Type, Coefficient.toFloat(), Unit);

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
        const char* PROGMEM HA_discovery_msg = "{"
            "\"name\":\"{{name_prop}}\","
            "\"unique_id\":\"{{unique_id}}\","
            "\"device_class\":\"{{device_class}}\","
            "\"state_class\":\"{{state_class}}\","
            "\"unit_of_measurement\":\"{{unit}}\","
            "\"icon\":\"mdi:{{mqtt_icon}}\","
            "\"state_topic\":\"{{state_topic}}/state\","
            "\"value_template\":\"{{value}}\","
            "\"device\": {"
                "\"name\":\"LiXee-Box_{{device_name}}\","
                "\"sw_version\":\"2.0\","
                "\"model\":\"HW V2\","
                "\"manufacturer\":\"LiXee\","
                "\"identifiers\":[\"LiXee-Box{{device_name}}\"]"
            "}"
        "}";
        datas = FPSTR(HA_discovery_msg);
        
        datas.replace("{{name_prop}}", t->e[i].name);
        datas.replace("{{unique_id}}", IEEE+"_"+String(t->e[i].cluster)+"_"+String(t->e[i].attribute));
        datas.replace("{{device_class}}", t->e[i].mqtt_device_class);
        datas.replace("{{state_class}}", t->e[i].mqtt_state_class);
        datas.replace("{{mqtt_icon}}", t->e[i].mqtt_icon);
        datas.replace("{{unit}}", t->e[i].unit);
        datas.replace("{{state_topic}}", ConfigGeneral.headerMQTT+ IEEE+"_"+String(t->e[i].cluster)+"_"+String(t->e[i].attribute));
        if ((String(t->e[i].type)=="numeric") || (String(t->e[i].type)=="float"))
        {
          if (t->e[i].coefficient!=1)
          {
            datas.replace("{{value}}", "{{ value_json."+String(t->e[i].cluster)+"_"+String(t->e[i].attribute)+" | float * "+String(t->e[i].coefficient)+"}}");
          }else{
            datas.replace("{{value}}", "{{ value_json."+String(t->e[i].cluster)+"_"+String(t->e[i].attribute)+"}}");
          }
        }else{
          datas.replace("{{value}}", "{{ value_json."+String(t->e[i].cluster)+"_"+String(t->e[i].attribute)+"}}");
        }
        datas.replace("{{device_name}}", model+"_"+IEEE);
        String topic = ConfigGeneral.headerMQTT+ IEEE+"_"+String(t->e[i].cluster)+"_"+String(t->e[i].attribute)+"/config";

        mqttClient.publish(topic.c_str(),1,true,datas.c_str());
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

  String SA, tmpMac;
  int i = 0;
  SA = request->arg(i);
  tmpMac = GetMacAdrr(SA.toInt());
  tmpMac = tmpMac.substring(0, 16);
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

  if (res == 0)
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
    if (xSemaphoreTake(file_Mutex, portMAX_DELAY) == pdTRUE) 
    {
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
      xSemaphoreGive(file_Mutex);
    }
      filedevice.close();
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
    if (xSemaphoreTake(file_Mutex, portMAX_DELAY) == pdTRUE) 
    {
      File file = LittleFS.open("/db/" + inifile, FILE_READ);
      if (!file || file.isDirectory())
      {
        result = "Échec de l'ouverture du fichier : "+ inifile;
        request->send(500, "text/plain", result);
        file.close();
        xSemaphoreGive(file_Mutex);
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
      xSemaphoreGive(file_Mutex);
      result += "}";
    }
    
  }else{
    result="{}";
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
    if (xSemaphoreTake(file_Mutex, portMAX_DELAY) == pdTRUE) 
    {
      File file = LittleFS.open("/db/" + inifile, FILE_READ);
      if (!file || file.isDirectory())
      {
        result = "Échec de l'ouverture du fichier : "+ inifile;
        request->send(500, "text/plain", result);
        file.close();
        xSemaphoreGive(file_Mutex);
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
      xSemaphoreGive(file_Mutex);
      result += "}";
    }
    
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
    if (xSemaphoreTake(file_Mutex, portMAX_DELAY) == pdTRUE) 
    {
      File file = LittleFS.open("/db/" + inifile, FILE_READ);
      if (!file || file.isDirectory())
      {
        result = "Échec de l'ouverture du fichier : "+ inifile;
        request->send(500, "text/plain", result);
        file.close();
        xSemaphoreGive(file_Mutex);
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
      xSemaphoreGive(file_Mutex);
      result += "}";
    }
    
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
    if (xSemaphoreTake(file_Mutex, portMAX_DELAY) == pdTRUE) 
    {
      File file = LittleFS.open("/tp/" + inifile, FILE_READ);
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier ini_read "));
        DEBUG_PRINTLN(inifile);
        file.close();
        xSemaphoreGive(file_Mutex);
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
      xSemaphoreGive(file_Mutex);
    }
    filedevice.close();
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
  serverWeb.on("/configWebPush", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigWebPush(request); 
  });
  serverWeb.on("/configModbus", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigModbus(request); 
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
  serverWeb.on("/configEthernet", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigEthernet(request); 
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
  serverWeb.on("/saveFileTemplates", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveTemplates(request); 
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
  serverWeb.on("/saveConfigModbus", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveConfigModbus(request); 
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
  serverWeb.on("/saveEther", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveEther(request); 
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
    /*if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }*/
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
    /*if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }*/
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
  serverWeb.on("/javascript", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP) )
        return request->requestAuthentication();
    }
    handleJavascript(request); 
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
  serverWeb.serveStatic("/web/js/justgage.min.js.map", LittleFS, "/web/js/justgage.min.js.map").setCacheControl("max-age=600");
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
