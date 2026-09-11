/**
 * wifi_ota.h - Wi-Fi (soft-AP + optional station) and the HTTP firmware-update page
 *
 *   http://192.168.4.1/          when connected to the board's own access point
 *   http://weather-display.local when the board joined your Wi-Fi (mDNS)
 *
 * Endpoints:
 *   GET  /              update page (embedded index.html)
 *   GET  /api/status    JSON status (same as the /status console command)
 *   POST /api/weather   JSON {"city":..,"temperature":..,"sky":..,...} - same fields as /set_weather
 *   POST /api/wifi      JSON {"ssid":..,"password":..}  (or {"clear":true})
 *   POST /api/brightness JSON {"level": 80}
 *   POST /update        raw firmware .bin body -> OTA -> reboot
 *   POST /reboot
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_AP_SSID_PREFIX   "WeatherDisplay"
#define WIFI_AP_PASSWORD      "weather1234"
#define WIFI_HOSTNAME         "weather-display"

esp_err_t wifi_ota_start(void);
esp_err_t wifi_ota_set_sta_credentials(const char *ssid, const char *password);
esp_err_t wifi_ota_clear_sta_credentials(void);

#ifdef __cplusplus
}
#endif
