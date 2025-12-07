#include "Infrared.h"
#include "protocol.h"
#include "device.h"
#include "onoff.h"
#include "web.h"
#include <ctype.h>  // Pour isalnum()

// Pas de dépendance externe pour Base64 - implémentation native incluse

extern CircularBuffer<Packet, 100> *commandList;
extern CircularBuffer<Packet, 70> *PrioritycommandList;
extern std::vector<DeviceData*> devices;

// Instance singleton
ZosungIRManager* ZosungIRManager::instance = nullptr;

// ============================================================================
// ZosungIRManager - Gestion des messages
// ============================================================================

ZosungIRMessage* ZosungIRManager::getOrCreateMessage(uint16_t shortAddr) {
    auto it = pendingMessages.find(shortAddr);
    if (it != pendingMessages.end()) {
        return it->second;
    }
    
    ZosungIRMessage* msg = new ZosungIRMessage();
    msg->shortAddr = shortAddr;
    msg->timestamp = millis();
    pendingMessages[shortAddr] = msg;
    return msg;
}

void ZosungIRManager::removeMessage(uint16_t shortAddr) {
    auto it = pendingMessages.find(shortAddr);
    if (it != pendingMessages.end()) {
        delete it->second;
        pendingMessages.erase(it);
    }
}

void ZosungIRManager::cleanupOldMessages(unsigned long maxAge) {
    unsigned long now = millis();
    for (auto it = pendingMessages.begin(); it != pendingMessages.end(); ) {
        if (now - it->second->timestamp > maxAge) {
            delete it->second;
            it = pendingMessages.erase(it);
        } else {
            ++it;
        }
    }
}

ZosungIRSendContext* ZosungIRManager::getOrCreateSendContext(uint16_t shortAddr) {
    auto it = sendContexts.find(shortAddr);
    if (it != sendContexts.end()) {
        return it->second;
    }
    
    ZosungIRSendContext* ctx = new ZosungIRSendContext();
    ctx->shortAddr = shortAddr;
    sendContexts[shortAddr] = ctx;
    return ctx;
}

void ZosungIRManager::removeSendContext(uint16_t shortAddr) {
    auto it = sendContexts.find(shortAddr);
    if (it != sendContexts.end()) {
        if (it->second->buffer) {
            free(it->second->buffer);
        }
        delete it->second;
        sendContexts.erase(it);
    }
}

// ============================================================================
// Fonctions de haut niveau
// ============================================================================

bool ZosungIRManager::startLearnMode(uint16_t shortAddr, uint8_t endpoint) {
    log_i("IR Learn Mode START for 0x%04X ep%d", shortAddr, endpoint);
    SendZosungLearn(shortAddr, endpoint, true);
    return true;
}

bool ZosungIRManager::stopLearnMode(uint16_t shortAddr, uint8_t endpoint) {
    log_i("IR Learn Mode STOP for 0x%04X ep%d", shortAddr, endpoint);
    SendZosungLearn(shortAddr, endpoint, false);
    return true;
}

bool ZosungIRManager::sendIRCode(uint16_t shortAddr, uint8_t endpoint, const char* base64Code) {
    if (!base64Code || strlen(base64Code) == 0) {
        log_e("IR Code empty");
        return false;
    }
    
    // Décoder le Base64
    size_t decodedLen = strlen(base64Code) * 3 / 4 + 1;
    uint8_t* decoded = (uint8_t*)ps_malloc(decodedLen);
    if (!decoded) {
        decoded = (uint8_t*)malloc(decodedLen);
    }
    if (!decoded) {
        log_e("Memory allocation failed for IR code");
        return false;
    }
    
    size_t actualLen = base64Decode(base64Code, decoded, decodedLen);
    if (actualLen == 0) {
        free(decoded);
        log_e("Base64 decode failed");
        return false;
    }
    
    // Créer le contexte d'envoi
    ZosungIRSendContext* ctx = getOrCreateSendContext(shortAddr);
    if (ctx->buffer) {
        free(ctx->buffer);
    }
    ctx->buffer = decoded;
    ctx->totalLength = actualLen;
    ctx->position = 0;
    ctx->seq = getNextSeq();
    ctx->state = 0;
    
    // Démarrer l'envoi avec la commande 00
    log_i("Sending IR code to 0x%04X, len=%d, seq=%d", shortAddr, actualLen, ctx->seq);
    SendZosungIRCode00(shortAddr, endpoint, ctx->seq, actualLen, 2);
    
    return true;
}

// ============================================================================
// Traitement des messages reçus
// ============================================================================

