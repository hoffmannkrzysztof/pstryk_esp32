#pragma once
#include <Arduino.h>
#include "core/PriceData.h"

namespace pstryk {

enum class FetchStatus { Ok, RateLimited, AuthError, NetworkError, ParseError };

struct FetchResult {
  FetchStatus status = FetchStatus::NetworkError;
  int httpCode = 0;
  int retryAfterSec = 0;   // populated on RateLimited if header present
};

class PstrykClient {
 public:
  explicit PstrykClient(const String& apiKey) : apiKey_(apiKey) {}
  // Fetches the window [start,end) (UTC ISO strings) and fills `out` on success.
  FetchResult fetch(const char* startIso, const char* endIso, PriceData& out);

 private:
  String apiKey_;
};

}  // namespace pstryk
