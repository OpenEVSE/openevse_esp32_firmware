# OpenEVSE Solar Divert Simulator

A simulation tool for testing and analyzing the OpenEVSE solar diverter algorithm and current shaper functionality. This simulator allows you to test different configurations with historical solar/grid power data to optimize charging behavior.

## Overview

The divert_sim is a command-line tool that simulates the behavior of an OpenEVSE electric vehicle charger with solar diversion capabilities. It processes CSV files containing solar generation or grid import/export data and outputs charging behavior over time.

The simulator includes:
- **Command-line tool** (`divert_sim`) - Core simulation engine
- **Python test suite** - Automated pytest-based validation
- **Interactive web application** - Browser-based configuration and visualization

## Building

### Prerequisites

- g++ compiler
- Make
- Python 3.x (for testing and web server)
- EpoxyDuino and required Arduino libraries (see Makefile)

### Build Instructions

```bash
make
```

Or to build with verbose output:

```bash
make V=1
```

The build process creates the `divert_sim` executable in the current directory.

## Usage

### Command-Line Interface

Basic usage:

```bash
./divert_sim [options] < input.csv > output.csv
```

#### Command-Line Options

| Option | Description |
|--------|-------------|
| `-d, --date N` | Specify the date column number (default: 0) |
| `-s, --solar N` | Specify the solar generation column number |
| `-g, --gridie N` | Specify the grid import/export column number |
| `-l, --livepwr N` | Specify the live power column for current shaper |
| `-v, --voltage N` | Voltage column (if N < 50) or fixed voltage value |
| `-c, --config FILE\|JSON` | Configuration file path or JSON string |
| `--sep CHAR` | Field separator (default: comma) |
| `--kw` | Input values are in kW (will be converted to watts) |
| `--config-check` | Output the configuration and exit |
| `--config-load` | Simulate loading config from EEPROM |
| `--config-commit` | Simulate saving config to EEPROM |
| `--help` | Display help message |

#### Examples

**Basic solar simulation:**
```bash
./divert_sim -s 1 < data/almostperfect.csv > output/almostperfect.csv
```

**Grid import/export simulation:**
```bash
./divert_sim -s 1 -g 2 < data/day1_grid_ie.csv > output/day1_grid_ie.csv
```

**With custom configuration:**
```bash
./divert_sim -s 1 -c '{"divert_PV_ratio":1.1,"divert_attack_smoothing_time":60}' < data/day1.csv > output/day1.csv
```

**Using a configuration file:**
```bash
./divert_sim -s 1 -c config.json < data/day1.csv > output/day1.csv
```

**Current shaper simulation:**
```bash
./divert_sim -l 1 --sep ';' < data/data_shaper.csv > output/data_shaper.csv
```

**kW input data:**
```bash
./divert_sim -s 1 --sep ';' --kw < data/Energy_and_Power_Day_2020-03-22.csv > output/result.csv
```

### Input CSV Format

The input CSV file should contain timestamped power data. The first column (column 0) should always be the date/time.

**Supported date formats:**
- ISO 8601: `2020-03-22T14:30:00Z` or `2020-03-22T14:30:00+00:00`
- Standard: `2020-03-22 14:30:00`
- European: `22/03/2020 14:30:00`
- Time only: `14:30 PM` (assumes 2020-01-01)
- Unix timestamp: `1584890400`

**Example input CSV:**
```csv
Date,Solar (W),Grid IE (W)
2020-03-22T08:00:00Z,0,500
2020-03-22T09:00:00Z,1500,-200
2020-03-22T10:00:00Z,3000,-1800
```

### Output CSV Format

The output CSV contains the following columns:

| Column | Description |
|--------|-------------|
| Date | Timestamp (dd/mm/yyyy HH:MM:SS) |
| Solar | Solar generation (W) |
| Grid IE | Grid import/export (W) |
| Pilot | EVSE pilot current (A) |
| Charge Power | Actual charging power (W) |
| Min Charge Power | Minimum charging power (W) |
| State | EVSE state code |
| Smoothed Available | Smoothed available current (W) |
| Live Power | Current live power reading (W) |
| Smoothed Live Power | Smoothed live power (W) |
| Shaper Max Power | Current shaper maximum power (W) |

**EVSE State Codes:**
- 0: Starting
- 1: Not Connected
- 2: Connected
- 3: Charging
- 4: Vent Required / Sleeping
- 5: Disabled
- 6: GFI Fault
- 7: No Earth Ground
- 8: Stuck Relay
- 9: GFI Self Test Failed

## Configuration Options

Key configuration parameters that can be adjusted:

### Solar Divert Settings
- `divert_PV_ratio` (default: 1.1) - Ratio of solar power to available charging capacity
- `divert_attack_smoothing_time` (default: 60) - Smoothing time when increasing charge rate (seconds)
- `divert_decay_smoothing_time` (default: 600) - Smoothing time when decreasing charge rate (seconds)
- `divert_min_charge_time` (default: 600) - Minimum time to charge before stopping (seconds)

