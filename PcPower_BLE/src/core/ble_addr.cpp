#include "ble_addr.h"

#include <cstdio>
#include <cstring>

namespace core {

AddrKind classifyAddress(const uint8_t addr[6], uint8_t addr_type) {
  // Types 0 and 2 are public addresses; the top bits only carry meaning for random ones.
  if (addr_type == 0 || addr_type == 2) return AddrKind::PublicStatic;

  switch (addr[0] & 0xC0) {
    case 0xC0: return AddrKind::RandomStatic;
    case 0x40: return AddrKind::ResolvablePrivate;
    case 0x00: return AddrKind::NonResolvablePrivate;
    default:
      // 0x80 is reserved by the spec. Unknown means untrustworthy, so refuse it.
      return AddrKind::NonResolvablePrivate;
  }
}

bool isStable(AddrKind kind) {
  return kind == AddrKind::PublicStatic || kind == AddrKind::RandomStatic;
}

const char* addrKindName(AddrKind kind) {
  switch (kind) {
    case AddrKind::PublicStatic: return "public";
    case AddrKind::RandomStatic: return "random-static";
    case AddrKind::ResolvablePrivate: return "resolvable-private";
    case AddrKind::NonResolvablePrivate: return "non-resolvable-private";
  }
  return "unknown";
}

const char* addrKindReason(AddrKind kind) {
  switch (kind) {
    case AddrKind::PublicStatic:
    case AddrKind::RandomStatic:
      return "";
    case AddrKind::ResolvablePrivate:
      return "this device rotates its address every few minutes, so it would stop working "
             "within the hour";
    case AddrKind::NonResolvablePrivate:
      return "this device picks a new random address every time it powers on, so it can never "
             "be recognised again";
  }
  return "unrecognised address type";
}

static int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseAddress(const char* text, uint8_t out[6]) {
  if (!text) return false;
  uint8_t parsed[6];
  const char* p = text;
  for (int i = 0; i < 6; ++i) {
    const int hi = hexDigit(p[0]);
    if (hi < 0) return false;
    const int lo = hexDigit(p[1]);
    if (lo < 0) return false;
    parsed[i] = (uint8_t)((hi << 4) | lo);
    p += 2;
    if (i < 5) {
      if (*p != ':' && *p != '-') return false;
      ++p;
    }
  }
  if (*p != '\0') return false;  // trailing junk means the caller meant something else
  std::memcpy(out, parsed, 6);
  return true;
}

void formatAddress(const uint8_t addr[6], char* out, size_t len) {
  std::snprintf(out, len, "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1], addr[2], addr[3],
                addr[4], addr[5]);
}

bool addrEqual(const uint8_t a[6], const uint8_t b[6]) { return std::memcmp(a, b, 6) == 0; }

}  // namespace core
