# Thermostat virtuel — Guide climatisation

Ce guide complète [THERMOSTATS.md](THERMOSTATS.md) en se concentrant sur la **gestion des climatiseurs** (unités réversibles HVAC Zigbee). Si vous pilotez une prise ou un relais tout-ou-rien, reportez-vous au guide général.

---

## Table des matières

1. [Spécificités d'un climatiseur Zigbee](#1-spécificités-dun-climatiseur-zigbee)
2. [Préparer l'appareil : bind et reporting](#2-préparer-lappareil--bind-et-reporting)
3. [Configurer la zone — pas à pas](#3-configurer-la-zone--pas-à-pas)
4. [Choisir la sonde de température](#4-choisir-la-sonde-de-température)
5. [Mode Chaud / Mode Froid](#5-mode-chaud--mode-froid)
6. [Régulation par hystérésis — pourquoi et comment](#6-régulation-par-hystérésis--pourquoi-et-comment)
7. [Anti-court-cycle compresseur](#7-anti-court-cycle-compresseur)
8. [Setpoint interne de la clim](#8-setpoint-interne-de-la-clim)
9. [Priorités et inhibitions](#9-priorités-et-inhibitions)
10. [Lire le cadran d'une zone clim](#10-lire-le-cadran-dune-zone-clim)
11. [Cas d'usage courants](#11-cas-dusage-courants)
12. [FAQ climatisation](#12-faq-climatisation)

---

## 1. Spécificités d'un climatiseur Zigbee

Un climatiseur réversible Zigbee expose le cluster **0x0201 (Thermostat HVAC)**. Il s'agit d'un appareil qui possède **sa propre boucle de régulation interne** et qui s'attend à recevoir un **mode de fonctionnement** (HEAT / COOL / AUTO / OFF) et un **setpoint**, pas un simple on/off.

| Caractéristique | Prise on/off | Climatiseur HVAC |
|---|---|---|
| Cluster principal | 0x0006 | 0x0201 |
| Commande envoyée | ON / OFF | Mode + setpoint |
| Régulation interne | Non | Oui (PID intégré) |
| Algorithme LiXee-Box | **TPI** (PWM lent) | **Hystérésis** |
| Capteur de temp possible | Externe seulmt | Interne **ou** externe |

> La LiXee-Box **détecte automatiquement** le type d'actionneur au moment du choix de l'appareil et sélectionne le bon algorithme.

---

## 2. Préparer l'appareil : bind et reporting

Avant de configurer la zone, vérifiez que votre climatiseur est correctement appairé et envoie ses états à la box.

### Reporting minimum requis

Le cadran affiche l'état **réel** de la clim (pas seulement la dernière commande) : il faut que la box reçoive les rapports du cluster 0x0201.

| Attribut | Description | Attribut Zigbee |
|---|---|---|
| Température locale | Sonde interne (optionnel) | 0x0000 |
| Setpoint chaud | Consigne chauffage | 0x0012 |
| Setpoint froid | Consigne refroidissement | 0x0011 |
| System Mode | Mode actif (HEAT/COOL/OFF…) | 0x001C |
| Running Mode | Vraie action en cours | 0x001E |

Si ces attributs ne remontent pas, **ré-appairez l'appareil** ou configurez le Configure Reporting depuis l'interface Zigbee de la LiXee-Box (Réseau → Zigbee → appareil → Configurer).

> Un rapport manquant se traduit par un cadran figé ou un statut « Régule » même quand la clim tourne.

---

## 3. Configurer la zone — pas à pas

Dans **Config → Thermostat**, cliquez **« + »** pour créer une zone ou **✏️** pour en modifier une.

### 3.1 Mode de régulation

Sélectionnez le mode **selon l'usage principal** :

| Choix | Quand l'actionneur s'active |
|---|---|
| **Chauffage** | Température < consigne |
| **Rafraîchissement** | Température > consigne |

Pour une clim réversible utilisée en été **et** en hiver, choisissez le mode correspondant à la **saison en cours** ; vous changerez manuellement via le sélecteur Chaud/Froid sur la vignette (voir §5). Il est également possible de créer **deux zones distinctes** (une Chauffage, une Rafraîchissement) si vous voulez des consignes et des plages horaires différentes selon la saison.

### 3.2 Consigne et hors-gel

- **Consigne** : température cible (ex. 26 °C en été, 20 °C en hiver).
- **Hors-gel** : applicable uniquement en mode Chauffage (ex. 7 °C). Laissez la valeur par défaut en mode Rafraîchissement, elle ne s'applique pas.

### 3.3 Appareil piloté

1. Cliquez sur le champ **Appareil piloté** et sélectionnez votre climatiseur dans la liste (il doit être appairé).
2. Dès la sélection d'un appareil HVAC (cluster 0x0201), trois champs d'actions apparaissent :

| Champ | Description | Valeur typique |
|---|---|---|
| **Action Chaud** | Ordre envoyé quand la box veut chauffer | `HEAT` |
| **Action Froid** | Ordre envoyé quand la box veut refroidir | `COOL` |
| **Action Arrêt** | Ordre envoyé pour stopper la clim | `OFF` |

Renseignez **les trois** pour une clim réversible. Si vous n'utilisez que le froid (ex. climatiseur non réversible), laissez « Action Chaud » vide.

> Les valeurs exactes (libellés) dépendent du **template** de votre appareil dans la LiXee-Box. Elles correspondent aux actions déclarées dans ce template.

### 3.4 Capteur de température

Voir §4 pour le choix entre sonde interne et sonde externe.

### 3.5 Capteurs d'inhibition (optionnel)

- **Présence** : utile pour ne rafraîchir qu'une pièce occupée.
- **Ouverture** : contact fenêtre/porte — coupe la clim dès qu'une fenêtre est ouverte, ce qui est particulièrement important en climatisation.

---

## 4. Choisir la sonde de température

Deux sources sont disponibles pour les climatiseurs HVAC :

### Option A — Sonde externe (recommandée)

Un capteur Zigbee indépendant (ex. Aqara WSDCGQ11LM, Sonoff SNZB-02…) placé dans la pièce, loin de la clim et des courants d'air directs.

**Avantages** : mesure représentative de la température ambiante réelle, indépendante des variations de la clim elle-même.

**Inconvénient** : un appareil de plus à gérer, pile à changer.

### Option B — Sonde interne de la clim

La LiXee-Box peut lire la **température locale** (attribut 0x0000 du cluster 0x0201) de la clim elle-même.

**Avantages** : aucun capteur supplémentaire, l'appareil est déjà appairé.

**Inconvénient** : la sonde est dans la clim (en hauteur, près du flux d'air), elle peut être biaisée par rapport à la température ressentie dans la pièce. À tester selon votre installation.

> En pratique, si votre clim est bien positionnée et que la pièce est homogène (pas trop grande), la sonde interne est souvent suffisante. Pour une grande pièce ou un placement en hauteur, préférez une sonde externe au niveau de vie.

---

## 5. Mode Chaud / Mode Froid

Quand **Action Chaud ET Action Froid** sont tous les deux renseignés, la vignette affiche un sélecteur **Chaud / Froid** :

```
[ Chaud ]  [ Froid ]
```

Ce sélecteur change le **sens de régulation** à la volée, sans retourner dans la configuration :

| Sélecteur | Mode actif | Action envoyée quand écart dépasse hystérésis |
|---|---|---|
| **Chaud** | Chauffage | `HEAT` si temp < consigne, sinon `OFF` |
| **Froid** | Rafraîchissement | `COOL` si temp > consigne, sinon `OFF` |

> Le mode sélectionné est **persisté** : il survit à un redémarrage de la box.

### Icônes du cadran

| Icône | Couleur | Signification |
|---|---|---|
| 🔥 flamme | Orange | Clim en mode HEAT, compresseur actif |
| ❄️ flocon | Bleu | Clim en mode COOL, compresseur actif |
| 🔥 / ❄️ | Gris | Clim en marche mais compresseur au repos (temp OK) |
| — | — | Clim à l'arrêt (OFF) |

---

## 6. Régulation par hystérésis — pourquoi et comment

### Pourquoi pas du TPI pour une clim ?

Le TPI (Time Proportional & Integral) envoie des ON/OFF rapprochés pour moduler la puissance. Un climatiseur **n'est pas conçu pour ça** :

- Le compresseur est **protégé contre les démarrages fréquents** (risque mécanique, surconsommation électrique).
- La clim a **son propre PID interne** : lui imposer des cycles en plus crée des conflits.
- Chaque commande reçue peut déclencher un **bip** ou une animation d'affichage.

### Fonctionnement de l'hystérésis

La box maintient en mémoire le **dernier ordre envoyé** à la clim. Un nouvel ordre n'est émis que si la température franchit un **seuil** :

```
Bande morte : ±0,3 °C autour de la consigne

Mode Froid (ex. consigne 24 °C) :
  temp > 24,3 °C  →  envoie COOL  (si pas déjà en COOL)
  temp < 23,7 °C  →  envoie OFF   (si pas déjà OFF)
  entre les deux  →  aucune commande

Mode Chaud (ex. consigne 20 °C) :
  temp < 19,7 °C  →  envoie HEAT  (si pas déjà en HEAT)
  temp > 20,3 °C  →  envoie OFF   (si pas déjà OFF)
  entre les deux  →  aucune commande
```

La clim gère elle-même sa modulation interne pour atteindre la consigne reçue (voir §8).

---

## 7. Anti-court-cycle compresseur

Des durées minimales de marche (`minOn`) et d'arrêt (`minOff`) empêchent les basculements trop rapides :

| Paramètre | Défaut | Recommandation clim |
|---|---|---|
| **Durée ON min** | 180 s (3 min) | 300–600 s (5–10 min) |
| **Durée OFF min** | 180 s (3 min) | 300–600 s (5–10 min) |

Ces valeurs se modifient dans **Paramètres avancés de régulation** du formulaire de zone.

> Augmentez ces durées si votre clim démarre/s'arrête trop souvent. La plupart des compresseurs ont besoin de 3 à 5 min d'arrêt minimum avant redémarrage ; certains fabricants recommandent 10 min.

---

## 8. Setpoint interne de la clim

La LiXee-Box envoie un **mode** (HEAT / COOL / OFF) à la clim, mais pas nécessairement un setpoint de température. Si le setpoint interne de votre clim est réglé à une valeur « normale » (ex. 23 °C en mode COOL), sa propre régulation peut **s'opposer** à celle de la box (la clim s'arrête d'elle-même avant que la box le décide).

**Pratique recommandée** : réglez le setpoint interne de la clim (depuis sa télécommande ou son interface Zigbee) à une valeur **extrême** :

| Mode | Setpoint interne conseillé |
|---|---|
| **COOL** (rafraîchissement) | 16 °C (ou minimum possible) |
| **HEAT** (chauffage) | 30 °C (ou maximum possible) |

Ainsi, **c'est toujours la LiXee-Box qui décide** quand la clim est ON ou OFF, en envoyant le mode, sans que la régulation interne de la clim prenne le dessus.

> Certaines clims Zigbee (ex. Mitsubishi MELCloud, Daikin BRP) ignorent leur setpoint interne quand elles reçoivent un mode Zigbee. Dans ce cas, cette étape est inutile — testez d'abord.

---

## 9. Priorités et inhibitions

Le même ordre de priorité que pour le chauffage s'applique :

1. **Hors-gel** (mode Chauffage uniquement) → envoie HEAT même si inhibé.
2. **Forçage manuel** (boutons Marche / Arrêt sur la vignette).
3. **Inhibitions** : fenêtre ouverte, absence, hors plage/tarif → envoie OFF.
4. **Hystérésis** normale.

### Précautions propres à la climatisation

- **Fenêtre ouverte** : la LiXee-Box coupe la clim (envoie OFF), mais votre clim peut avoir sa propre détection de fenêtre ouverte via sa télécommande. Évitez les doublons de configuration.
- **Mode Rafraîchissement** : le hors-gel (protection antigel) **n'a aucun effet**. Ne vous attendez pas à ce que la clim se déclenche automatiquement en hiver si vous êtes configuré en « Rafraîchissement ».
- **Absence** : en climatisation d'été, l'inhibition par présence évite de refroidir une pièce vide. Pratique dans une chambre occupée seulement la nuit.

---

## 10. Lire le cadran d'une zone clim

Dans **Mesures → Thermostat** :

![Cadran — climatisation (rafraîchissement)](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_Thermostat_Cadran_climatisation.jpg)

| Élément | Signification |
|---|---|
| **Chiffre central** | Consigne active (°C) |
| **Point sur l'arc** | Température réelle mesurée |
| **Arc dégradé animé** | Visible uniquement quand la clim travaille à réduire l'écart (COOL actif et temp > consigne, ou HEAT actif et temp < consigne) |
| **Icône couleur** | 🔥 orange = chauffe / ❄️ bleu = refroidit |
| **Texte statut** | `Rafraîchit`, `Chauffe`, `Régule`, `Fenêtre ouverte`, `Absent`, `Hors plage`, `Tarif …` |
| **Boutons − / +** | Ajustent la consigne de ±0,5 °C sans rouvrir la configuration |
| **Chaud / Froid** | Bascule le sens de régulation (clim réversible uniquement) |
| **Auto / Marche / Arrêt** | Forçage manuel (Marche = envoie le mode actif, Arrêt = envoie OFF) |
| **⚙️** | Accès direct au formulaire de configuration |

> **Statut « Régule »** sans icône colorée = température dans la bande morte, aucune commande envoyée, c'est le comportement normal.

---

## 11. Cas d'usage courants

### Climatisation d'été (pièce de vie)

- **Mode** : Rafraîchissement
- **Capteur** : sonde externe au niveau de vie (ou interne si placement correct)
- **Consigne** : 26 °C (modifiable sur la vignette)
- **Ouverture** : contact fenêtre → coupe automatiquement si fenêtre ouverte
- **Présence** : optionnel (si la pièce est souvent inoccupée)
- **Plages horaires** : ex. 10h-22h pour ne pas refroidir la nuit

### Chauffage d'hiver par clim réversible

- **Mode** : Chauffage (sélecteur vignette → Chaud)
- **Hors-gel** : 7 °C (sécurité en cas d'absence prolongée)
- **Consigne** : 20 °C
- **Tarif Linky** : Heures Creuses → préchauffage la nuit pour économiser

### Clim réversible toute l'année (zone unique)

Créez une seule zone avec **Action Chaud = HEAT** et **Action Froid = COOL**. Depuis la vignette, basculez manuellement Chaud/Froid selon la saison. La consigne s'adapte à chaque mode.

### Chambre (nuit seulement, en présence)

- **Mode** : Rafraîchissement
- **Plages horaires** : 22h-8h
- **Présence** : capteur de présence → coupe si personne dans la chambre
- **Consigne nocturne** : 23 °C (plus douce qu'une pièce de vie)

### Multi-split (plusieurs clims, une zone)

La LiXee-Box supporte **jusqu'à 4 prises/appareils supplémentaires** par zone. Ajoutez les clims secondaires dans « Prises supplémentaires » : elles reçoivent la même commande simultanément.

> Les clims supplémentaires doivent également être préparées (setpoint interne extrême, voir §8).

---

## 12. FAQ climatisation

**Le cadran affiche « Régule » mais la clim ne fait rien.**
C'est normal si la température est dans la bande morte (±0,3 °C autour de la consigne). La clim est ON (mode reçu) mais son compresseur ne tourne pas car la température est acceptable. Vérifiez le setpoint interne (§8) si la clim s'arrête trop tôt.

**La clim émet des bips ou clignote à chaque cycle.**
La box envoie une commande trop souvent. Vérifiez que l'appareil est bien piloté par **actions** (HEAT/COOL/OFF) et non en on/off simple. Assurez-vous également que le reporting du System Mode (0x001C) est actif : si la box ne reçoit pas les confirmations d'état, elle peut renvoyer la même commande.

**La clim ignore les commandes de la LiXee-Box.**
Vérifiez que le **bind** du cluster 0x0201 est correctement configuré entre la clim et le coordinateur ZiGate. En l'absence de bind, la box peut écrire l'attribut directement (write attribute), mais ce n'est pas la méthode recommandée.

**Le sélecteur Chaud/Froid n'apparaît pas sur la vignette.**
Il faut que **Action Chaud ET Action Froid** soient tous deux renseignés dans la configuration de la zone. Si l'un des deux est vide, le sélecteur est masqué.

**La vignette affiche « Chauffe » mais je suis en mode Froid.**
Le texte affiché suit le **System Mode réel** remontant de la clim. Si le reporting n'est pas configuré, la box peut afficher l'état précédent. Configurez le reporting de l'attribut 0x001C (System Mode) sur l'appareil.

**La clim refroidit moins que prévu / ne descend pas sous la consigne.**
Probablement le setpoint interne de la clim qui prend le dessus (§8). Mettez le setpoint COOL interne à 16 °C (minimum). Si cela ne change rien, vérifiez que la clim n'a pas une limite logicielle (certains modèles plafonnent à 19 °C).

**Après un redémarrage de la LiXee-Box, la clim reçoit une commande au démarrage.**
Au démarrage, la box lit le **System Mode** actuel de la clim avant d'émettre quoi que ce soit. Elle adopte l'état existant et ne commande que si l'hystérésis le requiert. Si vous constatez quand même un bip au démarrage, vérifiez que le reporting de 0x001C est actif sur la clim avant le boot de la box.

**J'ai deux clims dans la même pièce. Faut-il deux zones ?**
Non, utilisez la fonctionnalité **Prises supplémentaires** (§11 — Multi-split) : une seule zone pilote les deux clims simultanément avec la même commande.

**En mode Rafraîchissement, le hors-gel ne fonctionne pas.**
Correct et voulu. Le hors-gel est une protection **antigel** (chauffage uniquement). En rafraîchissement, il n'a pas de sens et est ignoré.

---

## Voir aussi

- [THERMOSTATS.md](THERMOSTATS.md) — guide général (toutes les zones, TPI, fil pilote, programmation, tarif Linky)
- [README.md](README.md) — présentation globale de la LiXee-Box
