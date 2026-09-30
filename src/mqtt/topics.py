"""MQTT topics used by the Raspberry Pi and ESP32 nodes."""

TRAFFIC_SIGNAL_TOPIC = "traffic/signals"
TRAFFIC_STATUS_TOPIC = "traffic/status"

# Keep the topic names in one place so that the Python side and the ESP32
# side always use the same strings.
