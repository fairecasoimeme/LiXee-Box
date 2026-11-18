// ============================================================================
// LiXee MatterBridge - Mesures -> Energie -> Historique (#energyHistory)
//
// Historique detaille de la consommation : archive datee de la box (13 mois
// heure par heure, jours, semaines, mois, annees), navigation par fleches et
// calendrier (jours sans donnees grises), periode libre, comparaison, kWh/€,
// empile/separe, temperature et presence, statistiques et export CSV.
// Electricite, production et gaz sur un graphe ; eau sur un second.
// API : /api/energy/history, /api/energy/history/days (web_energy.cpp)
// ============================================================================
(function() {
  'use strict';
  var A = window.LixeeApp;
  if (!A) return;
  var esc = A.escHtml;

  var GRANS = [
    { id: 'hour',  label: 'Heure' },
    { id: 'day',   label: 'Jour' },
    { id: 'week',  label: 'Semaine' },
    { id: 'month', label: 'Mois' },
    { id: 'year',  label: 'Année' }
  ];
  var MAX_DAYS = { hour: 31, day: 400, week: 742, month: 3653, year: 7305 };
  var MONTHS = ['janvier', 'février', 'mars', 'avril', 'mai', 'juin', 'juillet', 'août',
                'septembre', 'octobre', 'novembre', 'décembre'];
  var MONTHS_S = ['janv.', 'févr.', 'mars', 'avr.', 'mai', 'juin', 'juil.', 'août', 'sept.', 'oct.', 'nov.', 'déc.'];
  var DAYS = ['dimanche', 'lundi', 'mardi', 'mercredi', 'jeudi', 'vendredi', 'samedi'];
  var TEMP_COLORS = ['#c0392b', '#8e44ad', '#16a085', '#b7950b'];   // le gaz est orange
  var SEP = '────────────────────';

  // ---------------------------------------------------------------- outils
  function num(v) { var n = Number(v); return (v == null || !isFinite(n)) ? 0 : n; }
  function isNum(v) { return v != null && isFinite(Number(v)); }
  function fmt(v, d) { return num(v).toLocaleString('fr-FR', { minimumFractionDigits: d, maximumFractionDigits: d }); }
  function fmtQ(v) {
    var a = Math.abs(num(v));
    return num(v).toLocaleString('fr-FR', { maximumFractionDigits: a >= 100 ? 0 : (a >= 1 ? 2 : (a >= 0.01 || !a ? 3 : 4)) });
  }
  function fmtEur(v) { return fmt(v, 2) + ' €'; }
  function safeColor(c, def) { return /^#[0-9a-fA-F]{3,8}$/.test(String(c || '')) ? String(c) : def; }

  // Dates : objets Date a midi (pas de surprise aux changements d'heure)
  function ymdToDate(ymd) { ymd = Number(ymd); return new Date(Math.floor(ymd / 10000), Math.floor(ymd / 100) % 100 - 1, ymd % 100, 12); }
  function dateToYmd(d) { return d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate(); }
  function addDays(d, n) { var r = new Date(d.getTime()); r.setDate(r.getDate() + n); r.setHours(12, 0, 0, 0); return r; }
  function addMonths(d, n) { var r = new Date(d.getFullYear(), d.getMonth() + n, 1, 12); return r; }
  function today() { var d = new Date(); d.setHours(12, 0, 0, 0); return d; }
  function dd(d) { return ('0' + d.getDate()).slice(-2) + '/' + ('0' + (d.getMonth() + 1)).slice(-2); }
  function ddmmyyyy(d) { return dd(d) + '/' + d.getFullYear(); }
  function isoDate(d) { return d.getFullYear() + '-' + ('0' + (d.getMonth() + 1)).slice(-2) + '-' + ('0' + d.getDate()).slice(-2); }
  function mondayOf(d) { return addDays(d, -((d.getDay() + 6) % 7)); }
  function isoWeek(d) {
    var t = new Date(d.getTime());
    t.setDate(t.getDate() + 3 - (t.getDay() + 6) % 7);
    var w1 = new Date(t.getFullYear(), 0, 4, 12);
    return 1 + Math.round(((t - w1) / 86400000 - 3 + (w1.getDay() + 6) % 7) / 7);
  }
  function daysBetween(a, b) { return Math.round((b - a) / 86400000); }

  // ---------------------------------------------------------------- etat
  var S = null;                // etat de la page
  var gen = 0;                 // generation des requetes (reponses perimees ignorees)

  function newState() {
    var p = A.params() || {};
    var g = GRANS.some(function(x) { return x.id === p.gran; }) ? p.gran : 'hour';
    var anchor = /^\d{8}$/.test(p.date || '') ? ymdToDate(p.date) : today();
    if (anchor > today()) anchor = today();
    return {
      gran: g, anchor: anchor,
      custom: null,                          // {from: Date, to: Date} : periode libre
      mode: 'kwh', stacked: true, compare: 'none',
      avail: { hour: [], day: [] }, first: null,
      cfg: {}, data: null, cmp: null, water: null, waterCmp: null,
      calMonth: null
    };
  }

  // Periode affichee [from, to] (Date) pour la granularite et l'ancre
  function range() {
    if (S.custom) return { from: S.custom.from, to: S.custom.to };
    var a = S.anchor;
    switch (S.gran) {
    case 'hour':  return { from: a, to: a };
    case 'day':   return { from: new Date(a.getFullYear(), a.getMonth(), 1, 12), to: new Date(a.getFullYear(), a.getMonth() + 1, 0, 12) };
    case 'week': { var m = mondayOf(a); return { from: addDays(m, -7 * 11), to: addDays(m, 6) }; }
    case 'month': return { from: new Date(a.getFullYear(), 0, 1, 12), to: new Date(a.getFullYear(), 11, 31, 12) };
    default:      return { from: new Date(a.getFullYear() - 9, 0, 1, 12), to: new Date(a.getFullYear(), 11, 31, 12) };
    }
  }

  // Periode decalee d'un pas (dir = -1 / +1)
  function shifted(r, dir) {
    if (S.custom) {
      var len = daysBetween(r.from, r.to) + 1;
      return { from: addDays(r.from, dir * len), to: addDays(r.to, dir * len) };
    }
    switch (S.gran) {
    case 'hour':  return { from: addDays(r.from, dir), to: addDays(r.to, dir) };
    case 'day':   var f = addMonths(r.from, dir); return { from: f, to: new Date(f.getFullYear(), f.getMonth() + 1, 0, 12) };
    case 'week':  return { from: addDays(r.from, dir * 84), to: addDays(r.to, dir * 84) };
    case 'month': return { from: new Date(r.from.getFullYear() + dir, 0, 1, 12), to: new Date(r.to.getFullYear() + dir, 11, 31, 12) };
    default:      return { from: new Date(r.from.getFullYear() + dir * 10, 0, 1, 12), to: new Date(r.to.getFullYear() + dir * 10, 11, 31, 12) };
    }
  }

  // Periode de comparaison : precedente, ou la meme un an plus tot
  function compareRange(r) {
    if (S.compare === 'prev') return shifted(r, -1);
    if (S.compare === 'year') {
      var f = new Date(r.from.getTime()), t = new Date(r.to.getTime());
      f.setFullYear(f.getFullYear() - 1);
      t.setFullYear(t.getFullYear() - 1);
      if (S.gran === 'week') { f = mondayOf(f); t = addDays(f, daysBetween(r.from, r.to)); }
      return { from: f, to: t };
    }
    return null;
  }

  function periodTitle(r) {
    var g = S.gran, f = r.from, t = r.to;
    if (S.custom) return 'Du ' + ddmmyyyy(f) + ' au ' + ddmmyyyy(t);
    if (g === 'hour') {
      var s = DAYS[f.getDay()] + ' ' + f.getDate() + ' ' + MONTHS[f.getMonth()] + ' ' + f.getFullYear();
      return s.charAt(0).toUpperCase() + s.slice(1);
    }
    if (g === 'day') { var m = MONTHS[f.getMonth()]; return m.charAt(0).toUpperCase() + m.slice(1) + ' ' + f.getFullYear(); }
    if (g === 'week') return 'Semaines ' + isoWeek(f) + ' à ' + isoWeek(t) + ' (' + dd(f) + ' – ' + ddmmyyyy(t) + ')';
    if (g === 'month') return String(f.getFullYear());
    return f.getFullYear() + ' – ' + t.getFullYear();
  }

  // Libelle d'un point a partir de sa cle serveur
  function pointLabel(key, multiDay) {
    var k = String(key);
    if (S.gran === 'hour') {
      var h = k.slice(11, 13) + 'h';
      return multiDay ? k.slice(8, 10) + '/' + k.slice(5, 7) + ' ' + h : h;
    }
    if (S.gran === 'day') return k.slice(8, 10) + '/' + k.slice(5, 7);
    if (S.gran === 'week') { var d = ymdToDate(k.replace(/-/g, '')); return 'S' + isoWeek(d) + ' (' + dd(d) + ')'; }
    if (S.gran === 'month') return MONTHS_S[Number(k.slice(5, 7)) - 1] + ' ' + k.slice(0, 4);
    return k;
  }

  // ---------------------------------------------------------------- disponibilite
  function availList() { return S.gran === 'hour' ? S.avail.hour : S.avail.day; }
  function isAvail(ymd) {
    var l = availList();
    for (var i = 0; i < l.length; i++) if (ymd >= l[i][0] && ymd <= l[i][1]) return true;
    return false;
  }
  function firstAvail() {
    var l = S.avail.day.concat(S.avail.hour), m = null;
    l.forEach(function(r) { if (m == null || r[0] < m) m = r[0]; });
    return m;
  }

  // ---------------------------------------------------------------- page
  function page() {
    gen++;
    S = newState();
    A.setContent(
      '<div class="container-fluid" id="hist-page" style="max-width:1400px;">' +
        '<div class="d-flex flex-wrap align-items-center gap-2" style="margin:1rem 0;">' +
          '<h3 class="mb-0 me-auto">Historique de consommation</h3>' +
          '<a href="#energy" class="btn btn-sm btn-outline-secondary">&larr; &Eacute;nergie</a>' +
        '</div>' +
        // Barre d'outils
        '<div class="card p-3 mb-3"><div class="d-flex flex-wrap align-items-center gap-2">' +
          '<div class="btn-group btn-group-sm" role="group" id="hist-gran">' +
            GRANS.map(function(g) { return '<button type="button" class="btn btn-outline-primary" data-g="' + g.id + '">' + g.label + '</button>'; }).join('') +
          '</div>' +
          '<div class="d-flex align-items-center gap-1 position-relative">' +
            '<button type="button" class="btn btn-sm btn-outline-secondary" id="hist-prev" title="Période précédente">&lsaquo;</button>' +
            '<button type="button" class="btn btn-sm btn-outline-secondary" id="hist-cal-btn" title="Choisir une date">&#128197; <span id="hist-title"></span></button>' +
            '<button type="button" class="btn btn-sm btn-outline-secondary" id="hist-next" title="Période suivante">&rsaquo;</button>' +
            '<button type="button" class="btn btn-sm btn-outline-secondary" id="hist-today">Aujourd&#39;hui</button>' +
            '<div id="hist-cal" class="card shadow p-2" style="display:none;position:absolute;top:100%;left:0;z-index:1050;width:290px;"></div>' +
          '</div>' +
          '<details id="hist-custom-box" class="ms-1"><summary class="small text-primary" style="cursor:pointer;">P&eacute;riode libre</summary>' +
            '<div class="d-flex flex-wrap align-items-center gap-1 mt-1">' +
              '<input type="date" class="form-control form-control-sm" id="hist-from" style="width:auto;">' +
              '<span class="small">au</span>' +
              '<input type="date" class="form-control form-control-sm" id="hist-to" style="width:auto;">' +
              '<button type="button" class="btn btn-sm btn-primary" id="hist-custom-go">Afficher</button>' +
              '<button type="button" class="btn btn-sm btn-link" id="hist-custom-clear" style="display:none;">Annuler</button>' +
            '</div></details>' +
          '<div class="ms-lg-auto d-flex flex-wrap align-items-center gap-2">' +
            '<div class="btn-group btn-group-sm" role="group" id="hist-mode">' +
              '<button type="button" class="btn btn-outline-secondary" data-m="kwh">kWh</button>' +
              '<button type="button" class="btn btn-outline-secondary" data-m="eur">€</button>' +
            '</div>' +
            '<div class="btn-group btn-group-sm" role="group" id="hist-stack">' +
              '<button type="button" class="btn btn-outline-secondary" data-s="1">Empilé</button>' +
              '<button type="button" class="btn btn-outline-secondary" data-s="0">Séparé</button>' +
            '</div>' +
            '<select class="form-select form-select-sm" id="hist-compare" style="width:auto;">' +
              '<option value="none">Sans comparaison</option>' +
              '<option value="prev">Comparer à la période précédente</option>' +
              '<option value="year">Comparer à l&#39;année précédente</option>' +
            '</select>' +
          '</div>' +
        '</div></div>' +
        // Graphe electricite / production / gaz
        '<div class="card p-3 mb-3">' +
          '<div class="d-flex flex-wrap align-items-center gap-2 mb-2">' +
            '<h5 class="card-title mb-0 me-auto">&Eacute;lectricit&eacute;, production et gaz</h5>' +
            '<button type="button" class="btn btn-sm btn-outline-secondary" id="hist-zoom-reset" style="display:none;">Zoom initial</button>' +
            '<button type="button" class="btn btn-sm btn-outline-secondary" id="hist-csv">Export CSV</button>' +
          '</div>' +
          '<div id="hist-msg" class="text-muted small mb-2" style="display:none;"></div>' +
          '<div style="position:relative;height:420px;"><canvas id="hist-chart"></canvas></div>' +
          '<div class="form-text">Molette ou pincement : zoom ; glisser : s&eacute;lection d&#39;une plage ; Ctrl + glisser : d&eacute;placement. Clic sur la l&eacute;gende : masquer une s&eacute;rie.</div>' +
        '</div>' +
        // Statistiques
        '<div class="card p-3 mb-3"><h5 class="card-title">Statistiques de la p&eacute;riode</h5><div id="hist-stats" class="row g-2"></div></div>' +
        // Eau
        '<div class="card p-3 mb-3" id="hist-water-card" style="display:none;">' +
          '<h5 class="card-title">Eau</h5>' +
          '<div style="position:relative;height:300px;"><canvas id="hist-water"></canvas></div>' +
          '<div id="hist-water-stats" class="small mt-2"></div>' +
        '</div>' +
      '</div>');
    bind();
    A.loadScripts(A.CHART_LIBS).then(function() {
      var g0 = gen;
      return $.getJSON('/api/config/general').then(function(c) {
        if (g0 !== gen) return;
        S.cfg = c || {};
        return $.getJSON('/api/energy/history/days');
      }).then(function(d) {
        if (g0 !== gen || !d) return;
        S.avail = { hour: $.isArray(d.hour) ? d.hour : [], day: $.isArray(d.day) ? d.day : [] };
        S.first = firstAvail();
        refresh();
      });
    }).catch(function() {
      $('#hist-msg').text('Chargement impossible des graphiques.').show();
    });
  }

  function syncControls() {
    $('#hist-gran [data-g]').each(function() {
      var on = $(this).data('g') === S.gran;
      $(this).toggleClass('btn-primary', on).toggleClass('btn-outline-primary', !on);
    });
    function onOff($b, on) { $b.toggleClass('btn-secondary', on).toggleClass('btn-outline-secondary', !on); }
    $('#hist-mode [data-m]').each(function() { onOff($(this), $(this).data('m') === S.mode); });
    $('#hist-stack [data-s]').each(function() { onOff($(this), String($(this).data('s')) === (S.stacked ? '1' : '0')); });
    $('#hist-compare').val(S.compare);
    var r = range();
    $('#hist-title').text(periodTitle(r));
    var nx = shifted(r, 1), pv = shifted(r, -1);
    $('#hist-next').prop('disabled', nx.from > today());
    $('#hist-prev').prop('disabled', S.first != null && dateToYmd(pv.to) < S.first);
    $('#hist-custom-clear').toggle(!!S.custom);
    $('#hist-from').val(isoDate(r.from));
    $('#hist-to').val(isoDate(r.to));
    try {
      history.replaceState(null, '', '#energyHistory?gran=' + S.gran + '&date=' + dateToYmd(S.anchor));
    } catch (e) {}
  }

  function bind() {
    $('#hist-gran').on('click', '[data-g]', function() {
      S.gran = String($(this).data('g'));
      S.custom = null;
      refresh();
    });
    $('#hist-prev').on('click', function() { step(-1); });
    $('#hist-next').on('click', function() { step(1); });
    $('#hist-today').on('click', function() { S.anchor = today(); S.custom = null; refresh(); });
    $('#hist-mode').on('click', '[data-m]', function() { S.mode = String($(this).data('m')); render(); });
    $('#hist-stack').on('click', '[data-s]', function() { S.stacked = String($(this).data('s')) === '1'; render(); });
    $('#hist-compare').on('change', function() { S.compare = this.value; refresh(); });
    $('#hist-cal-btn').on('click', function(e) {
      e.stopPropagation();
      var $c = $('#hist-cal');
      if ($c.is(':visible')) { $c.hide(); return; }
      S.calMonth = new Date(S.anchor.getFullYear(), S.anchor.getMonth(), 1, 12);
      drawCalendar();
      $c.show();
    });
    $(document).off('click.histcal').on('click.histcal', function(e) {
      if (!$(e.target).closest('#hist-cal').length) $('#hist-cal').hide();
    });
    $('#hist-cal').on('click', '[data-cm]', function(e) {
      e.stopPropagation();
      S.calMonth = addMonths(S.calMonth, Number($(this).data('cm')));
      drawCalendar();
    });
    $('#hist-cal').on('click', '[data-day]', function(e) {
      e.stopPropagation();
      S.anchor = ymdToDate($(this).data('day'));
      S.custom = null;
      $('#hist-cal').hide();
      refresh();
    });
    $('#hist-custom-go').on('click', function() {
      var f = $('#hist-from').val(), t = $('#hist-to').val();
      if (!f || !t) return;
      var df = ymdToDate(f.replace(/-/g, '')), dt = ymdToDate(t.replace(/-/g, ''));
      if (dt < df) { var x = df; df = dt; dt = x; }
      if (dt > today()) dt = today();
      var max = MAX_DAYS[S.gran];
      if (daysBetween(df, dt) + 1 > max) {
        dt = addDays(df, max - 1);
        A.showAlert('Période réduite à ' + max + ' jours pour cette granularité.', 'warning');
      }
      S.custom = { from: df, to: dt };
      S.anchor = dt;
      refresh();
    });
    $('#hist-custom-clear').on('click', function() { S.custom = null; refresh(); });
    $('#hist-zoom-reset').on('click', function() {
      if (S.chart && S.chart.resetZoom) S.chart.resetZoom();
      $(this).hide();
    });
    $('#hist-csv').on('click', exportCsv);
  }

  function step(dir) {
    var r = shifted(range(), dir);
    if (S.custom) S.custom = r;
    S.anchor = r.to > today() ? today() : r.to;
    if (!S.custom && S.gran === 'hour') S.anchor = r.from;
    refresh();
  }

  // ---------------------------------------------------------------- calendrier
  function drawCalendar() {
    var m = S.calMonth, y = m.getFullYear(), mo = m.getMonth();
    var first = new Date(y, mo, 1, 12), last = new Date(y, mo + 1, 0, 12);
    var lead = (first.getDay() + 6) % 7, t = today(), sel = dateToYmd(S.anchor);
    var h = '<div class="d-flex align-items-center mb-1">' +
              '<button type="button" class="btn btn-sm btn-light" data-cm="-12" title="Année précédente">&laquo;</button>' +
              '<button type="button" class="btn btn-sm btn-light" data-cm="-1" title="Mois précédent">&lsaquo;</button>' +
              '<div class="flex-fill text-center fw-bold small">' + MONTHS[mo] + ' ' + y + '</div>' +
              '<button type="button" class="btn btn-sm btn-light" data-cm="1" title="Mois suivant">&rsaquo;</button>' +
              '<button type="button" class="btn btn-sm btn-light" data-cm="12" title="Année suivante">&raquo;</button>' +
            '</div><table class="table table-sm mb-1 text-center small" style="table-layout:fixed;"><thead><tr>' +
            ['L', 'M', 'M', 'J', 'V', 'S', 'D'].map(function(x) { return '<th class="p-1">' + x + '</th>'; }).join('') +
            '</tr></thead><tbody><tr>';
    for (var i = 0; i < lead; i++) h += '<td></td>';
    for (var d = 1; d <= last.getDate(); d++) {
      var dt = new Date(y, mo, d, 12), ymd = dateToYmd(dt);
      var ok = dt <= t && isAvail(ymd);
      var st = ymd === sel ? 'background:#0d6efd;color:#fff;border-radius:4px;' : '';
      if (ok) h += '<td class="p-1" style="cursor:pointer;' + st + '" data-day="' + ymd + '">' + d + '</td>';
      else h += '<td class="p-1" style="color:#c0c0c0;' + st + '" title="Pas de données">' + d + '</td>';
      if ((lead + d) % 7 === 0 && d < last.getDate()) h += '</tr><tr>';
    }
    h += '</tr></tbody></table><div class="small text-muted px-1">Jours gris&eacute;s : pas de donn&eacute;es ' +
         (S.gran === 'hour' ? 'heure par heure' : 'journali&egrave;res') + '.</div>';
    $('#hist-cal').html(h);
  }

  // ---------------------------------------------------------------- donnees
  function url(r, ieee) {
    return '/api/energy/history?gran=' + S.gran + '&from=' + dateToYmd(r.from) + '&to=' + dateToYmd(r.to) +
           (ieee ? '&ieee=' + encodeURIComponent(ieee) : '');
  }

  function refresh() {
    var g0 = ++gen;
    syncControls();
    $('#hist-msg').hide();
    var r = range(), cr = compareRange(r), water = String(S.cfg.Water || '');
    // Requetes l'une apres l'autre : le serveur du pont a peu de sockets
    var out = {};
    var steps = [function() { return $.getJSON(url(r)).then(function(d) { out.data = d; }); }];
    if (cr) steps.push(function() { return $.getJSON(url(cr)).then(function(d) { out.cmp = d; }); });
    if (water) {
      steps.push(function() { return $.getJSON(url(r, water)).then(function(d) { out.water = d; }); });
      if (cr) steps.push(function() { return $.getJSON(url(cr, water)).then(function(d) { out.waterCmp = d; }); });
    }
    steps.reduce(function(p, s) { return p.then(function() { if (g0 === gen) return s(); }); }, Promise.resolve())
      .then(function() {
        if (g0 !== gen || !document.getElementById('hist-page')) return;
        S.data = out.data || null;
        S.cmp = out.cmp || null;
        S.water = out.water || null;
        S.waterCmp = out.waterCmp || null;
        render();
      }, function() {
        if (g0 !== gen) return;
        $('#hist-msg').text('Erreur de chargement des données.').show();
      });
  }

  // Series electriques du graphe (production en negatif), valeurs en kWh ou €
  function buildSets(d, mode, forTotal) {
    var c = d.costs || {}, cspe = num(c.cspe), out = [];
    (d.series || []).forEach(function(s, i) {
      if (!s || !$.isArray(s.data)) return;
      var prod = s.type === 'prod';
      var data = s.data.map(function(v, j) {
        if (!isNum(v)) return null;
        var kwh = Math.abs(Number(v)) / 1000;
        var val = kwh;
        if (mode === 'eur') {
          var pr = (s.prices && isNum(s.prices[j])) ? Number(s.prices[j]) : num(s.price);
          val = prod ? kwh * num(s.price || c.prodPrice) : kwh * (pr + cspe);
        }
        return prod ? -val : val;
      });
      out.push({ key: s.key, name: String(s.name || s.key), type: s.type, color: safeColor(s.color, '#2980b9'), data: data, raw: s });
    });
    // Gaz : dans la pile si son unite est une energie, sinon sur son propre axe
    var g = d.gas;
    if (g && $.isArray(g.data)) {
      var wf = num(g.whFactor), gp = num(g.price);
      out.push({
        key: 'gas', name: String(g.name || 'Gaz'), type: 'gas', color: safeColor(g.color, '#e67e22'),
        energy: wf > 0, unit: String(g.unit || ''),
        data: g.data.map(function(v) {
          if (!isNum(v)) return null;
          if (mode === 'eur') return Number(v) * gp;
          return wf > 0 ? Number(v) * wf / 1000 : Number(v);
        })
      });
    }
    return out;
  }

  // Consommation totale d'un point (electricite + gaz energie), dans le mode courant
  function totalsOf(sets, n) {
    var t = [];
    for (var i = 0; i < n; i++) {
      var s = null;
      sets.forEach(function(x) {
        if (x.type === 'prod' || (x.type === 'gas' && !x.energy && S.mode !== 'eur')) return;
        var v = x.data[i];
        if (v != null) s = (s || 0) + v;
      });
      t.push(s);
    }
    return t;
  }

  var PRESENCE_PLUGIN = {
    id: 'histPresence',
    beforeDraw: function(c) {
      var p = c.lixeePresence, ca = c.chartArea;
      if (!p || !ca) return;
      var x = c.scales.x, ctx = c.ctx;
      if (!x) return;
      ctx.save();
      for (var i = 0; i < p.length; i++) {
        if (!p[i]) continue;
        var px = x.getPixelForValue(i);
        if (px < ca.left - 1 || px > ca.right + 1) continue;
        var bw = (ca.right - ca.left) / Math.max(1, (x.max - x.min + 1));
        ctx.fillStyle = 'rgba(76,175,80,0.15)';
        ctx.fillRect(px - bw / 2, ca.top, bw, ca.bottom - ca.top);
      }
      ctx.restore();
    }
  };

  function render() {
    syncControls();
    var d = S.data;
    if (!d) return;
    if (d.configured === false) {
      $('#hist-msg').html('S&eacute;lectionnez votre ZLinky dans <a href="#configEnergy">Config &Eacute;nergie</a>.').show();
      return;
    }
    var keys = $.isArray(d.keys) ? d.keys : [], n = keys.length;
    var multiDay = S.gran === 'hour' && n > 24;
    var labels = keys.map(function(k) { return pointLabel(k, multiDay); });
    var eur = S.mode === 'eur';
    var unit = eur ? '€' : 'kWh';
    var sets = buildSets(d, S.mode);
    var hasData = sets.some(function(s) { return s.data.some(function(v) { return v; }); });
    if (!hasData) $('#hist-msg').text('Aucune consommation enregistrée sur cette période.').show();

    var datasets = [];
    var gasAxis = false;
    sets.forEach(function(s) {
      if (!s.data.some(function(v) { return v; })) return;
      var own = s.type === 'gas' && !s.energy && !eur;
      if (own) gasAxis = true;
      datasets.push({
        type: own ? 'line' : 'bar',
        label: s.type === 'prod' ? s.name + ' (Prod)' : (s.type === 'gas' && !s.energy && !eur ? s.name + ' (' + s.unit + ')' : s.name),
        data: s.data, backgroundColor: s.color, borderColor: s.color,
        stack: own ? 'gas' : (S.stacked ? 'e' : s.key), yAxisID: own ? 'yG' : 'y',
        borderWidth: own ? 2 : 0, pointRadius: own ? 2 : 0, fill: false, order: own ? -1 : 1,
        lixee: { kind: s.type, unit: own ? s.unit : unit }
      });
    });

    // Comparaison : total de la periode de reference, en pointilles
    var cmpTotals = null;
    if (S.cmp && S.cmp.configured !== false) {
      var csets = buildSets(S.cmp, S.mode);
      cmpTotals = totalsOf(csets, $.isArray(S.cmp.keys) ? S.cmp.keys.length : 0);
      var ckeys = S.cmp.keys || [];
      var lbl = S.compare === 'year' ? 'Année précédente' : 'Période précédente';
      datasets.push({
        type: 'line', label: lbl + ' (total)', data: labels.map(function(_, i) { return i < cmpTotals.length ? cmpTotals[i] : null; }),
        borderColor: '#555', backgroundColor: '#555', borderDash: [6, 4], borderWidth: 2, pointRadius: 0,
        pointHoverRadius: 3, fill: false, stack: 'cmp', yAxisID: 'y', order: -2,
        lixee: { kind: 'cmp', unit: unit, keys: ckeys }
      });
    }

    // Temperature
    var tempOn = $.isArray(d.temp) && d.temp.length && d.tempEnabled !== false;
    if (tempOn) {
      d.temp.forEach(function(t, k) {
        var avg = $.isArray(t.avg) ? t.avg : [];
        if (!avg.some(isNum)) return;
        var c = TEMP_COLORS[k % TEMP_COLORS.length];
        datasets.push({
          type: 'line', label: String(t.name || t.ieee) + ' (°C)', data: avg.map(function(v) { return isNum(v) ? Number(v) : null; }),
          yAxisID: 'yT', stack: 'temp_' + k, borderColor: c, backgroundColor: c, borderWidth: 2,
          pointRadius: n > 200 ? 0 : 2, pointHoverRadius: 4, fill: false, tension: 0.3, order: -3, spanGaps: false,
          lixee: { kind: 'temp', min: t.min, max: t.max }
        });
      });
    }
    var pres = d.presence && d.presence.enabled !== false && $.isArray(d.presence.data) ? d.presence.data : null;

    if (S.chart) { try { S.chart.destroy(); } catch (e) {} S.chart = null; }
    var canvas = document.getElementById('hist-chart');
    if (!canvas || typeof Chart === 'undefined') return;
    var chart = new Chart(canvas.getContext('2d'), {
      type: 'bar',
      data: { labels: labels, datasets: datasets },
      options: {
        responsive: true, maintainAspectRatio: false, animation: false,
        interaction: { mode: 'index', intersect: false },
        datasets: { bar: { barPercentage: 0.9, categoryPercentage: S.stacked ? 0.95 : 0.85 } },
        scales: {
          x: { stacked: S.stacked, ticks: { maxRotation: 70, minRotation: 0, autoSkip: true, maxTicksLimit: 31 } },
          y: { stacked: S.stacked, beginAtZero: true, grace: '10%',
               ticks: { callback: function(v) { return fmtQ(v) + ' ' + unit; } } },
          yG: { display: gasAxis ? true : false, position: 'right', beginAtZero: true, grid: { drawOnChartArea: false },
                ticks: { callback: function(v) { return fmtQ(v) + ' ' + ((d.gas && d.gas.unit) || ''); } } },
          yT: { display: 'auto', position: 'right', beginAtZero: false, grace: '10%', grid: { drawOnChartArea: false },
                ticks: { callback: function(v) { return fmt(v, Math.round(v) === v ? 0 : 1) + ' °C'; } } }
        },
        plugins: {
          legend: { display: true, position: 'top' },
          zoom: {
            limits: { x: { min: 'original', max: 'original', minRange: 3 } },
            pan: { enabled: true, mode: 'x', modifierKey: 'ctrl' },
            zoom: { wheel: { enabled: true, speed: 0.1 }, pinch: { enabled: true }, mode: 'x', drag: { enabled: true },
                    onZoomComplete: function() { $('#hist-zoom-reset').show(); } }
          },
          tooltip: {
            filter: function(it) {
              var m = it.dataset.lixee || {};
              if (m.kind === 'temp' || m.kind === 'cmp') return it.parsed.y != null;
              return it.parsed.y != null && Number(it.parsed.y) !== 0;
            },
            callbacks: {
              title: function(items) { return items.length ? tooltipTitle(keys[items[0].dataIndex]) : ''; },
              label: function(ctx) { return tooltipLabel(ctx, unit); },
              footer: function(items) { return tooltipFooter(items, unit, pres); }
            }
          }
        }
      },
      plugins: [PRESENCE_PLUGIN]
    });
    chart.lixeePresence = (pres && S.gran === 'hour') ? pres.map(function(v) { return v === 1; }) : null;
    S.chart = chart;
    $('#hist-zoom-reset').hide();

    renderStats(d, sets, cmpTotals);
    renderWater();
  }

  function tooltipTitle(key) {
    var k = String(key || '');
    if (S.gran === 'hour') { var dt = ymdToDate(k.slice(0, 10).replace(/-/g, '')); return DAYS[dt.getDay()] + ' ' + ddmmyyyy(dt) + ', ' + k.slice(11, 13) + 'h – ' + ('0' + ((Number(k.slice(11, 13)) + 1) % 24)).slice(-2) + 'h'; }
    if (S.gran === 'day') { var d2 = ymdToDate(k.replace(/-/g, '')); return DAYS[d2.getDay()] + ' ' + ddmmyyyy(d2); }
    if (S.gran === 'week') { var m = ymdToDate(k.replace(/-/g, '')); return 'Semaine ' + isoWeek(m) + ' : ' + dd(m) + ' – ' + ddmmyyyy(addDays(m, 6)); }
    if (S.gran === 'month') { var mm = MONTHS[Number(k.slice(5, 7)) - 1]; return mm.charAt(0).toUpperCase() + mm.slice(1) + ' ' + k.slice(0, 4); }
    return k;
  }

  function tooltipLabel(ctx, unit) {
    var m = ctx.dataset.lixee || {}, v = ctx.parsed.y;
    if (m.kind === 'temp') {
      var s = ctx.dataset.label.replace(' (°C)', '') + ' : ' + fmt(v, 1) + ' °C';
      var mn = $.isArray(m.min) ? m.min[ctx.dataIndex] : null, mx = $.isArray(m.max) ? m.max[ctx.dataIndex] : null;
      if (isNum(mn) && isNum(mx)) s += ' (min ' + fmt(mn, 1) + ' / max ' + fmt(mx, 1) + ')';
      return s;
    }
    if (m.kind === 'cmp') {
      var ck = $.isArray(m.keys) ? m.keys[ctx.dataIndex] : null;
      return ctx.dataset.label + (ck ? ' [' + tooltipTitle(ck) + ']' : '') + ' : ' + (unit === '€' ? fmtEur(v) : fmtQ(v) + ' ' + unit);
    }
    var u = m.unit || unit;
    return ctx.dataset.label + ' : ' + (u === '€' ? fmtEur(Math.abs(v)) : fmtQ(Math.abs(v)) + ' ' + u);
  }

  function tooltipFooter(items, unit, pres) {
    if (!items.length) return '';
    var i = items[0].dataIndex, cons = 0, prod = 0, any = false;
    items.forEach(function(it) {
      var m = it.dataset.lixee || {}, v = num(it.parsed.y);
      if (m.kind === 'temp' || m.kind === 'cmp' || (m.unit && m.unit !== unit)) return;
      any = true;
      if (v < 0) prod += -v; else cons += v;
    });
    var L = [];
    if (any) {
      L.push(SEP);
      var f = function(v) { return unit === '€' ? fmtEur(v) : fmtQ(v) + ' ' + unit; };
      L.push('Consommation : ' + f(cons));
      if (prod > 0) { L.push('Production : ' + f(prod)); L.push('Net : ' + f(cons - prod)); }
    }
    if (pres && pres[i] != null) {
      if (S.gran === 'hour') L.push(pres[i] ? 'Présence détectée' : 'Pas de présence');
      else L.push('Présence : ' + pres[i] + ' h');
    }
    return L;
  }

  // ---------------------------------------------------------------- statistiques
  function statCard(title, value, sub) {
    return '<div class="col-6 col-md-4 col-xl-3"><div class="border rounded p-2 h-100">' +
             '<div class="small text-muted">' + title + '</div>' +
             '<div class="fs-5 fw-bold">' + value + '</div>' +
             (sub ? '<div class="small text-muted">' + sub + '</div>' : '') +
           '</div></div>';
  }

  function sumArr(a) { var s = 0; a.forEach(function(v) { if (v != null) s += v; }); return s; }

  function renderStats(d, setsMode, cmpTotals) {
    var keys = d.keys || [], n = keys.length, c = d.costs || {};
    var kwhSets = buildSets(d, 'kwh'), eurSets = buildSets(d, 'eur');
    var elec = kwhSets.filter(function(s) { return s.type === 'index' || s.type === 'submeter'; });
    var consK = [], consE = [], elapsed = 0;
    for (var i = 0; i < n; i++) {
      var k = null, e = null;
      elec.forEach(function(s, j) {
        var v = s.data[i];
        if (v == null) return;
        k = (k || 0) + v;
        var ev = eurSets.filter(function(x) { return x.key === s.key; })[0];
        e = (e || 0) + (ev ? num(ev.data[i]) : 0);
      });
      if (k != null) elapsed++;
      consK.push(k);
      consE.push(e);
    }
    var totK = sumArr(consK), totE = sumArr(consE);
    // Abonnement et CTA : part de chaque point ecoule
    var fixed = (num(c.abo) + num(c.cta)) * num(d.periodFactor) * elapsed;
    var H = [];
    var unitPt = { hour: 'heure', day: 'jour', week: 'semaine', month: 'mois', year: 'an' }[S.gran];
    H.push(statCard('Consommation électrique', fmtQ(totK) + ' kWh',
                    elapsed ? 'Moyenne : ' + fmtQ(totK / elapsed) + ' kWh / ' + unitPt : ''));
    if (totE > 0 || fixed > 0) H.push(statCard('Coût électricité', fmtEur(totE + fixed),
                    'Énergie et taxes ' + fmtEur(totE) + (fixed > 0 ? ' + abonnement ' + fmtEur(fixed) : '')));
    // Pic
    var pk = -1;
    consK.forEach(function(v, j) { if (v != null && (pk < 0 || v > consK[pk])) pk = j; });
    if (pk >= 0 && consK[pk] > 0) H.push(statCard('Pic de consommation', fmtQ(consK[pk]) + ' kWh', tooltipTitle(keys[pk])));
    // Talon : moyenne des minima horaires de la nuit (0 h - 6 h), en W
    if (S.gran === 'hour') {
      var mins = [];
      for (var day = 0; day * 24 < n; day++) {
        var m = null;
        for (var h = 0; h < 6; h++) {
          var v = consK[day * 24 + h];
          if (v != null && (m == null || v < m)) m = v;
        }
        if (m != null) mins.push(m);
      }
      if (mins.length) H.push(statCard('Consommation de fond', fmt(sumArr(mins) / mins.length * 1000, 0) + ' W',
                                       'Plus faible heure de la nuit (0 h – 6 h)'));
    }
    // Heures creuses / pleines (noms des index)
    var hc = 0, hp = 0;
    elec.forEach(function(s) {
      var nm = String(s.name).toUpperCase(), t = sumArr(s.data);
      if (s.type !== 'index') return;
      if (nm.indexOf('HC') === 0 || nm === 'HN') hc += t;
      else if (nm.indexOf('HP') === 0) hp += t;
    });
    if (hc > 0 && hp > 0) H.push(statCard('Heures creuses', fmt(hc / (hc + hp) * 100, 0) + ' %',
                                          'HC ' + fmtQ(hc) + ' kWh · HP ' + fmtQ(hp) + ' kWh'));
    // Production
    var prod = kwhSets.filter(function(s) { return s.type === 'prod'; })[0];
    if (prod) {
      var pk2 = -sumArr(prod.data), pe = eurSets.filter(function(s) { return s.type === 'prod'; })[0];
      H.push(statCard('Production', fmtQ(pk2) + ' kWh', pe && sumArr(pe.data) ? 'Revenu ' + fmtEur(-sumArr(pe.data)) : ''));
    }
    // Gaz
    var gas = d.gas;
    if (gas && $.isArray(gas.data)) {
      var gt = sumArr(gas.data.map(function(v) { return isNum(v) ? Number(v) : null; }));
      H.push(statCard('Gaz', fmtQ(gt) + ' ' + esc(gas.unit || ''), num(gas.price) ? 'Coût ' + fmtEur(gt * num(gas.price)) : ''));
    }
    // Comparaison
    if (cmpTotals) {
      var curT = sumArr(totalsOf(setsMode, n)), refT = sumArr(cmpTotals);
      if (refT > 0) {
        var pct = (curT - refT) / refT * 100;
        var col = pct > 0 ? '#c0392b' : '#27ae60';
        H.push(statCard(S.compare === 'year' ? 'Écart sur un an' : 'Écart avec la période précédente',
                        '<span style="color:' + col + ';">' + (pct > 0 ? '+' : '') + fmt(pct, 1) + ' %</span>',
                        (S.mode === 'eur' ? fmtEur(curT) + ' contre ' + fmtEur(refT) : fmtQ(curT) + ' contre ' + fmtQ(refT) + ' kWh') +
                        (elapsed < n ? ' (période en cours)' : '')));
      }
    }
    // Temperature
    if ($.isArray(d.temp)) {
      d.temp.forEach(function(t) {
        var a = (t.avg || []).filter(isNum).map(Number);
        if (!a.length) return;
        var mn = ($.isArray(t.min) ? t.min : t.avg).filter(isNum).map(Number), mx = ($.isArray(t.max) ? t.max : t.avg).filter(isNum).map(Number);
        H.push(statCard('Température ' + esc(t.name || t.ieee), fmt(sumArr(a) / a.length, 1) + ' °C',
                        'min ' + fmt(Math.min.apply(null, mn), 1) + ' / max ' + fmt(Math.max.apply(null, mx), 1) + ' °C'));
      });
    }
    // Presence
    if (d.presence && $.isArray(d.presence.data)) {
      var ph = sumArr(d.presence.data.map(function(v) { return isNum(v) ? Number(v) : null; }));
      H.push(statCard('Présence', fmt(ph, 0) + ' h', 'Heures avec une présence détectée'));
    }
    $('#hist-stats').html(H.join('') || '<div class="col-12 text-muted">Aucune donnée.</div>');
  }

  // ---------------------------------------------------------------- eau
  function renderWater() {
    var w = S.water, $card = $('#hist-water-card');
    if (S.waterChart) { try { S.waterChart.destroy(); } catch (e) {} S.waterChart = null; }
    if (!S.cfg.Water || !w || w.configured === false) { $card.hide(); return; }
    $card.show();
    if (!$.isArray(w.series) || !w.series.length) {
      $('#hist-water').hide();
      $('#hist-water-stats').text('Aucune consommation d’eau enregistrée sur cette période.');
      return;
    }
    $('#hist-water').show();
    var coeff = num(w.coeff) || 1, unit = String(w.displayUnit || 'L'), price = num(w.meterPrice);
    var eur = S.mode === 'eur';
    var s = w.series[0];
    var data = (s.data || []).map(function(v) { return isNum(v) ? Number(v) * coeff * (eur ? price : 1) : null; });
    var u = eur ? '€' : unit;
    var multiDay = S.gran === 'hour' && (w.keys || []).length > 24;
    var labels = (w.keys || []).map(function(k) { return pointLabel(k, multiDay); });
    var ds = [{ type: 'bar', label: 'Eau', data: data, backgroundColor: safeColor(s.color, '#2e86c1') }];
    if (S.waterCmp && $.isArray(S.waterCmp.series) && S.waterCmp.series[0]) {
      var cs = S.waterCmp.series[0];
      ds.push({ type: 'line', label: (S.compare === 'year' ? 'Année précédente' : 'Période précédente'),
                data: labels.map(function(_, i) { var v = (cs.data || [])[i]; return isNum(v) ? Number(v) * coeff * (eur ? price : 1) : null; }),
                borderColor: '#555', borderDash: [6, 4], borderWidth: 2, pointRadius: 0, fill: false });
    }
    var canvas = document.getElementById('hist-water');
    S.waterChart = new Chart(canvas.getContext('2d'), {
      type: 'bar', data: { labels: labels, datasets: ds },
      options: {
        responsive: true, maintainAspectRatio: false, animation: false,
        interaction: { mode: 'index', intersect: false },
        scales: { x: { ticks: { maxRotation: 70, autoSkip: true, maxTicksLimit: 31 } },
                  y: { beginAtZero: true, ticks: { callback: function(v) { return fmtQ(v) + ' ' + u; } } } },
        plugins: { tooltip: { callbacks: {
          title: function(items) { return items.length ? tooltipTitle((w.keys || [])[items[0].dataIndex]) : ''; },
          label: function(ctx) { return ctx.dataset.label + ' : ' + (eur ? fmtEur(ctx.parsed.y) : fmtQ(ctx.parsed.y) + ' ' + unit); } } } }
      }
    });
    var tot = sumArr((s.data || []).map(function(v) { return isNum(v) ? Number(v) * coeff : null; }));
    $('#hist-water-stats').html('Total : <b>' + fmtQ(tot) + ' ' + esc(unit) + '</b>' + (price ? ' (' + fmtEur(tot * price) + ')' : ''));
  }

  // ---------------------------------------------------------------- export CSV
  function csvNum(v, d) { return v == null ? '' : Number(v).toFixed(d).replace('.', ','); }

  function exportCsv() {
    var d = S.data;
    if (!d || !$.isArray(d.keys)) return;
    var sets = buildSets(d, 'kwh');
    var head = ['Date'];
    sets.forEach(function(s) { head.push(s.type === 'gas' && !s.energy ? s.name + ' (' + s.unit + ')' : s.name + (s.type === 'prod' ? ' (Prod)' : '') + ' (kWh)'); });
    var temps = $.isArray(d.temp) ? d.temp : [];
    temps.forEach(function(t) { head.push(String(t.name || t.ieee) + ' (°C)'); });
    var pres = d.presence && $.isArray(d.presence.data) ? d.presence.data : null;
    if (pres) head.push(S.gran === 'hour' ? 'Présence' : 'Présence (h)');
    var w = S.water && $.isArray(S.water.series) && S.water.series[0] ? S.water : null;
    if (w) head.push('Eau (' + (w.displayUnit || '') + ')');
    var lines = [head.join(';')];
    d.keys.forEach(function(k, i) {
      var row = [k];
      sets.forEach(function(s) { var v = s.data[i]; row.push(v == null ? '' : csvNum(Math.abs(v), 3)); });
      temps.forEach(function(t) { row.push(csvNum(isNum((t.avg || [])[i]) ? t.avg[i] : null, 1)); });
      if (pres) row.push(pres[i] == null ? '' : String(pres[i]));
      if (w) { var wv = (w.series[0].data || [])[i]; row.push(isNum(wv) ? csvNum(Number(wv) * (num(w.coeff) || 1), 3) : ''); }
      lines.push(row.join(';'));
    });
    var blob = new Blob(['﻿' + lines.join('\r\n')], { type: 'text/csv;charset=utf-8' });
    var a = document.createElement('a');
    var r = range();
    a.href = URL.createObjectURL(blob);
    a.download = 'historique_' + S.gran + '_' + dateToYmd(r.from) + '_' + dateToYmd(r.to) + '.csv';
    document.body.appendChild(a);
    a.click();
    setTimeout(function() { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
  }

  A.registerPage('energyHistory', page);
})();
