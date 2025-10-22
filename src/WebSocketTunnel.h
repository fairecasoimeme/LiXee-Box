#ifndef WEBSOCKET_TUNNEL_H
#define WEBSOCKET_TUNNEL_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>

#include "config.h"

// Forward declarations UNIQUEMENT (pas de redéfinition!)
class WebSocketsClient;

// ============================================
// MACROS PSRAM
// ============================================
#define PSRAM_MALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM)
#define PSRAM_FREE(ptr) if(ptr) { heap_caps_free(ptr); ptr = nullptr; }

// ============================================
// CONFIGURATION
// ============================================
struct WebSocketTunnelConfig {
    const char* deviceToken;
    const char* tunnelHost;
    int tunnelPort;
    const char* tunnelPath;
    const char* deviceName;
    
    const char* localHttpUser;
    const char* localHttpPass;
    
    uint32_t reconnectInterval;
    uint32_t heartbeatInterval;
    bool debugEnabled;
    
    // Paramètres PSRAM
    bool usePSRAM;
    size_t jsonBufferSize;
    size_t httpBufferSize;
    bool enableMemoryMonitoring;
    uint32_t memoryMonitorInterval;
    
    WebSocketTunnelConfig() :
        deviceToken(""),
        tunnelHost("lixee-box.fr"),
        tunnelPort(80),
        tunnelPath("/device"),
        deviceName("ESP32-Device"),
        localHttpUser("admin"),
        localHttpPass("admin"),
        reconnectInterval(5000),
        heartbeatInterval(30000),
        debugEnabled(true),
        usePSRAM(true),
        jsonBufferSize(16384),
        httpBufferSize(32768),
        enableMemoryMonitoring(true),
        memoryMonitorInterval(30000)
    {}
};

// ============================================
// STATISTIQUES MÉMOIRE
// ============================================
struct MemoryStats {
    uint32_t totalPSRAM;
    uint32_t freePSRAM;
    uint32_t usedPSRAM;
    uint32_t totalDRAM;
    uint32_t freeDRAM;
    uint32_t usedDRAM;
    uint32_t largestFreeBlock;
    uint32_t timestamp;
    
    void update();
    void print() const;
};

// ============================================
// CALLBACKS
// ============================================
typedef std::function<void(bool connected)> ConnectionCallback;
typedef std::function<void(const String& deviceId, const String& tunnelUrl)> ConnectedCallback;
typedef std::function<void(const char* requestId, const char* method, const char* path, JsonDocument& doc)> HttpRequestCallback;
typedef std::function<void(const MemoryStats& stats)> MemoryStatsCallback;

// ============================================
// CLASSE PRINCIPALE
// ============================================
class WebSocketTunnel {
public:
    WebSocketTunnel();
    ~WebSocketTunnel();
    
    // Initialisation
    bool begin(const WebSocketTunnelConfig& config);
    void end();
    
    // Gestion du cycle de vie
    void loop();
    bool isConnected() const { return _isConnected; }
    String getDeviceId() const { return _deviceId; }
    String getTunnelUrl() const { return _tunnelUrl; }
    
    // Configuration des callbacks
    void onConnectionChange(ConnectionCallback callback);
    void onConnected(ConnectedCallback callback);
    void onHttpRequest(HttpRequestCallback callback);
    void onMemoryStats(MemoryStatsCallback callback);
    
    // Envoi de données
    bool sendHttpResponse(const char* requestId, int status, const String& contentType, const String& body);
    bool sendHeartbeat();
    bool sendText(const String& payload);
    
    // Gestion manuelle
    void disconnect();
    void reconnect();
    
    // Gestion mémoire
    MemoryStats getMemoryStats();
    void printMemoryStats();
    size_t getPSRAMUsage() const;
    size_t getDRAMUsage() const;
    void optimizeMemory();
    
    // Getters
    WebSocketTunnelConfig getConfig() const { return _config; }
    uint32_t getLastPingTime() const { return _lastPing; }
    
private:
    // Configuration
    WebSocketTunnelConfig _config;
    
    // WebSocket client (pointeur pour éviter include)
    WebSocketsClient* _webSocket;
    
    // État
    bool _isConnected;
    String _deviceId;
    String _tunnelUrl;
    uint32_t _lastPing;
    
    // Buffers PSRAM
    char* _httpBuffer;
    char* _jsonBuffer;
    uint32_t _lastMemoryCheck;
    MemoryStats _memStats;
    
    // Callbacks
    ConnectionCallback _connectionCallback;
    ConnectedCallback _connectedCallback;
    HttpRequestCallback _httpRequestCallback;
    MemoryStatsCallback _memoryStatsCallback;
    
    // Méthodes privées - utilisent des types génériques pour éviter dépendances
    static void webSocketEventStatic(uint8_t type, uint8_t* payload, size_t length);
    void handleWebSocketEvent(uint8_t type, uint8_t* payload, size_t length);
    void handleConnected(JsonDocument& doc);
    void handleHttpRequest(JsonDocument& doc);
    void handlePong();
    void sendConnectMessage();
    void processHttpRequest(const char* requestId, const char* method, const char* path, JsonDocument& requestDoc);
    
    // Gestion PSRAM
    bool allocateBuffers();
    void freeBuffers();
    void monitorMemory();
    void* allocatePSRAM(size_t size, const char* name = "buffer");
    
    // Instance statique pour callback
    static WebSocketTunnel* _instance;
};

#endif // WEBSOCKET_TUNNEL_H