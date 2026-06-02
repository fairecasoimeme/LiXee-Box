# Système de règles — Guide complet

Le moteur de règles permet d'automatiser des actions sur les appareils Zigbee en réponse à des conditions mesurées (capteurs, heure, calendrier). Chaque règle est évaluée périodiquement (mode **timer**) ou à chaque mise à jour d'un attribut spécifique (mode **événement**).

---

## Table des matières

1. [Structure d'une règle](#1-structure-dune-règle)
2. [Déclencheur (Trigger)](#2-déclencheur-trigger)
3. [Conditions](#3-conditions)
4. [Logique entre conditions (ET / OU)](#4-logique-entre-conditions-et--ou)
5. [Options d'évaluation](#5-options-dévaluation)
6. [Actions (SI)](#6-actions-si)
7. [Actions contraires (SINON)](#7-actions-contraires-sinon)
8. [Variables dans les notifications](#8-variables-dans-les-notifications)
9. [Exemples complets](#9-exemples-complets)
10. [Cas avancés](#10-cas-avancés)

---

## 1. Structure d'une règle

| Champ | Description |
|---|---|
| **Nom** | Identifiant unique de la règle |
| **Activée** | Case à cocher pour activer/désactiver sans supprimer |
| **Déclencheur** | Quand la règle est évaluée (timer ou événement) |
| **Conditions** | Liste de critères à satisfaire (ET/OU) |
| **Durée de maintien** | Nombre de minutes pendant lesquelles les conditions doivent rester vraies avant d'exécuter les actions |
| **Mode d'exécution** | Une seule fois / Répété à chaque évaluation |
| **Intervalle min** | Pause minimale entre deux exécutions (mode répété uniquement) |
| **Max/jour** | Nombre maximal d'exécutions par journée (0 = illimité) |
| **Actions SI** | Ce qui se passe quand les conditions sont vraies |
| **Actions SINON** | Ce qui se passe quand les conditions redeviennent fausses |

---

## 2. Déclencheur (Trigger)

### Mode Timer
La règle est évaluée automatiquement à intervalles réguliers par le firmware (toutes les ~30 secondes). C'est le mode par défaut, adapté aux règles continues basées sur des seuils.

**Exemple :** Vérifier toutes les 30 s si la température dépasse 25 °C.

### Mode Événement
La règle est évaluée **uniquement** quand un attribut précis d'un appareil Zigbee est mis à jour. Ce mode est plus réactif et économe en CPU.

**Configuration requise :**
- Appareil Zigbee source
- Cluster (ex : `0x0402 - Temperature`)
- Attribut (ex : `Température mesurée`)

**Exemple :** Déclencher immédiatement à la réception d'une nouvelle mesure de température du capteur salon.

> **Conseil :** Préférez le mode événement pour des réactions immédiates (détection de présence, ouverture de porte). Utilisez le mode timer pour surveiller des grandeurs qui changent lentement (température ambiante, énergie).

---

## 3. Conditions

Plusieurs types de conditions peuvent être combinés dans une même règle.

### 3.1 Appareil Zigbee (`device`)

Compare la valeur actuelle d'un attribut Zigbee à une valeur seuil.

| Champ | Description |
|---|---|
| **Appareil** | Sélectionner parmi les appareils appairés (alias ou modèle) |
| **Cluster** | Groupe fonctionnel Zigbee (ex : `0x0402 Temperature`) |
| **Attribut** | Grandeur mesurée (ex : `Température mesurée`) |
| **Opérateur** | `==` `!=` `<` `<=` `>` `>=` |
| **Valeur** | Seuil de comparaison (en unité réelle après application du coefficient) |

La valeur actuelle est affichée en vert sous le sélecteur d'attribut pour faciliter le paramétrage.

**Le coefficient du template est automatiquement appliqué** (ex : valeur brute Zigbee `09C4` hex → 2500 → ×0.01 = 25,00 °C).

**Exemples :**
```
Capteur salon - Température mesurée  >  25     → déclenche si température > 25 °C
Capteur cave  - Humidité             >= 80     → déclenche si humidité ≥ 80 %
Capteur porte - IAS Zone             == 1      → déclenche si la porte est ouverte
Prise connectée - Puissance active   >  2000   → déclenche si puissance > 2000 W
```

---

### 3.2 Comparer 2 appareils (`device_compare`)

Compare la valeur d'un premier appareil à celle d'un second, avec un offset optionnel.

| Champ | Description |
|---|---|
| **Appareil 1** | Premier capteur (valeur de gauche) |
| **Cluster / Attribut 1** | Attribut à lire sur l'appareil 1 |
| **Opérateur** | `>` `>=` `<` `<=` `==` `!=` |
| **Offset** | Valeur ajoutée à la valeur de l'appareil 2 avant comparaison (peut être négatif) |
| **Appareil 2** | Second capteur (valeur de référence) |
| **Cluster / Attribut 2** | Attribut à lire sur l'appareil 2 |

**Formule évaluée :** `valeur1  OP  (valeur2 + offset)`

La valeur actuelle de chaque appareil est affichée sous chaque sélecteur d'attribut.

**Exemples :**
```
Temp. salon > Temp. extérieur              → salon plus chaud que l'extérieur
Temp. salon > Temp. extérieur + 5         → salon au moins 5 °C plus chaud
Puissance circuit A > Puissance circuit B  → circuit A consomme plus
Humidité chambre > Humidité couloir + 10  → chambre beaucoup plus humide
```

---

### 3.3 Heure précise (`time`)

Compare l'heure courante (HH:MM) à une heure définie.

| Opérateur | Signification |
|---|---|
| `==` | Exactement à cette heure (à la minute près) |
| `!=` | À toute heure sauf celle-ci |
| `<` | Avant cette heure |
| `<=` | Avant ou à cette heure |
| `>` | Après cette heure |
| `>=` | À partir de cette heure |

**Exemples :**
```
Heure >= 08:00   → la matinée a commencé
Heure <  22:00   → avant 22h
Heure == 07:30   → pile à 7h30 (recommandé avec mode événement ou répété)
```

---

### 3.4 Plage horaire (`time_range`)

Vérifie si l'heure courante est dans une plage définie.

| Champ | Description |
|---|---|
| **Heure début** | Début de la plage (HH:MM) |
| **Heure fin** | Fin de la plage (HH:MM) |
| **Opérateur** | `dans la plage` ou `hors de la plage` |

> **Plage à cheval sur minuit** : Si l'heure de début est **après** l'heure de fin (ex : `22:00` → `06:00`), la plage englobe minuit automatiquement.

**Exemples :**
```
08:00 → 22:00  dans la plage   → heures diurnes
22:00 → 07:00  dans la plage   → heures nocturnes (traverse minuit)
12:00 → 14:00  hors de la plage → en dehors de la pause déjeuner
```

---

### 3.5 Jour de semaine (`weekday`)

Sélectionnez un ou plusieurs jours (lundi = 1, dimanche = 7).

- **Opérateur `in`** : le jour courant est dans la liste sélectionnée
- **Opérateur `not_in`** : le jour courant n'est pas dans la liste

**Exemples :**
```
Lundi, Mardi, Mercredi, Jeudi, Vendredi  →  en semaine
Samedi, Dimanche                          →  le week-end
Lundi uniquement                          →  début de semaine
```

---

### 3.6 Date (`date`)

Compare la date courante à une date précise (récurrente ou ponctuelle).

| Option | Description |
|---|---|
| **Récurrent** (case cochée) | Ignorer l'année → condition vraie tous les ans à cette date |
| **Ponctuel** (case décochée) | L'année est prise en compte |

| Opérateur | Signification |
|---|---|
| `==` | Ce jour précis |
| `!=` | Tout sauf ce jour |
| `<` | Avant cette date |
| `>` | Après cette date |

**Exemples :**
```
25/12  ==  récurrent   → chaque 25 décembre
01/07/2025  >=          → à partir du 1er juillet 2025
31/10  ==  récurrent   → chaque Halloween
```

---

### 3.7 Date et heure (`datetime`)

Combine une date et une heure pour un point précis dans le temps. Utile pour des automatisations ponctuelles.

| Opérateur | Signification |
|---|---|
| `==` | Exactement à ce moment (à la minute) |
| `<` | Avant ce moment |
| `>` | Après ce moment |

**Exemple :**
```
01/01/2026 00:00  ==   → bonne année ! (avec mode répété désactivé)
15/08/2025 08:00  >=   → à partir du 15 août 2025 matin
```

---

### 3.8 Jour du mois (`day`)

Compare le numéro du jour (1–31) au jour courant.

**Exemples :**
```
Jour == 1    → le 1er de chaque mois
Jour >= 15   → deuxième quinzaine
```

---

### 3.9 Mois (`month`)

Compare le mois courant (1 = janvier, 12 = décembre).

**Exemples :**
```
Mois == 12   → décembre
Mois >= 6 ET Mois <= 8   → été (juin, juillet, août)
```

---

## 4. Logique entre conditions (ET / OU)

Chaque condition après la première est précédée d'un séparateur logique :

- **ET (AND)** : toutes les conditions doivent être vraies simultanément
- **OU (OR)** : il suffit qu'une condition soit vraie

Les conditions sont évaluées **de gauche à droite** avec court-circuit :
- Un `AND` faux arrête immédiatement l'évaluation (résultat = faux)
- Un `OR` vrai arrête immédiatement l'évaluation (résultat = vrai)

**Exemple — Chauffage nocturne week-end :**
```
Température salon  <   18 °C
    ET
Plage horaire  22:00 → 08:00  dans la plage
    ET
Jour de semaine  Samedi, Dimanche
```

**Exemple — Alerte chaleur ou humidité excessive :**
```
Température chambre  >  28 °C
    OU
Humidité chambre    >  85 %
```

---

## 5. Options d'évaluation

### Durée de maintien (minutes)

Les conditions doivent rester vraies **en continu** pendant cette durée avant que les actions soient exécutées. Cela évite les faux déclenchements sur des pics transitoires.

```
Durée = 5 min + Température > 25 °C
→ Le chauffage ne se déclenche que si la température reste > 25 °C pendant 5 minutes
```

### Mode d'exécution

| Mode | Comportement |
|---|---|
| **Une seule fois** | Les actions SI sont exécutées une seule fois à la transition FAUX→VRAI. Les actions SINON sont exécutées une seule fois à la transition VRAI→FAUX. |
| **Répété** | Les actions SI sont ré-exécutées à chaque cycle d'évaluation tant que les conditions restent vraies. |

### Intervalle minimum entre exécutions (mode répété uniquement)

Définit une pause minimale en minutes entre deux exécutions successives. Évite de spammer des commandes ou des notifications.

```
Mode répété + Intervalle min = 30 min
→ Les actions sont exécutées au plus une fois toutes les 30 minutes
```

### Max/jour

Limite le nombre total d'exécutions sur une journée calendaire (remise à zéro à minuit).

```
Max/jour = 3
→ Les actions ne seront déclenchées que 3 fois au maximum dans la journée
```

---

## 6. Actions (SI)

Les actions SI s'exécutent lorsque toutes les conditions sont vérifiées.

### 6.1 Appareil (`device`)

Envoie une commande prédéfinie à un appareil Zigbee, en utilisant les actions disponibles dans son template.

| Champ | Description |
|---|---|
| **Appareil** | Appareil cible |
| **Action** | Commande du template (ex : `ON`, `OFF`, `SetLevel`, `SetPoint`) |
| **Endpoint** | Endpoint Zigbee (défaut : 1) |

**Exemples :**
```
Volet salon     → Ouvrir
Ampoule bureau  → OFF
Thermostat      → SetPoint (température consigne)
Prise connectée → OFF
```

---

### 6.2 Valeur dynamique (`dynamic`)

Lit la valeur d'un appareil source, applique un calcul linéaire (`coeff × valeur + offset`), puis envoie le résultat à un appareil cible. Idéal pour transmettre une mesure d'un capteur vers un actionneur.

| Champ | Description |
|---|---|
| **Appareil source** | Appareil dont on lit la valeur |
| **Cluster source** | Cluster de l'attribut source |
| **Attribut source** | Attribut à lire |
| **Coefficient** | Multiplicateur (défaut : 1.0) |
| **Offset** | Valeur ajoutée après multiplication (défaut : 0) |
| **Appareil cible** | Appareil qui reçoit la commande |
| **Action cible** | Commande du template de l'appareil cible |
| **Endpoint** | Endpoint Zigbee de l'appareil cible |

**Formule :** `résultat = round(valeur_source × coefficient + offset)`

**Exemples :**

*Transmettre directement la consigne de température d'un capteur à un thermostat :*
```
Source : Capteur extérieur → Température mesurée
Coeff = 1.0  |  Offset = 0
Cible : Thermostat salon → SetPoint
→ La consigne du thermostat suit la température extérieure
```

*Convertir des watts en pourcentage pour un variateur :*
```
Source : Compteur solaire → Puissance active  (0–3000 W)
Coeff = 0.033  |  Offset = 0
Cible : Variateur → SetLevel  (0–100 %)
→ 1500 W → niveau 50 %
```

*Décaler une consigne de +2 degrés :*
```
Source : Capteur chambre → Température mesurée
Coeff = 1.0  |  Offset = 2
Cible : Thermostat chambre → SetPoint
→ Consigne = température lue + 2 °C
```

---

### 6.3 Notification (`notification`)

Envoie une notification interne avec un titre et un message. Les [variables dynamiques](#8-variables-dans-les-notifications) sont substituées au moment de l'envoi.

| Champ | Description |
|---|---|
| **Titre** | Titre de la notification (supporte les variables) |
| **Message** | Corps du message (supporte les variables) |

Les valeurs actuelles des capteurs impliqués dans les conditions sont automatiquement ajoutées en pied de notification.

---

## 7. Actions contraires (SINON)

Les actions SINON s'exécutent **une seule fois** lors de la transition de l'état VRAI → FAUX, c'est-à-dire quand les conditions qui étaient vraies redeviennent fausses. Elles supportent les mêmes types que les actions SI.

**Exemple :**
```
Condition : Température salon > 26 °C

Actions SI    → Activer ventilateur (ON)
Actions SINON → Désactiver ventilateur (OFF)
```

> **Important :** Les actions SINON ne se déclenchent que s'il y a eu au préalable une transition FAUX→VRAI (la règle a déjà déclenché ses actions SI).

---

## 8. Variables dans les notifications

Le titre et le message d'une notification peuvent contenir des variables remplacées dynamiquement :

| Variable | Contenu |
|---|---|
| `{rule}` | Nom de la règle |
| `{date}` | Date et heure de déclenchement formatée |
| `{value}` | Valeur du 1er capteur (condition device) avec unité |
| `{device}` | Alias/modèle du 1er appareil |
| `{threshold}` | Seuil défini dans la 1ère condition device |
| `{value_N}` | Valeur du Nème capteur (N = 1, 2, 3…) |
| `{device_N}` | Alias/modèle du Nème appareil |

**Exemple de titre :**
```
Alerte température — {device}
```

**Exemple de message :**
```
La {device} mesure {value} (seuil : {threshold}).
Relevé le {date}.
```

**Résultat rendu :**
```
Alerte température — Capteur Salon
La Capteur Salon mesure 27,5 °C (seuil : 25).
Relevé le 01/06/2025 14:32.
```

---

## 9. Exemples complets

### Exemple 1 — Volet automatique le matin en semaine

**Objectif :** Ouvrir le volet du salon à 7h30 du lundi au vendredi.

```
Nom        : Volet salon matin
Déclencheur: Timer

Conditions :
  Heure précise  ==  07:30
  ET Jour de semaine  in  Lundi, Mardi, Mercredi, Jeudi, Vendredi

Mode       : Une seule fois
Durée      : 0 min

Actions SI :
  Volet salon → Ouvrir
```

---

### Exemple 2 — Alerte température avec notification

**Objectif :** Être alerté si la cave dépasse 12 °C et activer un ventilateur.

```
Nom        : Température cave
Déclencheur: Timer

Conditions :
  Capteur cave — Température mesurée  >  12

Durée      : 5 min  (évite les pics transitoires)
Mode       : Une seule fois

Actions SI :
  Notification — "⚠️ Cave surchauffée" / "Température {value} (seuil 12°C) — {date}"
  Ventilateur cave → ON

Actions SINON :
  Ventilateur cave → OFF
```

---

### Exemple 3 — Présence nocturne

**Objectif :** Allumer une veilleuse la nuit si un mouvement est détecté.

```
Nom        : Veilleuse nuit
Déclencheur: Événement sur Capteur couloir — Occupancy (0x0406)

Conditions :
  Capteur couloir — Occupancy  ==  1
  ET Plage horaire  22:00 → 07:00  dans la plage

Mode       : Répété
Intervalle min : 1 min  (évite le clignotement)
Max/jour   : 0 (illimité)

Actions SI :
  Veilleuse couloir → ON

Actions SINON :
  Veilleuse couloir → OFF
```

---

### Exemple 4 — Comparaison de deux capteurs (double zone)

**Objectif :** Ouvrir une fenêtre de ventilation quand le salon est plus chaud que l'extérieur de 3 °C minimum.

```
Nom        : Ventilation naturelle
Déclencheur: Timer

Conditions :
  Capteur salon — Température  >  Capteur extérieur  (offset : +3)

Durée      : 10 min
Mode       : Une seule fois

Actions SI :
  Fenêtre salon → Ouvrir

Actions SINON :
  Fenêtre salon → Fermer
```

---

### Exemple 5 — Régulation solaire dynamique

**Objectif :** Moduler le niveau d'un chauffe-eau en fonction de la puissance solaire disponible.

```
Nom        : Chauffe-eau solaire
Déclencheur: Événement sur Compteur solaire — Puissance (0x0B04)

Conditions :
  Compteur solaire — Puissance active  >  500  (W)

Mode       : Répété
Intervalle min : 5 min

Actions SI (valeur dynamique) :
  Source : Compteur solaire → Puissance active
  Coeff = 0.02  |  Offset = 0
  Cible : Relais chauffe-eau → SetLevel
  → 500 W → 10 % | 2500 W → 50 % | 5000 W → 100 %

Actions SINON :
  Relais chauffe-eau → OFF
```

---

### Exemple 6 — Gel hivernal

**Objectif :** Activer le chauffage de la véranda si la température extérieure passe sous 2 °C pendant la nuit, mais seulement d'octobre à mars.

```
Nom        : Anti-gel véranda
Déclencheur: Timer

Conditions :
  Capteur extérieur — Température  <  2
  ET Plage horaire  20:00 → 08:00  dans la plage
  ET Mois  >=  10
  OU Mois  <=  3

Durée      : 2 min
Mode       : Une seule fois
Max/jour   : 5

Actions SI :
  Chauffage véranda → ON
  Notification — "Anti-gel activé" / "Temp. extérieure : {value}. Gel prévu."

Actions SINON :
  Chauffage véranda → OFF
```

---

### Exemple 7 — Limite de consommation quotidienne

**Objectif :** Couper la prise d'un appareil énergivore une fois 3 kWh consommés dans la journée.

```
Nom        : Budget énergie four
Déclencheur: Timer

Conditions :
  Prise four — Énergie cumulée  >=  3000  (Wh)

Mode       : Une seule fois
Max/jour   : 1

Actions SI :
  Prise four → OFF
  Notification — "Budget énergie atteint" / "La prise four a consommé {value} Wh aujourd'hui."
```

---

## 10. Cas avancés

### Chaînage de règles via attributs intermédiaires

Il n'existe pas de déclencheur direct "règle B se déclenche quand règle A est vraie". En revanche, vous pouvez utiliser un **groupe Zigbee** ou une **prise virtuelle** comme intermédiaire : la règle A active un attribut, la règle B surveille cet attribut.

### Migration automatique des anciennes plages horaires

Les règles créées avec l'ancienne interface utilisant des `timeRanges` sont **automatiquement converties** au chargement en conditions de type `time_range` + `weekday`. Aucune action manuelle n'est requise.

### Stockage en PSRAM

Toutes les règles, conditions et actions sont stockées en PSRAM (mémoire externe) pour préserver la RAM interne de l'ESP32-S3. La limite pratique dépend de la taille disponible en PSRAM (8 Mo sur la plupart des modules S3).

### Persistance de l'état

L'état de chaque règle (`0` = faux, `1` = vrai déclenché, `2` = en attente de durée) est persisté dans `/config/statusRules.json` sur LittleFS. Cela permet de :
- Retrouver l'état après un redémarrage
- Éviter de ré-exécuter les actions SI au boot si la condition était déjà vraie

### Format de la valeur dans les conditions `device`

La valeur de comparaison s'exprime **dans l'unité affichée à l'écran**, après application du coefficient du template. Inutile de saisir la valeur hexadécimale brute Zigbee.

```
Température : saisir 21.5 (et non 0865 hex)
Humidité    : saisir 65   (et non 1F40 hex)
Puissance   : saisir 1500 (et non 17D0 hex)
```

### Opérateurs textuels

Pour les attributs dont la valeur est une chaîne de caractères (ex : état d'une IAS Zone : `"open"`, `"close"`), les opérateurs `==` et `!=` effectuent une **comparaison insensible à la casse**.
