#include "current_shaper.h"
#include "input_filter.h"

//global instance
CurrentShaperTask shaper;

CurrentShaperTask::CurrentShaperTask() : MicroTasks::Task() {
	_changed = false;
	_enabled = false;
	_timer_controlled = false;
	_max_pwr = 0;
	_live_pwr = 0;
	_smoothed_live_pwr = 0;
	_chg_cur = 0;
	_max_cur = 0;
	_pause_timer = 0;
	_timer = 0;
	_updated = false;
	_loadshare_limit_active = false;
	_loadshare_max_cur = 0;
	_loadshare_force_disabled = false;
}

CurrentShaperTask::~CurrentShaperTask() {
	// should be useless but just in case
	if (_evse) {
		_evse->release(EvseClient_OpenEVSE_Shaper);
	}
}

// Publish the shaper's live status over websocket / MQTT.
//
// The document must hold all six keys. The previous hardcoded 128-byte
// document was too small once the event grew to six fields: ArduinoJson
// silently drops the key/value pairs that don't fit (on the 64-bit host
// builds used by divert_sim, six slots alone need 192 bytes), so
// shaper_cur and shaper_updated never reached the status feed. 256 bytes
// covers six slots on both the 32-bit ESP32 (16-byte slots) and 64-bit
// hosts (32-byte slots), with keys stored as linked pointers.
void CurrentShaperTask::publishShaperEvent()
{
	StaticJsonDocument<256> event;
	event["shaper"] = 1;
	event["shaper_live_pwr"] = _live_pwr;
	event["shaper_smoothed_live_pwr"] = _smoothed_live_pwr;
	event["shaper_max_pwr"] = _max_pwr;
	event["shaper_cur"] = _max_cur;
	event["shaper_updated"] = _updated;
	event_send(event);
}

void CurrentShaperTask::setup() {

}

unsigned long CurrentShaperTask::loop(MicroTasks::WakeReason reason) {

	if (_enabled) {
			EvseProperties props;
			if (_changed) {
				double effective_max_cur = _max_cur;
				if (_loadshare_limit_active && _loadshare_max_cur < effective_max_cur) {
					effective_max_cur = _loadshare_max_cur;
				}
				props.setMaxCurrent(floor(effective_max_cur));
				if (_loadshare_force_disabled || effective_max_cur < _evse->getMinCurrent()) {
					// pause temporary, not enough amps available
					props.setState(EvseState::Disabled);
					if (!_pause_timer)
					{
						_pause_timer = millis();
					}

				}
				else if (millis() - _pause_timer >= current_shaper_min_pause_time * 1000 && (effective_max_cur - _evse->getMinCurrent() >= EVSE_SHAPER_HYSTERESIS))
				{
					_pause_timer = 0;
					props.setState(EvseState::None);
				}
				_timer = millis();
				_changed = false;
				// claim only if we have change
				if (_evse->getState(EvseClient_OpenEVSE_Shaper) != props.getState() ||
					_evse->getMaxCurrent(EvseClient_OpenEVSE_Shaper) != props.getMaxCurrent())
				{
					// Always-on shaper claims at Safety (5000); a shaper running *only*
					// because of a timer window claims at TimerFeature (900) so a
					// manual override can still beat it.  A window must never demote a
					// config-enabled (always-on) shaper below Safety, and neither must
					// it demote a load sharing limit.
					int priority = (_timer_controlled && !config_current_shaper_enabled() &&
					                !_loadshare_limit_active)
					               ? EvseManager_Priority_TimerFeature : EvseManager_Priority_Safety;
					_evse->claim(EvseClient_OpenEVSE_Shaper, priority, props);
				}
				// Publish the live status on every data update, like the divert task
				// does. Gating this on the claim change above starved the status feed
				// (websocket + MQTT) whenever the house load was stable: the
				// shaper_live_pwr topic then stayed quiet for minutes, which surfaces
				// as "unavailable" in consumers with a data expiry (e.g. Home
				// Assistant) and as a frozen readout in the web UI.
				publishShaperEvent();
			}
			else if ( !_updated || millis() - _timer > current_shaper_data_maxinterval * 1000 )
			{
				//available power has not been updated since EVSE_SHAPER_FAILSAFE_TIME, pause charge
				DBUGF("shaper_live_pwr has not been updated in time, pausing charge");

				bool wentStale = false;
				if (_updated)
				{
					_pause_timer = millis();
					_updated = false;
					_smoothed_live_pwr = _live_pwr;
					wentStale = true;
				}

				bool claimed = false;
				if (_evse->getState(EvseClient_OpenEVSE_Shaper) != EvseState::Disabled)
				{
					props.setState(EvseState::Disabled);
					// Stale-data failsafe: overridable by Manual only when the shaper
					// runs purely from a timer window, as above.
					int failsafe_priority = (_timer_controlled && !config_current_shaper_enabled())
					               ? EvseManager_Priority_TimerFeature : EvseManager_Priority_Limit;
					_evse->claim(EvseClient_OpenEVSE_Shaper, failsafe_priority, props);
					claimed = true;
				}

				// Announce the stale transition even when the claim was already
				// Disabled (e.g. paused for insufficient current), so consumers see
				// shaper_updated=false instead of the last "updating" values.
				if (wentStale || claimed)
				{
					publishShaperEvent();
				}
			}
	}
	else {
		if (_loadshare_limit_active) {
			EvseProperties props;
			props.setMaxCurrent(floor(_loadshare_max_cur));
			props.setState((_loadshare_force_disabled ||
			                _loadshare_max_cur < _evse->getMinCurrent())
			                 ? EvseState::Disabled : EvseState::None);

			if (_changed ||
				_evse->getState(EvseClient_OpenEVSE_Shaper) != props.getState() ||
				_evse->getMaxCurrent(EvseClient_OpenEVSE_Shaper) != props.getMaxCurrent()) {
				_evse->claim(EvseClient_OpenEVSE_Shaper, EvseManager_Priority_Safety, props);
				_changed = false;
			}
		} else {
			//remove shaper claim
			if (_evse->clientHasClaim(EvseClient_OpenEVSE_Shaper)) {
				_evse->release(EvseClient_OpenEVSE_Shaper);
				_smoothed_live_pwr = 0;
			}
		}
	}

	return EVSE_SHAPER_LOOP_TIME;
}

