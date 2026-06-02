#ifndef INFRARED_H
#define INFRARED_H

#include <Arduino.h>
#include <map>
#include "PsramAllocator.h"

// Clusters Zosung IR
#define CLUSTER_ZOSUNG_IR_CONTROL   0xE004  // 57348 - Contrôle apprentissage
#define CLUSTER_ZOSUNG_IR_TRANSMIT  0xED00  // 60672 - Transmission IR

// Commandes du cluster zosungIRTransmit (0xED00)
#define ZOSUNG_CMD_SEND_IR_CODE_00  0x00  // Début transmission (device -> hub)
#define ZOSUNG_CMD_SEND_IR_CODE_01  0x01  // Acquittement (hub -> device)
#define ZOSUNG_CMD_SEND_IR_CODE_02  0x02  // Demande données (hub -> device)
#define ZOSUNG_CMD_SEND_IR_CODE_03  0x03  // Fragment données (device -> hub)
#define ZOSUNG_CMD_SEND_IR_CODE_04  0x04  // Continuation (hub -> device)
#define ZOSUNG_CMD_SEND_IR_CODE_05  0x05  // Fin transmission

// Commandes du cluster zosungIRControl (0xE004)
#define ZOSUNG_CMD_LEARN_START      0x00  // Démarrer apprentissage

// Taille max d'un fragment
#define ZOSUNG_MAX_FRAGMENT_SIZE    56    // 0x38

// Actions pour le template
#define IR_ACTION_LEARN             200   // Activer/désactiver apprentissage
#define IR_ACTION_SEND              201   // Envoyer un code IR

// Structure pour stocker un message IR en cours de réception
struct ZosungIRMessage {
    uint16_t shortAddr;
    uint16_t seq;
    uint16_t totalLength;
    uint16_t position;
    uint8_t* buffer;
    unsigned long timestamp;
    bool complete;
    
    ZosungIRMessage() : shortAddr(0), seq(0), totalLength(0), position(0), 
                        buffer(nullptr), timestamp(0), complete(false) {}
    
    ~ZosungIRMessage() {
        if (buffer) {
            free(buffer);
            buffer = nullptr;
        }
    }
    
    bool allocate(uint16_t size) {
        if (buffer) free(buffer);
        buffer = (uint8_t*)ps_malloc(size);
        if (!buffer) {
            buffer = (uint8_t*)malloc(size);
        }
        totalLength = buffer ? size : 0;
        position = 0;
        return buffer != nullptr;
    }
};

// Structure pour envoyer un code IR
struct ZosungIRSendContext {
    uint16_t shortAddr;
    uint16_t seq;
    uint16_t totalLength;
    uint16_t position;
    uint8_t* buffer;
    uint8_t state;  // État de la machine d'état d'envoi
    
    ZosungIRSendContext() : shortAddr(0), seq(0), totalLength(0), position(0),
                           buffer(nullptr), state(0) {}
};

// Classe singleton pour gérer les communications IR
class ZosungIRManager {
private:
    static ZosungIRManager* instance;
    std::map<uint16_t, ZosungIRMessage*, std::less<uint16_t>, PsramAllocator<std::pair<const uint16_t, ZosungIRMessage*>>> pendingMessages;
    std::map<uint16_t, ZosungIRSendContext*, std::less<uint16_t>, PsramAllocator<std::pair<const uint16_t, ZosungIRSendContext*>>> sendContexts;
    uint16_t seqCounter;
    
    ZosungIRManager() : seqCounter(0) {}
    
public:
    static ZosungIRManager* getInstance() {
        if (!instance) {
            instance = new ZosungIRManager();
        }
        return instance;
    }
    
    // Gestion de la réception
    ZosungIRMessage* getOrCreateMessage(uint16_t shortAddr);
    void removeMessage(uint16_t shortAddr);
    void cleanupOldMessages(unsigned long maxAge = 30000);
    
    // Gestion de l'envoi
    uint16_t getNextSeq() { return ++seqCounter; }
    ZosungIRSendContext* getOrCreateSendContext(uint16_t shortAddr);
    void removeSendContext(uint16_t shortAddr);
    
    // Fonctions de haut niveau
    bool startLearnMode(uint16_t shortAddr, uint8_t endpoint);
    bool stopLearnMode(uint16_t shortAddr, uint8_t endpoint);
    bool sendIRCode(uint16_t shortAddr, uint8_t endpoint, const char* base64Code);
    
    // Traitement des messages reçus
    void handleIRTransmitCommand(uint16_t shortAddr, uint8_t endpoint, 
                                  uint8_t cmdId, uint8_t* data, uint16_t len);
    void handleIRControlCommand(uint16_t shortAddr, uint8_t endpoint,
                                 uint8_t cmdId, uint8_t* data, uint16_t len);
};

// Fonctions d'interface Zigbee
void zosungIRManage(String filename, uint16_t cluster, uint8_t cmdId, 
                    uint8_t* data, uint16_t len);

// Actions depuis le template
void SendIRAction(int shortAddr, int endpoint, int command, String value);

// Fonctions d'envoi de commandes Zigbee
void SendZosungIRCode00(uint16_t shortAddr, uint8_t endpoint, uint16_t seq, 
                        uint16_t length, uint8_t cmd);
void SendZosungIRCode01(uint16_t shortAddr, uint8_t endpoint, uint16_t seq,
                        uint16_t length, uint8_t unk1, uint16_t unk2, 
                        uint8_t unk3, uint8_t cmd, uint8_t unk4);
void SendZosungIRCode02(uint16_t shortAddr, uint8_t endpoint, uint16_t seq,
                        uint16_t position, uint8_t maxlen);
void SendZosungIRCode03(uint16_t shortAddr, uint8_t endpoint, uint16_t seq,
                        uint16_t position, uint8_t* data, uint8_t len, uint8_t crc);
void SendZosungIRCode04(uint16_t shortAddr, uint8_t endpoint, uint16_t seq,
                        uint16_t position);
void SendZosungLearn(uint16_t shortAddr, uint8_t endpoint, bool enable);

// Utilitaires
String base64Encode(uint8_t* data, size_t len);
size_t base64Decode(const char* input, uint8_t* output, size_t maxLen);
uint8_t calculateCRC(uint8_t* data, size_t len);

#endif // INFRARED_H