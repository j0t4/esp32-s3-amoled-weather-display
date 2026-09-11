/**
 * rtc_pcf85063.h - Minimal driver for the PCF85063A real time clock (I2C 0x51)
 * Keeps the wall-clock time across resets (backed by the board battery).
 */
#pragma once

#include <time.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCF85063_I2C_ADDR 0x51

esp_err_t rtc_pcf85063_init(i2c_master_bus_handle_t bus);
/* Reads the RTC as local broken-down time. Returns ESP_ERR_INVALID_STATE if the oscillator stopped (time unset). */
esp_err_t rtc_pcf85063_get(struct tm *out);
esp_err_t rtc_pcf85063_set(const struct tm *in);

/* Convenience: RTC -> system clock at boot, system clock -> RTC after /set_time or NTP sync */
esp_err_t rtc_pcf85063_sync_system_from_rtc(void);
esp_err_t rtc_pcf85063_sync_rtc_from_system(void);

#ifdef __cplusplus
}
#endif
