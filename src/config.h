#pragma once

#ifndef CONFIG_H_
#define CONFIG_H_

#include <Arduino.h>
//#define CIRCULAR_BUFFER_INT_SAFE
#include <CircularBuffer.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/* File circulaire PARTAGEE entre taches (issue #46).
 * deviceList, notifList et alertList sont remplies par la boucle principale (a chaque trame
 * recue) et videes par les handlers web, qui tournent dans la tache async_tcp. CircularBuffer
 * n'est pas prevu pour cela : push() et shift() modifient le meme compteur et les memes cases,
 * dont les elements contiennent des String. Deux taches qui s'y croisent -- typiquement une page
 * ouverte dans le navigateur pendant que des trames arrivent -- corrompent le compteur ou le
 * heap. Chaque operation est donc protegee par un mutex ; les appels existants ne changent pas.
 */
template <typename T, size_t S>
class LockedBuffer : public CircularBuffer<T, S> {
    SemaphoreHandle_t m_;
    struct Guard {
        SemaphoreHandle_t m;
        explicit Guard(SemaphoreHandle_t x) : m(x) { if (m) xSemaphoreTake(m, portMAX_DELAY); }
        ~Guard() { if (m) xSemaphoreGive(m); }
    };
public:
    LockedBuffer() : m_(xSemaphoreCreateMutex()) {}
    bool push(T value) { Guard g(m_); return CircularBuffer<T, S>::push(value); }
    T    shift()       { Guard g(m_); return CircularBuffer<T, S>::shift(); }
    bool isEmpty()     { Guard g(m_); return CircularBuffer<T, S>::isEmpty(); }
    bool isFull()      { Guard g(m_); return CircularBuffer<T, S>::isFull(); }
    void clear()       { Guard g(m_); CircularBuffer<T, S>::clear(); }
    // Retire un element s'il y en a un : test et retrait en une seule operation, pour les
    // boucles de vidage ("tant que non vide, retirer") executees par une autre tache.
    bool take(T &out) {
        Guard g(m_);
        if (CircularBuffer<T, S>::isEmpty()) return false;
        out = CircularBuffer<T, S>::shift();
        return true;
    }
};
#include <ArduinoJson.h>
#include <malloc.h>

#define VERSION "v2.26"

// hardware config64
#define RESET_ZIGATE 40//4
#define FLASH_ZIGATE 39//33
#define LED_PIN 3
#define PRODUCTION 1
#define FLASH 0

#define VOLTAGE 4

#define RXD2 17//17//17 
#define TXD2 18//18//16 

#define MAXHEAP 1000000//ESP.getMaxAllocHeap() //(ESP.getFreeHeap() / 2) //96000

/* ===================== Entiers signes ZCL (issue #34) =====================
 * Les valeurs d'attributs sont stockees en HEXA. Jusqu'ici elles etaient toujours relues avec
 * strtol(..., 16), donc interpretees comme NON SIGNEES : un int16 valant 0xFFE4 (-28 W sur un
 * NodOn SEM-4) s'affichait 65508. Or le type ZCL recu dit si l'attribut est signe.
 *
 * Principe retenu : a la reception, si le type ZCL est signe ET la valeur negative, on etend le
 * signe sur 32 bits AVANT de stocker (0xFFE4 -> "FFFFFFE4"). La valeur stockee devient ainsi
 * auto-descriptive, sans avoir a memoriser le type ailleurs. Les lecteurs utilisent
 * zclHexToSigned(), qui interprete le complement a deux 32 bits.
 * (strtol saturerait a LONG_MAX sur "FFFFFFE4" : on passe par strtoul puis cast.)
 */
// Types entiers ZCL signes : 0x28 (int8) .. 0x2F (int64). Non signes : 0x20 (uint8) .. 0x27.
static inline bool zclTypeIsSigned(uint8_t t) { return (t >= 0x28 && t <= 0x2F); }

