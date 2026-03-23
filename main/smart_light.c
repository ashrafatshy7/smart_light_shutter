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

// Zigbee-controllable LEDs (each with its own optional button)
static led_control_t led1 = {.gpio = 19, .button_gpio = 18};
static led_control_t led2 = {.gpio = 4, .button_gpio = -1};
static led_control_t led3 = {.gpio = 10, .button_gpio = -1};

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

// Shutters (each with its own open/close/stop buttons)
static shutter_control_t shutter1 = {
    .relay_open_pin = 20,
    .relay_close_pin = 21,
    .button_open_gpio = 22,
    .button_close_gpio = 23,
    .button_stop_gpio = 15,
    .report_cb = shutter_report_cb,
};

// All Shutters array — just add new shutters here
static shutter_control_t *all_shutters[] = {&shutter1};
static const int NUM_SHUTTERS = sizeof(all_shutters) / sizeof(all_shutters[0]);

// ============================================================
// Button helpers
// ============================================================

// Initialize a single button GPIO (if valid)
static void init_button_pin(gpio_num_t pin) {
  if (pin >= 0) {
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
  }
}

// Check for falling edge (1 -> 0), returns true if pressed
static bool check_button(gpio_num_t pin, int *last_state) {
  if (pin < 0) return false;
  int current = gpio_get_level(pin);
  bool pressed = (*last_state == 1 && current == 0);
  *last_state = current;
  return pressed;
}

// ============================================================
// Main Application
// ============================================================

void application_task(void *pvParameters) {
  while (1) {
    // Check LED buttons
    for (int i = 0; i < NUM_LEDS; i++) {
      led_control_t *led = all_leds[i];
      if (check_button(led->button_gpio, &led->button_last_state)) {
        bool new_state = led_control_toggle_main(led);
        printf("Physical Light Button (LED %d) Pressed! State: %d\n",
               i + 1, new_state);
        // Endpoint = i + 1 (LED endpoints start at 1)
        zigbee_report_onoff_state(i + 1, new_state);
      }
    }

    // Check shutter buttons
    for (int i = 0; i < NUM_SHUTTERS; i++) {
      shutter_control_t *sh = all_shutters[i];

      if (check_button(sh->button_open_gpio, &sh->button_open_last_state)) {
        printf("Physical Open Button (Shutter %d) Pressed\n", i + 1);
        shutter_open(sh);
      }
      if (check_button(sh->button_close_gpio, &sh->button_close_last_state)) {
        printf("Physical Close Button (Shutter %d) Pressed\n", i + 1);
        shutter_close(sh);
      }
      if (check_button(sh->button_stop_gpio, &sh->button_stop_last_state)) {
        printf("Physical Stop Button (Shutter %d) Pressed\n", i + 1);
        shutter_stop(sh);
      }

      // Update shutter timing
      shutter_update(sh);
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

  // Initialize all LEDs + their buttons
  for (int i = 0; i < NUM_LEDS; i++) {
    led_control_init(all_leds[i]);
    all_leds[i]->button_last_state = 1; // Pullup default
    init_button_pin(all_leds[i]->button_gpio);
  }

  // Initialize all shutters + their buttons
  for (int i = 0; i < NUM_SHUTTERS; i++) {
    shutter_init(all_shutters[i]);
    all_shutters[i]->button_open_last_state = 1;
    all_shutters[i]->button_close_last_state = 1;
    all_shutters[i]->button_stop_last_state = 1;
    init_button_pin(all_shutters[i]->button_open_gpio);
    init_button_pin(all_shutters[i]->button_close_gpio);
    init_button_pin(all_shutters[i]->button_stop_gpio);
  }

  // Initialize Zigbee Network (pass all controllable devices)
  zigbee_init_and_start(all_leds, NUM_LEDS, all_shutters, NUM_SHUTTERS);

  // Start the application task
  xTaskCreate(application_task, "app_task", 4096, NULL, 5, NULL);

  // This keeps the Zigbee protocol stack running (infinite loop)
  esp_zb_stack_main_loop();
}