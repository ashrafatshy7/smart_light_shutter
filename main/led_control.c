#include "led_control.h"
#include "esp_timer.h"

void led_control_init(led_control_t *dev) {
  // 1. Setup RGB LED (if configured)
  if (dev->rgb_gpio >= 0) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = dev->rgb_gpio,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(
        led_strip_new_rmt_device(&strip_config, &rmt_config, &dev->led_strip));
    led_strip_clear(dev->led_strip);
  }

  // 2. Setup External LED (if configured)
  if (dev->external_led_gpio >= 0) {
    gpio_reset_pin(dev->external_led_gpio);
    gpio_set_direction(dev->external_led_gpio, GPIO_MODE_OUTPUT);
  }

  // 3. Setup Main LED
  gpio_reset_pin(dev->main_led_gpio);
  gpio_set_direction(dev->main_led_gpio, GPIO_MODE_OUTPUT);
}

bool led_control_toggle_main(led_control_t *dev) {
  dev->main_light_state = !dev->main_light_state;
  gpio_set_level(dev->main_led_gpio, dev->main_light_state);
  return dev->main_light_state;
}

bool led_control_get_main_state(led_control_t *dev) {
  return dev->main_light_state;
}

void led_control_blink_loop(led_control_t *dev) {
  uint64_t current_time = esp_timer_get_time();
  if (current_time - dev->last_blink_time >= 1000000) {
    dev->last_blink_time = current_time;
    dev->blink_state = !dev->blink_state;

    if (dev->blink_state == 1) {
      if (dev->led_strip) {
        led_strip_set_pixel(dev->led_strip, 0, 125, 0, 20);
        led_strip_refresh(dev->led_strip);
      }
      if (dev->external_led_gpio >= 0) {
        gpio_set_level(dev->external_led_gpio, 0);
      }
    } else {
      if (dev->led_strip) {
        led_strip_clear(dev->led_strip);
      }
      if (dev->external_led_gpio >= 0) {
        gpio_set_level(dev->external_led_gpio, 1);
      }
    }
  }
}

void led_control_set_main_state(led_control_t *dev, bool state) {
  dev->main_light_state = state;
  gpio_set_level(dev->main_led_gpio, dev->main_light_state);
}

static void identify_task(void *pvParameters) {
  led_control_t *dev = (led_control_t *)pvParameters;
  uint16_t seconds = dev->identify_seconds;

  // Save the original state to restore it after identifying
  bool original_state = dev->main_light_state;

  // Blink once per second for the requested duration
  for (int i = 0; i < seconds; i++) {
    // Turn ON (or toggle opposite of original)
    gpio_set_level(dev->main_led_gpio, !original_state);
    vTaskDelay(pdMS_TO_TICKS(500));

    // Restore to original
    gpio_set_level(dev->main_led_gpio, original_state);
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  dev->identify_task_handle = NULL;
  vTaskDelete(NULL); // Kill the task when done
}

void led_control_start_identify(led_control_t *dev, uint16_t seconds) {
  // If it's already identifying, don't start a duplicate task
  if (dev->identify_task_handle != NULL) {
    return;
  }

  dev->identify_seconds = seconds;
  // Create an independent task so it doesn't block your main loops
  xTaskCreate(identify_task, "identify_task", 2048, (void *)dev,
              5, &dev->identify_task_handle);
}