// Relit une valeur hexa d'attribut en entier signe 32 bits (complement a deux).
static inline long zclHexToSigned(const char *hex) {
    return (long)(int32_t)strtoul(hex, nullptr, 16);
}
/* --- Attribut propre a un endpoint (champ "endpoint" d'une ligne "status" de template) -------
 * Les valeurs sont rangees par (cluster, attribut), sans l'endpoint. Un appareil a plusieurs
 * relais (Aqara T2 : L1 sur l'endpoint 1, L2 sur l'endpoint 2) remonte DEUX fois 0006/0 : la
 * seconde valeur ecrasait la premiere (issue #44).
 *
 * Dans le template, une ligne peut preciser son endpoint :
 *     { "name": "On/Off L2", "cluster": "0006", "attribut": 0, "endpoint": 2 }
 *   - champ ABSENT (ou 0 / 1) : comportement historique, la valeur est acceptee quel que soit
 *     l'endpoint qui l'envoie. Indispensable : beaucoup d'appareils n'ont pas leurs donnees sur
 *     l'endpoint 1 (11, 8, 21...), leurs templates continuent de fonctionner tels quels.
 *   - champ >= 2 : la ligne ne recoit que les valeurs de CET endpoint.
 *
 * En interne, une telle ligne porte une cle composee : attribut | (endpoint << 16). Un
 * identifiant d'attribut ZCL tient sur 16 bits : aucune collision possible, quel que soit le
 * cluster. Tout le reste (stockage, MQTT, regles, affichage) manipule cette cle comme un
 * attribut ordinaire, sans rien savoir des endpoints.
 */
static inline uint32_t epAttrKey(uint32_t attribut, uint32_t endpoint) {
    return (endpoint >= 2 && endpoint <= 240) ? ((attribut & 0xFFFF) | (endpoint << 16)) : attribut;
}
static inline uint8_t  epAttrEndpoint(uint32_t key) { return (uint8_t)(key >> 16); }   // 0 = aucun
static inline uint16_t epAttrId(uint32_t key)       { return (uint16_t)(key & 0xFFFF); }
// Nom lisible de la cle, pour ce qui sort de la box (sujets et champs MQTT) : "0" pour une
// ligne sans endpoint, "0_ep2" pour l'attribut 0 de l'endpoint 2 -- plutot que 131072.
static inline String epAttrLabel(uint32_t key) {
    if (!epAttrEndpoint(key)) return String(key);
    return String(epAttrId(key)) + "_ep" + String(epAttrEndpoint(key));
}

// Détection des modules présents (le Zigbee et le LoRa peuvent coexister).
// zigbeeDetected passe à true dès que la ZiGate répond au Get Version (trame 0x8010).
extern bool zigbeeDetected;

extern char FormattedDate[];
extern char Hour[];
extern char Day[];
extern char Month[];
extern char Year[];
extern char Minute[];
extern char Yesterday[];


#define MAX_SUBMETERS 10

struct SubMeterConfig {
    char IEEE[20];          // Adresse IEEE du device (ex: "00158D0001234567")
    char alias[32];         // Nom affiché (ex: "Salon", "Cuisine")
    char color[10];         // Couleur hex pour le donut (ex: "#e74c3c")
    bool enabled;           // Activé/désactivé
};

// ===== Thermostat virtuel (Phase 0) =====
// Une zone = un capteur de température + un actionneur on/off, régulés en TPI.
#define MAX_VTHERMOSTATS 8
#define MAX_OPEN_SENSORS 4      // capteurs d'ouverture par zone
#define MAX_EXTRA_ACTUATORS 4   // prises/relais supplémentaires par zone (en plus du principal)