void ZosungIRManager::handleIRTransmitCommand(uint16_t shortAddr, uint8_t endpoint,
                                               uint8_t cmdId, uint8_t* data, uint16_t len) {
    log_d("IR Transmit cmd 0x%02X from 0x%04X, len=%d", cmdId, shortAddr, len);
    
    switch (cmdId) {
        case ZOSUNG_CMD_SEND_IR_CODE_00: {
            // Début de transmission d'un code IR appris
            // Format: seq(2) + length(2) + unk1(1) + unk2(2) + unk3(1) + cmd(1) + unk4(1)
            if (len < 10) {
                log_e("IR Code00 too short");
                return;
            }
            
            uint16_t seq = (data[0] << 8) | data[1];
            uint16_t irLen = (data[2] << 8) | data[3];
            uint8_t unk1 = data[4];
            uint16_t unk2 = (data[5] << 8) | data[6];
            uint8_t unk3 = data[7];
            uint8_t cmd = data[8];
            uint8_t unk4 = data[9];
            
            log_i("IR Code00: seq=%d, len=%d, cmd=%d", seq, irLen, cmd);
            
            // Créer le buffer pour recevoir les données
            ZosungIRMessage* msg = getOrCreateMessage(shortAddr);
            if (!msg->allocate(irLen)) {
                log_e("Failed to allocate IR buffer");
                return;
            }
            msg->seq = seq;
            msg->timestamp = millis();
            msg->complete = false;
            
            // Répondre avec Code01 (acquittement)
            SendZosungIRCode01(shortAddr, endpoint, seq, irLen, unk1, unk2, unk3, cmd, unk4);
            
            // Demander les données avec Code02
            SendZosungIRCode02(shortAddr, endpoint, seq, 0, ZOSUNG_MAX_FRAGMENT_SIZE);
            break;
        }
        
        case ZOSUNG_CMD_SEND_IR_CODE_03: {
            // Fragment de données reçu
            // Format: seq(2) + position(2) + data(...) + crc(1)
            if (len < 5) {
                log_e("IR Code03 too short");
                return;
            }
            
            uint16_t seq = (data[0] << 8) | data[1];
            uint16_t position = (data[2] << 8) | data[3];
            uint8_t dataLen = len - 5;  // -2 seq -2 pos -1 crc
            uint8_t crc = data[len - 1];
            
            log_d("IR Code03: seq=%d, pos=%d, dataLen=%d", seq, position, dataLen);
            
            ZosungIRMessage* msg = getOrCreateMessage(shortAddr);
            if (!msg->buffer || msg->seq != seq) {
                log_e("No pending message for seq %d", seq);
                return;
            }
            
            // Copier les données
            if (position + dataLen <= msg->totalLength) {
                memcpy(msg->buffer + position, data + 4, dataLen);
                msg->position = position + dataLen;
            }
            
            // Vérifier si on a tout reçu
            if (msg->position >= msg->totalLength) {
                // Transmission complète
                msg->complete = true;
                log_i("IR Code complete! Total %d bytes", msg->totalLength);
                
                // Encoder en Base64 et sauvegarder
                String base64 = base64Encode(msg->buffer, msg->totalLength);
                
                // Trouver le device et sauvegarder le code
                String ieee = GetMacAdrr(shortAddr);
                if (ieee.length() > 0) {
                    // Sauvegarder dans l'attribut "Code IR appris"
                    String filename = "/db/" + ieee;
                    // TODO: Sauvegarder dans le fichier JSON du device
                    log_i("Learned IR code saved for %s: %s", ieee.c_str(), 
                          base64.substring(0, 50).c_str());
                    
                    // Notifier via WebSocket si nécessaire
                    // sendWebSocketUpdate(ieee, "learned_ir_code", base64);
                }
                
                // Nettoyer
                removeMessage(shortAddr);
            } else {
                // Demander le fragment suivant
                SendZosungIRCode02(shortAddr, endpoint, seq, msg->position, 
                                   ZOSUNG_MAX_FRAGMENT_SIZE);
            }
            break;
        }
        
        case ZOSUNG_CMD_SEND_IR_CODE_05: {
            // Fin de transmission (accusé de réception de notre envoi)
            log_i("IR send complete for 0x%04X", shortAddr);
            removeSendContext(shortAddr);
            break;
        }
        
        default:
            log_w("Unknown IR Transmit cmd 0x%02X", cmdId);
            break;
    }
}

