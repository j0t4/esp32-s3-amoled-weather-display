# Abre el monitor serie de ESP-IDF (Ctrl+] para salir). Permite teclear los comandos /set_weather ...
param([string]$Port = "COM9")
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ProjectRoot "env.ps1")
Set-Location $ProjectRoot
python "$env:IDF_PATH\tools\idf.py" -p $Port monitor
