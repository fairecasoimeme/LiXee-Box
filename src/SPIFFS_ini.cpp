/*
    ----------------------   SPIFFS_ini ver. 2.0  ----------------------
      (c) 2020 SpeedBit, reg. Czestochowa, Poland
    --------------------------------------------------------------------
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "SPIFFS_ini.h"
#define ARDUINOJSON_USE_LONG_LONG 1
#define ARDUINOJSON_SLOT_ID_SIZE 2
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_task_wdt.h"
#include <map>
#include "stdlib.h"
#include "log.h"
#include "config.h"

extern ConfigGeneralStruct ConfigGeneral;
extern ConfigSettingsStruct ConfigSettings;

extern String FormattedDate;
extern String Hour;
extern String Day;
extern String Month;
extern String Minute;
extern String Year;
extern String Yesterday;

extern SemaphoreHandle_t file_Mutex;
extern SemaphoreHandle_t inifile_Mutex;

// Carte pour suivre les fichiers ouverts
std::map<String, bool> openFilesMap;

// Fonction pour ouvrir un fichier avec suivi d'état

File safeOpenFile(const char *path, const char *mode) {
  int i=0;
  while (openFilesMap[String(path)]) {
    esp_task_wdt_reset();
    log_e("Fichier déjà ouvert : %s\n",path);
    vTaskDelay(20);
    i++;
    if (i > 5)
    {
      return (File)NULL;
    }
  }

  File file = LittleFS.open(path, mode);
  if (file) {
    openFilesMap[String(path)] = true;
  }
  return file;
}

// Fonction pour fermer un fichier avec mise à jour de l'état
void safeCloseFile(File &file, const char *path) {
  if (file) {
    file.close();
    openFilesMap[String(path)] = false;
  }

}

bool copyFile(String srcPath) {
  // Ouvrir le fichier source en lecture
  File srcFile = LittleFS.open("/db/"+srcPath, "r");
  if (!srcFile) {
   log_e("Failed to open source file for reading.");
    srcFile.close();
    return false;
  }

  // Ouvrir/Cree le fichier de destination en écriture
  String filename = "/bk/"+srcPath;
  File destFile = safeOpenFile(filename.c_str(), "w");
  if (!destFile) {
    log_e("Failed to open destination file for writing.");
    srcFile.close();
    safeCloseFile(destFile,filename.c_str());
    return false;
  }

  // Buffer de lecture
  uint8_t buffer[128];
  size_t bytesRead;

  // Lire le fichier source et écrire dans le fichier de destination
  while ((bytesRead = srcFile.read(buffer, sizeof(buffer))) > 0) {
    destFile.write(buffer, bytesRead);
  }

  // Fermer les fichiers
  srcFile.close();
  safeCloseFile(destFile,filename.c_str());

  return true;
}

bool ini_exist(String path)
{
  // Vérifier si le fichier existe
  if (LittleFS.exists("/db/" + path))
  {
    return true;
  }
  else
  {
    return false;
  }
}

bool init_raz_energy(String path, String time)
{
  if (path.length() > 0)
  {
    const char* prefix ="/db/nrg_";
    char name_with_extension[64];
    strcpy(name_with_extension,prefix);
    strcat(name_with_extension,path.c_str());
    //if (xSemaphoreTake(file_Mutex, portMAX_DELAY) == pdTRUE) 
    //{
      File file = LittleFS.open(name_with_extension, "r+");
      if (!file)
      {
        file = safeOpenFile(name_with_extension, "w+");
        DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier (init_raz_energy) - "));
        DEBUG_PRINTLN(path);
        if (!file)
        {
          DEBUG_PRINTLN(F("Impossible de créer le fichier (Energy) "));
          safeCloseFile(file,name_with_extension);
          //xSemaphoreGive(file_Mutex);
          return false;
        }
      }
      size_t filesize = file.size();
      DynamicJsonDocument doc(MAXHEAP);
      if (filesize > 0)
      {
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        if (error)
        {
          String err;
          err += F("Erreur lors de la désérialisation du fichier (init_raz_energy) : ");
          err += error.c_str();
          err += " "+path;
          DEBUG_PRINTLN(err);
          addDebugLog(err);
          //xSemaphoreGive(file_Mutex);
          return false;
        }
      }
      

      if (time == "hour")
      {
        JsonObject sections = doc[F("hours")]["graph"][Hour].as<JsonObject>();
        for (JsonPair j : sections)
        {
            doc[F("hours")]["graph"][Hour][j.key().c_str()]=0;
        } 
      }else if (time == "day")
      {
        JsonObject sections = doc[F("days")]["graph"][Day].as<JsonObject>();
        for (JsonPair j : sections)
        {
            doc[F("days")]["graph"][Day][j.key().c_str()]=0;
        } 
      }else if (time == "month")
      {
        JsonObject sections = doc[F("months")]["graph"][Month].as<JsonObject>();
        for (JsonPair j : sections)
        {
            doc[F("months")]["graph"][Month][j.key().c_str()]=0;
        } 
      }else if (time == "year")
      {
        JsonObject sections = doc[F("years")]["graph"][Month].as<JsonObject>();
        for (JsonPair j : sections)
        {
            doc[F("years")]["graph"][Month][j.key().c_str()]=0;
        } 
      }
      file = safeOpenFile(name_with_extension, "w+");
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
        DEBUG_PRINTLN(path);
        safeCloseFile(file,name_with_extension);
        //xSemaphoreGive(file_Mutex);
        return false;
      }
      //  Écrire les données dans le fichier
      if (!doc.isNull())
      {
        if (serializeJson(doc, file) == 0)
        {
          DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier"));
          safeCloseFile(file,name_with_extension);
          //xSemaphoreGive(file_Mutex);
          return false;
        }
      }
    // Fermer le fichier
    safeCloseFile(file,name_with_extension);
    //xSemaphoreGive(file_Mutex);
    return true;
    //}
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  else
  {
    addDebugLog(F("PB path.length (ini_energy) "));
    return false;
  }
  // Délai pour simuler une tâche
  
}

bool ini_energy(String path, String section, String value)
{
  if (ConfigSettings.enableHistory)
  {
    if (path.length() > 0)
    {
      const char* prefix ="/db/nrg_";
      char name_with_extension[64];
      strcpy(name_with_extension,prefix);
      strcat(name_with_extension,path.c_str());
      //if (xSemaphoreTake(file_Mutex, portMAX_DELAY) == pdTRUE) 
      //{
        File file = LittleFS.open(name_with_extension, "r+");
        if (!file)
        {
          file = safeOpenFile(name_with_extension, "w+");
          DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
          DEBUG_PRINTLN(path);
          if (!file)
          {
            DEBUG_PRINTLN(F("Impossible de créer le fichier (Energy) "));
            safeCloseFile(file,name_with_extension);
            //xSemaphoreGive(file_Mutex);
            return false;
          }
        }
        size_t filesize = file.size();

        DynamicJsonDocument doc(MAXHEAP);
        if (filesize > 0)
        {
          DeserializationError error = deserializeJson(doc, file);
          file.close();
          if (error)
          {
            String err;
            err += F("Erreur lors de la désérialisation du fichier (ini_energy) : ");
            err += error.c_str();
            DEBUG_PRINTLN(err);
            addDebugLog(err);
            
            //xSemaphoreGive(file_Mutex);
            return false;
          }
        }

        // Ajouter des valeurs dans le fichier json
        long tmpvalue = strtol(value.c_str(), NULL, 16);

        if (tmpvalue >0)
        {
          if (Year != "")
          {

            doc[F("hours")][Hour][section] = tmpvalue;
            doc[F("days")][Day][section] = tmpvalue;
            doc[F("months")][Month][section] = tmpvalue;
            doc[F("years")][Year][section] = tmpvalue;
          }
        
          file = safeOpenFile(name_with_extension, "w+");
          if (!file || file.isDirectory())
          {
            DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
            DEBUG_PRINTLN(path);
            safeCloseFile(file,name_with_extension);
           // xSemaphoreGive(file_Mutex);
            return false;
          }
          // serializeJsonPretty(doc, Serial);
          //  Écrire les données dans le fichier
          if (!doc.isNull())
          {
            if (serializeJson(doc, file) == 0)
            {
              DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier"));
              safeCloseFile(file,name_with_extension);
             // xSemaphoreGive(file_Mutex);
              return false;
            }
          }

          // Fermer le fichier
          safeCloseFile(file,name_with_extension);   
        } 
        //xSemaphoreGive(file_Mutex);
      //}
      // Délai pour simuler une tâche
      vTaskDelay(10 / portTICK_PERIOD_MS);
      return true;
    }
    else
    {
      addDebugLog(F("PB path.length (ini_energy) "));
      return false;
    }
  }else{
    return true;
  }
}

bool ini_trendEnergy(String path, String section, String value)
{
  if (ConfigSettings.enableHistory)
  {
    if (path.length() > 0)
    {
      const char* prefix ="/db/nrg_";
      char name_with_extension[64];
      strcpy(name_with_extension,prefix);
      strcat(name_with_extension,path.c_str());
      //if (xSemaphoreTake(file_Mutex, portMAX_DELAY) == pdTRUE) 
      //{
        File file = LittleFS.open(name_with_extension, "r");
        if (!file)
        {
          file = safeOpenFile(name_with_extension, "w+");
          DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
          DEBUG_PRINTLN(path);
          if (!file)
          {
            DEBUG_PRINTLN(F("Impossible de créer le fichier (trendEnergy) "));
            safeCloseFile(file,name_with_extension);
           // xSemaphoreGive(file_Mutex);
            return false;
          }
        }
        size_t filesize = file.size();
        DynamicJsonDocument doc(MAXHEAP);

        if (filesize > 0)
        {
          DeserializationError error = deserializeJson(doc, file);
          file.close();
          if (error)
          {  
            //xSemaphoreGive(file_Mutex);
            return false;
          }
        }
        // Ajouter des valeurs dans le fichier json

        long tmpvalue = strtol(value.c_str(), NULL, 16);
        if (tmpvalue >0)
        {
          if (Year != "")
          {
            // hour
            if (!doc[F("hours")][F("last")][section])
            {
              doc[F("hours")][F("trend")][section] = 0;
            }
            else
            {
              signed int result;
              int tmpHour = (Hour.toInt() - 1);
              if (tmpHour < 0)
              {
                tmpHour = 23;
              }
              String hourtmp = tmpHour < 10 ? "0" + String(tmpHour) : String(tmpHour);
              int tmp = doc[F("hours")][F("last")][section].as<int>();
              if (doc[F("hours")][hourtmp][section].as<int>()==0)
              {
                doc[F("hours")][hourtmp][section]=doc[F("hours")][F("last")][section];
              }
              
              result = (tmp - doc[F("hours")][hourtmp][section].as<int>());

              if (!doc[F("hours")][hourtmp][section])
              {
                doc[F("hours")][F("graph")][Hour][section]= 0;
              }
              else
              {
                doc[F("hours")][F("graph")][Hour][section]= result;
              }

              doc[F("hours")][F("trend")][section] = result;
            }
            doc[F("hours")][F("last")][section] = tmpvalue;

            // day
            if (!doc[F("days")][F("last")][section])
            {
              doc[F("days")][F("trend")][section] = 0;
            }
            else
            {

              signed int result;

              int tmpDay = Yesterday.toInt();

              String daytmp = tmpDay < 10 ? "0" + String(tmpDay) : String(tmpDay);
              int tmp = doc[F("days")][F("last")][section].as<int>();

              if (doc[F("days")][daytmp][section].as<int>()==0)
              {
                doc[F("days")][daytmp][section]=doc[F("days")][F("last")][section];
              }
              
              result = tmp - doc[F("days")][daytmp][section].as<int>();

              if (!doc[F("days")][daytmp][section])
              {
                doc[F("days")][F("graph")][Day][section] = 0;
              }
              else
              {
                doc[F("days")][F("graph")][Day][section] = result;
              }

              doc[F("days")][F("trend")][section] = result;
            }
            doc[F("days")][F("last")][section] = tmpvalue;

            // month
            if (!doc[F("months")][F("last")][section])
            {
              doc[F("months")][F("trend")][section] = 0;
            }
            else
            {

              signed int result;

              int tmpMonth = (Month.toInt() - 1);
              if (tmpMonth < 1)
              {
                tmpMonth = 12;
              }

              String monthtmp = tmpMonth < 10 ? "0" + String(tmpMonth) : String(tmpMonth);
              int tmp = doc[F("months")][F("last")][section].as<int>();
              if (doc[F("months")][monthtmp][section].as<int>()==0)
              {
                doc[F("months")][monthtmp][section]=doc[F("months")][F("last")][section];
              }
              
              result = tmp - doc[F("months")][monthtmp][section].as<int>();

              if (!doc[F("months")][monthtmp][section])
              {
                doc[F("months")][F("graph")][Month][section] = 0;
              }
              else
              {
                doc[F("months")][F("graph")][Month][section] = result;
              }

              doc[F("months")][F("trend")][section] = result;
            }
            doc[F("months")][F("last")][section] = tmpvalue;

            // year
            if (!doc[F("years")][F("last")][section])
            {
              doc[F("years")][F("trend")][section] = 0;
            }
            else
            {

              signed int result;

              int tmpYear = (Year.toInt() - 1);

              String yeartmp = tmpYear < 10 ? "0" + String(tmpYear) : String(tmpYear);
              int tmp = doc[F("years")][F("last")][section].as<int>();
              if (doc[F("years")][yeartmp][section].as<int>()==0)
              {
                doc[F("years")][yeartmp][section]=doc[F("years")][F("last")][section];
              }
              
              result = tmp - doc[F("years")][yeartmp][section].as<int>();

              if (!doc[F("years")][yeartmp][section])
              {
                // doc[section]["year"]["graph"][Year]=0;
                long int monthSum = 0;
                JsonObject graph = doc[F("months")][F("graph")][section].as<JsonObject>();
                for (JsonPair j : graph)
                {
                  monthSum += j.value().as<long int>();
                }
                doc[F("years")][F("graph")][Year][section] = monthSum;
              }
              else
              {
                doc[F("years")][F("graph")][Year][section] = result;
              }

              doc[F("years")][F("trend")][section] = result;
            }
            doc[F("years")][F("last")][section] = tmpvalue;
          }
        }
      

        // Écrire les données dans le fichier
        file = safeOpenFile(name_with_extension, "w+");
        if (!file || file.isDirectory())
        {
          DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
          DEBUG_PRINTLN(path);
          safeCloseFile(file,name_with_extension);
          //xSemaphoreGive(file_Mutex);
          return false;
        }
        if (!doc.isNull())
        {
          if (serializeJson(doc, file) == 0)
          {
            DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier"));
            safeCloseFile(file,name_with_extension);
            //xSemaphoreGive(file_Mutex);
            return false;
          }
        }

        // Fermer le fichier
        safeCloseFile(file,name_with_extension);
        //xSemaphoreGive(file_Mutex);
      //}
      vTaskDelay(10 / portTICK_PERIOD_MS);
      return true;
    }
    else
    {
      return false;
    }
  }else{
    return true;
  }
}


bool ini_trendPower(String path, String section, String value)
{

  if (ConfigSettings.enableHistory)
  {
    if (path.length() > 0)
    {
      const char* prefix ="/db/pwr_";
      char name_with_extension[64];
      strcpy(name_with_extension,prefix);
      strcat(name_with_extension,path.c_str());
      //if (xSemaphoreTake(file_Mutex, portMAX_DELAY) == pdTRUE) 
      //{
        File file = LittleFS.open(name_with_extension, "r");
        if (!file)
        {
          file = safeOpenFile(name_with_extension, "w+");
          DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
          DEBUG_PRINTLN(path);
          if (!file)
          {
            DEBUG_PRINTLN(F("Impossible de créer le fichier (trendPower) "));
            safeCloseFile(file,name_with_extension);
            //xSemaphoreGive(file_Mutex);
            return false;
          }
        }
        size_t filesize = file.size();
        DynamicJsonDocument doc(MAXHEAP);

        if (filesize > 0)
        {
          DeserializationError error = deserializeJson(doc, file);
          file.close();
          if (error)
          {
            //xSemaphoreGive(file_Mutex);
            return false;
          }
        }

        // Ajouter des valeurs dans le fichier json
        long tmpvalue = strtol(value.c_str(), NULL, 16);

        // if ((Year != "1970")||(Year != ""))
        if (Year != "")
        {
          if (!doc[section][F("last")])
          {
            doc[section][F("trend")] = 0;
          } else
          {

            signed int result;
            int tmp = doc[section][F("last")].as<int>();
            result = (tmpvalue - tmp);

            doc[section][F("trend")] = String(result);
          }
          doc[section][F("last")] = String(tmpvalue);
        }
        // serializeJsonPretty(doc, Serial);
        //  Écrire les données dans le fichier
        file = safeOpenFile(name_with_extension, "w+");
        if (!file || file.isDirectory())
        {
          DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
          DEBUG_PRINTLN(path);
          safeCloseFile(file,name_with_extension);
          //xSemaphoreGive(file_Mutex);
          return false;
        }
        if (!doc.isNull())
        {
          if (serializeJson(doc, file) == 0)
          {
            DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier"));
            safeCloseFile(file,name_with_extension);
            //xSemaphoreGive(file_Mutex);
            return false;
          }
        }

        // Fermer le fichier
        safeCloseFile(file,name_with_extension);
        //xSemaphoreGive(file_Mutex);
        
      //}
      vTaskDelay(10 / portTICK_PERIOD_MS);
      return true;
    }
    else
    {
      return false;
    }
  }else{
    return true;
  }
}

bool ini_power2(String path,String section,String value)
{
  if (ConfigSettings.enableHistory)
  {
    if (path.length() > 0)
    {
      const char* tmp = "/tmppwr";
      char tmpFilenamePower[64];
      strcpy(tmpFilenamePower,tmp);
      strcat(tmpFilenamePower,path.c_str());

      const char* prefix ="/db/pwr_";
      char name_with_extension[64];
      strcpy(name_with_extension,prefix);
      strcat(name_with_extension,path.c_str());
      //if (xSemaphoreTake(file_Mutex, portMAX_DELAY) == pdTRUE) 
      //{
        File file = LittleFS.open(name_with_extension, "r");
        if (!file)
        {
          file = safeOpenFile(name_with_extension, "w+");
          DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
          DEBUG_PRINTLN(path);
          if (!file)
          {
            DEBUG_PRINTLN(F("Impossible de créer le fichier (trendPower) "));
            safeCloseFile(file,name_with_extension);
            //xSemaphoreGive(file_Mutex);
            return false;
          }
        }
        size_t filesize = file.size();

        DynamicJsonDocument doc(MAXHEAP);

        if (filesize > 0)
        {
          DEBUG_PRINT(F("deserializeJson (iniPower) "));
          DEBUG_PRINTLN(ESP.getFreePsram());
          DeserializationError error = deserializeJson(doc, file);
          file.close();
          DEBUG_PRINT(F("Memory Usage : "));
          DEBUG_PRINTLN(doc.memoryUsage());
          if (error)
          {
            String err;
            err = F("ERROR : deserializeJson (iniPower) ");
            err += error.c_str();
            DEBUG_PRINTLN(err);
            addDebugLog(err);
            //xSemaphoreGive(file_Mutex);
            return false;
          }
        }

        long tmpvalue = strtol(value.c_str(), NULL, 16);

        // if ((Year != "1970")||(Year != ""))
        if (Year != "")
        {
          JsonArray powerArray = doc[F("datas")].as<JsonArray>();
          int sizeArray = powerArray.size();
          DEBUG_PRINTLN(sizeArray);
          DEBUG_PRINTLN(ConfigGeneral.powerMaxDatas);
          // doc[section]["minute"][sizeArray][Hour +":"+ Minute]=tmpvalue;
          DEBUG_PRINTLN(Hour + ":" + Minute);
          DEBUG_PRINTLN(doc[F("datas")][(sizeArray-1)][F("y")].as<String>());
          if (( doc[F("datas")][(sizeArray-1)][F("y")].as<String>() == Hour + ":" + Minute))
          {
            sizeArray--;
          }
          DEBUG_PRINTLN(sizeArray);
          doc[F("datas")][sizeArray][F("y")] = Hour + ":" + Minute;
          doc[F("datas")][sizeArray][section] = tmpvalue;
          if (sizeArray >= ConfigGeneral.powerMaxDatas)
          {
            powerArray.remove(0);
          }

          if (!doc[section][F("min")])
          {
            doc[section][F("min")] = String(tmpvalue);
          }
          else
          {
            if (tmpvalue < (long)doc[section][F("min")])
            {
              doc[section][F("min")] = String(tmpvalue);
            }
          }
          if (!doc[section][F("max")])
          {
            doc[section][F("max")] = String(tmpvalue);
          }
          else
          {
            if (tmpvalue > (long)doc[section][F("max")])
            {
              doc[section][F("max")] = String(tmpvalue);
            }
          }
        }
        // serializeJsonPretty(doc, Serial);
        //  Écrire les données dans le fichier
        File tmpFile = safeOpenFile(tmpFilenamePower, "w+");
        if (!tmpFile || tmpFile.isDirectory())
        {
          DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
          DEBUG_PRINTLN(path);
          safeCloseFile(tmpFile,tmpFilenamePower);
          //xSemaphoreGive(file_Mutex);
          return false;
        }
        if (!doc.isNull())
        {
          if (serializeJson(doc, tmpFile) == 0)
          {
            DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier"));
            safeCloseFile(tmpFile,tmpFilenamePower);
            //xSemaphoreGive(file_Mutex);
            return false;
          }
        }

        // Fermer le fichier
        safeCloseFile(tmpFile,tmpFilenamePower);
        if (!LittleFS.rename(tmpFilenamePower, name_with_extension)) {
          DEBUG_PRINTLN("Échec du renommage du fichier temporaire : ini_power2");
        }

       // xSemaphoreGive(file_Mutex);
     // }
      vTaskDelay(10 / portTICK_PERIOD_MS);
      return true;
      
    }
    else
    {
      return false;
    }
  }else{
    return true;
  }
}

struct SpiRamAllocator {
        void* allocate(size_t size) {
                return ps_malloc(size);

        }
        void deallocate(void* pointer) {
                free(pointer);
        }
};

using SpiRamJsonDocument = BasicJsonDocument<SpiRamAllocator>;

/*bool ini_power(String path, String section, String value)
{

  if (path.length() > 0)
  {
    const char* prefix ="/db/";
    char name_with_extension[64];
    strcpy(name_with_extension,prefix);
    strcpy(name_with_extension,section.c_str());
    strcpy(name_with_extension,"_");
    strcat(name_with_extension,path.c_str());
    File file = safeOpenFile(name_with_extension, "r");
    if (!file)
    {
      file = safeOpenFile(name_with_extension, "w+");
      DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
      DEBUG_PRINTLN(path);
      if (!file)
      {
        DEBUG_PRINTLN(F("Impossible de créer le fichier (iniPower) "));
        file.close();
        return false;
      }
    }
    size_t filesize = file.size();

    DynamicJsonDocument doc(MAXHEAP);

    if (filesize > 0)
    {
      DEBUG_PRINT(F("deserializeJson (iniPower) "));
      DEBUG_PRINTLN(ESP.getFreePsram());
      DeserializationError error = deserializeJson(doc, file);
      DEBUG_PRINT(F("Memory Usage : "));
      DEBUG_PRINTLN(doc.memoryUsage());
      if (error)
      {
        String err;
        err = F("ERROR : deserializeJson (iniPower) ");
        err += error.c_str();
        DEBUG_PRINTLN(err);
        addDebugLog(err);
        
        file.close();
        return false;
      }
    }
    file.close();

    // Ajouter des valeurs dans le fichier json

    long tmpvalue = strtol(value.c_str(), NULL, 16);

    if ((Year != "1970")||(Year != ""))
    {
      JsonArray powerArray = doc[section][F("minute")].as<JsonArray>();
      int sizeArray = powerArray.size();
      DEBUG_PRINTLN(sizeArray);
      DEBUG_PRINTLN(ConfigGeneral.powerMaxDatas);
      // doc[section]["minute"][sizeArray][Hour +":"+ Minute]=tmpvalue;
      doc[section][F("minute")][sizeArray][F("y")] = Hour + ":" + Minute;
      doc[section][F("minute")][sizeArray][F("a")] = tmpvalue;
      if (sizeArray >= ConfigGeneral.powerMaxDatas)
      {
        powerArray.remove(0);
      }
      if (!doc[section][F("min")])
      {
        doc[section][F("min")] = String(tmpvalue);
      }
      else
      {
        if (tmpvalue < (long)doc[section][F("min")])
        {
          doc[section][F("min")] = String(tmpvalue);
        }
      }
      if (!doc[section][F("max")])
      {
        doc[section][F("max")] = String(tmpvalue);
      }
      else
      {
        if (tmpvalue > (long)doc[section][F("max")])
        {
          doc[section][F("max")] = String(tmpvalue);
        }
      }
    }
    // serializeJsonPretty(doc, Serial);
    //  Écrire les données dans le fichier
    
    file = safeOpenFile(name_with_extension, "w+");
    if (!file || file.isDirectory())
    {
      DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
      DEBUG_PRINTLN(path);
      file.close();
      return false;
    }
    if (!doc.isNull())
    {
      DEBUG_PRINT(F("Memory Usage : "));
      DEBUG_PRINTLN(doc.memoryUsage());
      if (serializeJson(doc, file) == 0)
      {
        DEBUG_PRINT(F("Erreur lors de l'écriture dans le fichier serializeJson"));
        DEBUG_PRINTLN(path);
        file.close();
        return false;
      }
    }

    // Fermer le fichier
    file.close();
    return true;
  }
  else
  {
    DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier path.length() "));
    DEBUG_PRINTLN(path);
    return false;
  }
}*/

