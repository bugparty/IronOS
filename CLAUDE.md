# CLAUDE.md — FNIRSI HS-02 port of IronOS

This repo is a fork of [IronOS](https://github.com/Ralim/IronOS) with an in-progress port
to the **FNIRSI HS-02 / HS-02A** soldering iron (PanKleszcz's `fnirsi_hs02` port + ok2cm's
work, merged into `dev`). This file documents the non-obvious, HS-02-specific facts needed
to work on it. General IronOS docs live in `Documentation/`.

## Build & flash

- One-shot build: `./build-hs02.sh` (repo root). `--clean` to rebuild, `--flash <MSD mount>`
  to build + flash. It checks the toolchain and points at the output.
- Manual: `cd source && make model=HS02 -j$(nproc)` (or `model=HS02B` — see the A/B section
  below). Output to flash is `source/Hexfile/$(model)_EN_firmware.bin` (carries the 16-byte
  magic header the MSD bootloader needs — plain `HS02_EN.bin` will NOT flash).
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
- **Bootloader firmware magic is CASE-SENSITIVE and differs per model.** The 16-byte header
  `add_header.py` prepends starts with a 3-char magic, and each bootloader accepts **only its
  own case** (verified by diffing the two bootloaders' full flash dumps — the check is
  otherwise byte-identical code, same branch offsets, differing *only* in three `CMP`
  immediates):
  - **HS-02A** bootloader compares `0x62 0x69 0x6E` = **`bin`** (lowercase).
  - **HS-02B** bootloader compares `0x42 0x49 0x4E` = **`BIN`** (uppercase).
  - Neither has a fallback path for the other case. Stock images match: `APP_HS_02A_*.bin`
    begin `62 69 6e 00`, `APP_HS_02B_V1.8.bin` begins `42 49 4e 00`.

  This is handled automatically: `source/Makefile` picks the magic from `$(model)`
  (`CUSTOM_FORMAT_CMD` in the Fnirsi block), so `model=HS02` emits `bin` and `model=HS02B`
  emits `BIN`. You cannot cross-flash by accident — the wrong magic is silently rejected
  by the bootloader.

## HS-02A vs HS-02B build targets

Same PCB, different tip: **HS-02A takes F245 cartridges, HS-02B takes F210** (FNIRSI's own
designations, ≈ JBC C245/C210 form factors; the two tips are NOT interchangeable). Confirmed
same board by diffing `APP_HS_02A_V1.8` against `APP_HS_02B_V1.8` (same version, one day
apart): peripheral reference counts match item for item (GPIOA 17/17, GPIOB 8/8, RCC 11/11,
TIM3 3/3, TIM4 3/3). So the whole BSP transfers unchanged; only tip-dependent parameters vary.

- `make model=HS02` → HS-02A (name kept for backwards compatibility) → `HS02_EN_firmware.bin`
- `make model=HS02B` → HS-02B → `HS02B_EN_firmware.bin`
- `MODEL=HS02B ./build-hs02.sh` for the one-shot script.
- **`MODEL_HS02` is the FAMILY define, set for BOTH variants** — every existing
  `#ifdef MODEL_HS02` (`Pins.h`, `I2CBB1.cpp`, `Font.h`, `MOVThread.cpp`, `PIDThread.cpp`)
  is a shared-PCB concern and applies to both. `MODEL_HS02A` / `MODEL_HS02B` select only
  the tip parameters in `configuration.h`, with an `#error` enforcing exactly one.

⚠️ **HS-02B is UNVALIDATED — nobody on this fork owns the hardware.** Its parameters are
deliberately conservative guesses, not tuned values: KP 40→20, KI 700→150, KD 8000→3000,
integral clamp 30→10, power 100 W→65 W, max temp 450→400 °C. Rationale: F210 is the smaller
cartridge, so less thermal mass → higher plant gain → controller gains must come down, which
is also the safe direction (an under-tuned PID is sluggish rather than overshooting).
`TIP_RESISTANCE` stays 25: F210's real value is unpublished (checked FNIRSI docs, manuals,
reviews, IronOS discussion #1935) and is bounded to ≤4 Ω by the 100 W-at-20 V rating. It
matters less than it looks — R only scales watts→PWM, giving
`P_delivered = P_requested × (R_assumed / R_true)`, a pure gain on the controller output that
is mathematically indistinguishable from scaling KP/KI/KD together. **Retuning absorbs any
error in it, so do not "correct" it in isolation** — that would silently rescale loop gain.

**HS-02B temperature model (stock RE, V1.8):** the B's thermocouple signal at the ADC is
**~3.2× smaller** than the A's. The stock temperature function (`0x08007408` on A,
`0x080073ec` on B) is otherwise identical. Only the calibration biases differ, and they
encode the nominal counts at 140/240/340 °C: A `0x7F21/0x7E77/0x7DC4` = 223/393/572,
B `0x7FBC/0x7F8B/0x7F42` = 68/117/190 (count = word − bias; bias = 0x8000 − nominal).
Running the A's curve on a B therefore read a real ~540 °C as 200 °C, which a PR #10
tester saw as a red-hot tip at a displayed 200. `ThermoModel.cpp` uses the B biases, and
an uncalibrated B (0x8000 placeholder or erased page) runs on the B nominal curve, never
the A's measured fallback. Other stock A/B differences in that path: A has a fourth
anchor (770 counts at 440 °C) while B extrapolates its 240–340 segment; A averages 40
samples and B 20; the ADC sample time is 0 (A) vs 5 (B), but IronOS uses 239.5 cycles on
every channel. The V2.x dumps (B 2.0.1, A 2.1.1) restructured this code and have not been
decoded yet. The B nominal curve has not been checked with a thermometer. On the A, the
nominal curve over-reads by 25–30 %, so if the B behaves the same, the error is on the
safe side.

