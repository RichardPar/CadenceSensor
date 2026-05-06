# CadenceSensor

ESP32-WROOM firmware that reads two SS41F Hall-effect sensors (wheel & crank)
and broadcasts live cycling data over BLE using the standard **Cycling Speed
and Cadence (CSC) Profile** (Bluetooth SIG CSP_SPEC v1.0).

Compatible with Garmin, Wahoo, Zwift, MyWhoosh, Strava, and any app that supports the
standard BLE CSC service (UUID **0x1816**).

> **MyWhoosh** — confirmed working.
>
> **Zwift note:** Zwift splits speed and cadence into separate pairing slots.
> To use both channels you must pair `CadenceSensor` twice — once under
> *Speed Source* and again under *Cadence Source* in the Zwift pairing screen.

---

## Features

- Wheel and crank revolution counting via interrupt-driven Hall-effect sensors
- BLE CSC Profile — 1 Hz notifications with cumulative revolutions and event timestamps
- SC Control Point support — clients can reset the wheel revolution counter
- Instantaneous speed logged to serial (km/h), calculated from wheel pulse interval
- Configurable pins, wheel circumference, debounce time, and device name via [`main/config.h`](main/config.h)

---

## Hardware

### Components

| Qty | Item |
|-----|------|
| 1 | ESP32-WROOM-32 dev board |
| 2 | SS41F unipolar Hall-effect switch |
| 2 | Small neodymium magnet (one per sensing location) |

The SS41F has an active-low, open-collector output.  The ESP32's internal
pull-up resistors are enabled in firmware, so no external resistors are
required.  For cable runs longer than ~30 cm an external 4.7 kΩ pull-up to
3.3 V is recommended.

### Wiring

```
SS41F (wheel)          ESP32
─────────────          ─────
  VCC ───────────────  3V3
  GND ───────────────  GND
  OUT ───────────────  GPIO 18

SS41F (crank)
  VCC ───────────────  3V3
  GND ───────────────  GND
  OUT ───────────────  GPIO 19
```

The output is pulled low when the sensor detects a magnet (south pole facing
the sensor).  A falling-edge interrupt increments the revolution counter.

### Magnet placement

- **Wheel** — attach one magnet to a spoke; mount the sensor on a fixed part
  of the fork so the magnet passes within ~5 mm of the sensor face.
- **Crank** — attach one magnet to the crank arm; mount the sensor on the
  chainstay.

---

## Configuration

All user-configurable constants are in [`main/config.h`](main/config.h):

```c
/* GPIO pins */
#define HALL_WHEEL_GPIO         18
#define HALL_CRANK_GPIO         19

/* Debounce — ignore pulses closer together than this (ms) */
#define HALL_DEBOUNCE_MS        50

/* Wheel circumference — 700c road bike with ~25 mm tyre */
#define WHEEL_CIRCUMFERENCE_MM  2096

/* BLE device name */
#define DEVICE_NAME             "CadenceSensor"

/* BLE Sensor Location: 6 = Rear Wheel */
#define SENSOR_LOCATION         6U
```

Set `WHEEL_CIRCUMFERENCE_MM` to match your tyre.  Common values:

| Tyre | Circumference |
|------|--------------|
| 700c × 23 mm | 2096 mm |
| 700c × 25 mm | 2105 mm |
| 700c × 28 mm | 2136 mm |
| 650b × 47 mm | 2156 mm |
| 29" MTB × 2.1" | 2288 mm |

---

## BLE GATT profile

| Characteristic | UUID | Properties | Notes |
|---|---|---|---|
| Cycling Speed & Cadence (service) | 0x1816 | — | Primary service |
| CSC Measurement | 0x2A5B | Notify | 11-byte packet, 1 Hz |
| CSC Feature | 0x2A5C | Read | `0x0003` — wheel + crank |
| Sensor Location | 0x2A5D | Read | `6` = Rear Wheel |
| SC Control Point | 0x2A55 | Write / Indicate | Opcode `0x01` resets wheel counter |

### CSC Measurement packet layout

```
Offset  Size  Field
──────  ────  ─────────────────────────────────────────────
  0      1    Flags = 0x03 (wheel rev present, crank rev present)
  1      4    Cumulative Wheel Revolutions  (uint32 LE)
  5      2    Last Wheel Event Time         (uint16 LE, 1/1024 s)
  7      2    Cumulative Crank Revolutions  (uint16 LE)
  9      2    Last Crank Event Time         (uint16 LE, 1/1024 s)
```

Event times are 16-bit rolling counters.  Connected apps derive **speed** from
the change in wheel revolutions and event time, and **cadence (RPM)** from the
change in crank revolutions and event time between consecutive notifications.

---

## Build & flash

Requires ESP-IDF ≥ 5.0 sourced in the current shell.

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

The device advertises as **`CadenceSensor`** immediately after boot.  Serial
output logs speed and revolution counts once per second:

```
I (1234) main: Speed: 28.4 km/h  wheel_revs: 52  crank_revs: 18
```

---

## Project structure

```
CadenceSensor/
├── CMakeLists.txt          Top-level build file
├── sdkconfig.defaults      Pre-configured BLE/NimBLE settings
├── LICENSE                 MIT licence
└── main/
    ├── CMakeLists.txt
    ├── config.h            User-configurable pins and constants
    ├── main.c              Entry point — init + 1 Hz notification loop
    ├── hall_sensor.h/.c    GPIO ISR revolution counter + speed calculation
    └── ble_csc.h/.c        NimBLE GATT server implementing the CSC Profile
```

---

## Licence

MIT — see [LICENSE](LICENSE).  Copyright © 2026 Richard Parsons.

