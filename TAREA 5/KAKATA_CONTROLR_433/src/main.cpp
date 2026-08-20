#include <Arduino.h>
#include "pin_config.h"

#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <MPU6050.h>
#include <RCSwitch.h>
#include <Preferences.h>
#include <WiFi.h>
#include <PubSubClient.h>

#define WIFI_SSID     "KAKATA_WIFI"
#define WIFI_PASS     "kakata123"
#define MQTT_BROKER   "192.168.1.100"
#define MQTT_PORT     1883
#define MQTT_TOPIC    "kakata/ctrl"
#define PUBLISH_MS    200

#define OLED_ADDR    0x3C
#define OLED_WIDTH   128
#define OLED_HEIGHT  64
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

MPU6050 mpu;
RCSwitch rfSend = RCSwitch();
RCSwitch rfRecv = RCSwitch();
Preferences prefs;

struct Button {
  uint8_t pin;
  bool    state;
  uint32_t lastDebounce;
};

#define DEBOUNCE_MS   20
#define NUM_BTNS      10

Button buttons[NUM_BTNS] = {
  { PIN_JOY0_BTN, HIGH, 0 },
  { PIN_JOY1_BTN, HIGH, 0 },
  { PIN_BTN_0,    HIGH, 0 },
  { PIN_BTN_1,    HIGH, 0 },
  { PIN_BTN_2,    HIGH, 0 },
  { PIN_BTN_3,    HIGH, 0 },
  { PIN_BTNL1,    HIGH, 0 },
  { PIN_BTNL2,    HIGH, 0 },
  { PIN_BTNL3,    HIGH, 0 },
  { PIN_BTNL4,    HIGH, 0 },
};

#define NUM_LEDS 6
const uint8_t ledPins[NUM_LEDS] = {
  PIN_LED1, PIN_LED2, PIN_LED3,
  PIN_LED4, PIN_LED5, PIN_LED6,
};

const float VBAT_DIVIDER = (100.0 + 22.0) / 22.0;
const float VREF         = 3.3;

struct CalibData {
  int joy0MT_min, joy0MT_max, joy0MT_center;
  int joy0MD_min, joy0MD_max, joy0MD_center;
  int joy1MT_min, joy1MT_max, joy1MT_center;
  int joy1MD_min, joy1MD_max, joy1MD_center;
};

CalibData cal = { 0, 4095, 2048, 0, 4095, 2048, 0, 4095, 2048, 0, 4095, 2048 };

void showSplashAnimation() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  for (int x = OLED_WIDTH; x > (OLED_WIDTH - 6 * 18) / 2; x -= 6) {
    display.clearDisplay();
    display.setTextSize(3);
    display.setCursor(x, 16);
    display.print("KKC433");
    display.display();
    delay(20);
  }

  for (int y = 0; y < OLED_HEIGHT / 2; y += 2) {
    display.clearDisplay();
    display.setTextSize(3);
    display.setCursor((OLED_WIDTH - 6 * 18) / 2, 16);
    display.print("KKC433");
    display.fillRect(0, 0, OLED_WIDTH, y, SSD1306_WHITE);
    display.display();
    delay(12);
  }

  display.clearDisplay();
  display.setTextSize(3);
  display.setCursor((OLED_WIDTH - 6 * 18) / 2, 16);
  display.print("KKC433");
  display.display();
  delay(300);

  display.setTextSize(1);
  display.setCursor(20, 54);
  display.print("KAKATA RC433 V1");
  display.display();
  delay(600);
}

