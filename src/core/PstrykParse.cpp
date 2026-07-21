#include "core/PstrykParse.h"
#include "core/TimeService.h"
#include <ArduinoJson.h>

namespace pstryk {

bool parsePricing(const char* json, PriceData& out) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return false;
  JsonArray frames = doc["frames"].as<JsonArray>();
  if (frames.isNull()) return false;

  out.frames.clear();
  for (JsonObject fr : frames) {
    PriceFrame f;
    f.start = parseIso8601Utc(fr["start"] | "");
    // Price fields are nested under frames[].metrics.pricing.* (not flat on the
    // frame). A frame with a null/absent price_gross (placeholder tomorrow stub,
    // or a re-shaped pricing object) is skipped, NOT parsed as 0.00 zl: rendering
    // confident zeros is exactly the historical all-zeros regression, and a
    // null-priced stub must not fake hasTomorrow.
    JsonObjectConst p = fr["metrics"]["pricing"];
    JsonVariantConst pg = p["price_gross"];
    if (pg.isNull()) continue;
    f.buy         = pg.as<float>();
    f.sell        = p["price_prosumer_gross"] | 0.0f;
    f.isCheap     = p["is_cheap"]     | false;
    f.isExpensive = p["is_expensive"] | false;
    // The API does not emit `is_live`; the current frame is derived from the
    // clock in PriceLogic::buildView, so isLive stays false here.
    out.frames.push_back(f);
  }
  // Zero usable frames (empty array or every frame degenerate) is a parse
  // failure: both boards classify it as retryable and keep the last-good view.
  return !out.frames.empty();
}

}  // namespace pstryk
