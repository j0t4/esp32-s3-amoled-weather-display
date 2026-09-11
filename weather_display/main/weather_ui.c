/**
 * weather_ui.c - Retro terminal-style weather card (368x448)
 *
 * Dark background, VT323 monospace font, inverted title bar, square panels with a thick border.
 * A single accent colour, chosen from the sky condition, tints the title bar, borders and labels.
 */
#include "weather_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "app_state.h"
#include "weather_icons.h"
#include "ota_progress.h"

LV_FONT_DECLARE(font_vt323_24);
LV_FONT_DECLARE(font_vt323_30);
LV_FONT_DECLARE(font_vt323_42);
LV_FONT_DECLARE(font_vt323_100);

#define F_S      (&font_vt323_24)          /* labels, date, footer      (9.6 px/char) */
#define F_M      (&font_vt323_30)          /* clock                     (12 px/char)  */
#define F_L      (&font_vt323_42)          /* city, sky, unit           (16.8 px/char)*/
#define F_XL     (&font_vt323_100)         /* temperature               (40 px/char)  */
#define F_SYMBOL (&lv_font_montserrat_20)  /* built-in: LV_SYMBOL_* glyphs */

#define SCREEN_W   368
#define SCREEN_H   448
#define BAR_H      40
#define ICON_SIZE  130
#define MARGIN     12
#define BORDER     3

#define STALE_AFTER_S   (3 * 3600)

/* Palette ----------------------------------------------------------------- */
#define C_BG        lv_color_hex(0x07080D)
#define C_PANEL     lv_color_hex(0x10141F)
#define C_TEXT      lv_color_hex(0xF2EFE4)
#define C_TEXT_DIM  lv_color_hex(0xA8A79C)
#define C_WARN      lv_color_hex(0xFF8A3D)
#define C_ALERT     lv_color_hex(0xFF5C5C)

static lv_obj_t *s_scr;
static lv_obj_t *s_bar;
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
static lv_obj_t *s_panel;
static lv_obj_t *s_cell_label[6];
static lv_obj_t *s_cell_value[6];
static lv_obj_t *s_footer;
static lv_obj_t *s_hint;
static lv_obj_t *s_ota_overlay;
static lv_obj_t *s_ota_box;
static lv_obj_t *s_ota_bar;
static lv_obj_t *s_ota_label;
static lv_color_t s_accent;

static const char *WDAY_ES[7]   = { "domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado" };
static const char *MONTH_ES[12] = { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio",
                                    "agosto", "septiembre", "octubre", "noviembre", "diciembre" };
/* Panel cells: column-major (left column rows 0-2, right column rows 3-5) */
static const char *CELL_LABELS[6] = { "HUMEDAD", "SENSACIÓN", "PRESIÓN", "VIENTO", "MÍN/MÁX", "UV" };

