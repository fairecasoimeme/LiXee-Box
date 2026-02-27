/**
 * TunnelClient.h
 * Classe pour gérer le client WebSocket tunnel
 */

#ifndef TUNNEL_CLIENT_H
#define TUNNEL_CLIENT_H

#include <WiFi.h>
#include <ArduinoJson.h>
#include "config.h"
#include <functional>

class TunnelClient {
public:
    // Constructeur
    TunnelClient(const char* host, uint16_t port, const char* path, 
                 const char* deviceId, const char* authToken);
    
    // Initialisation
    void begin();
    
    // À appeler dans loop()
    void loop();
    
    // État de la connexion
    bool isConnected() const { return _connected; }
    
    // Callbacks
    typedef std::function<void(const char* method, const char* path, 
                               const char* requestId)> HttpRequestCallback;
    typedef std::function<void(bool connected)> StatusCallback;
    
    void onHttpRequest(HttpRequestCallback callback) { _onHttpRequest = callback; }
    void onStatusChange(StatusCallback callback) { _onStatusChange = callback; }
    
    // Envoyer une réponse HTTP via le tunnel
    void sendHttpResponse(const char* requestId, int statusCode, 
                         const char* contentType, const String& body);
    
    // Reconnecter manuellement
    void reconnect();
    
private:
    // Configuration
    const char* _host;
    uint16_t _port;
    const char* _path;
    const char* _deviceId;
    const char* _authToken;
    
    // État
    WiFiClient _client;
    bool _connected;
    unsigned long _lastReconnect;
    const unsigned long _reconnectInterval = 5000;
    
    // Callbacks
    HttpRequestCallback _onHttpRequest;
    StatusCallback _onStatusChange;
    
    // Méthodes privées
    bool connect();
    void disconnect();
    String generateWebSocketKey();
    void sendWebSocketFrame(const String& payload);
    void sendAuthentication();
    void sendPong();
    void readWebSocketFrame();
    void handleMessage(const String& payload);
    void setConnected(bool connected);
};

#endif // TUNNEL_CLIENT_H