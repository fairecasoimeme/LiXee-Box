// ============================================================================
// LiXee MatterBridge - thermostats virtuels (portage LiXee-Box v2.25)
//   #thermostats        Mesures -> Thermostats : cadrans des zones
//   #configThermostats  Config -> Thermostat : fiches des zones
//   #editThermostat     formulaire d'une zone (?id=N ou ?id=new)
// Plus le glissement Thermostats <-> Energie <-> Appareils sur mobile.
// API : /api/thermostats* (components/webserver/web_thermostat.cpp)
// ============================================================================
(function() {
  'use strict';
  var A = window.LixeeApp;
  if (!A) return;
  var esc = A.escHtml;

  function errMsg(xhr, fallback) {
    return A.apiErrorMsg ? A.apiErrorMsg(xhr, fallback) : esc(fallback || 'Erreur');
  }
  function post(url, obj) { return A.postJSON(url, obj); }

  // ==========================================================================
  // Mesures -> Thermostats (handleThermostats de la LiXee-Box)
  // ==========================================================================
  var tZones = [];

  function p2c(cx, cy, r, d) { var a = (d - 90) * Math.PI / 180; return [cx + r * Math.cos(a), cy + r * Math.sin(a)]; }
  function arc(cx, cy, r, a0, a1) {
    var s = p2c(cx, cy, r, a0), e = p2c(cx, cy, r, a1);
    var f = (a1 - a0) <= 180 ? '0' : '1';
    return 'M' + s[0].toFixed(1) + ' ' + s[1].toFixed(1) + ' A' + r + ' ' + r + ' 0 ' + f + ' 1 ' + e[0].toFixed(1) + ' ' + e[1].toFixed(1);
  }

  var ICON_FIRE = "M8 16c3.314 0 6-2 6-5.5 0-1.5-.5-4-2.5-6 .25 1.5-1.25 2-1.25 2C11 4 9 .5 6 0c.357 2 .5 4-2 6-1.25 1-2 2.729-2 4.5C2 14 4.686 16 8 16m0-1c-1.657 0-3-1-3-2.75 0-.75.25-2 1.25-3C6.125 10 7 10.5 7 10.5c-.375-1.25.5-3.25 2-3.5-.179 1-.25 2 1 3 .625.5 1 1.364 1 2.25C12 14 10.657 15 8 15";
  var ICON_SNOW = "M8 16a.5.5 0 0 1-.5-.5v-1.293l-.646.647a.5.5 0 0 1-.707-.708L7.5 12.793V8.866l-3.4 1.963-.496 1.85a.5.5 0 1 1-.966-.26l.237-.882-1.12.646a.5.5 0 0 1-.5-.866l1.12-.646-.884-.237a.5.5 0 1 1 .26-.966l1.848.495L6.5 8 3.102 6.037l-1.849.495a.5.5 0 0 1-.259-.966l.883-.237-1.12-.646a.5.5 0 1 1 .5-.866l1.12.646-.237-.883a.5.5 0 0 1 .966-.259l.495 1.849L7.5 7.134V3.207L6.147 1.854a.5.5 0 1 1 .707-.708l.646.647V.5a.5.5 0 0 1 1 0v1.293l.647-.647a.5.5 0 1 1 .707.708L8.5 3.207v3.927l3.4-1.963.495-1.849a.5.5 0 1 1 .966.259l-.236.883 1.12-.646a.5.5 0 0 1 .5.866l-1.12.646.883.237a.5.5 0 1 1-.26.966l-1.848-.495L8.866 8l3.398 1.963 1.849-.495a.5.5 0 0 1 .259.966l-.883.237 1.12.646a.5.5 0 0 1-.5.866l-1.12-.646.236.883a.5.5 0 1 1-.966.259l-.495-1.849L8.5 8.866v3.927l1.354 1.353a.5.5 0 0 1-.708.708L8.5 14.207V15.5a.5.5 0 0 1-.5.5z";
  var ICON_PERSON = "M8 8a3 3 0 1 0 0-6 3 3 0 0 0 0 6m2-3a2 2 0 1 1-4 0 2 2 0 0 1 4 0m4 8c0 1-1 1-1 1H3s-1 0-1-1 1-4 6-4 6 3 6 4m-1-.004c-.001-.246-.154-.986-.832-1.664C11.516 10.68 10.289 10 8 10s-3.516.68-4.168 1.332c-.678.678-.83 1.418-.832 1.664z";
  var ICON_DOOR = "<path d='M3 2a1 1 0 0 1 1-1h8a1 1 0 0 1 1 1v13h1.5a.5.5 0 0 1 0 1h-13a.5.5 0 0 1 0-1H3zm1 13h8V2H4z'/><path d='M9 9a1 1 0 1 0 2 0 1 1 0 0 0-2 0'/>";
  var ICON_GEAR = "<path d='M8 4.754a3.246 3.246 0 1 0 0 6.492 3.246 3.246 0 0 0 0-6.492M5.754 8a2.246 2.246 0 1 1 4.492 0 2.246 2.246 0 0 1-4.492 0'/><path d='M9.796 1.343c-.527-1.79-3.065-1.79-3.592 0l-.094.319a.873.873 0 0 1-1.255.52l-.292-.16c-1.64-.892-3.433.902-2.54 2.541l.159.292a.873.873 0 0 1-.52 1.255l-.319.094c-1.79.527-1.79 3.065 0 3.592l.319.094a.873.873 0 0 1 .52 1.255l-.16.292c-.892 1.64.901 3.434 2.541 2.54l.292-.159a.873.873 0 0 1 1.255.52l.094.319c.527 1.79 3.065 1.79 3.592 0l.094-.319a.873.873 0 0 1 1.255-.52l.292.16c1.64.893 3.434-.902 2.54-2.541l-.159-.292a.873.873 0 0 1 .52-1.255l.319-.094c1.79-.527 1.79-3.065 0-3.592l-.319-.094a.873.873 0 0 1-.52-1.255l.16-.292c.893-1.64-.902-3.433-2.541-2.54l-.292.159a.873.873 0 0 1-1.255-.52zm-2.633.283c.246-.835 1.428-.835 1.674 0l.094.319a1.873 1.873 0 0 0 2.693 1.115l.291-.16c.764-.415 1.6.42 1.184 1.185l-.159.292a1.873 1.873 0 0 0 1.116 2.692l.318.094c.835.246.835 1.428 0 1.674l-.319.094a1.873 1.873 0 0 0-1.115 2.693l.16.291c.415.764-.42 1.6-1.185 1.184l-.291-.159a1.873 1.873 0 0 0-2.693 1.116l-.094.318c-.246.835-1.428.835-1.674 0l-.094-.319a1.873 1.873 0 0 0-2.692-1.115l-.292.16c-.764.415-1.6-.42-1.184-1.185l.159-.291A1.873 1.873 0 0 0 1.945 8.93l-.319-.094c-.835-.246-.835-1.428 0-1.674l.319-.094A1.873 1.873 0 0 0 3.06 4.377l-.16-.292c-.415-.764.42-1.6 1.185-1.184l.292.159a1.873 1.873 0 0 0 2.692-1.115z'/>";

  function pageThermostats() {
    A.setContent(
      '<div class="container">' +
        '<div style="margin:1rem 0;"><h3>Thermostats</h3></div>' +
        '<div class="row" id="thermoCards"></div>' +
        '<div id="thermoEmpty" style="display:none;color:#888;">Aucune zone configur&eacute;e. Rendez-vous dans <b>Config &rarr; Thermostat</b> pour en cr&eacute;er une.</div>' +
      '</div>');
    tLoad();
    A.pageInterval(tLoad, 5000);
  }

  function tLoad() {
    $.getJSON('/api/thermostats').done(tRender);
  }

  function tRender(z) {
    var c = document.getElementById('thermoCards');
    if (!c) return;
    tZones = z || [];
    document.getElementById('thermoEmpty').style.display = tZones.length ? 'none' : 'block';
    var h = '';
    for (var i = 0; i < tZones.length; i++) h += zoneCard(tZones[i]);
    c.innerHTML = h;
  }

  function zoneCard(t) {
    var active = !!t.actualOn;
    var tv = (t.temp === null || t.temp === undefined) ? null : t.temp;
    var mc = t.heating ? (active ? '#e74c3c' : '#f1948a') : (active ? '#2980b9' : '#aed6f1');
    var mic = active ? (t.heating ? '#e67e22' : '#2980b9') : '#adb5bd';
    var icon = "<svg width='18' height='18' viewBox='0 0 16 16' fill='" + mic + "'><path d='" + (t.heating ? ICON_FIRE : ICON_SNOW) + "'/></svg>";

    // Le forcage s'affiche avant "Capteur HS" : il s'applique meme capteur HS
    var hs = t.sensorValid ? '' : ' &middot; capteur HS';
    var stat;
    if (!t.enabled) {
      stat = '<span style="color:#999;">Zone d&eacute;sactiv&eacute;e</span>';
    } else if (t.forceMode == 1) {
      stat = '<span style="color:#8e44ad;">Marche forc&eacute;e' + hs + '</span>';
    } else if (t.forceMode == 2) {
      stat = '<span style="color:#8e44ad;">Arr&ecirc;t forc&eacute;' + hs + '</span>';
    } else if (!t.sensorValid) {
      var rs = ['', 'capteur introuvable', 'aucune temp&eacute;rature lue', 'aucune trame depuis ' + Math.round(t.sensorAge / 60) + ' min'][t.sensorReason] || '';
      stat = '<span style="color:#e74c3c;" title="' + rs + '">Capteur HS' + (rs ? ' (' + rs + ')' : '') + '</span>';
    } else if (t.windowOpen) {
      stat = '<span style="color:#e67e22;">Fen&ecirc;tre ouverte</span>';
    } else if (t.hasPresence && !t.occupied) {
      stat = '<span style="color:#999;">Absence</span>';
    } else if (!t.schedActive) {
      stat = '<span style="color:#9b59b6;">' + (t.operMode == 2 ? ('Tarif ' + esc(t.tariffNow || '')) : 'Hors plage') + '</span>';
    } else if (t.output && !t.actualOn) {
      stat = '<span style="color:#f39c12;">D&eacute;marrage&hellip;</span>';
    } else if (!t.output && t.actualOn) {
      stat = '<span style="color:#f39c12;">Arr&ecirc;t&hellip;</span>';
    } else {
      var act = t.heating ? 'Chauffe' : 'Rafra&icirc;chit';
      stat = t.actualOn ? act + ' (' + t.duty + '%)' : 'R&eacute;gule';
    }

    var bs = 'display:inline-flex;align-items:center;gap:4px;background:#f5f6f8;border-radius:14px;padding:3px 8px;font-size:11px;color:#495057;white-space:nowrap;';
    var ig = '#6c757d';
    var icP = "<svg width='13' height='13' viewBox='0 0 16 16' fill='" + ig + "'><path d='" + ICON_PERSON + "'/></svg>";
    var icD = "<svg width='13' height='13' viewBox='0 0 16 16' fill='" + ig + "'>" + ICON_DOOR + "</svg>";
    var b = '';
    if (t.hasPresence) b += "<span style='" + bs + "'>" + icP + '<span>' + (t.occupied ? 'Pr&eacute;sent' : 'Absent') + '</span></span>';
    if (t.openCount > 0) b += "<span style='" + bs + "'>" + icD + '<span>' + (t.windowOpen ? 'Ouvert' : 'Ferm&eacute;') + '</span></span>';

    var mn = 5, mx = 35;
    function ang(v) { return 225 + (Math.max(mn, Math.min(mx, v)) - mn) / (mx - mn) * 270; }
    var cur = (tv === null) ? null : p2c(105, 105, 86, ang(tv));
    var lo = p2c(105, 105, 104, 225), hi = p2c(105, 105, 104, 495);
    var prog = "<path d='" + arc(105, 105, 86, 225, ang(t.setpoint)) + "' fill='none' stroke='" + mc + "' stroke-width='14' stroke-linecap='round'/>";
    var sa = ang(t.setpoint), dz = '';
    // Animation seulement si l'appareil rapproche la temperature de la consigne
    if (active && tv !== null && Math.abs(tv - t.setpoint) > 0.2 && (t.heating ? (tv < t.setpoint) : (tv > t.setpoint))) {
      var ca = ang(tv);
      var a0 = Math.min(sa, ca), a1 = Math.max(sa, ca);
      var off = (ca < sa) ? '32;0' : '0;32';   // flux de la temperature actuelle vers la consigne
      var g0 = t.heating ? '#ff6b35' : '#1e88e5', g1 = t.heating ? '#ffd166' : '#80d8ff';
      dz = "<defs><linearGradient id='dg" + t.id + "' gradientUnits='userSpaceOnUse' x1='10' y1='10' x2='200' y2='200'><stop offset='0' stop-color='" + g0 + "'/><stop offset='1' stop-color='" + g1 + "'/></linearGradient></defs>" +
           "<path d='" + arc(105, 105, 86, a0, a1) + "' fill='none' stroke='url(#dg" + t.id + ")' stroke-width='14' stroke-linecap='round' stroke-dasharray='5 11'>" +
           "<animate attributeName='stroke-dashoffset' values='" + off + "' dur='1.4s' repeatCount='indefinite'/>" +
           "<animate attributeName='opacity' values='0.55;1;0.55' dur='1.8s' repeatCount='indefinite'/></path>";
    }
    var dot = (cur === null) ? '' : ("<circle cx='" + cur[0].toFixed(1) + "' cy='" + cur[1].toFixed(1) + "' r='7' fill='#fff' stroke='#495057' stroke-width='3'/>");

    var h = "<div class='col-lg-4 col-md-6 col-12'><div class='card' style='padding:18px;margin-bottom:1rem;position:relative;'>";
    h += "<div style='display:flex;align-items:center;justify-content:space-between;'><h5 style='margin:0;font-size:17px;'>" + esc(t.name) + '</h5>';
    h += "<a href='#editThermostat?id=" + t.id + "' title='Configurer' style='line-height:0;color:#9aa0a6;'><svg width='18' height='18' viewBox='0 0 16 16' fill='currentColor'>" + ICON_GEAR + '</svg></a></div>';
    h += "<div style='position:relative;text-align:center;'><svg viewBox='0 0 210 210' style='width:100%;max-width:240px;'>";
    h += "<path d='" + arc(105, 105, 86, 225, 495) + "' fill='none' stroke='#e9ecef' stroke-width='14' stroke-linecap='round'/>" + prog + dz;
    h += dot;
    h += "<text x='" + lo[0].toFixed(0) + "' y='" + (lo[1] + 4).toFixed(0) + "' text-anchor='middle' font-size='13' fill='#adb5bd'>" + mn + '&#176;</text>';
    h += "<text x='" + hi[0].toFixed(0) + "' y='" + (hi[1] + 4).toFixed(0) + "' text-anchor='middle' font-size='13' fill='#adb5bd'>" + mx + '&#176;</text></svg>';
    h += "<div style='position:absolute;top:50%;left:0;right:0;transform:translateY(-50%);text-align:center;'>";
    h += "<div style='font-size:11px;color:#999;text-transform:uppercase;letter-spacing:.5px;'>Consigne</div>";
    h += "<div style='font-size:40px;font-weight:700;line-height:1.05;'>" + Number(t.setpoint).toFixed(1) + "<span style='font-size:18px;font-weight:400;'>&deg;C</span></div>";
    h += "<div style='margin-top:8px;line-height:0;'>" + icon + '</div>';
    h += "<div style='margin-top:4px;font-size:12px;color:#888;'>" + stat + '</div>';
    h += '</div></div>';
    h += "<div style='text-align:center;font-size:13px;color:#6c757d;margin-top:2px;'>Actuelle : <b style='color:#343a40;'>" + (tv === null ? '&mdash;' : (Number(tv).toFixed(1) + ' &deg;C')) + '</b></div>';
    h += "<div style='display:flex;justify-content:center;align-items:center;gap:24px;margin-top:8px;'>";
    h += "<button class='btn btn-outline-secondary' style='font-size:24px;width:48px;height:48px;line-height:1;padding:0;border-radius:50%;' onclick='tSet(" + t.id + ",-0.5)'>&minus;</button>";
    h += "<button class='btn btn-outline-secondary' style='font-size:24px;width:48px;height:48px;line-height:1;padding:0;border-radius:50%;' onclick='tSet(" + t.id + ",0.5)'>+</button></div>";
    if (t.reversible) {
      var hm = t.heating ? 1 : 0;
      var mb = function(x, l) {
        var on = (hm == x);
        return "<button onclick='tMode(" + t.id + ',' + x + ")' style='flex:1;border:1px solid #dee2e6;background:" + (on ? (x ? '#e67e22' : '#2980b9') : '#fff') + ';color:' + (on ? '#fff' : '#6c757d') + ";font-size:12px;padding:5px 0;cursor:pointer;'>" + l + '</button>';
      };
      h += "<div style='display:flex;border-radius:8px;overflow:hidden;margin-top:12px;'>" + mb(1, 'Chaud') + mb(0, 'Froid') + '</div>';
    }
    var fm = t.forceMode || 0;
    var fb = function(m, l) {
      var on = (fm == m);
      return "<button onclick='tForce(" + t.id + ',' + m + ")' style='flex:1;border:1px solid #dee2e6;background:" + (on ? '#6c757d' : '#fff') + ';color:' + (on ? '#fff' : '#6c757d') + ";font-size:12px;padding:5px 0;cursor:pointer;'>" + l + '</button>';
    };
    var fr = fb(0, 'Auto') + fb(1, 'Marche') + fb(2, 'Arr&ecirc;t');
    if (t.heating) {
      var gon = (t.frostMode == 1);
      fr += "<button onclick='tFrost(" + t.id + ',' + (gon ? 0 : 1) + ")' title='Bascule la consigne sur le hors-gel' style='flex:1;border:1px solid #dee2e6;background:" + (gon ? '#5dade2' : '#fff') + ';color:' + (gon ? '#fff' : '#6c757d') + ";font-size:12px;padding:5px 0;cursor:pointer;'>Hors-gel</button>";
    }
    h += "<div style='display:flex;border-radius:8px;overflow:hidden;margin-top:8px;'>" + fr + '</div>';
    h += "<div style='display:flex;flex-wrap:nowrap;justify-content:center;align-items:center;gap:6px;margin-top:10px;overflow-x:auto;'>" + b + '</div>';
    h += '</div></div>';
    return h;
  }

  function zoneById(id) {
    for (var i = 0; i < tZones.length; i++) if (tZones[i].id === id) return tZones[i];
    return null;
  }
  function tCall(url, obj) {
    post(url, obj).always(tLoad).fail(function(xhr) { A.showAlert(errMsg(xhr, 'Commande refusée'), 'danger'); });
  }
  window.tSet = function(id, d) {
    var z = zoneById(id), s = z ? z.setpoint : 18;
    var v = Math.round((s + d) * 2) / 2;
    if (v < 0) v = 0;
    if (v > 40) v = 40;
    tCall('/api/thermostats/setpoint', { id: id, value: v });
  };
  // Forcage d'une zone desactivee : refuse par le pont, on le dit
  window.tForce = function(id, m) {
    var z = zoneById(id);
    if (z && !z.enabled) {
      alert('Cette zone est désactivée : activez-la dans Config → Thermostat pour la mettre en marche ou à l\'arrêt.');
      return;
    }
    tCall('/api/thermostats/force', { id: id, mode: m });
  };
  window.tMode = function(id, h) { tCall('/api/thermostats/mode', { id: id, heat: h }); };
  window.tFrost = function(id, on) { tCall('/api/thermostats/frost', { id: id, on: on }); };

  // ==========================================================================
  // Config -> Thermostat (handleConfigThermostats)
  // ==========================================================================
  var CFG_CSS = '<style>' +
    '.config-card{background:#fff;border:none;border-radius:12px;box-shadow:0 2px 8px rgba(0,0,0,0.08);transition:transform .2s,box-shadow .2s;height:100%}' +
    '.config-card:hover{transform:translateY(-2px);box-shadow:0 4px 16px rgba(0,0,0,0.12)}' +
    '.config-card table td{padding:6px 4px;border:none}' +
    '.config-card .btn-actions{display:flex;flex-wrap:wrap;justify-content:flex-end;gap:6px;margin-top:12px;padding-top:12px;border-top:1px solid #e9ecef}' +
    '.config-card .btn{padding:6px 10px;display:inline-flex;align-items:center;justify-content:center}' +
    '.config-card .btn svg{width:16px;height:16px;flex-shrink:0}' +
    '</style>';
  var SVG_PLUS = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' viewBox='0 0 16 16' style='width:16px;vertical-align:-2px;'><path d='M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16'/><path d='M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4'/></svg>";
  var SVG_PEN = "<svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'><path d='M12.146.146a.5.5 0 0 1 .708 0l3 3a.5.5 0 0 1 0 .708l-10 10a.5.5 0 0 1-.168.11l-5 2a.5.5 0 0 1-.65-.65l2-5a.5.5 0 0 1 .11-.168zM11.207 2.5 13.5 4.793 14.793 3.5 12.5 1.207zm1.586 3L10.5 3.207 4 9.707V10h.5a.5.5 0 0 1 .5.5v.5h.5a.5.5 0 0 1 .5.5v.5h.293zm-9.761 5.175-.106.106-1.528 3.821 3.821-1.528.106-.106A.5.5 0 0 1 5 12.5V12h-.5a.5.5 0 0 1-.5-.5V11h-.5a.5.5 0 0 1-.468-.325'/></svg>";
  var SVG_TRASH = "<svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'><path d='M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z'/><path d='M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H6a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1h3.5a1 1 0 0 1 1 1zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z'/></svg>";

  function lbl(label, ieee) {
    return ieee ? esc(label || ieee) : '(non d&eacute;fini)';
  }

  function pageConfigThermostats() {
    $.getJSON('/api/thermostats/config').done(function(cfg) {
      var zones = (cfg && cfg.zones) || [];
      var h = CFG_CSS + '<div class="container">' +
        '<div style="display:flex;justify-content:space-between;align-items:center;margin:1rem 0;">' +
        '<h3>Configuration des thermostats</h3>';
      if (zones.length < (cfg.max || 8)) {
        h += '<a class="btn btn-primary" href="#editThermostat?id=new">' + SVG_PLUS + ' Ajouter une zone</a>';
      }
      h += '</div><div class="row g-4">';
      zones.forEach(function(t) {
        var i = t.id;
        h += '<div class="col-12 col-sm-6 col-md-4"><div class="config-card p-3">';
        h += '<div style="display:flex;align-items:center;gap:10px;margin-bottom:8px;">' +
             '<img src="img/icon_thermostat.png" height="48px" style="flex-shrink:0;">';
        h += '<h5 style="margin:0;flex:1;">' + esc(t.name) + '</h5>';
        h += '<div class="form-check form-switch" style="margin:0;padding-left:2.6em;">' +
             '<input class="form-check-input th-toggle" type="checkbox" role="switch" title="Activer / d&eacute;sactiver" ' +
             'style="width:2.5em;height:1.3em;cursor:pointer;" data-id="' + i + '"' + (t.enabled ? ' checked' : '') + '></div>';
        h += '</div>';
        h += '<table style="width:100%;font-size:13px;">';
        h += '<tr><td style="color:#6c757d;">Mode</td><td style="text-align:right;">' + (t.heating ? 'Chauffage' : 'Rafra&icirc;chissement') + '</td></tr>';
        h += '<tr><td style="color:#6c757d;">Consigne</td><td style="text-align:right;">' + Number(t.setpoint).toFixed(1) + ' &deg;C</td></tr>';
        h += '<tr><td style="color:#6c757d;">Hors-gel</td><td style="text-align:right;">' + Number(t.frostTemp).toFixed(1) + ' &deg;C</td></tr>';
        h += '<tr><td style="color:#6c757d;">Capteur</td><td style="text-align:right;">' + lbl(t.sensorLabel, t.sensorIEEE) + '</td></tr>';
        var nx = (t.actuatorsExtra || []).length;
        h += '<tr><td style="color:#6c757d;">Prise</td><td style="text-align:right;">' + lbl(t.actuatorLabel, t.actuatorIEEE) +
             (nx > 0 ? ' <span style="color:#888;">+' + nx + '</span>' : '') + '</td></tr>';
        h += '<tr><td style="color:#6c757d;">Pr&eacute;sence</td><td style="text-align:right;">' + (t.presenceIEEE ? lbl(t.presenceLabel, t.presenceIEEE) : '&mdash;') + '</td></tr>';
        h += '<tr><td style="color:#6c757d;">Ouvertures</td><td style="text-align:right;">' + (t.openSensors || []).length + '</td></tr>';
        h += '</table>';
        h += '<div class="btn-actions">';
        h += '<a class="btn btn-info" href="#editThermostat?id=' + i + '" title="Modifier">' + SVG_PEN + '</a>';
        h += '<button class="btn btn-danger th-del" data-id="' + i + '" data-name="' + esc(t.name) + '" title="Supprimer">' + SVG_TRASH + '</button>';
        h += '</div></div></div>';
      });
      h += '</div>';
      if (!zones.length) {
        h += '<div align="center" style="color:#888;margin-top:2rem;">Aucune zone configur&eacute;e. Cliquez sur &laquo; Ajouter une zone &raquo;.</div>';
      }
      h += '</div>';
      A.setContent(h);
      var $c = $('#page-content');
      $c.off('.thcfg');
      $c.on('change.thcfg', '.th-toggle', function() {
        post('/api/thermostats/toggle', { id: Number($(this).data('id')) })
          .always(pageConfigThermostats)
          .fail(function(xhr) { A.showAlert(errMsg(xhr), 'danger'); });
      });
      $c.on('click.thcfg', '.th-del', function() {
        var id = Number($(this).data('id')), name = $(this).attr('data-name');
        if (!confirm('Supprimer la zone "' + name + '" ?')) return;
        post('/api/thermostats/delete', { id: id })
          .always(pageConfigThermostats)
          .fail(function(xhr) { A.showAlert(errMsg(xhr), 'danger'); });
      });
    }).fail(function(xhr) {
      A.setContent('<div class="container py-4"><div class="alert alert-danger">' + errMsg(xhr, 'Configuration indisponible') + '</div></div>');
    });
  }

  // ==========================================================================
  // Formulaire d'une zone (handleEditThermostat / handleSaveThermostat)
  // ==========================================================================
  function tic(p) {
    return "<span style='display:inline-flex;align-items:center;vertical-align:-3px;margin-right:6px;'><svg width='15' height='15' viewBox='0 0 16 16' fill='#6c757d'>" + p + '</svg></span>';
  }
  var IC_HOME = tic("<path d='M8.707 1.5a1 1 0 0 0-1.414 0L.646 8.146a.5.5 0 0 0 .708.708L2 8.207V13.5A1.5 1.5 0 0 0 3.5 15h9a1.5 1.5 0 0 0 1.5-1.5V8.207l.646.647a.5.5 0 0 0 .708-.708L13 5.793V2.5a.5.5 0 0 0-.5-.5h-1a.5.5 0 0 0-.5.5v1.293zM13 7.207V13.5a.5.5 0 0 1-.5.5h-9a.5.5 0 0 1-.5-.5V7.207l5-5z'/>");
  var IC_THERMO = tic("<path d='M9.5 12.5a1.5 1.5 0 1 1-2-1.415V6.5a.5.5 0 0 1 1 0v4.585a1.5 1.5 0 0 1 1 1.415'/><path d='M5.5 2.5a2.5 2.5 0 0 1 5 0v7.55a3.5 3.5 0 1 1-5 0zM8 1a1.5 1.5 0 0 0-1.5 1.5v7.987l-.167.15a2.5 2.5 0 1 0 3.333 0l-.166-.15V2.5A1.5 1.5 0 0 0 8 1'/>");
  var IC_SNOW = tic("<path d='" + ICON_SNOW + "'/>");
  var IC_CLOCK = tic("<path d='M8 3.5a.5.5 0 0 0-1 0V9a.5.5 0 0 0 .252.434l3.5 2a.5.5 0 0 0 .496-.868L8 8.71z'/><path d='M8 16A8 8 0 1 0 8 0a8 8 0 0 0 0 16m7-8A7 7 0 1 1 1 8a7 7 0 0 1 14 0'/>");
  var IC_PLUG = tic("<path d='M7.5 1v7h1V1z'/><path d='M3 8.812a5 5 0 0 1 2.578-4.375l-.485-.874A6 6 0 1 0 11 3.616l-.501.865A5 5 0 1 1 3 8.812'/>");
  var IC_STOP = tic("<path d='M3.5 5A1.5 1.5 0 0 1 5 3.5h6A1.5 1.5 0 0 1 12.5 5v6a1.5 1.5 0 0 1-1.5 1.5H5A1.5 1.5 0 0 1 3.5 11z'/>");
  var IC_PERSON = tic("<path d='" + ICON_PERSON + "'/>");
  var IC_DOOR = tic(ICON_DOOR);
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
      h += "<div class='form-check'><input class='form-check-input th-" + prefix + "' type='checkbox' value='" + esc(d.ieee) +
           "' id='" + prefix + '_' + esc(d.ieee) + "'" + (on ? ' checked' : '') + '>' +
           "<label class='form-check-label' for='" + prefix + '_' + esc(d.ieee) + "'>" + esc(d.label) + ' (' + esc(d.ieee) + ')</label></div>';
    });
    return h || "<p style='color:#888;font-size:12px;'>" + empty + '</p>';
  }
  function tariffChecks(opts, sel) {
    if (!opts.hasZlinky) return "<p style='color:#888;font-size:12px;'>Configurez d'abord un compteur ZLinky (Config &rarr; Energie).</p>";
    var cur = ',' + (sel || '') + ',', h = '';
    (opts.tariffs || []).forEach(function(p) {
      h += "<div class='form-check'><input class='form-check-input th-tp' type='checkbox' value='" + p.id + "' id='tp" + p.id + "'" +
           (cur.indexOf(',' + p.id + ',') >= 0 ? ' checked' : '') + '>' +
           "<label class='form-check-label' for='tp" + p.id + "'>" + esc(p.label) + '</label></div>';
    });
    return h || "<p style='color:#888;font-size:12px;'>P&eacute;riodes tarifaires non encore d&eacute;tect&eacute;es.</p>";
  }
  function actionOptions(names, cur) {
    var h = "<option value=''>Marche/Arr&ecirc;t simple (on/off)</option>";
    (names || []).forEach(function(n) {
      h += "<option value='" + esc(n) + "'" + (n === cur ? ' selected' : '') + '>' + esc(n) + '</option>';
    });
    return h;
  }

  // Plages "[jours/]HH:MM-HH:MM" separees par des virgules ; jours 1 (lundi)
  // a 7 (dimanche), absents = tous les jours
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
    $('#schedList .sched-row').each(function() {
      var $r = $(this);
      var a = $r.find('.sched-a').val(), b = $r.find('.sched-b').val();
      if (!/^\d{2}:\d{2}$/.test(a) || !/^\d{2}:\d{2}$/.test(b)) return;
      var days = $r.find('.sched-day.active').map(function() { return $(this).data('d'); }).get().join('');
      if (!days) return;                          // plage sans jour : ignoree
      segs.push((days === '1234567' ? '' : days + '/') + a + '-' + b);
    });
    return segs.join(',');
  }

  function pageEditThermostat() {
    var p = A.params();
    var idParam = p.id || 'new';
    $.when($.getJSON('/api/thermostats/config'), $.getJSON('/api/thermostats/options')).done(function(r1, r2) {
      var cfg = r1[0] || {}, opts = r2[0] || {};
      var zones = cfg.zones || [];
      var id = idParam === 'new' ? -1 : parseInt(idParam, 10);
      var t = null;
      for (var i = 0; i < zones.length; i++) if (zones[i].id === id) t = zones[i];
      var existing = !!t;
      if (!existing) {
        id = -1;
        t = $.extend({ name: '', sensorIEEE: '', actuatorIEEE: '', actuatorsExtra: [], actionHeat: '', actionCool: '',
                       actionOff: '', presenceIEEE: '', openSensors: [], schedule: '', tariffPeriods: '' }, cfg.defaults || {});
      }
      renderEditForm(id, existing, t, opts);
    }).fail(function(xhr) {
      A.setContent('<div class="container py-4"><div class="alert alert-danger">' + errMsg(xhr, 'Configuration indisponible') + '</div></div>');
    });
  }

  function renderEditForm(id, existing, t, opts) {
    var h = "<div class='container' style='max-width:680px;margin-bottom:2rem;'>";
    h += existing ? "<h3 class='mt-3'>Modifier : " + esc(t.name) + '</h3>' : "<h3 class='mt-3'>Nouvelle zone</h3>";
    h += "<form id='thermoForm' autocomplete='off'>";

    // Bloc 1 : identite + activation
    h += "<div class='card p-4 mb-3'>";
    h += "<label class='form-label fw-bold'>" + IC_HOME + 'Nom de la zone</label>';
    h += "<input class='form-control' id='thName' maxlength='31' placeholder='Ex : Salon' value='" + esc(t.name) + "'>";
    h += "<div class='form-check form-switch mt-3' style='font-size:18px;'>" +
         "<input class='form-check-input' type='checkbox' role='switch' id='ckEn' style='width:3em;height:1.5em;cursor:pointer;'" + (t.enabled ? ' checked' : '') + '>' +
         "<label class='form-check-label' for='ckEn' style='padding-left:10px;'>Zone activ&eacute;e</label></div>";
    h += '</div>';

    // Bloc 2 : regulation
    h += "<div class='card p-4 mb-3'><h5 class='mb-3'>R&eacute;gulation</h5>" +
         "<label class='form-label'>Mode</label>" +
         "<select class='form-select mb-3' id='modeSel'>" +
         "<option value='1'" + (t.heating ? ' selected' : '') + '>Chauffage</option>' +
         "<option value='0'" + (!t.heating ? ' selected' : '') + '>Rafra&icirc;chissement</option></select>';
    h += "<label class='form-label'>" + IC_THERMO + 'Consigne (&deg;C)</label>';
    h += "<input class='form-control' type='number' step='0.5' id='thSetpoint' value='" + Number(t.setpoint).toFixed(1) + "'>";
    h += "<div class='form-text mb-3'>Temp&eacute;rature cible &agrave; maintenir dans la zone.</div>";
    h += "<div id='frostRow'><label class='form-label'>" + IC_SNOW + 'Hors-gel (&deg;C)</label>';
    h += "<input class='form-control' type='number' step='0.5' id='thFrost' value='" + Number(t.frostTemp).toFixed(1) + "'>";
    h += "<div class='form-text'>S&eacute;curit&eacute; : en mode chauffage, la zone chauffe toujours sous ce seuil, m&ecirc;me si elle est &eacute;teinte, une fen&ecirc;tre ouverte ou en cas d'absence.</div></div>";
    h += "<label class='form-label mt-3'>" + IC_CLOCK + "Fonctionnement</label><select class='form-select mb-2' id='operSel'>" +
         "<option value='0'" + (t.operMode == 0 ? ' selected' : '') + '>Toujours</option>' +
         "<option value='1'" + (t.operMode == 1 ? ' selected' : '') + '>Plages horaires</option>' +
         "<option value='2'" + (t.operMode == 2 ? ' selected' : '') + '>Selon tarif Linky</option></select>';
    h += "<div id='schedRow'><label class='form-label'>Plages horaires</label>";
    h += "<div id='schedList'></div>";
    h += "<button type='button' class='btn btn-sm btn-outline-primary' id='schedAdd'>+ Ajouter une plage</button>";
    h += "<div class='form-text'>Pour chaque plage, cochez les <b>jours</b> puis l'heure de d&eacute;but et de fin (d&eacute;but = fin : toute la journ&eacute;e ; une plage peut passer minuit). La zone r&eacute;gule uniquement pendant ces plages (le hors-gel reste actif).</div></div>";
    h += "<div id='tarifRow' style='display:none;'><label class='form-label'>P&eacute;riodes tarifaires actives</label>" +
         "<div style='border:1px solid #dee2e6;border-radius:8px;padding:10px;'>" + tariffChecks(opts, t.tariffPeriods) + '</div>' +
         "<div class='form-text'>La zone r&eacute;gule pendant les p&eacute;riodes <b>coch&eacute;es</b> (le hors-gel reste actif). " +
         'Les p&eacute;riodes propos&eacute;es d&eacute;pendent de votre abonnement Linky (Base, HC/HP, Tempo Bleu/Blanc/Rouge).</div></div>';
    h += '</div>';

    // Bloc 3 : capteurs
    h += "<div class='card p-4 mb-3'><h5 class='mb-3'>Capteurs</h5>";
    h += "<label class='form-label'>" + IC_THERMO + "Capteur de temp&eacute;rature</label><select class='form-select' id='thSensor'>" + deviceOptions(opts.sensors, t.sensorIEEE) + '</select>';
    h += "<div class='form-text mb-3'>Mesure la temp&eacute;rature de la zone (indispensable pour r&eacute;guler).</div>";
    h += "<label class='form-label'>" + IC_PLUG + "Appareil pilot&eacute; principal</label><select class='form-select' id='actSel'>" + deviceOptions(opts.actuators, t.actuatorIEEE) + '</select>';
    h += "<div class='form-text mb-3'>Prise, relais, climatiseur ou radiateur fil pilote qui chauffe / refroidit la zone. C'est lui qui d&eacute;finit les actions ci-dessous.</div>";
    h += "<label class='form-label'>" + IC_PLUG + "Prises suppl&eacute;mentaires <span class='text-muted'>(optionnel)</span></label>" +
         "<div id='xactBox' style='border:1px solid #dee2e6;border-radius:8px;padding:10px;margin-bottom:4px;'>" +
         deviceChecks(opts.actuators, 'xact', t.actuatorsExtra, null, 'Aucune prise suppl&eacute;mentaire disponible.') + '</div>';
    h += "<div class='form-text mb-3'>Prises/relais pilot&eacute;s <b>en parall&egrave;le</b> du principal (m&ecirc;me marche/arr&ecirc;t). Utile pour chauffer une zone avec plusieurs appareils.</div>";
    h += "<label class='form-label'>" + IC_THERMO + "Action Chaud</label><select class='form-select' id='aHeat'>" + actionOptions([], '') + '</select>';
    h += "<div class='form-text mb-2'>Action quand la zone doit <b>chauffer</b> (ex : <b>HEAT</b>, <b>CONFORT</b>). Laissez sur &laquo; Marche/Arr&ecirc;t simple &raquo; pour une prise on/off.</div>";
    h += "<label class='form-label'>" + IC_SNOW + "Action Froid <span class='text-muted'>(clim r&eacute;versible)</span></label><select class='form-select' id='aCool'>" + actionOptions([], '') + '</select>';
    h += "<div class='form-text mb-2'>Action quand la zone doit <b>refroidir</b> (ex : <b>COOL</b>). Renseignez <b>Chaud ET Froid</b> pour une clim r&eacute;versible (le mode sera choisissable sur la vignette).</div>";
    h += "<label class='form-label'>" + IC_STOP + "Action Arr&ecirc;t</label><select class='form-select' id='aOff'>" + actionOptions([], '') + '</select>';
    h += "<div class='form-text mb-3'>Action au repos (ex : <b>OFF</b>, <b>ARRET</b>, <b>ECO</b>).</div>";
    h += "<label class='form-label'>" + IC_PERSON + "Capteur de pr&eacute;sence <span class='text-muted'>(optionnel)</span></label><select class='form-select' id='thPresence'>" + deviceOptions(opts.presence, t.presenceIEEE) + '</select>';
    h += "<div class='form-text mb-3'>Si configur&eacute; : la chauffe est suspendue en cas d'absence (aucun mouvement depuis 30 min).</div>";
    h += "<label class='form-label'>" + IC_DOOR + "Capteurs d'ouverture <span class='text-muted'>(optionnel, plusieurs possibles)</span></label>" +
         "<div style='border:1px solid #dee2e6;border-radius:8px;padding:10px;'>" +
         deviceChecks(opts.contacts, 'open', t.openSensors, null, "Aucun capteur d'ouverture d&eacute;tect&eacute;.") + '</div>';
    h += "<div class='form-text'>Si une porte / fen&ecirc;tre est ouverte, la chauffe est suspendue.</div></div>";

    // Bloc 4 : parametres avances (TPI)
    h += "<details class='card p-4 mb-3'>" +
         "<summary style='cursor:pointer;font-weight:600;font-size:17px;'>" + IC_GEAR + 'Param&egrave;tres avanc&eacute;s de r&eacute;gulation (TPI)</summary>' +
         "<p class='form-text mt-2'>La r&eacute;gulation <b>TPI</b> (Time Proportional &amp; Integral) module le <i>temps</i> de chauffe sur un cycle pour stabiliser la temp&eacute;rature sans &agrave;-coups. Les valeurs par d&eacute;faut conviennent &agrave; la plupart des installations &mdash; ne les modifiez que si la r&eacute;gulation oscille ou r&eacute;agit mal.</p>";
    h += "<label class='form-label mt-2'>Dur&eacute;e du cycle (secondes)</label>";
    h += "<input class='form-control' type='number' id='thCycle' value='" + t.tpiCycleSec + "'>";
    h += "<div class='form-text mb-3'>Sur chaque cycle, le chauffage est actif une fraction du temps proportionnelle au besoin. <b>Plus court</b> = plus r&eacute;actif mais plus de commutations ; <b>plus long</b> = plus doux. Recommand&eacute; : <b>900 s (15 min)</b> pour un convecteur &eacute;lectrique.</div>";
    h += "<label class='form-label'>Kp &mdash; gain proportionnel</label>";
    h += "<input class='form-control' type='number' step='0.1' id='thKp' value='" + Number(t.tpiKp).toFixed(2) + "'>";
    h += "<div class='form-text mb-3'>Force de r&eacute;action &agrave; l'&eacute;cart avec la consigne. <b>Plus &eacute;lev&eacute;</b> = chauffe plus fort quand on est loin du but, mais risque d'<b>oscillations</b> autour de la consigne. D&eacute;faut : <b>0,8</b>.</div>";
    h += "<label class='form-label'>Ki &mdash; gain int&eacute;gral</label>";
    h += "<input class='form-control' type='number' step='0.05' id='thKi' value='" + Number(t.tpiKi).toFixed(2) + "'>";
    h += "<div class='form-text mb-3'>Corrige l'erreur qui <b>persiste dans le temps</b> (&eacute;vite de stagner juste sous la consigne). Trop &eacute;lev&eacute; = <b>d&eacute;passements</b> de la consigne. D&eacute;faut : <b>0,1</b>.</div>";
    h += "<label class='form-label'>Temps ON minimal (secondes)</label>";
    h += "<input class='form-control' type='number' id='thMinOn' value='" + t.minOnSec + "'>";
    h += "<div class='form-text mb-3'>Dur&eacute;e minimale d'allumage : <b>prot&egrave;ge l'appareil</b> et &eacute;vite les cycles marche/arr&ecirc;t trop rapproch&eacute;s. D&eacute;faut : <b>180 s (3 min)</b>.</div>";
    h += "<label class='form-label'>Temps OFF minimal (secondes)</label>";
    h += "<input class='form-control' type='number' id='thMinOff' value='" + t.minOffSec + "'>";
    h += "<div class='form-text mb-3'>Dur&eacute;e minimale d'extinction avant de pouvoir rallumer. D&eacute;faut : <b>180 s (3 min)</b>.</div>";
    h += "<label class='form-label'>D&eacute;lai avant capteur &laquo; HS &raquo; (minutes)</label>";
    h += "<input class='form-control' type='number' id='thTimeout' value='" + Math.round(t.sensorTimeoutSec / 60) + "'>";
    h += "<div class='form-text'>Si le capteur de temp&eacute;rature n'a envoy&eacute; <b>aucune trame</b> (temp&eacute;rature, humidit&eacute;, batterie&hellip;) depuis ce d&eacute;lai, il est consid&eacute;r&eacute; &laquo; HS &raquo; et la zone est coup&eacute;e par s&eacute;curit&eacute; (la marche forc&eacute;e reste possible). Un capteur sur pile n'&eacute;met souvent que lorsque la temp&eacute;rature varie : dans une pi&egrave;ce stable, il peut rester silencieux plus d'une heure. D&eacute;faut : <b>180 min</b>.</div>" +
         '</details>';

    // Boutons
    h += "<div style='display:flex;gap:8px;'>" +
         "<button type='submit' class='btn btn-primary' style='flex:1;'>Enregistrer</button>" +
         "<a class='btn btn-secondary' style='flex:1;' href='#configThermostats'>Annuler</a>" +
         '</div></form></div>';
    A.setContent(h);

    // Hors-gel : mode chauffage seulement ; plages / tarif selon le fonctionnement
    function upMode() { $('#frostRow').toggle($('#modeSel').val() === '1'); }
    function upOper() {
      var o = $('#operSel').val();
      $('#schedRow').toggle(o === '1');
      $('#tarifRow').toggle(o === '2');
    }
    $('#modeSel').on('change', upMode);
    $('#operSel').on('change', upOper);
    upMode();
    upOper();

    // Plages horaires : une ligne par plage (jours + debut + fin)
    parseSchedule(t.schedule).forEach(function(p) { $('#schedList').append(schedRowHtml(p)); });
    $('#schedAdd').on('click', function() {
      $('#schedList').append(schedRowHtml({ days: '12345', a: '06:00', b: '09:00' }));
    });
    $('#schedList').on('click', '.sched-day', function() { $(this).toggleClass('active'); });
    $('#schedList').on('click', '.sched-del', function() { $(this).closest('.sched-row').remove(); });

    // Listes d'actions selon l'appareil principal (deviceActions)
    function fillActions(ieee, keep) {
      var cur = keep || { h: $('#aHeat').val(), c: $('#aCool').val(), o: $('#aOff').val() };
      if (!ieee) {
        $('#aHeat').html(actionOptions([], ''));
        $('#aCool').html(actionOptions([], ''));
        $('#aOff').html(actionOptions([], ''));
        return;
      }
      $.getJSON('/api/thermostats/actions?ieee=' + encodeURIComponent(ieee)).done(function(a) {
        $('#aHeat').html(actionOptions(a, cur.h));
        $('#aCool').html(actionOptions(a, cur.c));
        $('#aOff').html(actionOptions(a, cur.o));
      });
    }
    $('#actSel').on('change', function() { fillActions(this.value); });
    fillActions(t.actuatorIEEE, { h: t.actionHeat, c: t.actionCool, o: t.actionOff });

    $('#thermoForm').on('submit', function(e) {
      e.preventDefault();
      var act = $('#actSel').val();
      var body = {
        id: existing ? id : 'new',
        name: $('#thName').val(),
        enabled: $('#ckEn').is(':checked'),
        heating: $('#modeSel').val() === '1',
        setpoint: parseFloat($('#thSetpoint').val()),
        frostTemp: parseFloat($('#thFrost').val()),
        operMode: parseInt($('#operSel').val(), 10),
        schedule: serializeSchedule(),
        tariffPeriods: $('.th-tp:checked').map(function() { return this.value; }).get().join(','),
        sensorIEEE: $('#thSensor').val(),
        actuatorIEEE: act,
        actuatorsExtra: $('.th-xact:checked').map(function() { return this.value; }).get()
          .filter(function(v) { return v !== act; }).slice(0, 4),
        actionHeat: $('#aHeat').val() || '',
        actionCool: $('#aCool').val() || '',
        actionOff: $('#aOff').val() || '',
        presenceIEEE: $('#thPresence').val(),
        openSensors: $('.th-open:checked').map(function() { return this.value; }).get().slice(0, 4),
        tpiCycleSec: parseInt($('#thCycle').val(), 10),
        tpiKp: parseFloat($('#thKp').val()),
        tpiKi: parseFloat($('#thKi').val()),
        minOnSec: parseInt($('#thMinOn').val(), 10),
        minOffSec: parseInt($('#thMinOff').val(), 10),
        sensorTimeoutSec: parseInt($('#thTimeout').val(), 10) * 60
      };
      ['setpoint', 'frostTemp', 'tpiKp', 'tpiKi', 'tpiCycleSec', 'minOnSec', 'minOffSec', 'sensorTimeoutSec'].forEach(function(k) {
        if (!isFinite(body[k])) delete body[k];
      });
      var $b = $(this).find('button[type=submit]').prop('disabled', true);
      post('/api/thermostats/save', body).done(function() {
        window.location.hash = '#configThermostats';
      }).fail(function(xhr) {
        $b.prop('disabled', false);
        A.showAlert(errMsg(xhr, 'Enregistrement impossible'), 'danger');
      });
    });
  }

  A.registerPage('thermostats', pageThermostats);
  A.registerPage('configThermostats', pageConfigThermostats);
  A.registerPage('editThermostat', pageEditThermostat);

  // ==========================================================================
  // Glissement entre pages sur mobile (HTTP_MENU_JS de la LiXee-Box) :
  // [Thermostats] <-> [Energie] <-> [Appareils]
  // ==========================================================================
  (function() {
    function route() {
      var h = (window.location.hash || '#energy').replace('#', '');
      var q = h.indexOf('?');
      return q >= 0 ? h.substring(0, q) : h;
    }
    function hasMenu(r) { return !!document.querySelector('a[href="#' + r + '"]'); }
    function targets() {
      var p = route();
      var d = hasMenu('devices') ? 'devices' : '';
      if (p === 'thermostats') return { L: 'energy', R: '' };
      if (p === 'devices') return { L: '', R: 'energy' };
      if (p === 'energy' || p === '') return { L: d, R: 'thermostats' };
      return null;
    }
    var sx = 0, sy = 0, dx = 0, dy = 0, dragging = false, locked = '', tg = null;
    function start(x, y) {
      if (window.innerWidth > 900) return;
      tg = targets();
      if (!tg) return;
      sx = x; sy = y; dx = dy = 0; dragging = true; locked = '';
    }
    function move(x, y, e) {
      if (!dragging) return;
      dx = x - sx; dy = y - sy;
      if (!locked && (Math.abs(dx) > 10 || Math.abs(dy) > 10)) locked = Math.abs(dx) > Math.abs(dy) ? 'h' : 'v';
      if (locked !== 'h') return;
      if (e && e.cancelable) e.preventDefault();
      var has = dx < 0 ? tg.L : tg.R;
      document.body.style.transition = 'none';
      document.body.style.transform = 'translateX(' + (has ? dx : dx * 0.3) + 'px)';
    }
    function end() {
      if (!dragging) return;
      dragging = false;
      var dest = dx < 0 ? tg.L : tg.R;
      if (locked === 'h' && Math.abs(dx) > 70 && Math.abs(dx) > 1.2 * Math.abs(dy) && dest) {
        document.body.style.transition = '';
        document.body.style.transform = '';
        window.location.hash = '#' + dest;
        return;
      }
      document.body.style.transition = 'transform .25s ease';
      document.body.style.transform = '';
    }
    document.addEventListener('touchstart', function(e) {
      if (e.touches.length === 1) start(e.touches[0].clientX, e.touches[0].clientY);
    }, { passive: true });
    document.addEventListener('touchmove', function(e) {
      if (e.touches.length === 1) move(e.touches[0].clientX, e.touches[0].clientY, e);
    }, { passive: false });
    document.addEventListener('touchend', end);
    document.addEventListener('touchcancel', end);
  })();
})();
