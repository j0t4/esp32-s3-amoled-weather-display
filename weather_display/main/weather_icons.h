/**
 * weather_icons.h - Weather icons drawn with LVGL primitives (no image assets needed)
 */
#pragma once

#include "lvgl.h"
#include "app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a square icon of `size` px for the given sky condition.
 * The returned object is a transparent container; position it with lv_obj_set_pos / align.
 * Must be called with the LVGL lock held.
 */
lv_obj_t *weather_icon_create(lv_obj_t *parent, sky_kind_t kind, int32_t size);

#ifdef __cplusplus
}
#endif
