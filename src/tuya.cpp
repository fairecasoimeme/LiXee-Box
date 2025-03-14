#include <Arduino.h>
#include <WiFi.h>
#include "tuya.h"
#include <PsychicMqttClient.h>

const char *tuya_root_ca =   "-----BEGIN CERTIFICATE-----\n"
                                "MIIDxTCCAq2gAwIBAgIBADANBgkqhkiG9w0BAQsFADCBgzELMAkGA1UEBhMCVVMx\n"
                                "EDAOBgNVBAgTB0FyaXpvbmExEzARBgNVBAcTClNjb3R0c2RhbGUxGjAYBgNVBAoT\n"
                                "EUdvRGFkZHkuY29tLCBJbmMuMTEwLwYDVQQDEyhHbyBEYWRkeSBSb290IENlcnRp\n"
                                "ZmljYXRlIEF1dGhvcml0eSAtIEcyMB4XDTA5MDkwMTAwMDAwMFoXDTM3MTIzMTIz\n"
                                "NTk1OVowgYMxCzAJBgNVBAYTAlVTMRAwDgYDVQQIEwdBcml6b25hMRMwEQYDVQQH\n"
                                "EwpTY290dHNkYWxlMRowGAYDVQQKExFHb0RhZGR5LmNvbSwgSW5jLjExMC8GA1UE\n"
                                "AxMoR28gRGFkZHkgUm9vdCBDZXJ0aWZpY2F0ZSBBdXRob3JpdHkgLSBHMjCCASIw\n"
                                "DQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAL9xYgjx+lk09xvJGKP3gElY6SKD\n"
                                "E6bFIEMBO4Tx5oVJnyfq9oQbTqC023CYxzIBsQU+B07u9PpPL1kwIuerGVZr4oAH\n"
                                "/PMWdYA5UXvl+TW2dE6pjYIT5LY/qQOD+qK+ihVqf94Lw7YZFAXK6sOoBJQ7Rnwy\n"
                                "DfMAZiLIjWltNowRGLfTshxgtDj6AozO091GB94KPutdfMh8+7ArU6SSYmlRJQVh\n"
                                "GkSBjCypQ5Yj36w6gZoOKcUcqeldHraenjAKOc7xiID7S13MMuyFYkMlNAJWJwGR\n"
                                "tDtwKj9useiciAF9n9T521NtYJ2/LOdYq7hfRvzOxBsDPAnrSTFcaUaz4EcCAwEA\n"
                                "AaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYE\n"
                                "FDqahQcQZyi27/a9BUFuIMGU2g/eMA0GCSqGSIb3DQEBCwUAA4IBAQCZ21151fmX\n"
                                "WWcDYfF+OwYxdS2hII5PZYe096acvNjpL9DbWu7PdIxztDhC2gV7+AJ1uP2lsdeu\n"
                                "9tfeE8tTEH6KRtGX+rcuKxGrkLAngPnon1rpN5+r5N9ss4UXnT3ZJE95kTXWXwTr\n"
                                "gIOrmgIttRD02JDHBHNA7XIloKmf7J6raBKZV8aPEjoJpL1E/QYVN8Gb5DKj7Tjo\n"
                                "2GTzLH4U/ALqn83/B2gX2yKQOC16jdFU8WnjXzPKej17CuPKf1855eJ1usV2GDPO\n"
                                "LPAvTK33sefOT6jEm0pUBsV/fdUID+Ic/n4XuKxe9tQWskMJDE32p2u0mYRlynqI\n"
                                "4uJEvlz36hz1\n"
                                "-----END CERTIFICATE-----\n";

PsychicMqttClient mqttClientTuya;

void initTuya()
{
    mqttClientTuya.setServer("mqtts://m1.tuyacn.com:8883");
    mqttClientTuya.setClientId("tuyalink_26eb8c39a2c9ab5202iede");

    mqttClientTuya.setCredentials("26eb8c39a2c9ab5202iede|signMethod=hmacSha256,timestamp=1737391130,secureMode=1,accessType=1", "ddb7c2acd9ca7b9e7f4516ac4ac9252762189e0996c5b420585eec5fb39816b1");
    
    mqttClientTuya.setCACert(tuya_root_ca);
    String topic = "tylink/26eb8c39a2c9ab5202iede/thing/property/report_response";

    mqttClientTuya.onTopic(topic.c_str(), 0, [&](const char *topic, const char *payload, int retain, int qos, bool dup)
                       {
                        /**
                         * Using a lambda callback function is a very convenient way to handle the 
                         * received message. The function will be called when a message is received. 
                         * 
                         * The parameters are:
                         * - topic: The topic of the received message
                         * - payload: The payload of the received message
                         * - retain: The retain flag of the received message
                         * - qos: The qos of the received message
                         * - dup: The duplicate flag of the received message
                         * 
                         * It is important not to do any heavy calculations, hardware access, delays or 
                         * blocking code in the callback function.
                        */
                        Serial.printf("Received Topic: %s\r\n", topic);
                        Serial.printf("Received Payload: %s\r\n", payload); });

    /**
     * Connect to the MQTT broker
     */
    mqttClientTuya.connect();

    /**
     * Wait blocking until the connection is established
     */
    while (!mqttClientTuya.connected())
    {
        delay(500);
    }

    /**
     * Publish a message to the topic "{MAC-Address}/simple" with QoS 0 and retain flag 0.
     *
     * You can only publish messages after the connection is established.
     */
    String topic2 = "tylink/26eb8c39a2c9ab5202iede/thing/property/report";
    mqttClientTuya.publish(topic2.c_str(), 0, 0, "{\"data\":{\"EAST\":111},\"time\":1741345692}");
    mqttClientTuya.publish(topic2.c_str(), 0, 0, "{\"data\":{\"Current Power\":333},\"time\":1741345692}");

    mqttClientTuya.disconnect();

}
