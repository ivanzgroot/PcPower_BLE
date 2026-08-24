// Bounded JSON appending. Everything the firmware emits is built with these two helpers so a
// short buffer truncates cleanly instead of running off the end - there is no JSON library on
// the device, and no parser either: requests arrive as form-encoded key/value pairs.
#pragma once
#include <cstddef>

namespace core {

void jsonAppend(char* buf, size_t len, size_t* pos, const char* fmt, ...);
void jsonAppendEscaped(char* buf, size_t len, size_t* pos, const char* text);

}  // namespace core
