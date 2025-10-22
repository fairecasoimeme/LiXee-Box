#include "WebSocketTunnel.h"
#include <WebSocketsClient_Generic.h>  // ✅ Include SEULEMENT dans le .cpp

// Instance statique
WebSocketTunnel* WebSocketTunnel::_instance = nullptr;

// ============================================
// IMPLÉMENTATION MemoryStats
// ============================================
void MemoryStats::update() {
    timestamp = millis();
    totalPSRAM = ESP.getPsramSize();
    freePSRAM = ESP.getFreePsram();
    usedPSRAM = totalPSRAM - freePSRAM;
    totalDRAM = ESP.getHeapSize();
    freeDRAM = ESP.getFreeHeap();
    usedDRAM = totalDRAM - freeDRAM;
    largestFreeBlock = ESP.getMaxAllocHeap();
}

void MemoryStats::print() const {
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║       MEMORY STATISTICS              ║");
    Serial.println("╠══════════════════════════════════════╣");
    Serial.printf("║ PSRAM: %7lu / %7lu KB (%.1f%%) ║\n", 
        usedPSRAM/1024, totalPSRAM/1024, 
        totalPSRAM > 0 ? (float)usedPSRAM*100/totalPSRAM : 0);
    Serial.printf("║ DRAM:  %7lu / %7lu KB (%.1f%%) ║\n", 
        usedDRAM/1024, totalDRAM/1024, 
        (float)usedDRAM*100/totalDRAM);
    Serial.printf("║ Largest block: %7lu KB        ║\n", 
        largestFreeBlock/1024);
    Serial.println("╚══════════════════════════════════════╝");
}

// ============================================
// CONSTRUCTEUR / DESTRUCTEUR
// ============================================
WebSocketTunnel::WebSocketTunnel() :
    _webSocket(nullptr),
    _isConnected(false),
    _deviceId(""),
    _tunnelUrl(""),
    _lastPing(0),
    _httpBuffer(nullptr),
    _jsonBuffer(nullptr),
    _lastMemoryCheck(0),
    _connectionCallback(nullptr),
    _connectedCallback(nullptr),
    _httpRequestCallback(nullptr),
    _memoryStatsCallback(nullptr)
{
    _instance = this;
}

WebSocketTunnel::~WebSocketTunnel() {
    end();
    _instance = nullptr;
}

// ============================================
// ALLOCATION BUFFERS PSRAM
// ============================================
bool WebSocketTunnel::allocateBuffers() {
    if (!_config.usePSRAM) {
        if (_config.debugEnabled) {
            Serial.println("⚠️ PSRAM désactivé");
        }
        return true;
    }
    
    size_t freePSRAM = ESP.getFreePsram();
    size_t requiredSize = _config.httpBufferSize + _config.jsonBufferSize;
    
    if (freePSRAM < requiredSize) {
        log_e("PSRAM insuffisante: requis=%d, dispo=%d", requiredSize, freePSRAM);
        return false;
    }
    
    _httpBuffer = (char*)allocatePSRAM(_config.httpBufferSize, "HTTP");
    if (!_httpBuffer) {
        log_e("Échec allocation HTTP buffer");
        return false;
    }
    
    _jsonBuffer = (char*)allocatePSRAM(_config.jsonBufferSize, "JSON");
    if (!_jsonBuffer) {
        PSRAM_FREE(_httpBuffer);
        log_e("Échec allocation JSON buffer");
        return false;
    }
    
    if (_config.debugEnabled) {
        Serial.println("\n✅ Buffers PSRAM alloués:");
        Serial.printf("   HTTP: %d KB\n", _config.httpBufferSize/1024);
        Serial.printf("   JSON: %d KB\n", _config.jsonBufferSize/1024);
        Serial.printf("   Total: %d KB\n", requiredSize/1024);
    }
    
    return true;
}

void* WebSocketTunnel::allocatePSRAM(size_t size, const char* name) {
    void* ptr = PSRAM_MALLOC(size);
    if (ptr) {
        memset(ptr, 0, size);
        if (_config.debugEnabled) {
            Serial.printf("✅ PSRAM alloc [%s]: %d bytes\n", name, size);
        }
    } else {
        log_e("❌ PSRAM alloc failed [%s]: %d bytes", name, size);
    }
    return ptr;
}

void WebSocketTunnel::freeBuffers() {
    PSRAM_FREE(_httpBuffer);
    PSRAM_FREE(_jsonBuffer);
    
    if (_config.debugEnabled) {
        Serial.println("🗑️ Buffers PSRAM libérés");
    }
}

