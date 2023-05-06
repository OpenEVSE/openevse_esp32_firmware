#!/usr/bin/env python3
# PYTHON_ARGCOMPLETE_OK

from os import EX_OK, path
from subprocess import PIPE, Popen
from datetime import datetime

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

def divert_test(summary_file, dataset: str, config: bool = False, grid_ie_col: bool = False,
                solar_col: bool = False, voltage_col: bool = False,
                separator: str = ',', is_kw: bool = False) -> None:
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
        with open(path.join('output', dataset+'.csv'), 'w', encoding="utf-8") as output_data:
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
            if voltage_col:
                command.append("-v")
                command.append(str(voltage_col))
            if separator:
                command.append("--sep")
                command.append(separator)
            if is_kw:
                command.append("--kw")

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

        summary_file.write(f'"{dataset}",{solar_kwh},{ev_kwh},{kwh_from_solar},{kwh_from_grid},{number_of_charges},{min_time_charging},{max_time_charging},{total_time_charging}\n')


def main() -> int:
    """Run the divert_sim process on all the datasets in the data directory"""
    with open(path.join('output', 'summary.csv'), 'w', encoding="utf-8") as summary_file:
        summary_file.write('"Dataset","Total Solar (kWh)","Total EV Charge (kWh)","Charge from solar (kWh)","Charge from grid (kWh)","Number of charges","Min time charging","Max time charging","Total time charging"\n')
        divert_test(summary_file, 'almostperfect')
        divert_test(summary_file, 'CloudyMorning')
        divert_test(summary_file, 'day1')
        divert_test(summary_file, 'day2')
        divert_test(summary_file, 'day3')
        divert_test(summary_file, 'day1_grid_ie', grid_ie_col=2)
        divert_test(summary_file, 'day2_grid_ie', grid_ie_col=2)
        divert_test(summary_file, 'day3_grid_ie', grid_ie_col=2)
        divert_test(summary_file, 'solar-vrms', voltage_col=2)
        divert_test(summary_file, 'Energy_and_Power_Day_2020-03-22', separator=';',
                    is_kw=True, config='{"divert_decay_smoothing_factor":0.4}')
        divert_test(summary_file, 'Energy_and_Power_Day_2020-03-31', separator=';',
                    is_kw=True, config='{"divert_decay_smoothing_factor":0.4}')
        divert_test(summary_file, 'Energy_and_Power_Day_2020-04-01', separator=';',
                    is_kw=True, config='{"divert_decay_smoothing_factor":0.4}')

    return EX_OK

if __name__ == '__main__':
    # Run the script
    exit(main())
