#pragma once
#include <cstdint>

namespace pstryk {

class IRenderer {
 public:
  virtual ~IRenderer() = default;
  virtual bool begin() = 0;
  virtual int  width() = 0;
  virtual int  height() = 0;

  virtual uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) = 0;

  virtual void clear(uint16_t color) = 0;
  virtual void fillRect(int x, int y, int w, int h, uint16_t color) = 0;
  virtual void drawRect(int x, int y, int w, int h, uint16_t color) = 0;
  virtual void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) = 0;
  virtual void drawLine(int x0, int y0, int x1, int y1, uint16_t color) = 0;

  // Text anchored at top-left (x,y); `size` scales the built-in 6x8 font.
  virtual void text(int x, int y, const char* s, uint16_t color, int size) = 0;
  virtual int  textWidth(const char* s, int size) = 0;
  virtual int  textHeight(int size) = 0;

  virtual void present() = 0;  // push canvas to the panel
};

}  // namespace pstryk
