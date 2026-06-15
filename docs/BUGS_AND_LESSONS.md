# Bugs & Lessons Learned

## Stepper Motor Shield — Complete Troubleshooting Guide

Comprehensive catalogue of every bug encountered during development, root causes,
solutions, and preventative measures for future builders.

---

## 1. Firmware & Hardware Bugs

### 1.1 TB6600 ENA Polarity (Common-Anode Wiring)

| Aspect | Detail |
|--------|--------|
| **Symptom** | Motor vibrated/hummed but did not spin. Step LED solid (not blinking). |
| **Root Cause** | In common-anode wiring, TB6600 optocoupler works inverted: **ENA+ LOW = enabled**, ENA+ HIGH = disabled. Opposite of common-cathode docs. |
| **Fix** | Tie `ENA_PIN = LOW` to enable. All `-` pins (PUL-/DIR-/ENA-) to GND. |
| **Prevention** | Common-anode: `-` pins are the return path, all tied to GND. `+` pins are active-LOW for enable. |
| **File** | `firmware:newxc32_newfile.c:14` (`#define ENA_ON 0`) |

### 1.2 PPS Unlock (Three Lock Bits)

| Aspect | Detail |
|--------|--------|
| **Symptom** | UART RX, OC1 output, QEI encoder not working. PPS remapping ignored. |
| **Root Cause** | PIC32MK has three peripheral lock bits: `PGLOCK`, `PMDLOCK`, `IOLOCK`. All must be cleared before PPS changes take effect. Many examples only clear `IOLOCK`. |
| **Fix** | Clear all three: `SYSKEY = 0xAA996655; SYSKEY = 0x556699AA; PCFGLOCK=0; __builtin_write_RPCON(0); CFGCON=0; __builtin_write_RPCON(1);` |
| **Prevention** | Always clear all three locks when configuring PPS. |
| **File** | `newxc32_newfile.c` initialization sequence |

### 1.3 PB1 Clock Divider (UART Baud Rate)

| Aspect | Detail |
|--------|--------|
| **Symptom** | UART output garbled at expected baud rate. Wrong character data on serial. |
| **Root Cause** | PB1DIV defaults to SYSCLK/2. UART baud calculations use PB1CLK, not SYSCLK. |
| **Fix** | Calculate PB_CLOCK correctly (`SYSCLK / PB1DIV`). On Curiosity Pro with FRC = 8 MHz and PB1DIV=2, PB1CLK = 4 MHz. |
| **Prevention** | Always verify PB1DIV config bits and use `PB_CLOCK` (not `SYSCLK`) for all peripheral rate calculations. |
| **File** | `newxc32_newfile.c:11` (`#define PB_CLOCK 4000000`) |

### 1.4 CFGCON Address (PIC32MK vs PIC32MX)

| Aspect | Detail |
|--------|--------|
| **Symptom** | Writing `0xBF80F610` to unlock PPS causes crash or lockup. |
| **Root Cause** | `0xBF80F610` is the CFGCON register address for PIC32MX family. PIC32MK family uses `0xBF800000`. Wrong address = writes to unmapped memory. |
| **Fix** | Use `#define CFGCON_ADDR 0xBF800000` for PIC32MK0512MCJ064. |
| **Prevention** | Check the family reference manual, not copy-paste from PIC32MX examples. |
| **File** | Listed in `docs/TESTS.md` Key Lessons |

### 1.5 Watchdog Timer Not Disabled

| Aspect | Detail |
|--------|--------|
| **Symptom** | Firmware randomly resets after a few seconds. |
| **Root Cause** | Config bits alone may not reliably disable WDT on PIC32MK if not using section-based config words. |
| **Fix** | Use `#pragma config FWDTEN = OFF` and `#pragma config FDMTEN = OFF` in source. Verify in MPLABX config bits viewer. |
| **Prevention** | Always disable WDT explicitly in `#pragma config` directives. |
| **File** | `newxc32_newfile.c:6-7` |

### 1.6 Timer3 ISR Never Fired (Wrong Vector Bit)

