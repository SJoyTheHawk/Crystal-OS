idf_component_register(SRCS "src/weather_app.cpp" "src/weather_glyph.c" "src/weather_icon.c"
                       INCLUDE_DIRS "include"
                       REQUIRES crystal_app crystal_core lvgl__lvgl)
