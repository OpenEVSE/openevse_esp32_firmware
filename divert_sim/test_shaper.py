#!/usr/bin/env python3
"""Test the current shaper functionality"""

# PYTHON_ARGCOMPLETE_OK
# pylint: disable=line-too-long

from typing import Union
from run_simulations import run_simulation, setup_summary

def setup_module():
    """Create the output directory and summary file"""
    setup_summary('_shaper')

def run_shaper_test_with_dataset(dataset: str,
                                  output: str,
                                  expected_solar_kwh:  float,
                                  expected_ev_kwh : float,
                                  expected_kwh_from_solar : float,
                                  expected_kwh_from_grid : float,
                                  expected_number_of_charges: int,
                                  expected_min_time_charging: int,
                                  expected_max_time_charging: int,
                                  expected_total_time_charging: int,
                                  config: Union[str, bool] = False,
                                  live_power_col: int = 1,
                                  separator: str = ';') -> None:
    """Run the divert_sim process with current shaper on the given dataset and return the results"""

    ( solar_kwh,
      ev_kwh,
      kwh_from_solar,
      kwh_from_grid,
      number_of_charges,
      min_time_charging,
      max_time_charging,
      total_time_charging ) = run_simulation(dataset, output, config, False, False, False, separator, False, live_power_col)

    assert solar_kwh == expected_solar_kwh, f"Solar kWh mismatch: expected {expected_solar_kwh}, got {solar_kwh}"
    assert ev_kwh == expected_ev_kwh, f"EV kWh mismatch: expected {expected_ev_kwh}, got {ev_kwh}"
    assert kwh_from_solar == expected_kwh_from_solar, f"kWh from solar mismatch: expected {expected_kwh_from_solar}, got {kwh_from_solar}"
    assert kwh_from_grid == expected_kwh_from_grid, f"kWh from grid mismatch: expected {expected_kwh_from_grid}, got {kwh_from_grid}"
    assert number_of_charges == expected_number_of_charges, f"Number of charges mismatch: expected {expected_number_of_charges}, got {number_of_charges}"
    assert min_time_charging == expected_min_time_charging, f"Min time charging mismatch: expected {expected_min_time_charging}, got {min_time_charging}"
    assert max_time_charging == expected_max_time_charging, f"Max time charging mismatch: expected {expected_max_time_charging}, got {max_time_charging}"
    assert total_time_charging == expected_total_time_charging, f"Total time charging mismatch: expected {expected_total_time_charging}, got {total_time_charging}"


# Current Shaper Tests

def test_shaper_data_shaper_default() -> None:
    """Run the current shaper test with the data_shaper dataset with the default values"""
    run_shaper_test_with_dataset('data_shaper', 'data_shaper_default',
                                  0.0, 0.6, 0.0, 0.6, 2, 118, 410, 528,
                                  config='data/config-shaper-default.json')

def test_shaper_data_shaper_3000w() -> None:
    """Run the current shaper test with the data_shaper dataset with 3000W max power"""
    run_shaper_test_with_dataset('data_shaper', 'data_shaper_3000w',
                                  0.0, 0.23, 0.0, 0.23, 2, 16, 342, 358,
                                  config='data/config-shaper-3000w.json')

def test_shaper_data_shaper_5000w() -> None:
    """Run the current shaper test with the data_shaper dataset with 5000W max power"""
    run_shaper_test_with_dataset('data_shaper', 'data_shaper_5000w',
                                  0.0, 0.6, 0.0, 0.6, 2, 118, 410, 528,
                                  config='data/config-shaper-5000w.json')

def test_shaper_data_shaper_8000w() -> None:
    """Run the current shaper test with the data_shaper dataset with 8000W max power"""
    run_shaper_test_with_dataset('data_shaper', 'data_shaper_8000w',
                                  0.0, 1.21, 0.0, 1.21, 3, 126, 458, 812,
                                  config='data/config-shaper-8000w.json')

def test_shaper_data_shaper_10000w() -> None:
    """Run the current shaper test with the data_shaper dataset with 10000W max power"""
    run_shaper_test_with_dataset('data_shaper', 'data_shaper_10000w',
                                  0.0, 3.57, 0.0, 3.57, 10, 2, 2730, 5528,
                                  config='data/config-shaper-10000w.json')

