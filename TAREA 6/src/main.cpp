#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_system.h>
#include "soc/gpio_reg.h"

// Hardware.
constexpr uint8_t START_TOUCH_PIN = 13;   // T4 / GPIO13, hold finger here first
constexpr uint8_t LED1_PIN = 2;           // Built-in LED on many ESP32 dev boards
constexpr uint8_t TARGET_TOUCH_PIN = 4;   // T0 / GPIO4, touch after releasing GPIO13
constexpr uint8_t LED2_PIN = 15;          // Requested LED2 output

constexpr uint32_t START_TOUCH_DEBOUNCE_US = 50000UL;
constexpr uint32_t START_RELEASE_DEBOUNCE_US = 2000UL;
constexpr uint32_t TOUCH_DEBOUNCE_US = 50000UL;
constexpr uint32_t TOUCH_CALIBRATION_MS = 2000UL;
constexpr uint32_t TOUCH_SAMPLE_EVERY_MS = 250UL;
constexpr uint32_t RANDOM_MIN_US = 1000000UL;
constexpr uint32_t RANDOM_MAX_US = 8000000UL;

const char *WIFI_SSID = "Las Penas";
const char *WIFI_PASSWORD = "Pena123321";
const char *MQTT_HOST = "broker.emqx.io";
constexpr uint16_t MQTT_PORT = 1883;
const char *MQTT_TOPIC = "reaction_game/times";

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

enum class GameState {
  WaitForBoot,
  RandomDelay,
  WaitForRelease,
  WaitForTouch,
  PublishResult,
  Cooldown
};

GameState state = GameState::WaitForBoot;

uint32_t startTouchCandidateUs = 0;
bool startTouchCandidateActive = false;

uint32_t waitStartUs = 0;
uint32_t randomDelayUs = 0;
uint32_t ledOnUs = 0;
uint32_t releaseCandidateUs = 0;
bool releaseCandidateActive = false;
uint32_t releaseUs = 0;
uint32_t touchCandidateUs = 0;
bool touchCandidateActive = false;
uint32_t reaction1Us = 0;
uint32_t reaction2Us = 0;
uint32_t cooldownStartMs = 0;
uint32_t lastTouchPrintMs = 0;
uint32_t lastWifiAttemptMs = 0;
uint32_t lastMqttAttemptMs = 0;
uint32_t lastPublishAttemptMs = 0;
uint16_t startTouchBaseline = 0;
uint16_t startTouchThreshold = 0;
uint16_t targetTouchBaseline = 0;
uint16_t targetTouchThreshold = 0;
char resultJson[96] = {0};

static inline uint32_t pinMask(uint8_t pin) {
  return 1UL << pin;
}

static inline void ledOnFast(uint8_t pin) {
  REG_WRITE(GPIO_OUT_W1TS_REG, pinMask(pin));
}

static inline void ledOffFast(uint8_t pin) {
  REG_WRITE(GPIO_OUT_W1TC_REG, pinMask(pin));
}

static inline uint32_t elapsedUs(uint32_t sinceUs) {
  return micros() - sinceUs;
}

static inline uint32_t usToRoundedMs(uint32_t valueUs) {
  return (valueUs + 500UL) / 1000UL;
}

void clearLeds() {
  ledOffFast(LED1_PIN);
  ledOffFast(LED2_PIN);
}

void printTouchStatus() {
  const uint16_t startRaw = touchRead(START_TOUCH_PIN);
  const uint16_t targetRaw = touchRead(TARGET_TOUCH_PIN);
  Serial.print(F("GPIO13 raw="));
  Serial.print(startRaw);
  Serial.print(F(" base="));
  Serial.print(startTouchBaseline);
  Serial.print(F(" th="));
  Serial.print(startTouchThreshold);
  Serial.print(F(" | GPIO4 raw="));
  Serial.print(targetRaw);
  Serial.print(F(" base="));
  Serial.print(targetTouchBaseline);
  Serial.print(F(" th="));
  Serial.println(targetTouchThreshold);
}

uint16_t thresholdFromBaseline(uint16_t baseline) {
  const uint16_t defaultThreshold = (baseline * 70U) / 100U;
  return defaultThreshold < 5U ? 5U : defaultThreshold;
}

