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
    // frame). A missing object yields a null JsonObject, so the | defaults hold.
    JsonObjectConst p = fr["metrics"]["pricing"];
    f.buy         = p["price_gross"]          | 0.0f;
    f.sell        = p["price_prosumer_gross"] | 0.0f;
    f.isCheap     = p["is_cheap"]     | false;
    f.isExpensive = p["is_expensive"] | false;
    // The API does not emit `is_live`; the current frame is derived from the
    // clock in PriceLogic::buildView, so isLive stays false here.
    out.frames.push_back(f);
  }
  return true;
}

}  // namespace pstryk
