#include "weather_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "app_state.h"
#include "weather_icons.h"
#include "ota_progress.h"

LV_FONT_DECLARE(font_montserrat_14_latin);
LV_FONT_DECLARE(font_montserrat_18_latin);
LV_FONT_DECLARE(font_montserrat_26_latin);
LV_FONT_DECLARE(font_montserrat_72_latin);

#define F_SMALL  (&font_montserrat_14_latin)
#define F_BODY   (&font_montserrat_18_latin)
#define F_TITLE  (&font_montserrat_26_latin)
#define F_TEMP   (&font_montserrat_72_latin)
#define F_SYMBOL (&lv_font_montserrat_16)     /* built-in font: has LV_SYMBOL_* glyphs */

#define SCREEN_W 368
#define SCREEN_H 448
#define ICON_SIZE 150

#define STALE_AFTER_S   (3 * 3600)   /* highlight when no update for 3 h */

static lv_obj_t *s_scr;
static lv_obj_t *s_time;
static lv_obj_t *s_battery;
static lv_obj_t *s_wifi;
static lv_obj_t *s_city;
static lv_obj_t *s_date;
static lv_obj_t *s_icon;
static lv_obj_t *s_temp;
static lv_obj_t *s_unit;
static lv_obj_t *s_sky;
static lv_obj_t *s_desc;
static lv_obj_t *s_card;
static lv_obj_t *s_cell_val[6];
static lv_obj_t *s_cell_title[6];
static lv_obj_t *s_footer;
static lv_obj_t *s_hint;
static lv_obj_t *s_ota_overlay;
static lv_obj_t *s_ota_bar;
static lv_obj_t *s_ota_label;

static const char *WDAY_ES[7]  = { "domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado" };
static const char *MONTH_ES[12] = { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio",
                                    "agosto", "septiembre", "octubre", "noviembre", "diciembre" };
static const char *CELL_TITLES[6] = { "Humedad", "Viento", "Sensación", "Mín / Máx", "Presión", "Índice UV" };

/* ----------------------------------------------------------------------- */
static void sky_gradient(sky_kind_t sky, lv_color_t *top, lv_color_t *bottom)
{
    switch (sky) {
    case SKY_SUNNY:         *top = lv_color_hex(0x1E6FD9); *bottom = lv_color_hex(0x6FB7FF); break;
    case SKY_CLEAR_NIGHT:   *top = lv_color_hex(0x0B1A3A); *bottom = lv_color_hex(0x1F3A6B); break;
    case SKY_PARTLY_CLOUDY: *top = lv_color_hex(0x3A7BD5); *bottom = lv_color_hex(0x8FB8E8); break;
    case SKY_CLOUDY:        *top = lv_color_hex(0x4B5A6E); *bottom = lv_color_hex(0x8A98AA); break;
    case SKY_RAIN:          *top = lv_color_hex(0x2B3E5C); *bottom = lv_color_hex(0x4E6A8C); break;
    case SKY_STORM:         *top = lv_color_hex(0x1B1F2E); *bottom = lv_color_hex(0x3C3F58); break;
    case SKY_SNOW:          *top = lv_color_hex(0x5C7FA8); *bottom = lv_color_hex(0xB8CCE3); break;
    case SKY_FOG:           *top = lv_color_hex(0x6B7480); *bottom = lv_color_hex(0xA9B0BA); break;
    case SKY_WINDY:         *top = lv_color_hex(0x2E8B8B); *bottom = lv_color_hex(0x7CC6C6); break;
    default:                *top = lv_color_hex(0x1E2530); *bottom = lv_color_hex(0x2E3A4A); break;
    }
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, lv_text_align_t align)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_align(l, align, 0);
    lv_label_set_text(l, "");
    return l;
}

static bool time_is_valid(void)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_year >= 124; /* >= 2024 */
}

static void fmt_temp(char *buf, size_t n, float v)
{
    /* one decimal only when it is not .0 */
    if (fabsf(v - roundf(v)) < 0.05f) {
        snprintf(buf, n, "%d", (int)lroundf(v));
    } else {
        snprintf(buf, n, "%.1f", v);
    }
}

