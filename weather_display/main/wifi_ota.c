#include "wifi_ota.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/param.h>
#include <stdarg.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_netif_sntp.h"
#include "nvs.h"
#include "mdns.h"
#include "cJSON.h"

#include "app_state.h"
#include "status_json.h"
#include "rtc_pcf85063.h"
#include "ota_progress.h"
#include "display_ctl.h"

static const char *TAG = "wifi_ota";

#define NVS_NS_WIFI   "wifi"
#define STA_RETRY_MS  8000

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

static esp_netif_t   *s_ap_netif;
static esp_netif_t   *s_sta_netif;
static httpd_handle_t s_httpd;
static esp_timer_handle_t s_retry_timer;
static esp_timer_handle_t s_reboot_timer;
static network_status_t s_net;
static bool s_sta_configured;
static bool s_sntp_started;

/* ----------------------------------------------------------------------- */
/* Credentials in NVS                                                       */
/* ----------------------------------------------------------------------- */
static bool load_sta_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    bool ok = nvs_get_str(h, "ssid", ssid, &ssid_len) == ESP_OK && ssid[0] != '\0';
    size_t pl = pass_len;
    if (ok && nvs_get_str(h, "pass", pass, &pl) != ESP_OK) {
        pass[0] = '\0';
    }
    nvs_close(h);
    return ok;
}

static esp_err_t apply_sta_config(const char *ssid, const char *pass)
{
    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "sta config");
    strlcpy(s_net.sta_ssid, ssid, sizeof(s_net.sta_ssid));
    s_sta_configured = true;
    return ESP_OK;
}

esp_err_t wifi_ota_set_sta_credentials(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0 || strlen(ssid) > 32 || (password && strlen(password) > 63)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h), TAG, "nvs");
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", password ? password : "");
    nvs_commit(h);
    nvs_close(h);

    esp_wifi_disconnect();
    ESP_RETURN_ON_ERROR(apply_sta_config(ssid, password ? password : ""), TAG, "apply");
    s_net.sta_connected = false;
    s_net.sta_ip[0] = '\0';
    app_state_set_network(&s_net);
    return esp_wifi_connect();
}

esp_err_t wifi_ota_clear_sta_credentials(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    s_sta_configured = false;
    esp_timer_stop(s_retry_timer);
    esp_wifi_disconnect();
    s_net.sta_connected = false;
    s_net.sta_ip[0] = '\0';
    s_net.sta_ssid[0] = '\0';
    app_state_set_network(&s_net);
    return ESP_OK;
}

/* ----------------------------------------------------------------------- */
/* Events                                                                   */
/* ----------------------------------------------------------------------- */
static void retry_timer_cb(void *arg)
{
    if (s_sta_configured) {
        ESP_LOGI(TAG, "Reintentando conexión a '%s'", s_net.sta_ssid);
        esp_wifi_connect();
    }
}

static void sntp_sync_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Hora sincronizada por NTP");
    rtc_pcf85063_sync_rtc_from_system();
    app_state_notify();
}

