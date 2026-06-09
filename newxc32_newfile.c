#include <xc.h>
#include <sys/attribs.h>

#pragma config FNOSC   = FRC
#pragma config POSCMOD = OFF
#pragma config FWDTEN  = OFF
#pragma config FDMTEN  = OFF
#pragma config ICESEL  = ICS_PGx2
#pragma config JTAGEN  = OFF

#define PB_CLOCK    4000000
#define DIR_CW      0
#define DIR_CCW     1
#define ENA_ON      0
#define ENA_OFF     1
#define OC1_FN      5
#define ENCODER_PPR 4096

#define DIR_PIN     LATBbits.LATB14
#define ENA_PIN     LATBbits.LATB13
#define LED_PIN     LATAbits.LATA10

static int motor_on = 0;
static int speed = 0;
static int dir = DIR_CW;

static inline uint32_t core_ticks(void) { return _CP0_GET_COUNT(); }

static void msleep(uint32_t ms) {
    uint32_t start = core_ticks();
    while ((core_ticks() - start) < (ms * 4000u));
}

static void syskey_unlock(void) {
    SYSKEY = 0xAA996655;
    SYSKEY = 0x556699AA;
}

// ── UART ──
static void uart_init(void) {
    syskey_unlock(); CFGCONbits.IOLOCK = 0;
    RPE0R = 1; U1RXR = 1;
    syskey_unlock(); CFGCONbits.IOLOCK = 1;
    ANSELECLR = (1 << 0); TRISECLR = (1 << 0);
    U1MODE = 0;
    U1BRG = (PB_CLOCK / (16 * 19200)) - 1;
    U1MODEbits.UARTEN = 1; U1STAbits.UTXEN = 1;
}

static void uart_putchar(char c) { while (U1STAbits.UTXBF); U1TXREG = c; }
static void uart_puts(const char *s) { while (*s) uart_putchar(*s++); }

static void uart_putint(int32_t val) {
    char buf[12], *p = buf + sizeof(buf) - 1;
    int neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    *p = '\0';
    if (val == 0) { *--p = '0'; }
    else { while (val) { *--p = '0' + (val % 10); val /= 10; } }
    if (neg) *--p = '-';
    uart_puts(p);
}

static char uart_getchar(void) {
    return U1STAbits.URXDA ? U1RXREG : (char)-1;
}

// ── Encoder (QEI1) ──
static int encoder_rpm;
static int encoder_dir;

static void qei_init(void) {
    syskey_unlock(); CFGCONbits.IOLOCK = 0;
    QEA1R = 10; QEB1R = 10;
    syskey_unlock(); CFGCONbits.IOLOCK = 1;
    ANSELGCLR = (1 << 6) | (1 << 7);
    TRISGSET  = (1 << 6) | (1 << 7);
    QEI1CON = 0; QEI1IOC = 0;
    QEI1IOCbits.QEA = 1; QEI1IOCbits.QEB = 1;
    QEI1IOCbits.QCAPEN = 1;
    QEI1CONbits.CCM = 0;
    QEI1CONbits.QEIEN = 1;
}

static void encoder_poll(void) {
    static uint32_t last_pos = 0, last_tick = 0, inited = 0;
    if (!inited) { last_pos = POS1CNT; last_tick = core_ticks(); inited = 1; return; }
    uint32_t pos = POS1CNT, tick = core_ticks();
    uint32_t dt = tick - last_tick;
    if (dt < (1000 * 4000u)) return;
    int32_t delta = (int32_t)(pos - last_pos);
    int dt_ms = dt / 4000u;
    encoder_dir = (delta > 0) - (delta < 0);
    encoder_rpm = (dt_ms > 0) ? (int)((int64_t)(delta > 0 ? delta : -delta) * 60000 / (ENCODER_PPR * 4 * dt_ms)) : 0;
    last_pos = pos; last_tick = tick;
}

