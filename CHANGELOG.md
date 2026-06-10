# Changelog

## v4.0.0 — 2026-06-10

UART serial fixes — the firmware now reliably communicates over USB serial (19200 baud)
with both terminal and web dashboard. Motor auto-starts at 2000 steps/s CW on power-up.

### UART Fixes

| Issue | What Didn't Work | Fix |
|-------|-----------------|-----|
| PPS RX mapping | `U1RXR = 1` — mapped wrong pin for UART RX | **`U1RXR = 10`** — correct value for RPG8 as U1RX input on Curiosity Pro board |
| Receiver enable | `URXEN` left at default (0) — receiver never activated | **`U1STAbits.URXEN = 1`** |
| Overrun lockup | OERR set on RX overflow → UART permanently stuck | **`uart_clear_oerr()`** — clears OERR flag and drains RX FIFO |
| SPEED command bug | Digit `0` in `SPEED 5000` matched single-char `0` (STOP) | Single-char handlers only fire when **`rx_idx == 0`** (not mid-string) |

### Key Lessons Learned
- PIC32MK PPS input values (`U1RXR`) are NOT the same as PPS output values (`RPE0R`)
- `U1STAbits.URXEN = 1` is mandatory — not set by UARTEN
- Overrun errors are fatal unless explicitly handled
- Common-anode TB6600 wiring: all `-` pins to GND, `ENA+ LOW = enabled`
- MDB program-only mode (`-p`) works but device stops after USB power cycle until re-flashed

## v3.1.0 — 2026-06-09

Open-loop speed control. Motor spins continuously at commanded speed from browser slider. Simpler for initial testing than position control.

### Changes
- Replaced closed-loop position control with open-loop speed control
- Commands: `ON`, `OFF`, `CW`, `CCW`, `SPEED <steps/s>`
- Dashboard: RPM gauge, speed slider (0–10000 steps/s), direction toggle, start/stop
- Encoder RPM displayed in browser, full CW/CCW/STOPPED direction indicator

## v3.0.0 — 2026-06-09

Full closed-loop position control with PI controller, encoder feedback, and web dashboard. (Replaced by v3.1.0 speed control.)

### Features
- **Timer3 position control loop** at 1 kHz — reads encoder → computes error → adjusts OC1 speed
- **PI controller** with configurable Kp, Ki, integral limit, and anti-windup
- **Following error** computed every cycle; **fault threshold** triggers emergency stop on stall
- **Software zero** (HOME command) resets position reference
- **Point-to-point moves**: set target position via UART or web UI, motor servos to target within configurable tolerance
- **Position hold**: at target, OC1 stops and motor is disabled to reduce heating
- **Current limit enforcement**: `MAXV=` command caps step rate (and thus torque demand)
- **UART command set**: `T=<pos>`, `HOME`, `STOP`, `CLEAR`, `KP=<v>`, `KI=<v>`, `MAXV=<v>`, `TOL=<v>`, `FT=<v>`
- **Web dashboard rewritten** for position control: actual/target position bars, following error gauge, status LEDs (homed/moving/at-target/fault), live velocity, tuning sliders
- **Status telemetry** at 10 Hz: `P:<pos>,E:<error>,V:<vel>,T:<target>,F:<fault>,H:<homed>,M:<moving>`

### Step Generation
- OC1 + Timer2 PWM at variable rate (10–5000 steps/s, 1:8 prescaler)
- Direction via RPB14, enable via RPB13 (LOW = enabled)
- Minimum speed floor (10 steps/s) to prevent cogging at very low rates

### Bug Fixes / Improvements
- Removed open-loop CW/CCW test loop in favor of closed-loop position control
- Encoder position tracked via software offset (POS1CNT is read-only on PIC32MK)

## v2.0.0 — 2026-06-09

Second release. Adds TB6600 stepper driver control and full documentation of the testing journey.