# API de maintenance (lot 6 : sauvegarde, restauration, mise a jour)

Code : `components/maintenance` (tar, OTA, telechargement), `components/webserver/web_maint.cpp`
(routes), `data/web/js/page_maint.js` (pages `#backup` et `#update`).
Toutes les routes passent par `web_server_register_uri` (authentification + CSRF).
Les reponses d'action sont `{"status":"ok"|"error","ok":bool,"message":"...","error":"..."}`
(`error` seulement en echec ; `reboot:true` quand un redemarrage est programme, 2 s apres).

## Etat
| Methode | Route | Description |
|---|---|---|
| GET | `/api/maint/status` | `{"manual":{"state","status","progress"},"auto":{...},"reboot","busy":"none\|restore\|firmware","busyChannel","received","total","autoEnabled","updateUrl","releaseInfoUrl","developerMode","chunkSize":8192,"app":{"project","version","date","time","idf","running","boot","next","maxImageSize"}}` |
| POST | `/api/maint/status/reset` | efface les messages termines (`/resetUpdateStatus`) |
| POST | `/api/maint/abort` | annule l'operation en cours (fichiers provisoires effaces, OTA abandonnee) |

`state` : `idle`, `running`, `done`, `error` ; `progress` : -1 (rien), 0-90 reception, 95 verification, 100 fin.

## Sauvegardes (`/littlefs/bk`)
| Methode | Route | Description |
|---|---|---|
| GET | `/api/maint/backup/list` | `{"backups":[{name,size,mtime}],"files":[...json],"estimate","estimateFiles","dirs","containsSecrets":true,"httpAuth","fsTotal","fsUsed"}` |
| POST | `/api/maint/backup/create` | ecrit `bk/backup.tar` (controle d'espace, ecriture provisoire + renommage) -> `size` |
| GET | `/api/maint/backup/download?name=X[&inline=1]` | contenu de `bk/X` (.tar ou .json) |
| GET | `/api/maint/backup/export` | meme archive produite a la volee (aucune ecriture flash) |
| POST | `/api/maint/backup/delete` `{"name"}` | supprime `bk/X` |
| POST | `/api/maint/backup/restore-json` `{"name"}` | `bk/X.json` -> `db/X.json`, applique au redemarrage |

Contenu : `db/`, `config/`, `debug/`, `hst/`, `tp/`, `bk/*.json`. **`config/` contient les mots de
passe en clair** (WiFi, MQTT, SMTP, WebPush, HTTP) : rien n'est retire, pour qu'une restauration
rende un appareil fonctionnel. La page l'affiche, et signale si l'authentification HTTP est desactivee.

## Restauration / mise a jour manuelle
| Methode | Route | Corps |
|---|---|---|
| POST | `/api/maint/restore` | archive .tar brute (`application/octet-stream`, pas de multipart) |
| POST | `/api/maint/firmware` | image .bin brute |
| POST | `/api/maint/restore/init` puis `/firmware/init` | `{"totalSize":N,"filename":"x"}` |
| POST | `/api/maint/{restore,firmware}/chunk?n=I` | base64 de 8 Ko bruts (corps <= 16 Ko) -> `next` |
| POST | `/api/maint/{restore,firmware}/finish` | - |

Morceaux (tunnel, trames <= 15 Ko) : `n` doit valoir le numero attendu ; `n = attendu - 1` (reponse
perdue, morceau rejoue) repond 200 `duplicate` sans reecrire ; autre valeur : 409 avec `expected`.
Une session sans donnees depuis 2 min est consideree abandonnee. Une seule session a la fois (409).

Archive : `firmware.bin` -> partition OTA ; autres entrees -> `/littlefs/<chemin>` si le chemin est
`dir/fichier` avec dir dans `config, db, hst, tp, debug, bk, web` (plus un sous-repertoire pour `web/`),
composantes `[A-Za-z0-9._-]`, sans `..` ni chemin absolu ; les autres entrees sont ignorees et comptees.
Les fichiers sont ecrits en `<nom>~r` et renommes au demarrage suivant (`maintenance_early_init`).

Image : en-tete ESP32 verifie avant toute ecriture (magic, puce ESP32-S3, `esp_app_desc_t`, nom de
projet identique au firmware courant : un firmware LiXee-Box/Arduino est refuse), effacement
progressif, `esp_ota_end` (SHA-256 / signature) puis `esp_ota_set_boot_partition`.

## Mise a jour automatique
| Methode | Route | Description |
|---|---|---|
| GET | `/api/maint/auto/config` | `{"updateUrl","releaseInfoUrl","enabled"}` |
| POST | `/api/maint/auto/config` | `{"updateUrl","releaseInfoUrl"}` - mode developpeur requis (403) |
| POST | `/api/maint/auto/start` | lance la tache de telechargement ; suivre `auto` dans `/api/maint/status` |

Reglage stocke dans `/littlefs/config/maintenance.json` ; vide par defaut = fonction masquee (pas de
canal de publication pour l'instant). `updateUrl` : `https://` obligatoire, archive .tar (meme format
que l'`update.tar` de la LiXee-Box) ou image `.bin` (selon l'extension). Le telechargement utilise
`esp_http_client` + bundle de certificats ESP-IDF, suit 5 redirections (GitHub `releases/latest/download`),
et alimente directement la session (rien sur LittleFS). `releaseInfoUrl` (facultatif) est un JSON au
format GitHub `releases/latest` (`tag_name`, `name`, `body`, `published_at`) lu par le navigateur pour
afficher la version disponible. Exemple pour un futur depot :
`updateUrl = https://github.com/<org>/<repo>/releases/latest/download/update.tar`,
`releaseInfoUrl = https://api.github.com/repos/<org>/<repo>/releases/latest`.
Sans Secure Boot, l'authenticite du firmware ne repose que sur TLS et sur l'URL configuree.
