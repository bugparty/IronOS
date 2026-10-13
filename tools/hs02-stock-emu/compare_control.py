#!/usr/bin/env python3
"""Run the stock HS-02 controller and this fork's PID on the same simulated tip.

The stock side is control_sim.py: the vendor firmware in Unicorn. The fork's side is
the real Core/Threads/PIDThread.cpp and Core/Src/power.cpp, built for the host with
the real Fnirsi configuration.h (host/pid_host.cpp), stepped at the firmware's 20 Hz
frame. Both get the same tip model, the same ADC resolution and the same scenario:
heat up from cold, then press the tip on a large cold joint.

    ./compare_control.py APP_HS_02A_V1.8_250217.bin --plot a.png
    ./compare_control.py APP_HS_02B_V1.8_250218.bin --touch 12:22 --ironos-limit 65

The tip and the joint are assumptions (see control_sim.py). Read the result as "how do
the two control laws differ on the same plant", not as a prediction for a real iron.
"""

import argparse
import subprocess
import tempfile
from pathlib import Path

import control_sim
from control_sim import MODELS, Tip, Workpiece

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parents[1] / "source"
# Core/BSP/Fnirsi: a 20 Hz frame of 400 ticks. The heater may run for 395 of them, the
# tip is sampled 3 ticks after it stops, and the new level applies from the next frame.
FRAME_TICKS, HEAT_TICKS, SAMPLE_TICK = 400, 395, 398
TICK = 125e-6
FRAME = FRAME_TICKS * TICK
FAST_PWM_PERIOD = 128  # TIM1; setTipPWM() scales the level to 0..127 of it


