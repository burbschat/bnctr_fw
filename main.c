#include <avr/io.h>
// F_CPU frequency is defined in the makefile
#include <util/delay.h>
#include "uart.h"

// LED is on pin 0 of PORTC
#define LED 0
#define DELAY_MS 500

int main()
{
    // Use MCLKCTRLB to disable the clock divider to get a 20 MHz clock
    // MCLKCTRLB is under CCP (configuration change protection), so we
    // have to write 0xD8 to the CCP register before we change it.
    CCP = 0xD8;
    CLKCTRL_MCLKCTRLB &= ~(1);

    // setup LED pin for output in port C's direction register and set LED pin LOW
    PORTC_DIR |= (1 << LED);
    PORTC_OUT &= ~(1 << LED);

    // Initialize UART
    USART0_init();

    while (1)
    {
        // Toggle pin 0
        PORTC_OUT ^= PIN0_bm;

        // Print something on the UART
        printf("Hello World");

        _delay_ms(500);
    }

    return 0;
}
