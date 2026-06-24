# Thermostat virtuel — Guide complet

Le **thermostat virtuel** transforme la LiXee-Box en régulateur de température. Il **découple la mesure de l'action** : un capteur de température Zigbee (quelconque) sert de sonde, et la box pilote un **actionneur** séparé (prise, relais, climatiseur, radiateur fil pilote…) pour maintenir une **consigne**, tout en gérant l'énergie (présence, ouverture de fenêtre, plages horaires, tarif Linky).

On peut définir **jusqu'à 8 zones** indépendantes.

---

## Table des matières

1. [Principe et vue d'ensemble](#1-principe-et-vue-densemble)
2. [Créer et gérer une zone](#2-créer-et-gérer-une-zone)
3. [Régulation : mode, consigne, hors-gel](#3-régulation--mode-consigne-hors-gel)
4. [Mode de fonctionnement (toujours / plages / tarif)](#4-mode-de-fonctionnement-toujours--plages--tarif)
5. [Capteurs (température, présence, ouverture)](#5-capteurs-température-présence-ouverture)
6. [Actionneurs (prise, clim, fil pilote, multi-prises)](#6-actionneurs-prise-clim-fil-pilote-multi-prises)
7. [Algorithmes de régulation (TPI / hystérésis)](#7-algorithmes-de-régulation-tpi--hystérésis)
8. [Paramètres avancés](#8-paramètres-avancés)
9. [La vignette / cadran (lecture et contrôles)](#9-la-vignette--cadran-lecture-et-contrôles)
10. [Priorités et sécurités](#10-priorités-et-sécurités)
11. [Cas d'usage](#11-cas-dusage)
12. [Dépannage (FAQ)](#12-dépannage-faq)

---

## 1. Principe et vue d'ensemble

Un thermostat classique combine sonde et relais dans un même boîtier. Le thermostat virtuel les **sépare** :

- **Sonde** = n'importe quel capteur de température Zigbee déjà appairé (capteur mural, sonde d'un TRV, température interne d'une clim…).
- **Actionneur** = un appareil Zigbee distinct qui chauffe ou refroidit (prise on/off pilotant un radiateur, relais, climatiseur réversible, module fil pilote…).
- **Régulateur** = la LiXee-Box, qui compare la température mesurée à la consigne et commande l'actionneur, toutes les 30 secondes.

À cela s'ajoute la **maîtrise de l'énergie** : la régulation peut être suspendue en cas d'absence, de fenêtre ouverte, en dehors de plages horaires, ou selon le tarif Linky en cours.

Deux pages :

| Page | Menu | Rôle |
|---|---|---|
| **Configuration** | Config → Thermostat | Créer / modifier / supprimer les zones |
| **Visuel** | Mesures → Thermostat | Cadran temps réel + contrôles (consigne, mode, forçage) |

---

## 2. Créer et gérer une zone

Dans **Config → Thermostat**, chaque zone est représentée par une **fiche** (comme les fiches Réseau → Zigbee) :

- **Interrupteur** d'activation : active/désactive la zone sans la supprimer (une zone désactivée coupe son actionneur par sécurité).
- **✏️ Modifier** : ouvre le formulaire de configuration.
- **🗑️ Supprimer** : demande une confirmation avant suppression.
- Bouton **« + »** pour ajouter une nouvelle zone (jusqu'à 8).

La fiche récapitule : Mode, Consigne, Hors-gel, Capteur, Prise (+N si prises supplémentaires), Présence, nombre d'Ouvertures.

![Fiches de configuration des thermostats](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_Thermostat_Config.jpg)

Le formulaire de configuration regroupe la **Régulation** (mode, consigne, hors-gel, fonctionnement), les **Capteurs / actionneurs**, et les **Paramètres avancés** (repliés) :

![Formulaire de configuration d'une zone](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_Thermostat_Edit.jpg)

---

## 3. Régulation : mode, consigne, hors-gel

### Mode

| Mode | Effet |
|---|---|
| **Chauffage** | L'actionneur est activé quand la température est **inférieure** à la consigne |
| **Rafraîchissement** | L'actionneur est activé quand la température est **supérieure** à la consigne |

### Consigne (°C)

Température cible à maintenir. Réglable au formulaire et directement sur la vignette avec les boutons **−/+** (pas de 0,5 °C).

### Hors-gel (°C)

Sécurité antigel, **uniquement en chauffage** (par défaut **7 °C**). C'est une protection **prioritaire** : la zone chauffe dès que la température passe sous ce seuil, **même si** la zone est en arrêt, en absence ou fenêtre ouverte.

> En mode Rafraîchissement, le hors-gel n'a pas de sens et peut être ignoré.

Le hors-gel existe aussi comme **mode bascule** sur la vignette (voir §9) : il abaisse temporairement la consigne au niveau hors-gel et **restaure** la consigne précédente quand on le désactive.

---

## 4. Mode de fonctionnement (toujours / plages / tarif)

Définit **quand** la zone est autorisée à réguler. Le **hors-gel reste actif** dans tous les cas.

| Fonctionnement | Description |
|---|---|
| **Toujours** | Régule en permanence (par défaut) |
| **Plages horaires** | Régule uniquement pendant des créneaux `HH:MM-HH:MM` (plusieurs possibles, gère le passage de minuit) |
| **Tarif Linky** | Régule uniquement pendant les **périodes tarifaires cochées** |

### Mode Tarif Linky

Quand un **ZLinky** est configuré, la zone peut suivre la tarification en cours. Les périodes proposées dépendent de votre **abonnement** détecté :

- **Base** : Toutes Heures
- **Heures Creuses / Heures Pleines** : HC / HP
- **EJP** : Normal / Pointe Mobile
- **Tempo** : Bleu HC/HP, Blanc HC/HP, Rouge HC/HP

Vous cochez les périodes pendant lesquelles la zone **doit réguler** (ex. uniquement en Heures Creuses, ou tout sauf Tempo Rouge HP). En dehors, la régulation est suspendue (hors-gel maintenu).

---

## 5. Capteurs (température, présence, ouverture)

### Capteur de température (obligatoire)

Sonde de la zone. La régulation est **impossible** sans capteur valide. Sont acceptés :

- les capteurs exposant le cluster **0x0402** (sonde de température standard) ;
- la **température locale** d'un thermostat/clim HVAC (cluster **0x0201**, attribut 0).

Si aucune mesure n'est reçue depuis un certain délai (**60 min** par défaut, configurable), le capteur est marqué **HS** et l'actionneur est coupé par sécurité.

### Capteur de présence (optionnel — cluster 0x0406)

Si configuré, la régulation est **suspendue en cas d'absence** (aucun mouvement détecté). Pratique pour ne chauffer/refroidir qu'une pièce occupée.

### Capteurs d'ouverture (optionnel, plusieurs — IAS 0x0500)

Contacts de porte/fenêtre. Si **au moins un** est ouvert, la régulation est **suspendue** (inutile de chauffer fenêtre ouverte). On peut en associer plusieurs à une même zone.

> Présence et ouverture sont des **inhibiteurs** : ils ne coupent pas le hors-gel de sécurité.

---

## 6. Actionneurs (prise, clim, fil pilote, multi-prises)

### Appareil piloté principal

L'appareil qui chauffe/refroidit la zone. C'est lui qui **définit les actions** disponibles :

- **Prise / relais on/off** (cluster 0x0006) : pilotage tout-ou-rien classique.
- **Climatiseur / thermostat HVAC** (cluster 0x0201) : piloté par modes HEAT / COOL / OFF.
- **Radiateur fil pilote** : piloté par ordres CONFORT / ECO / OFF…

### Prises supplémentaires (optionnel)

Une zone peut piloter **plusieurs prises en parallèle** (jusqu'à 4 en plus du principal) : elles reçoivent **exactement la même commande** au même instant. Utile pour chauffer une pièce avec plusieurs convecteurs.

### Actions Chaud / Froid / Arrêt

Pour les appareils pilotés par **action** (clim, fil pilote), on associe une action à chaque état :

| Champ | Exemple clim | Exemple fil pilote |
|---|---|---|
| **Action Chaud** | `HEAT` | `CONFORT` |
| **Action Froid** | `COOL` | — |
| **Action Arrêt** | `OFF` | `OFF` / `ECO` |

- Laissez sur **« Marche/Arrêt simple (on/off) »** pour une prise classique.
- Renseignez **Chaud ET Froid** pour une **clim réversible** : un sélecteur **Chaud / Froid** apparaît alors sur la vignette pour choisir le mode à la volée.

---

## 7. Algorithmes de régulation (TPI / hystérésis)

La box choisit automatiquement l'algorithme selon le type d'actionneur :

### TPI (prises / relais tout-ou-rien)

**Time Proportional & Integral** : modulation par le temps (PWM lent). Sur un cycle (15 min par défaut), l'actionneur est ON une fraction du temps proportionnelle à l'effort calculé :

```
erreur   = consigne − température   (inversé en rafraîchissement)
intégrale = clamp(intégrale + erreur)        (anti-windup)
duty     = clamp(Kp × erreur + Ki × intégrale, 0..1)
```

Cela évite les oscillations brutales d'un simple thermostat tout-ou-rien et lisse la température.

### Hystérésis (clim / appareils pilotés par action)

Pour un appareil qui a sa **propre régulation interne** (climatiseur), le PWM n'a pas de sens et générerait des commandes répétées (bips). La box utilise donc une **hystérésis** avec bande morte de **±0,3 °C** : la commande (HEAT/COOL/OFF) n'est envoyée **qu'au franchissement du seuil**, jamais en boucle. La clim gère ensuite sa propre modulation.

> La **décision d'envoi** d'une clim se base sur le dernier ordre donné (et non sur l'état lu en continu), pour ne pas « se battre » avec le thermostat interne de la clim. L'**affichage**, lui, reflète l'état réel (System Mode HVAC).

### Anti-court-cycle

Des durées **minimales** de marche (`minOn`) et d'arrêt (`minOff`) — 3 min par défaut — empêchent les basculements trop rapprochés qui useraient le matériel (compresseur de clim notamment).

---

## 8. Paramètres avancés

Repliés dans **« Paramètres avancés de régulation (TPI) »** du formulaire :

| Paramètre | Défaut | Rôle |
|---|---|---|
| **Cycle TPI** | 900 s (15 min) | Durée d'un cycle de modulation |
| **Kp** | 0.8 | Gain proportionnel (réactivité à l'écart) |
| **Ki** | 0.1 | Gain intégral (élimine l'erreur résiduelle, borné anti-windup) |
| **Durée ON min** | 180 s | Temps de marche minimal (anti-court-cycle) |
| **Durée OFF min** | 180 s | Temps d'arrêt minimal (anti-court-cycle) |
| **Délai capteur HS** | 3600 s (60 min) | Sans mesure au-delà → capteur « HS », actionneur coupé |

Les valeurs par défaut conviennent à la plupart des cas. Augmentez `minOn/minOff` pour un compresseur de clim, réduisez le cycle TPI pour une régulation plus fine d'un radiateur.

---

## 9. La vignette / cadran (lecture et contrôles)

Dans **Mesures → Thermostat**, chaque zone est un **cadran circulaire** (échelle 5–35 °C) mis à jour en temps réel.

![Cadran — climatisation (rafraîchissement)](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_Thermostat_Cadran_climatisation.jpg)
![Cadran — chauffage (hors-gel)](https://github.com/fairecasoimeme/LiXee-Box/blob/master/doc/screenshots/LiXee-Box_Thermostat_Cadran_chauffage.jpg)

### Lecture

- **Consigne** affichée en gros au centre.
- **Point (rond)** le long de l'arc = **température réelle** → on visualise l'écart à la consigne.
- **Icône** flamme (chauffage) / flocon (rafraîchissement) :
  - **grise** quand l'actionneur ne tourne pas,
  - **colorée** (orange / bleu) quand il est actif.
- **Segment dégradé animé** entre température et consigne : présent **uniquement** quand l'actionneur travaille réellement à **réduire** l'écart (l'animation va du point température vers la consigne). Absent au repos ou si l'action ne rapproche pas de la consigne.
- **Statut** texte : `Chauffe / Rafraîchit (xx %)`, `Régule`, `Capteur HS`, `Fenêtre ouverte`, `Absence`, `Hors plage` / `Tarif …`, `Marche/Arrêt forcé`, `Démarrage…` / `Arrêt…`.
- **Badges** : présence (Présent/Absent), ouverture (Ouvert/Fermé).
- **⚙️** en haut à droite : accès direct au formulaire de configuration.

### Contrôles

| Contrôle | Effet |
|---|---|
| **− / +** | Ajuste la consigne (±0,5 °C) |
| **Chaud / Froid** | (clim réversible uniquement) bascule le mode |
| **Auto / Marche / Arrêt** | **Forçage manuel** : Marche force l'actionneur, Arrêt le coupe, Auto rend la main à la régulation |
| **Hors-gel** | (chauffage) bascule la consigne sur la valeur hors-gel ; un nouvel appui restaure la consigne précédente |

> Le forçage est un override **runtime** (revient en Auto au redémarrage). Le hors-gel de sécurité reste prioritaire même en « Arrêt forcé ».

La page **Mesures** se parcourt aussi par **glissement (swipe)** : Thermostat ↔ Énergie ↔ Appareils.

---

## 10. Priorités et sécurités

À chaque cycle (30 s), pour une zone **active** et capteur **valide**, la décision suit cet ordre de priorité :

1. **Hors-gel de sécurité** (chauffage, température < seuil) → **chauffe forcée**.
2. **Forçage manuel** (Marche / Arrêt depuis la vignette).
3. **Inhibition** : fenêtre ouverte, absence, ou hors plage/tarif → **arrêt**.
4. **Régulation** normale (TPI ou hystérésis).

Sécurités complémentaires :

- **Capteur HS** (pas de mesure depuis le délai configuré) → actionneur coupé.
- **Zone désactivée** → actionneur coupé.
- **Anti-court-cycle** sur tous les basculements.
- Au **redémarrage** de la box, l'état réel des clims (System Mode) est adopté pour ne pas réémettre de commande inutile.

---

## 11. Cas d'usage

- **Radiateur électrique sur prise** : capteur mural + prise on/off, mode Chauffage, TPI. Optionnel : capteur d'ouverture pour couper fenêtre ouverte.
- **Climatiseur réversible** : capteur (ou température interne de la clim) + clim HVAC, actions HEAT/COOL/OFF, choix Chaud/Froid sur la vignette.
- **Pièce occupée seulement** : ajouter un capteur de présence → ne régule qu'en présence.
- **Optimisation tarifaire** : mode Tarif Linky, cocher Heures Creuses (ou tout sauf Tempo Rouge) → chauffe surtout quand l'électricité est moins chère, hors-gel garanti le reste du temps.
- **Plusieurs convecteurs** : un principal + prises supplémentaires, commandés ensemble.

---

## 12. Dépannage (FAQ)

**La zone n'agit pas / reste sur « Régule ».**
Vérifiez : capteur de température **valide** (pas « HS »), fenêtre **fermée**, **présence** détectée (si capteur présence), et que l'on est **dans la plage / période tarifaire** autorisée. Le hors-gel ne s'applique qu'en chauffage.

**Le capteur est marqué « HS ».**
Aucune mesure reçue depuis le délai configuré (60 min par défaut). Vérifiez la pile / la portée du capteur, ou augmentez le délai dans les paramètres avancés.

**Ma clim émet des bips / reçoit des commandes répétées.**
La régulation par hystérésis n'envoie qu'au changement d'état. Assurez-vous que l'appareil est bien piloté par **actions** (HEAT/COOL/OFF) et non en on/off simple. Réglez aussi le **setpoint interne** de la clim à une valeur extrême pour qu'elle obéisse au mode envoyé plutôt qu'à sa propre régulation.

**La clim tourne mais la vignette indique « Régule » (inactif).**
L'affichage suit le **System Mode** réel de la clim. Vérifiez que le **reporting** est bien configuré (bind + Configure Reporting) — au besoin, ré-appairez l'appareil après une mise à jour de son template.

**En mode froid, le texte affichait « Chauffe ».**
Corrigé : le vocabulaire suit le mode (Rafraîchit en froid).
