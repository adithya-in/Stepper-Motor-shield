# TB6600 + NEMA17 Motor Test — Journey Log

## Overview

Goal: Drive a NEMA17 stepper motor using a TB6600 driver, controlled first by an Arduino Uno for validation, then by a PIC32MK0512MCJ064 on a Curiosity Pro board with closed-loop encoder feedback.

---

## Phase 1: Arduino Prototyping

### 1.1 Initial Wiring (Broken)

First attempt with the TB6600's common-anode wiring (all `-` signals tied to GND, `+` signals driven by MCU):

| TB6600 | Arduino | Note |
|---|---|---|
| PUL+ | D3 | Step pulse signal |
| DIR+ | D4 | Direction: LOW=CW, HIGH=CCW |
| **ENA+** | **D5** | ❌ Initially set HIGH to "enable" — wrong |
| PUL-/DIR-/ENA- | GND | All three commons tied together |
| V+ | 9-42V DC supply | |
| A+/A- | NEMA17 coil A | |
| B+/B- | NEMA17 coil B | |

**Symptom:** Motor vibrated/hummed but did not spin. Step LED on TB6600 was solid (not blinking).

### 1.2 ENA Polarity Fix

**Root cause:** In common-anode wiring, the TB6600 optocoupler works in reverse:

- **ENA+ LOW** → optocoupler OFF → **driver enabled** ✅
- **ENA+ HIGH** → optocoupler ON → **driver disabled** ❌

This is the opposite of common-cathode wiring and opposite of the typical TB6600 documentation.

**Fix:** Set `ENA_PIN = LOW` to enable. Motor immediately spun.

### 1.3 Arduino Test Code

**File:** `tests/arduino/motor_test.ino`

Final test sequence (verified working at 10 kHz step rate):
1. 16000 steps CW (~2.5 revs at 1/32 microstep = 6400 steps/rev), pause 2 s
2. 16000 steps CCW, pause 2 s
3. Loop forever

Step pulse: 50 µs HIGH, 50 µs LOW → 100 µs period → **10 kHz** step rate.

### 1.4 PlatformIO Build

**File:** `tests/arduino/platformio_test/`

PlatformIO project for Arduino Uno clone (ATmega16U2 USB, 57600 baud upload).

---

## Phase 2: PIC32MK Integration

### 2.1 Pin Mapping

After Arduino validation, the same control scheme was ported to PIC32MK using hardware-generated step pulses (no CPU-wasting `digitalWrite` loops):

| Signal | PIC32MK Pin | Function | TB6600 |
|---|---|---|---|
| STEP | **RPB15** (pin 44) | OC1 — Output Compare PWM | PUL+ |
| DIR | **LATB14** (pin 43) | GPIO | DIR+ |
| ENA | **LATB13** (pin 42) | GPIO (LOW = enable) | ENA+ |
| GND | GND | | PUL-/DIR-/ENA- |

### 2.2 Hardware Step Generation

Instead of bit-banging step pulses (Arduino approach), the PIC32MK uses **OC1** in PWM mode clocked by **Timer2**:

- Timer2 prescaler = 1:1, clocked at PB1CLK = 4 MHz
- PR2 = `(PB_CLOCK / STEP_RATE) - 1`
- OC1R = OC1RS = PR2 >> 1 (50% duty cycle)
- OC1 output mapped to RPB15 via `RPB15R = OC1_FN` (PPS remapping)

The CPU is free to poll the encoder, drive UART, etc. while the hardware generates precise step pulses.

### 2.3 Encoder Feedback

AS5047U magnetic encoder in ABI mode connected to QEI1:
- A → RPG6 (QEA1), B → RPG7 (QEB1)
- 4096 PPR, 4× decoding → 16384 counts/rev
- RPM calculated from position delta over 1 s sample window

### 2.4 Current Firmware Behavior

