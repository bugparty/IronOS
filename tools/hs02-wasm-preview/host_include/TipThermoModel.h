#pragma once
#include "OperatingModes.h"
namespace TipThermoModel {
TemperatureType_t getTipInC();
uint32_t          convertTipRawADCTouV(uint16_t rawADC, bool skipCalOffset = false);
}