# Test with different smoothing times
def test_shaper_data_shaper_smoothing_30s() -> None:
    """Run the current shaper test with 30 second smoothing time"""
    run_shaper_test_with_dataset('data_shaper', 'data_shaper_smoothing_30s',
                                  0.0, 0.63, 0.0, 0.63, 2, 118, 434, 552,
                                  config='data/config-shaper-smoothing_30s.json')

def test_shaper_data_shaper_smoothing_120s() -> None:
    """Run the current shaper test with 120 second smoothing time"""
    run_shaper_test_with_dataset('data_shaper', 'data_shaper_smoothing_120s',
                                  0.0, 0.53, 0.0, 0.53, 2, 118, 362, 480,
                                  config='data/config-shaper-smoothing_120s.json')

# Test with different minimum pause times
def test_shaper_data_shaper_minpause_60s() -> None:
    """Run the current shaper test with 60 second minimum pause time"""
    run_shaper_test_with_dataset('data_shaper', 'data_shaper_minpause_60s',
                                  0.0, 0.6, 0.0, 0.6, 2, 118, 410, 528,
                                  config='data/config-shaper-minpause_60s.json')

def test_shaper_data_shaper_minpause_600s() -> None:
    """Run the current shaper test with 600 second (10 min) minimum pause time"""
    run_shaper_test_with_dataset('data_shaper', 'data_shaper_minpause_600s',
                                  0.0, 0.6, 0.0, 0.6, 2, 118, 410, 528,
                                  config='data/config-shaper-minpause_600s.json')

# Test with different max data intervals
def test_shaper_data_shaper_maxinterval_30s() -> None:
    """Run the current shaper test with 30 second max data interval"""
    run_shaper_test_with_dataset('data_shaper', 'data_shaper_maxinterval_30s',
                                  0.0, 0.6, 0.0, 0.6, 2, 118, 410, 528,
                                  config='data/config-shaper-maxinterval_30s.json')

def test_shaper_data_shaper_maxinterval_120s() -> None:
    """Run the current shaper test with 120 second max data interval"""
    run_shaper_test_with_dataset('data_shaper', 'data_shaper_maxinterval_120s',
                                  0.0, 0.6, 0.0, 0.6, 2, 118, 410, 528,
                                  config='data/config-shaper-maxinterval_120s.json')

# Combined parameter tests
def test_shaper_data_shaper_aggressive() -> None:
    """Run the current shaper test with aggressive settings (low max power, fast response)"""
    run_shaper_test_with_dataset('data_shaper', 'data_shaper_aggressive',
                                  0.0, 0.27, 0.0, 0.27, 2, 16, 396, 412,
                                  config='data/config-shaper-aggressive.json')

def test_shaper_data_shaper_conservative() -> None:
    """Run the current shaper test with conservative settings (high max power, slow response)"""
    run_shaper_test_with_dataset('data_shaper', 'data_shaper_conservative',
                                  0.0, 1.1, 0.0, 1.1, 3, 20, 450, 596,
                                  config='data/config-shaper-conservative.json')

# Edge case tests
def test_shaper_data_shaper_very_low_power() -> None:
    """Run the current shaper test with very low max power (1500W - below minimum charging)"""
    run_shaper_test_with_dataset('data_shaper', 'data_shaper_very_low_power',
                                  0.0, 0.0, 0.0, 0.0, 0, 0, 0, 0,
                                  config='data/config-shaper-very_low_power.json')

def test_shaper_data_shaper_very_high_power() -> None:
    """Run the current shaper test with very high max power (20000W - effectively no limit)"""
    run_shaper_test_with_dataset('data_shaper', 'data_shaper_very_high_power',
                                  0.0, 14.72, 0.0, 14.72, 1, 0, 0, 0,
                                  config='data/config-shaper-very_high_power.json')

# Test with three-phase configuration
def test_shaper_data_shaper_threephase() -> None:
    """Run the current shaper test with three-phase configuration"""
    run_shaper_test_with_dataset('data_shaper', 'data_shaper_threephase',
                                  0.0, 0.6, 0.0, 0.6, 2, 118, 410, 528,
                                  config='data/config-shaper-threephase.json')

if __name__ == '__main__':
    import pytest
    pytest.main([__file__, '-v'])