struct VirtualThermostat {
    // --- configuration (persistée dans /config/thermostats.json) ---
    char  name[32];          // Nom affiché (ex: "Salon")
    char  sensorIEEE[20];    // Capteur de température (cluster 0402)
    char  actuatorIEEE[20];  // Prise / relais / appareil piloté (principal : porte les actions ci-dessous)
    char  actuatorsExtra[MAX_EXTRA_ACTUATORS][20];  // Prises supplémentaires (même commande que le principal)
    int   actuatorsExtraCount;
    char  actionHeat[40];    // Action "chauffer" (vide => on/off cluster 0006)
    char  actionCool[40];    // Action "refroidir" (clim réversible ; vide si non géré)
    char  actionOff[40];     // Action "arrêt" (vide => on/off cluster 0006)
    char  presenceIEEE[20];  // Capteur de présence (optionnel, cluster 0406)
    char  openSensors[MAX_OPEN_SENSORS][20];  // Capteurs d'ouverture (optionnels, IAS 0500)
    int   openSensorCount;
    bool  enabled;           // Zone active
    bool  heating;           // true = chauffage, false = rafraîchissement
    float setpoint;          // Consigne °C (réglage manuel en P0)
    float frostTemp;         // Sécurité hors-gel °C
    bool  frostMode;         // Mode hors-gel actif (consigne basculée sur frostTemp)
    float setpointSaved;     // Consigne mémorisée avant passage en hors-gel
    int   tpiCycleSec;       // Durée du cycle TPI (s)
    float tpiKp;             // Gain proportionnel
    float tpiKi;             // Gain intégral
    int   minOnSec;          // Anti-court-cycle : durée ON minimale
    int   minOffSec;         // Anti-court-cycle : durée OFF minimale
    int   sensorTimeoutSec;  // Délai sans nouvelle mesure avant capteur "HS"
    int   operMode;          // Fonctionnement : 0=toujours, 1=plages horaires, 2=tarif Linky
    char  schedule[64];      // Plages horaires "HH:MM-HH:MM,HH:MM-HH:MM" (operMode=1)
    char  tariffPeriods[48]; // Périodes tarifaires actives, IDs séparés par virgule (operMode=2), ex "256,260,264"
    int   priority;          // Priorité (futur délestage)
    int   nominalPowerW;     // Puissance nominale W (futur coût/délestage, 0 = inconnu)

    // --- état runtime (NON persisté) ---
    float         currentTemp;   // Dernière température lue (°C)
    bool          sensorValid;   // Capteur joignable et valeur fraîche
    uint8_t       sensorReason;  // Pourquoi il est HS : 0=OK, 1=introuvable, 2=pas de température,
                                 // 3=trop ancien (cf. refreshZoneSensors)
    long          sensorAgeSec;  // Âge de la dernière trame du capteur (s), -1 = inconnu
    bool          occupied;      // Présence détectée (true si pas de capteur présence)
    bool          windowOpen;    // Au moins un capteur d'ouverture ouvert
    bool          schedActive;   // Dans la plage horaire / tarif favorable (true = autorisé)
    int           forceMode;     // Forçage manuel runtime : 0=auto, 1=marche forcée, 2=arrêt forcé
    float         duty;          // Sortie TPI calculée (0..1)
    bool          output;        // État commandé/visé de l'actionneur
    bool          actualOn;      // État réel lu sur la prise (0006/0)
    float         integral;      // Terme intégral TPI
    unsigned long cycleStartMs;  // Début du cycle TPI courant
    unsigned long lastSwitchMs;  // Dernier basculement de l'actionneur
    unsigned long lastSensorMs;  // Dernière lecture capteur valide
    unsigned long lastAssertMs;  // Dernière ré-assertion de sécurité
};

extern VirtualThermostat vThermostats[MAX_VTHERMOSTATS];
extern int vThermostatCount;

// ma structure configCRC error
struct ConfigSettingsStruct {
  bool enableWiFi;
  bool enableDHCP;
  bool connectedWifiSta;
  int channelWifi;
  int RSSIWifi;
  char ssid[50];
  String bssid;
  char password[150];
  char ipAddressWiFi[18];
  char ipMaskWiFi[16];
  char ipGWWiFi[18];
  bool dhcp;
  char ipAddress[18];
  char ipMask[16];
  char ipGW[18];
  int serialSpeed;
  char radioType[20];
  char dataFlow[20];
  int  tcpListenPort;
  bool disableWeb;
  bool enableHeartBeat;
  double refreshLogs;
  bool enableDebug;
  bool enableNotif;
  bool enableWebPush;
  bool enableSecureHttp;
  bool enableMqtt;
  bool enableUDP;
  bool enableHistory;
  bool enableHWFlow;
  
};

