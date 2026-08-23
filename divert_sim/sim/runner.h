#ifndef _DIVERT_SIM_SIM_RUNNER_H
#define _DIVERT_SIM_SIM_RUNNER_H

#include <string>

namespace sim {

// Run the scenario described by the JSON file at `scenario_path`.
// `output_path` is the CSV output path (empty = stdout).
// If `config_check` is true, dump the resolved config and exit without running.
//
// Returns process exit code.
int run(const std::string &scenario_path,
        const std::string &output_path,
        bool config_check);

} // namespace sim

#endif // _DIVERT_SIM_SIM_RUNNER_H
