// ============================================================================
// Pages Zigbee - lot 3 du portage LiXee-Box
//
//   #devices       mesures des appareils (ex /statusDevices)
//   #device?ieee=  fiche d'un appareil (ex /configDevice?id=)
//   #dashboard     tableau de bord : jauges, batteries, valeurs (ex /dashboard)
//   #zigbee        config Zigbee : appareils (ex /configDevices) et
//                  coordinateur (ex /configZigbee) ; alias #configDevices,
//                  #configZigbee
//   #assistDevice  assistant d'appairage (ex /assistDevice)
//
// Donnees : /api/zigbee/* (contrat en tete de components/webserver/web_zigbee.cpp).
// Les valeurs arrivent deja formatees par le serveur selon le template
// (coefficient, type, mode TIC du ZLinky) : les pages ne font que l'affichage.
// ============================================================================

(function() {
  'use strict';

  var A = window.LixeeApp;
  if (!A) return;
  var esc = A.escHtml;

  // --------------------------------------------------------------------------
  // Outils communs
  // --------------------------------------------------------------------------

  var CSS =
    '<style>' +
    '.zb-card{background:#fff;border:none;border-radius:12px;box-shadow:0 2px 8px rgba(0,0,0,.08);overflow:hidden}' +
    '.zb-head{padding:12px 16px;display:flex;align-items:center;gap:10px}' +
    '.zb-head a{color:#222;text-decoration:none;font-weight:600;font-size:15px;display:flex;align-items:center;gap:10px;min-width:0;flex:1}' +
    '.zb-head a:hover{color:#6c757d}' +
    '.zb-head img{height:48px;width:48px;object-fit:contain;flex-shrink:0}' +
    '.zb-name{overflow:hidden;text-overflow:ellipsis}' +
    '.zb-tag{margin-left:auto;display:inline-flex;align-items:center;gap:5px;flex-shrink:0;font-size:11px;font-weight:600;color:#6c757d;background:#f1f3f5;border-radius:10px;padding:3px 8px}' +
    '.zb-radio{display:inline-flex;cursor:help}.zb-radio svg{width:18px;height:18px;fill:#0F70B7}#zb-radio svg{width:20px;height:20px}.zb-radio:empty{display:none}' +
    '.zb-body{padding:8px 16px}' +
    '.zb-row{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid #e9ecef;gap:8px}' +
    '.zb-row:last-child{border-bottom:none}' +
    '.zb-attr{color:#6c757d;font-size:13px;font-weight:500}' +
    '.zb-val{font-family:"Courier New",monospace;font-size:14px;font-weight:600;color:#212529;text-align:right;word-break:break-all}' +
    '.zb-unit{color:#adb5bd;font-size:12px;margin-left:4px}' +
    '.zb-actions{display:flex;flex-wrap:wrap;gap:8px;padding:12px 16px;background:#f8f9fa;border-top:1px solid #e9ecef}' +
    '.zb-actions:empty{display:none}' +
    '.zb-temps{display:flex;flex-wrap:wrap;gap:8px;width:100%}' +
    '.zb-temps .input-group{width:auto;flex-wrap:nowrap}' +
    '.zb-temps .zb-temp-val{max-width:80px}' +
    '.zb-cover{display:flex;align-items:center;gap:10px;width:100%}' +
    // Curseur natif en bleu LiXee, comme la Box : la barre de .form-range (gris
    // tres clair) ne se voyait pas sur le fond de la barre d'actions. Fond,
    // bordure et marge de style.css (tout <input>) neutralises.
    '.zb-cover input{flex:1;min-width:0;margin:0;padding:0;border:0;background:transparent;accent-color:#0F70B7;height:24px;cursor:pointer}' +
    '.zb-cover-val{font-family:"Courier New",monospace;font-size:13px;font-weight:600;min-width:44px;text-align:right}' +
    '.zb-changed{animation:zbFlash 2s ease-out}' +
    '@keyframes zbFlash{0%{background-color:#fff3cd}100%{background-color:transparent}}' +
    '.zb-badge{color:#fff;padding:2px 8px;border-radius:4px;font-size:11px;font-family:monospace}' +
    '.zb-info td:first-child{font-weight:600;color:#555;width:40%}' +
    '.zb-info td:last-child{font-family:"Courier New",monospace;text-align:right;word-break:break-all}' +
    '.zb-gauge{height:150px}' +
    // Page Zigbee (configDevices / configZigbee de la LiXee-Box)
    '.zbc-cont{padding:8px}' +
    '.zbc-card{background:#fff;border:none;border-radius:12px;box-shadow:0 2px 8px rgba(0,0,0,.08);transition:transform .2s,box-shadow .2s;overflow:hidden}' +
    '.zbc-card:hover{transform:translateY(-2px);box-shadow:0 4px 16px rgba(0,0,0,.12)}' +
    '.zbc-head{background:#fff;padding:12px 16px;display:flex;align-items:center;gap:8px}' +
    '.zbc-head a{color:#222;text-decoration:none;font-weight:600;font-size:14px;display:flex;align-items:center;flex:1;min-width:0}' +
    '.zbc-head a:hover{color:#6c757d;opacity:.95}' +
    '.zbc-head img{height:64px;margin-right:8px;flex-shrink:0}' +
    '.zbc-body{padding:12px 16px}' +
    '.zbc-body table td{padding:6px 4px;border:none}' +
    '.zbc-k{color:#6c757d;font-weight:500}' +
    '.zbc-v{font-family:"Courier New",monospace;text-align:right}' +
    '.zbc-v a{cursor:pointer;color:inherit}' +
    '.zbc-btns{display:flex;flex-wrap:wrap;gap:6px;margin-top:12px;padding-top:12px;border-top:1px solid #e9ecef}' +
    '.zbc-btns .btn{padding:6px 10px;display:inline-flex;align-items:center;justify-content:center}' +
    '.zbc-btns .btn svg{width:16px;height:16px;flex-shrink:0}' +
    '@media(max-width:576px){.zbc-card{border-radius:10px}.zbc-head{padding:10px 12px}.zbc-head a{font-size:13px}.zbc-btns{justify-content:center}.zbc-btns .btn{padding:5px 8px}.zbc-btns .btn svg{width:14px;height:14px}}' +
    '</style>';

  var SVG_ZB = '<svg width="13" height="13" viewBox="0 0 24 24" fill="currentColor"><path d="M11.988 0a11.85 11.85 0 00-8.617 3.696c7.02-.875 11.401-.583 13.289-.34 3.752.583 3.558 3.404 3.558 3.404L8.237 19.112c2.299.22 6.897.366 13.796-.631a11.86 11.86 0 001.912-6.469C23.945 5.374 18.595 0 11.988 0zm.232 4.31c-2.451-.014-5.772.146-9.963.723C.854 7.003.055 9.41.055 12.012.055 18.626 5.38 24 11.988 24c3.63 0 6.85-1.63 9.053-4.182-7.286.948-11.813.631-13.75.388-3.775-.56-3.557-3.404-3.557-3.404L15.691 4.474a38.635 38.635 0 00-3.471-.163Z"/></svg>';

  // Icones d'etat radio (Material Design, Apache 2.0) : signal-off, lan-disconnect, alert-circle
  var RADIO_PATHS = {
    0xE9: 'M18,3V16.18L21,19.18V3H18M4.28,5L3,6.27L10.73,14H8V21H11V14.27L13,16.27V21H16V19.27L19.73,23L21,21.72L4.28,5M13,9V11.18L16,14.18V9H13M3,18V21H6V18H3Z',
    0xD4: 'M4,1C2.89,1 2,1.89 2,3V7C2,8.11 2.89,9 4,9H1V11H13V9H10C11.11,9 12,8.11 12,7V3C12,1.89 11.11,1 10,1H4M4,3H10V7H4V3M14,13C12.89,13 12,13.89 12,15V19C12,20.11 12.89,21 14,21H11V23H23V21H20C21.11,21 22,20.11 22,19V15C22,13.89 21.11,13 20,13H14M3.88,13.46L2.46,14.88L4.59,17L2.46,19.12L3.88,20.54L6,18.41L8.12,20.54L9.54,19.12L7.41,17L9.54,14.88L8.12,13.46L6,15.59L3.88,13.46M14,15H20V19H14V15Z',
    0: 'M11,15H13V17H11V15M11,7H13V13H11V7M12,2C6.47,2 2,6.5 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M12,20A8,8 0 0,1 4,12A8,8 0 0,1 12,4A8,8 0 0,1 20,12A8,8 0 0,1 12,20Z'
  };

  function hex4(n) { return ('0000' + (Number(n) || 0).toString(16).toUpperCase()).slice(-4); }
  // LQI affiche comme la LiXee-Box : 2 chiffres hexadecimaux
  function hex2(n) { return ('00' + (Number(n) || 0).toString(16).toUpperCase()).slice(-2); }

  function radioLabel(code) {
    if (!code) return '';
    if (code === 0xE9) return 'Pas d\u2019accus\u00e9 de r\u00e9ception : appareil \u00e9teint, hors de port\u00e9e ou en veille';
    if (code === 0xD4) return 'Appareil injoignable, souvent d\u00fb \u00e0 une route cass\u00e9e';
    return '\u00c9chec de transmission radio';
  }

  // Pastille d'etat radio : vide si l'appareil est joignable. Infobulle de la
  // LiXee-Box (printRadioStatus) : cause, code, puis dernier contact
  function radioIcon(code, d) {
    if (!code) return '';
    var path = RADIO_PATHS[code] || RADIO_PATHS[0];
    var seen = d && (d.lastSeen > 1600000000 || d.ago >= 0) ? fmtSeen(d) : '';
    var tip = esc(radioLabel(code) + ' (code ' + hex2(code) + ')' + (seen && seen !== '-' ? ' — dernier contact : ' + seen : ''));
    return '<span title="' + tip + '" aria-label="' + tip + '"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="' + path + '"/></svg></span>';
  }

  function fmtAgo(s) {
    if (s == null || s < 0) return '-';
    if (s < 60) return 'il y a ' + s + ' s';
    if (s < 3600) return 'il y a ' + Math.floor(s / 60) + ' min';
    if (s < 86400) return 'il y a ' + Math.floor(s / 3600) + ' h';
    return 'il y a ' + Math.floor(s / 86400) + ' j';
  }

  function fmtSeen(d) {
    if (d.lastSeen > 1600000000) {
      var t = new Date(d.lastSeen * 1000);
      var p = function(n) { return ('0' + n).slice(-2); };
      return p(t.getDate()) + '/' + p(t.getMonth() + 1) + '/' + t.getFullYear() + ' ' + p(t.getHours()) + ':' + p(t.getMinutes());
    }
    return fmtAgo(d.ago);
  }

  function devName(d) { return d.alias || d.model || d.ieee; }

  // Fiche de l'appareil, Zigbee ou LoRa (configDevice?id= de la LiXee-Box)
  function devLink(d) { return '#device?ieee=' + esc(d.ieee); }

  // Icone : modele, puis DeviceId, puis rien
  function devIcon(d) {
    var a = 'img/icon_' + encodeURIComponent(d.model || '-') + '.png';
    var b = 'img/icon_' + (Number(d.deviceId) || 0) + '.png';
    return '<img src="' + esc(a) + '" data-alt="' + esc(b) + '" onerror="window._zbImgErr(this)" alt="">';
  }
  window._zbImgErr = function(img) {
    var alt = img.getAttribute('data-alt');
    if (alt) { img.removeAttribute('data-alt'); img.src = alt; }
    else { img.onerror = null; img.style.visibility = 'hidden'; }
  };

  function errMsg(xhr, fallback) { return A.apiErrorText(xhr) || fallback || 'Erreur'; }
  function okMsg(r, fallback) { return (r && r.message) || fallback; }

  function post(url, obj) { return A.postJSON(url, obj); }

  function confirmPost(question, url, obj, done) {
    if (question && !confirm(question)) return;
    post(url, obj).done(function(r) {
      A.showAlert(esc(okMsg(r, 'Commande envoy\u00e9e')));
      if (done) done(r);
    }).fail(function(xhr) { A.showAlert(esc(errMsg(xhr)), 'danger'); });
  }

  // Ouverture (%) d'un volet a partir de la position de levage ZCL (fermeture)
  function coverOpen(lift, cmd) {
    if (lift == null || lift < 0) return null;
    return (Number(cmd) === 252) ? lift : 100 - lift;
  }

  // Barre d'actions d'un appareil : boutons, curseurs pour les volets, puis
  // consignes (temperature + envoi) regroupees sur leur propre ligne
  function actionsHtml(d) {
    var h = '', temps = '';
    (d.actions || []).forEach(function(a) {
      if (a.cover) {
        var o = coverOpen(d.cover, a.c);
        h += '<div class="zb-cover"><span class="small text-muted fw-semibold">' + esc(a.n) + '</span>' +
          '<input type="range" class="zb-cover-range" min="0" max="100" step="1" value="' + (o == null ? 0 : o) + '"' +
          ' data-ieee="' + esc(d.ieee) + '" data-i="' + a.i + '" data-c="' + a.c + '" aria-label="' + esc(a.n) + '">' +
          '<span class="zb-cover-val">' + (o == null ? '–' : o + ' %') + '</span></div>';
      } else if (a.temp) {
        // Consigne : temperature choisie puis envoyee (valeur en 0,01 degC)
        var tv = (a.tv != null) ? a.tv / 100 : (a.v ? a.v / 100 : 20);
        temps += '<div class="input-group input-group-sm">' +
          '<span class="input-group-text">' + esc(a.n) + '</span>' +
          '<input type="number" class="form-control zb-temp-val" min="5" max="35" step="0.5" value="' + tv + '" aria-label="' + esc(a.n) + '">' +
          '<span class="input-group-text">&deg;C</span>' +
          '<button type="button" class="btn btn-primary zb-temp-send" data-ieee="' + esc(d.ieee) + '" data-i="' + a.i + '">Envoyer</button></div>';
      } else {
        h += '<button type="button" class="btn btn-sm btn-primary zb-act" data-ieee="' + esc(d.ieee) + '" data-i="' + a.i + '">' + esc(a.n) + '</button>';
      }
    });
    return h + (temps ? '<div class="zb-temps">' + temps + '</div>' : '');
  }

  // Gestionnaires delegues des actions, sur le conteneur de page (remplace a
  // chaque navigation : pas d'accumulation)
  function bindActions($root) {
    $root.off('.zbact');
    $root.on('click.zbact', '.zb-act', function() {
      var $b = $(this);
      $b.prop('disabled', true);
      post('/api/zigbee/action', { ieee: $b.attr('data-ieee'), index: Number($b.data('i')) })
        .done(function() { $b.removeClass('btn-primary').addClass('btn-success'); })
        .fail(function(xhr) { $b.removeClass('btn-primary').addClass('btn-danger'); A.showAlert(esc(errMsg(xhr)), 'danger'); })
        .always(function() {
          setTimeout(function() { $b.prop('disabled', false).removeClass('btn-success btn-danger').addClass('btn-primary'); }, 1200);
        });
    });
    $root.on('click.zbact', '.zb-temp-send', function() {
      var $b = $(this);
      var t = parseFloat(String($b.siblings('.zb-temp-val').val()).replace(',', '.'));
      if (!isFinite(t) || t < 5 || t > 35) { A.showAlert('Température invalide (5 à 35 °C)', 'danger'); return; }
      $b.prop('disabled', true);
      post('/api/zigbee/action', { ieee: $b.attr('data-ieee'), index: Number($b.data('i')), value: Math.round(t * 100) })
        .done(function() { $b.removeClass('btn-primary').addClass('btn-success'); })
        .fail(function(xhr) { $b.removeClass('btn-primary').addClass('btn-danger'); A.showAlert(esc(errMsg(xhr)), 'danger'); })
        .always(function() {
          setTimeout(function() { $b.prop('disabled', false).removeClass('btn-success btn-danger').addClass('btn-primary'); }, 1200);
        });
    });
    // Curseur de volet : commande envoyee au relachement seulement (pas a
    // chaque pas du glissement, qui inonderait la file Zigbee)
    $root.on('input.zbact', '.zb-cover-range', function() {
      $(this).next('.zb-cover-val').text(this.value + ' %');
    });
    $root.on('pointerdown.zbact', '.zb-cover-range', function() { $(this).data('drag', 1); });
    $root.on('pointerup.zbact', '.zb-cover-range', function() {
      var el = $(this);
      setTimeout(function() { el.removeData('drag'); }, 1500);
    });
    $root.on('change.zbact', '.zb-cover-range', function() {
      var $r = $(this);
      post('/api/zigbee/action', { ieee: $r.attr('data-ieee'), index: Number($r.data('i')), value: Number(this.value) })
        .fail(function(xhr) { A.showAlert(esc(errMsg(xhr)), 'danger'); });
    });
  }

  // Recale les curseurs de volet sur la position reelle (sauf en manipulation)
  function syncCovers(ieee, lift) {
    $('.zb-cover-range[data-ieee="' + ieee + '"]').each(function() {
      var $r = $(this);
      if ($r.data('drag')) return;
      var o = coverOpen(lift, $r.data('c'));
      if (o == null) return;
      $r.val(o);
      $r.next('.zb-cover-val').text(o + ' %');
    });
  }

  function setText(el, v) {
    if (!el) return;
    if (el.textContent !== v) {
      el.textContent = v;
      el.classList.remove('zb-changed'); void el.offsetWidth; el.classList.add('zb-changed');
    }
  }

  function masonry(sel) {
    if (typeof Masonry === 'undefined') return;
    var grid = document.querySelector(sel);
    if (!grid) return;
    try { new Masonry(grid, { itemSelector: '.zb-col', percentPosition: true }); } catch (e) {}
  }

  // --------------------------------------------------------------------------
  // #devices : mesures des appareils
  // --------------------------------------------------------------------------

  function pageDevices() {
    A.setContent(CSS +
      '<div class="container py-4">' +
        '<h4 style="font-weight:600;margin-bottom:20px;">Mesures des appareils</h4>' +
        // Groupes d'actions (lot regles) : boutons affiches s'il en existe
        '<div id="ag-bar" class="mb-3"></div>' +
        '<div class="row g-4" id="zb-grid"><div class="col-12 text-center p-3"><div class="spinner-border"></div></div></div>' +
      '</div>');
    if (window.LixeeActionGroups) window.LixeeActionGroups.renderBar('#ag-bar');
    var since = 0;
    $.getJSON('/api/zigbee/cards').done(function(r) {
      since = r.now || 0;
      var devs = r.devices || [];
      if (!devs.length) {
        $('#zb-grid').html('<div class="col-12 text-center" style="font-size:24px;font-weight:bold;">Aucun appareil pour l\'instant</div>' +
          '<div class="col-12 text-center">' + addDeviceButtons() + '</div>');
        return;
      }
      var h = '';
      devs.forEach(function(d) {
        h += '<div class="col-12 col-md-6 col-lg-4 zb-col"><div class="zb-card">' +
          '<div class="zb-head"><a href="' + devLink(d) + '">' + devIcon(d) + '<span class="zb-name">' + esc(devName(d)) + '</span></a>' +
            (d.lora ? '<span class="zb-tag" title="Re\u00e7u par radio LoRa 2.4 GHz">LoRa</span>'
                    : '<span class="zb-tag" title="Re\u00e7u par Zigbee">' + SVG_ZB + 'Zigbee</span>') +
            '<span class="zb-radio" id="zb-radio-' + esc(d.ieee) + '">' + radioIcon(d.radio, d) + '</span></div>' +
          '<div class="zb-body">';
        if (!d.tmpl) {
          h += '<div class="text-muted small py-2">Pas de template pour ce mod\u00e8le (' + esc(d.model || '?') + ')</div>';
        }
        (d.states || []).forEach(function(s) {
          h += '<div class="zb-row"><span class="zb-attr">' + esc(s.n) + '</span><span>' +
            '<span class="zb-val" id="zb-v-' + esc(d.ieee) + '-' + esc(s.k) + '">' + esc(s.v === '' ? '-' : s.v) + '</span>' +
            '<span class="zb-unit">' + esc(s.u) + '</span></span></div>';
        });
        h += '</div><div class="zb-actions">' + actionsHtml(d) + '</div></div></div>';
      });
      $('#zb-grid').html(h);
      bindActions($('#page-content'));
      masonry('#zb-grid');
    }).fail(function() {
      $('#zb-grid').html('<div class="col-12"><div class="alert alert-danger">Erreur de chargement des appareils.</div></div>');
    });

    A.pageInterval(function() {
      $.getJSON('/api/zigbee/values?since=' + since).done(function(r) {
        since = r.now || since;
        $.each(r.devices || {}, function(ieee, d) {
          $('#zb-radio-' + ieee).html(radioIcon(d.radio, d));
          $.each(d.v || {}, function(k, v) {
            setText(document.getElementById('zb-v-' + ieee + '-' + k), v === '' ? '-' : v);
          });
          syncCovers(ieee, d.cover);
        });
      });
    }, 5000);
  }

  // --------------------------------------------------------------------------
  // #device?ieee= : fiche d'un appareil
  // --------------------------------------------------------------------------

  function pageDevice() {
    var ieee = (A.params().ieee || A.params().id || '').toUpperCase();
    if (!/^[0-9A-F]{1,16}$/.test(ieee)) { window.location.hash = '#zigbee'; return; }

    A.setContent(CSS + '<div class="container py-4" id="zb-dev"><div class="text-center p-5"><div class="spinner-border"></div></div></div>');
    var cur = null;

    function info(d) {
      // Emetteur LoRa (comme la LiXee-Box) : ni adresse courte, ni endpoint,
      // ni LQI (metrique Zigbee) ; la qualite du lien se lit sur RSSI / SNR
      if (d.lora) {
        var lr = [
          ['Fabricant', d.manufacturer || '-'],
          ['Mod\u00e8le', d.model || (d.identified ? '-' : 'identification en cours\u2026')],
          ['Device ID', d.deviceId ? hex4(d.deviceId) : '-'],
          ['Version logicielle', d.swVersion || '-'],
          ['Derni\u00e8re activit\u00e9', fmtSeen(d)],
          ['RSSI', Math.round(d.rssi) + ' dBm'],
          ['SNR', Math.round(d.snr) + ' dB'],
          ['Trames', d.rx + ' re\u00e7ues / ' + d.missed + ' perdues'],
          ['Endpoint Matter', d.matterEndpoint >= 0 ? d.matterEndpoint : '-'],
          ['Statut', d.status || '-']
        ];
        if (d.zlinky) lr.splice(5, 0, ['Mode TIC', d.linkyMode >= 0 ? d.linkyMode : 'inconnu']);
        var lh = '<table class="table table-sm zb-info">';
        lr.forEach(function(r) { lh += '<tr><td>' + esc(r[0]) + '</td><td>' + esc(r[1]) + '</td></tr>'; });
        return lh + '</table>';
      }
      var rows = [
        ['Adresse courte', hex4(d.short)],
        ['Fabricant', d.manufacturer || '-'],
        ['Mod\u00e8le', d.model || '-'],
        ['Device ID', hex4(d.deviceId)],
        ['Endpoint', d.endpoint],
        ['Profil', hex4(d.profile)],
        ['Version logicielle', d.swVersion || '-'],
        ['Derni\u00e8re activit\u00e9', fmtSeen(d)],
        ['LQI', hex2(d.lqi)],
        ['\u00c9tat radio', d.radio ? radioLabel(d.radio) + ' (' + d.radio.toString(16).toUpperCase() + ')' : 'OK'],
        ['Bind / reporting envoy\u00e9s', d.configured ? 'Oui' : 'Non'],
        ['Endpoint Matter', d.matterEndpoint >= 0 ? d.matterEndpoint : '-'],
        ['Statut', d.status || '-'],
        ['Clusters entrants', (d.inClusters || []).join(' ') || '-'],
        ['Clusters sortants', (d.outClusters || []).join(' ') || '-']
      ];
      if (d.zlinky) rows.splice(9, 0, ['Mode TIC', d.linkyMode >= 0 ? d.linkyMode : 'inconnu']);
      var h = '<table class="table table-sm zb-info">';
      rows.forEach(function(r) { h += '<tr><td>' + esc(r[0]) + '</td><td>' + esc(r[1]) + '</td></tr>'; });
      return h + '</table>';
    }

    function statesTable(d) {
      var list = (d.states || []).filter(function(s) { return s.inMode; });
      if (!list.length) return '<p class="text-muted">' + (d.tmpl ? 'Aucun attribut.' :
        (d.lora && !d.identified ? 'Template appliqu\u00e9 apr\u00e8s l\u2019identification de l\u2019\u00e9metteur (prochaines trames).' :
         'Pas de template pour ce mod\u00e8le.')) + '</p>';
      if (d.lora) {
        // Rien d'inscriptible en LoRa : lecture a la demande (POLL) par ligne
        var lh = '<div class="table-responsive"><table class="table table-sm align-middle" style="font-size:13px;">' +
          '<thead><tr><th>Nom</th><th>Valeur</th><th>Unit\u00e9</th><th></th></tr></thead><tbody>';
        list.forEach(function(s) {
          lh += '<tr' + (s.vis ? '' : ' class="text-muted" title="Non affich\u00e9 sur la page Mesures"') + '>' +
            '<td>' + esc(s.n) + '</td>' +
            '<td style="font-family:Courier New,monospace;"><span id="zb-s-' + s.i + '" title="Brut : ' + esc(s.raw) + '">' + esc(s.v === '' ? '-' : s.v) + '</span></td>' +
            '<td>' + esc(s.u) + '</td>' +
            '<td><button class="btn btn-sm btn-info text-white zb-poll" data-i="' + s.i + '" title="Lire (POLL)">&#8635;</button></td></tr>';
        });
        return lh + '</tbody></table></div>';
      }
      var h = '<div class="table-responsive"><table class="table table-sm align-middle" style="font-size:13px;">' +
        '<thead><tr><th>Nom</th><th>Cluster</th><th>Attribut</th><th>Mfr</th><th>Valeur</th><th>Unit\u00e9</th><th></th></tr></thead><tbody>';
      list.forEach(function(s) {
        var id = 'zb-s-' + s.i;
        h += '<tr' + (s.vis ? '' : ' class="text-muted" title="Non affich\u00e9 sur la page Mesures"') + '>' +
          '<td>' + esc(s.n) + '</td>' +
          '<td><span class="zb-badge" style="background:#0d6efd;">' + hex4(s.cl) + '</span></td>' +
          '<td><span class="zb-badge" style="background:#6c757d;">' + hex4(s.at) + '</span></td>' +
          '<td>' + (s.mfr ? '<span class="zb-badge" style="background:#dc3545;">' + hex4(s.mfr) + '</span>' : '-') + '</td>' +
          '<td style="font-family:Courier New,monospace;">' +
            (s.w ? '<input type="text" class="form-control form-control-sm d-inline-block" style="width:110px;" id="' + id + '" value="' + esc(s.v) + '">'
                 : '<span id="' + id + '" title="Brut : ' + esc(s.raw) + '">' + esc(s.v === '' ? '-' : s.v) + '</span>') +
          '</td><td>' + esc(s.u) + '</td><td class="text-nowrap">' +
            '<button class="btn btn-sm btn-info text-white zb-read" data-i="' + s.i + '">Lire</button>' +
            (s.w ? ' <button class="btn btn-sm btn-success zb-write" data-i="' + s.i + '">\u00c9crire</button>' : '') +
          '</td></tr>';
      });
      return h + '</tbody></table></div>';
    }

    function rawTable(d) {
      var h = '<table class="table table-sm" style="font-size:12px;"><thead><tr><th>Cluster</th><th>Attribut</th><th>Hexa</th><th>Texte</th><th>Type</th><th>\u00c2ge</th></tr></thead><tbody>';
      (d.raw || []).forEach(function(r) {
        h += '<tr><td>' + hex4(r.cl) + '</td><td>' + hex4(r.at) + ' (' + r.at + ')</td><td style="font-family:monospace;word-break:break-all;">' + esc(r.v) +
          '</td><td>' + esc(r.txt) + '</td><td>' + (r.zt ? '0x' + ('0' + r.zt.toString(16)).slice(-2).toUpperCase() : '-') + '</td><td>' + fmtAgo(r.age) + '</td></tr>';
      });
      return h + '</tbody></table>';
    }

    function render(d) {
      cur = d;
      var title = esc(devName(d));
      var h =
        '<button onclick="history.back()" class="btn btn-primary mb-3">&larr; Retour</button>' +
        '<div class="zb-card p-4 mb-4">' +
          '<div class="d-flex flex-wrap align-items-center gap-3 mb-3 pb-3 border-bottom">' +
            '<div class="zb-head p-0" style="flex:1;min-width:240px;">' + devIcon(d) +
              '<div><div id="zb-title" style="font-size:22px;font-weight:bold;">' + title +
                ' <button class="btn btn-sm btn-link text-muted" id="zb-edit" title="Modifier le nom">&#9998;</button></div>' +
              '<div class="text-muted small">MAC : ' + esc(d.ieee) + '</div></div>' +
              '<span class="zb-radio ms-2" id="zb-radio">' + radioIcon(d.radio, d) + '</span></div>' +
            '<div class="d-flex flex-wrap gap-2">' +
              (d.lora ? '' :
                '<button class="btn btn-warning btn-sm" id="zb-refresh" title="Relire fabricant, mod\u00e8le et version">Rafra\u00eechir</button>' +
                '<button class="btn btn-secondary btn-sm" id="zb-config" title="Renvoyer bind et reporting du template">Reconfigurer</button>' +
                (d.ha ? '<button class="btn btn-warning btn-sm" id="zb-discover">MQTT Discover</button>' : '')) +
              '<button class="btn btn-danger btn-sm" id="zb-delete">Supprimer</button>' +
            '</div>' +
          '</div>' +
          '<h5>Informations g\u00e9n\u00e9rales</h5><div id="zb-info">' + info(d) + '</div>' +
          '<h5 class="mt-4">Attributs</h5><div id="zb-states">' + statesTable(d) + '</div>' +
          ((d.actions || []).length || d.ir ? '<h5 class="mt-4" id="zb-actions-h"' + ((d.actions || []).length ? '' : ' style="display:none;"') + '>Actions</h5>' +
            '<div class="zb-actions rounded" id="zb-actions">' + actionsHtml(d) + '</div>' : '') +
          (d.ir ? '<h5 class="mt-4">T\u00e9l\u00e9commande infrarouge</h5><div id="zb-ir" class="zb-actions rounded p-3"></div>' : '') +
          '<details class="mt-4"><summary class="fw-semibold">Valeurs brutes re\u00e7ues</summary><div id="zb-raw" class="mt-2">' + rawTable(d) + '</div></details>' +
        '</div>';
      $('#zb-dev').html(h);
      bindActions($('#page-content'));
      if (d.ir) irPanel(d.ieee);
    }

    // Mise a jour sans reconstruire la page (champs en cours de saisie intacts)
    function update(d) {
      cur = d;
      $('#zb-radio').html(radioIcon(d.radio, d));
      $('#zb-info').html(info(d));
      (d.states || []).forEach(function(s) {
        var el = document.getElementById('zb-s-' + s.i);
        if (!el) return;
        if (el.tagName === 'INPUT') {
          if (document.activeElement !== el && el.value !== s.v && !el.dataset.edit) el.value = s.v;
        } else {
          setText(el, s.v === '' ? '-' : s.v);
          el.title = 'Brut : ' + s.raw;
        }
      });
      $('#zb-raw').html(rawTable(d));
      syncCovers(d.ieee, d.cover);
    }

    function load(first) {
      $.getJSON('/api/zigbee/device?ieee=' + ieee).done(function(d) {
        if (first) render(d); else update(d);
      }).fail(function(xhr) {
        if (first) $('#zb-dev').html('<div class="alert alert-danger">' + esc(errMsg(xhr, 'Appareil introuvable')) + '</div>' +
          '<a class="btn btn-primary" href="#zigbee">Liste des appareils</a>');
      });
    }

    var $c = $('#page-content');
    $c.off('.zbdev');
    $c.on('input.zbdev', 'input[id^="zb-s-"]', function() { this.dataset.edit = '1'; });
    $c.on('click.zbdev', '.zb-read', function() {
      var s = cur && cur.states[Number($(this).data('i'))];
      if (!s) return;
      var $b = $(this).prop('disabled', true);
      post('/api/zigbee/read', { ieee: ieee, cluster: s.cl, attr: s.at, mfr: s.mfr })
        .done(function() { setTimeout(function() { load(false); }, 1500); })
        .fail(function(xhr) { A.showAlert(esc(errMsg(xhr)), 'danger'); })
        .always(function() { setTimeout(function() { $b.prop('disabled', false); }, 1500); });
    });
    $c.on('click.zbdev', '.zb-poll', function() {
      var s = cur && cur.states[Number($(this).data('i'))];
      if (!s || !cur.lora) return;
      var $b = $(this).prop('disabled', true);
      post('/api/lora/poll', { slot: cur.slot, cluster: hex4(s.cl), attr: hex4(s.at) })
        .done(function() {
          A.showAlert('Lecture demand\u00e9e : r\u00e9ponse \u00e0 la prochaine trame de l\u2019\u00e9metteur');
          // La requete part dans la fenetre ouverte apres un uplink : relire la fiche un moment
          var n = 0, t = setInterval(function() { load(false); if (++n >= 6) { clearInterval(t); $b.prop('disabled', false); } }, 5000);
        })
        .fail(function(xhr) { A.showAlert(esc(errMsg(xhr)), 'danger'); $b.prop('disabled', false); });
    });
    $c.on('click.zbdev', '.zb-write', function() {
      var s = cur && cur.states[Number($(this).data('i'))];
      if (!s) return;
      var el = document.getElementById('zb-s-' + s.i);
      var val = $.trim(el.value);
      // La page divise par le coefficient, comme la LiXee-Box : le serveur
      // recoit la valeur brute de l'attribut
      if (s.coef && s.coef !== 1) {
        var f = parseFloat(val.replace(',', '.'));
        if (isNaN(f)) { A.showAlert('Valeur num\u00e9rique attendue', 'warning'); return; }
        val = String(Math.round(f / s.coef));
      }
      var $b = $(this).prop('disabled', true);
      post('/api/zigbee/write', { ieee: ieee, cluster: s.cl, attr: s.at, type: s.wt, value: val, mfr: s.mfr })
        .done(function() {
          A.showAlert('\u00c9criture envoy\u00e9e');
          delete el.dataset.edit;
          setTimeout(function() { post('/api/zigbee/read', { ieee: ieee, cluster: s.cl, attr: s.at, mfr: s.mfr }); }, 600);
          setTimeout(function() { load(false); }, 2500);
        })
        .fail(function(xhr) { A.showAlert(esc(errMsg(xhr)), 'danger'); })
        .always(function() { $b.prop('disabled', false); });
    });
    $c.on('click.zbdev', '#zb-edit', function() {
      var name = (cur && cur.alias) || '';
      $('#zb-title').html('<div class="input-group input-group-sm" style="max-width:320px;">' +
        '<input type="text" class="form-control" id="zb-alias" maxlength="32" value="' + esc(name) + '" placeholder="Nom de l\'appareil">' +
        '<button class="btn btn-success" id="zb-alias-save">&#10003;</button><button class="btn btn-secondary" id="zb-alias-cancel">&#10005;</button></div>');
      $('#zb-alias').focus().select();
    });
    $c.on('keydown.zbdev', '#zb-alias', function(e) {
      if (e.key === 'Enter') { e.preventDefault(); $('#zb-alias-save').click(); }
      if (e.key === 'Escape') { e.preventDefault(); $('#zb-alias-cancel').click(); }
    });
    $c.on('click.zbdev', '#zb-alias-cancel', function() { render(cur); });
    $c.on('click.zbdev', '#zb-alias-save', function() {
      var alias = $.trim($('#zb-alias').val());
      post('/api/zigbee/alias', { ieee: ieee, alias: alias }).done(function() {
        cur.alias = alias;
        render(cur);
        A.showAlert('Nom enregistr\u00e9');
      }).fail(function(xhr) { A.showAlert(esc(errMsg(xhr)), 'danger'); });
    });
    $c.on('click.zbdev', '#zb-refresh', function() {
      confirmPost(null, '/api/zigbee/refresh', { ieee: ieee }, function() { setTimeout(function() { load(false); }, 2000); });
    });
    $c.on('click.zbdev', '#zb-config', function() {
      confirmPost('Renvoyer le bind et la configuration du reporting \u00e0 l\'appareil ?', '/api/zigbee/configure', { ieee: ieee });
    });
    $c.on('click.zbdev', '#zb-discover', function() {
      confirmPost(null, '/api/zigbee/discover', { ieee: ieee });
    });
    $c.on('click.zbdev', '#zb-delete', function() {
      if (cur && cur.lora) {
        confirmPost('Supprimer cet appareil LoRa et son appairage ?', '/api/lora/remove', { slot: cur.slot },
          function() { window.location.hash = '#configLora'; });
        return;
      }
      confirmPost('Supprimer cet appareil ? Il quittera le r\u00e9seau Zigbee et ses donn\u00e9es seront effac\u00e9es.',
        '/api/zigbee/delete', { ieee: ieee }, function() { window.location.hash = '#zigbee'; });
    });

    load(true);
    A.pageInterval(function() { load(false); }, 5000);
  }

  // --------------------------------------------------------------------------
  // #dashboard : jauges, batteries et valeurs des templates
  // --------------------------------------------------------------------------

  var GAUGE_LIBS = ['js/raphael-min.js', 'js/justgage.min.js'];

  function num(v) { var f = parseFloat(String(v).replace(',', '.')); return isNaN(f) ? 0 : f; }

  function pageDashboard() {
    A.setContent(CSS +
      '<div class="container py-4"><h4>Tableau de bord</h4>' +
        '<div class="row g-4" id="zb-dash"><div class="col-12 text-center p-3"><div class="spinner-border"></div></div></div>' +
      '</div>');
    var gauges = {};      // id -> JustGage
    var since = 0;

    function build(r) {
      since = r.now || 0;
      var h = '', specs = [];
      (r.devices || []).forEach(function(d) {
        var items = (d.states || []).filter(function(s) { return s.j === 'gauge' || s.j === 'battery' || s.j === 'text'; });
        var acts = d.actions || [];
        if (!items.length && !acts.length) return;
        h += '<div class="col-12 col-md-6 col-xl-4 zb-col"><div class="zb-card p-3">' +
          '<h5 class="d-flex align-items-center gap-2">' + devIcon(d).replace('<img ', '<img style="height:32px;width:32px;object-fit:contain;" ') +
          '<a href="' + devLink(d) + '" class="text-reset text-decoration-none">' + esc(d.alias || ('@Mac : ' + d.ieee)) + '</a></h5>' +
          '<div class="row g-2">';
        items.forEach(function(s) {
          var id = 'zb-g-' + d.ieee + '-' + s.k;
          if (s.j === 'text') {
            h += '<div class="col-12 text-center small">' + esc(s.n) + '<br><span id="' + id + '" style="font-size:24px;font-family:Courier New,monospace;">' +
              esc(s.v === '' ? '-' : s.v) + '</span>&nbsp;' + esc(s.u) + '</div>';
          } else {
            h += '<div class="col-6 text-center"><div id="' + id + '" class="zb-gauge"></div><div class="small" style="margin-top:-20px;">' + esc(s.n) + '</div></div>';
            specs.push({ id: id, s: s });
          }
        });
        h += '</div>' + (acts.length ? '<div class="zb-actions mt-3 rounded">' + actionsHtml(d) + '</div>' : '') + '</div></div>';
      });
      if (!h) h = '<div class="col-12 text-center" style="font-size:24px;font-weight:bold;">Aucune donn\u00e9e de tableau de bord pour l\'instant</div>';
      $('#zb-dash').html(h);
      bindActions($('#page-content'));

      specs.forEach(function(g) {
        var s = g.s, v = num(s.v), max = Number(s.max) || 100, min = Number(s.min) || 0;
        if (typeof JustGage === 'undefined') {
          $('#' + g.id).html('<div style="font-size:24px;font-weight:bold;padding-top:50px;">' + esc(s.v === '' ? '-' : s.v + ' ' + s.u) + '</div>');
          return;
        }
        try {
          var opt = {
            id: g.id, value: Math.min(Math.max(v, min), max), min: min, max: max,
            label: s.u, relativeGaugeSize: true, gaugeWidthScale: 0.6, decimals: (v % 1) ? 1 : 0
          };
          if (s.j === 'battery') opt.levelColors = ['#dc3545', '#f9c802', '#28a745'];
          else opt.pointer = true;
          gauges[g.id] = new JustGage(opt);
        } catch (e) { console.log('JustGage :', e); }
      });
      masonry('#zb-dash');
    }

    A.loadScripts(GAUGE_LIBS)
      .catch(function(e) { console.log('Jauges indisponibles :', e.message); })
      .then(function() { return $.getJSON('/api/zigbee/cards'); })
      .then(build, function() {
        $('#zb-dash').html('<div class="col-12"><div class="alert alert-danger">Erreur de chargement.</div></div>');
      });

    A.pageInterval(function() {
      $.getJSON('/api/zigbee/values?since=' + since).done(function(r) {
        since = r.now || since;
        $.each(r.devices || {}, function(ieee, d) {
          $.each(d.v || {}, function(k, v) {
            var id = 'zb-g-' + ieee + '-' + k;
            var g = gauges[id];
            if (g) { try { g.refresh(num(v)); } catch (e) {} }
            else setText(document.getElementById(id), v === '' ? '-' : v);
          });
          syncCovers(ieee, d.cover);
        });
      });
    }, 5000);
  }

  // --------------------------------------------------------------------------
  // #zigbee : appareils (ex /configDevices) et coordinateur (ex /configZigbee)
  // --------------------------------------------------------------------------

  // Icones de la LiXee-Box (Bootstrap Icons / Feather, MIT)
  var ICO = {
    devices: '<svg xmlns="http://www.w3.org/2000/svg" style="width:16px;" width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path d="M5.5 2A3.5 3.5 0 0 0 2 5.5v5A3.5 3.5 0 0 0 5.5 14h5a3.5 3.5 0 0 0 3.5-3.5V8a.5.5 0 0 1 1 0v2.5a4.5 4.5 0 0 1-4.5 4.5h-5A4.5 4.5 0 0 1 1 10.5v-5A4.5 4.5 0 0 1 5.5 1H8a.5.5 0 0 1 0 1z"/><path d="M16 3a3 3 0 1 1-6 0 3 3 0 0 1 6 0"/></svg>',
    config: '<svg xmlns="http://www.w3.org/2000/svg" style="width:16px;" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"></circle><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"></path></svg>',
    plus: '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path d="M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16"/><path d="M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4"/></svg>',
    refresh: '<svg xmlns="http://www.w3.org/2000/svg" style="width:16px;" width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path fill-rule="evenodd" d="M8 3a5 5 0 1 0 4.546 2.914.5.5 0 0 1 .908-.417A6 6 0 1 1 8 2z"/><path d="M8 4.466V.534a.25.25 0 0 1 .41-.192l2.36 1.966c.12.1.12.284 0 .384L8.41 4.658A.25.25 0 0 1 8 4.466"/></svg>',
    card: '<svg xmlns="http://www.w3.org/2000/svg" fill="currentColor" viewBox="0 0 16 16"><path d="M14.5 3a.5.5 0 0 1 .5.5v9a.5.5 0 0 1-.5.5h-13a.5.5 0 0 1-.5-.5v-9a.5.5 0 0 1 .5-.5zm-13-1A1.5 1.5 0 0 0 0 3.5v9A1.5 1.5 0 0 0 1.5 14h13a1.5 1.5 0 0 0 1.5-1.5v-9A1.5 1.5 0 0 0 14.5 2z"/><path d="M5 8a.5.5 0 0 1 .5-.5h7a.5.5 0 0 1 0 1h-7A.5.5 0 0 1 5 8m0-2.5a.5.5 0 0 1 .5-.5h7a.5.5 0 0 1 0 1h-7a.5.5 0 0 1-.5-.5m0 5a.5.5 0 0 1 .5-.5h7a.5.5 0 0 1 0 1h-7a.5.5 0 0 1-.5-.5m-1-5a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0M4 8a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0m0 2.5a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0"/></svg>',
    mqtt: '<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" fill="currentColor"><path d="M10.657 23.994h-9.45A1.212 1.212 0 0 1 0 22.788v-9.18h0.071c5.784 0 10.504 4.65 10.586 10.386Zm7.606 0h-4.045C14.135 16.246 7.795 9.977 0 9.942V6.038h0.071c9.983 0 18.121 8.044 18.192 17.956Zm4.53 0h-0.97C21.754 12.071 11.995 2.407 0 2.372v-1.16C0 0.55 0.544 0.006 1.207 0.006h7.64C15.733 2.49 21.257 7.789 24 14.508v8.291c0 0.663 -0.544 1.195 -1.207 1.195ZM16.713 0.006h6.092A1.19 1.19 0 0 1 24 1.2v5.914c-0.91 -1.242 -2.046 -2.65 -3.158 -3.762C19.588 2.11 18.122 0.987 16.714 0.005Z"/></svg>',
    ota: '<svg xmlns="http://www.w3.org/2000/svg" style="width:16px;" width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path fill-rule="evenodd" d="M7.646 10.854a.5.5 0 0 0 .708 0l2-2a.5.5 0 0 0-.708-.708L8.5 9.293V5.5a.5.5 0 0 0-1 0v3.793L6.354 8.146a.5.5 0 1 0-.708.708z"/><path d="M4.406 3.342A5.53 5.53 0 0 1 8 2c2.69 0 4.923 2 5.166 4.579C14.758 6.804 16 8.137 16 9.773 16 11.569 14.502 13 12.687 13H3.781C1.708 13 0 11.366 0 9.318c0-1.763 1.266-3.223 2.942-3.593.143-.863.698-1.723 1.464-2.383"/></svg>',
    trash: '<svg xmlns="http://www.w3.org/2000/svg" fill="currentColor" viewBox="0 0 16 16"><path d="M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z"/><path d="M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H6a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1h3.5a1 1 0 0 1 1 1zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z"/></svg>'
  };

  // Mise en page de la LiXee-Box : colonne de boutons (Appareils / Config),
  // le bouton de la vue courante desactive, contenu a droite
  function pageZigbee(tabArg) {
    var tab = tabArg || A.params().tab || 'devices';
    var side = function(t, href, icon, label) {
      return '<a href="' + href + '" style="width:100px;height:64px;" class="btn btn-primary mb-1' +
        (tab === t ? ' disabled' : '') + '">' + icon + '<br> ' + label + '</a>&nbsp;';
    };
    A.setContent(CSS +
      '<div class="row p-4 justify-content-md-center">' +
        '<div class="col-sm-2"><div class="btn-group-horizontal">' +
          side('devices', '#zigbee?tab=devices', ICO.devices, 'Appareils') +
          side('coordinator', '#zigbee?tab=coordinator', ICO.config, 'Config') +
          side('console', '#logs', ICO.refresh, 'Console') +
        '</div></div>' +
        '<div class="col-sm-10" id="zb-tab"></div>' +
      '</div>');
    if (tab === 'coordinator') coordinatorTab();
    else if (tab === 'console') consoleTab();
    else devicesTab();
  }

  // Console des trames ZiGate (/logs + /getLogBuffer de la LiXee-Box) :
  // lignes nouvelles toutes les 2 s, pause et effacement
  function consoleTab() {
    $('#zb-tab').html(
      '<h4>Console ZiGate</h4>' +
      '<div class="d-flex flex-wrap gap-2 mb-2 align-items-center">' +
        '<button class="btn btn-sm btn-outline-primary" id="zc-pause">Pause</button>' +
        '<button class="btn btn-sm btn-outline-danger zc-cmd" data-cmd="ClearConsole">Effacer</button>' +
      '</div>' +
      '<pre id="zc-log" style="height:60vh;overflow:auto;background:#1e1e1e;color:#d4d4d4;font-size:12px;padding:10px;border-radius:6px;"></pre>');
    var last = 0, paused = false;
    function load() {
      if (paused || !$('#zc-log').length) return;
      $.getJSON('/api/zigbee/console', { since: last, max: 200 }).done(function(d) {
        var $l = $('#zc-log'), el = $l[0];
        var atEnd = el.scrollTop + el.clientHeight >= el.scrollHeight - 20;
        (d.lines || []).forEach(function(t) { $l.append(document.createTextNode(t + '\n')); });
        // Garder les 2000 dernieres lignes
        var txt = $l.text().split('\n');
        if (txt.length > 2000) $l.text(txt.slice(-2000).join('\n'));
        if (atEnd) el.scrollTop = el.scrollHeight;
        if (d.last != null) last = d.last;
      });
    }
    $('#zc-pause').on('click', function() {
      paused = !paused;
      $(this).text(paused ? 'Reprendre' : 'Pause');
      if (!paused) load();
    });
    $('.zc-cmd').on('click', function() {
      var c = $(this).data('cmd');
      post('/api/zigbee/cmd', { cmd: c }).then(function() {
        if (c === 'ClearConsole') { $('#zc-log').empty(); last = 0; }
        load();
      }, function(xhr) { A.showAlert(esc(errMsg(xhr)), 'danger'); });
    });
    load();
    A.pageInterval(load, 2000);
  }

  // Boutons d'ajout selon les modules detectes (Zigbee et/ou LoRa). Tant que
  // /api/system/info n'a pas repondu, seul le bouton Zigbee est propose.
  function addDeviceButtons() {
    var m = A.modules() || {};
    var known = ('zigbee' in m) || ('lora' in m);
    var h = '';
    if (!known || m.zigbee) h += '<a class="btn btn-primary m-1" href="#assistDevice">Ajouter un appareil' + (m.lora ? ' Zigbee' : '') + '</a>';
    if (m.lora) h += '<a class="btn btn-primary m-1" href="#assistLora">Ajouter un appareil' + (m.zigbee ? ' LoRa' : '') + '</a>';
    return h;
  }

  function devicesTab() {
    $('#zb-tab').html(
      '<h4>Config appareils Zigbee</h4>' +
      '<div class="d-flex justify-content-end"><a class="btn btn-primary mb-1" href="#assistDevice" style="width:120px;height:64px;">' +
        ICO.plus + '<br> Ajouter</a></div><br>' +
      '<h5>Liste des appareils</h5>' +
      '<div class="row g-4" id="zb-cfg-list" style="font-size:12px;"><div class="col-12 text-center"><div class="spinner-border"></div></div></div>');
    var ha = false;

    function row(k, v) {
      return '<tr><td class="zbc-k">' + esc(k) + '</td><td class="zbc-v">' + v + '</td></tr>';
    }

    function load() {
      $.when($.getJSON('/api/zigbee/devices'), $.getJSON('/api/zigbee/cards')).done(function(a, b) {
        var devs = a[0] || [];
        ha = !!(b[0] && b[0].ha);
        if (!devs.length) {
          $('#zb-cfg-list').html('<div class="col-12" align="center" style="height:100px;font-size:28px;font-weight:bold;">Aucun appareil pour l\'instant</div>');
          return;
        }
        var h = '';
        devs.forEach(function(d) {
          var fiche = '#device?ieee=' + esc(d.ieee);
          h += '<div class="col-12 col-sm-6 col-md-4 col-lg-3 zbc-cont"><div class="zbc-card">' +
            '<div class="zbc-head"><a href="' + fiche + '">' + devIcon(d) + esc(d.alias || d.ieee) + '</a>' +
              '<span class="zb-radio">' + radioIcon(d.radio, d) + '</span></div>' +
            '<div class="zbc-body"><table style="width:100%;font-size:12px;">' +
              row('Manufacturer', esc(d.manufacturer || '')) +
              row('Model', esc(d.model || '')) +
              row('Short Addr', hex4(d.short)) +
              row('Device Id', hex4(d.deviceId)) +
              row('Soft Version', esc(d.swVersion || '') + ' <a class="zb-c" data-op="refresh" data-ieee="' + esc(d.ieee) + '" title="Relire la version">' + ICO.refresh + '</a>') +
              row('Last seen', esc(fmtSeen(d))) +
              row('LQI', hex2(d.lqi)) +
            '</table>' +
            '<div class="zb-ota" data-ieee="' + esc(d.ieee) + '" style="display:none;margin-top:10px;"></div>' +
            '<div class="zbc-btns">' +
              '<a href="' + fiche + '" class="btn btn-info" title="Fiche appareil">' + ICO.card + '</a>' +
              '<a href="#zigbeeOta?ieee=' + esc(d.ieee) + '" class="btn btn-warning" title="Mise &agrave; jour du firmware (OTA)">' + ICO.ota + '</a>' +
              (ha ? '<button class="btn btn-warning zb-c" data-op="discover" data-ieee="' + esc(d.ieee) + '" title="MQTT Discover">' + ICO.mqtt + '</button>' : '') +
              '<button class="btn btn-warning zb-c" data-op="refresh" data-ieee="' + esc(d.ieee) + '" title="Refresh">' + ICO.refresh + '</button>' +
              '<button class="btn btn-danger zb-c" data-op="delete" data-ieee="' + esc(d.ieee) + '" title="Supprimer">' + ICO.trash + '</button>' +
            '</div></div></div></div>';
        });
        $('#zb-cfg-list').html(h);
        otaBars();
      }).fail(function() { $('#zb-cfg-list').html('<div class="col-12"><div class="alert alert-danger">Erreur de chargement.</div></div>'); });
    }

    // Avancement d'une mise a jour OTA sur la fiche concernee (OTAUpdateBar de la Box)
    function otaBars() {
      if (!$('.zb-ota').length) return;
      $.getJSON('/api/zigbee/ota/status', function(d) {
        $('.zb-ota').hide();
        if (!d.ieee) return;
        var el = $('.zb-ota[data-ieee="' + d.ieee + '"]');
        var running = d.state === 'notified' || d.state === 'transfer';
        var show = running || ((d.state === 'done' || d.state === 'error') && d.since >= 0 && d.since < 600);
        if (!el.length || !show) return;
        el.html(otaHtml(d)).show();
      });
    }
    A.pageInterval(otaBars, 5000);

    $('#zb-tab').off('.zbcfg').on('click.zbcfg', '.zb-c', function() {
      var op = $(this).data('op'), ieee = $(this).attr('data-ieee');
      if (op === 'delete') {
        confirmPost('Supprimer l\'appareil ' + ieee + ' ? Il quittera le réseau Zigbee et ses données seront effacées.',
          '/api/zigbee/delete', { ieee: ieee }, load);
      } else {
        confirmPost(null, '/api/zigbee/' + op, { ieee: ieee }, op === 'refresh' ? function() { setTimeout(load, 2500); } : null);
      }
    });
    load();
  }

  // Telecommande infrarouge (Zosung) : apprentissage, codes nommes, emission.
  // Le numero d'un code (#) sert aux actions des regles (commande 201).
  function irPanel(ieee) {
    var $p = $('#zb-ir');
    $p.html(
      '<div class="d-flex flex-wrap gap-2 align-items-center mb-2">' +
        '<button class="btn btn-sm btn-primary" id="ir-learn">Apprendre un code</button>' +
        '<button class="btn btn-sm btn-outline-secondary" id="ir-stop">Arr\u00eater</button>' +
        '<span class="small text-muted" id="ir-state"></span></div>' +
      '<div id="ir-learned" class="mb-3" style="display:none;">' +
        '<label class="form-label small mb-0">Dernier code appris</label>' +
        '<textarea class="form-control form-control-sm font-monospace" id="ir-code" rows="2" readonly></textarea>' +
        '<div class="d-flex flex-wrap gap-2 mt-2"><input class="form-control form-control-sm" id="ir-name" maxlength="31" placeholder="Nom (ex. Clim ON)" style="max-width:220px;">' +
        '<button class="btn btn-sm btn-success" id="ir-save">Enregistrer</button>' +
        '<button class="btn btn-sm btn-outline-primary" id="ir-test">Tester</button></div></div>' +
      '<div id="ir-list"></div>' +
      '<details class="mt-2"><summary class="small">Envoyer un code (base64)</summary>' +
        '<div class="d-flex gap-2 mt-2"><input class="form-control form-control-sm font-monospace" id="ir-raw">' +
        '<button class="btn btn-sm btn-primary" id="ir-raw-send">Envoyer</button></div></details>');

    function op(o, msg) {
      o.ieee = ieee;
      return post('/api/zigbee/ir', o).then(function(r) {
        A.showAlert(esc(okMsg(r, msg || 'OK')));
        load();
        if (o.op === 'save' || o.op === 'delete') refreshActions();
      }, function(xhr) { A.showAlert(esc(errMsg(xhr)), 'danger'); });
    }
    // Chaque code enregistre est une action de l'appareil : barre d'actions relue
    function refreshActions() {
      $.getJSON('/api/zigbee/device?ieee=' + encodeURIComponent(ieee)).done(function(d) {
        $('#zb-actions').html(actionsHtml(d));
        $('#zb-actions-h').toggle((d.actions || []).length > 0);
      });
    }
    var lastCode = '';
    function load() {
      if (!$('#zb-ir').length) return;
      $.getJSON('/api/zigbee/ir', { ieee: ieee }).done(function(st) {
        $('#ir-state').text(st.learning ? 'En apprentissage : visez le r\u00e9cepteur et appuyez sur la touche\u2026' :
                            (st.sending ? 'Emission en cours\u2026' : (st.lastSend === 'ok' ? 'Dernier code \u00e9mis' : '')));
        if (st.learned && st.learned !== lastCode) {
          lastCode = st.learned;
          $('#ir-code').val(st.learned);
          $('#ir-learned').show();
        }
        var h = '';
        (st.codes || []).forEach(function(c) {
          h += '<div class="d-flex align-items-center gap-2 mb-1"><span class="badge bg-secondary">#' + c.i + '</span>' +
               '<span style="flex:1;">' + esc(c.name) + '</span>' +
               '<button class="btn btn-sm btn-primary ir-send" data-i="' + c.i + '">Envoyer</button>' +
               '<button class="btn btn-sm btn-outline-danger ir-del" data-i="' + c.i + '">&times;</button></div>';
        });
        $('#ir-list').html(h ? '<label class="form-label small mb-1">Codes enregistr\u00e9s (aussi en actions de l\'appareil : fiche, r\u00e8gles, groupes, Home Assistant)</label>' + h : '<div class="small text-muted">Aucun code enregistr\u00e9.</div>');
      });
    }
    $p.on('click', '#ir-learn', function() { op({ op: 'learn' }); });
    $p.on('click', '#ir-stop', function() { op({ op: 'stop' }); });
    $p.on('click', '#ir-save', function() {
      var n = $.trim($('#ir-name').val());
      if (!n) { A.showAlert('Donnez un nom au code', 'warning'); return; }
      op({ op: 'save', name: n, code: $('#ir-code').val() });
    });
    $p.on('click', '#ir-test', function() { op({ op: 'send', code: $('#ir-code').val() }); });
    $p.on('click', '#ir-raw-send', function() { op({ op: 'send', code: $.trim($('#ir-raw').val()) }); });
    $p.on('click', '.ir-send', function() { op({ op: 'send', index: Number($(this).data('i')) }); });
    $p.on('click', '.ir-del', function() {
      if (!window.confirm('Supprimer ce code ?')) return;
      op({ op: 'delete', index: Number($(this).data('i')) });
    });
    load();
    A.pageInterval(load, 3000);
  }

  // Etat d'une mise a jour OTA Zigbee (carte de la liste et page OTA)
  function otaHtml(d) {
    if (d.state === 'notified') {
      return '<div class="text-center">Image annonc&eacute;e, en attente de l\'appareil&hellip;</div>' +
        '<div class="progress"><div class="progress-bar progress-bar-striped progress-bar-animated" style="width:100%"></div></div>' +
        '<div class="text-center text-muted small">Un appareil sur pile se r&eacute;veille parfois apr&egrave;s plusieurs minutes : appuyez sur son bouton.</div>';
    }
    if (d.state === 'transfer') {
      return '<div class="text-center">En cours : ' + d.pct + ' %</div>' +
        '<div class="progress"><div class="progress-bar" style="width:' + d.pct + '%"></div></div>' +
        '<div class="text-center text-muted small">' + d.offset + ' / ' + d.total + ' octets</div>';
    }
    if (d.state === 'done') {
      return '<div class="text-center" style="color:#27ae60;font-weight:500;">Mise &agrave; jour termin&eacute;e, l\'appareil red&eacute;marre.</div>';
    }
    if (d.state === 'error') {
      return '<div class="text-center" style="color:#c0392b;font-weight:500;">&Eacute;chec : ' + esc(d.msg) + '</div>';
    }
    return '';
  }

  // ==========================================================================
  // PAGE : mise a jour du firmware d'un appareil Zigbee (/ota?id= de la Box)
  // ==========================================================================
  var OTA_CHUNK = 16384;

  function pageZigbeeOta() {
    var ieee = String(A.params().ieee || A.params().id || '');
    A.setContent('<div class="container" style="max-width:720px;margin-top:20px;" id="zb-ota-page"><div class="spinner-border"></div></div>');
    $.getJSON('/api/zigbee/device?ieee=' + encodeURIComponent(ieee)).done(function(d) {
      render(d);
    }).fail(function(xhr) {
      $('#zb-ota-page').html('<div class="alert alert-danger">' + esc(errMsg(xhr, 'Appareil introuvable')) + '</div>' +
        '<a class="btn btn-primary" href="#configDevices">Retour</a>');
    });

    function render(d) {
      $('#zb-ota-page').html(
        '<h4>Mise &agrave; jour du firmware &mdash; ' + esc(devName(d)) + '</h4>' +
        '<p style="color:#6c757d;font-family:Courier New,monospace;">' + esc(d.ieee) + ' &middot; ' + esc(d.model || '') + '</p>' +
        '<div class="alert alert-info">Choisissez le fichier OTA Zigbee du fabricant (<b>.ota</b>, <b>.zigbee</b> ou <b>.bin</b> ' +
          'commen&ccedil;ant par l\'en-t&ecirc;te 0x0BEEF11E). Le pont l\'annonce &agrave; l\'appareil, qui le t&eacute;l&eacute;charge ' +
          'ensuite bloc par bloc : comptez de 10 minutes &agrave; plus d\'une heure. Un appareil sur pile doit &ecirc;tre r&eacute;veill&eacute;.</div>' +
        '<div class="mb-3"><label class="form-label">Fichier OTA</label>' +
          '<input class="form-control" type="file" id="zb-ota-file" accept=".ota,.zigbee,.bin"></div>' +
        '<button class="btn btn-warning" id="zb-ota-go">Lancer la mise &agrave; jour</button> ' +
        '<button class="btn btn-outline-primary" id="zb-ota-again" style="display:none;">Relancer avec le fichier d&eacute;j&agrave; d&eacute;pos&eacute;</button> ' +
        '<button class="btn btn-secondary" id="zb-ota-cancel" style="display:none;">Annuler</button> ' +
        '<a class="btn btn-link" href="#configDevices">Retour</a>' +
        '<div style="margin-top:20px;"><div id="zb-ota-msg" style="font-weight:500;"></div>' +
          '<div class="progress mt-2" style="height:8px;"><div id="zb-ota-up" class="progress-bar bg-secondary" style="width:0%;"></div></div>' +
          '<div id="zb-ota-state" class="mt-3"></div></div>');

      function setMsg(t, color) { $('#zb-ota-msg').html(t).css('color', color || ''); }
      function post(url, data, raw) {
        var opt = { url: url, method: 'POST' };
        if (raw) { opt.data = data; opt.processData = false; opt.contentType = 'application/octet-stream'; }
        else { opt.data = JSON.stringify(data); opt.contentType = 'application/json'; }
        return $.ajax(opt);
      }
      function upload(file) {
        var n = Math.ceil(file.size / OTA_CHUNK), i = 0;
        setMsg('Envoi du fichier au pont&hellip;');
        return post('/api/lora/ota/init?id=' + encodeURIComponent(d.ieee), { totalSize: file.size }).then(function next() {
          if (i >= n) return post('/api/lora/ota/finish', {});
          var part = file.slice(i * OTA_CHUNK, Math.min((i + 1) * OTA_CHUNK, file.size));
          return post('/api/lora/ota/chunk?n=' + i, part, true).then(function() {
            i++;
            $('#zb-ota-up').css('width', Math.round(100 * i / n) + '%');
            return next();
          });
        });
      }

      $('#zb-ota-go').on('click', function() {
        var file = $('#zb-ota-file')[0].files[0];
        if (!file) { setMsg('Choisissez un fichier.', '#c0392b'); return; }
        if (file.size > 1048576) { setMsg('Fichier trop volumineux (1 Mo maximum).', '#c0392b'); return; }
        $('#zb-ota-go').prop('disabled', true);
        upload(file).then(function(r) {
          setMsg(esc((r && r.message) || 'Image annonc\u00e9e'), '#27ae60');
          poll();
        }, function(xhr) {
          setMsg('Erreur : ' + esc(errMsg(xhr, 'd\u00e9p\u00f4t refus\u00e9')), '#c0392b');
          $('#zb-ota-go').prop('disabled', false);
        });
      });
      $('#zb-ota-again').on('click', function() {
        A.postJSON('/api/zigbee/ota/start', { ieee: d.ieee }).then(function(r) {
          setMsg(esc((r && r.message) || 'Image annonc\u00e9e'), '#27ae60');
          poll();
        }, function(xhr) { setMsg('Erreur : ' + esc(errMsg(xhr, 'refus')), '#c0392b'); });
      });
      $('#zb-ota-cancel').on('click', function() {
        if (!window.confirm('Abandonner la mise \u00e0 jour ?')) return;
        A.postJSON('/api/zigbee/ota/cancel', {}).then(poll);
      });

      function poll() {
        if (!$('#zb-ota-page').length) return;
        $.getJSON('/api/zigbee/ota/status?ieee=' + encodeURIComponent(d.ieee), function(st) {
          var mine = st.ieee === d.ieee;
          var running = mine && (st.state === 'notified' || st.state === 'transfer');
          $('#zb-ota-state').html(mine ? otaHtml(st) : '');
          $('#zb-ota-cancel').toggle(running);
          $('#zb-ota-again').toggle(!running && !!st.file);
          $('#zb-ota-go').prop('disabled', running);
        });
      }
      poll();
      A.pageInterval(poll, 4000);
    }
  }

  function coordinatorTab() {
    $('#zb-tab').html(
      '<h4>Config Zigbee</h4>' +
      '<div align="right">' +
        '<button type="button" class="btn btn-primary zb-cmd" data-cmd="Network">Réseau</button> ' +
        '<button type="button" class="btn btn-primary zb-cmd" data-cmd="Reset">Redémarrer</button> ' +
        '<button type="button" class="btn btn-danger zb-cmd" data-cmd="ErasePDM" data-confirm="Effacer la PDM ? Tout le réseau Zigbee sera perdu et les appareils devront être réappairés.">RAZ</button> ' +
      '</div>' +
      '<h5 class="card-title mb-4">Paramètres</h5>' +
      '<div class="card mx-auto shadow-sm"><div class="card-body"><div class="mb-3">' +
        '<div id="zb-coord"><div class="spinner-border spinner-border-sm"></div></div>' +
        '<label>Set Led : </label><br>' +
        '<button type="button" class="btn btn-primary zb-cmd" data-cmd="SetLed" data-value="1">LED ON</button>&nbsp;' +
        '<button type="button" class="btn btn-primary zb-cmd" data-cmd="SetLed" data-value="0">LED OFF</button><br>' +
        '<label for="zb-chan">Set channel mask</label>' +
        '<input class="form-control" id="zb-chan" type="text" placeholder="ex. 15 ou 0x07FFF800"><br>' +
        '<button type="button" class="btn btn-primary" id="zb-chan-set">Set Channel</button><br>' +
      '</div></div></div>' +
      // Complements du pont : ouverture du reseau et nettoyage de la table
      '<h5 class="card-title mb-4 mt-4">Réseau Zigbee</h5>' +
      '<div class="card mx-auto shadow-sm"><div class="card-body">' +
        '<div class="mb-3"><button class="btn btn-success" id="zb-pj">Autoriser l\'appairage (60 s)</button> <span id="zb-pj-state" class="small text-muted"></span></div>' +
        '<div><button class="btn btn-outline-secondary zb-cmd" data-cmd="CleanGhosts" data-confirm="Effacer de la table de la ZiGate les appareils inconnus de la passerelle ?">Nettoyer la table de la ZiGate</button> ' +
        '<span id="zb-ghost" class="small text-muted"></span></div>' +
      '</div></div>' +
      // Outil de lecture d'attribut a la demande
      '<h5 class="card-title mb-4 mt-4">Lecture d\'attribut à la demande</h5>' +
      '<div class="card mx-auto shadow-sm"><div class="card-body"><div class="text-muted small mb-2">Envoie une requête <i>Read Attributes</i> vers l\'appareil et affiche la valeur reçue. ' +
        'Cluster et attribut en <b>hexa</b> (ex. FF66 / 0217 = STGE).</div>' +
        '<div class="d-flex flex-wrap gap-2 align-items-end">' +
          '<div><label class="small text-muted">Appareil</label><select id="zb-rd-dev" class="form-select" style="width:240px;"></select></div>' +
          '<div><label class="small text-muted">Cluster (hex)</label><input id="zb-rd-cl" class="form-control" style="width:110px;" placeholder="FF66"></div>' +
          '<div><label class="small text-muted">Attribut (hex)</label><input id="zb-rd-at" class="form-control" style="width:110px;" placeholder="0217"></div>' +
          '<button class="btn btn-primary" id="zb-rd-go">Lire</button>' +
        '</div><div id="zb-rd-res" class="mt-3 p-2 rounded" style="display:none;"></div></div></div>');

    function cmd(c, v, question) {
      var o = { cmd: c };
      if (v !== undefined && v !== '') o.value = v;
      confirmPost(question, '/api/zigbee/cmd', o, function() { setTimeout(loadCoord, 1500); });
    }

    function loadCoord() {
      $.getJSON('/api/zigbee/coordinator').done(function(c) {
        var ok = '<span class="badge bg-success">OK</span>', nok = '<span class="badge bg-danger">Non d\u00e9marr\u00e9</span>';
        var rows = [
          ['@MAC coordinator', c.stateKnown ? esc(c.ieee) : '-'],
          ['Version coordinator', c.versionKnown ? esc(c.version) : '-'],
          ['R\u00e9seau', c.network ? ok : nok],
          ['Canal', c.channel || '-'],
          ['PAN ID', c.stateKnown ? esc(c.panId) : '-']
        ];
        var h = '';
        rows.forEach(function(r) { h += '<span> ' + esc(r[0]) + ' : </span>' + r[1] + '<br>'; });
        $('#zb-coord').html(h);
        // Champ pre-rempli avec le canal en cours (une fois, sans ecraser une saisie)
        var $ch = $('#zb-chan');
        if (c.channel && !$ch.data('prefilled') && !$ch.val() && document.activeElement !== $ch[0]) {
          $ch.val(c.channel).data('prefilled', true);
        }
        $('#zb-pj-state').text(c.permitJoin < 0 ? 'ouvert sans limite' : (c.permitJoin > 0 ? 'ouvert encore ' + c.permitJoin + ' s' : 'ferm\u00e9'));
        $('#zb-ghost').text(c.ghostPending ? 'en cours...' : (c.ghostResult >= 0 ? c.ghostResult + ' entr\u00e9e(s) effac\u00e9e(s)' : ''));
      }).fail(function() { $('#zb-coord').html('<div class="text-danger">Erreur de chargement.</div>'); });
    }

    $.getJSON('/api/zigbee/devices').done(function(devs) {
      var $s = $('#zb-rd-dev');
      (devs || []).forEach(function(d) { $s.append($('<option>').val(d.ieee).text(devName(d) + ' (' + hex4(d.short) + ')')); });
      if (!(devs || []).length) $s.append($('<option>').val('').text('Aucun appareil'));
    });

    var $t = $('#zb-tab');
    $t.off('.zbco');
    $t.on('click.zbco', '.zb-cmd', function() {
      cmd($(this).data('cmd'), $(this).attr('data-value'), $(this).attr('data-confirm'));
    });
    $t.on('click.zbco', '#zb-chan-set', function() {
      var v = $.trim($('#zb-chan').val());
      if (!v) return;
      cmd('SetChannelMask', v, 'Changer le canal ou le masque (' + v + ') ? La ZiGate va red\u00e9marrer.');
    });
    $t.on('click.zbco', '#zb-pj', function() {
      confirmPost(null, '/api/zigbee/permit_join', { duration: 60 }, loadCoord);
    });
    $t.on('click.zbco', '#zb-rd-go', function() {
      var ieee = $('#zb-rd-dev').val();
      var cl = parseInt($('#zb-rd-cl').val(), 16), at = parseInt($('#zb-rd-at').val(), 16);
      var $r = $('#zb-rd-res').show();
      if (!ieee || isNaN(cl) || isNaN(at) || cl > 0xFFFF || at > 0xFFFF) { $r.css('background', '#fce8e6').text('Appareil, cluster ou attribut invalide'); return; }
      $r.css('background', '#e8f0fe').text('Lecture envoy\u00e9e, attente de la r\u00e9ponse\u2026');
      var sent = Date.now();
      post('/api/zigbee/read', { ieee: ieee, cluster: cl, attr: at }).done(function() { poll(0); })
        .fail(function(xhr) { $r.css('background', '#fce8e6').text('\u2717 ' + errMsg(xhr, '\u00c9chec de l\'envoi')); });
      function poll(n) {
        if (n > 6) { $r.css('background', '#fff3cd').text('\u26a0 Pas de r\u00e9ponse (l\'appareil dort peut-\u00eatre, ou attribut non support\u00e9).'); return; }
        setTimeout(function() {
          $.getJSON('/api/zigbee/read_value?ieee=' + ieee + '&cluster=' + cl + '&attr=' + at).done(function(v) {
            // Valeur fraiche : recue depuis la demande (age inferieur au temps ecoule)
            if (v.value && v.age != null && v.age <= Math.ceil((Date.now() - sent) / 1000) + 1) {
              var extra = (v.text && v.text !== v.value) ? ' (= ' + v.text + ')' : '';
              $r.css('background', '#e6f4ea').html('&#10003; Valeur : <b>' + esc(v.value) + '</b>' + esc(extra));
            } else poll(n + 1);
          }).fail(function() { poll(n + 1); });
        }, 1200);
      }
    });

    loadCoord();
    A.pageInterval(loadCoord, 3000);
  }

  // --------------------------------------------------------------------------
  // #assistDevice : assistant d'appairage
  // --------------------------------------------------------------------------

  function pageAssistDevice() {
    A.setContent(CSS +
      '<div class="container py-4" style="max-width:720px;"><h4>Ajouter un appareil Zigbee</h4>' +
        '<div class="card p-4" id="zb-as"></div></div>');
    var $box = $('#zb-as');
    var baseSeq = null, joined = null, waitStart = 0, timer = null;
    var PAIR_S = 60;

    function stopTimer() { if (timer) { clearInterval(timer); timer = null; } }
    // Minuterie propre a l'assistant, enregistree aussi aupres de l'application
    // pour etre arretee si l'on quitte la page
    function startTimer(fn) { stopTimer(); timer = A.pageInterval(fn, 2000); }

    function step1() {
      stopTimer();
      $box.html(
        '<h5>1. Pr\u00e9parez l\'appareil</h5>' +
        '<p>Alimentez l\'appareil (piles, secteur) \u00e0 proximit\u00e9 de la passerelle. S\'il a d\u00e9j\u00e0 \u00e9t\u00e9 appair\u00e9 ailleurs, ' +
        'r\u00e9initialisez-le d\'abord (voir sa notice).</p>' +
        '<h5 class="mt-3">2. Ouvrez le jumelage</h5>' +
        '<p>La passerelle accepte les nouveaux appareils pendant ' + PAIR_S + ' secondes. Mettez ensuite l\'appareil en mode appairage.</p>' +
        '<button class="btn btn-success btn-lg" id="zb-as-go">Lancer le jumelage</button> ' +
        '<a class="btn btn-outline-secondary" href="#zigbee">Annuler</a>');
    }

    function step2() {
      $box.html('<div class="text-center p-3"><div class="spinner-border text-primary" style="width:3rem;height:3rem;"></div>' +
        '<h5 class="mt-3">Recherche d\'appareils\u2026</h5>' +
        '<p>Mettez maintenant l\'appareil en mode appairage.</p>' +
        '<p class="text-muted" id="zb-as-left"></p>' +
        '<button class="btn btn-outline-secondary btn-sm" id="zb-as-more">Prolonger</button> ' +
        '<button class="btn btn-outline-danger btn-sm" id="zb-as-stop">Arr\u00eater</button></div>');
      startTimer(poll);
      poll();
    }

    function step3(c) {
      var j = c.lastJoin || {};
      joined = j;
      var known = !!j.model;
      var elapsed = (Date.now() - waitStart) / 1000;
      var d = { model: j.model, deviceId: j.deviceId, ieee: j.ieee };
      $box.html(
        '<div class="d-flex align-items-center gap-3 mb-3"><div class="zb-head p-0">' + devIcon(d) + '</div>' +
          '<div><h5 class="mb-1 text-success">\u2713 Appareil trouv\u00e9</h5>' +
          '<div class="small">MAC : <b>' + esc(j.ieee) + '</b> - adresse ' + hex4(j.short) + '</div>' +
          '<div class="small">' + (known ? 'Mod\u00e8le : <b>' + esc(j.model) + '</b> ' + esc(j.manufacturer || '') :
            '<span class="spinner-border spinner-border-sm"></span> Identification en cours\u2026') + '</div>' +
          (known ? '<div class="small">' + (j.tmpl ? 'Template disponible : mesures et actions configur\u00e9es automatiquement.' :
            (elapsed > 15 ? '<span class="text-warning">Aucun template pour ce mod\u00e8le : seules les valeurs brutes seront visibles.</span>' : 'Chargement du template\u2026')) + '</div>' : '') +
          '</div></div>' +
        '<div class="mb-3"><label class="form-label" for="zb-as-alias">Nom de l\'appareil</label>' +
          '<input type="text" class="form-control" id="zb-as-alias" maxlength="32" placeholder="ex. Prise salon" value="' + esc(j.alias || '') + '"></div>' +
        '<button class="btn btn-success" id="zb-as-save">Enregistrer</button> ' +
        '<button class="btn btn-outline-primary" id="zb-as-again">Ajouter un autre appareil</button>');
      // Le modele et le template arrivent quelques secondes apres l'annonce
      if (!known || (!j.tmpl && elapsed <= 15)) startTimer(waitModel); else stopTimer();
    }

    function waitModel() {
      $.getJSON('/api/zigbee/coordinator').done(function(c) {
        var j = c.lastJoin || {};
        if (!joined || j.ieee !== joined.ieee) return;
        if (j.model !== joined.model || j.tmpl !== joined.tmpl || (Date.now() - waitStart) / 1000 > 15) {
          var typed = $('#zb-as-alias').val();
          step3(c);
          if (typed) $('#zb-as-alias').val(typed);
        }
      });
    }

    function poll() {
      $.getJSON('/api/zigbee/coordinator').done(function(c) {
        if (baseSeq !== null && c.joinSeq > baseSeq && c.lastJoin) { waitStart = Date.now(); step3(c); return; }
        if (c.permitJoin === 0) {
          stopTimer();
          $box.html('<div class="alert alert-warning">Aucun appareil d\u00e9tect\u00e9 pendant la fen\u00eatre de jumelage.</div>' +
            '<ul class="small"><li>V\u00e9rifiez que l\'appareil est bien en mode appairage (souvent un voyant clignote).</li>' +
            '<li>Rapprochez-le de la passerelle.</li><li>R\u00e9initialisez-le s\'il a d\u00e9j\u00e0 \u00e9t\u00e9 appair\u00e9.</li></ul>' +
            '<button class="btn btn-primary" id="zb-as-go">R\u00e9essayer</button>');
          return;
        }
        $('#zb-as-left').text(c.permitJoin > 0 ? 'Jumelage ouvert encore ' + c.permitJoin + ' s' : 'Jumelage ouvert');
      });
    }

    function open(duration) {
      return post('/api/zigbee/permit_join', { duration: duration });
    }

    $box.off('.zbas');
    $box.on('click.zbas', '#zb-as-go', function() {
      var $b = $(this).prop('disabled', true);
      $.getJSON('/api/zigbee/coordinator').done(function(c) {
        baseSeq = c.joinSeq || 0;
        open(PAIR_S).done(step2).fail(function(xhr) { $b.prop('disabled', false); A.showAlert(esc(errMsg(xhr)), 'danger'); });
      }).fail(function() { $b.prop('disabled', false); A.showAlert('Coordinateur injoignable', 'danger'); });
    });
    $box.on('click.zbas', '#zb-as-more', function() { open(PAIR_S); });
    $box.on('click.zbas', '#zb-as-stop', function() { open(0).always(step1); });
    $box.on('click.zbas', '#zb-as-again', function() { step1(); });
    $box.on('click.zbas', '#zb-as-save', function() {
      var alias = $.trim($('#zb-as-alias').val());
      var ieee = joined && joined.ieee;
      if (!ieee) return;
      var go = function() { window.location.hash = '#device?ieee=' + ieee; };
      if (!alias) { go(); return; }
      post('/api/zigbee/alias', { ieee: ieee, alias: alias }).done(function() {
        A.showAlert('Appareil enregistr\u00e9');
        go();
      }).fail(function(xhr) { A.showAlert(esc(errMsg(xhr)), 'danger'); });
    });

    step1();
  }

  // --------------------------------------------------------------------------
  // Enregistrement (remplace les pages placeholder de app.js)
  // --------------------------------------------------------------------------

  A.registerPage('devices', pageDevices);
  A.registerPage('device', pageDevice);
  A.registerPage('configDevice', pageDevice);
  A.registerPage('dashboard', pageDashboard);
  A.registerPage('zigbee', function() { pageZigbee(); });
  A.registerPage('configDevices', function() { pageZigbee('devices'); });
  A.registerPage('configZigbee', function() { pageZigbee('coordinator'); });
  A.registerPage('assistDevice', pageAssistDevice);
  A.registerPage('zigbeeOta', pageZigbeeOta);
  A.registerPage('logs', function() { pageZigbee('console'); });
  A.registerPage('ota', pageZigbeeOta);
})();
