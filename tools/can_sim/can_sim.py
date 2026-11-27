"""Send test messages to a gauge through a gs_usb (candleLight) USB-CAN adapter.

For bench use only. Do not run this against a bus connected to a vehicle.
"""

import argparse
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


def encode_temperature(deg_c: int) -> bytes:
    # byte 0 = deg C + 40, matching process_scale_value in lib/GaugeLogic.
    # The gauge ignores the other bytes.
    return bytes([deg_c + 40, 0, 0, 0, 0, 0, 0, 0])


def encode_oil_pressure(psi: int) -> bytes:
    # byte 0 = PSI, matching process_oil_pressure_value in lib/GaugeLogic
    return bytes([psi, 0, 0, 0, 0, 0, 0, 0])


# Names match the PlatformIO environments. IDs, ranges and formulas must agree
# with include/gauge_config.h.
SIGNALS = {
    "coolant_temp": Signal(
        can_id=0x551,
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
        encode=encode_temperature,
    ),
    # PLACEHOLDER message, not from a real vehicle
    "oil_temp": Signal(
        can_id=0x600,
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
        encode=encode_temperature,
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
    report_every = max(1, round(1.0 / args.period))

    if args.dry_run:
        for i, value in enumerate(values):
            if i % report_every == 0:
                data = signal.encode(value).hex(" ").upper()
                print(f"t={i * args.period:6.1f}s  value={value:4d}  0x{signal.can_id:03X}  {data}")
            if args.scenario == "hold" and args.duration is None and i >= 2 * report_every:
                break
        return 0

    sent = acked = 0
    warned = False
    with open_bus(args.bitrate) as bus:
        print(f"Sending {args.gauge} '{args.scenario}' on 0x{signal.can_id:03X}. Ctrl+C to stop.")
        start = time.monotonic()
        try:
            for i, value in enumerate(values):
                message = can.Message(arbitration_id=signal.can_id,
                                      data=signal.encode(value), is_extended_id=False)
                bus.send(message)
                sent += 1
                acked += count_acks(bus, signal.can_id, start + (i + 1) * args.period)
                if i % report_every == 0:
                    print(f"t={i * args.period:6.1f}s  value={value:4d}  sent={sent}  acked={acked}")
                if not warned and acked == 0 and time.monotonic() - start > NO_ACK_WARNING_SECONDS:
                    print("Warning: nothing is acknowledging. Check wiring, termination, "
                          "bit rate and that the gauge is powered.")
                    warned = True
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

    args = parser.parse_args()
    try:
        return check(args) if args.command == "check" else send(args)
    except can.CanInitializationError as error:
        print(f"Could not open the adapter: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
