#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Max72xxPanel.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <math.h>
#include <time.h>
#include "ThermostatLogic.h"
#include "SetpointStorage.h"

// Vor dem Flashen pro Geraet aendern: 1 = Holzwerkstatt, 2 = Metallwerkstatt.
constexpr uint8_t DISPLAY_NUMBER = 1;
static_assert(DISPLAY_NUMBER == 1 || DISPLAY_NUMBER == 2, "DISPLAY_NUMBER muss 1 oder 2 sein");

// Diese zwei Werte muessen zur tatsaechlichen Relais-/Luefterverdrahtung passen.
constexpr bool COOLING_MODE = false; // Heizluefter: Ein unter, Aus ueber dem Sollwert
constexpr uint8_t RELAY_ON_LEVEL = HIGH;

constexpr float DEFAULT_SETPOINT = 8.0f;   // Nur beim ersten Start ohne gespeicherten Wert
constexpr float DEFAULT_HYSTERESIS = 2.0f; // Nach jedem Neustart wieder 2 C
constexpr float MIN_SETPOINT = 0.0f;
constexpr float MAX_SETPOINT = 40.0f;
constexpr float SETPOINT_STEP = 1.0f;
constexpr float MIN_HYSTERESIS = 0.1f;
constexpr float MAX_HYSTERESIS = 10.0f;

constexpr uint8_t DHT_PIN = 0;
constexpr uint8_t RELAY_PIN = 5;
constexpr uint8_t MOTION_PIN = 4;
constexpr uint8_t UP_PIN = 3;
constexpr uint8_t DOWN_PIN = 2;
constexpr uint8_t MATRIX_CS_PIN = D8;
constexpr uint8_t HORIZONTAL_DISPLAYS = 12;
constexpr unsigned long SENSOR_INTERVAL_MS = 2500;
constexpr unsigned long SENSOR_TIMEOUT_MS = 15000;
constexpr unsigned long MQTT_INTERVAL_MS = 60000;
constexpr unsigned long MQTT_RETRY_MS = 5000;
constexpr unsigned long WIFI_RETRY_MS = 15000;
constexpr unsigned long MOTION_OFF_DELAY_MS = 20000;
constexpr unsigned long BUTTON_DEBOUNCE_MS = 40;
constexpr unsigned long BUTTON_REPEAT_DELAY_MS = 550;
constexpr unsigned long BUTTON_REPEAT_INTERVAL_MS = 400;
constexpr unsigned long MATRIX_MUTE_MS = 3000;
constexpr size_t MQTT_BUFFER_BYTES = 512; // Tasmota-BME280-JSON ist bereits >160 Byte.

// Bisherige Netzwerkeinstellungen beibehalten.
const char WIFI_SSID[] = "Wilsche";
const char WIFI_PASSWORD[] = "lsvgifhorn16";
const char MQTT_SERVER[] = "192.168.1.200";

struct Button {
  uint8_t pin;
  bool rawPressed;
  bool stablePressed;
  unsigned long changedAt;
  unsigned long pressedAt;
  unsigned long lastRepeatAt;
};

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
DHT dht(DHT_PIN, DHT22);
Max72xxPanel matrix(MATRIX_CS_PIN, HORIZONTAL_DISPLAYS, 1);
Button upButton = {UP_PIN, false, false, 0, 0, 0};
Button downButton = {DOWN_PIN, false, false, 0, 0, 0};

char ownRoot[12];
char otherRoot[12];
char smartHomeSetpointTopic[32];
char hostname[32];
char willTopic[24];
float setpoint = DEFAULT_SETPOINT;
bool setpointPendingSmartHomeSync = false;
float hysteresis = DEFAULT_HYSTERESIS;
float insideTemperature = NAN;
float outsideTemperature = NAN;
float humidity = NAN;
bool relayOn = false;
bool sensorHasValue = false;
bool motionActive = false;
bool matrixMuted = false;
bool forceScroll = false;
int8_t otherRelayState = -1;
uint8_t intensity = 5;
uint8_t charSpacing = 6;
char notice[100] = "";
bool noticeScrolling = false;
int noticeOffset = 0;
unsigned long noticeUntil = 0;
unsigned long lastSensorRead = 0;
unsigned long lastValidSensor = 0;
unsigned long lastMqttPublish = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastMotionHigh = 0;
unsigned long matrixMuteUntil = 0;
unsigned long lastFrame = 0;
unsigned int scrollWaitMs = 30;

