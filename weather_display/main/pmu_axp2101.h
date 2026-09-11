/**
 * pmu_axp2101.h - Minimal driver for the AXP2101 power management unit (I2C 0x34)
 * Only the battery/charger status needed by the weather card is implemented.
 */
#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AXP2101_I2C_ADDR 0x34

esp_err_t pmu_axp2101_init(i2c_master_bus_handle_t bus);
esp_err_t pmu_axp2101_read(battery_status_t *out);

#ifdef __cplusplus
}
#endif
