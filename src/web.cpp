#include <stdio.h>
#include "rom/ets_sys.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include <esp_task_wdt.h>
#include <stddef.h>
#include <Arduino.h>
#include "AsyncJson.h"
#include <ArduinoJson.h>
#include <ETH.h>
#include "WiFi.h"

// #include <WebServer.h>
#include <HTTPClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "FS.h"
#include "LittleFS.h"
#include "SPIFFS_ini.h"
// #include "web.h"
#include "config.h"
#include "flash.h"
#include "log.h"
#include "protocol.h"
#include "zigbee.h"
#include "basic.h"

extern struct ZigbeeConfig ZConfig;
extern struct ConfigSettingsStruct ConfigSettings;
extern struct ConfigGeneralStruct ConfigGeneral;
extern unsigned long timeLog;
extern CircularBuffer<Packet, 20> commandList;
extern CircularBuffer<Alert, 10> alertList;

int maxDayOfTheMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

extern String Hour;
extern String Day;
extern String Month;
extern String Year;
extern String FormattedDate;

AsyncWebServer serverWeb(80);

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
    "<nav class='navbar navbar-expand-lg navbar-light bg-light rounded'><div class='container-fluid'><a class='navbar-brand' href='/'>"
    "<div style='display:block-inline;float:left;'><img src='web/img/logo.png'> </div>"
    "<div style='float:left;display:block-inline;font-weight:bold;padding:18px 10px 10px 10px;'> Box Config</div>"
    "<div id='FormattedDate' style='display:block;padding-top:58px;font-size:12px;'>{{FormattedDate}}</div>"
    "</a>"
    "<button class='navbar-toggler' type='button' data-bs-toggle='collapse' data-bs-target='#navbarNavDropdown' aria-controls='navbarNavDropdown' aria-expanded='false' aria-label='Toggle navigation'>"
    "<span class='navbar-toggler-icon'></span>"
    "</button>"
    "<div id='navbarNavDropdown' class='collapse navbar-collapse justify-content-md-center'>"
    "<ul class='navbar-nav me-auto mb-2 mb-lg-0'>"
    "<li class='nav-item'>"
    "<a class='nav-link' href='/'>Dashboard</a>"
    "</li>"
    "<li class='nav-item dropdown'>"
    "<a class='nav-link dropdown-toggle' href='#' id='navbarDropdown' role='button' data-bs-toggle='dropdown'>Status</a>"
    "<div class='dropdown-menu'>"
    "<a class='dropdown-item' href='statusEnergy'>Energy</a>"
    "<a class='dropdown-item' href='statusNetwork'>Network</a>"
    "<a class='dropdown-item' href='statusDevices'>Devices</a>"
    "</div>"
    "</li>"
    "<li class='nav-item dropdown'>"
    "<a class='nav-link dropdown-toggle' href='#' id='navbarDropdown' role='button' data-bs-toggle='dropdown'>Config</a>"
    "<div class='dropdown-menu'>"
    "<a class='dropdown-item' href='configGeneral'>General</a>"
    "<a class='dropdown-item' href='configZigbee'>Zigbee</a>"
    "<a class='dropdown-item' href='configWiFi'>WiFi</a>"
    "<a class='dropdown-item' href='configEthernet'>Ethernet</a>"
    "<a class='dropdown-item' href='configDevices'>Devices</a>"
    "</div>"
    "</li>"
    "<li class='nav-item'>"
    "<a class='nav-link' href='/tools'>Tools</a>"
    "</li>"
    "<li class='nav-item'>"
    "<a class='nav-link' href='#'>Help</a>"
    "</li>"
    "</ul></div></div>"
    "</nav>"
    "<div id='alert' style='display:none;' class='alert alert-success' role='alert'>"
    "</div>";

const char HTTP_TOOLS[] PROGMEM =
    "<h1>Tools</h1>"
    "<div class='btn-group-vertical'>"
    "<a href='/logs' class='btn btn-primary mb-2'>Console</a>"
    "<a href='/configFiles' class='btn btn-primary mb-2'>Config Files</a>"
    "<a href='/fsbrowser' class='btn btn-primary mb-2'>Device Files</a>"
    "<a href='/templates' class='btn btn-primary mb-2'>Templates</a>"
    "<a href='/javascript' class='btn btn-primary mb-2'>Javascript</a>"
    "<a href='/update' class='btn btn-primary mb-2'>Update</a>"
    "<a href='/reboot' class='btn btn-primary mb-2'>Reboot</a>"
    "</div>";

const char HTTP_CONFIG_MENU[] PROGMEM =
    "<a href='/configGeneral' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_general}}' >General</a>&nbsp"
    "<a href='/configHorloge' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_horloge}}' >Horloge</a>&nbsp"
    "<a href='/configLinky' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_linky}}' >Linky</a>&nbsp"
    "<a href='/configHTTP' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_http}}' >HTTP</a>&nbsp"
    "<a href='/configMQTT' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_mqtt}}' >MQTT</a>&nbsp"
    "<a href='/configNotif' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_notif}}' >Notif</a>&nbsp";

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
    "<label for='SetMaskChannel'>Set channel mask</label>"
    "<input class='form-control' id='SetMaskChannel' type='text' name='SetMaskChannel' value='{{SetMaskChannel}}'>"
    "<button type='button' onclick='cmd(\"SetChannelMask\",document.getElementById(\"SetMaskChannel\").value);' class='btn btn-primary'>Set Channel</button><br> "
    "<h2>Console</h2>"
    "<button type='button' onclick='cmd(\"ClearConsole\");document.getElementById(\"console\").value=\"\";' class='btn btn-primary'>Clear Console</button> "
    "<button type='button' onclick='cmd(\"GetVersion\");' class='btn btn-primary'>Get Version</button> "
    "<button type='button' onclick='cmd(\"ErasePDM\");' class='btn btn-primary'>Erase PDM</button> "
    "<button type='button' onclick='cmd(\"Reset\");' class='btn btn-primary'>Reset</button> "
    "<button type='button' onclick='cmd(\"StartNwk\");' class='btn btn-primary'>StartNwk</button> "
    "<button type='button' onclick='cmd(\"PermitJoin\");' class='btn btn-primary'>Permit Join</button> "
    "<br>Raw datas : <br><textarea id='console' rows='16' cols='100'></textarea>"
    "</div>"
    "</div>"
    "<script language='javascript'>"
    "$(document).ready(function() {"
    "logRefresh();});"
    "</script>";

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

    "<br>"
    "</div>"
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
    "<div class='row'>"
    "<div class='col-sm-6'>"
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
    "</div>"
    "<div class='col-sm-6'>"
    "<div class='card'>"
    "<div class='card-header'>Wifi</div>"
    "<div class='card-body'>"
    "<div id='wifiConfig'>"
    "<strong>Enable : </strong>{{enableWifi}}"
    "<br><strong>Connected : </strong>{{connectedWifi}}"
    "<br><strong>SSID : </strong>{{ssidWifi}}"
    "<br><strong>@IP : </strong>{{ipWifi}}"
    "<br><strong>@Mask : </strong>{{maskWifi}}"
    "<br><strong>@GW : </strong>{{GWWifi}}"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "<div class='row'>"
    "<div class='col-sm-6'><div class='card'><div class='card-header'>Infos"
    "</div>"
    "<div class='card-body'>"
    "<Strong>Box voltage :</strong> {{Voltage}} V<br>"
    "<Strong>Box temperature :</strong> {{Temperature}} °C<br>"
    "</div></div></div>"
    "</div>";

