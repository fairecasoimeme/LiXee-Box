// ============================================================================
// Page Tunnel (#configTunnel) - acces distant via remote.lixee-box.fr
// Portage de handleConfigTunnel / HTTP_CONFIG_TUNNEL de la LiXee-Box.
// API : /api/tunnel/* (voir components/webserver/web_tunnel.cpp).
//
//  - un SEUL interrupteur commande le tunnel (carte "Etat du tunnel") ;
//  - activation par code a 6 chiffres (POST /api/tunnel/activate, puis
//    sondage de /api/tunnel/activate/status chaque seconde) ;
//  - connexion manuelle : enregistre Client ID / Token sans changer l'etat
//    marche/arret (credsOnly) ;
//  - via le tunnel, l'interrupteur est verrouille : couper le tunnel depuis le
//    tunnel ferait perdre l'acces (le serveur le refuse aussi).
// ============================================================================
(function() {
  'use strict';
  var A = window.LixeeApp;
  if (!A) return;
  var esc = A.escHtml;

  var BADGES = {
    connected:  ['bg-success', 'Connect\u00e9'],
    connecting: ['bg-info text-dark', 'Connexion\u2026'],
    waiting:    ['bg-warning text-dark', 'D\u00e9connect\u00e9'],
    suspended:  ['bg-danger', 'Suspendu'],
    disabled:   ['bg-secondary', 'D\u00e9sactiv\u00e9']
  };

  var last = null;        // dernier etat recu
  var saving = false;     // enregistrement de l'interrupteur en cours
  var gen = 0;            // incremente a chaque affichage de la page

  function fmtDuration(s) {
    s = Number(s) || 0;
    if (s < 60) return s + ' s';
    if (s < 3600) return Math.floor(s / 60) + ' min';
    if (s < 86400) return Math.floor(s / 3600) + ' h ' + Math.floor((s % 3600) / 60) + ' min';
    return Math.floor(s / 86400) + ' j ' + Math.floor((s % 86400) / 3600) + ' h';
  }

  // Vrai tant que la page affichee est celle-ci (reponses tardives ignorees sinon)
  function onPage() {
    return (window.location.hash.replace('#', '').split('?')[0]) === 'configTunnel';
  }

  function digitInput() {
    return '<input class="form-control text-center tun-digit" type="text" inputmode="numeric" maxlength="1" ' +
           'pattern="[0-9]" autocomplete="off" style="width:42px;height:48px;font-size:1.4em;font-weight:bold;padding:0">';
  }

  function pageConfigTunnel() {
    var myGen = ++gen;
    last = null;
    saving = false;
    var digits = '';
    for (var i = 0; i < 6; i++) digits += digitInput();

    A.setContent(
      '<div class="container py-4">' +
        '<h4 class="mb-4">Tunnel (acc&egrave;s distant)</h4>' +
        '<div id="tun-sec-warning" class="alert alert-warning mb-3" style="display:none;">' +
          '<strong>S&eacute;curit&eacute; requise</strong> &mdash; L\'acc&egrave;s s&eacute;curis&eacute; doit &ecirc;tre activ&eacute;, ' +
          'avec un identifiant et un mot de passe, pour utiliser le tunnel. Tant qu\'il ne l\'est pas, le tunnel reste ' +
          'suspendu, m&ecirc;me s\'il avait &eacute;t&eacute; activ&eacute; auparavant. ' +
          '<a href="#configHTTP" class="alert-link">Configurer l\'acc&egrave;s s&eacute;curis&eacute;</a>' +
        '</div>' +

        '<div class="card shadow-sm mb-3"><div class="card-body">' +
          '<h5 class="card-title">&Eacute;tat du tunnel</h5>' +
          '<div class="d-flex flex-wrap align-items-center gap-2 mb-2">' +
            '<span id="tun-badge" class="badge bg-secondary">&hellip;</span>' +
            '<small id="tun-remote-warning" class="text-warning" style="display:none;">' +
              'Le tunnel ne peut &ecirc;tre d&eacute;sactiv&eacute; qu\'&agrave; partir de l\'adresse IP locale.</small>' +
          '</div>' +
          '<div id="tun-url" class="mb-2"></div>' +
          '<div id="tun-detail" class="small text-muted mb-3"></div>' +
          '<div class="form-check form-switch">' +
            '<input class="form-check-input" type="checkbox" id="tun-enable" disabled>' +
            '<label class="form-check-label" for="tun-enable">Tunnel actif</label>' +
          '</div>' +
          '<div class="form-text">Interrupteur unique : il commande le tunnel quelle que soit la m&eacute;thode de ' +
            'configuration utilis&eacute;e ci-dessous (code ou manuelle).</div>' +
        '</div></div>' +

        '<div class="card shadow-sm mb-3"><div class="card-body">' +
          '<h5 class="card-title">Activation par code</h5>' +
          '<div class="d-flex flex-wrap justify-content-center align-items-center gap-2 mb-3">' +
            '<div class="d-flex gap-1" id="tun-digits">' + digits + '</div>' +
            '<button class="btn btn-warning" type="button" id="tun-activate" style="height:48px" disabled>Activer</button>' +
          '</div>' +
          '<div id="tun-act-status" style="display:none;"></div>' +
        '</div></div>' +

        '<div class="card shadow-sm mb-3"><div class="card-body">' +
          '<details>' +
            '<summary class="h5" style="cursor:pointer">Connexion manuelle (avanc&eacute;)</summary>' +
            '<form id="tun-creds-form" class="mt-3" autocomplete="off">' +
              '<div class="mb-3"><label class="form-label" for="tun-client-id">Client ID</label>' +
                '<input class="form-control" type="text" id="tun-client-id" maxlength="63"></div>' +
              '<div class="mb-3"><label class="form-label" for="tun-token">Token</label>' +
                '<input class="form-control" type="password" id="tun-token" maxlength="127" autocomplete="new-password"></div>' +
              '<div class="form-text mb-3">Enregistre les identifiants sans changer l\'&eacute;tat du tunnel. ' +
                'Utilisez l\'interrupteur <b>&Eacute;tat du tunnel</b> en haut de page pour l\'activer ou le ' +
                'd&eacute;sactiver. Laisser le Token vide pour conserver l\'actuel.</div>' +
              '<button type="submit" class="btn btn-warning" id="tun-save-creds" disabled>Enregistrer</button>' +
            '</form>' +
          '</details>' +
        '</div></div>' +

        '<div class="card shadow-sm"><div class="card-body">' +
          '<h5>Comment activer le tunnel ?</h5>' +
          '<ol class="mb-0">' +
            '<li>Rendez-vous sur <a href="https://remote.lixee-box.fr" target="_blank" rel="noopener">remote.lixee-box.fr</a> ' +
              'et cr&eacute;ez un compte.</li>' +
            '<li>Ajoutez un nouveau dispositif depuis votre tableau de bord.</li>' +
            '<li>Cliquez sur &laquo; G&eacute;n&eacute;rer un code d\'activation &raquo; pour obtenir un code &agrave; 6 chiffres.</li>' +
            '<li>Entrez ce code ci-dessus et cliquez sur &laquo; Activer &raquo;.</li>' +
            '<li>Votre passerelle sera alors accessible &agrave; distance sans ouvrir de ports sur votre box Internet.</li>' +
          '</ol>' +
        '</div></div>' +
      '</div>'
    );

    bindDigits();
    $('#tun-enable').on('change', onToggle);
    $('#tun-activate').on('click', function() { onActivate(myGen); });
    $('#tun-creds-form').on('submit', onSaveCreds);

    refresh(true);
    A.pageInterval(function() { refresh(false); }, 5000);
  }

  // --------------------------------------------------------------------------
  // Etat
  // --------------------------------------------------------------------------

  function refresh(first) {
    var myGen = gen;
    $.getJSON('/api/tunnel/status').done(function(d) {
      if (myGen !== gen || !onPage()) return;   // page quittee ou reaffichee entre-temps
      render(d, first);
    });
  }

  function render(d, first) {
    last = d;
    $('#tun-sec-warning').toggle(!d.securityOk);

    var b = BADGES[d.state] || BADGES.disabled;
    $('#tun-badge').attr('class', 'badge ' + b[0]).text(b[1]);

    // URL publique : affichee seulement si elle a la forme attendue
    var $url = $('#tun-url').empty();
    if (d.connected && typeof d.url === 'string' && /^https:\/\/[A-Za-z0-9.:-]+$/.test(d.url)) {
      $url.append($('<small class="text-muted">').text('URL : '))
          .append($('<a target="_blank" rel="noopener">').attr('href', d.url).text(d.url));
    }

    var detail = [];
    if (d.state === 'connected') {
      detail.push('Connect\u00e9 depuis ' + fmtDuration(d.connectedFor));
      if (d.deviceId) detail.push('Device ID : ' + d.deviceId);
      detail.push((d.requests || 0) + ' requ\u00eate(s) relay\u00e9e(s)');
      if (d.queued || d.inFlight) detail.push('en cours : ' + (d.inFlight || 0) + ', en attente : ' + (d.queued || 0));
    } else if (d.state === 'waiting' || d.state === 'connecting') {
      if (d.lastError) detail.push('Derni\u00e8re erreur : ' + d.lastError);
      if (d.state === 'waiting' && d.retryIn) detail.push('nouvel essai dans ' + d.retryIn + ' s');
    } else if (d.state === 'suspended') {
      detail.push('Suspendu tant que l\'acc\u00e8s s\u00e9curis\u00e9 n\'est pas configur\u00e9.');
    } else if (d.enabled && !d.hasToken) {
      detail.push('Aucun Token : utilisez l\'activation par code.');
    }
    $('#tun-detail').text(detail.join(' \u2014 '));

    // Interrupteur (verrouille via le tunnel une fois actif, ou sans securite)
    var lockedRemote = !!(d.viaTunnel && d.enabled);
    if (!saving) $('#tun-enable').prop('checked', !!d.enabled);
    $('#tun-enable').prop('disabled', saving || !d.securityOk || lockedRemote);
    $('#tun-remote-warning').toggle(lockedRemote);

    $('#tun-activate').prop('disabled', !d.securityOk || $('#tun-activate').data('busy') === 1);
    $('#tun-save-creds').prop('disabled', !d.securityOk);

    if (first) {
      $('#tun-client-id').val(d.clientId || '');
      $('#tun-token').val('').attr('placeholder', d.hasToken ? '********' : '');
    }
  }

  function onToggle() {
    var want = $(this).is(':checked');
    saving = true;
    $('#tun-enable').prop('disabled', true);
    A.postJSON('/api/tunnel/config', { enableTunnel: want })
      .done(function() {
        A.showAlert(want ? 'Tunnel activ\u00e9' : 'Tunnel d\u00e9sactiv\u00e9');
      })
      .fail(function(xhr) {
        $('#tun-enable').prop('checked', !want);
        A.showAlert(esc(A.apiErrorText(xhr)), 'danger');
      })
      .always(function() {
        saving = false;
        refresh(false);
      });
  }

  // --------------------------------------------------------------------------
  // Activation par code
  // --------------------------------------------------------------------------

  function bindDigits() {
    var $d = $('.tun-digit');
    $d.each(function(i) {
      var inp = this;
      $(inp).on('input', function() {
        inp.value = inp.value.replace(/[^0-9]/g, '').slice(0, 1);
        if (inp.value.length === 1 && i < 5) $d.get(i + 1).focus();
      });
      $(inp).on('keydown', function(e) {
        if (e.key === 'Backspace' && inp.value === '' && i > 0) {
          var prev = $d.get(i - 1);
          prev.value = '';
          prev.focus();
        } else if (e.key === 'Enter') {
          $('#tun-activate').trigger('click');
        }
      });
      $(inp).on('paste', function(e) {
        e.preventDefault();
        var cd = (e.originalEvent && e.originalEvent.clipboardData) || window.clipboardData;
        var txt = (cd ? cd.getData('text') : '').replace(/[^0-9]/g, '');
        for (var j = 0; j < 6 && j < txt.length; j++) $d.get(j).value = txt[j];
        var k = Math.min(txt.length, 6) - 1;
        if (k >= 0) $d.get(k).focus();
      });
    });
  }

  function getCode() {
    var c = '';
    $('.tun-digit').each(function() { c += this.value; });
    return c;
  }

  // msgHtml : texte deja echappe (ou balisage local)
  function showStatus(msgHtml, type) {
    $('#tun-act-status').attr('class', 'alert alert-' + type + ' py-2').html(msgHtml).show();
  }

  function setActivateBusy(busy) {
    $('#tun-activate').data('busy', busy ? 1 : 0)
      .prop('disabled', busy || !(last && last.securityOk));
  }

  function onActivate(myGen) {
    var code = getCode();
    if (!/^\d{6}$/.test(code)) {
      showStatus('Veuillez entrer un code &agrave; 6 chiffres', 'danger');
      return;
    }
    setActivateBusy(true);
    showStatus('<span class="spinner-border spinner-border-sm"></span> Activation en cours&hellip;', 'warning');
    A.postJSON('/api/tunnel/activate', { code: code })
      .done(function(d) {
        if (myGen !== gen) return;
        if (d && (d.status === 'pending' || d.status === 'processing')) {
          pollActivation(myGen);
        } else {
          showStatus(esc((d && d.error) || 'Erreur'), 'danger');
          setActivateBusy(false);
        }
      })
      .fail(function(xhr) {
        if (myGen !== gen) return;
        showStatus(esc(A.apiErrorText(xhr) || 'Erreur de connexion'), 'danger');
        setActivateBusy(false);
      });
  }

  function pollActivation(myGen) {
    var fails = 0;
    // L'activation redemarre le tunnel : via le tunnel, quelques sondages
    // echouent normalement (15 echecs toleres, comme la LiXee-Box).
    var iv = A.pageInterval(function() {
      $.getJSON('/api/tunnel/activate/status')
        .done(function(d) {
          fails = 0;
          if (myGen !== gen || !d || d.status !== 'done') return;
          clearInterval(iv);
          setActivateBusy(false);
          if (d.success) {
            showStatus('Tunnel activ&eacute; avec succ&egrave;s !', 'success');
            setTimeout(function() { if (myGen === gen && onPage()) pageConfigTunnel(); }, 2000);
          } else {
            showStatus(esc(d.error || 'Erreur inconnue'), 'danger');
          }
        })
        .fail(function() {
          if (++fails >= 15) {
            clearInterval(iv);
            if (myGen !== gen) return;
            showStatus('Erreur de connexion', 'danger');
            setActivateBusy(false);
          }
        });
    }, 1000);
  }

  // --------------------------------------------------------------------------
  // Connexion manuelle
  // --------------------------------------------------------------------------

  function onSaveCreds(e) {
    e.preventDefault();
    var id = $.trim($('#tun-client-id').val());
    var tok = $('#tun-token').val();
    if (/\s/.test(id) || /\s/.test(tok)) {
      A.showAlert('Le Client ID et le Token ne doivent pas contenir d\'espace', 'danger');
      return;
    }
    var payload = { credsOnly: true, tunnelClientId: id };
    if (tok) payload.tunnelToken = tok;
    $('#tun-save-creds').prop('disabled', true);
    A.postJSON('/api/tunnel/config', payload)
      .done(function() {
        A.showAlert('Identifiants enregistr\u00e9s');
        $('#tun-token').val('').attr('placeholder', (tok || (last && last.hasToken)) ? '********' : '');
      })
      .fail(function(xhr) {
        A.showAlert(esc(A.apiErrorText(xhr)), 'danger');
      })
      .always(function() {
        $('#tun-save-creds').prop('disabled', !(last && last.securityOk));
        refresh(false);
      });
  }

  A.registerPage('configTunnel', pageConfigTunnel);
})();
