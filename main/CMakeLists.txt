idf_component_register(
    SRCS "smart_light.c"
    INCLUDE_DIRS "."
    REQUIRES
        led_control
        shutter_control
        status_blink
        zigbee_setup
        driver
        freertos
        nvs_flash
        esp-zigbee-lib
)