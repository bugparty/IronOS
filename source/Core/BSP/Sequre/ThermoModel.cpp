/*
 * ThermoModel.cpp
 *
 *  Created on: 1 May 2021
 *      Author: Ralim
 */
#include "TipThermoModel.h"
#include "Utils.hpp"
#include "configuration.h"

#ifdef TEMP_uV_LOOKUP_PT1000
// Use https://br.flukecal.com/pt100-table-generator to make table for resistance to temp
const int32_t ohmsToDegCx10[] = {
    //
    //  Resistance (ohms x10)	Temperature (Celsius x10)

    10000, 0,    //
    10390, 100,  //
    10779, 200,  //
    11167, 300,  //
    11554, 400,  //
    11940, 500,  //
    12324, 600,  //
    12708, 700,  //
    13090, 800,  //
    13471, 900,  //
    13851, 1000, //
    14229, 1100, //
    14607, 1200, //
    14983, 1300, //
    15358, 1400, //
    15733, 1500, //
    16105, 1600, //
    16477, 1700, //
    16848, 1800, //
    17217, 1900, //
    17586, 2000, //
    17953, 2100, //
    18319, 2200, //
    18684, 2300, //
    19047, 2400, //
    19410, 2500, //
    19771, 2600, //
    20131, 2700, //
    20490, 2800, //
    20848, 2900, //
    21205, 3000, //
    21561, 3100, //
    21915, 3200, //
    22268, 3300, //
    22621, 3400, //
    22972, 3500, //
    23321, 3600, //
    23670, 3700, //
    24018, 3800, //
    24364, 3900, //
    24709, 4000, //
    25053, 4100, //
    25396, 4200, //
    25738, 4300, //
    26078, 4400, //
    26418, 4500, //
    26756, 4600, //
    27093, 4700, //
    27429, 4800, //
    27764, 4900, //
    28098, 5000, //

};

TemperatureType_t TipThermoModel::convertuVToDegCx10(uint32_t tipuVDelta) {

  // 3.3V -> 1K ->(ADC) <- PT1000 <- GND
  // PT100 = (adc*r1)/(3.3V-adc)
  uint32_t reading_mv     = tipuVDelta / 1000;
  uint32_t resistance_x10 = (reading_mv * 10000) / (3300 - reading_mv);

  return Utils::InterpolateLookupTable(ohmsToDegCx10, sizeof(ohmsToDegCx10) / (2 * sizeof(int32_t)), resistance_x10);
}

#endif // TEMP_uV_LOOKUP_PT1000

#ifdef TEMP_uV_LOOKUP_S60
TemperatureType_t TipThermoModel::convertuVToDegCx10(uint32_t tipuVDelta) { return (tipuVDelta * 500) / 485; }
#endif // TEMP_uV_LOOKUP_S60