`newxc32_newfile.c` main loop:
1. Initialize UART (19200 baud), QEI encoder, and motor driver
2. Run **CW for 5 seconds**, report RPM via UART every second
3. Run **CCW for 5 seconds**, report RPM via UART every second
4. LED (RA10) toggles each second as heartbeat

---

## Phase 3: Closed-Loop Position Control

### Architecture

The firmware now runs a **1 kHz position control loop** (Timer3 ISR) that:

1. Reads actual position from QEI encoder (AS5047U, 16384 counts/rev)
2. Computes following error: `error = target_position - actual_position`
3. Checks fault threshold: if `|error| > FT`, triggers emergency stop
4. Runs PI controller: `velocity = Kp × error + Ki × ∫error`
5. Clamps velocity to `MAXV` (coil current limit enforcement)
6. Sets direction and OC1 step rate via hardware PWM
7. When within tolerance, stops OC1 and disables driver (position hold)

### UART Protocol

The firmware communicates with the dashboard at 19200 baud:

**Status telemetry** (10 Hz):  
`P:<pos>,E:<error>,V:<vel>,T:<target>,F:<fault>,H:<homed>,M:<moving>`

**Commands:**  
- `T=<pos>` — Set target position (triggers point-to-point move)
- `HOME` — Set current position as zero reference
- `STOP` — Emergency stop (motor off, target = current position)
- `CLEAR` — Clear fault and reinitialize motor pins
- `KP=<val>` — Set proportional gain (default 50)
- `KI=<val>` — Set integral gain (default 5)
- `MAXV=<val>` — Set max velocity in steps/s (default 2000)
- `TOL=<val>` — Set position tolerance in counts (default 5)
- `FT=<val>` — Set fault threshold in counts (default 500)

### Dashboard

Web-based control running on Node.js/Express with WebSocket:
- Real-time actual/target position bars
- Following error gauge (positive/negative/zero)
- Status LEDs: homed, moving, at-target, fault
- Velocity readout
- Target position input + GO button
- STOP, HOME, Clear Fault buttons
- Live tuning sliders for Kp, Ki, MaxV, Tolerance, Fault Threshold

---

## Key Lessons

| Issue | Lesson |
|---|---|
| **ENA polarity** | Common-anode wiring: LOW = enable, HIGH = disable. Check your optocoupler wiring! |
| **PPS unlock** | PIC32MK has three lock bits: PGLOCK, PMDLOCK, IOLOCK. All must be cleared. |
| **PB1 clock divider** | Default PB1DIV = SYSCLK/2. UART baud calculations must use actual PB1CLK. |
| **CFGCON address** | `0xBF800000` (not `0xBF80F610`) for PIC32MK family. Wrong address causes crash. |
| **Watchdog** | Config bits alone may not disable WDT reliably — use section-based config words. |
| **OC1 vs MC PWM** | OC1 is simple (2 register writes + PPS mapping). MC PWM module is complex (dead-time, fault handling) and overkill for step pulses. |

---

---

## Motor Test Results — Test Bench Data 1

Constant PID: Kp=10, Ki=5, Kd=0, 24V supply. Varying MAXV, Accel, and coil current to characterize high-speed limits.

| MAXV (steps/s) | Belt Speed (m/s) | RPM | Accel (steps/s²) | Kp | Ki | Kd | V | Coil Current (A) | Idle A | Result |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 250,000 | 1.56 | 2,344 | 3,200,000 | 10 | 5 | 0 | 24V | 3.5–4 | 0.676 | Smooth with heating |
| 250,000 | 1.56 | 2,344 | 3,200,000 | 10 | 5 | 0 | 24V | 3–3.2 | 0.662 | Smooth with heating |
| 250,000 | 1.56 | 2,344 | 3,200,000 | 10 | 5 | 0 | 24V | 2.8–2.9 | 0.642 | Smooth with heating |
| 250,000 | 1.56 | 2,344 | 3,200,000 | 10 | 5 | 0 | 24V | 2.5–2.7 | 0.550 | Smooth with heating |
| 230,000 | 1.44 | 2,156 | 3,300,000 | 10 | 5 | 0 | 24V | 2–2.2 | 0.292 | Smooth with heating |
| 200,000 | 1.25 | 1,875 | 1,900,000 | 10 | 5 | 0 | 24V | 1.5–1.7 | 0.223 | Minute Shuttering |
| 200,000 | 1.25 | 1,875 | 190,000 | 10 | 5 | 0 | 24V | 1–1.2 | 0.112 | **Motor Stuck** |

