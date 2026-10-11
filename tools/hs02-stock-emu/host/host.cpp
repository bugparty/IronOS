// Host build of the real Core/BSP/Fnirsi/ThermoModel.cpp, for compare.py. Reads
// raw ADC counts the way the firmware does (shared CJC-offset removal, the BSP
// curve, then the handle temperature added back) and prints the tip temperature
// for each.
//   host OFFSET_UV HANDLE_CX10 WORD140 WORD240 WORD340 COUNT...
#include "BSP.h"
#include "Settings.h"
#include "TipThermoModel.h"
#include "configuration.h"
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
uint16_t        tipSenseResistancex10Ohms = 0;
static uint16_t g_raw, g_offset, g_handle = 420;
uint16_t        getHandleTemperature(uint8_t) { return g_handle; }
uint16_t        getTipRawTemp(uint8_t) { return g_raw; }
uint16_t        getSettingValue(SettingsOptions) { return g_offset; }
// Copy of Core/Drivers/TipThermoModel.cpp convertTipRawADCTouV + getTipInCx10.
static uint32_t rawTouV(uint16_t rawADC) {
  uint32_t vddRailmVX10  = ADC_VDD_MV * 10;
  uint32_t rawInputmVX10 = (rawADC * vddRailmVX10) / (ADC_MAX_READING);
  uint32_t valueuV       = rawInputmVX10 * 100;
  valueuV /= OP_AMP_GAIN_STAGE;
  if (g_offset) {
    if (valueuV > g_offset)
      valueuV -= g_offset;
    else
      valueuV = 0;
  }
  return valueuV;
}
int main(int argc, char **argv) {
  // ThermoModel reads the stock settings page straight from its flash address.
  uint32_t *page = (uint32_t *)mmap((void *)0x0801F000, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  if (page != (uint32_t *)0x0801F000) {
    perror("mmap");
    return 1;
  }
  uint32_t *settings = (uint32_t *)0x0801F800;
  for (int i = 0; i < 0x200; i++)
    settings[i] = 0;
  g_offset = atoi(argv[1]);
  g_handle = atoi(argv[2]);
  for (int i = 0; i < 3; i++)
    settings[0x16 + i] = strtoul(argv[3 + i], nullptr, 0);
  for (int i = 6; i < argc; i++) {
    int count      = atoi(argv[i]);
    g_raw          = count * 8;
    int32_t tipX10 = TipThermoModel::convertuVToDegCx10(rawTouV(g_raw)) + g_handle;
    printf("%d,%.1f\n", count, tipX10 / 10.0);
  }
}
