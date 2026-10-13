#pragma once
#include <stdint.h>
typedef int32_t TemperatureType_t;
class TipThermoModel {
public:
  static TemperatureType_t getTipInC(bool sampleNow = false);
  static TemperatureType_t getTipInCx10(bool sampleNow = false);
  static TemperatureType_t getTipMaxInC();
};
