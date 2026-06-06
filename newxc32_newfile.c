/*
 * AS5047U Rotary Encoder → PIC32MK MCJ Curiosity Pro (QEI + UART)
 * =====================================================================
 *
 * HARDWARE CONNECTION (AS5047U TSSOP-14 to Motor Control Header):
 *
 *   AS5047U pin 7 (A)  → MC header pin 27  → PIC32MK RPG6 (QEA1 input)
 *   AS5047U pin 6 (B)  → MC header pin 29  → PIC32MK RPG7 (QEB1 input)
 *   AS5047U pin 14 (I) → MC header pin 28  → PIC32MK RA7  (optional index)
 *   AS5047U pin 1 (CSn)→ VDD via 10k pull-up       (SPI not used)
 *   AS5047U pin 2 (CLK)→ GND via 10k pull-down
 *   AS5047U pin 4 (MOSI)→ GND via 10k pull-down
 *   AS5047U pin 3 (MISO)→ leave open
 *   AS5047U pin 5 (TEST)→ GND
 *   AS5047U pin 11 (VDD)→ 5V supply rail
 *   AS5047U pin 12 (VDD3V3)→ 1uF cap to GND
 *   AS5047U pin 13 (GND)→ GND
 *
 * HOW THE AS5047U WORKS:
 *   The AS5047U is a magnetic rotary encoder that outputs absolute
 *   position (14-bit via SPI) AND incremental quadrature (A/B/I).
 *   In ABI mode it outputs 4096 pulses per revolution (PPR) by
 *   default on the A and B lines.  The PIC's QEI module counts
 *   both edges of A and B (4x decoding) → 16384 counts/rev.
 *   Direction is determined by which signal leads the other.
 *
 * HOW THE PIC QEI MODULE WORKS:
 *   The Peripheral Pin Select (PPS) system lets us route the QEI
 *   input signals to any PPS-capable pin.  We write the pin's PPS
 *   value to the QEA1R / QEB1R registers.  Then the QEI counter
 *   (POS1CNT) increments/decrements on every quadrature edge.
 *
 * VIRTUAL COM PORT (VCOM):
 *   The PKoB4 debugger provides a USB serial port.  On this board
 *   it connects to UART1, with TX on pin RPE0 and RX on pin RPG8.
 *   We output RPM and direction at 38400 baud.
 *
 * CLOCK:
 *   We use the FRC oscillator (8 MHz) with no PLL for simplicity.
 *   If you enable PLL later, update PB_CLOCK accordingly.
 */

#include <xc.h>

// =====================================================================
// CONFIGURATION BITS  (these set the hardware at startup)
// =====================================================================
#pragma config FNOSC   = FRC       // Fast RC oscillator (8 MHz, no PLL)
#pragma config POSCMOD = OFF       // Primary oscillator disabled
#pragma config FWDTEN  = OFF       // Watchdog timer disabled
#pragma config FDMTEN  = OFF       // DMT disabled
#pragma config ICESEL  = ICS_PGx2  // ICSP on PGx2 pins
#pragma config JTAGEN  = OFF       // JTAG off (frees up those pins)

// =====================================================================
// CONSTANTS
// =====================================================================

// The AS5047U outputs 4096 pulses per revolution (ABI default).
// The QEI counts ALL edges of A and B (4x decoding):
//    counts_per_rev = PPR * 4 = 4096 * 4 = 16384
#define ENCODER_PPR     4096

// How often we print RPM data (milliseconds)
#define SAMPLE_MS       500

// Peripheral bus clock (FRC = 8 MHz).  Used to calculate UART baud.
#define PB_CLOCK        8000000

// UART baud rate for the virtual COM port
#define UART_BAUD       38400

// PPS output function number for U1TX (= 1 according to Table 11-2)
#define U1TX_FN         1

// PPS pin value for QEA1/QEB1 inputs.
// From Table 11-1 in DS60001570D:
//   QEA1 value 10 → RPG6
//   QEB1 value 10 → RPG7
#define PPS_RPG6  10
#define PPS_RPG7  10

// U1RX PPS input value for RPG8.
// From the generic PPS example (Figure 11-2), value 1 selects RPG8
// as the U1RX input.  This matches the Curiosity Pro schematic.
#define U1RX_PIN_VAL   1

// =====================================================================
// DATA TYPE
// =====================================================================

typedef struct {
    int rpm;   // revolutions per minute (always non-negative)
    int dir;   // +1 = forward (CW), -1 = reverse (CCW)
} rpm_result;

