#include "utils.h"
#include <avr/io.h>
#include <stdint.h>

// Setup pins as required in this firmware
void setupPins() {
    // Set pin modes
    PORTA.DIR |= 0b00111110; // 7, 6 are IR det inputs, 0 is UPDI
    PORTB.DIR |= 0b000111;   // 3, 4 are button inputs, 5 is IR det input
    PORTC.DIR |= 0b1111;     // All outputs

    // Initialize all output pins to low
    PORTA.OUT = 0x00;
    PORTB.OUT = 0x00;
    PORTC.OUT = 0x00;

    // Disable input buffer/pullup for AC input pins to get highest possible
    // impedance.
    PORTA.PIN6CTRL = PORT_ISC_INPUT_DISABLE_gc;
    PORTA.PIN7CTRL = PORT_ISC_INPUT_DISABLE_gc;
    PORTB.PIN5CTRL = PORT_ISC_INPUT_DISABLE_gc;

    // Enable internal pullup (button on pulls low) and interrupts on both edge
    // for the button inputs
    PORTB.PIN3CTRL = PORT_PULLUPEN_bm | PORT_ISC_BOTHEDGES_gc;
    PORTB.PIN4CTRL = PORT_PULLUPEN_bm | PORT_ISC_BOTHEDGES_gc;
}

// Setup DAC for AC vref as required in this firmware
void setupDacVref(VREF_DAC0REFSEL_t ref0, VREF_DAC1REFSEL_t ref1, VREF_DAC2REFSEL_t ref2) {
    // Use DAC output as reference voltage for AC so setup the DAC(s).
    // AC0 uses DAC0, AC1 uses DAC1 etc.
    VREF.CTRLA = ref0 | ref1 | ref2;
    VREF.CTRLB = VREF_DAC0REFEN_bm | VREF_DAC1REFEN_bm | VREF_DAC2REFEN_bm;
    // Enable DAC(s)
    DAC0.CTRLA = DAC_ENABLE_bm;
    DAC1.CTRLA = DAC_ENABLE_bm;
    DAC2.CTRLA = DAC_ENABLE_bm;
}

// Drive pins organized in array according to mask
void driveMask(uint8_t mask, const Pin *pins, const uint8_t nPins) {
    // Drive the digit lines according to value
    for (uint8_t i = 0; i < nPins; i++) {
        if ((mask >> i) & 1) {
            pinHigh(pins[i]);
        } else {
            pinLow(pins[i]);
        }
    }
};

void blankDisplay() {
    // Turn all digits off. Pins HIGH as P-channel MOSFET used.
    driveMask(~0, DIGS, N_DIGS);
}

void clearDigitBuffer(uint8_t *buffer) {
    for (uint8_t i = 0; i < N_DIGS; i++) {
        buffer[i] = 0;
    }
}

// Only have three digits so 16 bits suffices but 8 would be insufficient
void setDigitBufferIntMasked(uint16_t val, uint8_t digit_mask, bool hide_leading_zeros,
                             uint8_t *buffer) {
    uint16_t temp_val = val;
    uint16_t digit_val = val;

    for (uint8_t i = 0; i < N_DIGS; i++) {
        if (digit_mask & (1 << i)) {
            digit_val = temp_val % 10;
            // Hide leading zeros if that is enabled and we are not on the
            // first digit (avoid empty display if value is 0)
            if (hide_leading_zeros && i > 0 && temp_val == 0) {
                // If the value is zero all following digits would be 0. So if
                // that is the case and leading zeros should be hidden, blank
                // the digit.
                buffer[N_DIGS - i - 1] = 0;
            } else {
                // Set digit if enabled in mask
                buffer[N_DIGS - i - 1] = DIG_MASK_LUT[digit_val];
            }
            // Move on to next digit
            temp_val /= 10;
        } else {
            // Blank the digit if disabled in mask
            buffer[N_DIGS - i - 1] = 0;
        }
    }
}

void setDigitBufferInt(uint16_t val, bool hide_leading_zeros, uint8_t *buffer) {
    setDigitBufferIntMasked(val, (1 << N_DIGS) - 1, hide_leading_zeros, buffer);
}

void displayACStates(uint8_t *ac_states, uint8_t *buffer) {
    for (uint8_t i = 0; i < 3; i += 1) {
        uint8_t buffer_val = 0;
        for (uint8_t j = 0; j < 3; j += 1) {
            buffer_val |= (((ac_states[i] & 0b1 << j) != 0) ? HBAR_MASK_LUT[j] : 0b0);
        }
        buffer[i] = buffer_val;
    }
}

