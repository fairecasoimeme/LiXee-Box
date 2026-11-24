# Appareils IP (Tasmota, Shelly...) : conception

But : piloter et mesurer des appareils WiFi/Ethernet qui ont une **API HTTP locale**, comme les appareils Zigbee et LoRa :
- fiche et page Appareils ;
- règles et groupes d'actions ;
- délestage et thermostats ;
- historique d'énergie et sous-compteurs ;
- MQTT et Matter.

Le module est **générique**, piloté par des templates. Tasmota, Shelly ou ESPHome (avec son serveur web) ne sont que des templates. Aucun nom de produit ne figure dans le code.

Les points **[À VALIDER]** sont résumés au §10.

---

## 1. Principe : un appareil IP ressemble à un appareil LoRa

Le LoRa est déjà intégré sans radio Zigbee :
- une clé de type IEEE ;
- ses valeurs rangées dans `device_data` comme des attributs ZCL ;
- puis le même chemin que le Zigbee (`process_attribute` : règles, historique, MQTT, Matter).

Les appareils IP suivent exactement ce modèle :

```
appareil IP --HTTP GET (relevé)--> composant ip_devices --valeurs ZCL--> process_attribute() (main.cpp)
                                                                         ├─ device_data, règles, historique, MQTT
                                                                         └─ Matter (matter_profile, comme le LoRa)
règle / groupe / délestage / thermostat / Matter / page --commande--> ipdev_send_action() --HTTP GET--> appareil
```

**Les valeurs prennent des attributs ZCL standard**, pour que le reste de la box les reconnaisse sans code particulier :

| Mesure | Cluster / attribut | Reconnu par |
|---|---|---|
| Relais (état) | 0006 / 0000 | Matter OnOff, règles, délestage |
| Puissance active (W) | 0B04 / 050B | Matter, page énergie |
| Tension (V), courant (A) | 0B04 / 0505, 0508 | Matter |
| Énergie (index, Wh) | 0702 / 0000 | historique d'énergie, **sous-compteurs** |
| Température, humidité | 0402 / 0000, 0405 / 0000 | thermostats, Matter |

## 2. Identité

- **Clé `device_data`** : un EUI-64 dérivé de l'adresse MAC de l'appareil, `AA:BB:CC:FF:FE:DD:EE:FF`, selon la conversion standard MAC → EUI-64. Elle est unique et stable si l'appareil change d'IP. La MAC est lue à l'ajout (Tasmota `Status 5`, Shelly `/shelly`).
- **Adresse affichée** : l'IP, et un nom d'hôte si l'appareil en donne un.
- **Liste persistante** : `/littlefs/db/ip_devices.json`, sauvegardée et restaurée comme `db/`. Pour chaque appareil :
  - la clé, l'IP et le port ;
  - le modèle (clé de template) ;
  - les identifiants : rangés comme les autres secrets de la box (clés `pass` et `key`, masquées par le gestionnaire de fichiers), jamais renvoyés par l'API ;
  - l'intervalle de relevé ;
  - la clé de « réveil » (§6) ;
  - l'endpoint Matter.

## 3. Templates

Même format que `data/tp/*.json`, pour que `device_data_load_template()`, la fiche et `matter_profile_build()` fonctionnent tels quels. Des champs `http` s'ajoutent. **[À VALIDER]** Fichier `data/tp/ip.json`, une entrée par modèle, dans le dossier des templates existant.

```jsonc
"Tasmota-Prise": [{
  "http": {
    "poll": ["/cm?cmnd=Status%200"],        // une ou plusieurs URL relevées à chaque tour
    "auth": "tasmota",                        // none | basic | digest | tasmota (?user=&password=)
    "detect": { "url": "/cm?cmnd=Status%200", "has": "StatusSNS.ENERGY" },
    "mac": "StatusNET.Mac"
  },
  "status": [
    { "name": "Etat",      "cluster": "0006", "attribut": 0,    "json": "StatusSTS.POWER", "map": { "ON": 1, "OFF": 0 } },
    { "name": "Puissance", "cluster": "0B04", "attribut": 1291, "json": "StatusSNS.ENERGY.Power",   "unit": "W" },
    { "name": "Tension",   "cluster": "0B04", "attribut": 1285, "json": "StatusSNS.ENERGY.Voltage", "unit": "V" },
    { "name": "Courant",   "cluster": "0B04", "attribut": 1288, "json": "StatusSNS.ENERGY.Current", "scale": 1000, "coefficient": 0.001, "unit": "A" },
    { "name": "Energie",   "cluster": "0702", "attribut": 0,    "json": "StatusSNS.ENERGY.Total",   "scale": 1000, "unit": "Wh" }
  ],
  "action": [
    { "name": "On",     "command": 146, "value": 1, "http": "/cm?cmnd=Power%20On" },
    { "name": "Off",    "command": 146, "value": 0, "http": "/cm?cmnd=Power%20Off" },
    { "name": "Toggle", "command": 146, "value": 2, "http": "/cm?cmnd=Power%20Toggle" }
  ]
}]
```

