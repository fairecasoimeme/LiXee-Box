// ============================================================================
// LiXee MatterBridge - Historique energie (#energyHistory), bouton du tableau de bord Energie
//
// Historique detaille de la consommation : archive datee de la box (13 mois
// heure par heure, jours, semaines, mois, annees), navigation par fleches et
// calendrier (jours sans donnees grises), periode libre, comparaison, kWh/€,
// empile/separe, temperature et presence, statistiques et export CSV.
// Electricite (et production) sur un graphe, avec le gaz s'il est compte en
// Wh / kWh (meme unite, empilable) ; sinon (m3...) le gaz a son graphe. Eau a part.
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
  var TEMP_COLORS = ['#c0392b', '#8e44ad', '#16a085', '#b7950b'];
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

  // ---------------------------------------------------------------- mobile
  // Ecran etroit : barre d'outils compacte, titre court, graphe plus bas, pas
  // de zoom (le glisser du zoom bloquait le defilement de la page) mais un
  // glissement du doigt sur le graphe pour changer de periode
  function isMobile() { return window.matchMedia ? window.matchMedia('(max-width: 767.98px)').matches : window.innerWidth < 768; }
  function isTouch() { return ('ontouchstart' in window) || navigator.maxTouchPoints > 0; }

  var CSS =
    '#hist-cal-btn{white-space:nowrap;overflow:hidden;text-overflow:ellipsis;min-width:0;}' +
    '#hist-chart-wrap{position:relative;height:420px;transition:opacity .15s;}' +
    '@media (max-width:767.98px){' +
      '#hist-page{padding-left:6px;padding-right:6px;}' +
      '#hist-page .hist-head{margin:.6rem 0 !important;}' +
      '#hist-page .hist-head h3{font-size:1.2rem;}' +
      '#hist-page .hist-card{padding:.6rem !important;margin-bottom:.6rem !important;}' +
      '#hist-page .hist-card h5{font-size:1rem;}' +
      '#hist-gran{width:100%;display:flex;}' +
      '#hist-gran .btn{flex:1 1 0;padding:.35rem 0;font-size:.82rem;}' +
      '#hist-page .hist-nav{width:100%;}' +
      '#hist-cal-btn{flex:1 1 auto;}' +
      '#hist-prev,#hist-next{padding:.2rem .75rem;font-size:1.15rem;line-height:1.2;}' +
      '#hist-page .hist-opts{width:100%;margin-left:0 !important;}' +
      '#hist-compare{flex:1 1 100%;min-width:0;}' +
      '#hist-chart-wrap{height:300px;}' +
      '#hist-gas-wrap,#hist-water-wrap{height:220px !important;}' +
      '#hist-stats .fs-5{font-size:1.05rem !important;}' +
      '#hist-stats .border{padding:.4rem !important;}' +
      '#hist-cal{left:50% !important;transform:translateX(-50%);max-width:calc(100vw - 20px);}' +
    '}';
  function injectCss() {
    if (document.getElementById('hist-style')) return;
    var st = document.createElement('style');
    st.id = 'hist-style';
    st.textContent = CSS;
    document.head.appendChild(st);
  }

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
      cfg: {}, data: null, cmp: null, water: null, waterCmp: null, gasChart: null, waterChart: null,
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
    if (isMobile()) {
      if (S.custom) return dd(f) + ' – ' + ddmmyyyy(t);
      if (g === 'hour') return DAYS[f.getDay()].slice(0, 3) + '. ' + f.getDate() + ' ' + MONTHS_S[f.getMonth()] + ' ' + f.getFullYear();
      if (g === 'week') return 'S' + isoWeek(f) + ' – S' + isoWeek(t) + ' ' + t.getFullYear();
    }
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
    S.mobile = isMobile();
    injectCss();
    A.setContent(
      '<div class="container-fluid" id="hist-page" style="max-width:1400px;">' +
        '<div class="d-flex align-items-center gap-2 hist-head" style="margin:1rem 0;">' +
          '<h3 class="mb-0 me-auto">Historique de consommation</h3>' +
          '<a href="#energy" class="btn btn-sm btn-outline-dark">&larr; &Eacute;nergie</a>' +
        '</div>' +
        // Barre d'outils
        '<div class="card p-3 mb-3 hist-card"><div class="d-flex flex-wrap align-items-center gap-2">' +
          '<div class="btn-group btn-group-sm" role="group" id="hist-gran">' +
            GRANS.map(function(g) { return '<button type="button" class="btn btn-outline-dark" data-g="' + g.id + '">' + g.label + '</button>'; }).join('') +
          '</div>' +
          '<div class="d-flex align-items-center gap-1 position-relative hist-nav">' +
            '<button type="button" class="btn btn-sm btn-outline-dark" id="hist-prev" title="Période précédente">&lsaquo;</button>' +
            '<button type="button" class="btn btn-sm btn-outline-dark" id="hist-cal-btn" title="Choisir une date">&#128197; <span id="hist-title"></span></button>' +
            '<button type="button" class="btn btn-sm btn-outline-dark" id="hist-next" title="Période suivante">&rsaquo;</button>' +
            '<button type="button" class="btn btn-sm btn-outline-dark" id="hist-today" title="Aujourd&#39;hui">' +
              '<span class="d-none d-md-inline">Aujourd&#39;hui</span><span class="d-md-none">Auj.</span></button>' +
            '<div id="hist-cal" class="card shadow p-2" style="display:none;position:absolute;top:100%;left:0;z-index:1050;width:290px;"></div>' +
          '</div>' +
          '<details id="hist-custom-box" class="ms-1"><summary class="small text-dark" style="cursor:pointer;">P&eacute;riode libre</summary>' +
            '<div class="d-flex flex-wrap align-items-center gap-1 mt-1">' +
              '<input type="date" class="form-control form-control-sm" id="hist-from" style="width:auto;">' +
              '<span class="small">au</span>' +
              '<input type="date" class="form-control form-control-sm" id="hist-to" style="width:auto;">' +
              '<button type="button" class="btn btn-sm btn-dark" id="hist-custom-go">Afficher</button>' +
              '<button type="button" class="btn btn-sm btn-link" id="hist-custom-clear" style="display:none;">Annuler</button>' +
            '</div></details>' +
          '<div class="ms-lg-auto d-flex flex-wrap align-items-center gap-2 hist-opts">' +
            '<div class="btn-group btn-group-sm" role="group" id="hist-mode">' +
              '<button type="button" class="btn btn-outline-dark" data-m="kwh">kWh</button>' +
              '<button type="button" class="btn btn-outline-dark" data-m="eur">€</button>' +
            '</div>' +
            '<div class="btn-group btn-group-sm" role="group" id="hist-stack">' +
              '<button type="button" class="btn btn-outline-dark" data-s="1">Empilé</button>' +
              '<button type="button" class="btn btn-outline-dark" data-s="0">Séparé</button>' +
            '</div>' +
            '<select class="form-select form-select-sm" id="hist-compare" style="width:auto;">' +
              '<option value="none">Sans comparaison</option>' +
              '<option value="prev">' + (S.mobile ? 'Comparer : période préc.' : 'Comparer à la période précédente') + '</option>' +
              '<option value="year">' + (S.mobile ? 'Comparer : année préc.' : 'Comparer à l&#39;année précédente') + '</option>' +
            '</select>' +
          '</div>' +
        '</div></div>' +
        // Graphe electricite / production
        '<div class="card p-3 mb-3 hist-card">' +
          '<div class="d-flex flex-wrap align-items-center gap-2 mb-2">' +
            '<h5 class="card-title mb-0 me-auto" id="hist-elec-title">&Eacute;lectricit&eacute;</h5>' +
            '<button type="button" class="btn btn-sm btn-outline-secondary" id="hist-zoom-reset" style="display:none;">Zoom initial</button>' +
            '<button type="button" class="btn btn-sm btn-outline-secondary" id="hist-csv">Export CSV</button>' +
          '</div>' +
          '<div id="hist-msg" class="text-muted small mb-2" style="display:none;"></div>' +
          '<div id="hist-chart-wrap"><canvas id="hist-chart"></canvas></div>' +
          '<div class="form-text d-none d-md-block">Molette : zoom ; glisser : s&eacute;lection d&#39;une plage ; Ctrl + glisser : d&eacute;placement. Clic sur la l&eacute;gende : masquer une s&eacute;rie.</div>' +
          '<div class="form-text d-md-none">Glissez le graphe vers la gauche ou la droite pour changer de p&eacute;riode ; touchez une barre pour le d&eacute;tail.</div>' +
        '</div>' +
        // Statistiques
        '<div class="card p-3 mb-3 hist-card"><h5 class="card-title">Statistiques de la p&eacute;riode</h5><div id="hist-stats" class="row g-2"></div></div>' +
        // Gaz
        '<div class="card p-3 mb-3 hist-card" id="hist-gas-card" style="display:none;">' +
          '<h5 class="card-title">Gaz</h5>' +
          '<div id="hist-gas-wrap" style="position:relative;height:300px;"><canvas id="hist-gas"></canvas></div>' +
          '<div id="hist-gas-stats" class="small mt-2"></div>' +
        '</div>' +
        // Eau
        '<div class="card p-3 mb-3 hist-card" id="hist-water-card" style="display:none;">' +
          '<h5 class="card-title">Eau</h5>' +
          '<div id="hist-water-wrap" style="position:relative;height:300px;"><canvas id="hist-water"></canvas></div>' +
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
      $(this).toggleClass('btn-dark', on).toggleClass('btn-outline-dark', !on);
    });
    function onOff($b, on) { $b.toggleClass('btn-dark', on).toggleClass('btn-outline-dark', !on); }
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

    // Glissement horizontal sur le graphe : periode precedente / suivante.
    // Ecouteurs passifs : le defilement vertical de la page reste libre.
    var wrap = document.getElementById('hist-chart-wrap'), sw = null;
    if (wrap) {
      wrap.addEventListener('touchstart', function(e) {
        sw = e.touches.length === 1 ? { x: e.touches[0].clientX, y: e.touches[0].clientY } : null;
      }, { passive: true });
      wrap.addEventListener('touchend', function(e) {
        if (!sw || !e.changedTouches.length) return;
        var dx = e.changedTouches[0].clientX - sw.x, dy = e.changedTouches[0].clientY - sw.y;
        sw = null;
        if (Math.abs(dx) < 60 || Math.abs(dx) < 1.5 * Math.abs(dy)) return;
        var dir = dx < 0 ? 1 : -1;
        if ($(dir > 0 ? '#hist-next' : '#hist-prev').prop('disabled')) return;
        $('#hist-chart-wrap').css('opacity', 0.35);
        step(dir);
      }, { passive: true });
    }

    // Rotation de l'ecran, fenetre redimensionnee : mise en page a refaire
    var rt = null;
    $(window).off('resize.hist').on('resize.hist', function() {
      clearTimeout(rt);
      rt = setTimeout(function() {
        if (!document.getElementById('hist-page')) { $(window).off('resize.hist'); return; }
        var m = isMobile();
        if (m === S.mobile) return;
        S.mobile = m;
        $('#hist-compare option[value="prev"]').text(m ? 'Comparer : période préc.' : 'Comparer à la période précédente');
        $('#hist-compare option[value="year"]').text(m ? 'Comparer : année préc.' : 'Comparer à l\u2019année précédente');
        render();
      }, 250);
    });
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

  // Series electriques du graphe (production en negatif), valeurs en kWh ou €.
  // Le gaz et l'eau ont chacun leur graphe (renderFluids).
  function buildSets(d, mode) {
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
    // Gaz compte en energie (Wh / kWh) : empile avec l'electricite, en kWh ;
    // tarif gaz en €/kWh (comme le serveur, energy_service)
    var g = d.gas;
    if (gasIsEnergy(g) && $.isArray(g.data)) {
      var wf = num(g.whFactor), gp = num(g.price);
      out.push({
        key: 'gas', name: String(g.name || 'Gaz'), type: 'gas', color: safeColor(g.color, '#e67e22'),
        data: g.data.map(function(v) {
          if (!isNum(v)) return null;
          var kwh = Number(v) * wf / 1000;
          return mode === 'eur' ? kwh * gp : kwh;
        })
      });
    }
    return out;
  }

  // Gaz en unite d'energie ? Reponse du serveur (whFactor), sinon config
  function gasIsEnergy(g) {
    if (g && g.whFactor != null) return num(g.whFactor) > 0;
    return /^k?wh$/i.test(String(S.cfg.unitGaz || '').trim());
  }

  // Consommation totale d'un point (electricite + gaz en energie), dans le mode courant
  function totalsOf(sets, n) {
    var t = [];
    for (var i = 0; i < n; i++) {
      var s = null;
      sets.forEach(function(x) {
        if (x.type === 'prod') return;
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
      if (!p || !ca || c.lixeePresenceHidden) return;
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
    $('#hist-chart-wrap').css('opacity', 1);
    var d = S.data;
    if (!d) return;
    var mob = isMobile(), small = { size: 10 };
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
    var tp = ['&Eacute;lectricit&eacute;'];
    if (sets.some(function(s) { return s.type === 'gas'; }) || (S.cfg.Gaz && gasIsEnergy(d.gas))) tp.push('gaz');
    if (sets.some(function(s) { return s.type === 'prod'; })) tp.push('production');
    $('#hist-elec-title').html(tp.length > 1 ? tp.slice(0, -1).join(', ') + ' et ' + tp[tp.length - 1] : tp[0]);

    var datasets = [];
    sets.forEach(function(s) {
      if (!s.data.some(function(v) { return v; })) return;
      datasets.push({
        type: 'bar', label: s.type === 'prod' ? s.name + ' (Prod)' : s.name,
        data: s.data, backgroundColor: s.color, borderColor: s.color,
        stack: S.stacked ? 'e' : s.key, yAxisID: 'y',
        borderWidth: 0, pointRadius: 0, fill: false, order: 1,
        lixee: { kind: s.type, unit: unit }
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

    // Lignes de reference : budget, talon et max de la consommation electrique
    refLines(d, sets, keys).forEach(function(r) {
      datasets.push({
        type: 'line', label: r.label, data: labels.map(function() { return r.value; }),
        borderColor: r.color, backgroundColor: r.color, borderDash: r.dash, borderWidth: 2,
        pointRadius: 0, pointHoverRadius: 0, fill: false, stack: 'ref_' + r.id, yAxisID: 'y', order: -2,
        lixee: { kind: 'ref' }
      });
    });

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
    var presMask = (pres && S.gran === 'hour') ? pres.map(function(v) { return v === 1; }) : null;

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
          x: { stacked: S.stacked,
               ticks: mob ? { maxRotation: 0, autoSkip: true, maxTicksLimit: 7, font: small }
                          : { maxRotation: 70, minRotation: 0, autoSkip: true, maxTicksLimit: 31 } },
          // Unite sur mobile : dans le titre de l'axe, pas sur chaque graduation
          y: { stacked: S.stacked, beginAtZero: true, grace: '10%',
               title: { display: mob, text: unit, font: small, padding: 0 },
               ticks: { font: mob ? small : undefined, callback: function(v) { return fmtQ(v) + (mob ? '' : ' ' + unit); } } },
          yT: { display: 'auto', position: 'right', beginAtZero: false, grace: '10%', grid: { drawOnChartArea: false },
                ticks: { font: mob ? small : undefined, callback: function(v) { return fmt(v, Math.round(v) === v ? 0 : 1) + (mob ? '°' : ' °C'); } } }
        },
        plugins: {
          legend: { display: true, position: 'top',
                    labels: $.extend(mob ? { boxWidth: 12, boxHeight: 10, padding: 6, font: { size: 11 } } : {}, {
                      // Entree "Presence detectee" pour la bande verte (vue heure)
                      generateLabels: function(c) {
                        var items = Chart.defaults.plugins.legend.labels.generateLabels(c);
                        if (presMask) {
                          items.push({ text: 'Présence détectée', fillStyle: 'rgba(76,175,80,0.35)',
                                       strokeStyle: 'rgba(76,175,80,0.8)', lineWidth: 1,
                                       hidden: !!c.lixeePresenceHidden, lixeePresence: true });
                        }
                        return items;
                      }
                    }),
                    onClick: function(e, item, legend) {
                      if (item && item.lixeePresence) {
                        legend.chart.lixeePresenceHidden = !legend.chart.lixeePresenceHidden;
                        legend.chart.update('none');
                        return;
                      }
                      Chart.defaults.plugins.legend.onClick.call(this, e, item, legend);
                    } },
          // Zoom : souris seulement (au doigt, le glisser bloquait le defilement)
          zoom: (mob || isTouch()) ? false : {
            limits: { x: { min: 'original', max: 'original', minRange: 3 } },
            pan: { enabled: true, mode: 'x', modifierKey: 'ctrl' },
            zoom: { wheel: { enabled: true, speed: 0.1 }, pinch: { enabled: false }, mode: 'x', drag: { enabled: true },
                    onZoomComplete: function() { $('#hist-zoom-reset').show(); } }
          },
          tooltip: {
            titleFont: mob ? { size: 11 } : undefined, bodyFont: mob ? { size: 11 } : undefined,
            footerFont: mob ? { size: 11 } : undefined, padding: mob ? 6 : 6, boxWidth: mob ? 9 : undefined,
            boxHeight: mob ? 9 : undefined,
            filter: function(it) {
              var m = it.dataset.lixee || {};
              if (m.kind === 'ref') return false;              // valeur dans la legende
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
    chart.lixeePresence = presMask;
    // Rendu initial fait sans le masque (sans animation, aucun autre trace) : on redessine
    if (presMask) chart.draw();
    S.chart = chart;
    $('#hist-zoom-reset').hide();

    renderStats(d, sets, cmpTotals);
    renderFluids();
  }

  /**
   * Lignes horizontales de la granularite affichee (heure, jour, semaine...) :
   *  - Budget : budget mensuel (Config Notifications) ramene au creneau, en €
   *    ou en kWh au prix moyen de la periode (comme le graphe d'usage) ;
   *  - Talon : creneau complet le plus faible (consommation de fond) ;
   *  - Max : creneau le plus fort.
   * Consommation electrique (index et sous-compteurs), creneau en cours exclu.
   */
  function refLines(d, sets, keys) {
    var out = [], eur = S.mode === 'eur', unit = eur ? '€' : 'kWh';
    var fv = function(v) { return eur ? fmtEur(v) : fmtQ(v) + ' kWh'; };
    var elec = sets.filter(function(s) { return s.type === 'index' || s.type === 'submeter'; });
    var tot = [];
    for (var i = 0; i < keys.length; i++) {
      var t = null;
      elec.forEach(function(s) { if (s.data[i] != null) t = (t || 0) + s.data[i]; });
      tot.push(t);
    }
    // Creneau en cours (dernier renseigne, periode incluant aujourd'hui) : incomplet
    var last = -1;
    tot.forEach(function(v, j) { if (v != null) last = j; });
    if (last >= 0 && range().to >= today()) tot[last] = null;
    var vals = tot.filter(function(v) { return v != null && v > 0; });
    var bud = num(d.budgetEur) * num(d.periodFactor);
    if (bud > 0 && !eur) {
      // Prix moyen du kWh (index) sur la periode, comme budgetWh du graphe d'usage
      var kwh = 0, cost = 0;
      (d.series || []).forEach(function(s) {
        if (s.type !== 'index' || !$.isArray(s.data)) return;
        s.data.forEach(function(v) { if (isNum(v)) { kwh += Number(v) / 1000; cost += Number(v) / 1000 * num(s.price); } });
      });
      bud = (kwh > 0 && cost > 0) ? bud * kwh / cost : 0;
    }
    if (bud > 0) out.push({ id: 'budget', label: 'Budget : ' + fv(bud), value: bud, color: '#8e44ad', dash: [6, 4] });
    if (vals.length >= 2) {
      var mn = Math.min.apply(null, vals), mx = Math.max.apply(null, vals);
      out.push({ id: 'talon', label: 'Talon : ' + fv(mn), value: mn, color: '#16a085', dash: [3, 3] });
      out.push({ id: 'max', label: 'Max : ' + fv(mx), value: mx, color: '#c0392b', dash: [3, 3] });
    }
    return out;
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
    var i = items[0].dataIndex, cons = 0, gas = 0, prod = 0, any = false, anyGas = false;
    items.forEach(function(it) {
      var m = it.dataset.lixee || {}, v = num(it.parsed.y);
      if (m.kind === 'temp' || m.kind === 'cmp' || m.kind === 'ref' || (m.unit && m.unit !== unit)) return;
      any = true;
      if (m.kind === 'gas') { gas += v; anyGas = true; }
      else if (v < 0) prod += -v; else cons += v;
    });
    var L = [];
    if (any) {
      L.push(SEP);
      var f = function(v) { return unit === '€' ? fmtEur(v) : fmtQ(v) + ' ' + unit; };
      if (anyGas) {
        L.push('Électricité : ' + f(cons));
        L.push('Gaz : ' + f(gas));
        L.push('Total : ' + f(cons + gas));
      } else {
        L.push('Consommation : ' + f(cons));
      }
      if (prod > 0) { L.push('Production : ' + f(prod)); L.push('Net : ' + f(cons + gas - prod)); }
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
    // Gaz empile (energie) ; en m3, il a son graphe et son total dessous
    var gk = kwhSets.filter(function(s) { return s.type === 'gas'; })[0];
    if (gk) {
      var ge = eurSets.filter(function(s) { return s.type === 'gas'; })[0];
      H.push(statCard('Consommation de gaz', fmtQ(sumArr(gk.data)) + ' kWh',
                      ge && sumArr(ge.data) ? 'Coût ' + fmtEur(sumArr(ge.data)) : ''));
    }
    // Comparaison : total du graphe (electricite et gaz empile)
    if (cmpTotals) {
      var curT = sumArr(totalsOf(setsMode, n)), refT = sumArr(cmpTotals);
      if (refT > 0) {
        var pct = (curT - refT) / refT * 100;
        var col = pct > 0 ? '#c0392b' : '#27ae60';
        H.push(statCard(S.compare === 'year' ? 'Écart sur un an' : 'Écart avec la période précédente',
                        '<span style="color:' + col + ';">' + (pct > 0 ? '+' : '') + fmt(pct, 1) + ' %</span>',
                        (S.mode === 'eur' ? fmtEur(curT) + ' contre ' + fmtEur(refT) : fmtQ(curT) + ' contre ' + fmtQ(refT) + ' kWh') +
                        (gk ? ', électricité et gaz' : '') + (elapsed < n ? ' (période en cours)' : '')));
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

  // ---------------------------------------------------------------- gaz et eau
  function arrNum(a, k) { return $.isArray(a) ? a.map(function(v) { return isNum(v) ? Number(v) * (k || 1) : null; }) : null; }

  // Gaz (hors Wh / kWh, empile sinon) et eau : un graphe chacun, dans leur
  // unite (ou en € si un prix est connu), periode de comparaison en
  // pointilles, total et ecart dessous
  function renderFluids() {
    var d = S.data || {}, g = d.gas, cg = S.cmp && S.cmp.gas;
    renderFluid({
      id: 'hist-gas', chart: 'gasChart', what: 'de gaz', label: 'Gaz',
      show: !!S.cfg.Gaz && d.configured !== false && !gasIsEnergy(g || cg),
      keys: $.isArray(d.keys) ? d.keys : [], data: g ? arrNum(g.data) : null,
      cmp: cg ? arrNum(cg.data) : null, cmpKeys: S.cmp && S.cmp.keys,
      unit: String((g && g.unit) || (cg && cg.unit) || ''), price: num((g || cg || {}).price),
      color: safeColor(g && g.color, '#e67e22')
    });
    var w = S.water, wc = S.waterCmp, ws = w && $.isArray(w.series) ? w.series[0] : null;
    var wcs = wc && $.isArray(wc.series) ? wc.series[0] : null, coeff = num((w || {}).coeff) || 1;
    renderFluid({
      id: 'hist-water', chart: 'waterChart', what: 'd’eau', label: 'Eau',
      show: !!S.cfg.Water && !!w && w.configured !== false,
      keys: w && $.isArray(w.keys) ? w.keys : [], data: ws ? arrNum(ws.data, coeff) : null,
      cmp: wcs ? arrNum(wcs.data, num(wc.coeff) || coeff) : null, cmpKeys: wc && wc.keys,
      unit: String((w && w.displayUnit) || 'L'), price: num((w || {}).meterPrice),
      color: safeColor(ws && ws.color, '#2e86c1')
    });
  }

  function renderFluid(o) {
    var $card = $('#' + o.id + '-card'), $st = $('#' + o.id + '-stats'), $cv = $('#' + o.id);
    if (S[o.chart]) { try { S[o.chart].destroy(); } catch (e) {} S[o.chart] = null; }
    if (!o.show) { $card.hide(); return; }
    $card.show();
    var has = o.data && o.data.some(function(v) { return v; });
    if (!has) {
      $cv.hide();
      $st.text('Aucune consommation ' + o.what + ' enregistrée sur cette période.');
      return;
    }
    $cv.show();
    var eur = S.mode === 'eur' && o.price > 0, u = eur ? '€' : o.unit;
    var conv = function(v) { return v == null ? null : (eur ? v * o.price : v); };
    var mob = isMobile(), small = { size: 10 };
    var multiDay = S.gran === 'hour' && o.keys.length > 24;
    var labels = o.keys.map(function(k) { return pointLabel(k, multiDay); });
    var ds = [{ type: 'bar', label: o.label, data: o.data.map(conv), backgroundColor: o.color, borderColor: o.color,
                borderWidth: 0, order: 1 }];
    if (o.cmp) {
      ds.push({ type: 'line', label: S.compare === 'year' ? 'Année précédente' : 'Période précédente',
                data: labels.map(function(_, i) { return i < o.cmp.length ? conv(o.cmp[i]) : null; }),
                borderColor: '#555', backgroundColor: '#555', borderDash: [6, 4], borderWidth: 2,
                pointRadius: 0, pointHoverRadius: 3, fill: false, order: -1 });
    }
    S[o.chart] = new Chart(document.getElementById(o.id).getContext('2d'), {
      type: 'bar', data: { labels: labels, datasets: ds },
      options: {
        responsive: true, maintainAspectRatio: false, animation: false,
        interaction: { mode: 'index', intersect: false },
        datasets: { bar: { barPercentage: 0.9, categoryPercentage: 0.95 } },
        scales: {
          x: { ticks: mob ? { maxRotation: 0, autoSkip: true, maxTicksLimit: 7, font: small }
                          : { maxRotation: 70, minRotation: 0, autoSkip: true, maxTicksLimit: 31 } },
          y: { beginAtZero: true, grace: '10%',
               title: { display: mob, text: u, font: small, padding: 0 },
               ticks: { font: mob ? small : undefined, callback: function(v) { return fmtQ(v) + (mob ? '' : ' ' + u); } } }
        },
        plugins: {
          legend: { display: !!o.cmp, position: 'top',
                    labels: mob ? { boxWidth: 12, boxHeight: 10, padding: 6, font: { size: 11 } } : {} },
          tooltip: {
            titleFont: mob ? { size: 11 } : undefined, bodyFont: mob ? { size: 11 } : undefined,
            filter: function(it) { return it.parsed.y != null; },
            callbacks: {
              title: function(items) { return items.length ? tooltipTitle(o.keys[items[0].dataIndex]) : ''; },
              label: function(ctx) {
                var ck = ctx.datasetIndex && $.isArray(o.cmpKeys) ? o.cmpKeys[ctx.dataIndex] : null;
                return ctx.dataset.label + (ck ? ' [' + tooltipTitle(ck) + ']' : '') + ' : ' +
                       (eur ? fmtEur(ctx.parsed.y) : fmtQ(ctx.parsed.y) + ' ' + o.unit);
              }
            }
          }
        }
      }
    });
    var tot = sumArr(o.data);
    var h = 'Total : <b>' + esc(fmtQ(tot) + ' ' + o.unit) + '</b>' + (o.price ? ' (' + fmtEur(tot * o.price) + ')' : '');
    var ref = o.cmp ? sumArr(o.cmp) : 0;
    if (ref > 0) {
      var pct = (tot - ref) / ref * 100;
      h += ' · ' + (S.compare === 'year' ? 'écart sur un an' : 'écart avec la période précédente') + ' : ' +
           '<span style="color:' + (pct > 0 ? '#c0392b' : '#27ae60') + ';">' + (pct > 0 ? '+' : '') + fmt(pct, 1) + ' %</span>' +
           ' (' + esc(fmtQ(ref) + ' ' + o.unit) + ')';
    }
    $st.html(h);
  }

  // ---------------------------------------------------------------- export CSV
  function csvNum(v, d) { return v == null ? '' : Number(v).toFixed(d).replace('.', ','); }

  function exportCsv() {
    var d = S.data;
    if (!d || !$.isArray(d.keys)) return;
    var sets = buildSets(d, 'kwh');
    var head = ['Date'];
    sets.forEach(function(s) { head.push(s.name + (s.type === 'prod' ? ' (Prod)' : '') + ' (kWh)'); });
    var gas = d.gas && $.isArray(d.gas.data) && !gasIsEnergy(d.gas) ? d.gas : null;   // sinon dans sets, en kWh
    if (gas) head.push('Gaz (' + (gas.unit || '') + ')');
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
      if (gas) row.push(isNum(gas.data[i]) ? csvNum(Number(gas.data[i]), 3) : '');
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
