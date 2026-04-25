"""Test load sharing simulation scenarios"""

from pytest import approx

from run_simulations import run_loadsharing_simulation


def test_loadsharing_2peer_basic():
    """Two peers sharing 32A available current with constant live power."""
    result = run_loadsharing_simulation(
        'data/scenario-2peer-basic.json',
        'loadsharing_2peer_basic',
    )
    assert not result['_supply_exceeded'], "Total demand exceeded max power budget"

    rows = result['_rows']
    assert len(rows) > 0
    last_row = rows[-1]

    assert last_row['available_a'] == approx(32.0)
    assert last_row['evse-001_allocated'] == approx(16.0)
    assert last_row['evse-002_allocated'] == approx(16.0)
    assert last_row['evse-001_actual'] == approx(16.0)
    assert last_row['evse-002_actual'] == approx(16.0)


def test_loadsharing_3peer_staggered():
    """Peers connect in sequence and allocations redistribute 32 -> 24/24 -> 16/16/16."""
    result = run_loadsharing_simulation(
        'data/scenario-3peer-staggered.json',
        'loadsharing_3peer_staggered',
    )
    assert not result['_supply_exceeded'], "Total demand exceeded max power budget"

    rows_by_time = {r['time']: r for r in result['_rows']}

    early = rows_by_time[0]
    assert early['evse-001_allocated'] == approx(32.0)
    assert early['evse-001_actual'] == approx(30.0)
    assert early['evse-002_allocated'] == approx(0.0)
    assert early['evse-003_allocated'] == approx(0.0)

    mid = rows_by_time[300]
    assert mid['evse-001_allocated'] == approx(24.0)
    assert mid['evse-002_allocated'] == approx(24.0)
    assert mid['evse-003_allocated'] == approx(0.0)

    late = rows_by_time[600]
    assert late['evse-001_allocated'] == approx(16.0)
    assert late['evse-002_allocated'] == approx(16.0)
    assert late['evse-003_allocated'] == approx(16.0)


def test_loadsharing_variable_supply():
    """Available current tracks live power feed updates."""
    result = run_loadsharing_simulation(
        'data/scenario-variable-supply.json',
        'loadsharing_variable_supply',
    )
    assert not result['_supply_exceeded'], "Total demand exceeded max power budget"

    rows_by_time = {r['time']: r for r in result['_rows']}

    assert rows_by_time[0]['available_a'] == approx(32.0)
    assert rows_by_time[0]['evse-001_allocated'] == approx(16.0)
    assert rows_by_time[0]['evse-002_allocated'] == approx(16.0)

    assert rows_by_time[1800]['available_a'] == approx(24.0)
    assert rows_by_time[1800]['evse-001_allocated'] == approx(12.0)
    assert rows_by_time[1800]['evse-002_allocated'] == approx(12.0)

    assert rows_by_time[3600]['available_a'] == approx(40.0)
    assert rows_by_time[3600]['evse-001_allocated'] == approx(20.0)
    assert rows_by_time[3600]['evse-002_allocated'] == approx(20.0)

    assert rows_by_time[5400]['available_a'] == approx(16.0)
    assert rows_by_time[5400]['evse-001_allocated'] == approx(8.0)
    assert rows_by_time[5400]['evse-002_allocated'] == approx(8.0)


def test_loadsharing_ev_limited():
    """Lower-rate EV draws less actual current than allocated."""
    result = run_loadsharing_simulation(
        'data/scenario-ev-limited.json',
        'loadsharing_ev_limited',
    )
    assert not result['_supply_exceeded'], "Total demand exceeded max power budget"

    last_row = result['_rows'][-1]

    assert last_row['evse-001_allocated'] == approx(16.0)
    assert last_row['evse-002_allocated'] == approx(16.0)

    assert last_row['evse-001_actual'] == approx(16.0)
    assert last_row['evse-002_actual'] == approx(15.0)
    assert last_row['total_actual'] < last_row['total_allocated']


