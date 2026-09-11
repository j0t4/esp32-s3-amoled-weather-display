/**
 * weather_ui.h - LVGL weather card for the 368x448 AMOLED
 *
 * All functions must be called from the LVGL task or with the LVGL lock held.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the screen on the active display */
void weather_ui_create(void);

/* Re-read the whole application state and redraw the card (icon, texts, colours) */
void weather_ui_refresh(void);

/* Cheap 1 Hz update: clock, "updated N min ago", battery, network, OTA overlay */
void weather_ui_tick(void);

#ifdef __cplusplus
}
#endif
