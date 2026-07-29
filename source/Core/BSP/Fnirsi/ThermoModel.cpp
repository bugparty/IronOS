/*
 * ThermoModel.cpp
 *
 *  Created on: 1 May 2021
 *      Author: Ralim, MrTick
 */
#include "Setup.h"
#include "TipThermoModel.h"
#include "Types.h"
#include "Utils.hpp"
#include "configuration.h"

extern uint16_t tipSenseResistancex10Ohms;

/*
 * HS-02 factory temperature calibration (from stock firmware reverse engineering).
 *
 * The stock firmware stores three per-unit calibration words in its settings page
 * at flash 0x0801F800 (word offsets 0x16/0x17/0x18 = byte offsets 0x58/0x5C/0x60).
 * Each word minus a fixed bias yields the raw 12-bit ADC count measured at a
 * reference tip temperature during factory calibration:
 *
 *   count(140C) = word[0x16] - 0x7F21
 *   count(240C) = word[0x17] - 0x7E77
 *   count(340C) = word[0x18] - 0x7DC4
 *
 * Stock converts ADC counts to degC with a piecewise-linear curve through these
 * points (the thermocouple's uV/C slope rises with temperature, so a single
 * constant slope reads increasingly high in the soldering range). IronOS keeps
 * its own settings at 0x0801E000-0x0801F000, so the stock page survives flashing
 * IronOS and we can reuse the factory calibration directly.
 */
namespace {
constexpr uint32_t stockSettingsPageAddr = 0x0801F800;
constexpr uint32_t stockCalBias140       = 0x7F21;
constexpr uint32_t stockCalBias240       = 0x7E77;
constexpr uint32_t stockCalBias340       = 0x7DC4;

uint32_t interpolateFallbackTempCx10(uint32_t tipuV, uint32_t loweruV, uint32_t lowerTempCx10, uint32_t upperuV, uint32_t upperTempCx10) {
  return lowerTempCx10 + ((tipuV - loweruV) * (upperTempCx10 - lowerTempCx10)) / (upperuV - loweruV);
}

uint32_t fallbackTipTempCx10(uint32_t tipuV) {
  // Absolute tip temperatures measured on one HS-02A without usable factory
  // calibration. The 0uV point is the midpoint of a 28-33C cold-tip reading.
  if (tipuV <= 5400) {
    return interpolateFallbackTempCx10(tipuV, 0, 305, 5400, 2230);
  }
  if (tipuV <= 6680) {
    return interpolateFallbackTempCx10(tipuV, 5400, 2230, 6680, 2730);
  }
  if (tipuV <= 8370) {
    return interpolateFallbackTempCx10(tipuV, 6680, 2730, 8370, 3500);
  }
  if (tipuV <= 10540) {
    return interpolateFallbackTempCx10(tipuV, 8370, 3500, 10540, 4000);
  }
  if (tipuV <= 12090) {
    return interpolateFallbackTempCx10(tipuV, 10540, 4000, 12090, 4500);
  }
  return interpolateFallbackTempCx10(tipuV, 12090, 4500, 13640, 5000);
}

bool     factoryCalChecked = false;
bool     factoryCalValid   = false;
uint32_t adcCount140       = 0; // Raw 12-bit ADC counts at the three factory reference points
uint32_t adcCount240       = 0;
uint32_t adcCount340       = 0;

void loadFactoryCal() {
  factoryCalChecked    = true;
  const uint32_t *page = (const uint32_t *)stockSettingsPageAddr;
  if (page[0] == 0xFFFFFFFF) {
    return; // Erased page; stock firmware never ran / settings wiped
  }
  const int32_t a140 = (int32_t)(page[0x16] & 0xFFFF) - (int32_t)stockCalBias140;
  const int32_t a240 = (int32_t)(page[0x17] & 0xFFFF) - (int32_t)stockCalBias240;
  const int32_t a340 = (int32_t)(page[0x18] & 0xFFFF) - (int32_t)stockCalBias340;
  // Always expose the decoded counts for the debug menu, even when rejected below.
  adcCount140 = (a140 > 0) ? (uint32_t)a140 : 0;
  adcCount240 = (a240 > 0) ? (uint32_t)a240 : 0;
  adcCount340 = (a340 > 0) ? (uint32_t)a340 : 0;
  // Units that never received a calibration carry the 0x8000 neutral placeholder
  // in all three words (decoding to 223/393/572). Verified on real hardware that
  // the resulting nominal curve over-reads such units by 25-30%, so placeholders
  // must be rejected in favour of the measured fallback slope.
  if ((page[0x16] & 0xFFFF) == 0x8000 && (page[0x17] & 0xFFFF) == 0x8000 && (page[0x18] & 0xFFFF) == 0x8000) {
    return;
  }
  // Sanity: counts must be positive, strictly increasing and within the 12-bit range.
  if (a140 < 50 || a240 <= a140 || a340 <= a240 || a340 > 1500) {
    return;
  }
  factoryCalValid = true;
}
} // namespace

