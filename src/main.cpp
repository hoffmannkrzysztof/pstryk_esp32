#include <Arduino.h>

#if defined(PSTRYK_BOARD_EPAPER)
#include "app/SleepCycle.h"
pstryk::SleepCycle app;
#else
#include "app/App.h"
pstryk::App app;
#endif

void setup() { app.setup(); }
void loop()  { app.loop(); }
