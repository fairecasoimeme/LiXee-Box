#include <Arduino.h>
#include "log.h"
#include "config.h"
#include "SPIFFS_ini.h"
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
      //const String path ="/debug/debug_"+Day+".log";
      const char* path ="/debug/debug_";
      const char* extension =".log";
      char name_with_extension[64];
      strcpy(name_with_extension,path);
      strncat(name_with_extension,Day.c_str(),2);
      strncat(name_with_extension,extension,4);
      File debugLog = safeOpenFile(name_with_extension,"a+");

      if (!debugLog)
      {
        debugLog.close();
        return false;
      }

      if (debugLog.size() > 20000)
      {
        safeCloseFile(debugLog,name_with_extension);
        debugLog = safeOpenFile(path,"w+");
      }

      char log[256];
      
      strcpy(log,"[");
      strncat(log,FormattedDate.c_str(),16);
      strncat(log,"] : Free Mem RAM : ",19);
      char freeHeap[16];
      itoa(ESP.getFreeHeap(),freeHeap,10);
      strncat(log,freeHeap,6);
      strncat(log," / Free Mem PSRAM : ",20);
      char freePsram[16];
      itoa(ESP.getFreePsram(),freePsram,10);
      strncat(log,freePsram,7);
      strncat(log," / Err :",8);
      strncat(log,text.c_str(),text.length());
      debugLog.println(log);
      safeCloseFile(debugLog,name_with_extension);
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
