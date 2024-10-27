#!/usr/bin/env python3
"""Test the app config process"""

from subprocess import PIPE, Popen
import json

CONFIG_SERVICE_EMONCMS = 1 << 0
CONFIG_SERVICE_MQTT = 1 << 1
CONFIG_SERVICE_OHM = 1 << 2
CONFIG_SERVICE_SNTP = 1 << 3
CONFIG_MQTT_PROTOCOL = 7 << 4
CONFIG_MQTT_ALLOW_ANY_CERT = 1 << 7
CONFIG_SERVICE_TESLA = 1 << 8
CONFIG_SERVICE_DIVERT = 1 << 9
CONFIG_CHARGE_MODE = 7 << 10
CONFIG_PAUSE_USES_DISABLED = 1 << 13
CONFIG_SERVICE_OCPP = 1 << 14
CONFIG_OCPP_ACCESS_SUSPEND = 1 << 15
CONFIG_OCPP_ACCESS_ENERGIZE = 1 << 16
CONFIG_VEHICLE_RANGE_MILES = 1 << 17
CONFIG_RFID = 1 << 18
CONFIG_SERVICE_CUR_SHAPER = 1 << 19
CONFIG_MQTT_RETAINED = 1 << 20
CONFIG_FACTORY_WRITE_LOCK = 1 << 21
CONFIG_OCPP_AUTO_AUTH = 1 << 22
CONFIG_OCPP_OFFLINE_AUTH = 1 << 23
CONFIG_THREEPHASE = 1 << 24
CONFIG_WIZARD = 1 << 25
CONFIG_DEFAULT_STATE = 1 << 26

def check_config(config: bool = False, load: bool = False, commit: bool = False):
    command = ["./divert_sim", "--config-check"]
    if config:
        command.append("-c")
        command.append(json.dumps(config))
    if load:
        command.append("--config-load")
    if commit:
        command.append("--config-commit")
    
    print(f"{' '.join(command)}")

    divert_process = Popen(command, stdout=PIPE, stderr=PIPE, universal_newlines=True)
    output = divert_process.communicate()

    if output[1]:
        print(output[1])

    return json.loads(output[0])

def test_config_defaults() -> None:
    """Test the default config"""
    config = check_config()
    assert config["ssid"] ==  ""
    assert config["pass"] ==  ""
    assert config["ap_ssid"] ==  ""
    assert config["ap_pass"] ==  ""
    assert config["lang"] ==  ""
    assert config["www_username"] ==  ""
    assert config["www_password"] ==  ""
    assert config["hostname"] ==  "openevse-7856"
    assert config["sntp_hostname"] ==  "pool.ntp.org"
    assert config["time_zone"] ==  "Europe/London|GMT0BST,M3.5.0/1,M10.5.0"
    assert config["limit_default_type"] ==  ""
    assert config["limit_default_value"] ==  0
    assert config["emoncms_server"] ==  "https://data.openevse.com/emoncms"
    assert config["emoncms_node"] ==  ""
    assert config["emoncms_apikey"] ==  ""
    assert config["emoncms_fingerprint"] ==  ""
    assert config["mqtt_server"] ==  "emonpi"
    assert config["mqtt_port"] ==  1883
    assert config["mqtt_topic"] ==  ""
    assert config["mqtt_user"] ==  "emonpi"
    assert config["mqtt_pass"] ==  "emonpimqtt2016"
    assert config["mqtt_solar"] ==  ""
    assert config["mqtt_grid_ie"] ==  "emon/emonpi/power1"
    assert config["mqtt_vrms"] ==  "emon/emonpi/vrms"
    assert config["mqtt_live_pwr"] ==  ""
    assert config["mqtt_vehicle_soc"] ==  ""
    assert config["mqtt_vehicle_range"] ==  ""
    assert config["mqtt_vehicle_eta"] ==  ""
    assert config["mqtt_announce_topic"] ==  "openevse/announce/7856"
    assert config["ocpp_server"] ==  ""
    assert config["ocpp_chargeBoxId"] ==  ""
    assert config["ocpp_authkey"] ==  ""
    assert config["ocpp_idtag"] ==  "DefaultIdTag"
    assert config["ohm"] ==  ""
    assert config["divert_type"] ==  -1
    assert config["divert_PV_ratio"] ==  1.1
    assert config["divert_attack_smoothing_time"] ==  20
    assert config["divert_decay_smoothing_time"] ==  600
    assert config["divert_min_charge_time"] ==  600
    assert config["current_shaper_max_pwr"] ==  0
    assert config["current_shaper_smoothing_time"] ==  60
    assert config["current_shaper_min_pause_time"] ==  300
    assert config["current_shaper_data_maxinterval"] ==  120
    assert config["vehicle_data_src"] ==  0
    assert config["tesla_access_token"] ==  ""
    assert config["tesla_refresh_token"] ==  ""
    #assert config["tesla_created_at"] ==  18446744073709552000
    #assert config["tesla_expires_in"] ==  18446744073709552000
    assert config["tesla_vehicle_id"] ==  ""
    assert config["rfid_storage"] ==  ""
    assert config["scheduler_start_window"] ==  600
    assert config["flags"] ==  79691784
    assert config["flags_changed"] ==  0
    assert config["emoncms_enabled"] ==  False
    assert config["mqtt_enabled"] ==  False
    assert config["mqtt_reject_unauthorized"] ==  True
    assert config["mqtt_retained"] ==  False
    assert config["ohm_enabled"] ==  False
    assert config["sntp_enabled"] ==  True
    assert config["tesla_enabled"] ==  False
    assert config["divert_enabled"] ==  False
    assert config["current_shaper_enabled"] ==  False
    assert config["pause_uses_disabled"] ==  False
    assert config["mqtt_vehicle_range_miles"] ==  False
    assert config["ocpp_enabled"] ==  False
    assert config["ocpp_auth_auto"] ==  True
    assert config["ocpp_auth_offline"] ==  True
    assert config["ocpp_suspend_evse"] ==  False
    assert config["ocpp_energize_plug"] ==  False
    assert config["rfid_enabled"] ==  False
    assert config["factory_write_lock"] ==  False
    assert config["is_threephase"] ==  False
    assert config["wizard_passed"] ==  False
    assert config["default_state"] ==  True
    assert config["mqtt_protocol"] ==  "mqtt"
    assert config["charge_mode"] ==  "fast"

