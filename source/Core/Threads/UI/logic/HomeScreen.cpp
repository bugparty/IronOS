
#include "Buttons.hpp"
#include "OperatingModes.h"
#include "ui_drawing.hpp"

bool showExitMenuTransition = false;

OperatingMode handleHomeButtons(const ButtonState buttons, guiContext *cxt) {
  if (buttons != BUTTON_NONE && cxt->scratch_state.state1 == 0) {
    return OperatingMode::HomeScreen; // Ignore button press
  } else {
    cxt->scratch_state.state1 = 1;
  }
  switch (buttons) {
  case BUTTON_NONE:
    // Do nothing
    break;
  case BUTTON_BOTH:
    break;

  case BUTTON_B_LONG:
#if defined(LCD_160x80)
    // The colour gauge screen and the mono menu system share one physical LCD buffer (see
    // LCD.hpp); a slide transition would need to render the far side into secondFrameBuffer,
    // which stays 1bpp-only, so every crossing between the two uses a hard cut.
    cxt->transitionMode = TransitionAnimation::None;
#else
    cxt->transitionMode = TransitionAnimation::Down;
#endif
    return OperatingMode::DebugMenuReadout;
    break;
  case BUTTON_F_LONG:
#ifdef PROFILE_SUPPORT
    if (!isTipDisconnected()) {
#if defined(LCD_160x80)
      cxt->transitionMode = TransitionAnimation::None;
#else
      cxt->transitionMode = TransitionAnimation::Left;
#endif
      return OperatingMode::SolderingProfile;
    } else {
      return OperatingMode::HomeScreen;
    }
#else
#if defined(LCD_160x80)
    cxt->transitionMode = TransitionAnimation::None;
#else
    cxt->transitionMode = TransitionAnimation::Left;
#endif
    return OperatingMode::TemperatureAdjust;
#endif
    break;
  case BUTTON_OK_SHORT: // Dedicated OK button enters soldering (same as front-press)
  case BUTTON_F_SHORT:
    if (!isTipDisconnected()) {
#if defined(LCD_160x80)
      cxt->transitionMode = TransitionAnimation::None; // Staying within the colour gauge screen family.
#else
      bool detailedView   = getSettingValue(SettingsOptions::DetailedIDLE) && getSettingValue(SettingsOptions::DetailedSoldering);
      cxt->transitionMode = detailedView ? TransitionAnimation::None : TransitionAnimation::Left;
#endif
      return OperatingMode::Soldering;
    }
    break;
  case BUTTON_B_SHORT:
#if defined(LCD_160x80)
    cxt->transitionMode = TransitionAnimation::None;
#else
    cxt->transitionMode = TransitionAnimation::Right;
#endif
    return OperatingMode::SettingsMenu;
    break;
  default:
    break;
  }
  return OperatingMode::HomeScreen;
}

OperatingMode drawHomeScreen(const ButtonState buttons, guiContext *cxt) {

  currentTempTargetDegC = 0; // ensure tip is off
  getInputVoltageX10(getSettingValue(SettingsOptions::VoltageDiv), 0);
  uint32_t tipTemp = TipThermoModel::getTipInC();

#if defined(LCD_160x80)
  ui_draw_home_gauge_idle(tipTemp);
#else
  // Setup LCD Cursor location
  if (Display::getRotation()) {
    Display::setCursor(50, 0);
  } else {
    Display::setCursor(-1, 0);
  }
  if (getSettingValue(SettingsOptions::DetailedIDLE)) {
    ui_draw_homescreen_detailed(tipTemp);
  } else {
    ui_draw_homescreen_simplified(tipTemp);
  }
#endif
  return handleHomeButtons(buttons, cxt);
}
