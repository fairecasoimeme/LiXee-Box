/*
 * LiXeeBoxTunnel - Reverse proxy tunnel client for ESP32
 *
 * Connects to remote.lixee-box.fr via WebSocket and forwards
 * HTTP requests to the local AsyncWebServer.
 *
 * Supports up to MAX_CONCURRENT parallel HTTP requests via
 * a non-blocking state machine (no multi-threading needed).
 *
 * Usage:
 *   #include "LiXeeBoxTunnel.h"
 *
 *   AsyncWebServer server(80);
 *   LiXeeBoxTunnel tunnel("wss://remote.lixee-box.fr/tunnel?token=YOUR_TOKEN");
 *
 *   void setup() {
 *     server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200, "text/html", "<h1>Hello</h1>"); });
 *     server.begin();
 *     tunnel.begin();
 *   }
 *
 *   void loop() {
 *     tunnel.loop();
 *   }
 */

#ifndef LIXEEBOX_TUNNEL_H
#define LIXEEBOX_TUNNEL_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <base64.h>
#include "mbedtls/base64.h"
#include "config.h"  // For SpiRamJsonDocument
#include "PsramAllocator.h"
#include <utility>
#include <vector>
#include <WebSocketsClient.h>

static const int MAX_CONCURRENT = 6;
static const size_t MAX_BODY_SIZE = 300000;  // 300KB max per response
static const unsigned long SLOT_TIMEOUT_MS = 30000;  // 30s per slot max

// State machine for a single tunneled HTTP request
struct TunnelSlot {
    enum State {
        IDLE,              // Slot available
        CONNECT_SEND,      // Connect to local server + send HTTP request
        WAIT_RESPONSE,     // Waiting for first byte of response
        READ_HEADERS,      // Reading HTTP response headers
        READ_BODY_KNOWN,   // Reading body with known Content-Length
        READ_BODY_CHUNKED, // Reading chunked body
        READ_BODY_CLOSE,   // Reading body until connection close
        READY_TO_SEND      // Body complete, waiting to send via WebSocket
    };

    State state = IDLE;
    unsigned long stateStartTime = 0;

    // Request data (from proxy)
    String reqId;
    String method;
    String path;
    String headersJson;
    String bodyB64;  // Base64-encoded request body from proxy

    // Local HTTP connection
    WiFiClient localClient;

    // Response parsing
    int statusCode = 200;
    int contentLength = -1;
    bool isChunked = false;
    std::vector<std::pair<String, String>, PsramAllocator<std::pair<String, String>>> respHeaders;
    String partialLine;       // Incremental line buffer for header parsing
    bool statusLineParsed = false;

    // Body accumulation (PSRAM)
    uint8_t* bodyBuffer = nullptr;
    size_t bodyLen = 0;
    size_t bodyCapacity = 0;

    // Chunked transfer state
    int chunkRemaining = 0;       // Bytes remaining in current chunk
    bool chunkNeedSize = true;    // Expecting a chunk size line
    String chunkSizeLine;
    bool chunkTrailerCR = false;  // Expecting trailing \r\n after chunk data

    void reset() {
        state = IDLE;
        localClient.stop();
        reqId = "";
        method = "";
        path = "";
        headersJson = "";
        bodyB64 = "";
        statusCode = 200;
        contentLength = -1;
        isChunked = false;
        respHeaders.clear();
        partialLine = "";
        statusLineParsed = false;
        if (bodyBuffer) { free(bodyBuffer); bodyBuffer = nullptr; }
        bodyLen = 0;
        bodyCapacity = 0;
        chunkRemaining = 0;
        chunkNeedSize = true;
        chunkSizeLine = "";
        chunkTrailerCR = false;
        stateStartTime = 0;
    }
};

// Sous-classe : envoie un gros message texte en FRAGMENTS WebSocket (frame TEXT + frames
// CONTINUATION) en traitant les pings/pongs entre chaque fragment. Sans ça, l'écriture
// bloquante d'une grosse frame (~90 Ko) monopolise la boucle et le relais coupe la connexion
// (les pings ne sont plus traités). Le relais réassemble les fragments de façon transparente.
class ChunkedWsClient : public WebSocketsClient {
public:
    bool sendTXTChunked(uint8_t* payload, size_t length, size_t chunk = 8192) {
        if (_client.status != WSC_CONNECTED) return false;
        if (length <= chunk) {
            return sendFrame(&_client, WSop_text, payload, length, true);
        }
        size_t offset = 0;
        bool first = true;
        while (offset < length) {
            if (_client.status != WSC_CONNECTED) return false;
            size_t n = length - offset;
            if (n > chunk) n = chunk;
            bool fin = (offset + n >= length);
            WSopcode_t op = first ? WSop_text : WSop_continuation;
            if (!sendFrame(&_client, op, payload + offset, n, fin)) return false;
            offset += n;
            first = false;
            WebSocketsClient::loop();  // répondre aux pings entre les fragments
        }
        return true;
    }
};

