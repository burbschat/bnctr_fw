#include "utils.h"
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sleep.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <util/delay.h>

// Buffer to store 7 segment LED masks for what is currently on the display.
// Pass this to the multiplexing functions which flashes one digit at a time.
uint8_t digit_buffer[N_DIGS];

// Device Mode
uiState ui_state = COUNT;

// Button states
uint8_t button_state_prev = 0b00;
uint8_t button_state_curr = 0b00;

// Number of holes per part in the tape. SMD tape usually has an integer number
// of holes per part and never more than one part per hole.
uint8_t holes_per_part = 1;

// Count is a place to store the parts count, especially when the display is
// showing something different
int state_trans_count = 0;
int hole_count = 0;
int part_count = 0;

// State of the analog comparators. 0th/1st bit indicate 0th/1st sensor state
// (HIGH or LOW). By checking the current and last state it is possible to
// deduce the direction the tape moved as the pattern has at least three
// elements and no mirror symmetry.
uint8_t ac_state_prev = 0b101;
uint8_t ac_state_curr = 0b101;

// Bitmasks for each of the three sensors
#define AC_STATE_S0_bm 0b001 // Top sensor
#define AC_STATE_S1_bm 0b010 // Middle sensor
#define AC_STATE_S2_bm 0b100 // Bottom sensor

// Macro to get the bits for sensor 0 and sensor 2 but next to each other. Used
// for support of the two-sensor mode. Map sensor 0 to 0b01 and sensor 2 to
// 0b10.
#define AC_STATE_TWOSTATE(state)                                                                   \
    (((state & AC_STATE_S0_bm) >> 0) | ((state & AC_STATE_S2_bm) >> 2) << 1)

// Pointer to the callback function the called by interrupt after button
// longpress timer times out.
void (*longpress_callback)(void) = NULL;

void setup() {
    // MCLKCTRLB is under CCP (configuration change protection), so we have to
    // write 0xD8 (CCP_IOREG_gc) to the CCP register before we change it.
    CCP = CCP_IOREG_gc;
#if PDIV
    CLKCTRL.MCLKCTRLB = CLKCTRL_PEN_bm | PDIV;
#else
    CLKCTRL.MCLKCTRLB &= ~CLKCTRL_PEN_bm;
#endif

    setupPins();

    // Blank display (all digits off)
    blankDisplay();
    // Clear the digit buffer
    clearDigitBuffer(digit_buffer);

    // Setup type A timer to call interrupt at 1/MUX_STEP_PERIOD_MS*1000 Hz but
    // illuminate only every MUX_DUTY_DIV times so the timer time is
    // adjusted for that to keep the step period fixed.
    // tcaInterruptInit(&TCA0, TCA_SINGLE_CLKSEL_DIV1024_gc, F_CPU / 1024 * MUX_STEP_PERIOD_MS /
    // MUX_DUTY_DIV / 1000);
    tcbInterruptInit(&TCB0, TCB_CLKSEL_CLKDIV2_gc,
                     F_CPU / 2 * MUX_STEP_PERIOD_MS / MUX_DUTY_DIV / 1000);
    // Enable the timer immediately
    TCB0.CTRLA |= TCB_ENABLE_bm;

    // Setup type B timer for button long press timeout.
    tcaInterruptInit(&TCA0, TCA_SINGLE_CLKSEL_DIV1024_gc,
                     F_CPU / 1024 * BUTTON_LONGPRESS_TIMEOUT_MS / 1000);

    setupDacVref(VREF_DAC0REFSEL_1V5_gc, VREF_DAC1REFSEL_0V55_gc, VREF_DAC2REFSEL_0V55_gc);

    // Set reference voltages
    setDacRef(&DAC0, 30);
    setDacRef(&DAC1, 30);
    setDacRef(&DAC2, 30);

    // Setup ACs and enable interrupts. Also enables AC output mapping to
    // package pin.
    // AC1 -> PA6 (IRDETA)
    // AC0 -> PA7 (IRDETB)
    // AC2 -> PB5 (IRDETC)
    // Can set POS/NEG/BOTH to set which edge call the interrupt on. Also
    // possible to get the output state in the intterupt to determine the edge.
    acInterruptInit(&AC0, AC_MUXPOS_PIN0_gc, AC_MUXNEG_DAC_gc, AC_HYSMODE_50mV_gc,
                    AC_INTMODE_BOTHEDGE_gc);
    acInterruptInit(&AC1, AC_MUXPOS_PIN1_gc, AC_MUXNEG_DAC_gc, AC_HYSMODE_50mV_gc,
                    AC_INTMODE_BOTHEDGE_gc);
    acInterruptInit(&AC2, AC_MUXPOS_PIN2_gc, AC_MUXNEG_DAC_gc, AC_HYSMODE_50mV_gc,
                    AC_INTMODE_BOTHEDGE_gc);

    // Turn on the IR LEDs
    pinHigh(IR_LED);

    // Enable global interrupts. Required call to use any interrupts.
    sei();
}

