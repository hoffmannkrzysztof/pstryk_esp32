#pragma once
#include "render/IRenderer.h"
#include <cstdint>

namespace pstryk {

// IRenderer over the vendored LilyGo EPD47 driver. Draws into a 4bpp grayscale
// framebuffer in PSRAM; present() does a full panel refresh. Colors are grayscale
// luminance 0(black)..255(white). Text renders as dark glyphs (bundled FiraSans);
// the `size` argument is advisory (single bundled font), large numerics are drawn
// by EpdDashboard via fillRect, so text() here is used for labels/messages.
class EpdRenderer : public IRenderer {
 public:
  bool begin() override;
  int  width() override  { return 960; }
  int  height() override { return 540; }

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

 private:
  uint8_t* fb_ = nullptr;  // EPD_WIDTH*EPD_HEIGHT/2 bytes in PSRAM
};

}  // namespace pstryk
