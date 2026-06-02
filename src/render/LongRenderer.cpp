#include "render/LongRenderer.h"
#include "render/pins_config.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <cstring>

// Adaptation notes (Arduino_GFX 1.6.5 vs plan 1.3.7 intent):
//
// 1. Class name: Arduino_AXS15231B (plan used Arduino_AXS15231 — no trailing B).
//    The 1.6.5 library only ships Arduino_AXS15231B.
//
// 2. Arduino_AXS15231B constructor has more parameters than the plan assumed:
//      (bus, rst, rotation, ips, w, h, col_offset1, row_offset1,
//       col_offset2, row_offset2, init_operations, init_operations_len)
//    Default w/h in the library are 360x640 (not 180x640), so we must pass
//    PANEL_NATIVE_W=180 and PANEL_NATIVE_H=640 explicitly.
//    Offsets default to 0, init_operations defaults to the 180x640 sequence.
//
// 3. Arduino_ESP32QSPI constructor:
//      (cs, sck, mosi, miso, quadwp, quadhd, is_shared_interface=false)
//    The QSPI data pins map as: D0->mosi, D1->miso, D2->quadwp, D3->quadhd.
//
// 4. panel_->begin(speed) takes a single int32_t; no separate frequency arg
//    beyond what was intended. Capped at 32 MHz per hardware constraint.
//
// 5. GFX_SKIP_OUTPUT_BEGIN is defined as -2 in Arduino_DataBus.h — exists and
//    works as intended for the canvas begin() call.
//
// 6. canvas_->flush() exists with signature flush(bool force_flush = false).
//
// 7. canvas_->setUTF8Print() is guarded by #if defined(U8G2_FONT_SUPPORT) inside
//    Arduino_GFX.h. That symbol is not defined in this build, so the call was
//    removed. The built-in font does not require UTF-8 handling.

namespace pstryk {

bool LongRenderer::begin() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);  // backlight on

  // D0=mosi=13, D1=miso=18, D2=quadwp=21, D3=quadhd=14
  bus_ = new Arduino_ESP32QSPI(
      TFT_QSPI_CS,   // cs  = 12
      TFT_QSPI_SCK,  // sck = 17
      TFT_QSPI_D0,   // mosi/D0 = 13
      TFT_QSPI_D1,   // miso/D1 = 18
      TFT_QSPI_D2,   // quadwp/D2 = 21
      TFT_QSPI_D3    // quadhd/D3 = 14
  );

  // rotation=1 => landscape 640x180; ips=false per LilyGo example.
  // Explicit w=180, h=640 required — library default is 360x640.
  panel_ = new Arduino_AXS15231B(
      bus_,
      TFT_QSPI_RST,    // rst = 16
      /*rotation=*/1,
      /*ips=*/false,
      /*w=*/PANEL_NATIVE_W,   // 180
      /*h=*/PANEL_NATIVE_H    // 640
  );

  // Off-screen canvas (uses PSRAM) -> flush to panel; avoids flicker.
  canvas_ = new Arduino_Canvas(SCREEN_W, SCREEN_H, panel_);

  // GFX_SKIP_OUTPUT_BEGIN (-2): initialise canvas memory without calling
  // panel_->begin() inside canvas_->begin(), so we can call it separately
  // with the desired QSPI clock.
  if (!canvas_->begin(GFX_SKIP_OUTPUT_BEGIN)) return false;

  // Initialise the panel at <=32 MHz (QSPI ceiling for this board).
  if (!panel_->begin(32000000)) return false;

  // setUTF8Print() is only available when U8G2_FONT_SUPPORT is enabled.
  // We use the built-in font so this call is not needed.
  return true;
}

uint16_t LongRenderer::rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);  // RGB565
}

void LongRenderer::clear(uint16_t c) { canvas_->fillScreen(c); }

void LongRenderer::fillRect(int x, int y, int w, int h, uint16_t c) {
  canvas_->fillRect(x, y, w, h, c);
}

void LongRenderer::drawRect(int x, int y, int w, int h, uint16_t c) {
  canvas_->drawRect(x, y, w, h, c);
}

void LongRenderer::fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
  canvas_->fillRoundRect(x, y, w, h, r, c);
}

void LongRenderer::drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
  canvas_->drawLine(x0, y0, x1, y1, c);
}

void LongRenderer::text(int x, int y, const char* s, uint16_t color, int size) {
  canvas_->setTextColor(color);
  canvas_->setTextSize(size);
  canvas_->setCursor(x, y);
  canvas_->print(s);
}

int LongRenderer::textWidth(const char* s, int size) {
  return (int)std::strlen(s) * 6 * size;
}

int LongRenderer::textHeight(int size) { return 8 * size; }

void LongRenderer::present() { canvas_->flush(); }

}  // namespace pstryk
