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

// ===================== STEP 1: PINS =====================
#define FAN_PWM_PIN    12
#define BUZZER_PIN     8
#define TACHO_PIN      11  // Fan speed feedback wire (Yellow/White)

// ===================== STEP 4: THRESHOLDS =====================
// These are RAW ADC values (0-4095). we can adjust it after testing!
// When sensors read ABOVE these values, we consider it "polluted"
#define MQ135_HIGH     2500
#define MQ7_HIGH       2000
#define DUST_HIGH      1000

// ===================== DATA PACKET (must match sender!) =====================
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

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=== VENTILATION CONTROLLER ===");

  // Buzzer init
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Fan PWM init
  pinMode(FAN_PWM_PIN, OUTPUT);

  // For future fan speed feedback
  pinMode(TACHO_PIN, INPUT);  // Use Input instead of INPUT_PULLUP because we will add an external pull-up resistor
  attachInterrupt(digitalPinToInterrupt(TACHO_PIN), onTachoPulse, FALLING);

  // WiFi + ESP-NOW init
  WiFi.mode(WIFI_STA);
  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("^^^ USE THIS MAC IN THE SENDER CODE ^^^\n");

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW failed!");
    //strip.fill(strip.Color(255, 0, 255));  // Magenta = error
    //strip.show();
    while (true) { delay(1000); }
  }

  esp_now_register_recv_cb(esp_now_recv_cb_t(onDataRecv));

  // Ready!
  /*strip.fill(strip.Color(0, 255, 0));  // Green = ready
  strip.show();*/
  Serial.println("Ready! Waiting for sensor data...\n");
}

// ===================== ESP-NOW CALLBACK =====================
// Called automatically when data arrives from sender
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len == sizeof(SensorPacket)) {
    memcpy(&packet, incomingData, sizeof(SensorPacket));
    gotData = true;
    lastDataTime = millis();
  }
}

// ===================== FAN CONTROL =====================
// Simple logic: the HIGHEST sensor reading controls fan speed
// We map that single highest value directly to PWM duty (0-255)
void setFanSpeed() {
  // Find the highest reading among all three sensors
  uint16_t highest = packet.mq135;
  if (packet.mq7 > highest) highest = packet.mq7;
  if (packet.dust > highest) highest = packet.dust;

  // Map highest reading directly to fan PWM
  // Clean air (~500) = fan off (0)
  // Very polluted (~3500) = fan max (255)
  int duty = map(highest, 500, 3500, 0, 255);

  /*// Don't let it go below 0 or above 255
  if (duty < 0) duty = 0;
  if (duty > 255) duty = 255;*/

  // Optional: minimum fan speed so it doesn't stall
  if (duty > 0 && duty < 40) duty = 40;

  analogWrite(FAN_PWM_PIN, duty);

  // Print for debugging
  Serial.printf("Highest:%4d -> Fan:%3d/255\n", highest, duty);
}



// ===================== TACHO / RPM =====================
// This is for future feedback control. It counts pulses from the fan's tacho wire to calculate RPM.
// Note: Sunon 60x60 fan gives 2 pulses per revolution, so we need to account for that in the calculation.
// We use an interrupt to count pulses accurately without blocking the main loop.
volatile unsigned long pulseCount = 0;

void IRAM_ATTR onTachoPulse() {
  pulseCount++;
}

unsigned int getFanRPM() {
  static unsigned long lastCheck = 0;
  static unsigned long lastPulseCount = 0;

  unsigned long now = millis();
  unsigned long elapsed = now - lastCheck;

  if (elapsed >= 1000) {  // Calculate once per second
    unsigned long pulses = pulseCount - lastPulseCount;
    lastPulseCount = pulseCount;
    lastCheck = now;

    // Sunon 60x60 = 2 pulses per revolution
    return (pulses * 60) / 2;
  }
  return 0;  // Not ready yet
}

// BUZZER
void checkBuzzer() {
  if (packet.alert) {
    tone(BUZZER_PIN, 1000);   // 1kHz tone while alert is active
  } else {
    noTone(BUZZER_PIN);
  }
}

//  MAIN LOOP
void loop() {
  if (gotData) {
    gotData = false;

    // Log which sender this came from (prep for multi-sender FR-12)
    // MAC is not stored yet — see onDataRecv note below
    Serial.printf("RECV | MQ135:%4d | MQ7:%4d | Dust:%4d | Alert:%s\n",
                  packet.mq135, packet.mq7, packet.dust,
                  packet.alert ? "YES" : "no");

    setFanSpeed();
    checkBuzzer();
  }

  // After 10 seconds without data, run fan at safety speed
  if (millis() - lastDataTime > 10000 && lastDataTime > 0) {
    analogWrite(FAN_PWM_PIN, 60);
    
    // Turn the buzzer on once-in-a-while to alert about lost communication
    if ((millis() / 1000) % 10 == 0) {
      tone(BUZZER_PIN, 500);  // Low tone every 10 seconds
    } else {
      noTone(BUZZER_PIN);
    }

    Serial.println("No data! Running fan at safety speed.");
    lastDataTime = millis();
  }

  // unsigned int rpm = getFanRPM(); 
  // if (rpm > 0) {
  //   Serial.printf("Fan RPM: %d\n", rpm);
  // }
}
