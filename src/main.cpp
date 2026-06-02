#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("pstryk boot");
}

void loop() {
  Serial.println("alive");
  delay(2000);
}