// ============================================
// MONITORING MÉMOIRE
// ============================================
void WebSocketTunnel::monitorMemory() {
    if (!_config.enableMemoryMonitoring) return;
    
    uint32_t now = millis();
    if (now - _lastMemoryCheck < _config.memoryMonitorInterval) {
        return;
    }
    
    _lastMemoryCheck = now;
    _memStats.update();
    
    if (_config.debugEnabled) {
        _memStats.print();
    }
    
    if (_memoryStatsCallback) {
        _memoryStatsCallback(_memStats);
    }
    
    if (_memStats.freePSRAM < 100000) {
        log_w("⚠️ PSRAM faible: %d KB", _memStats.freePSRAM/1024);
    }
    if (_memStats.freeDRAM < 50000) {
        log_w("⚠️ DRAM faible: %d KB", _memStats.freeDRAM/1024);
    }
}

MemoryStats WebSocketTunnel::getMemoryStats() {
    _memStats.update();
    return _memStats;
}

void WebSocketTunnel::printMemoryStats() {
    _memStats.update();
    _memStats.print();
}

size_t WebSocketTunnel::getPSRAMUsage() const {
    return ESP.getPsramSize() - ESP.getFreePsram();
}

size_t WebSocketTunnel::getDRAMUsage() const {
    return ESP.getHeapSize() - ESP.getFreeHeap();
}

void WebSocketTunnel::optimizeMemory() {
    if (_config.debugEnabled) {
        Serial.println("🔧 Optimisation mémoire...");
    }
    
    if (_httpBuffer) memset(_httpBuffer, 0, _config.httpBufferSize);
    if (_jsonBuffer) memset(_jsonBuffer, 0, _config.jsonBufferSize);
    
    if (_config.debugEnabled) {
        Serial.println("✅ Mémoire optimisée");
        printMemoryStats();
    }
}

// ============================================
// INITIALISATION
// ============================================
bool WebSocketTunnel::begin(const WebSocketTunnelConfig& config) {
    _config = config;
    
    if (_config.debugEnabled) {
        Serial.println("\n╔══════════════════════════════════════╗");
        Serial.println("║   WEBSOCKET TUNNEL - INIT PSRAM      ║");
        Serial.println("╠══════════════════════════════════════╣");
        Serial.printf("║ Host: %-30s ║\n", _config.tunnelHost);
        Serial.printf("║ Port: %-30d ║\n", _config.tunnelPort);
        Serial.printf("║ Device: %-28s ║\n", _config.deviceName);
        Serial.printf("║ PSRAM: %-29s ║\n", _config.usePSRAM ? "ENABLED" : "DISABLED");
        Serial.println("╚══════════════════════════════════════╝");
    }
    
    // Allouer buffers PSRAM
    if (!allocateBuffers()) {
        log_e("Échec allocation buffers");
        return false;
    }
    
    // État initial mémoire
    _memStats.update();
    if (_config.debugEnabled) {
        _memStats.print();
    }
    
    // Créer instance WebSocket
    _webSocket = new WebSocketsClient();
    if (!_webSocket) {
        log_e("Échec création WebSocketsClient");
        freeBuffers();
        return false;
    }
    
    // Configuration WebSocket avec callback lambda qui appelle la méthode statique
    _webSocket->begin(_config.tunnelHost, _config.tunnelPort, _config.tunnelPath);
    _webSocket->onEvent([](WStype_t type, uint8_t* payload, size_t length) {
        WebSocketTunnel::webSocketEventStatic((uint8_t)type, payload, length);
    });
    _webSocket->setReconnectInterval(_config.reconnectInterval);
    
    if (_config.debugEnabled) {
        Serial.println("✅ WebSocket tunnel configuré");
    }
    
    return true;
}

void WebSocketTunnel::end() {
    if (_webSocket) {
        _webSocket->disconnect();
        delete _webSocket;
        _webSocket = nullptr;
    }
    
    freeBuffers();
    _isConnected = false;
    _deviceId = "";
    _tunnelUrl = "";
    
    if (_config.debugEnabled) {
        Serial.println("🔴 WebSocket tunnel arrêté");
    }
}

// ============================================
// BOUCLE PRINCIPALE
// ============================================
void WebSocketTunnel::loop() {
    if (_webSocket) {
        _webSocket->loop();
    }
    
    // Heartbeat automatique
    if (_isConnected && (millis() - _lastPing > _config.heartbeatInterval)) {
        sendHeartbeat();
    }
    
    // Monitoring mémoire
    monitorMemory();
}

// ============================================
// ÉVÉNEMENTS WEBSOCKET
// ============================================
void WebSocketTunnel::webSocketEventStatic(uint8_t type, uint8_t* payload, size_t length) {
    if (_instance) {
        _instance->handleWebSocketEvent(type, payload, length);
    }
}

