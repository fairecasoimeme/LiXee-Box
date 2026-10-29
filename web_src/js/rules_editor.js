// ============================================================================
// LiXee MatterBridge - editeur de regles (#addRule, #editRule?name=...)
// Portage de data/web/js/rules.js de la LiXee-Box, charge a la demande par
// page_rules.js (LixeeApp.loadScripts). Expose window.RulesEditor.open(mode, nom).
//
// Donnees : /api/rules/devices (appareils + etats/actions de template + valeurs
// courantes : remplace /getDevices + /getTemplates), /api/actiongroups/list,
// /api/rules/get?name=. Enregistrement : POST /api/rules/add | /api/rules/edit,
// meme corps JSON que la LiXee-Box (rules.json reste compatible).
//
// Differences avec rules.js :
//  - evenements delegues (plus de fonctions globales ni de setTimeout en cascade :
//    toutes les donnees sont chargees avant l'affichage) ;
//  - textes venant du serveur echappes (noms d'appareils, d'actions...) ;
//  - type de condition "energy" (compteur Linky : puissance, tarif, Tempo, couts),
//    propre au MatterBridge ;
//  - action "Appareil" sur une commande de position de volet (251/252) : champ
//    Position transmis dans "value" (la LiXee-Box envoyait toujours une valeur
//    vide, l'action ne partait donc jamais) ;
//  - ligne de template propre a un endpoint : cle de rangement du pont
//    ((endpoint << 12) | attribut), convertie depuis "attribut + endpoint x 65536"
//    de la LiXee-Box au chargement des regles.
// ============================================================================
(function() {
  'use strict';
  var A = window.LixeeApp;
  if (!A) return;
  var esc = A.escHtml;

  var S = { mode: 'add', oldName: '', devices: {}, groups: [], seq: 0, timer: null };

  // --------------------------------------------------------------------------
  // Tables
  // --------------------------------------------------------------------------

  // Issue #31 : sous-champs du registre STGE (ZLinky, cluster FF66 = 65382,
  // attribut 535). La comparaison porte sur la VALEUR NUMERIQUE du champ ; les
  // libelles ne servent qu'a l'affichage.
  var STGE_CLUSTER = 65382, STGE_ATTRIBUTE = 535;
  var STGE_FIELDS = {
    'contact_sec':          { label: 'Contact sec',              opts: [[0, 'Fermé'], [1, 'Ouvert']] },
    'organe_coupure':       { label: 'Organe de coupure',        opts: [[0, 'Fermé'], [1, 'Surpuissance'], [2, 'Surtension'], [3, 'Délestage'], [4, 'Ordre CPL/Euridis'], [5, 'Surchauffe (surcourant)'], [6, 'Surchauffe']] },
    'cache_borne_dist':     { label: 'Cache-borne distributeur', opts: [[0, 'Fermé'], [1, 'Ouvert']] },
    'surtension_phase':     { label: 'Surtension sur une phase', opts: [[0, 'Non'], [1, 'Oui']] },
    'depassement_ref_pow':  { label: 'Dépassement puiss. réf.', opts: [[0, 'Non'], [1, 'Oui']] },
    'producteur':           { label: 'Producteur / conso.',      opts: [[0, 'Consommateur'], [1, 'Producteur']] },
    'sens_energie_active':  { label: 'Sens énergie active', opts: [[0, 'Positive'], [1, 'Négative']] },
    'tarif_four':           { label: 'Tarif fourniture (index)', opts: [] },
    'tarif_dist':           { label: 'Tarif distributeur (index)', opts: [] },
    'horloge':              { label: 'Horloge',                  opts: [[0, 'Correcte'], [1, 'Dégradée']] },
    'type_tic':             { label: 'Type TIC',                 opts: [[0, 'Historique'], [1, 'Standard']] },
    'comm_euridis':         { label: 'Comm. Euridis',            opts: [[0, 'Désactivée'], [1, 'Activée sans sécurité'], [3, 'Activée avec sécurité']] },
    'etat_cpl':             { label: 'État CPL',            opts: [[0, 'Nouveau / déverrouillé'], [1, 'Nouveau / verrouillé'], [2, 'Enregistré']] },
    'sync_cpl':             { label: 'Synchro CPL',              opts: [[0, 'Non synchronisé'], [1, 'Synchronisé']] },
    'tempo_jour':           { label: 'Couleur Tempo (jour)',     opts: [[0, 'Inconnu'], [1, 'Bleu'], [2, 'Blanc'], [3, 'Rouge']] },
    'tempo_demain':         { label: 'Couleur Tempo (demain)',   opts: [[0, 'Inconnu'], [1, 'Bleu'], [2, 'Blanc'], [3, 'Rouge']] },
    'preavis_pointe_mobile':{ label: 'Préavis pointe mobile', opts: [[0, 'Aucun'], [1, 'PM1'], [2, 'PM2'], [3, 'PM3']] },
    'pointe_mobile':        { label: 'Pointe mobile',            opts: [[0, 'Aucun'], [1, 'PM1'], [2, 'PM2'], [3, 'PM3']] }
  };
  (function() {
    for (var i = 0; i < 16; i++) STGE_FIELDS.tarif_four.opts.push([i, 'Index ' + (i + 1)]);
    for (var j = 0; j < 4; j++) STGE_FIELDS.tarif_dist.opts.push([j, 'Index ' + (j + 1)]);
  })();

  var CLUSTER_NAMES = {
    '0000': 'Basic', '0001': 'Power', '0003': 'Identify', '0004': 'Groups',
    '0005': 'Scenes', '0006': 'On/Off', '0008': 'Level Control', '000A': 'Time',
    '0019': 'OTA Upgrade', '0020': 'Poll Control', '0100': 'Shade Config',
    '0101': 'Door Lock', '0102': 'Window Covering', '0201': 'Thermostat',
    '0202': 'Fan Control', '0300': 'Color Control', '0400': 'Illuminance',
    '0402': 'Temperature', '0403': 'Pressure', '0405': 'Humidity',
    '0406': 'Occupancy', '0500': 'IAS Zone', '0502': 'IAS WD',
    '0702': 'Simple Metering', '0B04': 'Electrical Measurement',
    '0B05': 'Diagnostics', '1000': 'Touchlink', 'EF00': 'Tuya',
    'FC11': 'Sonoff Custom', 'FF66': 'LiXee'
  };

  var TEMPO_OPTS = [[0, 'Inconnu'], [1, 'Bleu'], [2, 'Blanc'], [3, 'Rouge']];
  // Grandeurs de la condition "energy" (cles lues par components/rules/rules.cpp)
  var ENERGY_METRICS = [
    { k: 'power_va',       l: 'Puissance soutirée',        u: 'VA' },
    { k: 'inj_va',         l: 'Puissance injectée',        u: 'VA' },
    { k: 'phase1_va',      l: 'Puissance phase 1',              u: 'VA' },
    { k: 'phase2_va',      l: 'Puissance phase 2',              u: 'VA' },
    { k: 'phase3_va',      l: 'Puissance phase 3',              u: 'VA' },
    { k: 'voltage1',       l: 'Tension phase 1',                u: 'V' },
    { k: 'voltage2',       l: 'Tension phase 2',                u: 'V' },
    { k: 'voltage3',       l: 'Tension phase 3',                u: 'V' },
    { k: 'overload',       l: 'Dépassement de puissance',  u: '', opts: [[0, 'Non'], [1, 'Oui']] },
    { k: 'tariff',         l: 'Tarif en cours (texte)',         u: '', text: true },
    { k: 'tariff_price',   l: 'Prix du kWh en cours',           u: '€/kWh' },
    { k: 'tempo_today',    l: 'Couleur Tempo du jour',          u: '', opts: TEMPO_OPTS },
    { k: 'tempo_tomorrow', l: 'Couleur Tempo de demain',        u: '', opts: TEMPO_OPTS },
    { k: 'wh_today',       l: 'Consommation du jour',           u: 'Wh' },
    { k: 'eur_today',      l: 'Coût du jour',              u: '€' },
    { k: 'prod_today',     l: 'Production du jour',             u: 'Wh' },
    { k: 'wh_yesterday',   l: 'Consommation de la veille',      u: 'Wh' },
    { k: 'eur_yesterday',  l: 'Coût de la veille',         u: '€' },
    { k: 'wh_month',       l: 'Consommation du mois',           u: 'Wh' },
    { k: 'eur_month',      l: 'Coût du mois',              u: '€' },
    { k: 'wh_year',        l: 'Consommation de l\'année',  u: 'Wh' },
    { k: 'eur_year',       l: 'Coût de l\'année',     u: '€' }
  ];

  var COND_TYPES = [
    ['device', 'Appareil Zigbee'], ['device_compare', 'Comparer 2 appareils'],
    ['energy', 'Énergie (compteur)'], ['time', 'Heure précise'],
    ['time_range', 'Plage horaire'], ['weekday', 'Jour de semaine'], ['date', 'Date'],
    ['datetime', 'Date et heure'], ['day', 'Jour du mois'], ['month', 'Mois']
  ];
  var ACT_TYPES = [['device', 'Appareil'], ['dynamic', 'Valeur dynamique'],
                   ['actiongroup', 'Groupe d\'actions'], ['notification', 'Notification']];
  var OPS = [['==', '=='], ['!=', '!='], ['<', '&lt;'], ['<=', '&le;'], ['>', '&gt;'], ['>=', '&ge;']];
  var DAY_NAMES = ['', 'Lundi', 'Mardi', 'Mercredi', 'Jeudi', 'Vendredi', 'Samedi', 'Dimanche'];
  var DAY_SHORT = ['', 'Lun', 'Mar', 'Mer', 'Jeu', 'Ven', 'Sam', 'Dim'];
  var MONTHS = ['', 'Janvier', 'Février', 'Mars', 'Avril', 'Mai', 'Juin', 'Juillet', 'Août',
                'Septembre', 'Octobre', 'Novembre', 'Décembre'];
  var COVER_POSITION_CMDS = [251, 252];

  // --------------------------------------------------------------------------
  // Appareils et templates
  // --------------------------------------------------------------------------

  function hex4(n) { return ('0000' + ((parseInt(n, 10) || 0) >>> 0).toString(16).toUpperCase()).slice(-4); }
  function dev(ieee) { return (ieee && S.devices[ieee]) || null; }
  function usable(s) { return s && s !== 'null'; }

  function devLabel(ieee) {
    var d = dev(ieee);
    if (!d || !d.INFO) return ieee || '?';
    if (usable(d.INFO.alias)) return d.INFO.alias;
    if (usable(d.INFO.model)) return d.INFO.model;
    return ieee;
  }

  function devOptionText(ieee) {
    var i = dev(ieee).INFO || {};
    var m = i.model || 'Inconnu';
    return usable(i.alias) ? i.alias + ' (' + m + ')' : m + ' (' + ieee + ')';
  }

  function statesOf(ieee) { var d = dev(ieee); return (d && d.status) || []; }
  function actionsOf(ieee) { var d = dev(ieee); return (d && d.action) || []; }
  function hasCluster0006(ieee) { return statesOf(ieee).some(function(s) { return String(s.cluster).toUpperCase() === '0006'; }); }

  function deviceOptions(sel, filter) {
    var ids = Object.keys(S.devices).filter(function(i) { return !filter || filter(i); });
    ids.sort(function(a, b) { return devLabel(a).localeCompare(devLabel(b)); });
    var h = '<option value="">-- Choisir --</option>';
    ids.forEach(function(i) {
      h += '<option value="' + esc(i) + '"' + (i === sel ? ' selected' : '') + '>' + esc(devOptionText(i)) + '</option>';
    });
    // Appareil retire du reseau : garde dans la liste pour ne pas perdre la regle
    if (sel && ids.indexOf(sel) < 0) h += '<option value="' + esc(sel) + '" selected>' + esc(sel) + ' (absent)</option>';
    return h;
  }

  function clusterLabel(hex) {
    var n = CLUSTER_NAMES[hex];
    return n ? '0x' + hex + ' - ' + n : '0x' + hex;
  }

  function clusterOptions(ieee, sel) {
    var seen = {}, h = '<option value="">--</option>', found = false;
    sel = parseInt(sel, 10);
    statesOf(ieee).forEach(function(s) {
      var hx = String(s.cluster).toUpperCase();
      if (seen[hx]) return;
      seen[hx] = true;
      var dec = parseInt(hx, 16);
      if (dec === sel) found = true;
      h += '<option value="' + dec + '"' + (dec === sel ? ' selected' : '') + '>' + esc(clusterLabel(hx)) + '</option>';
    });
    if (sel && !found) h += '<option value="' + sel + '" selected>0x' + hex4(sel) + '</option>';
    return h;
  }

  function attrOptions(ieee, cl, sel) {
    var h = '<option value="">--</option>', found = false;
    cl = parseInt(cl, 10);
    sel = parseInt(sel, 10);
    if (!isNaN(cl)) {
      statesOf(ieee).forEach(function(s) {
        if (parseInt(s.cluster, 16) !== cl) return;
        var a = parseInt(s.attribut, 10);
        if (a === sel) found = true;
        h += '<option value="' + a + '"' + (a === sel ? ' selected' : '') + '>' + esc((s.name || a) + ' (' + a + ')') + '</option>';
      });
    }
    if (!isNaN(sel) && !found && ieee) h += '<option value="' + sel + '" selected>' + sel + '</option>';
    return h;
  }

  function stateItem(ieee, cl, at) {
    cl = parseInt(cl, 10);
    at = parseInt(at, 10);
    var st = statesOf(ieee);
    for (var i = 0; i < st.length; i++) {
      if (parseInt(st[i].cluster, 16) === cl && parseInt(st[i].attribut, 10) === at) return st[i];
    }
    return null;
  }

  function attrName(ieee, cl, at) {
    var s = stateItem(ieee, cl, at);
    return (s && s.name) ? s.name : 'attr.' + at;
  }

  // Valeur courante affichee (coefficient du template applique). La conversion
  // hexa -> nombre ne vaut que pour un attribut numerique : les attributs "string"
  // arrivent deja decodes du serveur.
  function currentValue(ieee, cl, at) {
    var d = dev(ieee);
    if (!d || !d.values) return null;
    var raw = d.values[hex4(cl) + ':' + parseInt(at, 10)];
    if (raw === undefined || raw === null) return null;
    var s = stateItem(ieee, cl, at);
    var coef = (s && s.coefficient) ? s.coefficient : 1;
    var unit = (s && usable(s.unit)) ? s.unit : '';
    var t = s && s.type ? String(s.type) : '';
    var isNum = t === 'numeric' || t === 'float' || (t === '' && /^[0-9A-Fa-f]+$/.test(String(raw)));
    if (isNum) {
      var n = parseInt(raw, 16);
      if (!isNaN(n)) {
        var v = n * coef;
        return { value: coef === 1 ? String(Math.round(v)) : v.toFixed(2), unit: unit };
      }
    }
    return { value: String(raw), unit: '' };
  }

  function metric(k) {
    for (var i = 0; i < ENERGY_METRICS.length; i++) if (ENERGY_METRICS[i].k === k) return ENERGY_METRICS[i];
    return null;
  }

  // --------------------------------------------------------------------------
  // Petits constructeurs HTML
  // --------------------------------------------------------------------------

  function lbl(t) { return '<label class="form-label small">' + t + '</label>'; }
  function col(n, inner, cls) { return '<div class="col-md-' + n + (cls ? ' ' + cls : '') + '">' + inner + '</div>'; }
  function row(inner) { return '<div class="row g-2">' + inner + '</div>'; }
  function selectHtml(cls, list, sel, extra) {
    var h = '<select class="form-select form-select-sm ' + cls + '"' + (extra || '') + '>';
    list.forEach(function(o) {
      h += '<option value="' + esc(o[0]) + '"' + (String(o[0]) === String(sel) ? ' selected' : '') + '>' + o[1] + '</option>';
    });
    return h + '</select>';
  }
  function dlOptions(opts) {
    return (opts || []).map(function(o) { return '<option value="' + esc(o[0]) + '">' + esc(o[1]) + '</option>'; }).join('');
  }

  // --------------------------------------------------------------------------
  // Squelette du formulaire (HTTP_ADD_RULE_HTML / HTTP_EDIT_RULE_HTML)
  // --------------------------------------------------------------------------

  function formHtml() {
    var title = S.mode === 'edit' ? 'Éditer une règle' : 'Ajouter une règle';
    return '<div class="container p-4" id="rule-editor">' +
      '<h4 class="card-title mb-4">' + title + '</h4>' +
      '<div class="card mx-auto shadow-sm"><div class="card-body"><form id="ruleForm" novalidate>' +
        '<div class="mb-4"><label for="ruleName" class="form-label fw-bold">Nom de la règle</label>' +
          '<input class="form-control" id="ruleName" type="text" maxlength="47" required>' +
          '<div class="form-text">Lettres, chiffres, espaces, _ et -</div></div>' +
        '<div class="mb-4"><div class="form-check form-switch">' +
          '<input class="form-check-input" type="checkbox" id="ruleEnabled" checked>' +
          '<label class="form-check-label fw-bold" for="ruleEnabled">Règle activée</label></div></div>' +
        '<div class="mb-4"><label class="form-label fw-bold">Déclenchement</label>' +
          '<div class="card"><div class="card-body"><div class="row g-2">' +
            col(2, lbl('Mode') + selectHtml('t-mode', [['timer', 'Timer (60 s)'], ['event', 'Événement']], 'timer')) +
            col(3, lbl('Appareil') + '<select class="form-select form-select-sm t-dev"></select>', 't-event') +
            col(3, lbl('Cluster') + '<select class="form-select form-select-sm t-cl"></select>', 't-event') +
            col(3, lbl('Attribut') + '<select class="form-select form-select-sm t-at"></select>', 't-event') +
          '</div><div class="form-text t-help"></div></div></div></div>' +
        '<div class="mb-4"><label class="form-label fw-bold">SI ... (Conditions)</label>' +
          '<div id="conditionsContainer"></div>' +
          '<button type="button" class="btn btn-sm btn-outline-primary mt-2" id="btn-add-cond">+ Condition</button></div>' +
        '<div class="mb-4"><label class="form-label fw-bold">Options d\'évaluation</label>' +
          '<div class="d-flex align-items-center gap-4 flex-wrap">' +
            '<div class="d-flex align-items-center gap-2"><label class="form-label small mb-0 text-nowrap">Mode :</label>' +
              selectHtml('', [['0', 'Sur changement d\'état'], ['1', 'À chaque évaluation']], '0', ' id="ruleRepeat" style="width:auto;"') + '</div>' +
            '<div class="d-flex align-items-center gap-2"><label class="form-label small mb-0 text-nowrap">Maintien :</label>' +
              '<div class="input-group input-group-sm" style="width:150px;"><input type="number" class="form-control" id="ruleDuration" min="0" value="0"><span class="input-group-text">min</span></div></div>' +
            '<div class="d-flex align-items-center gap-2" id="cooldownGroup"><label class="form-label small mb-0 text-nowrap">Intervalle min :</label>' +
              '<div class="input-group input-group-sm" style="width:150px;"><input type="number" class="form-control" id="ruleCooldown" min="0" value="0"><span class="input-group-text">min</span></div></div>' +
            '<div class="d-flex align-items-center gap-2"><label class="form-label small mb-0 text-nowrap">Max/jour :</label>' +
              '<div class="input-group input-group-sm" style="width:120px;"><input type="number" class="form-control" id="ruleMaxExecPerDay" min="0" value="0"></div>' +
              '<span class="form-text small text-muted mb-0">(0 = illimité)</span></div>' +
          '</div><div id="modeInfoBox" class="alert alert-info small mt-2 mb-0"></div></div>' +
        '<div class="mb-4"><label class="form-label fw-bold">ALORS ... (Actions)</label>' +
          '<div id="actionsContainer"></div>' +
          '<button type="button" class="btn btn-sm btn-outline-primary" data-add-act="actionsContainer">+ Action</button></div>' +
        '<div class="mb-4"><label class="form-label fw-bold">SINON ... (Actions)</label>' +
          '<div id="elseActionsContainer"></div>' +
          '<button type="button" class="btn btn-sm btn-outline-primary" data-add-act="elseActionsContainer">+ Action</button></div>' +
        '<div class="mb-4"><label class="form-label fw-bold">Résumé</label>' +
          '<div id="ruleSummary" class="card bg-light"><div class="card-body py-2" style="font-size:0.9em;"></div></div></div>' +
        '<div class="d-flex justify-content-between mt-4">' +
          '<a href="#configRules" class="btn btn-secondary btn-lg">Annuler</a>' +
          '<button type="submit" class="btn btn-primary btn-lg">' + (S.mode === 'edit' ? 'Enregistrer' : 'Créer') + '</button>' +
        '</div>' +
      '</form></div></div></div>';
  }

  // --------------------------------------------------------------------------
  // Declenchement
  // --------------------------------------------------------------------------

  function renderTrigger(t) {
    t = t || {};
    var f = $('#ruleForm');
    f.find('.t-mode').val(t.mode === 'event' ? 'event' : 'timer');
    f.find('.t-dev').html(deviceOptions(t.IEEE || ''));
    f.find('.t-cl').html(clusterOptions(t.IEEE || '', t.cluster));
    f.find('.t-at').html(attrOptions(t.IEEE || '', t.cluster, t.mode === 'event' ? t.attribute : NaN));
    onTriggerMode();
  }

  function onTriggerMode() {
    var ev = $('#ruleForm .t-mode').val() === 'event';
    $('#ruleForm .t-event').toggle(ev);
    $('#ruleForm .t-help').text(ev
      ? 'La règle est évaluée à chaque rapport de cet attribut.'
      : 'La règle est évaluée une fois par minute.');
  }

  // --------------------------------------------------------------------------
  // Conditions
  // --------------------------------------------------------------------------

  function addCondition(data) {
    var id = ++S.seq;
    var box = $('#conditionsContainer');
    var h = '';
    if (box.children('.condition-item').length > 0) {
      var lg = (data && String(data.logic).toUpperCase() === 'OR') ? 'OR' : 'AND';
      h += '<div class="logic-separator d-flex align-items-center my-1" data-for="' + id + '">' +
        '<hr class="flex-grow-1 m-0">' +
        selectHtml('logic-select mx-2', [['AND', 'ET (AND)'], ['OR', 'OU (OR)']], lg, ' style="width:auto;font-weight:bold;"') +
        '<hr class="flex-grow-1 m-0"></div>';
    }
    var type = (data && data.type) || 'device';
    var types = COND_TYPES.slice();
    if (!types.some(function(t) { return t[0] === type; })) types.push([type, esc(type) + ' (inconnu)']);
    h += '<div class="card mb-2 condition-item" data-id="' + id + '"><div class="card-body"><div class="row g-2">' +
      col(3, lbl('Type') + selectHtml('cond-type', types, type)) +
      '<div class="col-md-8 cond-fields"></div>' +
      '<div class="col-md-1 d-flex align-items-end"><button type="button" class="btn btn-sm btn-danger cond-remove" title="Retirer">×</button></div>' +
      '</div></div></div>';
    box.append(h);
    renderCond(box.children('.condition-item[data-id="' + id + '"]'), type, data || {});
  }

  function weekdayChecks(id, values) {
    var h = '<div class="btn-group btn-group-sm d-flex flex-wrap">';
    for (var d = 1; d <= 7; d++) {
      var cid = 'wd-' + id + '-' + d;
      h += '<input type="checkbox" class="btn-check c-wd" id="' + cid + '" value="' + d + '"' +
        (values.indexOf(String(d)) >= 0 ? ' checked' : '') + '>' +
        '<label class="btn btn-outline-primary" for="' + cid + '">' + DAY_SHORT[d] + '</label>';
    }
    return h + '</div>';
  }

  function ddmmToIso(v, withYear) {
    var p = String(v || '').split('/');
    if (p.length < 2) return '';
    var y = p.length >= 3 ? p[2].substring(0, 4) : (withYear || String(new Date().getFullYear()));
    return y + '-' + ('0' + p[1]).slice(-2) + '-' + ('0' + p[0]).slice(-2);
  }

  function renderCond(card, type, d) {
    var id = card.data('id');
    var w = card.find('.cond-fields');
    var op = d.operator || '';
    var v = d.value !== undefined && d.value !== null ? String(d.value) : '';
    switch (type) {
      case 'time':
        w.html(row(col(4, lbl('Heure') + '<input type="time" class="form-control form-control-sm c-time" value="' + esc(v || '08:00') + '" required>') +
          col(4, lbl('Opérateur') + selectHtml('c-op', [['==', '= (égal)'], ['!=', '&ne; (différent)'], ['<', '&lt; (avant)'], ['<=', '&le;'], ['>', '&gt; (après)'], ['>=', '&ge;']], op || '=='))));
        break;
      case 'time_range':
        w.html(row(col(3, lbl('De') + '<input type="time" class="form-control form-control-sm c-start" value="' + esc(v || '08:00') + '" required>') +
          col(3, lbl('À') + '<input type="time" class="form-control form-control-sm c-end" value="' + esc(d.value2 || '18:00') + '" required>') +
          col(3, lbl('Opérateur') + selectHtml('c-op', [['in', 'Dans la plage'], ['not_in', 'Hors plage']], op === 'not_in' || op === '!=' ? 'not_in' : 'in'))));
        break;
      case 'weekday':
        w.html(row(col(8, lbl('Jours') + weekdayChecks(id, v.split(',').map(function(s) { return s.trim(); }))) +
          col(3, lbl('Op.') + selectHtml('c-op', [['in', 'Ces jours'], ['not_in', 'Sauf ces jours']], op === 'not_in' ? 'not_in' : 'in'))));
        break;
      case 'date': {
        var parts = v.split('/');
        w.html(row(col(4, lbl('Date') + '<input type="date" class="form-control form-control-sm c-date" value="' + esc(ddmmToIso(v)) + '" required>') +
          col(3, lbl('Opérateur') + selectHtml('c-op', [['==', '= ce jour'], ['!=', '&ne; pas ce jour'], ['<', '&lt; avant'], ['>', '&gt; après']], op || '==')) +
          col(3, '<div class="form-check"><input class="form-check-input c-iy" type="checkbox" id="iy-' + id + '"' + (v && parts.length >= 3 ? '' : ' checked') +
            '><label class="form-check-label small" for="iy-' + id + '">Récurrent (ignorer l\'année)</label></div>', 'd-flex align-items-end')));
        break;
      }
      case 'datetime': {
        var dt = v.split(' ');
        w.html(row(col(3, lbl('Date') + '<input type="date" class="form-control form-control-sm c-dtdate" value="' + esc(ddmmToIso(dt[0])) + '" required>') +
          col(2, lbl('Heure') + '<input type="time" class="form-control form-control-sm c-dttime" value="' + esc(dt[1] || '08:00') + '" required>') +
          col(3, lbl('Opérateur') + selectHtml('c-op', [['==', '= exactement'], ['!=', '&ne; différent'], ['<', '&lt; avant'], ['>', '&gt; après']], op || '=='))));
        break;
      }
      case 'day':
        w.html(row(col(4, lbl('Jour du mois') + '<input type="number" class="form-control form-control-sm c-day" min="1" max="31" value="' + esc(v || '1') + '" required>') +
          col(4, lbl('Opérateur') + selectHtml('c-op', [['==', '= égal'], ['!=', '&ne; différent'], ['<', '&lt;'], ['>', '&gt;']], op || '=='))));
        break;
      case 'month':
        w.html(row(col(4, lbl('Mois') + selectHtml('c-month', MONTHS.slice(1).map(function(m, i) { return [String(i + 1), m]; }), v || '1')) +
          col(4, lbl('Opérateur') + selectHtml('c-op', [['==', '= égal'], ['!=', '&ne; différent']], op || '=='))));
        break;
      case 'energy': {
        var ms = ENERGY_METRICS.map(function(m) { return [m.k, esc(m.l) + (m.u ? ' (' + esc(m.u) + ')' : '')]; });
        w.html(row(col(5, lbl('Grandeur') + selectHtml('c-metric', ms, d.metric || 'power_va')) +
          col(2, lbl('Op.') + selectHtml('c-op', OPS, op || '>')) +
          col(4, lbl('Valeur') + '<input type="text" class="form-control form-control-sm c-val" list="dl-' + id + '" value="' + esc(v) + '" required>' +
            '<datalist id="dl-' + id + '" class="c-dl"></datalist>')));
        onMetric(card);
        break;
      }
      case 'device_compare':
        w.html(row(col(5, lbl('Appareil 1') + '<select class="form-select form-select-sm c-dev" required>' + deviceOptions(d.IEEE || '') + '</select>') +
            col(4, lbl('Cluster') + '<select class="form-select form-select-sm c-cl" required>' + clusterOptions(d.IEEE, d.cluster) + '</select>') +
            col(3, lbl('Attribut') + '<select class="form-select form-select-sm c-at" required>' + attrOptions(d.IEEE, d.cluster, d.attribute) + '</select><span class="c-cur small text-success"></span>')) +
          '<div class="mt-2">' + row(col(3, lbl('Opérateur') + selectHtml('c-op', [['>', '&gt; supérieur(e)'], ['>=', '&ge; sup. ou égal(e)'], ['<', '&lt; inférieur(e)'], ['<=', '&le; inf. ou égal(e)'], ['==', '== égal'], ['!=', '!= différent']], op || '>')) +
            col(3, lbl('Décalage') + '<input type="number" class="form-control form-control-sm c-val" step="any" value="' + esc(v) + '" placeholder="ex : 2">')) + '</div>' +
          '<div class="mt-2">' + row(col(5, lbl('Appareil 2') + '<select class="form-select form-select-sm c-dev2" required>' + deviceOptions(d.IEEE2 || '') + '</select>') +
            col(4, lbl('Cluster') + '<select class="form-select form-select-sm c-cl2" required>' + clusterOptions(d.IEEE2, d.cluster2) + '</select>') +
            col(3, lbl('Attribut') + '<select class="form-select form-select-sm c-at2" required>' + attrOptions(d.IEEE2, d.cluster2, d.attribute2) + '</select><span class="c-cur2 small text-success"></span>')) + '</div>');
        showCurrent(card);
        break;
      case 'device':
        w.html(row(col(3, lbl('Appareil') + '<select class="form-select form-select-sm c-dev" required>' + deviceOptions(d.IEEE || '') + '</select>') +
          col(2, lbl('Cluster') + '<select class="form-select form-select-sm c-cl" required>' + clusterOptions(d.IEEE, d.cluster) + '</select>') +
          col(2, lbl('Attribut') + '<select class="form-select form-select-sm c-at" required>' + attrOptions(d.IEEE, d.cluster, d.attribute) + '</select>') +
          col(2, lbl('Propriété') + '<select class="form-select form-select-sm c-sub"></select>', 'c-sub-col') +
          col(1, lbl('Op.') + selectHtml('c-op', OPS, op || '==')) +
          col(2, lbl('Valeur') + '<input type="text" class="form-control form-control-sm c-val" list="dl-' + id + '" value="' + esc(v) + '" required>' +
            '<datalist id="dl-' + id + '" class="c-dl"></datalist><span class="c-cur small text-success"></span>')));
        refreshSubfield(card, d.subfield || '');
        showCurrent(card);
        break;
      default:
        // Type inconnu (fichier ecrit a la main) : conserve tel quel
        w.html('<div class="small text-muted pt-4">Condition de type non géré par l\'éditeur (conservée telle quelle).</div>');
        card.data('raw', d);
    }
  }

  // Sous-champ STGE : seulement pour le cluster FF66 / attribut 535
  function refreshSubfield(card, sel) {
    var colEl = card.find('.c-sub-col'), s = card.find('.c-sub');
    if (!colEl.length) return;
    var cl = parseInt(card.find('.c-cl').val(), 10), at = parseInt(card.find('.c-at').val(), 10);
    if (cl !== STGE_CLUSTER || at !== STGE_ATTRIBUTE) {
      colEl.hide();
      s.html('<option value="">--</option>');
      onSubfield(card);
      return;
    }
    var cur = sel !== undefined ? sel : s.val();
    var h = '<option value="">(valeur brute)</option>';
    for (var k in STGE_FIELDS) h += '<option value="' + k + '"' + (k === cur ? ' selected' : '') + '>' + esc(STGE_FIELDS[k].label) + '</option>';
    s.html(h);
    colEl.show();
    onSubfield(card);
  }

  function onSubfield(card) {
    var f = card.find('.c-sub').val();
    card.find('.c-dl').html(f && STGE_FIELDS[f] ? dlOptions(STGE_FIELDS[f].opts) : '');
  }

  function onMetric(card) {
    var m = metric(card.find('.c-metric').val());
    card.find('.c-dl').html(m && m.opts ? dlOptions(m.opts) : '');
    card.find('.c-val').attr('placeholder', m && m.text ? 'ex : HP BLEU' : (m && m.u ? m.u : ''));
  }

  function showCurrent(card) {
    function badge(devSel, clSel, atSel, out) {
      var ieee = card.find(devSel).val(), cl = card.find(clSel).val(), at = card.find(atSel).val();
      var b = card.find(out);
      if (!ieee || !cl || at === '' || at === null) { b.text(''); return; }
      var sv = currentValue(ieee, cl, at);
      b.text(sv ? 'Actuel : ' + sv.value + (sv.unit ? ' ' + sv.unit : '') : 'Actuel : --');
    }
    badge('.c-dev', '.c-cl', '.c-at', '.c-cur');
    badge('.c-dev2', '.c-cl2', '.c-at2', '.c-cur2');
  }

  function removeCondition(card) {
    var id = card.data('id');
    $('.logic-separator[data-for="' + id + '"]').remove();
    card.remove();
    var first = $('#conditionsContainer').children().first();
    if (first.hasClass('logic-separator')) first.remove();
  }

  function collectCondition(card) {
    var id = card.data('id');
    var type = card.find('.cond-type').val();
    if (card.data('raw')) return card.data('raw');
    var sep = $('.logic-separator[data-for="' + id + '"]');
    var c = { type: type, operator: card.find('.c-op').val() || '', logic: sep.length ? sep.find('.logic-select').val() : 'AND',
              IEEE: '', cluster: 0, attribute: 0, value: '', value2: '' };
    switch (type) {
      case 'time': c.value = card.find('.c-time').val(); break;
      case 'time_range': c.value = card.find('.c-start').val(); c.value2 = card.find('.c-end').val(); break;
      case 'weekday':
        c.value = card.find('.c-wd:checked').map(function() { return this.value; }).get().join(',');
        break;
      case 'date': {
        var dv = card.find('.c-date').val();
        if (dv) {
          var p = dv.split('-');
          c.value = card.find('.c-iy').is(':checked') ? p[2] + '/' + p[1] : p[2] + '/' + p[1] + '/' + p[0];
        }
        break;
      }
      case 'datetime': {
        var dd = card.find('.c-dtdate').val(), tt = card.find('.c-dttime').val() || '00:00';
        if (dd) { var q = dd.split('-'); c.value = q[2] + '/' + q[1] + '/' + q[0] + ' ' + tt; }
        break;
      }
      case 'day': c.value = card.find('.c-day').val(); break;
      case 'month': c.value = card.find('.c-month').val(); break;
      case 'energy': c.metric = card.find('.c-metric').val(); c.value = $.trim(card.find('.c-val').val()); break;
      case 'device_compare':
        c.IEEE = card.find('.c-dev').val(); c.cluster = parseInt(card.find('.c-cl').val(), 10) || 0;
        c.attribute = parseInt(card.find('.c-at').val(), 10) || 0;
        c.IEEE2 = card.find('.c-dev2').val(); c.cluster2 = parseInt(card.find('.c-cl2').val(), 10) || 0;
        c.attribute2 = parseInt(card.find('.c-at2').val(), 10) || 0;
        c.value = card.find('.c-val').val() || '';
        break;
      default:
        c.IEEE = card.find('.c-dev').val(); c.cluster = parseInt(card.find('.c-cl').val(), 10) || 0;
        c.attribute = parseInt(card.find('.c-at').val(), 10) || 0;
        c.value = card.find('.c-val').val();
        c.subfield = card.find('.c-sub').val() || '';
    }
    return c;
  }

  // --------------------------------------------------------------------------
  // Actions (listes ALORS et SINON)
  // --------------------------------------------------------------------------

  function addAction(listId, data) {
    var id = ++S.seq;
    var type = (data && data.type) || 'device';
    var types = ACT_TYPES.slice();
    if (type === 'onoff') types.push(['onoff', 'ON/OFF (ancien format)']);
    else if (!types.some(function(t) { return t[0] === type; })) types.push([type, esc(type) + ' (inconnu)']);
    var list = $('#' + listId);
    list.append('<div class="card mb-2 action-item" data-id="' + id + '"><div class="card-body"><div class="row g-2">' +
      col(2, lbl('Type') + selectHtml('a-type', types, type)) +
      '<div class="col-md-9 a-fields"></div>' +
      '<div class="col-md-1 d-flex align-items-end"><button type="button" class="btn btn-sm btn-danger a-remove" title="Retirer">×</button></div>' +
      '</div></div></div>');
    renderAction(list.children('.action-item[data-id="' + id + '"]'), type, data || {});
  }

  function actionNameOptions(ieee, sel) {
    var h = '<option value="">-- Action --</option>', found = false;
    actionsOf(ieee).forEach(function(a) {
      if (a.visible === 0 && a.name !== sel) return;
      if (a.name === sel) found = true;
      h += '<option value="' + esc(a.name) + '" data-endpoint="' + (parseInt(a.endpoint, 10) || 1) +
        '" data-command="' + (parseInt(a.command, 10) || 0) + '"' + (a.name === sel ? ' selected' : '') + '>' + esc(a.name) + '</option>';
    });
    if (sel && !found) h += '<option value="' + esc(sel) + '" selected>' + esc(sel) + ' (absente)</option>';
    return h;
  }

  function defaultEndpoint(ieee) {
    var d = dev(ieee);
    return (d && d.INFO && d.INFO.endpoint) ? d.INFO.endpoint : 1;
  }

  function renderAction(card, type, d) {
    var w = card.find('.a-fields');
    var ep = d.endpoint || defaultEndpoint(d.IEEE);
    var devFields = col(4, lbl('Appareil') + '<select class="form-select form-select-sm a-dev" required>' +
        deviceOptions(d.IEEE || '', function(i) { return actionsOf(i).length > 0; }) + '</select>') +
      col(4, lbl('Action') + '<select class="form-select form-select-sm a-name" required>' + actionNameOptions(d.IEEE, d.actionName || '') + '</select>') +
      col(2, lbl('Endpoint') + '<input type="number" class="form-control form-control-sm a-ep" min="1" value="' + esc(ep) + '">') +
      col(2, lbl('Position') + '<input type="number" class="form-control form-control-sm a-pos" min="0" max="100" value="' + esc(d.value || '') + '" placeholder="0-100">', 'a-pos-col');
    switch (type) {
      case 'device':
        w.html(row(devFields));
        onActionName(card, false);
        break;
      case 'dynamic':
        w.html(row(devFields) + '<div class="mt-2">' + row(
          col(4, lbl('Source') + '<select class="form-select form-select-sm a-src" required>' + deviceOptions(d.sourceIEEE || '') + '</select>') +
          col(3, lbl('Cluster') + '<select class="form-select form-select-sm a-scl" required>' + clusterOptions(d.sourceIEEE, d.sourceCluster) + '</select>') +
          col(3, lbl('Attribut') + '<select class="form-select form-select-sm a-sat" required>' + attrOptions(d.sourceIEEE, d.sourceCluster, d.sourceAttribute) + '</select>') +
          col(1, lbl('Coeff') + '<input type="number" class="form-control form-control-sm a-coef" step="any" value="' + esc(d.coefficient !== undefined ? d.coefficient : 1) + '">') +
          col(1, lbl('Offset') + '<input type="number" class="form-control form-control-sm a-off" step="any" value="' + esc(d.offset !== undefined ? d.offset : 0) + '">')) +
          '<div class="form-text">Valeur envoyée = valeur de la source &times; coeff + offset (arrondie).</div></div>');
        card.find('.a-pos-col').hide();
        break;
      case 'actiongroup': {
        var gl = [['', '-- Choisir --']].concat(S.groups.map(function(g) { return [g.name, esc(g.name)]; }));
        if (d.actionName && !S.groups.some(function(g) { return g.name === d.actionName; })) gl.push([d.actionName, esc(d.actionName) + ' (absent)']);
        w.html(row(col(6, lbl('Groupe d\'actions') + selectHtml('a-group', gl, d.actionName || '', ' required')) +
          col(6, '<div class="form-text">Déclenche toutes les actions du groupe. <a href="#configActionGroups">Gérer les groupes</a></div>', 'd-flex align-items-end')));
        break;
      }
      case 'notification':
        w.html(row(col(5, lbl('Titre') + '<input type="text" class="form-control form-control-sm a-title" maxlength="95" value="' + esc(d.title || '') + '" required>') +
          col(7, lbl('Message') + '<input type="text" class="form-control form-control-sm a-msg" maxlength="255" value="' + esc(d.message || '') + '">')) +
          '<div class="form-text small">Variables : {rule} {date} {time} {value} {device} {threshold} {value_N} {device_N}</div>');
        break;
      case 'onoff':
        w.html(row(col(5, lbl('Appareil') + '<select class="form-select form-select-sm a-dev" required>' + deviceOptions(d.IEEE || '', hasCluster0006) + '</select>') +
          col(3, lbl('Endpoint') + '<input type="number" class="form-control form-control-sm a-ep" min="1" value="' + esc(ep) + '">') +
          col(3, lbl('Valeur') + selectHtml('a-onoff', [['1', 'ON'], ['0', 'OFF'], ['2', 'TOGGLE']], d.value || '1'))));
        break;
      default:
        w.html('<div class="small text-muted pt-4">Action de type non géré par l\'éditeur (conservée telle quelle).</div>');
        card.data('raw', d);
    }
  }

  // Selection d'une action : endpoint du template, champ position si volet
  function onActionName(card, setEp) {
    var opt = card.find('.a-name option:selected');
    var cmd = parseInt(opt.data('command'), 10);
    if (setEp && opt.data('endpoint')) card.find('.a-ep').val(opt.data('endpoint'));
    var isPos = card.find('.a-type').val() === 'device' && COVER_POSITION_CMDS.indexOf(cmd) >= 0;
    card.find('.a-pos-col').toggle(isPos);
    card.find('.a-pos').prop('required', isPos);
  }

  function collectAction(card) {
    if (card.data('raw')) return card.data('raw');
    var t = card.find('.a-type').val();
    var a = { type: t, IEEE: '', actionName: '', endpoint: 0, value: '', title: '', message: '' };
    if (t === 'device' || t === 'dynamic') {
      a.IEEE = card.find('.a-dev').val();
      a.actionName = card.find('.a-name').val();
      a.endpoint = parseInt(card.find('.a-ep').val(), 10) || 1;
      if (t === 'device' && card.find('.a-pos-col').is(':visible')) a.value = $.trim(card.find('.a-pos').val());
      if (t === 'dynamic') {
        a.sourceIEEE = card.find('.a-src').val();
        a.sourceCluster = parseInt(card.find('.a-scl').val(), 10) || 0;
        a.sourceAttribute = parseInt(card.find('.a-sat').val(), 10) || 0;
        a.coefficient = parseFloat(card.find('.a-coef').val());
        if (isNaN(a.coefficient)) a.coefficient = 1;
        a.offset = parseFloat(card.find('.a-off').val()) || 0;
      }
    } else if (t === 'onoff') {
      a.IEEE = card.find('.a-dev').val();
      a.endpoint = parseInt(card.find('.a-ep').val(), 10) || 1;
      a.value = card.find('.a-onoff').val();
    } else if (t === 'actiongroup') {
      a.actionName = card.find('.a-group').val();
    } else {
      a.title = card.find('.a-title').val();
      a.message = card.find('.a-msg').val();
    }
    return a;
  }

  // --------------------------------------------------------------------------
  // Regle complete
  // --------------------------------------------------------------------------

  function collectRule() {
    var f = $('#ruleForm');
    var ev = f.find('.t-mode').val() === 'event';
    var r = {
      name: $.trim($('#ruleName').val()),
      enabled: $('#ruleEnabled').is(':checked'),
      trigger: {
        mode: ev ? 'event' : 'timer',
        IEEE: ev ? (f.find('.t-dev').val() || '') : '',
        cluster: ev ? (parseInt(f.find('.t-cl').val(), 10) || 0) : 0,
        attribute: ev ? (parseInt(f.find('.t-at').val(), 10) || 0) : 0
      },
      conditions: [], actions: [], elseActions: [],
      duration: (parseInt($('#ruleDuration').val(), 10) || 0) * 60,
      repeat: $('#ruleRepeat').val() === '1',
      cooldown: (parseInt($('#ruleCooldown').val(), 10) || 0) * 60,
      maxExecPerDay: parseInt($('#ruleMaxExecPerDay').val(), 10) || 0
    };
    if (S.mode === 'edit') r.oldName = S.oldName;
    $('#conditionsContainer .condition-item').each(function() { r.conditions.push(collectCondition($(this))); });
    $('#actionsContainer .action-item').each(function() { r.actions.push(collectAction($(this))); });
    $('#elseActionsContainer .action-item').each(function() { r.elseActions.push(collectAction($(this))); });
    return r;
  }

  function loadRule(rule) {
    S.oldName = rule.name || '';
    $('#ruleName').val(rule.name || '');
    $('#ruleEnabled').prop('checked', rule.enabled !== false);
    if (rule.duration > 0) $('#ruleDuration').val(Math.round(rule.duration / 60));
    if (rule.repeat) $('#ruleRepeat').val('1');
    if (rule.cooldown > 0) $('#ruleCooldown').val(Math.round(rule.cooldown / 60));
    if (rule.maxExecPerDay > 0) $('#ruleMaxExecPerDay').val(rule.maxExecPerDay);
    renderTrigger(rule.trigger || {});
    (rule.conditions && rule.conditions.length ? rule.conditions : [null]).forEach(function(c) { addCondition(c); });
    (rule.actions && rule.actions.length ? rule.actions : [null]).forEach(function(a) { addAction('actionsContainer', a); });
    (rule.elseActions || []).forEach(function(a) { addAction('elseActionsContainer', a); });
  }

  // --------------------------------------------------------------------------
  // Resume en francais (buildRuleSummary)
  // --------------------------------------------------------------------------

  function opText(op) {
    return { '==': 'vaut', '!=': 'ne vaut pas', '<': 'est inférieur(e) à', '<=': 'est inférieur(e) ou égal(e) à',
             '>': 'est supérieur(e) à', '>=': 'est supérieur(e) ou égal(e) à' }[op] || esc(op);
  }

  function timeOpText(op, v) {
    v = '<b>' + esc(v) + '</b>';
    return { '==': 'il est exactement ' + v, '!=': 'il n\'est pas ' + v, '<': 'il est avant ' + v, '<=': 'il est avant ou à ' + v,
             '>': 'il est après ' + v, '>=': 'il est après ou à ' + v }[op] || 'heure ' + esc(op) + ' ' + v;
  }

  function list(names, last) {
    return names.length > 1 ? names.slice(0, -1).join(', ') + ' ' + last + ' ' + names[names.length - 1] : (names[0] || '...');
  }

  function condText(c) {
    var v = esc(c.value || '?');
    switch (c.type) {
      case 'device': {
        var s = stateItem(c.IEEE, c.cluster, c.attribute);
        var unit = s && usable(s.unit) ? ' ' + esc(s.unit) : '';
        var an = c.subfield && STGE_FIELDS[c.subfield] ? STGE_FIELDS[c.subfield].label : attrName(c.IEEE, c.cluster, c.attribute);
        return 'la <b>' + esc(an) + '</b> <i>(' + esc(devLabel(c.IEEE)) + ')</i> ' + opText(c.operator) + ' <b>' + v + unit + '</b>';
      }
      case 'device_compare': {
        var t = 'la <b>' + esc(attrName(c.IEEE, c.cluster, c.attribute)) + '</b> <i>(' + esc(devLabel(c.IEEE)) + ')</i> ' + opText(c.operator) +
          ' la <b>' + esc(attrName(c.IEEE2, c.cluster2, c.attribute2)) + '</b> <i>(' + esc(devLabel(c.IEEE2)) + ')</i>';
        if (c.value && parseFloat(c.value) !== 0) t += ' + <b>' + v + '</b>';
        return t;
      }
      case 'energy': {
        var m = metric(c.metric);
        return 'la grandeur <b>' + esc(m ? m.l : c.metric) + '</b> ' + opText(c.operator) + ' <b>' + v + (m && m.u ? ' ' + esc(m.u) : '') + '</b>';
      }
      case 'time': return timeOpText(c.operator, c.value || '?');
      case 'time_range':
        return c.operator === 'not_in' ? 'l\'heure est en dehors de <b>' + esc(c.value) + ' - ' + esc(c.value2) + '</b>'
                                       : 'l\'heure est entre <b>' + esc(c.value) + '</b> et <b>' + esc(c.value2) + '</b>';
      case 'weekday': {
        var days = String(c.value || '').split(',').filter(Boolean).map(function(d) { return DAY_NAMES[parseInt(d, 10)] || d; });
        if (!days.length) return 'le jour est ...';
        return c.operator === 'not_in' ? 'le jour n\'est pas ' + list(days, 'ni') : 'le jour est ' + list(days, 'ou');
      }
      case 'date':
        return ({ '==': 'la date est le ', '!=': 'la date n\'est pas le ', '<': 'la date est avant le ' }[c.operator] ||
          'la date est après le ') + '<b>' + v + '</b>';
      case 'datetime':
        return ({ '==': 'la date et heure est exactement ', '!=': 'la date et heure n\'est pas ', '<': 'la date et heure est avant ' }[c.operator] ||
          'la date et heure est après ') + '<b>' + v + '</b>';
      case 'day':
        return ({ '==': 'le jour du mois est le ', '!=': 'le jour du mois n\'est pas le ', '<': 'le jour du mois est avant le ' }[c.operator] ||
          'le jour du mois est après le ') + '<b>' + v + '</b>';
      case 'month':
        return (c.operator === '!=' ? 'le mois n\'est pas ' : 'le mois est ') + '<b>' + esc(MONTHS[parseInt(c.value, 10)] || c.value) + '</b>';
      default: return esc(c.type || '...');
    }
  }

  function actText(a) {
    switch (a.type) {
      case 'device':
        return 'exécuter <b>' + esc(a.actionName || '?') + '</b>' + (a.value ? ' (' + esc(a.value) + ' %)' : '') + ' sur <i>' + esc(devLabel(a.IEEE)) + '</i>';
      case 'dynamic': {
        var t = 'envoyer la valeur de <i>' + esc(devLabel(a.sourceIEEE)) + '</i>';
        if (a.coefficient !== 1) t += ' ×' + esc(a.coefficient);
        if (a.offset) t += (a.offset >= 0 ? '+' : '') + esc(a.offset);
        return t + ' via <b>' + esc(a.actionName || '?') + '</b> sur <i>' + esc(devLabel(a.IEEE)) + '</i>';
      }
      case 'onoff':
        return (a.value === '1' ? 'allumer' : a.value === '0' ? 'éteindre' : 'basculer') + ' <i>' + esc(devLabel(a.IEEE)) + '</i>';
      case 'notification': return 'envoyer la notification « ' + esc(a.title || '...') + ' »';
      case 'actiongroup': return 'déclencher le groupe d’actions <b>' + esc(a.actionName || '?') + '</b>';
      default: return '...';
    }
  }

  function buildSummary() {
    var r = collectRule();
    var cs = '';
    r.conditions.forEach(function(c, i) {
      if (i > 0) cs += String(c.logic).toUpperCase() === 'OR' ? ' <strong>ou que</strong> ' : ' <strong>et que</strong> ';
      cs += condText(c);
    });
    var dur = Math.round(r.duration / 60);
    if (dur > 0) cs += ' <strong>pendant ' + dur + ' minute' + (dur > 1 ? 's' : '') + '</strong>';
    if (r.repeat) {
      var cd = Math.round(r.cooldown / 60);
      cs += cd > 0 ? ' <em>(répété toutes les ' + cd + ' min)</em>' : ' <em>(à chaque évaluation)</em>';
    }
    if (r.maxExecPerDay > 0) cs += ' <em>(max ' + r.maxExecPerDay + ' fois/jour)</em>';
    var h = '';
    if (cs) h += '<strong>Si</strong> ' + cs;
    if (r.actions.length) h += ', <strong>alors</strong> ' + r.actions.map(actText).join(', ');
    if (r.elseActions.length) h += ', <strong>sinon</strong> ' + r.elseActions.map(actText).join(', ');
    $('#ruleSummary .card-body').html(h ? h + '.' : '<em class="text-muted">Complétez la règle pour voir le résumé...</em>');
  }

  function scheduleSummary() {
    clearTimeout(S.timer);
    S.timer = setTimeout(function() { if ($('#ruleSummary').length) buildSummary(); }, 200);
  }

  function updateModeInfo() {
    var rep = $('#ruleRepeat').val() === '1';
    $('#cooldownGroup').toggleClass('d-none', !rep).toggleClass('d-flex', rep);
    $('#modeInfoBox').html(rep
      ? '<strong>Mode répétition :</strong> les actions se déclenchent <strong>à chaque évaluation</strong> tant que les conditions sont vraies. Utilisez l\'intervalle minimum pour espacer les exécutions. Les « actions sinon » se déclenchent une fois quand les conditions repassent à FAUX.'
      : '<strong>Mode changement d\'état :</strong> les actions ne se déclenchent que lors d\'un <strong>changement d\'état</strong> (FAUX → VRAI). Si les conditions restent vraies, aucune action n\'est ré-exécutée. Les « actions sinon » se déclenchent quand les conditions repassent de VRAI à FAUX.');
  }

  // --------------------------------------------------------------------------
  // Validation et envoi
  // --------------------------------------------------------------------------

  function validate(r) {
    if (!r.name) return 'Le nom de la règle est obligatoire';
    if (!/^[A-Za-z0-9_ \-]+$/.test(r.name)) return 'Le nom ne peut contenir que des lettres, chiffres, espaces, _ et -';
    if (!r.conditions.length) return 'Ajoutez au moins une condition';
    var f = $('#ruleForm');
    if (r.trigger.mode === 'event' && (!r.trigger.IEEE || f.find('.t-cl').val() === '' || f.find('.t-at').val() === ''))
      return 'Déclenchement sur événement : choisissez l\'appareil, le cluster et l\'attribut';
    var form = document.getElementById('ruleForm');
    if (form && form.checkValidity && !form.checkValidity()) {
      form.classList.add('was-validated');
      return 'Complétez les champs signalés en rouge';
    }
    return '';
  }

  function bindEvents() {
    var f = $('#ruleForm');
    f.on('change', '.t-mode', onTriggerMode);
    f.on('change', '.t-dev', function() {
      f.find('.t-cl').html(clusterOptions($(this).val()));
      f.find('.t-at').html(attrOptions('', NaN));
    });
    f.on('change', '.t-cl', function() { f.find('.t-at').html(attrOptions(f.find('.t-dev').val(), $(this).val())); });

    f.on('click', '#btn-add-cond', function() { addCondition(null); scheduleSummary(); });
    f.on('click', '[data-add-act]', function() { addAction($(this).attr('data-add-act'), null); scheduleSummary(); });
    f.on('click', '.cond-remove', function() { removeCondition($(this).closest('.condition-item')); scheduleSummary(); });
    f.on('click', '.a-remove', function() { $(this).closest('.action-item').remove(); scheduleSummary(); });

    f.on('change', '.cond-type', function() {
      var card = $(this).closest('.condition-item');
      card.removeData('raw');
      renderCond(card, $(this).val(), {});
    });
    f.on('change', '.c-dev, .c-dev2', function() {
      var card = $(this).closest('.condition-item'), two = $(this).hasClass('c-dev2') ? '2' : '';
      card.find('.c-cl' + two).html(clusterOptions($(this).val()));
      card.find('.c-at' + two).html(attrOptions('', NaN));
      refreshSubfield(card);
      showCurrent(card);
    });
    f.on('change', '.c-cl, .c-cl2', function() {
      var card = $(this).closest('.condition-item'), two = $(this).hasClass('c-cl2') ? '2' : '';
      card.find('.c-at' + two).html(attrOptions(card.find('.c-dev' + two).val(), $(this).val()));
      refreshSubfield(card);
      showCurrent(card);
    });
    f.on('change', '.c-at, .c-at2', function() {
      var card = $(this).closest('.condition-item');
      refreshSubfield(card);
      showCurrent(card);
    });
    f.on('change', '.c-sub', function() { onSubfield($(this).closest('.condition-item')); });
    f.on('change', '.c-metric', function() { onMetric($(this).closest('.condition-item')); });

    f.on('change', '.a-type', function() {
      var card = $(this).closest('.action-item');
      card.removeData('raw');
      renderAction(card, $(this).val(), {});
    });
    f.on('change', '.a-dev', function() {
      var card = $(this).closest('.action-item'), ieee = $(this).val();
      card.find('.a-name').html(actionNameOptions(ieee, ''));
      card.find('.a-ep').val(defaultEndpoint(ieee));
      onActionName(card, false);
    });
    f.on('change', '.a-name', function() { onActionName($(this).closest('.action-item'), true); });
    f.on('change', '.a-src', function() {
      var card = $(this).closest('.action-item');
      card.find('.a-scl').html(clusterOptions($(this).val()));
      card.find('.a-sat').html(attrOptions('', NaN));
    });
    f.on('change', '.a-scl', function() {
      var card = $(this).closest('.action-item');
      card.find('.a-sat').html(attrOptions(card.find('.a-src').val(), $(this).val()));
    });

    f.on('change', '#ruleRepeat', updateModeInfo);
    f.on('change input', 'select, input', scheduleSummary);

    f.on('submit', function(e) {
      e.preventDefault();
      var r = collectRule();
      var err = validate(r);
      if (err) { A.showAlert(esc(err), 'danger'); return; }
      var url = S.mode === 'edit' ? '/api/rules/edit' : '/api/rules/add';
      var btn = f.find('button[type=submit]').prop('disabled', true);
      A.postJSON(url, r).done(function() {
        A.showAlert(S.mode === 'edit' ? 'Règle modifiée !' : 'Règle créée !');
        window.location.hash = '#configRules';
      }).fail(function(xhr) {
        A.showAlert('Erreur : ' + A.apiErrorMsg(xhr, 'enregistrement impossible'), 'danger');
      }).always(function() { btn.prop('disabled', false); });
    });
  }

  // --------------------------------------------------------------------------
  // Ouverture
  // --------------------------------------------------------------------------

  function unwrap(x) { return (Array.isArray(x) && x.length === 3 && x[2] && x[2].getResponseHeader) ? x[0] : x; }

  function open(mode, name) {
    S.mode = mode === 'edit' ? 'edit' : 'add';
    S.seq = 0;
    S.oldName = '';
    A.setContent('<div class="text-center p-5"><div class="spinner-border" role="status"></div></div>');
    if (S.mode === 'edit' && !name) {
      A.setContent('<div class="container p-4"><div class="alert alert-warning">Nom de règle manquant. <a href="#configRules">Retour à la liste</a></div></div>');
      return;
    }
    var pDev = $.getJSON('/api/rules/devices');
    // Les groupes d'actions ne doivent jamais bloquer l'editeur
    var pGrp = $.Deferred();
    $.getJSON('/api/actiongroups/list').done(function(g) { pGrp.resolve(g); }).fail(function() { pGrp.resolve({ groups: [] }); });
    var pRule = S.mode === 'edit' ? $.getJSON('/api/rules/get', { name: name }) : $.Deferred().resolve(null);
    $.when(pDev, pGrp, pRule).done(function(d, g, r) {
      S.devices = unwrap(d) || {};
      S.groups = (unwrap(g) || {}).groups || [];
      var rule = unwrap(r);
      A.setContent(formHtml());
      bindEvents();
      if (rule) {
        loadRule(rule);
      } else {
        renderTrigger({});
        addCondition(null);
        addAction('actionsContainer', null);
      }
      updateModeInfo();
      buildSummary();
    }).fail(function(xhr) {
      A.setContent('<div class="container p-4"><div class="alert alert-danger">Chargement impossible : ' +
        A.apiErrorMsg(xhr, 'erreur') + '. <a href="#configRules">Retour à la liste</a></div></div>');
    });
  }

  window.RulesEditor = { open: open };
})();
