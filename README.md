# LiXee-Box
Little Universal Smarthome Box with ESP32S3 MCU and ZiGate (Zigbee Radio MCU)

⚠️ **WARNING : The actual version is in testing mode and not a release yet.**

## Compatibility
This application can be used with :  
* [LiXee-ZiWifi32](https://lixee.fr/produits/41-lixee-ziwifi32-3770014375162.html)  (WiFi only)
    * The zigbee radio is a ZiGate.  
* LiXee-Ethernet32  (WiFi / Ethernet) (In progress)
    * The zigbee radio must be : ZiGate (with PiZiGate). 
      
Obviously, you can use this code with **ESP32S3** chip but depend on your board pins connexions. 

## Operating diagram

<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/LiXee-Box_Schema.png" width="1024">  

## Uses cases example

Here is a list of some use cases which can be used with LiXee-Box.  

### Use case n°1
You need to visualize and control your consumption (electricity / gaz / water) with a screen

### Use case n°2
Your Linky counter is very far and Zigbee protocol is too light to transport datas to the coordinator. You can use LiXee-Box like a gateway Zigbee/WiFi.  
**Linky (ZLinky) <--> Zigbee <--> LiXee-Box <--> WiFi <--> MQTT <--> Home-Assistant / Jeedom / Domoticz**

### Use case n°3
You need to relay zigbee devices datas to the cloud (with Web API)

## Features
The main feature is to get a mini smarthome box with **energy management**.  
Based on ESP32S3, you can manage, configure and act with your smarthome accross a Web browser (tablet / mobile / computer)

You can :  
* Manage a [ZLinky_TIC](https://github.com/fairecasoimeme/Zlinky_TIC) for electricity consumption and production.
  * Management of subscriptions and prices
  * Graphs and gauges of consumption and power consumed according to a time base
  * Consumption trend display
* Manage a [ZiPulses](https://github.com/fairecasoimeme/ZiPulses/) for water and gas consumption management 
  * Prices management 
  * Graphs and gauges 
* Manage ZigBee devices
  * Object creation
  * Template creation
  * Status and actions management
* Time management by NTP
* MQTT API management
  * Server / Port / user / password /topic header customisable
  * MQTT discover available (Home Assistant)
* Rules management (in progress)
* Notification management (in progress)
* Update OTA
* Backup / Restore datas

## First start  

1. Plug the device on a USB power.
2. Use a mobile/computer and scan the WiFi
3. Connect to the SSID : **LiXeeBox-XXXX** (XXXX correspond to a part of unique @MAC)
4. By default, the password is : **adminXXXX** (XXXX correspond to the ssid XXXX)
5. Connected to the WiFi SSID, you can open a web navigator and type in URL label : `http://lixee-box`
6. Normally, you will redirect to the WiFi config page
7. Scan your SSID WiFi box and fill the form to complete configuration
8. Reboot the device

## Pair a device

1. With you web navigator, you can go to `Config` menu then `Devices`  
2. You can click on `Add Device` button. The ZiGate coordinator begin the `Permit Join` procedure for 30 seconds.   
3. The blue LED of your device blink slowly.  
4. Now, you can execute the pairing device procedure.
   
⚠️ **If a device is paired, a green alert appears. You can refresh to see devices properties.**  
  
<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/ConfigDevices_1.png" width="800">  

## How to template a new zigbee device

A template file is a JSON structure which give status and actions to a device type. The name of the template file corresponds to the device identification (decimal).
When a Zigbee device is joining, LiXee-Box create an object following the corresponding template with status and actions, binding and configure reporting if it is necessary.

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

## How to backup/restore
Just click on `Tools` menu then `Backup`  

<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/backup_restore.png" width="800">  

You can do a backup with clicking on `Create Backup` button. Wait a moment and a tar file appears. Then click on the link to download the backup.  
To do a restore, please click on `Choose backup...` and click on `Restore`button.  

## How to flash release
Just install esptools and run this command

### Windows

`esptool.py.exe --chip esp32 --port "COMXX" --baud 460800 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size 16MB 0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 lixee-box.bin`

## Screenshots

### Status Energy
<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/StatusEnergy_3.png" width="1024">  

### Status network
<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/StatusNetwork.png" width="1024">  

### Config Wifi
<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/ConfigWifi.png" width="1024">  

### Config Gaz
<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/ConfigGeneralGaz.png" width="1024">  

### Dashboard 
<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/dashboard.png" width="480">  


## Changelog

### V1.0a
* Initial source 
  