bool elapsed(unsigned long now, unsigned long then, unsigned long interval) {
  return (unsigned long)(now - then) >= interval;
}

bool topicIs(const char *topic, const char *suffix) {
  char expected[48];
  snprintf(expected, sizeof(expected), "%s/%s", ownRoot, suffix);
  return strcmp(topic, expected) == 0;
}

bool publish(const char *suffix, const char *value, bool retained = false) {
  if (!mqtt.connected()) return false;
  char topic[48];
  snprintf(topic, sizeof(topic), "%s/%s", ownRoot, suffix);
  return mqtt.publish(topic, value, retained);
}

void publishFloat(const char *suffix, float value, uint8_t decimals, bool retained = false) {
  if (!isfinite(value)) return;
  char text[16];
  dtostrf(value, 1, decimals, text);
  publish(suffix, text, retained);
}

void publishRelay() {
  publish("relais", relayOn ? "1" : "0");
}

void publishTelemetry() {
  if (sensorHasValue && !elapsed(millis(), lastValidSensor, SENSOR_TIMEOUT_MS)) {
    publishFloat("temperature", insideTemperature, 1);
    publishFloat("humidity", humidity, 0);
  }
  publish("LWT", "ONLINE", true);
}

bool parseNumber(const char *message, float &value, float minimum, float maximum) {
  char *end = nullptr;
  value = strtof(message, &end);
  if (end == message || !isfinite(value)) return false;
  while (*end == ' ' || *end == '\t') end++;
  return *end == '\0' && value >= minimum && value <= maximum;
}

void saveSetpoint() {
  StoredSetpoint stored = makeStoredSetpoint(static_cast<uint8_t>(setpoint), setpointPendingSmartHomeSync);
  EEPROM.put(0, stored);
  if (!EEPROM.commit()) Serial.println("Solltemperatur konnte nicht gespeichert werden");
}

void loadSetpoint() {
  EEPROM.begin(sizeof(StoredSetpoint));
  StoredSetpoint stored;
  EEPROM.get(0, stored);
  if (!validStoredSetpoint(stored)) return;
  setpoint = stored.degrees;
  setpointPendingSmartHomeSync = stored.pendingSmartHomeSync != 0;
}

bool publishSetpointToSmartHome() {
  if (!mqtt.connected()) return false;
  char value[8];
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(setpoint));
  return mqtt.publish(smartHomeSetpointTopic, value, true);
}

bool syncSetpointToSmartHome() {
  if (!setpointPendingSmartHomeSync || !publishSetpointToSmartHome()) return false;
  setpointPendingSmartHomeSync = false;
  saveSetpoint();
  return true;
}

void setSetpoint(float value, bool fromSmartHomeTopic = false) {
  if (!isfinite(value) || value < MIN_SETPOINT || value > MAX_SETPOINT) return;
  value = roundf(value / SETPOINT_STEP) * SETPOINT_STEP;
  if (fabsf(value - setpoint) < 0.01f) {
    if (fromSmartHomeTopic && setpointPendingSmartHomeSync) {
      setpointPendingSmartHomeSync = false;
      saveSetpoint();
    }
    return;
  }
  setpoint = value;
  setpointPendingSmartHomeSync = !fromSmartHomeTopic;
  if (setpointPendingSmartHomeSync && publishSetpointToSmartHome()) {
    setpointPendingSmartHomeSync = false;
  }
  saveSetpoint();
  publishFloat("solltemperatur", setpoint, 0, true);
  Serial.printf("Solltemperatur: %.1f C\n", setpoint);
}

void setHysteresis(float value) {
  if (!isfinite(value) || value < MIN_HYSTERESIS || value > MAX_HYSTERESIS) return;
  value = roundf(value * 10.0f) / 10.0f;
  if (fabsf(value - hysteresis) < 0.01f) return;
  hysteresis = value;
  publishFloat("hysterese", hysteresis, 1, true);
  Serial.printf("Hysterese: %.1f C\n", hysteresis);
}

