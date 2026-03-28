idf_component_register(
    SRCS "shutter_control.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer esp_common
)
