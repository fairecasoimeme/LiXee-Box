// ============================================================================
// LiXee MatterBridge - chauffe-eau virtuels (sur le modele de page_thermostat.js)
//   #waterheaters        Mesures -> Chauffe-eau : cartes des zones
//   #configWaterHeaters  Config -> Chauffe-eau : fiches des zones
//   #editWaterHeater     formulaire d'une zone (?id=N ou ?id=new)
// API : /api/waterheaters* (components/webserver/web_waterheater.cpp)
// Modes (water_heater.h) : 0 Arret, 1 Marche forcee, 2 Programme
// ============================================================================
(function() {
  'use strict';
  var A = window.LixeeApp;
  if (!A) return;
  var esc = A.escHtml;

  var MODE_OFF = 0, MODE_MANUAL = 1, MODE_TIMED = 2;
  var MODE_LABELS = ['Arrêt', 'Marche', 'Programme'];

  function errMsg(xhr, fallback) {
    return A.apiErrorMsg ? A.apiErrorMsg(xhr, fallback) : esc(fallback || 'Erreur');
  }
  function post(url, obj) { return A.postJSON(url, obj); }

  var ICON_DROP = "M8 16a6 6 0 0 0 6-6c0-1.655-1.122-2.904-2.432-4.362C10.254 4.176 8.75 2.503 8 0c-.75 2.503-2.254 4.176-3.568 5.638C3.122 7.096 2 8.345 2 10a6 6 0 0 0 6 6";
  var ICON_BOOST = "M11.251.068a.5.5 0 0 1 .227.58L9.677 6.5H13a.5.5 0 0 1 .364.843l-8 8.5a.5.5 0 0 1-.842-.49L6.323 9.5H3a.5.5 0 0 1-.364-.843l8-8.5a.5.5 0 0 1 .615-.09z";
  var ICON_GEAR = "<path d='M8 4.754a3.246 3.246 0 1 0 0 6.492 3.246 3.246 0 0 0 0-6.492M5.754 8a2.246 2.246 0 1 1 4.492 0 2.246 2.246 0 0 1-4.492 0'/><path d='M9.796 1.343c-.527-1.79-3.065-1.79-3.592 0l-.094.319a.873.873 0 0 1-1.255.52l-.292-.16c-1.64-.892-3.433.902-2.54 2.541l.159.292a.873.873 0 0 1-.52 1.255l-.319.094c-1.79.527-1.79 3.065 0 3.592l.319.094a.873.873 0 0 1 .52 1.255l-.16.292c-.892 1.64.901 3.434 2.541 2.54l.292-.159a.873.873 0 0 1 1.255.52l.094.319c.527 1.79 3.065 1.79 3.592 0l.094-.319a.873.873 0 0 1 1.255-.52l.292.16c1.64.893 3.434-.902 2.54-2.541l-.159-.292a.873.873 0 0 1 .52-1.255l.319-.094c1.79-.527 1.79-3.065 0-3.592l-.319-.094a.873.873 0 0 1-.52-1.255l.16-.292c.893-1.64-.902-3.433-2.541-2.54l-.292.159a.873.873 0 0 1-1.255-.52zm-2.633.283c.246-.835 1.428-.835 1.674 0l.094.319a1.873 1.873 0 0 0 2.693 1.115l.291-.16c.764-.415 1.6.42 1.184 1.185l-.159.292a1.873 1.873 0 0 0 1.116 2.692l.318.094c.835.246.835 1.428 0 1.674l-.319.094a1.873 1.873 0 0 0-1.115 2.693l.16.291c.415.764-.42 1.6-1.185 1.184l-.291-.159a1.873 1.873 0 0 0-2.693 1.116l-.094.318c-.246.835-1.428.835-1.674 0l-.094-.319a1.873 1.873 0 0 0-2.692-1.115l-.292.16c-.764.415-1.6-.42-1.184-1.185l.159-.291A1.873 1.873 0 0 0 1.945 8.93l-.319-.094c-.835-.246-.835-1.428 0-1.674l.319-.094A1.873 1.873 0 0 0 3.06 4.377l-.16-.292c-.415-.764.42-1.6 1.185-1.184l.292.159a1.873 1.873 0 0 0 2.692-1.115z'/>";

  // ==========================================================================
  // Mesures -> Chauffe-eau
  // ==========================================================================
  var whZones = [];

  function pageWaterHeaters() {
    A.setContent(
      '<div class="container">' +
        '<div style="margin:1rem 0;"><h3>Chauffe-eau</h3></div>' +
        '<div class="row" id="whCards"></div>' +
        '<div id="whEmpty" style="display:none;color:#888;">Aucun chauffe-eau configur&eacute;. Rendez-vous dans <b>Config &rarr; Chauffe-eau</b> pour en cr&eacute;er un.</div>' +
      '</div>');
    whLoad();
    A.pageInterval(whLoad, 5000);
  }

  function whLoad() { $.getJSON('/api/waterheaters').done(whRender); }

  function whRender(z) {
    var c = document.getElementById('whCards');
    if (!c) return;
    whZones = z || [];
    document.getElementById('whEmpty').style.display = whZones.length ? 'none' : 'block';
    var h = '';
    for (var i = 0; i < whZones.length; i++) h += zoneCard(whZones[i]);
    c.innerHTML = h;
  }

  function fmtRemaining(sec) {
    sec = Math.max(0, sec | 0);
    var m = Math.floor(sec / 60), s = sec % 60;
    return m + ' min' + (m < 5 ? ' ' + ('0' + s).slice(-2) + ' s' : '');
  }

  function zoneCard(t) {
    var active = !!(t.output && t.actualOn);
    var mc = active ? '#e67e22' : '#adb5bd';
    var icon = "<svg width='22' height='22' viewBox='0 0 16 16' fill='" + mc + "'><path d='" + ICON_DROP + "'/></svg>";

    var stat;
    if (t.boosting) {
      stat = "<span style='color:#8e44ad;'>Boost en cours (" + fmtRemaining(t.boostRemainingSec) + ' restantes)</span>';
    } else if (t.mode == MODE_OFF) {
      stat = "<span style='color:#999;'>Arr&ecirc;t</span>";
    } else if (t.hasSensor && !t.sensorValid) {
      var rs = ['', 'capteur introuvable', 'aucune temp&eacute;rature lue', 'aucune trame depuis ' + Math.round(t.sensorAge / 60) + ' min'][t.sensorReason] || '';
      stat = "<span style='color:#e74c3c;' title='" + rs + "'>Capteur HS" + (rs ? ' (' + rs + ')' : '') + '</span>';
    } else if (t.mode == MODE_TIMED && !t.schedActive) {
      stat = "<span style='color:#9b59b6;'>Hors plage</span>";
    } else if (t.output && !t.actualOn) {
      stat = "<span style='color:#f39c12;'>D&eacute;marrage&hellip;</span>";
    } else if (!t.output && t.actualOn) {
      stat = "<span style='color:#f39c12;'>Arr&ecirc;t&hellip;</span>";
    } else {
      stat = t.actualOn ? 'Chauffe (' + t.duty + '%)' : 'R&eacute;gule';
    }

    var tv = (t.temp === null || t.temp === undefined) ? null : t.temp;

    var h = "<div class='col-lg-4 col-md-6 col-12'><div class='card' style='padding:18px;margin-bottom:1rem;'>";
    h += "<div style='display:flex;align-items:center;justify-content:space-between;'><h5 style='margin:0;font-size:17px;'>" + esc(t.name) + '</h5>';
    h += "<a href='#editWaterHeater?id=" + t.id + "' title='Configurer' style='line-height:0;color:#9aa0a6;'><svg width='18' height='18' viewBox='0 0 16 16' fill='currentColor'>" + ICON_GEAR + '</svg></a></div>';
    h += "<div style='text-align:center;margin-top:10px;'>";
    h += "<div style='font-size:11px;color:#999;text-transform:uppercase;letter-spacing:.5px;'>Consigne</div>";
    h += "<div style='font-size:36px;font-weight:700;line-height:1.1;'>" + Number(t.setpoint).toFixed(1) + "<span style='font-size:16px;font-weight:400;'>&deg;C</span></div>";
    h += "<div style='margin-top:4px;line-height:0;'>" + icon + '</div>';
    h += "<div style='margin-top:4px;font-size:12px;color:#888;'>" + stat + '</div>';
    h += "<div style='font-size:13px;color:#6c757d;margin-top:6px;'>Actuelle : <b style='color:#343a40;'>" + (tv === null ? '&mdash;' : (Number(tv).toFixed(1) + ' &deg;C')) + '</b></div>';
    h += '</div>';
    h += "<div style='display:flex;justify-content:center;align-items:center;gap:24px;margin-top:10px;'>";
    h += "<button class='btn btn-outline-secondary' style='font-size:22px;width:44px;height:44px;line-height:1;padding:0;border-radius:50%;' onclick='whSet(" + t.id + ",-1)'>&minus;</button>";
    h += "<button class='btn btn-outline-secondary' style='font-size:22px;width:44px;height:44px;line-height:1;padding:0;border-radius:50%;' onclick='whSet(" + t.id + ",1)'>+</button></div>";
    var mb = function(m) {
      var on = (t.mode == m);
      return "<button onclick='whMode(" + t.id + ',' + m + ")' style='flex:1;border:1px solid #dee2e6;background:" + (on ? '#6c757d' : '#fff') + ';color:' + (on ? '#fff' : '#6c757d') + ";font-size:12px;padding:5px 0;cursor:pointer;'>" + MODE_LABELS[m] + '</button>';
    };
    h += "<div style='display:flex;border-radius:8px;overflow:hidden;margin-top:12px;'>" + mb(0) + mb(1) + mb(2) + '</div>';
    h += "<div style='display:flex;gap:8px;margin-top:8px;'>";
    if (t.boosting) {
      h += "<button class='btn btn-outline-danger btn-sm' style='flex:1;' onclick='whCancelBoost(" + t.id + ")'>Annuler le boost</button>";
    } else {
      h += "<button class='btn btn-outline-warning btn-sm' style='flex:1;' onclick='whBoost(" + t.id + ",1800)'><svg width='12' height='12' viewBox='0 0 16 16' fill='currentColor' style='vertical-align:-1px;margin-right:3px;'><path d='" + ICON_BOOST + "'/></svg>Boost 30 min</button>";
      h += "<button class='btn btn-outline-warning btn-sm' style='flex:1;' onclick='whBoost(" + t.id + ",7200)'>Boost 2h</button>";
    }
    h += '</div>';
    h += '</div></div>';
    return h;
  }

  function zoneById(id) {
    for (var i = 0; i < whZones.length; i++) if (whZones[i].id === id) return whZones[i];
    return null;
  }
  function whCall(url, obj) {
    post(url, obj).always(whLoad).fail(function(xhr) { A.showAlert(errMsg(xhr, 'Commande refusée'), 'danger'); });
  }
  window.whSet = function(id, d) {
    var z = zoneById(id), s = z ? z.setpoint : 55;
    var v = Math.max(0, Math.min(70, Math.round(s + d)));
    whCall('/api/waterheaters/setpoint', { id: id, value: v });
  };
  window.whMode = function(id, m) { whCall('/api/waterheaters/mode', { id: id, mode: m }); };
  window.whBoost = function(id, durationSec) { whCall('/api/waterheaters/boost', { id: id, durationSec: durationSec, oneShot: true }); };
  window.whCancelBoost = function(id) { whCall('/api/waterheaters/cancelboost', { id: id }); };

  // ==========================================================================
  // Config -> Chauffe-eau
  // ==========================================================================
  var CFG_CSS = '<style>' +
    '.wh-card{background:#fff;border:none;border-radius:12px;box-shadow:0 2px 8px rgba(0,0,0,0.08)}' +
    '.wh-card table td{padding:6px 4px;border:none}' +
    '.wh-card .btn-actions{display:flex;flex-wrap:wrap;justify-content:flex-end;gap:6px;margin-top:12px;padding-top:12px;border-top:1px solid #e9ecef}' +
    '.wh-card .btn{padding:6px 10px;display:inline-flex;align-items:center;justify-content:center}' +
    '.wh-card .btn svg{width:16px;height:16px;flex-shrink:0}' +
    '</style>';
  var SVG_PLUS = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' viewBox='0 0 16 16' style='width:16px;vertical-align:-2px;'><path d='M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16'/><path d='M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4'/></svg>";
  var SVG_PEN = "<svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'><path d='M12.146.146a.5.5 0 0 1 .708 0l3 3a.5.5 0 0 1 0 .708l-10 10a.5.5 0 0 1-.168.11l-5 2a.5.5 0 0 1-.65-.65l2-5a.5.5 0 0 1 .11-.168zM11.207 2.5 13.5 4.793 14.793 3.5 12.5 1.207zm1.586 3L10.5 3.207 4 9.707V10h.5a.5.5 0 0 1 .5.5v.5h.5a.5.5 0 0 1 .5.5v.5h.293zm-9.761 5.175-.106.106-1.528 3.821 3.821-1.528.106-.106A.5.5 0 0 1 5 12.5V12h-.5a.5.5 0 0 1-.5-.5V11h-.5a.5.5 0 0 1-.468-.325'/></svg>";
  var SVG_TRASH = "<svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'><path d='M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z'/><path d='M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H6a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1h3.5a1 1 0 0 1 1 1zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z'/></svg>";

  function lbl(label, ieee) { return ieee ? esc(label || ieee) : '(non d&eacute;fini)'; }

  function pageConfigWaterHeaters() {
    $.getJSON('/api/waterheaters/config').done(function(cfg) {
      var zones = (cfg && cfg.zones) || [];
      var h = CFG_CSS + '<div class="container">' +
        '<div style="display:flex;justify-content:space-between;align-items:center;margin:1rem 0;">' +
        '<h3>Configuration des chauffe-eau</h3>';
      if (zones.length < (cfg.max || 4)) {
        h += '<a class="btn btn-primary" href="#editWaterHeater?id=new">' + SVG_PLUS + ' Ajouter un chauffe-eau</a>';
      }
      h += '</div><div class="row g-4">';
      zones.forEach(function(t) {
        var i = t.id;
        h += '<div class="col-12 col-sm-6 col-md-4"><div class="wh-card p-3">';
        h += '<h5 style="margin:0 0 8px;">' + esc(t.name) + '</h5>';
        h += '<table style="width:100%;font-size:13px;">';
        h += '<tr><td style="color:#6c757d;">Mode</td><td style="text-align:right;">' + MODE_LABELS[t.mode] + '</td></tr>';
        h += '<tr><td style="color:#6c757d;">Consigne</td><td style="text-align:right;">' + Number(t.setpoint).toFixed(1) + ' &deg;C</td></tr>';
        h += '<tr><td style="color:#6c757d;">Capteur</td><td style="text-align:right;">' + lbl(t.sensorLabel, t.sensorIEEE) + '</td></tr>';
        var nx = (t.actuatorsExtra || []).length;
        h += '<tr><td style="color:#6c757d;">Contacteur</td><td style="text-align:right;">' + lbl(t.actuatorLabel, t.actuatorIEEE) +
             (nx > 0 ? ' <span style="color:#888;">+' + nx + '</span>' : '') + '</td></tr>';
        h += '</table>';
        h += '<div class="btn-actions">';
        h += '<a class="btn btn-info" href="#editWaterHeater?id=' + i + '" title="Modifier">' + SVG_PEN + '</a>';
        h += '<button class="btn btn-danger wh-del" data-id="' + i + '" data-name="' + esc(t.name) + '" title="Supprimer">' + SVG_TRASH + '</button>';
        h += '</div></div></div>';
      });
      h += '</div>';
      if (!zones.length) {
        h += '<div align="center" style="color:#888;margin-top:2rem;">Aucun chauffe-eau configur&eacute;. Cliquez sur &laquo; Ajouter un chauffe-eau &raquo;.</div>';
      }
      h += '</div>';
      A.setContent(h);
      var $c = $('#page-content');
      $c.off('.whcfg');
      $c.on('click.whcfg', '.wh-del', function() {
        var id = Number($(this).data('id')), name = $(this).attr('data-name');
        if (!confirm('Supprimer le chauffe-eau "' + name + '" ?')) return;
        post('/api/waterheaters/delete', { id: id })
          .always(pageConfigWaterHeaters)
          .fail(function(xhr) { A.showAlert(errMsg(xhr), 'danger'); });
      });
    }).fail(function(xhr) {
      A.setContent('<div class="container py-4"><div class="alert alert-danger">' + errMsg(xhr, 'Configuration indisponible') + '</div></div>');
    });
  }

  // ==========================================================================
  // Formulaire d'un chauffe-eau
  // ==========================================================================
  function tic(p) {
    return "<span style='display:inline-flex;align-items:center;vertical-align:-3px;margin-right:6px;'><svg width='15' height='15' viewBox='0 0 16 16' fill='#6c757d'>" + p + '</svg></span>';
  }
  var IC_HOME = tic("<path d='M8.707 1.5a1 1 0 0 0-1.414 0L.646 8.146a.5.5 0 0 0 .708.708L2 8.207V13.5A1.5 1.5 0 0 0 3.5 15h9a1.5 1.5 0 0 0 1.5-1.5V8.207l.646.647a.5.5 0 0 0 .708-.708L13 5.793V2.5a.5.5 0 0 0-.5-.5h-1a.5.5 0 0 0-.5.5v1.293zM13 7.207V13.5a.5.5 0 0 1-.5.5h-9a.5.5 0 0 1-.5-.5V7.207l5-5z'/>");
  var IC_DROP = tic("<path d='" + ICON_DROP + "'/>");
  var IC_CLOCK = tic("<path d='M8 3.5a.5.5 0 0 0-1 0V9a.5.5 0 0 0 .252.434l3.5 2a.5.5 0 0 0 .496-.868L8 8.71z'/><path d='M8 16A8 8 0 1 0 8 0a8 8 0 0 0 0 16m7-8A7 7 0 1 1 1 8a7 7 0 0 1 14 0'/>");
  var IC_PLUG = tic("<path d='M7.5 1v7h1V1z'/><path d='M3 8.812a5 5 0 0 1 2.578-4.375l-.485-.874A6 6 0 1 0 11 3.616l-.501.865A5 5 0 1 1 3 8.812'/>");
  var IC_GEAR = tic(ICON_GEAR);

  function deviceOptions(list, current) {
    var o = "<option value=''>-- Aucun --</option>";
    (list || []).forEach(function(d) {
      o += "<option value='" + esc(d.ieee) + "'" + (d.ieee === current ? ' selected' : '') + '>' + esc(d.label) + ' (' + esc(d.ieee) + ')</option>';
    });
    return o;
  }
  function deviceChecks(list, prefix, checked, skip, empty) {
    var h = '';
    (list || []).forEach(function(d) {
      if (skip && d.ieee === skip) return;
      var on = (checked || []).indexOf(d.ieee) >= 0;
      h += "<div class='form-check'><input class='form-check-input wh-" + prefix + "' type='checkbox' value='" + esc(d.ieee) +
           "' id='" + prefix + '_' + esc(d.ieee) + "'" + (on ? ' checked' : '') + '>' +
           "<label class='form-check-label' for='" + prefix + '_' + esc(d.ieee) + "'>" + esc(d.label) + ' (' + esc(d.ieee) + ')</label></div>';
    });
    return h || "<p style='color:#888;font-size:12px;'>" + empty + '</p>';
  }
  function tariffChecks(opts, sel) {
    if (!opts.hasZlinky) return "<p style='color:#888;font-size:12px;'>Configurez d'abord un compteur ZLinky (Config &rarr; Energie).</p>";
    var cur = ',' + (sel || '') + ',', h = '';
    (opts.tariffs || []).forEach(function(p) {
      h += "<div class='form-check'><input class='form-check-input wh-tp' type='checkbox' value='" + p.id + "' id='whtp" + p.id + "'" +
           (cur.indexOf(',' + p.id + ',') >= 0 ? ' checked' : '') + '>' +
           "<label class='form-check-label' for='whtp" + p.id + "'>" + esc(p.label) + '</label></div>';
    });
    return h || "<p style='color:#888;font-size:12px;'>P&eacute;riodes tarifaires non encore d&eacute;tect&eacute;es.</p>";
  }

  // Plages "[jours/]HH:MM-HH:MM" (voir page_thermostat.js pour le detail du format)
  var DAY_LETTERS = ['L', 'M', 'M', 'J', 'V', 'S', 'D'];
  var DAY_NAMES = ['Lundi', 'Mardi', 'Mercredi', 'Jeudi', 'Vendredi', 'Samedi', 'Dimanche'];
  function parseSchedule(s) {
    var out = [];
    String(s || '').split(',').forEach(function(seg) {
      seg = seg.trim();
      if (!seg) return;
      var days = '1234567', k = seg.indexOf('/');
      if (k >= 0) { days = seg.substring(0, k).replace(/[^1-7]/g, '') || '1234567'; seg = seg.substring(k + 1).trim(); }
      var m = seg.match(/^(\d{1,2}):(\d{2})\s*-\s*(\d{1,2}):(\d{2})$/);
      if (!m) return;
      var f = function(h, mn) { return ('0' + h).slice(-2) + ':' + mn; };
      out.push({ days: days, a: f(m[1], m[2]), b: f(m[3], m[4]) });
    });
    return out;
  }
  function schedRowHtml(p) {
    var h = "<div class='sched-row d-flex flex-wrap align-items-center gap-2 mb-2' style='border:1px solid #dee2e6;border-radius:8px;padding:8px;'>";
    h += "<div class='btn-group btn-group-sm' role='group'>";
    for (var d = 1; d <= 7; d++) {
      var on = p.days.indexOf(String(d)) >= 0;
      h += "<button type='button' class='btn btn-outline-primary sched-day" + (on ? ' active' : '') + "' data-d='" + d + "' title='" + DAY_NAMES[d - 1] + "' style='min-width:32px;'>" + DAY_LETTERS[d - 1] + '</button>';
    }
    h += '</div>';
    h += "<input type='time' class='form-control form-control-sm sched-a' style='width:auto;' value='" + esc(p.a) + "'>";
    h += '<span>&agrave;</span>';
    h += "<input type='time' class='form-control form-control-sm sched-b' style='width:auto;' value='" + esc(p.b) + "'>";
    h += "<button type='button' class='btn btn-sm btn-outline-danger sched-del ms-auto' title='Supprimer la plage'>&times;</button>";
    return h + '</div>';
  }
  function serializeSchedule() {
    var segs = [];
    $('#whSchedList .sched-row').each(function() {
      var $r = $(this);
      var a = $r.find('.sched-a').val(), b = $r.find('.sched-b').val();
      if (!/^\d{2}:\d{2}$/.test(a) || !/^\d{2}:\d{2}$/.test(b)) return;
      var days = $r.find('.sched-day.active').map(function() { return $(this).data('d'); }).get().join('');
      if (!days) return;
      segs.push((days === '1234567' ? '' : days + '/') + a + '-' + b);
    });
    return segs.join(',');
  }

  function pageEditWaterHeater() {
    var p = A.params();
    var idParam = p.id || 'new';
    $.when($.getJSON('/api/waterheaters/config'), $.getJSON('/api/waterheaters/options')).done(function(r1, r2) {
      var cfg = r1[0] || {}, opts = r2[0] || {};
      var zones = cfg.zones || [];
      var id = idParam === 'new' ? -1 : parseInt(idParam, 10);
      var t = null;
      for (var i = 0; i < zones.length; i++) if (zones[i].id === id) t = zones[i];
      var existing = !!t;
      if (!existing) {
        id = -1;
        t = $.extend({ name: '', sensorIEEE: '', actuatorIEEE: '', actuatorsExtra: [], schedule: '', tariffPeriods: '' }, cfg.defaults || {});
      }
      renderEditForm(id, existing, t, opts);
    }).fail(function(xhr) {
      A.setContent('<div class="container py-4"><div class="alert alert-danger">' + errMsg(xhr, 'Configuration indisponible') + '</div></div>');
    });
  }

  function renderEditForm(id, existing, t, opts) {
    var h = "<div class='container' style='max-width:680px;margin-bottom:2rem;'>";
    h += existing ? "<h3 class='mt-3'>Modifier : " + esc(t.name) + '</h3>' : "<h3 class='mt-3'>Nouveau chauffe-eau</h3>";
    h += "<form id='whForm' autocomplete='off'>";

    // Bloc 1 : identite
    h += "<div class='card p-4 mb-3'>";
    h += "<label class='form-label fw-bold'>" + IC_HOME + 'Nom</label>';
    h += "<input class='form-control' id='whName' maxlength='31' placeholder='Ex : Ballon' value='" + esc(t.name) + "'>";
    h += '</div>';

    // Bloc 2 : regulation
    h += "<div class='card p-4 mb-3'><h5 class='mb-3'>R&eacute;gulation</h5>";
    h += "<label class='form-label'>Mode</label><select class='form-select mb-3' id='whModeSel'>" +
         "<option value='0'" + (t.mode == 0 ? ' selected' : '') + '>Arr&ecirc;t</option>' +
         "<option value='1'" + (t.mode == 1 ? ' selected' : '') + '>Marche forc&eacute;e</option>' +
         "<option value='2'" + (t.mode == 2 ? ' selected' : '') + '>Programme (plages horaires / tarif Linky)</option></select>';
    h += "<label class='form-label'>" + IC_DROP + 'Consigne (&deg;C)</label>';
    h += "<input class='form-control' type='number' step='1' min='0' max='70' id='whSetpoint' value='" + Number(t.setpoint).toFixed(1) + "'>";
    h += "<div class='form-text'>Temp&eacute;rature cible de l'eau chaude.</div>";
    h += "<div id='whProgRow' class='mt-3'>";
    h += "<label class='form-label'>" + IC_CLOCK + 'Plages horaires</label>';
    h += "<div id='whSchedList'></div>";
    h += "<button type='button' class='btn btn-sm btn-outline-primary' id='whSchedAdd'>+ Ajouter une plage</button>";
    h += "<div class='form-text mb-3'>R&eacute;gule uniquement pendant ces plages (heures creuses). Laissez vide pour ne pas restreindre par horaire.</div>";
    h += "<label class='form-label'>P&eacute;riodes tarifaires actives</label>" +
         "<div style='border:1px solid #dee2e6;border-radius:8px;padding:10px;'>" + tariffChecks(opts, t.tariffPeriods) + '</div>' +
         "<div class='form-text'>R&eacute;gule aussi pendant les p&eacute;riodes <b>coch&eacute;es</b> (en plus des plages horaires ci-dessus).</div>";
    h += '</div></div>';

    // Bloc 3 : capteur / contacteur
    h += "<div class='card p-4 mb-3'><h5 class='mb-3'>Capteur et contacteur</h5>";
    h += "<label class='form-label'>" + IC_DROP + "Sonde de temp&eacute;rature <span class='text-muted'>(optionnel)</span></label><select class='form-select' id='whSensor'>" + deviceOptions(opts.sensors, t.sensorIEEE) + '</select>';
    h += "<div class='form-text mb-3'>Si aucune sonde n'est choisie, le chauffe-eau fonctionne en pilotage simple (le contacteur suit le mode / la plage horaire, sans r&eacute;gulation fine).</div>";
    h += "<label class='form-label'>" + IC_PLUG + "Contacteur / relais</label><select class='form-select' id='whActSel'>" + deviceOptions(opts.actuators, t.actuatorIEEE) + '</select>';
    h += "<div class='form-text mb-3'>Prise ou relais qui alimente la r&eacute;sistance.</div>";
    h += "<label class='form-label'>" + IC_PLUG + "Contacteurs suppl&eacute;mentaires <span class='text-muted'>(optionnel)</span></label>" +
         "<div style='border:1px solid #dee2e6;border-radius:8px;padding:10px;'>" +
         deviceChecks(opts.actuators, 'xact', t.actuatorsExtra, null, 'Aucun contacteur suppl&eacute;mentaire disponible.') + '</div></div>';

    // Bloc 4 : avance (TPI)
    h += "<details class='card p-4 mb-3'>" +
         "<summary style='cursor:pointer;font-weight:600;font-size:17px;'>" + IC_GEAR + 'Param&egrave;tres avanc&eacute;s de r&eacute;gulation (TPI)</summary>' +
         "<p class='form-text mt-2'>Sans effet si aucune sonde n'est choisie ci-dessus (pilotage simple). Les valeurs par d&eacute;faut conviennent &agrave; la plupart des installations.</p>";
    h += "<label class='form-label mt-2'>Type d'&eacute;quipement</label>";
    h += "<select class='form-select mb-1' id='whEquipPreset'>" +
         "<option value='resistance' selected>R&eacute;sistance (chauffage direct)</option>" +
         "<option value='pac'>Chauffe-eau thermo-dynamique</option></select>";
    h += "<div class='form-text mb-3'>Pr&eacute;-remplit les champs ci-dessous avec des valeurs adapt&eacute;es : une PAC fonctionne en tout-ou-rien (compresseur), elle a besoin d'un cycle plus long et d'un temps ON/OFF minimal plus grand pour limiter les courts cycles qui l'usent pr&eacute;matur&eacute;ment. Les champs restent modifiables ensuite.</div>";
    h += "<label class='form-label mt-2'>Dur&eacute;e du cycle (secondes)</label>";
    h += "<input class='form-control' type='number' id='whCycle' value='" + t.tpiCycleSec + "'>";
    h += "<div class='form-text mb-3'>Recommand&eacute; : <b>900 s (15 min)</b>.</div>";
    h += "<label class='form-label'>Kp &mdash; gain proportionnel</label>";
    h += "<input class='form-control' type='number' step='0.1' id='whKp' value='" + Number(t.tpiKp).toFixed(2) + "'>";
    h += "<div class='form-text mb-3'>D&eacute;faut : <b>0,8</b>.</div>";
    h += "<label class='form-label'>Ki &mdash; gain int&eacute;gral</label>";
    h += "<input class='form-control' type='number' step='0.05' id='whKi' value='" + Number(t.tpiKi).toFixed(2) + "'>";
    h += "<div class='form-text mb-3'>D&eacute;faut : <b>0,1</b>.</div>";
    h += "<label class='form-label'>Temps ON minimal (secondes)</label>";
    h += "<input class='form-control' type='number' id='whMinOn' value='" + t.minOnSec + "'>";
    h += "<div class='form-text mb-3'>D&eacute;faut : <b>180 s</b>.</div>";
    h += "<label class='form-label'>Temps OFF minimal (secondes)</label>";
    h += "<input class='form-control' type='number' id='whMinOff' value='" + t.minOffSec + "'>";
    h += "<div class='form-text mb-3'>D&eacute;faut : <b>180 s</b>.</div>";
    h += "<label class='form-label'>D&eacute;lai avant capteur &laquo; HS &raquo; (minutes)</label>";
    h += "<input class='form-control' type='number' id='whTimeout' value='" + Math.round(t.sensorTimeoutSec / 60) + "'>";
    h += "<div class='form-text'>D&eacute;faut : <b>180 min</b>.</div></details>";

    // Boutons
    h += "<div style='display:flex;gap:8px;'>" +
         "<button type='submit' class='btn btn-primary' style='flex:1;'>Enregistrer</button>" +
         "<a class='btn btn-secondary' style='flex:1;' href='#configWaterHeaters'>Annuler</a>" +
         '</div></form></div>';
    A.setContent(h);

    function upMode() { $('#whProgRow').toggle($('#whModeSel').val() === '2'); }
    $('#whModeSel').on('change', upMode);
    upMode();

    var EQUIP_PRESETS = {
      resistance: { cycle: 900, kp: 0.8, ki: 0.1, minOn: 180, minOff: 180 },
      pac: { cycle: 1800, kp: 1.0, ki: 0.0, minOn: 600, minOff: 600 }
    };
    $('#whEquipPreset').on('change', function() {
      var p = EQUIP_PRESETS[$(this).val()];
      if (!p) return;
      $('#whCycle').val(p.cycle);
      $('#whKp').val(p.kp.toFixed(2));
      $('#whKi').val(p.ki.toFixed(2));
      $('#whMinOn').val(p.minOn);
      $('#whMinOff').val(p.minOff);
    });

    parseSchedule(t.schedule).forEach(function(p) { $('#whSchedList').append(schedRowHtml(p)); });
    $('#whSchedAdd').on('click', function() {
      $('#whSchedList').append(schedRowHtml({ days: '1234567', a: '22:00', b: '06:00' }));
    });
    $('#whSchedList').on('click', '.sched-day', function() { $(this).toggleClass('active'); });
    $('#whSchedList').on('click', '.sched-del', function() { $(this).closest('.sched-row').remove(); });

    $('#whForm').on('submit', function(e) {
      e.preventDefault();
      var act = $('#whActSel').val();
      var body = {
        id: existing ? id : 'new',
        name: $('#whName').val(),
        mode: parseInt($('#whModeSel').val(), 10),
        setpoint: parseFloat($('#whSetpoint').val()),
        schedule: serializeSchedule(),
        tariffPeriods: $('.wh-tp:checked').map(function() { return this.value; }).get().join(','),
        sensorIEEE: $('#whSensor').val(),
        actuatorIEEE: act,
        actuatorsExtra: $('.wh-xact:checked').map(function() { return this.value; }).get()
          .filter(function(v) { return v !== act; }).slice(0, 4),
        tpiCycleSec: parseInt($('#whCycle').val(), 10),
        tpiKp: parseFloat($('#whKp').val()),
        tpiKi: parseFloat($('#whKi').val()),
        minOnSec: parseInt($('#whMinOn').val(), 10),
        minOffSec: parseInt($('#whMinOff').val(), 10),
        sensorTimeoutSec: parseInt($('#whTimeout').val(), 10) * 60
      };
      ['setpoint', 'tpiKp', 'tpiKi', 'tpiCycleSec', 'minOnSec', 'minOffSec', 'sensorTimeoutSec'].forEach(function(k) {
        if (!isFinite(body[k])) delete body[k];
      });
      var $b = $(this).find('button[type=submit]').prop('disabled', true);
      post('/api/waterheaters/save', body).done(function() {
        window.location.hash = '#configWaterHeaters';
      }).fail(function(xhr) {
        $b.prop('disabled', false);
        A.showAlert(errMsg(xhr, 'Enregistrement impossible'), 'danger');
      });
    });
  }

  A.registerPage('waterheaters', pageWaterHeaters);
  A.registerPage('configWaterHeaters', pageConfigWaterHeaters);
  A.registerPage('editWaterHeater', pageEditWaterHeater);
})();