void muteMatrix(unsigned long now) {
  if (!matrixMuted) {
    matrix.shutdown(true);
    matrixMuted = true;
  }
  matrixMuteUntil = now + MATRIX_MUTE_MS;
}

void updateRelay(unsigned long now) {
  const bool sensorValid = sensorHasValue && !elapsed(now, lastValidSensor, SENSOR_TIMEOUT_MS);
  bool wanted = nextRelayState(relayOn, insideTemperature, setpoint,
                               hysteresis, sensorValid, COOLING_MODE);
  if (wanted == relayOn) return;
  muteMatrix(now);
  relayOn = wanted;
  digitalWrite(RELAY_PIN, relayOn ? RELAY_ON_LEVEL : (RELAY_ON_LEVEL == HIGH ? LOW : HIGH));
  publishRelay();
  Serial.printf("Relais: %s\n", relayOn ? "ein" : "aus");
}

void readSensor(unsigned long now) {
  if (lastSensorRead != 0 && !elapsed(now, lastSensorRead, SENSOR_INTERVAL_MS)) return;
  lastSensorRead = now;
  float measuredTemperature = dht.readTemperature();
  float measuredHumidity = dht.readHumidity();
  if (isfinite(measuredTemperature) && measuredTemperature >= -40.0f && measuredTemperature <= 80.0f) {
    insideTemperature = measuredTemperature;
    sensorHasValue = true;
    lastValidSensor = now;
  }
  if (isfinite(measuredHumidity) && measuredHumidity >= 0.0f && measuredHumidity <= 100.0f) {
    humidity = measuredHumidity;
  }
}

void checkButton(Button &button, const char *topic, float delta, unsigned long now) {
  bool pressed = digitalRead(button.pin) == LOW;
  if (pressed != button.rawPressed) {
    button.rawPressed = pressed;
    button.changedAt = now;
  }
  if (pressed != button.stablePressed) {
    if (!elapsed(now, button.changedAt, BUTTON_DEBOUNCE_MS)) return;
    button.stablePressed = pressed;
    publish(topic, pressed ? "1" : "0"); // Nur Flanken, keine MQTT-Tasterflut beim Halten.
    if (pressed) {
      button.pressedAt = now;
      button.lastRepeatAt = now;
      setSetpoint(constrain(setpoint + delta, MIN_SETPOINT, MAX_SETPOINT));
    }
    return;
  }
  if (pressed && elapsed(now, button.pressedAt, BUTTON_REPEAT_DELAY_MS) &&
      elapsed(now, button.lastRepeatAt, BUTTON_REPEAT_INTERVAL_MS)) {
    button.lastRepeatAt = now;
    setSetpoint(constrain(setpoint + delta, MIN_SETPOINT, MAX_SETPOINT));
  }
}

void checkMotion(unsigned long now) {
  if (digitalRead(MOTION_PIN) == HIGH) lastMotionHigh = now;
  bool active = lastMotionHigh != 0 && !elapsed(now, lastMotionHigh, MOTION_OFF_DELAY_MS);
  if (active == motionActive) return;
  motionActive = active;
  publish("bewegung", motionActive ? "1" : "0");
}

