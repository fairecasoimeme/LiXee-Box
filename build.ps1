# Clear MSys env vars that interfere with ESP-IDF
Remove-Item Env:\MSYSTEM -ErrorAction SilentlyContinue
Remove-Item Env:\MSYS -ErrorAction SilentlyContinue
Remove-Item Env:\MINGW_PREFIX -ErrorAction SilentlyContinue
Remove-Item Env:\SHELL -ErrorAction SilentlyContinue

$env:IDF_PATH = "C:\esp\v5.5.2\esp-idf"

# Source the export script
& "$env:IDF_PATH\export.ps1"

# Navigate to project
Set-Location $PSScriptRoot

# Build
idf.py build
