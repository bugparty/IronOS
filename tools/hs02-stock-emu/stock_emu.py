#!/usr/bin/env python3
"""Run a stock FNIRSI HS-02 firmware image in Unicorn and ask it what it computes.

The stock app boots from its reset vector with every peripheral replaced by plain
memory, its timer interrupt is fired by hand, and its ADC read returns whatever count
we choose. That is enough to reach the main loop and to read the firmware's own tip
temperature for any ADC count, without owning the hardware.

    ./stock_emu.py APP_HS_02B_V1.8_250218.bin
    ./stock_emu.py APP_HS_02A_V1.8_250217.bin --counts 0:800:50
    ./stock_emu.py APP_HS_02B_V1.8_250218.bin --cal 0x8010 0x8000 0x7ff0 --csv

Needs `pip install unicorn`. The firmware images are FNIRSI's and are not in this repo.
"""

import argparse
import collections
import struct
import sys

from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn import UC_MODE_MCLASS, UC_MODE_THUMB, Uc, UcError
from unicorn.arm_const import UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR
from unicorn.arm_const import (
    UC_ARM_REG_PC,
    UC_ARM_REG_R0,
    UC_ARM_REG_SP,
    UC_CPU_ARM_CORTEX_M4,
)

APP_BASE = 0x08005800  # the first 0x5800 of flash is the bootloader, not in APP_ images
HEADER_LEN = 16  # "bin\0"/"BIN\0" + payload length, added for the MSD bootloader
RETURN_TRAP = 0x08000100  # unused bootloader space: where hand-made calls return to
TICK_VECTOR = (
    45  # IRQ 29 (TIM3), the 100 us interrupt that runs the stock tick scheduler
)
TIP_CHANNEL = 2  # ADC channel of the tip thermocouple amplifier
VIN_CHANNEL = (
    9  # ADC channel of the input voltage divider (count = V / 11 / 3.3 * 4095)
)

# Addresses in the V1.8 images (APP_HS_02A_V1.8_250217 / APP_HS_02B_V1.8_250218).
#   adc_read  one ADC conversion of the channel in r0, count returned in r0 (replaced by
#             our value for that channel)
#   temp_fn   count -> temperature, with a moving average (20 samples on B, 40 on A)
#   runtime   +0x18 averaged curve temp, +0x20 tip temperature the firmware acts on,
#             +0x30 latest curve temp, +0x38 the constant added on top (all doubles)
#   settings  RAM copy of the settings page; the three calibration words are at +0x58
MODELS = {
    b"bin\0": dict(
        name="HS-02A",
        adc_read=0x080096B4,
        temp_fn=0x08007408,
        runtime=0x20000408,
        settings=0x200004A0,
    ),
    b"BIN\0": dict(
        name="HS-02B",
        adc_read=0x080096A4,
        temp_fn=0x080073EC,
        runtime=0x200003A0,
        settings=0x20000430,
    ),
}
# GPIOA/GPIOB input data registers (+0x10 on the N32L40x, not the STM32F1's +0x08).
# Buttons are active low with pull-ups, so idle is all ones.
FIXED_INPUTS = {0x40010810: 0xFFFF, 0x40010C10: 0xFFFF}
PUSH_R4_R8_LR = bytes.fromhex("2de9f041")  # first instruction of both temp functions
# The interrupt handlers read their timer's status and interrupt-enable registers on
# every tick. Those are not wait loops and must not be flipped, or ticks go missing.
TIMER_BLOCKS = (0x40000400, 0x40000800)  # TIM3, TIM4
TIMER_STATUS = (
    0x40000410,
    0x40000810,
)  # TIM3/TIM4 status, set to "pending" before a tick


