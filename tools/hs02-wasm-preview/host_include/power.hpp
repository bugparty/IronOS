#pragma once
#include <stdint.h>
struct WasmWattHistory { uint32_t average() const; };
extern WasmWattHistory x10WattHistory;
uint32_t getInputVoltageX10(uint16_t, uint8_t);
uint8_t getPowerSourceNumber();
