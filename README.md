Smart Zigbee Light & Shutter Controller
Overview
This project is an ESP32-based firmware designed to create a unified Zigbee End Device (ZED) that simultaneously controls multiple smart lights (or generic relays) and motorized roller shutters. Built using the ESP-Zigbee-SDK, the device acts as a bridge between physical hardware (local push-buttons and relays) and a Zigbee network (such as Home Assistant via Zigbee2MQTT or ZHA).

By combining standard Zigbee Cluster Library (ZCL) commands with local physical overrides, this project provides a seamless, state-synchronized smart home experience.

Core Features
Multi-Endpoint Zigbee Integration: The device dynamically registers multiple Zigbee endpoints based on the configured hardware. Lights and shutters appear as distinct, fully controllable entities within your smart home hub.
Local Physical Control: Every light and shutter can be controlled via physical push-buttons. State changes triggered locally are instantly reported back to the Zigbee coordinator to ensure the smart home dashboard is always in sync.
Time-Based Shutter Positioning: Shutter positions (0% to 100%) are calculated using high-resolution timers (esp_timer), eliminating the need for complex physical position sensors.
Software Interlocks for Motors: The shutter control logic includes built-in safety interlocks to ensure that the "Open" and "Close" relays can never be activated simultaneously, protecting the shutter motors from electrical damage.
Device Identification: Supports the standard Zigbee "Identify" cluster, allowing the device to visually blink a specific light to help users locate it during network setup.
Continuous Heartbeat: A dedicated status LED provides a continuous visual heartbeat to confirm the main application loop is running healthily.
System Architecture
The project is modularly divided into several key components:

1. Main Application (smart_light.c)
This is the entry point and orchestrator of the device. It defines the hardware configuration, such as which GPIO pins correspond to which LEDs, relays, and buttons.
It polls the physical buttons for both the lights and the shutters in a continuous FreeRTOS task.
It initializes the Non-Volatile Storage (NVS) required by the Zigbee stack to store network keys.
It passes the hardware arrays to the Zigbee setup module to dynamically generate endpoints.
2. Zigbee Stack & Routing (zigbee_setup.c)
This module abstracts the complexity of the ESP-Zigbee-SDK.
Endpoint Mapping: It dynamically assigns endpoints starting at 1 for LEDs, followed sequentially by the shutters.
Cluster Management: * Lights use the HA_ON_OFF_LIGHT device ID and support the ON_OFF and IDENTIFY clusters.
Shutters use the HA_WINDOW_COVERING device ID and utilize the WINDOW_COVERING cluster.
Event Handling: It intercepts incoming Zigbee commands (e.g., a "Close" command from Home Assistant) and routes them to the appropriate local C function.
Reporting: It provides helper functions to push state changes (e.g., a local button press) upstream to the Zigbee coordinator.
3. Shutter Controller (shutter_control.c)
Handles the state machine for motorized window coverings.
States: Idle, Opening, and Closing.
Timing Logic: When a target percentage is received from Zigbee, the module calculates the exact microsecond duration the motor needs to run based on a predefined maximum travel time.
Periodic Updates: While a shutter is in motion, it periodically reports its estimated current position to the Zigbee network so the user can see it moving in real-time on their dashboard.
4. LED/Relay Controller (led_control.c)
Manages simple binary outputs (Lights/Relays). It handles basic GPIO toggling and implements the asynchronous "Identify" task, which flashes the light for a requested number of seconds without blocking the main device loop.

Extensibility
The codebase is designed to be easily expandable. Adding a new light or a new shutter to the device is as simple as defining a new configuration struct in the main file and adding it to the global device arrays. The Zigbee setup will automatically detect the new hardware and expose it as a new endpoint on the Zigbee network.
