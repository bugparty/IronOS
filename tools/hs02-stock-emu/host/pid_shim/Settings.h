#pragma once
#include <stdint.h>
enum class SettingsOptions { VoltageDiv, PowerLimit, KeepAwakePulse, KeepAwakePulseWait, KeepAwakePulseDuration };
uint16_t getSettingValue(SettingsOptions option);
