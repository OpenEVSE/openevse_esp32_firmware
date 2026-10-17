// divert_sim entrypoint — full runner is implemented in sim/runner.cpp.
// This file is a thin shim that parses CLI args and delegates.
//
// Backwards compatibility with the legacy stdin-CSV flags has been removed
// in favour of a single scenario-driven JSON entrypoint.

#include <iostream>
#include <string>
#include <vector>
#include <cstdio>   // std::remove
#include <cstdlib>  // std::_Exit, mkdtemp, setenv
#include <ftw.h>
#include <unistd.h>

#include <Arduino.h>
#include <MicroTasks.h>
#include <EpoxyFS.h>
#include <epoxy_test/ArduinoTest.h>

#include "RapiSender.h"
#include "openevse.h"
#include "divert.h"
#include "current_shaper.h"
#include "manual.h"
#include "event.h"
#include "event_log.h"
#include "app_config.h"

#include "cxxopts.hpp"

#include "sim/sim_stream.h"
#include "sim/sim_evse.h"
#include "sim/runner.h"

// Globals required for linkage with the firmware modules that the sim builds.
// Per-peer instances live inside the runner; these globals are unused at
// runtime but satisfy references in `evse_man.cpp` (`divert.isActive()` and
// `shaper.getState()` in the event-log path) and in `current_shaper.cpp`
// (the legacy `extern CurrentShaperTask shaper`).
EventLog eventLog;
static SimStream g_globalStream;
EvseManager evse(g_globalStream, eventLog);
DivertTask divert(evse);
ManualOverride manual(evse);

time_t simulated_time = 0;

time_t divertmode_get_time()
{
  return simulated_time;
}

void event_send(String) {}
void event_send(JsonDocument &) {}
void emoncms_publish(JsonDocument &) {}

static std::string g_private_root;

static int remove_entry(const char *path, const struct stat *, int, struct FTW *)
{
  return ::remove(path);
}

// Give a scenario run its own EpoxyFS/EEPROM directory. Scenario runs happen
// in parallel from the same working directory, and the scheduler persists
// /schedule.json, so a shared store would leak one run's schedule into
// another. The --config-load/--config-commit flows keep the shared store on
// purpose (they test persistence across invocations).
static void use_private_storage()
{
  const char *tmp = getenv("TMPDIR");
  std::string tmpl = std::string(tmp && *tmp ? tmp : "/tmp") + "/divert_sim.XXXXXX";
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');
  if (!mkdtemp(buf.data())) return;
  g_private_root = buf.data();
  setenv("EPOXY_FS_ROOT", (g_private_root + "/fs").c_str(), 1);
  setenv("EPOXY_EEPROM_DATA", (g_private_root + "/eeprom").c_str(), 1);
}

int main(int argc, char **argv)
{
  auto exit_now = [](int code) -> void {
    std::cout.flush();
    std::cerr.flush();
    if (!g_private_root.empty()) {
      nftw(g_private_root.c_str(), remove_entry, 8, FTW_DEPTH | FTW_PHYS);
    }
    std::_Exit(code);
  };

  std::string scenario;
  std::string output;
  std::string config_json_arg;
  cxxopts::Options options(argv[0], "OpenEVSE multi-peer backend simulator");
  options.add_options()
    ("help", "Print help")
    ("scenario", "Path to scenario JSON file", cxxopts::value<std::string>(scenario))
    ("o,output", "Output CSV path (default: stdout)", cxxopts::value<std::string>(output))
    ("c,config", "Config JSON string to apply before running", cxxopts::value<std::string>(config_json_arg))
    ("config-check", "Print resolved config as JSON and exit")
    ("config-load", "Load config from EpoxyFS before applying other args")
    ("config-commit", "Commit config to EpoxyFS after applying args");

  auto result = options.parse(argc, argv);

  if (result.count("help")) {
    std::cout << options.help() << std::endl;
    exit_now(0);
  }

  if (!scenario.empty() && !result.count("config-load") && !result.count("config-commit")) {
    use_private_storage();
  }

  EpoxyTest::set_millis(0);
  fs::EpoxyFS.begin();

  if (result.count("config-load")) {
    // Already have the EEPROM file — let config_load_settings read it.
  } else if (g_private_root.empty()) {
    // Start clean: erase any EEPROM data left by a previous subprocess so
    // factory_config state doesn't leak between test runs.
    std::remove("epoxyeepromdata");
  }

  // Always call config_load_settings so that ConfigOptDefinition::setDefault()
  // is called via ConfigJson::reset(), giving every variable its computed
  // default value (e.g. hostname = "openevse-7856").
  config_load_settings();

  if (result.count("config-load")) {
    config_load_settings();
  }

  if (!config_json_arg.empty()) {
    String cfg(config_json_arg.c_str());
    config_deserialize(cfg);
  }

  if (result.count("config-commit")) {
    config_commit(true);
  }

  if (result.count("config-check") && scenario.empty()) {
    String json;
    config_serialize(json, true, false, false);
    std::cout << json.c_str() << std::endl;
    exit_now(0);
  }

  if (scenario.empty()) {
    std::cerr << options.help() << std::endl;
    exit_now(1);
  }

  exit_now(sim::run(scenario, output, result.count("config-check") != 0));
}
