# MQTT Communication Layer

The MQTT layer is the communication bridge between the Raspberry Pi and ESP32.

## Final architecture

```text
Raspberry Pi
YOLO + traffic logic
      |
      | MQTT publish
      v
Mosquitto broker (Raspberry Pi)
      |
      | Wi-Fi
      v
ESP32 subscriber
      |
      v
Relay / traffic LEDs
```

## Signal message

Topic:

```text
traffic/signals
```

Payload:

```json
{"lane":1,"signal":"GREEN","duration":20}
```

## Raspberry Pi setup

Install the Python dependency:

```bash
pip install paho-mqtt
```

Install/run Mosquitto on the Raspberry Pi and keep the broker on port `1883`.
The Python publisher defaults to `127.0.0.1`, which matches a broker running on the same Pi.

To use another broker during testing:

```bash
export TRAFFIC_MQTT_BROKER=192.168.1.50
export TRAFFIC_MQTT_PORT=1883
```

Then test the publisher with:

```bash
python -m src.mqtt.publisher
```

The ESP32 should point `MQTT_BROKER` to the Raspberry Pi's LAN IP address.
