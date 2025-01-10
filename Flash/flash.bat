@echo off
xcopy "..\.pio\build\esp32dev\firmware.bin" "BinFiles\" /v /f /y
xcopy "..\.pio\build\esp32dev\partitions.bin" "BinFiles\" /v /f /y
xcopy "..\.pio\build\esp32dev\bootloader.bin" "BinFiles\" /v /f /y
xcopy "..\.pio\build\esp32dev\littlefs.bin" "BinFiles\" /v /f /y
xcopy "%userprofile%\.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin" "BinFiles\" /v /f /y
pause
@echo off
echo.
echo Available COM ports: 
echo.
wmic path win32_pnpentity get caption /format:table| find "COM" 
echo.
SET /P portNr="Which COM port should be used? Please enter the number: "
echo.
SET COMport=COM%PortNr%
echo Using %COMport% ...
echo.
esptool.exe --chip esp32s3 --port %COMport% --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0000 BinFiles\bootloader.bin 0x8000 BinFiles\partitions.bin 0xe000 BinFiles\boot_app0.bin 0x10000 BinFiles\firmware.bin 0x910000 BinFiles\littlefs.bin
pause