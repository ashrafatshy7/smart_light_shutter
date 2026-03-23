#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    // Configuration (set before calling led_control_init)
    gpio_num_t gpio;         // The controllable LED pin
    gpio_num_t button_gpio;  // Physical button pin (-1 to disable)
    // Runtime state (zero-initialize, managed internally)
    bool state;
    int button_last_state;
    TaskHandle_t identify_task_handle;
    uint16_t identify_seconds;
} led_control_t;

// Initialize the LED pin
void led_control_init(led_control_t *dev);

// Toggle on/off, returns new state
bool led_control_toggle_main(led_control_t *dev);

// Get current state
bool led_control_get_main_state(led_control_t *dev);

// Set on/off explicitly
void led_control_set_main_state(led_control_t *dev, bool state);

// Blink for identify (Zigbee identify command)
void led_control_start_identify(led_control_t *dev, uint16_t seconds);