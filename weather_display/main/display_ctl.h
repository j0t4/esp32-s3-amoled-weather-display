/**
 * display_ctl.h - Thin wrapper around the BSP display for other modules (console, web)
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Sets AMOLED brightness (5..100), persists it and is safe to call from any task */
void display_ctl_set_brightness(int percent);

#ifdef __cplusplus
}
#endif
