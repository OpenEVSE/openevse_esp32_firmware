#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_ENERGY_METER)
#undef ENABLE_DEBUG
#endif
#include "energy_meter.h"
#include "evse_monitor.h"

#include "debug.h"
#include "app_config.h"
#include "event.h"


EnergyMeterData::EnergyMeterData()
{
    total = 0;
    session = 0;
    daily = 0;
    weekly = 0;
    monthly = 0;
    yearly = 0;
    date.day = 0;
    date.month = 0;
    date.year = 0;
    imported = false;
    elapsed = 0;
    switches = 0;
};

void EnergyMeterData::reset(bool fullreset = false, bool import = false)
{
    session = 0;
    daily = 0;
    weekly = 0;
    monthly = 0;
    yearly = 0;
    date.day = 0;
    date.month = 0;
    date.year = 0;
    elapsed = 0;
    if (fullreset) {
        switches = 0;
        total = 0;
    }
    if (import) {
        imported = false;
    }
}

void EnergyMeterData::serialize(StaticJsonDocument<capacity> &doc)
{
    doc["to"] = total;
    doc["se"] = session;
    doc["dy"] = daily;
    doc["wk"] = weekly;
    doc["mo"] = monthly;
    doc["yr"] = yearly;
    doc["el"] = elapsed;
    doc["sw"] = switches;
    doc["im"] = imported;
    doc.createNestedObject("dt");
    doc["dt"]["dy"] = date.day;
    doc["dt"]["mo"] = date.month;
    doc["dt"]["yr"] = date.year;

};

void EnergyMeterData::deserialize(StaticJsonDocument<capacity> &doc)
{
    if (doc.containsKey("to") && doc["to"].is<double>())
    {
        // total
        total = doc["to"];
    }
    if (doc.containsKey("se") && doc["se"].is<double>())
    {
        // session
        session = doc["se"];
    }
    if (doc.containsKey("dy") && doc["dy"].is<double>())
    {
        // daily
        daily = doc["dy"];
    }
    if (doc.containsKey("we") && doc["we"].is<double>())
    {
        // weekly
        daily = doc["we"];
    }
    if (doc.containsKey("mo") && doc["mo"].is<double>())
    {
        // monthly
        monthly = doc["mo"];
    }
    if (doc.containsKey("yr") && doc["yr"].is<double>())
    {
        // yearly
        yearly = doc["yr"];
    }
    if (doc.containsKey("dt"))
    {
        // date
        if (doc["dt"].containsKey("dy") && doc["dt"]["dy"].is<uint8_t>())
        {
            date.day = doc["dt"]["dy"];
        }
        if (doc["dt"].containsKey("mo") && doc["dt"]["mo"].is<uint8_t>())
        {
            date.month = doc["dt"]["mo"];
        }
        if (doc["dt"].containsKey("yr") && doc["dt"]["yr"].is<uint16_t>())
        {
            date.year = doc["dt"]["yr"];
        }
    }
    if (doc.containsKey("im") && doc["im"].is<bool>())
    {
        // old OpenEvse total_energy imported flag
        imported = doc["im"];
    }
    if (doc.containsKey("el") && doc["el"].is<uint32_t>())
    {
        // old OpenEvse total_energy imported flag
        elapsed = doc["el"];
    }
    if (doc.containsKey("sw") && doc["sw"].is<uint32_t>())
    {
        // old OpenEvse total_energy imported flag
        switches = doc["sw"];
    }
};

EnergyMeter::EnergyMeter() : 
_last_upd(0),
_write_upd(0),
_rotate_upd(0),
_elapsed(0),
_switch_state(0)
{
};

EnergyMeter::~EnergyMeter()
{
    end();
};

void EnergyMeter::begin(EvseMonitor *monitor)
{ 
  this->_monitor = monitor;
  // get current state
  _switch_state = _monitor->isActive();
  _data.reset();
    if (load())
    {
        DBUGLN("Energy Meter loaded");
        _last_upd = millis();
        _write_upd = _last_upd;
        _event_upd = _last_upd;
        _rotate_upd = _last_upd;
        if (_data.elapsed > 0)
        {
            if (_monitor->isVehicleConnected())
            {
                // restoring session, wifi module has probably rebooted
                _elapsed = _data.elapsed * 1000;
            }
            else
            {
                _data.elapsed = 0;
                save();
            }
        }

        // if total is 0, try to load old OpenEvse module counter
        if (!_data.total && !_data.imported)
        {
            _monitor->importTotalEnergy();
        }

        // event data
        publish();
    }
    else
    {
        DBUGLN("Couldn't start Energy Meter");
        _last_upd = 0;
    }
};

