#include "crystal_version.h"

#include <stdio.h>

/* PROJECT_VER is generated from project(crystal_os VERSION ...). */
#ifndef PROJECT_VER
#define PROJECT_VER "0.12.14"
#endif
#define CRYSTAL_VERSION_STRING PROJECT_VER

static char s_version_buf[] = CRYSTAL_VERSION_STRING;

uint8_t crystal_version_major(void)
{
    unsigned int major = 0;
    (void)sscanf(CRYSTAL_VERSION_STRING, "%u", &major);
    return (uint8_t)major;
}

uint8_t crystal_version_minor(void)
{
    unsigned int major = 0;
    unsigned int minor = 0;
    (void)sscanf(CRYSTAL_VERSION_STRING, "%u.%u", &major, &minor);
    return (uint8_t)minor;
}

uint8_t crystal_version_patch(void)
{
    unsigned int major = 0;
    unsigned int minor = 0;
    unsigned int patch = 0;
    (void)sscanf(CRYSTAL_VERSION_STRING, "%u.%u.%u", &major, &minor, &patch);
    return (uint8_t)patch;
}

const char *crystal_version_string(void)
{
    return s_version_buf;
}