/* ----------------------------------------------------------------------- */
void weather_ui_create(void)
{
    s_scr = lv_screen_active();
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(s_scr, LV_GRAD_DIR_VER, 0);

    const lv_color_t white = lv_color_white();
    const lv_color_t soft  = lv_color_hex(0xDCE6F5);

    /* --- top bar: clock (left), wifi + battery (right) ------------------ */
    s_time = make_label(s_scr, F_TITLE, white, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(s_time, LV_ALIGN_TOP_LEFT, 18, 12);

    s_battery = make_label(s_scr, F_SYMBOL, white, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -18, 18);

    s_wifi = make_label(s_scr, F_SYMBOL, white, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align_to(s_wifi, s_battery, LV_ALIGN_OUT_LEFT_MID, -10, 0);

    /* --- city and date ---------------------------------------------------- */
    s_city = make_label(s_scr, F_TITLE, white, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_city, SCREEN_W - 40);
    lv_label_set_long_mode(s_city, LV_LABEL_LONG_DOT);
    lv_obj_align(s_city, LV_ALIGN_TOP_MID, 0, 58);

    s_date = make_label(s_scr, F_BODY, soft, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_date, SCREEN_W - 40);
    lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, 92);

    /* --- icon (left) + temperature (right) ------------------------------- */
    s_icon = NULL; /* created in refresh */

    s_temp = make_label(s_scr, F_TEMP, white, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align(s_temp, LV_ALIGN_TOP_RIGHT, -44, 138);

    s_unit = make_label(s_scr, F_TITLE, soft, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(s_unit, LV_ALIGN_TOP_RIGHT, -14, 150);

    /* --- sky description ------------------------------------------------- */
    s_sky = make_label(s_scr, F_TITLE, white, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_sky, SCREEN_W - 30);
    lv_label_set_long_mode(s_sky, LV_LABEL_LONG_DOT);
    lv_obj_align(s_sky, LV_ALIGN_TOP_MID, 0, 262);

    s_desc = make_label(s_scr, F_SMALL, soft, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_desc, SCREEN_W - 30);
    lv_label_set_long_mode(s_desc, LV_LABEL_LONG_DOT);
    lv_obj_align(s_desc, LV_ALIGN_TOP_MID, 0, 294);

    /* --- details card: 3 x 2 grid ---------------------------------------- */
    s_card = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_card);
    lv_obj_remove_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_card, SCREEN_W - 28, 112);
    lv_obj_align(s_card, LV_ALIGN_TOP_MID, 0, 314);
    lv_obj_set_style_radius(s_card, 20, 0);
    lv_obj_set_style_bg_color(s_card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_card, LV_OPA_20, 0);

    const int32_t cw = (SCREEN_W - 28) / 3;
    for (int i = 0; i < 6; i++) {
        int col = i % 3, row = i / 3;
        s_cell_title[i] = make_label(s_card, F_SMALL, soft, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(s_cell_title[i], cw - 6);
        lv_obj_set_pos(s_cell_title[i], col * cw + 3, 10 + row * 54);
        lv_label_set_text(s_cell_title[i], CELL_TITLES[i]);

        s_cell_val[i] = make_label(s_card, F_BODY, white, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(s_cell_val[i], cw - 6);
        lv_label_set_long_mode(s_cell_val[i], LV_LABEL_LONG_DOT);
        lv_obj_set_pos(s_cell_val[i], col * cw + 3, 28 + row * 54);
        lv_label_set_text(s_cell_val[i], "--");
    }

    /* --- footer: last update + OTA address ------------------------------- */
    s_footer = make_label(s_scr, F_SMALL, soft, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_footer, SCREEN_W - 20);
    lv_obj_align(s_footer, LV_ALIGN_BOTTOM_MID, 0, -6);

    /* --- hint shown while no data has been received ---------------------- */
    s_hint = make_label(s_scr, F_SMALL, soft, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_hint, SCREEN_W - 30);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_hint,
                      "Esperando datos por USB serie...\n\n"
                      "/set_weather -city \"Madrid\"\n-temperature \"32ºC\" -sky \"sunny\"");
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 150);

    /* --- OTA overlay ----------------------------------------------------- */
    s_ota_overlay = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_ota_overlay);
    lv_obj_set_size(s_ota_overlay, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_ota_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_ota_overlay, LV_OPA_80, 0);
    lv_obj_add_flag(s_ota_overlay, LV_OBJ_FLAG_HIDDEN);

    s_ota_label = make_label(s_ota_overlay, F_TITLE, white, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_ota_label, SCREEN_W - 40);
    lv_label_set_text(s_ota_label, "Actualizando firmware");
    lv_obj_align(s_ota_label, LV_ALIGN_CENTER, 0, -40);

    s_ota_bar = lv_bar_create(s_ota_overlay);
    lv_obj_set_size(s_ota_bar, SCREEN_W - 80, 18);
    lv_obj_align(s_ota_bar, LV_ALIGN_CENTER, 0, 10);
    lv_bar_set_range(s_ota_bar, 0, 100);
    lv_obj_set_style_bg_color(s_ota_bar, lv_color_hex(0x3A4356), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ota_bar, lv_color_hex(0x4FA3FF), LV_PART_INDICATOR);

    weather_ui_refresh();
    weather_ui_tick();
}

