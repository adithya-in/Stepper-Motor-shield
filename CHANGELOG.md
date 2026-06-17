# Changelog

## v6.1.0 — 2026-06-17

Clock architecture migrated from FRC (8 MHz) to SPLL (48 MHz) for reliable 115200 baud
serial. All clock-dependent timing formulas updated. Command parameter upper limits removed.

### Added
- **`flash.mdb`** — MDB programming script for command-line flashing
- **Dashboard telemetry toggle button** — `tlm:0`/`tlm:1` via button in UI
- **`#include <sys/attribs.h>`** — for `__ISR` macro on XC32 v5.10

### Changed
- **Clock: FRC (8 MHz) → SPLL (48 MHz)** — `FPLLIDIV=DIV_2`, `FPLLMULT=MUL_24`, `FPLLODIV=DIV_2`
- **PB_CLOCK: 4 MHz → 48 MHz** — 12× peripheral bus speed improvement
- **Baud rate: 19200 → 115200** — U1BRG=25 (0.16% error at 48 MHz PB clock)
- **`msleep()` corrected** — `ms * 4000u` → `MS(ms)` for 24 MHz core timer (was 4 MHz)
- **`motor_set_speed()` corrected** — `500000/sps` → `(PB_CLOCK/8)/sps` for 6 MHz Timer2 (was 500 kHz)
- **Auto-tune Tu corrected** — `/4000.0f` → `/ ((float)TICKS_PER_MS * 1000.0f)` for 24 MHz core timer
- **Config save timer corrected** — `PB_CLOCK/10` → `MS(100)` for correct 100 ms debounce at 24 MHz
- **`CONFIG_DATA_WORDS: 11 → 12`** — fixes checksum validation (struct layout includes padding)
- **MAXV: upper limit removed** — any positive value accepted (was ≤ 100000)
- **ACCEL: upper limit removed** — any ≥ 100 value accepted (was ≤ 5000000)
- **JERK: upper limit removed** — any ≥ 1000 value accepted (was ≤ 10000000)
- **`m:` command speed limit removed** — any positive speed accepted (was ≤ 100000)
- **TOL/FT command parsing simplified** — removed ERR RANGE branches, validation retained
- **Dashboard default baud: 19200 → 115200** — in server.js, app.js, index.html
- **Server parses TLM enable state, position/error/velocity from GET** — T=, P=, E=, V=, TOL=, FT= fields
- **README/PINOUT/TESTS microstep corrected** — 1/32 microstep documentation (6400 steps/rev)
- **`nbproject/Makefile-local-default.mk`** — updated MPLAB X IDE path for v6.30

### Bug Fixes
- **Integer overflow in `tlm_ticks` calculation** — added `(uint32_t)` cast for safe multiplication
- **Dashboard telemetry state not reflected** — added TLM button with live ON/OFF state
- **Dashboard server missed position/error from GET** — now parses T=, P=, E=, V=, TOL=, FT= fields

### Removed
- **All hard-coded upper limits on MAXV, ACCEL, JERK** — firmware now accepts any value the hardware can physically handle

## v6.0.0 — 2026-06-15

Phase 1 complete. Comprehensive bug/troubleshooting documentation, activeElement input
protection in dashboard, and full knowledge base capture for future builders.

### Added
- **`docs/BUGS_AND_LESSONS.md`** — complete catalogue of every bug encountered:
  - 24+ bugs documented with root cause, fix, prevention, and file references
  - Covers firmware bugs (Timer3 vector, global interrupts, ENA polarity, OERR lockup,
    PPS pins, fault triggering, motor disable, state reset, etc.)
  - Build/flash issues (MDB CLI failure, PKoB4 VCOM enumeration, clean build)
  - Dashboard issues (browser cache, activeElement overwrite, queue input spaces)
  - Electrical issues (TB6600 DIP switches, encoder wiring, magnet coupling)
  - NVM issues (magic/checksum, flash debounce)
  - Top 5 most painful bugs ranked by hours lost

