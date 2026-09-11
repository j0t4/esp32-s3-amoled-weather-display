#include "status_json.h"

#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_mac.h"

#include "app_state.h"

static void add_temp(cJSON *o, const char *key, bool valid, float v)
{
    if (valid) {
        cJSON_AddNumberToObject(o, key, (double)((int)(v * 10 + (v >= 0 ? 0.5f : -0.5f))) / 10.0);
    } else {
        cJSON_AddNullToObject(o, key);
    }
}

char *status_json_build(bool pretty)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    /* Firmware --------------------------------------------------------- */
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    cJSON *fw = cJSON_AddObjectToObject(root, "firmware");
    cJSON_AddStringToObject(fw, "project", app->project_name);
    cJSON_AddStringToObject(fw, "version", app->version);
    cJSON_AddStringToObject(fw, "idf", app->idf_ver);
    char build[40];
    snprintf(build, sizeof(build), "%s %s", app->date, app->time);
    cJSON_AddStringToObject(fw, "build", build);
    char sha[17];
    for (int i = 0; i < 8; i++) {
        snprintf(sha + i * 2, 3, "%02x", app->app_elf_sha256[i]);
    }
    cJSON_AddStringToObject(fw, "elf_sha256", sha);
    cJSON_AddStringToObject(fw, "running_partition", running ? running->label : "?");
    cJSON_AddStringToObject(fw, "boot_partition", boot ? boot->label : "?");
    cJSON_AddNumberToObject(fw, "partition_size", running ? (double)running->size : 0);

    /* System ------------------------------------------------------------ */
    cJSON *sys = cJSON_AddObjectToObject(root, "system");
    cJSON_AddNumberToObject(sys, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(sys, "heap_free", (double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(sys, "psram_free", (double)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macs[18];
    snprintf(macs, sizeof(macs), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(sys, "mac", macs);
    cJSON_AddNumberToObject(sys, "brightness", app_state_get_brightness());

    /* Time -------------------------------------------------------------- */
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    cJSON *tm = cJSON_AddObjectToObject(root, "time");
    char iso[32];
    strftime(iso, sizeof(iso), "%Y-%m-%d %H:%M:%S", &t);
    cJSON_AddStringToObject(tm, "local", iso);
    cJSON_AddNumberToObject(tm, "epoch", (double)now);
    cJSON_AddBoolToObject(tm, "valid", t.tm_year >= 124);

    /* Weather ----------------------------------------------------------- */
    weather_data_t w;
    app_state_get_weather(&w);
    cJSON *we = cJSON_AddObjectToObject(root, "weather");
    cJSON_AddBoolToObject(we, "valid", w.valid);
    cJSON_AddStringToObject(we, "city", w.city);
    cJSON_AddStringToObject(we, "temperature_raw", w.temperature_raw);
    add_temp(we, "temperature", w.temperature_valid, w.temperature);
    char unit[2] = { w.temp_unit ? w.temp_unit : 'C', 0 };
    cJSON_AddStringToObject(we, "unit", unit);
    cJSON_AddStringToObject(we, "sky_raw", w.sky_raw);
    cJSON_AddStringToObject(we, "sky", app_state_sky_key(w.sky));
    cJSON_AddStringToObject(we, "sky_label", app_state_sky_label_es(w.sky));
    cJSON_AddStringToObject(we, "description", w.description);
    if (w.humidity >= 0) cJSON_AddNumberToObject(we, "humidity", w.humidity); else cJSON_AddNullToObject(we, "humidity");
    cJSON_AddStringToObject(we, "wind", w.wind);
    add_temp(we, "feels_like", w.feels_like_valid, w.feels_like);
    add_temp(we, "temp_min", w.temp_min_valid, w.temp_min);
    add_temp(we, "temp_max", w.temp_max_valid, w.temp_max);
    if (w.pressure > 0) cJSON_AddNumberToObject(we, "pressure", w.pressure); else cJSON_AddNullToObject(we, "pressure");
    if (w.uv_index >= 0) cJSON_AddNumberToObject(we, "uv_index", w.uv_index); else cJSON_AddNullToObject(we, "uv_index");
    cJSON_AddNumberToObject(we, "updated_at", (double)w.updated_at);
    if (w.updated_at > 0) {
        struct tm tu;
        localtime_r(&w.updated_at, &tu);
        strftime(iso, sizeof(iso), "%Y-%m-%d %H:%M:%S", &tu);
        cJSON_AddStringToObject(we, "updated_local", iso);
    }

    /* Battery ----------------------------------------------------------- */
    battery_status_t b;
    app_state_get_battery(&b);
    cJSON *bat = cJSON_AddObjectToObject(root, "battery");
    cJSON_AddBoolToObject(bat, "pmu_present", b.present);
    cJSON_AddBoolToObject(bat, "battery_present", b.battery_present);
    cJSON_AddBoolToObject(bat, "charging", b.charging);
    cJSON_AddBoolToObject(bat, "usb_power", b.usb_power);
    if (b.percent >= 0) cJSON_AddNumberToObject(bat, "percent", b.percent); else cJSON_AddNullToObject(bat, "percent");
    cJSON_AddNumberToObject(bat, "voltage_mv", b.voltage_mv);

    /* Network ----------------------------------------------------------- */
    network_status_t n;
    app_state_get_network(&n);
    cJSON *net = cJSON_AddObjectToObject(root, "network");
    cJSON_AddStringToObject(net, "hostname", n.hostname);
    cJSON_AddBoolToObject(net, "ap_active", n.ap_active);
    cJSON_AddStringToObject(net, "ap_ip", n.ap_ip);
    cJSON_AddBoolToObject(net, "sta_connected", n.sta_connected);
    cJSON_AddStringToObject(net, "sta_ssid", n.sta_ssid);
    cJSON_AddStringToObject(net, "sta_ip", n.sta_ip);

    char *out = pretty ? cJSON_Print(root) : cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}
