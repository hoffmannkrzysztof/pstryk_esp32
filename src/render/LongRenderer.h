#pragma once
#include "render/IRenderer.h"

class Arduino_DataBus;
class Arduino_GFX;
class Arduino_Canvas;

namespace pstryk {

class LongRenderer : public IRenderer {
 public:
  bool begin() override;
  int  width() override  { return 640; }
  int  height() override { return 180; }

  uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) override;
  void clear(uint16_t color) override;
  void fillRect(int x, int y, int w, int h, uint16_t color) override;
  void drawRect(int x, int y, int w, int h, uint16_t color) override;
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) override;
  void drawLine(int x0, int y0, int x1, int y1, uint16_t color) override;
  void text(int x, int y, const char* s, uint16_t color, int size) override;
  int  textWidth(const char* s, int size) override;
  int  textHeight(int size) override;
  void present() override;

  // Backlight duty 0..255 via LEDC PWM (was hard-wired digitalWrite HIGH).
  // Lets the app dim the 24/7 wall display at night; no-op if duty unchanged.
  void setBacklight(uint8_t duty);

 private:
  Arduino_DataBus* bus_ = nullptr;
  Arduino_GFX*     panel_ = nullptr;
  Arduino_Canvas*  canvas_ = nullptr;  // 640x180 framebuffer in PSRAM
  int              backlightDuty_ = -1;
};

}  // namespace pstryk
