#pragma once
namespace pstryk {
// Writes a Polish-formatted price ("0,52") into out (>=8 bytes).
void formatPln(float value, char* out);
}
