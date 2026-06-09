#include <xc.h>
#include <sys/attribs.h>

#pragma config FNOSC   = FRC
#pragma config POSCMOD = OFF
#pragma config FWDTEN  = OFF
#pragma config FDMTEN  = OFF
#pragma config ICESEL  = ICS_PGx2
#pragma config JTAGEN  = OFF

#define PB_CLOCK      4000000
#define DIR_CW        0
#define DIR_CCW       1
#define ENA_ON        0
#define ENA_OFF       1
#define OC1_FN        5
#define CONTROL_FREQ  1000
#define INTEGRAL_LIMIT 10000

#define DIR_PIN     LATBbits.LATB14
#define ENA_PIN     LATBbits.LATB13
#define LED_PIN     LATAbits.LATA10

static volatile int32_t target_pos   = 0;
static volatile int32_t actual_pos   = 0;
static volatile int32_t following_err = 0;
static volatile int32_t current_vel   = 0;
static volatile uint8_t homed         = 0;
static volatile uint8_t fault         = 0;
static volatile uint8_t moving        = 0;
static volatile uint8_t at_target     = 1;

static int32_t Kp        = 50;
static int32_t Ki        = 5;
static int32_t max_vel   = 2000;
static int32_t tolerance = 5;
static int32_t fault_thr  = 500;

static int32_t pos_offset = 0;
static int32_t integral   = 0;

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

// ── Encoder ──
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

static inline int32_t encoder_read(void) {
    return (int32_t)POS1CNT - pos_offset;
}

// ── Motor ──
static void motor_pins_init(void) {
    syskey_unlock(); CFGCONbits.IOLOCK = 0;
    RPB15R = OC1_FN;
    syskey_unlock(); CFGCONbits.IOLOCK = 1;
    ANSELBCLR = (1 << 13) | (1 << 14);
    TRISBCLR  = (1 << 13) | (1 << 14);
    DIR_PIN = DIR_CW; ENA_PIN = ENA_OFF;
}

static void motor_oc1_init(void) {
    T2CON = 0; T2CONbits.TCKPS = 3;
    PR2 = 4999; TMR2 = 0;
    OC1CON = 0; OC1R = 2500; OC1RS = 2500;
    OC1CONbits.OCM = 0b110; OC1CONbits.OCTSEL = 0;
}

static void motor_set_speed(uint32_t steps_per_sec) {
    if (steps_per_sec == 0) { OC1CONbits.ON = 0; T2CONbits.ON = 0; return; }
    uint32_t pr = 500000 / steps_per_sec;
    if (pr < 2) pr = 2;
    if (pr > 65535) pr = 65535;
    PR2 = pr - 1; OC1R = pr >> 1; OC1RS = pr >> 1;
    TMR2 = 0; T2CONbits.ON = 1; OC1CONbits.ON = 1;
}

static void motor_disable(void) {
    OC1CONbits.ON = 0; T2CONbits.ON = 0; ENA_PIN = ENA_OFF;
}

// ── Position Control Loop (1 kHz) ──
static void control_init(void) {
    T3CON = 0; T3CONbits.TCKPS = 0;
    PR3 = (PB_CLOCK / CONTROL_FREQ) - 1;
    TMR3 = 0;
    IPC3bits.T3IP = 4;
    IFS0CLR = (1 << 12);
    IEC0SET = (1 << 12);
    T3CONbits.ON = 1;
}

void __ISR(_TIMER_3_VECTOR, IPL4AUTO) control_isr(void) {
    IFS0CLR = (1 << 12);

    actual_pos = encoder_read();
    int32_t error = target_pos - actual_pos;
    following_err = error;

    if (error > fault_thr || error < -fault_thr) {
        fault = 1; motor_disable(); return;
    }

    if (error < tolerance && error > -tolerance) {
        if (moving) { moving = 0; at_target = 1; motor_set_speed(0); motor_disable(); }
        integral = 0; current_vel = 0; return;
    }

    integral += error;
    if (integral > INTEGRAL_LIMIT) integral = INTEGRAL_LIMIT;
    if (integral < -INTEGRAL_LIMIT) integral = -INTEGRAL_LIMIT;

    int32_t vel = (Kp * error + Ki * integral) / 100;
    if (vel > max_vel) vel = max_vel;
    if (vel < -max_vel) vel = -max_vel;
    current_vel = vel;

    DIR_PIN = (vel >= 0) ? DIR_CW : DIR_CCW;
    uint32_t speed = (vel >= 0) ? vel : -vel;
    if (speed < 10) speed = 10;
    motor_set_speed(speed);
    moving = 1; at_target = 0; ENA_PIN = ENA_ON;
}

