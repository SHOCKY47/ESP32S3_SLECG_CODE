idf_component_register(
    SRCS
        "slecg_app.c"
        "slecg_fsm.c"
        "slecg_button.c"
        "slecg_led.c"
        "slecg_ecg.c"
        "slecg_transport.c"
        "slecg_uart_stream.c"
        "slecg_status.c"
        "slecg_proto_handler.c"
        "slecg_nvs.c"
    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/main"
    REQUIRES slecg_proto ads129x ble_slecg driver freertos esp_timer nvs_flash log
)
