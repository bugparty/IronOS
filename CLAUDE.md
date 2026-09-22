# CLAUDE.md — FNIRSI HS-02 port of IronOS

This repo is a fork of [IronOS](https://github.com/Ralim/IronOS) with an in-progress port
to the **FNIRSI HS-02 / HS-02A** soldering iron (PanKleszcz's `fnirsi_hs02` port + ok2cm's
work, merged into `dev`). This file documents the non-obvious, HS-02-specific facts needed
to work on it. General IronOS docs live in `Documentation/`.

## Build & flash

- One-shot build: `./build-hs02.sh` (repo root). `--clean` to rebuild, `--flash <MSD mount>`
  to build + flash. It checks the toolchain and points at the output.
- Manual: `cd source && make model=HS02 -j$(nproc)`. Output to flash is
  `source/Hexfile/HS02_EN_firmware.bin` (carries the 16-byte `bin` header the MSD bootloader
  needs — plain `HS02_EN.bin` will NOT flash).
- HS-02B: use `cd source && make model=HS02B -j$(nproc)`; the output is
  `source/Hexfile/HS02B_EN_firmware.bin`.
- Toolchain (Fedora): `arm-none-eabi-gcc-cs arm-none-eabi-gcc-cs-c++ arm-none-eabi-newlib
  arm-none-eabi-binutils-cs` + `pip install bdflib` (translation/font generation).
- **Flashing quirk:** the FNIRSI MSD bootloader mishandles USB write caching. On Linux you
  MUST copy the firmware to the mounted drive **twice**, each followed by `sync`, or the iron
  hangs in the bootloader with a half-written image. `build-hs02.sh --flash` does this.

## Hardware (source: schematic RE + stock-firmware RE)

- **MCU:** Nation **N32L403** (Cortex-M4, 64 MHz). Uses an **STM32F1-style peripheral map**
  (RCC `0x40021000`, GPIOA `0x40010800`, etc.). App is linked at `0x08005800`; the first
  0x5800 is the FNIRSI bootloader (not in the `APP_` images).
- **Display:** 160×80 colour IPS, **ST7735** over SPI @ 16 MHz. Driver: `Core/Drivers/LCD.cpp`
  (`LCD_160x80`), low-level SPI in `Core/BSP/Fnirsi/SPI_Wrapper.cpp`.
  - Panel quirks (learned the hard way doing the boot logo): **column-major** pixel scan
    (the mono UI's `refresh()` sends per-8-row strip, column-major), **INVON** (display
    inversion on — `0x0000` shows white), and the framebuffer/logo colours must be
    **inverted** and sent **RGB** (NOT byte/channel-swapped) to come out true. The source
    logo block also had a 64-pixel header + a trailing junk row.
- **Accelerometer:** Kionix **KXTJ3-1057** on **software (bit-banged) I2C**, PA9=SCL/PA10=SDA,
  addr `0x0E`, INT not wired (polled). Driver `Core/Drivers/KXTJ3.*`.
- **Heater:** AO4407A P-MOSFET driven by PWM on PA8 (`PWR_OUT`); current sense via 10 mΩ
  shunt → PB1 (`I_MEAS`). Tip thermocouple → AD8552 → PA0 (`T_MEAS`), read via ADC+DMA
  (no function pokes the ADC register directly). Amp gain is ambiguous: the KiCad-redrawn
  schematic shows both 48K/1K (=49) and 56K/1K (=57) networks around the two amp channels;
  the port's `OP_AMP_GAIN_STAGE 57` suggests T_SENSE uses 56K/1K. Doesn't matter for
  accuracy — the gain constant cancels in both temperature-model paths (see below).
- **USB-PD:** CH224K sink controller negotiates voltage; MCU sets the voltage tier via
  PB6/PB7 (CFG). This is what caps available wattage.
- **Buttons:** 3 physical — UP=PB4, DOWN=PB5, OK=PB3 (all input-pullup, active low).

## Gotchas / things that bite

- **`LCD` name clash:** `n32l40x.h` defines `LCD` as a peripheral pointer macro. Any file
  that uses the `LCD` **class** after including the Nation headers must `#undef LCD` first
  (see `Core/BSP/Fnirsi/BSP.cpp`).
- **HW I2C is unusable here.** The N32L40x I2C peripheral inherits the classic STM32F1 I2C
  errata (event-timing races needing top-priority IRQ/DMA, corrupt 1–2 byte reads, DMA
  conflicts, glitch sensitivity). Stock firmware and this port both use bit-banged I2C.
- **PWM pipeline width:** `powerPWM` is 395 on this board (> 255). The shared
  `X10WattsToPWM()` / `setTipPWM()` path must be `uint16_t` end to end, otherwise high duty
  wraps mod 256 and output collapses (the "100 W only reaches ~40 W" bug — fixed).

## Temperature control & calibration (branch `fix/hs02-pid-tuning`)

- **Controller:** PID (`TIP_CONTROL_PID`), NOT the ARDC it shipped with. Field-tuned on an
  HS-02A soldering XT60 connectors: KP=40, KI=300, KD=8000, `TIP_PID_INTEGRAL_LIMIT_SCALE 30`.
  Requires the **conditional anti-windup** added to the shared PID in `PIDThread.cpp`
  (upstream-worthy): without it the integral rails to ± the clamp during heatup/overshoot
  and the system limit-cycles around the set point for minutes.
- **Temp model** (`Core/BSP/Fnirsi/ThermoModel.cpp`): the original "21uV per 1C" guess
  over-read ~23% (displayed 450 ≈ real 370). Two paths now:
  1. **Stock calibration auto-adoption:** stock firmware keeps 3 user-calibration words in
     its settings page at `0x0801F800` (word offsets 0x16/0x17/0x18; count = word − bias
     0x7F21/0x7E77/0x7DC4; curve anchors 140/240/340 °C; stock cal MENU shows 150/250/350).
     `0x8000` = neutral "never calibrated" placeholder (decodes to nominal 223/393/572) and
     is REJECTED — the nominal curve over-reads ~25-30% (stock firmware itself shows 230 °C
     at a real 183 °C, hardware-verified). IronOS settings live below `0x0801F000`, so the
     stock page survives reflashing.
  2. **Fallback:** measured 26 uV/°C constant, calibrated with solder melting points
     (Sn45/Pb55: solidus 183 °C "blade can cut", liquidus ~227 °C "melts") — ~3× more
     accurate than stock on an uncalibrated unit (+17 vs +47 °C at 183 °C real).
- **Debug menu "Tip Cal" page** (17th page; home screen → long-press UP): shows the three
  decoded cal counts + "In Use" / "Unused 26uV/C". NOTE: raw ASCII literals render garbled —
  the small font only carries glyphs used by translations; UI text must go through
  `make_translation.py` (`get_constants()` SmallSymbol* entries or `get_debug_menu()`).
- Stock PID for reference (from RE): incremental/velocity form, Kp=2.38 Ki=0.81 Kd=0.28
  (doubles in the .data tail), gain-scheduled ×4.5/×3.0/×2.0 at 9/12/15 V PD tiers.

## UI architecture (IronOS)

Two layers, split by concern:
- **Logic** (`Core/Threads/UI/logic/`): screen state machines / navigation / input handling.
  **Shared across all devices**, resolution-independent (e.g. `HomeScreen.cpp`, `Soldering.cpp`,
  `SettingsMenu.cpp`, `TemperatureAdjust.cpp`). Edit here to change behaviour everywhere.
- **Drawing** (`Core/Threads/UI/drawing/<variant>/`): the actual pixel painting, one variant
  per resolution — `mono_96x16`, `mono_128x32`, and ok2cm's `color_160x80` (17 `draw_*.cpp`
  each). HS-02 uses `color_160x80`. Adding a screen = implement its `draw_*` per variant.