class LiXeeBoxTunnel {
public:
    LiXeeBoxTunnel(const char* tunnelUrl, uint16_t localPort = 80)
        : _localPort(localPort), _connected(false), _sending(false),
          _lastReconnect(0), _lastHeartbeat(0) {
        parseTunnelUrl(tunnelUrl);
    }

    void begin() {
        Serial.println("[Tunnel] Initializing WebSocket client...");
        Serial.println("[Tunnel] Host: " + _host);
        Serial.println("[Tunnel] Path: " + _path);

        _ws.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
            this->onWsEvent(type, payload, length);
        });

        _ws.beginSslWithCA(_host.c_str(), 443, _path.c_str(), nullptr, "lixeebox");
        _ws.setReconnectInterval(3000);   // Reconnexion rapide (3s au lieu de 10s)
        _ws.enableHeartbeat(15000, 10000, 3);  // Ping toutes les 15s, timeout 10s

        Serial.println("[Tunnel] WebSocket configuration complete, waiting for connection...");
    }

    void loop() {
        _ws.loop();

        if (!_connected) return;

        // Concurrence adaptative selon le heap INTERNE (≈292 Ko, partagé WiFi/TLS/MQTT/Zigbee).
        // Sous pression, on sérialise les fetchs locaux pour éviter l'épuisement -> reboot watchdog
        // (cas : page d'accueil lourde + rafale de grosses libs JS chargées en parallèle).
        uint32_t freeHeap = ESP.getFreeHeap();
        int maxActive = (freeHeap > 120000) ? MAX_CONCURRENT : (freeHeap > 80000 ? 2 : 1);

        // Compte les slots déjà connectés (en cours de transfert depuis le serveur local)
        int activeXfer = 0;
        for (int i = 0; i < MAX_CONCURRENT; i++) {
            switch (_slots[i].state) {
                case TunnelSlot::WAIT_RESPONSE:
                case TunnelSlot::READ_HEADERS:
                case TunnelSlot::READ_BODY_KNOWN:
                case TunnelSlot::READ_BODY_CHUNKED:
                case TunnelSlot::READ_BODY_CLOSE:
                    activeXfer++;
                    break;
                default:
                    break;
            }
        }

        // Process active slots (non-blocking), en limitant le démarrage de nouveaux fetchs
        for (int i = 0; i < MAX_CONCURRENT; i++) {
            TunnelSlot& s = _slots[i];
            if (s.state == TunnelSlot::IDLE) continue;
            // Ne pas démarrer une nouvelle connexion locale si trop de transferts sont déjà en cours.
            // Le slot reste en attente (on repousse son chrono pour ne pas déclencher le timeout).
            if (s.state == TunnelSlot::CONNECT_SEND && activeXfer >= maxActive) {
                s.stateStartTime = millis();
                continue;
            }
            if (s.state == TunnelSlot::CONNECT_SEND) activeXfer++;  // va devenir actif
            processSlot(s);
        }

        // Send application-level heartbeat every 15 seconds (aligned with WS heartbeat)
        if (millis() - _lastHeartbeat > 15000) {
            _lastHeartbeat = millis();
            SpiRamJsonDocument hb(256);
            hb["type"] = "heartbeat";
            hb["uptime"] = millis() / 1000;
            hb["freeHeap"] = ESP.getFreeHeap();
            hb["freePsram"] = ESP.getFreePsram();
            String hbStr;
            serializeJson(hb, hbStr);
            _ws.sendTXT(hbStr);
            // Log active slots count
            int active = 0;
            for (int i = 0; i < MAX_CONCURRENT; i++) {
                if (_slots[i].state != TunnelSlot::IDLE) active++;
            }
            Serial.printf("[Tunnel] Heartbeat - heap: %u, psram: %u, active_slots: %d\n",
                          ESP.getFreeHeap(), ESP.getFreePsram(), active);
        }
    }

    void stop() {
        Serial.println("[Tunnel] Stopping...");
        _connected = false;
        for (int i = 0; i < MAX_CONCURRENT; i++) {
            _slots[i].reset();
        }
        _ws.disconnect();
        Serial.println("[Tunnel] Stopped");
    }

    bool isConnected() { return _connected; }
    String getDeviceId() { return _deviceId; }
    String getSubdomain() { return _subdomain; }

    void sendNotification(const String& title, const String& message, int type,
                          const char* alertType, float value, float threshold) {
        if (!_connected) return;
        SpiRamJsonDocument doc(768);
        doc["type"] = "notification";
        doc["deviceId"] = _deviceId;
        doc["subdomain"] = _subdomain;
        doc["title"] = title;
        doc["message"] = message;
        doc["notifType"] = type;
        doc["timestamp"] = FormattedDate;
        if (alertType) doc["alertType"] = alertType;
        if (value != 0) doc["value"] = value;
        if (threshold != 0) doc["threshold"] = threshold;
        String str;
        serializeJson(doc, str);
        _ws.sendTXT(str);
        Serial.printf("[Tunnel] Push notification sent: %s (payload=%u bytes)\n", title.c_str(), str.length());
    }