struct ZigbeeConfig {
  char application[9];
  char type;
  char sdk;
  int channel;
  uint8_t network;
  uint64_t zigbeeMac;
};

struct ZiGateInfosStruct {
  char device[8];
  char mac[8];
  char flash[8];  
};

struct ConfigNotification{
  bool PriceChange;
  bool PEJP;
  bool ColorTomorrow;
  bool RedColor;
  bool OverVoltage;
  int  OverVoltageThreshold;
  bool UnderVoltage;
  int  UnderVoltageThreshold;
  bool OverBudget;
  int  OverBudgetThreshold;
  bool SubscribedPower;
  bool PowerOutage;
  bool ProdSupConso;
  bool ProdZero;

  // Alertes avancées
  bool OverPower;
  int  OverPowerThreshold;       // Watts
  int  OverPowerDuration;        // Secondes avant alerte
  int  OverPowerCooldown;        // Minutes entre alertes

  bool Freeze;
  int  FreezeThreshold;          // Centièmes °C (300 = 3.00°C)
  char FreezeSensorIEEE[20];
  int  FreezeCooldown;           // Minutes entre alertes

  bool DailyAnomaly;
  int  DailyAnomalyPercent;      // % au-dessus moyenne 7j

  bool WaterLeak;
  int  WaterLeakThreshold;       // Litres
  bool NightWaterLeak;
  int  NightWaterLeakThreshold;  // Litres (conso nocturne 00h-05h)

  bool DailyMetrics;
};

struct ConfigGeneralStruct {
  int firstStart;
  char ZLinky[20];
  char Production[20];
  char Gaz[20];
  char Water[20];
  int LinkyMode;
  int HouseSurface;
  int powerMaxDatas;
  char ntpserver[50];
  int timeoffset;
  long epochTime;
  char timezone[50];
  char tarifIdx1[10];
  char tarifIdx2[10];
  char tarifIdx3[10];
  char tarifIdx4[10];
  char tarifIdx5[10];
  char tarifIdx6[10];
  char tarifIdx7[10];
  char tarifIdx8[10];
  char tarifIdx9[10];
  char tarifIdx10[10];
  char tarifAbo[10];
  char tarifCSPE[10];
  char tarifCTA[10];
  char tarifAboProd[10];
  char tarifIdxProd[10];
  char servSMTP[50];
  char portSMTP[50];
  char userSMTP[50];
  char passSMTP[50];
  char servMQTT[50];
  char portMQTT[50];
  char clientIDMQTT[50];
  char userMQTT[50];
  char passMQTT[128];
  char headerMQTT[128];
  bool HAMQTT;
  bool TBMQTT;
  bool customMQTT;
  String customMQTTJson;
  char userHTTP[50];
  char passHTTP[50];
  char servWebPush[50];
  char userWebPush[50];
  char passWebPush[50];
  bool webPushAuth;
  char servUDP[50];
  char portUDP[50];
  String customUDPJson;
  float coeffGaz;
  float coeffWater;
  float coeffProduction;
  char unitGaz[8];     // "kWh" etait tronque a 2 caracteres avec [3]
  char unitWater[8];
  char tarifGaz[10];
  char tarifWater[10];
  int scanNumber;
  bool developerMode;
  SubMeterConfig subMeters[MAX_SUBMETERS];
  int subMeterCount;  
  char Presence[20] = "";          // IEEE du capteur de présence
  bool enablePresenceGraph = true; // Afficher sur le graphique

  // Délestage (v2.26)
  String delestage;                 // IEEE des prises délestées, séparés par des virgules
  int  delestageRestoreMin = 5;     // rallumage après N minutes sous le seuil (0 = jamais)
  int  delestageRestorePct = 80;    // seuil de rallumage, en % de la puissance souscrite
  int  delestageAnticipPct = 0;     // délestage anticipé : moyenne 1 min >= N % (0 = non)

  // Tunnel (reverse proxy)
  bool enableTunnel = false;
  char tunnelToken[128] = "";
  char tunnelClientId[64] = "";
};

struct SerialBuffer {
    uint8_t data[512];
    size_t length;
};

