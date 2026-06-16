/*
  ESP32 RECEIVER - Ventilation Controller 
  Receives raw sensor values, controls fan speed directly
  Visual/audio alerts via WS2812B LEDs and piezo buzzer

  Pin assignments:
  Fan PWM (MOSFET) -> GPIO 12
  Buzzer           -> GPIO 8
  Tacho fan wire (for future speed feedback) -> GPIO 11
*/

#include <esp_now.h>
#include <WiFi.h>
//#include <Adafruit_NeoPixel.h>

// PINS setup
#define FAN_PWM_PIN    12
#define BUZZER_PIN     8
#define TACHO_PIN      11  // Fan speed feedback wire (Yellow/White)

// THRESHOLDS: fan starts accelerating
// These are RAW ADC values (0-4095). we can adjust it after testing!
// When sensors read ABOVE these values, we consider it "polluted"
#define MQ135_HIGH     800
#define MQ7_HIGH       1000
#define DUST_HIGH      2000

// Separate the buzzer channels tone() and ledcAttach()
// They clash while tryig to use the same ESP32's LEDC hardware
#define BUZZER_CHANNEL  1
#define BUZZER_FREQ     1000    // Initial frequency in Hz
#define BUZZER_RES      8       // Duty cycle resolution in bits

// DATA PACKET (must match sender!)
typedef struct SensorPacket {
  uint16_t mq135;
  uint16_t mq7;
  uint16_t dust;
  bool alert;
  bool mq135Alert;
  bool mq7Alert;
  bool dustAlert;
} SensorPacket;

SensorPacket packet;
bool gotData = false;
unsigned long lastDataTime = 0;

// SETUP the buzzer, fan and bluetooth connection
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=== VENTILATION CONTROLLER ===");

  // Buzzer init
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Fan PWM init — replace the two fan lines in setup() with:
  ledcAttach(FAN_PWM_PIN, 25000, 8);  // pin, 25kHz, 8-bit
  ledcWrite(FAN_PWM_PIN, 0);           // start off

  // Buzzer separate channel ???
  ledcAttach(BUZZER_PIN, BUZZER_FREQ, BUZZER_RES);
  ledcWrite(BUZZER_PIN, 0);  // off

  // For future fan speed feedback
  pinMode(TACHO_PIN, INPUT);  // Use Input instead of INPUT_PULLUP because we will add an external pull-up resistor

  // WiFi + ESP-NOW init
  WiFi.mode(WIFI_STA);
  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("^^^ USE THIS MAC IN THE SENDER CODE ^^^\n");

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW failed!");
    while (true) { delay(1000); }
  }

  esp_now_register_recv_cb(esp_now_recv_cb_t(onDataRecv));

  Serial.println("Ready! Waiting for sensor data...\n");
}

// Use for fan control
int gasDuty(uint16_t value, uint16_t high) {
  if (value < high) return 0;
  int duty = map(value, high, 4095, 150, 255);  // 4095 = max possible ADC reading
  if (duty > 255) duty = 255;
  if (duty < 40)  duty = 40;
  return duty;
}

// FAN CONTROL 
void setFanSpeed() {
  int duty135  = gasDuty(packet.mq135, MQ135_HIGH);
  int duty7    = gasDuty(packet.mq7,   MQ7_HIGH);
  int dutyDust = gasDuty(packet.dust,  DUST_HIGH);

  int duty = duty135;
  if (duty7 > duty)    duty = duty7;
  if (dutyDust > duty) duty = dutyDust;

  ledcWrite(FAN_PWM_PIN, duty);  // 0 = off, 40-255 = running

  Serial.printf("MQ135:%4d(%3d) MQ7:%4d(%3d) Dust:%4d(%3d) -> Fan:%3d/255\n",
                packet.mq135, duty135, packet.mq7, duty7, packet.dust, dutyDust, duty);
}

// BUZZER
void checkBuzzer() {
  static unsigned long lastBuzzTime = 0;
  const unsigned long BUZZ_INTERVAL = 3000;  // every 3 seconds
  const unsigned long BUZZ_DURATION = 200;   // for 0.2 seconds

  // Fan is running = air is polluted = buzz
  int duty = max({gasDuty(packet.mq135, MQ135_HIGH), 
                  gasDuty(packet.mq7, MQ7_HIGH), 
                  gasDuty(packet.dust, DUST_HIGH)});

  if (duty == 0) {
    ledcWrite(BUZZER_PIN, 0);
    lastBuzzTime = millis();  // reset so it chirps immediately next time
    return;
  }

  unsigned long elapsed = millis() - lastBuzzTime;

  if (elapsed < BUZZ_DURATION) {
    ledcWrite(BUZZER_PIN, 128);  // 50% duty = buzzer on
  } else if (elapsed >= BUZZ_INTERVAL) {
    lastBuzzTime = millis();
    ledcWrite(BUZZER_PIN, 128);  // 50% duty = buzzer on
  } else {
    ledcWrite(BUZZER_PIN, 0);  // silent gap between chirps
  }
}

// Check if the packets are being received
unsigned long packetCount = 0;

void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  packetCount++;

  Serial.printf("\n[ESP-NOW] Packet #%lu received from %02X:%02X:%02X:%02X:%02X:%02X (len=%d)\n",
                packetCount, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], len);

  if (len == sizeof(SensorPacket)) {
    memcpy(&packet, incomingData, sizeof(SensorPacket));
    gotData = true;
    lastDataTime = millis();
    Serial.println("[ESP-NOW] Packet size OK, data copied.");
  } else {
    Serial.printf("[ESP-NOW] WRONG SIZE! Expected %d bytes, got %d\n", sizeof(SensorPacket), len);
  }
}


//  MAIN LOOP
void loop() {
  // Heartbeat while waiting for first packet
  static unsigned long lastHeartbeat = 0;
  if (lastDataTime == 0 && millis() - lastHeartbeat > 2000) {
    Serial.println("Still waiting for first packet...");
    lastHeartbeat = millis();
  }

  if (gotData) {
    gotData = false;

    // Log which sender this came from (prep for multi-sender FR-12)
    // MAC is not stored yet — see onDataRecv note below
    Serial.printf("RECV | MQ135:%4d | MQ7:%4d | Dust:%4d | Alert:%s\n",
                  packet.mq135, packet.mq7, packet.dust,
                  packet.alert ? "YES" : "no");

    setFanSpeed();
  }

  checkBuzzer();  // runs continuously, independent of packet timing

  // After 10 seconds without data, run fan at safety speed
  if (millis() - lastDataTime > 10000 && lastDataTime > 0) {
    ledcWrite(FAN_PWM_PIN, 60);  // safety speed
    
    // Turn the buzzer on once-in-a-while to alert about lost communication
    if ((millis() / 1000) % 10 == 0) {
      ledcWrite(BUZZER_PIN, 500);  // Low tone every 10 seconds
    } else {
      ledcWrite(BUZZER_PIN, 0);
    }

    Serial.println("No data! Running fan at safety speed.");
    lastDataTime = millis();
  }
}