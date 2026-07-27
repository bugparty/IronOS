/*
 * ThermoModel.cpp
 *
 *  Created on: 1 May 2021
 *      Author: Ralim
 */
#include "TipThermoModel.h"
#include "Utils.hpp"
#include "configuration.h"

#ifdef TEMP_uV_LOOKUP_HAKKO
const int32_t uVtoDegCx10[] = {
    //
    // uv -> temp in C x10
    0,     0,    //
    266,   100,  //
    522,   200,  //
    770,   300,  //
    1010,  400,  //
    1244,  500,  //
    1473,  600,  //
    1697,  700,  //
    1917,  800,  //
    2135,  900,  //
    2351,  1000, //
    2566,  1100, //
    2780,  1200, //
    2994,  1300, //
    3209,  1400, //
    3426,  1500, //
    3644,  1600, //
    3865,  1700, //
    4088,  1800, //
    4314,  1900, //
    4544,  2000, //
    4777,  2100, //
    5014,  2200, //
    5255,  2300, //
    5500,  2400, //
    5750,  2500, //
    6003,  2600, //
    6261,  2700, //
    6523,  2800, //
    6789,  2900, //
    7059,  3000, //
    7332,  3100, //
    7609,  3200, //
    7889,  3300, //
    8171,  3400, //
    8456,  3500, //
    8742,  3600, //
    9030,  3700, //
    9319,  3800, //
    9607,  3900, //
    9896,  4000, //
    10183, 4100, //
    10468, 4200, //
    10750, 4300, //
    11029, 4400, //
    11304, 4500, //
    11573, 4600, //
    11835, 4700, //
    12091, 4800, //
    12337, 4900, //
    12575, 5000, //

};
#endif

const int uVtoDegCx10Items = sizeof(uVtoDegCx10) / (2 * sizeof(uVtoDegCx10[0]));

TemperatureType_t TipThermoModel::convertuVToDegCx10(uint32_t tipuVDelta) { return Utils::InterpolateLookupTable(uVtoDegCx10, uVtoDegCx10Items, tipuVDelta); }
