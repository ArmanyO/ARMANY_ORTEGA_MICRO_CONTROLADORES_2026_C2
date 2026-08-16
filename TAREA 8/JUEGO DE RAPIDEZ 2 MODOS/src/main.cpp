#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ===== CONFIGURACIÓN WIFI =====
const char* ssid = "Rapidito";
const char* password = "Adm1N2584km";

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

enum GameMode {
  MODE_REACTION = 0,
  MODE_10_PRESSES = 1
};

enum GameState {
  WAITING_START,
  HOLDING_BTN1,
  LED1_ON_WAIT_RELEASE,
  WAIT_BTN2_PRESS,
  SHOW_RESULTS,
  MODE10_WAIT_FIRST,
  MODE10_COUNTING,
  MODE10_SHOW_RESULTS
};

GameMode gameMode = MODE_REACTION;
GameState state = WAITING_START;
volatile uint32_t led1_on_time = 0;
volatile uint32_t btn1_release_time = 0;
volatile uint32_t reaction1 = 0;
volatile uint32_t reaction2 = 0;

uint32_t mode10_press_times[10];
uint8_t mode10_press_count = 0;
uint32_t mode10_last_press_time = 0;
uint32_t mode10_avg_time = 0;

// Variables Debounce
uint32_t btn1_debounce = 0;
bool btn1_last = false;
bool btn1_stable = false;

uint32_t btn2_debounce = 0;
bool btn2_last = false;
bool btn2_stable = false;

uint32_t hold_start = 0;
uint32_t both_hold_start = 0;
bool both_held_triggered = false;

// Lectura de hardware
inline bool readBtn1() { return digitalRead(BTN1_PIN) == LOW; }
inline bool readBtn2() { return digitalRead(BTN2_PIN) == LOW; }
inline void led1On()   { digitalWrite(LED1_PIN, HIGH); }
inline void led1Off()  { digitalWrite(LED1_PIN, LOW); }
inline void led2On()   { digitalWrite(LED2_PIN, HIGH); }
inline void led2Off()  { digitalWrite(LED2_PIN, LOW); }

// Forward declarations
void publishCurrentMode();
void toggleGameMode();

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
      client.publish("topic/qos0", "ESP32 listo para jugar");
      publishCurrentMode();
    } else {
      Serial.print(" Fallo rc=");
      Serial.print(client.state());
      Serial.println(" Reintentando en 2s...");
      delay(2000);
    }
  }
}

void publishCurrentMode() {
  String modeStr = (gameMode == MODE_REACTION) ? "reaction" : "10presses";
  String payload = "{\"mode\":\"" + modeStr + "\"}";
  client.publish("reaction_game/mode", payload.c_str());
  
  String msg = (gameMode == MODE_REACTION) 
    ? "Modo: REACCION. Presiona BTN1 (GPIO4) para empezar."
    : "Modo: 10 PULSACIONES. Presiona BTN2 (GPIO13) 10 veces.";
  client.publish("topic/qos0", msg.c_str());
  Serial.println("-> Modo actual publicado: " + modeStr);
}

void toggleGameMode() {
  if (gameMode == MODE_REACTION) {
    gameMode = MODE_10_PRESSES;
    state = MODE10_WAIT_FIRST;
    mode10_press_count = 0;
    Serial.println("-> Cambiando a MODO 10 PULSACIONES");
  } else {
    gameMode = MODE_REACTION;
    state = WAITING_START;
    Serial.println("-> Cambiando a MODO REACCION");
  }
  publishCurrentMode();
  
  // Feedback visual: parpadeo rápido ambos LEDs
  for (int i = 0; i < 6; i++) {
    led1On(); led2On(); delay(80);
    led1Off(); led2Off(); delay(80);
  }
}

void publishTimes() {
  String payload = "{\"reaction1_ms\":" + String(reaction1) + ",\"reaction2_ms\":" + String(reaction2) + "}";
  client.publish("reaction_game/times", payload.c_str());
  
  String msg = "Reaccion 1: " + String(reaction1) + " ms | Reaccion 2: " + String(reaction2) + " ms";
  client.publish("topic/qos0", msg.c_str());
  Serial.println("-> Datos publicados en MQTT!");
}

void publishMode10Results() {
  String payload = "{\"mode\":\"10presses\",\"avg_ms\":" + String(mode10_avg_time) + ",\"presses\":" + String(mode10_press_count) + "}";
  client.publish("reaction_game/times", payload.c_str());
  
  String msg = "Modo 10 pulsaciones - Promedio: " + String(mode10_avg_time) + " ms";
  client.publish("topic/qos0", msg.c_str());
  Serial.println("-> Resultados modo 10 pulsaciones publicados en MQTT!");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- Iniciando ESP32 ---");

  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  led1Off(); 
  led2Off();

  setup_wifi();
  randomSeed(esp_random()); 
  client.setServer(mqtt_server, mqtt_port);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  uint32_t now = micros();

  // Debounce Botón 1
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

  // Detección: AMBOS botones presionados 4 segundos = toggle modo
  static bool both_prev = false;
  bool both_pressed = btn1_pressed && btn2_pressed;
  bool both_fell    = both_pressed && !both_prev;
  bool both_rose    = !both_pressed && both_prev;
  both_prev = both_pressed;

  if (both_fell) {
    both_hold_start = now;
    both_held_triggered = false;
  }
  if (both_rose) {
    both_hold_start = 0;
    both_held_triggered = false;
  }
  if (both_pressed && !both_held_triggered && both_hold_start > 0) {
    if (now - both_hold_start > 4000000) { // 4 segundos en microsegundos
      both_held_triggered = true;
      toggleGameMode();
    }
  }

  // Máquina de Estados según modo de juego
  if (gameMode == MODE_REACTION) {
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
          Serial.printf("-> Reaccion 1: %d ms. Presiona Boton 2 (GPIO13)...\n", reaction1);
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
      
      default:
        state = WAITING_START;
        break;
    }
  } else if (gameMode == MODE_10_PRESSES) {
    switch (state) {
      case MODE10_WAIT_FIRST:
        led1Off();
        led2Off();
        mode10_press_count = 0;
        if (btn2_fell) {
          mode10_press_times[0] = now;
          mode10_last_press_time = now;
          mode10_press_count = 1;
          Serial.println("-> Primera pulsacion detectada. Continua...");
          state = MODE10_COUNTING;
        }
        break;

      case MODE10_COUNTING:
        if (btn2_fell && mode10_press_count < 10) {
          mode10_press_times[mode10_press_count] = now;
          mode10_press_count++;
          Serial.printf("-> Pulsacion %d/10\n", mode10_press_count);
          
          if (mode10_press_count >= 10) {
            uint32_t total_intervals = 0;
            for (int i = 1; i < 10; i++) {
              total_intervals += (mode10_press_times[i] - mode10_press_times[i-1]) / 1000;
            }
            mode10_avg_time = total_intervals / 9;
            Serial.printf("-> 10 pulsaciones completadas. Promedio: %d ms\n", mode10_avg_time);
            led2On();
            state = MODE10_SHOW_RESULTS;
          }
        }
        break;

      case MODE10_SHOW_RESULTS:
        publishMode10Results();
        delay(2000);
        led2Off();
        state = MODE10_WAIT_FIRST;
        break;
      
      default:
        state = MODE10_WAIT_FIRST;
        break;
    }
  }
}