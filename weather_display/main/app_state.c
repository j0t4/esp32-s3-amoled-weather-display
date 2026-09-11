#include "app_state.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "app_state";
#define NVS_NS        "weather"
#define NVS_KEY_DATA  "data"
#define NVS_KEY_BRI   "brightness"

static SemaphoreHandle_t s_mutex;
static weather_data_t    s_weather;
static battery_status_t  s_battery = { .percent = -1 };
static network_status_t  s_network;
static int               s_brightness = 80;
static app_state_change_cb_t s_cb;

#define LOCK()   xSemaphoreTake(s_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mutex)

/* ------------------------------------------------------------------------ */
static void load_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_weather);
    weather_data_t tmp;
    if (nvs_get_blob(h, NVS_KEY_DATA, &tmp, &len) == ESP_OK && len == sizeof(tmp)) {
        s_weather = tmp;
        ESP_LOGI(TAG, "Loaded saved weather: %s %s %s", tmp.city, tmp.temperature_raw, tmp.sky_raw);
    }
    int32_t b;
    if (nvs_get_i32(h, NVS_KEY_BRI, &b) == ESP_OK && b >= 5 && b <= 100) {
        s_brightness = (int)b;
    }
    nvs_close(h);
}

static esp_err_t save_weather_to_nvs(const weather_data_t *w)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, NVS_KEY_DATA, w, sizeof(*w));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t app_state_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_weather, 0, sizeof(s_weather));
    s_weather.humidity = -1;
    s_weather.uv_index = -1;
    load_from_nvs();
    return ESP_OK;
}

void app_state_set_change_cb(app_state_change_cb_t cb)
{
    s_cb = cb;
}

void app_state_notify(void)
{
    if (s_cb) {
        s_cb();
    }
}

/* ------------------------------------------------------------------------ */
void app_state_get_weather(weather_data_t *out)
{
    LOCK();
    *out = s_weather;
    UNLOCK();
}

esp_err_t app_state_set_weather(const weather_data_t *in)
{
    weather_data_t copy = *in;
    copy.updated_at = time(NULL);
    copy.valid = true;
    LOCK();
    s_weather = copy;
    UNLOCK();
    esp_err_t err = save_weather_to_nvs(&copy);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS save failed: %s", esp_err_to_name(err));
    }
    app_state_notify();
    return err;
}

/* ------------------------------------------------------------------------ */
static bool has_word(const char *s, const char *w)
{
    return strcasestr(s, w) != NULL;
}

sky_kind_t app_state_parse_sky(const char *text)
{
    if (!text || !*text) {
        return SKY_UNKNOWN;
    }
    /* Order matters: more specific first. Accepts English and Spanish keywords. */
    if (has_word(text, "thunder") || has_word(text, "storm") || has_word(text, "torment")) return SKY_STORM;
    if (has_word(text, "snow") || has_word(text, "niev") || has_word(text, "sleet") || has_word(text, "hail") || has_word(text, "graniz")) return SKY_SNOW;
    if (has_word(text, "rain") || has_word(text, "lluv") || has_word(text, "drizzle") || has_word(text, "shower") || has_word(text, "llovizna") || has_word(text, "chubasco")) return SKY_RAIN;
    if (has_word(text, "fog") || has_word(text, "mist") || has_word(text, "haze") || has_word(text, "niebla") || has_word(text, "bruma") || has_word(text, "calima")) return SKY_FOG;
    if (has_word(text, "wind") || has_word(text, "vient") || has_word(text, "gale")) return SKY_WINDY;
    if (has_word(text, "partly") || has_word(text, "partial") || has_word(text, "parcial") || has_word(text, "intervalos") || has_word(text, "poco nub")) return SKY_PARTLY_CLOUDY;
    if (has_word(text, "cloud") || has_word(text, "nub") || has_word(text, "overcast") || has_word(text, "cubierto")) return SKY_CLOUDY;
    if (has_word(text, "night") || has_word(text, "noche") || has_word(text, "moon") || has_word(text, "luna")) return SKY_CLEAR_NIGHT;
    if (has_word(text, "sun") || has_word(text, "sol") || has_word(text, "clear") || has_word(text, "despej")) return SKY_SUNNY;
    return SKY_UNKNOWN;
}

const char *app_state_sky_label_es(sky_kind_t sky)
{
    switch (sky) {
    case SKY_SUNNY:         return "Soleado";
    case SKY_CLEAR_NIGHT:   return "Despejado";
    case SKY_PARTLY_CLOUDY: return "Parcialmente nublado";
    case SKY_CLOUDY:        return "Nublado";
    case SKY_RAIN:          return "Lluvia";
    case SKY_STORM:         return "Tormenta";
    case SKY_SNOW:          return "Nieve";
    case SKY_FOG:           return "Niebla";
    case SKY_WINDY:         return "Viento";
    default:                return "Sin datos";
    }
}

const char *app_state_sky_key(sky_kind_t sky)
{
    static const char *keys[SKY_COUNT] = {
        "unknown", "sunny", "clear-night", "partly-cloudy", "cloudy",
        "rain", "storm", "snow", "fog", "windy"
    };
    return (sky >= 0 && sky < SKY_COUNT) ? keys[sky] : keys[0];
}

