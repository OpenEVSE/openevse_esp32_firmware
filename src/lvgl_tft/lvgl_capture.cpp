// Host-only LVGL screenshot exporter for the native build.
#if defined(ENABLE_SCREEN_LVGL_TFT) && defined(EPOXY_DUINO)

#include <Arduino.h>
#include <errno.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "lvgl_capture.h"
#include "boot_screen.h"
#include "charge_screen.h"
#include "lvgl_panel.h"
#include "nightshift.h"
#include "openevse.h"
#include "setup_screen.h"
#include "standby_screen.h"
#include "fault_screen.h"
#include "fault_text.h"
#include "notifications.h"

// Advance LVGL until the screen has settled, then a few frames more.
//
// The ring tweens over ARC_ANIM_MS, so a fixed handful of frames captures it
// part-way through its travel and the screenshot shows a gauge reading that
// never actually occurs. Run the clock on until no animation is left, with a
// cap so a runaway animation can't wedge the export.
static void pump_frames(uint32_t frames = 4)
{
  const uint32_t max_frames = 120;   // ~2 s of 16 ms ticks
  uint32_t i = 0;
  do {
    lv_tick_inc(16);
    lv_timer_handler();
    lv_refr_now(NULL);
    lvgl_panel_pump();
    i++;
  } while((i < frames || lv_anim_count_running() > 0) && i < max_frames);
}

static bool write_capture(const char *out_dir, const char *name)
{
  char path[256];
  snprintf(path, sizeof(path), "%s/%s.ppm", out_dir, name);
  return lvgl_panel_write_ppm(path);
}

// Advisory line text, formatted exactly the way lcd_lvgl.cpp builds it: the
// worst advisory's short text, with a "+N" suffix when more are live. A
// separate buffer from lcd_lvgl.cpp's file-scope notify_buf -- this is
// host-only capture code in its own translation unit, never linked into a
// firmware image.
static char cap_notify_buf[48];

static const char *sample_notify_line(const char *id, size_t extra_count)
{
  if(extra_count > 0) {
    snprintf(cap_notify_buf, sizeof(cap_notify_buf), LV_SYMBOL_WARNING " %s  +%u",
             notification_short_text(id), (unsigned)extra_count);
  } else {
    snprintf(cap_notify_buf, sizeof(cap_notify_buf), LV_SYMBOL_WARNING " %s",
             notification_short_text(id));
  }
  return cap_notify_buf;
}

static ChargeScreenData sample_charge_data()
{
  ChargeScreenData d = {};
  d.pilot_a = 32;
  d.pilot_source = "";
  d.volts = 241.0f;
  d.temp_valid = true;
  d.temp_c = 31.4f;
  d.wifi_client = true;
  d.wifi_connected = true;
  d.wifi_pct = 85;
  d.datetime = "2026-06-21  07:37";
  d.hostname = "openevse.local";
  d.ip = "192.168.4.2";
  d.msg_line = "";
  // Lifetime totals back the tile column whenever no vehicle is attached.
  d.total_day_kwh = 8.42;
  d.total_week_kwh = 137.6;
  d.total_kwh = 4821.0;
  return d;
}

