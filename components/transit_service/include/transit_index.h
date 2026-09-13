/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

// Route-index refresh and the persisted index it installs.
//
// The compiled baseline in transit_index_data.c always works, so everything here
// is an improvement on it rather than a dependency. A device that never
// refreshes -- offline for months, or a full SPIFFS -- keeps the baseline
// indefinitely with no degraded mode and no nag.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRANSIT_INDEX_PATH "/spiffs/transit/routes.idx"

// Loads the persisted index, if any, over the compiled baseline. Called once at
// first use of the lookup functions. A file that fails its header, checksum, or
// sortedness check is deleted rather than used: a corrupt index would grey out
// real routes with no way for the user to tell why.
void transit_index_load(void);

// Runs one scheduled check if one is due. Called from the worker's idle tick, so
// never on the boot path. Returns true when a check actually ran.
//
// Policy, from the proposal §3.6: online only, app not in the foreground, KMB at
// most daily by ETag, CTB at most weekly by body hash. Neither feed deserves a
// daily 112 KB.
bool transit_index_maybe_refresh(bool online, bool foreground);

// Epoch of the last successful confirmation, 0 when never confirmed. Drives the
// Device Status "Route list" row.
int32_t transit_index_checked_at(void);

#ifdef __cplusplus
}
#endif
