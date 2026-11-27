"""Send test messages to a gauge through a gs_usb (candleLight) USB-CAN adapter.

For bench use only. Do not run this against a bus connected to a vehicle.
"""

import argparse
import csv
import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterator

import can
import libusb_package


@dataclass(frozen=True)
class Signal:
    can_id: int
    min_value: int
    max_value: int
    hold_value: int  # default for the "hold" scenario
    step_values: tuple
    warmup: tuple  # (seconds, value) keyframes, interpolated linearly
    encode: Callable[[int], bytes]


# Message 0x345 carries both temperatures: byte 3 is oil, byte 4 is coolant, each
# deg C + 40. The one not being tested is held at a normal operating value.
TEMPERATURE_ID = 0x345
NORMAL_OIL_C = 90
NORMAL_COOLANT_C = 88


def encode_temperatures(oil_c: int, coolant_c: int) -> bytes:
    return bytes([0, 0, 0, oil_c + 40, coolant_c + 40, 0, 0, 0])


def encode_oil_temp(deg_c: int) -> bytes:
    return encode_temperatures(deg_c, NORMAL_COOLANT_C)


def encode_coolant_temp(deg_c: int) -> bytes:
    return encode_temperatures(NORMAL_OIL_C, deg_c)


def encode_oil_pressure(psi: int) -> bytes:
    # byte 0 = PSI, matching process_oil_pressure_value in lib/GaugeLogic
    return bytes([psi, 0, 0, 0, 0, 0, 0, 0])


# Names match the PlatformIO environments. IDs, ranges and formulas must agree
# with include/gauge_config.h.
SIGNALS = {
    "coolant_temp": Signal(
        can_id=TEMPERATURE_ID,
        min_value=-20,
        max_value=140,
        hold_value=90,
        step_values=(-20, 20, 60, 90, 115, 140, 90),
        # illustrative and time-compressed, not recorded from a vehicle
        warmup=(
            (0, 20),    # cold start
            (30, 70),
            (50, 92),   # thermostat opens
            (55, 85),
            (65, 90),   # cruising
            (85, 105),  # idling in traffic
            (100, 91),  # radiator fan catches up
            (110, 90),
        ),
        encode=encode_coolant_temp,
    ),
    "oil_temp": Signal(
        can_id=TEMPERATURE_ID,
        min_value=0,
        max_value=160,
        hold_value=100,
        step_values=(0, 40, 80, 100, 130, 160, 100),
        # illustrative and time-compressed
        warmup=(
            (0, 20),    # cold start
            (40, 65),
            (60, 90),
            (70, 100),  # cruising
            (85, 120),  # hard driving
            (100, 105),
            (110, 100),
        ),
        encode=encode_oil_temp,
    ),
    # PLACEHOLDER message, not from a real vehicle
    "oil_pressure": Signal(
        can_id=0x601,
        min_value=0,
        max_value=100,
        hold_value=45,
        step_values=(0, 20, 40, 60, 80, 100, 45),
        # illustrative and time-compressed
        warmup=(
            (0, 0),     # engine off
            (2, 75),    # cold start, thick oil
            (30, 60),
            (50, 45),   # oil warming
            (55, 65),   # accelerating
            (65, 50),   # cruising
            (80, 25),   # warm idle
            (90, 25),
            (95, 60),   # accelerating
            (110, 45),
        ),
        encode=encode_oil_pressure,
    ),
}

STEP_SECONDS = 4.0
SWEEP_SECONDS = 10.0  # each direction
NO_ACK_WARNING_SECONDS = 2.0


def scenario_values(signal: Signal, name: str, period: float, hold: int,
                    duration: float) -> Iterator[int]:
    """Yield one value per message period."""
    if name == "hold":
        sent = 0
        while duration is None or sent * period < duration:
            yield hold
            sent += 1
    elif name == "steps":
        for value in signal.step_values:
            for _ in range(round(STEP_SECONDS / period)):
                yield value
    elif name == "sweep":
        low, high = signal.min_value, signal.max_value
        yield from interpolate(((0, low), (SWEEP_SECONDS, high),
                                (2 * SWEEP_SECONDS, low)), period)
    elif name == "warmup":
        yield from interpolate(signal.warmup, period)


