# LiXee-Box

## Description

**LiXee-Box** est une passerelle multi-protocole pour appareils Zigbee, conçue pour être un hub central de **gestion de l'énergie** et de domotique. Cette application transforme votre LiXee-ZiWifi32 en une passerelle complète capable de gérer vos appareils Zigbee, votre Linky, compteur de production, gaz, eau, et d'intégrer le tout dans votre système domotique.

## Matériel Compatible

Cette application fonctionne avec :

- **[LiXee-ZiWifi32 Lite](https://lixee.fr/produits/41-lixee-ziwifi32-3770014375162.html)** (WiFi uniquement)
  - Basé sur ESP32-S3-WROOM-N16R8 (PSRAM : 8MB Flash : 16MB)
  - Équipé d'un module JN5189 exécutant le [firmware ZiGate v2](https://github.com/fairecasoimeme/ZiGatev2)

> **Note** : Vous pouvez également utiliser ce code avec d'autres cartes ESP32S3, selon les connexions de broches de votre carte.

<table><tr><td><img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/LiXee_ZiWiFi32_face.png" width="480"></td>
<td><img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/LiXee_ZiWiFi32_pile.png" width="480">  </td></tr></table>
      
## Schéma de fonctionnement

<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/LiXee-Box_Schema.png" width="1024">  

## Installation

[![Tutoriel Installation LiXee-Box](https://img.youtube.com/vi/1w0yDo08sk0/0.jpg)](https://youtu.be/1w0yDo08sk0)

## Cas d'Usage Typiques

- **Relais longue distance** : Linky (ZLinky) ↔ Zigbee ↔ LiXee-Box ↔ WiFi ↔ MQTT ↔ Home-Assistant/Jeedom/Domoticz
- **Passerelle cloud** : Relayer les données des appareils Zigbee vers des services web via API
- **Gestion énergétique avancée** : Surveillance, délestage de charge, routage énergétique

### Pour aller plus loin
-  [🏠 LiXee-Box : Mesurer, analyser, économiser … pour une bonne gestion énergétique](https://faire-ca-soi-meme.fr/domotique/2025/08/18/lixee-box-mesurer-analyser-economiser-pour-une-bonne-gestion-energetique/)
-  [⚡️ Optimiser la recharge de son véhicule électrique avec la LiXee-Box](https://faire-ca-soi-meme.fr/domotique/2025/11/13/optimiser-la-recharge-de-son-vehicule-electrique-avec-la-lixee-box/)
-  [📡 Augmenter la portée du ZLinky_TIC v2](https://faire-ca-soi-meme.fr/domotique/2025/03/29/augmenter-la-portee-du-zlinky_tic-v2/)

## ✨ Fonctionnalités Principales
La fonctionnalité principale est de relayer les données des appareils Zigbee vers un site web ou un service MQTT

L'appareil peut être configuré via un site web local

### 🔧 Gestion des Appareils Zigbee
- Création et gestion d'objets Zigbee
- Modèles personnalisables pour différents types d'appareils
- Gestion des états et actions
- Historique des données pour les appareils de puissance et d'énergie
- Mises à jour OTA (Over-The-Air) automatiques et manuelles

### 📊 Surveillance et Tableau de Bord
- Tableau de bord énergétique avec jauges et graphiques
- Surveillance de la consommation en temps réel
- Graphiques de tendance et historiques
- Données Linky intégrées

### 🌐 Connectivité
- **MQTT** : Serveur/port/utilisateur/mot de passe personnalisables
- **MQTT Discovery** compatible avec Home Assistant
- **WebPush API** : URL/utilisateur/mot de passe

### ⚡ Gestion de l'Énergie
- Règles automatisées pour le délestage de charge et le routage énergétique
- Seuils configurables avec actions automatiques
- Gestion de la production et distribution d'énergie
- Gestion tarifaire pour l'énergie, la production, le gaz et l'eau

### 🔄 Mises à Jour et Maintenance

- Sauvegarde/restauration de configuration
- Mode développeur pour le débogage

## Mise à Jour du Firmware

### A partir de l'interface Web

Cliquer sur "A propos" puis "Mise à jour"

![Mise à Jour du Firmware](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_Update.png)

L'interface de mise à jour vous permet de maintenir votre LiXee-Box à jour avec les dernières fonctionnalités.

⚠️ **Si la mise à jour ne fonctionne pas, veuillez passer avec la méthode suivante (avec un ordinateur)**

### Avec un ordinateur

1. Tout d'abord il faut sortir la carte électronique du boitier. Dévisser les 2 vis.
<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/devisser_boitier.jpg" width="480"/>
2. Brancher la carte sur le port USB d'un ordinateur en appuyant sur le bouton flash.
<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/update_flash.jpg" width="480" />
4. Télécharger le fichier firmware (fichier **firmware.bin**) dans la section release

#### En mode GUI

6. Télécharger l'utilitaire de flash : ![Flash download tool](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32/production_stage/tools/flash_download_tool.html)
7. Décompresser et lancer l'exéctutable : flash_download_tool_x.x.x.exe
8. Suivez les même paramètres que les captures d'écran
<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/Flash/flash tools.png"/>
<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/Flash/flash_firmware.png"/>
<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/Flash/flash_firmware_loading.png" />
<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/Flash/flash_firmware_finish.png"/>

#### En mode console

9. Ouvrir une console ou powershell et se placer dans le répertoire ou se trouve le fichier **firmware.bin**
10. Taper la commande suivante :

   ```esptool.exe --chip esp32s3 --port "COMXX" --baud 921600 write_flash -z --flash_mode dio --flash_freq 40m --flash_size 16MB 0x10000 firmware.bin```

## 📱 Interface Utilisateur

### Jumelage d'Appareils
![Appairage d'Appareils](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_AssistDevice_p1.png)
![Appairage LiXee-Box](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_AssistDevice_p2.png)
![Recherche d'Appareils](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_AssistDevice_p3.png)

Le processus d'appairage est simplifié avec un assistant pas à pas pour connecter vos appareils Zigbee.

### Gestion des Appareils
![Configuration des Appareils Zigbee](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_config_zigbee.png)
![État des Appareils](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_DeviceStatus.png)

Interface complète pour configurer et surveiller tous vos appareils Zigbee connectés.

### Tableau de Bord Énergétique
![État Énergétique](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/screenshot_lixee-box-v2.8.PNG)
![Interface Mobile](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_Mobile_energy.png)

Tableau de bord complet avec visualisation en temps réel de votre consommation énergétique, graphiques historiques et interface mobile responsive.

### Production d'énergie et Injection

#### Mode Producteur

La **LiXee-Box** permet de récupérer la production d'énergie.  
Au travers du ZLinky, il est possible de récupérer les données d'injection et l'index de production dans les cas suivant :
* Le Linky est en mode standard
* vous avez un contrat avec EDF OA
Ces conditions sont nécessaires pour que votre Linky soit en mode **Producteur**

![Graphe producteur](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/Lixee-box_producteur.PNG)
![Jauge_producteur](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/Lixee-box_puissance_electricite.PNG)

Il suffit donc de paramétrer dans **Config** --> **Energie** puis sélectionner dans l'onglet **Production** le ZLinky qui est mode production.  
  
PS : Dans certaines configurations, il y a 2x Linky dont 1x Linky dédié à la production. Il faudra alors brancher et jumeler un autre ZLinky que celui dédié à la consommation

#### Mode Autoconsommation
Si vous possédez des panneaux photovoltaïques sans contrat, le Linky ne pourra pas être paramétrer en mode Producteur par votre fournisseur d'énergie.  
Cependant, il sera possible de détecter une sur production lorsque votre production sera supérieure à votre consommation. Dans ce cas, la **LiXee-Box** pourra déterminer la puissance d'injection sur le réseau et agir en conséquence.
En effet, cette information vous permettra de lancer des machines, enclencher votre chauffe-eau ou encore recharger votre véhicule électrique.

Voici les conditions permettant de détecter une surproduction sur votre Linky
* une puissance apparente à 0 VA
* une intensité > 0 A

Si ce cas arrive, vous aurez sur le graphique de puissance les données d'injection.

![Injection](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/Injection_autoconsommation.PNG)

## Les règles (automatismes)

Pour accéder à toutes les règles, il faut suivre **Config** --> **Règles**  
La page permet de suivre la liste des règles avec leur état et la date de dernière exécution.  
Vous pourrez **créer**, **modifier** ou **supprimer** une règle.  

![Liste des règles](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_Rules_status.png)

### Comment créer des règles

* Les règles sont stockées dans un fichier JSON.
* Une règle peut être déclenchée :
  * toutes les 60 secondes
  * dès que le couple cluster/attribut choisi est mis à jour  
* Une règle peut contenir une ou plusieurs conditions.  
* Une règle peut contenir une ou plusieurs actions si les conditions sont réunis.
* Une règle peut contenir une ou plusieurs actions si les conditions ne sont pas réunis
* Une règle peut intégrer une plage horaire (Optionnel)

![Ajouter/modifier une règles](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-box-regle-v2.11.PNG)

### Structure
Voici la structure :  

    ├── Rule     
    │   ├── name   
	|   ├── TimeRanges
	│   │   ├── startTime   
    │   │   ├── endTime   
    │   │   ├── days[...]  //1,2,3,4,5,6,7
	|   ├── trigger
	│   │   ├── mode   
    │   │   ├── IEEE   
    │   │   ├── cluster
	│   │   ├── attribut
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

### Paramètres des trigger

par défaut mode = timer

| Paramètre | Obligatoire | Type | Valeur | Commentaire |
|-----------|-------------|------|--------|-------------|
| `mode` | ✓ | String | "event" ou "timer" | |
| `IEEE` | ✓ | String | Adresse MAC sans ':' ou '-' | |
| `cluster` | ✓ | Decimal | ID du cluster en décimal | |
| `attribut` | ✓ | Decimal | Numéro d'attribut | |

### Paramètres de Condition

| Paramètre | Obligatoire | Type | Valeur | Commentaire |
|-----------|-------------|------|--------|-------------|
| `type` | ✓ | String | "device" | |
| `IEEE` | ✓ | String | Adresse MAC sans ':' ou '-' | |
| `cluster` | ✓ | Decimal | ID du cluster en décimal | |
| `attribut` | ✓ | Decimal | Numéro d'attribut | |
| `operator` | ✓ | String | "<", ">", "==", "!=", ">=", "<=" | |
| `value` | ✓ | Decimal/String | Valeur de comparaison | Peut être un String sur les opérateurs == ou != uniquement |
| `logic` | | String | "AND", "OR" | Uniquement pour conditions multiples |

### Paramètres d'Action

| Paramètre | Obligatoire | Type | Valeur | Commentaire |
|-----------|-------------|------|--------|-------------|
| `type` | ✓ | String | "onoff" / "notification" | |
| `IEEE` | ✓ | String | Adresse MAC sans ':' ou '-' | |
| `endpoint` | ✓ | Decimal | ID du point de terminaison | |
| `value` | ✓ | String | Valeur de l'action | |

### Paramètres plage horaire (Optionnel)

| Paramètre | Obligatoire | Type | Valeur | Commentaire |
|-----------|-------------|------|--------|-------------|
| `startTime` | ✓ | String | "HH:mm" | |
| `endTime` | ✓ | String | "HH:mm" | |
| `days` | ✓ | Array | [1,2,3,4,5,6,7] | Chaque chiffre correspond au numéro du jour|


Par exemple :  

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
		   "type":"notification",
           "IEEE":"",
           "endpoint":0,
           "value":"",
           "title":"🚨⚡puissance > 1000",
           "message":"🚨⚡puissance > 1000"
		}
        ]
    }
   ]
}
```

### Notifications

Les notifications permettent d'être informé ou alerté des évènements de votre habitat selon vos besoins

#### Exemple configuration
![Configuration notifications](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_ConfigNotifications.png)

#### Exemple d'évènements
![Evènements](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_Notifications.png)

## 🚀 Installation et Configuration

### Configuration Initiale

#### À partir de la v2.2a

avec **LiXee-Assist** https://github.com/fairecasoimeme/LiXee-Assist  
Veuillez télécharger l'application pour procéder 
<table><tr><td><a target="_blank" href="https://play.google.com/store/apps/details?id=com.lixee.assist" style="display: inline-block;">
  <img width="150px" src="https://play.google.com/intl/en_us/badges/static/images/badges/en_badge_web_generic.png" 
       alt="Get it on Google Play" 
      />
</a></td><td>
    <a target="_blank" href="https://apps.apple.com/us/app/lixee-assist/id6747671219?itscg=30200&itsct=apps_box_badge&mttnsubad=6747671219" style="display: inline-block;">
    <img src="https://toolbox.marketingtools.apple.com/api/v2/badges/download-on-the-app-store/black/fr-fr?releaseDate=1751500800" alt="Download on the App Store" style="width: 150px; vertical-align: middle; object-fit: contain;" />
    </a></td></tr></table>

Scannez simplement l'appareil (avec BLE) et complétez l'appairage avec vos identifiants WiFi locaux

#### Avant la v2.2a
Avec un navigateur web
1. **Alimentation** : Branchez l'appareil sur une alimentation USB
2. **WiFi** : Scannez les réseaux WiFi disponibles avec votre mobile/ordinateur
3. **Connexion** : Connectez-vous au SSID `LIXEEGW-XXXX` (XXXX = partie de l'adresse MAC)
4. **Authentification** : Mot de passe par défaut `adminXXXX` (XXXX = suffixe du SSID)
5. **Configuration** : Ouvrez `http://lixee-gw` dans votre navigateur
6. **WiFi Principal** : Configurez votre réseau WiFi principal
7. **Redémarrage** : L'appareil redémarre et se connecte à votre réseau

### Configuration Zigbee

1. Allez dans le menu **Réseau** → **Zigbee**
2. Cliquez sur **Ajouter un Appareil** pour démarrer l'appairage (30 secondes)
3. La LED bleue clignote lentement pendant l'appairage
4. Exécutez la procédure d'appairage sur votre appareil Zigbee
   
⚠️ **Si un appareil est appairé, une alerte verte apparaît. Vous pouvez rafraîchir pour voir les propriétés de l'appareil.**  

## 📦 Appareils Compatibles (à partir de la v2.12)

La **LiXee-Box** est compatible avec un large éventail d'appareils Zigbee grâce à sa prise en charge des clusters standards. Voici la liste détaillée du matériel compatible.

### Clusters Zigbee Supportés

| Cluster | Nom | Description |
|---------|-----|-------------|
| 0x0000 | Basic | Informations de base de l'appareil |
| 0x0001 | Power Configuration | Gestion batterie |
| 0x0006 | On/Off | Commandes marche/arrêt |
| 0x0102 | Window Covering | Contrôle volets/stores |
| 0x0402 | Temperature Measurement | Mesure de température |
| 0x0405 | Relative Humidity | Mesure d'humidité |
| 0x0406 | Occupancy Sensing | Détection de présence |
| 0x0702 | Simple Metering | Comptage d'énergie |
| 0x0B04 | Electrical Measurement | Mesures électriques |
| 0xFF66 | LiXee | Cluster propriétaire LiXee |

---

### ⚡ Appareils LiXee (Compatibilité Complète)

| Appareil | Device Type | Description | Testé |
|----------|-------------|-------------|:-----:|
| **ZLinky_TIC** | 0x0061 | Téléinformation Linky | ✅ |
| **ZiPulses** | 0x0107 | Compteur d'impulsions (eau, gaz) | ✅ |

---

### 🔌 Prises Connectées
**Device Type : 0x0107, 0x0101, 0x0061**

#### Marques testées et compatibles

| Marque | Modèles | Mesure énergie | Notes | Testé |
|--------|---------|----------------|-------|:-----:|
| **Aqara** | SP-EUC01, ZNCZ12LM | ✅ | 10A max, capteur température intégré | |
| **Sonoff** | S31 Lite ZB, S40 Lite | ✅ | 15-16A, bon rapport qualité/prix | |
| **IKEA** | TRÅDFRI, Grillplats | ❌ | Répéteur Zigbee | |
| **Tuya/Moes** | Diverses prises Smart Life | ✅ | 16A, économique | |
| **Neo Coolcam** | Plug-007SPB2 | ✅ | Basé Tuya | |
| **Nous** | Smart Zigbee Socket A1Z | ✅ | Version EU | ✅ |
| **Lidl** | SilverCrest Smart Plug | ✅ | Basé Tuya | |
| **BlitzWolf** | BW-SHP13 | ✅ | 16A, bon routeur Zigbee | |
| **Innr** | SP 120, SP 220, SP 222 | ✅ | Compatible Hue | |
| **Osram** | Smart+ | ✅ | |✅ |
| **Philips** | LOM001 | ✅ |  |✅ |

#### Autres prises compatibles (clusters standards)
Tout appareil Zigbee avec Device Type 0x0107, 0x0101 ou 0x0061 utilisant les clusters On/Off (0x0006) et Electrical Measurement (0x0B04) devrait fonctionner.

---

### 🪟 Volets Roulants / Stores
**Device Type : 0x0202**

#### Marques testées et compatibles

| Marque | Modèles | Notes | Testé |
|--------|---------|-------|:-----:|
| **NodOn** | SIN-4-RS-20 | Module encastrable, auto-calibration | |
| **Tuya/Moes** | Curtain Switch, Roller Shutter Module | Nombreux modèles disponibles | ✅ |
| **LoraTap** | SC500ZB, divers modèles | Compatible Zigbee2MQTT | |
| **Legrand** | Céliane/Mosaic Zigbee | Volet roulant connecté | |
| **Aqara** | Curtain Driver E1 | Moteur pour rideaux | |
| **Zemismart** | Roller Shade Motor | Moteur tubulaire | |

#### Autres volets compatibles (clusters standards)
Tout appareil avec Device Type 0x0202 utilisant le cluster Window Covering (0x0102) avec les commandes Up/Down/Stop.

---

### 🌡️ Capteurs Température / Humidité
**Device Type : 0x0302**

#### Marques testées et compatibles

| Marque | Modèles | Pression | Écran | Notes | Testé |
|--------|---------|----------|-------|-------|:-----:|
| **Aqara** | WSDCGQ11LM, T1 (TH-S02D) | ✅ | ❌ | Capteur Sensirion, très précis | |
| **Sonoff** | SNZB-02, SNZB-02D, SNZB-02P | ❌ | ✅ (02D) | Économique, écran LCD sur 02D | ✅ |
| **IKEA** | VINDSTYRKA, Timmerflotte | ❌ | ✅ | Qualité de l'air sur VINDSTYRKA | |
| **iHorn** | 113D | ❌ |  ✅ |  | ✅ |
| **Tuya/Moes** | ZSS-ZK-THL, TS0201, divers | Variable | Variable | Large choix de modèles | |
| **Nous** | E5 | ❌ | ❌ | Compact | |
| **OWON** | THS317-ET | ❌ | ❌ | Sonde externe sur câble | |
| **Xiaomi/Mijia** | WSDCGQ01LM | ❌ | ❌ | ⚠️ Non Zigbee 3.0 | |

#### Autres capteurs compatibles (clusters standards)
Tout appareil avec Device Type 0x0302 utilisant les clusters Temperature Measurement (0x0402) et Relative Humidity (0x0405).

---

### 👁️ Capteurs de Présence / Mouvement
**Device Type : 0x0107**

#### Marques testées et compatibles

| Marque | Modèles | Luminosité | Type | Notes | Testé |
|--------|---------|------------|------|-------|:-----:|
| **Aqara** | RTCGQ11LM, P1 | ✅ | PIR | Délai 60-90s | |
| **Aqara** | FP1, FP2 | ✅ | mmWave | Détection présence statique, zones | |
| **Sonoff** | SNZB-03 | ❌ | PIR | Délai 60s | |
| **Sonoff** | SNZB-06P | ✅ | Radar 5.8GHz | Présence statique | ✅ |
| **Philips Hue** | Indoor/Outdoor Motion | ✅ | PIR | Température intégrée | |
| **IKEA** | TRÅDFRI E1745, Myggspray | ❌ | PIR | IP44 (extérieur) | |
| **Tuya/Moes** | ZY-M100, MTG075-ZB | Variable | mmWave | Présence statique | |

#### Autres capteurs compatibles (clusters standards)
Tout appareil avec Device Type 0x0107 utilisant le cluster Occupancy Sensing (0x0406).

---

### 🔥 Vannes Thermostatiques (TRV)
**Compatibilité via clusters standards**

| Marque | Modèles | Notes | Testé |
|--------|---------|-------|:-----:|
| **Moes/Tuya** | BRT-100, TV01, TRV601 | Programmable, détection fenêtre ouverte | ✅ |
| **Danfoss** | Ally | Haute qualité | |
| **Eurotronic** | Spirit Zigbee | | |
| **Popp** | POPZ701721 | | |

> ⚠️ **Note** : Les TRV utilisent souvent des clusters propriétaires Tuya (TS0601). La compatibilité peut varier selon les modèles.

---

### 💡 Ampoules et Éclairage
**Compatibilité partielle via cluster On/Off**

| Marque | Notes | Testé |
|--------|-------|:-----:|
| **IKEA** | TRÅDFRI / Kajplats - On/Off et dimming | |
| **Philips Hue** | Compatible clusters standards | |
| **Innr** | Compatible Hue et Zigbee standards | |
| **Tuya/Moes** | Nombreux modèles | |

> ℹ️ La LiXee-Box supporte le cluster On/Off (0x0006). Le contrôle des couleurs (cluster 0x0300) n'est pas ecnore implémenté.


### ❓ Vérifier la Compatibilité

Pour vérifier si un appareil Zigbee est compatible :

1. **Identifiez le Device Type** de votre appareil (disponible dans la documentation)
2. **Vérifiez les clusters** utilisés par l'appareil
3. Un appareil est compatible si :
   - Son Device Type correspond à ceux listés ci-dessus
   - Il utilise les clusters standards supportés

## 📝 Créer un Template Personnalisé

Si votre appareil n'est pas reconnu automatiquement, vous pouvez créer un template personnalisé. 

Un fichier modèle est une structure JSON qui définit les états et actions d'un type d'appareil. Le nom du fichier modèle correspond à l'identification de l'appareil (en décimal).
Lorsqu'un appareil Zigbee rejoint le réseau, **LiXee-Box** crée un objet suivant le modèle correspondant avec les états et actions, effectue les liaisons et configure les rapports si nécessaire.

### Structure du Modèle
Voici la structure :

    ├── Modèle d'appareil ou 'default'    
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

Vous pouvez trouver des exemples de modèles dans le répertoire `data/tp`  
Exemple de fichier 24321.json pour l'appareil id (5F01 Hex) :
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

### Paramètres de Status

| Paramètre | Obligatoire | Type | Description |
|-----------|-------------|------|-------------|
| `name` | ✓ | String | Nom d'affichage |
| `cluster` | ✓ | String | ID du cluster (hex) |
| `attribut` | ✓ | Decimal | Numéro d'attribut |
| `type` | | String | "numeric", "float" |
| `unit` | | String | Unité de mesure |
| `coefficient` | | Float | Coefficient multiplicateur |
| `jauge` | | String | "Gauge", "Battery", "text" |
| `visible` | | Decimal | 1 (visible) ou 0 (masqué) |
| `poll` | | Decimal | Intervalle d'interrogation (sec) |
| `mqtt_device_class` | | String | Classe de périphérique MQTT pour HA |
| `mqtt_state_class` | | String | Classe d'état MQTT pour HA |
| `mqtt_icon` | | String | Icône MQTT pour HA |

### Action
### Paramètres d'Action

| Paramètre | Obligatoire | Type | Description |
|-----------|-------------|------|-------------|
| `name` | ✓ | String | Nom de l'action |
| `command` | ✓ | Decimal | ID de commande |
| `endpoint` | ✓ | Decimal | Numéro de point de terminaison |
| `value` | ✓ | Decimal | Valeur à envoyer |
| `visible` | | Decimal | 1 (visible) ou 0 (masqué) |

### Bind
Liste des clusters (en numérique) qui seront liés
exemple : `bind : "1026;1029;1794"`

### Report

| Commande | Obligatoire | Type | Valeur | Commentaire |
|----------|-------------|------|--------|-------------|			
| `cluster` | ✓ | String | | ID du cluster en hexadécimal |  
| `attribut` | ✓ | Decimal | | Numéro d'attribut en décimal |  
| `type` | ✓ | Decimal | | Correspond au type numérique de l'attribut | 
| `min` | ✓ | Decimal | | Temps minimum (en secondes) pour envoyer un rapport | 
| `max` | ✓ | Decimal | | Temps maximum (en secondes) pour envoyer un rapport | 
| `timeout` | | Decimal | | En millisecondes | 
| `change` | | Decimal | | Valeur de changement pour envoyer un rapport | 

## 🔌 API WEB
Pour accéder aux commandes de l'API, allez sur http://<HOST>/<commande>

### Liste des Commandes

* [getSystem](#getSystem)
* [getDevices](#getDevices)
* [getDevice?id=IEEE](#getDevice)
* [getLinky](#getLinky)
* [getTemplates](#getTemplates)


### Méthodes

#### getSystem

##### Requête
```bash
curl -X GET 'https://<HOST>/getSystem'\
    -u <username>:<password> \
    -H 'Content-Type: application/json' \
```
##### Réponse
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

##### Requête
```bash
curl -X GET 'https://<HOST>/getDevices'\
    -u <username>:<password> \
    -H 'Content-Type: application/json' \
```
##### Réponse
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

##### Requête
```bash
curl -X GET 'https://<HOST>/getDevice?id=04cf8cdf3c79ce2b'\
    -u <username>:<password> \
    -H 'Content-Type: application/json' \
```
##### Réponse
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

##### Requête
```bash
curl -X GET 'https://<HOST>/getLinky'\
    -u <username>:<password> \
    -H 'Content-Type: application/json' \
```
##### Réponse
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

##### Requête
```bash
curl -X GET 'https://<HOST>/getTemplates'\
    -u <username>:<password> \
    -H 'Content-Type: application/json' \
```
##### Réponse
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

## 📦 Installation du Firmware
Installez simplement esptools et exécutez cette commande

### Windows

```bash
esptool.py.exe --chip esp32s3 --port "COMXX" \
	 --baud 460800 \
	 --before default_reset --after hard_reset write_flash -z \
	 --flash_mode dio --flash_freq 40m --flash_size 16MB \
	 0x0 bootloader.bin \
	 0x8000 partitions.bin \
	 0xe000 boot_app0.bin \
	 0x10000 firmware.bin \
	 0x910000 littlefs.bin
```
### Linux

```bash
esptool.py --chip esp32s3 \
--port /dev/ttyUSB0 \
--baud 460800 \
--before default-reset \
--after hard-reset \
write-flash -z \
--flash-mode dio \
--flash-freq 40m \
--flash-size 16MB \
0x0 bootloader.bin \
0x8000 partitions.bin \
0xe000 boot_app0.bin \
0x10000 firmware.bin \
0x910000 littlefs.bin
```

## 🏠 Intégration Home Assistant

**LiXee-Gateway** est compatible avec la découverte MQTT de Home Assistant.

Allez simplement dans le menu **Passerelle** --> **MQTT** et activez la fonctionnalité

<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-GW_config_MQTT.png" width="800">  

Remplissez le formulaire :
  * Serveur MQTT
  * Port MQTT
  * Nom d'utilisateur MQTT
  * Mot de passe MQTT

Puis cliquez sur **Home-Assistant** et **Sauvegarder**  
Attendez un moment et si tout est correct, l'icône de connexion deviendra verte.

Ensuite, allez dans le menu **Réseau** --> **Zigbee**

Pour chaque appareil Zigbee, un nouveau bouton **MQTT Discover** apparaît. Veuillez cliquer dessus pour créer un nouveau périphérique sur HA. Et voilà.
<div align='center'><img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-GW_Devices_mqtt_discover.png" width="320">  </div>

Attendez un moment et allez dans vos périphériques MQTT HA :
<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/HA_Create_MQTT_device.png" width="1024">  

<img src="https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/HA_MQTT_device_entities.png" width="800">

## DEBUG

Il est possible de débugger l'application en branchant le matériel sur le port USB d'un ordinateur
**Utilisez putty.exe**
<img width="452" height="442" alt="image" src="https://github.com/user-attachments/assets/2b7fef49-60f3-4f9d-b3b0-39c3b53dff40" />
* Entrer le port "Serial line" : COMXX
* Entrer la vitesse "Speed" : 115200
* Sélectionner "Serial"
* Appuyer sur "Open"

## Crédits

Merci à tous les auteurs des bibliothèques tierces utilisées dans ce projet :

* [espressif / arduino-esp32](https://github.com/espressif/arduino-esp32)
* [rlogiacco/CircularBuffer](https://github.com/rlogiacco/CircularBuffer)
* [bblanchon/ArduinoJson](https://github.com/bblanchon/ArduinoJson)
* [paulstoffregen/Time](https://github.com/PaulStoffregen/Time)
* [marvinroger/AsyncMqttClient](https://github.com/marvinroger/async-mqtt-client)
* [arkhipenko/TaskScheduler](https://github.com/arkhipenko/TaskScheduler)
* [me-no-dev/AsyncTCP](https://github.com/me-no-dev/AsyncTCP)
* [me-no-dev/ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)

Merci à [ZigStar](https://github.com/mercenaruss) pour la mise à jour OTA



### V1.0a
* Source init
