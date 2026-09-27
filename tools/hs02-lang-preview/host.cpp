// Native host for the HS-02 UI. Links the production Display/LCD drivers, settings menu,
// colour screens and one generated Translation.<LANG>.cpp, stubs the hardware, and dumps
// every screen as a PPM plus an index.tsv for render.py to turn into a contact sheet.
#define private public // host-only: read the LCD framebuffers directly
#include "LCD.hpp"
#undef private
#include "Buttons.hpp"
#include "Display.hpp"
#include "OperatingModes.h"
#include "ScrollMessage.hpp"
#include "Settings.h"
#include "TipThermoModel.h"
#include "Translation.h"
#include "main.hpp"
#include "power.hpp"
#include "settingsGUI.hpp"
#include "ui_drawing.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

// ---- hardware / RTOS stubs -------------------------------------------------------------
static TickType_t g_tick = 0;
TickType_t        xTaskGetTickCount(void) { return g_tick; }
TickType_t        lastButtonTime   = 0;
volatile TickType_t lastMovementTime = 0;
volatile AccelType DetectedAccelerometerVersion = AccelType::KXTJ3;
expMovingAverage<uint32_t, wattHistoryFilter> x10WattHistory;
static uint16_t g_tipC = 320, g_vinX10 = 200;
static bool     g_tipDisconnected = false;

void               FRToSSPI::init() {}
void               FRToSSPI::fastSend(const uint8_t *, size_t) {}
void               FRToSSPI::sendCmdChain(const FRToSSPI::SPI_CMD *, size_t) {}
void               FRToSSPI::sendPixels(uint8_t *, size_t) {}
void               flash_read_buffer(uint8_t *buffer, const uint16_t length) { memset(buffer, 0xFF, length); }
void               flash_save_buffer(const uint8_t *, const uint16_t) {}
ButtonState        getButtonState() { return BUTTON_NONE; }
void               waitForButtonPress() {}
void               waitForButtonPressOrTimeout(TickType_t) {}
void               GUIDelay() {}
uint64_t           getDeviceID() { return 0x1234567890ABCDEFULL; }
uint16_t           getHandleTemperature(uint8_t) { return 250; }
uint16_t           getInputVoltageX10(uint16_t, uint8_t) { return g_vinX10; }
bool               getIsPoweredByDCIN() { return false; }
int8_t             getPowerSourceNumber(void) { return 0; }
uint32_t           getSleepTimeout(void) { return 5 * TICKS_MIN; }
uint16_t           getTipRawTemp(uint8_t) { return 9000; }
uint8_t            getTipResistanceX10() { return 25; }
TemperatureType_t  getTipTemp(void) { return g_tipC; }
bool               isTipDisconnected() { return g_tipDisconnected; }
uint16_t           X10WattsToPWM(int32_t, uint8_t) { return 0; }
bool               hs02GetFactoryTipCal(uint32_t &a, uint32_t &b, uint32_t &c) { a = 223; b = 393; c = 572; return false; }
uint32_t           TipThermoModel::convertTipRawADCTouV(uint16_t raw, bool) { return raw; }
TemperatureType_t  TipThermoModel::getTipInC(bool) { return g_tipC; }
TemperatureType_t  TipThermoModel::getTipMaxInC() { return 450; }

// ---- capture ---------------------------------------------------------------------------
static std::string outDir;
static FILE       *indexFile;
static int         frameNo = 0;
static uint8_t     lastFrame[LCD_WIDTH * LCD_HEIGHT * 3];

// Returns false (and writes nothing) when the frame is identical to the previous capture.
static bool capture(const char *group, const std::string &label, bool dedupe = false) {
  uint8_t rgb[LCD_WIDTH * LCD_HEIGHT * 3];
  for (int y = 0; y < LCD_HEIGHT; y++)
    for (int x = 0; x < LCD_WIDTH; x++) {
      uint8_t *p = &rgb[(y * LCD_WIDTH + x) * 3];
      if (LCD::isColorMode()) {
        uint8_t  idx = (LCD::activeColorBuffer[y * (LCD_WIDTH / 4) + x / 4] >> ((x % 4) * 2)) & 3;
        uint16_t v   = LCD::activePalette2bpp[idx];
        p[0] = ((v >> 11) & 31) * 255 / 31; p[1] = ((v >> 5) & 63) * 255 / 63; p[2] = (v & 31) * 255 / 31;
      } else {
        bool on = LCD::stripPointers[y / 8][x] & (1 << (y % 8)); // mono: one byte = 8 vertical pixels
        p[0] = p[1] = p[2] = on ? 0xFF : 0x00;
      }
    }
  if (dedupe && memcmp(rgb, lastFrame, sizeof rgb) == 0) return false;
  memcpy(lastFrame, rgb, sizeof rgb);
  char name[64];
  snprintf(name, sizeof name, "%04d.ppm", frameNo++);
  FILE *f = fopen((outDir + "/" + name).c_str(), "wb");
  fprintf(f, "P6\n%d %d\n255\n", LCD_WIDTH, LCD_HEIGHT);
  fwrite(rgb, 1, sizeof rgb, f);
  fclose(f);
  fprintf(indexFile, "%s\t%s\t%s\n", name, group, label.c_str());
  return true;
}

static void mono() { Display::setColorMode(false); Display::clearScreen(); }
static void color() { Display::setColorMode(true); LCD::clearScreenColor(); }

// Same as SettingsMenu.cpp printShortDescription() for LCD_160x80 (static there).
static void shortDescription(const menuitem *item) {
  Display::printWholeScreen(translatedString(Tr->SettingsShortNames[static_cast<uint8_t>(item->shortDescriptionIndex)]));
  Display::setCursor(4, 40);
}

