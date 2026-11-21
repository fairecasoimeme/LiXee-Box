# Conseils et économies : spécification

But : la box calcule seule les conseils, alertes et estimations d'économies.
LiXee-Assist (kiosque, widgets), la page web, MQTT et les notifications
affichent le même résultat.

Source : LiXee-Assist `master` (6fb6232, 17/10/2026), fichiers
`lib/services/energy_advice.dart`, `lib/services/tariff_compare.dart`,
`lib/services/energy_detail_service.dart`, `lib/panel/panel_savings.dart`,
comparés au firmware `lixee-box_v3.3`.

Les points marqués **[À VALIDER]** ont été tranchés le 21/11/2026 : voir §7.

---

## 1. Principe

- Une route `GET /api/energy/advice` renvoie tout l'état calculé (§4).
- La box donne pour chaque élément :
  - un **code** stable (ex. `weather.peak`, `alert.water_continuous`) ;
  - un **niveau** ;
  - les **valeurs** ;
  - un **texte français prêt à afficher**.
- Les clients choisissent la mise en page et les icônes, jamais les règles.
- Calcul à la demande, sur des données que la box garde déjà. Cache de 30 s pour le kiosque qui interroge souvent. Pas de tâche ni de mémoire permanente en plus, sauf l'historique Tempo (§3.11).
- Les règles qui existent déjà sur la box (alertes de `notif_alerts.cpp`) restent là où elles sont. `/api/energy/advice` ne fait que les reprendre.

## 2. Données disponibles sur la box

