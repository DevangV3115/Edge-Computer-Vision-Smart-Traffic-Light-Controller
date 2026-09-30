# ESP32 Traffic Controller

This firmware is the hardware-side MQTT subscriber for the project.

## Required Arduino libraries

Install these libraries using the Arduino IDE Library Manager:

- PubSubClient
- ArduinoJson

## Before flashing

Edit these values in `traffic_controller.ino`:

```cpp
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* MQTT_BROKER = "192.168.1.50";
```

`MQTT_BROKER` must be the Raspberry Pi's local Wi-Fi/LAN IP address when Mosquitto is running on the Pi.

Also confirm the GPIO mapping against the exact ESP32 board and the relay/LED wiring.

## MQTT command

The ESP32 subscribes to:

```text
traffic/signals
```

Example:

```json
{"lane":1,"signal":"GREEN","duration":20}
```

The firmware forces all lanes to red before applying a new lane signal.
If MQTT is unavailable, it enters a fixed-time fail-safe cycle.
