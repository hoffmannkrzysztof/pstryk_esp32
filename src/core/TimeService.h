#pragma once
#include <ctime>

namespace pstryk {

// Installs the Europe/Warsaw TZ rule for localtime_r/mktime. Call once at startup
// (host tests call it in setUp; device calls it after NTP config).
void timeServiceBegin();

time_t parseIso8601Utc(const char* s);     // "YYYY-MM-DDTHH:MM:SSZ" -> UTC epoch (0 on error)
void   formatIso8601Utc(time_t t, char* out21);  // -> "YYYY-MM-DDTHH:MM:SSZ" (needs >=21 bytes)

int    localHour(time_t utc);              // 0..23 in Europe/Warsaw
int    localDayOrdinal(time_t utc);        // days since 1970-01-01 of the LOCAL date
time_t localMidnightUtc(time_t utc);       // 00:00 local of utc's day, as UTC epoch

}  // namespace pstryk
