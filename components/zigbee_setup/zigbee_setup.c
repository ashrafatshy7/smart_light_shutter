#include "zigbee_setup.h"
#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"
#include <stdio.h>

// ============================================================
// Stored device arrays (set during zigbee_init_and_start)
// ============================================================

static led_control_t *s_leds[ZIGBEE_MAX_LEDS];
static int s_num_leds = 0;

static shutter_control_t *s_shutters[ZIGBEE_MAX_SHUTTERS];
static int s_num_shutters = 0;

// Endpoint layout:
//   LEDs:     endpoints 1 .. num_leds
//   Shutters: endpoints (num_leds + 1) .. (num_leds + num_shutters)

static int led_endpoint(int index) { return index + 1; }
static int shutter_endpoint(int index) { return s_num_leds + index + 1; }

// Look up a LED by endpoint number, returns NULL if not a LED endpoint
static led_control_t *find_led_by_endpoint(uint8_t ep) {
  int index = ep - 1;
  if (index >= 0 && index < s_num_leds) {
    return s_leds[index];
  }
  return NULL;
}

// Look up a shutter by endpoint number, returns NULL if not a shutter endpoint
static shutter_control_t *find_shutter_by_endpoint(uint8_t ep) {
  int index = ep - s_num_leds - 1;
  if (index >= 0 && index < s_num_shutters) {
    return s_shutters[index];
  }
  return NULL;
}

// ============================================================
// 1. Handle incoming commands from Home Assistant
// ============================================================

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id,
                                   const void *message) {
  esp_err_t ret = ESP_OK;

  printf("Zigbee Callback Triggered: ID 0x%02x\n", callback_id);

  switch (callback_id) {
  case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID: {
    const esp_zb_zcl_set_attr_value_message_t *msg =
        (esp_zb_zcl_set_attr_value_message_t *)message;

    uint8_t ep = msg->info.dst_endpoint;
    printf("SET_ATTR Endpoint: %d, Cluster: 0x%04x, Attr: 0x%04x\n",
           ep, msg->info.cluster, msg->attribute.id);

    // --- LED commands (On/Off + Identify) ---
    led_control_t *led = find_led_by_endpoint(ep);
    if (led) {
      if (msg->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
        if (msg->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID) {
          bool light_state = *(bool *)msg->attribute.data.value;
          printf("Zigbee Light Command (EP %d): %s\n", ep,
                 light_state ? "ON" : "OFF");
          led_control_set_main_state(led, light_state);
        }
      } else if (msg->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY) {
        if (msg->attribute.id == ESP_ZB_ZCL_ATTR_IDENTIFY_IDENTIFY_TIME_ID) {
          uint16_t identify_time = *(uint16_t *)msg->attribute.data.value;
          printf("Zigbee Identify Command (EP %d): Blink for %d seconds\n",
                 ep, identify_time);
          if (identify_time > 0) {
            led_control_start_identify(led, identify_time);
          }
        }
      }
    }

    // --- Shutter commands (Window Covering attribute set) ---
    shutter_control_t *shutter = find_shutter_by_endpoint(ep);
    if (shutter) {
      if (msg->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING) {
        if (msg->attribute.id == 0x0008 || msg->attribute.id == 0x000B) {
          uint8_t percentage = *(uint8_t *)msg->attribute.data.value;
          printf("Zigbee Shutter ATTR (EP %d): Set position to %d%%\n",
                 ep, percentage);
          shutter_set_position(shutter, percentage);
        }
      }
    }
    break;
  }
  case ESP_ZB_CORE_WINDOW_COVERING_MOVEMENT_CB_ID: {
    const esp_zb_zcl_window_covering_movement_message_t *msg =
        (esp_zb_zcl_window_covering_movement_message_t *)message;

    uint8_t ep = msg->info.dst_endpoint;
    printf("MOVEMENT COMMAND (EP %d): cluster 0x%04x, command 0x%02x\n",
           ep, msg->info.cluster, msg->command);

    shutter_control_t *shutter = find_shutter_by_endpoint(ep);
    if (shutter) {
      if (msg->command == ESP_ZB_ZCL_CMD_WINDOW_COVERING_UP_OPEN) {
        printf("Zigbee Shutter Command (EP %d): UP/OPEN\n", ep);
        shutter_open(shutter);
      } else if (msg->command == ESP_ZB_ZCL_CMD_WINDOW_COVERING_DOWN_CLOSE) {
        printf("Zigbee Shutter Command (EP %d): DOWN/CLOSE\n", ep);
        shutter_close(shutter);
      } else if (msg->command == ESP_ZB_ZCL_CMD_WINDOW_COVERING_STOP) {
        printf("Zigbee Shutter Command (EP %d): STOP\n", ep);
        shutter_stop(shutter);
      } else if (msg->command == 0x05) { // GO_TO_LIFT_PERCENTAGE
        uint8_t value = msg->payload.percentage_lift_value;
        printf("Zigbee Shutter Command (EP %d): GO TO LIFT %d%%\n", ep, value);
        shutter_set_position(shutter, value);
      }
    }
    break;
  }
  default:
    break;
  }
  return ret;
}