- **`json`** : chemin dans la réponse, points pour les objets et `[n]` pour les tableaux. `*` sert de joker pour un niveau (`StatusSNS.*.Temperature`, quand le nom du capteur varie : AM2301, DS18B20...).
- **`scale`** : multiplicateur avant rangement, pour garder des entiers comme le ZCL. `coefficient` reste celui de l'affichage, comme dans les templates Zigbee.
- **`map`** : texte → nombre (`"ON"`/`"OFF"`, `true`/`false`).
- **Les actions reprennent les numéros de commande des templates Zigbee** (146 = On/Off, comme `16.json`...). Une règle, le délestage ou Matter qui demandent « Off » trouvent donc la même action, et seule l'exécution change (HTTP au lieu de radio).

Templates du premier lot :
- Tasmota relais (1 à 4 voies : `POWER1`...) ;
- Tasmota prise avec mesure ;
- Shelly Gen2 / Plus (`/rpc/Switch.GetStatus?id=0` : `output`, `apower`, `voltage`, `current`, `aenergy.total`) ;
- Shelly Gen1 (`/status`).

## 4. Relevés

- **Une tâche `ipdev`.** Pile interne d'environ 6 Ko, tampon de réponse de 8 Ko en PSRAM. Elle interroge **un appareil à la fois**, avec `esp_http_client`, en HTTP simple sur le réseau local, sans TLS.
- **Intervalle :** 15 s par défaut, réglable par appareil (5 s à 10 min). Après 3 échecs : « injoignable » et attente doublée jusqu'à 5 min, comme le statut D4 du Zigbee.
- **Délai :** 3 s par requête. Un appareil lent ne bloque pas les autres plus de 3 s.
- **Rangement :** une valeur est rangée et propagée (`process_attribute`) quand elle change, et au moins toutes les 5 minutes. On évite ainsi d'inonder MQTT et l'historique, comme les rapports Zigbee.
- **Mémoire :** pas de connexion permanente. Pendant un échange, environ 10 Ko de RAM interne pour lwIP et le client, puis plus rien. La liste des appareils est en PSRAM.

## 5. Commandes

Un point d'entrée : `esp_err_t ipdev_send_action(uint64_t ieee, int command, uint8_t endpoint, int32_t value)`.
- Il cherche dans le template l'action qui a cette commande et cette valeur, puis appelle son URL `http`.
- Juste après la commande, il relève l'appareil pour mettre l'état à jour.

À brancher là où une commande part aujourd'hui vers la radio Zigbee, avant la recherche de l'appareil Zigbee :

| Fichier | Appel aujourd'hui |
|---|---|
| `components/rules/rules_exec.cpp` (`exec_radio_action`) | règles et groupes d'actions |
| `components/delestage/delestage.cpp` | délestage |
| `components/thermostat/thermostat.cpp` | thermostats virtuels (relais de chauffage) |
| `components/matter_bridge/matter_device_manager.cpp` | commandes reçues de Matter |
| `components/webserver/web_zigbee.cpp` | boutons d'action de la fiche |

Il n'y a pas d'ajout dans la table Zigbee (`zigate_devices`). Ce serait plus court, mais la scrutation, les liaisons et la découverte radio essaieraient alors de parler à ces appareils par la radio.

## 6. Réveil (remontée immédiate) **[À VALIDER]**

- L'appareil appelle la box quand son état change :
  - Tasmota : règle `WebSend` ;
  - Shelly : « Actions » (URL appelée sur changement d'état).
- **Route :** `GET /api/ip/wake?id=<clé>&k=<clé de réveil>`. Elle ne transporte aucune donnée : elle déclenche seulement un relevé immédiat de l'appareil. On a donc un seul format à maintenir (le relevé) et rien à analyser de ce que l'appareil envoie.
- **Sécurité :**
  - clé aléatoire de 128 bits par appareil ;
  - réseau local seulement ;
  - **refusée via le tunnel** ;
  - au plus un réveil par appareil toutes les 2 s.
  - Elle passe sans session HTTP, puisque l'appareil ne peut pas se connecter : la clé tient lieu d'authentification.
- La page de l'appareil affiche la commande prête à copier (règle Tasmota, URL d'action Shelly).

