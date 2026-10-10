#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Returns the major component of the firmware version. */
uint8_t crystal_version_major(void);

/** Returns the minor component of the firmware version. */
uint8_t crystal_version_minor(void);

/** Returns the patch component of the firmware version. */
uint8_t crystal_version_patch(void);

/** Returns the complete firmware version string. */
const char *crystal_version_string(void);

#ifdef __cplusplus
}
#endif