typedef struct __attribute__((packed)) {
    uint16_t cmd;           // uint16_t au lieu de unsigned int (32 bits -> 16 bits)
    uint16_t len;           // uint16_t au lieu de unsigned int
    uint8_t datas[512];
} Packet;


typedef struct {
  String section;
  String key;
  String value;
} iniPacket;

typedef struct {
  int iniPacketSize;
  iniPacket i[10];
}WriteIni;


/*typedef struct {
    char name[50];
    unsigned int cluster;
    unsigned int attribute;
    char mode[20];
    char mqtt_device_class[20];
    char mqtt_state_class[20];
    char mqtt_icon[20];
    char type[20];
    float coefficient;
    char unit[20];
    bool visible;
    char typeJauge[20];
    int jaugeMin;
    int jaugeMax;
    
} State;

typedef struct {
    char name[50];
    unsigned int command;
    unsigned int endpoint;
    unsigned int value;
    bool visible;
} Action;

typedef struct {
  int StateSize;
  int ActionSize;
  State e[100];
  Action a[10];
}Template;*/

typedef struct {
  String message;
  int state;
} Alert;

typedef struct {
  int shortAddr;
  int cluster;
  int attribute;
  String value;
} Device;

// Entree "etat radio" de la file deviceList (mise a jour en direct de la page Appareils).
// Couple cluster/attribut volontairement hors de toute plage ZCL : il ne peut pas entrer en
// collision avec un vrai attribut, et getDeviceValue le reconnait pour rendre l'icone.
#define RADIO_STATUS_CLUSTER 0xFFFF
#define RADIO_STATUS_ATTR    0xFFFF

typedef struct {
  String title;
  String message;
  String timeStamp;
  int type;
  int viewed;
} Notification;

typedef struct{
  String manufacturer;
  String model;
  int shortAddr;
  int deviceId;
  String sotfwareVersion;
  String lastSeen;
  String LQI;

} DeviceInfo;

struct StatusRegisterBreakout {
  String contact_sec;
  String organe_coupure;
  String cache_borne_dist;
  uint8_t surtension_phase;
  uint8_t depassement_ref_pow;
  String producteur;
  String sens_energie_active;
  String tarif_four;
  String tarif_dist;
  String horloge;
  String type_tic;
  String comm_euridis;
  String etat_cpl;
  String sync_cpl;
  String tempo_jour;
  String tempo_demain;
  String preavis_pointe_mobile;
  String pointe_mobile;
};

StatusRegisterBreakout parseStatusRegister(const String& hexVal);
String getStatusRegisterField(const String& hexVal, const String& field);       // issue #31 : libelle d'un champ STGE (affichage)
long   getStatusRegisterFieldValue(const String& hexVal, const String& field);  // issue #31 : valeur numerique du champ (comparaison regles)

//typedef CircularBuffer<char, 4096> LogConsoleType;

struct SpiRamAllocator {
  void* allocate(size_t size) {
    return ps_malloc(size);  // Allouer dans la PSRAM
  }

  void deallocate(void* pointer) {
    free(pointer);  // Libérer l'allocation
  }

  void* reallocate(void* pointer, size_t new_size) {
    // heap_caps_realloc connaît la taille de l'ancien bloc en interne
    void* new_ptr = heap_caps_realloc(pointer, new_size, MALLOC_CAP_SPIRAM);
    if (!new_ptr) new_ptr = realloc(pointer, new_size);
    return new_ptr;
  }
};

struct UpdateStatus {
      String statusManuel = "";
      String statusAuto = "";
      int progressAuto = -1;  // -1 = pas de MAJ, 0-100 = progression
      int progressManuel = -1;
      bool rebootRequested = false;
      // String log = "";         // Log de diagnostic mise a jour (desactive temporairement)
    };

// Définir un type de document JSON qui utilise la PSRAM
using SpiRamJsonDocument = BasicJsonDocument<SpiRamAllocator>;

#define DEBUG_ON

#ifdef DEBUG_ON
#define DEBUG_PRINT(x) Serial.print(x); 
#define DEBUG_PRINTLN(x) Serial.println(x);
#else
#define DEBUG_PRINT(x) 
#define DEBUG_PRINTLN(x) 
#endif
#endif