**Build hygiene gotcha (fixed, but know why):** `Core/Gen/` holds generated sources
(`macros.txt`, `Translation.*.cpp`) derived from the *model's* `configuration.h`, but unlike
`Objects/` it is shared across models, and `macros.txt` used to depend only on `Makefile`.
Building B after A therefore linked A's translation data into B's image. Now stamped with the
model name so switching regenerates. This affected any two models in one tree (e.g. TS100 then
TS80), so it is upstream-worthy, not Fnirsi-specific.

## Temperature control & calibration (branch `fix/hs02-pid-tuning`)

- **Controller:** PID (`TIP_CONTROL_PID`), NOT the ARDC it shipped with. Field-tuned on an
  HS-02A soldering XT60 connectors: KP=40, KI=700, KD=8000, `TIP_PID_INTEGRAL_LIMIT_SCALE 30`
  (authoritative values live in `Core/BSP/Fnirsi/configuration.h:185-188` — check there, not here).
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
  2. **Fallback:** a measured 36 uV/°C base through 250 °C, followed by continuous
     piecewise corrections of 1.17× from 250–350 °C and 1.56× above 350 °C.
- **Debug menu "Tip Cal" page** (17th page; home screen → long-press UP): shows the three
  decoded cal counts + "In Use" / "Custom Curve". NOTE: raw ASCII literals render garbled —
  the small font only carries glyphs used by translations; UI text must go through
  `make_translation.py` (`get_constants()` SmallSymbol* entries or `get_debug_menu()`).
- Stock PID for reference (from RE): incremental/velocity form. **HS-02A and HS-02B use
  DIFFERENT gain sets** (verified by decoding both firmwares' double constant pools — so
  the "02A/02B differ only in the soft-start ramp" claim in
  `firmwares/HS-02_PID_FUN_08009f00_还原.md` is wrong):
  - **HS-02A** (V1.8 & V2.1): Kp=2.38 Ki=0.81 Kd=0.28 (doubles at `0x080170d0`, in the
    .data tail), gain-scheduled ×4.5/×3.0/×2.0 at 9/12/15 V PD tiers (`0x0800a24c`).
  - **HS-02B** (V1.8 **and V2.0.1**): Kp=0.04 Ki=0.01 Kd=0.02 (doubles at `0x0800a1b0` in
    V1.8, `0x0800a13c` in V2.0.1), with direct output clamps e≥100→100 % / e≤−20→0
    (`0x0800a1d0`/`…1d8`) and Kp halved to 0.02 when setpoint < 200 °C. Tick fn
    `FUN_08009ed0`; see `firmwares/HS-02_PID_FUN_08009f00_还原.md`.
  - Neither gain set appears in the other machine's firmware — they are mutually exclusive.
    Now confirmed across **four** firmwares (02A V1.8 + V2.1.0, 02B V1.8 + V2.0.1): the split
    is per-model and persists across version bumps, it is not a version artefact.

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

### Full-flash dumps (`FLASH.BIN` 128K @`0x08000000` + `RAM.BIN` 24K + `SYSOPT.BIN` 20B)

Two community dumps live under `firmwares/`: `Flash+Ram+Sysopt.HS02A.2.1.1/` and
`hs202b-dump/` (= **HS-02B V2.0.1**, a build not otherwise available as an `APP_*.bin`).
These are the only source of the **bootloader** (first `0x5800`) and the stock settings page.

- **They were produced by an official firmware feature, not a homebrew tool.** Stock builds
  ship a debug USB MSD mode exposing a FAT16 volume `HS-02 MEM` with `FLASH.BIN`/`RAM.BIN`/
  `SYSOPT.BIN` (VID `0x19F5` Nations). It is versioned and lives in the **app**, not the
  bootloader: `HS-02 USB MSD v0.0.7` (02A 2.1.1), `v0.0.6` @`0x08008b8c` (02B V2.0.1).
  Binary forensics on 2.1.0→2.1.1 (LCP 0, 9.6 % aligned identity, 52.8 % of 64 B chunks
  verbatim-but-relocated, no constant-shift delta) says full recompile-from-source, i.e.
  official — not a binary patch.
- **Bootloaders are one source base with a per-model magic constant** — see the case-sensitive
  `bin`/`BIN` gotcha above. Otherwise the first ~15 KB is near-identical (a dozen data
  pointers shifted `0x20`); real code divergence is confined to `0x4000-0x5000` (the MSD
  flash-write path). `SYSOPT.BIN` is byte-identical across models (RDP off).
- **Neither dumped unit had ever been user-calibrated**: the three cal words at `0x0801F858`
  read `0x8000 0x8000 0x8000` on both — the placeholder our `ThermoModel` rejects.
- **RAM is not tight on stock**: both dumps show live data only in the low ~8 KB; from
  `0x20002000` up, entropy is 7.8–7.95 bits/byte (uninitialised). ≥16 KB of the 24 KB is free.
  (An early "2.1.1 uses all RAM" read was wrong — `SP=0x20006000` is the stack-*top*
  convention, not a usage figure.)
