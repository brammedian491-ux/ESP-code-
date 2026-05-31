/*
  ESP32 RECEIVER - Ventilation Controller 
  Receives raw sensor values, controls fan speed directly
  Visual/audio alerts via WS2812B LEDs and piezo buzzer

  Pin assignments:
  Fan PWM (MOSFET) -> GPIO 18
  WS2812B data     -> GPIO 27
  Buzzer           -> GPIO 25
*/

#include <esp_now.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>

// ===================== STEP 1: PINS =====================
#define FAN_PWM_PIN    18
#define LED_PIN        27
#define BUZZER_PIN     25

// ===================== STEP 2: LED SETUP =====================
#define NUM_LEDS       8
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ===================== STEP 3: FAN PWM SETUP =====================
// PWM = Pulse Width Modulation = flickering the power on/off really fast
// to control fan speed. 25kHz is the standard frequency for PC fans.
#define PWM_CHANNEL    0
#define PWM_FREQ       25000
#define PWM_RES        8          // 8-bit = values 0 to 255

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
} SensorPacket;

SensorPacket packet;
bool gotData = false;
unsigned long lastDataTime = 0;

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=== VENTILATION CONTROLLER ===");

  // LED strip init
  strip.begin();
  strip.setBrightness(80);
  strip.fill(strip.Color(0, 0, 255));  // Blue = booting
  strip.show();

  // Buzzer init
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Fan PWM init
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(FAN_PWM_PIN, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);  // Start with fan OFF

  // WiFi + ESP-NOW init
  WiFi.mode(WIFI_STA);
  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("^^^ USE THIS MAC IN THE SENDER CODE ^^^\n");

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW failed!");
    strip.fill(strip.Color(255, 0, 255));  // Magenta = error
    strip.show();
    while (true) { delay(1000); }
  }

  esp_now_register_recv_cb(onDataRecv);

  // Ready!
  strip.fill(strip.Color(0, 255, 0));  // Green = ready
  strip.show();
  Serial.println("Ready! Waiting for sensor data...\n");
}

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

  // Don't let it go below 0 or above 255
  if (duty < 0) duty = 0;
  if (duty > 255) duty = 255;

  // Optional: minimum fan speed so it doesn't stall
  if (duty > 0 && duty < 40) duty = 40;

  ledcWrite(PWM_CHANNEL, duty);

  // Print for debugging
  Serial.printf("Highest:%4d -> Fan:%3d/255\n", highest, duty);
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

// ===================== MAIN LOOP =====================
void loop() {
  if (gotData) {
    gotData = false;

    // Print received values
    Serial.printf("RECV | MQ135:%4d | MQ7:%4d | Dust:%4d | Alert:%s\n",
                  packet.mq135, packet.mq7, packet.dust,
                  packet.alert ? "YES" : "no");

    // Update everything
    setFanSpeed();
    updateLEDs();
    checkBuzzer();
  }

  // Safety: if no data for 10 seconds, run fan at low speed
  if (millis() - lastDataTime > 10000 && lastDataTime > 0) {
    ledcWrite(PWM_CHANNEL, 60);  // Low speed for safety
    strip.fill(strip.Color(255, 0, 255));  // Magenta = no data
    strip.show();
    Serial.println("No data! Running fan at safety speed.");
    lastDataTime = millis();  // Reset so we don't spam
  }

  delay(100);
}
