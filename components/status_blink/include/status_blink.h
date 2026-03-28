#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "led_strip.h"

typedef struct {
    // Configuration (set before calling status_blink_init)
    gpio_num_t rgb_gpio;           // RGB LED data pin (WS2812)
    gpio_num_t external_led_gpio;  // External single-color status LED
    // Runtime state (zero-initialize, managed internally)
    led_strip_handle_t led_strip;
    uint64_t last_blink_time;
    int blink_state;
} status_blink_t;

// Initialize the heartbeat LEDs
void status_blink_init(status_blink_t *dev);

// Call in the main loop — toggles the heartbeat every second
void status_blink_loop(status_blink_t *dev);