void showNotification(const char *message, unsigned long now) {
  strncpy(notice, message, sizeof(notice) - 1);
  notice[sizeof(notice) - 1] = '\0';
  noticeScrolling = forceScroll || strlen(notice) > 12;
  noticeOffset = matrix.width();
  noticeUntil = now + 5000;
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  char message[MQTT_BUFFER_BYTES];
  if (length >= sizeof(message)) return;
  memcpy(message, payload, length);
  message[length] = '\0';
  unsigned long now = millis();

  if (strcmp(topic, smartHomeSetpointTopic) == 0 || topicIs(topic, "Solltemperatur") ||
      topicIs(topic, "solltemperatur/set")) {
    float value;
    if (parseNumber(message, value, MIN_SETPOINT, MAX_SETPOINT)) {
      setSetpoint(value, strcmp(topic, smartHomeSetpointTopic) == 0);
    }
  } else if (topicIs(topic, "hysterese/set")) {
    float value;
    if (parseNumber(message, value, MIN_HYSTERESIS, MAX_HYSTERESIS)) setHysteresis(value);
  } else if (topicIs(topic, "text")) {
    if (strcmp(message, "RESET") == 0) ESP.restart();
    showNotification(message, now);
  } else if (topicIs(topic, "speed")) {
    scrollWaitMs = constrain(atoi(message), 10, 500);
  } else if (topicIs(topic, "scroll")) {
    forceScroll = strcmp(message, "1") == 0;
  } else if (topicIs(topic, "intensity")) {
    intensity = constrain(atoi(message), 0, 15);
    matrix.setIntensity(intensity);
  } else if (topicIs(topic, "abstand")) {
    charSpacing = constrain(atoi(message), 5, 8);
  } else {
    char otherRelayTopic[24];
    snprintf(otherRelayTopic, sizeof(otherRelayTopic), "%s/relais", otherRoot);
    if (strcmp(topic, otherRelayTopic) == 0 &&
        (strcmp(message, "0") == 0 || strcmp(message, "1") == 0)) {
      int8_t newState = message[0] - '0';
      if (otherRelayState != -1 && otherRelayState != newState) muteMatrix(now);
      otherRelayState = newState;
    } else if (strcmp(topic, "tele/schalter2/SENSOR") == 0) {
      // Optionaler Aussensensor. Keine Abhaengigkeit fuer die Innenraumregelung.
      const char *field = strstr(message, "\"Temperature\":");
      if (field != nullptr) {
        float value = strtof(field + strlen("\"Temperature\":"), nullptr);
        if (isfinite(value) && value >= -40.0f && value <= 80.0f) outsideTemperature = value;
      }
    }
  }
}

void maintainConnections(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED) {
    if (elapsed(now, lastWifiAttempt, WIFI_RETRY_MS)) {
      lastWifiAttempt = now;
      WiFi.reconnect();
    }
    return;
  }
  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }
  if (!elapsed(now, lastMqttAttempt, MQTT_RETRY_MS)) return;
  lastMqttAttempt = now;
  if (!mqtt.connect(ownRoot, willTopic, 0, true, "OFFLINE")) return;
  // Lokale Aenderungen aus Offline-Phasen gehen vor einem alten retained MQTT-Wert vor.
  if (setpointPendingSmartHomeSync && !syncSetpointToSmartHome()) {
    mqtt.disconnect();
    return;
  }
  const char *inputs[] = {"text", "speed", "scroll", "intensity", "abstand",
                          "solltemperatur/set", "Solltemperatur", "hysterese/set"};
  char topic[48];
  for (const char *input : inputs) {
    snprintf(topic, sizeof(topic), "%s/%s", ownRoot, input);
    mqtt.subscribe(topic);
  }
  mqtt.subscribe(smartHomeSetpointTopic);
  snprintf(topic, sizeof(topic), "%s/relais", otherRoot);
  mqtt.subscribe(topic);
  mqtt.subscribe("tele/schalter2/SENSOR");
  publish("LWT", "ONLINE", true);
  publishFloat("solltemperatur", setpoint, 0, true);
  publishFloat("hysterese", hysteresis, 1, true);
  publishRelay();
  publish("bewegung", motionActive ? "1" : "0");
  publishTelemetry();
  lastMqttPublish = now;
}

void drawNumber(int x, float value) {
  char label[4] = {'-', '-', '\0', '\0'};
  if (isfinite(value)) {
    int whole = (int)value;
    if (whole >= 0 && whole <= 99) snprintf(label, sizeof(label), "%02d", whole);
    else if (whole >= -9 && whole < 0) snprintf(label, sizeof(label), "%2d", whole);
  }
  matrix.drawChar(x, 0, label[0], HIGH, LOW, 1);
  matrix.drawChar(x + 6, 0, label[1], HIGH, LOW, 1);
  if (isfinite(value)) {
    int bars = constrain((int)lroundf(fabsf(value - truncf(value)) * 8.0f), 0, 8);
    for (int i = 0; i < bars; i++) matrix.drawPixel(x + i, 7, HIGH);
  }
}

