// Color theme for the Fnirsi HS-02 colour UI.
// All values are TRUE RGB565 colours; the driver applies the panel's INVON inversion itself.
// Use RGB565(r,g,b) with 0..255 components, or paste a raw 0xRRRR value.
#pragma once
#include <stdint.h>

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

// ---- Home / soldering gauge: one 4-colour palette per state ----
// Each palette is {background, ink, accent, secondary}. Index 0 is the screen background.
// Defaults reproduce the original firmware colours exactly.
#define THEME_IDLE_BG     0x0862 // navy
#define THEME_IDLE_INK    0xF77C // off-white
#define THEME_IDLE_ACCENT 0x45B8 // cool teal
#define THEME_IDLE_DIM    0x536A // secondary (dim)

#define THEME_SOLDER_BG     0x28A0 // dark red-brown
#define THEME_SOLDER_INK    0xF77C
#define THEME_SOLDER_ACCENT 0xFBC5 // ember
#define THEME_SOLDER_DIM    0x9B48

#define THEME_BOOST_BG     0x5000
#define THEME_BOOST_INK    0xF77C
#define THEME_BOOST_ACCENT 0xFBC5
#define THEME_BOOST_DIM    0xB344

#define THEME_SLEEP_BG     0x0924
#define THEME_SLEEP_INK    0xF77C
#define THEME_SLEEP_ACCENT 0x45B8
#define THEME_SLEEP_DIM    0x4B8F

// ---- Colour gauge screen (home / soldering), 4-entry palette ----
#define THEME_BACKGROUND THEME_IDLE_BG // index 0: screen background (must stay index 0)
#define THEME_INK        THEME_IDLE_INK // index 1: normal text / graphics
#define THEME_HEATING    THEME_SOLDER_ACCENT // index 2: tip heating
#define THEME_COOL       THEME_IDLE_ACCENT // index 3: idle / sleep

// ---- Mono screens (settings menu, temperature adjust, debug) ----
#define THEME_MONO_BG       THEME_IDLE_BG // same as the idle screen so the UI looks continuous
#define THEME_MONO_INK      THEME_IDLE_ACCENT       // accent colour when browsing
#define THEME_MONO_INK_EDIT THEME_BOOST_INK         // boost text colour while editing
#define THEME_MONO_BG_EDIT  THEME_BOOST_BG          // boost background while editing
