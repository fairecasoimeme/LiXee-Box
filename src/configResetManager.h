#ifndef CONFIG_RESET_MANAGER_H
#define CONFIG_RESET_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>

class ConfigResetManager {
public:
    ConfigResetManager(uint16_t windowMs = 3000, uint8_t bootThreshold = 3);
    
    // Vérifie si une RAZ est demandée (appeler au début de setup)
    bool checkForReset();
    
    // Remet le compteur à zéro
    void clearCounter();
    
    // À appeler dans loop() pour reset le compteur après boot stable
    void tick();
    
    // Getters
    uint8_t getBootCount() const { return _bootCount; }
    bool isStable() const { return _cleared; }

private:
    Preferences _prefs;
    uint16_t _windowMs;
    uint8_t _bootThreshold;
    uint8_t _bootCount;
    bool _cleared;
    
    static constexpr const char* NVS_NAMESPACE = "boot";
    static constexpr const char* NVS_KEY = "count";
};

#endif // CONFIG_RESET_MANAGER_H