**Conclusion:** At 250k steps/s (3.2M steps/s² accel), minimum reliable coil current is 2.5–2.7A. At 230k/3.3M, 2–2.2A is sufficient. Below 2A with high speed, shuttering or stalling occurs.

---

## Phase 4: v6.1.0 — SPLL Clock, 115200 Baud, No Limits

> **Clock note:** All testing in this phase (and all 31V data) was performed at **48 MHz** SYSCLK (SPLL). Target production clock is **10 MHz** (external crystal or lower PLL config). All clock-dependent formulas (UART baud, Timer2/OC1 step rate, Timer3 ISR, core ticks, auto-tune timing) will need recalculation and retesting at 10 MHz — pending.

### Clock Migration (FRC → SPLL)

The firmware was migrated from FRC (8 MHz) to SPLL (48 MHz) to enable reliable 115200 baud serial communication:

| Parameter | v6.0.0 (FRC) | v6.1.0 (SPLL) |
|---|---|---|
| System clock | 8 MHz (FRC) | 48 MHz (PLLIDIV=2, PLLMULT=24, PLLODIV=2) |
| PB clock | 4 MHz | 48 MHz |
| Core timer | 4 MHz | 24 MHz |
| Timer2 (step) | 500 kHz (TCKPS=3) | 6 MHz (TCKPS=3, same prescaler) |
| UART baud | 19200 | 115200 (U1BRG=25, 0.16% error) |

### All Clock-Dependent Formulas Updated

Eight formulas were adjusted to match the new clock speeds:

1. **`msleep()`**: `ms * 4000u` → `MS(ms)` (core timer 4 MHz → 24 MHz)
2. **`motor_set_speed()`**: `500000 / sps` → `(PB_CLOCK / 8) / sps` (Timer2 500 kHz → 6 MHz)
3. **Auto-tune Tu**: `/4000.0f` → `/ ((float)TICKS_PER_MS * 1000.0f)` (core timer)
4. **Config save**: `PB_CLOCK / 10` → `MS(100)` (3× faster core timer)
5. **`tlm_ticks`**: `(uint32_t)` cast added for overflow safety
6. **`CONFIG_DATA_WORDS`**: 11 → 12 (struct padding fix for checksum)
7. **Timer3 PR3**: auto-scaled by `PB_CLOCK / CONTROL_FREQ` (already formula-based)

### Removed Command Limits

All hard-coded upper bounds removed from firmware:

| Parameter | Old limit | New limit |
|---|---|---|
| MAXV | ≤ 100,000 steps/s | Any positive value |
| ACCEL | ≤ 5,000,000 steps/s² | ≥ 100 |
| JERK | ≤ 10,000,000 steps/s³ | ≥ 1000 |
| m: speed | ≤ 100,000 steps/s | Any positive value |

### Belt Speed Example (G2 Pulley, 20 Teeth × 2 mm)

Given `MAXV=190000`, `ACCEL=1800000`, `JERK=500000`:

