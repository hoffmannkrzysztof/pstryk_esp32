#pragma once
// Minimal but representative unified-metrics pricing response.
// Europe/Warsaw is CEST (UTC+2) on these June dates.
static const char* kPricingJson = R"JSON(
{
  "frames": [
    {"start":"2026-06-02T06:00:00Z","end":"2026-06-02T07:00:00Z","price_gross":0.30,"price_prosumer_gross":0.18,"is_cheap":true,"is_expensive":false},
    {"start":"2026-06-02T07:00:00Z","end":"2026-06-02T08:00:00Z","price_gross":0.52,"price_prosumer_gross":0.31,"is_live":true,"is_cheap":false,"is_expensive":false},
    {"start":"2026-06-02T08:00:00Z","end":"2026-06-02T09:00:00Z","price_gross":0.48,"price_prosumer_gross":0.29,"is_cheap":false,"is_expensive":false},
    {"start":"2026-06-02T09:00:00Z","end":"2026-06-02T10:00:00Z","price_gross":1.12,"price_prosumer_gross":0.40,"is_cheap":false,"is_expensive":true},
    {"start":"2026-06-03T06:00:00Z","end":"2026-06-03T07:00:00Z","price_gross":0.40,"price_prosumer_gross":0.20,"is_cheap":false,"is_expensive":false},
    {"start":"2026-06-03T07:00:00Z","end":"2026-06-03T08:00:00Z","price_gross":0.19,"price_prosumer_gross":0.10,"is_cheap":true,"is_expensive":false}
  ],
  "summary": {}
}
)JSON";

// Same response shape but with only today's frames (tomorrow not yet published).
static const char* kPricingTodayOnlyJson = R"JSON(
{"frames":[
  {"start":"2026-06-02T06:00:00Z","end":"2026-06-02T07:00:00Z","price_gross":0.30,"price_prosumer_gross":0.18,"is_cheap":true},
  {"start":"2026-06-02T07:00:00Z","end":"2026-06-02T08:00:00Z","price_gross":0.52,"price_prosumer_gross":0.31,"is_live":true}
],"summary":{}}
)JSON";