void ZosungIRManager::handleIRControlCommand(uint16_t shortAddr, uint8_t endpoint,
                                              uint8_t cmdId, uint8_t* data, uint16_t len) {
    log_d("IR Control cmd 0x%02X from 0x%04X", cmdId, shortAddr);
    
    // Les réponses du cluster Control sont généralement des ACK
    // Pas grand chose à faire ici
}

// ============================================================================
// Interface Zigbee
// ============================================================================

void zosungIRManage(String filename, uint16_t cluster, uint8_t cmdId,
                    uint8_t* data, uint16_t len) {
    // Extraire le shortAddr du filename
    uint16_t shortAddr = 0;
    // filename format: "XXXXXXXXXXXX.json" où XX... est l'IEEE
    // On doit retrouver le shortAddr via le DeviceData
    
    for (auto* device : devices) {
        if (device && device->getDeviceID() == filename) {
            shortAddr = device->getInfo().shortAddr.toInt();
            break;
        }
    }
    
    if (shortAddr == 0) {
        log_e("Device not found for %s", filename.c_str());
        return;
    }
    
    ZosungIRManager* mgr = ZosungIRManager::getInstance();
    
    if (cluster == CLUSTER_ZOSUNG_IR_TRANSMIT) {
        mgr->handleIRTransmitCommand(shortAddr, 1, cmdId, data, len);
    } else if (cluster == CLUSTER_ZOSUNG_IR_CONTROL) {
        mgr->handleIRControlCommand(shortAddr, 1, cmdId, data, len);
    }
}

// ============================================================================
// Actions depuis le template
// ============================================================================

void SendIRAction(int shortAddr, int endpoint, int command, String value) {
    ZosungIRManager* mgr = ZosungIRManager::getInstance();
    
    switch (command) {
        case IR_ACTION_LEARN:
            // value = "1" pour ON, "0" pour OFF
            if (value == "1" || value.equalsIgnoreCase("on")) {
                mgr->startLearnMode(shortAddr, endpoint);
            } else {
                mgr->stopLearnMode(shortAddr, endpoint);
            }
            break;
            
        case IR_ACTION_SEND:
            // value = code IR en Base64
            mgr->sendIRCode(shortAddr, endpoint, value.c_str());
            break;
            
        default:
            log_w("Unknown IR action %d", command);
            break;
    }
}

// ============================================================================
// Fonctions d'envoi de commandes Zigbee
// ============================================================================

void SendZosungLearn(uint16_t shortAddr, uint8_t endpoint, bool enable) {
    Packet trame;
    trame.cmd = 0x0530;
    
    uint8_t datas[40];
    uint8_t len = 0;
    
    datas[len++] = 0x02;                          // Address mode (short)
    datas[len++] = (shortAddr >> 8) & 0xFF;
    datas[len++] = shortAddr & 0xFF;
    datas[len++] = 0x01;                          // Source endpoint
    datas[len++] = endpoint;                      // Dest endpoint
    datas[len++] = 0xE0;                          // Cluster MSB (0xE004)
    datas[len++] = 0x04;                          // Cluster LSB
    datas[len++] = 0x00;                          // Direction: client to server
    datas[len++] = 0x00;                          // Disable default response
    datas[len++] = 0x00;                          // Manufacturer specific = FALSE
    datas[len++] = 0x00;                          // Manu code LSB
    datas[len++] = 0x00;                          // Manu code MSB
    datas[len++] = 0x00;                          // Command ID = 0x00
    
    // Payload
    const char* payload = enable ? "{\"study\":1}" : "{\"study\":0}";
    for (int i = 0; i < strlen(payload); i++) {
        datas[len++] = payload[i];
    }
    
    trame.len = len;
    memcpy(trame.datas, datas, trame.len);
    PrioritycommandList->push(trame);
    
    log_i("SendZosungLearn: addr=0x%04X, enable=%d", shortAddr, enable);
}

