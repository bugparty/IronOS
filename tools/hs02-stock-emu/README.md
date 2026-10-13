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
- `control_sim.py IMAGE` closes the temperature loop: both stock timer interrupts fire at
  their real rates, the heater PWM register drives a two-mass model of the tip, and the
  model's temperature comes back through the ADC. `--load START:STOP:WATTS` pulls heat
  from the tip, `--volts`, `--power` and `--setpoint` change the conditions, `--plot`
  and `--csv` save the run. See "Stock temperature control" below.

```
pip install unicorn            # and matplotlib for control_sim.py --plot
./stock_emu.py APP_HS_02B_V1.8_250218.bin --counts 0,68,117,190
./compare.py "../../Development Resources/firmwares"
./control_sim.py APP_HS_02A_V1.8_250217.bin --load 14:18:15 --plot a.png
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

## Stock temperature control

Read from the V1.8 control functions (`0x08009ef0` on the A, `0x08009ed0` on the B) and
confirmed by running them: constant-error runs for the laws, `control_sim.py` for the loop.

Common to both models:

- One control period is 10 ms: a 100 µs timer (TIM3) counts 100 phases. The heater is
  switched off at one phase, the tip is sampled 2 (A) or 3 (B) ticks later, the
  controller runs and the heater PWM (TIM1 channel 1, 30–40 kHz) gets its new duty.
- Error = setpoint − (curve temperature + 25). Above +100 °C the output is forced to
  full, below −20 °C to zero.
- The output is limited to a duty that stands for a power: the "power" setting (65 by
  default, 100 at most) reads as watts into an assumed heater resistance. B: `P × 200 / V²`
  percent, which is 2.0 Ω. A: `P × 325 / V²` (3.25 Ω), and at 20 V `P × 275 / 400` up to
  65, `P × 340 / 400` above (2.75 and 3.4 Ω). `V` is the supply tier (5/9/12/15/20),
  picked from the input voltage measured while not heating. At 20 V and 65 the cap is
  32 % (B) and 44 % (A). The tip models default to 2.0 Ω (B) and 3.0 Ω (A) for that
  reason; nobody has measured an F210.
- Each wake-up starts with a 0.4 s soft start: 12/18/24/30 % (B) or 15/22/30/37 % (A).
- The display is not the measurement. It shows the averaged reading pulled 10 °C
  towards the setpoint: reading + 10 below it, reading − 10 above it, and exactly the
  setpoint for anything within ±10 °C. While the tip cools it also holds the last value
  until the reading is about 30 °C below it, so a tip at 325 °C still shows 350.

Where they differ, which is more than the gains:

- **HS-02A is proportional only.** `duty % = 0.81 × e + 2.38 × Δe + 0.28 × Δ²e`, written
  to the output each period, not added to it, so the constants named Kp/Ki/Kd act as
  D/P/second-derivative gains. The whole expression is multiplied by 2, 3 and 4.5 at
  15, 12 and 9 V. It uses the 40-sample average (0.4 s) of the reading. Without an
  integrator the thermocouple sits below the setpoint by `duty / 0.81`: 9 °C idle and
  20 °C under a 15 W load in the default model, and the display says 350 in both cases.
- **HS-02B is a slow integrator.** `output += 0.04 × Δe + 0.01 × e + 0.02 × Δ²e`
  (0.02 for the first gain below 200 °C), clamped to 0..cap every period, on the latest
  sample. That is 1 % of duty per second per °C. It has no steady-state error, but it
  arrives at the setpoint with the output still at the cap and only the −20 °C cut-off
  stops it, so every heat-up overshoots by about 20 °C at the thermocouple. The soft
  start also preloads the integrator to 30 % on every wake-up, however small the error.

What the loop simulation cannot tell: heat-up time, the size of the overshoot at the tip
surface and the ringing all depend on the tip model, whose heat capacities and thermal
resistances are guesses. Fit them to a recorded heat-up before reading numbers off a plot.

Peripherals are plain memory. A status register polled repeatedly from one place has its
value flipped every 64 polls, which gets the firmware through its wait loops (timer
registers are exempt, or interrupt ticks go missing). Buttons read as released, the
SysTick delay returns at once, and the settings page is erased flash, so the firmware
starts from its defaults.
