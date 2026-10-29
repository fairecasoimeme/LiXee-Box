# Flash de l'application + mise a jour des fichiers livres (data/web, data/tp)
# dans LittleFS, en CONSERVANT le reste de la partition de la carte :
# configuration, base Zigbee (zigbee_devices.json, db/), historiques (hst/).
#
# La carte doit etre en mode telechargement (BOOT maintenu, RESET, relacher BOOT).
# Apres "FLASH OK", appuyer sur RESET pour demarrer.
#
# Adresses de LittleFS, de l'application et d'otadata : lues dans
# partitions/partitions.csv (table de la LiXee-Box v2.x : LittleFS dans la
# partition "spiffs").
#
# Usage : powershell -ExecutionPolicy Bypass -File tools\flash_keep_data.ps1 [-Port COM4]
#
# Changement de table des partitions (migration) : relire les donnees a leur
# ANCIENNE adresse, ecrire la nouvelle table et remettre a zero la table de
# demarrage OTA (demarrage sur ota_0, ou l'application est ecrite) :
#   ... flash_keep_data.ps1 -FromOffset 0x827000 -PartitionTable
# (0x827000 : LittleFS de l'ancienne table du pont). La NVS (0x9000 :
# appairage Matter, WiFi) n'est jamais ecrite ; mais la table v2.x la reduit
# de 32 a 20 Ko : ce qui etait au-dela peut etre perdu (Matter a reappairer).
param(
    [string]$Port = "COM4",
    [string]$FromOffset = "",       # adresse actuelle de LittleFS sur la carte (defaut : celle du CSV)
    [switch]$PartitionTable         # ecrire aussi la table des partitions et ota_data_initial
)

Remove-Item Env:\MSYSTEM -ErrorAction SilentlyContinue; Remove-Item Env:\MSYS -ErrorAction SilentlyContinue
Remove-Item Env:\MINGW_PREFIX -ErrorAction SilentlyContinue; Remove-Item Env:\SHELL -ErrorAction SilentlyContinue
$env:IDF_PATH = "C:\esp\v5.5.2\esp-idf"
& "$env:IDF_PATH\export.ps1" *> $null
# Apres l'activation d'ESP-IDF, qui ecrit sur stderr ; les commandes natives
# sont verifiees par $LASTEXITCODE
$ErrorActionPreference = "Stop"

$proj  = Split-Path -Parent $PSScriptRoot
$bkdir = "$proj\build\flash_backups"
$lfs   = "$proj\build\littlefs_py_venv\Scripts\littlefs-python.exe"
$work  = "$proj\build\flash_work"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"

# Partitions de la table du projet : colonnes (offset, taille) d'une ligne
function Get-Partition([string]$pattern, [string]$what) {
    $row = Get-Content "$proj\partitions\partitions.csv" | Where-Object { $_ -match $pattern } | Select-Object -First 1
    if (-not $row) { throw "partition $what absente de partitions/partitions.csv" }
    $cols = $row.Split(',') | ForEach-Object { $_.Trim() }
    return @($cols[3], $cols[4])
}
$lfsOff, $lfsSize = Get-Partition '^\s*(spiffs|littlefs)\s*,' "LittleFS"
$appOff = (Get-Partition '^\s*\w+\s*,\s*app\s*,\s*ota_0\s*,' "ota_0")[0]
$otaOff = (Get-Partition '^\s*otadata\s*,' "otadata")[0]
$readOff = if ($FromOffset) { $FromOffset } else { $lfsOff }
"LittleFS : lecture a $readOff, ecriture a $lfsOff ($lfsSize octets)"

New-Item -ItemType Directory -Force $bkdir | Out-Null
if (Test-Path $work) { Remove-Item -Recurse -Force $work -Confirm:$false }
New-Item -ItemType Directory -Force "$work\tree" | Out-Null

# 1. Lecture de la partition LittleFS actuelle (sauvegarde horodatee)
python -m esptool --chip esp32s3 -p $Port -b 921600 --before no_reset --after no_reset read_flash $readOff $lfsSize "$work\live.bin"
if ($LASTEXITCODE -ne 0) { throw "read_flash a echoue (carte en mode telechargement ?)" }
Copy-Item "$work\live.bin" "$bkdir\littlefs_live_$stamp.bin"

# 2. Extraction, remplacement des fichiers livres, reconstruction
& $lfs extract "$work\live.bin" "$work\tree" --block-size=4096 --name-max=64
if ($LASTEXITCODE -ne 0) { throw "extraction LittleFS impossible" }
# Fichiers web compresses a jour (web_src -> data/web/*.gz)
python "$proj\tools\build_web.py"
if ($LASTEXITCODE -ne 0) { throw "compression des fichiers web impossible" }
# web/ ne contient que des fichiers livres : remplace en entier, ce qui retire
# les anciennes copies non compressees et les fichiers abandonnes. tp/ est
# complete seulement (des templates peuvent avoir ete crees sur la carte).
if (Test-Path "$work\tree\web") { Remove-Item -Recurse -Force "$work\tree\web" -Confirm:$false }
foreach ($d in @("web", "tp")) {
    New-Item -ItemType Directory -Force "$work\tree\$d" | Out-Null
    Copy-Item "$proj\data\$d\*" "$work\tree\$d" -Recurse -Force
}
& $lfs create "$work\tree" "$work\new.bin" --fs-size=$lfsSize --name-max=64 --block-size=4096
if ($LASTEXITCODE -ne 0) { throw "creation de l'image LittleFS impossible" }
$listing = & $lfs list "$work\new.bin" --block-size=4096 --name-max=64
"Fichiers de donnees conserves :"
$listing | Where-Object { $_ -match "zigbee_devices|/config/|/db/" }
if (-not ($listing -match "zigbee_devices")) { throw "base Zigbee absente de la nouvelle image : flash annule" }

# 3. Ecriture (table des partitions si demandee) + application + LittleFS
$images = @()
if ($PartitionTable) {
    $images += @("0x8000", "$proj\build\partition_table\partition-table.bin",
                 $otaOff, "$proj\build\ota_data_initial.bin")
}
$images += @($appOff, "$proj\build\lixee_matter_bridge.bin", $lfsOff, "$work\new.bin")
python -m esptool --chip esp32s3 -p $Port -b 921600 --before no_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB @images
if ($LASTEXITCODE -ne 0) { throw "write_flash a echoue" }
"FLASH OK - appuyer sur RESET"
