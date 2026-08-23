#include "test_main.h"
#include "core/settings_model.h"

TEST(settings_defaults_match_the_spec) {
  core::Settings s;
  CHECK_EQ(s.num(core::S_PIN_OUT), 5);
  CHECK(s.flag(core::S_OUT_ACTIVE_HIGH));
  CHECK_EQ(s.num(core::S_PIN_SENSE), 3);
  CHECK(s.flag(core::S_SENSE_ACT_LOW));
  CHECK_EQ(s.num(core::S_PIN_LED), 8);
  CHECK(s.flag(core::S_LED_ACTIVE_LOW));
  CHECK_EQ(s.num(core::S_PULSE_MS), 300);
  CHECK_EQ(s.num(core::S_LONG_PRESS_MS), 6000);
  CHECK_EQ(s.num(core::S_SCAN_INTVL_MS), 200);
  CHECK_EQ(s.num(core::S_SCAN_WINDOW_MS), 60);
  CHECK_EQ(s.num(core::S_COOLDOWN_MS), 10000);
  CHECK_EQ(s.num(core::S_POSTOFF_MS), 30000);
  CHECK(!s.flag(core::S_REQUIRE_ABSENCE));   // optional, off by default
  CHECK_EQ(s.num(core::S_ABSENCE_MS), 60000);
  CHECK_EQ(s.num(core::S_RSSI_MIN), -100);
  CHECK(s.flag(core::S_SLEEP_IS_OFF));
  CHECK_EQ(s.num(core::S_OFF_CONFIRM), 12);
  CHECK_EQ(s.num(core::S_ON_FAST_MS), 300);
  CHECK_STREQ(s.str(core::S_AP_PASS), "123454321");
  CHECK_STREQ(s.str(core::S_HOSTNAME), "pcpower");
}

TEST(settings_every_key_is_unique_and_short_enough) {
  for (uint8_t i = 0; i < core::S_NUM_SETTINGS; ++i) {
    CHECK(std::strlen(core::kSettingDefs[i].key) > 0);
    CHECK(std::strlen(core::kSettingDefs[i].key) <= 15);  // NVS key limit
    CHECK(std::strlen(core::kSettingDefs[i].label) > 0);
    CHECK(std::strlen(core::kSettingDefs[i].group) > 0);
    for (uint8_t j = i + 1; j < core::S_NUM_SETTINGS; ++j)
      CHECK(std::strcmp(core::kSettingDefs[i].key, core::kSettingDefs[j].key) != 0);
  }
}

TEST(settings_clamp_instead_of_reject) {
  core::Settings s;
  bool clamped = false;
  CHECK(s.setNum(core::S_PULSE_MS, 5, &clamped));
  CHECK_EQ(s.num(core::S_PULSE_MS), 50);
  CHECK(clamped);
  clamped = false;
  CHECK(s.setNum(core::S_PULSE_MS, 99999, &clamped));
  CHECK_EQ(s.num(core::S_PULSE_MS), 2000);
  CHECK(clamped);
  clamped = false;
  CHECK(s.setNum(core::S_PULSE_MS, 400, &clamped));
  CHECK_EQ(s.num(core::S_PULSE_MS), 400);
  CHECK(!clamped);
}

TEST(settings_set_by_key_parses_bools_and_enums) {
  core::Settings s;
  char err[96];
  CHECK(s.setByKey("require_absence", "true", err, sizeof err));
  CHECK(s.flag(core::S_REQUIRE_ABSENCE));
  CHECK(s.setByKey("require_absence", "off", err, sizeof err));
  CHECK(!s.flag(core::S_REQUIRE_ABSENCE));
  CHECK(s.setByKey("require_absence", "yes", err, sizeof err));
  CHECK(s.flag(core::S_REQUIRE_ABSENCE));
  CHECK(s.setByKey("sense_mode", "force_off", err, sizeof err));
  CHECK_EQ(s.num(core::S_SENSE_MODE), (int32_t)core::SenseMode::ForceOff);
  CHECK(s.setByKey("sense_mode", "0", err, sizeof err));
  CHECK_EQ(s.num(core::S_SENSE_MODE), 0);
}

TEST(settings_set_by_key_reports_bad_input) {
  core::Settings s;
  char err[96];
  err[0] = 0;
  CHECK(!s.setByKey("no_such_key", "1", err, sizeof err));
  CHECK(std::strlen(err) > 0);
  err[0] = 0;
  CHECK(!s.setByKey("pulse_ms", "banana", err, sizeof err));
  CHECK(std::strlen(err) > 0);
  CHECK_EQ(s.num(core::S_PULSE_MS), 300);  // unchanged
  err[0] = 0;
  CHECK(!s.setByKey("pulse_ms", "", err, sizeof err));
  err[0] = 0;
  CHECK(!s.setByKey("sense_mode", "sideways", err, sizeof err));
  err[0] = 0;
  CHECK(!s.setByKey("ap_pass", "short", err, sizeof err));  // WPA2 needs 8
  CHECK_STREQ(s.str(core::S_AP_PASS), "123454321");
}

