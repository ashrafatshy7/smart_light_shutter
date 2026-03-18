#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    // Configuration (set before calling led_control_init)
    gpio_num_t rgb_gpio;           // RGB LED data pin (-1 to disable)
    gpio_num_t external_led_gpio;  // External status LED pin (-1 to disable)
    gpio_num_t main_led_gpio;      // Main controllable LED pin
    // Runtime state (zero-initialize, managed internally)
    bool main_light_state;
    led_strip_handle_t led_strip;
    uint64_t last_blink_time;
    int blink_state;
    TaskHandle_t identify_task_handle;
    uint16_t identify_seconds;
} led_control_t;

void led_control_init(led_control_t *dev);
bool led_control_toggle_main(led_control_t *dev);
bool led_control_get_main_state(led_control_t *dev);
void led_control_blink_loop(led_control_t *dev);
void led_control_set_main_state(led_control_t *dev, bool state);
void led_control_start_identify(led_control_t *dev, uint16_t seconds);