#include "alertChecks.h"
#include "config.h"
#include "device.h"
#include "notificationManager.h"
#include "tunnel.h"
#include "zigbee.h"
#include <ArduinoJson.h>

extern std::vector<DeviceData*> devices;
extern ConfigNotification ConfigNotif;
extern ConfigGeneralStruct ConfigGeneral;
extern NotificationManager notificationManager;
extern CircularBuffer<Notification, 10> *notifList;
extern LiXeeBoxTunnel* tunnel;
extern String Hour;
extern String Minute;
extern String Day;
extern String FormattedDate;
extern String section[12];

// --- État statique (non persisté, reset au reboot) ---

// Timers de throttle
static unsigned long lastOverPowerCheck = 0;
static unsigned long lastFreezeCheck = 0;
static bool dailyAnomalyCheckedToday = false;
static bool waterLeakCheckedToday = false;
static bool nightWaterLeakCheckedToday = false;
static bool dailyMetricsSentToday = false;
static String lastCheckedDay = "";

// OverPower state
static unsigned long overPowerFirstExceedMs = 0;
static bool overPowerActive = false;
static unsigned long overPowerLastAlertMs = 0;

// Freeze state
static unsigned long freezeLastAlertMs = 0;

// Daily consumption ring buffer (7 jours)
static long dailyConsumptionWh[7] = {0};
static int dailyConsumptionIndex = 0;
static int dailyConsumptionCount = 0;

// --- Fonctions utilitaires ---

static void pushNotification(const String& title, const String& message, int type,
                             const char* alertType = nullptr, float value = 0, float threshold = 0) {
    if (!notifList->isFull()) {
        notifList->push(Notification{title, message, FormattedDate, type, 0});
    } else {
        notifList->shift();
        notifList->push(Notification{title, message, FormattedDate, type, 0});
    }
    notificationManager.addNotification(title, message, type, alertType, value, threshold);
}

static DeviceData* findDeviceByIEEE(const char* ieee) {
    String ieeeStr(ieee);
    for (size_t i = 0; i < devices.size(); i++) {
        if (devices[i]->getDeviceID() == ieeeStr) {
            return devices[i];
        }
    }
    return nullptr;
}

// --- Vérifications ---

static void checkOverPower() {
    if (!ConfigNotif.OverPower) return;
    if (strlen(ConfigGeneral.ZLinky) == 0) return;

    DeviceData* device = findDeviceByIEEE(ConfigGeneral.ZLinky);
    if (!device) return;

    String powerHex = device->getValue(std::string("0B04"), std::string("1295"));
    if (powerHex.length() == 0) return;

    long powerW = strtol(powerHex.c_str(), NULL, 16);
    if (powerW <= 0) return;

    int threshold = ConfigNotif.OverPowerThreshold > 0 ? ConfigNotif.OverPowerThreshold : 6000;
    int durationMs = (ConfigNotif.OverPowerDuration > 0 ? ConfigNotif.OverPowerDuration : 60) * 1000;
    unsigned long cooldownMs = (unsigned long)(ConfigNotif.OverPowerCooldown > 0 ? ConfigNotif.OverPowerCooldown : 30) * 60000UL;

    if (powerW > threshold) {
        if (!overPowerActive) {
            overPowerActive = true;
            overPowerFirstExceedMs = millis();
        } else if ((millis() - overPowerFirstExceedMs) >= (unsigned long)durationMs) {
            // Durée dépassée — vérifier cooldown
            if (overPowerLastAlertMs == 0 || (millis() - overPowerLastAlertMs) >= cooldownMs) {
                overPowerLastAlertMs = millis();
                String msg = "Puissance " + String(powerW) + "W dépasse le seuil de " + String(threshold) + "W depuis " + String(ConfigNotif.OverPowerDuration) + "s";
                pushNotification("⚡🔴 Surpuissance soutenue", msg, 2,
                    "over_power", (float)powerW, (float)threshold);
            }
        }
    } else {
        overPowerActive = false;
        overPowerFirstExceedMs = 0;
    }
}

