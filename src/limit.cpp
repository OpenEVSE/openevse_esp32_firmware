#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LIMIT)
#undef ENABLE_DEBUG
#endif

#include "limit.h"
#include "debug.h"
#include "event.h"
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

bool LimitProperties::setAutoRelease(bool val) {
	_auto_release = val;
	return true;
}

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

Limit::Limit() :
  MicroTasks::Task(),
  _version(0),
  _sessionCompleteListener(this)
{
  _limit_properties.init();
}


Limit::~Limit() {
	_evse -> release(EvseClient_OpenEVSE_Limit);
};

void Limit::setup() {

};

void Limit::begin(EvseManager &evse) {
	// todo get saved default limit
	DBUGLN("Starting Limit task");
	this -> _evse = &evse;
	// retrieve default limit from config
	LimitProperties limitprops;
	LimitType limittype;
    limittype.fromString(limit_default_type.c_str());
    limitprops.setValue(limit_default_value);
	// default limits have auto_release set to false
    limitprops.setAutoRelease(false);
    limit.set(limitprops);
	MicroTask.startTask(this);
 	onSessionComplete(&_sessionCompleteListener);
};

unsigned long Limit::loop(MicroTasks::WakeReason reason) {


	if (hasLimit()) {
		LimitType type = _limit_properties.getType();
		uint32_t value = _limit_properties.getValue();
		bool auto_release = _limit_properties.getAutoRelease();

		if (_evse->isCharging() ) {
			bool limit_reached = false;
			switch (type) {
				case LimitType::Time:
					limit_reached = limitTime(value);
					break;
				case LimitType::Energy:
					limit_reached = limitEnergy(value);
					break;
				case LimitType::Soc:
					limit_reached = limitSoc(value);
					break;
				case LimitType::Range:
					limit_reached = limitRange(value);
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
	}
	else {
		if (_evse->clientHasClaim(EvseClient_OpenEVSE_Limit)) {
			//remove claim if limit has been deleted
			_evse->release(EvseClient_OpenEVSE_Limit);
		}
	}
	return EVSE_LIMIT_LOOP_TIME;
};

bool Limit::limitTime(uint32_t val) {
	uint32_t elapsed = (uint32_t)_evse->getSessionElapsed()/60;
	if ( val > 0 && elapsed >= val ) {
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
	if ( val > 0 && elapsed >= val ) {
		// Energy limit done
		DBUGLN("Energy limit reached");
		DBUGVAR(val);
		DBUGVAR(elapsed);
		return true;
	}
	else return false;
};

bool Limit::limitSoc(uint32_t val) {
	uint32_t soc = _evse->getVehicleStateOfCharge();
	if ( val > 0  && soc >= val ) {
		// SOC limit done
		DBUGLN("SOC limit reached");
		DBUGVAR(val);
		DBUGVAR(soc);
		return true;
	}
	else return false;
};

bool Limit::limitRange(uint32_t val) {
	uint32_t rng = _evse->getVehicleRange();
	if ( val > 0  && rng >= val ) {
		// Range limit done
		DBUGLN("Range limit reached");
		DBUGVAR(val);
		DBUGVAR(rng);
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
	StaticJsonDocument<32> doc;
	doc["limit"] = hasLimit();
	doc["limit_version"] = ++_version;
	event_send(doc);
	return true;
};

LimitProperties Limit::get() { 
	return _limit_properties;
};

bool Limit::clear() {
	_limit_properties.init();
	StaticJsonDocument<32> doc;
	doc["limit"] = false;
	doc["limit_version"] = ++_version;
	event_send(doc);
	return true;
};

uint8_t Limit::getVersion() {
	return _version;
}

void Limit::onSessionComplete(MicroTasks::EventListener *listner) {
    _evse -> onSessionComplete(listner);
    // disable claim if it has not been deleted already
		if (_evse->clientHasClaim(EvseClient_OpenEVSE_Limit)) {
			_evse->release(EvseClient_OpenEVSE_Limit);
		}
    if (_limit_properties.getAutoRelease()){
      clear();
    }
  }
  
