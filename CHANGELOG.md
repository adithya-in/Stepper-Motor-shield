# Changelog

## v5.0.1 — 2026-06-11

Trapezoidal profile support with UI toggle. Selectable S-curve or trapezoidal motion profile.

### Added
- `PROFILE=S` / `PROFILE=T` — select motion profile via serial or dashboard toggle
- `ACCEL=<n>` — configurable max acceleration (10–100000 steps/s², default 500)
- `JERK=<n>` — configurable jerk for S-curve (100–1000000 steps/s³, default 30000)
- Dashboard: profile toggle button (S-curve / Trapezoidal)
- Dashboard: ACCEL and JERK sliders with live command dispatch

### How profiles differ
| Profile | Acceleration change | Velocity shape |
|---------|-------------------|----------------|
| S-curve (`PROFILE=S`) | Jerk-limited — ramps up/down gradually | Rounded S-shaped transitions |
| Trapezoidal (`PROFILE=T`) | Instant — snaps to full accel immediately | Sharp V-shaped ramps |

## v5.0.0 — 2026-06-11

PID position control with S-curve trajectory generator, open-loop motor test mode,
and six bug fixes. Firmware rewrite from speed-control to position-control.

### Added
- **S-curve trajectory generator** — jerk-limited acceleration smoother replaces linear ramp
  - Three-phase per move: smooth accel → coast → smooth decel
  - `sm_acc` ramps toward target acceleration at `jerk_limit / CONTROL_FREQ` per tick
  - `sm_vel` integrates with fractional remainder for sub-step precision
- **Closed-loop PID position control** at 1 kHz via Timer3 ISR
  - `T=<pos>` — move to absolute target with encoder feedback
  - PID → velocity → S-curve smoother → motor steps
  - Holding torque at target (motor stays enabled at zero speed)
- **Open-loop motor test mode**
  - `ON` / `OFF` — enable/disable motor
  - `CW` / `CCW` — set direction
  - `SPEED=<n>` — set step rate (10–100000 steps/s)
- **Fault debounce** — fault threshold only checked when `at_target == 1`
  - 50 ms debounce window prevents false triggers during large moves
- **Dashboard Motor Test card** — ON, OFF, CW, CCW buttons + speed slider
- **GET command** — one-shot config dump with all parameters
- `ACCEL=<n>` — configure max acceleration
- `JERK=<n>` — configure max jerk
- Updated startup banner with full command list

### Bug Fixes
1. **Timer3 ISR never fired** — `IFS0CLR` and `IEC0SET` used bit 12 instead of bit 14
   - `_TIMER_3_VECTOR = 14` on PIC32MK0512MCJ064, not 12
   - PID ISR was completely dead since firmware inception
2. **Global interrupts never enabled** — missing `__builtin_enable_interrupts()`
3. **Fault triggered mid-move** — fault threshold checked on every cycle, killing valid moves
4. **Motor disabled at target** — `motor_disable()` called when position reached, losing holding torque
5. **Integral/velocity not reset** — `integral`, `sm_vel`, `sm_acc` persisted across `T=` commands
6. **Acceleration not reset on STOP** — `sm_vel`, `sm_acc` kept old values

## v4.0.0 — 2026-06-10

UART serial fixes — reliable USB serial (19200 baud) communication with terminal and web dashboard.

### Fixed
- PPS RX mapping: `U1RXR = 1` → `U1RXR = 10` (correct for RPG8 as U1RX)
- Receiver enable: added `U1STAbits.URXEN = 1`
- Overrun lockup: added `uart_clear_oerr()` — clears OERR flag and drains RX FIFO
- SPEED command bug: digit `0` in `SPEED 5000` matched single-char `0` (STOP)
  - Single-char handlers only fire when `rx_idx == 0`

## v3.1.0 — 2026-06-09

Open-loop speed control. Motor spins continuously at commanded speed from browser slider.

### Changes
- Replaced closed-loop position control with open-loop speed control
- Commands: `ON`, `OFF`, `CW`, `CCW`, `SPEED <steps/s>`
- Dashboard: RPM gauge, speed slider (0–10000 steps/s), direction toggle, start/stop

## v3.0.0 — 2026-06-09

Full closed-loop position control with PI controller, encoder feedback, and web dashboard.

### Features
- Timer3 position control loop at 1 kHz
- PI controller with configurable Kp, Ki, integral limit, anti-windup
- Following error + fault threshold emergency stop
- Point-to-point moves via UART or web UI
- UART command set: `T=<pos>`, `HOME`, `STOP`, `CLEAR`, `KP=<v>`, `KI=<v>`, `MAXV=<v>`, `TOL=<v>`, `FT=<v>`
- Status telemetry at 10 Hz

## v2.0.0 — 2026-06-09

TB6600 stepper driver control + encoder feedback + full test documentation.

## v1.0.0 — 2026-06-08

Initial release: AS5047U encoder QEI + UART firmware for PIC32MK MCJ Curiosity Pro.