static void checkFreeze() {
    if (!ConfigNotif.Freeze) return;
    if (strlen(ConfigNotif.FreezeSensorIEEE) == 0) return;

    DeviceData* device = findDeviceByIEEE(ConfigNotif.FreezeSensorIEEE);
    if (!device) return;

    String tempHex = device->getValue(std::string("0402"), std::string("0"));
    if (tempHex.length() == 0) return;

    long tempCenti = strtol(tempHex.c_str(), NULL, 16);
    // Valeur en centièmes de degré (ex: 300 = 3.00°C)
    // Les capteurs ZCL reportent en 0.01°C

    int threshold = ConfigNotif.FreezeThreshold > 0 ? ConfigNotif.FreezeThreshold : 300;
    unsigned long cooldownMs = (unsigned long)(ConfigNotif.FreezeCooldown > 0 ? ConfigNotif.FreezeCooldown : 60) * 60000UL;

    if (tempCenti < threshold) {
        if (freezeLastAlertMs == 0 || (millis() - freezeLastAlertMs) >= cooldownMs) {
            freezeLastAlertMs = millis();
            float tempC = tempCenti / 100.0f;
            float threshC = threshold / 100.0f;
            String msg = "Température " + String(tempC, 1) + "°C en dessous du seuil de " + String(threshC, 1) + "°C";
            pushNotification("🥶 Alerte gel", msg, 2,
                "freeze", tempC, threshC);
        }
    }
}

static void checkDailyAnomaly() {
    if (!ConfigNotif.DailyAnomaly) return;
    if (strlen(ConfigGeneral.ZLinky) == 0) return;

    DeviceData* device = findDeviceByIEEE(ConfigGeneral.ZLinky);
    if (!device) return;

    // Récupérer la conso du jour depuis l'historique
    PsString dayKey(Day.c_str(), PsramAllocator<char>());
    auto it = device->energyHistory.days.graph.find(dayKey);
    long todayWh = 0;
    if (it != device->energyHistory.days.graph.end()) {
        auto attrIt = it->second.attributes.find(1);
        if (attrIt != it->second.attributes.end()) {
            todayWh = attrIt->second;
        }
    }

    // Stocker dans le ring buffer
    dailyConsumptionWh[dailyConsumptionIndex] = todayWh;
    dailyConsumptionIndex = (dailyConsumptionIndex + 1) % 7;
    if (dailyConsumptionCount < 7) dailyConsumptionCount++;

    // Besoin d'au moins 3 jours pour comparer
    if (dailyConsumptionCount < 3) return;

    // Calculer la moyenne (exclure le jour courant)
    long sum = 0;
    int count = 0;
    for (int i = 0; i < dailyConsumptionCount; i++) {
        int idx = (dailyConsumptionIndex - 1 - i + 7) % 7;
        if (i == 0) continue; // Exclure le jour courant (vient d'être ajouté)
        sum += dailyConsumptionWh[idx];
        count++;
    }
    if (count == 0 || sum == 0) return;

    long average = sum / count;
    int percent = ConfigNotif.DailyAnomalyPercent > 0 ? ConfigNotif.DailyAnomalyPercent : 30;
    long threshold = average + (average * percent / 100);

    if (todayWh > threshold) {
        float todaykWh = todayWh / 1000.0f;
        float avgkWh = average / 1000.0f;
        String msg = "Consommation du jour " + String(todaykWh, 1) + " kWh dépasse la moyenne de " + String(avgkWh, 1) + " kWh (+" + String(percent) + "%)";
        pushNotification("📊 Consommation anormale", msg, 1,
            "daily_anomaly", todaykWh, avgkWh);
    }
}

