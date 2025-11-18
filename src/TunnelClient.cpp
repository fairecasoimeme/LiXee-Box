/**
 * TunnelClient.cpp
 * Implémentation du client WebSocket tunnel
 */

#include "TunnelClient.h"
#include <mbedtls/base64.h>

TunnelClient::TunnelClient(const char* host, uint16_t port, const char* path,
                           const char* deviceId, const char* authToken)
    : _host(host), _port(port), _path(path), 
      _deviceId(deviceId), _authToken(authToken),
      _connected(false), _lastReconnect(0),
      _onHttpRequest(nullptr), _onStatusChange(nullptr) {
}

void TunnelClient::begin() {
    Serial.println("[TunnelClient] Initialisation");
    connect();
}

void TunnelClient::loop() {
    if (_client.connected()) {
        if (_client.available()) {
            readWebSocketFrame();
        }
    } else if (_connected) {
        Serial.println("[TunnelClient] Déconnecté");
        setConnected(false);
    } else if (millis() - _lastReconnect > _reconnectInterval) {
        _lastReconnect = millis();
        connect();
    }
}

void TunnelClient::reconnect() {
    if (_client.connected()) {
        _client.stop();
    }
    _lastReconnect = 0; // Force reconnexion immédiate
}

String TunnelClient::generateWebSocketKey() {
    uint8_t key[16];
    for(int i = 0; i < 16; i++) {
        key[i] = random(256);
    }
    
    unsigned char output[25];
    size_t olen;
    mbedtls_base64_encode(output, sizeof(output), &olen, key, 16);
    output[olen] = 0;
    
    return String((char*)output);
}

bool TunnelClient::connect() {
    Serial.printf("[TunnelClient] Connexion à %s:%d%s\n", _host, _port, _path);
    
    if (!_client.connect(_host, _port)) {
        Serial.println("[TunnelClient] ❌ Échec TCP");
        return false;
    }
    
    // Upgrade HTTP vers WebSocket
    String key = generateWebSocketKey();
    
    String request = "GET " + String(_path) + " HTTP/1.1\r\n";
    request += "Host: " + String(_host) + "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: " + key + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n\r\n";
    
    _client.print(request);
    
    // Attendre la réponse
    unsigned long timeout = millis() + 5000;
    while (_client.available() == 0) {
        if (millis() > timeout) {
            Serial.println("[TunnelClient] ❌ Timeout");
            _client.stop();
            return false;
        }
        delay(10);
    }
    
    // Lire la réponse HTTP
    String response = "";
    while (_client.available()) {
        response += (char)_client.read();
        if (response.endsWith("\r\n\r\n")) break;
    }
    
    if (response.indexOf("101") == -1) {
        Serial.println("[TunnelClient] ❌ Échec upgrade WebSocket");
        Serial.println(response);
        _client.stop();
        return false;
    }
    
    Serial.println("[TunnelClient] ✅ WebSocket connecté");
    setConnected(true);
    
    // Envoyer l'authentification
    sendAuthentication();
    
    return true;
}

void TunnelClient::disconnect() {
    if (_client.connected()) {
        _client.stop();
    }
    setConnected(false);
}

void TunnelClient::setConnected(bool connected) {
    if (_connected != connected) {
        _connected = connected;
        if (_onStatusChange) {
            _onStatusChange(connected);
        }
    }
}

void TunnelClient::sendWebSocketFrame(const String& payload) {
    if (!_client.connected()) return;
    
    size_t len = payload.length();
    uint8_t header[10];
    size_t headerSize = 2;
    
    header[0] = 0x81; // FIN + Text frame
    
    // Masking (client doit masquer)
    uint8_t mask[4];
    for(int i = 0; i < 4; i++) {
        mask[i] = random(256);
    }
    
    // Payload length
    if (len < 126) {
        header[1] = 0x80 | len;
        headerSize = 2;
    } else if (len < 65536) {
        header[1] = 0x80 | 126;
        header[2] = (len >> 8) & 0xFF;
        header[3] = len & 0xFF;
        headerSize = 4;
    } else {
        header[1] = 0x80 | 127;
        header[2] = 0;
        header[3] = 0;
        header[4] = 0;
        header[5] = 0;
        header[6] = (len >> 24) & 0xFF;
        header[7] = (len >> 16) & 0xFF;
        header[8] = (len >> 8) & 0xFF;
        header[9] = len & 0xFF;
        headerSize = 10;
    }
    
    // Add mask
    for(int i = 0; i < 4; i++) {
        header[headerSize + i] = mask[i];
    }
    headerSize += 4;
    
    // Send header
    _client.write(header, headerSize);
    
    // Send masked payload
    for(size_t i = 0; i < len; i++) {
        _client.write(payload[i] ^ mask[i % 4]);
    }
}