void calibrateController() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 0);
  display.println("CALIBRACION");
  display.setCursor(0, 12);
  display.println("Suelta todos los");
  display.setCursor(0, 20);
  display.println("botones y joysticks");
  display.setCursor(35, 40);
  display.print("KKC433");
  display.display();
  delay(1500);

  prefs.begin("calib", false);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Centrando joysticks...");
  display.display();

  delay(500);
  int sum0MT = 0, sum0MD = 0, sum1MT = 0, sum1MD = 0;
  int samples = 32;
  for (int i = 0; i < samples; i++) {
    sum0MT += analogRead(PIN_JOY0_MT);
    sum0MD += analogRead(PIN_JOY0_MD);
    sum1MT += analogRead(PIN_JOY1_MT);
    sum1MD += analogRead(PIN_JOY1_MD);
    delay(10);

    int prog = map(i, 0, samples - 1, 0, OLED_WIDTH);
    display.fillRect(0, 20, prog, 8, SSD1306_WHITE);
    display.display();
  }

  cal.joy0MT_center = sum0MT / samples;
  cal.joy0MD_center = sum0MD / samples;
  cal.joy1MT_center = sum1MT / samples;
  cal.joy1MD_center = sum1MD / samples;

  cal.joy0MT_min = cal.joy0MT_center - 400;
  cal.joy0MT_max = cal.joy0MT_center + 400;
  cal.joy0MD_min = cal.joy0MD_center - 400;
  cal.joy0MD_max = cal.joy0MD_center + 400;
  cal.joy1MT_min = cal.joy1MT_center - 400;
  cal.joy1MT_max = cal.joy1MT_center + 400;
  cal.joy1MD_min = cal.joy1MD_center - 400;
  cal.joy1MD_max = cal.joy1MD_center + 400;

  // --- Calibrar giroscopio (posición 0) ---
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Calibrando giroscopio...");
  display.setCursor(0, 10);
  display.println("No muevas el control");
  display.display();

  long gxSum = 0, gySum = 0, gzSum = 0;
  int gSamples = 200;
  for (int i = 0; i < gSamples; i++) {
    int16_t gx, gy, gz;
    if (mpu.testConnection()) {
      mpu.getRotation(&gx, &gy, &gz);
      gxSum += gx;
      gySum += gy;
      gzSum += gz;
    }
    int prog = map(i, 0, gSamples - 1, 0, OLED_WIDTH);
    display.fillRect(0, 24, prog, 8, SSD1306_WHITE);
    display.display();
    delay(10);
  }

  int16_t gxOffset = -(gxSum / gSamples);
  int16_t gyOffset = -(gySum / gSamples);
  int16_t gzOffset = -(gzSum / gSamples);

  mpu.setXGyroOffset(gxOffset);
  mpu.setYGyroOffset(gyOffset);
  mpu.setZGyroOffset(gzOffset);

  prefs.putShort("gx_off", gxOffset);
  prefs.putShort("gy_off", gyOffset);
  prefs.putShort("gz_off", gzOffset);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Mueve cada joystick");
  display.setCursor(0, 8);
  display.println("al maximo en todas");
  display.setCursor(0, 16);
  display.println("direcciones...");
  display.display();
  delay(1000);

  uint32_t t0 = millis();
  while (millis() - t0 < 3000) {
    int v0MT = analogRead(PIN_JOY0_MT);
    int v0MD = analogRead(PIN_JOY0_MD);
    int v1MT = analogRead(PIN_JOY1_MT);
    int v1MD = analogRead(PIN_JOY1_MD);

    if (v0MT < cal.joy0MT_min) cal.joy0MT_min = v0MT;
    if (v0MT > cal.joy0MT_max) cal.joy0MT_max = v0MT;
    if (v0MD < cal.joy0MD_min) cal.joy0MD_min = v0MD;
    if (v0MD > cal.joy0MD_max) cal.joy0MD_max = v0MD;
    if (v1MT < cal.joy1MT_min) cal.joy1MT_min = v1MT;
    if (v1MT > cal.joy1MT_max) cal.joy1MT_max = v1MT;
    if (v1MD < cal.joy1MD_min) cal.joy1MD_min = v1MD;
    if (v1MD > cal.joy1MD_max) cal.joy1MD_max = v1MD;

    int remained = 3 - (millis() - t0) / 1000;
    display.fillRect(0, 28, OLED_WIDTH, 8, SSD1306_BLACK);
    display.setCursor(0, 28);
    display.printf("Tiempo: %ds", remained);
    display.display();
    delay(20);
  }

  prefs.putInt("j0mt_min", cal.joy0MT_min);
  prefs.putInt("j0mt_max", cal.joy0MT_max);
  prefs.putInt("j0mt_med", cal.joy0MT_center);
  prefs.putInt("j0md_min", cal.joy0MD_min);
  prefs.putInt("j0md_max", cal.joy0MD_max);
  prefs.putInt("j0md_med", cal.joy0MD_center);
  prefs.putInt("j1mt_min", cal.joy1MT_min);
  prefs.putInt("j1mt_max", cal.joy1MT_max);
  prefs.putInt("j1mt_med", cal.joy1MT_center);
  prefs.putInt("j1md_min", cal.joy1MD_min);
  prefs.putInt("j1md_max", cal.joy1MD_max);
  prefs.putInt("j1md_med", cal.joy1MD_center);
  prefs.end();

  for (int i = 0; i < 3; i++) {
    display.clearDisplay();
    display.setCursor(25, 20);
    display.setTextSize(2);
    display.print("LISTO!");
    display.display();
    for (int l = 0; l < NUM_LEDS; l++) digitalWrite(ledPins[l], LOW);
    delay(200);
    for (int l = 0; l < NUM_LEDS; l++) digitalWrite(ledPins[l], HIGH);
    delay(200);
  }
  display.setTextSize(1);
}

