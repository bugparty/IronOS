#!/usr/bin/env python3
"""Close the temperature loop around a stock FNIRSI HS-02 firmware image.

The stock firmware runs in Unicorn with its two timer interrupts fired at their real
rates. Its heater PWM register drives a small thermal model of the tip, and the model's
temperature is fed back through the ADC. The controller is therefore the vendor's own
code; only the tip is made up.

    ./control_sim.py APP_HS_02B_V1.8_250218.bin
    ./control_sim.py APP_HS_02A_V1.8_250217.bin --load 12:16:20 --plot a.png
    ./control_sim.py APP_HS_02A_V1.8_250217.bin --volts 12 --power 100 --csv a.csv

The tip model is two heat capacities (heater with the thermocouple, and the tip body).
Its defaults are guesses for an F210 / F245 cartridge, NOT measurements: heat-up time,
overshoot and ringing depend on them. What does not depend on them is the control law.
"""

import argparse
import csv
import struct
import sys

from stock_emu import TIP_CHANNEL, VIN_CHANNEL, StockFirmware

CONTROL_PERIOD = 0.01  # the scheduler runs its 100 phases on a 100 us tick (TIM3)
TIM3 = (45, 0x40000410)  # vector and status register; 32 MHz / 32 / 100 = 10 kHz
TIM4 = (46, 0x40000810)  # 32 MHz / 32 / 1000 = 1 kHz: buttons, input voltage, timers
HEATER_COMPARE = 0x40012C34  # TIM1 channel 1 (PA8), counts 0..100 of a period of 101
HEATER_PERIOD = 101
GPIOB_INPUT = 0x40010C10
OK_BUTTON = 1 << 3  # PB3, active low
MAIN_LOOP_STEP = 2000  # instructions of the main loop per 100 us tick

# Per model: where the firmware keeps its control state, its nominal thermocouple
# curve (ADC counts at curve temperatures; the tip temperature is the curve + 25 C),
# and the assumed tip. The heater resistances are the ones the firmware's own duty
# limit implies (see README); the heat capacities and thermal resistances are guesses.
#   heat_state  byte in the runtime struct: 0 heating, 1 idle, 2 sleep
#   output      double: the controller output in percent, before it becomes a duty
#   working_setpoint  word in the runtime struct that the heating state copies to +0
MODELS = {
    "HS-02A": dict(
        heat_state=0x83,
        output=0x20000030,
        curve=((0, 0), (223, 140), (393, 240), (572, 340), (770, 440)),
        tip=dict(c_heater=0.2, c_tip=0.7, r_heater_tip=1.5, r_tip_air=36.0, ohms=3.0),
    ),
    "HS-02B": dict(
        heat_state=0x7F,
        output=0x20000018,
        curve=((0, 0), (68, 140), (117, 240), (190, 340)),
        tip=dict(c_heater=0.08, c_tip=0.22, r_heater_tip=2.0, r_tip_air=72.0, ohms=2.0),
    ),
}
WORKING_SETPOINT = 0x08
POWER_SETTING = 0x28  # in the settings struct; 65 by default, 100 at most


class Tip:
    """Heater node (where the thermocouple is) and tip body. J/K, K/W, ohms, watts."""

    def __init__(self, c_heater, c_tip, r_heater_tip, r_tip_air, ohms, ambient=25.0):
        self.c_heater, self.c_tip = c_heater, c_tip
        self.r_heater_tip, self.r_tip_air = r_heater_tip, r_tip_air
        self.ohms, self.ambient = ohms, ambient
        self.heater = self.body = ambient
        self.load = 0.0  # extra heat drawn from the tip body, e.g. a joint

    def step(self, dt, volts, duty):
        power = volts * volts / self.ohms * duty
        flow = (self.heater - self.body) / self.r_heater_tip
        loss = (self.body - self.ambient) / self.r_tip_air + self.load
        self.heater += dt * (power - flow) / self.c_heater
        self.body += dt * (flow - loss) / self.c_tip
        return power


