"""Test the divert_sim process"""

# pylint: disable=line-too-long

from os import path
import os
from subprocess import PIPE, Popen
from datetime import datetime
from typing import Union

OPENEVSE_STATE_STARTING = 0
OPENEVSE_STATE_NOT_CONNECTED = 1
OPENEVSE_STATE_CONNECTED = 2
OPENEVSE_STATE_CHARGING = 3
OPENEVSE_STATE_VENT_REQUIRED = 4
OPENEVSE_STATE_DIODE_CHECK_FAILED = 5
OPENEVSE_STATE_GFI_FAULT = 6
OPENEVSE_STATE_NO_EARTH_GROUND = 7
OPENEVSE_STATE_STUCK_RELAY = 8
OPENEVSE_STATE_GFI_SELF_TEST_FAILED = 9
OPENEVSE_STATE_OVER_TEMPERATURE = 0
OPENEVSE_STATE_OVER_CURRENT = 1
OPENEVSE_STATE_SLEEPING = 4
OPENEVSE_STATE_DISABLED = 5

KWH_ROUNDING = 2

summary_filename = 'summary.csv'

def setup_summary(postfix: str = ''):
    global summary_filename
    """Create the output directory"""
    print("Setting up test environment")
    if not path.exists('output'):
        os.mkdir('output')
    summary_filename = 'summary'+postfix+'.csv'
    with open(path.join('output', summary_filename), 'w', encoding="utf-8") as summary_file:
        summary_file.write('"Dataset","Config","Total Solar (kWh)","Total EV Charge (kWh)","Charge from solar (kWh)","Charge from grid (kWh)","Number of charges","Min time charging","Max time charging","Total time charging"\n')

def run_simulation(dataset: str,
                output: str,
                config: Union[str, bool] = False, grid_ie_col: Union[bool, int] = False,
                solar_col: Union[bool, int] = False, voltage_col: Union[bool, int] = False,
                separator: str = ',', is_kw: bool = False,
                live_power_col: Union[bool, int] = False) -> tuple:
    """Run the divert_sim process on the given dataset and return the results"""
    line_number = 0

    last_date = None
    last_state = 0
    charge_start_date = None

    total_solar_wh = 0
    total_ev_wh = 0
    wh_from_solar = 0
    wh_from_grid = 0
    number_of_charges = 0
    min_time_charging = 0
    max_time_charging = 0
    total_time_charging = 0

    print("Testing dataset: " + dataset)

    # Read in the dataset and pass to the divert_sim process
    with open(path.join('data', dataset+'.csv'), 'r', encoding="utf-8") as input_data:
        with open(path.join('output', output+'.csv'), 'w', encoding="utf-8") as output_data:
            # open the divert_sim process
            command = ["./divert_sim"]
            if config:
                command.append("-c")
                command.append(config)
            if grid_ie_col:
                command.append("-g")
                command.append(str(grid_ie_col))
            if solar_col:
                command.append("-s")
                command.append(str(solar_col))
            if live_power_col:
                command.append("-l")
                command.append(str(live_power_col))
            if voltage_col:
                command.append("-v")
                command.append(str(voltage_col))
            if separator:
                command.append("--sep")
                command.append(separator)
            if is_kw:
                command.append("--kw")

            print(f"cat {input_data.name} | {' '.join(command)}")

            divert_process = Popen(command, stdin=input_data, stdout=PIPE,
                    stderr=PIPE, universal_newlines=True)
            while True:
                output = divert_process.stdout.readline()
                if output == '' and divert_process.poll() is not None:
                    break
                if output:
                    output_data.write(output)
                    line_number += 1
                    if line_number > 1:
                        # read in the csv line
                        csv_line = output.split(',')
                        date = datetime.strptime(csv_line[0], '%d/%m/%Y %H:%M:%S')
                        solar = float(csv_line[1])
                        # grid_ie = float(csv_line[2])
                        # pilot = int(csv_line[3])
                        charge_power = float(csv_line[4])
                        # min_charge_power = float(csv_line[5])
                        state = int(csv_line[6])
                        # smoothed_available = float(csv_line[7])

                        if last_date is not None:
                            # Get the difference between this date and last date
                            diff = date - last_date

                            hours = diff.seconds / 3600

                            total_solar_wh += solar * hours
                            ev_wh = charge_power * hours
                            total_ev_wh += charge_power * hours
                            charge_from_solar_wh = min(solar, charge_power) * hours
                            wh_from_solar += charge_from_solar_wh
                            wh_from_grid += ev_wh - charge_from_solar_wh

                            if state == OPENEVSE_STATE_CHARGING and last_state != OPENEVSE_STATE_CHARGING:
                                number_of_charges += 1
                                charge_start_date = date

                            if state != OPENEVSE_STATE_CHARGING and last_state == OPENEVSE_STATE_CHARGING:
                                this_session_time_charging = (date - charge_start_date).seconds
                                total_time_charging += this_session_time_charging
                                if min_time_charging == 0 or this_session_time_charging < min_time_charging:
                                    min_time_charging = this_session_time_charging
                                if this_session_time_charging > max_time_charging:
                                    max_time_charging = this_session_time_charging

                        last_date = date
                        last_state = state

        solar_kwh=total_solar_wh / 1000
        ev_kwh=total_ev_wh / 1000
        kwh_from_solar=wh_from_solar / 1000
        kwh_from_grid=wh_from_grid / 1000

        if config is False or config.startswith('{'):
            config = "Default"

        with open(path.join('output', summary_filename), 'a', encoding="utf-8") as summary_file:
            summary_file.write(f'"{dataset}","{config}",{solar_kwh},{ev_kwh},{kwh_from_solar},{kwh_from_grid},{number_of_charges},{min_time_charging},{max_time_charging},{total_time_charging}\n')


    return (round(solar_kwh, KWH_ROUNDING),
            round(ev_kwh, KWH_ROUNDING),
            round(kwh_from_solar, KWH_ROUNDING),
            round(kwh_from_grid, KWH_ROUNDING),
            number_of_charges,
            min_time_charging,
            max_time_charging,
            total_time_charging)


