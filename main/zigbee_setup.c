#include "zigbee_setup.h"
#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h" // <-- NEW: Required for HA macros
#include "led_control.h"
#include "shutter_control.h"
#include <stdio.h>

#define HA_ONOFF_LIGHT_ENDPOINT 1 // Standard Zigbee Endpoint ID
#define HA_WINDOW_COVERING_ENDPOINT 2

// 1. Handle incoming commands from Home Assistant
static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id,
                                   const void *message) {
  esp_err_t ret = ESP_OK;

  // LOG EVERYTHING TO FIND THE SLIDER COMMAND
  printf("Zigbee Callback Triggered: ID 0x%02x\n", callback_id);

  switch (callback_id) {
  case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID: {
    const esp_zb_zcl_set_attr_value_message_t *msg =
        (esp_zb_zcl_set_attr_value_message_t *)message;

    printf("SET_ATTR Cluster: 0x%04x, Attr: 0x%04x\n", msg->info.cluster,
           msg->attribute.id);

    // If the hub sent a command to the On/Off Cluster...
    if (msg->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
      if (msg->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID) {
        bool light_state = *(bool *)msg->attribute.data.value;
        printf("Zigbee Light Command Received! %s\n",
               light_state ? "ON" : "OFF");
        led_control_set_main_state(light_state);
      }
    } else if (msg->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY) {
      if (msg->attribute.id == ESP_ZB_ZCL_ATTR_IDENTIFY_IDENTIFY_TIME_ID) {
        uint16_t identify_time = *(uint16_t *)msg->attribute.data.value;
        printf("Zigbee Identify Command: Blink for %d seconds\n",
               identify_time);

        if (identify_time > 0) {
          led_control_start_identify(identify_time);
        }
      }
    }

    // Handle Window Covering Position (Lift Percentage)
    else if (msg->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING) {
      // 0x0008 = CurrentPositionLiftPercentage, 0x000B =
      // TargetPositionLiftPercentage
      if (msg->attribute.id == 0x0008 || msg->attribute.id == 0x000B) {
        uint8_t percentage = *(uint8_t *)msg->attribute.data.value;
        printf("Zigbee Shutter ATTR Command: Set position to %d%% (AttrID: "
               "0x%04x)\n",
               percentage, msg->attribute.id);
        shutter_set_position(percentage);
      }
    }
    break;
  }
  case ESP_ZB_CORE_WINDOW_COVERING_MOVEMENT_CB_ID: {
    const esp_zb_zcl_window_covering_movement_message_t *msg =
        (esp_zb_zcl_window_covering_movement_message_t *)message;

    printf("MOVEMENT COMMAND: cluster 0x%04x, command 0x%02x\n",
           msg->info.cluster, msg->command);

    if (msg->command == ESP_ZB_ZCL_CMD_WINDOW_COVERING_UP_OPEN) {
      printf("Zigbee Shutter Command: UP/OPEN\n");
      shutter_open();
    } else if (msg->command == ESP_ZB_ZCL_CMD_WINDOW_COVERING_DOWN_CLOSE) {
      printf("Zigbee Shutter Command: DOWN/CLOSE\n");
      shutter_close();
    } else if (msg->command == ESP_ZB_ZCL_CMD_WINDOW_COVERING_STOP) {
      printf("Zigbee Shutter Command: STOP\n");
      shutter_stop();
    } else if (msg->command == 0x05) { // GO_TO_LIFT_PERCENTAGE
      uint8_t value = msg->payload.percentage_lift_value;
      printf("Zigbee Shutter Command: GO TO LIFT %d%%\n", value);
      shutter_set_position(value);
    }
    break;
  }
  default:
    break;
  }
  return ret;
}

// 2. Handle Network Connection (Pairing mode)
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
      // Wait a bit before retrying if needed, but here we just retry
      esp_zb_bdb_start_top_level_commissioning(
          ESP_ZB_BDB_MODE_NETWORK_STEERING);
    }
    break;
  default:
    printf("ZDO signal received: 0x%x, status: %d\n", sig_type, err_status);
    break;
  }
}

