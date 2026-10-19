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
    SpiRamJsonDocument doc(1024);
    doc["IEEE"] = IEEE;
    doc["cluster"] = cluster;
    // Ligne de template propre a un endpoint : meme nom lisible qu'en MQTT ("0_ep2"), plutot
    // que la cle interne (cf. epAttrLabel, config.h).
    {
        bool digits = attribut.length() > 0;
        for (size_t k = 0; k < attribut.length() && digits; k++) digits = isDigit(attribut[k]);
        if (digits) attribut = epAttrLabel((uint32_t)strtoul(attribut.c_str(), nullptr, 10));
    }
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