| Aspect | Detail |
|--------|--------|
| **Symptom** | Position control loop dead. Motor never responds to commands. PID ISR never runs. |
| **Root Cause** | `IFS0CLR` and `IEC0SET` used bit 12 for Timer3. On PIC32MK0512MCJ064, Timer3 uses **bit 14** (vector `_TIMER_3_VECTOR = 14`). Bit 12 is a different peripheral. |
| **Fix** | Change to bit 14: `IFS0CLR = 1 << 14; IEC0SET = 1 << 14;`. |
| **Prevention** | Always verify interrupt vector numbers in the specific device family data sheet. PIC32MK is NOT the same as PIC32MX. |
| **Severity** | **CRITICAL** — ISR was dead since firmware inception (v1.0.0 through v5.0.0 release). PID control never actually ran. |
| **File** | `newxc32_newfile.c` Timer3 init |

### 1.7 Global Interrupts Never Enabled

| Aspect | Detail |
|--------|--------|
| **Symptom** | All interrupts configured but never fire. Even correct vector numbers don't work. |
| **Root Cause** | `__builtin_enable_interrupts()` was missing from `main()`. Without it, the CPU never enters interrupt mode regardless of peripheral setup. |
| **Fix** | Add `__builtin_enable_interrupts()` after all interrupt configuration. |
| **Prevention** | This is MIPS core 101 — always call `__builtin_enable_interrupts()` in `main()` before any interrupt-driven code. |
| **Severity** | **CRITICAL** — compounded with bug 1.6, the PID loop was doubly dead. |
| **File** | `newxc32_newfile.c` `main()` |

### 1.8 Fault Triggered Mid-Move

| Aspect | Detail |
|--------|--------|
| **Symptom** | Motor starts a large move, immediately faults. Emergency stop kills valid moves. |
| **Root Cause** | Fault threshold check ran every ISR cycle including during acceleration. Following error naturally exceeds FT during a fast move, but that is normal. Fault should only trigger when the system claims to be at target. |
| **Fix** | Fault threshold only checked when `at_target == 1`. Added 50ms debounce counter to filter transient overshoot. |
| **Prevention** | Distinguish between transient following error (normal during motion) and steady-state error (actual fault). |
| **File** | `newxc32_newfile.c` ISR fault check |

### 1.9 Motor Disabled at Target

| Aspect | Detail |
|--------|--------|
| **Symptom** | Motor reaches position, stops, then releases (goes limp). No holding torque. |
| **Root Cause** | The ISR called `motor_disable()` when position matched target, cutting power to the coils. |
| **Fix** | Keep motor enabled at target. Only disable on explicit `OFF`, `en:0`, or fault. Holding torque requires ENA+ LOW at all times. |
| **Prevention** | Holding torque is a requirement for position control: never disable the driver just because you arrived. |
| **File** | `newxc32_newfile.c` ISR arrival handling |

### 1.10 Integral, Velocity, Acceleration Not Reset Across Moves

| Aspect | Detail |
|--------|--------|
| **Symptom** | Second `T=` command behaves differently from the first. Residual velocity causes overshoot or slow response. |
| **Root Cause** | `integral`, `sm_vel`, `sm_acc` are `static` and retain their values from the previous move. No reset on new target. |
| **Fix** | On `T=` command: zero `integral`, `prev_error`, `sm_vel`, `sm_acc`. |
| **Prevention** | All state variables must be reinitialized when starting a new positioning cycle. |
| **File** | `newxc32_newfile.c` `cmd_T` handler |

### 1.11 Acceleration Not Reset on STOP

| Aspect | Detail |
|--------|--------|
| **Symptom** | After `STOP`, next move starts with residual acceleration values. Jerky initial motion. |
| **Root Cause** | Same root cause as 1.10 — `sm_vel` and `sm_acc` not cleared on `STOP`. |
| **Fix** | Zero `sm_vel`, `sm_acc`, `integral`, `prev_error` in `STOP` handler. |
| **Prevention** | Every command that alters motion state must reset the motion profile integrators. |
| **File** | `newxc32_newfile.c` `cmd_STOP` handler |

### 1.12 UART RX Overrun Lockup

| Aspect | Detail |
|--------|--------|
| **Symptom** | After sending several commands rapidly, UART stops responding entirely. Firmware running but serial dead. |
| **Root Cause** | UART receiver overrun error (`OERR` flag) halts all further reception until cleared. The RX FIFO fills up, OERR sets, and the UART module stops accepting new data. |
| **Fix** | Added `uart_clear_oerr()`: check `U1STAbits.OERR`, if set, clear it and drain the FIFO (`U1RXREG` read loop). |
| **Prevention** | Always handle OERR in UART RX ISR or polling loop. Even at 19200 baud, commands can arrive faster than they're processed. |
| **File** | `newxc32_newfile.c` UART receive |