void render(unsigned long now) {
  if (matrixMuted) {
    if ((long)(now - matrixMuteUntil) < 0) return;
    matrix.shutdown(false);
    matrixMuted = false;
  }
  unsigned long frameInterval = notice[0] != '\0' && noticeScrolling ? scrollWaitMs : 100;
  if (!elapsed(now, lastFrame, frameInterval)) return;
  lastFrame = now;
  matrix.fillScreen(LOW);
  if (notice[0] != '\0') {
    if (noticeScrolling) {
      int length = strlen(notice);
      for (int i = 0; i < length; i++) {
        int x = noticeOffset + i * charSpacing;
        if (x > -6 && x < matrix.width()) matrix.drawChar(x, 0, notice[i], HIGH, LOW, 1);
      }
      noticeOffset--;
      if (noticeOffset + length * charSpacing < 0) notice[0] = '\0';
    } else if ((long)(now - noticeUntil) < 0) {
      for (size_t i = 0; i < strlen(notice); i++) {
        int x = 26 + i * charSpacing;
        if (x < matrix.width()) matrix.drawChar(x, 0, notice[i], HIGH, LOW, 1);
      }
    } else {
      notice[0] = '\0';
    }
    if (notice[0] != '\0') {
      matrix.write();
      return;
    }
  }
  char clockText[] = "--:--";
  time_t clockNow = time(nullptr);
  if (clockNow > 1700000000) {
    struct tm localTime;
    localtime_r(&clockNow, &localTime);
    snprintf(clockText, sizeof(clockText), "%02d:%02d", localTime.tm_hour, localTime.tm_min);
  }
  const int clockX[] = {0, 6, 11, 16, 22};
  for (int i = 0; i < 5; i++) matrix.drawChar(clockX[i], 0, clockText[i], HIGH, LOW, 1);
  drawNumber(32, setpoint);
  drawNumber(57, sensorHasValue && !elapsed(now, lastValidSensor, SENSOR_TIMEOUT_MS)
                    ? insideTemperature : NAN);
  drawNumber(82, outsideTemperature);
  matrix.write();
}

void setup() {
  Serial.begin(115200);
  snprintf(ownRoot, sizeof(ownRoot), "display%u", DISPLAY_NUMBER);
  snprintf(otherRoot, sizeof(otherRoot), "display%u", DISPLAY_NUMBER == 1 ? 2 : 1);
  snprintf(smartHomeSetpointTopic, sizeof(smartHomeSetpointTopic), "Display%u/Solltemperatur", DISPLAY_NUMBER);
  snprintf(hostname, sizeof(hostname), "Werkstatt-%s-Display%u",
           DISPLAY_NUMBER == 1 ? "Holz" : "Metall", DISPLAY_NUMBER);
  snprintf(willTopic, sizeof(willTopic), "%s/LWT", ownRoot);

  digitalWrite(RELAY_PIN, RELAY_ON_LEVEL == HIGH ? LOW : HIGH);
  pinMode(RELAY_PIN, OUTPUT);
  loadSetpoint();
  pinMode(MOTION_PIN, INPUT);
  pinMode(UP_PIN, INPUT_PULLUP);
  pinMode(DOWN_PIN, INPUT_PULLUP);
  upButton.rawPressed = upButton.stablePressed = digitalRead(UP_PIN) == LOW;
  downButton.rawPressed = downButton.stablePressed = digitalRead(DOWN_PIN) == LOW;
  upButton.pressedAt = upButton.lastRepeatAt = millis();
  downButton.pressedAt = downButton.lastRepeatAt = millis();
  dht.begin();
  matrix.setIntensity(intensity);
  for (int i = 0; i < HORIZONTAL_DISPLAYS; i++) matrix.setRotation(i, 1);

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostname);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // Kein Warten auf WLAN.
  mqtt.setServer(MQTT_SERVER, 1883);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(MQTT_BUFFER_BYTES);
  mqtt.setSocketTimeout(1); // MQTT-Antworten nicht unbegrenzt abwarten.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  Serial.printf("%s: Soll %.1f C, Hysterese %.1f C\n", hostname, setpoint, hysteresis);
}

void loop() {
  unsigned long now = millis();
  readSensor(now);
  checkButton(upButton, "hoch", SETPOINT_STEP, now);
  checkButton(downButton, "runter", -SETPOINT_STEP, now);
  checkMotion(now);
  updateRelay(now);
  maintainConnections(now);
  if (mqtt.connected() && elapsed(now, lastMqttPublish, MQTT_INTERVAL_MS)) {
    publishTelemetry();
    lastMqttPublish = now;
  }
  render(now);
  yield();
}
