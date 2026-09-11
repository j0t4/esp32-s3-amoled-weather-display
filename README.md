# ESP32-S3 AMOLED Weather Display

Tarjeta del tiempo para la **Waveshare ESP32-S3-Touch-AMOLED-1.8** (pantalla AMOLED táctil de
1,8", 368×448). Los datos llegan por el puerto USB serie de la placa con un comando de texto:

```
/set_weather -city "Madrid" -temperature "32ºC" -sky "sunny"
```

Incluye un script que consulta el tiempo real (Open-Meteo, sin clave de API) y lo envía a la
pantalla, y una página web servida por la propia placa para actualizar el firmware por Wi-Fi (OTA).

> Firmware en ESP-IDF v5.5 + LVGL 9 usando el BSP oficial de Waveshare. El toolchain se instala
> **dentro del repositorio** con un script; no hace falta instalar ESP-IDF ni ningún IDE en el sistema.

## Qué muestra la pantalla

| Zona | Contenido |
|---|---|
| Barra superior | Hora (RTC PCF85063A, ajustable por serie o NTP), icono Wi-Fi, batería y carga (PMU AXP2101) |
| Cabecera | Ciudad y fecha en español |
| Centro | Icono del cielo dibujado con primitivas LVGL (sol, luna, nubes, lluvia, tormenta, nieve, niebla, viento), temperatura grande y unidad |
| Descripción | Estado del cielo en español y texto libre opcional |
| Tarjeta de detalles | Humedad, viento, sensación térmica, mín/máx, presión, índice UV |
| Pie | "Actualizado hace N min" (en naranja si los datos tienen más de 3 h) y la URL de la página OTA |

Estilo retro de terminal: fondo casi negro, fuente monoespaciada VT323 y un color de acento
(ámbar, cian, gris, azul...) que cambia según el cielo. Los datos se guardan en NVS y sobreviven al reinicio.

## Hardware

- **SoC**: ESP32-S3 (dual core 240 MHz), 16 MB flash QIO, 8 MB PSRAM octal
- **Pantalla**: AMOLED CO5300 368×448 por QSPI · **Táctil**: CST816S (I2C)
- **PMU**: AXP2101 · **RTC**: PCF85063A · **IMU**: QMI8658 · **Audio**: ES8311 (no usados: IMU y audio)
- **Conexión**: USB-Serial/JTAG nativo del ESP32-S3 (aparece como puerto COM; en este proyecto COM9)

La placa existe en dos revisiones (SH8601+FT3168 y CO5300+CST816S); el BSP detecta la revisión en
el arranque, así que el firmware funciona en ambas.

## Estructura del repositorio

```
.
├── README.md                ← este archivo
├── setup_tools.ps1          instala ESP-IDF v5.5.4 + toolchain xtensa en ./tools y descarga los componentes
├── restore_backup.ps1       reflashea un volcado completo de la flash (ver "Copia de seguridad")
├── .gitignore
├── tools/                   (no versionado) esp-idf/ y .espressif/ creados por setup_tools.ps1
└── weather_display/         proyecto ESP-IDF
    ├── main/                fuentes C, fuentes LVGL con Latin-1, página web incrustada
    ├── partitions.csv       nvs · otadata · phy · ota_0 (6 MB) · ota_1 (6 MB) · storage
    ├── sdkconfig.defaults   16 MB QIO, PSRAM octal, consola por USB-Serial/JTAG, rollback OTA
    ├── env.ps1 · build.ps1 · flash.ps1 · monitor.ps1
    ├── send_weather.py      envía comandos por el puerto serie desde el PC
    ├── update_weather.py    tiempo real de Open-Meteo → pantalla
    └── README.md            detalles del firmware, comandos y API HTTP
```

## Puesta en marcha desde cero

Requisitos en el PC: **Windows 10/11**, **Python 3.10+** y **git**. Nada más.

```powershell
git clone https://github.com/j0t4/esp32-s3-amoled-weather-display.git
cd esp32-s3-amoled-weather-display
.\setup_tools.ps1            # ~2 GB: clona ESP-IDF, instala el toolchain y descarga los componentes
cd weather_display
.\build.ps1                  # genera build\weather_display.bin
.\flash.ps1 COM9 -Monitor    # flashea por USB y abre el monitor serie (Ctrl+] para salir)
```

`setup_tools.ps1` es idempotente: si algo ya está instalado lo omite; con `-Force` lo reinstala.

## Uso diario

```powershell
cd weather_display
python update_weather.py --set-time          # tiempo real de Madrid + hora del PC → pantalla
python update_weather.py --interval 15       # actualizar cada 15 minutos
python send_weather.py --port COM9 --city Madrid --temperature "32ºC" --sky sunny   # datos manuales
python send_weather.py --port COM9 --status  # JSON con firmware, hora, tiempo, batería y red
```

Cualquier terminal serie a 115200 baudios sirve también; la consola responde `OK ...`, `ERR ...` o
`WARN ...` a cada comando. Lista completa de comandos y valores de `-sky` en
[weather_display/README.md](weather_display/README.md).

## Actualización de firmware por Wi-Fi

La placa levanta un punto de acceso `WeatherDisplay-XXXX` (contraseña por defecto `weather1234`,
definida en `main/wifi_ota.h`; cámbiala antes de usarla fuera de un entorno de pruebas). En
**http://192.168.4.1/** se puede:

- subir `build/weather_display.bin` con barra de progreso; la imagen se valida antes de escribir,
  la placa reinicia sola y si el nuevo firmware no arranca vuelve al anterior (*rollback*);
- ver el firmware en ejecución y el estado del dispositivo;
- enviar datos del tiempo con un formulario, configurar la Wi-Fi doméstica, ajustar brillo y reiniciar.

Si se configura una red Wi-Fi (`/set_wifi` o desde la web), la placa también se conecta a ella,
sincroniza la hora por NTP y queda accesible en **http://weather-display.local/**.

## Copia de seguridad del firmware de fábrica

Antes de flashear por primera vez conviene guardar la flash original (16 MB):

```powershell
python -m esptool --port COM9 --baud 921600 read-flash 0 0x1000000 backup_full_16MB.bin
```

`restore_backup.ps1 COM9` la restaura íntegra (bootloader, particiones, apps y datos). Los volcados
`.bin` están excluidos del repositorio: contienen la NVS del dispositivo (credenciales Wi-Fi, etc.).

## Licencia y créditos

Código propio bajo licencia MIT. Usa ESP-IDF (Apache-2.0), LVGL (MIT), el BSP
`waveshare/esp32_s3_touch_amoled_1_8` (Apache-2.0) y datos meteorológicos de
[Open-Meteo](https://open-meteo.com/) (CC BY 4.0). La fuente de pantalla es
[VT323](https://fonts.google.com/specimen/VT323) (OFL), convertida a bitmaps LVGL con `lv_font_conv`.
