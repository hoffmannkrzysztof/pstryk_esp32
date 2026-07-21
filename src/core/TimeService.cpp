#include "core/TimeService.h"
#include <cstdio>
#include <cstdlib>

namespace pstryk {

static const char* kTz = "CET-1CEST,M3.5.0,M10.5.0/3";

static long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

void timeServiceBegin() {
  setenv("TZ", kTz, 1);
  tzset();
}

time_t parseIso8601Utc(const char* s) {
  int Y, M, D, h, mi, se;
  if (!s || std::sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &mi, &se) != 6) return 0;
  long days = daysFromCivil(Y, M, D);
  return (time_t)days * 86400 + h * 3600 + mi * 60 + se;
}

void formatIso8601Utc(time_t t, char* out21) {
  struct tm g;
  gmtime_r(&t, &g);
  std::strftime(out21, 21, "%Y-%m-%dT%H:%M:%SZ", &g);
}

int localHour(time_t utc) {
  struct tm l;
  localtime_r(&utc, &l);
  return l.tm_hour;
}

int localDayOrdinal(time_t utc) {
  struct tm l;
  localtime_r(&utc, &l);
  return (int)daysFromCivil(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday);
}

time_t localMidnightUtc(time_t utc) {
  struct tm l;
  localtime_r(&utc, &l);
  l.tm_hour = 0; l.tm_min = 0; l.tm_sec = 0; l.tm_isdst = -1;
  return mktime(&l);
}

bool dstChangesWithin(time_t utc, long marginSec) {
  struct tm a, b;
  time_t before = utc - marginSec, after = utc + marginSec;
  localtime_r(&before, &a);
  localtime_r(&after, &b);
  return a.tm_isdst != b.tm_isdst;
}

}  // namespace pstryk