/* ----------------------------------------------------------------------- */
void weather_ui_refresh(void)
{
    weather_data_t w;
    app_state_get_weather(&w);

    /* Background */
    lv_color_t top, bottom;
    sky_gradient(w.valid ? w.sky : SKY_UNKNOWN, &top, &bottom);
    lv_obj_set_style_bg_color(s_scr, top, 0);
    lv_obj_set_style_bg_grad_color(s_scr, bottom, 0);

    /* Icon */
    if (s_icon) {
        lv_obj_delete(s_icon);
        s_icon = NULL;
    }

    if (!w.valid) {
        lv_label_set_text(s_city, "Sin datos");
        lv_label_set_text(s_temp, "");
        lv_label_set_text(s_unit, "");
        lv_label_set_text(s_sky, "");
        lv_label_set_text(s_desc, "");
        lv_obj_add_flag(s_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_card, LV_OBJ_FLAG_HIDDEN);

    s_icon = weather_icon_create(s_scr, w.sky, ICON_SIZE);
    lv_obj_set_pos(s_icon, 14, 118);
    lv_obj_move_background(s_icon);   /* keep labels above the icon */
    lv_obj_move_foreground(s_ota_overlay);

    lv_label_set_text(s_city, w.city[0] ? w.city : "--");

    char buf[32];
    if (w.temperature_valid) {
        fmt_temp(buf, sizeof(buf), w.temperature);
        lv_label_set_text(s_temp, buf);
        lv_label_set_text_fmt(s_unit, "°%c", w.temp_unit);
    } else {
        lv_label_set_text(s_temp, "--");
        lv_label_set_text(s_unit, "");
    }

    const char *label = app_state_sky_label_es(w.sky);
    if (w.sky == SKY_UNKNOWN && w.sky_raw[0]) {
        label = w.sky_raw; /* unknown keyword: show what was sent */
    }
    lv_label_set_text(s_sky, label);
    lv_label_set_text(s_desc, w.description);

    /* Details */
    if (w.humidity >= 0) lv_label_set_text_fmt(s_cell_val[0], "%d %%", w.humidity);
    else                 lv_label_set_text(s_cell_val[0], "--");

    lv_label_set_text(s_cell_val[1], w.wind[0] ? w.wind : "--");

    if (w.feels_like_valid) {
        fmt_temp(buf, sizeof(buf), w.feels_like);
        lv_label_set_text_fmt(s_cell_val[2], "%s°", buf);
    } else {
        lv_label_set_text(s_cell_val[2], "--");
    }

    if (w.temp_min_valid || w.temp_max_valid) {
        char mn[16] = "--", mx[16] = "--";
        if (w.temp_min_valid) fmt_temp(mn, sizeof(mn), w.temp_min);
        if (w.temp_max_valid) fmt_temp(mx, sizeof(mx), w.temp_max);
        lv_label_set_text_fmt(s_cell_val[3], "%s° / %s°", mn, mx);
    } else {
        lv_label_set_text(s_cell_val[3], "--");
    }

    if (w.pressure > 0) lv_label_set_text_fmt(s_cell_val[4], "%d hPa", w.pressure);
    else                lv_label_set_text(s_cell_val[4], "--");

    if (w.uv_index >= 0) lv_label_set_text_fmt(s_cell_val[5], "%d", w.uv_index);
    else                 lv_label_set_text(s_cell_val[5], "--");
}

/* ----------------------------------------------------------------------- */
void weather_ui_tick(void)
{
    /* Clock and date */
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    if (time_is_valid()) {
        lv_label_set_text_fmt(s_time, "%02d:%02d", t.tm_hour, t.tm_min);
        lv_label_set_text_fmt(s_date, "%s, %d de %s", WDAY_ES[t.tm_wday], t.tm_mday, MONTH_ES[t.tm_mon]);
    } else {
        lv_label_set_text(s_time, "--:--");
        lv_label_set_text(s_date, "Hora no ajustada  (/set_time)");
    }

    /* Battery */
    battery_status_t b;
    app_state_get_battery(&b);
    if (!b.present) {
        lv_label_set_text(s_battery, "");
    } else if (!b.battery_present) {
        lv_label_set_text(s_battery, LV_SYMBOL_USB);
    } else {
        const char *sym = LV_SYMBOL_BATTERY_EMPTY;
        if (b.percent > 87)      sym = LV_SYMBOL_BATTERY_FULL;
        else if (b.percent > 62) sym = LV_SYMBOL_BATTERY_3;
        else if (b.percent > 37) sym = LV_SYMBOL_BATTERY_2;
        else if (b.percent > 12) sym = LV_SYMBOL_BATTERY_1;
        if (b.percent >= 0) {
            lv_label_set_text_fmt(s_battery, "%s%s %d%%", b.charging ? LV_SYMBOL_CHARGE " " : "", sym, b.percent);
        } else {
            lv_label_set_text_fmt(s_battery, "%s%s", b.charging ? LV_SYMBOL_CHARGE " " : "", sym);
        }
        lv_obj_set_style_text_color(s_battery,
                                    (b.percent >= 0 && b.percent <= 15 && !b.charging) ? lv_color_hex(0xFF6B6B) : lv_color_white(), 0);
    }
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -18, 18);

    /* Network */
    network_status_t n;
    app_state_get_network(&n);
    lv_label_set_text(s_wifi, n.sta_connected ? LV_SYMBOL_WIFI : (n.ap_active ? LV_SYMBOL_WIFI : ""));
    lv_obj_set_style_text_color(s_wifi, n.sta_connected ? lv_color_white() : lv_color_hex(0xB9C4D6), 0);
    lv_obj_align_to(s_wifi, s_battery, LV_ALIGN_OUT_LEFT_MID, -10, 0);

    /* Footer: last update + where to find the OTA page */
    weather_data_t w;
    app_state_get_weather(&w);
    char upd[64] = "Sin actualizar";
    bool stale = false;
    if (w.valid && w.updated_at > 0 && time_is_valid()) {
        long age = (long)(now - w.updated_at);
        if (age < 0) age = 0;
        struct tm tu;
        localtime_r(&w.updated_at, &tu);
        if (age < 60)        snprintf(upd, sizeof(upd), "Actualizado ahora");
        else if (age < 3600) snprintf(upd, sizeof(upd), "Actualizado hace %ld min", age / 60);
        else                 snprintf(upd, sizeof(upd), "Actualizado %02d:%02d (hace %ld h)", tu.tm_hour, tu.tm_min, age / 3600);
        stale = age > STALE_AFTER_S;
    } else if (w.valid) {
        snprintf(upd, sizeof(upd), "Datos recibidos");
    }
    const char *ip = n.sta_connected ? n.sta_ip : (n.ap_active ? n.ap_ip : NULL);
    if (ip) {
        lv_label_set_text_fmt(s_footer, "%s  ·  OTA: http://%s", upd, ip);
    } else {
        lv_label_set_text(s_footer, upd);
    }
    lv_obj_set_style_text_color(s_footer, stale ? lv_color_hex(0xFFB35C) : lv_color_hex(0xDCE6F5), 0);

    /* OTA overlay */
    int p = ota_progress_get();
    if (p >= 0) {
        lv_obj_remove_flag(s_ota_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_ota_bar, p, LV_ANIM_OFF);
        if (p >= 100) {
            lv_label_set_text(s_ota_label, "Firmware actualizado\nReiniciando...");
        } else {
            lv_label_set_text_fmt(s_ota_label, "Actualizando firmware\n%d %%", p);
        }
    } else {
        lv_obj_add_flag(s_ota_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}
