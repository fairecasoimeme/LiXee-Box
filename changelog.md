# Changelog

## v2.27a

### Tableau de bord Énergie : sous-compteurs
- **Sous-compteurs comptés deux fois** dans « Usage d'électricité » et « Répartition » : un sous-compteur (radiateur, prise…) est en aval du Linky, sa consommation est donc déjà dans les index du Linky et doit en être retirée. Elle n'était retirée que de l'index tarifaire sous lequel le sous-compteur l'avait rangée ; or en mode Standard, l'index en cours était lu dans le libellé du tarif, propre à chaque fournisseur, et retombait souvent sur l'index 1 (HC). Pendant les heures pleines, rien n'était retiré : l'heure affichait Linky + radiateurs. L'index en cours vient désormais de NTARF (FF66/513), et la soustraction se fait au prorata des index de chaque tranche, ce qui corrige aussi l'affichage de l'historique déjà enregistré
- **Coût des sous-compteurs** (infobulle d'« Usage d'électricité », « Répartition », export CSV) : il était toujours calculé au prix HC, et, dans l'infobulle et l'export CSV, sans la taxe par kWh appliquée aux index. Il suit désormais le tarif réellement en cours à chaque tranche (prix moyen des index du Linky sur la tranche), avec la même taxe que les index
- **Prix arrondis à 2 décimales** dans les graphes : un prix de 0,2068 €/kWh était compté 0,21, et un prix d'eau de 0,004 €/L tombait à 0. Les prix sont transmis avec 6 décimales

### Réseau (Config LAN)
- **W5500 détecté à tort sur une carte qui n'en a pas** : le pilote d'ESP-IDF 4.4 ne vérifie pas la puce (il lit son registre de version sans le contrôler), son installation réussissait donc même sans W5500. La box vérifie désormais la puce elle-même (registre VERSIONR = 0x04 après une impulsion de reset) ; sans W5500, la broche de reset est relâchée et la box fonctionne en WiFi comme avant
- Journal de connexion WiFi : il affichait l'IP fixe de la configuration même en DHCP, ce qui laissait croire qu'elle était utilisée. Il indique maintenant `mode : DHCP` ou `mode : IP fixe ...`
- **Configuration WiFi vide quand la box démarre en Ethernet** : elle n'était lue qu'au démarrage du WiFi, qui n'a pas lieu quand l'Ethernet suffit. La page Config LAN affichait alors le WiFi en IP fixe au lieu de DHCP, et l'enregistrer basculait le WiFi en IP fixe avec un mot de passe vide. La configuration WiFi est désormais lue au démarrage dans tous les cas
- `/api/system` : `network.wifi.ip`, `netmask` et `gateway` donnent les adresses réellement utilisées quand le WiFi est connecté (en DHCP, c'était l'IP fixe saisie dans la configuration) ; nouveau champ `network.wifi.dhcp` (le champ `mode` existant valait toujours 0)

### API : noms uniformisés
- Les routes publiques suivent désormais **`/api/<groupe>/<ressource>`**, en minuscules ; un appareil se désigne par le paramètre **`ieee`** (`IEEE` et `id` restent acceptés) ; les graphes d'énergie lisent `ieee` et `time` par leur nom (et toujours par position pour les anciens appels)
- **Les anciens noms restent servis** par réécriture d'URL, avec la même réponse : aucun client existant (application, widgets, Home Assistant, scripts) n'est à modifier
- Correspondances : `/getSystem` → `/api/system`, `/api/wifiSignal` → `/api/network/wifi-signal`, `/getDevices` → `/api/devices`, `/getDevice` → `/api/device`, `/getEnergyDevice` → `/api/device/energy`, `/getPowerDevice` → `/api/device/power`, `/getTemplates` → `/api/templates`, `/getLinky` → `/api/energy/linky`, `/api/tariff` → `/api/energy/tariff`, `/getSubMeters` → `/api/energy/submeters`, `/loadEnergyChart` → `/api/energy/chart`, `/exportEnergyChart` → `/api/energy/chart/export`, `/api/stats` → `/api/notifications/stats`, `/api/debug` → `/api/notifications/debug`, `/api/tunnelStatus` → `/api/tunnel/status`, `/api/tunnelActivate` → `/api/tunnel/activate`, `/api/tunnelActivateStatus` → `/api/tunnel/activate/status`
- Les routes de l'API répondent à leur **nom exact** : une route ne capture plus celles qui la prolongent (`/api/notifications` captait `/api/notifications/stats`)
- `/api/system` donne aussi l'état de l'**Ethernet** (`network.ethernet`) et l'interface utilisée (`network.active`) ; les champs existants ne changent pas
- Section API du README réécrite : conventions, table de toutes les routes avec leur ancien nom, exemples

### Mise à jour
- Flasher le firmware **et** mettre à jour le système de fichiers (`data/web/js/functions.min.js.gz`), puis redémarrer (le script est versionné : pas besoin de vider le cache du navigateur)

## v2.26

