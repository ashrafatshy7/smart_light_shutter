#pragma once
#include <stdbool.h>
#include <stdint.h>

// Initialize all LED pins and the RGB strip
void led_control_init(void);

// Flips the main light on/off and returns the new state
bool led_control_toggle_main(void);

// Gets the current state of the light (for the web server)
bool led_control_get_main_state(void);

// Handles the background blinking
void led_control_blink_loop(void);

// Explicitly sets the light on or off
void led_control_set_main_state(bool state);

// Starts the identify blinking sequence
void led_control_start_identify(uint16_t seconds);