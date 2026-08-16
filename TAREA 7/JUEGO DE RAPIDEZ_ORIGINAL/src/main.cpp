#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ===== CONFIGURACIÓN WIFI =====
const char* ssid = "Las Penas";
const char* password = "Pena123321";

// ===== MQTT BROKER =====
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_user = "";
const char* mqtt_pass = "";

// ===== PINES =====
#define BTN1_PIN 4        // GPIO4 = Botón 1 (Active LOW, pull-up)
#define BTN2_PIN 13       // GPIO13 = Botón 2 (Active LOW, pull-up)
#define LED1_PIN 2        // GPIO2 = LED integrado
#define LED2_PIN 15       // GPIO15 = Segundo LED

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

// Variables Debounce
uint32_t btn1_debounce = 0;
bool btn1_last = false;
bool btn1_stable = false;

uint32_t btn2_debounce = 0;
bool btn2_last = false;
bool btn2_stable = false;

uint32_t hold_start = 0;

// Lectura de hardware
inline bool readBtn1() { return digitalRead(BTN1_PIN) == LOW; } // true si está presionado
inline bool readBtn2() { return digitalRead(BTN2_PIN) == LOW; } // true si está presionado
inline void led1On()   { digitalWrite(LED1_PIN, HIGH); }
inline void led1Off()  { digitalWrite(LED1_PIN, LOW); }
inline void led2On()   { digitalWrite(LED2_PIN, HIGH); }
inline void led2Off()  { digitalWrite(LED2_PIN, LOW); }

void setup_wifi() {
  Serial.print("Conectando a Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Conectado!");
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Intentando conexion MQTT...");
    String clientId = "ESP32-Game-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println(" Conectado a EMQX!");
      client.subscribe("reaction_game/control");
      client.publish("topic/qos0", "ESP32 listo para jugar");
    } else {
      Serial.print(" Fallo rc=");
      Serial.print(client.state());
      Serial.println(" Reintentando en 2s...");
      delay(2000);
    }
  }
}

void publishTimes() {
  String payload = "{\"reaction1_ms\":" + String(reaction1) + ",\"reaction2_ms\":" + String(reaction2) + "}";
  client.publish("reaction_game/times", payload.c_str());
  
  String msg = "Reaccion 1: " + String(reaction1) + " ms | Reaccion 2: " + String(reaction2) + " ms";
  client.publish("topic/qos0", msg.c_str());
  Serial.println("-> Datos publicados en MQTT!");
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Tiempo para estabilizar el Monitor Serie
  Serial.println("\n--- Iniciando ESP32 ---");

  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  led1Off(); 
  led2Off();

  // Red y Semilla aleatoria usando hardware del ESP32
  setup_wifi();
  randomSeed(esp_random()); 
  client.setServer(mqtt_server, mqtt_port);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  uint32_t now = micros();

  // Debounce Botón 1 (BOOT)
  bool btn1_raw = readBtn1();
  if (btn1_raw != btn1_last) { 
    btn1_debounce = now; 
    btn1_last = btn1_raw; 
  }
  if (now - btn1_debounce > 2000) { 
    btn1_stable = btn1_raw;
  }

  // Debounce Botón 2
  bool btn2_raw = readBtn2();
  if (btn2_raw != btn2_last) { 
    btn2_debounce = now; 
    btn2_last = btn2_raw; 
  }
  if (now - btn2_debounce > 2000) { 
    btn2_stable = btn2_raw;
  }

  // Detección de flancos
  static bool btn1_prev = false;
  bool btn1_pressed = btn1_stable;
  bool btn1_fell    = btn1_pressed && !btn1_prev;  
  bool btn1_rose    = !btn1_pressed && btn1_prev;  
  btn1_prev = btn1_pressed;

  static bool btn2_prev = false;
  bool btn2_pressed = btn2_stable;
  bool btn2_fell    = btn2_pressed && !btn2_prev; 
  bool btn2_rose    = !btn2_pressed && btn2_prev;
  btn2_prev = btn2_pressed;

  // Máquina de Estados
  switch (state) {
    case WAITING_START:
      led1Off(); 
      led2Off();
      hold_start = 0;
      if (btn1_fell) {
        Serial.println("-> Boton 1 PRESIONADO. Esperando tiempo aleatorio...");
        state = HOLDING_BTN1;
      }
      break;

    case HOLDING_BTN1:
      if (btn1_rose) { 
        Serial.println("-> Boton 1 soltado muy rapido. Reiniciando...");
        hold_start = 0;
        state = WAITING_START; 
        break; 
      }
      
      if (hold_start == 0) hold_start = now;
      
      if (now - hold_start > (random(1500000, 4500000))) {
        led1On();
        led1_on_time = now;
        Serial.println("-> LED 1 ENCENDIDO! Suelta el Boton 1!");
        state = LED1_ON_WAIT_RELEASE;
        hold_start = 0;
      }
      break;

    case LED1_ON_WAIT_RELEASE:
      if (btn1_rose) {
        btn1_release_time = now;
        reaction1 = (btn1_release_time - led1_on_time) / 1000;
        Serial.printf("-> Reaccion 1: %d ms. Presiona Boton 2 (GPIO12)...\n", reaction1);
        led1Off();
        state = WAIT_BTN2_PRESS;
      }
      break;

    case WAIT_BTN2_PRESS:
      if (btn2_fell) {
        uint32_t btn2_time = now;
        reaction2 = (btn2_time - btn1_release_time) / 1000;
        Serial.printf("-> Reaccion 2: %d ms.\n", reaction2);
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