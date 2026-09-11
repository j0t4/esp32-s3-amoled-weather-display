# Compila el firmware. Resultado: build\weather_display.bin (el archivo que se sube por la página OTA).
# Uso: .\build.ps1 [clean|menuconfig|size]
param([string]$Target = "build")
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ProjectRoot "env.ps1")
Set-Location $ProjectRoot

switch ($Target) {
    "clean"      { python "$env:IDF_PATH\tools\idf.py" fullclean }
    "menuconfig" { python "$env:IDF_PATH\tools\idf.py" menuconfig }
    "size"       { python "$env:IDF_PATH\tools\idf.py" size }
    default      {
        python "$env:IDF_PATH\tools\idf.py" set-target esp32s3 --preview 2>$null | Out-Null
        python "$env:IDF_PATH\tools\idf.py" build
        if ($LASTEXITCODE -eq 0) {
            $bin = Join-Path $ProjectRoot "build\weather_display.bin"
            Write-Host "`nFirmware OTA: $bin ($([math]::Round((Get-Item $bin).Length/1KB)) KB)" -ForegroundColor Green
        }
    }
}
exit $LASTEXITCODE