### 1.13 SPEED Command Parsing Bug

| Aspect | Detail |
|--------|--------|
| **Symptom** | `SPEED 5000` unexpectedly triggers STOP. Motor stops instead of changing speed. |
| **Root Cause** | The digit `0` in `5000` matched the single-character `0` handler (STOP command). The parser checked single-char commands on every input byte, not just at the start of a line. |
| **Fix** | Single-char handlers (`0`, `z`, etc.) only match when `rx_idx == 0` (no prior characters received). |
| **Prevention** | Stateful parsers must check receive index before matching short commands. |
| **File** | `newxc32_newfile.c` command parser |

### 1.14 PPS RX Pin Mapping Wrong

| Aspect | Detail |
|--------|--------|
| **Symptom** | UART RX not receiving any data. TX works fine. |
| **Root Cause** | `U1RXR = 1` mapped U1RX to the wrong pin. RPG8 (pin 54) requires `U1RXR = 10`. |
| **Fix** | `U1RXR = 10;` — maps RPG8 as U1RX input. |
| **Prevention** | Verify PPS input numbers in the datasheet's "Input and Output Maps" section. |
| **File** | `newxc32_newfile.c` UART init |

### 1.15 UART Receiver Never Enabled

| Aspect | Detail |
|--------|--------|
| **Symptom** | UART TX works, RX does not — even with correct PPS mapping. |
| **Root Cause** | `U1STAbits.URXEN = 1` was missing. The receiver was never turned on. |
| **Fix** | Add `U1STAbits.URXEN = 1` after baud rate configuration. |
| **Prevention** | The UART has separate TXEN and RXEN bits. RX is NOT enabled by default even when the module is on. |
| **File** | `newxc32_newfile.c` UART init |

### 1.16 OC1 vs MC PWM Module Selection

| Aspect | Detail |
|--------|--------|
| **Symptom** | Spent significant time trying to configure the dedicated Motor Control PWM module. Complex register setup with dead-time, fault handling, complementary outputs. |
| **Root Cause** | There are two PWM options: Output Compare (OC1) and Motor Control PWM (MC PWM). MC PWM is overkill for simple step pulses. |
| **Fix** | Use OC1 in PWM mode: 2 register writes (`OC1R`, `OC1RS`) + PPS remapping (`RPB15R = OC1_FN`). Simple, CPU-free step pulse generation. |
| **Prevention** | For stepper step pulses, use OC1 PWM, not the MC PWM module. The MC module is designed for 3-phase BLDC with dead-time insertion. |
| **File** | `newxc32_newfile.c` OC1 init |

---

## 2. Build, Flash & IDE Issues

### 2.1 MPLABX MDB Flash Failure (PKoB4 Communication Loss)

| Aspect | Detail |
|--------|--------|
| **Symptom** | `mdb.sh flash.mdb` fails during second programming pass. Error: "PKOB communication failure" or device not responding after config word erase. |
| **Root Cause** | MDB CLI performs two passes: (1) program main flash, (2) erase+program config words. During step 2, erasing config memory can glitch the PKoB4 debugger's connection, especially at default programming speeds. The PKoB4 VCOM disappears or resets. |
| **Fix** | Use **MPLABX IDE GUI** to program (right-click project → Make and Program Device → PKoB4). The IDE uses a more robust communication protocol with retry logic. Alternatively, power-cycle the PKoB4 after failed MDB flash and try again. |
| **Workaround** | Power-cycle PKoB4 (unplug/replug USB) between every flash attempt. This restores VCOM enumeration. |
| **Prevention** | Prefer IDE GUI for production flashing. Reserve MDB CLI for CI/automation where power-cycle is feasible. Do NOT use `rm -rf ~/.mplabcomm` — it doesn't help and loses settings. |
| **When it works** | MDB CLI flashing may succeed if PKoB4 was just power-cycled AND there is no config word erase needed. First MDB flash after power cycle often works. |

### 2.2 PKoB4 VCOM Enumeration

| Aspect | Detail |
|--------|--------|
| **Symptom** | After flashing, `/dev/cu.usbmodemBUR2143200772` does not appear. No serial port. |
| **Root Cause** | The PKoB4 debugger's VCOM port may not re-enumerate correctly after the debug session ends. This is a known issue with PICkit-on-Board (PKoB4) firmware. |
| **Fix** | Unplug and replug the USB cable to power-cycle the PKoB4. The VCOM reappears within 2–3 seconds. |
| **Prevention** | Always factor "power-cycle PKoB4 after flash" into your workflow. This is not optional. |
| **Cost** | 5 seconds per flash cycle. Factor this into your iteration speed. |

