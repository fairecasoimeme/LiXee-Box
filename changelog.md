# Changelog

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
