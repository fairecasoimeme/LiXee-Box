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
  if (LittleFS.exists("/database/" + path))
  {
    return true;
  }
  else
  {
    return false;
  }
}

bool ini_energy(String path, String section, String value)
{

  if (path.length() > 0)
  {
    File file = LittleFS.open("/database/" + path, "r+");
    if (!file)
    {
      file = LittleFS.open("/database/" + path, "w+");
      DEBUG_PRINTLN(F("Erreur lors de l'ouverture du fichier "));
      if (!file)
      {
        DEBUG_PRINTLN(F("Impossible de créer le fichier (Energy) "));
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
        // DEBUG_PRINTLN(F("Erreur lors de la désérialisation du fichier"));
        file.close();
        return false;
      }
    }
    file.close();

    // Ajouter des valeurs dans le fichier json
    long temp;
    long tmpvalue = strtol(value.c_str(), NULL, 16);
    int sizeArray;

    // if ((Year != "1970")||(Year != ""))
    if (Year != "")
    {
      JsonArray hourArray = doc[section]["hour"].as<JsonArray>();
      sizeArray = hourArray.size();
      doc[section]["hour"][Hour]["y"] = Hour;
      doc[section]["hour"][Hour]["a"] = tmpvalue;
      if (sizeArray >= 24)
      {
        hourArray.remove(0);
      }
      JsonArray dayArray = doc[section]["day"].as<JsonArray>();
      sizeArray = dayArray.size();
      doc[section]["day"][Day]["y"] = Day;
      doc[section]["day"][Day]["a"] = tmpvalue;
      if (sizeArray >= 30)
      {
        dayArray.remove(0);
      }
      JsonArray monthArray = doc[section]["month"].as<JsonArray>();
      sizeArray = monthArray.size();
      doc[section]["month"][Month]["y"] = Month;
      doc[section]["month"][Month]["a"] = tmpvalue;
      if (sizeArray >= 12)
      {
        monthArray.remove(0);
      }
      JsonArray yearArray = doc[section]["year"].as<JsonArray>();
      sizeArray = yearArray.size();
      doc[section]["year"][Year]["y"] = Year;
      doc[section]["year"][Year]["a"] = tmpvalue;
      if (sizeArray >= 10)
      {
        yearArray.remove(0);
      }
    }
    file = LittleFS.open("/database/" + path, "w+");
    if (!file || file.isDirectory())
    {
      DEBUG_PRINTLN(F("Erreur lors de l'ouverture du fichier "));
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
    return true;
  }
  else
  {
    return false;
  }
}
bool ini_trendEnergy(String path, String section, String value)
{
  if (path.length() > 0)
  {
    File file = LittleFS.open("/database/energy_" + path, "r");
    if (!file)
    {
      file = LittleFS.open("/database/energy_" + path, "w+");
      DEBUG_PRINTLN(F("Erreur lors de l'ouverture du fichier "));
      if (!file)
      {
        DEBUG_PRINTLN(F("Impossible de créer le fichier (trendEnergy) "));
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
      // hour
      if (!doc[section]["hour"]["last"])
      {
        doc[section]["hour"]["trend"] = 0;
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
        int tmp = doc[section]["hour"]["last"].as<int>();
        // result = (tmpvalue - tmp);
        result = (tmp - doc[section]["hour"][hourtmp]["a"].as<int>());

        if (!doc[section]["hour"][hourtmp])
        {
          doc[section]["hour"]["graph"][Hour] = 0;
        }
        else
        {
          doc[section]["hour"]["graph"][Hour] = result;
        }

        doc[section]["hour"]["trend"] = result;
      }
      doc[section]["hour"]["last"] = tmpvalue;

      // day
      if (!doc[section]["day"]["last"])
      {
        doc[section]["day"]["trend"] = 0;
      }
      else
      {

        signed int result;

        int tmpDay = Yesterday.toInt();

        String daytmp = tmpDay < 10 ? "0" + String(tmpDay) : String(tmpDay);
        int tmp = doc[section]["day"]["last"].as<int>();
        result = tmp - doc[section]["day"][daytmp]["a"].as<int>();

        if (!doc[section]["day"][daytmp])
        {
          doc[section]["day"]["graph"][Day] = 0;
        }
        else
        {
          doc[section]["day"]["graph"][Day] = tmp - doc[section]["day"][daytmp]["a"].as<int>();
        }

        doc[section]["day"]["trend"] = result;
      }
      doc[section]["day"]["last"] = tmpvalue;

      // month
      if (!doc[section]["month"]["last"])
      {
        doc[section]["month"]["trend"] = 0;
      }
      else
      {

        signed int result;

        int tmpMonth = (Month.toInt() - 1);
        if (tmpMonth < 0)
        {
          tmpMonth = 12;
        }

        String monthtmp = tmpMonth < 10 ? "0" + String(tmpMonth) : String(tmpMonth);
        int tmp = doc[section]["month"]["last"].as<int>();
        result = tmp - doc[section]["month"][monthtmp]["a"].as<int>();

        if (!doc[section]["month"][monthtmp])
        {
          doc[section]["month"]["graph"][Month] = 0;
        }
        else
        {
          doc[section]["month"]["graph"][Month] = result;
        }

        doc[section]["month"]["trend"] = result;
      }
      doc[section]["month"]["last"] = tmpvalue;

      // year
      if (!doc[section]["year"]["last"])
      {
        doc[section]["year"]["trend"] = 0;
      }
      else
      {

        signed int result;

        int tmpYear = (Year.toInt() - 1);

        String yeartmp = tmpYear < 10 ? "0" + String(tmpYear) : String(tmpYear);
        int tmp = doc[section]["year"]["last"].as<int>();
        result = tmp - doc[section]["year"][yeartmp]["a"].as<int>();

        if (!doc[section]["year"][yeartmp])
        {
          // doc[section]["year"]["graph"][Year]=0;
          long int monthSum = 0;
          JsonObject graph = doc[section]["month"]["graph"].as<JsonObject>();
          for (JsonPair j : graph)
          {
            monthSum += j.value().as<long int>();
          }
          doc[section]["year"]["graph"][Year] = monthSum;
        }
        else
        {
          doc[section]["year"]["graph"][Year] = result;
        }

        doc[section]["year"]["trend"] = result;
      }
      doc[section]["year"]["last"] = tmpvalue;
    }

    // Écrire les données dans le fichier
    file = LittleFS.open("/database/energy_" + path, "w+");
    if (!file || file.isDirectory())
    {
      DEBUG_PRINTLN(F("Erreur lors de l'ouverture du fichier "));
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
bool ini_trendPower(String path, String section, String value)
{

  if (path.length() > 0)
  {
    File file = LittleFS.open("/database/power_" + section + "_" + path, "r");
    if (!file)
    {
      file = LittleFS.open("/database/power_" + section + "_" + path, "w+");
      DEBUG_PRINTLN(F("Erreur lors de l'ouverture du fichier "));
      if (!file)
      {
        DEBUG_PRINTLN(F("Impossible de créer le fichier (trendPower) "));
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
      if (!doc[section]["last"])
      {
        doc[section]["trend"] = "0";
      }
      else
      {

        signed int result;
        int tmp = doc[section]["last"].as<int>();
        result = (tmpvalue - tmp);

        doc[section]["trend"] = String(result);
      }

      doc[section]["last"] = String(tmpvalue);
    }
    // serializeJsonPretty(doc, Serial);
    //  Écrire les données dans le fichier
    file = LittleFS.open("/database/power_" + section + "_" + path, "w+");
    if (!file || file.isDirectory())
    {
      DEBUG_PRINTLN(F("Erreur lors de l'ouverture du fichier "));
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
bool ini_power(String path, String section, String value)
{

  if (path.length() > 0)
  {
    File file = LittleFS.open("/database/power_" + section + "_" + path, "r");
    if (!file)
    {
      file = LittleFS.open("/database/power_" + section + "_" + path, "w+");
      DEBUG_PRINTLN(F("Erreur lors de l'ouverture du fichier "));
      if (!file)
      {
        DEBUG_PRINTLN(F("Impossible de créer le fichier (trendPower) "));
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
      JsonArray powerArray = doc[section]["minute"].as<JsonArray>();
      int sizeArray = powerArray.size();
      DEBUG_PRINTLN(sizeArray);
      DEBUG_PRINTLN(ConfigGeneral.powerMaxDatas);
      // doc[section]["minute"][sizeArray][Hour +":"+ Minute]=tmpvalue;
      doc[section]["minute"][sizeArray]["y"] = Hour + ":" + Minute;
      doc[section]["minute"][sizeArray]["a"] = tmpvalue;
      if (sizeArray >= ConfigGeneral.powerMaxDatas)
      {
        powerArray.remove(0);
      }
      if (!doc[section]["min"])
      {
        doc[section]["min"] = String(tmpvalue);
      }
      else
      {
        if (tmpvalue < (long)doc[section]["min"])
        {
          doc[section]["min"] = String(tmpvalue);
        }
      }
      if (!doc[section]["max"])
      {
        doc[section]["max"] = String(tmpvalue);
      }
      else
      {
        if (tmpvalue > (long)doc[section]["max"])
        {
          doc[section]["max"] = String(tmpvalue);
        }
      }
    }
    // serializeJsonPretty(doc, Serial);
    //  Écrire les données dans le fichier
    file = LittleFS.open("/database/power_" + section + "_" + path, "w+");
    if (!file || file.isDirectory())
    {
      DEBUG_PRINTLN(F("Erreur lors de l'ouverture du fichier "));
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

bool ini_write(String path, String section, String key, String value)
{

  File file = LittleFS.open("/database/" + path, "r+");
  if (!file)
  {
    file = LittleFS.open("/database/" + path, "w+");
    DEBUG_PRINTLN(F("Erreur lors de l'ouverture du fichier "));
    if (!file)
    {
      DEBUG_PRINTLN(F("Impossible de créer le fichier (ini_write) "));
      return false;
    }
    // return false;
  }
  size_t filesize = file.size();

  DynamicJsonDocument doc(5096);

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
  doc[section][key] = value;

  // serializeJsonPretty(doc, Serial);
  //  Écrire les données dans le fichier
  file = LittleFS.open("/database/" + path, "w+");
  if (!file || file.isDirectory())
  {
    DEBUG_PRINTLN(F("Erreur lors de l'ouverture du fichier "));
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
  File file = LittleFS.open("/database/" + path, FILE_READ);
  if (!file || file.isDirectory())
  {
    DEBUG_PRINTLN(F("Erreur lors de l'ouverture du fichier ini_read"));
    file.close();
    return "Error";
  }

  // Analyser le contenu JSON du fichier
  DynamicJsonDocument doc(5096);
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
    return "Error";
  }

  DynamicJsonDocument doc(5096);
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
    DEBUG_PRINTLN(F("Erreur lors de l'ouverture du fichier "));
    if (!file)
    {
      DEBUG_PRINTLN(F("Impossible de créer le fichier (config_write) "));
      return false;
    }
  }
  size_t filesize = file.size();

  DynamicJsonDocument doc(5096);

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
    DEBUG_PRINTLN(F("Erreur lors de l'ouverture du fichier "));
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