class ControlLoop:
    def __init__(self, path, volts=20.0):
        self.firmware = firmware = StockFirmware(path)
        self.model = MODELS[firmware.model["name"]]
        self.runtime = firmware.model["runtime"]
        self.settings = firmware.model["settings"]
        self.volts = volts
        # The firmware picks its supply tier from the voltage it sees while not heating.
        firmware.adc[VIN_CHANNEL] = round(volts / 11 / 3.3 * 4095)
        firmware.run(30_000_000)  # boot into the main loop
        self.wait(1.0)

    def _word(self, address):
        return struct.unpack("<I", self.firmware.uc.mem_read(address, 4))[0]

    def count_for(self, tip_c):
        """ADC count the nominal thermocouple curve gives for a tip temperature."""
        curve_c = max(tip_c - 25.0, 0.0)
        points = self.model["curve"]
        for (c0, t0), (c1, t1) in zip(points, points[1:]):
            if curve_c <= t1:
                break
        return round(c0 + (curve_c - t0) * (c1 - c0) / (t1 - t0))

    def period(self, tip=None):
        """Run one 10 ms control period. Returns (highest duty %, mean heater watts)."""
        firmware = self.firmware
        highest = 0
        energy = 0.0
        for tick in range(100):
            if tip:
                firmware.adc[TIP_CHANNEL] = self.count_for(tip.heater)
            firmware.irq(*TIM3)
            if tick % 10 == 9:
                firmware.irq(*TIM4)
            firmware.run(MAIN_LOOP_STEP)
            compare = min(self._word(HEATER_COMPARE), HEATER_PERIOD)
            highest = max(highest, compare)
            if tip:
                energy += tip.step(1e-4, self.volts, compare / HEATER_PERIOD) * 1e-4
        return highest, energy / CONTROL_PERIOD

    def wait(self, seconds, tip=None):
        for _ in range(round(seconds / CONTROL_PERIOD)):
            self.period(tip)

    def heating(self):
        state = self.runtime + self.model["heat_state"]
        return self.firmware.uc.mem_read(state, 1)[0] == 0

    def press_ok(self, seconds=0.1):
        """A short press of OK: the first leaves the start screen with the iron asleep,
        the next wakes it and heating starts. (Held 1 s it changes the preset, 1.5 s
        puts the iron to sleep, 2.5 s opens the menu.)"""
        self.firmware.uc.mem_write(GPIOB_INPUT, struct.pack("<I", 0xFFFF & ~OK_BUTTON))
        self.wait(seconds)
        self.firmware.uc.mem_write(GPIOB_INPUT, struct.pack("<I", 0xFFFF))

    def set_word(self, base, offset, value):
        self.firmware.uc.mem_write(base + offset, struct.pack("<I", value))

    def row(self, time, tip, duty, watts):
        firmware, runtime = self.firmware, self.runtime
        return dict(
            t=time,
            heater_c=tip.heater,
            tip_c=tip.body,
            measured_c=firmware.double(runtime + 0x30)
            + firmware.double(runtime + 0x38),
            shown_c=firmware.double(runtime + 0x20),
            setpoint_c=self._word(runtime),
            duty=duty,
            watts=watts,
            output=firmware.double(self.model["output"]),
            load_w=tip.load,
        )


def simulate(path, seconds, volts=20.0, setpoint=None, power=None, loads=(), tip=None):
    loop = ControlLoop(path, volts)
    plant = Tip(**{**loop.model["tip"], **(tip or {})})
    loop.press_ok()  # leave the start screen; the iron is asleep
    loop.wait(0.5)
    if power:
        loop.set_word(loop.settings, POWER_SETTING, power)
    if setpoint:
        loop.set_word(loop.runtime, WORKING_SETPOINT, setpoint)
    loop.press_ok()  # wake: heating starts a few periods after the release
    rows = []
    for index in range(round(seconds / CONTROL_PERIOD)):
        time = index * CONTROL_PERIOD
        plant.load = sum(watts for start, stop, watts in loads if start <= time < stop)
        rows.append(loop.row(time, plant, *loop.period(plant)))
        if index == 20 and not loop.heating():
            sys.exit("the firmware did not enter its heating state")
    return loop, rows


