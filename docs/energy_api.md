# API énergie (lot 2 du portage LiXee-Box)

Contrat entre le firmware (`components/energy_service`, `components/webserver`)
et la page Énergie de l'application web (`data/web/js/app.js`). Portage des
fonctions `statusEnergy` V2 de la LiXee-Box, en JSON plutôt qu'en fragments HTML.

Conventions :
- Énergies en **Wh**, puissances en **VA**, prix en **€**, toujours positifs.
  La production est une série à part (`type:"prod"`), jamais un nombre négatif.
- `time` / `period` ∈ `hour` | `day` | `month` | `year` (défaut `hour`).
- Index tarifaires du ZLinky : attributs `256,258,…,274` (0x0100..0x0112),
  prix : `tarifIdx2`→256, `tarifIdx3`→258 … `tarifIdx10`→272, 274 sans prix.
- Coût d'un créneau : énergie = Σ kWh × prix(index) ; taxes = Σ kWh × CSPE + CTA × f ;
  abonnement = tarifAbo × f ; f = 1/720 (heure), 1/30 (jour), 1 (mois), 12 (an).
  Revenu production = kWh produits × tarifIdxProd.
- Si aucun ZLinky n'est configuré : `{"configured":false}` (HTTP 200).

## GET /api/energy/summary?time=

```json
{
  "configured": true,
  "period": "hour",
  "linkyMode": 1, "modeLabel": "Standard monophasé", "tri": false, "producer": false,
  "connected": true,
  "alert": null,                     // ou "Dépassement de puissance souscrite"
  "label": { "value": 132, "class": "C" },   // null si surface non renseignée
  "current":  { "wh": 1234, "cost": { "energy": 0.21, "taxes": 0.03, "abo": 0.02, "total": 0.26 },
                "prodWh": 0, "prodEur": 0 },
  "previous": { ...même forme... },  // créneau précédent (heure/jour/mois/année)
  "variationPct": 12.5,              // null si previous.wh == 0
  "minWh": 120, "maxWh": 2400,       // sur les créneaux de la période
  "rows": [                          // vue "hour" uniquement : J-1, Mois, Année
    { "label": "J-1",   "wh": 8200,  "cost": {...}, "prodWh": 0, "prodEur": 0 },
    { "label": "Mois",  "wh": 150000, "cost": {...}, "prodWh": 0, "prodEur": 0, "projectionEur": 41.2 },
    { "label": "Année", "wh": 900000, "cost": {...}, "prodWh": 0, "prodEur": 0 }
  ],
  "budget": { "monthEur": 50, "periodEur": 1.67, "pct": 15.6 },   // null si pas de budget
  "breakdown": [                     // répartition du créneau courant (tableau + donut)
    { "key": "256", "name": "HC", "wh": 800, "eur": 0.14, "color": "#2980b9", "type": "index" },
    { "key": "sub_0", "name": "Salon", "wh": 120, "eur": 0.02, "color": "#e74c3c", "type": "submeter" },
    { "key": "gaz", "name": "Gaz", "wh": 0, "eur": 0, "color": "#e67e22", "type": "gaz" },
    { "key": "prod", "name": "Production", "wh": 0, "eur": 0, "color": "#27ae60", "type": "prod" }
  ],
  "totalWh": 1234, "totalEur": 0.26,
  "lastUpdate": "21/10/2026 14:05"
}
```

## GET /api/energy/gauge?time=

Vue `hour` (puissances instantanées) :
```json
{ "period": "hour", "unit": "VA", "tri": false, "goal": 9000,
  "phases": [ { "label": "Soutirée", "value": 1250, "max": 9000 } ],   // 3 entrées en triphasé
  "prod": { "label": "Injectée", "value": 0, "max": 9000 },           // null sans production
  "powerW": 1180 }                                                     // estimation W, null si inconnue
```
Autres périodes (énergie du créneau courant) :
```json
{ "period": "day", "unit": "kWh", "conso": { "value": 8.2, "max": 14.1 },
  "prod": { "value": 0, "max": 0 } }
```

