#include "test_main.h"
#include "core/ble_addr.h"

static const uint8_t kPublic[6] = {0x00, 0x1A, 0x7D, 0xDA, 0x71, 0x13};
static const uint8_t kStatic[6] = {0xC5, 0x1A, 0x7D, 0xDA, 0x71, 0x13};  // top bits 11
static const uint8_t kRpa[6]    = {0x45, 0x1A, 0x7D, 0xDA, 0x71, 0x13};  // top bits 01
static const uint8_t kNrpa[6]   = {0x05, 0x1A, 0x7D, 0xDA, 0x71, 0x13};  // top bits 00

TEST(addr_public_type_is_public_static) {
  CHECK(core::classifyAddress(kPublic, 0) == core::AddrKind::PublicStatic);
  CHECK(core::classifyAddress(kRpa, 0) == core::AddrKind::PublicStatic);  // the type wins
  CHECK(core::classifyAddress(kPublic, 2) == core::AddrKind::PublicStatic);
}

TEST(addr_random_static_is_learnable) {
  CHECK(core::classifyAddress(kStatic, 1) == core::AddrKind::RandomStatic);
  CHECK(core::isStable(core::AddrKind::RandomStatic));
  CHECK_STREQ(core::addrKindReason(core::AddrKind::RandomStatic), "");
}

TEST(addr_rpa_and_nrpa_are_refused) {
  CHECK(core::classifyAddress(kRpa, 1) == core::AddrKind::ResolvablePrivate);
  CHECK(core::classifyAddress(kNrpa, 1) == core::AddrKind::NonResolvablePrivate);
  CHECK(!core::isStable(core::AddrKind::ResolvablePrivate));
  CHECK(!core::isStable(core::AddrKind::NonResolvablePrivate));
  CHECK(std::strlen(core::addrKindReason(core::AddrKind::ResolvablePrivate)) > 0);
  CHECK(std::strlen(core::addrKindReason(core::AddrKind::NonResolvablePrivate)) > 0);
}

TEST(addr_boundary_bits) {
  uint8_t a[6] = {0x3F, 0, 0, 0, 0, 1};  // 00 111111 -> NRPA
  CHECK(core::classifyAddress(a, 1) == core::AddrKind::NonResolvablePrivate);
  a[0] = 0x40;                           // 01 000000 -> RPA
  CHECK(core::classifyAddress(a, 1) == core::AddrKind::ResolvablePrivate);
  a[0] = 0x7F;                           // 01 111111 -> RPA
  CHECK(core::classifyAddress(a, 1) == core::AddrKind::ResolvablePrivate);
  a[0] = 0x80;                           // 10 000000 -> reserved, must not be trusted
  CHECK(!core::isStable(core::classifyAddress(a, 1)));
  a[0] = 0xC0;                           // 11 000000 -> random static
  CHECK(core::classifyAddress(a, 1) == core::AddrKind::RandomStatic);
}

TEST(addr_random_identity_type_is_treated_as_the_bits_say) {
  CHECK(core::classifyAddress(kStatic, 3) == core::AddrKind::RandomStatic);
  CHECK(core::classifyAddress(kRpa, 3) == core::AddrKind::ResolvablePrivate);
}

TEST(addr_parse_and_format_round_trip) {
  uint8_t got[6];
  CHECK(core::parseAddress("c5:1a:7d:DA:71:13", got));
  CHECK(core::addrEqual(got, kStatic));
  char text[18];
  core::formatAddress(got, text, sizeof text);
  CHECK_STREQ(text, "C5:1A:7D:DA:71:13");
}

TEST(addr_parse_accepts_dashes) {
  uint8_t got[6];
  CHECK(core::parseAddress("C5-1A-7D-DA-71-13", got));
  CHECK(core::addrEqual(got, kStatic));
}

TEST(addr_parse_rejects_rubbish) {
  uint8_t got[6];
  CHECK(!core::parseAddress("", got));
  CHECK(!core::parseAddress("c5:1a:7d:da:71", got));
  CHECK(!core::parseAddress("zz:1a:7d:da:71:13", got));
  CHECK(!core::parseAddress("c5-1a-7d-da-71-13-99", got));
  CHECK(!core::parseAddress(nullptr, got));
}

TEST(addr_format_into_a_short_buffer_is_safe) {
  char text[8];
  core::formatAddress(kStatic, text, sizeof text);
  CHECK_EQ(text[sizeof text - 1], 0);
}

TEST(addr_every_kind_has_a_name) {
  const core::AddrKind all[] = {core::AddrKind::PublicStatic, core::AddrKind::RandomStatic,
                                core::AddrKind::ResolvablePrivate,
                                core::AddrKind::NonResolvablePrivate};
  for (auto k : all) CHECK(std::strlen(core::addrKindName(k)) > 0);
}
