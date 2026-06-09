# Changelog

## v2.0.0 — 2026-06-09

Second release. Adds TB6600 stepper driver control and full documentation of the testing journey.

### Features
- TB6600 NEMA17 stepper control via OC1 (hardware PWM step generation)
- OC1 + Timer2: 50% duty PWM at configurable `STEP_RATE` (default 1 kHz)
- Step output on RPB15 (OC1, pin 44), mapped via PPS (`RPB15R = OC1_FN`)
- Direction on RPB14 (LOW=CW, HIGH=CCW)
- Enable on RPB13 (LOW=enable — critical discovery for common-anode wiring)
- CW 5 s → CCW 5 s test loop with UART RPM reporting
- LED (RA10) heartbeat/second indicator

### Bug Fixes
- **ENA polarity**: Initial firmware assumed ENA+ HIGH = enable. In common-anode wiring, TB6600 optocoupler is active-low: LOW = optocoupler OFF = driver enabled. Fixed by setting `ENA_ON = 0`.

### Documentation
- `docs/TESTS.md` — Full journey log from Arduino prototyping through PIC32MK integration
- `docs/PINOUT.md` — Complete wiring reference with physical pin numbers
- `tests/arduino/` — Arduino validation test code (standalone .ino + PlatformIO project)

## v1.0.0 — 2026-06-08

First working release. Firmware reads RPM and direction from an AS5047U magnetic encoder via QEI1, and outputs values over UART (19200 baud) to a web dashboard.

### Features
- AS5047U ABI encoder (4096 PPR, 4× decoding → 16384 counts/rev)
- QEI1 configured in 4× count mode with digital noise filtering
- Velocity capture (QCAPEN) for accurate position delta sampling
- RPM calculation: `delta × 60000 / (16384 × dt_ms)`
- Direction detection from POS1CNT delta sign
- UART1 output at 19200 baud (8-N-1) with startup banner
- LED2 (RA10) heartbeat toggle in main loop
- Web dashboard (Node.js/Express/SerialPort): RPM gauge, direction arrows, port selector

### Hardware
- Target: PIC32MK0512MCJ064 on Curiosity Pro board
- Encoder: AS5047U (ABI mode, CSn pull-up, CLK/MOSI pull-down)
- A → MC header pin 27 (RPG6 / QEA1)
- B → MC header pin 29 (RPG7 / QEB1)
- VCOM via PKoB4 debugger (U1TX=RPE0, U1RX=RPG8)

### Clock Configuration
- FNOSC = FRC (8 MHz internal oscillator, no PLL)
- PB1CLK = SYSCLK / 2 = 4 MHz (default divider)
- Core timer: 4 MHz (SYSCLK/2), 4000 ticks/ms

### Bug Fixes
- **CFGCON address**: Was `0xBF80F610` (nonexistent), causing PPS unlock to crash the MCU. Fixed to `0xBF800000`.
- **PB1 clock divider**: Hardcoded `PB_CLOCK = 8000000` ignored the default divide-by-2 (actual PB1CLK = 4 MHz). This caused the UART baud to be wrong (8.5% error at 38400). Fixed by using `PB_CLOCK = 4000000` and 19200 baud (0.16% error).
- **PPS lock bits**: Only `IOLOCK` (bit 13) was cleared during PPS unlock. On PIC32MK, PPS has three separate lock bits: `PGLOCK` (bit 11), `PMDLOCK` (bit 12), and `IOLOCK` (bit 13). Fixed by clearing all three.
- **Watchdog timer**: `#pragma config FWDTEN = OFF` was not reliably disabling the WDT, causing periodic resets. Fixed by using direct section-based config word assignment.

### Known Issues
- Dashboard default baud must match firmware (19200)
- The `hex` and `elf` build artifacts are not tracked in git (see `.gitignore`)
- Index pulse (INDX1) not used — RPM/direction work without it

## v0.1.0 — 2026-06-06

Initial development version. Builds but UART serial output was garbled / non-functional due to multiple configuration bugs (see fixes above).