void WebSocketTunnel::handleWebSocketEvent(uint8_t type, uint8_t* payload, size_t length) {
    // Cast uint8_t vers WStype_t (défini dans la bibliothèque)
    WStype_t wsType = (WStype_t)type;
    
    switch(wsType) {
        case WStype_DISCONNECTED:
            if (_config.debugEnabled) {
                Serial.println("🔴 Déconnecté du serveur tunnel");
            }
            _isConnected = false;
            _deviceId = "";
            _tunnelUrl = "";
            
            if (_connectionCallback) {
                _connectionCallback(false);
            }
            break;

        case WStype_CONNECTED:
            if (_config.debugEnabled) {
                Serial.println("🟢 Connecté au serveur tunnel");
            }
            sendConnectMessage();
            break;

        case WStype_TEXT: {
            if (_config.debugEnabled) {
                Serial.printf("📨 Message reçu (%d bytes)\n", length);
            }

            SpiRamJsonDocument doc(_config.jsonBufferSize);
            DeserializationError error = deserializeJson(doc, payload, length);
            
            if (error) {
                log_e("❌ Erreur JSON: %s", error.c_str());
                return;
            }

            const char* msgType = doc["type"];
            
            if (strcmp(msgType, "connected") == 0) {
                handleConnected(doc);
            }
            else if (strcmp(msgType, "http_request") == 0) {
                handleHttpRequest(doc);
            }
            else if (strcmp(msgType, "pong") == 0) {
                handlePong();
            }
            break;
        }

        case WStype_ERROR:
            log_e("❌ Erreur WebSocket");
            break;
            
        default:
            break;
    }
}

// ============================================
// GESTION DES MESSAGES
// ============================================
void WebSocketTunnel::sendConnectMessage() {
    StaticJsonDocument<256> doc;
    doc["type"] = "connect";
    doc["token"] = _config.deviceToken;
    doc["name"] = _config.deviceName;
    doc["version"] = "1.0.0-PSRAM";
    
    String output;
    output.reserve(256);
    serializeJson(doc, output);
    
    if (_webSocket) {
        _webSocket->sendTXT(output);
    }
    
    if (_config.debugEnabled) {
        Serial.println("📤 Demande de connexion envoyée");
    }
}

void WebSocketTunnel::handleConnected(JsonDocument& doc) {
    _isConnected = true;
    _deviceId = doc["deviceId"].as<String>();
    _tunnelUrl = doc["tunnelUrl"].as<String>();
    
    if (_config.debugEnabled) {
        Serial.println("\n╔══════════════════════════════════════╗");
        Serial.println("║   CONNEXION TUNNEL CONFIRMÉE         ║");
        Serial.println("╠══════════════════════════════════════╣");
        Serial.printf("║ Device ID: %-26s ║\n", _deviceId.c_str());
        Serial.printf("║ URL: %-32s ║\n", _tunnelUrl.c_str());
        Serial.println("╚══════════════════════════════════════╝");
    }
    
    if (_connectionCallback) {
        _connectionCallback(true);
    }
    
    if (_connectedCallback) {
        _connectedCallback(_deviceId, _tunnelUrl);
    }
}

void WebSocketTunnel::handleHttpRequest(JsonDocument& doc) {
    const char* requestId = doc["requestId"];
    const char* method = doc["method"];
    const char* path = doc["path"];
    
    if (_config.debugEnabled) {
        Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        Serial.printf("🌐 HTTP: %s %s\n", method, path);
        Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    }
    
    if (_httpRequestCallback) {
        _httpRequestCallback(requestId, method, path, doc);
    } else {
        processHttpRequest(requestId, method, path, doc);
    }
}

void WebSocketTunnel::handlePong() {
    _lastPing = millis();
    if (_config.debugEnabled) {
        Serial.println("💓 Pong reçu");
    }
}