private:
    ChunkedWsClient _ws;
    String _host;
    String _path;
    uint16_t _localPort;
    bool _connected;
    bool _sending;  // True when a slot is currently doing sendTXT
    unsigned long _lastReconnect;
    unsigned long _lastHeartbeat;
    String _deviceId;
    String _subdomain;
    TunnelSlot _slots[MAX_CONCURRENT];

    void parseTunnelUrl(const char* url) {
        String u(url);
        if (u.startsWith("wss://")) u = u.substring(6);
        else if (u.startsWith("ws://")) u = u.substring(5);

        int pathIdx = u.indexOf('/');
        if (pathIdx > 0) {
            _host = u.substring(0, pathIdx);
            _path = u.substring(pathIdx);
        } else {
            _host = u;
            _path = "/";
        }
    }

    void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
        switch (type) {
            case WStype_CONNECTED:
                Serial.println("[Tunnel] WebSocket CONNECTED!");
                if (payload) Serial.printf("[Tunnel] Server URL: %s\n", payload);
                _connected = true;
                break;

            case WStype_DISCONNECTED:
                Serial.printf("[Tunnel] WebSocket DISCONNECTED (heap: %u, uptime: %lus)\n",
                              ESP.getFreeHeap(), millis() / 1000);
                _connected = false;
                // Reset all active slots
                for (int i = 0; i < MAX_CONCURRENT; i++) {
                    _slots[i].reset();
                }
                break;

            case WStype_TEXT:
                handleMessage((char*)payload, length);
                break;

            case WStype_BIN:
                break;

            case WStype_PING:
            case WStype_PONG:
                break;

            case WStype_ERROR:
                Serial.println("[Tunnel] ERROR!");
                if (payload) Serial.printf("[Tunnel] Error: %s\n", payload);
                break;

            default:
                break;
        }
    }

    void handleMessage(char* payload, size_t length) {
        // Size the JSON document to fit the actual message (PSRAM-backed)
        size_t docSize = length + 256;
        if (docSize < 2048) docSize = 2048;
        SpiRamJsonDocument doc(docSize);
        DeserializationError err = deserializeJson(doc, payload, length);
        if (err) {
            Serial.printf("[Tunnel] JSON parse error: %s (len=%u, doc=%u)\n",
                          err.c_str(), length, docSize);
            return;
        }

        const char* type = doc["type"];
        if (!type) return;

        if (strcmp(type, "welcome") == 0) {
            _deviceId = doc["deviceId"].as<String>();
            _subdomain = doc["subdomain"].as<String>();
            Serial.println("[Tunnel] Registered as " + _deviceId + " (" + _subdomain + ")");

            SpiRamJsonDocument info(512);
            info["type"] = "info";
            JsonObject data = info["data"].to<JsonObject>();
            data["firmware"] = "LiXeeBoxTunnel/1.0";
            data["version"] = VERSION;
            data["chip"] = ESP.getChipModel();
            data["freeHeap"] = ESP.getFreeHeap();
            data["localIP"] = WiFi.localIP().toString();
            String infoStr;
            serializeJson(info, infoStr);
            _ws.sendTXT(infoStr);
        }
        else if (strcmp(type, "http_request") == 0) {
            const char* reqId = doc["reqId"];
            const char* method = doc["method"];
            const char* path = doc["path"];
            if (!reqId || !method || !path) return;

            // Find a free slot
            int freeSlot = -1;
            for (int i = 0; i < MAX_CONCURRENT; i++) {
                if (_slots[i].state == TunnelSlot::IDLE) {
                    freeSlot = i;
                    break;
                }
            }

            if (freeSlot < 0) {
                Serial.printf("[Tunnel] WARN: All %d slots busy, rejecting %s %s\n",
                              MAX_CONCURRENT, method, path);
                sendErrorResponse(reqId, 503, "Server busy");
                return;
            }

            // PSRAM guard
            if (ESP.getFreePsram() < 200000) {
                Serial.printf("[Tunnel] WARN: PSRAM low (%u), rejecting request\n", ESP.getFreePsram());
                sendErrorResponse(reqId, 503, "Low memory");
                return;
            }
            // Plancher heap INTERNE : refuser plutôt que risquer le reboot watchdog (<40 Ko).
            // Le navigateur réessaiera l'asset ; mieux qu'un crash de toute la passerelle.
            if (ESP.getFreeHeap() < 55000) {
                Serial.printf("[Tunnel] WARN: heap low (%u), rejecting request\n", ESP.getFreeHeap());
                sendErrorResponse(reqId, 503, "Low memory");
                return;
            }

            TunnelSlot& slot = _slots[freeSlot];
            slot.reqId = reqId;
            slot.method = method;
            slot.path = path;

            slot.headersJson = "";
            if (doc.containsKey("headers")) {
                serializeJson(doc["headers"], slot.headersJson);
            }
            slot.bodyB64 = doc["body"].isNull() ? "" : doc["body"].as<String>();

            slot.state = TunnelSlot::CONNECT_SEND;
            slot.stateStartTime = millis();

            Serial.printf("[Tunnel] [%d] Queued: %s %s (reqId: %s)\n",
                          freeSlot, method, path, reqId);
        }
    }

    // ---- State machine: advance one slot by one step (non-blocking) ----

    void processSlot(TunnelSlot& slot) {
        // Watchdog: timeout any slot that's been active too long
        if (slot.state != TunnelSlot::IDLE && slot.state != TunnelSlot::READY_TO_SEND) {
            if (millis() - slot.stateStartTime > SLOT_TIMEOUT_MS) {
                Serial.printf("[Tunnel] Slot timeout (state=%d, reqId=%s)\n",
                              slot.state, slot.reqId.c_str());
                sendErrorResponse(slot.reqId.c_str(), 504, "Gateway timeout");
                slot.reset();
                return;
            }
        }

        switch (slot.state) {
            case TunnelSlot::CONNECT_SEND:
                processConnectSend(slot);
                break;
            case TunnelSlot::WAIT_RESPONSE:
                processWaitResponse(slot);
                break;
            case TunnelSlot::READ_HEADERS:
                processReadHeaders(slot);
                break;
            case TunnelSlot::READ_BODY_KNOWN:
                processReadBodyKnown(slot);
                break;
            case TunnelSlot::READ_BODY_CHUNKED:
                processReadBodyChunked(slot);
                break;
            case TunnelSlot::READ_BODY_CLOSE:
                processReadBodyClose(slot);
                break;
            case TunnelSlot::READY_TO_SEND:
                processReadyToSend(slot);
                break;
            default:
                break;
        }
    }

    // State: CONNECT_SEND - Connect to local server and send HTTP request
    void processConnectSend(TunnelSlot& slot) {
        if (!slot.localClient.connect("127.0.0.1", _localPort, 1000)) {
            Serial.printf("[Tunnel] [%s] Cannot connect to local server\n", slot.reqId.c_str());
            sendErrorResponse(slot.reqId.c_str(), 502, "Cannot connect to local server");
            slot.reset();
            return;
        }

        // Build HTTP request
        String request = slot.method + " " + slot.path + " HTTP/1.1\r\n";
        request += "Host: 127.0.0.1\r\n";
        request += "Connection: close\r\n";

        // Forward relevant headers
        bool hasContentType = false;
        if (slot.headersJson.length() > 2) {
            SpiRamJsonDocument hdrs(2048);
            if (deserializeJson(hdrs, slot.headersJson) == DeserializationError::Ok) {
                JsonObject headers = hdrs.as<JsonObject>();
                for (JsonPair kv : headers) {
                    String key = kv.key().c_str();
                    key.toLowerCase();
                    // Skip content-length: le tunnel le recalcule depuis le body décodé
                    if (key == "content-length") continue;
                    if (key == "content-type" ||
                        key == "accept" || key == "accept-encoding" ||
                        key == "cookie" || key == "authorization" ||
                        key == "x-requested-with" || key == "origin" || key == "referer") {
                        request += String(kv.key().c_str()) + ": " + kv.value().as<String>() + "\r\n";
                        if (key == "content-type") hasContentType = true;
                    }
                }
            }
        }

        // Body
        if (slot.bodyB64.length() > 0) {
            String body = base64Decode(slot.bodyB64);
            if (!hasContentType) {
                request += "Content-Type: application/x-www-form-urlencoded\r\n";
            }
            request += "Content-Length: " + String(body.length()) + "\r\n";
            request += "\r\n";
            request += body;
        } else {
            request += "\r\n";
        }

        slot.localClient.print(request);
        // Free request data no longer needed
        slot.headersJson = "";
        slot.bodyB64 = "";

        slot.state = TunnelSlot::WAIT_RESPONSE;
        slot.stateStartTime = millis();
    }

    // State: WAIT_RESPONSE - Wait for first byte from local server
    void processWaitResponse(TunnelSlot& slot) {
        if (slot.localClient.available()) {
            slot.state = TunnelSlot::READ_HEADERS;
            slot.stateStartTime = millis();
            slot.partialLine = "";
            slot.statusLineParsed = false;
        }
    }

    // State: READ_HEADERS - Read HTTP response headers incrementally
    void processReadHeaders(TunnelSlot& slot) {
        int bytesProcessed = 0;
        while (slot.localClient.available() && bytesProcessed < 1024) {
            char c = slot.localClient.read();
            bytesProcessed++;

            if (c == '\n') {
                // Remove trailing \r
                if (slot.partialLine.endsWith("\r")) {
                    slot.partialLine.remove(slot.partialLine.length() - 1);
                }

                if (!slot.statusLineParsed) {
                    // Parse status line: "HTTP/1.1 200 OK"
                    int spaceIdx = slot.partialLine.indexOf(' ');
                    if (spaceIdx > 0) {
                        slot.statusCode = slot.partialLine.substring(spaceIdx + 1).toInt();
                    }
                    slot.statusLineParsed = true;
                }
                else if (slot.partialLine.length() == 0) {
                    // Empty line = end of headers
                    Serial.printf("[Tunnel] [%s] Response status: %d, CL: %d, Chunked: %s\n",
                                  slot.reqId.c_str(), slot.statusCode, slot.contentLength,
                                  slot.isChunked ? "yes" : "no");

                    // Transition to body reading
                    if (slot.contentLength > 0) {
                        // Allocate body buffer
                        size_t allocSize = min((size_t)slot.contentLength, MAX_BODY_SIZE);
                        slot.bodyBuffer = (uint8_t*)ps_malloc(allocSize);
                        if (!slot.bodyBuffer) slot.bodyBuffer = (uint8_t*)malloc(allocSize);
                        if (!slot.bodyBuffer) {
                            Serial.printf("[Tunnel] [%s] Failed to allocate %u bytes\n",
                                          slot.reqId.c_str(), allocSize);
                            sendErrorResponse(slot.reqId.c_str(), 500, "Out of memory");
                            slot.reset();
                            return;
                        }
                        slot.bodyCapacity = allocSize;
                        slot.bodyLen = 0;
                        slot.state = TunnelSlot::READ_BODY_KNOWN;
                    } else if (slot.isChunked) {
                        // Start with 16KB buffer, will grow as needed
                        slot.bodyBuffer = (uint8_t*)ps_malloc(16384);
                        if (!slot.bodyBuffer) slot.bodyBuffer = (uint8_t*)malloc(16384);
                        if (!slot.bodyBuffer) {
                            sendErrorResponse(slot.reqId.c_str(), 500, "Out of memory");
                            slot.reset();
                            return;
                        }
                        slot.bodyCapacity = 16384;
                        slot.bodyLen = 0;
                        slot.chunkNeedSize = true;
                        slot.chunkRemaining = 0;
                        slot.chunkSizeLine = "";
                        slot.chunkTrailerCR = false;
                        slot.state = TunnelSlot::READ_BODY_CHUNKED;
                    } else if (slot.contentLength == 0) {
                        // No body
                        slot.state = TunnelSlot::READY_TO_SEND;
                    } else {
                        // Unknown length, read until close
                        slot.bodyBuffer = (uint8_t*)ps_malloc(16384);
                        if (!slot.bodyBuffer) slot.bodyBuffer = (uint8_t*)malloc(16384);
                        if (!slot.bodyBuffer) {
                            sendErrorResponse(slot.reqId.c_str(), 500, "Out of memory");
                            slot.reset();
                            return;
                        }
                        slot.bodyCapacity = 16384;
                        slot.bodyLen = 0;
                        slot.state = TunnelSlot::READ_BODY_CLOSE;
                    }
                    slot.stateStartTime = millis();
                    return;
                }
                else {
                    // Parse header line
                    int colonIdx = slot.partialLine.indexOf(':');
                    if (colonIdx > 0) {
                        String hKey = slot.partialLine.substring(0, colonIdx);
                        String hVal = slot.partialLine.substring(colonIdx + 1);
                        hVal.trim();
                        hKey.trim();

                        String hKeyLower = hKey;
                        hKeyLower.toLowerCase();

                        if (hKeyLower == "transfer-encoding") {
                            String hValLower = hVal;
                            hValLower.toLowerCase();
                            if (hValLower.indexOf("chunked") >= 0) {
                                slot.isChunked = true;
                            }
                        }
                        if (hKeyLower == "content-length") {
                            slot.contentLength = hVal.toInt();
                        }
                        if (hKeyLower != "connection" && hKeyLower != "transfer-encoding") {
                            slot.respHeaders.push_back({hKey, hVal});
                        }
                    }
                }
                slot.partialLine = "";
            } else {
                slot.partialLine += c;
            }
        }
    }

    // State: READ_BODY_KNOWN - Read body with known Content-Length
    void processReadBodyKnown(TunnelSlot& slot) {
        // Cap against buffer capacity (contentLength may exceed MAX_BODY_SIZE)
        if (slot.bodyLen >= slot.bodyCapacity) {
            Serial.printf("[Tunnel] [%s] Body buffer full: %u/%d bytes (truncated)\n",
                          slot.reqId.c_str(), slot.bodyLen, slot.contentLength);
            slot.state = TunnelSlot::READY_TO_SEND;
            slot.localClient.stop();
            return;
        }

        int remaining = slot.contentLength - slot.bodyLen;
        if (remaining <= 0) {
            slot.state = TunnelSlot::READY_TO_SEND;
            slot.localClient.stop();
            return;
        }

        int avail = slot.localClient.available();
        if (avail <= 0) return;

        // Never read beyond the allocated buffer
        int bufRemaining = (int)(slot.bodyCapacity - slot.bodyLen);
        int toRead = min(avail, min(remaining, bufRemaining));
        // Read in bulk
        int r = slot.localClient.read(slot.bodyBuffer + slot.bodyLen, toRead);
        if (r > 0) {
            slot.bodyLen += r;
            slot.stateStartTime = millis();  // Reset timeout on data received
        }

        if ((int)slot.bodyLen >= slot.contentLength || slot.bodyLen >= slot.bodyCapacity) {
            Serial.printf("[Tunnel] [%s] Body complete: %u/%d bytes\n",
                          slot.reqId.c_str(), slot.bodyLen, slot.contentLength);
            slot.state = TunnelSlot::READY_TO_SEND;
            slot.localClient.stop();
        }
    }

    // Grow body buffer (for chunked/close modes)
    bool growBodyBuffer(TunnelSlot& slot, size_t needed) {
        if (slot.bodyLen + needed <= slot.bodyCapacity) return true;
        if (slot.bodyLen + needed > MAX_BODY_SIZE) return false;

        size_t newCap = max(slot.bodyCapacity * 2, slot.bodyLen + needed);
        if (newCap > MAX_BODY_SIZE) newCap = MAX_BODY_SIZE;

        uint8_t* newBuf = (uint8_t*)ps_malloc(newCap);
        if (!newBuf) newBuf = (uint8_t*)malloc(newCap);
        if (!newBuf) return false;

        if (slot.bodyBuffer && slot.bodyLen > 0) {
            memcpy(newBuf, slot.bodyBuffer, slot.bodyLen);
        }
        if (slot.bodyBuffer) free(slot.bodyBuffer);
        slot.bodyBuffer = newBuf;
        slot.bodyCapacity = newCap;
        return true;
    }

    // State: READ_BODY_CHUNKED - Read chunked transfer encoding incrementally
    void processReadBodyChunked(TunnelSlot& slot) {
        int bytesProcessed = 0;

        while (slot.localClient.available() && bytesProcessed < 2048) {
            if (slot.chunkTrailerCR) {
                // Consume trailing \r\n after chunk data
                char c = slot.localClient.read();
                bytesProcessed++;
                if (c == '\n') {
                    slot.chunkTrailerCR = false;
                    slot.chunkNeedSize = true;
                    slot.chunkSizeLine = "";
                }
                // Skip \r silently
                continue;
            }

            if (slot.chunkNeedSize) {
                // Reading chunk size line
                char c = slot.localClient.read();
                bytesProcessed++;
                if (c == '\n') {
                    // Parse chunk size
                    int semi = slot.chunkSizeLine.indexOf(';');
                    if (semi >= 0) slot.chunkSizeLine = slot.chunkSizeLine.substring(0, semi);
                    slot.chunkSizeLine.trim();
                    slot.chunkRemaining = (int)strtol(slot.chunkSizeLine.c_str(), nullptr, 16);

                    if (slot.chunkRemaining == 0) {
                        // Last chunk
                        Serial.printf("[Tunnel] [%s] Chunked body complete: %u bytes\n",
                                      slot.reqId.c_str(), slot.bodyLen);
                        slot.state = TunnelSlot::READY_TO_SEND;
                        slot.localClient.stop();
                        return;
                    }
                    slot.chunkNeedSize = false;
                } else if (c != '\r') {
                    slot.chunkSizeLine += c;
                }
                continue;
            }

            // Reading chunk data
            int avail = slot.localClient.available();
            int toRead = min(avail, slot.chunkRemaining);
            if (toRead <= 0) break;

            if (!growBodyBuffer(slot, toRead)) {
                Serial.printf("[Tunnel] [%s] Body too large, truncating at %u bytes\n",
                              slot.reqId.c_str(), slot.bodyLen);
                slot.state = TunnelSlot::READY_TO_SEND;
                slot.localClient.stop();
                return;
            }

            int r = slot.localClient.read(slot.bodyBuffer + slot.bodyLen, toRead);
            if (r > 0) {
                slot.bodyLen += r;
                slot.chunkRemaining -= r;
                bytesProcessed += r;
                slot.stateStartTime = millis();
            }

            if (slot.chunkRemaining <= 0) {
                // Chunk data complete, expect trailing \r\n
                slot.chunkTrailerCR = true;
            }
        }
    }

    // State: READ_BODY_CLOSE - Read until connection closes
    void processReadBodyClose(TunnelSlot& slot) {
        int avail = slot.localClient.available();

        if (avail > 0) {
            if (!growBodyBuffer(slot, avail)) {
                Serial.printf("[Tunnel] [%s] Body too large, truncating at %u bytes\n",
                              slot.reqId.c_str(), slot.bodyLen);
                slot.state = TunnelSlot::READY_TO_SEND;
                slot.localClient.stop();
                return;
            }

            int r = slot.localClient.read(slot.bodyBuffer + slot.bodyLen, avail);
            if (r > 0) {
                slot.bodyLen += r;
                slot.stateStartTime = millis();
            }
        }

        if (!slot.localClient.connected() && !slot.localClient.available()) {
            Serial.printf("[Tunnel] [%s] Body complete (close): %u bytes\n",
                          slot.reqId.c_str(), slot.bodyLen);
            slot.state = TunnelSlot::READY_TO_SEND;
            slot.localClient.stop();
        }
    }

    // State: READY_TO_SEND - Base64 encode body and send via WebSocket
    void processReadyToSend(TunnelSlot& slot) {
        // Only one slot can send at a time (WebSocketsClient is not thread-safe)
        if (_sending) return;
        _sending = true;

        Serial.printf("[Tunnel] [%s] Encoding + sending (%u bytes body)\n",
                      slot.reqId.c_str(), slot.bodyLen);

        // Base64 encode body into PSRAM
        char* b64Buf = nullptr;
        size_t b64Len = 0;

        if (slot.bodyBuffer && slot.bodyLen > 0) {
            size_t b64MaxLen = 4 * ((slot.bodyLen + 2) / 3) + 1;
            b64Buf = (char*)ps_malloc(b64MaxLen);
            if (!b64Buf) b64Buf = (char*)malloc(b64MaxLen);

            if (b64Buf) {
                size_t written = 0;
                int ret = mbedtls_base64_encode((unsigned char*)b64Buf, b64MaxLen, &written,
                                                 slot.bodyBuffer, slot.bodyLen);
                if (ret == 0) {
                    b64Len = written;
                    b64Buf[b64Len] = '\0';
                } else {
                    Serial.printf("[Tunnel] [%s] base64 encode failed: %d\n", slot.reqId.c_str(), ret);
                    free(b64Buf);
                    b64Buf = nullptr;
                }
            }
            // Free raw body - no longer needed
            free(slot.bodyBuffer);
            slot.bodyBuffer = nullptr;
            slot.bodyLen = 0;
        }

        // Build JSON response
        String jsonHeader;
        jsonHeader.reserve(512);
        jsonHeader = "{\"type\":\"http_response\",\"reqId\":\"";
        jsonHeader += slot.reqId;
        jsonHeader += "\",\"status\":";
        jsonHeader += String(slot.statusCode);
        jsonHeader += ",\"headers\":{";

        bool firstHeader = true;
        for (const auto& h : slot.respHeaders) {
            if (!firstHeader) jsonHeader += ",";
            jsonHeader += "\"";
            jsonHeader += escapeJsonString(h.first);
            jsonHeader += "\":\"";
            jsonHeader += escapeJsonString(h.second);
            jsonHeader += "\"";
            firstHeader = false;
        }
        jsonHeader += "},\"body\":\"";

        // Assemble final response in PSRAM
        size_t totalLen = jsonHeader.length() + b64Len + 2;
        char* respBuf = (char*)ps_malloc(totalLen + 1);
        if (!respBuf) respBuf = (char*)malloc(totalLen + 1);

        if (respBuf) {
            size_t offset = 0;
            memcpy(respBuf + offset, jsonHeader.c_str(), jsonHeader.length());
            offset += jsonHeader.length();
            if (b64Buf && b64Len > 0) {
                memcpy(respBuf + offset, b64Buf, b64Len);
                offset += b64Len;
            }
            memcpy(respBuf + offset, "\"}", 2);
            offset += 2;
            respBuf[offset] = '\0';

            if (b64Buf) { free(b64Buf); b64Buf = nullptr; }

            // Process pending pings/pongs before sending
            _ws.loop();

            // Envoi fragmenté pour les gros corps : évite que l'écriture bloquante d'une
            // grosse frame ne fasse couper la connexion (pings traités entre fragments).
            _ws.sendTXTChunked((uint8_t*)respBuf, offset);

            // Traiter pings/pongs immédiatement après un gros envoi
            // pour éviter un timeout côté serveur
            if (offset > 10000) {
                _ws.loop();
            }

            Serial.printf("[Tunnel] [%s] Response sent: %d (%u bytes)\n",
                          slot.reqId.c_str(), slot.statusCode, offset);
            free(respBuf);
        } else {
            Serial.printf("[Tunnel] [%s] Failed to allocate %u bytes for response\n",
                          slot.reqId.c_str(), totalLen);
            if (b64Buf) free(b64Buf);
            sendErrorResponse(slot.reqId.c_str(), 500, "Out of memory");
        }

        slot.reset();
        _sending = false;
    }

    // ---- Utility methods ----

    String escapeJsonString(const String& input) {
        String output;
        output.reserve(input.length() + 10);
        for (size_t i = 0; i < input.length(); i++) {
            char c = input[i];
            switch (c) {
                case '"': output += "\\\""; break;
                case '\\': output += "\\\\"; break;
                case '\b': output += "\\b"; break;
                case '\f': output += "\\f"; break;
                case '\n': output += "\\n"; break;
                case '\r': output += "\\r"; break;
                case '\t': output += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                        output += buf;
                    } else {
                        output += c;
                    }
            }
        }
        return output;
    }

    void sendErrorResponse(const char* reqId, int status, const char* message) {
        SpiRamJsonDocument doc(512);
        doc["type"] = "http_response";
        doc["reqId"] = reqId;
        doc["status"] = status;
        doc["headers"]["Content-Type"] = "text/plain";
        doc["body"] = base64::encode((const uint8_t*)message, strlen(message));
        String str;
        serializeJson(doc, str);
        _ws.sendTXT(str);
    }

    String base64Decode(const String& input) {
        size_t outputLen = 0;
        mbedtls_base64_decode(nullptr, 0, &outputLen,
                              (const unsigned char*)input.c_str(), input.length());
        if (outputLen == 0) return "";

        unsigned char* buf = (unsigned char*)ps_malloc(outputLen + 1);
        if (!buf) buf = (unsigned char*)malloc(outputLen + 1);
        if (!buf) return "";

        size_t actualLen = 0;
        int ret = mbedtls_base64_decode(buf, outputLen, &actualLen,
                                        (const unsigned char*)input.c_str(), input.length());
        if (ret != 0) {
            free(buf);
            return "";
        }

        buf[actualLen] = 0;
        String result((char*)buf);
        free(buf);
        return result;
    }
};

#endif // LIXEEBOX_TUNNEL_H
