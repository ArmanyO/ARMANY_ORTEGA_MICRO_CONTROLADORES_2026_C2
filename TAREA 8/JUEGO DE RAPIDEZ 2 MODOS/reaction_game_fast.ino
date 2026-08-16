#include <ESP8266WiFi.h>
#include <PubSubClient.h>

const char* ssid = "Rapidito";
const char* password = "Adm1N2584km";
const char* mqtt_server = "mqtt://broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_user = "USUARIO_MQTT";
const char* mqtt_pass = "PASSWORD_MQTT";

#define BTN1_PIN 5   // D1 = GPIO5
#define BTN2_PIN 4   // D2 = GPIO4
#define LED1_PIN 14  // D5 = GPIO14
#define LED2_PIN 12  // D6 = GPIO12

WiFiClient espClient;
PubSubClient client(espClient);

enum GameState {
  WAITING_START,
  HOLDING_BTN1,
  LED1_ON_WAIT_RELEASE,
  WAIT_BTN2_PRESS,
  SHOW_RESULTS
};

GameState state = WAITING_START;
volatile uint32_t led1_on_time = 0;
volatile uint32_t btn1_release_time = 0;
volatile uint32_t reaction1 = 0;
volatile uint32_t reaction2 = 0;

uint32_t btn1_debounce = 0;
uint32_t btn2_debounce = 0;
bool btn1_last = true;
bool btn2_last = true;
bool btn1_stable = true;
bool btn2_stable = true;

inline bool readBtn1() { return (GPI & (1 << BTN1_PIN)) == 0; }
inline bool readBtn2() { return (GPI & (1 << BTN2_PIN)) == 0; }
inline void led1On()   { GPOS = (1 << LED1_PIN); }
inline void led1Off()  { GPOC = (1 << LED1_PIN); }
inline void led2On()   { GPOS = (1 << LED2_PIN); }
inline void led2Off()  { GPOC = (1 << LED2_PIN); }

void setup_wifi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
}

void reconnect() {
  while (!client.connected()) {
    String clientId = "ESP8266-Game-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      client.subscribe("reaction_game/control");
    } else {
      delay(5000);
    }
  }
}

void publishTimes() {
  String payload = "{\"reaction1_ms\":" + String(reaction1) + ",\"reaction2_ms\":" + String(reaction2) + "}";
  client.publish("reaction_game/times", payload.c_str());
}

void setup() {
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  led1Off(); led2Off();

  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  randomSeed(analogRead(A0));
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  uint32_t now = micros();
  bool btn1_raw = readBtn1();
  bool btn2_raw = readBtn2();

  if (btn1_raw != btn1_last) { btn1_debounce = now; btn1_last = btn1_raw; }
  if (btn2_raw != btn2_last) { btn2_debounce = now; btn2_last = btn2_raw; }

  if (now - btn1_debounce > 2000) btn1_stable = btn1_raw;  // 2ms debounce
  if (now - btn2_debounce > 2000) btn2_stable = btn2_raw;

  static bool btn1_prev = true, btn2_prev = true;
  bool btn1_pressed = !btn1_stable;
  bool btn2_pressed = !btn2_stable;
  bool btn1_fell  = btn1_prev && !btn1_pressed;
  bool btn1_rose  = !btn1_prev && btn1_pressed;
  bool btn2_fell  = btn2_prev && !btn2_pressed;
  btn1_prev = btn1_pressed;
  btn2_prev = btn2_pressed;

  switch (state) {
    case WAITING_START:
      led1Off(); led2Off();
      if (btn1_fell) state = HOLDING_BTN1;
      break;

    case HOLDING_BTN1:
      if (btn1_rose) { state = WAITING_START; break; }
      static uint32_t hold_start = 0;
      if (hold_start == 0) hold_start = now;
      if (now - hold_start > (random(1000000, 8000000))) {
        led1On();
        led1_on_time = now;
        state = LED1_ON_WAIT_RELEASE;
        hold_start = 0;
      }
      break;

    case LED1_ON_WAIT_RELEASE:
      if (btn1_rose) {
        reaction1 = (now - led1_on_time) / 1000;
        btn1_release_time = now;
        led1Off();
        state = WAIT_BTN2_PRESS;
      }
      break;

    case WAIT_BTN2_PRESS:
      if (btn2_fell) {
        reaction2 = (now - btn1_release_time) / 1000;
        led2On();
        state = SHOW_RESULTS;
      }
      break;

    case SHOW_RESULTS:
      publishTimes();
      delay(2000);
      led2Off();
      state = WAITING_START;
      break;
  }
}