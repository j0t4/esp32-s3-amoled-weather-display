/**
 * ota_progress.h - Tiny lock-free channel between the HTTP OTA handler and the UI
 */
#pragma once

#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

extern atomic_int g_ota_progress;   /* -1: idle, 0..100: percent, 100 = done (rebooting) */

static inline int  ota_progress_get(void)      { return atomic_load(&g_ota_progress); }
static inline void ota_progress_set(int value) { atomic_store(&g_ota_progress, value); }

#ifdef __cplusplus
}
#endif
