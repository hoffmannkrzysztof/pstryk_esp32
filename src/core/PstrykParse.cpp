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
    f.start       = parseIso8601Utc(fr["start"] | "");
    f.buy         = fr["price_gross"]          | 0.0f;
    f.sell        = fr["price_prosumer_gross"] | 0.0f;
    f.isLive      = fr["is_live"]      | false;
    f.isCheap     = fr["is_cheap"]     | false;
    f.isExpensive = fr["is_expensive"] | false;
    out.frames.push_back(f);
  }
  return true;
}

}  // namespace pstryk