TEST(settings_negative_values_parse) {
  core::Settings s;
  char err[96];
  CHECK(s.setByKey("rssi_min", "-70", err, sizeof err));
  CHECK_EQ(s.num(core::S_RSSI_MIN), -70);
}

TEST(settings_coupling_keeps_the_radio_usable) {
  core::Settings s;
  s.setNum(core::S_SCAN_INTVL_MS, 200);
  s.setNum(core::S_SCAN_WINDOW_MS, 500);
  s.applyCoupling(false);
  CHECK_EQ(s.num(core::S_SCAN_WINDOW_MS), 200);  // window never exceeds interval
  s.setNum(core::S_SCAN_WINDOW_MS, 200);
  s.applyCoupling(true);
  CHECK_EQ(s.num(core::S_SCAN_WINDOW_MS), 120);  // 60% of interval while WiFi is up
  s.setNum(core::S_SCAN_INTVL_MS, 50);
  s.setNum(core::S_SCAN_WINDOW_MS, 10);
  s.applyCoupling(true);
  CHECK_EQ(s.num(core::S_SCAN_WINDOW_MS), 10);   // never clamped below the minimum
}

TEST(settings_conf_round_trip) {
  core::Settings a;
  a.setNum(core::S_PULSE_MS, 450);
  a.setNum(core::S_REQUIRE_ABSENCE, 1);
  char err[96];
  CHECK(a.setStr(core::S_HOSTNAME, "steambox", err, sizeof err));

  char conf[6144];
  size_t n = a.toConf(conf, sizeof conf);
  CHECK(n > 0 && n < sizeof conf);

  core::Settings b;
  CHECK(b.applyConf(conf, err, sizeof err) > 0);
  CHECK_EQ(b.num(core::S_PULSE_MS), 450);
  CHECK(b.flag(core::S_REQUIRE_ABSENCE));
  CHECK_STREQ(b.str(core::S_HOSTNAME), "steambox");
  CHECK_STREQ(b.str(core::S_AP_PASS), "123454321");
}

TEST(settings_conf_ignores_comments_and_blank_lines) {
  core::Settings s;
  char err[96];
  const char* conf = "# exported by PcPower\n\npulse_ms=333\n  \nsleep_is_off=0\n";
  CHECK_EQ(s.applyConf(conf, err, sizeof err), 2);
  CHECK_EQ(s.num(core::S_PULSE_MS), 333);
  CHECK(!s.flag(core::S_SLEEP_IS_OFF));
}

TEST(settings_conf_survives_windows_line_endings) {
  core::Settings s;
  char err[96];
  CHECK_EQ(s.applyConf("pulse_ms=321\r\nsleep_is_off=0\r\n", err, sizeof err), 2);
  CHECK_EQ(s.num(core::S_PULSE_MS), 321);
}

TEST(settings_json_is_well_formed_and_complete) {
  core::Settings s;
  char buf[6144];
  size_t n = s.toJson(buf, sizeof buf);
  CHECK(n > 0);
  CHECK(buf[0] == '{');
  CHECK(buf[n - 1] == '}');
  for (uint8_t i = 0; i < core::S_NUM_SETTINGS; ++i)
    CHECK(std::strstr(buf, core::kSettingDefs[i].key) != nullptr);
  CHECK(std::strstr(buf, "\"ap_pass\":\"123454321\"") != nullptr);
  CHECK(std::strstr(buf, "\"pulse_ms\":300") != nullptr);
  CHECK(std::strstr(buf, "\"rssi_min\":-100") != nullptr);
}

TEST(settings_json_never_overflows_a_small_buffer) {
  core::Settings s;
  char small[32];
  size_t n = s.toJson(small, sizeof small);
  CHECK(n < sizeof small);
  CHECK_EQ(small[sizeof small - 1], 0);
}

TEST(settings_json_escapes_strings) {
  core::Settings s;
  char err[96];
  CHECK(s.setStr(core::S_AP_SSID, "my \"odd\" \\ap", err, sizeof err));
  char buf[6144];
  s.toJson(buf, sizeof buf);
  CHECK(std::strstr(buf, "my \\\"odd\\\" \\\\ap") != nullptr);
}

TEST(settings_schema_carries_ranges_and_groups) {
  char buf[16384];
  size_t n = core::Settings::schemaToJson(buf, sizeof buf);
  CHECK(n > 0);
  CHECK(buf[0] == '[');
  CHECK(buf[n - 1] == ']');
  CHECK(std::strstr(buf, "\"key\":\"pulse_ms\"") != nullptr);
  CHECK(std::strstr(buf, "\"min\":50") != nullptr);
  CHECK(std::strstr(buf, "\"max\":2000") != nullptr);
  CHECK(std::strstr(buf, "\"group\":") != nullptr);
  CHECK(std::strstr(buf, "auto|force_off") != nullptr);
}

TEST(settings_find_def_resolves_ids) {
  core::SettingId id = core::S_PIN_OUT;
  CHECK(core::Settings::findDef("cooldown_ms", &id) != nullptr);
  CHECK(id == core::S_COOLDOWN_MS);
  CHECK(core::Settings::findDef("nope", &id) == nullptr);
}
