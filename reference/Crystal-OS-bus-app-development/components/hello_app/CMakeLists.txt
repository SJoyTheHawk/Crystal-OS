idf_component_register(
    SRCS
        "src/hello_app.cpp"
        "src/hello_icon.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl__lvgl crystal_app
)
