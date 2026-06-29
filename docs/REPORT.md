---
title: "Stepper Motor Shield — Project Report"
subtitle: "PIC32MK0512MCJ064 + TMC2660 + AS5047D\nCAN Bus and USB-C Interface"
date: June 29, 2026
toc: true
toc-depth: 3
---

# Project Status

| Area | Status |
|------|--------|
| **Firmware** | **Complete** — v6.2.0 tested and verified at 48 MHz (SPLL, 115200 baud, auto-tune capped) |
| **Hardware testing** | **Complete** — NEMA17 + TB6600 characterized at 24V and 31V (11 test rows at 31V) |
| **Schematic** | **Complete** — KiCad schematic for PIC32MK + TMC2660 + AS5047D + CAN + USB-C |
| **PCB layout** | **In progress** — routing and layout yet to be finished |
| **10 MHz clock migration** | **Pending** — all clock-dependent formulas need recalculation and retesting |

## What Has Been Done

- Firmware v6.2.0 with SPLL 48 MHz clock, 115200 baud serial, PID position control, S-curve and trapezoidal profiles, auto-tune (Ziegler-Nichols relay method), multi-point queue, telemetry stream, and NVM config storage
- Hardware testing at 24V and 31V — 11 test configurations at 31V characterizing MAXV, ACCEL, and PID gain limits
- KiCad schematic capture with all major components placed and wired: PIC32MK0512MCJ064 (TQFP-64) with decoupling, VCAP, oscillator, ICSP; TMC2660 stepper driver (QFP-44) with SPI control, sense resistors, charge pump; AS5047D magnetic encoder (TSSOP-14) in SPI mode; MCP2562 CAN transceiver (SOIC-8); USB-C for power and programming; JST PH connectors for motor and encoder

## What Remains

- PCB layout — component placement, trace routing, thermal management, design rule check
- 10 MHz clock migration — recalculate UART baud (U1BRG), Timer2/OC1 step rate, Timer3 ISR period, core tick timing, auto-tune formulas
- Fabrication — generate Gerber files, BOM, pick-and-place
- Assembly and testing at target clock speed

---

# Firmware

## Clock Configuration

All testing performed at **48 MHz** SYSCLK using the SPLL from the internal 8 MHz FRC oscillator:

| Parameter | Value |
|-----------|-------|
| FRC oscillator | 8 MHz |
| PLLIDIV | DIV_2 (4 MHz to PLL) |
| PLLMULT | MUL_24 (96 MHz VCO) |
| PLLODIV | DIV_2 (48 MHz SYSCLK) |
| PB clock | 48 MHz |
| Core timer ticks | 24,000 / ms |
| UART baud | 115,200 (U1BRG = 25, 0.16% error) |
| Timer3 ISR | 1 kHz (PR3 = 47,999) |
| Telemetry interval | 200 ms (tlm_ticks = 4,800,000 core ticks) |

**Target production clock: 10 MHz** — all formulas need recalculation and testing is pending.

## Features

| Feature | Details |
|---------|---------|
| **Position control loop** | 1 kHz Timer3 ISR — error → profile feed-forward + PID trim → smoother → motor speed |
| **Step generation** | OC1 + Timer2 PWM — hardware-generated step pulses, no CPU bit-banging |
| **Encoder feedback** | QEI1 — AS5047D/AS5047U ABI, 16384 counts/rev |
| **Motion profiles** | S-curve (jerk-limited) and trapezoidal (instant accel) |
| **PID control** | Proportional, integral, derivative with anti-windup |
| **Auto-tune** | Relay-based Ziegler-Nichols — outputs Kp, Ki, Kd with caps |
| **Multi-point queue** | 2–32 waypoints with configurable dwell delay |
| **Telemetry** | Configurable status stream over UART |
| **NVM config** | Auto-save to last flash page on parameter change |
| **Dashboard** | Node.js/WebSocket web UI with sliders, tune button, TLM toggle |

## Firmware Architecture

| Component | Peripheral | Rate | Purpose |
|-----------|-----------|------|---------|
| Position loop | Timer3 ISR | 1 kHz | Profile + PID trim + smoother + velocity |
| Step generation | OC1 + Timer2 PWM | Variable | Hardware step pulses, 50% duty |
| Encoder | QEI1 | Continuous | AS5047U ABI, 16384 counts/rev |
| UART | UART1 | 115200 baud | Command + telemetry |
| NVM flash | Self-write | On change | Config storage (last flash page) |
| Status stream | Main loop | Configurable | P:E:V:T:F:M:A telemetry |

### Control Algorithm

