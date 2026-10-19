# Protocole LoRa ZLinky ⇄ LiXee-Box

Spécification des trames radio entre un **ZLinky LoRa** (émetteur, JN5189 + SX1280)
et le **récepteur** (LiXee-Box / passerelle).

Version protocole : **v1** — document révisé en octobre 2026 (firmware ZLinky LoRa 1.0.0, LiXee-Box v2.26).

---

## 1. Couche radio

| Paramètre | Valeur |
|---|---|
| Chip | SX1280 / SX1281 (LoRa 2.4 GHz) |
| Spreading Factor | **négocié à l'appairage** (7..12) — **SF11** par défaut sur la LiXee-Box, SF10 si le budget d'alimentation du compteur est juste (cf. §4.2) |
| Bandwidth | **BW 406 kHz** |
| Coding Rate | **4/5** |
| Preamble | **8 symboles** (encodé `0x08`) — ⚠️ le récepteur doit utiliser la même valeur |
| Header | Explicit |
| CRC | ON |
| IQ | Normal |
| Sync Word | registre `0x0944` = `{0x14, 0x24}` (équivalent RadioLib `setSyncWord(0x12)`) |
| TX Power | **+10 dBm** — limite ETSI EU (EN 300 440) pour la bande 2.4 GHz ISM |
| Regulator | DC-DC (`SetRegulatorMode 0x01`) |

> ⚠️ **Le préambule est passé de 16 à 8 symboles** — le `preambleLength` du
> récepteur doit être **≤ 8**, sinon il attend des symboles qui n'arrivent
> jamais et ne détecte **plus aucune trame** (panne nette, pas dégradation).
>
> **Pourquoi** : à SF11/BW406 le préambule coûte 102 ms à 16 symboles contre
> 62 ms à 8, soit **1,8 mA·s économisés sur chaque trame**. Le budget
> d'alimentation de la prise TIC limite la charge par émission, et certains
> compteurs sont juste au-dessus du seuil. Mesures terrain sur un Linky
> triphasé (SF11, +10 dBm) :
>
> | Préambule | Charge ESSENTIAL | Trames émises avec succès |
> |---|---|---|
> | 16 symboles | 11,0 mA·s | **36 %** (9/25) |
> | **8 symboles** | **9,1 mA·s** | **100 %** (91/91, 0 perte au récepteur) |
>
> La sensibilité ne dépend que du SF/BW/CR : 8 symboles ne coûte pas de
> portée, seulement un peu de marge de détection à très faible SNR. LoRaWAN
> utilise 8 symboles pour tous ses uplinks.

### Plan de fréquences (8 canaux)

