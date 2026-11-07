# Chauffe-eau virtuel — Guide complet

Le **chauffe-eau virtuel** transforme la LiXee-Box en régulateur de ballon d'eau chaude. Comme le [thermostat virtuel](THERMOSTATS.md), il **découple la mesure de l'action** : une sonde de température Zigbee (optionnelle) mesure le ballon, et la box pilote un **contacteur/relais** séparé pour l'alimenter, en respectant un mode (arrêt, marche forcée, programme) et en se maîtrisant par les heures creuses ou le tarif Linky.

Côté Matter, chaque chauffe-eau est exposé comme le device type standard **Water Heater** (`0x050F`, spécification Matter 1.5), reconnu nativement par **Home Assistant** et **SmartThings** (entités dédiées). **Non supporté par Apple Maison** (voir §11).

On peut définir **jusqu'à 4 chauffe-eau** indépendants.

---

## Table des matières

1. [Principe et vue d'ensemble](#1-principe-et-vue-densemble)
2. [Créer et gérer un chauffe-eau](#2-créer-et-gérer-un-chauffe-eau)
3. [Mode : Arrêt, Marche forcée, Programme](#3-mode--arrêt-marche-forcée-programme)
4. [Programme : plages horaires et tarif Linky](#4-programme--plages-horaires-et-tarif-linky)
5. [Sonde de température (optionnelle)](#5-sonde-de-température-optionnelle)
6. [Contacteur / relais (principal et supplémentaires)](#6-contacteur--relais-principal-et-supplémentaires)
7. [Régulation TPI et types d'équipement](#7-régulation-tpi-et-types-déquipement)
8. [Le Boost (marche forcée temporaire)](#8-le-boost-marche-forcée-temporaire)
9. [Paramètres avancés](#9-paramètres-avancés)
10. [La vignette (Mesures → Chauffe-eau)](#10-la-vignette-mesures--chauffe-eau)
11. [Exposition Matter (Water Heater 0x050F)](#11-exposition-matter-water-heater-0x050f)
12. [Priorités et sécurités](#12-priorités-et-sécurités)
13. [Cas d'usage](#13-cas-dusage)
14. [Dépannage (FAQ)](#14-dépannage-faq)

---

## 1. Principe et vue d'ensemble

Un chauffe-eau électrique classique n'a pas de pilotage fin : il chauffe dès qu'il est alimenté. Le chauffe-eau virtuel ajoute une couche de régulation et de programmation par-dessus un simple contacteur :

- **Sonde** (optionnelle) = un capteur de température Zigbee posé sur le ballon (cluster `0x0402`) ou la température interne d'un appareil HVAC (`0x0201`).
- **Contacteur** = un appareil Zigbee on/off (`0x0006`) qui alimente la résistance.
- **Régulateur** = la LiXee-Box, qui compare (si une sonde existe) la température à la consigne, ou pilote « en ouvert » (sans sonde) selon le mode, toutes les 30 secondes.

Sans sonde, le chauffe-eau fonctionne en **pilotage simple** : le contacteur suit directement le mode / la plage horaire, sans régulation fine (pas de modulation TPI).

Deux pages :

| Page | Menu | Rôle |
|---|---|---|
| **Configuration** | Config → Chauffe-eau | Créer / modifier / supprimer les chauffe-eau |
| **Visuel** | Mesures → Chauffe-eau | Cartes temps réel + contrôles (consigne, mode, boost) |

---

## 2. Créer et gérer un chauffe-eau

Dans **Config → Chauffe-eau**, chaque chauffe-eau est représenté par une **fiche** récapitulant Mode, Consigne, Capteur, Contacteur (+N si contacteurs supplémentaires) :

- **✏️** ouvre le formulaire de configuration.
- **🗑️** demande une confirmation avant suppression.
- Bouton **« + Ajouter un chauffe-eau »** (jusqu'à 4).

Le formulaire regroupe trois blocs : **Régulation** (mode, consigne, programme), **Capteur et contacteur**, et des **Paramètres avancés de régulation (TPI)** repliés.

---

## 3. Mode : Arrêt, Marche forcée, Programme

Contrairement au thermostat virtuel (réglages libres), le chauffe-eau a **3 modes fixes**, qui correspondent 1:1 aux valeurs `CurrentMode` du cluster Matter Water Heater Mode :

| Mode | Effet |
|---|---|
| **Arrêt** | Contacteur coupé en permanence |
| **Marche forcée** | Chauffe en continu vers la consigne (ou contacteur fermé en permanence sans sonde) |
| **Programme** | Ne chauffe que dans les plages horaires et/ou périodes tarifaires configurées (voir §4) |

### Consigne (°C)

Température cible du ballon, **0 à 70 °C** (55 °C par défaut — valeur usuelle anti-légionellose). Réglable au formulaire et directement sur la vignette avec les boutons **−/+** (pas de 1 °C).

> Contrairement au thermostat virtuel, il n'y a **pas de hors-gel** : un ballon d'eau chaude n'a pas cette notion. La sécurité correspondante est l'**arrêt** du mode Arrêt et la coupure si le capteur est HS (voir §12).

---

## 4. Programme : plages horaires et tarif Linky

En mode **Programme**, la régulation n'est autorisée **que** pendant les créneaux configurés :

- **Plages horaires** : une ou plusieurs plages `HH:MM-HH:MM`, avec des jours cochables individuellement (gère le passage de minuit, ex. `22:00-06:00` pour des heures creuses nocturnes). Laissez-les vides pour ne pas restreindre par horaire.
- **Périodes tarifaires Linky** (si un **ZLinky** est configuré) : cochez les périodes pendant lesquelles le chauffe-eau **doit** chauffer (Heures Creuses, Tempo Bleu HC…). Les périodes proposées dépendent de l'abonnement détecté (Base / HC-HP / EJP / Tempo).

Les deux peuvent se cumuler : le chauffe-eau chauffe si l'une **ou** l'autre condition est remplie. Si seul un filtre tarifaire est configuré (sans plage horaire) et qu'on est hors période tarifaire autorisée, la régulation est **suspendue**, sans repli silencieux sur l'horaire.

En dehors des créneaux autorisés : **arrêt** (sauf Boost en cours, voir §8).

---

## 5. Sonde de température (optionnelle)

Beaucoup de ballons d'eau chaude n'ont pas de sonde Zigbee dédiée ; la sonde est donc **facultative**. Sont acceptés :

- les capteurs exposant le cluster **0x0402** (sonde de température standard) ;
- la **température locale** d'un appareil HVAC (cluster **0x0201**, attribut 0).

**Avec sonde** : régulation TPI fine (voir §7), affichage de la température réelle, Boost « one-shot » possible (arrêt anticipé dès la consigne atteinte).

**Sans sonde** : pilotage « en ouvert" — le contacteur suit simplement le mode / la plage horaire, sans savoir si l'eau est déjà chaude.

Si aucune mesure n'est reçue depuis un certain délai (**180 min** par défaut, configurable), le capteur est marqué **HS** et le contacteur est coupé par sécurité.

---

## 6. Contacteur / relais (principal et supplémentaires)

### Contacteur principal

L'appareil Zigbee **on/off** (cluster `0x0006`) qui alimente la résistance du ballon. C'est un simple relais/contacteur — pas de pilotage par action comme pour une clim.

### Contacteurs supplémentaires (optionnel)

Un chauffe-eau peut piloter **plusieurs contacteurs en parallèle** (jusqu'à 4 en plus du principal) : ils reçoivent **exactement la même commande** au même instant. Utile pour un ballon alimenté par plusieurs résistances/phases pilotées séparément.

> **Pour que la box confirme l'état réel du contacteur** (et pas seulement la dernière commande envoyée), l'appareil doit avoir un **bind + reporting** configuré sur le cluster `0x0006` (bouton **Configurer** sur sa fiche, Réseau → Zigbee). Sans ça, l'état affiché (`actualOn`) retombe sur la dernière commande émise, sans confirmation matérielle.

---

## 7. Régulation TPI et types d'équipement

Avec une sonde, la régulation utilise l'algorithme **TPI** (Time Proportional & Integral, le même que le thermostat virtuel pour les actionneurs tout-ou-rien) :

```
erreur    = consigne − température
intégrale = clamp(intégrale + erreur)        (anti-windup)
duty      = clamp(Kp × erreur + Ki × intégrale, 0..1)
```

Sur un cycle (15 min par défaut), le contacteur est fermé une fraction du temps proportionnelle à l'effort calculé — cela évite les cycles marche/arrêt brutaux d'un simple thermostat tout-ou-rien.

### Type d'équipement (pré-réglages)

Le formulaire propose un sélecteur **« Type d'équipement »** qui pré-remplit les paramètres avancés (modifiables ensuite) :

| Type | Cycle | Kp | Ki | ON min | OFF min |
|---|---|---|---|---|---|
| **Résistance** (chauffage direct) | 900 s | 0,8 | 0,1 | 180 s | 180 s |
| **Chauffe-eau thermodynamique** (PAC) | 1800 s | 1,0 | 0,0 | 600 s | 600 s |

Une pompe à chaleur fonctionne en tout-ou-rien (compresseur) et a besoin d'un cycle plus long et de temps ON/OFF minimaux plus grands pour limiter les courts cycles qui l'useraient prématurément.

### Anti-court-cycle

Les durées minimales de marche (`minOn`) et d'arrêt (`minOff`) empêchent les basculements trop rapprochés, qui useraient le contacteur (et le compresseur d'une PAC).

---

## 8. Le Boost (marche forcée temporaire)

Le **Boost** force la chauffe pendant une **durée donnée**, puis revient **automatiquement** au mode précédent (Arrêt/Marche forcée/Programme) à l'échéance — utile pour un besoin ponctuel d'eau chaude en dehors des heures creuses.

- Boutons rapides sur la vignette : **Boost 30 min** et **Boost 2h** (arrêt anticipé dès la consigne atteinte si une sonde est présente — comportement « one-shot »).
- **Annuler le boost** : revient immédiatement au mode normal.
- Côté Matter (cluster Water Heater Management `0x0094`, commande `Boost`), un contrôleur peut aussi fixer une **consigne propre au boost** (`temporarySetpoint`), distincte de la consigne normale.
- Le paramètre `emergency` du cluster Matter est accepté mais sans effet matériel : la box ne pilote qu'un seul relais, il n'y a pas de distinction possible entre un boost normal et un boost « urgence ».

---

## 9. Paramètres avancés

Repliés dans **« Paramètres avancés de régulation (TPI) »**, sans effet si aucune sonde n'est configurée :

| Paramètre | Défaut | Rôle |
|---|---|---|
| **Cycle TPI** | 900 s (15 min) | Durée d'un cycle de modulation |
| **Kp** | 0,8 | Gain proportionnel (réactivité à l'écart) |
| **Ki** | 0,1 | Gain intégral (élimine l'erreur résiduelle, borné anti-windup) |
| **Temps ON minimal** | 180 s | Anti-court-cycle |
| **Temps OFF minimal** | 180 s | Anti-court-cycle |
| **Délai capteur HS** | 180 min | Sans mesure au-delà → capteur « HS », contacteur coupé |

---

## 10. La vignette (Mesures → Chauffe-eau)

Dans **Mesures → Chauffe-eau**, chaque chauffe-eau est une **carte** mise à jour en temps réel (consigne, température actuelle, icône goutte d'eau orange quand actif).

### Lecture

- **Consigne** affichée en gros au centre, **température actuelle** en dessous.
- **Icône goutte** : grise au repos, orange quand le contacteur chauffe réellement.
- **Statut** : `Boost en cours (xx min restantes)`, `Arrêt`, `Capteur HS`, `Hors plage`, `Démarrage…` / `Arrêt…` (transition, anti-court-cycle), `Chauffe (xx %)`, `Régule`.
- Les trois boutons **Arrêt / Marche / Programme** permettent de changer de mode d'un clic.

### Contrôles

| Contrôle | Effet |
|---|---|
| **− / +** | Ajuste la consigne (±1 °C) |
| **Arrêt / Marche / Programme** | Change le mode (persisté, pas un forçage temporaire) |
| **Boost 30 min / Boost 2h** | Marche forcée temporaire (voir §8) |
| **Annuler le boost** | Revient au mode normal (affiché à la place des boutons Boost pendant un boost) |

---

## 11. Exposition Matter (Water Heater 0x050F)

Chaque chauffe-eau est publié comme un endpoint **Water Heater** (device type `0x050F`), composé de :

| Cluster | ID | Rôle |
|---|---|---|
| **Thermostat** | `0x0201` | Température mesurée, consigne (`OccupiedHeatingSetpoint`, 0-70 °C), feature **chauffage seul** (pas de refroidissement) |
| **Water Heater Management** | `0x0094` | Commandes `Boost` / `CancelBoost`, attributs `HeaterTypes` (fixe : résistance immergée), `HeatDemand` (reflète l'état réel du contacteur), `BoostState` |
| **Water Heater Mode** | `0x009E` | 3 modes fixes (`SupportedModes`), commande `ChangeToMode` |

- **Support par contrôleur** : **Home Assistant** et **SmartThings** reconnaissent nativement le device type Water Heater (entités dédiées : mode, boost…). **Non supporté par Apple Maison**.
- **Écriture directe de `SystemMode`** (cluster Thermostat, via un contrôleur générique comme Apple Maison) : `Off` (0) ↔ Arrêt, `Heat` (4) ↔ Marche forcée. Le mode Programme reste accessible uniquement via la commande `ChangeToMode` (Water Heater Mode) — les contrôleurs qui l'exposent nativement (Home Assistant, SmartThings) l'affichent comme un sélecteur à 3 états.
- Les attributs `HeatDemand`/`BoostState`/`CurrentMode` sont **republiés en temps réel** (le contrôleur est notifié des changements, pas besoin de rafraîchir).
- **Non supporté** : les features optionnelles `EnergyManagement` (niveau de cuve, énergie estimée) et `TankPercent` — aucun ballon connecté de ce type dans l'écosystème Zigbee actuel.

> Sans actionneur réel configuré (contacteur non choisi dans le formulaire), l'endpoint Matter est tout de même créé — pratique pour tester un contrôleur avant d'appairer le matériel, mais `HeatDemand` reste alors toujours inactif.

---

## 12. Priorités et sécurités

À chaque cycle (30 s), pour un chauffe-eau **configuré**, la décision suit cet ordre de priorité :

1. **Boost en cours** → chauffe (continue, ou jusqu'à consigne atteinte en one-shot).
2. **Mode Arrêt** → coupure.
3. **Mode Programme, hors plage/tarif** → coupure.
4. **Régulation normale** (Marche forcée, ou Programme en plage) : TPI si sonde valide, pilotage en ouvert sinon.

Sécurités complémentaires :

- **Capteur HS** (pas de mesure depuis le délai configuré) → contacteur coupé.
- **Anti-court-cycle** sur tous les basculements (`minOn`/`minOff`).
- L'état réel lu sur le contacteur (`actualOn`) peut diverger de la commande (`output`) si l'appareil a été actionné en dehors de la box (bouton physique, autre automatisation) : la régulation **corrige** au cycle suivant, sans attendre une action de l'utilisateur.

---

## 13. Cas d'usage

- **Ballon simple, sans sonde** : juste un contacteur, mode Programme sur les heures creuses du contrat. Le ballon chauffe uniquement la nuit, sans régulation fine.
- **Ballon avec sonde, anti-légionellose** : consigne 55-60 °C, mode Programme en heures creuses + Boost ponctuel si besoin d'eau chaude en journée.
- **Chauffe-eau thermodynamique (PAC)** : preset « Chauffe-eau thermodynamique » pour un cycle et des temps ON/OFF adaptés au compresseur.
- **Tarif Tempo** : cocher Bleu HC / Blanc HC (et pas les jours Rouge), pour chauffer surtout quand l'électricité est la moins chère.
- **Besoin ponctuel** : bouton Boost 2h depuis la vignette ou via un contrôleur Matter, avant de recevoir des invités par exemple.

---

## 14. Dépannage (FAQ)

**Le chauffe-eau reste sur « Régule » sans jamais chauffer.**
Vérifiez le mode (pas sur Arrêt), et si Programme : qu'on est bien **dans la plage horaire / période tarifaire** configurée.

**Le capteur est marqué « HS ».**
Aucune mesure reçue depuis le délai configuré (180 min par défaut). Vérifiez la pile / la portée du capteur, ou augmentez le délai dans les paramètres avancés. Sans sonde du tout, ce statut n'apparaît pas : le pilotage passe en mode « ouvert ».

**L'état du contacteur (`actualOn`) ne se met jamais à jour tout seul.**
Le contacteur a besoin d'un **bind + reporting** Zigbee configuré sur le cluster `0x0006` (bouton **Configurer** sur sa fiche, Réseau → Zigbee). Sans ça, la box ne peut connaître l'état réel qu'en le relisant manuellement, et `actualOn` recopie simplement la dernière commande envoyée.

**J'ai actionné le contacteur manuellement (bouton physique) pendant un mode Programme hors plage.**
C'est normal que la box détecte la divergence puis **remette le contacteur dans l'état attendu** au cycle de régulation suivant (sous 30 s, en respectant l'anti-court-cycle `minOn`/`minOff`).

**Le Boost ne s'arrête pas avant la durée programmée alors que l'eau est chaude.**
L'arrêt anticipé (« one-shot ») nécessite une **sonde de température valide**. Sans sonde, le Boost va jusqu'au bout de sa durée.
