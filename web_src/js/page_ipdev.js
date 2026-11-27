// ============================================================================
// LiXee-Box - Appareils IP (#ipDevices), menu Reseau (IP_DEVICES.md)
//
// Appareils WiFi/Ethernet pilotes par leur API HTTP locale (Shelly, Tasmota) :
// activation du module, ajout par adresse IP avec detection du modele, liste
// avec etat, releve, modification, suppression. Les mesures et les actions
// apparaissent dans la page Appareils, comme le Zigbee et le LoRa.
// API : /api/ip/* (components/webserver/web_ipdev.cpp)
// ============================================================================
(function() {
  'use strict';
  var A = window.LixeeApp;
  if (!A) return;
  var esc = A.escHtml;

  var models = [];

  function errMsg(xhr, fallback) {
    return A.apiErrorMsg ? A.apiErrorMsg(xhr, fallback) : esc(fallback || 'Erreur');
  }

  function msg(sel, cls, html) {
    $(sel).html(html ? '<div class="alert alert-' + cls + ' py-2 mb-0">' + html + '</div>' : '');
  }

  function fmtAge(s) {
    if (s == null || s < 0) return 'jamais';
    if (s < 60) return 'il y a ' + s + ' s';
    if (s < 3600) return 'il y a ' + Math.round(s / 60) + ' min';
    return 'il y a ' + Math.round(s / 3600) + ' h';
  }

  function modelOptions(sel) {
    return '<option value="">Détection automatique</option>' + models.map(function(m) {
      return '<option value="' + esc(m.key) + '"' + (m.key === sel ? ' selected' : '') + '>' + esc(m.label) + '</option>';
    }).join('');
  }

  // ---------------------------------------------------------------- liste
  function stateBadge(d) {
    if (!d.polls) return '<span class="badge bg-secondary">en attente</span>';
    if (d.online) return '<span class="badge bg-success">en ligne</span>';
    return '<span class="badge bg-danger" title="' + esc(d.lastError || '') + '">injoignable</span>';
  }

  function row(d) {
    return '<tr data-id="' + esc(d.id) + '">' +
      '<td><a href="#device?ieee=' + esc(d.id) + '">' + esc(d.alias || d.label) + '</a>' +
        '<div class="small text-muted">' + esc(d.label) + '</div></td>' +
      '<td>' + esc(d.ip) + (d.port && d.port !== 80 ? ':' + d.port : '') + '</td>' +
      '<td>' + stateBadge(d) + '<div class="small text-muted">' + fmtAge(d.age) +
        (d.lastError && !d.online ? ' · ' + esc(d.lastError) : '') + '</div></td>' +
      '<td class="text-end text-nowrap">' +
        '<button type="button" class="btn btn-sm btn-outline-secondary ipd-poll" title="Relever maintenant">Relever</button> ' +
        '<button type="button" class="btn btn-sm btn-outline-primary ipd-edit">Modifier</button> ' +
        '<button type="button" class="btn btn-sm btn-outline-danger ipd-del">Supprimer</button></td>' +
    '</tr>';
  }

  function editRow(d) {
    return '<tr class="ipd-edit-row" data-id="' + esc(d.id) + '"><td colspan="4"><div class="row g-2 align-items-end">' +
      '<div class="col-md-3"><label class="form-label small mb-0">Adresse IP</label><input class="form-control form-control-sm" name="ip" value="' + esc(d.ip) + '"></div>' +
      '<div class="col-md-1"><label class="form-label small mb-0">Port</label><input class="form-control form-control-sm" name="port" inputmode="numeric" value="' + esc(d.port || 80) + '"></div>' +
      '<div class="col-md-3"><label class="form-label small mb-0">Modèle</label><select class="form-select form-select-sm" name="model">' + modelOptions(d.model) + '</select></div>' +
      '<div class="col-md-2"><label class="form-label small mb-0">Utilisateur</label><input class="form-control form-control-sm" name="user" value="' + esc(d.user || '') + '" autocomplete="off"></div>' +
      '<div class="col-md-2"><label class="form-label small mb-0">Mot de passe</label><input type="password" class="form-control form-control-sm" name="pass" placeholder="' + (d.hasPass ? 'inchangé' : 'aucun') + '" autocomplete="new-password"></div>' +
      '<div class="col-md-1"><label class="form-label small mb-0">Relevé (s)</label><input class="form-control form-control-sm" name="interval" inputmode="numeric" value="' + esc(d.interval || 15) + '"></div>' +
      '<div class="col-12 text-end"><button type="button" class="btn btn-sm btn-secondary ipd-cancel">Annuler</button> ' +
        '<button type="button" class="btn btn-sm btn-primary ipd-save">Enregistrer</button></div>' +
      '<div class="col-12 ipd-edit-msg"></div>' +
    '</div></td></tr>';
  }

  var devices = [];
  var editing = null;

  function renderList() {
    if (editing) return;   // pas de rafraichissement pendant une modification
    if (!devices.length) {
      $('#ipd-list').html('<p class="text-muted mb-0">Aucun appareil IP. Ajoutez-en un ci-dessus.</p>');
      return;
    }
    $('#ipd-list').html('<div class="table-responsive"><table class="table table-sm align-middle mb-0"><thead><tr>' +
      '<th>Appareil</th><th>Adresse</th><th>État</th><th></th></tr></thead><tbody>' +
      devices.map(row).join('') + '</tbody></table></div>');
  }

  function load() {
    return $.getJSON('/api/ip/devices').then(function(d) {
      $('#ipd-enabled').prop('checked', !!d.enabled);
      $('#ipd-body').toggle(!!d.enabled);
      devices = d.devices || [];
      renderList();
    }, function(xhr) { msg('#ipd-msg', 'danger', errMsg(xhr, 'Liste indisponible')); });
  }

  // ---------------------------------------------------------------- ajout
  function addForm() {
    return '<div class="card shadow-sm mb-3"><div class="card-body">' +
      '<h6 class="card-title">Ajouter un appareil</h6>' +
      '<div class="row g-2 align-items-end">' +
        '<div class="col-md-3"><label class="form-label small mb-0">Adresse IP</label><input class="form-control" id="ipd-ip" placeholder="192.168.1.50"></div>' +
        '<div class="col-md-1"><label class="form-label small mb-0">Port</label><input class="form-control" id="ipd-port" inputmode="numeric" value="80"></div>' +
        '<div class="col-md-2"><label class="form-label small mb-0">Utilisateur</label><input class="form-control" id="ipd-user" placeholder="si protégé" autocomplete="off"></div>' +
        '<div class="col-md-2"><label class="form-label small mb-0">Mot de passe</label><input type="password" class="form-control" id="ipd-pass" autocomplete="new-password"></div>' +
        '<div class="col-md-4"><label class="form-label small mb-0">Modèle</label><select class="form-select" id="ipd-model"></select></div>' +
        '<div class="col-md-4"><label class="form-label small mb-0">Nom (facultatif)</label><input class="form-control" id="ipd-alias" placeholder="nom lu sur l\'appareil sinon"></div>' +
        '<div class="col-md-2"><label class="form-label small mb-0">Relevé toutes les (s)</label><input class="form-control" id="ipd-interval" inputmode="numeric" value="15"></div>' +
        '<div class="col-md-6 text-end"><button type="button" class="btn btn-outline-primary" id="ipd-probe">Détecter</button> ' +
          '<button type="button" class="btn btn-primary" id="ipd-add">Ajouter</button></div>' +
      '</div>' +
      '<div class="form-text">Shelly Gen2/Plus protégé : utilisateur <b>admin</b>. Tasmota : utilisateur <b>admin</b> et le mot de passe web. ' +
        'Shelly Gen1 : identifiants de la « restricted login ».</div>' +
      '<div class="mt-2" id="ipd-add-msg"></div>' +
    '</div></div>';
  }

  function formPayload() {
    return {
      ip: $.trim($('#ipd-ip').val()), port: Number($('#ipd-port').val()) || 80,
      user: $.trim($('#ipd-user').val()), pass: $('#ipd-pass').val(),
      model: $('#ipd-model').val(), alias: $.trim($('#ipd-alias').val()),
      interval: Number($('#ipd-interval').val()) || 15
    };
  }

  function bindAdd() {
    $('#ipd-probe').on('click', function() {
      var p = formPayload();
      if (!p.ip) { msg('#ipd-add-msg', 'warning', 'Saisissez l\'adresse IP de l\'appareil.'); return; }
      var $b = $(this).prop('disabled', true);
      msg('#ipd-add-msg', 'info', 'Détection en cours…');
      A.postJSON('/api/ip/probe', { ip: p.ip, port: p.port, user: p.user, pass: p.pass }).then(function(r) {
        if (!r.ok) { msg('#ipd-add-msg', 'warning', esc(r.error || 'Appareil non reconnu')); return; }
        $('#ipd-model').val(r.model);
        if (!$('#ipd-alias').val() && r.name) $('#ipd-alias').val(r.name);
        msg('#ipd-add-msg', r.known ? 'warning' : 'success', '<b>' + esc(r.label) + '</b>' +
          (r.name ? ' « ' + esc(r.name) + ' »' : '') + (r.mac ? ' · MAC ' + esc(r.mac) : '') +
          (r.known ? ' · déjà ajouté' : ' · prêt à être ajouté'));
      }, function(xhr) { msg('#ipd-add-msg', 'danger', errMsg(xhr, 'Détection impossible')); })
        .always(function() { $b.prop('disabled', false); });
    });
    $('#ipd-add').on('click', function() {
      var p = formPayload();
      if (!p.ip) { msg('#ipd-add-msg', 'warning', 'Saisissez l\'adresse IP de l\'appareil.'); return; }
      var $b = $(this).prop('disabled', true);
      msg('#ipd-add-msg', 'info', 'Ajout en cours…');
      A.postJSON('/api/ip/add', p).then(function(r) {
        msg('#ipd-add-msg', 'success', esc(r.message || 'Appareil ajouté') +
          ' · <a href="#device?ieee=' + esc(r.id) + '">voir la fiche</a>');
        $('#ipd-ip, #ipd-pass, #ipd-alias').val('');
        load();
      }, function(xhr) { msg('#ipd-add-msg', 'danger', errMsg(xhr, 'Ajout impossible')); })
        .always(function() { $b.prop('disabled', false); });
    });
  }

  // ---------------------------------------------------------------- actions de la liste
  function bindList() {
    var $l = $('#ipd-list');
    $l.on('click', '.ipd-poll', function() {
      var id = $(this).closest('tr').attr('data-id');
      A.postJSON('/api/ip/poll', { id: id }).then(function() { setTimeout(load, 1500); });
    });
    $l.on('click', '.ipd-del', function() {
      var id = $(this).closest('tr').attr('data-id');
      var d = devices.filter(function(x) { return x.id === id; })[0] || {};
      if (!confirm('Supprimer l\'appareil « ' + (d.alias || d.label || id) + ' » ?\nSa fiche et ses valeurs sont aussi effacées.')) return;
      A.postJSON('/api/ip/delete', { id: id, removeData: true }).then(load, function(xhr) {
        msg('#ipd-msg', 'danger', errMsg(xhr, 'Suppression impossible'));
      });
    });
    $l.on('click', '.ipd-edit', function() {
      var id = $(this).closest('tr').attr('data-id');
      var d = devices.filter(function(x) { return x.id === id; })[0];
      if (!d) return;
      editing = id;
      $(this).closest('tr').replaceWith(editRow(d));
    });
    $l.on('click', '.ipd-cancel', function() { editing = null; renderList(); });
    $l.on('click', '.ipd-save', function() {
      var $r = $(this).closest('tr');
      var p = { id: $r.attr('data-id') };
      $r.find('input,select').each(function() { p[this.name] = $(this).val(); });
      p.port = Number(p.port) || 80;
      p.interval = Number(p.interval) || 15;
      if (p.pass === '') delete p.pass;          // vide : mot de passe inchange
      A.postJSON('/api/ip/update', p).then(function() { editing = null; load(); }, function(xhr) {
        $r.find('.ipd-edit-msg').html('<div class="alert alert-danger py-1 mb-0 small">' + errMsg(xhr, 'Modification impossible') + '</div>');
      });
    });
  }

  // ---------------------------------------------------------------- page
  function page() {
    editing = null;
    A.setContent(
      '<div class="container py-3">' +
        '<h4>Appareils IP</h4>' +
        '<p class="text-muted small">Prises, relais et compteurs WiFi/Ethernet pilotés par leur API HTTP locale (Shelly, Tasmota). ' +
          'Leurs mesures et leurs actions apparaissent dans <a href="#devices">Appareils</a>, les règles et les groupes d\'actions.</p>' +
        '<div class="form-check form-switch mb-3"><input class="form-check-input" type="checkbox" id="ipd-enabled">' +
          '<label class="form-check-label" for="ipd-enabled">Module Appareils IP actif</label></div>' +
        '<div id="ipd-msg" class="mb-2"></div>' +
        '<div id="ipd-body" style="display:none;">' + addForm() +
          '<div class="card shadow-sm"><div class="card-body"><h6 class="card-title">Appareils</h6><div id="ipd-list"></div></div></div>' +
        '</div>' +
      '</div>');
    $('#ipd-enabled').on('change', function() {
      var on = this.checked;
      A.postJSON('/api/ip/enable', { enabled: on }).then(function() {
        msg('#ipd-msg', 'success', on ? 'Module actif.' : 'Module arrêté : les appareils ne sont plus relevés.');
        load();
      }, function(xhr) { msg('#ipd-msg', 'danger', errMsg(xhr, 'Changement impossible')); });
    });
    bindAdd();
    bindList();
    $.getJSON('/api/ip/models').then(function(m) {
      models = m || [];
      $('#ipd-model').html(modelOptions(''));
    });
    load();
    A.pageInterval(load, 10000);
  }

  A.registerPage('ipDevices', page);
})();