bool ini_writes(String path, WriteIni ini, bool create)
{
  
  const char* prefix ="/db/";
  char name_with_extension[64];
  strcpy(name_with_extension,prefix);
  strcat(name_with_extension,path.c_str());

  if (path.length()>0)
  {
    File fileRead = LittleFS.open(name_with_extension, "r");
    if (!fileRead)
    {
      log_e("Erreur lors de l'ouverture du fichier %s\n",path.c_str());
      File file = safeOpenFile(name_with_extension, "w+");
      if (!file)
      {
        DEBUG_PRINTLN(F("Impossible de créer le fichier (ini_writes) "));
        safeCloseFile(file,name_with_extension);
        return false;
      }else{
        safeCloseFile(file,name_with_extension);
        fileRead = LittleFS.open(name_with_extension, "r");
      }
    }

    //String filename = String(fileRead.name());
    int filesize = fileRead.size();

    DynamicJsonDocument doc(MAXHEAP);

    if (filesize>0)
    {
       DeserializationError error = deserializeJson(doc, fileRead);
       if (error)
        {
          log_e("Erreur lors de la désérialisation du fichier : %s\n", error.c_str());
          return false;
        }
    }
   
    fileRead.close();
    // Ajouter des valeurs dans le fichier json
    if (doc.size() >0 || (create))
    {
      for (int i=0;i<ini.iniPacketSize;i++)
      {
        doc[ini.i[i].section][ini.i[i].key] = ini.i[i].value;
      }
    
      // serializeJsonPretty(doc, Serial);
      //  Écrire les données dans le fichier
      File fileWrite = safeOpenFile(name_with_extension, "w+");
      if (!fileWrite || fileWrite.isDirectory())
      {
        DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
        DEBUG_PRINTLN(path);
        safeCloseFile(fileWrite,name_with_extension);
        return false;
      }

      if (serializeJson(doc, fileWrite) == 0)
      {
        DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier"));
        safeCloseFile(fileWrite,name_with_extension);
        return false;
      }
      // Fermer le fichier
      safeCloseFile(fileWrite,name_with_extension);
      return true;
    }else{
      return false;
    }
    
  }else{
    return false;
  }
}