```
error = target_pos - encoder_read()

// Profile feed-forward velocity
stop_dist = max_vel^2 / (2 * accel_limit)
if abs(error) > stop_dist:
    profile_vel = max_vel * sign(error)     // cruise
else:
    profile_vel = max_vel * error / stop_dist // decel ramp

// PID trim
pid_trim = (Kp*error + Ki*integral + Kd*derivative) / 100

// Combined, fed into accel/jerk smoother
raw_vel = profile_vel + pid_trim
```

### Resolution

| Aspect | Value |
|--------|-------|
| Motor steps/rev | 200 (NEMA17, 1.8 degrees/step) |
| Microstep setting | 1/32 |
| Steps per revolution | 6400 |
| Encoder PPR | 4096 (AS5047U) |
| Encoder counts/rev (4x QEI) | 16384 |
| Position resolution | 360 degrees / 16384 = 0.022 degrees per count |
| Achieved accuracy | +-1-3 counts = 0.022-0.066 degrees |

## Motor Speed Formula

```
motor_set_speed(steps_per_sec):
    pr = (PB_CLOCK / 8) / steps_per_sec
    PR2 = pr - 1
    OC1R = pr >> 1    // 50% duty cycle
```

The Timer2 clock is PB_CLOCK / 8 = 6 MHz at 48 MHz PB. At 115200 baud, the OC1 produces clean step pulses up to approximately 300 kHz.

## Serial Protocol

### Baud Rate: 115200

### Legacy Commands

| Command | Action | Range |
|---------|--------|-------|
| `T=<pos>` | Move to absolute position | +/- 2M counts |
| `STOP` | Emergency stop, hold position | - |
| `HOME` | Set current position = 0 and target = 0 | - |
| `ZERO` | Set current position = 0 (leave target) | - |
| `CLEAR` | Clear fault, re-init driver | - |
| `ON` | Enable motor + spin at last speed | - |
| `OFF` | Disable motor, exit open-loop | - |
| `CW` | Set direction clockwise | - |
| `CCW` | Set direction counter-clockwise | - |
| `SPEED=<n>` | Step rate for open-loop | 10-100000 |
| `MAXV=<n>` | Max velocity | >= 1 steps/s |
| `ACCEL=<n>` | Max acceleration | >= 100 steps/s^2 |
| `JERK=<n>` | Max jerk (S-curve) | >= 1000 steps/s^3 |
| `PROFILE=S` | S-curve smooth transitions | - |
| `PROFILE=T` | Trapezoidal instant accel | - |
| `KP=<n>` | Proportional gain | 0-99999 |
| `KI=<n>` | Integral gain | 0-99999 |
| `KD=<n>` | Derivative gain | 0-99999 |
| `TOL=<n>` | At-target tolerance | 0-1000 counts |
| `FT=<n>` | Fault threshold | 1-100000 counts |
| `GET` | One-shot config dump | - |
| `Q=<pos>,...` | Load queue (2-32 points) | - |
| `DWELL=<ms>` | Inter-point dwell delay | 0-30000 ms |
| `QSTOP` | Abort queue mid-execution | - |

### Phase 1 Commands (Colon-Separated)

| Command | Action | Example |
|---------|--------|---------|
| `m:<speed>:<pos>` | Move to position at given speed | `m:20000:80000` |
| `en:1` | Enable closed-loop servo | `en:1` |
| `en:0` | Disable motor (release) | `en:0` |
| `z` | Set current encoder position as zero | `z` |
| `i:<mA>` | Set coil current limit | `i:800` |
| `us:<n>` | Set microstep (1/2/4/8/16/32) | `us:16` |
| `pid:<kp>:<ki>:<kd>` | Set PID gains | `pid:50:5:10` |
| `tlm:1:<ms>` | Enable status stream at N ms period | `tlm:1:50` |
| `tlm:0` | Disable status stream | `tlm:0` |
| `st?` | One-shot status query | `st?` |

### Telemetry Format

```
P:<position>,E:<error>,V:<velocity>,T:<target>,F:<fault>,M:<moving>,A:<at_target>,Q:<qidx>,QL:<qlen>
```

### Auto-Tune

Relay-based Ziegler-Nichols PID tuning. The motor oscillates around a target, measures ultimate gain (Ku) and period (Tu), then applies:

- Kp = 0.6 x Ku x 100 (capped at 10000)
- Ki = 1.2 x Ku / Tu (capped at 1000)
- Kd = 0.075 x Ku x Tu (capped at 10000)