## 7. Pages

- **Réseau → « Appareils IP »**, à côté de LoRa :
  - liste avec IP, modèle, état, dernier relevé et erreurs ;
  - ajout par IP, avec **détection automatique du modèle** et choix manuel possible ;
  - test de connexion, identifiants, intervalle ;
  - URL de réveil, suppression.
- **Appareils :** les appareils IP apparaissent avec la mention « IP », comme le LoRa (`"radio":"ip"` dans `/api/zigbee/devices/full`). Fiche, alias et actions sont communs.
- **Config Énergie → sous-compteurs :** une prise IP avec un index (0702/0000) est proposée, comme un compteur Zigbee.

## 8. API

| Méthode | Route | Rôle |
|---|---|---|
| GET | `/api/ip/devices` | liste et état |
| POST | `/api/ip/probe` `{ip, port, user, pass}` | détection : modèle proposé, MAC, nom |
| POST | `/api/ip/add` `{ip, port, model, user, pass, interval}` | ajout |
| POST | `/api/ip/update` `{id, ...}` | modification (identifiants vides : inchangés) |
| POST | `/api/ip/delete` `{id, removeData}` | suppression (et endpoint Matter) |
| POST | `/api/ip/poll` `{id}` | relevé immédiat |
| GET | `/api/ip/wake?id=&k=` | réveil (§6) |

Toutes ces routes demandent la session (sauf `/api/ip/wake`) et passent par les contrôles du tunnel. Les routes `POST` font des requêtes HTTP vers le réseau local depuis la box. Via le tunnel, `/api/ip/probe` est donc limitée à des adresses **privées** (RFC 1918), pour que la box ne serve pas à sonder Internet.

## 9. Activation et coût

- **Compilation :** option Kconfig `LIXEE_IP_DEVICES`, activée par défaut. Désactivée, rien n'est compilé.
- **Exécution :** module désactivé par défaut (`modules.ip` dans `/api/system/info`, case dans la page Réseau). Désactivé, la tâche n'est pas créée.
- **Ressources :** une tâche, environ 10 Ko de RAM interne pendant un échange, environ 1 Ko de PSRAM par appareil. Au plus **32 appareils**.

## 10. Points validés (21/11/2026, priorité aux Shelly)
1. Templates dans `data/tp/ip.json`, au même format que les templates Zigbee, avec des champs `http` (§3), plutôt qu'un dossier à part.
2. Clé = EUI-64 dérivé de la MAC (§2).
3. Pas d'ajout dans la table Zigbee : un point d'entrée `ipdev_send_action()` branché aux 5 endroits du §5.
4. Réveil par `/api/ip/wake` qui ne fait que déclencher un relevé (§6). Il est refusé via le tunnel et authentifié par une clé propre à chaque appareil.
5. Premier lot : Tasmota (relais, prise avec mesure, capteurs simples), Shelly Gen2 / Plus et Shelly Gen1. ESPHome plus tard.
6. Module désactivé par défaut, au plus 32 appareils, relevé toutes les 15 s par défaut.

## 11. Découpage proposé
- **Lot 1 :**
  - composant `ip_devices` (liste, tâche de relevé, templates `http`, `process_attribute`) ;
  - routes `/api/ip/*` sauf le réveil ;
  - page « Appareils IP » ;
  - apparition dans Appareils ;
  - actions On/Off/Toggle depuis la fiche et les règles.
- **Lot 2 :** délestage, thermostats, Matter, sous-compteurs.
- **Lot 3 :** réveil `/api/ip/wake`, autres templates (capteurs Tasmota, ESPHome).

## 12. État du lot 1 (21/11/2026)

**Fait :**
- **Composant `components/ip_devices`** :
  - option Kconfig `LIXEE_IP_DEVICES` ;
  - liste `db/ip_devices.json` ;
  - tâche `ipdev` : relevés, file des commandes, attente doublée en cas d'échec ;
  - authentification `basic` / `digest` (SHA-256, Shelly Gen2) / `tasmota` ;
  - chemins JSON avec `[n]` et `*` ;
  - détection avec conditions `path` et `gt`.