def interpolate(keyframes: tuple, period: float) -> Iterator[int]:
    index = 0
    for (t0, v0), (t1, v1) in zip(keyframes, keyframes[1:]):
        while index * period < t1:
            fraction = (index * period - t0) / (t1 - t0)
            yield round(v0 + fraction * (v1 - v0))
            index += 1
    yield keyframes[-1][1]


def open_bus(bitrate: int) -> can.BusABC:
    # pyusb looks for libusb-1.0.dll on PATH; use the copy from libusb-package
    library = libusb_package.get_library_path()
    if library:
        os.environ["PATH"] = f"{Path(library).parent}{os.pathsep}{os.environ['PATH']}"
    return can.Bus(interface="gs_usb", channel=0, index=0, bitrate=bitrate)


def check(args) -> int:
    with open_bus(args.bitrate) as bus:
        print(f"Adapter opened at {args.bitrate} bit/s. No messages were sent.")
        # DeviceInfo's own __str__ raises in gs-usb 0.3.1, so format the fields here
        info = bus.gs_usb.device_info
        print(f"Firmware version {info.fw_version / 10}, hardware version {info.hw_version / 10}")
    return 0


def count_acks(bus: can.BusABC, can_id: int, until: float) -> int:
    """Wait until the given time, counting transmit confirmations from the adapter."""
    acked = 0
    while (remaining := until - time.monotonic()) > 0:
        # the adapter library treats a timeout that rounds to 0 ms as "forever"
        message = bus.recv(timeout=max(remaining, 0.001))
        if message and not message.is_rx and message.arbitration_id == can_id:
            acked += 1
    return acked


def send(args) -> int:
    signal = SIGNALS[args.gauge]
    hold = signal.hold_value if args.value is None else args.value
    if not signal.min_value <= hold <= signal.max_value:
        print(f"--value must be from {signal.min_value} to {signal.max_value}", file=sys.stderr)
        return 2

    values = scenario_values(signal, args.scenario, args.period, hold, args.duration)
    schedule = ((i * args.period, signal.encode(value), f"value={value:4d}")
                for i, value in enumerate(values))

    if args.dry_run:
        endless = args.scenario == "hold" and args.duration is None
        return preview(signal.can_id, schedule, every=1.0, stop_after=2.0 if endless else None)

    print(f"Sending {args.gauge} '{args.scenario}' on 0x{signal.can_id:03X}. Ctrl+C to stop.")
    return transmit(args.bitrate, signal.can_id, schedule)


def read_log(path: Path) -> list:
    """Return (seconds, data) for each temperature message in a SavvyCAN CSV log."""
    frames = []
    with open(path, newline="") as handle:
        for row in csv.DictReader(handle):
            if int(row["ID"], 16) != TEMPERATURE_ID or row["Extended"] != "false":
                continue
            data = bytes(int(row[f"D{i}"], 16) for i in range(1, int(row["LEN"]) + 1))
            if len(data) >= 5:  # long enough to hold both temperatures
                frames.append((int(row["Time Stamp"]) / 1e6, data))
    return [(stamp - frames[0][0], data) for stamp, data in frames]


def replay(args) -> int:
    try:
        frames = read_log(args.log)
    except (OSError, KeyError, ValueError, TypeError) as error:
        print(f"Could not read {args.log} as a SavvyCAN CSV log: {error!r}", file=sys.stderr)
        return 2
    if not frames:
        print(f"No 0x{TEMPERATURE_ID:03X} messages in {args.log}", file=sys.stderr)
        return 2

    schedule = ((seconds / args.speed, data,
                 f"log={seconds:6.1f}s  oil={data[3] - 40:4d}  coolant={data[4] - 40:4d}")
                for seconds, data in frames)

    print(f"{len(frames)} messages on 0x{TEMPERATURE_ID:03X}, "
          f"{frames[-1][0] / args.speed:.0f}s at {args.speed:g}x speed.")
    if args.dry_run:
        return preview(TEMPERATURE_ID, schedule, every=30.0 / args.speed)

    print("Ctrl+C to stop.")
    return transmit(args.bitrate, TEMPERATURE_ID, schedule)


