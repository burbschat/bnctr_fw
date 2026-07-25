#include "utils.h"
#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdbool.h>
#include <stdint.h>
#include <util/delay.h>

#define MUX_STEP_PERIOD 1

// Buffer to store 7 segment LED masks for what is currently on the display.
// Pass this to the multiplexing functions which flashes one digit at a time.
uint8_t digitBuffer[N_DIGS];

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

    // Initialize all output pins to low
    PORTA.OUT = 0x00;
    PORTB.OUT = 0x00;
    PORTC.OUT = 0x00;

    // Disable input buffer/pullup for AC input pins to get highest possible
    // impedance.
    PORTA.PIN6CTRL = PORT_ISC_INPUT_DISABLE_gc;
    PORTA.PIN7CTRL = PORT_ISC_INPUT_DISABLE_gc;

    // Blank display (all digits off)
    blankDisplay();
    // Clear the digit buffer
    clearDigitBuffer(digitBuffer);

    // Setup type A timer to call interrupt at 1/MUX_STEP_PERIOD Hz
    tcaInterruptInit(&TCA0, TCA_SINGLE_CLKSEL_DIV1024_gc, F_CPU / 1024 * MUX_STEP_PERIOD / 1000);

    // Example: Setup type B timer to call interrupt
    // tcbInterruptInit(&TCB0, TCB_CLKSEL_CLKDIV1_gc, 1000);

    // Use DAC output as reference voltage for AC so setup the DAC(s).
    // AC0 uses DAC0, AC1 uses DAC1 etc.
    VREF.CTRLA = VREF_DAC0REFSEL_1V5_gc | VREF_DAC1REFSEL_1V5_gc;
    VREF.CTRLB = VREF_DAC0REFEN_bm | VREF_DAC1REFEN_bm;
    // Enable DAC(s)
    DAC0.CTRLA = DAC_ENABLE_bm;
    DAC1.CTRLA = DAC_ENABLE_bm;

    // Set reference voltages
    setDACRef(&DAC0, 255);
    setDACRef(&DAC1, 0);

    // Setup ACs and enable interrupts. Also enables AC output mapping to
    // package pin.
    acInterruptInit(&AC0, AC_MUXPOS_PIN0_gc, AC_MUXNEG_DAC_gc, AC_HYSMODE_25mV_gc, true);
    acInterruptInit(&AC1, AC_MUXPOS_PIN0_gc, AC_MUXNEG_DAC_gc, AC_HYSMODE_25mV_gc, true);

    // Enable global interrupts. Required call to use any interrupts.
    sei();
}

// Type A timer interrupt for digit MUX step
ISR(TCA0_OVF_vect) {
    // Must clear interrupt flag manually
    TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;

    // TODO: Implement dimming by setting SINGLE.PER to one or another value
    // every other call and blank every other time. The ratio between the two
    // counts then defines a duty cycle.

    digitsMuxNext(digitBuffer);
}

// Example: Type B timer interrupt
// ISR(TCB0_INT_vect) {
//     // Must clear interrupt flag manually
//     TCB0.INTFLAGS = TCB_CAPT_bm;
// }

// AC Interrupts for hole detection
ISR(AC0_AC_vect) {
    // Must clear interrupt flag manually
    AC0.STATUS = AC_CMP_bm;
}

ISR(AC1_AC_vect) {
    // Must clear interrupt flag manually
    AC1.STATUS = AC_CMP_bm;
}

void loop() {
    static uint16_t count;
    setDigitBufferInt(count, digitBuffer);
    if (count >= 0xFFFF) {
        count = 0;
    } else {
        count++;
    }

    // This blocks and thus slows down the MCU. Useful for debugging.
    // Eventually the MCU should run as fast as possible so make sure no
    // blocking delays remain.
    _delay_ms(10);
}

int main() {
    setup();
    while (1) {
        loop();
    }
    return 0;
}
