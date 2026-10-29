# Rapport de la nuit du 21 au 22 octobre 2026 — portage de la partie web LiXee-Box

## En bref
Toute la partie web de la LiXee-Box (sauf thermostats virtuels, client UDP et émulation
Shelly, écartés à ta demande) est portée sur le pont ESP-IDF, en application web + API JSON.
**Le firmware complet compile** (2,49 Mo, 21 % de marge dans la partition) et **les 39 pages de
l'interface s'affichent sans erreur JavaScript** (test de fumée contre une API vide).
**Rien des lots 2 à 6 n'a tourné sur la carte** : c'est la priorité de demain.

| Lot | Contenu | État |
|---|---|---|
| 1 Socle | graphiques, page Système, menu par module, connexion par session + CSRF, éditeurs de fichiers, fenêtre d'appairage Matter rouverte automatiquement | **testé sur la carte** (partiellement) |
| 2 Énergie | coûts (tarifs, abonnement, CSPE, CTA), étiquette kWh/m², jauges mono/tri + production, répartition Wh/€, tendances J-1/Mois/Année, budget, carte tarif Tempo/EJP, courbe de puissance 24 h, export CSV, gaz/eau, sous-compteurs, données Linky | compilé, testé contre une API simulée |
| 3 Appareils Zigbee | valeurs en direct, actions des templates, fiche détaillée, lecture/écriture d'attributs (dont Tuya EF00), découverte MQTT Home Assistant, coordinateur (canal, LED, mode brut, fantômes), console ZiGate, tableau de bord, assistant d'appairage | compilé |
| 4 Notifications | centre de notifications, ~25 alertes énergie/eau/gel, envoi email (SMTP SSL/STARTTLS) et WebPush, `/poll` LiXee-Assist | compilé |
| 5 Règles | moteur de règles + éditeur, groupes d'actions + barre de boutons, fichiers LiXee-Box compatibles | compilé, pages testées contre une API simulée |
| 6 Maintenance | sauvegarde/restauration tar, **mise à jour du firmware par le web (OTA)**, mise à jour automatique (URL à configurer) | compilé |
| Tunnel distant | accès par remote.lixee-box.fr (WSS), activation par code | compilé, **jamais testé contre le vrai relais** |
| LoRa | pilote SX1280, appairage, réglages, POLL, OTA des émetteurs | compilé, **aucun test radio** |

## Pour flasher (carte en mode téléchargement : BOOT maintenu, RESET, relâcher BOOT)
```
powershell -ExecutionPolicy Bypass -File tools\flash_keep_data.ps1
```
Le script relit d'abord la partition de données (sauvegarde dans `build/flash_backups/`),
y remplace les fichiers web et templates, vérifie que la base Zigbee est toujours là,
puis flashe application + données. **Configuration, appareils, historiques et association
Home Assistant sont conservés.** Ensuite : RESET.

**Après ce flash, les mises à jour suivantes pourront se faire depuis la page « Mise à jour »**
(fichier `build/lixee_matter_bridge.bin`), sans mode téléchargement.

