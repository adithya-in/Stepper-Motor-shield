# Stepper Motor Shield — TB6600 + PIC32MK + Encoder

PIC32MK0512MCJ064 firmware driving a NEMA17 stepper via TB6600 driver with AS5047U
magnetic encoder feedback. Controlled over USB serial (19200 baud) or a web dashboard.

## Quick Start

| Step | What |
|------|------|
| 1. Wire TB6600 | See [PINOUT.md](docs/PINOUT.md) — all `-` pins to GND, `+` pins to MCU |
| 2. Build & flash | Open in MPLAB X or run `make` + MDB (see below) |
| 3. Connect serial | `screen /dev/cu.usbmodemBUR2143200772 19200` |
| 4. Commands | `ON`, `OFF`, `CW`, `CCW`, `SPEED 5000` (or single chars: `0`=off, `1`=on, `2`=CW, `3`=CCW) |

## Pinout

| Signal | PIC32MK | TB6600 | Note |
|--------|---------|--------|------|
| STEP | RPB15 (pin 44) | PUL+ | OC1 PWM, 50% duty |
| DIR | RPB14 (pin 43) | DIR+ | LOW=CW, HIGH=CCW |
| ENA | RPB13 (pin 42) | ENA+ | **LOW=enable**, HIGH=disable |
| GND | GND | PUL-/DIR-/ENA- | **All tied together** |
| UART TX | RPE0 (pin 1) | — | 19200 baud |
| UART RX | RPG8 (pin 54) | — | PPS input via U1RXR=10 |
| QEI A | RPG6 (pin 52) | AS5047U A |
| QEI B | RPG7 (pin 53) | AS5047U B |

## Commands

| Command | Action |
|---------|--------|
| `ON` | Enable motor at current speed |
| `OFF` | Disable (coast) |
| `CW` | Set direction clockwise |
| `CCW` | Set direction counter-clockwise |
| `SPEED <n>` | Set speed in steps/s (0–10000) |
| `0` | STOP (single-char alias) |
| `1` | START (single-char alias) |
| `2` | CW (single-char alias) |
| `3` | CCW (single-char alias) |

Status output every 100ms:
```
RPM:4,DIR:CW,ON:1,POS:54801
```

## UART Serial Fixes (v4.0.0)

These fixes were required to get UART RX/TX working reliably:

| Issue | Root Cause | Fix |
|-------|-----------|-----|
| No RX | `U1RXR = 1` maps wrong PPS input | Changed to **`U1RXR = 10`** (RPG8 → U1RX) |
| No RX | Receiver not enabled | Added **`U1STAbits.URXEN = 1`** |
| RX lockup | Overrun error (OERR) never cleared | Added **`uart_clear_oerr()`** — clears OERR and flushes RX FIFO |
| `0` digit in `SPEED 5000` triggers STOP | Single-char `0` handler matched before string buffer | Single-char commands only processed when **`rx_idx == 0`** |

## Dashboard

```bash
cd dashboard && node server.js
# → http://localhost:3000
```

RPM gauge, speed slider (0–10000), direction toggle, start/stop, encoder position.

## TB6600 Common-Anode Wiring

The TB6600 uses optocouplers on all control inputs:
- **PUL-/DIR-/ENA- → GND** (all tied together)
- **PUL+/DIR+/ENA+** driven by MCU GPIO
- **ENA+ LOW = enabled**, ENA+ HIGH = disabled

Do NOT force ENA+/DIR+ to 5V or GND while the MCU is driving them — this shorts the
output and can damage the pin.

## Tool Reservation Fix

MDB leaves stale tool reservations in `~/.mplabcomm/tool-reservations.dat` after
a crash or kill. Delete the file between flash attempts:

```bash
rm -f ~/.mplabcomm/tool-reservations.dat
```

## Releases

- **v4.0.0** — UART serial fixes + stable speed control + dashboard
- **v3.1.0** — Open-loop speed control (browser slider, RPM display)
- **v3.0.0** — Closed-loop position control with PI controller
- **v2.0.0** — Stepper control + encoder feedback
- **v1.0.0** — Encoder-only firmware