// =====================================================================
// CORE TIMER  (a free-running 32-bit counter at half the sysclk)
// =====================================================================
// The MIPS core timer increments every 2 CPU cycles.
// At 8 MHz sysclk:  1 tick = 0.25 µs,  1 ms = 4000 ticks
static inline unsigned int core_ticks(void)
{
    return _CP0_GET_COUNT();
}

// =====================================================================
// SYSKEY / PPS UNLOCK SEQUENCE
// =====================================================================
// On PIC32, certain registers are "locked" to prevent accidental
// changes.  To unlock you must write the magic keys to SYSKEY.
// The PPS registers have an additional lock bit (bit 13 of CFGCON).
// The sequence is:  write SYSKEY keys, then write CFGCON.

static void syskey_unlock(void)
{
    SYSKEY = 0xAA996655;
    SYSKEY = 0x556699AA;
}

static void pps_unlock(void)
{
    syskey_unlock();
    unsigned int cfgcon_val = *(volatile unsigned int *)0xBF80F610;
    *(volatile unsigned int *)0xBF80F610 = cfgcon_val & ~(1 << 13);
}

static void pps_lock(void)
{
    syskey_unlock();
    unsigned int cfgcon_val = *(volatile unsigned int *)0xBF80F610;
    *(volatile unsigned int *)0xBF80F610 = cfgcon_val | (1 << 13);
}

// =====================================================================
// QEI1 INITIALISATION
// =====================================================================
// Steps:
//   1. Unlock PPS, write the pin values to QEA1R / QEB1R, re-lock.
//   2. Disable analog function on those pins (ANSELG), set as inputs (TRISG).
//   3. Clear the QEI control register, then:
//      - Enable digital filtering on A and B (QEI1IOC bits)
//      - Disable index (not needed for RPM)
//      - Enable velocity capture (QCAPEN) — tells QEI to snapshot
//        the timer on every position count change
//      - Set count comparison mode to 4x (CCM=0)
//      - Enable the QEI module

void qei1_init(void)
{
    // --- Map QEI inputs to physical pins via PPS ---
    pps_unlock();
    QEA1R = PPS_RPG6;   // QEI Phase A comes from RPG6
    QEB1R = PPS_RPG7;   // QEI Phase B comes from RPG7
    pps_lock();

    // --- Configure the GPIO pins ---
    ANSELGCLR = (1 << 6) | (1 << 7);   // Disable analog on RG6, RG7
    TRISGSET  = (1 << 6) | (1 << 7);   // Set RG6, RG7 as inputs

    // --- Configure the QEI module ---
    QEI1CON = 0;                         // Start from a known state
    QEI1IOC = 0;

    // QEI1IOC bits:
    //   QEA=1  → enable digital noise filter on Phase A
    //   QEB=1  → enable digital noise filter on Phase B
    //   INDEX=0→ ignore index pulse (not needed for RPM)
    //   QCAPEN=1→ enable velocity capture (timer snapshot on each edge)
    QEI1IOCbits.QEA    = 1;
    QEI1IOCbits.QEB    = 1;
    QEI1IOCbits.INDEX  = 0;
    QEI1IOCbits.QCAPEN = 1;

    // QEI1CON:
    //   CCM=0 → 4x quadrature count mode (counts all edges)
    //   QEIEN=1→ enable the module
    QEI1CONbits.CCM    = 0;
    QEI1CONbits.QEIEN  = 1;
}

// =====================================================================
// UART1 INITIALISATION
// =====================================================================
// The PKoB4 debugger provides a virtual COM port over USB.  On this
// board the UART1 signals are wired as (from schematic Figure 3-3):
//   TX → RPE0   (PPS output: write function 1 to RPE0R)
//   RX → RPG8   (PPS input:  write value 1 to U1RXR)
// We configure for 38400-8-N-1.

