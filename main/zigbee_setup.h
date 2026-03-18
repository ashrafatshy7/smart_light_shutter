#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "led_control.h"
#include "shutter_control.h"

// Initializes the Zigbee radio, registers the given devices, and starts networking
void zigbee_init_and_start(led_control_t *led, shutter_control_t *shutter);

// Reports the On/Off state back to the Zigbee Hub
void zigbee_report_onoff_state(bool state);

// Reports the Shutter position (Lift Percentage) back to the Zigbee Hub
void zigbee_report_shutter_position(uint8_t percentage);

// Reports the movement status (0=Idle, 1=Opening, 2=Closing)
void zigbee_report_shutter_status(uint8_t status);

// Reports the target position intended by the user
void zigbee_report_shutter_target(uint8_t percentage);