### Current Shaper Settings
- `current_shaper_max_pwr` (default: 0) - Maximum power limit (W)
- `current_shaper_smoothing_time` (default: 60) - Power smoothing time (seconds)
- `current_shaper_min_pause_time` (default: 300) - Minimum pause between charge sessions (seconds)
- `current_shaper_data_maxinterval` (default: 60) - Maximum interval between data updates (seconds)

## Python Test Suite

### Installation

Install Python dependencies:

```bash
pip install -r requirements.txt
```

Or use a virtual environment:

```bash
python3 -m venv .venv
source .venv/bin/activate  # On Windows: .venv\Scripts\activate
pip install -r requirements.txt
```

### Running Tests

Execute all tests:

```bash
pytest
```

Run specific test file:

```bash
pytest test_divert.py
```

Run with verbose output:

```bash
pytest -v
```

### Test Structure

The test suite (`test_divert.py`) validates:
- Solar-only diversion scenarios
- Grid import/export scenarios
- Various data formats (CSV, semicolon-separated, kW units)
- Different configuration options
- Edge cases and boundary conditions

Each test verifies:
- Total solar generation (kWh)
- Total EV charge (kWh)
- Charge from solar (kWh)
- Charge from grid (kWh)
- Number of charging sessions
- Min/max/total charging time

## Interactive Web Application

### Starting the Web Server

```bash
python3 server.py
```

The server starts on `http://localhost:8000`

### Using the Web Interface

1. Open `http://localhost:8000/interactive.html` in your browser
2. Adjust configuration parameters:
   - Solar divert PV ratio
   - Attack and decay smoothing times
   - Minimum charge time
   - Current shaper settings
3. Click "Run Simulation"
4. View results in interactive charts
5. Compare results against baseline ("master" profile)

### Features

- **Real-time visualization** - Charts display solar generation, grid import/export, and charging behavior
- **Multiple datasets** - Runs simulations across all available test datasets
- **Comparative analysis** - Compare your configuration against baseline results
- **Summary statistics** - View total energy metrics for each scenario

### Available Test Datasets

The web application runs simulations on the following datasets:
- `almostperfect` - Ideal sunny day
- `CloudyMorning` - Variable morning conditions
- `day1`, `day2`, `day3` - Various typical days
- `day1_grid_ie`, `day2_grid_ie`, `day3_grid_ie` - With grid import/export data
- `solar-vrms` - With voltage variation
- `Energy_and_Power_Day_*` - Real-world data samples
- `data_shaper` - Current shaper test data

### View Results

Access test results at `http://localhost:8000/view.html` to see:
- Summary table with energy metrics
- Interactive charts for each dataset
- Comparison between different configurations

## Directory Structure

```
divert_sim/
├── divert_sim.cpp           # Main simulator source code
├── RapiSender.cpp           # RAPI communication simulation
├── Makefile                 # Build configuration
├── run_simulations.py       # Python test runner
├── test_divert.py           # Pytest test suite
├── test_config.py           # Configuration tests
├── server.py                # Web server for interactive mode
├── interactive.html         # Interactive web UI
├── view.html                # Results visualization page
├── simulations.js           # JavaScript for charts
├── simulations.css          # Styling
├── requirements.txt         # Python dependencies
├── data/                    # Input CSV test datasets
└── output/                  # Generated simulation results
```

## Debugging

Enable debug output by uncommenting debug flags in the Makefile:

```makefile
-D ENABLE_DEBUG
-D ENABLE_DEBUG_MICRO_TASK
-D ENABLE_DEBUG_DIVERT
-D ENABLE_DEBUG_INPUT_FILTER
-D ENABLE_DEBUG_EVSE_MAN
-D ENABLE_DEBUG_EVSE_MONITOR
```

Then rebuild:

```bash
make clean
make
```

## Troubleshooting

**Build errors:**
- Ensure all library dependencies are available (check paths in Makefile)
- Verify g++ compiler is installed
- Check that EpoxyDuino and Arduino libraries are in correct locations

**Python errors:**
- Verify Python 3 is installed: `python3 --version`
- Install dependencies: `pip install -r requirements.txt`
- Check that `divert_sim` executable exists and is executable

**Web server issues:**
- Ensure port 8000 is not in use
- Check firewall settings
- Verify `simplejson` is installed: `pip install simplejson`

## License

This project is part of the OpenEVSE ecosystem. Refer to the main project repository for license information.

## Related Projects

- [OpenEVSE Firmware](https://github.com/OpenEVSE/openevse_esp32_firmware)
- [OpenEVSE Hardware](https://github.com/OpenEVSE/OpenEVSE)
- [EpoxyDuino](https://github.com/bxparks/EpoxyDuino) - Arduino emulation for native C++
