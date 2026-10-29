// ============================================================================
// LiXee MatterBridge - Lot 4 : notifications et alertes
// Pages : notifications (centre), configNotifications (alertes), configNotifMail
// (email SMTP), webpush (remplace la page d'app.js : ajoute test et etat).
// Fonctions globales : pastille des notifications non lues (.AlertNotif,
// #notif-unread-badge) et bandeaux /getAlert, comme functions.js LiXee-Box.
// Contrat des routes : components/webserver/web_notif.cpp (en-tete).
// ============================================================================

(function() {
  'use strict';
  var App = window.LixeeApp;
  if (!App) return;
  var esc = App.escHtml;

  var TYPE_LABELS = ['Info', 'Attention', 'Erreur', 'Critique'];
  var TYPE_CLASSES = ['bg-info text-dark', 'bg-warning text-dark', 'bg-danger', 'bg-dark'];

  function errText(xhr) { return esc(App.apiErrorText(xhr)); }
  function num(v, def) { var n = parseFloat(String(v).replace(',', '.')); return isNaN(n) ? def : n; }
  function toInt(v, def) { var n = parseInt(v, 10); return isNaN(n) ? def : n; }

  // --------------------------------------------------------------------------
  // Pastille "non lues" et bandeaux serveur (toutes les pages)
  // --------------------------------------------------------------------------
  function refreshUnread() {
    $.ajax({ url: '/api/notifications/stats', dataType: 'json', cache: false, global: false })
      .done(function(s) {
        var n = (s && s.unread) ? s.unread : 0;
        $('.AlertNotif').css('display', n > 0 ? 'inline-block' : 'none');
        $('#notif-unread-badge').text(n > 99 ? '99+' : String(n)).toggle(n > 0);
      });
  }

  // /getAlert renvoie "etat;message" (0 succes, 1 erreur, 2 info) ou rien
  function pollToast() {
    if (document.hidden) return;
    $.ajax({ url: '/getAlert', dataType: 'text', cache: false, global: false })
      .done(function(t) {
        if (!t) return;
        var i = t.indexOf(';');
        var st = parseInt(i >= 0 ? t.substring(0, i) : '0', 10);
        var msg = i >= 0 ? t.substring(i + 1) : t;
        // 3 : appareil trouve (appairage), message HTML de la Box ramene a son texte
        if (st === 3) { window.deviceFound = true; msg = $('<div>').html(msg).text() || 'Appareil trouvé'; }
        App.showAlert(esc(msg), st === 1 ? 'danger' : (st === 2 ? 'info' : 'success'));
      });
  }

  window.addEventListener('load', function() {
    refreshUnread();
    setInterval(function() { if (!document.hidden) refreshUnread(); }, 10000);   // 10 s, comme la Box
    setInterval(pollToast, 5000);
  });

  // --------------------------------------------------------------------------
  // Etat des envois (email / WebPush)
  // --------------------------------------------------------------------------
  function sendStatusHtml(s) {
    if (!s) return '<span class="text-muted">Indisponible</span>';
    var h = '<div>Envoy&eacute;s : <strong>' + (s.sent || 0) + '</strong> &middot; &Eacute;checs : <strong>' +
            (s.failed || 0) + '</strong> &middot; Abandonn&eacute;s : <strong>' + (s.dropped || 0) + '</strong></div>';
    if (s.last) {
      h += '<div class="mt-1">Dernier essai' + (s.last.time ? ' (' + esc(s.last.time) + ')' : '') + ' : ' +
           '<span class="badge ' + (s.last.ok ? 'bg-success' : 'bg-danger') + '">' + (s.last.ok ? 'OK' : '&Eacute;chec') + '</span> ' +
           esc(s.last.message || '') + '</div>';
    } else {
      h += '<div class="mt-1 text-muted">Aucun envoi depuis le d&eacute;marrage.</div>';
    }
    return h;
  }

  // Attend le resultat d'un test (champ last modifie), 40 s maximum
  function watchStatus(kind, target, before) {
    var tries = 0;
    var timer = App.pageInterval(function() {
      tries++;
      $.getJSON('/api/notifications/send-status', function(st) {
        var s = st && st[kind];
        $(target).html(sendStatusHtml(s));
        // Fin : nouveau "dernier essai" (le test met toujours a jour ce champ)
        var last = JSON.stringify(s ? s.last : null);
        if (last !== before || tries >= 20) clearInterval(timer);
      });
    }, 2000);
  }

  function loadStatus(kind, target, cb) {
    $.getJSON('/api/notifications/send-status', function(st) {
      var s = st && st[kind];
      $(target).html(sendStatusHtml(s));
      if (cb) cb(JSON.stringify(s ? s.last : null));
    }).fail(function() { $(target).html(sendStatusHtml(null)); if (cb) cb('fail'); });
  }

  // ==========================================================================
  // PAGE : centre de notifications (/notifications LiXee-Box)
  // ==========================================================================
  var ntPage = 1, ntPerPage = 10, ntHash = '';

  function pageNotifications() {
    ntHash = '';
    var p = App.params();
    if (p.page) ntPage = Math.max(1, toInt(p.page, 1));
    App.setContent(
      '<div class="container py-4" id="nt-root">' +
        '<div class="d-flex flex-wrap justify-content-between align-items-center mb-3 gap-2">' +
          '<h4 class="mb-0">Notifications</h4>' +
          '<div class="d-flex flex-wrap gap-2">' +
            '<button class="btn btn-outline-secondary btn-sm" id="nt-test">Notification de test</button>' +
            '<button class="btn btn-primary btn-sm" id="nt-read-all">Tout vu</button>' +
            '<button class="btn btn-danger btn-sm" id="nt-clear">RAZ</button>' +
          '</div>' +
        '</div>' +
        '<div class="card shadow-sm"><div class="card-body">' +
          '<div id="nt-list"><div class="text-center p-3"><div class="spinner-border" role="status"></div></div></div>' +
          '<div class="d-flex flex-wrap justify-content-between align-items-center mt-2 gap-2">' +
            '<small class="text-muted" id="nt-info"></small>' +
            '<ul class="pagination pagination-sm mb-0" id="nt-pages"></ul>' +
          '</div>' +
        '</div></div>' +
      '</div>'
    );
    ntLoad(true);
    App.pageInterval(function() { ntLoad(false); }, 10000);

    var root = $('#nt-root');
    root.on('click', '#nt-read-all', function() {
      App.postJSON('/api/notifications/read-all', {})
        .then(function() { App.showAlert('Toutes les notifications sont marqu&eacute;es comme lues'); ntLoad(true); refreshUnread(); },
              function(xhr) { App.showAlert(errText(xhr), 'danger'); });
    });
    root.on('click', '#nt-clear', function() {
      if (!confirm('Supprimer toutes les notifications ?')) return;
      App.postJSON('/api/notifications/clear', {})
        .then(function() { App.showAlert('Notifications supprim&eacute;es'); ntPage = 1; ntLoad(true); refreshUnread(); },
              function(xhr) { App.showAlert(errText(xhr), 'danger'); });
    });
    root.on('click', '#nt-test', function() {
      App.postJSON('/api/notifications/test', {})
        .then(function() { ntPage = 1; ntLoad(true); refreshUnread(); },
              function(xhr) { App.showAlert(errText(xhr), 'danger'); });
    });
    root.on('click', '.nt-read', function() {
      App.postJSON('/api/notifications/read', { id: $(this).data('id') })
        .then(function() { ntLoad(true); refreshUnread(); },
              function(xhr) { App.showAlert(errText(xhr), 'danger'); });
    });
    root.on('click', '.nt-del', function() {
      App.postJSON('/api/notifications/delete', { id: $(this).data('id') })
        .then(function() { ntLoad(true); refreshUnread(); },
              function(xhr) { App.showAlert(errText(xhr), 'danger'); });
    });
    root.on('click', '.nt-page', function(e) {
      e.preventDefault();
      var pg = toInt($(this).data('page'), 1);
      if (pg !== ntPage) { ntPage = pg; ntLoad(true); }
    });
  }

  function ntRow(n) {
    var t = (n.type >= 0 && n.type <= 3) ? n.type : 0;
    var unread = !n.viewed;
    return '<tr' + (unread ? ' class="table-warning"' : '') + '>' +
      '<td><span class="badge ' + TYPE_CLASSES[t] + '">' + TYPE_LABELS[t] + '</span> ' +
        (unread ? '<strong>' + esc(n.title) + '</strong>' : esc(n.title)) +
        '<div class="small" style="white-space:pre-line">' + esc(n.message) + '</div></td>' +
      '<td class="small text-nowrap">' + esc(n.timeStamp || '') + '</td>' +
      '<td class="small text-nowrap">' + (unread ? '<span class="text-danger fw-bold">Non lu</span>' : '<span class="text-success">Lu</span>') + '</td>' +
      '<td class="text-nowrap">' +
        (unread ? '<button class="btn btn-sm btn-outline-primary nt-read me-1" data-id="' + n.id + '" title="Marquer comme lu">Vu</button>' : '') +
        '<button class="btn btn-sm btn-outline-danger nt-del" data-id="' + n.id + '" title="Supprimer">&times;</button>' +
      '</td></tr>';
  }

  function ntPagination(total) {
    var pages = Math.max(1, Math.ceil(total / ntPerPage));
    if (pages <= 1) return '';
    function item(pg, label, disabled, active) {
      return '<li class="page-item' + (disabled ? ' disabled' : '') + (active ? ' active' : '') + '">' +
             '<a class="page-link nt-page" href="#" data-page="' + pg + '">' + label + '</a></li>';
    }
    var h = item(ntPage - 1, '&laquo;', ntPage <= 1, false);
    var from = Math.max(1, ntPage - 3), to = Math.min(pages, from + 6);
    from = Math.max(1, to - 6);
    for (var p = from; p <= to; p++) h += item(p, String(p), false, p === ntPage);
    h += item(ntPage + 1, '&raquo;', ntPage >= pages, false);
    return h;
  }

  // force : redessine meme si rien n'a change (rafraichissement periodique sinon)
  function ntLoad(force) {
    $.getJSON('/api/notifications', { page: ntPage, limit: ntPerPage }, function(data) {
      var list = (data && data.notifications) || [];
      var total = (data && data.total) || 0;
      var pages = Math.max(1, Math.ceil(total / ntPerPage));
      if (ntPage > pages) { ntPage = pages; ntLoad(true); return; }
      var hash = JSON.stringify(data);
      if (!force && hash === ntHash) return;
      ntHash = hash;
      if (!list.length) {
        $('#nt-list').html('<div class="alert alert-info mb-0">Aucune notification.</div>');
      } else {
        var rows = '';
        for (var i = 0; i < list.length; i++) rows += ntRow(list[i]);
        $('#nt-list').html(
          '<div class="table-responsive"><table class="table table-hover align-middle mb-0">' +
            '<thead><tr><th>Titre</th><th>Date/Heure</th><th>Statut</th><th>Actions</th></tr></thead>' +
            '<tbody>' + rows + '</tbody></table></div>');
      }
      var start = total ? data.offset + 1 : 0;
      var end = Math.min(data.offset + list.length, total);
      $('#nt-info').text(total ? ('Affichage ' + start + '-' + end + ' sur ' + total + ' notifications (' + (data.unread || 0) + ' non lues)') : '');
      $('#nt-pages').html(ntPagination(total));
      $('.AlertNotif').css('display', (data.unread || 0) > 0 ? 'inline-block' : 'none');
      $('#notif-unread-badge').text(String(data.unread || 0)).toggle((data.unread || 0) > 0);
    }).fail(function(xhr) {
      if (force) $('#nt-list').html('<div class="alert alert-danger mb-0">Erreur lors du chargement des notifications : ' + errText(xhr) + '</div>');
    });
  }

  // ==========================================================================
  // PAGE : configuration des alertes (/configNotifications LiXee-Box)
  // ==========================================================================
  function cnCheck(key, label) {
    return '<div class="form-check mb-2"><input class="form-check-input" type="checkbox" id="cn-' + key + '">' +
           '<label class="form-check-label" for="cn-' + key + '">' + label + '</label></div>';
  }
  // Case + champs numeriques sur une ligne (inputs : [{key, unit, ph, label}])
  function cnThreshold(key, label, inputs) {
    var h = '<div class="mb-3"><div class="form-check mb-1"><input class="form-check-input" type="checkbox" id="cn-' + key + '">' +
            '<label class="form-check-label" for="cn-' + key + '">' + label + '</label></div><div class="row g-2">';
    inputs.forEach(function(f) {
      h += '<div class="col-sm-4"><div class="input-group input-group-sm">' +
           (f.label ? '<span class="input-group-text">' + f.label + '</span>' : '') +
           '<input type="text" inputmode="decimal" class="form-control" id="cn-' + f.key + '" placeholder="' + (f.ph || '') + '">' +
           '<span class="input-group-text">' + f.unit + '</span></div></div>';
    });
    return h + '</div></div>';
  }
  function cnTab(id, label, active) {
    return '<li class="nav-item" role="presentation"><button class="nav-link' + (active ? ' active' : '') +
           '" data-bs-toggle="tab" data-bs-target="#cn-tab-' + id + '" type="button" role="tab">' + label + '</button></li>';
  }
  function cnPane(id, body, active) {
    return '<div class="tab-pane fade' + (active ? ' show active' : '') + '" id="cn-tab-' + id + '" role="tabpanel">' +
           '<div class="card shadow-sm border-top-0"><div class="card-body">' + body + '</div></div></div>';
  }

  var CN_FLAGS = ['SubscribedPower', 'PowerOutage', 'RedColor', 'OverVoltage', 'UnderVoltage', 'OverPower',
                  'DailyAnomaly', 'PriceChange', 'PEJP', 'ColorTomorrow', 'OverBudget', 'ProdZero', 'ProdSupConso',
                  'WaterLeak', 'NightWaterLeak', 'Freeze', 'DailyMetrics'];
  var CN_INTS = ['OverVoltageThreshold', 'UnderVoltageThreshold', 'OverPowerThreshold', 'OverPowerDuration',
                 'OverPowerCooldown', 'DailyAnomalyPercent', 'OverBudgetThreshold', 'WaterLeakThreshold',
                 'NightWaterLeakThreshold', 'FreezeCooldown'];

  function pageConfigNotifications() {
    var conso =
      '<h5>Alertes</h5>' +
      cnCheck('SubscribedPower', 'D&eacute;passement de puissance souscrite') +
      cnCheck('PowerOutage', 'Puissance nulle') +
      cnCheck('RedColor', 'Jour rouge (Tempo)') +
      cnThreshold('OverVoltage', 'Sur-tension', [{ key: 'OverVoltageThreshold', unit: 'V', ph: '253' }]) +
      cnThreshold('UnderVoltage', 'Sous-tension', [{ key: 'UnderVoltageThreshold', unit: 'V', ph: '207' }]) +
      cnThreshold('OverPower', 'Surpuissance soutenue', [
        { key: 'OverPowerThreshold', unit: 'W', ph: '6000' },
        { key: 'OverPowerDuration', unit: 's', ph: '60', label: 'Dur&eacute;e' },
        { key: 'OverPowerCooldown', unit: 'min', ph: '30', label: 'Intervalle' }]) +
      cnThreshold('DailyAnomaly', 'Consommation journali&egrave;re anormale (contr&ocirc;le &agrave; 23h50)',
        [{ key: 'DailyAnomalyPercent', unit: '% au-dessus moy. 7 j', ph: '30' }]) +
      '<h5 class="mt-3">Infos</h5>' +
      cnCheck('PriceChange', 'Changement de tarif') +
      cnCheck('PEJP', 'Pr&eacute;avis EJP') +
      cnCheck('ColorTomorrow', 'Couleur du lendemain (Tempo)') +
      cnThreshold('OverBudget', 'D&eacute;passement de budget (contr&ocirc;l&eacute; chaque nuit sur la veille)',
        [{ key: 'OverBudgetThreshold', unit: '&euro; / mois', ph: '50' }]);
    var prod =
      '<h5>Alertes</h5>' + cnCheck('ProdZero', 'Production nulle sur 24 h') +
      '<h5 class="mt-3">Infos</h5>' + cnCheck('ProdSupConso', 'Production &gt; Consommation');
    var eau =
      '<h5>Fuite d\'eau (journali&egrave;re)</h5>' +
      cnThreshold('WaterLeak', 'Consommation de 00h &agrave; 06h (contr&ocirc;le &agrave; 06h)', [{ key: 'WaterLeakThreshold', unit: 'L', ph: '5' }]) +
      '<h5 class="mt-3">Fuite d\'eau (nocturne)</h5>' +
      cnThreshold('NightWaterLeak', 'Consommation entre 00h et 05h59', [{ key: 'NightWaterLeakThreshold', unit: 'L', ph: '1' }]);
    var gaz = '<p class="text-muted mb-0">Aucune notification configur&eacute;e pour le gaz.</p>';
    var autres =
      '<h5>Alerte gel</h5>' +
      cnCheck('Freeze', 'Temp&eacute;rature inf&eacute;rieure au seuil') +
      '<div class="row g-2 mb-3">' +
        '<div class="col-sm-6"><select class="form-select form-select-sm" id="cn-FreezeSensorIEEE"><option value="">&mdash; Capteur de temp&eacute;rature &mdash;</option></select></div>' +
        '<div class="col-sm-3"><div class="input-group input-group-sm"><input type="text" inputmode="decimal" class="form-control" id="cn-FreezeThresholdC" placeholder="3"><span class="input-group-text">&deg;C</span></div></div>' +
        '<div class="col-sm-3"><div class="input-group input-group-sm"><span class="input-group-text">Intervalle</span><input type="text" inputmode="numeric" class="form-control" id="cn-FreezeCooldown" placeholder="60"><span class="input-group-text">min</span></div></div>' +
      '</div>' +
      '<h5>M&eacute;triques quotidiennes</h5>' +
      cnCheck('DailyMetrics', 'Envoyer un r&eacute;sum&eacute; quotidien (23h50)');

    App.setContent(
      '<div class="container py-4">' +
        '<h4 class="mb-3">Config Notifications</h4>' +
        '<p class="text-muted small">Les alertes sont enregistr&eacute;es dans le centre de notifications.</p>' +
        '<form id="cn-form">' +
          '<ul class="nav nav-tabs" role="tablist">' +
            cnTab('conso', 'Consommation', true) + cnTab('prod', 'Production') + cnTab('eau', 'Eau') +
            cnTab('gaz', 'Gaz') + cnTab('autres', 'Autres') +
          '</ul>' +
          '<div class="tab-content">' +
            cnPane('conso', conso, true) + cnPane('prod', prod) + cnPane('eau', eau) +
            cnPane('gaz', gaz) + cnPane('autres', autres) +
          '</div>' +
          '<div class="d-flex justify-content-end mt-3"><button type="submit" class="btn btn-warning btn-lg">Enregistrer</button></div>' +
        '</form>' +
      '</div>'
    );

    // Le capteur n'est envoye qu'une fois la liste chargee : sinon un
    // enregistrement rapide effacerait le capteur deja configure
    var sensorReady = false, loaded = false;
    $.getJSON('/api/config/notifications', function(cfg) {
      loaded = true;
      CN_FLAGS.forEach(function(k) { $('#cn-' + k).prop('checked', !!cfg[k]); });
      CN_INTS.forEach(function(k) { if (cfg[k] != null) $('#cn-' + k).val(cfg[k]); });
      if (cfg.FreezeThreshold != null) $('#cn-FreezeThresholdC').val(cfg.FreezeThreshold / 100);
      var cur = cfg.FreezeSensorIEEE || '';
      $.getJSON('/api/zigbee/devices', function(devs) {
        var sel = $('#cn-FreezeSensorIEEE');
        var found = false;
        (devs || []).forEach(function(d) {
          var name = d.alias || d.model || d.ieee;
          sel.append($('<option>').val(d.ieee).text(name + ' (' + d.ieee + ')'));
          if (String(d.ieee).toUpperCase() === cur.toUpperCase()) found = true;
        });
        if (cur && !found) sel.append($('<option>').val(cur).text(cur + ' (absent)'));
        sel.val(cur);
        if (sel.val() !== cur) sel.find('option').filter(function() { return this.value.toUpperCase() === cur.toUpperCase(); }).prop('selected', true);
        sensorReady = true;
      }).fail(function() {
        if (cur) $('#cn-FreezeSensorIEEE').append($('<option>').val(cur).text(cur)).val(cur);
        sensorReady = true;
      });
    }).fail(function(xhr) { App.showAlert(errText(xhr), 'danger'); });

    $('#cn-form').on('submit', function(e) {
      e.preventDefault();
      // Sans la configuration actuelle, tout enregistrer desactiverait les alertes
      if (!loaded) { App.showAlert('Configuration non charg&eacute;e : rechargez la page', 'warning'); return; }
      var o = {};
      CN_FLAGS.forEach(function(k) { o[k] = $('#cn-' + k).is(':checked'); });
      CN_INTS.forEach(function(k) {
        var v = $.trim($('#cn-' + k).val());
        if (v !== '') o[k] = Math.round(num(v, 0));
      });
      var fc = $.trim($('#cn-FreezeThresholdC').val());
      if (fc !== '') o.FreezeThreshold = Math.round(num(fc, 3) * 100);
      if (sensorReady) o.FreezeSensorIEEE = $('#cn-FreezeSensorIEEE').val() || '';
      if (sensorReady && o.Freeze && !o.FreezeSensorIEEE) { App.showAlert('Choisissez le capteur de temp&eacute;rature de l\'alerte gel', 'warning'); return; }
      App.postJSON('/api/config/notifications', o)
        .then(function() { App.showAlert('Configuration des notifications enregistr&eacute;e'); },
              function(xhr) { App.showAlert(errText(xhr), 'danger'); });
    });
  }

  // ==========================================================================
  // PAGE : email (/configNotifMail LiXee-Box)
  // ==========================================================================
  function pageConfigNotifMail() {
    App.setContent(
      '<div class="container py-4">' +
        '<h4 class="mb-3">Config Email</h4>' +
        '<div class="card shadow-sm mb-3"><div class="card-body">' +
          '<form id="cm-form" autocomplete="off">' +
            '<div class="form-check form-switch mb-3"><input class="form-check-input" type="checkbox" id="cm-enable">' +
              '<label class="form-check-label" for="cm-enable">Envoyer les notifications par email</label></div>' +
            '<div class="row g-2">' +
              '<div class="col-md-6 mb-2"><label class="form-label" for="cm-serv">Serveur SMTP</label><input class="form-control" id="cm-serv" placeholder="smtp.exemple.fr"></div>' +
              '<div class="col-md-2 mb-2"><label class="form-label" for="cm-port">Port</label><input class="form-control" id="cm-port" inputmode="numeric" placeholder="587"></div>' +
              '<div class="col-md-4 mb-2"><label class="form-label" for="cm-sec">S&eacute;curit&eacute;</label><select class="form-select" id="cm-sec">' +
                '<option value="0">Automatique (465 : SSL/TLS, sinon STARTTLS)</option>' +
                '<option value="1">SSL/TLS</option><option value="2">STARTTLS</option>' +
                '<option value="3">Aucune (d&eacute;conseill&eacute;)</option></select></div>' +
              '<div class="col-md-6 mb-2"><label class="form-label" for="cm-user">Identifiant</label><input class="form-control" id="cm-user" autocomplete="off"></div>' +
              '<div class="col-md-6 mb-2"><label class="form-label" for="cm-pass">Mot de passe</label><input type="password" class="form-control" id="cm-pass" autocomplete="new-password">' +
                '<div class="form-check mt-1"><input class="form-check-input" type="checkbox" id="cm-clearpass"><label class="form-check-label small" for="cm-clearpass">Effacer le mot de passe enregistr&eacute;</label></div></div>' +
              '<div class="col-md-6 mb-2"><label class="form-label" for="cm-from">Exp&eacute;diteur</label><input class="form-control" id="cm-from" placeholder="(identifiant si vide)"></div>' +
              '<div class="col-md-6 mb-2"><label class="form-label" for="cm-to">Destinataire</label><input class="form-control" id="cm-to" placeholder="(identifiant si vide)"></div>' +
              '<div class="col-md-6 mb-2"><label class="form-label" for="cm-min">Notifications envoy&eacute;es</label><select class="form-select" id="cm-min">' +
                '<option value="0">Toutes</option><option value="1">Attention, erreurs et critiques</option>' +
                '<option value="2">Erreurs et critiques</option><option value="3">Critiques seulement</option></select></div>' +
            '</div>' +
            '<div class="form-text mb-3">30 emails par heure au maximum. Le certificat du serveur est v&eacute;rifi&eacute;.</div>' +
            '<div class="d-flex flex-wrap justify-content-end gap-2">' +
              '<button type="button" class="btn btn-outline-secondary" id="cm-test">Envoyer un email de test</button>' +
              '<button type="submit" class="btn btn-warning">Enregistrer</button>' +
            '</div>' +
          '</form>' +
        '</div></div>' +
        '<div class="card shadow-sm"><div class="card-body"><h5>&Eacute;tat des envois</h5><div id="cm-status"></div></div></div>' +
      '</div>'
    );

    $.getJSON('/api/config/notifmail', function(c) {
      $('#cm-enable').prop('checked', !!c.enableNotif);
      $('#cm-serv').val(c.servSMTP || '');
      $('#cm-port').val(c.portSMTP || '');
      $('#cm-sec').val(String(c.securitySMTP || 0));
      $('#cm-user').val(c.userSMTP || '');
      if (c.hasPassSMTP) $('#cm-pass').attr('placeholder', '******** (inchang\u00E9 si vide)');
      $('#cm-from').val(c.fromSMTP || '');
      $('#cm-to').val(c.toSMTP || '');
      $('#cm-min').val(String(c.minTypeSMTP || 0));
    }).fail(function(xhr) { App.showAlert(errText(xhr), 'danger'); });
    loadStatus('mail', '#cm-status');

    function save() {
      return App.postJSON('/api/config/notifmail', {
        enableNotif: $('#cm-enable').is(':checked'),
        servSMTP: $.trim($('#cm-serv').val()),
        portSMTP: $.trim($('#cm-port').val()),
        securitySMTP: toInt($('#cm-sec').val(), 0),
        userSMTP: $.trim($('#cm-user').val()),
        passSMTP: $('#cm-pass').val(),          // vide = conserver l'actuel
        clearPassSMTP: $('#cm-clearpass').is(':checked'),
        fromSMTP: $.trim($('#cm-from').val()),
        toSMTP: $.trim($('#cm-to').val()),
        minTypeSMTP: toInt($('#cm-min').val(), 0)
      });
    }

    $('#cm-form').on('submit', function(e) {
      e.preventDefault();
      save().then(function() {
        $('#cm-pass').val('');
        $('#cm-clearpass').prop('checked', false);
        App.showAlert('Configuration email enregistr&eacute;e');
      }, function(xhr) { App.showAlert(errText(xhr), 'danger'); });
    });

    // Le test enregistre d'abord la saisie, puis suit l'etat jusqu'au resultat
    $('#cm-test').on('click', function() {
      var btn = $(this).prop('disabled', true);
      save().then(function() {
        loadStatus('mail', '#cm-status', function(before) {
          App.postJSON('/api/config/notifmail/test', {}).then(function() {
            App.showAlert('Envoi de l\'email de test en cours...', 'info');
            watchStatus('mail', '#cm-status', before);
          }, function(xhr) { App.showAlert(errText(xhr), 'danger'); })
            .always(function() { btn.prop('disabled', false); });
        });
      }, function(xhr) { btn.prop('disabled', false); App.showAlert(errText(xhr), 'danger'); });
    });
  }

  // ==========================================================================
  // PAGE : WebPush (/configWebPush LiXee-Box), remplace celle d'app.js
  // ==========================================================================
  function pageWebpush() {
    App.setContent(
      '<div class="container py-4">' +
        '<h4 class="mb-3">Config WebPush</h4>' +
        '<div class="card shadow-sm mb-3"><div class="card-body">' +
          '<form id="wp-form" autocomplete="off">' +
            '<div class="form-check form-switch mb-3"><input class="form-check-input" type="checkbox" id="wp-enable">' +
              '<label class="form-check-label" for="wp-enable">Activer WebPush</label></div>' +
            '<div class="mb-3"><label class="form-label" for="wp-serv">Serveur HTTP</label>' +
              '<input class="form-control" id="wp-serv" placeholder="http://serveur:port/api"></div>' +
            '<div class="form-check form-switch mb-3"><input class="form-check-input" type="checkbox" id="wp-auth">' +
              '<label class="form-check-label" for="wp-auth">Activer l\'authentification</label></div>' +
            '<div id="wp-auth-block" class="row g-2" style="display:none">' +
              '<div class="col-md-6 mb-2"><label class="form-label" for="wp-user">Identifiant</label><input class="form-control" id="wp-user"></div>' +
              '<div class="col-md-6 mb-2"><label class="form-label" for="wp-pass">Mot de passe</label><input type="password" class="form-control" id="wp-pass" autocomplete="new-password"></div>' +
            '</div>' +
            '<div class="d-flex flex-wrap justify-content-end gap-2 mt-2">' +
              '<button type="button" class="btn btn-outline-secondary" id="wp-test">Envoyer un test</button>' +
              '<button type="submit" class="btn btn-warning">Enregistrer</button>' +
            '</div>' +
          '</form>' +
        '</div></div>' +
        '<div class="card shadow-sm mb-3"><div class="card-body"><h5>&Eacute;tat des envois</h5><div id="wp-status"></div></div></div>' +
        '<div class="card shadow-sm"><div class="card-body"><h5>Exemple de donn&eacute;es HTTP</h5>' +
          '<p class="mb-1">Requ&ecirc;te POST, Content-Type : application/json, envoy&eacute;e &agrave; chaque valeur re&ccedil;ue d\'un appareil :</p>' +
          '<pre class="mb-0"><code>{\n  "IEEE" : "00158D0001234567",\n  "cluster" : "2820",\n  "attribute" : "1295",\n  "value" : "1250"\n}</code></pre>' +
          '<p class="small text-muted mt-2 mb-0">Cluster et attribut en d&eacute;cimal ; valeur num&eacute;rique en d&eacute;cimal ou texte. ' +
          'Apr&egrave;s un &eacute;chec, les envois sont suspendus 30 s.</p>' +
        '</div></div>' +
      '</div>'
    );

    $('#wp-auth').on('change', function() { $('#wp-auth-block').toggle(this.checked); });
    $.getJSON('/api/config/webpush', function(c) {
      $('#wp-enable').prop('checked', !!c.enableWebPush);
      $('#wp-serv').val(c.servWebPush || '');
      $('#wp-auth').prop('checked', !!c.webPushAuth);
      $('#wp-auth-block').toggle(!!c.webPushAuth);
      $('#wp-user').val(c.userWebPush || '');
      if (c.hasPassWebPush) $('#wp-pass').attr('placeholder', '******** (inchang\u00E9 si vide)');
    }).fail(function(xhr) { App.showAlert(errText(xhr), 'danger'); });
    loadStatus('webpush', '#wp-status');

    function save() {
      var url = $.trim($('#wp-serv').val());
      if ($('#wp-enable').is(':checked') && !/^https?:\/\//i.test(url)) {
        return $.Deferred().reject({ responseText: 'URL du serveur invalide (http:// ou https://)' }).promise();
      }
      return App.postJSON('/api/config/webpush', {
        enableWebPush: $('#wp-enable').is(':checked'),
        servWebPush: url,
        webPushAuth: $('#wp-auth').is(':checked'),
        userWebPush: $('#wp-user').val(),
        passWebPush: $('#wp-pass').val()       // vide = conserver l'actuel
      });
    }

    $('#wp-form').on('submit', function(e) {
      e.preventDefault();
      save().then(function() { $('#wp-pass').val(''); App.showAlert('Configuration WebPush enregistr&eacute;e'); },
                  function(xhr) { App.showAlert(errText(xhr), 'danger'); });
    });
    $('#wp-test').on('click', function() {
      var btn = $(this).prop('disabled', true);
      save().then(function() {
        loadStatus('webpush', '#wp-status', function(before) {
          App.postJSON('/api/notifications/test-webpush', {}).then(function() {
            watchStatus('webpush', '#wp-status', before);
          }, function(xhr) { App.showAlert(errText(xhr), 'danger'); })
            .always(function() { btn.prop('disabled', false); });
        });
      }, function(xhr) { btn.prop('disabled', false); App.showAlert(errText(xhr), 'danger'); });
    });
  }

  App.registerPage('notifications', pageNotifications);
  App.registerPage('configNotifications', pageConfigNotifications);
  // Email : non propose sur la LiXee-Box (la route reste "non disponible" d'app.js)
  void pageConfigNotifMail;
  App.registerPage('webpush', pageWebpush);
})();
