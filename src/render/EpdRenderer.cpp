#include "render/EpdRenderer.h"
#include <Arduino.h>
#include <cstring>
extern "C" {
#include "epd_driver.h"
#include "ui_font_body.h"   // const GFXfont UiSansBody  (~30 px line, Fira Sans)
#include "ui_font_head.h"   // const GFXfont UiSansHead  (~45 px line, Fira Sans)
}

// Adaptation notes vs the task's assumed LilyGo API. The actual vendored
// epd_driver.h (esp32s3 branch) declares:
//   void write_string(const GFXfont*, const char*, int32_t*, int32_t*, uint8_t*);
//   void get_text_bounds(const GFXfont*, const char*, int32_t*, int32_t*,
//                        int32_t*, int32_t*, int32_t*, int32_t*,
//                        const FontProperties*);
// So cursor / bounds args are int32_t* (not int*), strings are const char*
// (no cast needed), and get_text_bounds takes a trailing FontProperties* which
// we pass as nullptr (driver uses defaults). GFXfont uses .advance_y (matches).

namespace pstryk {

static const int FB_BYTES = EPD_WIDTH * EPD_HEIGHT / 2;

bool EpdRenderer::begin() {
  epd_init();
  fb_ = (uint8_t*)ps_calloc(sizeof(uint8_t), FB_BYTES);
  if (!fb_) return false;
  std::memset(fb_, 0xFF, FB_BYTES);  // white
  return true;
}

uint16_t EpdRenderer::rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)((r * 77 + g * 150 + b * 29) >> 8);  // luminance 0..255
}

void EpdRenderer::clear(uint16_t color) {
  uint8_t g4 = (uint8_t)((color & 0xFF) >> 4);
  std::memset(fb_, (g4 << 4) | g4, FB_BYTES);
}

void EpdRenderer::fillRect(int x, int y, int w, int h, uint16_t c) {
  epd_fill_rect(x, y, w, h, (uint8_t)(c & 0xFF), fb_);
}
void EpdRenderer::drawRect(int x, int y, int w, int h, uint16_t c) {
  epd_draw_rect(x, y, w, h, (uint8_t)(c & 0xFF), fb_);
}
void EpdRenderer::fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
  uint8_t col = (uint8_t)(c & 0xFF);
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  epd_fill_rect(x + r, y, w - 2 * r, h, col, fb_);
  epd_fill_rect(x, y + r, w, h - 2 * r, col, fb_);
  epd_fill_circle(x + r, y + r, r, col, fb_);
  epd_fill_circle(x + w - r - 1, y + r, r, col, fb_);
  epd_fill_circle(x + r, y + h - r - 1, r, col, fb_);
  epd_fill_circle(x + w - r - 1, y + h - r - 1, r, col, fb_);
}
void EpdRenderer::drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
  epd_draw_line(x0, y0, x1, y1, (uint8_t)(c & 0xFF), fb_);
}

// size>=2 selects the larger heading font; otherwise the body font.
static const GFXfont* fontForSize(int size) {
  return size >= 2 ? &UiSansHead : &UiSansBody;
}

void EpdRenderer::text(int x, int y, const char* s, uint16_t color, int size) {
  const GFXfont* f = fontForSize(size);
  // Top-left anchor (per IRenderer contract): the driver places the cursor at the
  // baseline, so drop it by the font ascender to put the glyph top near y.
  int32_t cx = x, cy = y + f->ascender;
  // Map our 8-bit luminance (0=black..255=white, from EpdRenderer::rgb) to the
  // driver's 4-bit grayscale foreground. White background, no DRAW_BACKGROUND so
  // text composites onto the existing framebuffer without an opaque box.
  FontProperties props = {};
  props.fg_color = (uint8_t)((color & 0xFF) >> 4);
  props.bg_color = 15;
  props.fallback_glyph = 0;
  props.flags = 0;
  write_mode(f, s, &cx, &cy, fb_, BLACK_ON_WHITE, &props);
}

int EpdRenderer::textWidth(const char* s, int size) {
  const GFXfont* f = fontForSize(size);
  int32_t x = 0, y = 0, x1 = 0, y1 = 0, w = 0, h = 0;
  get_text_bounds(f, s, &x, &y, &x1, &y1, &w, &h, nullptr);
  return (int)w;
}
int EpdRenderer::textHeight(int size) {
  return fontForSize(size)->advance_y;
}

void EpdRenderer::present() {
  Rect_t area = epd_full_screen();
  epd_poweron();
  if (deepClean_) {
    epd_clear();                          // vendor deghost: 4 black/white cycles
    deepClean_ = false;
  } else {
    // Quick clear: 2 cycles suffice between hourly refreshes; ghosting is reset
    // by the daily deep clean. Halves the panel passes (and poweron window) of
    // every ordinary refresh.
    epd_clear_area_cycles(area, 2, 50);
  }
  epd_draw_grayscale_image(area, fb_);
  epd_poweroff();
}

}  // namespace pstryk
