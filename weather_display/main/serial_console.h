/**
 * serial_console.h - Command console on the native USB Serial/JTAG port
 *
 * Commands (arguments are "-key value" pairs, values may be quoted):
 *   /set_weather -city "Madrid" -temperature "32ºC" -sky "sunny" [-humidity 40] [-wind "12 km/h"]
 *                [-feels 34] [-min 18] [-max 34] [-pressure 1015] [-uv 7] [-desc "texto"]
 *   /set_time    -datetime "2026-09-11 18:45:00"  |  -epoch 1789000000
 *   /set_wifi    -ssid "MiRed" -password "secreto"  |  -clear
 *   /brightness  -level 80
 *   /status      (JSON with everything the device knows)
 *   /reboot
 *   /help
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t serial_console_start(void);

#ifdef __cplusplus
}
#endif
