// ============================================================================
// LiXee MatterBridge - Lot 5 : regles et groupes d'actions
//   #configRules           liste des regles (activation, etat, derniere date),
//                          editeur JSON de rules.json (ex-page /rules)
//   #addRule, #editRule?name=   editeur de regle (js/rules_editor.js, charge a
//                          la demande)
//   #configActionGroups    groupes d'actions (HTTP_ACTION_GROUPS_JS de la Box)
// Barre de boutons des groupes, reutilisable par la page Appareils :
//   window.LixeeActionGroups.renderBar(selecteurOuElement)
// API : /api/rules/*, /api/actiongroups/* (components/webserver/web_rules.cpp)
// ============================================================================
(function() {
  'use strict';
  var A = window.LixeeApp;
  if (!A) return;
  var esc = A.escHtml;

  var LIXEE_BLUE = '#0F70B7';
  var SPINNER = '<div class="text-center p-3"><div class="spinner-border" role="status"></div></div>';

  var ICON_OK = '<svg xmlns="http://www.w3.org/2000/svg" width="28" height="28" fill="#1bc600" viewBox="0 0 16 16"><path fill-rule="evenodd" d="M2 15.5V2a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v13.5a.5.5 0 0 1-.74.439L8 13.069l-5.26 2.87A.5.5 0 0 1 2 15.5m8.854-9.646a.5.5 0 0 0-.708-.708L7.5 7.793 6.354 6.646a.5.5 0 1 0-.708.708l1.5 1.5a.5.5 0 0 0 .708 0z"/></svg>';
  var ICON_KO = '<svg xmlns="http://www.w3.org/2000/svg" width="28" height="28" fill="#c60000" viewBox="0 0 16 16"><path fill-rule="evenodd" d="M2 15.5V2a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v13.5a.5.5 0 0 1-.74.439L8 13.069l-5.26 2.87A.5.5 0 0 1 2 15.5M6.854 5.146a.5.5 0 1 0-.708.708L7.293 7 6.146 8.146a.5.5 0 1 0 .708.708L8 7.707l1.146 1.147a.5.5 0 1 0 .708-.708L8.707 7l1.147-1.146a.5.5 0 0 0-.708-.708L8 6.293z"/></svg>';
  var ICON_WAIT = '<svg xmlns="http://www.w3.org/2000/svg" width="28" height="28" fill="#e67e22" viewBox="0 0 16 16"><path d="M2 1.5a.5.5 0 0 1 .5-.5h11a.5.5 0 0 1 0 1h-1v1a4.5 4.5 0 0 1-2.557 4.06c-.29.139-.443.377-.443.59v.7c0 .213.154.451.443.59A4.5 4.5 0 0 1 12.5 13v1h1a.5.5 0 0 1 0 1h-11a.5.5 0 1 1 0-1h1v-1a4.5 4.5 0 0 1 2.557-4.06c.29-.139.443-.377.443-.59v-.7c0-.213-.154-.451-.443-.59A4.5 4.5 0 0 1 3.5 3V2h-1a.5.5 0 0 1-.5-.5"/></svg>';
  var ICON_EDIT = '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path d="M15.502 1.94a.5.5 0 0 1 0 .706L14.459 3.69l-2-2L13.502.646a.5.5 0 0 1 .707 0l1.293 1.293zm-1.75 2.456-2-2L4.939 9.21a.5.5 0 0 0-.121.196l-.805 2.414a.25.25 0 0 0 .316.316l2.414-.805a.5.5 0 0 0 .196-.12l6.813-6.814z"/><path fill-rule="evenodd" d="M1 13.5A1.5 1.5 0 0 0 2.5 15h11a1.5 1.5 0 0 0 1.5-1.5v-6a.5.5 0 0 0-1 0v6a.5.5 0 0 1-.5.5h-11a.5.5 0 0 1-.5-.5v-11a.5.5 0 0 1 .5-.5H9a.5.5 0 0 0 0-1H2.5A1.5 1.5 0 0 0 1 2.5z"/></svg>';
  var ICON_DEL = '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path d="M14 1a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H2a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1zM2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2z"/><path d="M4.646 4.646a.5.5 0 0 1 .708 0L8 7.293l2.646-2.647a.5.5 0 0 1 .708.708L8.707 8l2.647 2.646a.5.5 0 0 1-.708.708L8 8.707l-2.646 2.647a.5.5 0 0 1-.708-.708L7.293 8 4.646 5.354a.5.5 0 0 1 0-.708"/></svg>';
  var ICON_PLUS = '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path d="M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16"/><path d="M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4"/></svg>';
  var ICON_CODE = '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path d="M5.854 4.854a.5.5 0 1 0-.708-.708l-3.5 3.5a.5.5 0 0 0 0 .708l3.5 3.5a.5.5 0 0 0 .708-.708L2.707 8l3.147-3.146zm4.292 0a.5.5 0 0 1 .708-.708l3.5 3.5a.5.5 0 0 1 0 .708l-3.5 3.5a.5.5 0 0 1-.708-.708L13.293 8l-3.147-3.146z"/></svg>';

  // Styles des groupes d'actions (page de configuration et barre de boutons)
  function ensureStyle() {
    if (document.getElementById('lixee-ag-style')) return;
    var css =
      '.ag-card{background:#fff;border-radius:12px;box-shadow:0 2px 8px rgba(0,0,0,.08);padding:16px;margin-bottom:14px}' +
      '.ag-row{display:flex;align-items:center;gap:10px;flex-wrap:wrap}' +
      '.ag-chip{color:#fff;padding:8px 14px;border-radius:8px;font-weight:600;display:inline-flex;align-items:center;gap:8px}' +
      '.ag-act{background:#f8f9fa;border:1px solid #e9ecef;border-radius:8px;padding:10px;margin-bottom:8px}' +
      '.ag-muted{color:#6c757d;font-size:13px}' +
      '.ag-badge{display:inline-flex;align-items:center;justify-content:center;width:26px;height:26px;border-radius:7px;background:#fff;flex-shrink:0}' +
      '.ag-badge svg{width:18px;height:18px;fill:' + LIXEE_BLUE + '}' +
      '.ag-ico{font-size:16px;line-height:1}' +
      '.ag-btn{border:none;color:#fff;padding:10px 18px;border-radius:8px;font-size:14px;font-weight:600;cursor:pointer;display:inline-flex;align-items:center;gap:8px;box-shadow:0 2px 6px rgba(0,0,0,.12);transition:transform .15s,filter .15s}' +
      '.ag-btn:hover{filter:brightness(1.08);transform:translateY(-1px)}' +
      '.ag-btn:disabled{opacity:.6;cursor:default;transform:none}' +
      '.ag-btn.ok{background:#198754!important}.ag-btn.ko{background:#dc3545!important}' +
      '.ag-namegrp{display:flex;gap:6px;align-items:stretch}' +
      '.ag-iconbtn{width:44px;min-width:44px;border:1px solid #ced4da;border-radius:6px;background:#fff;display:inline-flex;align-items:center;justify-content:center;cursor:pointer;padding:0;font-size:22px;color:#adb5bd}' +
      '.ag-iconbtn:hover{border-color:' + LIXEE_BLUE + ';background:#eef5fb}.ag-iconbtn.empty{border-style:dashed}' +
      '.ag-iconbtn svg{width:24px;height:24px;fill:' + LIXEE_BLUE + '}' +
      '.agm-back{position:fixed;inset:0;background:rgba(0,0,0,.45);display:flex;align-items:center;justify-content:center;z-index:2000;padding:16px}' +
      '.agm{background:#fff;border-radius:12px;width:100%;max-width:560px;max-height:85vh;display:flex;flex-direction:column;box-shadow:0 10px 30px rgba(0,0,0,.25)}' +
      '.agm-head{display:flex;align-items:center;justify-content:space-between;padding:14px 18px;border-bottom:1px solid #e9ecef;font-weight:600}' +
      '.agm-body{overflow:auto;padding:4px 18px 18px}' +
      '.agm-cat{font-size:11px;color:#6c757d;text-transform:uppercase;letter-spacing:.04em;margin:14px 0 6px}' +
      '.agm-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(48px,1fr));gap:8px}' +
      '.agm-ic{height:48px;border:1px solid #dee2e6;background:#fff;border-radius:10px;cursor:pointer;display:flex;align-items:center;justify-content:center;padding:0;font-size:20px;color:#adb5bd}' +
      '.agm-ic:hover{background:#eef5fb;border-color:' + LIXEE_BLUE + '}.agm-ic.sel{border:2px solid ' + LIXEE_BLUE + ';background:#e7f1fa}' +
      '.agm-ic svg{width:26px;height:26px;fill:' + LIXEE_BLUE + '}' +
      '#rules-raw{width:100%;min-height:420px;font-family:Consolas,Monaco,monospace;font-size:13px;line-height:1.5;tab-size:2;background:#1e1e1e;color:#d4d4d4;border:none;padding:12px;border-radius:6px}';
    var st = document.createElement('style');
    st.id = 'lixee-ag-style';
    st.appendChild(document.createTextNode(css));
    document.head.appendChild(st);
  }

  // Catalogue d'icones (js/agicons.js) charge a la demande ; sans lui, les noms
  // d'icones s'affichent en texte.
  function loadIcons() {
    return A.loadScripts(['js/agicons.js']).catch(function() {});
  }

  function agSvg(name) {
    var ic = (typeof AG_ICONS !== 'undefined' && name) ? AG_ICONS[name] : null;
    return ic ? '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="' + esc(ic[2]) + '"/></svg>' : '';
  }

  // Icone d'un groupe : trace SVG du catalogue, ou emoji d'un groupe ancien
  function agIconHtml(icon) {
    if (!icon) return '';
    var s = agSvg(icon);
    return s ? '<span class="ag-badge">' + s + '</span>' : '<span class="ag-ico">' + esc(icon) + '</span>';
  }

  // La couleur finit dans un attribut style : seule une couleur #hex passe
  function safeColor(c) {
    return /^#[0-9a-fA-F]{3,8}$/.test(String(c || '')) ? c : '#0d6efd';
  }

  // ==========================================================================
  // Liste des regles (handleConfigRules)
  // ==========================================================================

  function statusIcon(st) {
    if (st === 1) return '<span title="Vraie">' + ICON_OK + '</span>';
    if (st === 2) return '<span title="En attente du maintien">' + ICON_WAIT + '</span>';
    return '<span title="Fausse">' + ICON_KO + '</span>';
  }

  function triggerText(r) {
    return r.trigger && r.trigger.mode === 'event' ? 'Événement' : 'Timer 60 s';
  }

  function renderRules(d) {
    var rules = (d && d.rules) || [];
    var warn = d && d.fileError
      ? '<div class="alert alert-danger">rules.json est illisible : aucune règle n\'est active. Corrigez-le dans l\'éditeur JSON.</div>' : '';
    if (!rules.length) {
      $('#rules-list').html(warn + '<div class="text-center p-4" style="font-size:24px;font-weight:bold;">Pas de règles</div>');
      return;
    }
    var h = warn + '<div class="table-responsive"><table class="table table-striped table-hover align-middle"><thead><tr>' +
      '<th>Nom</th><th style="width:70px">Actif</th><th style="width:60px">État</th><th style="width:160px">Dernière date</th>' +
      '<th style="width:110px">Déclenchement</th><th style="width:110px">Actions</th></tr></thead><tbody>';
    rules.forEach(function(r) {
      var n = esc(r.name);
      h += '<tr data-name="' + n + '">' +
        '<td><span class="rule-name' + (r.enabled ? '' : ' text-muted') + '">' + n + '</span></td>' +
        '<td><div class="form-check form-switch"><input class="form-check-input rule-toggle" type="checkbox"' + (r.enabled ? ' checked' : '') + '></div></td>' +
        '<td class="rule-status">' + statusIcon(r.status) + '</td>' +
        '<td class="rule-date">' + esc(r.lastDate || '') + '</td>' +
        '<td class="small">' + triggerText(r) + '</td>' +
        '<td><a class="btn btn-sm btn-warning me-1" title="Modifier" href="#editRule?name=' + encodeURIComponent(r.name) + '">' + ICON_EDIT + '</a>' +
        '<button type="button" class="btn btn-sm btn-danger rule-delete" title="Supprimer">' + ICON_DEL + '</button></td></tr>';
    });
    $('#rules-list').html(h + '</tbody></table></div>');
  }

  function loadRules() {
    $.getJSON('/api/rules/list').done(renderRules).fail(function(xhr) {
      $('#rules-list').html('<div class="alert alert-danger">' + A.apiErrorMsg(xhr, 'Chargement impossible') + '</div>');
    });
  }

  // Rafraichissement des etats et dates (getRuleStatus de la Box), sans redessiner
  function refreshStatus() {
    if (!$('#rules-list table').length) return;
    $.ajax({ url: '/api/rules/list', dataType: 'json', global: false }).done(function(d) {
      ((d && d.rules) || []).forEach(function(r) {
        var tr = $('#rules-list tr').filter(function() { return $(this).attr('data-name') === r.name; });
        tr.find('.rule-status').html(statusIcon(r.status));
        tr.find('.rule-date').text(r.lastDate || '');
      });
    });
  }

  function rawEditorHtml() {
    return '<div class="card mx-auto shadow-sm mt-4" id="rules-raw-card" style="display:none"><div class="card-body">' +
      '<div class="d-flex justify-content-between align-items-center mb-2 flex-wrap gap-2">' +
        '<h5 class="mb-0">Éditeur JSON (rules.json)</h5>' +
        '<div><button type="button" class="btn btn-sm btn-outline-secondary me-1" id="rules-raw-format">Formater</button>' +
        '<button type="button" class="btn btn-sm btn-outline-secondary me-1" id="rules-raw-reload">Recharger</button>' +
        '<button type="button" class="btn btn-sm btn-warning" id="rules-raw-save">Enregistrer</button></div></div>' +
      '<textarea id="rules-raw" spellcheck="false"></textarea>' +
      '<div class="small mt-2" id="rules-raw-state"></div>' +
      '<div class="form-text">Chaque règle est vérifiée par le pont avant l\'enregistrement ; le fichier est remplacé tel quel.</div>' +
      '</div></div>';
  }

  function validateRaw() {
    var t = $('#rules-raw').val();
    try {
      var j = JSON.parse(t);
      if (!j || !Array.isArray(j.rules)) throw new Error('clé "rules" absente ou pas un tableau');
      $('#rules-raw-state').attr('class', 'small mt-2 text-success').text('JSON valide : ' + j.rules.length + ' règle(s)');
      return j;
    } catch (e) {
      $('#rules-raw-state').attr('class', 'small mt-2 text-danger').text('JSON invalide : ' + e.message);
      return null;
    }
  }

  function loadRaw() {
    $('#rules-raw').val('');
    $.ajax({ url: '/api/rules/raw', dataType: 'text' }).done(function(t) {
      $('#rules-raw').val(t);
      validateRaw();
    }).fail(function(xhr) { A.showAlert('Lecture de rules.json impossible : ' + A.apiErrorMsg(xhr), 'danger'); });
  }

  function pageConfigRules() {
    ensureStyle();
    A.setContent(
      '<div class="container p-4" id="rules-page">' +
        '<div class="d-flex justify-content-between align-items-center mb-4 flex-wrap gap-2">' +
          '<h4 class="card-title mb-0">Config règles</h4>' +
          '<div><button type="button" class="btn btn-outline-secondary me-2" id="rules-raw-toggle">' + ICON_CODE + ' Éditeur JSON</button>' +
          '<a href="#addRule" class="btn btn-primary">' + ICON_PLUS + ' Ajouter une règle</a></div>' +
        '</div>' +
        '<div class="card mx-auto shadow-sm"><div class="card-body"><div id="rules-list">' + SPINNER + '</div></div></div>' +
        rawEditorHtml() +
      '</div>');
    var page = $('#rules-page');

    page.on('change', '.rule-toggle', function() {
      var cb = this, tr = $(cb).closest('tr'), name = tr.attr('data-name');
      A.postJSON('/api/rules/toggle', { name: name, enabled: cb.checked }).done(function() {
        tr.find('.rule-name').toggleClass('text-muted', !cb.checked);
      }).fail(function(xhr) {
        cb.checked = !cb.checked;
        A.showAlert('Erreur : ' + A.apiErrorMsg(xhr), 'danger');
      });
    });
    page.on('click', '.rule-delete', function() {
      var name = $(this).closest('tr').attr('data-name');
      if (!confirm('Voulez-vous vraiment supprimer la règle "' + name + '" ?')) return;
      A.postJSON('/api/rules/delete', { name: name }).done(function() {
        A.showAlert('Règle supprimée');
        loadRules();
      }).fail(function(xhr) { A.showAlert('Erreur lors de la suppression : ' + A.apiErrorMsg(xhr), 'danger'); });
    });

    page.on('click', '#rules-raw-toggle', function() {
      var card = $('#rules-raw-card');
      card.toggle();
      if (card.is(':visible')) loadRaw();
    });
    page.on('input', '#rules-raw', validateRaw);
    page.on('click', '#rules-raw-reload', loadRaw);
    page.on('click', '#rules-raw-format', function() {
      var j = validateRaw();
      if (j) $('#rules-raw').val(JSON.stringify(j, null, 2));
    });
    page.on('click', '#rules-raw-save', function() {
      if (!validateRaw()) { A.showAlert('JSON invalide : correction nécessaire avant l\'enregistrement', 'danger'); return; }
      $.ajax({ url: '/api/rules/raw', method: 'POST', contentType: 'application/json', data: $('#rules-raw').val() })
        .done(function() { A.showAlert('rules.json enregistré'); loadRules(); })
        .fail(function(xhr) { A.showAlert('Erreur : ' + A.apiErrorMsg(xhr), 'danger'); });
    });

    loadRules();
    A.pageInterval(refreshStatus, 15000);
  }

  // ==========================================================================
  // Editeur de regle (rules_editor.js charge a la demande)
  // ==========================================================================

  function openEditor(mode) {
    A.setContent(SPINNER);
    A.loadScripts(['js/rules_editor.js']).then(function() {
      window.RulesEditor.open(mode, A.params().name || '');
    }).catch(function() {
      A.setContent('<div class="container p-4"><div class="alert alert-danger">Chargement de l\'éditeur impossible.</div></div>');
    });
  }

  // ==========================================================================
  // Groupes d'actions (handleConfigActionGroups + HTTP_ACTION_GROUPS_JS)
  // ==========================================================================

  var G = { groups: [], max: 8, maxActions: 10, edit: -1, cur: null, devs: null };

  function groupsLoad() {
    $.getJSON('/api/actiongroups/list').done(function(d) {
      G.groups = (d && d.groups) || [];
      G.max = (d && d.max) || 8;
      G.maxActions = (d && d.maxActions) || 10;
      groupsRender();
    }).fail(function(xhr) {
      $('#ag-list').html('<div class="alert alert-danger">' + A.apiErrorMsg(xhr, 'Chargement impossible') + '</div>');
    });
  }

  function groupsRender() {
    var h = '';
    if (!G.groups.length) h = '<p class="ag-muted">Aucun groupe pour l\'instant.</p>';
    G.groups.forEach(function(g, i) {
      h += '<div class="ag-card"><div class="ag-row">' +
        '<span class="ag-chip" style="background:' + safeColor(g.color) + '">' + agIconHtml(g.icon) + esc(g.name) + '</span>' +
        '<span class="ag-muted">' + (g.actions || []).length + ' action(s)' + (g.enabled === false ? ' &middot; masqué' : '') + '</span>' +
        (g.matter ? '<span class="badge bg-info text-dark" title="Interrupteur Matter' + (g.matterOff ? ' marche/arrêt' : ' (impulsion)') + '">Matter' +
          (g.matterEp ? ' #' + g.matterEp : '') + '</span>' : '') +
        '<span style="flex:1"></span>' +
        '<button type="button" class="btn btn-sm btn-secondary ag-test" data-i="' + i + '">Tester</button> ' +
        '<button type="button" class="btn btn-sm btn-primary ag-edit" data-i="' + i + '">Modifier</button> ' +
        '<button type="button" class="btn btn-sm btn-danger ag-del" data-i="' + i + '">Supprimer</button>' +
        '</div></div>';
    });
    $('#ag-list').html(h);
    $('#ag-new').prop('disabled', G.groups.length >= G.max);
  }

  // Catalogue des appareils : charge a la premiere ouverture de l'editeur
  function ensureDevs(cb) {
    if (G.devs) { cb(); return; }
    $.getJSON('/api/actiongroups/devices').done(function(d) { G.devs = d || []; cb(); })
      .fail(function() { A.showAlert('Chargement des appareils impossible', 'danger'); });
  }

  function devById(i) {
    for (var k = 0; k < G.devs.length; k++) if (G.devs[k].i === i) return G.devs[k];
    return null;
  }

  function showEditor(g) {
    G.cur = g;
    var h = '<div class="ag-row mb-3">' +
      '<div><label class="ag-muted">Nom</label><div class="ag-namegrp">' +
        '<button type="button" id="ag-iconbtn" class="ag-iconbtn"></button>' +
        '<input id="ag-name" class="form-control" maxlength="47" value="' + esc(g.name) + '"></div></div>' +
      '<div><label class="ag-muted">Couleur</label><br><input id="ag-color" type="color" class="form-control form-control-color" style="width:70px" value="' + esc(safeColor(g.color)) + '"></div>' +
      '<div class="form-check form-switch mt-4"><input class="form-check-input" type="checkbox" id="ag-enabled"' + (g.enabled !== false ? ' checked' : '') +
        '><label class="form-check-label" for="ag-enabled">Bouton affiché</label></div>' +
      '</div>' +
      // Interrupteur Matter (prise On/Off virtuelle) : impulsion, ou marche/arret
      // avec un groupe d'extinction
      '<div class="ag-row mb-3">' +
        '<div class="form-check form-switch"><input class="form-check-input" type="checkbox" id="ag-matter"' + (g.matter ? ' checked' : '') +
          '><label class="form-check-label" for="ag-matter">Créer un interrupteur Matter</label></div>' +
        '<div id="ag-matter-opts"' + (g.matter ? '' : ' style="display:none"') + '><label class="ag-muted">À l\'extinction, lancer</label> ' +
          '<select id="ag-matter-off" class="form-select form-select-sm" style="display:inline-block;width:auto">' +
          '<option value="">rien (impulsion : retour automatique sur éteint)</option>' +
          G.groups.filter(function(o) { return o.name !== g.name; }).map(function(o) {
            return '<option value="' + esc(o.name) + '"' + (o.name === g.matterOff ? ' selected' : '') + '>' + esc(o.name) + '</option>';
          }).join('') +
          '</select></div>' +
      '</div>' +
      '<div class="ag-muted small mb-2">L\'interrupteur apparaît dans Home Assistant, Apple Maison... comme une prise. ' +
        'Il se retire ou se remet depuis la page Matter.</div>' +
      '<div id="ag-acts"></div>' +
      '<button type="button" class="btn btn-sm btn-secondary" id="ag-add-act">Ajouter une action</button> ' +
      '<button type="button" class="btn btn-success" id="ag-save">Enregistrer</button> ' +
      '<button type="button" class="btn btn-outline-secondary" id="ag-cancel">Annuler</button>';
    $('#ag-editor').html(h).show();
    updateIconBtn();
    renderActs();
  }

  function updateIconBtn() {
    var b = $('#ag-iconbtn');
    var s = agSvg(G.cur.icon);
    if (s) {
      b.html(s).removeClass('empty').attr('title', AG_ICONS[G.cur.icon][1] + ' (changer)');
    } else if (G.cur.icon) {
      b.html(esc(G.cur.icon)).removeClass('empty').attr('title', 'Changer l’icône');
    } else {
      b.html('+').addClass('empty').attr('title', 'Choisir une icône');
    }
    b.attr('aria-label', b.attr('title'));
  }

  function renderActs() {
    var h = '';
    G.cur.actions.forEach(function(a, j) {
      // Seules les actions "Appareil" se configurent ici (comme sur la LiXee-Box) ;
      // les autres types d'un fichier edite a la main sont conserves tels quels.
      if (a.type && a.type !== 'device') {
        h += '<div class="ag-act"><div class="ag-row"><span class="ag-muted">Action de type ' + esc(a.type) +
          ' (conservée telle quelle)</span><span style="flex:1"></span>' +
          '<button type="button" class="btn btn-sm btn-danger ag-rm" data-j="' + j + '">Retirer</button></div></div>';
        return;
      }
      h += '<div class="ag-act"><div class="ag-row">' +
        '<select class="form-select ag-dev" data-j="' + j + '" style="width:230px"><option value="">-- appareil --</option>';
      var known = false;
      G.devs.forEach(function(d) {
        if (d.i === a.IEEE) known = true;
        h += '<option value="' + esc(d.i) + '"' + (d.i === a.IEEE ? ' selected' : '') + '>' + esc(d.n) + '</option>';
      });
      if (a.IEEE && !known) h += '<option value="' + esc(a.IEEE) + '" selected>' + esc(a.IEEE) + ' (absent)</option>';
      h += '</select><select class="form-select ag-name" data-j="' + j + '" style="width:230px"><option value="">-- action --</option>';
      var dd = devById(a.IEEE), knownA = false;
      (dd ? dd.a : []).forEach(function(x) {
        if (x.n === a.actionName) knownA = true;
        h += '<option value="' + esc(x.n) + '"' + (x.n === a.actionName ? ' selected' : '') + '>' + esc(x.n) + '</option>';
      });
      if (a.actionName && !knownA) h += '<option value="' + esc(a.actionName) + '" selected>' + esc(a.actionName) + '</option>';
      h += '</select><button type="button" class="btn btn-sm btn-danger ag-rm" data-j="' + j + '">Retirer</button></div></div>';
    });
    if (!G.cur.actions.length) h = '<p class="ag-muted">Aucune action. Cliquez sur « Ajouter une action ».</p>';
    $('#ag-acts').html(h);
  }

  function openIconPicker() {
    if (typeof AG_ICONS === 'undefined') { A.showAlert('Catalogue d’icônes indisponible', 'danger'); return; }
    var byTheme = {};
    Object.keys(AG_ICONS).forEach(function(n) { var t = AG_ICONS[n][0]; (byTheme[t] = byTheme[t] || []).push(n); });
    var h = '<div class="agm" role="dialog" aria-modal="true" aria-label="Choisir une icône">' +
      '<div class="agm-head"><span>Choisir une icône</span><button type="button" class="btn-close agm-close" aria-label="Fermer"></button></div>' +
      '<div class="agm-body">';
    AG_THEMES.forEach(function(label, t) {
      if (!byTheme[t]) return;
      h += '<div class="agm-cat">' + esc(label) + '</div><div class="agm-grid">';
      byTheme[t].forEach(function(n) {
        var l = esc(AG_ICONS[n][1]);
        h += '<button type="button" class="agm-ic' + (n === G.cur.icon ? ' sel' : '') + '" data-n="' + esc(n) + '" title="' + l + '" aria-label="' + l + '">' + agSvg(n) + '</button>';
      });
      h += '</div>';
    });
    h += '<div class="agm-cat">Aucune</div><div class="agm-grid"><button type="button" class="agm-ic' + (G.cur.icon ? '' : ' sel') +
      '" data-n="" title="Aucune icône" aria-label="Aucune icône">∅</button></div></div></div>';
    $('#ag-icon-modal').html(h).show();
    $(document).on('keydown.agicons', function(e) { if (e.key === 'Escape') closeIconPicker(); });
    $('#ag-icon-modal .agm-ic.sel').trigger('focus');
  }

  function closeIconPicker() {
    $('#ag-icon-modal').hide().empty();
    $(document).off('keydown.agicons');
    $('#ag-iconbtn').trigger('focus');
  }

  function saveGroup() {
    var g = G.cur;
    g.name = $.trim($('#ag-name').val());
    g.color = $('#ag-color').val();
    g.enabled = $('#ag-enabled').is(':checked');
    g.matter = $('#ag-matter').is(':checked');
    g.matterOff = g.matter ? ($('#ag-matter-off').val() || '') : '';
    if (!g.name) { A.showAlert('Le nom est obligatoire', 'danger'); return; }
    if (g.actions.some(function(a) { return (!a.type || a.type === 'device') && (!a.IEEE || !a.actionName); })) {
      A.showAlert('Chaque action doit avoir un appareil et une action', 'danger');
      return;
    }
    var body = JSON.parse(JSON.stringify(g));
    if (G.edit >= 0) body.index = G.edit;
    A.postJSON('/api/actiongroups/save', body).done(function() {
      $('#ag-editor').hide().empty();
      groupsLoad();
    }).fail(function(xhr) {
      var m = (xhr.responseJSON && (xhr.responseJSON.error || xhr.responseJSON.message)) || '';
      A.showAlert('Échec : ' + esc(m || 'enregistrement impossible'), 'danger');
    });
  }

  function pageConfigActionGroups() {
    ensureStyle();
    G.devs = null;
    A.setContent(
      '<div class="container py-4" id="ag-page">' +
        '<h4 class="mb-1">Groupes d\'actions</h4>' +
        '<p class="ag-muted">Un groupe rassemble plusieurs actions, sur des appareils différents, derrière un seul bouton ' +
          'affiché en haut de la page <b>Appareils</b>. Une règle peut aussi déclencher un groupe.</p>' +
        '<div id="ag-list">' + SPINNER + '</div>' +
        '<button type="button" class="btn btn-primary" id="ag-new">Ajouter un groupe</button>' +
        '<div id="ag-editor" style="display:none;" class="ag-card mt-3"></div>' +
        '<div id="ag-icon-modal" class="agm-back" style="display:none"></div>' +
      '</div>');
    var page = $('#ag-page');

    page.on('click', '#ag-new', function() {
      G.edit = -1;
      ensureDevs(function() { showEditor({ name: '', icon: '', color: '#0d6efd', enabled: true, actions: [] }); });
    });
    page.on('click', '.ag-edit', function() {
      var i = parseInt($(this).attr('data-i'), 10);
      G.edit = i;
      ensureDevs(function() { showEditor(JSON.parse(JSON.stringify(G.groups[i]))); });
    });
    page.on('click', '.ag-del', function() {
      if (!confirm('Supprimer ce groupe ?')) return;
      A.postJSON('/api/actiongroups/delete', { index: parseInt($(this).attr('data-i'), 10) })
        .done(groupsLoad).fail(function(xhr) { A.showAlert('Erreur : ' + A.apiErrorMsg(xhr), 'danger'); });
    });
    page.on('click', '.ag-test', function() {
      A.postJSON('/api/actiongroups/run', { index: parseInt($(this).attr('data-i'), 10) }).done(function(d) {
        A.showAlert((d && d.sent !== undefined ? d.sent : 0) + ' action(s) envoyée(s)');
      }).fail(function(xhr) { A.showAlert('Échec : ' + A.apiErrorMsg(xhr), 'danger'); });
    });

    page.on('click', '#ag-iconbtn', openIconPicker);
    page.on('change', '#ag-matter', function() { $('#ag-matter-opts').toggle(this.checked); });
    page.on('click', '#ag-icon-modal', function(e) { if (e.target === this) closeIconPicker(); });
    page.on('click', '.agm-close', closeIconPicker);
    page.on('click', '.agm-ic', function() {
      G.cur.icon = $(this).attr('data-n') || '';
      updateIconBtn();
      closeIconPicker();
    });
    page.on('click', '#ag-add-act', function() {
      if (G.cur.actions.length >= G.maxActions) { A.showAlert(G.maxActions + ' actions maximum par groupe', 'danger'); return; }
      G.cur.actions.push({ type: 'device', IEEE: '', actionName: '', endpoint: 1, command: -1, value: '' });
      renderActs();
    });
    page.on('click', '.ag-rm', function() {
      G.cur.actions.splice(parseInt($(this).attr('data-j'), 10), 1);
      renderActs();
    });
    page.on('change', '.ag-dev', function() {
      var a = G.cur.actions[parseInt($(this).attr('data-j'), 10)];
      a.IEEE = $(this).val();
      a.actionName = '';
      var d = devById(a.IEEE);
      if (d && d.a.length) a.endpoint = d.a[0].e;
      renderActs();
    });
    page.on('change', '.ag-name', function() {
      var a = G.cur.actions[parseInt($(this).attr('data-j'), 10)];
      a.actionName = $(this).val();
      var d = devById(a.IEEE);
      if (d) d.a.forEach(function(x) { if (x.n === a.actionName) a.endpoint = x.e; });
    });
    page.on('click', '#ag-save', saveGroup);
    page.on('click', '#ag-cancel', function() { $('#ag-editor').hide().empty(); });

    loadIcons().then(groupsLoad);
  }

  // ==========================================================================
  // Barre de boutons des groupes (page Appareils)
  // ==========================================================================

  function runFromBar(btn) {
    if (btn.disabled) return;
    var idx = parseInt(btn.getAttribute('data-idx'), 10);
    var lbl = btn.querySelector('.ag-lbl');
    var old = lbl.textContent;
    function done(ok, txt) {
      btn.classList.add(ok ? 'ok' : 'ko');
      lbl.textContent = txt;
      setTimeout(function() { btn.classList.remove('ok', 'ko'); lbl.textContent = old; btn.disabled = false; }, 1800);
    }
    btn.disabled = true;
    lbl.textContent = '...';
    A.postJSON('/api/actiongroups/run', { index: idx })
      .done(function(d) { done(true, '✓ ' + ((d && d.sent) || 0)); })
      .fail(function() { done(false, '✗'); });
  }

  /**
   * Affiche les boutons des groupes actives dans target (selecteur ou element).
   * Rien n'est affiche s'il n'existe aucun groupe. Renvoie une promesse.
   */
  function renderBar(target) {
    ensureStyle();
    var el = $(target);
    if (!el.length) return $.Deferred().resolve().promise();
    return loadIcons().then(function() {
      return $.ajax({ url: '/api/actiongroups/list', dataType: 'json', global: false });
    }).then(function(d) {
      var groups = ((d && d.groups) || []).filter(function(g) { return g.enabled !== false; });
      if (!groups.length) { el.empty(); return; }
      var h = '<div class="d-flex flex-wrap align-items-center gap-2 mb-4">';
      groups.forEach(function(g) {
        h += '<button type="button" class="ag-btn" data-idx="' + (parseInt(g.index, 10) || 0) + '" style="background:' + safeColor(g.color) + ';">' +
          agIconHtml(g.icon) + '<span class="ag-lbl">' + esc(g.name) + '</span></button>';
      });
      el.html(h + '</div>');
      el.off('click.agbar').on('click.agbar', '.ag-btn', function() { runFromBar(this); });
    }, function() { el.empty(); });
  }

  window.LixeeActionGroups = { renderBar: renderBar };

  A.registerPage('configRules', pageConfigRules);
  A.registerPage('addRule', function() { openEditor('add'); });
  A.registerPage('editRule', function() { openEditor('edit'); });
  A.registerPage('configActionGroups', pageConfigActionGroups);
})();