## Ce qu'il faut tester en premier (dans cet ordre)
1. **Démarrage** : journal série, ligne « RAM interne: libre … min … » (doit rester > 30 Ko).
2. **Home Assistant** : le pont reste joignable, ZLinky toujours présent.
3. **Page Énergie** : valeurs cohérentes avec le compteur (puissance, index, coûts).
4. **Appareils** : valeurs en direct, une action marche/arrêt sur une prise
   (la commande on/off envoyée à la ZiGate était fausse jusqu'ici : corrigée dans le lot 3).
5. **Notifications** : « Notification de test », puis test email si tu as un SMTP.
6. **Sauvegarde** : créer puis télécharger une sauvegarde.
7. **Mise à jour par le web** : renvoyer le même firmware par la page « Mise à jour ».

## Relecture de fin de nuit (corrigée, recompilée, pages re-testées)
Une relecture complète du code porté a trouvé des défauts. Ils sont tous corrigés :
- **Plantage à distance** : un JSON très imbriqué (quelques centaines de `[`) faisait
  déborder la pile du serveur web, y compris sur `/login` sans être connecté.
  L'imbrication est maintenant limitée à 32 niveaux pour tout le projet.
- **Suppression d'un appareil** :
  - il y avait une double libération de mémoire et un accès à de la mémoire libérée ;
  - les emplacements ne sont plus jamais libérés et sont protégés par des verrous ;
  - toute suppression passe par la tâche ZiGate, y compris quand un appareil quitte le réseau.
- **Piles** :
  - tâche principale à 8 Ko et tâche de scrutation à 6 Ko ;
  - tampons d'envoi ZiGate statiques en PSRAM ;
  - sauvegarde du tunnel à 6 Ko.
- **Connexion** : une requête de connexion incomplète ne bloque plus le serveur
  (3 tentatives au maximum).
- **Règles** : une action n'est plus considérée comme réussie, ou renvoyée, à cause du
  statut d'une autre commande ZiGate.
- **RAM interne** :
  - les objets JSON sont alloués en PSRAM ;
  - la file d'envoi MQTT du tunnel est limitée à 16 Ko.
- **Configuration** :
  - écriture de `configGeneral.json` protégée par un verrou ;
  - les modules en arrière-plan demandent la sauvegarde, et la boucle principale l'effectue.
- **Mise à jour du firmware** :
  - chaque envoi a son propre identifiant de session ;
  - la mise à jour automatique ne peut plus mélanger ses données avec celles d'un envoi manuel.
- **Notifications** :
  - un fichier de notifications illisible est mis de côté, il n'est plus écrasé ;
  - les réglages SMTP sont copiés avant chaque envoi.
- **ZiGate** :
  - une trame tronquée est ignorée ;
  - la découverte copie l'adresse de l'appareil au lieu de garder un pointeur.
- **LoRa** : une mise à jour OTA annulée ne démarre plus.

Après corrections, avec tout compilé :
- **RAM interne statique** : 207 Ko utilisés sur 342 Ko, 135 Ko restants.
- **Firmware** : 2,49 Mo, avec 21 % de marge dans la partition.
- **Pages** : 39 pages et sous-pages testées, aucune erreur JavaScript.

## Détection des modules et LoRa dans Matter (ajouté après la nuit)
- **ZiGate détectée** dès sa première trame valide. Si elle ne répond pas au démarrage,
  la demande de version est renvoyée toutes les 30 s.
- **Menus masqués selon les modules**, comme sur la LiXee-Box :
  - Appareils et Tableau de bord : affichés si Zigbee ou LoRa est présent ;
  - Réseau → Zigbee, Console Zigbee et Groupes d'actions : seulement avec Zigbee ;
  - Configuration → LoRa : seulement avec LoRa.
- **Page Appareils** : le bouton d'ajout propose Zigbee, LoRa ou les deux, selon les modules.
- **Valeurs LoRa** : elles suivent désormais exactement le même chemin que le Zigbee
  (règles, alertes, historique d'énergie, MQTT).
- **ZLinky LoRa exposé dans Matter** comme compteur d'énergie, comme le ZLinky Zigbee :
  - créé au démarrage, ou à sa première trame s'il est appairé ensuite ;
  - son numéro d'appareil Matter est mémorisé dans `lora.json`, donc Home Assistant
    le garde après un redémarrage ;
  - supprimé de Matter quand l'émetteur est supprimé de la page LoRa.
- **À tester** : un ZLinky LoRa appairé doit apparaître dans Home Assistant avec la
  puissance et les index ; après un redémarrage, ce doit être le même appareil, pas un doublon.

## Attributs Matter construits depuis les templates
Chaque appareil Matter est désormais composé à partir des états de son template
(`components/matter_bridge/matter_profile.cpp`). Avant, il n'y avait qu'un type d'appareil,
déduit du DeviceId Zigbee, avec une dizaine d'attributs.

**Règles de liaison :**
- **Clusters Zigbee standard** (température, humidité, occupation, zone IAS, batterie, volet,
  thermostat, ventilation, compteur, mesures électriques) : liaison par cluster et attribut.
- **Clusters fabricant** (Tuya EF00, FCxx) **et entrée analogique** : liaison par la classe
  MQTT de l'état, sinon par son unité ou son nom.
- **États modifiables des clusters fabricant** : un réglage (calibration, seuils…) n'est pas
  exposé, sauf les consignes de thermostat.
- **Valeurs** : on part de la valeur du template (brute × coefficient), on la ramène à l'unité
  de base (mA → A, kWh → Wh…), puis on la convertit dans l'unité Matter.
- **Commandes depuis Home Assistant** :
  - elles passent par les actions du template : marche/arrêt, volet (monter, descendre, stop,
    position), modes, consignes et ventilation de climatisation ;
  - à défaut, le pont écrit directement l'attribut, ou le datapoint Tuya pour EF00.

**Ce que donne chaque template :**

| Template / modèle | Dans Matter |
|---|---|
| Capteurs temp./humidité (770, SNZB-02D, STPH) | température + humidité + batterie |
| ZiPulses | température + batterie (l'index eau n'est pas exposé) |
| Fuite d'eau (1026) | détecteur de fuite + batterie |
| Détecteurs de présence (SNZB-06P, Aqara, radar Tuya) | occupation (+ luminosité pour le radar) |
| Prises, relais, contacteurs, fil pilote | marche/arrêt + puissance, tension, courant, énergie selon le template |
| Compteur Tuya TS0601, SEM-4 | puissance, tension, courant, énergie |
| ZLinky (Zigbee et LoRa) | compteur : index, puissance apparente, tension et courant (phase 1) |
| Volet TS130F | volet avec position, commandes du template |
| TRVZB, tête Tuya | thermostat chauffage (+ batterie) |
| Climatiseurs AC201A, IRB-4 | thermostat chaud/froid + ventilation (+ humidité) |
| Boutons SNZB-01P, Aqara | bouton (appui simple, multiple, long) + batterie |

**Limites :**
- **Appareils à plusieurs voies** : une seule voie est exposée (la base ne distingue pas
  L1 de L2).
- **Triphasé** : les phases 2 et 3 ne sont pas exposées.
- **Pas d'équivalent Matter simple** :
  - l'énergie injectée ;
  - les modes fil pilote ;
  - la vanne d'arrosage ;
  - le bouton SNZB-01M ;
  - les champs TIC spécifiques.
- **Appareil déjà associé** : son numéro d'endpoint est conservé, mais sa composition change.
  Il faudra peut-être relancer l'interrogation de l'appareil dans Home Assistant pour voir
  les nouvelles entités.

## Changements notables hors pages
- RAM interne : la table des appareils Zigbee (17 Ko) passe en PSRAM ; au total les nouveaux
  modules n'ajoutent que ~5 Ko de RAM interne statique, le reste est en PSRAM.
- Lecture de `configGeneral.json` : les valeurs écrites en texte par la LiXee-Box ("1",
  "4000") sont maintenant comprises (avant : toutes les alertes lues désactivées).
- Enregistrement de `configGeneral.json` : les clés inconnues du pont sont conservées
  (avant : effacées à chaque sauvegarde).
- Valeurs Zigbee stockées avec leur type ZCL (températures négatives, textes).
- Sockets réseau : 10 → 16 (tunnel).

## Écarts voulus avec la LiXee-Box (sécurité et corrections)
- Toute action qui modifie l'état passe par POST (la LiXee-Box utilisait souvent GET,
  jusqu'à l'effacement de la PDM).
- Chemins de fichiers validés partout (plusieurs enregistrements de la LiXee-Box
  acceptaient `..`).
- Tunnel : certificat du relais vérifié (la LiXee-Box ne vérifiait pas).
- Mise à jour : l'image est contrôlée (ESP32-S3, nom du projet, SHA-256) avant
  basculement ; un firmware LiXee-Box Arduino est refusé.
- Restauration : appliquée au redémarrage (jamais de fichier à moitié écrasé).
- Défauts de l'original corrigés et signalés en commentaire dans le code
  (alerte gel jamais déclenchée sous 0 °C, mois précédent jamais trouvé, etc.).

## Pas porté / à faire
- Superposition des présences sur le graphique d'énergie, fenêtres d'aide de la page Énergie.
- Mises à jour OTA des appareils Zigbee (serveur OTA via la ZiGate) et reprogrammation
  de la ZiGate par le web.
- Thermostats virtuels, client UDP, émulation Shelly (écartés).
- Compression des fichiers JavaScript (gain ~400 Ko de flash, non urgent).
- Nettoyage du code mort dans `app.js` (anciennes pages remplacées par les modules).
- Le pont ne bascule pas en WiFi si le câble Ethernet est débranché en cours de route.

## Documents
- `docs/porting_guide.md` : conventions du portage.
- `docs/energy_api.md`, `docs/maintenance_api.md` : contrats d'API.
- Contrats des autres lots : en tête de `components/webserver/web_*.cpp`.
