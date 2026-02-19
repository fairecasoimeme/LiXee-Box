/*
 * LiXeeBoxTunnel - Reverse proxy tunnel client for ESP32
 *
 * Connects to proxy.lixee-box.fr via WebSocket and forwards
 * HTTP requests to the local AsyncWebServer.
 *
 * Usage:
 *   #include "LiXeeBoxTunnel.h"
 *
 *   AsyncWebServer server(80);
 *   LiXeeBoxTunnel tunnel("wss://proxy.lixee-box.fr/tunnel?token=YOUR_TOKEN");
 *
 *   void setup() {
 *     // Setup your web server routes
 *     server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200, "text/html", "<h1>Hello</h1>"); });
 *     server.begin();
 *
 *     // Start tunnel after WiFi is connected
 *     tunnel.begin();
 *   }
 *
 *   void loop() {
 *     tunnel.loop();
 *   }
 */

#ifndef LIXEEBOX_TUNNEL_H
#define LIXEEBOX_TUNNEL_H

// Disable WebSockets debug output for performance
// #define DEBUG_WEBSOCKETS(...) Serial.printf(__VA_ARGS__)
// #define WEBSOCKETS_LOGLEVEL 4

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <base64.h>
#include "mbedtls/base64.h"
#include "config.h"  // For SpiRamJsonDocument
#include <utility>
#include <vector>

// Lightweight WebSocket client (built-in, no extra lib needed)
#include <WebSocketsClient.h>

class LiXeeBoxTunnel {
public:
    LiXeeBoxTunnel(const char* tunnelUrl, uint16_t localPort = 80)
        : _localPort(localPort), _connected(false), _lastReconnect(0), _lastHeartbeat(0) {
        parseTunnelUrl(tunnelUrl);
    }

    void begin() {
        Serial.println("[Tunnel] Initializing WebSocket client...");
        Serial.println("[Tunnel] Host: " + _host);
        Serial.println("[Tunnel] Path: " + _path);

        // Set event handler FIRST (before begin)
        _ws.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
            this->onWsEvent(type, payload, length);
        });

        // Use beginSslWithCA with NULL CA to enable insecure mode
        _ws.beginSslWithCA(_host.c_str(), 443, _path.c_str(), nullptr, "lixeebox");

        // Reconnection settings
        _ws.setReconnectInterval(10000);  // Increased to 10 seconds

        // Enable WebSocket-level heartbeat (ping every 15s, timeout 5s, 2 retries)
        _ws.enableHeartbeat(15000, 5000, 2);

        Serial.println("[Tunnel] WebSocket configuration complete, waiting for connection...");
    }

    void loop() {
        _ws.loop();

        // Send application-level heartbeat every 25 seconds
        if (_connected && millis() - _lastHeartbeat > 25000) {
            _lastHeartbeat = millis();
            DynamicJsonDocument hb(256);
            hb["type"] = "heartbeat";
            hb["uptime"] = millis() / 1000;
            hb["freeHeap"] = ESP.getFreeHeap();
            hb["freePsram"] = ESP.getFreePsram();
            String hbStr;
            serializeJson(hb, hbStr);
            _ws.sendTXT(hbStr);
            Serial.println("[Tunnel] Heartbeat sent");
        }
    }

    bool isConnected() { return _connected; }
    String getDeviceId() { return _deviceId; }
    String getSubdomain() { return _subdomain; }

