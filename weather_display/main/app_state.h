/**
 * app_state.h - Shared application state (weather data, time, battery, network)
 *
 * All modules read/modify the state through this API, which is protected by a mutex.
 * Weather data is persisted in NVS so it survives a reboot.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_STR_LEN        48
#define APP_DESC_LEN       96

/* Sky condition, normalised from the free-text value received on the serial port */
typedef enum {
    SKY_UNKNOWN = 0,
    SKY_SUNNY,          /* sunny / clear (day) */
    SKY_CLEAR_NIGHT,    /* clear (night) */
    SKY_PARTLY_CLOUDY,
    SKY_CLOUDY,
    SKY_RAIN,
    SKY_STORM,
    SKY_SNOW,
    SKY_FOG,
    SKY_WINDY,
    SKY_COUNT
} sky_kind_t;

typedef struct {
    char   city[APP_STR_LEN];
    char   temperature_raw[APP_STR_LEN]; /* exactly as received, e.g. "32ºC" */
    float  temperature;                  /* parsed numeric value */
    char   temp_unit;                    /* 'C' or 'F' */
    bool   temperature_valid;
    char   sky_raw[APP_STR_LEN];         /* exactly as received, e.g. "sunny" */
    sky_kind_t sky;
    char   description[APP_DESC_LEN];    /* optional free text (-desc) */

    /* Optional extra fields, negative/empty means "not provided" */
    int    humidity;                     /* % */
    char   wind[APP_STR_LEN];            /* e.g. "12 km/h NE" */
    float  feels_like;  bool feels_like_valid;
    float  temp_min;    bool temp_min_valid;
    float  temp_max;    bool temp_max_valid;
    int    pressure;                     /* hPa, <=0 -> not provided */
    int    uv_index;                     /* <0 -> not provided */

    time_t updated_at;                   /* epoch of last /set_weather, 0 if never */
    bool   valid;                        /* at least one /set_weather received */
} weather_data_t;

typedef struct {
    bool  present;          /* PMU answered on I2C */
    bool  battery_present;
    bool  charging;
    bool  usb_power;
    int   percent;          /* 0..100, -1 unknown */
    int   voltage_mv;
} battery_status_t;

typedef struct {
    bool  ap_active;
    bool  sta_connected;
    char  ap_ip[20];
    char  sta_ip[20];
    char  sta_ssid[33];
    char  hostname[32];
} network_status_t;

/* Called (from any task) whenever the weather data changes. UI uses it to redraw. */
typedef void (*app_state_change_cb_t)(void);

esp_err_t app_state_init(void);
void      app_state_set_change_cb(app_state_change_cb_t cb);

/* Weather -------------------------------------------------------------- */
void      app_state_get_weather(weather_data_t *out);
/* Copies `in` into the state, stamps updated_at, persists to NVS and fires the change callback */
esp_err_t app_state_set_weather(const weather_data_t *in);
/* Helpers used by the console and the web API */
sky_kind_t  app_state_parse_sky(const char *text);
const char *app_state_sky_label_es(sky_kind_t sky);   /* Spanish label for the display */
const char *app_state_sky_key(sky_kind_t sky);        /* canonical english key for JSON */
bool        app_state_parse_temperature(const char *text, float *value, char *unit);
/**
 * Apply one "key=value" field (from the serial command or the web form) onto `w`.
 * Keys (case-insensitive): city, temperature|temp, sky, humidity, wind, feels|feels_like,
 * min|temp_min, max|temp_max, pressure, uv, desc|description.
 * Returns false and fills `err` when the key is unknown or the value cannot be parsed.
 */
bool        app_state_apply_field(weather_data_t *w, const char *key, const char *value, char *err, size_t err_len);

/* Battery / network (updated by their own modules) ----------------------- */
void app_state_get_battery(battery_status_t *out);
void app_state_set_battery(const battery_status_t *in);
void app_state_get_network(network_status_t *out);
void app_state_set_network(const network_status_t *in);

/* Display brightness (persisted) */
int  app_state_get_brightness(void);
void app_state_set_brightness(int percent);

/* Fires the change callback (e.g. after time/battery changes) */
void app_state_notify(void);

#ifdef __cplusplus
}
#endif