bool ini_write(String path, String section, String key, String value)
{

  
  const char* prefix ="/db/";
  char name_with_extension[64];
  strcpy(name_with_extension,prefix);
  strcat(name_with_extension,path.c_str());

  if (path.length()>0)
  {
    File fileRead = LittleFS.open(name_with_extension, "r");
    if (!fileRead)
    {
      log_e("Erreur lors de l'ouverture du fichier %s\n",path.c_str());
      File file = safeOpenFile(name_with_extension, "w+");
      if (!file)
      {
        DEBUG_PRINTLN(F("Impossible de créer le fichier (ini_write) "));
        safeCloseFile(file,name_with_extension);
        return false;
      }else{
        safeCloseFile(file,name_with_extension);
        fileRead = LittleFS.open(name_with_extension, "r");
      }
      // return false;
    }
    size_t filesize = fileRead.size();
    String filename = String(fileRead.name());

    DynamicJsonDocument doc(MAXHEAP);

    if (filesize > 0)
    {
      DeserializationError error = deserializeJson(doc, fileRead);
      fileRead.close();
      if (error)
      {
        // DEBUG_PRINTLN(F("Erreur lors de la désérialisation du fichier"));
        return false;
      }
    }else{
      fileRead.close();
    }

    if (!doc.isNull())
    {
      // Ajouter des valeurs dans le fichier json
      doc[section][key] = value;

      // serializeJsonPretty(doc, Serial);
      //  Écrire les données dans le fichier
      File fileWrite = safeOpenFile(name_with_extension, "w+");
      if (!fileWrite || fileWrite.isDirectory())
      {
        DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
        DEBUG_PRINTLN(path);
        safeCloseFile(fileWrite,name_with_extension);
        return false;
      }

      if (doc.size()>0)
      {
        if (serializeJson(doc, fileWrite) == 0)
        {
          DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier"));
          safeCloseFile(fileWrite,name_with_extension);
          return false;
        }
      }

      // Fermer le fichier
      safeCloseFile(fileWrite,name_with_extension);
      return true;
    }else{
      return false;
    }
  }else{
    return false;
  }
}

