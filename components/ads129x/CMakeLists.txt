idf_component_register(
    SRCS "ads129x.c"
    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/main"
    REQUIRES driver esp_timer
)
