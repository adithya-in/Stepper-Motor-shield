# Pinout Reference

## PIC32MK0512MCJ064 → TB6600 Stepper Driver

### Common-Anode Wiring

All TB6600 `-` pins are tied together to **GND**. All `+` pins are driven by PIC32MK GPIO.

**Critical**: ENA+ LOW = enabled, HIGH = disabled. Do NOT tie ENA- to ENA+ or bypass
the optocoupler — the TB6600's internal circuit needs PUL-/DIR-/ENA- as the return
path. Connecting ENA- alone without PUL-/DIR- also tied to GND will not work.

| TB6600 | PIC32MK Pin | Pin # (64-TQFP) | Notes |
|---|---|---|---|
| **PUL+** | **RPB15** (OC1) | 44 | 50% duty PWM, rate = `STEP_RATE` Hz |
| **DIR+** | **RPB14** | 43 | LOW = CW, HIGH = CCW |
| **ENA+** | **RPB13** | 42 | **LOW = enabled**, HIGH = disabled |
| PUL- | GND | | |
| DIR- | GND | | |
| ENA- | GND | | |

### Physical Pin Identifiers

Pin 44 on the 64-TQFP package is labeled as **PB15 / RB15 / PWML1** (different functions on same pad). The firmware routes OC1 output to RPB15 via PPS — no connection to the dedicated Motor Control PWM module is needed.

---

## Other Connections

| Function | PIC32MK Pin | Pin # | Notes |
|---|---|---|---|
| UART TX | **RPE0** | 1 | Debug output, 19200 baud |
| UART RX | **RPG8** | 54 | (via PPS input #1) |
| QEI A | **RPG6** | 52 | AS5047U encoder channel A |
| QEI B | **RPG7** | 53 | AS5047U encoder channel B |
| LED | **RA10** | 28 | Heartbeat/status indicator |

---

## TB6600 DIP Switch Settings

Set DIP switches to match NEMA17 motor rated current and desired microstep resolution:

| Switch | Setting | Effect |
|---|---|---|
| S1/S2/S3 | Per motor current | Match NEMA17 rating plate |
| S4/S5/S6 | Per microstep | 1/16 = all ON (3200 steps/rev) |

---

## NEMA17 → TB6600

| TB6600 | NEMA17 |
|---|---|
| A+ | Coil A+ (typically red) |
| A- | Coil A- (typically blue) |
| B+ | Coil B+ (typically green) |
| B- | Coil B- (typically black) |
| V+ | 9-42V DC supply positive |
| GND | DC supply negative |
