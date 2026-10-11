#!/usr/bin/env python3
"""Check IronOS's HS-02 stock-curve temperature against the stock firmware itself.

Builds the real Core/BSP/Fnirsi/ThermoModel.cpp for the host (host/), runs the stock
images in stock_emu.py, and compares the tip temperature for every ADC count, several
calibrations and several values of the IronOS CJC offset setting.

    ./compare.py "path/to/stock/firmwares"

The directory must hold APP_HS_02A_V1.8_250217.bin and APP_HS_02B_V1.8_250218.bin.
"""

import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
THERMO_MODEL = HERE.parents[1] / "source/Core/BSP/Fnirsi/ThermoModel.cpp"
HANDLE_CX10 = 420  # what the MCU die sensor reads; IronOS never shows less than this
TOLERANCE_C = 1.0  # IronOS works in integer tenths of a degree, stock in doubles

# model define, stock image, calibration words, highest count to test, and how far below
# stock IronOS is meant to read
CASES = [
    # 0x8000 x3 is the "never calibrated" placeholder. On the B, IronOS deliberately leaves
    # stock's constant 25C off the nominal curve (see ThermoModel.cpp); the A build rejects
    # the placeholder and uses its measured fallback, which is not a stock curve.
    ("HS02B", "APP_HS_02B_V1.8_250218.bin", ("0x8000", "0x8000", "0x8000"), 420, 25.0),
    ("HS02B", "APP_HS_02B_V1.8_250218.bin", ("0x800c", "0x7ffa", "0x8011"), 420, 0.0),
    ("HS02A", "APP_HS_02A_V1.8_250217.bin", ("0x8001", "0x8000", "0x8000"), 1100, 0.0),
    ("HS02A", "APP_HS_02A_V1.8_250217.bin", ("0x7fe0", "0x8015", "0x7ff0"), 1100, 0.0),
]
CJC_OFFSETS_UV = (100, 900, 2500)  # setting minimum, default, maximum


def table(command):
    lines = subprocess.run(
        command, capture_output=True, text=True, check=True
    ).stdout.splitlines()
    return {
        int(line.split(",")[0]): float(line.split(",")[-1])
        for line in lines
        if line[0].isdigit()
    }


def main():
    firmware_dir = Path(sys.argv[1])
    failed = False
    with tempfile.TemporaryDirectory() as build:
        for model, image, words, top, below_stock in CASES:
            host = f"{build}/host_{model}"
            subprocess.run(
                [
                    "g++",
                    "-std=c++17",
                    "-O1",
                    f"-I{HERE}/host/shim",
                    "-DMODEL_HS02",
                    f"-DMODEL_{model}",
                ]
                + ["-o", host, f"{HERE}/host/host.cpp", str(THERMO_MODEL)],
                check=True,
            )
            emulator = [
                sys.executable,
                f"{HERE}/stock_emu.py",
                str(firmware_dir / image),
            ]
            stock = table(
                emulator + ["--counts", f"0:{top}:1", "--cal", *words, "--csv"]
            )
            want = {c: t - below_stock for c, t in stock.items()}
            # Stock A sends a count exactly equal to its 240C point down the last segment,
            # so that one count reads high. IronOS does not copy the glitch.
            glitch = int(words[1], 0) - 0x7E77 if model == "HS02A" else None
            counts = [c for c in want if want[c] > HANDLE_CX10 / 10 + 1 and c != glitch]
            for offset in CJC_OFFSETS_UV:
                ours = table(
                    [host, str(offset), str(HANDLE_CX10), *words, *map(str, stock)]
                )
                error, count = max((abs(ours[c] - want[c]), c) for c in counts)
                ok = error <= TOLERANCE_C
                failed |= not ok
                print(
                    f"{'ok  ' if ok else 'FAIL'} {model} cal {' '.join(words)} CJC offset {offset:4d} uV: "
                    f"worst difference {error:.2f} C at count {count} "
                    f"(stock {stock[count]:.2f}{f' - {below_stock:g}' if below_stock else ''}, IronOS {ours[count]:.2f})"
                )
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