- **Templates `data/tp/ip.json` :**
  - `Shelly-Gen2-PM`, `Shelly-Gen2`, `Shelly-Gen1-PM`, `Shelly-Gen1` ;
  - `Tasmota-Prise`, `Tasmota-Relais`.
- **Routes `/api/ip/*`** (`web_ipdev.cpp`) : `devices`, `models`, `probe`, `add`, `update`, `delete`, `poll`, `enable`.
- **Page « Appareils IP »** (`page_ipdev.js`, menu Réseau).
- **Page Appareils** : étiquette « IP », pastille « injoignable », fiche, boutons d'action.
- **Commandes** : éditeur de règles et groupes d'actions (listes et exécution `rules_exec.cpp`).

**Testé sur la LiXee-Box Pro** avec un modèle de test qui relève la propre API de la box (Basic) :
- relevés et valeurs dans la fiche ;
- boutons d'action ;
- groupe d'actions ;
- erreurs de détection : adresse publique, rien à l'adresse, appareil non reconnu.

**Pas encore testé sur un vrai Shelly ni sur un Tasmota**, en particulier la détection et l'authentification Digest des Gen2.

**Limites du lot 1 :**
- une seule voie par appareil (`id=0`, `relays[0]`) : pas de Shelly 2PM ni de 2.5 ;
- pas de Matter, délestage, thermostats ni sous-compteurs (lot 2).

## 13. État du lot 2 (24/11/2026)

**Fait :**
- **`ip_devices`** :
  - `ip_devices_has_cluster()` : 0006 relais, 0402 température, 0702 index ;
  - `ip_devices_send_onoff()` et `ip_devices_send_named_action()` ;
  - rappel de suppression ;
  - champs `onoff`, `temperature`, `energy` et `matterEndpoint` dans `/api/ip/devices` ;
  - pile de la tâche passée à 7 Ko, comme celle du LoRa, pour créer un endpoint Matter.
- **Délestage** : une prise IP peut être coupée et remise. La page Config la propose avec les prises Zigbee.
- **Thermostats virtuels** :
  - appareil commandé IP, en marche/arrêt ou par action nommée du template ;
  - sonde IP, fraîche tant que l'âge du dernier relevé est sous le délai de la zone ;
  - les deux sont proposés dans les listes de la zone.
- **Matter** :
  - l'appareil est ponté d'après son template, comme un émetteur LoRa (`matter_device_manager_map_lora_device`) ;
  - endpoint mémorisé dans `ip_devices.json` et repris au démarrage ;
  - créé au premier relevé s'il est ajouté après le démarrage ;
  - retiré à la suppression, remis par la page Matter.
  - Les commandes On/Off d'un contrôleur passent par `matter_device_manager_set_ext_cmd_cb()`, puis `ip_devices_send_action()`.
- **Sous-compteurs** : une prise IP avec un index 0702/0000 est proposée (`/api/energy/submeters/eligible`).
- **Page Config** : les sondes de température IP sont proposées.

**Testé sur la LiXee-Box Pro** avec le modèle de test qui relève la box elle-même. Ses actions ON/OFF appellent `/setThermostatSetpoint`, ce qui laisse une trace visible :
- une zone forcée en marche puis à l'arrêt appelle bien les URL ON et OFF (consigne passée à 21, puis à 17 °C) ;
- l'appareil apparaît dans les sous-compteurs éligibles, les choix des thermostats et la liste du délestage.

**Testé avec un vrai Shelly Pro 3EM** (firmware 1.4.4, profil triphasé, sans authentification) :
- détecté comme « Shelly Pro 3EM (triphasé) » ;
- puissances, tensions, courants et énergies identiques à ce que renvoie le Shelly ;
- proposé en sous-compteur ;
- ponté dans Matter à son premier relevé (endpoint 11), endpoint repris après redémarrage.

**Corrigé pendant ce test :**
- `tpu/ip.json` remplaçait entièrement `tp/ip.json` (un seul modèle personnel faisait perdre tous les autres). Les deux fichiers sont maintenant fusionnés modèle par modèle, comme dans `device_data`.
- Le nom du modèle perdait ses accents dans la fiche (« triphas␣␣ ») : l'UTF-8 est conservé.

**Non testé :**
- l'affichage des valeurs dans un contrôleur Matter (le Pro n'est appairé à aucun contrôleur) ;
- une vraie coupure de délestage, déclenchée seulement par le compteur ;
- un Shelly avec authentification (Digest), le profil monophasé, un Tasmota.
