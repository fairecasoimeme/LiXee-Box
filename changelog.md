# Changelog

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