def test_flags_changed_bits() -> None:
    """Test the flags changed bits are set correctly"""
    config = check_config({
        "mqtt_enabled": "true"
    })
    assert config["flags_changed"] == CONFIG_SERVICE_MQTT

def test_saving_and_loading() -> None:
    """Test the config is saved and loaded correctly"""
    check_config({
        "mqtt_enabled": "true"
    }, commit=True)
    config = check_config(load=True)
    assert config["mqtt_enabled"] == True

def test_flags_changed_bits() -> None:
    """Test the flags changed bits are set correctly"""
    config = check_config({
        "mqtt_enabled": "true"
    })
    assert config["flags_changed"] == CONFIG_SERVICE_MQTT

def test_default_charging_mode() -> None:
    """Test the default chage mode is set correctly"""

    # When initially added the default charge mode was set to false (disabled)
    # This test ensures that the default is now set to true (active) and that
    # the option is still able to be set to false (disabled)

    check_config(commit=True)
    config = check_config(load=True)
    assert config["default_state"] == True

    # Check the previous defaults (that were set to false) result in true
    config = check_config({
        "flags":    CONFIG_SERVICE_SNTP |
                    CONFIG_OCPP_AUTO_AUTH |
                    CONFIG_OCPP_OFFLINE_AUTH
    }, commit=True)
    assert config["default_state"] == False
    assert config["flags_changed"] == CONFIG_FACTORY_WRITE_LOCK

    config = check_config(load=True)
    assert config["default_state"] == True
    assert config["flags_changed"] == CONFIG_FACTORY_WRITE_LOCK

    # Check setting the default state to false works
    config = check_config({
        "default_state": False
    }, commit=True)
    assert config["default_state"] == False
    assert config["flags_changed"] == CONFIG_FACTORY_WRITE_LOCK | CONFIG_DEFAULT_STATE

    config = check_config(load=True)
    assert config["default_state"] == False
    assert config["flags_changed"] == CONFIG_FACTORY_WRITE_LOCK | CONFIG_DEFAULT_STATE

    # Check setting the default state to true works
    config = check_config({
        "default_state": True
    }, commit=True, load=True)
    assert config["default_state"] == True
    assert config["flags_changed"] == CONFIG_FACTORY_WRITE_LOCK | CONFIG_DEFAULT_STATE

    config = check_config(load=True)
    assert config["default_state"] == True
    assert config["flags_changed"] == CONFIG_FACTORY_WRITE_LOCK | CONFIG_DEFAULT_STATE


def test_wizard_flag():
    """Test the wizard flag is set correctly"""

    config = check_config({
        "wizard_passed": True
    }, commit=True)
    assert config["wizard_passed"] == True
    assert config["flags_changed"] == CONFIG_FACTORY_WRITE_LOCK | CONFIG_WIZARD

def test_three_phase_flag():
    """Test the three phase flag is set correctly"""

    config = check_config({
        "is_threephase": True
    })
    assert config["is_threephase"] == True
    assert config["flags_changed"] == CONFIG_THREEPHASE

    config = check_config({
        "is_threephase": True
    }, commit=True)
    assert config["is_threephase"] == True
    assert config["flags_changed"] == CONFIG_FACTORY_WRITE_LOCK | CONFIG_THREEPHASE
