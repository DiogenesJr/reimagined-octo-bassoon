# CAN test sender

Sends test messages from a PC to a gauge through a USB-CAN adapter running gs_usb (candleLight)
firmware, such as the RH-02.

**Bench use only.** Do not connect the adapter to a vehicle while sending.

## Setup

Run these from the repository root (Windows PowerShell, Python 3):

```
python -m venv tools\can_sim\.venv
tools\can_sim\.venv\Scripts\python.exe -m pip install -r tools\can_sim\requirements.txt
```

Check that the adapter opens (no wiring needed, nothing is sent):

```
tools\can_sim\.venv\Scripts\python.exe tools\can_sim\can_sim.py check
```

## Wiring

- CANH to CANH, CANL to CANL, ground to ground.
- 500 kbit/s.
- 120 ohm termination at both ends. With power off, CANH to CANL should measure about 60 ohm.

## Sending

Each gauge has a firmware environment that listens to the bus:

| Gauge        | Environment        | CAN ID  | Encoding   | Source          |
|--------------|--------------------|---------|------------|-----------------|
| Coolant temp | `coolant_temp_can` | `0x345` | °C + 40    | GR86 / BRZ      |
| Oil temp     | `oil_temp_can`     | `0x345` | °C + 40    | GR86 / BRZ      |
| Oil pressure | `oil_pressure_can` | `0x601` | PSI        | **Placeholder** |

Both temperatures travel in one message: the fourth byte is oil and the fifth is coolant. The
definition comes from the [ft86 CAN notes](https://github.com/timurrrr/ft86/blob/main/can_bus/gen2.md)
for the 2022+ Toyota GR86 / Subaru BRZ. For oil pressure, the table shows the first byte; it is a
placeholder for bench testing and won't read anything from a car.

Flash one, for example:

```
pio run -e coolant_temp_can --target upload
```

Then pick the matching gauge name (`coolant_temp`, `oil_temp` or `oil_pressure`) and a scenario:

```
tools\can_sim\.venv\Scripts\python.exe tools\can_sim\can_sim.py send coolant_temp hold --value 90
```

| Scenario | What it sends                                                   |
|----------|-----------------------------------------------------------------|
| `hold`   | One value (`--value`) until Ctrl+C or `--duration`              |
| `steps`  | Jumps between fixed points, 4 seconds each                      |
| `sweep`  | Minimum to maximum and back over 20 seconds                     |
| `warmup` | A compressed 110 second start and drive (made up)               |

Without `--value`, `hold` sends 90 °C for coolant, 100 °C for oil temperature or 45 PSI.

Add `--dry-run` to preview messages without the adapter. Messages go out every 0.1 s unless
`--period` says otherwise.

`acked` in the output counts messages the adapter reports as sent. If it stays at 0, check wiring,
termination, bit rate and gauge power.

## Replaying a log from the car

`replay` sends the recorded `0x345` messages from a SavvyCAN CSV export with their original timing,
which exercises both temperature gauges. It skips every other message, so it does not simulate a
busy bus, and it cannot test oil pressure.

```
tools\can_sim\.venv\Scripts\python.exe tools\can_sim\can_sim.py replay path\to\log.csv --speed 5
```

`--speed` goes up to 10. Add `--dry-run` to preview.

## Adding a gauge

Add an entry to `SIGNALS` in `can_sim.py`. Its CAN ID, range and encoding must match
`include/gauge_config.h`.
