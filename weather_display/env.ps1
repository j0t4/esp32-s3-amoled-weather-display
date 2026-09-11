# Carga el entorno ESP-IDF instalado dentro del proyecto (tools/esp-idf + tools/.espressif).
# Uso:  . .\env.ps1     (con el punto delante, para que las variables queden en la sesión)
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot    = Split-Path -Parent $ProjectRoot

$env:IDF_PATH       = Join-Path $RepoRoot "tools\esp-idf"
$env:IDF_TOOLS_PATH = Join-Path $RepoRoot "tools\.espressif"
$env:IDF_GITHUB_ASSETS = "dl.espressif.com/github_assets"

if (-not (Test-Path $env:IDF_PATH)) { Write-Error "No se encuentra ESP-IDF en $env:IDF_PATH"; return }

# export.ps1 añade el toolchain xtensa, cmake, ninja y el venv de Python al PATH de esta sesión
. (Join-Path $env:IDF_PATH "export.ps1") | Out-Null
Write-Host "ESP-IDF listo: $env:IDF_PATH" -ForegroundColor Green