// ── UART Commands ──
#define RX_BUF_SIZE 64
static char rx_buf[RX_BUF_SIZE];
static int  rx_idx = 0;

static void parse_command(const char *cmd) {
    if (cmd[0] == 'T' && cmd[1] == '=') {
        int32_t val = 0, sign = 1; const char *p = cmd + 2;
        if (*p == '-') { sign = -1; p++; }
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        target_pos = val * sign; fault = 0;
        uart_puts("OK T="); uart_putint(target_pos); uart_puts("\r\n");
    }
    else if (cmd[0] == 'H' && cmd[1] == 'O' && cmd[2] == 'M' && cmd[3] == 'E') {
        pos_offset = POS1CNT; target_pos = 0; integral = 0; homed = 1; fault = 0;
        uart_puts("OK HOME\r\n");
    }
    else if (cmd[0] == 'S' && cmd[1] == 'T' && cmd[2] == 'O' && cmd[3] == 'P') {
        motor_disable(); target_pos = encoder_read(); integral = 0; moving = 0; at_target = 1;
        uart_puts("OK STOP\r\n");
    }
    else if (cmd[0] == 'C' && cmd[1] == 'L' && cmd[2] == 'E' && cmd[3] == 'A' && cmd[4] == 'R') {
        fault = 0; integral = 0;
        motor_pins_init(); motor_oc1_init();
        uart_puts("OK CLEAR\r\n");
    }
    else if (cmd[0] == 'K' && cmd[1] == 'P' && cmd[2] == '=') {
        int32_t val = 0; const char *p = cmd + 3;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        Kp = val; uart_puts("OK KP="); uart_putint(Kp); uart_puts("\r\n");
    }
    else if (cmd[0] == 'K' && cmd[1] == 'I' && cmd[2] == '=') {
        int32_t val = 0; const char *p = cmd + 3;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        Ki = val; uart_puts("OK KI="); uart_putint(Ki); uart_puts("\r\n");
    }
    else if (cmd[0] == 'M' && cmd[1] == 'A' && cmd[2] == 'X' && cmd[3] == 'V' && cmd[4] == '=') {
        int32_t val = 0; const char *p = cmd + 5;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (val > 0 && val <= 10000) max_vel = val;
        uart_puts("OK MAXV="); uart_putint(max_vel); uart_puts("\r\n");
    }
    else if (cmd[0] == 'T' && cmd[1] == 'O' && cmd[2] == 'L' && cmd[3] == '=') {
        int32_t val = 0; const char *p = cmd + 4;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (val >= 0 && val <= 1000) tolerance = val;
        uart_puts("OK TOL="); uart_putint(tolerance); uart_puts("\r\n");
    }
    else if (cmd[0] == 'F' && cmd[1] == 'T' && cmd[2] == '=') {
        int32_t val = 0; const char *p = cmd + 3;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (val > 0 && val <= 100000) fault_thr = val;
        uart_puts("OK FT="); uart_putint(fault_thr); uart_puts("\r\n");
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
    uart_puts("P:"); uart_putint(actual_pos);
    uart_puts(",E:"); uart_putint(following_err);
    uart_puts(",V:"); uart_putint(current_vel);
    uart_puts(",T:"); uart_putint(target_pos);
    uart_puts(",F:"); uart_putint(fault ? 1 : 0);
    uart_puts(",H:"); uart_putint(homed ? 1 : 0);
    uart_puts(",M:"); uart_putint(moving ? 1 : 0);
    uart_puts("\r\n");
}

// ── Main ──
int main(void) {
    ANSELA = 0; TRISACLR = (1 << 10); LED_PIN = 0;
    uart_init(); qei_init();
    motor_pins_init(); motor_oc1_init(); control_init();

    uart_puts("\r\n===== CLOSED-LOOP STEPPER v2.0 =====\r\n");
    uart_puts("T=<pos> HOME STOP CLEAR KP=<v> KI=<v> MAXV=<v> TOL=<v> FT=<v>\r\n");

    uint32_t last_status = core_ticks();
    for (;;) {
        uart_poll();
        if ((core_ticks() - last_status) >= (PB_CLOCK / 10)) {
            send_status(); last_status = core_ticks(); LED_PIN = !LED_PIN;
        }
    }
}