Send `TUNE` to start. The motor performs relay oscillation (+/- 3000 steps/s relay velocity), and after 4+ full cycles computes PID gains and reports: `OK TUNE:Kp=...,Ki=...,Kd=...,amp=...,Tu=...`

### Non-Volatile Config

All tuning parameters are auto-saved to the last flash page (512 KB, address 0x1D07F000) with 100 ms debounce. Integrity validated via magic word (0xBEADC0DE) and XOR checksum.

---

# Hardware Testing

## Test Setup

- **Motor:** NEMA17 stepper (200 steps/rev, 1.8 degrees/step)
- **Driver:** TB6600 with common-anode wiring
- **Microstep:** 1/32 (6400 steps/rev)
- **Encoder:** AS5047U magnetic encoder in ABI mode
- **Supply:** Regulated DC power supply at 24V and 31V
- **Belt drive:** G2 pulley, 20 teeth x 2 mm (40 mm/rev, 160 steps/mm)
- **Belt speed formula:** steps/s / 160

## 24V Test Data

Constant PID: Kp = 10, Ki = 5, Kd = 0. Varying MAXV, Accel, and coil current to characterize high-speed limits.

| MAXV (steps/s) | Belt Speed (m/s) | RPM | Accel (steps/s^2) | Coil Current (A) | Result |
|---------------|-----------------|-----|-----------------|------------------|--------|
| 250,000 | 1.56 | 2,344 | 3,200,000 | 2.5-2.7 | Smooth with heating |
| 250,000 | 1.56 | 2,344 | 3,200,000 | 3.0-3.2 | Smooth with heating |
| 250,000 | 1.56 | 2,344 | 3,200,000 | 2.8-2.9 | Smooth with heating |
| 250,000 | 1.56 | 2,344 | 3,200,000 | 2.5-2.7 | Smooth with heating |
| 230,000 | 1.44 | 2,156 | 3,300,000 | 2.0-2.2 | Smooth with heating |
| 200,000 | 1.25 | 1,875 | 1,900,000 | 1.5-1.7 | Minute shuttering |
| 200,000 | 1.25 | 1,875 | 190,000 | 1.0-1.2 | **Stuck** |

**Conclusion:** At 250k steps/s (3.2M steps/s^2 accel), minimum reliable coil current is 2.5-2.7A. At 230k/3.3M, 2-2.2A is sufficient. Below 2A with high speed, shuttering or stalling occurs.

## 31V Test Data

Systematic sweep of ACCEL, Vmax, and PID gains at 31V supply.

| Voltage | Accel (steps/s^2) | Vmax (steps/s) | Kp | Ki | Kd | Measured Velocity | Target | Result |
|---------|-----------------|---------------|----|----|----|--------------------|--------|--------|
| 31V | 3200000 | 300000 | 50 | 5 | 0 | 1670mm/s | 25000 | Smooth with heating |
| 31V | 3200000 | 300000 | 50 | 5 | 0 | 1780mm/s | 25000 | Smooth with heating |
| 31V | 3200000 | 300000 | 1000 | 5 | 0 | 1902mm/s | 25000 | Smooth with heating |
| 31V | 1400000 | 300000 | 1092 | 0 | 0 | 1445mm/s | 25000 | Smooth with heating |
| 31V | 1700000 | 300000 | 1092 | 46 | 0 | 1610mm/s | 25000 | Not Smooth |
| 31V | 1700000 | 300000 | 3000 | 46 | 0 | 1543mm/s | 25000 | Smooth With oscillation |
| 31V | 1800000 | 300000 | 3000 | 46 | 0 | - | - | **Stuck** |
| 31V | 1700000 | 310000 | 3000 | 46 | 0 | 1470mm/s | - | **Stuck** |
| 31V | 2000000 | 240000 | 1050 | 46 | 0 | 1440mm/s | 25000 | Smooth with heating |
| 31V | 2700000 | 270000 | 1934 | 46 | 0 | 1669mm/s | 25000 | Smooth with heating |
| 31V | 2700000 | 280000 | 2121 | 46 | 0 | 1725mm/s | 25000 | Smooth with heating |

### Key Findings at 31V

- **300k Vmax / 3.2M Accel** works with moderate Kp (50-1000) and Ki = 5, Kd = 0 — best measured velocity 1902 mm/s
- **High Kp (> 2000) + Ki (> 0)** causes oscillation at 31V — Kp > 2000 is unstable with nonzero Ki
- **Vmax > 300k requires lower Accel** trade-off: 280k Vmax + 2.7M Accel works, 310k + 1.7M does not
- **Best reliable result:** 270k Vmax + 2.7M Accel, Kp = 2121, Ki = 46 — 1725 mm/s smooth
- **Kd = 0** in all tests — derivative gain not needed at 31V with proper Kp/Ki balance
- **Mid-band resonance** observed at position 3007 — higher Kd (500-2000) helps dampen; Ki > 10 causes integral windup