static void checkWaterLeak() {
    if (!ConfigNotif.WaterLeak) return;
    if (strlen(ConfigGeneral.Water) == 0) return;

    DeviceData* device = findDeviceByIEEE(ConfigGeneral.Water);
    if (!device) return;

    // Récupérer la conso eau du jour
    PsString dayKey(Day.c_str(), PsramAllocator<char>());
    auto it = device->energyHistory.days.graph.find(dayKey);
    long todayL = 0;
    if (it != device->energyHistory.days.graph.end()) {
        auto attrIt = it->second.attributes.find(1);
        if (attrIt != it->second.attributes.end()) {
            todayL = attrIt->second;
        }
    }

    int threshold = ConfigNotif.WaterLeakThreshold > 0 ? ConfigNotif.WaterLeakThreshold : 5;

    if (todayL > threshold) {
        String msg = "Consommation d'eau nocturne de " + String(todayL) + "L dépasse le seuil de " + String(threshold) + "L";
        pushNotification("💧 Fuite d'eau possible", msg, 2,
            "water_leak", (float)todayL, (float)threshold);
    }
}

static void checkNightWaterLeak() {
    if (!ConfigNotif.NightWaterLeak) return;
    if (strlen(ConfigGeneral.Water) == 0) return;

    DeviceData* device = findDeviceByIEEE(ConfigGeneral.Water);
    if (!device) return;

    // Sommer la consommation d'eau entre 00h et 05h
    long nightL = 0;
    const char* nightHours[] = {"00", "01", "02", "03", "04", "05"};
    for (int i = 0; i < 6; i++) {
        PsString hourKey(nightHours[i], PsramAllocator<char>());
        auto it = device->energyHistory.hours.graph.find(hourKey);
        if (it != device->energyHistory.hours.graph.end()) {
            auto attrIt = it->second.attributes.find(1);
            if (attrIt != it->second.attributes.end()) {
                nightL += attrIt->second;
            }
        }
    }

    int threshold = ConfigNotif.NightWaterLeakThreshold > 0 ? ConfigNotif.NightWaterLeakThreshold : 1;

    if (nightL > threshold) {
        String msg = "Consommation d'eau nocturne (00h-05h) de " + String(nightL) + "L dépasse le seuil de " + String(threshold) + "L";
        pushNotification("💧🌙 Fuite d'eau nocturne", msg, 2,
            "night_water_leak", (float)nightL, (float)threshold);
    }
}

