# Weather Display · Waveshare ESP32-S3-Touch-AMOLED-1.8

Firmware ESP-IDF que muestra una tarjeta del tiempo (Madrid por defecto) en la pantalla AMOLED
de 368×448. Los datos llegan por el puerto USB serie de la placa (COM9) con el comando:

```
/set_weather -city "Madrid" -temperature "32ºC" -sky "sunny"
```

Además de ciudad, temperatura y cielo, la tarjeta muestra hora y fecha (RTC PCF85063A, ajustable
por serie o por NTP), nivel de batería (PMU AXP2101), humedad, viento, sensación térmica, mínima
y máxima, presión, índice UV, hace cuánto se actualizó y la dirección de la página web de
mantenimiento. Los datos se guardan en NVS y sobreviven a un reinicio.

## Estructura

```
weather_display/
├── main/
│   ├── main.c              arranque: NVS, pantalla (BSP), sensores I2C, UI, Wi-Fi, consola
│   ├── app_state.[ch]      estado compartido + persistencia NVS + parseo de -temperature / -sky
│   ├── weather_ui.[ch]     tarjeta LVGL 9 (fondo degradado según el cielo, tarjeta de detalles)
│   ├── weather_icons.[ch]  iconos sol/nube/lluvia/tormenta/nieve/niebla/luna/viento con primitivas
│   ├── serial_console.[ch] comandos /set_weather /set_time /set_wifi /brightness /status /reboot
│   ├── wifi_ota.[ch]       punto de acceso + estación, servidor HTTP, subida OTA, mDNS, SNTP
│   ├── status_json.[ch]    JSON de estado (firmware, hora, tiempo, batería, red)
│   ├── pmu_axp2101.[ch]    batería / carga (I2C 0x34)
│   ├── rtc_pcf85063.[ch]   reloj de tiempo real (I2C 0x51)
│   ├── fonts/              Montserrat 14/18/26/72 con Latin-1 (acentos, º, °) generadas con lv_font_conv
│   ├── www/index.html      página web de actualización de firmware (se incrusta en el binario)
│   └── idf_component.yml   dependencias: BSP waveshare/esp32_s3_touch_amoled_1_8, LVGL 9, mDNS
├── partitions.csv          nvs, otadata, phy, ota_0 (6 MB), ota_1 (6 MB), storage
├── sdkconfig.defaults      16 MB QIO, PSRAM octal, consola por USB-Serial/JTAG, rollback OTA
├── env.ps1                 carga el ESP-IDF instalado en ../tools (sin instalación global)
├── build.ps1               compila  →  build/weather_display.bin
├── flash.ps1               flashea por USB (primera vez):  .\flash.ps1 COM9 -Monitor
├── monitor.ps1             monitor serie interactivo
├── send_weather.py         envía comandos desde el PC (pyserial)
└── update_weather.py       consulta el tiempo REAL (Open-Meteo, sin clave) y lo envía a la pantalla
```

El compilador (xtensa-esp-elf), cmake, ninja y ESP-IDF v5.5.4 están en `../tools/` dentro del
repositorio; no hace falta instalar nada más aparte de Python 3 y git.

## Compilar y flashear

```powershell
cd C:\DES\esp32\waveshare_esp32_s3\weather_display
.\build.ps1                 # primera vez descarga los componentes gestionados (BSP, LVGL...)
.\flash.ps1 COM9 -Monitor   # flashea bootloader + tabla de particiones + app y abre el monitor
```

Para volver al firmware de fábrica: `..\restore_backup.ps1 COM9`.

## Tiempo real con un solo comando

`update_weather.py` pide a Open-Meteo el tiempo actual de la ciudad, lo traduce al comando
`/set_weather` (cielo, humedad, viento con dirección, sensación, mín/máx del día, presión a nivel
del mar, índice UV máximo y descripción en español) y lo envía por el puerto serie:

```powershell
python update_weather.py                     # Madrid, una vez
python update_weather.py --interval 15       # cada 15 minutos hasta Ctrl+C
python update_weather.py --set-time          # además sincroniza la hora del PC en la pantalla
python update_weather.py --city Sevilla --dry-run   # otra ciudad, sólo muestra el comando
```

Para dejarlo funcionando de forma permanente basta una tarea programada de Windows que ejecute
`python update_weather.py --set-time` cada 15 minutos, o dejar abierta una consola con `--interval 15`.

## Enviar el tiempo por USB serie

Cualquier terminal serie a 115200 baudios sirve (el monitor de ESP-IDF, PuTTY, etc.), o el
script incluido:

```powershell
python send_weather.py --port COM9 --city Madrid --temperature "32ºC" --sky sunny
python send_weather.py --port COM9 --city Madrid --temperature 27 --sky "partly cloudy" `
      --humidity 35 --wind "14 km/h SO" --min 19 --max 33 --pressure 1016 --uv 8 --desc "Tarde despejada"
python send_weather.py --port COM9 --set-time      # pone la hora del PC en el dispositivo y en el RTC
python send_weather.py --port COM9 --status        # JSON con todo el estado
```

Comandos disponibles (cada uno responde con una línea `OK ...`, `ERR ...` o `WARN ...`):

| Comando | Parámetros |
|---|---|
| `/set_weather` | `-city` `-temperature` `-sky` obligatorios la primera vez; opcionales `-humidity` `-wind` `-feels` `-min` `-max` `-pressure` `-uv` `-desc`. Los campos no enviados conservan el valor anterior. |
| `/set_time` | `-datetime "2026-09-11 18:45:00"` (hora local de Madrid) o `-epoch N` |
| `/set_wifi` | `-ssid "MiRed" -password "secreto"`, o `-clear` |
| `/brightness` | `-level 0..100` |
| `/status` | — |
| `/reboot`, `/help` | — |

Valores de `-sky` reconocidos (inglés o español): `sunny`/`clear`, `night`, `partly cloudy`,
`cloudy`, `rain`, `storm`, `snow`, `fog`, `windy` · `soleado`, `despejado`, `nublado`, `lluvia`,
`tormenta`, `nieve`, `niebla`, `viento`. La temperatura acepta `32ºC`, `32°C`, `32`, `89F`.

## Página web de actualización de firmware

La placa levanta siempre un punto de acceso Wi-Fi:

- SSID `WeatherDisplay-XXXX` (XXXX = final de la MAC), contraseña `weather1234`
- Página: **http://192.168.4.1/** (la dirección también aparece en el pie de la pantalla)

Si se configura una red con `/set_wifi` (o desde la propia página), además se conecta a ella y
queda accesible en **http://weather-display.local/** o en la IP que muestre la pantalla.

Desde la página se puede: subir `build/weather_display.bin` (barra de progreso, verificación de
la imagen, reinicio automático y *rollback* si el nuevo firmware no arranca), ver el firmware en
ejecución y el estado del dispositivo, enviar datos del tiempo con un formulario, configurar la
Wi-Fi, ajustar el brillo y reiniciar.

API HTTP: `GET /api/status`, `POST /api/weather`, `POST /api/wifi`, `POST /api/brightness`,
`POST /update` (cuerpo = .bin), `POST /reboot`.
