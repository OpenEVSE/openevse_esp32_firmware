#include "current_shaper.h"
#include "input_filter.h"

//global instance
CurrentShaperTask shaper;

CurrentShaperTask::CurrentShaperTask() : MicroTasks::Task() {
	_changed = false;
	_enabled = false;
	_max_pwr = 0;
	_live_pwr = 0;
	_smoothed_live_pwr = 0;
	_chg_cur = 0;
	_max_cur = 0;
	_pause_timer = 0;
	_timer = 0;
	_updated = false;
}

CurrentShaperTask::~CurrentShaperTask() {
	// should be useless but just in case
	evse.release(EvseClient_OpenEVSE_Shaper);
}

void CurrentShaperTask::setup() {

}

unsigned long CurrentShaperTask::loop(MicroTasks::WakeReason reason) {

	if (_enabled) {
			EvseProperties props;
			if (_changed) {
				props.setMaxCurrent(floor(_max_cur));
				if (_max_cur < evse.getMinCurrent()) {
					// pause temporary, not enough amps available
					props.setState(EvseState::Disabled);
					if (!_pause_timer)
					{
						_pause_timer = millis();
					}

				}
				else if (millis() - _pause_timer >= current_shaper_min_pause_time * 1000 && (_max_cur - evse.getMinCurrent() >= EVSE_SHAPER_HYSTERESIS))
				{
					_pause_timer = 0;
					props.setState(EvseState::None);
				}
				_timer = millis();
				_changed = false;
				// claim only if we have change
				if (evse.getState() != props.getState() || evse.getChargeCurrent() != props.getChargeCurrent())
				{
					evse.claim(EvseClient_OpenEVSE_Shaper, EvseManager_Priority_Safety, props);
					StaticJsonDocument<128> event;
					event["shaper"] = 1;
					event["shaper_live_pwr"] = _live_pwr;
					event["shaper_smoothed_live_pwr"] = _smoothed_live_pwr;
					event["shaper_max_pwr"] = _max_pwr;
					event["shaper_cur"] = _max_cur;
					event["shaper_updated"] = _updated;
					event_send(event);
				}
			}
			else if ( !_updated || millis() - _timer > current_shaper_data_maxinterval * 1000 )
			{
				//available power has not been updated since EVSE_SHAPER_FAILSAFE_TIME, pause charge
				DBUGF("shaper_live_pwr has not been updated in time, pausing charge");

				if (_updated)
				{
					_pause_timer = millis();
					_updated = false;
					_smoothed_live_pwr = _live_pwr;
				}

				if (evse.getState(EvseClient_OpenEVSE_Shaper) != EvseState::Disabled)
				{
					props.setState(EvseState::Disabled);
					evse.claim(EvseClient_OpenEVSE_Shaper, EvseManager_Priority_Limit, props);
					StaticJsonDocument<128> event;
					event["shaper"] = 1;
					event["shaper_live_pwr"] = _live_pwr;
					event["shaper_smoothed_live_pwr"] = _smoothed_live_pwr;
					event["shaper_max_pwr"] = _max_pwr;
					event["shaper_cur"] = _max_cur;
					event["shaper_updated"] = _updated;
					event_send(event);
				}
			}
	}
	else {
		//remove shaper claim
		if (_evse->clientHasClaim(EvseClient_OpenEVSE_Shaper)) {
			_evse->release(EvseClient_OpenEVSE_Shaper);
			_smoothed_live_pwr = 0;
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
	_enabled = enabled;
	_max_pwr = max_pwr;
	if (!enabled) evse.release(EvseClient_OpenEVSE_Shaper);
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
		evse.release(EvseClient_OpenEVSE_Shaper);
	}
	StaticJsonDocument<128> event;
	event["shaper"]  = state?1:0;
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
			max_pwr += solar;
		}
	}
//	if (livepwr > max_pwr) {
//		livepwr = max_pwr;
//	}
        double max_cur;
	if(!config_threephase_enabled()) {
		max_cur = ((max_pwr - livepwr) / evse.getVoltage()) + evse.getAmps();
	 }

	else {
		max_cur = ((max_pwr - livepwr) / evse.getVoltage() / 3.0) + evse.getAmps();
	}
        // Smooth shaper output to avoid instability with delayed live power samples.
        _max_cur = _outputFilter.filter(max_cur, _max_cur, current_shaper_smoothing_time);

	_changed = true;
}

int CurrentShaperTask::getMaxPwr() {
	return _max_pwr;
}
int CurrentShaperTask::getLivePwr() {
	return _live_pwr;
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