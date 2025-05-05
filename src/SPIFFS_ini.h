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


#ifndef _SPIFFS_ini_
#define _SPIFFS_ini_

#include "Arduino.h"
#include <string.h>
#include <FS.h>
#include <LittleFS.h>
#include "config.h"

bool ini_exist(String path);
bool ini_writes(String path, WriteIni i,bool create);
DeviceInfo getDeviceInfo(String path);


bool ini_write(String path, String section, String key, String value);
bool copyFile(String srcPath) ;
bool config_write(String path, String key, String value);
String config_read(String path,String key);
String ini_read (String path, String section, String key);
bool ini_energy(String path, String section, String value);
bool ini_power(String path, String section, String value);
bool ini_power2(String path, String section, String value);
bool ini_trendPower(String path, String section, String value);
bool ini_trendEnergy(String path, String section, String value);

//bool init_raz_energy(String path, String time);
//bool init_raz_energy(DeviceData* device, const String &periodStr);

void scanFilesError(void);

File safeOpenFile(const char *path, const char *mode);
void safeCloseFile(File &file, const char *path);

/*bool   ini_open (String ini_name);
String ini_read (String section, String key, String def);
bool   ini_write(String section, String key, String value);
bool   ini_delete_key(String section, String key);
bool   ini_delete_section(String section);
bool   ini_close();*/

#endif // _SPIFFS_ini_
