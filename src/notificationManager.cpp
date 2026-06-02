#include "NotificationManager.h"
#include "SPIFFS_ini.h"
#include <time.h>

NotificationManager::NotificationManager(const char* filePath, size_t maxSize) 
  : jsonFilePath(filePath), maxNotifications(maxSize) {
  
  // Réserver la capacité du vecteur (en mémoire standard)
  notifications.reserve(maxNotifications);
}

NotificationManager::~NotificationManager() {
  // Libérer toutes les notifications en PSRAM
  clearAll();
}

bool NotificationManager::begin() {
  if (!LittleFS.begin()) {
    log_e("Erreur: initialisation LittleFS échouée");
    return false;
  }
  
  // Charger les notifications existantes
  loadFromFile();
  
  log_i("NotificationManager initialisé: %d notifications chargées\n", getCount());
  return true;
}

bool NotificationManager::addNotification(const String& title, const String& message, int type,
                                          const char* alertType, float value, float threshold) {
  if (notifications.size() >= maxNotifications) {
    cleanupOldNotifications();
  }
  
  // Allocation de la notification en PSRAM
  Notification* notif = (Notification*)allocatePSRAM(sizeof(Notification));
  if (!notif) {
    log_e("Erreur: allocation PSRAM échouée pour notification");
    return false;
  }
  
  // Construction in-place
  new (notif) Notification();
  notif->title = title;
  notif->message = message;
  notif->timeStamp = FormattedDate;
  notif->type = type;
  notif->viewed = 0;
  
  notifications.push_back(notif);
  
  // Sauvegarde automatique
  saveToFile();

  // Push mobile via tunnel (si callback configuré)
  if (_pushCallback) {
      _pushCallback(title, message, type, alertType, value, threshold);
  }

  log_i("Notification ajoutée: %s\n", title.c_str());
  return true;
}

bool NotificationManager::addNotification(const Notification& notification) {
  return addNotification(notification.title, notification.message, notification.type);
}

size_t NotificationManager::getCount() const {
  return notifications.size();
}

Notification* NotificationManager::getNotification(size_t index) const {
  if (index >= notifications.size()) {
    return nullptr;
  }
  return notifications[index];
}

std::vector<Notification*> NotificationManager::getNotifications(size_t offset, size_t limit) const {
  std::vector<Notification*> result;
  
  size_t total = notifications.size();
  if (total == 0 || offset >= total) {
    return result;
  }
  
  // Parcours en ordre inverse (plus récentes en premier)
  size_t startReverseIndex = total - 1 - offset;
  size_t count = 0;
  
  for (int i = (int)startReverseIndex; i >= 0 && count < limit; i--, count++) {
    result.push_back(notifications[i]);
  }
  
  return result;
}

bool NotificationManager::markAsViewed(size_t index) {
  if (index >= notifications.size()) {
    return false;
  }
  
  notifications[index]->viewed = 1;
  saveToFile();
  return true;
}

bool NotificationManager::markAllAsViewed() {
  if (notifications.empty()) {
    return true; // Pas d'erreur si pas de notifications
  }
  
  int markedCount = 0;
  for (Notification* notif : notifications) {
    if (notif && notif->viewed == 0) {
      notif->viewed = 1;
      markedCount++;
    }
  }
  
  if (markedCount > 0) {
    saveToFile();
    Serial.printf("Marquées comme lues: %d notifications\n", markedCount);
  }
  
  return true;
}

bool NotificationManager::deleteNotification(size_t index) {
  if (index >= notifications.size()) {
    return false;
  }
  
  Notification* notif = notifications[index];
  notif->~Notification();
  freePSRAM(notif);
  
  notifications.erase(notifications.begin() + index);
  saveToFile();
  
  return true;
}

void NotificationManager::clearAll() {
  for (Notification* notif : notifications) {
    if (notif) {
      notif->~Notification();
      freePSRAM(notif);
    }
  }
  notifications.clear();
}

bool NotificationManager::saveToFile() {
  // ~256 bytes par notification (titre + message + timestamp + overhead JSON)
  size_t docSize = 512 + notifications.size() * 256;
  if (docSize < 8192) docSize = 8192;
  SpiRamJsonDocument doc(docSize);
  JsonArray array = doc.createNestedArray("notifications");
  
  for (const Notification* notif : notifications) {
    if (notif) {
      JsonObject obj = array.createNestedObject();
      obj["title"] = notif->title;
      obj["message"] = notif->message;
      obj["timeStamp"] = notif->timeStamp;
      obj["type"] = notif->type;
      obj["viewed"] = notif->viewed;
    }
  }
  
  bool ok = atomicWriteJson(jsonFilePath, doc);
  if (ok) Serial.println("Notifications sauvegardées");
  return ok;
}

