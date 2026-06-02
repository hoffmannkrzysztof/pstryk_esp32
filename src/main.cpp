#include <Arduino.h>
#include "render/LongRenderer.h"

pstryk::LongRenderer gfx;

void setup() {
  Serial.begin(115200);
  delay(300);
  if (!gfx.begin()) { Serial.println("renderer begin FAILED"); return; }
  uint16_t bg = gfx.rgb(0x0a, 0x0e, 0x14);
  uint16_t fg = gfx.rgb(0xe8, 0xed, 0xf4);
  uint16_t green = gfx.rgb(0x34, 0xd3, 0x99);
  gfx.clear(bg);
  gfx.text(20, 20, "PSTRYK", fg, 3);
  gfx.text(20, 70, "0,52 zl/kWh", green, 5);
  gfx.fillRoundRect(20, 140, 600, 20, 6, green);
  gfx.present();
}

void loop() { delay(1000); }
