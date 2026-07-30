#include "utils.h"
#include <avr/io.h>
#include <stdint.h>

// Setup pins as required in this firmware
void setupPins() {
    // Set pin modes
    PORTA.DIR |= 0b00111110; // 7, 6 are IR det inputs, 0 is UPDI
    PORTB.DIR |= 0b001111;   // 4, 5 are button inputs
    PORTC.DIR |= 0b1111;     // All outputs

    // Initialize all output pins to low
    PORTA.OUT = 0x00;
    PORTB.OUT = 0x00;
    PORTC.OUT = 0x00;

    // Disable input buffer/pullup for AC input pins to get highest possible
    // impedance.
    PORTA.PIN6CTRL = PORT_ISC_INPUT_DISABLE_gc;
    PORTA.PIN7CTRL = PORT_ISC_INPUT_DISABLE_gc;

    // Enable internal pullup and interrupts on falling edge (pull low) for the
    // button inputs
    PORTB.PIN4CTRL = PORT_PULLUPEN_bm | PORT_ISC_FALLING_gc;
    PORTB.PIN5CTRL = PORT_PULLUPEN_bm | PORT_ISC_FALLING_gc;
}

// Setup DAC for AC vref as required in this firmware
void setupDacVref(VREF_DAC0REFSEL_t ref1, VREF_DAC1REFSEL_t ref2) {
    // Use DAC output as reference voltage for AC so setup the DAC(s).
    // AC0 uses DAC0, AC1 uses DAC1 etc.
    VREF.CTRLA = ref1 | ref2;
    VREF.CTRLB = VREF_DAC0REFEN_bm | VREF_DAC1REFEN_bm;
    // Enable DAC(s)
    DAC0.CTRLA = DAC_ENABLE_bm;
    DAC1.CTRLA = DAC_ENABLE_bm;
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
void setDigitBufferInt(uint16_t val, uint8_t *buffer) {
    uint32_t tempVal = val;
    for (uint8_t i = 0; i < N_DIGS; i++) {
        buffer[N_DIGS - i - 1] = DIG_MASK_LUT[tempVal % 10];
        tempVal /= 10;
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

// Execute one step of the digit multiplexing.
// Sets the segments and MOSFET accordingly. This should be called
// periodically. Three calls are required to step through all three digits.
void digitsMuxNext(uint8_t *segMasks) {
    static uint8_t step = 0;

    displayDigit(segMasks[step], step);

    step++;
    step = step % N_DIGS;
}

// Initialize B type timer for periodic interrupt.
// Can use a divider of up to 1024 and thus suitable for periods also on the
// order of seconds.
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

    // Enable timer with prescaler /1024
    timer->SINGLE.CTRLA = clksel | TCA_SINGLE_ENABLE_bm;
}

// Initialize B type timer for periodic interrupt.
// Maximum divider is 2 so this timer type is not the best choice for 'slow'
// periods on the order of say ms as the count register only has 16 bits.
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

    // Enable timer, set timer clock
    timer->CTRLA = clksel | TCB_ENABLE_bm;
}

void setDacRef(DAC_t *dac, register8_t val) {
    // TODO: Scale of a voltage value?
    // V_DACREF = DACREF/256 * V_REF
    dac->DATA = val;
}

void acInterruptInit(AC_t *ac, AC_MUXPOS_t pos, AC_MUXNEG_t neg, AC_HYSMODE_t hysmode,
                     AC_INTMODE_t intmode, bool outen) {
    // Set positive and negative input
    ac->MUXCTRLA = pos | neg;
    // Enable AC, set hysteresis, enable output on pin.
    // Output pin is fixed for each AC, see datasheet.
    ac->CTRLA = AC_ENABLE_bm | hysmode | intmode;
    if (outen) {
        ac->CTRLA |= AC_OUTEN_bm;
    }
    // Enable interrupt
    ac->INTCTRL = AC_CMP_bm;
}

bool getAcOutState(AC_t *ac) { return ac->STATUS & AC_STATE_bm; }