// ============================================================
// 2. Handle Network Connection (Pairing mode)
// ============================================================

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct) {
  uint32_t *p_sg_p = signal_struct->p_app_signal;
  esp_err_t err_status = signal_struct->esp_err_status;
  esp_zb_app_signal_type_t sig_type = *p_sg_p;

  switch (sig_type) {
  case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
    printf("Zigbee stack initialized. Starting BDB initialization...\n");
    esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
    break;
  case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
  case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
    if (err_status == ESP_OK) {
      if (esp_zb_bdb_is_factory_new()) {
        printf("Device started (factory new). Starting network steering "
               "(pairing mode)...\n");
        esp_zb_bdb_start_top_level_commissioning(
            ESP_ZB_BDB_MODE_NETWORK_STEERING);
      } else {
        printf("Device rebooted. Already on network.\n");
      }
    } else {
      printf("BDB initialization failed with status: %d. Retrying...\n",
             err_status);
      esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
    }
    break;
  case ESP_ZB_BDB_SIGNAL_STEERING:
    if (err_status == ESP_OK) {
      printf("SUCCESS! Connected to Zigbee Network!\n");
    } else {
      printf("Network steering (pairing) failed. Status: %d. Retrying...\n",
             err_status);
      esp_zb_bdb_start_top_level_commissioning(
          ESP_ZB_BDB_MODE_NETWORK_STEERING);
    }
    break;
  default:
    printf("ZDO signal received: 0x%x, status: %d\n", sig_type, err_status);
    break;
  }
}

// ============================================================
// 3. Setup devices and clusters dynamically
// ============================================================