static void start_sntp_once(void)
{
    if (s_sntp_started) {
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = sntp_sync_cb;
    cfg.start = true;
    if (esp_netif_sntp_init(&cfg) == ESP_OK) {
        s_sntp_started = true;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            if (s_sta_configured) {
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_net.sta_connected) {
                ESP_LOGW(TAG, "Wi-Fi desconectado");
            }
            s_net.sta_connected = false;
            s_net.sta_ip[0] = '\0';
            app_state_set_network(&s_net);
            if (s_sta_configured) {
                esp_timer_stop(s_retry_timer);
                esp_timer_start_once(s_retry_timer, (uint64_t)STA_RETRY_MS * 1000);
            }
            break;
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *e = data;
            ESP_LOGI(TAG, "Cliente conectado al AP: " MACSTR, MAC2STR(e->mac));
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        snprintf(s_net.sta_ip, sizeof(s_net.sta_ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_net.sta_connected = true;
        ESP_LOGI(TAG, "Conectado a '%s', IP %s  ->  http://%s/  |  http://%s.local/",
                 s_net.sta_ssid, s_net.sta_ip, s_net.sta_ip, WIFI_HOSTNAME);
        app_state_set_network(&s_net);
        start_sntp_once();
    }
}

/* ----------------------------------------------------------------------- */
/* HTTP helpers                                                             */
/* ----------------------------------------------------------------------- */
static esp_err_t send_json(httpd_req_t *req, const char *status, const char *json)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t send_result(httpd_req_t *req, bool ok, const char *status, const char *fmt, ...)
{
    char msg[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", ok);
    cJSON_AddStringToObject(o, "message", msg);
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    esp_err_t r = send_json(req, status, s ? s : "{}");
    free(s);
    return r;
}

/* Reads the whole request body (small JSON payloads only) */
static char *read_body(httpd_req_t *req, size_t max_len)
{
    if (req->content_len == 0 || req->content_len > max_len) {
        return NULL;
    }
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        return NULL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(buf);
            return NULL;
        }
        got += r;
    }
    buf[got] = '\0';
    return buf;
}

/* ----------------------------------------------------------------------- */
/* Handlers                                                                 */
/* ----------------------------------------------------------------------- */
static esp_err_t h_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start - 1);
}

static esp_err_t h_status(httpd_req_t *req)
{
    char *json = status_json_build(false);
    if (!json) {
        return send_result(req, false, "500 Internal Server Error", "sin memoria");
    }
    esp_err_t r = send_json(req, "200 OK", json);
    free(json);
    return r;
}

static const char *json_str(cJSON *o, const char *key, char *tmp, size_t n)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!v) {
        return NULL;
    }
    if (cJSON_IsString(v)) {
        return v->valuestring;
    }
    if (cJSON_IsNumber(v)) {
        snprintf(tmp, n, "%g", v->valuedouble);
        return tmp;
    }
    return NULL;
}

static esp_err_t h_api_weather(httpd_req_t *req)
{
    char *body = read_body(req, 2048);
    if (!body) {
        return send_result(req, false, "400 Bad Request", "cuerpo JSON vacío o demasiado grande");
    }
    cJSON *o = cJSON_Parse(body);
    free(body);
    if (!o) {
        return send_result(req, false, "400 Bad Request", "JSON no válido");
    }

    weather_data_t w;
    app_state_get_weather(&w);
    static const char *keys[] = { "city", "temperature", "sky", "humidity", "wind", "feels_like",
                                  "temp_min", "temp_max", "pressure", "uv", "description" };
    char err[96] = "";
    char tmp[32];
    int applied = 0;
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        const char *v = json_str(o, keys[i], tmp, sizeof(tmp));
        if (!v || !*v) {
            continue;
        }
        if (!app_state_apply_field(&w, keys[i], v, err, sizeof(err))) {
            cJSON_Delete(o);
            return send_result(req, false, "400 Bad Request", "%s", err);
        }
        applied++;
    }
    cJSON_Delete(o);
    if (applied == 0) {
        return send_result(req, false, "400 Bad Request", "ningún campo reconocido");
    }
    if (!w.city[0] || !w.temperature_valid) {
        return send_result(req, false, "400 Bad Request", "faltan city y/o temperature");
    }
    app_state_set_weather(&w);
    return send_result(req, true, "200 OK", "tiempo actualizado: %s, %s", w.city, app_state_sky_label_es(w.sky));
}

static esp_err_t h_api_wifi(httpd_req_t *req)
{
    char *body = read_body(req, 512);
    if (!body) {
        return send_result(req, false, "400 Bad Request", "cuerpo vacío");
    }
    cJSON *o = cJSON_Parse(body);
    free(body);
    if (!o) {
        return send_result(req, false, "400 Bad Request", "JSON no válido");
    }
    cJSON *clear = cJSON_GetObjectItemCaseSensitive(o, "clear");
    if (cJSON_IsTrue(clear)) {
        cJSON_Delete(o);
        wifi_ota_clear_sta_credentials();
        return send_result(req, true, "200 OK", "credenciales borradas");
    }
    char tmp[8];
    const char *ssid = json_str(o, "ssid", tmp, sizeof(tmp));
    const char *pass = json_str(o, "password", tmp, sizeof(tmp));
    if (!ssid || !*ssid) {
        cJSON_Delete(o);
        return send_result(req, false, "400 Bad Request", "falta ssid");
    }
    esp_err_t r = wifi_ota_set_sta_credentials(ssid, pass ? pass : "");
    cJSON_Delete(o);
    if (r != ESP_OK) {
        return send_result(req, false, "400 Bad Request", "%s", esp_err_to_name(r));
    }
    return send_result(req, true, "200 OK", "conectando a la red");
}

