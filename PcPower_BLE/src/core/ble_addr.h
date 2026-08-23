// Classifies BLE addresses so only ones that stay put can be learned.
//
// A controller that advertises a rotating address would work for a few minutes and then
// silently stop waking the PC, which is far worse than refusing it up front with a reason.
#pragma once
#include <cstddef>
#include <cstdint>

namespace core {

enum class AddrKind : uint8_t {
  PublicStatic,          // IEEE-assigned, never changes
  RandomStatic,          // random but fixed until the device reboots
  ResolvablePrivate,     // RPA: rotates every few minutes
  NonResolvablePrivate,  // NRPA: new address on every power-up
};

// addr[0] is the most significant byte, i.e. display order AA:BB:CC:DD:EE:FF.
// addr_type follows NimBLE: 0 public, 1 random, 2 public identity, 3 random identity.
AddrKind classifyAddress(const uint8_t addr[6], uint8_t addr_type);

bool isStable(AddrKind kind);
const char* addrKindName(AddrKind kind);
const char* addrKindReason(AddrKind kind);  // "" when the address is stable

bool parseAddress(const char* text, uint8_t out[6]);  // "AA:BB:CC:DD:EE:FF" or with dashes
void formatAddress(const uint8_t addr[6], char* out, size_t len);  // wants >= 18 bytes
bool addrEqual(const uint8_t a[6], const uint8_t b[6]);

}  // namespace core