// Debug-menu accessor: returns true when the stock factory calibration was found
// and passed validation; outputs the three raw ADC counts (140C/240C/340C points).
bool hs02GetFactoryTipCal(uint32_t &a140, uint32_t &a240, uint32_t &a340) {
  if (!factoryCalChecked) {
    loadFactoryCal();
  }
  a140 = adcCount140;
  a240 = adcCount240;
  a340 = adcCount340;
  return factoryCalValid;
}

TemperatureType_t TipThermoModel::convertuVToDegCx10(uint32_t tipuVDelta) {
  if (!factoryCalChecked) {
    loadFactoryCal();
  }

  if (factoryCalValid) {
    // Convert IronOS uV back to stock 12-bit ADC counts (x10 for precision) by
    // inverting the exact forward conversion used in convertTipRawADCTouV:
    //   uV = raw * (ADC_VDD_MV * 1000 / ADC_MAX_READING) / OP_AMP_GAIN_STAGE
    // with stockCount = raw / 8 (the x8 oversampling). The gain assumption
    // cancels out since the same constants were used to produce tipuVDelta.
    //   countX10 = uV * OP_AMP_GAIN_STAGE * 4096 * 10 / (ADC_VDD_MV * 1000)
    //            = uV * (233472/33) / 10000 = uV * 0.70749 for the 57x gain stage
    const uint32_t countX10 = (tipuVDelta * ((OP_AMP_GAIN_STAGE * 4096) / 33)) / 10000;

    // Stock piecewise-linear curve; last segment also extrapolates above 340C.
    uint32_t tempX10;
    if (countX10 <= adcCount140 * 10) {
      tempX10 = (countX10 * 140) / adcCount140;
    } else if (countX10 <= adcCount240 * 10) {
      tempX10 = 1400 + ((countX10 - adcCount140 * 10) * 100) / (adcCount240 - adcCount140);
    } else {
      tempX10 = 2400 + ((countX10 - adcCount240 * 10) * 100) / (adcCount340 - adcCount240);
    }
    // The stock curve outputs ABSOLUTE tip temperature (factory calibration was done
    // at ~25C ambient, baked into the curve), but this function must return the
    // delta above the handle; the caller adds the handle temperature back on top.
    return (tempX10 > 250) ? (tempX10 - 250) : 0;
  }

  // The MCU die sensor remains around 41-43C and is not the connector's cold
  // junction temperature. Use the measured absolute-temperature curve, then
  // cancel the handle value which the generic caller adds back.
  const uint32_t handleTempCx10 = getHandleTemperature(0);
  const uint32_t tipTempCx10    = fallbackTipTempCx10(tipuVDelta);
  return (tipTempCx10 > handleTempCx10) ? tipTempCx10 - handleTempCx10 : 0;
}
