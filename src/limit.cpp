#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LIMIT)
#undef ENABLE_DEBUG
#endif

#include "limit.h"
#include "debug.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <MicroTasks.h>
#include "evse_man.h"

// ---------------------------------------------
//
//            LimitType Class
//
//----------------------------------------------

uint8_t LimitType::fromString(const char *value)
{
	// Cheat a bit and just check the first char
	switch (value[0]) {
		// None
		case ('n'):
			_value = LimitType::None;
			break;
		//Time
		case ('t'):
			_value = LimitType::Time;
			break;
		//Energy
		case ('e'):
			_value = LimitType::Energy;
			break;
		//Soc
		case ('s'):
			_value = LimitType::Soc;
			break;
		//Range
		case ('r'):
			_value = LimitType::Range;
			break;
	}
return _value;
}
const char *LimitType::toString()
{
	return  LimitType::None == _value ? "none" :
			LimitType::Time == _value ? "time" :
			LimitType::Energy == _value ? "energy" :
			LimitType::Soc == _value ? "soc" :
			LimitType::Range == _value ? "range" :
			"unknown";
}

LimitType LimitType::operator= (const Value val) {
	_value = val;
	return *this;
}

// ---------------------------------------------
//
//            LimitProperties Class
//
//----------------------------------------------
LimitProperties::LimitProperties()
{
	_type = LimitType::None;
	_value = 0;
	_auto_release = true;
};

LimitProperties::~LimitProperties()
{
	DBUGLN("LimitProperties Destructor");

};

void LimitProperties::init() 
{
	_type = LimitType::None;
	_value = 0;
	_auto_release = true;
};

LimitType LimitProperties::getType() {
	return _type;
};

bool LimitProperties::setType(LimitType type)
{
	_type = type;
	return true;
};

uint32_t  LimitProperties::getValue() {
	return _value;
};

bool LimitProperties::setValue(uint32_t value)
{
	_value = value;
	return true;
};

bool LimitProperties::getAutoRelease() {
	return _auto_release;
};

bool LimitProperties::deserialize(JsonObject &obj)
{
	if(obj.containsKey("type")) {
		_type.fromString(obj["type"]);
  	}
	if(obj.containsKey("value")) {
		_value = obj["value"];
  	}
	if(obj.containsKey("auto_release")) {
		_auto_release = obj["auto_release"];
  	}
	return _type > 0 && _value > 0;

};

bool LimitProperties::serialize(JsonObject &obj)
{

	obj["type"] = _type.toString();
	obj["value"] = _value;
	obj["auto_release"] = _auto_release;
	return true;
};

// ---------------------------------------------
//
//            Limit Class
//
//----------------------------------------------

//global instance
Limit limit;

Limit::Limit() : Limit::Task() {
	_limit_properties.init();
};

Limit::~Limit() {
	_evse -> release(EvseClient_OpenEVSE_Limit);
};

void Limit::setup() {

};

void Limit::begin(EvseManager &evse) {
	// todo get saved default limit
	DBUGLN("Starting Limit task");
	this -> _evse    = &evse;
	MicroTask.startTask(this);
};

unsigned long Limit::loop(MicroTasks::WakeReason reason) {


	if (hasLimit()) {
		LimitType type = _limit_properties.getType();
		uint32_t value = _limit_properties.getValue();
		bool auto_release = _limit_properties.getAutoRelease();

		if (_evse->isCharging() ) {
			_has_vehicle = true;
			bool limit_reached = false;
			switch (type) {
				case LimitType::Time:
					limit_reached = limitTime(value);
					break;
				case LimitType::Energy:
					limit_reached = limitEnergy(value);
					break;
			}
			if (limit_reached) {
				// Limit reached, disabling EVSE
				if (_evse->getClaimProperties(EvseClient_OpenEVSE_Limit).getState() == EvseState::None) {
					DBUGLN("Limit as expired, disable evse");
					EvseProperties props;
					props.setState(EvseState::Disabled);
					props.setAutoRelease(true);
					_evse->claim(EvseClient_OpenEVSE_Limit, EvseManager_Priority_Limit, props);
				}
			}
		}

		else if ( _has_vehicle && !_evse->isVehicleConnected()) {
			_has_vehicle = false;
			// if auto release is set, reset Limit properties
			if (auto_release) {
				_limit_properties.init();
			}
		}
	}
	
	else {
		if (_evse->clientHasClaim(EvseClient_OpenEVSE_Limit)) {
			//remove claim if limit as been deleted
			_evse->release(EvseClient_OpenEVSE_Limit);
		}
	}
	return EVSE_LIMIT_LOOP_TIME;
};

bool Limit::limitTime(uint32_t val) {
	uint32_t elapsed = (uint32_t)_evse->getSessionElapsed()/60;
	if ( val > 0 && _evse->getSessionElapsed() > 0 && _evse->getSessionElapsed()/60 >= val ) {
		// Time limit done
		DBUGLN("Time limit reached");
		DBUGVAR(val);
		DBUGVAR(elapsed);
		return true;
	}
	else return false;
};

bool Limit::limitEnergy(uint32_t val) {
	uint32_t elapsed = _evse->getSessionEnergy();
	if ( val > 0 && _evse->getSessionEnergy() > 0 && (uint32_t)_evse->getSessionEnergy() >= val ) {
		// Energy limit done
		DBUGLN("Energy limit reached");
		DBUGVAR(val);
		DBUGVAR(elapsed);
		return true;
	}
	else return false;
};

bool Limit::hasLimit() {
	return _limit_properties.getType() != LimitType::None;
};

bool Limit::set(String json) {
	LimitProperties props;
	if (props.deserialize(json)) {
		set(props);
		return true;
	}
	else return false;
};

bool Limit::set(LimitProperties props) {
	_limit_properties = props;
	return true;
};

bool Limit::clear() {
	_limit_properties.init();
	return true;
};

LimitProperties Limit::getLimitProperties() {
	return _limit_properties;
};