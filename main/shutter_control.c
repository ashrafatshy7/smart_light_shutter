#include <stdint.h>
#include <stdbool.h>
#include "shutter_control.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "zigbee_setup.h"

static const char *TAG = "SHUTTER_CONTROL";

#define RELAY_OPEN_PIN 20
#define RELAY_CLOSE_PIN 21

static shutter_state_t current_state = SHUTTER_IDLE;
static uint64_t current_position_us = 0; // 0 = Closed, SHUTTER_TOTAL_TIME_US = Open
static uint64_t last_update_us = 0;
static uint64_t target_position_us = 0;
static bool moving_to_target = false;
static uint64_t last_report_us = 0; // NEW: For periodic reporting

void shutter_init(void) {
    gpio_reset_pin(RELAY_OPEN_PIN);
    gpio_set_direction(RELAY_OPEN_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RELAY_OPEN_PIN, 0);

    gpio_reset_pin(RELAY_CLOSE_PIN);
    gpio_set_direction(RELAY_CLOSE_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RELAY_CLOSE_PIN, 0);

    last_update_us = esp_timer_get_time();
    current_position_us = 0; // Start Closed
}

void shutter_update(void) {
    uint64_t now = esp_timer_get_time();
    uint64_t delta = now - last_update_us;
    last_update_us = now;

    if (current_state == SHUTTER_OPENING) {
        current_position_us += delta;
        if (current_position_us >= SHUTTER_TOTAL_TIME_US) {
            current_position_us = SHUTTER_TOTAL_TIME_US;
            shutter_stop();
            ESP_LOGI(TAG, "Shutter Fully Open");
        }
        
        if (moving_to_target && current_position_us >= target_position_us) {
            shutter_stop();
            ESP_LOGI(TAG, "Reached Target Open Position");
        }
    } else if (current_state == SHUTTER_CLOSING) {
        if (current_position_us > delta) {
            current_position_us -= delta;
        } else {
            current_position_us = 0;
            shutter_stop();
            ESP_LOGI(TAG, "Shutter Fully Closed");
        }

        if (moving_to_target && current_position_us <= target_position_us) {
            shutter_stop();
            ESP_LOGI(TAG, "Reached Target Closed Position");
        }
    }

    // Periodic position updates to Zigbee while moving (every 1 second)
    if (current_state != SHUTTER_IDLE) {
        if (now - last_report_us >= 1000000ULL) { 
            zigbee_report_shutter_position(shutter_get_position());
            last_report_us = now;
        }
    }
}

void shutter_open(void) {
    moving_to_target = false;
    if (current_position_us >= SHUTTER_TOTAL_TIME_US) {
        ESP_LOGI(TAG, "Already Fully Open, ignoring button");
        return;
    }

    // Software Interlock
    gpio_set_level(RELAY_CLOSE_PIN, 0);
    gpio_set_level(RELAY_OPEN_PIN, 1);
    
    current_state = SHUTTER_OPENING;
    ESP_LOGI(TAG, "Opening Shutter...");
    
    // Notify Zigbee we are now moving
    // Status 0x01 | (0x01 << 2) = 0x05 (Global Opening | Lift Opening)
    zigbee_report_shutter_status(0x05); 
    zigbee_report_shutter_target(0);  // Moving to fully open (0%)
    last_report_us = esp_timer_get_time();
}

void shutter_close(void) {
    moving_to_target = false;
    if (current_position_us == 0) {
        ESP_LOGI(TAG, "Already Fully Closed, ignoring button");
        return;
    }

    // Software Interlock
    gpio_set_level(RELAY_OPEN_PIN, 0);
    gpio_set_level(RELAY_CLOSE_PIN, 1);

    current_state = SHUTTER_CLOSING;
    ESP_LOGI(TAG, "Closing Shutter...");

    // Notify Zigbee we are now moving
    // Status 0x02 | (0x02 << 2) = 0x0A (Global Closing | Lift Closing)
    zigbee_report_shutter_status(0x0A); 
    zigbee_report_shutter_target(100); // Moving to fully closed (100%)
    last_report_us = esp_timer_get_time();
}

void shutter_stop(void) {
    if (current_state == SHUTTER_IDLE && !moving_to_target) {
        return; // Already stopped
    }
    
    gpio_set_level(RELAY_OPEN_PIN, 0);
    gpio_set_level(RELAY_CLOSE_PIN, 0);
    current_state = SHUTTER_IDLE;
    moving_to_target = false;
    ESP_LOGI(TAG, "Shutter Stopped at %d%%", shutter_get_position());
    
    // Notify Zigbee of the final position and that we've stopped
    uint8_t pos = shutter_get_position();
    zigbee_report_shutter_position(pos);
    zigbee_report_shutter_target(pos); // Sync target with current on stop
    zigbee_report_shutter_status(0); // 0 = Idle/Stopped
}

void shutter_set_position(uint8_t percentage) {
    ESP_LOGI(TAG, "shutter_set_position called with %d%%", percentage);
    if (percentage > 100) percentage = 100;
    
    // Zigbee: 0=Open, 100=Closed
    // Internal: SHUTTER_TOTAL_TIME_US=Open, 0=Closed
    target_position_us = (uint64_t)(((100.0 - percentage) / 100.0) * SHUTTER_TOTAL_TIME_US);
    
    if (target_position_us == current_position_us) {
        shutter_stop();
        return;
    }

    moving_to_target = true;
    if (target_position_us > current_position_us) {
        // Moving towards OPEN
        gpio_set_level(RELAY_CLOSE_PIN, 0);
        gpio_set_level(RELAY_OPEN_PIN, 1);
        current_state = SHUTTER_OPENING;
    } else {
        // Moving towards CLOSED
        gpio_set_level(RELAY_OPEN_PIN, 0);
        gpio_set_level(RELAY_CLOSE_PIN, 1);
        current_state = SHUTTER_CLOSING;
    }
    ESP_LOGI(TAG, "Moving to %d%%", percentage);

    // Notify Zigbee of movement and target
    // Opening: 0x05, Closing: 0x0A
    zigbee_report_shutter_status(target_position_us > current_position_us ? 0x05 : 0x0A);
    zigbee_report_shutter_target(percentage);
    last_report_us = esp_timer_get_time();
}

shutter_state_t shutter_get_state(void) {
    return current_state;
}

uint8_t shutter_get_position(void) {
    // Zigbee standard: 0% = Fully Open, 100% = Fully Closed
    // Internal: current_position_us = 0 is Closed, SHUTTER_TOTAL_TIME_US is Open
    return (uint8_t)(((SHUTTER_TOTAL_TIME_US - current_position_us) * 100ULL) / SHUTTER_TOTAL_TIME_US);
}
