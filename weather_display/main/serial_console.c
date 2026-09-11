#include "serial_console.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>

#include "esp_console.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_state.h"
#include "rtc_pcf85063.h"
#include "wifi_ota.h"
#include "status_json.h"
#include "display_ctl.h"

static const char *TAG = "console";

/* ----------------------------------------------------------------------- */
/* "-key value" argument parsing                                            */
/* ----------------------------------------------------------------------- */
#define MAX_KV 16

typedef struct {
    const char *key;
    const char *value;
} kv_t;

static bool looks_like_number(const char *s)
{
    if (*s == '-' || *s == '+') s++;
    return isdigit((unsigned char)*s) || (*s == '.' && isdigit((unsigned char)s[1]));
}

/* Returns number of pairs, or -1 on syntax error (message printed) */
static int parse_kv(int argc, char **argv, kv_t *out, int max)
{
    int n = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || looks_like_number(a)) {
            printf("ERR argumento inesperado: '%s' (se esperaba -clave valor)\n", a);
            return -1;
        }
        while (*a == '-') a++;
        if (*a == '\0') {
            printf("ERR clave vacía\n");
            return -1;
        }
        if (n >= max) {
            printf("ERR demasiados argumentos\n");
            return -1;
        }
        out[n].key = a;
        /* value: next token unless it is another "-key" (negative numbers are values) */
        if (i + 1 < argc && (argv[i + 1][0] != '-' || looks_like_number(argv[i + 1]))) {
            out[n].value = argv[i + 1];
            i++;
        } else {
            out[n].value = "";
        }
        n++;
    }
    return n;
}

static const char *kv_get(const kv_t *kv, int n, const char *key)
{
    for (int i = 0; i < n; i++) {
        if (strcasecmp(kv[i].key, key) == 0) {
            return kv[i].value;
        }
    }
    return NULL;
}

/* ----------------------------------------------------------------------- */
static int cmd_set_weather(int argc, char **argv)
{
    kv_t kv[MAX_KV];
    int n = parse_kv(argc, argv, kv, MAX_KV);
    if (n < 0) {
        return 1;
    }
    if (n == 0) {
        printf("ERR uso: /set_weather -city \"Madrid\" -temperature \"32ºC\" -sky \"sunny\"\n");
        return 1;
    }

    /* Start from the current data so partial updates keep the other fields */
    weather_data_t w;
    app_state_get_weather(&w);

    char err[96];
    for (int i = 0; i < n; i++) {
        if (!app_state_apply_field(&w, kv[i].key, kv[i].value, err, sizeof(err))) {
            printf("ERR %s\n", err);
            return 1;
        }
    }
    if (!w.city[0] || !w.temperature_valid) {
        printf("ERR faltan -city y/o -temperature\n");
        return 1;
    }
    app_state_set_weather(&w);

    char t[16];
    snprintf(t, sizeof(t), "%.1f", w.temperature);
    printf("OK weather: %s %s°%c %s (%s)\n", w.city, t, w.temp_unit,
           app_state_sky_key(w.sky), app_state_sky_label_es(w.sky));
    if (w.sky == SKY_UNKNOWN) {
        printf("WARN cielo '%s' no reconocido; valores: sunny, clear, night, partly cloudy, cloudy, rain, storm, snow, fog, windy\n", w.sky_raw);
    }
    return 0;
}

static int cmd_set_time(int argc, char **argv)
{
    kv_t kv[MAX_KV];
    int n = parse_kv(argc, argv, kv, MAX_KV);
    if (n < 0) {
        return 1;
    }
    const char *dt = kv_get(kv, n, "datetime");
    const char *ep = kv_get(kv, n, "epoch");
    time_t epoch = 0;

    if (dt && *dt) {
        struct tm t = { 0 };
        int y, mo, d, h = 0, mi = 0, s = 0;
        int got = sscanf(dt, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s);
        if (got < 3) {
            got = sscanf(dt, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s);
        }
        if (got < 3 || y < 2024 || mo < 1 || mo > 12 || d < 1 || d > 31) {
            printf("ERR formato: -datetime \"YYYY-MM-DD HH:MM:SS\" (hora local)\n");
            return 1;
        }
        t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
        t.tm_hour = h; t.tm_min = mi; t.tm_sec = s; t.tm_isdst = -1;
        epoch = mktime(&t);
    } else if (ep && *ep) {
        epoch = (time_t)strtoll(ep, NULL, 10);
        if (epoch < 1700000000) {
            printf("ERR epoch no válido\n");
            return 1;
        }
    } else {
        printf("ERR uso: /set_time -datetime \"2026-09-11 18:45:00\"  |  -epoch 1789000000\n");
        return 1;
    }

    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    esp_err_t r = rtc_pcf85063_sync_rtc_from_system();
    app_state_notify();

    struct tm lt;
    localtime_r(&epoch, &lt);
    printf("OK time: %04d-%02d-%02d %02d:%02d:%02d (RTC %s)\n",
           lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec,
           r == ESP_OK ? "actualizado" : "no disponible");
    return 0;
}

