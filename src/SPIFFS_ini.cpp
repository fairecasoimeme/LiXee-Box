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
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "stdlib.h"
#include "log.h"
#include "config.h"

extern ConfigGeneralStruct ConfigGeneral;

extern String FormattedDate;
extern String Hour;
extern String Day;
extern String Month;
extern String Minute;
extern String Year;
extern String Yesterday;

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
    File file = LittleFS.open("/db/nrg_" + path, "r+");
    if (!file)
    {
      file = LittleFS.open("/db/nrg_" + path, "w+");
      DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
      DEBUG_PRINTLN(path);
      if (!file)
      {
        DEBUG_PRINTLN(F("Impossible de créer le fichier (Energy) "));
        file.close();
        return false;
      }
    }
    size_t filesize = file.size();
    DynamicJsonDocument doc(ESP.getMaxAllocHeap());
    if (filesize > 0)
    {
      DeserializationError error = deserializeJson(doc, file);
      if (error)
      {
        String err;
        err += F("Erreur lors de la désérialisation du fichier (ini_energy) : ");
        err += error.c_str();
        DEBUG_PRINTLN(err);
        addDebugLog(err);
        file.close();
        return false;
      }
    }
    file.close();

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
    file = LittleFS.open("/db/nrg_" + path, "w+");
    if (!file || file.isDirectory())
    {
      DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
      DEBUG_PRINTLN(path);
      file.close();
      return false;
    }
    //  Écrire les données dans le fichier
    if (!doc.isNull())
    {
      if (serializeJson(doc, file) == 0)
      {
        DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier"));
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
    addDebugLog(F("PB path.length (ini_energy) "));
    return false;
  }
  
}

bool ini_energy(String path, String section, String value)
{
  if (path.length() > 0)
  {
    File file = LittleFS.open("/db/nrg_" + path, "r+");
    if (!file)
    {
      file = LittleFS.open("/db/nrg_" + path, "w+");
      DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
      DEBUG_PRINTLN(path);
      if (!file)
      {
        DEBUG_PRINTLN(F("Impossible de créer le fichier (Energy) "));
        file.close();
        return false;
      }
    }
    size_t filesize = file.size();

    DynamicJsonDocument doc(ESP.getMaxAllocHeap());
    if (filesize > 0)
    {
      DeserializationError error = deserializeJson(doc, file);
      if (error)
      {
        String err;
        err += F("Erreur lors de la désérialisation du fichier (ini_energy) : ");
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

    if (tmpvalue >0)
    {
      if (Year != "")
      {

        doc[F("hours")][Hour][section] = tmpvalue;
        doc[F("days")][Day][section] = tmpvalue;
        doc[F("months")][Month][section] = tmpvalue;
        doc[F("years")][Year][section] = tmpvalue;
      }
    
      file = LittleFS.open("/db/nrg_" + path, "w+");
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
        DEBUG_PRINTLN(path);
        file.close();
        return false;
      }
      // serializeJsonPretty(doc, Serial);
      //  Écrire les données dans le fichier
      if (!doc.isNull())
      {
        if (serializeJson(doc, file) == 0)
        {
          DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier"));
          file.close();
          return false;
        }
      }

      // Fermer le fichier
      file.close();
    }
    return true;
  }
  else
  {
    addDebugLog(F("PB path.length (ini_energy) "));
    return false;
  }
}

bool ini_trendEnergy(String path, String section, String value)
{
  if (path.length() > 0)
  {
    File file = LittleFS.open("/db/nrg_" + path, "r");
    if (!file)
    {
      file = LittleFS.open("/db/nrg_" + path, "w+");
      DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
      DEBUG_PRINTLN(path);
      if (!file)
      {
        DEBUG_PRINTLN(F("Impossible de créer le fichier (trendEnergy) "));
        file.close();
        return false;
      }
    }
    size_t filesize = file.size();
    DynamicJsonDocument doc(MAXHEAP);

    if (filesize > 0)
    {
      DeserializationError error = deserializeJson(doc, file);
      if (error)
      {
        file.close();
        return false;
      }
    }
    file.close();

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

      // Écrire les données dans le fichier
      file = LittleFS.open("/db/nrg_" + path, "w+");
      if (!file || file.isDirectory())
      {
        DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
        DEBUG_PRINTLN(path);
        file.close();
        return false;
      }
      if (!doc.isNull())
      {
        if (serializeJson(doc, file) == 0)
        {
          DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier"));
          file.close();
          return false;
        }
      }

      // Fermer le fichier
      file.close();
    }
    return true;
  }
  else
  {
    return false;
  }
}


