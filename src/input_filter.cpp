#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_INPUT_FILTER)
#undef ENABLE_DEBUG
#endif

#include "input_filter.h"
#include "debug.h"

InputFilter::InputFilter() {
  _last_data_time = 0;
}

double InputFilter::getFactor(uint32_t delta, uint32_t tau)
{
	double factor;
	if (tau > 0)
	{
		if (tau < INPUT_FILTER_MIN_TAU)
		{
			tau = INPUT_FILTER_MIN_TAU;
		}
		factor = 1 - exp((-1) * ((double)delta / (double)tau));
	}
	else
	{
		// avoid divide by 0 , tau 0 means no filtering
		factor = 1;
	}
  DBUGVAR(factor);
  return factor;
}

double InputFilter::filter(double input, double filtered, uint32_t tau)
{
  if (!_last_data_time) {
		_last_data_time = millis();
  }
  uint32_t delta = (millis() - _last_data_time)/1000;
  DBUGVAR(delta);
  DBUGVAR(tau);
  _last_data_time = millis();
  double factor = getFactor(delta, tau);
  filtered = ((input * factor) + (filtered * (1 - factor)));
  DBUGVAR(filtered);
  return filtered;
}