// 3. Setup the device and clusters
// 3. Setup the device and clusters
void zigbee_init_and_start(void) {

  // Configure platform (radio and host)
  esp_zb_platform_config_t config = {
      .radio_config = {.radio_mode = ZB_RADIO_MODE_NATIVE},
      .host_config = {.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE},
  };
  ESP_ERROR_CHECK(esp_zb_platform_config(&config));

  // --- YOUR MANUAL CONFIGURATION ---
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
  // ---------------------------------

  // Create Endpoint List
  esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();

  // 1. Create a standard ZHA On/Off Light Endpoint
  esp_zb_on_off_light_cfg_t light_cfg = ESP_ZB_DEFAULT_ON_OFF_LIGHT_CONFIG();
  esp_zb_cluster_list_t *light_clusters =
      esp_zb_on_off_light_clusters_create(&light_cfg);

  esp_zb_endpoint_config_t light_ep_config = {
      .endpoint = HA_ONOFF_LIGHT_ENDPOINT,
      .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
      .app_device_id = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
      .app_device_version = 0,
  };

  // --- ADD DEVICE IDENTIFICATION ---
  esp_zb_attribute_list_t *basic_attr_list = esp_zb_cluster_list_get_cluster(
      light_clusters, ESP_ZB_ZCL_CLUSTER_ID_BASIC,
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

  esp_zb_ep_list_add_ep(ep_list, light_clusters, light_ep_config);

  // 2. Create a standard ZHA Window Covering Endpoint
  esp_zb_window_covering_cfg_t shutter_cfg =
      ESP_ZB_DEFAULT_WINDOW_COVERING_CONFIG();
  esp_zb_cluster_list_t *shutter_clusters =
      esp_zb_window_covering_clusters_create(&shutter_cfg);

  // --- ADD MISSING ATTRIBUTES FOR HOMEKIT/HA ---
  esp_zb_attribute_list_t *shutter_attr_list = esp_zb_cluster_list_get_cluster(
      shutter_clusters, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

  if (shutter_attr_list) {
    // Manually add attributes that are missing from default config but required
    // by HA/HomeKit
    uint8_t oper_status = 0x00;
    esp_zb_cluster_add_attr(shutter_attr_list,
                            ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING, 0x000A, 0x18,
                            0x05, &oper_status);

    uint8_t current_pos = 0x00;
    esp_zb_cluster_add_attr(shutter_attr_list,
                            ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING, 0x0008, 0x20,
                            0x05, &current_pos);

    uint8_t target_pos = 0x00;
    esp_zb_cluster_add_attr(shutter_attr_list,
                            ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING, 0x000B, 0x20,
                            0x07, &target_pos);

    uint16_t open_limit = 0x0000;
    esp_zb_cluster_add_attr(shutter_attr_list,
                            ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING, 0x0010, 0x21,
                            0x01, &open_limit);

    uint16_t closed_limit = 0x0064; // 100%
    esp_zb_cluster_add_attr(shutter_attr_list,
                            ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING, 0x0011, 0x21,
                            0x01, &closed_limit);

    uint32_t feature_map = 0x05; // Lift (0x01) + Lift Percentage (0x04)
    esp_zb_cluster_add_attr(shutter_attr_list,
                            ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING, 0xFFFC, 0x23,
                            0x01, &feature_map);
  }

  esp_zb_endpoint_config_t shutter_ep_config = {
      .endpoint = HA_WINDOW_COVERING_ENDPOINT,
      .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
      .app_device_id = ESP_ZB_HA_WINDOW_COVERING_DEVICE_ID,
      .app_device_version = 0,
  };
  esp_zb_ep_list_add_ep(ep_list, shutter_clusters, shutter_ep_config);

  // Register the full device
  esp_zb_device_register(ep_list);

  // Set default values after registration
  uint8_t wc_type = 0x00; // Roller Shade
  esp_zb_zcl_set_attribute_val(
      HA_WINDOW_COVERING_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 0x0000, &wc_type, false);

  uint8_t config_status = 0x0B; // Operational | Online | Closed Loop
  esp_zb_zcl_set_attribute_val(
      HA_WINDOW_COVERING_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 0x0007, &config_status, false);

  uint8_t mode = 0x00;
  esp_zb_zcl_set_attribute_val(
      HA_WINDOW_COVERING_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 0x0017, &mode, false);

  // Initial state report
  uint8_t initial_pos = shutter_get_position();
  zigbee_report_shutter_position(initial_pos);
  zigbee_report_shutter_target(initial_pos);
  zigbee_report_shutter_status(0); // Ensure it's reported as stopped

  // Register our function to intercept commands
  esp_zb_core_action_handler_register(zb_action_handler);

  // Start the Zigbee Stack
  esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);
  esp_zb_start(false);
}

void zigbee_report_shutter_position(uint8_t percentage) {
  esp_err_t err;
  esp_zb_lock_acquire(portMAX_DELAY);
  err = esp_zb_zcl_set_attribute_val(
      HA_WINDOW_COVERING_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
      0x0008,              // CurrentPositionLiftPercentage
      &percentage, false); // <-- CHANGED TO FALSE
  esp_zb_lock_release();

  if (err != ESP_OK) {
    printf("ERROR: Failed to report position: %d\n", err);
  }
}

void zigbee_report_shutter_status(uint8_t status) {
  esp_err_t err;
  esp_zb_lock_acquire(portMAX_DELAY);
  err = esp_zb_zcl_set_attribute_val(HA_WINDOW_COVERING_ENDPOINT,
                                     ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
                                     ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                     0x000A,          // OperationalStatus
                                     &status, false); // <-- CHANGED TO FALSE
  esp_zb_lock_release();

  if (err != ESP_OK) {
    printf("ERROR: Failed to report status: %d\n", err);
  }
}

void zigbee_report_shutter_target(uint8_t percentage) {
  esp_err_t err;
  esp_zb_lock_acquire(portMAX_DELAY);
  err = esp_zb_zcl_set_attribute_val(
      HA_WINDOW_COVERING_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
      0x000B,              // TargetPosition
      &percentage, false); // <-- CHANGED TO FALSE
  esp_zb_lock_release();

  if (err != ESP_OK) {
    printf("ERROR: Failed to report target: %d\n", err);
  }
}

void zigbee_report_onoff_state(bool state) {
  esp_zb_lock_acquire(portMAX_DELAY);
  esp_zb_zcl_set_attribute_val(HA_ONOFF_LIGHT_ENDPOINT,
                               ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                               ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                               ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &state, false);
  esp_zb_lock_release();
}