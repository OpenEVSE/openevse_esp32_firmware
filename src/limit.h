#ifndef _OPENEVSE_LIMIT_H
#define _OPENEVSE_LIMIT_H


#ifndef EVSE_LIMIT_LOOP_TIME
#define EVSE_LIMIT_LOOP_TIME 1000
#endif 
#include <Arduino.h>
#include <ArduinoJson.h>
#include <MicroTasks.h>
#include "evse_man.h"

class LimitType {

  	public:
		enum Value : uint8_t {
			None,
			Time,
			Energy,
			Soc,
			Range
		};
		
		LimitType() = default;
  		constexpr LimitType(Value value) : _value(value) { }
		uint8_t fromString(const char *value);
		const char *toString();

		operator Value() const { return _value; }
		explicit operator bool() = delete;        // Prevent usage: if(state)
		LimitType operator= (const Value val);

	private:
    	Value _value;
};

class LimitProperties : virtual public JsonSerialize<512> {
	private:
		LimitType _type;
		uint32_t  _value;
		bool      _auto_release;
	public:
		LimitProperties();
		~LimitProperties();
		void init();
		bool setType(LimitType type);
		bool setValue(uint32_t value);
		LimitType getType();
		uint32_t  getValue();
		bool getAutoRelease();

		using JsonSerialize::deserialize;
		virtual bool deserialize(JsonObject &obj);
		using JsonSerialize::serialize;
		virtual bool serialize(JsonObject &obj);

};

class Limit: public MicroTasks::Task
{
	private:
		EvseManager *_evse;
		LimitProperties _limit_properties;
		bool _has_vehicle;
		bool limitTime(uint32_t val);
		bool limitEnergy(uint32_t val);
		bool limitSoc(uint32_t val);
		bool limitRange(uint32_t val);

	protected:
		void setup();
		unsigned long loop(MicroTasks::WakeReason reason);
		

	public:
		Limit();
		~Limit();
		void begin(EvseManager &evse);
		bool hasLimit();
		bool set(String json);
		bool set(LimitProperties props);
		bool clear();
		LimitProperties getLimitProperties();
};

extern Limit limit;
#endif 