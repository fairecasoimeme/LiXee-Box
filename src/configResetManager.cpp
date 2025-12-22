#include "ConfigResetManager.h"

ConfigResetManager::ConfigResetManager(uint16_t windowMs, uint8_t bootThreshold)
    : _windowMs(windowMs)
    , _bootThreshold(bootThreshold)
    , _bootCount(0)
    , _cleared(false) {
}

bool ConfigResetManager::checkForReset() {
    _prefs.begin(NVS_NAMESPACE, false);
    _bootCount = _prefs.getUChar(NVS_KEY, 0) + 1;
    _prefs.putUChar(NVS_KEY, _bootCount);
    _prefs.end();
    
    Serial.printf("[RESET] Boot count: %d/%d\n", _bootCount, _bootThreshold);
    
    if (_bootCount >= _bootThreshold) {
        Serial.println("[RESET] !!! RAZ CONFIG DECLENCHEE !!!");
        clearCounter();
        return true;
    }
    
    return false;
}

void ConfigResetManager::clearCounter() {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.clear();
    _prefs.end();
    _bootCount = 0;
}

void ConfigResetManager::tick() {
    if (!_cleared && millis() > _windowMs) {
        clearCounter();
        _cleared = true;
        Serial.println("[RESET] Boot stable - compteur remis à zéro");
    }
}