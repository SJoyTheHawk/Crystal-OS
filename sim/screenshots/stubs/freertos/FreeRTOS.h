#pragma once
// Captures never launch firmware background tasks. Fail if invoked accidentally.
#include <cstdlib>
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
