#include "status_blink.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "STATUS_BLINK";

void status_blink_init(status_blink_t *dev) {
  // Setup RGB LED
  if (dev->rgb_gpio >= 0) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = dev->rgb_gpio,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    esp_err_t err =
        led_strip_new_rmt_device(&strip_config, &rmt_config, &dev->led_strip);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to init RGB strip on GPIO %d (err %d) — skipping",
               dev->rgb_gpio, err);
      dev->led_strip = NULL;
    } else {
      led_strip_clear(dev->led_strip);
    }
  }

  // Setup external status LED
  if (dev->external_led_gpio >= 0) {
    gpio_reset_pin(dev->external_led_gpio);
    gpio_set_direction(dev->external_led_gpio, GPIO_MODE_OUTPUT);
  }
}

void status_blink_loop(status_blink_t *dev) {
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
