#include "driver/gpio.h"
#include "esp_zigbee_core.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

#include "led_control.h"
#include "zigbee_setup.h"
#include "shutter_control.h"

#define BUTTON_LIGHT_PIN 18
#define BUTTON_OPEN_PIN 22
#define BUTTON_CLOSE_PIN 23
#define BUTTON_STOP_PIN 15

void application_task(void *pvParameters) {
  int last_light_btn = 1;
  int last_open_btn = 1;
  int last_close_btn = 1;
  int last_stop_btn = 1;

  while (1) {
    // Handle physical button presses (Light)
    int current_light_state = gpio_get_level(BUTTON_LIGHT_PIN);
    if (last_light_btn == 1 && current_light_state == 0) {
      bool new_state = led_control_toggle_main();
      printf("Physical Light Button Pressed! LED is now %d\n", new_state);
      zigbee_report_onoff_state(new_state);
    }
    last_light_btn = current_light_state;

    // Handle Shutter Buttons (Edge Triggered)
    int current_open_state = gpio_get_level(BUTTON_OPEN_PIN);
    if (last_open_btn == 1 && current_open_state == 0) {
        printf("Physical Open Button Pressed\n");
        shutter_open();
    }
    last_open_btn = current_open_state;

    int current_close_state = gpio_get_level(BUTTON_CLOSE_PIN);
    if (last_close_btn == 1 && current_close_state == 0) {
        printf("Physical Close Button Pressed\n");
        shutter_close();
    }
    last_close_btn = current_close_state;

    int current_stop_state = gpio_get_level(BUTTON_STOP_PIN);
    if (last_stop_btn == 1 && current_stop_state == 0) {
        printf("Physical Stop Button Pressed\n");
        shutter_stop();
    }
    last_stop_btn = current_stop_state;

    // Update Shutter Timing
    shutter_update();

    led_control_blink_loop();

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

  led_control_init();
  shutter_init();

  // Setup Buttons
  gpio_reset_pin(BUTTON_LIGHT_PIN);
  gpio_set_direction(BUTTON_LIGHT_PIN, GPIO_MODE_INPUT);
  gpio_set_pull_mode(BUTTON_LIGHT_PIN, GPIO_PULLUP_ONLY);

  gpio_reset_pin(BUTTON_OPEN_PIN);
  gpio_set_direction(BUTTON_OPEN_PIN, GPIO_MODE_INPUT);
  gpio_set_pull_mode(BUTTON_OPEN_PIN, GPIO_PULLUP_ONLY);

  gpio_reset_pin(BUTTON_CLOSE_PIN);
  gpio_set_direction(BUTTON_CLOSE_PIN, GPIO_MODE_INPUT);
  gpio_set_pull_mode(BUTTON_CLOSE_PIN, GPIO_PULLUP_ONLY);

  gpio_reset_pin(BUTTON_STOP_PIN);
  gpio_set_direction(BUTTON_STOP_PIN, GPIO_MODE_INPUT);
  gpio_set_pull_mode(BUTTON_STOP_PIN, GPIO_PULLUP_ONLY);

  // Initialize Zigbee Network
  zigbee_init_and_start();

  // Start the application task for button, blinking and shutter
  xTaskCreate(application_task, "app_task", 4096, NULL, 5, NULL);

  // This keeps the Zigbee protocol stack running (infinite loop)
  esp_zb_stack_main_loop();
}