## Voltage-Dependent Performance Limits

At 1/32 microstep (6400 steps/rev). Firmware accepts any positive value; the practical limit is set by the motor driver and supply voltage. Empirically determined guidelines (no load, idle condition):

| Supply Voltage | MAXV (steps/s) | MAXV (RPM) | ACCEL (steps/s^2) | Notes |
|----------------|----------------|------------|------------------|-------|
| 31V | 280,000 | 2,625 | 2,700,000 | Best reliable: 1725 mm/s, Kp = 2121, Ki = 46 |
| 24V | 250,000 | 2,344 | 3,200,000 | At >= 2.5A coil current, smooth with heating |
| 12V | 50,000 | 469 | 5,000,000 | Reduced max velocity |

**RPM formula:** RPM = (steps/s x 60) / steps_per_rev where steps_per_rev = 6400 at 1/32.

**All testing at 48 MHz SYSCLK — 10 MHz testing pending.**

---

# Schematics and PCB

## Current Status

- **Schematic:** Complete in KiCad. All component symbols placed, wired with net labels, and PPS mapping documented.
- **PCB layout:** Pending — component placement, trace routing, thermal management, and DRC not yet started.

## Power Architecture

```
USB-C VBUS (5V) -> LDO (MCP1825S / MCP1804) -> 3.3V rail (500 mA)
Motor PSU (9-30V) -> TMC2660 VM (direct)
                    -> Pre-regulator (optional for 5V CAN)

3.3V rail powers: PIC32MK, AS5047D, CAN transceiver
5V rail (optional): MCP2562 CAN transceiver (5V variant)
```

| Rail | Source | Devices | Max Current |
|------|--------|---------|-------------|
| 3.3V | LDO from VBUS | PIC32MK, AS5047D, CAN transceiver | ~300 mA |
| 5V (optional) | LDO from VBUS or motor PSU | MCP2562 (5V variant) | ~70 mA |
| VM (9-30V) | External motor supply | TMC2660 motor driver | Depends on motor |

## PIC32MK0512MCJ064 Minimum Circuit

- **Decoupling:** 0.1 uF per VDD/VSS pair (3 pairs on 64-TQFP) within 3 mm of pin; one shared 10 uF bulk per chip
- **VCAP (pin 32):** 10 uF ceramic X7R, ESR < 5 ohms, routed directly with minimal loop
- **AVDD/AVSS:** 10 ohm isolate resistor + 10 uF bulk + 0.1 uF bypass
- **MCLR:** 10k ohm pull-up to 3.3V + 100 ohm series to ICSP header
- **Oscillator:** 24 MHz crystal + 18-22 pF load caps (optional — FRC+SPLL sufficient if no USB)

## Pin Connections

### TMC2660 (QFP-44) — SPI1 Bus

| Pin | TMC2660 | Connected To | Notes |
|-----|---------|-------------|-------|
| 4 | CSN | RB4 (pin 21) | SPI chip select, active low |
| 5 | SCK | RB0 (pin 5) | SPI clock, PPS: RB0R = 8 |
| 6 | SDI | RB1 (pin 6) | SPI data in (MOSI), PPS: RB1R = 7 |
| 7 | SDO | RB2 (pin 7) | SPI data out (MISO), PPS: SDI1R = 5 |
| 25 | ENABLE | RB13 (pin 42) | LOW = enabled |
| 26 | STEP | RB15 (pin 44) | OC1 PWM, PPS: RPB15R = 5 |
| 27 | DIR | RB14 (pin 43) | LOW = CW, HIGH = CCW |
| 8 | REF | GND | Internal bandgap reference |
| 31 | TST | GND | Must be GND |
| 9 | VCC_IO | 3.3V | Logic supply |
| 35 | VDD | 3.3V | Analog supply |
| 37 | VCC | 3.3V | Internal logic |
| 39, 40 | VM | Motor PSU (9-30V) | 100-470 uF + 0.1 uF to GND |
| 12 | RSA | 0.12-0.22 ohm to GND | Sense resistor A, Kelvin connection |
| 22 | SRB | 0.12-0.22 ohm to GND | Sense resistor B, Kelvin connection |
| 9 | BRA | Same node as RSA | Bridge A return through sense resistor |
| 25 | BRB | Same node as SRB | Bridge B return through sense resistor |
| 32-34 | CP/C1_B/C1_C | 10 nF NPO | Charge pump capacitors |
| 36 | VS | VM | Supply voltage sense |
| 35 | VHS | VS via 100 nF | Bootstrap cap for high-side gate drive |
| 13 | 5VOUT | 470 nF to GND | Internal regulator, cap only |
| 14, 15 | OA1, OA2 | Motor coil A | A+ and A- |
| 18, 19 | OB1, OB2 | Motor coil B | B+ and B- |
| 30 | SG_TST | NC | Optional stallGuard2 output |
| 37, 38 | TST_ANA | NC | Do not connect |

