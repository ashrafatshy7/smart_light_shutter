#include "led_control.h"

void led_control_init(led_control_t *dev) {
  gpio_reset_pin(dev->gpio);
  gpio_set_direction(dev->gpio, GPIO_MODE_OUTPUT);
}

bool led_control_toggle_main(led_control_t *dev) {
  dev->state = !dev->state;
  gpio_set_level(dev->gpio, dev->state);
  return dev->state;
}

bool led_control_get_main_state(led_control_t *dev) {
  return dev->state;
}

void led_control_set_main_state(led_control_t *dev, bool state) {
  dev->state = state;
  gpio_set_level(dev->gpio, dev->state);
}

static void identify_task(void *pvParameters) {
  led_control_t *dev = (led_control_t *)pvParameters;
  uint16_t seconds = dev->identify_seconds;

  // Save the original state to restore it after identifying
  bool original_state = dev->state;

  // Blink once per second for the requested duration
  for (int i = 0; i < seconds; i++) {
    gpio_set_level(dev->gpio, !original_state);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(dev->gpio, original_state);
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  dev->identify_task_handle = NULL;
  vTaskDelete(NULL);
}

void led_control_start_identify(led_control_t *dev, uint16_t seconds) {
  if (dev->identify_task_handle != NULL) {
    return;
  }

  dev->identify_seconds = seconds;
  xTaskCreate(identify_task, "identify_task", 2048, (void *)dev,
              5, &dev->identify_task_handle);
}