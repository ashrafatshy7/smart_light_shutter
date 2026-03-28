#ifndef SHUTTER_CONTROL_H
#define SHUTTER_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"

#define SHUTTER_TOTAL_TIME_US (17 * 1000000ULL)

typedef enum {
    SHUTTER_IDLE,
    SHUTTER_OPENING,
    SHUTTER_CLOSING
} shutter_state_t;

typedef enum {
    SHUTTER_REPORT_POSITION,
    SHUTTER_REPORT_STATUS,
    SHUTTER_REPORT_TARGET,
    SHUTTER_REPORT_ALL_STOPPED,  // value = current pos; sends status+pos+target in one lock cycle
} shutter_report_type_t;

// Callback now includes endpoint so the reporter knows which Zigbee endpoint to update
typedef void (*shutter_report_cb_t)(uint8_t endpoint, shutter_report_type_t type, uint8_t value);

typedef struct {
    // Configuration (set before calling shutter_init)
    gpio_num_t relay_open_pin;
    gpio_num_t relay_close_pin;
    gpio_num_t button_open_gpio;    // Physical open button (-1 to disable)
    gpio_num_t button_close_gpio;   // Physical close button (-1 to disable)
    gpio_num_t button_stop_gpio;    // Physical stop button (-1 to disable)
    uint8_t zigbee_endpoint;        // Zigbee endpoint assigned to this shutter
    shutter_report_cb_t report_cb;  // Optional callback for Zigbee reporting
    // Runtime state (zero-initialize, managed internally)
    shutter_state_t state;
    uint64_t current_position_us;
    uint64_t last_update_us;
    uint64_t target_position_us;
    bool moving_to_target;
    uint64_t last_report_us;
    int button_open_last_state;
    int button_close_last_state;
    int button_stop_last_state;
} shutter_control_t;

void shutter_init(shutter_control_t *dev);
void shutter_update(shutter_control_t *dev);
void shutter_open(shutter_control_t *dev);
void shutter_close(shutter_control_t *dev);
void shutter_stop(shutter_control_t *dev);
void shutter_set_position(shutter_control_t *dev, uint8_t percentage);
shutter_state_t shutter_get_state(shutter_control_t *dev);
uint8_t shutter_get_position(shutter_control_t *dev);

#endif // SHUTTER_CONTROL_H
