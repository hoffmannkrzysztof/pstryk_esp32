#include "core/Format.h"
#include <cstdio>

namespace pstryk {

void formatPln(float value, char* out) {
  char tmp[8];
  std::snprintf(tmp, sizeof(tmp), "%.2f", value);
  for (int i = 0; tmp[i]; ++i) {
    if (tmp[i] == '.') tmp[i] = ',';
    out[i] = tmp[i];
    out[i + 1] = '\0';
  }
}

}  // namespace pstryk
