#include "pmu_axp2101.h"

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "axp2101";

/* Register map (subset), see AXP2101 datasheet / XPowersLib */
#define REG_STATUS1        0x00  /* bit5: VBUS good, bit3: battery present */
#define REG_STATUS2        0x01  /* bits[7:5]: 001 charging, 010 discharging, 000 standby */
#define REG_IC_TYPE        0x03  /* expected 0x4A (low nibble 0xA) */
#define REG_ADC_CH_CTRL    0x30  /* bit0: enable VBAT ADC */
#define REG_VBAT_H         0x34  /* 5 high bits */
#define REG_VBAT_L         0x35  /* 8 low bits  -> mV */
#define REG_BAT_PERCENT    0xA4

static i2c_master_dev_handle_t s_dev;
static bool s_present;

static esp_err_t rd(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

esp_err_t pmu_axp2101_init(i2c_master_bus_handle_t bus)
{
    if (!bus) {
        return ESP_ERR_INVALID_ARG;
    }
    if (i2c_master_probe(bus, AXP2101_I2C_ADDR, 100) != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 not found on I2C");
        return ESP_ERR_NOT_FOUND;
    }
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add device");

    uint8_t id = 0;
    ESP_RETURN_ON_ERROR(rd(REG_IC_TYPE, &id), TAG, "read id");
    ESP_LOGI(TAG, "AXP2101 IC type 0x%02X", id);

    /* Enable battery voltage ADC */
    uint8_t adc = 0;
    if (rd(REG_ADC_CH_CTRL, &adc) == ESP_OK) {
        wr(REG_ADC_CH_CTRL, adc | 0x01);
    }
    s_present = true;
    return ESP_OK;
}

esp_err_t pmu_axp2101_read(battery_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->percent = -1;
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t st1 = 0, st2 = 0;
    ESP_RETURN_ON_ERROR(rd(REG_STATUS1, &st1), TAG, "status1");
    ESP_RETURN_ON_ERROR(rd(REG_STATUS2, &st2), TAG, "status2");
    out->present = true;
    out->usb_power = (st1 & (1 << 5)) != 0;
    out->battery_present = (st1 & (1 << 3)) != 0;
    out->charging = ((st2 >> 5) & 0x07) == 0x01;

    if (out->battery_present) {
        uint8_t p = 0, h = 0, l = 0;
        if (rd(REG_BAT_PERCENT, &p) == ESP_OK && p <= 100) {
            out->percent = p;
        }
        if (rd(REG_VBAT_H, &h) == ESP_OK && rd(REG_VBAT_L, &l) == ESP_OK) {
            out->voltage_mv = ((h & 0x1F) << 8) | l;
        }
    }
    return ESP_OK;
}
