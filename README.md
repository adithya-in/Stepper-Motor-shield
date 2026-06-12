# Stepper Motor Shield — PID Position Control + S-Curve

PIC32MK0512MCJ064 firmware driving a NEMA17 stepper via TB6600 driver with AS5047U
magnetic encoder feedback. Controlled over USB serial (19200 baud) or web dashboard.

## Quick Start

```bash
# Flash firmware (via MDB)
rm -rf ~/.mplabcomm
/Applications/microchip/mplabx/v6.30/mplab_platform/bin/mdb.sh flash.mdb

# Connect serial
screen /dev/cu.usbmodemBUR2143200772 19200

# Start dashboard
cd dashboard && npm install && node server.js
# → http://localhost:3000
```

**Power cycle the PKoB4** (unplug/replug USB) after flashing — VCOM gets stuck otherwise.

## Serial Commands

All commands support both **legacy** and **Phase 1** formats. Legacy commands are preserved
so the dashboard and existing scripts continue working.

### Legacy Commands

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
| `SPEED=<n>` | Set step rate for open-loop mode | 10–100000 |
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

### Phase 1 Commands (Colon-Separated)

| Command | Action | Example |
|---------|--------|---------|
| **Move** | | |
| `m:<speed>:<pos>` | Move to position at given speed | `m:1500:8000` |
| **Enable** | | |
| `en:1` | Enable closed-loop servo (hold current position) | `en:1` |
| `en:0` | Disable motor (release) | `en:0` |
| **Zero** | | |
| `z` | Set current encoder position as zero | `z` |
| **Coil Current (stored)** | | |
| `i:<mA>` | Set coil current limit (stored value, 100–5000 mA) | `i:800` |
| **Microstep (stored)** | | |
| `us:<n>` | Set microstep setting | `us:16` |
| **PID Gains** | | |
| `pid:<kp>:<ki>:<kd>` | Set proportional, integral, derivative gains | `pid:50:5:10` |
| **Telemetry** | | |
| `tlm:1:<ms>` | Enable status stream at N ms period | `tlm:1:50` |
| `tlm:0` | Disable status stream | `tlm:0` |
| **Status Query** | | |
| `st?` | One-shot status: position, velocity, error, flags | `st?` |

### Response Format

All commands acknowledge:
```
OK <command>=<value>\r\n
```
or
```
ERR UNKNOWN\r\n
```

### Automatic Status Stream (10 Hz default)

```
P:<position>,E:<error>,V:<velocity>,T:<target>,F:<fault>,M:<moving>,A:<at_target>\r\n
```

Set period with `tlm:1:<ms>` (10–10000 ms), disable with `tlm:0`.

### st? Response (one-shot)

```
p:<position>,v:<velocity>,e:<error>,f:<flags>\r\n
```

Flags bitmask:
- bit 0: fault
- bit 1: moving
- bit 2: at_target
- bit 3: open_loop

### GET Response

```
T=<target>,P=<position>,E=<error>,V=<velocity>,KP=<kp>,KI=<ki>,KD=<kd>,MAXV=<maxv>,ACCEL=<accel>,JERK=<jerk>,TOL=<tol>,FT=<ft>,PROFILE=<S|T>,I=<mA>,US=<microstep>\r\n
```

## Usage Examples

### Basic Move

```
> en:1           Enable servo (holds current position)
OK en:1
> m:2000:10000   Move to position 10000 at 2000 steps/s
OK m:2000:10000
> st?            Check status
p:10000,v:0,e:0,f:4
```

### PID Tuning

```
> pid:30:2:5     Set Kp=30, Ki=2, Kd=5
OK pid:30:2:5
> GET            Verify all params
T=10000,P=10000,E=0,V=0,KP=30,KI=2,KD=5,...
```

### Telemetry Stream

```
> tlm:1:200      Stream every 200ms
OK tlm:1:200
P:5000,E:-2,V:150,T:5000,F:0,M:0,A:1
P:5000,E:-1,V:0,T:5000,F:0,M:0,A:1
...
> tlm:0          Stop streaming
OK tlm:0
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
| Status stream | Main loop | Configurable | P:E:V:T:F:M:A telemetry |

### S-Curve Algorithm (control_isr)

```
error = target_pos - encoder_read()
raw_vel = PID(error + integral + derivative) / 100
verr = raw_vel - sm_vel
tgt_acc = sign(verr) * accel_limit (or less if near target)

if PROFILE=S:
  astep = clamp(tgt_acc - sm_acc, ±jerk_limit/CONTROL_FREQ)
  sm_acc += astep
else (PROFILE=T):
  sm_acc = tgt_acc  // instant

sm_vel += sm_acc * dt
```

### Resolution

| Aspect | Value |
|--------|-------|
| Motor steps/rev | 200 (NEMA17, 1.8°/step) |
| Microstep (S4–S6 all ON) | 1/16 |
| Steps per revolution | 3200 |
| Encoder PPR | 4096 (AS5047U) |
| Encoder counts/rev (4× QEI) | 16384 |
| Position resolution | 360° / 16384 = 0.022° per count |

## Non-Volatile Config

All tuning parameters (Kp, Ki, Kd, max_vel, accel_limit, jerk_limit, tolerance,
fault_thr, profile, coil_current, microstep) are automatically saved to flash
when changed. These persist across power cycles and are loaded at boot.

- **Flash address**: last page of 512KB (0x1D07F000)
- **Save strategy**: 100ms debounce after last parameter change
- **Integrity**: magic word + XOR checksum validated on load
- **Defaults**: used if flash is blank or corrupted

## Releases

- **v5.1.1** — NVM flash config: all parameters persist across power cycles
- **v5.1.0** — Phase 1 commands: colon-separated opcode format, Kd, configurable telemetry

- **v5.1.0** — Phase 1 commands: colon-separated opcode format, Kd derivative term, configurable telemetry, coil current & microstep storage
- **v5.0.1** — Trapezoidal profile selectable via PROFILE=S/T, dashboard toggle
- **v5.0.0** — PID + S-curve + open-loop test + 6 bug fixes
- **v4.0.0** — UART serial fixes
- **v3.1.0** — Open-loop speed control
- **v3.0.0** — Closed-loop position control
- **v2.0.0** — Stepper control + encoder
- **v1.0.0** — Encoder-only firmware