void calibrateTouch() {
  Serial.println(F("Calibrando touch GPIO13/T4 y GPIO4/T0: no toques los pines..."));

  uint32_t startTotal = 0;
  uint32_t targetTotal = 0;
  uint16_t samples = 0;
  const uint32_t startMs = millis();

  while (millis() - startMs < TOUCH_CALIBRATION_MS) {
    startTotal += touchRead(START_TOUCH_PIN);
    targetTotal += touchRead(TARGET_TOUCH_PIN);
    samples++;
    delay(10);
  }

  startTouchBaseline = samples > 0 ? startTotal / samples : touchRead(START_TOUCH_PIN);
  targetTouchBaseline = samples > 0 ? targetTotal / samples : touchRead(TARGET_TOUCH_PIN);
  startTouchThreshold = thresholdFromBaseline(startTouchBaseline);
  targetTouchThreshold = thresholdFromBaseline(targetTouchBaseline);

  Serial.print(F("GPIO13 baseline="));
  Serial.print(startTouchBaseline);
  Serial.print(F(" threshold="));
  Serial.println(startTouchThreshold);
  Serial.print(F("GPIO4 baseline="));
  Serial.print(targetTouchBaseline);
  Serial.print(F(" threshold="));
  Serial.println(targetTouchThreshold);
  Serial.println(F("Comandos monitor: c=recalibrar, 1/2 ajustar GPIO13, +/- ajustar GPIO4, r=reset"));
}

bool startTouchDetected() {
  return touchRead(START_TOUCH_PIN) <= startTouchThreshold;
}

bool targetTouchDetected() {
  return touchRead(TARGET_TOUCH_PIN) <= targetTouchThreshold;
}

void resetGame(const __FlashStringHelper *reason = nullptr) {
  clearLeds();
  startTouchCandidateActive = false;
  releaseCandidateActive = false;
  touchCandidateActive = false;
  resultJson[0] = '\0';
  state = GameState::WaitForBoot;

  if (reason != nullptr) {
    Serial.print(F("Reset: "));
    Serial.println(reason);
  }
  Serial.println(F("Manten el dedo en GPIO13/T4 para iniciar ronda."));
}

void connectWifiIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  const uint32_t nowMs = millis();
  if (lastWifiAttemptMs != 0 && nowMs - lastWifiAttemptMs < 5000UL) {
    return;
  }
  lastWifiAttemptMs = nowMs;

  Serial.print(F("Conectando WiFi a "));
  Serial.println(WIFI_SSID);
  WiFi.disconnect(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectMqttIfNeeded() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) {
    return;
  }

  const uint32_t nowMs = millis();
  if (lastMqttAttemptMs != 0 && nowMs - lastMqttAttemptMs < 5000UL) {
    return;
  }
  lastMqttAttemptMs = nowMs;

  char clientId[48];
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(clientId, sizeof(clientId), "reaction-game-%04lX%08lX",
           static_cast<unsigned long>((mac >> 32) & 0xFFFFUL),
           static_cast<unsigned long>(mac & 0xFFFFFFFFUL));

  Serial.print(F("Conectando MQTT "));
  Serial.print(MQTT_HOST);
  Serial.print(F(":"));
  Serial.println(MQTT_PORT);

  if (mqtt.connect(clientId)) {
    Serial.println(F("MQTT conectado."));
  } else {
    Serial.print(F("MQTT fallo rc="));
    Serial.println(mqtt.state());
  }
}

void serviceNetwork() {
  connectWifiIfNeeded();
  connectMqttIfNeeded();

  if (mqtt.connected()) {
    mqtt.loop();
  }
}

void pollSerialCommands() {
  while (Serial.available() > 0) {
    const char cmd = Serial.read();

    if (cmd == 'c' && (state == GameState::WaitForBoot || state == GameState::Cooldown)) {
      calibrateTouch();
    } else if (cmd == '1') {
      startTouchThreshold += 1;
      printTouchStatus();
    } else if (cmd == '2' && startTouchThreshold > 1) {
      startTouchThreshold -= 1;
      printTouchStatus();
    } else if (cmd == '+') {
      targetTouchThreshold += 1;
      printTouchStatus();
    } else if (cmd == '-' && targetTouchThreshold > 1) {
      targetTouchThreshold -= 1;
      printTouchStatus();
    } else if (cmd == 'r') {
      resetGame(F("reset manual"));
    }
  }
}

bool stableStartTouchReady() {
  if (!startTouchDetected()) {
    startTouchCandidateActive = false;
    return false;
  }

  if (!startTouchCandidateActive) {
    startTouchCandidateUs = micros();
    startTouchCandidateActive = true;
    return false;
  }

  return elapsedUs(startTouchCandidateUs) >= START_TOUCH_DEBOUNCE_US;
}

void enterRandomDelay() {
  clearLeds();
  randomDelayUs = random(RANDOM_MIN_US, RANDOM_MAX_US + 1UL);
  waitStartUs = micros();
  state = GameState::RandomDelay;

  Serial.print(F("Ronda armada. Delay aleatorio aprox ms="));
  Serial.println(randomDelayUs / 1000UL);
}

