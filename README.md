# Stepper Motor Shield — PID Position Control + S-Curve

PIC32MK0512MCJ064 firmware driving a NEMA17 stepper via TB6600 driver with AS5047U
magnetic encoder feedback. Controlled over USB serial (19200 baud) or web dashboard.

## Quick Start

```bash
# Flash firmware (via MDB)
rm -rf ~/.mplabcomm
mdb -c "device PIC32MK0512MCJ064; hwtool pkob4 -p; program controller.X.production.hex; run"

# Connect serial
screen /dev/cu.usbmodemBUR2143200772 19200

# Start dashboard
cd dashboard && npm install && node server.js
# → http://localhost:3000
```

## Serial Commands

| Command | Action | Range |
|---------|--------|-------|
| **Position Control** | | |
| `T=<pos>` | Move to absolute position (encoder counts, negative OK) | ±2M |
| `STOP` | Emergency stop — hold position, reset integrator | — |
| `HOME` | Set current position = 0 AND target = 0 | — |
| `ZERO` | Set current position = 0 (leave target unchanged) | — |
| `CLEAR` | Clear fault, re-init motor driver | — |
| **Open-Loop Motor Test** | | |
| `ON` | Enable motor + start spinning at last speed (default 100) | — |
| `OFF` | Disable motor, exit open-loop mode | — |
| `CW` | Set direction clockwise | — |
| `CCW` | Set direction counter-clockwise | — |
| `SPEED=<n>` | Set step rate | 10–100000 |
| **Motion Profile** | | |
| `MAXV=<n>` | Max velocity | 1–10000 steps/s |
| `ACCEL=<n>` | Max acceleration | 10–100000 steps/s² |
| `JERK=<n>` | Max jerk (S-curve smoothness) | 100–1000000 steps/s³ |
| `PROFILE=S` | S-curve — jerk-limited smooth transitions | — |
| `PROFILE=T` | Trapezoidal — instant acceleration changes | — |
| **PID Tuning** | | |
| `KP=<n>` | Proportional gain | 0–9999 |
| `KI=<n>` | Integral gain | 0–9999 |
| `TOL=<n>` | At-target tolerance | 0–1000 counts |
| `FT=<n>` | Fault threshold (checked only at rest) | 1–100000 counts |
| **Diagnostics** | | |
| `GET` | One-shot config dump | — |

### Response Format

All commands acknowledge:
```
OK <command>=<value>\r\n
```
or
```
ERR UNKNOWN\r\n
```

### Automatic Status Stream (10 Hz)

```
P:<position>,E:<error>,V:<velocity>,T:<target>,F:<fault>,M:<moving>,A:<at_target>\r\n
```

### GET Response

```
T=<target>,P=<position>,E=<error>,V=<velocity>,KP=<kp>,KI=<ki>,MAXV=<maxv>,ACCEL=<accel>,JERK=<jerk>,TOL=<tol>,FT=<ft>,PROFILE=<S|T>\r\n
```

## Motion Profiles

### S-Curve (`PROFILE=S`)
Jerk-limited acceleration:

```
acceleration:  ──/‾‾‾‾‾‾\──  (ramps up/down at JERK rate)
velocity:      ──/‾‾‾‾‾‾\──  (rounded S-shaped transitions)
```

- Jerk limits how fast acceleration changes
- Lower JERK = smoother but slower moves
- Three phases: smooth accel → coast → smooth decel

### Trapezoidal (`PROFILE=T`)
Instant acceleration changes:

```
acceleration:  ──╯‾‾‾‾‾‾╰──  (square wave)
velocity:      ──/‾‾‾‾‾‾\──  (sharp V-shaped ramps)
```

- ACCEL limits the slope of velocity ramps
- Reaches target speed faster than S-curve at same ACCEL
- Higher mechanical stress at transitions

## Wiring

See [PINOUT.md](docs/PINOUT.md) for complete pin mapping.

### TB6600 Common-Anode Summary

| TB6600 | PIC32MK | Note |
|--------|---------|------|
| PUL+ | RPB15 (OC1 PWM) | Step pulses, 50% duty |
| DIR+ | RPB14 (GPIO) | LOW=CW, HIGH=CCW |
| ENA+ | RPB13 (GPIO) | **LOW=enable**, HIGH=disable |
| PUL-/DIR-/ENA- | GND | All three tied together |

### TB6600 DIP Switches
- S1/S2/S3: per motor current rating
- S4/S5/S6: all ON = 1/16 microstep (3200 steps/rev)

## Dashboard

Web-based UI on port 3000:

```
dashboard/
├── server.js          # Express + WebSocket + serial bridge
├── package.json
└── public/
    ├── index.html     # UI layout
    ├── app.js         # Client logic
    └── styles.css     # Dark theme
```

### Features
- Position display: target / actual with bar indicator
- Status: error, velocity, fault, moving, at-target
- Motor Test card: ON/OFF, CW/CCW, speed slider
- PID sliders: Kp, Ki
- Motion Profile: S-curve / Trapezoidal toggle
- ACCEL and JERK sliders
- MaxV, Tolerance, Fault Threshold inputs

### Architecture

```
Firmware (PIC32MK) ←→ Serial (19200) ←→ server.js ←→ WebSocket ←→ browser
```

## Firmware Architecture

| Component | Peripheral | Rate | Purpose |
|-----------|-----------|------|---------|
| Position loop | Timer3 ISR | 1 kHz | PID → profile → velocity → steps |
| Step generation | OC1 + Timer2 PWM | Variable | Hardware step pulses, 50% duty |
| Encoder | QEI1 | Continuous | AS5047U ABI, 16384 counts/rev |
| UART | UART1 | 19200 baud | Command + telemetry |
| Status stream | Main loop | 10 Hz | P:E:V:T:F:M:A telemetry |

### S-Curve Algorithm (control_isr)

```
error = target_pos - encoder_read()
raw_vel = PID(error)
verr = raw_vel - sm_vel
tgt_acc = sign(verr) * accel_limit (or less if near target)

if PROFILE=S:
  astep = clamp(tgt_acc - sm_acc, ±jerk_limit/CONTROL_FREQ)
  sm_acc += astep
else (PROFILE=T):
  sm_acc = tgt_acc  // instant

sm_vel += sm_acc * dt
```

## Releases

- **v5.0.1** — Trapezoidal profile selectable via PROFILE=S/T, dashboard toggle
- **v5.0.0** — PID + S-curve + open-loop test + 6 bug fixes
- **v4.0.0** — UART serial fixes
- **v3.1.0** — Open-loop speed control
- **v3.0.0** — Closed-loop position control
- **v2.0.0** — Stepper control + encoder
- **v1.0.0** — Encoder-only firmware
