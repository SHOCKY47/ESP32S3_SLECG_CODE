idf_component_register(
    SRCS "ble_slecg.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash bt
)
