# Restaura el firmware de fábrica completo (16 MB) en la Waveshare ESP32-S3-Touch-AMOLED-1.8.
# Uso: .\restore_backup.ps1 [COM9]

param([string]$Port = "COM9", [string]$Backup = "backup_full_16MB_20260911.bin")
$bin = Join-Path $PSScriptRoot $Backup
$expected = (Get-Content (Join-Path $PSScriptRoot "$Backup.sha256")).Split(" ")[0]
$actual = (Get-FileHash $bin -Algorithm SHA256).Hash.ToLower()
if ($actual -ne $expected) { Write-Error "El backup esta corrupto (SHA256 no coincide)"; exit 1 }
python -m esptool --port $Port --baud 921600 --chip esp32s3 --after hard-reset write-flash --flash-mode qio --flash-size 16MB 0x0 $bin