// Drive given digit using passed mask
void displayDigit(uint8_t segMask, uint8_t dig) {
    // Blank display before messing with the enabled segments
    blankDisplay();
    // Turn all digits off. Enabled pins LOW as low side driver.
    driveMask(~segMask, SEGS, N_SEGS);
    // Turn desired digit on. Enabled digit LOW, other HIGH as P-channel MOSFET
    // used.
    driveMask(~(1 << dig), DIGS, N_DIGS);
};

// Display HPP value, two digits, prepended by a P for 'pitch'
void displayHPPValue(uint16_t val, uint8_t *buffer) {
    // Set lower two digits to display the value
    // (Yes, the set first digit could be skipped completely but this is good
    // enough for now...)
    setDigitBufferIntMasked(val, 0b011, HIDE_LEADING_ZEROS, buffer);
    // Prepend with the letter P
    buffer[0] = MASK_FOR_CHAR('p');
}

// Display ERR to indicate error
void displayErr(uint8_t *buffer) {
    buffer[0] = MASK_FOR_CHAR('e');
    buffer[1] = MASK_FOR_CHAR('r');
    buffer[2] = MASK_FOR_CHAR('r');
}

// Execute one step of the digit multiplexing.
// Sets the segments and MOSFET accordingly. This should be called
// periodically. Three calls are required to step through all three digits.
void digitsMuxNext(uint8_t *segMasks) {
    static uint8_t step = 0;

    displayDigit(segMasks[step], step);

    step++;
    step = step % N_DIGS;
}

// Initialize A type timer for periodic interrupt.
// Can use a divider of up to 1024 and thus suitable for periods also on the
// order of seconds.
// Must be enabled by user after the setup by setting
// TCA<number>.SINGLE.CTRLA |= TCA_SINGLE_ENABLE_bm;
// Can be disabled again with (for a oneshot operation, disable in interrupt)
// TCA<number>.SINGLE.CTRLA &= ~TCA_SINGLE_ENABLE_bm;
void tcaInterruptInit(TCA_t *timer, TCA_SINGLE_CLKSEL_t clksel, register16_t count) {
    // Disable TCA before configuration
    timer->SINGLE.CTRLA = 0;
    // Normal mode
    timer->SINGLE.CTRLB = TCA_SINGLE_WGMODE_NORMAL_gc;

    // Set period (TOP value)
    timer->SINGLE.PER = count;

    // Clear interrupt flag. Interrupt must also clear this flag every time it
    // is called.
    timer->SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;
    // Enable overflow interrupt
    timer->SINGLE.INTCTRL = TCA_SINGLE_OVF_bm;

    // Set prescaler 1/clksel
    timer->SINGLE.CTRLA = clksel;
}

// Initialize B type timer for periodic interrupt.
// Maximum divider is 2 so this timer type is not the best choice for 'slow'
// periods on the order of say ms as the count register only has 16 bits.
// Must be enabled by user after the setup by setting
// TCB<number>.CTRLA |= TCB_ENABLE_bm;
// Can be disabled again with (for a oneshot operation, disable in interrupt)
// TCB<number>.CTRLA &= ~TCB_ENABLE_bm;
void tcbInterruptInit(TCB_t *timer, TCB_CLKSEL_t clksel, register16_t count) {
    // Disable timer before configuration
    timer->CTRLA = 0;
    // Periodic interrupt mode (actually the default but write to be sure)
    timer->CTRLB = TCB_CNTMODE_INT_gc;

    // Set the period
    // f_timer = f_CPU / CLKSEL
    // T_interrupt = CCMP / f_timer
    timer->CCMP = count;

    // Clear interrupt flag. Interrupt must also clear this flag every time it
    // is called.
    timer->INTFLAGS = TCB_CAPT_bm;
    // Enable interrupt on capture
    timer->INTCTRL = TCB_CAPT_bm;

    // Set prescaler 1/clksel
    timer->CTRLA = clksel;
}

void setDacRef(DAC_t *dac, register8_t val) {
    // TODO: Scale of a voltage value?
    // V_DACREF = DACREF/256 * V_REF
    dac->DATA = val;
}

void acInterruptInit(AC_t *ac, AC_MUXPOS_t pos, AC_MUXNEG_t neg, AC_HYSMODE_t hysmode,
                     AC_INTMODE_t intmode) {
    // Set positive and negative input
    ac->MUXCTRLA = pos | neg;
    // Enable AC and set hysteresis
    ac->CTRLA = AC_ENABLE_bm | hysmode | intmode;
    // Enable interrupt
    ac->INTCTRL = AC_CMP_bm;
}

bool getAcOutState(AC_t *ac) { return ac->STATUS & AC_STATE_bm; }
