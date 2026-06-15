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
#define ENCODER_PPR   4096
#define CONTROL_FREQ  1000
#define INTEGRAL_LIMIT 10000

#define DIR_PIN     LATBbits.LATB14
#define ENA_PIN     LATBbits.LATB13
#define LED_PIN     LATAbits.LATA10

static volatile int32_t target_pos   = 0;
static volatile int32_t follow_err   = 0;
static volatile int32_t current_vel  = 0;
static volatile uint8_t fault        = 0;
static volatile uint8_t moving       = 0;
static volatile uint8_t at_target    = 1;
static volatile uint8_t open_loop    = 0;
static volatile uint16_t fault_cnt  = 0;


static int32_t Kp          = 50;
static int32_t Ki          = 5;
static int32_t max_vel     = 5000;
static int32_t tolerance   = 5;
static int32_t fault_thr   = 500;
static int32_t accel_limit = 50000;
static int32_t jerk_limit  = 500000;
static uint8_t profile = 0;         // 0 = S-curve, 1 = Trapezoidal
static int32_t Kd = 0;
static int32_t prev_error = 0;
static int32_t coil_current = 800;  // mA
static uint16_t microstep = 16;
static uint32_t tlm_period = 100;   // ms
static uint8_t tlm_enabled = 1;
static uint32_t tlm_ticks = (PB_CLOCK * 100) / 1000;

// ── Auto-tune ──
#define TUNE_IDLE      0
#define TUNE_MOVE      1
#define TUNE_RELAY     2
#define TUNE_COMPLETE  3

static uint8_t tune_state = TUNE_IDLE;
static int32_t tune_setpoint = 0;
static int32_t tune_offset = 3000;
static int32_t tune_relay_vel = 3000;
static int32_t tune_hyst = 10;
static uint8_t tune_min_cycles = 4;
static uint8_t tune_dir = 0;
static int32_t tune_peak_max = 0;
static int32_t tune_peak_min = 0;
static int32_t tune_peak_max_sum = 0;
static int32_t tune_peak_min_sum = 0;
static uint8_t tune_full_cycles = 0;
static uint32_t tune_cycle_start = 0;
static uint32_t tune_period_sum = 0;
static uint8_t tune_output = 0;
static int32_t tune_amplitude = 0;
static uint32_t tune_period_ms = 0;

// ── Multi-point queue ──
#define QUEUE_MAX 32
static int32_t queue[QUEUE_MAX];
static uint8_t queue_len = 0;
static uint8_t queue_idx = 0;
static uint8_t queue_active = 0;
static uint32_t dwell_ms = 0;
static uint32_t dwell_remaining = 0;

static inline uint32_t core_ticks(void);

// ── NVM Config (non-volatile flash storage) ──
#define CONFIG_ADDR   0x1D07F000
#define CONFIG_MAGIC  0xBEADC0DE
#define CONFIG_DATA_WORDS 12

typedef struct {
    int32_t  Kp, Ki, Kd;
    int32_t  max_vel, tolerance, fault_thr;
    int32_t  accel_limit, jerk_limit;
    int32_t  coil_current;
    uint32_t profile, microstep;
} ConfigData;

static uint8_t config_dirty = 0;
static uint32_t config_save_time = 0;

static uint32_t config_checksum(const uint32_t *d, int n) {
    uint32_t c = 0; for (int i = 0; i < n; i++) c ^= d[i]; return c;
}

static void nvm_unlock(void) {
    __builtin_disable_interrupts();
    NVMKEY = 0xAA996655;
    NVMKEY = 0x556699AA;
    NVMCONSET = _NVMCON_WR_MASK;
    while (NVMCONbits.WR);
    __builtin_enable_interrupts();
}

static void nvm_erase_page(uint32_t addr) {
    NVMCON = 0; NVMCONbits.NVMOP = 4; NVMCONbits.WREN = 1;
    NVMADDR = addr; nvm_unlock(); NVMCONbits.WREN = 0;
}

static void nvm_write_word(uint32_t addr, uint32_t data) {
    NVMCON = 0; NVMCONbits.NVMOP = 1; NVMCONbits.WREN = 1;
    NVMADDR = addr; NVMDATA0 = data; nvm_unlock(); NVMCONbits.WREN = 0;
}

