#include "WiFi.h"

#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(2000);

  WiFi.mode(WIFI_STA);
  WiFi.begin();      // force WiFi initialization
  delay(1000);

  Serial.println(WiFi.macAddress());
}

void loop() {}



// SENDER MAC: 24:EC:4A:10:7B:04  RECEIVER MAC: 24:EC:4A:10:7D:84