class StockFirmware:
    def __init__(self, path):
        raw = open(path, "rb").read()
        if raw[:4] not in MODELS:
            sys.exit(f"{path}: no bin/BIN header, not an HS-02 APP image")
        self.model = MODELS[raw[:4]]
        image = raw[HEADER_LEN:]
        entry = self.model["temp_fn"] - APP_BASE
        if image[entry : entry + 4] != PUSH_R4_R8_LR:
            sys.exit(
                f"{path}: not a V1.8 image (the function addresses here are V1.8's)"
            )
        self.image = image
        self.adc = {TIP_CHANNEL: 0, VIN_CHANNEL: 2256}  # count per channel; 2256 = 20 V
        self.writes = []  # (pc, address) of writes inside watch() ranges
        self._polls = collections.Counter()

        uc = self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
        uc.mem_map(0x08000000, 0x20000)
        uc.mem_write(
            0x08000000, b"\xff" * 0x20000
        )  # erased flash, incl. the settings page
        uc.mem_write(APP_BASE, image)
        uc.mem_map(0x20000000, 0x8000)
        uc.mem_map(0x40000000, 0x100000)  # peripherals
        uc.mem_map(0xE0000000, 0x100000)  # system control space
        for address, value in FIXED_INPUTS.items():
            uc.mem_write(address, struct.pack("<I", value))
        # The firmware keeps doubles in FPU registers, so the FPU must be on.
        uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))
        uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        uc.hook_add(
            UC_HOOK_MEM_READ, self._systick_read, begin=0xE000E010, end=0xE000E013
        )
        uc.hook_add(
            UC_HOOK_MEM_READ, self._peripheral_read, begin=0x40000000, end=0x400FFFFF
        )
        adc = self.model["adc_read"]
        uc.hook_add(UC_HOOK_CODE, self._adc_read, begin=adc, end=adc)

        stack, reset = struct.unpack_from("<II", image, 0)
        self.tick_handler = struct.unpack_from("<I", image, 4 * TICK_VECTOR)[0]
        uc.reg_write(UC_ARM_REG_SP, stack)
        self._pc = reset

    # -- peripheral stand-ins ---------------------------------------------------------
    def _systick_read(self, uc, access, address, size, value, _):
        # COUNTFLAG always set, so every SysTick delay loop returns at once.
        uc.mem_write(0xE000E010, struct.pack("<I", 0x10001))

    def _peripheral_read(self, uc, access, address, size, value, _):
        # A status register read again and again from one place is a wait loop. Flip what
        # it reads every 64 polls, so "wait until set" and "wait until clear" both pass.
        address &= ~3
        if address in FIXED_INPUTS or address & ~0x3FF in TIMER_BLOCKS:
            return
        key = (uc.reg_read(UC_ARM_REG_PC), address)
        self._polls[key] += 1
        if self._polls[key] % 64 == 0:
            current = struct.unpack("<I", uc.mem_read(address, 4))[0]
            uc.mem_write(address, struct.pack("<I", 0 if current else 0xFFFFFFFF))

    def _adc_read(self, uc, address, size, _):
        uc.reg_write(UC_ARM_REG_R0, self.adc.get(uc.reg_read(UC_ARM_REG_R0), 0))
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    @property
    def adc_count(self):
        return self.adc[TIP_CHANNEL]

    @adc_count.setter
    def adc_count(self, count):
        self.adc[TIP_CHANNEL] = count

    # -- running ----------------------------------------------------------------------
    def run(self, instructions):
        """Run the main thread (boot, then the main loop) for a number of instructions."""
        self.uc.emu_start(self._pc | 1, 0, count=instructions)
        self._pc = self.uc.reg_read(UC_ARM_REG_PC)

    def call(self, function, limit=2_000_000):
        """Call a firmware function (or interrupt handler) on top of the main thread."""
        uc = self.uc
        context = uc.context_save()
        uc.reg_write(UC_ARM_REG_LR, RETURN_TRAP | 1)
        uc.emu_start(function | 1, RETURN_TRAP, count=limit)
        returned = uc.reg_read(UC_ARM_REG_PC) == RETURN_TRAP
        uc.context_restore(context)
        if not returned:
            raise RuntimeError(f"call to {function:#x} did not return")

    def tick(self):
        for status in TIMER_STATUS:
            self.uc.mem_write(status, struct.pack("<I", 0xFFFF))
        self.call(self.tick_handler & ~1)

    def irq(self, vector, status_register):
        """Run one interrupt handler with its timer's update flag set."""
        self.uc.mem_write(status_register, struct.pack("<I", 0xFFFF))
        self.call(struct.unpack_from("<I", self.image, 4 * vector)[0] & ~1)

    def watch(self, start, length):
        def on_write(uc, access, address, size, value, _):
            self.writes.append((uc.reg_read(UC_ARM_REG_PC), address))

        self.uc.hook_add(
            UC_HOOK_MEM_WRITE, on_write, begin=start, end=start + length - 1
        )

    # -- state ------------------------------------------------------------------------
    def double(self, address):
        return struct.unpack("<d", self.uc.mem_read(address, 8))[0]

    def set_calibration(self, words):
        for i, word in enumerate(words):
            self.uc.mem_write(
                self.model["settings"] + 0x58 + 4 * i, struct.pack("<I", word)
            )

    def calibration(self):
        return struct.unpack("<3I", self.uc.mem_read(self.model["settings"] + 0x58, 12))

    def temperature_for(self, count):
        """(curve temperature, tip temperature the firmware acts on) for a steady ADC count."""
        runtime = self.model["runtime"]
        self.adc_count = count
        for _ in range(64):  # longer than the moving average (20 samples on B, 40 on A)
            self.call(self.model["temp_fn"])
        for _ in range(3):  # let the main loop pick the new average up
            self.tick()
            self.run(200_000)
        return self.double(runtime + 0x18), self.double(runtime + 0x20)


