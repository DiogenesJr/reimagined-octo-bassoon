# Ultimate Gauge Board

A PlatformIO project for the Ultimate Gauge Board by Garage Tinkering, running on an ESP32-S3
with a round ST7701 display. It renders a gauge (styled after a Need for Speed Underground 2
gauge), performs a needle sweep on boot, and then tracks a value pulled in over CAN.

A compile-time simulation mode (`SIMULATE_CAN_DATA` in `src/main.cpp`, currently enabled) feeds
randomized readings instead, so the display can be exercised on the bench without a live CAN bus.

## Gauge types

Each gauge is its own PlatformIO environment. Their ranges, colors, captions and CAN settings are
in `include/gauge_config.h`.

| Environment              | Range        | Real CAN data        |
|--------------------------|--------------|----------------------|
| `coolant_temp` (default) | -20 to 140 C | ID `0x551`           |
| `oil_temp`               | 0 to 160 C   | Not yet defined      |
| `oil_pressure`           | 0 to 100 PSI | Not yet defined      |

Gauges without a CAN definition only build with simulation enabled.

## Dependencies

- **[PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)** (CLI)
  or the [PlatformIO IDE extension](https://platformio.org/platformio-ide) for VS Code, which
  bundles its own CLI.
  - On Windows, if PlatformIO's own `platformio.exe`/`pio.exe` isn't on your `PATH`, it's typically
    at `%USERPROFILE%\.platformio\penv\Scripts\`. Either add that to `PATH` or call the full path
    directly.
- **A host C/C++ compiler** (e.g. MinGW/GCC on Windows, or the system compiler on macOS/Linux) —
  only needed for the `native` unit test environment (see below), not for building firmware.
- Everything else — the ESP32-S3 Arduino framework/toolchain and the `lvgl` library — is declared
  in `platformio.ini` and downloaded automatically by PlatformIO on first build/test.
- **Windows only:** the ESP32 framework package includes deeply nested paths that can exceed
  Windows' 260-character `MAX_PATH` limit. This repo works around it via `core_dir = C:\pio` in
  `platformio.ini` (redirects PlatformIO's package cache to a short path). Alternatively/also,
  enabling [Windows long path support](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation#enable-long-paths-in-windows-10-version-1607-and-later)
  avoids the issue system-wide.

## Building

```
pio run
```

Builds the default `coolant_temp` firmware. Select another gauge with `-e`, for example
`pio run -e oil_temp`. Output lands in `.pio/build/<environment>/`, including
`firmware.factory.bin` (the combined image with bootloader + partitions + app, ready to flash from
scratch).

## Uploading to a device

With the board connected over USB:

```
pio run --target upload
```

Add `-e oil_temp` or `-e oil_pressure` to flash a different gauge.

PlatformIO auto-detects the serial port. If you have multiple serial devices connected and it
picks the wrong one:

```
pio device list
pio run --target upload --upload-port COM5
```

(substitute the correct port; on macOS/Linux this looks like `/dev/tty.usbserial-XXXX` instead of
`COMx`).

To watch serial output afterward (matches `monitor_speed = 115200` in `platformio.ini`):

```
pio device monitor
```

## Running tests

The hardware-independent gauge logic (moving-average smoothing, CAN byte decoding) lives in
`lib/GaugeLogic` and is unit-tested on your host machine — no board or CAN bus required:

```
pio test -e native
```

