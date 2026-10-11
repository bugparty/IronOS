# HS-02 stock firmware emulator

Runs a stock FNIRSI HS-02 `APP_*.bin` in [Unicorn](https://www.unicorn-engine.org/) so we
can ask the vendor firmware what it computes instead of reading its disassembly. Nobody on
this fork owns an HS-02B, so this is how the B's temperature model was checked.

- `stock_emu.py IMAGE` boots the image from its reset vector, fires its timer interrupt by
  hand and replaces its ADC read with a chosen count. It prints the stock curve temperature
  and the tip temperature the firmware acts on for each count. `--cal` sets the three
  calibration words, `--csv` prints CSV.
- `compare.py FIRMWARE_DIR` builds the real `Core/BSP/Fnirsi/ThermoModel.cpp` for the host
  and checks it against both stock images for every ADC count. It exits non-zero if they
  differ by more than 1 °C. Run it after any change to the stock-curve path.

```
pip install unicorn
./stock_emu.py APP_HS_02B_V1.8_250218.bin --counts 0,68,117,190
./compare.py "../../Development Resources/firmwares"
```

The stock images are FNIRSI's and are not in this repository. The function addresses in
`stock_emu.py` are for the V1.8 images (`APP_HS_02A_V1.8_250217.bin`,
`APP_HS_02B_V1.8_250218.bin`); other versions are rejected.

What it found (V1.8, both models):

- The ADC count goes into the curve raw. No zero offset is removed first.
- The curve gives 0 °C for a count of 0. Stock then adds a constant 25 °C and displays and
  regulates that sum, so the calibration points are tip temperatures of 165/265/365 °C.
- The A has a fourth, fixed curve point (770 counts = 440 °C). The B extrapolates.
- Stock A sends a count exactly equal to its 240 °C point down the last segment, so that
  single count reads high. IronOS does not copy this.

Where IronOS differs from stock on purpose: an HS-02B that was never calibrated (cal words
`0x8000` ×3) runs the nominal curve without the constant 25 °C, because a tester's unit
melted 60/40 solder at a curve value of 190–200 °C, which stock would show as 215–225 °C.
`compare.py` expects exactly that 25 °C gap for that case and a match for calibrated units.

What it cannot tell: anything physical. Whether the stock curve matches a real tip, what a
cold tip reads at the ADC, and when stock samples relative to the heater PWM all still
need hardware.

Peripherals are plain memory. A status register polled repeatedly from one place has its
value flipped every 64 polls, which gets the firmware through its wait loops. Buttons read
as released, the SysTick delay returns at once, and the settings page is erased flash, so
the firmware starts from its defaults.