static void renderMenu(const char *group, const menuitem *menu) {
  for (int i = 0; menu[i].draw != nullptr; i++) {
    const menuitem *item = &menu[i];
    char label[64];
    snprintf(label, sizeof label, "item %d%s", i, (item->isVisible && !item->isVisible()) ? " (hidden by default)" : "");
    bool cycles = item->autoSettingOption < SettingsOptions::SettingsOptionsLength;
    uint16_t saved = cycles ? getSettingValue(item->autoSettingOption) : 0;
    // One frame per distinct rendering of each value (option strings, on/off, units...).
    // Numeric ranges (temperatures, timeouts) are capped at a few frames.
    int distinct = 0;
    for (int v = 0; v < 40 && distinct < 6; v++) {
      mono();
      if (item->shortDescriptionSize > 0) shortDescription(item);
      item->draw();
      if (capture(group, std::string(label) + (cycles ? " value " + std::to_string(getSettingValue(item->autoSettingOption)) : ""), v > 0)) distinct++;
      if (!cycles) break;
      nextSettingValue(item->autoSettingOption);
      if (getSettingValue(item->autoSettingOption) == saved) break;
    }
    if (cycles) setSettingValue(item->autoSettingOption, saved);
    if (item->description) {
      mono();
      drawScrollingText(translatedString(Tr->SettingsDescriptions[item->description - 1]), 0);
      capture(group, std::string(label) + " help (first frame)");
    }
  }
}

int main(int argc, char **argv) {
  outDir = argc > 1 ? argv[1] : "out/frames";
  mkdir(outDir.c_str(), 0755);
  indexFile = fopen((outDir + "/index.tsv").c_str(), "w");
  Display::initialize();
  resetSettings();

  // Settings menus, as SettingsMenu.cpp draws them.
  renderMenu("Settings: root", rootSettingsMenu);
  const char *names[] = {"Settings: power", "Settings: soldering", "Settings: power saving", "Settings: UI", "Settings: advanced"};
  for (int m = 0; subSettingsMenus[m] != nullptr && m < 5; m++) renderMenu(names[m], subSettingsMenus[m]);

  // Full-screen warnings (warnUser / showStartupWarning path).
  struct { const char *name; uint16_t idx; } warnings[] = {
      {"CalibrationDone", Tr->CalibrationDone}, {"ResetOKMessage", Tr->ResetOKMessage}, {"SettingsResetMessage", Tr->SettingsResetMessage},
      {"NoAccelerometerMessage", Tr->NoAccelerometerMessage}, {"NoPowerDeliveryMessage", Tr->NoPowerDeliveryMessage},
      {"LockingKeysString", Tr->LockingKeysString}, {"UnlockingKeysString", Tr->UnlockingKeysString},
      {"WarningKeysLockedString", Tr->WarningKeysLockedString}, {"DeviceFailedValidationWarning", Tr->DeviceFailedValidationWarning},
      {"TooHotToStartProfileWarning", Tr->TooHotToStartProfileWarning}, {"WarningThermalRunaway", Tr->WarningThermalRunaway},
      {"WarningTipShorted", Tr->WarningTipShorted}};
  for (auto &w : warnings) {
    mono(); warnUser(translatedString(w.idx), BUTTON_NONE); capture("Warnings", w.name);
    // Messages too long to wrap scroll; show where the scroll is 4 s in (skipped if unchanged).
    g_tick = 4000; mono(); warnUser(translatedString(w.idx), BUTTON_NONE); capture("Warnings", std::string(w.name) + " (+4 s)", true); g_tick = 0;
  }
  // Confirmation prompts scroll in the large font.
  mono(); drawScrollingText(translatedString(Tr->SettingsResetWarning), 0); capture("Warnings", "SettingsResetWarning (first frame)");
  mono(); drawScrollingText(translatedString(Tr->SettingsCalibrationWarning), 0); capture("Warnings", "SettingsCalibrationWarning (first frame)");

  // Colour screens.
  color(); ui_draw_home_gauge_idle(25); capture("Home / soldering", "home idle (25C)");
  g_tipC = 318; color(); ui_draw_home_gauge_soldering(false); capture("Home / soldering", "soldering 318C");
  g_tipC = 400; color(); ui_draw_home_gauge_soldering(true); capture("Home / soldering", "soldering boost");
  g_tipC = 150; color(); ui_draw_home_gauge_sleep(150); capture("Home / soldering", "sleep 150C");
  mono(); ui_draw_temperature_change(); capture("Home / soldering", "temperature change");
  g_vinX10 = 95; mono(); ui_draw_warning_undervoltage(); capture("Warnings", "undervoltage (UVLO)"); g_vinX10 = 200;
  mono(); ui_draw_cjc_sampling(3); capture("Warnings", "CJC calibrating");
  mono(); ui_draw_soldering_profile_advanced(180, 150, 30, 0, 60); capture("Home / soldering", "profile preheat");
  mono(); ui_draw_soldering_profile_advanced(180, 150, 30, 6, 60); capture("Home / soldering", "profile cooldown");
  // DebugMenu.cpp wraps at 17 pages on LCD_160x80.
  for (int i = 0; i < 17; i++) { mono(); ui_draw_debug_menu(i); capture("Debug menu", "page " + std::to_string(i + 1)); }
  fclose(indexFile);
  printf("%d frames -> %s\n", frameNo, outDir.c_str());
  return 0;
}
