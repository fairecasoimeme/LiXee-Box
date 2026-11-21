// ============================================================================
// LiXee-Box - Economies (#savings), menu Mesures et bandeau du tarif
//
// Conseils calcules par la box (GET /api/energy/advice, energy_advice.cpp,
// regles de ECONOMIES.md) : meteo de l'electricite, risque de coupure, cout
// de l'instant, mois en cours et budget, consommation de fond, abonnement,
// comparaison des options du tarif reglemente, alertes. La page n'ajoute
// aucun calcul : LiXee-Assist et MQTT affichent les memes resultats.
// Graphes (Chart.js, charge a la demande) : 24 h et consommation de fond,
// jours du mois et budget, plus forts appels par mois et abonnement, cout
// annuel par option et part des heures creuses.
// ============================================================================
(function() {
  'use strict';
  var A = window.LixeeApp;
  if (!A) return;
  var esc = A.escHtml;

  function num(v) { var n = Number(v); return (v == null || !isFinite(n)) ? 0 : n; }
  function fmt(v, d) { return num(v).toLocaleString('fr-FR', { minimumFractionDigits: d, maximumFractionDigits: d }); }
  function eur(v, d) { return fmt(v, d == null ? 2 : d) + ' €'; }

  var WEATHER_COLORS = {
    surplus: '#1e8449', cheap: '#27ae60', flat: '#2980b9',
    normal: '#d68910', expensive: '#5d6d7e', peak: '#c0392b'
  };
  var RISK_COLORS = { low: '#27ae60', medium: '#d68910', high: '#c0392b' };
  var ALERT_CLASS = { danger: 'danger', warning: 'warning', info: 'info' };
  var COLOR_LABEL = { blue: 'bleu', white: 'blanc', red: 'rouge' };
  var COLOR_BADGE = { blue: '#2980b9', white: '#95a5a6', red: '#c0392b' };
  var MONTHS_S = ['janv.', 'févr.', 'mars', 'avr.', 'mai', 'juin', 'juil.', 'août', 'sept.', 'oct.', 'nov.', 'déc.'];
  var C_BAR = '#85929e', C_GOOD = '#27ae60', C_WARN = '#d68910', C_BAD = '#c0392b', C_BLUE = '#2980b9';

  // ---------------------------------------------------------------- graphes
  var charts = [];
  var chartLib = null;
  function loadChartLib() {
    if (!chartLib) chartLib = A.loadScripts(['js/chart.umd.min.js']);
    return chartLib;
  }
  function destroyCharts() { while (charts.length) { try { charts.pop().destroy(); } catch (e) {} } }
  function canvas(id, h) {
    return '<div class="mt-3" style="position:relative;height:' + (h || 170) + 'px;"><canvas id="' + id + '"></canvas></div>';
  }
  function makeChart(id, cfg) {
    var el = document.getElementById(id);
    if (!el || !window.Chart) return;
    cfg.options = $.extend(true, {
      responsive: true, maintainAspectRatio: false, animation: false,
      plugins: { legend: { display: false } },
      scales: { x: { grid: { display: false }, ticks: { font: { size: 10 } } }, y: { beginAtZero: true, ticks: { font: { size: 10 } } } }
    }, cfg.options || {});
    charts.push(new Chart(el.getContext('2d'), cfg));
  }
  // Ligne horizontale de reference (dataset de type ligne, sans points)
  function refLine(label, value, n, color, dash) {
    var data = [];
    for (var i = 0; i < n; i++) data.push(value);
    return { type: 'line', label: label, data: data, borderColor: color, borderWidth: 2, borderDash: dash || [],
             pointRadius: 0, pointHitRadius: 0, fill: false, order: 0 };
  }

  // ---------------------------------------------------------------- cartes
  function card(title, body, extra, wide) {
    return '<div class="' + (wide ? 'col-12' : 'col-md-6 col-xl-4') + '"><div class="card h-100 shadow-sm"' + (extra || '') + '>' +
             '<div class="card-body"><h6 class="card-title text-uppercase text-muted small mb-3">' + title + '</h6>' +
             body + '</div></div></div>';
  }

  function weatherCard(w, costPerHour) {
    if (!w) return '';
    var c = WEATHER_COLORS[w.level] || '#2980b9';
    var meta = [];
    if (w.price != null) meta.push(fmt(w.price, 4) + ' €/kWh');
    if (costPerHour != null) meta.push('en ce moment ' + eur(costPerHour) + ' / h');
    if (w.next) meta.push('prochain changement à ' + esc(w.next));
    var tomorrow = w.tomorrow ? '<span class="badge ms-2" style="background:' + COLOR_BADGE[w.tomorrow] + ';">Demain : ' +
                                COLOR_LABEL[w.tomorrow] + '</span>' : '';
    return card('Météo de l\'électricité',
      '<div class="d-flex align-items-center mb-2"><span class="rounded-circle me-2" style="width:14px;height:14px;background:' + c + ';display:inline-block;"></span>' +
        '<span class="fs-5 fw-semibold" style="color:' + c + ';">' + esc(w.title) + '</span>' + tomorrow + '</div>' +
      '<p class="mb-2">' + esc(w.text) + '</p>' +
      (meta.length ? '<div class="small text-muted">' + meta.join(' · ') + '</div>' : ''),
      ' style="border-left:6px solid ' + c + ';"');
  }

  function riskCard(r) {
    if (!r) return '';
    var c = RISK_COLORS[r.level] || '#27ae60';
    var pct = Math.max(0, Math.min(100, Math.round(num(r.ratio) * 100)));
    var src = r.averaged ? 'moyenne sur 1 min' : 'puissance instantanée';
    if (r.phase) src += ', phase ' + r.phase;
    // Jauge en trois zones (moins de 70 %, 70 a 90 %, 90 % et plus) et curseur
    var gauge =
      '<div class="position-relative mb-1" style="height:14px;">' +
        '<div class="d-flex h-100 rounded overflow-hidden">' +
          '<div style="width:70%;background:#d5f5e3;"></div><div style="width:20%;background:#fdebd0;"></div><div style="width:10%;background:#fadbd8;"></div>' +
        '</div>' +
        '<div class="position-absolute top-0 h-100 rounded" style="left:0;width:' + pct + '%;background:' + c + ';opacity:0.85;"></div>' +
      '</div>' +
      '<div class="position-relative small text-muted mb-2" style="height:1.3em;">' +
        '<span class="position-absolute" style="left:70%;transform:translateX(-50%);">70 %</span>' +
        '<span class="position-absolute" style="left:90%;transform:translateX(-50%);">90 %</span></div>';
    return card('Risque de coupure',
      '<div class="d-flex justify-content-between align-items-baseline mb-1"><span class="fs-5 fw-semibold" style="color:' + c + ';">' + esc(r.label) + '</span>' +
        '<span class="text-muted">' + pct + ' % de l\'abonnement</span></div>' + gauge +
      '<p class="mb-1">' + esc(r.text) + '</p><div class="small text-muted">' + src + '</div>');
  }

  function monthCard(m) {
    if (!m) return '';
    var unit = m.unit === 'EUR' ? ' €' : ' kWh';
    var d = m.unit === 'EUR' ? 2 : 1;
    var body = '<div class="d-flex justify-content-between"><div><div class="small text-muted">Depuis le 1er</div>' +
                 '<div class="fs-5 fw-semibold">' + fmt(m.soFar, d) + unit + '</div></div>' +
               '<div class="text-end"><div class="small text-muted">Au rythme actuel</div>' +
                 '<div class="fs-5 fw-semibold">' + fmt(m.projected, d) + unit + '</div></div></div>';
    if (m.budget) {
      var pct = Math.round(num(m.projected) / num(m.budget) * 100);
      var c = pct > 100 ? C_BAD : (pct > 90 ? C_WARN : C_GOOD);
      body += '<div class="mt-3 small">Objectif : ' + eur(m.budget, 0) + ' / mois</div>' +
              '<div class="progress mb-1" style="height:10px;"><div class="progress-bar" style="width:' + Math.min(100, pct) + '%;background:' + c + ';"></div></div>' +
              '<div class="small" style="color:' + c + ';">' + (pct > 100
                ? 'Au rythme actuel, le mois dépassera l\'objectif de ' + eur(num(m.projected) - num(m.budget), 0) + '.'
                : 'Au rythme actuel, le mois restera dans l\'objectif (' + pct + ' %).') + '</div>';
      if (m.hourlyBudgetWh) body += '<div class="small text-muted mt-1">Soit en moyenne ' + fmt(m.hourlyBudgetWh, 0) + ' Wh par heure.</div>';
    } else if (m.unit !== 'EUR') {
      body += '<div class="small text-muted mt-2">Saisis tes prix dans <a href="#configEnergy">Config Énergie</a> pour voir les montants en euros.</div>';
    }
    if (m.daily && m.daily.length) body += canvas('sv-month');
    return card('Mois en cours', body);
  }

  function drawMonth(m) {
    if (!m || !m.daily || !m.daily.length) return;
    var labels = [], data = [], colors = [], n = num(m.days) || m.daily.length;
    var lim = num(m.budgetDay);
    for (var i = 0; i < n; i++) {
      labels.push(String(i + 1));
      var v = i < m.daily.length ? num(m.daily[i]) : null;
      data.push(v);
      colors.push(lim > 0 && v > lim ? C_BAD : (i === m.daily.length - 1 ? '#aab7b8' : C_BLUE));
    }
    var u = m.unit === 'EUR' ? ' €' : ' kWh';
    var ds = [{ type: 'bar', label: m.unit === 'EUR' ? 'Coût du jour' : 'Consommation du jour', data: data, backgroundColor: colors, order: 1 }];
    if (lim > 0) ds.push(refLine('Budget du jour', lim, n, C_WARN, [5, 4]));
    makeChart('sv-month', {
      data: { labels: labels, datasets: ds },
      options: { plugins: { legend: { display: lim > 0, labels: { boxWidth: 10, font: { size: 10 } } },
                 tooltip: { callbacks: { label: function(c) { return c.dataset.label + ' : ' + fmt(c.parsed.y, m.unit === 'EUR' ? 2 : 1) + u; },
                                         title: function(c) { return 'Le ' + c[0].label; } } } } }
    });
  }

  function baseCard(b) {
    if (!b) return '';
    var h = b.hour != null ? ' (heure la plus calme : ' + b.hour + ' h)' : '';
    return card('Consommation de fond',
      '<div class="fs-5 fw-semibold mb-1">' + fmt(b.w, 0) + ' W</div>' +
      '<p class="mb-1">' + esc(b.text) + '</p>' +
      '<div class="small text-muted">' + fmt(b.kwhYear, 0) + ' kWh / an' +
        (b.eurYear != null ? ', environ ' + eur(b.eurYear, 0) + ' / an' : '') + h + '</div>' +
      (b.hours && b.hours.length ? canvas('sv-base') +
        '<div class="small text-muted mt-1"><span style="color:' + C_GOOD + ';">■</span> heure la plus calme' +
        (b.hours.some(function(x) { return num(x[2]) > 0; }) ? ' · <span style="color:#f1c40f;">■</span> heures avec production' : '') +
        '</div>' : ''));
  }

  function drawBase(b) {
    if (!b || !b.hours || !b.hours.length) return;
    var labels = [], data = [], colors = [];
    b.hours.forEach(function(x) {
      labels.push(x[0] + ' h');
      data.push(num(x[1]));
      colors.push(x[0] === b.hour ? C_GOOD : (num(x[2]) > 0 ? '#f7dc6f' : C_BAR));
    });
    makeChart('sv-base', {
      data: { labels: labels, datasets: [
        { type: 'bar', label: 'Consommation', data: data, backgroundColor: colors, order: 1 },
        refLine('Consommation de fond', num(b.w), labels.length, C_GOOD, [5, 4])
      ] },
      options: { scales: { x: { ticks: { maxRotation: 0, autoSkip: true, maxTicksLimit: 8 } } },
                 plugins: { tooltip: { callbacks: { label: function(c) { return c.dataset.label + ' : ' + fmt(c.parsed.y, 0) + ' Wh'; } } } } }
    });
  }

  function subCard(s) {
    if (!s) return '';
    var c = s.tight ? C_WARN : (s.suggestedKva ? C_GOOD : C_BLUE);
    var meta = [];
    if (s.peakVA) meta.push('plus fort appel ' + fmt(s.peakVA / 1000, 1) + ' kVA');
    if (s.phasePeakVA) meta.push('phase la plus chargée ' + fmt(s.phasePeakVA / 1000, 1) + ' / ' + fmt(s.phaseLimitVA / 1000, 1) + ' kVA');
    if (s.days30Over80 != null) meta.push(s.days30Over80 + ' j sur 30 au-dessus de 80 %');
    meta.push(s.days + ' j observés');
    return card('Abonnement',
      '<div class="fs-5 fw-semibold mb-1" style="color:' + c + ';">' + s.kva + ' kVA' +
        (s.suggestedKva ? ' → ' + s.suggestedKva + ' kVA' : '') + '</div>' +
      '<p class="mb-1">' + esc(s.text) + '</p><div class="small text-muted">' + meta.join(' · ') + '</div>' +
      (s.months && s.months.length && s.limitVA ? canvas('sv-sub') : ''));
  }

  function drawSub(s) {
    if (!s || !s.months || !s.months.length || !s.limitVA) return;
    var lim = num(s.limitVA) / 1000, labels = [], data = [], colors = [];
    s.months.forEach(function(x) {
      var p = String(x[0]).split('-');
      labels.push((MONTHS_S[Number(p[1]) - 1] || x[0]) + ' ' + String(p[0]).slice(2));
      var v = num(x[1]) / 1000;
      data.push(v);
      colors.push(v >= lim * 0.9 ? C_BAD : (v >= lim * 0.8 ? C_WARN : C_BLUE));
    });
    var n = labels.length, what = s.basis === 'phase' ? 'Limite par phase' : 'Abonnement';
    makeChart('sv-sub', {
      data: { labels: labels, datasets: [
        { type: 'bar', label: 'Plus fort appel', data: data, backgroundColor: colors, order: 1 },
        refLine(what, lim, n, C_BAD),
        refLine('80 %', lim * 0.8, n, C_WARN, [4, 4])
      ] },
      options: { plugins: { legend: { display: true, labels: { boxWidth: 10, font: { size: 10 } } },
                 tooltip: { callbacks: { label: function(c) { return c.dataset.label + ' : ' + fmt(c.parsed.y, 1) + ' kVA'; } } } },
                 scales: { y: { suggestedMax: lim * 1.05, title: { display: true, text: 'kVA', font: { size: 10 } } } } }
    });
  }

  function tariffsCard(t) {
    if (!t) return '';
    if (t.needDays) {
      return card('Tarif réglementé : quelle option ?', '<p class="mb-0 text-muted">' + esc(t.text) + '</p>', '', true);
    }
    function rows(list, yearly) {
      return (list || []).map(function(o) {
        var best = o.option === t.cheapest, cur = o.option === t.current;
        return '<tr' + (best ? ' class="table-success"' : '') + '><td>' + esc(o.label) + (cur ? ' <span class="badge bg-secondary">actuelle</span>' : '') +
               '<div class="small text-muted">' + o.kva + ' kVA</div></td>' +
               '<td class="text-end">' + eur(o.energy, yearly ? 0 : 2) + '</td><td class="text-end">' + eur(o.subscription, yearly ? 0 : 2) + '</td>' +
               '<td class="text-end fw-semibold">' + eur(o.total, yearly ? 0 : 2) + '</td></tr>';
      }).join('');
    }
    function table(title, list, yearly) {
      return '<div class="small text-muted mt-2">' + title + '</div>' +
             '<div class="table-responsive"><table class="table table-sm align-middle mb-1"><thead><tr><th>Option</th>' +
             '<th class="text-end">Énergie</th><th class="text-end">Abonnement</th><th class="text-end">Total</th></tr></thead><tbody>' +
             rows(list, yearly) + '</tbody></table></div>';
    }
    var share = Math.round(num(t.offPeakShare) * 100);
    var notes = [share + ' % en heures creuses (' + (t.offPeakSource === 'index' ? 'index du compteur' : 'mesuré de 22 h à 6 h') + ')'];
    if (t.redDays || t.whiteDays) notes.push(t.redDays + ' jour(s) rouge(s), ' + t.whiteDays + ' blanc(s)');
    if (t.colorsKnown < t.days) notes.push('couleur Tempo inconnue pour ' + (t.days - t.colorsKnown) + ' jour(s) : moyenne de saison');
    notes.push('sur un an : ' + (t.seasonMeasured ? 'couleurs de la dernière saison' : '22 rouges, 43 blancs, 300 bleus'));
    notes.push('grille du ' + esc(t.gridFrom) + (t.gridOutdated ? ' (à mettre à jour)' : ''));
    var graphs =
      '<div class="row g-3 align-items-center mt-1">' +
        '<div class="col-md-8"><div class="small text-muted">Coût estimé sur un an</div>' +
          '<div style="position:relative;height:' + (40 + 34 * (t.year || []).length) + 'px;"><canvas id="sv-tariffs"></canvas></div></div>' +
        '<div class="col-md-4"><div class="small text-muted text-center">Heures creuses / pleines</div>' +
          '<div style="position:relative;height:130px;"><canvas id="sv-offpeak"></canvas></div></div>' +
      '</div>';
    return '<div class="col-12"><div class="card shadow-sm"><div class="card-body">' +
             '<h6 class="card-title text-uppercase text-muted small mb-2">Tarif réglementé : quelle option ?</h6>' +
             (t.text ? '<p class="mb-1 fw-semibold">' + esc(t.text) + '</p>' : '') + graphs +
             '<div class="row"><div class="col-lg-6">' + table('Sur les ' + t.days + ' derniers jours', t.period, false) + '</div>' +
             '<div class="col-lg-6">' + table('Estimation sur un an', t.year, true) + '</div></div>' +
             '<div class="small text-muted">' + notes.join(' · ') + '</div>' +
             (t.gridOutdated ? '<div class="alert alert-warning small py-1 mt-2 mb-0">La grille tarifaire a plus de 7 mois : les prix ont peut-être changé (1er février, 1er août).</div>' : '') +
           '</div></div></div>';
  }

  function drawTariffs(t) {
    if (!t || t.needDays || !t.year || !t.year.length) return;
    var labels = t.year.map(function(o) {
      return o.label + (o.option === t.current ? ' (actuelle)' : '') + (o.option === t.cheapest ? ' ★' : '');
    });
    makeChart('sv-tariffs', {
      type: 'bar',
      data: { labels: labels, datasets: [
        { label: 'Énergie', data: t.year.map(function(o) { return num(o.energy); }),
          backgroundColor: t.year.map(function(o) { return o.option === t.cheapest ? C_GOOD : C_BLUE; }) },
        { label: 'Abonnement', data: t.year.map(function(o) { return num(o.subscription); }), backgroundColor: '#aab7b8' }
      ] },
      options: { indexAxis: 'y',
                 plugins: { legend: { display: true, position: 'bottom', labels: { boxWidth: 10, font: { size: 10 } } },
                            tooltip: { callbacks: { label: function(c) { return c.dataset.label + ' : ' + eur(c.parsed.x, 0); } } } },
                 scales: { x: { stacked: true, grid: { display: true }, ticks: { callback: function(v) { return fmt(v, 0) + ' €'; } } },
                           y: { stacked: true, grid: { display: false } } } }
    });
    var share = Math.max(0, Math.min(1, num(t.offPeakShare)));
    makeChart('sv-offpeak', {
      type: 'doughnut',
      data: { labels: ['Heures creuses', 'Heures pleines'],
              datasets: [{ data: [Math.round(share * 100), 100 - Math.round(share * 100)], backgroundColor: [C_GOOD, '#d5dbdb'], borderWidth: 0 }] },
      options: { cutout: '60%', scales: { x: { display: false }, y: { display: false } },
                 plugins: { legend: { display: true, position: 'bottom', labels: { boxWidth: 10, font: { size: 10 } } },
                            tooltip: { callbacks: { label: function(c) { return c.label + ' : ' + c.parsed + ' %'; } } } } }
    });
  }

  function alertsBlock(list) {
    if (!list || !list.length) return '';
    return '<div class="col-12">' + list.map(function(a) {
      var when = a.at ? ' <span class="small text-muted">' + new Date(a.at * 1000).toLocaleTimeString('fr-FR', { hour: '2-digit', minute: '2-digit' }) + '</span>' : '';
      return '<div class="alert alert-' + (ALERT_CLASS[a.level] || 'info') + ' py-2 mb-2"><strong>' + esc(a.title) + '</strong>' + when +
             (a.text ? '<div class="small">' + esc(a.text) + '</div>' : '') + '</div>';
    }).join('') + '</div>';
  }

  function render(d) {
    destroyCharts();
    var html = alertsBlock(d.alerts) +
               weatherCard(d.weather, d.costPerHour) + riskCard(d.cutRisk) + monthCard(d.month) +
               baseCard(d.baseLoad) + subCard(d.subscription) + tariffsCard(d.tariffs);
    if (!html) {
      html = '<div class="col-12"><p class="text-muted">Pas encore de conseil : il faut un ZLinky configuré dans ' +
             '<a href="#configEnergy">Config Énergie</a> et quelques heures de mesures.</p></div>';
    }
    $('#savings-cards').html(html);
    loadChartLib().then(function() {
      drawMonth(d.month);
      drawBase(d.baseLoad);
      drawSub(d.subscription);
      drawTariffs(d.tariffs);
    }, function() {});
  }

  function load() {
    return $.getJSON('/api/energy/advice').then(render, function() {
      $('#savings-cards').html('<div class="col-12"><div class="alert alert-warning">Conseils indisponibles.</div></div>');
    });
  }

  function page() {
    destroyCharts();
    A.setContent(
      '<div class="container py-3">' +
        '<div class="d-flex align-items-center mb-3"><h4 class="mb-0">Économies</h4>' +
          '<a class="btn btn-sm btn-outline-dark ms-auto" href="#energy">Tableau de bord</a></div>' +
        '<p class="text-muted small">Conseils calculés par la box à partir de ce qu\'elle mesure : les mêmes que dans LiXee-Assist.</p>' +
        '<div class="row g-3" id="savings-cards"><div class="col-12 text-muted">Chargement...</div></div>' +
      '</div>');
    load();
    A.pageInterval(load, 60000);
  }

  A.registerPage('savings', page);
})();
