#pragma once

#include <stdint.h>

// Brightness uses whole percentages. Round to nearest percent so a nonzero
// 1% preference stays visible; an explicit 0% remains off.
constexpr uint8_t crystal_effective_brightness(uint8_t requested, bool saving)
{
    return saving ? static_cast<uint8_t>((requested * 60u + 50u) / 100u) : requested;
}
