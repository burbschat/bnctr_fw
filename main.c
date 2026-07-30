#include "utils.h"
#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <util/delay.h>

#define MUX_STEP_PERIOD 1

// Buffer to store 7 segment LED masks for what is currently on the display.
// Pass this to the multiplexing functions which flashes one digit at a time.
uint8_t digit_buffer[N_DIGS];

// Device Mode
// This is the state of the device wrt the top-level state machine.
// 0 - IDLE/STOP
// 1 - COUNTING
// 2 - RESET
// 3 - SETTINGS MENU
uint8_t state = 0;

// Ratio of holes in the carrier tape to actual parts, calculated based on the
// pitch
float part_to_hole_ratio = 1.0;

// Count is a place to store the parts count, especially when the display is
// showing something different
int state_trans_count = 0;
int hole_count = 0;
int part_count = 0;

// numPos is used to store the individual digits of a number in order of their
// decimal place. This is the output buffer of the numPositions() method. It's
// global to avoid passing memory pointers around and inevitably creating a
// hard to diagnose bug.
uint8_t num_pos[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// State of the analog comparators. 0th/1st bit indicate 0th/1st sensor state
// (HIGH or LOW). By checking the current and last state it is possible to
// deduce the direction the tape moved as the pattern has at least three
// elements and no mirror symmetry.
uint8_t ac_state_prev = 0b11;
uint8_t ac_state_curr = 0b11;

void setup() {
    // Use MCLKCTRLB to disable the clock divider to get a 20 MHz clock
    // MCLKCTRLB is under CCP (configuration change protection), so we have to
    // write 0xD8 to the CCP register before we change it.
    CCP = 0xD8;
    CLKCTRL_MCLKCTRLB &= ~(1);

    setupPins();

    // Blank display (all digits off)
    blankDisplay();
    // Clear the digit buffer
    clearDigitBuffer(digit_buffer);

    // Setup type A timer to call interrupt at 1/MUX_STEP_PERIOD Hz
    tcaInterruptInit(&TCA0, TCA_SINGLE_CLKSEL_DIV1024_gc, F_CPU / 1024 * MUX_STEP_PERIOD / 1000);

    // Example: Setup type B timer to call interrupt
    // tcbInterruptInit(&TCB0, TCB_CLKSEL_CLKDIV1_gc, 1000);

    setupDacVref(VREF_DAC0REFSEL_1V5_gc, VREF_DAC1REFSEL_0V55_gc);

    // Set reference voltages
    setDacRef(&DAC0, 30);
    setDacRef(&DAC1, 30);

    // Setup ACs and enable interrupts. Also enables AC output mapping to
    // package pin.
    // AC1 -> PA6 (IRDETA)
    // AC0 -> PA7 (IRDETB)
    // Can set POS/NEG/BOTH to set which edge call the interrupt on. Also
    // possible to get the output state in the intterupt to determine the edge.
    acInterruptInit(&AC0, AC_MUXPOS_PIN0_gc, AC_MUXNEG_DAC_gc, AC_HYSMODE_50mV_gc,
                    AC_INTMODE_BOTHEDGE_gc, true);
    acInterruptInit(&AC1, AC_MUXPOS_PIN1_gc, AC_MUXNEG_DAC_gc, AC_HYSMODE_50mV_gc,
                    AC_INTMODE_BOTHEDGE_gc, true);

    // Turn on the IR LEDs
    pinHigh(IR_LED);

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

    digitsMuxNext(digit_buffer);
}

// Example: Type B timer interrupt
// ISR(TCB0_INT_vect) {
//     // Must clear interrupt flag manually
//     TCB0.INTFLAGS = TCB_CAPT_bm;
// }

// Handler called on AC state change. This function does the actual counting.
void acStateChangedHandler() {
    // Ignore state 0b11 as this is the shortest state and we only require
    // three to figure out the direction.
    if ((ac_state_curr != 0b11) && (ac_state_curr != ac_state_prev)) {
        if (ac_state_curr == ((ac_state_prev + 1) % 3)) {
            state_trans_count++;
        } else if (((ac_state_curr + 1) % 3) == ac_state_prev) {
            state_trans_count--;
        } else {
            // TODO: Error state? But really really should never happen I guess...
        }

        // Do the state update only if the state really changed (otherwise a
        // glitch could result in an invalid curr/prev state combination.
        ac_state_prev = ac_state_curr;

        // Update hole count (three state transitions per hole) Always
        // undercounting by one hole as we need at least two holes not
        // detect a transition.
        // TODO: Not sure how to deal with this yet. Perhaps we cannot get
        // around detecting the start/end of tape conditions? If we could
        // detect those reliably, I guess one could just add 1 to the hole
        // count to correct it? Maybe, do the +1 after start pattern and end
        // pattern detected each once (condition for tape completely passed the
        // device).
        // Another option might be to adjust hardware to actually never
        // give the 0b11 state except when there is not tape inserted. If
        // possible, this might be the most elegant solution...
        // +1 to the transitions count here (together with init values for
        // curr/prev state) makes it such that we count the same number for
        // both directions (up/down).
        hole_count = (abs(state_trans_count) + 1) / 3;
        // Update part count derived from hole count
        part_count = hole_count * part_to_hole_ratio;
        // Display the new count
        setDigitBufferInt(abs(hole_count), digit_buffer);
    } else {
        // State is 0b11. Could use for start/end of tape detection?
        // At start we get
        // 0b11 0b01 b011 0b01
        // or
        // 0b11 0b10 b011 0b10
        // depending on whether the tape end is currently at the top or
        // bottom of the device.
    }
}

// AC Interrupts for hole detection
ISR(AC0_AC_vect) {
    // Must clear interrupt flag manually
    AC0.STATUS = AC_CMP_bm;

    if (getAcOutState(&AC0)) {
        // Comparator HIGH -> IR detected (OUT LED off)

        // Indicate AC0 state high
        ac_state_curr |= 0b01;
    } else {
        // Comparator HIGH -> IR blocked (OUT LED on)

        // Indicate AC0 state low
        ac_state_curr &= ~0b01;
    }

    // Handle AC state changed
    acStateChangedHandler();
}

ISR(AC1_AC_vect) {
    // Must clear interrupt flag manually
    AC1.STATUS = AC_CMP_bm;

    if (getAcOutState(&AC1)) {
        // Comparator HIGH -> IR detected (OUT LED off)

        // Indicate AC1 state high
        ac_state_curr |= 0b10;
    } else {
        // Comparator HIGH -> IR blocked (OUT LED on)

        // Indicate AC1 state low
        ac_state_curr &= ~0b10;
    }

    // Handle AC state changed
    acStateChangedHandler();
}

ISR(PORTB_PORT_vect) {
    // Must clear interrupt flag manually
    uint8_t portBFlags = PORTB.INTFLAGS;
    PORTB.INTFLAGS = portBFlags;

    if (portBFlags & PIN4_bm) {
        // Upper button pressed
        setDigitBufferInt(111, digit_buffer);
    }
    if (portBFlags & PIN5_bm) {
        // Lower button pressed
        setDigitBufferInt(222, digit_buffer);
    }
}

void loop() {
    // This blocks and thus slows down the MCU. Useful for debugging.
    // Eventually the MCU should run as fast as possible so make sure no
    // blocking delays remain.
    // _delay_ms(10);
}

int main() {
    setup();
    while (1) {
        loop();
    }
    return 0;
}
