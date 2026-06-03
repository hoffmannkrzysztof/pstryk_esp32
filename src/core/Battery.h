#pragma once

namespace pstryk {

// Pin millivolts (already-calibrated ADC reading at GPIO14) -> battery volts.
// The board has a 2:1 divider, so battery = pin * 2.
float batteryVoltsFromPinMv(float pinMv);

// Li-ion cell volts -> 0..100 %, linear between 3.30 V (0%) and 4.20 V (100%).
int batteryPercent(float volts);

}  // namespace pstryk