// Type B timer interrupt for digit MUX step
ISR(TCB0_INT_vect) {
    // Must clear interrupt flag manually
    TCB0.INTFLAGS = TCB_CAPT_bm;

    // Note: Could implement dimming by setting SINGLE.PER to one or another
    // value every other call and blank every other time. The ratio between the
    // two counts then defines a duty cycle.

    // Illumination duty cycle set with an integer divider (fraction). Turn the
    // display only on every MUX_DUTY_DIVth cycle. For exsample if MUX_DUTY_DIV
    // = 4, every 4th MUX_STEP_PERIOD_MS/4 long cycle will illuminate a given
    // digit.
    static uint8_t updated_count = 0;
    if (updated_count % MUX_DUTY_DIV == 0) {
        digitsMuxNext(digit_buffer);
        updated_count = 0;
    } else {
        blankDisplay();
    }
    updated_count++;
}

// Update hole count (three state transitions per hole) Always undercounting by
// one hole as we need at least two holes not detect a transition.
// TODO: Not sure how to deal with this yet. Perhaps we cannot get around
// detecting the start/end of tape conditions? If we could detect those
// reliably, I guess one could just add 1 to the hole count to correct it?
// Maybe, do the +1 after start pattern and end pattern detected each once
// (condition for tape completely passed the device).
// Another option might be to adjust hardware to actually never give the 0b11
// state except when there is not tape inserted. If possible, this might be the
// most elegant solution...
// +1 to the transitions count here (together with init values for curr/prev
// state) makes it such that we count the same number for both directions
// (up/down).
void updateHoleCount() {
    hole_count = (abs(state_trans_count) + 1) / 3;
    // Update part count derived from hole count
    part_count = hole_count / holes_per_part;
    // Display the new count if in correct UI mode
    if (ui_state == COUNT) {
        setDigitBufferInt(part_count, HIDE_LEADING_ZEROS, digit_buffer);
    }
}

// Handler called on AC state change. This function does the actual counting.
void acStateChangedHandler() {
    // Ignore state 0b11 as this is the shortest state and we only require
    // three to figure out the direction.
    if ((AC_STATE_TWOSTATE(ac_state_curr) != 0b11) &&
        (AC_STATE_TWOSTATE(ac_state_curr) != AC_STATE_TWOSTATE(ac_state_prev))) {
        if (AC_STATE_TWOSTATE(ac_state_curr) == ((AC_STATE_TWOSTATE(ac_state_prev) + 1) % 3)) {
            state_trans_count++;
        } else if (((AC_STATE_TWOSTATE(ac_state_curr) + 1) % 3) ==
                   AC_STATE_TWOSTATE(ac_state_prev)) {
            state_trans_count--;
        } else {
            // Technically an error state, but really really should never happen I guess...
            displayErr(digit_buffer);
            return;
        }

        // Do the state update only if the state really changed (otherwise a
        // glitch could result in an invalid curr/prev state combination.
        ac_state_prev = ac_state_curr;

        updateHoleCount();
    } else {
        // State is 0b11. Could use for start/end of tape detection?
        // At start we get
        // 0b11 0b01 b011 0b01
        // or
        // 0b11 0b10 b011 0b10
        // depending on whether the tape end is currently at the top or
        // bottom of the device.
    }

#ifdef DEBUG_SHOW_SENSOR_STATE
    displayACStates((uint8_t[]){ ac_state_curr, ac_state_prev, 0 }, digit_buffer);
#endif /* ifdef DEBUG_SHOW_SENSOR_STATE */
}

// AC Interrupts for hole detection.
// Note that the SC number does not necessarily match the sensor number
// (whichever is fine so the assignment was chosen as to simplify PCB routing).
ISR(AC0_AC_vect) { // Sensor 1
    // Must clear interrupt flag manually
    AC0.STATUS = AC_CMP_bm;

    if (getAcOutState(&AC0)) {
        // Comparator HIGH -> IR detected (OUT LED off)

        // Indicate sensor 1 state high. AC0 is sensor 1!
        ac_state_curr |= AC_STATE_S1_bm;
    } else {
        // Comparator HIGH -> IR blocked (OUT LED on)

        // Indicate sensor 1 state low. AC0 is sensor 1!
        ac_state_curr &= ~AC_STATE_S1_bm;
    }

    // Handle AC state changed
    acStateChangedHandler();
}

ISR(AC1_AC_vect) { // Sensor 0
    // Must clear interrupt flag manually
    AC1.STATUS = AC_CMP_bm;

    if (getAcOutState(&AC1)) {
        // Comparator HIGH -> IR detected (OUT LED off)

        // Indicate sensor 0 state high. AC1 is sensor 0!
        ac_state_curr |= AC_STATE_S0_bm;
    } else {
        // Comparator HIGH -> IR blocked (OUT LED on)

        // Indicate sensor 0 state low. AC1 is sensor 0!
        ac_state_curr &= ~AC_STATE_S0_bm;
    }

    // Handle AC state changed
    acStateChangedHandler();
}

