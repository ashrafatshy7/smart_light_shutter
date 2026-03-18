#include "driver/gpio.h"
#include "esp_zigbee_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>

#include "led_control.h"
#include "shutter_control.h"
#include "status_blink.h"
#include "zigbee_setup.h"

// ============================================================
// Device Instances — add new LEDs or shutters here
// ============================================================

// Heartbeat indicator (one per device, not Zigbee-controllable)
static status_blink_t heartbeat = {
    .rgb_gpio = 8,
    .external_led_gpio = 3,
};

// Zigbee-controllable LEDs
static led_control_t led1 = {.gpio = 19};
static led_control_t led2 = {.gpio = 4};
static led_control_t led3 = {.gpio = 10};

// All LEDs array — just add new LEDs here
static led_control_t *all_leds[] = {&led1, &led2, &led3};
static const int NUM_LEDS = sizeof(all_leds) / sizeof(all_leds[0]);

// Shutter report callback — bridges shutter events to Zigbee
static void shutter_report_cb(uint8_t endpoint, shutter_report_type_t type,
                              uint8_t value) {
  switch (type) {
  case SHUTTER_REPORT_POSITION:
    zigbee_report_shutter_position(endpoint, value);
    break;
  case SHUTTER_REPORT_STATUS:
    zigbee_report_shutter_status(endpoint, value);
    break;
  case SHUTTER_REPORT_TARGET:
    zigbee_report_shutter_target(endpoint, value);
    break;
  }
}

// Primary Shutter
static shutter_control_t shutter1 = {
    .relay_open_pin = 20,
    .relay_close_pin = 21,
    .report_cb = shutter_report_cb,
};

// All Shutters array — just add new shutters here
static shutter_control_t *all_shutters[] = {&shutter1};
static const int NUM_SHUTTERS = sizeof(all_shutters) / sizeof(all_shutters[0]);

// ============================================================
// Button Handling
// ============================================================

typedef enum {
  BTN_LIGHT = 0,
  BTN_OPEN,
  BTN_CLOSE,
  BTN_STOP,
  BTN_COUNT
} button_id_t;

typedef struct {
  gpio_num_t pin;
  int last_state;
} button_t;

static button_t buttons[BTN_COUNT] = {
    [BTN_LIGHT] = {.pin = 18, .last_state = 1},
    [BTN_OPEN] = {.pin = 22, .last_state = 1},
    [BTN_CLOSE] = {.pin = 23, .last_state = 1},
    [BTN_STOP] = {.pin = 15, .last_state = 1},
};

// Called when a button is pressed (falling edge detected)
static void handle_button_press(button_id_t id) {
  switch (id) {
  case BTN_LIGHT: {
    bool new_state = led_control_toggle_main(&led1);
    printf("Physical Light Button Pressed! LED is now %d\n", new_state);
    zigbee_report_onoff_state(1, new_state);
    break;
  }

  case BTN_OPEN:
    printf("Physical Open Button Pressed\n");
    shutter_open(&shutter1);
    break;
  case BTN_CLOSE:
    printf("Physical Close Button Pressed\n");
    shutter_close(&shutter1);
    break;
  case BTN_STOP:
    printf("Physical Stop Button Pressed\n");
    shutter_stop(&shutter1);
    break;
  default:
    break;
  }
}

// ============================================================
// Main Application
// ============================================================

void application_task(void *pvParameters) {
  while (1) {
    // Check all buttons for falling edge (1 -> 0)
    for (int i = 0; i < BTN_COUNT; i++) {
      int current = gpio_get_level(buttons[i].pin);
      if (buttons[i].last_state == 1 && current == 0) {
        handle_button_press((button_id_t)i);
      }
      buttons[i].last_state = current;
    }

    // Update all shutters
    for (int i = 0; i < NUM_SHUTTERS; i++) {
      shutter_update(all_shutters[i]);
    }

    // Heartbeat blink
    status_blink_loop(&heartbeat);

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void app_main(void) {
  // Zigbee strictly requires NVS flash to store network keys
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  // Initialize heartbeat
  status_blink_init(&heartbeat);

  // Initialize all LEDs
  for (int i = 0; i < NUM_LEDS; i++) {
    led_control_init(all_leds[i]);
  }

  // Initialize all shutters
  for (int i = 0; i < NUM_SHUTTERS; i++) {
    shutter_init(all_shutters[i]);
  }

  // Setup all buttons
  for (int i = 0; i < BTN_COUNT; i++) {
    gpio_reset_pin(buttons[i].pin);
    gpio_set_direction(buttons[i].pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(buttons[i].pin, GPIO_PULLUP_ONLY);
  }

  // Initialize Zigbee Network (pass all controllable devices)
  zigbee_init_and_start(all_leds, NUM_LEDS, all_shutters, NUM_SHUTTERS);

  // Start the application task
  xTaskCreate(application_task, "app_task", 4096, NULL, 5, NULL);

  // This keeps the Zigbee protocol stack running (infinite loop)
  esp_zb_stack_main_loop();
}