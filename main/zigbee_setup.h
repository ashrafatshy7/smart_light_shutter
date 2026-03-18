#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "led_control.h"
#include "shutter_control.h"

#define ZIGBEE_MAX_LEDS     8
#define ZIGBEE_MAX_SHUTTERS 8

// Initializes the Zigbee radio, registers all devices, and starts networking
void zigbee_init_and_start(led_control_t *leds[], int num_leds,
                           shutter_control_t *shutters[], int num_shutters);

// Reports the On/Off state for a specific LED endpoint
void zigbee_report_onoff_state(uint8_t endpoint, bool state);

// Reports the Shutter position for a specific shutter endpoint
void zigbee_report_shutter_position(uint8_t endpoint, uint8_t percentage);

// Reports the movement status for a specific shutter endpoint
void zigbee_report_shutter_status(uint8_t endpoint, uint8_t status);

// Reports the target position for a specific shutter endpoint
void zigbee_report_shutter_target(uint8_t endpoint, uint8_t percentage);