static esp_err_t h_api_brightness(httpd_req_t *req)
{
    char *body = read_body(req, 128);
    if (!body) {
        return send_result(req, false, "400 Bad Request", "cuerpo vacío");
    }
    cJSON *o = cJSON_Parse(body);
    free(body);
    cJSON *lv = o ? cJSON_GetObjectItemCaseSensitive(o, "level") : NULL;
    if (!cJSON_IsNumber(lv)) {
        cJSON_Delete(o);
        return send_result(req, false, "400 Bad Request", "falta level (0..100)");
    }
    display_ctl_set_brightness((int)lv->valuedouble);
    cJSON_Delete(o);
    return send_result(req, true, "200 OK", "brillo %d", app_state_get_brightness());
}

static void reboot_timer_cb(void *arg)
{
    esp_restart();
}

static esp_err_t h_reboot(httpd_req_t *req)
{
    send_result(req, true, "200 OK", "reiniciando");
    esp_timer_start_once(s_reboot_timer, 800 * 1000);
    return ESP_OK;
}

/* Firmware upload: raw application image in the body ---------------------- */
static esp_err_t h_update(httpd_req_t *req)
{
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        return send_result(req, false, "500 Internal Server Error", "no hay partición OTA disponible");
    }
    if (req->content_len == 0) {
        return send_result(req, false, "400 Bad Request", "cuerpo vacío: envía el archivo .bin");
    }
    if (req->content_len > target->size) {
        return send_result(req, false, "413 Payload Too Large", "el firmware (%u B) no cabe en %s (%u B)",
                           (unsigned)req->content_len, target->label, (unsigned)target->size);
    }
    ESP_LOGI(TAG, "OTA: %u bytes -> partición %s", (unsigned)req->content_len, target->label);

    const size_t CHUNK = 4096;
    char *buf = malloc(CHUNK);
    if (!buf) {
        return send_result(req, false, "500 Internal Server Error", "sin memoria");
    }

    esp_ota_handle_t ota = 0;
    esp_err_t err = ESP_OK;
    size_t received = 0;
    bool header_checked = false;
    int last_pct = -1;
    ota_progress_set(0);

    while (received < req->content_len) {
        int r = httpd_req_recv(req, buf, MIN(CHUNK, req->content_len - received));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            err = ESP_FAIL;
            ESP_LOGE(TAG, "OTA: error de recepción");
            break;
        }
        if (!header_checked) {
            /* Validate magic byte + read the embedded app descriptor before erasing anything */
            if (r < (int)(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))) {
                err = ESP_ERR_INVALID_SIZE;
                break;
            }
            if ((uint8_t)buf[0] != ESP_IMAGE_HEADER_MAGIC) {
                ESP_LOGE(TAG, "OTA: el archivo no es una imagen de aplicación ESP32 (magic 0x%02x)", (uint8_t)buf[0]);
                err = ESP_ERR_OTA_VALIDATE_FAILED;
                break;
            }
            esp_app_desc_t desc;
            memcpy(&desc, buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(desc));
            if (desc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
                err = ESP_ERR_OTA_VALIDATE_FAILED;
                break;
            }
            ESP_LOGI(TAG, "OTA: nuevo firmware %s v%s (%s %s)", desc.project_name, desc.version, desc.date, desc.time);
            err = esp_ota_begin(target, req->content_len, &ota);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
                break;
            }
            header_checked = true;
        }
        err = esp_ota_write(ota, buf, r);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(err));
            break;
        }
        received += r;
        int pct = (int)((uint64_t)received * 100 / req->content_len);
        if (pct != last_pct && (pct % 5 == 0 || pct >= 99)) {
            last_pct = pct;
            ota_progress_set(pct);
        }
    }
    free(buf);

    if (err == ESP_OK) {
        err = esp_ota_end(ota);
        ota = 0;
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
        }
    } else if (ota) {
        esp_ota_abort(ota);
    }
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(target);
    }

    if (err != ESP_OK) {
        ota_progress_set(-1);
        const char *why = err == ESP_ERR_OTA_VALIDATE_FAILED ? "imagen no válida (¿es el .bin de la aplicación, no el merged/bootloader?)"
                          : esp_err_to_name(err);
        return send_result(req, false, "400 Bad Request", "OTA fallida: %s", why);
    }

    ota_progress_set(100);
    ESP_LOGI(TAG, "OTA completada, reiniciando en %s", target->label);
    send_result(req, true, "200 OK", "firmware escrito en %s (%u bytes). Reiniciando...", target->label, (unsigned)received);
    esp_timer_start_once(s_reboot_timer, 1500 * 1000);
    return ESP_OK;
}