def test_loadsharing_peer_offline():
    """Offline peer in safe-current mode reserves assumed current for safety."""
    result = run_loadsharing_simulation(
        'data/scenario-peer-offline.json',
        'loadsharing_peer_offline',
    )
    assert not result['_supply_exceeded'], "Total demand exceeded max power budget"

    rows_by_time = {r['time']: r for r in result['_rows']}

    assert rows_by_time[0]['evse-001_allocated'] == approx(16.0)
    assert rows_by_time[0]['evse-002_allocated'] == approx(16.0)

    r = rows_by_time[1800]
    assert r['evse-002_allocated'] == approx(0.0)
    assert r['evse-002_reason'] == 'offline'
    assert r['evse-001_allocated'] == approx(26.0)


def test_loadsharing_failsafe_disable():
    """Disable mode sets all allocations to zero when any peer is offline."""
    result = run_loadsharing_simulation(
        'data/scenario-failsafe-disable.json',
        'loadsharing_failsafe_disable',
    )
    rows_by_time = {r['time']: r for r in result['_rows']}

    assert rows_by_time[0]['evse-001_allocated'] == approx(16.0)
    assert rows_by_time[0]['evse-002_allocated'] == approx(16.0)

    r = rows_by_time[1800]
    assert r['evse-001_allocated'] == approx(0.0)
    assert r['evse-001_reason'] == 'failsafe_disabled'
    assert r['evse-002_allocated'] == approx(0.0)


def test_loadsharing_insufficient():
    """24A available with 4 peers yields 6A minimum each."""
    result = run_loadsharing_simulation(
        'data/scenario-insufficient.json',
        'loadsharing_insufficient',
    )
    assert not result['_supply_exceeded'], "Total demand exceeded max power budget"

    last_row = result['_rows'][-1]

    assert last_row['evse-001_allocated'] == approx(6.0)
    assert last_row['evse-002_allocated'] == approx(6.0)
    assert last_row['evse-003_allocated'] == approx(6.0)
    assert last_row['evse-004_allocated'] == approx(6.0)
    assert last_row['total_allocated'] == approx(24.0)


def test_loadsharing_taper():
    """High SoC tapering reduces actual charging power and caps SoC at 100%."""
    result = run_loadsharing_simulation(
        'data/scenario-ev-taper.json',
        'loadsharing_ev_taper',
    )
    assert not result['_supply_exceeded'], "Total demand exceeded max power budget"

    rows = result['_rows']
    soc_values = [r['evse-001_soc'] for r in rows]
    assert all(soc_values[i] <= soc_values[i + 1] + 1e-9 for i in range(len(soc_values) - 1))
    assert max(soc_values) <= 100.0 + 1e-9

    high_soc_rows = [r for r in rows if r['evse-001_soc'] >= 82.0 and r['evse-001_actual'] > 0]
    assert len(high_soc_rows) > 0
    assert all(r['evse-001_actual_power_w'] < 7200.0 for r in high_soc_rows)

    full_rows = [r for r in rows if r['evse-001_soc'] >= 100.0]
    assert len(full_rows) > 0
    first_full_time = full_rows[0]['time']
    after_full_rows = [r for r in rows if r['time'] > first_full_time]
    assert len(after_full_rows) > 0
    assert all(r['evse-001_actual'] == approx(0.0) for r in after_full_rows)

    peer_metrics = result['evse-001']
    assert peer_metrics['final_soc'] == approx(100.0)
    assert peer_metrics['soc_delta'] > 0


def test_loadsharing_safety_invariant():
    """Across all scenarios, total demand should not exceed max power budget."""
    scenarios = [
        ('data/scenario-2peer-basic.json', 'safety_2peer'),
        ('data/scenario-3peer-staggered.json', 'safety_3peer'),
        ('data/scenario-variable-supply.json', 'safety_variable'),
        ('data/scenario-ev-limited.json', 'safety_ev_limited'),
        ('data/scenario-peer-offline.json', 'safety_offline'),
        ('data/scenario-insufficient.json', 'safety_insufficient'),
        ('data/scenario-ev-taper.json', 'safety_taper'),
    ]
    for scenario, output in scenarios:
        result = run_loadsharing_simulation(scenario, output)
        assert not result['_supply_exceeded'], (
            f"Safety invariant violated in {scenario}: "
            f"max_total_demand_w={result['_max_total_demand_w']}"
        )
