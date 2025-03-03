# LiXee-Gateway
A WiFi gateway for zigbee devices

## Hardware compatibility
This application can be used with :  
* [LiXee-ZiWifi32](https://lixee.fr/produits/41-lixee-ziwifi32-3770014375162.html)  (WiFi only)
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

1. With you web navigator, you can go to `Config` menu then `Devices`  
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
|jauge| |String|"Gauge" / "Battery"| only for numeric type| 
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


## How to flash release
Just install esptools and run this command

### Windows

`esptool.py.exe --chip esp32 --port "COMXX" --baud 460800 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size 16MB 0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin`

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

### V1.1
* Add API commands
* Add Marstek compatibility
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
  