String ini_read(String path, String section, String key)
{
  const char* prefix ="/db/";
  char name_with_extension[64];
  strcpy(name_with_extension,prefix);
  strcat(name_with_extension,path.c_str());

  //if (xSemaphoreTake(inifile_Mutex, portMAX_DELAY) == pdTRUE) 
  //{
  File file = LittleFS.open(name_with_extension, FILE_READ);
  if (!file || file.isDirectory())
  {
    DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier ini_read : "));
    DEBUG_PRINTLN(path);
    file.close();
   // xSemaphoreGive(inifile_Mutex);
    return "Error";
  }

  // Analyser le contenu JSON du fichier
  DynamicJsonDocument doc(60240);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  vTaskDelay(10 / portTICK_PERIOD_MS);
  // Vérifier les erreurs de désérialisation
  if (error)
  {  
    //xSemaphoreGive(inifile_Mutex);
    return "Error : "+String(error.c_str());
  }

  // Accéder aux valeurs dans le fichier INI

  
  return doc[section][key];
  
}

DeviceInfo getDeviceInfo(String path)
{
  const char* prefix ="/db/";
  char name_with_extension[64];
  strcpy(name_with_extension,prefix);
  strcat(name_with_extension,path.c_str());

  DeviceInfo di;

  File file = LittleFS.open(name_with_extension, FILE_READ);
  if (!file || file.isDirectory())
  {
    DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier ini_read : "));
    DEBUG_PRINTLN(path);
    file.close();
  }

  // Analyser le contenu JSON du fichier
  DynamicJsonDocument doc(MAXHEAP);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  vTaskDelay(10 / portTICK_PERIOD_MS);
  // Vérifier les erreurs de désérialisation
  if (error)
  {  
    return di;
  }

  di.manufacturer = doc["INFO"]["manufacturer"].as<String>();
  di.model = doc["INFO"]["model"].as<String>();
  di.shortAddr = doc["INFO"]["shortAddr"].as<int>();
  di.deviceId = doc["INFO"]["device_id"].as<int>();
  di.sotfwareVersion = doc["INFO"]["software_version"].as<String>();
  di.lastSeen = doc["INFO"]["lastSeen"].as<String>();
  di.LQI = doc["INFO"]["LQI"].as<String>();

  return di;
}




