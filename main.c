#include "utils.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdbool.h>
#include <stdint.h>
#include <util/delay.h>

#define MUX_STEP_PERIOD 1

// These bytes are used to store the state of every LED in the 7-segment
// display. They are essentially a frame buffer for the 7-seg.
// TODO: Make a struct!
uint8_t digit1 = 0;
uint8_t digit2 = 0;
uint8_t digit3 = 0;

// Device Mode
// This is the state of the device wrt the top-level state machine.
// 0 - IDLE/STOP
// 1 - COUNTING
// 2 - RESET
// 3 - SETTINGS MENU
uint8_t deviceMode = 0;

// Return Mode stores the state number of the mode that a given mode "exits"
// to. This is useful when going to mode 3 because we should always return from
// the Settings Menu into the mode that we entered from
uint8_t returnMode;

// Counting Mode
// 0: Inventory Mode
// In Inventory Mode, we use only one IR interruptor. This is
// faster because we only need to read one ADC and the decision-making
// is simpler. Also, because it's direction agnostic, it correctly
// handles end/start of tape.
// 1: Dispense Mode
// In Dispense Mode we use both IR interruptors to create an encoder
// with the carrier tape. This allows us to determine the direction
// of the tape at the expense of counting more slowly. Also, because
// end/start of tape transitions look identical to motion in the
// opposite direction, we can't correctly count end/start of tape.
bool countingMode = 0;

// Pitch of the parts on the carrier tape in mm
uint8_t pitch = 4;

// Ratio of holes in the carrier tape to actual parts, calculated based on the
// pitch
float ratio = 1.0;

// Variable to store a value to be displayed on the 7-seg using the
// translateSegments() method
int displayBuf = 0;

// Count is a place to store the parts count, especially when the display is
// showing something different
int count = 0;

// numPos is used to store the individual digits of a number in order of their
// decimal place. This is the output buffer of the numPositions() method. It's
// global to avoid passing memory pointers around and inevitably creating a
// hard to diagnose bug.
uint8_t numPos[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

void setup() {
    // Use MCLKCTRLB to disable the clock divider to get a 20 MHz clock
    // MCLKCTRLB is under CCP (configuration change protection), so we have to
    // write 0xD8 to the CCP register before we change it.
    CCP = 0xD8;
    CLKCTRL_MCLKCTRLB &= ~(1);

    // Set pin modes
    PORTA.DIR |= 0b00111110; // 7, 6 are IR det inputs, 0 is UPDI
    PORTB.DIR |= 0b001111;   // 4, 5 are switch inputs
    PORTC.DIR |= 0b1111;     // All outputs

    // Initialize output pins to low
    PORTA.OUT = 0x00;
    PORTB.OUT = 0x00;
    PORTC.OUT = 0x00;

    // Turn GRN LED on
    PIN_HIGH(GRN_LED);

    // Setup type A timer to call interrupt at 1/MUX_STEP_PERIOD Hz
    tcaInterruptInit(&TCA0, TCA_SINGLE_CLKSEL_DIV1024_gc, F_CPU / 1024 * MUX_STEP_PERIOD / 1000);

    // Example: Setup type B timer to call interrupt
    // tcbInterruptInit(&TCB0, TCB_CLKSEL_CLKDIV1_gc, 1000);

    // Enable global interrupts. Required call to use any interrupts.
    sei();
}

// Type A timer interrupt for digit MUX step
ISR(TCA0_OVF_vect) {
    // Clear interrupt flag
    TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;

    // TODO: Implement dimming by setting SINGLE.PER to one or another value
    // every other call and blank every other time. The ratio between the two
    // counts then defines a duty cycle.

    digitsMuxNext(DIG_MASK_LUT);
    PIN_TOGGLE(GRN_LED);
}

// Example: Type B timer interrupt
// ISR(TCB0_INT_vect) {
//     // Clear interrupt flag
//     TCB0.INTFLAGS = TCB_CAPT_bm;
// }

void loop() {
    // Toggle GRN and YEL LED
    // PIN_TOGGLE(GRN_LED);
    PIN_TOGGLE(YEL_LED);

    // Print something on the UART
    // printf("Hello World");

    _delay_ms(1100);
}

int main() {
    setup();
    while (1) {
        loop();
    }
    return 0;
}
