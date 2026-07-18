#include <avr/io.h>
// F_CPU frequency is defined in the makefile
#include "uart.h"
#include <util/delay.h>

// GRN LED is on pin 5 of PORTA
#define LED_GRN 5
// YEL LED is on pin 3 of PORTA
#define LED_YEL 3
// Blink delay
#define DELAY_MS 500

int main() {
    // Use MCLKCTRLB to disable the clock divider to get a 20 MHz clock
    // MCLKCTRLB is under CCP (configuration change protection), so we
    // have to write 0xD8 to the CCP register before we change it.
    CCP = 0xD8;
    CLKCTRL_MCLKCTRLB &= ~(1);

    // GRN LED on PA5
    PORTA.DIR |= PIN5_bm;
    // YEL LED on PB3
    PORTB.DIR |= PIN3_bm;

    // Initialize output pins
    PORTA.OUT |= PIN5_bm;  // High
    PORTB.OUT &= ~PIN3_bm; // Low

    // Initialize UART
    // USART0_init();

    while (1) {
        // Toggle GRN LED
        PORTA.OUT ^= PIN5_bm;
        // Toggle YEL LED
        PORTB.OUT ^= PIN3_bm;

        // Print something on the UART
        // printf("Hello World");

        _delay_ms(300);
    }

    return 0;
}