static void config_save(void) {
    ConfigData d;
    d.Kp = Kp; d.Ki = Ki; d.Kd = Kd;
    d.max_vel = max_vel; d.tolerance = tolerance; d.fault_thr = fault_thr;
    d.accel_limit = accel_limit; d.jerk_limit = jerk_limit;
    d.coil_current = coil_current; d.profile = profile; d.microstep = microstep;

    nvm_erase_page(CONFIG_ADDR & 0xFFFFF000);
    uint32_t *w = (uint32_t*)&d;
    nvm_write_word(CONFIG_ADDR, CONFIG_MAGIC);
    for (int i = 0; i < CONFIG_DATA_WORDS; i++)
        nvm_write_word(CONFIG_ADDR + 4 + i * 4, w[i]);
    nvm_write_word(CONFIG_ADDR + 4 + CONFIG_DATA_WORDS * 4, config_checksum(w, CONFIG_DATA_WORDS));
}

static void config_load(void) {
    const volatile uint32_t *f = (const volatile uint32_t *)0xBD07F000;
    if (f[0] != CONFIG_MAGIC) return;
    const volatile uint32_t *d = f + 1;
    uint32_t cksum = config_checksum((const uint32_t *)d, CONFIG_DATA_WORDS);
    if (cksum != f[1 + CONFIG_DATA_WORDS]) return;
    Kp = d[0]; Ki = d[1]; Kd = d[2];
    max_vel = d[3]; tolerance = d[4]; fault_thr = d[5];
    accel_limit = d[6]; jerk_limit = d[7];
    coil_current = d[8]; profile = d[9]; microstep = d[10];
}

static void config_mark_dirty(void) {
    config_dirty = 1; config_save_time = core_ticks();
}

static int32_t encoder_offset = 0;
static int32_t integral = 0;
static int32_t sm_vel = 0;  // jerk-filtered velocity (steps/s) 
static int32_t sm_acc = 0;  // jerk-filtered acceleration (steps/s²)
static int32_t vfrac = 0;  // fractional velocity accumulator

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
    RPE0R = 1; U1RXR = 10;
    syskey_unlock(); CFGCONbits.IOLOCK = 1;
    ANSELECLR = (1 << 0); TRISECLR = (1 << 0);
    ANSELGCLR = (1 << 8); TRISGSET = (1 << 8);
    U1MODE = 0;
    U1BRG = (PB_CLOCK / (16 * 19200)) - 1;
    U1MODEbits.UARTEN = 1; U1STAbits.UTXEN = 1; U1STAbits.URXEN = 1;
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

