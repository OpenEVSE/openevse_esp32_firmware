#!/usr/bin/env python3
"""Test the divert_sim process"""

# PYTHON_ARGCOMPLETE_OK
# pylint: disable=line-too-long

from os import path
import os
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

KWH_ROUNDING = 2

def setup():
    """Create the output directory"""
    print("Setting up test environment")
    if not path.exists('output'):
        os.mkdir('output')
    with open(path.join('output', 'summary.csv'), 'w', encoding="utf-8") as summary_file:
        summary_file.write('"Dataset","Config","Total Solar (kWh)","Total EV Charge (kWh)","Charge from solar (kWh)","Charge from grid (kWh)","Number of charges","Min time charging","Max time charging","Total time charging"\n')

def run_test_with_dataset(dataset: str,
                output: str,
                expected_solar_kwh:  float,
                expected_ev_kwh : float,
                expected_kwh_from_solar : float,
                expected_kwh_from_grid : float,
                expected_number_of_charges: int,
                expected_min_time_charging: int,
                expected_max_time_charging: int,
                expected_total_time_charging: int,
                config: bool = False, grid_ie_col: bool = False,
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

        if config is False:
            config = "Default"

        with open(path.join('output', 'summary.csv'), 'a', encoding="utf-8") as summary_file:
            summary_file.write(f'"{dataset}","{config}",{solar_kwh},{ev_kwh},{kwh_from_solar},{kwh_from_grid},{number_of_charges},{min_time_charging},{max_time_charging},{total_time_charging}\n')

        assert round(solar_kwh, KWH_ROUNDING) == expected_solar_kwh
        assert round(ev_kwh, KWH_ROUNDING) == expected_ev_kwh
        assert round(kwh_from_solar, KWH_ROUNDING) == expected_kwh_from_solar
        assert round(kwh_from_grid, KWH_ROUNDING) == expected_kwh_from_grid
        assert number_of_charges == expected_number_of_charges
        assert min_time_charging == expected_min_time_charging
        assert max_time_charging == expected_max_time_charging
        assert total_time_charging == expected_total_time_charging

# default value tests

def test_divert_almostperfect_default() -> None:
    """Run the divert test with the almostperfect dataset with the default values"""
    run_test_with_dataset('almostperfect', 'almostperfect_default',
                21.08, 16.67, 16.6, 0.06, 6, 180, 20700, 28620)

def test_divert_cloudymorning_default() -> None:
    """Run the divert test with the CloudyMorning dataset with the default values"""
    run_test_with_dataset('CloudyMorning', 'CloudyMorning_default',
                16.64, 12.28, 12.07, 0.22, 7, 300, 14520, 20340)

def test_divert_day1_default() -> None:
    """Run the divert test with the day1 dataset with the default values"""
    run_test_with_dataset('day1', 'day1_default',
                10.12, 7.11, 6.51, 0.59, 7, 660, 8400, 12840)

def test_divert_day2_default() -> None:
    """Run the divert test with the day2 dataset with the default values"""
    run_test_with_dataset('day2', 'day2_default',
                12.35, 9.14, 9.14, 0.0, 1, 18060, 18060, 18060)

def test_divert_day3_default() -> None:
    """Run the divert test with the day3 dataset with the default values"""
    run_test_with_dataset('day3', 'day3_default',
                5.09, 1.66, 1.22, 0.44, 7, 60, 840, 3600)

def test_divert_day1_grid_ie_default() -> None:
    """Run the divert test with the day1_grid_ie dataset with the default values"""
    run_test_with_dataset('day1_grid_ie', 'day1_grid_ie_default',
                15.13, 7.84, 7.66, 0.18, 10, 660, 6300, 17280,
                grid_ie_col=2)

def test_divert_day2_grid_ie_default() -> None:
    """Run the divert test with the day2_grid_ie dataset with the default values"""
    run_test_with_dataset('day2_grid_ie', 'day2_grid_ie_default',
                10.85, 7.00, 5.87, 1.13, 21, 60, 2640, 14460,
                grid_ie_col=2)

def test_divert_day3_grid_ie_default() -> None:
    """Run the divert test with the day3_grid_ie dataset with the default values"""
    run_test_with_dataset('day3_grid_ie', 'day3_grid_ie_default',
                12.13, 5.39, 5.37, 0.02, 7, 60, 4320, 11160,
                grid_ie_col=2)

def test_divert_solar_vrms_default() -> None:
    """Run the divert test with the solar-vrms dataset with the default values"""
    run_test_with_dataset('solar-vrms', 'solar-vrms_default',
                13.85, 11.18, 11.14, 0.04, 1, 19440, 19440, 19440,
                voltage_col=2)

def test_divert_energy_and_power_day_2020_03_22_default() -> None:
    """Run the divert test with the Energy_and_Power_Day_2020-03-22 dataset with the default values"""
    run_test_with_dataset('Energy_and_Power_Day_2020-03-22', 'Energy_and_Power_Day_2020-03-22_default',
                41.87, 38.16, 38.16, 0.0, 1, 27900, 27900, 27900,
                separator=';', is_kw=True)

def test_divert_energy_and_power_day_2020_03_31_default() -> None:
    """Run the divert test with the Energy_and_Power_Day_2020-03-31 dataset with the default values"""
    run_test_with_dataset('Energy_and_Power_Day_2020-03-31', 'Energy_and_Power_Day_2020-03-31_default',
                23.91, 18.42, 18.42, 0.0, 2, 900, 20700, 21600,
                separator=';', is_kw=True)

def test_divert_energy_and_power_day_2020_04_01_default() -> None:
    """Run the divert test with the Energy_and_Power_Day_2020-04-01 dataset with the default values"""
    run_test_with_dataset('Energy_and_Power_Day_2020-04-01', 'Energy_and_Power_Day_2020-04-01_default',
                38.89, 36.42, 36.42, 0.0, 1, 26100, 26100, 26100,
                separator=';', is_kw=True)

# noimport profile tests

def test_divert_almostperfect_noimport() -> None:
    """Run the divert test with the almostperfect dataset with the noimport profile values"""
    run_test_with_dataset('almostperfect', 'almostperfect_noimport',
                21.08, 15.66, 15.57, 0.09, 5, 660, 20400, 26400,
                config='data/config-inputfilter-noimport.json')

def test_divert_cloudymorning_noimport() -> None:
    """Run the divert test with the CloudyMorning dataset with the noimport profile values"""
    run_test_with_dataset('CloudyMorning', 'CloudyMorning_noimport',
                16.64, 10.52, 10.4, 0.12, 6, 660, 9060, 16440,
                config='data/config-inputfilter-noimport.json')

def test_divert_day1_noimport() -> None:
    """Run the divert test with the day1 dataset with the noimport profile values"""
    run_test_with_dataset('day1', 'day1_noimport',
                10.12, 4.96, 4.71, 0.25, 6, 660, 2460, 7800,
                config='data/config-inputfilter-noimport.json')

def test_divert_day2_noimport() -> None:
    """Run the divert test with the day2 dataset with the noimport profile values"""
    run_test_with_dataset('day2', 'day2_noimport',
                12.35, 8.85, 8.85, 0.0, 1, 17400, 17400, 17400,
                config='data/config-inputfilter-noimport.json')

def test_divert_day3_noimport() -> None:
    """Run the divert test with the day3 dataset with the noimport profile values"""
    run_test_with_dataset('day3', 'day3_noimport',
                5.09, 0.04, 0.04, 0, 1, 120, 120, 120,
                config='data/config-inputfilter-noimport.json')

def test_divert_day1_grid_ie_noimport() -> None:
    """Run the divert test with the day1_grid_ie dataset with the noimport profile values"""
    run_test_with_dataset('day1_grid_ie', 'day1_grid_ie_noimport',
                15.13, 4.95, 4.91, 0.04, 9, 60, 2580, 10560,
                grid_ie_col=2, config='data/config-inputfilter-noimport.json')

def test_divert_day2_grid_ie_noimport() -> None:
    """Run the divert test with the day2_grid_ie dataset with the noimport profile values"""
    run_test_with_dataset('day2_grid_ie', 'day2_grid_ie_noimport',
                10.85, 1.74, 1.65, 0.09, 3, 660, 2040, 3360,
                grid_ie_col=2, config='data/config-inputfilter-noimport.json')

def test_divert_day3_grid_ie_noimport() -> None:
    """Run the divert test with the day3_grid_ie dataset with the noimport profile values"""
    run_test_with_dataset('day3_grid_ie', 'day3_grid_ie_noimport',
                12.13, 3.9, 3.86, 0.04, 7, 660, 2700, 7620,
                grid_ie_col=2, config='data/config-inputfilter-noimport.json')

def test_divert_solar_vrms_noimport() -> None:
    """Run the divert test with the solar-vrms dataset with the noimport profile values"""
    run_test_with_dataset('solar-vrms', 'solar-vrms_noimport',
                13.85, 10.94, 10.9, 0.04, 1, 18960, 18960, 18960,
                voltage_col=2, config='data/config-inputfilter-noimport.json')

def test_divert_energy_and_power_day_2020_03_22_noimport() -> None:
    """Run the divert test with the Energy_and_Power_Day_2020-03-22 dataset with the noimport profile values"""
    run_test_with_dataset('Energy_and_Power_Day_2020-03-22', 'Energy_and_Power_Day_2020-03-22_noimport',
                41.87, 38.16, 38.16, 0.0, 1, 27900, 27900, 27900,
                separator=';', is_kw=True, config='data/config-inputfilter-noimport.json')

def test_divert_energy_and_power_day_2020_03_31_noimport() -> None:
    """Run the divert test with the Energy_and_Power_Day_2020-03-31 dataset with the noimport profile values"""
    run_test_with_dataset('Energy_and_Power_Day_2020-03-31', 'Energy_and_Power_Day_2020-03-31_noimport',
                23.91, 18.06, 18.06, 0.0, 1, 20700, 20700, 20700,
                separator=';', is_kw=True, config='data/config-inputfilter-noimport.json')

def test_divert_energy_and_power_day_2020_04_01_noimport() -> None:
    """Run the divert test with the Energy_and_Power_Day_2020-04-01 dataset with the noimport profile values"""
    run_test_with_dataset('Energy_and_Power_Day_2020-04-01', 'Energy_and_Power_Day_2020-04-01_noimport',
                38.89, 36.42, 36.42, 0.0, 1, 26100, 26100, 26100,
                separator=';', is_kw=True, config='data/config-inputfilter-noimport.json')

# nowaste profile tests

def test_divert_almostperfect_nowaste() -> None:
    """Run the divert test with the almostperfect dataset with the nowaste profile values"""
    run_test_with_dataset('almostperfect', 'almostperfect_nowaste',
                21.08, 16.67, 16.6, 0.06, 6, 180, 20700, 28620,
                config='data/config-inputfilter-nowaste.json')

def test_divert_cloudymorning_nowaste() -> None:
    """Run the divert test with the CloudyMorning dataset with the nowaste profile values"""
    run_test_with_dataset('CloudyMorning', 'CloudyMorning_nowaste',
                16.64, 12.28, 12.07, 0.22, 7, 300, 14520, 20340,
                config='data/config-inputfilter-nowaste.json')

def test_divert_day1_nowaste() -> None:
    """Run the divert test with the day1 dataset with the nowaste profile values"""
    run_test_with_dataset('day1', 'day1_nowaste',
                10.12, 7.11, 6.51, 0.59, 7, 660, 8400, 12840,
                config='data/config-inputfilter-nowaste.json')

def test_divert_day2_nowaste() -> None:
    """Run the divert test with the day2 dataset with the nowaste profile values"""
    run_test_with_dataset('day2', 'day2_nowaste',
                12.35, 9.14, 9.14, 0.0, 1, 18060, 18060, 18060,
                config='data/config-inputfilter-nowaste.json')

def test_divert_day3_nowaste() -> None:
    """Run the divert test with the day3 dataset with the nowaste profile values"""
    run_test_with_dataset('day3', 'day3_nowaste',
                5.09, 1.66, 1.22, 0.44, 7, 60, 840, 3600,
                config='data/config-inputfilter-nowaste.json')

def test_divert_day1_grid_ie_nowaste() -> None:
    """Run the divert test with the day1_grid_ie dataset with the nowaste profile values"""
    run_test_with_dataset('day1_grid_ie', 'day1_grid_ie_nowaste',
                15.13, 7.84, 7.66, 0.18, 10, 660, 6300, 17280,
                grid_ie_col=2, config='data/config-inputfilter-nowaste.json')

def test_divert_day2_grid_ie_nowaste() -> None:
    """Run the divert test with the day2_grid_ie dataset with the nowaste profile values"""
    run_test_with_dataset('day2_grid_ie', 'day2_grid_ie_nowaste',
                10.85, 7.00, 5.87, 1.13, 21, 60, 2640, 14460,
                grid_ie_col=2, config='data/config-inputfilter-nowaste.json')

def test_divert_day3_grid_ie_nowaste() -> None:
    """Run the divert test with the day3_grid_ie dataset with the nowaste profile values"""
    run_test_with_dataset('day3_grid_ie', 'day3_grid_ie_nowaste',
                12.13, 5.39, 5.37, 0.02, 7, 60, 4320, 11160,
                grid_ie_col=2, config='data/config-inputfilter-nowaste.json')

def test_divert_solar_vrms_nowaste() -> None:
    """Run the divert test with the solar-vrms dataset with the nowaste profile values"""
    run_test_with_dataset('solar-vrms', 'solar-vrms_nowaste',
                13.85, 11.18, 11.14, 0.04, 1, 19440, 19440, 19440,
                voltage_col=2, config='data/config-inputfilter-nowaste.json')

def test_divert_energy_and_power_day_2020_03_22_nowaste() -> None:
    """Run the divert test with the Energy_and_Power_Day_2020-03-22 dataset with the nowaste profile values"""
    run_test_with_dataset('Energy_and_Power_Day_2020-03-22', 'Energy_and_Power_Day_2020-03-22_nowaste',
                41.87, 38.16, 38.16, 0.0, 1, 27900, 27900, 27900,
                separator=';', is_kw=True, config='data/config-inputfilter-nowaste.json')

def test_divert_energy_and_power_day_2020_03_31_nowaste() -> None:
    """Run the divert test with the Energy_and_Power_Day_2020-03-31 dataset with the nowaste profile values"""
    run_test_with_dataset('Energy_and_Power_Day_2020-03-31', 'Energy_and_Power_Day_2020-03-31_nowaste',
                23.91, 18.42, 18.42, 0.0, 2, 900, 20700, 21600,
                separator=';', is_kw=True, config='data/config-inputfilter-nowaste.json')

def test_divert_energy_and_power_day_2020_04_01_nowaste() -> None:
    """Run the divert test with the Energy_and_Power_Day_2020-04-01 dataset with the nowaste profile values"""
    run_test_with_dataset('Energy_and_Power_Day_2020-04-01', 'Energy_and_Power_Day_2020-04-01_nowaste',
                38.89, 36.42, 36.42, 0.0, 1, 26100, 26100, 26100,
                separator=';', is_kw=True, config='data/config-inputfilter-nowaste.json')

if __name__ == '__main__':
    # Run the script
    test_divert_almostperfect_default()
    test_divert_cloudymorning_default()
    test_divert_day1_default()
    test_divert_day2_default()
    test_divert_day3_default()
    test_divert_day1_grid_ie_default()
    test_divert_day2_grid_ie_default()
    test_divert_day3_grid_ie_default()
    test_divert_solar_vrms_default()
    test_divert_energy_and_power_day_2020_03_22_default()
    test_divert_energy_and_power_day_2020_03_31_default()
    test_divert_energy_and_power_day_2020_04_01_default()