## GET /api/energy/distribution?time=&type=euro

Tableau Morris : `[{ "label": "HC", "value": 800, "color": "#2980b9", "unit": "Wh" }, ...]`
(index moins la part des sous-compteurs, sous-compteurs, gaz si unité Wh, « Injection »).
`type=euro` : `value` en € (énergie seule), `unit:"€"`. `[]` si aucune donnée.

## GET /api/energy/chart?time=&ieee=   (alias /loadEnergyChart)

`ieee` absent = ZLinky. Pour le compteur de gaz/eau, une seule série `key:"0"`.
```json
{ "period": "day", "unit": "Wh", "device": "zlinky",
  "labels": ["22/09", "...", "21/10"],
  "series": [
    { "key": "256", "name": "HC", "type": "index", "color": "#2980b9", "price": 0.17, "data": [ ... ] },
    { "key": "sub_0", "name": "Salon", "type": "submeter", "color": "#e74c3c", "price": 0.17, "data": [ ... ] },
    { "key": "prod", "name": "Production", "type": "prod", "color": "#27ae60", "price": 0.13, "data": [ ... ] }
  ],
  "coeff": 1, "displayUnit": "kWh",
  "costs": { "abo": 12.5, "cspe": 0.0225, "cta": 2.1, "prodPrice": 0.13, "periodFactor": 0.0333 },
  "budgetWh": 7500 }
```
Libellés : hour `"15h"` ×24 (de H+1 à H), day `"DD/MM"` ×30 (jusqu'à aujourd'hui),
month `"MM/YYYY"` ×12, year `"YYYY"` (années disponibles). Séries vides omises.
Index : valeurs déjà diminuées de la part des sous-compteurs.

## GET /api/energy/chart/export?time=&ieee=   → CSV
`;` séparateur, CRLF, virgule décimale, BOM UTF-8.
`Periode;<séries> (Wh);Consommation totale (Wh);Conso - Energie (EUR);Conso - Abonnement (EUR);Conso - Taxes (EUR);Conso - Cout total (EUR)[;Production totale (Wh);Production - Revenu (EUR);Net (Wh);Facture nette (EUR)]`

## GET /api/energy/power   (puissance des dernières 24 h, minute par minute)
```json
{ "tri": false, "goal": 9000,
  "labels": ["14:06", "...", "14:05"],
  "consumption": [[...ph1], [...ph2], [...ph3]],   // 1 tableau en mono
  "injection":   [[...ph1]],                      // vide sans production
  "stats": { "min": 0, "max": 6100, "last": 900, "trend": -50 } }
```
Valeurs `null` pour les minutes sans mesure.

## GET /api/energy/power/export   → CSV (`Heure;Puissance (VA)[;Ph2;Ph3];Injection (VA);…`)

## GET /api/energy/tariff
```json
{ "show": true, "tariff": "HP Blanc", "bgColor": "#000000", "isTempo": true,
  "today": { "class": "tempo-blanc", "label": "Jour Blanc" },
  "tomorrow": { "class": "tempo-bleu", "label": "Jour Bleu" },
  "index": 262, "price": 0.1894 }
```

## GET /api/energy/linky   (données TIC pour la fenêtre « Données Linky »)
`{ "mode": 1, "items": [ { "key": "0B04_1295", "name": "PAPP / SINSTS", "value": 1250, "unit": "VA" }, ... ] }`
États visibles du template du ZLinky, filtrés par mode TIC.

## Sous-compteurs
- `GET  /api/energy/submeters` → `{ "subMeters": [ { "index":0, "IEEE":"…", "alias":"Salon", "color":"#e74c3c", "enabled":true } ], "max": 10 }`
- `GET  /api/energy/submeters/eligible` → `[ { "IEEE":"…", "alias":"…", "model":"…" } ]` (appareils avec 0x0702/0, hors ZLinky)
- `POST /api/energy/submeters` `{IEEE, alias, color, enabled}` → ajout ou mise à jour (par IEEE)
- `POST /api/energy/submeters/delete` `{IEEE}`
