#include <xc.h>
#include <sys/attribs.h>

#pragma config FNOSC   = FRC
#pragma config POSCMOD = OFF
#pragma config FWDTEN  = OFF
#pragma config FDMTEN  = OFF
#pragma config ICESEL  = ICS_PGx2
#pragma config JTAGEN  = OFF

#define PB_CLOCK    4000000
#define STEP_RATE   1000
#define DIR_CW      0
#define DIR_CCW     1
#define ENA_ON      0
#define ENA_OFF     1
#define OC1_FN      5
#define ENCODER_PPR 4096

#define DIR_PIN     LATBbits.LATB14
#define ENA_PIN     LATBbits.LATB13
#define LED_PIN     LATAbits.LATA10

static inline uint32_t core_ticks(void) { return _CP0_GET_COUNT(); }

static void msleep(uint32_t ms)
{
    uint32_t start = core_ticks();
    while ((core_ticks() - start) < (ms * 4000u));
}

static void syskey_unlock(void)
{
    SYSKEY = 0xAA996655;
    SYSKEY = 0x556699AA;
}

// ── UART ─────────────────────────────────────────────────────────────
static void uart_init(void)
{
    syskey_unlock();
    CFGCONbits.IOLOCK = 0;
    RPE0R = 1;              // U1TX → RPE0
    U1RXR = 1;              // U1RX from PPS input #1
    syskey_unlock();
    CFGCONbits.IOLOCK = 1;

    ANSELECLR = (1 << 0);
    TRISECLR  = (1 << 0);
    U1MODE = 0;
    U1BRG = (PB_CLOCK / (16 * 19200)) - 1;
    U1MODEbits.UARTEN = 1;
    U1STAbits.UTXEN   = 1;
}

static void uart_putchar(char c)
{
    while (U1STAbits.UTXBF);
    U1TXREG = c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putchar(*s++);
}

static void uart_putint(int val)
{
    char buf[12], *p = buf + sizeof(buf) - 1;
    int neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    *p = '\0';
    if (val == 0) { *--p = '0'; }
    else { while (val) { *--p = '0' + (val % 10); val /= 10; } }
    if (neg) *--p = '-';
    uart_puts(p);
}

// ── Encoder (QEI1) ───────────────────────────────────────────────────
static int  encoder_dir;
static int  encoder_rpm;
static void qei_init(void)
{
    syskey_unlock();
    CFGCONbits.IOLOCK = 0;
    QEA1R = 10;             // FIXME: PPS input number for RPG6
    QEB1R = 10;             // FIXME: PPS input number for RPG7
    syskey_unlock();
    CFGCONbits.IOLOCK = 1;

    ANSELGCLR = (1 << 6) | (1 << 7);
    TRISGSET  = (1 << 6) | (1 << 7);

    QEI1CON = 0;
    QEI1IOC = 0;
    QEI1IOCbits.QEA    = 1;
    QEI1IOCbits.QEB    = 1;
    QEI1IOCbits.QCAPEN = 1;
    QEI1CONbits.CCM    = 0;
    QEI1CONbits.QEIEN  = 1;
}

static void encoder_poll(void)
{
    static uint32_t last_pos  = 0;
    static uint32_t last_tick = 0;
    static int      inited    = 0;

    if (!inited) {
        last_pos  = POS1CNT;
        last_tick = core_ticks();
        inited    = 1;
        return;
    }

    uint32_t pos  = POS1CNT;
    uint32_t tick = core_ticks();
    uint32_t dt   = tick - last_tick;

    if (dt < (1000 * 4000u)) return;

    int32_t delta = (int32_t)(pos - last_pos);
    int dt_ms = dt / 4000u;

    encoder_dir  = (delta > 0) - (delta < 0);
    encoder_rpm  = (dt_ms > 0) ? (int)((int64_t)(delta > 0 ? delta : -delta) * 60000 / (ENCODER_PPR * 4 * dt_ms)) : 0;

    last_pos  = pos;
    last_tick = tick;
}

// ── Motor (TB6600 via OC1) ───────────────────────────────────────────
static void motor_init(void)
{
    syskey_unlock();
    CFGCONbits.IOLOCK = 0;
    RPB15R = OC1_FN;
    syskey_unlock();
    CFGCONbits.IOLOCK = 1;

    ANSELBCLR = (1 << 13) | (1 << 14);
    TRISBCLR  = (1 << 13) | (1 << 14);

    DIR_PIN = DIR_CW;
    ENA_PIN = ENA_OFF;

    T2CON = 0;
    T2CONbits.TCKPS = 0;
    PR2 = (PB_CLOCK / STEP_RATE) - 1;
    TMR2 = 0;
    OC1CON = 0;
    OC1R  = PR2 >> 1;
    OC1RS = PR2 >> 1;
    OC1CONbits.OCM    = 0b110;
    OC1CONbits.OCTSEL = 0;
    T2CONbits.ON = 1;
    OC1CONbits.ON = 1;
}

static void motor_run(int dir)
{
    DIR_PIN = dir;
    ENA_PIN = ENA_ON;
}

static void motor_stop(void)
{
    OC1CONbits.ON = 0;
    ENA_PIN = ENA_OFF;
}

// ── Main ──────────────────────────────────────────────────────────────
int main(void)
{
    ANSELA = 0;
    TRISACLR = (1 << 10);
    LED_PIN = 0;

    uart_init();
    qei_init();
    motor_init();

    uart_puts("\r\n===== MOTOR TEST =====\r\n");
    uart_puts("Motor: STOPPED, Encoder: IDLE\r\n");
    msleep(2000);

    for (;;)
    {
        // ── CW 5s ──
        uart_puts("\r\n>> CW 5s\r\n");
        motor_run(DIR_CW);
        for (int i = 0; i < 5; i++) {
            msleep(1000);
            encoder_poll();
            uart_puts("CW  RPM:");
            uart_putint(encoder_rpm);
            uart_puts("  Encoder:");
            uart_puts(encoder_dir > 0 ? "FWD" : encoder_dir < 0 ? "REV" : "IDLE");
            uart_puts("\r\n");
            LED_PIN = !LED_PIN;
        }

        // ── CCW 5s ──
        uart_puts("\r\n>> CCW 5s\r\n");
        motor_run(DIR_CCW);
        for (int i = 0; i < 5; i++) {
            msleep(1000);
            encoder_poll();
            uart_puts("CCW RPM:");
            uart_putint(encoder_rpm);
            uart_puts("  Encoder:");
            uart_puts(encoder_dir > 0 ? "FWD" : encoder_dir < 0 ? "REV" : "IDLE");
            uart_puts("\r\n");
            LED_PIN = !LED_PIN;
        }
    }
}
