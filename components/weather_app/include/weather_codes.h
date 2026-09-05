#pragma once
#include <stdint.h>
typedef enum WeatherGlyph {
    WEATHER_GLYPH_NEUTRAL = 0, WEATHER_GLYPH_SUN, WEATHER_GLYPH_SUN_CLOUD,
    WEATHER_GLYPH_CLOUD, WEATHER_GLYPH_FOG, WEATHER_GLYPH_DRIZZLE,
    WEATHER_GLYPH_RAIN, WEATHER_GLYPH_SNOW, WEATHER_GLYPH_STORM
} WeatherGlyph;
typedef struct WeatherGroup { uint8_t lo, hi; const char *label; WeatherGlyph glyph; } WeatherGroup;
static inline WeatherGroup weather_group_for_code(uint8_t code)
{
    static const WeatherGroup groups[] = {
        {0,0,"Clear",WEATHER_GLYPH_SUN}, {1,2,"Partly cloudy",WEATHER_GLYPH_SUN_CLOUD},
        {3,3,"Overcast",WEATHER_GLYPH_CLOUD}, {45,48,"Fog",WEATHER_GLYPH_FOG},
        {51,57,"Drizzle",WEATHER_GLYPH_DRIZZLE}, {61,67,"Rain",WEATHER_GLYPH_RAIN},
        {71,77,"Snow",WEATHER_GLYPH_SNOW}, {80,82,"Rain showers",WEATHER_GLYPH_RAIN},
        {85,86,"Snow showers",WEATHER_GLYPH_SNOW}, {95,99,"Thunderstorm",WEATHER_GLYPH_STORM}
    };
    for (unsigned i = 0; i < sizeof(groups) / sizeof(groups[0]); ++i)
        if (code >= groups[i].lo && code <= groups[i].hi) return groups[i];
    WeatherGroup unknown = {0,0,"Unknown",WEATHER_GLYPH_NEUTRAL};
    return unknown;
}