### Appareils à plusieurs canaux (issue #44)
- **Champ `endpoint` dans les templates** : une ligne d'état peut désigner le canal (endpoint Zigbee) qu'elle lit. Sans ce champ, rien ne change (n'importe quel canal, comme avant). Les modules à plusieurs sorties exposent ainsi un état par canal
- **Aqara T2 double relais `lumi.switch.acn047`** : L1 et L2 (marche / arrêt et état de chaque canal), puissance, consommation, tension
- Attributs **flottants** (type ZCL 0x39) décodés, par exemple la puissance des modules Aqara
- **MQTT et WebPush** : l'attribut d'un canal porte un nom lisible (`0_ep2`) au lieu d'un nombre calculé
- **Home Assistant** : un interrupteur par canal (L1, L2), relié à l'état de ce canal
- **Règles** : les conditions sur un canal précis sont proposées et évaluées correctement

### LoRa 2.4 GHz
- **Mise à jour du firmware du ZLinky LoRa (OTA)** depuis Config → LoRa, comme en Zigbee : bouton de mise à jour sur la fiche de l'appareil, envoi du fichier, puis **barre de progression sur la fiche** (inutile de rester sur la page)
- **OTA par fenêtre** : les blocs partent par fenêtres de 16, un seul accusé de réception par fenêtre (au lieu d'un par bloc), avec reprise au premier bloc manquant : environ 35 min au lieu de 50 pour un firmware complet (estimation, à confirmer sur le terrain). ⚠️ Nécessite un firmware ZLinky LoRa qui gère les fenêtres (voir `doc/LORA_PROTOCOL.md`)
- **Version du firmware** du ZLinky LoRa lue à l'appairage (cluster Basic, attribut `0xFF01`) et affichée sur la fiche LoRa et la fiche produit
- Un **ZLinky appairé en LoRa devient automatiquement le Linky** de la configuration Énergie, comme en Zigbee
- Aux SF plus rapides que SF11 (réglage par défaut), le préambule des requêtes POLL est allongé pour garder la même durée qu'en SF11 et laisser au ZLinky le temps de les entendre
- Alertes TIC du mode Historique (trame EXTENDED `0x0B`) prises en compte
- Protocole radio documenté dans `doc/LORA_PROTOCOL.md`

### Ethernet (W5500)
- **Port Ethernet filaire** sur les cartes équipées d'un W5500 (SPI : CS GPIO10, SCLK GPIO12, MOSI GPIO11, MISO GPIO13, INT GPIO14, RST GPIO9, 20 MHz, bus SPI3 ; le bus SPI2 reste au module LoRa). Pilote W5500 d'ESP-IDF, déjà présent dans le core : aucune bibliothèque ajoutée
- **Ethernet prioritaire** : câble branché et adresse obtenue au démarrage, le WiFi n'est pas démarré (environ 40 Ko de mémoire interne économisés). Sans câble au démarrage, la box démarre en WiFi comme avant
- **Relais WiFi** : si l'Ethernet perd son adresse plus de 30 s (câble débranché, DHCP…), le WiFi est démarré s'il est configuré. L'Ethernet redevient la route par défaut dès son retour. Sans WiFi configuré, la box attend le câble
- Tant que l'Ethernet a une adresse, le **provisioning BLE** n'est jamais lancé et le WiFi ne fait pas de tentatives de reconnexion (elles bloquaient la boucle jusqu'à 30 s)
- **Page Config → LAN** (anciennement « WiFi ») : sur une carte avec W5500, choix du **mode de connexion** — *Automatique* (Ethernet prioritaire, WiFi en secours), *Ethernet seul* (le WiFi n'est jamais démarré) ou *WiFi seul* (l'Ethernet n'est pas utilisé). La section que le mode n'utilise pas est grisée, et chaque section indique si elle est active, prioritaire ou de secours. Réglages Ethernet : **DHCP ou IP fixe** (adresse, masque, passerelle, DNS), enregistrés dans `/config/configEthernet.json` puis redémarrage. Sans W5500, la page reste celle du WiFi
- Le **RAZ par 3 redémarrages rapides** remet aussi le mode Automatique et le DHCP (une IP fixe erronée ou le mode Ethernet seul sans câble rendraient la box injoignable)
- Page **État réseau** : carte Ethernet (câble, adresse, masque, passerelle, DNS, MAC, débit et duplex du lien, interface active)
- MQTT, tunnel, NTP, mDNS (`LIXEEBOX-xxxx.local`) et serveur web fonctionnent sur l'une ou l'autre interface
- Une carte **sans W5500** n'est pas affectée : la puce n'est pas détectée et le firmware fonctionne en WiFi comme avant

### API pour le kiosque LiXee-Assist
Nouvelles routes en lecture (sauf `/api/delestage/restore`), protégées comme les autres (session ou Basic Auth en accès direct, session seule par le tunnel). Les paramètres se lisent **par nom**, les réponses font quelques Ko et ne contiennent **aucun secret**. Une box plus ancienne répond 404 sur ces routes : l'application peut s'en servir pour garder ses contournements.

- **`GET /api/energy/config`** : la configuration Énergie **sans les mots de passe** (l'application n'a plus besoin de lire `configGeneral.json` en entier par `/readFile`). Prix : `tarifIdx2`…`tarifIdx10` → attributs d'index 256…272, prix nuls ou absents omis. `delestage` : prises délestées
  ```json
  {"zlinky":"00158d0001a2b3c4","production":"","gaz":"","water":"00158d0009f8e7d6",
   "linkyMode":1,
   "prices":{"256":0.1612,"258":0.2068},
   "subscription":{"abo":15.65,"cspe":0.0225,"cta":1.2},
   "gas":{"price":0.12,"coeff":10,"unit":"m3"},
   "water":{"price":0.004,"coeff":1,"unit":"L"},
   "subMeters":[{"index":0,"ieee":"a4c138...","alias":"Véhicule","color":"#e74c3c","enabled":true}],
   "thresholds":{"budget":{"enabled":true,"monthEur":80},
                 "overPower":{"enabled":false,"w":6000},
                 "dailyAnomaly":{"enabled":true,"percent":30},
                 "nightWaterLeak":{"enabled":true,"litres":1},
                 "waterLeak":{"enabled":false,"litres":5},
                 "freeze":{"enabled":false,"centiDeg":300,"sensor":""}},
   "delestage":["a4c1380000000001"]}
  ```
- **`GET /api/power/peaks?days=90`** (1 à 90, défaut 90) : **puissance par jour**, conservée 90 jours dans `/hst/powerStats.json` (écrit au plus une fois par heure). Le Linky coupe sur un dépassement qui dure de l'ordre d'**une minute** : un pic isolé ne dit rien du risque de coupure. Chaque jour porte donc trois niveaux, calculés sur la puissance apparente (injection exclue, mesures supérieures à 1,5 × la puissance souscrite écartées) :
  - `instVA` : pic **instantané** (la plus forte mesure) ;
  - `va1m` : pic de la **moyenne sur 1 minute** glissante, celle qui fait couper le Linky ;
  - `va10m` : pic du **plateau sur 10 minutes**, la charge durable qui sert à juger l'abonnement.

  `va` est la valeur de référence du jour : en mode **Standard**, la valeur officielle du compteur, SMAXSN du jour (0B04/1293), la veille étant clôturée par SMAXSN-1 (FF66/530) → `"source":"meter"` ; en mode **Historique** (pas de SMAXSN), `va1m` → `"source":"sampled"`. Un jour Standard sans SMAXSN reçu retombe sur `va1m` et le signale par `"source":"sampled"` dans son entrée.
  `minutes` : nombre de minutes du jour dont la moyenne a atteint 50, 60, 70, 80, 90 ou 100 % de la puissance souscrite (cumulé : une minute à 92 % compte dans 50…90 ; tranches à 0 omises). `measuredMin` : minutes effectivement mesurées. `subscribedVA` n'apparaît dans un jour que si l'abonnement était différent. Valeurs inconnues : `null`
  ```json
  {"subscribedVA":9000,"source":"meter",
   "peaks":[{"date":"2026-10-12","va":7900,"instVA":8350,"va1m":7710,"va10m":5480,"floorW":310,
             "measuredMin":1438,"minutes":{"50":95,"60":41,"70":12,"80":4}},
            {"date":"2026-10-13","va":4200,"instVA":4410,"va1m":4120,"va10m":3050,"floorW":null,
             "measuredMin":1012,"minutes":{}}]}
  ```
- **Triphasé : par phase**. Le Linky coupe sur **chaque** phase : `instVA`, `va1m`, `va10m` et `minutes` sont aussi donnés dans `phases` (une entrée par phase), rapportés à `phaseSubscribedVA` (un tiers de la puissance souscrite). Le niveau du jour (`va`, `instVA`…) porte alors sur le total. Standard : puissance de chaque phase (SINSTS1 à 3 : 0B04/1295, 2319, 2575), total = leur somme, et `meterVA` officiel par phase quand il est connu (SMAXSN-1 : FF66/530 à 532 ; pour le jour en cours, seule la phase 1 est publiée). Historique : puissance totale (PAPP), phases calculées d'après les intensités IINST1 à 3 (200 VA/A, comme le reste du firmware) ; la puissance souscrite d'un compteur historique triphasé vaut 3 × ISOUSC × 200 VA (ISOUSC est l'intensité d'une phase)
  ```json
  {"subscribedVA":12000,"triphase":true,"phaseSubscribedVA":4000,"source":"sampled",
   "peaks":[{"date":"2026-10-12","va":7100,"instVA":7600,"va1m":7020,"va10m":5200,"floorW":420,
             "measuredMin":1440,"minutes":{"50":30},
             "phases":[{"phase":1,"meterVA":3650,"instVA":3900,"va1m":3610,"va10m":2900,"minutes":{"50":210,"60":95,"70":40,"80":12,"90":2}},
                       {"phase":2,"instVA":2100,"va1m":1980,"va10m":1500,"minutes":{"50":3}},
                       {"phase":3,"instVA":1700,"va1m":1650,"va10m":1200,"minutes":{}}]}]}
  ```
- **`GET /api/power/summary`** : de quoi juger l'**abonnement** et le risque de coupure
  - `worst` : pires `instVA`, `va1m` et `va10m` des 90 derniers jours, avec leur date ;
  - en triphasé, `worst.phases` donne les pires valeurs de chaque phase, et `months[].phases` le détail mensuel par phase ;
  - `frequency` (`frequencyBasis` : `total` en monophasé, `phases` en triphasé, où un jour compte dès qu'**une** phase a atteint la tranche de sa propre limite) : pour chaque tranche, jours où la moyenne sur 1 min l'a atteinte sur les 7, 30 et 90 derniers jours mesurés, date du dernier, et `rate` : `daily` (au moins 70 % des jours), `weekly` (au moins 1 jour sur 10), `monthly` (au moins 1 jour sur 40), `rare`, `never`, ou `unknown` (moins de 7 jours observés ; un jour compte comme observé à partir d'1 h de mesures) ;
  - `months` : vue mois par mois sur **24 mois**, pour voir les périodes de l'année (hiver…) : pics, minutes et jours par tranche
  ```json
  {"subscribedVA":9000,"bands":[50,60,70,80,90,100],
   "worst":{"instVA":{"va":8350,"date":"2026-10-12"},"va1m":{"va":7710,"date":"2026-10-12"},
            "va10m":{"va":5480,"date":"2026-10-12"}},
   "frequency":{"50":{"days7":5,"days30":19,"days90":52,"observedDays":88,"last":"2026-10-13","rate":"weekly"},
                "80":{"days7":1,"days30":2,"days90":3,"observedDays":88,"last":"2026-10-12","rate":"monthly"},
                "90":{"days7":0,"days30":0,"days90":0,"observedDays":88,"last":null,"rate":"never"}, "...":{}},
   "months":[{"month":"2026-10","observedDays":13,"subscribedVA":9000,"instVA":8350,"va1m":7710,"va10m":5480,
              "minutes":{"50":410,"60":150,"70":38,"80":4},"days":{"50":11,"60":6,"70":3,"80":1}}]}
  ```
- **`GET /api/power/floor?days=30`** (1 à 90, défaut 30) : **consommation plancher par jour**, c'est-à-dire la plus petite consommation horaire entre 1 h et 5 h, en W, calculée sur les index du compteur. Une heure avec production est ignorée. Calculée à 5 h ; `null` si les index n'ont pas pu être relevés (box redémarrée pendant la nuit…). Également présente dans `/api/power/peaks` (`floorW`)
  ```json
  {"floor":[{"date":"2026-10-12","w":310},{"date":"2026-10-13","w":null}]}
  ```
- **`GET /api/alerts?since=<id>`** : les **20 dernières alertes**, lisibles **sans être consommées** (la file lue par `/getAlert` se vide à la lecture et prive le téléphone de ses notifications). Chaque alerte a un `id` croissant, conservé au redémarrage (`/hst/alerts.json`) ; `since` ne renvoie que les plus récentes. `level` va de 0 à 3. Une même alerte répétée dans les 10 minutes (dépassement signalé à chaque trame…) n'occupe qu'une place. Le flux existant (file, notifications, push) ne change pas
  ```json
  {"alerts":[{"id":42,"ts":"14/10/2026 18:42","type":"over_budget","level":0,
              "title":"💸🚨Dépassement de budget","message":"...","value":4200,"threshold":4000}],
   "lastId":42}
  ```
- **`GET /api/delestage`** : état du délestage. `devices` liste les prises délestées ; `cut` vaut `true` pour celles coupées par le délestage en cours. `since` / `lastEnd` : début du délestage en cours ou du dernier, fin du dernier (`null` si inconnus). `reason` : `meter` (dépassement signalé par le compteur) ou `anticipation`. `minuteVA` : moyenne de la dernière minute (-1 si inconnue). `minuteLoadPercent` : charge de la dernière minute en % de la limite — total en monophasé, **phase la plus chargée** en triphasé ; c'est sur elle que se décident le délestage anticipé et le rallumage
  ```json
  {"active":true,"since":"14/10/2026 18:42","lastEnd":"13/10/2026 07:15","reason":"meter",
   "restoreMinutes":5,"restorePercent":80,"anticipationPercent":0,
   "minuteVA":5230,"subscribedVA":9000,
   "devices":[{"ieee":"a4c1380000000001","name":"Radiateur salon","cut":true}]}
  ```
- **`POST /api/delestage/restore`** : rallume les prises coupées (bouton « Rallumer »). La commande part au tour de boucle suivant → `202 {"status":"pending"}` ; relire `/api/delestage` pour l'état
- **`GET /api/tariff`** : trois champs ajoutés **après** les champs existants, qui ne changent pas
  - `index` : attribut de l'index en cours (256…272), d'après NTARF en Standard et la période tarifaire en Historique ; `null` si inconnu
  - `price` : prix du kWh en cours, `null` s'il n'est pas saisi
  - `nextChange` : prochain changement de tarif `"HH:MM"`, `null` si inconnu. Tempo : 06:00 et 22:00. HC/HP et EJP : d'après les heures de bascule **observées** (la configuration ne contient pas de plages d'heures creuses) — connu après la première journée complète. Base : toujours `null`
  ```json
  {"show":true,"tariff":"Heures Pleines","bgColor":"#154360","isTempo":false,
   "today":{...},"tomorrow":{...},
   "index":258,"price":0.2068,"nextChange":"22:00"}
  ```

### Délestage
- **Rallumage automatique** : les prises coupées sont rallumées quand la puissance (moyenne sur 1 minute, la durée sur laquelle le Linky coupe) est restée **5 minutes sous 80 %** de la puissance souscrite (réglable dans Config → Énergie → Délestage automatique ; 0 minute = jamais). Seules les prises **allumées avant la coupure** sont rallumées, et le fichier `/config/delestage.json` note la fin (`state` à `"0"`, `dateOff`). Avant, rien ne rallumait jamais les prises
- L'état des prises avant coupure n'est plus écrasé : le compteur signale le dépassement à chaque trame, et chaque trame ré-enregistrait l'état… alors déjà « éteint ». Le fichier n'est plus réécrit qu'au début et à la fin du délestage ; une prise rallumée à la main pendant le délestage est recoupée (au plus une fois par minute)
- **Délestage anticipé** (option, désactivé par défaut) : couper les prises dès que la moyenne sur 1 minute atteint un pourcentage de la puissance souscrite, avant que le compteur ne coupe. Le seuil est forcé au-dessus du seuil de rallumage, pour éviter de couper et rallumer en boucle
- Décocher **toutes** les prises du délestage les retire bien de la configuration (la liste n'était jamais vidée)

### Corrections
- **Serveur web figé** (issue #46 : connexion acceptée, aucune réponse) : les files partagées entre la boucle principale et le serveur web (mises à jour des appareils, notifications, alertes) sont protégées ; deux tâches pouvaient les corrompre en même temps. `/getLinky` et `/readFile` répondent toujours (une erreur au lieu de laisser la requête sans réponse) ; `/getDevices` ne libère plus un verrou qu'il ne détient pas
- **ZiPulses en compteur d'eau ou de gaz** : les impulsions étaient bien reçues mais rien ne s'affichait sur le tableau de bord ; l'historique est de nouveau alimenté (sans lissage sur le temps : un compteur à impulsions se tait légitimement tant que rien n'est consommé)
- **Scan des réseaux WiFi** quand la box fonctionne en Ethernet (WiFi éteint) : la liste restait vide ou la page attendait indéfiniment. La radio est allumée le temps du scan puis éteinte
- **Coefficients gaz et eau décimaux** : relus en entier au démarrage, ils étaient tronqués (0,01 m³/impulsion devenait 0). Ils sont aussi affichés avec toutes leurs décimales (au lieu de 2) dans la page de configuration et les graphes
- **Unités gaz et eau** : « kWh » était tronqué en « kW » (2 caractères au plus) ; 7 caractères désormais
- **Seuils des alertes avancées perdus au redémarrage** (puissance, gel, anomalie journalière, fuite d'eau, durées et délais entre alertes) : enregistrés en texte, ils étaient relus comme absents et revenaient à leur valeur par défaut
- **Alertes de fuite d'eau et bilan quotidien** : la consommation d'eau était lue sur un attribut que rien n'alimente (toujours 0) ; elle est lue sur l'index (0702/0) comme le graphe, coefficient appliqué

### Mise à jour
- Flasher le firmware **et** mettre à jour le système de fichiers (`data/tp/0.json`, `data/web/js/rules.js.gz`), puis redémarrer

## v2.25

### Corrections
- **Règles Timer avec « Maintien »** (issue #45) : une règle avec un maintien supérieur à 0 ne se déclenchait **jamais**. Les règles étaient rechargées chaque minute juste avant leur évaluation, ce qui remettait le décompte à zéro. Le même défaut rendait inopérants l'**intervalle minimum** et le **max par jour**. L'état des règles est désormais conservé ; une tolérance de 2 s évite qu'un maintien de 60 s n'attende une évaluation de plus
- **« Pas vu depuis plus de 1 heure » et puissance à 0 dans l'API** (suite de l'issue #42) : l'âge du dernier contact était calculé en comparant deux horloges différentes (heure NTP de la box et horloge système). Un ZLinky qui venait d'émettre pouvait être déclaré muet : ses puissances étaient remises à 0 chaque minute. Les deux dates viennent maintenant de la même horloge
- Le journal de debug ne signale plus un appareil muet **qu'une fois**, puis « de nouveau vu » à son retour (au lieu d'une ligne par minute)
- **Graphe « Usage d'électricité » après une coupure** : toute l'énergie consommée pendant la coupure était comptée sur l'heure du retour, d'où un pic énorme. Le lissage se déclenche désormais dès 45 min sans mesure (et plus seulement au-delà de 100 kWh), et répartit l'énergie sur les heures **réellement** concernées, y compris celles de la veille pour une coupure de nuit, sans toucher aux heures déjà enregistrées

### Thermostats
- Capteur de température jugé « HS » à tort : le délai par défaut passe de **60 min à 3 h** (un capteur sur pile n'émet souvent que lorsque la température varie). Les zones réglées sur l'ancien défaut sont mises à jour automatiquement
- Un échec d'envoi **vers** le capteur (erreur D4, fréquente sur un capteur qui dort) ne suffit plus à le déclarer HS : seule compte l'absence de trame
- La **marche forcée** fonctionne même capteur HS (elle était recoupée aussitôt)
- Une **zone désactivée** affiche « Zone désactivée » (et non « Capteur HS »), et les boutons Marche / Arrêt l'expliquent au lieu de ne rien faire
- Un capteur HS affiche la raison : capteur introuvable, aucune température lue, ou aucune trame depuis X min

### LoRa 2.4 GHz
- **Rendez-vous d'appairage de retour en SF11** : le SF10 de la v2.24 empêchait d'appairer les ZLinky LoRa actuels
- **Préambule de 8 symboles** (au lieu de 16), comme le firmware ZLinky LoRa à jour : une émission de 8 symboles coûte moins d'énergie au ZLinky, ce qui fiabilise l'émission sur les compteurs à faible budget d'alimentation
- **Identification de l'objet par lecture d'attribut** : après l'appairage, la box lit le `device_id` (cluster Basic `0x0000`, attribut `0xFF00`) puis le `model` (`0x0005`) pour appliquer le bon template, au lieu de supposer un ZLinky. Un ZLinky dont le firmware ne connaît pas encore ces attributs est reconnu comme tel ; les ZLinky déjà appairés ne changent pas
- À la première ouverture d'une box sans appareil, l'**assistant d'appairage LoRa** est proposé en priorité dès qu'un module LoRa est présent (avec ou sans Zigbee)
- Outil de test `recepteur/lora_rx_quality` aligné sur le préambule de 8 symboles

### Diagnostic
- La réponse d'un appareil à une écriture d'attribut (acceptée ou refusée) apparaît dans le log série (`[WriteAttr]`)

### Divers
- Icônes `icon_1` et `icon_SNZB-01P` ramenées en 128 × 128 (447 Ko → 35 Ko chacune) : le paquet de mise à jour passe de 4,7 à 3,9 Mo et tient sur les box dont l'espace disque est plus chargé

### Mise à jour
- Flasher le firmware **et** mettre à jour le système de fichiers (icônes `data/web/img/icon_1.png` et `icon_SNZB-01P.png`), puis redémarrer

## v2.24

### Corrections
- **Puissance instantanée à 0 dans l'API** (`/getLinky`, `/getDevice`) et journal de debug rempli de « Pas vu depuis plus de 1 heure » (issue #42) : la date de dernier contact du ZLinky n'était pas mise à jour pour les trames reçues par la ZiGate en trame brute (clusters propriétaires, dont le FF66). La box le croyait muet depuis plus d'une heure et remettait ses puissances à zéro chaque minute, alors que ses données arrivaient normalement. La date est désormais mise à jour à chaque trame reçue, en Zigbee comme en LoRa (même défaut sur le ZLinky LoRa, dont la date restait celle de l'appairage)
- **Pages en erreur ou blanches** (régression v2.23) : l'encodage de la page était déclaré trop loin dans l'en-tête ; le navigateur décodait mal les scripts, qui ne se chargeaient plus (`getFormattedDate` introuvable…). Même après un CTRL + F5
- **Page Config → Règles blanche** sur une box à la mémoire chargée : la page est désormais construite en PSRAM, au lieu d'être tronquée sans aucun message
- **Règles sur un texte** (ex. tarif en cours FF66/16 avec `!=`) : la condition était toujours vraie. Les espaces de remplissage des libellés TIC (« HC ROUGE        ») et les espaces doubles sont maintenant ignorés dans la comparaison, et un attribut déclaré texte par le template est toujours comparé comme un texte (un libellé fait uniquement de lettres A à F était pris pour un nombre hexadécimal)
- Appareils de **device_id 0** (boutons On/Off) : leur template n'était jamais appliqué (ni affichage, ni bind, ni rapports)
- Une commande On/Off reçue d'un bouton n'est plus interprétée à tort comme une réponse de lecture d'attribut

### Nouveaux appareils / templates
- **SONOFF SNZB-01P** (bouton) : action `single` / `double` / `long`, utilisable dans les règles et en MQTT, niveau de batterie et tension

### LoRa 2.4 GHz
- Le **rendez-vous d'appairage** passe en **SF10** (SF11 auparavant) : temps d'émission divisé par deux, donc plus de tentatives dans la fenêtre de 30 s et une confirmation plus rapide. ⚠️ Le ZLinky LoRa doit utiliser le même SF de rendez-vous pour être appairé ; un ZLinky déjà appairé n'est pas concerné
- Nouvel outil **`recepteur/lora_rx_quality`** : firmware de test à flasher temporairement pour mesurer la qualité de réception (taux de réception, RSSI, SNR, trames perdues, bilan toutes les 30 s sur la liaison série). Écoute passive, configuration d'appairage relue sans être modifiée. La LED reste allumée tant que la réception est bonne, s'éteint après deux intervalles sans trame et clignote sur une trame en erreur CRC

### Mise à jour
- Flasher le firmware **et** mettre à jour le système de fichiers (`data/web/js/rules.js.gz`, `data/tp/0.json`, `data/web/img/icon_SNZB-01P.png`), puis redémarrer

## v2.23

### Groupes d'actions (nouvelle fonctionnalité)
- Un **groupe d'actions** est un bouton qui déclenche **plusieurs actions sur des appareils différents** (ex. « Fermeture des volets », « Absence »). Les groupes s'affichent en **tête de la page Appareils**
- Nouvelle page **Config → Groupes d'actions** : création, modification, test et suppression
- **Icônes monochromes bleu LiXee** adaptées à la domotique (volets, température, éclairage, sécurité…) : 56 icônes en 9 thèmes, choisies dans une fenêtre ouverte depuis le bouton placé à côté du nom
- Un groupe peut être déclenché **par une règle**, en action « alors » comme « sinon ». Il est désigné par son nom : supprimer un autre groupe ne décale rien. Le **résumé de la règle** décrit l'action
- Les actions d'un groupe restent liées à l'adresse IEEE des appareils : un changement d'adresse courte ne casse pas le groupe

### Envoi cadencé des commandes (groupes et règles)
- Les commandes d'un groupe ou d'une règle à plusieurs actions partaient **toutes en même temps**, ce qui saturait la ZiGate : commandes perdues sans aucun message, volets qui ne bougeaient pas
- Elles partent désormais **une par une** : chaque commande attend l'accusé de réception (ou l'échec) de la précédente, **1 s au plus**
- Appareil momentanément sans route (erreur D4) : la box attend la fin de la recherche de route avant d'envoyer la commande suivante, au lieu de lancer toutes les recherches à la fois
- ZiGate saturée : la commande est **remise en file et renvoyée** dès qu'une place se libère, au lieu d'être perdue
- Un groupe déclenché deux fois de suite n'envoie pas deux fois les mêmes commandes

### Volets roulants
- Nouvelle action **Position 0–100 %** : curseur sur les pages Appareils, fiche appareil et tableau de bord, qui suit la position réelle en direct
- La position courante s'affiche en **pourcentage** (elle apparaissait en hexadécimal au chargement de la page : « 32 » pour 50 %)
- Une règle ou un groupe sans position renseignée n'envoie rien (la valeur par défaut aurait fermé le volet)

### État radio des appareils
- **Icône discrète** lorsque le dernier envoi vers un appareil a échoué (D4 : appareil injoignable, souvent une route cassée ; E9 : pas d'accusé…), dans **Réseau → Zigbee** et sur la page **Appareils**, mise à jour en direct
- L'icône disparaît dès que l'appareil accuse réception d'une commande

### Sécurité et accès distant
- Le **tunnel** ne peut être activé que si l'**accès sécurisé HTTP** est activé **avec un identifiant et un mot de passe**. Sinon il reste suspendu (badge « Suspendu » sur la page Tunnel)
- Une box dont l'accès sécurisé était activé **sans identifiant ni mot de passe** le voit désactivé automatiquement au démarrage, le tunnel aussi, avec une alerte expliquant pourquoi
- Page **Sécurité HTTP** : une saisie invalide ne modifie plus rien (la sécurité pouvait rester activée avec des identifiants vides, rendant la box inaccessible) ; désactiver la sécurité coupe le tunnel
- **Session expirée** : une page restée ouverte revient à la page de connexion au lieu d'interroger la box en boucle (en accès distant, chaque interrogation faisait transiter la page de connexion, ~6 Ko toutes les 5 s)
- Après connexion, retour **sur la page d'origine** plutôt que sur l'accueil
- Les **widgets LiXee-Assist** continuent de se reconnecter automatiquement

### Stabilité
- **WiFi** : une absence passagère du point d'accès au démarrage ne lance plus le provisioning BLE, qui immobilisait ~64 Ko de mémoire. Le BLE n'est proposé qu'après 10 échecs de reconnexion
- Si le BLE a tout de même été chargé, la box redémarre **une seule fois**, une fois le WiFi stable, pour récupérer cette mémoire
- **Redémarrages propres** : les redémarrages logiciels (mise à jour, surveillance mémoire, récupération BLE) se terminaient par un plantage, potentiellement en pleine écriture en flash
- **Surveillance mémoire** : le redémarrage de sécurité ne se déclenche plus sur un simple pic lors de l'envoi d'une page (le seuil bas doit durer 3 s ; seul un effondrement sous 25 Ko reste immédiat)
- **Graphes des pages Énergie** : les données sont préparées en PSRAM, ce qui supprime une chute brutale de la mémoire et le redémarrage qui suivait
- **MQTT** : la vérification du serveur ne bloque plus la box (délai 1,5 s, nouvelle tentative au plus une fois par minute)
- Suppression d'un ancien circuit de réception inutilisé (~85 Ko de PSRAM libérés)

### ZLinky LoRa — option Base (mode Historique)
- L'**historique d'énergie** est alimenté (le fichier restait à zéro), avec le libellé **BASE** à l'affichage et dans l'export CSV
- Plus d'index dupliqué dans les compteurs Heures Creuses / Heures Pleines
- La bannière **tarif en cours** de la page Énergie est renseignée

### Interface
- Éditeur de règles : une mise à jour de l'interface est prise en compte **immédiatement** (l'ancien script restait en cache jusqu'à une semaine)

### Nouveaux appareils / templates
- **SONOFF SNZB-01M** (bouton Orb 4-in-1) : une action par bouton et par type d'appui (`single_button_2`, `double_button_1`, `long_button_3`, `triple_button_4`…), utilisable dans les règles et en MQTT, et niveau de batterie

### Diagnostic
- Traces `[Action]` indiquant l'origine de chaque commande (interface, tunnel, règle, groupe, thermostat) et `[Cadence]` pour l'envoi cadencé
- Au redémarrage de sécurité, la liste des requêtes du tunnel en cours est journalisée
- Thermostat : un appareil déclaré deux fois dans une zone ne reçoit la commande qu'une fois

### Documentation
- Nouveau guide **CLIM.md** : pilotage des climatiseurs par le thermostat virtuel

### Mise à jour
- Flasher le firmware **et** mettre à jour le système de fichiers (`data/web/js/rules.js.gz`, `data/tp/514.json`, `data/tp/1.json`), puis redémarrer

## v2.22

### LoRa 2.4 GHz (nouvelle fonctionnalité majeure)
- La box reçoit désormais des objets **LoRa 2.4 GHz** en plus du Zigbee — le premier étant le **ZLinky LoRa**, qui émet ses trames TIC chiffrées (AES-128)
- Un objet LoRa est un **appareil normal** : ses données passent par le même traitement que le Zigbee, sur les mêmes clusters. Pages Énergie, historiques, export CSV, MQTT et tarif du thermostat fonctionnent **sans code dédié**
- Un nouvel objet LoRa ne demande **aucun développement** : seulement son template dans `data/tp/` (son type est annoncé à l'appairage)
- **Détection automatique** des modules présents (Zigbee et/ou LoRa) et **menu adaptatif**
- Appairage AES-128, déchiffrement AES-128-CTR + contrôle d'intégrité, **multi-émetteur** (jusqu'à 4)
- Page **Réseau → LoRa** calquée sur la page Zigbee : fiches d'appareils avec **RSSI / SNR / PDR**, assistant d'appairage dédié, distinction visuelle LoRa / Zigbee dans la liste des appareils
- **Protocole v1** : le spreading factor et le canal sont négociés à l'appairage (paramètres réseau communs), réglables depuis **Config → LoRa**
- **Lecture d'attribut à la demande** : interroger un attribut précis sans attendre le cycle périodique (~3 min 40), depuis la page de configuration ou par le bouton ⟳ de chaque ligne sur la fiche de l'appareil
- Nouvelles données TIC remontées : tarif en cours (`LTARF`/`PTEC`), numéro de série (`ADSC`/`ADCO`), horodate du compteur (`DATE`), option tarifaire (`OPTARIF`/`DEMAIN`) et courbe de charge soutirée
- Fiabilisation de l'appairage : purge des interruptions radio à chaque changement de canal et ré-armement périodique de l'écoute (une fenêtre d'appairage pouvait rester « ouverte » sans jamais rien recevoir)

### Stabilité — fin des reboots par saturation mémoire
- Les pages HTML étaient **intégralement assemblées en RAM interne**, avec des réallocations sans marge : sur les pages lourdes (Énergie, Appareils) la mémoire libre tombait à ~22 Ko et déclenchait un **redémarrage de sécurité**. Elles sont désormais assemblées en **PSRAM** et servies par tranches
- Mémoire libre en fonctionnement : **~72 Ko → ~120 Ko**
- 10 pages concernées : Énergie, Appareils, Config appareils, fiche appareil, tableau de bord, LoRa (liste et config), gestionnaire de fichiers, thermostats
- **Surveillance mémoire** : le tunnel n'est plus coupé sur le simple pic de démarrage (période de grâce après le boot, et coupure uniquement si la mémoire reste durablement basse)

### MQTT
- **Correction d'une boucle de redémarrage** : un port erroné pointant vers un serveur web (typiquement **8123** = Home Assistant, ou 80) mettait la box en redémarrage permanent, **sans aucun message d'erreur**. La box vérifie désormais que la cible répond bien en MQTT avant de s'y connecter, et affiche la cause en clair
- Les identifiants ne sont **jamais transmis** à un serveur qui n'est pas un broker MQTT

### Accès distant (tunnel)
- **Activation par code réparée** : le certificat racine embarqué était corrompu, toute activation échouait en `HTTP -1`. Prise en charge de la nouvelle chaîne de certification Let's Encrypt
- Correction de **pages tronquées ou dupliquées** en accès distant (la réponse relayée n'annonçait plus sa longueur)
- Respect du protocole WebSocket sur les gros envois fragmentés — plus de page corrompue sous charge
- **File d'attente** au lieu d'un refus « serveur occupé », qui cassait le chargement de certaines pages
- Assets statiques **versionnés** : fin des bibliothèques périmées conservées en cache après une mise à jour

### Interface
- Correction du **menu dupliqué en bas de page** et des **menus déroulants inopérants** en accès distant
- Ajout du **doctype HTML5** sur toutes les pages : elles s'affichaient en mode de compatibilité, ce qui perturbait la mise en page et le fonctionnement des menus
- **Config Tunnel** : un **seul** interrupteur d'activation (il y en avait deux), les deux méthodes de configuration restant disponibles
- Nouvel outil de **lecture d'attribut à la demande** côté Zigbee (Config → Zigbee)
- Assistant d'appairage : passage à l'étape suivante fiabilisé

### Règles
- Nouveau champ **propriété** sur les conditions portant sur **STGE** (#31) : comparer un état précis (contact sec, organe de coupure, surtension, dépassement de puissance, tarif en cours…) au lieu du mot d'état entier, avec les libellés correspondants proposés dans l'éditeur

### Correctifs
- **Valeurs signées** (#34) : les grandeurs négatives s'affichaient en positif géant (65508 W au lieu de −28 W pour la puissance active ; idem facteur de puissance)
- **Attributs numériques génériques** (#37) : les clusters sans traitement dédié publiaient une chaîne hexadécimale brute, que Home Assistant interprétait en notation scientifique (300 hPa au lieu de 994)
- **STGE jamais publié en MQTT** (#36) : l'entité restait indéfiniment indisponible
- **CSRF** (#32, #33) : l'accès distant hors tunnel (DynDNS + NAT, reverse proxy, VPN, domaine perso) était rejeté en 403 — création de règles et mise à jour du firmware bloquées
- **Export CSV puissance** (#30) : la fenêtre 24 h glissante ne correspondait pas à celle du graphe
- **Thermostat** : le forçage manuel (Auto / Marche / Arrêt) était perdu à chaque redémarrage
- Un appareil appairé **à chaud** restait invisible pour certains traitements (mode Linky bloqué à 0 sur un ZLinky fraîchement appairé) ; un appareil supprimé pouvait laisser une référence invalide
- Chaque appareil LoRa dispose désormais d'une **adresse propre** : la mise à jour temps réel ne concernait auparavant que le premier de la liste
- **Page Énergie** : génération de l'historique de puissance **275 ms → ~60 ms**

### Nouveaux appareils / templates
- **NodOn SEM-4-1-00** (module de mesure : tension, intensité, puissances, facteur de puissance)
- **NodOn STPH-4-1-00** (température, humidité, batterie)

### Build / maintenance
- **Dépendances épinglées** (plate-forme ESP32 et bibliothèques du serveur web) : compilations reproductibles, plus de montée de version involontaire
- Documentation du protocole radio LoRa mise à jour (`recepteur/PROTOCOLE_LORA.md`)

## v2.21

### Thermostat virtuel (nouvelle fonctionnalité majeure)
- Régulation multi-zone découplant le **capteur de température** de l'**actionneur** piloté : la box joue le rôle de régulateur
- Actionneurs supportés : **prise/relais on/off** (cluster 0006), **climatiseur / thermostat HVAC** (cluster 0201, modes HEAT/COOL/OFF), **radiateur fil pilote** — pilotage par les actions du template
- **Plusieurs prises** par zone (commandées en parallèle)
- **Clim réversible** : actions Chaud / Froid / Arrêt distinctes, choix du mode Chaud/Froid directement sur la vignette
- Régulation **TPI** (PWM lent) pour les charges tout-ou-rien ; **hystérésis** pour les appareils pilotés par action (évite les commandes répétées / bips)
- Capteur de **présence** + capteurs d'**ouverture** (porte/fenêtre) par zone, avec inhibition de la régulation
- **Hors-gel** (sécurité + mode bascule), **forçage** marche/arrêt/auto, protection anti-court-cycle
- Modes de fonctionnement : **toujours / plages horaires / tarif Linky** (multi-périodes Base, HC-HP, EJP, Tempo)
- Vignettes en **cadran circulaire** (jauge SVG) avec animation directionnelle de l'écart consigne↔température ; état réel reflété via le System Mode HVAC
- Pages dédiées : configuration (Config → Thermostat) et visuel temps réel (Mesures → Thermostat), navigation par glissement

### Corrections Zigbee (bind / reporting)
- **Bind** : les clusters du champ `bind` des templates sont lus en **décimal** ; le parseur accepte désormais `;`, `,` et l'espace comme séparateurs (un mélange hexa/virgule empêchait le bind du cluster HVAC 0x0201 → aucun report)
- **Configure Reporting** : le *reportable change* des attributs **int16 (0x29)** est désormais envoyé sur **2 octets** (température, consignes) — auparavant tronqué à 1 octet → report **rejeté** par l'appareil
- Lecture de la **température locale HVAC** (cluster 0201 attribut 0) en plus du cluster 0402

### Performances & stabilité (accès distant via tunnel)
- Correction de **reboots watchdog** par épuisement du heap interne : limitation **adaptative** de la concurrence du tunnel selon le heap + plancher de sécurité
- Envoi des grosses réponses WebSocket en **fragments** (avec traitement des pings entre fragments) → fin des déconnexions du tunnel sur les pages volumineuses
- **Menu commun externalisé** (`/menu.js`, mis en cache navigateur) au lieu d'être réinjecté dans chaque page (~29 Ko/page économisés)
- Pop-ups d'aide de la page Énergie **externalisés** et chargés à la demande
- Requêtes AJAX de la page Énergie **échelonnées** (évite la saturation du tunnel)

### Interface
- Spinner de chargement **limité à la navigation** entre pages (plus sur les uploads de mise à jour ni les exports CSV)
- Correction du **positionnement des pop-ups sur mobile** (toujours visibles quel que soit le défilement)
- **Export CSV** des graphes « Puissance apparente » et « Usage d'électricité » (format Excel FR, données du tooltip incluses)

### Correctifs
- Correction d'un **crash (Guru Meditation)** à l'authentification lorsqu'un appareil de production était configuré mais absent des appareils

### Nouveaux appareils / templates
- Support du climatiseur **IRB-4-1-00** (cluster 0201 : HEAT/COOL/OFF/AUTO/FAN_ONLY/DRY)
- Modules Tuya : **irrigation** et **présence**

## v2.20

### Moteur de règles — refonte complète
- Nouveau moteur de règles complet avec évaluation automatique (timer ou événement)
- **9 types de conditions** :
  - `device` — comparaison d'un attribut Zigbee à un seuil, avec affichage de la valeur actuelle
  - `device_compare` — comparaison entre deux appareils Zigbee avec offset optionnel et affichage des valeurs actuelles
  - `time` — heure précise (HH:MM)
  - `time_range` — plage horaire (supporte le passage à minuit)
  - `weekday` — jour(s) de semaine (lundi–dimanche)
  - `date` — date précise, récurrente (ignorer année) ou ponctuelle
  - `datetime` — date et heure combinées
  - `day` — jour du mois (1–31)
  - `month` — mois (1–12)
- **Logique combinatoire** : chaînage ET / OU entre conditions avec évaluation court-circuit
- **3 types d'actions** :
  - `device` — envoyer une commande template à un appareil Zigbee
  - `dynamic` — lire la valeur d'un capteur source, appliquer un calcul linéaire (coefficient × valeur + offset), envoyer le résultat à un actionneur cible
  - `notification` — notification interne avec variables dynamiques (`{rule}`, `{date}`, `{value}`, `{device}`, `{threshold}`, `{value_N}`, `{device_N}`)
- **Actions SINON** : exécutées lors de la transition VRAI → FAUX (même types que les actions SI)
- **Options d'évaluation** :
  - Durée de maintien (conditions vraies en continu pendant N minutes)
  - Mode une seule fois / répété
  - Intervalle minimum entre exécutions (mode répété)
  - Limite d'exécutions par jour (max/jour)
- **Migration automatique** des anciennes `timeRanges` en conditions `time_range` + `weekday` au chargement
- Rétrocompatibilité backend pour l'ancien type d'action `onoff`
- Interface web complète : éditeur de règles avec sélection dynamique des devices/clusters/attributs, résumé en temps réel, labels en français avec accord grammatical (inférieur(e), supérieur(e))
- Documentation complète : `RULES.md`

### Sécurité
- **Protection CSRF** sur les requêtes POST : vérification du header `Origin`/`Referer` (IPs locales, mDNS, tunnel autorisés)
- **Certificat TLS Let's Encrypt** (ISRG Root X1) embarqué en PROGMEM pour les connexions HTTPS sortantes — remplace `setInsecure()`
- Remplacement systématique de `sprintf` → `snprintf` et `strcpy` → `strlcpy` pour prévenir les dépassements de buffer

### Optimisation mémoire — migration PSRAM
- `PsramAllocator` étendu avec aliases : `PsString`, `PsVector<T>`, `PsUnorderedMap<V>`, `PsStringHash`
- Type `DeviceList` (vecteur de devices en PSRAM) remplace `std::vector<DeviceData*>` partout
- `TemplateCache` : map des templates parsés migré en PSRAM
- `NotificationManager` : vecteur de notifications migré en PSRAM
- Variables globales temporelles (`FormattedDate`, `Hour`, `Day`, `Month`, `Year`, `Minute`, `Yesterday`) changées de `String` en `char[]` — réduit la fragmentation heap

### Écriture atomique des fichiers JSON
- Nouvelle fonction `atomicWriteJson()` : écriture dans un fichier `.tmp` puis renommage — protège contre la corruption en cas de coupure ou crash
- Utilisée par `energyHistory`, `notificationManager` et les sauvegardes de configuration
- Protection mutex (`file_Mutex`) ajoutée dans les opérations `SPIFFS_ini`

### Divers
- Suppression du support Marstek (config, menu, code)
- Mise à jour du template device `81.json`
- Nettoyage de `data/firmware.tar` (remplacé par `data/web.tar`)

---

## v2.19

### Tunnel reverse proxy
- Reconnexion rapide du WebSocket (3s au lieu de 10s)
- Heartbeat plus fréquent (15s au lieu de 30s) pour maintenir la connexion
- Timeout des slots augmenté à 30s pour les requêtes longues
- Nettoyage propre de l'ancien tunnel avant reconnexion WiFi
- Dimensionnement dynamique du buffer JSON selon la taille du message recu
- Appel `_ws.loop()` après les gros envois (>10KB) pour traiter les pings immédiatement
- Log amélioré en cas de déconnexion (heap, uptime)
- Envoi de notifications push via le tunnel (`sendNotification`)

### Upload OTA et mise à jour via tunnel
- Upload OTA chunké en base64 pour les mises à jour de devices via tunnel (contourne la limite de taille WebSocket)
- Upload firmware (.bin) et restore (.tar) chunkés via tunnel avec endpoints `/restoreInit`, `/restoreChunk`, `/restoreFinish` et `/fwUpdateInit`, `/fwUpdateChunk`, `/fwUpdateFinish`
- Décompression gzip côté navigateur avant envoi (`.tar.gz`)
- Acceptation des fichiers `.bin` en plus de `.tar` / `.tar.gz` sur la page de mise à jour
- Upload firmware direct en local via `/doUpdate`

### Gestion mémoire (watchdog)
- Watchdog mémoire à 3 paliers avec confirmation temporelle (5s sous seuil) :
  - < 80KB pendant 5s : arrêt du tunnel (libère ~15-20KB SSL)
  - < 60KB pendant 5s : déconnexion MQTT (libère ~10-15KB SSL)
  - < 40KB : reboot de sécurité immédiat
- Relance automatique du tunnel et MQTT quand le heap remonte au-dessus de 120KB (hystérésis)
- Cooldown de 60s entre les actions du watchdog pour éviter le flapping
- Heap guard lors de la reconnexion WiFi : si heap < 50KB, MQTT et tunnel sont différés

### Reconnexion WiFi
- `initWiFiServices()` supporte les reconnexions (distinction first init / reconnect)
- mDNS relancé proprement (`MDNS.end()` avant `MDNS.begin()`)
- Callbacks MQTT et timer créés une seule fois (première init)
- NTP, serveur web, Marstek initialisés uniquement au premier démarrage
- Suppression du double `connectToMqtt()` dans le callback WiFi (géré par `initWiFiServices`)
- Logs avec heap avant/après pour diagnostiquer les fuites mémoire

### Injection / autoconsommation (PAPP)
- Synchronisation PAPP/URMS/IRMS par timestamps au lieu d'un compteur arbitraire
- Gestion du burst ordering : si URMS/IRMS arrive dans les 15s, considéré comme même burst
- Timeout de sécurité de 2 minutes si URMS/IRMS ne se synchronise pas
- Jauge soutirée affiche 0 quand PAPP est négatif (injection en cours)
- Jauge injectée utilise la dernière valeur de `powerHistory` au lieu de l'index SINSTI
- RAZ automatique de la jauge injection quand PAPP redevient positif (`wasInjecting`)
- Support PAPP négatif direct (pas seulement PAPP==0)
- Valeur de consommation dans powerHistory forcée à 0 pendant l'injection

### Graphique énergie
- Correction de la double négation pour la production dans les configurations 2 Linky (la valeur est déjà négative depuis `handleAttribute1`)

### Interface web
- Page "Mesures des appareils" : réécriture en streaming (`AsyncResponseStream`) au lieu de concaténation String (réduit la consommation mémoire)
- Remplacement des icônes SVG `?` par des `<span>` CSS légers sur la page énergie (économie ~3KB de Flash)
- Réécriture et simplification des textes d'aide (popups) de la page énergie Linky
- Ajout du favicon sur toutes les pages (header, header graph, login)
- Affichage RSSI WiFi et TX Power sur la page réseau
- Affichage de la version firmware sur la page de login
- Noms de devices dans les messages d'erreur Zigbee (alias si disponible, sinon IEEE)

### Notifications
- Dimensionnement dynamique du JSON pour `saveToFile`, `loadFromFile` et `toJson` (proportionnel au nombre de notifications, évite les dépassements)
- Callback push configurable (`setPushCallback`) pour relayer les notifications via le tunnel

### Alertes
- Fenêtre de déclenchement élargie (plage de minutes au lieu d'une minute exacte) pour les checks quotidiens, évitant les ratés si la boucle ne tombe pas pile sur la minute
- Logs de diagnostic pour le résumé quotidien et les changements de jour

### Divers
- Reset usine désactive aussi la sécurité HTTP (permet de récupérer l'accès si mot de passe oublié)
- `WiFiEspAT` ajouté à `lib_ignore` dans platformio.ini pour éviter les conflits de compilation
- Flag de build `USE_ENERGY_V2`