// Import old Evse total_kwh
bool EnergyMeter::importTotalEnergy(double kwh)
{
    _data.total += kwh;
    _data.imported = true;
    if (save())
    {
        publish();
        return true;
    }
    else {
        _data.imported = false;
        return false;
    }
};

void EnergyMeter::end()
{
    // save data
    save();
};

bool EnergyMeter::reset(bool full = false, bool import = false)
{
    if (createEnergyMeterStorage(full, import)) {
        return true;
    }
    else
        return false;
};

void EnergyMeter::clearSession()
{
    _data.session = 0;
    _data.elapsed = 0;
    _elapsed = 0;
    save();
    DBUGLN("Energy Meter: clearing session");
    publish();
};

bool EnergyMeter::update()
{
  // calc time elapsed since last update
    if (!_last_upd)
    {
        _last_upd = millis();
        return false;
    }

    uint32_t curms = millis();
    uint32_t dms = curms - _last_upd;

    // increment elapsed time
    _elapsed += dms;
    _data.elapsed = _elapsed / 1000U;

    // if (dms >=  MAX_INTERVAL) {
    DBUGLN("Energy Meter: Incrementing");
    // accumulate data
    uint32_t mv = _monitor->getVoltage() * 1000;
    uint32_t ma = _monitor->getAmps() * 1000;

    /*
     * The straightforward formula to compute 'milliwatt-seconds' would be:
     *     mws = (mv/1000) * (ma/1000) * dms;
     *
     * However, this has some serious drawbacks, namely, truncating values
     * when using integer math. This can introduce a lot of error!
     *     5900 milliamps -> 5.9 amps (float) -> 5 amps (integer)
     *     0.9 amps difference / 5.9 amps -> 15.2% error
     *
     * The crazy equation below helps ensure our intermediate results always
     * fit in a 32-bit unsigned integer, but retain as much precision as
     * possible throughout the calculation. Here is how it was derived:
     *     mws = (mv/1000) * (ma/1000) * dms;
     *     mws = (mv/(2**3 * 5**3)) * (ma/(2**3 * 5**3)) * dms;
     *     mws = (mv/2**3) * (ma/(2**3) / 5**6 * dms;
     *     mws = (mv/2**4) * (ma/(2**2) / 5**6 * dms;
     *
     * By breaking 1000 into prime factors of 2 and 5, and shifting things
     * around, we almost match the precision of floating-point math.
     *
     * Using 16 and 4 divisors, rather than 8 and 8, helps precision because
     * mv is always greater than ~100000, but ma can be as low as ~6000.
     *
     * A final note- the divisions by factors of 2 are done with right shifts
     * by the compiler, so the revised equation, although it looks quite
     * complex, only requires one divide operation.
     */
    double mws = (mv / 16) * (ma / 4) / 15625 * dms;
    if (config_threephase_enabled())
    {
        // Multiply calculation by 3 to get 3-phase energy.
        mws *= 3;
    }

    // convert to w/h
    double wh = mws / 3600000UL;
    _data.session += wh;
    _data.total += wh / 1000;
    DBUGVAR(_data.session);

    _data.daily += wh / 1000;
    _data.weekly += wh / 1000;
    _data.monthly += wh / 1000;
    _data.yearly += wh / 1000;

    _last_upd = curms;
    DBUGF("session_wh = %.2f, total_kwh = %.2f", _data.session, _data.total);
    if (curms - _write_upd >= SAVE_INTERVAL)
    {
        // save Energy Meter data to file
        save();
        _write_upd = curms;
    }

    if (curms - _event_upd >= EVENT_INTERVAL)
    {
        // publish Energy Meter data
        publish();
        _event_upd = curms;
    }
    if (curms - _rotate_upd >= ROTATE_INTERVAL)
    {
        rotate();
        _rotate_upd = curms;
    }
    return true;
    //}
    // else return false;
};

bool EnergyMeter::publish()
{
    DynamicJsonDocument doc(capacity);
    createEnergyMeterJsonDoc(doc);
    event_send(doc);
    return true;
};

void EnergyMeter::createEnergyMeterJsonDoc(JsonDocument &doc)
{
    doc["session_elapsed"] = _data.elapsed; // sec
    doc["session_energy"] = _data.session;	// wh
    doc["total_energy"] = _data.total;		// kwh
    doc["total_day"] = _data.daily;			// kwh
    doc["total_week"] = _data.weekly;		// kwh
    doc["total_month"] = _data.monthly;		// kwh
    doc["total_year"] = _data.yearly;		// kwh
    doc["total_switches"] = _data.switches;
}

