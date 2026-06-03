#include "core/Battery.h"

namespace pstryk {

float batteryVoltsFromPinMv(float pinMv) { return pinMv * 2.0f / 1000.0f; }

int batteryPercent(float volts) {
  const float lo = 3.30f, hi = 4.20f;
  float pct = (volts - lo) / (hi - lo) * 100.0f;
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  return (int)(pct + 0.5f);
}

}  // namespace pstryk
