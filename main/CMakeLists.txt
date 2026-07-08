idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES ads129x ble_slecg slecg_app driver esp_timer
)