bool NotificationManager::loadFromFile() {
  File file = LittleFS.open(jsonFilePath, "r");
  if (!file) {
    return true; // Pas d'erreur, juste pas de fichier existant
  }
  
  // Dimensionner le document selon la taille du fichier
  size_t fileSize = file.size();
  size_t docSize = fileSize + fileSize / 2 + 512; // fichier + 50% marge + overhead
  if (docSize < 8192) docSize = 8192;
  SpiRamJsonDocument doc(docSize);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    log_e("Erreur parsing JSON: %s\n", error.c_str());
    return false;
  }
  
  clearAll();
  
  JsonArray array = doc["notifications"];
  for (JsonObject obj : array) {
    Notification* notif = (Notification*)allocatePSRAM(sizeof(Notification));
    if (notif) {
      new (notif) Notification();
      notif->title = obj["title"].as<String>();
      notif->message = obj["message"].as<String>();
      notif->timeStamp = obj["timeStamp"].as<String>();
      notif->type = obj["type"] | 0;
      notif->viewed = obj["viewed"] | 0;
      
      notifications.push_back(notif);
    }
  }
  
  return true;
}

String NotificationManager::toJson(size_t offset, size_t limit) const {
  // ~300 bytes par notification serialisée + overhead
  size_t docSize = 512 + limit * 300;
  if (docSize < 4096) docSize = 4096;
  SpiRamJsonDocument doc(docSize);
  
  size_t total = getCount();
  doc["total"] = total;
  doc["offset"] = offset;
  doc["limit"] = limit;
  
  JsonArray array = doc.createNestedArray("notifications");
  
  if (total == 0) {
    String result;
    serializeJson(doc, result);
    return result;
  }
  
  // Calcul pour l'ordre inverse (plus récentes en premier)
  // Si total=10, offset=0, limit=3 -> on veut les index 9,8,7
  // Si total=10, offset=3, limit=3 -> on veut les index 6,5,4
  
  if (offset >= total) {
    String result;
    serializeJson(doc, result);
    return result;
  }
  
  size_t startReverseIndex = total - 1 - offset; // Index de départ en ordre inverse
  size_t count = 0;
   
  // Parcourir en ordre inverse
  for (int i = (int)startReverseIndex; i >= 0 && count < limit; i--, count++) {
    const Notification* notif = notifications[i];
    if (notif) {
      JsonObject obj = array.createNestedObject();
      obj["id"] = i; // Index réel dans le vecteur (important pour les actions)
      obj["title"] = notif->title;
      obj["message"] = notif->message;
      obj["timeStamp"] = notif->timeStamp;
      obj["type"] = notif->type;
      obj["viewed"] = notif->viewed;
      
    }
  }
  
  String result;
  serializeJson(doc, result);
  return result;
}

String NotificationManager::getStatsJson() const {
  SpiRamJsonDocument doc(512);
  
  int unreadCount = 0;
  for (const Notification* notif : notifications) {
    if (notif && notif->viewed == 0) {
      unreadCount++;
    }
  }
  
  doc["total"] = getCount();
  doc["unread"] = unreadCount;
  doc["read"] = getCount() - unreadCount;
  doc["maxCapacity"] = maxNotifications;
  doc["psramFree"] = ESP.getFreePsram();
  doc["psramTotal"] = ESP.getPsramSize();
  
  String result;
  serializeJson(doc, result);
  return result;
}


void NotificationManager::setPushCallback(PushCallback cb) {
    _pushCallback = cb;
}

void NotificationManager::cleanupOldNotifications() {
  if (notifications.size() < maxNotifications) return;
  
  // Supprimer les plus anciennes notifications (FIFO)
  size_t toRemove = notifications.size() - maxNotifications + 10; // Garde une marge
  
  for (size_t i = 0; i < toRemove && !notifications.empty(); i++) {
    Notification* notif = notifications.front();
    if (notif) {
      notif->~Notification();
      freePSRAM(notif);
    }
    notifications.erase(notifications.begin());
  }
  
  log_i("Nettoyage: %d anciennes notifications supprimées\n", toRemove);
}

void* NotificationManager::allocatePSRAM(size_t size) {
  void* ptr = ps_malloc(size);
  if (!ptr) {
    log_e("Erreur allocation PSRAM: %d bytes\n", size);
  }
  return ptr;
}

void NotificationManager::freePSRAM(void* ptr) {
  if (ptr) {
    free(ptr);
  }
}