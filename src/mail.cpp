#include <Arduino.h>
#include "mail.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h> // Inclusion de LittleFS

extern ConfigGeneralStruct ConfigGeneral;


bool awaitSMTPResponse(WiFiClientSecure &client, int expectedCode) {
    while (!client.available()) {
        vTaskDelay(500);
    }

    String response = client.readStringUntil('\n');
    Serial.println(response);
    if (response.startsWith(String(expectedCode))) {
       return true;
    }

    return false;
}



String encodeBase64(const String &input) {
    static const char base64Digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String output;
    int val = 0, valb = -6;
    for (uint8_t c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            output += base64Digits[(val >> valb) & 0x3F];
            valb -= 6;
        }
    }
    if (valb > -6) output += base64Digits[((val << 8) >> (valb + 8)) & 0x3F];
    while (output.length() % 4) output += '=';
    return output;

}


void initMailClient()
{
    WiFiClientSecure smtp;
    smtp.setInsecure();
    //if (!smtp.connect(ConfigGeneral.servMQTT,atoi(ConfigGeneral.portSMTP)))
    if (!smtp.connect("193.70.18.147",465))
    {
        DEBUG_PRINTLN("connection failed");
    }

    // Login to server
    smtp.println("EHLO esp32");
    if (!awaitSMTPResponse(smtp, 250))
    {
         DEBUG_PRINTLN("NOK 250");
    }

    smtp.println("AUTH LOGIN");
    if (!awaitSMTPResponse(smtp, 334))
    {
         DEBUG_PRINTLN("NOK 334");
    }

    smtp.println(encodeBase64(ConfigGeneral.userSMTP));
    if (!awaitSMTPResponse(smtp, 334))
    {
         DEBUG_PRINTLN("NOK 334");
    }

    smtp.println(encodeBase64(ConfigGeneral.passSMTP));
    if (!awaitSMTPResponse(smtp, 235))
    {
         DEBUG_PRINTLN("NOK 235");
    }

    // Send email
    smtp.println("MAIL FROM: <" + String(ConfigGeneral.userSMTP) + ">");
    if (!awaitSMTPResponse(smtp, 250)){
         DEBUG_PRINTLN("NOK 250");
    }

    smtp.println("RCPT TO: <" + String(ConfigGeneral.userSMTP) + ">");
    if (!awaitSMTPResponse(smtp, 250))
    {
         DEBUG_PRINTLN("NOK 250");
    }

    smtp.println("DATA");
    if (!awaitSMTPResponse(smtp, 354))
    {
         DEBUG_PRINTLN("NOK 354");
    }

    String message = "From: ESP32 <" + String(ConfigGeneral.userSMTP) + ">\r\n";
    message += "To: Destinataire <"+ String(ConfigGeneral.userSMTP) +">\r\n";
    message += "Subject: Test Email from ESP32\r\n";
    message += "Content-type: text/html\r\n";
    message += "\r\n";
    message += "<div style=\"color:#2c3e50;\"><h1>Salut depuis ESP32</h1><p>Ceci est un e-mail envoyé par ESP32.</p></div>\r\n";
    message += ".\r\n";

    smtp.println(message);
    if (!awaitSMTPResponse(smtp, 250))
    {
         DEBUG_PRINTLN("NOK 250");
    }

    // Quit
    smtp.println("QUIT");
    awaitSMTPResponse(smtp, 221);

    DEBUG_PRINTLN("Email envoyé avec succès!");

}

