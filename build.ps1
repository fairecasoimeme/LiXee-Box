# Build du firmware (ESP-IDF v5.5.2, cible esp32s3)
#
# Usage : powershell -ExecutionPolicy Bypass -File build.ps1
#
# Contournements Windows, appliques seulement si necessaire :
#  - Python 3 : l'activation d'ESP-IDF appelle "python" ; si c'est un Python 2
#    qui est en tete du PATH, le Python 3 du lanceur "py" passe devant.
#  - Chemins longs desactives (LongPathsEnabled != 1) : les fichiers
#    d'esp-matter (connectedhomeip) depassent 260 caracteres. Le cache des
#    composants passe dans un dossier court (IDF_COMPONENT_CACHE_PATH, defaut
#    C:\idfc) et build\ devient une jonction vers un dossier court (defaut
#    C:\lxb, ou $env:LIXEE_SHORT_BUILD) : idf.py compile dans le chemin reel,
#    les scripts qui lisent build\ (tools\flash_keep_data.ps1) sont inchanges.

# Clear MSys env vars that interfere with ESP-IDF
Remove-Item Env:\MSYSTEM -ErrorAction SilentlyContinue
Remove-Item Env:\MSYS -ErrorAction SilentlyContinue
Remove-Item Env:\MINGW_PREFIX -ErrorAction SilentlyContinue
Remove-Item Env:\SHELL -ErrorAction SilentlyContinue

$env:IDF_PATH = "C:\esp\v5.5.2\esp-idf"

# --- Python 3 en tete du PATH ---
. "$PSScriptRoot\tools\python3_path.ps1"

# --- Chemins longs desactives : cache et build dans des dossiers courts ---
$longPaths = 0
try {
    $longPaths = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -Name LongPathsEnabled -ErrorAction Stop).LongPathsEnabled
} catch { }
if ($longPaths -ne 1) {
    if (-not $env:IDF_COMPONENT_CACHE_PATH) { $env:IDF_COMPONENT_CACHE_PATH = "$env:SystemDrive\idfc" }
    $shortBuild = if ($env:LIXEE_SHORT_BUILD) { $env:LIXEE_SHORT_BUILD } else { "$env:SystemDrive\lxb" }
    $buildLink = Join-Path $PSScriptRoot "build"
    $item = Get-Item $buildLink -Force -ErrorAction SilentlyContinue
    if (-not $item) {
        New-Item -ItemType Directory -Force $shortBuild | Out-Null
        New-Item -ItemType Junction -Path $buildLink -Target $shortBuild | Out-Null
        Write-Host "build\ -> $shortBuild (chemins longs desactives)"
    } elseif (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        Write-Warning ("build\ est un vrai dossier : avec les chemins longs desactives, esp-matter risque " +
                       "de ne pas compiler. Le supprimer pour qu'il soit remplace par une jonction vers $shortBuild.")
    }
    Write-Host "Cache des composants : $env:IDF_COMPONENT_CACHE_PATH"
}

# Source the export script
& "$env:IDF_PATH\export.ps1"

# Navigate to project
Set-Location $PSScriptRoot

# Build
idf.py build
exit $LASTEXITCODE