static void uart_clear_oerr(void) {
    if (U1STAbits.OERR) {
        U1STACLR = _U1STA_OERR_MASK;
        while (U1STAbits.URXDA) (void)U1RXREG;
    }
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

static int32_t encoder_read(void) {
    return (int32_t)POS1CNT + encoder_offset;
}

// ── Motor ──
static void motor_init(void) {
    syskey_unlock(); CFGCONbits.IOLOCK = 0;
    RPB15R = OC1_FN;
    syskey_unlock(); CFGCONbits.IOLOCK = 1;
    ANSELBCLR = (1 << 13) | (1 << 14);
    TRISBCLR  = (1 << 13) | (1 << 14);
    DIR_PIN = DIR_CW; ENA_PIN = ENA_OFF;

    T2CON = 0; T2CONbits.TCKPS = 3;  // 1:8 (3-bit TCKPS: 011 = 1:8)
    PR2 = 49999; TMR2 = 0;
    OC1CON = 0; OC1R = 25000; OC1RS = 25000;
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

// ── Position Control Loop (1 kHz via Timer3) ──
static void control_init(void) {
    T3CON = 0; T3CONbits.TCKPS = 0;
    PR3 = (PB_CLOCK / CONTROL_FREQ) - 1;
    TMR3 = 0;
    IPC3bits.T3IP = 4;
    IFS0CLR = (1 << 14);
    IEC0SET = (1 << 14);
    T3CONbits.ON = 1;
}

void __ISR(_TIMER_3_VECTOR, IPL4AUTO) control_isr(void) {
    IFS0CLR = (1 << 14);

    if (open_loop) return;

    int32_t actual = encoder_read();

    // ── Auto-tune relay override ──
    if (tune_state == TUNE_RELAY) {
        int32_t err = actual - tune_setpoint;
        uint8_t nd;
        if (err > tune_hyst) nd = 0;
        else if (err < -tune_hyst) nd = 1;
        else nd = tune_dir;

        // Peak/period on direction change
        if (nd != tune_dir) {
            uint32_t now = core_ticks();
            uint32_t half = now - tune_cycle_start;
            if (nd == 1) {
                tune_peak_min_sum += tune_peak_min;
                tune_peak_min = 0;
            } else {
                tune_peak_max_sum += tune_peak_max;
                tune_peak_max = 0;
                tune_full_cycles++;
            }
            if (tune_full_cycles > 0)
                tune_period_sum += half;
            tune_cycle_start = now;

            if (tune_full_cycles >= tune_min_cycles) {
                int32_t amp = (tune_peak_max_sum + tune_peak_min_sum) / (2 * tune_full_cycles);
                if (amp < 1) amp = 1;
                tune_amplitude = amp;
                float Tu = (float)(tune_period_sum / tune_full_cycles) * 2.0f / 4000.0f;
                tune_period_ms = (uint32_t)(Tu * 1000.0f);
                float Ku = 4.0f * tune_relay_vel / (3.14159265f * amp);
                Kp = (int32_t)(0.6f * Ku * 100.0f);
                Ki = (int32_t)(1.2f * Ku * 100.0f / (Tu * CONTROL_FREQ));
                Kd = (int32_t)(0.075f * Ku * Tu * CONTROL_FREQ * 100.0f);
                if (Kp < 0) Kp = 0; if (Kp > 10000) Kp = 10000;
                if (Ki < 0) Ki = 0; if (Ki > 1000) Ki = 1000;
                if (Kd < 0) Kd = 0; if (Kd > 10000) Kd = 10000;
                tune_state = TUNE_COMPLETE;
                tune_output = 0;
            }
        }

        if (nd == 1 && err > tune_peak_max) tune_peak_max = err;
        if (nd == 0 && (-err) > tune_peak_min) tune_peak_min = -err;

        current_vel = nd ? tune_relay_vel : -tune_relay_vel;
        DIR_PIN = (current_vel >= 0) ? DIR_CW : DIR_CCW;
        motor_set_speed(tune_relay_vel);
        moving = 1; at_target = 0; ENA_PIN = ENA_ON;
        tune_dir = nd;
        return;
    }

    if (tune_state == TUNE_COMPLETE) {
        if (!tune_output) {
            tune_output = 1;
            uart_puts("OK TUNE:Kp="); uart_putint(Kp);
            uart_puts(",Ki="); uart_putint(Ki);
            uart_puts(",Kd="); uart_putint(Kd);
            uart_puts(",amp="); uart_putint(tune_amplitude);
            uart_puts(",Tu="); uart_putint(tune_period_ms);
            uart_puts("\r\n");
            config_mark_dirty();
            target_pos = tune_setpoint;
        }
    }

    // ── S-curve position mode ──
    int32_t error = target_pos - actual;
    follow_err = error;

    if (error < tolerance && error > -tolerance) {
        if (moving) { moving = 0; at_target = 1; motor_set_speed(0); }
        // ── TUNE_MOVE completion (motor just arrived at offset target) ──
        if (tune_state == TUNE_MOVE) {
            tune_state = TUNE_RELAY;
            tune_cycle_start = core_ticks();
            tune_dir = 1;
            tune_peak_max = 0; tune_peak_min = 0;
            tune_peak_max_sum = 0; tune_peak_min_sum = 0;
            tune_full_cycles = 0; tune_period_sum = 0;
            target_pos = tune_setpoint;
        }
        current_vel = 0; sm_vel = 0; sm_acc = 0; vfrac = 0; fault_cnt = 0; integral = 0; prev_error = 0;
        if (queue_active) {
            if (queue_idx < queue_len - 1) {
                if (dwell_remaining == 0) {
                    queue_idx++;
                    target_pos = queue[queue_idx];
                    dwell_remaining = dwell_ms;
                } else {
                    dwell_remaining--;
                }
            } else {
                queue_active = 0;
            }
        }
        return;
    }

    // ── Derivative kick fix: on first frame of a new move, zero derivative ──
    if (!moving) {
        prev_error = error;
    }

    // Fault only when at target (something pushed motor out of position)
    if (at_target && (error > fault_thr || error < -fault_thr)) {
        fault_cnt++;
        if (fault_cnt > 50) { fault = 1; motor_disable(); return; }
    } else {
        fault_cnt = 0;
    }

    // ── Profile velocity (feed-forward) ──
    uint32_t abs_err = (error > 0) ? error : -error;
    int32_t profile_vel = 0;

    if (abs_err > (uint32_t)tolerance) {
        uint32_t stop_d = (uint32_t)((int64_t)max_vel * max_vel / (2 * accel_limit + 1));
        if (stop_d < 10) stop_d = 10;
        if (abs_err > stop_d) {
            profile_vel = (error > 0) ? max_vel : -max_vel;
        } else {
            int32_t decel_vel = (int32_t)((int64_t)max_vel * abs_err / stop_d);
            if (decel_vel < 10) decel_vel = 10;
            profile_vel = (error > 0) ? decel_vel : -decel_vel;
        }
    }

    // ── PID trim (anti-windup during moves) ──
    if (abs_err > (uint32_t)tolerance) integral = 0;
    else integral += error;
    if (integral > INTEGRAL_LIMIT) integral = INTEGRAL_LIMIT;
    if (integral < -INTEGRAL_LIMIT) integral = -INTEGRAL_LIMIT;

    int32_t derivative = error - prev_error;
    prev_error = error;
    int32_t pid_trim = (Kp * error + Ki * integral + Kd * derivative) / 100;

    // ── Combined target velocity ──
    int32_t raw_vel = profile_vel + pid_trim;
    if (raw_vel > max_vel) raw_vel = max_vel;
    if (raw_vel < -max_vel) raw_vel = -max_vel;

    // ── Motion profile smoother ──
    int32_t verr = raw_vel - sm_vel;
    int32_t tgt_acc = (verr > 0) ? accel_limit : -accel_limit;
    int32_t accel_needed = abs(verr) * CONTROL_FREQ;
    if (accel_needed < accel_limit) tgt_acc = (verr > 0) ? accel_needed : -accel_needed;

    if (profile == 1) {
        // Trapezoidal: instant acceleration change (linear ramp)
        sm_acc = tgt_acc;
    } else {
        // S-curve: jerk-limited acceleration change
        int32_t astep = tgt_acc - sm_acc;
        int32_t max_jstep = jerk_limit / CONTROL_FREQ;
        if (max_jstep < 1) max_jstep = 1;
        if (astep > max_jstep) astep = max_jstep;
        if (astep < -max_jstep) astep = -max_jstep;
        sm_acc += astep;
        if (sm_acc > accel_limit) sm_acc = accel_limit;
        if (sm_acc < -accel_limit) sm_acc = -accel_limit;
    }

    // Step 2: integrate acceleration → velocity (with fractional remainder)
    vfrac += sm_acc;
    int32_t vd = vfrac / CONTROL_FREQ;
    sm_vel += vd;
    vfrac -= vd * CONTROL_FREQ;
    if (sm_vel > max_vel) sm_vel = max_vel;
    if (sm_vel < -max_vel) sm_vel = -max_vel;

    current_vel = sm_vel;

    // ── Apply smoothed velocity to motor ──
    DIR_PIN = (sm_vel >= 0) ? DIR_CW : DIR_CCW;
    uint32_t speed = (sm_vel >= 0) ? sm_vel : -sm_vel;
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
        target_pos = val * sign; fault = 0; fault_cnt = 0; integral = 0; sm_vel = 0; sm_acc = 0; vfrac = 0;
        queue_active = 0; queue_len = 0; queue_idx = 0;
        uart_puts("OK T="); uart_putint(target_pos); uart_puts("\r\n");
    }
    else if (cmd[0] == 'H' && cmd[1] == 'O' && cmd[2] == 'M' && cmd[3] == 'E') {
        encoder_offset = -(int32_t)POS1CNT;
        target_pos = 0; fault = 0; integral = 0; fault_cnt = 0;
        uart_puts("OK HOME\r\n");
    }
    else if (cmd[0] == 'S' && cmd[1] == 'T' && cmd[2] == 'O' && cmd[3] == 'P') {
        open_loop = 0; motor_disable(); target_pos = encoder_read(); integral = 0; moving = 0; at_target = 1; fault_cnt = 0; sm_vel = 0; sm_acc = 0; vfrac = 0;
        queue_active = 0; queue_len = 0; queue_idx = 0;
        uart_puts("OK STOP\r\n");
    }
    else if (cmd[0] == 'C' && cmd[1] == 'L' && cmd[2] == 'E' && cmd[3] == 'A' && cmd[4] == 'R') {
        fault = 0; integral = 0; fault_cnt = 0;
        motor_init();
        uart_puts("OK CLEAR\r\n");
    }
    else if (cmd[0] == 'K' && cmd[1] == 'P' && cmd[2] == '=') {
        int32_t val = 0; const char *p = cmd + 3;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        Kp = val; config_mark_dirty(); uart_puts("OK KP="); uart_putint(Kp); uart_puts("\r\n");
    }
    else if (cmd[0] == 'K' && cmd[1] == 'I' && cmd[2] == '=') {
        int32_t val = 0; const char *p = cmd + 3;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        Ki = val; config_mark_dirty(); uart_puts("OK KI="); uart_putint(Ki); uart_puts("\r\n");
    }
    else if (cmd[0] == 'K' && cmd[1] == 'D' && cmd[2] == '=') {
        int32_t val = 0; const char *p = cmd + 3;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        Kd = val; prev_error = 0; config_mark_dirty(); uart_puts("OK KD="); uart_putint(Kd); uart_puts("\r\n");
    }
    else if (cmd[0] == 'M' && cmd[1] == 'A' && cmd[2] == 'X' && cmd[3] == 'V' && cmd[4] == '=') {
        int32_t val = 0; const char *p = cmd + 5;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (val > 0 && val <= 100000) max_vel = val;
        config_mark_dirty(); uart_puts("OK MAXV="); uart_putint(max_vel); uart_puts("\r\n");
    }
    else if (cmd[0] == 'T' && cmd[1] == 'O' && cmd[2] == 'L' && cmd[3] == '=') {
        int32_t val = 0; const char *p = cmd + 4;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (val >= 0 && val <= 1000) tolerance = val;
        config_mark_dirty(); uart_puts("OK TOL="); uart_putint(tolerance); uart_puts("\r\n");
    }
    else if (cmd[0] == 'F' && cmd[1] == 'T' && cmd[2] == '=') {
        int32_t val = 0; const char *p = cmd + 3;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (val > 0 && val <= 100000) fault_thr = val;
        config_mark_dirty(); uart_puts("OK FT="); uart_putint(fault_thr); uart_puts("\r\n");
    }
    else if (cmd[0] == 'A' && cmd[1] == 'C' && cmd[2] == 'C' && cmd[3] == 'E' && cmd[4] == 'L' && cmd[5] == '=') {
        int32_t val = 0; const char *p = cmd + 6;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (val >= 100 && val <= 500000) accel_limit = val;
        config_mark_dirty(); uart_puts("OK ACCEL="); uart_putint(accel_limit); uart_puts("\r\n");
    }
    else if (cmd[0] == 'J' && cmd[1] == 'E' && cmd[2] == 'R' && cmd[3] == 'K' && cmd[4] == '=') {
        int32_t val = 0; const char *p = cmd + 5;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (val >= 1000 && val <= 10000000) jerk_limit = val;
        config_mark_dirty(); uart_puts("OK JERK="); uart_putint(jerk_limit); uart_puts("\r\n");
    }
    else if (cmd[0] == 'O' && cmd[1] == 'N') {
        open_loop = 1; ENA_PIN = ENA_ON; moving = 1;
        if (!OC1CONbits.ON) motor_set_speed(100);
        uart_puts("OK ON\r\n");
    }
    else if (cmd[0] == 'O' && cmd[1] == 'F' && cmd[2] == 'F') {
        open_loop = 0; motor_disable(); moving = 0; at_target = 1;
        queue_active = 0; queue_len = 0; queue_idx = 0;
        uart_puts("OK OFF\r\n");
    }
    else if (cmd[0] == 'C' && cmd[1] == 'W' && cmd[2] != 'W') {
        DIR_PIN = DIR_CW; uart_puts("OK CW\r\n");
    }
    else if (cmd[0] == 'C' && cmd[1] == 'C' && cmd[2] == 'W') {
        DIR_PIN = DIR_CCW; uart_puts("OK CCW\r\n");
    }
    else if (cmd[0] == 'S' && cmd[1] == 'P' && cmd[2] == 'E' && cmd[3] == 'E' && cmd[4] == 'D' && cmd[5] == '=') {
        int32_t val = 0; const char *p = cmd + 6;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (val > 0 && val <= 100000) { motor_set_speed((uint32_t)val); uart_puts("OK SPEED="); uart_putint(val); uart_puts("\r\n"); }
    }
    else if (cmd[0] == 'P' && cmd[1] == 'R' && cmd[2] == 'O' && cmd[3] == 'F' && cmd[4] == 'I' && cmd[5] == 'L' && cmd[6] == 'E' && cmd[7] == '=') {
        if (cmd[8] == 'S') { profile = 0; config_mark_dirty(); uart_puts("OK PROFILE=S (S-curve)\r\n"); }
        else if (cmd[8] == 'T') { profile = 1; config_mark_dirty(); uart_puts("OK PROFILE=T (Trapezoidal)\r\n"); }
        else { uart_puts("ERR USE PROFILE=S OR PROFILE=T\r\n"); }
    }
    else if (cmd[0] == 'Z' && cmd[1] == 'E' && cmd[2] == 'R' && cmd[3] == 'O') {
        encoder_offset = -(int32_t)POS1CNT;
        uart_puts("OK ZERO\r\n");
    }
    else if (cmd[0] == 'T' && cmd[1] == 'U' && cmd[2] == 'N' && cmd[3] == 'E') {
        int32_t off = tune_offset, vel = tune_relay_vel, hys = tune_hyst;
        const char *p = cmd + 4;
        if (*p == ':') {
            p++; off = 0;
            while (*p >= '0' && *p <= '9') { off = off * 10 + (*p - '0'); p++; }
            if (*p == ':') {
                p++; vel = 0;
                while (*p >= '0' && *p <= '9') { vel = vel * 10 + (*p - '0'); p++; }
                if (*p == ':') {
                    p++; hys = 0;
                    while (*p >= '0' && *p <= '9') { hys = hys * 10 + (*p - '0'); p++; }
                }
            }
        }
        if (off < 100) off = 3000;
        if (vel < 100) vel = 3000;
        if (hys < 1) hys = 10;
        tune_offset = off; tune_relay_vel = vel; tune_hyst = hys;
        tune_setpoint = encoder_read();
        tune_state = TUNE_MOVE;
        tune_output = 0;
        target_pos = tune_setpoint + tune_offset;
        fault = 0; fault_cnt = 0; integral = 0; sm_vel = 0; sm_acc = 0; vfrac = 0;
        queue_active = 0; queue_len = 0; queue_idx = 0;
        uart_puts("OK TUNE START\r\n");
    }
    else if (cmd[0] == 'G' && cmd[1] == 'E' && cmd[2] == 'T') {
        uart_puts("T="); uart_putint(target_pos);
        uart_puts(",P="); uart_putint(encoder_read());
        uart_puts(",E="); uart_putint(follow_err);
        uart_puts(",V="); uart_putint(current_vel);
        uart_puts(",KP="); uart_putint(Kp);
        uart_puts(",KI="); uart_putint(Ki);
        uart_puts(",KD="); uart_putint(Kd);
        uart_puts(",MAXV="); uart_putint(max_vel);
        uart_puts(",ACCEL="); uart_putint(accel_limit);
        uart_puts(",JERK="); uart_putint(jerk_limit);
        uart_puts(",TOL="); uart_putint(tolerance);
        uart_puts(",FT="); uart_putint(fault_thr);
        uart_puts(",PROFILE="); uart_puts(profile ? "T" : "S");
        uart_puts(",I="); uart_putint(coil_current);
        uart_puts(",US="); uart_putint(microstep);
        uart_puts(",QLEN="); uart_putint(queue_len);
        uart_puts(",QIDX="); uart_putint(queue_idx);
        uart_puts(",DWELL="); uart_putint(dwell_ms);
        uart_puts("\r\n");
    }
    // ── Queue commands ──
    else if (cmd[0] == 'Q' && cmd[1] == '=') {
        int32_t val = 0; uint8_t n = 0; const char *p = cmd + 2;
        while (1) {
            while (*p == ' ' || *p == '\t') p++;
            if (!((*p >= '0' && *p <= '9') || *p == '-')) break;
            int32_t sign = 1;
            if (*p == '-') { sign = -1; p++; }
            val = 0;
            while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
            if (n < QUEUE_MAX) queue[n++] = val * sign;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == ',') p++;
            else break;
        }
        if (n < 2) { uart_puts("ERR NEED ≥2 POINTS\r\n"); }
        else {
            queue_len = n; queue_idx = 0; queue_active = 1;
            target_pos = queue[0]; fault = 0; fault_cnt = 0; integral = 0; sm_vel = 0; sm_acc = 0; vfrac = 0; at_target = 0;
            uart_puts("OK Q="); uart_putint(queue_len); uart_puts(" points\r\n");
        }
    }
    else if (cmd[0] == 'D' && cmd[1] == 'W' && cmd[2] == 'E' && cmd[3] == 'L' && cmd[4] == 'L' && cmd[5] == '=') {
        int32_t val = 0; const char *p = cmd + 6;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (val >= 0 && val <= 30000) dwell_ms = val;
        uart_puts("OK DWELL="); uart_putint(dwell_ms); uart_puts("\r\n");
    }
    else if (cmd[0] == 'Q' && cmd[1] == 'S' && cmd[2] == 'T' && cmd[3] == 'O' && cmd[4] == 'P') {
        queue_active = 0; queue_len = 0; queue_idx = 0;
        uart_puts("OK QSTOP\r\n");
    }
    // ── New colon-opcode commands (Phase 1) ──
    else if (cmd[0] == 'm' && cmd[1] == ':') {
        int32_t speed = 0, pos = 0, sign = 1;
        const char *p = cmd + 2;
        while (*p >= '0' && *p <= '9') { speed = speed * 10 + (*p - '0'); p++; }
        if (*p == ':') {
            p++;
            if (*p == '-') { sign = -1; p++; }
            while (*p >= '0' && *p <= '9') { pos = pos * 10 + (*p - '0'); p++; }
            pos *= sign;
        }
        queue_active = 0; queue_len = 0; queue_idx = 0;
        if (speed > 0 && speed <= 100000) max_vel = speed;
        target_pos = pos; fault = 0; fault_cnt = 0; integral = 0; sm_vel = 0; sm_acc = 0; vfrac = 0;
        uart_puts("OK m:"); uart_putint(speed); uart_puts(":"); uart_putint(target_pos); uart_puts("\r\n");
    }
    else if (cmd[0] == 'e' && cmd[1] == 'n' && cmd[2] == ':') {
        if (cmd[3] == '1') {
            open_loop = 0; ENA_PIN = ENA_ON;
            target_pos = encoder_read(); integral = 0; fault_cnt = 0; at_target = 0; sm_vel = 0; sm_acc = 0; vfrac = 0;
            fault = 0; prev_error = 0;
            uart_puts("OK en:1\r\n");
        } else if (cmd[3] == '0') {
            open_loop = 0; motor_disable(); moving = 0; at_target = 1;
            queue_active = 0; queue_len = 0; queue_idx = 0;
            uart_puts("OK en:0\r\n");
        }
    }
    else if (cmd[0] == 'z' && (cmd[1] == '\0' || cmd[1] == '\r' || cmd[1] == '\n')) {
        encoder_offset = -(int32_t)POS1CNT;
        uart_puts("OK z\r\n");
    }
    else if (cmd[0] == 'i' && cmd[1] == ':') {
        int32_t val = 0; const char *p = cmd + 2;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (val >= 100 && val <= 5000) coil_current = val;
        config_mark_dirty(); uart_puts("OK i:"); uart_putint(coil_current); uart_puts("\r\n");
    }
    else if (cmd[0] == 'u' && cmd[1] == 's' && cmd[2] == ':') {
        int32_t val = 0; const char *p = cmd + 3;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (val == 1 || val == 2 || val == 4 || val == 8 || val == 16 || val == 32) microstep = val;
        config_mark_dirty(); uart_puts("OK us:"); uart_putint(microstep); uart_puts("\r\n");
    }
    else if (cmd[0] == 'p' && cmd[1] == 'i' && cmd[2] == 'd' && cmd[3] == ':') {
        int32_t kp = 0, ki = 0, kd = 0;
        const char *p = cmd + 4;
        while (*p >= '0' && *p <= '9') { kp = kp * 10 + (*p - '0'); p++; }
        if (*p == ':') {
            p++;
            while (*p >= '0' && *p <= '9') { ki = ki * 10 + (*p - '0'); p++; }
            if (*p == ':') {
                p++;
                while (*p >= '0' && *p <= '9') { kd = kd * 10 + (*p - '0'); p++; }
            }
        }
        Kp = kp; Ki = ki; Kd = kd; prev_error = 0; config_mark_dirty();
        uart_puts("OK pid:"); uart_putint(Kp); uart_puts(":"); uart_putint(Ki); uart_puts(":"); uart_putint(Kd); uart_puts("\r\n");
    }
    else if (cmd[0] == 't' && cmd[1] == 'l' && cmd[2] == 'm' && cmd[3] == ':') {
        if (cmd[4] == '0') {
            tlm_enabled = 0;
            uart_puts("OK tlm:0\r\n");
        } else if (cmd[4] == '1') {
            int32_t val = 100; const char *p = cmd + 5;
            if (*p == ':') { p++; val = 0; while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; } }
            if (val >= 10 && val <= 10000) tlm_period = val;
            tlm_enabled = 1;
            tlm_ticks = (PB_CLOCK * tlm_period) / 1000;
            uart_puts("OK tlm:1:"); uart_putint(tlm_period); uart_puts("\r\n");
        }
    }
    else if (cmd[0] == 's' && cmd[1] == 't' && cmd[2] == '?') {
        uart_puts("p:"); uart_putint(encoder_read());
        uart_puts(",v:"); uart_putint(current_vel);
        uart_puts(",e:"); uart_putint(follow_err);
        uart_puts(",f:"); uart_putint((fault ? 1 : 0) | (moving ? 2 : 0) | (at_target ? 4 : 0) | (open_loop ? 8 : 0));
        uart_puts("\r\n");
    }
    else if (cmd[0] == 'q' && cmd[1] == ':') {
        int32_t val = 0; uint8_t n = 0; const char *p = cmd + 2;
        while (1) {
            while (*p == ' ' || *p == '\t') p++;
            if (!((*p >= '0' && *p <= '9') || *p == '-')) break;
            int32_t sign = 1;
            if (*p == '-') { sign = -1; p++; }
            val = 0;
            while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
            if (n < QUEUE_MAX) queue[n++] = val * sign;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == ':') p++;
            else break;
        }
        if (n < 2) { uart_puts("ERR q: NEED ≥2 POINTS\r\n"); }
        else {
            queue_len = n; queue_idx = 0; queue_active = 1;
            target_pos = queue[0]; fault = 0; fault_cnt = 0; integral = 0; sm_vel = 0; sm_acc = 0; vfrac = 0; at_target = 0;
            uart_puts("OK q:"); uart_putint(queue_len); uart_puts(" points\r\n");
        }
    }
    else if (cmd[0] == 'd' && cmd[1] == 'w' && cmd[2] == 'e' && cmd[3] == 'l' && cmd[4] == 'l' && cmd[5] == ':') {
        int32_t val = 0; const char *p = cmd + 6;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (val >= 0 && val <= 30000) dwell_ms = val;
        uart_puts("OK dwell:"); uart_putint(dwell_ms); uart_puts("\r\n");
    }
    else if (cmd[0] == 'q' && cmd[1] == 's' && cmd[2] == 't' && cmd[3] == 'o' && cmd[4] == 'p') {
        queue_active = 0; queue_len = 0; queue_idx = 0;
        uart_puts("OK qstop\r\n");
    }
    else {
        uart_puts("ERR UNKNOWN\r\n");
    }
}