So ok2cm did NOT rewrite the UI — he added a `color_160x80` **drawing** variant and reused the
shared logic. Control changes (e.g. the dedicated OK button) live in the logic layer and apply
to all variants automatically.

## HS-02-specific additions in this fork (branches off `dev`)

- `feat/hs02-ok-button` — OK (PB3) wired as a real third button (`BUTTON_OK_SHORT/LONG`,
  one-shot long-press). Short = enter/confirm/select, long = back. Core input in
  `Core/Drivers/Buttons.cpp` (weak `getButtonOK()`, overridden in the Fnirsi BSP); per-screen
  actions in the logic layer.
- `fix/pwm-uint16-upstream` — the uint16 PWM widening (generic, upstream-worthy).
- `fix/hs02-power-100w` — the above + the colour boot logo (`showBootLogo()`,
  `LCD::drawNativeImage()`, `FRToSSPI::fastSend()`, `FnirsiBootLogo.h`).
- `fix/hs02-pid-tuning` — on top of the above: shared-PID conditional anti-windup +
  configurable integral clamp, ARDC→PID switch with field-tuned gains, corrected temperature
  model with stock-calibration auto-adoption, and the debug-menu "Tip Cal" page (see the
  "Temperature control & calibration" section).

## Reverse-engineering notes

Extensive stock-firmware RE (V1.8 vs V2.1) lives under
`Development Resources/firmwares/` (git-untracked): analysis write-up, schematic reading,
button/interaction map, and the extracted boot logo. Key finding: the temperature control
path (calibration, PID, factory defaults) is byte-identical V1.8↔V2.1; the only temp-relevant
change in V2.1 is the accelerometer tilt-angle logic (a new 90° breakpoint affecting
motion-sleep), which is why V2.1 "holds temperature more aggressively".
