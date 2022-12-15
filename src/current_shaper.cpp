#include "current_shaper.h"

//global instance
CurrentShaperTask shaper;

CurrentShaperTask::CurrentShaperTask() : MicroTasks::Task() {
	_changed = false;
	_enabled = false;
}

CurrentShaperTask::~CurrentShaperTask() {
	// should be useless but just in case
	evse.release(EvseClient_OpenEVSE_Shaper);
}

void CurrentShaperTask::setup() {

}

unsigned long CurrentShaperTask::loop(MicroTasks::WakeReason reason) {
	if (_enabled && !_evse->clientHasClaim(EvseClient_OpenEVSE_Divert)) {
			EvseProperties props;
			if (_changed) {
				props.setMaxCurrent(_max_cur);
				if (_max_cur < evse.getMinCurrent() ) {
					// pause temporary, not enough amps available
					props.setState(EvseState::Disabled);
				}
				else {
					props.setState(EvseState::None);
				}
				_changed = false;
				_updated = true;
				_timer = millis();
				evse.claim(EvseClient_OpenEVSE_Shaper,EvseManager_Priority_Safety, props);
				StaticJsonDocument<128> event;
				event["shaper"]  = 1;
				event["shaper_live_pwr"] = _live_pwr;
				event["shaper_max_pwr"] = _max_pwr;
				event["shaper_cur"]	     = _max_cur;
				event["shaper_updated"] = _updated;
				event_send(event);
			}
			if (millis() - _timer > EVSE_SHAPER_FAILSAFE_TIME) {
				//available power has not been updated since EVSE_SHAPER_FAILSAFE_TIME, pause charge
				DBUGF("shaper_live_pwr has not been updated in time, pausing charge");
				_updated = false;
				props.setState(EvseState::Disabled);
				evse.claim(EvseClient_OpenEVSE_Shaper,EvseManager_Priority_Limit, props);
				StaticJsonDocument<128> event;
				event["shaper"]  = 1;
				event["shaper_live_pwr"] = _live_pwr;
				event["shaper_max_pwr"] = _max_pwr;
				event["shaper_cur"]	     = _max_cur;
				event["shaper_updated"] = _updated;
				event_send(event);
			}
	}
	else {
		//remove shaper claim
		if (_evse->clientHasClaim(EvseClient_OpenEVSE_Shaper)) {
			_evse->release(EvseClient_OpenEVSE_Shaper);
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
	_max_cur = round(((_max_pwr - _live_pwr) / evse.getVoltage()) + (evse.getAmps()));
	_changed = true; 
}

int CurrentShaperTask::getMaxPwr() {
	return _max_pwr;
}
int CurrentShaperTask::getLivePwr() {
	return _live_pwr;
}

uint8_t CurrentShaperTask::getMaxCur() {
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