def run_loadsharing_simulation(scenario: str, output: str) -> dict:
    """Run a load sharing simulation and return per-peer metrics.

    Args:
        scenario: Path to scenario JSON file (relative to divert_sim dir)
        output: Output file name (without .csv)

        Returns:
                Dict keyed by peer_id with metrics:
                    - allocated_amp_seconds: total allocated current × time
                    - actual_amp_seconds: total actual draw × time
                    - allocation_changes: number of times allocation changed
                    - time_at_zero: seconds with zero allocation
                    - max_allocation: peak allocated current
                    - min_allocation_nonzero: minimum non-zero allocation
                    - final_soc: final state of charge percentage
                    - soc_delta: end SoC - start SoC
                    - max_soc: peak SoC over run
                Plus top-level keys:
                    - _supply_exceeded: True if total demand ever exceeded max power budget
                    - _max_total_actual: peak total actual current across all peers
                    - _max_total_demand_w: peak total demand in watts
                    - _rows: list of parsed output rows (for timestamp-specific assertions)
    """
    print(f"Testing load sharing scenario: {scenario}")

    command = ["./divert_sim", "--loadsharing", "--scenario", scenario]
    print(f"Running: {' '.join(command)}")

    with open(path.join('output', output + '.csv'), 'w', encoding="utf-8") as output_file:
        divert_process = Popen(command, stdout=PIPE, stderr=PIPE, universal_newlines=True)

        header = None
        peer_ids = []
        rows = []
        line_number = 0

        while True:
            line = divert_process.stdout.readline()
            if line == '' and divert_process.poll() is not None:
                break
            if line:
                output_file.write(line)
                line = line.strip()
                line_number += 1

                if line_number == 1:
                    header = line.split(',')
                    # Extract peer IDs from header columns like "evse-001_online"
                    for col in header:
                        if col.endswith('_online'):
                            peer_ids.append(col[:-len('_online')])
                    continue

                fields = line.split(',')
                row = {
                    'time': int(fields[0]),
                    'max_pwr_w': float(fields[1]),
                    'live_pwr_w': float(fields[2]),
                    'available_pwr_w': float(fields[3]),
                    'available_a': float(fields[4]),
                }
                col_idx = 5
                for pid in peer_ids:
                    row[f'{pid}_online'] = int(fields[col_idx])
                    row[f'{pid}_vehicle'] = int(fields[col_idx + 1])
                    row[f'{pid}_soc'] = float(fields[col_idx + 2])
                    row[f'{pid}_allocated'] = float(fields[col_idx + 3])
                    row[f'{pid}_available_power_w'] = float(fields[col_idx + 4])
                    row[f'{pid}_actual'] = float(fields[col_idx + 5])
                    row[f'{pid}_actual_power_w'] = float(fields[col_idx + 6])
                    row[f'{pid}_reason'] = fields[col_idx + 7]
                    col_idx += 8
                row['total_allocated'] = float(fields[col_idx])
                row['total_actual'] = float(fields[col_idx + 1])
                row['total_ev_power_w'] = float(fields[col_idx + 2])
                row['total_demand_w'] = float(fields[col_idx + 3])
                rows.append(row)

        # Check stderr for errors
        stderr = divert_process.stderr.read()
        if divert_process.returncode != 0:
            print(f"divert_sim failed (rc={divert_process.returncode}): {stderr}")

    # Compute per-peer metrics
    result = {
        '_rows': rows,
        '_supply_exceeded': False,
        '_max_total_actual': 0.0,
        '_max_total_demand_w': 0.0,
    }

    for pid in peer_ids:
        metrics = {
            'allocated_amp_seconds': 0.0,
            'actual_amp_seconds': 0.0,
            'allocation_changes': 0,
            'time_at_zero': 0,
            'max_allocation': 0.0,
            'min_allocation_nonzero': float('inf'),
        }
        last_allocation = None
        last_actual = None
        last_time = None

        start_soc = rows[0][f'{pid}_soc'] if rows else 0.0
        end_soc = start_soc
        max_soc = start_soc

        for row in rows:
            alloc = row[f'{pid}_allocated']
            actual = row[f'{pid}_actual']
            t = row['time']

            if last_time is not None:
                dt = t - last_time
                metrics['allocated_amp_seconds'] += last_allocation * dt
                metrics['actual_amp_seconds'] += last_actual * dt
                if last_allocation == 0:
                    metrics['time_at_zero'] += dt

            if last_allocation is not None and alloc != last_allocation:
                metrics['allocation_changes'] += 1

            if alloc > metrics['max_allocation']:
                metrics['max_allocation'] = alloc
            if alloc > 0 and alloc < metrics['min_allocation_nonzero']:
                metrics['min_allocation_nonzero'] = alloc

            soc = row[f'{pid}_soc']
            end_soc = soc
            if soc > max_soc:
                max_soc = soc

            last_allocation = alloc
            last_actual = actual
            last_time = t

        if metrics['min_allocation_nonzero'] == float('inf'):
            metrics['min_allocation_nonzero'] = 0.0

        metrics['final_soc'] = end_soc
        metrics['soc_delta'] = end_soc - start_soc
        metrics['max_soc'] = max_soc

        result[pid] = metrics

    # Check safety invariant: total demand should never exceed max power
    for row in rows:
        if row['total_actual'] > result['_max_total_actual']:
            result['_max_total_actual'] = row['total_actual']
        if row['total_demand_w'] > result['_max_total_demand_w']:
            result['_max_total_demand_w'] = row['total_demand_w']
        if row['total_demand_w'] > row['max_pwr_w'] * 1.001:
            result['_supply_exceeded'] = True

    return result