### Changed
- **Dashboard input fields** protected from telemetry overwrite:
  - `document.activeElement` check on ACCEL, JERK, MAXV, DWELL fields
  - Users can type without values being overwritten mid-edit
- **README** updated to reflect Phase 1 completion, release table extended

### Bug Fixes
- **Dashboard: telemetry overwrites input fields during typing** — added
  `document.activeElement` guard in `app.js` for accel, jerk, maxv, dwell inputs
- **Browser cache stale UI** — documented hard refresh requirement; recommend
  `Cache-Control: no-store` in server.js

## v5.3.0 — 2026-06-15

Auto-tune PID tuning: relay-based Ziegler-Nichols with bang-bang oscillation, integrated into
1 kHz ISR, dashboard UI, and server bridge.

### Added
- **`TUNE`** — start auto-tune relay with `:<offset>:<vel>:<hyst>` optional params
- **Auto-tune state machine** in control ISR:
  - `TUNE_IDLE` → `TUNE_MOVE` (move to offset) → `TUNE_RELAY` (bang-bang oscillation) → `TUNE_COMPLETE`
  - Relay control: hysteresis-based switching with peak/period tracking
  - Minimum 4 cycles for reliable measurement
  - ZN-PID computation on completion: `OK TUNE:Kp=...,Ki=...,Kd=...,amp=...,Tu=...`
- **`KD=<n>`** — set derivative gain directly
- **`TS:<state>`** — tune state in telemetry stream (0=IDLE, 1=MOVE, 2=RELAY, 3=COMPLETE)
- **Derivative kick fix** — `prev_error` reset on first frame of a new move
- **Dashboard**: Kd slider, Auto-Tune button with live status display (Idle/Moving/Relay/Complete)
- **server.js**: parses `OK TUNE:`, `TS:`, `KD=`, `KP=`, `KI=` from serial

### Changed
- Help banner updated with `TUNE` and `KD=` entries
- Dashboard tune status telemetry parsed from both `TS:` field and `OK TUNE:` event

## v5.2.0 — 2026-06-12

Multi-point queue with configurable dwell delay.

### Added
- **`Q=<pos>,<pos>,...`** — load up to 32 waypoints and start sequential execution
- **`DWELL=<ms>`** — set inter-point dwell delay (0–30000 ms)
- **`QSTOP` / `qstop`** — abort queue mid-execution
- **`q:<pos>:<pos>:...`** — Phase 1 colon-separated variant
- **`dwell:<ms>`** — Phase 1 colon-separated variant
- Queue fields `QLEN`, `QIDX`, `DWELL` in GET response
- Queue index/length in status telemetry (`Q:<idx>`, `QL:<len>`)
- Dashboard queue card: textarea for points, Dwell input+Set, Start/Stop buttons, live status

### Changed
- `T=`, `m:`, `STOP`, `OFF`, `en:0` commands now abort any active queue
- server.js parses `QLEN`, `QIDX`, `DWELL` from GET, and `Q/QL` from status stream

## v5.1.4 — 2026-06-12

Dashboard UI updated to match new firmware ranges. System achieves ±1–3 counts accuracy.

### Added
- **Accuracy documentation**: ±1–3 encoder counts = 0.022°–0.066° (at encoder noise floor)

### Changed
- MAXV input: range 10–50000, default 5000
- ACCEL: slider → number input (100–500000) + Set button
- JERK: slider → number input (1000–10000000) + Set button
- Speed slider: max 10000 → 50000
- server.js parses ACCEL, JERK, MAXV, KD, I, US from GET response
- app.js updates input fields when GET/status data arrives

### Changed
- **MAXV input**: range updated to 10–50000, default 5000
- **ACCEL**: slider replaced with number input (100–500000) + Set button
- **JERK**: slider replaced with number input (1000–10000000) + Set button
- **Speed slider**: max raised from 10000 to 50000
- **server.js** now parses ACCEL, JERK, MAXV, KD, I, US from GET response
- **app.js** updates input fields when GET/status data arrives

