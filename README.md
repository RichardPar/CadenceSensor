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
- Stepper motor driver for magnetic brake positioning (ready, not yet active)
- FTMS BLE service (UUID 0x1826) for ERG / target-power control (ready, not yet active)

---

## Hardware

### Components

| Qty | Item |
|-----|------|
| 1 | ESP32-WROOM-32 dev board |
| 2 | SS41F unipolar Hall-effect switch |
| 2 | Small neodymium magnet (one per sensing location) |
| 1 | Stepper motor (e.g. NEMA 17) |
| 1 | A4988 or DRV8825 stepper driver module |
| 1 | 12 V power supply for motor (current rating ≥ motor rated current) |
| 1 | 100 µF electrolytic capacitor (across VMOT/GND on driver) |
| 1 | NO microswitch — homing limit switch *(optional but recommended)* |

The SS41F has an active-low, open-collector output.  The ESP32's internal
pull-up resistors are enabled in firmware, so no external resistors are
required.  For cable runs longer than ~30 cm an external 4.7 kΩ pull-up to
3.3 V is recommended.

### Hall sensor wiring

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

### Stepper motor wiring (A4988 / DRV8825)

The driver sits between the ESP32 and the stepper motor coils.  Logic-side
signals run at 3.3 V; motor power (VMOT) is typically 12 V and must be
supplied separately.

```
ESP32              A4988 / DRV8825        Stepper motor
─────              ───────────────        ─────────────
3V3  ──────────── VDD   (logic power)
GND  ──────────── GND   (logic ground)
GPIO 25 ─────────  STEP
GPIO 26 ─────────  DIR
                   SLEEP ── 3V3           (keep HIGH to enable driver)
                   RESET ── 3V3           (keep HIGH to enable driver)

External supply:
12 V ─────────── VMOT   (motor power — add 100 µF cap close to driver)
GND  ──────────── GND   (shared ground)

                   1A ────────────────── Coil A+
                   1B ────────────────── Coil A−
                   2A ────────────────── Coil B+
                   2B ────────────────── Coil B−
```

> **Current limit:** Set the A4988/DRV8825 Vref trimmer before powering the
> motor.  Exceeding the motor's rated current will cause overheating.

> **Microstepping:** MS1/MS2/MS3 pins are left unconnected (full-step mode by
> default on A4988).  Tie them to 3V3/GND to select a finer step mode and
> update `STEPPER_MAX_STEPS` and `STEPPER_STEP_PERIOD_US` in `config.h`
> accordingly.

### Limit switch wiring

A normally-open (NO) microswitch wired active-low.  The ESP32 internal
pull-up keeps the line high; the switch pulls it low at the home end-stop.
Update `STEPPER_LIMIT_GPIO` in `config.h` once wired.

**Zero position = minimum resistance end-stop.**
The homing routine drives the motor in the `STEPPER_DIR_DECREASE` direction
(magnet moving *away* from the flywheel) until the switch triggers, then sets
the step counter to 0.  Place the limit switch so it fires when the magnet
assembly is at its furthest point from the flywheel — i.e. the lightest
possible resistance.  Higher step counts move the magnet *toward* the flywheel,
increasing resistance up to `STEPPER_MAX_STEPS`.

```
Resistance:   MIN ──────────────────────────────── MAX
Step count:    0                                  STEPPER_MAX_STEPS
               ▲
          limit switch here
          (home / zero position)
```

```
ESP32                        Limit switch (NO microswitch)
─────                        ─────────────────────────────
GPIO 27 ──┬──────────────── COM  (common)
          │                 NO   (normally open) ── GND
          └── (internal pull-up enabled in firmware)
```

```c
// config.h — change -1 to the chosen GPIO
#define STEPPER_LIMIT_GPIO   27
```

### Magnet placement

- **Wheel** — attach one magnet to a spoke; mount the sensor on a fixed part
  of the fork so the magnet passes within ~5 mm of the sensor face.
- **Crank** — attach one magnet to the crank arm; mount the sensor on the
  chainstay.

---

## Configuration

All user-configurable constants are in [`main/config.h`](main/config.h):

```c
/* GPIO pins — hall sensors */
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

/* Stepper motor — step/dir interface (A4988 / DRV8825) */
#define STEPPER_STEP_GPIO       25
#define STEPPER_DIR_GPIO        26
#define STEPPER_LIMIT_GPIO      -1   /* set to a GPIO if a limit switch is fitted */
#define STEPPER_MAX_STEPS       2000 /* tune to match physical travel */
#define STEPPER_STEP_PERIOD_US  500  /* ~2000 steps/s */
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
    ├── ble_csc.h/.c        NimBLE GATT server — CSC Profile (active)
    ├── stepper.h/.c        Stepper motor driver — magnetic brake (inactive)
    └── ble_ftms.h/.c       NimBLE GATT server — FTMS / ERG profile (inactive)
```

---

## ERG mode roadmap

The codebase already contains the BLE and motor scaffolding for ERG mode.
The remaining work is:

1. **Resistance hardware** — wire the stepper motor and driver (A4988/DRV8825)
   to the GPIOs defined in `config.h`.  Add a limit switch if possible and set
   `STEPPER_LIMIT_GPIO` accordingly.
2. **Activate the stepper** — call `stepper_init()` and `stepper_home()` in
   `main.c` during startup.
3. **Power estimation** — characterise the brake (measured watts vs. step
   position at several speeds) and store a lookup table in a new
   `brake_control.c`.
4. **Activate FTMS** — call `ble_ftms_register_services()` during BLE init
   so apps can send target power via the Fitness Machine Control Point
   (opcode `0x05`).
5. **PID control loop** — in the main task, read `ble_ftms_get_target_power()`,
   compare to estimated actual watts, and drive `stepper_move_to()` to close
   the loop.

---

## Licence

MIT — see [LICENSE](LICENSE).  Copyright © 2026 Richard Parsons.

