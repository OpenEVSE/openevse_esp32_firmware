#!/usr/bin/env python3
"""Test the divert_sim process"""

# PYTHON_ARGCOMPLETE_OK
# pylint: disable=line-too-long

from run_simulations import run_simulation, setup_summary

def setup():
    """Create the output directory and summary file"""
    setup_summary()

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

    if False is solar_col:
        solar_col = 1

    ( solar_kwh,
      ev_kwh,
      kwh_from_solar,
      kwh_from_grid,
      number_of_charges,
      min_time_charging,
      max_time_charging,
      total_time_charging ) = run_simulation(dataset, output, config, grid_ie_col, solar_col, voltage_col, separator, is_kw)

    assert solar_kwh == expected_solar_kwh
    assert ev_kwh == expected_ev_kwh
    assert kwh_from_solar == expected_kwh_from_solar
    assert kwh_from_grid == expected_kwh_from_grid
    assert number_of_charges == expected_number_of_charges
    assert min_time_charging == expected_min_time_charging
    assert max_time_charging == expected_max_time_charging
    assert total_time_charging == expected_total_time_charging

# default value tests

def test_divert_almostperfect_default() -> None:
    """Run the divert test with the almostperfect dataset with the default values"""
    run_test_with_dataset('almostperfect', 'almostperfect_default',
                21.08, 16.91, 16.73, 0.17, 1, 29220, 29220, 29220)

def test_divert_cloudymorning_default() -> None:
    """Run the divert test with the CloudyMorning dataset with the default values"""
    run_test_with_dataset('CloudyMorning', 'CloudyMorning_default',
                16.64, 12.93, 12.61, 0.32, 1, 21960, 21960, 21960)

def test_divert_day1_default() -> None:
    """Run the divert test with the day1 dataset with the default values"""
    run_test_with_dataset('day1', 'day1_default',
                10.12, 7.4, 6.72, 0.67, 5, 660, 9780, 13500)

def test_divert_day2_default() -> None:
    """Run the divert test with the day2 dataset with the default values"""
    run_test_with_dataset('day2', 'day2_default',
                12.35, 9.69, 9.68, 0.01, 1, 19440, 19440, 19440)

def test_divert_day3_default() -> None:
    """Run the divert test with the day3 dataset with the default values"""
    run_test_with_dataset('day3', 'day3_default',
                5.09, 1.83, 1.38, 0.45, 5, 660, 1140, 4320)

def test_divert_day1_grid_ie_default() -> None:
    """Run the divert test with the day1_grid_ie dataset with the default values"""
    run_test_with_dataset('day1_grid_ie', 'day1_grid_ie_default',
                15.13, 8.48, 8.22, 0.26, 7, 660, 7500, 18960,
                grid_ie_col=2)

def test_divert_day2_grid_ie_default() -> None:
    """Run the divert test with the day2_grid_ie dataset with the default values"""
    run_test_with_dataset('day2_grid_ie', 'day2_grid_ie_default',
                10.85, 8.09, 6.43, 1.65, 12, 540, 7800, 17220,
                grid_ie_col=2)

def test_divert_day3_grid_ie_default() -> None:
    """Run the divert test with the day3_grid_ie dataset with the default values"""
    run_test_with_dataset('day3_grid_ie', 'day3_grid_ie_default',
                12.13, 6.06, 6.01, 0.05, 2, 3000, 9840, 12840,
                grid_ie_col=2)

def test_divert_solar_vrms_default() -> None:
    """Run the divert test with the solar-vrms dataset with the default values"""
    run_test_with_dataset('solar-vrms', 'solar-vrms_default',
                13.85, 11.68, 11.62, 0.06, 1, 20640, 20640, 20640,
                voltage_col=2)

def test_divert_energy_and_power_day_2020_03_22_default() -> None:
    """Run the divert test with the Energy_and_Power_Day_2020-03-22 dataset with the default values"""
    run_test_with_dataset('Energy_and_Power_Day_2020-03-22', 'Energy_and_Power_Day_2020-03-22_default',
                41.87, 38.58, 38.51, 0.07, 1, 28800, 28800, 28800,
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
                16.64, 10.76, 10.62, 0.14, 5, 660, 9060, 16980,
                config='data/config-inputfilter-noimport.json')

def test_divert_day1_noimport() -> None:
    """Run the divert test with the day1 dataset with the noimport profile values"""
    run_test_with_dataset('day1', 'day1_noimport',
                10.12, 4.96, 4.71, 0.25, 6, 660, 2460, 7800,
                config='data/config-inputfilter-noimport.json')

def test_divert_day2_noimport() -> None:
    """Run the divert test with the day2 dataset with the noimport profile values"""
    run_test_with_dataset('day2', 'day2_noimport',
                12.35, 9.14, 9.14, 0.0, 1, 18120, 18120, 18120,
                config='data/config-inputfilter-noimport.json')

def test_divert_day3_noimport() -> None:
    """Run the divert test with the day3 dataset with the noimport profile values"""
    run_test_with_dataset('day3', 'day3_noimport',
                5.09, 0.04, 0.04, 0, 1, 120, 120, 120,
                config='data/config-inputfilter-noimport.json')

def test_divert_day1_grid_ie_noimport() -> None:
    """Run the divert test with the day1_grid_ie dataset with the noimport profile values"""
    run_test_with_dataset('day1_grid_ie', 'day1_grid_ie_noimport',
                15.13, 4.88, 4.86, 0.02, 7, 60, 2640, 10440,
                grid_ie_col=2, config='data/config-inputfilter-noimport.json')

