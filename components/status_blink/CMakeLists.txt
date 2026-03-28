idf_component_register(
    SRCS "status_blink.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer esp_common led_strip
)
