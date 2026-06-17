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

## File Reference

| File | Description |
|---|---|
| `newxc32_newfile.c` | PIC32MK firmware: stepper + encoder + UART |
| `blink_test.c` | Minimal GPIO blink test |
| `docs/PINOUT.md` | Wiring reference |
| `tests/arduino/motor_test.ino` | Arduino validation test |
| `tests/arduino/platformio_test/` | PlatformIO project for Arduino test |