### 2.3 Clean Build Artifact Confusion

| Aspect | Detail |
|--------|--------|
| **Issue** | After fixing bugs, the old `.hex` may still be used. MPLABX does not always regenerate on "Build" if timestamps look OK. |
| **Fix** | Use "Clean and Build" (or `mdb.sh` with fresh `make clean`) to force recompilation. Verify hex modification time. |
| **Prevention** | Always "Clean and Build" after any source change. Build alone may skip recompilation. |

---

## 3. Dashboard & UI Bugs

### 3.1 Browser Cache Stale HTML/JS

| Aspect | Detail |
|--------|--------|
| **Symptom** | Dashboard shows old parameter limits (e.g., DWELL max = 1000 instead of 30000, speed slider max = 10000 instead of 50000). |
| **Root Cause** | Browser aggressively caches `index.html`, `app.js`, `styles.css`. Updated files on disk are not reflected in the browser. |
| **Fix** | **Hard refresh**: `Cmd+Shift+R` (Mac) or `Ctrl+F5` (Windows/Linux). This bypasses the browser cache completely. |
| **Server-side fix** | Add `res.setHeader('Cache-Control', 'no-store')` to server.js for all `/public` files. |
| **Prevention** | Always hard refresh the dashboard after any file change. Add `Cache-Control: no-store` to server if users keep hitting stale caches. |
| **Symptom checklist** | If you changed a file but the dashboard looks the same, assume cache before assuming the change didn't work. |

### 3.2 Telemetry Overwriting Input Fields

| Aspect | Detail |
|--------|--------|
| **Symptom** | While typing a value (e.g., ACCEL = `123000`), telemetry update overwrites the field mid-typing with the previous value. User cannot finish typing. |
| **Root Cause** | WebSocket telemetry arrives every 100ms and sets `input.value` unconditionally for all fields including the one the user is editing. |
| **Fix** | Guard each field write: `if (document.activeElement !== els.accelInput) els.accelInput.value = data.accel;` — only update fields that don't have focus. |
| **Prevention** | Any field that sends its value on blur/button (not on every keystroke) needs activeElement protection against telemetry overwrite. |
| **Affected fields** | ACCEL, JERK, MAXV, DWELL. |
| **File** | `dashboard/public/app.js:172-175` |

### 3.3 Queue Input Space Handling

| Aspect | Detail |
|--------|--------|
| **Symptom** | `Q=10000, 20000, 30000` (with spaces after commas) fails silently or parses wrong. |
| **Root Cause** | Firmware string split on `,` includes spaces in the token. `strtol(" 20000")` can produce unexpected results. |
| **Fix** | Dashboard strips spaces before sending: `points.split(',').map(s => s.trim()).join(',')`. Firmware also handles leading spaces in `strtol`. |
| **Prevention** | Always sanitize user input on both client and server. Never trust the browser. |
| **File** | `dashboard/public/app.js` queue send + `newxc32_newfile.c` Q handler |

---

## 4. Electrical & Wiring Issues

### 4.1 TB6600 DIP Switch vs Motor Current

