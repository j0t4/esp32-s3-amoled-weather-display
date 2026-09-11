/**
 * status_json.h - Builds the JSON document served at /api/status and printed by /status
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns a malloc'ed string (free() it) or NULL. `pretty` adds indentation. */
char *status_json_build(bool pretty);

#ifdef __cplusplus
}
#endif
