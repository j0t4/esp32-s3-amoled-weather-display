#include "rtc_pcf85063.h"

#include <string.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "pcf85063";

#define REG_CTRL1    0x00
#define REG_SECONDS  0x04  /* bit7 = OS (oscillator stop) */

static i2c_master_dev_handle_t s_dev;
static bool s_present;

static uint8_t bcd2dec(uint8_t v) { return (uint8_t)(((v >> 4) * 10) + (v & 0x0F)); }
static uint8_t dec2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

esp_err_t rtc_pcf85063_init(i2c_master_bus_handle_t bus)
{
    if (!bus) {
        return ESP_ERR_INVALID_ARG;
    }
    if (i2c_master_probe(bus, PCF85063_I2C_ADDR, 100) != ESP_OK) {
        ESP_LOGW(TAG, "PCF85063A not found on I2C");
        return ESP_ERR_NOT_FOUND;
    }
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add device");
    /* Control_1: 24h mode, oscillator running, 12.5pF load cap (as on the Waveshare board) */
    uint8_t buf[2] = { REG_CTRL1, 0x01 };
    i2c_master_transmit(s_dev, buf, 2, 100);
    s_present = true;
    return ESP_OK;
}

esp_err_t rtc_pcf85063_get(struct tm *out)
{
    if (!s_present) {
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t reg = REG_SECONDS, d[7] = { 0 };
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, d, sizeof(d), 100), TAG, "read");
    if (d[0] & 0x80) {
        return ESP_ERR_INVALID_STATE; /* clock integrity not guaranteed */
    }
    memset(out, 0, sizeof(*out));
    out->tm_sec  = bcd2dec(d[0] & 0x7F);
    out->tm_min  = bcd2dec(d[1] & 0x7F);
    out->tm_hour = bcd2dec(d[2] & 0x3F);
    out->tm_mday = bcd2dec(d[3] & 0x3F);
    out->tm_wday = d[4] & 0x07;
    out->tm_mon  = bcd2dec(d[5] & 0x1F) - 1;
    out->tm_year = bcd2dec(d[6]) + 100;   /* RTC stores 00..99 -> 2000..2099 */
    out->tm_isdst = -1;
    if (out->tm_year < 124 || out->tm_mon < 0 || out->tm_mon > 11 || out->tm_mday < 1) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t rtc_pcf85063_set(const struct tm *in)
{
    if (!s_present) {
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t buf[8] = {
        REG_SECONDS,
        dec2bcd(in->tm_sec) & 0x7F,   /* also clears OS flag */
        dec2bcd(in->tm_min),
        dec2bcd(in->tm_hour),
        dec2bcd(in->tm_mday),
        (uint8_t)(in->tm_wday & 0x07),
        dec2bcd(in->tm_mon + 1),
        dec2bcd((in->tm_year + 1900) % 100),
    };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

esp_err_t rtc_pcf85063_sync_system_from_rtc(void)
{
    struct tm t;
    esp_err_t err = rtc_pcf85063_get(&t);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RTC time not valid (%s), system clock left unset", esp_err_to_name(err));
        return err;
    }
    time_t epoch = mktime(&t); /* RTC holds local time; mktime honours TZ */
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "System time set from RTC: %04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return ESP_OK;
}

esp_err_t rtc_pcf85063_sync_rtc_from_system(void)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year < 124) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = rtc_pcf85063_set(&t);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "RTC updated from system time");
    }
    return err;
}
