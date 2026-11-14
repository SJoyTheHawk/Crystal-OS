#pragma once
#include "FreeRTOS.h"
inline void vTaskDeleteWithCaps(void *) { std::abort(); }
inline int xTaskCreateWithCaps(void (*)(void *), const char *, unsigned,
                               void *, unsigned, void *, unsigned)
{ std::abort(); }