static void uart_poll(void) {
    uart_clear_oerr();
    char c = uart_getchar();
    while (c != (char)-1) {
        LED_PIN = 1;
        if (c == '\r' || c == '\n') {
            if (rx_idx > 0) { rx_buf[rx_idx] = '\0'; parse_command(rx_buf); rx_idx = 0; }
        } else if (rx_idx < RX_BUF_SIZE - 1) { rx_buf[rx_idx++] = c; }
        LED_PIN = 0;
        c = uart_getchar();
    }
}

static void send_status(void) {
    int32_t actual = encoder_read();
    uart_puts("P:"); uart_putint(actual);
    uart_puts(",E:"); uart_putint(follow_err);
    uart_puts(",V:"); uart_putint(current_vel);
    uart_puts(",T:"); uart_putint(target_pos);
    uart_puts(",F:"); uart_putint(fault ? 1 : 0);
    uart_puts(",M:"); uart_putint(moving ? 1 : 0);
    uart_puts(",A:"); uart_putint(at_target ? 1 : 0);
    uart_puts(",Q:"); uart_putint(queue_active ? queue_idx + 1 : 0);
    uart_puts(",QL:"); uart_putint(queue_len);
    uart_puts(",TS:"); uart_putint(tune_state);
    uart_puts("\r\n");
}