void TunnelClient::sendAuthentication() {
    DynamicJsonDocument doc(512);
    doc["type"] = "auth";
    doc["device_id"] = _deviceId;
    doc["token"] = _authToken;
    
    String json;
    serializeJson(doc, json);
    sendWebSocketFrame(json);
    
    Serial.println("[TunnelClient] Auth envoyée");
}

void TunnelClient::sendPong() {
    DynamicJsonDocument doc(64);
    doc["type"] = "pong";
    
    String json;
    serializeJson(doc, json);
    sendWebSocketFrame(json);
}

void TunnelClient::sendHttpResponse(const char* requestId, int statusCode,
                                    const char* contentType, const String& body) {
    // Utiliser DynamicJsonDocument
    DynamicJsonDocument doc(body.length() + 512);
    doc["type"] = "http_response";
    doc["request_id"] = requestId;
    doc["status_code"] = statusCode;
    doc["content_type"] = contentType;
    doc["body"] = body;
    
    String json;
    serializeJson(doc, json);
    sendWebSocketFrame(json);
    
    Serial.printf("[TunnelClient] Réponse envoyée (%d bytes)\n", json.length());
}

void TunnelClient::handleMessage(const String& payload) {
    // Utiliser DynamicJsonDocument pour les gros messages
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        Serial.printf("[TunnelClient] Erreur JSON: %s\n", error.c_str());
        return;
    }
    
    const char* type = doc["type"];
    
    if (strcmp(type, "http_request") == 0) {
        const char* requestId = doc["request_id"];
        const char* method = doc["method"];
        const char* path = doc["path"];
        
        Serial.printf("[TunnelClient] Requête: %s %s\n", method, path);
        
        // Appeler le callback si défini
        if (_onHttpRequest) {
            _onHttpRequest(method, path, requestId);
        }
    }
    else if (strcmp(type, "ping") == 0) {
        sendPong();
    }
    else if (strcmp(type, "auth_ok") == 0) {
        Serial.println("[TunnelClient] ✅ Auth OK");
    }
    else if (strcmp(type, "auth_failed") == 0) {
        Serial.println("[TunnelClient] ❌ Auth échouée");
        disconnect();
    }
}

void TunnelClient::readWebSocketFrame() {
    if (!_client.available()) return;
    
    // Lire header
    uint8_t header[2];
    _client.readBytes(header, 2);
    
    bool fin = (header[0] & 0x80) != 0;
    uint8_t opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payloadLen = header[1] & 0x7F;
    
    // Lire extended payload length
    if (payloadLen == 126) {
        uint8_t extLen[2];
        _client.readBytes(extLen, 2);
        payloadLen = (extLen[0] << 8) | extLen[1];
    } else if (payloadLen == 127) {
        uint8_t extLen[8];
        _client.readBytes(extLen, 8);
        payloadLen = 0;
        for(int i = 0; i < 8; i++) {
            payloadLen = (payloadLen << 8) | extLen[i];
        }
    }
    
    // Lire mask si présent
    uint8_t mask[4] = {0};
    if (masked) {
        _client.readBytes(mask, 4);
    }
    
    // Lire payload (limiter à 8KB)
    String payload = "";
    payload.reserve(min((uint64_t)8192, payloadLen));
    
    for(uint64_t i = 0; i < payloadLen && i < 8192; i++) {
        if (_client.available()) {
            uint8_t c = _client.read();
            if (masked) c ^= mask[i % 4];
            payload += (char)c;
        }
        
        // Yield régulièrement
        if (i % 512 == 0) yield();
    }
    
    // Traiter selon opcode
    if (opcode == 0x01) { // Text frame
        handleMessage(payload);
    }
    else if (opcode == 0x08) { // Close
        Serial.println("[TunnelClient] Fermeture demandée");
        disconnect();
    }
    else if (opcode == 0x09) { // Ping
        Serial.println("[TunnelClient] Ping WS reçu");
        // Envoyer Pong
        uint8_t pong[2] = {0x8A, 0x00};
        _client.write(pong, 2);
    }
}