const char HTTP_ROOT[] PROGMEM =
    "<h1>Dashboard</h1>"
    "<div class='row'>"
    "<div class='col-sm-12'>"
    "<Select class='form-select form-select-lg mb-3' aria-label='.form-select-lg example' name='time' onChange=\"window.location.href='?time='+this.value\">"
    "<option value='hour' {{selectedHour}}>Hour</option>"
    "<option value='day' {{selectedDay}}>Day</option>"
    "<option value='month' {{selectedMonth}}>Month</option>"
    "<option value='year' {{selectedYear}}>Year</option>"
    "</select>"
    "</div>"
    "</div>"
    "<div class='row'>"
    "<div class='col-sm-6'>"
    "<div class='card'>"
    "<div class='card-header'>Energy gauge</div>"
    "<div id='power_gauge_global'></div>"
    "</div>"
    "</div>"
    "<div class='col-sm-6'>"
    "<div class='card'>"
    "<div class='card-header'>Energy trend</div>"
    "<div id='power_trend' style='padding-top:40px;'></div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "<div class='row'>"
    "{{dashboard}}"
    "</div>"
    "{{javascript}}";

const char HTTP_DASHBOARD[] PROGMEM =
    "<div class='row'>"
    "<div class='col-sm-6'>"
    "<div class='card'>"
    "<div class='card-header'>Energy gauge</div>"
    "<div id='power_gauge_global'></div>"
    "</div>"
    "</div>"
    "<div class='col-sm-6'>"
    "<div class='card'>"
    "<div class='card-header'>Energy trend</div>"
    "<div id='power_trend' style='padding-top:40px;'></div>"
    "</div>"
    "</div>"
    "</div>"
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
    "</div>"
    "<div class='row'>"
    "<div class='col-sm-3'>"
    "<div id='power_gauge_global'></div>"
    "</div>"
    "<div class='col-sm-3'>"
    "<div id='power_trend' style='padding-top:40px;'></div>"
    "</div>"
    "<div class='col-sm-6'>"
    "<div class='card'>"
    "<div class='card-header'>Linky Datas</div>"
    "<div class='card-body'>"
    "<div id='power_data'></div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "<div class='row' style='display:{{stylePowerChart}}'>"
    "<div class='col-sm-12'>"
    "<div class='card'>"
    "<div class='card-header'>Power chart</div>"
    "<div class='card-body'>"
    "<div id='power-chart'></div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "<div class='row'>"
    "<div class='col-sm-12'>"
    "<div class='card'>"
    "<div class='card-header'>Energy chart</div>"
    "<div class='card-body'>"
    "<div id='energy-chart'></div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
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
    "<div class='form-check'>"
    "<input class='form-check-input' id='wifiEnable' type='checkbox' name='wifiEnable' {{checkedWiFi}}>"
    "<label class='form-check-label' for='wifiEnable'>Enable</label>"
    "</div>"
    "<div class='form-group'>"
    "<label for='ssid'>SSID</label>"
    "<input class='form-control' id='ssid' type='text' name='WIFISSID' value='{{ssid}}'> <a onclick='scanNetwork();' class='btn btn-primary mb-2'>Scan</a><div id='networks'></div>"
    "</div>"
    "<div class='form-group'>"
    "<label for='pass'>Password</label>"
    "<input class='form-control' id='pass' type='password' name='WIFIpassword' value=''>"
    "</div>"
    "<div class='form-group'>"
    "<label for='ip'>@IP</label>"
    "<input class='form-control' id='ip' type='text' name='ipAddress' value='{{ip}}'>"
    "</div>"
    "<div class='form-group'>"
    "<label for='mask'>@Mask</label>"
    "<input class='form-control' id='mask' type='text' name='ipMask' value='{{mask}}'>"
    "</div>"
    "<div class='form-group'>"
    "<label for='gateway'>@Gateway</label>"
    "<input type='text' class='form-control' id='gateway' name='ipGW' value='{{gw}}'>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form>";

const char HTTP_CREATE_TEMPLATE[] PROGMEM =
    "<h1>Create template file</h1>"
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
    "getFormattedDate();"
    "getAlert();"
    "</script>";

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
  return temp_c;
}

bool TemplateExist(int deviceId)
{
  String path = "/templates/" + (String)deviceId + ".json";

  File templateFile = LittleFS.open(path, FILE_READ);
  if (!templateFile || templateFile.isDirectory())
  {
    return false;
  }
  templateFile.close();
  return true;
}

