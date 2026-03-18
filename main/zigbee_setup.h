#pragma once
#include <stdbool.h>
#include <stdint.h>

// Initializes the Zigbee radio and starts looking for a ZHA Hub
void zigbee_init_and_start(void);

// Reports the On/Off state back to the Zigbee Hub
void zigbee_report_onoff_state(bool state);

// Reports the Shutter position (Lift Percentage) back to the Zigbee Hub
void zigbee_report_shutter_position(uint8_t percentage);

// Reports the movement status (0=Idle, 1=Opening, 2=Closing)
void zigbee_report_shutter_status(uint8_t status);

// Reports the target position intended by the user
void zigbee_report_shutter_target(uint8_t percentage);