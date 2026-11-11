idf_component_register(
    SRCS "src/crystal_signature.cpp" "src/crystal_crypto.cpp"
    INCLUDE_DIRS "include"
    REQUIRES crystal_hal esphome__libsodium
    PRIV_REQUIRES bootloader_support esp_hw_support
)