| Donnée | Où | Profondeur |
|---|---|---|
| Puissance instantanée, souscrite, phases, mode Linky | device_data du ZLinky | instant |
| Moyenne 1 min, tranches de charge, pics | `power_stats.cpp` (`/api/power/summary`) | 90 jours, 24 mois |
| Plancher nocturne 1 h - 5 h | `power_stats.cpp` (`/api/power/floor`) | 90 jours |
| Consommation horaire par index, production | `energy_history` | 24 h |
| Consommation journalière par index, coût | `energy_history` | 31 jours |
| Gaz, eau (horaire, journalier) | `energy_history` | 24 h / 31 j |
| Tarif en cours, prix, prochaine bascule, Tempo du jour et du lendemain | `/api/tariff` (FF66) | instant |
| Seuils (budget, sur-puissance, anomalie %, fuite d'eau) | config Notifications | — |
| Délestage | `delestage` | instant |

## 3. Règles

### 3.1 Météo de l'électricité (`weather`)
Le niveau de prix du moment et le conseil associé. Reprise telle quelle de l'application.

- `peak` : pointe mobile EJP, ou jour rouge en heures pleines. « Évite le four, le sèche-linge, le chauffage d'appoint, jusqu'à <bascule> ».
- `expensive` : jour blanc en heures pleines. « Décale à partir de <bascule> ».
- `flat` : tarif Base. « Ce qui compte c'est le total ».
- `normal` : heures pleines ordinaires. « Garde les gros appareils pour les HC, dès <bascule> ».
- `cheap` : heures creuses. Message particulier en heures creuses d'un jour rouge (« dès 6 h le prix grimpe »).
- `surplus` : la production dépasse la consommation (puissance injectée > puissance soutirée). Remplace tout niveau sauf `peak`.

Titre : « Heures pleines · jour blanc », « Tarif Base », « Pointe mobile », « Heures normales »…

Valeurs : prix du kWh en cours s'il est saisi, couleur de demain en Tempo.

### 3.2 Coût de l'instant (`costPerHour`)
Puissance apparente (VA, assimilée à des W) × prix du kWh en cours, en €/h.
Absent sans prix. C'est un ordre de grandeur, affiché comme tel.

### 3.3 Risque de coupure (`cutRisk`)
- Ratio = charge sur 1 min de la box (`va1m`) ÷ puissance souscrite. À défaut, la puissance instantanée.
- Triphasé : la phase la plus chargée, rapportée à un tiers de l'abonnement.
- Niveaux :
  - `low` : moins de 70 % ;
  - `medium` : de 70 % à 90 %, « évite un autre gros appareil » ;
  - `high` : 90 % et plus, « éteins un appareil maintenant ».
- Indiquer si le ratio est une moyenne sur 1 min ou une valeur instantanée, et quelle phase.

### 3.4 Consommation de fond (`baseLoad`) — DIVERGENCE
- **Box aujourd'hui** : plancher nocturne, la plus petite heure entre 1 h et 5 h, une valeur par jour (`floorW`, 90 jours).
- **Application** : l'heure complète la plus calme des 24 dernières heures, **à n'importe quelle heure**.
  - Raison : la nuit, c'est souvent la recharge de la voiture ou le ballon.
  - Il faut au moins 12 heures complètes.
  - En mode Linky 5 ou 7, les heures avec production sont exclues, et il en faut au moins 4.
- Coût annuel : W × 8 760 h ÷ 1 000 × prix moyen des index saisis.

**Proposition [À VALIDER]** : une seule règle, « l'heure complète la plus calme, sans production ».
- `/api/energy/advice` : sur les 24 dernières heures (définition de l'application).
- `floorW` journalier : sur toute la journée de 0 h à 24 h au lieu de 1 h à 5 h, calculé au changement de jour. L'historique 90 jours suit ainsi la même règle.
- Les valeurs déjà enregistrées restent telles quelles.

### 3.5 Abonnement (`subscription`)
- Paliers : 3, 6, 9, 12, 15, 18, 24, 30, 36 kVA.
- Données : `/api/power/summary`, soit le pic sur 1 min et la fréquence des tranches 80, 90 et 100 %.
- **Abonnement juste** (`tight`) : la tranche 90 % est atteinte chaque jour ou chaque semaine, ou la tranche 100 % chaque jour, semaine ou mois.
- **Conseil de baisse**, seulement si :
  - l'abonnement n'est pas juste ;
  - la tranche 80 % n'est pas atteinte chaque jour, semaine ou mois ;
  - il y a au moins 14 jours observés.

  Il propose le plus petit palier ≥ pic × 1,15, en triphasé sur le pic de la phase la plus chargée × 3.
- Jamais de conseil de hausse automatique. On affiche seulement « abonnement juste » et le risque.

### 3.6 Mois en cours (`month`) et budget
- Depuis le 1er (aujourd'hui inclus) : en € si les coûts journaliers existent, sinon en kWh.
- Projection : total ÷ jours écoulés (fraction du jour en cours comprise) × nombre de jours du mois.
- Objectif : budget mensuel de la config Notifications, et écart projeté.
- Budget horaire (ligne du graphe horaire) : budget ÷ jours du mois ÷ 24, converti en Wh au coût réel des dernières 24 h (abonnement et taxes compris). Sinon, au prix moyen.
- **Alerte box existante `over_budget`** : coût d'hier > budget ÷ 30. On la garde. **[À VALIDER]** : utiliser ÷ jours du mois au lieu de ÷ 30, comme l'application.

### 3.7 Hier comparé à d'habitude — DIVERGENCE
- **Box (`daily_anomaly`)** : jour du créneau 0 > moyenne des 6 jours précédents qui ont des données (au moins 2), + `DailyAnomalyPercent` (30 % par défaut).
- **Application** : **hier** (jour complet) comparé à la moyenne des 7 jours d'avant (au moins 9 jours d'historique).
  - `warning` à partir de + `DailyAnomalyPercent` ;
  - `info` « Bravo » à partir de − 15 %.

**Proposition [À VALIDER]** : la règle de l'application pour les deux (hier, jour complet, comparé à 7 jours), avec le « Bravo » à − 15 %.
- L'alerte `daily_anomaly` part au changement de jour, sur hier.
- Le « Bravo » n'est **pas** une notification, il reste dans les conseils.

### 3.8 Eau — DIVERGENCE
- **Box** :
  - `water_leak` : total journalier du créneau de nuit > seuil (5 L par défaut) ;
  - `night_water_leak` : créneaux horaires 0 h à 6 h (6 créneaux) > seuil (1 L).
- **Application** : 0 h à 5 h (5 créneaux) > seuil. À défaut de seuil, « l'eau coule sans s'arrêter depuis N heures », avec N ≥ 3 heures complètes consécutives jusqu'à la dernière, dont une heure entre 1 h et 5 h.

**Proposition [À VALIDER]** :
- Garder la fenêtre de la box (0 h à 6 h), réglée par l'utilisateur.
- Ajouter à la box l'alerte `water_continuous` (≥ 3 h consécutives, dont une de nuit), avec son interrupteur dans Notifications.

### 3.9 Demain en Tempo
Déjà sur la box : alerte de la couleur du lendemain dans `check_tariff_state`. L'application la duplique.
- Les conseils reprennent le texte : « Ce soir, programme le chauffe-eau et les machines en heures creuses ».
- L'alerte reste celle de la box.

### 3.10 Puissance proche du maximum
Déjà sur la box (`over_power`, `subscribed_power`). L'application n'a qu'un secours à 90 % pour les box anciennes. Rien à porter.

### 3.11 Comparaison des tarifs réglementés (`tariffs`)
- **Options** : Base, Heures creuses, Tempo. Option actuelle déduite d'OPTARIF / NGTF. EJP et offres de marché donnent `null`.
- **Grille** : abonnement annuel par kVA et prix du kWh TTC (Tarif Bleu, CRE). Elle change le 1er février et le 1er août.
  - **Fichier JSON** `/littlefs/tarifs/tarif_bleu.json` livré avec le firmware, remplaçable depuis la page Maintenance, avec `validFrom`.
  - Avertissement affiché si la grille a plus de 7 mois.
- **Puissance** : celle de l'abonnement, ou le palier supérieur le plus proche que l'option propose. Base s'arrête à 15 kVA, Tempo commence à 6 kVA.
- **Période** : jours complets de l'historique journalier, au moins 8, et 31 au maximum sur la box.
- **Part des heures creuses** :
  - contrat HC ou Tempo : index HC/HP de chaque jour (exacte) ;
  - sinon, index des dernières 24 h ;
  - sinon, de 22 h à 6 h supposé sur les dernières 24 h.
- **Coût sur la période** : énergie × prix (Tempo selon la couleur de chaque jour) + abonnement × jours ÷ 365.
- **Sur un an** : consommation moyenne d'un jour × 365. En Tempo, répartie sur 22 rouges, 43 blancs et 300 bleus, ou sur les comptes de la dernière saison connue.
- **Résultat** : coût de chaque option, l'option la moins chère, et l'économie annuelle par rapport à l'option actuelle.

**Couleurs Tempo passées** : l'application les lit sur api-couleur-tempo.fr. **Proposition [À VALIDER]** : la box les enregistre elle-même chaque jour (couleur du jour vue sur FF66).
- Fichier d'un octet par jour pour la saison en cours et la précédente, soit environ 730 octets.
- Pas d'accès Internet nécessaire.
- Tant qu'une saison complète n'est pas connue, l'estimation annuelle utilise 22 / 43 / 300.

**Amélioration possible avec un contrat Base [À VALIDER]** : la box voit la consommation heure par heure en direct. Elle peut enregistrer chaque jour la part 22 h - 6 h, comme une colonne de plus dans l'historique journalier. La part des HC serait alors mesurée sur 31 jours au lieu d'être supposée sur 24 h.

## 4. API `GET /api/energy/advice`

```json
{
  "at": 1795000000,
  "weather":   { "code": "weather.cheap", "level": "cheap", "title": "Heures creuses · jour bleu",
                 "text": "C'est le bon moment : ...", "price": 0.1356, "next": "06:00", "tomorrow": "white" },
  "costPerHour": 0.21,
  "cutRisk":   { "level": "medium", "ratio": 0.74, "averaged": true, "phase": null, "text": "..." },
  "baseLoad":  { "w": 180, "hour": 3, "kwhYear": 1577, "eurYear": 316 },
  "subscription": { "kva": 9, "peakVA": 5200, "days": 63, "tight": false, "suggestedKva": 6,
                    "phasePeakVA": null, "phaseLimitVA": null, "days30Over80": 0, "text": "..." },
  "month":     { "soFar": 41.2, "projected": 63.5, "unit": "EUR", "budget": 70, "hourlyBudgetWh": 520 },
  "alerts":    [ { "code": "alert.tomorrow_red", "level": "warning", "title": "...", "text": "...", "at": 1795000000 } ],
  "tariffs":   { "current": "offPeak", "days": 28, "offPeakShare": 0.41, "offPeakSource": "daily",
                 "redDays": 0, "whiteDays": 2, "gridFrom": "2026-08-01",
                 "period": [ { "option": "base", "kva": 9, "energy": 81.2, "subscription": 18.4 } ],
                 "year":   [ { "option": "tempo", "kva": 9, "energy": 905.0, "subscription": 237.34 } ] }
}
```

- Toute section non calculable (pas de ZLinky, pas de prix, historique trop court) est **absente**. Les clients n'affichent alors pas la carte.
- Lecture seule. Accessible via le tunnel (GET, mêmes contrôles que les autres routes).
- MQTT : publication de `weather.level`, `cutRisk.level` et `baseLoad.w` sur `lixee/advice/...` **[À VALIDER]**.

## 5. Page web de la box
Vue « Économies » dans le tableau de bord. Mêmes cartes que le kiosque :
- météo de l'électricité, risque de coupure ;
- mois et objectif ;
- consommation de fond ;
- abonnement ;
- comparaison des tarifs ;
- alertes récentes.

## 6. LiXee-Assist
- Si `/api/energy/advice` répond, l'application l'affiche sans rien recalculer.
- Sinon (box v2.26 ou v3.2), elle garde son code actuel, comme aujourd'hui pour les routes v2.26.
- Les tests de l'application (`test/energy_advice_test.dart`, `test/tariff_compare_test.dart`) servent de jeux d'essai pour le portage C++. Mêmes entrées, mêmes résultats, sauf pour les règles redéfinies aux §3.4, 3.7 et 3.8.

## 7. Décisions (validées le 21/11/2026)
1. Consommation de fond : l'heure complète la plus calme, à n'importe quelle heure, sans production en mode 5/7.
   - Dans les conseils : sur les 24 dernières heures.
   - Dans `floorW` (`/api/power/floor`) : sur toute la journée, calculée au changement de jour (`power_stats.cpp`, `close_floor`).
2. Anomalie : comparée aux 7 jours précédents. L'alerte `daily_anomaly` porte sur le jour à 23 h 50, les conseils sur hier. Le « Bravo » à − 15 % reste dans les conseils.
3. Eau :
   - les fenêtres 0 h - 6 h sont gardées ;
   - nouvelle alerte `water_continuous` (case « Fuite d'eau (continue) » dans Config Notifications, clé `WaterContinuous`) : ≥ 3 h consécutives dont une entre 1 h et 5 h, une seule alerte jusqu'à une heure sans eau.
4. Budget : ÷ nombre de jours du mois (alerte `over_budget` et budget horaire).
5. Grille tarifaire :
   - `data/tarifs/tarif_bleu.json` → `/littlefs/tarifs/tarif_bleu.json` (dossier à part de `tp/`) ;
   - accepté par la restauration `.tar` et le gestionnaire de fichiers ;
   - le firmware embarque la même grille et prend le fichier seulement si son `validFrom` est au moins aussi récent.
6. Couleurs Tempo :
   - enregistrées par la box dans `hst/tempo.json`, à partir de 6 h (le jour Tempo va de 6 h à 6 h), 400 jours au plus ;
   - avec un contrat Tempo, la couleur d'un jour absent est déduite de ses index ;
   - une couleur inconnue (contrat non Tempo) est comptée au prix moyen de saison (22 / 43 / 300).
7. Part des heures creuses mesurée jour par jour sur l'archive horaire des 31 derniers jours :
   - par les index avec un contrat HC ou Tempo ;
   - de 22 h à 6 h sinon.
8. MQTT : `<en-tête>advice/weather`, `advice/cut_risk`, `advice/base_load_w`, retenus, au changement et au moins une fois par heure.

### Écarts assumés avec LiXee-Assist
- **Option la moins chère :** jugée sur l'estimation annuelle, pas sur la période (l'été, Tempo paraîtrait toujours gagnant).
- **Heures sans mesure :** ignorées pour la consommation de fond, au lieu d'annuler le calcul.
- **Mois en cours :** l'abonnement et la CTA sont comptés au prorata jusqu'ici et une seule fois dans la projection.
- **Alertes de la box :** une alerte calculée qui double une alerte de la box des 6 dernières heures est retirée (puissance, eau, couleur du lendemain, anomalie).
