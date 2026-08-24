#include "json_out.h"

#include <cstdarg>
#include <cstdio>

namespace core {

void jsonAppend(char* buf, size_t len, size_t* pos, const char* fmt, ...) {
  if (!buf || len == 0 || *pos >= len - 1) return;
  va_list ap;
  va_start(ap, fmt);
  const int written = std::vsnprintf(buf + *pos, len - *pos, fmt, ap);
  va_end(ap);
  if (written < 0) return;
  *pos += (size_t)written;
  if (*pos > len - 1) *pos = len - 1;  // truncated
}

void jsonAppendEscaped(char* buf, size_t len, size_t* pos, const char* text) {
  if (!text) return;
  for (const char* p = text; *p; ++p) {
    if (*p == '"' || *p == '\\') {
      jsonAppend(buf, len, pos, "\\%c", *p);
    } else if ((unsigned char)*p < 0x20) {
      jsonAppend(buf, len, pos, "\\u%04x", (unsigned)(unsigned char)*p);
    } else {
      jsonAppend(buf, len, pos, "%c", *p);
    }
  }
}

}  // namespace core
