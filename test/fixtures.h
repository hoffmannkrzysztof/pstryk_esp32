#pragma once
// Representative unified-metrics pricing response, matching the REAL shape
// returned by api.pstryk.pl: price fields are nested under
// frames[].metrics.pricing.*, NOT flat on the frame. The first frame keeps the
// full real field set to prove the parser ignores the extras. Its price_gross
// (0.30), price_net (0.24), tge_price (0.20) and full_price (0.45) are kept
// DISTINCT on purpose so test_buy_is_gross (the cross-board canonical-field
// guard) can prove the parser picks price_gross and nothing else. The live API
// does not emit `is_live` at all -- the current frame is derived from the clock.
static const char* kPricingJson = R"JSON(
{
  "frames": [
    {"start":"2026-06-02T06:00:00Z","end":"2026-06-02T07:00:00Z","metrics":{"pricing":{"tge_price":0.20,"dist_price":0.0827,"service_price":0.08,"base_price":0.25,"vat_component":0.05,"excise_component":0.005,"full_price":0.45,"price_net":0.24,"price_gross":0.30,"price_prosumer_net":0.15,"price_prosumer_gross":0.18,"is_cheap":true,"is_expensive":false}}},
    {"start":"2026-06-02T07:00:00Z","end":"2026-06-02T08:00:00Z","metrics":{"pricing":{"price_gross":0.52,"price_prosumer_gross":0.31,"is_cheap":false,"is_expensive":false}}},
    {"start":"2026-06-02T08:00:00Z","end":"2026-06-02T09:00:00Z","metrics":{"pricing":{"price_gross":0.48,"price_prosumer_gross":0.29,"is_cheap":false,"is_expensive":false}}},
    {"start":"2026-06-02T09:00:00Z","end":"2026-06-02T10:00:00Z","metrics":{"pricing":{"price_gross":1.12,"price_prosumer_gross":0.40,"is_cheap":false,"is_expensive":true}}},
    {"start":"2026-06-03T06:00:00Z","end":"2026-06-03T07:00:00Z","metrics":{"pricing":{"price_gross":0.40,"price_prosumer_gross":0.20,"is_cheap":false,"is_expensive":false}}},
    {"start":"2026-06-03T07:00:00Z","end":"2026-06-03T08:00:00Z","metrics":{"pricing":{"price_gross":0.19,"price_prosumer_gross":0.10,"is_cheap":true,"is_expensive":false}}}
  ],
  "summary": {}
}
)JSON";

// Same response shape but with only today's frames (tomorrow not yet published).
static const char* kPricingTodayOnlyJson = R"JSON(
{"frames":[
  {"start":"2026-06-02T06:00:00Z","end":"2026-06-02T07:00:00Z","metrics":{"pricing":{"price_gross":0.30,"price_prosumer_gross":0.18,"is_cheap":true,"is_expensive":false}}},
  {"start":"2026-06-02T07:00:00Z","end":"2026-06-02T08:00:00Z","metrics":{"pricing":{"price_gross":0.52,"price_prosumer_gross":0.31,"is_cheap":false,"is_expensive":false}}}
],"summary":{}}
)JSON";
