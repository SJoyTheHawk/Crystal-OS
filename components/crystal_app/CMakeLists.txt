idf_component_register(
    SRCS "src/crystal_app.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp-brookesia crystal_hal nvs_flash
)
