/*
 * Edge Computer Vision Smart Traffic Light Controller
 * ESP32 Traffic Signal Controller
 *
 * Architecture:
 *   Raspberry Pi (YOLO + traffic logic + MQTT publisher)
 *                         |
 *                       Wi-Fi
 *                         |
 *                       MQTT
 *                         v
 *                     ESP32 node
 *                         |
 *                       GPIO
 *                         v
 *                 Relay / Traffic LEDs
 *
 * The ESP32 receives commands on:
 *     traffic/signals
 *
 * Expected JSON payload:
 *     {"lane":1,"signal":"GREEN","duration":20}
 *
 * Required Arduino libraries:
 *   - PubSubClient
 *   - ArduinoJson
 *
 * IMPORTANT:
 *   Replace the Wi-Fi credentials and Raspberry Pi MQTT broker IP before
 *   using the hardware. GPIO numbers are also configurable below.
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// =========================
// 1. NETWORK CONFIGURATION
// =========================

const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Mosquitto broker running on the Raspberry Pi.
// Example: "192.168.1.50"
const char* MQTT_BROKER = "192.168.1.50";
const uint16_t MQTT_PORT = 1883;

const char* MQTT_SIGNAL_TOPIC = "traffic/signals";
const char* MQTT_STATUS_TOPIC = "traffic/status";

// =========================
// 2. TRAFFIC SIGNAL GPIO MAP
// =========================
// These are example GPIOs for a typical ESP32 DevKit board.
// Confirm the exact board and wiring before connecting the hardware.

struct LanePins {
  uint8_t red;
  uint8_t yellow;
  uint8_t green;
};

LanePins lanePins[4] = {
  {13, 14, 16},  // Lane 1
  {17, 18, 19},  // Lane 2
  {21, 22, 23},  // Lane 3
  {25, 26, 27}   // Lane 4
};

// Use HIGH/LOW according to how your LEDs/relay module are wired.
// For many relay boards the inputs are active-LOW, so change these values
// if your relay behaves inversely.
const uint8_t OUTPUT_ON = HIGH;
const uint8_t OUTPUT_OFF = LOW;

// =========================
// 3. FAIL-SAFE CONFIGURATION
// =========================
// If MQTT is unavailable, ESP32 falls back to a fixed signal cycle instead
// of waiting indefinitely for a command.

const unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000;
const unsigned long FALLBACK_GREEN_MS = 20000;
const unsigned long FALLBACK_YELLOW_MS = 3000;
const unsigned long FALLBACK_ALL_RED_MS = 1000;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

unsigned long lastMqttReconnectAttempt = 0;
bool wasMqttConnected = false;
unsigned long phaseStartedAt = 0;

int fallbackLane = 0;
enum FallbackPhase {
  FALLBACK_GREEN,
  FALLBACK_YELLOW,
  FALLBACK_ALL_RED
};
FallbackPhase fallbackPhase = FALLBACK_GREEN;

// Track the externally commanded state.
int activeLane = -1;
String activeSignal = "RED";
unsigned long activeCommandStartedAt = 0;
unsigned long activeCommandDurationMs = 0;

// =========================
// 4. BASIC SIGNAL HELPERS
// =========================

void setLaneOutputs(int laneIndex, bool red, bool yellow, bool green) {
  digitalWrite(lanePins[laneIndex].red, red ? OUTPUT_ON : OUTPUT_OFF);
  digitalWrite(lanePins[laneIndex].yellow, yellow ? OUTPUT_ON : OUTPUT_OFF);
  digitalWrite(lanePins[laneIndex].green, green ? OUTPUT_ON : OUTPUT_OFF);
}

void setAllRed() {
  for (int i = 0; i < 4; i++) {
    setLaneOutputs(i, true, false, false);
  }
}

void setLaneSignal(int laneIndex, const String& signal) {
  if (laneIndex < 0 || laneIndex >= 4) {
    setAllRed();
    return;
  }

  // Always force every lane to RED first. This prevents two conflicting
  // green states from being active due to a new command.
  setAllRed();

  if (signal == "GREEN") {
    setLaneOutputs(laneIndex, false, false, true);
  } else if (signal == "YELLOW") {
    setLaneOutputs(laneIndex, false, true, false);
  } else {
    // Unknown/RED -> keep every lane red.
    setAllRed();
  }
}

void publishStatus(const char* status) {
  if (!mqttClient.connected()) {
    return;
  }

  JsonDocument doc;
  doc["status"] = status;
  if (activeLane >= 0) {
    doc["lane"] = activeLane + 1;
  }
  doc["signal"] = activeSignal;

  String payload;
  serializeJson(doc, payload);
  mqttClient.publish(MQTT_STATUS_TOPIC, payload.c_str());
}

// =========================
// 5. WIFI
// =========================

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.print("Connecting to Wi-Fi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Wi-Fi connected. ESP32 IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("Wi-Fi connection failed; continuing in fail-safe mode.");
  }
}

// =========================
// 6. MQTT CALLBACK
// =========================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (String(topic) != MQTT_SIGNAL_TOPIC) {
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    Serial.print("Invalid JSON command: ");
    Serial.println(error.c_str());
    setAllRed();
    return;
  }

  int lane = doc["lane"] | 0;
  const char* signalRaw = doc["signal"] | "RED";
  int duration = doc["duration"] | 0;

  String signal = String(signalRaw);
  signal.toUpperCase();

  if (lane < 1 || lane > 4) {
    Serial.println("Invalid lane. Valid lanes are 1-4.");
    setAllRed();
    return;
  }

  if (signal != "RED" && signal != "YELLOW" && signal != "GREEN") {
    Serial.println("Invalid signal. Valid values: RED, YELLOW, GREEN.");
    setAllRed();
    return;
  }

  if (duration < 0 || duration > 300) {
    Serial.println("Invalid duration. Allowed range: 0-300 seconds.");
    setAllRed();
    return;
  }

  activeLane = lane - 1;
  activeSignal = signal;
  activeCommandStartedAt = millis();
  activeCommandDurationMs = (unsigned long)duration * 1000UL;

  setLaneSignal(activeLane, activeSignal);

  Serial.print("MQTT command -> Lane ");
  Serial.print(lane);
  Serial.print(" | ");
  Serial.print(activeSignal);
  Serial.print(" | ");
  Serial.print(duration);
  Serial.println(" sec");

  publishStatus("command_received");
}

// =========================
// 7. MQTT CONNECTION
// =========================

bool connectMQTT() {
  if (mqttClient.connected()) {
    return true;
  }

  Serial.print("Connecting to MQTT broker at ");
  Serial.print(MQTT_BROKER);
  Serial.print(":");
  Serial.println(MQTT_PORT);

  String clientId = "esp32-traffic-" + String((uint32_t)ESP.getEfuseMac(), HEX);

  if (mqttClient.connect(clientId.c_str())) {
    Serial.println("MQTT connected.");
    mqttClient.subscribe(MQTT_SIGNAL_TOPIC, 1);
    publishStatus("mqtt_connected");
    return true;
  }

  Serial.print("MQTT connection failed, rc=");
  Serial.println(mqttClient.state());
  return false;
}

// =========================
// 8. FAIL-SAFE FIXED TIMER
// =========================

void startFallbackCycle() {
  fallbackLane = 0;
  fallbackPhase = FALLBACK_GREEN;
  phaseStartedAt = millis();
  setAllRed();
  setLaneSignal(fallbackLane, "GREEN");
  Serial.println("FAIL-SAFE: fixed timer started.");
}

void runFallbackCycle() {
  unsigned long elapsed = millis() - phaseStartedAt;

  if (fallbackPhase == FALLBACK_GREEN && elapsed >= FALLBACK_GREEN_MS) {
    fallbackPhase = FALLBACK_YELLOW;
    phaseStartedAt = millis();
    setLaneSignal(fallbackLane, "YELLOW");
    return;
  }

  if (fallbackPhase == FALLBACK_YELLOW && elapsed >= FALLBACK_YELLOW_MS) {
    fallbackPhase = FALLBACK_ALL_RED;
    phaseStartedAt = millis();
    setAllRed();
    return;
  }

  if (fallbackPhase == FALLBACK_ALL_RED && elapsed >= FALLBACK_ALL_RED_MS) {
    fallbackLane = (fallbackLane + 1) % 4;
    fallbackPhase = FALLBACK_GREEN;
    phaseStartedAt = millis();
    setLaneSignal(fallbackLane, "GREEN");
  }
}

void expireExternalCommandIfNeeded() {
  if (activeLane < 0 || activeSignal == "RED") {
    return;
  }

  if (activeCommandDurationMs == 0) {
    return;
  }

  if (millis() - activeCommandStartedAt >= activeCommandDurationMs) {
    // End the externally commanded phase safely.
    setAllRed();
    activeLane = -1;
    activeSignal = "RED";
    activeCommandDurationMs = 0;
    publishStatus("command_expired");
  }
}

// =========================
// 9. SETUP
// =========================

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    pinMode(lanePins[i].red, OUTPUT);
    pinMode(lanePins[i].yellow, OUTPUT);
    pinMode(lanePins[i].green, OUTPUT);
  }

  // Critical safe startup state.
  setAllRed();

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);

  connectWiFi();
  connectMQTT();

  // Start the fallback state if MQTT is not immediately available.
  if (!mqttClient.connected()) {
    startFallbackCycle();
  } else {
    wasMqttConnected = true;
  }
}

// =========================
// 10. LOOP
// =========================

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (!mqttClient.connected()) {
    // Enter the fixed-timer fallback exactly when the MQTT connection drops.
    if (wasMqttConnected) {
      wasMqttConnected = false;
      startFallbackCycle();
    }

    unsigned long now = millis();

    if (now - lastMqttReconnectAttempt >= MQTT_RECONNECT_INTERVAL_MS) {
      lastMqttReconnectAttempt = now;
      connectMQTT();
    }

    // Keep the prototype safe and functional without MQTT.
    runFallbackCycle();
  } else {
    if (!wasMqttConnected) {
      wasMqttConnected = true;
    }

    mqttClient.loop();
    expireExternalCommandIfNeeded();
  }

  delay(10);
}
