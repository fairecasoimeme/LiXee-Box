// ============================================================================
// LiXee MatterBridge - pages du module LoRa (portage LiXee-Box)
//   #configLora          appareils LoRa (fiches, lien radio, OTA, suppression)
//   #configLoraSettings  parametres reseau (canal / SF) + lecture d'attribut
//   #loraOta?slot=N      mise a jour du firmware d'un emetteur
//   #assistLora          assistant d'appairage LoRa (pendant de
//                        /assistDevice?type=lora de la Box)
// API : /api/lora/* (components/webserver/web_lora.cpp)
// ============================================================================
(function() {
  'use strict';
  var A = window.LixeeApp;
  if (!A) return;
  var esc = A.escHtml;

  // Logo LoRa (meme trace que la LiXee-Box), couleur du texte
  var LORA_VIEWBOX = '1 44 302 517';
  var LORA_PATHS = "<path d='M223.5 237.4a84 84 0 0 1 24.7 61c.1 24.9-4 47.5-21.9 66.3L224 367l-2 2.1c-16.4 15.6-40.8 23.2-63 23.9l-3.1.1c-26.2.5-51.4-5.3-71.9-22.1l-3-2.4A81 81 0 0 1 56 319c-.9-16.6-1.7-34.2 4-50l1.3-3.7a84 84 0 0 1 46.2-45.9c36.4-14.8 87-9.7 116 18m-102.8 26.2a65 65 0 0 0-11.2 49.5c2.2 14 7.5 24.3 18.4 33.5 9.6 6.7 22 7.6 33.2 6A39 39 0 0 0 187 336a82 82 0 0 0 5.4-56 47 47 0 0 0-20.8-25.3 44 44 0 0 0-50.9 8.9'/><path d='M22 483q5.5 2.1 10.6 5.2c22.4 13 47 21.4 72.2 26.6l2.9.6q22.5 4.2 45.4 4h2.4q20.4 0 40.5-3.4l2.8-.5A243 243 0 0 0 279 485q11.1 10.6 19 24c-30.3 21.2-67.7 32.4-104 38l-2.2.3c-65 10-130.8-6-186.8-39.3 0-3.6.8-4.5 3-7.3l1.8-2.6 2-2.6 2-2.6c5.2-7 5.2-7 8.2-9.9M197 58l3 .5A282 282 0 0 1 284 88l2 1c11.8 6.4 11.8 6.4 13 10-1.1 2-1.1 2-2.9 4.3l-3.9 5.2-2 2.6-5.2 6.9c-.7 1-.7 1-1.5 1.8-1.5 1.2-1.5 1.2-3.5 1q-2.9-1.2-5.6-2.8l-2-1.2-2.3-1.3A248 248 0 0 0 176 86l-3.3-.4c-47.8-4.7-96 5.7-138.2 28.5l-2.8 1.5-2.5 1.3q-3 1.4-6.2 2.1l-6.8-8.7-2-2.5-1.8-2.5-1.8-2.2L9 101l-1.8-2L6 97c1.2-3.6 2-3.8 5.2-5.6l2.6-1.5 3-1.5 3-1.6A282 282 0 0 1 197 58'/><path d='M52 443q4.5 1.7 8.8 4A195 195 0 0 0 206 464q19.3-5.4 36.7-15l2.3-1.2 2-1.1c2.5-.9 3.6-.7 6 .3 1.6 1.7 1.6 1.7 3 3.8l1.6 2.3 1.6 2.5 1.7 2.4 4.1 6c-3 3.4-6.5 5.6-10.5 7.8l-2 1a221 221 0 0 1-168.4 12A230 230 0 0 1 40 464c0-4.2.3-4.9 2.6-8.1l1.7-2.3 1.7-2.4 1.7-2.4zm200.7-308.7 3 1.6 2.9 1.5 2.5 1.3c1.9 1.3 1.9 1.3 2.9 4.3q-2.2 3.6-4.7 7l-1.4 1.8-5.7 7.6-1.2 1.6a56 56 0 0 1-12.2-5.3 180 180 0 0 0-89-22h-2.2c-29.9.1-58.8 6.6-85.4 20.6q-5.3 3-11.2 4.7l-6.6-8.8-1.9-2.5-1.8-2.5-1.6-2.2-1.1-2c1.7-5 6.5-6.3 11-8.6l3.1-1.6a217 217 0 0 1 58.2-19.3c48.1-10 99.3.7 142.4 22.8M81 404l1.5.7c37.4 18.1 75.8 25.7 116.3 12.2q12.3-4.5 24.2-9.9l6.4 8.8c3.6 5 3.6 5 3.6 7.2a163 163 0 0 1-80.8 20.4A154 154 0 0 1 71 421c.6-4.3 3.1-7.4 5.6-10.7z'/><path d='M233 184a60 60 0 0 1-6.1 9.8l-2.2 3c-.9 1-.9 1-1.7 2.2q-6.4-2-12.4-5.1a138 138 0 0 0-124.1 1.5c-3.5 1.6-3.5 1.6-5.5 1a25 25 0 0 1-6.2-6.8l-2.2-3C71 184 71 184 71 181c50.8-25.7 112.8-29.3 162 3'/>";
  function loraIcon(w, h) {
    return '<svg xmlns="http://www.w3.org/2000/svg" fill="currentColor" style="width:' + w + 'px;height:' + h +
      'px;" width="' + w + '" height="' + h + '" viewBox="' + LORA_VIEWBOX + '">' + LORA_PATHS + '</svg>';
  }
  var ICON_PEN = '<svg xmlns="http://www.w3.org/2000/svg" style="width:16px;" width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path d="M12.146.146a.5.5 0 0 1 .708 0l3 3a.5.5 0 0 1 0 .708l-10 10a.5.5 0 0 1-.168.11l-5 2a.5.5 0 0 1-.65-.65l2-5a.5.5 0 0 1 .11-.168zM11.207 2.5 13.5 4.793 14.793 3.5 12.5 1.207zm1.586 3L10.5 3.207 4 9.707V10h.5a.5.5 0 0 1 .5.5v.5h.5a.5.5 0 0 1 .5.5v.5h.293zm-9.761 5.175-.106.106-1.528 3.821 3.821-1.528.106-.106A.5.5 0 0 1 5 12.5V12h-.5a.5.5 0 0 1-.5-.5V11h-.5a.5.5 0 0 1-.468-.325"/></svg>';
  // Icone de carte absente : DeviceId, puis rien
  window._loraImgErr = function(img) {
    var alt = img.getAttribute('data-alt');
    if (alt) { img.setAttribute('data-alt', ''); img.src = alt; }
    else { img.onerror = null; img.style.display = 'none'; }
  };
  var ICON_GEAR = '<svg xmlns="http://www.w3.org/2000/svg" style="width:16px;" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"></circle><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"></path></svg>';
  var ICON_PLUS = '<svg xmlns="http://www.w3.org/2000/svg" style="width:16px;" width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path d="M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16"/><path d="M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4"/></svg>';
  var ICON_OTA = '<svg xmlns="http://www.w3.org/2000/svg" style="width:16px;" width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path fill-rule="evenodd" d="M7.646 10.854a.5.5 0 0 0 .708 0l2-2a.5.5 0 0 0-.708-.708L8.5 9.293V5.5a.5.5 0 0 0-1 0v3.793L6.354 8.146a.5.5 0 1 0-.708.708z"/><path d="M4.406 3.342A5.53 5.53 0 0 1 8 2c2.69 0 4.923 2 5.166 4.579C14.758 6.804 16 8.137 16 9.773 16 11.569 14.502 13 12.687 13H3.781C1.708 13 0 11.366 0 9.318c0-1.763 1.266-3.223 2.942-3.593.143-.863.698-1.723 1.464-2.383"/></svg>';
  var ICON_TRASH = '<svg xmlns="http://www.w3.org/2000/svg" style="width:16px;" width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path d="M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z"/><path d="M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H6a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1h3.5a1 1 0 0 1 1 1zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z"/></svg>';

  // Logo MQTT (bouton MQTT Discover, comme les cartes Zigbee)
  var ICON_MQTT = '<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" fill="currentColor" style="width:16px;" width="16" height="16"><path d="M10.657 23.994h-9.45A1.212 1.212 0 0 1 0 22.788v-9.18h0.071c5.784 0 10.504 4.65 10.586 10.386Zm7.606 0h-4.045C14.135 16.246 7.795 9.977 0 9.942V6.038h0.071c9.983 0 18.121 8.044 18.192 17.956Zm4.53 0h-0.97C21.754 12.071 11.995 2.407 0 2.372v-1.16C0 0.55 0.544 0.006 1.207 0.006h7.64C15.733 2.49 21.257 7.789 24 14.508v8.291c0 0.663 -0.544 1.195 -1.207 1.195ZM16.713 0.006h6.092A1.19 1.19 0 0 1 24 1.2v5.914c-0.91 -1.242 -2.046 -2.65 -3.158 -3.762C19.588 2.11 18.122 0.987 16.714 0.005Z"/></svg>';
  // MQTT actif en mode Home Assistant (bouton MQTT Discover), lu a l'ouverture de la page
  var loraHa = false;

  var SPINNER = '<div class="text-center p-3"><div class="spinner-border" role="status"></div></div>';
  var NO_MODULE = '<div class="text-center p-4" style="font-size:20px;font-weight:bold;color:#c0392b;">Aucun module LoRa d&eacute;tect&eacute;</div>';

  function errMsg(xhr, fallback) {
    return A.apiErrorMsg ? A.apiErrorMsg(xhr, fallback) : esc(fallback || 'Erreur');
  }

  // Menu lateral Appareils / Config (HTTP_CONFIG_MENU_LORA de la Box)
  function layout(active, title, inner) {
    function btn(hash, icon, label, key) {
      return '<a href="#' + hash + '" style="width:100px;height:64px;" class="btn btn-primary mb-1' +
        (active === key ? ' disabled' : '') + '">' + icon + '<br> ' + label + '</a>&nbsp;';
    }
    return '<div class="row p-4 justify-content-md-center">' +
      '<div class="col-sm-2"><div class="btn-group-horizontal">' +
        btn('configLora', loraIcon(10, 16), 'Appareils', 'devices') +
        btn('configLoraSettings', ICON_GEAR, 'Config', 'settings') +
      '</div></div>' +
      '<div class="col-sm-10"><h4>' + title + '</h4>' + inner + '</div></div>';
  }

  function fmtTime(s) {
    if (s < 0) return '?';
    var m = Math.floor(s / 60);
    return (m ? m + ' min ' : '') + (s % 60) + ' s';
  }

  function fmtAge(age) {
    if (age < 0) return 'jamais';
    return age < 90 ? age + ' s' : Math.floor(age / 60) + ' min';
  }

  function linkyModeText(e) {
    if (!e.modeKnown) return '';
    return ((e.linkyMode & 1) ? 'Standard' : 'Historique') + ' ' + ((e.linkyMode & 2) ? 'tri' : 'mono') +
      ((e.linkyMode & 4) ? ' (production)' : '');
  }

  function hexToAscii(h) {
    var s = '';
    for (var i = 0; i + 1 < h.length; i += 2) {
      var c = parseInt(h.substr(i, 2), 16);
      if (isNaN(c) || c < 0x20 || c > 0x7E) return null;
      s += String.fromCharCode(c);
    }
    return s;
  }

  // Valeur d'un etat de template : numerique (coefficient + unite) ou texte
  // (attributs sans "type" dont la valeur hex est de l'ASCII imprimable)
  function fmtState(st) {
    var t = (st.type || '').toLowerCase();
    if (t !== 'numeric' && t !== 'float') {
      var txt = hexToAscii(st.value || '');
      if (txt !== null && txt.length) return esc(txt);
    }
    var n = parseInt(st.value, 16);
    if (isNaN(n)) return esc(st.value);
    var v = n * (st.coef || 1);
    v = Math.round(v * 1000) / 1000;
    return esc(String(v)) + (st.unit ? ' ' + esc(st.unit) : '');
  }

  function deviceName(e, dev) {
    return (dev && dev.alias) || e.alias || e.model || e.ieee;
  }

  // ==========================================================================
  // PAGE : appareils LoRa
  // ==========================================================================
  var openDetails = {};

  function pageConfigLora() {
    A.setContent(layout('devices', 'Config appareils LoRa',
      '<div class="d-flex justify-content-end">' +
        '<a class="btn btn-primary mb-1" href="#assistLora" style="width:120px;height:64px;">' + ICON_PLUS + '<br> Ajouter</a>' +
      '</div><br><div id="lora-body">' + SPINNER + '</div>'));
    $.getJSON('/api/config/mqtt')
      .done(function(c) { loraHa = !!(c && c.enableMqtt && c.HAMQTT); })
      .always(loraLoad);
    A.pageInterval(loraLoad, 10000);
    A.pageInterval(loraOtaBar, 4000);
  }

  function loraLoad() {
    $.when($.getJSON('/api/lora/status'), $.getJSON('/api/lora/devices')).done(function(s, d) {
      loraRender(s[0] || {}, d[0] || []);
      loraOtaBar();
    }).fail(function(xhr) {
      $('#lora-body').html('<div class="alert alert-danger">' + errMsg(xhr, 'Erreur de chargement') + '</div>');
    });
  }

  function loraRender(st, devs) {
    var body = $('#lora-body');
    if (!body.length) return;
    // Conserver les sections "Mesures" ouvertes d'un rafraichissement a l'autre
    body.find('details[data-slot]').each(function() {
      openDetails[$(this).attr('data-slot')] = this.open;
    });
    if (!st.detected) { body.html(NO_MODULE); return; }

    var byIeee = {};
    for (var k = 0; k < devs.length; k++) byIeee[devs[k].ieee] = devs[k];
    var em = st.emitters || [];
    if (!em.length) {
      body.html('<div class="text-center p-4" style="font-size:22px;font-weight:bold;">Aucun appareil LoRa</div>' +
        '<p class="text-center"><a href="#assistLora">Appairer un appareil</a></p>');
      return;
    }

    var row = function(label, value, style) {
      return '<tr><td style="color:#6c757d;font-weight:500;">' + label + '</td>' +
        '<td style="font-family:Courier New,monospace;text-align:right;' + (style || '') + '">' + value + '</td></tr>';
    };

    var html = '<h5>Liste des appareils</h5><div class="row g-4" style="font-size:12px;">';
    for (var i = 0; i < em.length; i++) {
      var e = em[i], dev = byIeee[e.ieee] || null;
      // Icone : modele, puis DeviceId, sinon aucune (comme les cartes Zigbee)
      var icon = 'img/icon_' + encodeURIComponent(e.model || e.deviceId || '-') + '.png';
      var iconAlt = (e.model && e.deviceId) ? 'img/icon_' + encodeURIComponent(e.deviceId) + '.png' : '';
      var fiche = '#device?ieee=' + encodeURIComponent(e.ieee);
      var devHex = e.deviceId ? ('0000' + parseInt(e.deviceId, 10).toString(16).toUpperCase()).slice(-4) : '&hellip;';
      var ageColor = (e.age < 0 || e.age > 300) ? 'color:#c0392b;' : 'color:#27ae60;';

      html += '<div class="col-12 col-sm-6 col-md-4 col-lg-3"><div class="card h-100 border-0 shadow-sm" style="border-radius:12px;">' +
        '<div class="card-header bg-white border-0" style="font-weight:600;font-size:14px;">' +
          // Fiche de l'appareil, comme /configDevice?id= de la LiXee-Box
          '<a href="' + fiche + '" style="color:#222;text-decoration:none;">' +
          '<img src="' + icon + '" height="48" class="me-2" data-alt="' + iconAlt + '" onerror="window._loraImgErr(this)">' +
          esc(deviceName(e, dev)) + '</a>' +
        '</div><div class="card-body" style="padding:12px 16px;"><table style="width:100%;font-size:12px;">' +
        row('Fabricant', 'LiXee') +
        row('Mod&egrave;le', e.model ? esc(e.model) : '<i>identification&hellip;</i>') +
        row('Device Id', devHex) +
        row('MAC', esc(e.mac)) +
        row('Firmware', e.version ? esc(e.version) : '&hellip;') +
        '<tr><td colspan="2" style="padding-top:10px;border-top:1px solid #e9ecef;color:#0f70b7;font-weight:600;">Lien LoRa</td></tr>' +
        row('RSSI', e.rx ? esc(e.rssi) + ' dBm' : '&ndash;') +
        row('SNR', e.rx ? esc(e.snr) + ' dB' : '&ndash;') +
        row('PDR', esc(e.pdr) + ' %') +
        row('Trames', esc(e.rx) + ' re&ccedil;ues / ' + esc(e.missed) + ' perdues') +
        (e.modeKnown ? row('Mode Linky', esc(linkyModeText(e))) : '') +
        row('Derni&egrave;re trame', fmtAge(e.age), ageColor) +
        '</table>';

      // Mesures : etats du template qui ont une valeur (comme un appareil Zigbee)
      var states = (dev && dev.states) || [];
      var vis = [];
      for (var s = 0; s < states.length; s++) if (states[s].visible) vis.push(states[s]);
      if (vis.length) {
        html += '<details data-slot="' + e.slot + '"' + (openDetails[e.slot] ? ' open' : '') + ' style="margin-top:10px;">' +
          '<summary style="cursor:pointer;color:#0f70b7;font-weight:600;">Mesures (' + vis.length + ')</summary>' +
          '<table class="table table-sm mb-0" style="font-size:12px;">';
        for (var v = 0; v < vis.length; v++) {
          html += '<tr><td>' + esc(vis[v].name) + '</td><td style="text-align:right;font-family:Courier New,monospace;">' +
            fmtState(vis[v]) + '</td></tr>';
        }
        html += '</table></details>';
      } else if (!e.identified) {
        html += '<div class="text-muted mt-2">Identification de l\'appareil en cours (prochaines trames)&hellip;</div>';
      }

      html += '<div id="lora-ota-' + e.slot + '" class="lora-ota" style="display:none;margin-top:10px;font-size:12px;"></div>' +
        '<div class="d-flex flex-wrap gap-1 mt-3 pt-3" style="border-top:1px solid #e9ecef;">' +
          '<a href="' + fiche + '" class="btn btn-info btn-sm text-white" title="Fiche appareil">' + ICON_PEN + '</a>' +
          (loraHa ? '<button class="btn btn-warning btn-sm lora-disc" data-ieee="' + esc(e.ieee) + '" title="MQTT Discover">' + ICON_MQTT + '</button>' : '') +
          '<a href="#loraOta?slot=' + e.slot + '" class="btn btn-warning btn-sm" title="Mise &agrave; jour du firmware (OTA)">' + ICON_OTA + '</a>' +
          '<button class="btn btn-danger btn-sm lora-del" data-slot="' + e.slot + '" title="Supprimer">' + ICON_TRASH + '</button>' +
        '</div></div></div></div>';
    }
    html += '</div>';
    body.html(html);

    body.find('.lora-disc').on('click', function() {
      var $b = $(this).prop('disabled', true);
      A.postJSON('/api/zigbee/discover', { ieee: $b.attr('data-ieee') })
        .done(function(r) { A.showAlert(esc((r && r.message) || 'Publication MQTT envoyée')); })
        .fail(function(xhr) { A.showAlert(errMsg(xhr, 'Publication MQTT impossible'), 'danger'); })
        .always(function() { $b.prop('disabled', false); });
    });
    body.find('.lora-del').on('click', function() {
      var slot = parseInt($(this).attr('data-slot'), 10);
      if (!window.confirm('Supprimer cet appareil LoRa et son appairage ?')) return;
      A.postJSON('/api/lora/remove', { slot: slot }).then(function() {
        A.showAlert('Appareil supprim&eacute;');
        loraLoad();
      }, function(xhr) { A.showAlert(errMsg(xhr, 'Erreur lors du d&eacute;sappairage'), 'danger'); });
    });
  }

  // Avancement d'une mise a jour OTA sur la fiche de l'appareil concerne
  function loraOtaBar() {
    if (!$('.lora-ota').length) return;
    $.getJSON('/api/lora/ota/status', function(d) {
      $('.lora-ota').hide();
      var running = (d.state === 'waiting' || d.state === 'sending' || d.state === 'ending');
      var el = $('#lora-ota-' + d.slot);
      // Un resultat reste affiche 10 minutes
      var show = running || ((d.state === 'done' || d.state === 'error') && d.since >= 0 && d.since < 600);
      if (!el.length || !show) return;
      var h;
      if (d.state === 'waiting') {
        h = '<div class="text-center">Mise &agrave; jour : en attente de l\'appareil en mode mise &agrave; jour...</div>' +
          '<div class="progress"><div class="progress-bar progress-bar-striped progress-bar-animated" style="width:100%"></div></div>';
      } else if (d.state === 'sending') {
        h = '<div class="text-center">Mise &agrave; jour en cours : ' + d.pct + ' % &middot; reste environ ' + fmtTime(d.eta) + '</div>' +
          '<div class="progress"><div class="progress-bar" style="width:' + d.pct + '%"></div></div>' +
          '<div class="text-center text-muted">bloc ' + d.block + ' / ' + d.total + ' &middot; ' + d.retries + ' r&eacute;&eacute;mission(s)</div>';
      } else if (d.state === 'ending') {
        h = '<div class="text-center">Mise &agrave; jour : v&eacute;rification du firmware par l\'appareil...</div>';
      } else if (d.state === 'done') {
        h = '<div class="text-center" style="color:#27ae60;font-weight:500;">Mise &agrave; jour termin&eacute;e, l\'appareil red&eacute;marre.</div>';
      } else {
        h = '<div class="text-center" style="color:#c0392b;font-weight:500;">Mise &agrave; jour en &eacute;chec : ' + esc(d.msg) + '</div>' +
          '<div class="text-center"><a href="#loraOta?slot=' + d.slot + '">Relancer</a></div>';
      }
      el.html(h).show();
    });
  }

  // ==========================================================================
  // PAGE : parametres reseau + lecture d'attribut a la demande
  // ==========================================================================
  function pageConfigLoraSettings() {
    A.setContent(layout('settings', 'Config LoRa', '<div id="lora-set">' + SPINNER + '</div>'));
    $.getJSON('/api/lora/status', function(st) {
      if (!st.detected) { $('#lora-set').html(NO_MODULE); return; }
      var sfOpts = '', chOpts = '', emOpts = '';
      for (var sf = 7; sf <= 12; sf++) sfOpts += '<option value="' + sf + '"' + (st.sf === sf ? ' selected' : '') + '>SF' + sf + '</option>';
      var chans = st.channels || [];
      for (var c = 0; c < chans.length; c++) {
        chOpts += '<option value="' + c + '"' + (st.channel === c ? ' selected' : '') + '>Canal ' + c + ' (' + esc(chans[c]) + ' MHz)</option>';
      }
      var em = st.emitters || [];
      for (var i = 0; i < em.length; i++) {
        emOpts += '<option value="' + em[i].slot + '">' + esc(em[i].alias || em[i].model || 'LoRa') + ' (' + esc(String(em[i].mac).substring(8)) + ')</option>';
      }

      var html =
        '<p class="text-muted">Param&egrave;tres <b>r&eacute;seau</b>, communs &agrave; tous les &eacute;metteurs LoRa. ' +
        'Le SF et le canal sont assign&eacute;s &agrave; chaque appairage : apr&egrave;s les avoir chang&eacute;s, ' +
        '<b>r&eacute;-appairez chaque ZLinky</b> (appui long) pour qu\'il bascule dessus.</p>' +
        '<div class="card p-3 mb-3" style="max-width:620px;">' +
          '<label class="form-label"><b>Param&egrave;tres radio r&eacute;seau</b></label>' +
          '<div class="text-muted" style="font-size:13px;margin-bottom:10px;">Le <b>Spreading Factor</b> (SF7&ndash;12) arbitre ' +
          'port&eacute;e et d&eacute;bit ; le <b>canal</b> (2410&ndash;2480 MHz) aide en cas d\'interf&eacute;rences Wi-Fi / BLE. ' +
          'Ces valeurs sont transmises &agrave; l\'&eacute;metteur lors de l\'appairage.</div>' +
          '<div style="color:#b06000;font-size:13px;margin-bottom:10px;">&#9888; Le changement s\'applique au <b>prochain appairage</b>. ' +
          'Les &eacute;metteurs d&eacute;j&agrave; appair&eacute;s resteront muets tant qu\'ils n\'auront pas &eacute;t&eacute; r&eacute;-appair&eacute;s.</div>' +
          '<div class="d-flex flex-wrap gap-2 align-items-end">' +
            '<div><label class="form-label small text-muted mb-0">Spreading Factor</label><select id="lora-sf" class="form-select" style="width:120px;">' + sfOpts + '</select></div>' +
            '<div><label class="form-label small text-muted mb-0">Canal</label><select id="lora-ch" class="form-select" style="width:210px;">' + chOpts + '</select></div>' +
            '<button class="btn btn-success" id="lora-apply">Appliquer</button>' +
          '</div><div id="lora-net-banner" class="mt-2" style="display:none;"></div>' +
        '</div>';

      if (em.length) {
        html += '<div class="card p-3 mb-3" style="max-width:620px;">' +
          '<label class="form-label"><b>Lecture d\'attribut &agrave; la demande</b></label>' +
          '<div class="text-muted" style="font-size:13px;margin-bottom:10px;">Interroge un attribut pr&eacute;cis sans attendre ' +
          'le cycle p&eacute;riodique (~3 min 40). La requ&ecirc;te part &agrave; la prochaine trame de l\'&eacute;metteur (~7 s) ' +
          'et la valeur re&ccedil;ue alimente sa fiche comme une donn&eacute;e normale.</div>' +
          '<div class="d-flex flex-wrap gap-2 align-items-end">' +
            '<div><label class="form-label small text-muted mb-0">&Eacute;metteur</label><select id="lora-em" class="form-select" style="width:220px;">' + emOpts + '</select></div>' +
            '<div><label class="form-label small text-muted mb-0">Cluster (hex)</label><input id="lora-cl" class="form-control" style="width:110px;" placeholder="FF66"></div>' +
            '<div><label class="form-label small text-muted mb-0">Attribut (hex)</label><input id="lora-at" class="form-control" style="width:110px;" placeholder="0300"></div>' +
            '<button class="btn btn-primary" id="lora-poll">Lire</button>' +
          '</div><div id="lora-poll-banner" class="mt-2" style="display:none;"></div>' +
        '</div>';
      }
      $('#lora-set').html(html);

      $('#lora-apply').on('click', function() {
        var payload = { sf: parseInt($('#lora-sf').val(), 10), channel: parseInt($('#lora-ch').val(), 10) };
        if (!window.confirm('Changer les param\u00e8tres r\u00e9seau ? Chaque ZLinky d\u00e9j\u00e0 appair\u00e9 devra \u00eatre r\u00e9-appair\u00e9 (appui long) pour rester joignable.')) return;
        var b = $('#lora-net-banner');
        A.postJSON('/api/lora/settings', payload).then(function() {
          b.attr('class', 'mt-2 alert alert-success').html('&#10003; Param&egrave;tres enregistr&eacute;s. R&eacute;-appairez les &eacute;metteurs pour qu\'ils basculent.').show();
        }, function(xhr) {
          b.attr('class', 'mt-2 alert alert-danger').html('&#10007; ' + errMsg(xhr, 'Erreur')).show();
        });
      });
      $('#lora-poll').on('click', loraDoPoll);
    }).fail(function(xhr) {
      $('#lora-set').html('<div class="alert alert-danger">' + errMsg(xhr, 'Erreur de chargement') + '</div>');
    });
  }

  var pollTimer = null;
  function loraDoPoll() {
    var slot = parseInt($('#lora-em').val(), 10);
    var cl = $.trim($('#lora-cl').val()).replace(/^0x/i, ''), at = $.trim($('#lora-at').val()).replace(/^0x/i, '');
    var b = $('#lora-poll-banner');
    if (!/^[0-9a-fA-F]{1,4}$/.test(cl) || !/^[0-9a-fA-F]{1,4}$/.test(at)) {
      b.attr('class', 'mt-2 alert alert-danger').text('Cluster / attribut hexad\u00e9cimaux invalides').show();
      return;
    }
    b.attr('class', 'mt-2 alert alert-info').html('Lecture en attente d\'une trame de l\'&eacute;metteur (~7 s)&hellip;').show();
    A.postJSON('/api/lora/poll', { slot: slot, cluster: cl, attr: at }).then(function() {
      if (pollTimer) clearInterval(pollTimer);
      var n = 0;
      pollTimer = A.pageInterval(function() {
        if (++n > 60 || !$('#lora-poll-banner').length) { clearInterval(pollTimer); pollTimer = null; return; }
        $.getJSON('/api/lora/poll/status', { slot: slot }, function(d) {
          var bb = $('#lora-poll-banner');
          if (d.state === 'pending') return;
          clearInterval(pollTimer); pollTimer = null;
          if (d.state === 'ok') {
            bb.attr('class', 'mt-2 alert alert-success').html('&#10003; Valeur : <b>' + esc(d.value) + '</b> (type 0x' + Number(d.type).toString(16) + ')');
          } else if (d.state === 'unknown') {
            bb.attr('class', 'mt-2 alert alert-danger').html('&#10007; Attribut inconnu du firmware (UNKNOWN_ATTR)');
          } else if (d.state === 'bad') {
            bb.attr('class', 'mt-2 alert alert-danger').html('&#10007; Requ&ecirc;te rejet&eacute;e (BAD_REQUEST)');
          } else {
            bb.attr('class', 'mt-2 alert alert-warning').html('Pas de r&eacute;ponse de l\'&eacute;metteur (abandon apr&egrave;s 8 trames).');
          }
        });
      }, 2000);
    }, function(xhr) {
      b.attr('class', 'mt-2 alert alert-danger').html('&#10007; ' + errMsg(xhr, 'Erreur'));
    });
  }

  // ==========================================================================
  // PAGE : mise a jour du firmware d'un emetteur
  // ==========================================================================
  var OTA_CHUNK = 16384;

  function pageLoraOta() {
    var slot = parseInt(A.params().slot, 10);
    A.setContent('<div class="container" style="max-width:720px;margin-top:20px;" id="lora-ota-page">' + SPINNER + '</div>');
    $.getJSON('/api/lora/status', function(st) {
      var e = null, em = st.emitters || [];
      for (var i = 0; i < em.length; i++) if (em[i].slot === slot) e = em[i];
      if (!st.detected || !e) { window.location.hash = '#configLora'; return; }
      loraOtaRender(e);
    }).fail(function(xhr) {
      $('#lora-ota-page').html('<div class="alert alert-danger">' + errMsg(xhr, 'Erreur de chargement') + '</div>');
    });
  }

  function loraOtaRender(e) {
    var slot = e.slot, otaId = 'lora_' + e.mac, launchedAt = 0, uploading = false;
    $('#lora-ota-page').html(
      '<h4>Mise &agrave; jour du firmware &mdash; ' + esc(e.alias || e.model || e.mac) + '</h4>' +
      '<p style="color:#6c757d;font-family:Courier New,monospace;">' + esc(e.mac) + '</p>' +
      '<div class="alert alert-warning"><b>Avant de lancer</b>, mettez l\'appareil en mode mise &agrave; jour :' +
        '<ol style="margin-bottom:0;"><li>d&eacute;branchez-le du compteur ;</li><li>maintenez son bouton appuy&eacute; ;</li>' +
        '<li>rebranchez-le en gardant le bouton appuy&eacute; environ 3 secondes.</li></ol>' +
        'Vous pouvez aussi lancer d\'abord : le pont attend l\'appareil pendant 3 minutes.</div>' +
      '<div class="mb-3"><label class="form-label">Fichier firmware (.bin)</label>' +
        '<input class="form-control" type="file" id="lora-fw" accept=".bin,.ota"><div class="form-text" id="lora-fw-info"></div></div>' +
      '<div class="mb-3"><label class="form-label">Version du nouveau firmware</label>' +
        '<input class="form-control" type="text" id="lora-ver" placeholder="1.0.0" style="max-width:160px;">' +
        '<div class="form-text">Au format 1.2.3 (ou en hexad&eacute;cimal : 1.2.3 s\'&eacute;crit 0x0123). Version actuelle de l\'appareil : <b>' +
        (e.version ? esc(e.version) : 'inconnue') + '</b>.</div></div>' +
      '<button class="btn btn-warning" id="lora-ota-go">Lancer la mise &agrave; jour</button> ' +
      '<button class="btn btn-secondary" id="lora-ota-cancel" style="display:none;">Annuler</button> ' +
      '<a class="btn btn-link" href="#configLora">Retour</a>' +
      '<div style="margin-top:20px;"><div id="lora-ota-msg" style="font-weight:500;"></div>' +
        '<div class="progress mt-2" style="height:22px;"><div id="lora-ota-bar" class="progress-bar" style="width:0%;transition:width .4s;"></div></div>' +
        '<div id="lora-ota-detail" style="color:#6c757d;font-size:13px;margin-top:6px;"></div></div>');

    function setMsg(t, color) { $('#lora-ota-msg').html(t).css('color', color || ''); }
    function setBar(p, color) { $('#lora-ota-bar').css('width', p + '%').css('background-color', color || ''); }

    function post(url, data, raw) {
      var opt = { url: url, method: 'POST' };
      if (raw) { opt.data = data; opt.processData = false; opt.contentType = 'application/octet-stream'; }
      else { opt.data = JSON.stringify(data); opt.contentType = 'application/json'; }
      return $.ajax(opt);
    }

    // Depot par morceaux : init (taille), morceaux bruts, fin (renommage)
    function upload(file) {
      var n = Math.ceil(file.size / OTA_CHUNK), i = 0;
      setMsg('Envoi du fichier au pont...');
      return post('/api/lora/ota/init?id=' + encodeURIComponent(otaId), { totalSize: file.size }).then(function next() {
        if (i >= n) return post('/api/lora/ota/finish', {});
        var part = file.slice(i * OTA_CHUNK, Math.min((i + 1) * OTA_CHUNK, file.size));
        return post('/api/lora/ota/chunk?n=' + i, part, true).then(function() {
          i++;
          setBar(Math.round(100 * i / n));
          return next();
        });
      });
    }

    function parseVersion(vt) {
      var vm = /^(\d+)\.(\d+)\.(\d+)$/.exec(vt);
      if (vm) {
        if (+vm[1] <= 255 && +vm[2] <= 15 && +vm[3] <= 15) return (+vm[1] << 8) | (+vm[2] << 4) | (+vm[3]);
        return NaN;
      }
      if (/^(0x[0-9a-fA-F]+|\d+)$/.test(vt)) return parseInt(vt, vt.indexOf('0x') === 0 || vt.indexOf('0X') === 0 ? 16 : 10);
      return NaN;
    }

    $('#lora-ota-go').on('click', function() {
      var file = $('#lora-fw')[0].files[0];
      var ver = parseVersion($.trim($('#lora-ver').val()));
      if (isNaN(ver) || ver < 0 || ver > 65535) { setMsg('Version invalide : attendu 1.2.3 ou 0x0123.', '#c0392b'); return; }
      if (file && file.size > 1048576) { setMsg('Fichier trop volumineux (1 Mo maximum).', '#c0392b'); return; }
      $('#lora-ota-go').prop('disabled', true);
      setBar(0);
      $('#lora-ota-detail').html('');
      uploading = true;
      var chain = file ? upload(file) : $.Deferred().resolve().promise();
      chain.then(function() {
        uploading = false;
        return A.postJSON('/api/lora/ota/start', { slot: slot, version: ver });
      }).then(function() {
        setBar(0);
        setMsg('Mise &agrave; jour lanc&eacute;e...');
        launchedAt = Date.now();
      }, function(xhr) {
        uploading = false;
        setMsg('Erreur : ' + errMsg(xhr, 'd\u00e9marrage refus\u00e9'), '#c0392b');
        $('#lora-ota-go').prop('disabled', false);
      });
    });

    $('#lora-ota-cancel').on('click', function() {
      if (!window.confirm('Annuler la mise \u00e0 jour en cours ?')) return;
      A.postJSON('/api/lora/ota/cancel', {});
    });

    function show(d) {
      var mine = (d.slot === slot);
      var running = (d.state === 'waiting' || d.state === 'sending' || d.state === 'ending');
      $('#lora-ota-cancel').toggle(running && mine);
      if (!uploading) $('#lora-ota-go').prop('disabled', running);
      if (d.file > 0 && !$('#lora-fw')[0].files.length) {
        $('#lora-fw-info').html('Un fichier de ' + esc(d.file) + ' octets est d&eacute;j&agrave; sur le pont : vous pouvez lancer sans le renvoyer.');
      }
      if (uploading) return;
      if (running && !mine) { setMsg('Une mise &agrave; jour est en cours sur un autre appareil.', '#c0392b'); return; }
      if (!mine) return;
      if (d.state === 'waiting') {
        setMsg('En attente de l\'appareil en mode mise &agrave; jour... (essai ' + d.tries + ')');
      } else if (d.state === 'sending') {
        setBar(d.pct);
        setMsg('Envoi du firmware : ' + d.pct + ' %');
        $('#lora-ota-detail').html('Bloc ' + d.block + ' / ' + d.total + ' &middot; ' + d.retries + ' r&eacute;&eacute;mission(s) &middot; &eacute;coul&eacute; ' +
          fmtTime(d.elapsed) + ' &middot; reste environ ' + fmtTime(d.eta));
      } else if (d.state === 'ending') {
        setBar(100); setMsg('V&eacute;rification du firmware par l\'appareil...');
      } else if (d.state === 'done') {
        setBar(100, '#27ae60');
        setMsg('Mise &agrave; jour termin&eacute;e : l\'appareil red&eacute;marre sur le nouveau firmware.', '#27ae60');
        $('#lora-ota-detail').html(d.total + ' blocs en ' + fmtTime(d.elapsed) + ' &middot; ' + d.retries + ' r&eacute;&eacute;mission(s)');
      } else if (d.state === 'error') {
        setBar(d.pct, '#c0392b');
        setMsg('&Eacute;chec : ' + esc(d.msg), '#c0392b');
        $('#lora-ota-detail').html('L\'ancien firmware reste actif. Remettez l\'appareil en mode mise &agrave; jour puis relancez.');
      }
    }

    function poll() {
      if (!$('#lora-ota-page').length) return;
      $.getJSON('/api/lora/ota/status', { slot: slot }, function(d) {
        // Juste apres un lancement, un resultat final est celui d'une tentative precedente
        var fresh = launchedAt && (Date.now() - launchedAt < 8000);
        if (fresh && (d.state === 'error' || d.state === 'done' || d.state === 'idle')) return;
        show(d);
      });
    }
    poll();
    A.pageInterval(poll, 1500);
  }

  // ==========================================================================
  // PAGE : assistant d'appairage LoRa
  // ==========================================================================
  function pageAssistLora() {
    A.setContent(
      '<div class="container py-4" style="max-width:720px;">' +
        '<h4><span style="color:#0f70b7;">' + loraIcon(16, 28) + '</span> Assistant d\'appairage LoRa</h4>' +
        '<div class="card p-4" id="lora-assist">' + SPINNER + '</div>' +
      '</div>');

    $.getJSON('/api/lora/status', function(st) {
      if (!st.detected) { $('#lora-assist').html(NO_MODULE); return; }
      if (st.count >= st.maxEmitters) {
        $('#lora-assist').html('<div class="alert alert-warning">Nombre maximum d\'appareils LoRa atteint (' + esc(st.maxEmitters) +
          '). Supprimez-en un pour en appairer un nouveau, ou r&eacute;-appairez un appareil d&eacute;j&agrave; connu.</div>' +
          '<a class="btn btn-primary" href="#configLora">Retour</a> <button class="btn btn-outline-primary" id="lora-step-go">Continuer</button>');
        $('#lora-step-go').on('click', step1);
        return;
      }
      step1();
    }).fail(function(xhr) {
      $('#lora-assist').html('<div class="alert alert-danger">' + errMsg(xhr, 'Erreur de chargement') + '</div>');
    });

    function step1() {
      $('#lora-assist').html(
        '<h5>&Eacute;tape 1 : Pr&eacute;parez l\'appareil</h5>' +
        '<p>Branchez l\'appareil LoRa (ZLinky LoRa sur le compteur). Le pont va ouvrir une fen&ecirc;tre d\'appairage de 30 s ' +
        'sur le canal de rendez-vous : mettez alors l\'appareil en mode appairage (appui long sur son bouton).</p>' +
        '<button class="btn btn-primary" id="lora-pair-go">Ouvrir la fen&ecirc;tre d\'appairage</button> ' +
        '<a class="btn btn-link" href="#configLora">Annuler</a>');
      $('#lora-pair-go').on('click', step2);
    }

    function step2() {
      $('#lora-assist').html(
        '<h5>&Eacute;tape 2 : Fen&ecirc;tre d\'appairage LoRa ouverte</h5>' +
        '<p>Le pont &eacute;coute sur le canal d\'appairage. Mettez maintenant l\'appareil LoRa en mode appairage.</p>' +
        '<div class="text-center p-3"><div class="spinner-border text-primary" style="width:3rem;height:3rem;"></div>' +
        '<p class="mt-3" id="lora-pair-left">Recherche...</p></div><div id="lora-pair-found"></div>');
      // Un resultat reste d'un appairage precedent ne doit pas etre pris pour celui-ci
      $.getJSON('/api/lora/pair/status').always(function() {
        A.postJSON('/api/lora/pair', {}).then(function() {
          var started = Date.now(), timer = null;
          timer = A.pageInterval(function() {
            $.getJSON('/api/lora/pair/status', function(d) {
              if (d.found) {
                clearInterval(timer);
                step3(d);
                return;
              }
              var elapsed = (Date.now() - started) / 1000;
              if (d.pairing) $('#lora-pair-left').text('Fen\u00eatre ouverte encore ' + d.left + ' s...');
              // Marge apres la fermeture : l'appairage (bascule canal/SF) peut aboutir tard
              if ((!d.pairing && elapsed > 8) || elapsed > 45) {
                clearInterval(timer);
                $('#lora-assist').html('<div class="alert alert-warning">Aucun appareil trouv&eacute;.</div>' +
                  '<button class="btn btn-primary" id="lora-pair-retry">R&eacute;essayer</button> <a class="btn btn-link" href="#configLora">Retour</a>');
                $('#lora-pair-retry').on('click', step2);
              }
            });
          }, 1500);
        }, function(xhr) {
          $('#lora-assist').html('<div class="alert alert-danger">' + errMsg(xhr, 'Appairage impossible') + '</div>' +
            '<a class="btn btn-link" href="#configLora">Retour</a>');
        });
      });
    }

    function step3(d) {
      $('#lora-assist').html(
        '<div class="alert alert-success text-center"><span style="color:#0f70b7;">' + loraIcon(16, 28) + '</span><br>' +
          'Appareil trouv&eacute; !<br><strong>' + esc(d.model) + '</strong><br>(<span>' + esc(d.ieee) + '</span>)</div>' +
        '<h5>&Eacute;tape 3 : Nommer l\'appareil</h5>' +
        '<div class="mb-3"><label class="form-label">Nom / Alias</label>' +
          '<input type="text" class="form-control" id="lora-alias" maxlength="32" placeholder="Ex : Linky maison"></div>' +
        '<button class="btn btn-success" id="lora-alias-save">Enregistrer</button> ' +
        '<a class="btn btn-link" href="#configLora">Plus tard</a>');
      $('#lora-alias-save').on('click', function() {
        var alias = $.trim($('#lora-alias').val());
        if (!alias) { window.location.hash = '#configLora'; return; }
        A.postJSON('/api/zigbee/alias', { ieee: d.ieee, alias: alias }).then(function() {
          A.showAlert('Appareil enregistr&eacute; !');
          window.location.hash = '#configLora';
        }, function(xhr) { A.showAlert(errMsg(xhr, 'Erreur'), 'danger'); });
      });
    }
  }

  A.registerPage('configLora', pageConfigLora);
  A.registerPage('configLoraSettings', pageConfigLoraSettings);
  A.registerPage('loraOta', pageLoraOta);
  A.registerPage('assistLora', pageAssistLora);
})();