// ============================================
// TRAITEMENT HTTP OPTIMISÉ PSRAM
// ============================================
void WebSocketTunnel::processHttpRequest(const char* requestId, const char* method, 
                                         const char* path, JsonDocument& requestDoc) {
    HTTPClient http;
    String url = "http://127.0.0.1:80" + String(path);
    
    http.begin(url);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    if (strlen(_config.localHttpUser) > 0) {
        http.setAuthorization(_config.localHttpUser, _config.localHttpPass);
    }
    
    if (requestDoc.containsKey("headers")) {
        JsonObject headers = requestDoc["headers"];
        for (JsonPair kv : headers) {
            if (strcmp(kv.key().c_str(), "host") != 0) {
                http.addHeader(kv.key().c_str(), kv.value().as<String>());
            }
        }
    }
    
    int httpCode = 0;
    String response;
    
    if (strcmp(method, "GET") == 0) {
        httpCode = http.GET();
    } 
    else if (strcmp(method, "POST") == 0) {
        String body = requestDoc["body"].as<String>();
        httpCode = http.POST(body);
    }
    else {
        httpCode = http.GET();
    }
    
    if (httpCode > 0) {
        if (_httpBuffer && _config.usePSRAM) {
            WiFiClient* stream = http.getStreamPtr();
            size_t bytesRead = 0;
            
            while (stream->available() && bytesRead < _config.httpBufferSize - 1) {
                _httpBuffer[bytesRead++] = stream->read();
            }
            _httpBuffer[bytesRead] = '\0';
            response = String(_httpBuffer);
        } else {
            response = http.getString();
        }
    } else {
        httpCode = 500;
        response = "{\"error\":\"Erreur serveur\"}";
    }
    
    size_t responseSize = response.length() + 2048;
    SpiRamJsonDocument responseDoc(responseSize);
    
    responseDoc["type"] = "http_response";
    responseDoc["requestId"] = requestId;
    responseDoc["status"] = httpCode;
    
    JsonObject responseHeaders = responseDoc.createNestedObject("headers");
    
    for(int i = 0; i < http.headers(); i++) {
        responseHeaders[http.headerName(i)] = http.header(i);
    }
    
    if (!responseHeaders.containsKey("Content-Type")) {
        responseHeaders["Content-Type"] = "text/html";
    }
    responseHeaders["Access-Control-Allow-Origin"] = "*";
    
    responseDoc["body"] = response;
    
    String output;
    output.reserve(responseSize);
    serializeJson(responseDoc, output);
    
    if (_webSocket) {
        _webSocket->sendTXT(output);
    }
    
    http.end();
    
    if (_config.debugEnabled) {
        Serial.printf("✅ Réponse: %d (%d bytes)\n", httpCode, response.length());
        Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    }
}

// ============================================
// ENVOI DE DONNÉES
// ============================================
bool WebSocketTunnel::sendHttpResponse(const char* requestId, int status, 
                                       const String& contentType, const String& body) {
    size_t docSize = body.length() + 512;
    SpiRamJsonDocument doc(docSize);
    
    doc["type"] = "http_response";
    doc["requestId"] = requestId;
    doc["status"] = status;
    
    JsonObject headers = doc.createNestedObject("headers");
    headers["Content-Type"] = contentType;
    headers["Access-Control-Allow-Origin"] = "*";
    
    doc["body"] = body;
    
    String output;
    output.reserve(docSize);
    serializeJson(doc, output);
    
    bool success = false;
    if (_webSocket) {
        success = _webSocket->sendTXT(output);
    }
    
    if (_config.debugEnabled && success) {
        Serial.printf("📤 Réponse envoyée (%d) - %d bytes\n", status, body.length());
    }
    
    return success;
}

bool WebSocketTunnel::sendHeartbeat() {
    if (!_isConnected || !_webSocket) {
        return false;
    }
    
    StaticJsonDocument<64> doc;
    doc["type"] = "ping";
    
    String output;
    serializeJson(doc, output);
    
    bool success = _webSocket->sendTXT(output);
    
    if (success) {
        _lastPing = millis();
        if (_config.debugEnabled) {
            Serial.println("💓 Ping envoyé");
        }
    }
    
    return success;
}

bool WebSocketTunnel::sendText(const String& payload) {
    if (_webSocket) {
        return _webSocket->sendTXT(payload);
    }
    return false;
}

// ============================================
// CONFIGURATION DES CALLBACKS
// ============================================
void WebSocketTunnel::onConnectionChange(ConnectionCallback callback) {
    _connectionCallback = callback;
}

void WebSocketTunnel::onConnected(ConnectedCallback callback) {
    _connectedCallback = callback;
}

void WebSocketTunnel::onHttpRequest(HttpRequestCallback callback) {
    _httpRequestCallback = callback;
}

void WebSocketTunnel::onMemoryStats(MemoryStatsCallback callback) {
    _memoryStatsCallback = callback;
}

// ============================================
// GESTION MANUELLE
// ============================================
void WebSocketTunnel::disconnect() {
    if (_webSocket) {
        _webSocket->disconnect();
    }
}

void WebSocketTunnel::reconnect() {
    if (_webSocket) {
        _webSocket->disconnect();
        delay(1000);
        _webSocket->begin(_config.tunnelHost, _config.tunnelPort, _config.tunnelPath);
    }
}