void loadCalibration() {
  prefs.begin("calib", true);
  int32_t test = prefs.getInt("j0mt_min", -1);
  if (test != -1) {
    cal.joy0MT_min    = test;
    cal.joy0MT_max    = prefs.getInt("j0mt_max", 4095);
    cal.joy0MT_center = prefs.getInt("j0mt_med", 2048);
    cal.joy0MD_min    = prefs.getInt("j0md_min", 0);
    cal.joy0MD_max    = prefs.getInt("j0md_max", 4095);
    cal.joy0MD_center = prefs.getInt("j0md_med", 2048);
    cal.joy1MT_min    = prefs.getInt("j1mt_min", 0);
    cal.joy1MT_max    = prefs.getInt("j1mt_max", 4095);
    cal.joy1MT_center = prefs.getInt("j1mt_med", 2048);
    cal.joy1MD_min    = prefs.getInt("j1md_min", 0);
    cal.joy1MD_max    = prefs.getInt("j1md_max", 4095);
    cal.joy1MD_center = prefs.getInt("j1md_med", 2048);
  }
  int16_t gxOff = prefs.getShort("gx_off", 0);
  int16_t gyOff = prefs.getShort("gy_off", 0);
  int16_t gzOff = prefs.getShort("gz_off", 0);
  if (gxOff != 0 || gyOff != 0 || gzOff != 0) {
    mpu.setXGyroOffset(gxOff);
    mpu.setYGyroOffset(gyOff);
    mpu.setZGyroOffset(gzOff);
  }
  prefs.end();
}

int normalizeAxis(int raw, int minVal, int maxVal, int centerVal) {
  if (raw < centerVal) {
    return map(raw, minVal, centerVal, -100, 0);
  } else {
    return map(raw, centerVal, maxVal, 0, 100);
  }
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("WiFi ");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(250);
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("OK");
  } else {
    Serial.println("FAIL");
  }
}

void connectMQTT() {
  if (mqttClient.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  if (mqttClient.connect("kakata-ctrl")) {
    Serial.println("MQTT OK");
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  for (int i = 0; i < NUM_LEDS; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], HIGH);
  }

  for (int i = 0; i < NUM_BTNS; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
  }

  pinMode(PIN_JOY0_MT, INPUT);
  pinMode(PIN_JOY0_MD, INPUT);
  pinMode(PIN_JOY1_MT, INPUT);
  pinMode(PIN_JOY1_MD, INPUT);

  pinMode(PIN_VBAT, INPUT);

  pinMode(PIN_RF_ENABLE, OUTPUT);
  digitalWrite(PIN_RF_ENABLE, LOW);
  pinMode(PIN_RF_TX, OUTPUT);
  pinMode(PIN_RF_RX, INPUT);

  rfSend.enableTransmit(PIN_RF_TX);
  rfRecv.enableReceive(PIN_RF_RX);

  pinMode(PIN_MPU_INT, INPUT_PULLUP);

  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU-6050 no encontrado");
  } else {
    mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_2000);
    mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_16);
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED no encontrado");
  }

  loadCalibration();

  showSplashAnimation();

  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) connectMQTT();

  Serial.println("KAKATA RC433 V1 iniciado");
}

bool isPressed(Button &btn) {
  bool raw = digitalRead(btn.pin);
  if (raw != btn.state) {
    btn.lastDebounce = millis();
    btn.state = raw;
  }
  return ((millis() - btn.lastDebounce) > DEBOUNCE_MS) && (btn.state == LOW);
}

float readBatteryVoltage() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogRead(PIN_VBAT);
  }
  float avg = sum / 16.0;
  return (avg / 4095.0) * VREF * VBAT_DIVIDER;
}