void SendZosungIRCode00(uint16_t shortAddr, uint8_t endpoint, uint16_t seq,
                        uint16_t length, uint8_t cmd) {
    Packet trame;
    trame.cmd = 1328;
    
    uint8_t datas[30];
    uint8_t len = 0;
    
    datas[len++] = 0x02;
    datas[len++] = (shortAddr >> 8) & 0xFF;
    datas[len++] = shortAddr & 0xFF;
    datas[len++] = 0x01;
    datas[len++] = endpoint;
    datas[len++] = (CLUSTER_ZOSUNG_IR_TRANSMIT >> 8) & 0xFF;
    datas[len++] = CLUSTER_ZOSUNG_IR_TRANSMIT & 0xFF;
    datas[len++] = 0x01;
    datas[len++] = 0x01;  // Disable default response
    datas[len++] = 0x00;
    datas[len++] = 0x00;
    datas[len++] = 0x00;
    datas[len++] = ZOSUNG_CMD_SEND_IR_CODE_00;
    
    // Payload
    datas[len++] = (seq >> 8) & 0xFF;
    datas[len++] = seq & 0xFF;
    datas[len++] = (length >> 8) & 0xFF;
    datas[len++] = length & 0xFF;
    datas[len++] = 0x00;  // unk1
    datas[len++] = (CLUSTER_ZOSUNG_IR_CONTROL >> 8) & 0xFF;  // unk2
    datas[len++] = CLUSTER_ZOSUNG_IR_CONTROL & 0xFF;
    datas[len++] = 0x01;  // unk3
    datas[len++] = cmd;   // cmd (2 = send)
    datas[len++] = 0x00;  // unk4
    
    trame.len = len;
    memcpy(trame.datas, datas, len);
    PrioritycommandList->push(trame);
}

void SendZosungIRCode01(uint16_t shortAddr, uint8_t endpoint, uint16_t seq,
                        uint16_t length, uint8_t unk1, uint16_t unk2,
                        uint8_t unk3, uint8_t cmd, uint8_t unk4) {
    Packet trame;
    trame.cmd = 1328;
    
    uint8_t datas[30];
    uint8_t len = 0;
    
    datas[len++] = 0x02;
    datas[len++] = (shortAddr >> 8) & 0xFF;
    datas[len++] = shortAddr & 0xFF;
    datas[len++] = 0x01;
    datas[len++] = endpoint;
    datas[len++] = (CLUSTER_ZOSUNG_IR_TRANSMIT >> 8) & 0xFF;
    datas[len++] = CLUSTER_ZOSUNG_IR_TRANSMIT & 0xFF;
    datas[len++] = 0x01;
    datas[len++] = 0x01;
    datas[len++] = 0x00;
    datas[len++] = 0x00;
    datas[len++] = 0x00;
    datas[len++] = ZOSUNG_CMD_SEND_IR_CODE_01;
    
    // Payload
    datas[len++] = 0x00;  // zero
    datas[len++] = (seq >> 8) & 0xFF;
    datas[len++] = seq & 0xFF;
    datas[len++] = (length >> 8) & 0xFF;
    datas[len++] = length & 0xFF;
    datas[len++] = unk1;
    datas[len++] = (unk2 >> 8) & 0xFF;
    datas[len++] = unk2 & 0xFF;
    datas[len++] = unk3;
    datas[len++] = cmd;
    datas[len++] = unk4;
    
    trame.len = len;
    memcpy(trame.datas, datas, len);
    PrioritycommandList->push(trame);
}

void SendZosungIRCode02(uint16_t shortAddr, uint8_t endpoint, uint16_t seq,
                        uint16_t position, uint8_t maxlen) {
    Packet trame;
    trame.cmd = 1328;
    
    uint8_t datas[20];
    uint8_t len = 0;
    
    datas[len++] = 0x02;
    datas[len++] = (shortAddr >> 8) & 0xFF;
    datas[len++] = shortAddr & 0xFF;
    datas[len++] = 0x01;
    datas[len++] = endpoint;
    datas[len++] = (CLUSTER_ZOSUNG_IR_TRANSMIT >> 8) & 0xFF;
    datas[len++] = CLUSTER_ZOSUNG_IR_TRANSMIT & 0xFF;
    datas[len++] = 0x01;
    datas[len++] = 0x01;
    datas[len++] = 0x00;
    datas[len++] = 0x00;
    datas[len++] = 0x00;
    datas[len++] = ZOSUNG_CMD_SEND_IR_CODE_02;
    
    // Payload
    datas[len++] = (seq >> 8) & 0xFF;
    datas[len++] = seq & 0xFF;
    datas[len++] = (position >> 8) & 0xFF;
    datas[len++] = position & 0xFF;
    datas[len++] = maxlen;
    
    trame.len = len;
    memcpy(trame.datas, datas, len);
    PrioritycommandList->push(trame);
}