| Calculation | Value |
|---|---|
| Pulley circumference | 20 × 2 mm = **40 mm/rev** |
| Steps per rev | 200 steps × 32 microsteps = **6400 steps/rev** |
| Steps per mm | 6400 ÷ 40 = **160 steps/mm** |
| **Belt speed at MAXV** | 190,000 ÷ 160 = **1,187.5 mm/s = 1.19 m/s** |
| Accel in mm/s² | 1,800,000 ÷ 160 = **11,250 mm/s² ≈ 1.15 g** |
| Motor RPM | 190,000 ÷ 6400 × 60 = **1,781 RPM** |

### Dashboard Updates

- Default baud: 115200 (server.js, app.js, index.html)
- Telemetry toggle button (TLM ON/OFF)
- GET response parsing: position, error, velocity, TOL, FT fields

### Flash Script

`flash.mdb` — MDB programming script for command-line flashing:

```bash
rm -rf ~/.mplabcomm
/Applications/microchip/mplabx/v6.30/mplab_platform/bin/mdb.sh flash.mdb
# Must power-cycle PKoB4 after flashing (VCOM gets stuck)
```

---

---

## Motor Test Results — 31V Test Data

Constant PID varies. 31V supply. Systematic sweep of ACCEL, MAXV, and PID gains to characterize high-speed performance at 31V.

| Voltage | Accel (steps/s²) | Vmax (steps/s) | Kp | Ki | Kd | Measured Velocity | Target | Result |
|---------|-----------------|---------------|----|----|----|--------------------|--------|--------|
| 31V | 3200000 | 300000 | 50 | 5 | 0 | 1670mm/s | 25000 | Smooth with heating |
| 31V | 3200000 | 300000 | 50 | 5 | 0 | 1780mm/s | 25000 | Smooth with heating |
| 31V | 3200000 | 300000 | 1000 | 5 | 0 | 1902mm/s | 25000 | Smooth with heating |
| 31V | 1400000 | 300000 | 1092 | 0 | 0 | 1445mm/s | 25000 | Smooth with heating |
| 31V | 1700000 | 300000 | 1092 | 46 | 0 | 1610mm/s | 25000 | Not Smooth |
| 31V | 1700000 | 300000 | 3000 | 46 | 0 | 1543mm/s | 25000 | Smooth With oscillation |
| 31V | 1800000 | 300000 | 3000 | 46 | 0 | — | — | **Stuck** |
| 31V | 1700000 | 310000 | 3000 | 46 | 0 | 1470mm/s | — | **Stuck** |
| 31V | 2000000 | 240000 | 1050 | 46 | 0 | 1440mm/s | 25000 | Smooth with heating |
| 31V | 2700000 | 270000 | 1934 | 46 | 0 | 1669mm/s | 25000 | Smooth with heating |
| 31V | 2700000 | 280000 | 2121 | 46 | 0 | 1725mm/s | 25000 | Smooth with heating |

**Key Findings at 31V (all at 48 MHz — 10 MHz testing pending):**
- **300k MAXV / 3.2M ACCEL** works with moderate Kp (50–1000) and Ki=5, Kd=0 — best measured velocity 1902 mm/s
- **High Kp (3000) + Ki (46)** causes oscillation or stalling — Kp >~2000 at 31V is unstable with nonzero Ki
- **MAXV > 300k requires lower ACCEL** trade-off: 280k MAXV + 2.7M ACCEL works, 310k MAXV + 1.7M ACCEL does not
- **Best reliable result:** 270k MAXV + 2.7M ACCEL, Kp=2121, Ki=46 — 1725 mm/s smooth
- **Kd=0** in all tests — derivative gain not needed at 31V with proper Kp/Ki balance

---

## File Reference

| File | Description |
|---|---|
| `newxc32_newfile.c` | PIC32MK firmware: stepper + encoder + UART (v6.1.0) |
| `flash.mdb` | MDB command-line flash script |
| `blink_test.c` | Minimal GPIO blink test |
| `docs/PINOUT.md` | Wiring reference |
| `tests/arduino/motor_test.ino` | Arduino validation test |
| `tests/arduino/platformio_test/` | PlatformIO project for Arduino test |
