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
