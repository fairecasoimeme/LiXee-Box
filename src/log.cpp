#include <Arduino.h>
#include "log.h"
#include "config.h"
#include "LittleFS.h"

extern String Hour;
extern String Day;
extern String Month;
extern String Year;
extern String FormattedDate;
extern ConfigSettingsStruct ConfigSettings;

LogConsoleType logConsole;



String verbose_print_reset_reason(int reason)
{
  switch ( reason)
  {
    case 1  : return ("Vbat power on reset");break;
    case 3  : return ("Software reset digital core");break;
    case 4  : return ("Legacy watch dog reset digital core");break;
    case 5  : return ("Deep Sleep reset digital core");break;
    case 6  : return ("Reset by SLC module, reset digital core");break;
    case 7  : return ("Timer Group0 Watch dog reset digital core");break;
    case 8  : return ("Timer Group1 Watch dog reset digital core");break;
    case 9  : return ("RTC Watch dog Reset digital core");break;
    case 10 : return ("Instrusion tested to reset CPU");break;
    case 11 : return ("Time Group reset CPU");break;
    case 12 : return ("Software reset CPU");break;
    case 13 : return ("RTC Watch dog Reset CPU");break;
    case 14 : return ("for APP CPU, reseted by PRO CPU");break;
    case 15 : return ("Reset when the vdd voltage is not stable");break;
    case 16 : return ("RTC Watch dog reset digital core and rtc module");break;
    default : return ("NO_MEAN");
  }
}

bool addDebugLog(String text)
{
  if (ConfigSettings.enableDebug)
  {
    if (FormattedDate!="")
    {
      File debugLog = LittleFS.open("/debug/debug_"+Day+".log","a+");
      
      if (!debugLog)
      {
        return false;
      }

      if (debugLog.size() > 20000)
      {
        debugLog.close();
        debugLog = LittleFS.open("/debug/debug_"+Day+".log","w+");
      }

      String log ="";
      log += F("[");
      log += FormattedDate;
      log += F("] : ");
      log += F("Free Mem RAM : ");
      log += ESP.getFreeHeap();
      log += F(" / Free Mem PSRAM : ");
      log += ESP.getFreePsram();
      log +=F(" / Err :");
      log += text;
      debugLog.println(log);
      debugLog.close();
      return true;
    }
  }
  return false;
  
}

void logPush(char c)
{
  logConsole.push(c);
}

String logPrint()
{
  
  String buff="";

  if (logConsole.isEmpty()) {
    return "";
  } else {
    for (decltype(logConsole)::index_t i = 0; i < logConsole.size() - 1; i++) {
      buff+=logConsole[i];
    }
    return buff;
  }
}

void logClear()
{
  if (!logConsole.isEmpty()) {
    logConsole.clear();
  }
}
