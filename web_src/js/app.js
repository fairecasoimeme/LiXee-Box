// ============================================================================
// LiXee MatterBridge - Application SPA
// Navigation hash-based + chargement dynamique des pages
// ============================================================================

(function() {
  'use strict';

  // --- Routing ---
  // Les routes vers pageUnavailable pilotent des fonctions absentes de ce
  // firmware (regles, notifications, OTA, sauvegarde, Marstek, UDP, email).
  // Les fonctions page* d'origine restent definies : pour reactiver une page
  // une fois la fonction portee, il suffit de retablir sa route.
  var routes = {
    'energy':        pageEnergy,
    'devices':       pageDevices,
    'dashboard':     pageDashboard,
    'wifi':          pageNetwork,   // ancienne page WiFi : section de la page LAN
    'network':       pageNetwork,
    'zigbee':        pageZigbee,
    'mqtt':          pageMqtt,
    'webpush':       pageWebpush,
    'matter':        pageMatter,
    'configGeneral': pageConfigGeneral,
    'configEnergy':  pageConfigEnergy,
    'configRules':   pageUnavailable,
    'editRule':      pageUnavailable,
    'addRule':       pageUnavailable,
    'configHorloge': pageConfigHorloge,
    'configHTTP':    pageConfigHTTP,
    'configMarstek': pageUnavailable,
    'configUdp':     pageUnavailable,
    'configNotifMail': pageUnavailable,
    'tools':         pageTools,
    'files':         pageFiles,
    'backup':        pageUnavailable,
    'notifications': pageUnavailable,
    'help':          pageHelp,
    'system':        pageSystem,
    'update':        pageUnavailable
  };

  // Minuteries de la page courante : arretees a chaque changement de page,
  // pour qu'un rafraichissement ne continue pas sur une page quittee.
  var pageTimers = [];
  function pageInterval(fn, ms) {
    var id = setInterval(fn, ms);
    pageTimers.push(id);
    return id;
  }
  function clearPageTimers() {
    while (pageTimers.length) clearInterval(pageTimers.pop());
  }

  // Chargement a la demande des bibliotheques (servies compressees par le
  // pont) : seules les pages qui en ont besoin les telechargent, une fois.
  var libPromises = {};
  function loadScript(src) {
    if (!libPromises[src]) {
      libPromises[src] = new Promise(function(resolve, reject) {
        var s = document.createElement('script');
        s.src = src;
        s.onload = resolve;
        s.onerror = function() { delete libPromises[src]; reject(new Error(src)); };
        document.head.appendChild(s);
      });
    }
    return libPromises[src];
  }
  // Dans l'ordre : Raphael avant JustGage et Morris, qui en dependent
  function loadScripts(list) {
    return list.reduce(function(p, src) {
      return p.then(function() { return loadScript(src); });
    }, Promise.resolve());
  }
  var CHART_LIBS = ['js/raphael-min.js', 'js/justgage.min.js', 'js/morris.min.js',
                    'js/chart.umd.min.js', 'js/chart-zoom.min.js'];

  // Parametres de la route courante : "#files?dir=config" -> {dir:'config'}
  var routeParams = {};

  function navigate() {
    var full = window.location.hash.replace('#','') || 'energy';
    var qpos = full.indexOf('?');
    var hash = qpos >= 0 ? full.substring(0, qpos) : full;
    routeParams = {};
    if (qpos >= 0) {
      full.substring(qpos + 1).split('&').forEach(function(kv) {
        var p = kv.split('=');
        if (p[0]) routeParams[decodeURIComponent(p[0])] = decodeURIComponent(p[1] || '');
      });
    }
    var fn = routes[hash];
    clearPageTimers();
    // Fenetre modale restee ouverte (ex. Donnees Linky) : son fond grise
    // resterait par-dessus la nouvelle page
    $('.modal-backdrop').remove();
    $('body').removeClass('modal-open').css({ overflow: '', paddingRight: '' });
    if (fn) {
      document.getElementById('page-content').innerHTML = '<div class="text-center p-5"><div class="spinner-border" role="status"></div></div>';
      fn();
    }
  }

  // Session expiree ou absente (protection HTTP active) : le serveur repond
  // 401 aux appels de l'application, qui renvoie vers la page de connexion.
  $(document).ajaxError(function(ev, xhr) {
    // Retour a la page courante apres reconnexion (comme la LiXee-Box)
    if (xhr && xhr.status === 401) window.location.href = '/login?next=' + encodeURIComponent('/' + window.location.hash);
  });

  // API commune aux modules de pages (js/page_*.js, charges apres ce fichier
  // par index.html) : chaque lot du portage LiXee-Box ajoute ses pages par
  // registerPage() sans modifier app.js. Les modules sont charges avant
  // l'evenement 'load', donc avant la premiere navigation.
  window.LixeeApp = {
    registerPage: function(name, fn) { routes[name] = fn; },
    setContent: setContent,
    showAlert: showAlert,
    escHtml: escHtml,
    postJSON: postJSON,
    apiErrorText: apiErrorText,
    apiErrorMsg: apiErrorMsg,
    pageInterval: pageInterval,
    loadScripts: loadScripts,
    CHART_LIBS: CHART_LIBS,
    params: function() { return routeParams; },
    modules: function() { return activeModules; }
  };

  window.addEventListener('hashchange', navigate);
  window.addEventListener('load', function() {
    loadVersion();
    updateClock();
    setInterval(updateClock, 30000);
    navigate();
  });

  function loadVersion() {
    $.getJSON('/api/system/info', function(data) {
      $('#app-version').text(data.version || '--');
      $('.nav-logout').toggle(!!data.secureHttp);   // "Deconnexion" si la protection est active
      applyModules(data.modules || {});
    });
    $.getJSON('/api/config/general', function(g) { showTools(g.developerMode); });
  }

  // Menus propres a un module (attribut data-mod="lora" ou "zigbee,lora") :
  // retires si aucun des modules listes n'est present, comme le faisait
  // /modules.js sur la LiXee-Box.
  var activeModules = {};
  function applyModules(mods) {
    activeModules = mods;
    $('[data-mod]').each(function() {
      var wanted = String($(this).attr('data-mod')).split(',');
      var present = wanted.some(function(m) { return mods[$.trim(m)]; });
      if (!present) $(this).remove();
    });
  }

  function updateClock() {
    $.ajax({ url: '/getFormattedDate', dataType: 'text', cache: false, global: false })
      .done(function(t) { if (t) $('#FormattedDate').text(t); else browserClock(); })
      .fail(browserClock);
  }
  // Repli : heure du navigateur (box sans heure valide ou injoignable)
  function browserClock() {
    var d = new Date();
    var s = ('0'+d.getDate()).slice(-2)+'/'+('0'+(d.getMonth()+1)).slice(-2)+'/'+d.getFullYear()+' '+('0'+d.getHours()).slice(-2)+':'+('0'+d.getMinutes()).slice(-2);
    $('#FormattedDate').text(s);
  }

  // Popups d'aide (showPopup/hidePopup de la LiXee-Box) : les textes sont
  // dans help-energy.html, charge au premier clic puis garde dans le body
  // (hors du contenu remplace a chaque page).
  function showPopupEl(el) {
    document.body.style.transform = '';
    el.classList.add('show');
    document.body.style.overflow = 'hidden';
  }
  window.showPopup = function(id) {
    var el = document.getElementById(id);
    if (el) { showPopupEl(el); return; }
    fetch('help-energy.html').then(function(r) { return r.text(); }).then(function(t) {
      if (!document.getElementById('help-popups')) {
        var d = document.createElement('div');
        d.id = 'help-popups';
        d.innerHTML = t;
        document.body.appendChild(d);
      }
      var e2 = document.getElementById(id);
      if (e2) showPopupEl(e2);
    }).catch(function() {});
  };
  window.hidePopup = function(id, event) {
    // Clic dans le contenu : seul un clic sur le fond ferme
    if (event && event.target !== event.currentTarget) return;
    var el = document.getElementById(id);
    if (el) el.classList.remove('show');
    document.body.style.overflow = 'auto';
  };
  document.addEventListener('keydown', function(e) {
    if (e.key !== 'Escape') return;
    var open = document.querySelector('.popup.show');
    if (open) window.hidePopup(open.id);
  });
  function helpLink(id) {
    return '<a href="javascript:void(0)" onclick="showPopup(\'' + id + '\')" ' +
           'class="position-absolute bottom-0 start-0 p-2 text-muted" title="Aide"><span class="hi">?</span></a>';
  }

  function setContent(html) {
    document.getElementById('page-content').innerHTML = html;
  }

  // Page affichee pour une fonction absente de ce firmware.
  function pageUnavailable() {
    setContent(
      '<div class="container py-5"><div class="card p-4 text-center">' +
        '<h5>Fonction non disponible</h5>' +
        '<p class="text-muted mb-0">Cette fonction n\'est pas disponible dans cette version du firmware LiXee MatterBridge.</p>' +
      '</div></div>'
    );
  }

  function showAlert(msg, type) {
    var el = document.getElementById('alert');
    el.className = 'alert alert-' + (type||'success');
    el.innerHTML = msg;
    el.style.display = 'block';
    setTimeout(function(){ el.style.display='none'; }, 4000);
  }

  // Echappe une valeur avant insertion dans du HTML. Indispensable pour tout
  // texte venu de l'exterieur : un point d'acces voisin choisit librement son
  // nom, qui finit dans la liste du scan WiFi.
  function escHtml(s) {
    return String(s == null ? '' : s)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
  }

  // POST JSON ; renvoie la promesse jQuery (chainable avec .then).
  function postJSON(url, payload) {
    return $.ajax({ url: url, method: 'POST', contentType: 'application/json',
                    data: JSON.stringify(payload) });
  }

  // Message d'erreur renvoye par le serveur (JSON {message} ou texte brut).
  function apiErrorMsg(xhr, fallback) {
    var m = (xhr && xhr.responseJSON && xhr.responseJSON.message) ||
            (xhr && xhr.responseText && xhr.responseText.length < 160 ? xhr.responseText : '');
    return escHtml(m || fallback || 'Erreur');
  }

  // ============================================================================
  // PAGE: Energie (portage de handleStatusEnergy V2 de la LiXee-Box)
  // Toutes les donnees viennent de /api/energy/* (contrat : docs/energy_api.md) :
  // le serveur calcule agregats et couts, la page ne fait que la mise en forme.
  // ============================================================================
  var ENERGY_PERIODS = ['hour', 'day', 'month', 'year'];
  var energyTime = 'hour';
  var energyGen = 0;          // incremente a chaque affichage : les reponses d'un affichage precedent sont ignorees
  var energyDistType = '';    // repartition en Wh ('') ou en euros ('euro')
  var energyCharts = {};      // instances Chart.js : power, energy, gaz, water
  var energyGauges = { sig: '', list: [] };
  var energyState = {};       // dernier resume recu (configured, tri, producer)
  var donutChart = null;
  var donutEl = null;
  var donutEmpty = false;
  var ENERGY_CSV_ICON = '<svg xmlns="http://www.w3.org/2000/svg" style="width:22px;" width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>';

  function pageEnergy(time) {
    // "#energy?time=day" : periode passee dans l'URL
    if (!time && ENERGY_PERIODS.indexOf(routeParams.time) >= 0) time = routeParams.time;
    if (time && ENERGY_PERIODS.indexOf(time) >= 0) energyTime = time;
    var t = energyTime;
    var isHour = (t === 'hour');
    var gen = ++energyGen;

    // Icones de periode
    var iconHour = '<svg xmlns="http://www.w3.org/2000/svg" style="width:32px" fill="currentColor" viewBox="0 0 16 16"><path d="M8.515 1.019A7 7 0 0 0 8 1V0a8 8 0 0 1 .589.022zm2.004.45a7 7 0 0 0-.985-.299l.219-.976q.576.129 1.126.342zm1.37.71a7 7 0 0 0-.439-.27l.493-.87a8 8 0 0 1 .979.654l-.615.789a7 7 0 0 0-.418-.302zm1.834 1.79a7 7 0 0 0-.653-.796l.724-.69q.406.429.747.91zm.744 1.352a7 7 0 0 0-.214-.468l.893-.45a8 8 0 0 1 .45 1.088l-.95.313a7 7 0 0 0-.179-.483m.53 2.507a7 7 0 0 0-.1-1.025l.985-.17q.1.58.116 1.17zm-.131 1.538q.05-.254.081-.51l.993.123a8 8 0 0 1-.23 1.155l-.964-.267q.069-.247.12-.501m-.952 2.379q.276-.436.486-.908l.914.405q-.24.54-.555 1.038zm-.964 1.205q.183-.183.35-.378l.758.653a8 8 0 0 1-.401.432z"/><path d="M8 1a7 7 0 1 0 4.95 11.95l.707.707A8.001 8.001 0 1 1 8 0z"/><path d="M7.5 3a.5.5 0 0 1 .5.5v5.21l3.248 1.856a.5.5 0 0 1-.496.868l-3.5-2A.5.5 0 0 1 7 9V3.5a.5.5 0 0 1 .5-.5"/></svg>';
    var iconDay = '<svg xmlns="http://www.w3.org/2000/svg" style="width:32px" fill="currentColor" viewBox="0 0 16 16"><path d="M14 0H2a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2M1 3.857C1 3.384 1.448 3 2 3h12c.552 0 1 .384 1 .857v10.286c0 .473-.448.857-1 .857H2c-.552 0-1-.384-1-.857z"/><path d="M12 7a1 1 0 1 0 0-2 1 1 0 0 0 0 2"/></svg>';
    var iconMonth = '<svg xmlns="http://www.w3.org/2000/svg" style="width:32px" fill="currentColor" viewBox="0 0 16 16"><path d="M14 0H2a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2M1 3.857C1 3.384 1.448 3 2 3h12c.552 0 1 .384 1 .857v10.286c0 .473-.448.857-1 .857H2c-.552 0-1-.384-1-.857z"/><path d="M6.5 7a1 1 0 1 0 0-2 1 1 0 0 0 0 2m3 0a1 1 0 1 0 0-2 1 1 0 0 0 0 2m3 0a1 1 0 1 0 0-2 1 1 0 0 0 0 2m-9 3a1 1 0 1 0 0-2 1 1 0 0 0 0 2m3 0a1 1 0 1 0 0-2 1 1 0 0 0 0 2m3 0a1 1 0 1 0 0-2 1 1 0 0 0 0 2m3 0a1 1 0 1 0 0-2 1 1 0 0 0 0 2m-9 3a1 1 0 1 0 0-2 1 1 0 0 0 0 2m3 0a1 1 0 1 0 0-2 1 1 0 0 0 0 2m3 0a1 1 0 1 0 0-2 1 1 0 0 0 0 2"/></svg>';
    var iconYear = '<svg xmlns="http://www.w3.org/2000/svg" style="width:32px" fill="currentColor" viewBox="0 0 16 16"><path d="M0 2a2 2 0 0 1 2-2h12a2 2 0 0 1 2 2zm0 1v11a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V3z"/><text x="8" y="11" font-size="6" text-anchor="middle" fill="white" font-family="Arial">12</text></svg>';

    function linkCls(p) { return t === p ? 'link active' : 'link'; }

    var iconBolt = '<svg xmlns="http://www.w3.org/2000/svg" style="width:40px;" width="28" height="28" fill="currentColor" class="me-2" viewBox="0 0 16 16"><path d="M11.251.068a.5.5 0 0 1 .227.58L9.677 6.5H13a.5.5 0 0 1 .364.843l-8 8.5a.5.5 0 0 1-.842-.49L6.323 9.5H3a.5.5 0 0 1-.364-.843l8-8.5a.5.5 0 0 1 .615-.09z"/></svg>';
    var iconGear = '<svg xmlns="http://www.w3.org/2000/svg" style="width:24px;" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"></circle><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"></path></svg>';
    var gearLink = '<a href="#configEnergy" class="position-absolute bottom-0 end-0 p-2 text-muted" title="Param&eacute;trer la tarification">' + iconGear + '</a>';

    setContent(
      '<div class="container">' +
        '<h4>Tableau de bord &eacute;nerg&eacute;tique</h4>' +
        '<div class="row g-4"><div class="col-md-12">' +
          '<div class="nav justify-content-end">' +
            '<div><a class="' + linkCls('hour') + '" href="javascript:void(0)" onclick="window._energyNav(\'hour\')" title="Heure">' + iconHour + '</a></div>&nbsp;' +
            '<div><a class="' + linkCls('day') + '" href="javascript:void(0)" onclick="window._energyNav(\'day\')" title="Jour">' + iconDay + '</a></div>&nbsp;' +
            '<div><a class="' + linkCls('month') + '" href="javascript:void(0)" onclick="window._energyNav(\'month\')" title="Mois">' + iconMonth + '</a></div>&nbsp;' +
            '<div><a class="' + linkCls('year') + '" href="javascript:void(0)" onclick="window._energyNav(\'year\')" title="Ann&eacute;e">' + iconYear + '</a></div>' +
          '</div>' +
        '</div></div>' +
      '</div>' +

      // Zone principale
      '<div class="container py-4" id="cadre_energy">' +

        // Etat du ZLinky (summary.connected / summary.alert)
        '<div id="energy-status" class="alert alert-danger" role="alert" style="display:none;">Appareil d&eacute;connect&eacute;</div>' +
        '<div class="row g-2 mb-2" id="energy-alert-row" style="display:none;"><div class="col-12"><span style="color:red;" id="energyAlert"></span></div></div>' +

        // Aucun ZLinky configure ({"configured":false})
        '<div id="energy-notconf" class="card p-4 mb-3" style="display:none;">' +
          '<h5 class="card-title">Aucun compteur Linky configur&eacute;</h5>' +
          '<p class="text-muted mb-0">S&eacute;lectionnez votre ZLinky dans <a href="#configEnergy">Config &Eacute;nergie</a> pour afficher le tableau de bord.</p>' +
        '</div>' +

        '<div id="linky-section">' +

          // Tarif en cours (/api/energy/tariff)
          '<div class="row g-2 mb-3" id="tariff-section" style="display:none;"><div class="col-12">' +
            '<div class="card p-3" id="tariff-card" style="background:#2980b9;color:white;">' +
              '<div class="row align-items-center">' +
                '<div class="col-auto"><div class="d-flex align-items-center">' + iconBolt +
                  '<div>' +
                    '<span style="opacity:0.8;font-size:0.85rem;">Tarif en cours : </span>' +
                    '<span class="fw-bold" style="font-size:1.15rem;" id="tariff-name">---</span>' +
                    '<div id="tariff-price" style="opacity:0.8;font-size:0.85rem;display:none;"></div>' +
                  '</div>' +
                '</div></div>' +
                '<div class="col-auto ms-auto" id="tempo-section" style="display:none;"><div class="d-flex gap-3">' +
                  '<div class="text-center"><small style="opacity:0.8;display:block;">Aujourd\'hui</small><div class="tempo-badge tempo-undef" id="tempo-today">?</div></div>' +
                  '<div class="text-center"><small style="opacity:0.8;display:block;">Demain</small><div class="tempo-badge tempo-undef" id="tempo-tomorrow">-</div></div>' +
                '</div></div>' +
              '</div>' +
            '</div>' +
          '</div></div>' +

          // Etiquette energetique (summary.label, masquee si null)
          '<div class="row g-2">' +
            '<div class="col-12">' +
              '<div class="card p-4" id="label-energy" style="display:none;">' +
                '<h5 class="card-title">&Eacute;tiquette &eacute;nerg&eacute;tique</h5>' +
                '<div class="card-body p-0">' +
                  '<div class="energy-bars-horizontal">' +
                    '<div class="energy-bar-horizontal A" data-class="A"><span class="bar-label-horizontal">A</span><div class="cursor-horizontal"></div><div class="value-display-horizontal"></div></div>' +
                    '<div class="energy-bar-horizontal B" data-class="B"><span class="bar-label-horizontal">B</span><div class="cursor-horizontal"></div><div class="value-display-horizontal"></div></div>' +
                    '<div class="energy-bar-horizontal C" data-class="C"><span class="bar-label-horizontal">C</span><div class="cursor-horizontal"></div><div class="value-display-horizontal"></div></div>' +
                    '<div class="energy-bar-horizontal D" data-class="D"><span class="bar-label-horizontal">D</span><div class="cursor-horizontal"></div><div class="value-display-horizontal"></div></div>' +
                    '<div class="energy-bar-horizontal E" data-class="E"><span class="bar-label-horizontal">E</span><div class="cursor-horizontal"></div><div class="value-display-horizontal"></div></div>' +
                    '<div class="energy-bar-horizontal F" data-class="F"><span class="bar-label-horizontal">F</span><div class="cursor-horizontal"></div><div class="value-display-horizontal"></div></div>' +
                    '<div class="energy-bar-horizontal G" data-class="G"><span class="bar-label-horizontal">G</span><div class="cursor-horizontal"></div><div class="value-display-horizontal"></div></div>' +
                  '</div>' +
                '</div>' +
                helpLink('popupHelpEnergyLabel') +
                gearLink +
              '</div>' +
            '</div>' +
          '</div>' +

          // Jauges + Repartition + Tendances
          '<div class="row g-2 mt-2">' +
            // Jauges (construites a la premiere reponse de /api/energy/gauge)
            '<div class="col-lg-4 col-md-12 col-12">' +
              '<div id="energyGauge" class="card p-4" style="height:100%;min-height:270px;">' +
                '<h5 class="card-title">' + (isHour ? 'Linky : Puissances' : 'Linky') + '</h5>' +
                '<div class="card-body" style="margin-left:-30px;margin-right:-30px;">' +
                  '<div class="row" id="energy-gauges"><div class="col-12 text-center text-muted">Chargement...</div></div>' +
                '</div>' +
                '<div class="text-end"><button type="button" class="btn btn-sm btn-outline-secondary" id="btn-linky-datas">Donn&eacute;es Linky</button></div>' +
                helpLink(isHour ? 'popupHelpPowerJaugeHour' : 'popupHelpPowerJauge') +
              '</div>' +
            '</div>' +

            // Repartition energetique (donut + tableau)
            '<div class="col-lg-5 col-md-6 col-12">' +
              '<div class="card p-4" id="energyDistrib" style="height:100%;">' +
                '<h5 class="card-title">R&eacute;partition &eacute;nerg&eacute;tique</h5>' +
                '<div class="row" style="font-size:12px;">' +
                  '<div class="col-md-12 col-lg-6">' +
                    '<div class="card-body position-relative p-1" style="min-height:240px;">' +
                      '<div id="donut-chart" style="padding-top:40px;"></div>' +
                    '</div>' +
                    '<div class="text-center mb-2"><div class="btn-group btn-group-sm" role="group" aria-label="Unit&eacute;">' +
                      '<button type="button" class="btn btn-outline-secondary" data-dist="" title="R&eacute;partition en &eacute;nergie">Wh</button>' +
                      '<button type="button" class="btn btn-outline-secondary" data-dist="euro" title="R&eacute;partition en euros">&euro;</button>' +
                    '</div></div>' +
                  '</div>' +
                  '<div class="col-md-12 col-lg-6">' +
                    '<div class="card-body position-relative p-1" style="padding-bottom:36px !important;">' +
                      '<div id="trend-datas"></div>' +
                    '</div>' +
                  '</div>' +
                '</div>' +
                helpLink('popupHelpEnergyDispatch') +
                gearLink +
              '</div>' +
            '</div>' +

            // Tendances
            '<div class="col-lg-3 col-md-6 col-12">' +
              '<div class="card p-4" id="energyTrend" style="height:100%;">' +
                '<h5 class="card-title">Tendances</h5>' +
                '<div class="card-body position-relative p-1" style="padding-bottom:36px !important;">' +
                  '<div id="power_trend"><div id="power_trend_head"></div><div id="power_trend_body"><p class="text-muted">Chargement...</p></div></div>' +
                '</div>' +
                helpLink(isHour ? 'popupHelpEnergyTrendHour' : 'popupHelpEnergyTrend') +
              '</div>' +
            '</div>' +
          '</div>' +

          // Graphiques
          '<div class="row g-2 mt-2">' +
            // Puissance apparente (vue heure uniquement)
            (isHour ?
            '<div class="col-md-6">' +
              '<div class="card p-4">' +
                '<h5 class="card-title">Puissance apparente (graphique temps r&eacute;el)</h5>' +
                '<div class="card-body"><div style="position:relative;height:342px;"><canvas id="power-chart"></canvas></div></div>' +
                helpLink('popupHelpApparentPower') +
                '<a href="/api/energy/power/export" download class="position-absolute bottom-0 end-0 p-2 text-muted" title="Exporter en CSV">' + ENERGY_CSV_ICON + '</a>' +
              '</div>' +
            '</div>' : '') +

            // Usage electricite
            '<div class="col-md-6">' +
              '<div class="card p-4">' +
                '<h5 class="card-title">Usage d\'&eacute;lectricit&eacute;</h5>' +
                '<div class="card-body"><div style="position:relative;height:342px;"><canvas id="energy-chart"></canvas></div></div>' +
                helpLink('popupHelpElectricity') +
                '<a href="/api/energy/chart/export?time=' + t + '" download class="position-absolute bottom-0 end-0 p-2 text-muted" title="Exporter en CSV">' + ENERGY_CSV_ICON + '</a>' +
              '</div>' +
            '</div>' +
          '</div>' +

        '</div>' +

        // Gaz et eau (ajoutes si configures)
        '<div class="row g-2 mt-2" id="energy-meters"></div>' +

      '</div>' +

      // Fenetre "Donnees Linky" (/api/energy/linky)
      '<div class="modal fade" id="linkyModal" tabindex="-1" aria-labelledby="linkyModalTitle" aria-hidden="true">' +
        '<div class="modal-dialog modal-dialog-scrollable"><div class="modal-content">' +
          '<div class="modal-header">' +
            '<h5 class="modal-title" id="linkyModalTitle">Donn&eacute;es Linky</h5>' +
            '<button type="button" class="btn-close" data-bs-dismiss="modal" aria-label="Fermer"></button>' +
          '</div>' +
          '<div class="modal-body" id="power_data"></div>' +
        '</div></div>' +
      '</div>'
    );

    // Initialiser les composants une fois les bibliotheques chargees ; sans
    // elles (echec de chargement), les donnees textuelles s'affichent quand meme.
    loadScripts(CHART_LIBS)
      .catch(function(e) { console.log('Bibliotheque graphique indisponible :', e.message); })
      .then(function() { initEnergyPage(t, isHour, gen); });
  }

  window._energyNav = function(t) {
    clearPageTimers();
    pageEnergy(t);
  };

  function getEnergyClass(value) {
    if (value < 50) return { cls: 'A', range: '< 50' };
    if (value <= 90) return { cls: 'B', range: '51 - 90' };
    if (value <= 150) return { cls: 'C', range: '91 - 150' };
    if (value <= 230) return { cls: 'D', range: '151 - 230' };
    if (value <= 330) return { cls: 'E', range: '231 - 330' };
    if (value <= 450) return { cls: 'F', range: '331 - 450' };
    return { cls: 'G', range: '> 451' };
  }

  function displayEnergyLabel(energyClass, value) {
    document.querySelectorAll('.cursor-horizontal').forEach(function(c) { c.classList.remove('active'); });
    document.querySelectorAll('.value-display-horizontal').forEach(function(d) { d.classList.remove('active'); d.textContent = ''; });
    document.querySelectorAll('.energy-bar-horizontal').forEach(function(b) {
      b.style.transform = 'translateY(0)'; b.style.zIndex = '1';
      b.style.boxShadow = 'none'; b.style.border = '2px solid white'; b.style.animation = 'none';
    });
    var bar = document.querySelector('[data-class="' + energyClass + '"]');
    if (bar) {
      var cursor = bar.querySelector('.cursor-horizontal');
      var vd = bar.querySelector('.value-display-horizontal');
      cursor.classList.add('active');
      vd.classList.add('active');
      vd.textContent = value + ' kWh/m\u00b2.an';
      bar.style.transform = 'translateY(-12px) scale(1.05)';
      bar.style.zIndex = '10';
      bar.style.boxShadow = '0 15px 30px rgba(100,100,100,0.4), 0 0 0 2px #ffffff, 0 0 0 4px white';
      bar.style.border = '2px solid #ffffff';
      bar.style.borderRadius = '4px';
    }
  }

  // --- Mise en forme ---
  var ENERGY_COLORS = ['#d35400','#27ae60','#2980b9','#154360','#7f8c8d','#000000','#e74c3c','#c0392b','#f5b041','#145a32'];
  var DONUT_COLORS  = ['#2980b9','#154360','#7f8c8d','#000000','#e74c3c','#c0392b','#f5b041','#145a32'];
  // Part de l'abonnement mensuel imputee a un creneau (repli si costs.periodFactor absent)
  var PERIOD_FACTORS = { hour: 1 / 720, day: 1 / 30, month: 1, year: 12 };
  var TREND_NAMES = {
    day:   ['Aujourd\'hui', 'Hier'],
    month: ['Ce mois', 'Mois précédent'],
    year:  ['Cette année', 'Année précédente']
  };
  var TEMPO_ICONS = { 'tempo-bleu': 'B', 'tempo-blanc': 'W', 'tempo-rouge': 'R' };
  var TOOLTIP_SEP = '────────────────────';

  function arr(v) { return $.isArray(v) ? v : []; }
  function num(v) { var n = Number(v); return (v == null || !isFinite(n)) ? 0 : n; }
  function fmtNum(v, d) {
    return num(v).toLocaleString('fr-FR', { minimumFractionDigits: d, maximumFractionDigits: d });
  }
  // Quantite avec un nombre de decimales adapte a sa grandeur
  function fmtQty(v) {
    var n = num(v), a = Math.abs(n);
    return n.toLocaleString('fr-FR', { maximumFractionDigits: a >= 100 ? 0 : (a >= 1 ? 2 : 3) });
  }
  function fmtEur(v) { return fmtNum(v, 2) + ' €'; }
  function fmtKwh(wh) { return fmtNum(num(wh) / 1000, 2); }          // colonnes "kWh" (sans unite)
  function fmtWh(wh) {                                                 // avec unite, comme getValuekWh
    var n = num(wh);
    return Math.abs(n) >= 1000 ? fmtNum(n / 1000, 2) + ' kWh' : fmtNum(n, 0) + ' Wh';
  }
  // Couleur venue du serveur : seulement #rgb..#rrggbbaa, sinon couleur par defaut
  function safeColor(c, def) {
    return /^#[0-9a-fA-F]{3,8}$/.test(String(c || '')) ? String(c) : def;
  }
  // Valeur acceptee par un <input type="color"> (#rrggbb uniquement)
  function colorInputValue(c, def) {
    var s = String(c || '');
    if (/^#[0-9a-fA-F]{6}$/.test(s)) return s.toLowerCase();
    if (/^#[0-9a-fA-F]{3}$/.test(s)) return ('#' + s[1] + s[1] + s[2] + s[2] + s[3] + s[3]).toLowerCase();
    return def;
  }

  // Requete de la page Energie : les reponses arrivees apres un changement de
  // periode ou de page sont ignorees. Renvoie une promesse toujours resolue.
  function energyAlive(gen) { return gen === energyGen && !!document.getElementById('cadre_energy'); }
  function energyGet(url, gen, onData, onFail) {
    return $.getJSON(url).then(function(d) {
      if (!energyAlive(gen)) return;
      try { onData(d); } catch (e) { console.log('Energie : affichage ' + url, e); }
    }, function(xhr) {
      if (!energyAlive(gen) || !onFail) return;
      try { onFail(xhr); } catch (e) { console.log('Energie : erreur ' + url, e); }
    });
  }
  // Execute les etapes l'une apres l'autre (chacune renvoie une promesse ou
  // rien) : le serveur HTTP du pont a peu de sockets, comme le tunnel de la
  // LiXee-Box (cf. refreshStatusEnergy).
  function runSequence(steps) {
    return steps.reduce(function(p, step) {
      return p.then(function() {
        return Promise.resolve(step()).catch(function() {});
      });
    }, Promise.resolve());
  }
  // Remplace le contenu en gardant ouverts les <details> deja deplies
  function setHtmlKeepDetails($el, html) {
    var open = [];
    $el.find('details').each(function(i) { if (this.open) open.push(i); });
    $el.html(html);
    var $d = $el.find('details');
    open.forEach(function(i) { if ($d[i]) $d[i].open = true; });
  }

  function destroyEnergyCharts() {
    Object.keys(energyCharts).forEach(function(k) {
      if (energyCharts[k]) { try { energyCharts[k].destroy(); } catch (e) {} }
    });
    energyCharts = {};
  }

  function initEnergyPage(time, isHour, gen) {
    // La page a pu etre quittee pendant le chargement des bibliotheques
    if (!energyAlive(gen)) return;

    // Graphiques d'un affichage precedent : liberes avant d'en recreer
    destroyEnergyCharts();
    energyGauges = { sig: '', list: [] };
    energyState = {};
    donutChart = null;
    donutEl = null;

    if (isHour) energyCharts.power = createPowerChart();
    energyCharts.energy = createEnergyChart('energy-chart', { fees: true, palette: ENERGY_COLORS, presence: true });

    // Barres de presence : vue heure (rafraichie chaque minute) et vue jour (5 min)
    presenceState = { period: '', hour: [], day: {} };
    if (time === 'hour' || time === 'day') {
      loadPresence(time, gen);
      pageInterval(function() { loadPresence(time, gen); }, time === 'hour' ? 60000 : 300000);
    }

    // Bascule Wh / euros de la repartition
    markDistButtons();
    $('#energyDistrib [data-dist]').on('click', function() {
      energyDistType = $(this).attr('data-dist') || '';
      markDistButtons();
      loadDistribution(time, gen);
    });

    $('#btn-linky-datas').on('click', function() {
      loadLinkyDatas(gen);
      var el = document.getElementById('linkyModal');
      if (el && window.bootstrap && bootstrap.Modal) bootstrap.Modal.getOrCreateInstance(el).show();
    });

    // Resume, jauges, repartition et graphiques : 15 s en vue heure, 60 s
    // sinon (refreshStatusEnergy / refreshGaugeAbo de la version Arduino).
    // Un cycle encore en cours n'est pas relance.
    var busy = false;
    function cycle() {
      if (busy || !energyAlive(gen)) return;
      busy = true;
      runSequence(energySteps(time, isHour, gen)).then(function() { busy = false; });
    }
    cycle();
    pageInterval(cycle, isHour ? 15000 : 60000);

    // Carte tarif : toutes les 60 s
    loadTariff(gen);
    pageInterval(function() { if (energyAlive(gen)) loadTariff(gen); }, 60000);

    initOtherMeters(time, gen);
  }

  // Etapes d'un cycle ; tout ce qui suit le resume est saute sans ZLinky
  function energySteps(time, isHour, gen) {
    function linky(fn) { return function() { return energyState.configured === false ? null : fn(); }; }
    var steps = [
      function() { return loadSummary(time, gen); },
      linky(function() { return loadGauge(time, isHour, gen); }),
      linky(function() { return loadDistribution(time, gen); }),
      linky(function() { return loadMainEnergyChart(time, gen); })
    ];
    if (isHour) steps.push(linky(function() { return loadPowerChart(gen); }));
    return steps;
  }

  // --- Resume (/api/energy/summary) ---
  function loadSummary(time, gen) {
    return energyGet('/api/energy/summary?time=' + time, gen, function(s) {
      s = s || {};
      if (s.configured === false) {
        energyState = { configured: false };
        showLinkySection(false);
        return;
      }
      energyState = { configured: true, tri: !!s.tri, producer: !!s.producer };
      showLinkySection(true);
      renderEnergyStatus(s);
      renderEnergyLabel(s.label);
      renderBreakdown(s);
      renderTrends(s, time);
    }, function() {
      $('#power_trend_body').html('<p class="text-muted">Données non disponibles</p>');
    });
  }

  function showLinkySection(on) {
    $('#linky-section').toggle(on);
    $('#energy-notconf').toggle(!on);
    if (!on) { $('#energy-status').hide(); $('#energy-alert-row').hide(); }
  }

  function renderEnergyStatus(s) {
    $('#energy-status').toggle(s.connected === false);
    if (s.alert) {
      $('#energyAlert').text('⚠ ' + s.alert);
      $('#energy-alert-row').show();
    } else {
      $('#energy-alert-row').hide();
    }
  }

  function renderEnergyLabel(label) {
    var v = label ? num(label.value) : 0;
    if (!(v > 0)) { $('#label-energy').hide(); return; }
    var cls = String(label['class'] || '').toUpperCase();
    if (!/^[A-G]$/.test(cls)) cls = getEnergyClass(v).cls;
    $('#label-energy').show();
    displayEnergyLabel(cls, v);
  }

  // Tableau de repartition (summary.breakdown), au format de getDatasPower de
  // la LiXee-Box : carre de couleur, energie, cout, puis le total. Le serveur
  // ne transmet que les lignes a afficher (index et sous-compteurs non nuls).
  var BD_ICON_BOLT = "<svg fill='#000000' style='width:16px;' width='24px' height='24px' viewBox='-3.2 -3.2 38.40 38.40' version='1.1' xmlns='http://www.w3.org/2000/svg' stroke='#000000'><g id='SVGRepo_bgCarrier' stroke-width='0'></g><g id='SVGRepo_tracerCarrier' stroke-linecap='round' stroke-linejoin='round' stroke='#CCCCCC' stroke-width='0.384'></g><g id='SVGRepo_iconCarrier'> <path d='M18.605 2.022v0zM18.605 2.022l-2.256 11.856 8.174 0.027-11.127 16.072 2.257-13.043-8.174-0.029zM18.606 0.023c-0.054 0-0.108 0.002-0.161 0.006-0.353 0.028-0.587 0.147-0.864 0.333-0.154 0.102-0.295 0.228-0.419 0.373-0.037 0.043-0.071 0.088-0.103 0.134l-11.207 14.832c-0.442 0.607-0.508 1.407-0.168 2.076s1.026 1.093 1.779 1.099l5.773 0.042-1.815 10.694c-0.172 0.919 0.318 1.835 1.18 2.204 0.257 0.11 0.527 0.163 0.793 0.163 0.629 0 1.145-0.294 1.533-0.825l11.22-16.072c0.442-0.607 0.507-1.408 0.168-2.076-0.34-0.669-1.026-1.093-1.779-1.098l-5.773-0.010 1.796-9.402c0.038-0.151 0.057-0.308 0.057-0.47 0-1.082-0.861-1.964-1.939-1.999-0.024-0.001-0.047-0.001-0.071-0.001v0z'></path> </g></svg>";
  var BD_ICON_COIN = "<svg style='width:16px;' width='24px' height='24px' viewBox='0 0 1024 1024' class='icon' version='1.1' xmlns='http://www.w3.org/2000/svg' fill='#000000'><g id='SVGRepo_bgCarrier' stroke-width='0'/><g id='SVGRepo_tracerCarrier' stroke-linecap='round' stroke-linejoin='round'/><g id='SVGRepo_iconCarrier'><path d='M951.87 253.86c0-82.18-110.05-144.14-256-144.14s-256 61.96-256 144.14c0 0.73 0.16 1.42 0.18 2.14h-0.18v109.71h73.14v-9.06c45.77 25.81 109.81 41.33 182.86 41.33 67.39 0 126.93-13.33 171.71-35.64 6.94 7.18 11.15 14.32 11.15 20.58 0 28.25-72.93 70.98-182.86 70.98h-73.12v73.14h73.12c67.4 0 126.96-13.33 171.74-35.65 6.95 7.17 11.11 14.31 11.11 20.6 0 28.27-72.93 71-182.86 71l-25.89 0.12c-15.91 0.14-31.32 0.29-46.34-0.11l-1.79 73.11c8.04 0.2 16.18 0.27 24.48 0.27 7.93 0 16-0.05 24.2-0.12l25.34-0.12c67.44 0 127.02-13.35 171.81-35.69 6.97 7.23 11.04 14.41 11.04 20.62 0 28.27-72.93 71-182.86 71h-73.12v73.14h73.12c67.44 0 127.01-13.35 171.81-35.69 6.98 7.22 11.05 14.4 11.05 20.62 0 28.27-72.93 71-182.86 71h-73.12v73.14h73.12c145.95 0 256-61.96 256-144.14 0-0.68-0.09-1.45-0.11-2.14h0.11V256h-0.18c0.03-0.72 0.2-1.42 0.2-2.14z m-438.86 0c0-28.27 72.93-71 182.86-71s182.86 42.73 182.86 71c0 28.25-72.93 70.98-182.86 70.98s-182.86-42.73-182.86-70.98z' fill='currentColor'/><path d='M330.15 365.71c-145.95 0-256 61.96-256 144.14 0 0.73 0.16 1.42 0.18 2.14h-0.18v256c0 82.18 110.05 144.14 256 144.14s256-61.96 256-144.14V512h-0.18c0.02-0.72 0.18-1.42 0.18-2.14 0-82.18-110.05-144.15-256-144.15zM147.29 638.93c0-6.32 4.13-13.45 11.08-20.62 44.79 22.33 104.36 35.67 171.78 35.67 67.39 0 126.93-13.33 171.71-35.64 6.94 7.18 11.15 14.32 11.15 20.58 0 28.25-72.93 70.98-182.86 70.98s-182.86-42.72-182.86-70.97z m182.86-200.07c109.93 0 182.86 42.73 182.86 71 0 28.25-72.93 70.98-182.86 70.98s-182.86-42.73-182.86-70.98c0-28.27 72.93-71 182.86-71z m0 400.14c-109.93 0-182.86-42.73-182.86-71 0-6.29 4.17-13.43 11.11-20.6 44.79 22.32 104.34 35.66 171.75 35.66 67.4 0 126.96-13.33 171.74-35.65 6.95 7.17 11.11 14.31 11.11 20.6 0.01 28.26-72.92 70.99-182.85 70.99z' fill='currentColor'/></g></svg>";
  var BD_ICON_SOLAR = "<svg fill='#000000' style='width:16px;' width='24px' height='24px' viewBox='0 -64 640 640' xmlns='http://www.w3.org/2000/svg'><g id='SVGRepo_bgCarrier' stroke-width='0'></g><g id='SVGRepo_tracerCarrier' stroke-linecap='round' stroke-linejoin='round'></g><g id='SVGRepo_iconCarrier'><path d='M431.98 448.01l-47.97.05V416h-128v32.21l-47.98.05c-8.82.01-15.97 7.16-15.98 15.99l-.05 31.73c-.01 8.85 7.17 16.03 16.02 16.02l223.96-.26c8.82-.01 15.97-7.16 15.98-15.98l.04-31.73c.01-8.85-7.17-16.03-16.02-16.02zM585.2 26.74C582.58 11.31 568.99 0 553.06 0H86.93C71 0 57.41 11.31 54.79 26.74-3.32 369.16.04 348.08.03 352c-.03 17.32 14.29 32 32.6 32h574.74c18.23 0 32.51-14.56 32.59-31.79.02-4.08 3.35 16.95-54.76-325.47zM259.83 64h120.33l9.77 96H250.06l9.77-96zm-75.17 256H71.09L90.1 208h105.97l-11.41 112zm16.29-160H98.24l16.29-96h96.19l-9.77 96zm32.82 160l11.4-112h149.65l11.4 112H233.77zm195.5-256h96.19l16.29 96H439.04l-9.77-96zm26.06 256l-11.4-112H549.9l19.01 112H455.33z'></path></g></svg>";
  var BD_ICON_FLAME = "<svg fill='#000000' style='width:16px;' width='24px' height='24px' viewBox='0 0 32 32' version='1.1' xmlns='http://www.w3.org/2000/svg'><g id='SVGRepo_bgCarrier' stroke-width='0'></g><g id='SVGRepo_tracerCarrier' stroke-linecap='round' stroke-linejoin='round'></g><g id='SVGRepo_iconCarrier'> <title>flame-symbol</title> <path d='M10.375 7.562c0 5.625 5.625 6.563 5.625 11.25 0 1.875-1.875 4.687-4.687 4.687s-4.687-2.813-2.813-7.5c-2.813 1.875-3.75 3.75-3.75 5.625 0 4.688 4.687 9.375 11.25 9.375s11.25-2.812 11.25-8.438c0.042-8.32-9.587-11.1-12.188-15-1.875-2.813-0.937-4.688 0.937-6.563-3.75 0.938-5.625 3.563-5.625 6.563v0z'></path> </g></svg>";
  function bdValueKwh(wh) {                                            // getValuekWh
    var n = Math.round(num(wh)), a = Math.abs(n);
    if (a >= 1000000) return (n < 0 ? '-' : '') + (a / 1000000).toFixed(2) + ' MWh';
    if (a >= 1000) return (n < 0 ? '-' : '') + (a / 1000).toFixed(2) + ' kWh';
    return n + ' Wh';
  }
  function renderBreakdown(s) {
    var html = '<h5>R&eacute;partition</h5>';
    arr(s.breakdown).forEach(function(b) {
      b = b || {};
      var icon = b.type === 'prod' ? BD_ICON_SOLAR : (b.type === 'gaz' ? BD_ICON_FLAME : BD_ICON_BOLT);
      // Une ligne par entree (carre, nom, energie, cout) : la grille col-1 / col-11
      // de la Box passait a la ligne dans une carte etroite, et le nom n'etait
      // qu'en infobulle
      html += '<div class="d-flex align-items-center flex-wrap" style="gap:6px;margin-bottom:4px;">' +
                '<span style="flex:0 0 10px;border:1px solid grey;width:10px;height:15px;background-color:' +
                  safeColor(b.color, '#95a5a6') + '"></span>' +
                '<span class="fw-semibold" style="min-width:60px;">' + escHtml(b.name || '') + '</span>' +
                '<span style="white-space:nowrap;">' + icon + ' ' + bdValueKwh(b.wh) + '</span>' +
                '<span style="white-space:nowrap;">' + BD_ICON_COIN + ' ' + num(b.eur).toFixed(2) + ' &euro;</span>' +
              '</div>';
    });
    html += '<br><div class=""><h5>Total</h5>' + BD_ICON_BOLT + ' ' + bdValueKwh(s.totalWh) + '  ' +
            BD_ICON_COIN + '  ' + num(s.totalEur).toFixed(2) + ' &euro; </div>';
    $('#trend-datas').html(html);
  }

  // --- Tendances (reproduit getTrendPower de la LiXee-Box) ---
  function trendIcon(color) {
    return '<svg style="width:36px;flex:0 0 36px;" width="36" height="36" viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg">' +
             '<rect width="100" height="100" rx="20" ry="20" fill="' + color + '"/>' +
             '<path d="M55 10 L30 55 H48 L40 90 L70 45 H52 Z" fill="#FFFFFF"/>' +
           '</svg>';
  }
  function costDetailHtml(c) {
    c = c || {};
    return '<div style="font-size:10px;color:#888;">&Eacute;nergie ' + fmtNum(c.energy, 2) + ' | Taxes ' +
           fmtNum(c.taxes, 2) + ' | Abo. ' + fmtNum(c.abo, 2) + ' &euro;</div>';
  }
  function avgPriceHtml(totalEur, wh, nbDays) {
    if (!(num(wh) > 0)) return '';
    var txt = fmtNum(num(totalEur) / (num(wh) / 1000), 3) + ' &euro;/kWh';
    if (nbDays > 1) txt += ' | ' + fmtNum(num(totalEur) / nbDays, 2) + ' &euro;/j';
    return '<div style="font-size:10px;color:#888;">' + txt + '</div>';
  }
  // Projection fin de mois : projectionEur du serveur si fourni
  function projectionHtml(wh, totalEur, nbDays, projEur) {
    if (nbDays < 2) return '';
    var projWh = num(wh) / nbDays * 30;
    var pe = (projEur != null && isFinite(Number(projEur))) ? Number(projEur) : num(totalEur) / nbDays * 30;
    return '<div style="font-size:10px;color:#666;">Projection : ~' + fmtWh(projWh) + ' / ~' + fmtNum(pe, 2) + ' &euro;</div>';
  }
  // Barre de budget : vert < 75 %, orange < 100 %, rouge au-dela
  function budgetBarHtml(totalEur, budget, pct) {
    budget = num(budget);
    if (!(budget > 0)) return '';
    var p = (pct != null && isFinite(Number(pct))) ? Number(pct) : num(totalEur) / budget * 100;
    var color = p < 75 ? '#27ae60' : (p < 100 ? '#f39c12' : '#e74c3c');
    var w = Math.max(0, Math.min(p, 100));
    return '<div style="margin-top:3px;" title="Budget">' +
             '<div style="background:#eee;border-radius:3px;height:8px;width:100%;">' +
               '<div style="background:' + color + ';border-radius:3px;height:100%;width:' + w.toFixed(0) + '%;"></div>' +
             '</div>' +
             '<span style="font-size:10px;">Budget : ' + fmtNum(totalEur, 2) + ' / ' + fmtNum(budget, 0) + ' &euro; (' + fmtNum(p, 0) + ' %)</span>' +
           '</div>';
  }
  function injectionHtml(wh, eur) {
    if (!(num(wh) > 0)) return '';
    return '<div style="font-size:10px;color:#27ae60;">Production : ' + fmtWh(wh) + ' (' + fmtNum(eur, 2) + ' &euro;)</div>';
  }

  function renderTrends(s, time) {
    var html = '';
    var now = new Date();
    if (s.lastUpdate) {
      html += '<div style="text-align:right;font-size:10px;color:#999;margin-bottom:4px;">Maj : ' + escHtml(s.lastUpdate) + '</div>';
    }
    var budgetM = (s.budget && num(s.budget.monthEur) > 0) ? num(s.budget.monthEur) : 0;

    if (time === 'hour') {
      // Lignes J-1 / Mois / Annee, dans cet ordre (contrat)
      var rows = arr(s.rows);
      if (!rows.length) {
        html += '<p class="text-muted">En attente de données du ZLinky...</p>';
      } else {
        html += '<table style="width:100%;font-size:12px;border-collapse:collapse;margin-top:4px;">' +
                '<tr style="border-bottom:1px solid #eee;"><th></th><th>kWh</th><th>&euro;</th></tr>';
        rows.forEach(function(r, i) {
          r = r || {};
          var c = r.cost || {};
          var days = (i === 1) ? now.getDate() : (i === 2 ? now.getMonth() * 30 + now.getDate() : 0);
          var budget = (i === 0) ? budgetM / 30 : (i === 1 ? budgetM : 0);
          html += '<tr style="border-bottom:1px solid #f5f5f5;">' +
                    '<td style="padding:3px 0;">' + escHtml(r.label || '') + '</td>' +
                    '<td style="padding:3px 4px;">' + fmtKwh(r.wh) + '</td>' +
                    '<td style="padding:3px 4px;">' + fmtNum(c.total, 2) + '</td>' +
                  '</tr>' +
                  '<tr><td colspan="3" style="padding:0;"><details name="td" style="font-size:10px;color:#888;">' +
                    '<summary style="cursor:pointer;">D&eacute;tail</summary>' +
                    costDetailHtml(c) +
                    avgPriceHtml(c.total, r.wh, days) +
                    (i === 1 ? projectionHtml(r.wh, c.total, days, r.projectionEur) : '') +
                    budgetBarHtml(c.total, budget) +
                    injectionHtml(r.prodWh, r.prodEur) +
                  '</details></td></tr>';
        });
        html += '</table>';
      }
    } else {
      // Creneau courant compare au precedent
      var cur = s.current || {}, prev = s.previous || {};
      var cc = cur.cost || {}, pc = prev.cost || {};
      var curWh = num(cur.wh), prevWh = num(prev.wh);
      var diff = curWh - prevWh;
      var color = diff > 0 ? 'orange' : (diff < 0 ? 'green' : 'grey');
      var op = diff > 0 ? '+' : (diff < 0 ? '-' : '');
      var names = TREND_NAMES[time] || ['Actuel', 'Précédent'];
      var nbDays = time === 'month' ? now.getDate() : (time === 'year' ? now.getMonth() * 30 + now.getDate() : 0);

      html += '<div class="d-flex align-items-center gap-2">' + trendIcon(color) + '<div>' +
                '<span style="font-size:18px;font-weight:bold;">' + op + fmtWh(Math.abs(diff)) + '</span>';
      if (s.variationPct != null && isFinite(Number(s.variationPct))) {
        var pct = Number(s.variationPct);
        html += ' <span style="font-size:12px;color:' + color + ';">(' + (pct >= 0 ? '+' : '') + fmtNum(pct, 1) + ' %)</span>';
      }
      html += '<br><span style="font-size:12px;color:#888;">Min : ' + fmtWh(s.minWh) + ' | Max : ' + fmtWh(s.maxWh) + '</span>' +
              '</div></div>';
      html += '<div style="font-size:12px;margin-top:4px;">' + escHtml(names[0]) + ' : ' + fmtWh(curWh) + '</div>' +
              '<div style="font-size:12px;color:#888;">' + escHtml(names[1]) + ' : ' + fmtWh(prevWh) + ' (' + fmtNum(pc.total, 2) + ' &euro;)</div>';

      // Cout principal + detail repliable
      html += '<div style="font-size:20px;font-weight:bold;margin:4px 0;">' + fmtNum(cc.total, 2) + ' &euro;</div>';
      html += '<details name="td" style="font-size:10px;color:#888;margin-bottom:4px;">' +
                '<summary style="cursor:pointer;font-size:11px;">D&eacute;tail co&ucirc;t</summary>' +
                costDetailHtml(cc) +
                avgPriceHtml(cc.total, curWh, nbDays) +
                (time === 'month' ? projectionHtml(curWh, cc.total, nbDays, cur.projectionEur) : '') +
                injectionHtml(cur.prodWh, cur.prodEur) +
              '</details>';
      if (s.budget) html += budgetBarHtml(cc.total, s.budget.periodEur, s.budget.pct);
    }
    setHtmlKeepDetails($('#power_trend_body'), html);
  }

  // En-tete des tendances en vue heure : statistiques de puissance (power.stats)
  function renderPowerTrendHead(st, priceKwh, cspe, aboDay) {
    var $h = $('#power_trend_head');
    if (!st || (st.last == null && st.trend == null)) { $h.empty(); return; }
    var tr = num(st.trend);
    var color = tr > 0 ? 'orange' : (tr < 0 ? 'green' : 'grey');
    var html = '<div class="d-flex align-items-center gap-2 mb-1">' + trendIcon(color) + '<div>' +
                 '<span style="font-size:20px;font-weight:bold;">' + (tr > 0 ? '+' : '') + fmtNum(tr, 0) + ' VA</span>' +
                 '<br><span style="font-size:12px;color:#888;">Min : ' + fmtNum(st.min, 0) + ' | Max : ' + fmtNum(st.max, 0) + ' VA</span>';
    // Equivalent journalier de la derniere puissance mesuree
    if (num(st.last) > 0) {
      var kwhDay = num(st.last) * 24 / 1000;
      html += '<br><span style="font-size:12px;color:#666;">&asymp; ' + fmtNum(kwhDay, 1) + ' kWh/j' +
              (priceKwh > 0 ? ' (~' + fmtNum(kwhDay * (priceKwh + (cspe || 0)) + (aboDay || 0), 2) + ' &euro;/j)' : '') + '</span>';
    }
    html += '</div></div>';
    $h.html(html);
  }

  // --- Jauges (/api/energy/gauge) ---
  function loadGauge(time, isHour, gen) {
    return energyGet('/api/energy/gauge?time=' + time, gen, function(g) { renderGauges(g || {}, isHour); }, function() {
      // Jauges deja construites : gardees telles quelles jusqu'a la prochaine reponse
      if (!energyGauges.sig) $('#energy-gauges').html('<div class="col-12 text-center text-muted">Données non disponibles</div>');
    });
  }

  function gaugeText(val, unit) {
    return (unit === 'VA' || unit === 'W' ? fmtNum(val, 0) : fmtNum(val, 2)) + ' ' + unit;
  }

  function renderGauges(g, isHour) {
    if (g.configured === false) return;
    var specs = [];
    if (isHour) {
      // Une jauge par phase (3 en triphase) + injection si production
      var phases = arr(g.phases);
      var unitH = g.unit || 'VA';
      phases.slice(0, 3).forEach(function(p, i) {
        p = p || {};
        specs.push({ title: p.label || ('Soutirée' + (phases.length > 1 ? ' P.' + (i + 1) : '')),
                     value: p.value, max: p.max || g.goal, unit: unitH,
                     label: (i === 0 && g.powerW != null) ? fmtNum(g.powerW, 0) + ' W' : '' });
      });
      if (g.prod) {
        specs.push({ title: g.prod.label || 'Injectée', value: g.prod.value,
                     max: g.prod.max || g.goal, unit: unitH, label: '', sectors: true });
      }
    } else {
      // Energie du creneau courant
      var unitP = g.unit || 'kWh';
      var c = g.conso || {};
      specs.push({ title: 'Consommation', value: c.value, max: c.max, unit: unitP, label: '' });
      if (g.prod && (energyState.producer || num(g.prod.max) > 0 || num(g.prod.value) > 0)) {
        specs.push({ title: 'Production', value: g.prod.value, max: g.prod.max, unit: unitP, label: '' });
      }
    }

    var $row = $('#energy-gauges');
    if (!specs.length) {
      $row.html('<div class="col-12 text-center text-muted">Aucune mesure</div>');
      energyGauges = { sig: '', list: [] };
      return;
    }

    // Recreation seulement si la composition change (triphase, production...)
    var sig = specs.map(function(s) { return s.title + '/' + s.unit; }).join('|');
    var rebuild = (sig !== energyGauges.sig);
    if (rebuild) {
      $row.empty();
      energyGauges = { sig: sig, list: [] };
      var colCls = specs.length > 1 ? 'col-12 col-md-6 col-lg-6 mb-3' : 'col-12 mb-3';
      specs.forEach(function(s, i) {
        $row.append($('<div style="text-align:center;"></div>').addClass(colCls).append(
          $('<h5></h5>').text(s.title),
          $('<div class="w-100"></div>').attr('id', 'energy-gauge-' + i)
        ));
      });
    }

    specs.forEach(function(s, i) {
      var v = Math.max(0, num(s.value));
      var m = num(s.max);
      if (m < v) m = v;
      if (!(m > 0)) m = isHour ? 1000 : 1;
      var gauge = energyGauges.list[i];
      if (rebuild && typeof JustGage !== 'undefined') {
        try {
          gauge = new JustGage({
            id: 'energy-gauge-' + i,
            value: v, min: 0, max: m,
            label: s.label,
            customSectors: s.sectors ? { percents: true, ranges: [
              { color: '#ff3b30', lo: 0, hi: 30 }, { color: '#f39c12', lo: 31, hi: 60 },
              { color: '#43bf58', lo: 61, hi: 100 }] } : undefined,
            gaugeWidthScale: 0.6,
            pointer: true,
            textRenderer: (function(unit) { return function(val) { return gaugeText(val, unit); }; })(s.unit),
            pointerOptions: { toplength: -15, bottomlength: 10, bottomwidth: 12, color: '#8e8e93',
                              stroke: '#ffffff', stroke_width: 3, stroke_linecap: 'round' },
            relativeGaugeSize: true,
            refreshAnimationTime: 1000
          });
        } catch (e) { console.log('JustGage init error:', e); gauge = null; }
        energyGauges.list[i] = gauge;
        if (gauge) return;
      }
      if (gauge) {
        // refresh(valeur, max, min, libelle)
        try { gauge.refresh(v, m, 0, s.label); } catch (e) {}
      } else {
        // Sans JustGage : valeur en texte
        $('#energy-gauge-' + i).html('<div style="font-size:24px;font-weight:bold;">' + escHtml(gaugeText(v, s.unit)) + '</div>' +
          '<div class="text-muted small">max ' + escHtml(gaugeText(m, s.unit)) + (s.label ? ' | ' + escHtml(s.label) : '') + '</div>');
      }
    });
  }

  // --- Repartition (/api/energy/distribution) ---
  function markDistButtons() {
    $('#energyDistrib [data-dist]').each(function() {
      $(this).toggleClass('active', ($(this).attr('data-dist') || '') === energyDistType);
    });
  }

  function loadDistribution(time, gen) {
    var url = '/api/energy/distribution?time=' + time + (energyDistType === 'euro' ? '&type=euro' : '');
    return energyGet(url, gen, renderDonut, function() { renderDonut([]); });
  }

  function renderDonut(data) {
    var el = document.getElementById('donut-chart');
    if (!el || typeof Morris === 'undefined') return;
    var euro = (energyDistType === 'euro');
    var rows = arr(data).filter(function(d) { return d && num(d.value) > 0; }).map(function(d) {
      return { label: String(d.label == null ? '' : d.label), value: num(d.value),
               unit: d.unit || (euro ? '€' : 'Wh'), color: safeColor(d.color, null) };
    });
    donutEmpty = !rows.length;
    if (donutEmpty) rows = [{ label: 'Aucune donnée', value: 1, unit: '', color: '#cccccc' }];
    var colors = rows.map(function(r, i) { return r.color || DONUT_COLORS[i % DONUT_COLORS.length]; });

    if (donutChart && donutEl === el) {
      donutChart.options.colors = colors;
      donutChart.options.showPercentage = !donutEmpty;
      donutChart.setData(rows);
      return;
    }
    $(el).empty();
    try {
      donutChart = Morris.Donut({
        element: 'donut-chart',
        data: rows,
        colors: colors,
        formatter: function(value, d) {
          if (donutEmpty) return '';
          return (d && d.unit === '€' ? fmtNum(value, 2) : fmtNum(value, 0)) + ' ' + ((d && d.unit) || '');
        },
        resize: true,
        animate: false,
        showPercentage: !donutEmpty
      });
      donutEl = el;
    } catch (e) { console.log('Morris.Donut init error:', e); donutChart = null; }
  }

  // --- Graphiques Chart.js ---
  // Remplace les series en conservant celles masquees par l'utilisateur (legende)
  function setChartData(chart, labels, datasets) {
    if (!chart) return;
    var hidden = {};
    (chart.data.datasets || []).forEach(function(ds, i) {
      if (!chart.isDatasetVisible(i)) hidden[ds.label] = true;
    });
    datasets.forEach(function(ds) { if (hidden[ds.label]) ds.hidden = true; });
    chart.data.labels = labels;
    chart.data.datasets = datasets;
    chart.update('none');
  }
  function refLine(label, value, count, color, dash) {
    var data = [];
    for (var i = 0; i < count; i++) data.push(value);
    // Pile a part : la ligne ne s'empile pas sur les barres
    return { type: 'line', label: label, data: data, borderColor: color, backgroundColor: color,
             borderWidth: 2, borderDash: dash, pointRadius: 0, pointHoverRadius: 0, fill: false,
             stack: 'ref_' + label, lixee: { ref: true } };
  }
  function tooltipKeep(item) {
    var meta = item.dataset.lixee || {};
    return !meta.ref && item.parsed.y != null && Number(item.parsed.y) !== 0;
  }

  // Puissance apparente des dernieres 24 h (createPowerGraph)
  function createPowerChart() {
    var canvas = document.getElementById('power-chart');
    if (!canvas || typeof Chart === 'undefined') return null;
    return new Chart(canvas.getContext('2d'), {
      type: 'bar',
      data: { labels: [], datasets: [] },
      options: {
        responsive: true, maintainAspectRatio: false, animation: false,
        interaction: { mode: 'index', intersect: false },
        datasets: { bar: { barPercentage: 0.9, categoryPercentage: 0.95 } },
        scales: {
          x: { stacked: true, ticks: { maxRotation: 70, minRotation: 70, autoSkip: true, maxTicksLimit: 20 } },
          y: { stacked: true, beginAtZero: false, suggestedMax: 100,
               ticks: { callback: function(v) { return fmtNum(v, 0) + ' VA'; } } }
        },
        plugins: {
          legend: { display: true, position: 'top' },
          zoom: {
            limits: { x: { min: 'original', max: 'original', minRange: 3 } },
            pan: { enabled: true, mode: 'x', modifierKey: 'ctrl' },
            zoom: { wheel: { enabled: true, speed: 0.1 }, pinch: { enabled: true }, mode: 'x', drag: { enabled: true } }
          },
          tooltip: {
            enabled: false,
            backgroundColor: 'rgba(133, 133, 133, 0.85)',
            filter: tooltipKeep,
            callbacks: {
              label: function(ctx) { return ctx.dataset.label + ' : ' + fmtNum(Math.abs(ctx.parsed.y), 0) + ' VA'; },
              footer: function(items) { return powerTooltipFooter(this.chart, items); }
            }
          }
        },
        // Infobulle affichee / masquee au clic, comme la LiXee-Box
        onClick: function(evt, active) {
          var chart = this, tt = chart.options.plugins.tooltip;
          if (active.length && !tt.enabled) {
            tt.enabled = true;
            chart.tooltip.setActiveElements(active, { x: evt.x, y: evt.y });
          } else {
            tt.enabled = false;
            chart.tooltip.setActiveElements([], { x: 0, y: 0 });
          }
          chart.update();
        },
        onHover: function(evt, active) {
          if (evt.native && evt.native.target) evt.native.target.style.cursor = active.length ? 'pointer' : 'default';
        }
      }
    });
  }

  // Pied d'infobulle : injection, consommation, net ou total, part de la
  // puissance souscrite et marge (getPowerTooltipFooter de la LiXee-Box)
  function powerTooltipFooter(chart, items) {
    if (!items || !items.length) return '';
    var inj = 0, cons = 0, injD = [], consD = [];
    var tri = chart && chart.lixeeTri;
    items.forEach(function(it) {
      var meta = it.dataset.lixee || {};
      if (meta.ref) return;
      var v = num(it.parsed.y);
      if (!v) return;
      var a = Math.abs(v), lbl = String(it.dataset.label || '');
      if (lbl.indexOf('Injection') === 0) { inj += a; injD.push([lbl.replace('Injection ', ''), a]); }
      else { cons += a; consD.push([lbl.replace('Puissance ', ''), a]); }
    });
    var L = [TOOLTIP_SEP];
    if (inj > 0) {
      L.push('Injection : ' + fmtNum(inj, 0) + ' VA');
      if (tri) injD.forEach(function(d) { L.push('   • ' + d[0] + ' : ' + fmtNum(d[1], 0) + ' VA'); });
    }
    if (cons > 0) {
      L.push('Consommation : ' + fmtNum(cons, 0) + ' VA');
      if (tri) consD.forEach(function(d) { L.push('   • ' + d[0] + ' : ' + fmtNum(d[1], 0) + ' VA'); });
    }
    L.push(TOOLTIP_SEP);
    if (inj > 0 && cons > 0) L.push('Puissance nette : ' + fmtNum(cons - inj, 0) + ' VA');
    else L.push('Total : ' + fmtNum(inj + cons, 0) + ' VA');
    var goal = num(chart && chart.lixeeGoal);
    if (goal > 0 && cons > 0) {
      L.push('Utilisation : ' + fmtNum(cons / goal * 100, 1) + ' % de la limite');
      if (cons > goal) L.push('Dépassement : +' + fmtNum(cons - goal, 0) + ' VA');
      else L.push('Marge : ' + fmtNum(goal - cons, 0) + ' VA');
    }
    return L;
  }

  function loadPowerChart(gen) {
    return energyGet('/api/energy/power', gen, function(p) {
      p = p || {};
      fillPowerChart(energyCharts.power, p);
      renderPowerTrendHead(p.stats, num(p.priceKwh), num(p.cspe), num(p.aboDay));
    });
  }

  function fillPowerChart(chart, p) {
    if (!chart) return;
    var labels = arr(p.labels).map(String);
    var cons = arr(p.consumption), inj = arr(p.injection);
    var tri = (p.tri != null) ? !!p.tri : cons.length > 1;
    var BLUES = ['#1e88e5', '#5dade2', '#85c1e9'], GREENS = ['#27ae60', '#0ed160', '#08612d'];
    // null = minute sans mesure ; injection affichee en negatif
    function clean(a, sign) {
      return arr(a).map(function(v) {
        var n = Number(v);
        return (v == null || !isFinite(n)) ? null : sign * Math.abs(n);
      });
    }
    var sets = [];
    inj.slice(0, 3).forEach(function(a, i) {
      var data = clean(a, -1);
      if (!data.some(function(v) { return v; })) return;     // injection vide : omise
      sets.push({ label: tri ? 'Injection Ph' + (i + 1) : 'Injection', data: data,
                  backgroundColor: GREENS[i], stack: 'Stack0' });
    });
    cons.slice(0, 3).forEach(function(a, i) {
      sets.push({ label: tri ? 'Puissance Ph' + (i + 1) : 'Puissance', data: clean(a, 1),
                  backgroundColor: BLUES[i], stack: 'Stack0' });
    });
    var goal = num(p.goal);
    chart.lixeeGoal = goal;
    chart.lixeeTri = tri;
    if (goal > 0 && labels.length) {
      sets.push(refLine('Puiss. souscrite : ' + fmtNum(goal, 0) + ' VA', goal, labels.length, 'rgb(255, 0, 0)', [5, 5]));
    }
    setChartData(chart, labels, sets);
  }

  // Energie par creneau (createEnergyGraph) ; opts.fees : abonnement et taxes
  // electricite dans le pied d'infobulle (ZLinky uniquement)
  function createEnergyChart(canvasId, opts) {
    var canvas = document.getElementById(canvasId);
    if (!canvas || typeof Chart === 'undefined') return null;
    opts = opts || {};
    // Cree avant le graphique : les callbacks servent des la construction
    var info = { unit: 'Wh', qtyScale: 0.001, costs: null, period: 'hour',
                 fees: !!opts.fees, palette: opts.palette || ENERGY_COLORS };
    var chart = new Chart(canvas.getContext('2d'), {
      type: 'bar',
      data: { labels: [], datasets: [] },
      options: {
        responsive: true, maintainAspectRatio: false, animation: false,
        interaction: { mode: 'index', intersect: false },
        datasets: { bar: { barPercentage: 0.9, categoryPercentage: 0.95 } },
        scales: {
          x: { stacked: true, ticks: { maxRotation: 70, minRotation: 70, autoSkip: true } },
          y: { stacked: true, beginAtZero: true, grace: '10%',
               ticks: { callback: function(v) { return fmtQty(v) + ' ' + info.unit; } } }
        },
        plugins: {
          legend: { display: true, position: 'top' },
          tooltip: {
            filter: tooltipKeep,
            callbacks: {
              label: function(ctx) { return energyTooltipLabel(chart, ctx); },
              footer: function(items) { return energyTooltipFooter(chart, items); }
            }
          }
        }
      },
      plugins: opts.presence ? [PRESENCE_OVERLAY] : []
    });
    chart.lixeeInfo = info;
    return chart;
  }

  // Barres de presence (getPresenceJavaScript de la LiXee-Box) : sous les
  // barres d'energie, une bande verte pale et un point vert en haut de chaque
  // creneau (heure, ou jour) ou une presence a ete detectee
  var presenceState = { period: '', hour: [], day: {} };
  var PRESENCE_OVERLAY = {
    id: 'presenceOverlay',
    beforeDraw: function(c) {
      var ca = c.chartArea, p = presenceState.period;
      if (!ca || (p !== 'hour' && p !== 'day')) return;
      var l = c.data.labels || [];
      if (!l.length) return;
      var bw = (ca.right - ca.left) / l.length, ctx = c.ctx;
      ctx.save();
      for (var i = 0; i < l.length; i++) {
        var on;
        if (p === 'hour') on = presenceState.hour.length === 24 && i < 24 && presenceState.hour[i] === 1;
        else {
          var dy = parseInt(String(l[i]).split('/')[0], 10);
          on = !isNaN(dy) && dy >= 1 && dy <= 31 && presenceState.day[dy] === 1;
        }
        if (!on) continue;
        var x = ca.left + i * bw + bw / 2;
        ctx.fillStyle = 'rgba(76,175,80,0.15)';
        ctx.fillRect(x - bw / 2, ca.top, bw, ca.bottom - ca.top);
        ctx.fillStyle = 'rgba(76,175,80,0.8)';
        ctx.beginPath();
        ctx.arc(x, ca.top + 8, 4, 0, 6.28);
        ctx.fill();
      }
      ctx.restore();
    }
  };

  // Presence de la periode (vues heure et jour seulement), redessinee sans animation
  function loadPresence(time, gen) {
    return energyGet('/api/energy/presence?time=' + time, gen, function(d) {
      d = d || {};
      presenceState = { period: d.enabled ? time : '', hour: arr(d.hour), day: d.day || {} };
      var ch = energyCharts.energy;
      if (ch) { try { ch.update('none'); } catch (e) {} }
    });
  }

  function loadMainEnergyChart(time, gen) {
    return energyGet('/api/energy/series?time=' + time, gen, function(d) {
      fillEnergyChart(energyCharts.energy, d, time);
    });
  }

  function fillEnergyChart(chart, d, period) {
    if (!chart) return;
    d = d || {};
    var info = chart.lixeeInfo;
    var unit = d.unit || 'Wh';
    var coeff = Number(d.coeff);
    if (!(coeff > 0)) coeff = 1;
    var disp = d.displayUnit || unit;
    // Valeur affichee = brute x coeff ; Wh affiches en kWh si displayUnit le demande
    var scale = coeff;
    if (unit === 'Wh' && disp === 'kWh' && coeff === 1 && d.device !== 'meter') scale = 0.001;   // ZLinky seulement
    info.unit = disp;
    // Quantite facturee : kWh pour l'energie, unite affichee sinon (m3, L)
    info.qtyScale = (disp === 'Wh') ? 0.001 : 1;
    info.costs = d.costs || null;
    info.period = d.period || period;

    var labels = arr(d.labels).map(String);
    var prodSets = [], consoSets = [];
    arr(d.series).forEach(function(s, i) {
      if (!s || !$.isArray(s.data)) return;
      var isProd = (s.type === 'prod');
      var data = s.data.map(function(v) {
        var n = Number(v);
        if (v == null || !isFinite(n)) return null;
        n = Math.abs(n) * scale;
        return isProd ? -n : n;
      });
      if (!data.some(function(v) { return v; })) return;       // serie vide : omise
      var name = String(s.name || s.key || ('Série ' + (i + 1)));
      var ds = {
        label: isProd ? name + ' (Prod)' : name,
        data: data,
        backgroundColor: isProd ? safeColor(s.color, '#27ae60') : safeColor(s.color, info.palette[i % info.palette.length]),
        stack: 'Stack0',
        lixee: { price: num(s.price), prices: $.isArray(s.prices) ? s.prices : null, prod: isProd }
      };
      (isProd ? prodSets : consoSets).push(ds);
    });
    // Production d'abord (sous l'axe), puis consommation
    var datasets = prodSets.concat(consoSets);
    var budget = num(d.budgetWh) * (disp === 'kWh' ? 0.001 : 1);
    if (budget > 0 && labels.length) {
      datasets.push(refLine('Budget', budget, labels.length, '#8e44ad', [6, 4]));
    }
    setChartData(chart, labels, datasets);
  }

  // Prix du kWh d'un point : prix moyen du creneau pour un sous-compteur
  function slotPrice(meta, i) {
    if (meta.prices && meta.prices[i] != null && isFinite(Number(meta.prices[i]))) return Number(meta.prices[i]);
    return num(meta.price);
  }

  function energyTooltipLabel(chart, ctx) {
    var info = chart.lixeeInfo, meta = ctx.dataset.lixee || {};
    var costs = info.costs || {};
    var v = Math.abs(num(ctx.parsed.y));
    var line = ctx.dataset.label + ' : ' + fmtQty(v) + ' ' + info.unit;
    var price = slotPrice(meta, ctx.dataIndex);
    if (price > 0) {
      var q = v * info.qtyScale;
      var cost = q * price;
      // Accise ajoutee a la consommation electrique seulement
      if (!meta.prod && info.fees && num(costs.cspe) > 0) cost += q * num(costs.cspe);
      line += ' (' + fmtEur(cost) + ')';
    }
    return line;
  }

  // Pied d'infobulle : cout de la consommation (energie + abonnement x f +
  // accise + CTA x f), revenu de la production et bilan net (functions.js)
  function energyTooltipFooter(chart, items) {
    if (!items || !items.length) return '';
    var info = chart.lixeeInfo, costs = info.costs || {};
    var f = num(costs.periodFactor);
    if (!(f > 0)) f = PERIOD_FACTORS[info.period] || 0;
    var u = ' ' + info.unit;
    var prodQty = 0, consoQty = 0, prodEur = 0, consoEnergy = 0, consoTax = 0;
    items.forEach(function(it) {
      var meta = it.dataset.lixee || {};
      if (meta.ref) return;
      var v = num(it.parsed.y);
      if (!v) return;
      var a = Math.abs(v), q = a * info.qtyScale;
      if (v < 0) {
        prodQty += a;
        prodEur += q * (meta.price || num(costs.prodPrice));
      } else {
        consoQty += a;
        consoEnergy += q * slotPrice(meta, it.dataIndex);
        if (info.fees) consoTax += q * num(costs.cspe);
      }
    });
    // Abonnement et CTA : une seule fois pour le creneau
    var abo = 0;
    if (consoQty > 0 && info.fees) {
      abo = num(costs.abo) * f;
      consoTax += num(costs.cta) * f;
    }
    var consoTotal = consoEnergy + abo + consoTax;
    var lines = [TOOLTIP_SEP];
    function details(energyLabel, energy) {
      if (energy) lines.push('   • ' + energyLabel + ' : ' + fmtEur(energy));
      if (abo > 0) lines.push('   • Abonnement : ' + fmtEur(abo));
      if (consoTax > 0) lines.push('   • Taxes : ' + fmtEur(consoTax));
    }

    if (prodQty > 0) {
      lines.push('Production : ' + fmtQty(prodQty) + u);
      if (prodEur > 0) lines.push('   Revenu : ' + fmtEur(prodEur));
    }
    if (consoQty > 0) {
      lines.push('Consommation : ' + fmtQty(consoQty) + u);
      if (consoTotal > 0) {
        lines.push('   Coût : ' + fmtEur(consoTotal));
        details('Énergie', consoEnergy);
      }
    }
    if (prodQty > 0 && consoQty > 0) {
      lines.push(TOOLTIP_SEP);
      lines.push('Net : ' + fmtQty(consoQty - prodQty) + u);
      lines.push('   Facture nette : ' + fmtEur(consoTotal - prodEur));
      details('Énergie nette', consoEnergy - prodEur);
    } else if (consoQty > 0) {
      lines.push(TOOLTIP_SEP);
      lines.push('Total : ' + fmtQty(consoQty) + u + ' | ' + fmtEur(consoTotal));
    } else if (prodQty > 0) {
      lines.push(TOOLTIP_SEP);
      lines.push('Total : ' + fmtQty(prodQty) + u + ' | Revenu ' + fmtEur(prodEur));
    }
    return lines;
  }

  // --- Gaz et eau : seulement si configures (/api/config/general Gaz / Water) ---
  function initOtherMeters(time, gen) {
    $.getJSON('/api/config/general').done(function(cfg) {
      if (!energyAlive(gen)) return;
      cfg = cfg || {};
      var meters = [];
      if (cfg.Gaz)   meters.push({ key: 'gaz',   ieee: String(cfg.Gaz),   title: 'Consommation de Gaz', palette: ['#e67e22'] });
      if (cfg.Water) meters.push({ key: 'water', ieee: String(cfg.Water), title: 'Consommation d\'eau', palette: ['#2e86c1'] });
      if (!meters.length) return;
      var $box = $('#energy-meters');
      meters.forEach(function(m) {
        var q = 'time=' + time + '&ieee=' + encodeURIComponent(m.ieee);
        $box.append(
          '<div class="col-md-6">' +
            '<div class="card p-4">' +
              '<h5 class="card-title">' + escHtml(m.title) + '</h5>' +
              '<div class="card-body"><div style="position:relative;height:342px;"><canvas id="' + m.key + '-chart"></canvas></div></div>' +
              '<a href="/api/energy/chart/export?' + escHtml(q) + '" download class="position-absolute bottom-0 end-0 p-2 text-muted" title="Exporter en CSV">' + ENERGY_CSV_ICON + '</a>' +
            '</div>' +
          '</div>');
        m.url = '/api/energy/series?' + q;
        energyCharts[m.key] = createEnergyChart(m.key + '-chart', { fees: false, palette: m.palette });
      });
      function refresh() {
        if (!energyAlive(gen)) return;
        runSequence(meters.map(function(m) {
          return function() {
            return energyGet(m.url, gen, function(d) { fillEnergyChart(energyCharts[m.key], d, time); });
          };
        }));
      }
      refresh();
      pageInterval(refresh, 60000);
    });
  }

  // --- Carte tarif (/api/energy/tariff) ---
  function setTempoBadge(sel, day, def) {
    day = day || {};
    var cls = /^tempo-[a-z]+$/.test(String(day['class'] || '')) ? day['class'] : 'tempo-undef';
    $(sel).attr('class', 'tempo-badge ' + cls).attr('title', day.label || '').text(day.icon || TEMPO_ICONS[cls] || def);
  }

  function loadTariff(gen) {
    return energyGet('/api/energy/tariff', gen, function(d) {
      d = d || {};
      if (!d.show) { $('#tariff-section').hide(); return; }
      $('#tariff-name').text(d.tariff || '---');
      $('#tariff-card').css('background', safeColor(d.bgColor, '#2980b9'));
      var price = num(d.price);
      $('#tariff-price').text(price > 0 ? fmtNum(price, 4) + ' €/kWh' : '').toggle(price > 0);
      $('#tempo-section').toggle(!!d.isTempo);
      if (d.isTempo) {
        setTempoBadge('#tempo-today', d.today, '?');
        setTempoBadge('#tempo-tomorrow', d.tomorrow, '-');
      }
      $('#tariff-section').show();
    }, function() { $('#tariff-section').hide(); });
  }

  // --- Fenetre "Donnees Linky" (/api/energy/linky) ---
  function loadLinkyDatas(gen) {
    var $b = $('#power_data').html('<div class="text-center p-3"><div class="spinner-border" role="status"></div></div>');
    energyGet('/api/energy/linky', gen, function(d) {
      var items = arr(d && d.items);
      if (!items.length) { $b.html('<p class="text-muted mb-0">Aucune donnée disponible</p>'); return; }
      var $tb = $('<tbody></tbody>');
      items.forEach(function(it) {
        it = it || {};
        $tb.append($('<tr></tr>').append(
          $('<td></td>').text(it.name || it.key || ''),
          $('<td class="text-end fw-bold"></td>').text(it.value == null ? '' : String(it.value)),
          $('<td></td>').text(it.unit || '')
        ));
      });
      $b.empty().append($('<table class="table table-sm table-striped mb-0"></table>').append(
        '<thead><tr><th>Donn&eacute;e</th><th class="text-end">Valeur</th><th>Unit&eacute;</th></tr></thead>', $tb));
      // Registre de statuts STGE decode (mode standard)
      var sg = arr(d && d.stge);
      if (sg.length) {
        var $sb = $('<tbody></tbody>');
        sg.forEach(function(it) {
          $sb.append($('<tr></tr>').append($('<td></td>').text(it.name || ''),
                                           $('<td class="text-end fw-bold"></td>').text(it.value || '')));
        });
        $b.append('<h6 class="mt-3">Registre de statuts (STGE)</h6>',
                  $('<table class="table table-sm table-striped mb-0"></table>').append($sb));
      }
    }, function(xhr) {
      $b.empty().append($('<div class="alert alert-danger mb-0"></div>').text(apiErrorText(xhr)));
    });
  }

  // ============================================================================
  // PAGE: Appareils (Devices)
  // ============================================================================
  function pageDevices() {
    setContent(
      '<div class="container py-4">' +
        '<h4>Appareils</h4>' +
        '<div id="devices-list"><div class="text-center p-3"><div class="spinner-border" role="status"></div></div></div>' +
      '</div>'
    );
    $.getJSON('/api/zigbee/devices', function(devices) {
      if (!devices || devices.length === 0) {
        $('#devices-list').html('<div class="alert alert-info">Aucun appareil Zigbee appair&eacute;.</div>');
        return;
      }
      var html = '<div class="row g-3">';
      for (var i = 0; i < devices.length; i++) {
        var d = devices[i];
        var icon = 'img/icon_' + d.deviceId + '.png';
        var modelIcon = d.model ? 'img/icon_' + d.model + '.png' : '';
        html += '<div class="col-md-6 col-lg-4">' +
          '<div class="card p-3">' +
            '<div class="d-flex align-items-center">' +
              '<img src="' + (modelIcon || icon) + '" width="48" height="48" class="me-3" onerror="this.src=\'img/icon_97.png\'">' +
              '<div>' +
                '<strong>' + (d.alias || d.model || 'Device') + '</strong><br>' +
                '<small class="text-muted">IEEE: ' + d.ieee + '</small><br>' +
                '<small class="text-muted">Addr: 0x' + d.shortAddr.toString(16).toUpperCase() + '</small>' +
                (d.matterEndpoint > 0 ? '<br><small class="badge bg-primary">Matter EP ' + d.matterEndpoint + '</small>' : '') +
              '</div>' +
            '</div>' +
            '<div class="mt-2 d-flex justify-content-between align-items-center">' +
              '<span class="badge ' + (d.active ? 'bg-success' : 'bg-secondary') + '">' + (d.active ? 'Actif' : 'Inactif') + '</span>' +
              '<small>LQI: ' + d.lqi + '</small>' +
              '<button class="btn btn-sm btn-outline-danger" onclick="removeDevice(\'' + d.ieee + '\')">Supprimer</button>' +
            '</div>' +
          '</div>' +
        '</div>';
      }
      html += '</div>';
      $('#devices-list').html(html);
    }).fail(function() {
      $('#devices-list').html('<div class="alert alert-danger">Erreur de chargement des appareils.</div>');
    });
  }

  window.removeDevice = function(ieee) {
    if (!confirm('Supprimer cet appareil ?')) return;
    $.ajax({
      url: '/api/zigbee/remove',
      method: 'POST',
      contentType: 'application/json',
      data: JSON.stringify({ieee: ieee}),
      success: function() { showAlert('Appareil supprim&eacute;'); pageDevices(); },
      error: function() { showAlert('Erreur', 'danger'); }
    });
  };

  // ============================================================================
  // Section WiFi de la page LAN : etat, provisioning BLE, identifiants
  // ============================================================================
  function wifiSectionHtml() {
    return '' +
        // Mode "Ethernet seul" : le WiFi n'est pas demarre (rempli par loadEthernetCard)
        '<div id="wifi-disabled-note" class="alert alert-secondary small" style="display:none;"></div>' +

        // Etat actuel
        '<div class="card p-4 mb-4">' +
          '<h5>WiFi</h5>' +
          '<div id="wifi-info"><div class="text-center"><div class="spinner-border" role="status"></div></div></div>' +
        '</div>' +

        // BLE Provisioning info
        '<div class="card p-4 mb-4" id="wifi-ble-card" style="display:none;">' +
          '<h5>Provisioning Bluetooth</h5>' +
          '<div id="wifi-ble-info"></div>' +
        '</div>' +

        // Formulaire
        '<div class="card p-4">' +
          '<h5>Param&egrave;tres WiFi</h5>' +
          '<form id="wifi-form">' +
            // SSID + Scan
            '<div class="mb-3">' +
              '<label class="form-label">SSID</label>' +
              '<div class="input-group">' +
                '<input type="text" class="form-control" id="wifi-ssid" maxlength="32" required>' +
                '<button class="btn btn-primary" type="button" id="wifi-scan-btn">Scan</button>' +
              '</div>' +
              '<div id="wifi-networks" class="mt-2" style="display:none;"></div>' +
            '</div>' +

            // Password
            '<div class="mb-3"><label class="form-label">Mot de passe</label>' +
              '<div class="input-group">' +
                '<input type="password" class="form-control" id="wifi-pass" maxlength="64">' +
                '<button class="btn btn-outline-secondary" type="button" id="wifi-pass-toggle">Voir</button>' +
              '</div>' +
              '<div class="form-text">Laisser vide pour conserver le mot de passe actuel (m&ecirc;me r&eacute;seau).</div>' +
            '</div>' +

            // Adressage : DHCP ou IP fixe
            '<div class="form-check form-switch mb-2">' +
              '<input class="form-check-input" type="checkbox" id="wifi-dhcp" checked>' +
              '<label class="form-check-label" for="wifi-dhcp">DHCP (adresse automatique)</label>' +
            '</div>' +
            '<div id="wifi-static" class="mb-3" style="display:none;">' +
              '<div class="mb-2"><label class="form-label small mb-0" for="wifi-ip">Adresse IP</label><input type="text" class="form-control form-control-sm" id="wifi-ip" maxlength="15" placeholder="192.168.1.50"></div>' +
              '<div class="mb-2"><label class="form-label small mb-0" for="wifi-mask">Masque</label><input type="text" class="form-control form-control-sm" id="wifi-mask" maxlength="15" placeholder="255.255.255.0"></div>' +
              '<div class="mb-2"><label class="form-label small mb-0" for="wifi-gw">Passerelle</label><input type="text" class="form-control form-control-sm" id="wifi-gw" maxlength="15" placeholder="192.168.1.1"></div>' +
            '</div>' +

            '<div id="wifi-error" class="text-danger mb-3" style="display:none;"></div>' +
            '<button type="submit" class="btn btn-warning btn-lg">Enregistrer</button>' +
          '</form>' +
          '<div id="wifi-reboot" class="mt-3" style="display:none;"><div class="spinner-border spinner-border-sm"></div> Red&eacute;marrage...</div>' +
        '</div>';
  }

  function initWifiSection() {
    // Charger etat WiFi via la nouvelle API
    $.getJSON('/api/config/wifi', function(data) {
      var html = '<table class="table table-sm mb-0">';
      html += '<tr><td><strong>Connexion</strong></td><td>' +
        (data.connected ?
          '<span class="badge bg-success">Connect&eacute;</span>' :
          '<span class="badge bg-danger">D&eacute;connect&eacute;</span>') +
        '</td></tr>';
      if (data.ssid) html += '<tr><td><strong>SSID</strong></td><td>' + escHtml(data.ssid) + '</td></tr>';
      if (data.ip) html += '<tr><td><strong>Adresse IP</strong></td><td>' + data.ip + '</td></tr>';
      if (data.rssi) {
        var rssiClass = data.rssi > -60 ? 'success' : (data.rssi > -75 ? 'warning' : 'danger');
        html += '<tr><td><strong>Signal (RSSI)</strong></td><td><span class="badge bg-' + rssiClass + '">' + data.rssi + ' dBm</span></td></tr>';
      }
      if (data.mac) html += '<tr><td><strong>MAC</strong></td><td>' + data.mac + '</td></tr>';
      html += '</table>';
      $('#wifi-info').html(html);

      // Pre-fill form
      $('#wifi-dhcp').prop('checked', data.enableDHCP !== false);
      $('#wifi-ip').val(data.staticIp || '');
      $('#wifi-mask').val(data.staticMask || '');
      $('#wifi-gw').val(data.staticGw || '');
      $('#wifi-static').toggle(data.enableDHCP === false);
      if (data.savedSsid) { $('#wifi-ssid').val(data.savedSsid); }
      else if (data.ssid) { $('#wifi-ssid').val(data.ssid); }

      // BLE provisioning info
      if (data.bleDeviceName) {
        var bleHtml = '<p><strong>Nom BLE:</strong> ' + data.bleDeviceName + '</p>';
        if (data.bleActive) {
          bleHtml += '<p><span class="badge bg-info">BLE actif</span> — Ouvrez LiXee-Assist et envoyez vos identifiants WiFi.</p>';
        } else {
          bleHtml += '<p class="text-muted">Le BLE provisioning se lance automatiquement si le WiFi n\'est pas configur&eacute; ou si la connexion &eacute;choue.</p>';
        }
        $('#wifi-ble-info').html(bleHtml);
        $('#wifi-ble-card').show();
      }
    }).fail(function() {
      $.getJSON('/api/system/info', function(data) {
        $('#wifi-info').html(
          '<p><strong>SSID:</strong> ' + escHtml(data.ssid || '--') + '</p>' +
          '<p><strong>RSSI:</strong> ' + (data.rssi || '--') + ' dBm</p>'
        );
        if (data.ssid) $('#wifi-ssid').val(data.ssid);
      });
    });

    // WiFi Scan
    $(document).off('click', '#wifi-scan-btn').on('click', '#wifi-scan-btn', function() {
      var $btn = $(this);
      $btn.prop('disabled', true).text('Scan...');
      $('#wifi-networks').show().html('<div class="spinner-border spinner-border-sm"></div> Recherche des r\u00e9seaux...');
      $.getJSON('/scanNetwork', function(networks) {
        if (!networks || networks.length === 0) {
          $('#wifi-networks').html('<p class="text-muted">Aucun r&eacute;seau trouv&eacute;.</p>');
        } else {
          var html = '<div class="list-group">';
          for (var i = 0; i < networks.length; i++) {
            var n = networks[i];
            var rssiCls = n.rssi > -60 ? 'success' : (n.rssi > -75 ? 'warning' : 'danger');
            html += '<a href="#" class="list-group-item list-group-item-action wifi-pick" data-ssid="' + escHtml(n.ssid) + '">' +
              '<div class="d-flex justify-content-between">' +
                '<span>' + (n.ssid ? escHtml(n.ssid) : '(Hidden)') + '</span>' +
                '<span class="badge bg-' + rssiCls + '">' + n.rssi + ' dBm</span>' +
              '</div></a>';
          }
          html += '</div>';
          $('#wifi-networks').html(html);
          // Click to pick SSID
          $('.wifi-pick').on('click', function(e) {
            e.preventDefault();
            $('#wifi-ssid').val($(this).data('ssid'));
            $('#wifi-networks').hide();
          });
        }
        $btn.prop('disabled', false).text('Scan');
      }).fail(function() {
        $('#wifi-networks').html('<p class="text-danger">Erreur de scan.</p>');
        $btn.prop('disabled', false).text('Scan');
      });
    });

    $(document).off('change', '#wifi-dhcp').on('change', '#wifi-dhcp', function() {
      $('#wifi-static').toggle(!this.checked);
    });

    // Toggle visibility mot de passe
    $(document).off('click', '#wifi-pass-toggle').on('click', '#wifi-pass-toggle', function() {
      var inp = document.getElementById('wifi-pass');
      if (inp.type === 'password') { inp.type = 'text'; $(this).text('Masquer'); }
      else { inp.type = 'password'; $(this).text('Voir'); }
    });

    // Soumission formulaire
    $(document).off('submit', '#wifi-form').on('submit', '#wifi-form', function(e) {
      e.preventDefault();
      var ssid = $('#wifi-ssid').val().trim();
      if (!ssid) { showAlert('Veuillez saisir un SSID', 'warning'); return; }

      var pass = $('#wifi-pass').val();
      if (pass && pass.length < 8) {
        $('#wifi-error').text('Le mot de passe doit faire au moins 8 caract\u00e8res.').show();
        return;
      }
      $('#wifi-error').hide();

      if (!confirm('Enregistrer le WiFi "' + ssid + '" et red\u00e9marrer ?')) return;

      // /api/config/wifi ecrit en NVS, la ou la connexion WiFi lit ses
      // identifiants. L'ancien /saveWifi ecrivait dans un fichier jamais relu.
      // Mot de passe vide sur le meme reseau = conserver l'actuel.
      $.ajax({
        url: '/api/config/wifi',
        method: 'POST',
        contentType: 'application/json',
        data: JSON.stringify({ ssid: ssid, password: pass, dhcp: $('#wifi-dhcp').is(':checked'),
                               ip: $('#wifi-ip').val().trim(), mask: $('#wifi-mask').val().trim(),
                               gw: $('#wifi-gw').val().trim() }),
        success: function() {
          showAlert('WiFi enregistr&eacute;. Red&eacute;marrage en cours...');
          $('#wifi-reboot').show();
        },
        error: function(xhr) { showAlert(apiErrorMsg(xhr, 'Erreur de sauvegarde'), 'danger'); }
      });
    });
  }

  // ============================================================================
  // PAGE: Zigbee
  // ============================================================================
  function pageZigbee() {
    setContent(
      '<div class="container py-4">' +
        '<h4>Config Zigbee</h4>' +

        // Commandes coordinateur
        '<div class="d-flex flex-wrap gap-2 justify-content-end mb-3">' +
          '<button class="btn btn-primary btn-sm" id="zbtn-network"><svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" class="bi bi-bootstrap-reboot" viewBox="0 0 16 16"><path d="M1.161 8a6.84 6.84 0 1 0 6.842-6.84.58.58 0 1 1 0-1.16 8 8 0 1 1-6.556 3.412l-.663-.577a.58.58 0 0 1 .227-.997l2.52-.69a.58.58 0 0 1 .728.633l-.332 2.592a.58.58 0 0 1-.956.364l-.643-.56A6.8 6.8 0 0 0 1.16 8z"/><path d="M6.641 11.671V8.843h1.57l1.498 2.828h1.314L9.377 8.665c.897-.3 1.427-1.106 1.427-2.1 0-1.37-.943-2.246-2.456-2.246H5.5v7.352zm0-3.75V5.277h1.57c.881 0 1.416.499 1.416 1.32 0 .84-.504 1.324-1.386 1.324z"/></svg> R&eacute;seau</button>' +
          '<button class="btn btn-primary btn-sm" id="zbtn-reset">Red&eacute;marrer</button>' +
          '<button class="btn btn-danger btn-sm" id="zbtn-erase">RAZ</button>' +
        '</div>' +

        // Infos coordinateur, LED et masque de canaux retires : le pilote
        // ne fournit ni ces informations ni ces commandes.

        // Devices
        '<div class="card p-4">' +
          '<div class="d-flex justify-content-between align-items-center mb-3">' +
            '<h5 class="mb-0">Appareils Zigbee</h5>' +
            '<button class="btn btn-success" id="btn-permit-join">Autoriser l\'appairage (60s)</button>' +
          '</div>' +
          '<div id="zigbee-devices"><div class="text-center"><div class="spinner-border" role="status"></div></div></div>' +
        '</div>' +
      '</div>'
    );

    // Zigbee command helper
    function zCmd(cmd, val) {
      var data = { cmd: cmd };
      if (val !== undefined) data.value = val;
      $.ajax({ url: '/cmd', method: 'POST', contentType: 'application/json', data: JSON.stringify(data),
        success: function() { showAlert('Commande ' + cmd + ' envoy\u00e9e'); },
        error: function(xhr) { showAlert(apiErrorMsg(xhr), 'danger'); }
      });
    }

    // Commands
    $('#zbtn-network').on('click', function() { zCmd('Network'); });
    $('#zbtn-reset').on('click', function() { zCmd('Reset'); });
    $('#zbtn-erase').on('click', function() {
      if (!confirm('\u00cates-vous s\u00fbr ? Cela effacera toute la config Zigbee !')) return;
      zCmd('ErasePDM');
    });

    // Permit join
    $('#btn-permit-join').on('click', function() {
      $.ajax({
        url: '/api/zigbee/permit_join',
        method: 'POST',
        contentType: 'application/json',
        data: JSON.stringify({duration: 60}),
        success: function() { showAlert('Appairage ouvert pendant 60 secondes'); },
        error: function() { showAlert('Erreur', 'danger'); }
      });
    });


    // Load devices
    $.getJSON('/api/zigbee/devices', function(devices) {
      if (!devices || devices.length === 0) {
        $('#zigbee-devices').html('<p class="text-muted">Aucun appareil.</p>');
        return;
      }
      var html = '<div class="table-responsive"><table class="table table-sm table-hover"><thead><tr><th>Mod&egrave;le</th><th>IEEE</th><th>Addr</th><th>DeviceId</th><th>LQI</th><th>Matter</th><th></th></tr></thead><tbody>';
      for (var i = 0; i < devices.length; i++) {
        var d = devices[i];
        html += '<tr>' +
          '<td>' + (d.model || d.alias || '--') + '</td>' +
          '<td><small class="text-muted">' + d.ieee + '</small></td>' +
          '<td>0x' + (d.shortAddr || 0).toString(16).toUpperCase() + '</td>' +
          '<td>0x' + (d.deviceId || 0).toString(16).toUpperCase() + '</td>' +
          '<td>' + (d.lqi != null ? d.lqi : '--') + '</td>' +
          '<td>' + (d.matterEndpoint > 0 ? 'EP ' + d.matterEndpoint : '-') + '</td>' +
          '<td><button class="btn btn-sm btn-outline-danger" data-ieee="' + d.ieee + '" title="Supprimer"><svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" fill="currentColor" viewBox="0 0 16 16"><path d="M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z"/><path d="M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H6a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1h3.5a1 1 0 0 1 1 1zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z"/></svg></button></td>' +
          '</tr>';
      }
      html += '</tbody></table></div>';
      $('#zigbee-devices').html(html);

      // Delete device handler
      $('#zigbee-devices').on('click', '[data-ieee]', function() {
        var ieee = $(this).data('ieee');
        if (!confirm('Supprimer l\'appareil ' + ieee + ' ?')) return;
        $.ajax({
          url: '/api/zigbee/remove',
          method: 'POST',
          contentType: 'application/json',
          data: JSON.stringify({ieee: ieee}),
          success: function() { showAlert('Appareil supprim\u00e9'); pageZigbee(); },
          error: function() { showAlert('Erreur', 'danger'); }
        });
      });
    });
  }

  // ============================================================================
  // PAGE: MQTT
  // ============================================================================
  function pageMqtt() {
    setContent(
      '<div class="container py-4">' +
        '<h4>Configuration MQTT</h4>' +

        // Statut connexion
        '<div class="card p-4 mb-4">' +
          '<div class="d-flex justify-content-between align-items-center">' +
            '<h5 class="mb-0">Statut</h5>' +
            '<span id="mqtt-status" class="badge bg-secondary">V&eacute;rification...</span>' +
          '</div>' +
        '</div>' +

        '<div class="card p-4">' +
          '<form id="mqtt-form">' +
            // Enable
            '<div class="mb-3"><div class="form-check form-switch"><input class="form-check-input" type="checkbox" id="mqtt-enabled"><label class="form-check-label" for="mqtt-enabled">Activer MQTT</label></div></div>' +

            // Broker + Port
            '<div class="row">' +
              '<div class="col-md-8 mb-3"><label class="form-label">Serveur MQTT</label><input type="text" class="form-control" id="mqtt-broker" placeholder="192.168.1.x"></div>' +
              '<div class="col-md-4 mb-3"><label class="form-label">Port</label><input type="number" class="form-control" id="mqtt-port" value="1883"></div>' +
            '</div>' +

            // User + Pass
            '<div class="row">' +
              '<div class="col-md-6 mb-3"><label class="form-label">Utilisateur</label><input type="text" class="form-control" id="mqtt-user"></div>' +
              '<div class="col-md-6 mb-3"><label class="form-label">Mot de passe</label><input type="password" class="form-control" id="mqtt-pass"></div>' +
            '</div>' +

            // Client ID
            '<div class="mb-3"><label class="form-label">Client ID</label><input type="text" class="form-control" id="mqtt-clientid"></div>' +

            // Mode HA / ThingsBoard / Custom
            '<div class="mb-3">' +
              '<label class="form-label">Type d\'int&eacute;gration</label>' +
              '<div class="form-check"><input class="form-check-input" type="radio" name="mqttMode" id="mqtt-ha" value="ha"><label class="form-check-label" for="mqtt-ha">Home Assistant (auto-discovery)</label></div>' +
              '<div class="form-check"><input class="form-check-input" type="radio" name="mqttMode" id="mqtt-tb" value="tb"><label class="form-check-label" for="mqtt-tb">ThingsBoard</label></div>' +
              '<div class="form-check"><input class="form-check-input" type="radio" name="mqttMode" id="mqtt-custom" value="custom"><label class="form-check-label" for="mqtt-custom">Personnalis&eacute;</label></div>' +
            '</div>' +

            // Header MQTT
            '<div class="mb-3"><label class="form-label" for="mqtt-header">Topic header MQTT</label><input type="text" class="form-control" id="mqtt-header"></div>' +

            // Custom JSON (visible only in custom mode)
            '<div class="mb-3" id="mqtt-custom-block" style="display:none;">' +
              '<label class="form-label">JSON personnalis&eacute;</label>' +
              '<textarea class="form-control" id="mqtt-customjson" rows="6" style="font-family:monospace;font-size:13px;"></textarea>' +
            '</div>' +

            '<button type="submit" class="btn btn-primary">Enregistrer</button>' +
          '</form>' +
        '</div>' +
      '</div>'
    );

    // Topic par defaut de chaque integration (comme la LiXee-Box) ; le mode
    // personnalise garde le topic saisi
    var MQTT_HEADERS = { ha: 'homeassistant/sensor/', tb: 'v1/gateway/telemetry' };

    // Choix du type d'integration : topic par defaut, bloc JSON du mode personnalise
    $(document).off('change', 'input[name=mqttMode]').on('change', 'input[name=mqttMode]', function() {
      var mode = $(this).val();
      $('#mqtt-custom-block').toggle(mode === 'custom');
      if (MQTT_HEADERS[mode]) $('#mqtt-header').val(MQTT_HEADERS[mode]);
    });

    // Load config
    // Noms de cles = ceux du serveur et de la configuration persistee.
    // Le mot de passe n'est jamais renvoye : seul hasPassMQTT l'est.
    $.getJSON('/api/config/mqtt', function(cfg) {
      $('#mqtt-enabled').prop('checked', !!cfg.enableMqtt);
      $('#mqtt-broker').val(cfg.servMQTT || '');
      $('#mqtt-port').val(cfg.portMQTT || 1883);
      $('#mqtt-user').val(cfg.userMQTT || '');
      $('#mqtt-clientid').val(cfg.clientIDMQTT || '');
      $('#mqtt-header').val(cfg.headerMQTT || '');
      if (cfg.hasPassMQTT) { $('#mqtt-pass').attr('placeholder', '******** (inchang\u00e9 si vide)'); }
      if (cfg.customMQTT) { $('#mqtt-custom').prop('checked', true); $('#mqtt-custom-block').show(); }
      else if (cfg.TBMQTT) { $('#mqtt-tb').prop('checked', true); }
      else { $('#mqtt-ha').prop('checked', true); }
      // Topic jamais renseigne : celui de l'integration choisie
      if (!cfg.headerMQTT) {
        var m = $('input[name=mqttMode]:checked').val();
        if (MQTT_HEADERS[m]) $('#mqtt-header').val(MQTT_HEADERS[m]);
      }
      if (cfg.customMQTTJson) { $('#mqtt-customjson').val(cfg.customMQTTJson); }
    });

    // Poll MQTT status
    function pollMqttStatus() {
      $.getJSON('/api/mqtt/status', function(st) {
        var cls = !st.enabled ? 'bg-secondary' : (st.connected ? 'bg-success' : 'bg-danger');
        var txt = !st.enabled ? 'D\u00e9sactiv\u00e9' : (st.connected ? 'Connect\u00e9' : 'D\u00e9connect\u00e9');
        $('#mqtt-status')
          .removeClass('bg-secondary bg-success bg-danger')
          .addClass(cls)
          .text(txt);
      }).fail(function() {
        $('#mqtt-status').removeClass('bg-secondary bg-success bg-danger').addClass('bg-secondary').text('Inconnu');
      });
    }
    pollMqttStatus();
    var mqttPoll = setInterval(pollMqttStatus, 5000);
    $(window).off('hashchange.mqtt').on('hashchange.mqtt', function() { clearInterval(mqttPoll); });

    // Save
    $('#mqtt-form').on('submit', function(e) {
      e.preventDefault();
      var mode = $('input[name=mqttMode]:checked').val() || 'ha';
      var payload = {
        enableMqtt: $('#mqtt-enabled').is(':checked'),
        servMQTT: $('#mqtt-broker').val().trim(),
        portMQTT: String(parseInt($('#mqtt-port').val(), 10) || 1883),
        userMQTT: $('#mqtt-user').val(),
        passMQTT: $('#mqtt-pass').val(),   // vide = conserver l'actuel
        clientIDMQTT: $('#mqtt-clientid').val(),
        headerMQTT: $('#mqtt-header').val(),
        HAMQTT: mode === 'ha',
        TBMQTT: mode === 'tb',
        customMQTT: mode === 'custom'
      };
      if (mode === 'custom') { payload.customMQTTJson = $('#mqtt-customjson').val(); }
      $.ajax({
        url: '/api/config/mqtt',
        method: 'POST',
        contentType: 'application/json',
        data: JSON.stringify(payload),
        success: function() {
          $('#mqtt-pass').val('');
          showAlert('MQTT enregistr&eacute; et appliqu&eacute; (reconnexion au broker).');
        },
        error: function(xhr) { showAlert(apiErrorMsg(xhr), 'danger'); }
      });
    });
  }

  // ============================================================================
  // PAGE: WebPush
  // ============================================================================
  function pageWebpush() {
    setContent(
      '<div class="container py-4">' +
        '<h4>Configuration WebPush</h4>' +
        '<div class="card p-4">' +
          '<form id="webpush-form">' +
            // Enable
            '<div class="mb-3"><div class="form-check form-switch"><input class="form-check-input" type="checkbox" id="webpush-enabled"><label class="form-check-label" for="webpush-enabled">Activer les notifications WebPush</label></div></div>' +

            // Server URL
            '<div class="mb-3"><label class="form-label">Serveur HTTP</label><input type="text" class="form-control" id="webpush-server" placeholder="http://serveur:port/api"></div>' +

            // Auth toggle
            '<div class="mb-3"><div class="form-check form-switch"><input class="form-check-input" type="checkbox" id="webpush-auth"><label class="form-check-label" for="webpush-auth">Authentification</label></div></div>' +

            // User + Pass (hidden by default)
            '<div id="webpush-auth-block" style="display:none;">' +
              '<div class="row">' +
                '<div class="col-md-6 mb-3"><label class="form-label">Utilisateur</label><input type="text" class="form-control" id="webpush-user"></div>' +
                '<div class="col-md-6 mb-3"><label class="form-label">Mot de passe</label><input type="password" class="form-control" id="webpush-pass"></div>' +
              '</div>' +
            '</div>' +

            '<button type="submit" class="btn btn-primary">Enregistrer</button>' +
          '</form>' +
        '</div>' +
      '</div>'
    );

    // Toggle auth fields
    $(document).off('change', '#webpush-auth').on('change', '#webpush-auth', function() {
      $('#webpush-auth-block').toggle(this.checked);
    });

    // Load config
    $.getJSON('/api/config/webpush', function(cfg) {
      $('#webpush-enabled').prop('checked', !!cfg.enableWebPush);
      $('#webpush-server').val(cfg.servWebPush || '');
      $('#webpush-auth').prop('checked', !!cfg.webPushAuth);
      if (cfg.webPushAuth) { $('#webpush-auth-block').show(); }
      $('#webpush-user').val(cfg.userWebPush || '');
      if (cfg.hasPassWebPush) { $('#webpush-pass').attr('placeholder', '******** (inchang\u00e9 si vide)'); }
    });

    // Save
    $('#webpush-form').on('submit', function(e) {
      e.preventDefault();
      var payload = {
        enableWebPush: $('#webpush-enabled').is(':checked'),
        servWebPush: $('#webpush-server').val().trim(),
        webPushAuth: $('#webpush-auth').is(':checked'),
        userWebPush: $('#webpush-user').val(),
        passWebPush: $('#webpush-pass').val()   // vide = conserver l'actuel
      };
      $.ajax({
        url: '/api/config/webpush',
        method: 'POST',
        contentType: 'application/json',
        data: JSON.stringify(payload),
        success: function() {
          $('#webpush-pass').val('');
          showAlert('WebPush enregistr&eacute;');
        },
        error: function(xhr) { showAlert(apiErrorMsg(xhr), 'danger'); }
      });
    });
  }

  // ============================================================================
  // PAGE: Matter (NOUVEAU)
  // ============================================================================

  // Code manuel 11 chiffres -> "XXXX-XXX-XXXX", forme affichee par les controleurs
  function formatManualCode(code) {
    if (/^\d{11}$/.test(code)) {
      return code.substr(0, 4) + '-' + code.substr(4, 3) + '-' + code.substr(7);
    }
    return code;
  }

  // Codes d'appairage du pont. Construits par le DOM (.text) plutot que par
  // concatenation HTML.
  function renderMatterPairing(data) {
    var $box = $('#matter-qrcode').empty();

    if (!data.started) {
      $box.append($('<p class="text-muted">').text(
        'La pile Matter n’est pas démarrée : redémarrez la passerelle après activation.'));
      return;
    }
    if (!data.manualCode && !data.qrCode) {
      $box.append($('<p class="text-muted">').text('Codes d’appairage indisponibles.'));
      return;
    }

    if (data.commissioned) {
      $box.append($('<div class="alert alert-info text-start small">').text(
        'La passerelle est associée à ' + (data.fabrics > 1 ? data.fabrics + ' contrôleurs' : 'un contrôleur') + '. ' +
        'Pour l’ajouter à un autre contrôleur (Apple Maison, Google Home...), ouvrez la fenêtre ' +
        'd’appairage ci-dessous puis saisissez le code habituel, ou partagez l’appareil depuis le premier contrôleur.'));
      var $w = $('<div class="mb-3">');
      if (data.windowOpen) {
        $w.append($('<span class="badge bg-success">').text('Fenêtre d’appairage ouverte'));
      } else {
        $w.append($('<button class="btn btn-sm btn-primary">').text('Ouvrir la fenêtre d’appairage (15 min)')
          .on('click', function() {
            postJSON('/api/matter/open-window', { seconds: 900 }).then(function() {
              showAlert('Fenêtre d’appairage ouverte pour 15 minutes');
              $.getJSON('/api/matter/status', renderMatterPairing);
            }, function(xhr) { showAlert(apiErrorMsg(xhr), 'danger'); });
          }));
      }
      $box.append($w);
    }

    if (data.manualCode) {
      $box.append($('<div class="small text-muted">').text('Code d’appairage manuel'));
      $box.append($('<div class="fs-3 fw-bold font-monospace mb-3">').text(formatManualCode(data.manualCode)));
    }

    if (data.qrCode) {
      $box.append($('<div class="small text-muted">').text('Contenu du QR code'));
      $box.append($('<div class="font-monospace mb-2">').text(data.qrCode));
      // Pas de generateur de QR code dans le firmware : la page officielle du
      // projet Matter le dessine dans le navigateur a partir de ce contenu.
      $box.append($('<a class="btn btn-outline-primary btn-sm" target="_blank" rel="noopener noreferrer">')
        .attr('href', 'https://project-chip.github.io/connectedhomeip/qrcode.html?data=' +
              encodeURIComponent(data.qrCode))
        .text('Afficher le QR code'));
    }

    $box.append($('<p class="small text-muted mt-3 mb-0 text-start">').text(
      'Home Assistant : Paramètres > Appareils et services > Ajouter une intégration > ' +
      'Matter, puis, depuis l’application mobile, scanner le QR code ou saisir le code manuel. ' +
      'Le téléphone doit avoir le Bluetooth activé si la passerelle n’est pas encore sur le réseau.'));
  }

  // Appareils du pont : Zigbee, LoRa et thermostats virtuels, avec leur
  // presence dans Matter (interrupteur : retirer / remettre)
  function loadMatterDevices() {
    var urls = ['/api/zigbee/devices'];
    if (activeModules && activeModules.lora) urls.push('/api/lora/devices');
    urls.push('/api/thermostats/config');
    urls.push('/api/actiongroups/list');
    var list = [], excluded = [], pending = urls.length + 1;
    function done() { if (--pending === 0) render(); }
    $.getJSON('/api/matter/excluded').done(function(e) { excluded = arr(e); }).always(done);
    urls.forEach(function(u) {
      $.getJSON(u).done(function(devices) {
        if (devices && devices.groups) {
          // Groupes d'actions publies : UniqueID "LXAG" + identifiant du groupe
          arr(devices.groups).forEach(function(g) {
            if (!g.matter || !g.matterUid) return;
            var key = '4C584147' + ('00000000' + Number(g.matterUid).toString(16).toUpperCase()).slice(-8);
            list.push({ key: key, alias: g.name, model: 'Groupe d\'actions', ieee: '', radio: 'virtual',
                        ep: g.matterEp || 0, active: g.enabled !== false });
          });
          return;
        }
        if (devices && devices.zones) {
          arr(devices.zones).forEach(function(z) {
            list.push({ key: z.matterUid, alias: z.name, model: 'Thermostat virtuel', ieee: '', radio: 'virtual',
                        ep: z.matterEndpoint, active: z.enabled });
          });
          return;
        }
        arr(devices).forEach(function(d) {
          if (!d || !d.ieee) return;
          list.push({ key: d.ieee, alias: d.alias, model: d.model, ieee: d.ieee, radio: d.radio === 'lora' ? 'lora' : 'zigbee',
                      ep: d.matterEndpoint, active: d.active });
        });
      }).always(done);
    });
    function render() {
      list.forEach(function(d) {
        d.excluded = excluded.indexOf(String(d.key || '').toUpperCase()) >= 0;
        d.bridged = !d.excluded && d.ep > 0;
      });
      if (!list.length) {
        $('#matter-devices').html('<p class="text-muted">Aucun appareil.</p>');
        return;
      }
      // Appareils pontes d'abord (par endpoint), puis les autres
      list.sort(function(a, b) {
        if (a.bridged !== b.bridged) return a.bridged ? -1 : 1;
        if (a.bridged) return a.ep - b.ep;
        return String(a.alias || a.model || '').localeCompare(String(b.alias || b.model || ''));
      });
      var html = '<div class="table-responsive"><table class="table table-sm mb-0 align-middle"><thead><tr><th>Nom</th><th>Mod&egrave;le</th><th>IEEE</th><th>Radio</th><th>Endpoint</th><th>Actif</th><th>Dans Matter</th></tr></thead><tbody>';
      list.forEach(function(d) {
        html += '<tr' + (d.excluded ? ' class="text-muted"' : '') + '><td>' + escHtml(d.alias || '--') + '</td><td>' + escHtml(d.model || '--') + '</td><td><small>' + escHtml(d.ieee) + '</small></td>' +
                '<td>' + (d.radio === 'lora' ? 'LoRa' : (d.radio === 'virtual' ? 'Virtuel' : 'Zigbee')) + '</td>' +
                '<td>' + (d.bridged ? d.ep : '&mdash;') + '</td>' +
                '<td>' + (d.active ? '<span class="badge bg-success">Oui</span>' : '<span class="badge bg-secondary">Non</span>') + '</td>' +
                '<td><div class="form-check form-switch m-0"><input class="form-check-input mt-expose" type="checkbox" role="switch" style="cursor:pointer;"' +
                  ' data-key="' + escHtml(d.key) + '"' + (d.excluded ? '' : ' checked') + ' title="Publier dans Matter / retirer de Matter"></div></td></tr>';
      });
      html += '</tbody></table></div>' +
              '<div class="form-text mt-2">D&eacute;cochez &laquo; Dans Matter &raquo; pour retirer un appareil de Matter (il dispara&icirc;t de Home Assistant) ; recochez pour le remettre, sur le m&ecirc;me endpoint.</div>';
      $('#matter-devices').html(html);
    }
  }

  $(document).off('change', '.mt-expose').on('change', '.mt-expose', function() {
    var $c = $(this).prop('disabled', true);
    postJSON('/api/matter/expose', { ieee: String($c.data('key')), expose: $c.is(':checked') })
      .fail(function(xhr) { showAlert(apiErrorMsg(xhr, 'Modification impossible'), 'danger'); })
      .always(function() { setTimeout(function() { if ($('#matter-devices').length) pageMatter(); }, 1500); });
  });

  function pageMatter() {
    setContent(
      '<div class="container py-4">' +
        '<h4>Passerelle Matter</h4>' +

        // Activation Matter
        '<div class="card p-4 mb-4">' +
          '<div class="d-flex justify-content-between align-items-center">' +
            '<div>' +
              '<h5 class="mb-1">Activation Matter</h5>' +
              '<small class="text-muted">Un red&eacute;marrage est n&eacute;cessaire apr&egrave;s modification.</small>' +
            '</div>' +
            '<div class="form-check form-switch">' +
              '<input class="form-check-input" type="checkbox" id="matter-toggle" style="transform:scale(1.5);">' +
              '<label class="form-check-label ms-2" id="matter-toggle-label" for="matter-toggle">--</label>' +
            '</div>' +
          '</div>' +
        '</div>' +

        // Statut (visible seulement si Matter actif)
        // Appairage (QR code, haut) a gauche ; statut et appareils empiles a
        // droite, pour ne pas laisser de vide sous le statut
        '<div id="matter-details">' +
          '<div class="row g-4">' +
            '<div class="col-lg-4">' +
              '<div class="card p-4 h-100">' +
                '<h5>Appairage Matter</h5>' +
                '<div id="matter-qrcode" class="text-center p-3">' +
                  '<p class="text-muted">Chargement...</p>' +
                '</div>' +
              '</div>' +
            '</div>' +
            '<div class="col-lg-8">' +
              '<div class="card p-4 mb-4">' +
                '<h5>Statut</h5>' +
                '<div id="matter-status"><div class="text-center"><div class="spinner-border" role="status"></div></div></div>' +
              '</div>' +
              '<div class="card p-4">' +
                '<h5>Appareils brid&eacute;s</h5>' +
                '<div id="matter-devices"><div class="text-center"><div class="spinner-border" role="status"></div></div></div>' +
              '</div>' +
            '</div>' +
          '</div>' +
          '<div class="row g-4 mt-2">' +
            '<div class="col-12">' +
              '<div class="card p-4">' +
                '<h5>Actions</h5>' +
                '<button class="btn btn-danger" id="btn-matter-reset">Factory Reset Matter</button>' +
                '<small class="text-muted ms-3">Supprime toutes les fabrics et red&eacute;marre le commissioning.</small>' +
              '</div>' +
            '</div>' +
          '</div>' +
        '</div>' +

        // Message quand Matter est desactive
        '<div id="matter-disabled" style="display:none;">' +
          '<div class="alert alert-info">' +
            'Matter est d&eacute;sactiv&eacute;. Activez-le ci-dessus et red&eacute;marrez pour d&eacute;marrer la passerelle Matter.' +
          '</div>' +
        '</div>' +

      '</div>'
    );

    // Charger statut et etat du toggle
    $.getJSON('/api/matter/status', function(data) {
      var enabled = data.enabled;
      $('#matter-toggle').prop('checked', enabled);
      $('#matter-toggle-label').text(enabled ? 'Activ\u00e9' : 'D\u00e9sactiv\u00e9');

      if (enabled) {
        $('#matter-details').show();
        $('#matter-disabled').hide();

        var html = '<table class="table table-sm mb-0">' +
          '<tr><td><strong>Commissionn&eacute;</strong></td><td>' + (data.commissioned ? '<span class="badge bg-success">Oui</span>' : '<span class="badge bg-warning">Non</span>') + '</td></tr>' +
          '<tr><td><strong>Appareils pont&eacute;s</strong></td><td>' +
            (data.bridgedDevices != null ? data.bridgedDevices + ' / ' + data.maxDevices + ' maximum' : '--') + '</td></tr>' +
          '</table>';
        $('#matter-status').html(html);

        renderMatterPairing(data);
        loadMatterDevices();
      } else {
        $('#matter-details').hide();
        $('#matter-disabled').show();
      }
    }).fail(function() {
      $('#matter-toggle-label').text('Erreur');
      $('#matter-details').hide();
      $('#matter-disabled').html('<div class="alert alert-danger">Erreur de connexion \u00e0 l\'API.</div>').show();
    });

    // Toggle Matter on/off
    $(document).off('change', '#matter-toggle').on('change', '#matter-toggle', function() {
      var enabled = $(this).is(':checked');
      $.ajax({
        url: '/api/matter/config',
        method: 'POST',
        contentType: 'application/json',
        data: JSON.stringify({enabled: enabled}),
        success: function(resp) {
          $('#matter-toggle-label').text(enabled ? 'Activ\u00e9' : 'D\u00e9sactiv\u00e9');
          showAlert(enabled ?
            'Matter activ&eacute;. <strong>Red&eacute;marrez</strong> pour appliquer.' :
            'Matter d&eacute;sactiv&eacute;. <strong>Red&eacute;marrez</strong> pour appliquer.',
            'warning');
        },
        error: function() { showAlert('Erreur de sauvegarde', 'danger'); }
      });
    });

    // Factory reset
    $(document).off('click', '#btn-matter-reset').on('click', '#btn-matter-reset', function() {
      if (!confirm('Voulez-vous vraiment effectuer un factory reset Matter ?\nToutes les fabrics seront supprim\u00e9es.')) return;
      $.ajax({
        url: '/api/matter/factory_reset',
        method: 'POST',
        success: function() { showAlert('Factory reset effectu&eacute;. Red&eacute;marrage...'); },
        error: function() { showAlert('Erreur', 'danger'); }
      });
    });
  }

  // ============================================================================
  // PAGE: Config General (2 tabs: Parametres + Debug)
  // ============================================================================
  function pageConfigGeneral() {
    setContent(
      '<div class="container py-4">' +
        '<h4>Configuration g&eacute;n&eacute;rale</h4>' +
        '<ul class="nav nav-tabs mb-3"><li class="nav-item"><button class="nav-link active" data-bs-toggle="tab" data-bs-target="#tab-params">Param&egrave;tres</button></li><li class="nav-item"><button class="nav-link" data-bs-toggle="tab" data-bs-target="#tab-debug">Debug</button></li></ul>' +
        '<div class="tab-content">' +
          '<div class="tab-pane fade show active" id="tab-params">' +
            '<div class="card p-4">' +
              '<form id="general-form">' +
                '<div class="form-check mb-3"><input type="checkbox" class="form-check-input" id="cfg-devmode"><label class="form-check-label">Mode d&eacute;veloppeur</label></div>' +
                '<button type="submit" class="btn btn-primary">Enregistrer</button>' +
              '</form>' +
            '</div>' +
          '</div>' +
          '<div class="tab-pane fade" id="tab-debug">' +
            '<div class="card p-4">' +
              '<form id="debug-form">' +
                '<div class="form-check mb-3"><input type="checkbox" class="form-check-input" id="cfg-debug-serial"><label class="form-check-label">Debug serial</label></div>' +
                '<button type="submit" class="btn btn-primary">Enregistrer</button>' +
              '</form>' +
            '</div>' +
          '</div>' +
        '</div>' +
      '</div>'
    );
    // /getConfig, /saveConfigGeneral et /saveDebug n'existent pas cote
    // serveur : tout passe par /api/config/general, avec ses noms de cles.
    $.getJSON('/api/config/general', function(cfg) {
      $('#cfg-devmode').prop('checked', !!cfg.developerMode);
      $('#cfg-debug-serial').prop('checked', !!cfg.enableDebug);
    });
    function saveGeneral(payload, okMsg, onOk) {
      $.ajax({
        url: '/api/config/general',
        method: 'POST',
        contentType: 'application/json',
        data: JSON.stringify(payload),
        success: function() { showAlert(okMsg); if (onOk) onOk(); },
        error: function(xhr) { showAlert(apiErrorMsg(xhr), 'danger'); }
      });
    }
    $('#general-form').on('submit', function(e) {
      e.preventDefault();
      var dev = $('#cfg-devmode').is(':checked');
      saveGeneral({ developerMode: dev },
                  'Configuration enregistr&eacute;e', function() { showTools(dev); });
    });
    $('#debug-form').on('submit', function(e) {
      e.preventDefault();
      saveGeneral({ enableDebug: $('#cfg-debug-serial').is(':checked') },
                  'Debug enregistr&eacute;');
    });
  }

  // ============================================================================
  // PAGE: Config Energie (6 onglets: Linky, Production, Gaz, Eau, Sous-compteurs, Presence)
  // ============================================================================
  function pageConfigEnergy() {
    setContent(
      '<div class="container py-5">' +
        '<h4 class="mb-4">Config Energie</h4>' +
        '<ul class="nav nav-tabs" id="energyTab" role="tablist">' +
          '<li class="nav-item"><button class="nav-link active" data-bs-toggle="tab" data-bs-target="#linky" type="button">Linky</button></li>' +
          '<li class="nav-item"><button class="nav-link" data-bs-toggle="tab" data-bs-target="#production" type="button">Production</button></li>' +
          '<li class="nav-item"><button class="nav-link" data-bs-toggle="tab" data-bs-target="#gaz" type="button">Gaz</button></li>' +
          '<li class="nav-item"><button class="nav-link" data-bs-toggle="tab" data-bs-target="#water" type="button">Eau</button></li>' +
          '<li class="nav-item"><button class="nav-link" data-bs-toggle="tab" data-bs-target="#submeters" type="button">Sous-compteurs</button></li>' +
          '<li class="nav-item"><button class="nav-link" data-bs-toggle="tab" data-bs-target="#presence" type="button">Pr&eacute;sence</button></li>' +
        '</ul>' +
        '<div class="tab-content">' +

          // === Onglet Linky ===
          '<div class="tab-pane fade show active" id="linky">' +
            '<div class="card mx-auto shadow-sm"><div class="card-body">' +
              '<form id="form-linky">' +
                '<h5>Appareil</h5>' +
                '<div class="mb-3"><label class="form-label">Appareil Linky</label><select class="form-select" id="cfg-zlinky"></select></div>' +
                '<h5>G&eacute;n&eacute;ral</h5>' +
                '<div class="mb-3"><label class="form-label">Surface habitable (en m&sup2;)</label><input class="form-control" id="cfg-shon" type="text"></div>' +
                '<h5>Tarifs <button class="btn btn-link btn-sm" type="button" data-bs-toggle="collapse" data-bs-target="#tarifLinky">+/-</button></h5>' +
                '<div class="collapse" id="tarifLinky">' +
                  '<div class="mb-3"><label>Tarif abonnement (&euro; / mois)</label><input class="form-control decimal-input" id="cfg-tarifAbo" type="text"></div>' +
                  '<div class="mb-3"><label>Accise sur l\'Electricit&eacute; (&euro; / kWh)</label><input class="form-control decimal-input" id="cfg-tarifCSPE" type="text"></div>' +
                  '<div class="mb-3"><label>CTA (&euro; / mois)</label><input class="form-control decimal-input" id="cfg-tarifCTA" type="text"></div>' +
                  '<div class="mb-3"><label>Tarif BASE/HC/EJPHN/BBRHCJB/EASF01 (&euro; / kWh)</label><input class="form-control decimal-input" id="cfg-tarifIdx2" type="text"></div>' +
                  '<div class="mb-3"><label>Tarif HP/EJPHPM/BBRHPJB/EASF02 (&euro; / kWh)</label><input class="form-control decimal-input" id="cfg-tarifIdx3" type="text"></div>' +
                  '<div class="mb-3"><label>Tarif BBRHCJW/EASF03 (&euro; / kWh)</label><input class="form-control decimal-input" id="cfg-tarifIdx4" type="text"></div>' +
                  '<div class="mb-3"><label>Tarif BBRHPJW/EASF04 (&euro; / kWh)</label><input class="form-control decimal-input" id="cfg-tarifIdx5" type="text"></div>' +
                  '<div class="mb-3"><label>Tarif BBRHCJR/EASF05 (&euro; / kWh)</label><input class="form-control decimal-input" id="cfg-tarifIdx6" type="text"></div>' +
                  '<div class="mb-3"><label>Tarif BBRHPJR/EASF06 (&euro; / kWh)</label><input class="form-control decimal-input" id="cfg-tarifIdx7" type="text"></div>' +
                  '<div class="mb-3"><label>Tarif EASF07 (&euro; / kWh)</label><input class="form-control decimal-input" id="cfg-tarifIdx8" type="text"></div>' +
                  '<div class="mb-3"><label>Tarif EASF08 (&euro; / kWh)</label><input class="form-control decimal-input" id="cfg-tarifIdx9" type="text"></div>' +
                  '<div class="mb-3"><label>Tarif EASF09 (&euro; / kWh)</label><input class="form-control decimal-input" id="cfg-tarifIdx10" type="text"></div>' +
                '</div>' +
                '<h5>D&eacute;lestage automatique <button class="btn btn-link btn-sm" type="button" data-bs-toggle="collapse" data-bs-target="#delestageBox">+/-</button></h5>' +
                '<div class="collapse" id="delestageBox">' +
                  '<div id="dl-state" class="mb-2"></div>' +
                  '<h6>Prise(s) connect&eacute;e(s) :</h6>' +
                  '<div id="dl-plugs" class="mb-2"><span class="text-muted">Chargement...</span></div>' +
                  '<div class="form-text mb-2">En cas de d&eacute;passement de la puissance souscrite signal&eacute; par le compteur, ces prises sont coup&eacute;es.</div>' +
                  '<h6 class="mt-3">Rallumage automatique</h6>' +
                  '<div class="row g-2">' +
                    '<div class="col-sm-6"><label for="cfg-dlRestoreMin">Apr&egrave;s (minutes, 0 = jamais)</label><input class="form-control" id="cfg-dlRestoreMin" type="number" min="0" max="240"></div>' +
                    '<div class="col-sm-6"><label for="cfg-dlRestorePct">Sous (% de la puissance souscrite)</label><input class="form-control" id="cfg-dlRestorePct" type="number" min="10" max="100"></div>' +
                  '</div>' +
                  '<small class="text-muted">Seules les prises allum&eacute;es avant la coupure sont rallum&eacute;es. La puissance est jug&eacute;e sur sa moyenne sur 1 minute, la dur&eacute;e sur laquelle le Linky coupe.</small>' +
                  '<h6 class="mt-3">D&eacute;lestage anticip&eacute;</h6>' +
                  '<label for="cfg-dlAnticip">Couper d&egrave;s que la moyenne sur 1 minute atteint (% de la puissance souscrite, 0 = seulement sur signal du compteur)</label>' +
                  '<input class="form-control" id="cfg-dlAnticip" type="number" min="0" max="100">' +
                '</div>' +
                '<button type="submit" class="btn btn-primary mt-3">Enregistrer</button>' +
              '</form>' +
            '</div></div>' +
          '</div>' +

          // === Onglet Production ===
          '<div class="tab-pane fade" id="production">' +
            '<div class="card mx-auto shadow-sm"><div class="card-body">' +
              '<form id="form-production">' +
                '<h5>Appareil Production</h5>' +
                '<div class="mb-3"><label class="form-label">Appareil</label><select class="form-select" id="cfg-production"></select></div>' +
                '<h5>Tarifs</h5>' +
                '<div class="mb-3"><label>Abonnement production (&euro; / mois)</label><input class="form-control decimal-input" id="cfg-tarifAboProd" type="text"></div>' +
                '<div class="mb-3"><label>Tarif rachat (&euro; / kWh)</label><input class="form-control decimal-input" id="cfg-tarifProd" type="text"></div>' +
                '<button type="submit" class="btn btn-primary mt-3">Enregistrer</button>' +
              '</form>' +
            '</div></div>' +
          '</div>' +

          // === Onglet Gaz ===
          '<div class="tab-pane fade" id="gaz">' +
            '<div class="card mx-auto shadow-sm"><div class="card-body">' +
              '<form id="form-gaz">' +
                '<h5>Appareil Gaz</h5>' +
                '<div class="mb-3"><label class="form-label">Appareil</label><select class="form-select" id="cfg-gaz"></select></div>' +
                '<div class="mb-3"><label>Coefficient de conversion</label><input class="form-control decimal-input" id="cfg-coeffGaz" type="text" value="1"></div>' +
                '<div class="mb-3"><label>Unit&eacute;</label><input class="form-control" id="cfg-unitGaz" type="text" maxlength="11" list="dl-unit-gaz" placeholder="m3">' +
                  '<datalist id="dl-unit-gaz"><option value="Wh"><option value="kWh"><option value="m3"><option value="L"></datalist>' +
                  '<div class="form-text">En <b>Wh</b>, le gaz est ajout&eacute; au bilan &eacute;nerg&eacute;tique (r&eacute;partition, &eacute;tiquette).</div></div>' +
                '<div class="mb-3"><label>Tarif (&euro; / unit&eacute;)</label><input class="form-control decimal-input" id="cfg-tarifGaz" type="text"></div>' +
                '<button type="submit" class="btn btn-primary mt-3">Enregistrer</button>' +
              '</form>' +
            '</div></div>' +
          '</div>' +

          // === Onglet Eau ===
          '<div class="tab-pane fade" id="water">' +
            '<div class="card mx-auto shadow-sm"><div class="card-body">' +
              '<form id="form-water">' +
                '<h5>Appareil Eau</h5>' +
                '<div class="mb-3"><label class="form-label">Appareil</label><select class="form-select" id="cfg-water"></select></div>' +
                '<div class="mb-3"><label>Coefficient</label><input class="form-control decimal-input" id="cfg-coeffWater" type="text" value="1"></div>' +
                '<div class="mb-3"><label>Unit&eacute;</label><input class="form-control" id="cfg-unitWater" type="text" maxlength="11" list="dl-unit-water" placeholder="L">' +
                  '<datalist id="dl-unit-water"><option value="L"><option value="m3"></datalist></div>' +
                '<div class="mb-3"><label>Tarif (&euro; / unit&eacute;)</label><input class="form-control decimal-input" id="cfg-tarifWater" type="text"></div>' +
                '<button type="submit" class="btn btn-primary mt-3">Enregistrer</button>' +
              '</form>' +
            '</div></div>' +
          '</div>' +

          // === Onglet Sous-compteurs (/api/energy/submeters) ===
          '<div class="tab-pane fade" id="submeters">' +
            '<div class="card mx-auto shadow-sm"><div class="card-body">' +
              '<h5>Sous-compteurs <small class="text-muted" id="sm-count"></small></h5>' +
              '<div class="form-text mb-2">Appareils Zigbee mesurant l\'&eacute;nergie (cluster 0x0702) : leur consommation est d&eacute;duite des index du Linky et affich&eacute;e s&eacute;par&eacute;ment dans le tableau de bord.</div>' +
              '<div class="table-responsive"><table class="table table-sm align-middle">' +
                '<thead><tr><th>Nom</th><th>Appareil</th><th>Couleur</th><th>Actif</th><th></th></tr></thead>' +
                '<tbody id="sm-list"><tr><td colspan="5" class="text-muted">Chargement...</td></tr></tbody>' +
              '</table></div>' +
              '<h5 class="mt-3">Ajouter un sous-compteur</h5>' +
              '<form id="form-submeter">' +
                '<div class="mb-3"><label class="form-label" for="sm-ieee">Appareil</label><select class="form-select" id="sm-ieee"></select></div>' +
                '<div class="mb-3"><label class="form-label" for="sm-alias">Nom</label><input class="form-control" id="sm-alias" type="text"></div>' +
                '<div class="mb-3"><label class="form-label" for="sm-color">Couleur</label><input class="form-control form-control-color" id="sm-color" type="color" value="#e74c3c"></div>' +
                '<div class="form-text mb-2" id="sm-full" style="display:none;">Nombre maximal de sous-compteurs atteint.</div>' +
                '<button type="submit" class="btn btn-primary" id="sm-add">Ajouter</button>' +
              '</form>' +
            '</div></div>' +
          '</div>' +

          // === Onglet Presence ===
          '<div class="tab-pane fade" id="presence">' +
            '<div class="card mx-auto shadow-sm"><div class="card-body">' +
              '<form id="form-presence">' +
                '<h5>D&eacute;tection de pr&eacute;sence</h5>' +
                '<div class="form-text mb-2">Les heures (et les jours) avec une pr&eacute;sence d&eacute;tect&eacute;e sont surlign&eacute;es en vert sur le graphe &laquo; Usage d&#39;&eacute;lectricit&eacute; &raquo; de la page &Eacute;nergie.</div>' +
                '<div class="mb-3"><label class="form-label">Appareil</label><select class="form-select" id="cfg-presence"></select></div>' +
                '<div class="form-check"><input type="checkbox" class="form-check-input" id="cfg-presenceGraph"><label class="form-check-label" for="cfg-presenceGraph">Afficher sur le graphique</label></div>' +
                '<button type="submit" class="btn btn-primary mt-3">Enregistrer</button>' +
              '</form>' +
            '</div></div>' +
          '</div>' +

        '</div>' +
      '</div>'
    );

    // Les selecteurs d'appareil doivent etre remplis AVANT d'appliquer la
    // config : un .val() sur un <select> sans l'option correspondante est
    // ignore, d'ou un ordre impose. Noms d'appareils echappes : ils viennent
    // du reseau Zigbee.
    var TARIF_KEYS = ['tarifAbo','tarifCSPE','tarifCTA','tarifIdx2','tarifIdx3','tarifIdx4',
                      'tarifIdx5','tarifIdx6','tarifIdx7','tarifIdx8','tarifIdx9','tarifIdx10'];
    function dec(sel) { return String($(sel).val() || '').trim().replace(',', '.'); }

    // Appareils Zigbee ET emetteurs LoRa (ZLinky LoRa), comme la LiXee-Box qui
    // liste tous ses appareils ; une radio absente ne bloque pas l'autre
    function devList(url) {
      var d = $.Deferred();
      $.getJSON(url).done(function(r) { d.resolve(r || []); }).fail(function() { d.resolve([]); });
      return d.promise();
    }
    $.when(devList('/api/zigbee/devices'), devList('/api/lora/devices')).done(function(zb, lora) {
      // Delestage : prises et relais pilotables en marche/arret
      var plugs = zb.filter(function(d) { return d && d.onoff; });
      $('#dl-plugs').html(plugs.length ? '' : '<span class="text-muted">Aucune prise d&eacute;tect&eacute;e.</span>');
      plugs.forEach(function(d) {
        var id = 'dl-' + d.ieee;
        $('#dl-plugs').append('<div class="form-check"><input type="checkbox" class="form-check-input dl-plug" id="' + escHtml(id) +
          '" value="' + escHtml(d.ieee) + '"><label class="form-check-label" for="' + escHtml(id) + '">' +
          escHtml((d.alias || d.model || d.ieee) + ' (' + d.ieee + ')') + '</label></div>');
      });

      // Presence : seuls les appareils dotes du cluster Occupancy (0x0406)
      $('#cfg-presence').html('<option value="">-- Aucun --</option>');
      zb.forEach(function(d) {
        if (d.occupancy) $('#cfg-presence').append('<option value="' + escHtml(d.ieee) + '">' + escHtml(d.alias || d.model || d.ieee) + '</option>');
      });
      ['#cfg-zlinky','#cfg-production','#cfg-gaz','#cfg-water'].forEach(function(sel) {
        $(sel).html('<option value="">-- Aucun --</option>');
        zb.forEach(function(d) {
          $(sel).append('<option value="' + escHtml(d.ieee) + '">' + escHtml(d.alias || d.model || d.ieee) + '</option>');
        });
        lora.forEach(function(d) {
          $(sel).append('<option value="' + escHtml(d.ieee) + '">' + escHtml((d.alias || d.model || d.ieee) + ' (LoRa)') + '</option>');
        });
      });
    }).always(function() {
      // /getConfig n'existe pas : tout vient de /api/config/general
      $.getJSON('/api/config/general', function(cfg) {
        $('#cfg-zlinky').val(cfg.ZLinky || '');
        $('#cfg-shon').val(cfg.shon || '');
        TARIF_KEYS.forEach(function(k) { $('#cfg-' + k).val(cfg[k] || ''); });
        $('#cfg-production').val(cfg.Production || '');
        $('#cfg-tarifProd').val(cfg.tarifIdxProd || '');
        $('#cfg-tarifAboProd').val(cfg.tarifAboProd || '');
        $('#cfg-gaz').val(cfg.Gaz || '');
        $('#cfg-coeffGaz').val(cfg.coeffGaz != null ? cfg.coeffGaz : 1);
        $('#cfg-unitGaz').val(cfg.unitGaz || '');
        $('#cfg-tarifGaz').val(cfg.tarifGaz || '');
        $('#cfg-water').val(cfg.Water || '');
        $('#cfg-coeffWater').val(cfg.coeffWater != null ? cfg.coeffWater : 1);
        $('#cfg-unitWater').val(cfg.unitWater || '');
        $('#cfg-tarifWater').val(cfg.tarifWater || '');
        $('#cfg-presence').val(cfg.Presence || '');
        var dl = ',' + String(cfg.delestage || '').toUpperCase().replace(/\s/g, '') + ',';
        $('.dl-plug').each(function() { this.checked = dl.indexOf(',' + String(this.value).toUpperCase() + ',') >= 0; });
        $('#cfg-dlRestoreMin').val(cfg.delestageRestoreMin != null ? cfg.delestageRestoreMin : 5);
        $('#cfg-dlRestorePct').val(cfg.delestageRestorePct != null ? cfg.delestageRestorePct : 80);
        $('#cfg-dlAnticip').val(cfg.delestageAnticipPct != null ? cfg.delestageAnticipPct : 0);
        $('#cfg-presenceGraph').prop('checked', !!cfg.enablePresenceGraph);
      });
    });

    // Enregistrement en JSON. Les anciens formulaires faisaient
    // $(this).serialize() sur des champs SANS attribut name, donc envoyaient
    // un corps vide, vers des routes /saveConfig* qui n'existaient pas.
    function saved(msg) { return function() { showAlert(msg); }; }
    function failed(xhr) { showAlert(apiErrorMsg(xhr), 'danger'); }

    $(document).off('submit','#form-linky').on('submit','#form-linky', function(e) {
      e.preventDefault();
      var g = { ZLinky: $('#cfg-zlinky').val() || '', shon: parseInt($('#cfg-shon').val(), 10) || 0 };
      TARIF_KEYS.forEach(function(k) { g[k] = dec('#cfg-' + k); });
      g.delestage = $('.dl-plug:checked').map(function() { return this.value; }).get().join(',');
      g.delestageRestoreMin = parseInt($('#cfg-dlRestoreMin').val(), 10) || 0;
      g.delestageRestorePct = parseInt($('#cfg-dlRestorePct').val(), 10) || 80;
      g.delestageAnticipPct = parseInt($('#cfg-dlAnticip').val(), 10) || 0;
      // Les alertes se reglent dans Config -> Notifications
      postJSON('/api/config/general', g).then(saved('Configuration Linky enregistr&eacute;e'), failed);
    });
    function loadDelestage() {
      $.getJSON('/api/delestage').done(function(d) {
        d = d || {};
        var h;
        if (d.active) {
          var cut = (d.devices || []).filter(function(x) { return x.cut; }).length;
          h = '<div class="alert alert-warning py-2 mb-2">D&eacute;lestage en cours depuis ' + escHtml(d.since || '?') +
              ' (' + (d.reason === 'anticipation' ? 'anticip&eacute;' : 'signal du compteur') + ') : ' + cut + ' prise(s) coup&eacute;e(s). ' +
              '<button type="button" class="btn btn-sm btn-warning ms-2" id="dl-restore">Rallumer maintenant</button></div>';
        } else {
          h = '<div class="text-muted small mb-2">Aucun d&eacute;lestage en cours' + (d.lastEnd ? ' (dernier termin&eacute; le ' + escHtml(d.lastEnd) + ')' : '') + '.</div>';
        }
        if (d.minuteLoadPercent != null && d.minuteLoadPercent >= 0) {
          h += '<div class="text-muted small mb-2">Charge moyenne sur 1 minute : ' + d.minuteLoadPercent + ' % de la puissance souscrite.</div>';
        }
        $('#dl-state').html(h);
      });
    }
    loadDelestage();
    pageInterval(loadDelestage, 30000);
    $(document).off('click', '#dl-restore').on('click', '#dl-restore', function() {
      postJSON('/api/delestage/restore', {}).then(function() {
        showAlert('Rallumage demand&eacute;');
        setTimeout(loadDelestage, 2000);
      }, failed);
    });

    $(document).off('submit','#form-production').on('submit','#form-production', function(e) {
      e.preventDefault();
      postJSON('/api/config/general', { Production: $('#cfg-production').val() || '', tarifIdxProd: dec('#cfg-tarifProd'),
                                        tarifAboProd: dec('#cfg-tarifAboProd') })
        .then(saved('Configuration Production enregistr&eacute;e'), failed);
    });
    $(document).off('submit','#form-gaz').on('submit','#form-gaz', function(e) {
      e.preventDefault();
      postJSON('/api/config/general', {
        Gaz: $('#cfg-gaz').val() || '', coeffGaz: parseFloat(dec('#cfg-coeffGaz')) || 1,
        unitGaz: $.trim($('#cfg-unitGaz').val()) || 'm3', tarifGaz: dec('#cfg-tarifGaz')
      }).then(saved('Configuration Gaz enregistr&eacute;e'), failed);
    });
    $(document).off('submit','#form-water').on('submit','#form-water', function(e) {
      e.preventDefault();
      postJSON('/api/config/general', {
        Water: $('#cfg-water').val() || '', coeffWater: parseFloat(dec('#cfg-coeffWater')) || 1,
        unitWater: $.trim($('#cfg-unitWater').val()) || 'L', tarifWater: dec('#cfg-tarifWater')
      }).then(saved('Configuration Eau enregistr&eacute;e'), failed);
    });
    $(document).off('submit','#form-presence').on('submit','#form-presence', function(e) {
      e.preventDefault();
      postJSON('/api/config/general', {
        Presence: $('#cfg-presence').val() || '',
        enablePresenceGraph: $('#cfg-presenceGraph').is(':checked')
      }).then(saved('Configuration Pr&eacute;sence enregistr&eacute;e'), failed);
    });

    // --- Sous-compteurs : liste, activation, couleur, suppression, ajout ---
    // Noms et IEEE construits avec .text() : ils viennent du reseau Zigbee.
    var sm = { list: [], max: 0, eligible: [], autoAlias: '' };

    function loadSubmeters() {
      $.getJSON('/api/energy/submeters').done(function(d) {
        d = d || {};
        sm.list = $.isArray(d.subMeters) ? d.subMeters : [];
        sm.max = Number(d.max) || 0;
        renderSubmeters();
      }).fail(function(xhr) {
        sm.list = [];
        $('#sm-list').empty().append($('<tr></tr>').append(
          $('<td colspan="5" class="text-danger"></td>').text(apiErrorText(xhr))));
      }).always(loadEligible);
    }

    function saveSubmeter(item, changes) {
      var p = { IEEE: item.IEEE, alias: item.alias || '', color: item.color || '#95a5a6', enabled: !!item.enabled };
      Object.keys(changes).forEach(function(k) { p[k] = changes[k]; });
      postJSON('/api/energy/submeters', p)
        .then(saved('Sous-compteur enregistr&eacute;'), failed)
        .always(loadSubmeters);
    }

    function renderSubmeters() {
      var $tb = $('#sm-list').empty();
      if (!sm.list.length) {
        $tb.append('<tr><td colspan="5" class="text-muted">Aucun sous-compteur configur&eacute;</td></tr>');
      }
      sm.list.forEach(function(item) {
        item = item || {};
        var $color = $('<input type="color" class="form-control form-control-color" title="Couleur">')
          .val(colorInputValue(item.color, '#95a5a6'))
          .on('change', function() { saveSubmeter(item, { color: $(this).val() }); });
        var $sw = $('<div class="form-check form-switch mb-0"><input class="form-check-input" type="checkbox" title="Actif"></div>');
        $sw.find('input').prop('checked', !!item.enabled)
          .on('change', function() { saveSubmeter(item, { enabled: $(this).is(':checked') }); });
        var $del = $('<button type="button" class="btn btn-sm btn-outline-danger">Supprimer</button>')
          .on('click', function() {
            if (!confirm('Supprimer le sous-compteur "' + (item.alias || item.IEEE) + '" ?')) return;
            postJSON('/api/energy/submeters/delete', { IEEE: item.IEEE })
              .then(saved('Sous-compteur supprim&eacute;'), failed)
              .always(loadSubmeters);
          });
        $tb.append($('<tr></tr>').append(
          $('<td></td>').text(item.alias || ''),
          $('<td class="small text-muted"></td>').text(item.IEEE || ''),
          $('<td></td>').append($color),
          $('<td></td>').append($sw),
          $('<td class="text-end"></td>').append($del)
        ));
      });
      $('#sm-count').text(sm.max ? '(' + sm.list.length + ' / ' + sm.max + ')' : '');
    }

    function loadEligible() {
      $.getJSON('/api/energy/submeters/eligible')
        .done(function(list) { sm.eligible = $.isArray(list) ? list : []; })
        .fail(function() { sm.eligible = []; })
        .always(renderEligible);
    }

    // Appareils compatibles pas encore declares comme sous-compteurs
    function renderEligible() {
      var used = {};
      sm.list.forEach(function(s) { if (s && s.IEEE) used[s.IEEE] = true; });
      var avail = sm.eligible.filter(function(e) { return e && e.IEEE && !used[e.IEEE]; });
      var $sel = $('#sm-ieee').empty();
      if (!avail.length) $sel.append($('<option value=""></option>').text('-- Aucun appareil compatible --'));
      avail.forEach(function(e) {
        var name = e.alias || e.model || e.IEEE;
        if (e.alias && e.model) name += ' (' + e.model + ')';
        $sel.append($('<option></option>').val(e.IEEE).text(name).attr('data-alias', e.alias || ''));
      });
      var full = sm.max > 0 && sm.list.length >= sm.max;
      $('#sm-full').toggle(full);
      $('#sm-add').prop('disabled', !avail.length || full);
      fillSubmeterAlias();
    }

    // Nom propose : alias de l'appareil, sauf si l'utilisateur en a saisi un
    function fillSubmeterAlias() {
      var cur = String($('#sm-alias').val() || '');
      if (cur && cur !== sm.autoAlias) return;
      sm.autoAlias = String($('#sm-ieee option:selected').attr('data-alias') || '');
      $('#sm-alias').val(sm.autoAlias);
    }
    $('#sm-ieee').on('change', fillSubmeterAlias);

    $(document).off('submit','#form-submeter').on('submit','#form-submeter', function(e) {
      e.preventDefault();
      var ieee = $('#sm-ieee').val();
      var alias = String($('#sm-alias').val() || '').trim();
      if (!ieee) { showAlert('S&eacute;lectionnez un appareil', 'danger'); return; }
      if (!alias) { showAlert('Indiquez un nom', 'danger'); return; }
      postJSON('/api/energy/submeters', { IEEE: ieee, alias: alias, color: $('#sm-color').val() || '#e74c3c', enabled: true })
        .then(function() {
          showAlert('Sous-compteur ajout&eacute;');
          $('#sm-alias').val('');
          sm.autoAlias = '';
        }, failed)
        .always(loadSubmeters);
    });

    loadSubmeters();

    // Decimal input setup
    $('.decimal-input').on('keypress', function(e) {
      var char = String.fromCharCode(e.which);
      if (!'0123456789.,'.includes(char)) e.preventDefault();
    });
  }


  // ============================================================================
  // PAGE: Config Horloge
  // ============================================================================
  function pageConfigHorloge() {
    setContent(
      '<div class="container py-4">' +
        '<h4>Config horloge</h4>' +
        '<div class="card p-4">' +
          '<form id="clock-form">' +
            // Current datetime display
            '<div class="mb-3"><label class="form-label fw-bold">Date/Heure actuelle</label><div id="clock-current" class="form-control-plaintext fs-5">--</div></div>' +

            // NTP server
            '<div class="mb-3"><label class="form-label" for="cfg-ntp">NTP serveur URL</label><input type="text" class="form-control" id="cfg-ntp" value="pool.ntp.org"></div>' +

            // Time offset
            '<div class="mb-3"><label class="form-label" for="cfg-timeoffset">Time Offset</label><input type="text" class="form-control" id="cfg-timeoffset" value="0"></div>' +

            // Timezone
            '<div class="mb-3"><label class="form-label" for="cfg-timezone">Time Zone</label><input type="text" class="form-control" id="cfg-timezone" value="1"></div>' +

            // Epoch time
            '<div class="mb-3"><label class="form-label" for="cfg-epoch">UTC epoch date</label><input type="text" class="form-control" id="cfg-epoch" readonly></div>' +

            // Reglage manuel (sans NTP)
            '<div class="mb-3"><label class="form-label" for="cfg-settime">R&eacute;gler la date et l\'heure</label>' +
              '<div class="input-group"><input type="datetime-local" class="form-control" id="cfg-settime">' +
              '<button type="button" class="btn btn-outline-primary" id="btn-settime">Appliquer</button>' +
              '<button type="button" class="btn btn-outline-secondary" id="btn-settime-now">Heure du navigateur</button></div>' +
              '<div class="form-text">Utile sans acc&egrave;s Internet : NTP reprend la main d&egrave;s qu\'il r&eacute;pond.</div></div>' +

            '<div class="d-flex justify-content-end"><button type="submit" class="btn btn-warning btn-lg">Enregistrer</button></div>' +
          '</form>' +
          '<div id="clock-reboot" class="mt-3" style="display:none;"><div class="spinner-border spinner-border-sm"></div> Red&eacute;marrage...</div>' +
        '</div>' +
      '</div>'
    );

    // Load config
    $.getJSON('/api/config/horloge', function(cfg) {
      $('#cfg-ntp').val(cfg.ntpserver || 'pool.ntp.org');
      $('#cfg-timeoffset').val(cfg.timeoffset != null ? cfg.timeoffset : 0);
      $('#cfg-timezone').val(cfg.timezone != null ? cfg.timezone : 1);
      if (cfg.epoch) { $('#cfg-epoch').val(cfg.epoch); }
      if (cfg.datetime) { $('#clock-current').text(cfg.datetime); }
    }).fail(function() {
      // Show browser time as fallback
      $('#clock-current').text($('#FormattedDate').text());
    });

    // Reglage manuel de l'heure
    function setDeviceTime(epoch) {
      postJSON('/api/system/time', { epoch: epoch }).then(function() {
        showAlert('Heure r&eacute;gl&eacute;e.');
        setTimeout(pageConfigHorloge, 1000);
        updateClock();
      }, function(xhr) { showAlert(apiErrorMsg(xhr), 'danger'); });
    }
    $('#btn-settime').on('click', function() {
      var v = $('#cfg-settime').val();
      var d = v ? new Date(v) : null;
      if (!d || isNaN(d.getTime())) { showAlert('Date invalide', 'warning'); return; }
      setDeviceTime(Math.floor(d.getTime() / 1000));
    });
    $('#btn-settime-now').on('click', function() { setDeviceTime(Math.floor(Date.now() / 1000)); });

    // Save
    $('#clock-form').on('submit', function(e) {
      e.preventDefault();
      // JSON type : le serveur parse du JSON et attend timeoffset en nombre.
      // L'epoch n'est plus envoyee : c'est un affichage, pas un reglage.
      // Les reglages NTP ne sont appliques qu'au demarrage : pas de
      // redemarrage automatique, l'utilisateur choisit le moment.
      $.ajax({
        url: '/saveConfigHorloge',
        method: 'POST',
        contentType: 'application/json',
        data: JSON.stringify({
          ntpserver: $('#cfg-ntp').val().trim(),
          timeoffset: parseInt($('#cfg-timeoffset').val(), 10) || 0,
          timezone: $('#cfg-timezone').val().trim()
        }),
        success: function() {
          showAlert('Horloge enregistr&eacute;e et appliqu&eacute;e.');
          setTimeout(pageConfigHorloge, 1500);
        },
        error: function(xhr) { showAlert(apiErrorMsg(xhr), 'danger'); }
      });
    });
  }

  // ============================================================================
  // PAGE: Config HTTP / Securite
  // ============================================================================
  function pageConfigHTTP() {
    setContent(
      '<div class="container py-4">' +
        '<h4>S&eacute;curit&eacute; HTTP</h4>' +
        '<div class="card p-4">' +
          '<form id="http-form">' +
            '<div class="mb-3"><div class="form-check form-switch"><input class="form-check-input" type="checkbox" id="cfg-auth"><label class="form-check-label" for="cfg-auth">Activer l\'authentification HTTP</label></div></div>' +
            '<div id="http-auth-fields">' +
              '<div class="mb-3"><label class="form-label">Utilisateur</label><input type="text" class="form-control" id="cfg-http-user"></div>' +
              '<div class="mb-3"><label class="form-label">Mot de passe</label><input type="password" class="form-control" id="cfg-http-pass"><div class="form-text">Minimum 4 caract&egrave;res. Laisser vide pour conserver le mot de passe actuel.</div></div>' +
            '</div>' +
            '<div id="http-error" class="text-danger mb-3" style="display:none;"></div>' +
            '<button type="submit" class="btn btn-primary">Enregistrer</button>' +
          '</form>' +
        '</div>' +
      '</div>'
    );

    // Load config (le mot de passe n'est jamais renvoye, seulement hasPassword)
    var hasPassword = false;
    $.getJSON('/api/config/http', function(cfg) {
      $('#cfg-auth').prop('checked', !!cfg.enabled);
      $('#cfg-http-user').val(cfg.user || '');
      hasPassword = !!cfg.hasPassword;
      if (hasPassword) { $('#cfg-http-pass').attr('placeholder', '********'); }
      // Tunnel distant actif : la securite HTTP est imposee (le serveur la
      // maintient de toute facon), l'interrupteur est grise
      if (cfg.lockedByTunnel) {
        $('#cfg-auth').prop('checked', true).prop('disabled', true);
        $('#cfg-auth').closest('.form-check, .form-switch, div').first().after(
          $('<div class="form-text text-warning">').append(
            document.createTextNode('Imposée par le tunnel distant actif ('),
            $('<a href="#configTunnel">').text('désactiver le tunnel'),
            document.createTextNode(' pour la retirer).')));
      }
    });

    // Save with validation
    $('#http-form').on('submit', function(e) {
      e.preventDefault();
      var enabled = $('#cfg-auth').is(':checked');
      var user = $('#cfg-http-user').val().trim();
      var pass = $('#cfg-http-pass').val();

      if (enabled) {
        var errors = [];
        if (!user) errors.push('Identifiant requis');
        if (!pass && !hasPassword) errors.push('Mot de passe requis');
        if (pass && pass.length < 4) errors.push('Mot de passe >= 4 caract\u00e8res');
        if (errors.length) {
          $('#http-error').html(errors.join('<br>')).show();
          return;
        }
      }
      $('#http-error').hide();

      // Envoi en JSON avec un vrai booleen : le serveur parse du JSON, et
      // l'ancien envoi en x-www-form-urlencoded avec 'on' etait rejete (400).
      // Un mot de passe vide signifie "conserver l'actuel".
      $.ajax({
        url: '/saveConfigHTTP',
        method: 'POST',
        contentType: 'application/json',
        data: JSON.stringify({ enableSecureHttp: enabled, userHTTP: user, passHTTP: pass }),
        success: function() {
          if (pass) hasPassword = true;
          $('#cfg-http-pass').val('');
          if (hasPassword) { $('#cfg-http-pass').attr('placeholder', '********'); }
          showAlert(enabled
            ? 'S\u00e9curit\u00e9 HTTP enregistr\u00e9e. Le navigateur va demander vos identifiants.'
            : 'S\u00e9curit\u00e9 HTTP enregistr\u00e9e');
        },
        error: function(xhr) {
          var msg = (xhr.responseJSON && xhr.responseJSON.message) || 'Erreur';
          showAlert(msg, 'danger');
        }
      });
    });
  }

  // ============================================================================
  // PAGE: Outils avances (/tools de la LiXee-Box, a l'identique). Menu affiche
  // en mode developpeur seulement (Config > General). Les editeurs de fichiers
  // de la Box (/fsbrowser, /hst, /tp, /rules, /debugFiles) sont ici la page
  // #files sur le dossier correspondant.
  // ============================================================================
  var TOOLS_CSS =
    '<style>' +
    '.tools-container{max-width:1200px;margin:0 auto;padding:20px;}' +
    '.tools-header{margin-bottom:30px;}' +
    '.tools-header h4{margin:0;font-size:24px;font-weight:600;color:#333;}' +
    '.tools-header p{color:#6c757d;margin-top:5px;}' +
    '.tools-section{margin-bottom:30px;}' +
    '.tools-section-title{font-size:14px;font-weight:600;color:#6c757d;text-transform:uppercase;letter-spacing:0.5px;margin-bottom:15px;padding-bottom:10px;border-bottom:1px solid #dee2e6;}' +
    '.tools-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:15px;}' +
    '.tool-card{display:flex;flex-direction:column;align-items:center;justify-content:center;padding:20px 15px;background:#fff;border-radius:12px;border:1px solid #dee2e6;text-decoration:none;color:#333;transition:all 0.2s ease;min-height:120px;}' +
    '.tool-card:hover{transform:translateY(-3px);box-shadow:0 8px 25px rgba(0,0,0,0.1);border-color:#0d6efd;text-decoration:none;color:#333;}' +
    '.tool-card:active{transform:translateY(0);}' +
    '.tool-card .icon{width:48px;height:48px;border-radius:12px;display:flex;align-items:center;justify-content:center;margin-bottom:12px;}' +
    '.tool-card .icon svg{width:24px;height:24px;}' +
    '.tool-card .icon.blue{background:#e7f1ff;color:#0d6efd;}' +
    '.tool-card .icon.green{background:#d1e7dd;color:#198754;}' +
    '.tool-card .icon.orange{background:#fff3cd;color:#fd7e14;}' +
    '.tool-card .icon.red{background:#f8d7da;color:#dc3545;}' +
    '.tool-card .icon.purple{background:#e2d9f3;color:#6f42c1;}' +
    '.tool-card .icon.cyan{background:#cff4fc;color:#0dcaf0;}' +
    '.tool-card .label{font-size:13px;font-weight:500;text-align:center;}' +
    '.tool-card .desc{font-size:11px;color:#6c757d;text-align:center;margin-top:4px;}' +
    '@media(max-width:576px){' +
    '.tools-grid{grid-template-columns:repeat(2,1fr);gap:10px;}' +
    '.tool-card{padding:15px 10px;min-height:100px;}' +
    '.tool-card .icon{width:40px;height:40px;margin-bottom:8px;}' +
    '.tool-card .icon svg{width:20px;height:20px;}' +
    '.tool-card .label{font-size:12px;}' +
    '.tool-card .desc{display:none;}' +
    '}' +
    '</style>';

  function toolCard(href, color, svg, label, desc, id) {
    return '<a href="' + href + '" class="tool-card"' + (id ? ' id="' + id + '"' : '') + '>' +
      '<div class="icon ' + color + '">' + svg + '</div>' +
      '<span class="label">' + label + '</span><span class="desc">' + desc + '</span></a>';
  }

  function pageTools() {
    var S16 = '<svg xmlns="http://www.w3.org/2000/svg" fill="currentColor" viewBox="0 0 16 16">';
    setContent(TOOLS_CSS +
      '<div class="tools-container">' +
        '<div class="tools-header"><h4>Outils avanc&eacute;s</h4></div>' +

        '<div class="tools-section">' +
          '<div class="tools-section-title">Fichiers &amp; Donn&eacute;es</div>' +
          '<div class="tools-grid">' +
            toolCard('#files?dir=db', 'blue', S16 + '<path d="M5.5 2A3.5 3.5 0 0 0 2 5.5v5A3.5 3.5 0 0 0 5.5 14h5a3.5 3.5 0 0 0 3.5-3.5V8a.5.5 0 0 1 1 0v2.5a4.5 4.5 0 0 1-4.5 4.5h-5A4.5 4.5 0 0 1 1 10.5v-5A4.5 4.5 0 0 1 5.5 1H8a.5.5 0 0 1 0 1z"/><path d="M16 3a3 3 0 1 1-6 0 3 3 0 0 1 6 0"/></svg>',
              'Appareils', 'Fichiers devices') +
            toolCard('#files?dir=hst', 'green', S16 + '<path d="M8.515 1.019A7 7 0 0 0 8 1V0a8 8 0 0 1 .589.022l-.074.997zm2.004.45a7.003 7.003 0 0 0-.985-.299l.219-.976c.383.086.76.2 1.126.342l-.36.933zm1.37.71a7.01 7.01 0 0 0-.439-.27l.493-.87a8.025 8.025 0 0 1 .979.654l-.615.789a6.996 6.996 0 0 0-.418-.302zm1.834 1.79a6.99 6.99 0 0 0-.653-.796l.724-.69c.27.285.52.59.747.91l-.818.576zm.744 1.352a7.08 7.08 0 0 0-.214-.468l.893-.45a7.976 7.976 0 0 1 .45 1.088l-.95.313a7.023 7.023 0 0 0-.179-.483zm.53 2.507a6.991 6.991 0 0 0-.1-1.025l.985-.17c.067.386.106.778.116 1.17l-1 .025zm-.131 1.538c.033-.17.06-.339.081-.51l.993.123a7.957 7.957 0 0 1-.23 1.155l-.964-.267c.046-.165.086-.332.12-.501zm-.952 2.379c.184-.29.346-.594.486-.908l.914.405c-.16.36-.345.706-.555 1.038l-.845-.535zm-.964 1.205c.122-.122.239-.248.35-.378l.758.653a8.073 8.073 0 0 1-.401.432l-.707-.707z"/><path d="M8 1a7 7 0 1 0 4.95 11.95l.707.707A8.001 8.001 0 1 1 8 0v1z"/><path d="M7.5 3a.5.5 0 0 1 .5.5v5.21l3.248 1.856a.5.5 0 0 1-.496.868l-3.5-2A.5.5 0 0 1 7 9V3.5a.5.5 0 0 1 .5-.5z"/></svg>',
              'Historique', 'Donn&eacute;es history') +
            toolCard('#files?dir=tp', 'purple', S16 + '<path d="M14 4.5V14a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V2a2 2 0 0 1 2-2h5.5L14 4.5zm-3 0A1.5 1.5 0 0 1 9.5 3V1H4a1 1 0 0 0-1 1v12a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1V4.5h-2z"/><path d="M4.5 12.5A.5.5 0 0 1 5 12h3a.5.5 0 0 1 0 1H5a.5.5 0 0 1-.5-.5zm0-2A.5.5 0 0 1 5 10h6a.5.5 0 0 1 0 1H5a.5.5 0 0 1-.5-.5zm1.639-3.708 1.33.886 1.854-1.855a.25.25 0 0 1 .289-.047l1.888.974V8.5a.5.5 0 0 1-.5.5H5a.5.5 0 0 1-.5-.5V8s1.54-1.274 1.639-1.208zM6.25 6a.75.75 0 1 0 0-1.5.75.75 0 0 0 0 1.5z"/></svg>',
              'Templates', 'Mod&egrave;les JSON') +
            toolCard('#files?dir=config&only=rules', 'orange', S16 + '<path d="M2 2a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2zm2-1a1 1 0 0 0-1 1v4h10V2a1 1 0 0 0-1-1zm9 6H6v2h7zm0 3H6v2h7zm0 3H6v2h6a1 1 0 0 0 1-1zm-8 2v-2H3v1a1 1 0 0 0 1 1zm-2-3h2v-2H3zm0-3h2V7H3z"/></svg>',
              'R&egrave;gles', 'Automatisations') +
          '</div>' +
        '</div>' +

        '<div class="tools-section">' +
          '<div class="tools-section-title">Syst&egrave;me</div>' +
          '<div class="tools-grid">' +
            toolCard('#files?dir=debug', 'cyan', '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="4 17 10 11 4 5"></polyline><line x1="12" y1="19" x2="20" y2="19"></line></svg>',
              'Debug', 'Console logs') +
            toolCard('#backup', 'purple', S16 + '<path fill-rule="evenodd" d="M7.646 5.146a.5.5 0 0 1 .708 0l2 2a.5.5 0 0 1-.708.708L8.5 6.707V10.5a.5.5 0 0 1-1 0V6.707L6.354 7.854a.5.5 0 1 1-.708-.708l2-2z"/><path d="M4.406 3.342A5.53 5.53 0 0 1 8 2c2.69 0 4.923 2 5.166 4.579C14.758 6.804 16 8.137 16 9.773 16 11.569 14.502 13 12.687 13H3.781C1.708 13 0 11.366 0 9.318c0-1.763 1.266-3.223 2.942-3.593.143-.863.698-1.723 1.464-2.383zm.653.757c-.757.653-1.153 1.44-1.153 2.056v.448l-.445.049C2.064 6.805 1 7.952 1 9.318 1 10.785 2.23 12 3.781 12h8.906C13.98 12 15 10.988 15 9.773c0-1.216-1.02-2.228-2.313-2.228h-.5v-.5C12.188 4.825 10.328 3 8 3a4.53 4.53 0 0 0-2.941 1.1z"/></svg>',
              'Sauvegarde', 'Backup config') +
            toolCard('#', 'red', S16 + '<path d="M11.534 7h3.932a.25.25 0 0 1 .192.41l-1.966 2.36a.25.25 0 0 1-.384 0l-1.966-2.36a.25.25 0 0 1 .192-.41zm-11 2h3.932a.25.25 0 0 0 .192-.41L2.692 6.23a.25.25 0 0 0-.384 0L.342 8.59A.25.25 0 0 0 .534 9z"/><path fill-rule="evenodd" d="M8 3c-1.552 0-2.94.707-3.857 1.818a.5.5 0 1 1-.771-.636A6.002 6.002 0 0 1 13.917 7H12.9A5.002 5.002 0 0 0 8 3zM3.1 9a5.002 5.002 0 0 0 8.757 2.182.5.5 0 1 1 .771.636A6.002 6.002 0 0 1 2.083 9H3.1z"/></svg>',
              'Red&eacute;marrer', 'Reboot syst&egrave;me', 'tool-reboot') +
            toolCard('#', 'orange', S16 + '<path d="M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z"/><path d="M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H5.5l1-1h3l1 1H14a1 1 0 0 1 .5.5V3zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z"/></svg>',
              'Fant&ocirc;mes', 'Nettoyer ZiGate', 'tool-ghosts') +
          '</div>' +
        '</div>' +
      '</div>'
    );
    $('#tool-reboot').on('click', function(e) {
      e.preventDefault();
      if (!confirm('Redémarrer l\'appareil ?')) return;
      $.post('/api/system/reboot', function() { showAlert('Redémarrage en cours...'); });
    });
    $('#tool-ghosts').on('click', function(e) {
      e.preventDefault();
      postJSON('/api/zigbee/cmd', { cmd: 'CleanGhosts' })
        .done(function() { alert('Nettoyage lancé'); })
        .fail(function(xhr) { showAlert(apiErrorMsg(xhr), 'danger'); });
    });
  }

  // Menu Outils : affiche en mode developpeur seulement, comme showTools() de
  // la LiXee-Box (masque par defaut dans index.html)
  function showTools(on) { $('#Tools').toggle(!!on); }

  // ============================================================================
  // PAGE: Editeur de fichiers (outils developpeur LiXee-Box : configFiles,
  // fsbrowser, hst, tp, rules, debugFiles, javascript)
  // ============================================================================
  var FILE_DIRS = [
    { dir: 'config', label: 'Configuration' },
    { dir: 'db',     label: 'Base des appareils' },
    { dir: 'hst',    label: 'Historiques' },
    { dir: 'tp',     label: 'Templates' },
    { dir: 'debug',  label: 'Debug' },
    { dir: 'bk',     label: 'Sauvegardes' },
    { dir: 'web/js', label: 'JavaScript' }
  ];

  // Editeurs des outils (fsbrowser, hst, tp, rules, debugFiles de la
  // LiXee-Box), meme presentation : liste des fichiers a gauche (recherche),
  // barre d'outils, editeur sombre, validation JSON et previsualisation.
  // Une variante par dossier, comme les pages de la Box.
  var FILE_EDITORS = {
    tp:     { title: 'Templates', item: 'template', search: true, create: true, importFile: true, exportFile: true,
              preview: 'template', format: true, save: true, del: true },
    db:     { title: 'Appareils', search: true, importFile: true, exportFile: true, preview: 'device', format: true, save: true, del: true },
    hst:    { title: 'Historique', search: true, importFile: true, exportFile: true, preview: 'history', format: true, save: true, del: true },
    config: { title: 'Configuration', preview: 'rules', format: true, save: true, masked: true },
    debug:  { title: 'Debug Files', search: true, readonly: true, del: true, delAll: true }
  };

  var FE_CSS =
    '<style>' +
    '.fe .fe-container{display:flex;height:calc(100vh - 120px);gap:0;overflow:hidden;}' +
    '.fe .fe-sidebar{width:280px;min-width:280px;background:#fff;border-right:1px solid #dee2e6;display:flex;flex-direction:column;transition:margin-left 0.3s;}' +
    '.fe .fe-sidebar.collapsed{margin-left:-280px;}' +
    '.fe .fe-sidebar-header{padding:15px;border-bottom:1px solid #dee2e6;background:#f8f9fa;}' +
    '.fe .fe-search{position:relative;}' +
    '.fe .fe-search input{width:100%;padding:8px 12px 8px 35px;border:1px solid #ced4da;border-radius:6px;font-size:14px;}' +
    '.fe .fe-list{flex:1;overflow-y:auto;padding:10px;}' +
    '.fe .fe-item{display:flex;align-items:center;padding:10px 12px;margin-bottom:6px;background:#f8f9fa;border-radius:8px;cursor:pointer;transition:all 0.2s;border:2px solid transparent;}' +
    '.fe .fe-item:hover{background:#e9ecef;transform:translateX(3px);}' +
    '.fe .fe-item.active{background:#e7f1ff;border-color:#0d6efd;}' +
    '.fe .fe-item .name{font-weight:500;font-size:14px;flex:1;word-break:break-all;}' +
    '.fe .fe-item .size{font-size:12px;color:#6c757d;margin-left:8px;white-space:nowrap;}' +
    '.fe .fe-main{flex:1;display:flex;flex-direction:column;overflow:hidden;background:#fff;}' +
    '.fe .fe-toolbar{display:flex;align-items:center;gap:10px;padding:12px 15px;background:#f8f9fa;border-bottom:1px solid #dee2e6;flex-wrap:wrap;}' +
    '.fe .fe-toolbar-title{font-weight:600;font-size:16px;margin-right:auto;}' +
    '.fe .fe-toolbar .btn{padding:6px 12px;font-size:13px;}' +
    '.fe .fe-editor-container{flex:1;display:flex;overflow:hidden;}' +
    '.fe .fe-editor-pane{flex:1;display:flex;flex-direction:column;overflow:hidden;min-width:0;}' +
    '.fe .fe-editor-pane.split{flex:0 0 50%;}' +
    '.fe .fe-preview-pane{flex:0 0 50%;border-left:1px solid #dee2e6;display:none;flex-direction:column;overflow:hidden;}' +
    '.fe .fe-preview-pane.visible{display:flex;}' +
    '.fe .fe-pane-header{padding:8px 15px;background:#e9ecef;font-weight:500;font-size:13px;border-bottom:1px solid #dee2e6;}' +
    '.fe .fe-editor-wrapper{flex:1;position:relative;overflow:hidden;}' +
    '.fe #fe-file{width:100%;height:100%;border:none;resize:none;padding:15px;font-family:Consolas,Monaco,monospace;font-size:13px;line-height:1.5;tab-size:2;background:#1e1e1e;color:#d4d4d4;}' +
    '.fe .fe-preview-content{flex:1;overflow-y:auto;padding:15px;}' +
    '.fe .model-section{margin-bottom:20px;border:1px solid #dee2e6;border-radius:8px;overflow:hidden;}' +
    '.fe .model-header{background:#f8f9fa;padding:10px 15px;font-weight:600;border-bottom:1px solid #dee2e6;display:flex;align-items:center;gap:10px;}' +
    '.fe .model-body{padding:15px;}' +
    '.fe .section-group{margin-bottom:15px;}' +
    '.fe .section-title{font-weight:500;color:#495057;margin-bottom:8px;display:flex;align-items:center;gap:8px;}' +
    '.fe .section-title .badge{font-size:11px;}' +
    '.fe .item-card{background:#f8f9fa;border-radius:6px;padding:10px;margin-bottom:8px;font-size:12px;word-break:break-all;}' +
    '.fe .item-card .label{color:#6c757d;font-size:11px;}' +
    '.fe .item-card .value{font-weight:500;}' +
    '.fe .rule-card{background:#f8f9fa;border-radius:6px;padding:10px;margin-bottom:8px;}' +
    '.fe .rule-name{font-weight:600;font-size:13px;}' +
    '.fe .rule-detail{font-size:12px;color:#6c757d;}' +
    '.fe .fe-validation{padding:10px 15px;background:#f8f9fa;border-top:1px solid #dee2e6;font-size:13px;}' +
    '.fe .validation-success{color:#198754;}' +
    '.fe .validation-error{color:#dc3545;}' +
    '.fe .fe-toggle-sidebar{display:none;position:fixed;bottom:20px;left:20px;z-index:1000;width:50px;height:50px;border-radius:50%;box-shadow:0 2px 10px rgba(0,0,0,0.2);}' +
    '.fe .empty-state{display:flex;flex-direction:column;align-items:center;justify-content:center;height:100%;color:#6c757d;gap:15px;}' +
    '.fe .empty-state svg{opacity:0.5;}' +
    // style.css impose svg{width:100%} : les icones gardent leur taille propre
    '.fe svg{width:auto;flex-shrink:0;}' +
    '.fe .is-invalid{border-color:#dc3545!important;background-color:#2d1f1f!important;}' +
    '.fe .fe-hidden{display:none!important;}' +
    '.fe .close-preview{display:none;background:none;border:none;color:#fff;font-size:24px;cursor:pointer;padding:0 5px;}' +
    '.fe .loading-overlay{position:absolute;top:0;left:0;right:0;bottom:0;background:rgba(30,30,30,0.95);display:flex;flex-direction:column;align-items:center;justify-content:center;z-index:10;}' +
    '.fe .loading-spinner{width:40px;height:40px;border:3px solid #444;border-top-color:#0d6efd;border-radius:50%;animation:fespin 1s linear infinite;}' +
    '.fe .loading-text{color:#aaa;margin-top:15px;font-size:14px;}' +
    '@keyframes fespin{to{transform:rotate(360deg);}}' +
    '@media(max-width:768px){' +
      '.fe .fe-sidebar{position:fixed;left:0;top:60px;height:calc(100vh - 60px);z-index:999;box-shadow:2px 0 10px rgba(0,0,0,0.1);}' +
      '.fe .fe-toggle-sidebar{display:flex;align-items:center;justify-content:center;}' +
      '.fe .fe-container{height:calc(100vh - 60px);}' +
      '.fe .fe-toolbar{gap:6px;padding:10px;}' +
      '.fe .fe-toolbar-title{width:100%;font-size:14px;margin-bottom:5px;}' +
      '.fe .fe-toolbar .btn{padding:5px 8px;font-size:12px;}' +
      '.fe .fe-editor-pane.split{flex:1;}' +
      '.fe .fe-preview-pane{position:fixed;top:0;left:0;right:0;bottom:0;z-index:1001;border:none;flex:none;width:100%;height:100%;background:#fff;}' +
      '.fe .fe-preview-pane .fe-pane-header{display:flex;justify-content:space-between;align-items:center;padding:12px 15px;background:#0d6efd;color:#fff;}' +
      '.fe .close-preview{display:block;}' +
    '}' +
    '</style>';

  var FE_ICONS = {
    search: '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" style="position:absolute;left:12px;top:50%;transform:translateY(-50%);color:#6c757d;" viewBox="0 0 16 16"><path d="M11.742 10.344a6.5 6.5 0 1 0-1.397 1.398h-.001c.03.04.062.078.098.115l3.85 3.85a1 1 0 0 0 1.415-1.414l-3.85-3.85a1.007 1.007 0 0 0-.115-.1zM12 6.5a5.5 5.5 0 1 1-11 0 5.5 5.5 0 0 1 11 0z"/></svg>',
    upload: '<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" fill="currentColor" class="me-1" viewBox="0 0 16 16"><path d="M.5 9.9a.5.5 0 0 1 .5.5v2.5a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-2.5a.5.5 0 0 1 1 0v2.5a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2v-2.5a.5.5 0 0 1 .5-.5z"/><path d="M7.646 1.146a.5.5 0 0 1 .708 0l3 3a.5.5 0 0 1-.708.708L8.5 2.707V11.5a.5.5 0 0 1-1 0V2.707L5.354 4.854a.5.5 0 1 1-.708-.708l3-3z"/></svg>',
    eye: '<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" fill="currentColor" viewBox="0 0 16 16"><path d="M16 8s-3-5.5-8-5.5S0 8 0 8s3 5.5 8 5.5S16 8 16 8zM1.173 8a13.133 13.133 0 0 1 1.66-2.043C4.12 4.668 5.88 3.5 8 3.5c2.12 0 3.879 1.168 5.168 2.457A13.133 13.133 0 0 1 14.828 8c-.058.087-.122.183-.195.288-.335.48-.83 1.12-1.465 1.755C11.879 11.332 10.119 12.5 8 12.5c-2.12 0-3.879-1.168-5.168-2.457A13.134 13.134 0 0 1 1.172 8z"/><path d="M8 5.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5zM4.5 8a3.5 3.5 0 1 1 7 0 3.5 3.5 0 0 1-7 0z"/></svg>',
    code: '<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" fill="currentColor" viewBox="0 0 16 16"><path d="M5.854 4.854a.5.5 0 1 0-.708-.708l-3.5 3.5a.5.5 0 0 0 0 .708l3.5 3.5a.5.5 0 0 0 .708-.708L2.707 8l3.147-3.146zm4.292 0a.5.5 0 0 1 .708-.708l3.5 3.5a.5.5 0 0 1 0 .708l-3.5 3.5a.5.5 0 0 1-.708-.708L13.293 8l-3.147-3.146z"/></svg>',
    download: '<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" fill="currentColor" viewBox="0 0 16 16"><path d="M.5 9.9a.5.5 0 0 1 .5.5v2.5a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-2.5a.5.5 0 0 1 1 0v2.5a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2v-2.5a.5.5 0 0 1 .5-.5z"/><path d="M7.646 11.854a.5.5 0 0 0 .708 0l3-3a.5.5 0 0 0-.708-.708L8.5 10.293V1.5a.5.5 0 0 0-1 0v8.793L5.354 8.146a.5.5 0 1 0-.708.708l3 3z"/></svg>',
    file: '<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48" fill="currentColor" viewBox="0 0 16 16"><path d="M14 4.5V14a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V2a2 2 0 0 1 2-2h5.5L14 4.5zm-3 0A1.5 1.5 0 0 1 9.5 3V1H4a1 1 0 0 0-1 1v12a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1V4.5h-2z"/></svg>',
    model: '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path d="M11 6a3 3 0 1 1-6 0 3 3 0 0 1 6 0z"/><path fill-rule="evenodd" d="M0 8a8 8 0 1 1 16 0A8 8 0 0 1 0 8zm8-7a7 7 0 0 0-5.468 11.37C3.242 11.226 4.805 10 8 10s4.757 1.225 5.468 2.37A7 7 0 0 0 8 1z"/></svg>',
    menu: '<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" fill="currentColor" viewBox="0 0 16 16"><path fill-rule="evenodd" d="M2.5 12a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5zm0-4a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5zm0-4a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5z"/></svg>'
  };

  function pageFiles() {
    var dir = routeParams.dir || 'config';
    var label = (FILE_DIRS.filter(function(d) { return d.dir === dir; })[0] || {}).label || dir;
    var cfg = FILE_EDITORS[dir] || { title: label, search: true, format: true, save: true, del: true, preview: 'device' };
    if (dir === 'config' && routeParams.only === 'rules') cfg = $.extend({}, cfg, { title: 'Règles' });
    var what = cfg.item || 'fichier';
    var cur = '', hasChanges = false, previewVisible = false;

    setContent(FE_CSS +
      '<div class="fe"><div class="fe-container">' +
        '<div class="fe-sidebar" id="fe-sidebar">' +
          '<div class="fe-sidebar-header">' +
            '<div class="d-flex justify-content-between align-items-center' + (cfg.search ? ' mb-2' : '') + '">' +
              '<h5 class="mb-0">' + escHtml(cfg.title) + '</h5>' +
              (cfg.create ? '<button class="btn btn-primary btn-sm" id="fe-new">+ Nouveau</button>' : '') +
            '</div>' +
            (cfg.search ? '<div class="fe-search">' + FE_ICONS.search + '<input type="text" id="fe-search" placeholder="Rechercher..."></div>' : '') +
          '</div>' +
          '<div class="fe-list" id="fe-list"></div>' +
          (cfg.importFile ? '<div class="p-2 border-top"><button class="btn btn-outline-secondary btn-sm w-100" id="fe-import">' + FE_ICONS.upload + 'Importer</button></div>' : '') +
          (cfg.delAll ? '<div class="p-2 border-top"><button class="btn btn-danger btn-sm w-100" id="fe-delall">Tout supprimer</button></div>' : '') +
        '</div>' +
        '<div class="fe-main">' +
          '<div class="fe-toolbar">' +
            '<span class="fe-toolbar-title" id="fe-current">Sélectionnez un ' + what + '</span>' +
            '<div id="fe-actions" class="fe-hidden">' +
              (cfg.preview ? '<button class="btn btn-outline-secondary btn-sm" id="fe-btn-preview" title="Prévisualisation">' + FE_ICONS.eye + '</button> ' : '') +
              (cfg.format ? '<button class="btn btn-outline-secondary btn-sm" id="fe-btn-format" title="Formater JSON">' + FE_ICONS.code + '</button> ' : '') +
              (cfg.exportFile ? '<button class="btn btn-outline-secondary btn-sm" id="fe-btn-export" title="Exporter">' + FE_ICONS.download + '</button> ' : '') +
              (cfg.save ? '<button class="btn btn-warning btn-sm" id="fe-btn-save">Enregistrer</button> ' : '') +
              (cfg.del ? '<button class="btn btn-danger btn-sm" id="fe-btn-delete">Supprimer</button>' : '') +
            '</div>' +
          '</div>' +
          '<div class="fe-editor-container">' +
            '<div class="fe-editor-pane" id="fe-editor-pane">' +
              '<div class="fe-pane-header">' + (cfg.readonly ? 'Contenu' : 'Éditeur JSON') + '</div>' +
              '<div class="fe-editor-wrapper"><textarea id="fe-file" spellcheck="false"' + (cfg.readonly ? ' readonly' : '') +
                ' placeholder="Sélectionnez un ' + what + ' dans la liste..."></textarea></div>' +
              '<div class="fe-validation" id="fe-validation"><span id="fe-status"></span></div>' +
            '</div>' +
            (cfg.preview ? '<div class="fe-preview-pane" id="fe-preview-pane">' +
              '<div class="fe-pane-header"><span>Prévisualisation</span><button class="close-preview" id="fe-close-preview">&times;</button></div>' +
              '<div class="fe-preview-content" id="fe-preview"><div class="empty-state">' + FE_ICONS.file + '<span>Sélectionnez un ' + what + '</span></div></div>' +
            '</div>' : '') +
          '</div>' +
        '</div>' +
      '</div>' +
      '<button class="btn btn-primary fe-toggle-sidebar" id="fe-toggle">' + FE_ICONS.menu + '</button>' +
      '<input type="file" id="fe-import-file" accept=".json" style="display:none">' +
      '</div>');

    function q(name) {
      return 'dir=' + encodeURIComponent(dir) + (name ? '&name=' + encodeURIComponent(name) : '');
    }
    function isJson(name) { return /\.json$/i.test(name || ''); }
    function status(html) { $('#fe-status').html(html); }

    function loadList(select) {
      $.getJSON('/api/files?' + q()).done(function(data) {
        var $l = $('#fe-list').empty();
        var files = (data.files || []).sort(function(a, b) { return a.name < b.name ? -1 : 1; });
        // Editeur "Regles" (rules de la LiXee-Box) : rules.json seul
        if (routeParams.only === 'rules') files = files.filter(function(f) { return f.name === 'rules.json'; });
        if (!files.length) $l.append($('<div class="text-muted p-2">').text('Aucun fichier'));
        files.forEach(function(f) {
          $l.append($('<div class="fe-item">').attr('data-file', f.name).toggleClass('active', f.name === cur)
            .append($('<span class="name">').text(f.name), $('<span class="size">').text(f.size + ' o'))
            .on('click', function() { selectFile(f.name); }));
        });
        filter();
        if (select) selectFile(select, true);
      }).fail(function(xhr) {
        $('#fe-list').empty().append($('<div class="text-danger p-2">').text(apiErrorText(xhr)));
      });
    }

    function filter() {
      var s = ($('#fe-search').val() || '').toLowerCase();
      $('#fe-list .fe-item').each(function() {
        $(this).toggle(String($(this).attr('data-file')).toLowerCase().indexOf(s) > -1);
      });
    }

    function selectFile(name, force) {
      if (!force && hasChanges && !confirm('Modifications non sauvegardées. Continuer ?')) return;
      $('#fe-list .fe-item').removeClass('active');
      $('#fe-list .fe-item').filter(function() { return $(this).attr('data-file') === name; }).addClass('active');
      cur = name;
      $('#fe-current').text(name);
      $('#fe-actions').removeClass('fe-hidden');
      $('#fe-file').val('');
      status('');
      $('.fe-editor-wrapper').append('<div class="loading-overlay" id="fe-loader"><div class="loading-spinner"></div>' +
        '<div class="loading-text">Chargement de ' + escHtml(name) + '...</div></div>');
      $.ajax({ url: '/api/files/read?' + q(name), dataType: 'text' }).done(function(txt) {
        $('#fe-loader').remove();
        $('#fe-file').val(txt).removeClass('is-invalid');
        validate();
        if (previewVisible) updatePreview();
        hasChanges = false;
      }).fail(function(xhr) {
        $('#fe-loader').remove();
        status('<span class="validation-error">' + escHtml(apiErrorText(xhr) || 'Erreur de chargement') + '</span>');
      });
      if (window.innerWidth < 768) $('#fe-sidebar').addClass('collapsed');
    }

    // Validation JSON (ligne de l'erreur, comme la LiXee-Box)
    function validate() {
      var content = $('#fe-file').val();
      if (cfg.readonly || !isJson(cur) || !content.trim()) { status(''); return null; }
      try {
        var json = JSON.parse(content);
        $('#fe-file').removeClass('is-invalid');
        var msg = '✓ JSON valide';
        if (cfg.preview === 'template') {
          var models = Object.keys(json).length, sc = 0, ac = 0;
          for (var k in json) { if (json[k] && json[k][0]) { sc += (json[k][0].status || []).length; ac += (json[k][0].action || []).length; } }
          msg += ' - ' + models + ' modèle(s), ' + sc + ' status, ' + ac + ' actions';
        }
        if (cfg.masked && content.indexOf('********') >= 0) {
          msg += ' — mots de passe masqués : laissez <code>********</code> pour les conserver';
        }
        status('<span class="validation-success">' + msg + '</span>');
        return json;
      } catch (e) {
        $('#fe-file').addClass('is-invalid');
        var m = e.message, pos = m.match(/position (\d+)/);
        if (pos) m += ' (ligne ' + content.substr(0, parseInt(pos[1], 10)).split('\n').length + ')';
        status('<span class="validation-error">✗ ' + escHtml(m) + '</span>');
        return null;
      }
    }

    function card(k, v) {
      return '<div class="item-card"><span class="label">' + escHtml(k) + ':</span> <span class="value">' + escHtml(v) + '</span></div>';
    }

    function updatePreview() {
      if (!cfg.preview) return;
      var json = validate();
      if (!json) { $('#fe-preview').html('<div class="empty-state"><span class="text-danger">JSON invalide</span></div>'); return; }
      var h = '';
      if (cfg.preview === 'template') {
        for (var model in json) {
          var data = (json[model] && json[model][0]) || {};
          h += '<div class="model-section"><div class="model-header">' + FE_ICONS.model + escHtml(model) + '</div><div class="model-body">';
          if (data.status && data.status.length) {
            h += '<div class="section-group"><div class="section-title"><span>Status</span><span class="badge bg-primary">' + data.status.length + '</span></div>';
            data.status.forEach(function(s) {
              h += '<div class="item-card"><div class="value">' + escHtml(s.name) + '</div><div class="label">Cluster: ' + escHtml(s.cluster) +
                ' | Attr: ' + escHtml(s.attribut) + (s.unit ? ' | ' + escHtml(s.unit) : '') + '</div></div>';
            });
            h += '</div>';
          }
          if (data.action && data.action.length) {
            h += '<div class="section-group"><div class="section-title"><span>Actions</span><span class="badge bg-success">' + data.action.length + '</span></div>';
            data.action.forEach(function(a) {
              h += '<div class="item-card"><div class="value">' + escHtml(a.name) + '</div><div class="label">Cmd: ' + escHtml(a.command) +
                (a.endpoint ? ' | EP: ' + escHtml(a.endpoint) : '') + '</div></div>';
            });
            h += '</div>';
          }
          if (data.report && data.report.length) {
            h += '<div class="section-group"><div class="section-title"><span>Report</span><span class="badge bg-warning text-dark">' + data.report.length + '</span></div>';
            data.report.forEach(function(r) {
              h += '<div class="item-card"><div class="label">Cluster: ' + escHtml(r.cluster) + ' | Attr: ' + escHtml(r.attribut) +
                ' | Min: ' + escHtml(r.min) + 's Max: ' + escHtml(r.max) + 's</div></div>';
            });
            h += '</div>';
          }
          if (data.bind) h += '<div class="section-group"><div class="section-title"><span>Bind</span></div><div class="item-card"><div class="label">' + escHtml(data.bind) + '</div></div></div>';
          h += '</div></div>';
        }
      } else if (cfg.preview === 'rules' && json.rules && json.rules.length) {
        json.rules.forEach(function(rule, i) {
          var c = rule.conditions ? rule.conditions.length : 0, a = rule.actions ? rule.actions.length : 0;
          h += '<div class="rule-card"><div class="rule-name">' + escHtml(rule.name || ('Règle ' + (i + 1))) + '</div>' +
            '<div class="rule-detail">' + c + ' condition(s), ' + a + ' action(s)</div>' +
            (rule.enabled === false ? '<div class="rule-detail text-warning">Désactivée</div>' : '') + '</div>';
        });
      } else {
        // Appareils / historique / autres : arbre a deux niveaux
        h += '<div class="model-section"><div class="model-header">' + (cfg.preview === 'history' ? 'History Data' : 'Device Info') + '</div><div class="model-body">';
        if (Array.isArray(json)) {
          json.forEach(function(item, i) { h += card('#' + (i + 1), JSON.stringify(item)); });
        } else {
          for (var key in json) {
            if (json[key] !== null && typeof json[key] === 'object') {
              h += '<div class="section-group"><div class="section-title">' + escHtml(key) + '</div>';
              for (var k2 in json[key]) h += card(k2, JSON.stringify(json[key][k2]));
              h += '</div>';
            } else {
              h += card(key, String(json[key]));
            }
          }
        }
        h += '</div></div>';
      }
      $('#fe-preview').html(h || '<div class="empty-state"><span>Rien à afficher</span></div>');
    }

    function togglePreview() {
      previewVisible = !previewVisible;
      $('#fe-preview-pane').toggleClass('visible', previewVisible);
      $('#fe-editor-pane').toggleClass('split', previewVisible);
      $('#fe-btn-preview').toggleClass('btn-primary btn-outline-secondary');
      if (previewVisible) updatePreview();
    }

    function write(name, content) {
      return $.ajax({ url: '/api/files/write?' + q(name), method: 'POST', contentType: 'text/plain', data: content });
    }

    function validName(name) { return /^[A-Za-z0-9._-]{1,64}$/.test(name) && name.indexOf('..') < 0; }

    var $c = $('#page-content');
    $c.off('.fe');                  // gestionnaires d'une visite precedente
    $c.on('input.fe', '#fe-search', filter);
    $c.on('click.fe', '#fe-toggle', function() { $('#fe-sidebar').toggleClass('collapsed'); });
    $c.on('input.fe', '#fe-file', function() { hasChanges = true; validate(); if (previewVisible) updatePreview(); });
    $c.on('click.fe', '#fe-btn-preview, #fe-close-preview', togglePreview);
    $c.on('click.fe', '#fe-btn-format', function() {
      try {
        $('#fe-file').val(JSON.stringify(JSON.parse($('#fe-file').val()), null, 2));
        validate();
        hasChanges = true;
      } catch (e) { alert('JSON invalide: ' + e.message); }
    });
    $c.on('click.fe', '#fe-btn-export', function() {
      var blob = new Blob([$('#fe-file').val()], { type: 'application/json' });
      var a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = cur || 'fichier.json';
      a.click();
    });
    $c.on('click.fe', '#fe-btn-save', function() {
      if (isJson(cur) && !validate()) { alert('Corrigez les erreurs JSON avant de sauvegarder'); return; }
      write(cur, $('#fe-file').val()).done(function(r) {
        hasChanges = false;
        showAlert(r && r.rebootRequired === false ? 'Enregistré et appliqué' : 'Enregistré — redémarrez pour appliquer');
        loadList();
      }).fail(function(xhr) { alert('Erreur: ' + apiErrorText(xhr)); });
    });
    $c.on('click.fe', '#fe-btn-delete', function() {
      if (!cur || !confirm('Supprimer ' + cur + ' ?')) return;
      $.ajax({ url: '/api/files/delete?' + q(cur), method: 'POST' }).done(function() {
        cur = ''; hasChanges = false;
        $('#fe-file').val(''); $('#fe-current').text('Sélectionnez un ' + what);
        $('#fe-actions').addClass('fe-hidden'); status('');
        loadList();
      }).fail(function(xhr) { alert('Erreur: ' + apiErrorText(xhr)); });
    });
    $c.on('click.fe', '#fe-delall', function() {
      var names = $('#fe-list .fe-item').map(function() { return $(this).attr('data-file'); }).get();
      if (!names.length || !confirm('Supprimer les ' + names.length + ' fichier(s) ?')) return;
      var next = function() {
        if (!names.length) { cur = ''; $('#fe-file').val(''); $('#fe-actions').addClass('fe-hidden'); loadList(); return; }
        $.ajax({ url: '/api/files/delete?' + q(names.shift()), method: 'POST' }).always(next);
      };
      next();
    });
    $c.on('click.fe', '#fe-new', function() {
      var name = prompt('Nom du nouveau ' + what + ' (ex: 123.json):');
      if (!name) return;
      if (!/\.json$/i.test(name)) name += '.json';
      if (!validName(name)) { alert('Nom invalide (lettres, chiffres, . _ - uniquement)'); return; }
      var tpl = '{\n  "default": [\n    {\n      "status": [],\n      "action": [],\n      "bind": "",\n      "report": []\n    }\n  ]\n}';
      write(name, tpl).done(function() { loadList(name); }).fail(function(xhr) { alert('Erreur: ' + apiErrorText(xhr)); });
    });
    $c.on('click.fe', '#fe-import', function() { $('#fe-import-file').click(); });
    $c.on('change.fe', '#fe-import-file', function(e) {
      var file = e.target.files[0];
      if (!file) return;
      var reader = new FileReader();
      reader.onload = function(ev) {
        try {
          JSON.parse(ev.target.result);
          var name = prompt('Nom du fichier:', file.name);
          if (!name) return;
          if (!/\.json$/i.test(name)) name += '.json';
          if (!validName(name)) { alert('Nom invalide (lettres, chiffres, . _ - uniquement)'); return; }
          write(name, ev.target.result).done(function() { loadList(name); }).fail(function(xhr) { alert('Erreur: ' + apiErrorText(xhr)); });
        } catch (err) { alert('JSON invalide: ' + err.message); }
      };
      reader.readAsText(file);
      e.target.value = '';
    });

    loadList();
  }

  // Texte brut d'une erreur d'API (pour .text(), sans echappement HTML)
  function apiErrorText(xhr) {
    return (xhr && xhr.responseJSON && xhr.responseJSON.message) ||
           (xhr && xhr.responseText && xhr.responseText.length < 160 ? xhr.responseText : 'Erreur');
  }

  function _toolCard(label, icon, onclick) {
    return '<div class="col-md-4"><div class="card p-3 text-center" style="cursor:pointer;" onclick="' + onclick + '"><svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" fill="currentColor" class="bi ' + icon + ' mb-2" viewBox="0 0 16 16"></svg><strong>' + label + '</strong></div></div>';
  }

  // ============================================================================
  // PAGE: Notifications (with pagination, mark read, delete, clear)
  // ============================================================================
  var _notifPage = 1, _notifPerPage = 10;

  function _loadNotifications() {
    $.getJSON('/api/notifications?page=' + _notifPage + '&limit=' + _notifPerPage, function(data) {
      var notifs = data.notifications || data || [];
      var total = data.total || notifs.length;
      if (!notifs.length) {
        $('#notif-list').html('<div class="alert alert-info">Aucune notification.</div>');
        $('#notif-pagination').html('');
        return;
      }
      var html = '';
      for (var i = 0; i < notifs.length; i++) {
        var n = notifs[i];
        var bgClass = n.read ? '' : 'border-start border-primary border-4';
        var typeIcon = n.type === 'warning' ? '<span class="badge bg-warning text-dark">Alerte</span>' :
                       n.type === 'update' ? '<span class="badge bg-info">MAJ</span>' :
                       '<span class="badge bg-secondary">Info</span>';
        html += '<div class="card mb-2 ' + bgClass + '"><div class="card-body d-flex justify-content-between align-items-start">' +
          '<div>' + typeIcon + ' <strong>' + _escHtml(n.title || '') + '</strong><br>' +
          '<span>' + _escHtml(n.message || '') + '</span><br>' +
          '<small class="text-muted">' + (n.timestamp || '') + '</small></div>' +
          '<div class="d-flex gap-1">' +
          (!n.read ? '<button class="btn btn-sm btn-outline-primary btn-notif-read" data-id="' + n.id + '" title="Marquer comme lu">Lu</button>' : '') +
          '<button class="btn btn-sm btn-outline-danger btn-notif-del" data-id="' + n.id + '">X</button>' +
          '</div></div></div>';
      }
      $('#notif-list').html(html);

      // Pagination
      var pages = Math.ceil(total / _notifPerPage);
      var phtml = '';
      for (var p = 1; p <= pages; p++) {
        phtml += '<li class="page-item' + (p === _notifPage ? ' active' : '') + '"><a class="page-link notif-page-link" href="#" data-page="' + p + '">' + p + '</a></li>';
      }
      $('#notif-pagination').html(phtml);
    }).fail(function() {
      $('#notif-list').html('<div class="alert alert-info">Aucune notification.</div>');
    });
  }

  function _escHtml(s) {
    var d = document.createElement('div');
    d.appendChild(document.createTextNode(s));
    return d.innerHTML;
  }

  // ============================================================================
  // PAGE: Help / Info
  // ============================================================================
  function pageHelp() {
    setContent(
      '<div class="container p-4">' +
        '<h4>A propos</h4>' +
        '<div class="card mx-auto shadow-sm">' +
          '<div class="card-body">' +
            '<h5>Version firmware : <span id="help-version">--</span></h5>' +
            '<div id="help-update-badge" class="mb-3" style="display:none;"></div>' +
            '<hr>' +
            '<h5>Boutique</h5>' +
            '<p>Visitez la boutique LiXee :<br>' +
            '<a href="https://lixee.fr/" target="_blank" class="btn btn-sm btn-outline-primary mt-1">Boutique LiXee</a></p>' +
            '<h5>Sources Firmware</h5>' +
            '<p>Code source sur GitHub :<br>' +
            '<a href="https://github.com/fairecasoimeme/LiXee-Gateway" target="_blank" class="btn btn-sm btn-outline-dark mt-1">Sources GitHub</a></p>' +
          '</div>' +
        '</div>' +
      '</div>'
    );
    $.getJSON('/api/system/info', function(data) {
      var ver = data.version || '--';
      $('#help-version').text(ver);
      // Pas de verification de mise a jour : ce firmware n'a pas d'OTA, et le
      // depot interroge (LiXee-Gateway) est celui d'un autre produit, dont les
      // numeros de version ne se comparent pas a celui-ci.
    });
  }

  // ============================================================================
  // PAGE: Systeme
  // ============================================================================
  // Etat des passerelles (MQTT, WebPush, Matter, tunnel) de la page Systeme
  function loadGatewayStatus() {
    function j(u) {
      var d = $.Deferred();
      $.getJSON(u).done(function(r) { d.resolve(r || null); }).fail(function() { d.resolve(null); });
      return d.promise();
    }
    function badge(cls, txt) { return '<span class="badge bg-' + cls + '">' + txt + '</span>'; }
    function off() { return badge('secondary', 'D&eacute;sactiv&eacute;'); }
    function row(name, href, state, detail) {
      return '<tr><td><strong><a href="' + href + '">' + name + '</a></strong></td><td>' + state + '</td>' +
             '<td class="text-muted small">' + (detail || '') + '</td></tr>';
    }
    function sendRow(name, href, st) {
      if (!st) return row(name, href, badge('secondary', 'Inconnu'), '');
      if (!st.enabled) return row(name, href, off(), '');
      if (!st.configured) return row(name, href, badge('warning', 'Non configur&eacute;'), '');
      var det = st.sent + ' envoi(s), ' + st.failed + ' &eacute;chec(s)';
      return row(name, href, (st.failed > 0 && !st.sent) ? badge('danger', 'Erreurs') : badge('success', 'Actif'), det);
    }
    $.when(j('/api/mqtt/status'), j('/api/notifications/send-status'), j('/api/matter/status'), j('/api/tunnel/status'))
      .done(function(mq, ns, mt, tn) {
        var h = '<table class="table table-sm mb-0 align-middle">';
        // MQTT
        if (!mq) h += row('MQTT', '#mqtt', badge('secondary', 'Inconnu'), '');
        else if (!mq.enabled) h += row('MQTT', '#mqtt', off(), '');
        else h += row('MQTT', '#mqtt', mq.connected ? badge('success', 'Connect&eacute;') : badge('danger', 'D&eacute;connect&eacute;'),
                      escHtml((mq.server || '') + (mq.port ? ':' + mq.port : '')));
        // WebPush (l'email n'est pas propose sur la LiXee-Box : pas de ligne)
        h += sendRow('WebPush', '#webpush', ns && ns.webpush);
        // Matter
        if (!mt) h += row('Matter', '#matter', badge('secondary', 'Inconnu'), '');
        else if (!mt.enabled) h += row('Matter', '#matter', off(), '');
        else {
          var st = !mt.started ? badge('danger', 'Arr&ecirc;t&eacute;') :
                   (mt.commissioned ? badge('success', 'Appair&eacute;') : badge('warning', 'Non appair&eacute;'));
          h += row('Matter', '#matter', st, mt.bridgedDevices != null ? mt.bridgedDevices + ' / ' + mt.maxDevices + ' appareils pont&eacute;s' : '');
        }
        // Tunnel distant
        if (tn) {
          if (!tn.enabled) h += row('Tunnel distant', '#configTunnel', off(), '');
          else h += row('Tunnel distant', '#configTunnel', tn.connected ? badge('success', 'Connect&eacute;') : badge('danger', 'D&eacute;connect&eacute;'),
                        escHtml(tn.subdomain || tn.lastError || ''));
        }
        h += '</table>';
        $('#sys-gw').html(h);
      });
  }

  function pageSystem() {
    setContent(
      '<div class="container py-4">' +
        '<h4>Informations syst&egrave;me</h4>' +
        '<div class="card p-4 mb-4">' +
          '<div id="sys-info"><div class="text-center"><div class="spinner-border" role="status"></div></div></div>' +
        '</div>' +
        '<div class="card p-4 mb-4">' +
          '<h5>R&eacute;seau</h5>' +
          '<div id="sys-net"><div class="text-center"><div class="spinner-border spinner-border-sm" role="status"></div></div></div>' +
        '</div>' +
        '<div class="card p-4 mb-4">' +
          '<h5>Passerelles</h5>' +
          '<div id="sys-gw"><div class="text-center"><div class="spinner-border spinner-border-sm" role="status"></div></div></div>' +
        '</div>' +
        '<div class="card p-4">' +
          '<h5>Actions</h5>' +
          '<button class="btn btn-warning" id="sys-reboot">Red&eacute;marrer</button>' +
        '</div>' +
      '</div>'
    );

    $.getJSON('/api/system/info', function(data) {
      var html = '<table class="table table-sm">';
      html += '<tr><td><strong>Version</strong></td><td>' + (data.version || '--') + '</td></tr>';
      if (data.chip) html += '<tr><td><strong>Chip</strong></td><td>' + data.chip + '</td></tr>';
      if (data.sdkVersion) html += '<tr><td><strong>SDK</strong></td><td>' + data.sdkVersion + '</td></tr>';
      html += '<tr><td><strong>Heap libre</strong></td><td>' + (data.freeHeap ? Math.round(data.freeHeap / 1024) + ' KB' : '--') + '</td></tr>';
      html += '<tr><td><strong>Heap min</strong></td><td>' + (data.minFreeHeap ? Math.round(data.minFreeHeap / 1024) + ' KB' : '--') + '</td></tr>';
      if (data.psramSize) html += '<tr><td><strong>PSRAM total</strong></td><td>' + Math.round(data.psramSize / 1024) + ' KB</td></tr>';
      if (data.psramFree) html += '<tr><td><strong>PSRAM libre</strong></td><td>' + Math.round(data.psramFree / 1024) + ' KB</td></tr>';
      if (data.flashSize) html += '<tr><td><strong>Flash</strong></td><td>' + Math.round(data.flashSize / (1024 * 1024)) + ' MB</td></tr>';
      if (data.uptime) {
        var s = data.uptime;
        var d = Math.floor(s / 86400); s %= 86400;
        var h = Math.floor(s / 3600); s %= 3600;
        var m = Math.floor(s / 60);
        html += '<tr><td><strong>Uptime</strong></td><td>' + (d > 0 ? d + 'j ' : '') + h + 'h ' + m + 'min</td></tr>';
      }
      if (data.internalFree !== undefined) {
        html += '<tr><td><strong>RAM interne libre</strong></td><td>' + Math.round(data.internalFree / 1024) +
                ' KB (min ' + Math.round(data.internalMinFree / 1024) + ' KB)</td></tr>';
      }
      if (data.resetReason) html += '<tr><td><strong>Dernier red&eacute;marrage</strong></td><td>' + escHtml(data.resetReason) + '</td></tr>';
      if (data.temperature !== undefined) html += '<tr><td><strong>Temp&eacute;rature puce</strong></td><td>' + data.temperature + ' &deg;C</td></tr>';
      if (data['interface']) html += '<tr><td><strong>Interface</strong></td><td>' + (data['interface'] === 'ethernet' ? 'Ethernet' : 'WiFi') + '</td></tr>';
      if (data.hostname) html += '<tr><td><strong>Nom r&eacute;seau</strong></td><td>' + escHtml(data.hostname) + '.local</td></tr>';
      html += '<tr><td><strong>SSID</strong></td><td>' + escHtml(data.ssid || '--') + '</td></tr>';
      html += '<tr><td><strong>RSSI</strong></td><td id="sys-rssi">' + (data.rssi || '--') + ' dBm</td></tr>';
      if (data.ip) html += '<tr><td><strong>IP</strong></td><td>' + data.ip + '</td></tr>';
      if (data.mac) html += '<tr><td><strong>MAC WiFi</strong></td><td>' + data.mac + '</td></tr>';
      html += '</table>';
      $('#sys-info').html(html);
    }).fail(function() {
      $('#sys-info').html('<div class="alert alert-danger">Erreur de connexion.</div>');
    });

    loadGatewayStatus();

    // Reseau (/api/system de la LiXee-Box) et signal WiFi rafraichi en direct
    $.getJSON('/api/system', function(sys) {
      var n = (sys && sys.network) || {}, w = n.wifi || {}, e = n.ethernet || {};
      function yes(v) { return v ? 'Oui' : 'Non'; }
      function card(title, active, rows) {
        var h = '<div class="col-md-6 mb-2"><div class="border rounded p-2' + (active ? ' border-success' : '') + '" style="' + (active ? '' : 'opacity:.6;') + '">' +
                '<strong>' + title + '</strong>' + (active ? ' <span class="badge bg-success">actif</span>' : '') +
                '<table class="table table-sm mb-0 mt-1">';
        rows.forEach(function(r) { h += '<tr><td class="text-muted">' + r[0] + '</td><td>' + (r[2] ? r[1] : escHtml(String(r[1]))) + '</td></tr>'; });
        return h + '</table></div></div>';
      }
      var html = '<div class="row">' +
        card('WiFi', n.active === 'wifi', [
          ['Activ\u00e9', yes(w.enable)], ['Connect\u00e9', yes(w.connected)], ['DHCP', yes(w.mode)],
          ['IP', w.ip || '-'], ['Masque', w.netmask || '-'], ['Passerelle', w.gateway || '-'],
          ['Qualit\u00e9', '<span id="sys-q">-</span>', true], ['Puissance \u00e9mise', '<span id="sys-tx">-</span>', true]]) +
        card('Ethernet', n.active === 'ethernet', [
          ['Pr\u00e9sent', yes(e.present)], ['Connect\u00e9', yes(e.connected)], ['DHCP', yes(e.dhcp)],
          ['IP', e.ip || '-'], ['Masque', e.netmask || '-'], ['Passerelle', e.gateway || '-']]) +
        '</div>';
      $('#sys-net').html(html);
      refreshSignal();
    }).fail(function() { $('#sys-net').html('<div class="text-muted">Indisponible</div>'); });

    function refreshSignal() {
      $.ajax({ url: '/api/network/wifi-signal', dataType: 'json', cache: false, global: false }).done(function(sg) {
        if (!sg) return;
        $('#sys-rssi').text(sg.rssi ? sg.rssi + ' dBm' : '--');
        $('#sys-q').text(sg.rssi ? sg.quality + ' %' : '-');
        $('#sys-tx').text(sg.rssi ? sg.txPower + ' dBm' : '-');
      });
    }
    pageInterval(refreshSignal, 5000);

    $('#sys-reboot').on('click', function() {
      if (!confirm('Red\u00e9marrer l\'appareil ?')) return;
      $.post('/api/system/reboot', function() {
        showAlert('Red\u00e9marrage en cours...');
        setContent('<div class="container py-4 text-center"><h4>Red\u00e9marrage en cours...</h4><div class="spinner-border mt-3"></div></div>');
      });
    });
  }


  // ============================================================================
  // PAGE: Dashboard (devices with gauges/labels/actions via templates)
  // ============================================================================
  function pageDashboard() {
    setContent(
      '<div class="container py-4">' +
        '<h4>Dashboard</h4>' +
        '<div class="row g-3" id="masonry-grid"><div class="text-center p-3 col-12"><div class="spinner-border"></div></div></div>' +
      '</div>'
    );
    $.getJSON('/api/zigbee/devices', function(devs) {
      if (!devs || !devs.length) { $('#masonry-grid').html('<div class="col-12 text-center"><h5>No dashboard datas yet</h5></div>'); return; }
      var html = '';
      for (var i = 0; i < devs.length; i++) {
        var d = devs[i];
        html += '<div class="col-12 col-md-6 col-lg-4 d-flex"><div class="card p-4 flex-fill" style="min-width:380px;">' +
          '<h5 class="card-title">' + (d.alias || '@Mac : ' + d.ieee) + '</h5>' +
          '<div class="card-body"><div id="status_' + d.shortAddr + '">';
        // Status will be loaded dynamically via polling
        html += '<p class="text-muted">Chargement...</p>';
        html += '</div>';
        html += '<div id="action_' + d.shortAddr + '"></div>';
        html += '</div></div></div>';
      }
      $('#masonry-grid').html(html);
      // Init masonry
      if (typeof Masonry !== 'undefined') {
        var grid = document.querySelector('#masonry-grid');
        new Masonry(grid, { itemSelector: '.col-12', percentPosition: true });
      }
    });
  }

  // ============================================================================
  // PAGE: Network status
  // ============================================================================
  // Carte Ethernet (W5500) de la page Reseau : etat, mode de connexion et
  // adressage. Les reglages sont pris en compte au redemarrage.
  function loadEthernetCard() {
    $.getJSON('/api/config/ethernet', function(e) {
      if (!e.detected) return;   // carte sans W5500 : rien a afficher
      $('#net-eth-col').show();
      $('#net-wifi-col').removeClass('col-12').addClass('col-lg-6');
      var c = e.config || {};
      $('#wifi-disabled-note').text('Mode « Ethernet seul » : le WiFi n’est pas démarré. ' +
        'Changez le mode de connexion dans la carte Ethernet pour l’utiliser.').toggle(String(c.mode || 0) === '1');
      var ok = '<span class="badge bg-success">Oui</span>';
      var nok = '<span class="badge bg-danger">Non</span>';
      var $box = $('#net-eth').empty();

      var $t = $('<table class="table table-sm">');
      function row(label, value) {
        $t.append($('<tr>').append($('<td>').text(label), $('<td>').text(value || '--')));
      }
      $t.append($('<tr>').append($('<td>').text('Câble branché'), $('<td>').html(e.linkUp ? ok : nok)));
      $t.append($('<tr>').append($('<td>').text('Adresse IP obtenue'), $('<td>').html(e.hasIp ? ok : nok)));
      if (e.linkUp) row('Lien', e.speed + ' Mbit/s ' + (e.fullDuplex ? 'full duplex' : 'half duplex'));
      row('IP', e.ip); row('Masque', e.mask); row('Passerelle', e.gw); row('DNS', e.dns);
      row('MAC', e.mac);
      row('Interface utilisée', e['interface'] === 'ethernet' ? 'Ethernet' : (e['interface'] === 'wifi' ? 'WiFi' : 'aucune'));
      $box.append($t);

      var modes = [
        ['0', 'Automatique : Ethernet prioritaire, WiFi en secours si le câble est débranché'],
        ['1', 'Ethernet seul : le WiFi n’est jamais démarré'],
        ['2', 'WiFi seul : l’Ethernet n’est pas utilisé']
      ];
      var $f = $('<form id="eth-form">');
      $f.append($('<h6 class="mt-3">').text('Mode de connexion'));
      modes.forEach(function(m) {
        var id = 'eth-mode' + m[0];
        $f.append($('<div class="form-check">').append(
          $('<input class="form-check-input" type="radio" name="eth-mode">').attr({ id: id, value: m[0] })
            .prop('checked', String(c.mode || 0) === m[0]),
          $('<label class="form-check-label">').attr('for', id).text(m[1])));
      });
      $f.append($('<small class="text-muted d-block mt-1">').text(
        'En cas de box injoignable, 3 redémarrages rapides (moins de 4 s chacun) remettent le mode Automatique en DHCP, ' +
        'effacent la configuration WiFi et désactivent la sécurité HTTP.'));
      $f.append($('<div class="form-check form-switch mt-3">').append(
        $('<input class="form-check-input" type="checkbox" id="eth-dhcp">').prop('checked', c.dhcp !== 0),
        $('<label class="form-check-label" for="eth-dhcp">').text('DHCP (adresse automatique)')));
      var $static = $('<div id="eth-static" class="mt-2">');
      [['ip', 'Adresse IP'], ['mask', 'Masque'], ['gw', 'Passerelle'], ['dns', 'DNS (facultatif)']].forEach(function(f) {
        $static.append($('<div class="mb-2">').append(
          $('<label class="form-label small mb-0">').attr('for', 'eth-' + f[0]).text(f[1]),
          $('<input type="text" class="form-control form-control-sm" maxlength="15">')
            .attr({ id: 'eth-' + f[0], placeholder: '192.168.1.50' }).val(c[f[0]] || '')));
      });
      $f.append($static);
      $f.append($('<div class="text-danger small" id="eth-error" style="display:none;">'));
      $f.append($('<button type="submit" class="btn btn-primary btn-sm mt-2">').text('Enregistrer'));
      $f.append($('<div class="alert alert-info small mt-2" id="eth-saved" style="display:none;">')
        .text('Enregistré : la passerelle redémarre pour appliquer la configuration.'));
      $box.append($f);

      function toggleStatic() { $('#eth-static').toggle(!$('#eth-dhcp').is(':checked')); }
      toggleStatic();
      $('#eth-dhcp').on('change', toggleStatic);

      // Interface inutilisee grisee selon le mode choisi (comme configWiFi de la Box)
      function applyMode() {
        var m = $('input[name="eth-mode"]:checked').val() || '0';
        $('#net-wifi-col').css('opacity', m === '1' ? 0.45 : 1);
        $('#eth-dhcp').closest('.form-check').add('#eth-static').css('opacity', m === '2' ? 0.45 : 1);
        $('#wifi-disabled-note').toggle(m === '1');
      }
      applyMode();
      $f.on('change', 'input[name="eth-mode"]', applyMode);

      $f.on('submit', function(ev) {
        ev.preventDefault();
        var body = {
          mode: parseInt($('input[name="eth-mode"]:checked').val() || '0', 10),
          dhcp: $('#eth-dhcp').is(':checked') ? 1 : 0,
          ip: $('#eth-ip').val().trim(), mask: $('#eth-mask').val().trim(),
          gw: $('#eth-gw').val().trim(), dns: $('#eth-dns').val().trim()
        };
        $('#eth-error').hide(); $('#eth-saved').hide();
        $.ajax({ url: '/api/config/ethernet', type: 'POST', contentType: 'application/json',
                 data: JSON.stringify(body) })
          .done(function(r) {
            $('#eth-saved').show();
            if (r && r.reboot) {
              // Redemarrage automatique : retour sur la nouvelle adresse si IP fixe
              var dest = (!body.dhcp && body.ip) ? 'http://' + body.ip + '/' : '/';
              showAlert('Configuration enregistr&eacute;e. Red&eacute;marrage en cours...');
              setTimeout(function() { window.location.href = dest; }, 15000);
            }
          })
          .fail(function(xhr) {
            var msg = 'Erreur d’enregistrement';
            try { msg = JSON.parse(xhr.responseText).message || msg; } catch (err) {}
            $('#eth-error').text(msg).show();
          });
      });
    });
  }

  // ============================================================================
  // PAGE: LAN (Reseau > LAN), comme configWiFi de la LiXee-Box : le WiFi
  // (toujours present sur l'ESP32-S3) et l'Ethernet si le W5500 est detecte
  // ============================================================================
  function pageNetwork() {
    setContent(
      '<div class="container py-4">' +
        '<h4>LAN</h4>' +
        '<div class="row g-4">' +
          '<div class="col-12" id="net-wifi-col">' + wifiSectionHtml() + '</div>' +
          // Ethernet W5500 : colonne affichee seulement si la puce est detectee
          '<div class="col-lg-6" id="net-eth-col" style="display:none;"><div class="card p-4"><h5>Ethernet</h5><div id="net-eth"></div></div></div>' +
        '</div>' +
      '</div>'
    );
    initWifiSection();
    loadEthernetCard();
  }

  // ============================================================================
  // PAGE: Config General (Parametres + Debug tabs)
  // ============================================================================
  // Override existing pageConfigGeneral with full version
  // (Already defined above, but we enhance it here by replacing the existing definition)

  // ============================================================================
  // PAGE: Assist Device (4-step pairing wizard)
  // ============================================================================

})();
