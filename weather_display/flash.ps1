# Flashea por USB (primera vez, o si la OTA queda inutilizable). Después basta con la página web.
# Uso: .\flash.ps1 [COM9] [-Monitor]
param([string]$Port = "COM9", [switch]$Monitor)
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ProjectRoot "env.ps1")
Set-Location $ProjectRoot

$args_ = @("-p", $Port, "-b", "921600", "flash")
if ($Monitor) { $args_ += "monitor" }
python "$env:IDF_PATH\tools\idf.py" @args_
exit $LASTEXITCODE