String config_read(String path,String key)
{
  const char* prefix ="/config/";
  char name_with_extension[64];
  strcpy(name_with_extension,prefix);
  strcat(name_with_extension,path.c_str());
  File file = LittleFS.open(name_with_extension, "r");
  if (!file)
  {
    DEBUG_PRINTLN(F("Impossible de lire le fichier (config_read) "));
    safeCloseFile(file,name_with_extension);
    return "Error";
  }

  DynamicJsonDocument doc(MAXHEAP);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  // Vérifier les erreurs de désérialisation
  if (error)
  {
    // DEBUG_PRINTLN(F("Erreur lors de la désérialisation du fichier"));

    return "Error";
  }

  // Accéder aux valeurs dans le fichier INI
  // const char* valeur = doc[key];
  return doc[key].as<String>();
}


bool config_write(String path, String key, String value)
{
  const char* tmp = "/config/tmpFile";
  char tempFileName[64];
  strcpy(tempFileName,tmp);
  strcat(tempFileName,path.c_str());

  const char* prefix ="/config/";
  char name_with_extension[64];
  strcpy(name_with_extension,prefix);
  strcat(name_with_extension,path.c_str());
  //  xSemaphoreTake(file_Mutex, portMAX_DELAY);
    File fileRead = LittleFS.open(name_with_extension, "r");
    if (!fileRead)
    {
      log_e("Erreur lors de l'ouverture du fichier %s\n",path.c_str());
      File file = safeOpenFile(name_with_extension, "w+");
      if (!file)
      {
        DEBUG_PRINTLN(F("Impossible de créer le fichier (ini_write) "));
        safeCloseFile(file,name_with_extension);
        return false;
      }else{
        safeCloseFile(file,name_with_extension);
        fileRead = LittleFS.open(name_with_extension, "r");
      }
    }

    size_t filesize = fileRead.size();
    DynamicJsonDocument doc(MAXHEAP);

    if (filesize > 0)
    {
      DeserializationError error = deserializeJson(doc, fileRead);
      fileRead.close();
      if (error)
      {
        return "Error";
      }
    }else{
      fileRead.close();
    }
    //xSemaphoreGive(file_Mutex);
    // Ajouter des valeurs dans le fichier json
    doc[key] = value;

    // serializeJsonPretty(doc, Serial);
    //  Écrire les données dans le fichier
    //xSemaphoreTake(file_Mutex, portMAX_DELAY);
    File tpFile = safeOpenFile(tempFileName, "w+");
    if (!tpFile || tpFile.isDirectory())
    {
      DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
      DEBUG_PRINTLN(path);
      safeCloseFile(tpFile,tempFileName);
      //xSemaphoreGive(file_Mutex);
      return false;
    }

    if (!doc.isNull())
    {
      if (serializeJson(doc, tpFile) == 0)
      {
        DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier"));
        safeCloseFile(tpFile,tempFileName);
        //xSemaphoreGive(file_Mutex);
        return false;
      }
    }

    // Fermer le fichier
    safeCloseFile(tpFile,tempFileName);
    //xSemaphoreGive(file_Mutex);

    // Renommer le fichier temporaire pour remplacer l'original
    //xSemaphoreTake(file_Mutex, portMAX_DELAY);
    if (!LittleFS.rename(tempFileName, name_with_extension)) {
      DEBUG_PRINTLN("Échec du renommage du fichier temporaire : config_write");
    }
    //xSemaphoreGive(file_Mutex);
  vTaskDelay(10 / portTICK_PERIOD_MS);
  return true;
}