static int cmd_set_wifi(int argc, char **argv)
{
    kv_t kv[MAX_KV];
    int n = parse_kv(argc, argv, kv, MAX_KV);
    if (n < 0) {
        return 1;
    }
    if (kv_get(kv, n, "clear")) {
        wifi_ota_clear_sta_credentials();
        printf("OK wifi: credenciales borradas, sólo punto de acceso\n");
        return 0;
    }
    const char *ssid = kv_get(kv, n, "ssid");
    const char *pass = kv_get(kv, n, "password");
    if (!pass) pass = kv_get(kv, n, "pass");
    if (!ssid || !*ssid) {
        printf("ERR uso: /set_wifi -ssid \"MiRed\" -password \"secreto\"   |   /set_wifi -clear\n");
        return 1;
    }
    esp_err_t r = wifi_ota_set_sta_credentials(ssid, pass ? pass : "");
    if (r != ESP_OK) {
        printf("ERR %s\n", esp_err_to_name(r));
        return 1;
    }
    printf("OK wifi: conectando a '%s'...\n", ssid);
    return 0;
}

static int cmd_brightness(int argc, char **argv)
{
    kv_t kv[MAX_KV];
    int n = parse_kv(argc, argv, kv, MAX_KV);
    if (n < 0) {
        return 1;
    }
    const char *lv = kv_get(kv, n, "level");
    if (!lv) lv = kv_get(kv, n, "value");
    if (!lv || !*lv) {
        printf("ERR uso: /brightness -level 0..100\n");
        return 1;
    }
    int level = atoi(lv);
    display_ctl_set_brightness(level);
    printf("OK brightness: %d\n", app_state_get_brightness());
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    char *json = status_json_build(false);
    if (!json) {
        printf("ERR sin memoria\n");
        return 1;
    }
    printf("%s\n", json);
    free(json);
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    printf("OK reiniciando...\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return 0;
}

static int cmd_help(int argc, char **argv)
{
    printf("Comandos disponibles:\n"
           "  /set_weather -city \"Madrid\" -temperature \"32ºC\" -sky \"sunny\"\n"
           "               [-humidity 40] [-wind \"12 km/h NE\"] [-feels 34] [-min 18] [-max 34]\n"
           "               [-pressure 1015] [-uv 7] [-desc \"Cielo despejado toda la tarde\"]\n"
           "      sky: sunny | clear | night | partly cloudy | cloudy | rain | storm | snow | fog | windy\n"
           "           (también en español: soleado, despejado, nublado, lluvia, tormenta, nieve, niebla, viento)\n"
           "  /set_time    -datetime \"2026-09-11 18:45:00\"   |   -epoch 1789000000\n"
           "  /set_wifi    -ssid \"MiRed\" -password \"secreto\"   |   -clear\n"
           "  /brightness  -level 80\n"
           "  /status      JSON con tiempo, hora, batería, red y firmware\n"
           "  /reboot\n"
           "  /help\n"
           "Respuestas: una línea que empieza por OK, ERR o WARN.\n");
    return 0;
}

/* ----------------------------------------------------------------------- */
esp_err_t serial_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "weather>";
    repl_config.max_cmdline_length = 512;
    repl_config.task_stack_size = 6144;

    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl), TAG, "repl");

    const esp_console_cmd_t cmds[] = {
        { .command = "/set_weather", .help = "Actualiza la tarjeta del tiempo: -city -temperature -sky [-humidity -wind -feels -min -max -pressure -uv -desc]", .func = cmd_set_weather },
        { .command = "/set_time",    .help = "Ajusta fecha y hora: -datetime \"YYYY-MM-DD HH:MM:SS\" | -epoch N", .func = cmd_set_time },
        { .command = "/set_wifi",    .help = "Credenciales Wi-Fi para la página OTA: -ssid -password | -clear", .func = cmd_set_wifi },
        { .command = "/brightness",  .help = "Brillo de la pantalla: -level 0..100", .func = cmd_brightness },
        { .command = "/status",      .help = "Estado completo en JSON", .func = cmd_status },
        { .command = "/reboot",      .help = "Reinicia el dispositivo", .func = cmd_reboot },
        { .command = "/help",        .help = "Ayuda de los comandos", .func = cmd_help },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_RETURN_ON_ERROR(esp_console_cmd_register(&cmds[i]), TAG, "register %s", cmds[i].command);
    }
    esp_console_register_help_command();

    ESP_RETURN_ON_ERROR(esp_console_start_repl(repl), TAG, "start repl");
    ESP_LOGI(TAG, "Consola lista. Ejemplo: /set_weather -city \"Madrid\" -temperature \"32ºC\" -sky \"sunny\"");
    return ESP_OK;
}