bool ini_trendPower(String path, String section, String value)
{

  if (path.length() > 0)
  {
    File file = LittleFS.open("/db/pwr_" + path, "r");
    if (!file)
    {
      file = LittleFS.open("/db/pwr_" + path, "w+");
      DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
      DEBUG_PRINTLN(path);
      if (!file)
      {
        DEBUG_PRINTLN(F("Impossible de créer le fichier (trendPower) "));
        file.close();
        return false;
      }
    }
    size_t filesize = file.size();
    DynamicJsonDocument doc(MAXHEAP);

    if (filesize > 0)
    {
      DeserializationError error = deserializeJson(doc, file);
      if (error)
      {
        file.close();
        return false;
      }
    }
    file.close();

    // Ajouter des valeurs dans le fichier json

    long tmpvalue = strtol(value.c_str(), NULL, 16);

    // if ((Year != "1970")||(Year != ""))
    if (Year != "")
    {
      if (!doc[section][F("last")])
      {
        doc[section][F("trend")] = "0";
      }
      else
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
    file = LittleFS.open("/db/pwr_"+ path, "w+");
    if (!file || file.isDirectory())
    {
      DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
      DEBUG_PRINTLN(path);
      file.close();
      return false;
    }
    if (!doc.isNull())
    {
      if (serializeJson(doc, file) == 0)
      {
        DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier"));
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
    return false;
  }
}

bool ini_power2(String path,String section,String value)
{
  if (path.length() > 0)
  {
    File file = LittleFS.open("/db/pwr_" + path, "r");
    if (!file)
    {
      file = LittleFS.open("/db/pwr_"  + path, "w+");
      DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
      DEBUG_PRINTLN(path);
      if (!file)
      {
        DEBUG_PRINTLN(F("Impossible de créer le fichier (trendPower) "));
        file.close();
        return false;
      }
    }
    size_t filesize = file.size();

    DynamicJsonDocument doc(3000000);

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
    file = LittleFS.open("/db/pwr_" + path, "w+");
    if (!file || file.isDirectory())
    {
      DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
      DEBUG_PRINTLN(path);
      file.close();
      return false;
    }
    if (!doc.isNull())
    {
      if (serializeJson(doc, file) == 0)
      {
        DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier"));
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
    return false;
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

bool ini_power(String path, String section, String value)
{

  if (path.length() > 0)
  {
    File file = LittleFS.open("/db/" + section + "_" + path, "r");
    if (!file)
    {
      file = LittleFS.open("/db/" + section + "_" + path, "w+");
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

    DynamicJsonDocument doc(1000000);

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
    
    file = LittleFS.open("/db/" + section + "_" + path, "w+");
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
}

bool ini_write(String path, String section, String key, String value)
{

  File file = LittleFS.open("/db/" + path, "r+");
  if (!file)
  {
    file = LittleFS.open("/db/" + path, "w+");
    DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
    DEBUG_PRINTLN(path);
    if (!file)
    {
      DEBUG_PRINTLN(F("Impossible de créer le fichier (ini_write) "));
      file.close();
      return false;
    }
    // return false;
  }
  size_t filesize = file.size();

  DynamicJsonDocument doc(10192);

  if (filesize > 0)
  {
    DeserializationError error = deserializeJson(doc, file);
    if (error)
    {
      // DEBUG_PRINTLN(F("Erreur lors de la désérialisation du fichier"));
      file.close();
      return false;
    }
  }
  file.close();

  // Ajouter des valeurs dans le fichier json
  doc[section][key] = value;

  // serializeJsonPretty(doc, Serial);
  //  Écrire les données dans le fichier
  file = LittleFS.open("/db/" + path, "w+");
  if (!file || file.isDirectory())
  {
    DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
    DEBUG_PRINTLN(path);
    file.close();
    return false;
  }

  if (doc.size()>0)
  {
    if (serializeJson(doc, file) == 0)
    {
      DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier"));
      file.close();
      return false;
    }
  }

  // Fermer le fichier
  file.close();
  return true;
}

String ini_read(String path, String section, String key)
{
  File file = LittleFS.open("/db/" + path, FILE_READ);
  if (!file || file.isDirectory())
  {
    DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier ini_read : "));
    DEBUG_PRINTLN(path);
    file.close();
    return "Error";
  }

  // Analyser le contenu JSON du fichier
  DynamicJsonDocument doc(10192);
  DeserializationError error = deserializeJson(doc, file);

  // Vérifier les erreurs de désérialisation
  if (error)
  {
    // DEBUG_PRINTLN(F("Erreur lors de la désérialisation du fichier"));
    file.close();
    return "Error";
  }

  // Accéder aux valeurs dans le fichier INI
  // const char* valeur = doc[section][key];
  file.close();
  return doc[section][key];
}

String config_read(String path,String key)
{
  File file = LittleFS.open("/config/" + path, "r");
  if (!file)
  {
    DEBUG_PRINTLN(F("Impossible de lire le fichier (config_read) "));
    file.close();
    return "Error";
  }

  DynamicJsonDocument doc(10192);
  DeserializationError error = deserializeJson(doc, file);
  // Vérifier les erreurs de désérialisation
  if (error)
  {
    // DEBUG_PRINTLN(F("Erreur lors de la désérialisation du fichier"));
    file.close();
    return "Error";
  }

  // Accéder aux valeurs dans le fichier INI
  // const char* valeur = doc[key];
  file.close();
  return doc[key].as<String>();
}


bool config_write(String path, String key, String value)
{

  File file = LittleFS.open("/config/" + path, "r+");
  if (!file)
  {
    file = LittleFS.open("/config/" + path, "w+");
    DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
    DEBUG_PRINTLN(path);
    if (!file)
    {
      DEBUG_PRINTLN(F("Impossible de créer le fichier (config_write) "));
      file.close();
      return false;
    }
  }

  size_t filesize = file.size();

  DynamicJsonDocument doc(10192);

  if (filesize > 0)
  {
    DeserializationError error = deserializeJson(doc, file);
    if (error)
    {
      // DEBUG_PRINTLN(F("Erreur lors de la désérialisation du fichier"));
      file.close();
      return "Error";
    }
  }
  file.close();

  // Ajouter des valeurs dans le fichier json
  doc[key] = value;

  // serializeJsonPretty(doc, Serial);
  //  Écrire les données dans le fichier
  file = LittleFS.open("/config/" + path, "w+");
  if (!file || file.isDirectory())
  {
    DEBUG_PRINT(F("Erreur lors de l'ouverture du fichier "));
    DEBUG_PRINTLN(path);
    file.close();
    return false;
  }

  if (!doc.isNull())
  {
    if (serializeJson(doc, file) == 0)
    {
      DEBUG_PRINTLN(F("Erreur lors de l'écriture dans le fichier"));
      file.close();
      return false;
    }
  }

  // Fermer le fichier
  file.close();
  return true;
}
