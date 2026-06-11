#include <xc.h>

#pragma config FNOSC   = FRC
#pragma config POSCMOD = OFF
#pragma config FWDTEN  = OFF
#pragma config FDMTEN  = OFF
#pragma config ICESEL  = ICS_PGx2
#pragma config JTAGEN  = OFF

#define PB_CLOCK 4000000

void syskey_unlock(void) {
    SYSKEY = 0xAA996655;
    SYSKEY = 0x556699AA;
}

void delay(uint32_t n) { while (n--) { asm("nop"); } }

int main(void) {
    // LED
    ANSELA = 0; TRISACLR = (1 << 10); LATAbits.LATA10 = 0;

    // UART init
    syskey_unlock(); CFGCONbits.IOLOCK = 0;
    RPE0R = 1; U1RXR = 10;
    syskey_unlock(); CFGCONbits.IOLOCK = 1;
    ANSELECLR = (1 << 0); TRISECLR = (1 << 0);
    ANSELGCLR = (1 << 8); TRISGSET = (1 << 8);
    U1MODE = 0;
    U1BRG = (PB_CLOCK / (16 * 19200)) - 1;
    U1MODEbits.UARTEN = 1; U1STAbits.UTXEN = 1; U1STAbits.URXEN = 1;

    // Send Hello
    const char msg[] = "\r\nHELLO FROM PIC32MK!\r\n";
    const char *p = msg;
    while (*p) {
        while (U1STAbits.UTXBF);
        U1TXREG = *p++;
    }

    delay(4000000);
    LATAbits.LATA10 = 1;
    delay(4000000);
    LATAbits.LATA10 = 0;

    while (1) {
        delay(4000000);
        LATAbits.LATA10 = !LATAbits.LATA10;
        const char *q = msg;
        while (*q) {
            while (U1STAbits.UTXBF);
            U1TXREG = *q++;
        }
        delay(8000000);
    }
}
