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

static void pump_frames(uint32_t frames = 4)
{
  for(uint32_t i = 0; i < frames; i++) {
    lv_tick_inc(16);
    lv_timer_handler();
    lv_refr_now(NULL);
  }
}

static bool write_capture(const char *out_dir, const char *name)
{
  char path[256];
  snprintf(path, sizeof(path), "%s/%s.ppm", out_dir, name);
  return lvgl_panel_write_ppm(path);
}

static ChargeScreenData sample_charge_data()
{
  ChargeScreenData d = {};
  d.pilot_a = 32;
  d.volts = 241.0f;
  d.temp_valid = true;
  d.temp_c = 31.4f;
  d.wifi_client = true;
  d.wifi_connected = true;
  d.rssi = -58;
  d.datetime = "2026-06-21 07:37:00";
  d.hostname = "openevse.local";
  d.ip = "192.168.4.2";
  d.msg_line = "";
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
  charge_screen_update(d);
  pump_frames();
  if(!write_capture(out_dir, "charge-connected")) {
    return false;
  }

  d.evse_state = OPENEVSE_STATE_CHARGING;
  d.vehicle_connected = true;
  d.charging = true;
  d.power_kw = 7.20f;
  d.amps = 30.0f;
  d.elapsed_s = 4523;
  d.session_wh = 9120.0;
  charge_screen_update(d);
  pump_frames();
  if(!write_capture(out_dir, "charge-charging")) {
    return false;
  }

  d.evse_state = OPENEVSE_STATE_GFI_FAULT;
  d.vehicle_connected = true;
  d.charging = false;
  d.power_kw = 0.0f;
  d.amps = 0.0f;
  d.elapsed_s = 0;
  d.session_wh = 0.0;
  d.msg_line = "GFCI self-test fault";
  charge_screen_update(d);
  pump_frames();
  if(!write_capture(out_dir, "charge-fault")) {
    return false;
  }

  charge_screen_destroy();
  return true;
}

#endif
