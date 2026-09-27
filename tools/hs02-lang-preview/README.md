# HS-02 language preview

This tool renders every HS-02 UI screen in one or more languages and puts the results side by side. Use it to find translations that overflow the 160×80 panel, or strings that were never translated, without flashing hardware.

```sh
tools/hs02-lang-preview/run.sh EN RU SV
# -> tools/hs02-lang-preview/out/sheets/*.png  (one column per language)
```

The tool needs `g++`, `python3` and Pillow. If a language's `Core/Gen/Translation.<LANG>.cpp` is missing or older than its JSON, `run.sh` regenerates it through the firmware Makefile.

## What it renders

The tool compiles the production code natively with `g++`. It does not reimplement the UI:

- `Display.cpp` / `LCD.cpp`: fonts, line wrapping, clipping
- `settingsGUI.cpp` + `Settings.cpp`: every settings menu item, cycled through its values, plus the first frame of its scrolling help text
- `color_160x80/*`: home/soldering/sleep gauges, warnings, undervoltage, CJC, profile, debug menu
- the generated `Translation.<LANG>.cpp`

Only RTOS, SPI and sensor calls are replaced: `shim/` provides the headers and `host.cpp` provides fixed sensor values. The screens use the same colour split as the device. Home, soldering and sleep are drawn in colour with the real palette. Menus, warnings and the debug menu use the 1bpp mono framebuffer, shown here as white on black.

## Limits

- `printShortDescription()` is `static` in `SettingsMenu.cpp`, so `host.cpp` has a copy of its three lines. Update that copy if the menu layout changes.
- Menu items with a custom increment handler (calibration, reset) are rendered in their default state only.
- Scrolling help text is captured at its first frame only. It scrolls on the device, so its length is not a problem.
