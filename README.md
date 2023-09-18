# LiXee-Box
Little Universal Smarthome Box with ESP32 MCU and ZiGate (Zigbee Radio MCU)


## Compatibility
This application can be used with :  
* [LiXee-ZiWifi32](https://lixee.fr/produits/41-lixee-ziwifi32-3770014375162.html)  (WiFi only)
    * The zigbee radio is a ZiGate.  
* LiXee-Ethernet32  (WiFi / Ethernet)
    * The zigbee radio must be : ZiGate (with PiZiGate).
      
Obviously, you can use this code with ESP32 chip but depend on your board pins connexions. 

## Features
The main feature is to get a mini smarthome box with energy management.
Based on ESP32, you can manage, configure and act with your smarthome accros a Web browser (tablet / mobile / computer)

You can :  
* Manage a ZLinky_TIC for electricity consumption and production.
  * Management of subscriptions and prices
  * Graphs and gauges of consumption and power consumed according to a time base
  * Consumption trend display
* Manage a ZiPulses for water and gas consumption management (in progress)
  * Prices management (in progress)
  * Graphs and gauges (in progress)
* Manage ZigBee device
  * Object creation
  * Template creation
  * Status and actions management
* Time management by NTP
* Rules management (in progress)
* Notification management (in progress)
* MQTT API management (in progress)

## How to template zigbe device

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

### Status

|Command|Mandatory|Type|Value|Comment|
|-------|---------|----|-----|-------|			
|name|x|String||string character|   
|cluster|x|String||cluster id in hexadecimal|  
|attribut|x|Decimal||attribute number in decimal|  
|type| |String|"numeric"| only if you want manage numeric value| 
|unit| |String|""| only for numeric type| 
|coefficient| |float|| only for numeric type| 
|jauge| |String|"Gauge"| only for numeric type| 
|min| |Decimal|| only for jauge = "Gauge"| 
|max| |Decimal|| only for jauge = "Gauge"| 
|visible| |Decimal|1 or 0| only if you want to display on dashboard| 
|poll| |Decimal|| number of seconds if you want to poll the device| 

### Action

|Command|Mandatory|Type|Value|Comment|
|-------|---------|----|-----|-------|			
|name|x|String||string character|   
|command|x|Decimal|146|command id in decimal value|  
|endpoint|x|Decimal||endpoint number in decimal|  
|value|x|Decimal||value sent in decimal| 
|visible| |Decimal|1 or 0| only if you want to display button on dashboard| 

## How to flash release
Just install esptools and run this command

### Windows

`esptool.py.exe --chip esp32 --port "COMXX" --baud 460800 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size 16MB 0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 lixee-box.bin`

## Screenshots

## Changelog

### V1.0
* Initial source 
  
