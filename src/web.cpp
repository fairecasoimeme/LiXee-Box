#include <stdio.h>
#include <algorithm>
#include "rom/ets_sys.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include "driver/temp_sensor.h"
#include <esp_task_wdt.h>
#include <stddef.h>
#include <Arduino.h>
#include <TimeLib.h>
#include "AsyncJson.h"
#define ARDUINOJSON_USE_LONG_LONG 1
#define ARDUINOJSON_SLOT_ID_SIZE 2
#include <ArduinoJson.h>
#include "WiFi.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncMqttClient.h>

#include "FS.h"
#include "LittleFS.h"
#include "SPIFFS_ini.h"
#include <Update.h>

#include "config.h"
#include "loraModule.h"
#include "loraReceiver.h"
#include "mbedtls/base64.h"
#include "esp_crt_bundle.h"
#include "flash.h"
#include "log.h"
#include "protocol.h"
#include "zigbee.h"
#include "basic.h"
#include "thermostat.h"
#include "presence.h"
#include "virtualThermostat.h"

#include "rules.h"
#include "microtar.h"
#include "device.h"
#include "zigate_flasher.h"
#include "notificationManager.h"
#include "TemplateData.h"
#include "TemplateCache.h"
#include "tunnel.h"
#include "ElectricalMeasurement.h"

extern DeviceList devices;
extern LiXeeBoxTunnel* tunnel;

extern SemaphoreHandle_t file_Mutex;

extern struct ZigbeeConfig ZConfig;
extern struct ConfigSettingsStruct ConfigSettings;
extern struct ConfigGeneralStruct ConfigGeneral;
extern struct ConfigNotification ConfigNotif;
extern AsyncMqttClient mqttClient;

extern unsigned long timeLog;
extern CircularBuffer<Packet, 100> *commandList;
extern CircularBuffer<Packet, 70> *PrioritycommandList;
extern CircularBuffer<Alert, 10> *alertList;
extern CircularBuffer<Device, 50> *deviceList;
extern CircularBuffer<Notification, 10> *notifList;

extern RulesManager rulesManager;

extern bool executeReboot;
extern bool updatePending ;

extern NotificationManager notificationManager;

// ISRG Root X1 (Let's Encrypt) - valid until 2035-06-04
static const char lixee_root_ca[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6
UA5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+s
WT8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qy
HB5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+U
CvdGz7aLzIwBBVRnkLb1fqB3pVFnk/Cn6SfNhTU65JkZ+8/pBjYbl5cw+At+a1o
Zr/Gt9XHDGR/M7hz45zB/al51fqzYsAOHagPl/MX3S6K8/riPMPOnfKjPPbXPyQh
sPBBmFr8fVPbEOF8FjGDLUsbgpf0GIGMBoFnaBCCIZOHk5EVJI0oBNFN1G5W4SBK
r8WI0tBQ+rKn0dXBqFB/I4ek0WqfXjKl/LUoxDq48wLMFNxWL5U4gO8cHj3bT97I
6neIEBq+xfYLwY1zQOzkvAkr4VZnOaJ/3ceEF1g8rMqIK3rRbsIhSmvJMN3B1lMy
LLpXBccWpU5tPBWYhNg+DlMRbAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogZiUvsaXPFJYcFIB7LREoA1g1BQuGyMPNjpCKWB5BhpE/m
8tVFEkneA6D1JYxr/7xtAplJSl3MgIFvzPO0JKr3NZprMpI3OSnEbNBbnCCdflcR
p5/G4MGSUVMK4D90rjZbsWi0+bBfUjTMOBtOWEjqFf5j+aKKlIorMkX71MCTHF/x
nNohmGFiFqYSDA19VO+J0qw3SYTzfdxofsqFmObUhDce4f0Fdb2lyNGjIKH15m4y
qkgMzFBBrkixNS0a6Y2gdMg3e3Fse3N5KJY49X0VfhIyNJVEhmj8LNYMdK5S3zuA
jRaGTGHaYbSEX5EvGNmJ0lJ2bkw7z6FCh7S3kvPFfMk3AbsHtT2oCEsG5T8qR+hh
5MUC9aO0dwPJ5w/FPbFk+fk/1x/ZerQFOr/5IxJVWMhnav8pu6f0w3dBG/nb7kNN
VPQ/qKSM9nHHKq+s3YlBEfMD2JG+r8OnVJSS1JfM/IVHd2Y4y2n65Arx9g=
-----END CERTIFICATE-----
)EOF";

// ==================== Session Management ====================
struct SessionInfo {
    char token[33];          // 32 hex chars + null
    unsigned long createdAt; // millis()
    bool active;
};

static const int MAX_SESSIONS = 4;
static SessionInfo sessions[MAX_SESSIONS] = {};
static const unsigned long SESSION_TIMEOUT = 24UL * 3600UL * 1000UL; // 24h

String generateSessionToken() {
    char token[33];
    for (int i = 0; i < 16; i++) {
        snprintf(token + i*2, sizeof(token) - i*2, "%02x", (uint8_t)esp_random());
    }
    token[32] = 0;
    return String(token);
}

String createSession() {
    int oldest = 0;
    unsigned long oldestTime = ULONG_MAX;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!sessions[i].active) { oldest = i; break; }
        if (sessions[i].createdAt < oldestTime) {
            oldestTime = sessions[i].createdAt;
            oldest = i;
        }
    }
    String token = generateSessionToken();
    strlcpy(sessions[oldest].token, token.c_str(), 33);
    sessions[oldest].createdAt = millis();
    sessions[oldest].active = true;
    return token;
}

bool isValidSession(const String& token) {
    if (token.length() == 0) return false;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].active && token == sessions[i].token) {
            if (millis() - sessions[i].createdAt > SESSION_TIMEOUT) {
                sessions[i].active = false;
                return false;
            }
            return true;
        }
    }
    return false;
}

void deleteSession(const String& token) {
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].active && token == sessions[i].token) {
            sessions[i].active = false;
        }
    }
}

String getSessionCookie(AsyncWebServerRequest *request) {
    if (request->hasHeader("Cookie")) {
        String cookies = request->header("Cookie");
        int idx = cookies.indexOf("session=");
        if (idx >= 0) {
            int end = cookies.indexOf(';', idx);
            if (end < 0) end = cookies.length();
            return cookies.substring(idx + 8, end);
        }
    }
    return "";
}

// CSRF protection: validate Origin/Referer on POST requests
bool checkCsrf(AsyncWebServerRequest *request) {
    if (request->method() != HTTP_POST) return true;

    // Requêtes via le tunnel (reverse proxy) : injectées depuis 127.0.0.1.
    // Elles restent protégées par checkAuth (session) exécuté juste après.
    if (request->client()->remoteIP() == IPAddress(127, 0, 0, 1)) return true;

    // Check Origin header first, then Referer
    String origin;
    if (request->hasHeader("Origin")) {
        origin = request->header("Origin");
    } else if (request->hasHeader("Referer")) {
        origin = request->header("Referer");
    } else {
        // No Origin/Referer on POST = likely CSRF (browser always sends at least one)
        log_e("[CSRF] POST without Origin/Referer from %s", request->client()->remoteIP().toString().c_str());
        return false;
    }

    // Vérification "same-origin" : l'hôte de l'Origin/Referer doit correspondre à l'hôte
    // ciblé par la requête (header Host). PAS de liste blanche de domaines : ça fonctionne
    // quel que soit le mode d'accès (IP locale, mDNS, tunnel, DynDNS+NAT, reverse proxy,
    // VPN, domaine perso...), alors qu'une attaque CSRF provient forcément d'une origine
    // DIFFÉRENTE de l'hôte ciblé — donc elle reste bloquée.
    // "http://mabox.dyndns.net:8080/rules" -> "mabox.dyndns.net:8080"
    String originHost = origin;
    int schemeEnd = originHost.indexOf("://");
    if (schemeEnd >= 0) originHost = originHost.substring(schemeEnd + 3);
    int pathStart = originHost.indexOf('/');
    if (pathStart >= 0) originHost = originHost.substring(0, pathStart);

    if (originHost.equalsIgnoreCase(request->host())) return true;

    log_e("[CSRF] Rejected POST: origin '%s' != host '%s'", originHost.c_str(), request->host().c_str());
    return false;
}

bool checkAuth(AsyncWebServerRequest *request) {
    if (!ConfigSettings.enableSecureHttp) return true;

    // 0. CSRF protection on POST requests
    if (!checkCsrf(request)) {
        request->send(403, "text/plain", "Forbidden: CSRF check failed");
        return false;
    }

    // 1. Check session cookie
    String token = getSessionCookie(request);
    if (isValidSession(token)) return true;

    // 2. Fallback: Basic Auth pour les clients API en accès direct uniquement
    //    - Pas de fallback pour les requêtes tunnel (127.0.0.1) : seul le cookie compte
    //    - Pas de Digest Auth : le navigateur le cache et empêche le logout
    if (request->client()->remoteIP() != IPAddress(127, 0, 0, 1)) {
        if (request->hasHeader("Authorization")) {
            String authHeader = request->header("Authorization");
            if (authHeader.startsWith("Basic ")) {
                if (request->authenticate(ConfigGeneral.userHTTP, ConfigGeneral.passHTTP)) return true;
            }
        }
    }

    // 3. Not authenticated - determine response type
    bool isAjax = request->hasHeader("X-Requested-With") ||
                  (request->hasHeader("Accept") &&
                   request->header("Accept").indexOf("application/json") >= 0);

    if (isAjax) {
        request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    } else {
        request->redirect("/login");
    }
    return false;
}
// ==================== End Session Management ====================

// ==================== Tunnel Activation (async) ====================
struct TunnelActivation {
    bool pending;       // Le handler a posé une demande
    bool processing;    // En cours de traitement dans loop()
    bool done;          // Résultat disponible
    bool success;
    char code[7];
    unsigned long requestTime;  // millis() quand pending a été posé
    String error;
    String deviceId;
    void reset() { pending = false; processing = false; done = false; success = false; code[0] = 0; requestTime = 0; error = ""; deviceId = ""; }
};
static TunnelActivation tunnelActivation = {};

// Appelée depuis loop() dans le main .ino
void processTunnelActivation() {
    if (!tunnelActivation.pending || tunnelActivation.processing) return;
    // Attendre 2s pour que la réponse {"status":"pending"} soit renvoyée au navigateur via le tunnel
    if (millis() - tunnelActivation.requestTime < 2000) return;
    tunnelActivation.pending = false;
    tunnelActivation.processing = true;

    Serial.printf("[Tunnel] Activation avec code: %s\n", tunnelActivation.code);

    WiFiClientSecure secClient;
    secClient.setCACert(lixee_root_ca);
    HTTPClient http;
    String url = "https://remote.lixee-box.fr/api/activate?code=" + String(tunnelActivation.code);
    http.begin(secClient, url);
    http.setTimeout(10000);

    int httpCode = http.GET();

    if (httpCode != 200) {
        http.end();
        tunnelActivation.success = false;
        tunnelActivation.error = "Erreur de connexion au serveur, vérifiez votre connexion internet";
        tunnelActivation.done = true;
        tunnelActivation.processing = false;
        Serial.printf("[Tunnel] Activation échouée: HTTP %d\n", httpCode);
        return;
    }

    String body = http.getString();
    http.end();

    SpiRamJsonDocument doc(1024);
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        tunnelActivation.success = false;
        tunnelActivation.error = "Réponse invalide du serveur";
        tunnelActivation.done = true;
        tunnelActivation.processing = false;
        return;
    }

    if (!doc["success"].as<bool>()) {
        tunnelActivation.success = false;
        tunnelActivation.error = doc["error"].as<String>();
        tunnelActivation.done = true;
        tunnelActivation.processing = false;
        return;
    }

    // Succès : sauvegarder et activer
    String deviceId = doc["deviceId"].as<String>();
    String token = doc["token"].as<String>();

    strlcpy(ConfigGeneral.tunnelClientId, deviceId.c_str(), sizeof(ConfigGeneral.tunnelClientId));
    strlcpy(ConfigGeneral.tunnelToken, token.c_str(), sizeof(ConfigGeneral.tunnelToken));
    ConfigGeneral.enableTunnel = true;

    String cfgPath = "configGeneral.json";
    config_write(cfgPath, "tunnelClientId", deviceId);
    config_write(cfgPath, "tunnelToken", token);
    config_write(cfgPath, "enableTunnel", "1");

    // Hot reload tunnel
    if (tunnel != nullptr) {
        tunnel->stop();
        delete tunnel;
        tunnel = nullptr;
    }
    String tunnelUrl = "wss://remote.lixee-box.fr/tunnel?token=" + token;
    if (strlen(ConfigGeneral.tunnelClientId) > 0) {
        tunnelUrl += "&clientId=";
        tunnelUrl += ConfigGeneral.tunnelClientId;
    }
    tunnel = new LiXeeBoxTunnel(tunnelUrl.c_str(), 80);
    tunnel->begin();

    tunnelActivation.success = true;
    tunnelActivation.deviceId = deviceId;
    tunnelActivation.done = true;
    tunnelActivation.processing = false;
    Serial.println("[Tunnel] Activé via code d'activation");
    addDebugLog("Tunnel activé via code");
}
// ==================== End Tunnel Activation ====================

int maxDayOfTheMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
String section[12] = { "0", "1", "256", "258" , "260", "262", "264" ,"266", "268", "270", "272", "274"};

UpdateStatus updateStatus;

// Log de mise a jour : ecrit sur Serial (+ accumule dans updateStatus.log si active)
static void updateLog(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.println(buf);
    /* LOG UPDATE - desactive temporairement
    if (updateStatus.log.length() < 8192) {  // limite 8KB
        updateStatus.log += buf;
        updateStatus.log += "\n";
    }
    */
}

HTTPClient clientWeb;

AsyncWebServer serverWeb(80);
AsyncWebSocket ws("/ws");

//TemplateCache templateCache(true);
extern TemplateCache templateCache;

#define UPD_FILE "https://github.com/fairecasoimeme/lixee-gateway/releases/latest/download/update.tar"

extern uint8_t* au8OTAFile;


const char HTTP_SHELLY_EMULE[] PROGMEM = 
"{"
    "\"name\":null,"
    "\"id\":\"shellypro3em-ac1518778a1c\","
    "\"mac\":\"AC1518778A1C\","
    "\"slot\":0,"
    "\"model\":\"SPEM-003CEBEU\","
    "\"gen\":2,"
    "\"fw_id\":\"20241011-114455/1.4.4-g6d2a586\","
    "\"ver\":\"1.4.4\","
    "\"app\":\"Pro3EM\","
    "\"auth_en\":false,"
    "\"auth_domain\":null,"
    "\"profile\":\"triphase\""

"}";

const char HTTP_HEADER[] PROGMEM =
    "<head>"
    "<link rel='icon' type='image/x-icon' href='web/favicon.ico'>"
    /* Le ?v= sur chaque asset statique n'est pas cosmetique.
     *
     * 1. Correction : ces fichiers sont servis en "max-age=604800, immutable". Sans version
     *    dans l'URL, un navigateur garde une lib PERIMEE une semaine apres une mise a jour
     *    qui la change -- immutable lui interdit meme de revalider.
     *
     * 2. Prerequis pour LiXee-Assist : l'app peut embarquer ces libs et les servir
     *    localement (shouldInterceptRequest), ce qui evite de les faire transiter par le
     *    tunnel (~210 Ko au premier chargement, encodes en base64 par dessus). La version
     *    dans l'URL lui permet de ne substituer que ce dont elle a la copie EXACTE et de
     *    laisser passer le reste vers la box -- sinon elle servirait une lib obsolete sans
     *    pouvoir le detecter.
     *
     * La query ne gene pas serveStatic : ESPAsyncWebServer met le '?' de cote avant de
     * router (_url exclut la query).
     */
    "<script type='text/javascript' src='web/js/jquery-min.js?v=" VERSION "'></script>"
    "<script type='text/javascript' src='web/js/masonry.pkgd.min.js?v=" VERSION "'></script>"
    //"<script type='text/javascript' src='web/js/bootstrap.min.js'></script>"
    "<script type='text/javascript' src='web/js/functions.min.js?v=" VERSION "'></script>"
    "<script>$(document).ajaxError(function(e,x){if(x.status===401)window.location.href='/login';});</script>"
    "<link href='web/css/bootstrap.min.css?v=" VERSION "' rel='stylesheet' type='text/css' />"
    "<link href='web/css/style.css?v=" VERSION "' rel='stylesheet' type='text/css' />"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
      "body {"
        "background-color: #f7f9fc;"
        "font-family: 'Inter', sans-serif;"
      "}"
     ".card {"
        "border-radius: 1rem;"
        "box-shadow: 0 4px 6px rgba(0,0,0,0.05);"
      "}"
    "</style>"
     "</head>";


const char HTTP_HEADERGRAPH[] PROGMEM =
    "<head>"
    "<link rel='icon' type='image/x-icon' href='web/favicon.ico'>"
    // raphael est requis par justgage (jauges) ET par morris -- ne pas retirer.
    "<script type='text/javascript' src='web/js/raphael-min.js?v=" VERSION "'></script>"
    // morris est REQUIS : createDistributionGraph() genere un Morris.Donut, et elle est
    // appelee par handleStatusEnergy() (le donut de repartition de la page Energie).
    // Piege : les deux AUTRES appels a Morris (createPowerGraph, createEnergyGraph v1) sont
    // bien dans des fonctions commentees -- de quoi conclure a tort que la lib est morte.
    "<script type='text/javascript' src='web/js/morris.min.js?v=" VERSION "'></script>"
    "<script type='text/javascript' src='web/js/chart.umd.min.js?v=" VERSION "'></script>"
    "<script type='text/javascript' src='web/js/annotation.min.js?v=" VERSION "'></script>"
    "<script type='text/javascript' src='web/js/chart-zoom.min.js?v=" VERSION "'></script>"
    "<script type='text/javascript' src='web/js/justgage.min.js?v=" VERSION "'></script>"
    "<script type='text/javascript' src='web/js/functions.min.js?v=" VERSION "'></script>"
    "<script type='text/javascript' src='web/js/jquery-min.js?v=" VERSION "'></script>"
    "<script type='text/javascript' src='web/js/presence.min.js?v=" VERSION "'></script>"
    "<script src='https://cdn.jsdelivr.net/npm/hammerjs@2.0.8/hammer.min.js'></script>"
    "<script>$(document).ajaxError(function(e,x){if(x.status===401)window.location.href='/login';});</script>"
    "<link href='web/css/bootstrap.min.css?v=" VERSION "' rel='stylesheet' type='text/css' />"
    "<link href='web/css/style.css?v=" VERSION "' rel='stylesheet' type='text/css' />"
    "<link href='web/css/energy.css?v=" VERSION "' rel='stylesheet' type='text/css' />"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0, user-scalable=yes'>"
    "</head>";


// Le menu (nav + script spinner/swipe) est servi via la route /menu.js (mise en cache navigateur),
// pour ne plus renvoyer ~29 Ko de menu dans le HTML de CHAQUE page. La page n'inclut qu'un <script src>.
// /modules.js (quelques octets, NON caché) publie les modules détectés ; /menu.js (29 Ko,
// caché 1 semaine) contient le menu statique et se filtre à partir de ces drapeaux.
// Les <script src> sans defer s'exécutent dans l'ordre : les drapeaux existent donc
// avant que menu.js ne tourne.
// Cache-buster = VERSION + heure de COMPILATION (littéraux concaténés à la compilation).
// Le menu étant compilé dans le firmware, chaque build produit une URL différente : les
// modifications du menu sont donc toujours reprises, tout en gardant le cache long (1 semaine)
// à l'intérieur d'un même firmware. VERSION seule ne suffit pas : elle ne change pas entre
// deux builds de développement, et menu.js restait figé en cache (entrées jamais visibles).
const char HTTP_MENU[] PROGMEM = "<body><script src='/modules.js'></script><script src='/menu.js?v=" VERSION "-" __TIME__ "'></script>";

// Symbole du logo LoRa officiel, monochrome : le fill currentColor le fait suivre la
// couleur du texte, comme les autres icones du menu (blanc sur un bouton bleu, sombre
// sur fond clair). Trace optimise (-78%) puis fusionne en 4 chemins.
// Macro et non const char[] : ces blocs PROGMEM sont assembles par concatenation de
// litteraux a la compilation, ce qui n'est possible qu'entre litteraux.
// Le viewBox et les traces du logo, separes de l'element <svg> : ils servent tels quels
// dans un <symbol>, ce qui permet de declarer le dessin une seule fois par page et de le
// referencer par <use> autant de fois que voulu (fiches appareils) sans le repeter.
#define SVG_LORA_VIEWBOX "1 44 302 517"
#define SVG_LORA_PATHS \
  "<path d='M223.5 237.4a84 84 0 0 1 24.7 61c.1 24.9-4 47.5-21.9 66.3L224 367l-2 2.1c-16.4 15.6-40.8 23.2-63 23.9l-3.1.1c-26.2.5-51.4-5.3-71.9-22.1l-3-2.4A81 81 0 0 1 56 319c-.9-16.6-1.7-34.2 4-50l1.3-3.7a84 84 0 0 1 46.2-45.9c36.4-14.8 87-9.7 116 18m-102.8 26.2a65 65 0 0 0-11.2 49.5c2.2 14 7.5 24.3 18.4 33.5 9.6 6.7 22 7.6 33.2 6A39 39 0 0 0 187 336a82 82 0 0 0 5.4-56 47 47 0 0 0-20.8-25.3 44 44 0 0 0-50.9 8.9'/>" \
  "<path d='M22 483q5.5 2.1 10.6 5.2c22.4 13 47 21.4 72.2 26.6l2.9.6q22.5 4.2 45.4 4h2.4q20.4 0 40.5-3.4l2.8-.5A243 243 0 0 0 279 485q11.1 10.6 19 24c-30.3 21.2-67.7 32.4-104 38l-2.2.3c-65 10-130.8-6-186.8-39.3 0-3.6.8-4.5 3-7.3l1.8-2.6 2-2.6 2-2.6c5.2-7 5.2-7 8.2-9.9M197 58l3 .5A282 282 0 0 1 284 88l2 1c11.8 6.4 11.8 6.4 13 10-1.1 2-1.1 2-2.9 4.3l-3.9 5.2-2 2.6-5.2 6.9c-.7 1-.7 1-1.5 1.8-1.5 1.2-1.5 1.2-3.5 1q-2.9-1.2-5.6-2.8l-2-1.2-2.3-1.3A248 248 0 0 0 176 86l-3.3-.4c-47.8-4.7-96 5.7-138.2 28.5l-2.8 1.5-2.5 1.3q-3 1.4-6.2 2.1l-6.8-8.7-2-2.5-1.8-2.5-1.8-2.2L9 101l-1.8-2L6 97c1.2-3.6 2-3.8 5.2-5.6l2.6-1.5 3-1.5 3-1.6A282 282 0 0 1 197 58'/>" \
  "<path d='M52 443q4.5 1.7 8.8 4A195 195 0 0 0 206 464q19.3-5.4 36.7-15l2.3-1.2 2-1.1c2.5-.9 3.6-.7 6 .3 1.6 1.7 1.6 1.7 3 3.8l1.6 2.3 1.6 2.5 1.7 2.4 4.1 6c-3 3.4-6.5 5.6-10.5 7.8l-2 1a221 221 0 0 1-168.4 12A230 230 0 0 1 40 464c0-4.2.3-4.9 2.6-8.1l1.7-2.3 1.7-2.4 1.7-2.4zm200.7-308.7 3 1.6 2.9 1.5 2.5 1.3c1.9 1.3 1.9 1.3 2.9 4.3q-2.2 3.6-4.7 7l-1.4 1.8-5.7 7.6-1.2 1.6a56 56 0 0 1-12.2-5.3 180 180 0 0 0-89-22h-2.2c-29.9.1-58.8 6.6-85.4 20.6q-5.3 3-11.2 4.7l-6.6-8.8-1.9-2.5-1.8-2.5-1.6-2.2-1.1-2c1.7-5 6.5-6.3 11-8.6l3.1-1.6a217 217 0 0 1 58.2-19.3c48.1-10 99.3.7 142.4 22.8M81 404l1.5.7c37.4 18.1 75.8 25.7 116.3 12.2q12.3-4.5 24.2-9.9l6.4 8.8c3.6 5 3.6 5 3.6 7.2a163 163 0 0 1-80.8 20.4A154 154 0 0 1 71 421c.6-4.3 3.1-7.4 5.6-10.7z'/>" \
  "<path d='M233 184a60 60 0 0 1-6.1 9.8l-2.2 3c-.9 1-.9 1-1.7 2.2q-6.4-2-12.4-5.1a138 138 0 0 0-124.1 1.5c-3.5 1.6-3.5 1.6-5.5 1a25 25 0 0 1-6.2-6.8l-2.2-3C71 184 71 184 71 181c50.8-25.7 112.8-29.3 162 3'/>"

// Icone complete, prete a poser dans du HTML (menus).
// La taille est en style inline, et pas seulement en attributs : style.css impose un
// « svg{width:100%} » global, qui bat les attributs width/height. Sans ce style, l'icone
// occupe toute la largeur du menu. C'est pour la meme raison que les autres icones du menu
// portent toutes un style='width:16px;'.
#define SVG_LORA_ICON \
  "<svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' style='width:10px;height:16px;' width='10' height='16' viewBox='" SVG_LORA_VIEWBOX "'>" \
  SVG_LORA_PATHS \
  "</svg>"

// Markup du menu, injecte par /menu.js via document.write (sync, avant le footer => dropdowns/badges OK).
const char HTTP_MENU_NAV[] PROGMEM =
   "<nav class='navbar navbar-expand-md rounded'>"
   "<div class='container-fluid' style=''>"
   "<button class='navbar-toggler' type='button' data-bs-toggle='collapse' data-bs-target='#navbarNavDropdown' aria-controls='navbarNavDropdown' aria-expanded='false' aria-label='Toggle navigation'>"
   "<span class='navbar-toggler-icon'></span>"
   "<div class='AlertNotif' style='display:none; width: 8px;height: 8px; background-color: red; margin-left: 4px; vertical-align: middle;border-radius: 50%;  '></div>"
   "<div class='AboutMaj' style='display:none; width: 8px;height: 8px; background-color: red; margin-left: 4px; vertical-align: middle;border-radius: 50%;  '></div>"
   "</button>"
   "<a class='navbar-brand p-0 me-0 me-lg-2' href='/' style='margin-right:0px;text-decoration:none;'>"
   "  <div style='display:flex;align-items:center;'>"
   "    <img width='70px' src='web/img/logo.png'>"
   "    <span style='font-size:16px;font-weight:bold;padding-left:10px;padding-top:15px;'> Box</span>"
   "  </div>"
   "  <div style='width:13px;font-size:11px;color:#6c757d;text-align:center;margin-top:10px;'>"
   "    <svg xmlns='http://www.w3.org/2000/svg' width='11' height='11' fill='currentColor' viewBox='0 0 16 16'>"
   "      <path d='M8 3.5a.5.5 0 0 0-1 0V9a.5.5 0 0 0 .252.434l3.5 2a.5.5 0 0 0 .496-.868L8 8.71z'/>"
   "      <path d='M8 16A8 8 0 1 0 8 0a8 8 0 0 0 0 16m7-8A7 7 0 1 1 1 8a7 7 0 0 1 14 0'/>"
   "    </svg> "
   "    <span id='FormattedDate'></span>"
   "  </div>"
   "</a>"
   "<div id='navbarNavDropdown' class='collapse navbar-collapse justify-content-center'>"
   "<ul class='navbar-nav mc-auto mb-2 mb-lg-0'>"
   "<li class='nav-item dropdown'>"
   "<a class='nav-link dropdown-toggle' href='#' id='navbarDropdown' role='button' data-bs-toggle='dropdown'>"
   "<svg xmlns='http://www.w3.org/2000/svg' style='width:24px;' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' class='feather feather-home'>"
   "  <path d='M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z'></path>"
   "  <polyline points='9 22 9 12 15 12 15 22'></polyline>"
   "</svg>"
   " Mesures"
   "</a>"
   "<div class='dropdown-menu'>"
   
   "<a class='dropdown-item' href='statusEnergy'>"
   "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px; width='16' height='16' fill='currentColor' class='bi bi-flower1' viewBox='0 0 16 16'>"
    "<path d='M6.174 1.184a2 2 0 0 1 3.652 0A2 2 0 0 1 12.99 3.01a2 2 0 0 1 1.826 3.164 2 2 0 0 1 0 3.652 2 2 0 0 1-1.826 3.164 2 2 0 0 1-3.164 1.826 2 2 0 0 1-3.652 0A2 2 0 0 1 3.01 12.99a2 2 0 0 1-1.826-3.164 2 2 0 0 1 0-3.652A2 2 0 0 1 3.01 3.01a2 2 0 0 1 3.164-1.826M8 1a1 1 0 0 0-.998 1.03l.01.091q.017.116.054.296c.049.241.122.542.213.887.182.688.428 1.513.676 2.314L8 5.762l.045-.144c.248-.8.494-1.626.676-2.314.091-.345.164-.646.213-.887a5 5 0 0 0 .064-.386L9 2a1 1 0 0 0-1-1M2 9l.03-.002.091-.01a5 5 0 0 0 .296-.054c.241-.049.542-.122.887-.213a61 61 0 0 0 2.314-.676L5.762 8l-.144-.045a61 61 0 0 0-2.314-.676 17 17 0 0 0-.887-.213 5 5 0 0 0-.386-.064L2 7a1 1 0 1 0 0 2m7 5-.002-.03a5 5 0 0 0-.064-.386 16 16 0 0 0-.213-.888 61 61 0 0 0-.676-2.314L8 10.238l-.045.144c-.248.8-.494 1.626-.676 2.314-.091.345-.164.646-.213.887a5 5 0 0 0-.064.386L7 14a1 1 0 1 0 2 0m-5.696-2.134.025-.017a5 5 0 0 0 .303-.248c.184-.164.408-.377.661-.629A61 61 0 0 0 5.96 9.23l.103-.111-.147.033a61 61 0 0 0-2.343.572c-.344.093-.64.18-.874.258a5 5 0 0 0-.367.138l-.027.014a1 1 0 1 0 1 1.732zM4.5 14.062a1 1 0 0 0 1.366-.366l.014-.027q.014-.03.036-.084a5 5 0 0 0 .102-.283c.078-.233.165-.53.258-.874a61 61 0 0 0 .572-2.343l.033-.147-.11.102a61 61 0 0 0-1.743 1.667 17 17 0 0 0-.629.66 5 5 0 0 0-.248.304l-.017.025a1 1 0 0 0 .366 1.366m9.196-8.196a1 1 0 0 0-1-1.732l-.025.017a5 5 0 0 0-.303.248 17 17 0 0 0-.661.629A61 61 0 0 0 10.04 6.77l-.102.111.147-.033a61 61 0 0 0 2.342-.572c.345-.093.642-.18.875-.258a5 5 0 0 0 .367-.138zM11.5 1.938a1 1 0 0 0-1.366.366l-.014.027q-.014.03-.036.084a5 5 0 0 0-.102.283c-.078.233-.165.53-.258.875a61 61 0 0 0-.572 2.342l-.033.147.11-.102a61 61 0 0 0 1.743-1.667c.252-.253.465-.477.629-.66a5 5 0 0 0 .248-.304l.017-.025a1 1 0 0 0-.366-1.366M14 9a1 1 0 0 0 0-2l-.03.002a5 5 0 0 0-.386.064c-.242.049-.543.122-.888.213-.688.182-1.513.428-2.314.676L10.238 8l.144.045c.8.248 1.626.494 2.314.676.345.091.646.164.887.213a5 5 0 0 0 .386.064zM1.938 4.5a1 1 0 0 0 .393 1.38l.084.035q.108.045.283.103c.233.078.53.165.874.258a61 61 0 0 0 2.343.572l.147.033-.103-.111a61 61 0 0 0-1.666-1.742 17 17 0 0 0-.66-.629 5 5 0 0 0-.304-.248l-.025-.017a1 1 0 0 0-1.366.366m2.196-1.196.017.025a5 5 0 0 0 .248.303c.164.184.377.408.629.661A61 61 0 0 0 6.77 5.96l.111.102-.033-.147a61 61 0 0 0-.572-2.342c-.093-.345-.18-.642-.258-.875a5 5 0 0 0-.138-.367l-.014-.027a1 1 0 1 0-1.732 1m9.928 8.196a1 1 0 0 0-.366-1.366l-.027-.014a5 5 0 0 0-.367-.138c-.233-.078-.53-.165-.875-.258a61 61 0 0 0-2.342-.572l-.147-.033.102.111a61 61 0 0 0 1.667 1.742c.253.252.477.465.66.629a5 5 0 0 0 .304.248l.025.017a1 1 0 0 0 1.366-.366m-3.928 2.196a1 1 0 0 0 1.732-1l-.017-.025a5 5 0 0 0-.248-.303 17 17 0 0 0-.629-.661A61 61 0 0 0 9.23 10.04l-.111-.102.033.147a61 61 0 0 0 .572 2.342c.093.345.18.642.258.875a5 5 0 0 0 .138.367zM8 9.5a1.5 1.5 0 1 0 0-3 1.5 1.5 0 0 0 0 3'/>"
  "</svg>"
   " Energie"
   "</a>"
   "<a class='dropdown-item' href='thermostats'>"
   "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px;' width='16' height='16' fill='currentColor' class='bi bi-thermometer-half' viewBox='0 0 16 16'>"
   "  <path d='M9.5 12.5a1.5 1.5 0 1 1-2-1.415V6.5a.5.5 0 0 1 1 0v4.585a1.5 1.5 0 0 1 1 1.415'/>"
   "  <path d='M5.5 2.5a2.5 2.5 0 0 1 5 0v7.55a3.5 3.5 0 1 1-5 0zM8 1a1.5 1.5 0 0 0-1.5 1.5v7.987l-.167.15a2.5 2.5 0 1 0 3.333 0l-.166-.15V2.5A1.5 1.5 0 0 0 8 1'/>"
   "</svg>"
   " Thermostats"
   "</a>"
   /*"<a class='dropdown-item' href='dashboard'>"
   "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px; width='16' height='16' fill='currentColor' class='bi bi-speedometer' viewBox='0 0 16 16'>"
   "  <path d='M8 2a.5.5 0 0 1 .5.5V4a.5.5 0 0 1-1 0V2.5A.5.5 0 0 1 8 2M3.732 3.732a.5.5 0 0 1 .707 0l.915.914a.5.5 0 1 1-.708.708l-.914-.915a.5.5 0 0 1 0-.707M2 8a.5.5 0 0 1 .5-.5h1.586a.5.5 0 0 1 0 1H2.5A.5.5 0 0 1 2 8m9.5 0a.5.5 0 0 1 .5-.5h1.5a.5.5 0 0 1 0 1H12a.5.5 0 0 1-.5-.5m.754-4.246a.39.39 0 0 0-.527-.02L7.547 7.31A.91.91 0 1 0 8.85 8.569l3.434-4.297a.39.39 0 0 0-.029-.518z'/>"
   "  <path fill-rule='evenodd' d='M6.664 15.889A8 8 0 1 1 9.336.11a8 8 0 0 1-2.672 15.78zm-4.665-4.283A11.95 11.95 0 0 1 8 10c2.186 0 4.236.585 6.001 1.606a7 7 0 1 0-12.002 0'/>"
   "</svg>"
   " Dashboard"
   "</a>"*/
   // Liste TOUS les appareils (Zigbee et/ou LoRa) : visible dès qu'un module est présent.
   "<a class='dropdown-item' data-mod='zigbee,lora' href='statusDevices'>"
   "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px;' width='16' height='16' fill='currentColor' class='bi bi-app-indicator' viewBox='0 0 16 16'>"
   "  <path d='M5.5 2A3.5 3.5 0 0 0 2 5.5v5A3.5 3.5 0 0 0 5.5 14h5a3.5 3.5 0 0 0 3.5-3.5V8a.5.5 0 0 1 1 0v2.5a4.5 4.5 0 0 1-4.5 4.5h-5A4.5 4.5 0 0 1 1 10.5v-5A4.5 4.5 0 0 1 5.5 1H8a.5.5 0 0 1 0 1z'/>"
   "  <path d='M16 3a3 3 0 1 1-6 0 3 3 0 0 1 6 0'/>"
   "</svg>"
   " Appareils"
   "</a>"
   "</div>"
   "</li>"
   ""
   "<li class='nav-item dropdown'>"
   "<a class='nav-link dropdown-toggle' href='#' id='navbarDropdown' role='button' data-bs-toggle='dropdown'>"
   "<svg xmlns='http://www.w3.org/2000/svg' style='width:24px;' width='24' height='24' fill='currentColor' class='bi bi-router' viewBox='0 0 16 16'>"
   "  <path d='M5.525 3.025a3.5 3.5 0 0 1 4.95 0 .5.5 0 1 0 .707-.707 4.5 4.5 0 0 0-6.364 0 .5.5 0 0 0 .707.707'/>"
   "  <path d='M6.94 4.44a1.5 1.5 0 0 1 2.12 0 .5.5 0 0 0 .708-.708 2.5 2.5 0 0 0-3.536 0 .5.5 0 0 0 .707.707ZM2.5 11a.5.5 0 1 1 0-1 .5.5 0 0 1 0 1m4.5-.5a.5.5 0 1 0 1 0 .5.5 0 0 0-1 0m2.5.5a.5.5 0 1 1 0-1 .5.5 0 0 1 0 1m1.5-.5a.5.5 0 1 0 1 0 .5.5 0 0 0-1 0m2 0a.5.5 0 1 0 1 0 .5.5 0 0 0-1 0'/>"
   "  <path d='M2.974 2.342a.5.5 0 1 0-.948.316L3.806 8H1.5A1.5 1.5 0 0 0 0 9.5v2A1.5 1.5 0 0 0 1.5 13H2a.5.5 0 0 0 .5.5h2A.5.5 0 0 0 5 13h6a.5.5 0 0 0 .5.5h2a.5.5 0 0 0 .5-.5h.5a1.5 1.5 0 0 0 1.5-1.5v-2A1.5 1.5 0 0 0 14.5 8h-2.306l1.78-5.342a.5.5 0 1 0-.948-.316L11.14 8H4.86zM14.5 9a.5.5 0 0 1 .5.5v2a.5.5 0 0 1-.5.5h-13a.5.5 0 0 1-.5-.5v-2a.5.5 0 0 1 .5-.5z'/>"
   "  <path d='M8.5 5.5a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0'/>"
   "</svg>"
   " Réseau"
   "</a>"
   "<div class='dropdown-menu'>"
   "<a class='dropdown-item' href='configWiFi'>"
   "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px;' width='16' height='16' fill='currentColor' class='bi bi-wifi' viewBox='0 0 16 16'>"
   "  <path d='M15.384 6.115a.485.485 0 0 0-.047-.736A12.44 12.44 0 0 0 8 3C5.259 3 2.723 3.882.663 5.379a.485.485 0 0 0-.048.736.52.52 0 0 0 .668.05A11.45 11.45 0 0 1 8 4c2.507 0 4.827.802 6.716 2.164.205.148.49.13.668-.049'/>"
   "  <path d='M13.229 8.271a.482.482 0 0 0-.063-.745A9.46 9.46 0 0 0 8 6c-1.905 0-3.68.56-5.166 1.526a.48.48 0 0 0-.063.745.525.525 0 0 0 .652.065A8.46 8.46 0 0 1 8 7a8.46 8.46 0 0 1 4.576 1.336c.206.132.48.108.653-.065m-2.183 2.183c.226-.226.185-.605-.1-.75A6.5 6.5 0 0 0 8 9c-1.06 0-2.062.254-2.946.704-.285.145-.326.524-.1.75l.015.015c.16.16.407.19.611.09A5.5 5.5 0 0 1 8 10c.868 0 1.69.201 2.42.56.203.1.45.07.61-.091zM9.06 12.44c.196-.196.198-.52-.04-.66A2 2 0 0 0 8 11.5a2 2 0 0 0-1.02.28c-.238.14-.236.464-.04.66l.706.706a.5.5 0 0 0 .707 0l.707-.707z'/>"
   "</svg>"
   " WiFi"
   "</a>"
   "<a class='dropdown-item' data-mod='zigbee' href='configDevices'>"
   "<svg fill='currentColor' style='width:16px;' width='16' height='16' viewBox='0 0 24 24' role='img' xmlns='http://www.w3.org/2000/svg'>"
   "  <path d='M11.988 0a11.85 11.85 0 00-8.617 3.696c7.02-.875 11.401-.583 13.289-.34 3.752.583 3.558 3.404 3.558 3.404L8.237 19.112c2.299.22 6.897.366 13.796-.631a11.86 11.86 0 001.912-6.469C23.945 5.374 18.595 0 11.988 0zm.232 4.31c-2.451-.014-5.772.146-9.963.723C.854 7.003.055 9.41.055 12.012.055 18.626 5.38 24 11.988 24c3.63 0 6.85-1.63 9.053-4.182-7.286.948-11.813.631-13.75.388-3.775-.56-3.557-3.404-3.557-3.404L15.691 4.474a38.635 38.635 0 00-3.471-.163Z'/>"
   "</svg>"
   " Zigbee"
   "</a>"
   "<a class='dropdown-item' data-mod='lora' href='configLora'>"
   // Logo LoRa officiel (fichier, pas inline : ~10 Ko de tracés, servis une fois et mis en
   // cache, alors que le menu est rendu sur chaque page). Symbole portrait (ratio 0.58).
   SVG_LORA_ICON
   " LoRa"
   "</a>"
   "</div>"
   "</li>"
   ""
   "<li class='nav-item dropdown'>"
   "<a class='nav-link dropdown-toggle' href='#' id='navbarDropdown' role='button' data-bs-toggle='dropdown'>"
   "<svg style='width:24px;' width='24' height='24' viewBox='0 0 16 16' xmlns='http://www.w3.org/2000/svg' fill='currentColor'>"
   "  <path fill='#000000' fill-rule='evenodd' d='M13.75.5a2.25 2.25 0 00-1.847 3.536l-.933.934a.752.752 0 00-.11.14c-.19-.071-.396-.11-.61-.11h-2.5A1.75 1.75 0 006 6.75v.5H4.372a2.25 2.25 0 100 1.5H6v.5c0 .966.784 1.75 1.75 1.75h2.5c.214 0 .42-.039.61-.11.03.05.067.097.11.14l.933.934a2.25 2.25 0 101.24-.881L12.03 9.97a.75.75 0 00-.14-.11c.071-.19.11-.396.11-.61v-2.5c0-.214-.039-.42-.11-.61a.75.75 0 00.14-.11l1.113-1.113A2.252 2.252 0 0016 2.75 2.25 2.25 0 0013.75.5zM13 2.75a.75.75 0 111.5 0 .75.75 0 01-1.5 0zM7.75 6.5a.25.25 0 00-.25.25v2.5c0 .138.112.25.25.25h2.5a.25.25 0 00.25-.25v-2.5a.25.25 0 00-.25-.25h-2.5zm6 6a.75.75 0 100 1.5.75.75 0 000-1.5zM1.5 8A.75.75 0 113 8a.75.75 0 01-1.5 0z' clip-rule='evenodd'/>"
   "</svg>"
   " Passerelle"
   "</a>"
   "<div class='dropdown-menu'>"
   "<a class='dropdown-item' href='/configMQTT'>"
   "<svg role='img' viewBox='0 0 24 24' xmlns='http://www.w3.org/2000/svg' style='width:16px;' height='16' width='16'>"
   "  <path d='M10.657 23.994h-9.45A1.212 1.212 0 0 1 0 22.788v-9.18h0.071c5.784 0 10.504 4.65 10.586 10.386Zm7.606 0h-4.045C14.135 16.246 7.795 9.977 0 9.942V6.038h0.071c9.983 0 18.121 8.044 18.192 17.956Zm4.53 0h-0.97C21.754 12.071 11.995 2.407 0 2.372v-1.16C0 0.55 0.544 0.006 1.207 0.006h7.64C15.733 2.49 21.257 7.789 24 14.508v8.291c0 0.663 -0.544 1.195 -1.207 1.195ZM16.713 0.006h6.092A1.19 1.19 0 0 1 24 1.2v5.914c-0.91 -1.242 -2.046 -2.65 -3.158 -3.762C19.588 2.11 18.122 0.987 16.714 0.005Z' fill='currentColor' stroke-width='1'></path>"
   "</svg>"
   " MQTT"
   "</a>"
   "<a class='dropdown-item' href='/configWebPush'>"
   "<svg style='width:16px;' width='16' height='16' viewBox='0 0 24 24' fill='currentColor' xmlns='http://www.w3.org/2000/svg'>"
   "  <path fill-rule='evenodd' clip-rule='evenodd' d='M9.83824 18.4467C10.0103 18.7692 10.1826 19.0598 10.3473 19.3173C8.59745 18.9238 7.07906 17.9187 6.02838 16.5383C6.72181 16.1478 7.60995 15.743 8.67766 15.4468C8.98112 16.637 9.40924 17.6423 9.83824 18.4467ZM11.1618 17.7408C10.7891 17.0421 10.4156 16.1695 10.1465 15.1356C10.7258 15.0496 11.3442 15 12.0001 15C12.6559 15 13.2743 15.0496 13.8535 15.1355C13.5844 16.1695 13.2109 17.0421 12.8382 17.7408C12.5394 18.3011 12.2417 18.7484 12 19.0757C11.7583 18.7484 11.4606 18.3011 11.1618 17.7408ZM9.75 12C9.75 12.5841 9.7893 13.1385 9.8586 13.6619C10.5269 13.5594 11.2414 13.5 12.0001 13.5C12.7587 13.5 13.4732 13.5593 14.1414 13.6619C14.2107 13.1384 14.25 12.5841 14.25 12C14.25 11.4159 14.2107 10.8616 14.1414 10.3381C13.4732 10.4406 12.7587 10.5 12.0001 10.5C11.2414 10.5 10.5269 10.4406 9.8586 10.3381C9.7893 10.8615 9.75 11.4159 9.75 12ZM8.38688 10.0288C8.29977 10.6478 8.25 11.3054 8.25 12C8.25 12.6946 8.29977 13.3522 8.38688 13.9712C7.11338 14.3131 6.05882 14.7952 5.24324 15.2591C4.76698 14.2736 4.5 13.168 4.5 12C4.5 10.832 4.76698 9.72644 5.24323 8.74088C6.05872 9.20472 7.1133 9.68686 8.38688 10.0288ZM10.1465 8.86445C10.7258 8.95042 11.3442 9 12.0001 9C12.6559 9 13.2743 8.95043 13.8535 8.86447C13.5844 7.83055 13.2109 6.95793 12.8382 6.2592C12.5394 5.69894 12.2417 5.25156 12 4.92432C11.7583 5.25156 11.4606 5.69894 11.1618 6.25918C10.7891 6.95791 10.4156 7.83053 10.1465 8.86445ZM15.6131 10.0289C15.7002 10.6479 15.75 11.3055 15.75 12C15.75 12.6946 15.7002 13.3521 15.6131 13.9711C16.8866 14.3131 17.9412 14.7952 18.7568 15.2591C19.233 14.2735 19.5 13.1679 19.5 12C19.5 10.8321 19.233 9.72647 18.7568 8.74093C17.9413 9.20477 16.8867 9.6869 15.6131 10.0289ZM17.9716 7.46178C17.2781 7.85231 16.39 8.25705 15.3224 8.55328C15.0189 7.36304 14.5908 6.35769 14.1618 5.55332C13.9897 5.23077 13.8174 4.94025 13.6527 4.6827C15.4026 5.07623 16.921 6.08136 17.9716 7.46178ZM8.67765 8.55325C7.61001 8.25701 6.7219 7.85227 6.02839 7.46173C7.07906 6.08134 8.59745 5.07623 10.3472 4.6827C10.1826 4.94025 10.0103 5.23076 9.83823 5.5533C9.40924 6.35767 8.98112 7.36301 8.67765 8.55325ZM15.3224 15.4467C15.0189 16.637 14.5908 17.6423 14.1618 18.4467C13.9897 18.7692 13.8174 19.0598 13.6527 19.3173C15.4026 18.9238 16.921 17.9186 17.9717 16.5382C17.2782 16.1477 16.3901 15.743 15.3224 15.4467ZM12 21C16.9706 21 21 16.9706 21 12C21 7.02944 16.9706 3 12 3C7.02944 3 3 7.02944 3 12C3 16.9706 7.02944 21 12 21Z' fill='currentColor'/>"
   "</svg>"
   " WebPush"
   "</a>"
   "</div>"
   "</li>"
   "<li class='nav-item dropdown'>"
   "<a class='nav-link dropdown-toggle' href='#' id='navbarDropdown' role='button' data-bs-toggle='dropdown'>"
   "<svg xmlns='http://www.w3.org/2000/svg' style='width:24px;' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' class='feather feather-settings'>"
   "  <circle cx='12' cy='12' r='3'></circle>"
   "  <path d='M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z'></path>"
   "</svg>"
   " Config"
   "</a>"
   "<div class='dropdown-menu'>"
   "<a class='dropdown-item' href='/configGeneral'>"
   "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px;' width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' class='feather feather-settings'>"
   "  <circle cx='12' cy='12' r='3'></circle>"
   "  <path d='M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z'></path>"
   "</svg>"
   " General"
   "</a>"
   "<a class='dropdown-item' href='configEnergy'>"
   "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px; width='16' height='16' fill='currentColor' class='bi bi-flower1' viewBox='0 0 16 16'>"
    "<path d='M6.174 1.184a2 2 0 0 1 3.652 0A2 2 0 0 1 12.99 3.01a2 2 0 0 1 1.826 3.164 2 2 0 0 1 0 3.652 2 2 0 0 1-1.826 3.164 2 2 0 0 1-3.164 1.826 2 2 0 0 1-3.652 0A2 2 0 0 1 3.01 12.99a2 2 0 0 1-1.826-3.164 2 2 0 0 1 0-3.652A2 2 0 0 1 3.01 3.01a2 2 0 0 1 3.164-1.826M8 1a1 1 0 0 0-.998 1.03l.01.091q.017.116.054.296c.049.241.122.542.213.887.182.688.428 1.513.676 2.314L8 5.762l.045-.144c.248-.8.494-1.626.676-2.314.091-.345.164-.646.213-.887a5 5 0 0 0 .064-.386L9 2a1 1 0 0 0-1-1M2 9l.03-.002.091-.01a5 5 0 0 0 .296-.054c.241-.049.542-.122.887-.213a61 61 0 0 0 2.314-.676L5.762 8l-.144-.045a61 61 0 0 0-2.314-.676 17 17 0 0 0-.887-.213 5 5 0 0 0-.386-.064L2 7a1 1 0 1 0 0 2m7 5-.002-.03a5 5 0 0 0-.064-.386 16 16 0 0 0-.213-.888 61 61 0 0 0-.676-2.314L8 10.238l-.045.144c-.248.8-.494 1.626-.676 2.314-.091.345-.164.646-.213.887a5 5 0 0 0-.064.386L7 14a1 1 0 1 0 2 0m-5.696-2.134.025-.017a5 5 0 0 0 .303-.248c.184-.164.408-.377.661-.629A61 61 0 0 0 5.96 9.23l.103-.111-.147.033a61 61 0 0 0-2.343.572c-.344.093-.64.18-.874.258a5 5 0 0 0-.367.138l-.027.014a1 1 0 1 0 1 1.732zM4.5 14.062a1 1 0 0 0 1.366-.366l.014-.027q.014-.03.036-.084a5 5 0 0 0 .102-.283c.078-.233.165-.53.258-.874a61 61 0 0 0 .572-2.343l.033-.147-.11.102a61 61 0 0 0-1.743 1.667 17 17 0 0 0-.629.66 5 5 0 0 0-.248.304l-.017.025a1 1 0 0 0 .366 1.366m9.196-8.196a1 1 0 0 0-1-1.732l-.025.017a5 5 0 0 0-.303.248 17 17 0 0 0-.661.629A61 61 0 0 0 10.04 6.77l-.102.111.147-.033a61 61 0 0 0 2.342-.572c.345-.093.642-.18.875-.258a5 5 0 0 0 .367-.138zM11.5 1.938a1 1 0 0 0-1.366.366l-.014.027q-.014.03-.036.084a5 5 0 0 0-.102.283c-.078.233-.165.53-.258.875a61 61 0 0 0-.572 2.342l-.033.147.11-.102a61 61 0 0 0 1.743-1.667c.252-.253.465-.477.629-.66a5 5 0 0 0 .248-.304l.017-.025a1 1 0 0 0-.366-1.366M14 9a1 1 0 0 0 0-2l-.03.002a5 5 0 0 0-.386.064c-.242.049-.543.122-.888.213-.688.182-1.513.428-2.314.676L10.238 8l.144.045c.8.248 1.626.494 2.314.676.345.091.646.164.887.213a5 5 0 0 0 .386.064zM1.938 4.5a1 1 0 0 0 .393 1.38l.084.035q.108.045.283.103c.233.078.53.165.874.258a61 61 0 0 0 2.343.572l.147.033-.103-.111a61 61 0 0 0-1.666-1.742 17 17 0 0 0-.66-.629 5 5 0 0 0-.304-.248l-.025-.017a1 1 0 0 0-1.366.366m2.196-1.196.017.025a5 5 0 0 0 .248.303c.164.184.377.408.629.661A61 61 0 0 0 6.77 5.96l.111.102-.033-.147a61 61 0 0 0-.572-2.342c-.093-.345-.18-.642-.258-.875a5 5 0 0 0-.138-.367l-.014-.027a1 1 0 1 0-1.732 1m9.928 8.196a1 1 0 0 0-.366-1.366l-.027-.014a5 5 0 0 0-.367-.138c-.233-.078-.53-.165-.875-.258a61 61 0 0 0-2.342-.572l-.147-.033.102.111a61 61 0 0 0 1.667 1.742c.253.252.477.465.66.629a5 5 0 0 0 .304.248l.025.017a1 1 0 0 0 1.366-.366m-3.928 2.196a1 1 0 0 0 1.732-1l-.017-.025a5 5 0 0 0-.248-.303 17 17 0 0 0-.629-.661A61 61 0 0 0 9.23 10.04l-.111-.102.033.147a61 61 0 0 0 .572 2.342c.093.345.18.642.258.875a5 5 0 0 0 .138.367zM8 9.5a1.5 1.5 0 1 0 0-3 1.5 1.5 0 0 0 0 3'/>"
  "</svg>"
   " Energie"
   "</a>"
   "<a class='dropdown-item' href='configThermostats'>"
   "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px;' width='16' height='16' fill='currentColor' class='bi bi-thermometer-half' viewBox='0 0 16 16'>"
   "  <path d='M9.5 12.5a1.5 1.5 0 1 1-2-1.415V6.5a.5.5 0 0 1 1 0v4.585a1.5 1.5 0 0 1 1 1.415'/>"
   "  <path d='M5.5 2.5a2.5 2.5 0 0 1 5 0v7.55a3.5 3.5 0 1 1-5 0zM8 1a1.5 1.5 0 0 0-1.5 1.5v7.987l-.167.15a2.5 2.5 0 1 0 3.333 0l-.166-.15V2.5A1.5 1.5 0 0 0 8 1'/>"
   "</svg>"
   " Thermostat"
   "</a>"
   "<a class='dropdown-item' href='/configNotifications'>"
   "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' style='width:16px;' class='bi bi-bell' viewBox='0 0 16 16'>"
   "  <path d='M8 16a2 2 0 0 0 2-2H6a2 2 0 0 0 2 2M8 1.918l-.797.161A4 4 0 0 0 4 6c0 .628-.134 2.197-.459 3.742-.16.767-.376 1.566-.663 2.258h10.244c-.287-.692-.502-1.49-.663-2.258C12.134 8.197 12 6.628 12 6a4 4 0 0 0-3.203-3.92zM14.22 12c.223.447.481.801.78 1H1c.299-.199.557-.553.78-1C2.68 10.2 3 6.88 3 6c0-2.42 1.72-4.44 4.005-4.901a1 1 0 1 1 1.99 0A5 5 0 0 1 13 6c0 .88.32 4.2 1.22 6'/>"
   "</svg>"
   " Notifications"
   "</a>"
    "<a class='dropdown-item' href='/configRules'>"
   "<svg style='width:16px;' xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' class='bi bi-file-ruled' viewBox='0 0 16 16'>"
   "  <path d='M2 2a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2zm2-1a1 1 0 0 0-1 1v4h10V2a1 1 0 0 0-1-1zm9 6H6v2h7zm0 3H6v2h7zm0 3H6v2h6a1 1 0 0 0 1-1zm-8 2v-2H3v1a1 1 0 0 0 1 1zm-2-3h2v-2H3zm0-3h2V7H3z'/>"
   "</svg>"
   " Règles"
   "</a>"
   "<a class='dropdown-item' href='/configHorloge'>"
   "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' style='width:16px;' class='bi bi-clock' viewBox='0 0 16 16'>"
   "  <path d='M8 3.5a.5.5 0 0 0-1 0V9a.5.5 0 0 0 .252.434l3.5 2a.5.5 0 0 0 .496-.868L8 8.71z'></path><path d='M8 16A8 8 0 1 0 8 0a8 8 0 0 0 0 16m7-8A7 7 0 1 1 1 8a7 7 0 0 1 14 0'></path>"
   "</svg>"
   " Horloge"
   "</a>"
   "<a class='dropdown-item' href='/configHTTP'>"
   "<svg style='width:16px;' width='16' height='16' viewBox='0 0 24 24' fill='none' xmlns='http://www.w3.org/2000/svg'>"
   "  <path d='M2 16C2 13.1716 2 11.7574 2.87868 10.8787C3.75736 10 5.17157 10 8 10H16C18.8284 10 20.2426 10 21.1213 10.8787C22 11.7574 22 13.1716 22 16C22 18.8284 22 20.2426 21.1213 21.1213C20.2426 22 18.8284 22 16 22H8C5.17157 22 3.75736 22 2.87868 21.1213C2 20.2426 2 18.8284 2 16Z' stroke='currentColor' stroke-width='1.5'/>"
   "  <path d='M6 10V8C6 4.68629 8.68629 2 12 2C15.3137 2 18 4.68629 18 8V10' stroke='currentColor' stroke-width='1.5' stroke-linecap='round'/>"
   "</svg>"
   " Securité"
   "</a>"
   "<a class='dropdown-item' href='/configTunnel'>"
   "<svg style='width:16px;' width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' xmlns='http://www.w3.org/2000/svg'>"
   "  <path d='M12 2L2 7l10 5 10-5-10-5z'/>"
   "  <path d='M2 17l10 5 10-5'/>"
   "  <path d='M2 12l10 5 10-5'/>"
   "</svg>"
   " Tunnel"
   "</a>"
   "</div>"
   "</li>"
   "<li class='nav-item' id='Tools'>"
   "<a class='nav-link' href='/tools'>"
   "<svg viewBox='0 0 24 24' style='width:24px;' width='24' height='24' stroke='currentColor' stroke-width='2' fill='none' stroke-linecap='round' stroke-linejoin='round' class='css-i6dzq1'>"
   "  <path d='M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94l-3.76 3.76z'></path>"
   "</svg>"
   " Outils"
   "</a>"
   "</li>"
   "<li class='nav-item' id='Notifs'>"
   "<a class='nav-link' href='/notifications'>"
   "<svg xmlns='http://www.w3.org/2000/svg' style='width:24px;' width='24' height='24' fill='currentColor' class='bi bi-bell' viewBox='0 0 16 16'>"
      "<path d='M8 16a2 2 0 0 0 2-2H6a2 2 0 0 0 2 2M8 1.918l-.797.161A4 4 0 0 0 4 6c0 .628-.134 2.197-.459 3.742-.16.767-.376 1.566-.663 2.258h10.244c-.287-.692-.502-1.49-.663-2.258C12.134 8.197 12 6.628 12 6a4 4 0 0 0-3.203-3.92zM14.22 12c.223.447.481.801.78 1H1c.299-.199.557-.553.78-1C2.68 10.2 3 6.88 3 6c0-2.42 1.72-4.44 4.005-4.901a1 1 0 1 1 1.99 0A5 5 0 0 1 13 6c0 .88.32 4.2 1.22 6'/>"
   "</svg>"
   " Notifications"
   "<div class='AlertNotif' style='display: none; width: 8px;height: 8px; background-color: red; margin-left: 4px; vertical-align: middle;border-radius: 50%;  '></div>"
   "</a>"
   "</li>"
   "<li class='nav-item dropdown'>"
   "<a class='dropdown-item' href='/help'>"
    "<a class='nav-link dropdown-toggle' href='#' id='navbarDropdown' role='button' data-bs-toggle='dropdown'>"
   "<svg xmlns='http://www.w3.org/2000/svg' style='width:24px;' width='24' height='24' fill='currentColor' class='bi bi-question-circle' viewBox='0 0 16 16'>"
   "  <path d='M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16'/>"
   "  <path d='M5.255 5.786a.237.237 0 0 0 .241.247h.825c.138 0 .248-.113.266-.25.09-.656.54-1.134 1.342-1.134.686 0 1.314.343 1.314 1.168 0 .635-.374.927-.965 1.371-.673.489-1.206 1.06-1.168 1.987l.003.217a.25.25 0 0 0 .25.246h.811a.25.25 0 0 0 .25-.25v-.105c0-.718.273-.927 1.01-1.486.609-.463 1.244-.977 1.244-2.056 0-1.511-1.276-2.241-2.673-2.241-1.267 0-2.655.59-2.75 2.286m1.557 5.763c0 .533.425.927 1.01.927.609 0 1.028-.394 1.028-.927 0-.552-.42-.94-1.029-.94-.584 0-1.009.388-1.009.94'/>"
   "</svg>"
   " A propos"
   "<div class='AboutMaj' style='display:none; width: 8px;height: 8px; background-color: red; margin-left: 4px; vertical-align: middle;border-radius: 50%;  '></div>"
   "</a>"
   "<div class='dropdown-menu'>"
   "<a class='dropdown-item' href='/help'>"
   "<svg xmlns='http://www.w3.org/2000/svg' style='width:24px;' width='24' height='24' fill='currentColor' class='bi bi-question-circle' viewBox='0 0 16 16'>"
   "  <path d='M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16'/>"
   "  <path d='M5.255 5.786a.237.237 0 0 0 .241.247h.825c.138 0 .248-.113.266-.25.09-.656.54-1.134 1.342-1.134.686 0 1.314.343 1.314 1.168 0 .635-.374.927-.965 1.371-.673.489-1.206 1.06-1.168 1.987l.003.217a.25.25 0 0 0 .25.246h.811a.25.25 0 0 0 .25-.25v-.105c0-.718.273-.927 1.01-1.486.609-.463 1.244-.977 1.244-2.056 0-1.511-1.276-2.241-2.673-2.241-1.267 0-2.655.59-2.75 2.286m1.557 5.763c0 .533.425.927 1.01.927.609 0 1.028-.394 1.028-.927 0-.552-.42-.94-1.029-.94-.584 0-1.009.388-1.009.94'/>"
   "</svg>"
   " Info"
   "</a>"
   "<a class='dropdown-item' href='statusNetwork'>"
   "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px;' width='16' height='16' fill='currentColor' class='bi bi-wifi' viewBox='0 0 16 16'>"
   "  <path d='M15.384 6.115a.485.485 0 0 0-.047-.736A12.44 12.44 0 0 0 8 3C5.259 3 2.723 3.882.663 5.379a.485.485 0 0 0-.048.736.52.52 0 0 0 .668.05A11.45 11.45 0 0 1 8 4c2.507 0 4.827.802 6.716 2.164.205.148.49.13.668-.049'/>"
   "  <path d='M13.229 8.271a.482.482 0 0 0-.063-.745A9.46 9.46 0 0 0 8 6c-1.905 0-3.68.56-5.166 1.526a.48.48 0 0 0-.063.745.525.525 0 0 0 .652.065A8.46 8.46 0 0 1 8 7a8.46 8.46 0 0 1 4.576 1.336c.206.132.48.108.653-.065m-2.183 2.183c.226-.226.185-.605-.1-.75A6.5 6.5 0 0 0 8 9c-1.06 0-2.062.254-2.946.704-.285.145-.326.524-.1.75l.015.015c.16.16.407.19.611.09A5.5 5.5 0 0 1 8 10c.868 0 1.69.201 2.42.56.203.1.45.07.61-.091zM9.06 12.44c.196-.196.198-.52-.04-.66A2 2 0 0 0 8 11.5a2 2 0 0 0-1.02.28c-.238.14-.236.464-.04.66l.706.706a.5.5 0 0 0 .707 0l.707-.707z'/>"
   "</svg>"
   " Système"
   "</a>"
   "<a class='dropdown-item' href='/update'>"
   "<svg style='width:16px;' xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' class='bi bi-download' viewBox='0 0 16 16'>"
    "  <path d='M.5 9.9a.5.5 0 0 1 .5.5v2.5a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-2.5a.5.5 0 0 1 1 0v2.5a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2v-2.5a.5.5 0 0 1 .5-.5'/>"
    "  <path d='M7.646 11.854a.5.5 0 0 0 .708 0l3-3a.5.5 0 0 0-.708-.708L8.5 10.293V1.5a.5.5 0 0 0-1 0v8.793L5.354 8.146a.5.5 0 1 0-.708.708z'/>"
    "</svg>"
   " Mise à jour"
   "<div class='AboutMaj' style='display: none; width: 8px;height: 8px; background-color: red; margin-left: 4px; vertical-align: middle;border-radius: 50%;  '></div>"
   "</a>"
   "<div class='dropdown-divider logoutItem' style='display:none'></div>"
   "<a class='dropdown-item logoutItem' href='/logout' style='display:none'>"
   "<svg style='width:16px;' xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' viewBox='0 0 16 16'>"
   "  <path fill-rule='evenodd' d='M10 12.5a.5.5 0 0 1-.5.5h-8a.5.5 0 0 1-.5-.5v-9a.5.5 0 0 1 .5-.5h8a.5.5 0 0 1 .5.5v2a.5.5 0 0 0 1 0v-2A1.5 1.5 0 0 0 9.5 2h-8A1.5 1.5 0 0 0 0 3.5v9A1.5 1.5 0 0 0 1.5 14h8a1.5 1.5 0 0 0 1.5-1.5v-2a.5.5 0 0 0-1 0z'/>"
   "  <path fill-rule='evenodd' d='M15.854 8.354a.5.5 0 0 0 0-.708l-3-3a.5.5 0 0 0-.708.708L14.293 7.5H5.5a.5.5 0 0 0 0 1h8.793l-2.147 2.146a.5.5 0 0 0 .708.708z'/>"
   "</svg>"
   " Déconnexion"
   "</a>"
   "</div>"
   "</li>"
   "</ul></div></div>"
   "</nav>"
   ""
   "<div id='alert' style='display:none;' class='alert alert-success' role='alert'>"
   "</div>";

// Script du menu (spinner + navigation par glissement). Servi dans /menu.js, APRES le document.write
// du nav (donc les liens du menu existent quand lnk() les resout).
const char HTTP_MENU_JS[] PROGMEM =
   "(function(){"
   // URL des liens du menu (résolues, robustes au tunnel) ; repli sur l'URL relative si lien absent.
   "function lnk(h){var a=document.querySelector(\"a[href='\"+h+\"']\");return a?a.href:h;}"
   "var eU=lnk('statusEnergy'),tU=lnk('thermostats'),dU=lnk('statusDevices');"
   // spinner (style + fonction)
   "var stl=document.createElement('style');stl.innerHTML='@keyframes tspin{to{transform:rotate(360deg)}}body{overflow-x:hidden;}';document.head.appendChild(stl);"
   "var sp=null;function showSp(){if(sp)return;document.body.style.transform='';sp=document.createElement('div');sp.style.cssText='position:fixed;inset:0;z-index:99999;display:flex;align-items:center;justify-content:center;background:#fff;';"
   "sp.innerHTML=\"<div style='width:48px;height:48px;border:5px solid #ccc;border-top-color:#2980b9;border-radius:50%;animation:tspin 0.8s linear infinite;'></div>\";document.body.appendChild(sp);}"
   // spinner immédiat sur toute navigation par lien (menu inclus)
   "document.addEventListener('click',function(e){var a=e.target.closest?e.target.closest('a'):null;if(!a)return;var h=a.getAttribute('href');if(!h||h.charAt(0)=='#'||h.indexOf('javascript')==0)return;if(a.target&&a.target!='_self')return;if(a.hasAttribute('download'))return;showSp();},true);"
   // spinner sur soumission de formulaire (POST) — en phase bubble pour voir si le form a fait
   // preventDefault (upload AJAX chunké : maj firmware, historique, restore... => pas de navigation),
   // et on skippe aussi les uploads multipart classiques.
   "document.addEventListener('submit',function(e){if(e.defaultPrevented)return;var f=e.target;if(f&&f.getAttribute&&(f.getAttribute('enctype')||'').indexOf('multipart')>=0)return;showSp();},false);"
   // spinner à CHAQUE chargement de page, masqué une fois la page prête (filet de sécurité 8 s)
   "function hideSp(){if(sp&&sp.parentNode){sp.parentNode.removeChild(sp);}sp=null;}"
   "showSp();"
   "if(document.readyState=='complete'){hideSp();}else{window.addEventListener('load',hideSp);}"
   "setTimeout(hideSp,8000);"
   // page courante par sous-chaine du chemin (Energie = page par defaut a la racine)
   "var pp=location.pathname;"
   "var cur=pp.indexOf('thermostats')>=0?'t':(pp.indexOf('statusDevices')>=0?'d':((pp.indexOf('statusEnergy')>=0||pp=='/'||pp=='')?'e':''));"
   "if(!cur)return;"
   "var L='',R='';if(cur=='e'){L=dU;R=tU;}else if(cur=='t'){L=eU;R='';}else{L='';R=eU;}"
   "var sx=0,sy=0,drag=false,dec=false,hz=false,b=document.body;"
   // laisse le défilement vertical au navigateur mais réserve l'horizontal au swipe (pages longues)
   "b.style.touchAction='pan-y';"
   "function bg(x,y){if(window.innerWidth>900)return;sx=x;sy=y;drag=true;dec=false;hz=false;b.style.transition='none';}"
   "function mv(x,y){if(!drag)return true;var dx=x-sx,dy=y-sy;if(!dec){if(Math.abs(dx)>10||Math.abs(dy)>10){dec=true;hz=Math.abs(dx)>Math.abs(dy);}}"
   "if(dec&&hz){var eff=dx;if((dx<0&&!L)||(dx>0&&!R))eff=dx*0.3;b.style.transform='translateX('+eff+'px)';return false;}return true;}"
   "function en(x,y){if(!drag)return;drag=false;var dx=x-sx,dy=y-sy;b.style.transition='transform 0.25s ease';"
   "if(hz&&Math.abs(dx)>70&&Math.abs(dx)>Math.abs(dy)*1.2){var d=dx<0?L:R;if(d){b.style.transition='none';b.style.transform='';showSp();setTimeout(function(){location.href=d;},60);return;}}"
   "b.style.transform='translateX(0)';setTimeout(function(){if(!drag)b.style.transform='';},260);}"
   "document.addEventListener('touchstart',function(e){if(e.touches.length!=1)return;bg(e.touches[0].clientX,e.touches[0].clientY);},{passive:true});"
   "document.addEventListener('touchmove',function(e){if(mv(e.touches[0].clientX,e.touches[0].clientY)===false&&e.cancelable)e.preventDefault();},{passive:false});"
   "document.addEventListener('touchend',function(e){var t=e.changedTouches[0];en(t.clientX,t.clientY);});"
   "document.addEventListener('mousedown',function(e){bg(e.clientX,e.clientY);});"
   "document.addEventListener('mousemove',function(e){mv(e.clientX,e.clientY);});"
   "document.addEventListener('mouseup',function(e){en(e.clientX,e.clientY);});"
   "})();";


const char HTTP_TOOLS[] PROGMEM =
    "<style>"
    ".tools-container{max-width:1200px;margin:0 auto;padding:20px;}"
    ".tools-header{margin-bottom:30px;}"
    ".tools-header h4{margin:0;font-size:24px;font-weight:600;color:#333;}"
    ".tools-header p{color:#6c757d;margin-top:5px;}"
    ".tools-section{margin-bottom:30px;}"
    ".tools-section-title{font-size:14px;font-weight:600;color:#6c757d;text-transform:uppercase;letter-spacing:0.5px;margin-bottom:15px;padding-bottom:10px;border-bottom:1px solid #dee2e6;}"
    ".tools-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:15px;}"
    ".tool-card{display:flex;flex-direction:column;align-items:center;justify-content:center;padding:20px 15px;background:#fff;border-radius:12px;border:1px solid #dee2e6;text-decoration:none;color:#333;transition:all 0.2s ease;min-height:120px;}"
    ".tool-card:hover{transform:translateY(-3px);box-shadow:0 8px 25px rgba(0,0,0,0.1);border-color:#0d6efd;text-decoration:none;color:#333;}"
    ".tool-card:active{transform:translateY(0);}"
    ".tool-card .icon{width:48px;height:48px;border-radius:12px;display:flex;align-items:center;justify-content:center;margin-bottom:12px;}"
    ".tool-card .icon svg{width:24px;height:24px;}"
    ".tool-card .icon.blue{background:#e7f1ff;color:#0d6efd;}"
    ".tool-card .icon.green{background:#d1e7dd;color:#198754;}"
    ".tool-card .icon.orange{background:#fff3cd;color:#fd7e14;}"
    ".tool-card .icon.red{background:#f8d7da;color:#dc3545;}"
    ".tool-card .icon.purple{background:#e2d9f3;color:#6f42c1;}"
    ".tool-card .icon.cyan{background:#cff4fc;color:#0dcaf0;}"
    ".tool-card .label{font-size:13px;font-weight:500;text-align:center;}"
    ".tool-card .desc{font-size:11px;color:#6c757d;text-align:center;margin-top:4px;}"
    "@media(max-width:576px){"
    ".tools-grid{grid-template-columns:repeat(2,1fr);gap:10px;}"
    ".tool-card{padding:15px 10px;min-height:100px;}"
    ".tool-card .icon{width:40px;height:40px;margin-bottom:8px;}"
    ".tool-card .icon svg{width:20px;height:20px;}"
    ".tool-card .label{font-size:12px;}"
    ".tool-card .desc{display:none;}"
    "}"
    "</style>"
    "<div class='tools-container'>"
    "<div class='tools-header'>"
    "<h4>Outils avancés</h4>"
    "</div>"

    // Section Fichiers & Données
    "<div class='tools-section'>"
    "<div class='tools-section-title'>Fichiers & Données</div>"
    "<div class='tools-grid'>"

    "<a href='/fsbrowser' class='tool-card'>"
    "<div class='icon blue'><svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'><path d='M5.5 2A3.5 3.5 0 0 0 2 5.5v5A3.5 3.5 0 0 0 5.5 14h5a3.5 3.5 0 0 0 3.5-3.5V8a.5.5 0 0 1 1 0v2.5a4.5 4.5 0 0 1-4.5 4.5h-5A4.5 4.5 0 0 1 1 10.5v-5A4.5 4.5 0 0 1 5.5 1H8a.5.5 0 0 1 0 1z'/><path d='M16 3a3 3 0 1 1-6 0 3 3 0 0 1 6 0'/></svg></div>"
    "<span class='label'>Appareils</span>"
    "<span class='desc'>Fichiers devices</span>"
    "</a>"

    "<a href='/hst' class='tool-card'>"
    "<div class='icon green'><svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'><path d='M8.515 1.019A7 7 0 0 0 8 1V0a8 8 0 0 1 .589.022l-.074.997zm2.004.45a7.003 7.003 0 0 0-.985-.299l.219-.976c.383.086.76.2 1.126.342l-.36.933zm1.37.71a7.01 7.01 0 0 0-.439-.27l.493-.87a8.025 8.025 0 0 1 .979.654l-.615.789a6.996 6.996 0 0 0-.418-.302zm1.834 1.79a6.99 6.99 0 0 0-.653-.796l.724-.69c.27.285.52.59.747.91l-.818.576zm.744 1.352a7.08 7.08 0 0 0-.214-.468l.893-.45a7.976 7.976 0 0 1 .45 1.088l-.95.313a7.023 7.023 0 0 0-.179-.483zm.53 2.507a6.991 6.991 0 0 0-.1-1.025l.985-.17c.067.386.106.778.116 1.17l-1 .025zm-.131 1.538c.033-.17.06-.339.081-.51l.993.123a7.957 7.957 0 0 1-.23 1.155l-.964-.267c.046-.165.086-.332.12-.501zm-.952 2.379c.184-.29.346-.594.486-.908l.914.405c-.16.36-.345.706-.555 1.038l-.845-.535zm-.964 1.205c.122-.122.239-.248.35-.378l.758.653a8.073 8.073 0 0 1-.401.432l-.707-.707z'/><path d='M8 1a7 7 0 1 0 4.95 11.95l.707.707A8.001 8.001 0 1 1 8 0v1z'/><path d='M7.5 3a.5.5 0 0 1 .5.5v5.21l3.248 1.856a.5.5 0 0 1-.496.868l-3.5-2A.5.5 0 0 1 7 9V3.5a.5.5 0 0 1 .5-.5z'/></svg></div>"
    "<span class='label'>Historique</span>"
    "<span class='desc'>Données history</span>"
    "</a>"

    "<a href='/tp' class='tool-card'>"
    "<div class='icon purple'><svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'><path d='M14 4.5V14a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V2a2 2 0 0 1 2-2h5.5L14 4.5zm-3 0A1.5 1.5 0 0 1 9.5 3V1H4a1 1 0 0 0-1 1v12a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1V4.5h-2z'/><path d='M4.5 12.5A.5.5 0 0 1 5 12h3a.5.5 0 0 1 0 1H5a.5.5 0 0 1-.5-.5zm0-2A.5.5 0 0 1 5 10h6a.5.5 0 0 1 0 1H5a.5.5 0 0 1-.5-.5zm1.639-3.708 1.33.886 1.854-1.855a.25.25 0 0 1 .289-.047l1.888.974V8.5a.5.5 0 0 1-.5.5H5a.5.5 0 0 1-.5-.5V8s1.54-1.274 1.639-1.208zM6.25 6a.75.75 0 1 0 0-1.5.75.75 0 0 0 0 1.5z'/></svg></div>"
    "<span class='label'>Templates</span>"
    "<span class='desc'>Modèles JSON</span>"
    "</a>"

    "<a href='/rules' class='tool-card'>"
    "<div class='icon orange'><svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'><path d='M2 2a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2zm2-1a1 1 0 0 0-1 1v4h10V2a1 1 0 0 0-1-1zm9 6H6v2h7zm0 3H6v2h7zm0 3H6v2h6a1 1 0 0 0 1-1zm-8 2v-2H3v1a1 1 0 0 0 1 1zm-2-3h2v-2H3zm0-3h2V7H3z'/></svg></div>"
    "<span class='label'>Règles</span>"
    "<span class='desc'>Automatisations</span>"
    "</a>"

    "</div>"
    "</div>"

    // Section Système
    "<div class='tools-section'>"
    "<div class='tools-section-title'>Système</div>"
    "<div class='tools-grid'>"

    "<a href='/debugFiles' class='tool-card'>"
    "<div class='icon cyan'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><polyline points='4 17 10 11 4 5'></polyline><line x1='12' y1='19' x2='20' y2='19'></line></svg></div>"
    "<span class='label'>Debug</span>"
    "<span class='desc'>Console logs</span>"
    "</a>"

    /* GESTIONNAIRE DE FICHIERS - desactive temporairement
    "<a href='/filesManager' class='tool-card'>"
    "<div class='icon cyan'><svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'><path d='M1 3.5A1.5 1.5 0 0 1 2.5 2h2.764c.958 0 1.76.56 2.311 1.184C7.985 3.648 8.48 4 9 4h4.5A1.5 1.5 0 0 1 15 5.5v7a1.5 1.5 0 0 1-1.5 1.5h-11A1.5 1.5 0 0 1 1 12.5zM2.5 3a.5.5 0 0 0-.5.5V6h12v-.5a.5.5 0 0 0-.5-.5H9c-.964 0-1.71-.629-2.174-1.154C6.374 3.334 5.82 3 5.264 3zM14 7H2v5.5a.5.5 0 0 0 .5.5h11a.5.5 0 0 0 .5-.5z'/></svg></div>"
    "<span class='label'>Fichiers</span>"
    "<span class='desc'>Espace disque</span>"
    "</a>"
    */

    "<a href='/backup' class='tool-card'>"
    "<div class='icon purple'><svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'><path fill-rule='evenodd' d='M7.646 5.146a.5.5 0 0 1 .708 0l2 2a.5.5 0 0 1-.708.708L8.5 6.707V10.5a.5.5 0 0 1-1 0V6.707L6.354 7.854a.5.5 0 1 1-.708-.708l2-2z'/><path d='M4.406 3.342A5.53 5.53 0 0 1 8 2c2.69 0 4.923 2 5.166 4.579C14.758 6.804 16 8.137 16 9.773 16 11.569 14.502 13 12.687 13H3.781C1.708 13 0 11.366 0 9.318c0-1.763 1.266-3.223 2.942-3.593.143-.863.698-1.723 1.464-2.383zm.653.757c-.757.653-1.153 1.44-1.153 2.056v.448l-.445.049C2.064 6.805 1 7.952 1 9.318 1 10.785 2.23 12 3.781 12h8.906C13.98 12 15 10.988 15 9.773c0-1.216-1.02-2.228-2.313-2.228h-.5v-.5C12.188 4.825 10.328 3 8 3a4.53 4.53 0 0 0-2.941 1.1z'/></svg></div>"
    "<span class='label'>Sauvegarde</span>"
    "<span class='desc'>Backup config</span>"
    "</a>"

    "<a href='/reboot' class='tool-card'>"
    "<div class='icon red'><svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'><path d='M11.534 7h3.932a.25.25 0 0 1 .192.41l-1.966 2.36a.25.25 0 0 1-.384 0l-1.966-2.36a.25.25 0 0 1 .192-.41zm-11 2h3.932a.25.25 0 0 0 .192-.41L2.692 6.23a.25.25 0 0 0-.384 0L.342 8.59A.25.25 0 0 0 .534 9z'/><path fill-rule='evenodd' d='M8 3c-1.552 0-2.94.707-3.857 1.818a.5.5 0 1 1-.771-.636A6.002 6.002 0 0 1 13.917 7H12.9A5.002 5.002 0 0 0 8 3zM3.1 9a5.002 5.002 0 0 0 8.757 2.182.5.5 0 1 1 .771.636A6.002 6.002 0 0 1 2.083 9H3.1z'/></svg></div>"
    "<span class='label'>Redémarrer</span>"
    "<span class='desc'>Reboot système</span>"
    "</a>"

    "<a href='#' onclick='fetch(\"/cmdCleanGhosts\").then(()=>alert(\"Nettoyage lancé\"));return false;' class='tool-card'>"
    "<div class='icon orange'><svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'><path d='M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z'/><path d='M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H5.5l1-1h3l1 1H14a1 1 0 0 1 .5.5V3zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z'/></svg></div>"
    "<span class='label'>Fantômes</span>"
    "<span class='desc'>Nettoyer ZiGate</span>"
    "</a>"

    "</div>"
    "</div>"
    "</div>"; 

const char HTTP_HISTORY[] PROGMEM = 
    "<form method='POST' action='/doUploadHistory' enctype='multipart/form-data' id='upload_form'>"
    "<input type='file' name='update' id='file' onchange='sub(this)' style=display:none accept='*.*'>"
    "<label id='file-input' for='file'>Choose history...</label>"
    "<input type='submit' class='btn btn-warning mb-2' value='Restore'>"
    "<br><br>"
    "<div id='prg'></div>"
    "<br><div id='prgbar'><div id='bar'></div></div><br></form>"

    "<script>"
    "function sub(obj){"
    "var fileName = obj.value.split('\\\\');"
    "document.getElementById('file-input').innerHTML = '   '+ fileName[fileName.length-1];"
    "};"
    "$('form').submit(function(e){"
    "e.preventDefault();"
    "var form = $('#upload_form')[0];"
    "var data = new FormData(form);"
    "$.ajax({"
    "url: '/doUploadHistory',"
    "type: 'POST',"
    "data: data,"
    "contentType: false,"
    "processData:false,"
    "xhr: function() {"
    "var xhr = new window.XMLHttpRequest();"
    "xhr.upload.addEventListener('progress', function(evt) {"
    "if (evt.lengthComputable) {"
    "var per = evt.loaded / evt.total;"
    "$('#prg').html( Math.round(per*100) + '%');"
    "$('#bar').css('width',Math.round(per*100) + '%');"
    "}"
    "}, false);"
    "return xhr;"
    "},"
    "success:function(d, s) {"
    "console.log('success!');"
    "$('#prg').html('restore completed!<br>Rebooting!');"
    "window.location.href='/hst';"
    "},"
    "error: function (a, b, c) {"
    "}"
    "});"
    "});"
    "</script>";

const char HTTP_OTA[] PROGMEM = R"(
<form method='POST' action='/doUploadOTA' enctype='multipart/form-data' id='upload_form'>
  <input type='file' name='update' id='file' onchange='sub(this)' style=display:none accept='*.*'>
  <label id='file-input' for='file'>Choose OTA file...</label>
  <input type='submit' class='btn btn-warning mb-2' value='Update'>
  <br><br>
  <div id='prg'></div>
  <br><div id='prgbar'><div id='bar'></div></div><br>
</form>

<script>
  function getUrlParameter(name) {
    name = name.replace(/[\[]/, '\\[').replace(/[\]]/, '\\]');
    var regex = new RegExp('[\\?&]' + name + '=([^&#]*)');
    var results = regex.exec(location.search);
    return results === null ? '' : decodeURIComponent(results[1].replace(/\+/g, ' '));
  };

  var deviceId = getUrlParameter('id');
  document.getElementById('upload_form').action = '/doUploadOTA?id=' + deviceId;

  function sub(obj){
    var fileName = obj.value.split('\\');
    document.getElementById('file-input').innerHTML = '   '+ fileName[fileName.length-1];
  };

  function isTunnel() {
    return window.location.hostname.indexOf('lixee-box.fr') >= 0;
  }

  function blobToBase64(blob) {
    return new Promise(function(resolve, reject) {
      var reader = new FileReader();
      reader.onloadend = function() { resolve(reader.result.split(',')[1]); };
      reader.onerror = reject;
      reader.readAsDataURL(blob);
    });
  }

  async function uploadChunkedOta(file) {
    var CHUNK = 8192;
    var total = file.size;
    var chunks = Math.ceil(total / CHUNK);

    $('#prg').html('Initialisation...');
    $('#bar').css('width', '0%');

    var resp = await fetch('/otaInit?id=' + deviceId, {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({totalSize: total})
    });
    if (!resp.ok) { $('#prg').html('Erreur init: ' + resp.status); return; }

    for (var i = 0; i < chunks; i++) {
      var start = i * CHUNK;
      var end = Math.min(start + CHUNK, total);
      var b64 = await blobToBase64(file.slice(start, end));

      resp = await fetch('/otaChunk?n=' + i, {
        method: 'POST',
        headers: {'Content-Type': 'text/plain'},
        body: b64
      });
      if (!resp.ok) {
        $('#prg').html('Erreur chunk ' + i + ': ' + resp.status);
        return;
      }

      var pct = Math.round(((i + 1) / chunks) * 90);
      $('#bar').css('width', pct + '%');
      $('#prg').html('Envoi ' + (i+1) + '/' + chunks);
    }

    $('#prg').html('Finalisation...');
    $('#bar').css('width', '95%');

    resp = await fetch('/otaFinish', {method: 'POST'});
    if (!resp.ok) {
      $('#prg').html('Erreur finish: ' + resp.status);
      return;
    }

    $('#prg').html('Upload completed for device ' + deviceId + '<br> Redirect to config ...');
    $('#bar').css('width', '100%');
    setTimeout(function(){window.location.href='/configDevices';},5000);
  }

  $('form').submit(function(e){
    e.preventDefault();
    var form = $('#upload_form')[0];
    var file = document.getElementById('file').files[0];
    if (!file) return;

    if (isTunnel()) {
      uploadChunkedOta(file);
      return;
    }

    var data = new FormData(form);
    data.append('device_id', deviceId);
    $.ajax({
      url: '/doUploadOTA?id=' + deviceId,
      type: 'POST',
      data: data,
      contentType: false,
      processData:false,
      xhr: function() {
        var xhr = new window.XMLHttpRequest();
        xhr.upload.addEventListener('progress', function(evt) {
          if (evt.lengthComputable) {
            var per = evt.loaded / evt.total;
            $('#prg').html( Math.round(per*100) + '%');
            $('#bar').css('width',Math.round(per*100) + '%');
          }
        }, false);
        return xhr;
      },
      success:function(d, s) {
        console.log('success!');
        $('#prg').html('Upload completed for device ' + deviceId + '<br> Redirect to config ...');
        setTimeout(function(){window.location.href='/configDevices';},5000);
      },
      error: function (a, b, c) {
        console.log('Error updating device ' + deviceId);
      }
    });
  });
</script>
)";

const char HTTP_BACKUP[] PROGMEM =
    "<h4>Backup datas</h4>"
    "<a href='#' class='btn btn-primary mb-2' onClick='createBackupFile()'>Create Backup</a>"
    "<div id='createBackupFile'>"
    "</div>"
    "<h4>Restore datas</h4>"
    "<div id='restoreBackupFile'>"
    "{{listBackupFiles}}"
    "</div>"
    /*"<form method='POST' action='/doRestore' enctype='multipart/form-data' id='upload_form'>"
      "<input type='file' name='update' id='file'  style=display:none accept='.tar'>" //onchange='sub(this)'
      "<label id='file-input' for='file'>Choose backup...</label>"
      "<input type='submit' class='btn btn-warning mb-2' value='Restore'>"
      "<br><br>"
      "<progress id='prg' value='0' max='100' style='width:100%'></progress>"
      "<p id='prgbar'>0%</p><br>"
    "</form>"*/
    "<div class='container py-5'>"
    "   <h1 class='mb-4'>Update</h1>"
    "   <form id='frm' class='mb-4'>"
    "     <div class='mb-3'>"
    "       <label for='f' class='form-label'>Sélection le fichier tar</label>"
    "       <input class='form-control' type='file' id='f' name='archive' accept='.tar,.tar.gz,.gz'>"
    "     </div>"
    "     <button type='submit' class='btn btn-primary'>Start</button>"
    "   </form>"
    "   <div class='progress mb-2' style='height: 1.5rem;'>"
    "     <div"
    "       id='bar'"
    "       class='progress-bar progress-bar-striped progress-bar-animated'"
    "       role='progressbar'"
    "       aria-valuemin='0' aria-valuemax='100'"
    "       style='width: 0%;'>"
    "       0%"
    "     </div>"
    "   </div>"
    "   <div id='status' class='text-muted'>Prêt.</div>"
    "</div>"

    "<script>"
       "  const frm = document.getElementById('frm'),"
       "        f   = document.getElementById('f'),"
       "        bar = document.getElementById('bar'),"
       "        st  = document.getElementById('status');"

       "  frm.addEventListener('submit', e => {"
       "    e.preventDefault();"
       "    const file = f.files[0];"
       "    if (!file) return alert('Choisissez un .tar');"
       
       "    const xhr = new XMLHttpRequest();"
       "    xhr.open('POST','/doRestore');"

       "    xhr.upload.onprogress = ev => {"
       "      if (ev.lengthComputable) {"
       "        const pct = Math.round(ev.loaded/ev.total*40);"
       "        bar.style.width = pct + '%';"
       "        bar.textContent = pct + '%';"
       "      }"
       "    };"

       "    xhr.onload = () => {"
       "      if (xhr.status === 200) {"
       "        bar.classList.remove('progress-bar-animated');"
       "        st.textContent = 'Rebooting ...';"
       "      } else {"
       "         bar.classList.remove('progress-bar-animated');"
       "         bar.classList.add('bg-danger');"
       "         st.textContent = 'Error: ' + xhr.status;"
       "      }"
       "    };"

       "    const fd = new FormData();"
       "    fd.append('archive', file, file.name);"
       "    xhr.send(fd);"
            
       "    st.textContent = 'Loading ...';"
       "     bar.classList.add('progress-bar-animated');"
       "     bar.classList.remove('bg-danger');"
       "     bar.style.width = '0%';"
       "     bar.textContent = '0%';"

       "  });"
       "</script>"

    ;


const char HTTP_CONFIG_PARAM_ENERGY[] PROGMEM = R"rawstring(

  <div class="container py-5">
    <h4 class="mb-4">Config Energie</h4>

    <!-- Nav tabs -->
    <ul class="nav nav-tabs" id="energyTab" role="tablist">
      <li class="nav-item" role="presentation">
        <button
          class="nav-link active"
          id="linky-tab"
          data-bs-toggle="tab"
          data-bs-target="#linky"
          type="button"
          role="tab"
          aria-controls="linky"
          aria-selected="true">
          Linky
        </button>
      </li>
      <li class="nav-item" role="presentation">
        <button
          class="nav-link"
          id="production-tab"
          data-bs-toggle="tab"
          data-bs-target="#production"
          type="button"
          role="tab"
          aria-controls="production"
          aria-selected="false">
          Production
        </button>
      </li>
      <li class="nav-item" role="presentation">
        <button
          class="nav-link"
          id="gaz-tab"
          data-bs-toggle="tab"
          data-bs-target="#gaz"
          type="button"
          role="tab"
          aria-controls="gaz"
          aria-selected="false">
          Gaz
        </button>
      </li>
      <li class="nav-item" role="presentation">
        <button
          class="nav-link"
          id="water-tab"
          data-bs-toggle="tab"
          data-bs-target="#water"
          type="button"
          role="tab"
          aria-controls="water"
          aria-selected="false">
          Eau
        </button>
      </li>
      <li class="nav-item" role="presentation">
        <button
          class="nav-link"
          id="submeters-tab"
          data-bs-toggle="tab"
          data-bs-target="#submeters"
          type="button"
          role="tab"
          aria-controls="submeters"
          aria-selected="false">
          Sous-compteurs
        </button>
      </li>
      <li class="nav-item" role="presentation">
        <button class="nav-link" id="presence-tab" data-bs-toggle="tab" 
          data-bs-target="#presence" type="button" role="tab">Présence</button>
      </li>
    </ul>

    <!-- Tab contents -->
    <div class="tab-content" id="updateTabContent">
      <!-- Onglet Linky -->
      <div
        class="tab-pane fade show active"
        id="linky"
        role="tabpanel"
        aria-labelledby="linky-tab">

        <div class='card mx-auto shadow-sm' >
          <div class="card-body"> 
            <form method='POST' action='saveConfigLinky'> 
              <div class='form-check'> 
                <h5>Appareil</h5> 
                {{selectDevices}} 
                <h5>Général</h5>
                <div class="mb-3">
                  <label for='shon'>Surface habitable (en m²)</label> 
                  <input class='form-control' id='shon' type='text' name='shon' value='{{shon}}'> 
                </div>
                <h5>Tarifs 
                  <button
                      id="toggleButtonLinkyTarif"
                      class="btn btn-link p-0 ms-auto"
                      type="button"
                      data-bs-toggle="collapse"
                      data-bs-target="#tarifLinky"
                      aria-expanded="true"
                      aria-controls="tarifLinky"
                      onClick="toggleDiv('tarifLinky');"
                    >
                      <span id="IcotarifLinky" ><svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" class="bi bi-plus-square" viewBox="0 0 16 16">
                        <path d="M14 1a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H2a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1zM2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2z"/>
                        <path d="M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4"/>
                      </svg></span>
                    </button>
                </h5>
                <div class="collapse" id="tarifLinky" style="display:none;">
                  <div class="mb-3">
                    <label for='tarifAbo'>Tarif abonnement (€ / mois)</label> 
                    <input class='form-control' id='tarifAbo' type='text' name='tarifAbo' value='{{tarifAbo}}'> 
                  </div>
                  <div class="mb-3">
                    <label for='tarifCSPE'>Accise sur l'Electricité (€ / kWh)</label> 
                    <input class='form-control' id='tarifCSPE' type='text' name='tarifCSPE' value='{{tarifCSPE}}'> 
                  </div>
                  <div class="mb-3">
                    <label for='tarifCTA'>Contribution Tarifaire d'Acheminement Electricité (CTA) (€ / mois)</label> 
                    <input class='form-control' id='tarifCTA' type='text' name='tarifCTA' value='{{tarifCTA}}'> 
                  </div>
                  <div class="mb-3">
                    <label for='tarifIdx2'>Tarif BASE/HC/EJPHN/BBRHCJB/EASF01 (€ / kWh)</label> 
                    <input class='form-control' id='tarifIdx2' type='text' name='tarifIdx2' value='{{tarifIdx2}}'> 
                  </div>
                  <div class="mb-3">
                    <label for='tarifIdx3'>Tarif HP/EJPHPM/BBRHPJB/EASF02 (€ / kWh)</label> 
                    <input class='form-control' id='tarifIdx3' type='text' name='tarifIdx3' value='{{tarifIdx3}}'> 
                  </div>
                  <div class="mb-3">
                    <label for='tarifIdx4'>Tarif BBRHCJW/EASF03  (€ / kWh)</label> 
                    <input class='form-control' id='tarifIdx4' type='text' name='tarifIdx4' value='{{tarifIdx4}}'> 
                  </div>
                  <div class="mb-3">
                    <label for='tarifIdx5'>Tarif BBRHPJW/EASF04 (€ / kWh)</label> 
                    <input class='form-control' id='tarifIdx5' type='text' name='tarifIdx5' value='{{tarifIdx5}}'> 
                  </div>
                  <div class="mb-3">
                    <label for='tarifIdx6'>Tarif BBRHCJR/EASF05 (€ / kWh)</label> 
                    <input class='form-control' id='tarifIdx6' type='text' name='tarifIdx6' value='{{tarifIdx6}}'> 
                  </div>
                  <div class="mb-3">
                    <label for='tarifIdx7'>Tarif BBRHPJR/EASF06  (€ / kWh)</label> 
                    <input class='form-control' id='tarifIdx7' type='text' name='tarifIdx7' value='{{tarifIdx7}}'> 
                  </div>
                  <div class="mb-3">
                    <label for='tarifIdx8'>Tarif EASF07 (€ / kWh)</label> 
                    <input class='form-control' id='tarifIdx8' type='text' name='tarifIdx8' value='{{tarifIdx8}}'> 
                  </div>
                  <div class="mb-3">
                    <label for='tarifIdx9'>Tarif EASF08 (€ / kWh)</label> 
                    <input class='form-control' id='tarifIdx9' type='text' name='tarifIdx9' value='{{tarifIdx9}}'> 
                  </div>
                  <div class="mb-3">
                    <label for='tarifIdx10'>Tarif EASF09 (€ / kWh)</label> 
                    <input class='form-control' id='tarifIdx10' type='text' name='tarifIdx10' value='{{tarifIdx10}}'> 
                  </div>
                </div>
                <h5>Délestage automatique
                  <button
                      id="toggleButtonLinkyDelestage"
                      class="btn btn-link p-0 ms-auto"
                      type="button"
                      data-bs-toggle="collapse"
                      data-bs-target="#delestage"
                      aria-expanded="true"
                      aria-controls="delestage"
                      onClick="toggleDiv('delestage');"
                    >
                      <span id="Icodelestage" ><svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" class="bi bi-plus-square" viewBox="0 0 16 16">
                        <path d="M14 1a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H2a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1zM2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2z"/>
                        <path d="M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4"/>
                      </svg></span>
                    </button>
                </h5>
                <div class="collapse" id="delestage" style="display:none;">
                  <h5>Prise(s) connectée(s) :</h5>
                  {{selectDevicesAction}} 
                </div>
              </div>
              <div class="d-flex justify-content-end">
                <button type="submit" class="btn btn-warning btn-lg">Enregistrer</button>
              </div>
            </form>
          </div> 
        </div>     
      </div>

      <!-- Onglet Production -->
      <div
        class="tab-pane fade"
        id="production"
        role="tabpanel"
        aria-labelledby="production-tab">
        
        <div class='card mx-auto shadow-sm' >
          <div class="card-body"> 
            <form method='POST' action='saveConfigProduction'> 
              <div class='form-check'> 
                <h5>Appareil</h5> 
                {{selectDevicesProd}} 
                <h5>Tarifs 
                  <button
                      id="toggleButtonProdTarif"
                      class="btn btn-link p-0 ms-auto"
                      type="button"
                      data-bs-toggle="collapse"
                      data-bs-target="#prodLinky"
                      aria-expanded="true"
                      aria-controls="prodLinky"
                      onClick="toggleDiv('prodLinky');"
                    >
                      <span id="IcoprodLinky" ><svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" class="bi bi-plus-square" viewBox="0 0 16 16">
                        <path d="M14 1a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H2a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1zM2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2z"/>
                        <path d="M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4"/>
                      </svg></span>
                    </button>
                </h5>
                <div class="collapse" id="prodLinky" style="display:none;"> 
                
                  <div class="mb-3">
                    <label for='tarifAboProd'>Tarif abonnement (€)</label> 
                    <input class='form-control' id='tarifAboProd' type='text' name='tarifAboProd' value='{{tarifAboProd}}'> 
                  </div>
                  
                  <div class="mb-3">
                    <label for='tarifIdxProd'>Tarif production (€)</label> 
                    <input class='form-control' id='tarifIdxProd' type='text' name='tarifIdxProd' value='{{tarifIdxProd}}'> 
                  </div>
                </div>

              </div>
              <div class="d-flex justify-content-end">
                <button type="submit" class="btn btn-warning btn-lg">Enregistrer</button>
              </div>
            </form>
          </div> 
        </div>
      </div>
      <!-- Onglet Gaz -->
      <div
        class="tab-pane fade"
        id="gaz"
        role="tabpanel"
        aria-labelledby="gaz-tab">
        
        <div class='card mx-auto shadow-sm' >
          <div class="card-body"> 
            <form method='POST' action='saveConfigGaz'> 
              <div class='form-check'> 
                <h5>Appareil</h5> 
                {{selectDevicesGaz}} 
                <h5>Parameters 
                  <button
                      id="toggleButtonGazParam"
                      class="btn btn-link p-0 ms-auto"
                      type="button"
                      data-bs-toggle="collapse"
                      data-bs-target="#gazLinky"
                      aria-expanded="true"
                      aria-controls="gazLinky"
                      onClick="toggleDiv('gazLinky');"
                    >
                      <span id="IcogazLinky" ><svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" class="bi bi-plus-square" viewBox="0 0 16 16">
                        <path d="M14 1a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H2a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1zM2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2z"/>
                        <path d="M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4"/>
                      </svg></span>
                    </button>
                </h5>
                <div class="collapse" id="gazLinky" style="display:none;">
                
                  <div class="mb-3">
                    <label for='coeffGaz'>Impulsion coefficient </label> 
                    <input class='form-control' id='coeffGaz' type='text' name='coeffGaz' value='{{coeffGaz}}'> 
                  </div>
                  <div class="mb-3">
                    <label for='unitGaz'>Unit </label> 
                    <input class='form-control' id='unitGaz' type='text' name='unitGaz' value='{{unitGaz}}'> 
                  </div>
                  <div class="mb-3">
                    <label for='tarifGaz'>Tarif (€)</label> 
                    <input class='form-control' id='tarifGaz' type='text' name='tarifGaz' value='{{tarifGaz}}'> 
                  </div>
                </div>
              </div>
              <div class="d-flex justify-content-end">
                <button type="submit" class="btn btn-warning btn-lg">Enregistrer</button>
              </div>
            </form>
          </div>
        </div>
      </div>
      <!-- Onglet Water -->
      <div
        class="tab-pane fade"
        id="water"
        role="tabpanel"
        aria-labelledby="water-tab">
        
        <div class='card mx-auto shadow-sm' >
          <div class="card-body"> 
            <form method='POST' action='saveConfigWater'> 
              <div class='form-check'> 
                <h5>Appareil</h5> 
                {{selectDevicesWater}} 
                <h5>Paramètres
                  <button
                      id="toggleButtonWaterParam"
                      class="btn btn-link p-0 ms-auto"
                      type="button"
                      data-bs-toggle="collapse"
                      data-bs-target="#waterLinky"
                      aria-expanded="true"
                      aria-controls="waterLinky"
                      onClick="toggleDiv('waterLinky');"
                    >
                      <span id="IcowaterLinky" ><svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" class="bi bi-plus-square" viewBox="0 0 16 16">
                        <path d="M14 1a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H2a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1zM2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2z"/>
                        <path d="M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4"/>
                      </svg></span>
                    </button>
                </h5>
                <div class="collapse" id="waterLinky" style="display:none;">
                
                  <div class="mb-3">
                    <label for='coeffWater'>Impulsion coefficient </label> 
                    <input class='form-control' id='coeffWater' type='text' name='coeffWater' value='{{coeffWater}}'> 
                  </div>
                  <div class="mb-3">
                    <label for='unitWater'>Unit </label> 
                    <input class='form-control' id='unitWater' type='text' name='unitWater' value='{{unitWater}}'> 
                  </div>
                  <div class="mb-3">
                    <label for='tarifWater'>Tarif (€)</label> 
                    <input class='form-control' id='tarifWater' type='text' name='tarifWater' value='{{tarifWater}}'> 
                  </div>
                </div>
              </div>
              <div class="d-flex justify-content-end">
                <button type="submit" class="btn btn-warning btn-lg">Enregistrer</button>
              </div>
            </form>
          </div> 
        </div>
      </div>
      <!-- Onglet Sous-compteurs -->
      <div
        class="tab-pane fade"
        id="submeters"
        role="tabpanel"
        aria-labelledby="submeters-tab">
        
        <div class='card mx-auto shadow-sm'>
          <div class="card-body"> 
            <h5>Sous-compteurs énergie</h5>
            <p class="text-muted small">
              Ajoutez des prises ou appareils Zigbee avec mesure de consommation pour détailler 
              la répartition de votre consommation électrique dans le dashboard.
            </p>
            
            <!-- Liste des sous-compteurs configurés -->
            <div id="subMetersList" class="mb-3">
              <div class="text-center py-3">
                <div class="spinner-border spinner-border-sm" role="status"></div>
                Chargement...
              </div>
            </div>
            
            <!-- Formulaire d'ajout -->
            <div class="card bg-light mt-3">
              <div class="card-body">
                <h6>Ajouter un sous-compteur</h6>
                <div class="mb-3">
                  <label for="subMeterDevice" class="form-label">Appareil</label>
                  <select id="subMeterDevice" class="form-select">
                    <option value="">-- Sélectionner --</option>
                  </select>
                  <div class="form-text">Seuls les appareils avec mesure d'énergie (Wh) sont affichés.</div>
                </div>
                <div class="mb-3">
                  <label for="subMeterAlias" class="form-label">Nom affiché</label>
                  <input type="text" id="subMeterAlias" class="form-control" placeholder="Ex: Salon, Cuisine, Bureau...">
                </div>
                <div class="mb-3">
                  <label for="subMeterColor" class="form-label">Couleur</label>
                  <input type="color" id="subMeterColor" class="form-control form-control-color" value="#3498db" title="Choisir une couleur">
                </div>
                <button type="button" class="btn btn-primary" onclick="addSubMeter()">
                  <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" class="bi bi-plus-lg" viewBox="0 0 16 16">
                    <path fill-rule="evenodd" d="M8 2a.5.5 0 0 1 .5.5v5h5a.5.5 0 0 1 0 1h-5v5a.5.5 0 0 1-1 0v-5h-5a.5.5 0 0 1 0-1h5v-5A.5.5 0 0 1 8 2"/>
                  </svg>
                  Ajouter
                </button>
              </div>
            </div>
            
          </div> 
        </div>
      </div>
      <!-- Onglet Présence -->
      <div class="tab-pane fade" id="presence" role="tabpanel">
        <div class='card mx-auto shadow-sm'>
          <div class="card-body"> 
            <form method='POST' action='saveConfigPresence'> 
              <h5>Capteur de présence</h5>
              {{selectDevicesPresence}}
              <div class='form-check mt-3'>
                <input id='enablePresenceGraph' type='checkbox' name='enablePresenceGraph' {{checkedEnablePresenceGraph}}>
                <label for='enablePresenceGraph'>Afficher sur le graphique</label>
              </div>
              <div class="d-flex justify-content-end mt-3">
                <button type="submit" class="btn btn-warning btn-lg">Enregistrer</button>
              </div>
            </form>
          </div> 
        </div>
      </div>
    </div>
  </div>
  <script>
  // Fonction pour remplacer virgule par point dans les champs numériques
    function setupDecimalInputs() {
      // Sélectionner tous les inputs qui doivent contenir des nombres
      const numericInputs = document.querySelectorAll(
        'input[name*="tarif"], input[name*="coeff"], input[name*="Threshold"], input[name*="unit"], input[name="shon"]'
      );
      
      numericInputs.forEach(input => {
        input.addEventListener('input', function(e) {
          // Remplacer virgule par point
          const cursorPos = this.selectionStart;
          const oldValue = this.value;
          const newValue = oldValue.replace(',', '.');
          
          if (oldValue !== newValue) {
            this.value = newValue;
            // Maintenir la position du curseur
            this.setSelectionRange(cursorPos, cursorPos);
          }
        });
      });
    }

    // ==========================================
    // SOUS-COMPTEURS
    // ==========================================
    
    // Charger au clic sur l'onglet
    document.getElementById('submeters-tab').addEventListener('shown.bs.tab', function() {
      loadSubMetersList();
      loadEligibleDevices();
    });
    
    // Charger la liste des sous-compteurs configurés
    function loadSubMetersList() {
      fetch('/getSubMeters')
        .then(response => response.json())
        .then(data => {
          let html = '';
          if (data.subMeters.length === 0) {
            html = '<p class="text-muted text-center py-3">Aucun sous-compteur configuré</p>';
          } else {
            html = '<div class="list-group">';
            data.subMeters.forEach(function(sm) {
              html += '<div class="list-group-item d-flex align-items-center">';
              html += '<span class="me-3" style="width:24px;height:24px;background:' + sm.color + ';border-radius:50%;flex-shrink:0;"></span>';
              html += '<div class="flex-grow-1">';
              html += '<strong>' + sm.alias + '</strong>';
              html += '<br><small class="text-muted">' + sm.IEEE + '</small>';
              html += '</div>';
              html += '<button class="btn btn-sm btn-outline-danger" onclick="deleteSubMeter(\'' + sm.IEEE + '\',\'' + sm.alias + '\')" title="Supprimer">';
              html += '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path d="M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z"/><path d="M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H6a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1h3.5a1 1 0 0 1 1 1zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z"/></svg>';
              html += '</button>';
              html += '</div>';
            });
            html += '</div>';
          }
          document.getElementById('subMetersList').innerHTML = html;
        })
        .catch(err => {
          document.getElementById('subMetersList').innerHTML = '<p class="text-danger">Erreur de chargement</p>';
        });
    }
    
    // Charger les devices éligibles
    function loadEligibleDevices() {
      fetch('/getEligibleSubMeters')
        .then(response => response.json())
        .then(data => {
          let select = document.getElementById('subMeterDevice');
          select.innerHTML = '<option value="">-- Sélectionner --</option>';
          data.forEach(function(dev) {
            let opt = document.createElement('option');
            opt.value = dev.IEEE;
            opt.textContent = (dev.alias || dev.model || dev.IEEE) + ' (' + dev.IEEE.substring(0,8) + '...)';
            select.appendChild(opt);
          });
        });
    }
    
    // Ajouter un sous-compteur
    function addSubMeter() {
      let IEEE = document.getElementById('subMeterDevice').value;
      let alias = document.getElementById('subMeterAlias').value.trim();
      let color = document.getElementById('subMeterColor').value;
      
      if (!IEEE) {
        alert('Veuillez sélectionner un appareil');
        return;
      }
      if (!alias) {
        alert('Veuillez entrer un nom');
        return;
      }
      
      let formData = new FormData();
      formData.append('IEEE', IEEE);
      formData.append('alias', alias);
      formData.append('color', color);
      formData.append('enabled', 'true');
      
      fetch('/setSubMeter', {
        method: 'POST',
        body: new URLSearchParams(formData)
      })
      .then(response => response.text())
      .then(result => {
        if (result === 'OK') {
          document.getElementById('subMeterAlias').value = '';
          document.getElementById('subMeterDevice').value = '';
          loadSubMetersList();
          loadEligibleDevices();
        } else {
          alert('Erreur: ' + result);
        }
      })
      .catch(err => alert('Erreur réseau'));
    }
    
    // Supprimer un sous-compteur
    function deleteSubMeter(IEEE, alias) {
      if (!confirm('Supprimer le sous-compteur "' + alias + '" ?')) return;
      
      let formData = new FormData();
      formData.append('IEEE', IEEE);
      
      fetch('/deleteSubMeter', {
        method: 'POST',
        body: new URLSearchParams(formData)
      })
      .then(response => response.text())
      .then(result => {
        if (result === 'OK') {
          loadSubMetersList();
          loadEligibleDevices();
        } else {
          alert('Erreur: ' + result);
        }
      })
      .catch(err => alert('Erreur réseau'));
    }
    
    // Exécuter au chargement de la page
    document.addEventListener('DOMContentLoaded', setupDecimalInputs);
  </script>


)rawstring";

const char HTTP_CONFIG_NOTIFICATIONS[] PROGMEM = R"rawstring(
  <div class="container py-5">
    <h4 class="mb-4">Config Notifications</h4>
    <form method='POST' action='saveConfigNotifications'>
    <ul class="nav nav-tabs" id="notifTab" role="tablist">
      <li class="nav-item" role="presentation">
        <button class="nav-link active" id="conso-tab" data-bs-toggle="tab" data-bs-target="#conso" type="button" role="tab" aria-controls="conso" aria-selected="true">Consommation</button>
      </li>
      <li class="nav-item" role="presentation">
        <button class="nav-link" id="prod-tab" data-bs-toggle="tab" data-bs-target="#prod" type="button" role="tab" aria-controls="prod" aria-selected="false">Production</button>
      </li>
      <li class="nav-item" role="presentation">
        <button class="nav-link" id="eauNotif-tab" data-bs-toggle="tab" data-bs-target="#eauNotif" type="button" role="tab" aria-controls="eauNotif" aria-selected="false">Eau</button>
      </li>
      <li class="nav-item" role="presentation">
        <button class="nav-link" id="gazNotif-tab" data-bs-toggle="tab" data-bs-target="#gazNotif" type="button" role="tab" aria-controls="gazNotif" aria-selected="false">Gaz</button>
      </li>
      <li class="nav-item" role="presentation">
        <button class="nav-link" id="autres-tab" data-bs-toggle="tab" data-bs-target="#autres" type="button" role="tab" aria-controls="autres" aria-selected="false">Autres</button>
      </li>
    </ul>

    <div class="tab-content" id="notifTabContent">

      <!-- Onglet Consommation -->
      <div class="tab-pane fade show active" id="conso" role="tabpanel" aria-labelledby="conso-tab">
        <div class='card mx-auto shadow-sm'>
          <div class="card-body">
            <h5>Alertes</h5>
            <div class='form-check'>
              <input class='' id='NotifSubscribedPower' type='checkbox' name='NotifSubscribedPower' {{checkedNotifSubscribedPower}}>
              <label class='' for='NotifSubscribedPower'> D&eacute;passement de puissance souscrite</label>
            </div>
            <div class='form-check'>
              <input class='' id='NotifPowerOutage' type='checkbox' name='NotifPowerOutage' {{checkedNotifPowerOutage}}>
              <label class='' for='NotifPowerOutage'> Puissance nulle</label>
            </div>
            <div class='form-check'>
              <input class='' id='NotifRedColor' type='checkbox' name='NotifRedColor' {{checkedNotifRedColor}}>
              <label class='' for='NotifRedColor'> Jour rouge (Tempo)</label>
            </div>
            <div class='form-check'>
              Sur-tension
              <div class="input-group mb-3">
                <div class="input-group-text">
                  <input class="mt-0" type="checkbox" name='NotifOverVoltage' {{checkedNotifOverVoltage}}>
                </div>
                <input type="text" class="form-control" name='NotifOverVoltageThreshold' value='{{valOverVoltageThreshold}}'>
                <span class="input-group-text"> V</span>
              </div>
            </div>
            <div class='form-check'>
              Sous-tension
              <div class="input-group mb-3">
                <div class="input-group-text">
                  <input class="mt-0" type="checkbox" name='NotifUnderVoltage' {{checkedNotifUnderVoltage}}>
                </div>
                <input type="text" class="form-control" name='NotifUnderVoltageThreshold' value='{{valUnderVoltageThreshold}}'>
                <span class="input-group-text"> V</span>
              </div>
            </div>
            <div class='form-check'>
              Surpuissance soutenue
              <div class="input-group mb-2">
                <div class="input-group-text">
                  <input class="mt-0" type="checkbox" name='NotifOverPower' {{checkedNotifOverPower}}>
                </div>
                <input type="text" class="form-control" name='NotifOverPowerThreshold' value='{{valOverPowerThreshold}}' placeholder='6000'>
                <span class="input-group-text">W</span>
              </div>
              <div class="input-group mb-2">
                <span class="input-group-text">Dur&eacute;e</span>
                <input type="text" class="form-control" name='NotifOverPowerDuration' value='{{valOverPowerDuration}}' placeholder='60'>
                <span class="input-group-text">s</span>
              </div>
              <div class="input-group mb-3">
                <span class="input-group-text">Cooldown</span>
                <input type="text" class="form-control" name='NotifOverPowerCooldown' value='{{valOverPowerCooldown}}' placeholder='30'>
                <span class="input-group-text">min</span>
              </div>
            </div>
            <div class='form-check'>
              Consommation journali&egrave;re anormale
              <div class="input-group mb-3">
                <div class="input-group-text">
                  <input class="mt-0" type="checkbox" name='NotifDailyAnomaly' {{checkedNotifDailyAnomaly}}>
                </div>
                <input type="text" class="form-control" name='NotifDailyAnomalyPercent' value='{{valDailyAnomalyPercent}}' placeholder='30'>
                <span class="input-group-text">% au-dessus moy. 7j</span>
              </div>
            </div>
            <h5>Infos</h5>
            <div class='form-check'>
              <input class='' id='NotifPriceChange' type='checkbox' name='NotifPriceChange' {{checkedNotifPriceChange}}>
              <label class='' for='NotifPriceChange'> Changement de tarif</label>
            </div>
            <div class='form-check'>
              <input class='' id='NotifPEJP' type='checkbox' name='NotifPEJP' {{checkedNotifPEJP}}>
              <label class='' for='NotifPEJP'> Pr&eacute;avis EJP</label>
            </div>
            <div class='form-check'>
              <input class='' id='NotifColorTomorrow' type='checkbox' name='NotifColorTomorrow' {{checkedNotifColorTomorrow}}>
              <label class='' for='NotifColorTomorrow'> Couleur du lendemain (Tempo)</label>
            </div>
            <div class='form-check'>
              D&eacute;passement de budget
              <div class="input-group mb-3">
                <div class="input-group-text">
                  <input class="mt-0" type="checkbox" name='NotifOverBudget' {{checkedNotifOverBudget}}>
                </div>
                <input type="text" class="form-control" name='NotifOverBudgetThreshold' value='{{valOverBudgetThreshold}}'>
                <span class="input-group-text"> &euro; </span>
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- Onglet Production -->
      <div class="tab-pane fade" id="prod" role="tabpanel" aria-labelledby="prod-tab">
        <div class='card mx-auto shadow-sm'>
          <div class="card-body">
            <h5>Alertes</h5>
            <div class='form-check'>
              <input class='' id='NotifProdZero' type='checkbox' name='NotifProdZero' {{checkedNotifProdZero}}>
              <label class='' for='NotifProdZero'>Production = 0</label>
            </div>
            <h5>Infos</h5>
            <div class='form-check'>
              <input class='' id='NotifProdSupConso' type='checkbox' name='NotifProdSupConso' {{checkedNotifProdSupConso}}>
              <label class='' for='NotifProdSupConso'>Production > Consommation</label>
            </div>
          </div>
        </div>
      </div>

      <!-- Onglet Eau -->
      <div class="tab-pane fade" id="eauNotif" role="tabpanel" aria-labelledby="eauNotif-tab">
        <div class='card mx-auto shadow-sm'>
          <div class="card-body">
            <h5>Fuite d'eau (journali&egrave;re)</h5>
            <div class='form-check'>
              Consommation totale du jour
              <div class="input-group mb-3">
                <div class="input-group-text">
                  <input class="mt-0" type="checkbox" name='NotifWaterLeak' {{checkedNotifWaterLeak}}>
                </div>
                <input type="text" class="form-control" name='NotifWaterLeakThreshold' value='{{valWaterLeakThreshold}}' placeholder='5'>
                <span class="input-group-text">L</span>
              </div>
            </div>
            <h5>Fuite d'eau (nocturne)</h5>
            <div class='form-check'>
              Consommation entre 00h et 05h
              <div class="input-group mb-3">
                <div class="input-group-text">
                  <input class="mt-0" type="checkbox" name='NotifNightWaterLeak' {{checkedNotifNightWaterLeak}}>
                </div>
                <input type="text" class="form-control" name='NotifNightWaterLeakThreshold' value='{{valNightWaterLeakThreshold}}' placeholder='1'>
                <span class="input-group-text">L</span>
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- Onglet Gaz -->
      <div class="tab-pane fade" id="gazNotif" role="tabpanel" aria-labelledby="gazNotif-tab">
        <div class='card mx-auto shadow-sm'>
          <div class="card-body">
            <p class="text-muted">Aucune notification configur&eacute;e pour le gaz.</p>
          </div>
        </div>
      </div>

      <!-- Onglet Autres -->
      <div class="tab-pane fade" id="autres" role="tabpanel" aria-labelledby="autres-tab">
        <div class='card mx-auto shadow-sm'>
          <div class="card-body">
            <h5>M&eacute;triques quotidiennes</h5>
            <div class='form-check mb-3'>
              <input class='' id='NotifDailyMetrics' type='checkbox' name='NotifDailyMetrics' {{checkedNotifDailyMetrics}}>
              <label class='' for='NotifDailyMetrics'> Envoyer un r&eacute;sum&eacute; quotidien</label>
            </div>
          </div>
        </div>
      </div>

    </div>
    <div class="d-flex justify-content-end mt-3">
      <button type="submit" class="btn btn-warning btn-lg">Enregistrer</button>
    </div>
    </form>
  </div>
)rawstring";

const char HTTP_UPDATE[] PROGMEM = R"(
    <div class="container py-5">
    <h4 class="mb-4">Mise à jour firmware</h4>

    <!-- Nav tabs -->
    <ul class="nav nav-tabs" id="updateTab" role="tablist">
      <li class="nav-item" role="presentation">
        <button
          class="nav-link active"
          id="auto-tab"
          data-bs-toggle="tab"
          data-bs-target="#auto"
          type="button"
          role="tab"
          aria-controls="auto"
          aria-selected="true">
          Automatique
        </button>
      </li>
      <li class="nav-item" role="presentation">
        <button
          class="nav-link"
          id="manual-tab"
          data-bs-toggle="tab"
          data-bs-target="#manual"
          type="button"
          role="tab"
          aria-controls="manual"
          aria-selected="false">
          Manuelle
        </button>
      </li>
    </ul>

    <!-- Tab contents -->
    <div class="tab-content" id="updateTabContent">
      <!-- Onglet Automatique -->
      <div
        class="tab-pane fade show active"
        id="auto"
        role="tabpanel"
        aria-labelledby="auto-tab">

        <div align="center">
          <div id="update_info" class="card p-4">
            <h5>Dernière version disponible sur Github</h5>
            <div id="onlineupdate" style="text-align:left">
              <h6 id="releasehead"></h6>
              <br>
              <pre id="releasebody">En attente d'informations de GitHub...</pre>
            </div>
            <div id="autoBtn">
              <button id="btnUpdate" style="width:100%" class="btn btn-primary mb-3">
                Mettre à jour
              </button>
              <div id="statusDL" class="text-muted">Prêt</div>

              <div class="progress" style="height:1.5rem">
                <div
                  id="barDL"
                  class="progress-bar"
                  role="progressbar"
                  style="width:0%"
                  aria-valuemin="0"
                  aria-valuemax="100">
                  0%
                </div>
              </div>
              <!-- GESTIONNAIRE DE FICHIERS + LOG - desactive temporairement
              <div style="margin-top:10px">
                <a href="/filesManager" class="btn btn-sm btn-outline-info">Gestionnaire de fichiers</a>
              </div>
              <div id="logSection" style="display:none;margin-top:15px;text-align:left">
                <button class="btn btn-sm btn-outline-secondary mb-2" onclick="document.getElementById('updateLogPre').select();document.execCommand('copy');">
                  Copier le log
                </button>
                <textarea id="updateLogPre" readonly rows="12"
                  style="width:100%;font-family:monospace;font-size:11px;background:#1e1e1e;color:#d4d4d4;padding:8px;border-radius:4px;resize:vertical;white-space:pre;overflow-x:auto"></textarea>
              </div>
              -->
            </div>
          </div>
        </div>

      </div>

      <!-- Onglet Manuel -->
      <div
        class="tab-pane fade"
        id="manual"
        role="tabpanel"
        aria-labelledby="manual-tab">
        <div class="card p-4">
        <div align='center'>
        <form id="frm">
          <div class="mb-3">
            <label for="f" class="form-label">Sélectionner le Fichier de mise à jour</label>
            <input
              class="form-control"
              type="file"
              id="f"
              name="archive"
              accept=".tar,.tar.gz,.gz,.bin">
          </div>
          <button id="btnUpdateMan" type="submit" style="width:100%" class="btn btn-primary mb-3">Mettre à jour</button>
        </form>
        
        <div id="status" class="text-muted">Prêt</div>
        <div class="progress" style="height:1.5rem">
          <div
            id="barP"
            class="progress-bar"
            role="progressbar"
            aria-valuemin="0"
            aria-valuemax="100"
            style="width:0%">
            0%
          </div>
        </div>
        </div>
        
        </div>
      </div>
    </div>
  </div>
    
  <script>
      function getReleaseInfo() {
        $.getJSON("https://api.github.com/repos/fairecasoimeme/LiXee-Gateway/releases/latest").done(function(release) {
            var asset = release.assets[0];
            var downloadCount = 0;
            for (var i = 0; i < release.assets.length; i++) {
            downloadCount += release.assets[i].download_count;
          }
          var oneHour = 60 * 60 * 1000;
          var oneDay = 24 * oneHour;
          var dateDiff = new Date() - new Date(release.published_at);
          var timeAgo;
          if (dateDiff < oneDay) {
            timeAgo = (dateDiff / oneHour).toFixed(1) + " hours ago";
          } else {
            timeAgo = (dateDiff / oneDay).toFixed(1) + " days ago";
          }

          var releaseInfo = release.name; //+ " was updated " + timeAgo + " and downloaded " + downloadCount.toLocaleString() + " times.";

          var version = release.tag_name;
          if (version == "{{version}}")
          {
            $("#autoBtn").text("Pas de mise à jour nécessaire");
          }else{
            $("#autoBtn").show();
            
          }
          $("#downloadupdate").attr("href", asset.browser_download_url);
          $("#releasehead").text(releaseInfo);
          $("#releasebody").text(release.body);
          $("#releaseinfo").fadeIn("slow");
        });
      }

      let updatePollingInterval = null;
      let isUpdating = false;
      
      function pollUpdateStatusAuto() {
        $.ajax({
          url: '/getUpdateStatusAuto',
          type: 'GET',
          dataType: 'json',
          success: function(data) {
            const stdl = document.getElementById('statusDL');
            const bardl = document.getElementById('barDL');

            if (data.status && data.status !== '') {
              stdl.textContent = data.status;
            }

            if (data.progress >= 0) {
              const pct = parseInt(data.progress);
              bardl.style.width = pct + '%';
              bardl.textContent = pct + '%';
              /* LOG desactive temporairement
              document.getElementById('logSection').style.display = 'block';
              pollUpdateLog();
              */
            }

            if (data.reboot) {
              /* pollUpdateLog(); */
              setTimeout(function() { location.reload(); }, 3000);
            }
          }
        });
      }

      /* LOG desactive temporairement
      function pollUpdateLog() {
        $.ajax({
          url: '/getUpdateLog',
          type: 'GET',
          dataType: 'text',
          success: function(data) {
            var el = document.getElementById('updateLogPre');
            if (el && data) {
              el.value = data;
              el.scrollTop = el.scrollHeight;
            }
          }
        });
      }
      */
      
      function pollUpdateStatusManuel() {
        $.ajax({
          url: '/getUpdateStatusManuel',
          type: 'GET',
          dataType: 'json',
          success: function(data) {
            const st = document.getElementById('status');
            const bar = document.getElementById('barP');
            
            if (data.status && data.status !== '') {
              st.textContent = data.status;
            }
            
            // ← AJOUTER : Mettre à jour la barre avec le progress du serveur
            if (data.progress >= 0) {
              const pct = parseInt(data.progress);
              bar.style.width = pct + '%';
              bar.textContent = pct + '%';
            }
            
            if (data.reboot) {
              setTimeout(function() { location.reload(); }, 3000);
            }
          }
        });
      }
      
      function startUpdatePolling() {
        if (updatePollingInterval) return;
        isUpdating = true;
        
        pollUpdateStatusAuto();
        pollUpdateStatusManuel();
        
        updatePollingInterval = setInterval(function() {
          pollUpdateStatusAuto();
          pollUpdateStatusManuel();
        }, 2000); // Toutes les 2 secondes
      }
      
      function stopUpdatePolling() {
        if (updatePollingInterval) {
          clearInterval(updatePollingInterval);
          updatePollingInterval = null;
        }
        isUpdating = false;
      }

      getReleaseInfo();
      const btn = document.getElementById('btnUpdate'),
            stdl  = document.getElementById('statusDL');
            bardl  = document.getElementById('barDL');
      
      btn.addEventListener('click', () => {
        stdl.textContent = 'Démarrage…';
        startUpdatePolling(); 
        fetch('/downloadUpdate', { method: 'POST' })
          .then(resp => {
            if (resp.ok) {
              stdl.textContent = 'Démarrage. merci d\'attendre ...';
              //btn.disabled = true;
              btn.style.display = 'none';
            } else {
              stdl.textContent = 'Erreur: ' + resp.status;
              stopUpdatePolling();
            }
          })
          .then(text => {
            stdl.textContent = text;  // par exemple "Mise à jour programmée"
          })
          .catch(err => {
            stdl.textContent = 'Erreur réseau';
            stopUpdatePolling(); 
          });
      });

      const frm = document.getElementById('frm'),
            f   = document.getElementById('f'),
            bar = document.getElementById('barP'),
            st  = document.getElementById('status');

      function isTunnel() {
        return window.location.hostname.indexOf('lixee-box.fr') >= 0;
      }

      function blobToBase64(blob) {
        return new Promise(function(resolve, reject) {
          var reader = new FileReader();
          reader.onloadend = function() {
            resolve(reader.result.split(',')[1]);
          };
          reader.onerror = reject;
          reader.readAsDataURL(blob);
        });
      }

      async function uploadChunked(file) {
        var CHUNK = 8192;
        var total = file.size;
        var chunks = Math.ceil(total / CHUNK);

        st.textContent = 'Initialisation...';
        bar.style.width = '0%';
        bar.textContent = '0%';

        var resp = await fetch('/restoreInit', {
          method: 'POST',
          headers: {'Content-Type': 'application/json'},
          body: JSON.stringify({totalSize: total})
        });
        if (!resp.ok) { st.textContent = 'Erreur init: ' + resp.status; return; }

        isUpdating = true;
        updatePollingInterval = setInterval(pollUpdateStatusManuel, 2000);

        for (var i = 0; i < chunks; i++) {
          var start = i * CHUNK;
          var end = Math.min(start + CHUNK, total);
          var b64 = await blobToBase64(file.slice(start, end));

          resp = await fetch('/restoreChunk?n=' + i, {
            method: 'POST',
            headers: {'Content-Type': 'text/plain'},
            body: b64
          });
          if (!resp.ok) {
            st.textContent = 'Erreur chunk ' + i + ': ' + resp.status;
            stopUpdatePolling();
            return;
          }

          var pct = Math.round(((i + 1) / chunks) * 50);
          bar.style.width = pct + '%';
          bar.textContent = pct + '%';
          st.textContent = 'Téléchargement ' + (i+1) + '/' + chunks;
        }

        st.textContent = 'Installation...';
        bar.style.width = '55%';
        bar.textContent = '55%';

        resp = await fetch('/restoreFinish', {method: 'POST'});
        if (!resp.ok) {
          st.textContent = 'Erreur finish: ' + resp.status;
          stopUpdatePolling();
          return;
        }

        st.textContent = 'Redémarrage...';
        bar.style.width = '100%';
        bar.textContent = '100%';
        setTimeout(function() { window.location.href = '/'; }, 5000);
      }

      async function uploadChunkedFirmware(file) {
        var CHUNK = 8192;
        var total = file.size;
        var chunks = Math.ceil(total / CHUNK);

        st.textContent = 'Initialisation firmware...';
        bar.style.width = '0%';
        bar.textContent = '0%';

        var resp = await fetch('/fwUpdateInit', {
          method: 'POST',
          headers: {'Content-Type': 'application/json'},
          body: JSON.stringify({totalSize: total, filename: file.name})
        });
        if (!resp.ok) { st.textContent = 'Erreur init: ' + resp.status; return; }

        isUpdating = true;
        updatePollingInterval = setInterval(pollUpdateStatusManuel, 2000);

        for (var i = 0; i < chunks; i++) {
          var start = i * CHUNK;
          var end = Math.min(start + CHUNK, total);
          var b64 = await blobToBase64(file.slice(start, end));

          resp = await fetch('/fwUpdateChunk?n=' + i, {
            method: 'POST',
            headers: {'Content-Type': 'text/plain'},
            body: b64
          });
          if (!resp.ok) {
            st.textContent = 'Erreur chunk ' + i + ': ' + resp.status;
            stopUpdatePolling();
            return;
          }

          var pct = Math.round(((i + 1) / chunks) * 90);
          bar.style.width = pct + '%';
          bar.textContent = pct + '%';
          st.textContent = 'Flash firmware ' + (i+1) + '/' + chunks;
        }

        st.textContent = 'Finalisation...';
        bar.style.width = '95%';
        bar.textContent = '95%';

        resp = await fetch('/fwUpdateFinish', {method: 'POST'});
        if (!resp.ok) {
          st.textContent = 'Erreur finish: ' + resp.status;
          stopUpdatePolling();
          return;
        }

        st.textContent = 'Redémarrage...';
        bar.style.width = '100%';
        bar.textContent = '100%';
        setTimeout(function() { window.location.href = '/'; }, 10000);
      }

      function isBinFile(name) {
        return name.toLowerCase().endsWith('.bin');
      }

      function isGzFile(name) {
        var n = name.toLowerCase();
        return n.endsWith('.gz') || n.endsWith('.tgz');
      }

      async function decompressGzip(file) {
        st.textContent = 'Décompression gzip...';
        var ds = new DecompressionStream('gzip');
        var decompressed = file.stream().pipeThrough(ds);
        var blob = await new Response(decompressed).blob();
        var tarName = file.name.replace(/\.gz$/i, '').replace(/\.tgz$/i, '.tar');
        if (!tarName.endsWith('.tar')) tarName += '.tar';
        return new File([blob], tarName, {type: 'application/x-tar'});
      }

      async function handleSubmit(file) {
        $("#btnUpdateMan").hide();

        // Décompresser les .tar.gz/.gz côté navigateur avant envoi
        if (isGzFile(file.name)) {
          try {
            file = await decompressGzip(file);
          } catch(e) {
            st.textContent = 'Erreur décompression: ' + e.message;
            return;
          }
        }

        if (isTunnel()) {
          if (isBinFile(file.name)) {
            uploadChunkedFirmware(file);
          } else {
            uploadChunked(file);
          }
          return;
        }

        // --- Upload direct (réseau local) ---
        if (isBinFile(file.name)) {
          // Direct firmware .bin upload
          bar.style.width = '0%';
          bar.textContent = '0%';

          isUpdating = true;
          pollUpdateStatusManuel();
          updatePollingInterval = setInterval(pollUpdateStatusManuel, 2000);

          const xhr = new XMLHttpRequest();
          xhr.open('POST','/doUpdate');
          xhr.upload.onprogress = function(ev) {
            if (ev.lengthComputable) {
              var pct = Math.round((ev.loaded / ev.total) * 100);
              bar.style.width = pct + '%';
              bar.textContent = pct + '%';
            }
          };
          xhr.onload = () => {
            if (xhr.status === 200 || xhr.status === 302) {
              st.textContent = 'Redémarrage...';
              setTimeout(() => { window.location.href='/'; }, 10000);
            } else {
              st.textContent = 'Erreur: ' + xhr.status;
              stopUpdatePolling();
            }
          };
          const fd = new FormData();
          fd.append('firmware', file, file.name);
          xhr.send(fd);
        } else {
          // .tar restore upload
          if (updatePollingInterval) {
            clearInterval(updatePollingInterval);
            updatePollingInterval = null;
          }

          isUpdating = true;
          pollUpdateStatusManuel();
          updatePollingInterval = setInterval(function() {
            pollUpdateStatusManuel();
          }, 500);

          const xhr = new XMLHttpRequest();
          xhr.open('POST','/doRestore');

          xhr.onload = () => {
            if (xhr.status === 200) {
              setTimeout(() => {
                  window.location.href='/';
              }, 3000);
            } else {
                st.textContent = 'Erreur: ' + xhr.status;
                if (updatePollingInterval) {
                  clearInterval(updatePollingInterval);
                }
                updatePollingInterval = setInterval(function() {
                  pollUpdateStatusManuel();
                }, 2000);
            }
          };
          const fd = new FormData();
          fd.append('archive', file, file.name);
          xhr.send(fd);
          bar.style.width = '0%';
          bar.textContent = '0%';
        }
      }

      frm.addEventListener('submit', e => {
        e.preventDefault();
        const file = f.files[0];
        if (!file) return alert('Choisissez un fichier');
        handleSubmit(file);
      });


    </script>)";

const char HTTP_CONFIG_MENU_ZIGBEE[] PROGMEM =
    "<a href='/configDevices' style='width:100px;height:64px;' class='btn btn-primary mb-1 {{menu_config_devices}}' >"
    "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px;' width='16' height='16' fill='currentColor' class='bi bi-app-indicator' viewBox='0 0 16 16'>"
      "<path d='M5.5 2A3.5 3.5 0 0 0 2 5.5v5A3.5 3.5 0 0 0 5.5 14h5a3.5 3.5 0 0 0 3.5-3.5V8a.5.5 0 0 1 1 0v2.5a4.5 4.5 0 0 1-4.5 4.5h-5A4.5 4.5 0 0 1 1 10.5v-5A4.5 4.5 0 0 1 5.5 1H8a.5.5 0 0 1 0 1z'/>"
      "<path d='M16 3a3 3 0 1 1-6 0 3 3 0 0 1 6 0'/>"
    "</svg><br>"
    " Appareils"
    "</a>&nbsp"
    "<a href='/configZigbee' style='width:100px;height:64px;' class='btn btn-primary mb-1 {{menu_config_zigbee}}' >"
    "<svg xmlns='http://www.w3.org/2000/svg' style='width:16px;' width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' class='feather feather-settings'>"
      "<circle cx='12' cy='12' r='3'></circle>"
      "<path d='M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z'></path>"
    "</svg><br>"
    " Config"
    "</a>&nbsp"
    ;

// Pendant LoRa de HTTP_CONFIG_MENU_ZIGBEE. Une seule entree pour l'instant (le LoRa n'a pas
// de page de config radio), mais on garde la meme colonne pour que les deux pages
// s'alignent visuellement.
const char HTTP_CONFIG_MENU_LORA[] PROGMEM =
    "<a href='/configLora' style='width:100px;height:64px;' class='btn btn-primary mb-1 {{menu_config_lora}}' >"
    SVG_LORA_ICON "<br>"
    " Appareils"
    "</a>&nbsp"
    ;

const char HTTP_CONFIG_DEVICES_ZIGBEE[] PROGMEM =

    "<div class='row p-4 justify-content-md-center' >"
      "<div class='col-sm-2'>"
        "<div class='btn-group-horizontal'>"
          "{{menu_config_zigbee}}"
        "</div>"
      "</div>"
      "<div class='col-sm-10'>"
        "<h4>Config appareils Zigbee</h4>"
        "<div class='d-flex justify-content-end'>"
          "<a class='btn btn-primary mb-1' href='/assistDevice' style='width:120px;height:64px;'>"
          "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' class='bi bi-plus-circle' viewBox='0 0 16 16'>"
            "<path d='M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16'/>"
            "<path d='M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4'/>"
          "</svg><br>"
          " Ajouter"
          "</a> "
        "</div><br>"
        "<h5>Liste des appareils</h5>"
        "<div class='row g-4' style='font-size:12px;'>"
          "<script>"
              "function OTAUpdateBar(id){"
                "$.ajax({"
                  "url: '/OTAUpdateBar?id='+id,"
                  "type: 'GET',"
                  "success:function(data) {"
                  "if (data>=0){"
                    "$('#uploadOTA'+id).html('<div align=\"center\">En cours ...</div><progress value=\"'+data+'\" max=\"100\" style=\"width:100%\">'+data+'%</progress>');"
                    "$('#uploadOTA'+id).show();"
                  "}else{"
                  "$('#uploadOTA'+id).hide();"
                  "}"
                  "setTimeout(function(){OTAUpdateBar(id); }, 5000);" 
          
                "}"
            "});"
          "}"
          "</script>"
          "{{devicesList}}"
        "</div>"
      "</div>"
      
;

const char HTTP_CONFIG_GENERAL[] PROGMEM = R"(
  
<div class="container py-5">
  <h4 class="mb-4">Config Générale</h4>

  <!-- Nav tabs -->
  <ul class="nav nav-tabs" id="generalTab" role="tablist">
    <li class="nav-item" role="presentation">
      <button
        class="nav-link active"
        id="param-tab"
        data-bs-toggle="tab"
        data-bs-target="#param"
        type="button"
        role="tab"
        aria-controls="param"
        aria-selected="true">
        Paramètres
      </button>
    </li>
    <li class="nav-item" role="presentation">
      <button
        class="nav-link"
        id="debug-tab"
        data-bs-toggle="tab"
        data-bs-target="#debug"
        type="button"
        role="tab"
        aria-controls="debug"
        aria-selected="false">
        Debug
      </button>
    </li>
  </ul>

  <!-- Tab contents -->
  <div class="tab-content" id="updateTabContent">
    <!-- Onglet Notification -->
    <div
      class="tab-pane fade show active"
      id="param"
      role="tabpanel"
      aria-labelledby="param-tab">

      <div class='card mx-auto shadow-sm' >
        <div class="card-body"> 
          <form method='POST' action='saveConfigParameter'> 
            <h5>Développeur</h5>
            <div class='form-check'>
              <input class='form-check-input' id='developerMode' type='checkbox' name='developerMode' {{checkeddeveloperMode}}>
              <label class='form-check-label' for='developerMode'>Mode développeur</label>
            </div>

            <div class="d-flex justify-content-end">
              <button type="submit" class="btn btn-warning btn-lg">Sauvegarder</button>
            </div>
          </form>
        </div> 
      </div>
      
    </div>

    <!-- Onglet General -->
    <div
      class="tab-pane fade"
      id="debug"
      role="tabpanel"
      aria-labelledby="debug-tab">
      
      <div class='card mx-auto shadow-sm' >
        <div class="card-body"> 
          <form method='POST' action='saveConfigGeneral'> 
            <div class='form-check'>
              <input class='form-check-input' id='debugSerial' type='checkbox' name='debugSerial' {{checkedDebug}}>
              <label class='form-check-label' for='debugSerial'>Debug</label>
            </div>
            <div class="d-flex justify-content-end">
              <button type="submit" class="btn btn-warning btn-lg">Enregistrer</button>
            </div>
          </form>
        </div> 
      </div>
    </div>
  </div>
</div>
)";


const char HTTP_CONFIG_ZIGBEE[] PROGMEM =  
    "<div class='row p-4 justify-content-md-center' >"
      "<div class='col-sm-2'>"
        "<div class='btn-group-horizontal'>"
          "{{menu_config_zigbee}}"
        "</div>"
      "</div>"
    "<div class='col-sm-10'>"
      "<h4>Config Zigbee</h4>"
      "<div align='right'>"
      
      "<button type='button' onclick='cmd(\"Network\");' class='btn btn-primary'>"
        "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='#FFFFFF' class='bi bi-bootstrap-reboot' viewBox='0 0 16 16'>"
          "<path d='M1.161 8a6.84 6.84 0 1 0 6.842-6.84.58.58 0 1 1 0-1.16 8 8 0 1 1-6.556 3.412l-.663-.577a.58.58 0 0 1 .227-.997l2.52-.69a.58.58 0 0 1 .728.633l-.332 2.592a.58.58 0 0 1-.956.364l-.643-.56A6.8 6.8 0 0 0 1.16 8z'/>"
          "<path d='M6.641 11.671V8.843h1.57l1.498 2.828h1.314L9.377 8.665c.897-.3 1.427-1.106 1.427-2.1 0-1.37-.943-2.246-2.456-2.246H5.5v7.352zm0-3.75V5.277h1.57c.881 0 1.416.499 1.416 1.32 0 .84-.504 1.324-1.386 1.324z'/>"
        "</svg>"
        " Réseau"
        "</button> "
        "<button type='button' onclick='cmd(\"Reset\");' class='btn btn-primary'>"
        "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='#FFFFFF' class='bi bi-bootstrap-reboot' viewBox='0 0 16 16'>"
          "<path d='M1.161 8a6.84 6.84 0 1 0 6.842-6.84.58.58 0 1 1 0-1.16 8 8 0 1 1-6.556 3.412l-.663-.577a.58.58 0 0 1 .227-.997l2.52-.69a.58.58 0 0 1 .728.633l-.332 2.592a.58.58 0 0 1-.956.364l-.643-.56A6.8 6.8 0 0 0 1.16 8z'/>"
          "<path d='M6.641 11.671V8.843h1.57l1.498 2.828h1.314L9.377 8.665c.897-.3 1.427-1.106 1.427-2.1 0-1.37-.943-2.246-2.456-2.246H5.5v7.352zm0-3.75V5.277h1.57c.881 0 1.416.499 1.416 1.32 0 .84-.504 1.324-1.386 1.324z'/>"
        "</svg>"
        " Redémarrer"
        "</button> "
        "<button type='button' onClick=\"if (confirm('Are you sure ?')==true){cmd('ErasePDM');}else{return false;};\" class='btn btn-danger'>"
        "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='currentColor' class='bi bi-trash' viewBox='0 0 16 16'>"
          "<path d='M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z'/>"
          "<path d='M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H6a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1h3.5a1 1 0 0 1 1 1zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z'/>"
        "</svg>"
        " RAZ"
        "</button> "
      "</div>"
      "<h5 class='card-title mb-4'>Paramètres</h5>"
      "<div class='card mx-auto shadow-sm' >"
        "<div class='card-body'>"
          "<div class='mb-3'>"
            "<span> @MAC coordinator : </span>{{macCoordinator}}<br>"
            "<span> Version coordinator : </span>{{versionCoordinator}}<br>"
            "<span> Réseau : </span>{{networkCoordinator}}<br>"
            "<label for='SetLed'>Set Led : </label><br>"
            "<button type='button' onclick='cmd(\"SetLed\",1);' class='btn btn-primary'>LED ON</button>&nbsp;"
            "<button type='button' onclick='cmd(\"SetLed\",0);' class='btn btn-primary'>LED OFF</button><br>"
            "<label for='SetMaskChannel'>Set channel mask</label>"
            "<input class='form-control' id='SetMaskChannel' type='text' name='SetMaskChannel' value='{{SetMaskChannel}}'><br>"
            "<button type='button' onclick='cmd(\"SetChannelMask\",document.getElementById(\"SetMaskChannel\").value);' class='btn btn-primary'>Set Channel</button><br> "
          "</div>"
        "</div>"
      "</div>"
      /*"<h5 class='card-title mb-4 mt-4'>Firmware ZiGate</h5>"
      "<div class='card mx-auto shadow-sm'>"
        "<div class='card-body'>"
          "<div class='mb-3'>"
            "<p>Mise à jour du firmware de la ZiGate+ (JN5189)</p>"
            "<div class='alert alert-info'>"
              "<strong>Mode Manuel:</strong> Avant de lancer le flash, mettez la ZiGate en mode bootloader:<br>"
              "<ol class='mb-0'>"
                "<li>Maintenez le bouton <b>FLASH</b> de la ZiGate</li>"
                "<li>Appuyez brièvement sur <b>RESET</b> tout en maintenant FLASH</li>"
                "<li>Relâchez le bouton FLASH après 1 seconde</li>"
                "<li>Lancez le flash ci-dessous</li>"
              "</ol>"
            "</div>"
            "<form method='POST' action='/flashZigate' enctype='multipart/form-data' id='zigate_flash_form'>"
              "<div class='row'>"
                "<div class='col-md-6 mb-3'>"
                  "<label for='zigatefw' class='form-label'>Fichier firmware (.bin)</label>"
                  "<input class='form-control' type='file' id='zigatefw' name='zigatefw' accept='.bin' required>"
                "</div>"
                "<div class='col-md-3 mb-3'>"
                  "<label for='zigateBaud' class='form-label'>Vitesse</label>"
                  "<select class='form-select' id='zigateBaud' name='baudrate'>"
                    "<option value='38400'>38400</option>"
                    "<option value='115200' selected>115200</option>"
                    "<option value='230400'>230400</option>"
                    "<option value='460800'>460800</option>"
                    "<option value='921600'>921600</option>"
                    "<option value='1000000'>1000000</option>"
                  "</select>"
                "</div>"
                "<div class='col-md-3 mb-3'>"
                  "<label for='zigateMode' class='form-label'>Mode</label>"
                  "<select class='form-select' id='zigateMode' name='mode'>"
                    "<option value='gpio' selected>GPIO Auto</option>"
                    "<option value='manual'>Manuel</option>"
                  "</select>"
                "</div>"
              "</div>"
              "<div class='d-flex justify-content-between align-items-center'>"
                "<button type='submit' class='btn btn-warning' id='btnFlashZigate' onclick='return startZigateFlash();'>"
                  "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' class='bi bi-cpu' viewBox='0 0 16 16'>"
                    "<path d='M5 0a.5.5 0 0 1 .5.5V2h1V.5a.5.5 0 0 1 1 0V2h1V.5a.5.5 0 0 1 1 0V2h1V.5a.5.5 0 0 1 1 0V2A2.5 2.5 0 0 1 14 4.5h1.5a.5.5 0 0 1 0 1H14v1h1.5a.5.5 0 0 1 0 1H14v1h1.5a.5.5 0 0 1 0 1H14v1h1.5a.5.5 0 0 1 0 1H14a2.5 2.5 0 0 1-2.5 2.5v1.5a.5.5 0 0 1-1 0V14h-1v1.5a.5.5 0 0 1-1 0V14h-1v1.5a.5.5 0 0 1-1 0V14h-1v1.5a.5.5 0 0 1-1 0V14A2.5 2.5 0 0 1 2 11.5H.5a.5.5 0 0 1 0-1H2v-1H.5a.5.5 0 0 1 0-1H2v-1H.5a.5.5 0 0 1 0-1H2v-1H.5a.5.5 0 0 1 0-1H2A2.5 2.5 0 0 1 4.5 2V.5A.5.5 0 0 1 5 0m-.5 3A1.5 1.5 0 0 0 3 4.5v7A1.5 1.5 0 0 0 4.5 13h7a1.5 1.5 0 0 0 1.5-1.5v-7A1.5 1.5 0 0 0 11.5 3h-7zM5 6.5A1.5 1.5 0 0 1 6.5 5h3A1.5 1.5 0 0 1 11 6.5v3A1.5 1.5 0 0 1 9.5 11h-3A1.5 1.5 0 0 1 5 9.5v-3zM6.5 6a.5.5 0 0 0-.5.5v3a.5.5 0 0 0 .5.5h3a.5.5 0 0 0 .5-.5v-3a.5.5 0 0 0-.5-.5h-3z'/>"
                  "</svg>"
                  " Flasher"
                "</button>"
                "<span id='zigateFlashStatus'></span>"
              "</div>"
            "</form>"
            "<div id='zigateFlashProgress' style='display:none;' class='mt-3'>"
              "<div class='progress'>"
                "<div id='zigateProgressBar' class='progress-bar progress-bar-striped progress-bar-animated' role='progressbar' style='width: 0%'>0%</div>"
              "</div>"
              "<p id='zigateFlashMsg' class='mt-2 text-muted'></p>"
            "</div>"
          "</div>"
        "</div>"
      "</div>"
      "<script>"
        "function startZigateFlash() {"
          "var fileInput = document.getElementById('zigatefw');"
          "var baudSelect = document.getElementById('zigateBaud');"
          "var modeSelect = document.getElementById('zigateMode');"
          "if (!fileInput.files.length) { alert('Sélectionnez un fichier firmware'); return false; }"
          "var msg = modeSelect.value === 'manual' ? "
            "'IMPORTANT: Avez-vous mis la ZiGate en mode bootloader ?\\n\\n"
            "1. Maintenir FLASH\\n2. Appuyer RESET\\n3. Relâcher FLASH\\n\\nContinuer ?' : "
            "'Le flash va interrompre le réseau Zigbee. Continuer ?';"
          "if (!confirm(msg)) return false;"
          "document.getElementById('btnFlashZigate').disabled = true;"
          "document.getElementById('zigateFlashProgress').style.display = 'block';"
          "document.getElementById('zigateFlashStatus').innerHTML = '<span class=\"text-info\">Upload en cours...</span>';"
          "var formData = new FormData();"
          "formData.append('zigatefw', fileInput.files[0]);"
          "var baudRate = baudSelect.value;"
          "var mode = modeSelect.value;"
          "var xhr = new XMLHttpRequest();"
          "xhr.open('POST', '/flashZigate?baudrate=' + baudRate + '&mode=' + mode, true);"
          "xhr.upload.onprogress = function(e) {"
            "if (e.lengthComputable) {"
              "var pct = Math.round((e.loaded / e.total) * 50);"
              "document.getElementById('zigateProgressBar').style.width = pct + '%';"
              "document.getElementById('zigateProgressBar').textContent = 'Upload: ' + pct*2 + '%';"
            "}"
          "};"
          "xhr.onload = function() {"
            "if (xhr.status === 200) {"
              "document.getElementById('zigateFlashStatus').innerHTML = '<span class=\"text-info\">Flash en cours...</span>';"
              "pollZigateFlashStatus();"
            "} else {"
              "document.getElementById('zigateFlashStatus').innerHTML = '<span class=\"text-danger\">Erreur: ' + xhr.responseText + '</span>';"
              "document.getElementById('btnFlashZigate').disabled = false;"
            "}"
          "};"
          "xhr.onerror = function() {"
            "document.getElementById('zigateFlashStatus').innerHTML = '<span class=\"text-danger\">Erreur réseau</span>';"
            "document.getElementById('btnFlashZigate').disabled = false;"
          "};"
          "xhr.send(formData);"
          "return false;"
        "}"
        "function pollZigateFlashStatus() {"
          "fetch('/zigateFlashStatus').then(r => r.json()).then(data => {"
            "document.getElementById('zigateProgressBar').style.width = (50 + data.progress/2) + '%';"
            "document.getElementById('zigateProgressBar').textContent = data.progress + '%';"
            "document.getElementById('zigateFlashMsg').textContent = data.message;"
            "if (data.status === 'flashing') {"
              "setTimeout(pollZigateFlashStatus, 500);"
            "} else if (data.status === 'success') {"
              "document.getElementById('zigateProgressBar').classList.remove('progress-bar-animated');"
              "document.getElementById('zigateProgressBar').classList.add('bg-success');"
              "document.getElementById('zigateFlashStatus').innerHTML = '<span class=\"text-success\">Flash terminé !</span>';"
              "document.getElementById('btnFlashZigate').disabled = false;"
            "} else {"
              "document.getElementById('zigateProgressBar').classList.remove('progress-bar-animated');"
              "document.getElementById('zigateProgressBar').classList.add('bg-danger');"
              "document.getElementById('zigateFlashStatus').innerHTML = '<span class=\"text-danger\">' + data.message + '</span>';"
              "document.getElementById('btnFlashZigate').disabled = false;"
            "}"
          "}).catch(e => {"
            "document.getElementById('zigateFlashStatus').innerHTML = '<span class=\"text-danger\">Erreur communication</span>';"
            "document.getElementById('btnFlashZigate').disabled = false;"
          "});"
        "}"
      "</script>"*/
    "</div>"
  "</div>"    ;

const char HTTP_CONFIG_HORLOGE[] PROGMEM = R"(
  <div class='container p-4'>
    <h4 class='card-title mb-4'>Config horloge</h4>
    <div class='card mx-auto shadow-sm' >
      <div class="card-body">
        <form method='POST' action='saveConfigHorloge'>
        <span> Datetime : </span><br><br>{{FormattedDate}}
        <div class="mb-3">
          <label for='ntpserver'>NTP serveur URL</label>
          <input class='form-control' id='ntpserver' type='text' name='ntpserver' value='{{ntpserver}}'>
        </div>
        <div class="mb-3">
          <label for='timeoffset'>Time Offset</label>
          <input class='form-control' id='timeoffset' type='text' name='timeoffset' value='{{timeoffset}}'>
        </div>
        <div class="mb-3">
          <label for='timezone'>Time Zone</label>
          <input class='form-control' id='timezone' type='text' name='timezone' value='{{timezone}}'>
        </div>
        <div class="mb-3">
          <label for='epochtime'>UTC epoch date</label>
          <input class='form-control' id='epochtime' type='text' name='epochtime' value='{{epochtime}}'>
        <br>
        <div class="d-flex justify-content-end">
          <button type="submit" class="btn btn-warning btn-lg" onclick='document.getElementById("reboot").style.display="block";'>Enregistrer</button>
        </div>
        </form>
        <div id='reboot' style='display:none;'><img src='web/img/wait.gif' /> Redémarrage ...</div>
      </div>
    </div>
  </div>
)";


const char HTTP_CONFIG_RULES[] PROGMEM = R"rawstring(
    <div class='container p-4'>
      <div class='d-flex justify-content-between align-items-center mb-4'>
        <h4 class='card-title mb-0'>Config règles</h4>
        <a href='/addRule' class='btn btn-primary'>
          <svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' class='bi bi-plus-circle' viewBox='0 0 16 16'>
            <path d='M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16'/>
            <path d='M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4'/>
          </svg>
          Ajouter une règle
        </a>
      </div>
      <div class='card mx-auto shadow-sm' >
        <div class="card-body">
          {{rulesList}}
        </div>
      </div>
    </div>
    <script>
      function deleteRule(ruleName) {
        if (!confirm('Voulez-vous vraiment supprimer la règle "' + ruleName + '" ?')) {
          return;
        }

        $.ajax({
          url: '/api/rules/delete',
          type: 'POST',
          contentType: 'application/json',
          data: JSON.stringify({ name: ruleName }),
          success: function() {
            alert('Règle supprimée avec succès !');
            location.reload();
          },
          error: function(xhr) {
            alert('Erreur lors de la suppression : ' + xhr.responseText);
          }
        });
      }

      function toggleRule(ruleName, cb) {
        $.ajax({
          url: '/api/rules/toggle',
          type: 'POST',
          contentType: 'application/json',
          data: JSON.stringify({ name: ruleName, enabled: cb.checked }),
          success: function() {
            var row = $(cb).closest('tr');
            if (cb.checked) { row.find('.rule-name').removeClass('text-muted'); }
            else { row.find('.rule-name').addClass('text-muted'); }
          },
          error: function(xhr) {
            alert('Erreur : ' + xhr.responseText);
            cb.checked = !cb.checked;
          }
        });
      }
    </script>
)rawstring";

// ============================================
// HTTP_EDIT_RULE - Page d'édition de règle
// ============================================
const char HTTP_EDIT_RULE_HTML[] PROGMEM = R"rawstring(
<div class='container p-4'>
  <h4 class='card-title mb-4'>Éditer une règle</h4>
  <div class='card mx-auto shadow-sm'>
    <div class="card-body">
      <form id="ruleForm">
        <input type="hidden" id="oldRuleName">

        <div class="mb-4">
          <label for='ruleName' class="form-label fw-bold">Nom de la règle</label>
          <input class='form-control' id='ruleName' type='text' required>
        </div>

        <div class="mb-4">
          <div class="form-check form-switch">
            <input class="form-check-input" type="checkbox" id="ruleEnabled" checked>
            <label class="form-check-label fw-bold" for="ruleEnabled">Règle activée</label>
          </div>
        </div>

        <div class="mb-4">
          <label class="form-label fw-bold">Déclenchement</label>
          <div class="card"><div class="card-body">
            <div class="row g-2">
              <div class="col-md-2">
                <label class="form-label small">Mode</label>
                <select class="form-select form-select-sm" id="triggerMode" onchange="onTriggerModeChange()">
                  <option value="timer">Timer (60s)</option>
                  <option value="event">Event</option>
                </select>
              </div>
              <div class="col-md-3 trigger-event-fields" style="display:none;">
                <label class="form-label small">Appareil</label>
                <select class="form-select form-select-sm" id="triggerDevice" onchange="onTriggerDeviceChange()"><option value="">-- Choisir --</option></select>
              </div>
              <div class="col-md-3 trigger-event-fields" style="display:none;">
                <label class="form-label small">Cluster</label>
                <select class="form-select form-select-sm" id="triggerCluster" onchange="onTriggerClusterChange()"><option value="">-- Cluster --</option></select>
              </div>
              <div class="col-md-3 trigger-event-fields" style="display:none;">
                <label class="form-label small">Attribut</label>
                <select class="form-select form-select-sm" id="triggerAttribute"><option value="">-- Attribut --</option></select>
              </div>
            </div>
          </div></div>
        </div>

        <div class="mb-4">
          <label class="form-label fw-bold">SI ... (Conditions)</label>
          <div id="conditionsContainer"></div>
          <button type="button" class="btn btn-sm btn-outline-primary mt-2" onclick="addCondition()">+ Condition</button>
        </div>

        <div class="mb-4">
          <label class="form-label fw-bold">Options d'évaluation</label>
          <div class="d-flex align-items-center gap-4 flex-wrap">
            <div class="d-flex align-items-center gap-2">
              <label class="form-label small mb-0 text-nowrap">Mode :</label>
              <select class="form-select form-select-sm" id="ruleRepeat" style="width:auto;" onchange="updateModeInfo()">
                <option value="0">Sur changement d'état</option>
                <option value="1">À chaque évaluation</option>
              </select>
            </div>
            <div class="d-flex align-items-center gap-2">
              <label class="form-label small mb-0 text-nowrap">Maintien :</label>
              <div class="input-group input-group-sm" style="width:150px;">
                <input type="number" class="form-control" id="ruleDuration" min="0" value="0">
                <span class="input-group-text">min</span>
              </div>
            </div>
            <div class="d-flex align-items-center gap-2" id="cooldownGroup" style="display:none;">
              <label class="form-label small mb-0 text-nowrap">Intervalle min :</label>
              <div class="input-group input-group-sm" style="width:150px;">
                <input type="number" class="form-control" id="ruleCooldown" min="0" value="0">
                <span class="input-group-text">min</span>
              </div>
            </div>
            <div class="d-flex align-items-center gap-2">
              <label class="form-label small mb-0 text-nowrap">Max/jour :</label>
              <div class="input-group input-group-sm" style="width:120px;">
                <input type="number" class="form-control" id="ruleMaxExecPerDay" min="0" value="0">
              </div>
              <span class="form-text small text-muted mb-0">(0=illimit&eacute;)</span>
            </div>
          </div>
          <div id="modeInfoBox" class="alert alert-info small mt-2 mb-0" style="font-size:0.85em;"></div>
        </div>

        <div class="mb-4">
          <label class="form-label fw-bold">ALORS ... (Actions)</label>
          <div id="actionsContainer"></div>
          <button type="button" class="btn btn-sm btn-outline-primary" onclick="addAction()">+ Action</button>
        </div>

        <div class="mb-4">
          <label class="form-label fw-bold">SINON ... (Actions)</label>
          <div id="elseActionsContainer"></div>
          <button type="button" class="btn btn-sm btn-outline-primary" onclick="addElseAction()">+ Action</button>
        </div>

        <div class="mb-4">
          <label class="form-label fw-bold">Résumé</label>
          <div id="ruleSummary" class="card bg-light">
            <div class="card-body py-2" style="font-size:0.9em;">
              <em class="text-muted">Complétez la règle pour voir le résumé...</em>
            </div>
          </div>
        </div>

        <div class="d-flex justify-content-between mt-4">
          <a href="/configRules" class="btn btn-secondary btn-lg">Annuler</a>
          <button type="submit" class="btn btn-primary btn-lg">Enregistrer</button>
        </div>
      </form>
    </div>
  </div>
</div>
)rawstring";

const char HTTP_EDIT_RULE_JS[] PROGMEM = R"rawstring(
<script src='web/js/rules.js'></script>
<script>
$(document).ready(function(){
  initRulesEditor('edit', typeof ruleToEdit!=='undefined' ? ruleToEdit : null);
  setupSubmitHandler('edit');
});

</script>
)rawstring";

const char HTTP_ADD_RULE_HTML[] PROGMEM = R"rawstring(
<div class='container p-4'>
  <h4 class='card-title mb-4'>Ajouter une règle</h4>
  <div class='card mx-auto shadow-sm'>
    <div class="card-body">
      <form id="ruleForm">

        <div class="mb-4">
          <label for='ruleName' class="form-label fw-bold">Nom de la règle</label>
          <input class='form-control' id='ruleName' type='text' required>
        </div>

        <div class="mb-4">
          <div class="form-check form-switch">
            <input class="form-check-input" type="checkbox" id="ruleEnabled" checked>
            <label class="form-check-label fw-bold" for="ruleEnabled">Règle activée</label>
          </div>
        </div>

        <div class="mb-4">
          <label class="form-label fw-bold">Déclenchement</label>
          <div class="card"><div class="card-body">
            <div class="row g-2">
              <div class="col-md-2">
                <label class="form-label small">Mode</label>
                <select class="form-select form-select-sm" id="triggerMode" onchange="onTriggerModeChange()">
                  <option value="timer">Timer (60s)</option>
                  <option value="event">Event</option>
                </select>
              </div>
              <div class="col-md-3 trigger-event-fields" style="display:none;">
                <label class="form-label small">Appareil</label>
                <select class="form-select form-select-sm" id="triggerDevice" onchange="onTriggerDeviceChange()"><option value="">-- Choisir --</option></select>
              </div>
              <div class="col-md-3 trigger-event-fields" style="display:none;">
                <label class="form-label small">Cluster</label>
                <select class="form-select form-select-sm" id="triggerCluster" onchange="onTriggerClusterChange()"><option value="">-- Cluster --</option></select>
              </div>
              <div class="col-md-3 trigger-event-fields" style="display:none;">
                <label class="form-label small">Attribut</label>
                <select class="form-select form-select-sm" id="triggerAttribute"><option value="">-- Attribut --</option></select>
              </div>
            </div>
          </div></div>
        </div>

        <div class="mb-4">
          <label class="form-label fw-bold">SI ... (Conditions)</label>
          <div id="conditionsContainer"></div>
          <button type="button" class="btn btn-sm btn-outline-primary mt-2" onclick="addCondition()">+ Condition</button>
        </div>

        <div class="mb-4">
          <label class="form-label fw-bold">Options d'évaluation</label>
          <div class="d-flex align-items-center gap-4 flex-wrap">
            <div class="d-flex align-items-center gap-2">
              <label class="form-label small mb-0 text-nowrap">Mode :</label>
              <select class="form-select form-select-sm" id="ruleRepeat" style="width:auto;" onchange="updateModeInfo()">
                <option value="0">Sur changement d'état</option>
                <option value="1">À chaque évaluation</option>
              </select>
            </div>
            <div class="d-flex align-items-center gap-2">
              <label class="form-label small mb-0 text-nowrap">Maintien :</label>
              <div class="input-group input-group-sm" style="width:150px;">
                <input type="number" class="form-control" id="ruleDuration" min="0" value="0">
                <span class="input-group-text">min</span>
              </div>
            </div>
            <div class="d-flex align-items-center gap-2" id="cooldownGroup" style="display:none;">
              <label class="form-label small mb-0 text-nowrap">Intervalle min :</label>
              <div class="input-group input-group-sm" style="width:150px;">
                <input type="number" class="form-control" id="ruleCooldown" min="0" value="0">
                <span class="input-group-text">min</span>
              </div>
            </div>
            <div class="d-flex align-items-center gap-2">
              <label class="form-label small mb-0 text-nowrap">Max/jour :</label>
              <div class="input-group input-group-sm" style="width:120px;">
                <input type="number" class="form-control" id="ruleMaxExecPerDay" min="0" value="0">
              </div>
              <span class="form-text small text-muted mb-0">(0=illimit&eacute;)</span>
            </div>
          </div>
          <div id="modeInfoBox" class="alert alert-info small mt-2 mb-0" style="font-size:0.85em;"></div>
        </div>

        <div class="mb-4">
          <label class="form-label fw-bold">ALORS ... (Actions)</label>
          <div id="actionsContainer"></div>
          <button type="button" class="btn btn-sm btn-outline-primary" onclick="addAction()">+ Action</button>
        </div>

        <div class="mb-4">
          <label class="form-label fw-bold">SINON ... (Actions)</label>
          <div id="elseActionsContainer"></div>
          <button type="button" class="btn btn-sm btn-outline-primary" onclick="addElseAction()">+ Action</button>
        </div>

        <div class="mb-4">
          <label class="form-label fw-bold">Résumé</label>
          <div id="ruleSummary" class="card bg-light">
            <div class="card-body py-2" style="font-size:0.9em;">
              <em class="text-muted">Complétez la règle pour voir le résumé...</em>
            </div>
          </div>
        </div>

        <div class="d-flex justify-content-between mt-4">
          <a href="/configRules" class="btn btn-secondary btn-lg">Annuler</a>
          <button type="submit" class="btn btn-primary btn-lg">Créer</button>
        </div>
      </form>
    </div>
  </div>
</div>
)rawstring";

const char HTTP_ADD_RULE_JS[] PROGMEM = R"rawstring(
<script src='web/js/rules.js'></script>
<script>
$(document).ready(function(){
  initRulesEditor('add');
  setupSubmitHandler('add');
});
</script>
)rawstring";

 const char HTTP_CONFIG_MQTT[] PROGMEM = R"(
    <div class='container p-4'>
      <h4 class='card-title mb-4'>Config MQTT</h4>
      <div class='card mx-auto shadow-sm' >
        <div class="card-body">
        <form method='POST' action='saveConfigMQTT'>
          <div class="form-check form-switch mb-3">
            <input class="form-check-input" type="checkbox" id="enableMqtt" name='enableMqtt' {{checkedMqtt}}>
            <label class="form-check-label" for="enableMqtt">Activer MQTT</label>
          </div>
          <div class="mb-3">
            <label for='servMQTT' class="form-label">Serveur MQTT</label>
            <input class='form-control' id='servMQTT' type='text' name='servMQTT' value='{{servMQTT}}'>
          </div>
          <div class="mb-3">
            <label for='portMQTT' class="form-label">Port MQTT</label>
            <input class='form-control' id='portMQTT' type='text' name='portMQTT' value='{{portMQTT}}'>
          </div>
          <div class="mb-3">
            <label for='clientIDMQTT' class="form-label">Client ID MQTT</label>
            <input class='form-control' id='clientIDMQTT' type='text' name='clientIDMQTT' value='{{clientIDMQTT}}'>
          </div>
          <div class="mb-3">
            <label for='userMQTT' class="form-label">Identifiant MQTT</label>
            <input class='form-control' id='userMQTT' type='text' name='userMQTT' value='{{userMQTT}}'>
          </div>
          <div class="mb-3">
            <label for='passMQTT' class="form-label">Mot de passe MQTT</label>
            <input class='form-control' id='passMQTT' type='password' name='passMQTT' value='{{passMQTT}}'>
          </div>
          <div class="mb-3">
            <label for='headerMQTT' class="form-label">Topic header MQTT</label>
            <input class='form-control' id='headerMQTT' type='text' name='headerMQTT' value='{{headerMQTT}}'>
          </div>

          <div class='form-check'>
            <input class='form-check-input' id='ha' type='radio' name='appliMQTT' value='HA' {{checkedHA}} onClick='document.getElementById("displayCustomMQTT").style.display="none";document.getElementById("headerMQTT").value="homeassistant/sensor/";'>
            <label class='form-check-label' for='ha'>Home-Assistant</label>
          </div>
          <div class='form-check'>
            <input class='form-check-input' id='TB' type='radio' name='appliMQTT' value='TB' {{checkedTB}} onClick='document.getElementById("displayCustomMQTT").style.display="none";document.getElementById("headerMQTT").value="v1/gateway/telemetry";'>
            <label class='form-check-label' for='TB'>ThingsBoard</label>
          </div>
          <div class='form-check'>
            <input class='form-check-input' id='custom' type='radio' name='appliMQTT' value='custom' onClick='document.getElementById("displayCustomMQTT").style.display="block";' {{checkedCustom}}>
            <label class='form-check-label' for='custom'>Personnalisé</label>
          </div>
          <div class='form-floating' id='displayCustomMQTT' style='{{displayCustomMQTT}}'>
            <textarea class='form-control' name='customMQTTJson' placeholder='' id='customMQTTJson' style='min-height:200px;'>{{customMQTTJson}}</textarea>
            <label for='customMQTTJson'>JSON personnalisé</label>
          </div>
          <br><Strong>Connecté : </strong><span id='mqttStatus'><img src='web/img/wait.gif' /></span>
          <br>
          <div class="d-flex justify-content-end">
            <button type="submit" class="btn btn-warning btn-lg">Enregistrer</button>
          </div>
        </form>
        </div>
      </div>
    </div>
    <script>
      function fetchStatus(){
        $.ajax({
          url: '/getMQTTStatus',
          type: 'GET',
          success:function(data) {
            if (data=='1')
            {
              $('#mqttStatus').html('<img src="web/img/ok.png" />');
            }else{
                $('#mqttStatus').html('<img src="web/img/nok.png" />');
            }
          }
        });
      }
      $(document).ready(function() {
          setTimeout(function(){fetchStatus();setInterval(fetchStatus,2000);},5000); 
      });
    </script>      
 )";

 const char HTTP_CONFIG_HTTP[] PROGMEM = R"(
    <div class='container p-4'>
      <h4 class='card-title mb-4'>Accès sécurisé</h4>
      <div class='card mx-auto shadow-sm' >
        <div class="card-body">
          <form method='POST' action='saveConfigHTTP'>
          {{securityLockWarning}}
          <div class="form-check form-switch mb-3">
            <input class="form-check-input" type="checkbox" id="enableSecureHttp" name='enableSecureHttp' {{checkedHttp}} {{disabledSecuToggle}}>
            <label class="form-check-label" for="enableSecureHttp">Activer l'accès sécurisé</label>
          </div>
          <div class="mb-3">
            <label for='userHTTP' class="form-label">Identifiant</label>
            <input class='form-control' id='userHTTP' type='text' name='userHTTP' value='{{userHTTP}}' style='{{userborder}}'>
          </div>
          <div class="mb-3">
            <label for='passHTTP' class="form-label">Mot de passe</label>
            <input class='form-control' id='passHTTP' type='password' name='passHTTP' value='{{passHTTP}}' style='{{passborder}}'>
          </div>
          <small class='text-muted'>La session reste active pendant 24 heures. Les clients API peuvent aussi utiliser l'authentification Basic.</small>
          <br><br>
          <div class="d-flex justify-content-end">
            <button type="submit" class="btn btn-warning btn-lg">Enregistrer</button>
          </div>
          </form>
          <div style='color:red'>{{error}}</div>
        </div>
      </div>
    </div>
 )";

const char HTTP_LOGIN[] PROGMEM = R"(
<!DOCTYPE html>
<html>
<head>
  <meta charset='utf-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <link rel='icon' type='image/x-icon' href='web/favicon.ico'>
  <title>Box - Connexion</title>
  <link href='web/css/bootstrap.min.css' rel='stylesheet'>
  <style>
    body {
      background-color: #f7f9fc;
      font-family: 'Inter', sans-serif;
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .login-card {
      width: 100%;
      max-width: 400px;
      border-radius: 1rem;
      box-shadow: 0 4px 24px rgba(0,0,0,0.10);
    }
    .login-logo {
      text-align: center;
      margin-bottom: 1.5rem;
    }
    .login-logo img {
      height: 48px;
    }
  </style>
</head>
<body>
  <div class='card login-card'>
    <div class='card-body p-4'>
      <div class='login-logo'>
        <img src='web/img/logo.png' alt='Box' onerror="this.style.display='none'">
        <h4 class='mt-2'>Box</h4>
      </div>
      <div id='error' class='alert alert-danger {{errorDisplay}}'>{{errorMsg}}</div>
      <form method='POST' action='/login'>
        <div class='mb-3'>
          <label for='user' class='form-label'>Identifiant</label>
          <input class='form-control' id='user' type='text' name='user' required autofocus>
        </div>
        <div class='mb-3'>
          <label for='pass' class='form-label'>Mot de passe</label>
          <div class='input-group'>
            <input class='form-control' id='pass' type='password' name='pass' required>
            <button class='btn btn-outline-secondary' type='button' id='togglePass' tabindex='-1'>
              <svg id='eyeIcon' xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' viewBox='0 0 16 16'>
                <path d='M16 8s-3-5.5-8-5.5S0 8 0 8s3 5.5 8 5.5S16 8 16 8zM1.173 8a13.133 13.133 0 0 1 1.66-2.043C4.12 4.668 5.88 3.5 8 3.5c2.12 0 3.879 1.168 5.168 2.457A13.133 13.133 0 0 1 14.828 8c-.058.087-.122.183-.195.288-.335.48-.83 1.12-1.465 1.755C11.879 11.332 10.119 12.5 8 12.5c-2.12 0-3.879-1.168-5.168-2.457A13.134 13.134 0 0 1 1.172 8z'/>
                <path d='M8 5.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5zM4.5 8a3.5 3.5 0 1 1 7 0 3.5 3.5 0 0 1-7 0z'/>
              </svg>
            </button>
          </div>
        </div>
        <button type='submit' class='btn btn-warning w-100 btn-lg'>Se connecter</button>
      </form>
      <div class='text-center text-muted mt-3' style='font-size:0.8rem;'>{{version}}</div>
    </div>
  </div>
  <script>
    document.getElementById('togglePass').addEventListener('click',function(){
      var p=document.getElementById('pass'),e=document.getElementById('eyeIcon');
      if(p.type==='password'){p.type='text';e.innerHTML='<path d="M13.359 11.238C15.06 9.72 16 8 16 8s-3-5.5-8-5.5a7.028 7.028 0 0 0-2.79.588l.77.771A5.944 5.944 0 0 1 8 3.5c2.12 0 3.879 1.168 5.168 2.457A13.134 13.134 0 0 1 14.828 8c-.058.087-.122.183-.195.288-.335.48-.83 1.12-1.465 1.755-.165.165-.337.328-.517.486l.708.709z"/><path d="M11.297 9.176a3.5 3.5 0 0 0-4.474-4.474l.823.823a2.5 2.5 0 0 1 2.829 2.829l.822.822zm-2.943 1.299l.822.822a3.5 3.5 0 0 1-4.474-4.474l.823.823a2.5 2.5 0 0 0 2.829 2.829z"/><path d="M3.35 5.47c-.18.16-.353.322-.518.487A13.134 13.134 0 0 0 1.172 8l.195.288c.335.48.83 1.12 1.465 1.755C4.121 11.332 5.881 12.5 8 12.5c.716 0 1.39-.133 2.02-.36l.77.772A7.029 7.029 0 0 1 8 13.5C3 13.5 0 8 0 8s.939-1.721 2.641-3.238l.708.709z"/><path d="M13.646 14.354l-12-12 .708-.708 12 12-.708.708z"/>';}
      else{p.type='password';e.innerHTML='<path d="M16 8s-3-5.5-8-5.5S0 8 0 8s3 5.5 8 5.5S16 8 16 8zM1.173 8a13.133 13.133 0 0 1 1.66-2.043C4.12 4.668 5.88 3.5 8 3.5c2.12 0 3.879 1.168 5.168 2.457A13.133 13.133 0 0 1 14.828 8c-.058.087-.122.183-.195.288-.335.48-.83 1.12-1.465 1.755C11.879 11.332 10.119 12.5 8 12.5c-2.12 0-3.879-1.168-5.168-2.457A13.134 13.134 0 0 1 1.172 8z"/><path d="M8 5.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5zM4.5 8a3.5 3.5 0 1 1 7 0 3.5 3.5 0 0 1-7 0z"/>';}
    });
  </script>
</body>
</html>
)";

 const char HTTP_CONFIG_WEBPUSH[] PROGMEM = R"(
    <div class='container p-4'>
      <h4 class='card-title mb-4'>Config WebPush</h4>
      <div class='card mx-auto shadow-sm' >
        <div class="card-body">
          <form method='POST' action='saveConfigWebPush'>
          <div class="form-check form-switch mb-3">
            <input class="form-check-input" type="checkbox" id="enableWebPush" name='enableWebPush' {{checkedWebPush}}>
            <label class="form-check-label" for="enableWebPush">Activer WebPush</label>
          </div> 
          <div class="mb-3">
            <label for='servWebPush' class="form-label">Serveur HTTP</label>
            <input class='form-control' id='servWebPush' type='text' name='servWebPush' value='{{servWebPush}}' style='{{urlborder}}'>
          </div>
          <div class="form-check form-switch mb-3">
            <input class="form-check-input" type="checkbox" id="webPushAuth" name='webPushAuth' {{checkedWebPushAuth}} onClick='toggleDiv("authWebPush");'>
            <label class="form-check-label" for="webPushAuth">Activer l'authentification</label>
          </div> 
          <div id='authWebPush' style='{{displayWebPushAuth}}'>
            <h5>Authentification</h5>
            <div class="mb-3">
              <label for='userWebPush' class="form-label">Identifiant</label>
              <input class='form-control' id='userWebPush' type='text' name='userWebPush' value='{{userWebPush}}' style='{{userborder}}'>
            </div>
            <div class="mb-3">
              <label for='passWebPush' class="form-label">Mot de passe</label>
              <input class='form-control' id='passWebPush' type='password' name='passWebPush' value='{{passWebPush}}' style='{{passborder}}'>
            </div>
          </div>
          <div class="d-flex justify-content-end">
            <button type="submit" class="btn btn-warning btn-lg">Enregistrer</button>
          </div>
          </form>
          <div style='color:red'>{{error}}</div>
        </div> 
      </div>
      <br><br><h5>Exemple données HTTP</h5>
      Header : POST
      <br>Content-Type : JSON
      <br>Content :
      <br>
      <pre><code>
      {
        &nbsp;&nbsp;"IEEE" : "@mac",  
        &nbsp;&nbsp;"cluster" : "decimal",  
        &nbsp;&nbsp;"attribute" : "decimal",  
        &nbsp;&nbsp;"value" : "decimal / string" 
      }
      </code></pre>
    </div>

 )";

 const char HTTP_CONFIG_TUNNEL[] PROGMEM = R"(
    <div class='container p-4'>
      <h4 class='card-title mb-4'>Config Tunnel (Accès distant)</h4>
      {{securityWarning}}

      <div class='card mx-auto shadow-sm mb-3'>
        <div class='card-body'>
          <h5 class='card-title'>Activation rapide</h5>
          <div class='d-flex align-items-center gap-2 mb-3'>
            <span id='tunnelBadge' class='badge {{badgeClass}}'>{{badgeText}}</span>
            {{tunnelRemoteWarning}}
          </div>
          <div id='tunnelUrlBox'>{{tunnelUrl}}</div>
          <div class='d-flex justify-content-center align-items-center gap-2 mb-3'>
            <div class='d-flex gap-1' id='codeInputs'>
              <input class='form-control text-center digit-input' type='text' inputmode='numeric' maxlength='1' pattern='[0-9]' style='width:42px;height:48px;font-size:1.4em;font-weight:bold;padding:0'>
              <input class='form-control text-center digit-input' type='text' inputmode='numeric' maxlength='1' pattern='[0-9]' style='width:42px;height:48px;font-size:1.4em;font-weight:bold;padding:0'>
              <input class='form-control text-center digit-input' type='text' inputmode='numeric' maxlength='1' pattern='[0-9]' style='width:42px;height:48px;font-size:1.4em;font-weight:bold;padding:0'>
              <input class='form-control text-center digit-input' type='text' inputmode='numeric' maxlength='1' pattern='[0-9]' style='width:42px;height:48px;font-size:1.4em;font-weight:bold;padding:0'>
              <input class='form-control text-center digit-input' type='text' inputmode='numeric' maxlength='1' pattern='[0-9]' style='width:42px;height:48px;font-size:1.4em;font-weight:bold;padding:0'>
              <input class='form-control text-center digit-input' type='text' inputmode='numeric' maxlength='1' pattern='[0-9]' style='width:42px;height:48px;font-size:1.4em;font-weight:bold;padding:0'>
            </div>
            <button class='btn btn-warning' type='button' id='btnActivate' style='height:48px' {{disabledNoSec}}>Activer</button>
          </div>
          <div id='activateStatus' class='d-none'></div>
          <hr>
          <form method='POST' action='saveConfigTunnel' id='formToggle'>
            <div class='form-check form-switch'>
              <input class='form-check-input' type='checkbox' id='enableTunnel' name='enableTunnel' {{checkedTunnel}} {{disabledNoSec}}>
              <label class='form-check-label' for='enableTunnel'>Tunnel actif</label>
            </div>
            <input type='hidden' name='tunnelClientId' value='{{tunnelClientId}}'>
            <input type='hidden' name='tunnelToken' value='{{tunnelToken}}'>
            <noscript><button type='submit' class='btn btn-warning btn-sm mt-2'>Enregistrer</button></noscript>
          </form>
        </div>
      </div>

      <div class='card mx-auto shadow-sm mb-3'>
        <div class='card-body'>
          <details>
            <summary class='h5' style='cursor:pointer'>Connexion manuelle (avancé)</summary>
            <form method='POST' action='saveConfigTunnel' class='mt-3'>
              <div class='form-check form-switch mb-3'>
                <input class='form-check-input' type='checkbox' name='enableTunnel' {{checkedTunnelManual}} {{disabledNoSec}}>
                <label class='form-check-label'>Activer le Tunnel</label>
              </div>
              <div class='mb-3'>
                <label class='form-label'>Client ID</label>
                <input class='form-control' type='text' name='tunnelClientId' value='{{tunnelClientId}}'>
              </div>
              <div class='mb-3'>
                <label class='form-label'>Token</label>
                <input class='form-control' type='password' name='tunnelToken' value='{{tunnelToken}}'>
              </div>
              <button type='submit' class='btn btn-warning' {{disabledNoSec}}>Enregistrer</button>
            </form>
          </details>
        </div>
      </div>

      <div class='card mx-auto shadow-sm'>
        <div class='card-body'>
          <h5>Comment activer le tunnel ?</h5>
          <ol>
            <li>Rendez-vous sur <a href='https://remote.lixee-box.fr' target='_blank'>remote.lixee-box.fr</a> et créez un compte.</li>
            <li>Ajoutez un nouveau dispositif depuis votre tableau de bord.</li>
            <li>Cliquez sur "Générer un code d'activation" pour obtenir un code à 6 chiffres.</li>
            <li>Entrez ce code ci-dessus et cliquez "Activer".</li>
            <li>Votre gateway sera alors accessible à distance sans ouvrir de ports sur votre box Internet.</li>
          </ol>
        </div>
      </div>

      <div style='color:red'>{{error}}</div>
    </div>

    <script>
    document.getElementById('enableTunnel').addEventListener('change', function() {
      document.getElementById('formToggle').submit();
    });

    // Gestion des 6 inputs individuels
    var digits = document.querySelectorAll('.digit-input');
    digits.forEach(function(inp, i) {
      inp.addEventListener('input', function() {
        this.value = this.value.replace(/[^0-9]/g, '');
        if (this.value.length === 1 && i < 5) digits[i+1].focus();
      });
      inp.addEventListener('keydown', function(e) {
        if (e.key === 'Backspace' && this.value === '' && i > 0) {
          digits[i-1].focus(); digits[i-1].value = '';
        }
      });
      inp.addEventListener('paste', function(e) {
        e.preventDefault();
        var txt = (e.clipboardData||window.clipboardData).getData('text').replace(/[^0-9]/g, '');
        for (var j = 0; j < 6 && j < txt.length; j++) digits[j].value = txt[j];
        if (txt.length >= 6) digits[5].focus();
        else if (txt.length > 0) digits[txt.length-1].focus();
      });
    });

    function getCode() {
      var c = '';
      digits.forEach(function(d) { c += d.value; });
      return c;
    }

    document.getElementById('btnActivate').addEventListener('click', function() {
      var code = getCode();
      if (!/^\d{6}$/.test(code)) {
        showStatus('Veuillez entrer un code à 6 chiffres', 'danger');
        return;
      }
      var btn = this;
      btn.disabled = true;
      showStatus('<span class="spinner-border spinner-border-sm"></span> Activation en cours...', 'warning');
      fetch('/api/tunnelActivate?code=' + code)
        .then(function(r) { return r.json(); })
        .then(function(data) {
          if (data.status === 'pending' || data.status === 'processing') {
            pollActivation(btn);
          } else if (data.status === 'error') {
            showStatus(data.error || 'Erreur', 'danger');
            btn.disabled = false;
          }
        })
        .catch(function() {
          showStatus('Erreur de connexion', 'danger');
          btn.disabled = false;
        });
    });

    function pollActivation(btn) {
      var fails = 0;
      var iv = setInterval(function() {
        fetch('/api/tunnelActivateStatus')
          .then(function(r) { return r.json(); })
          .then(function(d) {
            fails = 0;
            if (d.status === 'done') {
              clearInterval(iv);
              if (d.success) {
                showStatus('Tunnel activé avec succès !', 'success');
                setTimeout(function() { location.reload(); }, 2000);
              } else {
                showStatus(d.error || 'Erreur inconnue', 'danger');
                btn.disabled = false;
              }
            }
          })
          .catch(function() {
            fails++;
            if (fails >= 15) {
              clearInterval(iv);
              showStatus('Erreur de connexion', 'danger');
              btn.disabled = false;
            }
          });
      }, 1000);
    }

    function showStatus(msg, type) {
      var el = document.getElementById('activateStatus');
      el.className = 'alert alert-' + type + ' py-2';
      el.innerHTML = msg;
    }

    function pollStatus() {
      fetch('/api/tunnelStatus').then(function(r){return r.json();}).then(function(d) {
        var b = document.getElementById('tunnelBadge');
        var u = document.getElementById('tunnelUrlBox');
        if (d.connected) {
          b.className='badge bg-success'; b.textContent='Connecté';
          if (d.url) {
            u.innerHTML="<div class='mb-3'><small class='text-muted'>URL :</small> <a href='"+d.url+"' target='_blank'>"+d.url+"</a></div>";
          }
        } else {
          u.innerHTML='';
          if (d.enabled) { b.className='badge bg-warning text-dark'; b.textContent='Déconnecté'; }
          else { b.className='badge bg-secondary'; b.textContent='Désactivé'; }
        }
      }).catch(function(){});
    }
    pollStatus();
    setInterval(pollStatus, 5000);
    </script>
 )";


const char HTTP_CONFIG_UDPCLIENT[] PROGMEM =
    
    "<div class='row justify-content-md-center' >"
    "<div class='col col-md-6'>"
    "<h4>Client UDP</h4>"
    "<form method='POST' action='saveConfigUDPClient'>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='enableUDP' type='checkbox' name='enableUDP' {{checkedUDP}}>"
    "<label class='form-check-label' for='enableUDP'>Enable UDP</label>"
    "</div>"
    "<label for='servUDP'>Server UDP</label>"
    "<input class='form-control' id='servUDP' type='text' name='servUDP' value='{{servUDP}}' style='{{urlborder}}'>"    
    "<label for='portUDP'>Port UDP</label>"
    "<input class='form-control' id='portUDP' type='text' name='portUDP' value='{{portUDP}}' style='{{portborder}}'>"
    "Datas :<br>"
    "<div class='form-floating' id='displayCustomUDP' style='{{displayCustomUDP}}'>"
      "<textarea class='form-control' name='customUDPJson' placeholder='' id='customUDPJson' style='min-height:200px;'>{{customUDPJson}}</textarea>"
      "<label for='customUDPJson'>Custom JSON</label>"
    "</div>"

    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form>"
    "<div style='color:red'>{{error}}</div>"

  
    "</div>"
    "</div>"

    ;

const char HTTP_CONFIG_NOTIFICATION_MAIL[] PROGMEM =
    "<h4>Config Notification</h4>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigNotificationMail'>"
    "<h5>General</h5>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='enableNotif' type='checkbox' name='enableNotif' {{checkedNotif}}>"
    "<label class='form-check-label' for='enableNotif'>Enable Notification</label>"
    "</div>"
    "<h5>EMail</h5>"
    "<label for='servSMTP'>Server SMTP</label>"
    "<input class='form-control' id='servSMTP' type='text' name='servSMTP' value='{{servSMTP}}'>"
    "<label for='portSMTP'>Port SMTP</label>"
    "<input class='form-control' id='portSMTP' type='text' name='portSMTP' value='{{portSMTP}}'>"
    "<label for='userSMTP'>Username SMTP</label>"
    "<input class='form-control' id='userSMTP' type='text' name='userSMTP' value='{{userSMTP}}'>"
    "<label for='passSMTP'>Password SMTP</label>"
    "<input class='form-control' id='passSMTP' type='password' name='passSMTP' value='{{passSMTP}}'>"
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form></div>"
    "</div>";

const char HTTP_NETWORK[] PROGMEM =
    "<div class='container py-4' >"
      "<h4>Etat réseau</h4>"
      "<div class='row g-4'>"
        "<div class='col'>"
          "<div class='card'>"
            "<div class='card-header'>"
              "<svg xmlns='http://www.w3.org/2000/svg' style='width:24px;' width='24' height='24' fill='#000000' class='bi bi-wifi' viewBox='0 0 16 16'>"
                "<path d='M15.384 6.115a.485.485 0 0 0-.047-.736A12.44 12.44 0 0 0 8 3C5.259 3 2.723 3.882.663 5.379a.485.485 0 0 0-.048.736.52.52 0 0 0 .668.05A11.45 11.45 0 0 1 8 4c2.507 0 4.827.802 6.716 2.164.205.148.49.13.668-.049'/>"
                "<path d='M13.229 8.271a.482.482 0 0 0-.063-.745A9.46 9.46 0 0 0 8 6c-1.905 0-3.68.56-5.166 1.526a.48.48 0 0 0-.063.745.525.525 0 0 0 .652.065A8.46 8.46 0 0 1 8 7a8.46 8.46 0 0 1 4.576 1.336c.206.132.48.108.653-.065m-2.183 2.183c.226-.226.185-.605-.1-.75A6.5 6.5 0 0 0 8 9c-1.06 0-2.062.254-2.946.704-.285.145-.326.524-.1.75l.015.015c.16.16.407.19.611.09A5.5 5.5 0 0 1 8 10c.868 0 1.69.201 2.42.56.203.1.45.07.61-.091zM9.06 12.44c.196-.196.198-.52-.04-.66A2 2 0 0 0 8 11.5a2 2 0 0 0-1.02.28c-.238.14-.236.464-.04.66l.706.706a.5.5 0 0 0 .707 0l.707-.707z'/>"
              "</svg>"
              " Wifi"
            "</div>"
            "<div class='card-body'>"
              "<div id='wifiConfig'>"
                "<strong>Actif : </strong>{{enableWifi}}"
                "<br><strong>Connecté : </strong>{{connectedWifi}}"
                "<br><strong>SSID : </strong>{{ssidWifi}}"
                "<br><strong>Mode : </strong>{{modeWifi}}"
                "<br><strong>@IP : </strong>{{ipWifi}}"
                "<br><strong>@Masque : </strong>{{maskWifi}}"
                "<br><strong>@Passerelle : </strong>{{GWWifi}}"
                "<br><strong>Signal (RSSI) : </strong><span id='rssiVal'>{{rssiWifi}}</span>"
                "<br><strong>TX Power : </strong><span id='txPwrVal'>{{txPowerWifi}}</span>"
              "</div>"
            "</div>"
          "</div>"
        "</div>"
      "</div><br>"
      "<div class='row g-4'>"
        "<div class='col'>"
          "<div class='card'>"
            "<div class='card-header'>"
              "<svg style='width:24px;' width='24' height='24' viewBox='0 0 24 24' fill='none' xmlns='http://www.w3.org/2000/svg'>"
                "<path d='M12 16.75C11.8019 16.7474 11.6126 16.6676 11.4725 16.5275C11.3324 16.3874 11.2526 16.1981 11.25 16V11C11.25 10.8011 11.329 10.6103 11.4697 10.4697C11.6103 10.329 11.8011 10.25 12 10.25C12.1989 10.25 12.3897 10.329 12.5303 10.4697C12.671 10.6103 12.75 10.8011 12.75 11V16C12.7474 16.1981 12.6676 16.3874 12.5275 16.5275C12.3874 16.6676 12.1981 16.7474 12 16.75Z' fill='#000000'/>"
                "<path d='M12 9.25C11.8019 9.24741 11.6126 9.16756 11.4725 9.02747C11.3324 8.88737 11.2526 8.69811 11.25 8.5V8C11.25 7.80109 11.329 7.61032 11.4697 7.46967C11.6103 7.32902 11.8011 7.25 12 7.25C12.1989 7.25 12.3897 7.32902 12.5303 7.46967C12.671 7.61032 12.75 7.80109 12.75 8V8.5C12.7474 8.69811 12.6676 8.88737 12.5275 9.02747C12.3874 9.16756 12.1981 9.24741 12 9.25Z' fill='#000000'/>"
                "<path d='M12 21C10.22 21 8.47991 20.4722 6.99987 19.4832C5.51983 18.4943 4.36628 17.0887 3.68509 15.4442C3.0039 13.7996 2.82567 11.99 3.17294 10.2442C3.5202 8.49836 4.37737 6.89472 5.63604 5.63604C6.89472 4.37737 8.49836 3.5202 10.2442 3.17294C11.99 2.82567 13.7996 3.0039 15.4442 3.68509C17.0887 4.36628 18.4943 5.51983 19.4832 6.99987C20.4722 8.47991 21 10.22 21 12C21 14.387 20.0518 16.6761 18.364 18.364C16.6761 20.0518 14.387 21 12 21ZM12 4.5C10.5166 4.5 9.0666 4.93987 7.83323 5.76398C6.59986 6.58809 5.63856 7.75943 5.07091 9.12988C4.50325 10.5003 4.35473 12.0083 4.64411 13.4632C4.9335 14.918 5.64781 16.2544 6.6967 17.3033C7.7456 18.3522 9.08197 19.0665 10.5368 19.3559C11.9917 19.6453 13.4997 19.4968 14.8701 18.9291C16.2406 18.3614 17.4119 17.4001 18.236 16.1668C19.0601 14.9334 19.5 13.4834 19.5 12C19.5 10.0109 18.7098 8.10323 17.3033 6.6967C15.8968 5.29018 13.9891 4.5 12 4.5Z' fill='#000000'/>"
              "</svg>"
              " Informations système"
            "</div>"
            "<div class='card-body'>"
              "{{MQTT card}}"
              "<Strong>Température Box:</strong> {{Temperature}} °C<br>"
            "</div>"
          "</div>"
        "</div>"
      "</div>"
    "</div>";

const char HTTP_ROOT[] PROGMEM =
    "<h4>Dashboard</h4>"
    "<div class='row' style='--bs-gutter-x: 0.3rem;'>"
    "<div class='col-sm-12'>"
    "<Select class='form-select form-select-lg mb-3' aria-label='.form-select-lg example' name='time' onChange=\"window.location.href='?time='+this.value\">"
    "<option value='hour' {{selectedHour}}>Hour</option>"
    "<option value='day' {{selectedDay}}>Day</option>"
    "<option value='month' {{selectedMonth}}>Month</option>"
    "<option value='year' {{selectedYear}}>Year</option>"
    "</select>"
    "</div>"
    "</div>"
    "<div class='row'  style='--bs-gutter-x: 0.3rem;'>"
    "<div class='col col-md-6'>"
    "<div class='card' >"
    "<div class='card-header' style='font-size:12px;font-weight:bold;color:#FFF;background-color:#007bc6;'>Energy gauge</div>"
    "<div class='card-body' style='min-height:272px;'>"
    "<div id='power_gauge_global' style='height:230px;'></div>"
    "</div>"
    "</div>"
    "</div>"
    "<div class='col col-md-6'>"
    "<div class='card'>"
    "<div class='card-header' style='font-size:12px;font-weight:bold;color:#FFF;background-color:#007bc6;'>Energy trend</div>"
    "<div class='card-body' style='min-height:272px;'>"
    "<div id='power_trend'></div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "<div class='row'  style='--bs-gutter-x: 0.3rem;'>"
    "{{dashboard}}" 
    "</div>"
    "{{javascript}}";

const char HTTP_DASHBOARD[] PROGMEM =
    
    "<div class='container py-4' >"
      "<h4>Dashboard</h4>"
      "<div class='row justify-content-start gx-4 gy-4' id='masonry-grid'>" //style='--bs-gutter-x: 0.3rem;' data-masonry='{\"percentPosition\": true }'
        "{{dashboard}}" 
      "</div>"
    "</div>"
    "{{javascript}}";

const char HTTP_ENERGY[] PROGMEM = R"(
    
    <div class='container'>
    <h4>Tableau de bord énergétique
    </h4>
    <div class='row g-4'>
        <div class='col-md-12'>
            <div class='nav justify-content-end'>
              <div id='h'><a class='link' href='?time=hour' onClick=\"wait('h');\">
               <svg xmlns='http://www.w3.org/2000/svg' style='width:32px' width='24' height='24' fill='currentColor' class='bi bi-clock-history' viewBox='0 0 16 16'>
                  <path d='M8.515 1.019A7 7 0 0 0 8 1V0a8 8 0 0 1 .589.022zm2.004.45a7 7 0 0 0-.985-.299l.219-.976q.576.129 1.126.342zm1.37.71a7 7 0 0 0-.439-.27l.493-.87a8 8 0 0 1 .979.654l-.615.789a7 7 0 0 0-.418-.302zm1.834 1.79a7 7 0 0 0-.653-.796l.724-.69q.406.429.747.91zm.744 1.352a7 7 0 0 0-.214-.468l.893-.45a8 8 0 0 1 .45 1.088l-.95.313a7 7 0 0 0-.179-.483m.53 2.507a7 7 0 0 0-.1-1.025l.985-.17q.1.58.116 1.17zm-.131 1.538q.05-.254.081-.51l.993.123a8 8 0 0 1-.23 1.155l-.964-.267q.069-.247.12-.501m-.952 2.379q.276-.436.486-.908l.914.405q-.24.54-.555 1.038zm-.964 1.205q.183-.183.35-.378l.758.653a8 8 0 0 1-.401.432z'/>
                  <path d='M8 1a7 7 0 1 0 4.95 11.95l.707.707A8.001 8.001 0 1 1 8 0z'/>
                  <path d='M7.5 3a.5.5 0 0 1 .5.5v5.21l3.248 1.856a.5.5 0 0 1-.496.868l-3.5-2A.5.5 0 0 1 7 9V3.5a.5.5 0 0 1 .5-.5'/>
                </svg>
              </a></div>&nbsp;
              <div id='d'><a class='link' href='?time=day' onClick=\"wait('d');\">
                <svg xmlns='http://www.w3.org/2000/svg' style='width:32px' width='24' height='24' fill='currentColor' class='bi bi-calendar3-event' viewBox='0 0 16 16'>
                  <path d='M14 0H2a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2M1 3.857C1 3.384 1.448 3 2 3h12c.552 0 1 .384 1 .857v10.286c0 .473-.448.857-1 .857H2c-.552 0-1-.384-1-.857z'/>
                  <path d='M12 7a1 1 0 1 0 0-2 1 1 0 0 0 0 2'/>
                </svg>            
              </a></div>&nbsp;
              <div id='m'><a class='link' href='?time=month' onClick=\"wait('m');\">
                <svg xmlns='http://www.w3.org/2000/svg' style='width:32px' width='24' height='24' fill='currentColor' class='bi bi-calendar3' viewBox='0 0 16 16'>
                  <path d='M14 0H2a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2M1 3.857C1 3.384 1.448 3 2 3h12c.552 0 1 .384 1 .857v10.286c0 .473-.448.857-1 .857H2c-.552 0-1-.384-1-.857z'/>
                  <path d='M6.5 7a1 1 0 1 0 0-2 1 1 0 0 0 0 2m3 0a1 1 0 1 0 0-2 1 1 0 0 0 0 2m3 0a1 1 0 1 0 0-2 1 1 0 0 0 0 2m-9 3a1 1 0 1 0 0-2 1 1 0 0 0 0 2m3 0a1 1 0 1 0 0-2 1 1 0 0 0 0 2m3 0a1 1 0 1 0 0-2 1 1 0 0 0 0 2m3 0a1 1 0 1 0 0-2 1 1 0 0 0 0 2m-9 3a1 1 0 1 0 0-2 1 1 0 0 0 0 2m3 0a1 1 0 1 0 0-2 1 1 0 0 0 0 2m3 0a1 1 0 1 0 0-2 1 1 0 0 0 0 2'/>
                </svg>
              </a></div>&nbsp;
              <div id='y'><a class='link' href='?time=year' onClick=\"wait('y');\">
                <svg xmlns='http://www.w3.org/2000/svg' style='width:32px' width='24' height='24' fill='currentColor' class='bi bi-calendar3-fill' viewBox='0 0 16 16'>
                  <path d='M0 2a2 2 0 0 1 2-2h12a2 2 0 0 1 2 2zm0 1v11a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V3z'/>  <text x='8' y='11' font-size='6' text-anchor='middle' fill='white' font-family='Arial, sans-serif'>12</text>
                </svg>
              </a></div>&nbsp;
            </div>
         </div>
      </div>
    </div>
    )";
    
const char HTTP_TARIFF_CARD[] PROGMEM = R"rawstring(
<div class='row g-2 mb-3' id='tariff-section' style='display:none;'>
  <div class='col-12'>
    <div class='card p-3' id='tariff-card' style='background:#2980b9;color:white;'>
      <div class='row align-items-center'>
        <div class='col-auto'>
          <div class='d-flex align-items-center'>
            <svg xmlns='http://www.w3.org/2000/svg' style="width:40px;" width='28' height='28' fill='currentColor' class='me-2' viewBox='0 0 16 16'><path d='M11.251.068a.5.5 0 0 1 .227.58L9.677 6.5H13a.5.5 0 0 1 .364.843l-8 8.5a.5.5 0 0 1-.842-.49L6.323 9.5H3a.5.5 0 0 1-.364-.843l8-8.5a.5.5 0 0 1 .615-.09z'/></svg>
            <div style="width:400px;">
              <span style='opacity:0.8;font-size:0.85rem;'>Tarif en cours : </span>
              <span class='fw-bold' style='font-size:1.15rem;' id='tariff-name'>---</span>
            </div>
          </div>
        </div>
        <div class='col-auto ms-auto' id='tempo-section' style='display:none;'>
          <div class='d-flex gap-3'>
            <div class='text-center'>
              <small style='opacity:0.8;display:block;'>Aujourd'hui</small>
              <div class='tempo-badge tempo-undef' id='tempo-today'>?</div>
            </div>
            <div class='text-center'>
              <small style='opacity:0.8;display:block;'>Demain</small>
              <div class='tempo-badge tempo-undef' id='tempo-tomorrow'>-</div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</div>
)rawstring";


const char HTTP_ENERGY_LINKY[] PROGMEM = R"rawstring(
<style>.hi{display:inline-flex;align-items:center;justify-content:center;width:24px;height:24px;border:1.5px solid currentColor;border-radius:50%;font-size:14px;font-weight:bold;cursor:pointer;}</style>
<div class='col-sm-12'>
  {{LinkyStatus}}
</div>
</div>
<div class='container py-4' id='cadre_energy'>
 <div class='row g-2'  style='{{styleEnergyAlert}}'>
  <div class='col-12'>
    <span style='color:red;' id='energyAlert'>⚠️ {{energyAlertMessage}}</span>
  </div>
</div>
{{tariffCard}}
<div class='row g-2' >
  <div class='col-12'>
    <div class='card p-4' id='label-energy'>
      <h5 class='card-title'>Etiquette énergétique</h5>
      <div class='card-body p-0'>
        <div class="energy-bars-horizontal">
          <div class="energy-bar-horizontal A" data-class="A">
            <span class="bar-label-horizontal">A</span>
            <div class="cursor-horizontal"></div>
            <div class="value-display-horizontal"></div>
          </div>
          <div class="energy-bar-horizontal B" data-class="B">
            <span class="bar-label-horizontal">B</span>
            <div class="cursor-horizontal"></div>
            <div class="value-display-horizontal"></div>
          </div>
          <div class="energy-bar-horizontal C" data-class="C">
            <span class="bar-label-horizontal">C</span>
            <div class="cursor-horizontal"></div>
            <div class="value-display-horizontal"></div>
          </div>
          <div class="energy-bar-horizontal D" data-class="D">
            <span class="bar-label-horizontal">D</span>
            <div class="cursor-horizontal"></div>
            <div class="value-display-horizontal"></div>
          </div>
          <div class="energy-bar-horizontal E" data-class="E">
            <span class="bar-label-horizontal">E</span>
            <div class="cursor-horizontal"></div>
            <div class="value-display-horizontal"></div>
          </div>
          <div class="energy-bar-horizontal F" data-class="F">
            <span class="bar-label-horizontal">F</span>
            <div class="cursor-horizontal"></div>
            <div class="value-display-horizontal"></div>
          </div>
          <div class="energy-bar-horizontal G" data-class="G">
            <span class="bar-label-horizontal">G</span>
            <div class="cursor-horizontal"></div>
            <div class="value-display-horizontal"></div>
          </div>
        </div>
      </div>
      <a href='javascript:void(0)' onclick='showPopup("popupHelpEnergyLabel")' class='position-absolute bottom-0 begin-0 p-2 text-muted' title='Help'>
        <span class='hi'>?</span>
      </a>
      <a href='/configEnergy' class='position-absolute bottom-0 end-0 p-2 text-muted'
        title='Paramétrer la tarification'>
        <svg xmlns='http://www.w3.org/2000/svg' style='width:24px;' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' class='feather feather-settings'>
          <circle cx='12' cy='12' r='3'></circle>
          <path d='M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z'></path>
        </svg>
      </a>
    </div>
  </div>

  {{power_gauge}}

  <div class='col-lg-5 col-md-6 col-12'>
    <div class='card p-4' id='energyTrend' style='height:100%;'>
      <h5 class='card-title' style=''>Répartition énergétique</h5>
      <div class='row' style='font-size:12px;'>
        <div class='col-md-12 col-lg-6'>
          <div class='card-body position-relative p-1' style='min-height:240px;'>
            <div id='donut-chart' style='padding-top:40px;'></div>
          </div>
          <div align='center'>
            <a href='javascript:void(0)' onclick='loadDistributionChart("{{time}}","");' ><svg fill='#000000' style='width:24px;' width='24px' height='24px' viewBox='-3.2 -3.2 38.40 38.40' version='1.1' xmlns='http://www.w3.org/2000/svg' stroke='#000000'><g id='SVGRepo_bgCarrier' stroke-width='0'></g><g id='SVGRepo_tracerCarrier' stroke-linecap='round' stroke-linejoin='round' stroke='#CCCCCC' stroke-width='0.384'></g><g id='SVGRepo_iconCarrier'> <path d='M18.605 2.022v0zM18.605 2.022l-2.256 11.856 8.174 0.027-11.127 16.072 2.257-13.043-8.174-0.029zM18.606 0.023c-0.054 0-0.108 0.002-0.161 0.006-0.353 0.028-0.587 0.147-0.864 0.333-0.154 0.102-0.295 0.228-0.419 0.373-0.037 0.043-0.071 0.088-0.103 0.134l-11.207 14.832c-0.442 0.607-0.508 1.407-0.168 2.076s1.026 1.093 1.779 1.099l5.773 0.042-1.815 10.694c-0.172 0.919 0.318 1.835 1.18 2.204 0.257 0.11 0.527 0.163 0.793 0.163 0.629 0 1.145-0.294 1.533-0.825l11.22-16.072c0.442-0.607 0.507-1.408 0.168-2.076-0.34-0.669-1.026-1.093-1.779-1.098l-5.773-0.010 1.796-9.402c0.038-0.151 0.057-0.308 0.057-0.47 0-1.082-0.861-1.964-1.939-1.999-0.024-0.001-0.047-0.001-0.071-0.001v0z'></path> </g></svg></a> 
            <a href='javascript:void(0)' onclick='loadDistributionChart("{{time}}","euro");' ><svg style='width:24px;' width='24px' height='24px' viewBox='0 0 1024 1024' class='icon' version='1.1' xmlns='http://www.w3.org/2000/svg' fill='#000000'><g id='SVGRepo_bgCarrier' stroke-width='0'/><g id='SVGRepo_tracerCarrier' stroke-linecap='round' stroke-linejoin='round'/><g id='SVGRepo_iconCarrier'><path d='M951.87 253.86c0-82.18-110.05-144.14-256-144.14s-256 61.96-256 144.14c0 0.73 0.16 1.42 0.18 2.14h-0.18v109.71h73.14v-9.06c45.77 25.81 109.81 41.33 182.86 41.33 67.39 0 126.93-13.33 171.71-35.64 6.94 7.18 11.15 14.32 11.15 20.58 0 28.25-72.93 70.98-182.86 70.98h-73.12v73.14h73.12c67.4 0 126.96-13.33 171.74-35.65 6.95 7.17 11.11 14.31 11.11 20.6 0 28.27-72.93 71-182.86 71l-25.89 0.12c-15.91 0.14-31.32 0.29-46.34-0.11l-1.79 73.11c8.04 0.2 16.18 0.27 24.48 0.27 7.93 0 16-0.05 24.2-0.12l25.34-0.12c67.44 0 127.02-13.35 171.81-35.69 6.97 7.23 11.04 14.41 11.04 20.62 0 28.27-72.93 71-182.86 71h-73.12v73.14h73.12c67.44 0 127.01-13.35 171.81-35.69 6.98 7.22 11.05 14.4 11.05 20.62 0 28.27-72.93 71-182.86 71h-73.12v73.14h73.12c145.95 0 256-61.96 256-144.14 0-0.68-0.09-1.45-0.11-2.14h0.11V256h-0.18c0.03-0.72 0.2-1.42 0.2-2.14z m-438.86 0c0-28.27 72.93-71 182.86-71s182.86 42.73 182.86 71c0 28.25-72.93 70.98-182.86 70.98s-182.86-42.73-182.86-70.98z' fill='#000000'/><path d='M330.15 365.71c-145.95 0-256 61.96-256 144.14 0 0.73 0.16 1.42 0.18 2.14h-0.18v256c0 82.18 110.05 144.14 256 144.14s256-61.96 256-144.14V512h-0.18c0.02-0.72 0.18-1.42 0.18-2.14 0-82.18-110.05-144.15-256-144.15zM147.29 638.93c0-6.32 4.13-13.45 11.08-20.62 44.79 22.33 104.36 35.67 171.78 35.67 67.39 0 126.93-13.33 171.71-35.64 6.94 7.18 11.15 14.32 11.15 20.58 0 28.25-72.93 70.98-182.86 70.98s-182.86-42.72-182.86-70.97z m182.86-200.07c109.93 0 182.86 42.73 182.86 71 0 28.25-72.93 70.98-182.86 70.98s-182.86-42.73-182.86-70.98c0-28.27 72.93-71 182.86-71z m0 400.14c-109.93 0-182.86-42.73-182.86-71 0-6.29 4.17-13.43 11.11-20.6 44.79 22.32 104.34 35.66 171.75 35.66 67.4 0 126.96-13.33 171.74-35.65 6.95 7.17 11.11 14.31 11.11 20.6 0.01 28.26-72.92 70.99-182.85 70.99z' fill='#000000'/></g></svg></a>
          </div>
        </div>
        <div class='col-md-12 col-lg-6'>
          <div class='card-body position-relative p-1' style='height:270px;width:280px;margin-left:-10px;'>
            <div id='trend-datas'></div>
          </div>
        </div>
        
      </div>
      <a href='javascript:void(0)' onclick='showPopup("popupHelpEnergyDispatch")' class='position-absolute bottom-0 begin-0 p-2 text-muted' title='Help'>
        <span class='hi'>?</span>
      </a>
      <a href='/configEnergy' class='position-absolute bottom-0 end-0 p-2 text-muted'
        title='Paramétrer la tarification'>
        <svg xmlns='http://www.w3.org/2000/svg' style='width:24px;' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' class='feather feather-settings'>
          <circle cx='12' cy='12' r='3'></circle>
          <path d='M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z'></path>
        </svg>
      </a>
    </div>
  </div>
  <div class='col-lg-3 col-md-6 col-12'>
    <div class='card p-4' id='energyTrend' style='height:100%;'>
      <h5 class='card-title' style=''>Tendances</h5>
      <div class='card-body position-relative p-1' style='margin-left:-10px;padding-bottom:36px;'>
        <div id='power_trend'></div>
      </div>
      {{helpTrend}}
    </div>
  </div>
  <div class='col-md-6' style='display:{{stylePowerChart}}'>
    <div class='card p-4'>
      <h5 class='card-title'>Puissance apparente (graphique temps réel)</h5>
      <div class='card-body'>
          <canvas id='power-chart' height="342"></canvas>
      </div>

      <a href='javascript:void(0)' onclick='showPopup("popupHelpApparentPower")' class='position-absolute bottom-0 begin-0 p-2 text-muted' title='Help'>
        <span class='hi'>?</span>
      </a>
      <a href='/exportPowerChart?IEEE={{zlinkyIeee}}' download class='position-absolute bottom-0 end-0 p-2 text-muted' title='Exporter en CSV'>
        <svg xmlns='http://www.w3.org/2000/svg' style='width:22px;' width='22' height='22' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><path d='M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4'></path><polyline points='7 10 12 15 17 10'></polyline><line x1='12' y1='15' x2='12' y2='3'></line></svg>
      </a>
    </div>
  </div>
  <div class='col-md-6'>
    <div class='card p-4'>
      <h5 class='card-title'>Usage d'électricité</h5>
      <div class='card-body'>
          <canvas id='energy-chart' style='height: 342px;'></canvas>
      </div>
      <a href='javascript:void(0)' onclick='showPopup("popupHelpElectricity")' class='position-absolute bottom-0 begin-0 p-2 text-muted' title='Help'>
        <span class='hi'>?</span>
      </a>
      <a href='/exportEnergyChart?IEEE={{zlinkyIeee}}&time={{time}}' download class='position-absolute bottom-0 end-0 p-2 text-muted' title='Exporter en CSV'>
        <svg xmlns='http://www.w3.org/2000/svg' style='width:22px;' width='22' height='22' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><path d='M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4'></path><polyline points='7 10 12 15 17 10'></polyline><line x1='12' y1='15' x2='12' y2='3'></line></svg>
      </a>
    </div>
  </div>

  <!-- Popups d'aide externalis&eacute;s dans /web/help-energy.html (charg&eacute;s &agrave; la demande par showPopup) -->
  <div id='popupLinkyDatas' class='popup' onclick='hidePopup("popupLinkyDatas", event)'>
    <div class='popup-content'>
      <button class='close-btn' onclick='hidePopup("popupLinkyDatas")'>&times;</button>
      <h2 class='popup-header'>Donn&eacute;es Linky</h2>
      <div id='power_data'></div>
    </div>
  </div>

  <script>

    function getEnergyClass(value) {
        if (value < 50) return { class: 'A', range: '< 50' };
        if (value >= 51 && value <= 90) return { class: 'B', range: '51 - 90' };
        if (value >= 91 && value <= 150) return { class: 'C', range: '91 - 150' };
        if (value >= 151 && value <= 230) return { class: 'D', range: '151 - 230' };
        if (value >= 231 && value <= 330) return { class: 'E', range: '231 - 330' };
        if (value >= 331 && value <= 450) return { class: 'F', range: '331 - 450' };
        if (value >= 451) return { class: 'G', range: '> 451' };
    }

    function calculateEnergyClass(IEEE, time) {  
        var xhr = getXhr();
        xhr.onreadystatechange = function(){
            if(xhr.readyState == 4 ){
                if (xhr.responseText > 1) {
                    document.getElementById('label-energy').style.display = 'block';
                    const result = getEnergyClass(xhr.responseText);
                    displayResult(result.class, xhr.responseText, result.range);
                } else {
                    document.getElementById('label-energy').style.display = 'none';
                }
            }
        }
        xhr.open("GET", "loadTotalEnergy?IEEE=" + escape(IEEE) + "&time=" + escape(time), true);
        xhr.setRequestHeader('Content-Type', 'application/html');
        xhr.send();
    }

    function displayResult(energyClass, value, range) {
        // Cacher tous les curseurs et valeurs
        document.querySelectorAll('.cursor-horizontal').forEach(cursor => {
            cursor.classList.remove('active');
        });
        document.querySelectorAll('.value-display-horizontal').forEach(display => {
            display.classList.remove('active');
            display.textContent = '';
        });
        
        // Remettre toutes les barres à leur état normal
        document.querySelectorAll('.energy-bar-horizontal').forEach(bar => {
            bar.style.transform = 'translateY(0)';
            bar.style.zIndex = '1';
            bar.style.boxShadow = 'none';
            bar.style.border = '2px solid white';
            bar.style.animation = 'none';
        });
        
        // Trouver la barre correspondante et afficher le curseur
        const targetBar = document.querySelector(`[data-class="${energyClass}"]`);
        if (targetBar) {
            const cursor = targetBar.querySelector('.cursor-horizontal');
            const valueDisplay = targetBar.querySelector('.value-display-horizontal');
            
            cursor.classList.add('active');
            valueDisplay.classList.add('active');
            valueDisplay.textContent = `${value} kWh/m².an`;
            
            // Ajouter un effet de surbrillance très visible
            targetBar.style.transform = 'translateY(-12px) scale(1.05)';
            targetBar.style.zIndex = '10';
            targetBar.style.boxShadow = '0 15px 30px rgba(100,100,100,0.4), 0 0 0 2px #ffffff, 0 0 0 4px white';
            targetBar.style.border = '2px solid #ffFFFF';
            targetBar.style.borderRadius = '4px';
        }
    }

    // Fonctions popup ultra-simples
    function _showPopupEl(el) {
        // Un transform residuel sur body (laisse par le swipe) casse position:fixed et place
        // le popup hors ecran apres scroll sur mobile -> on le retire avant d'afficher.
        document.body.style.transform = '';
        el.classList.add('show');
        document.body.style.overflow = 'hidden';
    }
    function showPopup(id) {
        var el = document.getElementById(id);
        if (!el) {
            // Popups d'aide externalis&eacute;s : chargement &agrave; la demande (1er clic), puis mis en cache navigateur
            fetch('/web/help-energy.html').then(function(r){return r.text();}).then(function(t){
                var d = document.createElement('div'); d.innerHTML = t; document.body.appendChild(d);
                var e2 = document.getElementById(id);
                if (e2) _showPopupEl(e2);
            }).catch(function(){});
            return;
        }
        _showPopupEl(el);
    }

    function hidePopup(id, event) {
        // Si on clique sur l'overlay (pas sur le contenu)
        if (event && event.target !== event.currentTarget) {
            return;
        }
        
        document.getElementById(id).classList.remove('show');
        document.body.style.overflow = 'auto';
    }

    // Fermer avec Escape
    document.addEventListener('keydown', function(e) {
        if (e.key === 'Escape') {
            const openPopup = document.querySelector('.popup.show');
            if (openPopup) {
                hidePopup(openPopup.id);
            }
        }
    });

    // Empêcher la fermeture quand on clique dans le contenu
    document.addEventListener('DOMContentLoaded', function() {
        document.querySelectorAll('.popup-content').forEach(content => {
            content.addEventListener('click', function(e) {
                e.stopPropagation();
            });
        });
    });

  </script>
      )rawstring";

const char HTTP_NOTIFICATION[] PROGMEM = 
R"HTML(
    <style>
     
        .pagination-info {
            color: #666;
            font-size: 14px;
        }
        .notification-title {
            font-weight: bold;
            color: #333;
            margin-bottom: 5px;
        }
        
        .notification-message {
            color: #666;
            font-size: 0.9em;
        }
        
        .notification-date {
          font-size: 0.7em;
        }

        .type-badge {
            padding: 4px 12px;
            border-radius: 20px;
            font-size: 0.8em;
            font-weight: bold;
            color: white;
        }
        
        .type-0 { background: #17a2b8; }
        .type-1 { background: #ffc107; color: #333; }
        .type-2 { background: #dc3545; }
        
        .status-viewed { font-size: 0.8em;color: #28a745; }
        .status-unread { font-size: 0.8em;color: #dc3545; font-weight: bold; }
        
        .pagination {
            display: flex;
            justify-content: center;
            align-items: center;
            gap: 10px;
            margin-top: 30px;
            padding: 20px;
            border-radius: 10px;
        }
        
        .page-btn {
            padding: 8px 16px;
            border: 1px solid #ddd;
            background: white;
            border-radius: 5px;
            cursor: pointer;
            transition: all 0.3s ease;
        }
        
        .page-btn:hover:not(.active) {
            background: #e9ecef;
        }
        
        .page-btn.active {
            background: #3553d8ff;
            color: white;
            border-color: #3553d8ff;
        }
        
        .page-btn:disabled {
            opacity: 0.5;
            cursor: not-allowed;
        }
        
        .loading {
            text-align: center;
            padding: 50px;
            color: #666;
        }
        
        .error {
            background: #f8d7da;
            color: #721c24;
            padding: 15px;
            border-radius: 5px;
            margin: 20px 0;
            border-left: 4px solid #dc3545;
        }     
        .btn-small {
            padding: 6px 12px;
            font-size: 12px;
            border-radius: 15px;
        }

    </style>

    <div class='row p-4'>
      <h4 class='card-title mb-4'>Notifications</h4>
      <div class="d-flex justify-content-end">
        <button class="btn btn-primary mb-1 " onclick="markAllAsRead()" id="markAllBtn">
          <svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='currentColor' class='bi bi-binoculars' viewBox='0 0 16 16'>
            <path d='M3 2.5A1.5 1.5 0 0 1 4.5 1h1A1.5 1.5 0 0 1 7 2.5V5h2V2.5A1.5 1.5 0 0 1 10.5 1h1A1.5 1.5 0 0 1 13 2.5v2.382a.5.5 0 0 0 .276.447l.895.447A1.5 1.5 0 0 1 15 7.118V14.5a1.5 1.5 0 0 1-1.5 1.5h-3A1.5 1.5 0 0 1 9 14.5v-3a.5.5 0 0 1 .146-.354l.854-.853V9.5a.5.5 0 0 0-.5-.5h-3a.5.5 0 0 0-.5.5v.793l.854.853A.5.5 0 0 1 7 11.5v3A1.5 1.5 0 0 1 5.5 16h-3A1.5 1.5 0 0 1 1 14.5V7.118a1.5 1.5 0 0 1 .83-1.342l.894-.447A.5.5 0 0 0 3 4.882zM4.5 2a.5.5 0 0 0-.5.5V3h2v-.5a.5.5 0 0 0-.5-.5zM6 4H4v.882a1.5 1.5 0 0 1-.83 1.342l-.894.447A.5.5 0 0 0 2 7.118V13h4v-1.293l-.854-.853A.5.5 0 0 1 5 10.5v-1A1.5 1.5 0 0 1 6.5 8h3A1.5 1.5 0 0 1 11 9.5v1a.5.5 0 0 1-.146.354l-.854.853V13h4V7.118a.5.5 0 0 0-.276-.447l-.895-.447A1.5 1.5 0 0 1 12 4.882V4h-2v1.5a.5.5 0 0 1-.5.5h-3a.5.5 0 0 1-.5-.5zm4-1h2v-.5a.5.5 0 0 0-.5-.5h-1a.5.5 0 0 0-.5.5zm4 11h-4v.5a.5.5 0 0 0 .5.5h3a.5.5 0 0 0 .5-.5zm-8 0H2v.5a.5.5 0 0 0 .5.5h3a.5.5 0 0 0 .5-.5z'/>
          </svg>
            Tout vu
        </button> &nbsp;
        <button type="button" onclick="clearAllNotifications()" class="btn btn-danger mb-1">
          <svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" fill="currentColor" class="bi bi-trash" viewBox="0 0 16 16">
            <path d="M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z"></path>
            <path d="M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H6a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1h3.5a1 1 0 0 1 1 1zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z"></path>
            </svg>
            RAZ
        </button>
      </div>
    </div>
    <div class='row p-4'>
      <div class='card mx-auto shadow-sm' >
        <div class="card-body">
          <div class="content">
              <div id="loadingMessage" class="loading">
                  <p>📡 Chargement des notifications...</p>
              </div>
              
              <div id="errorMessage" class="error" style="display: none;"></div>
              
              <table class="table table-hover" id="notificationsTable" style="display: none;">
                  <thead>
                      <tr>
                          <th scope="col">Titre</th>
                          <th scope="col">Date/Heure</th>
                          <th scope="col">Statut</th>
                          <th scope="col">Actions</th>
                      </tr>
                  </thead>
                  <tbody id="notificationsBody">
                  </tbody>
              </table>
              <div class="controls">
                <div class="pagination-info" id="paginationInfo">
                    Chargement...
                </div>
              </div>
              <div class="pagination" id="pagination" style="display: none;">
              </div>
            </div>
        </div>
      </div>
    </div>

    <script>
        let currentPage = 1;
        const itemsPerPage = 10;
        let totalPages = 1;
        let totalItems = 0;

        // Charger les notifications au démarrage
        document.addEventListener('DOMContentLoaded', function() {
          
          loadNotifications(currentPage);
          updateMarkAllButton();
          setInterval(() => {
            loadNotifications(currentPage);
            updateMarkAllButton();
          }, 10000);
        });

        let isFirstLoad = true;
        let lastDataHash = '';
        async function loadNotifications(page = 1) {
            try {
                const isPageChange = (page !== currentPage);
                
                // Afficher le loading seulement au premier chargement ou changement de page
                if (isFirstLoad || isPageChange) {
                    document.getElementById('loadingMessage').style.display = 'block';
                    document.getElementById('notificationsTable').style.display = 'none';
                    document.getElementById('errorMessage').style.display = 'none';
                    document.getElementById('pagination').style.display = 'none';
                }
              
                const response = await fetch(`/api/notifications?page=${page}&limit=${itemsPerPage}`);
                const data = await response.json();
                
                // Créer un hash pour détecter les changements
                const newHash = JSON.stringify(data.notifications);
                
                // Ne mettre à jour que si les données ont changé
                if (newHash !== lastDataHash || isFirstLoad || isPageChange) {
                    lastDataHash = newHash;
                    
                    currentPage = page;
                    totalItems = data.total;
                    totalPages = Math.ceil(totalItems / itemsPerPage);

                    updatePaginationInfo(data);
                    displayNotifications(data.notifications);
                    createPagination();
                }

                document.getElementById('loadingMessage').style.display = 'none';
                document.getElementById('notificationsTable').style.display = 'table';
                document.getElementById('pagination').style.display = 'flex';
                
                isFirstLoad = false;

            } catch (error) {
                console.error('Erreur chargement notifications:', error);
                if (isFirstLoad) {
                    showError('Erreur lors du chargement des notifications');
                }
            }
        }

        // Mise à jour intelligente : ne modifie que les lignes changées
        function updateNotificationsTable(notifications) {
            const tbody = document.getElementById('notificationsBody');
            
            if (notifications.length === 0) {
                tbody.innerHTML = '<tr><td colspan="5" style="text-align: center; padding: 50px; color: #666;">📭 Aucune notification</td></tr>';
                return;
            }

            // Créer un map des lignes existantes par ID
            const existingRows = {};
            tbody.querySelectorAll('tr[data-notif-id]').forEach(row => {
                existingRows[row.dataset.notifId] = row;
            });

            const newIds = new Set();
            
            notifications.forEach((notif, index) => {
                const notifId = typeof notif.id !== 'undefined' ? notif.id : ((currentPage - 1) * itemsPerPage + index);
                newIds.add(String(notifId));
                
                const rowHtml = createNotificationRowHtml(notif, notifId);
                
                if (existingRows[notifId]) {
                    // Mettre à jour seulement si le contenu a changé
                    const existingRow = existingRows[notifId];
                    if (existingRow.dataset.hash !== hashNotif(notif)) {
                        existingRow.innerHTML = rowHtml;
                        existingRow.dataset.hash = hashNotif(notif);
                    }
                } else {
                    // Nouvelle notification : l'ajouter
                    const row = document.createElement('tr');
                    row.setAttribute('data-notif-id', notifId);
                    row.dataset.hash = hashNotif(notif);
                    row.innerHTML = rowHtml;
                    tbody.appendChild(row);
                }
            });
            
            // Supprimer les lignes qui n'existent plus
            tbody.querySelectorAll('tr[data-notif-id]').forEach(row => {
                if (!newIds.has(row.dataset.notifId)) {
                    row.remove();
                }
            });
            
            // Réordonner si nécessaire
            const rows = Array.from(tbody.querySelectorAll('tr[data-notif-id]'));
            notifications.forEach((notif, index) => {
                const notifId = typeof notif.id !== 'undefined' ? notif.id : ((currentPage - 1) * itemsPerPage + index);
                const row = tbody.querySelector(`tr[data-notif-id="${notifId}"]`);
                if (row && rows[index] !== row) {
                    tbody.insertBefore(row, rows[index]);
                }
            });
        }

        function hashNotif(notif) {
            return `${notif.viewed}-${notif.type}-${notif.title}-${notif.message}`;
        }

        function createNotificationRowHtml(notif, notifId) {
            const typeLabels = { 0: 'Info', 1: 'Attention', 2: 'Erreur' };
            return `
                <td>
                    <div class="notification-title">
                    <span class="type-badge type-${notif.type}">
                        ${typeLabels[notif.type] || 'Inconnu'}
                    </span>
                    &nbsp;${escapeHtml(notif.title)}
                    </div>
                    <div class="notification-message">${escapeHtml(notif.message)}</div>
                </td>
                <td class="notification-date">${escapeHtml(notif.timeStamp)}</td>
                <td>
                    <span class="${notif.viewed ? 'status-viewed' : 'status-unread'}">
                        ${notif.viewed ? '✅ Lu' : '🚨 Non lu'}
                    </span>
                </td>
                <td>
                    <div class="action-buttons">
                        ${!notif.viewed ? `<button class="btn btn-small success" onclick="markAsRead(${notifId})">...</button>` : ''}
                        <button class="btn btn-small danger" onclick="deleteNotification(${notifId})">...</button>
                    </div>
                </td>
            `;
        }

        function displayNotifications(notifications) {
            const tbody = document.getElementById('notificationsBody');
            tbody.innerHTML = '';

            if (notifications.length === 0) {
                tbody.innerHTML = '<tr><td colspan="5" style="text-align: center; padding: 50px; color: #666;">📭 Aucune notification</td></tr>';
                return;
            }

            notifications.forEach((notif, index) => {
                const row = document.createElement('tr');
                
                // Vérification et fallback pour l'ID
                const notifId = typeof notif.id !== 'undefined' ? notif.id : ((currentPage - 1) * itemsPerPage + index);
                
                const typeLabels = {
                    0: 'Info',
                    1: 'Attention',
                    2: 'Erreur'
                };

                row.innerHTML = `
                    <td>
                        <div class="notification-title">
                        <span class="type-badge type-${notif.type}">
                            ${typeLabels[notif.type] || 'Inconnu'}
                        </span>
                        &nbsp;${escapeHtml(notif.title)}
                        </div>
                        <div class="notification-message">${escapeHtml(notif.message)}</div>
                    </td>
                    <td class="notification-date">${escapeHtml(notif.timeStamp)}</td>
                    <td>
                        <span class="${notif.viewed ? 'status-viewed' : 'status-unread'}">
                            ${notif.viewed ? '✅ Lu' : '🚨 Non lu'}
                        </span>
                    </td>
                    <td>
                        <div class="action-buttons">
                            ${!notif.viewed ? `<button class="btn btn-small success" onclick="markAsRead(${notifId})">
                              <svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='#3553d8ff' class='bi bi-binoculars' viewBox='0 0 16 16'>
                                <path d='M3 2.5A1.5 1.5 0 0 1 4.5 1h1A1.5 1.5 0 0 1 7 2.5V5h2V2.5A1.5 1.5 0 0 1 10.5 1h1A1.5 1.5 0 0 1 13 2.5v2.382a.5.5 0 0 0 .276.447l.895.447A1.5 1.5 0 0 1 15 7.118V14.5a1.5 1.5 0 0 1-1.5 1.5h-3A1.5 1.5 0 0 1 9 14.5v-3a.5.5 0 0 1 .146-.354l.854-.853V9.5a.5.5 0 0 0-.5-.5h-3a.5.5 0 0 0-.5.5v.793l.854.853A.5.5 0 0 1 7 11.5v3A1.5 1.5 0 0 1 5.5 16h-3A1.5 1.5 0 0 1 1 14.5V7.118a1.5 1.5 0 0 1 .83-1.342l.894-.447A.5.5 0 0 0 3 4.882zM4.5 2a.5.5 0 0 0-.5.5V3h2v-.5a.5.5 0 0 0-.5-.5zM6 4H4v.882a1.5 1.5 0 0 1-.83 1.342l-.894.447A.5.5 0 0 0 2 7.118V13h4v-1.293l-.854-.853A.5.5 0 0 1 5 10.5v-1A1.5 1.5 0 0 1 6.5 8h3A1.5 1.5 0 0 1 11 9.5v1a.5.5 0 0 1-.146.354l-.854.853V13h4V7.118a.5.5 0 0 0-.276-.447l-.895-.447A1.5 1.5 0 0 1 12 4.882V4h-2v1.5a.5.5 0 0 1-.5.5h-3a.5.5 0 0 1-.5-.5zm4-1h2v-.5a.5.5 0 0 0-.5-.5h-1a.5.5 0 0 0-.5.5zm4 11h-4v.5a.5.5 0 0 0 .5.5h3a.5.5 0 0 0 .5-.5zm-8 0H2v.5a.5.5 0 0 0 .5.5h3a.5.5 0 0 0 .5-.5z'/>
                              </svg>
                             </button>` : ''}
                            <button class="btn btn-small danger" onclick="deleteNotification(${notifId})">
                            <svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='#dc3545' class='bi bi-trash' viewBox='0 0 16 16'>
                              <path d='M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z'/>
                              <path d='M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H6a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1h3.5a1 1 0 0 1 1 1zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z'/>
                            </svg>
                            </button>
                        </div>
                    </td>
                `;
                
                tbody.appendChild(row);
            });
        }

        function updatePaginationInfo(data) {
            const start = data.offset + 1;
            const end = Math.min(data.offset + data.notifications.length, data.total);
            document.getElementById('paginationInfo').textContent = 
                `Affichage ${start}-${end} sur ${data.total} notifications`;
        }

        function createPagination() {
            const pagination = document.getElementById('pagination');
            pagination.innerHTML = '';

            // Bouton précédent
            const prevBtn = document.createElement('button');
            prevBtn.textContent = '◀ Précédent';
            prevBtn.className = 'page-btn';
            prevBtn.disabled = currentPage <= 1;
            prevBtn.onclick = () => currentPage > 1 && loadNotifications(currentPage - 1);
            pagination.appendChild(prevBtn);

            // Numéros de pages
            const startPage = Math.max(1, currentPage - 2);
            const endPage = Math.min(totalPages, currentPage + 2);

            if (startPage > 1) {
                addPageButton(1);
                if (startPage > 2) {
                    const dots = document.createElement('span');
                    dots.textContent = '...';
                    dots.style.padding = '8px';
                    pagination.appendChild(dots);
                }
            }

            for (let i = startPage; i <= endPage; i++) {
                addPageButton(i);
            }

            if (endPage < totalPages) {
                if (endPage < totalPages - 1) {
                    const dots = document.createElement('span');
                    dots.textContent = '...';
                    dots.style.padding = '8px';
                    pagination.appendChild(dots);
                }
                addPageButton(totalPages);
            }

            // Bouton suivant
            const nextBtn = document.createElement('button');
            nextBtn.textContent = 'Suivant ▶';
            nextBtn.className = 'page-btn';
            nextBtn.disabled = currentPage >= totalPages;
            nextBtn.onclick = () => currentPage < totalPages && loadNotifications(currentPage + 1);
            pagination.appendChild(nextBtn);
        }

        function addPageButton(pageNum) {
            const pagination = document.getElementById('pagination');
            const btn = document.createElement('button');
            btn.textContent = pageNum;
            btn.className = 'page-btn' + (pageNum === currentPage ? ' active' : '');
            btn.onclick = () => loadNotifications(pageNum);
            pagination.appendChild(btn);
        }

        async function markAsRead(index) {
            try {
                const response = await fetch(`/api/notifications/read?id=${index}`, {
                    method: 'PUT'
                });

                if (response.ok) {
                    
                    await loadNotifications(currentPage);
                } else {
                    const errorData = await response.json();
                    showError(errorData.error || 'Erreur lors du marquage comme lu');
                }
            } catch (error) {
                console.error('Erreur mark as read:', error);
                showError('Erreur lors du marquage comme lu');
            }
        }

        async function markAllAsRead() {
            try {
                const markAllBtn = document.getElementById('markAllBtn');
                const originalText = markAllBtn.textContent;
                
                // Indicateur de chargement
                markAllBtn.textContent = '⏳ Marquage...';
                markAllBtn.disabled = true;

                const response = await fetch('/api/notifications/read-all', {
                    method: 'PUT'
                });

                if (response.ok) {
                    const result = await response.json();
                    console.log('Toutes les notifications marquées comme lues:', result);
                    
                    // Feedback visuel temporaire
                    markAllBtn.textContent = '✅ Terminé !';
                    markAllBtn.classList.add('success');
                    
                    // Actualiser les données
                    await loadNotifications(currentPage);
                    
                    // Restaurer le bouton après 2 secondes
                    setTimeout(() => {
                        markAllBtn.textContent = originalText;
                        markAllBtn.disabled = false;
                        markAllBtn.classList.remove('success');
                        updateMarkAllButton(); // Met à jour l'état du bouton
                    }, 2000);
                    
                } else {
                    const errorData = await response.json();
                    showError(errorData.error || 'Erreur lors du marquage');
                    
                    // Restaurer le bouton
                    markAllBtn.textContent = originalText;
                    markAllBtn.disabled = false;
                }
            } catch (error) {
                console.error('Erreur mark all as read:', error);
                showError('Erreur lors du marquage de toutes les notifications');
                
                // Restaurer le bouton
                const markAllBtn = document.getElementById('markAllBtn');
                markAllBtn.textContent = 'Tout lu';
                markAllBtn.disabled = false;
            }
        }

        // Fonction pour mettre à jour l'état du bouton "Tout marquer lu"
        async function updateMarkAllButton() {
            try {
                const response = await fetch('/api/stats');
                if (response.ok) {
                    const stats = await response.json();
                    const markAllBtn = document.getElementById('markAllBtn');
                    
                    if (stats.unread > 0) {
                        markAllBtn.disabled = false;
                        markAllBtn.style.opacity = '1';
                        markAllBtn.setAttribute('title', `Marquer ${stats.unread} notification(s) comme lues`);
                        markAllBtn.textContent = `Tout vu (${stats.unread})`;
                    } else {
                        markAllBtn.disabled = true;
                        markAllBtn.style.opacity = '0.5';
                        markAllBtn.setAttribute('title', 'Aucune notification non lue');
                        markAllBtn.textContent = 'Tout vu';
                    }
                }
            } catch (error) {
                console.error('Erreur lors de la mise à jour du bouton:', error);
            }
        }

        async function deleteNotification(index) {
            if (!confirm('Êtes-vous sûr de vouloir supprimer cette notification ?')) {
                return;
            }

            try {
                const response = await fetch(`/api/notifications/delete?id=${index}`, {
                    method: 'DELETE'
                });

                if (response.ok) {
                    // Si on supprime le dernier élément de la page et qu'on n'est pas sur la première page,
                    // revenir à la page précédente
                    const newTotalPages = Math.ceil((totalItems - 1) / itemsPerPage);
                    if (currentPage > newTotalPages && currentPage > 1) {
                        await loadNotifications(currentPage - 1);
                    } else {
                        await loadNotifications(currentPage);
                    }
                } else {
                    const errorData = await response.json();
                    showError(errorData.error || 'Erreur lors de la suppression');
                }
            } catch (error) {
                console.error('Erreur delete:', error);
                showError('Erreur lors de la suppression');
            }
        }

        async function clearAllNotifications() {
            if (!confirm('Êtes-vous sûr de vouloir supprimer TOUTES les notifications ?')) {
                return;
            }

            try {
                const response = await fetch('/api/notifications/clear', {
                    method: 'DELETE'
                });

                if (response.ok) {
                    currentPage = 1;
                    await loadNotifications(currentPage);
                } else {
                    showError('Erreur lors de la suppression');
                }
            } catch (error) {
                console.error('Erreur clear all:', error);
                showError('Erreur lors de la suppression');
            }
        }

        function showError(message) {
            const errorDiv = document.getElementById('errorMessage');
            errorDiv.textContent = message;
            errorDiv.style.display = 'block';
            document.getElementById('loadingMessage').style.display = 'none';
            
            setTimeout(() => {
                errorDiv.style.display = 'none';
            }, 5000);
        }

        function escapeHtml(text) {
            const map = {
                '&': '&amp;',
                '<': '&lt;',
                '>': '&gt;',
                '"': '&quot;',
                "'": '&#039;'
            };
            return text.replace(/[&<>"']/g, function(m) { return map[m]; });
        }

    </script>
)HTML";

/*const char HTTP_ENERGY_LINKY[] PROGMEM =
    
      "<div class='col-sm-12'>"
        "{{LinkyStatus}}"
      "</div>"
    "</div>"
    "<div class='container py-4' id='cadre_energy'>"
     "<div class='row g-2'  style='{{styleEnergyAlert}}'>"
      "<div class='col-12'>"
        "<span style='color:red;' id='energyAlert'>⚠️ {{energyAlertMessage}}</span>"
      "</div>"
    "</div>"
    "<div class='row g-2' style=''>"
      "{{power_gauge}}"
      "<div class='col-lg-8 col-md-12 col-12'>"
        "<div class='card p-4' id='energyTrend' style='height:100%;'>"
          "<h5 class='card-title' style=''>Tendance</h5>"
          
          "<div class='row'>"
            "<div class='col-md-12 col-lg-4'>"
              "<div class='card-body position-relative p-1' style='min-height:270px;min-width:240px;margin-left:-10px;'>"
                "<div id='power_trend'></div>"
              "</div>"
            "</div>"
            "<div class='col-md-12 col-lg-4'>"
              "<div class='card-body position-relative p-1' style='min-height:240px;'>"
                "<div id='donut-chart' style='padding-top:40px;'></div>"
              "</div>"
              "<div align='center'>"
                "<a onclick='loadDistributionChart(\"{{time}}\",\"\");' ><svg fill='#000000' style='width:24px;' width='24px' height='24px' viewBox='-3.2 -3.2 38.40 38.40' version='1.1' xmlns='http://www.w3.org/2000/svg' stroke='#000000'><g id='SVGRepo_bgCarrier' stroke-width='0'></g><g id='SVGRepo_tracerCarrier' stroke-linecap='round' stroke-linejoin='round' stroke='#CCCCCC' stroke-width='0.384'></g><g id='SVGRepo_iconCarrier'> <path d='M18.605 2.022v0zM18.605 2.022l-2.256 11.856 8.174 0.027-11.127 16.072 2.257-13.043-8.174-0.029zM18.606 0.023c-0.054 0-0.108 0.002-0.161 0.006-0.353 0.028-0.587 0.147-0.864 0.333-0.154 0.102-0.295 0.228-0.419 0.373-0.037 0.043-0.071 0.088-0.103 0.134l-11.207 14.832c-0.442 0.607-0.508 1.407-0.168 2.076s1.026 1.093 1.779 1.099l5.773 0.042-1.815 10.694c-0.172 0.919 0.318 1.835 1.18 2.204 0.257 0.11 0.527 0.163 0.793 0.163 0.629 0 1.145-0.294 1.533-0.825l11.22-16.072c0.442-0.607 0.507-1.408 0.168-2.076-0.34-0.669-1.026-1.093-1.779-1.098l-5.773-0.010 1.796-9.402c0.038-0.151 0.057-0.308 0.057-0.47 0-1.082-0.861-1.964-1.939-1.999-0.024-0.001-0.047-0.001-0.071-0.001v0z'></path> </g></svg></a> "
                "<a onclick='loadDistributionChart(\"{{time}}\",\"euro\");' ><svg style='width:24px;' width='24px' height='24px' viewBox='0 0 1024 1024' class='icon' version='1.1' xmlns='http://www.w3.org/2000/svg' fill='#000000'><g id='SVGRepo_bgCarrier' stroke-width='0'/><g id='SVGRepo_tracerCarrier' stroke-linecap='round' stroke-linejoin='round'/><g id='SVGRepo_iconCarrier'><path d='M951.87 253.86c0-82.18-110.05-144.14-256-144.14s-256 61.96-256 144.14c0 0.73 0.16 1.42 0.18 2.14h-0.18v109.71h73.14v-9.06c45.77 25.81 109.81 41.33 182.86 41.33 67.39 0 126.93-13.33 171.71-35.64 6.94 7.18 11.15 14.32 11.15 20.58 0 28.25-72.93 70.98-182.86 70.98h-73.12v73.14h73.12c67.4 0 126.96-13.33 171.74-35.65 6.95 7.17 11.11 14.31 11.11 20.6 0 28.27-72.93 71-182.86 71l-25.89 0.12c-15.91 0.14-31.32 0.29-46.34-0.11l-1.79 73.11c8.04 0.2 16.18 0.27 24.48 0.27 7.93 0 16-0.05 24.2-0.12l25.34-0.12c67.44 0 127.02-13.35 171.81-35.69 6.97 7.23 11.04 14.41 11.04 20.62 0 28.27-72.93 71-182.86 71h-73.12v73.14h73.12c67.44 0 127.01-13.35 171.81-35.69 6.98 7.22 11.05 14.4 11.05 20.62 0 28.27-72.93 71-182.86 71h-73.12v73.14h73.12c145.95 0 256-61.96 256-144.14 0-0.68-0.09-1.45-0.11-2.14h0.11V256h-0.18c0.03-0.72 0.2-1.42 0.2-2.14z m-438.86 0c0-28.27 72.93-71 182.86-71s182.86 42.73 182.86 71c0 28.25-72.93 70.98-182.86 70.98s-182.86-42.73-182.86-70.98z' fill='#000000'/><path d='M330.15 365.71c-145.95 0-256 61.96-256 144.14 0 0.73 0.16 1.42 0.18 2.14h-0.18v256c0 82.18 110.05 144.14 256 144.14s256-61.96 256-144.14V512h-0.18c0.02-0.72 0.18-1.42 0.18-2.14 0-82.18-110.05-144.15-256-144.15zM147.29 638.93c0-6.32 4.13-13.45 11.08-20.62 44.79 22.33 104.36 35.67 171.78 35.67 67.39 0 126.93-13.33 171.71-35.64 6.94 7.18 11.15 14.32 11.15 20.58 0 28.25-72.93 70.98-182.86 70.98s-182.86-42.72-182.86-70.97z m182.86-200.07c109.93 0 182.86 42.73 182.86 71 0 28.25-72.93 70.98-182.86 70.98s-182.86-42.73-182.86-70.98c0-28.27 72.93-71 182.86-71z m0 400.14c-109.93 0-182.86-42.73-182.86-71 0-6.29 4.17-13.43 11.11-20.6 44.79 22.32 104.34 35.66 171.75 35.66 67.4 0 126.96-13.33 171.74-35.65 6.95 7.17 11.11 14.31 11.11 20.6 0.01 28.26-72.92 70.99-182.85 70.99z' fill='#000000'/></g></svg></a>"
              "</div>"
            "</div>"
            "<div class='col-md-12 col-lg-4'>"
              "<div class='card-body position-relative p-1' style='height:270px;width:280px;margin-left:-10px;'>"
                "<div id='trend-datas'></div>"
              "</div>"
            "</div>"
          "</div>"
          "<a href='/configEnergy' class='position-absolute bottom-0 end-0 p-2 text-muted'" 
            "title='Paramétrer la tarification'>"
            "<svg xmlns='http://www.w3.org/2000/svg' style='width:24px;' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' class='feather feather-settings'>"
              "<circle cx='12' cy='12' r='3'></circle>"
              "<path d='M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z'></path>"
            "</svg>"
          "</a>"
        "</div>"
      "</div>"
      "<div class='col-md-12'>"
        "<div class='card p-4'>"
          "<h5 class='card-title' style=''>Données Linky</h5>"
          "<div class='card-body' style='min-height:270px;'>"
            "<div id='power_data'></div>"
          "</div>"
        "</div>"
      "</div>"   
      "<div class='col-md-6' style='display:{{stylePowerChart}}'>"
        "<div class='card p-4'>"
          "<h5 class='card-title'>Puissance apparente (graphique temps réel)</h5>"
          "<div class='card-body'>"
              "<div id='power-chart'></div>"
          "</div>"
        "</div>"
      "</div>"
      "<div class='col-md-6'>"
        "<div class='card p-4'>"
          "<h5 class='card-title'>Consommation d'électricité</h5>"
          "<div class='card-body'>"
              "<div id='energy-chart'></div>"
          "</div>"
        "</div>"
      "</div>"
;*/

const char HTTP_ENERGY_GAZ[] PROGMEM =

    //"<br><div class='row g-4'>"
    "<div class='col-md-6'>"
        "<div class='card p-4'>"
          "<h5 class='card-title'>Consommation de Gaz</h5>"
          "<div class='card-body'>"
              "<canvas id='gaz-chart' style='height: 342px;'></canvas>"
          "</div>"
        "</div>"
      "</div>"
    //"</div>"
;

const char HTTP_ENERGY_WATER[] PROGMEM =
    //"<br><div class='row g-4'>"
    "<div class='col-md-6'>"
        "<div class='card p-4'>"
          "<h5 class='card-title'>Consommation d'eau</h5>"
          "<div class='card-body'>"
              "<canvas id='water-chart' style='height: 342px;'></canvas>"
          "</div>"
        "</div>"
      "</div>";
    //"</div>";
  
const char HTTP_ENERGY_JAVASCRIPT[] PROGMEM =
    "{{javascript}}";

const char HTTP_CONFIG_WIFI[] PROGMEM = R"(
  <div class="container p-4">
    <h4 class="card-title mb-4">Config WiFi</h4>
    <div class="card mx-auto shadow-sm">
      <div class="card-body">
        <form method='POST' action='saveWifi'>
          <!-- SSID -->
          <div class="mb-3">
            <label for="ssid" class="form-label">SSID</label>
            <div class="input-group">
              <input type="text" class="form-control" id="ssid" name='WIFISSID' placeholder="SSID" value='{{ssid}}' style='{{ssidborder}}'>
              <button class="btn btn-primary" type="button" id="scanBtn" onclick='scanNetwork(-1);'>Scan</button>
            </div>
            <div id='networks'></div>
          </div>
          <!-- Password -->
          <div class="mb-3">
            <label for="password" class="form-label">Mot de passe</label>
            <input type="password" class="form-control" id="password"  name='WIFIpassword' value='{{password}}' style='{{passborder}}'>
          </div>
          <!-- DHCP Toggle -->
          <div class="form-check form-switch mb-3">
            <input class="form-check-input" type="checkbox" id="dhcpSwitch" name='DHCPEnable' {{checkedDHCP}} onClick="toggleDiv('static');">
            <label class="form-check-label" for="dhcpSwitch">DHCP</label>
          </div>
          <div id='static' style='display:{{static}}'>
            <div id="staticFields">
              <div class="mb-3">
                <label for="ip" class="form-label">@IP</label>
                <input type="text" class="form-control mb-2" id="ip" name='ipAddress' value='{{ip}}' style='{{ipborder}}'>
                <label for="mask" class="form-label">@Masque</label>
                <input type="text" class="form-control mb-2" id="mask" name='ipMask' value='{{mask}}' style='{{ipmask}}'>
                <label for="gateway" class="form-label">@Passerelle</label>
                <input type="text" class="form-control" id="gateway"  name='ipGW' value='{{gw}}' style='{{ipgw}}'>
              </div>
            </div>
          </div>
          <!-- Save Button -->
          <div class="d-flex justify-content-end">
            <button type="submit" class="btn btn-warning btn-lg" onclick='document.getElementById("reboot").style.display="block";'>Enregistrer</button>
          </div>
        </form>
        <div style='color:red'>{{error}}</div>
        <div style='color:red'>{{ipError}}</div>
        <div id='reboot' style='display:none;'><img src='web/img/wait.gif' /> Redémarrage ...</div>
      </div>
    </div>
  </div>
  )";

const char HTTP_CREATE_DEVICE[] PROGMEM =
    "<h4>Create device file</h4>"
    "<div class='row justify-content-md-center' >"
    "<div class='col col-md-6'><form method='POST' action='saveFileDevice'>"
    "<div class='form-group'>"
    "<label for='filename'>@ mac</label>"
    "<input class='form-control' id='filename' type='text' name='filename' value=''> "
    "</div>"
    "<div class='form-group'>"
    " <label for='file'>Content</label>"
    " <textarea class='form-control' id='file' name='file' rows='20'>"
    "</textarea>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2' name='save' value='save'>Enregistrer</button>"
    "</form>";
const char HTTP_CREATE_HISTORY[] PROGMEM =
    "<h4>Create history file</h4>"
    "<div class='row justify-content-md-center' >"
    "<div class='col col-md-6'><form method='POST' action='saveFileHistory'>"
    "<div class='form-group'>"
    "<label for='filename'>@ mac</label>"
    "<input class='form-control' id='filename' type='text' name='filename' value=''> "
    "</div>"
    "<div class='form-group'>"
    " <label for='file'>Content</label>"
    " <textarea class='form-control' id='file' name='file' rows='20'>"
    "</textarea>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2' name='save' value='save'>Enregistrer</button>"
    "</form>";
const char HTTP_CREATE_TEMPLATE[] PROGMEM =
    "<style>"
    ".create-container{max-width:900px;margin:0 auto;padding:20px;}"
    ".create-card{background:#fff;border-radius:12px;box-shadow:0 2px 10px rgba(0,0,0,0.08);overflow:hidden;}"
    ".create-header{background:#f8f9fa;padding:20px;border-bottom:1px solid #dee2e6;}"
    ".create-header h4{margin:0;display:flex;align-items:center;gap:10px;}"
    ".create-body{padding:20px;}"
    ".form-label{font-weight:500;margin-bottom:8px;}"
    "#file{font-family:'Consolas','Monaco',monospace;font-size:13px;background:#1e1e1e;color:#d4d4d4;border-radius:8px;}"
    ".validation-msg{padding:10px;border-radius:6px;margin-top:10px;}"
    ".validation-msg.valid{background:#d1e7dd;color:#0f5132;}"
    ".validation-msg.invalid{background:#f8d7da;color:#842029;}"
    ".btn-group-actions{display:flex;gap:10px;margin-top:20px;}"
    "</style>"
    "<div class='create-container'>"
    "<div class='create-card'>"
    "<div class='create-header'>"
    "<h4><svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='currentColor' viewBox='0 0 16 16'><path d='M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4z'/></svg> Nouveau Template</h4>"
    "</div>"
    "<div class='create-body'>"
    "<form id='createForm'>"
    "<div class='mb-3'>"
    "<label class='form-label' for='filename'>Nom du fichier</label>"
    "<input class='form-control' id='filename' type='text' name='filename' placeholder='Ex: 123.json' required>"
    "<div class='form-text'>Le fichier sera enregistré dans /tp/</div>"
    "</div>"
    "<div class='mb-3'>"
    "<label class='form-label' for='file'>Contenu JSON</label>"
    "<textarea class='form-control' id='file' name='file' rows='18' spellcheck='false'>"
    "{\\n  \"default\": [\\n    {\\n      \"status\": [],\\n      \"action\": [],\\n      \"bind\": \"\",\\n      \"report\": []\\n    }\\n  ]\\n}"
    "</textarea>"
    "<div id='validationMsg' class='validation-msg valid'>✓ JSON valide</div>"
    "</div>"
    "<div class='btn-group-actions'>"
    "<button type='button' class='btn btn-primary' onclick='saveNewTemplate()'>Créer le template</button>"
    "<a href='/configTemplates' class='btn btn-outline-secondary'>Annuler</a>"
    "</div>"
    "</form>"
    "</div></div></div>"
    "<script>"
    "$('#file').on('input',validateJson);"
    "function validateJson(){"
    "try{JSON.parse($('#file').val());"
    "$('#validationMsg').removeClass('invalid').addClass('valid').html('✓ JSON valide');return true;"
    "}catch(e){$('#validationMsg').removeClass('valid').addClass('invalid').html('✗ '+e.message);return false;}}"
    "function _isTunnel(){return window.location.hostname.indexOf('lixee-box.fr')>=0;}"
    "async function _saveChunked(fn,ct){"
    "var C=8192,r=await fetch('/templateSaveInit?file='+encodeURIComponent(fn),{method:'POST'});"
    "if(!r.ok)throw new Error('init:'+r.status);"
    "for(var i=0;i<ct.length;i+=C){"
    "r=await fetch('/templateSaveChunk',{method:'POST',headers:{'Content-Type':'text/plain'},body:ct.substring(i,i+C)});"
    "if(!r.ok)throw new Error('chunk:'+r.status);}"
    "r=await fetch('/templateSaveFinish',{method:'POST'});"
    "if(!r.ok){var e=await r.json().catch(function(){return{error:r.status}});throw new Error(e.error||r.status);}"
    "return r.json();}"
    "function saveNewTemplate(){"
    "var filename=$('#filename').val();"
    "if(!filename){alert('Entrez un nom de fichier');return;}"
    "if(!filename.endsWith('.json'))filename+='.json';"
    "if(!validateJson()){alert('Corrigez les erreurs JSON');return;}"
    "var content=$('#file').val();"
    "if(_isTunnel()){"
    "_saveChunked(filename,content).then(function(){window.location='/configTemplates';}).catch(function(e){alert('Erreur: '+e.message);});"
    "}else{"
    "$.ajax({url:'saveFileTemplates',type:'POST',data:{0:filename,1:content,2:'save'},dataType:'json',"
    "success:function(){window.location='/configTemplates';},"
    "error:function(xhr){alert('Erreur: '+(xhr.responseJSON||{}).error||'Erreur inconnue');}});}}"
    "</script>";


const char HTTP_DEVICE[] PROGMEM =
    "<div class='col-sm-3'><div class='card'><div class='card-header'>Socket num : {{numero}}"
    "</div>"
    "<div class='card-body'>"
    "<strong>ID : {{id}}</strong>"
    "</br><strong>IP : {{ip}}</strong>"
    "</br><strong>MAC : {{mac}}</strong>"
    "<br>"
    "<div id='{{mac}}'>"
    "<img src='web/img/wait.gif' />"
    "</div>"
    "</div></div></div>";

const char HTTP_FOOTER[] PROGMEM = R"(
    <script type='text/javascript' src='web/js/bootstrap.min.js'></script>
    <script language='javascript'>
      function getReleaseInfo() {
        $.getJSON("https://api.github.com/repos/fairecasoimeme/LiXee-Gateway/releases/latest").done(function(release) {          
          var version = release.tag_name;
          if (version == "{{version}}")
          {
            $(".AboutMaj").hide();
          }else{
            $(".AboutMaj").css('display', 'inline-block');
          }
        });
      }

      async function showAlertNotification(){
        const hasUnread = await hasUnreadNotifications();
        if (hasUnread) {
          $(".AlertNotif").css('display', 'inline-block');
        }else{
          $(".AlertNotif").hide();
        }
      }

      function showTools(val){
        if (val) {
          $('#Tools').show();
        }else{
          $('#Tools').hide();
        }
      }

      async function hasUnreadNotifications() {
          try {
              const response = await fetch('/api/stats');
              if (!response.ok) {
                  console.error('Erreur API stats:', response.status);
                  return -1;
              }
              
              const stats = await response.json();
              return stats.unread || 0;
              
          } catch (error) {
            console.error('Erreur lors de la vérification des notifications:', error);
            return -1;
          }

      }
    showAlertNotification();
    setInterval(() => {
            showAlertNotification();
          }, 10000);      
   
    showTools({{value}});
    getReleaseInfo();
    getFormattedDate();
    getAlert();
    </script>
    )";


const char HTTP_FOOTER_ASSIST[] PROGMEM = R"(
    <script type='text/javascript' src='web/js/bootstrap.min.js'></script>
    <script language='javascript'>
    getFormattedDate();
    </script>
    )";

const char HTTP_ASSIST_DEVICE[] PROGMEM = R"(

<div class="container py-5">
      <div class="row justify-content-center">
        <div class="col-md-8 col-lg-6">
          <div class="card shadow-lg">
            <div class="card-body">
              <!-- Barre de progression -->
              <div class="progress mb-4" style="height:0.5rem;">
                <div id="progressBar" class="progress-bar bg-primary" role="progressbar" style="width:25%"></div>
              </div>

              <!-- Titre & icône -->
              <div class="d-flex align-items-center mb-3">
                <svg id="icon" fill="#0f70b7" style='width:48px;' width='32' height='32' viewBox='0 0 24 24' role='img' xmlns='http://www.w3.org/2000/svg'></svg>
                <h5 class="mb-0" id="stepTitle">Mettre la passerelle en mode appairage</h5>
              </div>

              <!-- Description -->
              <p id="stepDesc" class="mb-4">Assurez-vous que votre passerelle Zigbee est sous tension puis cliquez sur Démarrer l'appairage.</p>

              <!-- Zone dynamique -->
              <div id="dynamicZone"></div>

              <!-- Actions -->
              <div class="d-flex justify-content-around pt-5">
                <button id="prevBtn" class="btn btn-outline-secondary" style="display:none;" >Précédent</button>
                <button id="nextBtn" class="btn btn-primary">Suivant</button>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <script>
      const steps = [
        {
          title: "Alimenter l'appareil",
          desc: "Allumez ou branchez l'appareil à associer. Référez-vous à la notice pour le passer en mode appairage (ex. appui 5s sur le bouton).",
          icon: '<path d="M11.988 0a11.85 11.85 0 00-8.617 3.696c7.02-.875 11.401-.583 13.289-.34 3.752.583 3.558 3.404 3.558 3.404L8.237 19.112c2.299.22 6.897.366 13.796-.631a11.86 11.86 0 001.912-6.469C23.945 5.374 18.595 0 11.988 0zm.232 4.31c-2.451-.014-5.772.146-9.963.723C.854 7.003.055 9.41.055 12.012.055 18.626 5.38 24 11.988 24c3.63 0 6.85-1.63 9.053-4.182-7.286.948-11.813.631-13.75.388-3.775-.56-3.557-3.404-3.557-3.404L15.691 4.474a38.635 38.635 0 00-3.471-.163Z"></path>',
        },
        {
          title: "{{pairTitle}}",
          desc: "{{pairDesc}}",
          icon: '<path d="M11.988 0a11.85 11.85 0 00-8.617 3.696c7.02-.875 11.401-.583 13.289-.34 3.752.583 3.558 3.404 3.558 3.404L8.237 19.112c2.299.22 6.897.366 13.796-.631a11.86 11.86 0 001.912-6.469C23.945 5.374 18.595 0 11.988 0zm.232 4.31c-2.451-.014-5.772.146-9.963.723C.854 7.003.055 9.41.055 12.012.055 18.626 5.38 24 11.988 24c3.63 0 6.85-1.63 9.053-4.182-7.286.948-11.813.631-13.75.388-3.775-.56-3.557-3.404-3.557-3.404L15.691 4.474a38.635 38.635 0 00-3.471-.163Z"></path>',
        },
        {
          title: "Recherche de l'appareil",
          desc: "Nous scannons le réseau… Cela peut prendre jusqu'à 30s. Votre appareil apparaîtra automatiquement ci-dessous.",
          icon: '<path d="M11.988 0a11.85 11.85 0 00-8.617 3.696c7.02-.875 11.401-.583 13.289-.34 3.752.583 3.558 3.404 3.558 3.404L8.237 19.112c2.299.22 6.897.366 13.796-.631a11.86 11.86 0 001.912-6.469C23.945 5.374 18.595 0 11.988 0zm.232 4.31c-2.451-.014-5.772.146-9.963.723C.854 7.003.055 9.41.055 12.012.055 18.626 5.38 24 11.988 24c3.63 0 6.85-1.63 9.053-4.182-7.286.948-11.813.631-13.75.388-3.775-.56-3.557-3.404-3.557-3.404L15.691 4.474a38.635 38.635 0 00-3.471-.163Z"></path>',
        },
        {
          title: "Nommer & enregistrer",
          desc: "Choisissez un nom puis cliquez sur Enregistrer.",
          icon: '<path d="M11.988 0a11.85 11.85 0 00-8.617 3.696c7.02-.875 11.401-.583 13.289-.34 3.752.583 3.558 3.404 3.558 3.404L8.237 19.112c2.299.22 6.897.366 13.796-.631a11.86 11.86 0 001.912-6.469C23.945 5.374 18.595 0 11.988 0zm.232 4.31c-2.451-.014-5.772.146-9.963.723C.854 7.003.055 9.41.055 12.012.055 18.626 5.38 24 11.988 24c3.63 0 6.85-1.63 9.053-4.182-7.286.948-11.813.631-13.75.388-3.775-.56-3.557-3.404-3.557-3.404L15.691 4.474a38.635 38.635 0 00-3.471-.163Z"></path>',
        },
      ];

      let current = 0;
      let timeout =0;
      // Armes par getAlert() a la reception de l'alerte "appareil trouve" (code 3).
      // En var (et non let) : getAlert() vit dans functions.js et les positionne sur window.
      // Sans cette declaration, waitDevice() levait une ReferenceError des son 1er appel et
      // la detection automatique ne fonctionnait pas.
      var deviceFound = false;
      var deviceFoundInfo = '';
      const progress   = document.getElementById('progressBar');
      const iconEl     = document.getElementById('icon');
      const titleEl    = document.getElementById('stepTitle');
      const descEl     = document.getElementById('stepDesc');
      const dynamic    = document.getElementById('dynamicZone');
      const prevBtn    = document.getElementById('prevBtn');
      const nextBtn    = document.getElementById('nextBtn');

      // Enregistre le nom puis revient a la liste de la bonne radio. Volontairement local a
      // l'assistant (et non dans functions.js) : cette page est servie par le firmware, sa
      // navigation ne doit pas dependre d'un functions.min.js.gz uploade separement, qui
      // peut etre plus ancien que le firmware.
      function saveAlias(IEEE,alias)
      {
        const xhr = getXhr();
        xhr.onreadystatechange = function(){
          if (xhr.readyState == 4) { window.location.href = "{{backUrl}}"; }
        };
        xhr.open('GET','setAlias?ieee='+encodeURIComponent(IEEE)+'&alias='+encodeURIComponent(alias),true);
        xhr.setRequestHeader('Content-Type','application/html');
        xhr.send();
      }

      // Affiche l'appareil trouve. L'alerte peut arriver avant qu'on soit a l'etape 2 (la
      // zone n'existe alors pas encore) : getAlert() memorise le libelle, on le pose ici.
      function showFoundDevice()
      {
        const zone = document.getElementById('deviceFound');
        if (zone && deviceFoundInfo) { zone.innerHTML = deviceFoundInfo; }
        nextBtn.style.display='block';
      }

      function waitDevice()
      {
        if (current < 1) return;

        if (deviceFound)
        {
          deviceFound = false;
          if (current != 2) { current = 2; timeout = 0; render(); }
          showFoundDevice();
          return;
        }

        if (timeout>=30)
        {
          //not found
          timeout = 0;
          current = 2;
          render();
          dynamic.innerHTML = `<div id="deviceFound" class="d-flex flex-column align-items-center gap-2">
              <span>Not found !</span>
            </div>`;
          return;
        }
        timeout++;
        setTimeout(function(){waitDevice();}, 1000);
      }

      function render(){
        const step = steps[current];
        progress.style.width = ((current+1)/steps.length)*100 + '%';
        iconEl.innerHTML = step.icon;
        titleEl.textContent = step.title;
        descEl.textContent  = step.desc;
        dynamic.innerHTML   = '';
        
        if(current === 0){
          dynamic.innerHTML = `<div class="d-flex flex-column align-items-center gap-2">
           <img src="web/img/zlinky.gif" width="120px">
          </div>`;
          nextBtn.style.display='block';
        }else if(current === 1){
          dynamic.innerHTML = `<div class="d-flex flex-column align-items-center gap-2">
           <img src="web/img/ziwifi32.gif" width="120px">
          </div>`;
          cmd("{{pairCmd}}");
          getAlert();
          setTimeout(function(){waitDevice();}, 1000);
          nextBtn.style.display='block';
        }else if(current === 2){
          dynamic.innerHTML = `<div id="deviceFound" class="d-flex flex-column align-items-center gap-2">
            <div class="spinner-border text-primary spinner-border-lg" role="status"></div>
            <span>Recherche en cours…</span>
          </div>`;
          nextBtn.style.display='none';
        }else if(current === 3){
          dynamic.innerHTML = `<div class="mb-3">
            <label class="form-label">Nom de l'appareil</label>
            <input type="text" id="alias" class="form-control" required>
          </div>`;
          nextBtn.style.display='block';
        }
        if (current > 0){
          prevBtn.style.display='block';
        }else{
          prevBtn.style.display='none';         
        }
        nextBtn.textContent = current < steps.length - 1 ? 'Suivant' : 'Enregistrer';
      }

      prevBtn.addEventListener('click', () => {
        if(current > 0){ current--; render(); }
      });
      var IEEE;
      nextBtn.addEventListener('click', () => {
        if(current < steps.length - 1){
          if (current === 0) {
            
          }else if (current === 1) {
            getAlert();
          }else if (current === 2) {
            IEEE = document.getElementById('newDevice').innerHTML;
          }
          current++; 
          render(); 
        } else {
          var alias = document.getElementById('alias').value;
          saveAlias(IEEE,alias);
        }
      });
      render();
    </script>

)";

const char HTTP_HELP[] PROGMEM =  R"(
    <div class="container p-4">
      <h4 class="card-title mb-4">About</h4>
      <div class="card mx-auto shadow-sm">
        <div class="card-body">
          <h5>Version : {{version}}</h5>
          Visitez la boutique :</br>
          <a href="https://lixee.fr/" target='_blank'>Boutique LiXee </a></br><br>
          <h5>Sources Firmware</h5>
          Suivez le lien :</br>
          <a href="https://github.com/fairecasoimeme/LiXee-Gateway" target='_blank'>Sources</a>
        </div>
      </div>
    </div>
)";


const char HTTP_NOTIF_ALERT[] PROGMEM = R"(
   {
  "notifications": [
    {
      "title": "Consommation élevée",
      "message": "Votre consommation électrique a dépassé 10kWh aujourd'hui.",
      "timestamp": "2025-05-14T18:45:00Z",
      "type": "warning"
    },
    {
      "title": "Appareil connecté",
      "message": "La LiXee-BOX est maintenant en ligne.",
      "timestamp": "2025-05-14T16:20:00Z",
      "type": "info"
    },
    {
      "title": "Mise à jour disponible",
      "message": "Une mise à jour firmware est disponible pour ce périphérique.",
      "timestamp": "2025-05-13T08:00:00Z",
      "type": "update"
    }
  ]
}
)";

/*const char HTTP_HELP[] PROGMEM =  
    "<h4>About</h4>"
    "<h5>Version : {{version}}</h5>"
   " <h5>Shop & description</h5>"
    "You can go to this url :</br>"
    "<a href=\"https://lixee.fr/\" target='_blank'>Shop </a></br>"

    "<h5>Firmware Source & Issues</h5>"
    "Please go here :</br>"
    "<a href=\"https://github.com/fairecasoimeme/LiXee-Gateway\" target='_blank'>Sources</a>";*/

String footer()
{
  String result="";
  if (ConfigSettings.enableSecureHttp) {
    result += "<style>.logoutItem{display:block!important}</style>";
  }
  result +="<br><hr>";
  result +="<div align='center' style='font-size:12px;'>";
  result +=    "Copyright : LiXee 2025 - version : "+ String(VERSION);
  result +=  "</div>";
  result+=FPSTR(HTTP_FOOTER);
  result.replace("{{version}}",String(VERSION));
  result.replace("{{value}}",String((int)ConfigGeneral.developerMode));
  return result;
}

String footerAssist()
{
  String result="";
  if (ConfigSettings.enableSecureHttp) {
    result += "<style>.logoutItem{display:block!important}</style>";
  }
  result +="<br><hr>";
  result +="<div align='center' style='font-size:12px;'>";
  result +=    "Copyright : LiXee 2025 - version : "+ String(VERSION);
  result +=  "</div>";
  result+=FPSTR(HTTP_FOOTER_ASSIST);

  return result;
}

String getMenuGeneralZigbee(String tmp, String selected)
{
  
  tmp.replace("{{menu_config_zigbee}}", FPSTR(HTTP_CONFIG_MENU_ZIGBEE));
  if (selected=="devices")
  {
    tmp.replace("{{menu_config_devices}}", "disabled");
  }else{
    tmp.replace("{{menu_config_devices}}", "");
  }
  if (selected=="config")
  {
    tmp.replace("{{menu_config_zigbee}}", "disabled");
  }else{
    tmp.replace("{{menu_config_zigbee}}", "");
  }
  return tmp;
}

/* ESP32-DEV
float temperatureReadFixed()
{
  SET_PERI_REG_BITS(SENS_SAR_MEAS_WAIT2_REG, SENS_FORCE_XPD_SAR, 3, SENS_FORCE_XPD_SAR_S);
  SET_PERI_REG_BITS(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_CLK_DIV, 10, SENS_TSENS_CLK_DIV_S);
  CLEAR_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP);
  CLEAR_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_DUMP_OUT);
  SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP_FORCE);
  SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP);
  ets_delay_us(100);
  SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_DUMP_OUT);
  ets_delay_us(5);
  float temp_f = (float)GET_PERI_REG_BITS2(SENS_SAR_SLAVE_ADDR3_REG, SENS_TSENS_OUT, SENS_TSENS_OUT_S);
  float temp_c = (temp_f - 32) / 1.8;

  float temp_c = 60;
  return temp_c;
}*/

bool isValidIP(const String &ip) {
    int sections = 0;     // Compte les sections séparées par des points
    int start = 0;        // Indice de début de la section actuelle

    for (int i = 0; i <= ip.length(); i++) {
        // Vérifie si nous avons atteint la fin de la section
        if (i == ip.length() || ip[i] == '.') {
            if (i == start) {
                return false; // Aucun numéro entre les points
            }

            String part = ip.substring(start, i);
            if (part.length() > 1 && part[0] == '0') {
                return false; // Pas de partie avec zéro(s) en tête (ex: "01")
            }

            for (char c : part) {
                if (!isDigit(c)) {
                    return false; // Chaque caractère doit être un chiffre
                }
            }

            int num = part.toInt();
            if (num < 0 || num > 255) {
                return false; // Chaque partie doit être entre 0 et 255
            }

            sections++;
            start = i + 1; // Met à jour le début pour la prochaine section
        }
    }

    return sections == 4; // Une adresse IPv4 doit avoir exactement 4 sections
}

float temperatureReadFixed()
{
  float result = 0;
  temp_sensor_read_celsius(&result);

  return result;
}

bool TemplateExist(int deviceId)
{
  if (deviceId>0)
  {
    //String path = "/tp/" + (String)deviceId + ".json";
    const char* path ="/tp/";
    const char* extension =".json";
    char name_with_extension[64];
    strlcpy(name_with_extension,path, sizeof(name_with_extension));
    strcat(name_with_extension,String(deviceId).c_str());
    strcat(name_with_extension,extension);
    File tpFile = LittleFS.open(name_with_extension, FILE_READ);
    if (!tpFile || tpFile.isDirectory())
    {
      return false;
    }
    tpFile.close();
    return true;
  }else{
    return false;
  }
}

/*void setTemplateElement(State &e, JsonVariant v) 
{
    strlcpy(e.name, v[F("name")], sizeof(e.name));
    e.cluster = (int)strtol(v[F("cluster")], nullptr, 16);
    e.attribute = v[F("attribut")];
    strlcpy(e.type, v[F("type")] | "", sizeof(e.type));
    strlcpy(e.mqtt_device_class, v[F("mqtt_device_class")] | "null", sizeof(e.mqtt_device_class));
    strlcpy(e.mqtt_state_class, v[F("mqtt_state_class")] | "null", sizeof(e.mqtt_state_class));
    strlcpy(e.mqtt_icon, v[F("mqtt_icon")] | "", sizeof(e.mqtt_icon));
    e.coefficient = v[F("coefficient")] | 1.0;
    strlcpy(e.unit, v[F("unit")] | "", sizeof(e.unit));
    e.visible = v[F("visible")].as<int>() == 1;
    strlcpy(e.mode, v[F("mode")] | "", sizeof(e.mode));
    if (!v[F("jauge")].isNull()) {
        strlcpy(e.typeJauge, v[F("jauge")], sizeof(e.typeJauge));
        e.jaugeMin = v[F("min")].as<int>();
        e.jaugeMax = v[F("max")].as<int>();
    } else {
        strlcpy(e.typeJauge, "", sizeof(e.typeJauge));
    }
}

void parseStatusArray(JsonArray statusArray, Template* t) 
{
    int i = 0;
    for (JsonVariant v : statusArray) 
    {
        setTemplateElement(t->e[i], v);
        i++;
        vTaskDelay(1);
    }
    t->StateSize = i;
}

void parseActionArray(JsonArray actionArray, Template* t) 
{
    int i = 0;
    for (JsonVariant v : actionArray) 
    {
        strlcpy(t->a[i].name, v[F("name")], sizeof(t->a[i].name));
        t->a[i].command = v[F("command")];
        t->a[i].endpoint = v[F("endpoint")];
        t->a[i].value = v[F("value")];
        t->a[i].visible = v[F("visible")].as<int>() == 1;
        i++;
        vTaskDelay(1);
    }
    t->ActionSize = i;
}

Template* GetTemplate(int deviceId, String model) 
{
    Template *t = (Template *) ps_malloc(sizeof(Template));
    if (deviceId <= 0) {
        return t;
    }

    // Construire le chemin du fichier
    String filePath = "/tp/" + String(deviceId) + ".json";
    File tpFile = LittleFS.open(filePath, FILE_READ);

    if (!tpFile || tpFile.isDirectory()) {
        DEBUG_PRINTLN(F("failed open"));
        return t;
    }

    SpiRamJsonDocument doc(MAXHEAP);
    DeserializationError error = deserializeJson(doc, tpFile);
    tpFile.close();
    if (error) {
        DEBUG_PRINTLN(F("deserializeJson failed"));
        return t;
    }

    // Déterminer le modèle à utiliser
    const char* modelKey = doc.containsKey(model) ? model.c_str() : "default";
    JsonObject modelObj = doc[modelKey][0];
    if (modelObj) {
        JsonArray statusArray = modelObj["status"].as<JsonArray>();
        JsonArray actionArray = modelObj["action"].as<JsonArray>();
        
        parseStatusArray(statusArray, t);
        parseActionArray(actionArray, t);
    }

    

    return t;
}*/

/*Template * GetTemplate(int deviceId, String model)
{
  Template *t = (Template *) ps_malloc(sizeof(Template));
  if (deviceId>0)
  {
    //String path = "/tp/" + String(deviceId) + ".json";
    const char* path ="/tp/";
    const char* extension =".json";
    char name_with_extension[64];
    strlcpy(name_with_extension,path, sizeof(name_with_extension));
    strcat(name_with_extension,String(deviceId).c_str());
    strcat(name_with_extension,extension);
    File tpFile = LittleFS.open(name_with_extension, FILE_READ);
    if (!tpFile || tpFile.isDirectory())
    {
      DEBUG_PRINTLN(F("failed open"));
      return t;
    }
    else
    {
      SpiRamJsonDocument temp(MAXHEAP);
      deserializeJson(temp, tpFile);
      tpFile.close();
      int i = 0;
      const char *tmp;

      if (temp.containsKey(model))
      {
        
        JsonArray StatusArray = temp[model][0][F("status")].as<JsonArray>();
        for (JsonVariant v : StatusArray)
        {
          
          tmp = temp[model][0][F("status")][i][F("name")];
          strlcpy(t->e[i].name, tmp, sizeof(t->e[i].name));
          t->e[i].cluster = (int)strtol(temp[model][0][F("status")][i][F("cluster")], 0, 16);
          t->e[i].attribute = (int)temp[model][0][F("status")][i][F("attribut")];
          if (temp[model][0][F("status")][i][F("type")])
          {
            strlcpy(t->e[i].type, temp[model][0][F("status")][i][F("type")], sizeof(t->e[i].type));
          }
          else
          {
            strlcpy(t->e[i].type, "", sizeof(t->e[i].type));
          }
          //MQTT
          if (temp[model][0][F("status")][i][F("mqtt_device_class")])
          {
            strlcpy(t->e[i].mqtt_device_class, temp[model][0][F("status")][i][F("mqtt_device_class")], sizeof(t->e[i].mqtt_device_class));
          }
          else
          {
            strlcpy(t->e[i].mqtt_device_class, "null", sizeof(t->e[i].mqtt_device_class));
          }
          if (temp[model][0][F("status")][i][F("mqtt_state_class")])
          {
            strlcpy(t->e[i].mqtt_state_class, temp[model][0][F("status")][i][F("mqtt_state_class")], sizeof(t->e[i].mqtt_state_class));
          }
          else
          {
            strlcpy(t->e[i].mqtt_state_class, "null", sizeof(t->e[i].mqtt_state_class));
          }
          if (temp[model][0][F("status")][i][F("mqtt_icon")])
          {
            strlcpy(t->e[i].mqtt_icon, temp[model][0][F("status")][i][F("mqtt_icon")], sizeof(t->e[i].mqtt_icon));
          }
          else
          {
            strlcpy(t->e[i].mqtt_icon, "", sizeof(t->e[i].mqtt_icon));
          }



          if (temp[model][0][F("status")][i][F("coefficient")])
          {
            t->e[i].coefficient = (float)temp[model][0][F("status")][i][F("coefficient")];
          }
          else
          {
            t->e[i].coefficient = 1;
          }
          if (temp[model][0][F("status")][i][F("unit")])
          {
            strlcpy(t->e[i].unit, temp[model][0][F("status")][i][F("unit")], sizeof(t->e[i].unit));
          }
          else
          {
            strlcpy(t->e[i].unit, "", sizeof(t->e[i].unit));
          }
        
          if (temp[model][0][F("status")][i][F("visible")].as<int>() == 1)
          {
            t->e[i].visible = 1;
          }
          else
          {
            t->e[i].visible = 0;
          }

          if (temp[model][0][F("status")][i][F("mode")])
          {
            strlcpy(t->e[i].mode, temp[model][0][F("status")][i][F("mode")], sizeof(t->e[i].mode));
          }
          else
          {
            strlcpy(t->e[i].mode, "", sizeof(t->e[i].mode));
          }

          if (temp[model][0][F("status")][i][F("jauge")])
          {
            strlcpy(t->e[i].typeJauge, temp[model][0][F("status")][i][F("jauge")], sizeof(t->e[i].typeJauge));
            t->e[i].jaugeMin = temp[model][0][F("status")][i][F("min")].as<int>();
            t->e[i].jaugeMax = temp[model][0][F("status")][i][F("max")].as<int>();
          }
          else
          {
            strlcpy(t->e[i].typeJauge, "", sizeof(t->e[i].typeJauge));
          }
          i++;
          vTaskDelay(1);
        }
        t->StateSize = i;
        i = 0;
        JsonArray ActionArray = temp[model][0][F("action")].as<JsonArray>();
        for (JsonVariant v : ActionArray)
        {

          tmp = temp[model][0][F("action")][i][F("name")];
          strlcpy(t->a[i].name, tmp, sizeof(t->a[i].name));
          t->a[i].command = (int)temp[model][0][F("action")][i][F("command")];
          t->a[i].value = (int)temp[model][0][F("action")][i][F("value")];
          if (temp[model][0][F("action")][i][F("visible")].as<int>() == 1)
          {
            t->a[i].visible = 1;
          }
          else
          {
            t->a[i].visible = 0;
          }
          i++;
          vTaskDelay(1);
        }
        t->ActionSize = i;
        // tmp = temp[model][0]["bind"];
        // strlcpy(t.bind,tmp,sizeof(50));
        return t;
      } else if (temp.containsKey("default"))       
      {
        JsonArray StatusArray = temp[F("default")][0][F("status")].as<JsonArray>();
        for (JsonVariant v : StatusArray)
        {

          tmp = temp[F("default")][0][F("status")][i][F("name")];
          strlcpy(t->e[i].name, tmp, sizeof(t->e[i].name));
          t->e[i].cluster = (int)strtol(temp[F("default")][0][F("status")][i][F("cluster")], 0, 16);
          t->e[i].attribute = (int)temp[F("default")][0][F("status")][i][F("attribut")];
          if (temp[F("default")][0][F("status")][i][F("type")])
          {
            strlcpy(t->e[i].type, temp[F("default")][0][F("status")][i][F("type")], sizeof(t->e[i].type));
          }
          else
          {
            strlcpy(t->e[i].type, "", sizeof(t->e[i].type));
          }

          //MQTT
          if (temp[F("default")][0][F("status")][i][F("mqtt_device_class")])
          {
            strlcpy(t->e[i].mqtt_device_class, temp[F("default")][0][F("status")][i][F("mqtt_device_class")], sizeof(t->e[i].mqtt_device_class));
          }
          else
          {
            strlcpy(t->e[i].mqtt_device_class, "", sizeof(t->e[i].mqtt_device_class));
          }
          if (temp[F("default")][0][F("status")][i][F("mqtt_state_class")])
          {
            strlcpy(t->e[i].mqtt_state_class, temp[F("default")][0][F("status")][i][F("mqtt_state_class")], sizeof(t->e[i].mqtt_state_class));
          }
          else
          {
            strlcpy(t->e[i].mqtt_state_class, "", sizeof(t->e[i].mqtt_state_class));
          }
          if (temp[F("default")][0][F("status")][i][F("mqtt_icon")])
          {
            strlcpy(t->e[i].mqtt_icon, temp[F("default")][0][F("status")][i][F("mqtt_icon")], sizeof(t->e[i].mqtt_icon));
          }
          else
          {
            strlcpy(t->e[i].mqtt_icon, "", sizeof(t->e[i].mqtt_icon));
          }



          if (temp[F("default")][0][F("status")][i][F("coefficient")])
          {
            t->e[i].coefficient = (float)temp[F("default")][0][F("status")][i][F("coefficient")];
          }
          else
          {
            t->e[i].coefficient = 1;
          }
          if (temp[F("default")][0][F("status")][i][F("unit")])
          {
            strlcpy(t->e[i].unit, temp[F("default")][0][F("status")][i][F("unit")], sizeof(t->e[i].unit));
          }
          else
          {
            strlcpy(t->e[i].unit, "", sizeof(t->e[i].unit));
          }
          if (temp[F("default")][0][F("status")][i][F("visible")].as<int>() == 1)
          {
            t->e[i].visible = 1;
          }
          else
          {
            t->e[i].visible = 0;
          }
          if (temp[F("default")][0][F("status")][i][F("jauge")])
          {
            strlcpy(t->e[i].typeJauge, temp[F("default")][0][F("status")][i][F("jauge")], sizeof(t->e[i].typeJauge));
            t->e[i].jaugeMin = temp[F("default")][0][F("status")][i][F("min")].as<int>();
            t->e[i].jaugeMax = temp[F("default")][0][F("status")][i][F("max")].as<int>();
          }
          else
          {
            strlcpy(t->e[i].typeJauge, "", sizeof(t->e[i].typeJauge));
          }
          i++;
          vTaskDelay(1);
        }
        t->StateSize = i;
        i = 0;
        JsonArray ActionArray = temp[F("default")][0][F("action")].as<JsonArray>();
        for (JsonVariant v : ActionArray)
        {

          tmp = temp[F("default")][0][F("action")][i][F("name")];
          strlcpy(t->a[i].name, tmp, sizeof(t->a[i].name));
          t->a[i].command = (int)temp[F("default")][0][F("action")][i][F("command")];
          t->a[i].value = (int)temp[F("default")][0][F("action")][i][F("value")];
          if (temp[F("default")][0][F("action")][i][F("visible")].as<int>() == 1)
          {
            t->a[i].visible = 1;
          }
          else
          {
            t->a[i].visible = 0;
          }
          i++;
          vTaskDelay(1);
        }
        t->ActionSize = i;
      }
      else
      {
        t->StateSize = 0;
        t->ActionSize = 0;
      }
      return t;
      
    }
    
  }
  return t;
}*/

TemplateData* getTemplateForDevice(DeviceData* device) {
    if (!device) return nullptr;
    String deviceId = device->getInfo().device_id;
    String model = device->getInfo().model;
    if (deviceId.isEmpty()) return nullptr;
    if (model.isEmpty()) model = "default";
    return templateCache.get(deviceId + ".json", model);
}




bool existDashboard(String inifile)
{
  String tmp = ini_read(inifile, "dashboard", "enable");
  return tmp.toInt();
}

String getAliasDashboard(String inifile)
{
  String tmp = ini_read(inifile, "dashboard", "alias");
  return tmp;
}

String createGaugePower(String div, String min, String max, String label)
{
  String result = "";
  result += "var Gauge" + div + " = new JustGage({";
  result += "id: 'status_" + div + "',";
  result += F("value: 0,");
  result += "min: " + min + ",";
  result += "max: " + max + ",";
  result += F("title: 'Target',");
  result += "label:'" + label + "',";
  result += F("gaugeWidthScale: 0.6,");
  result += F("pointer: true,");
  result += F("pointerOptions: {");
  result += F("toplength: 10,");
  result += F("bottomlength: 10,");
  result += F("bottomwidth: 2");
  result += F("},");
  result += F("humanFriendly: true,");
  result += F("relativeGaugeSize: true,");
  result += F("refreshAnimationTime: 1000");
  result += F("});");

  return result;
}


String createGaugeDashboard(String div, String i, String min, String max, String label)
{
  String result = "";
  result += "var Gauge" + div  + i + " = new JustGage({";
  result += "id: 'gauge_" + div + i +"',";
  result += F("value: 0,");
  result += "min: " + min + ",";
  result += "max: " + max + ",";
  result += F("height: 200,");
  result += F("title: 'Target',");
  result += "label:'" + label + "',";
  result += F("gaugeWidthScale: 0.6,");
  result += F("pointer: true,");
  result += F("pointerOptions: {");
  result += F("toplength: 10,");
  result += F("bottomlength: 10,");
  result += F("bottomwidth: 2");
  result += F("},");
  result += F("humanFriendly: true,");
  result += F("relativeGaugeSize: true,");
  result += F("refreshAnimationTime: 1000");
  result += F("});");

  return result;
}

String createBaterryDashboard(String div, String i, String min, String max, String label)
{
  String result = "";
  result += "var Gauge" + div + i + " = new JustGage({";
  result += "id: 'gauge_" + div + i + "',";
  result += F("value: 0,");
  result += "min: " + min + ",";
  result += "max: " + max + ",";
  result += F("title: 'Target',");
  result += "label:'" + label + "',";
  result += F("gaugeWidthScale: 0.6,");
  result += F("pointer: true,");
  result += F("pointerOptions: {");
  result += F("toplength: 10,");
  result += F("bottomlength: 10,");
  result += F("bottomwidth: 2");
  result += F("},");
  result += F("customSectors: {");
  result +=    F(" percents: true,");
  result +=    F(" ranges: [{");
  result +=      F("   color : '#ff3b30',");
  result +=      F("   lo : 0,");
  result +=      F("   hi : 20");
  result +=      F(" },{");
  result +=      F("  color : '#f39c12',");
  result +=      F("  lo : 21,");
  result +=      F("  hi : 50");
  result +=      F(" },{");
  result +=      F("  color : '#43bf58',");
  result +=      F("  lo : 51,");
  result +=      F("  hi : 100");
  result +=    F(" }]");
  result += F(" },");
  result += F("humanFriendly: true,");
  result += F("relativeGaugeSize: true,");
  result += F("refreshAnimationTime: 1000");
  result += F("});");

  return result;
}
// String CreateTimeGauge(String div,String IEEE,String cluster, String attr,String type, String coefficient)
String CreateTimeGauge(String div)
{
  String result = "";
  result += "function refreshGauge" + div + "(IEEE,cluster,attr,type,coefficient)";
  result += F("{");
  result += F("var xhr = getXhr();");
  result += F("xhr.onreadystatechange = function(){");
  result += F("if(xhr.readyState == 4 ){");
  result += F("leselect = xhr.responseText;");
  result += "Gauge" + div + ".refresh(leselect);";
  result += "setTimeout(function(){ refreshGauge" + div + "(IEEE,cluster,attr,type,coefficient); }, 5000);";
  result += F("}");
  result += F("};");
  result += F("xhr.open('GET','loadGaugeDashboard?IEEE='+escape(IEEE)+'&cluster='+escape(cluster)+'&attribute='+escape(attr)+'&type='+escape(type)+'&coefficient='+escape(coefficient),true);");
  result += F("xhr.setRequestHeader('Content-Type','application/html');");
  result += F("xhr.send();");
  result += F("};");

  return result;
}

String createDistributionGraph(String IEEE)
{
  String result = "";
  result += F("donutChart = Morris.Donut({");
  result += F(" element: 'donut-chart',");
  result += F("data: [],");
  result += F("formatter: function (value,data){return value +' '+data.unit;},");
  
  // Couleurs par défaut - seront remplacées dynamiquement si sous-compteurs configurés
  if (ConfigGeneral.subMeterCount > 0) {
    // Mode sous-compteurs : couleurs gérées dynamiquement par JavaScript
    result += F(" colors: ['#3498db','#e74c3c','#f39c12','#9b59b6','#1abc9c','#e67e22','#95a5a6','#27ae60','#2980b9','#8e44ad'],");
  } else {
    // Mode index tarifaires : couleurs originales
    if (strcmp(ConfigGeneral.Production, "") == 0) {
      if ((strcmp(ConfigGeneral.Gaz, "") == 0) || (strcmp(ConfigGeneral.unitGaz, "Wh") != 0)) {
        result += F(" colors: ['#2980b9','#154360','#7f8c8d','#000000','#e74c3c','#c0392b','#f5b041','#145a32'],");
      } else {
        result += F(" colors: ['#e67e22','#2980b9','#154360','#7f8c8d','#000000','#e74c3c','#c0392b','#f5b041','#145a32'],");
      }
    } else {
      if ((strcmp(ConfigGeneral.Gaz, "") == 0) || (strcmp(ConfigGeneral.unitGaz, "Wh") != 0)) {
        result += F(" colors: ['#27ae60','#2980b9','#154360','#7f8c8d','#000000','#e74c3c','#c0392b','#f5b041','#145a32'],");
      } else {
        result += F(" colors: ['#e67e22','#27ae60','#2980b9','#154360','#7f8c8d','#000000','#e74c3c','#c0392b','#f5b041','#145a32'],");
      }
    }
  }
  
  result += F(" resize: true,");
  result += F(" animate: false,");
  result += F(" showPercentage: true,");
  result += F(" });");

  return result;
}

/*String createPowerGraph(String IEEE)
{
  String result = "";
  result += F("powerChart = Morris.Bar({");
  result += F(" element: 'power-chart',");
  result += F("data: [],");
  result += F("xkey: 'y',");

  if ((ConfigGeneral.LinkyMode == 2 ) || (ConfigGeneral.LinkyMode == 3 ) || (ConfigGeneral.LinkyMode == 7 ))
  {
    if (strcmp(ConfigGeneral.Production,"")!=0)
    {
      result += F(" ykeys: ['1','2','3','1295','2319','2575'],");
      result += F(" labels: ['Injection Ph1(VA)','Injection Ph2(VA)','Injection Ph3(VA)','Power Ph1(VA)','Power Ph2(VA)','Power Ph3(VA)''],");
    }else{
      result += F(" ykeys: ['1','2','3','1295','2319','2575'],");
      result += F(" labels: ['Injection Ph1(VA)','Injection Ph2(VA)','Injection Ph3(VA)','Power Ph1(VA)','Power Ph2(VA)','Power Ph3(VA)'],");
    }
      result += F(" barColors: ['#27ae60','#0ed160ff','#08612dff','#1e88e5','#5dade2','#85c1e9'],");
  }else{
    if (strcmp(ConfigGeneral.Production,"")!=0)
    {
      //result += F(" ykeys: [1295,519],");
      //result += F(" labels: ['Power (VA)','Production(VA)'],");
      result += F(" ykeys: ['1','2','3','1295'],");
      result += F(" labels: ['Injection Ph1(VA)','Injection Ph2(VA)','Injection Ph3(VA)','Power (VA)'],");

    }else{
      result += F(" ykeys: ['1','2','3','1295'],");
      result += F(" labels: ['Injection Ph1(VA)','Injection Ph2(VA)','Injection Ph3(VA)','Power (VA)'],");
    }
    result += F(" barColors: ['#27ae60','#0ed160ff','#08612dff','#1e88e5'],");
  }
  result += F(" resize: true,");
  result += F(" redraw: true,");
  result += F(" xLabelAngle: 70,");
  result += F(" stacked: true,");
  result += F(" goals : [");
  int goal=0;
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == IEEE)
    {
      
      goal = strtol(device->getValue("0B01","13").c_str(),0,16)*230;
      result += String(goal);
      break;
    }
  }  

  result += F("],");
  result += F(" ymin: ");
  result += String(-1 * round((goal * 0.5) / 1000) * 1000); // 50% du goal en négatif
  result += F(",");
  result += F(" ymax: ");
    result += String(round((goal * 1.25) / 1000) * 1000);
  result += F(",");
  result += F(" postUnits: ' VA',");
  result += F(" dataLabels: false,");
  result += F(" animate: false,");
  result += F(" });");

  return result;
}*/
String createPowerGraph(String IEEE)
{
  String result = "";
  
  result += F("var canvas = document.getElementById('power-chart');");
  result += F("if (!canvas) { console.error('Canvas power-chart non trouvé'); return; }");
  result += F("var ctx = canvas.getContext('2d');");
  result += F("if (window.powerChart) window.powerChart.destroy();");
  
  // Calculer le goal
  int goal = 0;
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == IEEE)
    {
      if ((ConfigGeneral.LinkyMode == 0) || (ConfigGeneral.LinkyMode == 2))
      {
        goal += strtol(device->getValue("0B01", "13").c_str(), 0, 16) * 200;
      }else {
        goal += strtol(device->getValue("0B01", "14").c_str(), 0, 16) * 1000;
      }
     
      break;
    }
  }
  
  result += F("window.powerGoal = ");
  result += String(goal);
  result += F(";");
  
  bool isTriphase = (ConfigGeneral.LinkyMode == 2) || (ConfigGeneral.LinkyMode == 3) || (ConfigGeneral.LinkyMode == 7);
  result += F("window.powerIsTriphasé = ");
  result += isTriphase ? F("true") : F("false");
  result += F(";");

  result += F("window.powerChart = new Chart(ctx, {");
  result += F(" type: 'bar',");
  result += F(" data: {");
  result += F("  labels: [],");
  result += F("  datasets: [");

  if (isTriphase)
  {
    result += F("   {label: 'Injection Ph1', data: [], backgroundColor: '#27ae60', stack: 'Stack0', hidden: true},");
    result += F("   {label: 'Injection Ph2', data: [], backgroundColor: '#0ed160', stack: 'Stack0', hidden: true},");
    result += F("   {label: 'Injection Ph3', data: [], backgroundColor: '#08612d', stack: 'Stack0', hidden: true},");
    result += F("   {label: 'Puissance Ph1', data: [], backgroundColor: '#1e88e5', stack: 'Stack0'},");
    result += F("   {label: 'Puissance Ph2', data: [], backgroundColor: '#5dade2', stack: 'Stack0'},");
    result += F("   {label: 'Puissance Ph3', data: [], backgroundColor: '#85c1e9', stack: 'Stack0'}");
  }
  else
  {
    result += F("   {label: 'Injection', data: [], backgroundColor: '#27ae60', stack: 'Stack0', hidden: true},");
    result += F("   {label: 'Puissance', data: [], backgroundColor: '#1e88e5', stack: 'Stack0'}");
  }

  result += F("  ]");
  result += F(" },");
  result += F(" options: {");
  result += F("  responsive: true,");
  result += F("  maintainAspectRatio: false,");
  result += F("  animation: false,");
  result += F("  interaction: { mode: 'index', intersect: false },");

  result += F("  scales: {");
  result += F("   x: {");
  result += F("    stacked: true,");
  result += F("    ticks: { maxRotation: 70, minRotation: 70, autoSkip: true, maxTicksLimit: 20 }");
  result += F("   },");
  result += F("   y: {");
  result += F("    stacked: true,");
  result += F("    beginAtZero: false,");
  result += F("    ticks: { callback: function(value) { return value + ' VA'; } }");
  result += F("   }");
  result += F("  },");
  result += F("  barPercentage: 0.9,");
  result += F("  categoryPercentage: 0.95,");

  result += F("  plugins: {");
  result += F("   zoom: {");
  result += F("    limits: { x: { min: 'original', max: 'original', minRange: 3 } },");
  result += F("    pan: { enabled: true, mode: 'x', modifierKey: 'ctrl' },");
  result += F("    zoom: { wheel: { enabled: true, speed: 0.1 }, pinch:{enable: true}, mode: 'x', drag: { enabled: true } }");
  result += F("   },");

  result += F("   tooltip: {");
  result += F("    enabled: false,");
  result += F("    mode: 'index',");
  result += F("    intersect: false,");
  result += F("    backgroundColor: 'rgba(133, 133, 133, 0.65)',");
  result += F("    callbacks: {");
  result += F("     title: function(tooltipItems) { return tooltipItems.length > 0 ? tooltipItems[0].label : ''; },");
  result += F("     label: function(context) { return getPowerTooltipLabel(context); },");
  result += F("     footer: function(tooltipItems) { return getPowerTooltipFooter(tooltipItems); }");
  result += F("    }");
  result += F("   },");
  result += F("   legend: { display: true, position: 'top' },");
  
  result += F("   annotation: { annotations: {");
  if (goal > 0)
  {
    result += F("    goalLine: {");
    result += F("     type: 'line',");
    result += F("     yMin: ");
    result += String(goal);
    result += F(", yMax: ");
    result += String(goal);
    result += F(",");
    result += F("     borderColor: 'rgb(255, 0, 0)', borderWidth: 2, borderDash: [5, 5],");
    result += F("     label: { display: true, content: 'Puiss. souscrite: ");
    result += String(goal);
    result += F(" VA', position: 'end', backgroundColor: 'rgba(255, 0, 0, 0.8)', color: 'white' }");
    result += F("    }");
  }
  result += F("   } }");
  result += F("  },");

  // HOVER
  result += F("  onHover: function(event, activeElements) {");
  result += F("   const chart = this;");
  result += F("   event.native.target.style.cursor = activeElements.length > 0 ? 'pointer' : 'default';");
  result += F("   chart.data.datasets.forEach(function(dataset) {");
  result += F("    dataset.borderWidth = dataset.data.map(function() { return 0; });");
  result += F("   });");
  result += F("   if (activeElements.length > 0) {");
  result += F("    const index = activeElements[0].index;");
  result += F("    const label = chart.data.labels[index];");
  result += F("    chart.data.datasets.forEach(function(dataset) {");
  result += F("     dataset.borderWidth = dataset.data.map(function(d, i) { return i === index ? 3 : 0; });");
  result += F("    });");
  result += F("    let totalHeight = 0;");
  result += F("    chart.data.datasets.forEach(function(dataset) {");
  result += F("     const value = dataset.data[index] || 0;");
  result += F("     if (value > 0) totalHeight += value;");
  result += F("    });");
  result += F("    chart.options.plugins.annotation.annotations.hoverArrow = {");
  result += F("     type: 'label', xValue: label, yValue: totalHeight,");
  result += F("     content: '▼', color: '#525252ff', font: { size: 16, weight: 'bold' }, yAdjust: -15");
  result += F("    };");
  result += F("   } else {");
  result += F("    delete chart.options.plugins.annotation.annotations.hoverArrow;");
  result += F("   }");
  result += F("   chart.update('none');");
  result += F("  },");

  // CLIC avec annulation du timer
  result += F("  onClick: function(evt, activeElements) {");
  result += F("   const chart = this;");
  result += F("   if (chart.tooltipTimeout) { clearTimeout(chart.tooltipTimeout); chart.tooltipTimeout = null; }");
  result += F("   if (activeElements.length > 0) {");
  result += F("    if (chart.options.plugins.tooltip.enabled) {");
  result += F("     chart.options.plugins.tooltip.enabled = false;");
  result += F("     chart.tooltip.setActiveElements([], {x: 0, y: 0});");
  result += F("     chart.update();");
  result += F("    } else {");
  result += F("     chart.options.plugins.tooltip.enabled = true;");
  result += F("     chart.tooltip.setActiveElements(activeElements, {x: evt.x, y: evt.y});");
  result += F("     chart.update();");
  result += F("    }");
  result += F("   } else {");
  result += F("    chart.options.plugins.tooltip.enabled = false;");
  result += F("    chart.tooltip.setActiveElements([], {x: 0, y: 0});");
  result += F("    chart.update();");
  result += F("   }");
  result += F("  }");

  result += F(" }");
  result += F("});");

  return result;
}


/*String createEnergyGraph(String IEEE, String Type, String barColor, int budget)
{
  String result = "";
  String unit = "";
  String sep = "";
  result = Type;
  result += F("Chart = Morris.Bar({element: '");
  result += Type;
  result += F("-chart',data: [],xkey: 'y',");
  // list attr
  result += F("ykeys: [");
  String JsonEuros;
  JsonEuros= "{";
  int cntsection;
  int arrayLength = sizeof(section) / sizeof(section[0]);
  if (Type=="energy")
  {
    int i = 0;
    for (cntsection=0 ; cntsection <arrayLength; cntsection++)
    {
      if (i > 0)
      {
        sep = ",";
      }
      else
      {
        sep = "";
      }
      if (section[cntsection]!="1") // on exclut EAIT
      {
        JsonEuros += sep + "\"" + String(section[cntsection]) + "\":{\"name\":\"" + GetNameStatus(97, "0702", String(section[cntsection]).toInt(), "ZLinky_TIC") + "\",\"coeff\":1,\"price\":" + getTarif(String(section[cntsection]).toInt(),"energy") +",\"abo\":"+ConfigGeneral.tarifAbo+",\"taxe\":"+ConfigGeneral.tarifCSPE+",\"unit\":\"Wh\"}";
      } 
      result += sep + String(section[cntsection]);
      i++;
      
    }
    if (strcmp(ConfigGeneral.Production,"")!=0)
    {
      JsonEuros += sep + "\"1\":{\"name\":\"Production\",\"coeff\":1,\"price\":" + getTarif(1,"production") + ",\"unit\":\"Wh\"}";
    }  
    unit = F(" postUnits: ' Wh',");
    
  }else if (Type=="gaz")
  {
      JsonEuros += "\"0\":{\"name\":\"Gaz\",\"coeff\":"+String(ConfigGeneral.coeffGaz)+",\"price\":" + getTarif(0,"gaz") + ",\"unit\":\""+String(ConfigGeneral.unitGaz)+"\"}";
      result += "0"; 
      unit = " postUnits: ' "+String(ConfigGeneral.unitGaz)+"',";

  }else if (Type=="water")
  {
    JsonEuros += sep + "\"0\":{\"name\":\"Water\",\"coeff\":"+String(ConfigGeneral.coeffWater)+",\"price\":" + getTarif(0,"water") + ",\"unit\":\""+String(ConfigGeneral.unitWater)+"\"}";
    result += "0";
    unit = " postUnits: ' "+String(ConfigGeneral.unitWater)+"',";
  }else if (Type=="production")
  {
    JsonEuros += sep + "\"1\":{\"name\":\"Production\",\"coeff\":1,\"price\":" + getTarif(1,"production") + ",\"unit\":\"Wh\"}";
    result += "1";
    unit = F(" postUnits: ' Wh',");
  }
  JsonEuros += "}";
  result += F("],");
  // list name
  result += F("labels: [");
  result += F("],");
  result += F("barColors: ");
  result += barColor;
  result += F(",");
  result += unit;
  result += F("barWidth: '3px',");
  result += F("resize: true,");
  result += F("redraw: true,");
  result += F(" xLabelAngle: 70,");
  result += F("stacked: true,");
  
  if (budget > 0)
  { 
      result += F(" goals : [");
      result += String(budget);
      result +=F("],");
  }

  result += F(" dataLabels: false,");
  result += F(" animate: false,");
  result += F("hoverCallback: function (index, options, content, row) {");
  result += F("return getLabelEnergy('");
  result += JsonEuros;
  result += F("',row,");
  result += barColor;
  result += F(",options,index);");
  result += F("}");
  result += F("});");
  
  return result;
}*/

String createEnergyGraph(String IEEE, String Type, String barColor, int budget)
{
  String result = "";
  String unit = "";
  String sep = "";
  result +=F("var ");
  result +=Type;
  result += F("Chart = document.getElementById('");
  result += Type;
  result += F("-chart');");
  result += F("if (!");
  result += Type;
  result += F("Chart) { console.error('Canvas ");
  result += Type;
  result += F("-chart non trouvé'); return; }");
  result += F("var ctx");
  result += Type;
  result += F(" = ");
  result += Type;
  result += F("Chart.getContext('2d');");
  result += F("if (window.");
  result += Type;
  result += F("Chart) window.");
  result += Type;
  result += F("Chart.destroy();");
  
  // Préparer les informations de tarifs et sections
  String JsonEuros = "{";
  String ykeys = "[";
  String labels = "[";
  String colors = "[";
  
  int arrayLength = sizeof(section) / sizeof(section[0]);
  
  if (Type == "energy")
  {
    int i = 0;
    for (int cntsection = 0; cntsection < arrayLength; cntsection++)
    {
      sep = (i > 0) ? "," : "";
      
      if (section[cntsection] != "1") // Exclure EAIT
      {
        int sectionInt = String(section[cntsection]).toInt();
        // Appel unique au cache — réutilisé pour JsonEuros et labels
        String sectionName = GetNameStatus(97, "0702", sectionInt, "ZLinky_TIC");
        JsonEuros += sep + "\"" + String(section[cntsection]) + "\":{";
        JsonEuros += "\"name\":\"" + sectionName + "\",";
        JsonEuros += "\"coeff\":1,";
        JsonEuros += "\"price\":" + String(getTarif(sectionInt, "energy")) + ",";
        JsonEuros += "\"abo\":" + String(ConfigGeneral.tarifAbo) + ",";
        JsonEuros += "\"taxe\":" + String(ConfigGeneral.tarifCSPE) + ",";
        JsonEuros += "\"taxe2\":" + String(ConfigGeneral.tarifCTA) + ",";
        JsonEuros += "\"unit\":\"Wh\"}";

        ykeys += sep + "'" + String(section[cntsection]) + "'";
        labels += sep + "'" + sectionName + "'";
        i++;
      }
    }
    
    // Ajouter la production si configurée
    if (strcmp(ConfigGeneral.Production, "") != 0)
    {
      sep = (i > 0) ? "," : "";
      JsonEuros += sep + "\"1\":{\"name\":\"Production\",\"coeff\":1,\"price\":" + String(getTarif(1, "production")) + ",\"unit\":\"Wh\"}";
      ykeys += sep + "'1'";
      labels += sep + "'Production'";
      i++;
    }
    
    // Ajouter les sous-compteurs
    for (int sm = 0; sm < ConfigGeneral.subMeterCount; sm++)
    {
      if (!ConfigGeneral.subMeters[sm].enabled) continue;
      if (strlen(ConfigGeneral.subMeters[sm].IEEE) == 0) continue;
      
      sep = (i > 0) ? "," : "";
      String subKey = "sub_" + String(sm);
      
      // Utiliser le tarif moyen (256 = BASE/HC)
      JsonEuros += sep + "\"" + subKey + "\":{";
      JsonEuros += "\"name\":\"" + String(ConfigGeneral.subMeters[sm].alias) + "\",";
      JsonEuros += "\"coeff\":1,";
      JsonEuros += "\"price\":" + String(getTarif(256, "energy")) + ",";
      JsonEuros += "\"color\":\"" + String(ConfigGeneral.subMeters[sm].color) + "\",";
      JsonEuros += "\"unit\":\"Wh\"}";
      
      ykeys += sep + "'" + subKey + "'";
      labels += sep + "'" + String(ConfigGeneral.subMeters[sm].alias) + "'";
      i++;
    }
    
    unit = "Wh";
  }
  else if (Type == "gaz")
  {
    JsonEuros += "\"0\":{";
    JsonEuros += "\"name\":\"Gaz\",";
    JsonEuros += "\"coeff\":" + String(ConfigGeneral.coeffGaz) + ",";
    JsonEuros += "\"price\":" + String(getTarif(0, "gaz")) + ",";
    JsonEuros += "\"unit\":\"" + String(ConfigGeneral.unitGaz) + "\"}";
    
    ykeys += "'0'";
    labels += "'Gaz'";
    unit = String(ConfigGeneral.unitGaz);
  }
  else if (Type == "water")
  {
    JsonEuros += "\"0\":{";
    JsonEuros += "\"name\":\"Eau\",";
    JsonEuros += "\"coeff\":" + String(ConfigGeneral.coeffWater) + ",";
    JsonEuros += "\"price\":" + String(getTarif(0, "water")) + ",";
    JsonEuros += "\"unit\":\"" + String(ConfigGeneral.unitWater) + "\"}";
    
    ykeys += "'0'";
    labels += "'Eau'";
    unit = String(ConfigGeneral.unitWater);
  }
  else if (Type == "production")
  {
    JsonEuros += "\"1\":{";
    JsonEuros += "\"name\":\"Production\",";
    JsonEuros += "\"coeff\":1,";
    JsonEuros += "\"price\":" + String(getTarif(1, "production")) + ",";
    JsonEuros += "\"unit\":\"Wh\"}";
    
    ykeys += "'1'";
    labels += "'Production'";
    unit = "Wh";
  }
  
  JsonEuros += "}";
  ykeys += "]";
  labels += "]";
  
  // Exposer les infos comme variables globales
  result += F("window.");
  result += Type;
  result += F("TarifInfo = ");
  result += JsonEuros;
  result += F(";");
  
  result += F("window.");
  result += Type;
  result += F("Keys = ");
  result += ykeys;
  result += F(";");
  
  result += F("window.");
  result += Type;
  result += F("Budget = ");
  result += String(budget);
  result += F(";");
  
  result += F("window.");
  result += Type;
  result += F("Period = 'hour';");

  // Créer le graphique Chart.js
  result += F("window.");
  result += Type;
  result += F("Chart = new Chart(ctx");
  result += Type;
  result +=F(", {");
  result += F(" type: 'bar',");
  result += F(" data: {");
  result += F("  labels: [],");
  result += F("  datasets: []"); // Sera rempli dynamiquement
  result += F(" },");
  result += F(" options: {");
  result += F("  responsive: true,");
  result += F("  maintainAspectRatio: false,");
  result += F("  animation: false,");
  result += F("  interaction: {");
  result += F("   mode: 'index',"); 
  result += F("   intersect: false,"); 
  result += F("   events: ['mousemove', 'mouseout', 'click']");
  result += F("  },");
  result += F("  scales: {");
  result += F("   x: {");
  result += F("    stacked: true,");
  result += F("    ticks: { maxRotation: 70, minRotation: 70, autoSkip: true }");
  result += F("   },");
  result += F("   y: {");
  result += F("    stacked: true,");
  result += F("    beginAtZero: true,");
  result += F("    grace: '10%',");
  result += F("    ticks: { callback: function(value) { return value + ' ");
  result += unit;
  result += F("'; } }");
  result += F("   }");
  result += F("  },");
  result += F("  barPercentage: 0.9,");
  result += F("  categoryPercentage: 0.95,");
  result += F("  plugins: {");
  result += F("   tooltip: {");
  result += F("    enabled: false,"); // ← Garder enabled
  result += F("    mode: 'index',");
  result += F("    intersect: false,");
  result += F("    backgroundColor: 'rgba(133, 133, 133, 0.65)',");
  result += F("    callbacks: {");
  result += F("     title: function(tooltipItems) {");
  result += F("      return tooltipItems.length > 0 ? tooltipItems[0].label : '';");
  result += F("     },");
  result += F("     label: function(context) {");
  result += F("      return getEnergyTooltipLabel(context, '");
  result += Type;
  result += F("');");
  result += F("     },");
  result += F("     footer: function(tooltipItems) {");
  result += F("      return getEnergyTooltipFooter(tooltipItems, '");
  result += Type;
  result += F("');");
  result += F("     }");
  result += F("    }");
  result += F("   },");
  result += F("   legend: { display: true, position: 'top' }");
  
  // Ajouter la ligne de budget si défini
  if (budget > 0)
  {
    result += F(",");
    result += F("   annotation: {");
    result += F("    annotations: {");
    result += F("     budgetLine: {");
    result += F("      type: 'line',");
    result += F("      yMin: ");
    result += String(budget);
    result += F(",");
    result += F("      yMax: ");
    result += String(budget);
    result += F(",");
    result += F("      borderColor: 'rgba(162, 0, 255, 1)',");
    result += F("      borderWidth: 2,");
    result += F("      borderDash: [5, 5],");
    result += F("      label: {");
    result += F("       display: true,");
    result += F("       content: 'Budget: ");
    result += String(budget);
    result += F(" ");
    result += unit;
    result += F("',");
    result += F("       position: 'end',");
    result += F("       backgroundColor: 'rgba(162, 0, 255, 1)',");
    result += F("       color: 'white'");
    result += F("      }");
    result += F("     }");
    result += F("    }");
    result += F("   }");
  }
  result += F("  },"); // Fermeture plugins

  // ← HOVER : Bordure sur barre survolée
  result += F("  onHover: function(event, activeElements) {");
  result += F("   const chart = this;");
  result += F("   event.native.target.style.cursor = activeElements.length > 0 ? 'pointer' : 'default';");

  result += F("   chart.data.datasets.forEach(function(dataset) {");
  result += F("    dataset.borderWidth = dataset.data.map(function() { return 0; });");
  result += F("   });");

  result += F("   if (activeElements.length > 0) {");
  result += F("    const index = activeElements[0].index;");
  result += F("    chart.data.datasets.forEach(function(dataset) {");
  result += F("     dataset.borderWidth = dataset.data.map(function(d, i) {");
  result += F("      return i === index ? 3 : 0;");
  result += F("     });");
  result += F("    });");
  result += F("   }");

  result += F("   chart.update('none');");
  result += F("  },");

  // ← CLIC : Tooltip 5 secondes
  result += F("  onClick: function(evt, activeElements) {");
  result += F("   const chart = this;");
  result += F("   if (chart.tooltipTimeout) {");
  result += F("    clearTimeout(chart.tooltipTimeout);");
  result += F("   }");

  result += F("   if (activeElements.length > 0) {");
  result += F("    chart.options.plugins.tooltip.enabled = true;");
  result += F("    chart.tooltip.setActiveElements(activeElements, {x: evt.x, y: evt.y});");
  result += F("    chart.update();");

  /*result += F("    chart.tooltipTimeout = setTimeout(function() {");
  result += F("     chart.options.plugins.tooltip.enabled = false;");
  result += F("     chart.tooltip.setActiveElements([], {x: 0, y: 0});");
  result += F("     chart.update();");
  result += F("    }, 5000);");*/

  result += F("   } else {");
  result += F("    chart.options.plugins.tooltip.enabled = false;");
  result += F("    chart.tooltip.setActiveElements([], {x: 0, y: 0});");
  result += F("    chart.update();");
  result += F("   }");
  result += F("  }");

  result += F(" }"); // Fermeture options
  result += F("});");

  return result;
}

// === Garde mémoire pour les handlers HTML ===
// Retourne false (et envoie 503) si le heap est trop bas pour servir une page
bool checkHeapForPage(AsyncWebServerRequest *request) {
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 50000) {
        Serial.printf("[WebServer] Heap trop bas (%u) - 503 renvoye\n", freeHeap);
        request->send(503, "text/html",
            "<html><head><meta charset='utf-8'><meta http-equiv='refresh' content='3'>"
            "</head><body style='font-family:sans-serif;text-align:center;padding:40px;'>"
            "<h2>Serveur temporairement surcharg&eacute;</h2>"
            "<p>M&eacute;moire insuffisante. Rechargement automatique...</p>"
            "</body></html>");
        return false;
    }
    return true;
}

// === Helper pour streamer un bloc PROGMEM avec remplacement FormattedDate ===
void streamSection(AsyncResponseStream *response, const char *progmem) {
    String section = FPSTR(progmem);
    section.replace("{{FormattedDate}}", FormattedDate);
    response->print(section);
}

void handleNotFound(AsyncWebServerRequest *request)
{

  String message = F("File Not Found\n\n");
  message += F("URI: ");
  // message += serverWeb.uri();
  message += request->url();
  message += F("\nMethod: ");
  // message += (serverWeb.method() == HTTP_GET) ? "GET" : "POST";
  message += (request->method() == HTTP_GET) ? "GET" : "POST";
  message += F("\nArguments: ");
  // message += serverWeb.args();
  message += request->args();
  message += F("\n");

  for (uint8_t i = 0; i < request->args(); i++)
  {
    message += " " + request->argName(i) + ": " + request->arg(i) + "\n";
  }

  request->send(404, F("text/plain"), message);
  // serverWeb.send(404, F("text/plain"), message);
  /*for (uint8_t i = 0; i < serverWeb.args(); i++) {
    message += " " + serverWeb.argName(i) + ": " + serverWeb.arg(i) + "\n";
  }

  serverWeb.send(404, F("text/plain"), message);*/
}

/*void handleRoot(AsyncWebServerRequest *request)
{
  AsyncResponseStream *response = request->beginResponseStream("text/html");
  String result;
  result = F("<html>");
  result += FPSTR(HTTP_HEADERGRAPH);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_ROOT);
  result.replace("{{FormattedDate}}", FormattedDate);
  
  int i = 0;
  String time;
  int paramsNr = request->params();
  if (paramsNr > 0)
  {
    time = request->arg(i);
  }
  else
  {
    time = "hour";
  }

  if (time == "hour")
  {
    result.replace("{{selectedHour}}", F("selected"));
    result.replace("{{selectedDay}}", F(""));
    result.replace("{{selectedMonth}}", F(""));
    result.replace("{{selectedYear}}", F(""));
  }
  else if (time == "day")
  {
    result.replace("{{selectedHour}}", F(""));
    result.replace("{{selectedDay}}", F("selected"));
    result.replace("{{selectedMonth}}", F(""));
    result.replace("{{selectedYear}}", F(""));
  }
  else if (time == "month")
  {
    result.replace("{{selectedHour}}", F(""));
    result.replace("{{selectedDay}}", F(""));
    result.replace("{{selectedMonth}}", F("selected"));
    result.replace("{{selectedYear}}", F(""));
  }
  else if (time == "year")
  {
    result.replace("{{selectedHour}}", F(""));
    result.replace("{{selectedDay}}", F(""));
    result.replace("{{selectedMonth}}", F(""));
    result.replace("{{selectedYear}}", F("selected"));
  }
  response->print(result);
  String dashboard = "";
  String js = "";
  File root = LittleFS.open("/db");
  File file = root.openNextFile();
  while (file)
  {
    String tmp = file.name();
    if (tmp.substring(16) == ".json")
    {
      if (existDashboard(tmp))
      {
        int ShortAddr = GetShortAddr(file.name());
        int DeviceId = GetDeviceId(file.name());
        String model;
        model = GetModel(file.name());
        dashboard += F("<div class='col-sm-3'><div class='card'><div class='card-header'>");
        String alias = getAliasDashboard(file.name());

        if (alias != "null")
        {
          dashboard += F("<strong>");
          dashboard += alias;
          dashboard += F("</strong>");
          dashboard += F("<br>(@Mac : ");
          dashboard += tmp.substring(0, 16);
          dashboard += F(")");
        }
        else
        {
          dashboard += F("@Mac : ");
          dashboard += tmp.substring(0, 16);
        }
        dashboard += F("</div>");
        dashboard += F("<div class='card-body'>");
        // Get status and action from json

        if (TemplateExist(DeviceId))
        {
          Template t;
          t = GetTemplate(DeviceId, model);
          // toutes les propiétés
          dashboard += F("<div id='status_");
          dashboard += (String)ShortAddr;
          dashboard += F("'>");

          for (int i = 0; i < t->StateSize; i++)
          {
            if (t->e[i].visible)
            {
              if (String(t->e[i].typeJauge) == "gauge")
              {
                js += createGaugeDashboard((String)ShortAddr, (String)i, String(t->e[i].jaugeMin), String(t->e[i].jaugeMax), t->e[i].unit);
                js += CreateTimeGauge((String)ShortAddr + (String)i);
                js += "refreshGauge" + (String)ShortAddr + (String)i + "('" + tmp.substring(0, 16) + "'," + t->e[i].cluster + "," + t->e[i].attribute + ",'" + t->e[i].type + "'," + t->e[i].coefficient + ");";
              }
              else
              {
                dashboard += t->e[i].name;
                dashboard += " : <span id='";
                dashboard += F("label_");
                dashboard += (String)ShortAddr;
                dashboard += F("_");
                dashboard += t->e[i].cluster;
                dashboard += F("_");
                dashboard += t->e[i].attribute;
                dashboard += F("'>");
                dashboard += GetValueStatus(file.name(), t->e[i].cluster, t->e[i].attribute, (String)t->e[i].type, t->e[i].coefficient, (String)t->e[i].unit);
                dashboard += F("</span><br>");
                js += "refreshLabel('"+String(file.name())+"','"+(String)ShortAddr+"',"+t->e[i].cluster+","+t->e[i].attribute+",'"+(String)t->e[i].type+"',"+t->e[i].coefficient+",'"+(String)t->e[i].unit+"');";
              }
            }
          }
          dashboard += F("</div>");
          dashboard += F("<div id='action_");
          dashboard += (String)ShortAddr;
          dashboard += F("'>");
          // toutes les actions

          for (int i = 0; i < t->ActionSize; i++)
          {
            if (t->a[i].visible)
            {
              dashboard += F("<button onclick=\"ZigbeeAction(");
              dashboard += ShortAddr;
              dashboard += ",";
              dashboard += t->a[i].command;
              dashboard += ",";
              dashboard += t->a[i].value;
              dashboard += ");\" class='btn btn-primary mb-2'>";
              dashboard += t->a[i].name;
              dashboard += F("</button>");
            }
          }
          dashboard += F("</div>");
        }
        dashboard += F("</div></div></div>");
      }
    }
    file = root.openNextFile();
  }
  file.close();
  root.close();
  result=F("<div class='row'>");
  result+=dashboard; 
  result+=F("</div>");
  response->print(result);

  String javascript = "";
  javascript = F("<script language='javascript'>");
  javascript += F("$(document).ready(function() {");
  javascript += F("loadPowerGaugeAbo('");
  javascript += String(ConfigGeneral.ZLinky);
  javascript += F("','1295','");
  javascript += time;
  javascript += F("');");
  javascript += F("refreshDashboard('");
  javascript += String(ConfigGeneral.ZLinky);
  javascript += F("','1295','");
  javascript += time;
  javascript += F("');");
  javascript += js;
  javascript += F("});");

  javascript += F("</script>");
  response->print(javascript);

  result = FPSTR(HTTP_FOOTER);
  result += F("</html>");
  response->print(result);
  request->send(response);

}*/



void handleRoot(AsyncWebServerRequest *request)
{
  if (sizeof(ConfigSettings.ssid)==0)
  {
    // rediriger vers config Wifi assist
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/configWiFi"));
    request->send(response);
  }else{
    if (devices.size()==0)
    {
      //rediriger vers page jumelage assist
      AsyncWebServerResponse *response = request->beginResponse(303);
      response->addHeader(F("Location"), F("/assistDevice"));
      request->send(response);
    }else{
      //rediriger vers page energy
      AsyncWebServerResponse *response = request->beginResponse(303);
      response->addHeader(F("Location"), F("/statusEnergy"));
      request->send(response);
    }
  }

}

void handleDashboard(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;

  AsyncResponseStream *response = request->beginResponseStream("text/html");

  response->print(F("<html>"));
  response->print(FPSTR(HTTP_HEADERGRAPH));
  streamSection(response, HTTP_MENU);

  // Début conteneur dashboard (inline au lieu de HTTP_DASHBOARD pour éviter template replace)
  response->print(F("<div class='container py-4' >"));
  response->print(F("<h4>Dashboard</h4>"));
  response->print(F("<div class='row justify-content-start gx-4 gy-4' id='masonry-grid'>"));

  String js = "";
  int exist = 0;

  for (size_t ident = 0; ident < devices.size(); ident++)
  {
    esp_task_wdt_reset();
    DeviceData* device = devices[ident];

    int ShortAddr = device->getInfo().shortAddr.toInt();
    int DeviceId = device->getInfo().device_id.toInt();
    String model = device->getInfo().model;

    response->print(F("<div class='col-12 col-sm-12 col-md-12 col-lg-5 col-xl-4 d-flex'>"));
    response->print(F("<div class='card p-4 flex-fill' style='min-width:380px;'>"));
    response->print(F("<h5 class='card-title' >"));
    response->print(F("@Mac : "));
    response->print(device->getDeviceID());
    response->print(F("</h5>"));
    response->print(F("<div class='card-body'>"));

    if (TemplateExist(DeviceId))
    {
      TemplateData* t = device->getTemplate();
      if (!t) {
          Serial.printf("WARNING: Template introuvable pour model: %s\n", model.c_str());
          continue;
      }
      response->print(F("<div id='status_"));
      response->print(ShortAddr);
      response->print(F("'>"));

      for (int i = 0; i < t->StateSize(); i++)
      {
        if (t->states[i].visible)
        {
          String sa = String(ShortAddr);
          String si = String(i);
          String sai = sa + si;

          if (String(t->states[i].typeJauge) == "gauge")
          {
            exist++;
            response->printf("<div id='gauge_%s' style='height:150px;'><div align='center' style='font-size:12px;margin-top:-70px;'>%s</div></div>", sai.c_str(), t->states[i].name);
            js += createGaugeDashboard(sa, si, String(t->states[i].jaugeMin), String(t->states[i].jaugeMax), t->states[i].unit);
            js += CreateTimeGauge(sai);
            js += "refreshGauge" + sai + "('" + device->getDeviceID() + "'," + t->states[i].cluster + "," + t->states[i].attribute + ",'" + t->states[i].type + "'," + t->states[i].coefficient + ");";
          }
          else if(String(t->states[i].typeJauge) == "battery")
          {
            exist++;
            response->printf("<div id='gauge_%s' style='height:150px;'><div align='center' style='font-size:12px;margin-top:-70px;'>%s</div></div>", sai.c_str(), t->states[i].name);
            js += createBaterryDashboard(sa, si, String(t->states[i].jaugeMin), String(t->states[i].jaugeMax), t->states[i].unit);
            js += CreateTimeGauge(sai);
            js += "refreshGauge" + sai + "('" + device->getDeviceID() + "'," + t->states[i].cluster + "," + t->states[i].attribute + ",'" + t->states[i].type + "'," + t->states[i].coefficient + ");";
          }else if(String(t->states[i].typeJauge) == "text")
          {
            exist++;
            response->printf("<div id='text_%s' style='text-align:center;font-size:12px;'>%s<br>", sai.c_str(), t->states[i].name);
            response->printf("<span id='label_%d_%d_%d' style='font-size:24px;font-family:\"Courier New\",Courier,monospace;'>",
              ShortAddr, t->states[i].cluster, t->states[i].attribute);
            response->print(GetValueStatus(device->getDeviceID(), t->states[i].cluster, t->states[i].attribute, (String)t->states[i].type, t->states[i].coefficient));
            response->print(F("</span>&nbsp;"));
            response->print(t->states[i].unit);
            response->print(F("</div><br>"));
            js += "refreshLabel('"+String(device->getDeviceID())+"','"+sa+"',"+t->states[i].cluster+","+t->states[i].attribute+",'"+t->states[i].type+"',"+t->states[i].coefficient+",'"+t->states[i].unit+"');";
          }
        }
      }
      response->print(F("</div>"));
      response->print(F("<div id='action_"));
      response->print(ShortAddr);
      response->print(F("'>"));

      for (int i = 0; i < t->ActionSize(); i++)
      {
        if (t->actions[i].visible)
        {
          exist++;
          response->printf("<button onclick=\"ZigbeeAction(%d,%d,%d,%d",
            ShortAddr, t->actions[i].command, t->actions[i].endpoint, t->actions[i].value);
          if (t->actions[i].command == 400) {
            response->printf(",%d,%d", t->actions[i].cluster, t->actions[i].manufacturerCode);
          }
          response->print(F(");\" class='btn btn-primary mb-2'>"));
          response->print(t->actions[i].name);
          response->print(F("</button>&nbsp;"));
        }
      }
      response->print(F("</div>"));
    }
    response->print(F("</div></div></div>"));

    vTaskDelay(1);
  }

  if (exist == 0)
  {
    response->print(F("<div align='center' style='font-size:28px;font-weight:bold;'>No dashboard datas yet</div>"));
  }

  response->print(F("</div></div>")); // fin row + container

  // JavaScript
  response->print(F("<script language='javascript'>$(document).ready(function(){"));
  response->print(js);
  response->print(F(
    "const grid=document.querySelector('#masonry-grid');"
    "const msnry=new Masonry(grid,{itemSelector:'.col-12',percentPosition:true});"
    "const observer=new ResizeObserver(()=>{msnry.layout();});"
    "document.querySelectorAll('.col-12').forEach(card=>observer.observe(card));"
  ));
  response->print(F("});</script>"));

  response->print(footer());
  response->print(F("</html>"));

  request->send(response);
}



/*void handleDashboard(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADERGRAPH);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_DASHBOARD);
  result +=footer();
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);

  String time;

  String dashboard = "";
  String js = "";
  File root = LittleFS.open("/db");
  File file = root.openNextFile();
  int exist = 0;
  while (file)
  {
    String tmp = file.name();
    if (tmp.substring(16) == ".json")
    {
      int ShortAddr = GetShortAddr(file.name());
      int DeviceId = GetDeviceId(file.name());
      String model;
      model = GetModel(file.name());
      dashboard += F("<div class='col-md-auto col-sm-auto'>");
      dashboard += F("<div class='card' style='min-width:380px;'>");
      dashboard += F("<div class='card-header' style='font-size:12px;font-weight:bold;color:#FFF;background-color:#007bc6;'>");
      String alias = getAliasDashboard(file.name());

      if (alias != "null")
      {
        dashboard += F("<strong>");
        dashboard += alias;
        dashboard += F("</strong>");
        dashboard += F("<br>(@Mac : ");
        dashboard += tmp.substring(0, 16);
        dashboard += F(")");
      }
      else
      {
        dashboard += F("@Mac : ");
        dashboard += tmp.substring(0, 16);
      }
      dashboard += F("</div>");
      dashboard += F("<div class='card-body'>");
      // Get status and action from json

      if (TemplateExist(DeviceId))
      {
        Template *t;
        t = GetTemplate(DeviceId, model);
        // toutes les propiétés
        dashboard += F("<div id='status_");
        dashboard += (String)ShortAddr;
        dashboard += F("'>");

        for (int i = 0; i < t->StateSize; i++)
        {
          if (t->e[i].visible)
          {

            if (String(t->e[i].typeJauge) == "gauge")
            {
              exist++;
              dashboard += "<div id='gauge_";
              dashboard += (String)ShortAddr+String(i);
              dashboard += F("' style='height:150px;'>");
              dashboard += F("<div align='center' style='font-size:12px;margin-top:-70px;'>");
              dashboard += String(t->e[i].name);
              dashboard += F("</div>");
              dashboard += F("</div>");
              js += createGaugeDashboard((String)ShortAddr, (String)i, String(t->e[i].jaugeMin), String(t->e[i].jaugeMax), t->e[i].unit);
              js += CreateTimeGauge((String)ShortAddr + (String)i);
              js += "refreshGauge" + (String)ShortAddr + (String)i + "('" + tmp.substring(0, 16) + "'," + t->e[i].cluster + "," + t->e[i].attribute + ",'" + t->e[i].type + "'," + t->e[i].coefficient + ");";
            }
            else if(String(t->e[i].typeJauge) == "battery")
            {
              exist++;
              dashboard += "<div id='gauge_";
              dashboard += (String)ShortAddr+String(i);
              dashboard += F("' style='height:150px;'>");
              dashboard += F("<div align='center' style='font-size:12px;margin-top:-70px;'>");
              dashboard += String(t->e[i].name);
              dashboard += F("</div>");
              dashboard += F("</div>");
              js += createBaterryDashboard((String)ShortAddr, (String)i, String(t->e[i].jaugeMin), String(t->e[i].jaugeMax), t->e[i].unit);
              js += CreateTimeGauge((String)ShortAddr + (String)i);
              js += "refreshGauge" + (String)ShortAddr + (String)i + "('" + tmp.substring(0, 16) + "'," + t->e[i].cluster + "," + t->e[i].attribute + ",'" + t->e[i].type + "'," + t->e[i].coefficient + ");";
            }else if(String(t->e[i].typeJauge) == "text")
            {
              exist++;
              dashboard +=F("<div id='text_");
              dashboard += (String)ShortAddr+String(i);
              dashboard += F("' style='text-align:center;font-size:12px;'>");
              dashboard += t->e[i].name;
              dashboard += F("<br>");
              dashboard += "<span id='";
              dashboard += F("label_");
              dashboard += (String)ShortAddr;
              dashboard += F("_");
              dashboard += t->e[i].cluster;
              dashboard += F("_");
              dashboard += t->e[i].attribute;
              dashboard += F("' style='font-size:24px;font-family :\"Courier New\", Courier, monospace;'>");
              dashboard += GetValueStatus(file.name(), t->e[i].cluster, t->e[i].attribute, (String)t->e[i].type, t->e[i].coefficient);             
              dashboard += F("</span>&nbsp;");
              dashboard += String(t->e[i].unit);
              dashboard += F("</div><br>");
              js += "refreshLabel('"+String(file.name())+"','"+(String)ShortAddr+"',"+t->e[i].cluster+","+t->e[i].attribute+",'"+(String)t->e[i].type+"',"+t->e[i].coefficient+",'"+(String)t->e[i].unit+"');";

            }
          }
        }
        dashboard += F("</div>");
        dashboard += F("<div id='action_");
        dashboard += (String)ShortAddr;
        dashboard += F("'>");
        // toutes les actions

        for (int i = 0; i < t->ActionSize; i++)
        {
          if (t->a[i].visible)
          {
            exist++;
            dashboard += F("<button onclick=\"ZigbeeAction(");
            dashboard += ShortAddr;
            dashboard += ",";
            dashboard += t->a[i].command;
            dashboard += ",";
            dashboard += t->a[i].value;
            dashboard += ");\" class='btn btn-primary mb-2'>";
            dashboard += t->a[i].name;
            dashboard += F("</button>&nbsp;");
          }
        }
        dashboard += F("</div>");
        free(t);
      }
      dashboard += F("</div></div></div>");
      
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  file.close();
  root.close();

  if (exist>0)
  {
    result.replace("{{dashboard}}", dashboard);
  }else{
    result.replace("{{dashboard}}", "<div align='center' style='font-size:28px;font-weight:bold;'>No dashboard datas yet</div>");
  }
  

  String javascript = "";
  javascript = F("<script language='javascript'>");
  javascript += F("$(document).ready(function() {");

  javascript += js;
  javascript += F("});");

  javascript += F("</script>");
  result.replace("{{javascript}}", javascript);

  request->send(200, "text/html", result);
}*/

void handleStatusNetwork(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  PSRAMString result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_NETWORK);
  result += footer();
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);

  result.replace("{{enableWifi}}", F("<img src='/web/img/ok.png'>"));
  result.replace("{{ssidWifi}}", String(ConfigSettings.ssid));
  if (ConfigSettings.enableDHCP)
  {
    result.replace("{{modeWifi}}", "DHCP");
    result.replace("{{ipWifi}}", WiFi.localIP().toString());
    result.replace("{{maskWifi}}", WiFi.subnetMask().toString());
    result.replace("{{GWWifi}}", WiFi.gatewayIP().toString());
  }else{
    result.replace("{{modeWifi}}", "Static");
    result.replace("{{ipWifi}}", ConfigSettings.ipAddressWiFi);
    result.replace("{{maskWifi}}", ConfigSettings.ipMaskWiFi);
    result.replace("{{GWWifi}}", ConfigSettings.ipGWWiFi);
  }

  // Signal WiFi
  if (WiFi.isConnected()) {
    int rssi = WiFi.RSSI();
    int quality = 0;
    if (rssi >= -50) quality = 100;
    else if (rssi <= -100) quality = 0;
    else quality = 2 * (rssi + 100);
    String rssiStr = String(rssi) + " dBm (" + String(quality) + "%)";
    result.replace("{{rssiWifi}}", rssiStr);
    result.replace("{{txPowerWifi}}", String((float)WiFi.getTxPower() / 4.0, 1) + " dBm");
  } else {
    result.replace("{{rssiWifi}}", "-");
    result.replace("{{txPowerWifi}}", "-");
  }

  if (ConfigSettings.connectedWifiSta)
  {
    result.replace("{{connectedWifi}}", F("<img src='/web/img/ok.png'>"));
  }
  else
  {
    result.replace("{{connectedWifi}}", F("<img src='/web/img/nok.png'>"));
  }
  
  if (ConfigSettings.enableMqtt)
  {
    String MqttCard = F("<i>Infos MQTT :</i>");
    MqttCard +=F("<br>");
    MqttCard +=F("<Strong>MQTT connecté :</strong> ");
    if (mqttClient.connected())
    {
      MqttCard +=F("<img src='/web/img/ok.png'>");
    }else{
      MqttCard +=F("<img src='/web/img/nok.png'>");
    }
    MqttCard +=F(" <br>");
    MqttCard +=F("<Strong>MQTT serv :</strong> ");
    MqttCard +=ConfigGeneral.servMQTT;
    MqttCard +=F(" <br>");
    MqttCard +=F("<Strong>MQTT port :</strong> ");
    MqttCard +=ConfigGeneral.portMQTT;
    MqttCard +=F(" <br><br>");

    result.replace("{{MQTT card}}", MqttCard);
  }else{
    result.replace("{{MQTT card}}", "");
  }

  float val;
  float Voltage = 0.0;
  val = analogRead(VOLTAGE);
  Voltage = (val * 5300) / 4095;
  result.replace("{{Voltage}}", String(Voltage / 1000));

  float temperature = 0;
  temperature = temperatureReadFixed();
  result.replace("{{Temperature}}", String(temperature));

  result += F("<script>"
    "setInterval(function(){"
      "fetch('/api/wifiSignal').then(function(r){return r.json();}).then(function(d){"
        "if(d.rssi){"
          "document.getElementById('rssiVal').textContent=d.rssi+' dBm ('+d.quality+'%)';"
          "document.getElementById('txPwrVal').textContent=d.txPower.toFixed(1)+' dBm';"
        "}"
      "}).catch(function(){});"
    "},5000);"
  "</script>");

  request->send(200, "text/html", result.c_str());
}

void handleAPITariff(AsyncWebServerRequest *request)
{
  String json = "{";
    
  String currentTariff = "---";
  String tariffBgColor = "#2980b9";
  String tempoTodayClass = "tempo-undef";
  String tempoTodayLabel = "Non défini";
  String tempoTodayIcon = "?";
  String tempoTomorrowClass = "tempo-undef";
  String tempoTomorrowLabel = "Non défini";
  String tempoTomorrowIcon = "-";
  bool isTempo = false;
  bool isHP = false;
  bool showCard = false;

  // Chercher le device ZLinky
  DeviceData* device = nullptr;
  for (size_t i = 0; i < devices.size(); i++) {
    if (devices[i]->getDeviceID() == String(ConfigGeneral.ZLinky)) {
      device = devices[i];
      break;
    }
  }

  if (device != nullptr)
  {
    showCard = true;
    String tarif = "";
    
    // =====================================================
    // Mode Historique (LinkyMode 0 ou 2)
    // =====================================================
    if ((ConfigGeneral.LinkyMode == 0) || (ConfigGeneral.LinkyMode == 2))
    {
      // PTEC = Cluster 0x0702 attribut 32 (0x0020)
      tarif = device->getValue("0702", "32");
      tarif.trim();
      
      if (tarif.length() > 0)
      {
        currentTariff = tarif;
        
        // Déterminer si c'est HP ou HC
        String tarifUpper = tarif;
        tarifUpper.toUpperCase();
        isHP = (tarifUpper.indexOf("HP") >= 0) || (tarifUpper.indexOf("PLEINE") >= 0);
        
        // Mapping des codes PTEC vers des noms lisibles
        if (currentTariff.startsWith("TH")) {
          currentTariff = "Base";
          tariffBgColor = "#2980b9";
        }
        else if (currentTariff == "HC..") {
          currentTariff = "Heures Creuses";
          tariffBgColor = "#2980b9";
        }
        else if (currentTariff == "HP..") {
          currentTariff = "Heures Pleines";
          tariffBgColor = "#154360";
        }
        else if (currentTariff == "HN..") {
          currentTariff = "Heures Normales";
          tariffBgColor = "#2980b9";
        }
        else if (currentTariff == "PM..") {
          currentTariff = "Pointe Mobile";
          tariffBgColor = "#c0392b";
        }
        
        // Tempo via PTEC
        if (tarif.startsWith("HCJB") || tarif.startsWith("HPJB")) {
          isTempo = true;
          currentTariff = tarif.startsWith("HC") ? "HC Bleu" : "HP Bleu";
          tariffBgColor = tarif.startsWith("HC") ? "#2980b9" : "#154360";
          tempoTodayClass = "tempo-bleu";
          tempoTodayLabel = "Jour Bleu";
          tempoTodayIcon = "B";
        }
        else if (tarif.startsWith("HCJW") || tarif.startsWith("HPJW")) {
          isTempo = true;
          currentTariff = tarif.startsWith("HC") ? "HC Blanc" : "HP Blanc";
          tariffBgColor = tarif.startsWith("HC") ? "#7f8c8d" : "#000000";
          tempoTodayClass = "tempo-blanc";
          tempoTodayLabel = "Jour Blanc";
          tempoTodayIcon = "W";
        }
        else if (tarif.startsWith("HCJR") || tarif.startsWith("HPJR")) {
          isTempo = true;
          currentTariff = tarif.startsWith("HC") ? "HC Rouge" : "HP Rouge";
          tariffBgColor = tarif.startsWith("HC") ? "#e74c3c" : "#c0392b";
          tempoTodayClass = "tempo-rouge";
          tempoTodayLabel = "Jour Rouge";
          tempoTodayIcon = "R";
        }
        
        // DEMAIN (cluster FF66 attribut 1)
        if (isTempo)
        {
          String demain = device->getValue("FF66", "1");
          if (demain.length() > 0)
          {
            demain.trim();
            demain.toUpperCase();
            
            if (demain.startsWith("BLEU")) {
              tempoTomorrowClass = "tempo-bleu";
              tempoTomorrowLabel = "Jour Bleu";
              tempoTomorrowIcon = "B";
            } else if (demain.startsWith("BLAN")) {
              tempoTomorrowClass = "tempo-blanc";
              tempoTomorrowLabel = "Jour Blanc";
              tempoTomorrowIcon = "W";
            } else if (demain.startsWith("ROUG")) {
              tempoTomorrowClass = "tempo-rouge";
              tempoTomorrowLabel = "Jour Rouge";
              tempoTomorrowIcon = "R";
            }
          }
        }
      }
    }
    // =====================================================
    // Mode Standard (LinkyMode 1, 3 ou 7)
    // =====================================================
    else
    {
      // Essayer cluster FF66 attribut 16 (0x10) d'abord
      tarif = device->getValue("FF66", "16");
      tarif.trim();
      
      // FALLBACK : Si vide, utiliser LTARF (attribut 512)
      if (tarif.length() == 0)
      {
        tarif = device->getValue("FF66", "512");
        tarif.trim();
      }
      
      if (tarif.length() > 0)
      {
        currentTariff = tarif;
        
        // Déterminer si c'est HP ou HC
        String tarifUpper = tarif;
        tarifUpper.toUpperCase();
        isHP = (tarifUpper.indexOf("HP") >= 0) || (tarifUpper.indexOf("PLEINE") >= 0);
      }
      
      // Couleur par défaut selon HP/HC
      tariffBgColor = isHP ? "#154360" : "#2980b9";
      
      // Récupérer STGE (attribut 535) pour les couleurs Tempo
      String stge = device->getValue("FF66", "535");
      if (stge.length() >= 8)
      {
        auto status = parseStatusRegister(stge);
        
        if (status.tempo_jour != "UNDEF")
        {
          isTempo = true;
          
          if (status.tempo_jour == "BLEU") {
            tempoTodayClass = "tempo-bleu";
            tempoTodayLabel = "Jour Bleu";
            tempoTodayIcon = "B";
            tariffBgColor = isHP ? "#154360" : "#2980b9";
          } else if (status.tempo_jour == "BLANC") {
            tempoTodayClass = "tempo-blanc";
            tempoTodayLabel = "Jour Blanc";
            tempoTodayIcon = "W";
            tariffBgColor = isHP ? "#000000" : "#7f8c8d";
          } else if (status.tempo_jour == "ROUGE") {
            tempoTodayClass = "tempo-rouge";
            tempoTodayLabel = "Jour Rouge";
            tempoTodayIcon = "R";
            tariffBgColor = isHP ? "#c0392b" : "#e74c3c";
          }
          
          if (status.tempo_demain == "BLEU") {
            tempoTomorrowClass = "tempo-bleu";
            tempoTomorrowLabel = "Jour Bleu";
            tempoTomorrowIcon = "B";
          } else if (status.tempo_demain == "BLANC") {
            tempoTomorrowClass = "tempo-blanc";
            tempoTomorrowLabel = "Jour Blanc";
            tempoTomorrowIcon = "W";
          } else if (status.tempo_demain == "ROUGE") {
            tempoTomorrowClass = "tempo-rouge";
            tempoTomorrowLabel = "Jour Rouge";
            tempoTomorrowIcon = "R";
          }
        }
      }
    }
  }

  // Construire le JSON
  json += "\"show\":" + String(showCard ? "true" : "false") + ",";
  json += "\"tariff\":\"" + currentTariff + "\",";
  json += "\"bgColor\":\"" + tariffBgColor + "\",";
  json += "\"isTempo\":" + String(isTempo ? "true" : "false") + ",";
  json += "\"today\":{";
  json += "\"class\":\"" + tempoTodayClass + "\",";
  json += "\"label\":\"" + tempoTodayLabel + "\",";
  json += "\"icon\":\"" + tempoTodayIcon + "\"";
  json += "},";
  json += "\"tomorrow\":{";
  json += "\"class\":\"" + tempoTomorrowClass + "\",";
  json += "\"label\":\"" + tempoTomorrowLabel + "\",";
  json += "\"icon\":\"" + tempoTomorrowIcon + "\"";
  json += "}";
  json += "}";

  request->send(200, "application/json", json);
}

String getPresenceJavaScript(String time) {
    String js = "";
    
    // Plugin Chart.js pour dessiner les bandes de présence
    // UNIQUEMENT sur le graphe d'énergie (window.energyChart)
    js += F("if(typeof Chart!=='undefined'){");
    js += F("Chart.register({id:'presenceOverlay',beforeDraw:function(c){");
    
    // Vérifier que c'est bien le graphe d'énergie
    js += F("if(c!==window.energyChart)return;");
    
    js += F("var ctx=c.ctx,ca=c.chartArea,p=window.energyPeriod||'hour';");
    js += F("if(!ca)return;ctx.save();");
    
    // Mode horaire - pD[i] correspond directement au label i (sliding 24h)
    js += F("if(p==='hour'&&pD.length===24){");
    js += F("var l=c.data.labels||[],cw=ca.right-ca.left,bw=cw/l.length;");
    js += F("for(var i=0;i<l.length&&i<24;i++){");
    js += F("if(pD[i]===1){");
    js += F("var x=ca.left+(i*bw)+(bw/2);");
    js += F("ctx.fillStyle='rgba(76,175,80,0.15)';");
    js += F("ctx.fillRect(x-bw/2,ca.top,bw,ca.bottom-ca.top);");
    js += F("ctx.fillStyle='rgba(76,175,80,0.8)';");
    js += F("ctx.beginPath();ctx.arc(x,ca.top+8,4,0,6.28);ctx.fill();");
    js += F("}}}");
    
    // Mode journalier
    js += F("else if(p==='day'&&Object.keys(pDD).length>0){");
    js += F("var l=c.data.labels||[],cw=ca.right-ca.left,bw=cw/l.length;");
    js += F("for(var i=0;i<l.length;i++){");
    js += F("var dy=parseInt(l[i].toString().split('/')[0]);");
    js += F("if(!isNaN(dy)&&dy>=1&&dy<=31&&pDD[dy]===1){");
    js += F("var x=ca.left+(i*bw)+(bw/2);");
    js += F("ctx.fillStyle='rgba(76,175,80,0.15)';");
    js += F("ctx.fillRect(x-bw/2,ca.top,bw,ca.bottom-ca.top);");
    js += F("ctx.fillStyle='rgba(76,175,80,0.8)';");
    js += F("ctx.beginPath();ctx.arc(x,ca.top+8,4,0,6.28);ctx.fill();");
    js += F("}}}");
    
    js += F("ctx.restore();}});}");
    
    return js;
}

#ifndef USE_ENERGY_V2
void handleStatusEnergy(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  PSRAMString result(500000);
  result = F("<html>");
  result += FPSTR(HTTP_HEADERGRAPH);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_ENERGY);
  result += F("<div class='row'>");
  if (strcmp(ConfigGeneral.ZLinky,"")!=0)
  {
    result += FPSTR(HTTP_ENERGY_LINKY);
  }
  if (strcmp(ConfigGeneral.Gaz,"")!=0)
  {
     result += FPSTR(HTTP_ENERGY_GAZ);
  }
  if (strcmp(ConfigGeneral.Water,"")!=0)
  {
     result += FPSTR(HTTP_ENERGY_WATER);
  }
  result +=F("</div>");
  result +=R"(<script>
            document.addEventListener('DOMContentLoaded', () => {
              const params  = new URLSearchParams(window.location.search);
              // Si aucun ?time, on choisit 'hour' par défaut
              const current = params.has('time') ? params.get('time') : 'hour';

              document.querySelectorAll('.link').forEach(link => {
                const linkTime = new URL(link.href, window.location.href)
                                  .searchParams.get('time');
                if (linkTime === current) {
                  link.classList.add('active');
                }
              });
            });

            function isMobileDevice() {
                return (typeof window.orientation !== "undefined") || 
                      (navigator.userAgent.indexOf('IEMobile') !== -1) ||
                      /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent);
            }

            function preventCanvasZoom(canvasId) {
              const canvas = document.getElementById(canvasId);
              if (!canvas) return;
              
              // Style CSS direct
              canvas.style.touchAction = 'none';
              
              // Empêcher le zoom du navigateur
              canvas.addEventListener('touchstart', function(e) {
                  if (e.touches.length > 1) {
                      e.preventDefault();
                  }
              }, { passive: false });
              
              canvas.addEventListener('touchmove', function(e) {
                  if (e.touches.length > 1) {
                      e.preventDefault();
                  }
              }, { passive: false });
              
              // Empêcher le double-tap zoom
              let lastTouchEnd = 0;
              canvas.addEventListener('touchend', function(e) {
                  const now = Date.now();
                  if (now - lastTouchEnd <= 300) {
                      e.preventDefault();
                  }
                  lastTouchEnd = now;
              }, { passive: false });
              
              console.log('Canvas zoom prevention enabled for:', canvasId);
          }

          </script>)";
      result += FPSTR(HTTP_ENERGY_JAVASCRIPT);
  result +=footer();
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);
  String LinkyStatus;
  String tmpStatus = getDeviceStatus(String(ConfigGeneral.ZLinky)+".json");
  if (tmpStatus =="d4")
  {
    LinkyStatus="<div class='alert alert-danger' role='alert'>Appareil déconnecté</div>";
  }else{
    LinkyStatus="";
  }
  result.replace("{{LinkyStatus}}",LinkyStatus);

  int i = 0;
  String time;

  int paramsNr = request->params();
  if (paramsNr > 0)
  {
    time = request->arg(i);
  }
  else
  {
    time = "hour";
  }

  if (time == "hour")
  {
    result.replace("{{stylePowerChart}}", F("block"));
    
    String help="<a href='javascript:void(0)' onclick='showPopup(\"popupHelpEnergyTrendHour\")' class='position-absolute bottom-0 begin-0 p-2 text-muted' title='Help'><span class='hi'>?</span></a>";
    result.replace("{{helpTrend}}", help);
  }
  else{
    result.replace("{{stylePowerChart}}", F("none"));
    String help="<a href='javascript:void(0)' onclick='showPopup(\"popupHelpEnergyTrend\")' class='position-absolute bottom-0 begin-0 p-2 text-muted' title='Help'><span class='hi'>?</span></a>";
    result.replace("{{helpTrend}}", help);
  }

  result.replace("{{time}}",time);
  result.replace("{{zlinkyIeee}}", String(ConfigGeneral.ZLinky));

  ConfigGeneral.LinkyMode = getZigbeeValue(String(ConfigGeneral.ZLinky)+".json","FF66","768").toInt();

  bool foundDevice = false;
  DeviceData* device;
  for (size_t ident = 0; ident < devices.size(); ident++) 
  {   
    if (devices[ident]->getDeviceID() == String(ConfigGeneral.ZLinky))
    {
      device = devices[ident];
      foundDevice=true;
      break;
    }   
  }

  if (foundDevice)
  {
    if ((ConfigGeneral.LinkyMode == 0 ) || (ConfigGeneral.LinkyMode == 2 ))
    {
      String tmp = device->getValue("FF66","5");

      if (tmp.toInt()>0)
      {
        result.replace("{{styleEnergyAlert}}", F("display:block;"));
        result.replace("{{energyAlertMessage}}", F("Dépassement de puissance souscrite"));
      }else{
        result.replace("{{styleEnergyAlert}}", F("display:none;"));
        result.replace("{{energyAlertMessage}}", F(""));
      }
    }else{
      String tmp = device->getValue("FF66","535");
      auto status = parseStatusRegister(tmp);

      if (status.depassement_ref_pow)
      {
        result.replace("{{styleEnergyAlert}}", F("display:block;"));
        result.replace("{{energyAlertMessage}}", F("Dépassement de puissance souscrite"));
      }else{
        result.replace("{{styleEnergyAlert}}", F("display:none;"));
        result.replace("{{energyAlertMessage}}", F(""));
      }
    }   
  }
  else
  {
    result.replace("{{styleEnergyAlert}}", F("display:none;"));
    result.replace("{{energyAlertMessage}}", F(""));
  }
 
  result.replace("{{tariffCard}}", FPSTR(HTTP_TARIFF_CARD));
  
  //
  
  String powerGauge="";
  if (time == "hour")
  {
    if ((ConfigGeneral.LinkyMode == 2 ) || (ConfigGeneral.LinkyMode == 3 ) || (ConfigGeneral.LinkyMode == 7 ))
    {
      powerGauge=F("<div class='col-lg-4 col-md-12 col-12'>");
            powerGauge +=F("<div id='energyGauge' class='card p-4' style='height:100%;min-height:270px;'>");
              powerGauge +=F("<h5 class='card-title' >Linky : Puissances</h5>");
              powerGauge +=F("<div class='card-body' style='margin-left:-30px;margin-right:-30px;'>");
                powerGauge += F("<div class='row'>");
                  
                if (strcmp(ConfigGeneral.Production,"") != 0 )
                {
                  powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'>");
                    powerGauge +=F("<h5>Soutirée P.1</h5>");
                    powerGauge +=F("<div id='power_gauge_global' class='w-100' ></div>"); //style='width:30%;display:inline-block;'
                  powerGauge +=F("</div>");
                  powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'>");
                    powerGauge +=F("<h5>Soutirée P.2</h5>");
                    powerGauge +=F("<div id='power_gauge_global2' class='w-100'></div>");
                  powerGauge +=F("</div>");
                  powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'>");
                    powerGauge +=F("<h5>Soutirée P.3</h5>");
                    powerGauge +=F("<div id='power_gauge_global3' class='w-100'></div>");
                  powerGauge +=F("</div>");
                  powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'>");
                  powerGauge +=F("<h5>Injectée</h5>");
                    powerGauge +=F("<div id='power_gauge_prod' class='w-100'></div>");
                  powerGauge +=F("</div>");
                }else{
                  powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'>");
                    powerGauge +=F("<h5>Soutirée P.1</h5>");
                    powerGauge +=F("<div id='power_gauge_global' class='w-100' ></div>"); //style='width:30%;display:inline-block;'
                  powerGauge +=F("</div>");
                  powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'>");
                    powerGauge +=F("<h5>Soutirée P.2</h5>");
                    powerGauge +=F("<div id='power_gauge_global2' class='w-100'></div>");
                  powerGauge +=F("</div>");
                  powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'>");
                    powerGauge +=F("<h5>Soutirée P.3</h5>");
                    powerGauge +=F("<div id='power_gauge_global3' class='w-100'></div>");
                  powerGauge +=F("</div>");
                }
                powerGauge +=F("</div>");
                powerGauge +=F("<a href='javascript:void(0)' onclick='showPopup(\"popupHelpPowerJaugeHour\")' class='position-absolute bottom-0 begin-0 p-2 text-muted' title='Help'><span class='hi'>?</span></a>");
                powerGauge +=F("<a href='javascript:void(0)' onclick='showPopup(\"popupLinkyDatas\")' class='position-absolute bottom-0 end-0 p-2 text-muted' title='Help'>");
                  powerGauge +=F("<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='currentColor' class='bi bi-info-square' viewBox='0 0 16 16'>");
                    powerGauge +=F("<path d='M14 1a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H2a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1zM2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2z'/>");
                    powerGauge +=F("<path d='m8.93 6.588-2.29.287-.082.38.45.083c.294.07.352.176.288.469l-.738 3.468c-.194.897.105 1.319.808 1.319.545 0 1.178-.252 1.465-.598l.088-.416c-.2.176-.492.246-.686.246-.275 0-.375-.193-.304-.533zM9 4.5a1 1 0 1 1-2 0 1 1 0 0 1 2 0'/>");
                  powerGauge +=F("</svg>");
                powerGauge +=F("</a>");
              powerGauge +=F("</div>");
            powerGauge +=F("</div>");
          powerGauge +=F("</div>");
    }else{
      powerGauge =F("<div class='col-lg-4 col-md-12 col-12'>");
            powerGauge +=F("<div id='energyGauge' class='card p-4' style='height:100%;min-height:270px;'>");
              powerGauge +=F("<h5 class='card-title' >Linky : Puissances</h5>");
              powerGauge +=F("<div class='card-body' style='margin-left:-30px;margin-right:-30px;'>");
                powerGauge += F("<div class='row'>");
                if (strcmp(ConfigGeneral.Production,"") != 0 )
                {
                  powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'>");
                    powerGauge +=F("<h5>Soutirée</h5>");
                    powerGauge +=F("<div id='power_gauge_global' class='w-100' ></div>");
                  powerGauge +=F("</div>");
                  powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'>");
                    powerGauge +=F("<h5>Injectée</h5>");
                    powerGauge +=F("<div id='power_gauge_prod' class='w-100'></div>");
                  powerGauge +=F("</div>");
                }else{
                  powerGauge += F("<div class='col-12 col-md-12 col-lg-12 mb-3' style='text-align:center;'>");
                    powerGauge +=F("<h5>Soutirée</h5>");
                    powerGauge +=F("<div id='power_gauge_global' class='w-100' ></div>");
                  powerGauge +=F("</div>");
                }
                powerGauge +=F("</div>");
                powerGauge +=F("<a href='javascript:void(0)' onclick='showPopup(\"popupHelpPowerJaugeHour\")' class='position-absolute bottom-0 begin-0 p-2 text-muted' title='Help'><span class='hi'>?</span></a>");
                powerGauge +=F("<a href='javascript:void(0)' onclick='showPopup(\"popupLinkyDatas\")' class='position-absolute bottom-0 end-0 p-2 text-muted' title='Help'>");
                  powerGauge +=F("<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='currentColor' class='bi bi-info-square' viewBox='0 0 16 16'>");
                    powerGauge +=F("<path d='M14 1a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H2a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1zM2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2z'/>");
                    powerGauge +=F("<path d='m8.93 6.588-2.29.287-.082.38.45.083c.294.07.352.176.288.469l-.738 3.468c-.194.897.105 1.319.808 1.319.545 0 1.178-.252 1.465-.598l.088-.416c-.2.176-.492.246-.686.246-.275 0-.375-.193-.304-.533zM9 4.5a1 1 0 1 1-2 0 1 1 0 0 1 2 0'/>");
                  powerGauge +=F("</svg>");
                powerGauge +=F("</a>");
              powerGauge +=F("</div>");  
            powerGauge +=F("</div>");
          powerGauge +=F("</div>");
    }
  }else{
    powerGauge =F("<div class='col-lg-4 col-md-12 col-12'>");
      powerGauge +=F("<div id='energyGauge'  class='card p-4' style='height:100%;min-height:270px;'>");
        powerGauge +=F("<h5 class='card-title'>Linky </h5>");
        powerGauge +=F("<div class='card-body' style='margin-left:-30px;margin-right:-30px;'>");
          powerGauge += F("<div class='row'>");
            if (strcmp(ConfigGeneral.Production,"") != 0 )
            {
              powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'>");
                powerGauge +=F("<h5>Consommation</h5>");
                powerGauge +=F("<div id='power_gauge_global' class='w-100' ></div>");
              powerGauge +=F("</div>");
              powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'>");
                powerGauge +=F("<h5>Production</h5>");
                powerGauge +=F("<div id='power_gauge_prod' class='w-100'></div>");
              powerGauge +=F("</div>");
            }else{
              powerGauge += F("<div class='col-12 col-md-12 col-lg-12 mb-3' style='text-align:center;'>");
                powerGauge +=F("<h5>Consommation</h5>");
                powerGauge +=F("<div id='power_gauge_global' class='w-100' ></div>");
              powerGauge +=F("</div>");
            }
          powerGauge +=F("</div>");
          powerGauge +=F("<a href='javascript:void(0)' onclick='showPopup(\"popupHelpPowerJauge\")' class='position-absolute bottom-0 begin-0 p-2 text-muted' title='Help'><span class='hi'>?</span></a>"); 
        powerGauge +=F("</div>");
      powerGauge +=F("</div>");
    powerGauge +=F("</div>");
  }
  result.replace("{{power_gauge}}",powerGauge);

  String javascript = "";
  int budget = 0;
  javascript = F("<script language='javascript'>");
  javascript += F("$(document).ready(function() {");
  if (strcmp(ConfigGeneral.ZLinky,"")!=0)
  {
    javascript+=F("calculateEnergyClass('");
    javascript += String(ConfigGeneral.ZLinky);
    javascript += F("','");
    javascript += time;
    javascript += F("');");
    if (time == "hour")
    {
      
      javascript += createPowerGraph(ConfigGeneral.ZLinky);
      if ((ConfigGeneral.LinkyMode == 2 ) || (ConfigGeneral.LinkyMode == 3 ) || (ConfigGeneral.LinkyMode == 7 ))
      {
        javascript += F("loadPowerGaugeAbo(2");
        javascript += F(",'");
        javascript += String(ConfigGeneral.ZLinky);
        javascript += F("','2319','");
        javascript += time;
        javascript += F("');");
        javascript += F("loadPowerGaugeAbo(3");
        javascript += F(",'");
        javascript += String(ConfigGeneral.ZLinky);
        javascript += F("','2575','");
        javascript += time;
        javascript += F("');");
      }

    }
    javascript += createDistributionGraph(ConfigGeneral.ZLinky);
   
    if (ConfigNotif.OverBudgetThreshold)
    {
      budget= getkWhBudget(String(ConfigGeneral.ZLinky), time, ConfigNotif.OverBudgetThreshold);
    }
     

    javascript += createEnergyGraph(ConfigGeneral.ZLinky,"energy","['#d35400','#27ae60','#2980b9','#154360','#7f8c8d','#000000','#e74c3c','#c0392b','#f5b041','#145a32']",budget);
    if (strlen(ConfigGeneral.Presence) > 0 && ConfigGeneral.enablePresenceGraph) {
      if (time == "hour" || time == "day") {
        javascript += getPresenceJavaScript(time);
      }
    }
    javascript += F("loadPowerGaugeAbo(1");
    javascript += F(",'");
    javascript += String(ConfigGeneral.ZLinky);
    javascript += F("','1295','");
    javascript += time;
    javascript += F("');");
    
    if (strcmp(ConfigGeneral.Production,"") != 0 )
    {
      javascript += F("loadPowerGaugeAbo(4");
      javascript += F(",'");
      javascript += String(ConfigGeneral.Production);
      javascript += F("','519','");
      javascript += time;
      javascript += F("');");
    }
    
    javascript += F("refreshStatusEnergy('");
    javascript += String(ConfigGeneral.ZLinky);
    javascript += F("','1295','");
    javascript += time;
    javascript += "','energy'";
    javascript += F(");");

    if ((ConfigGeneral.LinkyMode == 2 ) || (ConfigGeneral.LinkyMode == 3 ) || (ConfigGeneral.LinkyMode == 7 ))
    {
      javascript += F("refreshGaugeAbo('");
      javascript += String(ConfigGeneral.ZLinky);
      javascript += F("','1295','");
      javascript += time;
      javascript += F("');");
      javascript += F("refreshGaugeAbo('");
      javascript += String(ConfigGeneral.ZLinky);
      javascript += F("','2319','");
      javascript += time;
      javascript += F("');");
      javascript += F("refreshGaugeAbo('");
      javascript += String(ConfigGeneral.ZLinky);
      javascript += F("','2575','");
      javascript += time;
      javascript += F("');");

    }else{
      if (strcmp(ConfigGeneral.Production,"") != 0 )
      {
        javascript += F("refreshGaugeAbo('");
        javascript += String(ConfigGeneral.Production);
        javascript += F("','519','");
        javascript += time;
        javascript += F("');");
      }
      javascript += F("refreshGaugeAbo('");
      javascript += String(ConfigGeneral.ZLinky);
      javascript += F("','1295','");
      javascript += time;
      javascript += F("');");
    }

  }
  if (strcmp(ConfigGeneral.Gaz,"")!=0)
  {
    budget = 0;
    javascript += createEnergyGraph(ConfigGeneral.Gaz, "gaz","['#e67e22','#2785c7','#00c967','#c9c600','#c96100', '#c90000','#00c6c9', '#a700c9', '#c90043','#373737']",budget);
    javascript += F("refreshStatusGaz('");
    javascript += String(ConfigGeneral.Gaz);
    javascript += F("','");
    javascript += time;
    javascript += F("');");
  }
  if (strcmp(ConfigGeneral.Water,"")!=0)
  {
    budget = 0;
    javascript += createEnergyGraph(ConfigGeneral.Water, "water","['#2e86c1','#2785c7','#00c967','#c9c600','#c96100', '#c90000','#00c6c9', '#a700c9', '#c90043','#373737']",budget);
    javascript += F("refreshStatusWater('");
    javascript += String(ConfigGeneral.Water);
    javascript += F("','");
    javascript += time;
    javascript += F("');");
  }
  if (time == "hour" || time == "day") {
      javascript += F("initPresence('");
      javascript += time;
      javascript += F("');");
  }
  javascript += F("});");
  javascript += F("var ET = document.getElementById('energyTrend').offsetHeight;");
  javascript += F("var EG = document.getElementById('energyGauge').offsetHeight;");
  javascript += F("var cadre = document.getElementById('cadre_energy').clientWidth;");
  javascript += F("if (cadre>720){");
  javascript += F("if (EG<ET){");
    javascript += F("document.getElementById('energyGauge').style.minHeight=`${ET}px`;");
  javascript +=F("}else{");
    javascript += F("document.getElementById('energyTrend').style.minHeight=`${EG}px`;");
  javascript +=F("} }");  
  javascript += F("function uT(){fetch('/api/tariff').then(r=>r.json()).then(d=>{if(d.show){document.getElementById('tariff-name').textContent=d.tariff;document.getElementById('tariff-card').style.background=d.bgColor;document.getElementById('tempo-section').style.display=d.isTempo?'block':'none';if(d.isTempo){var t=document.getElementById('tempo-today');t.className='tempo-badge '+d.today.class;t.textContent=d.today.icon;var m=document.getElementById('tempo-tomorrow');m.className='tempo-badge '+d.tomorrow.class;m.textContent=d.tomorrow.icon;}document.getElementById('tariff-section').style.display='block';}}).catch(e=>{});}uT();setInterval(uT,60000);");
  javascript += F("</script>");

  result.replace("{{javascript}}", javascript);
",
  request->send(200, "text/html", result.c_str());
}
#endif // !USE_ENERGY_V2

#ifdef USE_ENERGY_V2

void handleStatusEnergy(AsyncWebServerRequest *request)
{
    if (!checkAuth(request)) return;
    if (!checkHeapForPage(request)) return;

    unsigned long startTime = millis();

    // Créer réponse streamée
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");

    // Pré-calculer toutes les valeurs nécessaires
    String time = "hour";
    if (request->params() > 0) {
        time = request->arg(0);
    }

    bool hasZLinky = (strcmp(ConfigGeneral.ZLinky, "") != 0);
    bool hasGaz = (strcmp(ConfigGeneral.Gaz, "") != 0);
    bool hasWater = (strcmp(ConfigGeneral.Water, "") != 0);
    bool hasProduction = (strcmp(ConfigGeneral.Production, "") != 0);

    ConfigGeneral.LinkyMode = hasZLinky ?
        getZigbeeValue(String(ConfigGeneral.ZLinky) + ".json", "FF66", "768").toInt() : 0;
    bool isTriphase = (ConfigGeneral.LinkyMode == 2) || (ConfigGeneral.LinkyMode == 3) || (ConfigGeneral.LinkyMode == 7);
    bool isHourMode = (time == "hour");

    // === Part 1 : Header statique ===
    response->print(F("<html>"));
    response->print(FPSTR(HTTP_HEADERGRAPH));

    {
        String menu = FPSTR(HTTP_MENU);
        menu.replace("{{FormattedDate}}", FormattedDate);
        response->print(menu);
    }

    // HTTP_ENERGY = onglets navigation (pas de placeholders)
    response->print(FPSTR(HTTP_ENERGY));

    // === Part 2 : HTTP_ENERGY_LINKY — charger en PSRAM, remplacer, streamer ===
    response->print(F("<div class='row'>"));
    if (hasZLinky) {
        PSRAMString linky;
        linky = FPSTR(HTTP_ENERGY_LINKY);

        // LinkyStatus
        String LinkyStatus;
        if (getDeviceStatus(String(ConfigGeneral.ZLinky) + ".json") == "d4") {
            LinkyStatus = "<div class='alert alert-danger' role='alert'>Appareil déconnecté</div>";
        } else {
            LinkyStatus = "";
        }
        linky.replace("{{LinkyStatus}}", LinkyStatus.c_str());

        // Alerte énergie (tous les modes Linky)
        bool foundDevice = false;
        DeviceData* device = nullptr;
        for (size_t ident = 0; ident < devices.size(); ident++) {
            if (devices[ident]->getDeviceID() == String(ConfigGeneral.ZLinky)) {
                device = devices[ident];
                foundDevice = true;
                break;
            }
        }

        if (foundDevice) {
            if ((ConfigGeneral.LinkyMode == 0) || (ConfigGeneral.LinkyMode == 2)) {
                String tmp = device->getValue("FF66", "5");
                if (tmp.toInt() > 0) {
                    linky.replace("{{styleEnergyAlert}}", "display:block;");
                    linky.replace("{{energyAlertMessage}}", "Dépassement de puissance souscrite");
                } else {
                    linky.replace("{{styleEnergyAlert}}", "display:none;");
                    linky.replace("{{energyAlertMessage}}", "");
                }
            } else {
                String tmp = device->getValue("FF66", "535");
                auto status = parseStatusRegister(tmp);
                if (status.depassement_ref_pow) {
                    linky.replace("{{styleEnergyAlert}}", "display:block;");
                    linky.replace("{{energyAlertMessage}}", "Dépassement de puissance souscrite");
                } else {
                    linky.replace("{{styleEnergyAlert}}", "display:none;");
                    linky.replace("{{energyAlertMessage}}", "");
                }
            }
        } else {
            linky.replace("{{styleEnergyAlert}}", "display:none;");
            linky.replace("{{energyAlertMessage}}", "");
        }

        // TariffCard
        linky.replace("{{tariffCard}}", FPSTR(HTTP_TARIFF_CARD));

        // time (utilisé dans les liens distribution)
        linky.replace("{{time}}", time.c_str());
        linky.replace("{{zlinkyIeee}}", String(ConfigGeneral.ZLinky));

        // stylePowerChart
        linky.replace("{{stylePowerChart}}", isHourMode ? "block" : "none");

        // helpTrend
        {
            String help = "<a href='javascript:void(0)' onclick='showPopup(\"";
            help += isHourMode ? "popupHelpEnergyTrendHour" : "popupHelpEnergyTrend";
            help += "\")' class='position-absolute bottom-0 begin-0 p-2 text-muted' title='Help'><span class='hi'>?</span></a>";
            linky.replace("{{helpTrend}}", help.c_str());
        }

        // power_gauge — générer le HTML comme V1
        {
            String powerGauge = "";
            if (isHourMode) {
                if (isTriphase) {
                    powerGauge = F("<div class='col-lg-4 col-md-12 col-12'>");
                    powerGauge += F("<div id='energyGauge' class='card p-4' style='height:100%;min-height:270px;'>");
                    powerGauge += F("<h5 class='card-title'>Linky : Puissances</h5>");
                    powerGauge += F("<div class='card-body' style='margin-left:-30px;margin-right:-30px;'>");
                    powerGauge += F("<div class='row'>");
                    if (hasProduction) {
                        powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'><h5>Soutirée P.1</h5><div id='power_gauge_global' class='w-100'></div></div>");
                        powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'><h5>Soutirée P.2</h5><div id='power_gauge_global2' class='w-100'></div></div>");
                        powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'><h5>Soutirée P.3</h5><div id='power_gauge_global3' class='w-100'></div></div>");
                        powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'><h5>Injectée</h5><div id='power_gauge_prod' class='w-100'></div></div>");
                    } else {
                        powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'><h5>Soutirée P.1</h5><div id='power_gauge_global' class='w-100'></div></div>");
                        powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'><h5>Soutirée P.2</h5><div id='power_gauge_global2' class='w-100'></div></div>");
                        powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'><h5>Soutirée P.3</h5><div id='power_gauge_global3' class='w-100'></div></div>");
                    }
                    powerGauge += F("</div>");
                    powerGauge += F("<a href='javascript:void(0)' onclick='showPopup(\"popupHelpPowerJaugeHour\")' class='position-absolute bottom-0 begin-0 p-2 text-muted' title='Help'><span class='hi'>?</span></a>");
                    powerGauge += F("<a href='javascript:void(0)' onclick='showPopup(\"popupLinkyDatas\")' class='position-absolute bottom-0 end-0 p-2 text-muted' title='Help'>");
                    powerGauge += F("<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='currentColor' class='bi bi-info-square' viewBox='0 0 16 16'>");
                    powerGauge += F("<path d='M14 1a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H2a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1zM2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2z'/>");
                    powerGauge += F("<path d='m8.93 6.588-2.29.287-.082.38.45.083c.294.07.352.176.288.469l-.738 3.468c-.194.897.105 1.319.808 1.319.545 0 1.178-.252 1.465-.598l.088-.416c-.2.176-.492.246-.686.246-.275 0-.375-.193-.304-.533zM9 4.5a1 1 0 1 1-2 0 1 1 0 0 1 2 0'/>");
                    powerGauge += F("</svg></a>");
                    powerGauge += F("</div></div></div>");
                } else {
                    // Monophasé mode heure
                    powerGauge = F("<div class='col-lg-4 col-md-12 col-12'>");
                    powerGauge += F("<div id='energyGauge' class='card p-4' style='height:100%;min-height:270px;'>");
                    powerGauge += F("<h5 class='card-title'>Linky : Puissances</h5>");
                    powerGauge += F("<div class='card-body' style='margin-left:-30px;margin-right:-30px;'>");
                    powerGauge += F("<div class='row'>");
                    if (hasProduction) {
                        powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'><h5>Soutirée</h5><div id='power_gauge_global' class='w-100'></div></div>");
                        powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'><h5>Injectée</h5><div id='power_gauge_prod' class='w-100'></div></div>");
                    } else {
                        powerGauge += F("<div class='col-12 col-md-12 col-lg-12 mb-3' style='text-align:center;'><h5>Soutirée</h5><div id='power_gauge_global' class='w-100'></div></div>");
                    }
                    powerGauge += F("</div>");
                    powerGauge += F("<a href='javascript:void(0)' onclick='showPopup(\"popupHelpPowerJaugeHour\")' class='position-absolute bottom-0 begin-0 p-2 text-muted' title='Help'><span class='hi'>?</span></a>");
                    powerGauge += F("<a href='javascript:void(0)' onclick='showPopup(\"popupLinkyDatas\")' class='position-absolute bottom-0 end-0 p-2 text-muted' title='Help'>");
                    powerGauge += F("<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='currentColor' class='bi bi-info-square' viewBox='0 0 16 16'>");
                    powerGauge += F("<path d='M14 1a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H2a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1zM2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2z'/>");
                    powerGauge += F("<path d='m8.93 6.588-2.29.287-.082.38.45.083c.294.07.352.176.288.469l-.738 3.468c-.194.897.105 1.319.808 1.319.545 0 1.178-.252 1.465-.598l.088-.416c-.2.176-.492.246-.686.246-.275 0-.375-.193-.304-.533zM9 4.5a1 1 0 1 1-2 0 1 1 0 0 1 2 0'/>");
                    powerGauge += F("</svg></a>");
                    powerGauge += F("</div></div></div>");
                }
            } else {
                // Mode jour/mois/année
                powerGauge = F("<div class='col-lg-4 col-md-12 col-12'>");
                powerGauge += F("<div id='energyGauge' class='card p-4' style='height:100%;min-height:270px;'>");
                powerGauge += F("<h5 class='card-title'>Linky</h5>");
                powerGauge += F("<div class='card-body' style='margin-left:-30px;margin-right:-30px;'>");
                powerGauge += F("<div class='row'>");
                if (hasProduction) {
                    powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'><h5>Consommation</h5><div id='power_gauge_global' class='w-100'></div></div>");
                    powerGauge += F("<div class='col-12 col-md-6 col-lg-6 mb-3' style='text-align:center;'><h5>Production</h5><div id='power_gauge_prod' class='w-100'></div></div>");
                } else {
                    powerGauge += F("<div class='col-12 col-md-12 col-lg-12 mb-3' style='text-align:center;'><h5>Consommation</h5><div id='power_gauge_global' class='w-100'></div></div>");
                }
                powerGauge += F("</div>");
                powerGauge += F("<a href='javascript:void(0)' onclick='showPopup(\"popupHelpPowerJauge\")' class='position-absolute bottom-0 begin-0 p-2 text-muted' title='Help'><span class='hi'>?</span></a>");
                powerGauge += F("</div></div></div>");
            }
            linky.replace("{{power_gauge}}", powerGauge.c_str());
        }

        esp_task_wdt_reset();
        response->print(linky.c_str());
    }
    if (hasGaz) response->print(FPSTR(HTTP_ENERGY_GAZ));
    if (hasWater) response->print(FPSTR(HTTP_ENERGY_WATER));
    response->print(F("</div>"));

    // === Part 3 : JavaScript inline ===
    response->print(F(R"(<script>
document.addEventListener('DOMContentLoaded', () => {
  const params = new URLSearchParams(window.location.search);
  const current = params.has('time') ? params.get('time') : 'hour';
  document.querySelectorAll('.link').forEach(link => {
    const linkTime = new URL(link.href, window.location.href).searchParams.get('time');
    if (linkTime === current) link.classList.add('active');
  });
});
function isMobileDevice() {
  return (typeof window.orientation !== "undefined") || /Android|iPhone|iPad|iPod/i.test(navigator.userAgent);
}
function preventCanvasZoom(canvasId) {
  const c = document.getElementById(canvasId);
  if (!c) return;
  c.style.touchAction = 'none';
  c.addEventListener('touchstart', e => { if (e.touches.length > 1) e.preventDefault(); }, { passive: false });
  c.addEventListener('touchmove', e => { if (e.touches.length > 1) e.preventDefault(); }, { passive: false });
}
</script>)"));

    // === Part 4 : JavaScript des graphiques ===
    esp_task_wdt_reset();
    response->print(F("<script>$(document).ready(function() {"));

    if (hasZLinky) {
        response->printf("calculateEnergyClass('%s','%s');", ConfigGeneral.ZLinky, time.c_str());

        if (isHourMode) {
            response->print(createPowerGraph(ConfigGeneral.ZLinky));
            if (isTriphase) {
                response->printf("loadPowerGaugeAbo(2,'%s','2319','%s');", ConfigGeneral.ZLinky, time.c_str());
                response->printf("loadPowerGaugeAbo(3,'%s','2575','%s');", ConfigGeneral.ZLinky, time.c_str());
            }
        }

        response->print(createDistributionGraph(ConfigGeneral.ZLinky));

        int budget = ConfigNotif.OverBudgetThreshold ?
            getkWhBudget(String(ConfigGeneral.ZLinky), time, ConfigNotif.OverBudgetThreshold) : 0;

        response->print(createEnergyGraph(ConfigGeneral.ZLinky, "energy",
            "['#d35400','#27ae60','#2980b9','#154360','#7f8c8d','#000000','#e74c3c','#c0392b','#f5b041','#145a32']", budget));

        // Presence JS (overlay sur le graphe d'énergie)
        if (strlen(ConfigGeneral.Presence) > 0 && ConfigGeneral.enablePresenceGraph) {
            if (isHourMode || time == "day") {
                response->print(getPresenceJavaScript(time));
            }
        }

        response->printf("loadPowerGaugeAbo(1,'%s','1295','%s');", ConfigGeneral.ZLinky, time.c_str());

        if (hasProduction) {
            response->printf("loadPowerGaugeAbo(4,'%s','519','%s');", ConfigGeneral.Production, time.c_str());
        }

        response->printf("refreshStatusEnergy('%s','1295','%s','energy');", ConfigGeneral.ZLinky, time.c_str());

        if (isTriphase) {
            response->printf("refreshGaugeAbo('%s','1295','%s');", ConfigGeneral.ZLinky, time.c_str());
            response->printf("refreshGaugeAbo('%s','2319','%s');", ConfigGeneral.ZLinky, time.c_str());
            response->printf("refreshGaugeAbo('%s','2575','%s');", ConfigGeneral.ZLinky, time.c_str());
        } else {
            if (hasProduction) {
                response->printf("refreshGaugeAbo('%s','519','%s');", ConfigGeneral.Production, time.c_str());
            }
            response->printf("refreshGaugeAbo('%s','1295','%s');", ConfigGeneral.ZLinky, time.c_str());
        }
    }

    if (hasGaz) {
        response->print(createEnergyGraph(ConfigGeneral.Gaz, "gaz",
            "['#e67e22','#2785c7','#00c967','#c9c600','#c96100','#c90000','#00c6c9','#a700c9','#c90043','#373737']", 0));
        response->printf("refreshStatusGaz('%s','%s');", ConfigGeneral.Gaz, time.c_str());
    }

    if (hasWater) {
        response->print(createEnergyGraph(ConfigGeneral.Water, "water",
            "['#2e86c1','#2785c7','#00c967','#c9c600','#c96100','#c90000','#00c6c9','#a700c9','#c90043','#373737']", 0));
        response->printf("refreshStatusWater('%s','%s');", ConfigGeneral.Water, time.c_str());
    }

    // Presence init
    if (isHourMode || time == "day") {
        response->printf("initPresence('%s');", time.c_str());
    }

    response->print(F("});"));

    // Ajustement hauteur
    response->print(F(
        "var ET=document.getElementById('energyTrend').offsetHeight;"
        "var EG=document.getElementById('energyGauge').offsetHeight;"
        "var cadre=document.getElementById('cadre_energy').clientWidth;"
        "if(cadre>720){if(EG<ET){document.getElementById('energyGauge').style.minHeight=`${ET}px`;}"
        "else{document.getElementById('energyTrend').style.minHeight=`${EG}px`;}}"
    ));

    // Tariff update JS
    response->print(F(
        "function uT(){fetch('/api/tariff').then(r=>r.json()).then(d=>{if(d.show){"
        "document.getElementById('tariff-name').textContent=d.tariff;"
        "document.getElementById('tariff-card').style.background=d.bgColor;"
        "document.getElementById('tempo-section').style.display=d.isTempo?'block':'none';"
        "if(d.isTempo){var t=document.getElementById('tempo-today');"
        "t.className='tempo-badge '+d.today.class;t.textContent=d.today.icon;"
        "var m=document.getElementById('tempo-tomorrow');"
        "m.className='tempo-badge '+d.tomorrow.class;m.textContent=d.tomorrow.icon;}"
        "document.getElementById('tariff-section').style.display='block';"
        "}}).catch(e=>{});}uT();setInterval(uT,60000);"
    ));

    response->print(F("</script>"));

    // Footer
    response->print(footer());
    response->print(F("</html>"));

    request->send(response);

    log_d("handleStatusEnergy took %lu ms", millis() - startTime);
}
#endif // USE_ENERGY_V2

// ============================================================
// PAGE TV — /statusEnergyTV
// Dashboard plein écran optimisé télécommande
// Hors de tout #ifdef — toujours compilé
// ============================================================
/*void handleStatusEnergyTV(AsyncWebServerRequest *request)
{
    if (!checkAuth(request)) return;

    bool hasZLinky = (strcmp(ConfigGeneral.ZLinky, "") != 0);
    const char* ieee = ConfigGeneral.ZLinky;

    AsyncResponseStream *response = request->beginResponseStream("text/html");
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");

    // ── HEAD ──────────────────────────────────────────────
    response->print(F(
        "<!DOCTYPE html><html lang='fr'><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<link rel='icon' type='image/x-icon' href='web/favicon.ico'>"
        "<title>LiXee TV</title>"
        "<script src='web/js/chart.umd.min.js'></script>"
        "<style>"
        ":root{"
          "--bg:#0d1117;--card:#161b22;--card2:#1c2128;"
          "--accent:#58a6ff;--green:#3fb950;--orange:#d29922;--red:#f85149;"
          "--text:#e6edf3;--sub:#8b949e;--border:#30363d;"
          "--radius:16px;--gap:18px;"
        "}"
        "*{box-sizing:border-box;margin:0;padding:0}"
        "html,body{width:100%;height:100%;background:var(--bg);color:var(--text);"
          "font-family:'Segoe UI',Arial,sans-serif;overflow:hidden}"
        "#app{display:grid;height:100vh;padding:var(--gap);"
          "grid-template-rows:auto 1fr auto auto;"
          "grid-template-columns:1fr 1fr 1fr;"
          "gap:var(--gap)}"
        "#hdr{grid-column:1/4;display:flex;align-items:center;justify-content:space-between;"
          "padding:10px 18px;background:var(--card);border-radius:var(--radius);"
          "border:1px solid var(--border)}"
        "#hdr .logo{font-size:22px;font-weight:700;color:var(--accent)}"
        "#hdr .date{font-size:18px;color:var(--sub)}"
        "#hdr .rinfo{font-size:16px;color:var(--sub)}"
        ".tile{background:var(--card);border-radius:var(--radius);border:2px solid var(--border);"
          "padding:22px 20px;display:flex;flex-direction:column;justify-content:space-between;"
          "transition:border-color .15s,box-shadow .15s;outline:none;cursor:default}"
        ".tile.focused,.tile:focus{border-color:var(--accent);"
          "box-shadow:0 0 0 3px rgba(88,166,255,.25)}"
        ".tlbl{font-size:16px;color:var(--sub);text-transform:uppercase;"
          "letter-spacing:.08em;margin-bottom:8px}"
        ".tval{font-size:52px;font-weight:700;line-height:1;color:var(--text)}"
        ".tunit{font-size:22px;font-weight:400;color:var(--sub);margin-left:4px}"
        ".tsub{font-size:16px;color:var(--sub);margin-top:8px}"
        ".tico{font-size:32px;margin-bottom:6px}"
        "#tile-tarif .tval{font-size:34px}"
        ".tbadge{display:inline-block;padding:6px 16px;border-radius:30px;"
          "font-size:20px;font-weight:700;margin-top:10px}"
        ".tempo-bleu{background:#1565c0;color:#fff}"
        ".tempo-blanc{background:#e0e0e0;color:#222}"
        ".tempo-rouge{background:#c62828;color:#fff}"
        ".tempo-undef{background:var(--card2);color:var(--sub)}"
        "#tile-chart{grid-column:1/4;background:var(--card);"
          "border-radius:var(--radius);border:2px solid var(--border);padding:18px 20px}"
        "#tile-chart.focused,#tile-chart:focus{border-color:var(--accent);"
          "box-shadow:0 0 0 3px rgba(88,166,255,.25)}"
        "#tile-chart canvas{max-height:200px}"
        "#ftr{grid-column:1/4;display:flex;align-items:center;justify-content:center;gap:32px;"
          "padding:8px;font-size:14px;color:var(--sub)}"
        "#ftr kbd{background:var(--card2);border:1px solid var(--border);"
          "border-radius:6px;padding:2px 8px;font-size:13px;margin:0 4px}"
        "#rbar{position:fixed;bottom:0;left:0;height:3px;background:var(--accent);"
          "width:100%;transform-origin:left;transition:transform 1s linear}"
        "</style></head>"
    ));

    // ── BODY ──────────────────────────────────────────────
    response->print(F("<body><div id='app'>"));

    response->print(F(
        "<div id='hdr'>"
          "<span class='logo'>&#x26A1; LiXee</span>"
          "<span class='date' id='hdr-date'>--/--/---- --:--</span>"
          "<span class='rinfo'>Actualisation dans <b id='countdown'>30</b>s</span>"
        "</div>"
    ));

    response->print(F(
        "<div class='tile focused' id='tile-power' tabindex='0'>"
          "<div class='tico'>&#x26A1;</div>"
          "<div class='tlbl'>Puissance instantan&eacute;e</div>"
          "<div><span class='tval' id='power-val'>---</span><span class='tunit'>W</span></div>"
          "<div class='tsub' id='power-sub'>Abonnement : --- VA</div>"
        "</div>"
    ));

    response->print(F(
        "<div class='tile' id='tile-today' tabindex='0'>"
          "<div class='tico'>&#x1F4C5;</div>"
          "<div class='tlbl'>Consommation du jour</div>"
          "<div><span class='tval' id='today-val'>---</span><span class='tunit'>kWh</span></div>"
          "<div class='tsub'>&nbsp;</div>"
        "</div>"
    ));

    response->print(F(
        "<div class='tile' id='tile-tarif' tabindex='0'>"
          "<div class='tico'>&#x1F3F7;</div>"
          "<div class='tlbl'>Tarif en cours</div>"
          "<div class='tval' id='tarif-val'>---</div>"
          "<div id='tempo-today' class='tbadge tempo-undef' style='display:none'></div>"
          "<div style='margin-top:12px;font-size:15px;color:var(--sub)'>"
            "Demain : "
            "<span id='tempo-tomorrow' class='tbadge tempo-undef' style='display:none'></span>"
            "<span id='demain-label' style='color:var(--sub)'>---</span>"
          "</div>"
        "</div>"
    ));

    response->print(F(
        "<div class='tile' id='tile-chart' tabindex='0'>"
          "<div class='tlbl'>Puissance 24h</div>"
          "<canvas id='powerChart'></canvas>"
        "</div>"
    ));

    response->print(F(
        "<div id='ftr'>"
          "<span><kbd>&lt; &gt;</kbd> Naviguer</span>"
          "<span><kbd>OK</kbd> Retour : <kbd>Back</kbd></span>"
        "</div>"
    ));

    response->print(F("</div><div id='rbar'></div>"));

    // ── JAVASCRIPT ────────────────────────────────────────
    response->print(F("<script>"));
    response->printf("var TV_IEEE='%s';", hasZLinky ? ieee : "");

    response->print(F(
        "function tvGet(id){return document.getElementById(id);}"
        "function tvClock(){"
          "var d=new Date();"
          "var p=function(n){return ('0'+n).slice(-2);};"
          "tvGet('hdr-date').textContent=p(d.getDate())+'/'+p(d.getMonth()+1)+'/'+d.getFullYear()+"
            "' '+p(d.getHours())+':'+p(d.getMinutes())+':'+p(d.getSeconds());"
        "}"
        "tvClock();setInterval(tvClock,1000);"
    ));

    response->print(F(
        "var tvTiles=['tile-power','tile-today','tile-tarif','tile-chart'];"
        "var tvIdx=0;"
        "function tvFocus(i){"
          "tvTiles.forEach(function(id){tvGet(id).classList.remove('focused');});"
          "tvIdx=i;var el=tvGet(tvTiles[tvIdx]);el.classList.add('focused');el.focus();"
        "}"
        "document.addEventListener('keydown',function(e){"
          "if(e.key==='ArrowRight'||e.key==='ArrowDown'){tvFocus((tvIdx+1)%tvTiles.length);e.preventDefault();}"
          "else if(e.key==='ArrowLeft'||e.key==='ArrowUp'){tvFocus((tvIdx-1+tvTiles.length)%tvTiles.length);e.preventDefault();}"
          "else if(e.key==='Backspace'||e.key==='BrowserBack'){window.location.href='/statusEnergy';e.preventDefault();}"
        "});"
        "tvFocus(0);"
    ));

    response->print(F(
        "var tvChart=null;"
        "function tvInitChart(){"
          "var ctx=tvGet('powerChart').getContext('2d');"
          "tvChart=new Chart(ctx,{type:'line',data:{labels:[],datasets:[{"
            "label:'W',data:[],borderColor:'#58a6ff',backgroundColor:'rgba(88,166,255,0.12)',"
            "borderWidth:2,pointRadius:0,fill:true,tension:0.3}]},"
            "options:{responsive:true,maintainAspectRatio:false,animation:false,"
              "plugins:{legend:{display:false}},"
              "scales:{x:{ticks:{color:'#8b949e',maxTicksLimit:12},grid:{color:'#21262d'}},"
                "y:{ticks:{color:'#8b949e'},grid:{color:'#21262d'}}}"
          "}});"
        "}"
    ));

    response->print(F(
        "function tvLoadPower(){"
          "if(!TV_IEEE)return;"
          "var xhr=new XMLHttpRequest();"
          "xhr.onload=function(){if(xhr.status===200){"
            "var p=xhr.responseText.trim().split(';');"
            "if(p.length>=2){"
              "tvGet('power-val').textContent=Math.round(parseFloat(p[0])||0);"
              "tvGet('power-sub').textContent='Abonnement : '+Math.round(parseFloat(p[1])||0)+' VA';"
            "}"
          "}};"
          "xhr.open('GET','loadPowerGaugeAbo?IEEE='+encodeURIComponent(TV_IEEE)+'&attribute=1295&time=hour',true);"
          "xhr.send();"
        "}"
    ));

    response->print(F(
        "function tvLoadChart(){"
          "if(!TV_IEEE)return;"
          "var xhr=new XMLHttpRequest();"
          "xhr.onload=function(){if(xhr.status===200){try{"
            "var j=JSON.parse(xhr.responseText);var datas=j.datas||[];"
            "var lbl=[],val=[];"
            "datas.forEach(function(pt){"
              "lbl.push(pt.y||'');"
              "var v=pt['1295'];"
              "if(v===undefined){for(var k in pt){if(k!=='y'&&!isNaN(parseInt(k))){v=pt[k];break;}}}"
              "val.push(v||0);"
            "});"
            "if(tvChart){tvChart.data.labels=lbl;tvChart.data.datasets[0].data=val;tvChart.update();}"
          "}catch(ex){}}};"
          "xhr.open('GET','loadPowerChart?IEEE='+encodeURIComponent(TV_IEEE)+'&attribute=1295',true);"
          "xhr.send();"
        "}"
    ));

    response->print(F(
        "function tvLoadTariff(){"
          "var xhr=new XMLHttpRequest();"
          "xhr.onload=function(){if(xhr.status===200){try{"
            "var d=JSON.parse(xhr.responseText);"
            "if(!d.show){tvGet('tarif-val').textContent='N/A';return;}"
            "tvGet('tarif-val').textContent=d.tariff||'---';"
            "var te=tvGet('tempo-today'),tr=tvGet('tempo-tomorrow'),dl=tvGet('demain-label');"
            "if(d.isTempo){"
              "te.style.display='inline-block';te.className='tbadge '+d.today.class;te.textContent=d.today.label;"
              "if(d.tomorrow.class!=='tempo-undef'){"
                "tr.style.display='inline-block';tr.className='tbadge '+d.tomorrow.class;tr.textContent=d.tomorrow.label;"
                "dl.style.display='none';"
              "}else{tr.style.display='none';dl.textContent='Non d\\u00e9fini';}"
            "}else{te.style.display='none';tr.style.display='none';dl.textContent='';}"
          "}catch(ex){}}};"
          "xhr.open('GET','api/tariff',true);xhr.send();"
        "}"
    ));

    response->print(F(
        "function tvLoadEnergy(){"
          "if(!TV_IEEE)return;"
          "var xhr=new XMLHttpRequest();"
          "xhr.onload=function(){if(xhr.status===200){try{"
            "var datas=JSON.parse(xhr.responseText);var total=0;"
            "if(datas.length>0){var last=datas[datas.length-1];"
              "for(var k in last){if(k!=='y'){var v=parseFloat(last[k]);if(!isNaN(v)&&v>0)total+=v;}}"
            "}"
            "tvGet('today-val').textContent=(total/1000).toFixed(2);"
          "}catch(ex){}}};"
          "xhr.open('GET','loadEnergyChart?IEEE='+encodeURIComponent(TV_IEEE)+'&time=day',true);"
          "xhr.send();"
        "}"
    ));

    response->print(F(
        "var TV_SEC=30,tvSec=TV_SEC,tvBar=tvGet('rbar');"
        "function tvTick(){"
          "tvSec--;tvGet('countdown').textContent=tvSec;"
          "tvBar.style.transform='scaleX('+(tvSec/TV_SEC)+')';"
          "if(tvSec<=0){tvSec=TV_SEC;tvAll();}"
        "}"
        "function tvAll(){tvLoadPower();tvLoadTariff();tvLoadEnergy();tvLoadChart();}"
        "tvInitChart();tvAll();"
        "setInterval(tvTick,1000);"
        "setInterval(tvLoadPower,10000);"
    ));

    response->print(F("</script></body></html>"));
    request->send(response);
}*/

// ============================================================
// JavaScript Masonry - PROGMEM
// ============================================================
const char HTTP_SCRIPT_MASONRY[] PROGMEM = R"rawliteral(
<script>
$(document).ready(function() {
  const grid = document.querySelector('#masonry-grid');
  const msnry = new Masonry(grid, {
    itemSelector: '.col-12',
    percentPosition: true
  });
  const observer = new ResizeObserver(() => {
    msnry.layout();
  });
  document.querySelectorAll('.col-12').forEach(card => observer.observe(card));
});
</script>
)rawliteral";

void handleStatusDevices(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;

  if(!deviceList->isEmpty())
  {
    deviceList->clear();
  }

  AsyncResponseStream *response = request->beginResponseStream("text/html");

  response->print(F("<html>"));
  response->print(FPSTR(HTTP_HEADER));

  // Styles personnalisés pour les fiches
  response->print(F("<style>"
    ".status-card{background:#fff;border:none;border-radius:12px;box-shadow:0 2px 8px rgba(0,0,0,0.08);transition:transform 0.2s,box-shadow 0.2s;overflow:hidden}"
    ".status-card:hover{transform:translateY(-2px);box-shadow:0 4px 16px rgba(0,0,0,0.12)}"
    ".card-header-custom{background:#fff;padding:14px 18px}"
    ".card-header-custom a{color:#222;text-decoration:none;font-weight:600;font-size:15px;display:flex;align-items:center}"
    ".card-header-custom a:hover{color:#6c757d;opacity:0.95}"
    ".card-header-custom svg{flex-shrink:0;margin-right:10px;width:18px;height:18px}"
    ".card-body-custom{padding:12px 18px}"
    ".attr-row{display:flex;justify-content:space-between;align-items:center;padding:9px 0;border-bottom:1px solid #e9ecef}"
    ".attr-row:last-child{border-bottom:none}"
    ".attr-name{color:#6c757d;font-size:13px;font-weight:500}"
    ".attr-value{font-family:'Courier New',monospace;font-size:14px;font-weight:600;color:#212529}"
    ".attr-unit{color:#adb5bd;font-size:12px;margin-left:4px}"
    ".actions-bar{display:flex;flex-wrap:wrap;gap:8px;padding:12px 18px;background:#f8f9fa;border-top:1px solid #e9ecef}"
    ".btn-action{background:#0d6efd;border:none;color:#fff;padding:7px 14px;border-radius:6px;font-size:13px;font-weight:500;cursor:pointer;transition:background 0.2s}"
    ".btn-action:hover{background:#0b5ed7}"
    ".btn-action:active{background:#0a58ca}"
    "@media(max-width:576px){"
      ".status-card{border-radius:10px}"
      ".card-header-custom{padding:12px 14px}"
      ".card-header-custom a{font-size:14px}"
      ".card-body-custom{padding:10px 14px}"
      ".attr-row{padding:7px 0}"
      ".attr-name{font-size:12px}"
      ".attr-value{font-size:13px}"
      ".actions-bar{padding:10px 14px}"
      ".btn-action{padding:6px 12px;font-size:12px}"
    "}"
    // Pastille de radio, poussee a droite de l'en-tete (l'en-tete est un flex).
    // Les tailles sont en !important : la regle .card-header-custom svg ci-dessus impose
    // 18x18 et une marge a toutes les icones de l'en-tete, y compris celle-ci.
    ".radio-tag{margin-left:auto;display:inline-flex;align-items:center;gap:5px;flex-shrink:0;"
      "font-size:11px;font-weight:600;color:#6c757d;background:#f1f3f5;border-radius:10px;padding:3px 8px}"
    ".radio-tag svg{margin:0!important;flex-shrink:0}"
    // Le symbole LoRa est portrait (ratio 0.58), celui du Zigbee carre : tailles distinctes
    // pour qu'ils pesent optiquement pareil.
    ".radio-tag .ic-lora{width:8px!important;height:14px!important}"
    ".radio-tag .ic-zb{width:13px!important;height:13px!important}"
  "</style>"));

  // Sprite : les traces sont declares UNE fois par page puis reference par <use> sur chaque
  // fiche. Les inliner par fiche couterait ~2 Ko x N appareils dans la reponse.
  response->print(F("<svg xmlns='http://www.w3.org/2000/svg' style='display:none'>"
    "<symbol id='ic-lora' viewBox='" SVG_LORA_VIEWBOX "' fill='currentColor'>" SVG_LORA_PATHS "</symbol>"
    "<symbol id='ic-zb' viewBox='0 0 24 24' fill='currentColor'>"
    "<path d='M11.988 0a11.85 11.85 0 00-8.617 3.696c7.02-.875 11.401-.583 13.289-.34 3.752.583 3.558 3.404 3.558 3.404L8.237 19.112c2.299.22 6.897.366 13.796-.631a11.86 11.86 0 001.912-6.469C23.945 5.374 18.595 0 11.988 0zm.232 4.31c-2.451-.014-5.772.146-9.963.723C.854 7.003.055 9.41.055 12.012.055 18.626 5.38 24 11.988 24c3.63 0 6.85-1.63 9.053-4.182-7.286.948-11.813.631-13.75.388-3.775-.56-3.557-3.404-3.557-3.404L15.691 4.474a38.635 38.635 0 00-3.471-.163Z'/>"
    "</symbol></svg>"));

  streamSection(response, HTTP_MENU);

  response->print(F("<div class='container py-4'>"));
  response->print(F("<h4 style='color:#212529;font-weight:600;margin-bottom:20px;'>Mesures des appareils</h4>"));
  response->print(F("<div class='row g-4' id='masonry-grid'>"));

  int exist = 0;
  for (size_t ident = 0; ident < devices.size(); ident++)
  {
    esp_task_wdt_reset();
    DeviceData* device = devices[ident];

    exist++;
    response->print(F("<div class='col-12 col-sm-12 col-md-6 col-lg-4 col-xl-4'>"));
    response->print(F("<div class='status-card'>"));

    // Header avec icône et titre
    response->print(F("<div class='card-header-custom'>"));
    response->print(F("<a href='/configDevice?id="));
    response->print(device->getDeviceID());
    response->print(F("'>"));
    // Icône device
    if (LittleFS.exists("/web/img/icon_" + device->getInfo().model +".png"))
    {
      response->print(F("<img src='web/img/icon_"));
      response->print(device->getInfo().model);
      response->print(F(".png'  height='64px'/>"));
    }else{
      response->print(F("<img src='web/img/icon_"));
      response->print(device->getInfo().device_id);
      response->print(F(".png'  height='64px'/>"));
    }

    if (device->getInfo().alias.length() > 0) {
      response->print(device->getInfo().alias);
    } else {
      response->print(device->getDeviceID());
    }

    // Radio d'origine : cette page liste les appareils des deux modules, et rien d'autre ne
    // les distingue (un ZLinky LoRa est un appareil normal, mêmes clusters qu'en Zigbee).
    // Un émetteur LoRa appairé porte la MAC de l'appareil.
    if (loraFindEmitterByMac(device->getDeviceID()) >= 0) {
      response->print(F("<span class='radio-tag' title='Recu par radio LoRa 2.4 GHz'>"
        "<svg class='ic-lora'><use href='#ic-lora'/></svg>LoRa</span>"));
    } else {
      response->print(F("<span class='radio-tag' title='Recu par Zigbee'>"
        "<svg class='ic-zb'><use href='#ic-zb'/></svg>Zigbee</span>"));
    }
    response->print(F("</a></div>"));

    // Body avec attributs
    response->print(F("<div class='card-body-custom'>"));

    int ShortAddr = device->getInfo().shortAddr.toInt();
    int DeviceId = device->getInfo().device_id.toInt();

    // Get status and action from json
    if (TemplateExist(DeviceId))
    {
      TemplateData* t = device->getTemplate();
      if (!t) {
          Serial.printf("WARNING: Template introuvable pour model: %s\n", device->getInfo().model.c_str());
          continue;
      }

      response->print(F("<div id='status_"));
      response->print(device->getInfo().shortAddr);
      response->print(F("'>"));

      for (int i = 0; i < t->StateSize(); i++)
      {
        if (t->states[i].visible)
        {
          if (device->getInfo().model == "ZLinky_TIC")
          {
            bool afficheOK = false;
            const char *tmp = t->states[i].mode;

            if ((tmp != NULL) && (tmp[0] != '\0'))
            {
              char modeCopy[50];
              strncpy(modeCopy, tmp, sizeof(modeCopy) - 1);
              modeCopy[sizeof(modeCopy) - 1] = '\0';

              char *pch = strtok(modeCopy, ";");
              while (pch != NULL)
              {
                if (atoi(pch) == device->getInfo().linkyMode.toInt())
                {
                  afficheOK=true;
                  break;
                }
                pch = strtok (NULL, ";");
              }
            }else{
              afficheOK=true;
            }

            if (afficheOK){
              String attrIdLinky = String(ShortAddr)+"_"+String(t->states[i].cluster)+"_"+String(t->states[i].attribute);
              response->print(F("<div class='attr-row'><span class='attr-name'>"));
              response->print(t->states[i].name);
              response->print(F("</span><span><span class='attr-value' id='"));
              response->print(attrIdLinky);
              response->print(F("'>"));
              response->print(GetValueStatus(device->getDeviceID(), t->states[i].cluster, t->states[i].attribute, (String)t->states[i].type, t->states[i].coefficient));
              response->print(F("</span><span class='attr-unit'>"));
              response->print(t->states[i].unit);
              response->print(F("</span></span></div>"));
            }
          }else{
            String attrId = String(ShortAddr) + "_" + String(t->states[i].cluster) + "_" + String(t->states[i].attribute);
            response->print(F("<div class='attr-row'><span class='attr-name'>"));
            response->print(t->states[i].name);
            response->print(F("</span><span><span class='attr-value' id='"));
            response->print(attrId);
            response->print(F("'>"));
            response->print(GetValueStatus(device->getDeviceID(), t->states[i].cluster, t->states[i].attribute, (String)t->states[i].type, t->states[i].coefficient));
            response->print(F("</span><span class='attr-unit'>"));
            response->print(t->states[i].unit);
            response->print(F("</span></span></div>"));
          }
        }
      }
      response->print(F("</div>")); // fin status_
      response->print(F("</div>")); // fin card-body-custom

      // Actions bar
      if (t->ActionSize() > 0) {
        response->print(F("<div class='actions-bar'>"));
        for (int i = 0; i < t->ActionSize(); i++)
        {
          esp_task_wdt_reset();
          response->printf("<button onclick=\"ZigbeeAction(%s,%d,%d,%d",
            device->getInfo().shortAddr.c_str(),
            t->actions[i].command,
            t->actions[i].endpoint,
            t->actions[i].value);
          if (t->actions[i].command == 400) {
            response->printf(",%d,%d", t->actions[i].cluster, t->actions[i].manufacturerCode);
          }
          response->print(F(");\" class='btn-action'>"));
          response->print(t->actions[i].name);
          response->print(F("</button>"));
        }
        response->print(F("</div>"));
      }
    } else {
      response->print(F("</div>")); // fin card-body-custom si pas de template
    }

    response->print(F("</div></div>")); // fin status-card et col

    vTaskDelay(1);
  }
  response->print(F("</div>"));
  response->print(F("</div>"));

  if (exist>0)
  {
    response->print(F("<script>getDeviceValue();</script>"));
    response->print(FPSTR(HTTP_SCRIPT_MASONRY));
  }else{
    response->print(F("<div align='center' style='height:100px;font-size:28px;font-weight:bold;'>No devices yet</div> <br>"));
  }
  response->print(footer());
  response->print(F("</html>"));

  request->send(response);
}

void handleConfigGeneral(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_GENERAL);
  result+=footer();
  result += F("</html>");

  //result = getMenuGeneral(result, "general");

  result.replace("{{FormattedDate}}", FormattedDate);

  if (ConfigSettings.enableDebug)
  {
    result.replace("{{checkedDebug}}", "Checked");
  }
  else
  {
    result.replace("{{checkedDebug}}", "");
  }

  //PARAMETER
  if (ConfigGeneral.developerMode)
  {
    result.replace("{{checkeddeveloperMode}}", "Checked");
  }
  else
  {
    result.replace("{{checkeddeveloperMode}}", "");
  }

  request->send(200, "text/html", result);
}

void handleConfigZigbee(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;

  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_ZIGBEE);
  result+=footer();
  result = getMenuGeneralZigbee(result, "config");
  
  result.replace("{{macCoordinator}}", String(ZConfig.zigbeeMac,HEX));
  result.replace("{{versionCoordinator}}", "SDK: "+String(ZConfig.sdk, DEC)+" Ver: "+String(ZConfig.application));
  if (ZConfig.network == 1)
  {
    result.replace("{{networkCoordinator}}", "<img src='web/img/ok.png' />");
  }else{
    result.replace("{{networkCoordinator}}", "<img src='web/img/nok.png' />");
  }
  
  result.replace("{{FormattedDate}}", FormattedDate);
  result.replace("{{SetMaskChannel}}", String(ZConfig.channel));

  request->send(200, F("text/html"), result);
}

void handleConfigHorloge(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_HORLOGE);
  result+=footer();
  result += F("</html>");

  //result = getMenuGeneral(result, "horloge");

  result.replace("{{FormattedDate}}", FormattedDate);

  result.replace("{{ntpserver}}", ConfigGeneral.ntpserver);
  result.replace("{{timeoffset}}", String(ConfigGeneral.timeoffset));
  result.replace("{{timezone}}", String(ConfigGeneral.timezone));
  String path = "configGeneral.json";
  String value = config_read(path,"epoch");
  /*unsigned long t = atol(value.c_str());
  char manualDate[32];
  sprintf(manualDate, "%02d/%02d/%02d %02d:%02d:%02d", day(t), month(t), year(t), hour(t), minute(t), second(t));*/
  result.replace("{{epochtime}}",value);

  request->send(200, "text/html", result);
}

void handleGetMQTTStatus(AsyncWebServerRequest *request)
{
  String result="0";
  if (ConfigSettings.enableMqtt)
  {
    if (mqttClient.connected())
    {
      result = "1";
    }else{
      result = "0";
    }
  }else{
    result="0";
  }
  request->send(200, "text/html", result);
}

void handleConfigMQTT(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_MQTT);
  result+=footer();
  result += F("</html>");
  if (ConfigSettings.enableMqtt)
  {
    result.replace("{{checkedMqtt}}", "Checked");
  }
  else
  {
    result.replace("{{checkedMqtt}}", "");
  }
  //result = getMenuGeneral(result, "mqtt");

  result.replace("{{FormattedDate}}", FormattedDate);

  result.replace("{{servMQTT}}", String(ConfigGeneral.servMQTT));
  result.replace("{{portMQTT}}", String(ConfigGeneral.portMQTT));
  result.replace("{{userMQTT}}", String(ConfigGeneral.userMQTT));
  result.replace("{{clientIDMQTT}}", String(ConfigGeneral.clientIDMQTT));
  if (String(ConfigGeneral.passMQTT) !="")
  {
    result.replace("{{passMQTT}}", "********");
  }else{
    result.replace("{{passMQTT}}", "");
  }
  //result.replace("{{passMQTT}}", String(ConfigGeneral.passMQTT));
  result.replace("{{headerMQTT}}", String(ConfigGeneral.headerMQTT));
  result.replace("{{displayCustomMQTT}}", "display:none;");

  if (ConfigGeneral.HAMQTT)
  {
    result.replace("{{checkedHA}}", "checked");
    result.replace("{{checkedTB}}", "");
    result.replace("{{checkedCustom}}", "");
    result.replace("{{displayCustomMQTT}}", "display:none;");
  }

  if (ConfigGeneral.TBMQTT)
  {
    result.replace("{{checkedTB}}", "checked");
    result.replace("{{checkedHA}}", "");
    result.replace("{{checkedCustom}}", "");
    result.replace("{{displayCustomMQTT}}", "display:none;");
  }

  if (ConfigGeneral.customMQTT)
  {
    result.replace("{{checkedCustom}}", "checked");
    result.replace("{{checkedHA}}", "");
    result.replace("{{checkedCustom}}", "");
    result.replace("{{displayCustomMQTT}}", "display:block;");
  }


  if (ConfigGeneral.customMQTTJson != "null")
  {
    result.replace("{{customMQTTJson}}", ConfigGeneral.customMQTTJson);
  }else{
    result.replace("{{customMQTTJson}}", "");
  }

  request->send(200, "text/html", result);
}

void handleConfigHTTP(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_HTTP);
  result+=footer();
  result += F("</html>");
  //result = getMenuGeneral(result, "http");

  if (ConfigSettings.enableSecureHttp)
  {
    result.replace("{{checkedHttp}}", "Checked");
  }else{
    result.replace("{{checkedHttp}}", "");
  }

  // Empêcher la désactivation de la sécurité si le tunnel est actif
  if (ConfigGeneral.enableTunnel && ConfigSettings.enableSecureHttp) {
    result.replace("{{disabledSecuToggle}}", "disabled");
    result.replace("{{securityLockWarning}}",
      "<div class='alert alert-info mb-3'>"
      "L'acc&egrave;s s&eacute;curis&eacute; ne peut pas &ecirc;tre d&eacute;sactiv&eacute; tant que le tunnel est actif. "
      "<a href='/configTunnel' class='alert-link'>D&eacute;sactiver le tunnel</a>"
      "</div>");
  } else {
    result.replace("{{disabledSecuToggle}}", "");
    result.replace("{{securityLockWarning}}", "");
  }

  if (request->arg("error").toInt() > 0)
  {
    result.replace("{{error}}", "Erreur : Veuillez verifier l'identifiant et le mode de passe HTTP ou la longeur du mot de passe >= 4 characters");
    if ((request->arg("error").toInt() & 1) == 1)
    {
      result.replace("{{passborder}}", "border:1px solid red;");
    }else{
      result.replace("{{passborder}}", "");
    }
    if ((request->arg("error").toInt() & 2) == 2)
    {
      result.replace("{{userborder}}", "border:1px solid red;");
    }else{
      result.replace("{{userborder}}", "");
    }
  }else{
    result.replace("{{error}}", "");
  }

  result.replace("{{FormattedDate}}", FormattedDate);

  result.replace("{{userHTTP}}", String(ConfigGeneral.userHTTP));

  if (String(ConfigGeneral.passHTTP) !="")
  {
    result.replace("{{passHTTP}}", "********");
  }else{
    result.replace("{{passHTTP}}", "");
  }

  request->send(200, "text/html", result);
}

void handleConfigRules(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result = F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_RULES);
  
  String rulesList=F("<table class='table table-striped table-hover'>");
  rulesList+=F("<thead>");
    rulesList+=F("<tr>");
      rulesList+=F("<th scope='col'>Nom</th>");
      rulesList+=F("<th scope='col' width='60px;'>Actif</th>");
      rulesList+=F("<th scope='col' width='50px;'>Etat</th>");
      rulesList+=F("<th scope='col' width='150px;'>Dernière Date</th>");
      rulesList+=F("<th scope='col' width='100px;'>Actions</th>");
    rulesList+=F("</tr>");
  rulesList+=F("</thead>");

  int exist=0;
  String js="";
  
  size_t rulesCount = rulesManager.size();
  for (size_t i = 0; i < rulesCount; i++) 
  {
    const Rule* rule = rulesManager.getRuleByIndex(i);
    if (!rule) continue;
    exist++;
    rulesList+=F("<tr>");
      rulesList+=F("<td scope='row'><span class='rule-name");
      if (!rule->enabled) rulesList+=F(" text-muted");
      rulesList+=F("'>");
        rulesList+=rule->name.c_str();
      rulesList+=F("</span></td>");
      rulesList+=F("<td><div class='form-check form-switch'><input class='form-check-input' type='checkbox' onchange='toggleRule(\"");
      rulesList+=rule->name.c_str();
      rulesList+=F("\", this)'");
      if (rule->enabled) rulesList+=F(" checked");
      rulesList+=F("></div></td>");
      rulesList+=F("<td>");
        int status = rulesManager.getStatusRule(rule->name.c_str());
        js += F("getRuleStatus('");
        js +=rule->name.c_str();
        js +=F("');");
        rulesList+=F("<span id='status_");
        rulesList+=rule->name.c_str();
        rulesList+=F("'>");
        if (status)
        {
          rulesList+=F("<svg xmlns='http://www.w3.org/2000/svg' width='32' height='32' fill='#1bc600' class='bi bi-bookmark-check-fill' viewBox='0 0 16 16'>");
            rulesList+=F("<path fill-rule='evenodd' d='M2 15.5V2a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v13.5a.5.5 0 0 1-.74.439L8 13.069l-5.26 2.87A.5.5 0 0 1 2 15.5m8.854-9.646a.5.5 0 0 0-.708-.708L7.5 7.793 6.354 6.646a.5.5 0 1 0-.708.708l1.5 1.5a.5.5 0 0 0 .708 0z'/>");
          rulesList+=F("</svg>");
        }else{
          rulesList+=F("<svg xmlns='http://www.w3.org/2000/svg' width='32' height='32' fill='#c60000' class='bi bi-bookmark-x-fill' viewBox='0 0 16 16'>");
            rulesList+=F("<path fill-rule='evenodd' d='M2 15.5V2a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v13.5a.5.5 0 0 1-.74.439L8 13.069l-5.26 2.87A.5.5 0 0 1 2 15.5M6.854 5.146a.5.5 0 1 0-.708.708L7.293 7 6.146 8.146a.5.5 0 1 0 .708.708L8 7.707l1.146 1.147a.5.5 0 1 0 .708-.708L8.707 7l1.147-1.146a.5.5 0 0 0-.708-.708L8 6.293z'/>");
          rulesList+=F("</svg>");
        }
      rulesList+=F("</span>");
      rulesList+=F("</td>");
      rulesList+=F("<td>");
        rulesList+=F("<span id='dateStatus_");
          rulesList+=rule->name.c_str();
        rulesList+=F("'>");  
        rulesList+=rulesManager.getLastDateRule(rule->name.c_str()).c_str();
      rulesList+=F("</span>");
      rulesList+=F("</td>");
      rulesList+=F("<td>");
        // Bouton Editer
        rulesList+=F("<a href='/editRule?name=");
        rulesList+=rule->name.c_str();
        rulesList+=F("' class='btn btn-sm btn-warning me-1'>");
          rulesList+=F("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' class='bi bi-pencil-square' viewBox='0 0 16 16'>");
            rulesList+=F("<path d='M15.502 1.94a.5.5 0 0 1 0 .706L14.459 3.69l-2-2L13.502.646a.5.5 0 0 1 .707 0l1.293 1.293zm-1.75 2.456-2-2L4.939 9.21a.5.5 0 0 0-.121.196l-.805 2.414a.25.25 0 0 0 .316.316l2.414-.805a.5.5 0 0 0 .196-.12l6.813-6.814z'/>");
            rulesList+=F("<path fill-rule='evenodd' d='M1 13.5A1.5 1.5 0 0 0 2.5 15h11a1.5 1.5 0 0 0 1.5-1.5v-6a.5.5 0 0 0-1 0v6a.5.5 0 0 1-.5.5h-11a.5.5 0 0 1-.5-.5v-11a.5.5 0 0 1 .5-.5H9a.5.5 0 0 0 0-1H2.5A1.5 1.5 0 0 0 1 2.5z'/>");
          rulesList+=F("</svg>");
        rulesList+=F("</a>");
        // Bouton Supprimer
        rulesList+=F("<button type='button' class='btn btn-sm btn-danger' onclick='deleteRule(\"");
        rulesList+=rule->name.c_str();
        rulesList+=F("\")'>");
          rulesList+=F("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' class='bi bi-x-square' viewBox='0 0 16 16'>");
            rulesList+=F("<path d='M14 1a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H2a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1zM2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2z'/>");
            rulesList+=F("<path d='M4.646 4.646a.5.5 0 0 1 .708 0L8 7.293l2.646-2.647a.5.5 0 0 1 .708.708L8.707 8l2.647 2.646a.5.5 0 0 1-.708.708L8 8.707l-2.646 2.647a.5.5 0 0 1-.708-.708L7.293 8 4.646 5.354a.5.5 0 0 1 0-.708'/>");
          rulesList+=F("</svg>");
        rulesList+=F("</button>");
      rulesList+=F("</td>");
    rulesList+=F("</tr>");
  }
  rulesList+=F("</table>");
  result.replace("{{rulesList}}",rulesList);

  if (exist>0)
  {
    result +="<script>"+js+"</script>";
  }else{
    result += F("<div align='center' style='height:100px;font-size:28px;font-weight:bold;'>Pas de règles</div> <br>");
  }
  result += footer();
  result += F("</html>");

  request->send(200, "text/html", result);
}

void handleEditRule(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String ruleName = "";
  if (request->hasArg("name")) {
    ruleName = request->arg("name");
  }
  
  if (ruleName.length() == 0) {
    request->send(400, "text/plain", "Nom de règle manquant");
    return;
  }

  // ✅ CORRECTION : Utiliser /config/rules.json au lieu de /rules.json
  File file = LittleFS.open("/config/rules.json", FILE_READ);
  if (!file) {
    request->send(500, "text/plain", "Impossible d'ouvrir rules.json");
    return;
  }

  SpiRamJsonDocument doc(100000);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    request->send(500, "text/plain", "Erreur de lecture JSON");
    return;
  }

  // Trouver la règle
  JsonArray rules = doc["rules"];
  JsonObject ruleToEdit;
  
  for (JsonObject rule : rules) {
    if (rule["name"].as<String>() == ruleName) {
      ruleToEdit = rule;
      break;
    }
  }
  
  if (ruleToEdit.isNull()) {
    request->send(404, "text/plain", "Règle non trouvée");
    return;
  }

  // Convertir la règle en JSON pour JavaScript
  String ruleJson;
  serializeJson(ruleToEdit, ruleJson);

  // Générer la page HTML avec le formulaire pré-rempli (PSRAM pour éviter le dépassement heap)
  PSRAMString result(100000);
  result = F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_EDIT_RULE_HTML);
  result += FPSTR(HTTP_EDIT_RULE_JS);
  result += F("<script>var ruleToEdit = ");
  result += ruleJson.c_str();
  result += F(";</script>");
  result += footer().c_str();
  result += F("</html>");

  request->send(200, "text/html", result.c_str());
}

void APIEditRule(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  // Buffer statique pour accumuler les données
  static String jsonBuffer;
  
  // Premier chunk : réinitialiser le buffer
  if (index == 0) {
    jsonBuffer = "";
    jsonBuffer.reserve(total);
  }
  
  // Ajouter les données reçues au buffer
  for (size_t i = 0; i < len; i++) {
    jsonBuffer += (char)data[i];
  }
  
  // Si on n'a pas encore tout reçu, attendre le prochain chunk
  if (index + len < total) {
    return;
  }
  
  // Parser le JSON complet depuis jsonBuffer (pas data!)
  SpiRamJsonDocument doc(100000);
  DeserializationError error = deserializeJson(doc, jsonBuffer);
  
  // Libérer le buffer
  jsonBuffer = "";
  
  if (error) {
    request->send(400, "text/plain", "JSON invalide");
    return;
  }

  String oldName = doc["oldName"].as<String>();
  String newName = doc["name"].as<String>();
  
  if (oldName.length() == 0) {
    request->send(400, "text/plain", "Ancien nom manquant");
    return;
  }

  if (newName.length() == 0) {
    request->send(400, "text/plain", "Le nom de la règle ne peut pas être vide");
    return;
  }

  // ✅ VALIDATION : Nom de règle valide (caractères alphanumériques, underscore, tiret)
  bool validName = true;
  for (size_t i = 0; i < newName.length(); i++) {
    char c = newName.charAt(i);
    if (!isAlphaNumeric(c) && c != '_' && c != '-') {
      validName = false;
      break;
    }
  }
  
  if (!validName) {
    request->send(400, "text/plain", "Le nom de règle ne peut contenir que des lettres, chiffres, _ et -");
    return;
  }

  // Lire le fichier rules.json
  File file = LittleFS.open("/config/rules.json", FILE_READ);
  SpiRamJsonDocument rulesDoc(100000);
  
  if (file) {
    deserializeJson(rulesDoc, file);
    file.close();
  } else {
    request->send(500, "text/plain", "Impossible d'ouvrir rules.json");
    return;
  }

  JsonArray rules = rulesDoc["rules"];
  
  // ✅ VALIDATION D'UNICITÉ : Vérifier si le nouveau nom existe déjà
  if (oldName != newName) {
    for (JsonObject rule : rules) {
      String existingName = rule["name"].as<String>();
      // Si un autre règle a déjà ce nom
      if (existingName == newName && existingName != oldName) {
        request->send(409, "text/plain", "Une règle avec ce nom existe déjà");
        return;
      }
    }
  }

  // Trouver et remplacer la règle
  bool found = false;
  
  for (size_t i = 0; i < rules.size(); i++) {
    if (rules[i]["name"].as<String>() == oldName) {
      found = true;
      
      // Remplacer la règle
      JsonObject rule = rules[i];
      rule["name"] = newName;
      rule["enabled"] = doc["enabled"] | true;
      rule["duration"] = doc["duration"] | 0;
      rule["repeat"]   = doc["repeat"] | false;
      rule["cooldown"] = doc["cooldown"] | 0;
      rule["maxExecPerDay"] = doc["maxExecPerDay"] | 0;

      //Remplacer le trigger
      rule.remove("trigger");
      JsonObject trigger = rule.createNestedObject("trigger");
      JsonObject triggerDoc = doc["trigger"].as<JsonObject>();
      trigger["mode"] = triggerDoc["mode"] | "timer";
      trigger["IEEE"] = triggerDoc["IEEE"] | "";
      trigger["cluster"] = triggerDoc["cluster"] | 0;
      trigger["attribute"] = triggerDoc["attribute"] | 0;

      // Supprimer les anciennes timeRanges si présentes
      rule.remove("timeRanges");

      // Remplacer les conditions
      rule.remove("conditions");
      JsonArray conditions = rule.createNestedArray("conditions");
      for (JsonObject cond : doc["conditions"].as<JsonArray>()) {
        JsonObject c = conditions.createNestedObject();
        c["type"] = cond["type"];
        c["IEEE"] = cond["IEEE"];
        c["cluster"] = cond["cluster"];
        c["attribute"] = cond["attribute"];
        c["operator"] = cond["operator"];
        c["value"] = cond["value"];
        c["value2"] = cond["value2"];
        c["logic"] = cond["logic"];
        // device_compare fields
        if (cond.containsKey("IEEE2")) c["IEEE2"] = cond["IEEE2"];
        if (cond.containsKey("cluster2")) c["cluster2"] = cond["cluster2"];
        if (cond.containsKey("attribute2")) c["attribute2"] = cond["attribute2"];
        if (cond.containsKey("subfield")) c["subfield"] = cond["subfield"];  // issue #31
      }

      // Remplacer les actions
      rule.remove("actions");
      JsonArray actions = rule.createNestedArray("actions");
      for (JsonObject act : doc["actions"].as<JsonArray>()) {
        JsonObject a = actions.createNestedObject();
        a["type"] = act["type"];
        a["IEEE"] = act["IEEE"];
        a["actionName"] = act["actionName"];
        a["endpoint"] = act["endpoint"];
        a["value"] = act["value"];
        a["title"] = act["title"];
        a["message"] = act["message"];
        // dynamic action fields
        if (act.containsKey("sourceIEEE")) a["sourceIEEE"] = act["sourceIEEE"];
        if (act.containsKey("sourceCluster")) a["sourceCluster"] = act["sourceCluster"];
        if (act.containsKey("sourceAttribute")) a["sourceAttribute"] = act["sourceAttribute"];
        if (act.containsKey("coefficient")) a["coefficient"] = act["coefficient"];
        if (act.containsKey("offset")) a["offset"] = act["offset"];
      }

      rule.remove("elseActions");
      JsonArray elseActions = rule.createNestedArray("elseActions");
      for (JsonObject act : doc["elseActions"].as<JsonArray>()) {
        JsonObject a = elseActions.createNestedObject();
        a["type"] = act["type"];
        a["IEEE"] = act["IEEE"];
        a["actionName"] = act["actionName"];
        a["endpoint"] = act["endpoint"];
        a["value"] = act["value"];
        a["title"] = act["title"];
        a["message"] = act["message"];
        // dynamic action fields
        if (act.containsKey("sourceIEEE")) a["sourceIEEE"] = act["sourceIEEE"];
        if (act.containsKey("sourceCluster")) a["sourceCluster"] = act["sourceCluster"];
        if (act.containsKey("sourceAttribute")) a["sourceAttribute"] = act["sourceAttribute"];
        if (act.containsKey("coefficient")) a["coefficient"] = act["coefficient"];
        if (act.containsKey("offset")) a["offset"] = act["offset"];
      }
      
      break;
    }
  }
  
  if (!found) {
    request->send(404, "text/plain", "Règle non trouvée");
    return;
  }

  // Sauvegarder le fichier
  file = LittleFS.open("/config/rules.json", FILE_WRITE);
  if (!file) {
    request->send(500, "text/plain", "Erreur d'écriture");
    return;
  }
  
  serializeJson(rulesDoc, file);
  file.close();

  // Recharger les règles
  rulesManager.loadFromFile("/config/rules.json");

  request->send(200, "text/plain", "Règle modifiée");
}

void APIDeleteRule(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  // Buffer statique pour accumuler les données
  static String jsonBuffer;
  
  // Premier chunk : réinitialiser le buffer
  if (index == 0) {
    jsonBuffer = "";
    jsonBuffer.reserve(total);
  }
  
  // Ajouter les données reçues au buffer
  for (size_t i = 0; i < len; i++) {
    jsonBuffer += (char)data[i];
  }
  
  // Si on n'a pas encore tout reçu, attendre le prochain chunk
  if (index + len < total) {
    return;
  }

  SpiRamJsonDocument doc(100000);
  DeserializationError error = deserializeJson(doc, jsonBuffer);
  
  // Libérer le buffer
  jsonBuffer = "";
  
  if (error) {
    request->send(400, "text/plain", "JSON invalide");
    return;
  }

  String ruleName = doc["name"].as<String>();
  
  if (ruleName.length() == 0) {
    request->send(400, "text/plain", "Nom de règle manquant");
    return;
  }

  // Lire le fichier rules.json
  File file = LittleFS.open("/config/rules.json", FILE_READ);
  SpiRamJsonDocument rulesDoc(100000);
  
  if (file) {
    deserializeJson(rulesDoc, file);
    file.close();
  } else {
    request->send(500, "text/plain", "Impossible d'ouvrir rules.json");
    return;
  }

  // Supprimer la règle
  JsonArray rules = rulesDoc["rules"];
  bool found = false;
  for (size_t i = 0; i < rules.size(); i++) {
    if (rules[i]["name"].as<String>() == ruleName) {
      rules.remove(i);
      found = true;
      break;
    }
  }

  if (!found) {
    request->send(404, "text/plain", "Règle non trouvée");
    return;
  }

  // Sauvegarder rules.json
  file = LittleFS.open("/config/rules.json", FILE_WRITE);
  if (!file) {
    request->send(500, "text/plain", "Erreur d'écriture rules.json");
    return;
  }
  serializeJson(rulesDoc, file);
  file.close();

  // ====== SUPPRIMER LE STATUT DE LA RÈGLE ======
  File statusFile = LittleFS.open("/config/statusRules.json", FILE_READ);
  if (statusFile) {
    SpiRamJsonDocument statusDoc(50000);
    DeserializationError statusError = deserializeJson(statusDoc, statusFile);
    statusFile.close();
    
    if (!statusError) {
      // Supprimer l'entrée correspondant à la règle
      if (statusDoc.containsKey(ruleName)) {
        statusDoc.remove(ruleName);
        
        // Sauvegarder statusRules.json
        statusFile = LittleFS.open("/config/statusRules.json", FILE_WRITE);
        if (statusFile) {
          serializeJson(statusDoc, statusFile);
          statusFile.close();
        }
      }
    }
  }

  // Recharger les règles
  rulesManager.loadFromFile("/config/rules.json");

  request->send(200, "text/plain", "Règle supprimée");
}

// API pour activer/désactiver une règle
void APIToggleRule(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  static String jsonBuffer;

  if (index == 0) {
    jsonBuffer = "";
    jsonBuffer.reserve(total);
  }

  for (size_t i = 0; i < len; i++) {
    jsonBuffer += (char)data[i];
  }

  if (index + len < total) {
    return;
  }

  SpiRamJsonDocument doc(10000);
  DeserializationError error = deserializeJson(doc, jsonBuffer);
  jsonBuffer = "";

  if (error) {
    request->send(400, "text/plain", "JSON invalide");
    return;
  }

  String ruleName = doc["name"].as<String>();
  bool enabled = doc["enabled"] | true;

  if (ruleName.length() == 0) {
    request->send(400, "text/plain", "Nom de règle manquant");
    return;
  }

  File file = LittleFS.open("/config/rules.json", FILE_READ);
  SpiRamJsonDocument rulesDoc(100000);

  if (file) {
    deserializeJson(rulesDoc, file);
    file.close();
  } else {
    request->send(500, "text/plain", "Impossible d'ouvrir rules.json");
    return;
  }

  JsonArray rules = rulesDoc["rules"];
  bool found = false;
  for (size_t i = 0; i < rules.size(); i++) {
    if (rules[i]["name"].as<String>() == ruleName) {
      rules[i]["enabled"] = enabled;
      found = true;
      break;
    }
  }

  if (!found) {
    request->send(404, "text/plain", "Règle non trouvée");
    return;
  }

  file = LittleFS.open("/config/rules.json", FILE_WRITE);
  if (!file) {
    request->send(500, "text/plain", "Erreur d'écriture");
    return;
  }
  serializeJson(rulesDoc, file);
  file.close();

  rulesManager.loadFromFile("/config/rules.json");

  request->send(200, "text/plain", enabled ? "Règle activée" : "Règle désactivée");
}

// Handler pour afficher la page
void handleAddRule(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result = F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += HTTP_ADD_RULE_HTML;
  result += HTTP_ADD_RULE_JS;
  result += footer();
  result += F("</html>");

  request->send(200, "text/html", result);
}

// API pour ajouter une règle
void APIAddRule(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  // Buffer statique pour accumuler les données
  static String jsonBuffer;
  
  // Premier chunk : réinitialiser le buffer
  if (index == 0) {
    jsonBuffer = "";
    jsonBuffer.reserve(total);  // Pré-allouer la mémoire
  }
  
  // Ajouter les données reçues au buffer
  for (size_t i = 0; i < len; i++) {
    jsonBuffer += (char)data[i];
  }
  
  // Si on n'a pas encore tout reçu, attendre le prochain chunk
  if (index + len < total) {
    return;
  }
  
  // Maintenant on a tout le JSON, on peut le parser
  SpiRamJsonDocument doc(100000);
  DeserializationError error = deserializeJson(doc, jsonBuffer);
  
  // Libérer le buffer
  jsonBuffer = "";
  
  if (error) {
    Serial.printf("JSON parse error: %s\n", error.c_str());
    request->send(400, "text/plain", "JSON invalide");
    return;
  }

  String ruleName = doc["name"].as<String>();
  
  // ✅ VALIDATION : Nom non vide
  if (ruleName.length() == 0) {
    request->send(400, "text/plain", "Le nom de la règle ne peut pas être vide");
    return;
  }

  // ✅ VALIDATION : Nom valide (caractères alphanumériques, underscore, tiret, espaces)
  bool validName = true;
  for (size_t i = 0; i < ruleName.length(); i++) {
    char c = ruleName.charAt(i);
    if (!isAlphaNumeric(c) && c != '_' && c != '-' && c != ' ') {
      validName = false;
      break;
    }
  }
  
  if (!validName) {
    request->send(400, "text/plain", "Le nom de règle ne peut contenir que des lettres, chiffres, _ et -");
    return;
  }

  // Lire le fichier rules.json existant
  File file = LittleFS.open("/config/rules.json", FILE_READ);
  SpiRamJsonDocument rulesDoc(100000);
  
  if (file) {
    deserializeJson(rulesDoc, file);
    file.close();
  } else {
    rulesDoc["rules"] = rulesDoc.createNestedArray();
  }

  // ✅ VALIDATION D'UNICITÉ
  JsonArray rules = rulesDoc["rules"];
  for (JsonObject rule : rules) {
    if (rule["name"].as<String>() == ruleName) {
      request->send(409, "text/plain", "Une règle avec ce nom existe déjà");
      return;
    }
  }

  // Ajouter la nouvelle règle
  JsonObject newRule = rules.createNestedObject();
  
  newRule["name"] = ruleName;
  newRule["enabled"] = doc["enabled"] | true;
  newRule["duration"] = doc["duration"] | 0;
  newRule["repeat"]   = doc["repeat"] | false;
  newRule["cooldown"] = doc["cooldown"] | 0;
  newRule["maxExecPerDay"] = doc["maxExecPerDay"] | 0;

  // Trigger
  JsonObject trigger = newRule.createNestedObject("trigger");
  JsonObject triggerDoc = doc["trigger"].as<JsonObject>();
  trigger["mode"] = triggerDoc["mode"] | "timer";
  trigger["IEEE"] = triggerDoc["IEEE"] | "";
  trigger["cluster"] = triggerDoc["cluster"] | 0;
  trigger["attribute"] = triggerDoc["attribute"] | 0;

  // Conditions
  JsonArray conditions = newRule.createNestedArray("conditions");
  for (JsonObject cond : doc["conditions"].as<JsonArray>()) {
    JsonObject c = conditions.createNestedObject();
    c["type"] = cond["type"];
    c["IEEE"] = cond["IEEE"];
    c["cluster"] = cond["cluster"];
    c["attribute"] = cond["attribute"];
    c["operator"] = cond["operator"];
    c["value"] = cond["value"];
    c["value2"] = cond["value2"];
    c["logic"] = cond["logic"];
    // device_compare fields
    if (cond.containsKey("IEEE2")) c["IEEE2"] = cond["IEEE2"];
    if (cond.containsKey("cluster2")) c["cluster2"] = cond["cluster2"];
    if (cond.containsKey("attribute2")) c["attribute2"] = cond["attribute2"];
    if (cond.containsKey("subfield")) c["subfield"] = cond["subfield"];  // issue #31
  }

  // Actions
  JsonArray actions = newRule.createNestedArray("actions");
  for (JsonObject act : doc["actions"].as<JsonArray>()) {
    JsonObject a = actions.createNestedObject();
    a["type"] = act["type"];
    a["IEEE"] = act["IEEE"];
    a["actionName"] = act["actionName"];
    a["endpoint"] = act["endpoint"];
    a["value"] = act["value"];
    a["title"] = act["title"];
    a["message"] = act["message"];
    // dynamic action fields
    if (act.containsKey("sourceIEEE")) a["sourceIEEE"] = act["sourceIEEE"];
    if (act.containsKey("sourceCluster")) a["sourceCluster"] = act["sourceCluster"];
    if (act.containsKey("sourceAttribute")) a["sourceAttribute"] = act["sourceAttribute"];
    if (act.containsKey("coefficient")) a["coefficient"] = act["coefficient"];
    if (act.containsKey("offset")) a["offset"] = act["offset"];
  }

  // Else Actions
  JsonArray elseActions = newRule.createNestedArray("elseActions");
  for (JsonObject act : doc["elseActions"].as<JsonArray>()) {
    JsonObject a = elseActions.createNestedObject();
    a["type"] = act["type"];
    a["IEEE"] = act["IEEE"];
    a["actionName"] = act["actionName"];
    a["endpoint"] = act["endpoint"];
    a["value"] = act["value"];
    a["title"] = act["title"];
    a["message"] = act["message"];
    // dynamic action fields
    if (act.containsKey("sourceIEEE")) a["sourceIEEE"] = act["sourceIEEE"];
    if (act.containsKey("sourceCluster")) a["sourceCluster"] = act["sourceCluster"];
    if (act.containsKey("sourceAttribute")) a["sourceAttribute"] = act["sourceAttribute"];
    if (act.containsKey("coefficient")) a["coefficient"] = act["coefficient"];
    if (act.containsKey("offset")) a["offset"] = act["offset"];
  }

  // Sauvegarder
  file = LittleFS.open("/config/rules.json", FILE_WRITE);
  if (!file) {
    request->send(500, "text/plain", "Erreur d'écriture");
    return;
  }
  
  serializeJson(rulesDoc, file);
  file.close();

  // Recharger les règles
  rulesManager.loadFromFile("/config/rules.json");

  request->send(200, "text/plain", "Règle ajoutée");
}

void handleConfigEnergy(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  PSRAMString result;
  String listLinky,listProd,ListGaz,ListWater,listDevicesAction;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_PARAM_ENERGY);
  result+=footer();
  result += F("</html>");

  result.replace("{{FormattedDate}}", FormattedDate);

  listLinky="<Select name='linkyDevice' class='form-select form-select-lg mb-3' aria-label='.form-select-lg example'><OPTION value=''>--Choix--</OPTION>";
  for (size_t i = 0; i < devices.size(); i++) 
  {
  
    DeviceData* device = devices[i];
    if (device->getInfo().model=="ZLinky_TIC")
    {
      listLinky += F("<OPTION value='");
      listLinky += device->getDeviceID();
      listLinky += F("' ");
      if (device->getDeviceID() == ConfigGeneral.ZLinky)
      {
        listLinky +="Selected";
      }
      listLinky += F(">");
      listLinky += F("ZLinky (");
      listLinky += device->getDeviceID();
      listLinky += F(")");
      listLinky += F("</OPTION>");
    }
  }
  listLinky +="</select>";

  //Trouver les appareils qui ont des actions
  listDevicesAction ="<div class='form-check'> ";
  int nbActionDevices = 0;
  String checked;
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getInfo().powerSocket.toInt())
    {
      //on affiche les appareils qui ont le cluster ONOFF
      String confDelestage = config_read("configGeneral.json","delestage");
      if (confDelestage.indexOf(device->getDeviceID())!=-1)
      {
        checked="Checked";
      }else{
        checked="";
      }
      listDevicesAction += "<input type='checkbox' name='delestage_"+device->getDeviceID()+"' "+checked+"> "+device->getInfo().model+" ("+device->getDeviceID()+")<br>";
      nbActionDevices++;
    }
  }
  listDevicesAction += "</div>";
  if (!nbActionDevices)
  {
    listDevicesAction =" Pas d'appareil disponible. Prise connectée obligatoire.";
  }

  listProd="<Select name='prodDevice' class='form-select form-select-lg mb-3' aria-label='.form-select-lg example'><OPTION value=''>--Choix--</OPTION>";
  for (size_t i = 0; i < devices.size(); i++) 
  {
  
    DeviceData* device = devices[i];
    if (device->getInfo().model=="ZLinky_TIC")
    {
      listProd += F("<OPTION value='");
      listProd += device->getDeviceID();
      listProd += F("' ");
      if (device->getDeviceID() == ConfigGeneral.Production)
      {
        listProd +="Selected";
      }
      listProd += F(">");
      listProd += F("ZLinky (");
      listProd += device->getDeviceID();
      listProd += F(")");
      listProd += F("</OPTION>");
    }
  }
  listProd +="</select>";

  ListGaz="<Select name='gazDevice' class='form-select form-select-lg mb-3' aria-label='.form-select-lg example'><OPTION value=''>--Choix--</OPTION>";
  for (size_t i = 0; i < devices.size(); i++) 
  {
  
    DeviceData* device = devices[i];
    if (device->getInfo().model=="ZiPulses")
    {
      ListGaz += F("<OPTION value='");
      ListGaz += device->getDeviceID();
      ListGaz += F("' ");
      if (device->getDeviceID() == ConfigGeneral.Gaz)
      {
        ListGaz +="Selected";
      }
      ListGaz += F(">");
      ListGaz += F("ZiPulses (");
      ListGaz += device->getDeviceID();
      ListGaz += F(")");
      ListGaz += F("</OPTION>");
    }
  }
  ListGaz +="</select>";

  ListWater="<Select name='waterDevice' class='form-select form-select-lg mb-3' aria-label='.form-select-lg example'><OPTION value=''>--Choix--</OPTION>";
  for (size_t i = 0; i < devices.size(); i++) 
  {
  
    DeviceData* device = devices[i];
    if (device->getInfo().model=="ZiPulses")
    {
      ListWater += F("<OPTION value='");
      ListWater += device->getDeviceID();
      ListWater += F("' ");
      if (device->getDeviceID() == ConfigGeneral.Water)
      {
        ListWater +="Selected";
      }
      ListWater += F(">");
      ListWater += F("ZiPulses (");
      ListWater += device->getDeviceID();
      ListWater += F(")");
      ListWater += F("</OPTION>");
    }
  }
  ListWater +="</select>";

  String ListPresence = "<Select name='presenceDevice' class='form-select form-select-lg mb-3'><OPTION value=''>--Aucun--</OPTION>";
  for (size_t i = 0; i < devices.size(); i++) 
  {
      DeviceData* device = devices[i];
      
      // Vérifier si le device a le cluster Occupancy (0x0406 = 1030) avec attribut 0x0000
      if (device->hasCluster(0x0406, 0x0000))
      {
          ListPresence += F("<OPTION value='");
          ListPresence += device->getDeviceID();
          ListPresence += F("'");
          if (device->getDeviceID() == String(ConfigGeneral.Presence))
          {
              ListPresence += F(" Selected");
          }
          ListPresence += F(">");
          if (device->getInfo().alias.length() > 0) {
              ListPresence += device->getInfo().alias;
              ListPresence += F(" (");
              ListPresence += device->getInfo().model;
              ListPresence += F(")");
          } else {
              ListPresence += device->getInfo().model;
              ListPresence += F(" (");
              ListPresence += device->getDeviceID();
              ListPresence += F(")");
          }
          ListPresence += F("</OPTION>");
      }
  }
  ListPresence += F("</select>");

  result.replace("{{selectDevicesAction}}", listDevicesAction);
  result.replace("{{selectDevices}}", listLinky);
  result.replace("{{shon}}", String(ConfigGeneral.HouseSurface));
  result.replace("{{tarifAbo}}", String(ConfigGeneral.tarifAbo));
  result.replace("{{tarifCSPE}}", String(ConfigGeneral.tarifCSPE));
  result.replace("{{tarifCTA}}", String(ConfigGeneral.tarifCTA));
  result.replace("{{tarifIdx1}}", String(ConfigGeneral.tarifIdx1));
  result.replace("{{tarifIdx2}}", String(ConfigGeneral.tarifIdx2));
  result.replace("{{tarifIdx3}}", String(ConfigGeneral.tarifIdx3));
  result.replace("{{tarifIdx4}}", String(ConfigGeneral.tarifIdx4));
  result.replace("{{tarifIdx5}}", String(ConfigGeneral.tarifIdx5));
  result.replace("{{tarifIdx6}}", String(ConfigGeneral.tarifIdx6));
  result.replace("{{tarifIdx7}}", String(ConfigGeneral.tarifIdx7));
  result.replace("{{tarifIdx8}}", String(ConfigGeneral.tarifIdx8));
  result.replace("{{tarifIdx9}}", String(ConfigGeneral.tarifIdx9));
  result.replace("{{tarifIdx10}}", String(ConfigGeneral.tarifIdx10));

  result.replace("{{selectDevicesProd}}", listProd);
  result.replace("{{tarifAboProd}}", String(ConfigGeneral.tarifAboProd));
  result.replace("{{tarifIdxProd}}", String(ConfigGeneral.tarifIdxProd));

  result.replace("{{selectDevicesGaz}}", ListGaz);
  result.replace("{{coeffGaz}}", String(ConfigGeneral.coeffGaz));
  result.replace("{{unitGaz}}", String(ConfigGeneral.unitGaz));
  result.replace("{{tarifGaz}}", String(ConfigGeneral.tarifGaz));

  result.replace("{{selectDevicesWater}}", ListWater);
  result.replace("{{coeffWater}}", String(ConfigGeneral.coeffWater));
  result.replace("{{unitWater}}", String(ConfigGeneral.unitWater));
  result.replace("{{tarifWater}}", String(ConfigGeneral.tarifWater));

  // Présence
  result.replace("{{selectDevicesPresence}}", ListPresence);
  if (ConfigGeneral.enablePresenceGraph)
  {
    result.replace("{{checkedEnablePresenceGraph}}", "Checked");
  }
  else
  {
    result.replace("{{checkedEnablePresenceGraph}}", "");
  }

  request->send(200, "text/html", result.c_str());
}

// ==================== Config Notifications ====================

void handleConfigNotifications(AsyncWebServerRequest *request) {
  if (!checkHeapForPage(request)) return;
  PSRAMString result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_NOTIFICATIONS);
  result += footer();
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);

  // Consommation
  result.replace("{{checkedNotifSubscribedPower}}", ConfigNotif.SubscribedPower ? "Checked" : "");
  result.replace("{{checkedNotifPowerOutage}}", ConfigNotif.PowerOutage ? "Checked" : "");
  result.replace("{{checkedNotifRedColor}}", ConfigNotif.RedColor ? "Checked" : "");
  result.replace("{{checkedNotifOverVoltage}}", ConfigNotif.OverVoltage ? "Checked" : "");
  result.replace("{{valOverVoltageThreshold}}", String(ConfigNotif.OverVoltageThreshold));
  result.replace("{{checkedNotifUnderVoltage}}", ConfigNotif.UnderVoltage ? "Checked" : "");
  result.replace("{{valUnderVoltageThreshold}}", String(ConfigNotif.UnderVoltageThreshold));
  result.replace("{{checkedNotifPriceChange}}", ConfigNotif.PriceChange ? "Checked" : "");
  result.replace("{{checkedNotifPEJP}}", ConfigNotif.PEJP ? "Checked" : "");
  result.replace("{{checkedNotifColorTomorrow}}", ConfigNotif.ColorTomorrow ? "Checked" : "");
  result.replace("{{checkedNotifOverBudget}}", ConfigNotif.OverBudget ? "Checked" : "");
  result.replace("{{valOverBudgetThreshold}}", String(ConfigNotif.OverBudgetThreshold));

  // Production
  result.replace("{{checkedNotifProdZero}}", ConfigNotif.ProdZero ? "Checked" : "");
  result.replace("{{checkedNotifProdSupConso}}", ConfigNotif.ProdSupConso ? "Checked" : "");

  // Autres (push + alertes avancées)
  result.replace("{{checkedNotifOverPower}}", ConfigNotif.OverPower ? "Checked" : "");
  result.replace("{{valOverPowerThreshold}}", String(ConfigNotif.OverPowerThreshold > 0 ? ConfigNotif.OverPowerThreshold : 6000));
  result.replace("{{valOverPowerDuration}}", String(ConfigNotif.OverPowerDuration > 0 ? ConfigNotif.OverPowerDuration : 60));
  result.replace("{{valOverPowerCooldown}}", String(ConfigNotif.OverPowerCooldown > 0 ? ConfigNotif.OverPowerCooldown : 30));
  result.replace("{{checkedNotifDailyAnomaly}}", ConfigNotif.DailyAnomaly ? "Checked" : "");
  result.replace("{{valDailyAnomalyPercent}}", String(ConfigNotif.DailyAnomalyPercent > 0 ? ConfigNotif.DailyAnomalyPercent : 30));
  result.replace("{{checkedNotifWaterLeak}}", ConfigNotif.WaterLeak ? "Checked" : "");
  result.replace("{{valWaterLeakThreshold}}", String(ConfigNotif.WaterLeakThreshold > 0 ? ConfigNotif.WaterLeakThreshold : 5));
  result.replace("{{checkedNotifNightWaterLeak}}", ConfigNotif.NightWaterLeak ? "Checked" : "");
  result.replace("{{valNightWaterLeakThreshold}}", String(ConfigNotif.NightWaterLeakThreshold > 0 ? ConfigNotif.NightWaterLeakThreshold : 1));
  result.replace("{{checkedNotifDailyMetrics}}", ConfigNotif.DailyMetrics ? "Checked" : "");

  request->send(200, "text/html", result.c_str());
}

void handleSaveConfigNotifications(AsyncWebServerRequest *request) {
  String path = "configGeneral.json";

  // Consommation - toggles
  ConfigNotif.SubscribedPower = (request->arg("NotifSubscribedPower") == "on");
  config_write(path, "SubscribedPower", ConfigNotif.SubscribedPower ? "1" : "0");

  ConfigNotif.PowerOutage = (request->arg("NotifPowerOutage") == "on");
  config_write(path, "PowerOutage", ConfigNotif.PowerOutage ? "1" : "0");

  ConfigNotif.RedColor = (request->arg("NotifRedColor") == "on");
  config_write(path, "RedColor", ConfigNotif.RedColor ? "1" : "0");

  ConfigNotif.OverVoltage = (request->arg("NotifOverVoltage") == "on");
  config_write(path, "OverVoltage", ConfigNotif.OverVoltage ? "1" : "0");

  ConfigNotif.OverVoltageThreshold = request->arg("NotifOverVoltageThreshold").toInt();
  config_write(path, "OverVoltageThreshold", String(ConfigNotif.OverVoltageThreshold));

  ConfigNotif.UnderVoltage = (request->arg("NotifUnderVoltage") == "on");
  config_write(path, "UnderVoltage", ConfigNotif.UnderVoltage ? "1" : "0");

  ConfigNotif.UnderVoltageThreshold = request->arg("NotifUnderVoltageThreshold").toInt();
  config_write(path, "UnderVoltageThreshold", String(ConfigNotif.UnderVoltageThreshold));

  ConfigNotif.PriceChange = (request->arg("NotifPriceChange") == "on");
  config_write(path, "PriceChange", ConfigNotif.PriceChange ? "1" : "0");

  ConfigNotif.PEJP = (request->arg("NotifPEJP") == "on");
  config_write(path, "PEJP", ConfigNotif.PEJP ? "1" : "0");

  ConfigNotif.ColorTomorrow = (request->arg("NotifColorTomorrow") == "on");
  config_write(path, "ColorTomorrow", ConfigNotif.ColorTomorrow ? "1" : "0");

  ConfigNotif.OverBudget = (request->arg("NotifOverBudget") == "on");
  config_write(path, "OverBudget", ConfigNotif.OverBudget ? "1" : "0");

  ConfigNotif.OverBudgetThreshold = request->arg("NotifOverBudgetThreshold").toInt();
  config_write(path, "OverBudgetThreshold", String(ConfigNotif.OverBudgetThreshold));

  // Production
  ConfigNotif.ProdSupConso = (request->arg("NotifProdSupConso") == "on");
  config_write(path, "ProdSupConso", ConfigNotif.ProdSupConso ? "1" : "0");

  ConfigNotif.ProdZero = (request->arg("NotifProdZero") == "on");
  config_write(path, "ProdZero", ConfigNotif.ProdZero ? "1" : "0");

  // Autres
  ConfigNotif.OverPower = (request->arg("NotifOverPower") == "on");
  config_write(path, "OverPower", ConfigNotif.OverPower ? "1" : "0");

  ConfigNotif.OverPowerThreshold = request->arg("NotifOverPowerThreshold").toInt();
  config_write(path, "OverPowerThreshold", String(ConfigNotif.OverPowerThreshold));

  ConfigNotif.OverPowerDuration = request->arg("NotifOverPowerDuration").toInt();
  config_write(path, "OverPowerDuration", String(ConfigNotif.OverPowerDuration));

  ConfigNotif.OverPowerCooldown = request->arg("NotifOverPowerCooldown").toInt();
  config_write(path, "OverPowerCooldown", String(ConfigNotif.OverPowerCooldown));

  ConfigNotif.DailyAnomaly = (request->arg("NotifDailyAnomaly") == "on");
  config_write(path, "DailyAnomaly", ConfigNotif.DailyAnomaly ? "1" : "0");

  ConfigNotif.DailyAnomalyPercent = request->arg("NotifDailyAnomalyPercent").toInt();
  config_write(path, "DailyAnomalyPercent", String(ConfigNotif.DailyAnomalyPercent));

  ConfigNotif.WaterLeak = (request->arg("NotifWaterLeak") == "on");
  config_write(path, "WaterLeak", ConfigNotif.WaterLeak ? "1" : "0");

  ConfigNotif.WaterLeakThreshold = request->arg("NotifWaterLeakThreshold").toInt();
  config_write(path, "WaterLeakThreshold", String(ConfigNotif.WaterLeakThreshold));

  ConfigNotif.NightWaterLeak = (request->arg("NotifNightWaterLeak") == "on");
  config_write(path, "NightWaterLeak", ConfigNotif.NightWaterLeak ? "1" : "0");

  ConfigNotif.NightWaterLeakThreshold = request->arg("NotifNightWaterLeakThreshold").toInt();
  config_write(path, "NightWaterLeakThreshold", String(ConfigNotif.NightWaterLeakThreshold));

  ConfigNotif.DailyMetrics = (request->arg("NotifDailyMetrics") == "on");
  config_write(path, "DailyMetrics", ConfigNotif.DailyMetrics ? "1" : "0");

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configNotifications"));
  request->send(response);
}

void handleConfigGaz(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result,list;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  //result += FPSTR(HTTP_CONFIG_GAZ);
  result+=footer();
  result += F("</html>");

  //result = getMenuGeneral(result, "gaz");

  result.replace("{{FormattedDate}}", FormattedDate);

  list="<Select name='gazDevice' class='form-select form-select-lg mb-3' aria-label='.form-select-lg example'><OPTION value=''>--Choix--</OPTION>";
  String str = "";
  File root = LittleFS.open("/db");
  File file = root.openNextFile();
  while (file)
  {
    String tmp = file.name();
    String mac = tmp.substring(0, 16);
    if (tmp.substring(16) == ".json")
    {
      String model;
      model = GetModel(file.name());
      if (model == "ZiPulses")
      { 
        list += F("<OPTION value='");
        list += mac;
        list += F("' ");
        if (strcmp(mac.c_str(),ConfigGeneral.Gaz)==0)
        {
          list +="Selected";
        }
        list += F(">");
        list += F("ZiPulses (");
        list += mac;
        list += F(")");
        list += F("</OPTION>");
      }
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  file.close();
  list +="</select>";

  result.replace("{{selectDevices}}", list);

  result.replace("{{tarifGaz}}", String(ConfigGeneral.tarifGaz));
  result.replace("{{coeffGaz}}", String(ConfigGeneral.coeffGaz));
  result.replace("{{unitGaz}}", String(ConfigGeneral.unitGaz));

  request->send(200, "text/html", result);
}

void handleConfigWater(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result,list;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
 // result += FPSTR(HTTP_CONFIG_WATER);
  result+=footer();
  result += F("</html>");

  //result = getMenuGeneral(result, "water");

  result.replace("{{FormattedDate}}", FormattedDate);

  list="<Select name='waterDevice' class='form-select form-select-lg mb-3' aria-label='.form-select-lg example'><OPTION value=''>--Choix--</OPTION>";
  String str = "";
  File root = LittleFS.open("/db");
  File file = root.openNextFile();
  while (file)
  {
    String tmp = file.name();
    String mac = tmp.substring(0, 16);
    if (tmp.substring(16) == ".json")
    {
      String model;
      model = GetModel(file.name());
      if (model == "ZiPulses")
      { 
        list += F("<OPTION value='");
        list += mac;
        list += F("' ");
        if (strcmp(mac.c_str(),ConfigGeneral.Water)==0)
        {
          list +="Selected";
        }
        list += F(">");
        list += F("ZiPulses (");
        list += mac;
        list += F(")");
        list += F("</OPTION>");
      }
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  file.close();
  list +="</select>";

  result.replace("{{selectDevices}}", list);

  result.replace("{{tarifWater}}", String(ConfigGeneral.tarifWater));
  result.replace("{{coeffWater}}", String(ConfigGeneral.coeffWater));
  result.replace("{{unitWater}}", String(ConfigGeneral.unitWater));
  request->send(200, "text/html", result);
}

void handleConfigNotificationMail(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_NOTIFICATION_MAIL);
  result+=footer();
  result += F("</html>");

  //result = getMenuGeneral(result, "notif");

  result.replace("{{FormattedDate}}", FormattedDate);
  if (ConfigSettings.enableNotif)
  {
    result.replace("{{checkedNotif}}", "Checked");
  }
  else
  {
    result.replace("{{checkedNotif}}", "");
  }
  result.replace("{{servSMTP}}", String(ConfigGeneral.servSMTP));
  result.replace("{{portSMTP}}", String(ConfigGeneral.portSMTP));
  result.replace("{{userSMTP}}", String(ConfigGeneral.userSMTP));
  result.replace("{{passSMTP}}", strlen(ConfigGeneral.passSMTP) > 0 ? "********" : "");

  request->send(200, "text/html", result);
}



void handleConfigWebPush(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_WEBPUSH);
  result+=footer();
  result += F("</html>");

  //result = getMenuGeneral(result, "webpush");

  result.replace("{{FormattedDate}}", FormattedDate);
  if (ConfigSettings.enableWebPush)
  {
    result.replace("{{checkedWebPush}}", "Checked");
    
  }
  else
  {
    result.replace("{{checkedWebPush}}", "");
    
  }
  if (ConfigGeneral.webPushAuth)
  {
    result.replace("{{checkedWebPushAuth}}", "Checked");
    result.replace("{{displayWebPushAuth}}","");
  }
  else
  {
    result.replace("{{checkedWebPushAuth}}", "");
    result.replace("{{displayWebPushAuth}}","display:none;");
  }

  String error ="Erreur : ";
  if (request->arg("error").toInt() > 0)
  {
    if ((request->arg("error").toInt() & 4) == 4)
    {
      result.replace("{{urlborder}}", "border:1px solid red;");
      error = error+"Serveur HTTP vide. <br>";
      
    }else{
      result.replace("{{urlborder}}", "");
    }

    if ((request->arg("error").toInt() & 2) == 2)
    {
      result.replace("{{userborder}}", "border:1px solid red;");
      error = error+"Identifiant manquant.<br>";
    }else{
      result.replace("{{userborder}}", "");
    }

    if ((request->arg("error").toInt() & 1) == 1)
    {
      result.replace("{{passborder}}", "border:1px solid red;");
      error = error + "Mot de passe trop court >= 4 caractères.<br>";
    }else{
      result.replace("{{passborder}}", "");
    }
    
   

    result.replace("{{error}}", error);
  }else{
    result.replace("{{error}}", "");
  }


  result.replace("{{servWebPush}}", String(ConfigGeneral.servWebPush));
  result.replace("{{userWebPush}}", String(ConfigGeneral.userWebPush));
  if (String(ConfigGeneral.passWebPush) !="")
  {
    result.replace("{{passWebPush}}", "********");
  }else{
    result.replace("{{passWebPush}}", "");
  }

  request->send(200, "text/html", result);
}

void handleConfigTunnel(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_TUNNEL);
  result += footer();
  result += F("</html>");

  result.replace("{{FormattedDate}}", FormattedDate);

  // Badge statut tunnel
  bool tunnelConnected = (tunnel != nullptr && tunnel->isConnected());
  if (tunnelConnected) {
    result.replace("{{badgeClass}}", "bg-success");
    result.replace("{{badgeText}}", "Connecté");
    String subdomain = tunnel->getSubdomain();
    if (subdomain.length() > 0) {
      String tunnelFullUrl = "https://" + subdomain;
      result.replace("{{tunnelUrl}}", "<div class='mb-3'><small class='text-muted'>URL :</small> <a href='" + tunnelFullUrl + "' target='_blank'>" + tunnelFullUrl + "</a></div>");
    } else {
      result.replace("{{tunnelUrl}}", "");
    }
  } else if (ConfigGeneral.enableTunnel) {
    result.replace("{{badgeClass}}", "bg-warning text-dark");
    result.replace("{{badgeText}}", "Déconnecté");
    result.replace("{{tunnelUrl}}", "");
  } else {
    result.replace("{{badgeClass}}", "bg-secondary");
    result.replace("{{badgeText}}", "Désactivé");
    result.replace("{{tunnelUrl}}", "");
  }

  // Toggle tunnel + protection accès distant
  bool viaTunnelPage = (request->client()->remoteIP() == IPAddress(127, 0, 0, 1));
  if (ConfigGeneral.enableTunnel)
  {
    if (viaTunnelPage) {
      result.replace("{{checkedTunnel}}", "checked disabled");
      result.replace("{{checkedTunnelManual}}", "checked disabled");
      result.replace("{{tunnelRemoteWarning}}",
        "<small class='text-warning'>Le tunnel ne peut être désactivé qu'à partir de l'adresse IP locale.</small>");
    } else {
      result.replace("{{checkedTunnel}}", "Checked");
      result.replace("{{checkedTunnelManual}}", "Checked");
      result.replace("{{tunnelRemoteWarning}}", "");
    }
  }
  else
  {
    result.replace("{{checkedTunnel}}", "");
    result.replace("{{checkedTunnelManual}}", "");
    result.replace("{{tunnelRemoteWarning}}", "");
  }

  result.replace("{{tunnelClientId}}", String(ConfigGeneral.tunnelClientId));

  if (String(ConfigGeneral.tunnelToken) != "")
  {
    result.replace("{{tunnelToken}}", "********");
  }
  else
  {
    result.replace("{{tunnelToken}}", "");
  }

  // Sécurité HTTP requise pour le tunnel
  if (!ConfigSettings.enableSecureHttp) {
    result.replace("{{securityWarning}}",
      "<div class='alert alert-warning mb-3'>"
      "<strong>S&eacute;curit&eacute; requise</strong> &mdash; "
      "L'acc&egrave;s s&eacute;curis&eacute; (identifiant + mot de passe) doit &ecirc;tre activ&eacute; avant de pouvoir utiliser le tunnel. "
      "<a href='/configHTTP' class='alert-link'>Configurer l'acc&egrave;s s&eacute;curis&eacute;</a>"
      "</div>");
    result.replace("{{disabledNoSec}}", "disabled");
  } else {
    result.replace("{{securityWarning}}", "");
    result.replace("{{disabledNoSec}}", "");
  }

  String error = "";
  if (request->arg("error").toInt() > 0)
  {
    if ((request->arg("error").toInt() & 1) == 1)
    {
      error = "Erreur : Client ID requis quand le tunnel est activé.";
    }
    if ((request->arg("error").toInt() & 2) == 2)
    {
      error = "Erreur : Token requis quand le tunnel est activé.";
    }
    if ((request->arg("error").toInt() & 4) == 4)
    {
      error = "Impossible de désactiver le tunnel depuis un accès distant.";
    }
    if ((request->arg("error").toInt() & 8) == 8)
    {
      error = "Erreur : L'acc&egrave;s s&eacute;curis&eacute; HTTP doit &ecirc;tre activ&eacute; avant d'activer le tunnel.";
    }
  }
  result.replace("{{error}}", error);

  request->send(200, "text/html", result);
}

void handleSaveConfigTunnel(AsyncWebServerRequest *request)
{
  String path = "configGeneral.json";
  String enableTunnel;
  bool saveOk = true;
  uint8_t error = 0;

  // Empêcher la désactivation du tunnel depuis le tunnel lui-même
  bool viaTunnel = (request->client()->remoteIP() == IPAddress(127, 0, 0, 1));
  if (viaTunnel && request->arg("enableTunnel") != "on") {
    AsyncWebServerResponse *response = request->beginResponse(302);
    response->addHeader(F("Location"), F("/configTunnel?error=4"));
    request->send(response);
    return;
  }

  // Sécurité HTTP requise pour activer le tunnel
  if (request->arg("enableTunnel") == "on" && !ConfigSettings.enableSecureHttp) {
    AsyncWebServerResponse *response = request->beginResponse(302);
    response->addHeader(F("Location"), F("/configTunnel?error=8"));
    request->send(response);
    return;
  }

  String clientId = request->arg("tunnelClientId");
  String token = request->arg("tunnelToken");

  // Validation si le tunnel est activé
  if (request->arg("enableTunnel") == "on")
  {
    if (clientId == "")
    {
      saveOk = false;
      error = error + 1;
    }
    if (token == "" && String(ConfigGeneral.tunnelToken) == "")
    {
      saveOk = false;
      error = error + 2;
    }
  }

  if (saveOk)
  {
    if (request->arg("enableTunnel") == "on")
    {
      enableTunnel = "1";
      ConfigGeneral.enableTunnel = true;
    }
    else
    {
      enableTunnel = "0";
      ConfigGeneral.enableTunnel = false;
    }
    config_write(path, "enableTunnel", enableTunnel);

    if (request->arg("tunnelClientId"))
    {
      strlcpy(ConfigGeneral.tunnelClientId, request->arg("tunnelClientId").c_str(), sizeof(ConfigGeneral.tunnelClientId));
      config_write(path, "tunnelClientId", String(request->arg("tunnelClientId")));
    }

    // Ne pas écraser le token si on reçoit ********
    if (token != "********" && token != "")
    {
      strlcpy(ConfigGeneral.tunnelToken, token.c_str(), sizeof(ConfigGeneral.tunnelToken));
      config_write(path, "tunnelToken", token);
    }

    // Hot reload du tunnel : arrêter l'ancien, démarrer le nouveau si activé
    if (tunnel != nullptr) {
      tunnel->stop();
      delete tunnel;
      tunnel = nullptr;
      Serial.println("[Tunnel] Service tunnel arrêté");
      addDebugLog("Tunnel reverse proxy arrêté");
    }

    if (ConfigGeneral.enableTunnel && strlen(ConfigGeneral.tunnelToken) > 0) {
      String tunnelUrl = "wss://remote.lixee-box.fr/tunnel?token=";
      tunnelUrl += ConfigGeneral.tunnelToken;
      if (strlen(ConfigGeneral.tunnelClientId) > 0) {
        tunnelUrl += "&clientId=";
        tunnelUrl += ConfigGeneral.tunnelClientId;
      }
      tunnel = new LiXeeBoxTunnel(tunnelUrl.c_str(), 80);
      tunnel->begin();
      Serial.println("[Tunnel] Service tunnel activé");
      addDebugLog("Tunnel reverse proxy activé");
    }

    AsyncWebServerResponse *response = request->beginResponse(302);
    response->addHeader(F("Location"), F("/configTunnel"));
    request->send(response);
  }
  else
  {
    AsyncWebServerResponse *response = request->beginResponse(302);
    response->addHeader(F("Location"), "/configTunnel?error=" + String(error));
    request->send(response);
  }
}

void handleConfigUdpClient(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_UDPCLIENT);
  
  result+=footer();
  result += F("</html>");

  result.replace("{{FormattedDate}}", FormattedDate);
  if (ConfigSettings.enableUDP)
  {
    result.replace("{{checkedUDP}}", "Checked"); 
  }
  else
  {
    result.replace("{{checkedUDP}}", "");
  }

  if (ConfigGeneral.customUDPJson != "")
  {
    result.replace("{{customUDPJson}}", ConfigGeneral.customUDPJson);
  }else{
    result.replace("{{customUDPJson}}", "");
  }

  String error ="Erreur : ";
  if (request->arg("error").toInt() > 0)
  {
    if ((request->arg("error").toInt() & 2) == 2)
    {
      result.replace("{{urlborder}}", "border:1px solid red;");
      error = error+"Serveur UDP manquant. <br>";
      
    }else{
      result.replace("{{urlborder}}", "");
    }

    if ((request->arg("error").toInt() & 1) == 1)
    {
      result.replace("{{portborder}}", "border:1px solid red;");
      error = error+"Port UDP manquant.<br>";
    }else{
      result.replace("{{portborder}}", "");
    }
    result.replace("{{error}}", error);
  }else{
    result.replace("{{error}}", "");
  }
  result.replace("{{servUDP}}", String(ConfigGeneral.servUDP));
  result.replace("{{portUDP}}", String(ConfigGeneral.portUDP));
  result.replace("{{customUDPJson}}", String(ConfigGeneral.customUDPJson));

  request->send(200, "text/html", result);

}


void handleConfigWifi(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_WIFI);
  result+=footer();
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);

  if (request->arg("error").toInt() > 0)
  {
    result.replace("{{error}}", "Erreur STA : Vérifier le SSID et le mot de passe (Taille >= 8 caractères)");
    if ((request->arg("error").toInt() & 1) == 1)
    {
      result.replace("{{passborder}}", "border:1px solid red;");
    }else{
      result.replace("{{passborder}}", "");
    }
    if ((request->arg("error").toInt() & 2) == 2)
    {
      result.replace("{{ssidborder}}", "border:1px solid red;");
    }else{
      result.replace("{{ssidborder}}", "");
    }


  }else{
    result.replace("{{error}}", "");
  }

  if (request->arg("ipError").toInt() >0 )
  {
    result.replace("{{ipError}}", "Erreur IP : Le format de l'adresse IP n'est pas correcte");
    if ((request->arg("ipError").toInt() & 1) == 1)
    {
      result.replace("{{ipborder}}", "border:1px solid red;");
    }else{
      result.replace("{{ipborder}}", "");
    }
    
    if ((request->arg("ipError").toInt() & 2) == 2)
    {
      result.replace("{{ipmask}}", "border:1px solid red;");
    }else{
      result.replace("{{ipmask}}", "");
    }
    
    if ((request->arg("ipError").toInt() & 4) == 4)
    {
      result.replace("{{ipgw}}", "border:1px solid red;");
    }else{
      result.replace("{{ipgw}}", "");
    }
  }else{
    result.replace("{{ipError}}", "");
  }

  if (ConfigSettings.enableDHCP)
  {
    result.replace("{{checkedDHCP}}", "Checked");
    result.replace("{{static}}", "none");
  }
  else
  {
    result.replace("{{checkedDHCP}}", "");
    result.replace("{{static}}", "block");
  }

  result.replace("{{ssid}}", String(ConfigSettings.ssid));
  if (String(ConfigSettings.password)!="")
  {
    result.replace("{{password}}", "********");
  }else{
    result.replace("{{password}}", "");
  }

  result.replace("{{ip}}", ConfigSettings.ipAddressWiFi);
  result.replace("{{mask}}", ConfigSettings.ipMaskWiFi);
  result.replace("{{gw}}", ConfigSettings.ipGWWiFi);

  request->send(200, "text/html", result);
}

void handleLogs(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;

  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);

  result += F("<h4>Console</h4>");
  result += F("<div class='row justify-content-md-center'>");
  result += F("<div class='col col-md-6'>");
  result += F("<button type='button' onclick='cmd(\"ClearConsole\");document.getElementById(\"console\").value=\"\";' class='btn btn-primary'>Clear Console</button> ");
  result += F("<button type='button' onclick='cmd(\"GetVersion\");' class='btn btn-primary'>Get Version</button> ");
  result += F("<button type='button' onclick='cmd(\"ErasePDM\");' class='btn btn-primary'>Erase PDM</button> ");
  result += F("<button type='button' onclick='cmd(\"Reset\");' class='btn btn-primary'>Reset</button> ");
  result += F("<button type='button' onclick='cmd(\"StartNwk\");' class='btn btn-primary'>StartNwk</button> ");
  result += F("<button type='button' onclick='cmd(\"PermitJoin\");' class='btn btn-primary'>Permit Join</button> ");
  result += F("<button type='button' onclick='cmd(\"RawMode\");' class='btn btn-primary'>RAW mode</button> ");
  result += F("<button type='button' onclick='cmd(\"RawModeOff\");' class='btn btn-primary'>RAW mode Off</button> ");
  result += F("<button type='button' onclick='cmd(\"ActiveReq\");' class='btn btn-primary'>ActiveReq</button> ");
  result += F("</div></div>");
  result += F("<div class='row justify-content-md-center' >");
  result += F("<div class='col col-md-6'>");

  result += F("Raw datas : <textarea id='console' rows='16' cols='100'>");

  result += F("</textarea></div></div>");
  // result += F("</div>");
  result += F("</body>");
  result += F("<script language='javascript'>");
  result += F("$(document).ready(function() {");
  result += F("logRefresh();});");
  result += F("</script>");

  result+=footer();
  result += F("</html>");
  request->send(200, F("text/html"), result);
}

void handleNotifications(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += FPSTR(HTTP_NOTIFICATION);
  result+=footer();
  result += F("</html>");
 
  request->send(200,"text/html", result);
}


void handleTools(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;

 /* AsyncWebServerResponse *response = request->beginChunkedResponse("text/plain",[](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
    
    String result; 
    result = F("<html>");
    result += FPSTR(HTTP_HEADER);
    result += FPSTR(HTTP_MENU);
    result.replace("{{FormattedDate}}", FormattedDate);
    result += FPSTR(HTTP_TOOLS);
    result+=footer();
    result += F("</html>");
    memcpy(buffer,result.c_str(),result.length());

    return result.length();
  });
  request->send(response);*/

  /*String result;
  
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += FPSTR(HTTP_TOOLS);
  result+=footer();
  result += F("</html>");

  request->send(200, F("text/html"), result);*/
  
  
  String result;
  //AsyncResponseStream *response = request->beginResponseStream("text/html");
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);

  result += FPSTR(HTTP_TOOLS);
  result+=footer();
  result += F("</html>");
 
  request->send(200,"text/html", result);

  /*AsyncWebServerResponse* response = request->beginChunkedResponse(contentType,
                                       [](uint8_t* buffer, size_t maxLen, size_t index) -> size_t 
    {

    });

  request->send(response);*/

}


void handleShelly(AsyncWebServerRequest * request) {
  String result;
  result = FPSTR(HTTP_SHELLY_EMULE);
  
  request->send(200,"application/json", result);
  
}

void handlePoll(AsyncWebServerRequest * request)
{
  String result = "";
  if (!notifList->isEmpty())
  {
    int i=0;
    result="{ \"notifications\" : [";
    while (!notifList->isEmpty())
    {
      Notification n = notifList->shift();
      if (i>0){result+=",";}
      result += F("{\"title\":\"");
      result += n.title;
      result += F("\",");
      result += F("\"message\":\"");
      result += n.message;
      result += F("\",");
      result += F("\"timeStamp\":\"");
      result += n.timeStamp;
      result += F("\",");
      result += F("\"type\":");
      result += n.type;
      i++;
      result += "}";
    }
    result +="]}";
  }

  request->send(200,"application/json", result);
}


void handleHelp(AsyncWebServerRequest * request) {
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += FPSTR(HTTP_HELP);
  result+=footer();
  result += F("</html>");
  result.replace("{{version}}", VERSION);
  
  request->send(200,"text/html", result);
  
}

void hard_restart()
{
  esp_task_wdt_init(1, true);
  esp_task_wdt_add(NULL);
  while (true)
    ;
}

void handleReboot(AsyncWebServerRequest *request)
{
  String result;

  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h4>Redémarrage ...</h4>");
  result = result + F("</body>");
  result+=footer();
  result += F("</html>");
   executeReboot=true;
  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/"));
  request->send(response);

}

/*static void ensureDirs(const String &fullPath)
{
    size_t pos = 1;                     // on saute le ‘/’ initial
    while ((pos = fullPath.indexOf('/', pos)) != -1) {
        String dir = fullPath.substring(0, pos);
        if (!LittleFS.exists(dir)) LittleFS.mkdir(dir);
        ++pos;
    }
}

void untarToLittleFS(const char *tarPath)
{
    mtar_t tar;
    mtar_open(&tar, tarPath, "r");

    mtar_header_t h;
    while (mtar_read_header(&tar, &h) != MTAR_ENULLRECORD) {
        esp_task_wdt_reset();
        String path = "/" + String(h.name);          // Chemin LittleFS voulu
        
        if (h.type == '5') {                     // Entrée = répertoire
            if (!LittleFS.exists(path)) LittleFS.mkdir(path);
            mtar_next(&tar);
            continue;
        }

        ensureDirs(path);                            // Crée les dossiers

        File f = LittleFS.open(path, FILE_WRITE);    // FILE_WRITE = "w+"
        if (!f) { Serial.printf("Can't open %s\n", path.c_str()); break; }

       //recopier le contenu sans tout charger en RAM  
        log_w("fichier : %s - size : %d",path.c_str(),h.size);
        uint8_t buf[512];
        uint32_t remaining = h.size;
        while (remaining) {
            uint32_t n = remaining > sizeof(buf) ? sizeof(buf) : remaining;
            mtar_read_data(&tar, buf, n);
            f.write(buf, n);
            remaining -= n;
            vTaskDelay(1);
        }
        f.close();

        mtar_next(&tar);                             // passe à l’en‑tête suivant
    }
    mtar_close(&tar);
}

void handleDoRestore(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
  String logmessage="";
  if (!index){
    DEBUG_PRINTLN(F("Restore start..."));
    request->_tempFile = LittleFS.open("/rt/" + filename, "w+");
  }

  if (len) {
    // stream the incoming chunk to the opened file
    request->_tempFile.write(data, len);
    
    logmessage = "Writing file: " + String(filename) + " index=" + String(index) + " len=" + String(len);
    DEBUG_PRINTLN(logmessage);
  }

  if (final) {
    // close the file handle as the upload is now done
    logmessage = "Upload Complete: " + String(filename) + ",size: " + String(index + len);
    request->_tempFile.close();
    DEBUG_PRINTLN(logmessage);
    String path = "/rt/"+filename;
    untarToLittleFS(path.c_str());
    request->redirect("/backup");
    
  }
}

*/

static void ensureDirs(const String &fullPath) {
  size_t pos = 1;
  while ((pos = fullPath.indexOf('/', pos)) != -1) {
    String dir = fullPath.substring(0, pos);
    if (!LittleFS.exists(dir)) LittleFS.mkdir(dir);
    ++pos;
  }
}

// parcourt le .tar, flash le firmware et écrit les autres fichiers
static void untarApplyAndRestore(const char *tarPath) {
  mtar_t tar;
  if (mtar_open(&tar, tarPath, "r") != 0) {
    log_e("mtar_open failed");
    return;
  }

  bool fwStarted = false;
  mtar_header_t h;
  while (mtar_read_header(&tar, &h) == MTAR_ESUCCESS) {
    esp_task_wdt_reset();
    if (tunnel) tunnel->loop();
    String name = String(h.name);
    // dossier ?
    if (h.type == '5') {
      String dir = "/" + name;
      if (!LittleFS.exists(dir)) LittleFS.mkdir(dir);
      mtar_next(&tar);
      continue;
    }
    esp_task_wdt_reset();
    // fichier
    if (name == "firmware.bin") {
      // démarrage de l'OTA
      if (!fwStarted) {
        if (!Update.begin(h.size, U_FLASH)) {
          Update.printError(Serial);
          return;
        }
        fwStarted = true;
      }
      // stream vers le flash
      uint32_t rem = h.size;
      uint8_t buf[512];
      while (rem) {
        esp_task_wdt_reset();
        if (tunnel) tunnel->loop();
        uint32_t n = rem > sizeof(buf) ? sizeof(buf) : rem;
        mtar_read_data(&tar, buf, n);
        if (Update.write(buf, n) != n) {
          Update.printError(Serial);
        }
        rem -= n;
      }
      esp_task_wdt_reset();
      // fin de l’image
      if (!Update.end(true)) {
        Update.printError(Serial);
      } else {
        log_i("Firmware flashed");
      }
    } else {
      // écriture dans LittleFS
      String path = "/" + name;
      ensureDirs(path);
      File f = LittleFS.open(path, FILE_WRITE);
      if (!f) {
        log_e("Can't open %s\n", path.c_str());
        break;
      }
      uint32_t rem = h.size;
      uint8_t buf[512];
      log_w("fichier : %s - size : %d",path.c_str(),h.size);
      while (rem) {
        esp_task_wdt_reset();
        if (tunnel) tunnel->loop();
        uint32_t n = rem > sizeof(buf) ? sizeof(buf) : rem;
        mtar_read_data(&tar, buf, n);
        f.write(buf, n);
        rem -= n;
      }
      f.close();
    }
    mtar_next(&tar);
  }
  mtar_close(&tar);

  LittleFS.remove(tarPath);

}

// handler unique pour l’upload .tar
void handleDoRestore(AsyncWebServerRequest *request,
                         const String& filename, size_t index,
                         uint8_t *data, size_t len, bool final) {
  static size_t content_len = 0;
  static const char *tmpPath = "/rt/upload.tar";
  if (!index) {
    content_len = request->contentLength(); 
    // premier chunk : créer le fichier temporaire
    if (LittleFS.exists(tmpPath)) LittleFS.remove(tmpPath);
    request->_tempFile = LittleFS.open(tmpPath, "w+");
    log_i("Upload start");
    updateStatus.statusManuel = "Téléchargement ...";
    updateStatus.progressManuel = 0;
  }
  esp_task_wdt_reset();
  // Pendant l'upload, calculer le pourcentage
  static size_t totalReceived = 0;
  if (!index) totalReceived = 0;
  totalReceived += len;

  if (content_len > 0) {
    int uploadPct = (totalReceived * 40) / content_len;
    updateStatus.progressManuel = 10 + uploadPct;  // ← 10-50%
    log_d("Upload progress: %d / %d bytes (%d%%)", 
          totalReceived, content_len, updateStatus.progressManuel);
  }
  // écrire chunk dans le .tar temporaire
  request->_tempFile.write(data, len);
  if (final) {
    esp_task_wdt_reset();
    request->_tempFile.close();
    updateStatus.statusManuel = "Installation ...";
    updateStatus.progressManuel = 60;

    delay(500);
    untarApplyAndRestore(tmpPath);
    esp_task_wdt_reset();
    updateStatus.statusManuel = "Redémarrage ...";
    updateStatus.progressManuel = 100;
    updateStatus.rebootRequested = true;

    executeReboot=true;

    request->send(200, "text/plain", "Mise à jour terminée");



  }
}

// ---- Chunked restore via tunnel (small HTTP requests) ----
// Each 8KB raw chunk is base64-encoded client-side (~11KB text),
// which stays under the 15KB WebSocket tunnel limit.
// Chunks are sent sequentially; file is kept open in append mode.

static struct {
    bool active;
    size_t totalSize;
    size_t received;
    size_t nextChunk;
    File file;
} _chunkedRestore = {false, 0, 0, 0, File()};

static const char* _crTmpPath = "/rt/upload.tar";

// Shared body accumulation handler (used by both init and chunk)
static void _crAccumBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (index == 0) {
        auto* buf = (uint8_t*)ps_malloc(total + 1);
        if (!buf) buf = (uint8_t*)malloc(total + 1);
        request->_tempObject = buf;
    }
    if (request->_tempObject) {
        memcpy((uint8_t*)request->_tempObject + index, data, len);
        if (index + len == total) {
            ((uint8_t*)request->_tempObject)[total] = '\0';
        }
    }
}

// POST /restoreInit  body: {"totalSize":12345}
void handleRestoreInitResponse(AsyncWebServerRequest *request) {
    if (!request->_tempObject) {
        request->send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }
    SpiRamJsonDocument doc(256);
    DeserializationError err = deserializeJson(doc, (const char*)request->_tempObject);
    free(request->_tempObject);
    request->_tempObject = nullptr;

    if (err || !doc.containsKey("totalSize")) {
        request->send(400, "application/json", "{\"error\":\"bad json\"}");
        return;
    }

    _chunkedRestore.totalSize = doc["totalSize"].as<size_t>();
    _chunkedRestore.received = 0;
    _chunkedRestore.nextChunk = 0;

    if (LittleFS.exists(_crTmpPath)) LittleFS.remove(_crTmpPath);
    _chunkedRestore.file = LittleFS.open(_crTmpPath, "w");
    if (!_chunkedRestore.file) {
        request->send(500, "application/json", "{\"error\":\"fs open\"}");
        return;
    }
    _chunkedRestore.active = true;

    updateStatus.statusManuel = "Téléchargement ...";
    updateStatus.progressManuel = 0;

    Serial.printf("[ChunkedRestore] Init: %u bytes\n", _chunkedRestore.totalSize);
    request->send(200, "application/json", "{\"ok\":true}");
}

// POST /restoreChunk?n=<index>  body: base64-encoded raw bytes
// Sequential append - chunks must arrive in order
void handleRestoreChunkResponse(AsyncWebServerRequest *request) {
    if (!_chunkedRestore.active || !_chunkedRestore.file) {
        if (request->_tempObject) { free(request->_tempObject); request->_tempObject = nullptr; }
        request->send(409, "application/json", "{\"error\":\"not initialized\"}");
        return;
    }
    if (!request->_tempObject) {
        request->send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }

    // Decode base64 body to raw binary
    const char* b64 = (const char*)request->_tempObject;
    size_t b64Len = strlen(b64);

    size_t outLen = 0;
    mbedtls_base64_decode(nullptr, 0, &outLen, (const uint8_t*)b64, b64Len);

    uint8_t* raw = (uint8_t*)ps_malloc(outLen);
    if (!raw) raw = (uint8_t*)malloc(outLen);
    if (!raw) {
        free(request->_tempObject); request->_tempObject = nullptr;
        request->send(500, "application/json", "{\"error\":\"OOM decode\"}");
        return;
    }

    size_t actualLen = 0;
    int ret = mbedtls_base64_decode(raw, outLen, &actualLen, (const uint8_t*)b64, b64Len);
    free(request->_tempObject);
    request->_tempObject = nullptr;

    if (ret != 0) {
        free(raw);
        request->send(400, "application/json", "{\"error\":\"b64 decode\"}");
        return;
    }

    // Append to file
    esp_task_wdt_reset();
    size_t written = _chunkedRestore.file.write(raw, actualLen);
    free(raw);

    _chunkedRestore.received += written;
    _chunkedRestore.nextChunk++;

    // Update progress (0-50% for upload phase)
    if (_chunkedRestore.totalSize > 0) {
        int pct = (_chunkedRestore.received * 50) / _chunkedRestore.totalSize;
        updateStatus.progressManuel = pct;
    }

    request->send(200, "application/json", "{\"ok\":true}");
}

// Flag checked in main loop() to apply the restore outside of AsyncTCP context
static volatile bool _restoreApplyPending = false;

// Called from main loop() - safe context for tunnel->loop() inside untarApplyAndRestore
void chunkedRestoreApplyIfPending() {
    if (!_restoreApplyPending) return;
    _restoreApplyPending = false;

    Serial.println("[ChunkedRestore] Applying update from main loop...");
    updateStatus.statusManuel = "Installation ...";
    updateStatus.progressManuel = 60;

    untarApplyAndRestore(_crTmpPath);
    esp_task_wdt_reset();

    updateStatus.statusManuel = "Redémarrage ...";
    updateStatus.progressManuel = 100;
    updateStatus.rebootRequested = true;
    executeReboot = true;
}

// POST /restoreFinish
void handleRestoreFinishResponse(AsyncWebServerRequest *request) {
    if (!_chunkedRestore.active) {
        request->send(409, "application/json", "{\"error\":\"not initialized\"}");
        return;
    }

    _chunkedRestore.file.close();
    _chunkedRestore.active = false;

    Serial.printf("[ChunkedRestore] File complete: %u bytes received\n", _chunkedRestore.received);

    // Defer untarApplyAndRestore to main loop - calling it here would crash
    // because we're in AsyncTCP callback context and tunnel->loop() inside
    // untarApplyAndRestore would re-enter the network stack.
    updateStatus.statusManuel = "Installation en attente ...";
    updateStatus.progressManuel = 55;
    _restoreApplyPending = true;

    request->send(200, "application/json", "{\"ok\":true}");
}

size_t content_len;
#define U_PART U_SPIFFS

void handleDoUpdate(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index){
    DEBUG_PRINTLN("Update");
    content_len = request->contentLength();
    // if filename includes spiffs, update the spiffs partition
    int cmd = (filename.indexOf("spiffs") > -1) ? U_PART : U_FLASH;
    if (!Update.begin(content_len, cmd)) {
      Update.printError(Serial);
    }
  }

  if (Update.write(data, len) != len) {
    Update.printError(Serial);
  }

  if (final) {
    AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "Attendre pendant le redémarrage");
    response->addHeader("Refresh", "20");  
    response->addHeader("Location", "/");
    request->send(response);
    if (!Update.end(true)){
      Update.printError(Serial);
    } else {
      DEBUG_PRINTLN("Update complete");
      Serial.flush();
      ESP.restart();
    }
  }
}

// ===== Chunked firmware update via tunnel =====
// Same pattern as chunked restore: 8KB raw → ~11KB base64 per chunk,
// under the 15KB WebSocket tunnel limit.
// Writes directly to flash via Update API (no temp file needed).

static struct {
    bool active;
    size_t totalSize;
    size_t received;
    size_t nextChunk;
    int cmd;  // U_FLASH or U_SPIFFS
} _chunkedFwUpdate = {false, 0, 0, 0, U_FLASH};

// POST /fwUpdateInit  body: {"totalSize":12345,"filename":"firmware.bin"}
void handleFwUpdateInitResponse(AsyncWebServerRequest *request) {
    if (!request->_tempObject) {
        request->send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }
    SpiRamJsonDocument doc(256);
    DeserializationError err = deserializeJson(doc, (const char*)request->_tempObject);
    free(request->_tempObject);
    request->_tempObject = nullptr;

    if (err || !doc.containsKey("totalSize")) {
        request->send(400, "application/json", "{\"error\":\"bad json\"}");
        return;
    }

    _chunkedFwUpdate.totalSize = doc["totalSize"].as<size_t>();
    _chunkedFwUpdate.received = 0;
    _chunkedFwUpdate.nextChunk = 0;

    // Detect partition type from filename
    String fname = doc["filename"] | "firmware.bin";
    _chunkedFwUpdate.cmd = (fname.indexOf("spiffs") > -1) ? U_PART : U_FLASH;

    if (!Update.begin(_chunkedFwUpdate.totalSize, _chunkedFwUpdate.cmd)) {
        Update.printError(Serial);
        request->send(500, "application/json", "{\"error\":\"update begin failed\"}");
        return;
    }

    _chunkedFwUpdate.active = true;

    updateStatus.statusManuel = "Téléchargement firmware ...";
    updateStatus.progressManuel = 0;

    Serial.printf("[ChunkedFwUpdate] Init: %u bytes, cmd=%d\n", _chunkedFwUpdate.totalSize, _chunkedFwUpdate.cmd);
    request->send(200, "application/json", "{\"ok\":true}");
}

// POST /fwUpdateChunk?n=<index>  body: base64-encoded raw bytes
void handleFwUpdateChunkResponse(AsyncWebServerRequest *request) {
    if (!_chunkedFwUpdate.active) {
        if (request->_tempObject) { free(request->_tempObject); request->_tempObject = nullptr; }
        request->send(409, "application/json", "{\"error\":\"not initialized\"}");
        return;
    }
    if (!request->_tempObject) {
        request->send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }

    // Decode base64 body to raw binary
    const char* b64 = (const char*)request->_tempObject;
    size_t b64Len = strlen(b64);

    size_t outLen = 0;
    mbedtls_base64_decode(nullptr, 0, &outLen, (const uint8_t*)b64, b64Len);

    uint8_t* raw = (uint8_t*)ps_malloc(outLen);
    if (!raw) raw = (uint8_t*)malloc(outLen);
    if (!raw) {
        free(request->_tempObject); request->_tempObject = nullptr;
        request->send(500, "application/json", "{\"error\":\"OOM decode\"}");
        return;
    }

    size_t actualLen = 0;
    int ret = mbedtls_base64_decode(raw, outLen, &actualLen, (const uint8_t*)b64, b64Len);
    free(request->_tempObject);
    request->_tempObject = nullptr;

    if (ret != 0) {
        free(raw);
        request->send(400, "application/json", "{\"error\":\"b64 decode\"}");
        return;
    }

    // Write to flash
    esp_task_wdt_reset();
    if (Update.write(raw, actualLen) != actualLen) {
        free(raw);
        Update.printError(Serial);
        request->send(500, "application/json", "{\"error\":\"flash write\"}");
        return;
    }
    free(raw);

    _chunkedFwUpdate.received += actualLen;
    _chunkedFwUpdate.nextChunk++;

    // Update progress (0-90% for upload phase)
    if (_chunkedFwUpdate.totalSize > 0) {
        int pct = (_chunkedFwUpdate.received * 90) / _chunkedFwUpdate.totalSize;
        updateStatus.progressManuel = pct;
    }

    request->send(200, "application/json", "{\"ok\":true}");
}

// POST /fwUpdateFinish
void handleFwUpdateFinishResponse(AsyncWebServerRequest *request) {
    if (!_chunkedFwUpdate.active) {
        request->send(409, "application/json", "{\"error\":\"not initialized\"}");
        return;
    }

    _chunkedFwUpdate.active = false;

    Serial.printf("[ChunkedFwUpdate] Complete: %u bytes received\n", _chunkedFwUpdate.received);

    if (!Update.end(true)) {
        Update.printError(Serial);
        request->send(500, "application/json", "{\"error\":\"update end failed\"}");
        return;
    }

    updateStatus.statusManuel = "Redémarrage ...";
    updateStatus.progressManuel = 100;
    updateStatus.rebootRequested = true;

    request->send(200, "application/json", "{\"ok\":true}");

    Serial.println("[ChunkedFwUpdate] Update complete, rebooting...");
    Serial.flush();
    delay(500);
    ESP.restart();
}

void handleDoUploadOTA(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
  String logmessage="";
  static String deviceId = ""; // Variable statique pour conserver l'ID
  if (!index){
    // Récupérer l'ID depuis les paramètres GET de l'URL
    if (request->hasParam("id")) {
        deviceId = request->getParam("id")->value()+".ota";
        DEBUG_PRINTLN("Device ID: " + deviceId);
    }
    request->_tempFile = LittleFS.open("/ota/" + deviceId, "w+");
  }

  if (len) {
    // stream the incoming chunk to the opened file
    request->_tempFile.write(data, len);
    
    logmessage = "Writing file: " + String(deviceId) + " index=" + String(index) + " len=" + String(len);
    DEBUG_PRINTLN(logmessage);
  }else{
    // close the file handle as the upload is now done
    logmessage = "Upload Complete: " + String(deviceId) + ",size: " + String(index + len);
    DEBUG_PRINTLN(logmessage);
    request->_tempFile.close();
    request->redirect("/configDevices");
  }

  if (final) {
    // close the file handle as the upload is now done
    logmessage = "Upload Complete: " + String(deviceId) + ",size: " + String(index + len);
    DEBUG_PRINTLN(logmessage);
    request->_tempFile.close();
    loadOTAFile(deviceId.c_str());
    
    request->redirect("/configDevices");
  }
}

// ---- Chunked OTA upload via tunnel ----
// Same pattern as chunked restore but writes to /ota/<deviceId>.ota

static struct {
    bool active;
    size_t totalSize;
    size_t received;
    String deviceId;
    File file;
} _chunkedOta = {false, 0, 0, "", File()};

// POST /otaInit?id=<deviceId>  body: {"totalSize":12345}
void handleOtaInitResponse(AsyncWebServerRequest *request) {
    if (!request->_tempObject) {
        request->send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }
    SpiRamJsonDocument doc(256);
    DeserializationError err = deserializeJson(doc, (const char*)request->_tempObject);
    free(request->_tempObject);
    request->_tempObject = nullptr;

    if (err || !doc.containsKey("totalSize")) {
        request->send(400, "application/json", "{\"error\":\"bad json\"}");
        return;
    }

    String deviceId = "";
    if (request->hasParam("id")) {
        deviceId = request->getParam("id")->value();
    }
    if (deviceId.length() == 0) {
        request->send(400, "application/json", "{\"error\":\"missing id\"}");
        return;
    }

    _chunkedOta.totalSize = doc["totalSize"].as<size_t>();
    _chunkedOta.received = 0;
    _chunkedOta.deviceId = deviceId + ".ota";

    String path = "/ota/" + _chunkedOta.deviceId;
    if (LittleFS.exists(path)) LittleFS.remove(path);
    _chunkedOta.file = LittleFS.open(path, "w");
    if (!_chunkedOta.file) {
        request->send(500, "application/json", "{\"error\":\"fs open\"}");
        return;
    }
    _chunkedOta.active = true;

    Serial.printf("[ChunkedOTA] Init: %u bytes for %s\n", _chunkedOta.totalSize, _chunkedOta.deviceId.c_str());
    request->send(200, "application/json", "{\"ok\":true}");
}

// POST /otaChunk  body: base64-encoded raw bytes
void handleOtaChunkResponse(AsyncWebServerRequest *request) {
    if (!_chunkedOta.active || !_chunkedOta.file) {
        if (request->_tempObject) { free(request->_tempObject); request->_tempObject = nullptr; }
        request->send(409, "application/json", "{\"error\":\"not initialized\"}");
        return;
    }
    if (!request->_tempObject) {
        request->send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }

    const char* b64 = (const char*)request->_tempObject;
    size_t b64Len = strlen(b64);

    size_t outLen = 0;
    mbedtls_base64_decode(nullptr, 0, &outLen, (const uint8_t*)b64, b64Len);

    uint8_t* raw = (uint8_t*)ps_malloc(outLen);
    if (!raw) raw = (uint8_t*)malloc(outLen);
    if (!raw) {
        free(request->_tempObject); request->_tempObject = nullptr;
        request->send(500, "application/json", "{\"error\":\"OOM\"}");
        return;
    }

    size_t actualLen = 0;
    int ret = mbedtls_base64_decode(raw, outLen, &actualLen, (const uint8_t*)b64, b64Len);
    free(request->_tempObject);
    request->_tempObject = nullptr;

    if (ret != 0) {
        free(raw);
        request->send(400, "application/json", "{\"error\":\"b64 decode\"}");
        return;
    }

    esp_task_wdt_reset();
    _chunkedOta.file.write(raw, actualLen);
    free(raw);
    _chunkedOta.received += actualLen;

    request->send(200, "application/json", "{\"ok\":true}");
}

// POST /otaFinish
void handleOtaFinishResponse(AsyncWebServerRequest *request) {
    if (!_chunkedOta.active) {
        request->send(409, "application/json", "{\"error\":\"not initialized\"}");
        return;
    }

    _chunkedOta.file.close();
    _chunkedOta.active = false;

    Serial.printf("[ChunkedOTA] Complete: %u bytes for %s\n", _chunkedOta.received, _chunkedOta.deviceId.c_str());

    loadOTAFile(_chunkedOta.deviceId.c_str());

    request->send(200, "application/json", "{\"ok\":true,\"redirect\":\"/configDevices\"}");
}

void handleDoUploadHistory(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
  String logmessage="";
  if (!index){
    request->_tempFile = LittleFS.open("/hst/" + filename, "w+");
  }

  if (len) {
    // stream the incoming chunk to the opened file
    request->_tempFile.write(data, len);
    
    logmessage = "Writing file: " + String(filename) + " index=" + String(index) + " len=" + String(len);
    DEBUG_PRINTLN(logmessage);
  }

  if (final) {
    // close the file handle as the upload is now done
    logmessage = "Upload Complete: " + String(filename) + ",size: " + String(index + len);
    request->_tempFile.close();
    request->redirect("/hst");
  }
}

// ============================================================================
// ZiGate Flash Handlers
// ============================================================================

// Structure to track flash progress
struct ZigateFlashProgress {
  bool active;
  int progress;
  String status;   // "idle", "uploading", "flashing", "success", "error"
  String message;
  uint8_t* firmwareBuffer;
  size_t firmwareSize;
  size_t firmwareAllocated;
  uint32_t baudRate;
  bool gpioMode;  // true = GPIO auto, false = manual mode
};

static ZigateFlashProgress zigateFlash = {false, 0, "idle", "", nullptr, 0, 0, 115200, true};

void handleZigateFlashUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    // First chunk - get parameters and allocate buffer in PSRAM
    zigateFlash.active = true;
    zigateFlash.status = "uploading";
    zigateFlash.progress = 0;
    zigateFlash.message = "Réception du firmware...";
    zigateFlash.firmwareSize = 0;

    // Get baudrate from URL parameter
    if (request->hasParam("baudrate")) {
      zigateFlash.baudRate = request->getParam("baudrate")->value().toInt();
      if (zigateFlash.baudRate < 9600 || zigateFlash.baudRate > 1000000) {
        zigateFlash.baudRate = 115200;  // Default
      }
    } else {
      zigateFlash.baudRate = 115200;
    }

    // Get mode from URL parameter
    if (request->hasParam("mode")) {
      zigateFlash.gpioMode = (request->getParam("mode")->value() == "gpio");
    } else {
      zigateFlash.gpioMode = false;  // Default to manual mode
    }

    DEBUG_PRINTLN("ZiGate flash: baudrate=" + String(zigateFlash.baudRate) + ", gpioMode=" + String(zigateFlash.gpioMode));

    // Allocate in PSRAM - max 1MB for firmware
    size_t allocSize = request->contentLength();
    if (allocSize == 0 || allocSize > 1024 * 1024) {
      allocSize = 512 * 1024; // Default 512KB
    }

    zigateFlash.firmwareBuffer = (uint8_t*)ps_malloc(allocSize);
    if (!zigateFlash.firmwareBuffer) {
      zigateFlash.status = "error";
      zigateFlash.message = "Erreur allocation mémoire";
      return;
    }
    zigateFlash.firmwareAllocated = allocSize;
    DEBUG_PRINTLN("ZiGate flash: allocated " + String(allocSize) + " bytes");
  }

  if (zigateFlash.firmwareBuffer && len > 0) {
    // Check bounds
    if (zigateFlash.firmwareSize + len <= zigateFlash.firmwareAllocated) {
      memcpy(zigateFlash.firmwareBuffer + zigateFlash.firmwareSize, data, len);
      zigateFlash.firmwareSize += len;

      // Update progress (upload phase = 0-50%)
      if (request->contentLength() > 0) {
        zigateFlash.progress = (zigateFlash.firmwareSize * 50) / request->contentLength();
      }
    }
  }

  if (final) {
    if (!zigateFlash.firmwareBuffer || zigateFlash.firmwareSize == 0) {
      zigateFlash.status = "error";
      zigateFlash.message = "Firmware vide ou invalide";
      request->send(400, "text/plain", "Firmware invalide");
      return;
    }

    DEBUG_PRINTLN("ZiGate flash: received " + String(zigateFlash.firmwareSize) + " bytes");

    // Start flash process in a separate task
    zigateFlash.status = "flashing";
    zigateFlash.message = "Démarrage du flash...";
    zigateFlash.progress = 50;

    // Create flash task
    xTaskCreatePinnedToCore(
      [](void* param) {
        // Initialize flasher
        if (!zigateFlasher) {
          zigateFlasher = new ZigateFlasher();
        }

        // Configure flasher with parameters from web request
        zigateFlasher->setBaudRate(zigateFlash.baudRate);
        zigateFlasher->setGpioControl(zigateFlash.gpioMode);

        log_i("ZiGate Flasher config: baudRate=%d, gpioMode=%s",
              zigateFlash.baudRate, zigateFlash.gpioMode ? "true" : "false");

        ZigateFlashStatus status = zigateFlasher->init();
        if (status != ZigateFlashStatus::OK) {
          zigateFlash.status = "error";
          zigateFlash.message = "Erreur init: " + String(zigateFlasher->getLastError());
          goto cleanup;
        }

        if (zigateFlash.gpioMode) {
          zigateFlash.message = "Connexion au bootloader (GPIO)...";
        } else {
          zigateFlash.message = "Connexion au bootloader (manuel)...";
        }
        zigateFlash.progress = 55;

        status = zigateFlasher->connect();
        if (status != ZigateFlashStatus::OK) {
          zigateFlash.status = "error";
          zigateFlash.message = "Erreur connexion: " + String(zigateFlasher->getLastError());
          goto cleanup;
        }

        zigateFlash.message = "Flash en cours...";

        // Flash with progress callback
        status = zigateFlasher->flash(
          zigateFlash.firmwareBuffer,
          zigateFlash.firmwareSize,
          [](int percent, const char* msg) {
            zigateFlash.progress = 55 + (percent * 40 / 100); // 55-95%
            if (msg) zigateFlash.message = msg;
          }
        );

        if (status == ZigateFlashStatus::OK) {
          zigateFlash.status = "success";
          zigateFlash.message = "Flash terminé avec succès!";
          zigateFlash.progress = 100;
        } else {
          zigateFlash.status = "error";
          zigateFlash.message = "Erreur flash: " + String(zigateFlasher->getLastError());
        }

      cleanup:
        // Free firmware buffer
        if (zigateFlash.firmwareBuffer) {
          free(zigateFlash.firmwareBuffer);
          zigateFlash.firmwareBuffer = nullptr;
        }
        zigateFlash.firmwareSize = 0;
        zigateFlash.firmwareAllocated = 0;
        zigateFlash.active = false;

        vTaskDelete(NULL);
      },
      "zigateFlash",
      8192,
      NULL,
      1,
      NULL,
      0  // Run on core 0
    );

    request->send(200, "text/plain", "Flash démarré");
  }
}

void handleZigateFlashStatus(AsyncWebServerRequest *request) {
  String json = "{";
  json += "\"status\":\"" + zigateFlash.status + "\",";
  json += "\"progress\":" + String(zigateFlash.progress) + ",";
  json += "\"message\":\"" + zigateFlash.message + "\"";
  json += "}";

  request->send(200, "application/json", json);
}

// ============================================================================

void handleToolCreateBackup(AsyncWebServerRequest *request)
{

  mtar_t tar;
  int error;
  error = mtar_open(&tar, "/bk/backup.tar", "w");
  DEBUG_PRINTLN(mtar_strerror(error));

  //backup database
  File root = LittleFS.open("/db");
  File file = root.openNextFile();
  esp_task_wdt_reset();
  while (file)
  {
    esp_task_wdt_reset();
    if (!file.isDirectory())
    {
      String tmp = F("db/");
      tmp += file.name();
      DEBUG_PRINT("mtar_write_file_header : ");
      DEBUG_PRINTLN(tmp.c_str());
      error = mtar_write_file_header(&tar, tmp.c_str(), file.size());
      DEBUG_PRINTLN(mtar_strerror(error));
      String buff="";
      while (file.available())
      { 
        buff+=(char)file.read();
      }
      DEBUG_PRINT("mtar_write_data : ");
      error = mtar_write_data(&tar, buff.c_str(), strlen(buff.c_str()));
      DEBUG_PRINTLN(mtar_strerror(error));
      file.close(); 
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  
  root.close();
  file.close();

  //backup config
  root = LittleFS.open("/config");
  file = root.openNextFile();
  esp_task_wdt_reset();
  while (file)
  {
    esp_task_wdt_reset();
    if (!file.isDirectory())
    {
      String tmp = F("config/");
      tmp += file.name();
      DEBUG_PRINT("mtar_write_file_header : ");
      DEBUG_PRINTLN(tmp.c_str());
      error = mtar_write_file_header(&tar, tmp.c_str(), file.size());
      DEBUG_PRINTLN(mtar_strerror(error));
      String buff="";
      while (file.available())
      { 
        buff+=(char)file.read();
      }
      DEBUG_PRINT("mtar_write_data : ");
      error = mtar_write_data(&tar, buff.c_str(), strlen(buff.c_str()));
      DEBUG_PRINTLN(mtar_strerror(error));
      file.close(); 
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  
  root.close();
  file.close();

  //backup debug
  root = LittleFS.open("/debug");
  file = root.openNextFile();
  esp_task_wdt_reset();
  while (file)
  {
    esp_task_wdt_reset();
    if (!file.isDirectory())
    {
      String tmp = F("debug/");
      tmp += file.name();
      DEBUG_PRINT("mtar_write_file_header : ");
      DEBUG_PRINTLN(tmp.c_str());
      error = mtar_write_file_header(&tar, tmp.c_str(), file.size());
      DEBUG_PRINTLN(mtar_strerror(error));
      String buff="";
      while (file.available())
      { 
        buff+=(char)file.read();
      }
      DEBUG_PRINT("mtar_write_data : ");
      error = mtar_write_data(&tar, buff.c_str(), strlen(buff.c_str()));
      DEBUG_PRINTLN(mtar_strerror(error));
      file.close(); 
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  
  root.close();
  file.close();


//backup history
  root = LittleFS.open("/hst");
  file = root.openNextFile();
  esp_task_wdt_reset();
  while (file)
  {
    esp_task_wdt_reset();
    if (!file.isDirectory())
    {
      String tmp = F("hst/");
      tmp += file.name();
      DEBUG_PRINT("mtar_write_file_header : ");
      DEBUG_PRINTLN(tmp.c_str());
      error = mtar_write_file_header(&tar, tmp.c_str(), file.size());
      DEBUG_PRINTLN(mtar_strerror(error));
      String buff="";
      while (file.available())
      { 
        buff+=(char)file.read();
      }
      DEBUG_PRINT("mtar_write_data : ");
      error = mtar_write_data(&tar, buff.c_str(), strlen(buff.c_str()));
      DEBUG_PRINTLN(mtar_strerror(error));
      file.close(); 
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  
  root.close();
  file.close();


  //backup template
  root = LittleFS.open("/tp");
  file = root.openNextFile();
  esp_task_wdt_reset();
  while (file)
  {
    esp_task_wdt_reset();
    if (!file.isDirectory())
    {
      String tmp = F("tp/");
      tmp += file.name();
      DEBUG_PRINT("mtar_write_file_header : ");
      DEBUG_PRINTLN(tmp.c_str());
      error = mtar_write_file_header(&tar, tmp.c_str(), file.size());
      DEBUG_PRINTLN(mtar_strerror(error));
      String buff="";
      while (file.available())
      { 
        buff+=(char)file.read();
      }
      DEBUG_PRINT("mtar_write_data : ");
      error = mtar_write_data(&tar, buff.c_str(), strlen(buff.c_str()));
      DEBUG_PRINTLN(mtar_strerror(error));
      file.close(); 
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  
  root.close();
  file.close();

  //backup backup
  root = LittleFS.open("/bk");
  file = root.openNextFile();
  esp_task_wdt_reset();
  while (file)
  {
    esp_task_wdt_reset();
    if (!file.isDirectory())
    {
      String tmp = F("bk/");
      tmp += file.name();
      if (strcmp(tmp.substring(19,24).c_str(),".json")==0)
      {
        DEBUG_PRINT("mtar_write_file_header : ");
        DEBUG_PRINTLN(tmp.c_str());
        error = mtar_write_file_header(&tar, tmp.c_str(), file.size());
        DEBUG_PRINTLN(mtar_strerror(error));
        String buff="";
        while (file.available())
        { 
          buff+=(char)file.read();
        }
        DEBUG_PRINT("mtar_write_data : ");
        error = mtar_write_data(&tar, buff.c_str(), strlen(buff.c_str()));
        DEBUG_PRINTLN(mtar_strerror(error));
      }
      
      file.close(); 
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  
  root.close();
  file.close();

  error =mtar_finalize(&tar);
  DEBUG_PRINTLN(mtar_strerror(error));
  error =mtar_close(&tar);
  DEBUG_PRINTLN(mtar_strerror(error));

 
  root = LittleFS.open("/bk");
  file = root.openNextFile();
  String listFiles="";
  esp_task_wdt_reset();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      if (tmp.substring((tmp.length()-3),tmp.length()) == "tar")
      {
        listFiles += F("<li><a href='web/");
        listFiles += tmp;
        listFiles += F("'>");
        listFiles += tmp;
        listFiles += F(" ( ");
        listFiles += file.size();
        listFiles += F(" o)</a></li>");
      } 
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  file.close();
  root.close();
  
  request->send(200, F("text/html"), listFiles);


}

void handleToolBackup(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);

  // CSS Styles
  result += F("<style>");
  result += F(".backup-container{max-width:800px;margin:0 auto;padding:20px;}");
  result += F(".backup-container svg{display:inline!important;vertical-align:middle!important;}");
  result += F(".page-title{margin-bottom:30px;padding-bottom:15px;border-bottom:2px solid #dee2e6;}");
  result += F(".page-title h2{margin:0;color:#333;font-size:1.5rem;}");
  result += F(".backup-card{background:#fff;border-radius:12px;box-shadow:0 2px 8px rgba(0,0,0,0.1);padding:25px;margin-bottom:20px;}");
  result += F(".backup-card h4{margin:0 0 15px 0;color:#333;font-size:1.1rem;}");
  result += F(".backup-card p{color:#666;margin-bottom:20px;line-height:1.6;}");
  result += F(".backup-info{background:#f8f9fa;border-radius:8px;padding:15px;margin-bottom:20px;}");
  result += F(".backup-info-item{display:flex;align-items:center;padding:8px 0;border-bottom:1px solid #e9ecef;}");
  result += F(".backup-info-item:last-child{border-bottom:none;}");
  result += F(".backup-info-item .label{color:#666;width:140px;flex-shrink:0;}");
  result += F(".backup-info-item .value{color:#333;font-weight:500;}");
  result += F(".btn-backup{display:inline-block;padding:12px 24px;font-size:1rem;border-radius:8px;border:none;cursor:pointer;text-decoration:none;}");
  result += F(".btn-backup-primary{background:#0d6efd;color:#fff;}");
  result += F(".btn-backup-primary:hover{background:#0b5ed7;}");
  result += F(".btn-backup-primary:disabled{background:#6c757d;cursor:not-allowed;}");
  result += F(".backup-status{margin-top:20px;padding:15px;border-radius:8px;display:none;}");
  result += F(".backup-status.show{display:block;}");
  result += F(".backup-status.loading{background:#e7f1ff;border:1px solid #b6d4fe;color:#084298;}");
  result += F(".backup-status.success{background:#d1e7dd;border:1px solid #badbcc;color:#0f5132;}");
  result += F(".backup-status.error{background:#f8d7da;border:1px solid #f5c2c7;color:#842029;}");
  result += F(".spinner{display:inline-block;width:20px;height:20px;border:2px solid #b6d4fe;border-top-color:#0d6efd;border-radius:50%;animation:spin 1s linear infinite;margin-right:10px;}");
  result += F("@keyframes spin{to{transform:rotate(360deg);}}");
  result += F(".section-title{margin:30px 0 15px 0;color:#333;font-size:1rem;font-weight:600;}");
  result += F(".file-list{list-style:none;padding:0;margin:0;}");
  result += F(".file-item{display:flex;align-items:center;justify-content:space-between;padding:12px 15px;background:#f8f9fa;border-radius:8px;margin-bottom:8px;}");
  result += F(".file-item:hover{background:#e9ecef;}");
  result += F(".file-info{display:flex;align-items:center;}");
  result += F(".file-name{color:#333;font-weight:500;margin:0 10px;}");
  result += F(".file-size{color:#666;font-size:0.85rem;}");
  result += F(".file-actions{display:flex;gap:8px;}");
  result += F(".btn-sm{width:36px;height:36px;border-radius:6px;border:none;cursor:pointer;display:inline-flex;align-items:center;justify-content:center;}");
  result += F(".btn-success{background:#198754;color:#fff;}");
  result += F(".btn-success:hover{background:#157347;}");
  result += F(".btn-danger{background:#dc3545;color:#fff;}");
  result += F(".btn-danger:hover{background:#bb2d3b;}");
  result += F(".empty-state{text-align:center;padding:30px;color:#666;}");
  result += F(".btn-primary{background:#0d6efd;color:#fff;}.btn-primary:hover{background:#0b5ed7;}");
  result += F(".bk-overlay{position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.5);z-index:1000;display:none;justify-content:center;align-items:center;}");
  result += F(".bk-modal{background:#fff;border-radius:12px;max-width:700px;width:90%;max-height:80vh;overflow:auto;padding:25px;}");
  result += F(".bk-modal pre{background:#f8f9fa;padding:15px;border-radius:8px;overflow-x:auto;font-size:0.85em;max-height:50vh;overflow-y:auto;}");
  result += F("@media(max-width:576px){.backup-container{padding:15px;}.backup-card{padding:20px;}.backup-info-item{flex-direction:column;align-items:flex-start;}.backup-info-item .label{width:auto;margin-bottom:4px;}}");
  result += F("</style>");

  // HTML Content
  result += F("<div class='backup-container'>");

  // Header
  result += F("<div class='page-title'>");
  result += F("<h2><svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' fill='#0d6efd' style='margin-right:10px;width:24px' viewBox='0 0 16 16'><path fill-rule='evenodd' d='M7.646 5.146a.5.5 0 0 1 .708 0l2 2a.5.5 0 0 1-.708.708L8.5 6.707V10.5a.5.5 0 0 1-1 0V6.707L6.354 7.854a.5.5 0 1 1-.708-.708l2-2z'/><path d='M4.406 3.342A5.53 5.53 0 0 1 8 2c2.69 0 4.923 2 5.166 4.579C14.758 6.804 16 8.137 16 9.773 16 11.569 14.502 13 12.687 13H3.781C1.708 13 0 11.366 0 9.318c0-1.763 1.266-3.223 2.942-3.593.143-.863.698-1.723 1.464-2.383zm.653.757c-.757.653-1.153 1.44-1.153 2.056v.448l-.445.049C2.064 6.805 1 7.952 1 9.318 1 10.785 2.23 12 3.781 12h8.906C13.98 12 15 10.988 15 9.773c0-1.216-1.02-2.228-2.313-2.228h-.5v-.5C12.188 4.825 10.328 3 8 3a4.53 4.53 0 0 0-2.941 1.1z'/></svg>Sauvegarde</h2>");
  result += F("</div>");

  // Create Backup Card
  result += F("<div class='backup-card'>");
  result += F("<h4><svg xmlns='http://www.w3.org/2000/svg' width='18' height='18' fill='#0d6efd' style='margin-right:10px;width:18px' viewBox='0 0 16 16'><path d='M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14zm0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16z'/><path d='M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4z'/></svg>Créer une sauvegarde</h4>");
  result += F("<p>Créez une sauvegarde de vos données utilisateur. Ce fichier peut être utilisé lors d'une mise à jour manuelle pour restaurer vos paramètres.</p>");

  // Info about what's backed up
  result += F("<div class='backup-info'>");
  result += F("<div class='backup-info-item'><span class='label'>Appareils</span><span class='value'>/db/</span></div>");
  result += F("<div class='backup-info-item'><span class='label'>Configuration</span><span class='value'>/config/</span></div>");
  result += F("<div class='backup-info-item'><span class='label'>Debug</span><span class='value'>/debug/</span></div>");
  result += F("<div class='backup-info-item'><span class='label'>Templates</span><span class='value'>/tp/</span></div>");
  result += F("<div class='backup-info-item'><span class='label'>Historique</span><span class='value'>/hst/</span></div>");
  result += F("</div>");

  result += F("<button class='btn-backup btn-backup-primary' id='btnBackup' onclick='createBackup()'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' style='margin-right:10px;width:16px' viewBox='0 0 16 16'><path d='M.5 9.9a.5.5 0 0 1 .5.5v2.5a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-2.5a.5.5 0 0 1 1 0v2.5a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2v-2.5a.5.5 0 0 1 .5-.5z'/><path d='M7.646 11.854a.5.5 0 0 0 .708 0l3-3a.5.5 0 0 0-.708-.708L8.5 10.293V1.5a.5.5 0 0 0-1 0v8.793L5.354 8.146a.5.5 0 1 0-.708.708l3 3z'/></svg>Créer la sauvegarde</button>");

  result += F("<div class='backup-status' id='backupStatus'>");
  result += F("<div class='status-content'><span class='spinner' id='statusSpinner'></span><span id='statusText'></span></div>");
  result += F("</div>");
  result += F("</div>");

  // List of existing backups
  result += F("<h5 class='section-title'><svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='#0d6efd' style='margin-right:10px;width:16px' viewBox='0 0 16 16'><path d='M9.293 0H4a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h8a2 2 0 0 0 2-2V4.707A1 1 0 0 0 13.707 4L10 .293A1 1 0 0 0 9.293 0zM9.5 3.5v-2l3 3h-2a1 1 0 0 1-1-1zM4.5 9a.5.5 0 0 1 0-1h7a.5.5 0 0 1 0 1h-7zM4 10.5a.5.5 0 0 1 .5-.5h7a.5.5 0 0 1 0 1h-7a.5.5 0 0 1-.5-.5zm.5 2.5a.5.5 0 0 1 0-1h4a.5.5 0 0 1 0 1h-4z'/></svg>Sauvegardes existantes</h5>");
  result += F("<ul class='file-list' id='backupList'>");

  // List backup files from /bk directory
  File root = LittleFS.open("/bk");
  File file = root.openNextFile();
  bool hasFiles = false;

  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      // Filtrer uniquement les fichiers .tar
      if (!tmp.endsWith(".tar")) {
        file.close();
        file = root.openNextFile();
        continue;
      }
      hasFiles = true;
      int fileSize = file.size();
      result += F("<li class='file-item'>");
      result += F("<div class='file-info'>");
      result += F("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='#6c757d' style='width:16px' viewBox='0 0 16 16'><path d='M4 0h5.293A1 1 0 0 1 10 .293L13.707 4a1 1 0 0 1 .293.707V14a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V2a2 2 0 0 1 2-2zm5.5 1.5v2a1 1 0 0 0 1 1h2l-3-3z'/></svg>");
      result += F("<span class='file-name'>");
      result += tmp;
      result += F("</span><span class='file-size'>(");
      if (fileSize > 1024) {
        result += String(fileSize / 1024);
        result += F(" Ko");
      } else {
        result += String(fileSize);
        result += F(" o");
      }
      result += F(")</span></div>");
      result += F("<div class='file-actions'>");
      result += F("<a href='/web/");
      result += tmp;
      result += F("' download class='btn-sm btn-success' title='Télécharger'>");
      result += F("<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' fill='currentColor' style='width:14px' viewBox='0 0 16 16'><path d='M.5 9.9a.5.5 0 0 1 .5.5v2.5a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-2.5a.5.5 0 0 1 1 0v2.5a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2v-2.5a.5.5 0 0 1 .5-.5z'/><path d='M7.646 11.854a.5.5 0 0 0 .708 0l3-3a.5.5 0 0 0-.708-.708L8.5 10.293V1.5a.5.5 0 0 0-1 0v8.793L5.354 8.146a.5.5 0 1 0-.708.708l3 3z'/></svg>");
      result += F("</a>");
      result += F("<button class='btn-sm btn-danger' onclick=\"deleteBackup('");
      result += tmp;
      result += F("')\" title='Supprimer'>");
      result += F("<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' fill='currentColor' style='width:14px' viewBox='0 0 16 16'><path d='M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5zm2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5zm3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0V6z'/><path fill-rule='evenodd' d='M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H6a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1h3.5a1 1 0 0 1 1 1v1zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4H4.118zM2.5 3V2h11v1h-11z'/></svg>");
      result += F("</button>");
      result += F("</div>");
      result += F("</li>");
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  root.close();
  file.close();

  if (!hasFiles) {
    result += F("<li class='empty-state'><p>Aucune sauvegarde disponible</p></li>");
  }

  result += F("</ul>");

  // Individual backup files (.json) from /bk/
  result += F("<h5 class='section-title'><svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='#0d6efd' style='margin-right:10px;width:16px' viewBox='0 0 16 16'><path d='M6 .5a.5.5 0 0 0-.5-.5h-3a.5.5 0 0 0-.5.5V3H.5a.5.5 0 0 0 0 1H2v.5a.5.5 0 0 0 .5.5h3a.5.5 0 0 0 .5-.5V4h3.5a.5.5 0 0 0 0-1H6V.5zM5 4V1h-2v3h2z'/><path d='M2 7v7a1 1 0 0 0 1 1h10a1 1 0 0 0 1-1V7H2zm2 1h8v1H4V8z'/></svg>Backups individuels (appareils)</h5>");
  result += F("<ul class='file-list' id='bkJsonList'>");

  {
    File rootBk = LittleFS.open("/bk");
    File fileBk = rootBk.openNextFile();
    bool hasBkJson = false;

    while (fileBk)
    {
      if (!fileBk.isDirectory())
      {
        String bkName = fileBk.name();
        if (bkName.endsWith(".json"))
        {
          hasBkJson = true;
          int bkSize = fileBk.size();
          result += F("<li class='file-item'>");
          result += F("<div class='file-info'>");
          result += F("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='#6c757d' style='width:16px' viewBox='0 0 16 16'><path d='M14 4.5V14a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V2a2 2 0 0 1 2-2h5.5L14 4.5zM9.5 3A1.5 1.5 0 0 1 8 1.5V0H4a1 1 0 0 0-1 1v14a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1V4.5H9.5z'/></svg>");
          result += F("<span class='file-name'>");
          result += bkName;
          result += F("</span><span class='file-size'>(");
          if (bkSize > 1024) {
            result += String(bkSize / 1024);
            result += F(" Ko");
          } else {
            result += String(bkSize);
            result += F(" o");
          }
          result += F(")</span></div>");
          result += F("<div class='file-actions'>");
          // View button (blue eye)
          result += F("<button class='btn-sm btn-primary' onclick=\"viewBkFile('");
          result += bkName;
          result += F("')\" title='Voir le contenu'>");
          result += F("<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' fill='currentColor' style='width:14px' viewBox='0 0 16 16'><path d='M16 8s-3-5.5-8-5.5S0 8 0 8s3 5.5 8 5.5S16 8 16 8zM1.173 8a13.133 13.133 0 0 1 1.66-2.043C4.12 4.668 5.88 3.5 8 3.5c2.12 0 3.879 1.168 5.168 2.457A13.133 13.133 0 0 1 14.828 8c-.058.087-.122.183-.195.288-.335.48-.83 1.12-1.465 1.755C11.879 11.332 10.119 12.5 8 12.5c-2.12 0-3.879-1.168-5.168-2.457A13.134 13.134 0 0 1 1.172 8z'/><path d='M8 5.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5zM4.5 8a3.5 3.5 0 1 1 7 0 3.5 3.5 0 0 1-7 0z'/></svg>");
          result += F("</button>");
          // Restore button (green arrow)
          result += F("<button class='btn-sm btn-success' onclick=\"restoreBkFile('");
          result += bkName;
          result += F("')\" title='Restaurer vers /db/'>");
          result += F("<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' fill='currentColor' style='width:14px' viewBox='0 0 16 16'><path fill-rule='evenodd' d='M8 3a5 5 0 1 0 4.546 2.914.5.5 0 0 1 .908-.417A6 6 0 1 1 8 2v1z'/><path d='M8 4.466V.534a.25.25 0 0 1 .41-.192l2.36 1.966c.12.1.12.284 0 .384L8.41 4.658A.25.25 0 0 1 8 4.466z'/></svg>");
          result += F("</button>");
          result += F("</div>");
          result += F("</li>");
        }
      }
      fileBk.close();
      vTaskDelay(1);
      fileBk = rootBk.openNextFile();
    }
    rootBk.close();
    fileBk.close();

    if (!hasBkJson) {
      result += F("<li class='empty-state'><p>Aucun backup individuel</p></li>");
    }
  }

  result += F("</ul>");

  // Modal for viewing backup file content
  result += F("<div class='bk-overlay' id='bkModal' onclick='closeBkModal(event)'>");
  result += F("<div class='bk-modal' onclick='event.stopPropagation()'>");
  result += F("<h4 id='bkModalTitle' style='margin:0 0 15px 0;'>...</h4>");
  result += F("<pre id='bkModalContent'>Chargement...</pre>");
  result += F("<button class='btn-backup' onclick='closeBkModal()' style='margin-top:15px;background:#6c757d;color:#fff;'>Fermer</button>");
  result += F("</div></div>");

  result += F("</div>");

  // JavaScript
  result += F("<script>");
  result += F("function createBackup(){");
  result += F("var btn=$('#btnBackup');");
  result += F("var status=$('#backupStatus');");
  result += F("btn.prop('disabled',true).html('<span class=\"spinner\" style=\"width:16px;height:16px;border-width:2px;\"></span> Création en cours...');");
  result += F("status.removeClass('success error').addClass('loading show');");
  result += F("$('#statusSpinner').show();");
  result += F("$('#statusText').text('Création de la sauvegarde en cours...');");
  result += F("$.get('/createBackupFile',function(data){");
  result += F("status.removeClass('loading').addClass('success');");
  result += F("$('#statusSpinner').hide();");
  result += F("$('#statusText').html('Sauvegarde créée avec succès! <a href=\"/web/backup.tar\" download>Télécharger</a>');");
  result += F("btn.prop('disabled',false).html('<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"18\" height=\"18\" fill=\"currentColor\" viewBox=\"0 0 16 16\"><path d=\"M.5 9.9a.5.5 0 0 1 .5.5v2.5a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-2.5a.5.5 0 0 1 1 0v2.5a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2v-2.5a.5.5 0 0 1 .5-.5z\"/><path d=\"M7.646 11.854a.5.5 0 0 0 .708 0l3-3a.5.5 0 0 0-.708-.708L8.5 10.293V1.5a.5.5 0 0 0-1 0v8.793L5.354 8.146a.5.5 0 1 0-.708.708l3 3z\"/></svg> Créer la sauvegarde');");
  result += F("setTimeout(function(){location.reload();},2000);");
  result += F("}).fail(function(){");
  result += F("status.removeClass('loading').addClass('error');");
  result += F("$('#statusSpinner').hide();");
  result += F("$('#statusText').text('Erreur lors de la création de la sauvegarde');");
  result += F("btn.prop('disabled',false).html('<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"18\" height=\"18\" fill=\"currentColor\" viewBox=\"0 0 16 16\"><path d=\"M.5 9.9a.5.5 0 0 1 .5.5v2.5a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-2.5a.5.5 0 0 1 1 0v2.5a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2v-2.5a.5.5 0 0 1 .5-.5z\"/><path d=\"M7.646 11.854a.5.5 0 0 0 .708 0l3-3a.5.5 0 0 0-.708-.708L8.5 10.293V1.5a.5.5 0 0 0-1 0v8.793L5.354 8.146a.5.5 0 1 0-.708.708l3 3z\"/></svg> Créer la sauvegarde');");
  result += F("});");
  result += F("}");
  result += F("function deleteBackup(filename){");
  result += F("if(confirm('Supprimer '+filename+' ?')){");
  result += F("$.post('/deleteBackupFile',{filename:filename},function(){location.reload();});");
  result += F("}");
  result += F("}");
  result += F("function viewBkFile(f){");
  result += F("var m=$('#bkModal');$('#bkModalTitle').text(f);$('#bkModalContent').text('Chargement...');");
  result += F("m.css('display','flex');");
  result += F("$.get('/readFile?0=bk&1='+f,function(d){");
  result += F("try{$('#bkModalContent').text(JSON.stringify(JSON.parse(d),null,2));}");
  result += F("catch(e){$('#bkModalContent').text(d);}");
  result += F("}).fail(function(){$('#bkModalContent').text('Erreur de lecture');});");
  result += F("}");
  result += F("function closeBkModal(e){");
  result += F("if(!e||e.target===$('#bkModal')[0])$('#bkModal').hide();");
  result += F("}");
  result += F("function restoreBkFile(f){");
  result += F("if(confirm('Restaurer '+f+' vers /db/ ?\\nLe fichier actuel sera remplace.')){");
  result += F("$.post('/restoreBkFile',{filename:f},function(){");
  result += F("alert('Restauration OK !');location.reload();");
  result += F("}).fail(function(x){alert('Erreur: '+x.responseText);});");
  result += F("}");
  result += F("}");
  result += F("</script>");

  result += footer();
  result += F("</html>");

  request->send(200, F("text/html"), result);
}


int totalLength;       //total size of firmware
int currentLength = 0; //current size of written firmware

void progressFunc(unsigned int progress,unsigned int total) {
  Serial.printf("%u of %u\r", progress, total);
};

void runUpdateFirmware(uint8_t *data, size_t len)
{
  Update.write(data, len);
  currentLength += len;
  // Print dots while waiting for update to finish
  Serial.print('.');
  // if current length of written firmware is not equal to total firmware size, repeat
  if(currentLength != totalLength) return;
  Update.end(true);
  Serial.printf("\nUpdate Success, Total Size: %u\nRebooting...\n", currentLength);
  // Restart ESP32 to see changes 
  ESP.restart();
}

bool checkUpdateFirmware()
{
  updateLog("====== UPDATE DIAG START ======");
  updateLog("Heap libre: %u | PSRAM libre: %u",
                ESP.getFreeHeap(), ESP.getFreePsram());
  updateLog("Heap min depuis boot: %u",
                esp_get_minimum_free_heap_size());
  updateLog("WiFi RSSI: %d dBm", WiFi.RSSI());
  updateLog("URL: %s", UPD_FILE);

  clientWeb.begin(UPD_FILE);
  clientWeb.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  clientWeb.setTimeout(15000);  // 15s timeout sur le GET initial
  int resp = clientWeb.GET();

  updateLog("HTTP response: %d", resp);

  int64_t contentLength = clientWeb.getSize();
  if (contentLength <= 0) {
    updateLog("ECHEC: taille invalide (contentLength=%lld, httpCode=%d)", contentLength, resp);
    clientWeb.end();
    return false;
  }

  updateLog("Fichier: %lld octets", contentLength);

  if(resp == HTTP_CODE_OK)
  {
    // Verifier l'espace LittleFS disponible
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    size_t freeBytes = totalBytes - usedBytes;
    updateLog("LittleFS: %u / %u utilises (%u libres)",
                  usedBytes, totalBytes, freeBytes);
    if ((int64_t)freeBytes < contentLength) {
      updateLog("ECHEC: espace insuffisant: %u libres < %lld necessaires", freeBytes, contentLength);
      updateStatus.statusAuto = "Espace disque insuffisant";
      clientWeb.end();
      return false;
    }

    File f = LittleFS.open("/bk/update.tar.gz", "w");
    if (!f) {
      updateLog("ECHEC: impossible d'ouvrir /bk/update.tar.gz");
      clientWeb.end();
      return false;
    }

    const size_t BUF_SZ = 400 * 1024;
    uint8_t *buff = (uint8_t*) heap_caps_malloc(BUF_SZ, MALLOC_CAP_SPIRAM);
    if (!buff) {
      updateLog("ECHEC: allocation PSRAM 400KB (PSRAM libre: %u)", ESP.getFreePsram());
      f.close();
      clientWeb.end();
      return false;
    }

    WiFiClient * stream = clientWeb.getStreamPtr();
    int64_t bytesRead = 0;
    unsigned long lastDataTime = millis();
    unsigned long downloadStart = millis();
    const unsigned long DOWNLOAD_TIMEOUT_MS = 30000;  // 30s sans donnees = abandon
    int lastLoggedPct = -1;

    updateLog("Debut telechargement | Heap: %u", ESP.getFreeHeap());

    while(bytesRead < contentLength) {
      esp_task_wdt_reset();
      if (tunnel) tunnel->loop();

      // Verifier que la connexion est toujours active
      if (!stream->connected() && !stream->available()) {
        updateLog("ECHEC: connexion perdue a %lld / %lld octets (%d%%)",
                      bytesRead, contentLength,
                      (int)((bytesRead * 100) / contentLength));
        updateLog("Heap: %u | RSSI: %d | duree: %lu ms",
                      ESP.getFreeHeap(), WiFi.RSSI(),
                      millis() - downloadStart);
        updateStatus.statusAuto = "Connexion perdue";
        f.close();
        heap_caps_free(buff);
        clientWeb.end();
        return false;
      }

      size_t size = stream->available();
      if (!size) {
        unsigned long waitTime = millis() - lastDataTime;
        if (waitTime > DOWNLOAD_TIMEOUT_MS) {
          updateLog("ECHEC: timeout a %lld / %lld octets (%d%%)",
                        bytesRead, contentLength,
                        (int)((bytesRead * 100) / contentLength));
          updateLog("Pas de donnees depuis %lu ms | Heap: %u | RSSI: %d | connected: %d",
                        waitTime, ESP.getFreeHeap(), WiFi.RSSI(), stream->connected());
          updateStatus.statusAuto = "Timeout téléchargement";
          f.close();
          heap_caps_free(buff);
          clientWeb.end();
          return false;
        }
        esp_task_wdt_reset();
        delay(1);
        continue;
      }

      lastDataTime = millis();
      size_t toRead = min<size_t>( min<size_t>(size, BUF_SZ),
                               contentLength - bytesRead );

      int c = stream->readBytes(buff, toRead);
      if (c <= 0){
        updateLog("ECHEC: readBytes=%d a %lld / %lld octets | Heap: %u | RSSI: %d",
                      c, bytesRead, contentLength, ESP.getFreeHeap(), WiFi.RSSI());
        updateStatus.statusAuto = "Erreur lecture";
        f.close();
        heap_caps_free(buff);
        clientWeb.end();
        return false;
      }

      size_t written = f.write(buff, c);
      if (written != (size_t)c) {
        updateLog("ECHEC: ecriture LittleFS %u / %d octets | libre: %u",
                      written, c, LittleFS.totalBytes() - LittleFS.usedBytes());
        updateStatus.statusAuto = "Erreur ecriture disque";
        f.close();
        heap_caps_free(buff);
        clientWeb.end();
        return false;
      }

      bytesRead += c;
      int pct = (bytesRead * 100) / contentLength;
      updateStatus.progressAuto = pct;

      // Log progression tous les 10%
      if (pct / 10 > lastLoggedPct / 10) {
        lastLoggedPct = pct;
        updateLog("%d%% (%lld / %lld) | Heap: %u | RSSI: %d | %lu ms",
                      pct, bytesRead, contentLength,
                      ESP.getFreeHeap(), WiFi.RSSI(),
                      millis() - downloadStart);
      }
    }
    heap_caps_free(buff);
    f.close();

    updateLog("Telechargement OK: %lld octets en %lu ms",
                  bytesRead, millis() - downloadStart);
    updateLog("Heap apres DL: %u | PSRAM: %u",
                  ESP.getFreeHeap(), ESP.getFreePsram());

  } else {
    updateLog("ECHEC: HTTP %d (attendu 200)", resp);
    return false;
  }
  clientWeb.end();
  updateLog("====== TELECHARGEMENT REUSSI ======");
  return true;
}


// ============================================================================
// GESTIONNAIRE DE FICHIERS LITTLEFS - desactive temporairement
// ============================================================================

/* GESTIONNAIRE DE FICHIERS - desactive temporairement
static void listFilesRecursive(AsyncResponseStream *response, const char* dirPath,
                                int &fileCount, size_t &totalSize) {
    File root = LittleFS.open(dirPath);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }
    File file = root.openNextFile();
    while (file) {
        vTaskDelay(1);
        if (file.isDirectory()) {
            String subdir = String(dirPath);
            if (subdir != "/") subdir += "/";
            subdir += file.name();
            file.close();
            listFilesRecursive(response, subdir.c_str(), fileCount, totalSize);
        } else {
            size_t sz = file.size();
            String fullPath = String(dirPath);
            if (fullPath != "/") fullPath += "/";
            fullPath += file.name();
            file.close();

            // Taille humaine
            char sizeBuf[16];
            if (sz >= 1048576) snprintf(sizeBuf, sizeof(sizeBuf), "%.1f MB", sz / 1048576.0);
            else if (sz >= 1024) snprintf(sizeBuf, sizeof(sizeBuf), "%.1f KB", sz / 1024.0);
            else snprintf(sizeBuf, sizeof(sizeBuf), "%u B", sz);

            response->printf(
                "<tr data-name=\"%s\" data-size=\"%u\">"
                "<td style=\"word-break:break-all\">%s</td>"
                "<td style=\"white-space:nowrap;text-align:right\">%s</td>"
                "<td style=\"text-align:center\">"
                "<button class=\"btn btn-sm btn-outline-danger\" onclick=\"del('%s')\">"
                "<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' fill='currentColor' viewBox='0 0 16 16'>"
                "<path d='M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z'/>"
                "<path d='M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H6a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1h3.5a1 1 0 0 1 1 1zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z'/>"
                "</svg>"
                "</button>"
                "</td>"
                "</tr>",
                fullPath.c_str(), sz, fullPath.c_str(), sizeBuf, fullPath.c_str());

            totalSize += sz;
            fileCount++;
        }
        file = root.openNextFile();
    }
    root.close();
}

void handleFilesManager(AsyncWebServerRequest *request) {
    if (!checkHeapForPage(request)) return;

    AsyncResponseStream *response = request->beginResponseStream("text/html");

    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    size_t freeBytes = totalBytes - usedBytes;
    int usedPct = (usedBytes * 100) / totalBytes;

    char totalStr[16], usedStr[16], freeStr[16];
    snprintf(totalStr, sizeof(totalStr), "%.1f MB", totalBytes / 1048576.0);
    snprintf(usedStr, sizeof(usedStr), "%.1f MB", usedBytes / 1048576.0);
    snprintf(freeStr, sizeof(freeStr), "%.1f MB", freeBytes / 1048576.0);

    // HTML header
    response->print(F("<html>"));
    response->print(FPSTR(HTTP_HEADER));
    response->print(FPSTR(HTTP_MENU));

    // Page content
    response->print(F("<div class='container-fluid' style='max-width:900px;margin-top:20px'>"
        "<div class='d-flex justify-content-between align-items-center mb-3'>"
        "<h4 style='margin:0'>Gestionnaire de fichiers</h4>"
        "<a href='/update' class='btn btn-outline-secondary btn-sm'>Retour</a>"
        "</div>"));

    // Disk usage bar
    response->printf(
        "<div class='card p-3 mb-3'>"
        "<div class='d-flex justify-content-between mb-1'>"
        "<span><b>Espace disque</b></span>"
        "<span>%s utilises / %s total (<b>%s libres</b>)</span>"
        "</div>"
        "<div class='progress' style='height:20px'>"
        "<div id='diskBar' class='progress-bar %s' role='progressbar' style='width:%d%%'>%d%%</div>"
        "</div>"
        "</div>",
        usedStr, totalStr, freeStr,
        usedPct > 80 ? "bg-danger" : usedPct > 60 ? "bg-warning" : "bg-success",
        usedPct, usedPct);

    // Search + file count
    response->print(F(
        "<div class='d-flex justify-content-between align-items-center mb-2'>"
        "<input id='search' class='form-control form-control-sm' style='max-width:300px' "
        "type='text' placeholder='Filtrer...' oninput='filterFiles()'>"
        "<span id='fileCount' class='text-muted'></span>"
        "</div>"));

    // Table header
    response->print(F(
        "<div style='max-height:60vh;overflow-y:auto'>"
        "<table class='table table-sm table-hover' id='fileTable'>"
        "<thead style='position:sticky;top:0;background:white'>"
        "<tr>"
        "<th style='cursor:pointer' onclick='sortTable(0)'>Fichier</th>"
        "<th style='cursor:pointer;width:100px;text-align:right' onclick='sortTable(1)'>Taille</th>"
        "<th style='width:50px'></th>"
        "</tr>"
        "</thead>"
        "<tbody id='fileBody'>"));

    // List all files recursively
    int fileCount = 0;
    size_t totalSize = 0;
    listFilesRecursive(response, "/", fileCount, totalSize);

    // Close table
    response->print(F("</tbody></table></div>"));

    // File count
    response->printf("<script>document.getElementById('fileCount').textContent='%d fichiers';</script>", fileCount);

    // JavaScript
    response->print(F(
        "<script>"
        "function filterFiles(){"
          "var s=document.getElementById('search').value.toLowerCase();"
          "var rows=document.querySelectorAll('#fileBody tr');"
          "var c=0;"
          "rows.forEach(function(r){"
            "var show=r.getAttribute('data-name').toLowerCase().indexOf(s)>-1;"
            "r.style.display=show?'':'none';"
            "if(show)c++;"
          "});"
          "document.getElementById('fileCount').textContent=c+' fichiers';"
        "}"
        "function del(path){"
          "if(!confirm('Supprimer '+path+' ?'))return;"
          "fetch('/deleteFile',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
          "body:'path='+encodeURIComponent(path)})"
          ".then(r=>r.json()).then(d=>{"
            "if(d.ok){"
              "var row=document.querySelector('tr[data-name=\"'+path+'\"]');"
              "if(row)row.remove();"
              "document.getElementById('diskBar').style.width=d.usedPct+'%';"
              "document.getElementById('diskBar').textContent=d.usedPct+'%';"
              "document.getElementById('diskBar').className='progress-bar '+(d.usedPct>80?'bg-danger':d.usedPct>60?'bg-warning':'bg-success');"
              "filterFiles();"
            "}else{alert('Erreur: '+d.error);}"
          "}).catch(e=>alert('Erreur: '+e));"
        "}"
        "function sortTable(col){"
          "var body=document.getElementById('fileBody');"
          "var rows=Array.from(body.querySelectorAll('tr'));"
          "var asc=body.getAttribute('data-sort-asc')==col?0:1;"
          "body.setAttribute('data-sort-asc',asc?col:-1);"
          "rows.sort(function(a,b){"
            "if(col==1){"
              "var va=parseInt(a.getAttribute('data-size'));"
              "var vb=parseInt(b.getAttribute('data-size'));"
              "return asc?va-vb:vb-va;"
            "}else{"
              "var va=a.getAttribute('data-name');"
              "var vb=b.getAttribute('data-name');"
              "return asc?va.localeCompare(vb):vb.localeCompare(va);"
            "}"
          "});"
          "rows.forEach(function(r){body.appendChild(r);});"
        "}"
        "sortTable(1);"  // Tri par taille decroissant par defaut
        "</script>"));

    response->print(footer());
    response->print(F("</html>"));

    request->send(response);
}

void handleDeleteFile(AsyncWebServerRequest *request) {
    if (!request->hasArg("path")) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing path\"}");
        return;
    }
    String path = request->arg("path");

    // Protection path traversal
    if (path.indexOf("..") >= 0) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid path\"}");
        return;
    }

    if (!LittleFS.exists(path)) {
        request->send(404, "application/json", "{\"ok\":false,\"error\":\"file not found\"}");
        return;
    }

    if (!LittleFS.remove(path)) {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"delete failed\"}");
        return;
    }

    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    int usedPct = (usedBytes * 100) / totalBytes;

    char buf[128];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"usedPct\":%d,\"free\":%u}", usedPct, totalBytes - usedBytes);
    request->send(200, "application/json", buf);
}
*/  // FIN GESTIONNAIRE DE FICHIERS

// ============================================================================

void handleToolUpdate(AsyncWebServerRequest *request)
{
    if (!checkHeapForPage(request)) return;
    String result;
    result += F("<html>");
    result += FPSTR(HTTP_HEADER);
    result += FPSTR(HTTP_MENU);
    result.replace("{{FormattedDate}}", FormattedDate);
    
    result += FPSTR(HTTP_UPDATE);
    result+=footer();
    result.replace("{{linkFirmware}}", UPD_FILE);
    result.replace("{{version}}", VERSION);
    result += F("</html>");

    request->send(200, F("text/html"), result);
}



void handleConfigFiles(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h4>Config files</h4>");
  result += F("<nav id='navbar-custom' class='navbar navbar-default navbar-fixed-left'>");
  result += F("      <div class='navbar-header'>");
  result += F("        <!--<a class='navbar-brand' href='#'>Brand</a>-->");
  result += F("      </div>");
  result += F("<ul class='nav navbar-nav'>");

  String str = "";
  File root = LittleFS.open("/config");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      // tmp = tmp.substring(10);
      result += F("<li><a href='#' onClick=\"readfile('");
      result += tmp;
      result += F("','config');document.getElementById('actions').style.display='block';\">");
      result += tmp;
      result += F(" ( ");
      result += file.size();
      result += F(" o)</a></li>");
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  result += F("</ul></nav>");
  result += F("<div class='container-fluid' >");
  result += F("  <div class='app-main-content'>");
  result += F("<form method='POST' action='saveFileConfig'>");
  result += F("<div class='form-group'>");
  result += F(" <label for='file'>File : <span id='title'></span></label>");
  result += F("<input type='hidden' name='filename' id='filename' value=''>");
  result += F(" <textarea class='form-control' id='file' name='file' rows='10'>");
  result += F("</textarea>");
  result += F("</div>");
  result += F("<div id='actions' style='display:none;'>");
  result += F("<button type='submit' value='save' name='action' class='btn btn-warning mb-2'>Save</button>");
   result += F("</div>");

  result += F("</Form>");
  result += F("</div>");
  result += F("</div>");
  result += F("</body>");
  result+=footer();
  result += F("</html>");
  file.close();
  root.close();
  request->send(200, F("text/html"), result);
}

void handleDebugFiles(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);

  // CSS pour l'interface
  result += F("<style>");
  result += F(".debug-container{display:flex;height:calc(100vh - 120px);gap:0;overflow:hidden;}");
  result += F(".sidebar{width:280px;min-width:280px;background:#fff;border-right:1px solid #dee2e6;display:flex;flex-direction:column;transition:margin-left 0.3s;}");
  result += F(".sidebar.collapsed{margin-left:-280px;}");
  result += F(".sidebar-header{padding:15px;border-bottom:1px solid #dee2e6;background:#f8f9fa;}");
  result += F(".sidebar-search{position:relative;}");
  result += F(".sidebar-search input{width:100%;padding:8px 12px 8px 35px;border:1px solid #ced4da;border-radius:6px;font-size:14px;}");
  result += F(".file-list{flex:1;overflow-y:auto;padding:10px;}");
  result += F(".file-item{display:flex;align-items:center;padding:10px 12px;margin-bottom:6px;background:#f8f9fa;border-radius:8px;cursor:pointer;transition:all 0.2s;border:2px solid transparent;}");
  result += F(".file-item:hover{background:#e9ecef;transform:translateX(3px);}");
  result += F(".file-item.active{background:#e7f1ff;border-color:#0d6efd;}");
  result += F(".file-item .name{font-weight:500;font-size:14px;flex:1;word-break:break-all;}");
  result += F(".file-item .size{font-size:12px;color:#6c757d;margin-left:8px;white-space:nowrap;}");
  result += F(".main-content{flex:1;display:flex;flex-direction:column;overflow:hidden;background:#fff;}");
  result += F(".toolbar{display:flex;align-items:center;gap:10px;padding:12px 15px;background:#f8f9fa;border-bottom:1px solid #dee2e6;flex-wrap:wrap;}");
  result += F(".toolbar-title{font-weight:600;font-size:16px;margin-right:auto;}");
  result += F(".toolbar .btn{padding:6px 12px;font-size:13px;}");
  result += F(".editor-container{flex:1;display:flex;overflow:hidden;}");
  result += F(".editor-pane{flex:1;display:flex;flex-direction:column;overflow:hidden;min-width:0;}");
  result += F(".pane-header{padding:8px 15px;background:#e9ecef;font-weight:500;font-size:13px;border-bottom:1px solid #dee2e6;}");
  result += F(".editor-wrapper{flex:1;position:relative;overflow:hidden;}");
  result += F("#file{width:100%;height:100%;border:none;resize:none;padding:15px;font-family:'Consolas','Monaco',monospace;font-size:13px;line-height:1.5;tab-size:2;background:#1e1e1e;color:#d4d4d4;}");
  result += F(".toggle-sidebar{display:none;position:fixed;bottom:20px;left:20px;z-index:1000;width:50px;height:50px;border-radius:50%;box-shadow:0 2px 10px rgba(0,0,0,0.2);}");
  result += F(".empty-state{display:flex;flex-direction:column;align-items:center;justify-content:center;height:100%;color:#6c757d;}");
  result += F("@media(max-width:768px){");
  result += F(".sidebar{position:fixed;left:0;top:60px;height:calc(100vh - 60px);z-index:999;box-shadow:2px 0 10px rgba(0,0,0,0.1);}");
  result += F(".toggle-sidebar{display:flex;align-items:center;justify-content:center;}");
  result += F(".debug-container{height:calc(100vh - 60px);}");
  result += F(".toolbar{flex-wrap:wrap;gap:6px;padding:10px;}");
  result += F(".toolbar-title{width:100%;font-size:14px;margin-bottom:5px;}");
  result += F(".toolbar .btn{padding:5px 8px;font-size:12px;}");
  result += F("}");
  result += F(".hidden{display:none!important;}");
  result += F(".loading-overlay{position:absolute;top:0;left:0;right:0;bottom:0;background:rgba(30,30,30,0.95);display:flex;flex-direction:column;align-items:center;justify-content:center;z-index:10;}");
  result += F(".loading-spinner{width:40px;height:40px;border:3px solid #444;border-top-color:#0d6efd;border-radius:50%;animation:spin 1s linear infinite;}");
  result += F(".loading-text{color:#aaa;margin-top:15px;font-size:14px;}");
  result += F("@keyframes spin{to{transform:rotate(360deg);}}");
  result += F(".file-count{font-size:12px;color:#6c757d;padding:10px 15px;border-top:1px solid #dee2e6;background:#f8f9fa;}");
  result += F("</style>");

  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);

  // Structure principale
  result += F("<div class='debug-container'>");

  // Sidebar avec liste des fichiers
  result += F("<div class='sidebar' id='sidebar'>");
  result += F("<div class='sidebar-header'>");
  result += F("<div class='d-flex justify-content-between align-items-center mb-2'>");
  result += F("<h5 class='mb-0'>Debug Files</h5>");
  result += F("</div>");
  result += F("<div class='sidebar-search'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' style='position:absolute;left:12px;top:50%;transform:translateY(-50%);color:#6c757d;' viewBox='0 0 16 16'><path d='M11.742 10.344a6.5 6.5 0 1 0-1.397 1.398h-.001c.03.04.062.078.098.115l3.85 3.85a1 1 0 0 0 1.415-1.414l-3.85-3.85a1.007 1.007 0 0 0-.115-.1zM12 6.5a5.5 5.5 0 1 1-11 0 5.5 5.5 0 0 1 11 0z'/></svg>");
  result += F("<input type='text' id='searchInput' placeholder='Rechercher...' onkeyup='filterFiles()'>");
  result += F("</div>");
  result += F("</div>");
  result += F("<div class='file-list' id='fileList'>");

  // Liste des fichiers
  int fileCount = 0;
  File root = LittleFS.open("/debug");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      fileCount++;
      String tmp = file.name();
      result += F("<div class='file-item' data-file='");
      result += tmp;
      result += F("' onclick=\"selectFile('");
      result += tmp;
      result += F("')\">");
      result += F("<span class='name'>");
      result += tmp;
      result += F("</span>");
      result += F("<span class='size'>");
      result += file.size();
      result += F(" o</span>");
      result += F("</div>");
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  file.close();
  root.close();

  result += F("</div>");
  result += F("<div class='file-count'><span id='fileCount'>");
  result += fileCount;
  result += F("</span> fichier(s)</div>");
  result += F("<div class='p-2 border-top'>");
  result += F("<button class='btn btn-danger btn-sm w-100' onclick='deleteAllFiles()'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' fill='currentColor' class='me-1' viewBox='0 0 16 16'><path d='M6.5 1h3a.5.5 0 0 1 .5.5v1H6v-1a.5.5 0 0 1 .5-.5ZM11 2.5v-1A1.5 1.5 0 0 0 9.5 0h-3A1.5 1.5 0 0 0 5 1.5v1H2.506a.58.58 0 0 0-.01 0H1.5a.5.5 0 0 0 0 1h.538l.853 10.66A2 2 0 0 0 4.885 16h6.23a2 2 0 0 0 1.994-1.84l.853-10.66h.538a.5.5 0 0 0 0-1h-.995a.59.59 0 0 0-.01 0H11Zm1.958 1-.846 10.58a1 1 0 0 1-.997.92h-6.23a1 1 0 0 1-.997-.92L3.042 3.5h9.916Zm-7.487 1a.5.5 0 0 1 .528.47l.5 8.5a.5.5 0 0 1-.998.06L5 5.03a.5.5 0 0 1 .47-.53Zm5.058 0a.5.5 0 0 1 .47.53l-.5 8.5a.5.5 0 1 1-.998-.06l.5-8.5a.5.5 0 0 1 .528-.47ZM8 4.5a.5.5 0 0 1 .5.5v8.5a.5.5 0 0 1-1 0V5a.5.5 0 0 1 .5-.5Z'/></svg>");
  result += F("Tout supprimer</button>");
  result += F("</div>");
  result += F("</div>");

  // Contenu principal
  result += F("<div class='main-content'>");

  // Toolbar
  result += F("<div class='toolbar'>");
  result += F("<span class='toolbar-title' id='currentFile'>Sélectionnez un fichier</span>");
  result += F("<input type='hidden' id='filename' value=''>");
  result += F("<div id='toolbarActions' class='hidden'>");
  result += F("<button class='btn btn-danger btn-sm' onclick='deleteFile()' id='btnDelete'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' fill='currentColor' class='me-1' viewBox='0 0 16 16'><path d='M6.5 1h3a.5.5 0 0 1 .5.5v1H6v-1a.5.5 0 0 1 .5-.5ZM11 2.5v-1A1.5 1.5 0 0 0 9.5 0h-3A1.5 1.5 0 0 0 5 1.5v1H2.506a.58.58 0 0 0-.01 0H1.5a.5.5 0 0 0 0 1h.538l.853 10.66A2 2 0 0 0 4.885 16h6.23a2 2 0 0 0 1.994-1.84l.853-10.66h.538a.5.5 0 0 0 0-1h-.995a.59.59 0 0 0-.01 0H11Zm1.958 1-.846 10.58a1 1 0 0 1-.997.92h-6.23a1 1 0 0 1-.997-.92L3.042 3.5h9.916Z'/></svg>");
  result += F("Supprimer</button>");
  result += F("</div>");
  result += F("</div>");

  // Zone de visualisation (lecture seule)
  result += F("<div class='editor-container'>");
  result += F("<div class='editor-pane' id='editorPane'>");
  result += F("<div class='pane-header'>Contenu du fichier (lecture seule)</div>");
  result += F("<div class='editor-wrapper'>");
  result += F("<textarea id='file' spellcheck='false' readonly placeholder='Sélectionnez un fichier dans la liste...'></textarea>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");

  // Bouton toggle sidebar mobile
  result += F("<button class='btn btn-primary toggle-sidebar' onclick='toggleSidebar()'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20' fill='currentColor' viewBox='0 0 16 16'><path fill-rule='evenodd' d='M2.5 12a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5zm0-4a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5zm0-4a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5z'/></svg>");
  result += F("</button>");

  // Scripts
  result += F("<script>");
  result += F("var currentFile='';");

  result += F("function toggleSidebar(){$('#sidebar').toggleClass('collapsed');}");

  result += F("function filterFiles(){");
  result += F("var search=$('#searchInput').val().toLowerCase();");
  result += F("var count=0;");
  result += F("$('.file-item').each(function(){");
  result += F("var name=$(this).data('file').toLowerCase();");
  result += F("var show=name.indexOf(search)>-1;");
  result += F("$(this).toggle(show);");
  result += F("if(show)count++;");
  result += F("});");
  result += F("$('#fileCount').text(count);");
  result += F("}");

  result += F("function selectFile(filename){");
  result += F("$('.file-item').removeClass('active');");
  result += F("$('.file-item[data-file=\"'+filename+'\"]').addClass('active');");
  result += F("currentFile=filename;");
  result += F("$('#filename').val(filename);");
  result += F("$('#currentFile').text(filename);");
  result += F("$('#toolbarActions').removeClass('hidden');");
  result += F("$('#file').val('');");
  result += F("$('.editor-wrapper').append('<div class=\"loading-overlay\" id=\"loader\"><div class=\"loading-spinner\"></div><div class=\"loading-text\">Chargement de '+filename+'...</div></div>');");
  result += F("$.get('readFile?0=debug&1='+filename,function(data){");
  result += F("$('#loader').remove();");
  result += F("$('#file').val(data);");
  result += F("}).fail(function(){$('#loader').remove();$('#file').val('Erreur de chargement');});");
  result += F("if(window.innerWidth<768)toggleSidebar();");
  result += F("}");

  result += F("function deleteFile(){");
  result += F("if(!confirm('Supprimer ce fichier ?'))return;");
  result += F("var filename=$('#filename').val();");
  result += F("$.post('saveDebug',{filename:filename,delete:'delete'},function(){location.reload();});");
  result += F("}");

  result += F("function deleteAllFiles(){");
  result += F("if(!confirm('Supprimer TOUS les fichiers debug ?'))return;");
  result += F("$.post('saveDebug',{deleteAll:'deleteAll'},function(){location.reload();});");
  result += F("}");

  result += F("</script>");
  result += F("</body>");
  result += footer();
  result += F("</html>");
  request->send(200, F("text/html"), result);
}


void handleFSbrowserBackup(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h4>backup list files</h4>");
  result += F("<nav id='navbar-custom' class='navbar navbar-default navbar-fixed-left'>");
  result += F("      <div class='navbar-header'>");
  result += F("        <!--<a class='navbar-brand' href='#'>Brand</a>-->");
  result += F("      </div>");
  result += F("<ul class='nav navbar-nav'>");

  String str = "";
  File root = LittleFS.open("/bk");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      // tmp = tmp.substring(10);
      result += F("<li><a href='#' onClick=\"readfile('");
      result += tmp;
      result += F("','bk');document.getElementById('actions').style.display='block';\">");
      result += tmp;
      result += F(" ( ");
      result += file.size();
      result += F(" o)</a></li>");
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  result += F("</ul></nav>");
  result += F("<div class='container-fluid' >");
  result += F("  <div class='app-main-content'>");
  result += F("<form method='POST' action='saveFileDatabase'>");
  result += F("<div class='form-group'>");
  result += F(" <label for='file'>File : <span id='title'></span></label>");
  result += F("<input type='hidden' name='filename' id='filename' value=''>");
  result += F(" <textarea class='form-control' id='file' name='file' rows='10'>");
  result += F("</textarea>");
  result += F("</div>");
  result += F("<div id='actions' style='display:none;'>");
  result += F("<button type='submit' class='btn btn-warning mb-2' name='save' value='save'>Enregistrer</button>&nbsp;");
  result += F("<button type='submit' class='btn btn-danger mb-2' name='delete' value='delete' onClick=\"if (confirm('Etes-vous sure ?')==true){return true;}else{return false;};\">Supprimer</button>");
  result += F("</div>");
  result += F("</Form>");
  result += F("</div>");
  result += F("</div>");
  result += F("</body>");
  result+=footer();
  result += F("</html>");
  file.close();
  root.close();
  request->send(200, F("text/html"), result);
}

void handleFSbrowser(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);

  // CSS pour l'interface
  result += F("<style>");
  result += F(".fs-container{display:flex;height:calc(100vh - 120px);gap:0;overflow:hidden;}");
  result += F(".sidebar{width:280px;min-width:280px;background:#fff;border-right:1px solid #dee2e6;display:flex;flex-direction:column;transition:margin-left 0.3s;}");
  result += F(".sidebar.collapsed{margin-left:-280px;}");
  result += F(".sidebar-header{padding:15px;border-bottom:1px solid #dee2e6;background:#f8f9fa;}");
  result += F(".sidebar-search{position:relative;}");
  result += F(".sidebar-search input{width:100%;padding:8px 12px 8px 35px;border:1px solid #ced4da;border-radius:6px;font-size:14px;}");
  result += F(".file-list{flex:1;overflow-y:auto;padding:10px;}");
  result += F(".file-item{display:flex;align-items:center;padding:10px 12px;margin-bottom:6px;background:#f8f9fa;border-radius:8px;cursor:pointer;transition:all 0.2s;border:2px solid transparent;}");
  result += F(".file-item:hover{background:#e9ecef;transform:translateX(3px);}");
  result += F(".file-item.active{background:#e7f1ff;border-color:#0d6efd;}");
  result += F(".file-item .name{font-weight:500;font-size:14px;flex:1;word-break:break-all;}");
  result += F(".file-item .size{font-size:12px;color:#6c757d;margin-left:8px;white-space:nowrap;}");
  result += F(".main-content{flex:1;display:flex;flex-direction:column;overflow:hidden;background:#fff;}");
  result += F(".toolbar{display:flex;align-items:center;gap:10px;padding:12px 15px;background:#f8f9fa;border-bottom:1px solid #dee2e6;flex-wrap:wrap;}");
  result += F(".toolbar-title{font-weight:600;font-size:16px;margin-right:auto;}");
  result += F(".toolbar .btn{padding:6px 12px;font-size:13px;}");
  result += F(".editor-container{flex:1;display:flex;overflow:hidden;}");
  result += F(".editor-pane{flex:1;display:flex;flex-direction:column;overflow:hidden;min-width:0;}");
  result += F(".editor-pane.split{flex:0 0 50%;}");
  result += F(".preview-pane{flex:0 0 50%;border-left:1px solid #dee2e6;display:none;flex-direction:column;overflow:hidden;}");
  result += F(".preview-pane.visible{display:flex;}");
  result += F(".pane-header{padding:8px 15px;background:#e9ecef;font-weight:500;font-size:13px;border-bottom:1px solid #dee2e6;}");
  result += F(".editor-wrapper{flex:1;position:relative;overflow:hidden;}");
  result += F("#file{width:100%;height:100%;border:none;resize:none;padding:15px;font-family:'Consolas','Monaco',monospace;font-size:13px;line-height:1.5;tab-size:2;background:#1e1e1e;color:#d4d4d4;}");
  result += F(".preview-content{flex:1;overflow-y:auto;padding:15px;}");
  result += F(".model-section{margin-bottom:20px;border:1px solid #dee2e6;border-radius:8px;overflow:hidden;}");
  result += F(".model-header{background:#f8f9fa;padding:10px 15px;font-weight:600;border-bottom:1px solid #dee2e6;display:flex;align-items:center;gap:10px;}");
  result += F(".model-body{padding:15px;}");
  result += F(".section-group{margin-bottom:15px;}");
  result += F(".section-title{font-weight:500;color:#495057;margin-bottom:8px;display:flex;align-items:center;gap:8px;}");
  result += F(".section-title .badge{font-size:11px;}");
  result += F(".item-card{background:#f8f9fa;border-radius:6px;padding:10px;margin-bottom:8px;font-size:12px;}");
  result += F(".item-card .label{color:#6c757d;font-size:11px;}");
  result += F(".item-card .value{font-weight:500;}");
  result += F(".validation-panel{padding:10px 15px;background:#f8f9fa;border-top:1px solid #dee2e6;}");
  result += F(".validation-success{color:#198754;}");
  result += F(".validation-error{color:#dc3545;}");
  result += F(".toggle-sidebar{display:none;position:fixed;bottom:20px;left:20px;z-index:1000;width:50px;height:50px;border-radius:50%;box-shadow:0 2px 10px rgba(0,0,0,0.2);}");
  result += F(".empty-state{display:flex;flex-direction:column;align-items:center;justify-content:center;height:100%;color:#6c757d;}");
  result += F("@media(max-width:768px){");
  result += F(".sidebar{position:fixed;left:0;top:60px;height:calc(100vh - 60px);z-index:999;box-shadow:2px 0 10px rgba(0,0,0,0.1);}");
  result += F(".toggle-sidebar{display:flex;align-items:center;justify-content:center;}");
  result += F(".fs-container{height:calc(100vh - 60px);}");
  result += F(".toolbar{flex-wrap:wrap;gap:6px;padding:10px;}");
  result += F(".toolbar-title{width:100%;font-size:14px;margin-bottom:5px;}");
  result += F(".toolbar .btn{padding:5px 8px;font-size:12px;}");
  result += F(".preview-pane{position:fixed;top:0;left:0;right:0;bottom:0;z-index:1001;border:none;flex:none;width:100%;height:100%;background:#fff;}");
  result += F(".preview-pane .pane-header{display:flex;justify-content:space-between;align-items:center;padding:12px 15px;background:#0d6efd;color:#fff;}");
  result += F(".preview-pane .close-preview{background:none;border:none;color:#fff;font-size:24px;cursor:pointer;padding:0 5px;}");
  result += F("}");
  result += F(".is-invalid{border-color:#dc3545!important;background-color:#2d1f1f!important;}");
  result += F(".hidden{display:none!important;}");
  result += F(".close-preview{display:none;}");
  result += F("@media(max-width:768px){.close-preview{display:block;}}");
  result += F(".loading-overlay{position:absolute;top:0;left:0;right:0;bottom:0;background:rgba(30,30,30,0.95);display:flex;flex-direction:column;align-items:center;justify-content:center;z-index:10;}");
  result += F(".loading-spinner{width:40px;height:40px;border:3px solid #444;border-top-color:#0d6efd;border-radius:50%;animation:spin 1s linear infinite;}");
  result += F(".loading-text{color:#aaa;margin-top:15px;font-size:14px;}");
  result += F("@keyframes spin{to{transform:rotate(360deg);}}");
  result += F("</style>");

  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);

  // Structure principale
  result += F("<div class='fs-container'>");

  // Sidebar avec liste des fichiers
  result += F("<div class='sidebar' id='sidebar'>");
  result += F("<div class='sidebar-header'>");
  result += F("<div class='d-flex justify-content-between align-items-center mb-2'>");
  result += F("<h5 class='mb-0'>Appareils</h5>");
  result += F("</div>");
  result += F("<div class='sidebar-search'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' style='position:absolute;left:12px;top:50%;transform:translateY(-50%);color:#6c757d;' viewBox='0 0 16 16'><path d='M11.742 10.344a6.5 6.5 0 1 0-1.397 1.398h-.001c.03.04.062.078.098.115l3.85 3.85a1 1 0 0 0 1.415-1.414l-3.85-3.85a1.007 1.007 0 0 0-.115-.1zM12 6.5a5.5 5.5 0 1 1-11 0 5.5 5.5 0 0 1 11 0z'/></svg>");
  result += F("<input type='text' id='searchInput' placeholder='Rechercher...' onkeyup='filterFiles()'>");
  result += F("</div>");
  result += F("</div>");
  result += F("<div class='file-list' id='fileList'>");

  // Liste des fichiers
  File root = LittleFS.open("/db");
  File file = root.openNextFile();
  while (file)
  {
    esp_task_wdt_reset();
    if (!file.isDirectory())
    {
      String tmp = file.name();
      result += F("<div class='file-item' data-file='");
      result += tmp;
      result += F("' onclick=\"selectFile('");
      result += tmp;
      result += F("')\">");
      result += F("<span class='name'>");
      result += tmp;
      result += F("</span>");
      result += F("<span class='size'>");
      result += file.size();
      result += F(" o</span>");
      result += F("</div>");
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  file.close();
  root.close();

  result += F("</div>");
  result += F("</div>");

  // Contenu principal
  result += F("<div class='main-content'>");

  // Toolbar
  result += F("<div class='toolbar'>");
  result += F("<span class='toolbar-title' id='currentFile'>Sélectionnez un fichier</span>");
  result += F("<input type='hidden' id='filename' value=''>");
  result += F("<div id='toolbarActions' class='hidden'>");
  result += F("<button class='btn btn-outline-secondary btn-sm' onclick='togglePreview()' id='btnPreview' title='Prévisualisation'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' fill='currentColor' viewBox='0 0 16 16'><path d='M16 8s-3-5.5-8-5.5S0 8 0 8s3 5.5 8 5.5S16 8 16 8zM1.173 8a13.133 13.133 0 0 1 1.66-2.043C4.12 4.668 5.88 3.5 8 3.5c2.12 0 3.879 1.168 5.168 2.457A13.133 13.133 0 0 1 14.828 8c-.058.087-.122.183-.195.288-.335.48-.83 1.12-1.465 1.755C11.879 11.332 10.119 12.5 8 12.5c-2.12 0-3.879-1.168-5.168-2.457A13.134 13.134 0 0 1 1.172 8z'/><path d='M8 5.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5zM4.5 8a3.5 3.5 0 1 1 7 0 3.5 3.5 0 0 1-7 0z'/></svg>");
  result += F("</button>");
  result += F("<button class='btn btn-outline-secondary btn-sm' onclick='formatJson()' title='Formater JSON'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' fill='currentColor' viewBox='0 0 16 16'><path d='M5.854 4.854a.5.5 0 1 0-.708-.708l-3.5 3.5a.5.5 0 0 0 0 .708l3.5 3.5a.5.5 0 0 0 .708-.708L2.707 8l3.147-3.146zm4.292 0a.5.5 0 0 1 .708-.708l3.5 3.5a.5.5 0 0 1 0 .708l-3.5 3.5a.5.5 0 0 1-.708-.708L13.293 8l-3.147-3.146z'/></svg>");
  result += F("</button>");
  result += F("<button class='btn btn-warning btn-sm' onclick='saveFile()' id='btnSave'>Enregistrer</button>");
  result += F("</div>");
  result += F("</div>");

  // Zone d'édition
  result += F("<div class='editor-container'>");
  result += F("<div class='editor-pane' id='editorPane'>");
  result += F("<div class='pane-header'>Éditeur JSON</div>");
  result += F("<div class='editor-wrapper'>");
  result += F("<textarea id='file' spellcheck='false' placeholder='Sélectionnez un fichier dans la liste...'></textarea>");
  result += F("</div>");
  result += F("<div class='validation-panel' id='validationPanel'>");
  result += F("<span id='validationStatus'></span>");
  result += F("</div>");
  result += F("</div>");

  // Panneau de prévisualisation
  result += F("<div class='preview-pane' id='previewPane'>");
  result += F("<div class='pane-header'><span>Prévisualisation</span><button class='close-preview' onclick='togglePreview()'>&times;</button></div>");
  result += F("<div class='preview-content' id='previewContent'>");
  result += F("<div class='empty-state'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' viewBox='0 0 16 16'><path d='M14 4.5V14a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V2a2 2 0 0 1 2-2h5.5L14 4.5zm-3 0A1.5 1.5 0 0 1 9.5 3V1H4a1 1 0 0 0-1 1v12a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1V4.5h-2z'/></svg>");
  result += F("<span>Sélectionnez un fichier</span>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");

  // Bouton toggle sidebar mobile
  result += F("<button class='btn btn-primary toggle-sidebar' onclick='toggleSidebar()'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20' fill='currentColor' viewBox='0 0 16 16'><path fill-rule='evenodd' d='M2.5 12a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5zm0-4a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5zm0-4a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5z'/></svg>");
  result += F("</button>");

  // Scripts
  result += F("<script>");
  result += F("var currentFile='';var previewVisible=false;var hasChanges=false;");

  result += F("function toggleSidebar(){$('#sidebar').toggleClass('collapsed');}");

  result += F("function filterFiles(){");
  result += F("var search=$('#searchInput').val().toLowerCase();");
  result += F("$('.file-item').each(function(){");
  result += F("var name=$(this).data('file').toLowerCase();");
  result += F("$(this).toggle(name.indexOf(search)>-1);");
  result += F("});");
  result += F("}");

  result += F("function selectFile(filename){");
  result += F("if(hasChanges&&!confirm('Modifications non sauvegardées. Continuer ?'))return;");
  result += F("$('.file-item').removeClass('active');");
  result += F("$('.file-item[data-file=\"'+filename+'\"]').addClass('active');");
  result += F("currentFile=filename;");
  result += F("$('#filename').val(filename);");
  result += F("$('#currentFile').text(filename);");
  result += F("$('#toolbarActions').removeClass('hidden');");
  result += F("$('#file').val('');");
  result += F("$('.editor-wrapper').append('<div class=\"loading-overlay\" id=\"loader\"><div class=\"loading-spinner\"></div><div class=\"loading-text\">Chargement de '+filename+'...</div></div>');");
  result += F("$('#validationStatus').html('');");
  result += F("$.get('readFile?0=db&1='+filename,function(data){");
  result += F("$('#loader').remove();");
  result += F("$('#file').val(data).removeClass('is-invalid');");
  result += F("validateJson();");
  result += F("updatePreview();");
  result += F("hasChanges=false;");
  result += F("}).fail(function(){$('#loader').remove();$('#validationStatus').html('<span class=\"validation-error\">Erreur de chargement</span>');});");
  result += F("if(window.innerWidth<768)toggleSidebar();");
  result += F("}");

  result += F("function validateJson(){");
  result += F("var content=$('#file').val();");
  result += F("if(!content.trim()){$('#validationStatus').html('');return null;}");
  result += F("try{");
  result += F("var json=JSON.parse(content);");
  result += F("$('#file').removeClass('is-invalid');");
  result += F("$('#validationStatus').html('<span class=\"validation-success\">✓ JSON valide</span>');");
  result += F("return json;");
  result += F("}catch(e){");
  result += F("$('#file').addClass('is-invalid');");
  result += F("var msg=e.message;");
  result += F("var line=msg.match(/position (\\d+)/);");
  result += F("if(line){var pos=parseInt(line[1]);var lines=content.substr(0,pos).split('\\n');msg+=' (ligne '+lines.length+')';}");
  result += F("$('#validationStatus').html('<span class=\"validation-error\">✗ '+msg+'</span>');");
  result += F("return null;");
  result += F("}");
  result += F("}");

  result += F("function updatePreview(){");
  result += F("var json=validateJson();");
  result += F("if(!json){$('#previewContent').html('<div class=\"empty-state\"><span class=\"text-danger\">JSON invalide</span></div>');return;}");
  result += F("var html='<div class=\"model-section\"><div class=\"model-header\">Device Info</div><div class=\"model-body\">';");
  result += F("for(var key in json){");
  result += F("if(typeof json[key]==='object'){");
  result += F("html+='<div class=\"section-group\"><div class=\"section-title\">'+key+'</div>';");
  result += F("for(var k2 in json[key]){html+='<div class=\"item-card\"><span class=\"label\">'+k2+':</span> <span class=\"value\">'+JSON.stringify(json[key][k2])+'</span></div>';}");
  result += F("html+='</div>';");
  result += F("}else{html+='<div class=\"item-card\"><span class=\"label\">'+key+':</span> <span class=\"value\">'+json[key]+'</span></div>';}");
  result += F("}");
  result += F("html+='</div></div>';");
  result += F("$('#previewContent').html(html);");
  result += F("}");

  result += F("function togglePreview(){");
  result += F("previewVisible=!previewVisible;");
  result += F("$('#previewPane').toggleClass('visible',previewVisible);");
  result += F("$('#editorPane').toggleClass('split',previewVisible);");
  result += F("$('#btnPreview').toggleClass('btn-primary btn-outline-secondary');");
  result += F("if(previewVisible)updatePreview();");
  result += F("}");

  result += F("function formatJson(){");
  result += F("try{");
  result += F("var json=JSON.parse($('#file').val());");
  result += F("$('#file').val(JSON.stringify(json,null,2));");
  result += F("validateJson();");
  result += F("hasChanges=true;");
  result += F("}catch(e){alert('JSON invalide: '+e.message);}");
  result += F("}");

  result += F("function saveFile(){");
  result += F("var filename=$('#filename').val();");
  result += F("var content=$('#file').val();");
  result += F("if(!validateJson()){alert('Corrigez les erreurs JSON avant de sauvegarder');return;}");
  result += F("$.post('saveFileDatabase',{filename:filename,file:content,save:'save'},function(){");
  result += F("hasChanges=false;alert('Fichier sauvegardé!');");
  result += F("}).fail(function(){alert('Erreur lors de la sauvegarde');});");
  result += F("}");

  result += F("function deleteFile(){");
  result += F("if(!confirm('Supprimer ce fichier ?'))return;");
  result += F("var filename=$('#filename').val();");
  result += F("$.post('saveFileDatabase',{filename:filename,file:'',delete:'delete'},function(){location.reload();});");
  result += F("}");

  result += F("$('#file').on('input',function(){hasChanges=true;validateJson();if(previewVisible)updatePreview();});");
  result += F("$(window).on('beforeunload',function(){if(hasChanges)return'Modifications non sauvegardées';});");

  result += F("</script>");
  result += F("</body>");
  result += footer();
  result += F("</html>");
  request->send(200, F("text/html"), result);
}

void handleCreateDevice(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CREATE_DEVICE);
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);
  request->send(200, "text/html", result);
}

void handleOTA(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_OTA);
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);
  request->send(200, "text/html", result);
}

void handleCreateHistory(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_HISTORY);
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);
  request->send(200, "text/html", result);
}

void handleHistory(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);

  // CSS pour l'interface
  result += F("<style>");
  result += F(".hst-container{display:flex;height:calc(100vh - 120px);gap:0;overflow:hidden;}");
  result += F(".sidebar{width:280px;min-width:280px;background:#fff;border-right:1px solid #dee2e6;display:flex;flex-direction:column;transition:margin-left 0.3s;}");
  result += F(".sidebar.collapsed{margin-left:-280px;}");
  result += F(".sidebar-header{padding:15px;border-bottom:1px solid #dee2e6;background:#f8f9fa;}");
  result += F(".sidebar-search{position:relative;}");
  result += F(".sidebar-search input{width:100%;padding:8px 12px 8px 35px;border:1px solid #ced4da;border-radius:6px;font-size:14px;}");
  result += F(".file-list{flex:1;overflow-y:auto;padding:10px;}");
  result += F(".file-item{display:flex;align-items:center;padding:10px 12px;margin-bottom:6px;background:#f8f9fa;border-radius:8px;cursor:pointer;transition:all 0.2s;border:2px solid transparent;}");
  result += F(".file-item:hover{background:#e9ecef;transform:translateX(3px);}");
  result += F(".file-item.active{background:#e7f1ff;border-color:#0d6efd;}");
  result += F(".file-item .name{font-weight:500;font-size:14px;flex:1;word-break:break-all;}");
  result += F(".file-item .size{font-size:12px;color:#6c757d;margin-left:8px;white-space:nowrap;}");
  result += F(".main-content{flex:1;display:flex;flex-direction:column;overflow:hidden;background:#fff;}");
  result += F(".toolbar{display:flex;align-items:center;gap:10px;padding:12px 15px;background:#f8f9fa;border-bottom:1px solid #dee2e6;flex-wrap:wrap;}");
  result += F(".toolbar-title{font-weight:600;font-size:16px;margin-right:auto;}");
  result += F(".toolbar .btn{padding:6px 12px;font-size:13px;}");
  result += F(".editor-container{flex:1;display:flex;overflow:hidden;}");
  result += F(".editor-pane{flex:1;display:flex;flex-direction:column;overflow:hidden;min-width:0;}");
  result += F(".editor-pane.split{flex:0 0 50%;}");
  result += F(".preview-pane{flex:0 0 50%;border-left:1px solid #dee2e6;display:none;flex-direction:column;overflow:hidden;}");
  result += F(".preview-pane.visible{display:flex;}");
  result += F(".pane-header{padding:8px 15px;background:#e9ecef;font-weight:500;font-size:13px;border-bottom:1px solid #dee2e6;}");
  result += F(".editor-wrapper{flex:1;position:relative;overflow:hidden;}");
  result += F("#file{width:100%;height:100%;border:none;resize:none;padding:15px;font-family:'Consolas','Monaco',monospace;font-size:13px;line-height:1.5;tab-size:2;background:#1e1e1e;color:#d4d4d4;}");
  result += F(".preview-content{flex:1;overflow-y:auto;padding:15px;}");
  result += F(".model-section{margin-bottom:20px;border:1px solid #dee2e6;border-radius:8px;overflow:hidden;}");
  result += F(".model-header{background:#f8f9fa;padding:10px 15px;font-weight:600;border-bottom:1px solid #dee2e6;display:flex;align-items:center;gap:10px;}");
  result += F(".model-body{padding:15px;}");
  result += F(".section-group{margin-bottom:15px;}");
  result += F(".section-title{font-weight:500;color:#495057;margin-bottom:8px;display:flex;align-items:center;gap:8px;}");
  result += F(".section-title .badge{font-size:11px;}");
  result += F(".item-card{background:#f8f9fa;border-radius:6px;padding:10px;margin-bottom:8px;font-size:12px;}");
  result += F(".item-card .label{color:#6c757d;font-size:11px;}");
  result += F(".item-card .value{font-weight:500;}");
  result += F(".validation-panel{padding:10px 15px;background:#f8f9fa;border-top:1px solid #dee2e6;}");
  result += F(".validation-success{color:#198754;}");
  result += F(".validation-error{color:#dc3545;}");
  result += F(".toggle-sidebar{display:none;position:fixed;bottom:20px;left:20px;z-index:1000;width:50px;height:50px;border-radius:50%;box-shadow:0 2px 10px rgba(0,0,0,0.2);}");
  result += F(".empty-state{display:flex;flex-direction:column;align-items:center;justify-content:center;height:100%;color:#6c757d;}");
  result += F("@media(max-width:768px){");
  result += F(".sidebar{position:fixed;left:0;top:60px;height:calc(100vh - 60px);z-index:999;box-shadow:2px 0 10px rgba(0,0,0,0.1);}");
  result += F(".toggle-sidebar{display:flex;align-items:center;justify-content:center;}");
  result += F(".hst-container{height:calc(100vh - 60px);}");
  result += F(".toolbar{flex-wrap:wrap;gap:6px;padding:10px;}");
  result += F(".toolbar-title{width:100%;font-size:14px;margin-bottom:5px;}");
  result += F(".toolbar .btn{padding:5px 8px;font-size:12px;}");
  result += F(".preview-pane{position:fixed;top:0;left:0;right:0;bottom:0;z-index:1001;border:none;flex:none;width:100%;height:100%;background:#fff;}");
  result += F(".preview-pane .pane-header{display:flex;justify-content:space-between;align-items:center;padding:12px 15px;background:#0d6efd;color:#fff;}");
  result += F(".preview-pane .close-preview{background:none;border:none;color:#fff;font-size:24px;cursor:pointer;padding:0 5px;}");
  result += F("}");
  result += F(".is-invalid{border-color:#dc3545!important;background-color:#2d1f1f!important;}");
  result += F(".hidden{display:none!important;}");
  result += F(".close-preview{display:none;}");
  result += F("@media(max-width:768px){.close-preview{display:block;}}");
  result += F(".loading-overlay{position:absolute;top:0;left:0;right:0;bottom:0;background:rgba(30,30,30,0.95);display:flex;flex-direction:column;align-items:center;justify-content:center;z-index:10;}");
  result += F(".loading-spinner{width:40px;height:40px;border:3px solid #444;border-top-color:#0d6efd;border-radius:50%;animation:spin 1s linear infinite;}");
  result += F(".loading-text{color:#aaa;margin-top:15px;font-size:14px;}");
  result += F("@keyframes spin{to{transform:rotate(360deg);}}");
  result += F("</style>");

  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);

  // Structure principale
  result += F("<div class='hst-container'>");

  // Sidebar avec liste des fichiers
  result += F("<div class='sidebar' id='sidebar'>");
  result += F("<div class='sidebar-header'>");
  result += F("<div class='d-flex justify-content-between align-items-center mb-2'>");
  result += F("<h5 class='mb-0'>Historique</h5>");
  //result += F("<a href='/createHistory' class='btn btn-primary btn-sm'>+ Nouveau</a>");
  result += F("</div>");
  result += F("<div class='sidebar-search'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' style='position:absolute;left:12px;top:50%;transform:translateY(-50%);color:#6c757d;' viewBox='0 0 16 16'><path d='M11.742 10.344a6.5 6.5 0 1 0-1.397 1.398h-.001c.03.04.062.078.098.115l3.85 3.85a1 1 0 0 0 1.415-1.414l-3.85-3.85a1.007 1.007 0 0 0-.115-.1zM12 6.5a5.5 5.5 0 1 1-11 0 5.5 5.5 0 0 1 11 0z'/></svg>");
  result += F("<input type='text' id='searchInput' placeholder='Rechercher...' onkeyup='filterFiles()'>");
  result += F("</div>");
  result += F("</div>");
  result += F("<div class='file-list' id='fileList'>");

  // Liste des fichiers
  File root = LittleFS.open("/hst");
  File file = root.openNextFile();
  while (file)
  {
    esp_task_wdt_reset();
    if (!file.isDirectory())
    {
      String tmp = file.name();
      result += F("<div class='file-item' data-file='");
      result += tmp;
      result += F("' onclick=\"selectFile('");
      result += tmp;
      result += F("')\">");
      result += F("<span class='name'>");
      result += tmp;
      result += F("</span>");
      result += F("<span class='size'>");
      result += file.size();
      result += F(" o</span>");
      result += F("</div>");
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  file.close();
  root.close();

  result += F("</div>");
  result += F("</div>");

  // Contenu principal
  result += F("<div class='main-content'>");

  // Toolbar
  result += F("<div class='toolbar'>");
  result += F("<span class='toolbar-title' id='currentFile'>Sélectionnez un fichier</span>");
  result += F("<input type='hidden' id='filename' value=''>");
  result += F("<div id='toolbarActions' class='hidden'>");
  result += F("<button class='btn btn-outline-secondary btn-sm' onclick='togglePreview()' id='btnPreview' title='Prévisualisation'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' fill='currentColor' viewBox='0 0 16 16'><path d='M16 8s-3-5.5-8-5.5S0 8 0 8s3 5.5 8 5.5S16 8 16 8zM1.173 8a13.133 13.133 0 0 1 1.66-2.043C4.12 4.668 5.88 3.5 8 3.5c2.12 0 3.879 1.168 5.168 2.457A13.133 13.133 0 0 1 14.828 8c-.058.087-.122.183-.195.288-.335.48-.83 1.12-1.465 1.755C11.879 11.332 10.119 12.5 8 12.5c-2.12 0-3.879-1.168-5.168-2.457A13.134 13.134 0 0 1 1.172 8z'/><path d='M8 5.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5zM4.5 8a3.5 3.5 0 1 1 7 0 3.5 3.5 0 0 1-7 0z'/></svg>");
  result += F("</button>");
  result += F("<button class='btn btn-outline-secondary btn-sm' onclick='formatJson()' title='Formater JSON'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' fill='currentColor' viewBox='0 0 16 16'><path d='M5.854 4.854a.5.5 0 1 0-.708-.708l-3.5 3.5a.5.5 0 0 0 0 .708l3.5 3.5a.5.5 0 0 0 .708-.708L2.707 8l3.147-3.146zm4.292 0a.5.5 0 0 1 .708-.708l3.5 3.5a.5.5 0 0 1 0 .708l-3.5 3.5a.5.5 0 0 1-.708-.708L13.293 8l-3.147-3.146z'/></svg>");
  result += F("</button>");
  result += F("<button class='btn btn-warning btn-sm' onclick='saveFile()' id='btnSave'>Enregistrer</button>");
  result += F("</div>");
  result += F("</div>");

  // Zone d'édition
  result += F("<div class='editor-container'>");
  result += F("<div class='editor-pane' id='editorPane'>");
  result += F("<div class='pane-header'>Éditeur JSON</div>");
  result += F("<div class='editor-wrapper'>");
  result += F("<textarea id='file' spellcheck='false' placeholder='Sélectionnez un fichier dans la liste...'></textarea>");
  result += F("</div>");
  result += F("<div class='validation-panel' id='validationPanel'>");
  result += F("<span id='validationStatus'></span>");
  result += F("</div>");
  result += F("</div>");

  // Panneau de prévisualisation
  result += F("<div class='preview-pane' id='previewPane'>");
  result += F("<div class='pane-header'><span>Prévisualisation</span><button class='close-preview' onclick='togglePreview()'>&times;</button></div>");
  result += F("<div class='preview-content' id='previewContent'>");
  result += F("<div class='empty-state'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' viewBox='0 0 16 16'><path d='M14 4.5V14a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V2a2 2 0 0 1 2-2h5.5L14 4.5zm-3 0A1.5 1.5 0 0 1 9.5 3V1H4a1 1 0 0 0-1 1v12a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1V4.5h-2z'/></svg>");
  result += F("<span>Sélectionnez un fichier</span>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");

  // Bouton toggle sidebar mobile
  result += F("<button class='btn btn-primary toggle-sidebar' onclick='toggleSidebar()'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20' fill='currentColor' viewBox='0 0 16 16'><path fill-rule='evenodd' d='M2.5 12a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5zm0-4a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5zm0-4a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5z'/></svg>");
  result += F("</button>");

  // Scripts
  result += F("<script>");
  result += F("var currentFile='';var previewVisible=false;var hasChanges=false;");

  result += F("function toggleSidebar(){$('#sidebar').toggleClass('collapsed');}");

  result += F("function filterFiles(){");
  result += F("var search=$('#searchInput').val().toLowerCase();");
  result += F("$('.file-item').each(function(){");
  result += F("var name=$(this).data('file').toLowerCase();");
  result += F("$(this).toggle(name.indexOf(search)>-1);");
  result += F("});");
  result += F("}");

  result += F("function selectFile(filename){");
  result += F("if(hasChanges&&!confirm('Modifications non sauvegardées. Continuer ?'))return;");
  result += F("$('.file-item').removeClass('active');");
  result += F("$('.file-item[data-file=\"'+filename+'\"]').addClass('active');");
  result += F("currentFile=filename;");
  result += F("$('#filename').val(filename);");
  result += F("$('#currentFile').text(filename);");
  result += F("$('#toolbarActions').removeClass('hidden');");
  result += F("$('#file').val('');");
  result += F("$('.editor-wrapper').append('<div class=\"loading-overlay\" id=\"loader\"><div class=\"loading-spinner\"></div><div class=\"loading-text\">Chargement de '+filename+'...</div></div>');");
  result += F("$('#validationStatus').html('');");
  result += F("$.get('readFile?0=hst&1='+filename,function(data){");
  result += F("$('#loader').remove();");
  result += F("$('#file').val(data).removeClass('is-invalid');");
  result += F("validateJson();");
  result += F("updatePreview();");
  result += F("hasChanges=false;");
  result += F("}).fail(function(){$('#loader').remove();$('#validationStatus').html('<span class=\"validation-error\">Erreur de chargement</span>');});");
  result += F("if(window.innerWidth<768)toggleSidebar();");
  result += F("}");

  result += F("function validateJson(){");
  result += F("var content=$('#file').val();");
  result += F("if(!content.trim()){$('#validationStatus').html('');return null;}");
  result += F("try{");
  result += F("var json=JSON.parse(content);");
  result += F("$('#file').removeClass('is-invalid');");
  result += F("var entries=Array.isArray(json)?json.length:Object.keys(json).length;");
  result += F("$('#validationStatus').html('<span class=\"validation-success\">✓ JSON valide - '+entries+' entrée(s)</span>');");
  result += F("return json;");
  result += F("}catch(e){");
  result += F("$('#file').addClass('is-invalid');");
  result += F("var msg=e.message;");
  result += F("var line=msg.match(/position (\\d+)/);");
  result += F("if(line){var pos=parseInt(line[1]);var lines=content.substr(0,pos).split('\\n');msg+=' (ligne '+lines.length+')';}");
  result += F("$('#validationStatus').html('<span class=\"validation-error\">✗ '+msg+'</span>');");
  result += F("return null;");
  result += F("}");
  result += F("}");

  result += F("function updatePreview(){");
  result += F("var json=validateJson();");
  result += F("if(!json){$('#previewContent').html('<div class=\"empty-state\"><span class=\"text-danger\">JSON invalide</span></div>');return;}");
  result += F("var html='<div class=\"model-section\"><div class=\"model-header\">History Data</div><div class=\"model-body\">';");
  result += F("if(Array.isArray(json)){");
  result += F("json.forEach(function(item,i){");
  result += F("html+='<div class=\"item-card\"><span class=\"label\">#'+(i+1)+':</span> <span class=\"value\">'+JSON.stringify(item)+'</span></div>';");
  result += F("});");
  result += F("}else{");
  result += F("for(var key in json){");
  result += F("if(typeof json[key]==='object'){");
  result += F("html+='<div class=\"section-group\"><div class=\"section-title\">'+key+'</div>';");
  result += F("for(var k2 in json[key]){html+='<div class=\"item-card\"><span class=\"label\">'+k2+':</span> <span class=\"value\">'+JSON.stringify(json[key][k2])+'</span></div>';}");
  result += F("html+='</div>';");
  result += F("}else{html+='<div class=\"item-card\"><span class=\"label\">'+key+':</span> <span class=\"value\">'+json[key]+'</span></div>';}");
  result += F("}");
  result += F("}");
  result += F("html+='</div></div>';");
  result += F("$('#previewContent').html(html);");
  result += F("}");

  result += F("function togglePreview(){");
  result += F("previewVisible=!previewVisible;");
  result += F("$('#previewPane').toggleClass('visible',previewVisible);");
  result += F("$('#editorPane').toggleClass('split',previewVisible);");
  result += F("$('#btnPreview').toggleClass('btn-primary btn-outline-secondary');");
  result += F("if(previewVisible)updatePreview();");
  result += F("}");

  result += F("function formatJson(){");
  result += F("try{");
  result += F("var json=JSON.parse($('#file').val());");
  result += F("$('#file').val(JSON.stringify(json,null,2));");
  result += F("validateJson();");
  result += F("hasChanges=true;");
  result += F("}catch(e){alert('JSON invalide: '+e.message);}");
  result += F("}");

  result += F("function saveFile(){");
  result += F("var filename=$('#filename').val();");
  result += F("var content=$('#file').val();");
  result += F("if(!validateJson()){alert('Corrigez les erreurs JSON avant de sauvegarder');return;}");
  result += F("$.post('saveFileHistory',{filename:filename,file:content,save:'save'},function(){");
  result += F("hasChanges=false;alert('Fichier sauvegardé!');");
  result += F("}).fail(function(){alert('Erreur lors de la sauvegarde');});");
  result += F("}");

  result += F("function deleteFile(){");
  result += F("if(!confirm('Supprimer ce fichier ?'))return;");
  result += F("var filename=$('#filename').val();");
  result += F("$.post('saveFileHistory',{filename:filename,file:'',delete:'delete'},function(){location.reload();});");
  result += F("}");

  result += F("$('#file').on('input',function(){hasChanges=true;validateJson();if(previewVisible)updatePreview();});");
  result += F("$(window).on('beforeunload',function(){if(hasChanges)return'Modifications non sauvegardées';});");

  result += F("</script>");
  result += F("</body>");
  result += footer();
  result += F("</html>");
  request->send(200, F("text/html"), result);
}

void handleCreateTemplate(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CREATE_TEMPLATE);
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);
  request->send(200, "text/html", result);
}


void handleTemplates(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);

  // CSS pour la nouvelle interface
  result += F("<style>");
  result += F(".template-container{display:flex;height:calc(100vh - 120px);gap:0;overflow:hidden;}");
  result += F(".sidebar{width:280px;min-width:280px;background:#fff;border-right:1px solid #dee2e6;display:flex;flex-direction:column;transition:margin-left 0.3s;}");
  result += F(".sidebar.collapsed{margin-left:-280px;}");
  result += F(".sidebar-header{padding:15px;border-bottom:1px solid #dee2e6;background:#f8f9fa;}");
  result += F(".sidebar-search{position:relative;}");
  result += F(".sidebar-search input{width:100%;padding:8px 12px 8px 35px;border:1px solid #ced4da;border-radius:6px;font-size:14px;}");
  result += F(".sidebar-search i{position:absolute;left:12px;top:50%;transform:translateY(-50%);color:#6c757d;}");
  result += F(".template-list{flex:1;overflow-y:auto;padding:10px;}");
  result += F(".template-item{display:flex;align-items:center;padding:10px 12px;margin-bottom:6px;background:#f8f9fa;border-radius:8px;cursor:pointer;transition:all 0.2s;border:2px solid transparent;}");
  result += F(".template-item:hover{background:#e9ecef;transform:translateX(3px);}");
  result += F(".template-item.active{background:#e7f1ff;border-color:#0d6efd;}");
  result += F(".template-item .name{font-weight:500;font-size:14px;flex:1;}");
  result += F(".template-item .size{font-size:12px;color:#6c757d;margin-left:8px;}");
  result += F(".template-item .badge{font-size:10px;padding:2px 6px;}");
  result += F(".main-content{flex:1;display:flex;flex-direction:column;overflow:hidden;background:#fff;}");
  result += F(".toolbar{display:flex;align-items:center;gap:10px;padding:12px 15px;background:#f8f9fa;border-bottom:1px solid #dee2e6;flex-wrap:wrap;}");
  result += F(".toolbar-title{font-weight:600;font-size:16px;margin-right:auto;}");
  result += F(".toolbar .btn{padding:6px 12px;font-size:13px;}");
  result += F(".editor-container{flex:1;display:flex;overflow:hidden;}");
  result += F(".editor-pane{flex:1;display:flex;flex-direction:column;overflow:hidden;min-width:0;}");
  result += F(".editor-pane.split{flex:0 0 50%;}");
  result += F(".preview-pane{flex:0 0 50%;border-left:1px solid #dee2e6;display:none;flex-direction:column;overflow:hidden;}");
  result += F(".preview-pane.visible{display:flex;}");
  result += F(".pane-header{padding:8px 15px;background:#e9ecef;font-weight:500;font-size:13px;border-bottom:1px solid #dee2e6;}");
  result += F(".editor-wrapper{flex:1;position:relative;overflow:hidden;}");
  result += F("#file{width:100%;height:100%;border:none;resize:none;padding:15px;font-family:'Consolas','Monaco',monospace;font-size:13px;line-height:1.5;tab-size:2;background:#1e1e1e;color:#d4d4d4;}");
  result += F(".preview-content{flex:1;overflow-y:auto;padding:15px;}");
  result += F(".json-tree{font-family:'Consolas',monospace;font-size:13px;}");
  result += F(".json-key{color:#0d6efd;font-weight:500;}");
  result += F(".json-string{color:#28a745;}");
  result += F(".json-number{color:#fd7e14;}");
  result += F(".json-boolean{color:#6f42c1;}");
  result += F(".json-null{color:#6c757d;}");
  result += F(".json-bracket{color:#666;}");
  result += F(".model-section{margin-bottom:20px;border:1px solid #dee2e6;border-radius:8px;overflow:hidden;}");
  result += F(".model-header{background:#f8f9fa;padding:10px 15px;font-weight:600;border-bottom:1px solid #dee2e6;display:flex;align-items:center;gap:10px;}");
  result += F(".model-body{padding:15px;}");
  result += F(".section-group{margin-bottom:15px;}");
  result += F(".section-title{font-weight:500;color:#495057;margin-bottom:8px;display:flex;align-items:center;gap:8px;}");
  result += F(".section-title .badge{font-size:11px;}");
  result += F(".item-card{background:#f8f9fa;border-radius:6px;padding:10px;margin-bottom:8px;font-size:12px;}");
  result += F(".item-card .label{color:#6c757d;font-size:11px;}");
  result += F(".item-card .value{font-weight:500;}");
  result += F(".validation-panel{padding:10px 15px;background:#f8f9fa;border-top:1px solid #dee2e6;}");
  result += F(".validation-success{color:#198754;}");
  result += F(".validation-error{color:#dc3545;}");
  result += F(".validation-error pre{background:#fff5f5;padding:10px;border-radius:4px;font-size:12px;margin-top:5px;white-space:pre-wrap;word-break:break-all;}");
  result += F(".toggle-sidebar{display:none;position:fixed;bottom:20px;left:20px;z-index:1000;width:50px;height:50px;border-radius:50%;box-shadow:0 2px 10px rgba(0,0,0,0.2);}");
  result += F(".empty-state{display:flex;flex-direction:column;align-items:center;justify-content:center;height:100%;color:#6c757d;}");
  result += F(".empty-state i{font-size:48px;margin-bottom:15px;opacity:0.5;}");
  // Mobile styles avec prévisualisation en overlay
  result += F("@media(max-width:768px){");
  result += F(".sidebar{position:fixed;left:0;top:60px;height:calc(100vh - 60px);z-index:999;box-shadow:2px 0 10px rgba(0,0,0,0.1);}");
  result += F(".toggle-sidebar{display:flex;align-items:center;justify-content:center;}");
  result += F(".template-container{height:calc(100vh - 60px);}");
  result += F(".toolbar{flex-wrap:wrap;gap:6px;padding:10px;}");
  result += F(".toolbar-title{width:100%;font-size:14px;margin-bottom:5px;}");
  result += F(".toolbar .btn{padding:5px 8px;font-size:12px;}");
  result += F(".editor-pane.split{flex:1;}");
  result += F(".preview-pane{position:fixed;top:0;left:0;right:0;bottom:0;z-index:1001;border:none;flex:none;width:100%;height:100%;background:#fff;}");
  result += F(".preview-pane .pane-header{display:flex;justify-content:space-between;align-items:center;padding:12px 15px;background:#0d6efd;color:#fff;}");
  result += F(".preview-pane .close-preview{background:none;border:none;color:#fff;font-size:24px;cursor:pointer;padding:0 5px;}");
  result += F(".preview-content{padding:15px;}");
  result += F(".model-section{margin-bottom:15px;}");
  result += F(".model-header{padding:8px 12px;font-size:14px;}");
  result += F(".model-body{padding:10px;}");
  result += F(".item-card{padding:8px;font-size:11px;}");
  result += F(".section-title{font-size:13px;}");
  result += F("}");
  result += F(".is-invalid{border-color:#dc3545!important;background-color:#2d1f1f!important;}");
  result += F(".btn-icon{padding:6px 10px;}");
  result += F(".hidden{display:none!important;}");
  result += F(".close-preview{display:none;}");
  result += F("@media(max-width:768px){.close-preview{display:block;}}");
  // Loader styles
  result += F(".loading-overlay{position:absolute;top:0;left:0;right:0;bottom:0;background:rgba(30,30,30,0.95);display:flex;flex-direction:column;align-items:center;justify-content:center;z-index:10;}");
  result += F(".loading-spinner{width:40px;height:40px;border:3px solid #444;border-top-color:#0d6efd;border-radius:50%;animation:spin 1s linear infinite;}");
  result += F(".loading-text{color:#aaa;margin-top:15px;font-size:14px;}");
  result += F("@keyframes spin{to{transform:rotate(360deg);}}");
  result += F("</style>");

  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);

  // Structure principale
  result += F("<div class='template-container'>");

  // Sidebar avec liste des templates
  result += F("<div class='sidebar' id='sidebar'>");
  result += F("<div class='sidebar-header'>");
  result += F("<div class='d-flex justify-content-between align-items-center mb-2'>");
  result += F("<h5 class='mb-0'>Templates</h5>");
  result += F("<button class='btn btn-primary btn-sm' onclick='createNewTemplate()'>+ Nouveau</button>");
  result += F("</div>");
  result += F("<div class='sidebar-search'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' style='position:absolute;left:12px;top:50%;transform:translateY(-50%);color:#6c757d;' viewBox='0 0 16 16'><path d='M11.742 10.344a6.5 6.5 0 1 0-1.397 1.398h-.001c.03.04.062.078.098.115l3.85 3.85a1 1 0 0 0 1.415-1.414l-3.85-3.85a1.007 1.007 0 0 0-.115-.1zM12 6.5a5.5 5.5 0 1 1-11 0 5.5 5.5 0 0 1 11 0z'/></svg>");
  result += F("<input type='text' id='searchInput' placeholder='Rechercher...' onkeyup='filterTemplates()'>");
  result += F("</div>");
  result += F("</div>");
  result += F("<div class='template-list' id='templateList'>");

  // Liste des fichiers templates
  File root = LittleFS.open("/tp");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      result += F("<div class='template-item' data-file='");
      result += tmp;
      result += F("' onclick=\"selectTemplate('");
      result += tmp;
      result += F("')\">");
      result += F("<span class='name'>");
      result += tmp;
      result += F("</span>");
      result += F("<span class='size'>");
      result += file.size();
      result += F(" o</span>");
      result += F("</div>");
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  file.close();
  root.close();

  result += F("</div>");
  result += F("<div class='p-2 border-top'>");
  result += F("<button class='btn btn-outline-secondary btn-sm w-100' onclick='importTemplate()'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' fill='currentColor' class='me-1' viewBox='0 0 16 16'><path d='M.5 9.9a.5.5 0 0 1 .5.5v2.5a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-2.5a.5.5 0 0 1 1 0v2.5a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2v-2.5a.5.5 0 0 1 .5-.5z'/><path d='M7.646 1.146a.5.5 0 0 1 .708 0l3 3a.5.5 0 0 1-.708.708L8.5 2.707V11.5a.5.5 0 0 1-1 0V2.707L5.354 4.854a.5.5 0 1 1-.708-.708l3-3z'/></svg>");
  result += F("Importer</button>");
  result += F("</div>");
  result += F("</div>");

  // Contenu principal
  result += F("<div class='main-content'>");

  // Toolbar
  result += F("<div class='toolbar'>");
  result += F("<span class='toolbar-title' id='currentFile'>Sélectionnez un template</span>");
  result += F("<input type='hidden' id='filename' value=''>");
  result += F("<div id='toolbarActions' class='hidden'>");
  result += F("<button class='btn btn-outline-secondary btn-sm' onclick='togglePreview()' id='btnPreview' title='Prévisualisation'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' fill='currentColor' viewBox='0 0 16 16'><path d='M16 8s-3-5.5-8-5.5S0 8 0 8s3 5.5 8 5.5S16 8 16 8zM1.173 8a13.133 13.133 0 0 1 1.66-2.043C4.12 4.668 5.88 3.5 8 3.5c2.12 0 3.879 1.168 5.168 2.457A13.133 13.133 0 0 1 14.828 8c-.058.087-.122.183-.195.288-.335.48-.83 1.12-1.465 1.755C11.879 11.332 10.119 12.5 8 12.5c-2.12 0-3.879-1.168-5.168-2.457A13.134 13.134 0 0 1 1.172 8z'/><path d='M8 5.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5zM4.5 8a3.5 3.5 0 1 1 7 0 3.5 3.5 0 0 1-7 0z'/></svg>");
  result += F("</button>");
  result += F("<button class='btn btn-outline-secondary btn-sm' onclick='formatJson()' title='Formater JSON'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' fill='currentColor' viewBox='0 0 16 16'><path d='M5.854 4.854a.5.5 0 1 0-.708-.708l-3.5 3.5a.5.5 0 0 0 0 .708l3.5 3.5a.5.5 0 0 0 .708-.708L2.707 8l3.147-3.146zm4.292 0a.5.5 0 0 1 .708-.708l3.5 3.5a.5.5 0 0 1 0 .708l-3.5 3.5a.5.5 0 0 1-.708-.708L13.293 8l-3.147-3.146z'/></svg>");
  result += F("</button>");
  result += F("<button class='btn btn-outline-secondary btn-sm' onclick='exportTemplate()' title='Exporter'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' fill='currentColor' viewBox='0 0 16 16'><path d='M.5 9.9a.5.5 0 0 1 .5.5v2.5a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-2.5a.5.5 0 0 1 1 0v2.5a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2v-2.5a.5.5 0 0 1 .5-.5z'/><path d='M7.646 11.854a.5.5 0 0 0 .708 0l3-3a.5.5 0 0 0-.708-.708L8.5 10.293V1.5a.5.5 0 0 0-1 0v8.793L5.354 8.146a.5.5 0 1 0-.708.708l3 3z'/></svg>");
  result += F("</button>");
  result += F("<button class='btn btn-warning btn-sm' onclick='saveTemplate()' id='btnSave'>Enregistrer</button>");
  result += F("<button class='btn btn-danger btn-sm' onclick='deleteTemplate()' id='btnDelete'>Supprimer</button>");
  result += F("</div>");
  result += F("</div>");

  // Zone d'édition
  result += F("<div class='editor-container'>");
  result += F("<div class='editor-pane' id='editorPane'>");
  result += F("<div class='pane-header'>Éditeur JSON</div>");
  result += F("<div class='editor-wrapper'>");
  result += F("<textarea id='file' spellcheck='false' placeholder='Sélectionnez un template dans la liste...'></textarea>");
  result += F("</div>");
  result += F("<div class='validation-panel' id='validationPanel'>");
  result += F("<span id='validationStatus'></span>");
  result += F("</div>");
  result += F("</div>");

  // Panneau de prévisualisation
  result += F("<div class='preview-pane' id='previewPane'>");
  result += F("<div class='pane-header'><span>Prévisualisation</span><button class='close-preview' onclick='togglePreview()'>&times;</button></div>");
  result += F("<div class='preview-content' id='previewContent'>");
  result += F("<div class='empty-state'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' viewBox='0 0 16 16'><path d='M14 4.5V14a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V2a2 2 0 0 1 2-2h5.5L14 4.5zm-3 0A1.5 1.5 0 0 1 9.5 3V1H4a1 1 0 0 0-1 1v12a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1V4.5h-2z'/></svg>");
  result += F("<span>Sélectionnez un template</span>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");

  // Bouton toggle sidebar mobile
  result += F("<button class='btn btn-primary toggle-sidebar' onclick='toggleSidebar()'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20' fill='currentColor' viewBox='0 0 16 16'><path fill-rule='evenodd' d='M2.5 12a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5zm0-4a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5zm0-4a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5z'/></svg>");
  result += F("</button>");

  // Input file caché pour import
  result += F("<input type='file' id='importFile' accept='.json' style='display:none' onchange='handleImport(event)'>");

  // Scripts
  result += F("<script>");

  // Variables globales
  result += F("var currentTemplate='';var previewVisible=false;var hasChanges=false;");

  // Toggle sidebar
  result += F("function toggleSidebar(){$('#sidebar').toggleClass('collapsed');}");

  // Filtrage des templates
  result += F("function filterTemplates(){");
  result += F("var search=$('#searchInput').val().toLowerCase();");
  result += F("$('.template-item').each(function(){");
  result += F("var name=$(this).data('file').toLowerCase();");
  result += F("$(this).toggle(name.indexOf(search)>-1);");
  result += F("});");
  result += F("}");

  // Sélection d'un template
  result += F("function selectTemplate(filename){");
  result += F("if(hasChanges&&!confirm('Modifications non sauvegardées. Continuer ?'))return;");
  result += F("$('.template-item').removeClass('active');");
  result += F("$('.template-item[data-file=\"'+filename+'\"]').addClass('active');");
  result += F("currentTemplate=filename;");
  result += F("$('#filename').val(filename);");
  result += F("$('#currentFile').text(filename);");
  result += F("$('#toolbarActions').removeClass('hidden');");
  // Afficher le loader
  result += F("$('#file').val('');");
  result += F("$('.editor-wrapper').append('<div class=\"loading-overlay\" id=\"loader\"><div class=\"loading-spinner\"></div><div class=\"loading-text\">Chargement de '+filename+'...</div></div>');");
  result += F("$('#validationStatus').html('');");
  result += F("$.get('readFile?0=tp&1='+filename,function(data){");
  result += F("$('#loader').remove();");
  result += F("$('#file').val(data).removeClass('is-invalid');");
  result += F("validateJson();");
  result += F("updatePreview();");
  result += F("hasChanges=false;");
  result += F("}).fail(function(){$('#loader').remove();$('#validationStatus').html('<span class=\"validation-error\">Erreur de chargement</span>');});");
  result += F("if(window.innerWidth<768)toggleSidebar();");
  result += F("}");

  // Validation JSON
  result += F("function validateJson(){");
  result += F("var content=$('#file').val();");
  result += F("if(!content.trim()){$('#validationStatus').html('');return null;}");
  result += F("try{");
  result += F("var json=JSON.parse(content);");
  result += F("$('#file').removeClass('is-invalid');");
  result += F("var models=Object.keys(json).length;");
  result += F("var statusCount=0,actionCount=0;");
  result += F("for(var k in json){if(json[k][0]){statusCount+=(json[k][0].status||[]).length;actionCount+=(json[k][0].action||[]).length;}}");
  result += F("$('#validationStatus').html('<span class=\"validation-success\">✓ JSON valide - '+models+' modèle(s), '+statusCount+' status, '+actionCount+' actions</span>');");
  result += F("return json;");
  result += F("}catch(e){");
  result += F("$('#file').addClass('is-invalid');");
  result += F("var msg=e.message;");
  result += F("var line=msg.match(/position (\\d+)/);");
  result += F("if(line){var pos=parseInt(line[1]);var lines=content.substr(0,pos).split('\\n');msg+=' (ligne '+lines.length+')';}");
  result += F("$('#validationStatus').html('<span class=\"validation-error\">✗ '+msg+'</span>');");
  result += F("return null;");
  result += F("}");
  result += F("}");

  // Mise à jour prévisualisation
  result += F("function updatePreview(){");
  result += F("var json=validateJson();");
  result += F("if(!json){$('#previewContent').html('<div class=\"empty-state\"><span class=\"text-danger\">JSON invalide</span></div>');return;}");
  result += F("var html='';");
  result += F("for(var model in json){");
  result += F("html+='<div class=\"model-section\">';");
  result += F("html+='<div class=\"model-header\"><svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" fill=\"currentColor\" viewBox=\"0 0 16 16\"><path d=\"M11 6a3 3 0 1 1-6 0 3 3 0 0 1 6 0z\"/><path fill-rule=\"evenodd\" d=\"M0 8a8 8 0 1 1 16 0A8 8 0 0 1 0 8zm8-7a7 7 0 0 0-5.468 11.37C3.242 11.226 4.805 10 8 10s4.757 1.225 5.468 2.37A7 7 0 0 0 8 1z\"/></svg>'+model+'</div>';");
  result += F("html+='<div class=\"model-body\">';");
  result += F("var data=json[model][0]||{};");
  // Status section
  result += F("if(data.status&&data.status.length){");
  result += F("html+='<div class=\"section-group\"><div class=\"section-title\"><span>Status</span><span class=\"badge bg-primary\">'+data.status.length+'</span></div>';");
  result += F("data.status.forEach(function(s){");
  result += F("html+='<div class=\"item-card\"><div class=\"value\">'+s.name+'</div><div class=\"label\">Cluster: '+s.cluster+' | Attr: '+s.attribut+(s.unit?' | '+s.unit:'')+'</div></div>';");
  result += F("});");
  result += F("html+='</div>';}");
  // Actions section
  result += F("if(data.action&&data.action.length){");
  result += F("html+='<div class=\"section-group\"><div class=\"section-title\"><span>Actions</span><span class=\"badge bg-success\">'+data.action.length+'</span></div>';");
  result += F("data.action.forEach(function(a){");
  result += F("html+='<div class=\"item-card\"><div class=\"value\">'+a.name+'</div><div class=\"label\">Cmd: '+a.command+(a.endpoint?' | EP: '+a.endpoint:'')+'</div></div>';");
  result += F("});");
  result += F("html+='</div>';}");
  // Report section
  result += F("if(data.report&&data.report.length){");
  result += F("html+='<div class=\"section-group\"><div class=\"section-title\"><span>Report</span><span class=\"badge bg-warning text-dark\">'+data.report.length+'</span></div>';");
  result += F("data.report.forEach(function(r){");
  result += F("html+='<div class=\"item-card\"><div class=\"label\">Cluster: '+r.cluster+' | Attr: '+r.attribut+' | Min: '+r.min+'s Max: '+r.max+'s</div></div>';");
  result += F("});");
  result += F("html+='</div>';}");
  // Bind section
  result += F("if(data.bind){");
  result += F("html+='<div class=\"section-group\"><div class=\"section-title\"><span>Bind</span></div>';");
  result += F("html+='<div class=\"item-card\"><div class=\"label\">'+data.bind+'</div></div></div>';}");
  result += F("html+='</div></div>';");
  result += F("}");
  result += F("$('#previewContent').html(html);");
  result += F("}");

  // Toggle prévisualisation
  result += F("function togglePreview(){");
  result += F("previewVisible=!previewVisible;");
  result += F("$('#previewPane').toggleClass('visible',previewVisible);");
  result += F("$('#editorPane').toggleClass('split',previewVisible);");
  result += F("$('#btnPreview').toggleClass('btn-primary btn-outline-secondary');");
  result += F("if(previewVisible)updatePreview();");
  result += F("}");

  // Formater JSON
  result += F("function formatJson(){");
  result += F("try{");
  result += F("var json=JSON.parse($('#file').val());");
  result += F("$('#file').val(JSON.stringify(json,null,2));");
  result += F("validateJson();");
  result += F("hasChanges=true;");
  result += F("}catch(e){alert('JSON invalide: '+e.message);}");
  result += F("}");

  // Tunnel detection
  result += F("function _isTunnel(){return window.location.hostname.indexOf('lixee-box.fr')>=0;}");

  // Chunked save for tunnel (sends text in ~8KB chunks)
  result += F("async function _saveChunked(filename,content){");
  result += F("var CHUNK=8192;");
  result += F("var r=await fetch('/templateSaveInit?file='+encodeURIComponent(filename),{method:'POST'});");
  result += F("if(!r.ok)throw new Error('init: '+r.status);");
  result += F("for(var i=0;i<content.length;i+=CHUNK){");
  result += F("var chunk=content.substring(i,i+CHUNK);");
  result += F("r=await fetch('/templateSaveChunk',{method:'POST',headers:{'Content-Type':'text/plain'},body:chunk});");
  result += F("if(!r.ok)throw new Error('chunk: '+r.status);");
  result += F("}");
  result += F("r=await fetch('/templateSaveFinish',{method:'POST'});");
  result += F("if(!r.ok){var e=await r.json().catch(function(){return{error:r.status}});throw new Error(e.error||r.status);}");
  result += F("return r.json();");
  result += F("}");

  // Generic save (auto-detects tunnel vs direct)
  result += F("function _doSave(filename,content,onSuccess,onError){");
  result += F("if(_isTunnel()){");
  result += F("_saveChunked(filename,content).then(onSuccess).catch(function(e){onError(e.message);});");
  result += F("}else{");
  result += F("$.ajax({url:'saveFileTemplates',type:'POST',data:{0:filename,1:content,2:'save'},dataType:'json',");
  result += F("success:onSuccess,");
  result += F("error:function(xhr){onError((xhr.responseJSON||{}).error||'Erreur inconnue');}");
  result += F("});");
  result += F("}");
  result += F("}");

  // Sauvegarde
  result += F("function saveTemplate(){");
  result += F("var filename=$('#filename').val();");
  result += F("var content=$('#file').val();");
  result += F("if(!validateJson()){alert('Corrigez les erreurs JSON avant de sauvegarder');return;}");
  result += F("_doSave(filename,content,function(d){hasChanges=false;alert('Template sauvegardé!');},function(e){alert('Erreur: '+e);});");
  result += F("}");

  // Suppression
  result += F("function deleteTemplate(){");
  result += F("if(!confirm('Supprimer ce template ?'))return;");
  result += F("var filename=$('#filename').val();");
  result += F("$.post('saveFileTemplates',{0:filename,1:'',2:'delete'},function(){location.reload();});");
  result += F("}");

  // Export
  result += F("function exportTemplate(){");
  result += F("var content=$('#file').val();");
  result += F("var filename=$('#filename').val()||'template.json';");
  result += F("var blob=new Blob([content],{type:'application/json'});");
  result += F("var a=document.createElement('a');");
  result += F("a.href=URL.createObjectURL(blob);");
  result += F("a.download=filename;");
  result += F("a.click();");
  result += F("}");

  // Import
  result += F("function importTemplate(){$('#importFile').click();}");
  result += F("function handleImport(e){");
  result += F("var file=e.target.files[0];");
  result += F("if(!file)return;");
  result += F("var reader=new FileReader();");
  result += F("reader.onload=function(ev){");
  result += F("try{");
  result += F("JSON.parse(ev.target.result);");
  result += F("var filename=prompt('Nom du fichier:',file.name);");
  result += F("if(!filename)return;");
  result += F("if(!filename.endsWith('.json'))filename+='.json';");
  result += F("_doSave(filename,ev.target.result,function(){location.reload();},function(e){alert('Erreur: '+e);});");
  result += F("}catch(err){alert('JSON invalide: '+err.message);}");
  result += F("};");
  result += F("reader.readAsText(file);");
  result += F("e.target.value='';");
  result += F("}");

  // Nouveau template
  result += F("function createNewTemplate(){");
  result += F("var name=prompt('Nom du nouveau template (ex: 123.json):');");
  result += F("if(!name)return;");
  result += F("if(!name.endsWith('.json'))name+='.json';");
  result += F("var defaultContent='{\\n  \"default\": [\\n    {\\n      \"status\": [],\\n      \"action\": [],\\n      \"bind\": \"\",\\n      \"report\": []\\n    }\\n  ]\\n}';");
  result += F("_doSave(name,defaultContent,function(){location.reload();},function(e){alert('Erreur: '+e);});");
  result += F("}");

  // Détecter les changements
  result += F("$('#file').on('input',function(){hasChanges=true;validateJson();if(previewVisible)updatePreview();});");

  // Avertir avant de quitter
  result += F("$(window).on('beforeunload',function(){if(hasChanges)return'Modifications non sauvegardées';});");

  result += F("</script>");
  result += F("</body>");
  result += footer();
  result += F("</html>");
  request->send(200, F("text/html"), result);
}


void handleRules(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);

  // CSS pour l'interface
  result += F("<style>");
  result += F(".rules-container{display:flex;height:calc(100vh - 120px);gap:0;overflow:hidden;}");
  result += F(".sidebar{width:280px;min-width:280px;background:#fff;border-right:1px solid #dee2e6;display:flex;flex-direction:column;transition:margin-left 0.3s;}");
  result += F(".sidebar.collapsed{margin-left:-280px;}");
  result += F(".sidebar-header{padding:15px;border-bottom:1px solid #dee2e6;background:#f8f9fa;}");
  result += F(".file-list{flex:1;overflow-y:auto;padding:10px;}");
  result += F(".file-item{display:flex;align-items:center;padding:10px 12px;margin-bottom:6px;background:#f8f9fa;border-radius:8px;cursor:pointer;transition:all 0.2s;border:2px solid transparent;}");
  result += F(".file-item:hover{background:#e9ecef;transform:translateX(3px);}");
  result += F(".file-item.active{background:#e7f1ff;border-color:#0d6efd;}");
  result += F(".file-item .name{font-weight:500;font-size:14px;flex:1;word-break:break-all;}");
  result += F(".file-item .size{font-size:12px;color:#6c757d;margin-left:8px;white-space:nowrap;}");
  result += F(".main-content{flex:1;display:flex;flex-direction:column;overflow:hidden;background:#fff;}");
  result += F(".toolbar{display:flex;align-items:center;gap:10px;padding:12px 15px;background:#f8f9fa;border-bottom:1px solid #dee2e6;flex-wrap:wrap;}");
  result += F(".toolbar-title{font-weight:600;font-size:16px;margin-right:auto;}");
  result += F(".toolbar .btn{padding:6px 12px;font-size:13px;}");
  result += F(".editor-container{flex:1;display:flex;overflow:hidden;}");
  result += F(".editor-pane{flex:1;display:flex;flex-direction:column;overflow:hidden;min-width:0;}");
  result += F(".editor-pane.split{flex:0 0 50%;}");
  result += F(".preview-pane{flex:0 0 50%;border-left:1px solid #dee2e6;display:none;flex-direction:column;overflow:hidden;}");
  result += F(".preview-pane.visible{display:flex;}");
  result += F(".pane-header{padding:8px 15px;background:#e9ecef;font-weight:500;font-size:13px;border-bottom:1px solid #dee2e6;}");
  result += F(".editor-wrapper{flex:1;position:relative;overflow:hidden;}");
  result += F("#file{width:100%;height:100%;border:none;resize:none;padding:15px;font-family:'Consolas','Monaco',monospace;font-size:13px;line-height:1.5;tab-size:2;background:#1e1e1e;color:#d4d4d4;}");
  result += F(".preview-content{flex:1;overflow-y:auto;padding:15px;}");
  result += F(".validation-panel{padding:10px 15px;background:#f8f9fa;border-top:1px solid #dee2e6;}");
  result += F(".validation-success{color:#198754;}");
  result += F(".validation-error{color:#dc3545;}");
  result += F(".toggle-sidebar{display:none;position:fixed;bottom:20px;left:20px;z-index:1000;width:50px;height:50px;border-radius:50%;box-shadow:0 2px 10px rgba(0,0,0,0.2);}");
  result += F(".empty-state{display:flex;flex-direction:column;align-items:center;justify-content:center;height:100%;color:#6c757d;}");
  result += F(".rule-card{background:#f8f9fa;border-radius:8px;padding:12px;margin-bottom:10px;border-left:4px solid #0d6efd;}");
  result += F(".rule-card .rule-name{font-weight:600;margin-bottom:5px;}");
  result += F(".rule-card .rule-detail{font-size:12px;color:#6c757d;}");
  result += F("@media(max-width:768px){");
  result += F(".sidebar{position:fixed;left:0;top:60px;height:calc(100vh - 60px);z-index:999;box-shadow:2px 0 10px rgba(0,0,0,0.1);}");
  result += F(".toggle-sidebar{display:flex;align-items:center;justify-content:center;}");
  result += F(".rules-container{height:calc(100vh - 60px);}");
  result += F(".toolbar{flex-wrap:wrap;gap:6px;padding:10px;}");
  result += F(".toolbar-title{width:100%;font-size:14px;margin-bottom:5px;}");
  result += F(".toolbar .btn{padding:5px 8px;font-size:12px;}");
  result += F(".preview-pane{position:fixed;top:0;left:0;right:0;bottom:0;z-index:1001;border:none;flex:none;width:100%;height:100%;background:#fff;}");
  result += F(".preview-pane .pane-header{display:flex;justify-content:space-between;align-items:center;padding:12px 15px;background:#0d6efd;color:#fff;}");
  result += F(".preview-pane .close-preview{background:none;border:none;color:#fff;font-size:24px;cursor:pointer;padding:0 5px;}");
  result += F("}");
  result += F(".is-invalid{border-color:#dc3545!important;background-color:#2d1f1f!important;}");
  result += F(".hidden{display:none!important;}");
  result += F(".close-preview{display:none;}");
  result += F("@media(max-width:768px){.close-preview{display:block;}}");
  result += F(".loading-overlay{position:absolute;top:0;left:0;right:0;bottom:0;background:rgba(30,30,30,0.95);display:flex;flex-direction:column;align-items:center;justify-content:center;z-index:10;}");
  result += F(".loading-spinner{width:40px;height:40px;border:3px solid #444;border-top-color:#0d6efd;border-radius:50%;animation:spin 1s linear infinite;}");
  result += F(".loading-text{color:#aaa;margin-top:15px;font-size:14px;}");
  result += F("@keyframes spin{to{transform:rotate(360deg);}}");
  result += F("</style>");

  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);

  // Structure principale
  result += F("<div class='rules-container'>");

  // Sidebar avec liste des fichiers
  result += F("<div class='sidebar' id='sidebar'>");
  result += F("<div class='sidebar-header'>");
  result += F("<h5 class='mb-0'>Règles</h5>");
  result += F("</div>");
  result += F("<div class='file-list' id='fileList'>");

  // Liste des fichiers (seulement rules.json)
  File root = LittleFS.open("/config");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      if (strcmp(file.name(), "rules.json") == 0)
      {
        result += F("<div class='file-item' data-file='");
        result += tmp;
        result += F("' onclick=\"selectFile('");
        result += tmp;
        result += F("')\">");
        result += F("<span class='name'>");
        result += tmp;
        result += F("</span>");
        result += F("<span class='size'>");
        result += file.size();
        result += F(" o</span>");
        result += F("</div>");
      }
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  file.close();
  root.close();

  result += F("</div>");
  result += F("</div>");

  // Contenu principal
  result += F("<div class='main-content'>");

  // Toolbar
  result += F("<div class='toolbar'>");
  result += F("<span class='toolbar-title' id='currentFile'>Sélectionnez un fichier</span>");
  result += F("<input type='hidden' id='filename' value=''>");
  result += F("<div id='toolbarActions' class='hidden'>");
  result += F("<button class='btn btn-outline-secondary btn-sm' onclick='togglePreview()' id='btnPreview' title='Prévisualisation'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' fill='currentColor' viewBox='0 0 16 16'><path d='M16 8s-3-5.5-8-5.5S0 8 0 8s3 5.5 8 5.5S16 8 16 8zM1.173 8a13.133 13.133 0 0 1 1.66-2.043C4.12 4.668 5.88 3.5 8 3.5c2.12 0 3.879 1.168 5.168 2.457A13.133 13.133 0 0 1 14.828 8c-.058.087-.122.183-.195.288-.335.48-.83 1.12-1.465 1.755C11.879 11.332 10.119 12.5 8 12.5c-2.12 0-3.879-1.168-5.168-2.457A13.134 13.134 0 0 1 1.172 8z'/><path d='M8 5.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5zM4.5 8a3.5 3.5 0 1 1 7 0 3.5 3.5 0 0 1-7 0z'/></svg>");
  result += F("</button>");
  result += F("<button class='btn btn-outline-secondary btn-sm' onclick='formatJson()' title='Formater JSON'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' fill='currentColor' viewBox='0 0 16 16'><path d='M5.854 4.854a.5.5 0 1 0-.708-.708l-3.5 3.5a.5.5 0 0 0 0 .708l3.5 3.5a.5.5 0 0 0 .708-.708L2.707 8l3.147-3.146zm4.292 0a.5.5 0 0 1 .708-.708l3.5 3.5a.5.5 0 0 1 0 .708l-3.5 3.5a.5.5 0 0 1-.708-.708L13.293 8l-3.147-3.146z'/></svg>");
  result += F("</button>");
  result += F("<button class='btn btn-warning btn-sm' onclick='saveFile()' id='btnSave'>Enregistrer</button>");
  result += F("</div>");
  result += F("</div>");

  // Zone d'édition
  result += F("<div class='editor-container'>");
  result += F("<div class='editor-pane' id='editorPane'>");
  result += F("<div class='pane-header'>Éditeur JSON</div>");
  result += F("<div class='editor-wrapper'>");
  result += F("<textarea id='file' spellcheck='false' placeholder='Sélectionnez un fichier dans la liste...'></textarea>");
  result += F("</div>");
  result += F("<div class='validation-panel' id='validationPanel'>");
  result += F("<span id='validationStatus'></span>");
  result += F("</div>");
  result += F("</div>");

  // Panneau de prévisualisation
  result += F("<div class='preview-pane' id='previewPane'>");
  result += F("<div class='pane-header'><span>Prévisualisation</span><button class='close-preview' onclick='togglePreview()'>&times;</button></div>");
  result += F("<div class='preview-content' id='previewContent'>");
  result += F("<div class='empty-state'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='48' height='48' fill='currentColor' viewBox='0 0 16 16'><path d='M14 4.5V14a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V2a2 2 0 0 1 2-2h5.5L14 4.5zm-3 0A1.5 1.5 0 0 1 9.5 3V1H4a1 1 0 0 0-1 1v12a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1V4.5h-2z'/></svg>");
  result += F("<span>Sélectionnez un fichier</span>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");
  result += F("</div>");

  // Bouton toggle sidebar mobile
  result += F("<button class='btn btn-primary toggle-sidebar' onclick='toggleSidebar()'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20' fill='currentColor' viewBox='0 0 16 16'><path fill-rule='evenodd' d='M2.5 12a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5zm0-4a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5zm0-4a.5.5 0 0 1 .5-.5h10a.5.5 0 0 1 0 1H3a.5.5 0 0 1-.5-.5z'/></svg>");
  result += F("</button>");

  // Scripts
  result += F("<script>");
  result += F("var currentFile='';var previewVisible=false;var hasChanges=false;");

  result += F("function toggleSidebar(){$('#sidebar').toggleClass('collapsed');}");

  result += F("function selectFile(filename){");
  result += F("if(hasChanges&&!confirm('Modifications non sauvegardées. Continuer ?'))return;");
  result += F("$('.file-item').removeClass('active');");
  result += F("$('.file-item[data-file=\"'+filename+'\"]').addClass('active');");
  result += F("currentFile=filename;");
  result += F("$('#filename').val(filename);");
  result += F("$('#currentFile').text(filename);");
  result += F("$('#toolbarActions').removeClass('hidden');");
  result += F("$('#file').val('');");
  result += F("$('.editor-wrapper').append('<div class=\"loading-overlay\" id=\"loader\"><div class=\"loading-spinner\"></div><div class=\"loading-text\">Chargement de '+filename+'...</div></div>');");
  result += F("$('#validationStatus').html('');");
  result += F("$.get('readFile?0=config&1='+filename,function(data){");
  result += F("$('#loader').remove();");
  result += F("$('#file').val(data).removeClass('is-invalid');");
  result += F("validateJson();");
  result += F("updatePreview();");
  result += F("hasChanges=false;");
  result += F("}).fail(function(){$('#loader').remove();$('#validationStatus').html('<span class=\"validation-error\">Erreur de chargement</span>');});");
  result += F("if(window.innerWidth<768)toggleSidebar();");
  result += F("}");

  result += F("function validateJson(){");
  result += F("var content=$('#file').val();");
  result += F("if(!content.trim()){$('#validationStatus').html('');return null;}");
  result += F("try{");
  result += F("var json=JSON.parse(content);");
  result += F("$('#file').removeClass('is-invalid');");
  result += F("var rulesCount=json.rules?json.rules.length:0;");
  result += F("$('#validationStatus').html('<span class=\"validation-success\">✓ JSON valide - '+rulesCount+' règle(s)</span>');");
  result += F("return json;");
  result += F("}catch(e){");
  result += F("$('#file').addClass('is-invalid');");
  result += F("var msg=e.message;");
  result += F("var line=msg.match(/position (\\d+)/);");
  result += F("if(line){var pos=parseInt(line[1]);var lines=content.substr(0,pos).split('\\n');msg+=' (ligne '+lines.length+')';}");
  result += F("$('#validationStatus').html('<span class=\"validation-error\">✗ '+msg+'</span>');");
  result += F("return null;");
  result += F("}");
  result += F("}");

  result += F("function updatePreview(){");
  result += F("var json=validateJson();");
  result += F("if(!json){$('#previewContent').html('<div class=\"empty-state\"><span class=\"text-danger\">JSON invalide</span></div>');return;}");
  result += F("var html='';");
  result += F("if(json.rules&&json.rules.length){");
  result += F("json.rules.forEach(function(rule,i){");
  result += F("html+='<div class=\"rule-card\">';");
  result += F("html+='<div class=\"rule-name\">'+(rule.name||'Règle '+(i+1))+'</div>';");
  result += F("var conds=rule.conditions?rule.conditions.length:0;");
  result += F("var acts=rule.actions?rule.actions.length:0;");
  result += F("html+='<div class=\"rule-detail\">'+conds+' condition(s), '+acts+' action(s)</div>';");
  result += F("if(rule.enabled===false)html+='<div class=\"rule-detail text-warning\">Désactivée</div>';");
  result += F("html+='</div>';");
  result += F("});");
  result += F("}else{html='<div class=\"empty-state\"><span>Aucune règle définie</span></div>';}");
  result += F("$('#previewContent').html(html);");
  result += F("}");

  result += F("function togglePreview(){");
  result += F("previewVisible=!previewVisible;");
  result += F("$('#previewPane').toggleClass('visible',previewVisible);");
  result += F("$('#editorPane').toggleClass('split',previewVisible);");
  result += F("$('#btnPreview').toggleClass('btn-primary btn-outline-secondary');");
  result += F("if(previewVisible)updatePreview();");
  result += F("}");

  result += F("function formatJson(){");
  result += F("try{");
  result += F("var json=JSON.parse($('#file').val());");
  result += F("$('#file').val(JSON.stringify(json,null,2));");
  result += F("validateJson();");
  result += F("hasChanges=true;");
  result += F("}catch(e){alert('JSON invalide: '+e.message);}");
  result += F("}");

  result += F("function saveFile(){");
  result += F("var filename=$('#filename').val();");
  result += F("var content=$('#file').val();");
  result += F("if(!validateJson()){alert('Corrigez les erreurs JSON avant de sauvegarder');return;}");
  result += F("$.post('saveFileRules',{filename:filename,file:content,save:'save'},function(){");
  result += F("hasChanges=false;alert('Règles sauvegardées!');");
  result += F("}).fail(function(){alert('Erreur lors de la sauvegarde');});");
  result += F("}");

  result += F("$('#file').on('input',function(){hasChanges=true;validateJson();if(previewVisible)updatePreview();});");
  result += F("$(window).on('beforeunload',function(){if(hasChanges)return'Modifications non sauvegardées';});");

  result += F("</script>");
  result += F("</body>");
  result += footer();
  result += F("</html>");

  request->send(200, F("text/html"), result);
}

void handleGenerateNotif(AsyncWebServerRequest *request)
{

  if (!notifList->isFull())
  {
    notifList->push(Notification{"TEST","Test de "+String(ConfigGeneral.ZLinky),FormattedDate,1,0});
    notificationManager.addNotification(
      "TEST",
      "Test de "+String(ConfigGeneral.ZLinky),
      0, "test"
    );
  }  
  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/tools"));
  request->send(response);
}

void handleSaveDevice(AsyncWebServerRequest *request)
{
  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
  }
  else
  {
    uint8_t i = 0;

    String arg0 = request->arg(i);
    if (arg0.indexOf("..") >= 0) {
      request->send(400, F("text/plain"), F("Invalid path"));
      return;
    }
    String filename = "/db/" + arg0 +".json";
    String content = request->arg(1);
    String action = request->arg(2);

    if (action == "save")
    {
      File file = LittleFS.open(filename.c_str(), "w+");
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Failed to open file for reading\r\n"));
        file.close();
        return;
      }
      
      int bytesWritten = file.print(content);

      if (bytesWritten > 0)
      {
        DEBUG_PRINTLN(F("File was written"));
        DEBUG_PRINTLN(bytesWritten);
      }
      else
      {
        DEBUG_PRINTLN(F("File write failed"));
      }

      file.close();
    }
    else if (action == "delete")
    {
      LittleFS.remove(filename);
    }
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/fsbrowser"));
    request->send(response);
  }
}


void handleSaveHistory(AsyncWebServerRequest *request)
{

  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
  }
  else
  {
    uint8_t i = 0;

    String filename = "/hst/" + request->arg(i);
    Serial.println(filename);

    String content = request->arg(1);
    String action = request->arg(2);

    if (action == "save")
    {
      File file = LittleFS.open(filename.c_str(), "w+");
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Failed to open file for reading\r\n"));
        file.close();
        return;
      }
      
      int bytesWritten = file.print(content);

      if (bytesWritten > 0)
      {
        DEBUG_PRINTLN(F("File was written"));
        DEBUG_PRINTLN(bytesWritten);
      }
      else
      {
        DEBUG_PRINTLN(F("File write failed"));
      }

      file.close();
    }
    else if (action == "delete")
    {
      LittleFS.remove(filename);
    }
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/hst"));
    request->send(response);
  }
}

void handleSaveTemplates(AsyncWebServerRequest *request)
{
  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
    return;
  }

  String filename = request->arg(0);  // Juste le nom (ex: "TS130F.json")
  String filepath = "/tp/" + filename;
  String content = request->arg(1);
  String action = request->arg(2);

  if (action == "save")
  {
    // Validation du JSON avant écriture
    SpiRamJsonDocument doc(content.length() + 256);
    DeserializationError error = deserializeJson(doc, content);
    
    if (error)
    {
      DEBUG_PRINT(F("JSON invalide: "));
      DEBUG_PRINTLN(error.c_str());
      
      String errorJson = "{\"success\":false,\"error\":\"";
      errorJson += error.c_str();
      errorJson += "\"}";
      request->send(400, F("application/json"), errorJson);
      return;
    }

    File file = LittleFS.open(filepath.c_str(), "w+");
    if (!file || file.isDirectory())
    {
      DEBUG_PRINT(F("Failed to open file for writing\r\n"));
      file.close();
      request->send(500, F("application/json"), F("{\"success\":false,\"error\":\"Erreur ouverture fichier\"}"));
      return;
    }
    
    int bytesWritten = file.print(content);
    file.close();

    if (bytesWritten <= 0)
    {
      DEBUG_PRINTLN(F("File write failed"));
      request->send(500, F("application/json"), F("{\"success\":false,\"error\":\"Erreur écriture fichier\"}"));
      return;
    }
    
    DEBUG_PRINT(F("Template saved: "));
    DEBUG_PRINTLN(filepath);

    // *** CRUCIAL: Recharger le template dans le cache AVANT les devices ***
    templateCache.reload(filename);
  }
  else if (action == "delete")
  {
    LittleFS.remove(filepath);
    // Réindexer tout le cache après suppression
    templateCache.indexTemplates();
  }

  // Extraire le device_id du nom de fichier (sans .json)
  String templateId = filename;
  if (templateId.endsWith(".json")) {
    templateId = templateId.substring(0, templateId.length() - 5);
  }

  // Recharger uniquement les devices qui utilisent ce template
  int reloadCount = 0;
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getInfo().device_id == templateId) {
      device->reloadTemplate();
      reloadCount++;
      log_e("Reloaded template for device: %s\n", device->getDeviceID().c_str());
    }
  }
  log_e("Templates reloaded for %d devices\n", reloadCount);

  request->send(200, F("application/json"), F("{\"success\":true}"));
}

// ===== Chunked template save via tunnel =====
// Large JSON templates exceed the 15KB WebSocket tunnel frame limit
// when form-encoded. Split into raw text chunks of ~8KB each.

static struct {
    bool active;
    String filename;
    size_t received;
    File file;
} _chunkedTemplate = {false, "", 0, File()};

static const char* _ctTmpPath = "/rt/template_tmp.json";

// POST /templateSaveInit?file=xxx.json
void handleTemplateSaveInitResponse(AsyncWebServerRequest *request) {
    if (!request->hasParam("file")) {
        request->send(400, "application/json", "{\"error\":\"missing file param\"}");
        return;
    }
    _chunkedTemplate.filename = request->getParam("file")->value();
    _chunkedTemplate.received = 0;

    if (LittleFS.exists(_ctTmpPath)) LittleFS.remove(_ctTmpPath);
    _chunkedTemplate.file = LittleFS.open(_ctTmpPath, "w");
    if (!_chunkedTemplate.file) {
        request->send(500, "application/json", "{\"error\":\"fs open\"}");
        return;
    }
    _chunkedTemplate.active = true;

    Serial.printf("[ChunkedTemplate] Init: %s\n", _chunkedTemplate.filename.c_str());
    request->send(200, "application/json", "{\"ok\":true}");
}

// POST /templateSaveChunk  body: raw text chunk
void handleTemplateSaveChunkResponse(AsyncWebServerRequest *request) {
    if (!_chunkedTemplate.active || !_chunkedTemplate.file) {
        if (request->_tempObject) { free(request->_tempObject); request->_tempObject = nullptr; }
        request->send(409, "application/json", "{\"error\":\"not initialized\"}");
        return;
    }
    if (!request->_tempObject) {
        request->send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }

    const char* text = (const char*)request->_tempObject;
    size_t len = strlen(text);
    _chunkedTemplate.file.write((const uint8_t*)text, len);
    _chunkedTemplate.received += len;

    free(request->_tempObject);
    request->_tempObject = nullptr;

    request->send(200, "application/json", "{\"ok\":true}");
}

// POST /templateSaveFinish?action=save|delete
void handleTemplateSaveFinishResponse(AsyncWebServerRequest *request) {
    if (!_chunkedTemplate.active) {
        request->send(409, "application/json", "{\"error\":\"not initialized\"}");
        return;
    }

    _chunkedTemplate.file.close();
    _chunkedTemplate.active = false;

    String filepath = "/tp/" + _chunkedTemplate.filename;

    Serial.printf("[ChunkedTemplate] Complete: %u bytes for %s\n",
                  _chunkedTemplate.received, filepath.c_str());

    // Read back and validate JSON
    File tmpFile = LittleFS.open(_ctTmpPath, "r");
    if (!tmpFile) {
        LittleFS.remove(_ctTmpPath);
        request->send(500, "application/json", "{\"error\":\"read failed\"}");
        return;
    }
    size_t fSize = tmpFile.size();
    SpiRamJsonDocument doc(fSize + 256);
    DeserializationError error = deserializeJson(doc, tmpFile);
    tmpFile.close();

    if (error) {
        LittleFS.remove(_ctTmpPath);
        String errJson = "{\"success\":false,\"error\":\"" + String(error.c_str()) + "\"}";
        request->send(400, "application/json", errJson);
        return;
    }

    // Move to final location
    if (LittleFS.exists(filepath)) LittleFS.remove(filepath);
    LittleFS.rename(_ctTmpPath, filepath.c_str());

    // Reload template cache
    templateCache.reload(_chunkedTemplate.filename);

    // Reload devices using this template
    String templateId = _chunkedTemplate.filename;
    if (templateId.endsWith(".json")) {
        templateId = templateId.substring(0, templateId.length() - 5);
    }
    for (size_t i = 0; i < devices.size(); i++) {
        DeviceData* device = devices[i];
        if (device->getInfo().device_id == templateId) {
            device->reloadTemplate();
        }
    }

    request->send(200, "application/json", "{\"success\":true}");
}

void handleSaveRules(AsyncWebServerRequest *request)
{
  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
  }
  else
  {
    uint8_t i = 0;

    String filename = "/config/" + request->arg(i);
    String content = request->arg(1);
    String action = request->arg(2);

    if (action == "save")
    {
      File file = LittleFS.open(filename.c_str(), "w+");
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Failed to open file for reading\r\n"));
        file.close();
        return;
      }
      
      int bytesWritten = file.print(content);

      if (bytesWritten > 0)
      {
        DEBUG_PRINTLN(F("File was written"));
        DEBUG_PRINTLN(bytesWritten);
      }
      else
      {
        DEBUG_PRINTLN(F("File write failed"));
      }

      file.close();
    }
    else if (action == "delete")
    {
      LittleFS.remove(filename);
    }
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/rules"));
    request->send(response);
  }
}


void handleJavascript(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("<h4>Javascript</h4>");
  result += F("<nav id='navbar-custom' class='navbar navbar-default navbar-fixed-left'>");
  result += F("      <div class='navbar-header'>");
  result += F("        <!--<a class='navbar-brand' href='#'>Brand</a>-->");
  result += F("      </div>");
  result += F("<ul class='nav navbar-nav'>");

  String str = "";
  File root = LittleFS.open("/web/js");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      DEBUG_PRINTLN(tmp);
      if (tmp == "functions.js")
      {
        // tmp = tmp.substring(11);
        result += F("<li><a href='#' onClick=\"readfile('");
        result += tmp;
        result += F("','web/js');document.getElementById('actions').style.display = 'block';\">");
        result += tmp;
        result += F(" ( ");
        result += file.size();
        result += F(" o)</a></li>");
      }
    }
    file.close();
    vTaskDelay(1);
    file = root.openNextFile();
  }
  result += F("</ul></nav>");
  result += F("<div class='container-fluid' >");
  result += F("  <div class='app-main-content'>");
  result += F("<form method='POST' action='saveFileJavascript'>");
  result += F("<div class='form-group'>");
  result += F(" <label for='file'>File : <span id='title'></span></label>");
  result += F("<input type='hidden' name='filename' id='filename' value=''>");
  result += F(" <textarea class='form-control' id='file' name='file' rows='10'>");
  result += F("</textarea>");
  result += F("</div>");
  result += F("<div id='actions' style='display:none;'>");
  result += F("<button type='submit' name='save' value='save' class='btn btn-warning mb-2'>Enregistrer</button>");
  result += F("</div>");
  result += F("</Form>");
  result += F("</div>");
  result += F("</div>");
  result += F("</body>");
  result+=footer();
  result += F("</html>");
  file.close();
  root.close();
  request->send(200, F("text/html"), result);
}

void handleSaveJavascript(AsyncWebServerRequest *request)
{
  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
  }
  else
  {
    uint8_t i = 0;

    String filename = "/web/js/" + request->arg(i);
    String content = request->arg(1);
    String action = request->arg(2);

    if (action == "save")
    {
      File file = LittleFS.open(filename, "w+");
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Failed to open file for reading\r\n"));
        file.close();
        return;
      }

      int bytesWritten = file.print(content);

      if (bytesWritten > 0)
      {
        DEBUG_PRINTLN(F("File was written"));
        DEBUG_PRINTLN(bytesWritten);
      }
      else
      {
        DEBUG_PRINTLN(F("File write failed"));
      }

      file.close();
    }
    else if (action == "delete")
    {
      LittleFS.remove(filename);
    }
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/javascript"));
    request->send(response);
  }
}


void handleSaveDebug(AsyncWebServerRequest *request)
{
  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
  }
  else
  {
    // Vérifier si c'est une suppression de tous les fichiers
    if (request->hasArg("deleteAll"))
    {
      File root = LittleFS.open("/debug");
      File file = root.openNextFile();
      while (file)
      {
          if (!file.isDirectory())
          {
            String tmp = file.name();
            file.close();
            LittleFS.remove("/debug/"+tmp);
          }
          else
          {
            file.close();
          }
          vTaskDelay(1);
          file = root.openNextFile();
      }
      root.close();
    }
    // Vérifier si c'est une suppression d'un fichier unique
    else if (request->hasArg("delete") && request->hasArg("filename"))
    {
      String filename = "/debug/" + request->arg("filename");
      LittleFS.remove(filename);
    }
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/debugFiles"));
    request->send(response);
  }
}

void handleSaveConfig(AsyncWebServerRequest *request)
{
  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
  }
  else
  {
    uint8_t i = 0;
    String filename = "/config/" + request->arg(i);
    String content = request->arg(1);
    String action = request->arg(2);
    if (action == "save")
    {
      File file = LittleFS.open(filename, "w+");
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Failed to open file for reading\r\n"));
        file.close();
        return;
      }

      int bytesWritten = file.print(content);

      if (bytesWritten > 0)
      {
        DEBUG_PRINTLN(F("File was written"));
        DEBUG_PRINTLN(bytesWritten);
      }
      else
      {
        DEBUG_PRINTLN(F("File write failed"));
      }

      file.close();
    }
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/configFiles"));
    request->send(response);
  }
}
void handleSaveDatabase(AsyncWebServerRequest *request)
{
  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
  }
  else
  {
    uint8_t i = 0;
    String filename = "/db/" + request->arg(i);
    String content = request->arg(1);
    String action = request->arg(2);

    if (action == "save")
    {
      File file = LittleFS.open(filename, "w+");
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Failed to open file for reading\r\n"));
        file.close();
        return;
      }

      int bytesWritten = file.print(content);

      if (bytesWritten > 0)
      {
        DEBUG_PRINT(F("File was written : "));
        DEBUG_PRINTLN(bytesWritten);
      }
      else
      {
        DEBUG_PRINT(F("File write failed : "));
        DEBUG_PRINTLN(filename);
        
      }

      file.close();
    }
    else if (action == "delete")
    {
      LittleFS.remove(filename);
    }
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/fsbrowser"));
    request->send(response);
  }
}

void handleReadfile(AsyncWebServerRequest *request)
{
  String result;
  int i = 0;
  String repertory = request->arg(i);
  String arg1 = request->arg(1);
  if (repertory.indexOf("..") >= 0 || arg1.indexOf("..") >= 0) {
    request->send(400, F("text/plain"), F("Invalid path"));
    return;
  }
  String filename = "/" + repertory + "/" + arg1;
  DEBUG_PRINTLN(filename);
  File file = LittleFS.open(filename, "r");

  if (!file || file.isDirectory())
  {
    file.close();
    return;
  }

  size_t fileSize = file.size();
  // Allocation d'un buffer dans la PSRAM avec heap_caps_malloc 
  char* buffer = (char*) heap_caps_malloc(fileSize + 1, MALLOC_CAP_SPIRAM); 
  if (!buffer) {
    Serial.println("Erreur d'allocation dans la PSRAM");
    file.close();
    request->send(500, F("text/plain"), F("Memory allocation error"));
    return;
  }

  size_t readBytes = file.readBytes(buffer, fileSize); 
  buffer[readBytes] = '\0';
  file.close();

  result = String(buffer,readBytes);
  
  heap_caps_free(buffer); 

 /* while (file.available())
  {
    result += (char)file.read();
  }
  file.close();*/
  request->send(200, F("text/html"), result);
}

void handleLogBuffer(AsyncWebServerRequest *request)
{
  String result;
  result = logPrint();
  request->send(200, F("text/html"), result);
}

void handleScanNetwork(AsyncWebServerRequest * request)
{
  String ret = request->arg("ret");
  String result=""; 
  if (ret == "-1")
  {
     WiFi.scanNetworks(true);
  }
 
  int n = WiFi.scanComplete();

  if (n>=0)
  { 
    if (ConfigGeneral.scanNumber == 0) 
    {
      result = " <label for='ssid'>SSID</label>";
      result += "<input class='form-control' id='ssid' type='text' name='WIFISSID' value='{{ssid}}'> <a onclick='scanNetwork();' class='btn btn-primary mb-2'>Scan</a><div id='networks'></div>";
    } else if (ConfigGeneral.scanNumber > 0) {
      
      result = "<select name='WIFISSID' onChange='updateSSID(this.value);'>";
      result += "<OPTION value=''>--Choix SSID--</OPTION>";
      for (int i = 0; i < ConfigGeneral.scanNumber; ++i) {
            result += "<OPTION value='";
            result +=WiFi.SSID(i);
            result +="'>";
            result +=WiFi.SSID(i)+" ("+WiFi.RSSI(i)+")";
            result+="</OPTION>";
        }
        result += "</select>";
    } 
    ConfigGeneral.scanNumber = -1;
    WiFi.scanDelete();
  } 

  request->send(200, F("text/html"), String(n)+"|"+result);
}

void handleSendReadAttribute(AsyncWebServerRequest *request)
{
  commandList->push(Packet{0x0010, 0x0000, 0});
  request->send(200, F("text/html"), "");
}

void handleClearConsole(AsyncWebServerRequest *request)
{
  logClear();

  request->send(200, F("text/html"), "");
}

void handleGetVersion(AsyncWebServerRequest *request)
{
  commandList->push(Packet{0x0010, 0x0000, 0});
  request->send(200, F("text/html"), "");
}

void handleErasePDM(AsyncWebServerRequest *request)
{
  commandList->push(Packet{0x0012, 0x0000, 0});
  request->send(200, F("text/html"), "");
}

void handleStartNwk(AsyncWebServerRequest *request)
{
  commandList->push(Packet{0x0024, 0x0000, 0});
  request->send(200, F("text/html"), "");
}

void handleSetLed(AsyncWebServerRequest *request)
{
  if (request->arg("param") != "")
  {
    uint8_t cmd;
    cmd = (uint8_t)request->arg("param").toInt() & 0xFF;
    commandList->push(Packet{0x0018, 0x0001, cmd});
  }
  request->send(200, F("text/html"), "");
}

void handleSetChannelMask(AsyncWebServerRequest *request)
{

  if (request->arg("param") != "")
  {
    uint8_t datas[4];
    int channel;

    Packet trame;
    channel = request->arg("param").toInt();
    datas[0] = (channel >> 24) & 0xFF;
    datas[1] = (channel >> 16) & 0xFF;
    datas[2] = (channel >> 8) & 0xFF;
    datas[3] = channel & 0xFF;
    trame.cmd = 0x0021;
    trame.len = 0x0004;
    memcpy(trame.datas, datas, 4);
    commandList->push(trame);
    DEBUG_PRINTLN(F("add networkState"));
    commandList->push(Packet{0x0011, 0x0000, 0});
  }

  request->send(200, F("text/html"), "");
}


void handlePermitJoinAssist(AsyncWebServerRequest *request)
{
  uint8_t datas[4];
  Packet trame;
  datas[0] = 0xFF;
  datas[1] = 0xFC;
  datas[2] = 0x1E;
  datas[3] = 0x00;

  trame.cmd = 0x0049;
  trame.len = 0x0004;
  memcpy(trame.datas, datas, 4);
  PrioritycommandList->push(trame);
  request->send(200, F("text/html"), "");
}


void handlePermitJoin(AsyncWebServerRequest *request)
{
  uint8_t datas[4];
  Packet trame;
  datas[0] = 0xFF;
  datas[1] = 0xFC;
  datas[2] = 0x1E;
  datas[3] = 0x00;

  trame.cmd = 0x0049;
  trame.len = 0x0004;
  memcpy(trame.datas, datas, 4);

  PrioritycommandList->push(trame);
  //commandList->push(trame);
  alertList->push(Alert{"Jumelage actif : 30 sec", 2});
  request->send(200, F("text/html"), "");
}


void handleNetwork(AsyncWebServerRequest *request)
{
  commandList->push(Packet{0x0025, 0x0000, 0});
  request->send(200, F("text/html"), "");
}

void handleReset(AsyncWebServerRequest *request)
{
  commandList->push(Packet{0x0011, 0x0000, 0});
  request->send(200, F("text/html"), "");
}

void handleRawMode(AsyncWebServerRequest *request)
{
  uint8_t datas[1];
  Packet trame;
  datas[0] = 0x01;

  trame.cmd = 0x0002;
  trame.len = 0x0001;
  memcpy(trame.datas, datas, 1);
  DEBUG_PRINTLN(F("RawMode"));
  commandList->push(trame);

  request->send(200, F("text/html"), "");
}

void handleRawModeOff(AsyncWebServerRequest *request)
{
  uint8_t datas[1];
  Packet trame;
  datas[0] = 0x00;

  trame.cmd = 0x0002;
  trame.len = 0x0001;
  memcpy(trame.datas, datas, 1);
  DEBUG_PRINTLN(F("RawMode off"));
  commandList->push(trame);

  request->send(200, F("text/html"), "");
}

void handleActiveReq(AsyncWebServerRequest *request)
{
  uint8_t shrtAddr[2];
  shrtAddr[0] = 0x1F;
  shrtAddr[1] = 0x7A;
  /*SendActiveRequest(shrtAddr);*/
  SendBasicDescription(shrtAddr, 1);

  request->send(200, F("text/html"), "");
}

void handleSaveConfigGeneral(AsyncWebServerRequest *request)
{
  String path = "configGeneral.json";

  String enableDebug;
  if (request->arg("debugSerial") == "on")
  {
    enableDebug = "1";
    ConfigSettings.enableDebug = true;
  }
  else
  {
    enableDebug = "0";
    ConfigSettings.enableDebug = false;
  }
  config_write(path, "enableDebug", enableDebug);

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configGeneral"));
  request->send(response);
}

void handleSaveConfigHorloge(AsyncWebServerRequest *request)
{
  String path = "configGeneral.json";
  if (request->arg("ntpserver") != "")
  {
    strlcpy(ConfigGeneral.ntpserver, request->arg("ntpserver").c_str(), sizeof(ConfigGeneral.ntpserver));
    config_write(path, "ntpserver", String(request->arg("ntpserver")));
  }

  if (request->arg("timezone") != "")
  {
    strlcpy(ConfigGeneral.timezone, request->arg("timezone").c_str(), sizeof(ConfigGeneral.timezone));
    config_write(path, "timezone", String(request->arg("timezone")));
  }

  if (request->arg("timeoffset").toInt() < 10)
  {
    ConfigGeneral.timeoffset = request->arg("timeoffset").toInt();
    config_write(path, "timeoffset", String(request->arg("timeoffset")));
  }

  if (request->arg("epochtime").toDouble()>=0)
  {
    config_write(path, "epoch", String(request->arg("epochtime")));
    ConfigGeneral.epochTime = atol(request->arg("epochtime").c_str());

  }

  executeReboot=true;
  
  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/"));
  request->send(response);
}

void handleSetAlias(AsyncWebServerRequest *request)
{
  String IEEE = request->arg("ieee");
  String alias = request->arg("alias");
  String result = "NOK";
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == IEEE)
    {
      device->setInfoAlias(alias);
      device->saveToFile(); 
      result = "OK";
      break;
    }
  }

  request->send(200, F("text/html"), result);

}

void handleSaveConfigLinky(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";

  if (request->arg("linkyDevice") != "")
  {
    strlcpy(ConfigGeneral.ZLinky, request->arg("linkyDevice").c_str(), sizeof(ConfigGeneral.ZLinky));
    config_write(path, "ZLinky", String(request->arg("linkyDevice")));
  }
  if (request->arg("shon").toInt() >= 0)
  {
    ConfigGeneral.HouseSurface = request->arg("shon").toInt();
    config_write(path, "shon", String(request->arg("shon")));
  }
  if (request->arg("tarifAbo").toFloat() >= 0)
  {
    // ConfigGeneral.tarifAbo = request->arg("tarifAbo");
    strlcpy(ConfigGeneral.tarifAbo, request->arg("tarifAbo").c_str(), sizeof(ConfigGeneral.tarifAbo));
    config_write(path, "tarifAbo", String(request->arg("tarifAbo")));
  }

  if (request->arg("tarifCSPE").toFloat() >= 0)
  {
    // ConfigGeneral.tarifCSPE = request->arg("tarifCSPE");
    strlcpy(ConfigGeneral.tarifCSPE, request->arg("tarifCSPE").c_str(), sizeof(ConfigGeneral.tarifCSPE));
    config_write(path, "tarifCSPE", String(request->arg("tarifCSPE")));
  }

  if (request->arg("tarifCTA").toFloat() >= 0)
  {
    // ConfigGeneral.tarifCTA = request->arg("tarifCTA");
    strlcpy(ConfigGeneral.tarifCTA, request->arg("tarifCTA").c_str(), sizeof(ConfigGeneral.tarifCTA));
    config_write(path, "tarifCTA", String(request->arg("tarifCTA")));
  }

  if (request->arg("tarifIdx1").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx1, request->arg("tarifIdx1").c_str(), sizeof(ConfigGeneral.tarifIdx1));
    config_write(path, "tarifIdx1", String(request->arg("tarifIdx1")));
  }

  if (request->arg("tarifIdx2").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx2, request->arg("tarifIdx2").c_str(), sizeof(ConfigGeneral.tarifIdx2));
    config_write(path, "tarifIdx2", String(request->arg("tarifIdx2")));
  }

  if (request->arg("tarifIdx3").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx3, request->arg("tarifIdx3").c_str(), sizeof(ConfigGeneral.tarifIdx3));
    config_write(path, "tarifIdx3", String(request->arg("tarifIdx3")));
  }

  if (request->arg("tarifIdx4").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx4, request->arg("tarifIdx4").c_str(), sizeof(ConfigGeneral.tarifIdx4));
    config_write(path, "tarifIdx4", String(request->arg("tarifIdx4")));
  }

  if (request->arg("tarifIdx5").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx5, request->arg("tarifIdx5").c_str(), sizeof(ConfigGeneral.tarifIdx5));
    config_write(path, "tarifIdx5", String(request->arg("tarifIdx5")));
  }

  if (request->arg("tarifIdx6").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx6, request->arg("tarifIdx6").c_str(), sizeof(ConfigGeneral.tarifIdx6));
    config_write(path, "tarifIdx6", String(request->arg("tarifIdx6")));
  }

  if (request->arg("tarifIdx7").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx7, request->arg("tarifIdx7").c_str(), sizeof(ConfigGeneral.tarifIdx7));
    config_write(path, "tarifIdx7", String(request->arg("tarifIdx7")));
  }

  if (request->arg("tarifIdx8").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx8, request->arg("tarifIdx8").c_str(), sizeof(ConfigGeneral.tarifIdx8));
    config_write(path, "tarifIdx8", String(request->arg("tarifIdx8")));
  }

  if (request->arg("tarifIdx9").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx9, request->arg("tarifIdx9").c_str(), sizeof(ConfigGeneral.tarifIdx9));
    config_write(path, "tarifIdx9", String(request->arg("tarifIdx9")));
  }

  if (request->arg("tarifIdx10").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdx10, request->arg("tarifIdx10").c_str(), sizeof(ConfigGeneral.tarifIdx10));
    config_write(path, "tarifIdx10", String(request->arg("tarifIdx10")));
  }

  //Enregistrement device délestage.
  String delestageIDs="";
  int j=0;
  int params = request->params();
  for(int i = 0; i < params; i++) {
      const AsyncWebParameter* p = request->getParam(i);
      const String prefixe="delestage_";
      if(p->name().startsWith(prefixe)) {
          // Traiter le paramètre
          if (j>0){delestageIDs +=",";}
          int indexDebut = p->name().indexOf(prefixe);
          if(indexDebut != -1) {
            delestageIDs += p->name().substring(indexDebut + prefixe.length());
          }
          j++;
      }
  }

  if (j>0)
  {
    config_write(path,"delestage", delestageIDs);
  }


  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configEnergy"));
  request->send(response);
}

void handleSaveConfigProduction(AsyncWebServerRequest *request)
{
  String path = "configGeneral.json";


  strlcpy(ConfigGeneral.Production, request->arg("prodDevice").c_str(), sizeof(ConfigGeneral.Production));
  config_write(path, "Production", String(request->arg("prodDevice")));
  
  if (request->arg("tarifAboProd").toFloat() >= 0)
  {
    // ConfigGeneral.tarifAbo = request->arg("tarifAbo");
    strlcpy(ConfigGeneral.tarifAboProd, request->arg("tarifAboProd").c_str(), sizeof(ConfigGeneral.tarifAboProd));
    config_write(path, "tarifAboProd", String(request->arg("tarifAboProd")));
  }

  if (request->arg("tarifIdxProd").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifIdxProd, request->arg("tarifIdxProd").c_str(), sizeof(ConfigGeneral.tarifIdxProd));
    config_write(path, "tarifIdxProd", String(request->arg("tarifIdxProd")));
  }

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configEnergy"));
  request->send(response);
}

void handleSaveConfigGaz(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";

  strlcpy(ConfigGeneral.Gaz, request->arg("gazDevice").c_str(), sizeof(ConfigGeneral.Gaz));
  config_write(path, "Gaz", String(request->arg("gazDevice")));

  if (request->arg("tarifGaz").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifGaz, request->arg("tarifGaz").c_str(), sizeof(ConfigGeneral.tarifGaz));
    config_write(path, "tarifGaz", String(request->arg("tarifGaz")));
  }

  if (request->arg("coeffGaz").toFloat() >= 0)
  {
    ConfigGeneral.coeffGaz= request->arg("coeffGaz").toFloat();
    config_write(path, "coeffGaz", String(request->arg("coeffGaz")));
  }

  if (request->arg("unitGaz") != "")
  {
    strlcpy(ConfigGeneral.unitGaz, request->arg("unitGaz").c_str(), sizeof(ConfigGeneral.unitGaz));
    config_write(path, "unitGaz", String(request->arg("unitGaz")));
  }

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configEnergy"));
  request->send(response);
}

void handleSaveConfigWater(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";

  strlcpy(ConfigGeneral.Water, request->arg("waterDevice").c_str(), sizeof(ConfigGeneral.Water));
  config_write(path, "Water", String(request->arg("waterDevice")));

  if (request->arg("tarifWater").toFloat() >= 0)
  {
    strlcpy(ConfigGeneral.tarifWater, request->arg("tarifWater").c_str(), sizeof(ConfigGeneral.tarifWater));
    config_write(path, "tarifWater", String(request->arg("tarifWater")));
  }

  if (request->arg("coeffWater").toFloat() >= 0)
  {
    ConfigGeneral.coeffWater= request->arg("coeffWater").toFloat();
    config_write(path, "coeffWater", String(request->arg("coeffWater")));
  }

  if (request->arg("unitWater") != "")
  {
    strlcpy(ConfigGeneral.unitWater, request->arg("unitWater").c_str(), sizeof(ConfigGeneral.unitWater));
    config_write(path, "unitWater", String(request->arg("unitWater")));
  }

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configEnergy"));
  request->send(response);
}

void handleSaveConfigPresence(AsyncWebServerRequest *request)
{
  String path = "configGeneral.json";

  // Sauvegarder le capteur de présence sélectionné
  strlcpy(ConfigGeneral.Presence, request->arg("presenceDevice").c_str(), sizeof(ConfigGeneral.Presence));
  config_write(path, "Presence", String(request->arg("presenceDevice")));

  // Sauvegarder l'option d'affichage sur le graphique
  if (request->arg("enablePresenceGraph") == "on")
  {
    ConfigGeneral.enablePresenceGraph = true;
    config_write(path, "enablePresenceGraph", "1");
  }
  else
  {
    ConfigGeneral.enablePresenceGraph = false;
    config_write(path, "enablePresenceGraph", "0");
  }

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configEnergy"));
  request->send(response);
}

void handleSaveConfigMQTT(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";
  String enableMqtt;
  if (request->arg("enableMqtt") == "on")
  {
    enableMqtt = "1";
    ConfigSettings.enableMqtt = true;
  }
  else
  {
    enableMqtt = "0";
    ConfigSettings.enableMqtt = false;
  }
  config_write(path, "enableMqtt", enableMqtt);
  if (request->arg("servMQTT"))
  {
    strlcpy(ConfigGeneral.servMQTT, request->arg("servMQTT").c_str(), sizeof(ConfigGeneral.servMQTT));
    config_write(path, "servMQTT", String(request->arg("servMQTT")));
  }

  if (request->arg("portMQTT"))
  {
    strlcpy(ConfigGeneral.portMQTT, request->arg("portMQTT").c_str(), sizeof(ConfigGeneral.portMQTT));
    config_write(path, "portMQTT", String(request->arg("portMQTT")));
  }

  if (request->arg("clientIDMQTT"))
  {
    strlcpy(ConfigGeneral.clientIDMQTT, request->arg("clientIDMQTT").c_str(), sizeof(ConfigGeneral.clientIDMQTT));
    config_write(path, "clientIDMQTT", String(request->arg("clientIDMQTT")));
  }

  if (request->arg("userMQTT"))
  {
    strlcpy(ConfigGeneral.userMQTT, request->arg("userMQTT").c_str(), sizeof(ConfigGeneral.userMQTT));
    config_write(path, "userMQTT", String(request->arg("userMQTT")));
  }

  if (request->arg("passMQTT"))
  {
    if (String(request->arg("passMQTT"))!="********")
    {
      strlcpy(ConfigGeneral.passMQTT, request->arg("passMQTT").c_str(), sizeof(ConfigGeneral.passMQTT));
      config_write(path, "passMQTT", String(request->arg("passMQTT")));
    }
  }

  if (request->arg("headerMQTT"))
  {
    strlcpy(ConfigGeneral.headerMQTT, request->arg("headerMQTT").c_str(), sizeof(ConfigGeneral.headerMQTT));
    config_write(path, "headerMQTT", String(request->arg("headerMQTT")));
  }

  if (String(request->arg("appliMQTT")) == "HA")
  {
    ConfigGeneral.HAMQTT = true;
    ConfigGeneral.TBMQTT = false;
    ConfigGeneral.customMQTT = false;
    config_write(path, "HAMQTT", "1");
    config_write(path, "TBMQTT", "0");
    config_write(path, "customMQTT", "0");
  }

  if (String(request->arg("appliMQTT")) == "TB")
  {
    ConfigGeneral.HAMQTT = false;
    ConfigGeneral.TBMQTT = true;
    ConfigGeneral.customMQTT = false;
    config_write(path, "HAMQTT", "0");
    config_write(path, "TBMQTT", "1");
    config_write(path, "customMQTT", "0");
  }
  if (String(request->arg("appliMQTT")) == "custom")
  {
    ConfigGeneral.HAMQTT = false;
    ConfigGeneral.TBMQTT = false;
    ConfigGeneral.customMQTT = true;
    config_write(path, "HAMQTT", "0");
    config_write(path, "TBMQTT", "0");
    config_write(path, "customMQTT", "1");
    config_write(path,"customMQTTJson",String(request->arg("customMQTTJson")));
  }
  
  //MQTT connection process
  if (ConfigSettings.enableMqtt)
  {
    mqttClient.disconnect();
    mqttClient.setServer(ConfigGeneral.servMQTT, atoi(ConfigGeneral.portMQTT));
    mqttClient.setClientId(ConfigGeneral.clientIDMQTT);
    if (String(ConfigGeneral.userMQTT) !="")
    {
      mqttClient.setCredentials(ConfigGeneral.userMQTT, ConfigGeneral.passMQTT);
    }
    mqttClient.connect();
    
  }else{
    mqttClient.disconnect();
  }
  
  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configMQTT"));
  request->send(response);
}

void handleSaveConfigHTTP(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";
  String enableHTTP;
  if (request->arg("enableSecureHttp") == "on")
  {
    enableHTTP = "1";
    ConfigSettings.enableSecureHttp = true;
  }
  else
  {
    enableHTTP = "0";
    ConfigSettings.enableSecureHttp = false;

    // Couper le tunnel si la sécurité HTTP est désactivée
    if (ConfigGeneral.enableTunnel) {
      ConfigGeneral.enableTunnel = false;
      config_write(path, "enableTunnel", "0");
      if (tunnel != nullptr) {
        tunnel->stop();
        delete tunnel;
        tunnel = nullptr;
        Serial.println("[Tunnel] Arrêté (sécurité HTTP désactivée)");
        addDebugLog("Tunnel arrêté : sécurité HTTP désactivée");
      }
    }
  }
  config_write(path, "enableSecureHttp", enableHTTP);

  String user = request->arg("userHTTP");
  String pass = request->arg("passHTTP");
  bool saveOk=true;
  uint8_t error=0;
  
  if (request->arg("enableSecureHttp") == "on")
  {

    if (strlen(pass.c_str())<4)
    {
      saveOk=saveOk & false;  
      error=error+1;
    }  

    if (user == "")
    {
      saveOk=saveOk & false;  
      error=error+2;
    }
  }

  if (saveOk)
  {
     if (request->arg("userHTTP"))
    {
      strlcpy(ConfigGeneral.userHTTP, request->arg("userHTTP").c_str(), sizeof(ConfigGeneral.userHTTP));
      config_write(path, "userHTTP", String(request->arg("userHTTP")));
    }

    if (pass=="********")
    {
      pass = ConfigGeneral.passHTTP;
      strlcpy(ConfigGeneral.passHTTP, pass.c_str(), sizeof(ConfigGeneral.passHTTP));
      config_write(path, "passHTTP", pass);
    }else{
      strlcpy(ConfigGeneral.passHTTP, pass.c_str(), sizeof(ConfigGeneral.passHTTP));
      config_write(path, "passHTTP", pass);
    }

    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/configHTTP"));
    request->send(response);
  }else{
    AsyncWebServerResponse *response = request->beginResponse(303);
    String url="/configHTTP?error="+String(error);
    response->addHeader(F("Location"), url);
    request->send(response);
  }
}

void handleSaveConfigWebPush(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";
  String enableWebPush,webPushAuth;
  
  
  String user = request->arg("userWebPush");
  String pass = request->arg("passWebPush");
  String url = request->arg("servWebPush");
  bool saveOk=true;
  uint8_t error=0;
  
  if (request->arg("webPushAuth") == "on")
  {

    if (strlen(pass.c_str())<4)
    {
      saveOk=false;  
      error=error+1;
    }  

    if (user == "")
    {
      saveOk=false;  
      error=error+2;
    }
  }

  if (request->arg("enableWebPush") == "on")
  {
    if (url == "")
    {
      saveOk = false;
      error=error+4;
    }
  }

  if (saveOk)
  {
    if (request->arg("enableWebPush") == "on")
    {
      enableWebPush = "1";
      ConfigSettings.enableWebPush = true;
    }
    else
    {
      enableWebPush = "0";
      ConfigSettings.enableWebPush = false;
    }
    if (request->arg("webPushAuth") == "on")
    {
      webPushAuth = "1";
      ConfigGeneral.webPushAuth = true;
    }
    else
    {
      enableWebPush = "0";
      ConfigGeneral.webPushAuth = false;
    }
    config_write(path, "enableWebPush", enableWebPush);
    config_write(path, "webPushAuth", webPushAuth);

    if (request->arg("servWebPush"))
    {
      strlcpy(ConfigGeneral.servWebPush, request->arg("servWebPush").c_str(), sizeof(ConfigGeneral.servWebPush));
      config_write(path, "servWebPush", String(request->arg("servWebPush")));
    }

    if (request->arg("userWebPush"))
    {
      strlcpy(ConfigGeneral.userWebPush, request->arg("userWebPush").c_str(), sizeof(ConfigGeneral.userWebPush));
      config_write(path, "userWebPush", String(request->arg("userWebPush")));
    }

    if (pass=="********")
    {
      pass = ConfigGeneral.passWebPush;
      strlcpy(ConfigGeneral.passWebPush, pass.c_str(), sizeof(ConfigGeneral.passWebPush));
      config_write(path, "passWebPush", pass);
    }else{
      strlcpy(ConfigGeneral.passWebPush, pass.c_str(), sizeof(ConfigGeneral.passWebPush));
      config_write(path, "passWebPush", pass);
    }

    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/configWebPush"));
    request->send(response);

  }else{
    AsyncWebServerResponse *response = request->beginResponse(303);
     String url="/configWebPush?error="+String(error);
    response->addHeader(F("Location"), url);
    request->send(response); 
  }
  
}

void handleSaveConfigUDPClient(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";
  String enableUDP;
  
  String port = request->arg("portUDP");
  String url = request->arg("servUDP");
  bool saveOk=true;
  uint8_t error=0;
  
  if (request->arg("enableUDP") == "on")
  {
    if (url == "")
    {
      saveOk = false;
      error=error+2;
    }

    if (port == "")
    {
      saveOk = false;
      error=error+1;
    }
  }

  if (saveOk)
  {
    if (request->arg("enableUDP") == "on")
    {
      enableUDP = "1";
      ConfigSettings.enableUDP = true;
    }
    else
    {
      enableUDP = "0";
      ConfigSettings.enableUDP = false;
    }
    
    config_write(path, "enableUDP", enableUDP);

    if (request->arg("servUDP"))
    {
      strlcpy(ConfigGeneral.servUDP, request->arg("servUDP").c_str(), sizeof(ConfigGeneral.servUDP));
      config_write(path, "servUDP", String(request->arg("servUDP")));
    }

    if (request->arg("portUDP"))
    {
      strlcpy(ConfigGeneral.portUDP, request->arg("portUDP").c_str(), sizeof(ConfigGeneral.portUDP));
      config_write(path, "portUDP", String(request->arg("portUDP")));
    }

    if (request->arg("customUDPJson"))
    {
      ConfigGeneral.customUDPJson = String(request->arg("customUDPJson"));
      config_write(path, "customUDPJson", String(request->arg("customUDPJson")));
    }

   
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/configUdpClient"));
    request->send(response);

  }else{
    AsyncWebServerResponse *response = request->beginResponse(303);
     String url="/configUdpClient?error="+String(error);
    response->addHeader(F("Location"), url);
    request->send(response); 
  }
  
}


void handleSaveConfigParameter(AsyncWebServerRequest *request)
{
  String path = "configGeneral.json";
  String developerMode;
  if (request->arg("developerMode") == "on")
  {
    developerMode = "1";
    ConfigGeneral.developerMode = true;
  }
  else
  {
    developerMode = "0";
    ConfigGeneral.developerMode = false;
  }
  config_write(path, "developerMode", developerMode);

  String enableHWFlow;
  if (request->arg("enableHWFlow") == "on")
  {
    enableHWFlow = "1";
    ConfigSettings.enableHWFlow = true;
  }
  else
  {
    enableHWFlow = "0";
    ConfigSettings.enableHWFlow = false;
  }
  config_write(path, "enableHWFlow", enableHWFlow);

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configGeneral"));
  request->send(response);
}

void handleSaveConfigNotificationMail(AsyncWebServerRequest *request)
{

  String path = "configGeneral.json";
  String enableNotif;
  if (request->arg("enableNotif") == "on")
  {
    enableNotif = "1";
    ConfigSettings.enableNotif = true;
  }
  else
  {
    enableNotif = "0";
    ConfigSettings.enableNotif = false;
  }
  config_write(path, "enableNotif", enableNotif);
  if (request->arg("servSMTP"))
  {
    strlcpy(ConfigGeneral.servSMTP, request->arg("servSMTP").c_str(), sizeof(ConfigGeneral.servSMTP));
    config_write(path, "servSMTP", String(request->arg("servSMTP")));
  }

  if (request->arg("portSMTP"))
  {
    strlcpy(ConfigGeneral.portSMTP, request->arg("portSMTP").c_str(), sizeof(ConfigGeneral.portSMTP));
    config_write(path, "portSMTP", String(request->arg("portSMTP")));
  }

  if (request->arg("userSMTP"))
  {
    strlcpy(ConfigGeneral.userSMTP, request->arg("userSMTP").c_str(), sizeof(ConfigGeneral.userSMTP));
    config_write(path, "userSMTP", String(request->arg("userSMTP")));
  }

  if (request->arg("passSMTP") && request->arg("passSMTP") != "********")
  {
    strlcpy(ConfigGeneral.passSMTP, request->arg("passSMTP").c_str(), sizeof(ConfigGeneral.passSMTP));
    config_write(path, "passSMTP", String(request->arg("passSMTP")));
  }

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configNotif"));
  request->send(response);
}

void APISetResetDevice(AsyncWebServerRequest *request)
{
  String result="";
  if (request->method() != HTTP_POST)
  {
    result="{\"result\" : false}";
  }
  else
  {

    String path="configWifi.json";
    config_write(path,"ssid","");
    config_write(path,"pass","");
    config_write(path,"enableDHCP","1");
    result="{\"result\": true}";
    executeReboot = true;

  }
  AsyncWebServerResponse *response = request->beginResponse(200, F("application/json"), result);
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);

}

void APISetConfigWiFi(AsyncWebServerRequest *request)
{
  String result="";
  if (request->method() != HTTP_POST)
  {
    result="{\"result\" : false}";
  }
  else
  {

    String ssid = request->arg("ssid");
    String password = request->arg("password");
    DEBUG_PRINTLN(ssid);
    DEBUG_PRINTLN(password);  

    if ((ssid.length()>0) && (password.length()>0))
    {
      if ((ssid!="null") && (password!="null"))
      {
        String path="configWifi.json";
        
        config_write(path,"ssid",ssid);
        config_write(path,"pass",password);
        result="{\"result\": true}";
        executeReboot = true;
      }else{
        result="{\"result\" : false}";
      }
      
    }else{
      result="{\"result\" : false}";
    }

  }
  AsyncWebServerResponse *response = request->beginResponse(200, F("application/json"), result);
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);

}

void handleSaveWifi(AsyncWebServerRequest *request)
{
  
  String StringConfig;
  String enableDHCP;
  if (request->arg("DHCPEnable") == "on")
  {
    enableDHCP = "1";
  }
  else
  {
    enableDHCP = "0";
  }
  String ssid = request->arg("WIFISSID");
  String pass = request->arg("WIFIpassword");
  String ipAddress = request->arg("ipAddress");
  String ipMask = request->arg("ipMask");
  String ipGW = request->arg("ipGW");
  String tcpListenPort = request->arg("tcpListenPort");
  bool saveOk=true;
  uint8_t error=0;
  uint8_t ipError=0;

  if (strlen(pass.c_str())<8)
  {
    saveOk=saveOk & false;  
    error=error+1;
  }  

  if (ssid == "")
  {
    saveOk=saveOk & false;  
    error=error+2;
  }

  if (request->arg("DHCPEnable") != "on")
  {
  
    if (!isValidIP(ipAddress))
    {
      saveOk=saveOk & false;
      ipError=1;
    }
    if (!isValidIP(ipMask))
    {
      saveOk=saveOk & false;
      ipError=ipError + 2;
    }

    if (!isValidIP(ipGW))
    {
      saveOk=saveOk & false;
      ipError=ipError + 4;
    }

  }

  if (saveOk)
  {
    if (pass=="********")
    {
      pass = ConfigSettings.password;
    }

    const char *path = "/config/configWifi.json";
    StringConfig = "{\"enableDHCP\":" + enableDHCP + ",\"ssid\":\"" + ssid + "\",\"pass\":\"" + pass + "\",\"ip\":\"" + ipAddress + "\",\"mask\":\"" + ipMask + "\",\"gw\":\"" + ipGW + "\",\"tcpListenPort\":\"" + tcpListenPort + "\"}";
    SpiRamJsonDocument jsonBuffer(512);
    SpiRamJsonDocument doc(MAXHEAP);
    deserializeJson(doc, StringConfig);

    File configFile = LittleFS.open(path, FILE_WRITE);
    if (!configFile)
    {
      DEBUG_PRINTLN(F("failed open"));
    }
    else
    {
      if (!doc.isNull())
      {
        serializeJson(doc, configFile);
      }
    }
    configFile.close();

    executeReboot=true;

    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/"));
    request->send(response);

  }else{
    AsyncWebServerResponse *response = request->beginResponse(303);
    String url="/configWiFi?error="+String(error)+"&ipError="+String(ipError);
    response->addHeader(F("Location"), url);
    request->send(response);
  }
}

void handleConfigDevices(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;

  AsyncResponseStream *response = request->beginResponseStream("text/html");

  // === 1. Header HTML + CSS ===
  response->print(F("<html>"));
  response->print(FPSTR(HTTP_HEADER));

  response->print(F("<style>"
    ".device-card-container{padding:8px}"
    ".config-card{background:#fff;border:none;border-radius:12px;box-shadow:0 2px 8px rgba(0,0,0,0.08);transition:transform 0.2s,box-shadow 0.2s;overflow:hidden}"
    ".config-card:hover{transform:translateY(-2px);box-shadow:0 4px 16px rgba(0,0,0,0.12)}"
    ".card-header-cfg{background:#fff;padding:12px 16px}"
    ".card-header-cfg a{color:#222;text-decoration:none;font-weight:600;font-size:14px;display:flex;align-items:center}"
    ".card-header-cfg a:hover{color:#6c757d;opacity:0.95}"
    ".card-header-cfg svg{flex-shrink:0;margin-right:8px;width:16px;height:16px}"
    ".config-card .card-body{border-top:none}"
    ".config-card .card-body table td{padding:6px 4px;border:none}"
    ".config-card .btn-actions{display:flex;flex-wrap:wrap;gap:6px;margin-top:12px;padding-top:12px;border-top:1px solid #e9ecef}"
    ".config-card .btn{padding:6px 10px;display:inline-flex;align-items:center;justify-content:center}"
    ".config-card .btn svg{width:16px;height:16px;flex-shrink:0}"
    "@media(max-width:576px){"
    ".config-card{border-radius:10px}"
    ".card-header-cfg{padding:10px 12px}"
    ".card-header-cfg a{font-size:13px}"
    ".config-card .btn-actions{justify-content:center}"
    ".config-card .btn{padding:5px 8px}"
    ".config-card .btn svg{width:14px;height:14px}"
    "}"
    "</style>"));

  // === 2. Menu ===
  streamSection(response, HTTP_MENU);

  // === 3. Page template (inline au lieu de HTTP_CONFIG_DEVICES_ZIGBEE + getMenuGeneralZigbee) ===
  response->print(F("<div class='row p-4 justify-content-md-center'>"
    "<div class='col-sm-2'><div class='btn-group-horizontal'>"));

  // Menu zigbee inline (bouton "Appareils" disabled, "Config" actif)
  {
    String menuZigbee = FPSTR(HTTP_CONFIG_MENU_ZIGBEE);
    menuZigbee.replace("{{menu_config_devices}}", "disabled");
    menuZigbee.replace("{{menu_config_zigbee}}", "");
    response->print(menuZigbee);
  }

  response->print(F("</div></div><div class='col-sm-10'>"
    "<h4>Config appareils Zigbee</h4>"
    "<div class='d-flex justify-content-end'>"
    "<a class='btn btn-primary mb-1' href='/assistDevice' style='width:120px;height:64px;'>"
    "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' class='bi bi-plus-circle' viewBox='0 0 16 16'>"
    "<path d='M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16'/>"
    "<path d='M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4'/>"
    "</svg><br> Ajouter</a></div><br>"
    "<h5>Liste des appareils</h5>"
    "<div class='row g-4' style='font-size:12px;'>"));

  // Script OTAUpdateBar
  response->print(F("<script>"
    "function OTAUpdateBar(id){"
    "$.ajax({url:'/OTAUpdateBar?id='+id,type:'GET',success:function(data){"
    "if(data>=0){"
    "$('#uploadOTA'+id).html('<div align=\"center\">En cours ...</div><progress value=\"'+data+'\" max=\"100\" style=\"width:100%\">'+data+'%</progress>');"
    "$('#uploadOTA'+id).show();"
    "}else{$('#uploadOTA'+id).hide();}"
    "setTimeout(function(){OTAUpdateBar(id);},5000);"
    "}});}</script>"));

  // === 4. Boucle devices - streamees une par une ===
  int exist = 0;
  bool mqttHA = (ConfigSettings.enableMqtt && ConfigGeneral.HAMQTT);

  for (size_t ident = 0; ident < devices.size(); ident++)
  {
    DeviceData* device = devices[ident];
    // Page de config Zigbee : ne lister QUE les appareils Zigbee. Un objet LoRa est un
    // appareil normal (memes clusters), donc present dans `devices`, mais il a sa propre
    // page /configLora -- symetrique du filtre applique la-bas.
    if (loraFindEmitterByMac(device->getDeviceID()) >= 0) continue;
    exist++;

    response->print(F("<div class='col-12 col-sm-6 col-md-4 col-lg-3 device-card-container'>"
      "<div class='config-card'><div class='card-header-cfg'><a href='/configDevice?id="));
    response->print(device->getDeviceID());
    response->print(F("'>"));

    if (LittleFS.exists("/web/img/icon_" + device->getInfo().model + ".png")) {
      response->print(F("<img src='web/img/icon_"));
      response->print(device->getInfo().model);
      response->print(F(".png' height='64px'/>"));
    } else {
      response->print(F("<img src='web/img/icon_"));
      response->print(device->getInfo().device_id);
      response->print(F(".png' height='64px'/>"));
    }

    response->print(device->getInfo().alias.length() > 0 ? device->getInfo().alias : device->getDeviceID());
    response->print(F("</a></div><div class='card-body' style='padding:12px 16px;'>"
      "<table style='width:100%;font-size:12px;'><tr>"
      "<td style='color:#6c757d;font-weight:500;'>Manufacturer</td><td style='font-family:Courier New,monospace;text-align:right;'>"));
    response->print(device->getInfo().manufacturer);
    response->print(F("</td></tr><tr><td style='color:#6c757d;font-weight:500;'>Model</td><td style='font-family:Courier New,monospace;text-align:right;'>"));
    response->print(device->getInfo().model);
    response->print(F("</td></tr><tr><td style='color:#6c757d;font-weight:500;'>Short Addr</td><td style='font-family:Courier New,monospace;text-align:right;'>"));
    response->printf("%04X", (unsigned int)device->getInfo().shortAddr.toInt());
    response->print(F("</td></tr><tr><td style='color:#6c757d;font-weight:500;'>Device Id</td><td style='font-family:Courier New,monospace;text-align:right;'>"));
    response->printf("%04X", (unsigned int)device->getInfo().device_id.toInt());
    response->print(F("</td></tr><tr><td style='color:#6c757d;font-weight:500;'>Soft Version</td><td style='font-family:Courier New,monospace;text-align:right;'>"));
    response->print(device->getInfo().software_version);
    response->printf(" <a onClick=\"ZigbeeSendRequest(%s,%s,0,16384)\">",
      device->getInfo().shortAddr.c_str(), device->getInfo().endpoint.c_str());
    response->print(F("<svg xmlns='http://www.w3.org/2000/svg' style='width:16px;' width='16' height='16' fill='currentColor' class='bi bi-arrow-clockwise' viewBox='0 0 16 16'>"
      "<path fill-rule='evenodd' d='M8 3a5 5 0 1 0 4.546 2.914.5.5 0 0 1 .908-.417A6 6 0 1 1 8 2z'/>"
      "<path d='M8 4.466V.534a.25.25 0 0 1 .41-.192l2.36 1.966c.12.1.12.284 0 .384L8.41 4.658A.25.25 0 0 1 8 4.466'/>"
      "</svg></a>"));
    response->print(F("</td></tr><tr><td style='color:#6c757d;font-weight:500;'>Last seen</td><td style='font-family:Courier New,monospace;text-align:right;'>"));
    response->print(device->getInfo().lastSeen);
    response->print(F("</td></tr><tr><td style='color:#6c757d;font-weight:500;'>LQI</td><td style='font-family:Courier New,monospace;text-align:right;'>"));
    response->print(device->getInfo().LQI);
    response->print(F("</td></tr></table>"));

    // OTA progress bar
    response->printf("<div id='uploadOTA%s' style='display:none;margin-top:10px;'><div align='center'>Updating ...</div>"
      "<progress value='%d' max='100' style='width:100%%'>%d%%</progress></div>"
      "<script>OTAUpdateBar('%s');</script>",
      device->getDeviceID().c_str(), device->otaPercentage, device->otaPercentage, device->getDeviceID().c_str());

    // Boutons d'action
    response->print(F("<div class='btn-actions'>"));

    // Fiche appareil
    response->print(F("<a href='/configDevice?id="));
    response->print(device->getDeviceID());
    response->print(F("' class='btn btn-info' title='Fiche appareil'>"
      "<svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'>"
      "<path d='M14.5 3a.5.5 0 0 1 .5.5v9a.5.5 0 0 1-.5.5h-13a.5.5 0 0 1-.5-.5v-9a.5.5 0 0 1 .5-.5zm-13-1A1.5 1.5 0 0 0 0 3.5v9A1.5 1.5 0 0 0 1.5 14h13a1.5 1.5 0 0 0 1.5-1.5v-9A1.5 1.5 0 0 0 14.5 2z'/>"
      "<path d='M5 8a.5.5 0 0 1 .5-.5h7a.5.5 0 0 1 0 1h-7A.5.5 0 0 1 5 8m0-2.5a.5.5 0 0 1 .5-.5h7a.5.5 0 0 1 0 1h-7a.5.5 0 0 1-.5-.5m0 5a.5.5 0 0 1 .5-.5h7a.5.5 0 0 1 0 1h-7a.5.5 0 0 1-.5-.5m-1-5a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0M4 8a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0m0 2.5a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0'/>"
      "</svg></a>"));

    // MQTT Discover
    if (mqttHA) {
      response->printf("<button onclick=\"sendMqttDiscover('%s');\" class='btn btn-warning' title='MQTT Discover'>",
        device->getInfo().shortAddr.c_str());
      response->print(F("<svg viewBox='0 0 24 24' xmlns='http://www.w3.org/2000/svg' fill='currentColor'>"
        "<path d='M10.657 23.994h-9.45A1.212 1.212 0 0 1 0 22.788v-9.18h0.071c5.784 0 10.504 4.65 10.586 10.386Zm7.606 0h-4.045C14.135 16.246 7.795 9.977 0 9.942V6.038h0.071c9.983 0 18.121 8.044 18.192 17.956Zm4.53 0h-0.97C21.754 12.071 11.995 2.407 0 2.372v-1.16C0 0.55 0.544 0.006 1.207 0.006h7.64C15.733 2.49 21.257 7.789 24 14.508v8.291c0 0.663 -0.544 1.195 -1.207 1.195ZM16.713 0.006h6.092A1.19 1.19 0 0 1 24 1.2v5.914c-0.91 -1.242 -2.046 -2.65 -3.158 -3.762C19.588 2.11 18.122 0.987 16.714 0.005Z'/>"
        "</svg></button>"));
    }

    // OTA
    response->print(F("<a href='/ota?id="));
    response->print(device->getDeviceID());
    response->print(F("' class='btn btn-warning' title='OTA Update'>"
      "<svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'>"
      "<path fill-rule='evenodd' d='M7.646 10.854a.5.5 0 0 0 .708 0l2-2a.5.5 0 0 0-.708-.708L8.5 9.293V5.5a.5.5 0 0 0-1 0v3.793L6.354 8.146a.5.5 0 1 0-.708.708z'/>"
      "<path d='M4.406 3.342A5.53 5.53 0 0 1 8 2c2.69 0 4.923 2 5.166 4.579C14.758 6.804 16 8.137 16 9.773 16 11.569 14.502 13 12.687 13H3.781C1.708 13 0 11.366 0 9.318c0-1.763 1.266-3.223 2.942-3.593.143-.863.698-1.723 1.464-2.383m.653.757c-.757.653-1.153 1.44-1.153 2.056v.448l-.445.049C2.064 6.805 1 7.952 1 9.318 1 10.785 2.23 12 3.781 12h8.906C13.98 12 15 10.988 15 9.773c0-1.216-1.02-2.228-2.313-2.228h-.5v-.5C12.188 4.825 10.328 3 8 3a4.53 4.53 0 0 0-2.941 1.1z'/>"
      "</svg></a>"));

    // Refresh
    response->printf("<button onclick=\"ZigbeeSendRequest(%s,%s,0,5);\" class='btn btn-warning' title='Refresh'>",
      device->getInfo().shortAddr.c_str(), device->getInfo().endpoint.c_str());
    response->print(F("<svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'>"
      "<path fill-rule='evenodd' d='M8 3a5 5 0 1 0 4.546 2.914.5.5 0 0 1 .908-.417A6 6 0 1 1 8 2z'/>"
      "<path d='M8 4.466V.534a.25.25 0 0 1 .41-.192l2.36 1.966c.12.1.12.284 0 .384L8.41 4.658A.25.25 0 0 1 8 4.466'/>"
      "</svg></button>"));

    // Supprimer
    response->printf("<button onclick=\"deleteDevice('%s');\" class='btn btn-danger' title='Supprimer'>",
      device->getDeviceID().c_str());
    response->print(F("<svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'>"
      "<path d='M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z'/>"
      "<path d='M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H6a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1h3.5a1 1 0 0 1 1 1zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z'/>"
      "</svg></button>"));

    response->print(F("</div></div></div></div>")); // btn-actions, card-body, config-card, col

    vTaskDelay(1); // Eviter watchdog timeout
  }

  // Pas de devices
  if (exist == 0) {
    response->print(F("<div align='center' style='height:100px;font-size:28px;font-weight:bold;'>No devices yet</div>"));
  }

  // Script deleteDevice
  response->print(F("<script>"
    "function deleteDevice(devId){"
    "if(confirm('Are you sure you want to delete this device ?')){"
    "var xhr=getXhr();"
    "xhr.onreadystatechange=function(){"
    "if(xhr.readyState==4){"
    "if(xhr.status==200){window.location.href='/configDevices';}"
    "else{alert('Erreur lors de la suppression');}}};"
    "xhr.open('GET','deleteDevice?devId='+encodeURIComponent(devId),true);"
    "xhr.send();}}</script>"));

  // === 5. Fermer les divs du template + footer ===
  response->print(F("</div></div>")); // row g-4, col-sm-10
  response->print(footer());
  response->print(F("</html>"));

  request->send(response);
}

// ============================================================
// Page détaillée d'un appareil - configDevice
// ============================================================
void handleConfigDevice(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;
  // Vérifier le paramètre id
  if (!request->hasParam("id")) {
    request->redirect("/configDevices");
    return;
  }
  
  String deviceID = request->getParam("id")->value();
  
  // Rechercher le device
  DeviceData* device = nullptr;
  for (size_t i = 0; i < devices.size(); i++) {
    if (devices[i]->getDeviceID() == deviceID) {
      device = devices[i];
      break;
    }
  }
  
  if (device == nullptr) {
    request->redirect("/configDevices");
    return;
  }
  
  PSRAMString result(150000);
  result = F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  
  // Styles CSS pour la page
  result += F("<style>");
  result += F(".device-card { background: #fff; border-radius: 12px; box-shadow: 0 2px 8px rgba(0,0,0,0.1); padding: 20px; margin-bottom: 20px; }");
  result += F(".device-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; padding-bottom: 15px; border-bottom: 2px solid #e9ecef; }");
  result += F(".device-title { font-size: 24px; font-weight: bold; color: #333; }");
  result += F(".device-subtitle { font-size: 14px; color: #6c757d; }");
  result += F(".info-table { width: 100%; border-collapse: collapse; }");
  result += F(".info-table td { padding: 8px 12px; border-bottom: 1px solid #e9ecef; }");
  result += F(".info-table td:first-child { font-weight: bold; color: #555; width: 40%; }");
  result += F(".info-table td:last-child { font-family: 'Courier New', monospace; text-align: right; word-break: break-all; }");
  result += F(".section-title { font-size: 18px; font-weight: bold; color: #333; margin: 20px 0 15px 0; padding-bottom: 8px; border-bottom: 1px solid #dee2e6; }");
  result += F(".attr-table { width: 100%; border-collapse: collapse; font-size: 13px; }");
  result += F(".attr-table th { background: #f8f9fa; padding: 10px; text-align: left; border-bottom: 2px solid #dee2e6; }");
  result += F(".attr-table td { padding: 8px 10px; border-bottom: 1px solid #e9ecef; vertical-align: middle; }");
  result += F(".attr-table tr:hover { background: #f8f9fa; }");
  result += F(".cluster-badge { background: #007bff; color: white; padding: 2px 8px; border-radius: 4px; font-size: 11px; font-family: monospace; }");
  result += F(".attr-badge { background: #6c757d; color: white; padding: 2px 8px; border-radius: 4px; font-size: 11px; font-family: monospace; }");
  result += F(".mfr-badge { background: #dc3545; color: white; padding: 2px 8px; border-radius: 4px; font-size: 11px; font-family: monospace; }");
  result += F(".value-cell { font-family: 'Courier New', monospace; }");
  result += F(".btn-read { background: #17a2b8; border: none; color: white; padding: 4px 10px; border-radius: 4px; cursor: pointer; font-size: 12px; }");
  result += F(".btn-read:hover { background: #138496; }");
  result += F(".btn-write { background: #28a745; border: none; color: white; padding: 4px 10px; border-radius: 4px; cursor: pointer; font-size: 12px; }");
  result += F(".btn-write:hover { background: #218838; }");
  result += F(".writable-input { width: 80px; padding: 4px 8px; border: 1px solid #ced4da; border-radius: 4px; font-family: monospace; }");
  result += F(".action-btn { margin: 5px 5px 5px 0; }");
  result += F(".back-link { display: inline-flex; align-items: center; color: #007bff; text-decoration: none; margin-bottom: 15px; }");
  result += F(".back-link:hover { text-decoration: underline; }");
  // Styles pour édition inline du titre
  result += F(".title-container { display: flex; align-items: center; gap: 10px; }");
  result += F(".title-text { cursor: pointer; }");
  result += F(".title-text:hover { color: #007bff; }");
  result += F(".btn-edit { background: none; border: none; color: #6c757d; cursor: pointer; padding: 4px; border-radius: 4px; }");
  result += F(".btn-edit:hover { background: #e9ecef; color: #333; }");
  result += F(".title-input { font-size: 24px; font-weight: bold; border: 2px solid #007bff; border-radius: 6px; padding: 4px 8px; width: 250px; }");
  result += F(".title-input:focus { outline: none; box-shadow: 0 0 0 3px rgba(0,123,255,0.25); }");
  result += F(".btn-save { background: #28a745; border: none; color: white; padding: 6px 12px; border-radius: 4px; cursor: pointer; font-size: 14px; }");
  result += F(".btn-save:hover { background: #218838; }");
  result += F(".btn-cancel { background: #6c757d; border: none; color: white; padding: 6px 12px; border-radius: 4px; cursor: pointer; font-size: 14px; }");
  result += F(".btn-cancel:hover { background: #5a6268; }");
  result += F(".edit-buttons { display: flex; gap: 5px; }");
  result += F(".saving-indicator { color: #6c757d; font-style: italic; }");
  result += F(".pending { opacity: 0.6; }");
  result += F(".success { background-color: #d4edda !important; }");
  result += F(".error { background-color: #f8d7da !important; }");
  // Animation pour changement de valeur
  result += F("@keyframes valueChanged { 0% { background-color: #fff3cd; } 100% { background-color: transparent; } }");
  result += F(".value-changed { animation: valueChanged 2s ease-out; }");
  result += F(".value-cell span, .value-cell input { transition: background-color 0.3s; padding: 2px 4px; border-radius: 3px; }");
  // Styles responsive mobile
  result += F("@media (max-width: 576px) {");
  result += F("  .container { padding: 10px !important; }");
  result += F("  .device-card { padding: 12px !important; margin-bottom: 15px !important; }");
  result += F("  .device-header { flex-direction: column; align-items: flex-start !important; }");
  result += F("  .device-title { font-size: 18px !important; word-break: break-all; }");
  result += F("  .device-subtitle { font-size: 12px !important; word-break: break-all; }");
  result += F("  .title-container { flex-wrap: wrap; }");
  result += F("  .title-input { font-size: 16px !important; width: 100% !important; max-width: 200px; }");
  result += F("  .edit-buttons { margin-top: 5px; }");
  result += F("  .section-title { font-size: 16px !important; }");
  result += F("  .info-table td { padding: 6px 4px !important; font-size: 12px !important; }");
  result += F("  .info-table td:first-child { width: 45% !important; }");
  result += F("  .attr-table { display: block; overflow-x: auto; white-space: nowrap; }");
  result += F("  .attr-table th, .attr-table td { padding: 6px 4px !important; font-size: 11px !important; }");
  result += F("  .attr-table .cluster-badge, .attr-table .attr-badge, .attr-table .mfr-badge { padding: 1px 4px !important; font-size: 10px !important; }");
  result += F("  .writable-input { width: 60px !important; font-size: 11px !important; }");
  result += F("  .btn-read, .btn-write { padding: 3px 6px !important; font-size: 11px !important; }");
  result += F("  .action-btn { padding: 8px 12px !important; font-size: 12px !important; margin: 3px !important; }");
  result += F("}");
  // Affichage alternatif en liste pour très petits écrans
  result += F("@media (max-width: 400px) {");
  result += F("  .attr-table, .attr-table tbody, .attr-table tr, .attr-table td, .attr-table th { display: block; white-space: normal; }");
  result += F("  .attr-table thead { display: none; }");
  result += F("  .attr-table tr { margin-bottom: 12px; border: 1px solid #dee2e6; border-radius: 8px; padding: 8px; }");
  result += F("  .attr-table td { border: none !important; padding: 4px 0 !important; text-align: left !important; }");
  result += F("  .attr-table td:before { content: attr(data-label); font-weight: bold; display: inline-block; width: 80px; color: #555; }");
  result += F("  .attr-table td.actions-cell { text-align: center !important; margin-top: 8px; }");
  result += F("}");
  result += F("</style>");
  
  // Scripts JavaScript
  result += F("<script>");
  // Variable pour stocker l'ID de l'appareil
  result += F("var deviceId = '");
  result += device->getDeviceID();
  result += F("';");
  result += F("var previousValues = {};");
  
  // Fonction pour marquer visuellement un changement
  result += F("function highlightChange(el) {");
  result += F("  el.classList.remove('value-changed');");
  result += F("  void el.offsetWidth;");
  result += F("  el.classList.add('value-changed');");
  result += F("}");
  
  // Fonction pour récupérer les valeurs de cet appareil
  result += F("function refreshDeviceValues() {");
  result += F("  fetch('/getDeviceAttrValues?id=' + deviceId).then(r => r.json()).then(data => {");
  result += F("    for(var id in data) {");
  result += F("      var el = document.getElementById(id);");
  result += F("      if(el) {");
  result += F("        var newVal = data[id];");
  result += F("        var oldVal = previousValues[id];");
  result += F("        if(el.tagName === 'INPUT') {");
  result += F("          if(oldVal !== undefined && oldVal !== newVal) { highlightChange(el); }");
  result += F("          el.value = newVal;");
  result += F("        } else {");
  result += F("          if(oldVal !== undefined && oldVal !== newVal) { highlightChange(el); }");
  result += F("          el.textContent = newVal;");
  result += F("        }");
  result += F("        previousValues[id] = newVal;");
  result += F("      }");
  result += F("    }");
  result += F("  }).catch(e => console.error('Refresh error:', e));");
  result += F("}");
  
  // Refresh permanent toutes les 5 secondes
  result += F("setInterval(refreshDeviceValues, 5000);");
  // Premier refresh au chargement
  result += F("refreshDeviceValues();");
  
  // Fonction de lecture d'attribut avec support manufacturer specific
  result += F("function readAttribute(btnElement, shortAddr, endpoint, cluster, attr, spanId, mfrCode) {");
  result += F("  if(btnElement) btnElement.classList.add('pending');");
  result += F("  var url = '/ZigbeeReadAttribut?addr=' + shortAddr + '&endpoint=' + endpoint + '&cluster=' + cluster + '&attr=' + attr;");
  result += F("  if(mfrCode && mfrCode > 0) { url += '&mfr=' + mfrCode; }");
  result += F("  fetch(url)");
  result += F("  .then(r => r.json()).then(d => {");
  result += F("    if(btnElement) btnElement.classList.remove('pending');");
  result += F("    if(d.success) {");
  result += F("      if(btnElement) { btnElement.classList.add('success'); setTimeout(() => btnElement.classList.remove('success'), 2000); }");
  result += F("    } else {");
  result += F("      if(btnElement) { btnElement.classList.add('error'); setTimeout(() => btnElement.classList.remove('error'), 3000); }");
  result += F("    }");
  result += F("  }).catch(e => { if(btnElement) { btnElement.classList.remove('pending'); btnElement.classList.add('error'); } });");
  result += F("}");
  
  // Fonction d'écriture d'attribut avec support manufacturer specific
  result += F("function writeAttribute(btnElement, shortAddr, endpoint, cluster, attr, type, inputId, mfrCode, coeff) {");
  result += F("  var inp = document.getElementById(inputId);");
  result += F("  var val = inp.value;");
  result += F("  if(coeff && coeff != 1 && coeff != 0) { val = Math.round(parseFloat(val) / coeff); }");
  result += F("  if(btnElement) btnElement.classList.add('pending');");
  result += F("  var url = '/ZigbeeWriteAttribut?addr=' + shortAddr + '&endpoint=' + endpoint + '&cluster=' + cluster + '&attr=' + attr + '&type=' + type + '&value=' + encodeURIComponent(val);");
  result += F("  if(mfrCode && mfrCode > 0) { url += '&mfr=' + mfrCode; }");
  result += F("  fetch(url)");
  result += F("  .then(r => r.json()).then(d => {");
  result += F("    if(btnElement) btnElement.classList.remove('pending');");
  result += F("    if(d.success) {");
  result += F("      if(btnElement) { btnElement.classList.add('success'); setTimeout(function() { btnElement.classList.remove('success'); }, 2000); }");
  result += F("      setTimeout(function() { readAttribute(null, shortAddr, endpoint, cluster, attr, inputId, mfrCode); }, 500);");
  result += F("    } else {");
  result += F("      if(btnElement) { btnElement.classList.add('error'); setTimeout(function() { btnElement.classList.remove('error'); }, 3000); }");
  result += F("      alert('Erreur: ' + (d.error || 'Echec'));");
  result += F("    }");
  result += F("  }).catch(function(e) { if(btnElement) { btnElement.classList.remove('pending'); btnElement.classList.add('error'); } });");
  result += F("}");
  
  // Variables pour l'édition du titre
  result += F("var currentAlias = '");
  String jsAlias = device->getInfo().alias;
  jsAlias.replace("\\", "\\\\");
  jsAlias.replace("'", "\\'");
  result += jsAlias;
  result += F("';");
  
  // Fonction pour démarrer l'édition du titre
  result += F("function startEditTitle() {");
  result += F("  var container = document.getElementById('titleContainer');");
  result += F("  var currentText = currentAlias || deviceId;");
  result += F("  container.innerHTML = \"<input type='text' id='titleInput' class='title-input' value='\" + currentText.replace(/'/g, '&#39;') + \"' placeholder='Nom du device...'/>\" +");
  result += F("    \"<div class='edit-buttons'>\" +");
  result += F("    \"<button class='btn-save' onclick='saveTitle()'>&#10003;</button>\" +");
  result += F("    \"<button class='btn-cancel' onclick='cancelEditTitle()'>&#10005;</button>\" +");
  result += F("    \"</div>\";");
  result += F("  var inp = document.getElementById('titleInput');");
  result += F("  inp.focus();");
  result += F("  inp.select();");
  result += F("  inp.addEventListener('keydown', function(e) {");
  result += F("    if(e.key === 'Enter') { e.preventDefault(); saveTitle(); }");
  result += F("    if(e.key === 'Escape') { e.preventDefault(); cancelEditTitle(); }");
  result += F("  });");
  result += F("}");
  
  // Fonction pour annuler l'édition
  result += F("function cancelEditTitle() {");
  result += F("  showTitleDisplay();");
  result += F("}");
  
  // Fonction pour afficher le titre en mode lecture
  result += F("function showTitleDisplay() {");
  result += F("  var container = document.getElementById('titleContainer');");
  result += F("  var displayText = currentAlias || deviceId;");
  result += F("  container.innerHTML = \"<span class='title-text' onclick='startEditTitle()' title='Cliquer pour modifier'>\" + displayText + \"</span>\" +");
  result += F("    \"<button class='btn-edit' onclick='startEditTitle()' title='Modifier le nom'>\" +");
  result += F("    \"<svg xmlns='http://www.w3.org/2000/svg' width='18' height='18' fill='currentColor' viewBox='0 0 16 16'>\" +");
  result += F("    \"<path d='M12.146.146a.5.5 0 0 1 .708 0l3 3a.5.5 0 0 1 0 .708l-10 10a.5.5 0 0 1-.168.11l-5 2a.5.5 0 0 1-.65-.65l2-5a.5.5 0 0 1 .11-.168l10-10zM11.207 2.5 13.5 4.793 14.793 3.5 12.5 1.207 11.207 2.5zm1.586 3L10.5 3.207 4 9.707V10h.5a.5.5 0 0 1 .5.5v.5h.5a.5.5 0 0 1 .5.5v.5h.293l6.5-6.5zm-9.761 5.175-.106.106-1.528 3.821 3.821-1.528.106-.106A.5.5 0 0 1 5 12.5V12h-.5a.5.5 0 0 1-.5-.5V11h-.5a.5.5 0 0 1-.468-.325z'/>\" +");
  result += F("    \"</svg></button>\";");
  result += F("}");
  
  // Fonction pour sauvegarder le titre
  result += F("function saveTitle() {");
  result += F("  var inp = document.getElementById('titleInput');");
  result += F("  var newAlias = inp.value.trim();");
  result += F("  var container = document.getElementById('titleContainer');");
  result += F("  container.innerHTML = \"<span class='saving-indicator'>Sauvegarde...</span>\";");
  result += F("  fetch('/setAlias?ieee=' + deviceId + '&alias=' + encodeURIComponent(newAlias))");
  result += F("  .then(r => r.text()).then(d => {");
  result += F("    if(d === 'OK') {");
  result += F("      currentAlias = newAlias;");
  result += F("      showTitleDisplay();");
  result += F("    } else {");
  result += F("      alert('Erreur lors de la sauvegarde');");
  result += F("      showTitleDisplay();");
  result += F("    }");
  result += F("  }).catch(e => {");
  result += F("    alert('Erreur de connexion');");
  result += F("    showTitleDisplay();");
  result += F("  });");
  result += F("}");
  result += F("</script>");
  
  // Contenu de la page
  result += F("<div class='container mt-4'>");
  
  // Lien retour
  result += F("<button onclick='history.back()' class='btn btn-primary'>");
  result += F("<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20' fill='currentColor' viewBox='0 0 16 16' style='margin-right:5px;'>");
  result += F("<path fill-rule='evenodd' d='M15 8a.5.5 0 0 0-.5-.5H2.707l3.147-3.146a.5.5 0 1 0-.708-.708l-4 4a.5.5 0 0 0 0 .708l4 4a.5.5 0 0 0 .708-.708L2.707 8.5H14.5A.5.5 0 0 0 15 8'/>");
  result += F("</svg> Retour</button><br>");
  
  // Carte principale
  result += F("<div class='device-card'>");
  
  // Header avec titre
  result += F("<div class='device-header'><div>");
  result += F("<div class='device-title title-container' id='titleContainer'>");
  result += F("</div>");
  result += F("<div class='device-subtitle'>MAC: ");
  result += device->getDeviceID();
  result += F("</div></div></div>");
  // Initialiser l'affichage du titre
  result += F("<script>showTitleDisplay();</script>");
  
  // Radio d'origine de l'appareil : un émetteur LoRa appairé porte la MAC de l'appareil.
  // Un appareil LoRa n'est pas interrogeable (le récepteur ne fait que pousser ce que
  // l'émetteur envoie), et ses clusters/attributs ne sont qu'un artifice pour réutiliser le
  // pipeline Zigbee : plusieurs blocs de cette page n'ont donc pas de sens pour lui.
  int  loraSlot = loraFindEmitterByMac(device->getDeviceID());
  bool isLora   = (loraSlot >= 0);

  // Section Informations générales
  result += F("<div class='section-title'>Informations générales</div>");
  result += F("<table class='info-table'>");
  // Adresse courte : notion Zigbee, forcée à 0 pour un appareil LoRa -> masquée.
  if (!isLora) {
    result += F("<tr><td>Adresse courte</td><td>");
    char sAddr[5];
    snprintf(sAddr, 5, "%04X", device->getInfo().shortAddr.toInt());
    result += sAddr;
    result += F("</td></tr>");
  }

  result += F("<tr><td>Fabricant</td><td>");
  result += device->getInfo().manufacturer;
  result += F("</td></tr>");
  
  result += F("<tr><td>Modèle</td><td>");
  result += device->getInfo().model;
  result += F("</td></tr>");
  
  result += F("<tr><td>Device ID</td><td>");
  char devId[5];
  snprintf(devId, 5, "%04X", device->getInfo().device_id.toInt());
  result += devId;
  result += F("</td></tr>");
  
  // Endpoint : notion Zigbee, fixée à 1 à la création d'un appareil LoRa -> masquée.
  if (!isLora) {
    result += F("<tr><td>Endpoint</td><td>");
    result += device->getInfo().endpoint;
    result += F("</td></tr>");
  }

  // Version logicielle : conservée en LoRa, l'émetteur la remontera plus tard.
  result += F("<tr><td>Version logicielle</td><td>");
  result += device->getInfo().software_version;
  result += F("</td></tr>");
  
  result += F("<tr><td>Dernière activité</td><td>");
  result += device->getInfo().lastSeen;
  result += F("</td></tr>");
  
  if (isLora) {
    // Le LQI est une métrique Zigbee : elle vaut 0 et ne veut rien dire ici. La qualité du
    // lien radio se lit sur le RSSI et le SNR de la dernière trame reçue.
    const LoraEmitter &em = loraEmitters[loraSlot];
    result += F("<tr><td>RSSI</td><td>");
    result += String(em.lastRssi, 0);
    result += F(" dBm</td></tr>");

    result += F("<tr><td>SNR</td><td>");
    result += String(em.lastSnr, 0);
    result += F(" dB</td></tr>");
  } else {
    result += F("<tr><td>LQI</td><td>");
    result += device->getInfo().LQI;
    result += F("</td></tr>");
  }

  result += F("<tr><td>Statut</td><td>");
  result += device->getInfo().Status;
  result += F("</td></tr>");
  
  result += F("</table>");
  
  // Section Attributs du template
  TemplateData* t = device->getTemplate();
  int shortAddr = device->getInfo().shortAddr.toInt();
  int endpoint = device->getInfo().endpoint.toInt();
  bool isZLinky = (device->getInfo().model == "ZLinky_TIC");
  int linkyMode = device->getInfo().linkyMode.toInt();
  
  // Cluster/Attribut/Mfr/Actions sont masqués en LoRa : les afficher promettrait des
  // interactions qui n'existent pas (cf. isLora plus haut).
  if (t != nullptr && t->StateSize() > 0) {
    result += F("<div class='section-title'>Attributs (");
    result += String(t->StateSize());
    result += F(" états)</div>");

    result += F("<table class='attr-table'>");
    if (isLora) {
      result += F("<thead><tr><th>Nom</th><th>Valeur</th><th>Unité</th></tr></thead>");
    } else {
      result += F("<thead><tr><th>Nom</th><th>Cluster</th><th>Attribut</th><th>Mfr</th><th>Valeur</th><th>Unité</th><th>Actions</th></tr></thead>");
    }
    result += F("<tbody>");
    
    for (int i = 0; i < t->StateSize(); i++) {
      // Filtrage par mode Linky pour ZLinky_TIC
      if (isZLinky) {
        bool afficheOK = false;
        const char *tmp = t->states[i].mode;
        
        if ((tmp != NULL) && (tmp[0] != '\0')) {
          char modeCopy[50];
          strncpy(modeCopy, tmp, sizeof(modeCopy) - 1);
          modeCopy[sizeof(modeCopy) - 1] = '\0';
          
          char *pch = strtok(modeCopy, ";");
          while (pch != NULL) {
            if (atoi(pch) == linkyMode) {
              afficheOK = true;
              break;
            }
            pch = strtok(NULL, ";");
          }
        } else {
          afficheOK = true;
        }
        
        if (!afficheOK) continue;
      }
      
      String attrId = String(shortAddr) + "_" + String(t->states[i].cluster) + "_" + String(t->states[i].attribute);
      
      result += F("<tr>");
      
      // Nom
      result += F("<td data-label='Nom'>");
      result += t->states[i].name;
      result += F("</td>");
      
      if (!isLora) {
        // Cluster
        result += F("<td data-label='Cluster'><span class='cluster-badge'>");
        char clusterHex[5];
        snprintf(clusterHex, 5, "%04X", t->states[i].cluster);
        result += clusterHex;
        result += F("</span></td>");

        // Attribut
        result += F("<td data-label='Attribut'><span class='attr-badge'>");
        char attrHex[5];
        snprintf(attrHex, 5, "%04X", t->states[i].attribute);
        result += attrHex;
        result += F("</span></td>");

        // Manufacturer Code
        result += F("<td data-label='Mfr'>");
        if (t->states[i].manufacturerCode != 0) {
          result += F("<span class='mfr-badge'>");
          char mfrHex[7];
          snprintf(mfrHex, 7, "%04X", t->states[i].manufacturerCode);
          result += mfrHex;
          result += F("</span>");
        } else {
          result += F("-");
        }
        result += F("</td>");
      }

      // Valeur (avec input si writable). En LoRa jamais d'input : rien n'est inscriptible
      // et, la colonne Actions étant masquée, le champ n'aurait aucun bouton d'envoi.
      result += F("<td data-label='Valeur' class='value-cell'>");
      if (t->states[i].writable && !isLora) {
        result += F("<input type='text' class='writable-input' id='");
        result += attrId;
        result += F("' value='");
        result += GetValueStatus(device->getDeviceID(), t->states[i].cluster, t->states[i].attribute, 
                                  (String)t->states[i].type, t->states[i].coefficient);
        result += F("' />");
      } else {
        result += F("<span id='");
        result += attrId;
        result += F("'>");
        result += GetValueStatus(device->getDeviceID(), t->states[i].cluster, t->states[i].attribute, 
                                  (String)t->states[i].type, t->states[i].coefficient);             
        result += F("</span>");
      }
      result += F("</td>");
      
      // Unité
      result += F("<td data-label='Unité'>");
      result += t->states[i].unit;
      result += F("</td>");
      
      // Actions (Zigbee uniquement : lire/écrire supposent une requête vers l'appareil)
      if (isLora) { result += F("</tr>"); continue; }

      result += F("<td data-label='' class='actions-cell'>");

      // Bouton lecture avec manufacturer code
      result += F("<button class='btn-read' onclick=\"readAttribute(this, ");
      result += String(shortAddr);
      result += F(", ");
      result += String(endpoint);
      result += F(", ");
      result += String(t->states[i].cluster);
      result += F(", ");
      result += String(t->states[i].attribute);
      result += F(", '");
      result += attrId;
      result += F("', ");
      result += String(t->states[i].manufacturerCode);
      result += F(")\" title='Lire'>&#8635;</button> ");
      
      // Bouton écriture si writable avec manufacturer code
      if (t->states[i].writable) {
        result += F("<button class='btn-write' onclick=\"writeAttribute(this, ");
        result += String(shortAddr);
        result += F(", ");
        result += String(endpoint);
        result += F(", ");
        result += String(t->states[i].cluster);
        result += F(", ");
        result += String(t->states[i].attribute);
        result += F(", ");
        result += String(t->states[i].typewritable);
        result += F(", '");
        result += attrId;
        result += F("', ");
        result += String(t->states[i].manufacturerCode);
        result += F(", ");
        result += String(t->states[i].coefficient, 6);
        result += F(")\" title='Écrire'>&#9998;</button>");
      }
      result += F("</td>");
      
      result += F("</tr>");
    }
    result += F("</tbody></table>");
  } else {
    result += F("<div class='section-title'>Attributs</div>");
    result += F("<p style='color:#6c757d;'>Aucun template disponible pour cet appareil.</p>");
  }
  
  // Section Actions du template (masquée en LoRa : ces boutons envoient des commandes Zigbee)
  if (t != nullptr && t->ActionSize() > 0 && !isLora) {
    result += F("<div class='section-title'>Actions (");
    result += String(t->ActionSize());
    result += F(")</div>");
    result += F("<div style='display:flex;flex-wrap:wrap;gap:5px;'>");
    
    for (int i = 0; i < t->ActionSize(); i++) {
      result += F("<button onclick=\"ZigbeeAction(");
      result += String(shortAddr);
      result += F(",");
      result += String(t->actions[i].command);
      result += F(",");
      result += String(t->actions[i].endpoint);
      result += F(",");
      result += String(t->actions[i].value);

      if (t->actions[i].command == 400) {
        result += F(",");
        result += String(t->actions[i].cluster);
        result += F(",");
        result += String(t->actions[i].manufacturerCode);
      }

      result += F(");\" class='btn btn-primary action-btn'>");
      result += t->actions[i].name;
      result += F("</button>");
    }
    result += F("</div>");
  }
  
  result += F("</div>"); // fin device-card
  result += F("</div>"); // fin container
  
  result += footer();
  result.replace("{{FormattedDate}}", FormattedDate);
  result += F("</html>");
  
  request->send(200, F("text/html"), result.c_str());
}

void handleAssistDevice(AsyncWebServerRequest *request)
{
  if (!checkHeapForPage(request)) return;

  Serial.printf("[AssistDevice] Heap: %u, client: %s\n", ESP.getFreeHeap(), request->client()->remoteIP().toString().c_str());

  // Même assistant pour les deux radios : seule l'étape "mise en jumelage" diffère (la
  // commande envoyée à la passerelle et son libellé). La suite est identique car
  // l'appairage LoRa publie la même alerte "appareil trouvé" (code 3) que le Zigbee.
  bool lora = (request->arg("type") == "lora");

  // Construction String pour garantir Content-Length (compatibilité tunnel)
  String result;
  result.reserve(8192);
  result = F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_ASSIST_DEVICE);
  result += footerAssist();
  result.replace("{{FormattedDate}}", FormattedDate);
  if (lora) {
    result.replace("{{pairCmd}}",   F("LoraPairAssist"));
    result.replace("{{pairTitle}}", F("Fenêtre d'appairage LoRa ouverte"));
    result.replace("{{pairDesc}}",  F("La LiXee-Box écoute sur le canal d'appairage pendant 30 s. Mettez maintenant l'appareil LoRa en mode appairage."));
    result.replace("{{backUrl}}",   F("/configLora"));
  } else {
    result.replace("{{pairCmd}}",   F("PermitJoinAssist"));
    result.replace("{{pairTitle}}", F("Mode jumelage de la LiXee-Box"));
    result.replace("{{pairDesc}}",  F("Vérifiez que la LiXee-Box clignote"));
    result.replace("{{backUrl}}",   F("/configDevices"));
  }
  result += F("</html>");

  Serial.printf("[AssistDevice] Sending %u bytes text/html\n", result.length());
  request->send(200, F("text/html"), result);
}

void handleZigbeeAction(AsyncWebServerRequest *request)
{
  // Récupérer les paramètres avec vérification
  int ShortAddr = 0;
  int command = 0; 
  int endpoint = 0;
  String tmpValue = "";
  int cluster = 0;           // NOUVEAU
  uint16_t mfrCode = 0;      // NOUVEAU
  
  // Vérification et conversion sécurisée
  if (request->hasArg("0") || request->args() > 0) {
      ShortAddr = request->arg(static_cast<size_t>(0)).toInt();
  }
  
  if (request->hasArg("1") || request->args() > 1) {
      command = request->arg(1).toInt();
  }
  
  if (request->hasArg("2") || request->args() > 2) {
      endpoint = request->arg(2).toInt();
  }
  
  if (request->hasArg("3") || request->args() > 3) {
      tmpValue = request->arg(3);
  }
  
  // NOUVEAU: Paramètres optionnels pour commandes cluster-specific
  if (request->args() > 4) {
      cluster = request->arg(4).toInt();
  }
  
  if (request->args() > 5) {
      mfrCode = (uint16_t)request->arg(5).toInt();
  }
  
  // Appeler la version étendue si cluster est spécifié
  if (cluster != 0) {
      SendActionEx(command, ShortAddr, endpoint, cluster, mfrCode, tmpValue);
  } else {
      SendAction(command, ShortAddr, endpoint, tmpValue);
  }

  request->send(200, F("text/html"), "");
}

void handleZigbeeWriteattribut(AsyncWebServerRequest *request)
{

  // Vérifier les paramètres requis
    if (!request->hasParam(F("addr")) || !request->hasParam(F("cluster")) || 
        !request->hasParam(F("attr")) || !request->hasParam(F("value"))) {
        request->send(400, F("application/json"), F("{\"success\":false,\"error\":\"Paramètres manquants\"}"));
        return;
    }
  
  uint16_t shortAddr = request->getParam(F("addr"))->value().toInt();
    uint8_t endpoint = 1;
    if (request->hasParam(F("endpoint"))) {
        endpoint = request->getParam(F("endpoint"))->value().toInt();
    }
    uint16_t cluster = request->getParam(F("cluster"))->value().toInt();
    uint16_t attribute = request->getParam(F("attr"))->value().toInt();
    uint8_t type = request->getParam(F("type"))->value().toInt();
    String valueStr = request->getParam(F("value"))->value();

    Serial.printf("ZigbeeWrite: addr=%04X ep=%d cluster=%04X attr=%04X type=%02X value=%s\n",
                  shortAddr, endpoint, cluster, attribute, type, valueStr.c_str());
  // ====== DÉTECTION TUYA ======
    if (cluster == 0xEF00) {
        uint8_t dpId = (uint8_t)attribute;
        uint8_t dpType = type;
        uint32_t value = (uint32_t)valueStr.toInt();

        Serial.printf("Tuya Write: DP%d type=%d value=%lu\n", dpId, dpType, value);
        sendTuyaDatapointSet(shortAddr, endpoint, dpId, dpType, value);

        request->send(200, F("application/json"), F("{\"success\":true,\"tuya\":true}"));
        return;
    }

  SendAttributeWrite(shortAddr, endpoint, cluster, attribute, type, valueStr.toInt());


  bool success = true; // Placeholder
    
    if (success) {
        request->send(200, F("application/json"), F("{\"success\":true}"));
    } else {
        request->send(500, F("application/json"), F("{\"success\":false,\"error\":\"Echec écriture Zigbee\"}"));
    }
}

void handleZigbeeReadattribut(AsyncWebServerRequest *request)
{
  // Vérifier les paramètres requis
  if (!request->hasParam(F("addr")) || !request->hasParam(F("endpoint")) ||
      !request->hasParam(F("cluster")) || !request->hasParam(F("attr"))) {
      request->send(400, F("application/json"), F("{\"success\":false,\"error\":\"Paramètres manquants\"}"));
      return;
  }

  uint16_t shortAddr = request->getParam(F("addr"))->value().toInt();
  uint8_t endpoint = request->getParam(F("endpoint"))->value().toInt();
  uint16_t cluster = request->getParam(F("cluster"))->value().toInt();
  uint16_t attribute = request->getParam(F("attr"))->value().toInt();
  // Récupérer manufacturer code (optionnel)
  uint16_t mfrCode = 0;
  if (request->hasParam("mfr")) {
    mfrCode = request->getParam("mfr")->value().toInt();
  }
  Serial.printf("ZigbeeRead: addr=%04X endpoint=%d cluster=%04X attr=%04X\n",
                shortAddr, endpoint, cluster, attribute);

  if (cluster == 0xEF00) {
      Serial.printf("Tuya Query: DP%d\n", attribute);
      sendTuyaDatapointQuery(shortAddr, endpoint);
      
      request->send(200, F("application/json"), 
          F("{\"success\":true,\"tuya\":true,\"note\":\"Query sent\"}"));
      return;
  }

  SendAttributeRead(shortAddr, endpoint, cluster, attribute, mfrCode);

  request->send(200, F("application/json"), F("{\"success\":true}"));
}

void handleZigbeeSendRequest(AsyncWebServerRequest *request)
{

  int ShortAddr,endpoint, cluster, attribute;
  int i = 0;
  ShortAddr = request->arg(i).toInt();
  endpoint = request->arg(1).toInt();
  cluster = request->arg(2).toInt();
  attribute = request->arg(3).toInt();
  SendAttributeRead(ShortAddr,endpoint, cluster, attribute);

  request->send(200, F("text/html"), "");
}

void handleLoadPowerGaugeAbo(AsyncWebServerRequest *request)
{

  String IEEE, Attribute, Time, result;
  int i = 0;
  IEEE = request->arg(i);
  Attribute = request->arg(1);
  Time = request->arg(2);
  result = getPowerGaugeAbo(IEEE, Attribute, Time);

  request->send(200, F("text/html"), result);
}

void handleLoadGaugeDashboard(AsyncWebServerRequest *request)
{

  String IEEE, Cluster, Attribute, Type, Coefficient, result;
  int i = 0;
  IEEE = request->arg(i);
  Cluster = request->arg(1);
  Attribute = request->arg(2);
  Type = request->arg(3);
  Coefficient = request->arg(4);
  result = GetValueStatus(IEEE, Cluster.toInt(), Attribute.toInt(), (String)Type, Coefficient.toFloat());

  request->send(200, F("text/html"), result);
}

void handleLoadPowerGaugeTimeDay(AsyncWebServerRequest *request)
{

  String IEEE, Attribute, result;
  int i = 0;
  IEEE = request->arg(i);
  Attribute = request->arg(1);

  result = getPowerGaugeTimeDay(IEEE, Attribute);

  request->send(200, F("text/html"), result);
}

void handleLoadLinkyDatas(AsyncWebServerRequest *request)
{

  String IEEE, result;
  int i = 0;
  IEEE = request->arg(i);

  //result = getLinkyDatas(IEEE);

  request->send(200, F("text/html"), getLinkyDatas(IEEE).c_str());
}

void handleRefreshLabel(AsyncWebServerRequest *request)
{

  String file,Shortaddr,Cluster, Attribute, Type, Coefficient, Unit, result;
  int i = 0;
  file = request->arg(i);
  Cluster = request->arg(1);
  Attribute = request->arg(2);
  Type = request->arg(3);
  Coefficient = request->arg(4);
  Unit = request->arg(5);

  result = GetValueStatus(file, Cluster.toInt(), Attribute.toInt(), Type, Coefficient.toFloat());

  request->send(200, F("text/html"), result);
}

void handleRefreshGaugeAbo(AsyncWebServerRequest *request)
{

  String IEEE, Attribute, Time, result;
  int i = 0;
  IEEE = request->arg(i);
  Attribute = request->arg(1);
  Time = request->arg(2);

  result = getLastValuePower(IEEE, Attribute, Time);

  request->send(200, F("text/html"), result);
}
void handleLoadPowerTrend(AsyncWebServerRequest *request)
{

  String IEEE, Attribute, Time;
  
  int i = 0;
  IEEE = request->arg(i);
  Attribute = request->arg(1);
  Time = request->arg(2);

  //result = getTrendPower(IEEE, Attribute, Time);

  request->send(200, F("text/html"), getTrendPower(IEEE, Attribute, Time).c_str());
}

void handleLoadDatasTrend(AsyncWebServerRequest *request)
{

  String IEEE, Attribute, Time;
  
  int i = 0;
  IEEE = request->arg(i);
  Attribute = request->arg(1);
  Time = request->arg(2);

  //result = getDatasPower(IEEE, Attribute, Time);

  request->send(200, F("text/html"), getDatasPower(IEEE, Attribute, Time).c_str());
}

void handleLoadTotalEnergy(AsyncWebServerRequest *request)
{

  String IEEE, Attribute, Time, result;
  int i = 0;
  IEEE = request->arg(i);
  Time = request->arg(1);

  result = getTotalEnergy(IEEE, Time);

  request->send(200, F("text/html"), result);
}

String getTariffName(int attrId) {
    switch (attrId) {
        case 256:  return "BASE/HC";        // Base, Heures Creuses, HCJB, EJPHN
        case 258:  return "HP";             // Heures Pleines, HPJB, EJPHPM
        case 260:  return "HC Blanc";       // HCJW (Tempo)
        case 262:  return "HP Blanc";       // HPJW (Tempo)
        case 264:  return "HC Rouge";       // HCJR (Tempo)
        case 266:  return "HP Rouge";       // HPJR (Tempo)
        case 268:  return "EASF07";
        case 270:  return "EASF08";
        case 272:  return "EASF09";
        case 274:  return "EASF10";
        default:   return "Index " + String(attrId);
    }
}

// Couleur associée à chaque tarif
String getTariffColor(int attrId) {
    switch (attrId) {
        case 256:  return "#2980b9";   // Bleu - BASE/HC
        case 258:  return "#154360";   // Bleu foncé - HP
        case 260:  return "#bdc3c7";   // Gris clair - HC Blanc
        case 262:  return "#7f8c8d";   // Gris - HP Blanc
        case 264:  return "#e74c3c";   // Rouge clair - HC Rouge
        case 266:  return "#c0392b";   // Rouge foncé - HP Rouge
        case 268:  return "#f39c12";   // Orange
        case 270:  return "#d35400";   // Orange foncé
        case 272:  return "#8e44ad";   // Violet
        case 274:  return "#6c3483";   // Violet foncé
        default:   return "#95a5a6";   // Gris par défaut
    }
}

void handleLoadDistribChart(AsyncWebServerRequest *request)
{
  String type = "";
  String time = request->arg(static_cast<size_t>(0));
  if (request->args() > 1) {
    type = request->arg(static_cast<size_t>(1));
  }

  // Trouver le device ZLinky
  DeviceData* devZLinky = nullptr;
  for (auto* d : devices) {
    if (d == nullptr) continue;
    try {
      if (d->getDeviceID() == ConfigGeneral.ZLinky) {
        devZLinky = d;
        break;
      }
    } catch (...) {
      continue;
    }
  }
  if (!devZLinky) {
    return request->send(404, "application/json", "[]");
  }

  // Sélectionner la période pour ZLinky
  DeviceEnergyHistory& ehZLinky = devZLinky->energyHistory;
  PeriodData* pdZLinky = nullptr;
  if      (time == "hour")  pdZLinky = &ehZLinky.hours;
  else if (time == "day")   pdZLinky = &ehZLinky.days;
  else if (time == "month") pdZLinky = &ehZLinky.months;
  else if (time == "year")  pdZLinky = &ehZLinky.years;
  else return request->send(400, "application/json", "[]");

  int arrayLength = sizeof(section) / sizeof(section[0]);

  // ============================================
  // 1. INDEX TARIFAIRES ZLINKY (bruts)
  // ============================================
  std::map<int, long> indexSums;

  for (auto &kv : pdZLinky->graph) {
    ValueMap &vm = kv.second;
    for (size_t i = 2; i < arrayLength; ++i) {
      int attrId = section[i].toInt();
      auto itv = vm.attributes.find(attrId);
      if (itv != vm.attributes.end()) {
        indexSums[attrId] += itv->second;
      }
    }
  }

  // ============================================
  // 2. SOUS-COMPTEURS - Calculer d'abord pour soustraire
  // ============================================
  struct SubMeterResult {
    String alias;
    String color;
    long totalWh;
    float totalEuros;
    std::map<int, long> perTariff;  // Répartition par index tarifaire
  };
  std::vector<SubMeterResult> subMeters;

  for (int i = 0; i < ConfigGeneral.subMeterCount; i++) {
    if (!ConfigGeneral.subMeters[i].enabled) continue;
    if (strlen(ConfigGeneral.subMeters[i].IEEE) == 0) continue;

    DeviceData* devSub = nullptr;
    for (auto* d : devices) {
      if (d == nullptr) continue;
      try {
        if (d->getDeviceID() == ConfigGeneral.subMeters[i].IEEE) {
          devSub = d;
          break;
        }
      } catch (...) {
        continue;
      }
    }
    if (!devSub) continue;

    DeviceEnergyHistory& ehSub = devSub->energyHistory;
    PeriodData* pdSub = nullptr;
    if      (time == "hour")  pdSub = &ehSub.hours;
    else if (time == "day")   pdSub = &ehSub.days;
    else if (time == "month") pdSub = &ehSub.months;
    else if (time == "year")  pdSub = &ehSub.years;

    if (!pdSub) continue;

    SubMeterResult result;
    result.alias = String(ConfigGeneral.subMeters[i].alias);
    result.color = String(ConfigGeneral.subMeters[i].color);
    result.totalWh = 0;
    result.totalEuros = 0;

    float coef = devSub->GetAttributeCoefficient(0x0702, 0);

    for (auto &kv : pdSub->graph) {
      ValueMap &vm = kv.second;
      for (size_t j = 2; j < arrayLength; ++j) {
        int attrId = section[j].toInt();
        auto itv = vm.attributes.find(attrId);
        if (itv != vm.attributes.end()) {
          long correctedValue = (long)(itv->second * coef);
          result.totalWh += correctedValue;
          result.totalEuros += correctedValue * getTarif(attrId, "energy") / 1000;
          result.perTariff[attrId] += correctedValue;
        }
      }
    }

    if (result.totalWh > 0) {
      subMeters.push_back(result);

      // *** SOUSTRACTION DES SOUS-COMPTEURS DES INDEX TARIFAIRES ***
      for (auto &pt : result.perTariff) {
        if (indexSums.find(pt.first) != indexSums.end()) {
          indexSums[pt.first] -= pt.second;
          if (indexSums[pt.first] < 0) indexSums[pt.first] = 0;
        }
      }
    }
  }

  // ============================================
  // 3. CONSTRUIRE LE JSON
  // ============================================
  String json = "[";
  bool first = true;

  // 3a. Index tarifaires (maintenant ajustés)
  for (auto &p : indexSums) {
    if (p.second <= 0) continue;

    int attrId = p.first;
    long value = p.second;

    if (!first) json += ",";
    first = false;

    json += "{\"label\":\"" + getTariffName(attrId) + "\",\"value\":";
    if (type == "euro") {
      json += String(value * getTarif(attrId, "energy") / 1000);
      json += ",\"unit\":\"€\"";
    } else {
      json += String(value);
      json += ",\"unit\":\"Wh\"";
    }
    json += ",\"color\":\"" + getTariffColor(attrId) + "\"";
    json += "}";
  }

  // 3b. Sous-compteurs
  for (auto &sub : subMeters) {
    if (!first) json += ",";
    first = false;

    json += "{\"label\":\"" + sub.alias + "\",\"value\":";
    if (type == "euro") {
      json += String(sub.totalEuros);
      json += ",\"unit\":\"€\"";
    } else {
      json += String(sub.totalWh);
      json += ",\"unit\":\"Wh\"";
    }
    json += ",\"color\":\"" + sub.color + "\"";
    json += "}";
  }

  // ============================================
  // 4. GAZ (si en Wh)
  // ============================================
  if (strcmp(ConfigGeneral.unitGaz, "Wh") == 0 && strcmp(ConfigGeneral.Gaz, "") != 0) {
    DeviceData* devGaz = nullptr;
    for (auto* d : devices) {
      if (d == nullptr) continue;
      try {
        if (d->getDeviceID() == ConfigGeneral.Gaz) {
          devGaz = d;
          break;
        }
      } catch (...) {
        continue;
      }
    }

    if (devGaz) {
      DeviceEnergyHistory& ehGaz = devGaz->energyHistory;
      PeriodData* pdGaz = nullptr;
      if      (time == "hour")  pdGaz = &ehGaz.hours;
      else if (time == "day")   pdGaz = &ehGaz.days;
      else if (time == "month") pdGaz = &ehGaz.months;
      else if (time == "year")  pdGaz = &ehGaz.years;

      long sumGaz = 0;
      if (pdGaz) {
        for (auto &kv : pdGaz->graph) {
          ValueMap &vm = kv.second;
          auto itv = vm.attributes.find(0);
          if (itv != vm.attributes.end()) {
            sumGaz += itv->second * ConfigGeneral.coeffGaz;
          }
        }
      }

      if (sumGaz > 0) {
        if (!first) json += ",";
        first = false;

        json += "{\"label\":\"Gaz\",\"value\":";
        if (type == "euro") {
          json += String(sumGaz * getTarif(0, "gaz") / 1000);
          json += ",\"unit\":\"€\"";
        } else {
          json += String(sumGaz);
          json += ",\"unit\":\"Wh\"";
        }
        json += ",\"color\":\"#e67e22\"";
        json += "}";
      }
    }
  }

  // ============================================
  // 5. PRODUCTION (injection)
  // ============================================
  if (strcmp(ConfigGeneral.Production, "") != 0) {
    // Chercher l'attribut 1 (production) — d'abord dans le ZLinky, sinon dans le device production
    long sumProd = 0;
    PeriodData* pdProd = nullptr;

    // Essayer dans le ZLinky (attribut 1 stocke ici quand meme device)
    DeviceEnergyHistory& ehZL = devZLinky->energyHistory;
    if      (time == "hour")  pdProd = &ehZL.hours;
    else if (time == "day")   pdProd = &ehZL.days;
    else if (time == "month") pdProd = &ehZL.months;
    else if (time == "year")  pdProd = &ehZL.years;

    if (pdProd) {
      for (auto &kv : pdProd->graph) {
        auto itv = kv.second.attributes.find(1);
        if (itv != kv.second.attributes.end()) sumProd += itv->second;
      }
    }

    // Si rien dans le ZLinky, chercher dans le device production separe
    if (sumProd == 0) {
      for (auto* d : devices) {
        if (d == nullptr) continue;
        if (d->getDeviceID() == String(ConfigGeneral.Production) && d != devZLinky) {
          DeviceEnergyHistory& ehProd = d->energyHistory;
          pdProd = nullptr;
          if      (time == "hour")  pdProd = &ehProd.hours;
          else if (time == "day")   pdProd = &ehProd.days;
          else if (time == "month") pdProd = &ehProd.months;
          else if (time == "year")  pdProd = &ehProd.years;
          if (pdProd) {
            for (auto &kv : pdProd->graph) {
              auto itv = kv.second.attributes.find(1);
              if (itv != kv.second.attributes.end()) sumProd += itv->second;
            }
          }
          break;
        }
      }
    }

    if (sumProd != 0) {
      // sumProd est negatif (injection) => -sumProd est positif pour l'affichage
      long absProd = (sumProd < 0) ? -sumProd : sumProd;
      if (!first) json += ",";
      first = false;

      json += "{\"label\":\"Injection\",\"value\":";
      if (type == "euro") {
        json += String(absProd * getTarif(0, "production") / 1000);
        json += ",\"unit\":\"€\"";
      } else {
        json += String(absProd);
        json += ",\"unit\":\"Wh\"";
      }
      json += ",\"color\":\"#27ae60\"";
      json += "}";
    }
  }

  json += "]";
  request->send(200, "application/json", json);
}

void handleLoadPowerChart(AsyncWebServerRequest *request)
{

  String IEEE, Attribute, result;

  int i = 0;
  IEEE = request->arg(i);
  Attribute = request->arg(1);
  
  for (size_t i = 0; i < devices.size(); i++) 
  {
    DeviceData* device = devices[i];
    if (device->getDeviceID() == IEEE)
    {
      String now = String(Hour)+":"+Minute;
#if POWER_CHART_STREAM
      // Serialisation directe dans la reponse : evite la String intermediaire ET la copie
      // que fait request->send(). Le buffer est pre-dimensionne via measureJson() car
      // AsyncResponseStream reallouerait sinon a chaque caractere (PrintWriter d'ArduinoJson
      // n'est pas bufferise, et AsyncResponseStream::write fait resizeAdd() si le buffer est
      // plein) -- ce qui serait bien pire que la String qu'on cherche a supprimer.
      SpiRamJsonDocument *doc = buildPowerChartDoc(device->powerHistory, now);
      if (!doc) { request->send(200, F("application/json"), F("{}")); break; }
      size_t jsonSize = measureJson(*doc);
      AsyncResponseStream *response =
          request->beginResponseStream(F("application/json"), jsonSize + 64);
      serializeJson(*doc, *response);
      delete doc;
      request->send(response);
#else
      request->send(200, F("application/json"), toJson(device->powerHistory,now));
#endif
      break;
    }
  }

}

/*void handleLoadEnergyChart(AsyncWebServerRequest* request) {
  // 1) Récupération des arguments
  String IEEE  = request->arg(static_cast<size_t>(0));
  String time = request->arg(static_cast<size_t>(1));

  // 2) On cherche le DeviceData correspondant
  DeviceData* dev = nullptr;
  for (auto* d : devices) {
    if (d->getDeviceID() == IEEE) {
      dev = d;
      break;
    }
  }
  if (!dev) {
    request->send(404, "application/json", "[]");
    return;
  }

  DeviceEnergyHistory& eh = dev->energyHistory;
  PeriodData* pd = nullptr;

  if (time == "hour") {
    pd         = &eh.hours;
  }
  else if (time == "day") {
    pd         = &eh.days;
  }
  else if (time == "month") {
    pd         = &eh.months;
  }
  else if (time == "year") {
    pd         = &eh.years;
  }
  else {
    request->send(400, "application/json", "[]");
    return;
  }

  String result,sep;
  int cntsection;
  int arrayLength = sizeof(section) / sizeof(section[0]);
  result = F("[");

  if (time == "hour")
  {
    int now = atoi(Hour);
    int i = 0;
    while (i < 24)
    {
      if (i > 0)
      {
        sep = ",";
      }
      now++;
      if (now > 23)
      {
        now = 0;
      }
      String tmpi = now < 10 ? "0" + String(now) : String(now);
      PsString keyPS(tmpi.c_str(), PsramAllocator<char>());
      result += sep + F("{\"y\":\"") + tmpi + F("H\"");
      String sep2 = "";

      auto it = pd->graph.find(keyPS);
      if (it != pd->graph.end()) {
        ValueMap& vm = it->second;
        for (cntsection=0 ; cntsection <arrayLength; cntsection++) {
          int attrId = atoi(section[cntsection].c_str());
          auto itv = vm.attributes.find(attrId);
          if (itv != vm.attributes.end() && itv->second != 0) {
            result += ",";
            result += "\""; result += section[cntsection]; result += "\":";
            result += String(itv->second); 
          }
        }
      }
      result += F("}");
      i++;
    }
  }else if (time == "day")
  {
    int now = atoi(Day);
    int reste = 30 - now;
    int lastNbDayMonth;
    String  m;
    if (atoi(Month)-2>0)
    {
      lastNbDayMonth=maxDayOfTheMonth[(atoi(Month)-2)];
    }else{
      lastNbDayMonth=31;
    }
    if (reste>0)
    {
      now = (lastNbDayMonth - reste)+1;   
    }else if (reste<0){
      now=2;
    }else if (reste==0){
      now=reste+1;
    }

    int i = 0;
    while (i < 30)
    {
      if (i > 0)
      {
        sep = ",";
      }

      if (reste>0)
      {
        if (now > lastNbDayMonth)
        {
          now = 1;
        }
      }
      String tmpi = now < 10 ? "0" + String(now) : String(now);
      PsString keyPS(tmpi.c_str(), PsramAllocator<char>());
      String tmpm;
      if (i>now)
      {
        tmpm = Month;
      }else{
        if (reste<=0)
        {
          tmpm = Month;
        }else{
          if ((atoi(Month)-1)>0)
          {
            tmpm = (atoi(Month)-1) < 10 ? "0" + String((atoi(Month)-1)) : String((atoi(Month)-1));
          }else{
            tmpm = "12";
          }
        }
      }
      //String tmpm = m.toInt() < 10 ? "0" + String(m) : String(m);
      
      result += sep + F("{\"y\":\"") + tmpi + F("/")+ tmpm +F("\"");
      String sep2 = "";
      auto it = pd->graph.find(keyPS);
      if (it != pd->graph.end()) {
        ValueMap& vm = it->second;
        for (cntsection=0 ; cntsection <arrayLength; cntsection++) {
          int attrId = atoi(section[cntsection].c_str());
          auto itv = vm.attributes.find(attrId);
          if (itv != vm.attributes.end() && itv->second != 0) {
            result += ",";
            result += "\""; result += section[cntsection]; result += "\":";
            result += String(itv->second); 
          }
        }
      }
      result += F("}");
      now++;
      i++;
    }
  }else if (time == "month")
  {
    int now = atoi(Month);
    String  y;
    int i = 0;
    while (i < 12)
    {
      if (i > 0)
      {
        sep = ",";
      }
      
      now++;
      if (now > 12)
      {
        now = 1;
      }
      String tmpi = now < 10 ? "0" + String(now) : String(now);
      PsString keyPS(tmpi.c_str(), PsramAllocator<char>());
      if (i<now)
      {
        y = String((atoi(Year)-1));
      }else{
        y = Year;
      }
      result += sep + F("{\"y\":\"") + tmpi + F("/")+ y +F("\"");
      String sep2 = "";
      auto it = pd->graph.find(keyPS);
      if (it != pd->graph.end()) {
        ValueMap& vm = it->second;
        for (cntsection=0 ; cntsection <arrayLength; cntsection++) {
          int attrId = atoi(section[cntsection].c_str());
          auto itv = vm.attributes.find(attrId);
          if (itv != vm.attributes.end() && itv->second != 0) {
            result += ",";
            result += "\""; result += section[cntsection]; result += "\":";
            result += String(itv->second); 
          }
        }
      }
      result += F("}");
      i++;
    }
  }else if (time == "year")
  {
    int now = atoi(Year) - 10;
    int i = 0;
    while (i < 11)
    {
      if (i > 0)
      {
        sep = ",";
      }

      result += sep + F("{\"y\":\"") + String(now) + F("\"");
      PsString keyPS(String(now).c_str(), PsramAllocator<char>());
      String sep2 = "";
      auto it = pd->graph.find(keyPS);
      if (it != pd->graph.end()) {
        ValueMap& vm = it->second;
        for (cntsection=0 ; cntsection <arrayLength; cntsection++) {
          int attrId = atoi(section[cntsection].c_str());
          auto itv = vm.attributes.find(attrId);
          if (itv != vm.attributes.end() && itv->second != 0) {
            result += ",";
            result += "\""; result += section[cntsection]; result += "\":";
            result += String(itv->second); 
          }
        }
      }
      result += F("}");
      now++;
      i++;
    }
  }
  result += F("]");
  // 7) Envoi
  request->send(200, "application/json", result);
}*/

// Helper pour ajouter les données des sous-compteurs au response stream
// Structure pour stocker les données des sous-compteurs par attribut
struct SubMeterValues {
  std::map<int, long> byAttr;  // attrId -> valeur
  std::map<int, long> total;   // subMeterIndex -> total
};

// Calcule les valeurs des sous-compteurs pour un point temporel donné
SubMeterValues getSubMeterValuesForKey(const String& time, const String& keyStr) {
  SubMeterValues result;
  int arrayLength = sizeof(section) / sizeof(section[0]);
  
  for (int sm = 0; sm < ConfigGeneral.subMeterCount; sm++) {
    if (!ConfigGeneral.subMeters[sm].enabled) continue;
    if (strlen(ConfigGeneral.subMeters[sm].IEEE) == 0) continue;
    
    // Trouver le device du sous-compteur
    DeviceData* devSub = nullptr;
    for (auto* d : devices) {
      if (d == nullptr) continue;
      try {
        if (d->getDeviceID() == ConfigGeneral.subMeters[sm].IEEE) {
          devSub = d;
          break;
        }
      } catch (...) {
        continue;
      }
    }
    if (!devSub) continue;
    
    // Sélectionner la période
    DeviceEnergyHistory& ehSub = devSub->energyHistory;
    PeriodData* pdSub = nullptr;
    if      (time == "hour")  pdSub = &ehSub.hours;
    else if (time == "day")   pdSub = &ehSub.days;
    else if (time == "month") pdSub = &ehSub.months;
    else if (time == "year")  pdSub = &ehSub.years;
    
    if (!pdSub) continue;
    
    // Chercher les données pour ce point temporel
    PsString psKey(keyStr.c_str(), PsramAllocator<char>());
    auto it = pdSub->graph.find(psKey);
    if (it != pdSub->graph.end()) {
      ValueMap& vm = it->second;
      
      long smTotal = 0;
      float coef = devSub->GetAttributeCoefficient(0x0702, 0);
      // Parcourir tous les attributs tarifaires
      for (int j = 2; j < arrayLength; j++) {
        int attrId = atoi(section[j].c_str());
        auto itv = vm.attributes.find(attrId);
        if (itv != vm.attributes.end() && itv->second > 0) {
          long correctedValue = (long)(itv->second * coef);
          // Ajouter à la somme par attribut (pour soustraction du ZLinky)
          result.byAttr[attrId] += correctedValue;
          smTotal += correctedValue;
        }
      }
      result.total[sm] = smTotal;
    }
  }
  
  return result;
}

// Écrit les données ZLinky (après soustraction) + sous-compteurs + production
// Version String& — utilisée par handleLoadEnergyChart pour garantir Content-Length
void writeEnergyDataWithSubMeters(String& out,
                                   PeriodData* pdZLinky,
                                   const String& time,
                                   const String& keyStr) {
  int arrayLength = sizeof(section) / sizeof(section[0]);

  // 1. Obtenir les valeurs des sous-compteurs pour ce point temporel
  SubMeterValues subValues = getSubMeterValuesForKey(time, keyStr);

  // 2. Écrire les données ZLinky (avec soustraction des sous-compteurs)
  PsString psKey(keyStr.c_str(), PsramAllocator<char>());
  auto it = pdZLinky->graph.find(psKey);
  if (it != pdZLinky->graph.end()) {
    ValueMap& vm = it->second;
    for (int cntsection = 0; cntsection < arrayLength; cntsection++) {
      int attrId = atoi(section[cntsection].c_str());
      if (attrId == 1) continue;
      auto itv = vm.attributes.find(attrId);
      if (itv != vm.attributes.end() && itv->second != 0) {
        long value = itv->second;
        auto itSub = subValues.byAttr.find(attrId);
        if (itSub != subValues.byAttr.end()) {
          value -= itSub->second;
          if (value < 0) value = 0;
        }
        if (value != 0) {
          out += ",\"";
          out += section[cntsection];
          out += "\":";
          out += String(value);
        }
      }
    }
  }

  // 3. Écrire les données des sous-compteurs
  for (int sm = 0; sm < ConfigGeneral.subMeterCount; sm++) {
    if (!ConfigGeneral.subMeters[sm].enabled) continue;
    auto itTotal = subValues.total.find(sm);
    if (itTotal != subValues.total.end() && itTotal->second > 0) {
      out += ",\"sub_";
      out += String(sm);
      out += "\":";
      out += String(itTotal->second);
    }
  }

  // 4. Écrire les données de Production (device séparé)
  if (strcmp(ConfigGeneral.Production, "") != 0) {
    DeviceData* devProd = nullptr;
    for (auto* d : devices) {
      if (d == nullptr) continue;
      try {
        if (d->getDeviceID() == ConfigGeneral.Production) { devProd = d; break; }
      } catch (...) { continue; }
    }
    if (devProd) {
      DeviceEnergyHistory& ehProd = devProd->energyHistory;
      PeriodData* pdProd = nullptr;
      if      (time == "hour")  pdProd = &ehProd.hours;
      else if (time == "day")   pdProd = &ehProd.days;
      else if (time == "month") pdProd = &ehProd.months;
      else if (time == "year")  pdProd = &ehProd.years;
      if (pdProd) {
        auto itProd = pdProd->graph.find(psKey);
        if (itProd != pdProd->graph.end()) {
          ValueMap& vmProd = itProd->second;
          auto itvProd = vmProd.attributes.find(1);
          if (itvProd != vmProd.attributes.end() && itvProd->second != 0) {
            // La valeur est déjà négative (handleAttribute1 ajoute le signe -)
            out += ",\"1\":";
            out += String(itvProd->second);
          }
        }
      }
    }
    // NB : si l'appareil de production configuré est absent des devices, on ignore
    // silencieusement (pas de Serial.printf par tranche, qui spammait/bloquait la série
    // pendant la construction du graphe).
  }
}

// Ancienne version AsyncResponseStream* conservée pour compatibilité
void writeEnergyDataWithSubMeters(AsyncResponseStream* response,
                                   PeriodData* pdZLinky,
                                   const String& time,
                                   const String& keyStr) {
  String tmp;
  tmp.reserve(512);
  writeEnergyDataWithSubMeters(tmp, pdZLinky, time, keyStr);
  response->print(tmp);
}

// Construit le JSON du graphe Usage d'électricité pour une période donnée.
// Extrait de handleLoadEnergyChart afin d'être réutilisé par l'export CSV
// (garantit que l'export reflète exactement ce que montre le graphe).
String buildEnergyChartJson(PeriodData* pd, const String& time) {
  // Construction dans un String — garantit Content-Length dans la réponse HTTP,
  // ce qui évite le chunked encoding et assure la compatibilité avec le tunnel.
  String result;
  result.reserve(8192);
  result = "[";

  if (time == "hour") {
    int now = atoi(Hour);
    for (int i = 0; i < 24; i++) {
      if (i > 0) result += ",";
      now++;
      if (now > 23) now = 0;
      String tmpi = now < 10 ? "0" + String(now) : String(now);
      result += "{\"y\":\"";
      result += tmpi;
      result += "H\"";
      writeEnergyDataWithSubMeters(result, pd, time, tmpi);
      result += "}";
    }
  }
  else if (time == "day") {
    int now = atoi(Day);
    int reste = 30 - now;
    int lastNbDayMonth = (atoi(Month) - 2 > 0) ? maxDayOfTheMonth[atoi(Month) - 2] : 31;
    if (reste > 0) now = (lastNbDayMonth - reste) + 1;
    else if (reste < 0) now = 2;
    else now = 1;

    for (int i = 0; i < 30; i++) {
      if (i > 0) result += ",";
      if (reste > 0 && now > lastNbDayMonth) now = 1;
      String tmpi = now < 10 ? "0" + String(now) : String(now);
      String tmpm;
      if (i > now) {
        tmpm = Month;
      } else {
        if (reste <= 0) {
          tmpm = Month;
        } else {
          if ((atoi(Month) - 1) > 0) {
            tmpm = (atoi(Month) - 1) < 10 ? "0" + String(atoi(Month) - 1) : String(atoi(Month) - 1);
          } else {
            tmpm = "12";
          }
        }
      }
      result += "{\"y\":\"";
      result += tmpi;
      result += "/";
      result += tmpm;
      result += "\"";
      writeEnergyDataWithSubMeters(result, pd, time, tmpi);
      result += "}";
      now++;
    }
  }
  else if (time == "month") {
    int now = atoi(Month);
    for (int i = 0; i < 12; i++) {
      if (i > 0) result += ",";
      now++;
      if (now > 12) now = 1;
      String tmpi = now < 10 ? "0" + String(now) : String(now);
      String y = (i < now) ? String(atoi(Year) - 1) : Year;
      result += "{\"y\":\"";
      result += tmpi;
      result += "/";
      result += y;
      result += "\"";
      writeEnergyDataWithSubMeters(result, pd, time, tmpi);
      result += "}";
    }
  }
  else if (time == "year") {
    int now = atoi(Year) - 10;
    for (int i = 0; i < 11; i++) {
      if (i > 0) result += ",";
      result += "{\"y\":\"";
      result += String(now);
      result += "\"";
      writeEnergyDataWithSubMeters(result, pd, time, String(now));
      result += "}";
      now++;
    }
  }

  result += "]";
  return result;
}

void handleLoadEnergyChart(AsyncWebServerRequest* request) {
  String IEEE = request->arg(static_cast<size_t>(0));
  String time = request->arg(static_cast<size_t>(1));

  DeviceData* dev = nullptr;
  for (auto* d : devices) {
    if (d->getDeviceID() == IEEE) { dev = d; break; }
  }
  if (!dev) {
    request->send(404, "application/json", "[]");
    return;
  }

  DeviceEnergyHistory& eh = dev->energyHistory;
  PeriodData* pd = nullptr;
  if      (time == "hour")  pd = &eh.hours;
  else if (time == "day")   pd = &eh.days;
  else if (time == "month") pd = &eh.months;
  else if (time == "year")  pd = &eh.years;
  else {
    request->send(400, "application/json", "[]");
    return;
  }

  esp_task_wdt_reset();
  String result = buildEnergyChartJson(pd, time);
  esp_task_wdt_reset();

  request->send(200, "application/json", result);
}

// ============================================================
// EXPORT CSV (format Excel FR : séparateur ';', BOM UTF-8)
// ============================================================

// Échappe un champ CSV : entoure de guillemets si nécessaire.
static String csvEscape(const String& f) {
  if (f.indexOf(';') < 0 && f.indexOf('"') < 0 &&
      f.indexOf('\n') < 0 && f.indexOf('\r') < 0) {
    return f;
  }
  String r = f;
  r.replace("\"", "\"\"");
  return "\"" + r + "\"";
}

// Formate un nombre décimal au format français (virgule décimale) pour Excel FR.
static String csvNum(double v, int dec) {
  String s = String(v, dec);
  s.replace(".", ",");
  return s;
}

// Export CSV du graphe "Puissance apparente" (historique du jour, pas HH:MM)
void handleExportPowerChart(AsyncWebServerRequest* request) {
  String IEEE = request->arg(static_cast<size_t>(0));

  DeviceData* dev = nullptr;
  for (auto* d : devices) {
    if (d->getDeviceID() == IEEE) { dev = d; break; }
  }
  if (!dev) { request->send(404, "text/plain", "Device introuvable"); return; }

  bool isTriphase = (ConfigGeneral.LinkyMode == 2) ||
                    (ConfigGeneral.LinkyMode == 3) ||
                    (ConfigGeneral.LinkyMode == 7);

  // Colonnes ordonnées : id d'attribut -> intitulé lisible
  std::vector<std::pair<int, String>> cols;
  if (isTriphase) {
    cols.push_back({1295, "Puissance Ph1 (VA)"});
    cols.push_back({2319, "Puissance Ph2 (VA)"});
    cols.push_back({2575, "Puissance Ph3 (VA)"});
    cols.push_back({1,    "Injection Ph1 (VA)"});
    cols.push_back({2,    "Injection Ph2 (VA)"});
    cols.push_back({3,    "Injection Ph3 (VA)"});
  } else {
    cols.push_back({1295, "Puissance (VA)"});
    cols.push_back({1,    "Injection (VA)"});
  }

  // Limite de puissance souscrite (goal) — même calcul que createPowerGraph
  long goal = 0;
  if ((ConfigGeneral.LinkyMode == 0) || (ConfigGeneral.LinkyMode == 2)) {
    goal = strtol(dev->getValue("0B01", "13").c_str(), 0, 16) * 200;
  } else {
    goal = strtol(dev->getValue("0B01", "14").c_str(), 0, 16) * 1000;
  }

  // Trier les relevés par horodatage (HH:MM, déjà zéro-paddé => ordre lexicographique)
  PowerHistory& ph = dev->powerHistory;
  std::vector<const DataRecord*> sorted;
  sorted.reserve(ph.datas.size());
  for (auto& rec : ph.datas) sorted.push_back(&rec);
  std::sort(sorted.begin(), sorted.end(),
            [](const DataRecord* a, const DataRecord* b) { return a->timeStamp < b->timeStamp; });

  // Rotation IDENTIQUE à toJson() (powerHistory.cpp) : le relevé de l'heure courante doit
  // finir en DERNIER. L'historique est un buffer glissant sur 24 h indexé par HH:MM : les
  // relevés postérieurs à l'heure courante datent d'HIER. Sans cette rotation, le CSV sort
  // 00:00->23:59 en mélangeant hier et aujourd'hui, et ne correspond pas au graphe.
  String nowHM = String(Hour) + ":" + Minute;
  std::vector<const DataRecord*> pivoted;
  pivoted.reserve(sorted.size());
  if (!sorted.empty()) {
    size_t idx = 0;
    while (idx < sorted.size() && String(sorted[idx]->timeStamp.c_str()) != nowHM) ++idx;
    if (idx < sorted.size()) idx = (idx + 1) % sorted.size();   // démarrer juste après 'now'
    for (size_t k = 0; k < sorted.size(); ++k)
      pivoted.push_back(sorted[(idx + k) % sorted.size()]);
  } else {
    pivoted = sorted;
  }

  String csv;
  csv.reserve(96 + sorted.size() * 80);
  csv = "\xEF\xBB\xBF";  // BOM UTF-8
  csv += "Heure";
  for (auto& c : cols) { csv += ";"; csv += csvEscape(c.second); }
  // Colonnes calculées (données du tooltip)
  csv += ";Consommation totale (VA);Injection totale (VA);Puissance nette (VA)";
  if (goal > 0) csv += ";Utilisation limite (%);Marge limite (VA)";
  csv += "\r\n";

  for (auto* rec : pivoted) {
    csv += csvEscape(String(rec->timeStamp.c_str()));
    for (auto& c : cols) {
      csv += ";";
      auto it = rec->values.find(c.first);
      if (it != rec->values.end()) csv += String(it->second);
    }

    // Agrégats du tooltip : consommation = part positive des puissances ;
    // injection = valeur absolue de la part négative des attributs d'injection.
    long conso = 0, inj = 0;
    for (auto& c : cols) {
      auto it = rec->values.find(c.first);
      if (it == rec->values.end()) continue;
      long v = it->second;
      if (c.first == 1295 || c.first == 2319 || c.first == 2575) {
        if (v > 0) conso += v;
      } else if (c.first == 1 || c.first == 2 || c.first == 3) {
        if (v < 0) inj += -v;
      }
    }
    csv += ";"; csv += String(conso);
    csv += ";"; csv += String(inj);
    csv += ";"; csv += String(conso - inj);
    if (goal > 0) {
      csv += ";"; csv += csvNum((double)conso / goal * 100.0, 1);
      csv += ";"; csv += String(goal - conso);
    }
    csv += "\r\n";
  }

  String fname = "puissance_apparente_" + String(Year) + "-" + String(Month) + "-" + String(Day) + ".csv";
  AsyncWebServerResponse* response = request->beginResponse(200, "text/csv; charset=utf-8", csv);
  response->addHeader("Content-Disposition", "attachment; filename=\"" + fname + "\"");
  request->send(response);
}

// Export CSV du graphe "Usage d'électricité" (réutilise buildEnergyChartJson)
void handleExportEnergyChart(AsyncWebServerRequest* request) {
  String IEEE = request->arg(static_cast<size_t>(0));
  String time = request->arg(static_cast<size_t>(1));

  DeviceData* dev = nullptr;
  for (auto* d : devices) {
    if (d->getDeviceID() == IEEE) { dev = d; break; }
  }
  if (!dev) { request->send(404, "text/plain", "Device introuvable"); return; }

  DeviceEnergyHistory& eh = dev->energyHistory;
  PeriodData* pd = nullptr;
  if      (time == "hour")  pd = &eh.hours;
  else if (time == "day")   pd = &eh.days;
  else if (time == "month") pd = &eh.months;
  else if (time == "year")  pd = &eh.years;
  else { request->send(400, "text/plain", "Periode invalide"); return; }

  esp_task_wdt_reset();
  String json = buildEnergyChartJson(pd, time);

  SpiRamJsonDocument doc(MAXHEAP);
  DeserializationError err = deserializeJson(doc, json);
  if (err) { request->send(500, "text/plain", "Erreur interne"); return; }
  JsonArray arr = doc.as<JsonArray>();

  // Colonnes candidates : sections tarifaires (hors "0" et "1"), sous-compteurs, production
  std::vector<std::pair<String, String>> cols;  // clé JSON -> intitulé lisible
  int arrayLength = sizeof(section) / sizeof(section[0]);
  for (int i = 0; i < arrayLength; i++) {
    if (section[i] == "0" || section[i] == "1") continue;
    int sId = section[i].toInt();
    String nm = GetNameStatus(97, "0702", sId, "ZLinky_TIC");
    if (nm == "") nm = section[i];
    cols.push_back({section[i], nm + " (Wh)"});
  }
  for (int sm = 0; sm < ConfigGeneral.subMeterCount; sm++) {
    if (!ConfigGeneral.subMeters[sm].enabled) continue;
    String nm = String(ConfigGeneral.subMeters[sm].alias);
    if (nm == "") nm = "Sous-compteur " + String(sm);
    cols.push_back({"sub_" + String(sm), nm + " (Wh)"});
  }
  if (strcmp(ConfigGeneral.Production, "") != 0) {
    cols.push_back({"1", "Production (Wh)"});
  }

  // Ne garder que les colonnes effectivement renseignées
  std::vector<bool> used(cols.size(), false);
  for (JsonObject row : arr) {
    for (size_t c = 0; c < cols.size(); c++) {
      if (row.containsKey(cols[c].first.c_str())) {
        long v = row[cols[c].first.c_str()] | 0L;
        if (v != 0) used[c] = true;
      }
    }
  }

  // Tarifs pour reproduire le calcul de coût du tooltip
  bool hasProd = (strcmp(ConfigGeneral.Production, "") != 0);
  double cspe = atof(ConfigGeneral.tarifCSPE);
  double cta  = atof(ConfigGeneral.tarifCTA);
  double abo  = atof(ConfigGeneral.tarifAbo);
  double prixSection256 = getTarif(256, "energy");  // tarif des sous-compteurs (comme le tooltip)
  double prixProd = getTarif(1, "production");
  double aboFactor;  // proratisation de l'abonnement/CTA selon la période
  if      (time == "hour")  aboFactor = 1.0 / (30.0 * 24.0);
  else if (time == "day")   aboFactor = 1.0 / 30.0;
  else if (time == "month") aboFactor = 1.0;
  else                      aboFactor = 12.0;  // year

  String csv;
  csv.reserve(4096);
  csv = "\xEF\xBB\xBF";  // BOM UTF-8
  csv += "Periode";
  for (size_t c = 0; c < cols.size(); c++) {
    if (used[c]) { csv += ";"; csv += csvEscape(cols[c].second); }
  }
  // Colonnes calculées (données du tooltip)
  csv += ";Consommation totale (Wh);Conso - Energie (EUR);Conso - Abonnement (EUR);Conso - Taxes (EUR);Conso - Cout total (EUR)";
  if (hasProd) csv += ";Production totale (Wh);Production - Revenu (EUR);Net (Wh);Facture nette (EUR)";
  csv += "\r\n";

  for (JsonObject row : arr) {
    String y = row["y"] | "";
    csv += csvEscape(y);
    for (size_t c = 0; c < cols.size(); c++) {
      if (!used[c]) continue;
      csv += ";";
      if (row.containsKey(cols[c].first.c_str())) {
        long v = row[cols[c].first.c_str()] | 0L;
        csv += String(v);
      }
    }

    // Agrégats du tooltip : coût de consommation décomposé + production + net
    long totalConso = 0, totalProd = 0;
    double consoEnergy = 0, consoTax = 0, prodEnergy = 0;
    for (size_t c = 0; c < cols.size(); c++) {
      if (!row.containsKey(cols[c].first.c_str())) continue;
      long v = row[cols[c].first.c_str()] | 0L;
      const String& key = cols[c].first;
      if (key.startsWith("sub_")) {
        if (v > 0) { totalConso += v; consoEnergy += v / 1000.0 * prixSection256; }
      } else if (key == "1") {
        long a = (v < 0) ? -v : v;
        totalProd += a;
        prodEnergy += a / 1000.0 * prixProd;
      } else {
        if (v > 0) {
          int attrId = key.toInt();
          totalConso += v;
          consoEnergy += v / 1000.0 * getTarif(attrId, "energy");
          consoTax += v / 1000.0 * cspe;
        }
      }
    }
    double subscription = 0;
    if (totalConso > 0) { subscription = abo * aboFactor; consoTax += cta * aboFactor; }
    double consoCost = consoEnergy + subscription + consoTax;

    csv += ";"; csv += String(totalConso);
    csv += ";"; csv += csvNum(consoEnergy, 2);
    csv += ";"; csv += csvNum(subscription, 2);
    csv += ";"; csv += csvNum(consoTax, 2);
    csv += ";"; csv += csvNum(consoCost, 2);
    if (hasProd) {
      csv += ";"; csv += String(totalProd);
      csv += ";"; csv += csvNum(prodEnergy, 2);
      csv += ";"; csv += String(totalConso - totalProd);
      csv += ";"; csv += csvNum(consoCost - prodEnergy, 2);
    }
    csv += "\r\n";
  }

  String fname = "usage_electricite_" + time + "_" + String(Year) + "-" + String(Month) + "-" + String(Day) + ".csv";
  AsyncWebServerResponse* response = request->beginResponse(200, "text/csv; charset=utf-8", csv);
  response->addHeader("Content-Disposition", "attachment; filename=\"" + fname + "\"");
  request->send(response);
}

void handleLoadLabelEnergy(AsyncWebServerRequest *request)
{

  String IEEE, datas, result;
  int i = 0;
  IEEE = request->arg(i);
  datas = request->arg(1);

  result = datas;

  request->send(200, F("text/html"), result);
}

void handleGetAlert(AsyncWebServerRequest *request)
{

  String result = "";

  if (!alertList->isEmpty())
  {
    Alert a = alertList->shift();
    result = String(a.state);
    result += F(";");
    result += a.message;
    DEBUG_PRINTLN(result);
  }

  request->send(200, F("text/html"), result);
}


void handleGetPresenceSummary(AsyncWebServerRequest *request) {
    String dayNum;
    String deviceId = "all";
    bool sliding = false;
    
    // Nouveau paramètre sliding pour 24h glissantes
    if (request->hasParam("sliding")) {
        String slidingVal = request->getParam("sliding")->value();
        sliding = (slidingVal == "true" || slidingVal == "1");
        log_d("Presence sliding mode: %s -> %d", slidingVal.c_str(), sliding);
    }
    
    if (request->hasParam("day")) {
        dayNum = request->getParam("day")->value();
    } else {
        dayNum = String(atoi(Day));
    }
    
    if (request->hasParam("device")) {
        deviceId = request->getParam("device")->value();
    }
    
    String result;
    
    if (sliding) {
        // Mode 24h glissantes - fusionne hier + aujourd'hui
        log_d("Using 24h sliding mode");
        if (deviceId == "all") {
            result = getPresenceSummary24hSliding();
        } else {
            result = getPresenceSummary24hSliding(deviceId);
        }
    } else {
        // Mode classique - un seul jour
        if (deviceId == "all") {
            result = getPresenceSummaryAll(dayNum);
        } else {
            result = getPresenceSummary(deviceId, dayNum);
        }
    }
    
    request->send(200, F("application/json"), result);
}

void handleGetPresenceHistory(AsyncWebServerRequest *request) {
    if (!request->hasParam("device")) {
        request->send(400, F("application/json"), F("{\"error\":\"Missing device parameter\"}"));
        return;
    }
    
    String deviceId = request->getParam("device")->value();
    String result = getPresenceHistory(deviceId);
    request->send(200, F("application/json"), result);
}

void handleGetPresenceStatus(AsyncWebServerRequest *request) {
    String deviceId = "all";
    
    if (request->hasParam("device")) {
        deviceId = request->getParam("device")->value();
    }
    
    bool status = getCurrentPresenceStatus(deviceId);
    String result = status ? "{\"presence\":true}" : "{\"presence\":false}";
    request->send(200, F("application/json"), result);
}

void handleGetPresenceAll(AsyncWebServerRequest *request) {
    String result = getPresenceAll();
    request->send(200, F("application/json"), result);
}




void handleGetRuleStatus(AsyncWebServerRequest *request)
{

  int i = 0;
  String name = request->arg(i);
  int value =rulesManager.getStatusRule(name.c_str());
  String lastDate = rulesManager.getLastDateRule(name.c_str()).c_str();
  String result = String(value)+"|"+lastDate;

  request->send(200, F("text/html"), result);
}

void handleGetDeviceValue(AsyncWebServerRequest *request)
{

  String result = "";
  if (!deviceList->isEmpty())
  {
    int i=0;
    result="[";
    while (!deviceList->isEmpty())
    {
      Device d = deviceList->shift();
      if (i>0){result+=",";}
      result += "\""+String(d.shortAddr)+"_"+String(d.cluster)+"_"+String(d.attribute);
      result += F(";");
      result += d.value+"\"";
      i++;
    }
    result +="]";
    
  }
  request->send(200, F("text/html"), result);
}

// ============================================================
// Récupère les valeurs des attributs d'un appareil spécifique
// Paramètre: id (MAC address de l'appareil)
// Retourne: JSON avec shortAddr_cluster_attribute: value
// ============================================================
void handleGetDeviceAttrValues(AsyncWebServerRequest *request)
{
  if (!request->hasParam("id")) {
    request->send(400, F("application/json"), F("{\"error\":\"Missing id parameter\"}"));
    return;
  }
  
  String deviceID = request->getParam("id")->value();
  
  // Rechercher le device
  DeviceData* device = nullptr;
  for (size_t i = 0; i < devices.size(); i++) {
    if (devices[i]->getDeviceID() == deviceID) {
      device = devices[i];
      break;
    }
  }
  
  if (device == nullptr) {
    request->send(404, F("application/json"), F("{\"error\":\"Device not found\"}"));
    return;
  }
  
  String result = "{";
  TemplateData* t = device->getTemplate();
  int shortAddr = device->getInfo().shortAddr.toInt();
  bool isZLinky = (device->getInfo().model == "ZLinky_TIC");
  int linkyMode = device->getInfo().linkyMode.toInt();

  if (t != nullptr) {
    bool first = true;
    for (int i = 0; i < t->StateSize(); i++) {
      // Vérifier si visible
      if (!t->states[i].visible) continue;
      
      // Filtrage par mode Linky pour ZLinky_TIC
      if (isZLinky) {
        bool afficheOK = false;
        const char *tmp = t->states[i].mode;
        
        if ((tmp != NULL) && (tmp[0] != '\0')) {
          char modeCopy[50];
          strncpy(modeCopy, tmp, sizeof(modeCopy) - 1);
          modeCopy[sizeof(modeCopy) - 1] = '\0';
          
          char *pch = strtok(modeCopy, ";");
          while (pch != NULL) {
            if (atoi(pch) == linkyMode) {
              afficheOK = true;
              break;
            }
            pch = strtok(NULL, ";");
          }
        } else {
          afficheOK = true;
        }
        
        if (!afficheOK) continue;
      }
      
      String attrId = String(shortAddr) + "_" + String(t->states[i].cluster) + "_" + String(t->states[i].attribute);
      String value = GetValueStatus(device->getDeviceID(), t->states[i].cluster, t->states[i].attribute,
                                     (String)t->states[i].type, t->states[i].coefficient);

      if (!first) result += ",";
      first = false;

      result += "\"" + attrId + "\":\"" + value + "\"";
    }
  }
  
  result += "}";
  request->send(200, F("application/json"), result);
}

void handleSendMqttDiscover(AsyncWebServerRequest *request)
{
  String IEEE, ShortAddr, datas, result;
  int i = 0;
  ShortAddr = request->arg(i);
  // Les appareils LoRa n'ont pas d'adresse courte Zigbee : leur fiche passe directement la
  // MAC (16 caractères hex) et l'adresse courte se relit depuis la base. Sinon, résolution
  // classique depuis l'adresse courte.
  int shortAddrInt;
  if (ShortAddr.length() == 16) {
    IEEE = ShortAddr;
    shortAddrInt = GetShortAddr(IEEE + ".json");
  } else {
    shortAddrInt = ShortAddr.toInt();
    IEEE = GetMacAdrr(shortAddrInt);
    IEEE = IEEE.substring(0, 16);
  }
  String model;
  model = GetModel(IEEE + ".json");

  File DeviceFile = LittleFS.open("/db/" + IEEE + ".json", "r");

  if (!DeviceFile || DeviceFile.isDirectory())
  {
    DEBUG_PRINTLN(F("failed open"));
  }
  else
  {
    int DeviceId = GetDeviceId(IEEE + ".json");
    if (TemplateExist(DeviceId))
    {
      DeviceData *device = nullptr;
      for (size_t i = 0; i < devices.size(); i++)
      {
        device = devices[i];
        if (device->getDeviceID() == IEEE)
        {
          break;
        }
      }

      TemplateData *t = device->getTemplate();
      if (!t)
      {
        Serial.printf("WARNING: Template introuvable pour model: %s\n", model.c_str());
        DeviceFile.close();
        request->send(200, F("text/html"), "");
        return;
      }

      // ========== DISCOVERY DES STATES (sensors) - CODE EXISTANT ==========
      for (int i = 0; i < t->StateSize(); i++)
      {
        if (strlen(t->states[i].mqtt_icon) > 0)
        {
          const char *PROGMEM HA_discovery_msg = "{"
                                                  "\"name\":\"{{name_prop}}\","
                                                  "\"unique_id\":\"{{unique_id}}\","
                                                  "\"device_class\":{{device_class}},"
                                                  "\"state_class\":{{state_class}},"
                                                  "{{unit}}"
                                                  "\"icon\":\"mdi:{{mqtt_icon}}\","
                                                  "\"state_topic\":\"{{state_topic}}/state\","
                                                  "\"value_template\":\"{{value}}\","
                                                  "\"device\": {"
                                                  "\"name\":\"LiXee-GW_{{device_name}}\","
                                                  "\"sw_version\":\"2.0\","
                                                  "\"model\":\"HW V2\","
                                                  "\"manufacturer\":\"LiXee\","
                                                  "\"identifiers\":[\"LiXee-GW{{device_name}}\"]"
                                                  "}"
                                                  "}";

          datas = FPSTR(HA_discovery_msg);

          datas.replace("{{name_prop}}", t->states[i].name);
          datas.replace("{{unique_id}}", IEEE + "_" + String(t->states[i].cluster) + "_" + String(t->states[i].attribute));

          if (memcmp(t->states[i].mqtt_device_class, "null", 4) == 0)
          {
            datas.replace("{{device_class}}", t->states[i].mqtt_device_class);
          }
          else
          {
            String tmp = "\"" + String(t->states[i].mqtt_device_class) + "\"";
            datas.replace("{{device_class}}", tmp);
          }

          if (memcmp(t->states[i].mqtt_state_class, "null", 4) == 0)
          {
            datas.replace("{{state_class}}", t->states[i].mqtt_state_class);
          }
          else
          {
            String tmp = "\"" + String(t->states[i].mqtt_state_class) + "\"";
            datas.replace("{{state_class}}", tmp);
          }

          datas.replace("{{mqtt_icon}}", t->states[i].mqtt_icon);

          if (strlen(t->states[i].unit) > 0)
          {
            String tmp = "\"unit_of_measurement\":\"" + String(t->states[i].unit) + "\",";
            datas.replace("{{unit}}", tmp);
          }
          else
          {
            datas.replace("{{unit}}", "");
          }

          datas.replace("{{state_topic}}", ConfigGeneral.headerMQTT + IEEE + "_" + String(t->states[i].cluster) + "_" + String(t->states[i].attribute));

          if ((String(t->states[i].type) == "numeric") || (String(t->states[i].type) == "float"))
          {
            if (t->states[i].coefficient != 1)
            {
              datas.replace("{{value}}", "{{value_json.value_" + String(t->states[i].cluster) + "_" + String(t->states[i].attribute) + " | float * " + String(t->states[i].coefficient) + "}}");
            }
            else
            {
              datas.replace("{{value}}", "{{value_json.value_" + String(t->states[i].cluster) + "_" + String(t->states[i].attribute) + "}}");
            }
          }
          else
          {
            datas.replace("{{value}}", "{{value_json.value_" + String(t->states[i].cluster) + "_" + String(t->states[i].attribute) + "}}");
          }

          datas.replace("{{device_name}}", model + "_" + IEEE);

          String topic = ConfigGeneral.headerMQTT + IEEE + "_" + String(t->states[i].cluster) + "_" + String(t->states[i].attribute) + "/config";

          if (model == "ZLinky_TIC")
          {
            const char *tmp;
            bool discoverOk = false;
            tmp = t->states[i].mode;
            if ((tmp != NULL) && (tmp[0] != '\0'))
            {
              char *modeCopy = strdup(tmp);
              if (modeCopy) {
                char *pch;
                pch = strtok(modeCopy, ";");
                while (pch != NULL)
                {
                  if (atoi(pch) == ConfigGeneral.LinkyMode)
                  {
                    discoverOk = true;
                    break;
                  }
                  pch = strtok(NULL, " ;");
                }
                free(modeCopy);
              }
            }
            else
            {
              discoverOk = true;
            }

            if (discoverOk)
            {
              mqttClient.publish(topic.c_str(), 0, true, datas.c_str());
            }
          }
          else
          {
            mqttClient.publish(topic.c_str(), 0, true, datas.c_str());
          }
        }
      }

      // ========== DISCOVERY DES ACTIONS (NOUVEAU) ==========
      if (t->ActionSize() > 0)
      {
        // Chercher si on a des actions ON/OFF pour créer un switch
        bool hasOnAction = false;
        bool hasOffAction = false;
        Action *onAction = nullptr;
        Action *offAction = nullptr;

        for (int i = 0; i < t->ActionSize(); i++)
        {
          Action *a = t->getAction(i);
          if (!a || !a->visible) continue;

          if (strcasecmp(a->name, "ON") == 0)
          {
            hasOnAction = true;
            onAction = a;
          }
          else if (strcasecmp(a->name, "OFF") == 0)
          {
            hasOffAction = true;
            offAction = a;
          }
        }

        // Si on a ON et OFF, créer une entité switch
        if (hasOnAction && hasOffAction && onAction && offAction)
        {
          // Dans handleSendMqttDiscover, remplacer le HA_switch_msg par :
          const char *PROGMEM HA_switch_msg = "{"
                    "\"name\":\"{{name_prop}}\","
                    "\"unique_id\":\"{{unique_id}}\","
                    "\"icon\":\"mdi:power-plug\","
                    "\"state_topic\":\"{{state_topic}}/state\","
                    "\"value_template\":\"{{ value_json.value_6_0 }}\","
                    "\"command_topic\":\"{{command_topic}}\","
                    "\"payload_on\":\"ON\","
                    "\"payload_off\":\"OFF\","
                    "\"state_on\":\"1\","
                    "\"state_off\":\"0\","
                    "\"device\": {"
                        "\"name\":\"LiXee-GW_{{device_name}}\","
                        "\"sw_version\":\"2.0\","
                        "\"model\":\"HW V2\","
                        "\"manufacturer\":\"LiXee\","
                        "\"identifiers\":[\"LiXee-GW{{device_name}}\"]"
                    "}"
                "}";

          datas = FPSTR(HA_switch_msg);
          datas.replace("{{name_prop}}", model + " Switch");
          datas.replace("{{unique_id}}", IEEE + "_switch");
          datas.replace("{{state_topic}}", ConfigGeneral.headerMQTT + IEEE + "_6_0");
          datas.replace("{{command_topic}}", "lixee/cmd/" + IEEE + "/action");
          datas.replace("{{shortAddr}}", String(shortAddrInt));
          datas.replace("{{endpoint}}", String(onAction->endpoint));
          datas.replace("{{command}}", String(onAction->command));
          datas.replace("{{endpoint_off}}", String(offAction->endpoint));
          datas.replace("{{command_off}}", String(offAction->command));
          datas.replace("{{device_name}}", model + "_" + IEEE);

          String topic = "homeassistant/switch/" + IEEE + "_switch/config";
          mqttClient.publish(topic.c_str(), 0, true, datas.c_str());

          Serial.printf("MQTT Discovery: Switch publié pour %s\n", IEEE.c_str());
        }

        // Publier les autres actions comme des boutons
        for (int i = 0; i < t->ActionSize(); i++)
        {
          Action *a = t->getAction(i);
          if (!a || !a->visible) continue;

          // Skip ON/OFF si déjà traités comme switch
          if (hasOnAction && hasOffAction)
          {
            if (strcasecmp(a->name, "ON") == 0 || strcasecmp(a->name, "OFF") == 0)
              continue;
          }

          const char *PROGMEM HA_button_msg = "{"
                                              "\"name\":\"{{name_prop}}\","
                                              "\"unique_id\":\"{{unique_id}}\","
                                              "\"command_topic\":\"{{command_topic}}\","
                                              "\"payload_press\":\"{{action_name}}\","
                                              "\"device\": {"
                                              "\"name\":\"LiXee-GW_{{device_name}}\","
                                              "\"sw_version\":\"2.0\","
                                              "\"model\":\"HW V2\","
                                              "\"manufacturer\":\"LiXee\","
                                              "\"identifiers\":[\"LiXee-GW{{device_name}}\"]"
                                              "}"
                                              "}";

          datas = FPSTR(HA_button_msg);
          datas.replace("{{name_prop}}", String(a->name));
          datas.replace("{{unique_id}}", IEEE + "_action_" + String(a->name));
          datas.replace("{{command_topic}}", "lixee/cmd/" + IEEE + "/action");
          datas.replace("{{action_name}}", String(a->name));  // Payload simple : "TOGGLE"
          datas.replace("{{device_name}}", model + "_" + IEEE);

          String topic = "homeassistant/button/" + IEEE + "_" + String(a->name) + "/config";
          mqttClient.publish(topic.c_str(), 0, true, datas.c_str());

          Serial.printf("MQTT Discovery: Button '%s' publié pour %s\n", a->name, IEEE.c_str());
        }
      }

      // ========== DISCOVERY DES ENTITÉS TARIF/COULEUR LINKY ==========
      if (model == "ZLinky_TIC")
      {
        String deviceBlock = "{\"name\":\"LiXee-GW_" + model + "_" + IEEE + "\","
                             "\"sw_version\":\"2.0\","
                             "\"model\":\"HW V2\","
                             "\"manufacturer\":\"LiXee\","
                             "\"identifiers\":[\"LiXee-GW" + model + "_" + IEEE + "\"]}";

        // 1. Tarif actuel
        {
          String payload = "{\"name\":\"Tarif actuel\","
                          "\"unique_id\":\"" + IEEE + "_tarif_actuel\","
                          "\"icon\":\"mdi:currency-eur\","
                          "\"state_topic\":\"" + String(ConfigGeneral.headerMQTT) + IEEE + "_tarif_actuel/state\","
                          "\"value_template\":\"{{ value_json.tarif_actuel }}\","
                          "\"device\":" + deviceBlock + "}";
          String topic = "homeassistant/sensor/" + IEEE + "_tarif_actuel/config";
          mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
          Serial.printf("MQTT Discovery: Tarif actuel publié pour %s\n", IEEE.c_str());
        }

        // 2. Couleur du jour
        {
          String payload = "{\"name\":\"Couleur du jour\","
                          "\"unique_id\":\"" + IEEE + "_couleur_jour\","
                          "\"icon\":\"mdi:palette\","
                          "\"state_topic\":\"" + String(ConfigGeneral.headerMQTT) + IEEE + "_couleur_jour/state\","
                          "\"value_template\":\"{{ value_json.couleur_jour }}\","
                          "\"device\":" + deviceBlock + "}";
          String topic = "homeassistant/sensor/" + IEEE + "_couleur_jour/config";
          mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
          Serial.printf("MQTT Discovery: Couleur jour publié pour %s\n", IEEE.c_str());
        }

        // 3. Couleur de demain
        {
          String payload = "{\"name\":\"Couleur de demain\","
                          "\"unique_id\":\"" + IEEE + "_couleur_demain\","
                          "\"icon\":\"mdi:calendar-today\","
                          "\"state_topic\":\"" + String(ConfigGeneral.headerMQTT) + IEEE + "_couleur_demain/state\","
                          "\"value_template\":\"{{ value_json.couleur_demain }}\","
                          "\"device\":" + deviceBlock + "}";
          String topic = "homeassistant/sensor/" + IEEE + "_couleur_demain/config";
          mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
          Serial.printf("MQTT Discovery: Couleur demain publié pour %s\n", IEEE.c_str());
        }
      }
    }
  }
  DeviceFile.close();

  result = "";
  request->send(200, F("text/html"), result);
}

void handleDeleteDevice(AsyncWebServerRequest *request)
{

  String tmpMac;
  int i = 0;
  tmpMac = request->arg(i);
  uint8_t mac[9];
  sscanf(tmpMac.c_str(), "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5], &mac[6], &mac[7]);
  uint64_t macInt = (uint64_t)mac[0] << 56 |
                    (uint64_t)mac[1] << 48 |
                    (uint64_t)mac[2] << 40 |
                    (uint64_t)mac[3] << 32 |
                    (uint64_t)mac[4] << 24 |
                    (uint64_t)mac[5] << 16 |
                    (uint64_t)mac[6] << 8 |
                    (uint64_t)mac[7];
  SendDeleteDevice(macInt);

  // on efface le fichier json
  String filename = "/db/" + tmpMac + ".json";
  int res;
  res = LittleFS.remove(filename);

  String filenamebk = "/bk/" + tmpMac + ".json";
  LittleFS.remove(filenamebk);

  for (auto it = devices.begin(); it != devices.end(); ++it) 
  {
    if ((*it)->getDeviceID() == tmpMac) 
    {
      (*it)->~DeviceData();
      free(*it);
      devices.erase(it);
      invalidateElectricalDeviceCache();
      break;
    }
  }

  if (res)
  {
      request->send(200, "text/plain", "OK");
  }
  else
  {
      request->send(500, "text/plain", "Error");
  }
}

void handleCleanGhostDevices(AsyncWebServerRequest *request) {
    requestGhostClean();
    request->send(200, F("text/html"), "OK");
}

void handleGetFormattedDate(AsyncWebServerRequest *request)
{

  String result;
  result = FormattedDate;
  request->send(200, F("text/html"), result);
}

// Handler pour récupérer la liste des sous-compteurs
void APIgetSubMeters(AsyncWebServerRequest *request) {
    String json = "{\"subMeters\":[";
    
    for (int i = 0; i < ConfigGeneral.subMeterCount; i++) {
        if (i > 0) json += ",";
        json += "{";
        json += "\"index\":" + String(i) + ",";
        json += "\"IEEE\":\"" + String(ConfigGeneral.subMeters[i].IEEE) + "\",";
        json += "\"alias\":\"" + String(ConfigGeneral.subMeters[i].alias) + "\",";
        json += "\"color\":\"" + String(ConfigGeneral.subMeters[i].color) + "\",";
        json += "\"enabled\":" + String(ConfigGeneral.subMeters[i].enabled ? "true" : "false");
        json += "}";
    }
    
    json += "],\"count\":" + String(ConfigGeneral.subMeterCount) + "}";
    request->send(200, "application/json", json);
}

// Handler pour ajouter/modifier un sous-compteur
void APIsetSubMeter(AsyncWebServerRequest *request) {
    if (!request->hasArg("IEEE") || !request->hasArg("alias")) {
        return request->send(400, "text/plain", "Missing parameters");
    }
    
    String IEEE = request->arg("IEEE");
    String alias = request->arg("alias");
    String color = request->hasArg("color") ? request->arg("color") : "#3498db";
    bool enabled = request->hasArg("enabled") ? (request->arg("enabled") == "true") : true;
    
    // Chercher si existe déjà
    int index = -1;
    for (int i = 0; i < ConfigGeneral.subMeterCount; i++) {
        if (strcmp(ConfigGeneral.subMeters[i].IEEE, IEEE.c_str()) == 0) {
            index = i;
            break;
        }
    }
    
    // Nouveau sous-compteur
    if (index == -1) {
        if (ConfigGeneral.subMeterCount >= MAX_SUBMETERS) {
            return request->send(400, "text/plain", "Max submeters reached");
        }
        index = ConfigGeneral.subMeterCount++;
    }
    
    // Mettre à jour
    strncpy(ConfigGeneral.subMeters[index].IEEE, IEEE.c_str(), 19);
    ConfigGeneral.subMeters[index].IEEE[sizeof(ConfigGeneral.subMeters[index].IEEE) - 1] = '\0';
    strncpy(ConfigGeneral.subMeters[index].alias, alias.c_str(), 31);
    ConfigGeneral.subMeters[index].alias[sizeof(ConfigGeneral.subMeters[index].alias) - 1] = '\0';
    strncpy(ConfigGeneral.subMeters[index].color, color.c_str(), 9);
    ConfigGeneral.subMeters[index].color[sizeof(ConfigGeneral.subMeters[index].color) - 1] = '\0';
    ConfigGeneral.subMeters[index].enabled = enabled;
    
    // Sauvegarder la config
    if (saveSubMetersConfig()) {
        request->send(200, "text/plain", "OK");
    } else {
        request->send(500, "text/plain", "Save failed");
    }
}

// Handler pour supprimer un sous-compteur
void APIdeleteSubMeter(AsyncWebServerRequest *request) {
    if (!request->hasArg("IEEE")) {
        return request->send(400, "text/plain", "Missing IEEE");
    }
    
    String IEEE = request->arg("IEEE");
    
    for (int i = 0; i < ConfigGeneral.subMeterCount; i++) {
        if (strcmp(ConfigGeneral.subMeters[i].IEEE, IEEE.c_str()) == 0) {
            // Décaler les suivants
            for (int j = i; j < ConfigGeneral.subMeterCount - 1; j++) {
                ConfigGeneral.subMeters[j] = ConfigGeneral.subMeters[j + 1];
            }
            ConfigGeneral.subMeterCount--;
            
            // Sauvegarder
            saveSubMetersConfig();

            return request->send(200, "text/plain", "OK");
        }
    }
    
    request->send(404, "text/plain", "SubMeter not found");
}

void APIgetEligibleSubMeters(AsyncWebServerRequest *request) {
    String json = "[";
    bool first = true;
    
    for (size_t i = 0; i < devices.size(); i++) {
        DeviceData* device = devices[i];
        if (device == nullptr) continue;
        
        // Vérifier que ce n'est pas le ZLinky principal
        if (strcmp(device->getDeviceID().c_str(), ConfigGeneral.ZLinky) == 0) continue;
        
        // Vérifier que le device a le cluster 0x0702 (SimpleMeter)
        // On vérifie si l'attribut 0 existe dans le cluster 0702
        String val = device->getValue("0702", "0");
        if (val.length() == 0) continue;  // Pas de données énergie
        
        if (!first) json += ",";
        first = false;
        
        json += "{";
        json += "\"IEEE\":\"" + device->getDeviceID() + "\",";
        json += "\"alias\":\"" + device->getInfo().alias + "\",";
        json += "\"model\":\"" + device->getInfo().model + "\",";
        json += "\"manufacturer\":\"" + device->getInfo().manufacturer + "\"";
        json += "}";
    }
    
    json += "]";
    request->send(200, "application/json", json);
}

void APIgetConfig(AsyncWebServerRequest *request)
{
  String result;


  request->send(200, F("application/json"), result);
}

void APIgetSystem(AsyncWebServerRequest *request)
{
  const char JSON_GET_SYSTEM[] PROGMEM = 
  "{"
  "    \"network\" :"
   "    {"
   "     \"wifi\":"
   "     {"
   "       \"enable\" : {{wifienable}},"
   "       \"connected\" : {{wificonnected}},"
   "       \"mode\" : {{wifimode}},"
   "       \"ip\" : \"{{wifiip}}\","
   "       \"netmask\" : \"{{wifimask}}\","
   "       \"gateway\" : \"{{wifigateway}}\""
   "     }"
  "    },"
  "    \"system\" :"
  "    {"
  "      \"mqtt\" : "
  "      {"
  "        \"enable\" : {{mqttenable}},"
  "        \"connected\" : {{mqttconected}},"
   "       \"url\" : \"{{mqtturl}}\","
  "        \"port\" : {{mqttport}}"
  "      },"
  "      \"webpush\" :"
  "      {"
  "        \"enable\" : {{webpushenable}},"
  "        \"auth\" : {{webpushauth}},"
  "        \"url\" : \"{{webpushurl}}\""
  "      },"
  "      \"infos\" :"
  "      {"
  "        \"t\" : {{Temperature}}"
  "      }"
  "    }"
  "  }";

  String result;

  result = FPSTR(JSON_GET_SYSTEM);

  result.replace("{{wifienable}}",String(ConfigSettings.enableWiFi));
  result.replace("{{wificonnected}}",String(ConfigSettings.connectedWifiSta));
  result.replace("{{wifimode}}",String(ConfigSettings.dhcp));
  result.replace("{{wifiip}}",ConfigSettings.ipAddressWiFi);
  result.replace("{{wifimask}}",ConfigSettings.ipMaskWiFi);
  result.replace("{{wifigateway}}",ConfigSettings.ipGWWiFi);
  
  result.replace("{{mqttenable}}",String(ConfigSettings.enableMqtt));
  result.replace("{{mqttconected}}",String(mqttClient.connected()));
  result.replace("{{mqtturl}}",ConfigGeneral.servMQTT);
  result.replace("{{mqttport}}", strlen(ConfigGeneral.portMQTT) > 0 ? String(ConfigGeneral.portMQTT) : "0");

  result.replace("{{webpushenable}}",String(ConfigSettings.enableWebPush));
  result.replace("{{webpushauth}}",String(ConfigGeneral.webPushAuth));
  result.replace("{{webpushurl}}",ConfigGeneral.servWebPush);

  float temperature = 0;
  temperature = temperatureReadFixed();
  result.replace("{{Temperature}}", String(temperature));

  request->send(200, F("application/json"), result);
}

void APIgetDevices(AsyncWebServerRequest *request)
{
  String result;

  File root = LittleFS.open("/db");
  File filedevice = root.openNextFile();
  result = "{";
  int i = 0;
  while (filedevice)
  {

    String inifile = filedevice.name();

    File file = LittleFS.open("/db/" + inifile, FILE_READ);
    if (!file || file.isDirectory())
    {
      DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier ini_read "));
      DEBUG_PRINTLN(inifile);
      file.close();
      xSemaphoreGive(file_Mutex);
    }
    size_t filesize = file.size();
    if (inifile.substring(3,4)!="_")
    {
      if (filesize > 0)
      {
        // Read entire file content first, then validate
        String content;
        content.reserve(filesize);
        while (file.available())
        {
          content += (char)file.read();
        }
        // Validate: must start with '{' and end with '}'
        content.trim();
        if (content.length() > 2 && content.charAt(0) == '{' && content.charAt(content.length() - 1) == '}')
        {
          if (i > 0)
          {
            result += ",";
          }
          result += "\""+inifile.substring(0,16)+"\" : ";
          result += content;
          i++;
        }
        else
        {
          Serial.printf("WARNING: /db/%s contient du JSON invalide (len=%d, first=0x%02X), ignoré\n",
            inifile.c_str(), content.length(),
            content.length() > 0 ? (uint8_t)content.charAt(0) : 0);
        }
      }
    }
    file.close();
    filedevice.close();
    vTaskDelay(1);
    filedevice = root.openNextFile();
  }
  result += "}";
  filedevice.close();
  root.close();

  request->send(200, F("application/json"), result);
}

void APIgetDevice(AsyncWebServerRequest *request)
{
  String IEEE;
  int args = request->args();
  String result;
  if (args > 0 && (request->hasArg("IEEE") || request->hasArg("id")))
  {
    IEEE = request->hasArg("IEEE") ? request->arg("IEEE") : request->arg("id");
  
    result = "{";
    String inifile = IEEE+".json";
    File file = LittleFS.open("/db/" + inifile, FILE_READ);
    if (!file || file.isDirectory())
    {
      result = "Échec de l'ouverture du fichier : "+ inifile;
      request->send(500, "text/plain", result);
      file.close();
    }
    size_t filesize = file.size();
    if (filesize > 0)
    {
      result += "\""+inifile.substring(0,16)+"\" : ";
      while (file.available())
      {
        result += (char)file.read();
      }
    }  
    file.close();
    result += "}";       
  }else{
    result="{}";
  }

  request->send(200, F("application/json"), result);
}

void APIgetLinky(AsyncWebServerRequest *request)
{
  String IEEE, result;
  IEEE = String(ConfigGeneral.ZLinky);
  if (IEEE.length()>0)
  {
    String model;
    model = GetModel(IEEE+".json");
    int DeviceId = GetDeviceId(IEEE+".json");
    result ="{";
    if (TemplateExist(DeviceId))
    {
      /*Template *t;
      t = GetTemplate(DeviceId, model);*/
      DeviceData* device;
      for (size_t i = 0; i < devices.size(); i++) 
      {
        device = devices[i];
        if (device->getDeviceID() == IEEE)
        {
          break;
        }
      }
      TemplateData* t = device->getTemplate();  

      if (!t) {
          // Template introuvable - logger pour debug
          Serial.printf("WARNING: Template introuvable pour model: %s\n", model.c_str());
          return; // ou return, selon votre logique
      }
      for (int i = 0; i < t->StateSize(); i++)
      {      
        const char *tmp;
        bool discoverOk = false;
        tmp = t->states[i].mode;
        if ((tmp != NULL) && (tmp[0] != '\0'))
        {
          char *modeCopy = strdup(tmp);
          if (modeCopy) {
            char * pch;
            pch = strtok(modeCopy, ";");
            while (pch != NULL)
            {
              if (atoi(pch) == ConfigGeneral.LinkyMode)
              {
                discoverOk=true;
                break;
              }
              pch = strtok(NULL, " ;");
            }
            free(modeCopy);
          }
        }else{
          discoverOk=true;
        }

        if (discoverOk)
        {
          if (i>0){result+=",";}
          result += "\"";
          result += (String)t->states[i].cluster+"_"+(String)t->states[i].attribute;
          result += "\" :";

          if ((memcmp(t->states[i].type,"numeric",7)==0) || (memcmp(t->states[i].type,"float",5)==0) )
          {
            result +=  GetValueStatus(IEEE, t->states[i].cluster, t->states[i].attribute, (String)t->states[i].type, t->states[i].coefficient);
          }else{
            result +="\"";
            result +=  GetValueStatus(IEEE, t->states[i].cluster, t->states[i].attribute, (String)t->states[i].type, t->states[i].coefficient);
            result +="\"";
          }
          
        }

      }

    }
    result +="}";
  }else{
    result = "{}";
  }

  request->send(200, F("application/json"), result);
}

void APIgetEnergyDevice(AsyncWebServerRequest *request)
{
  String IEEE;
  int args = request->args();
  String result;
  if (args > 0 && (request->hasArg("IEEE") || request->hasArg("id")))
  {
    IEEE = request->hasArg("IEEE") ? request->arg("IEEE") : request->arg("id");
    result = "{";
    String inifile = "nrg_"+IEEE+".json";
    File file = LittleFS.open("/db/" + inifile, FILE_READ);
    if (!file || file.isDirectory())
    {
      result = "Échec de l'ouverture du fichier : "+ inifile;
      request->send(500, "text/plain", result);
      file.close();
    }
    size_t filesize = file.size();
    if (filesize > 0)
    {
      result += "\""+inifile.substring(4,20)+"\" : ";
      while (file.available())
      {
        result += (char)file.read();
      }
    }  
    file.close();
    result += "}";    
  }else{
    result="{}";
  }

  request->send(200, F("application/json"), result);
}

void APIgetPowerDevice(AsyncWebServerRequest *request)
{
  String IEEE;
  int args = request->args();
  String result;
  if (args > 0 && (request->hasArg("IEEE") || request->hasArg("id")))
  {
    IEEE = request->hasArg("IEEE") ? request->arg("IEEE") : request->arg("id");

    result = "{";
    String inifile = "pwr_"+IEEE+".json";
    File file = LittleFS.open("/db/" + inifile, FILE_READ);
    if (!file || file.isDirectory())
    {
      result = "Échec de l'ouverture du fichier : "+ inifile;
      request->send(500, "text/plain", result);
      file.close();
    }
    size_t filesize = file.size();
    if (filesize > 0)
    {
      result += "\""+inifile.substring(4,20)+"\" : ";
      while (file.available())
      {
        result += (char)file.read();
      }
    }  
    file.close();
    result += "}";
    
    
  }else{
    result="{}";
  }

  request->send(200, F("application/json"), result);
}

void APIgetTemplates(AsyncWebServerRequest *request)
{
  
  const char* json = templateCache.getJson();
  if (json) {
      request->send(200, F("application/json"), json);
  } else {
      request->send(500, F("application/json"), "{}");
  }
}

void launchUpdateTask() {
  esp_task_wdt_reset();
  // updateStatus.log = "";  // Reset du log a chaque tentative (desactive temporairement)

  updateLog("========================================");
  updateLog("    DEMARRAGE MISE A JOUR AUTO");
  updateLog("========================================");
  updateLog("Heap: %u | Min heap: %u | PSRAM: %u",
                ESP.getFreeHeap(), esp_get_minimum_free_heap_size(),
                ESP.getFreePsram());
  updateLog("WiFi: RSSI=%d dBm | IP=%s",
                WiFi.RSSI(), WiFi.localIP().toString().c_str());
  updateLog("LittleFS: %u / %u octets utilises",
                LittleFS.usedBytes(), LittleFS.totalBytes());

  updateStatus.statusAuto = "Téléchargement ...";
  updateStatus.progressAuto = 0;

  unsigned long startTime = millis();

  if (checkUpdateFirmware())
  {
    updateLog("Phase installation... Heap: %u", ESP.getFreeHeap());
    updateStatus.statusAuto = "Installation ...";
    esp_task_wdt_reset();
    untarApplyAndRestore("/bk/update.tar.gz");

    updateLog("Installation terminee en %lu ms | Heap: %u",
                  millis() - startTime, ESP.getFreeHeap());

    executeReboot=true;
    updateStatus.statusAuto = "Mise à jour terminée ...";
    updateStatus.progressAuto = 100;
    updateStatus.rebootRequested = true;
    updateLog("====== SUCCES - REDEMARRAGE ======");
  }else{
    updateLog("ECHEC apres %lu ms | Heap: %u | RSSI: %d",
                  millis() - startTime, ESP.getFreeHeap(), WiFi.RSSI());
    updateStatus.statusAuto = "Problème de téléchargement ...";
    updateLog("====== ECHEC ======");
  }
}

void handleGetUpdateStatusManuel(AsyncWebServerRequest *request) {
    
  String response = "{";
  response += "\"status\":\"" + updateStatus.statusManuel + "\",";
  response += "\"progress\":" + String(updateStatus.progressManuel) + ","; 
  response += "\"reboot\":" + String(updateStatus.rebootRequested ? "true" : "false");
  response += "}";
  
  request->send(200, "application/json", response);
}

void handleGetUpdateStatusAuto(AsyncWebServerRequest *request) {

  String response = "{";
  response += "\"status\":\"" + updateStatus.statusAuto + "\",";
  response += "\"progress\":" + String(updateStatus.progressAuto) + ",";
  response += "\"reboot\":" + String(updateStatus.rebootRequested ? "true" : "false");
  response += "}";

  request->send(200, "application/json", response);
}

/* LOG UPDATE - desactive temporairement
void handleGetUpdateLog(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", updateStatus.log.length() > 0 ? updateStatus.log : "(aucun log)");
}
*/



// ===================== Thermostat virtuel — UI & endpoints =====================

// Filtre un device selon le type recherché.
// kind: 0=capteur température (0402), 1=actionneur on/off (0006/powerSocket),
//       2=capteur de présence (0406 ou modèle motion/presence), 3=capteur d'ouverture (IAS 0500 ou modèle contact/door)
static bool thermoDeviceMatch(DeviceData* d, int kind) {
  switch (kind) {
    case 0:
      // Capteur de température : cluster 0x0402 (sonde) ou 0x0201/0 (température locale HVAC/clim)
      if (d->hasCluster(1026)) return true;
      if (d->getValue("0402", "0").length() > 0 || d->getValue("0402", "0000").length() > 0) return true;
      if (d->getValue("0201", "0").length() > 0 || d->getValue("0201", "0000").length() > 0) return true;
      return false;
    case 1: {
      // Prise on/off, ou tout appareil pilotable par action (clim HVAC 0x0201, fil pilote, etc.)
      if (d->hasCluster(6) || d->getInfo().powerSocket.toInt() == 1) return true;
      if (d->hasCluster(513)) return true;  // 0x0201 HVAC thermostat
      TemplateData* tpl = d->getTemplate();
      return tpl && tpl->ActionSize() > 0;
    }
    case 2: {
      if (d->hasCluster(1030)) return true;  // 0406 occupancy
      String m = d->getInfo().model; m.toLowerCase();
      return m.indexOf("motion") >= 0 || m.indexOf("presence") >= 0 ||
             m.indexOf("occupancy") >= 0 || m.indexOf("fp1") >= 0 || m.indexOf("sml") >= 0;
    }
    case 3: {
      if (d->hasCluster(1280)) return true;  // 0500 IAS Zone
      String m = d->getInfo().model; m.toLowerCase();
      return m.indexOf("contact") >= 0 || m.indexOf("door") >= 0 ||
             m.indexOf("magnet") >= 0 || m.indexOf("ouverture") >= 0;
    }
  }
  return false;
}

// Construit les <option> d'un <select> de devices, filtrés par type (cf. thermoDeviceMatch).
static String thermoDeviceOptions(int kind, const String& current) {
  String opt = "<option value=''>-- Aucun --</option>";
  for (size_t i = 0; i < devices.size(); i++) {
    DeviceData* d = devices[i];
    if (!thermoDeviceMatch(d, kind)) continue;
    String id = d->getDeviceID();
    String label = d->getInfo().alias.length() ? d->getInfo().alias : id;
    opt += "<option value='" + id + "'";
    if (id == current) opt += " selected";
    opt += ">" + label + " (" + id + ")</option>";
  }
  return opt;
}

// Cases à cocher des capteurs d'ouverture, pré-cochées selon la zone.
static String thermoOpenCheckboxes(const VirtualThermostat& t) {
  String html;
  for (size_t i = 0; i < devices.size(); i++) {
    DeviceData* d = devices[i];
    if (!thermoDeviceMatch(d, 3)) continue;
    String id = d->getDeviceID();
    bool checked = false;
    for (int k = 0; k < t.openSensorCount; k++) {
      if (id == t.openSensors[k]) { checked = true; break; }
    }
    String label = d->getInfo().alias.length() ? d->getInfo().alias : id;
    html += "<div class='form-check'><input class='form-check-input' type='checkbox' name='open_" + id +
            "' id='o_" + id + "' " + (checked ? "checked" : "") + ">"
            "<label class='form-check-label' for='o_" + id + "'>" + label + " (" + id + ")</label></div>";
  }
  if (html.length() == 0) html = "<p style='color:#888;font-size:12px;'>Aucun capteur d'ouverture d&eacute;tect&eacute;.</p>";
  return html;
}

// Cases à cocher des prises/relais supplémentaires (kind=1), pré-cochées selon la zone.
static String thermoActuatorCheckboxes(const VirtualThermostat& t) {
  String html;
  for (size_t i = 0; i < devices.size(); i++) {
    DeviceData* d = devices[i];
    if (!thermoDeviceMatch(d, 1)) continue;
    String id = d->getDeviceID();
    bool checked = false;
    for (int k = 0; k < t.actuatorsExtraCount; k++) {
      if (id == t.actuatorsExtra[k]) { checked = true; break; }
    }
    String label = d->getInfo().alias.length() ? d->getInfo().alias : id;
    html += "<div class='form-check'><input class='form-check-input' type='checkbox' name='xact_" + id +
            "' id='x_" + id + "' " + (checked ? "checked" : "") + ">"
            "<label class='form-check-label' for='x_" + id + "'>" + label + " (" + id + ")</label></div>";
  }
  if (html.length() == 0) html = "<p style='color:#888;font-size:12px;'>Aucune prise suppl&eacute;mentaire disponible.</p>";
  return html;
}

String tariffLabelForAttr(int attrId);      // défini dans lixee.cpp
const int* getTariffAvailablePeriods();     // défini dans lixee.cpp

// Cases à cocher des périodes tarifaires du ZLinky (selon le contrat détecté), pré-cochées.
static String thermoTariffCheckboxes(const VirtualThermostat& t) {
  DeviceData* lk = nullptr;
  for (size_t i = 0; i < devices.size(); i++) {
    if (devices[i]->getDeviceID() == String(ConfigGeneral.ZLinky)) { lk = devices[i]; break; }
  }
  if (!lk) return "<p style='color:#888;font-size:12px;'>Configurez d'abord un compteur ZLinky (Config &rarr; Energie).</p>";
  String sel = String(",") + t.tariffPeriods + ",";
  String html;
  const int* ids = getTariffAvailablePeriods();
  for (int k = 0; ids[k] >= 0; k++) {
    int id = ids[k];
    String name = tariffLabelForAttr(id);
    bool checked = sel.indexOf("," + String(id) + ",") >= 0;
    html += "<div class='form-check'><input class='form-check-input' type='checkbox' name='tp_" + String(id) +
            "' id='tp" + String(id) + "' " + (checked ? "checked" : "") + ">"
            "<label class='form-check-label' for='tp" + String(id) + "'>" + name + "</label></div>";
  }
  if (html.length() == 0) html = "<p style='color:#888;font-size:12px;'>P&eacute;riodes tarifaires non encore d&eacute;tect&eacute;es.</p>";
  return html;
}

// <option> des actions disponibles de l'appareil actionneur (depuis son template).
static String thermoActionOptions(const char* ieee, const String& current) {
  String opt = "<option value=''>Marche/Arr&ecirc;t simple (on/off)</option>";
  if (ieee && ieee[0]) {
    for (size_t i = 0; i < devices.size(); i++) {
      if (devices[i]->getDeviceID() == String(ieee)) {
        TemplateData* tpl = devices[i]->getTemplate();
        if (tpl) {
          for (int k = 0; k < tpl->ActionSize(); k++) {
            String nm = tpl->actions[k].name;
            if (nm.length() == 0) continue;
            opt += "<option value='" + nm + "'";
            if (nm == current) opt += " selected";
            opt += ">" + nm + "</option>";
          }
        }
        break;
      }
    }
  }
  return opt;
}

void handleLoadThermostats(AsyncWebServerRequest *request) {
  request->send(200, F("application/json"), thermostatsToJson());
}

// Renvoie en JSON les noms d'actions du template d'un appareil (pour peupler les listes Action marche/arrêt).
void handleDeviceActions(AsyncWebServerRequest *request) {
  String ieee = request->arg("IEEE");
  String j = "[";
  bool first = true;
  for (size_t i = 0; i < devices.size(); i++) {
    if (devices[i]->getDeviceID() == ieee) {
      TemplateData* tpl = devices[i]->getTemplate();
      if (tpl) {
        for (int k = 0; k < tpl->ActionSize(); k++) {
          String nm = tpl->actions[k].name;
          if (nm.length() == 0) continue;
          nm.replace("\\", "\\\\");
          nm.replace("\"", "\\\"");
          if (!first) j += ",";
          first = false;
          j += "\"" + nm + "\"";
        }
      }
      break;
    }
  }
  j += "]";
  request->send(200, F("application/json"), j);
}

void handleSetThermostatSetpoint(AsyncWebServerRequest *request) {
  if (!request->hasParam("id") || !request->hasParam("value")) {
    request->send(400, F("application/json"), F("{\"ok\":false}"));
    return;
  }
  int id = request->getParam("id")->value().toInt();
  float v = request->getParam("value")->value().toFloat();
  bool ok = setThermostatSetpoint(id, v);
  request->send(ok ? 200 : 400, F("application/json"), ok ? F("{\"ok\":true}") : F("{\"ok\":false}"));
}

void handleSetThermostatForce(AsyncWebServerRequest *request) {
  if (!request->hasParam("id") || !request->hasParam("mode")) {
    request->send(400, F("application/json"), F("{\"ok\":false}"));
    return;
  }
  int id = request->getParam("id")->value().toInt();
  int mode = request->getParam("mode")->value().toInt();
  bool ok = setThermostatForce(id, mode);
  request->send(ok ? 200 : 400, F("application/json"), ok ? F("{\"ok\":true}") : F("{\"ok\":false}"));
}

void handleSetThermostatMode(AsyncWebServerRequest *request) {
  if (!request->hasParam("id") || !request->hasParam("heat")) {
    request->send(400, F("application/json"), F("{\"ok\":false}"));
    return;
  }
  int id = request->getParam("id")->value().toInt();
  bool heat = request->getParam("heat")->value().toInt() != 0;
  bool ok = setThermostatMode(id, heat);
  request->send(ok ? 200 : 400, F("application/json"), ok ? F("{\"ok\":true}") : F("{\"ok\":false}"));
}

void handleSetThermostatFrost(AsyncWebServerRequest *request) {
  if (!request->hasParam("id") || !request->hasParam("on")) {
    request->send(400, F("application/json"), F("{\"ok\":false}"));
    return;
  }
  int id = request->getParam("id")->value().toInt();
  bool on = request->getParam("on")->value().toInt() != 0;
  bool ok = setThermostatFrost(id, on);
  request->send(ok ? 200 : 400, F("application/json"), ok ? F("{\"ok\":true}") : F("{\"ok\":false}"));
}

void handleThermostats(AsyncWebServerRequest *request) {
  if (!checkHeapForPage(request)) return;
  PSRAMString result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += F("<div class='container'>"
              "<div style='margin:1rem 0;'><h3>Thermostats</h3></div>"
              "<div class='row' id='thermoCards'></div>"
              "<div id='thermoEmpty' style='display:none;color:#888;'>Aucune zone configur&eacute;e. Rendez-vous dans <b>Config &rarr; Thermostat</b> pour en cr&eacute;er une.</div>"
              "</div>");
  result += F("<script>"
    "function tFetch(u){return fetch(u,{headers:{'X-Requested-With':'XMLHttpRequest'}});}"
    "var tZones=[];"
    "function p2c(cx,cy,r,d){var a=(d-90)*Math.PI/180;return [cx+r*Math.cos(a),cy+r*Math.sin(a)];}"
    "function arc(cx,cy,r,a0,a1){var s=p2c(cx,cy,r,a0),e=p2c(cx,cy,r,a1);var f=(a1-a0)<=180?'0':'1';"
      "return 'M'+s[0].toFixed(1)+' '+s[1].toFixed(1)+' A'+r+' '+r+' 0 '+f+' 1 '+e[0].toFixed(1)+' '+e[1].toFixed(1);}"
    "function tLoad(){tFetch('/loadThermostats').then(function(r){return r.json();}).then(tRender).catch(function(){});}"
    "function tRender(z){tZones=z;var c=document.getElementById('thermoCards');"
      "document.getElementById('thermoEmpty').style.display=z.length?'none':'block';"
      "var h='';for(var i=0;i<z.length;i++){var t=z[i];"
        "var active=t.actualOn;var tv=(t.temp===null||t.temp===undefined)?null:t.temp;"
        "var mc=t.heating?(active?'#e74c3c':'#f1948a'):(active?'#2980b9':'#aed6f1');"
        "var mic=active?(t.heating?'#e67e22':'#2980b9'):'#adb5bd';"
        "var icon=t.heating?(\"<svg width='18' height='18' viewBox='0 0 16 16' fill='\"+mic+\"'><path d='M8 16c3.314 0 6-2 6-5.5 0-1.5-.5-4-2.5-6 .25 1.5-1.25 2-1.25 2C11 4 9 .5 6 0c.357 2 .5 4-2 6-1.25 1-2 2.729-2 4.5C2 14 4.686 16 8 16m0-1c-1.657 0-3-1-3-2.75 0-.75.25-2 1.25-3C6.125 10 7 10.5 7 10.5c-.375-1.25.5-3.25 2-3.5-.179 1-.25 2 1 3 .625.5 1 1.364 1 2.25C12 14 10.657 15 8 15'/></svg>\"):(\"<svg width='18' height='18' viewBox='0 0 16 16' fill='\"+mic+\"'><path d='M8 16a.5.5 0 0 1-.5-.5v-1.293l-.646.647a.5.5 0 0 1-.707-.708L7.5 12.793V8.866l-3.4 1.963-.496 1.85a.5.5 0 1 1-.966-.26l.237-.882-1.12.646a.5.5 0 0 1-.5-.866l1.12-.646-.884-.237a.5.5 0 1 1 .26-.966l1.848.495L6.5 8 3.102 6.037l-1.849.495a.5.5 0 0 1-.259-.966l.883-.237-1.12-.646a.5.5 0 1 1 .5-.866l1.12.646-.237-.883a.5.5 0 0 1 .966-.259l.495 1.849L7.5 7.134V3.207L6.147 1.854a.5.5 0 1 1 .707-.708l.646.647V.5a.5.5 0 0 1 1 0v1.293l.647-.647a.5.5 0 1 1 .707.708L8.5 3.207v3.927l3.4-1.963.495-1.849a.5.5 0 1 1 .966.259l-.236.883 1.12-.646a.5.5 0 0 1 .5.866l-1.12.646.883.237a.5.5 0 1 1-.26.966l-1.848-.495L8.866 8l3.398 1.963 1.849-.495a.5.5 0 0 1 .259.966l-.883.237 1.12.646a.5.5 0 0 1-.5.866l-1.12-.646.236.883a.5.5 0 1 1-.966.259l-.495-1.849L8.5 8.866v3.927l1.354 1.353a.5.5 0 0 1-.708.708L8.5 14.207V15.5a.5.5 0 0 1-.5.5z'/></svg>\");"
        "var stat;if(!t.sensorValid){stat='<span style=\"color:#e74c3c;\">Capteur HS</span>';}"
        "else if(t.forceMode==1){stat='<span style=\"color:#8e44ad;\">Marche forc&eacute;e</span>';}"
        "else if(t.forceMode==2){stat='<span style=\"color:#8e44ad;\">Arr&ecirc;t forc&eacute;</span>';}"
        "else if(t.windowOpen){stat='<span style=\"color:#e67e22;\">Fen&ecirc;tre ouverte</span>';}"
        "else if(t.hasPresence&&!t.occupied){stat='<span style=\"color:#999;\">Absence</span>';}"
        "else if(!t.schedActive){stat='<span style=\"color:#9b59b6;\">'+(t.operMode==2?('Tarif '+(t.tariffNow||'')):'Hors plage')+'</span>';}"
        "else if(t.output&&!t.actualOn){stat='<span style=\"color:#f39c12;\">D&eacute;marrage&hellip;</span>';}"
        "else if(!t.output&&t.actualOn){stat='<span style=\"color:#f39c12;\">Arr&ecirc;t&hellip;</span>';}"
        "else{var act=t.heating?'Chauffe':'Rafra&icirc;chit';stat=t.actualOn?act+' ('+t.duty+'%)':'R&eacute;gule';}"
        "var bs=\"display:inline-flex;align-items:center;gap:4px;background:#f5f6f8;border-radius:14px;padding:3px 8px;font-size:11px;color:#495057;white-space:nowrap;\";"
        "var ig='#6c757d';"
        "var icP=\"<svg width='13' height='13' viewBox='0 0 16 16' fill='\"+ig+\"'><path d='M8 8a3 3 0 1 0 0-6 3 3 0 0 0 0 6m2-3a2 2 0 1 1-4 0 2 2 0 0 1 4 0m4 8c0 1-1 1-1 1H3s-1 0-1-1 1-4 6-4 6 3 6 4m-1-.004c-.001-.246-.154-.986-.832-1.664C11.516 10.68 10.289 10 8 10s-3.516.68-4.168 1.332c-.678.678-.83 1.418-.832 1.664z'/></svg>\";"
        "var icD=\"<svg width='13' height='13' viewBox='0 0 16 16' fill='\"+ig+\"'><path d='M3 2a1 1 0 0 1 1-1h8a1 1 0 0 1 1 1v13h1.5a.5.5 0 0 1 0 1h-13a.5.5 0 0 1 0-1H3zm1 13h8V2H4z'/><path d='M9 9a1 1 0 1 0 2 0 1 1 0 0 0-2 0'/></svg>\";"
        "var b='';"
        "if(t.hasPresence){var pv=t.occupied?'Pr&eacute;sent':'Absent';b+=\"<span style='\"+bs+\"'>\"+icP+\"<span>\"+pv+\"</span></span>\";}"
        "if(t.openCount>0){var ov=t.windowOpen?'Ouvert':'Ferm&eacute;';b+=\"<span style='\"+bs+\"'>\"+icD+\"<span>\"+ov+\"</span></span>\";}"
        "var mn=5,mx=35;function ang(v){return 225+(Math.max(mn,Math.min(mx,v))-mn)/(mx-mn)*270;}"
        "var cur=(tv===null)?null:p2c(105,105,86,ang(tv));"
        "var lo=p2c(105,105,104,225),hi=p2c(105,105,104,495);"
        "var prog=\"<path d='\"+arc(105,105,86,225,ang(t.setpoint))+\"' fill='none' stroke='\"+mc+\"' stroke-width='14' stroke-linecap='round'/>\";"
        "var sa=ang(t.setpoint);var dz='';"
        /* Animation seulement si l'actionneur rapproche la temperature de la consigne :
           chauffe utile si temp<consigne, froid utile si temp>consigne. Sinon (ex. marche forcee
           a contre-sens) : pas de segment anime (couleur + icone restent allumees). */
        "if(active&&tv!==null&&Math.abs(tv-t.setpoint)>0.2&&(t.heating?(tv<t.setpoint):(tv>t.setpoint))){var ca=ang(tv);var a0=Math.min(sa,ca),a1=Math.max(sa,ca);"
          "var off=(ca<sa)?'32;0':'0;32';"  /* flux toujours du point temperature actuelle vers le point consigne (sens valide empiriquement) */
          "var g0=t.heating?'#ff6b35':'#1e88e5',g1=t.heating?'#ffd166':'#80d8ff';"
          "dz=\"<defs><linearGradient id='dg\"+t.id+\"' gradientUnits='userSpaceOnUse' x1='10' y1='10' x2='200' y2='200'><stop offset='0' stop-color='\"+g0+\"'/><stop offset='1' stop-color='\"+g1+\"'/></linearGradient></defs>\"+"
             "\"<path d='\"+arc(105,105,86,a0,a1)+\"' fill='none' stroke='url(#dg\"+t.id+\")' stroke-width='14' stroke-linecap='round' stroke-dasharray='5 11'>\"+"
             "\"<animate attributeName='stroke-dashoffset' values='\"+off+\"' dur='1.4s' repeatCount='indefinite'/>\"+"
             "\"<animate attributeName='opacity' values='0.55;1;0.55' dur='1.8s' repeatCount='indefinite'/></path>\";}"
        "var dot=(cur===null)?'':(\"<circle cx='\"+cur[0].toFixed(1)+\"' cy='\"+cur[1].toFixed(1)+\"' r='7' fill='#fff' stroke='#495057' stroke-width='3'/>\");"
        "h+=\"<div class='col-lg-4 col-md-6 col-12'><div class='card' style='padding:18px;margin-bottom:1rem;position:relative;'>\";"
        "h+=\"<div style='display:flex;align-items:center;justify-content:space-between;'><h5 style='margin:0;font-size:17px;'>\"+t.name+\"</h5>\";"
        "h+=\"<a href='/editThermostat?id=\"+t.id+\"' title='Configurer' style='line-height:0;color:#9aa0a6;'><svg width='18' height='18' viewBox='0 0 16 16' fill='currentColor'><path d='M8 4.754a3.246 3.246 0 1 0 0 6.492 3.246 3.246 0 0 0 0-6.492M5.754 8a2.246 2.246 0 1 1 4.492 0 2.246 2.246 0 0 1-4.492 0'/><path d='M9.796 1.343c-.527-1.79-3.065-1.79-3.592 0l-.094.319a.873.873 0 0 1-1.255.52l-.292-.16c-1.64-.892-3.433.902-2.54 2.541l.159.292a.873.873 0 0 1-.52 1.255l-.319.094c-1.79.527-1.79 3.065 0 3.592l.319.094a.873.873 0 0 1 .52 1.255l-.16.292c-.892 1.64.901 3.434 2.541 2.54l.292-.159a.873.873 0 0 1 1.255.52l.094.319c.527 1.79 3.065 1.79 3.592 0l.094-.319a.873.873 0 0 1 1.255-.52l.292.16c1.64.893 3.434-.902 2.54-2.541l-.159-.292a.873.873 0 0 1 .52-1.255l.319-.094c1.79-.527 1.79-3.065 0-3.592l-.319-.094a.873.873 0 0 1-.52-1.255l.16-.292c.893-1.64-.902-3.433-2.541-2.54l-.292.159a.873.873 0 0 1-1.255-.52zm-2.633.283c.246-.835 1.428-.835 1.674 0l.094.319a1.873 1.873 0 0 0 2.693 1.115l.291-.16c.764-.415 1.6.42 1.184 1.185l-.159.292a1.873 1.873 0 0 0 1.116 2.692l.318.094c.835.246.835 1.428 0 1.674l-.319.094a1.873 1.873 0 0 0-1.115 2.693l.16.291c.415.764-.42 1.6-1.185 1.184l-.291-.159a1.873 1.873 0 0 0-2.693 1.116l-.094.318c-.246.835-1.428.835-1.674 0l-.094-.319a1.873 1.873 0 0 0-2.692-1.115l-.292.16c-.764.415-1.6-.42-1.184-1.185l.159-.291A1.873 1.873 0 0 0 1.945 8.93l-.319-.094c-.835-.246-.835-1.428 0-1.674l.319-.094A1.873 1.873 0 0 0 3.06 4.377l-.16-.292c-.415-.764.42-1.6 1.185-1.184l.292.159a1.873 1.873 0 0 0 2.692-1.115z'/></svg></a></div>\";"
        "h+=\"<div style='position:relative;text-align:center;'><svg viewBox='0 0 210 210' style='width:100%;max-width:240px;'>\";"
        "h+=\"<path d='\"+arc(105,105,86,225,495)+\"' fill='none' stroke='#e9ecef' stroke-width='14' stroke-linecap='round'/>\"+prog+dz;"
        "h+=dot;"
        "h+=\"<text x='\"+lo[0].toFixed(0)+\"' y='\"+(lo[1]+4).toFixed(0)+\"' text-anchor='middle' font-size='13' fill='#adb5bd'>\"+mn+\"&#176;</text>\";"
        "h+=\"<text x='\"+hi[0].toFixed(0)+\"' y='\"+(hi[1]+4).toFixed(0)+\"' text-anchor='middle' font-size='13' fill='#adb5bd'>\"+mx+\"&#176;</text></svg>\";"
        "h+=\"<div style='position:absolute;top:50%;left:0;right:0;transform:translateY(-50%);text-align:center;'>\";"
        "h+=\"<div style='font-size:11px;color:#999;text-transform:uppercase;letter-spacing:.5px;'>Consigne</div>\";"
        "h+=\"<div style='font-size:40px;font-weight:700;line-height:1.05;'>\"+t.setpoint.toFixed(1)+\"<span style='font-size:18px;font-weight:400;'>&deg;C</span></div>\";"
        "h+=\"<div style='margin-top:8px;line-height:0;'>\"+icon+\"</div>\";"
        "h+=\"<div style='margin-top:4px;font-size:12px;color:#888;'>\"+stat+\"</div>\";"
        "h+=\"</div></div>\";"
        "h+=\"<div style='text-align:center;font-size:13px;color:#6c757d;margin-top:2px;'>Actuelle : <b style='color:#343a40;'>\"+(tv===null?'&mdash;':(tv.toFixed(1)+' &deg;C'))+\"</b></div>\";"
        "h+=\"<div style='display:flex;justify-content:center;align-items:center;gap:24px;margin-top:8px;'>\";"
        "h+=\"<button class='btn btn-outline-secondary' style='font-size:24px;width:48px;height:48px;line-height:1;padding:0;border-radius:50%;' onclick='tSet(\"+t.id+\",-0.5)'>&minus;</button>\";"
        "h+=\"<button class='btn btn-outline-secondary' style='font-size:24px;width:48px;height:48px;line-height:1;padding:0;border-radius:50%;' onclick='tSet(\"+t.id+\",0.5)'>+</button></div>\";"
        "if(t.reversible){var hm=t.heating?1:0;"
        "function mb(x,l){var on=(hm==x);return \"<button onclick='tMode(\"+t.id+\",\"+x+\")' style='flex:1;border:1px solid #dee2e6;background:\"+(on?(x?'#e67e22':'#2980b9'):'#fff')+\";color:\"+(on?'#fff':'#6c757d')+\";font-size:12px;padding:5px 0;cursor:pointer;'>\"+l+\"</button>\";}"
        "h+=\"<div style='display:flex;border-radius:8px;overflow:hidden;margin-top:12px;'>\"+mb(1,'Chaud')+mb(0,'Froid')+\"</div>\";}"
        "var fm=t.forceMode||0;"
        "function fb(m,l){var on=(fm==m);return \"<button onclick='tForce(\"+t.id+\",\"+m+\")' style='flex:1;border:1px solid #dee2e6;background:\"+(on?'#6c757d':'#fff')+\";color:\"+(on?'#fff':'#6c757d')+\";font-size:12px;padding:5px 0;cursor:pointer;'>\"+l+\"</button>\";}"
        "var fr=fb(0,'Auto')+fb(1,'Marche')+fb(2,'Arr&ecirc;t');"
        "if(t.heating){var gon=(t.frostMode==1);"
        "fr+=\"<button onclick='tFrost(\"+t.id+\",\"+(gon?0:1)+\")' title='Bascule la consigne sur le hors-gel' style='flex:1;border:1px solid #dee2e6;background:\"+(gon?'#5dade2':'#fff')+\";color:\"+(gon?'#fff':'#6c757d')+\";font-size:12px;padding:5px 0;cursor:pointer;'>Hors-gel</button>\";}"
        "h+=\"<div style='display:flex;border-radius:8px;overflow:hidden;margin-top:8px;'>\"+fr+\"</div>\";"
        "h+=\"<div style='display:flex;flex-wrap:nowrap;justify-content:center;align-items:center;gap:6px;margin-top:10px;overflow-x:auto;'>\"+b+\"</div>\";"
        "h+=\"</div></div>\";"
      "}c.innerHTML=h;}"
    "function tSet(id,d){var s=18;for(var i=0;i<tZones.length;i++){if(tZones[i].id===id){s=tZones[i].setpoint;break;}}"
      "var v=Math.round((s+d)*2)/2;if(v<0)v=0;if(v>40)v=40;"
      "tFetch('/setThermostatSetpoint?id='+id+'&value='+v).then(function(){tLoad();});}"
    "function tForce(id,m){tFetch('/setThermostatForce?id='+id+'&mode='+m).then(function(){tLoad();});}"
    "function tMode(id,h){tFetch('/setThermostatMode?id='+id+'&heat='+h).then(function(){tLoad();});}"
    "function tFrost(id,on){tFetch('/setThermostatFrost?id='+id+'&on='+on).then(function(){tLoad();});}"
    "tLoad();setInterval(tLoad,5000);"
    "</script>");
  result += footer();
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);
  AsyncResponseStream *response = request->beginResponseStream("text/html");
  response->print(result.c_str());
  request->send(response);
}

// Libellé lisible d'un device (alias sinon IEEE) pour les fiches.
static String thermoDeviceLabel(const char* ieee) {
  if (ieee == nullptr || ieee[0] == '\0') return "(non d&eacute;fini)";
  String id(ieee);
  for (size_t i = 0; i < devices.size(); i++) {
    if (devices[i]->getDeviceID() == id) {
      String a = devices[i]->getInfo().alias;
      return a.length() ? a : id;
    }
  }
  return id;  // device introuvable : afficher l'IEEE
}

// Échappe une chaîne pour un contexte JS entre apostrophes.
static String jsEscapeSingle(const String& s) {
  String r = s;
  r.replace("\\", "\\\\");
  r.replace("'", "\\'");
  return r;
}

// Redirection côté client (navigation normale) vers une URL relative.
// Évite la redirection HTTP 303 que le relais/app du tunnel ne suit pas correctement.
void sendThermoRedirect(AsyncWebServerRequest *request, const char *rel) {
  String h = F("<html><head><meta charset='utf-8'>"
               "<style>@keyframes ts{to{transform:rotate(360deg)}}</style></head>"
               "<body style='display:flex;align-items:center;justify-content:center;height:100vh;margin:0;'>"
               "<div style='width:48px;height:48px;border:5px solid #ccc;border-top-color:#2980b9;border-radius:50%;animation:ts 0.8s linear infinite;'></div>"
               "<script>location.replace('");
  h += rel;
  h += F("');</script></body></html>");
  AsyncResponseStream *response = request->beginResponseStream("text/html");
  response->print(h);
  request->send(response);
}

// Liste des zones sous forme de fiches (style Zigbee), avec boutons Modifier / Supprimer.
void handleConfigThermostats(AsyncWebServerRequest *request) {
  if (!checkHeapForPage(request)) return;
  PSRAMString result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += F("<style>"
    ".config-card{background:#fff;border:none;border-radius:12px;box-shadow:0 2px 8px rgba(0,0,0,0.08);transition:transform .2s,box-shadow .2s;height:100%}"
    ".config-card:hover{transform:translateY(-2px);box-shadow:0 4px 16px rgba(0,0,0,0.12)}"
    ".config-card table td{padding:6px 4px;border:none}"
    ".config-card .btn-actions{display:flex;flex-wrap:wrap;justify-content:flex-end;gap:6px;margin-top:12px;padding-top:12px;border-top:1px solid #e9ecef}"
    ".config-card .btn{padding:6px 10px;display:inline-flex;align-items:center;justify-content:center}"
    ".config-card .btn svg{width:16px;height:16px;flex-shrink:0}"
    "</style>");
  result += F("<div class='container'>"
              "<div style='display:flex;justify-content:space-between;align-items:center;margin:1rem 0;'>"
              "<h3>Configuration des thermostats</h3>");
  if (vThermostatCount < MAX_VTHERMOSTATS) {
    result += F("<a class='btn btn-primary' href='/editThermostat?id=new'>"
                "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' class='bi bi-plus-circle' viewBox='0 0 16 16' style='vertical-align:-2px;'>"
                "<path d='M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16'/>"
                "<path d='M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4'/>"
                "</svg> Ajouter une zone</a>");
  }
  result += F("</div><div class='row g-4'>");

  for (int i = 0; i < vThermostatCount; i++) {
    VirtualThermostat& t = vThermostats[i];
    result += F("<div class='col-12 col-sm-6 col-md-4'><div class='config-card p-3'>");
    result += F("<div style='display:flex;align-items:center;gap:10px;margin-bottom:8px;'>"
                "<img src='web/img/icon_thermostat.png' height='48px' style='flex-shrink:0;'>");
    result += "<h5 style='margin:0;flex:1;'>" + String(t.name) + "</h5>";
    // Switch d'activation (remplace le bouton Activer/Désactiver)
    result += String("<div class='form-check form-switch' style='margin:0;padding-left:2.6em;'>"
                     "<input class='form-check-input' type='checkbox' role='switch' title='Activer / d&eacute;sactiver' "
                     "style='width:2.5em;height:1.3em;cursor:pointer;' onchange=\"location.href='/toggleThermostat?id=") + String(i) + "'\" " + (t.enabled ? "checked" : "") + "></div>";
    result += F("</div>");
    result += F("<table style='width:100%;font-size:13px;'>");
    // Mode
    result += String("<tr><td style='color:#6c757d;'>Mode</td><td style='text-align:right;'>") + (t.heating ? "Chauffage" : "Rafra&icirc;chissement") + "</td></tr>";
    // Consignes
    result += "<tr><td style='color:#6c757d;'>Consigne</td><td style='text-align:right;'>" + String(t.setpoint, 1) + " &deg;C</td></tr>";
    result += "<tr><td style='color:#6c757d;'>Hors-gel</td><td style='text-align:right;'>" + String(t.frostTemp, 1) + " &deg;C</td></tr>";
    // Capteurs
    result += "<tr><td style='color:#6c757d;'>Capteur</td><td style='text-align:right;'>" + thermoDeviceLabel(t.sensorIEEE) + "</td></tr>";
    result += "<tr><td style='color:#6c757d;'>Prise</td><td style='text-align:right;'>" + thermoDeviceLabel(t.actuatorIEEE) +
              (t.actuatorsExtraCount > 0 ? " <span style='color:#888;'>+" + String(t.actuatorsExtraCount) + "</span>" : String("")) + "</td></tr>";
    result += String("<tr><td style='color:#6c757d;'>Pr&eacute;sence</td><td style='text-align:right;'>") + (strlen(t.presenceIEEE) ? thermoDeviceLabel(t.presenceIEEE) : String("&mdash;")) + "</td></tr>";
    result += "<tr><td style='color:#6c757d;'>Ouvertures</td><td style='text-align:right;'>" + String(t.openSensorCount) + "</td></tr>";
    result += "</table>";
    result += F("<div class='btn-actions'>");
    result += "<a class='btn btn-info' href='/editThermostat?id=" + String(i) + "' title='Modifier'>"
              "<svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'>"
              "<path d='M12.146.146a.5.5 0 0 1 .708 0l3 3a.5.5 0 0 1 0 .708l-10 10a.5.5 0 0 1-.168.11l-5 2a.5.5 0 0 1-.65-.65l2-5a.5.5 0 0 1 .11-.168zM11.207 2.5 13.5 4.793 14.793 3.5 12.5 1.207zm1.586 3L10.5 3.207 4 9.707V10h.5a.5.5 0 0 1 .5.5v.5h.5a.5.5 0 0 1 .5.5v.5h.293zm-9.761 5.175-.106.106-1.528 3.821 3.821-1.528.106-.106A.5.5 0 0 1 5 12.5V12h-.5a.5.5 0 0 1-.5-.5V11h-.5a.5.5 0 0 1-.468-.325'/>"
              "</svg></a>";
    result += "<button class='btn btn-danger' onclick=\"delThermo(" + String(i) + ",'" + jsEscapeSingle(String(t.name)) + "')\" title='Supprimer'>"
              "<svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'>"
              "<path d='M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z'/>"
              "<path d='M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H6a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1h3.5a1 1 0 0 1 1 1zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z'/>"
              "</svg></button>";
    result += F("</div></div></div>");
  }
  result += F("</div>");
  if (vThermostatCount == 0) {
    result += F("<div align='center' style='color:#888;margin-top:2rem;'>Aucune zone configur&eacute;e. Cliquez sur &laquo; Ajouter une zone &raquo;.</div>");
  }
  result += F("</div>"
              "<script>function delThermo(id,name){if(confirm('Supprimer la zone \"'+name+'\" ?')){window.location.href='/deleteThermostat?id='+id;}}</script>");
  result += footer();
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);
  AsyncResponseStream *response = request->beginResponseStream("text/html");
  response->print(result.c_str());
  request->send(response);
}

// Icônes SVG plates monochromes (grises) pour les libellés du formulaire thermostat.
#define TIC(p) "<span style='display:inline-flex;align-items:center;vertical-align:-3px;margin-right:6px;'><svg width='15' height='15' viewBox='0 0 16 16' fill='#6c757d'>" p "</svg></span>"
#define IC_HOME   TIC("<path d='M8.707 1.5a1 1 0 0 0-1.414 0L.646 8.146a.5.5 0 0 0 .708.708L2 8.207V13.5A1.5 1.5 0 0 0 3.5 15h9a1.5 1.5 0 0 0 1.5-1.5V8.207l.646.647a.5.5 0 0 0 .708-.708L13 5.793V2.5a.5.5 0 0 0-.5-.5h-1a.5.5 0 0 0-.5.5v1.293zM13 7.207V13.5a.5.5 0 0 1-.5.5h-9a.5.5 0 0 1-.5-.5V7.207l5-5z'/>")
#define IC_THERMO TIC("<path d='M9.5 12.5a1.5 1.5 0 1 1-2-1.415V6.5a.5.5 0 0 1 1 0v4.585a1.5 1.5 0 0 1 1 1.415'/><path d='M5.5 2.5a2.5 2.5 0 0 1 5 0v7.55a3.5 3.5 0 1 1-5 0zM8 1a1.5 1.5 0 0 0-1.5 1.5v7.987l-.167.15a2.5 2.5 0 1 0 3.333 0l-.166-.15V2.5A1.5 1.5 0 0 0 8 1'/>")
#define IC_SNOW   TIC("<path d='M8 16a.5.5 0 0 1-.5-.5v-1.293l-.646.647a.5.5 0 0 1-.707-.708L7.5 12.793V8.866l-3.4 1.963-.496 1.85a.5.5 0 1 1-.966-.26l.237-.882-1.12.646a.5.5 0 0 1-.5-.866l1.12-.646-.884-.237a.5.5 0 1 1 .26-.966l1.848.495L6.5 8 3.102 6.037l-1.849.495a.5.5 0 0 1-.259-.966l.883-.237-1.12-.646a.5.5 0 1 1 .5-.866l1.12.646-.237-.883a.5.5 0 0 1 .966-.259l.495 1.849L7.5 7.134V3.207L6.147 1.854a.5.5 0 1 1 .707-.708l.646.647V.5a.5.5 0 0 1 1 0v1.293l.647-.647a.5.5 0 1 1 .707.708L8.5 3.207v3.927l3.4-1.963.495-1.849a.5.5 0 1 1 .966.259l-.236.883 1.12-.646a.5.5 0 0 1 .5.866l-1.12.646.883.237a.5.5 0 1 1-.26.966l-1.848-.495L8.866 8l3.398 1.963 1.849-.495a.5.5 0 0 1 .259.966l-.883.237 1.12.646a.5.5 0 0 1-.5.866l-1.12-.646.236.883a.5.5 0 1 1-.966.259l-.495-1.849L8.5 8.866v3.927l1.354 1.353a.5.5 0 0 1-.708.708L8.5 14.207V15.5a.5.5 0 0 1-.5.5z'/>")
#define IC_CLOCK  TIC("<path d='M8 3.5a.5.5 0 0 0-1 0V9a.5.5 0 0 0 .252.434l3.5 2a.5.5 0 0 0 .496-.868L8 8.71z'/><path d='M8 16A8 8 0 1 0 8 0a8 8 0 0 0 0 16m7-8A7 7 0 1 1 1 8a7 7 0 0 1 14 0'/>")
#define IC_PLUG   TIC("<path d='M7.5 1v7h1V1z'/><path d='M3 8.812a5 5 0 0 1 2.578-4.375l-.485-.874A6 6 0 1 0 11 3.616l-.501.865A5 5 0 1 1 3 8.812'/>")
#define IC_PLAY   TIC("<path d='m11.596 8.697-6.363 3.692c-.54.313-1.233-.066-1.233-.697V4.308c0-.63.692-1.01 1.233-.696l6.363 3.692a.802.802 0 0 1 0 1.393'/>")
#define IC_STOP   TIC("<path d='M3.5 5A1.5 1.5 0 0 1 5 3.5h6A1.5 1.5 0 0 1 12.5 5v6a1.5 1.5 0 0 1-1.5 1.5H5A1.5 1.5 0 0 1 3.5 11z'/>")
#define IC_PERSON TIC("<path d='M8 8a3 3 0 1 0 0-6 3 3 0 0 0 0 6m2-3a2 2 0 1 1-4 0 2 2 0 0 1 4 0m4 8c0 1-1 1-1 1H3s-1 0-1-1 1-4 6-4 6 3 6 4m-1-.004c-.001-.246-.154-.986-.832-1.664C11.516 10.68 10.289 10 8 10s-3.516.68-4.168 1.332c-.678.678-.83 1.418-.832 1.664z'/>")
#define IC_DOOR   TIC("<path d='M3 2a1 1 0 0 1 1-1h8a1 1 0 0 1 1 1v13h1.5a.5.5 0 0 1 0 1h-13a.5.5 0 0 1 0-1H3zm1 13h8V2H4z'/><path d='M9 9a1 1 0 1 0 2 0 1 1 0 0 0-2 0'/>")
#define IC_GEAR   TIC("<path d='M8 4.754a3.246 3.246 0 1 0 0 6.492 3.246 3.246 0 0 0 0-6.492M5.754 8a2.246 2.246 0 1 1 4.492 0 2.246 2.246 0 0 1-4.492 0'/><path d='M9.796 1.343c-.527-1.79-3.065-1.79-3.592 0l-.094.319a.873.873 0 0 1-1.255.52l-.292-.16c-1.64-.892-3.433.902-2.54 2.541l.159.292a.873.873 0 0 1-.52 1.255l-.319.094c-1.79.527-1.79 3.065 0 3.592l.319.094a.873.873 0 0 1 .52 1.255l-.16.292c-.892 1.64.901 3.434 2.541 2.54l.292-.159a.873.873 0 0 1 1.255.52l.094.319c.527 1.79 3.065 1.79 3.592 0l.094-.319a.873.873 0 0 1 1.255-.52l.292.16c1.64.893 3.434-.902 2.54-2.541l-.159-.292a.873.873 0 0 1 .52-1.255l.319-.094c1.79-.527 1.79-3.065 0-3.592l-.319-.094a.873.873 0 0 1-.52-1.255l.16-.292c.893-1.64-.902-3.433-2.541-2.54l-.292.159a.873.873 0 0 1-1.255-.52zm-2.633.283c.246-.835 1.428-.835 1.674 0l.094.319a1.873 1.873 0 0 0 2.693 1.115l.291-.16c.764-.415 1.6.42 1.184 1.185l-.159.292a1.873 1.873 0 0 0 1.116 2.692l.318.094c.835.246.835 1.428 0 1.674l-.319.094a1.873 1.873 0 0 0-1.115 2.693l.16.291c.415.764-.42 1.6-1.185 1.184l-.291-.159a1.873 1.873 0 0 0-2.693 1.116l-.094.318c-.246.835-1.428.835-1.674 0l-.094-.319a1.873 1.873 0 0 0-2.692-1.115l-.292.16c-.764.415-1.6-.42-1.184-1.185l.159-.291A1.873 1.873 0 0 0 1.945 8.93l-.319-.094c-.835-.246-.835-1.428 0-1.674l.319-.094A1.873 1.873 0 0 0 3.06 4.377l-.16-.292c-.415-.764.42-1.6 1.185-1.184l.292.159a1.873 1.873 0 0 0 2.692-1.115z'/>")

// Formulaire d'édition d'une zone (id=N) ou de création (id=new).
void handleEditThermostat(AsyncWebServerRequest *request) {
  if (!checkHeapForPage(request)) return;

  int id = -1;  // -1 = nouvelle zone
  if (request->hasParam("id")) {
    String s = request->getParam("id")->value();
    if (s != "new") id = s.toInt();
  }
  VirtualThermostat def;
  initThermostatDefaults(def);
  bool existing = (id >= 0 && id < vThermostatCount);
  VirtualThermostat& t = existing ? vThermostats[id] : def;

  PSRAMString result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += F("<div class='container' style='max-width:680px;margin-bottom:2rem;'>");
  result += existing ? ("<h3 class='mt-3'>Modifier : " + String(t.name) + "</h3>") : String("<h3 class='mt-3'>Nouvelle zone</h3>");
  result += F("<form method='POST' action='/saveThermostat'>");
  result += "<input type='hidden' name='id' value='" + (existing ? String(id) : String("new")) + "'>";

  // --- Bloc 1 : identité + activation (switch) ---
  result += F("<div class='card p-4 mb-3'>");
  result += "<label class='form-label fw-bold'>" IC_HOME "Nom de la zone</label>";
  result += "<input class='form-control' name='name' placeholder='Ex : Salon' value='" + String(t.name) + "'>";
  result += String("<div class='form-check form-switch mt-3' style='font-size:18px;'>"
                   "<input class='form-check-input' type='checkbox' role='switch' name='enabled' id='ckEn' style='width:3em;height:1.5em;cursor:pointer;' ") + (t.enabled ? "checked" : "") +
            ">"
            "<label class='form-check-label' for='ckEn' style='padding-left:10px;'>Zone activée</label></div>";
  result += F("</div>");

  // --- Bloc 2 : régulation (priorité) ---
  result += F("<div class='card p-4 mb-3'>"
              "<h5 class='mb-3'>Régulation</h5>"
              "<label class='form-label'>Mode</label>"
              "<select class='form-select mb-3' name='heating' id='modeSel'>");
  result += String("<option value='1'") + (t.heating ? " selected" : "") + ">Chauffage</option>";
  result += String("<option value='0'") + (!t.heating ? " selected" : "") + ">Rafra&icirc;chissement</option></select>";
  result += "<label class='form-label'>" IC_THERMO "Consigne (°C)</label>";
  result += "<input class='form-control' type='number' step='0.5' name='setpoint' value='" + String(t.setpoint, 1) + "'>";
  result += F("<div class='form-text mb-3'>Température cible à maintenir dans la zone.</div>");
  // Hors-gel : uniquement en mode chauffage
  result += F("<div id='frostRow'><label class='form-label'>" IC_SNOW "Hors-gel (°C)</label>");
  result += "<input class='form-control' type='number' step='0.5' name='frost' value='" + String(t.frostTemp, 1) + "'>";
  result += F("<div class='form-text'>Sécurité : en mode chauffage, la zone chauffe toujours sous ce seuil, même si elle est éteinte, une fenêtre ouverte ou en cas d'absence.</div></div>"
              "<script>(function(){var m=document.getElementById('modeSel'),f=document.getElementById('frostRow');"
              "function u(){f.style.display=m.value=='1'?'':'none';}m.addEventListener('change',u);u();})();</script>");
  // Fonctionnement : toujours / plages horaires / tarif Linky
  result += F("<label class='form-label'>" IC_CLOCK "Fonctionnement</label><select class='form-select mb-2' name='operMode' id='operSel'>");
  result += String("<option value='0'") + (t.operMode == 0 ? " selected" : "") + ">Toujours</option>";
  result += String("<option value='1'") + (t.operMode == 1 ? " selected" : "") + ">Plages horaires</option>";
  result += String("<option value='2'") + (t.operMode == 2 ? " selected" : "") + ">Selon tarif Linky</option></select>";
  result += F("<div id='schedRow'><label class='form-label'>Plages horaires</label>");
  result += "<input class='form-control' name='schedule' placeholder='06:00-09:00,17:00-23:00' value='" + String(t.schedule) + "'>";
  result += F("<div class='form-text'>Format <b>HH:MM-HH:MM</b>, séparées par des virgules. La zone régule uniquement pendant ces plages (le hors-gel reste actif).</div></div>");
  result += F("<div id='tarifRow' style='display:none;'><label class='form-label'>Périodes tarifaires actives</label>"
              "<div style='border:1px solid #dee2e6;border-radius:8px;padding:10px;'>");
  result += thermoTariffCheckboxes(t);
  result += F("</div><div class='form-text'>La zone régule pendant les périodes <b>cochées</b> (le hors-gel reste actif). "
              "Les périodes proposées dépendent de votre abonnement Linky (Base, HC/HP, Tempo Bleu/Blanc/Rouge).</div></div>");
  result += F("<script>(function(){var o=document.getElementById('operSel'),s=document.getElementById('schedRow'),n=document.getElementById('tarifRow');"
              "function u(){s.style.display=o.value=='1'?'':'none';n.style.display=o.value=='2'?'':'none';}o.addEventListener('change',u);u();})();</script>"
              "</div>");

  // --- Bloc 3 : capteurs ---
  result += F("<div class='card p-4 mb-3'><h5 class='mb-3'>Capteurs</h5>");
  result += "<label class='form-label'>" IC_THERMO "Capteur de température</label><select class='form-select' name='sensor'>";
  result += thermoDeviceOptions(0, String(t.sensorIEEE));
  result += F("</select><div class='form-text mb-3'>Mesure la température de la zone (indispensable pour réguler).</div>");
  result += "<label class='form-label'>" IC_PLUG "Appareil piloté principal</label><select class='form-select' id='actSel' name='actuator'>";
  result += thermoDeviceOptions(1, String(t.actuatorIEEE));
  result += F("</select><div class='form-text mb-3'>Prise, relais, climatiseur ou radiateur fil pilote qui chauffe / refroidit la zone. C'est lui qui définit les actions ci-dessous.</div>");
  // Prises supplémentaires : reçoivent exactement la même commande que le principal
  result += F("<label class='form-label'>" IC_PLUG "Prises supplémentaires <span class='text-muted'>(optionnel)</span></label>"
              "<div style='border:1px solid #dee2e6;border-radius:8px;padding:10px;margin-bottom:4px;'>");
  result += thermoActuatorCheckboxes(t);
  result += F("</div><div class='form-text mb-3'>Prises/relais pilotés <b>en parallèle</b> du principal (même marche/arrêt). Utile pour chauffer une zone avec plusieurs appareils.</div>");
  // Actions de l'appareil (clim HEAT/COOL/OFF, fil pilote CONFORT/ECO/OFF...) — sinon marche/arrêt simple
  result += F("<label class='form-label'>" IC_THERMO "Action Chaud</label><select class='form-select' id='aHeat' name='actionHeat'>");
  result += thermoActionOptions(t.actuatorIEEE, String(t.actionHeat));
  result += F("</select><div class='form-text mb-2'>Action quand la zone doit <b>chauffer</b> (ex : <b>HEAT</b>, <b>CONFORT</b>). Laissez sur « Marche/Arrêt simple » pour une prise on/off.</div>");
  result += F("<label class='form-label'>" IC_SNOW "Action Froid <span class='text-muted'>(clim réversible)</span></label><select class='form-select' id='aCool' name='actionCool'>");
  result += thermoActionOptions(t.actuatorIEEE, String(t.actionCool));
  result += F("</select><div class='form-text mb-2'>Action quand la zone doit <b>refroidir</b> (ex : <b>COOL</b>). Renseignez <b>Chaud ET Froid</b> pour une clim réversible (le mode sera choisissable sur la vignette).</div>");
  result += F("<label class='form-label'>" IC_STOP "Action Arrêt</label><select class='form-select' id='aOff' name='actionOff'>");
  result += thermoActionOptions(t.actuatorIEEE, String(t.actionOff));
  result += F("</select><div class='form-text mb-3'>Action au repos (ex : <b>OFF</b>, <b>ARRET</b>, <b>ECO</b>).</div>");
  // Met à jour dynamiquement les listes d'actions selon l'appareil sélectionné
  result += F("<script>(function(){var sel=document.getElementById('actSel');"
              "function fill(s,a,cur){var h=\"<option value=''>Marche/Arr&ecirc;t simple (on/off)</option>\";"
              "for(var i=0;i<a.length;i++){var n=a[i];var e=n.replace(/&/g,'&amp;').replace(/</g,'&lt;');"
              "h+=\"<option value='\"+e.replace(/'/g,'&#39;')+\"'\"+(n==cur?' selected':'')+\">\"+e+\"</option>\";}s.innerHTML=h;}"
              "sel.addEventListener('change',function(){var ie=sel.value;"
              "var H=document.getElementById('aHeat'),C=document.getElementById('aCool'),O=document.getElementById('aOff');"
              "var hc=H.value,cc=C.value,oc=O.value;"
              "if(!ie){fill(H,[],'');fill(C,[],'');fill(O,[],'');return;}"
              "fetch('deviceActions?IEEE='+encodeURIComponent(ie),{headers:{'X-Requested-With':'XMLHttpRequest'}}).then(function(r){return r.json();})"
              ".then(function(a){fill(H,a,hc);fill(C,a,cc);fill(O,a,oc);}).catch(function(){});});"
              "})();</script>");
  result += F("<label class='form-label'>" IC_PERSON "Capteur de présence <span class='text-muted'>(optionnel)</span></label><select class='form-select' name='presence'>");
  result += thermoDeviceOptions(2, String(t.presenceIEEE));
  result += F("</select><div class='form-text mb-3'>Si configuré : la chauffe est suspendue en cas d'absence (aucun mouvement depuis 30 min).</div>");
  result += F("<label class='form-label'>" IC_DOOR "Capteurs d'ouverture <span class='text-muted'>(optionnel, plusieurs possibles)</span></label>"
              "<div style='border:1px solid #dee2e6;border-radius:8px;padding:10px;'>");
  result += thermoOpenCheckboxes(t);
  result += F("</div><div class='form-text'>Si une porte / fenêtre est ouverte, la chauffe est suspendue.</div>"
              "</div>");

  // --- Bloc 4 : paramètres avancés (repliable + pédagogie) ---
  result += F("<details class='card p-4 mb-3'>"
              "<summary style='cursor:pointer;font-weight:600;font-size:17px;'>" IC_GEAR "Paramètres avancés de régulation (TPI)</summary>"
              "<p class='form-text mt-2'>La régulation <b>TPI</b> (Time Proportional &amp; Integral) module le <i>temps</i> de chauffe sur un cycle pour stabiliser la température sans à-coups. Les valeurs par défaut conviennent à la plupart des installations — ne les modifiez que si la régulation oscille ou réagit mal.</p>");
  result += "<label class='form-label mt-2'>Durée du cycle (secondes)</label>";
  result += "<input class='form-control' type='number' name='cycle' value='" + String(t.tpiCycleSec) + "'>";
  result += F("<div class='form-text mb-3'>Sur chaque cycle, le chauffage est actif une fraction du temps proportionnelle au besoin. <b>Plus court</b> = plus réactif mais plus de commutations ; <b>plus long</b> = plus doux. Recommandé : <b>900 s (15 min)</b> pour un convecteur électrique.</div>");
  result += "<label class='form-label'>Kp — gain proportionnel</label>";
  result += "<input class='form-control' type='number' step='0.1' name='kp' value='" + String(t.tpiKp, 2) + "'>";
  result += F("<div class='form-text mb-3'>Force de réaction à l'écart avec la consigne. <b>Plus élevé</b> = chauffe plus fort quand on est loin du but, mais risque d'<b>oscillations</b> autour de la consigne. Défaut : <b>0,8</b>.</div>");
  result += "<label class='form-label'>Ki — gain intégral</label>";
  result += "<input class='form-control' type='number' step='0.05' name='ki' value='" + String(t.tpiKi, 2) + "'>";
  result += F("<div class='form-text mb-3'>Corrige l'erreur qui <b>persiste dans le temps</b> (évite de stagner juste sous la consigne). Trop élevé = <b>dépassements</b> de la consigne. Défaut : <b>0,1</b>.</div>");
  result += "<label class='form-label'>Temps ON minimal (secondes)</label>";
  result += "<input class='form-control' type='number' name='minon' value='" + String(t.minOnSec) + "'>";
  result += F("<div class='form-text mb-3'>Durée minimale d'allumage : <b>protège l'appareil</b> et évite les cycles marche/arrêt trop rapprochés. Défaut : <b>180 s (3 min)</b>.</div>");
  result += "<label class='form-label'>Temps OFF minimal (secondes)</label>";
  result += "<input class='form-control' type='number' name='minoff' value='" + String(t.minOffSec) + "'>";
  result += F("<div class='form-text mb-3'>Durée minimale d'extinction avant de pouvoir rallumer. Défaut : <b>180 s (3 min)</b>.</div>");
  result += "<label class='form-label'>Délai avant capteur « HS » (minutes)</label>";
  result += "<input class='form-control' type='number' name='sensortimeout' value='" + String(t.sensorTimeoutSec / 60) + "'>";
  result += F("<div class='form-text'>Si le capteur de température n'a envoyé <b>aucune mesure</b> depuis ce délai, il est considéré « HS » et la zone est coupée par sécurité. Augmentez-le si votre capteur ne remonte une valeur que de temps en temps (sur variation). Défaut : <b>60 min</b>.</div>"
              "</details>");

  // --- Boutons ---
  result += F("<div style='display:flex;gap:8px;'>"
              "<input type='submit' class='btn btn-primary' style='flex:1;' value='Enregistrer'>"
              "<a class='btn btn-secondary' style='flex:1;' href='/configThermostats'>Annuler</a>"
              "</div></form></div>");
  result += footer();
  result += F("</html>");
  result.replace("{{FormattedDate}}", FormattedDate);
  AsyncResponseStream *response = request->beginResponseStream("text/html");
  response->print(result.c_str());
  request->send(response);
}

// Enregistre une zone (création si id=new, sinon mise à jour de la zone id).
void handleSaveThermostat(AsyncWebServerRequest *request) {
  int id = -1;
  String sid = request->arg("id");
  if (sid != "new" && sid.length() > 0) id = sid.toInt();

  int target;
  if (id >= 0 && id < vThermostatCount) {
    target = id;  // mise à jour
  } else {
    if (vThermostatCount >= MAX_VTHERMOSTATS) {  // plus de place
      sendThermoRedirect(request, "configThermostats");
      return;
    }
    target = vThermostatCount;  // ajout
    initThermostatDefaults(vThermostats[target]);
  }

  VirtualThermostat& t = vThermostats[target];
  String name = request->arg("name"); name.trim();
  String sensor = request->arg("sensor"); sensor.trim();
  String act = request->arg("actuator"); act.trim();
  String presence = request->arg("presence"); presence.trim();
  strlcpy(t.name, name.length() ? name.c_str() : "Zone", sizeof(t.name));
  strlcpy(t.sensorIEEE, sensor.c_str(), sizeof(t.sensorIEEE));
  strlcpy(t.actuatorIEEE, act.c_str(), sizeof(t.actuatorIEEE));
  // Prises supplémentaires : cases cochées "xact_<IEEE>" (on exclut le principal pour éviter un double envoi)
  t.actuatorsExtraCount = 0;
  {
    int np = request->params();
    for (int k = 0; k < np; k++) {
      const AsyncWebParameter* pr = request->getParam(k);
      if (pr->name().startsWith("xact_") && t.actuatorsExtraCount < MAX_EXTRA_ACTUATORS) {
        String ieee = pr->name().substring(5);
        if (ieee.length() == 0 || ieee == act) continue;  // ignore vide ou identique au principal
        strlcpy(t.actuatorsExtra[t.actuatorsExtraCount], ieee.c_str(), 20);
        t.actuatorsExtraCount++;
      }
    }
  }
  strlcpy(t.actionHeat, request->arg("actionHeat").c_str(), sizeof(t.actionHeat));
  strlcpy(t.actionCool, request->arg("actionCool").c_str(), sizeof(t.actionCool));
  strlcpy(t.actionOff, request->arg("actionOff").c_str(), sizeof(t.actionOff));
  strlcpy(t.presenceIEEE, presence.c_str(), sizeof(t.presenceIEEE));
  // Capteurs d'ouverture : cases cochées "open_<IEEE>"
  t.openSensorCount = 0;
  int nparams = request->params();
  for (int k = 0; k < nparams; k++) {
    const AsyncWebParameter* pr = request->getParam(k);
    if (pr->name().startsWith("open_") && t.openSensorCount < MAX_OPEN_SENSORS) {
      String ieee = pr->name().substring(5);
      strlcpy(t.openSensors[t.openSensorCount], ieee.c_str(), 20);
      if (strlen(t.openSensors[t.openSensorCount]) > 0) t.openSensorCount++;
    }
  }
  t.enabled = request->hasArg("enabled");
  t.heating = request->arg("heating").toInt() != 0;
  if (request->hasArg("setpoint")) t.setpoint = request->arg("setpoint").toFloat();
  if (request->hasArg("frost"))    t.frostTemp = request->arg("frost").toFloat();
  if (request->hasArg("cycle"))    { int c = request->arg("cycle").toInt(); if (c >= 60) t.tpiCycleSec = c; }
  if (request->hasArg("kp"))       t.tpiKp = request->arg("kp").toFloat();
  if (request->hasArg("ki"))       t.tpiKi = request->arg("ki").toFloat();
  if (request->hasArg("minon"))    t.minOnSec = request->arg("minon").toInt();
  if (request->hasArg("minoff"))   t.minOffSec = request->arg("minoff").toInt();
  if (request->hasArg("sensortimeout")) { int m = request->arg("sensortimeout").toInt(); if (m >= 1) t.sensorTimeoutSec = m * 60; }
  t.operMode = request->arg("operMode").toInt();
  strlcpy(t.schedule, request->arg("schedule").c_str(), sizeof(t.schedule));
  // Périodes tarifaires cochées "tp_<id>"
  {
    String tp = "";
    int np2 = request->params();
    for (int k = 0; k < np2; k++) {
      const AsyncWebParameter* pr = request->getParam(k);
      if (pr->name().startsWith("tp_")) {
        if (tp.length()) tp += ",";
        tp += pr->name().substring(3);
      }
    }
    strlcpy(t.tariffPeriods, tp.c_str(), sizeof(t.tariffPeriods));
  }

  if (target == vThermostatCount) vThermostatCount++;  // valider l'ajout
  saveThermostats();

  sendThermoRedirect(request, "configThermostats");
}

// Active / désactive une zone (la désactivation coupe l'actionneur).
/* ===================== Module LoRa (ZLinky 2.4 GHz) ===================== */

// État du récepteur en JSON (rafraîchissement live de la page).
void handleLoadLora(AsyncWebServerRequest *request) {
  String j = "{\"detected\":";
  j += loraDetected ? "true" : "false";
  j += ",\"fw\":\"" + String(loraFwVersion, HEX) + "\"";
  j += ",\"channel\":" + String(LORA_OP_CHANNEL);
  j += ",\"pairing\":" + String(loraPairingMode ? "true" : "false");
  if (loraPairingMode) {
    long left = (long)LORA_PAIR_WINDOW_MS - (long)(millis() - loraPairingStartMs);
    j += ",\"pairLeft\":" + String(left > 0 ? left / 1000 : 0);
  }
  j += ",\"emitters\":[";
  bool first = true;
  for (int i = 0; i < LORA_MAX_EMITTERS; i++) {
    if (!loraEmitters[i].valid) continue;
    if (!first) j += ",";
    first = false;
    LoraEmitter& e = loraEmitters[i];
    char mac[17];
    for (int k = 0; k < 8; k++) snprintf(&mac[k * 2], 3, "%02X", e.mac[k]);
    uint32_t tot = e.rxCount + e.missed;
    j += "{\"slot\":" + String(i);
    j += ",\"mac\":\"" + String(mac) + "\"";
    j += ",\"rx\":" + String(e.rxCount);
    j += ",\"missed\":" + String(e.missed);
    j += ",\"pdr\":" + String(tot ? (int)(100.0 * e.rxCount / tot) : 0);
    j += ",\"rssi\":" + String(e.lastRssi, 0);
    j += ",\"snr\":" + String(e.lastSnr, 1);
    j += ",\"age\":" + String(e.rxCount ? (millis() - e.lastSeenMs) / 1000 : -1);
    j += "}";
  }
  j += "]}";
  request->send(200, F("application/json"), j);
}

void handleLoraPair(AsyncWebServerRequest *request) {
  loraStartPairing();
  request->send(200, "text/plain", "pairing");
}

void handleLoraRemove(AsyncWebServerRequest *request) {
  if (request->hasParam("slot")) loraRemoveEmitter(request->getParam("slot")->value().toInt());
  request->send(200, "text/plain", "ok");
}

void handleConfigLora(AsyncWebServerRequest *request) {
  if (!checkHeapForPage(request)) return;

  AsyncResponseStream *response = request->beginResponseStream("text/html");

  // === 1. Header HTML + CSS (identique a handleConfigDevices) ===
  response->print(F("<html>"));
  response->print(FPSTR(HTTP_HEADER));

  response->print(F("<style>"
    ".device-card-container{padding:8px}"
    ".config-card{background:#fff;border:none;border-radius:12px;box-shadow:0 2px 8px rgba(0,0,0,0.08);transition:transform 0.2s,box-shadow 0.2s;overflow:hidden}"
    ".config-card:hover{transform:translateY(-2px);box-shadow:0 4px 16px rgba(0,0,0,0.12)}"
    ".card-header-cfg{background:#fff;padding:12px 16px}"
    ".card-header-cfg a{color:#222;text-decoration:none;font-weight:600;font-size:14px;display:flex;align-items:center}"
    ".card-header-cfg a:hover{color:#6c757d;opacity:0.95}"
    ".card-header-cfg svg{flex-shrink:0;margin-right:8px;width:16px;height:16px}"
    ".config-card .card-body{border-top:none}"
    ".config-card .card-body table td{padding:6px 4px;border:none}"
    ".config-card .btn-actions{display:flex;flex-wrap:wrap;gap:6px;margin-top:12px;padding-top:12px;border-top:1px solid #e9ecef}"
    ".config-card .btn{padding:6px 10px;display:inline-flex;align-items:center;justify-content:center}"
    ".config-card .btn svg{width:16px;height:16px;flex-shrink:0}"
    ".lora-sep td{padding-top:10px!important;border-top:1px solid #e9ecef!important;color:#0f70b7!important;font-weight:600!important}"
    "@media(max-width:576px){"
    ".config-card{border-radius:10px}"
    ".card-header-cfg{padding:10px 12px}"
    ".card-header-cfg a{font-size:13px}"
    ".config-card .btn-actions{justify-content:center}"
    ".config-card .btn{padding:5px 8px}"
    ".config-card .btn svg{width:14px;height:14px}"
    "}"
    "</style>"));

  // === 2. Menu ===
  streamSection(response, HTTP_MENU);

  // === 3. Entete de page : meme gabarit que la page Zigbee ===
  response->print(F("<div class='row p-4 justify-content-md-center'>"
    "<div class='col-sm-2'><div class='btn-group-horizontal'>"));
  {
    String menuLora = FPSTR(HTTP_CONFIG_MENU_LORA);
    menuLora.replace("{{menu_config_lora}}", "disabled");
    response->print(menuLora);
  }
  response->print(F("</div></div><div class='col-sm-10'>"
    "<h4>Config appareils LoRa</h4>"
    "<div class='d-flex justify-content-end'>"
    "<a class='btn btn-primary mb-1' href='/assistDevice?type=lora' style='width:120px;height:64px;'>"
    "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' class='bi bi-plus-circle' viewBox='0 0 16 16'>"
    "<path d='M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16'/>"
    "<path d='M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4'/>"
    "</svg><br> Ajouter</a></div><br>"));

  // Sans radio detectee la page n'a rien a montrer : on le dit plutot que d'afficher une
  // liste vide qui ferait croire a une absence d'appareils.
  if (!loraDetected) {
    response->print(F("<div align='center' style='height:100px;font-size:20px;font-weight:bold;color:#c0392b;'>"
      "Aucun module LoRa d&eacute;tect&eacute;</div>"));
    response->print(F("</div></div>"));
    response->print(footer());
    response->print(F("</html>"));
    request->send(response);
    return;
  }
  response->print(F("<h5>Liste des appareils</h5>"
    "<div class='row g-4' style='font-size:12px;'>"));

  // === 4. Boucle devices : uniquement ceux qui arrivent par LoRa ===
  int exist = 0;
  bool mqttHA = (ConfigSettings.enableMqtt && ConfigGeneral.HAMQTT);

  for (size_t ident = 0; ident < devices.size(); ident++)
  {
    DeviceData* device = devices[ident];
    int slot = loraFindEmitterByMac(device->getDeviceID());
    if (slot < 0) continue;               // appareil Zigbee : il a deja sa page
    LoraEmitter &em = loraEmitters[slot];
    exist++;

    response->print(F("<div class='col-12 col-sm-6 col-md-4 col-lg-3 device-card-container'>"
      "<div class='config-card'><div class='card-header-cfg'><a href='/configDevice?id="));
    response->print(device->getDeviceID());
    response->print(F("'>"));

    if (LittleFS.exists("/web/img/icon_" + device->getInfo().model + ".png")) {
      response->print(F("<img src='web/img/icon_"));
      response->print(device->getInfo().model);
      response->print(F(".png' height='64px'/>"));
    } else {
      response->print(F("<img src='web/img/icon_"));
      response->print(device->getInfo().device_id);
      response->print(F(".png' height='64px'/>"));
    }

    response->print(device->getInfo().alias.length() > 0 ? device->getInfo().alias : device->getDeviceID());
    response->print(F("</a></div><div class='card-body' style='padding:12px 16px;'>"
      "<table style='width:100%;font-size:12px;'><tr>"
      "<td style='color:#6c757d;font-weight:500;'>Manufacturer</td><td style='font-family:Courier New,monospace;text-align:right;'>"));
    response->print(device->getInfo().manufacturer);
    response->print(F("</td></tr><tr><td style='color:#6c757d;font-weight:500;'>Model</td><td style='font-family:Courier New,monospace;text-align:right;'>"));
    response->print(device->getInfo().model);
    response->print(F("</td></tr><tr><td style='color:#6c757d;font-weight:500;'>Device Id</td><td style='font-family:Courier New,monospace;text-align:right;'>"));
    response->printf("%04X", (unsigned int)device->getInfo().device_id.toInt());
    response->print(F("</td></tr><tr><td style='color:#6c757d;font-weight:500;'>Last seen</td><td style='font-family:Courier New,monospace;text-align:right;'>"));
    response->print(device->getInfo().lastSeen);
    response->print(F("</td></tr>"));

    // --- Donnees propres au lien LoRa (remplacent Short Addr / LQI / Soft version, qui
    //     n'ont pas d'equivalent radio) ---
    uint32_t total = em.rxCount + em.missed;
    int pdr = total ? (int)((em.rxCount * 100) / total) : 0;
    long age = em.lastSeenMs ? (long)((millis() - em.lastSeenMs) / 1000) : -1;
    String ageTxt = (age < 0) ? String("jamais") : (age < 90 ? String(age) + " s" : String(age / 60) + " min");

    response->print(F("<tr class='lora-sep'><td colspan='2'>Lien LoRa</td></tr>"));
    response->printf("<tr><td style='color:#6c757d;font-weight:500;'>RSSI</td><td style='font-family:Courier New,monospace;text-align:right;'>%.0f dBm</td></tr>", em.lastRssi);
    response->printf("<tr><td style='color:#6c757d;font-weight:500;'>SNR</td><td style='font-family:Courier New,monospace;text-align:right;'>%.0f dB</td></tr>", em.lastSnr);
    response->printf("<tr><td style='color:#6c757d;font-weight:500;'>PDR</td><td style='font-family:Courier New,monospace;text-align:right;'>%d %%</td></tr>", pdr);
    response->printf("<tr><td style='color:#6c757d;font-weight:500;'>Trames</td><td style='font-family:Courier New,monospace;text-align:right;'>%u re&ccedil;ues / %u perdues</td></tr>",
                     (unsigned)em.rxCount, (unsigned)em.missed);
    if (em.modeKnown) {
      response->printf("<tr><td style='color:#6c757d;font-weight:500;'>Mode Linky</td><td style='font-family:Courier New,monospace;text-align:right;'>%s %s</td></tr>",
                       (em.linkyMode & 0x01) ? "Standard" : "Historique",
                       (em.linkyMode & 0x02) ? "tri" : "mono");
    }
    response->printf("<tr><td style='color:#6c757d;font-weight:500;'>Derni&egrave;re trame</td><td style='font-family:Courier New,monospace;text-align:right;color:%s;'>%s</td></tr>",
                     (age < 0 || age > 300) ? "#c0392b" : "#27ae60", ageTxt.c_str());
    response->print(F("</table>"));

    // Boutons : ni OTA ni Refresh, qui supposent une requete Zigbee vers l'appareil.
    response->print(F("<div class='btn-actions'>"));

    response->print(F("<a href='/configDevice?id="));
    response->print(device->getDeviceID());
    response->print(F("' class='btn btn-info' title='Fiche appareil'>"
      "<svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'>"
      "<path d='M14.5 3a.5.5 0 0 1 .5.5v9a.5.5 0 0 1-.5.5h-13a.5.5 0 0 1-.5-.5v-9a.5.5 0 0 1 .5-.5zm-13-1A1.5 1.5 0 0 0 0 3.5v9A1.5 1.5 0 0 0 1.5 14h13a1.5 1.5 0 0 0 1.5-1.5v-9A1.5 1.5 0 0 0 14.5 2z'/>"
      "<path d='M5 8a.5.5 0 0 1 .5-.5h7a.5.5 0 0 1 0 1h-7A.5.5 0 0 1 5 8m0-2.5a.5.5 0 0 1 .5-.5h7a.5.5 0 0 1 0 1h-7a.5.5 0 0 1-.5-.5m0 5a.5.5 0 0 1 .5-.5h7a.5.5 0 0 1 0 1h-7a.5.5 0 0 1-.5-.5m-1-5a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0M4 8a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0m0 2.5a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0'/>"
      "</svg></a>"));

    // MQTT Discover : on passe la MAC, l'appareil LoRa n'ayant pas d'adresse courte.
    if (mqttHA) {
      response->printf("<button onclick=\"sendMqttDiscover('%s');\" class='btn btn-warning' title='MQTT Discover'>",
        device->getDeviceID().c_str());
      response->print(F("<svg viewBox='0 0 24 24' xmlns='http://www.w3.org/2000/svg' fill='currentColor'>"
        "<path d='M10.657 23.994h-9.45A1.212 1.212 0 0 1 0 22.788v-9.18h0.071c5.784 0 10.504 4.65 10.586 10.386Zm7.606 0h-4.045C14.135 16.246 7.795 9.977 0 9.942V6.038h0.071c9.983 0 18.121 8.044 18.192 17.956Zm4.53 0h-0.97C21.754 12.071 11.995 2.407 0 2.372v-1.16C0 0.55 0.544 0.006 1.207 0.006h7.64C15.733 2.49 21.257 7.789 24 14.508v8.291c0 0.663 -0.544 1.195 -1.207 1.195ZM16.713 0.006h6.092A1.19 1.19 0 0 1 24 1.2v5.914c-0.91 -1.242 -2.046 -2.65 -3.158 -3.762C19.588 2.11 18.122 0.987 16.714 0.005Z'/>"
        "</svg></button>"));
    }

    response->printf("<button onclick=\"deleteLoraDevice('%s',%d);\" class='btn btn-danger' title='Supprimer'>",
      device->getDeviceID().c_str(), slot);
    response->print(F("<svg xmlns='http://www.w3.org/2000/svg' fill='currentColor' viewBox='0 0 16 16'>"
      "<path d='M5.5 5.5A.5.5 0 0 1 6 6v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m2.5 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 1-1 0V6a.5.5 0 0 1 .5-.5m3 .5a.5.5 0 0 0-1 0v6a.5.5 0 0 0 1 0z'/>"
      "<path d='M14.5 3a1 1 0 0 1-1 1H13v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4h-.5a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1H6a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1h3.5a1 1 0 0 1 1 1zM4.118 4 4 4.059V13a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V4.059L11.882 4zM2.5 3h11V2h-11z'/>"
      "</svg></button>"));

    response->print(F("</div></div></div></div>"));

    vTaskDelay(1); // Eviter watchdog timeout
  }

  if (exist == 0) {
    response->print(F("<div align='center' style='height:100px;font-size:28px;font-weight:bold;'>No devices yet</div>"));
  }

  // Suppression : desappairer l'emetteur AVANT de supprimer l'appareil, sinon la prochaine
  // trame recreerait la fiche aussitot.
  response->print(F("<script>"
    "function deleteLoraDevice(devId,slot){"
    "if(confirm('Supprimer cet appareil LoRa et son appairage ?')){"
    "var xhr=getXhr();"
    "xhr.onreadystatechange=function(){"
    "if(xhr.readyState==4){"
    "if(xhr.status==200){"
    "var x2=getXhr();"
    "x2.onreadystatechange=function(){if(x2.readyState==4){window.location.href='/configLora';}};"
    "x2.open('GET','deleteDevice?devId='+encodeURIComponent(devId),true);x2.send();"
    "}else{alert('Erreur lors du desappairage');}}};"
    "xhr.open('GET','loraRemove?slot='+slot,true);"
    "xhr.send();}}</script>"));

  // === 5. Fermeture + footer ===
  response->print(F("</div></div>"));
  response->print(footer());
  response->print(F("</html>"));

  request->send(response);
}

void handleToggleThermostat(AsyncWebServerRequest *request) {
  if (request->hasParam("id")) {
    int id = request->getParam("id")->value().toInt();
    if (id >= 0 && id < vThermostatCount) {
      setThermostatEnabled(id, !vThermostats[id].enabled);
    }
  }
  sendThermoRedirect(request, "configThermostats");
}

// Supprime la zone id (décale les suivantes) et persiste.
void handleDeleteThermostat(AsyncWebServerRequest *request) {
  if (request->hasParam("id")) {
    int id = request->getParam("id")->value().toInt();
    if (id >= 0 && id < vThermostatCount) {
      for (int i = id; i < vThermostatCount - 1; i++) {
        vThermostats[i] = vThermostats[i + 1];
      }
      vThermostatCount--;
      saveThermostats();
    }
  }
  sendThermoRedirect(request, "configThermostats");
}

void initWebServer()
{
  static bool webServerInitialized = false;
  if (webServerInitialized) {
    Serial.println("[WebServer] Already initialized, skipping");
    return;
  }
  webServerInitialized = true;

  // ==================== Login / Logout Routes ====================
  serverWeb.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
    // If already authenticated, redirect to home
    if (ConfigSettings.enableSecureHttp) {
      String token = getSessionCookie(request);
      if (isValidSession(token)) {
        request->redirect("/");
        return;
      }
    }
    String page = FPSTR(HTTP_LOGIN);
    if (request->hasArg("error")) {
        page.replace("{{errorDisplay}}", "");
        page.replace("{{errorMsg}}", "Identifiant ou mot de passe incorrect");
    } else {
        page.replace("{{errorDisplay}}", "d-none");
        page.replace("{{errorMsg}}", "");
    }
    page.replace("{{version}}", VERSION);
    request->send(200, "text/html", page);
  });

  serverWeb.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
    String user = request->arg("user");
    String pass = request->arg("pass");

    if (user == ConfigGeneral.userHTTP && pass == ConfigGeneral.passHTTP) {
        String token = createSession();
        AsyncWebServerResponse *response = request->beginResponse(303);
        response->addHeader("Location", "/");
        response->addHeader("Set-Cookie",
            "session=" + token + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=86400");
        request->send(response);
    } else {
        AsyncWebServerResponse *response = request->beginResponse(303);
        response->addHeader("Location", "/login?error=1");
        request->send(response);
    }
  });

  serverWeb.on("/logout", HTTP_GET, [](AsyncWebServerRequest *request) {
    String token = getSessionCookie(request);
    if (token.length() > 0) deleteSession(token);

    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader("Location", "/login");
    response->addHeader("Set-Cookie", "session=; Path=/; Max-Age=0");
    request->send(response);
  });
  // ==================== End Login / Logout Routes ====================

  // serverWeb.on("/", handleRoot);
  serverWeb.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleRoot(request);
  });
  serverWeb.on("/dashboard", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    //request->client()->setRxTimeout(15);
    if (!checkAuth(request)) return;
    handleDashboard(request);
  });
  serverWeb.on("/statusEnergy", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleStatusEnergy(request);
  });

  // ==================== Thermostat virtuel ====================
  serverWeb.on("/thermostats", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleThermostats(request);
  });
  serverWeb.on("/loadThermostats", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleLoadThermostats(request);
  });
  serverWeb.on("/deviceActions", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleDeviceActions(request);
  });
  serverWeb.on("/setThermostatSetpoint", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleSetThermostatSetpoint(request);
  });
  serverWeb.on("/setThermostatForce", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleSetThermostatForce(request);
  });
  serverWeb.on("/setThermostatMode", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleSetThermostatMode(request);
  });
  serverWeb.on("/setThermostatFrost", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleSetThermostatFrost(request);
  });
  serverWeb.on("/configThermostats", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleConfigThermostats(request);
  });
  serverWeb.on("/editThermostat", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleEditThermostat(request);
  });
  serverWeb.on("/saveThermostat", HTTP_POST, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleSaveThermostat(request);
  });
  // === Module LoRa ===
  serverWeb.on("/configLora", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleConfigLora(request);
  });
  serverWeb.on("/loadLora", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleLoadLora(request);
  });
  serverWeb.on("/loraPair", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleLoraPair(request);
  });
  serverWeb.on("/loraRemove", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleLoraRemove(request);
  });
  serverWeb.on("/toggleThermostat", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleToggleThermostat(request);
  });
  serverWeb.on("/deleteThermostat", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleDeleteThermostat(request);
  });

  /*serverWeb.on("/statusEnergyTV", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleStatusEnergyTV(request);
  });*/

  // ==================== Tunnel API ====================
  serverWeb.on("/api/tunnelStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) return;
    bool connected = (tunnel != nullptr && tunnel->isConnected());
    String json = "{\"enabled\":";
    json += ConfigGeneral.enableTunnel ? "true" : "false";
    json += ",\"connected\":";
    json += connected ? "true" : "false";
    if (connected) {
      String sub = tunnel->getSubdomain();
      if (sub.length() > 0) {
        json += ",\"url\":\"https://";
        json += sub;
        json += "\"";
      }
    }
    json += "}";
    request->send(200, "application/json", json);
  });

  serverWeb.on("/api/tunnelActivate", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) return;

    if (!ConfigSettings.enableSecureHttp) {
      request->send(200, "application/json",
        "{\"status\":\"error\",\"error\":\"L'acc\\u00e8s s\\u00e9curis\\u00e9 HTTP doit \\u00eatre activ\\u00e9 avant d'activer le tunnel.\"}");
      return;
    }

    String code = request->arg("code");
    if (code.length() != 6) {
      request->send(200, "application/json", "{\"status\":\"error\",\"error\":\"Code invalide\"}");
      return;
    }
    // Vérifier qu'une activation n'est pas déjà en cours
    if (tunnelActivation.pending || tunnelActivation.processing) {
      request->send(200, "application/json", "{\"status\":\"processing\"}");
      return;
    }
    tunnelActivation.reset();
    strlcpy(tunnelActivation.code, code.c_str(), sizeof(tunnelActivation.code));
    tunnelActivation.requestTime = millis();
    tunnelActivation.pending = true;
    request->send(200, "application/json", "{\"status\":\"pending\"}");
  });

  serverWeb.on("/api/tunnelActivateStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) return;
    if (tunnelActivation.done) {
      String json = "{\"status\":\"done\",\"success\":";
      json += tunnelActivation.success ? "true" : "false";
      if (tunnelActivation.success) {
        json += ",\"deviceId\":\"" + tunnelActivation.deviceId + "\"";
      } else {
        json += ",\"error\":\"" + tunnelActivation.error + "\"";
      }
      json += "}";
      request->send(200, "application/json", json);
      tunnelActivation.reset();
    } else if (tunnelActivation.processing || tunnelActivation.pending) {
      request->send(200, "application/json", "{\"status\":\"processing\"}");
    } else {
      request->send(200, "application/json", "{\"status\":\"idle\"}");
    }
  });

  // GET /api/tunnel/credentials — retourne clientId + token en clair
  serverWeb.on("/api/tunnel/credentials", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) return;
    SpiRamJsonDocument doc(256);
    doc["tunnelClientId"] = ConfigGeneral.tunnelClientId;
    doc["tunnelToken"] = ConfigGeneral.tunnelToken;
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // POST /api/tunnel/credentials — met à jour clientId et/ou token
  serverWeb.on("/api/tunnel/credentials", HTTP_POST,
    [](AsyncWebServerRequest *request){},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!checkAuth(request)) return;
      static String jsonBuffer;
      if (index == 0) { jsonBuffer = ""; jsonBuffer.reserve(total); }
      for (size_t i = 0; i < len; i++) jsonBuffer += (char)data[i];
      if (index + len < total) return;

      SpiRamJsonDocument doc(256);
      if (deserializeJson(doc, jsonBuffer)) {
        jsonBuffer = "";
        request->send(400, "application/json", "{\"error\":\"JSON invalide\"}");
        return;
      }
      jsonBuffer = "";

      String path = "configGeneral.json";
      bool changed = false;

      if (doc.containsKey("tunnelClientId")) {
        strlcpy(ConfigGeneral.tunnelClientId, doc["tunnelClientId"] | "", sizeof(ConfigGeneral.tunnelClientId));
        config_write(path, "tunnelClientId", String(ConfigGeneral.tunnelClientId));
        changed = true;
      }
      if (doc.containsKey("tunnelToken")) {
        const char* token = doc["tunnelToken"] | "";
        if (strlen(token) > 0 && strcmp(token, "********") != 0) {
          strlcpy(ConfigGeneral.tunnelToken, token, sizeof(ConfigGeneral.tunnelToken));
          config_write(path, "tunnelToken", String(ConfigGeneral.tunnelToken));
          changed = true;
        }
      }

      if (changed && ConfigGeneral.enableTunnel) {
        if (tunnel != nullptr) {
          tunnel->stop();
          delete tunnel;
          tunnel = nullptr;
        }
        if (strlen(ConfigGeneral.tunnelToken) > 0) {
          String tunnelUrl = "wss://remote.lixee-box.fr/tunnel?token=";
          tunnelUrl += ConfigGeneral.tunnelToken;
          if (strlen(ConfigGeneral.tunnelClientId) > 0) {
            tunnelUrl += "&clientId=";
            tunnelUrl += ConfigGeneral.tunnelClientId;
          }
          tunnel = new LiXeeBoxTunnel(tunnelUrl.c_str(), 80);
          tunnel->begin();
          Serial.println("[Tunnel] Service tunnel restarted with new credentials");
        }
      }

      request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
  );

  serverWeb.on("/api/tariff", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleAPITariff(request);
  });


  serverWeb.on("/statusNetwork", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleStatusNetwork(request);
  });
  serverWeb.on("/statusDevices", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleStatusDevices(request); 
  });
  serverWeb.on("/configGeneral", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleConfigGeneral(request); 
  });
  serverWeb.on("/configZigbee", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleConfigZigbee(request); 
  });
  serverWeb.on("/configHorloge", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleConfigHorloge(request); 
  });
  serverWeb.on("/configEnergy", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleConfigEnergy(request);
  });
  serverWeb.on("/configNotifications", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleConfigNotifications(request);
  });
  serverWeb.on("/configGaz", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleConfigGaz(request); 
  });
  serverWeb.on("/configWater", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleConfigWater(request); 
  });
  serverWeb.on("/getPresenceSummary", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) return;
    handleGetPresenceSummary(request);
  });
  
  serverWeb.on("/getPresenceHistory", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) return;
    handleGetPresenceHistory(request);
  });
  
  serverWeb.on("/getPresenceStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) return;
    handleGetPresenceStatus(request);
  });
  
  serverWeb.on("/getPresenceAll", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) return;
    handleGetPresenceAll(request);
  });
  serverWeb.on("/getMQTTStatus", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleGetMQTTStatus(request); 
  });
  serverWeb.on("/configMQTT", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleConfigMQTT(request); 
  });
  serverWeb.on("/configHTTP", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleConfigHTTP(request); 
  });
  serverWeb.on("/configRules", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleConfigRules(request); 
  });
  serverWeb.on("/editRule", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleEditRule(request);
  });
  serverWeb.on("/api/rules/delete", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
    { 
      if (!checkAuth(request)) return;
      APIDeleteRule(request, data, len, index, total);
    }
  );
  serverWeb.on("/api/rules/edit", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
    { 
      if (!checkAuth(request)) return;
      APIEditRule(request, data, len, index, total);
    }
  );
  serverWeb.on("/api/rules/toggle", HTTP_POST,
    [](AsyncWebServerRequest *request){},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
    {
      if (!checkAuth(request)) return;
      APIToggleRule(request, data, len, index, total);
    }
  );
  serverWeb.on("/addRule", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleAddRule(request);
  });

  serverWeb.on("/api/rules/add", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
    { 
      if (!checkAuth(request)) return;
      APIAddRule(request, data, len, index, total);
    }
  );

  // Dans initWebServer(), ajouter :
  serverWeb.on("/getSubMeters", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (!checkAuth(request)) return;
      APIgetSubMeters(request);
  });

  serverWeb.on("/setSubMeter", HTTP_POST, [](AsyncWebServerRequest *request) {
      if (!checkAuth(request)) return;
      APIsetSubMeter(request);
  });

  serverWeb.on("/deleteSubMeter", HTTP_POST, [](AsyncWebServerRequest *request) {
      if (!checkAuth(request)) return;
      APIdeleteSubMeter(request);
  });

  serverWeb.on("/getEligibleSubMeters", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) return;
    APIgetEligibleSubMeters(request);
  });

  serverWeb.on("/configWebPush", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleConfigWebPush(request);
  });

  serverWeb.on("/configTunnel", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleConfigTunnel(request);
  });
  serverWeb.on("/configUdpClient", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleConfigUdpClient(request); 
  });
  serverWeb.on("/configNotifMail", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleConfigNotificationMail(request); 
  });
  serverWeb.on("/configWiFi", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleConfigWifi(request); 
  });
  
  serverWeb.on("/configDevices", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleConfigDevices(request); 
  });
  serverWeb.on("/configDevice", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleConfigDevice(request); 
  });
  serverWeb.on("/assistDevice", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleAssistDevice(request); 
  });

  serverWeb.on("/downloadUpdate", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    request->send(200,"text/plain","starting download");
    updatePending = true;
  });
  serverWeb.on("/update", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleToolUpdate(request);
  });
  /* GESTIONNAIRE DE FICHIERS - desactive temporairement
  serverWeb.on("/filesManager", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleFilesManager(request);
  });
  serverWeb.on("/deleteFile", HTTP_POST, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleDeleteFile(request);
  });
  */
  serverWeb.on("/backup", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleToolBackup(request); 
  });
  serverWeb.on("/createBackupFile", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleToolCreateBackup(request); 
  });

  serverWeb.on("/deleteBackupFile", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    if (request->hasArg("filename"))
    {
      String argFilename = request->arg("filename");
      if (argFilename.indexOf("..") >= 0) {
        request->send(400, F("text/plain"), F("Invalid path"));
        return;
      }
      String filename = "/bk/" + argFilename;
      if (LittleFS.exists(filename))
      {
        LittleFS.remove(filename);
        request->send(200, F("text/plain"), F("OK"));
      }
      else
      {
        request->send(404, F("text/plain"), F("File not found"));
      }
    }
    else
    {
      request->send(400, F("text/plain"), F("Missing filename"));
    }
  });
  serverWeb.on("/restoreBkFile", HTTP_POST, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    if (!request->hasArg("filename"))
    {
      request->send(400, F("text/plain"), F("Missing filename"));
      return;
    }
    String argFilename = request->arg("filename");
    if (argFilename.indexOf("..") >= 0 || !argFilename.endsWith(".json"))
    {
      request->send(400, F("text/plain"), F("Invalid path"));
      return;
    }
    String srcPath = "/bk/" + argFilename;
    String dstPath = "/db/" + argFilename;
    File src = LittleFS.open(srcPath, "r");
    if (!src)
    {
      request->send(404, F("text/plain"), F("Backup not found"));
      return;
    }
    File dst = LittleFS.open(dstPath, "w");
    if (!dst)
    {
      src.close();
      request->send(500, F("text/plain"), F("Cannot write to /db/"));
      return;
    }
    uint8_t buffer[512];
    size_t n;
    while ((n = src.read(buffer, sizeof(buffer))) > 0)
    {
      dst.write(buffer, n);
    }
    src.close();
    dst.close();
    request->send(200, F("text/plain"), F("OK"));
  });
  serverWeb.on("/saveDebug", HTTP_POST, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleSaveDebug(request);  
  });
  serverWeb.on("/saveFileConfig", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveConfig(request);  
  });
  serverWeb.on("/saveFileDevice", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveDevice(request); 
  });
  serverWeb.on("/saveFileHistory", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveHistory(request); 
  });
  

  serverWeb.on("/saveFileTemplates", HTTP_POST, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleSaveTemplates(request);
  });

  // Chunked template save via tunnel
  serverWeb.on("/templateSaveInit", HTTP_POST,
    [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        handleTemplateSaveInitResponse(request);
    }
  );
  serverWeb.on("/templateSaveChunk", HTTP_POST,
    [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        handleTemplateSaveChunkResponse(request);
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkAuth(request)) return;
        _crAccumBody(request, data, len, index, total);
    }
  );
  serverWeb.on("/templateSaveFinish", HTTP_POST,
    [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        handleTemplateSaveFinishResponse(request);
    }
  );

  serverWeb.on("/saveFileRules", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveRules(request); 
  });
  
  serverWeb.on("/saveFileJavascript", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveJavascript(request); 
  });
  serverWeb.on("/saveFileDatabase", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveDatabase(request); 
  });
  serverWeb.on("/saveConfigGeneral", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveConfigGeneral(request); 
  });
  serverWeb.on("/saveConfigHorloge", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveConfigHorloge(request); 
  });
  serverWeb.on("/saveConfigLinky", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveConfigLinky(request); 
  });
  serverWeb.on("/saveConfigProduction", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveConfigProduction(request); 
  });
  serverWeb.on("/saveConfigGaz", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveConfigGaz(request); 
  });
  serverWeb.on("/saveConfigWater", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveConfigWater(request); 
  });
  serverWeb.on("/saveConfigPresence", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveConfigPresence(request); 
  });

  serverWeb.on("/saveConfigMQTT", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveConfigMQTT(request); 
  });
  serverWeb.on("/saveConfigHTTP", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveConfigHTTP(request); 
  });
  serverWeb.on("/saveConfigWebPush", HTTP_POST, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleSaveConfigWebPush(request);
  });

  serverWeb.on("/saveConfigTunnel", HTTP_POST, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleSaveConfigTunnel(request);
  });

  serverWeb.on("/saveConfigUDPClient", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveConfigUDPClient(request); 
  });

  serverWeb.on("/saveConfigNotificationMail", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveConfigNotificationMail(request); 
  });

  serverWeb.on("/saveConfigParameter", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveConfigParameter(request); 
  });

  serverWeb.on("/saveConfigNotifications", HTTP_POST, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleSaveConfigNotifications(request);
  });

  serverWeb.on("/saveWifi", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSaveWifi(request); 
  });

  // API: Obtenir les notifications (avec pagination)
  serverWeb.on("/api/notifications", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!checkAuth(request)) return;
    int page = request->hasParam("page") ? request->getParam("page")->value().toInt() : 1;
    int limit = request->hasParam("limit") ? request->getParam("limit")->value().toInt() : 10;
    
    // Validation des paramètres
    page = max(1, page);
    limit = min(max(1, limit), 50); // Limite entre 1 et 50
    
    size_t offset = (page - 1) * limit;
        
    String json = notificationManager.toJson(offset, limit);
    
    request->send(200, "application/json", json);
  });
  
  // API: Ajouter une notification
  serverWeb.on("/api/notifications", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!checkAuth(request)) return;
  }, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      if (!checkAuth(request)) return;
      SpiRamJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, (char*)data);
      
      if (error) {
        request->send(400, "application/json", "{\"error\":\"JSON invalide\"}");
        return;
      }
      
      String title = doc["title"] | "";
      String message = doc["message"] | "";
      int type = doc["type"] | 0;
      
      if (title.isEmpty() || message.isEmpty()) {
        request->send(400, "application/json", "{\"error\":\"Title et message requis\"}");
        return;
      }
      
      bool success = notificationManager.addNotification(title, message, type);
      
      if (success) {
        request->send(200, "application/json", "{\"success\":true}");
      } else {
        request->send(500, "application/json", "{\"error\":\"Erreur ajout notification\"}");
      }
    });
  
  // API: Marquer comme lu - Route simplifiée
  serverWeb.on("/api/notifications/read", HTTP_PUT, [](AsyncWebServerRequest *request){
    if (!checkAuth(request)) return;
    if (!request->hasParam("id")) {
      log_e("Erreur markAsRead: paramètre id manquant");
      request->send(400, "application/json", "{\"error\":\"Paramètre id manquant\"}");
      return;
    }
    
    int index = request->getParam("id")->value().toInt();
   
    bool success = notificationManager.markAsViewed(index);
    
    if (success) {
      request->send(200, "application/json", "{\"success\":true}");
    } else {
      log_e("Erreur: notification %d non trouvée\n", index);
      request->send(404, "application/json", "{\"error\":\"Notification non trouvée\"}");
    }
  });

  // API: Marquer TOUTES les notifications comme lues - NOUVEAU
  serverWeb.on("/api/notifications/read-all", HTTP_PUT, [](AsyncWebServerRequest *request){
    if (!checkAuth(request)) return;
    bool success = notificationManager.markAllAsViewed();
    
    if (success) {
      Serial.println("Toutes les notifications marquées comme lues");
      request->send(200, "application/json", "{\"success\":true,\"message\":\"Toutes les notifications marquées comme lues\"}");
    } else {
      log_e("Erreur lors du marquage global");
      request->send(500, "application/json", "{\"error\":\"Erreur lors du marquage global\"}");
    }
  });
  
  // API: Supprimer une notification - Route simplifiée
  serverWeb.on("/api/notifications/delete", HTTP_DELETE, [](AsyncWebServerRequest *request){
    if (!checkAuth(request)) return;
    if (!request->hasParam("id")) {
      log_e("Erreur deleteNotification: paramètre id manquant");
      request->send(400, "application/json", "{\"error\":\"Paramètre id manquant\"}");
      return;
    }
    
    int index = request->getParam("id")->value().toInt();
    Serial.printf("Tentative deleteNotification index: %d, total: %d\n", index, notificationManager.getCount());
    
    bool success = notificationManager.deleteNotification(index);
    
    if (success) {
      request->send(200, "application/json", "{\"success\":true}");
    } else {
      log_e("Erreur: notification %d non trouvée\n", index);
      request->send(404, "application/json", "{\"error\":\"Notification non trouvée\"}");
    }
  });
  
  // API: Statistiques
  serverWeb.on("/api/stats", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!checkAuth(request)) return;
    String json = notificationManager.getStatsJson();
    request->send(200, "application/json", json);
  });
  
  // API: Debug - Lister les notifications avec leurs index
  serverWeb.on("/api/debug", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!checkAuth(request)) return;
    SpiRamJsonDocument doc(2048);
    JsonArray array = doc.createNestedArray("notifications");
    
    for (size_t i = 0; i < notificationManager.getCount(); i++) {
      Notification* notif = notificationManager.getNotification(i);
      if (notif) {
        JsonObject obj = array.createNestedObject();
        obj["index"] = i;
        obj["title"] = notif->title;
        obj["viewed"] = notif->viewed;
      }
    }
    
    doc["total"] = notificationManager.getCount();
    doc["psramFree"] = ESP.getFreePsram();
    
    String result;
    serializeJson(doc, result);
    request->send(200, "application/json", result);
  });
  
  // API: Vider toutes les notifications
  serverWeb.on("/api/notifications/clear", HTTP_DELETE, [](AsyncWebServerRequest *request){
    if (!checkAuth(request)) return;
    notificationManager.clearAll();
    notificationManager.saveToFile();
    request->send(200, "application/json", "{\"success\":true}");
  });

  serverWeb.on("/notifications", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleNotifications(request); 
  });

  serverWeb.on("/tools", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleTools(request); 
  });
  serverWeb.on("/logs", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleLogs(request); 
  });
  serverWeb.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleReboot(request); 
  });
  serverWeb.on("/update", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleToolUpdate(request); 
  });
  serverWeb.on("/doUpdate", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data,
                  size_t len, bool final) 
        {
          if (!checkAuth(request)) return;
          handleDoUpdate(request, filename, index, data, len, final);
        }
  );
  serverWeb.on("/doRestore", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data,
                  size_t len, bool final)
        {
          if (!checkAuth(request)) return;
          handleDoRestore(request, filename, index, data, len, final);
        }
  );

  // Chunked restore via tunnel (small HTTP requests under 15KB WS limit)
  serverWeb.on("/restoreInit", HTTP_POST,
    [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        handleRestoreInitResponse(request);
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkAuth(request)) return;
        _crAccumBody(request, data, len, index, total);
    }
  );
  serverWeb.on("/restoreChunk", HTTP_POST,
    [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        handleRestoreChunkResponse(request);
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkAuth(request)) return;
        _crAccumBody(request, data, len, index, total);
    }
  );
  serverWeb.on("/restoreFinish", HTTP_POST,
    [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        handleRestoreFinishResponse(request);
    }
  );

  // Chunked firmware update via tunnel (ESP32 .bin flash)
  serverWeb.on("/fwUpdateInit", HTTP_POST,
    [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        handleFwUpdateInitResponse(request);
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkAuth(request)) return;
        _crAccumBody(request, data, len, index, total);
    }
  );
  serverWeb.on("/fwUpdateChunk", HTTP_POST,
    [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        handleFwUpdateChunkResponse(request);
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkAuth(request)) return;
        _crAccumBody(request, data, len, index, total);
    }
  );
  serverWeb.on("/fwUpdateFinish", HTTP_POST,
    [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        handleFwUpdateFinishResponse(request);
    }
  );

  // Chunked OTA upload via tunnel
  serverWeb.on("/otaInit", HTTP_POST,
    [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        handleOtaInitResponse(request);
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkAuth(request)) return;
        _crAccumBody(request, data, len, index, total);
    }
  );
  serverWeb.on("/otaChunk", HTTP_POST,
    [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        handleOtaChunkResponse(request);
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkAuth(request)) return;
        _crAccumBody(request, data, len, index, total);
    }
  );
  serverWeb.on("/otaFinish", HTTP_POST,
    [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        handleOtaFinishResponse(request);
    }
  );

  serverWeb.on("/doUploadHistory", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data,
                  size_t len, bool final) 
        {
          if (!checkAuth(request)) return;
          handleDoUploadHistory(request, filename, index, data, len, final);
        }
  );

  serverWeb.on("/doUploadOTA", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data,
                  size_t len, bool final) 
        {
          if (!checkAuth(request)) return;
          handleDoUploadOTA(request, filename, index, data, len, final);
        }
  );

  // ZiGate Flash routes
  serverWeb.on("/flashZigate", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data,
                  size_t len, bool final)
        {
          if (!checkAuth(request)) return;
          handleZigateFlashUpload(request, filename, index, data, len, final);
        }
  );

  serverWeb.on("/zigateFlashStatus", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleZigateFlashStatus(request);
  });

  serverWeb.on("/readFile", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleReadfile(request); 
  });
  serverWeb.on("/getLogBuffer", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleLogBuffer(request); 
  });
  serverWeb.on("/scanNetwork", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleScanNetwork(request); 
  });
  serverWeb.on("/cmdClearConsole", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleClearConsole(request); 
  });
  serverWeb.on("/cmdGetVersion", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleGetVersion(request); 
  });

  serverWeb.on("/cmdErasePDM", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleErasePDM(request); 
  });
  serverWeb.on("/cmdStartNwk", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleStartNwk(request); 
  });

  serverWeb.on("/cmdSetLed", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSetLed(request); 
  });
  serverWeb.on("/cmdSetChannelMask", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSetChannelMask(request); 
  });
  serverWeb.on("/cmdPermitJoin", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handlePermitJoin(request); 
  });

  serverWeb.on("/cmdPermitJoinAssist", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handlePermitJoinAssist(request);
  });

  // Pendant LoRa de /cmdPermitJoinAssist : ouvre la fenêtre d'appairage radio. Appelé par
  // l'assistant via cmd("LoraPairAssist") quand il est lancé avec ?type=lora.
  serverWeb.on("/cmdLoraPairAssist", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    loraStartPairing();
    request->send(200, F("text/html"), "");
  });
  serverWeb.on("/cmdRawMode", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleRawMode(request); 
  });
  serverWeb.on("/cmdRawModeOff", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleRawModeOff(request); 
  });
  serverWeb.on("/cmdActiveReq", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleActiveReq(request); 
  });
  serverWeb.on("/cmdReset", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleReset(request); 
  });
  serverWeb.on("/cmdNetwork", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleNetwork(request); 
  });
  serverWeb.on("/configFiles", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleConfigFiles(request); 
  });
  serverWeb.on("/debugFiles", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleDebugFiles(request); 
  });
  serverWeb.on("/fsbrowser", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleFSbrowser(request); 
  });
  serverWeb.on("/fsbrowserBackup", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleFSbrowserBackup(request); 
  });
  serverWeb.on("/hst", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleHistory(request); 
  });
  serverWeb.on("/tp", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleTemplates(request); 
  });
   serverWeb.on("/rules", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleRules(request); 
  });
   serverWeb.on("/generateNotif", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleGenerateNotif(request); 
  });
  
  serverWeb.on("/javascript", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleJavascript(request); 
  });
  serverWeb.on("/createDevice", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleCreateDevice(request); 
  });
  serverWeb.on("/createHistory", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleCreateHistory(request); 
  });
  serverWeb.on("/ota", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleOTA(request); 
  });
  serverWeb.on("/createTemplate", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleCreateTemplate(request); 
  });
  serverWeb.on("/ZigbeeAction", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleZigbeeAction(request); 
  });
  serverWeb.on("/ZigbeeWriteAttribut", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleZigbeeWriteattribut(request); 
  });
  serverWeb.on("/ZigbeeReadAttribut", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleZigbeeReadattribut(request); 
  });
  serverWeb.on("/ZigbeeSendRequest", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleZigbeeSendRequest(request); 
  });
  serverWeb.on("/loadLinkyDatas", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleLoadLinkyDatas(request); 
  });
  serverWeb.on("/loadGaugeDashboard", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleLoadGaugeDashboard(request); 
  });
  serverWeb.on("/loadPowerGaugeAbo", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleLoadPowerGaugeAbo(request); 
  });
  serverWeb.on("/loadPowerGaugeTimeDay", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleLoadPowerGaugeTimeDay(request); 
  });
  serverWeb.on("/refreshGaugeAbo", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleRefreshGaugeAbo(request); 
  });
  serverWeb.on("/refreshLabel", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleRefreshLabel(request);
  });             
  serverWeb.on("/loadPowerTrend", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleLoadPowerTrend(request); 
  });

  serverWeb.on("/loadDatasTrend", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleLoadDatasTrend(request); 
  });

  serverWeb.on("/loadTotalEnergy", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleLoadTotalEnergy(request); 
  });

  serverWeb.on("/loadPowerChart", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleLoadPowerChart(request); 
  });
  serverWeb.on("/loadEnergyChart", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleLoadEnergyChart(request);
  });

  serverWeb.on("/exportPowerChart", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleExportPowerChart(request);
  });
  serverWeb.on("/exportEnergyChart", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleExportEnergyChart(request);
  });

  serverWeb.on("/loadDistributionChart", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleLoadDistribChart(request); 
  });
 
  serverWeb.on("/loadLabelEnergy", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleLoadLabelEnergy(request); 
  });
  serverWeb.on("/deleteDevice", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleDeleteDevice(request);
  });
  serverWeb.on("/cmdCleanGhosts", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleCleanGhostDevices(request);
  });
  serverWeb.on("/getDeviceValue", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleGetDeviceValue(request); 
  });
  serverWeb.on("/getDeviceAttrValues", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleGetDeviceAttrValues(request); 
  });
  serverWeb.on("/getRuleStatus", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleGetRuleStatus(request); 
  });
  serverWeb.on("/getAlert", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleGetAlert(request); 
  });

  serverWeb.on("/OTAUpdateBar", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    String result="-2";
    String IEEE = request->arg("id");
    for (size_t i = 0; i < devices.size(); i++) 
    {
      DeviceData* device = devices[i];
      if (device->getDeviceID() == IEEE)
      {
        if (device->otaInProgress == 1)
        {
          result = device->otaPercentage;
        }else{
          result = "-1";
        }
        break;
      }
    }  

    request->send(200, F("text/html"), result);

  });
  

  serverWeb.on("/getFormattedDate", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleGetFormattedDate(request); 
  });
  serverWeb.on("/sendMqttDiscover", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleSendMqttDiscover(request); 
  });
  serverWeb.on("/help", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleHelp(request); 
  });
  serverWeb.on("/poll", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    
    handlePoll(request);
  });

  serverWeb.on("/shelly", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (!checkAuth(request)) return;
    handleShelly(request); 
  });


  serverWeb.on("/getSystem", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    APIgetSystem(request); 
    
  });
  serverWeb.on("/api/wifiSignal", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    char buf[96];
    if (WiFi.isConnected()) {
      int rssi = WiFi.RSSI();
      int q = rssi >= -50 ? 100 : (rssi <= -100 ? 0 : 2 * (rssi + 100));
      snprintf(buf, sizeof(buf), "{\"rssi\":%d,\"quality\":%d,\"txPower\":%.1f}",
               rssi, q, (float)WiFi.getTxPower() / 4.0);
    } else {
      snprintf(buf, sizeof(buf), "{\"rssi\":0,\"quality\":0,\"txPower\":0}");
    }
    request->send(200, "application/json", buf);
  });
  serverWeb.on("/setAlias", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    handleSetAlias(request);
  });
  serverWeb.on("/setConfigWiFi", HTTP_POST, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    APISetConfigWiFi(request); 
    
  });
  serverWeb.on("/setResetDevice", HTTP_POST, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    APISetResetDevice(request); 
    
  });

  

  serverWeb.on("/getConfig", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    APIgetConfig(request); 
    
  });

  serverWeb.on("/getDevices", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    APIgetDevices(request); 
    
  });
  serverWeb.on("/getDevice", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    APIgetDevice(request); 
    
  });
  serverWeb.on("/getEnergyDevice", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    APIgetEnergyDevice(request); 
    
  });
  serverWeb.on("/getPowerDevice", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    APIgetPowerDevice(request); 
    
  });
  
  serverWeb.on("/getLinky", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    APIgetLinky(request); 
    
  });

  serverWeb.on("/getTemplates", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!checkAuth(request)) return;
    APIgetTemplates(request); 
    
  });

  serverWeb.on("/getUpdateStatusManuel", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) return;
    handleGetUpdateStatusManuel(request);
  });
  
  serverWeb.on("/getUpdateStatusAuto", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) return;
    handleGetUpdateStatusAuto(request);
  });

  /* LOG UPDATE - desactive temporairement
  serverWeb.on("/getUpdateLog", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) return;
    handleGetUpdateLog(request);
  });
  */

  serverWeb.on("/resetUpdateStatus", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) return;
    updateStatus.statusManuel = "";
    updateStatus.statusAuto = "";
    updateStatus.progressAuto = -1;
    updateStatus.rebootRequested = false;
    request->send(200, "text/plain", "OK");
  });

  /*serverWeb.serveStatic("/web/js/jquery-min.js", LittleFS, "/web/js/jquery-min.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/functions.min.js", LittleFS, "/web/js/functions.min.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/raphael-min.js", LittleFS, "/web/js/raphael-min.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/morris.min.js", LittleFS, "/web/js/morris.min.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/justgage.min.js", LittleFS, "/web/js/justgage.min.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/bootstrap.min.js", LittleFS, "/web/js/bootstrap.min.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/bootstrap.bundle.min.js.map", LittleFS, "/web/js/bootstrap.map").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/masonry.pkgd.min.js", LittleFS, "/web/js/masonry.pkgd.min.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/chart.umd.min.js", LittleFS, "/web/js/chart.umd.min.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/annotation.min.js", LittleFS, "/web/js/annotation.min.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/chart-zoom.min.js", LittleFS, "/web/js/chart-zoom.min.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/css/bootstrap.min.css", LittleFS, "/web/css/bootstrap.min.css").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/css/style.css", LittleFS, "/web/css/style.css").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/css/energy.css", LittleFS, "/web/css/energy.css").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/img/logo.png", LittleFS, "/web/img/logo.png").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/img/wait.gif", LittleFS, "/web/img/wait.gif").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/img/ziwifi32.gif", LittleFS, "/web/img/ziwifi32.gif").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/img/zlinky.gif", LittleFS, "/web/img/zlinky.gif").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/img/", LittleFS, "/web/img/").setCacheControl("max-age=600");*/
  serverWeb.on("/web/backup.tar", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!checkAuth(request)) return;
    request->send(LittleFS, "/bk/backup.tar", "application/x-tar");
  });
  serverWeb.serveStatic("/web", LittleFS, "/web")
    .setCacheControl("max-age=604800, immutable");
  // Menu commun servi une seule fois (mis en cache navigateur) au lieu d'etre re-envoye dans
  // le HTML de chaque page. document.write injecte le nav de maniere synchrone (avant le footer).
  // Modules matériels détectés au boot. Volontairement NON caché (quelques octets) : ainsi
  // le menu suit le matériel réellement présent, même si /menu.js est en cache long.
  serverWeb.on("/modules.js", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    AsyncResponseStream *response = request->beginResponseStream("application/javascript");
    response->addHeader("Cache-Control", "no-store");
    response->printf("var HAS_ZIGBEE=%d,HAS_LORA=%d;", zigbeeDetected ? 1 : 0, loraDetected ? 1 : 0);
    request->send(response);
  });
  serverWeb.on("/menu.js", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    AsyncResponseStream *response = request->beginResponseStream("application/javascript");
    response->addHeader("Cache-Control", "max-age=604800, immutable");
    response->print(F("document.write(`"));
    response->print(FPSTR(HTTP_MENU_NAV));
    response->print(F("`);"));
    response->print(FPSTR(HTTP_MENU_JS));
    // Masque les entrées dont le module n'est pas présent (data-mod='zigbee'|'lora').
    // Les entrées sans data-mod restent toujours visibles.
    // data-mod accepte une LISTE ('zigbee,lora') : l'entrée reste visible si AU MOINS un
    // des modules cités est présent (ex. Mesures->Appareils liste les appareils des deux).
    response->print(F(
      "(function(){var f={zigbee:(typeof HAS_ZIGBEE!=='undefined'&&HAS_ZIGBEE),"
      "lora:(typeof HAS_LORA!=='undefined'&&HAS_LORA)};"
      "var l=document.querySelectorAll('[data-mod]');"
      "for(var i=0;i<l.length;i++){var m=l[i].getAttribute('data-mod').split(','),ok=false;"
      "for(var j=0;j<m.length;j++){if(f[m[j]])ok=true;}"
      "if(!ok)l[i].remove();}})();"));
    request->send(response);
  });
  // === CORS : preflight OPTIONS handler (#24) ===
  serverWeb.onNotFound([](AsyncWebServerRequest *request) {
    if (request->method() == HTTP_OPTIONS) {
      request->send(204);
    } else {
      handleNotFound(request);
    }
  });

  // === CORS Headers pour accès API cross-origin (#24) ===
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

  serverWeb.begin();

  /*templateCache.indexTemplates();
  templateCache.printStats();*/

  //Update.onProgress(printProgress);
}
