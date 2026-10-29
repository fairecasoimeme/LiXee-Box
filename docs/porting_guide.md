# Guide de portage LiXee-Box -> LiXee MatterBridge (ESP-IDF)

Portage de toute la partie web du firmware Arduino LiXee-Box vers ce projet.
Source Arduino (lecture seule) : `C:\Users\akila\Documents\PlatformIO\Projects\smart-home\LiXee-WebMQTT-Gateway\src`
(web.cpp ~24 500 lignes, routes enregistrees l. 22703-24464 ; modules : rules.cpp, actionGroups.cpp,
notificationManager.cpp, alertChecks.cpp, WebPush.cpp, mail.cpp, TunnelClient.cpp, loraReceiver.cpp,
loraModule.cpp, zigbee.cpp, protocol.cpp, mqtt.cpp, device.cpp, microtar.cpp, zigate_flasher.cpp...).
Projet cible : `C:\Users\akila\Documents\PlatformIO\Projects\smart-home\LiXee-MatterBridge`.

## Architecture retenue
- **Application web monopage + API JSON** (pas de pages HTML construites par le serveur).
- Pages existantes : `data/web/js/app.js` (routeur `#hash`). Lots 1 et 2 deja portes
  (socle, energie : voir `docs/energy_api.md`, `components/energy_service`, `components/webserver/web_energy.cpp`).

## Regles de travail (plusieurs agents en parallele)
- **Ne jamais lancer `idf.py`** ni flasher : un seul build partage, fait par l'integrateur.
  Ecrire du code qui compile du premier coup : ESP-IDF 5.5.2, ESP32-S3, g++ `-std=gnu++2b -Wall -Werror=all`
  (attention a `-Werror=format-truncation` : dimensionner largement les tampons de snprintf ; initialiser
  les `httpd_uri_t` par `httpd_uri_t u = {}; u.uri = ...` pour eviter les avertissements).
- **Fichiers reserves a l'integrateur** (ne pas modifier) : `main/main.cpp`, `main/CMakeLists.txt`,
  `components/webserver/web_api.cpp`, `web_server.cpp`, `components/webserver/CMakeLists.txt`,
  `data/web/index.html`, `data/web/js/app.js`, `sdkconfig*`, `components/energy_service/*`.
  Indiquer dans le rapport final ce qu'il faut y ajouter.
- **Chaque lot a ses fichiers** : un composant `components/<nom>/` (CMakeLists.txt, include/<nom>.h,
  <nom>.cpp), un fichier de routes `components/webserver/web_<lot>.cpp` + `.h` exposant
  `void web_<lot>_register(httpd_handle_t server);`, et un module de pages `data/web/js/page_<lot>.js`.
  Un composant existant n'est modifie que par le lot qui en est proprietaire (voir la liste des lots).

## Firmware
- Composants disponibles (noms pour REQUIRES/PRIV_REQUIRES) : `config_manager`, `device_data`, `zigate`,
  `energy_history`, `energy_service`, `ntp_sync`, `mqtt_manager`, `wifi_manager`, `eth_manager`,
  `matter_bridge`, `cluster_handlers`, `poll_engine`, `device_setup`, `webserver`, et ESP-IDF :
  `json` (cJSON), `vfs`, `esp_http_server`, `esp_http_client`, `esp-tls`, `tcp_transport`, `esp_timer`,
  `esp_event`, `esp_netif`, `esp_wifi`, `nvs_flash`, `app_update` (OTA), `esp_partition`, `mbedtls`,
  `esp_driver_spi`, `esp_driver_gpio`, `esp_driver_uart`, `log`.
- Routes : `web_server_register_uri(server, &uri)` (`components/webserver/include/web_server.h`) ajoute
  l'authentification (session/Basic) et la protection CSRF. Modele a suivre :
  `components/webserver/web_energy.cpp` (lecture de parametres, corps JSON, reponses).
  Toute action qui modifie l'etat passe par **POST** (jamais GET).
- **Memoire** : la RAM interne est rare (~50 Ko libres au minimum). Tout tampon > 1 Ko va en PSRAM
  (`heap_caps_malloc(n, MALLOC_CAP_SPIRAM)`, repli `malloc`). La pile des handlers HTTP fait 8 Ko :
  pas de gros tableaux locaux. Une tache qui n'ecrit jamais en flash peut avoir sa pile en PSRAM
  (`xTaskCreatePinnedToCoreWithCaps(..., MALLOC_CAP_SPIRAM)`) ; sinon pile interne la plus petite possible.
- **Fichiers** : LittleFS monte sur `/littlefs` (`config/`, `db/`, `hst/`, `tp/`, `debug/`, `bk/`, `rt/`,
  `ota/`). Ecriture atomique : `cfg_write_file(path, text)` (config_manager.h). Jamais de chemin construit
  depuis une entree utilisateur sans validation (refuser `..`, `/`, caracteres hors `[A-Za-z0-9._-]`).
- **Configuration** : `cfg_get_general()` / `cfg_get_general_mut()` + `cfg_save_general()`,
  `cfg_get_settings()`, `cfg_get_notifications()` (config_manager.h). Les nouveaux reglages d'un lot
  vont dans son propre fichier `/littlefs/config/<lot>.json` sauf si le lot est proprietaire de config_manager.
- **Temps** : `ntp_sync_is_synced()`, `ntp_sync_get_local_time(&tm)`, `time(NULL)`.
- **Zigbee** : `components/zigate/include/zigate_driver.h` (commandes ZiGate : lecture/ecriture
  d'attribut, on/off, bind, config report, permit join...), `zigate_devices.h` (liste des appareils),
  `device_data.h` (dernieres valeurs en hexadecimal, templates `/littlefs/tp`).
- **Energie** : `energy_service_get_live()`, `energy_service_period_cost()` (energy_service.h).
- Commentaires en francais **sans accents**, densite de commentaires comparable au code existant ;
  expliquer le pourquoi des choix non evidents et les differences avec la LiXee-Box.
- Corriger plutot que copier les defauts de l'original (traversee de chemin, GET qui modifient l'etat,
  debordements), et le signaler en commentaire.

## Pages web
- Fichier `data/web/js/page_<lot>.js`, charge apres app.js : une IIFE `'use strict'` qui utilise
  `window.LixeeApp` : `registerPage(nom, fn)`, `setContent(html)`, `showAlert(msg, type)`,
  `escHtml(s)`, `postJSON(url, obj)`, `apiErrorText(xhr)`, `pageInterval(fn, ms)` (minuteries
  arretees au changement de page), `loadScripts(list)`, `CHART_LIBS`, `params()` (parametres de
  `#page?cle=valeur`), `modules()`. jQuery et Bootstrap 5 sont charges globalement.
- Toujours echapper le texte venant du serveur (`escHtml` ou `.text()`).
- Textes de l'interface en francais avec accents (entites HTML ou `\u00e9`).
- Verifier la syntaxe : `node -e "new Function(require('fs').readFileSync('<fichier>','utf8'))"`.

## Rapport final attendu de chaque agent
1. Fichiers crees / modifies.
2. Integration a faire : lignes CMake (SRCS, REQUIRES), appels d'init dans main.cpp (ou et pourquoi),
   appel `web_<lot>_register(server)`, entrees de menu (hash, libelle, section) et balise
   `<script src="js/page_<lot>.js">` pour index.html.
3. Contrat des routes (methode, parametres, JSON).
4. Ce qui n'a pas pu etre porte ou teste, et les ecarts voulus avec la LiXee-Box.