void loop() {
  static uint32_t calHoldStart = 0;
  static bool calHolding = false;
  static bool calDone = false;
  static bool gyroMode = false;

  bool btn1 = !digitalRead(PIN_BTN_1);
  bool btn2 = !digitalRead(PIN_BTN_2);

  if (!btn1 || !btn2) {
    calDone = false;
  }

  if (btn1 && btn2 && !calDone) {
    if (!calHolding) {
      calHolding = true;
      calHoldStart = millis();
    } else if ((millis() - calHoldStart) >= 3000) {
      calibrateController();
      calHolding = false;
      calDone = true;
    }
  } else {
    calHolding = false;
  }

  static bool btn01done = false;
  bool btn0 = !digitalRead(PIN_BTN_0);
  bool btn01 = btn0 && btn1;
  if (btn01 && !btn01done) {
    gyroMode = !gyroMode;
    btn01done = true;
    for (int l = 0; l < NUM_LEDS; l++) digitalWrite(ledPins[l], HIGH);
    digitalWrite(ledPins[0], LOW);
    digitalWrite(ledPins[1], LOW);
    delay(200);
    for (int l = 0; l < NUM_LEDS; l++) digitalWrite(ledPins[l], HIGH);
  }
  if (!btn01 && btn01done) btn01done = false;

  int16_t ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
  if (mpu.testConnection()) {
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
  }

  int joy0MT, joy0MD, joy1MT, joy1MD;

  if (gyroMode) {
    float roll  = atan2(-ay, az) * 180.0 / PI;
    float pitch = atan2(ax, sqrt(ay * ay + az * az)) * 180.0 / PI;
    joy0MT = constrain(map((int)(roll * 10), -450, 450, -100, 100), -100, 100);
    joy0MD = constrain(map((int)(pitch * 10), -450, 450, -100, 100), -100, 100);
    if (abs(joy0MT) < 5) joy0MT = 0;
    if (abs(joy0MD) < 5) joy0MD = 0;
    joy1MT = 0;
    joy1MD = 0;
  } else {
    int raw0MT = analogRead(PIN_JOY0_MT);
    int raw0MD = analogRead(PIN_JOY0_MD);
    int raw1MT = analogRead(PIN_JOY1_MT);
    int raw1MD = analogRead(PIN_JOY1_MD);
    joy0MT = normalizeAxis(raw0MT, cal.joy0MT_min, cal.joy0MT_max, cal.joy0MT_center);
    joy0MD = normalizeAxis(raw0MD, cal.joy0MD_min, cal.joy0MD_max, cal.joy0MD_center);
    joy1MT = normalizeAxis(raw1MT, cal.joy1MT_min, cal.joy1MT_max, cal.joy1MT_center);
    joy1MD = normalizeAxis(raw1MD, cal.joy1MD_min, cal.joy1MD_max, cal.joy1MD_center);
  }

  uint16_t btnState = 0;
  for (int i = 0; i < NUM_BTNS; i++) {
    if (isPressed(buttons[i])) {
      btnState |= (1 << i);
    }
  }

  float vbat = readBatteryVoltage();

  static bool rfHasData = false;
  static unsigned int rfValue = 0;
  if (rfRecv.available()) {
    rfValue = rfRecv.getReceivedValue();
    Serial.printf("RF RX: %u\n", rfValue);
    rfRecv.resetAvailable();
    rfHasData = true;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.printf("%s J0:%+3d %+3d", gyroMode ? "GY" : "JO", joy0MT, joy0MD);
  display.setCursor(72, 0);
  display.printf("J1:%+3d %+3d", joy1MT, joy1MD);

  display.setCursor(0, 10);
  display.printf("BTN:%04X", btnState);

  display.setCursor(0, 20);
  display.printf("GYRO:%4d %4d %4d", gx, gy, gz);

  display.setCursor(0, 30);
  display.printf("BAT:%.2fV", vbat);

  display.setCursor(0, 40);
  display.printf("RF:%s", rfHasData ? "RX" : "idle");
  if (rfHasData) display.printf(" %u", rfValue);

  if (calHolding) {
    int remaining = 3 - (millis() - calHoldStart) / 1000;
    display.setCursor(0, 50);
    display.printf("CAL %ds", remaining + 1);
  } else if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(80, 50);
    display.printf("MQTT:%s", mqttClient.connected() ? "OK" : "...");
  }

  display.display();

  static uint32_t lastPub = 0;
  if (WiFi.status() == WL_CONNECTED && (millis() - lastPub > PUBLISH_MS)) {
    if (!mqttClient.connected()) connectMQTT();
    if (mqttClient.connected()) {
      char buf[160];
      snprintf(buf, sizeof(buf),
        "{\"j0\":[%d,%d],\"j1\":[%d,%d],\"btn\":%u,\"gyr\":[%d,%d,%d],\"mod\":\"%s\",\"bat\":%.2f}",
        joy0MT, joy0MD, joy1MT, joy1MD, btnState, gx, gy, gz,
        gyroMode ? "GY" : "JO", vbat);
      mqttClient.publish(MQTT_TOPIC, buf);
      mqttClient.loop();
    }
    lastPub = millis();
  }

  static uint32_t lastBlink = 0;
  if (millis() - lastBlink > 1000) {
    lastBlink = millis();
    digitalWrite(PIN_LED1, !digitalRead(PIN_LED1));
  }

  delay(20);
}