void uart1_init(void)
{
    // --- Map UART1 to physical pins via PPS ---
    pps_unlock();
    U1RXR = U1RX_PIN_VAL;   // U1RX input comes from RPG8 (value 1)
    RPE0R = U1TX_FN;        // RPE0 pin outputs U1TX (function 1)
    pps_lock();

    // --- Configure the GPIO pins ---
    ANSELECLR = (1 << 0);   // Disable analog on RE0
    TRISECLR  = (1 << 0);   // RE0 = output (U1TX drives it)
    ANSELGCLR = (1 << 8);   // Disable analog on RG8
    TRISGSET  = (1 << 8);   // RG8 = input (U1RX receives)

    // --- Configure the UART module ---
    U1MODE = 0;              // Reset to default (8-bit, no parity, 1 stop)

    // Baud rate = PB_CLOCK / (16 * (U1BRG + 1))
    // → U1BRG = (PB_CLOCK / (16 * UART_BAUD)) - 1
    // With PB_CLOCK = 8 MHz and UART_BAUD = 38400:
    //   U1BRG = (8000000 / (16 * 38400)) - 1 = 12.02 → 12
    U1BRG = (PB_CLOCK / (16 * UART_BAUD)) - 1;

    U1MODEbits.UARTEN = 1;   // Enable the UART module
    U1STAbits.UTXEN   = 1;   // Enable the transmitter
}

// =====================================================================
// UART OUTPUT FUNCTIONS
// =====================================================================

void uart_putchar(char c)
{
    while (U1STAbits.UTXBF);  // Wait until TX buffer has room
    U1TXREG = c;               // Write character to transmit register
}

void uart_puts(const char *s)
{
    while (*s)
        uart_putchar(*s++);
}

// Convert an integer to its decimal string and send it.
void uart_putint(int val)
{
    char buf[12];
    char *p = buf + sizeof(buf) - 1;
    int neg = 0;

    if (val < 0) { neg = 1; val = -val; }

    *p = '\0';
    if (val == 0) {
        *--p = '0';
    } else {
        while (val) {
            *--p = '0' + (val % 10);
            val /= 10;
        }
    }

    if (neg) *--p = '-';
    uart_puts(p);
}

// =====================================================================
// RPM CALCULATION
// =====================================================================
// We read POS1CNT (the QEI position counter) periodically.
// The difference from the last reading tells us how many counts
// occurred.  Dividing by counts-per-rev gives revolutions, and
// dividing by the elapsed time gives RPM.
//
// Formula:
//   delta    = POS1CNT - last_POS1CNT    (signed)
//   dt_ms    = elapsed core timer ticks / 4000
//   revs     = delta / (ENCODER_PPR * 4)
//   rpm      = revs * 60000 / dt_ms
//            = delta * 60000 / (ENCODER_PPR * 4 * dt_ms)
//
// Direction: sign of delta (+1 = CW, -1 = CCW)

rpm_result calc_rpm(unsigned int *last_pos, unsigned int *last_tick)
{
    rpm_result res = {0, 0};

    unsigned int pos  = POS1CNT;
    unsigned int tick = core_ticks();
    unsigned int dt_ticks = tick - *last_tick;

    // Wait at least SAMPLE_MS before computing a new value
    if (dt_ticks < (SAMPLE_MS * 4000u))
        return res;

    int delta    = (int)(pos - *last_pos);
    int dt_ms    = dt_ticks / 4000u;

    *last_pos  = pos;
    *last_tick = tick;

    // Determine direction from the sign of the position change
    res.dir = (delta >= 0) ? 1 : -1;
    if (delta < 0) delta = -delta;

    if (dt_ms > 0) {
        // 60000 = ms per minute
        // ENCODER_PPR * 4 = counts per revolution (16384)
        res.rpm = (int)((long long)delta * 60000
                     / (ENCODER_PPR * 4 * dt_ms));
    }

    return res;
}

// =====================================================================
// MAIN
// =====================================================================

int main(void)
{
    // --- Board-level setup ---
    // Turn off all analog on PORTA (just as a precaution)
    ANSELA = 0;

    // LED2 (RA10) is an output, start it low
    TRISACLR = (1 << 10);
    LATACLR  = (1 << 10);

    // --- Initialise peripherals ---
    qei1_init();     // Configure QEI module for the AS5047U
    uart1_init();    // Configure UART1 for VCOM output

    // --- Print startup message ---
    uart_puts("\r\nAS5047U QEI RPM Monitor\r\n");

    // --- State for RPM calculation ---
    unsigned int last_pos  = 0;
    unsigned int last_tick = core_ticks();

    // --- Main loop ---
    while (1)
    {
        rpm_result r = calc_rpm(&last_pos, &last_tick);

        if (r.dir != 0)   // A valid reading is ready
        {
            uart_puts("RPM: ");
            uart_putint(r.rpm);
            uart_puts("  Dir: ");
            uart_puts(r.dir > 0 ? "FWD" : "REV");
            uart_puts("\r\n");
        }

        // Toggle the user LED every loop iteration
        LATAbits.LATA10 = !LATAbits.LATA10;
    }

    return 0;  // never reached
}