static esp_err_t start_httpd(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 12;
    cfg.recv_wait_timeout = 15;
    cfg.send_wait_timeout = 15;
    cfg.lru_purge_enable = true;
    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &cfg), TAG, "httpd_start");

    const httpd_uri_t uris[] = {
        { .uri = "/",               .method = HTTP_GET,  .handler = h_index },
        { .uri = "/index.html",     .method = HTTP_GET,  .handler = h_index },
        { .uri = "/api/status",     .method = HTTP_GET,  .handler = h_status },
        { .uri = "/api/weather",    .method = HTTP_POST, .handler = h_api_weather },
        { .uri = "/api/wifi",       .method = HTTP_POST, .handler = h_api_wifi },
        { .uri = "/api/brightness", .method = HTTP_POST, .handler = h_api_brightness },
        { .uri = "/update",         .method = HTTP_POST, .handler = h_update },
        { .uri = "/reboot",         .method = HTTP_POST, .handler = h_reboot },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_httpd, &uris[i]);
    }
    return ESP_OK;
}

/* ----------------------------------------------------------------------- */
esp_err_t wifi_ota_start(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    s_ap_netif = esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(s_sta_netif, WIFI_HOSTNAME);

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL), TAG, "evt");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL), TAG, "evt");

    const esp_timer_create_args_t retry_args = { .callback = retry_timer_cb, .name = "wifi_retry" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&retry_args, &s_retry_timer), TAG, "timer");
    const esp_timer_create_args_t reboot_args = { .callback = reboot_timer_cb, .name = "reboot" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&reboot_args, &s_reboot_timer), TAG, "timer");

    /* Soft-AP: SSID with the last 2 MAC bytes so several boards can coexist */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    wifi_config_t ap = { 0 };
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "%s-%02X%02X", WIFI_AP_SSID_PREFIX, mac[4], mac[5]);
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    strlcpy((char *)ap.ap.password, WIFI_AP_PASSWORD, sizeof(ap.ap.password));
    ap.ap.channel = 6;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "ap config");
    esp_wifi_set_ps(WIFI_PS_NONE);

    char ssid[33] = "", pass[65] = "";
    if (load_sta_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGI(TAG, "Credenciales Wi-Fi guardadas para '%s'", ssid);
        apply_sta_config(ssid, pass);
    }

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");

    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(s_ap_netif, &ip) == ESP_OK) {
        snprintf(s_net.ap_ip, sizeof(s_net.ap_ip), IPSTR, IP2STR(&ip.ip));
    }
    s_net.ap_active = true;
    strlcpy(s_net.hostname, WIFI_HOSTNAME, sizeof(s_net.hostname));
    app_state_set_network(&s_net);
    ESP_LOGI(TAG, "Punto de acceso '%s' (clave %s) -> http://%s/", ap.ap.ssid, WIFI_AP_PASSWORD, s_net.ap_ip);

    /* mDNS: http://weather-display.local */
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(WIFI_HOSTNAME);
        mdns_instance_name_set("Weather Display OTA");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }

    return start_httpd();
}