| Aspect | Detail |
|--------|--------|
| **Issue** | Wrong current setting on S1/S2/S3 causes motor to run hot (too high) or weak torque (too low). |
| **Fix** | Set DIP switches to match NEMA17 rated current. Example: for a 1.5A motor, set S1=OFF, S2=ON, S3=OFF (check TB6600 datasheet for your specific model's table). |
| **Note** | There are multiple TB6600 variants with different current tables. Verify before setting. |

### 4.2 Encoder ABI Wiring

| Aspect | Detail |
|--------|--------|
| **Issue** | QEI counts go backwards or give erratic readings. |
| **Root Cause** | A and B channels swapped, or encoder not aligned to motor phase. |
| **Fix** | Swap A and B on the PIC32MK QEI inputs, or swap on the encoder board. |
| **Note** | AS5047U in ABI mode: A → RPG6 (QEA1), B → RPG7 (QEB1). Verify your encoder board labeling. |

### 4.3 Encoder + Motor Mechanical Coupling

| Aspect | Detail |
|--------|--------|
| **Issue** | Encoder magnet slips relative to motor shaft. Position control oscillates or never settles. |
| **Root Cause** | The AS5047U requires a diametric magnet centered on the shaft. If the magnet is off-center, the reading is non-linear. If it slips, position is lost. |
| **Fix** | Secure magnet with epoxy or set screw. Verify with `st?`: when you rotate the motor by hand, counts should increase monotonically. |
| **Prevention** | Mechanical coupling is as important as the control algorithm. A perfect PID can't fix a slipping magnet. |

---

## 5. Non-Volatile Memory (NVM) Issues

### 5.1 Flash Config Magic/Checksum Mismatch

| Aspect | Detail |
|--------|--------|
| **Symptom** | After power cycle, all settings are back to defaults. NVM save appears to work but load fails. |
| **Root Cause** | Magic word (`0xBEADC0DE`) or XOR checksum mismatch on load. Common causes: partial write, wrong flash address, or flash page not erased before programming. |
| **Fix** | On boot: validate magic and checksum. If mismatch, use defaults and re-save. This is self-healing. |
| **File** | `newxc32_newfile.c` `config_load()` and `config_save()` |

### 5.2 Flash Write Debounce

| Aspect | Detail |
|--------|--------|
| **Symptom** | Multiple rapid parameter changes cause flash write collisions. Config corrupts. |
| **Root Cause** | Flash page erase takes ~20ms. Writing parameters one at a time without debounce causes queue buildup. |
| **Fix** | `config_mark_dirty()` sets a flag, then a 100ms timer debounce before actual flash write. Only the last value in the window is persisted. |
| **Prevention** | Flash has limited write endurance (~10K cycles). Debounce is essential for both data integrity and flash lifetime. |

---

## 6. Testing & Validation

### 6.1 Serial Terminal Echo Confusion

| Aspect | Detail |
|--------|--------|
| **Issue** | Typing commands in `screen` — characters echo locally AND the device responds. Hard to read. |
| **Fix** | `screen /dev/cu.usbmodem... 19200` has local echo off by default. If using `picocom`, add `--echo` flags carefully. |
| **Tip** | Use `screen` with `C-a :` then `exec !!` to log session. |

### 6.2 Open-Loop Test vs Closed-Loop PID

| Aspect | Detail |
|--------|--------|
| **Issue** | Confusion between `ON`/`OFF`/`CW`/`CCW`/`SPEED` (open-loop test mode) and `T=`/`en:1`/`m:` (closed-loop position control). They are different modes. |
| **Clarification** | Open-loop: motor spins continuously at set speed ignoring encoder. Closed-loop: motor servos to encoder position. Do NOT use both simultaneously. Open-loop is for wiring verification only. |
| **Safety** | Open-loop can drive motor to end-stop without feedback. Use with caution. |

---

## 7. Release & Versioning

### 7.1 GitHub Release Hex Attachment

| Aspect | Detail |
|--------|--------|
| **Process** | After tagging, use `gh release create` with the hex file: `gh release create v6.0.0 ./tmp/controller_v6.0.0.hex --title "v6.0.0" --notes "..."` |
| **Note** | The hex file must be a clean production build (not debug). Remove `.debug` from filename if present. |
| **Tip** | Always verify hex file modification time matches build time. |

### 7.2 MPLABX Project File Changes

| Aspect | Detail |
|--------|--------|
| **Issue** | Modifying `p32MK0512MCJ064.ld` (linker script) causes MPLABX to lose track of project. |
| **Fix** | Only modify linker settings through the IDE GUI (File → Project Properties → XC32 → Linker → Option categories → Memory model). Do NOT hand-edit `.ld` files. |

---

## Summary: Top 5 Most Painful Bugs

| # | Bug | Hours Lost | Lesson |
|---|-----|------------|--------|
| 1 | **Timer3 ISR bit 12 vs 14** | ~4 | Always verify vector numbers in device-specific datasheet, not copy-paste |
| 2 | **Global interrupts not enabled** | ~2 | Call `__builtin_enable_interrupts()` — this is MIPS core 101 |
| 3 | **MDB flash failure** | ~2 | Use MPLABX IDE GUI, not MDB CLI. Power-cycle PKoB4 between flashes. |
| 4 | **TB6600 ENA polarity** | ~1.5 | Common-anode wiring: LOW = enable. Read the wiring guide carefully. |
| 5 | **UART RX OERR lockup** | ~1 | Always clear OERR and drain FIFO. Every UART needs an overrun handler. |

---

*Last updated: 2026-06-15*
*Phase 1 completion document — captures all knowledge from v1.0.0 through v6.0.0*