bool lvgl_capture_write_samples(const char *out_dir)
{
  if(out_dir == nullptr || out_dir[0] == '\0') {
    fprintf(stderr, "LVGL capture output directory is required\n");
    return false;
  }

  if(mkdir(out_dir, 0777) != 0 && errno != EEXIST) {
    fprintf(stderr, "Failed to create capture directory '%s': %s\n", out_dir, strerror(errno));
    return false;
  }

  if(!lvgl_panel_begin()) {
    fprintf(stderr, "Failed to initialise native LVGL panel\n");
    return false;
  }

  ns_set_theme(false);

  boot_screen_build();
  boot_screen_update(48, "Bringing up services");
  pump_frames();
  if(!write_capture(out_dir, "boot-splash")) {
    return false;
  }

  setup_screen_build("WIFI:T:WPA;S:OpenEVSE_123456;P:openevse;;",
                     "OpenEVSE_123456",
                     "openevse",
                     "192.168.4.1");
  boot_screen_destroy();
  pump_frames();
  if(!write_capture(out_dir, "wifi-setup")) {
    return false;
  }

  charge_screen_build();
  setup_screen_destroy();

  ChargeScreenData d = sample_charge_data();

  d.evse_state = OPENEVSE_STATE_NOT_CONNECTED;
  d.vehicle_connected = false;
  d.session_active = false;
  d.charging = false;
  d.amps = 0.0f;
  d.elapsed_s = 0;
  d.session_wh = 0.0;
  charge_screen_update(d);
  pump_frames();
  if(!write_capture(out_dir, "charge-idle")) {
    return false;
  }

  d.evse_state = OPENEVSE_STATE_CONNECTED;
  d.vehicle_connected = true;
  d.session_active = true;
  charge_screen_update(d);
  pump_frames();
  if(!write_capture(out_dir, "charge-connected")) {
    return false;
  }

  // Charging with everything live: a claim trimming the pilot, and vehicle SoC
  // and range from the HA/MQTT push path. Exercises both rings at once.
  d.evse_state = OPENEVSE_STATE_CHARGING;
  d.vehicle_connected = true;
  d.charging = true;
  d.power_kw = 7.20f;
  d.amps = 30.0f;
  d.elapsed_s = 4523;
  d.session_wh = 9120.0;
  d.pilot_a = 31;
  d.pilot_source = "solar divert";
  d.soc_valid = true;
  d.soc_percent = 78;
  d.range_valid = true;
  d.range = 214;
  d.range_miles = false;
  charge_screen_update(d);
  pump_frames();
  if(!write_capture(out_dir, "charge-charging")) {
    return false;
  }

  // Advisory active while charging: the amber perimeter border plus the
  // worst advisory named on the top strip's second line (a transient message
  // would win that line instead - see charge_screen.cpp). Two advisories are
  // live, so this also exercises the "+N" suffix. Neither --dump-lvgl-screens
  // path was otherwise rendered anywhere in the tree.
  d.msg_line = "";
  d.notify_active = true;
  d.notify_line = sample_notify_line("thermal.high_temp", 2);
  charge_screen_update(d);
  pump_frames();
  if(!write_capture(out_dir, "charge-advisory")) {
    return false;
  }

  // Clear the advisory before the remaining scenarios, which exercise
  // unrelated screen states and must not carry the border over by accident.
  d.notify_active = false;
  d.notify_line = "";

  // Worst-case widths: the longest fault word, the longest claim name and
  // three-digit figures all at once.
  d.evse_state = OPENEVSE_STATE_GFI_SELF_TEST_FAILED;
  d.vehicle_connected = true;
  d.charging = false;
  d.power_kw = 0.0f;
  d.amps = 0.0f;
  d.elapsed_s = 359999;
  d.session_wh = 99450.0;
  d.pilot_a = 48;
  d.pilot_source = "demand shaper";
  d.soc_percent = 100;
  d.range = 388;
  d.range_miles = true;
  d.temp_c = 61.2f;
  d.msg_line = "GFCI self-test fault";
  charge_screen_update(d);
  pump_frames();
  if(!write_capture(out_dir, "charge-fault")) {
    return false;
  }

  // Standby: the same skeleton with the live figures dropped. Captured last so
  // a side-by-side with charge-idle shows the ring and tile column landing in
  // exactly the same place.
  StandbyScreenData sd = {};
  sd.evse_state = OPENEVSE_STATE_SLEEPING;
  sd.temp_valid = true;
  sd.temp_c = 31.4f;
  sd.wifi_client = true;
  sd.wifi_connected = true;
  sd.wifi_pct = 85;
  sd.today_kwh = 8.42;
  sd.week_kwh = 137.6;
  sd.total_kwh = 4821.0;
  sd.clock = "2026-06-21  07:37";
  sd.hostname = "openevse.local";
  sd.ip = "192.168.4.2";

  standby_screen_build();
  charge_screen_destroy();
  standby_screen_update(sd);
  pump_frames();
  if(!write_capture(out_dir, "standby")) {
    return false;
  }

  // Advisory active on standby: the amber perimeter border, and the advisory
  // takes the footer line outright in place of the hostname/IP -- a warning
  // outranks knowing where to point a browser (see standby_screen.cpp). A
  // single advisory here, so this also confirms the "+N" suffix is only
  // added when there is more than one.
  sd.notify_active = true;
  sd.notify_line = sample_notify_line("safety.gfci_check", 0);
  standby_screen_update(sd);
  pump_frames();
  if(!write_capture(out_dir, "standby-advisory")) {
    return false;
  }

  // Every fault page, so the whole copy set can be reviewed at the size it is
  // actually read at rather than one sample standing in for twelve.
  static const struct { uint8_t state; const char *name; } FAULT_SHOTS[] = {
    { OPENEVSE_STATE_VENT_REQUIRED,        "fault-vent-required" },
    { OPENEVSE_STATE_DIODE_CHECK_FAILED,   "fault-diode-check" },
    { OPENEVSE_STATE_GFI_FAULT,            "fault-gfci-trip" },
    { OPENEVSE_STATE_NO_EARTH_GROUND,      "fault-no-ground" },
    { OPENEVSE_STATE_STUCK_RELAY,          "fault-stuck-relay" },
    { OPENEVSE_STATE_GFI_SELF_TEST_FAILED, "fault-gfci-self-test" },
    { OPENEVSE_STATE_OVER_TEMPERATURE,     "fault-over-temp" },
    { OPENEVSE_STATE_OVER_CURRENT,         "fault-over-current" },
    { OPENEVSE_STATE_RELAY_CLOSURE_FAULT,  "fault-relay-fault" },
    { OPENEVSE_STATE_EEPROM_FAILURE,       "fault-eeprom-fail" },
    { OPENEVSE_STATE_PP_MISSING,           "fault-pp-missing" },
    { OPENEVSE_STATE_PP_SHORTED,           "fault-pp-shorted" },
  };

  FaultScreenData fd = {};
  fd.wifi_client = true;
  fd.wifi_connected = true;
  fd.wifi_pct = 85;
  fd.hostname = "openevse.local";
  fd.ip = "192.168.4.2";

  fault_screen_build();
  standby_screen_destroy();
  for(size_t i = 0; i < sizeof(FAULT_SHOTS) / sizeof(FAULT_SHOTS[0]); i++) {
    fd.evse_state = FAULT_SHOTS[i].state;
    fault_screen_update(fd);
    pump_frames();
    if(!write_capture(out_dir, FAULT_SHOTS[i].name)) {
      return false;
    }
  }

  fault_screen_destroy();
  return true;
}

#endif
