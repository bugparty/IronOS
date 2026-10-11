#pragma once
#include <stdint.h>
enum class SettingsOptions { CalibrationOffset };
uint16_t getSettingValue(SettingsOptions option);
