#include "weather_icons.h"
#include <math.h>

#define C_SUN        lv_color_hex(0xFFC93C)
#define C_SUN_RAY    lv_color_hex(0xFFB300)
#define C_CLOUD      lv_color_hex(0xF4F6FA)
#define C_CLOUD_GREY lv_color_hex(0xC9D2E0)
#define C_CLOUD_DARK lv_color_hex(0x6B7A90)
#define C_RAIN       lv_color_hex(0x4FA3FF)
#define C_SNOW       lv_color_hex(0xFFFFFF)
#define C_FOG        lv_color_hex(0xB8C2D0)
#define C_BOLT       lv_color_hex(0xFFD93B)
#define C_MOON       lv_color_hex(0xF2E9C4)
#define C_CRATER     lv_color_hex(0xD9CFA6)
#define C_WIND       lv_color_hex(0xE6F2F2)

/* ----------------------------------------------------------------------- */
/* Small helpers                                                            */
/* ----------------------------------------------------------------------- */
static lv_obj_t *blank(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *circle(lv_obj_t *parent, int32_t cx, int32_t cy, int32_t d, lv_color_t color)
{
    lv_obj_t *o = blank(parent);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, cx - d / 2, cy - d / 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *rrect(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, lv_color_t color)
{
    lv_obj_t *o = blank(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static void free_points_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

/* Polyline with `n` points. Points are copied to heap and freed when the object is deleted. */
static lv_obj_t *line(lv_obj_t *parent, const lv_point_precise_t *pts, uint32_t n, int32_t width, lv_color_t color)
{
    lv_point_precise_t *copy = lv_malloc(sizeof(lv_point_precise_t) * n);
    if (!copy) {
        return NULL;
    }
    for (uint32_t i = 0; i < n; i++) {
        copy[i] = pts[i];
    }
    lv_obj_t *l = lv_line_create(parent);
    lv_obj_remove_style_all(l);
    lv_line_set_points(l, copy, n);
    lv_obj_add_event_cb(l, free_points_cb, LV_EVENT_DELETE, copy);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, color, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_style_line_opa(l, LV_OPA_COVER, 0);
    return l;
}

static void seg(lv_obj_t *parent, float x0, float y0, float x1, float y1, int32_t width, lv_color_t color)
{
    lv_point_precise_t p[2] = { { (lv_value_precise_t)x0, (lv_value_precise_t)y0 },
                                { (lv_value_precise_t)x1, (lv_value_precise_t)y1 } };
    line(parent, p, 2, width, color);
}

/* ----------------------------------------------------------------------- */
/* Icon parts                                                               */
/* ----------------------------------------------------------------------- */
static void draw_sun(lv_obj_t *p, float cx, float cy, float r, float s)
{
    /* 8 rays */
    for (int i = 0; i < 8; i++) {
        float a = (float)i * (float)M_PI / 4.0f;
        float r0 = r * 1.35f, r1 = r * 1.75f;
        seg(p, cx + cosf(a) * r0, cy + sinf(a) * r0, cx + cosf(a) * r1, cy + sinf(a) * r1,
            (int32_t)(s * 0.05f) + 1, C_SUN_RAY);
    }
    circle(p, (int32_t)cx, (int32_t)cy, (int32_t)(2 * r), C_SUN);
}

/* Cloud centred at (cx, cy) with total width w */
static void draw_cloud(lv_obj_t *p, float cx, float cy, float w, lv_color_t color)
{
    float h = w * 0.36f;
    rrect(p, (int32_t)(cx - w / 2), (int32_t)(cy - h * 0.25f), (int32_t)w, (int32_t)h, (int32_t)(h / 2), color);
    circle(p, (int32_t)(cx + w * 0.05f), (int32_t)(cy - w * 0.10f), (int32_t)(w * 0.50f), color);
    circle(p, (int32_t)(cx - w * 0.22f), (int32_t)(cy - w * 0.02f), (int32_t)(w * 0.36f), color);
}

static void draw_rain(lv_obj_t *p, float cx, float cy, float w)
{
    int32_t lw = (int32_t)(w * 0.06f) + 1;
    for (int i = -1; i <= 1; i++) {
        float x = cx + i * w * 0.22f;
        seg(p, x + w * 0.04f, cy, x - w * 0.04f, cy + w * 0.20f, lw, C_RAIN);
    }
}

static void draw_snow(lv_obj_t *p, float cx, float cy, float w)
{
    for (int i = -1; i <= 1; i++) {
        float x = cx + i * w * 0.24f;
        float y = cy + (i == 0 ? w * 0.14f : w * 0.06f);
        circle(p, (int32_t)x, (int32_t)y, (int32_t)(w * 0.11f), C_SNOW);
    }
}

static void draw_bolt(lv_obj_t *p, float cx, float cy, float w)
{
    lv_point_precise_t pts[4] = {
        { cx + w * 0.06f, cy },
        { cx - w * 0.06f, cy + w * 0.17f },
        { cx + w * 0.04f, cy + w * 0.17f },
        { cx - w * 0.08f, cy + w * 0.36f },
    };
    line(p, pts, 4, (int32_t)(w * 0.07f) + 1, C_BOLT);
}

static void draw_fog_lines(lv_obj_t *p, float cx, float cy, float w)
{
    int32_t lw = (int32_t)(w * 0.06f) + 1;
    seg(p, cx - w * 0.45f, cy,             cx + w * 0.45f, cy,             lw, C_FOG);
    seg(p, cx - w * 0.35f, cy + w * 0.14f, cx + w * 0.30f, cy + w * 0.14f, lw, C_FOG);
    seg(p, cx - w * 0.20f, cy + w * 0.28f, cx + w * 0.40f, cy + w * 0.28f, lw, C_FOG);
}

static void draw_moon(lv_obj_t *p, float cx, float cy, float r)
{
    circle(p, (int32_t)cx, (int32_t)cy, (int32_t)(2 * r), C_MOON);
    circle(p, (int32_t)(cx - r * 0.35f), (int32_t)(cy - r * 0.25f), (int32_t)(r * 0.45f), C_CRATER);
    circle(p, (int32_t)(cx + r * 0.30f), (int32_t)(cy + r * 0.35f), (int32_t)(r * 0.30f), C_CRATER);
    circle(p, (int32_t)(cx + r * 0.25f), (int32_t)(cy - r * 0.45f), (int32_t)(r * 0.20f), C_CRATER);
    /* a few stars */
    circle(p, (int32_t)(cx - r * 1.55f), (int32_t)(cy - r * 1.2f), (int32_t)(r * 0.16f), C_SNOW);
    circle(p, (int32_t)(cx + r * 1.60f), (int32_t)(cy - r * 0.6f), (int32_t)(r * 0.12f), C_SNOW);
    circle(p, (int32_t)(cx - r * 1.35f), (int32_t)(cy + r * 1.1f), (int32_t)(r * 0.10f), C_SNOW);
}

static void draw_wind(lv_obj_t *p, float cx, float cy, float w)
{
    int32_t lw = (int32_t)(w * 0.065f) + 1;
    lv_point_precise_t a[4] = { { cx - w * 0.45f, cy - w * 0.16f }, { cx + w * 0.25f, cy - w * 0.16f },
                                { cx + w * 0.40f, cy - w * 0.26f }, { cx + w * 0.30f, cy - w * 0.36f } };
    lv_point_precise_t b[4] = { { cx - w * 0.35f, cy + w * 0.02f }, { cx + w * 0.42f, cy + w * 0.02f },
                                { cx + w * 0.50f, cy + w * 0.12f }, { cx + w * 0.42f, cy + w * 0.20f } };
    lv_point_precise_t c[2] = { { cx - w * 0.45f, cy + w * 0.22f }, { cx + w * 0.10f, cy + w * 0.22f } };
    line(p, a, 4, lw, C_WIND);
    line(p, b, 4, lw, C_WIND);
    line(p, c, 2, lw, C_WIND);
}

/* ----------------------------------------------------------------------- */
lv_obj_t *weather_icon_create(lv_obj_t *parent, sky_kind_t kind, int32_t size)
{
    lv_obj_t *box = blank(parent);
    lv_obj_set_size(box, size, size);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    const float s = (float)size;
    const float cx = s / 2, cy = s / 2;

    switch (kind) {
    case SKY_SUNNY:
        draw_sun(box, cx, cy, s * 0.26f, s);
        break;
    case SKY_CLEAR_NIGHT:
        draw_moon(box, cx, cy, s * 0.30f);
        break;
    case SKY_PARTLY_CLOUDY:
        draw_sun(box, cx + s * 0.14f, cy - s * 0.16f, s * 0.19f, s);
        draw_cloud(box, cx - s * 0.05f, cy + s * 0.14f, s * 0.64f, C_CLOUD);
        break;
    case SKY_CLOUDY:
        draw_cloud(box, cx + s * 0.12f, cy - s * 0.12f, s * 0.55f, C_CLOUD_GREY);
        draw_cloud(box, cx - s * 0.06f, cy + s * 0.14f, s * 0.70f, C_CLOUD);
        break;
    case SKY_RAIN:
        draw_cloud(box, cx, cy - s * 0.10f, s * 0.70f, C_CLOUD);
        draw_rain(box, cx, cy + s * 0.18f, s * 0.70f);
        break;
    case SKY_STORM:
        draw_cloud(box, cx, cy - s * 0.12f, s * 0.72f, C_CLOUD_DARK);
        draw_bolt(box, cx + s * 0.02f, cy + s * 0.08f, s * 0.72f);
        draw_rain(box, cx - s * 0.20f, cy + s * 0.16f, s * 0.35f);
        break;
    case SKY_SNOW:
        draw_cloud(box, cx, cy - s * 0.10f, s * 0.70f, C_CLOUD);
        draw_snow(box, cx, cy + s * 0.20f, s * 0.70f);
        break;
    case SKY_FOG:
        draw_cloud(box, cx, cy - s * 0.14f, s * 0.66f, C_CLOUD_GREY);
        draw_fog_lines(box, cx, cy + s * 0.12f, s * 0.70f);
        break;
    case SKY_WINDY:
        draw_wind(box, cx, cy, s * 0.80f);
        break;
    default: {
        lv_obj_t *q = lv_label_create(box);
        lv_label_set_text(q, "?");
        lv_obj_set_style_text_color(q, C_CLOUD_GREY, 0);
        lv_obj_set_style_text_font(q, LV_FONT_DEFAULT, 0);
        lv_obj_center(q);
        break;
    }
    }
    return box;
}
