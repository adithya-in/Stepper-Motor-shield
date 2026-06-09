# Stepper Motor Shield — TB6600 + PIC32MK + Encoder

PIC32MK0512MCJ064 firmware for driving a NEMA17 stepper via TB6600 driver with AS5047U magnetic encoder feedback.

## Files

| File | What |
|---|---|
| `newxc32_newfile.c` | Main firmware — stepper (OC1), QEI encoder, UART debug |
| `blink_test.c` | GPIO blink smoke test |
| `docs/TESTS.md` | Full journey log: Arduino prototyping → PIC32MK integration |
| `docs/PINOUT.md` | Wiring reference for TB6600, encoder, UART |
| `tests/arduino/` | Arduino validation test code (standalone .ino + PlatformIO) |

## Quick Pinout

| Signal | PIC32MK | TB6600 | Note |
|---|---|---|---|
| STEP | RPB15 (pin 44) | PUL+ | OC1 PWM |
| DIR | RPB14 (pin 43) | DIR+ | LOW=CW |
| ENA | RPB13 (pin 42) | ENA+ | **LOW=enable** |
| GND | GND | PUL-/DIR-/ENA- | |

## Build

Open `controller.X` in MPLAB X IDE, select the default configuration, and build/program.

## Test Sequence

The firmware runs CW 5 s → CCW 5 s → repeat, reporting RPM over UART at 19200 baud.

## Releases

- **v1.0.0** — Encoder-only firmware (AS5047U → QEI → UART)
- **v2.0.0** — Integrated stepper control (TB6600) + encoder feedback + full test documentation
- **v3.0.0** — Closed-loop position control with PI controller, fault detection, and position-control web dashboard
