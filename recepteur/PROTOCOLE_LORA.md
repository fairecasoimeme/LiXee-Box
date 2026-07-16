# ZLinky TIC — Protocole radio LoRa 2.4 GHz (SX1280/SX1281)

Spécification dérivée de l'analyse exhaustive de `ZLinky_TIC_Receiver.ino` (1939 lignes).
Émetteur = ZLinky (JN5189 + SX1280) ; récepteur = ce firmware (RadioLib + AES-128 logiciel).
Flux **unidirectionnel** émetteur → récepteur (sauf OTA qui a un ACK). Les layouts émetteur sont
reconstruits depuis les parsers de réception (le firmware JN5189 n'est pas dans le dépôt).

> ⚠️ Numéros de ligne = ceux de `ZLinky_TIC_Receiver.ino`.

---

## 1. Configuration radio / PHY

`radio.begin(freq, bw, sf, cr, syncWord, power, preambleLength)` :

| Paramètre | Valeur | Ligne |
|---|---|---|
| Fréquence | par canal (2410–2480 MHz), init sur 2440 | 1580, 83-85 |
| Bandwidth | **406.25 kHz** | 337, 1581 |
| Spreading Factor | **SF11** | 337, 1581 |
| Coding Rate | **4/5** | 337, 1581 |
| Sync Word | **0x12** | 337, 1581 |
| Power TX | **10 dBm** | 337, 1581 |
| Préambule | **16 symboles** | 337, 1581 |
| CRC (PHY) | **activé, 2 octets** `setCRC(2)` | 339, 1590 |
| Header | **explicit** | 339, 1591 |
| IQ | **normal** (`invertIQ(false)`) | 340, 1592 |

```cpp
radio.begin(2440.0, 406.25, 11, 5, 0x12, 10, 16);  // SF11
radio.setCRC(2); radio.explicitHeader(); radio.invertIQ(false);
```

> ❗ **Incohérence** : l'en-tête/bannières annoncent SF10, mais le code utilise **SF11** (confirmé par les commentaires inline `/* SF11 */`). C'est SF11 la vérité.

---

## 2. Plan de canaux

| Index | Fréquence | Rôle |
|---|---|---|
| 0 | 2410 MHz | data |
| 1 | 2420 MHz | data |
| 2 | 2430 MHz | data |
| **3** | **2440 MHz** | **canal d'APPAIRAGE** (handshake) |
| **4** | **2450 MHz** | **canal opérationnel par défaut** (DEFAULT_OP_CHANNEL) |
| 5 | 2460 MHz | data |
| 6 | 2470 MHz | data |
| 7 | 2480 MHz | data |

- **Appairage** : tout le handshake PAIR_REQUEST/RESPONSE se fait sur **2440 MHz** (canal 3).
- **Opérationnel** : `opChannel`, résolu au boot par priorité (1594-1626) : EEPROM si appairé → canal forcé `C N` → `DEFAULT_OP_CHANNEL` (=4, 2450 MHz) → CCA scan si `-1`.
- **CCA scan** (288-322) : écoute 80 ms/canal, retient le RSSI le plus bas (canal le plus calme).
- **Pas de channel hopping** en fonctionnement : une fois `opChannel` choisi, le récepteur y reste. Le seul saut est appairage (2440) → opChannel après le PAIR_RESPONSE.

---

## 3. Octet d'en-tête (byte[0])

`byte[0] = (version << 4) | type` :
- **version** = bits de poids fort, attendue = **1** → high nibble `0x1`.
- **type** = bits de poids faible (`byte[0] & 0x0F`) :

| type | Nom | Chiffré ? |
|---|---|---|
| 0x01 | ESSENTIAL (données) | payload chiffré |
| 0x02 | EXTENDED (données) | payload chiffré |
| 0x03 | PAIR_REQUEST | **clair** |
| 0x04 | PAIR_RESPONSE (émis par le récepteur) | **clair** |
| 0x05 | PAIR_CONFIRM | **clair** |
| 0x06–0x0A | OTA (start/data/end/ack…) | chiffré |

`byte[1]` = **numéro de séquence** (seq, sur 1 octet, wrap mod 256), toujours en **clair**.

---

## 4. Handshake d'appairage (sur 2440 MHz)

Manuel côté récepteur (bouton long-press 3 s ou commande série `P`), fenêtre **30 s**
(`PAIR_LISTEN_TIMEOUT_MS=30000`). Multi-émetteur (jusqu'à **4**).

```
Émetteur                          Récepteur (sur 2440)
  | -- PAIR_REQUEST (11B) ------->  | extrait MAC, génère clé AES-128
  | <----- PAIR_RESPONSE (20B) ---  | clé (en clair) + op_channel
  |  (passe sur op_channel)         | passe sur op_channel
  | -- PAIR_CONFIRM (15B) -------->  | vérifie preuve AES-ECB, sauve EEPROM
```

### PAIR_REQUEST — 11 octets (type 0x03 → byte0=`0x13`)
| Off | Taille | Champ |
|---|---|---|
| 0 | 1 | version\|type (0x13) |
| 1 | 1 | seq |
| 2 | 8 | MAC émetteur |
| 10 | 1 | canal (informatif) |

#### Extension : annonce du type d'objet (14+N octets) — **LiXee-Box ≥ v2.22**

Le format 11 octets ne dit pas **quel objet** s'appaire : le récepteur devait supposer « ZLinky ».
Pour supporter d'autres objets LoRa, l'émetteur peut annoncer son type, exactement comme
l'interview Zigbee (Simple Descriptor + attribut Basic 0x0005) :

| Off | Taille | Champ |
|---|---|---|
| 0 | 1 | version\|type (0x13) |
| 1 | 1 | seq |
| 2 | 8 | MAC émetteur |
| 10 | 1 | canal (informatif) |
| **11** | **2** | **device_id** (uint16 BE) — nomme le template `data/tp/<device_id>.json` |
| **13** | **1** | **longueur du model** (N) |
| **14** | **N** | **model** (ASCII) — la clé dans ce fichier |

Exemple ZLinky : `device_id = 81`, `model = "ZLinky_TIC"` → 24 octets → template
`data/tp/81.json` → clé `["ZLinky_TIC"]`.

> ⚠️ **Le device_id doit correspondre à un template qui existe réellement.** Ce n'est pas un
> identifiant libre : c'est le **nom de fichier** du template. Pour le ZLinky, `ZLinky_TIC` est
> décliné dans **`81.json`, `97.json` et `257.json`** uniquement (contenus équivalents :
> les 36 attributs du mapping LoRa sont présents dans les trois) → **utiliser 81**.
> Toute autre valeur (ex. 83) fait échouer le chargement du template : les trames sont reçues
> et déchiffrées normalement, mais la fiche de l'appareil affiche « Aucun template disponible ».
> Le récepteur le signale au boot et à l'appairage :
> `[LoRa] ATTENTION : template /tp/83.json introuvable -> la fiche de l'appareil sera vide.`

**Rétrocompatible** : une trame de **11 octets** (sans ces champs) est traitée comme un ZLinky
(`device_id=81`, `model="ZLinky_TIC"`). Le récepteur accepte donc les anciens émetteurs.

> Intérêt : un **nouvel objet LoRa ne demande aucun code** dans le récepteur — il suffit de
> déposer son template dans `data/tp/`, comme pour un appareil Zigbee. Le couple
> (device_id, model) est mémorisé dans `/config/lora.json` à l'appairage.

### PAIR_RESPONSE — 20 octets (type 0x04 → byte0=`0x14`) *(émis par le récepteur)*
| Off | Taille | Champ |
|---|---|---|
| 0 | 1 | version\|type (0x14) |
| 1 | 1 | seq (écho) |
| 2 | 16 | **netKey** (AES-128, EN CLAIR) |
| 18 | 1 | status (0x00=OK) |
| 19 | 1 | op_channel (0..7) que l'émetteur doit adopter |

### PAIR_CONFIRM — 15 octets (type 0x05 → byte0=`0x15`)
| Off | Taille | Champ |
|---|---|---|
| 0 | 1 | version\|type (0x15) |
| 1 | 1 | seq |
| 2 | 8 | MAC (doit matcher le REQUEST) |
| 10 | 4 | proof (preuve AES-ECB, 4 octets) |
| 14 | 1 | status |

- **Génération clé** : 16 octets, entropie `analogRead(A0)`+`micros()` (faible, mais clé en clair de toute façon).
- **Preuve** = `AES-ECB( MAC(8) || 0x00×8, netKey )` ; le récepteur compare les **4 premiers octets** au champ `proof`.
- Succès → `addOrUpdateEmitter(MAC, key)`, `savePairingToEEPROM()`.

> ❗ **Incohérence** : l'en-tête dit « appairage 30 s au boot » → **faux**, c'est strictement manuel.

---

## 5. Trames de données

Multi-octets = **big-endian**. Octets [0] (type) et [1] (seq) en clair ; payload `[2..]` chiffré.
`dataLen = len - 2` (MIC retiré).

### ESSENTIAL — 17 octets clair (+2 MIC) — type 0x01 (byte0=`0x11`)
| Off | Champ | Unité |
|---|---|---|
| 0-1 | type/seq | — |
| 2-3 | SINSTS | VA (puissance app. soutirée totale) |
| 4-5 | SINSTS1 | VA (ph1) |
| 6-7 | SINSTS2 | VA (ph2) |
| 8-9 | SINSTS3 | VA (ph3) |
| 10-13 | STGE | registre statut (hex) |
| 14-15 | ADPS | VA (alerte si >0) |
| 16 | mode | mode Linky |

### EXTENDED — type 0x02 (byte0=`0x12`), **sous-type = byte[2]**

| sous-type | Nom | Taille | Champs (offsets, big-endian) |
|---|---|---|---|
| 0x00 | CURRENT_VOLTAGE | 15 | IRMS1/2/3 (mA) @3,5,7 ; URMS1/2/3 (V) @9,11,13 |
| 0x01 | ENERGY | 19 | EAST @3, EAIT @7, EASF01 @11, EASF02 @15 (U32 Wh) |
| 0x02 | ENERGY_2 | 19 | EASF03 @3, EASF04 @7, EASF05 @11, EASF06 @15 (U32 Wh) |
| 0x03 | VOLTAGE_STATS | 15 | UMOY1/2/3 (V) @3,5,7 ; IMAX1/2/3 (mA) @9,11,13 |
| 0x04 | POWER_MAX_CFG | 18 | SMAXSN @3, SMAXIN @5, SINSTI @7, CCAIN @9 (VA, U16) ; RELAIS @11 (U16 hex) ; NTARF @13, PCOUP @14, PREF @15, mode @16 (U8) |
| 0x05 | DAILY_ENERGY | 19 | EASD01..04 (J, J-1, J-2, J-3) @3,7,11,15 (U32 Wh) |
| 0x06 | COMPLEMENT | 9 | DPM1/FPM1/DPM2/FPM2/DPM3/FPM3 @3..8 (U8) |
| **0x07** | **TARIFF_LABEL** | 6+N | mode @3, code tarif @4, longueur du libellé @5 (N), libellé ASCII @6..5+N (`LTARF` en Standard, `PTEC` en Historique) |
| **0x08** | **METER_SERIAL** | 4..16 | longueur @3 (N, 0..12), numéro ASCII @4..3+N (`ADSC` en Standard, `ADCO` en Historique) |

> Conversions : énergies U32 en **Wh bruts** (kWh = /1000) ; puissances en **VA** ; courants en **mA** ; tensions en **V**. Pas d'autre coefficient.

### Sous-type 0x07 — TARIFF_LABEL (6+N octets, taille variable)

**Pourquoi.** L'attribut `FF66/16` (« Tarif en cours ») attend le **libellé texte** du tarif :
`PTEC` en Historique, **`LTARF` en Standard**. Il alimente le libellé du tarif et la couleur
Tempo publiés en MQTT. En Historique le récepteur saurait le déduire du code tarif unifié
(§7.5.1), mais **en Standard c'est impossible** : `NTARF` ne donne qu'un index, et l'index 1
vaut « Base », « HC » ou « HC Bleu » selon le contrat.

**Format** — la trame est **auto-descriptive** : elle porte son propre mode et son propre code
tarif, elle ne dépend donc pas du dernier ESSENTIAL reçu.

| Offset | Taille | Champ |
|---|---|---|
| 0 | 1 | `0x12` |
| 1 | 1 | seq |
| 2 | 1 | `0x07` |
| 3 | 1 | **mode Linky** (encodage §6.1) — dit lequel des deux libellés est transmis |
| 4 | 1 | **code tarif unifié** (encodage §7.5.1) |
| 5 | 1 | **longueur du libellé** (N, 0..16) |
| 6 | N | **libellé ASCII**, sans nul terminal — `LTARF` (Standard) ou `PTEC` (Historique) |

Le libellé est transmis **espaces de fin retirés** (la TIC pade `LTARF` à 16 caractères) ; un
caractère non imprimable coupe la chaîne.

| Mode | Valeurs typiques |
|---|---|
| Standard | `BASE`, `HEURE CREUSE`, `HEURE PLEINE`, `HC BLEU`, `HP ROUGE` |
| Historique | `TH..`, `HC..`, `HP..`, `HCJB`, `HPJR` |

Exemple (Standard, `LTARF = "HEURE CREUSE"`) → 18 octets en clair :
```
12 2A 07 01 01 0C 48 45 55 52 45 20 43 52 45 55 53 45
│  │  │  │  │  │  └──────── "HEURE CREUSE" ─────────┘
│  │  │  │  │  len=12
│  │  │  │  code tarif (NTARF=1)
│  │  │  mode=1 (Standard mono)
│  │  sous-type TARIFF_LABEL
│  seq
type EXTENDED
```

Côté récepteur : le libellé va dans `FF66/16` (« Tarif en cours (report) », lu par
`publishLinkyTariffInfo()` dans les deux modes) et, en Standard, aussi dans `FF66/512`
(« Tarif en cours Standard », là où le ZLinky Zigbee met `LTARF`).

Dès qu'un TARIFF_LABEL est reçu, le récepteur **cesse de déduire** le PTEC du code tarif : le
libellé réel prime, et surtout les deux sources écriraient en alternance sur le même attribut.


### Sous-type 0x08 — METER_SERIAL (4..16 octets, taille variable)

**Pourquoi.** La fiche de l'appareil affiche un champ **« Serial Number »** = l'attribut
Zigbee `0702/776` (`MeterSerialNumber`), que le ZLinky Zigbee renseigne avec `ADSC`
(Standard) / `ADCO` (Historique). Les deux sont aujourd'hui dans la liste des étiquettes non
transmises (§8), donc le champ reste vide en LoRa.

**Format** — même motif que TARIFF_LABEL (chaîne longueur-préfixée). Pas de champ mode :
`ADSC` et `ADCO` font tous deux 12 caractères et partagent le même champ interne côté
émetteur — le numéro est identique quel que soit le mode.

| Offset | Taille | Champ |
|---|---|---|
| 0 | 1 | `0x12` |
| 1 | 1 | seq |
| 2 | 1 | `0x08` |
| 3 | 1 | **longueur** (N, 0..12) |
| 4 | N | **numéro ASCII**, sans nul terminal — `ADSC` (Standard) ou `ADCO` (Historique) |

Exemple (`ADSC = "021761234567"`) → 16 octets en clair :
```
12 2B 08 0C 30 32 31 37 36 31 32 33 34 35 36 37
│  │  │  │  └────────── "021761234567" ────────┘
│  │  │  len=12
│  │  sous-type METER_SERIAL
│  seq
type EXTENDED
```

Le numéro étant **statique**, une cadence lente suffit — mais mieux vaut le joindre à la
rotation des sous-types que de ne l'envoyer qu'à l'appairage : un compteur remplacé est
alors vu sans avoir à ré-appairer.

> ⚠️ Côté récepteur, `0702/776` n'est **pas** une chaîne Zigbee longueur-préfixée : son
> handler (`createTextMeterData`) consomme les octets bruts. Le récepteur retire donc le
> préfixe de longueur avant d'écrire l'attribut — contrairement aux chaînes du cluster FF66
> (STGE, LTARF), où l'octet de longueur est attendu.

---


## 6. Cryptographie

- **Payload** : AES-128-**CTR**. **MIC** : AES-128-**CMAC** tronqué à **2 octets**.
- **Nonce (16 o)** = `MAC(8) || 0x00×7 || seq(1)` — dépend de **MAC + seq uniquement** (pas du type).
- **Périmètre chiffré** : `pkt[2 .. dataLen-1]` (header type+seq en clair).
- **MIC** = CMAC sur le **clair complet** `[type][seq][payload déchiffré]` (longueur `dataLen`), 2 octets = les 2 derniers du paquet.
- **Ordre réception (`decryptPacket`, 544-584)** : decrypt-then-MAC →
  1. lire MIC (2 derniers octets) ;
  2. construire nonce (MAC+seq) ;
  3. déchiffrer payload AES-CTR in-place ;
  4. calculer CMAC attendu sur le clair ;
  5. comparer 2 octets MIC.
- **Multi-émetteur** : on essaie **chaque clé** ; le bon émetteur = celui dont le MIC valide (copie `backupBuf` du ciphertext pour réessayer).
- Crypto dans `recepteur/aes128.h` : `aes128_ctr_crypt`, `aes128_cmac`, `aes128_ecb_encrypt`.

---

## 7. Réception : robustesse

- ISR DIO1 (`rxDone`) pose `rxFlag` ; traitement dans `loop()`.
- Séquence : `missedPackets += (seq - attendu) & 0xFF` ; PDR = `100*rx/(rx+miss)`.
- **Reset complet (`doFullReset`)** déclenché par :
  | Déclencheur | Seuil |
  |---|---|
  | CRC consécutifs | `CRC_ERROR_RESET_THRESHOLD = 3` |
  | Préventif | `PREVENTIVE_RESET_INTERVAL = 50` paquets OK |
  | Dérive SNR | `snr < SNR_RESET_THRESHOLD = -25.0 dB` |
- **Aucun ACK/retransmission** pour les données (fire-and-forget). ACK uniquement pour OTA (timeout 5 s).
- `startReceive(0xFFFF)` = réception continue.

---

## 8. EEPROM (256 octets)

| Adr | Taille | Contenu |
|---|---|---|
| 0 | 4 | Magic `0x4C505251` ("LPRQ", BE) — sert de version |
| 4 | 1 | op_channel |
| 5 | 1 | count (émetteurs) |
| 6 | 2 | réservé |
| 8 | 25×N | émetteurs : `[valid(1)][MAC(8)][key(16)]`, 4 slots |

Stats par émetteur = **RAM uniquement** (jamais en EEPROM).

---

## 9. Modes Linky (byte[16] de ESSENTIAL / POWER_MAX_CFG)

| Mode | Signification | Tri |
|---|---|---|
| 0 | Historique monophasé | non |
| 1 | Standard monophasé | non |
| 2 | Historique triphasé | oui |
| 3 | Standard triphasé | oui |
| 5 | Standard monophasé producteur | non |
| 7 | Standard triphasé producteur | oui |

Le mode **ne change pas le layout** binaire (les 3 phases sont toujours présentes) ; il pilote l'affichage (mono = SINSTS1 seul) et indique la pertinence de l'injection (modes 5/7).

---

## 10. Incohérences à retenir
1. **SF10 (commentaires) vs SF11 (code réel)** → utiliser **SF11**.
2. CCA : commentaire « 100 ms » vs `delay(80)` réel.
3. « Appairage 30 s au boot » : faux, appairage **manuel** (bouton/commande `P`).
4. Justification du seuil SNR basée sur SF10 (mais le seuil -25 reste valide en SF11).
