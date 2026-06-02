#ifndef NOTIFICATION_MANAGER_H
#define NOTIFICATION_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>
#include <functional>
#include "config.h"
#include "PsramAllocator.h"

class NotificationManager {
public:
  typedef std::function<void(const String&, const String&, int, const char*, float, float)> PushCallback;

private:
  std::vector<Notification*, PsramAllocator<Notification*>> notifications; // Vecteur en PSRAM
  const char* jsonFilePath;
  size_t maxNotifications;
  PushCallback _pushCallback = nullptr;

public:
  // Constructeur
  NotificationManager(const char* filePath = "/config/notifications.json", size_t maxSize = 100);
  
  // Destructeur
  ~NotificationManager();
  
  // Initialisation
  bool begin();
  
  // Gestion des notifications
  bool addNotification(const String& title, const String& message, int type = 0,
                       const char* alertType = nullptr, float value = 0, float threshold = 0);
  bool addNotification(const Notification& notification);
  
  // Accès aux données
  size_t getCount() const;
  Notification* getNotification(size_t index) const;
  std::vector<Notification*> getNotifications(size_t offset = 0, size_t limit = 10) const;
  
  // Gestion des états
  bool markAsViewed(size_t index);
  bool markAllAsViewed(); 
  bool deleteNotification(size_t index);
  void clearAll();
  
  // Persistance
  bool saveToFile();
  bool loadFromFile();
  
  // Export JSON
  String toJson(size_t offset = 0, size_t limit = 10) const;
  String getStatsJson() const;

  // Push callback
  void setPushCallback(PushCallback cb);
  bool hasPushCallback() const { return _pushCallback != nullptr; }

private:
  void cleanupOldNotifications();
  void* allocatePSRAM(size_t size);
  void freePSRAM(void* ptr);
};

#endif