String encodeBase64(const String &input);

// ===== CLASSE STRING PSRAM SIMPLE =====
class PSRAMString {
private:
    char* buffer;
    size_t capacity;
    size_t len;
    
public:
    PSRAMString(size_t size = 100000) {
        capacity = size;
        buffer = (char*)ps_malloc(capacity);
        if (!buffer) {
            Serial.println("Erreur allocation PSRAM, utilisation heap");
            buffer = (char*)malloc(capacity);
        }
        clear();
    }
    
    ~PSRAMString() {
        if (buffer) free(buffer);
    }
    
    // Opérateur += comme String normale
    PSRAMString& operator+=(const String& str) {
        append(str.c_str());
        return *this;
    }
    
    PSRAMString& operator+=(const char* str) {
        append(str);
        return *this;
    }
    
    PSRAMString& operator+=(const __FlashStringHelper* str) {
        append(reinterpret_cast<const char*>(str));
        return *this;
    }
    
    // Opérateur = pour affecter
    PSRAMString& operator=(const String& str) {
        clear();
        append(str.c_str());
        return *this;
    }
    
    PSRAMString& operator=(const __FlashStringHelper* str) {
        clear();
        append(reinterpret_cast<const char*>(str));
        return *this;
    }
    
    // Fonction replace native — travaille directement sur le buffer PSRAM
    // sans copie intermédiaire vers un String heap
    void replace(const String& find, const String& replaceWith) {
        const char* f = find.c_str();
        const char* r = replaceWith.c_str();
        size_t fLen = find.length();
        size_t rLen = replaceWith.length();
        if (fLen == 0) return;

        // Comptage des occurrences pour calculer la taille finale
        size_t count = 0;
        const char* pos = buffer;
        while ((pos = strstr(pos, f)) != nullptr) { count++; pos += fLen; }
        if (count == 0) return;

        // Calcul de la nouvelle taille (cast signé pour éviter underflow si rLen < fLen)
        size_t newLen = (size_t)((ssize_t)len + (ssize_t)count * ((ssize_t)rLen - (ssize_t)fLen));

        // Réécriture dans un buffer temporaire PSRAM de la taille exacte
        char* tmp = (char*)ps_malloc(newLen + 1);
        if (!tmp) tmp = (char*)malloc(newLen + 1);
        if (!tmp) return;

        char* dst = tmp;
        const char* src = buffer;
        const char* found;
        while ((found = strstr(src, f)) != nullptr) {
            size_t chunk = found - src;
            memcpy(dst, src, chunk);
            dst += chunk;
            memcpy(dst, r, rLen);
            dst += rLen;
            src = found + fLen;
        }
        // Copier le reste
        size_t tail = len - (src - buffer);
        memcpy(dst, src, tail);
        dst[tail] = '\0';

        // Si le résultat est plus grand que le buffer actuel, on réalloue en PSRAM
        if (newLen + 1 > capacity) {
            size_t newCap = (newLen + 1) * 2;
            char* newBuf = (char*)ps_malloc(newCap);
            if (!newBuf) newBuf = (char*)malloc(newCap);
            if (!newBuf) { free(tmp); return; }
            free(buffer);
            buffer = newBuf;
            capacity = newCap;
        }

        memcpy(buffer, tmp, newLen + 1);
        len = newLen;
        free(tmp);
    }
    
    // Fonctions utiles
    const char* c_str() const { return buffer; }
    size_t length() const { return len; }
    void clear() { buffer[0] = '\0'; len = 0; }
    
private:
    void append(const char* str) {
        if (!str) return;
        size_t strLen = strlen(str);
        if (len + strLen >= capacity) {
            size_t newCap = (len + strLen + 1) * 2;
            char* newBuf = (char*)ps_malloc(newCap);
            if (!newBuf) newBuf = (char*)malloc(newCap);
            if (!newBuf) return;
            memcpy(newBuf, buffer, len);
            free(buffer);
            buffer = newBuf;
            capacity = newCap;
        }
        strlcpy(buffer + len, str, capacity - len);
        len += strLen;
    }
};