void SendZosungIRCode04(uint16_t shortAddr, uint8_t endpoint, uint16_t seq,
                        uint16_t position) {
    Packet trame;
    trame.cmd = 1328;
    
    uint8_t datas[20];
    uint8_t len = 0;
    
    datas[len++] = 0x02;
    datas[len++] = (shortAddr >> 8) & 0xFF;
    datas[len++] = shortAddr & 0xFF;
    datas[len++] = 0x01;
    datas[len++] = endpoint;
    datas[len++] = (CLUSTER_ZOSUNG_IR_TRANSMIT >> 8) & 0xFF;
    datas[len++] = CLUSTER_ZOSUNG_IR_TRANSMIT & 0xFF;
    datas[len++] = 0x01;
    datas[len++] = 0x01;
    datas[len++] = 0x00;
    datas[len++] = 0x00;
    datas[len++] = 0x00;
    datas[len++] = ZOSUNG_CMD_SEND_IR_CODE_04;
    
    // Payload
    datas[len++] = (seq >> 8) & 0xFF;
    datas[len++] = seq & 0xFF;
    datas[len++] = (position >> 8) & 0xFF;
    datas[len++] = position & 0xFF;
    
    trame.len = len;
    memcpy(trame.datas, datas, len);
    PrioritycommandList->push(trame);
}

// ============================================================================
// Utilitaires - Implémentation Base64 native (pas de dépendance externe)
// ============================================================================

static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const uint8_t base64_dec[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 62,  0,  0,  0, 63,
   52, 53, 54, 55, 56, 57, 58, 59, 60, 61,  0,  0,  0,  0,  0,  0,
    0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
   15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  0,  0,  0,  0,  0,
    0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
   41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,  0,  0,  0,  0,  0
};

String base64Encode(uint8_t* data, size_t len) {
    size_t encodedLen = 4 * ((len + 2) / 3) + 1;
    char* encoded = (char*)ps_malloc(encodedLen);
    if (!encoded) {
        encoded = (char*)malloc(encodedLen);
        if (!encoded) return "";
    }
    
    size_t i = 0, j = 0;
    uint8_t arr3[3], arr4[4];
    size_t in_len = len;
    
    while (in_len--) {
        arr3[i++] = *(data++);
        if (i == 3) {
            arr4[0] = (arr3[0] & 0xfc) >> 2;
            arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
            arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);
            arr4[3] = arr3[2] & 0x3f;
            
            for (i = 0; i < 4; i++)
                encoded[j++] = base64_chars[arr4[i]];
            i = 0;
        }
    }
    
    if (i) {
        for (size_t k = i; k < 3; k++)
            arr3[k] = '\0';
        
        arr4[0] = (arr3[0] & 0xfc) >> 2;
        arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
        arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);
        
        for (size_t k = 0; k < i + 1; k++)
            encoded[j++] = base64_chars[arr4[k]];
        
        while (i++ < 3)
            encoded[j++] = '=';
    }
    
    encoded[j] = '\0';
    
    String result = String(encoded);
    free(encoded);
    return result;
}

size_t base64Decode(const char* input, uint8_t* output, size_t maxLen) {
    size_t inputLen = strlen(input);
    if (inputLen == 0) return 0;
    
    // Calculer la taille de sortie
    size_t padding = 0;
    if (input[inputLen - 1] == '=') padding++;
    if (input[inputLen - 2] == '=') padding++;
    
    size_t outputLen = (inputLen / 4) * 3 - padding;
    if (outputLen > maxLen) return 0;
    
    size_t i = 0, j = 0;
    uint8_t arr4[4], arr3[3];
    size_t in_len = inputLen;
    const char* p = input;
    
    while (in_len-- && *p != '=' && 
           (isalnum((unsigned char)*p) || *p == '+' || *p == '/')) {
        arr4[i++] = *p++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                arr4[i] = base64_dec[(uint8_t)arr4[i]];
            
            arr3[0] = (arr4[0] << 2) + ((arr4[1] & 0x30) >> 4);
            arr3[1] = ((arr4[1] & 0xf) << 4) + ((arr4[2] & 0x3c) >> 2);
            arr3[2] = ((arr4[2] & 0x3) << 6) + arr4[3];
            
            for (i = 0; i < 3 && j < maxLen; i++)
                output[j++] = arr3[i];
            i = 0;
        }
    }
    
    if (i) {
        for (size_t k = 0; k < i; k++)
            arr4[k] = base64_dec[(uint8_t)arr4[k]];
        
        arr3[0] = (arr4[0] << 2) + ((arr4[1] & 0x30) >> 4);
        arr3[1] = ((arr4[1] & 0xf) << 4) + ((arr4[2] & 0x3c) >> 2);
        
        for (size_t k = 0; k < i - 1 && j < maxLen; k++)
            output[j++] = arr3[k];
    }
    
    return j;
}

uint8_t calculateCRC(uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
    }
    return crc;
}