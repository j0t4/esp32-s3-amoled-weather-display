# setup_tools.ps1 - Instala todo lo necesario para compilar el firmware DENTRO del repositorio.
#
#   tools\esp-idf\      ESP-IDF (clon superficial de la etiqueta indicada, con submódulos)
#   tools\.espressif\   toolchain xtensa-esp-elf, cmake, ninja, openocd, esptool y el venv de Python
#   weather_display\managed_components\   componentes del registro (BSP Waveshare, LVGL, mDNS...)
#
# Requisitos: Windows, git, Python 3.10+ en el PATH. Ejecutar desde PowerShell (no desde Git Bash).
# Uso:  .\setup_tools.ps1 [-IdfVersion v5.5.4] [-Force] [-SkipComponents]
param(
    [string]$IdfVersion = "v5.5.4",
    [switch]$Force,           # borra tools\ y reinstala todo
    [switch]$SkipComponents   # no lanza la descarga de componentes gestionados
)

$ErrorActionPreference = "Stop"
$Root      = Split-Path -Parent $MyInvocation.MyCommand.Path
$ToolsDir  = Join-Path $Root "tools"
$IdfPath   = Join-Path $ToolsDir "esp-idf"
$IdfTools  = Join-Path $ToolsDir ".espressif"
$Project   = Join-Path $Root "weather_display"

function Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }
function Need($cmd, $hint) {
    if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) { throw "Falta '$cmd'. $hint" }
}

Need git    "Instala Git para Windows: https://git-scm.com/download/win"
Need python "Instala Python 3.10+ desde https://www.python.org/downloads/ (marca 'Add to PATH')"
$pyver = (python -c "import sys;print('%d.%d'%sys.version_info[:2])")
if ([version]$pyver -lt [version]"3.10") { throw "Python $pyver es demasiado antiguo (mínimo 3.10)" }
if ($env:MSYSTEM) { throw "Ejecuta este script desde PowerShell, no desde Git Bash/MSYS" }

if ($Force -and (Test-Path $ToolsDir)) {
    Step "Eliminando instalación anterior en $ToolsDir"
    Remove-Item -Recurse -Force $ToolsDir
}
New-Item -ItemType Directory -Force $ToolsDir | Out-Null

# ---------------------------------------------------------------- ESP-IDF
if (Test-Path (Join-Path $IdfPath "tools\idf.py")) {
    $have = (git -C $IdfPath describe --tags --exact-match 2>$null)
    Step "ESP-IDF ya presente ($have) en $IdfPath"
} else {
    Step "Clonando ESP-IDF $IdfVersion (superficial, con submódulos) ..."
    git clone -b $IdfVersion --depth 1 --recursive --shallow-submodules -j 8 `
        https://github.com/espressif/esp-idf.git $IdfPath
    if ($LASTEXITCODE -ne 0) { throw "git clone falló" }
}

# ---------------------------------------------------------------- Toolchain + venv Python
$env:IDF_PATH          = $IdfPath
$env:IDF_TOOLS_PATH    = $IdfTools
$env:IDF_GITHUB_ASSETS = "dl.espressif.com/github_assets"   # mirror rápido para las descargas

Step "Instalando toolchain para esp32s3 en $IdfTools ..."
python "$IdfPath\tools\idf_tools.py" --non-interactive install --targets=esp32s3
if ($LASTEXITCODE -ne 0) { throw "idf_tools.py install falló" }

Step "Creando el entorno Python de ESP-IDF ..."
python "$IdfPath\tools\idf_tools.py" --non-interactive install-python-env
if ($LASTEXITCODE -ne 0) { throw "idf_tools.py install-python-env falló" }

Step "Instalando pyserial y esptool en el Python del sistema (para send_weather.py / update_weather.py)"
python -m pip install --quiet --upgrade pyserial esptool

# ---------------------------------------------------------------- Componentes gestionados
if (-not $SkipComponents) {
    Step "Descargando componentes gestionados (BSP Waveshare, LVGL, mDNS...) ..."
    . (Join-Path $IdfPath "export.ps1") | Out-Null
    Push-Location $Project
    try {
        python "$IdfPath\tools\idf.py" set-target esp32s3 --preview 2>$null | Out-Null
        python "$IdfPath\tools\idf.py" reconfigure
        if ($LASTEXITCODE -ne 0) { throw "idf.py reconfigure falló (¿sin conexión al registro de componentes?)" }
    } finally { Pop-Location }
}

Step "Listo."
Write-Host @"
Siguientes pasos:
  cd weather_display
  .\build.ps1                 # compila -> build\weather_display.bin
  .\flash.ps1 COM9 -Monitor   # flashea por USB
  python update_weather.py --set-time
"@
