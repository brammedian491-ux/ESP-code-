/*
  ESP32-S3 SENDER - Lamp Unit with Sensors

  Reads:
  - MQ-135 analog output
  - MQ-7 analog output
  - GP2Y1010AU0F Sharp dust sensor analog output

  LED on GPIO 6 turns on when:
  - MQ135 value exceeds 800
  OR
  - MQ7 value exceeds 1000
*/

#include <esp_now.h>
#include <WiFi.h>

// ===================== RECEIVER MAC ADDRESS =====================
uint8_t RECEIVER_MAC[6] = {0x24, 0xEC, 0x4A, 0x10, 0x7D, 0x84};

// ===================== PIN SETUP =====================
#define MQ135_ANALOG_PIN   1
#define MQ7_ANALOG_PIN     2

#define DUST_ANALOG_PIN    3
#define DUST_LED_PIN       4

#define LED_PIN            6

// ===================== THRESHOLDS =====================
#define MQ135_THRESHOLD    800
#define MQ7_THRESHOLD      1000
#define DUST_THRESHOLD     2000

// ===================== TIMING =====================
#define SAMPLE_INTERVAL_MS  500

// ===================== DATA PACKET =====================
typedef struct SensorPacket {
  uint16_t mq135;
  uint16_t mq7;
  uint16_t dust;
  bool mq135Alert;
  bool mq7Alert;
  bool dustAlert;
  bool alert;
} SensorPacket;

SensorPacket packet;

// ===================== GLOBAL VARIABLES =====================
esp_now_peer_info_t peerInfo;
unsigned long lastSampleTime = 0;

// ===================== ESP-NOW SEND CALLBACK =====================
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  Serial.print("ESP-NOW send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

// ===================== READ SHARP DUST SENSOR =====================
uint16_t readDust() {
  digitalWrite(DUST_LED_PIN, LOW);      // Dust sensor LED on
  delayMicroseconds(280);

  uint16_t value = analogRead(DUST_ANALOG_PIN);

  delayMicroseconds(40);
  digitalWrite(DUST_LED_PIN, HIGH);     // Dust sensor LED off
  delayMicroseconds(9680);

  return value;
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("================================");
  Serial.println("ESP32-S3 SENSOR SENDER STARTING");
  Serial.println("================================");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(MQ135_ANALOG_PIN, INPUT);
  pinMode(MQ7_ANALOG_PIN, INPUT);
  pinMode(DUST_ANALOG_PIN, INPUT);

  pinMode(DUST_LED_PIN, OUTPUT);
  digitalWrite(DUST_LED_PIN, HIGH);   // Dust sensor LED off initially

  analogReadResolution(12);           // 0 to 4095

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.print("Sender MAC address: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW init failed!");

    while (true) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(500);
    }
  }

  Serial.println("ESP-NOW initialized.");

  esp_now_register_send_cb(onDataSent);

  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, RECEIVER_MAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("ERROR: Failed to add ESP-NOW peer!");

    while (true) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(250);
    }
  }

  Serial.println("ESP-NOW peer added.");
  Serial.println("Ready. Reading sensors...");
  Serial.println();
}

// ===================== MAIN LOOP =====================
void loop() {
  if (millis() - lastSampleTime >= SAMPLE_INTERVAL_MS) {
    lastSampleTime = millis();

    packet.mq135 = analogRead(MQ135_ANALOG_PIN);
    packet.mq7   = analogRead(MQ7_ANALOG_PIN);
    packet.dust  = readDust();

    packet.mq135Alert = packet.mq135 > MQ135_THRESHOLD;
    packet.mq7Alert   = packet.mq7 > MQ7_THRESHOLD;
    packet.dustAlert  = packet.dust > DUST_THRESHOLD;

    packet.alert = packet.mq135Alert || packet.mq7Alert || packet.dustAlert;

    // LED on GPIO 6 turns on if MQ135 OR MQ7 exceeds threshold
    digitalWrite(LED_PIN, (packet.mq135Alert || packet.mq7Alert) ? HIGH : LOW);

    Serial.printf(
      "MQ135: %4d | MQ7: %4d | Dust: %4d | MQ135 alert: %s | MQ7 alert: %s | LED: %s | Overall: %s\n",
      packet.mq135,
      packet.mq7,
      packet.dust,
      packet.mq135Alert ? "YES" : "NO",
      packet.mq7Alert ? "YES" : "NO",
      (packet.mq135Alert || packet.mq7Alert) ? "ON" : "OFF",
      packet.alert ? "ALERT" : "normal"
    );

    esp_err_t result = esp_now_send(RECEIVER_MAC, (uint8_t *)&packet, sizeof(packet));

    if (result == ESP_OK) {
      Serial.println("Packet queued for sending.");
    } else {
      Serial.print("Send error. Code: ");
      Serial.println(result);
    }

    Serial.println();
  }
}