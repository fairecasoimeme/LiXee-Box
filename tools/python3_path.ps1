# Python 3 en tete du PATH, avant l'activation d'ESP-IDF (export.ps1 appelle
# "python"). Si c'est un Python 2 qui repond a "python", le Python 3 du
# lanceur "py" passe devant ; sinon rien n'est change.
#
# Usage (dot-source) : . "$PSScriptRoot\tools\python3_path.ps1"

$pyver = ""
try { $pyver = & python -c "import sys; print(sys.version_info[0])" 2>$null } catch { }
if ($pyver -ne "3") {
    $py3 = ""
    try { $py3 = & py -3 -c "import sys; print(sys.executable)" 2>$null } catch { }
    if ($py3 -and (Test-Path $py3)) {
        $pydir = Split-Path -Parent $py3
        $env:PATH = "$pydir;$pydir\Scripts;$env:PATH"
        Write-Host "Python 3 utilise : $py3"
    } else {
        Write-Warning "Python 3 introuvable (ni 'python', ni 'py -3') : l'activation d'ESP-IDF risque d'echouer"
    }
}