/* Accepts "32ºC", "32°C", "32 C", "-3.5", "89F", "89 ºF" ... (any encoding of the degree sign) */
bool app_state_parse_temperature(const char *text, float *value, char *unit)
{
    if (!text) {
        return false;
    }
    while (*text && isspace((unsigned char)*text)) {
        text++;
    }
    char *end = NULL;
    float v = strtof(text, &end);
    if (end == text) {
        return false;
    }
    char u = 'C';
    for (const char *p = end; *p; p++) {
        if (*p == 'F' || *p == 'f') { u = 'F'; break; }
        if (*p == 'C' || *p == 'c') { u = 'C'; break; }
    }
    if (value) *value = v;
    if (unit)  *unit = u;
    return isfinite(v);
}

static void copy_str(char *dst, size_t n, const char *src)
{
    if (!src) {
        dst[0] = '\0';
        return;
    }
    /* trim leading/trailing whitespace */
    while (*src && isspace((unsigned char)*src)) src++;
    size_t len = strlen(src);
    while (len > 0 && isspace((unsigned char)src[len - 1])) len--;
    if (len >= n) len = n - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static bool parse_int(const char *s, int *out)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s) return false;
    *out = (int)v;
    return true;
}

static bool key_is(const char *key, const char *a, const char *b)
{
    return strcasecmp(key, a) == 0 || (b && strcasecmp(key, b) == 0);
}

bool app_state_apply_field(weather_data_t *w, const char *key, const char *value, char *err, size_t err_len)
{
    if (!key || !value) {
        snprintf(err, err_len, "campo vacío");
        return false;
    }
    if (key_is(key, "city", "ciudad")) {
        copy_str(w->city, sizeof(w->city), value);
        return true;
    }
    if (key_is(key, "temperature", "temp")) {
        float v; char u;
        if (!app_state_parse_temperature(value, &v, &u)) {
            snprintf(err, err_len, "temperatura no válida: '%s'", value);
            return false;
        }
        copy_str(w->temperature_raw, sizeof(w->temperature_raw), value);
        w->temperature = v;
        w->temp_unit = u;
        w->temperature_valid = true;
        return true;
    }
    if (key_is(key, "sky", "cielo")) {
        copy_str(w->sky_raw, sizeof(w->sky_raw), value);
        w->sky = app_state_parse_sky(w->sky_raw);
        return true;
    }
    if (key_is(key, "humidity", "hum")) {
        int v;
        if (!parse_int(value, &v) || v < 0 || v > 100) {
            snprintf(err, err_len, "humedad no válida: '%s'", value);
            return false;
        }
        w->humidity = v;
        return true;
    }
    if (key_is(key, "wind", "viento")) {
        copy_str(w->wind, sizeof(w->wind), value);
        return true;
    }
    if (key_is(key, "feels", "feels_like")) {
        float v; char u;
        if (!app_state_parse_temperature(value, &v, &u)) {
            snprintf(err, err_len, "sensación no válida: '%s'", value);
            return false;
        }
        w->feels_like = v; w->feels_like_valid = true;
        return true;
    }
    if (key_is(key, "min", "temp_min")) {
        float v; char u;
        if (!app_state_parse_temperature(value, &v, &u)) {
            snprintf(err, err_len, "mínima no válida: '%s'", value);
            return false;
        }
        w->temp_min = v; w->temp_min_valid = true;
        return true;
    }
    if (key_is(key, "max", "temp_max")) {
        float v; char u;
        if (!app_state_parse_temperature(value, &v, &u)) {
            snprintf(err, err_len, "máxima no válida: '%s'", value);
            return false;
        }
        w->temp_max = v; w->temp_max_valid = true;
        return true;
    }
    if (key_is(key, "pressure", "presion")) {
        int v;
        if (!parse_int(value, &v) || v <= 0) {
            snprintf(err, err_len, "presión no válida: '%s'", value);
            return false;
        }
        w->pressure = v;
        return true;
    }
    if (key_is(key, "uv", "uv_index")) {
        int v;
        if (!parse_int(value, &v) || v < 0) {
            snprintf(err, err_len, "índice UV no válido: '%s'", value);
            return false;
        }
        w->uv_index = v;
        return true;
    }
    if (key_is(key, "desc", "description")) {
        copy_str(w->description, sizeof(w->description), value);
        return true;
    }
    snprintf(err, err_len, "parámetro desconocido: -%s", key);
    return false;
}

/* ------------------------------------------------------------------------ */
void app_state_get_battery(battery_status_t *out) { LOCK(); *out = s_battery; UNLOCK(); }
void app_state_set_battery(const battery_status_t *in) { LOCK(); s_battery = *in; UNLOCK(); }
void app_state_get_network(network_status_t *out) { LOCK(); *out = s_network; UNLOCK(); }
void app_state_set_network(const network_status_t *in) { LOCK(); s_network = *in; UNLOCK(); app_state_notify(); }

int app_state_get_brightness(void) { return s_brightness; }

void app_state_set_brightness(int percent)
{
    if (percent < 5) percent = 5;
    if (percent > 100) percent = 100;
    s_brightness = percent;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, NVS_KEY_BRI, percent);
        nvs_commit(h);
        nvs_close(h);
    }
}
