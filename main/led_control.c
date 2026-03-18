#include "led_control.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#define BLINK_GPIO 8
#define BLICK_EXTERNAL_LED 3
#define LED_PIN 19

static bool main_light_state = false;
static led_strip_handle_t led_strip;
static uint64_t last_blink_time = 0;
static int blink_state = 0;

void led_control_init(void) {
  // 1. Setup RGB LED
  led_strip_config_t strip_config = {
      .strip_gpio_num = BLINK_GPIO,
      .max_leds = 1,
  };
  led_strip_rmt_config_t rmt_config = {
      .resolution_hz = 10 * 1000 * 1000,
      .flags.with_dma = false,
  };
  ESP_ERROR_CHECK(
      led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
  led_strip_clear(led_strip);

  // 2. Setup Standard LEDs
  gpio_reset_pin(BLICK_EXTERNAL_LED);
  gpio_set_direction(BLICK_EXTERNAL_LED, GPIO_MODE_OUTPUT);
  gpio_reset_pin(LED_PIN);
  gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
}

bool led_control_toggle_main(void) {
  main_light_state = !main_light_state;
  gpio_set_level(LED_PIN, main_light_state);
  return main_light_state;
}

bool led_control_get_main_state(void) { return main_light_state; }

void led_control_blink_loop(void) {
  uint64_t current_time = esp_timer_get_time();
  if (current_time - last_blink_time >= 1000000) {
    last_blink_time = current_time;
    blink_state = !blink_state;

    if (blink_state == 1) {
      led_strip_set_pixel(led_strip, 0, 125, 0, 20);
      led_strip_refresh(led_strip);
      gpio_set_level(BLICK_EXTERNAL_LED, 0);
    } else {
      led_strip_clear(led_strip);
      gpio_set_level(BLICK_EXTERNAL_LED, 1);
    }
  }
}

void led_control_set_main_state(bool state) {
  main_light_state = state;
  gpio_set_level(LED_PIN, main_light_state);
}

static TaskHandle_t identify_task_handle = NULL;

static void identify_task(void *pvParameters) {
  uint16_t seconds = (uint16_t)(uintptr_t)pvParameters;

  // Save the original state to restore it after identifying
  bool original_state = main_light_state;

  // Blink once per second for the requested duration
  for (int i = 0; i < seconds; i++) {
    // Turn ON (or toggle opposite of original)
    gpio_set_level(LED_PIN, !original_state);
    vTaskDelay(pdMS_TO_TICKS(500));

    // Restore to original
    gpio_set_level(LED_PIN, original_state);
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  identify_task_handle = NULL;
  vTaskDelete(NULL); // Kill the task when done
}

void led_control_start_identify(uint16_t seconds) {
  // If it's already identifying, don't start a duplicate task
  if (identify_task_handle != NULL) {
    return;
  }

  // Create an independent task so it doesn't block your main loops
  xTaskCreate(identify_task, "identify_task", 2048, (void *)(uintptr_t)seconds,
              5, &identify_task_handle);
}