private:
    WebSocketsClient _ws;
    String _host;
    String _path;
    uint16_t _localPort;
    bool _connected;
    unsigned long _lastReconnect;
    unsigned long _lastHeartbeat;
    String _deviceId;
    String _subdomain;

    void parseTunnelUrl(const char* url) {
        String u(url);
        // Remove wss://
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
                if (payload) {
                    Serial.printf("[Tunnel] Server URL: %s\n", payload);
                }
                _connected = true;
                break;

            case WStype_DISCONNECTED:
                Serial.println("[Tunnel] WebSocket DISCONNECTED");
                _connected = false;
                break;

            case WStype_TEXT:
                Serial.printf("[Tunnel] Received TEXT (%d bytes)\n", length);
                handleMessage((char*)payload, length);
                break;

            case WStype_BIN:
                Serial.printf("[Tunnel] Received BIN (%d bytes)\n", length);
                break;

            case WStype_PING:
                Serial.println("[Tunnel] Received PING");
                break;

            case WStype_PONG:
                Serial.println("[Tunnel] Received PONG");
                break;

            case WStype_ERROR:
                Serial.println("[Tunnel] ERROR!");
                if (payload) {
                    Serial.printf("[Tunnel] Error details: %s\n", payload);
                }
                break;

            case WStype_FRAGMENT_TEXT_START:
            case WStype_FRAGMENT_BIN_START:
            case WStype_FRAGMENT:
            case WStype_FRAGMENT_FIN:
                Serial.println("[Tunnel] Fragment received");
                break;

            default:
                Serial.printf("[Tunnel] Unknown event type: %d\n", type);
                break;
        }
    }

    void handleMessage(char* payload, size_t length) {
        DynamicJsonDocument doc(2048);
        DeserializationError err = deserializeJson(doc, payload, length);
        if (err) {
            Serial.println("[Tunnel] JSON parse error: " + String(err.c_str()));
            return;
        }

        const char* type = doc["type"];
        if (!type) return;

        if (strcmp(type, "welcome") == 0) {
            _deviceId = doc["deviceId"].as<String>();
            _subdomain = doc["subdomain"].as<String>();
            Serial.println("[Tunnel] Registered as " + _deviceId + " (" + _subdomain + ")");

            // Send device info
            DynamicJsonDocument info(512);
            info["type"] = "info";
            JsonObject data = info["data"].to<JsonObject>();
            data["firmware"] = "LiXeeBoxTunnel/1.0";
            data["chip"] = ESP.getChipModel();
            data["freeHeap"] = ESP.getFreeHeap();
            data["localIP"] = WiFi.localIP().toString();
            String infoStr;
            serializeJson(info, infoStr);
            _ws.sendTXT(infoStr);
        }
        else if (strcmp(type, "http_request") == 0) {
            handleHttpRequest(doc);
        }
    }

    void handleHttpRequest(JsonDocument& doc) {
        const char* reqId = doc["reqId"];
        const char* method = doc["method"];
        const char* path = doc["path"];

        if (!reqId || !method || !path) return;

        String reqIdStr = String(reqId);  // Save reqId as String before doc is reused
        Serial.printf("[Tunnel] %s %s (reqId: %s)\n", method, path, reqId);

        // Make local HTTP request to AsyncWebServer
        WiFiClient localClient;
        if (!localClient.connect("127.0.0.1", _localPort, 5000)) {
            sendErrorResponse(reqIdStr.c_str(), 502, "Cannot connect to local server");
            return;
        }

        // Build HTTP request
        String request = String(method) + " " + String(path) + " HTTP/1.1\r\n";
        request += "Host: 127.0.0.1\r\n";
        request += "Connection: close\r\n";

        // Forward relevant headers
        JsonObject headers = doc["headers"].as<JsonObject>();
        for (JsonPair kv : headers) {
            String key = kv.key().c_str();
            key.toLowerCase();
            if (key == "content-type" || key == "content-length" ||
                key == "accept" || key == "accept-encoding" ||
                key == "cookie" || key == "authorization") {
                request += String(kv.key().c_str()) + ": " + kv.value().as<String>() + "\r\n";
            }
        }

        // Body
        if (!doc["body"].isNull()) {
            String bodyB64 = doc["body"].as<String>();
            String body = base64Decode(bodyB64);
            request += "Content-Length: " + String(body.length()) + "\r\n";
            request += "\r\n";
            request += body;
        } else {
            request += "\r\n";
        }

        localClient.print(request);

        // Read response with timeout - use yield() to let AsyncWebServer process
        unsigned long start = millis();
        unsigned long lastWsLoop = millis();
        while (!localClient.available() && millis() - start < 30000) {
            yield();
            delay(10);
            // Call _ws.loop() every 500ms to handle pings and keep connection alive
            if (millis() - lastWsLoop > 500) {
                _ws.loop();
                lastWsLoop = millis();
            }
        }

        if (!localClient.available()) {
            sendErrorResponse(reqIdStr.c_str(), 504, "Local server timeout");
            localClient.stop();
            return;
        }

        // Parse HTTP response
        String statusLine = localClient.readStringUntil('\n');
        int statusCode = 200;
        int spaceIdx = statusLine.indexOf(' ');
        if (spaceIdx > 0) {
            statusCode = statusLine.substring(spaceIdx + 1).toInt();
        }

        // Read headers
        String line;
        int contentLength = -1;
        std::vector<std::pair<String, String>> headerList;

        while (true) {
            line = localClient.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) break;

            int colonIdx = line.indexOf(':');
            if (colonIdx > 0) {
                String hKey = line.substring(0, colonIdx);
                String hVal = line.substring(colonIdx + 1);
                hVal.trim();
                hKey.trim();

                String hKeyLower = hKey;
                hKeyLower.toLowerCase();
                if (hKeyLower != "transfer-encoding" && hKeyLower != "connection") {
                    headerList.push_back({hKey, hVal});
                }
                if (hKeyLower == "content-length") {
                    contentLength = hVal.toInt();
                }
            }
        }

        Serial.printf("[Tunnel] Response status: %d, Content-Length: %d\n", statusCode, contentLength);

        // Read body
        String bodyBase64 = "";
        if (contentLength > 0) {
            // Allocate in PSRAM for large responses
            uint8_t* buf = nullptr;
            if (contentLength > 32000 && ESP.getFreePsram() > (size_t)(contentLength + 50000)) {
                buf = (uint8_t*)ps_malloc(contentLength);
                Serial.printf("[Tunnel] Allocated %d bytes in PSRAM for body\n", contentLength);
            } else {
                buf = (uint8_t*)malloc(contentLength);
            }

            if (buf) {
                int bytesRead = 0;
                unsigned long lastWsLoop = millis();
                start = millis();
                while (bytesRead < contentLength && millis() - start < 30000) {
                    if (localClient.available()) {
                        int r = localClient.read(buf + bytesRead, contentLength - bytesRead);
                        if (r > 0) bytesRead += r;
                    } else {
                        yield();
                        delay(1);
                    }
                    // Call _ws.loop() every 500ms to handle pings and keep connection alive
                    if (millis() - lastWsLoop > 500) {
                        _ws.loop();
                        lastWsLoop = millis();
                    }
                }
                Serial.printf("[Tunnel] Read %d/%d bytes from local server\n", bytesRead, contentLength);
                if (bytesRead > 0) {
                    bodyBase64 = base64::encode(buf, bytesRead);
                }
                free(buf);
            } else {
                Serial.printf("[Tunnel] ERROR: Failed to allocate %d bytes for body!\n", contentLength);
            }
        } else if (contentLength == -1) {
            // Chunked or read until close
            std::vector<uint8_t> bodyBuf;
            bodyBuf.reserve(8192);
            unsigned long lastWsLoop = millis();
            start = millis();
            while (millis() - start < 30000) {
                if (localClient.available()) {
                    uint8_t b = localClient.read();
                    bodyBuf.push_back(b);
                    start = millis();
                } else if (!localClient.connected()) {
                    break;
                } else {
                    yield();
                    delay(1);
                }
                // Call _ws.loop() every 500ms to handle pings and keep connection alive
                if (millis() - lastWsLoop > 500) {
                    _ws.loop();
                    lastWsLoop = millis();
                }
            }
            if (!bodyBuf.empty()) {
                Serial.printf("[Tunnel] Read %d bytes (chunked)\n", bodyBuf.size());
                bodyBase64 = base64::encode(bodyBuf.data(), bodyBuf.size());
            }
        }

        localClient.stop();

        // Build JSON response MANUALLY to avoid ArduinoJson buffer corruption
        Serial.printf("[Tunnel] Building JSON manually, body base64 size: %d\n", bodyBase64.length());

        String* respStr = new String();
        respStr->reserve(bodyBase64.length() + 2048);

        *respStr = "{\"type\":\"http_response\",\"reqId\":\"";
        *respStr += reqIdStr;
        *respStr += "\",\"status\":";
        *respStr += String(statusCode);
        *respStr += ",\"headers\":{";

        // Add headers
        bool firstHeader = true;
        for (const auto& h : headerList) {
            if (!firstHeader) *respStr += ",";
            *respStr += "\"";
            *respStr += escapeJsonString(h.first);
            *respStr += "\":\"";
            *respStr += escapeJsonString(h.second);
            *respStr += "\"";
            firstHeader = false;
        }

        *respStr += "},\"body\":\"";
        *respStr += bodyBase64;
        *respStr += "\"}";

        Serial.printf("[Tunnel] Response JSON size: %d bytes\n", respStr->length());

        _ws.sendTXT(*respStr);
        Serial.printf("[Tunnel] Response sent: %d (%d bytes)\n", statusCode, respStr->length());

        delete respStr;
    }

    // Helper to escape special characters in JSON strings
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
        DynamicJsonDocument doc(2048);
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
        // Decode using mbedtls
        size_t outputLen = 0;
        // First call to get required length
        mbedtls_base64_decode(nullptr, 0, &outputLen,
                              (const unsigned char*)input.c_str(), input.length());

        if (outputLen == 0) return "";

        unsigned char* buf = (unsigned char*)malloc(outputLen + 1);
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
