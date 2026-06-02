#pragma once
#include "core/PriceData.h"

namespace pstryk {
// Parses a unified-metrics pricing response. Returns false on JSON error.
bool parsePricing(const char* json, PriceData& out);
}
