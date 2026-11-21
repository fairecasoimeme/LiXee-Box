// ============================================================================
// LiXee-Box - Economies (#savings), bouton du tableau de bord Energie
//
// Conseils calcules par la box (GET /api/energy/advice, energy_advice.cpp,
// regles de ECONOMIES.md) : meteo de l'electricite, risque de coupure, cout
// de l'instant, mois en cours et budget, consommation de fond, abonnement,
// comparaison des options du tarif reglemente, alertes. La page n'ajoute
// aucun calcul : LiXee-Assist et MQTT affichent les memes resultats.
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

  function card(title, body, extra) {
    return '<div class="col-md-6 col-xl-4"><div class="card h-100 shadow-sm"' + (extra || '') + '>' +
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
    return card('Risque de coupure',
      '<div class="d-flex justify-content-between align-items-baseline mb-1"><span class="fs-5 fw-semibold" style="color:' + c + ';">' + esc(r.label) + '</span>' +
        '<span class="text-muted">' + pct + ' % de l\'abonnement</span></div>' +
      '<div class="progress mb-2" style="height:10px;"><div class="progress-bar" style="width:' + pct + '%;background:' + c + ';"></div></div>' +
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
      var c = pct > 100 ? '#c0392b' : (pct > 90 ? '#d68910' : '#27ae60');
      body += '<div class="mt-3 small">Objectif : ' + eur(m.budget, 0) + ' / mois</div>' +
              '<div class="progress mb-1" style="height:10px;"><div class="progress-bar" style="width:' + Math.min(100, pct) + '%;background:' + c + ';"></div></div>' +
              '<div class="small" style="color:' + c + ';">' + (pct > 100
                ? 'Au rythme actuel, le mois dépassera l\'objectif de ' + eur(num(m.projected) - num(m.budget), 0) + '.'
                : 'Au rythme actuel, le mois restera dans l\'objectif (' + pct + ' %).') + '</div>';
      if (m.hourlyBudgetWh) body += '<div class="small text-muted mt-1">Soit en moyenne ' + fmt(m.hourlyBudgetWh, 0) + ' Wh par heure.</div>';
    } else if (m.unit !== 'EUR') {
      body += '<div class="small text-muted mt-2">Saisis tes prix dans <a href="#configEnergy">Config Énergie</a> pour voir les montants en euros.</div>';
    }
    return card('Mois en cours', body);
  }

  function baseCard(b) {
    if (!b) return '';
    var h = b.hour != null ? ' (heure la plus calme : ' + b.hour + ' h)' : '';
    return card('Consommation de fond',
      '<div class="fs-5 fw-semibold mb-1">' + fmt(b.w, 0) + ' W</div>' +
      '<p class="mb-1">' + esc(b.text) + '</p>' +
      '<div class="small text-muted">' + fmt(b.kwhYear, 0) + ' kWh / an' +
        (b.eurYear != null ? ', environ ' + eur(b.eurYear, 0) + ' / an' : '') + h + '</div>');
  }

  function subCard(s) {
    if (!s) return '';
    var c = s.tight ? '#d68910' : (s.suggestedKva ? '#27ae60' : '#2980b9');
    var meta = [];
    if (s.peakVA) meta.push('plus fort appel ' + fmt(s.peakVA / 1000, 1) + ' kVA');
    if (s.phasePeakVA) meta.push('phase la plus chargée ' + fmt(s.phasePeakVA / 1000, 1) + ' / ' + fmt(s.phaseLimitVA / 1000, 1) + ' kVA');
    if (s.days30Over80 != null) meta.push(s.days30Over80 + ' j sur 30 au-dessus de 80 %');
    meta.push(s.days + ' j observés');
    return card('Abonnement',
      '<div class="fs-5 fw-semibold mb-1" style="color:' + c + ';">' + s.kva + ' kVA' +
        (s.suggestedKva ? ' → ' + s.suggestedKva + ' kVA' : '') + '</div>' +
      '<p class="mb-1">' + esc(s.text) + '</p><div class="small text-muted">' + meta.join(' · ') + '</div>');
  }

  function tariffsCard(t) {
    if (!t) return '';
    if (t.needDays) {
      return '<div class="col-12"><div class="card shadow-sm"><div class="card-body">' +
               '<h6 class="card-title text-uppercase text-muted small mb-2">Tarif réglementé : quelle option ?</h6>' +
               '<p class="mb-0 text-muted">' + esc(t.text) + '</p></div></div></div>';
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
    return '<div class="col-12"><div class="card shadow-sm"><div class="card-body">' +
             '<h6 class="card-title text-uppercase text-muted small mb-2">Tarif réglementé : quelle option ?</h6>' +
             (t.text ? '<p class="mb-1 fw-semibold">' + esc(t.text) + '</p>' : '') +
             '<div class="row"><div class="col-lg-6">' + table('Sur les ' + t.days + ' derniers jours', t.period, false) + '</div>' +
             '<div class="col-lg-6">' + table('Estimation sur un an', t.year, true) + '</div></div>' +
             '<div class="small text-muted">' + notes.join(' · ') + '</div>' +
             (t.gridOutdated ? '<div class="alert alert-warning small py-1 mt-2 mb-0">La grille tarifaire a plus de 7 mois : les prix ont peut-être changé (1er février, 1er août).</div>' : '') +
           '</div></div></div>';
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
    var html = alertsBlock(d.alerts) +
               weatherCard(d.weather, d.costPerHour) + riskCard(d.cutRisk) + monthCard(d.month) +
               baseCard(d.baseLoad) + subCard(d.subscription) + tariffsCard(d.tariffs);
    if (!html) {
      html = '<div class="col-12"><p class="text-muted">Pas encore de conseil : il faut un ZLinky configuré dans ' +
             '<a href="#configEnergy">Config Énergie</a> et quelques heures de mesures.</p></div>';
    }
    $('#savings-cards').html(html);
  }

  function load() {
    return $.getJSON('/api/energy/advice').then(render, function() {
      $('#savings-cards').html('<div class="col-12"><div class="alert alert-warning">Conseils indisponibles.</div></div>');
    });
  }

  function page() {
    A.setContent(
      '<div class="container py-3">' +
        '<div class="d-flex align-items-center mb-3"><h4 class="mb-0">Économies</h4>' +
          '<a class="btn btn-sm btn-outline-dark ms-auto" href="#energy">Tableau de bord</a></div>' +
        '<p class="text-muted small">Conseils calculés par la box à partir de ce qu\'elle mesure : les mêmes que dans LiXee-Assist.</p>' +
        '<div class="row g-3" id="savings-cards"><div class="col-12 text-muted">Chargement...</div></div>' +
      '</div>');
    load();
    A.pageInterval(load, 30000);
  }

  A.registerPage('savings', page);
})();
