#include <xc.h>

#pragma config FNOSCS   = FRC
#pragma config POSCMOD  = OFF
#pragma config FWDTEN   = OFF
#pragma config FDMTEN   = OFF
#pragma config ICESEL   = ICS_PGx2
#pragma config JTAGEN   = OFF

int main(void) {
    // LED on RA10
    ANSELA = 0;
    TRISACLR = (1 << 10);
    
    // UART TX on RPE0
    SYSKEY = 0xAA996655;
    SYSKEY = 0x556699AA;
    CFGCONbits.IOLOCK = 0;
    RPE0R = 1;
    CFGCONbits.IOLOCK = 1;
    SYSKEY = 0;
    
    ANSELECLR = (1 << 0);
    TRISECLR = (1 << 0);
    
    // UART1 at 19200 (PB_CLOCK=4MHz default)
    U1BRG = 12;
    U1MODEbits.UARTEN = 1;
    U1STAbits.UTXEN = 1;
    
    volatile int count = 0;
    while (1) {
        LATAbits.LATA10 = !LATAbits.LATA10;
        
        // Send "HELLO X\r\n"
        const char msg[] = "HELLO\r\n";
        for (int i = 0; msg[i]; i++) {
            while (U1STAbits.UTXBF);
            U1TXREG = msg[i];
        }
        
        // Delay
        for (volatile int d = 0; d < 2000000; d++);
    }
    return 0;
}