void scanFilesError(void)
{
  File root = LittleFS.open("/db");
  if (!root)
  {
    log_e("db : Failed to open dir");
  }

  if (!root.isDirectory())
  {
    log_e("db : not a dir");
  }
  File filedevice = root.openNextFile();
  while (filedevice) 
  {
    if (!filedevice.isDirectory())
    {
      String tmp = ini_read(filedevice.name(),"INFO","shortAddr");
      if ((filedevice.size() == 0) || (tmp.length()==0))
      {
        //recherche backup
        File rootbk = LittleFS.open("/bk");
        if (!rootbk)
        {
          log_e("bk : Failed to open dir");
        }

        if (!rootbk.isDirectory())
        {
          log_e("bk : not a dir");
        }
        File filebk = rootbk.openNextFile();
        while (filebk) 
        {
          if (memcmp(filebk.name(),filedevice.name(),strlen(filedevice.name()))==0)
          {
            filebk = LittleFS.open("/bk/"+String(filebk.name()), "r");
            if (!filebk) {
            log_e("filebk : Failed to open source file for reading.");
            }

            filedevice = LittleFS.open("/db/"+String(filedevice.name()), "w");
            if (!filedevice) {
            log_e("filedevice : Failed to open source file for reading.");
            }
            addDebugLog("backup executed");
            log_d("backup go");
            // Buffer de lecture
            uint8_t buffer[2048];
            size_t bytesRead;

            // Lire le fichier source et écrire dans le fichier de destination
            while ((bytesRead = filebk.read(buffer, sizeof(buffer))) > 0) {
              log_d("%s \r\n",buffer);
              filedevice.write(buffer, bytesRead);
            }
            log_d("restore backup");
            // Fermer les fichiers
            filebk.close();
            filedevice.close();
            break;
          }
          vTaskDelay(1);
          filebk = rootbk.openNextFile();
        }

      }
      filedevice.close();
    }
    vTaskDelay(1);
    filedevice = root.openNextFile();
  }
  
}
