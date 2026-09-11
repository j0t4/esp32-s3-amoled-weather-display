/**
 * Weather Display - Waveshare ESP32-S3-Touch-AMOLED-1.8
 *
 * Shows a weather card for a city on the 368x448 AMOLED. Data arrives on the native
 * USB Serial/JTAG port (COM port) with:
 *
 *     /set_weather -city "Madrid" -temperature "32ºC" -sky "sunny"
 *
 * Extras: clock (RTC PCF85063A + NTP), battery (AXP2101), Wi-Fi access point with a web page
 * to upload new firmware (OTA), persistent data in NVS.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"

#include "bsp/esp-bsp.h"
#include "lvgl.h"

#include "app_state.h"
#include "weather_ui.h"
#include "serial_console.h"
#include "wifi_ota.h"
#include "pmu_axp2101.h"
#include "rtc_pcf85063.h"
#include "ota_progress.h"
#include "display_ctl.h"

static const char *TAG = "main";

/* Europe/Madrid: CET/CEST with EU daylight-saving rules */
#define TZ_MADRID "CET-1CEST,M3.5.0,M10.5.0/3"

atomic_int g_ota_progress = -1;
static atomic_bool s_ui_dirty = true;
static bool s_pmu_ok;

/* ----------------------------------------------------------------------- */
static void on_state_change(void)
{
    atomic_store(&s_ui_dirty, true);
}

/* Runs inside the LVGL task every second: no extra locking needed */
static void ui_timer_cb(lv_timer_t *timer)
{
    if (atomic_exchange(&s_ui_dirty, false)) {
        weather_ui_refresh();
    }
    weather_ui_tick();
}

void display_ctl_set_brightness(int percent)
{
    app_state_set_brightness(percent);
    if (bsp_display_lock(500)) {
        bsp_display_brightness_set(app_state_get_brightness());
        bsp_display_unlock();
    }
}

static void battery_task(void *arg)
{
    for (;;) {
        battery_status_t b;
        if (s_pmu_ok && pmu_axp2101_read(&b) == ESP_OK) {
            app_state_set_battery(&b);
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

/* ----------------------------------------------------------------------- */
void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "%s v%s (%s %s)", app->project_name, app->version, app->date, app->time);

    setenv("TZ", TZ_MADRID, 1);
    tzset();

    init_nvs();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(app_state_init());

    /* Display + touch + LVGL task (BSP) */
    esp_log_level_set("i2c.master", ESP_LOG_NONE);   /* silence the pull-up warning of the BSP */
    lv_display_t *disp = bsp_display_start();
    esp_log_level_set("i2c.master", ESP_LOG_WARN);
    if (!disp) {
        ESP_LOGE(TAG, "No se pudo iniciar la pantalla");
        return;
    }
    bsp_display_brightness_set(app_state_get_brightness());

    /* Sensors sharing the BSP I2C bus */
    i2c_master_bus_handle_t i2c = bsp_i2c_get_handle();
    s_pmu_ok = pmu_axp2101_init(i2c) == ESP_OK;
    if (rtc_pcf85063_init(i2c) == ESP_OK) {
        rtc_pcf85063_sync_system_from_rtc();
    }
    if (s_pmu_ok) {
        battery_status_t b;
        if (pmu_axp2101_read(&b) == ESP_OK) {
            app_state_set_battery(&b);
        }
    }

    /* UI */
    app_state_set_change_cb(on_state_change);
    if (bsp_display_lock(0)) {
        weather_ui_create();
        lv_timer_create(ui_timer_cb, 1000, NULL);
        bsp_display_unlock();
    }

    xTaskCreate(battery_task, "battery", 3072, NULL, 3, NULL);

    /* Network + OTA page, then the serial console */
    esp_err_t err = wifi_ota_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi/OTA no disponible: %s", esp_err_to_name(err));
    }
    ESP_ERROR_CHECK(serial_console_start());

    /* If we got here after an OTA, the new image works: cancel the rollback */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "Nuevo firmware validado en %s", running->label);
    }

    ESP_LOGI(TAG, "Listo. Envía por el puerto serie: /set_weather -city \"Madrid\" -temperature \"32ºC\" -sky \"sunny\"");
}