ISR(AC2_AC_vect) { // Sensor 2
    // Must clear interrupt flag manually
    AC2.STATUS = AC_CMP_bm;

    if (getAcOutState(&AC2)) {
        // Comparator HIGH -> IR detected (OUT LED off)

        // Indicate sensor 2 state high. Here AC and sensor numbers match!
        ac_state_curr |= AC_STATE_S2_bm;
    } else {
        // Comparator HIGH -> IR blocked (OUT LED on)

        // Indicate sensor 2 state low. Here AC and sensor numbers match!
        ac_state_curr &= ~AC_STATE_S2_bm;
    }

    // Handle AC state changed
    acStateChangedHandler();
}

// Type A timer interrupt for button timeout
ISR(TCA0_OVF_vect) {
    // Must clear interrupt flag manually
    TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;

    // Stop timer as button timeout should be a oneshot
    TCA0.SINGLE.CTRLA &= ~TCA_SINGLE_ENABLE_bm;

    // Call the callback
    if (longpress_callback != NULL) longpress_callback();
}

void startLongpressTimeout(void (*callback)(void)) {
    // Set pointer to callback function
    longpress_callback = callback;

    // Clear interrupt flag and Reset timer count (clearing interrupt might not
    // be strictly necessary)
    TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;
    TCA0.SINGLE.CNT = 0;

    // Enable the timer
    TCA0.SINGLE.CTRLA |= TCA_SINGLE_ENABLE_bm;
}

void abortLongpressTimeout() {
    // Disable the timer
    TCA0.SINGLE.CTRLA &= ~TCA_SINGLE_ENABLE_bm;

    // Clear interrupt flag and Reset timer count (clearing interrupt might not
    // be strictly necessary)
    TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;
    TCA0.SINGLE.CNT = 0;
}

void returnToCountLongpressCallback() {
    // Move back to count state
    ui_state = COUNT;
    // Display the
    updateHoleCount();
}

void updateHolesPerPart(int8_t delta) {
    // Update according to passed delta but only when in allowed range (must be
    // displayable with two digits and at least 1).
    int8_t tmp = holes_per_part + delta;
    if ((tmp > 0) && (tmp < 100)) {
        holes_per_part = tmp;
    }
    // Display the current value
    displayHPPValue(holes_per_part, digit_buffer);
}

void buttonsStateChangedHandler() {
    // Update buttons state
    button_state_prev = button_state_curr;
    button_state_curr = BUTTON_STATE;

    // Previously not pressed, now pressed flags (rising edge)
    uint8_t button_r = button_state_curr & ~button_state_prev;
    // Previously pressed, now not pressed flags (falling edge)
    uint8_t button_f = button_state_prev & ~button_state_curr;

    switch (ui_state) {
    case COUNT:
        // Upper button rising edge
        if (button_r & BUTTON_UPPER_bm) {
            // Reset count
            state_trans_count = 0;
            updateHoleCount();
        }
        // Lower button falling edge
        if (button_f & BUTTON_LOWER_bm) {
            // Transition on release (falling edge) to avoid immediately
            // decrementing the value after transition to HPP_INPUT.
            ui_state = HPP_INPUT;
            // Call once with delta=0 to just update the display to show the
            // current HPP value
            updateHolesPerPart(0);
        }
        break;
    case HPP_INPUT:
        // Upper button rising edge
        if (button_r & BUTTON_UPPER_bm) {
            // Start longpress timeout
            startLongpressTimeout(returnToCountLongpressCallback);
        }
        // Upper button falling edge
        if (button_f & BUTTON_UPPER_bm) {
            // Abort longpress timeout if released
            abortLongpressTimeout();
            // Increment HPP value and update display. Do on button release to
            // avoid interference with return to COUNT long press.
            updateHolesPerPart(1);
        }
        // Lower button falling edge
        if (button_f & BUTTON_LOWER_bm) {
            // Decrement HPP value and update display. Do on button release to
            // follow behaviour used for increment.
            updateHolesPerPart(-1);
        }
        break;
    }
}

ISR(PORTB_PORT_vect) {
    // Must clear interrupt flag manually
    uint8_t port_b_flags = PORTB.INTFLAGS;
    PORTB.INTFLAGS = port_b_flags;

    buttonsStateChangedHandler();
}

int main() {
    setup();

    // Choose either of: SLEEP_MODE_IDLE, SLEEP_MODE_STANDBY, SLEEP_MODE_PWR_DOWN
    // The latter two stop the clock so timers won't work.
    // PIT/RTC should still work so consider using those for the interrupts.
    set_sleep_mode(SLEEP_MODE_IDLE);

    // As the firmware is completely interrupt based, we can have the CPU wait
    // in a lower power mode until an interrupt occurs.
    while (1) {
        sleep_mode();
    }
    return 0;
}