def preview(can_id: int, schedule, every: float, stop_after: float = None) -> int:
    """Print a sample of the messages without using the adapter."""
    next_report = 0.0
    for seconds, data, label in schedule:
        if seconds >= next_report - 1e-6:
            print(f"t={seconds:6.1f}s  {label}  0x{can_id:03X}  {data.hex(' ').upper()}")
            next_report += every
        if stop_after is not None and seconds >= stop_after:
            break
    return 0


def transmit(bitrate: int, can_id: int, schedule) -> int:
    """Send (seconds from start, data, label) entries at their scheduled times."""
    sent = acked = 0
    warned = False
    next_report = 0.0
    with open_bus(bitrate) as bus:
        start = time.monotonic()
        try:
            for seconds, data, label in schedule:
                acked += count_acks(bus, can_id, start + seconds)
                bus.send(can.Message(arbitration_id=can_id, data=data, is_extended_id=False))
                sent += 1
                if seconds >= next_report - 1e-6:
                    print(f"t={seconds:6.1f}s  {label}  sent={sent}  acked={acked}")
                    next_report += 1.0
                if not warned and acked == 0 and seconds > NO_ACK_WARNING_SECONDS:
                    print("Warning: nothing is acknowledging. Check wiring, termination, "
                          "bit rate and that the gauge is powered.")
                    warned = True
            acked += count_acks(bus, can_id, time.monotonic() + 0.2)  # last confirmations
        except KeyboardInterrupt:
            print()
        except can.CanOperationError as error:
            print(f"Send failed: {error}. Check wiring, termination, bit rate and "
                  "that the gauge is powered.", file=sys.stderr)
            return 1

    print(f"Done: {sent} sent, {acked} acknowledged.")
    return 0 if acked else 1


def positive_float(text: str) -> float:
    value = float(text)
    if value <= 0:
        raise argparse.ArgumentTypeError("must be greater than 0")
    return value


def replay_speed(text: str) -> float:
    value = positive_float(text)
    if value > 10:
        raise argparse.ArgumentTypeError("must be 10 or less")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--bitrate", type=int, default=500_000)
    commands = parser.add_subparsers(dest="command", required=True)

    commands.add_parser("check", help="open the adapter without sending anything")

    send_parser = commands.add_parser("send", help="send test messages")
    send_parser.add_argument("gauge", choices=SIGNALS)
    send_parser.add_argument("scenario", choices=("hold", "steps", "sweep", "warmup"))
    send_parser.add_argument("--value", type=int,
                             help="value for 'hold' (default depends on the gauge)")
    send_parser.add_argument("--duration", type=positive_float,
                             help="seconds to 'hold' (default: until Ctrl+C)")
    send_parser.add_argument("--period", type=positive_float, default=0.1,
                             help="seconds between messages (default: 0.1)")
    send_parser.add_argument("--dry-run", action="store_true",
                             help="print messages without using the adapter")

    replay_parser = commands.add_parser(
        "replay", help="replay the temperature messages from a SavvyCAN CSV log")
    replay_parser.add_argument("log", type=Path)
    replay_parser.add_argument("--speed", type=replay_speed, default=1.0,
                               help="playback speed, up to 10 (default: 1)")
    replay_parser.add_argument("--dry-run", action="store_true",
                               help="print messages without using the adapter")

    args = parser.parse_args()
    run = {"check": check, "send": send, "replay": replay}[args.command]
    try:
        return run(args)
    except can.CanInitializationError as error:
        print(f"Could not open the adapter: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
