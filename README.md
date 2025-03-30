# LiXee-Gateway
A multi protocols gateway for zigbee devices 

## Hardware compatibility
This application can be used with :  
* [LiXee-ZiWifi32 Lite](https://lixee.fr/produits/41-lixee-ziwifi32-3770014375162.html)  (WiFi only)
  The device is based on :
  * ESP32-S3-WROOM-N16R8 (PSRAM: 8Mo Flash: 16Mo)
  * JN5189 with [ZiGate v2 firmware](https://github.com/fairecasoimeme/ZiGatev2)
<table><tr><td><img src="https://github.com/fairecasoimeme/LiXee-Gateway/blob/master/doc/LiXee_ZiWiFi32_face.png" width="480"></td>
<td><img src="https://github.com/fairecasoimeme/LiXee-Gateway/blob/master/doc/LiXee_ZiWiFi32_pile.png" width="480">  </td></tr></table>
      
Obviously, you can use this code with **ESP32S3** chip but depend on your board pins connexions. 

## Operating diagram

<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/LiXee-Box_Schema.png" width="1024">  

## Uses cases example

Here is a list of some use cases which can be used with **LiXee-Gateway**.  

### Use case n°1
Your Linky counter is very far and Zigbee protocol is too light to transport datas to the coordinator.
**Linky (ZLinky) <--> Zigbee <--> LiXee-Gateway <--> WiFi <--> MQTT <--> Home-Assistant / Jeedom / Domoticz / other**

### Use case n°2
You need to relay zigbee devices datas to the cloud (with Web API)

## Features
The main feature is to relay Zigbee device datas to a website or a MQTT service

The device can be configured with a local website

You can :  
* Monitor :
  * Your zigbee devices with :
  	* gauge and dashboard
   	* all properties table	 
  * Your network status and gateway features
* Manage ZigBee devices
  * Object creation
  * Template creation
  * Status and actions management
* MQTT API management
  * Server / Port / user / password /topic header customisable
  * MQTT discover available (Home Assistant)
* WebPush API management
  * Url / user / password
  * POST method with JSON format
* Marstek CT001 Emulation
  * Can communicate with Marstek battery to enhance energy management.
* Rules management
  * You can create rules to :
  	* act when a zigbee device reach a threshold.
  	* do load shedding
  	* route energy
* Update OTA


## First start  

1. Plug the device on a USB power.
2. Use a mobile/computer and scan the WiFi
3. Connect to the SSID : **LIXEEGW-XXXX** (XXXX correspond to a part of unique @MAC)
4. By default, the password is : **adminXXXX** (XXXX correspond to the ssid XXXX)
5. Connected to the WiFi SSID, you can open a web navigator and type in URL label : `http://lixee-gw`
6. Normally, you will redirect to the WiFi config page
7. Scan your SSID WiFi box and fill the form to complete configuration
8. Reboot the device

## Pair a device

1. With your web navigator, you can go to `Network` menu then `Zigbee`  
2. You can click on `Add Device` button. The ZiGate coordinator begin the `Permit Join` procedure for 30 seconds.   
3. The blue LED of your device blink slowly.  
4. Now, you can execute the pairing device procedure.
   
⚠️ **If a device is paired, a green alert appears. You can refresh to see devices properties.**  
  
<img src="https://github.com/fairecasoimeme/LiXee-Gateway/blob/master/doc/screenshots/LiXee-GW_config_zigbee_devices.png" width="800">  

## How to template a new zigbee device

A template file is a JSON structure which give status and actions to a device type. The name of the template file corresponds to the device identification (decimal).
When a Zigbee device is joining, **LiXee-Gateway** create an object following the corresponding template with status and actions, binding and configure reporting if it is necessary.

### Structure
Here is the structure :

    ├── Device model or 'default'    
    │   ├── status   
    │   │   ├── name   
    │   │   ├── cluster  
    │   │   ├── attribut  
    │   │   ├── type   
    │   │   ├── unit  
    │   │   ├── coefficient   
    │   │   ├── visible  
    │   │   ├── jauge     
    │   │   ├── min  
    │   │   ├── max  
    │   │   ├── poll  
    │   │   ├── mqtt_device_class  
    │   │   ├── mqtt_state_class  
    │   │   ├── mqtt_icon 
    │   ├── action   
    │   │   ├── name   
    │   │   ├── command  
    │   │   ├── endpoint  
    │   │   ├── value    
    │   │   ├── visible   
    │   ├── bind  
    │   ├── report  
    │   │   ├── cluster   
    │   │   ├── attribut  
    │   │   ├── type  
    │   │   ├── min   
    │   │   ├── max  
    │   │   ├── timeout   
    │   │   ├── change  

You can find Template examples in `data/tp` directory  
Example of 24321.json device id (5F01 Hex) :
```json
{
	"lumi.sensor_switch.aq2" : [
	{
		"status" : [
			{
				"name" : "Clic",
				"cluster" : "0006",
				"attribut" : 0
			},
			{
				"name" : "MultiClic",
				"cluster" : "0000",
				"attribut" : 32768
			}
		]
	}
       ],
        "default" : [
	{
		"status" : [
			{
				"name" : "Clic",
				"cluster" : "0012",
				"attribut" : 85
			},
			{
				"name" : "MultiClic",
				"cluster" : "0012",
				"attribut" : 1293
			}
		]
	}
	]
}
```

### Status

|Command|Mandatory|Type|Value|Comment|
|-------|---------|----|-----|-------|			
|name|x|String||string character|   
|cluster|x|String||cluster id in hexadecimal|  
|attribut|x|Decimal||attribute number in decimal|  
|type| |String|"numeric","float"| only if you want manage numeric value| 
|unit| |String|""| only for numeric type| 
|coefficient| |float|| only for numeric type| 
|jauge| |String|"Gauge" / "Battery" / "text" | only for numeric type| 
|min| |Decimal|| only for jauge = "Gauge"| 
|max| |Decimal|| only for jauge = "Gauge"| 
|visible| |Decimal|1 or 0| only if you want to display on dashboard| 
|poll| |Decimal|| number of seconds if you want to poll the device| 
|mqtt_device_class| |String|| could be "energy", "power", "apparent_power" (see mqtt discover HA webpage)| 
|mqtt_state_class| |String|| could be "total_increasing", "measurement" (see mqtt discover HA webpage)| 
|mqtt_icon| |String|| could be "transmission-tower", "lightning-bolt" (see mqtt discover HA webpage)| 


### Action

|Command|Mandatory|Type|Value|Comment|
|-------|---------|----|-----|-------|			
|name|x|String||string character|   
|command|x|Decimal|146|command id in decimal value|  
|endpoint|x|Decimal||endpoint number in decimal|  
|value|x|Decimal||value sent in decimal| 
|visible| |Decimal|1 or 0| only if you want to display button on dashboard| 

### Bind
List of cluster(in numeric) which will be binded
example : `bind : "1026;1029;1794"`

### Report

|Command|Mandatory|Type|Value|Comment|
|-------|---------|----|-----|-------|			
|cluster|x|String||cluster id in hexadecimal|  
|attribut|x|Decimal||attribute number in decimal|  
|type|x|Decimal|| Correspond to the numeric type of attribut| 
|min|x|Decimal||min time (in second) to send report| 
|max|x|Decimal||max time (in second) to send report| 
|timeout| |Decimal|| in millisecond| 
|change| |Decimal|| change value to send report| 


## How to create rules

For the moment, you have to edit a json file to create / modify / delete rule.  
For the moment, you can create only 10 rules.  
One rule can contains one or more conditions.  
One rule can contains one or more actions.  

### Structure
Here is the structure :  

    ├── Rule     
    │   ├── name   
    │   ├── conditions   
    │   │   ├── type   
    │   │   ├── IEEE   
    │   │   ├── cluster  
    │   │   ├── attribut  
    │   │   ├── operator   
    │   │   ├── value  
    │   │   ├── logic  
    │   ├── actions   
    │   │   ├── type   
    │   │   ├── IEEE  
    │   │   ├── endpoint  
    │   │   ├── value    

### Conditions

|Command|Mandatory|Type|Value|Comment|
|-------|---------|----|-----|-------|			
|type|x|String|"device"||   
|IEEE|x|String||@mac without ':' or '-'|  
|cluster|x|Decimal||cluster id in decimal|  
|attribut|x|Decimal||attribute number in decimal|  
|operator|x|String|"<",">","==","!=",">=","<="|| 
|value|x|Decimal|| | 
|logic| |String|"AND","OR"|only needed if there are more than one condition| 

### Actions

|Command|Mandatory|Type|Value|Comment|
|-------|---------|----|-----|-------|			
|type|x|String|"onoff"||   
|IEEE|x|String||@mac without ':' or '-'|  
|endpoint|x|Decimal||endpoint id in decimal|  
|value|x|String|||  

For example :  

```json 
{
   "rules":[
    {
        "name":"rule_1",
        "conditions" : [
		{
		   "type" : "device",
		   "IEEE" : "00158d0006204fcf",
		   "cluster" : 2820,
		   "attribute" : 1295,
		   "operator" : "<",
		   "value" : 1000,
		   "logic" : "AND"
		}
        ],
        "actions" : [
		{
		   "type" : "onoff",
		   "IEEE" : "a4c138bb23185d2c",
                   "endpoint":1,
		   "value": "1"
		}
        ]
    }, {
        "name":"rule_2",
        "conditions" : [
		{
		   "type" : "device",
		   "IEEE" : "00158d0006204fcf",
		   "cluster" : 2820,
		   "attribute" : 1295,
		   "operator" : ">",
		   "value" : 1000,
		   "logic" : "AND"
		}
        ],
        "actions" : [
		{
		   "type" : "onoff",
		   "IEEE" : "a4c138bb23185d2c",
                   "endpoint":1,
		   "value": "1"
		}
        ]
    }
   ]
}
```

## WEB API

To access to the API command, go to http://<HOST>/<command>

### Commands List

* [getSystem](#getSystem)
* [getDevices](#getDevices)
* [getDevice?id=IEEE](#getDevice)
* [getLinky](#getLinky)
* [getTemplates](#getTemplates)


### Methods

#### getSystem

##### Request
```bash
curl -X GET 'https://<HOST>/getSystem'\
    -u <username>:<password> \
    -H 'Content-Type: application/json' \
```
##### Response
```json
{
  "network": {
    "wifi": {
      "enable": 1,
      "connected": 1,
      "mode": 0,
      "ip": "192.168.0.144",
      "netmask": "255.255.255.0",
      "gateway": "192.168.0.254"
    }
  },
  "system": {
    "mqtt": {
      "enable": 1,
      "connected": 1,
      "url": "192.168.0.21",
      "port": 1883
    },
    "webpush": {
      "enable": 0,
      "auth": 0,
      "url": ""
    },
    "marstek": {
      "enable": 1,
      "connected": 0,
      "ip": ""
    },
    "infos": {
      "t": 48.1
    }
  }
}
```
#### getDevices

##### Request
```bash
curl -X GET 'https://<HOST>/getDevices'\
    -u <username>:<password> \
    -H 'Content-Type: application/json' \
```
##### Response
```json
{
  "00158d0006203a63": {
    "1": {
      "IN": "0,1,3,1026,1794",
      "OUT": "4,3,1794"
    },
    "INFO": {
      "shortAddr": "38694",
      "LQI": "66",
      "device_id": "263",
      "lastSeen": "2025-03-14 14:40",
      "Status": "00",
      "manufacturer": "LiXee",
      "model": "ZiPulses",
      "software_version": "4000-0008"
    },
    "0702": {
      "0": "000000000036"
    },
    "0402": {
      "0": "09F8"
    },
    "0001": {
      "32": "23",
      "33": "C8"
    }
  }
}
```
#### getDevice

##### Request
```bash
curl -X GET 'https://<HOST>/getDevice?id=04cf8cdf3c79ce2b'\
    -u <username>:<password> \
    -H 'Content-Type: application/json' \
```
##### Response
```json
{
  "04cf8cdf3c79ce2b": {
    "1": {
      "IN": "0,2,3,4,5,6,9,1794,2820",
      "OUT": "10,25"
    },
    "242": {
      "IN": "",
      "OUT": "33"
    },
    "INFO": {
      "shortAddr": "5561",
      "LQI": "5C",
      "device_id": "97",
      "lastSeen": "2025-03-13 19:30",
      "Status": "00",
      "manufacturer": "LUMI",
      "model": "lumi.plug.maeu01",
      "software_version": "22"
    }
  }
}
```

#### getLinky

##### Request
```bash
curl -X GET 'https://<HOST>/getLinky'\
    -u <username>:<password> \
    -H 'Content-Type: application/json' \
```
##### Response
```json
{
  "65382_768": 0,
  "1794_0": 28870881,
  "1794_256": 28870881,
  "1794_258": 0,
  "1794_260": 0,
  "1794_262": 0,
  "1794_264": 0,
  "1794_266": 0,
  "2820_1295": 1560,
  "2820_1293": 0,
  "1794_32": "TH..",
  "1794_776": "022161823588",
  "2817_13": 45,
  "2817_14": 0,
  "2820_1288": 6,
  "2820_1290": 90,
  "65382_0": "BASE",
  "65382_1": "",
  "65382_2": "00",
  "65382_3": 0,
  "65382_4": 0,
  "65382_5": 0
}
```
#### getTemplates

##### Request
```bash
curl -X GET 'https://<HOST>/getTemplates'\
    -u <username>:<password> \
    -H 'Content-Type: application/json' \
```
##### Response
```json
{
  "24321.json": {
    "lumi.sensor_switch.aq2": [
      {
        "status": [
          {
            "name": "Clic",
            "cluster": "0006",
            "attribut": 0
          },
          {
            "name": "MultiClic",
            "cluster": "0000",
            "attribut": 32768
          }
        ]
      }
    ],
    "default": [
      {
        "status": [
          {
            "name": "Clic",
            "cluster": "0012",
            "attribut": 85
          },
          {
            "name": "MultiClic",
            "cluster": "0012",
            "attribut": 1293
          }
        ]
      }
    ]
  },.........
}
```

## How to flash release
Just install esptools and run this command

### Windows

```bash
esptool.py.exe --chip esp32 --port "COMXX" \
	 --baud 460800 \
	 --before default_reset --after hard_reset write_flash -z \
	 --flash_mode dio --flash_freq 40m --flash_size 16MB \
	 0x1000 bootloader.bin \
	 0x8000 partitions.bin \
	 0xe000 boot_app0.bin \
	 0x10000 firmware.bin
```

## Home assistant MQTT discover

**LiXee-Gateway** is compatible with the MQTT discover from HA.

Just go to **Gateway** menu --> **MQTT** and enable the feature

<img src="https://github.com/fairecasoimeme/LiXee-Gateway/blob/master/doc/screenshots/LiXee-GW_config_MQTT.png" width="800">  

Fill the form :
  * MQTT server
  * MQTT port
  * MQTT username
  * MQTT password

Then click to **Home-Assistant** and **Save**  
Wait a moment and if all it's good, the connected icon will change to green.

Then go to **Network** menu --> **Zigbee**

For each Zigbee devices, a new button **MQTT Discover** appear. Please click on it to create a new device on HA. And that's it.
<div align='center'><img src="https://github.com/fairecasoimeme/LiXee-Gateway/blob/master/doc/screenshots/LiXee-GW_Devices_mqtt_discover.png" width="320">  </div>

Wait a moment and go to your HA MQTT devices:
<img src="https://github.com/fairecasoimeme/LiXee-Gateway/blob/master/doc/screenshots/HA_Create_MQTT_device.png" width="1024">  

<img src="https://github.com/fairecasoimeme/LiXee-Gateway/blob/master/doc/screenshots/HA_MQTT_device_entities.png" width="800">

## Screenshots

### Dashboard
<img src="https://github.com/fairecasoimeme/LiXee-Gateway/blob/master/doc/screenshots/LiXee-GW_dashboard.png" width="800">  

### Devices status
<img src="https://github.com/fairecasoimeme/LiXee-Gateway/blob/master/doc/screenshots/LiXee-GW_status_devices.png" width="800">  

### Config Wifi
<img src="https://github.com/fairecasoimeme/LiXee-Gateway/blob/master/doc/screenshots/LiXee-GW_config_wifi.png" width="800">  

### Config Zigbee
<img src="https://github.com/fairecasoimeme/LiXee-Gateway/blob/master/doc/screenshots/LiXee-GW_config_zigbee.png" width="800">  

### Config MQTT
<img src="https://github.com/fairecasoimeme/LiXee-Gateway/blob/master/doc/screenshots/LiXee-GW_config_MQTT.png" width="800">  

### Config WebPush
<img src="https://github.com/fairecasoimeme/LiXee-Gateway/blob/master/doc/screenshots/LiXee-GW_config_webpush.png" width="800">  

### Rules
<img src="https://github.com/fairecasoimeme/LiXee-Gateway/blob/master/doc/screenshots/LiXee-GW_rules.png" width="800">  

### Advanced tools
<img src="https://github.com/fairecasoimeme/LiXee-Gateway/blob/master/doc/screenshots/LiXee-GW_tools.png" width="800">  

### OTA update
<img src="https://github.com/fairecasoimeme/LiXee-Gateway/blob/master/doc/screenshots/LiXee-GW_update.png" width="800">  

### Mobile responsive web page
<div align='center'><img src="https://github.com/fairecasoimeme/LiXee-Gateway/blob/master/doc/screenshots/LiXee-GW_mobile_devices.png" width="400">  </div>

## Credits

Thanks to all authors of 3rd party libraries which are used in this project:

* [espressif / arduino-esp32](https://github.com/espressif/arduino-esp32)
* [rlogiacco/CircularBuffer](https://github.com/rlogiacco/CircularBuffer)
* [bblanchon/ArduinoJson](https://github.com/bblanchon/ArduinoJson)
* [paulstoffregen/Time](https://github.com/PaulStoffregen/Time)
* [marvinroger/AsyncMqttClient](https://github.com/marvinroger/async-mqtt-client)
* [arkhipenko/TaskScheduler](https://github.com/arkhipenko/TaskScheduler)
* [me-no-dev/AsyncTCP](https://github.com/me-no-dev/AsyncTCP)
* [me-no-dev/ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)

Thanks to [ZigStar](https://github.com/mercenaruss) for the update OTA

## Changelog

### V1.2
* Add mDNS service for lixee-assist
* Add api to set WiFi
* Add poll webpage
* Fix wifi enable bug
* Fix Scan Wifi network
* Fix Json file load to PSRAM
* Reboot when wifi is configured
* Fix memory leak
* Enhance memory use

### V1.1
* Add API commands
* Add Marstek compatibility
* Add Rules capacity
* Add Dashboard feature
* Fix HA MQTT discovery
* Fix files management
* Optimize some treatment
* Fix some bugs

### V1.0 (initial stable version)
#### Initial stable version

* Network Status
* Devices Status
* Config WiFi
* Config Zigbee
* MQTT gateway
* WebPush API gateway
* NTP client
* HTTP Security
* Backup / Restore
* Update

#### Compatible devices
* ZLinky
* ZiPulses
* Power socket
* All other (need template update)


### V1.0a
* Initial source 
  
