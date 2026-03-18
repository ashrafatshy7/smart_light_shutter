#include <stdint.h>
#include <stdbool.h>
#include "shutter_control.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "SHUTTER_CONTROL";

static void report(shutter_control_t *dev, shutter_report_type_t type, uint8_t value) {
    if (dev->report_cb) {
        dev->report_cb(dev->zigbee_endpoint, type, value);
    }
}

void shutter_init(shutter_control_t *dev) {
    gpio_reset_pin(dev->relay_open_pin);
    gpio_set_direction(dev->relay_open_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(dev->relay_open_pin, 0);

    gpio_reset_pin(dev->relay_close_pin);
    gpio_set_direction(dev->relay_close_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(dev->relay_close_pin, 0);

    dev->last_update_us = esp_timer_get_time();
    dev->current_position_us = 0; // Start Closed
}

void shutter_update(shutter_control_t *dev) {
    uint64_t now = esp_timer_get_time();
    uint64_t delta = now - dev->last_update_us;
    dev->last_update_us = now;

    if (dev->state == SHUTTER_OPENING) {
        dev->current_position_us += delta;
        if (dev->current_position_us >= SHUTTER_TOTAL_TIME_US) {
            dev->current_position_us = SHUTTER_TOTAL_TIME_US;
            shutter_stop(dev);
            ESP_LOGI(TAG, "Shutter Fully Open");
        }
        
        if (dev->moving_to_target && dev->current_position_us >= dev->target_position_us) {
            shutter_stop(dev);
            ESP_LOGI(TAG, "Reached Target Open Position");
        }
    } else if (dev->state == SHUTTER_CLOSING) {
        if (dev->current_position_us > delta) {
            dev->current_position_us -= delta;
        } else {
            dev->current_position_us = 0;
            shutter_stop(dev);
            ESP_LOGI(TAG, "Shutter Fully Closed");
        }

        if (dev->moving_to_target && dev->current_position_us <= dev->target_position_us) {
            shutter_stop(dev);
            ESP_LOGI(TAG, "Reached Target Closed Position");
        }
    }

    // Periodic position updates while moving (every 1 second)
    if (dev->state != SHUTTER_IDLE) {
        if (now - dev->last_report_us >= 1000000ULL) { 
            report(dev, SHUTTER_REPORT_POSITION, shutter_get_position(dev));
            dev->last_report_us = now;
        }
    }
}

void shutter_open(shutter_control_t *dev) {
    dev->moving_to_target = false;
    if (dev->current_position_us >= SHUTTER_TOTAL_TIME_US) {
        ESP_LOGI(TAG, "Already Fully Open, ignoring button");
        return;
    }

    // Software Interlock
    gpio_set_level(dev->relay_close_pin, 0);
    gpio_set_level(dev->relay_open_pin, 1);
    
    dev->state = SHUTTER_OPENING;
    ESP_LOGI(TAG, "Opening Shutter...");
    
    // Notify via callback
    report(dev, SHUTTER_REPORT_STATUS, 0x05);
    report(dev, SHUTTER_REPORT_TARGET, 0);
    dev->last_report_us = esp_timer_get_time();
}

void shutter_close(shutter_control_t *dev) {
    dev->moving_to_target = false;
    if (dev->current_position_us == 0) {
        ESP_LOGI(TAG, "Already Fully Closed, ignoring button");
        return;
    }

    // Software Interlock
    gpio_set_level(dev->relay_open_pin, 0);
    gpio_set_level(dev->relay_close_pin, 1);

    dev->state = SHUTTER_CLOSING;
    ESP_LOGI(TAG, "Closing Shutter...");

    // Notify via callback
    report(dev, SHUTTER_REPORT_STATUS, 0x0A);
    report(dev, SHUTTER_REPORT_TARGET, 100);
    dev->last_report_us = esp_timer_get_time();
}

void shutter_stop(shutter_control_t *dev) {
    if (dev->state == SHUTTER_IDLE && !dev->moving_to_target) {
        return; // Already stopped
    }
    
    gpio_set_level(dev->relay_open_pin, 0);
    gpio_set_level(dev->relay_close_pin, 0);
    dev->state = SHUTTER_IDLE;
    dev->moving_to_target = false;
    ESP_LOGI(TAG, "Shutter Stopped at %d%%", shutter_get_position(dev));
    
    // Notify via callback
    uint8_t pos = shutter_get_position(dev);
    report(dev, SHUTTER_REPORT_POSITION, pos);
    report(dev, SHUTTER_REPORT_TARGET, pos);
    report(dev, SHUTTER_REPORT_STATUS, 0);
}

void shutter_set_position(shutter_control_t *dev, uint8_t percentage) {
    ESP_LOGI(TAG, "shutter_set_position called with %d%%", percentage);
    if (percentage > 100) percentage = 100;
    
    // Zigbee: 0=Open, 100=Closed
    // Internal: SHUTTER_TOTAL_TIME_US=Open, 0=Closed
    dev->target_position_us = (uint64_t)(((100.0 - percentage) / 100.0) * SHUTTER_TOTAL_TIME_US);
    
    if (dev->target_position_us == dev->current_position_us) {
        shutter_stop(dev);
        return;
    }

    dev->moving_to_target = true;
    if (dev->target_position_us > dev->current_position_us) {
        // Moving towards OPEN
        gpio_set_level(dev->relay_close_pin, 0);
        gpio_set_level(dev->relay_open_pin, 1);
        dev->state = SHUTTER_OPENING;
    } else {
        // Moving towards CLOSED
        gpio_set_level(dev->relay_open_pin, 0);
        gpio_set_level(dev->relay_close_pin, 1);
        dev->state = SHUTTER_CLOSING;
    }
    ESP_LOGI(TAG, "Moving to %d%%", percentage);

    // Notify via callback
    report(dev, SHUTTER_REPORT_STATUS, dev->target_position_us > dev->current_position_us ? 0x05 : 0x0A);
    report(dev, SHUTTER_REPORT_TARGET, percentage);
    dev->last_report_us = esp_timer_get_time();
}

shutter_state_t shutter_get_state(shutter_control_t *dev) {
    return dev->state;
}

uint8_t shutter_get_position(shutter_control_t *dev) {
    // Zigbee standard: 0% = Fully Open, 100% = Fully Closed
    // Internal: current_position_us = 0 is Closed, SHUTTER_TOTAL_TIME_US is Open
    return (uint8_t)(((SHUTTER_TOTAL_TIME_US - dev->current_position_us) * 100ULL) / SHUTTER_TOTAL_TIME_US);
}
