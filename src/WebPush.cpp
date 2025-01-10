#include <Arduino.h>
#include "WebPush.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>


extern ConfigGeneralStruct ConfigGeneral;

String getAuthHeader() {
    String authString = String(ConfigGeneral.userWebPush) + ":" + String(ConfigGeneral.passWebPush);
    String encodedAuth = encodeBase64(authString);
    return "Basic " + encodedAuth;
}


void WebPush(String IEEE, String cluster, String attribut, String value)
{
    HTTPClient http;
    int httpResponseCode; 
    http.begin(ConfigGeneral.servWebPush); // Spécification de l'URL pour la requête POST
    if (ConfigGeneral.webPushAuth)
    {
        http.addHeader("Authorization", getAuthHeader());
    }
    http.addHeader("Content-Type", "application/json");

    // Création du JSON à envoyer
    DynamicJsonDocument doc(1024);
    doc["IEEE"] = IEEE;
    doc["cluster"] = cluster;
    doc["attribute"] = attribut;
    doc["value"] = value;

    String jsonString;
    serializeJson(doc, jsonString);

    DEBUG_PRINTLN(jsonString);
    httpResponseCode = http.POST(jsonString); // Envoi de la requête POST

    if (httpResponseCode > 0) {
        String response = http.getString();
        DEBUG_PRINTLN("Response: " + response);
    } else {
        DEBUG_PRINT("Error on POST request: ");
        DEBUG_PRINTLN(httpResponseCode);
    }
    http.end();

    //(1000); // Délai entre les requêtes  
}