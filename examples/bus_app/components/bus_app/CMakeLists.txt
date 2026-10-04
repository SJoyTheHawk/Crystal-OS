idf_component_register(
    SRCS "src/bus_app.cpp"
         "src/bus_icon.c"
    INCLUDE_DIRS "include"
    REQUIRES crystal_app crystal_core bus_service lvgl__lvgl
)
