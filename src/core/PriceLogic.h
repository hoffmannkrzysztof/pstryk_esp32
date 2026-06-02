#pragma once
#include "core/PriceData.h"
#include "view/PriceView.h"
#include <ctime>

namespace pstryk {
// Builds a display-ready PriceView from parsed data and the current UTC time.
PriceView buildView(const PriceData& data, time_t now);
}