class IronOS:
    """This fork's PID task, running in a host process."""

    def __init__(self, name, build_dir, volts=20.0, limit=None):
        define = {"HS-02A": "MODEL_HS02A", "HS-02B": "MODEL_HS02B"}[name]
        binary = f"{build_dir}/pid_{define}"
        subprocess.run(
            ["g++", "-std=c++17", "-O1", f"-I{HERE}/host/pid_shim"]
            + [f"-I{SOURCE}/Core/Inc", f"-I{SOURCE}/Core/BSP/Fnirsi"]
            + [
                "-DMODEL_HS02",
                f"-D{define}",
                "-o",
                binary,
                f"{HERE}/host/pid_host.cpp",
            ],
            check=True,
        )
        self.process = subprocess.Popen(
            [binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True
        )
        keys = ("kp", "ki", "kd", "integral_scale", "default_limit", "ohms_x10")
        self.config = dict(zip(keys, map(int, self.process.stdout.readline().split())))
        self.limit = limit or self.config["default_limit"]
        self.volts = volts
        self.curve = MODELS[name]["curve"]
        self.time_ms = 0

    def reading(self, tip_c):
        """The tip temperature as an ADC with the stock firmware's resolution sees it."""
        curve_c = max(tip_c - 25.0, 0.0)
        for (c0, t0), (c1, t1) in zip(self.curve, self.curve[1:]):
            if curve_c <= t1:
                break
        count = round(c0 + (curve_c - t0) * (c1 - c0) / (t1 - t0))
        return 25.0 + t0 + (count - c0) * (t1 - t0) / (c1 - c0)

    def frame(self, tip, setpoint):
        """Run one 50 ms frame. Returns (level 0..395, mean heater watts, reading)."""
        level = int(self.process.stdout.readline())
        duty = (level * (FAST_PWM_PERIOD - 1) // (HEAT_TICKS - 1)) / FAST_PWM_PERIOD
        energy = 0.0
        for tick in range(FRAME_TICKS):
            energy += (
                tip.step(TICK, self.volts, duty if tick < HEAT_TICKS else 0.0) * TICK
            )
            if tick == SAMPLE_TICK:
                reading = self.reading(tip.heater)
        self.time_ms += round(FRAME * 1000)
        self.process.stdin.write(
            f"{self.time_ms} {setpoint} {round(reading * 10)} "
            f"{round(self.volts * 10)} {self.limit}\n"
        )
        self.process.stdin.flush()
        return level, energy / FRAME, reading

    def close(self):
        self.process.stdin.close()
        self.process.wait()


def simulate_ironos(name, seconds, setpoint, volts, limit, loads, touches, tip):
    with tempfile.TemporaryDirectory() as build_dir:
        controller = IronOS(name, build_dir, volts, limit)
        plant = Tip(**{**MODELS[name]["tip"], **(tip or {})})
        plant.workpiece = Workpiece() if touches else None
        for _ in range(60):  # idle first: the task seeds its filters before it controls
            controller.frame(plant, 0)
        rows = []
        for index in range(round(seconds / FRAME)):
            time = index * FRAME
            plant.at(time, loads, touches)
            level, watts, reading = controller.frame(plant, setpoint)
            rows.append(
                dict(
                    t=time,
                    heater_c=plant.heater,
                    tip_c=plant.body,
                    measured_c=reading,
                    setpoint_c=setpoint,
                    duty=round(100 * level / HEAT_TICKS),
                    watts=watts,
                    load_w=plant.drawn,
                    work_c=plant.workpiece.temp if plant.workpiece else plant.ambient,
                )
            )
        controller.close()
        return controller, rows


def first_time(rows, key, level, start=0.0, rising=True):
    for row in rows:
        if row["t"] >= start and (row[key] >= level) == rising:
            return row["t"] - start
    return None


def mean(rows, key, start, stop):
    values = [row[key] for row in rows if start <= row["t"] < stop]
    return sum(values) / len(values)


def summary(rows, setpoint, touches):
    def text(seconds):
        return "never" if seconds is None else f"{seconds:.1f} s"

    heat_up = [row for row in rows if row["t"] < (touches[0][0] if touches else 1e9)]
    idle_stop = touches[0][0] if touches else rows[-1]["t"]
    idle = [row for row in rows if idle_stop - 3 <= row["t"] < idle_stop]

    def swing(key):
        return max(row[key] for row in idle) - min(row[key] for row in idle)

    lines = [
        f"  thermocouple within 10 C of {setpoint}: {text(first_time(rows, 'heater_c', setpoint - 10))}",
        f"  tip body reaches {setpoint - 30} C: {text(first_time(rows, 'tip_c', setpoint - 30))}",
        f"  highest thermocouple during heat-up: {max(r['heater_c'] for r in heat_up):.0f} C",
        f"  idle swing over 3 s: thermocouple {swing('heater_c'):.1f} C, "
        f"tip body {swing('tip_c'):.2f} C, heater {swing('watts'):.0f} W",
    ]
    if touches:
        start, stop = touches[0]
        idle = mean(rows, "heater_c", start - 2, start)
        lines.append(
            f"  before the joint: thermocouple {idle:.0f} C, "
            f"tip body {mean(rows, 'tip_c', start - 2, start):.0f} C, "
            f"heater {mean(rows, 'watts', start - 2, start):.1f} W"
        )
        during = [row for row in rows if start <= row["t"] < stop]
        lines += [
            f"  joint reaches 183 C (solder melts): {text(first_time(rows, 'work_c', 183, start))}",
            f"  joint reaches 220 C (solder flows): {text(first_time(rows, 'work_c', 220, start))}",
            f"  lowest tip body on the joint: {min(r['tip_c'] for r in during):.0f} C",
            f"  heater power, first 3 s on the joint: {mean(rows, 'watts', start, start + 3):.0f} W, "
            f"highest {max(r['watts'] for r in during):.0f} W",
            f"  end of the joint: thermocouple {mean(rows, 'heater_c', stop - 1, stop):.0f} C, "
            f"tip body {mean(rows, 'tip_c', stop - 1, stop):.0f} C, "
            f"joint {mean(rows, 'work_c', stop - 1, stop):.0f} C",
            f"  highest thermocouple after lifting off: "
            f"{max(r['heater_c'] for r in rows if r['t'] >= stop):.0f} C",
        ]
    return "\n".join(lines)


def plot(stock, ours, title, labels, path):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figure, axes = plt.subplots(
        3, 1, figsize=(11, 9.5), sharex=True, height_ratios=(3, 3, 2)
    )
    sensor, body, power = axes
    runs = ((stock, "tab:red", labels[0]), (ours, "tab:blue", labels[1]))
    time = [row["t"] for row in stock]
    sensor.plot(
        time, [row["setpoint_c"] for row in stock], "k--", lw=1, label="setpoint"
    )
    for rows, colour, label in runs:
        time = [row["t"] for row in rows]
        sensor.plot(time, [row["heater_c"] for row in rows], color=colour, label=label)
        body.plot(
            time,
            [row["tip_c"] for row in rows],
            color=colour,
            label=f"tip body, {label}",
        )
        body.plot(
            time,
            [row["work_c"] for row in rows],
            color=colour,
            ls=":",
            label=f"joint, {label}",
        )
        power.plot(time, [row["watts"] for row in rows], color=colour, label=label)
    time = [row["t"] for row in stock]
    sensor.plot(
        time, [row["shown_c"] for row in stock], color="tab:red", ls=":", lw=1,
        label="stock display",
    )  # fmt: skip
    body.axhline(183, color="gray", lw=0.8, ls="--")
    sensor.set_ylabel("thermocouple, °C")
    body.set_ylabel("°C")
    power.set_ylabel("heater W")
    power.set_xlabel("s since heating started")
    sensor.set_title(title)
    for axis in axes:
        axis.grid(alpha=0.3)
        axis.legend(
            loc="lower right" if axis is not power else "upper right", fontsize=8
        )
    low = min(row["heater_c"] for rows, _, _ in runs for row in rows if row["t"] > 6)
    sensor.set_ylim(min(low - 15, 300), None)
    figure.tight_layout()
    figure.savefig(path, dpi=110)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("firmware", help="stock APP_HS_02x_V1.8 image")
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("--volts", type=float, default=20.0)
    parser.add_argument("--setpoint", type=int, default=350)
    parser.add_argument(
        "--stock-power", type=int, help="stock power setting (default 65)"
    )
    parser.add_argument("--ironos-limit", type=int, help="this fork's power limit, W")
    parser.add_argument(
        "--touch",
        type=control_sim.parse_touch,
        action="append",
        metavar="START:STOP",
        help="press the tip on a large cold joint (default 12:22)",
    )
    parser.add_argument("--no-touch", action="store_true", help="heat-up only")
    for name in ("c_heater", "c_tip", "r_heater_tip", "r_tip_air", "ohms"):
        parser.add_argument(f"--{name}", type=float, help="override the assumed tip")
    parser.add_argument("--plot", help="write a PNG (needs matplotlib)")
    args = parser.parse_args()

    names = ("c_heater", "c_tip", "r_heater_tip", "r_tip_air", "ohms")
    tip = {name: getattr(args, name) for name in names if getattr(args, name)}
    touches = [] if args.no_touch else (args.touch or [(12.0, 22.0)])
    scenario = (args.seconds, args.volts, args.setpoint)

    loop, stock = control_sim.simulate(
        args.firmware, *scenario, args.stock_power, (), touches, tip
    )
    name = loop.firmware.model["name"]
    controller, ours = simulate_ironos(
        name,
        args.seconds,
        args.setpoint,
        args.volts,
        args.ironos_limit,
        (),
        touches,
        tip,
    )

    config = controller.config
    assumed = ", ".join(f"{k}={v:g}" for k, v in {**MODELS[name]["tip"], **tip}.items())
    labels = (
        f"stock V1.8, power {args.stock_power or 65}",
        f"this fork, limit {controller.limit} W",
    )
    print(
        f"{name} at {args.volts:g} V, setpoint {args.setpoint} C. Assumed tip: {assumed}"
    )
    print(
        f"This fork: Kp {config['kp']} Ki {config['ki']} Kd {config['kd']}, power limit "
        f"{controller.limit} W, assumes a {config['ohms_x10'] / 10:g} ohm heater"
    )
    for label, rows in zip(labels, (stock, ours)):
        print(f"{label}:\n{summary(rows, args.setpoint, touches)}")
    if args.plot:
        plot(
            stock,
            ours,
            f"{name}, {args.volts:g} V: stock vs this fork ({assumed})",
            labels,
            args.plot,
        )


if __name__ == "__main__":
    main()