void CurrentShaperTask::begin(EvseManager &evse) {
	this -> _timer   = millis();
	this -> _enabled = config_current_shaper_enabled();
	this -> _evse    = &evse;
	this -> _max_pwr = current_shaper_max_pwr;
	this -> _live_pwr = 0;
	this -> _smoothed_live_pwr = 0;
	this -> _max_cur = 0;
	this -> _updated = false;
	MicroTask.startTask(this);
	StaticJsonDocument<128> event;
	event["shaper"]  = 1;
	event_send(event);
}

void CurrentShaperTask::notifyConfigChanged( bool enabled, uint32_t max_pwr) {
	DBUGF("CurrentShaper: got config changed");
	// A timer window overrides the config setting; don't cancel it mid-window.
	_enabled = enabled || _timer_controlled;
	_max_pwr = max_pwr;
	if (!_enabled && _evse) _evse->release(EvseClient_OpenEVSE_Shaper);
	StaticJsonDocument<128> event;
	event["shaper"] = enabled == true ? 1 : 0;
	event["shaper_max_pwr"] = max_pwr;
	event_send(event);
}

void CurrentShaperTask::setMaxPwr(int max_pwr) {
		_max_pwr = max_pwr;
		shapeCurrent();
}

void CurrentShaperTask::setLivePwr(int live_pwr) {
	_live_pwr = live_pwr;
	shapeCurrent();
}

// temporary change Current Shaper state without changing configuration
void CurrentShaperTask::setState(bool state) {
	_enabled = state;
	if (!_enabled) {
		//remove claim
		if (_evse) {
			_evse->release(EvseClient_OpenEVSE_Shaper);
		}
	}
	StaticJsonDocument<128> event;
	event["shaper"]  = state?1:0;
	event_send(event);
}

// Enable shaper from a scheduler timer window (priority 900 instead of 5000)
void CurrentShaperTask::setTimerEnabled(bool active) {
	_timer_controlled = active;
	_enabled = active ? true : config_current_shaper_enabled();
	if (!_enabled && _evse) {
		_evse->release(EvseClient_OpenEVSE_Shaper);
	}
	StaticJsonDocument<128> event;
	event["shaper"] = _enabled ? 1 : 0;
	event_send(event);
}

void CurrentShaperTask::shapeCurrent() {
	_updated = true;
	// adding self produced energy to total
	int max_pwr = _max_pwr;

	int livepwr;
	DBUGVAR(_pause_timer);
	if (_pause_timer == 0) {
		_smoothed_live_pwr = _live_pwr;
		livepwr = _live_pwr;
	}
	else {
		if (_live_pwr > _smoothed_live_pwr) {
			_smoothed_live_pwr = _live_pwr;
		}
		else {
			_smoothed_live_pwr = _inputFilter.filter(_live_pwr, _smoothed_live_pwr, current_shaper_smoothing_time);
		}
		livepwr = _smoothed_live_pwr;
	}

	if (config_divert_enabled() == true) {
		if ( divert_type == DIVERT_TYPE_SOLAR ) {
			max_pwr += divert.getSolar();
		}
	}
//	if (livepwr > max_pwr) {
//		livepwr = max_pwr;
//	}
	if(!config_threephase_enabled()) {
		_max_cur = ((max_pwr - livepwr) / _evse->getVoltage()) + _evse->getAmps();
	 }

	else {
		_max_cur = ((max_pwr - livepwr) / _evse->getVoltage() / 3.0) + _evse->getAmps();
	}



	_changed = true;
}

int CurrentShaperTask::getMaxPwr() {
	return _max_pwr;
}
int CurrentShaperTask::getLivePwr() {
	return _live_pwr;
}
int CurrentShaperTask::getSmoothedLivePwr() {
	return _smoothed_live_pwr;
}

double CurrentShaperTask::getMaxCur() {
	return _max_cur;
}
bool CurrentShaperTask::getState() {
	return _enabled;
}

bool CurrentShaperTask::isActive() {
	return _evse->clientHasClaim(EvseClient_OpenEVSE_Shaper);
}

bool CurrentShaperTask::isUpdated() {
	return _updated;
}

void CurrentShaperTask::setLoadSharingLimit(double max_cur, bool force_disabled) {
	if (max_cur < 0) {
		max_cur = 0;
	}
	_loadshare_limit_active = true;
	_loadshare_max_cur = max_cur;
	_loadshare_force_disabled = force_disabled;
	_changed = true;
}

void CurrentShaperTask::clearLoadSharingLimit() {
	_loadshare_limit_active = false;
	_loadshare_max_cur = 0;
	_loadshare_force_disabled = false;
	_changed = true;
}

bool CurrentShaperTask::hasLoadSharingLimit() {
	return _loadshare_limit_active;
}