def plot(rows, title, path):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    time = [row["t"] for row in rows]
    figure, (top, bottom) = plt.subplots(
        2, 1, figsize=(10, 6.5), sharex=True, height_ratios=(3, 2)
    )
    top.plot(time, [row["setpoint_c"] for row in rows], "k--", lw=1, label="setpoint")
    top.plot(time, [row["heater_c"] for row in rows], label="thermocouple (model)")
    top.plot(time, [row["tip_c"] for row in rows], label="tip body (model)")
    top.plot(time, [row["shown_c"] for row in rows], label="shown on the display")
    top.set_ylabel("°C")
    top.set_title(title)
    top.legend(loc="lower right")
    top.grid(alpha=0.3)
    bottom.plot(time, [row["watts"] for row in rows], color="tab:red", label="heater W")
    bottom.plot(time, [row["load_w"] for row in rows], color="tab:gray", label="load W")
    bottom.set_ylabel("W")
    bottom.set_xlabel("s since the OK button was released")
    bottom.legend(loc="upper right")
    bottom.grid(alpha=0.3)
    figure.tight_layout()
    figure.savefig(path, dpi=110)


def parse_load(text):
    start, stop, watts = (float(part) for part in text.split(":"))
    return start, stop, watts


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("firmware", help="stock APP_HS_02x_V1.8 image")
    parser.add_argument("--seconds", type=float, default=20.0)
    parser.add_argument("--volts", type=float, default=20.0, help="supply voltage")
    parser.add_argument("--setpoint", type=int, help="°C (firmware default: 350)")
    parser.add_argument("--power", type=int, help="power setting, 1-100 (default 65)")
    parser.add_argument(
        "--load",
        type=parse_load,
        action="append",
        default=[],
        metavar="START:STOP:WATTS",
        help="heat drawn from the tip body between two times; may be repeated",
    )
    for name in ("c_heater", "c_tip", "r_heater_tip", "r_tip_air", "ohms"):
        parser.add_argument(f"--{name}", type=float, help="override the assumed tip")
    parser.add_argument("--csv", help="write every control period to this file")
    parser.add_argument("--plot", help="write a PNG (needs matplotlib)")
    args = parser.parse_args()

    names = ("c_heater", "c_tip", "r_heater_tip", "r_tip_air", "ohms")
    tip = {name: getattr(args, name) for name in names if getattr(args, name)}
    loop, rows = simulate(
        args.firmware,
        args.seconds,
        args.volts,
        args.setpoint,
        args.power,
        args.load,
        tip,
    )
    name = loop.firmware.model["name"]
    assumed = ", ".join(f"{k}={v:g}" for k, v in {**loop.model["tip"], **tip}.items())
    print(f"{name} at {args.volts:g} V, assumed tip: {assumed}")
    print("    t  thermocouple  tip body  measured  shown  setpoint  duty%  heater W")
    for row in rows[:: max(1, len(rows) // 40)]:
        print(
            f"{row['t']:5.2f}  {row['heater_c']:12.1f}  {row['tip_c']:8.1f}  "
            f"{row['measured_c']:8.1f}  {row['shown_c']:5.0f}  {row['setpoint_c']:8d}  "
            f"{row['duty']:5d}  {row['watts']:8.1f}"
        )
    if args.csv:
        with open(args.csv, "w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
    if args.plot:
        plot(
            rows,
            f"Stock {name} V1.8 in the loop, {args.volts:g} V ({assumed})",
            args.plot,
        )


if __name__ == "__main__":
    main()