static void sendDailyMetrics() {
    if (!ConfigNotif.DailyMetrics) return;

    PsString dayKey(Day.c_str(), PsramAllocator<char>());
    String msg = "";
    float totalCost = 0;
    size_t arrayLength = sizeof(section) / sizeof(section[0]);

    // Consommation électrique
    if (strlen(ConfigGeneral.ZLinky) > 0) {
        DeviceData* device = findDeviceByIEEE(ConfigGeneral.ZLinky);
        if (device) {
            long consoWh = 0;
            float consoEuros = 0;
            auto it = device->energyHistory.days.graph.find(dayKey);
            if (it != device->energyHistory.days.graph.end()) {
                for (auto &attrPair : it->second.attributes) {
                    int attrId = attrPair.first;
                    long val = attrPair.second;
                    if (val > 0) consoWh += val;
                    if (attrId > 0 && val > 0) {
                        consoEuros += val * getTarif(attrId, "energy") / 1000.0f;
                        consoEuros += (val / 1000.0f) * atof(ConfigGeneral.tarifCSPE);
                    }
                }
            }
            if (consoWh > 0) {
                msg += "⚡ Conso: " + String(consoWh / 1000.0f, 1) + " kWh";
                if (consoEuros > 0) msg += " (" + String(consoEuros, 2) + " €)";
                msg += "\n";
                totalCost += consoEuros;
            }
        }
    }

    // Production
    if (strlen(ConfigGeneral.Production) > 0) {
        DeviceData* device = findDeviceByIEEE(ConfigGeneral.Production);
        if (device) {
            long prodWh = 0;
            auto it = device->energyHistory.days.graph.find(dayKey);
            if (it != device->energyHistory.days.graph.end()) {
                auto attrIt = it->second.attributes.find(1);
                if (attrIt != it->second.attributes.end()) prodWh = attrIt->second;
            }
            if (prodWh > 0) {
                float prodEuros = prodWh * getTarif(0, "production") / 1000.0f;
                msg += "☀️ Prod: " + String(prodWh / 1000.0f, 1) + " kWh";
                if (prodEuros > 0) msg += " (+" + String(prodEuros, 2) + " €)";
                msg += "\n";
            }
        }
    }

    // Gaz
    if (strlen(ConfigGeneral.Gaz) > 0) {
        DeviceData* device = findDeviceByIEEE(ConfigGeneral.Gaz);
        if (device) {
            long gazRaw = 0;
            auto it = device->energyHistory.days.graph.find(dayKey);
            if (it != device->energyHistory.days.graph.end()) {
                auto attrIt = it->second.attributes.find(0);
                if (attrIt != it->second.attributes.end()) gazRaw = attrIt->second;
            }
            if (gazRaw > 0) {
                long gazWh = gazRaw * ConfigGeneral.coeffGaz;
                float gazEuros = 0;
                if (strcmp(ConfigGeneral.unitGaz, "Wh") == 0) {
                    gazEuros = gazWh * getTarif(0, "gaz") / 1000.0f;
                }
                msg += "🔥 Gaz: " + String(gazWh / 1000.0f, 1) + " kWh";
                if (gazEuros > 0) {
                    msg += " (" + String(gazEuros, 2) + " €)";
                    totalCost += gazEuros;
                }
                msg += "\n";
            }
        }
    }

    // Eau
    if (strlen(ConfigGeneral.Water) > 0) {
        DeviceData* device = findDeviceByIEEE(ConfigGeneral.Water);
        if (device) {
            long waterL = 0;
            auto it = device->energyHistory.days.graph.find(dayKey);
            if (it != device->energyHistory.days.graph.end()) {
                auto attrIt = it->second.attributes.find(1);
                if (attrIt != it->second.attributes.end()) waterL = attrIt->second;
            }
            if (waterL > 0) {
                float waterEuros = waterL * atof(ConfigGeneral.tarifWater) / 1000.0f;
                msg += "💧 Eau: " + String(waterL) + " L";
                if (waterEuros > 0) {
                    msg += " (" + String(waterEuros, 2) + " €)";
                    totalCost += waterEuros;
                }
                msg += "\n";
            }
        }
    }

    if (msg.length() == 0) {
        msg = "Aucune donnée disponible pour aujourd'hui.";
    } else if (totalCost > 0) {
        msg += "💰 Total: " + String(totalCost, 2) + " €";
    }

    pushNotification("📊 Résumé quotidien", msg, 0, "daily_metrics");
}

// --- Boucle principale ---

void alertChecksLoop() {
    unsigned long now = millis();

    // Reset des flags quotidiens à minuit
    if (Day != lastCheckedDay) {
        lastCheckedDay = Day;
        dailyAnomalyCheckedToday = false;
        waterLeakCheckedToday = false;
        nightWaterLeakCheckedToday = false;
        dailyMetricsSentToday = false;
    }

    // Surpuissance : toutes les 10s
    if (now - lastOverPowerCheck >= 10000) {
        lastOverPowerCheck = now;
        checkOverPower();
    }

    // Gel : toutes les 60s
    if (now - lastFreezeCheck >= 60000) {
        lastFreezeCheck = now;
        checkFreeze();
    }

    // Anomalie conso journalière : à 23h55
    if (Hour == "23" && Minute == "55" && !dailyAnomalyCheckedToday) {
        dailyAnomalyCheckedToday = true;
        checkDailyAnomaly();
    }

    // Fuite d'eau : à 06h00
    if (Hour == "06" && Minute == "00" && !waterLeakCheckedToday) {
        waterLeakCheckedToday = true;
        checkWaterLeak();
    }

    // Fuite d'eau nocturne : à 06h00
    if (Hour == "06" && Minute == "00" && !nightWaterLeakCheckedToday) {
        nightWaterLeakCheckedToday = true;
        checkNightWaterLeak();
    }

    // Résumé quotidien : à 23h55
    if (Hour == "23" && Minute == "55" && !dailyMetricsSentToday) {
        dailyMetricsSentToday = true;
        sendDailyMetrics();
    }
}
