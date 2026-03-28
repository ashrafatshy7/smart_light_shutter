idf_component_register(
    SRCS "zigbee_setup.c"
    INCLUDE_DIRS "include"
    REQUIRES led_control shutter_control esp-zigbee-lib esp-zboss-lib
)