/* ----------------------------------------------------------------------- */
static lv_color_t sky_accent(sky_kind_t sky)
{
    switch (sky) {
    case SKY_SUNNY:         return lv_color_hex(0xFFB000);   /* amber phosphor */
    case SKY_CLEAR_NIGHT:   return lv_color_hex(0x8FD3FF);
    case SKY_PARTLY_CLOUDY: return lv_color_hex(0xFFC857);
    case SKY_CLOUDY:        return lv_color_hex(0xC7CCD6);
    case SKY_RAIN:          return lv_color_hex(0x4FB3FF);
    case SKY_STORM:         return lv_color_hex(0xFFE04D);
    case SKY_SNOW:          return lv_color_hex(0xDDF3FF);
    case SKY_FOG:           return lv_color_hex(0xB0B8C4);
    case SKY_WINDY:         return lv_color_hex(0x5FE0C0);
    default:                return lv_color_hex(0x33FF66);   /* green phosphor when idle */
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

static lv_obj_t *make_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static bool time_is_valid(void)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_year >= 124;
}

static void fmt_temp(char *buf, size_t n, float v)
{
    if (fabsf(v - roundf(v)) < 0.05f) {
        snprintf(buf, n, "%d", (int)lroundf(v));
    } else {
        snprintf(buf, n, "%.1f", v);
    }
}

static void apply_accent(lv_color_t accent)
{
    s_accent = accent;
    lv_obj_set_style_bg_color(s_bar, accent, 0);
    lv_obj_set_style_border_color(s_panel, accent, 0);
    lv_obj_set_style_text_color(s_date, accent, 0);
    lv_obj_set_style_text_color(s_unit, accent, 0);
    lv_obj_set_style_text_color(s_sky, accent, 0);
    lv_obj_set_style_border_color(s_ota_box, accent, 0);
    lv_obj_set_style_bg_color(s_ota_bar, accent, LV_PART_INDICATOR);
    for (int i = 0; i < 6; i++) {
        lv_obj_set_style_text_color(s_cell_label[i], accent, 0);
    }
}

/* ----------------------------------------------------------------------- */
void weather_ui_create(void)
{
    s_scr = lv_screen_active();
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, C_BG, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);

    /* --- inverted title bar: clock left, wifi + battery right ------------- */
    s_bar = make_box(s_scr, 0, 0, SCREEN_W, BAR_H);

    s_time = make_label(s_bar, F_M, C_BG, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(s_time, LV_ALIGN_LEFT_MID, MARGIN, 0);

    s_battery = make_label(s_bar, F_SYMBOL, C_BG, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align(s_battery, LV_ALIGN_RIGHT_MID, -MARGIN, 0);

    s_wifi = make_label(s_bar, F_SYMBOL, C_BG, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align_to(s_wifi, s_battery, LV_ALIGN_OUT_LEFT_MID, -12, 0);

    /* --- city + date ------------------------------------------------------ */
    s_city = make_label(s_scr, F_L, C_TEXT, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_city, SCREEN_W - 2 * MARGIN);
    lv_label_set_long_mode(s_city, LV_LABEL_LONG_DOT);
    lv_obj_align(s_city, LV_ALIGN_TOP_MID, 0, 50);

    s_date = make_label(s_scr, F_S, C_TEXT_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_date, SCREEN_W - 2 * MARGIN);
    lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, 96);

    /* --- icon left, temperature right ------------------------------------- */
    s_icon = NULL;

    s_unit = make_label(s_scr, F_L, C_TEXT, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(s_unit, LV_ALIGN_TOP_RIGHT, -MARGIN - 4, 146);

    s_temp = make_label(s_scr, F_XL, C_TEXT, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_width(s_temp, 170);
    lv_obj_align(s_temp, LV_ALIGN_TOP_RIGHT, -MARGIN - 44, 128);

    /* --- sky text --------------------------------------------------------- */
    s_sky = make_label(s_scr, F_L, C_TEXT, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_sky, SCREEN_W - 2 * MARGIN);
    lv_label_set_long_mode(s_sky, LV_LABEL_LONG_DOT);
    lv_obj_align(s_sky, LV_ALIGN_TOP_MID, 0, 262);

    s_desc = make_label(s_scr, F_S, C_TEXT_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_desc, SCREEN_W - 2 * MARGIN);
    lv_label_set_long_mode(s_desc, LV_LABEL_LONG_DOT);
    lv_obj_align(s_desc, LV_ALIGN_TOP_MID, 0, 306);

    /* --- details panel: 2 columns x 3 rows, "LABEL      value" ------------ */
    const int32_t panel_y = 334, panel_h = 86;
    s_panel = make_box(s_scr, MARGIN, panel_y, SCREEN_W - 2 * MARGIN, panel_h);
    lv_obj_set_style_bg_color(s_panel, C_PANEL, 0);
    lv_obj_set_style_border_width(s_panel, BORDER, 0);
    lv_obj_set_style_border_opa(s_panel, LV_OPA_COVER, 0);

    const int32_t inner_w = SCREEN_W - 2 * MARGIN - 2 * BORDER;
    const int32_t col_w = inner_w / 2;
    const int32_t pad = 8;
    for (int i = 0; i < 6; i++) {
        int col = i / 3, row = i % 3;
        int32_t x0 = col * col_w + pad;
        int32_t y0 = 5 + row * 26;

        s_cell_label[i] = make_label(s_panel, F_S, C_TEXT, LV_TEXT_ALIGN_LEFT);
        lv_obj_set_pos(s_cell_label[i], x0, y0);
        lv_label_set_text(s_cell_label[i], CELL_LABELS[i]);

        s_cell_value[i] = make_label(s_panel, F_S, C_TEXT, LV_TEXT_ALIGN_RIGHT);
        lv_obj_set_width(s_cell_value[i], col_w - 2 * pad - 62);
        lv_label_set_long_mode(s_cell_value[i], LV_LABEL_LONG_DOT);
        lv_obj_set_pos(s_cell_value[i], x0 + 62, y0);
        lv_label_set_text(s_cell_value[i], "--");
    }
    /* thin divider between the two columns */
    lv_obj_t *div = make_box(s_panel, col_w - 1, 4, 2, panel_h - 2 * BORDER - 8);
    lv_obj_set_style_bg_color(div, lv_color_hex(0x2A3040), 0);

    /* --- footer ------------------------------------------------------------ */
    s_footer = make_label(s_scr, F_S, C_TEXT_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_footer, SCREEN_W - 2 * MARGIN);
    lv_label_set_long_mode(s_footer, LV_LABEL_LONG_DOT);
    lv_obj_align(s_footer, LV_ALIGN_TOP_MID, 0, 423);

    /* --- hint while no data ----------------------------------------------- */
    s_hint = make_label(s_scr, F_S, C_TEXT, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_hint, SCREEN_W - 2 * MARGIN);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_hint,
                      "ESPERANDO DATOS POR USB SERIE\n\n"
                      "/set_weather -city \"Madrid\"\n"
                      "  -temperature \"32ºC\" -sky \"sunny\"\n\n"
                      "o abre la web de la placa (ver pie)");
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 150);

    /* --- OTA overlay --------------------------------------------------------- */
    s_ota_overlay = make_box(s_scr, 0, 0, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_ota_overlay, C_BG, 0);
    lv_obj_set_style_bg_opa(s_ota_overlay, LV_OPA_90, 0);
    lv_obj_add_flag(s_ota_overlay, LV_OBJ_FLAG_HIDDEN);

    s_ota_box = make_box(s_ota_overlay, MARGIN, 0, SCREEN_W - 2 * MARGIN, 150);
    lv_obj_align(s_ota_box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_ota_box, C_PANEL, 0);
    lv_obj_set_style_border_width(s_ota_box, BORDER, 0);
    lv_obj_set_style_border_opa(s_ota_box, LV_OPA_COVER, 0);

    s_ota_label = make_label(s_ota_box, F_M, C_TEXT, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_ota_label, SCREEN_W - 2 * MARGIN - 20);
    lv_label_set_text(s_ota_label, "ACTUALIZANDO FIRMWARE");
    lv_obj_align(s_ota_label, LV_ALIGN_TOP_MID, 0, 16);

    s_ota_bar = lv_bar_create(s_ota_box);
    lv_obj_set_size(s_ota_bar, SCREEN_W - 2 * MARGIN - 40, 22);
    lv_obj_align(s_ota_bar, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_bar_set_range(s_ota_bar, 0, 100);
    lv_obj_set_style_radius(s_ota_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ota_bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_ota_bar, lv_color_hex(0x2A3040), LV_PART_MAIN);

    apply_accent(sky_accent(SKY_UNKNOWN));
    weather_ui_refresh();
    weather_ui_tick();
}

/* ----------------------------------------------------------------------- */
void weather_ui_refresh(void)
{
    weather_data_t w;
    app_state_get_weather(&w);

    apply_accent(sky_accent(w.valid ? w.sky : SKY_UNKNOWN));

    if (s_icon) {
        lv_obj_delete(s_icon);
        s_icon = NULL;
    }

    if (!w.valid) {
        lv_label_set_text(s_city, "SIN DATOS");
        lv_label_set_text(s_temp, "");
        lv_label_set_text(s_unit, "");
        lv_label_set_text(s_sky, "");
        lv_label_set_text(s_desc, "");
        lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);

    s_icon = weather_icon_create(s_scr, w.sky, ICON_SIZE);
    lv_obj_set_pos(s_icon, 16, 128);
    lv_obj_move_background(s_icon);
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
        label = w.sky_raw;
    }
    lv_label_set_text(s_sky, label);
    lv_label_set_text(s_desc, w.description);

    /* Left column: humidity, feels like, pressure */
    if (w.humidity >= 0) lv_label_set_text_fmt(s_cell_value[0], "%d %%", w.humidity);
    else                 lv_label_set_text(s_cell_value[0], "--");

    if (w.feels_like_valid) {
        fmt_temp(buf, sizeof(buf), w.feels_like);
        lv_label_set_text_fmt(s_cell_value[1], "%s°", buf);
    } else {
        lv_label_set_text(s_cell_value[1], "--");
    }

    if (w.pressure > 0) lv_label_set_text_fmt(s_cell_value[2], "%d hPa", w.pressure);
    else                lv_label_set_text(s_cell_value[2], "--");

    /* Right column: wind, min/max, uv */
    lv_label_set_text(s_cell_value[3], w.wind[0] ? w.wind : "--");

    if (w.temp_min_valid || w.temp_max_valid) {
        char mn[16] = "--", mx[16] = "--";
        if (w.temp_min_valid) fmt_temp(mn, sizeof(mn), w.temp_min);
        if (w.temp_max_valid) fmt_temp(mx, sizeof(mx), w.temp_max);
        lv_label_set_text_fmt(s_cell_value[4], "%s°/%s°", mn, mx);
    } else {
        lv_label_set_text(s_cell_value[4], "--");
    }

    if (w.uv_index >= 0) lv_label_set_text_fmt(s_cell_value[5], "%d", w.uv_index);
    else                 lv_label_set_text(s_cell_value[5], "--");
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
        lv_label_set_text(s_date, "hora no ajustada  (/set_time)");
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
    }
    bool low = b.battery_present && b.percent >= 0 && b.percent <= 15 && !b.charging;
    lv_obj_set_style_text_color(s_battery, low ? C_ALERT : C_BG, 0);
    lv_obj_align(s_battery, LV_ALIGN_RIGHT_MID, -MARGIN, 0);

    /* Network */
    network_status_t n;
    app_state_get_network(&n);
    lv_label_set_text(s_wifi, (n.sta_connected || n.ap_active) ? LV_SYMBOL_WIFI : "");
    lv_obj_set_style_text_opa(s_wifi, n.sta_connected ? LV_OPA_COVER : LV_OPA_60, 0);
    lv_obj_align_to(s_wifi, s_battery, LV_ALIGN_OUT_LEFT_MID, -12, 0);

    /* Footer: last update + OTA address */
    weather_data_t w;
    app_state_get_weather(&w);
    char upd[48] = "sin actualizar";
    bool stale = false;
    if (w.valid && w.updated_at > 0 && time_is_valid()) {
        long age = (long)(now - w.updated_at);
        if (age < 0) age = 0;
        struct tm tu;
        localtime_r(&w.updated_at, &tu);
        if (age < 60)        snprintf(upd, sizeof(upd), "act. ahora");
        else if (age < 3600) snprintf(upd, sizeof(upd), "act. hace %ld min", age / 60);
        else                 snprintf(upd, sizeof(upd), "act. %02d:%02d (%ld h)", tu.tm_hour, tu.tm_min, age / 3600);
        stale = age > STALE_AFTER_S;
    } else if (w.valid) {
        snprintf(upd, sizeof(upd), "datos recibidos");
    }
    const char *ip = n.sta_connected ? n.sta_ip : (n.ap_active ? n.ap_ip : NULL);
    if (ip) {
        lv_label_set_text_fmt(s_footer, "%s  ·  http://%s", upd, ip);
    } else {
        lv_label_set_text(s_footer, upd);
    }
    lv_obj_set_style_text_color(s_footer, stale ? C_WARN : C_TEXT_DIM, 0);

    /* OTA overlay */
    int p = ota_progress_get();
    if (p >= 0) {
        lv_obj_remove_flag(s_ota_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_ota_bar, p, LV_ANIM_OFF);
        if (p >= 100) {
            lv_label_set_text(s_ota_label, "FIRMWARE ACTUALIZADO\nreiniciando...");
        } else {
            lv_label_set_text_fmt(s_ota_label, "ACTUALIZANDO FIRMWARE\n%d %%", p);
        }
    } else {
        lv_obj_add_flag(s_ota_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}
