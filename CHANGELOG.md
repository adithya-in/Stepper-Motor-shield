# Changelog

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