// ── Main ──
int main(void) {
    ANSELA = 0; TRISACLR = (1 << 10); LED_PIN = 0;
    __builtin_enable_interrupts();
    uart_init(); qei_init(); motor_init(); control_init();
    config_load();

    uart_puts("\r\n===== POSITION CONTROL =====\r\n");
    uart_puts("T=<pos> HOME STOP CLEAR KP=<n> KI=<n> KD=<n>\r\n");
    uart_puts("MAXV=<n> ACCEL=<n> JERK=<n> TOL=<n> FT=<n>\r\n");
    uart_puts("PROFILE=S PROFILE=T ZERO GET\r\n");
    uart_puts("ON OFF CW CCW SPEED=<n>\r\n");
    uart_puts("m:<speed>:<pos> en:1/0 z i:<mA> us:<n>\r\n");
    uart_puts("pid:<kp>:<ki>:<kd> tlm:1:<ms> tlm:0 st?\r\n");
    uart_puts("TUNE[:<offset>:<vel>:<hyst>] auto-tune\r\n");

    uint32_t last_status = core_ticks();
    for (;;) {
        uart_poll();
        if (config_dirty && ((core_ticks() - config_save_time) >= (PB_CLOCK / 10))) {
            config_dirty = 0; config_save();
        }
        if (tlm_enabled && ((core_ticks() - last_status) >= tlm_ticks)) {
            send_status(); last_status = core_ticks(); LED_PIN = !LED_PIN;
        }
    }
}
