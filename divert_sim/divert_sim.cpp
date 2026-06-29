// divert_sim entrypoint — full runner is implemented in sim/runner.cpp.
// This file is a thin shim that parses CLI args and delegates.
//
// Backwards compatibility with the legacy stdin-CSV flags has been removed
// in favour of a single scenario-driven JSON entrypoint.

#include <iostream>
#include <string>
#include <cstdio>   // std::remove
#include <cstdlib>  // std::_Exit

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

int main(int argc, char **argv)
{
  auto exit_now = [](int code) -> int {
    std::cout.flush();
    std::cerr.flush();
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
    return exit_now(0);
  }

  EpoxyTest::set_millis(0);
  fs::EpoxyFS.begin();

  if (result.count("config-load")) {
    // Already have the EEPROM file — let config_load_settings read it.
  } else {
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
    return exit_now(0);
  }

  if (scenario.empty()) {
    std::cerr << options.help() << std::endl;
    return exit_now(1);
  }

  return exit_now(sim::run(scenario, output, result.count("config-check") != 0));
}
