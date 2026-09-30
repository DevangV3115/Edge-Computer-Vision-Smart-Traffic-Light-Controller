"""MQTT publisher for the Raspberry Pi traffic-control node.

The Raspberry Pi is the AI/edge computer. It publishes signal commands after
YOLO + traffic logic decide which lane should receive the green light.

The code is intentionally hardware-independent: it only sends MQTT messages.
The ESP32 is responsible for reading those messages and controlling GPIOs.
"""

from __future__ import annotations

import json
import os
import socket
import time
from dataclasses import dataclass, asdict
from typing import Any

import paho.mqtt.client as mqtt

from .topics import TRAFFIC_SIGNAL_TOPIC, TRAFFIC_STATUS_TOPIC


@dataclass(frozen=True)
class SignalCommand:
    """Command sent from Raspberry Pi to ESP32."""

    lane: int
    signal: str
    duration: int

    def __post_init__(self) -> None:
        if self.lane not in (1, 2, 3, 4):
            raise ValueError("lane must be 1, 2, 3, or 4")

        signal = self.signal.upper()
        if signal not in {"RED", "YELLOW", "GREEN"}:
            raise ValueError("signal must be RED, YELLOW, or GREEN")
        object.__setattr__(self, "signal", signal)

        if not 0 <= self.duration <= 300:
            raise ValueError("duration must be between 0 and 300 seconds")


def build_signal_payload(lane: int, signal: str, duration: int) -> str:
    """Build a compact JSON payload for the ESP32."""
    command = SignalCommand(lane=lane, signal=signal, duration=duration)
    return json.dumps(asdict(command), separators=(",", ":"))


class MQTTPublisher:
    """Publishes traffic commands from the Raspberry Pi to the ESP32.

    Defaults are suitable for the final architecture where Mosquitto runs on
    the Raspberry Pi itself. The broker can be overridden with environment
    variables for development/testing:

        TRAFFIC_MQTT_BROKER=192.168.1.50
        TRAFFIC_MQTT_PORT=1883
    """

    def __init__(
        self,
        broker_host: str | None = None,
        broker_port: int | None = None,
        keepalive: int = 60,
        client_id: str | None = None,
    ) -> None:
        self.broker_host = broker_host or os.getenv("TRAFFIC_MQTT_BROKER", "127.0.0.1")
        self.broker_port = int(
            broker_port or os.getenv("TRAFFIC_MQTT_PORT", "1883")
        )
        self.keepalive = keepalive
        self.client_id = client_id or f"traffic-pi-{socket.gethostname()}"

        self.client = mqtt.Client(client_id=self.client_id, protocol=mqtt.MQTTv5)
        self.client.reconnect_delay_set(min_delay=2, max_delay=30)

    def connect(self) -> None:
        """Connect to the MQTT broker and start the network loop."""
        self.client.connect(self.broker_host, self.broker_port, self.keepalive)
        self.client.loop_start()

    def disconnect(self) -> None:
        """Stop the network loop and disconnect cleanly."""
        try:
            self.client.loop_stop()
        finally:
            try:
                self.client.disconnect()
            except Exception:
                # Already-disconnected clients can safely be ignored here.
                pass

    def publish_signal(
        self,
        lane: int,
        signal: str,
        duration: int,
        qos: int = 1,
        retain: bool = False,
    ) -> dict[str, Any]:
        """Publish a signal command and return MQTT publish metadata."""
        if not self.client.is_connected():
            raise RuntimeError("MQTT client is not connected")

        payload = build_signal_payload(lane, signal, duration)
        info = self.client.publish(
            TRAFFIC_SIGNAL_TOPIC,
            payload=payload,
            qos=qos,
            retain=retain,
        )
        info.wait_for_publish(timeout=5.0)

        if info.rc != mqtt.MQTT_ERR_SUCCESS:
            raise RuntimeError(f"MQTT publish failed with rc={info.rc}")

        return {
            "topic": TRAFFIC_SIGNAL_TOPIC,
            "payload": payload,
            "mid": info.mid,
            "timestamp": time.time(),
        }

    def publish_status(self, status: str, lane: int | None = None) -> None:
        """Optionally publish a status message for monitoring/debugging."""
        payload: dict[str, Any] = {
            "status": status,
            "timestamp": time.time(),
        }
        if lane is not None:
            payload["lane"] = lane

        info = self.client.publish(
            TRAFFIC_STATUS_TOPIC,
            payload=json.dumps(payload, separators=(",", ":")),
            qos=1,
            retain=False,
        )
        info.wait_for_publish(timeout=5.0)

        if info.rc != mqtt.MQTT_ERR_SUCCESS:
            raise RuntimeError(f"MQTT status publish failed with rc={info.rc}")


if __name__ == "__main__":
    # Simple showcase/test command. On the Raspberry Pi, start Mosquitto first.
    # Example:
    #     python -m src.mqtt.publisher
    publisher = MQTTPublisher()

    try:
        print(f"Connecting to MQTT broker {publisher.broker_host}:{publisher.broker_port} ...")
        publisher.connect()
        print("Connected.")

        result = publisher.publish_signal(
            lane=1,
            signal="GREEN",
            duration=20,
        )
        print("Published:", result["payload"])

    except (OSError, RuntimeError) as exc:
        print(f"MQTT test failed: {exc}")
        raise SystemExit(1)
    finally:
        publisher.disconnect()
