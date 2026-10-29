// ============================================================================
// Pages Sauvegarde (#backup) et Mise a jour (#update) - lot 6 du portage
// LiXee-Box (handleToolBackup, HTTP_UPDATE). API : /api/maint/* (voir
// components/webserver/web_maint.h).
//
// Envoi des fichiers :
//  - reseau local : corps brut (application/octet-stream), progression par
//    l'evenement upload du navigateur. Le serveur traite le flux pendant la
//    reception : il ne repond pas aux autres requetes avant la fin, d'ou
//    l'absence de sondage de l'etat pendant un envoi direct ;
//  - tunnel (lixee-box.fr) ou case cochee : morceaux de 8 Ko encodes en base64
//    (~11 Ko, sous la limite de 15 Ko des trames du tunnel), numerotes ; un
//    morceau en echec est renvoye avec le meme numero.
// Les .tar.gz / .gz sont decompresses par le navigateur avant l'envoi, comme
// sur la LiXee-Box.
// ============================================================================
(function() {
  'use strict';
  var A = window.LixeeApp;
  if (!A) return;
  var esc = A.escHtml;

  var CHUNK = 8192;

  function fmtSize(n) {
    n = Number(n) || 0;
    if (n >= 1048576) return (n / 1048576).toFixed(1) + ' Mo';
    if (n >= 1024) return Math.round(n / 1024) + ' Ko';
    return n + ' o';
  }

  function fmtDate(t) {
    return t > 1000000000 ? new Date(t * 1000).toLocaleString('fr-FR') : '--';
  }

  function isTunnel() {
    return window.location.hostname.indexOf('lixee-box.fr') >= 0;
  }

  function progressHtml(id) {
    return '<div class="progress mt-2" style="height:1.5rem;display:none" id="' + id + '-wrap">' +
             '<div class="progress-bar" role="progressbar" id="' + id + '-bar" style="width:0%">0%</div>' +
           '</div>' +
           '<div class="small text-muted mt-1" id="' + id + '-msg"></div>';
  }

  function setProgress(id, pct, msg, cls) {
    $('#' + id + '-wrap').show();
    if (pct !== null && pct !== undefined && pct >= 0) {
      $('#' + id + '-bar').css('width', pct + '%').text(pct + '%')
        .toggleClass('bg-danger', cls === 'error').toggleClass('bg-success', cls === 'done');
    }
    if (msg !== undefined) {
      $('#' + id + '-msg').text(msg).toggleClass('text-danger', cls === 'error').toggleClass('text-muted', cls !== 'error');
    }
  }

  // --------------------------------------------------------------------------
  // Envoi d'un fichier (restauration .tar ou firmware .bin)
  // --------------------------------------------------------------------------

  function decompressIfGz(file) {
    var n = file.name.toLowerCase();
    if (!/\.(gz|tgz)$/.test(n)) return Promise.resolve(file);
    if (typeof DecompressionStream === 'undefined') {
      return Promise.reject(new Error('Ce navigateur ne sait pas décompresser les .gz : décompressez le fichier avant l\'envoi'));
    }
    var ds = new DecompressionStream('gzip');
    return new Response(file.stream().pipeThrough(ds)).blob().then(function(blob) {
      var tarName = file.name.replace(/\.tgz$/i, '.tar').replace(/\.gz$/i, '');
      if (!/\.(tar|bin)$/i.test(tarName)) tarName += '.tar';
      return new File([blob], tarName);
    });
  }

  function blobToBase64(blob) {
    return new Promise(function(resolve, reject) {
      var r = new FileReader();
      r.onloadend = function() { resolve(String(r.result).split(',')[1] || ''); };
      r.onerror = reject;
      r.readAsDataURL(blob);
    });
  }

  function errText(xhr) {
    return (xhr && xhr.responseJSON && (xhr.responseJSON.message || xhr.responseJSON.error)) ||
           (xhr && xhr.status ? 'Erreur HTTP ' + xhr.status : 'Erreur réseau');
  }

  // Envoi direct : corps brut, progression du navigateur (0-90 %)
  function uploadDirect(file, kind, pid) {
    return new Promise(function(resolve, reject) {
      $.ajax({
        url: kind === 'firmware' ? '/api/maint/firmware' : '/api/maint/restore',
        method: 'POST', data: file, processData: false, contentType: 'application/octet-stream',
        xhr: function() {
          var x = $.ajaxSettings.xhr();
          if (x.upload) {
            x.upload.addEventListener('progress', function(e) {
              if (e.lengthComputable) {
                var p = Math.round(e.loaded / e.total * 90);
                setProgress(pid, p, p < 90 ? 'Envoi et écriture... ' + fmtSize(e.loaded) + ' / ' + fmtSize(e.total)
                                           : 'Vérification...');
              }
            });
          }
          return x;
        }
      }).done(function(r) { resolve(r); })
        .fail(function(xhr) { reject(new Error(errText(xhr))); });
    });
  }

  // Envoi par morceaux numerotes (tunnel)
  function uploadChunked(file, kind, pid) {
    var base = kind === 'firmware' ? '/api/maint/firmware' : '/api/maint/restore';
    var total = file.size;
    var chunks = Math.ceil(total / CHUNK);
    function post(url, data, ctype) {
      return new Promise(function(resolve, reject) {
        $.ajax({ url: url, method: 'POST', data: data, processData: false, contentType: ctype })
          .done(resolve).fail(function(xhr) { reject(xhr); });
      });
    }
    function sendChunk(i, attempt) {
      return blobToBase64(file.slice(i * CHUNK, Math.min((i + 1) * CHUNK, total))).then(function(b64) {
        return post(base + '/chunk?n=' + i, b64, 'text/plain');
      }).catch(function(xhr) {
        // Erreur reseau : renvoyer le meme morceau (le serveur ignore un doublon)
        if (attempt < 3 && (!xhr || !xhr.status || xhr.status >= 500)) return sendChunk(i, attempt + 1);
        throw new Error(errText(xhr));
      });
    }
    setProgress(pid, 0, 'Initialisation...');
    return post(base + '/init', JSON.stringify({ totalSize: total, filename: file.name }), 'application/json')
      .catch(function(xhr) { throw new Error(errText(xhr)); })
      .then(function() {
        var p = Promise.resolve();
        for (var i = 0; i < chunks; i++) {
          (function(i) {
            p = p.then(function() {
              return sendChunk(i, 0).then(function() {
                var pct = Math.round((i + 1) / chunks * 90);
                setProgress(pid, pct, 'Envoi ' + (i + 1) + ' / ' + chunks);
              });
            });
          })(i);
        }
        return p;
      })
      .then(function() {
        setProgress(pid, 95, 'Vérification...');
        return post(base + '/finish', '', 'application/json').catch(function(xhr) { throw new Error(errText(xhr)); });
      })
      .catch(function(e) {
        // Liberer la session pour un nouvel essai
        $.ajax({ url: '/api/maint/abort', method: 'POST' });
        throw e;
      });
  }

  // Attente du redemarrage puis rechargement de la page
  function waitReboot(pid) {
    setProgress(pid, 100, 'Redémarrage en cours...', 'done');
    var tries = 0;
    setTimeout(function poll() {
      $.ajax({ url: '/api/system/info', dataType: 'json', timeout: 3000 })
        .done(function() { window.location.reload(); })
        .fail(function() {
          if (++tries < 40) setTimeout(poll, 3000);
          else setProgress(pid, 100, 'L\'appareil ne répond pas encore : rechargez la page manuellement.', 'error');
        });
    }, 8000);
  }

  // Envoie le fichier choisi dans 'inputSel' : .bin -> firmware, sinon
  // archive tar (restauration ou paquet de mise a jour).
  function startUpload(inputSel, chunkedSel, pid, btnSel) {
    var f = $(inputSel)[0];
    if (!f || !f.files.length) { A.showAlert('Sélectionnez un fichier', 'warning'); return; }
    var file = f.files[0];
    var chunked = isTunnel() || $(chunkedSel).is(':checked');
    $(btnSel).prop('disabled', true);
    setProgress(pid, 0, file.name.match(/\.(gz|tgz)$/i) ? 'Décompression...' : 'Préparation...');
    decompressIfGz(file).then(function(plain) {
      var kind = /\.bin$/i.test(plain.name) ? 'firmware' : 'restore';
      return (chunked ? uploadChunked : uploadDirect)(plain, kind, pid);
    }).then(function(r) {
      setProgress(pid, 100, (r && r.message) || 'Terminé', 'done');
      if (r && r.reboot) waitReboot(pid);
    }).catch(function(e) {
      setProgress(pid, null, e.message || String(e), 'error');
      $(btnSel).prop('disabled', false);
    });
  }

  function uploadCard(prefix, title, help, accept) {
    return '<div class="card p-4 mb-4">' +
        '<h5>' + title + '</h5>' +
        '<p class="text-muted">' + help + '</p>' +
        '<div class="mb-2"><input class="form-control" type="file" id="' + prefix + '-file" accept="' + accept + '"></div>' +
        '<div class="form-check mb-3"' + (isTunnel() ? ' style="display:none"' : '') + '>' +
          '<input class="form-check-input" type="checkbox" id="' + prefix + '-chunked">' +
          '<label class="form-check-label small" for="' + prefix + '-chunked">Envoi par morceaux (connexion lente ou acc&egrave;s distant)</label>' +
        '</div>' +
        '<button class="btn btn-warning" id="' + prefix + '-btn">Envoyer</button>' +
        progressHtml(prefix + '-p') +
      '</div>';
  }

  // ==========================================================================
  // PAGE : Sauvegarde
  // ==========================================================================

  function pageBackup() {
    A.setContent(
      '<div class="container py-4" style="max-width:860px">' +
        '<h4 class="mb-4">Sauvegarde</h4>' +
        '<div class="card p-4 mb-4">' +
          '<h5>Cr&eacute;er une sauvegarde</h5>' +
          '<p class="text-muted mb-2">Archive .tar de vos donn&eacute;es, utilisable pour une restauration ou lors d\'une mise &agrave; jour manuelle.</p>' +
          '<ul class="small mb-2" id="bk-dirs"></ul>' +
          '<div class="alert alert-warning small" id="bk-secret">' +
            '<strong>Contenu sensible :</strong> les fichiers de configuration contiennent vos mots de passe en clair ' +
            '(WiFi, MQTT, e-mail, WebPush, acc&egrave;s HTTP). Conservez la sauvegarde en lieu s&ucirc;r.' +
          '</div>' +
          '<div class="small text-muted mb-3" id="bk-estimate"></div>' +
          '<div class="d-flex flex-wrap gap-2">' +
            '<button class="btn btn-primary" id="bk-create">Cr&eacute;er sur l\'appareil</button>' +
            '<a class="btn btn-outline-primary" id="bk-export" href="/api/maint/backup/export" download>T&eacute;l&eacute;charger directement</a>' +
          '</div>' +
          '<div class="small mt-2" id="bk-create-msg"></div>' +
        '</div>' +
        '<div class="card p-4 mb-4">' +
          '<h5>Sauvegardes enregistr&eacute;es</h5>' +
          '<ul class="list-group" id="bk-tars"></ul>' +
        '</div>' +
        '<div class="card p-4 mb-4">' +
          '<h5>Sauvegardes individuelles (appareils)</h5>' +
          '<p class="text-muted small">Copies de fichiers d\'appareils (/bk/*.json). La restauration remplace le fichier de /db au prochain red&eacute;marrage.</p>' +
          '<ul class="list-group" id="bk-jsons"></ul>' +
        '</div>' +
        uploadCard('rs', 'Restaurer une sauvegarde',
                   'Envoyez une archive .tar (ou .tar.gz) cr&eacute;&eacute;e par cette page. Les fichiers sont appliqu&eacute;s au red&eacute;marrage, ' +
                   'qui suit automatiquement. Seuls les r&eacute;pertoires connus sont restaur&eacute;s ; une archive contenant firmware.bin met aussi &agrave; jour le firmware.',
                   '.tar,.gz,.tgz') +
        '<div class="modal fade" id="bk-modal" tabindex="-1"><div class="modal-dialog modal-lg modal-dialog-scrollable"><div class="modal-content">' +
          '<div class="modal-header"><h5 class="modal-title" id="bk-modal-title"></h5>' +
          '<button type="button" class="btn-close" data-bs-dismiss="modal"></button></div>' +
          '<div class="modal-body"><pre class="small mb-0" id="bk-modal-body" style="white-space:pre-wrap"></pre></div>' +
        '</div></div></div>' +
      '</div>'
    );

    $('#bk-create').on('click', function() {
      var btn = $(this).prop('disabled', true);
      $('#bk-create-msg').removeClass('text-danger text-success').text('Création en cours...');
      $.ajax({ url: '/api/maint/backup/create', method: 'POST' })
        .done(function(r) {
          $('#bk-create-msg').addClass('text-success').text('Sauvegarde créée (' + fmtSize(r.size) + ')');
          loadList();
        })
        .fail(function(xhr) { $('#bk-create-msg').addClass('text-danger').text(errText(xhr)); })
        .always(function() { btn.prop('disabled', false); });
    });

    $('#rs-btn').on('click', function() {
      if (!confirm('Restaurer cette sauvegarde ? La configuration actuelle sera remplacée et l\'appareil redémarrera.')) return;
      startUpload('#rs-file', '#rs-chunked', 'rs-p', '#rs-btn');
    });

    $('#bk-tars').on('click', '.bk-del', function() {
      var name = $(this).data('name');
      if (!confirm('Supprimer ' + name + ' ?')) return;
      A.postJSON('/api/maint/backup/delete', { name: name })
        .done(function() { A.showAlert('Supprimé'); loadList(); })
        .fail(function(xhr) { A.showAlert(esc(errText(xhr)), 'danger'); });
    });

    $('#bk-jsons').on('click', '.bk-view', function() {
      var name = $(this).data('name');
      $('#bk-modal-title').text(name);
      $('#bk-modal-body').text('Chargement...');
      if (window.bootstrap) bootstrap.Modal.getOrCreateInstance(document.getElementById('bk-modal')).show();
      $.ajax({ url: '/api/maint/backup/download?inline=1&name=' + encodeURIComponent(name), dataType: 'text' })
        .done(function(t) {
          var txt = t;
          try { txt = JSON.stringify(JSON.parse(t), null, 2); } catch (e) { /* texte brut */ }
          $('#bk-modal-body').text(txt);
        })
        .fail(function(xhr) { $('#bk-modal-body').text(errText(xhr)); });
    }).on('click', '.bk-restore', function() {
      var name = $(this).data('name');
      if (!confirm('Restaurer ' + name + ' vers /db/ ?\nLe fichier actuel sera remplacé au prochain redémarrage.')) return;
      A.postJSON('/api/maint/backup/restore-json', { name: name })
        .done(function() {
          if (confirm('Fichier prêt. Redémarrer maintenant pour l\'appliquer ?')) {
            $.post('/api/system/reboot');
            A.showAlert('Redémarrage en cours...');
          }
        })
        .fail(function(xhr) { A.showAlert(esc(errText(xhr)), 'danger'); });
    });

    loadList();
  }

  function fileItem(f, buttons) {
    var li = $('<li class="list-group-item d-flex justify-content-between align-items-center flex-wrap gap-2">');
    li.append($('<div>').append($('<span class="fw-semibold me-2">').text(f.name),
                               $('<small class="text-muted">').text(fmtSize(f.size) + ' – ' + fmtDate(f.mtime))));
    li.append($('<div class="d-flex gap-1">').append(buttons));
    return li;
  }

  function loadList() {
    $.getJSON('/api/maint/backup/list', function(d) {
      var dirs = String(d.dirs || '').split(',');
      $('#bk-dirs').empty();
      dirs.forEach(function(x) { if ($.trim(x)) $('#bk-dirs').append($('<li>').text('/' + $.trim(x))); });
      if (!d.httpAuth) {
        $('#bk-secret').removeClass('alert-warning').addClass('alert-danger')
          .append($('<div class="mt-1">').html('La <a href="#configHTTP">s&eacute;curit&eacute; HTTP</a> est d&eacute;sactiv&eacute;e : ' +
                                               'tout appareil du r&eacute;seau peut t&eacute;l&eacute;charger ces sauvegardes.'));
      }
      var est = 'Taille estimée : ' + fmtSize(d.estimate) + ' (' + (d.estimateFiles || 0) + ' fichiers)';
      if (d.fsTotal) est += ' – espace utilisé : ' + fmtSize(d.fsUsed) + ' / ' + fmtSize(d.fsTotal);
      $('#bk-estimate').text(est);

      var tars = $('#bk-tars').empty();
      (d.backups || []).forEach(function(f) {
        var url = '/api/maint/backup/download?name=' + encodeURIComponent(f.name);
        tars.append(fileItem(f, [
          $('<a class="btn btn-sm btn-success" download>').attr('href', url).text('Télécharger'),
          $('<button class="btn btn-sm btn-outline-danger bk-del">').attr('data-name', f.name).text('Supprimer')
        ]));
      });
      if (!(d.backups || []).length) tars.append('<li class="list-group-item text-muted">Aucune sauvegarde enregistr&eacute;e</li>');

      var js = $('#bk-jsons').empty();
      (d.files || []).forEach(function(f) {
        js.append(fileItem(f, [
          $('<button class="btn btn-sm btn-outline-primary bk-view">').attr('data-name', f.name).text('Voir'),
          $('<button class="btn btn-sm btn-success bk-restore">').attr('data-name', f.name).text('Restaurer')
        ]));
      });
      if (!(d.files || []).length) js.append('<li class="list-group-item text-muted">Aucune sauvegarde individuelle</li>');
    }).fail(function(xhr) {
      $('#bk-tars').html('<li class="list-group-item text-danger">' + esc(errText(xhr)) + '</li>');
    });
  }

  // ==========================================================================
  // PAGE : Mise a jour
  // ==========================================================================

  function stripV(v) { return String(v || '').replace(/^v/i, ''); }
  // Comparaison numerique de versions ("v3.0" / "3.0.1" / "2.37") : > 0 si a
  // est plus recente. La release peut encore porter le firmware Arduino de la
  // LiXee-Box (2.x), que le pont refuse : pas de "nouveaute" pour une version
  // plus ancienne que celle installee.
  function cmpVer(a, b) {
    var x = stripV(a).split(/[^0-9]+/).filter(Boolean).map(Number);
    var y = stripV(b).split(/[^0-9]+/).filter(Boolean).map(Number);
    for (var i = 0; i < Math.max(x.length, y.length); i++) {
      var d = (x[i] || 0) - (y[i] || 0);
      if (d) return d;
    }
    return 0;
  }

  function pageUpdate() {
    A.setContent(
      '<div class="container py-4" style="max-width:860px">' +
        '<h4 class="mb-4">Mise &agrave; jour du firmware</h4>' +
        '<div class="card p-3 mb-4"><table class="table table-sm mb-0" id="up-info"><tbody>' +
          '<tr><td colspan="2" class="text-muted">Chargement...</td></tr></tbody></table></div>' +
        '<ul class="nav nav-tabs" role="tablist" id="up-tabs">' +
          '<li class="nav-item" role="presentation" id="up-tab-auto-li" style="display:none">' +
            '<button class="nav-link" data-bs-toggle="tab" data-bs-target="#up-auto" type="button" role="tab">Automatique</button></li>' +
          '<li class="nav-item" role="presentation">' +
            '<button class="nav-link active" id="up-tab-manual" data-bs-toggle="tab" data-bs-target="#up-manual" type="button" role="tab">Manuelle</button></li>' +
        '</ul>' +
        '<div class="tab-content pt-3">' +
          '<div class="tab-pane fade" id="up-auto" role="tabpanel">' +
            '<div class="card p-4 mb-4">' +
              '<h5>Derni&egrave;re version disponible</h5>' +
              '<div id="up-release" class="mb-3 text-muted">V&eacute;rification...</div>' +
              '<button class="btn btn-primary" id="up-auto-btn">Mettre &agrave; jour</button>' +
              progressHtml('up-auto-p') +
            '</div>' +
          '</div>' +
          '<div class="tab-pane fade show active" id="up-manual" role="tabpanel">' +
            uploadCard('up', 'Mise &agrave; jour manuelle',
                       'Image firmware <code>.bin</code> du LiXee MatterBridge, ou paquet <code>.tar</code> / <code>.tar.gz</code> ' +
                       '(firmware.bin + fichiers de l\'interface). L\'image est v&eacute;rifi&eacute;e (puce, produit, empreinte) avant d\'&ecirc;tre activ&eacute;e ; ' +
                       'un firmware LiXee-Box n\'est pas accept&eacute;. Le red&eacute;marrage suit automatiquement.',
                       '.bin,.tar,.gz,.tgz') +
          '</div>' +
        '</div>' +
      '</div>'
    );

    $('#up-btn').on('click', function() {
      if (!confirm('Lancer la mise à jour ? L\'appareil redémarrera.')) return;
      startUpload('#up-file', '#up-chunked', 'up-p', '#up-btn');
    });

    $('#up-auto-btn').on('click', function() {
      if (!confirm('Télécharger et installer la mise à jour ? L\'appareil redémarrera.')) return;
      var btn = $(this).prop('disabled', true);
      setProgress('up-auto-p', 0, 'Démarrage...');
      $.ajax({ url: '/api/maint/auto/start', method: 'POST' })
        .done(function() { pollAuto(); })
        .fail(function(xhr) { setProgress('up-auto-p', null, errText(xhr), 'error'); btn.prop('disabled', false); });
    });

    $.when($.getJSON('/api/maint/status'), $.getJSON('/api/system/info')).done(function(a, b) {
      var st = a[0] || {}, sys = b[0] || {};
      var app = st.app || {};
      var current = sys.version || app.version || '--';
      var rows = [
        ['Version', current],
        ['Build', app.build || '--'],
        ['ESP-IDF', app.idf || '--'],
        ['Partition active', (app.running || '--') + (app.next ? ' (prochaine : ' + app.next + ', ' + fmtSize(app.maxImageSize) + ' max)' : '')]
      ];
      var tb = $('#up-info tbody').empty();
      rows.forEach(function(r) { tb.append($('<tr>').append($('<td class="text-muted">').text(r[0]), $('<td>').text(r[1]))); });

      // Source fixe (release GitHub de la LiXee-Box), sans mode developpeur ni
      // reglage : la mise a jour automatique est toujours proposee
      if (st.autoEnabled) {
        $('#up-tab-auto-li').show();
        // Onglet par defaut de la LiXee-Box
        var tabBtn = $('#up-tab-auto-li button')[0];
        if (window.bootstrap && bootstrap.Tab) bootstrap.Tab.getOrCreateInstance(tabBtn).show();
        loadRelease(st.releaseInfoUrl, current);
      }
      // Operation en cours (lancee depuis un autre onglet) : afficher son avancement
      if (st.busy !== 'none' && st.busyChannel === 'auto') {
        $('#up-auto-btn').prop('disabled', true);
        pollAuto();
      } else if (st.auto && st.auto.state === 'error' && st.autoEnabled) {
        setProgress('up-auto-p', null, st.auto.status, 'error');
      }
      if (st.reboot) waitReboot('up-p');
      // Restauration preparee (fichier d'appareil) pas encore appliquee : le dire
      else if (st.restorePending) A.showAlert('Une restauration est en attente : elle sera appliquée au prochain redémarrage.', 'warning');
    }).fail(function() {
      $('#up-info tbody').html('<tr><td class="text-danger">Erreur de connexion</td></tr>');
    });
  }

  function loadRelease(infoUrl, current) {
    if (!infoUrl) {
      $('#up-release').text('Aucune information de version configurée : le bouton installe le fichier de la source configurée.');
      return;
    }
    $.ajax({ url: infoUrl, dataType: 'json', timeout: 10000 }).done(function(rel) {
      var tag = rel.tag_name || '';
      var box = $('#up-release').empty().removeClass('text-muted');
      box.append($('<div class="fw-semibold">').text((rel.name || tag) + (tag ? ' (' + tag + ')' : '')));
      if (rel.published_at) box.append($('<div class="small text-muted">').text('Publiée le ' + new Date(rel.published_at).toLocaleDateString('fr-FR')));
      if (rel.body) box.append($('<pre class="small mt-2 p-2 bg-light rounded" style="white-space:pre-wrap;max-height:240px;overflow:auto">').text(rel.body));
      if (tag && cmpVer(tag, current) <= 0) {
        box.append($('<div class="alert alert-success small mt-2 mb-0">').text('Le firmware est à jour.'));
      } else if (tag) {
        box.append($('<div class="alert alert-info small mt-2 mb-0">').text('Nouvelle version disponible : ' + tag + ' (installée : ' + current + ').'));
      }
    }).fail(function() {
      $('#up-release').text('Impossible de lire les informations de version (réseau ou URL).');
    });
  }

  // Sondage de l'etat du telechargement automatique (traite dans une tache :
  // le serveur web reste disponible)
  function pollAuto() {
    var id = A.pageInterval(function() {
      $.ajax({ url: '/api/maint/status', dataType: 'json', timeout: 4000 }).done(function(st) {
        var c = st.auto || {};
        if (c.state === 'error') {
          clearInterval(id);
          setProgress('up-auto-p', null, c.status || 'Erreur', 'error');
          $('#up-auto-btn').prop('disabled', false);
          return;
        }
        var msg = c.status || 'En cours...';
        if (st.total) msg += ' (' + fmtSize(st.received) + ' / ' + fmtSize(st.total) + ')';
        else if (st.received) msg += ' (' + fmtSize(st.received) + ')';
        setProgress('up-auto-p', c.progress, msg, c.state === 'done' ? 'done' : '');
        if (st.reboot || c.state === 'done') {
          clearInterval(id);
          waitReboot('up-auto-p');
        }
      });
    }, 1500);
  }

  // Pastille "mise a jour disponible" du menu A propos (AboutMaj de la
  // LiXee-Box) : version publiee (source configuree) differente de la version
  // installee. Verifiee au chargement puis toutes les 6 heures.
  function checkUpdateDot() {
    $.ajax({ url: '/api/maint/status', dataType: 'json', cache: false, global: false }).done(function(st) {
      if (!st.autoEnabled || !st.releaseInfoUrl) return;
      $.ajax({ url: '/api/system/info', dataType: 'json', cache: false, global: false }).done(function(info) {
        $.ajax({ url: st.releaseInfoUrl, dataType: 'json', timeout: 10000, global: false }).done(function(rel) {
          var tag = rel && rel.tag_name;
          var newer = !!tag && cmpVer(tag, info.version || '') > 0;
          $('.AboutMaj').css('display', newer ? 'inline-block' : 'none');
        });
      });
    });
  }
  window.addEventListener('load', function() {
    setTimeout(checkUpdateDot, 3000);
    setInterval(checkUpdateDot, 6 * 3600 * 1000);
  });

  A.registerPage('backup', pageBackup);
  A.registerPage('update', pageUpdate);
})();
