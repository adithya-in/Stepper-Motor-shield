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
 */

#include <xc.h>

#pragma config FNOSC   = FRC
#pragma config POSCMOD = OFF
#pragma config FWDTEN  = OFF
#pragma config FDMTEN  = OFF
#pragma config ICESEL  = ICS_PGx2
#pragma config JTAGEN  = OFF

#define ENCODER_PPR     4096
#define SAMPLE_MS       500
// PB1 clock defaults to SYSCLK/2 (divide-by-2), so PB1CLK = 4 MHz
// At 4 MHz, 19200 baud gives 0.16% error (U1BRG = 12)
#define PB_CLOCK        4000000
#define UART_BAUD       19200
#define U1TX_FN         1
#define PPS_RPG6        10
#define PPS_RPG7        10
#define U1RX_PIN_VAL    1

typedef struct {
    int rpm;
    int dir;
} rpm_result;

static inline unsigned int core_ticks(void)
{
    return _CP0_GET_COUNT();
}

static void syskey_unlock(void)
{
    SYSKEY = 0xAA996655;
    SYSKEY = 0x556699AA;
}

static void pps_unlock(void)
{
    syskey_unlock();
    CFGCON &= ~((1 << 11) | (1 << 12) | (1 << 13));
}

static void pps_lock(void)
{
    syskey_unlock();
    CFGCON |= (1 << 13);
}

void qei1_init(void)
{
    pps_unlock();
    QEA1R = PPS_RPG6;
    QEB1R = PPS_RPG7;
    pps_lock();

    ANSELGCLR = (1 << 6) | (1 << 7);
    TRISGSET  = (1 << 6) | (1 << 7);

    QEI1CON = 0;
    QEI1IOC = 0;
    QEI1IOCbits.QEA    = 1;
    QEI1IOCbits.QEB    = 1;
    QEI1IOCbits.INDEX  = 0;
    QEI1IOCbits.QCAPEN = 1;
    QEI1CONbits.CCM    = 0;
    QEI1CONbits.QEIEN  = 1;
}

void uart1_init(void)
{
    pps_unlock();
    U1RXR = U1RX_PIN_VAL;
    RPE0R = U1TX_FN;
    pps_lock();

    ANSELECLR = (1 << 0);
    TRISECLR  = (1 << 0);
    ANSELGCLR = (1 << 8);
    TRISGSET  = (1 << 8);

    U1MODE = 0;
    U1BRG = (PB_CLOCK / (16 * UART_BAUD)) - 1;
    U1MODEbits.UARTEN = 1;
    U1STAbits.UTXEN   = 1;
}

void uart_putchar(char c)
{
    while (U1STAbits.UTXBF);
    U1TXREG = c;
}

void uart_puts(const char *s)
{
    while (*s)
        uart_putchar(*s++);
}

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

rpm_result calc_rpm(unsigned int *last_pos, unsigned int *last_tick)
{
    rpm_result res = {0, 0};

    unsigned int pos  = POS1CNT;
    unsigned int tick = core_ticks();
    unsigned int dt_ticks = tick - *last_tick;

    if (dt_ticks < (SAMPLE_MS * 4000u))
        return res;

    int delta    = (int)(pos - *last_pos);
    int dt_ms    = dt_ticks / 4000u;

    *last_pos  = pos;
    *last_tick = tick;

    res.dir = (delta >= 0) ? 1 : -1;
    if (delta < 0) delta = -delta;

    if (dt_ms > 0) {
        res.rpm = (int)((long long)delta * 60000
                     / (ENCODER_PPR * 4 * dt_ms));
    }

    return res;
}

int main(void)
{
    ANSELA = 0;
    TRISACLR = (1 << 10);
    LATACLR  = (1 << 10);

    qei1_init();
    uart1_init();

    uart_puts("\r\nAS5047U QEI RPM Monitor\r\n");

    unsigned int last_pos  = 0;
    unsigned int last_tick = core_ticks();

    while (1)
    {
        rpm_result r = calc_rpm(&last_pos, &last_tick);

        if (r.dir != 0)
        {
            uart_puts("RPM: ");
            uart_putint(r.rpm);
            uart_puts("  Dir: ");
            uart_puts(r.dir > 0 ? "FWD" : "REV");
            uart_puts("\r\n");
        }

        LATAbits.LATA10 = !LATAbits.LATA10;
    }

    return 0;
}
