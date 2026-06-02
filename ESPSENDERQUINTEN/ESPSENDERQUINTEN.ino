CODE FIRST ESP -> the sender in the lamp!

/*
  ESP32 SENDER - Lamp Unit with Sensors
  Reads MQ-135, MQ-7, and GP2Y1010AU0F dust sensor
  Sends RAW values via ESP-NOW to ventilation controller

  Pin assignments can be changed:
  MQ-135 analog  -> GPIO 34
  MQ-135 digital -> GPIO 32 (optional, for potentiometer threshold)
  MQ-7 analog    -> GPIO 35
  MQ-7 digital   -> GPIO 33 (optional)
  Dust LED ctrl  -> GPIO 26
  Dust analog    -> GPIO 36
*/

#include <esp_now.h>
#include <WiFi.h>

// ===================== STEP 1: SET RECEIVER MAC =====================
// Run the receiver sketch first, check Serial Monitor for its MAC
// Then replace the line below with that MAC address
uint8_t RECEIVER_MAC[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

// ===================== STEP 2: PINS =====================
#define MQ135_ANALOG_PIN   34
#define MQ135_DIGITAL_PIN  32
#define MQ7_ANALOG_PIN     35
#define MQ7_DIGITAL_PIN    33
#define DUST_LED_PIN       26
#define DUST_ANALOG_PIN    36

// ===================== STEP 3: TIMING =====================
#define SAMPLE_INTERVAL_MS   2000   // Read sensors every 2 seconds
#define ALERT_REPEAT_MS      5000   // If alert, resend every 5 seconds

// ===================== DATA PACKET (what we send) =====================
// This struct must be IDENTICAL on receiver!
typedef struct SensorPacket {
  uint16_t mq135;      // Raw ADC value 0-4095
  uint16_t mq7;        // Raw ADC value 0-4095
  uint16_t dust;       // Raw ADC value 0-4095
  bool alert;          // True if any sensor is above threshold
} SensorPacket;

SensorPacket packet;

// ESP-NOW stuff
esp_now_peer_info_t peerInfo;
unsigned long lastSampleTime = 0;
unsigned long lastAlertTime  = 0;
bool wasAlert = false;  // Remember if we were in alert last time

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=== SENSOR SENDER ===");

  // Set pin modes
  pinMode(MQ135_ANALOG_PIN, INPUT);
  pinMode(MQ7_ANALOG_PIN, INPUT);
  pinMode(MQ135_DIGITAL_PIN, INPUT);
  pinMode(MQ7_DIGITAL_PIN, INPUT);
  pinMode(DUST_LED_PIN, OUTPUT);
  pinMode(DUST_ANALOG_PIN, INPUT);
  digitalWrite(DUST_LED_PIN, LOW);

  // Start WiFi in station mode (needed for ESP-NOW)
  WiFi.mode(WIFI_STA);
  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());

  // Start ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW failed!");
    while (true) { delay(1000); }
  }

  // Register callback (tells us if send worked or not)
  esp_now_register_send_cb(onDataSent);

  // Add the receiver as a "peer" (who we send to)
  memcpy(peerInfo.peer_addr, RECEIVER_MAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer!");
    while (true) { delay(1000); }
  }

  Serial.println("Ready! Reading sensors...\n");
}

// Called after every send, tells us success or fail
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Send: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

// ===================== READ DUST SENSOR =====================
// The Sharp sensor needs a specific LED pulse to give accurate readings
uint16_t readDust() {
  digitalWrite(DUST_LED_PIN, LOW);     // Turn LED on
  delayMicroseconds(280);              // Wait 280 microseconds
  uint16_t value = analogRead(DUST_ANALOG_PIN);  // Read the value
  delayMicroseconds(40);               // Wait a bit more
  digitalWrite(DUST_LED_PIN, HIGH);    // Turn LED off
  delayMicroseconds(9680);             // Wait rest of 10ms cycle
  return value;
}

// ===================== MAIN LOOP =====================
void loop() {
  unsigned long now = millis();

  // Only read sensors every SAMPLE_INTERVAL_MS (2 seconds)
  if (now - lastSampleTime >= SAMPLE_INTERVAL_MS) {
    lastSampleTime = now;

    // Read all three sensors (raw values, 0-4095)
    packet.mq135 = analogRead(MQ135_ANALOG_PIN);
    packet.mq7   = analogRead(MQ7_ANALOG_PIN);
    packet.dust  = readDust();

    // Check digital pins (if modules have the potentiometer set)
    // LOW usually means "threshold exceeded" on most MQ modules
    bool mq135_digital = (digitalRead(MQ135_DIGITAL_PIN) == LOW);
    bool mq7_digital   = (digitalRead(MQ7_DIGITAL_PIN) == LOW);

    // Alert if any digital pin says so
    packet.alert = mq135_digital || mq7_digital;

    // Print what we read (for debugging)
    Serial.printf("MQ135:%4d | MQ7:%4d | Dust:%4d | Alert:%s\n",
                  packet.mq135, packet.mq7, packet.dust,
                  packet.alert ? "YES" : "no");

    // Decide whether to send
    bool shouldSend = false;

    if (packet.alert) {
      // In alert mode: send immediately, then repeat every ALERT_REPEAT_MS
      if (now - lastAlertTime >= ALERT_REPEAT_MS) {
        shouldSend = true;
        lastAlertTime = now;
      }
    } else {
      // Not in alert: send once when transitioning from alert to normal
      if (wasAlert) {
        shouldSend = true;
        lastAlertTime = 0;
      }
    }

    // Actually send the packet
    if (shouldSend) {
      esp_err_t result = esp_now_send(RECEIVER_MAC, (uint8_t *)&packet, sizeof(packet));
      Serial.println(result == ESP_OK ? "Sending..." : "Send error!");
    }

    wasAlert = packet.alert;  // Remember for next loop
  }
}



// ===================== LED CONTROL =====================
void updateLEDs() {
  uint32_t color;

  // Determine color based on highest sensor reading
  uint16_t highest = max(packet.mq135, max(packet.mq7, packet.dust));

  if (highest < 1000) {
    color = strip.Color(0, 255, 0);      // Green = clean
  } else if (highest < 2000) {
    color = strip.Color(255, 255, 0);    // Yellow = moderate
  } else {
    color = strip.Color(255, 0, 0);      // Red = bad
  }

  strip.fill(color);

}

