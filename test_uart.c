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

int main(void) {
    // UART init
    syskey_unlock(); CFGCONbits.IOLOCK = 0;
    RPE0R = 1; U1RXR = 10;
    syskey_unlock(); CFGCONbits.IOLOCK = 1;
    ANSELECLR = (1 << 0); TRISECLR = (1 << 0);
    ANSELGCLR = (1 << 8); TRISGSET = (1 << 8);
    U1MODE = 0;
    U1BRG = (PB_CLOCK / (16 * 19200)) - 1;
    U1MODEbits.UARTEN = 1; U1STAbits.UTXEN = 1; U1STAbits.URXEN = 1;

    // Blink LED
    ANSELA = 0; TRISACLR = (1 << 10);
    LATAbits.LATA10 = 1;

    // Send hello
    while (U1STAbits.UTXBF);
    U1TXREG = 'H';
    while (U1STAbits.UTXBF);
    U1TXREG = 'i';
    while (U1STAbits.UTXBF);
    U1TXREG = '!';
    while (U1STAbits.UTXBF);
    U1TXREG = '\r';
    while (U1STAbits.UTXBF);
    U1TXREG = '\n';

    LATAbits.LATA10 = 0;

    while (1) {
        int i;
        for (i = 0; i < 4000000; i++) { asm("nop"); }
        LATAbits.LATA10 = !LATAbits.LATA10;
    }
}