void EnergyMeter::rotate()
{
    // check if those counters need to be reset first
    EnergyMeterDate curdate = getCurrentDate();
    bool has_changed = false;
    if (_data.date.day != curdate.day)
    {
        DBUGLN("EnergyMeter: reset daily");
        _data.daily = 0;
        if (curdate.day == 0)
        {
            DBUGLN("EnergyMeter: reset weekly");
            _data.weekly = 0;
        }
        _data.date.day = curdate.day;
        has_changed = true;
    }
    if (_data.date.month != curdate.month)
    {
        DBUGLN("EnergyMeter: reset monthly");
        _data.date.month = curdate.month;
        _data.monthly = 0;
        has_changed = true;
    }
    if (_data.date.year != curdate.year)
    {
        DBUGLN("EnergyMeter: reset yearly");
        _data.date.year = curdate.year;
        _data.yearly = 0;
        has_changed = true;
    }
    if (has_changed)
    {
        save();
        publish();
    }
    if (!_monitor->isVehicleConnected() && _data.elapsed)
    {
        _data.elapsed = 0;
    }
};

bool EnergyMeter::load()
{
    // open or create file if not present
    File file = LittleFS.open(ENERGY_METER_FILE, "r");
    if (file)
    {
        String ret = file.readString();
        DBUGVAR(ret);
        StaticJsonDocument<capacity> doc;
        DeserializationError err = deserializeJson(doc, ret);
        file.close();
        DBUGVAR(err.code());
        if (DeserializationError::Code::Ok == err)
        {
            _data.deserialize(doc);
            DBUGVAR(_data.elapsed);
            DBUGVAR(_data.total);
            DBUGVAR(_data.session);
            DBUGVAR(_data.daily);
            DBUGVAR(_data.weekly);
            DBUGVAR(_data.monthly);
            DBUGVAR(_data.yearly);
            DBUGVAR(_data.switches);
            DBUGVAR(_data.date.day);
            DBUGVAR(_data.date.month);
            DBUGVAR(_data.date.year);
            DBUGVAR(_data.imported);
            // check if we need to reset some counters
            rotate();
            return true;
        }
        else
        {
            DBUGLN("EnergyMeter: Can't parse 'emeter.json', creating a new file");
            _data.reset();
            if (createEnergyMeterStorage(true, false))
            {
                return true;
            }
            else
                return false;
        }
    }
    else
    {
        file.close();
        DBUGLN("Energy Meter: File missing, creating");
        if (createEnergyMeterStorage(true, false))
        {
            return true;
        }
        else
        {
            return false;
        }
    }
};

bool EnergyMeter::write(EnergyMeterData &data)
{
    DBUGLN("Energy Meter: Saving data");
    File file = LittleFS.open(ENERGY_METER_FILE, "w", true);
    if (!file)
    {
        file.close();
        DBUGLN("Energy Meter: error can't open/create file");
        return false;
    }
    StaticJsonDocument<capacity> doc;
    _data.serialize(doc);
    // Keep previous "imported" property
    if (_data.imported)
    {
        doc["imported"] = _data.imported;
    }
    if (serializeJson(doc,file))
    {
        DBUGLN("Energy Meter: data saved");
        file.close();
        return true;
    }
    else
    {
        file.close();
        DBUGLN("Energy Meter: can't write to file");
        return false;
    }
};

bool EnergyMeter::createEnergyMeterStorage(bool fullreset = false, bool forceimport = false)
{
    _data.reset(fullreset, forceimport);
    _data.date = getCurrentDate();
    if (forceimport) {
        return _monitor->importTotalEnergy();
    }
    else {
        return write(_data);
    }
};

EnergyMeterDate EnergyMeter::getCurrentDate()
{
    EnergyMeterDate date;
    struct timeval local_time;
    gettimeofday(&local_time, NULL);
    tm *timeinfo = gmtime(&local_time.tv_sec);
    uint16_t yday = (uint16_t)timeinfo->tm_yday;
    date.day = (uint8_t)timeinfo->tm_wday;
    date.month = (uint8_t)timeinfo->tm_mon;
    date.year = 1900 + (uint16_t)timeinfo->tm_year;

    return date;
};

void EnergyMeter::increment_switch_counter()
{
  if (_switch_state != _monitor->isActive())
  {
    _data.switches++;
    DynamicJsonDocument doc(JSON_OBJECT_SIZE(1)+16);
    doc["total_switches"] = _data.switches;
    event_send(doc);
  }
  _switch_state = _monitor->isActive();
};