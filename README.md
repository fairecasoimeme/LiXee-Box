# LiXee-Box

## Description

**LiXee-Box** is a multi-protocol gateway for Zigbee devices, designed to be a central hub for energy management and home automation. This application transforms your LiXee-ZiWifi32 into a complete gateway capable of managing your Zigbee devices, your Linky, production, gaz, water meter, and integrating everything into your home automation system.

## Compatible Hardware

This application works with:

- **[LiXee-ZiWifi32 Lite](https://lixee.fr/produits/41-lixee-ziwifi32-3770014375162.html)** (WiFi only)
  - Based on ESP32-S3-WROOM-N16R8 (PSRAM: 8MB Flash: 16MB)
  - Equipped with JN5189 module running [ZiGate v2 firmware](https://github.com/fairecasoimeme/ZiGatev2)

> **Note**: You can also use this code with other ESP32S3 boards, depending on your board's pin connections.

<table><tr><td><img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/LiXee_ZiWiFi32_face.png" width="480"></td>
<td><img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/LiXee_ZiWiFi32_pile.png" width="480">  </td></tr></table>
      
## Operating diagram

<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/LiXee-Box_Schema.png" width="1024">  

## Typical Use Cases

- **Long-distance relay**: Linky (ZLinky) ↔ Zigbee ↔ LiXee-Box ↔ WiFi ↔ MQTT ↔ Home-Assistant/Jeedom/Domoticz
- **Cloud gateway**: Relay Zigbee device data to web services via API
- **Advanced energy management**: Monitoring, load shedding, energy routing

## ✨ Main Features
The main feature is to relay Zigbee device datas to a website or a MQTT service

The device can be configured with a local website

### 🔧 Zigbee Device Management
- Creation and management of Zigbee objects
- Customizable templates for different device types
- Status and action management
- Historical data for power and energy devices

### 📊 Monitoring and Dashboard
- Energy dashboard with gauges and charts
- Real-time consumption monitoring
- Trend and historical graphs
- Integrated Linky data

### 🌐 Connectivity
- **MQTT**: Customizable server/port/user/password
- **MQTT Discovery** compatible with Home Assistant
- **WebPush API**: URL/user/password
- **Marstek CT001 Emulation** for battery management

### ⚡ Energy Management
- Automated rules for load shedding and energy routing
- Configurable thresholds with automatic actions
- Production management and energy distribution
- Tariff management for energy, production, gas, and water

### 🔄 Updates and Maintenance
- OTA (Over-The-Air) automatic and manual updates
- Configuration backup/restore
- Developer mode for debugging

## 📱 User Interface

### Firmware Update
![Firmware Update](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_Update.png)

The update interface allows you to keep your LiXee-Box up to date with the latest features.

### Device Pairing
![Device Pairing](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_AssistDevice_p1.png)
![LiXee-Box Pairing](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_AssistDevice_p2.png)
![Device Search](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_AssistDevice_p3.png)

The pairing process is simplified with a step-by-step wizard to connect your Zigbee devices.

### Device Management
![Zigbee Device Configuration](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_config_zigbee.png)
![Device Status](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_DeviceStatus.png)

Complete interface to configure and monitor all your connected Zigbee devices.

### Energy Dashboard
![Energy Status](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_Energy_1.png)
![Detailed Charts](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_Energy_2.png)
![Mobile Interface](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_Mobile_energy.png)

Complete dashboard with real-time visualization of your energy consumption, historical charts, and responsive mobile interface.

## 🚀 Installation and Configuration

### First Setup

With web navigator
1. **Power**: Plug the device into a USB power supply
2. **WiFi**: Scan available WiFi networks with your mobile/computer
3. **Connection**: Connect to SSID `LIXEEGW-XXXX` (XXXX = part of MAC address)
4. **Authentication**: Default password `adminXXXX` (XXXX = SSID suffix)
5. **Configuration**: Open `http://lixee-gw` in your browser
6. **Home WiFi**: Configure your main WiFi network
7. **Reboot**: Device restarts and connects to your network

with **LiXee-Assist** https://github.com/fairecasoimeme/LiXee-Assist  

<a href="https://play.google.com/store/apps/details?id=fr.lixee.assist">
  <img src="https://play.google.com/intl/en_us/badges/static/images/badges/en_badge_web_generic.png" 
       alt="Get it on Google Play" 
       style="height: 50px;"/>
</a>


### Zigbee Configuration))

1. Go to **Network** menu → **Zigbee**
2. Click **Add Device** to start pairing (30 seconds)
3. Blue LED blinks slowly during pairing
4. Execute the pairing procedure on your Zigbee device
   
⚠️ **If a device is paired, a green alert appears. You can refresh to see devices properties.**  
  
## How to template a new zigbee device

A template file is a JSON structure which give status and actions to a device type. The name of the template file corresponds to the device identification (decimal).
When a Zigbee device is joining, **LiXee-Box** create an object following the corresponding template with status and actions, binding and configure reporting if it is necessary.

### Template Structure
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

### Status Parameters

| Parameter | Mandatory | Type | Description |
|-----------|-----------|------|-------------|
| `name` | ✓ | String | Display name |
| `cluster` | ✓ | String | Cluster ID (hex) |
| `attribut` | ✓ | Decimal | Attribute number |
| `type` | | String | "numeric", "float" |
| `unit` | | String | Unit of measure |
| `coefficient` | | Float | Multiplier coefficient |
| `jauge` | | String | "Gauge", "Battery", "text" |
| `visible` | | Decimal | 1 (visible) or 0 (hidden) |
| `poll` | | Decimal | Polling interval (sec) |
| `mqtt_device_class` | | String | MQTT device class for HA |
| `mqtt_state_class` | | String | MQTT state class for HA |
| `mqtt_icon` | | String | MQTT icon for HA |

### Action
### Action Parameters

| Parameter | Mandatory | Type | Description |
|-----------|-----------|------|-------------|
| `name` | ✓ | String | Action name |
| `command` | ✓ | Decimal | Command ID |
| `endpoint` | ✓ | Decimal | Endpoint number |
| `value` | ✓ | Decimal | Value to send |
| `visible` | | Decimal | 1 (visible) or 0 (hidden) |

### Bind
List of cluster(in numeric) which will be binded
example : `bind : "1026;1029;1794"`

### Report

|Command|Mandatory|Type|Value|Comment|
|-------|---------|----|-----|-------|			
|`cluster`|✓|String||cluster id in hexadecimal|  
|`attribut`|✓|Decimal||attribute number in decimal|  
|`type`|✓|Decimal|| Correspond to the numeric type of attribut| 
|`min`|✓|Decimal||min time (in second) to send report| 
|`max`|✓|Decimal||max time (in second) to send report| 
|`timeout`| |Decimal|| in millisecond| 
|`change`| |Decimal|| change value to send report| 


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


### Condition Parameters

| Parameter | Mandatory | Type | Value | Comment |
|-----------|-----------|------|-------|---------|
| `type` | ✓ | String | "device" | |
| `IEEE` | ✓ | String | MAC address without ':' or '-' | |
| `cluster` | ✓ | Decimal | Cluster ID in decimal | |
| `attribut` | ✓ | Decimal | Attribute number | |
| `operator` | ✓ | String | "<", ">", "==", "!=", ">=", "<=" | |
| `value` | ✓ | Decimal | Comparison value | |
| `logic` | | String | "AND", "OR" | Only for multiple conditions |

### Action Parameters

| Parameter | Mandatory | Type | Value | Comment |
|-----------|-----------|------|-------|---------|
| `type` | ✓ | String | "onoff" | |
| `IEEE` | ✓ | String | MAC address without ':' or '-' | |
| `endpoint` | ✓ | Decimal | Endpoint ID | |
| `value` | ✓ | String | Action value | |

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

## 🔌WEB API
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

## 📦 Firmware Installation
Just install esptools and run this command

### Windows

```bash
esptool.py.exe --chip esp32 --port "COMXX" \
	 --baud 460800 \
	 --before default_reset --after hard_reset write_flash -z \
	 --flash_mode dio --flash_freq 40m --flash_size 16MB \
	 0x0 bootloader.bin \
	 0x8000 partitions.bin \
	 0xe000 boot_app0.bin \
	 0x10000 firmware.bin \
	 0x910000 littleFS.bin
```

## 🏠 Home Assistant Integration

**LiXee-Gateway** is compatible with the MQTT discover from HA.

Just go to **Gateway** menu --> **MQTT** and enable the feature

<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-GW_config_MQTT.png" width="800">  

Fill the form :
  * MQTT server
  * MQTT port
  * MQTT username
  * MQTT password

Then click to **Home-Assistant** and **Save**  
Wait a moment and if all it's good, the connected icon will change to green.

Then go to **Network** menu --> **Zigbee**

For each Zigbee devices, a new button **MQTT Discover** appear. Please click on it to create a new device on HA. And that's it.
<div align='center'><img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-GW_Devices_mqtt_discover.png" width="320">  </div>

Wait a moment and go to your HA MQTT devices:
<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/HA_Create_MQTT_device.png" width="1024">  

<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/HA_MQTT_device_entities.png" width="800">

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
  