// Helper: create and add one On/Off Light endpoint
static void register_led_endpoint(esp_zb_ep_list_t *ep_list, int ep_num) {
  esp_zb_on_off_light_cfg_t light_cfg = ESP_ZB_DEFAULT_ON_OFF_LIGHT_CONFIG();
  esp_zb_cluster_list_t *clusters =
      esp_zb_on_off_light_clusters_create(&light_cfg);

  // Add device identification
  esp_zb_attribute_list_t *basic_attr_list = esp_zb_cluster_list_get_cluster(
      clusters, ESP_ZB_ZCL_CLUSTER_ID_BASIC,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  if (basic_attr_list) {
    esp_zb_basic_cluster_add_attr(basic_attr_list,
                                  ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                  "\x0c"
                                  "Ashraf Atshy");
    esp_zb_basic_cluster_add_attr(basic_attr_list,
                                  ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                  "\x11"
                                  "Light and shutter");
  }

  esp_zb_endpoint_config_t ep_config = {
      .endpoint = ep_num,
      .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
      .app_device_id = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
      .app_device_version = 0,
  };

  esp_zb_ep_list_add_ep(ep_list, clusters, ep_config);
  printf("Registered LED on endpoint %d\n", ep_num);
}

// Helper: create and add one Window Covering endpoint
static void register_shutter_endpoint(esp_zb_ep_list_t *ep_list, int ep_num) {
  esp_zb_window_covering_cfg_t shutter_cfg =
      ESP_ZB_DEFAULT_WINDOW_COVERING_CONFIG();
  esp_zb_cluster_list_t *clusters =
      esp_zb_window_covering_clusters_create(&shutter_cfg);

  esp_zb_attribute_list_t *attr_list = esp_zb_cluster_list_get_cluster(
      clusters, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

  if (attr_list) {
    uint8_t oper_status = 0x00;
    esp_zb_cluster_add_attr(attr_list,
                            ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING, 0x000A, 0x18,
                            0x05, &oper_status);

    uint8_t current_pos = 0x00;
    esp_zb_cluster_add_attr(attr_list,
                            ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING, 0x0008, 0x20,
                            0x05, &current_pos);

    uint8_t target_pos = 0x00;
    esp_zb_cluster_add_attr(attr_list,
                            ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING, 0x000B, 0x20,
                            0x07, &target_pos);

    uint16_t open_limit = 0x0000;
    esp_zb_cluster_add_attr(attr_list,
                            ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING, 0x0010, 0x21,
                            0x01, &open_limit);

    uint16_t closed_limit = 0x0064; // 100%
    esp_zb_cluster_add_attr(attr_list,
                            ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING, 0x0011, 0x21,
                            0x01, &closed_limit);

    uint32_t feature_map = 0x05; // Lift (0x01) + Lift Percentage (0x04)
    esp_zb_cluster_add_attr(attr_list,
                            ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING, 0xFFFC, 0x23,
                            0x01, &feature_map);
  }

  esp_zb_endpoint_config_t ep_config = {
      .endpoint = ep_num,
      .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
      .app_device_id = ESP_ZB_HA_WINDOW_COVERING_DEVICE_ID,
      .app_device_version = 0,
  };

  esp_zb_ep_list_add_ep(ep_list, clusters, ep_config);
  printf("Registered Shutter on endpoint %d\n", ep_num);
}

// Helper: set default attribute values for a shutter after registration
static void set_shutter_defaults(int ep_num, shutter_control_t *shutter) {
  uint8_t wc_type = 0x00; // Roller Shade
  esp_zb_zcl_set_attribute_val(
      ep_num, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 0x0000, &wc_type, false);

  uint8_t config_status = 0x0B; // Operational | Online | Closed Loop
  esp_zb_zcl_set_attribute_val(
      ep_num, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 0x0007, &config_status, false);

  uint8_t mode = 0x00;
  esp_zb_zcl_set_attribute_val(
      ep_num, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 0x0017, &mode, false);

  // Initial state report
  uint8_t initial_pos = shutter_get_position(shutter);
  zigbee_report_shutter_position(ep_num, initial_pos);
  zigbee_report_shutter_target(ep_num, initial_pos);
  zigbee_report_shutter_status(ep_num, 0);
}

void zigbee_init_and_start(led_control_t *leds[], int num_leds,
                           shutter_control_t *shutters[], int num_shutters) {
  // Store device arrays for use in callbacks
  s_num_leds = num_leds;
  for (int i = 0; i < num_leds && i < ZIGBEE_MAX_LEDS; i++) {
    s_leds[i] = leds[i];
  }

  s_num_shutters = num_shutters;
  for (int i = 0; i < num_shutters && i < ZIGBEE_MAX_SHUTTERS; i++) {
    s_shutters[i] = shutters[i];
    // Assign endpoint number to each shutter so the report callback knows it
    s_shutters[i]->zigbee_endpoint = shutter_endpoint(i);
  }

  // Configure platform (radio and host)
  esp_zb_platform_config_t config = {
      .radio_config = {.radio_mode = ZB_RADIO_MODE_NATIVE},
      .host_config = {.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE},
  };
  ESP_ERROR_CHECK(esp_zb_platform_config(&config));

  esp_zb_cfg_t zb_nwk_cfg = {
      .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,
      .install_code_policy = false,
      .nwk_cfg =
          {
              .zed_cfg =
                  {
                      .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,
                      .keep_alive = 3000,
                  },
          },
  };
  esp_zb_init(&zb_nwk_cfg);

  // Create Endpoint List
  esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();

  // Register all LEDs (endpoints 1 .. num_leds)
  for (int i = 0; i < num_leds; i++) {
    register_led_endpoint(ep_list, led_endpoint(i));
  }

  // Register all Shutters (endpoints num_leds+1 .. num_leds+num_shutters)
  for (int i = 0; i < num_shutters; i++) {
    register_shutter_endpoint(ep_list, shutter_endpoint(i));
  }

  // Register the full device
  esp_zb_device_register(ep_list);

  // Set default values for each shutter after registration
  for (int i = 0; i < num_shutters; i++) {
    set_shutter_defaults(shutter_endpoint(i), s_shutters[i]);
  }

  // Register our function to intercept commands
  esp_zb_core_action_handler_register(zb_action_handler);

  // Start the Zigbee Stack
  esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);
  esp_zb_start(false);
}

// ============================================================
// 4. Reporting functions (now endpoint-aware)
// ============================================================

void zigbee_report_shutter_stopped(uint8_t endpoint, uint8_t pos) {
  // One lock, need_report=TRUE: forces immediate OTA attribute report frames.
  // With need_report=false, the ZCL scheduler defers the report to the next
  // reporting interval (coordinator-configured, typically 1 second) — that was
  // the root cause of the 1-2 second UI latency after a STOP command.
  esp_zb_lock_acquire(portMAX_DELAY);
  uint8_t status = 0;
  esp_zb_zcl_set_attribute_val(endpoint, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 0x000A, &status, true);  // status=idle (immediate OTA)
  esp_zb_zcl_set_attribute_val(endpoint, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 0x0008, &pos,    true);  // position    (immediate OTA)
  esp_zb_zcl_set_attribute_val(endpoint, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 0x000B, &pos,    true);  // target      (immediate OTA)
  esp_zb_lock_release();
}


void zigbee_report_shutter_position(uint8_t endpoint, uint8_t percentage) {
  esp_err_t err;
  esp_zb_lock_acquire(portMAX_DELAY);
  err = esp_zb_zcl_set_attribute_val(
      endpoint, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
      0x0008,
      &percentage, false);
  esp_zb_lock_release();

  if (err != ESP_OK) {
    printf("ERROR: Failed to report position (EP %d): %d\n", endpoint, err);
  }
}

void zigbee_report_shutter_status(uint8_t endpoint, uint8_t status) {
  esp_err_t err;
  esp_zb_lock_acquire(portMAX_DELAY);
  err = esp_zb_zcl_set_attribute_val(endpoint,
                                     ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
                                     ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                     0x000A,
                                     &status, false);
  esp_zb_lock_release();

  if (err != ESP_OK) {
    printf("ERROR: Failed to report status (EP %d): %d\n", endpoint, err);
  }
}

void zigbee_report_shutter_target(uint8_t endpoint, uint8_t percentage) {
  esp_err_t err;
  esp_zb_lock_acquire(portMAX_DELAY);
  err = esp_zb_zcl_set_attribute_val(
      endpoint, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
      0x000B,
      &percentage, false);
  esp_zb_lock_release();

  if (err != ESP_OK) {
    printf("ERROR: Failed to report target (EP %d): %d\n", endpoint, err);
  }
}

void zigbee_report_onoff_state(uint8_t endpoint, bool state) {
  esp_zb_lock_acquire(portMAX_DELAY);
  esp_zb_zcl_set_attribute_val(endpoint,
                               ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                               ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                               ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &state, false);
  esp_zb_lock_release();
}