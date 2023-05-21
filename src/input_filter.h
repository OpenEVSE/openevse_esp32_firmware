#ifndef _INPUT_FILTER_H
#define _INPUT_FILTER_H


#ifndef INPUT_FILTER_MIN_TAU
#define INPUT_FILTER_MIN_TAU 10 // minimum tau constant in sec
#endif
#include <Arduino.h>

class InputFilter {
  private:
    long _last_data_time;
    double getFactor(uint32_t delta, uint32_t tau);

  public:
    InputFilter();
    // tau in sec
    double filter(double input, double filtered, uint32_t tau);
};

#endif