// ── Motor ──
static void motor_init(void) {
    syskey_unlock(); CFGCONbits.IOLOCK = 0;
    RPB15R = OC1_FN;
    syskey_unlock(); CFGCONbits.IOLOCK = 1;
    ANSELBCLR = (1 << 13) | (1 << 14);
    TRISBCLR  = (1 << 13) | (1 << 14);
    DIR_PIN = DIR_CW; ENA_PIN = ENA_OFF;

    T2CON = 0; T2CONbits.TCKPS = 3;
    PR2 = 49999; TMR2 = 0;
    OC1CON = 0; OC1R = 25000; OC1RS = 25000;
    OC1CONbits.OCM = 0b110; OC1CONbits.OCTSEL = 0;
}

static void motor_set_speed(int steps_per_sec) {
    if (steps_per_sec <= 0) {
        OC1CONbits.ON = 0; T2CONbits.ON = 0; ENA_PIN = ENA_OFF; return;
    }
    uint32_t pr = 500000 / steps_per_sec;
    if (pr < 2) pr = 2;
    if (pr > 65535) pr = 65535;
    PR2 = pr - 1; OC1R = pr >> 1; OC1RS = pr >> 1;
    TMR2 = 0;
    ENA_PIN = ENA_ON; DIR_PIN = dir;
    T2CONbits.ON = 1; OC1CONbits.ON = 1;
}

// ── UART Commands ──
#define RX_BUF_SIZE 64
static char rx_buf[RX_BUF_SIZE];
static int rx_idx = 0;

static void parse_command(const char *cmd) {
    if (cmd[0] == 'O' && cmd[1] == 'N') {
        motor_on = 1; motor_set_speed(speed);
        uart_puts("OK ON\r\n");
    }
    else if (cmd[0] == 'O' && cmd[1] == 'F' && cmd[2] == 'F') {
        motor_on = 0; motor_set_speed(0);
        uart_puts("OK OFF\r\n");
    }
    else if (cmd[0] == 'C' && cmd[1] == 'W') {
        dir = DIR_CW;
        if (motor_on) { motor_set_speed(0); motor_set_speed(speed); }
        uart_puts("OK CW\r\n");
    }
    else if (cmd[0] == 'C' && cmd[1] == 'C' && cmd[2] == 'W') {
        dir = DIR_CCW;
        if (motor_on) { motor_set_speed(0); motor_set_speed(speed); }
        uart_puts("OK CCW\r\n");
    }
    else if (cmd[0] == 'S' && cmd[1] == 'P' && cmd[2] == 'E' && cmd[3] == 'E' && cmd[4] == 'D') {
        int32_t val = 0; const char *p = cmd + 5;
        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        speed = (val > 10000) ? 10000 : (val < 0) ? 0 : val;
        if (motor_on) motor_set_speed(speed);
        uart_puts("OK SPEED "); uart_putint(speed); uart_puts("\r\n");
    }
    else {
        uart_puts("ERR UNKNOWN\r\n");
    }
}

static void uart_poll(void) {
    char c = uart_getchar();
    while (c != (char)-1) {
        if (c == '\r' || c == '\n') {
            if (rx_idx > 0) { rx_buf[rx_idx] = '\0'; parse_command(rx_buf); rx_idx = 0; }
        } else if (rx_idx < RX_BUF_SIZE - 1) { rx_buf[rx_idx++] = c; }
        c = uart_getchar();
    }
}

static void send_status(void) {
    uart_puts("RPM:"); uart_putint(encoder_rpm);
    uart_puts(",DIR:"); uart_puts(dir == DIR_CW ? "CW" : "CCW");
    uart_puts(",ON:"); uart_puts(motor_on ? "1" : "0");
    uart_puts("\r\n");
}

// ── Main ──
int main(void) {
    ANSELA = 0; TRISACLR = (1 << 10); LED_PIN = 0;
    uart_init(); qei_init(); motor_init();

    // Auto-start motor at 2000 steps/s CW
    speed = 2000;
    dir = DIR_CW;
    motor_on = 1;
    motor_set_speed(speed);

    uart_puts("\r\n===== SPEED CONTROL =====\r\n");
    uart_puts("ON OFF CW CCW SPEED <steps/s>\r\n");

    uint32_t last_status = core_ticks();
    for (;;) {
        uart_poll();
        encoder_poll();
        if ((core_ticks() - last_status) >= (PB_CLOCK / 10)) {
            send_status(); last_status = core_ticks(); LED_PIN = !LED_PIN;
        }
    }
}
