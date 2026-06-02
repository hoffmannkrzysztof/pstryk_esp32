#pragma once

namespace pstryk {

// Deep-sleep orchestrator for the e-paper board. setup() runs one full
// wake->fetch->paint->sleep cycle and ends in deep sleep; loop() is never reached.
class SleepCycle {
 public:
  void setup();
  void loop() {}
};

}  // namespace pstryk
