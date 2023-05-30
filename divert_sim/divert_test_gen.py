#!/usr/bin/env python3
"""Test the divert_sim process"""

# PYTHON_ARGCOMPLETE_OK
# pylint: disable=line-too-long

from run_simulations import run_simulation, setup_summary

datasets = [
    { "id": "almostperfect", "grid_ie_col": False, "solar_col": False, "voltage_col": False, "separator": ",", "is_kw": False },
    { "id": "CloudyMorning", "grid_ie_col": False, "solar_col": False, "voltage_col": False, "separator": ",", "is_kw": False },
    { "id": "day1", "grid_ie_col": False, "solar_col": False, "voltage_col": False, "separator": ",", "is_kw": False },
    { "id": "day2", "grid_ie_col": False, "solar_col": False, "voltage_col": False, "separator": ",", "is_kw": False },
    { "id": "day3", "grid_ie_col": False, "solar_col": False, "voltage_col": False, "separator": ",", "is_kw": False },
    { "id": "day1_grid_ie", "grid_ie_col": 2, "solar_col": False, "voltage_col": False, "separator": ",", "is_kw": False },
    { "id": "day2_grid_ie", "grid_ie_col": 2, "solar_col": False, "voltage_col": False, "separator": ",", "is_kw": False },
    { "id": "day3_grid_ie", "grid_ie_col": 2, "solar_col": False, "voltage_col": False, "separator": ",", "is_kw": False },
    { "id": "solar-vrms", "grid_ie_col": False, "solar_col": False, "voltage_col": 2, "separator": ",", "is_kw": False },
    { "id": "Energy_and_Power_Day_2020-03-22", "grid_ie_col": False, "solar_col": False, "voltage_col": False, "separator": ";", "is_kw": True },
    { "id": "Energy_and_Power_Day_2020-03-31", "grid_ie_col": False, "solar_col": False, "voltage_col": False, "separator": ";", "is_kw": True },
    { "id": "Energy_and_Power_Day_2020-04-01", "grid_ie_col": False, "solar_col": False, "voltage_col": False, "separator": ";", "is_kw": True },
]

profiles = [
    "default",
    "noimport",
    "nowaste"
]

if __name__ == '__main__':
    setup_summary()

    print("")
    for profile in profiles:
        print(F"# {profile} {'profile' if profile is not 'default' else 'value' } tests")
        print("")
        for dataset in datasets:
            config = 'data/config-inputfilter-' + profile + '.json' if profile != "default" else False
            (solar_kwh,
             ev_kwh,
             kwh_from_solar,
             kwh_from_grid,
             number_of_charges,
             min_time_charging,
             max_time_charging,
             total_time_charging) = run_simulation(dataset['id'], dataset['id'] + "_" + profile, config, dataset['grid_ie_col'], dataset['solar_col'], dataset['voltage_col'], dataset['separator'], dataset['is_kw'])

            FUNC_NAME = dataset['id'].lower().replace("-", "_") + "_" + profile

            option_list = []
            if False is not dataset['grid_ie_col']:
                option_list.append(F"grid_ie_col={dataset['grid_ie_col']}")
            if False is not dataset['solar_col']:
                option_list.append(F"solar_col={dataset['solar_col']}")
            if False is not dataset['voltage_col']:
                option_list.append(F"voltage_col={dataset['voltage_col']}")
            if ',' != dataset['separator']:
                option_list.append(F"separator='{dataset['separator']}'")
            if False is not dataset['is_kw']:
                option_list.append(F"is_kw={dataset['is_kw']}")
            if 'default' != profile:
                option_list.append(F"config='data/config-inputfilter-{profile}.json'")

            HAS_OPTIONS = len(option_list) > 0

            print(F"def test_divert_{FUNC_NAME}() -> None:")
            print(F"    \"\"\"Run the divert test with the {dataset['id']} dataset with the {profile}{' profile' if profile is not 'default' else '' } values\"\"\"")
            print(F"    run_test_with_dataset('{dataset['id']}', '{dataset['id']}_{profile}',")
            print(F"                {solar_kwh}, {ev_kwh}, {kwh_from_solar}, {kwh_from_grid}, {number_of_charges}, {min_time_charging}, {max_time_charging}, {total_time_charging}{',' if HAS_OPTIONS else ')'}")
            if HAS_OPTIONS:
                print(F"                {', '.join(option_list)})")
            print("")