def parse_counts(text):
    if ":" in text:
        start, stop, step = (int(part, 0) for part in text.split(":"))
        return list(range(start, stop + 1, step))
    return [int(part, 0) for part in text.split(",")]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "firmware", help="stock APP_HS_02x_V1.8 image (with its 16-byte header)"
    )
    parser.add_argument(
        "--counts", default="0:260:20", help="ADC counts: start:stop:step or a,b,c"
    )
    parser.add_argument(
        "--cal", nargs=3, metavar="WORD", help="the three stored calibration words"
    )
    parser.add_argument(
        "--csv", action="store_true", help="print CSV instead of a table"
    )
    args = parser.parse_args()

    firmware = StockFirmware(args.firmware)
    runtime = firmware.model["runtime"]
    firmware.watch(runtime + 0x38, 8)
    try:
        firmware.run(30_000_000)  # boot into the main loop
        if args.cal:
            firmware.set_calibration([int(word, 0) for word in args.cal])
        rows = [
            (count, *firmware.temperature_for(count))
            for count in parse_counts(args.counts)
        ]
    except UcError as error:
        sys.exit(
            f"emulation stopped at {firmware.uc.reg_read(UC_ARM_REG_PC):#x}: {error}"
        )

    if args.csv:
        print("count,curve_c,tip_c")
        for count, curve, tip in rows:
            print(f"{count},{curve:.2f},{tip:.2f}")
        return
    words = " ".join(f"{word:#06x}" for word in firmware.calibration())
    # The C runtime zeroes RAM in two halves, then the firmware's own init stores the constant.
    late_writers = sorted({f"{pc:#x}" for pc, _ in firmware.writes[3:]})
    print(f"{firmware.model['name']}  calibration words {words}")
    print(f"constant added to the curve: {firmware.double(runtime + 0x38):g} C", end="")
    print(
        f" (rewritten later by {', '.join(late_writers)})"
        if late_writers
        else " (never rewritten after init)"
    )
    print(" count   curve C    tip C")
    for count, curve, tip in rows:
        print(f"  {count:4d}   {curve:7.2f}  {tip:7.2f}")


if __name__ == "__main__":
    main()
