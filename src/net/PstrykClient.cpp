#include "net/PstrykClient.h"
#include "core/PstrykParse.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

namespace pstryk {

static String urlEncodeIso(const char* iso) {
  // Only ':' needs encoding in our ISO8601 strings.
  String s;
  s.reserve(26);  // 20-char ISO + up to 3 colons expanded to %3A
  for (const char* p = iso; *p; ++p) {
    if (*p == ':') s += F("%3A");
    else           s += *p;      // char overload, no heap alloc
  }
  return s;
}

FetchResult PstrykClient::fetch(const char* startIso, const char* endIso, PriceData& out) {
  FetchResult r;
  if (!startIso || !endIso) {
    r.status = FetchStatus::NetworkError;
    return r;
  }
  String url =
      "https://api.pstryk.pl/integrations/meter-data/unified-metrics/"
      "?metrics=pricing&resolution=hour&window_start=" + urlEncodeIso(startIso) +
      "&window_end=" + urlEncodeIso(endIso);

  WiFiClientSecure client;
  client.setInsecure();  // v1: skip cert validation (personal device)

  HTTPClient https;
  https.setConnectTimeout(8000);
  https.setTimeout(8000);
  if (!https.begin(client, url)) {
    r.status = FetchStatus::NetworkError;
    return r;
  }
  https.addHeader("Authorization", apiKey_);  // raw key, no "Bearer"
  https.addHeader("Accept", "application/json");
  const char* collect[] = {"Retry-After"};
  https.collectHeaders(collect, 1);

  int code = https.GET();
  r.httpCode = code;

  if (code == 200) {
    String body = https.getString();
    if (body.isEmpty()) log_e("PstrykClient: empty body (OOM or zero-length response)");
    https.end();
    if (parsePricing(body.c_str(), out)) {
      r.status = FetchStatus::Ok;
    } else {
      r.status = FetchStatus::ParseError;
    }
    return r;
  }

  if (code == 429) {
    r.retryAfterSec = https.header("Retry-After").toInt();
    r.status = FetchStatus::RateLimited;
  } else if (code == 401 || code == 403) {
    r.status = FetchStatus::AuthError;
  } else {
    r.status = FetchStatus::NetworkError;
  }
  https.end();
  return r;
}

}  // namespace pstryk