def test_divert_day2_grid_ie_noimport() -> None:
    """Run the divert test with the day2_grid_ie dataset with the noimport profile values"""
    run_test_with_dataset('day2_grid_ie', 'day2_grid_ie_noimport',
                10.85, 1.74, 1.65, 0.09, 3, 660, 2040, 3360,
                grid_ie_col=2, config='data/config-inputfilter-noimport.json')

def test_divert_day3_grid_ie_noimport() -> None:
    """Run the divert test with the day3_grid_ie dataset with the noimport profile values"""
    run_test_with_dataset('day3_grid_ie', 'day3_grid_ie_noimport',
                12.13, 3.97, 3.93, 0.04, 7, 660, 2700, 7800,
                grid_ie_col=2, config='data/config-inputfilter-noimport.json')

def test_divert_solar_vrms_noimport() -> None:
    """Run the divert test with the solar-vrms dataset with the noimport profile values"""
    run_test_with_dataset('solar-vrms', 'solar-vrms_noimport',
                13.85, 11.29, 11.25, 0.04, 1, 19800, 19800, 19800,
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
                21.08, 21.32, 19.9, 1.42, 1, 37500, 37500, 37500,
                config='data/config-inputfilter-nowaste.json')

def test_divert_cloudymorning_nowaste() -> None:
    """Run the divert test with the CloudyMorning dataset with the nowaste profile values"""
    run_test_with_dataset('CloudyMorning', 'CloudyMorning_nowaste',
                16.64, 15.44, 14.26, 1.18, 1, 26820, 26820, 26820,
                config='data/config-inputfilter-nowaste.json')

def test_divert_day1_nowaste() -> None:
    """Run the divert test with the day1 dataset with the nowaste profile values"""
    run_test_with_dataset('day1', 'day1_nowaste',
                10.12, 11.65, 9.14, 2.51, 3, 720, 17280, 23340,
                config='data/config-inputfilter-nowaste.json')

def test_divert_day2_nowaste() -> None:
    """Run the divert test with the day2 dataset with the nowaste profile values"""
    run_test_with_dataset('day2', 'day2_nowaste',
                12.35, 12.45, 11.66, 0.78, 1, 24540, 24540, 24540,
                config='data/config-inputfilter-nowaste.json')

def test_divert_day3_nowaste() -> None:
    """Run the divert test with the day3 dataset with the nowaste profile values"""
    run_test_with_dataset('day3', 'day3_nowaste',
                5.09, 4.97, 3.24, 1.73, 3, 660, 8760, 12000,
                config='data/config-inputfilter-nowaste.json')

def test_divert_day1_grid_ie_nowaste() -> None:
    """Run the divert test with the day1_grid_ie dataset with the nowaste profile values"""
    run_test_with_dataset('day1_grid_ie', 'day1_grid_ie_nowaste',
                15.13, 14.21, 13.01, 1.20, 4, 1860, 14880, 30900,
                grid_ie_col=2, config='data/config-inputfilter-nowaste.json')

def test_divert_day2_grid_ie_nowaste() -> None:
    """Run the divert test with the day2_grid_ie dataset with the nowaste profile values"""
    run_test_with_dataset('day2_grid_ie', 'day2_grid_ie_nowaste',
                10.85, 11.53, 8.60, 2.93, 7, 540, 14700, 24600,
                grid_ie_col=2, config='data/config-inputfilter-nowaste.json')

def test_divert_day3_grid_ie_nowaste() -> None:
    """Run the divert test with the day3_grid_ie dataset with the nowaste profile values"""
    run_test_with_dataset('day3_grid_ie', 'day3_grid_ie_nowaste',
                12.13, 8.65, 8.12, 0.53, 3, 1500, 14640, 17700,
                grid_ie_col=2, config='data/config-inputfilter-nowaste.json')

def test_divert_solar_vrms_nowaste() -> None:
    """Run the divert test with the solar-vrms dataset with the nowaste profile values"""
    run_test_with_dataset('solar-vrms', 'solar-vrms_nowaste',
                13.85, 14.22, 13.18, 1.04, 1, 24960, 24960, 24960,
                voltage_col=2, config='data/config-inputfilter-nowaste.json')

def test_divert_energy_and_power_day_2020_03_22_nowaste() -> None:
    """Run the divert test with the Energy_and_Power_Day_2020-03-22 dataset with the nowaste profile values"""
    run_test_with_dataset('Energy_and_Power_Day_2020-03-22', 'Energy_and_Power_Day_2020-03-22_nowaste',
                41.87, 41.34, 40.81, 0.53, 1, 33300, 33300, 33300,
                separator=';', is_kw=True, config='data/config-inputfilter-nowaste.json')

def test_divert_energy_and_power_day_2020_03_31_nowaste() -> None:
    """Run the divert test with the Energy_and_Power_Day_2020-03-31 dataset with the nowaste profile values"""
    run_test_with_dataset('Energy_and_Power_Day_2020-03-31', 'Energy_and_Power_Day_2020-03-31_nowaste',
                23.91, 23.82, 22.38, 1.44, 2, 4500, 27900, 32400,
                separator=';', is_kw=True, config='data/config-inputfilter-nowaste.json')

def test_divert_energy_and_power_day_2020_04_01_nowaste() -> None:
    """Run the divert test with the Energy_and_Power_Day_2020-04-01 dataset with the nowaste profile values"""
    run_test_with_dataset('Energy_and_Power_Day_2020-04-01', 'Energy_and_Power_Day_2020-04-01_nowaste',
                38.89, 38.16, 37.61, 0.55, 1, 28800, 28800, 28800,
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