| Canal | Fréquence | Note |
|---|---|---|
| 0 | 2 410 MHz | bas de bande |
| 1 | 2 420 MHz | |
| 2 | 2 430 MHz | |
| **3** | **2 440 MHz** | **PAIR_CHANNEL** (canal d'appairage, fixe et connu des deux côtés, **SF11**) |
| 4 | 2 450 MHz | |
| 5 | 2 460 MHz | |
| 6 | 2 470 MHz | |
| 7 | 2 480 MHz | haut de bande |

L'appairage se fait **toujours sur le canal 3**. Le récepteur assigne ensuite un
**op_channel** (0..7) que l'émetteur utilisera pour toutes les trames de données.

---

## 2. Format général d'une trame

```
┌────────┬────────┬───────────────────────────┬─────────┐
│ byte 0 │ byte 1 │      payload chiffré      │  MIC    │
│ ver|typ│  seq   │      (AES-128-CTR)        │  (2 B)  │
└────────┴────────┴───────────────────────────┴─────────┘
   clair    clair                                  clair
```

- **byte[0]** : `(version << 4) | type` — version = `0x1`
- **byte[1]** : `seq` — compteur de séquence uint8, incrémenté à chaque trame,
  wrap naturel à 255→0
- **byte[2..N-3]** : payload, **chiffré AES-128-CTR**
- **byte[N-2..N-1]** : MIC = 2 premiers octets du CMAC

> ⚠️ Les trames de **pairing** (types `0x03`, `0x04`, `0x05`) ne sont **PAS chiffrées**
> ni MIC-ées : elles circulent en clair.

### Types de paquets (byte[0] bits [3:0])

| Type | Nom | Sens | Chiffré |
|---|---|---|---|
| `0x01` | DATA_ESSENTIAL | device → box | ✅ |
| `0x02` | DATA_EXTENDED | device → box | ✅ |
| `0x03` | PAIR_REQUEST | device → box | ❌ |
| `0x04` | PAIR_RESPONSE | box → device | ❌ |
| `0x05` | PAIR_CONFIRM | device → box | ❌ |
| `0x06` | OTA_START | box → device | ✅ |
| `0x07` | OTA_BLOCK | box → device | ✅ |
| `0x08` | OTA_END | box → device | ✅ |
| `0x09` | OTA_ACK | device → box | ✅ |
| `0x0A` | OTA_STATUS | device → box | ✅ |
| `0x0B` | POLL_REQUEST | box → device | ✅ |
| `0x0C` | POLL_RESPONSE | device → box | ✅ |

Donc `byte[0]` vaut `0x11` pour ESSENTIAL, `0x12` pour EXTENDED, `0x13` pour
PAIR_REQUEST, etc.

---

## 3. Chiffrement

### AES-128-CTR

- **Clé** : AES-128 partagée, échangée au pairing
- **Nonce / compteur initial (16 octets)** :

```
┌──────────────────────┬──────────────────┬──────┐
│   MAC émetteur (8B)  │  0x00 × 7        │ seq  │
│      [0..7]          │    [8..14]       │ [15] │
└──────────────────────┴──────────────────┴──────┘
```

- Le payload est padé à un multiple de 16, chiffré, puis tronqué à la longueur utile.
- **Header (`type`, `seq`) non chiffré** — nécessaire pour reconstruire le nonce.

### MIC (CMAC tronqué)

- Calculé en **AES-128-CMAC sur le message EN CLAIR**, header inclus (`type`+`seq`+payload).
- On garde les **2 premiers octets** du tag CMAC.

### Ordre des opérations

**Émission :**
1. Calculer `MIC = CMAC(clair[0..N-1], key)[0..1]`
2. Chiffrer `clair[2..N-1]` en AES-CTR
3. Appender `MIC` → longueur finale `N + 2`

**Réception :**
1. Extraire `MIC_reçu` = 2 derniers octets
2. Déchiffrer `pkt[2..N-3]` en AES-CTR (CTR est symétrique)
3. Calculer `MIC_attendu = CMAC(clair, key)[0..1]`
4. Comparer — si différent, **jeter silencieusement**

---

## 4. Appairage

L'appairage se fait sur un **rendez-vous fixe : canal 3 + SF11**, quelle que soit
la configuration opérationnelle. C'est ce qui permet à un ZLinky qui tourne en
SF7/canal 7 de revenir se faire entendre pour renégocier. Le récepteur ouvre une
fenêtre de 30 s sur ce rendez-vous ; l'émetteur envoie un PAIR_REQUEST toutes les
300 ms et écoute 200 ms après chaque envoi.

> ⚠️ **Le SF du rendez-vous doit être identique côté récepteur**, sinon plus
> aucun appairage n'est possible — il n'y a pas de négociation à ce stade.
>
> **SF11 est à la limite du budget d'alimentation TIC.** Mesures terrain à
> +10 dBm, deux compteurs au même endroit et à la même distance du récepteur :
>
> | Rendez-vous | Charge par PAIR_REQUEST | Monophasé | Triphasé |
> |---|---|---|---|
> | SF11 | 45 mA × 247 ms = 11,1 mA·s | ✅ | ❌ rien reçu |
> | SF10 | 45 mA × 124 ms = 5,6 mA·s | ✅ | ✅ |
>
> Le budget limite la **charge** par émission (courant × airtime), pas le
> courant instantané, et il **varie selon le compteur** : le seuil est entre
> ~7 et ~11 mA·s. SF11 tient donc sur les installations favorables et lâche
> sur les autres. Passer le rendez-vous en SF10 le rend fiable partout, pour
> −2,5 dB de sensibilité et sans toucher à la puissance.

**C'est l'appairage qui fixe le SF et le canal opérationnels.** Ce sont des
paramètres **réseau**, communs à tous les ZLinky d'un même récepteur, pas des
réglages par appareil. Pour les changer, on **refait un appairage** (appui long).

```
device                                box
  │         (rendez-vous : canal 3, SF11)
  │──── PAIR_REQUEST ────────────────▶│
  │                                    │  génère clé AES-128
  │                                    │  choisit op_channel + op_SF
  │◀─── PAIR_RESPONSE ─────────────────│  (clé + op_channel + op_SF)
  │                                    │
  │   bascule sur (op_channel, op_SF)  │  bascule sur (op_channel, op_SF)
  │                                    │
  │──── PAIR_CONFIRM ────────────────▶│
  │      (sur op_channel / op_SF)      │  vérifie la preuve AES
  │                                    │  ✅ appairé
```

### 4.1 PAIR_REQUEST — device → box

**Format de base (11 octets)** — **celui qu'émet le firmware actuel** :

| Offset | Taille | Champ |
|---|---|---|
| 0 | 1 | `0x13` (version 1, type PAIR_REQUEST) |
| 1 | 1 | seq |
| 2 | 8 | MAC émetteur (big-endian) |
| 10 | 1 | canal (informatif, toujours `0`) |

**Format étendu (14+N octets)** — **n'est plus émis**. Il annonçait le type de l'objet dans la
demande d'appairage, ce qui la faisait passer de 11 à 24 octets : trop pour le budget
d'alimentation de certains compteurs (cf. §8, cluster Basic). Le type se lit désormais par
POLL après l'appairage. La LiXee-Box (≥ v2.22) accepte encore ce format, pour les firmwares
intermédiaires qui l'émettent :

| Offset | Taille | Champ |
|---|---|---|
| 0 | 1 | `0x13` |
| 1 | 1 | seq |
| 2 | 8 | MAC émetteur |
| 10 | 1 | canal (informatif) |
| **11** | **2** | **device_id** (uint16 BE) — nomme le template `data/tp/<device_id>.json` |
| **13** | **1** | **longueur du model** (N) |
| **14** | **N** | **model** (ASCII, sans nul terminal) — clé dans ce fichier |

Pour un ZLinky LoRa qui l'émettrait : `device_id = 0x0051`, `model = "ZLinky_TIC"` (N = 10),
soit **24 octets** au total.

Exemple de trame :
```
13 05 00 15 8D 00 05 D2 AB E1 00 00 51 0A 5A 4C 69 6E 6B 79 5F 54 49 43
│  │  └───────── MAC ─────────┘ │  └─id─┘ │  └────── "ZLinky_TIC" ──────┘
│  seq                          ch        len=10
type
```

> Un récepteur ancien lit ses 11 premiers octets et ignore la queue — pas de casse.
>
> Avec le format de base, le récepteur ne connaît pas le type de l'objet à l'appairage : il
> le lit ensuite par POLL (`0x0000/0xFF00` puis `0x0000/0x0005`, §8). Un firmware qui ne
> connaît pas ces attributs répond `UNKNOWN_ATTR` : c'est un ZLinky d'avant le cluster Basic.

### 4.2 PAIR_RESPONSE — box → device (21 octets)

| Offset | Taille | Champ |
|---|---|---|
| 0 | 1 | `0x14` (type PAIR_RESPONSE) |
| 1 | 1 | seq |
| 2 | 16 | **clé AES-128** (⚠️ en clair sur l'air) |
| 18 | 1 | status (`0x00` = OK) |
| 19 | 1 | **op_channel** (0..7) — canal assigné pour les données |
| 20 | 1 | **op_SF** (7..12) — spreading factor assigné pour les données |

Si `op_channel` est hors plage, l'émetteur retombe sur `PAIR_CHANNEL` (3). Si
`op_SF` est hors plage, il retombe sur **SF11**.

> ⚠️ **SF11 ne passe pas sur tous les compteurs.** La LiXee-Box assigne `op_SF = 11` par
> défaut (meilleure portée) ; le réglage se change dans Config → LoRa, avant l'appairage. Sur
> un compteur dont le budget d'alimentation est juste, assigner **SF10**. Le budget
> d'alimentation de la prise TIC limite la **charge** tirée par émission
> (courant × airtime), pas le courant instantané. Mesures terrain, à pic de
> courant identique (45 mA) :
>
> | Config | Charge par trame | Résultat |
> |---|---|---|
> | SF11 / +10 dBm | 11,1 mA·s | ❌ échoue sur certains compteurs |
> | SF10 / +10 dBm | 5,6 mA·s | ✅ |
>
> SF10 à pleine puissance est préférable à SF11 à puissance réduite : moins
> de charge **et** meilleure portée (−2,5 dB de sensibilité contre −10 dB de
> puissance).

> **Rétrocompat** : un récepteur qui envoie encore l'ancien format **20 octets**
> (sans `op_SF`) est accepté ; l'émetteur applique SF11 par défaut. Un ZLinky
> anciennement appairé (enregistrement PDM sans SF) fait de même.

### 4.3 PAIR_CONFIRM — device → box (15 octets)

Envoyé **sur la config opérationnelle** (op_channel + op_SF), pas sur le
rendez-vous. Émettre le CONFIRM sur cette config la **valide** : si elle ne
passe pas, l'appairage échoue ici et l'émetteur retente depuis le rendez-vous,
plutôt que de se retrouver appairé sur une config muette.

| Offset | Taille | Champ |
|---|---|---|
| 0 | 1 | `0x15` (type PAIR_CONFIRM) |
| 1 | 1 | seq |
| 2 | 8 | MAC émetteur |
| 10 | 4 | **preuve** = 4 premiers octets de `AES-128-ECB(MAC padé à 16B, netKey)` |
| 14 | 1 | status (`0x00` = OK) |

Le récepteur recalcule la preuve avec la clé qu'il a générée : si ça matche,
le device connaît bien la clé → appairage validé.

---

## 5. Cadencement des trames de données

| Élément | Nominal | Mesuré (Standard triphasé) |
|---|---|---|
| DATA_ESSENTIAL | 5 s (négocié à l'appairage) | **~9 s** |
| DATA_EXTENDED | 1 tous les 4 ESSENTIAL → 20 s | **~37,5 s** |

> **Pourquoi l'écart** : l'intervalle de 5 s ne redémarre qu'**après** la fin
> du cycle, et chaque cycle commence par une acquisition TIC bloquante (~3 s
> en Standard triphasé à 9600 bauds), suivie du TX, de la fenêtre RX de 300 ms
> et, une fois sur quatre, de l'intervalle inter-trames et de l'EXTENDED. Le
> récepteur ne doit donc **pas** déduire une perte de trame d'un écart
> supérieur à 5 s : seul le numéro de séquence fait foi.

### Ordre des sous-types EXTENDED

Les sous-types ne tournent **pas** en round-robin uniforme : l'ordre suit une
table de séquence pondérée, pour que les index d'énergie soient rafraîchis
plus souvent que les données qui ne changent jamais.

**1. Tour de démarrage** — après chaque reset du ZLinky, les 12 sous-types
sont émis une fois chacun dans l'ordre `0x00 → 0x01 → … → 0x0B` (~7 min 30).
Le récepteur dispose ainsi de toutes les données, y compris les
quasi-statiques, sans attendre leur créneau dans la table.

**2. Table pondérée** — ensuite, 24 créneaux répétés indéfiniment :

```
01 00 04  01 03 05  01 07 02  01 00 04  01 03 05  01 07 0A  01 08 09  01 06 0B
```

| Sous-type | Créneaux / 24 | Période (à ~37,5 s par EXTENDED) |
|---|---|---|
| `0x01` ENERGY | 8 | **~1 min 52** |
| `0x00` CURRENT_VOLTAGE | 2 | ~7 min 30 |
| `0x03` VOLTAGE_STATS | 2 | ~7 min 30 |
| `0x04` POWER_MAX_CFG | 2 | ~7 min 30 |
| `0x05` DAILY_ENERGY | 2 | ~7 min 30 |
| `0x07` TARIFF_LABEL | 2 | ~7 min 30 |
| `0x02` ENERGY_2 | 1 | ~15 min |
| `0x06` COMPLEMENT | 1 | ~15 min |
| `0x08` METER_SERIAL | 1 | ~15 min |
| `0x09` METER_DATE | 1 | ~15 min |
| `0x0A` TARIFF_OPTION | 1 | ~15 min |
| `0x0B` HISTO_ALERTS | 1 | ~15 min |

**3. Remontée sur changement** — quand une valeur événementielle change,
son sous-type passe en priorité au prochain EXTENDED (≤ ~37,5 s) au lieu
d'attendre son créneau :

| Valeur surveillée | Sous-type émis en priorité |
|---|---|
| PEJP, PPOT, ADIR1/2/3 (Historique) | `0x0B` HISTO_ALERTS |
| LTARF (Standard) / PTEC (Historique) | `0x07` TARIFF_LABEL |
| DEMAIN (Historique Tempo) | `0x0A` TARIFF_OPTION |

Le créneau prévu est seulement retardé d'un EXTENDED. Il n'y a jamais deux
trames prioritaires d'affilée, pour qu'une valeur qui change sans cesse
(ADIR pendant un dépassement) ne bloque pas la table. Une trame prioritaire
a le même format qu'une trame normale : rien ne la distingue côté récepteur.

En Standard, les changements d'état remontent de toute façon en ~9 s par le
registre STGE de l'ESSENTIAL.

Le débit radio est inchangé (même nombre de trames) : seul l'ordre change.
Le récepteur ne doit faire **aucune hypothèse sur l'ordre** d'arrivée — il
identifie chaque trame par son octet de sous-type. Une donnée nécessaire plus
vite que sa période peut être lue à la demande par POLL (§8).

Un EXTENDED est envoyé **après** l'ESSENTIAL correspondant (piggyback), en deux
trames radio distinctes, séparées par un **intervalle d'environ 1 s**.

> **Pourquoi cet intervalle** : sans lui, les deux trames s'enchaînent à
> ~30-50 ms (le temps du reconfig radio côté émetteur). C'est trop court pour
> un récepteur qui doit déchiffrer la première puis la publier en MQTT avant
> l'arrivée du préambule de la seconde — il rate alors systématiquement
> l'EXTENDED. Le récepteur doit malgré tout **ré-armer la réception
> immédiatement** après chaque trame et déporter le traitement hors de la
> boucle d'écoute : l'intervalle est une marge, pas une garantie.

### Période réelle

L'intervalle est compté entre la **fin** d'un cycle et le **début** du suivant,
et l'acquisition TIC qui précède chaque émission bloque de 1 à 3 secondes selon
le mode du compteur. Le cycle observé va donc de **~7 s** à **~9 s** (Standard
triphasé, cf. tableau ci-dessus), pour 250 à 500 ms d'émission effective : la
radio est inoccupée plus de 90 % du temps.

C'est ce qui rend possible la **fenêtre de réception descendante** ouverte
après chaque TX (§8), sans peser sur le budget d'alimentation.

```
│◀──────────── ~7 à 9 s ────────────▶│
├──────────┬─────┬────┬──────────────┤
│ TIC 1-3s │ TX  │ RX │   standby    │
│          │250ms│300 │              │
                   ▲
                   └─ fenêtre downlink
```

---

## 6. DATA_ESSENTIAL (type `0x01`)

**17 octets en clair, 19 octets sur l'air (avec MIC).**

| Offset | Taille | Champ | Standard | Historique |
|---|---|---|---|---|
| 0 | 1 | `0x11` | | |
| 1 | 1 | seq | | |
| 2-3 | 2 | Puissance apparente totale (VA, uint16 BE) | `SINSTS` | `PAPP` |
| 4-5 | 2 | Puissance apparente phase 1 (VA, uint16 BE) | `SINSTS1` | *(= PAPP)* |
| 6-7 | 2 | Puissance apparente phase 2 (VA, uint16 BE) | `SINSTS2` | 0 en mono |
| 8-9 | 2 | Puissance apparente phase 3 (VA, uint16 BE) | `SINSTS3` | 0 en mono |
| 10-13 | 4 | `STGE` — mot d'état (32 bits binaire) | `STGE` | 0 (absent) |
| 14-15 | 2 | `ADPS` — alerte dépassement (uint16 BE) | 0 (absent) | `ADPS` |
| 16 | 1 | **mode Linky** (voir §6.1) | | |

Les valeurs sont plafonnées à 65535 (cap uint16).

`STGE` est transmis en **binaire** : les 8 caractères hex ASCII de la TIC sont
convertis en 4 octets. Ex : `"003A1B2C"` → `0x00 0x3A 0x1B 0x2C`.

### 6.1 Encodage du mode Linky (byte 16)

Champ de bits :

| Bit | Signification |
|---|---|
| 0 | 0 = **Historique**, 1 = **Standard** |
| 1 | 0 = mono, 1 = **triphasé** |
| 2 | 1 = **production** (injection présente, étiquette `EAIT` vue) |

Valeurs observées :

| Valeur | Signification |
|---|---|
| 0 | Historique monophasé |
| 1 | Standard monophasé |
| 2 | Historique triphasé |
| 3 | Standard triphasé |
| 5 | Standard monophasé + production |
| 7 | Standard triphasé + production |

> **C'est le champ pivot du protocole.** Les paquets EXTENDED ne le portent pas
> (sauf POWER_MAX_CFG) : le récepteur doit **mémoriser le dernier mode reçu par
> émetteur** pour interpréter correctement les index Tier1..Tier6 (voir §7.2).

---

## 7. DATA_EXTENDED (type `0x02`)

**En-tête commun (3 octets) :**

| Offset | Taille | Champ |
|---|---|---|
| 0 | 1 | `0x12` |
| 1 | 1 | seq |
| 2 | 1 | **sub_type** (0x00..0x0B) |

### 7.1 Sub-type `0x00` — CURRENT_VOLTAGE (15 octets)

| Offset | Taille | Champ | Standard | Historique |
|---|---|---|---|---|
| 3-4 | 2 | Courant phase 1 (uint16 BE) | `IRMS1` (mA) | `IINST1` / `IINST` (A) |
| 5-6 | 2 | Courant phase 2 | `IRMS2` (mA) | `IINST2` (A) |
| 7-8 | 2 | Courant phase 3 | `IRMS3` (mA) | `IINST3` (A) |
| 9-10 | 2 | Tension phase 1 (V, uint16 BE) | `URMS1` | 0 (absent) |
| 11-12 | 2 | Tension phase 2 | `URMS2` | 0 (absent) |
| 13-14 | 2 | Tension phase 3 | `URMS3` | 0 (absent) |

⚠️ **Unité des courants** : mA en Standard, **A** en Historique.

### 7.2 Sub-type `0x01` — ENERGY (19 octets)

| Offset | Taille | Champ | Standard | Historique |
|---|---|---|---|---|
| 3-6 | 4 | Index total (Wh, uint32 BE) | `EAST` | `BASE` *(uniquement si option BASE)* |
| 7-10 | 4 | Index injecté (Wh, uint32 BE) | `EAIT` | 0 (absent) |
| 11-14 | 4 | **Index tarifaire 1** (Wh, uint32 BE) | `EASF01` | voir table §7.2.1 |
| 15-18 | 4 | **Index tarifaire 2** (Wh, uint32 BE) | `EASF02` | voir table §7.2.1 |

#### 7.2.1 Interprétation des index tarifaires

L'émetteur remplit **les mêmes champs Tier1..Tier6** dans les deux modes.
Le récepteur doit résoudre le nom TIC réel à partir du **mode** (§6.1) et du
**code tarif** (§7.5) :

| Tier | Standard | Histo BASE | Histo HC/HP | Histo EJP | Histo Tempo |
|---|---|---|---|---|---|
| 1 | `EASF01` | `BASE` | `HCHC` | `EJPHN` | `BBRHCJB` |
| 2 | `EASF02` | — | `HCHP` | `EJPHPM` | `BBRHPJB` |
| 3 | `EASF03` | — | — | — | `BBRHCJW` |
| 4 | `EASF04` | — | — | — | `BBRHPJW` |
| 5 | `EASF05` | — | — | — | `BBRHCJR` |
| 6 | `EASF06` | — | — | — | `BBRHPJR` |

Les cases « — » signifient que le tier n'a **aucune signification** dans cette
configuration : la valeur transmise n'est pas pertinente, à masquer.

**L'option tarifaire Historique se déduit du code tarif** :

| Code tarif (§7.5) | Option déduite |
|---|---|
| 1 | BASE |
| 2, 3 | HC/HP |
| 4, 5 | EJP |
| 6..11 | Tempo |

### 7.3 Sub-type `0x02` — ENERGY_2 (19 octets)

| Offset | Taille | Champ | Standard | Historique |
|---|---|---|---|---|
| 3-6 | 4 | Index tarifaire 3 (Wh, uint32 BE) | `EASF03` | `BBRHCJW` *(Tempo seul)* |
| 7-10 | 4 | Index tarifaire 4 | `EASF04` | `BBRHPJW` *(Tempo seul)* |
| 11-14 | 4 | Index tarifaire 5 | `EASF05` | `BBRHCJR` *(Tempo seul)* |
| 15-18 | 4 | Index tarifaire 6 | `EASF06` | `BBRHPJR` *(Tempo seul)* |

Hors Tempo (mode Historique), ces 4 champs sont **sans signification**.

### 7.4 Sub-type `0x03` — VOLTAGE_STATS (15 octets)

| Offset | Taille | Champ | Standard | Historique |
|---|---|---|---|---|
| 3-4 | 2 | Tension moyenne phase 1 (V, uint16 BE) | `UMOY1` | 0 (absent) |
| 5-6 | 2 | Tension moyenne phase 2 | `UMOY2` | 0 (absent) |
| 7-8 | 2 | Tension moyenne phase 3 | `UMOY3` | 0 (absent) |
| 9-10 | 2 | Courant max phase 1 (uint16 BE) | *(SMAXSN1)* | `IMAX1` / `IMAX` (A) |
| 11-12 | 2 | Courant max phase 2 | | `IMAX2` (A) |
| 13-14 | 2 | Courant max phase 3 | | `IMAX3` (A) |

### 7.5 Sub-type `0x04` — POWER_MAX_CFG (18 octets)

**Paquet clé** : c'est le seul EXTENDED qui porte à la fois le **mode** et le **tarif**.

| Offset | Taille | Champ | Standard | Historique |
|---|---|---|---|---|
| 3-4 | 2 | Puissance max soutirée **du jour** (VA, uint16 BE) | `SMAXSN` | *(voir note)* |
| 5-6 | 2 | Puissance max injectée jour (VA, uint16 BE) | `SMAXIN` | 0 (absent) |
| 7-8 | 2 | Puissance apparente injectée (VA, uint16 BE) | `SINSTI` | 0 (absent) |
| 9-10 | 2 | Point courbe de charge injectée (VA, uint16 BE) | `CCAIN` | 0 (absent) |
| 11-12 | 2 | `RELAIS` (uint16 BE) | `RELAIS` | 0 (absent) |
| 13 | 1 | `NTARF` — numéro tarif (uint8) | `NTARF` (1..10) | 0 (absent) |
| 14 | 1 | Seuil de coupure (kVA, uint8) | `PCOUP` | 0 (absent) |
| 15 | 1 | Puissance/intensité souscrite (uint8) | `PREF` (kVA) | `ISOUSC` (A) |
| 16 | 1 | **mode Linky** (même encodage que §6.1) | | |
| 17 | 1 | **code tarif unifié** (voir §7.5.1) | | |

> `SMAXSN` est la puissance max soutirée **du jour en cours**. La valeur de la
> veille (`SMAXSN-1`) n'est pas transmise.

#### 7.5.1 Code tarif unifié (byte 17)

**Mode Standard** — reprend `NTARF` :

| Valeur | Signification |
|---|---|
| 0 | inconnu / pas encore renseigné |
| 1..10 | index du tarif en cours (= `NTARF`) |

**Mode Historique** — code dérivé de `PTEC` :

| Valeur | `PTEC` | Signification |
|---|---|---|
| 0 | — | inconnu |
| 1 | `TH..` / `BASE` | BASE |
| 2 | `HP..` | Heures Pleines |
| 3 | `HC..` | Heures Creuses |
| 4 | `HN..` | EJP heures normales |
| 5 | `PM..` | EJP pointe mobile |
| 6 | `HCJB` | Tempo Bleu HC |
| 7 | `HPJB` | Tempo Bleu HP |
| 8 | `HCJW` | Tempo Blanc HC |
| 9 | `HPJW` | Tempo Blanc HP |
| 10 | `HCJR` | Tempo Rouge HC |
| 11 | `HPJR` | Tempo Rouge HP |

C'est **ce champ qui permet de déduire l'option tarifaire Historique** et donc de
nommer correctement les index Tier1..Tier6 (§7.2.1).

### 7.6 Sub-type `0x05` — DAILY_ENERGY (19 octets)

| Offset | Taille | Champ | Standard | Historique |
|---|---|---|---|---|
| 3-6 | 4 | Énergie journalière J (Wh, uint32 BE) | `EASD01` | 0 (absent) |
| 7-10 | 4 | Énergie journalière J-1 | `EASD02` | 0 (absent) |
| 11-14 | 4 | Énergie journalière J-2 | `EASD03` | 0 (absent) |
| 15-18 | 4 | Énergie journalière J-3 | `EASD04` | 0 (absent) |

### 7.7 Sub-type `0x06` — COMPLEMENT (9 octets)

| Offset | Taille | Champ | Standard | Historique |
|---|---|---|---|---|
| 3 | 1 | `DPM1` — début pointe mobile 1 | `DPM1` | 0 (absent) |
| 4 | 1 | `FPM1` — fin pointe mobile 1 | `FPM1` | 0 (absent) |
| 5 | 1 | `DPM2` | `DPM2` | 0 (absent) |
| 6 | 1 | `FPM2` | `FPM2` | 0 (absent) |
| 7 | 1 | `DPM3` | `DPM3` | 0 (absent) |
| 8 | 1 | `FPM3` | `FPM3` | 0 (absent) |

### 7.8 Sub-type `0x07` — TARIFF_LABEL (6..22 octets, **taille variable**)

Libellé **texte** du tarif en cours, tel que le compteur l'annonce.

| Offset | Taille | Champ | Standard | Historique |
|---|---|---|---|---|
| 3 | 1 | mode Linky (même encodage que §6.1) | | |
| 4 | 1 | code tarif unifié (même encodage que §7.5.1) | | |
| 5 | 1 | **longueur du libellé** (N, 0..16) | | |
| 6..6+N-1 | N | **libellé ASCII** (sans nul terminal) | `LTARF` | `PTEC` |

Taille totale du paquet = **6 + N**.

Le libellé est transmis **espaces de fin retirés** (la TIC pade `LTARF` à 16
caractères). Les caractères non imprimables coupent la chaîne.

Exemples de libellés :

| Mode | Valeurs typiques |
|---|---|
| Standard | `"BASE"`, `"HEURE CREUSE"`, `"HEURE PLEINE"`, `"HC BLEU"`, `"HP ROUGE"` |
| Historique | `"TH.."`, `"HC.."`, `"HP.."`, `"HCJB"`, `"HPJR"` |

> Côté émetteur les deux étiquettes partagent le même champ interne
> (`au8LinkyTarifPeriod`) : `LTARF` l'écrit en Standard, `PTEC` en Historique.
> C'est le champ `mode` [3] qui indique lequel des deux est transmis.

Exemple de trame (Standard, `LTARF = "HEURE CREUSE"`) :
```
12 2A 07 01 01 0C 48 45 55 52 45 20 43 52 45 55 53 45
│  │  │  │  │  │  └──────── "HEURE CREUSE" ─────────┘
│  │  │  │  │  len=12
│  │  │  │  code tarif (NTARF=1)
│  │  │  mode=1 (Std Mono)
│  │  sub_type
│  seq
type
```

### 7.9 Sub-type `0x08` — METER_SERIAL (4..16 octets, **taille variable**)

Numéro de série du compteur.

| Offset | Taille | Champ | Standard | Historique |
|---|---|---|---|---|
| 3 | 1 | **longueur du numéro** (N, 0..12) | | |
| 4..4+N-1 | N | **numéro de série ASCII** (sans nul terminal) | `ADSC` | `ADCO` |

Taille totale du paquet = **4 + N**.

**Pas de champ mode** : `ADSC` et `ADCO` font tous deux 12 caractères et
partagent le même champ interne côté émetteur — le numéro est identique
quel que soit le mode.

Le numéro est transmis espaces de fin retirés ; les caractères non
imprimables coupent la chaîne.

Exemple de trame (`ADSC = "021761234567"`) :
```
12 2B 08 0C 30 32 31 37 36 31 32 33 34 35 36 37
│  │  │  │  └────────── "021761234567" ───────┘
│  │  │  len=12
│  │  sub_type
│  seq
type
```

### 7.10 Sub-type `0x09` — METER_DATE (4..17 octets, **taille variable**)

Horodate interne du compteur. **Étiquette `DATE`, mode Standard uniquement.**

| Offset | Taille | Champ |
|---|---|---|
| 3 | 1 | **longueur de l'horodate** (N, 0..13) |
| 4..4+N-1 | N | **horodate ASCII** `SAAMMJJhhmmss` (sans nul terminal) |

Taille totale du paquet = **4 + N**.

En mode Historique l'étiquette n'existe pas : `N = 0` et le paquet fait
4 octets — le récepteur peut l'ignorer.

Format de l'horodate :

| Champ | Position | Signification |
|---|---|---|
| `S` | 0 | saison **et état de l'horloge** (voir ci-dessous) |
| `AA` | 1-2 | année (2 chiffres) |
| `MM` | 3-4 | mois |
| `JJ` | 5-6 | jour |
| `hh` | 7-8 | heure |
| `mm` | 9-10 | minute |
| `ss` | 11-12 | seconde |

Le premier caractère porte **deux informations** :

| Valeur | Signification |
|---|---|
| `E` / `H` | été / hiver, **horloge synchronisée** |
| `e` / `h` | été / hiver, **horloge en mode dégradé** (non synchronisée) |
| espace | saison inconnue |

> Une minuscule signale que l'horloge du compteur n'est pas synchronisée :
> l'horodate est alors à considérer comme approximative.

Exemple de trame (`DATE = "E250508143022"` → 8 mai 2026 14:30:22, heure d'été,
horloge synchronisée) :
```
12 2C 09 0D 45 32 35 30 35 30 38 31 34 33 30 32 32
│  │  │  │  └────────── "E250508143022" ─────────┘
│  │  │  len=13
│  │  sub_type
│  seq
type
```

> ⚠️ **Fraîcheur** : ce sous-type n'occupe qu'un créneau de la table de
> séquence (§5), soit toutes les ~15 min. L'horodate reçue peut donc avoir
> jusqu'à ~15 min de retard — utilisable pour vérifier/dériver l'horloge du compteur, pas
> comme source de temps précise.

### 7.11 Sub-type `0x0A` — TARIFF_OPTION (16 octets)

Option tarifaire souscrite, couleur Tempo du lendemain, courbe de charge
soutirée. Les deux moitiés du paquet sont **mutuellement exclusives selon
le mode**, d'où leur regroupement.

| Offset | Taille | Champ | Standard | Historique |
|---|---|---|---|---|
| 3 | 1 | mode Linky (même encodage que §6.1) | | |
| 4-7 | 4 | Option tarifaire (4 ASCII) | `0x00` × 4 | `OPTARIF` |
| 8-11 | 4 | Couleur Tempo du lendemain (4 ASCII) | `0x00` × 4 | `DEMAIN` *(Tempo seul)* |
| 12-13 | 2 | Courbe de charge soutirée, point courant (**int16 BE**, VA) | `CCASN` | 0 |
| 14-15 | 2 | Courbe de charge soutirée, point précédent (**int16 BE**, VA) | `CCASN-1` | 0 |

Les champs ASCII sont **complétés par `0x00`** dès qu'un caractère non
imprimable est rencontré : un champ absent vaut `0x00 0x00 0x00 0x00`, ce qui
le distingue sans ambiguïté d'une valeur réelle.

**Valeurs de `OPTARIF`** : `"BASE"`, `"HC.."`, `"EJP."`, `"BBR("` (Tempo).

**Valeurs de `DEMAIN`** : `"BLEU"`, `"BLAN"`, `"ROUG"`, `"----"` (inconnue).

> ⚠️ **`CCASN` / `CCASN-1` sont signés** (int16), contrairement à la plupart
> des autres puissances du protocole qui sont en uint16 — la courbe de charge
> peut être négative en cas d'injection.

> **Pourquoi `OPTARIF` n'est pas transmis en Standard** : côté émetteur, le
> champ interne porte `OPTARIF` (4 car.) en Historique mais `NGTF` (nom du
> calendrier tarifaire fournisseur, 16 car.) en Standard. Transmettre ses
> 4 premiers caractères en Standard donnerait un `NGTF` tronqué et trompeur.
> `NGTF` reste donc non transmis (cf. §9).

> **`CCASN` et `CCAIN`** : `CCAIN` (courbe de charge **injectée**) est déjà
> transmis dans POWER_MAX_CFG [9-10] (§7.5). `CCASN` est sa contrepartie
> **soutirée**.

Exemple de trame (Historique Tempo, `OPTARIF = "BBR("`, `DEMAIN = "BLEU"`) :
```
12 2D 0A 00 42 42 52 28 42 4C 45 55 00 00 00 00
│  │  │  │  └─"BBR("─┘ └─"BLEU"─┘ └CCASN┘ └CCASN-1┘
│  │  │  mode=0 (Histo Mono)
│  │  sub_type
│  seq
type
```

---

### 7.12 Sub-type `0x0B` — HISTO_ALERTS (12 octets)

Alertes et préavis **spécifiques au mode Historique** (préavis EJP,
dépassements de puissance par phase en triphasé). En Standard ces étiquettes
n'existent pas : si `mode` (offset 3) indique le Standard, le récepteur peut
ignorer le paquet.

| Offset | Taille | Champ | Signification |
|---|---|---|---|
| 3 | 1 | mode Linky (même encodage que §6.1) | |
| 4 | 1 | `PEJP` (uint8) | Préavis EJP : minutes avant le début de la période EJP |
| 5 | 1 | `PPOT` (uint8) | Présence des potentiels (triphasé) |
| 6-7 | 2 | `ADIR1` (uint16 BE, VA) | Avertissement dépassement puissance phase 1 |
| 8-9 | 2 | `ADIR2` (uint16 BE, VA) | Avertissement dépassement puissance phase 2 |
| 10-11 | 2 | `ADIR3` (uint16 BE, VA) | Avertissement dépassement puissance phase 3 |

> `PPOT` et `ADIR1/2/3` ne sont émis par le compteur qu'en **triphasé** ; en
> monophasé Historique ils valent 0. `PEJP` n'a de sens que pour un abonné
> **option EJP**.

> **Fin d'alerte** : `PEJP` et `ADIR1/2/3` ne sont émis par le compteur que
> pendant l'événement. Le ZLinky les remet à **0** après 2 trames TIC valides
> consécutives sans l'étiquette (~18 s) ; `0` signifie donc « pas d'alerte en
> cours ». Le début comme la fin de l'alerte déclenchent une trame
> prioritaire (§5).

---

## 8. Lecture d'attribut à la demande (bidirectionnel)

Le canal descendant sert **uniquement à interroger un attribut** — modèle ZCL
Read Attributes. Il ne modifie aucun état : le SF et le canal se négocient à
l'appairage (§4), pas ici.

Une lecture étant idempotente, il n'y a **ni compteur anti-rejeu ni retour
arrière** à gérer : rejouer une requête ne fait que relire la valeur courante.

### Principe

Le ZLinky ouvre une **fenêtre de réception de 300 ms juste après chaque TX**.
Le récepteur, qui vient de recevoir l'uplink, y dépose sa requête.

```
ZLinky ──── ESSENTIAL / EXTENDED ─────▶ Récepteur
       ◀─── POLL_REQUEST (optionnel) ──┘   dans les 300 ms
       ──── POLL_RESPONSE ─────────────▶
```

Chronologie dans la fenêtre :

| Instant | Événement |
|---|---|
| T+0 | fin du TX uplink |
| T+5 ms | ZLinky en RX (restauration IRQ DIO1 + `payloadLength`) |
| T+20 ms | récepteur a déchiffré, consulté sa file, démarre son TX |
| T+60 ms | préambule détecté par le ZLinky |
| T+300 ms | fenêtre fermée, retour en standby |

> **Préambule de la requête aux SF bas** : le ZLinky ne détecte un préambule que vers
> T+40 à 60 ms. À SF7, un préambule de 8 symboles ne dure que ~2,5 ms : il est déjà passé.
> Le récepteur doit donc **allonger le préambule de son POLL_REQUEST** pour qu'il dure
> ~40 ms : 126 symboles à SF7, 63 à SF8, 31 à SF9, 15 à SF10, 8 à SF11 et SF12 (la
> LiXee-Box le fait). Un préambule plus long que celui attendu par le ZLinky (8) reste reçu ;
> c'est l'inverse qui empêche toute détection (§1).

> **Pourquoi une fenêtre et pas une écoute permanente** : le SX1280 tire
> ~7-10 mA en RX contre ~0,6 mA en standby. Écouter en permanence coûterait
> ~30 mW, soit près du quart du budget TIC (130 mW). La fenêtre courte revient
> à ~1,5 mW, soit ~1 %.

### POLL_REQUEST — box → device (6 octets + MIC)

| Offset | Taille | Champ |
|---|---|---|
| 0 | 1 | `0x1B` (version 1, type `0x0B`) |
| 1 | 1 | seq |
| 2-3 | 2 | **cluster_id** (uint16 BE) |
| 4-5 | 2 | **attribute_id** (uint16 BE) |

### POLL_RESPONSE — device → box (9..26 octets + MIC)

| Offset | Taille | Champ |
|---|---|---|
| 0 | 1 | `0x1C` (version 1, type `0x0C`) |
| 1 | 1 | seq |
| 2-3 | 2 | cluster_id (écho) |
| 4-5 | 2 | attribute_id (écho) |
| 6 | 1 | **statut** |
| 7 | 1 | **type ZCL** de la valeur |
| 8 | 1 | **longueur** de la valeur (N) |
| 9..9+N-1 | N | **valeur** (big-endian pour les numériques) |

| Statut | Signification |
|---|---|
| `0x00` | OK |
| `0x01` | `UNKNOWN_ATTR` — cluster/attribut non géré (N = 0) |
| `0x02` | `BAD_REQUEST` — requête malformée |

Types ZCL utilisés :

| Code | Type | Longueur |
|---|---|---|
| `0x20` | uint8 | 1 |
| `0x21` | uint16 | 2 |
| `0x23` | uint32 | 4 |
| `0x25` | uint48 | 6 |
| `0x28` | int8 | 1 |
| `0x29` | int16 | 2 |
| `0x41` | octet string | variable |
| `0x42` | character string | variable |

### Clusters et attributs adressables

**Tous les attributs TIC disponibles sont adressables**, répartis sur 4 clusters
aux mêmes identifiants que le firmware Zigbee LiXee — la connaissance des
clusters se transfère d'un monde à l'autre. S'y ajoute le cluster **Basic**,
qui décrit l'objet lui-même et non le compteur.

**Cluster Basic `0x0000`** (identification de l'objet) :

| attr | Champ | Type | Valeur ZLinky LoRa |
|---|---|---|---|
| `0xFF00` | **device_id** | uint16 | `0x0051` |
| `0x0005` | **ModelIdentifier** | char string | `"ZLinky_TIC"` |
| `0xFF01` | **version firmware** | uint16 | `0x0100` pour 1.0.0 |
| `0x4000` | **SWBuildID** | char string | `"1.0.0+abc1234"` |

> **Version du firmware** : numérotée `MAJEUR.MINEUR.CORRECTIF`.
>
> - `0xFF01` la donne sur 16 bits au format `0xMMmp` (MM = majeur, m = mineur,
>   p = correctif) : 1.2.3 → `0x0123`. Les valeurs se comparent numériquement,
>   et c'est le même encodage que le champ version d'OTA_START (§10). Le
>   récepteur lit cet attribut pour décider d'une mise à jour, puis le relit
>   après l'OTA pour vérifier qu'elle a pris.
> - `0x4000` la donne en clair, suivie du commit git du build
>   (`+abc1234`). Le suffixe `-dirty` signale un build de développement fait
>   avec des sources non commitées ; `nogit` remplace le commit si git était
>   absent à la compilation. À afficher tel quel et à joindre aux demandes de
>   support.
> - Un changement de **majeur** signale une rupture de compatibilité radio.
>
> Un firmware antérieur à 1.0.0 répond `UNKNOWN_ATTR` à ces deux attributs.

> **À quoi ça sert** : après l'appairage, le récepteur interroge d'abord
> `0xFF00` pour connaître le `device_id` — qui nomme le fichier
> `data/tp/<device_id>.json` — puis `0x0005` pour le `model`, qui en est la clé.
> Il peut alors appliquer le bon template.
>
> **Pourquoi en POLL et non dans le PAIR_REQUEST** : ces deux champs y étaient
> auparavant, et faisaient passer la trame de 11 à 24 octets, soit **8,7 →
> 12,1 mA·s** à SF11. C'est au-delà du budget d'alimentation de la prise TIC de
> certains compteurs, et l'appairage devenait **impossible** — le PAIR_REQUEST
> est la trame la plus répétée du protocole. Les exposer en POLL ne coûte rien
> sur le chemin critique : le récepteur les demande **une seule fois**, quand il
> veut, après l'appairage. Le PAIR_REQUEST reste au format 11 octets.

**Cluster LiXee `0xFF66`** (données TIC spécifiques) :

| attr | Étiquette | Type |
|---|---|---|
| `0x0000` | OPTARIF | char string |
| `0x0001` | DEMAIN | char string |
| `0x0002` | HHPHC | uint8 |
| `0x0003` | PPOT | uint8 |
| `0x0004` | PEJP | uint8 |
| `0x0005` | ADPS | uint16 |
| `0x0006`..`0x0008` | ADIR1..ADIR3 | uint16 |
| `0x0009` | MOTDETAT | char string |
| `0x0010` | PTEC / LTARF (période tarifaire) | char string |
| `0x0100` | PERIODIC_SEND | uint8 |
| `0x0200` | LTARF | char string |
| `0x0201` | NTARF | uint8 |
| `0x0202` | DATE | char string |
| `0x0203`..`0x0206` | EASD01..EASD04 | uint32 |
| `0x0207` | SINSTI | uint16 |
| `0x0208` | SMAXIN | uint16 |
| `0x0209` | SMAXIN-1 | uint16 |
| `0x0210` | CCAIN | uint16 |
| `0x0211` | CCAIN-1 | uint16 |
| `0x0212`..`0x0214` | SMAXSN1-1/SMAXSN2-1/SMAXSN3-1 | uint16 |
| `0x0215` | MSG1 | char string |
| `0x0216` | MSG2 | char string |
| `0x0217` | STGE (8 car. hex) | char string |
| `0x0218`..`0x0223` | DPM1/FPM1/DPM2/FPM2/DPM3/FPM3 | uint8 |
| `0x0224` | RELAIS | uint16 |
| `0x0225` | NJOURF | uint8 |
| `0x0226` | NJOURF+1 | uint8 |
| `0x0227` | PJOURF+1 (tronqué à 32 car.) | char string |
| `0x0228` | PPOINTE (tronqué à 32 car.) | char string |
| `0x0300` | mode Linky | uint8 |
| `0x0400` | SMAXSN-1 | uint16 |

**Cluster Simple Metering `0x0702`** (index d'énergie, transmis en uint32) :

| attr | Étiquette | Type |
|---|---|---|
| `0x0000` | EAST (Std) / BASE (Histo) | uint32 |
| `0x0001` | EAIT (injection) | uint32 |
| `0x0100`..`0x0109` | index tarifaire 1 à 10 (EASF01..10) | uint32 |

**Cluster Electrical Measurement `0x0B04`** (puissances, courants, tensions) :

| attr | Étiquette | Type |
|---|---|---|
| `0x0304` | SMAXSN (puissance max active du jour) | int32 |
| `0x0305` | ERQ1 (énergie réactive totale) | int32 |
| `0x0306` | SINSTS total | uint32 |
| `0x0505`/`0x0905`/`0x0A05` | URMS1/URMS2/URMS3 | uint16 |
| `0x0508`/`0x0908`/`0x0A08` | IRMS1/IRMS2/IRMS3 (IINST en Histo) | uint16 |
| `0x050A`/`0x090A`/`0x0A0A` | IMAX1/IMAX2/IMAX3 | uint16 |
| `0x050B`/`0x090B` | CCASN / CCASN-1 | int16 |
| `0x050D`/`0x090D`/`0x0A0D` | SMAXSN1/SMAXSN2/SMAXSN3 (PMAX en Histo) | int16 |
| `0x050E`/`0x090E`/`0x0A0E` | ERQ2/ERQ3/ERQ4 | int16 |
| `0x050F`/`0x090F`/`0x0A0F` | SINSTS1/SINSTS2/SINSTS3 (PAPP en Histo) | uint16 |
| `0x0511`/`0x0911`/`0x0A11` | UMOY1/UMOY2/UMOY3 | uint16 |

**Cluster Meter Identification `0x0B01`** (configuration compteur) :

| attr | Étiquette | Type |
|---|---|---|
| `0x000A` | VTIC (version TIC) | uint8 |
| `0x000C` | POD | uint16 |
| `0x000D` | PREF (Std) / ISOUSC (Histo) | uint8 |
| `0x000E` | PCOUP | uint8 |

> Un couple (cluster, attribut) hors de ces tables renvoie `UNKNOWN_ATTR`.
> Selon le mode (Standard/Historique) certains attributs valent 0 (non
> renseignés par le compteur) — le récepteur les interprète comme en réception
> normale (cf. §11.1).

### Exemple

Lire le mode Linky (cluster `0xFF66`, attr `0x0300`) :

```
box → device   1B 04 FF 66 03 00                    POLL_REQUEST
device → box   1C 07 FF 66 03 00 00 20 01 01        POLL_RESPONSE
               │           │     │  │  │  └─ valeur = 1 (Std Mono)
               │           │     │  │  len=1
               │           │     │  type ZCL uint8
               │           │     status OK
               │           attr 0x0300
               cluster 0xFF66
```

---

## 9. Étiquettes TIC non transmises

Pour information, ces étiquettes sont parsées par l'émetteur mais **ne sont pas
encore envoyées** en LoRa :

**Standard** : `EASF07`..`EASF10`, `ERQ1`..`ERQ4`, `NJOURF`, `NJOURF+1`,
`PJOURF+1`, `PPOINTE`, `SMAXIN-1`, `SMAXSN-1`, `SMAXSN1-1`..`SMAXSN3-1`,
`CCAIN-1`, `VTIC`, `NGTF`, `MSG1`, `MSG2`, `PRM`

**Historique** : `HHPHC`, `MOTDETAT`
*(`PAPP` et `PMAX` désormais transmis — ESSENTIAL / POWER_MAX_CFG ;
`PEJP`, `PPOT`, `ADIR1`..`ADIR3` transmis via HISTO_ALERTS §7.12)*

---

## 10. OTA (mise à jour firmware)

Cette section fait référence. `OTA_README.md` décrit l'ancien outil PC (`ota_send.py`), qui
travaille sans fenêtre, un ACK par bloc : il reste utilisable, mais ce n'est plus le
déroulement recommandé. La LiXee-Box (≥ v2.26) pilote la mise à jour elle-même, depuis
Réseau → LoRa, avec une fenêtre de 16 blocs.

Résumé des trames (toutes chiffrées comme les DATA) :

| Type | Nom | Sens | Taille (clair) |
|---|---|---|---|
| `0x06` | OTA_START | box → device | 15 B, ou 16 B avec la fenêtre |
| `0x07` | OTA_BLOCK | box → device | 68 B (4 + 64 de données) |
| `0x08` | OTA_END | box → device | 7 B |
| `0x09` | OTA_ACK | device → box | 7 B |
| `0x0A` | OTA_STATUS | device → box | 8 B |

### Déroulement

Le ZLinky doit être en **mode OTA recovery** (bouton maintenu à la mise sous
tension). Il écoute alors en continu, pendant 5 minutes, sur le canal et le SF
opérationnels, et **n'émet rien de lui-même** : c'est le récepteur qui ouvre
l'échange.

1. Le récepteur émet `OTA_START` et le répète jusqu'à recevoir un `OTA_ACK`.
2. Le récepteur envoie les `OTA_BLOCK` dans l'ordre. Le ZLinky acquitte par
   **fenêtre** de W blocs (voir ci-dessous).
3. Le récepteur émet `OTA_END`. Le ZLinky vérifie l'image, acquitte, puis
   redémarre sur le nouveau firmware.

Après le redémarrage, le récepteur peut relire la version par POLL (§8,
attribut `0xFF01`) pour vérifier que la mise à jour a pris.

### Acquittement par fenêtre

Acquitter chaque bloc obligerait le ZLinky à émettre toutes les ~0,7 s, ce que
l'alimentation TIC ne supporte pas. Le récepteur demande donc une fenêtre de
W blocs dans `OTA_START` (W = 16 recommandé) :

- le ZLinky **n'émet que sur un bloc de fin de fenêtre** : un bloc dont l'index
  vérifie `(index + 1) % W == 0`, ou le dernier bloc de l'image ;
- son `OTA_ACK` porte alors l'index du **prochain bloc attendu**, statut `OK` ;
- un bloc hors séquence (trou ou doublon) est **ignoré en silence**.

Algorithme côté récepteur, pour une fenêtre couvrant les blocs `a` à `b`
(`b` = fin de fenêtre) :

1. Envoyer les blocs `a`..`b` à la suite, espacés d'au moins **100 ms**.
2. Écouter l'`OTA_ACK` pendant **2 s**.
3. Lire `next` = index porté par l'ACK :
   - `next == b + 1` : fenêtre reçue, passer à la suivante ;
   - `next <= b` : un bloc a été perdu, reprendre l'étape 1 avec `a = next`
     (même `b`) ;
   - `next == total` : tous les blocs sont reçus, envoyer `OTA_END`.
4. Pas d'ACK au bout de 2 s : renvoyer **uniquement le bloc `b`**, qui
   sollicite un nouvel ACK, puis reprendre à l'étape 2. Ne pas renvoyer toute
   la fenêtre : le ZLinky a peut-être tout reçu et c'est l'ACK qui s'est perdu.
5. Abandonner après une dizaine d'échecs consécutifs sur la même fenêtre.

Le récepteur ne doit **jamais émettre pendant qu'il attend un ACK** : la
liaison est à l'alternat, il brouillerait la réponse.

**Durées à SF11 / BW 406 kHz :**

| Trame | Octets sur l'air (avec MIC) | Durée |
|---|---|---|
| OTA_START | 18 | ~205 ms |
| OTA_BLOCK | 70 | ~505 ms |
| OTA_ACK | 9 | ~150 ms |

> Ces durées sont celles mesurées (205 ms, 507 ms et 156 ms). Le calcul les retrouve à
> condition d'utiliser le dénominateur `4 × (SF − 2)` que le SX1280 applique à SF11 et SF12 ;
> avec `4 × SF`, on trouve à tort ~430 ms pour un bloc.

Avec W = 16, une fenêtre dure environ 10 s (16 blocs de 505 ms espacés de 100 ms, puis
l'ACK) et le ZLinky n'émet qu'une fois par fenêtre, comme en fonctionnement normal. Un
firmware de 200 ko (~3150 blocs) se transfère en **environ 35 minutes** sans perte.

À SF plus bas, tout raccourcit : un bloc dure ~240 ms à SF10, ~126 ms à SF9, ~69 ms à SF8.
L'OTA se fait sur le SF opérationnel : pour une mise à jour rapide, appairer à SF bas avec
l'appareil près du récepteur.

**Compatibilité :** un ZLinky dont le firmware ignore la fenêtre renvoie `0`
dans l'octet 5 de l'`OTA_ACK` de `OTA_START` et acquitte chaque bloc. Le
récepteur doit alors travailler bloc par bloc (envoyer un bloc, attendre son
ACK, qui porte l'index du bloc acquitté et non le suivant).

### OTA_START (15 ou 16 octets)

| Offset | Taille | Champ |
|---|---|---|
| 0 | 1 | `0x16` |
| 1 | 1 | seq |
| 2-3 | 2 | version du nouveau firmware (uint16 BE, format `0xMMmp` — cf. §8, attribut `0xFF01`) |
| 4-7 | 4 | taille du firmware en octets (uint32 BE) |
| 8-11 | 4 | CRC32 du firmware complet (uint32 BE) |
| 12-13 | 2 | nombre total de blocs (uint16 BE) |
| 14 | 1 | taille d'un bloc (toujours 64) |
| 15 | 1 | **optionnel** — fenêtre d'acquittement W demandée (2 à 32 ; 16 recommandé). Absent, `0` ou `1` : un ACK par bloc |

Le ZLinky borne W à 32 et renvoie la valeur retenue dans l'`OTA_ACK` : c'est
**cette valeur** que le récepteur doit utiliser.

### OTA_BLOCK (68 octets)

| Offset | Taille | Champ |
|---|---|---|
| 0 | 1 | `0x17` |
| 1 | 1 | seq |
| 2-3 | 2 | index du bloc (uint16 BE) |
| 4-67 | 64 | données (dernier bloc padé à `0xFF`) |

Les blocs doivent arriver **séquentiellement**.

- **Avec fenêtre (W ≥ 2)** : un bloc hors séquence est ignoré en silence ; le
  récepteur l'apprend par l'ACK de fin de fenêtre (voir plus haut).
- **Sans fenêtre** : le device répond immédiatement `OTA_ACK` avec
  `status = OUT_OF_RANGE` et `block_idx` = le bloc qu'il attend → la gateway
  doit reprendre à cet index.

Si le ZLinky n'est pas en réception (pas d'`OTA_START` accepté, ou redémarrage
en cours de transfert), il répond `NOT_STARTED` au premier bloc reçu puis une
fois tous les 32 blocs : le récepteur doit alors reprendre à `OTA_START`.

### OTA_END (7 octets)

| Offset | Taille | Champ |
|---|---|---|
| 0 | 1 | `0x18` |
| 1 | 1 | seq |
| 2-5 | 4 | CRC32 du firmware complet (uint32 BE) |
| 6 | 1 | réservé (`0x00`) |

### OTA_ACK (7 octets)

| Offset | Taille | Champ |
|---|---|---|
| 0 | 1 | `0x19` |
| 1 | 1 | seq (celui du paquet acquitté) |
| 2-3 | 2 | index (uint16 BE) — `0xFFFF` pour START/END ; avec fenêtre : **prochain bloc attendu** ; sans fenêtre : bloc acquitté |
| 4 | 1 | status (voir table) |
| 5 | 1 | fenêtre W retenue (`0` = firmware sans fenêtre ou pas d'OTA en cours) |
| 6 | 1 | réservé |

**Codes status :**

| Code | Signification |
|---|---|
| `0x00` | OK |
| `0x01` | BUSY |
| `0x02` | BLOCK_BAD_CRC |
| `0x03` | FLASH_ERROR |
| `0x04` | OUT_OF_RANGE |
| `0x05` | FW_TOO_BIG |
| `0x06` | FW_BAD_CRC |
| `0x07` | NOT_STARTED |

### OTA_STATUS (8 octets)

| Offset | Taille | Champ |
|---|---|---|
| 0 | 1 | `0x1A` |
| 1 | 1 | seq |
| 2 | 1 | état courant de la machine OTA |
| 3-4 | 2 | nombre de blocs reçus (uint16 BE) |
| 5-6 | 2 | index du dernier bloc reçu (uint16 BE) |
| 7 | 1 | réservé |

---

## 11. Recommandations d’implémentation côté récepteur

### 11.1 Réception des données (montée)

1. **Mémoriser `mode` et `code tarif` par émetteur.** Les paquets EXTENDED
   (hors POWER_MAX_CFG et TARIFF_LABEL) ne portent pas le mode. Sans cette
   mémorisation, impossible de nommer correctement les index (`EASF01` vs
   `HCHC` vs `BBRHCJB`).

2. **Masquer les champs sans signification** plutôt que d'afficher des zéros :
   `EAIT`/`SMAXIN`/`SINSTI`/`CCAIN`/`URMS*`/`UMOY*` en Historique, tiers 3-6 hors Tempo.

3. **Attention aux unités** : courants en mA (Standard) vs A (Historique).
   `CCASN`/`CCASN-1` sont **signés** (int16), contrairement aux autres puissances.

4. **Multi-émetteur** : essayer chaque clé connue au déchiffrement. Le MIC sert
   de discriminant — si aucune clé ne valide, jeter silencieusement (c'est du
   bruit ou un émetteur d'un autre réseau).

5. **Suivi de séquence par émetteur** (pas de compteur global) pour calculer un
   PDR correct.

6. **Avant le premier POWER_MAX_CFG** (~3 min après le boot), l'option tarifaire
   Historique est inconnue : afficher des libellés génériques (`Tier1`, `Tier2`)
   plutôt que de masquer la donnée.

### 11.2 Interrogation d'attribut (descente)

Partie optionnelle. Elle repose entièrement sur la fenêtre de 300 ms qui suit
chaque uplink (§8).

#### Ce qu'il faut mettre en place

**Une file de requêtes par émetteur.** Le récepteur ne choisit pas quand il
émet : il doit attendre d'avoir entendu un uplink de l'émetteur visé. Une
requête reste donc en attente jusqu'au prochain uplink de ce ZLinky, soit
jusqu'à ~7 à 9 s (cf. §5).

**Émettre immédiatement après le RX_DONE de l'uplink.** Le budget est de
300 ms, dont ~60 ms consommés par la détection de préambule côté ZLinky. Il
reste de quoi déchiffrer, consulter la file et démarrer le TX, mais il ne faut
pas insérer de traitement lourd entre les deux (écriture disque, publication
MQTT, log verbeux…).

Pas de compteur ni de confirmation à gérer : une lecture est idempotente.

#### Boucle type

```
sur RX_DONE d'un uplink de l'émetteur N :
    déchiffrer, vérifier le MIC
    publier la donnée                     ← peut être fait après
    si file[N] non vide :
        req = file[N].tête                 ← (cluster, attribut)
        chiffrer POLL_REQUEST avec la clé de N
        transmettre immédiatement
        passer en RX, attendre le POLL_RESPONSE (~500 ms)
        décoder selon le type ZCL renvoyé
```

#### Décodage de la réponse

Le `type ZCL` (octet [7]) dit comment lire la valeur : entier big-endian
(uint8/16/32/48, int8/16) ou chaîne (octets bruts). Le récepteur n'a donc pas
besoin de connaître à l'avance le type de chaque attribut.

Un statut `UNKNOWN_ATTR` (avec N = 0) signifie que le firmware ne mappe pas ce
couple (cluster, attribut) — voir la liste en §8.

#### Points d'attention

| Sujet | À retenir |
|---|---|
| Latence | une requête attend le prochain uplink, ~7 à 9 s typiquement |
| Idempotence | rejouer une requête est sans effet de bord, juste une relecture |
| Perte de la réponse | simplement réémettre la requête au prochain uplink |
| Fraîcheur | la valeur renvoyée est celle du dernier échantillonnage TIC (jusqu'à un cycle de retard), pas une lecture instantanée du compteur |
| SF / canal | ne se changent PAS par ce canal — refaire un appairage (§4) |
| Multi-émetteur | les fenêtres des différents ZLinky ne sont pas synchronisées, chacune se traite indépendamment |