Template GetTemplate(int deviceId, String model)
{
  String path = "/templates/" + String(deviceId) + ".json";
  Template t;
  File templateFile = LittleFS.open(path, FILE_READ);
  if (!templateFile || templateFile.isDirectory())
  {
    DEBUG_PRINTLN(F("failed open"));
    return t;
  }
  else
  {
    DynamicJsonDocument temp(5096);
    deserializeJson(temp, templateFile);
    templateFile.close();
    int i = 0;
    const char *tmp;

    if (temp.containsKey(model))
    {
      
      JsonArray StatusArray = temp[model][0]["status"].as<JsonArray>();
      for (JsonVariant v : StatusArray)
      {

        tmp = temp[model][0]["status"][i]["name"];
        strlcpy(t.e[i].name, tmp, sizeof(t.e[i].name));
        t.e[i].cluster = (int)strtol(temp[model][0]["status"][i]["cluster"], 0, 16);
        t.e[i].attribute = (int)temp[model][0]["status"][i]["attribut"];
        if (temp[model][0]["status"][i]["type"])
        {
          strlcpy(t.e[i].type, temp[model][0]["status"][i]["type"], sizeof(t.e[i].type));
        }
        else
        {
          strlcpy(t.e[i].type, "", sizeof(t.e[i].type));
        }
        if (temp[model][0]["status"][i]["coefficient"])
        {
          t.e[i].coefficient = (float)temp[model][0]["status"][i]["coefficient"];
        }
        else
        {
          t.e[i].coefficient = 1;
        }
        if (temp[model][0]["status"][i]["unit"])
        {
          strlcpy(t.e[i].unit, temp[model][0]["status"][i]["unit"], sizeof(t.e[i].unit));
        }
        else
        {
          strlcpy(t.e[i].unit, "", sizeof(t.e[i].unit));
        }

        if (temp[model][0]["status"][i]["visible"].as<int>() == 1)
        {
          t.e[i].visible = 1;
        }
        else
        {
          t.e[i].visible = 0;
        }
        if (temp[model][0]["status"][i]["jauge"])
        {
          strlcpy(t.e[i].typeJauge, temp[model][0]["status"][i]["jauge"], sizeof(t.e[i].typeJauge));
          t.e[i].jaugeMin = temp[model][0]["status"][i]["min"].as<int>();
          t.e[i].jaugeMax = temp[model][0]["status"][i]["max"].as<int>();
        }
        else
        {
          strlcpy(t.e[i].typeJauge, "", sizeof(t.e[i].typeJauge));
        }
        i++;
      }
      t.StateSize = i;
      i = 0;
      JsonArray ActionArray = temp[model][0]["action"].as<JsonArray>();
      for (JsonVariant v : ActionArray)
      {

        tmp = temp[model][0]["action"][i]["name"];
        strlcpy(t.a[i].name, tmp, sizeof(t.a[i].name));
        t.a[i].command = (int)temp[model][0]["action"][i]["command"];
        t.a[i].value = (int)temp[model][0]["action"][i]["value"];
        if (temp[model][0]["action"][i]["visible"].as<int>() == 1)
        {
          t.a[i].visible = 1;
        }
        else
        {
          t.a[i].visible = 0;
        }
        i++;
      }
      t.ActionSize = i;
      // tmp = temp[model][0]["bind"];
      // strlcpy(t.bind,tmp,sizeof(50));
      return t;
    }
    else
    {
      if (temp.containsKey("default"))
      {
        JsonArray StatusArray = temp["default"][0]["status"].as<JsonArray>();
        for (JsonVariant v : StatusArray)
        {

          tmp = temp["default"][0]["status"][i]["name"];
          strlcpy(t.e[i].name, tmp, sizeof(t.e[i].name));
          t.e[i].cluster = (int)strtol(temp["default"][0]["status"][i]["cluster"], 0, 16);
          t.e[i].attribute = (int)temp["default"][0]["status"][i]["attribut"];
          if (temp["default"][0]["status"][i]["type"])
          {
            strlcpy(t.e[i].type, temp["default"][0]["status"][i]["type"], sizeof(t.e[i].type));
          }
          else
          {
            strlcpy(t.e[i].type, "", sizeof(t.e[i].type));
          }
          if (temp["default"][0]["status"][i]["coefficient"])
          {
            t.e[i].coefficient = (float)temp["default"][0]["status"][i]["coefficient"];
          }
          else
          {
            t.e[i].coefficient = 1;
          }
          if (temp["default"][0]["status"][i]["unit"])
          {
            strlcpy(t.e[i].unit, temp["default"][0]["status"][i]["unit"], sizeof(t.e[i].unit));
          }
          else
          {
            strlcpy(t.e[i].unit, "", sizeof(t.e[i].unit));
          }
          if (temp["default"][0]["status"][i]["visible"].as<int>() == 1)
          {
            t.e[i].visible = 1;
          }
          else
          {
            t.e[i].visible = 0;
          }
          if (temp["default"][0]["status"][i]["jauge"])
          {
            strlcpy(t.e[i].typeJauge, temp["default"][0]["status"][i]["jauge"], sizeof(t.e[i].typeJauge));
            t.e[i].jaugeMin = temp["default"][0]["status"][i]["min"].as<int>();
            t.e[i].jaugeMax = temp["default"][0]["status"][i]["max"].as<int>();
          }
          else
          {
            strlcpy(t.e[i].typeJauge, "", sizeof(t.e[i].typeJauge));
          }
          i++;
        }
        t.StateSize = i;
        i = 0;
        JsonArray ActionArray = temp["default"][0]["action"].as<JsonArray>();
        for (JsonVariant v : ActionArray)
        {

          tmp = temp["default"][0]["action"][i]["name"];
          strlcpy(t.a[i].name, tmp, sizeof(t.a[i].name));
          t.a[i].command = (int)temp["default"][0]["action"][i]["command"];
          t.a[i].value = (int)temp["default"][0]["action"][i]["value"];
          if (temp["default"][0]["action"][i]["visible"].as<int>() == 1)
          {
            t.a[i].visible = 1;
          }
          else
          {
            t.a[i].visible = 0;
          }
          i++;
        }
        t.ActionSize = i;
      }
      else
      {
        t.StateSize = 0;
        t.ActionSize = 0;
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

String createGaugeDashboard(String div, String i, String min, String max, String label)
{
  String result = "";
  result += "var Gauge" + div + i + " = new JustGage({";
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
  result += F(" ykeys: ['a'],");
  result += F(" labels: ['Power (VA)'],");
  result += F("lineColors: ['#1e88e5'],");
  result += F(" lineWidth: '3px',");
  result += F(" resize: true,");
  result += F(" redraw: true,");
  result += F(" });");

  return result;
}

String createEnergyGraph(String IEEE)
{
  String result = "";
  String sep = "";
  result = F("energyChart = Morris.Bar({element: 'energy-chart',data: [],xkey: 'y',");
  // list attr
  String path = "/database/energy_" + IEEE + ".json";
  File DeviceFile = LittleFS.open(path, FILE_READ);
  if (!DeviceFile || DeviceFile.isDirectory())
  {
    DEBUG_PRINTLN(F("failed open"));
  }
  else
  {
    DynamicJsonDocument temp(5096);
    deserializeJson(temp, DeviceFile);
    JsonObject root = temp.as<JsonObject>();
    DeviceFile.close();

    result += F("ykeys: [");
    String JsonEuros = "{";
    int i = 0;
    for (JsonPair kv : root)
    {
      if (i > 0)
      {
        sep = ",";
      }
      else
      {
        sep = "";
      }
      JsonEuros += sep + "\"" + String(kv.key().c_str()) + "\":{\"name\":\"" + GetNameStatus(97, "0702", String(kv.key().c_str()).toInt(), "ZLinky_TIC") + "\",\"price\":" + getTarif(String(kv.key().c_str()).toInt()) + "}";
      result += sep + String(kv.key().c_str());
      i++;
    }
    JsonEuros += "}";
    result += F("],");
    // list name
    result += F("labels: [");

    result += F("],");
    result += F("barColors: ['#7d7d7d ','#2785c7','#00c967','#c9c600','#c96100', '#c90000','#00c6c9', '#a700c9', '#c90043','#373737'],");
    result += F("barWidth: '3px',");
    result += F("resize: true,");
    result += F("redraw: true,");
    result += F("stacked: true,");
    result += F("hoverCallback: function (index, options, content, row) {");
    result += F("return getLabelEnergy('");
    result += JsonEuros;
    result += F("',row);");
    result += F("}");
    result += F("});");
  }
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

void handleRoot(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADERGRAPH);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_ROOT);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
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

  String dashboard = "";
  String js = "";
  File root = LittleFS.open("/database");
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

          for (int i = 0; i < t.StateSize; i++)
          {
            if (t.e[i].visible)
            {
              if (String(t.e[i].typeJauge) == "gauge")
              {
                js += createGaugeDashboard((String)ShortAddr, (String)i, String(t.e[i].jaugeMin), String(t.e[i].jaugeMax), t.e[i].unit);
                js += CreateTimeGauge((String)ShortAddr + (String)i);
                js += "refreshGauge" + (String)ShortAddr + (String)i + "('" + tmp.substring(0, 16) + "'," + t.e[i].cluster + "," + t.e[i].attribute + ",'" + t.e[i].type + "'," + t.e[i].coefficient + ");";
                /*js+=F("loadGaugeDashboard('status_");
                js+=(String)ShortAddr;
                js+=F("','");
                js+=tmp.substring(0,16);
                js+=F("',");
                js+=t.e[i].cluster;
                js+=F(",");
                js+=t.e[i].attribute;
                js+=F(",'");
                js+=t.e[i].type;
                js+=F("',");
                js+=t.e[i].coefficient;
                js+=F(",");
                js+=String(t.e[i].jaugeMin);
                js+=F(",");
                js+=String(t.e[i].jaugeMax);
                js+=F(",'");
                js+=t.e[i].unit;
                js+=F("');");*/
              }
              else
              {
                dashboard += t.e[i].name;
                dashboard += " : ";
                dashboard += GetValueStatus(file.name(), t.e[i].cluster, t.e[i].attribute, (String)t.e[i].type, t.e[i].coefficient, (String)t.e[i].unit);
                dashboard += F("<br>");
              }
            }
          }
          dashboard += F("</div>");
          dashboard += F("<div id='action_");
          dashboard += (String)ShortAddr;
          dashboard += F("'>");
          // toutes les actions

          for (int i = 0; i < t.ActionSize; i++)
          {
            if (t.a[i].visible)
            {
              dashboard += F("<button onclick=\"ZigbeeAction(");
              dashboard += ShortAddr;
              dashboard += ",";
              dashboard += t.a[i].command;
              dashboard += ",";
              dashboard += t.a[i].value;
              dashboard += ");\" class='btn btn-primary mb-2'>";
              dashboard += t.a[i].name;
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
  result.replace("{{dashboard}}", dashboard);

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
  result.replace("{{javascript}}", javascript);

  request->send(200, "text/html", result);
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

  if (ConfigSettings.enableEthernet)
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
  }
  result.replace("{{ssidWifi}}", String(ConfigSettings.ssid));
  result.replace("{{ipWifi}}", ConfigSettings.ipAddressWiFi);
  result.replace("{{maskWifi}}", ConfigSettings.ipMaskWiFi);
  result.replace("{{GWWifi}}", ConfigSettings.ipGWWiFi);

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

  float val;
  float Voltage = 0.0;
  val = analogRead(35);
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
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
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

  String javascript = "";

  javascript = F("<script language='javascript'>");
  javascript += F("$(document).ready(function() {");
  javascript += createEnergyGraph(ConfigGeneral.ZLinky);
  if (time == "hour")
  {
    javascript += createPowerGraph(ConfigGeneral.ZLinky);
  }
  javascript += F("loadPowerGaugeAbo('");
  javascript += String(ConfigGeneral.ZLinky);
  javascript += F("','1295','");
  javascript += time;
  javascript += F("');");
  javascript += F("refreshStatusEnergy('");
  javascript += String(ConfigGeneral.ZLinky);
  javascript += F("','1295','");
  javascript += time;
  javascript += F("');");
  javascript += F("});");
  javascript += F("</script>");

  result.replace("{{javascript}}", javascript);

  request->send(200, "text/html", result);
}

void handleStatusDevices(AsyncWebServerRequest *request)
{
  String result;

  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h1>Status Devices</h1>");
  result += F("<h2>List of devices</h2>");
  result += F("<div class='row'>");

  String str = "";
  File root = LittleFS.open("/database");
  File file = root.openNextFile();
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
      char SAddr[4];
      int ShortAddr = GetShortAddr(file.name());
      sprintf(SAddr, "%04X", ShortAddr);
      result += SAddr;
      result += F("<br><strong>Device Id: </strong>");
      char devId[4];
      int DeviceId = GetDeviceId(file.name());
      sprintf(devId, "%04X", DeviceId);
      result += devId;
      result += F("<br><strong>Soft Version: </strong>");
      String SoftVer = GetSoftwareVersion(file.name());
      result += SoftVer;
      result += F("<br><strong>Last seen: </strong>");
      String lastseen = GetLastSeen(file.name());
      result += lastseen;

      // Get status and action from json

      if (TemplateExist(DeviceId))
      {
        Template t;
        t = GetTemplate(DeviceId, model);
        // toutes les propiétés
        result += F("<div id='status_");
        result += (String)ShortAddr;
        result += F("'>");

        for (int i = 0; i < t.StateSize; i++)
        {

          result += t.e[i].name;
          result += " : ";
          result += GetValueStatus(file.name(), t.e[i].cluster, t.e[i].attribute, (String)t.e[i].type, t.e[i].coefficient, (String)t.e[i].unit);
          result += F("<br>");
        }
        result += F("</div>");
        result += F("<div id='action_");
        result += (String)ShortAddr;
        result += F("'>");
        // toutes les actions
        for (int i = 0; i < t.ActionSize; i++)
        {

          result += F("<button onclick=\"ZigbeeAction(");
          result += ShortAddr;
          result += ",";
          result += t.a[i].command;
          result += ",";
          result += t.a[i].value;
          result += ");\" class='btn btn-primary mb-2'>";
          result += t.a[i].name;
          result += F("</button>");
        }
        result += F("</div>");
      }
      result += F("</div></div></div>");
    }

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

  result.replace("{{menu_config}}", FPSTR(HTTP_CONFIG_MENU));
  result.replace("{{menu_config_general}}", "disabled");
  result.replace("{{menu_config_horloge}}", "");
  result.replace("{{menu_config_linky}}", "");
  result.replace("{{menu_config_http}}", "");
  result.replace("{{menu_config_mqtt}}", "");
  result.replace("{{menu_config_notif}}", "");

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

  result.replace("{{menu_config}}", FPSTR(HTTP_CONFIG_MENU));
  result.replace("{{menu_config_general}}", "");
  result.replace("{{menu_config_horloge}}", "disabled");
  result.replace("{{menu_config_linky}}", "");
  result.replace("{{menu_config_http}}", "");
  result.replace("{{menu_config_mqtt}}", "");
  result.replace("{{menu_config_notif}}", "");

  result.replace("{{FormattedDate}}", FormattedDate);

  result.replace("{{ntpserver}}", ConfigGeneral.ntpserver);
  result.replace("{{timeoffset}}", String(ConfigGeneral.timeoffset));
  result.replace("{{timezone}}", String(ConfigGeneral.timezone));

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
  result.replace("{{menu_config}}", FPSTR(HTTP_CONFIG_MENU));
  result.replace("{{menu_config_general}}", "");
  result.replace("{{menu_config_horloge}}", "");
  result.replace("{{menu_config_linky}}", "");
  result.replace("{{menu_config_http}}", "");
  result.replace("{{menu_config_mqtt}}", "disabled");
  result.replace("{{menu_config_notif}}", "");

  result.replace("{{FormattedDate}}", FormattedDate);

  result.replace("{{servMQTT}}", String(ConfigGeneral.servMQTT));
  result.replace("{{portMQTT}}", String(ConfigGeneral.portMQTT));
  result.replace("{{userMQTT}}", String(ConfigGeneral.userMQTT));
  result.replace("{{passMQTT}}", String(ConfigGeneral.passMQTT));

  request->send(200, "text/html", result);
}

void handleConfigLinky(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_LINKY);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

  result.replace("{{menu_config}}", FPSTR(HTTP_CONFIG_MENU));
  result.replace("{{menu_config_general}}", "");
  result.replace("{{menu_config_horloge}}", "");
  result.replace("{{menu_config_linky}}", "disabled");
  result.replace("{{menu_config_http}}", "");
  result.replace("{{menu_config_mqtt}}", "");
  result.replace("{{menu_config_notif}}", "");

  result.replace("{{FormattedDate}}", FormattedDate);

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

void handleConfigNotification(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_NOTIFICATION);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

  result.replace("{{menu_config}}", FPSTR(HTTP_CONFIG_MENU));
  result.replace("{{menu_config_general}}", "");
  result.replace("{{menu_config_horloge}}", "");
  result.replace("{{menu_config_linky}}", "");
  result.replace("{{menu_config_http}}", "");
  result.replace("{{menu_config_mqtt}}", "");
  result.replace("{{menu_config_notif}}", "disabled");

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

  if (ConfigSettings.enableWiFi)
  {
    result.replace("{{checkedWiFi}}", "Checked");
  }
  else
  {
    result.replace("{{checkedWiFi}}", "");
  }
  result.replace("{{ssid}}", String(ConfigSettings.ssid));
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
  String result;

  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += FPSTR(HTTP_TOOLS);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

  request->send(200, F("text/html"), result);
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

void handleUpdate(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h1>Update ...</h1>");
  result += F("<div class='btn-group-vertical'>");
  result += F("<a href='/setchipid' class='btn btn-primary mb-2'>setChipId</button>");
  result += F("<a href='/setmodeprod' class='btn btn-primary mb-2'>setModeProd</button>");
  result += F("</div>");

  result = result + F("</body>");
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
  request->send(200, F("text/html"), result);
}

void handleSetchipid(AsyncWebServerRequest *request)
{
  check_chip_id();
  request->send(200, F("text/html"), "");
}

void handleSetmodeprod(AsyncWebServerRequest *request)
{
  setProdMode();
  request->send(200, F("text/html"), "");
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
  result += F("<button type='submit' class='btn btn-warning mb-2'>Save</button>");
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
  File root = LittleFS.open("/database");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      // tmp = tmp.substring(10);
      result += F("<li><a href='#' onClick=\"readfile('");
      result += tmp;
      result += F("','database');document.getElementById('actions').style.display='block';\">");
      result += tmp;
      result += F(" ( ");
      result += file.size();
      result += F(" o)</a></li>");
    }
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
  File root = LittleFS.open("/templates");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      // tmp = tmp.substring(11);
      result += F("<li><a href='#' onClick=\"readfile('");
      result += tmp;
      result += F("','templates');document.getElementById('actions').style.display = 'block';\">");
      result += tmp;
      result += F(" ( ");
      result += file.size();
      result += F(" o)</a></li>");
    }
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

    String filename = "/templates/" + request->arg(i);
    String content = request->arg(1);
    String action = request->arg(2);

    if (action == "save")
    {
      File file = LittleFS.open(filename, "w+");
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Failed to open file for reading\r\n"));
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
    response->addHeader(F("Location"), F("/templates"));
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
    File file = LittleFS.open(filename, "w+");
    if (!file || file.isDirectory())
    {
      DEBUG_PRINT(F("Failed to open file for reading\r\n"));
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
    String filename = "/database/" + request->arg(i);
    String content = request->arg(1);
    String action = request->arg(2);

    if (action == "save")
    {
      File file = LittleFS.open(filename, "w");
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Failed to open file for reading\r\n"));
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

void handleScanNetwork(AsyncWebServerRequest *request)
{
  String result = "";
  int n = WiFi.scanNetworks();

  if (n == 0)
  {
    result = " <label for='ssid'>SSID</label>";
    result += "<input class='form-control' id='ssid' type='text' name='WIFISSID' value='{{ssid}}'> <a onclick='scanNetwork();' class='btn btn-primary mb-2'>Scan</a><div id='networks'></div>";
  }
  else
  {

    result = "<select name='WIFISSID' onChange='updateSSID(this.value);'>";
    result += "<OPTION value=''>--Choose SSID--</OPTION>";
    for (int i = 0; i < n; ++i)
    {
      result += "<OPTION value='";
      result += WiFi.SSID(i);
      result += "'>";
      result += WiFi.SSID(i) + " (" + WiFi.RSSI(i) + ")";
      result += "</OPTION>";
    }
    result += "</select>";
  }
  request->send(200, F("text/html"), result);
}
void handleClearConsole(AsyncWebServerRequest *request)
{
  logClear();

  request->send(200, F("text/html"), "");
}

void handleGetVersion(AsyncWebServerRequest *request)
{
  commandList.push(Packet{0x0010, 0x0000, 0});
  request->send(200, F("text/html"), "");
}

void handleErasePDM(AsyncWebServerRequest *request)
{
  commandList.push(Packet{0x0012, 0x0000, 0});
  request->send(200, F("text/html"), "");
}

void handleStartNwk(AsyncWebServerRequest *request)
{
  commandList.push(Packet{0x0024, 0x0000, 0});
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
    commandList.push(trame);
    DEBUG_PRINTLN(F("add networkState"));
    commandList.push(Packet{0x0011, 0x0000, 0});
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

  commandList.push(trame);

  request->send(200, F("text/html"), "");
}

void handleReset(AsyncWebServerRequest *request)
{
  commandList.push(Packet{0x0011, 0x0000, 0});
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
  commandList.push(trame);

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
  commandList.push(trame);

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

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configHorloge"));
  request->send(response);
}

void handleSaveConfigLinky(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";
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
    strlcpy(ConfigGeneral.passMQTT, request->arg("passMQTT").c_str(), sizeof(ConfigGeneral.passMQTT));
    config_write(path, "passMQTT", String(request->arg("passMQTT")));
  }

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configMQTT"));
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
  if (!request->hasArg("WIFISSID"))
  {
    request->send(500, "text/plain", "BAD ARGS");
    return;
  }

  String StringConfig;
  String enableWiFi;
  if (request->arg("wifiEnable") == "on")
  {
    enableWiFi = "1";
  }
  else
  {
    enableWiFi = "0";
  }
  String ssid = request->arg("WIFISSID");
  String pass = request->arg("WIFIpassword");
  String ipAddress = request->arg("ipAddress");
  String ipMask = request->arg("ipMask");
  String ipGW = request->arg("ipGW");
  String tcpListenPort = request->arg("tcpListenPort");

  const char *path = "/config/configWifi.json";

  StringConfig = "{\"enableWiFi\":" + enableWiFi + ",\"ssid\":\"" + ssid + "\",\"pass\":\"" + pass + "\",\"ip\":\"" + ipAddress + "\",\"mask\":\"" + ipMask + "\",\"gw\":\"" + ipGW + "\",\"tcpListenPort\":\"" + tcpListenPort + "\"}";
  StaticJsonDocument<512> jsonBuffer;
  DynamicJsonDocument doc(1024);
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
  request->send(200, "text/html", "Save config OK ! <br><form method='GET' action='reboot'><input type='submit' name='reboot' value='Reboot'></form>");
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
  DynamicJsonDocument doc(1024);
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
  request->send(200, "text/html", "Save config OK ! <br><form method='GET' action='reboot'><input type='submit' name='reboot' value='Reboot'></form>");
}

void handleConfigDevices(AsyncWebServerRequest *request)
{
  String result;

  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h1>Config</h1>");
  result += F("<div align='right'>");
  result += F("<button type='button' onclick='cmd(\"PermitJoin\");' class='btn btn-primary'>Add Device</button> ");
  result += F("<button type='button' onclick='cmd(\"Reset\");' class='btn btn-primary'>Reset</button> ");
  result += F("</div><br>");
  result += F("<h2>List of devices</h2>");
  result += F("<div class='row'>");

  String str = "";
  File root = LittleFS.open("/database");
  File file = root.openNextFile();
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
      char SAddr[4];
      int ShortAddr = GetShortAddr(file.name());
      sprintf(SAddr, "%04X", ShortAddr);
      result += SAddr;
      result += F("<br><strong>Device Id: </strong>");
      char devId[4];
      int DeviceId = GetDeviceId(file.name());
      sprintf(devId, "%04X", DeviceId);
      result += devId;
      result += F("<br><strong>Soft Version: </strong>");
      String SoftVer = GetSoftwareVersion(file.name());
      result += SoftVer;
      result += F("<br><strong>Last seen: </strong>");
      String lastseen = GetLastSeen(file.name());
      result += lastseen;

      // Get status and action from json

      if (TemplateExist(DeviceId))
      {
        Template t;
        t = GetTemplate(DeviceId, model);
        // toutes les propiétés
        result += F("<div id='status_");
        result += (String)ShortAddr;
        result += F("'>");

        for (int i = 0; i < t.StateSize; i++)
        {

          result += t.e[i].name;
          result += " : ";
          result += GetValueStatus(file.name(), t.e[i].cluster, t.e[i].attribute, (String)t.e[i].type, t.e[i].coefficient, (String)t.e[i].unit);
          result += F("<br>");
        }
        result += F("</div>");
        result += F("<div id='action_");
        result += (String)ShortAddr;
        result += F("'>");
        // toutes les actions
        for (int i = 0; i < t.ActionSize; i++)
        {

          result += F("<button onclick=\"ZigbeeAction(");
          result += ShortAddr;
          result += ",";
          result += t.a[i].command;
          result += ",";
          result += t.a[i].value;
          result += ");\" class='btn btn-primary mb-2'>";
          result += t.a[i].name;
          result += F("</button>");
        }
        result += F("</div>");
        // Paramétrages
        result += F("<div>");
        result += F("<button onclick=\"ZigbeeSendRequest(");
        result += ShortAddr;
        result += ",";
        result += "0,5";
        result += ");\" class='btn btn-warning mb-2'>";
        result += "Config";
        result += F("</button>");

        result += F("</div>");
      }
      result += F("<button onclick=\"deleteDevice('");
      result += ShortAddr;
      result += "');\" class='btn btn-danger mb-2'>";
      result += "Delete";
      result += F("</button>");
      result += F("</div></div></div>");
    }

    file = root.openNextFile();
  }
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
  result = getPowerDatas(IEEE, "power", Attribute, "minute");

  request->send(200, F("application/json"), result);
}

void handleLoadEnergyChart(AsyncWebServerRequest *request)
{

  String IEEE, result, time;
  String sep = "";
  int i = 0;
  IEEE = request->arg(i);
  time = request->arg(1);

  String path = "/database/energy_" + IEEE + ".json";

  File DeviceFile = LittleFS.open(path, FILE_READ);
  if (!DeviceFile || DeviceFile.isDirectory())
  {
    DEBUG_PRINTLN(F("failed open"));
  }
  else
  {
    DynamicJsonDocument temp(MAXHEAP / 2);
    deserializeJson(temp, DeviceFile);
    JsonObject root = temp.as<JsonObject>();
    DeviceFile.close();

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
        result += sep + F("{\"y\":\"") + tmpi + F("H\",");
        int j = 0;
        String sep2 = "";
        for (JsonPair kv : root)
        {
          if (j > 0)
          {
            sep2 = ",";
          }
          result += sep2 + "\"" + String(kv.key().c_str()) + "\":" + root[kv.key().c_str()][time]["graph"][tmpi].as<String>();
          j++;
        }
        result += F("}");
        i++;
      }
    }
    else if (time == "day")
    {
      int now = Day.toInt();

      int i = 0;
      while (i < 30)
      {
        if (i > 0)
        {
          sep = ",";
        }
        now++;
        if (now > maxDayOfTheMonth[(Month.toInt() - 2)])
        {
          now = 1;
        }
        String tmpi = now < 10 ? "0" + String(now) : String(now);
        result += sep + F("{\"y\":\"") + tmpi + F("\",");
        int j = 0;
        String sep2 = "";
        for (JsonPair kv : root)
        {
          if (j > 0)
          {
            sep2 = ",";
          }
          result += sep2 + "\"" + String(kv.key().c_str()) + "\":" + root[kv.key().c_str()][time]["graph"][tmpi].as<String>();
          j++;
        }
        result += F("}");
        i++;
      }
    }
    else if (time == "month")
    {
      int now = Month.toInt();

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
        result += sep + F("{\"y\":\"") + tmpi + F("\",");
        int j = 0;
        String sep2 = "";
        for (JsonPair kv : root)
        {
          if (j > 0)
          {
            sep2 = ",";
          }
          result += sep2 + "\"" + String(kv.key().c_str()) + "\":" + root[kv.key().c_str()][time]["graph"][tmpi].as<String>();
          j++;
        }
        result += F("}");
        i++;
      }
    }
    else if (time == "year")
    {
      int now = Year.toInt() - 10;

      int i = 0;
      while (i < 11)
      {
        if (i > 0)
        {
          sep = ",";
        }

        result += sep + F("{\"y\":\"") + String(now) + F("\",");
        int j = 0;
        String sep2 = "";
        for (JsonPair kv : root)
        {
          if (j > 0)
          {
            sep2 = ",";
          }
          result += sep2 + "\"" + String(kv.key().c_str()) + "\":" + root[kv.key().c_str()][time]["graph"][String(now)].as<String>();
          j++;
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

  if (!alertList.isEmpty())
  {
    Alert a = alertList.shift();
    result = String(a.state);
    result += F(";");
    result += a.message;
    DEBUG_PRINTLN(result);
  }

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
  String filename = "/database/" + tmpMac + ".json";
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

  File root = LittleFS.open("/database");
  File filedevice = root.openNextFile();
  result = "[";
  int i = 0;
  while (filedevice)
  {

    String inifile = filedevice.name();
    File file = LittleFS.open("/database/" + inifile, FILE_READ);
    if (!file || file.isDirectory())
    {
      DEBUG_PRINTLN(F("Erreur lors de l'ouverture du fichier ini_read"));
      file.close();
    }
    size_t filesize = file.size();
    if (filesize > 0)
    {
      if (i > 0)
      {
        result += ",";
      }
      while (file.available())
      {

        result += (char)file.read();
      }

      i++;
    }
    file.close();
    filedevice = root.openNextFile();
  }
  result += "]";
  filedevice.close();
  root.close();

  request->send(200, F("application/json"), result);
}

void initWebServer()
{

  // serverWeb.on("/", handleRoot);
  serverWeb.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleRoot(request); });
  serverWeb.on("/dashboard", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleDashboard(request); });
  serverWeb.on("/statusEnergy", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleStatusEnergy(request); });
  serverWeb.on("/statusNetwork", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleStatusNetwork(request); });
  serverWeb.on("/statusDevices", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleStatusDevices(request); });
  serverWeb.on("/configGeneral", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleConfigGeneral(request); });
  serverWeb.on("/configZigbee", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleConfigZigbee(request); });
  serverWeb.on("/configHorloge", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleConfigHorloge(request); });
  serverWeb.on("/configLinky", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleConfigLinky(request); });
  serverWeb.on("/configMQTT", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleConfigMQTT(request); });
  serverWeb.on("/configNotif", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleConfigNotification(request); });
  serverWeb.on("/configWiFi", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleConfigWifi(request); });
  serverWeb.on("/configEthernet", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleConfigEthernet(request); });
  serverWeb.on("/configDevices", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleConfigDevices(request); });

  serverWeb.on("/saveFileConfig", HTTP_POST, [](AsyncWebServerRequest *request)
               { handleSaveConfig(request); });
  serverWeb.on("/saveFileTemplates", HTTP_POST, [](AsyncWebServerRequest *request)
               { handleSaveTemplates(request); });
  serverWeb.on("/saveFileJavascript", HTTP_POST, [](AsyncWebServerRequest *request)
               { handleSaveJavascript(request); });
  serverWeb.on("/saveFileDatabase", HTTP_POST, [](AsyncWebServerRequest *request)
               { handleSaveDatabase(request); });
  serverWeb.on("/saveConfigGeneral", HTTP_POST, [](AsyncWebServerRequest *request)
               { handleSaveConfigGeneral(request); });
  serverWeb.on("/saveConfigHorloge", HTTP_POST, [](AsyncWebServerRequest *request)
               { handleSaveConfigHorloge(request); });
  serverWeb.on("/saveConfigLinky", HTTP_POST, [](AsyncWebServerRequest *request)
               { handleSaveConfigLinky(request); });
  serverWeb.on("/saveConfigMQTT", HTTP_POST, [](AsyncWebServerRequest *request)
               { handleSaveConfigMQTT(request); });
  serverWeb.on("/saveConfigNotification", HTTP_POST, [](AsyncWebServerRequest *request)
               { handleSaveConfigNotification(request); });
  serverWeb.on("/saveWifi", HTTP_POST, [](AsyncWebServerRequest *request)
               { handleSaveWifi(request); });
  serverWeb.on("/saveEther", HTTP_POST, [](AsyncWebServerRequest *request)
               { handleSaveEther(request); });
  // serverWeb.on("/tools", handleTools);
  serverWeb.on("/tools", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleTools(request); });
  // serverWeb.on("/logs", handleLogs);
  serverWeb.on("/logs", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleLogs(request); });
  // serverWeb.on("/reboot", handleReboot);
  serverWeb.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleReboot(request); });
  // serverWeb.on("/update", handleUpdate);
  serverWeb.on("/update", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleUpdate(request); });
  // serverWeb.on("/readFile", handleReadfile);
  serverWeb.on("/readFile", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleReadfile(request); });

  // serverWeb.on("/getLogBuffer", handleLogBuffer);
  serverWeb.on("/getLogBuffer", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleLogBuffer(request); });
  // serverWeb.on("/scanNetwork", handleScanNetwork);
  serverWeb.on("/scanNetwork", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleScanNetwork(request); });
  // serverWeb.on("/cmdClearConsole", handleClearConsole);
  serverWeb.on("/cmdClearConsole", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleClearConsole(request); });
  // serverWeb.on("/cmdGetVersion", handleGetVersion);
  serverWeb.on("/cmdGetVersion", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleGetVersion(request); });
  // serverWeb.on("/cmdErasePDM", handleErasePDM);
  serverWeb.on("/cmdErasePDM", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleErasePDM(request); });
  serverWeb.on("/cmdStartNwk", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleStartNwk(request); });
  serverWeb.on("/cmdSetChannelMask", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleSetChannelMask(request); });
  // serverWeb.on("/cmdPermitJoin", handlePermitJoin);
  serverWeb.on("/cmdPermitJoin", HTTP_GET, [](AsyncWebServerRequest *request)
               { handlePermitJoin(request); });
  // serverWeb.on("/cmdRawMode", handleRawMode);
  serverWeb.on("/cmdRawMode", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleRawMode(request); });
  // serverWeb.on("/cmdRawModeOff", handleRawModeOff);
  serverWeb.on("/cmdRawModeOff", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleRawModeOff(request); });
  // serverWeb.on("/cmdActiveReq", handleActiveReq);
  serverWeb.on("/cmdActiveReq", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleActiveReq(request); });
  // serverWeb.on("/cmdReset", handleReset);
  serverWeb.on("/cmdReset", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleReset(request); });
  // serverWeb.on("/setchipid", handleSetchipid);
  serverWeb.on("/setchipid", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleSetchipid(request); });
  // serverWeb.on("/setmodeprod", handleSetmodeprod);
  serverWeb.on("/setmodeprod", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleSetmodeprod(request); });
  // serverWeb.on("/fsbrowser", handleFSbrowser);
  serverWeb.on("/configFiles", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleConfigFiles(request); });
  serverWeb.on("/fsbrowser", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleFSbrowser(request); });
  serverWeb.on("/templates", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleTemplates(request); });
  serverWeb.on("/javascript", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleJavascript(request); });
  serverWeb.on("/createTemplate", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleCreateTemplate(request); });
  serverWeb.on("/ZigbeeAction", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleZigbeeAction(request); });
  serverWeb.on("/ZigbeeSendRequest", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleZigbeeSendRequest(request); });
  serverWeb.on("/loadLinkyDatas", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleLoadLinkyDatas(request); });
  serverWeb.on("/loadGaugeDashboard", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleLoadGaugeDashboard(request); });
  serverWeb.on("/loadPowerGaugeAbo", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleLoadPowerGaugeAbo(request); });
  serverWeb.on("/loadPowerGaugeTimeDay", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleLoadPowerGaugeTimeDay(request); });
  serverWeb.on("/refreshGaugeAbo", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleRefreshGaugeAbo(request); });
  serverWeb.on("/loadPowerTrend", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleLoadPowerTrend(request); });
  serverWeb.on("/loadPowerChart", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleLoadPowerChart(request); });
  serverWeb.on("/loadEnergyChart", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleLoadEnergyChart(request); });
  serverWeb.on("/loadTrendEnergyEuros", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleLoadTrendEnergyEuros(request); });
  serverWeb.on("/loadLabelEnergy", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleLoadLabelEnergy(request); });
  serverWeb.on("/deleteDevice", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleDeleteDevice(request); });
  serverWeb.on("/getAlert", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleGetAlert(request); });
  serverWeb.on("/getFormattedDate", HTTP_GET, [](AsyncWebServerRequest *request)
               { handleGetFormattedDate(request); });

  serverWeb.on("/getDevices", HTTP_GET, [](AsyncWebServerRequest *request)
               {
     if(!request->authenticate("admin", "admin"))
        return request->requestAuthentication();
    APIgetDevices(request); });

  serverWeb.serveStatic("/web/js/jquery-min.js", LittleFS, "/web/js/jquery-min.js");
  serverWeb.serveStatic("/web/js/functions.js", LittleFS, "/web/js/functions.js");
  serverWeb.serveStatic("/web/js/raphael-min.js", LittleFS, "/web/js/raphael-min.js");
  serverWeb.serveStatic("/web/js/morris.min.js", LittleFS, "/web/js/morris.min.js");
  serverWeb.serveStatic("/web/js/justgage.min.js", LittleFS, "/web/js/justgage.min.js");
  serverWeb.serveStatic("/web/js/justgage.min.js.map", LittleFS, "/web/js/justgage.min.js.map");
  serverWeb.serveStatic("/web/js/bootstrap.min.js", LittleFS, "/web/js/bootstrap.min.js");
  serverWeb.serveStatic("/web/js/bootstrap.bundle.min.js.map", LittleFS, "/web/js/bootstrap.bundle.min.js.map");
  serverWeb.serveStatic("/web/css/bootstrap.min.css", LittleFS, "/web/css/bootstrap.min.css");
  serverWeb.serveStatic("/web/css/style.css", LittleFS, "/web/css/style.css");
  serverWeb.serveStatic("/web/img/logo.png", LittleFS, "/web/img/logo.png");
  serverWeb.serveStatic("/web/img/wait.gif", LittleFS, "/web/img/wait.gif");
  serverWeb.serveStatic("/web/img/", LittleFS, "/web/img/");
  serverWeb.onNotFound(handleNotFound);

  serverWeb.begin();
}