void handleWaitForBoot() {
  serviceNetwork();

  const uint32_t nowMs = millis();
  if (nowMs - lastTouchPrintMs >= TOUCH_SAMPLE_EVERY_MS) {
    lastTouchPrintMs = nowMs;
    printTouchStatus();
  }

  if (stableStartTouchReady()) {
    enterRandomDelay();
  }
}

void handleRandomDelay() {
  serviceNetwork();

  if (!startTouchDetected()) {
    resetGame(F("quitaste GPIO13 antes del LED1"));
    return;
  }

  if (elapsedUs(waitStartUs) >= randomDelayUs) {
    ledOnFast(LED1_PIN);
    ledOnUs = micros();
    releaseCandidateActive = false;
    state = GameState::WaitForRelease;
    Serial.println(F("LED1 ON: quita el dedo de GPIO13."));
  }
}

void handleWaitForRelease() {
  if (startTouchDetected()) {
    releaseCandidateActive = false;
    return;
  }

  if (!releaseCandidateActive) {
    releaseCandidateUs = micros();
    releaseCandidateActive = true;
    return;
  }

  if (elapsedUs(releaseCandidateUs) >= START_RELEASE_DEBOUNCE_US) {
    releaseUs = releaseCandidateUs;
    reaction1Us = releaseUs - ledOnUs;
    ledOffFast(LED1_PIN);
    touchCandidateActive = false;
    state = GameState::WaitForTouch;

    Serial.print(F("reaction1_ms="));
    Serial.println(usToRoundedMs(reaction1Us));
    Serial.println(F("Toca GPIO4/T0."));
  }
}

void handleWaitForTouch() {
  if (targetTouchDetected()) {
    if (!touchCandidateActive) {
      touchCandidateUs = micros();
      reaction2Us = touchCandidateUs - releaseUs;
      ledOnFast(LED2_PIN);
      touchCandidateActive = true;

      Serial.print(F("reaction2_ms="));
      Serial.println(usToRoundedMs(reaction2Us));
      return;
    }

    if (elapsedUs(touchCandidateUs) >= TOUCH_DEBOUNCE_US) {
      snprintf(resultJson, sizeof(resultJson),
               "{\"reaction1_ms\":%lu,\"reaction2_ms\":%lu}",
               usToRoundedMs(reaction1Us), usToRoundedMs(reaction2Us));

      Serial.print(F("JSON listo: "));
      Serial.println(resultJson);
      state = GameState::PublishResult;
      lastPublishAttemptMs = 0;
    }
    return;
  }

  if (touchCandidateActive) {
    ledOffFast(LED2_PIN);
    touchCandidateActive = false;
  }
}

void handlePublishResult() {
  serviceNetwork();

  const uint32_t nowMs = millis();
  if (lastPublishAttemptMs != 0 && nowMs - lastPublishAttemptMs < 2000UL) {
    return;
  }
  lastPublishAttemptMs = nowMs;

  if (!mqtt.connected()) {
    Serial.println(F("MQTT no conectado; reintentando publicar..."));
    return;
  }

  if (mqtt.publish(MQTT_TOPIC, resultJson, false)) {
    Serial.print(F("Publicado en "));
    Serial.print(MQTT_TOPIC);
    Serial.print(F(": "));
    Serial.println(resultJson);
    cooldownStartMs = millis();
    state = GameState::Cooldown;
  } else {
    Serial.println(F("Publish MQTT fallo; reintentando..."));
  }
}

void handleCooldown() {
  serviceNetwork();

  if (millis() - cooldownStartMs >= 1500UL && !startTouchDetected()) {
    resetGame(F("ronda completada"));
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  clearLeds();

  randomSeed(esp_random());
  WiFi.mode(WIFI_STA);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);

  Serial.println();
  Serial.println(F("ESP32 Reaction Game"));
  Serial.println(F("Start touch=T4/GPIO13, LED1=GPIO2, Target touch=T0/GPIO4, LED2=GPIO15"));
  calibrateTouch();
  resetGame();
}

void loop() {
  pollSerialCommands();

  switch (state) {
    case GameState::WaitForBoot:
      handleWaitForBoot();
      break;
    case GameState::RandomDelay:
      handleRandomDelay();
      break;
    case GameState::WaitForRelease:
      handleWaitForRelease();
      break;
    case GameState::WaitForTouch:
      handleWaitForTouch();
      break;
    case GameState::PublishResult:
      handlePublishResult();
      break;
    case GameState::Cooldown:
      handleCooldown();
      break;
  }
}