## v5.1.3 — 2026-06-12

Snappier defaults and wider parameter ranges for aggressive moves.

### Changed
- **Default MAXV**: 2000 → 5000 steps/s (2.5× faster)
- **Default ACCEL**: 500 → 50000 steps/s² (100× faster ramp)
- **Default JERK**: 30000 → 500000 steps/s³ (16× faster S-curve)
- **MAXV range**: 10000 → 50000 (5× higher top speed)
- **ACCEL range**: 100000 → 500000 (5× faster acceleration)
- **JERK range**: 1000000 → 10000000 (10× faster jerk)

### Tips
- `PROFILE=T` for maximum snappiness (instant acceleration, no jerk limit)
- With `PROFILE=S`, `JERK=<n>` must be high for quick acceleration changes
- `m:20000:80000` will hit 20000 steps/s within 40ms (at ACCEL=500000)
- For ultimate snap: `PROFILE=T` + `ACCEL=500000` + `MAXV=50000`

## v5.1.2 — 2026-06-12

Profile feed-forward velocity: motor runs at full speed until stopping distance,
then decelerates. PID becomes a trim, active only near target.

### Changed
- **Profile velocity feed-forward** replaces PID as primary velocity source
  - `profile_vel` = max_vel when far from target, linear deceleration near target
  - Deceleration starts at stopping distance `v²/(2·accel_limit)` from target
  - PID output added as trim on top of profile velocity
- **Integral anti-windup** — integral reset during moves (only active near target)
- **At-target reset** — integral and prev_error cleared on arrival

### Result
- Full speed maintained until close to target — no premature deceleration
- `m:50000:100000` reaches max velocity and stays there until stopping distance
- ACCEL and JERK settings now actually control acceleration rate, not PID-decay rate
- Following error at rest: **±1–3 counts** (0.022°–0.066°)

## v5.1.1 — 2026-06-12

Non-volatile flash storage: all settings persist across power cycles.

### Added
- **NVM flash config** — parameters saved to last flash page (PIC32MK self-write)
  - `config_save()` — page erase + word-by-word program via NVM controller
  - `config_load()` — reads at boot, validates magic + checksum
  - `config_mark_dirty()` — 100ms debounce before flash write
- Parameters persisted: Kp, Ki, Kd, max_vel, tolerance, fault_thr, accel_limit, jerk_limit, profile, coil_current, microstep

### Legacy
- All commands, dashboard, UART stream unchanged

## v5.1.0 — 2026-06-12

Phase 1 command protocol: colon-separated opcode format, with backward-compatible legacy
commands. Kd derivative term in PID loop. Configurable telemetry period.

### Added
- **Phase 1 commands** (colon-separated, same parser reused for CAN Phase 2):
  - `m:<speed>:<pos>` — move to position at given speed
  - `en:1` / `en:0` — enable closed-loop servo / disable motor
  - `z` — set current position as zero
  - `i:<mA>` — set coil current limit (100–5000 mA, stored value)
  - `us:<n>` — set microstep setting (1/2/4/8/16/32, stored value)
  - `pid:<kp>:<ki>:<kd>` — set PID gains (adds derivative term)
  - `tlm:1:<ms>` — enable telemetry stream at configurable period (10–10000 ms)
  - `tlm:0` — disable telemetry stream
  - `st?` — one-shot status query: `p:<pos>,v:<vel>,e:<err>,f:<flags>`
- **Kd derivative term** in PID controller (1 kHz, tracks `prev_error`)
- **Configurable telemetry period** — replaces hardcoded 100 ms
- **GET response** now includes KD, I (coil current), US (microstep)
- **Banner** updated to show all Phase 1 commands

### Legacy
- All existing commands (`T=`, `KP=`, `KI=`, `GET`, `ON`, `OFF`, etc.) unchanged
- Dashboard status stream format (`P:E:V:T:F:M:A`) preserved

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