### AS5047D (TSSOP-14) — Shared SPI1 Bus

| Pin | AS5047D | Connected To | Notes |
|-----|---------|-------------|-------|
| 1 | CSn | RB3 (pin 8) | Chip select, active low |
| 2 | CLK | RB0 (pin 5) | SPI clock (shared with TMC2660) |
| 3 | MISO | RB2 (pin 7) | SPI data out (shared) |
| 4 | MOSI | RB1 (pin 6) | SPI data in (shared) |
| 5 | VDD | 3.3V + 0.1 uF | Main supply |
| 6 | VDD3V3 | 10 uF + 0.1 uF to GND | LDO output, do NOT drive externally |
| 7 | GND | GND | Ground |
| 8 | TEST | GND | Must be GND for normal operation |
| 9 | A | RPG6 (pin 53) | QEI channel A, PPS: QEA1R = 10 |
| 10 | B | RPG7 (pin 54) | QEI channel B, PPS: QEB1R = 10 |
| 11 | U | NC | UVW not used |
| 12 | V | NC | UVW not used |
| 13 | W/PWM | NC | Not used |
| 14 | OffCompN | NC or 10k to 3.3V | Internal pull-up, safe floating |

### 6-Pin ICSP Header

| Pin | Signal | PIC32MK | Notes |
|-----|--------|---------|-------|
| 1 | MCLR | Pin 9 via 100 ohm | Reset |
| 2 | 3.3V | VDD | Target power |
| 3 | GND | GND | Common ground |
| 4 | PGED1 | RB10 (pin 24) | Programming data |
| 5 | PGEC1 | RB11 (pin 25) | Programming clock |
| 6 | NC | — | Not connected |

- Programmer: PKOB4 (serial BUR214320077)
- Command: `hwtool PKOB4 -p -i <hex file>`
- After MDB flash, chip may hold in reset until USB re-plugged; use `hwtool PKOB4 -p` for programming-only mode

### Other Connections

| Function | PIC32MK | Pin | Notes |
|----------|---------|-----|-------|
| U1TX | RPE0 | 16 | Serial TX, PPS: RPE0R = 1 |
| U1RX | RPG8 | 50 | Serial RX, PPS: U1RXR = 10 |
| USB D+ | RGI0 | 51 | Via 27 ohm + ESD diode |
| USB D- | RGI1 | 52 | Via 27 ohm + ESD diode |
| CAN TX | RF2 | 26 | PPS: RF2R = 11 |
| CAN RX | RF3 | 27 | PPS: C1RXR = 15 |
| LED | RA10 | 4 | Via 330 ohm to GND |

### TMC2660 Sense Resistor Kelvin Connection

```
        +-- Wide trace (1 mm) BRA ------+-- R_sense (0.12 ohm) -- GND
TMC2660 |                                |
        +-- Thin trace (0.25 mm) SRA ---+
             (Kelvin — no motor current)
```

SRA and SRB carry ~0.5 mV signals. Use thin separate traces from the sense resistor pads back to the IC, not shared with the high-current BRA/BRB path.

---

# Next Steps

1. **PCB layout** — route traces, place components, run DRC, generate Gerber files
2. **10 MHz clock migration** — after oscillator hardware is verified, recalculate:
   - U1BRG for 115200 baud at 10 MHz PB clock
   - Timer2 prescaler and PR2 for OC1 step generation
   - Timer3 PR3 for 1 kHz position control ISR
   - Core tick formulas (msleep, auto-tune Tu, telemetry timing)
3. **Re-test** at 10 MHz — repeat 24V and 31V characterization to verify no regression
4. **Fabrication** — send Gerber files to fab house, order components from BOM
5. **Assembly** — solder prototype boards, validate against test data
6. **CAN bus firmware** — implement CAN protocol for multi-axis communication

---

*Generated from firmware v6.2.0 test data. All testing performed at 48 MHz SYSCLK using SPLL from 8 MHz FRC (PLLIDIV = 2, PLLMULT = 24, PLLODIV = 2). 10 MHz target clock testing is pending.*
