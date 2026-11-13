#pragma once
#include <avr/io.h>
#include <stdbool.h>
#include <stdint.h>

// Time in ms to wait before illuminating the next digit
#ifndef MUX_STEP_PERIOD_MS
#define MUX_STEP_PERIOD_MS 1
#endif

// Duty cycle divider for digit illumination (duty = 1/MUX_DUTY_DIV).
// This is a per-digit duty cycle as the digits are flashed one at a time.
#ifndef MUX_DUTY_DIV
#define MUX_DUTY_DIV 1
#endif

// Duration in ms for a long button press event
#ifndef BUTTON_LONGPRESS_TIMEOUT_MS
#define BUTTON_LONGPRESS_TIMEOUT_MS 500
#endif

#ifndef HIDE_LEADING_ZEROS
#define HIDE_LEADING_ZEROS true
#endif

// Define type to completely describe a pin (port and pin number)
typedef struct {
    PORT_t *port;
    uint8_t pin;
} Pin;

static inline void pinHigh(Pin p) { p.port->OUTSET = (1 << p.pin); }
static inline void pinLow(Pin p) { p.port->OUTCLR = (1 << p.pin); }
static inline void pinToggle(Pin p) { p.port->OUTTGL = (uint8_t)(1u << p.pin); }

// Define pins as assigned on the board
#define IR_LED ((Pin){ &PORTA, 4 })

// Use arrays for pins that we might want to index. Consequently indices start
// at 0, not as 1 (as for the schematic), but start at 0 is nicer for the code
// so adopt that here.
#define N_DIGS 3
static const Pin DIGS[] = {
    { &PORTA, 2 },
    { &PORTA, 3 },
    { &PORTA, 5 },
};

#define N_SEGS 7
static const Pin SEGS[] = {
    { &PORTC, 3 }, { &PORTC, 1 }, { &PORTC, 2 }, { &PORTC, 0 },
    { &PORTB, 1 }, { &PORTB, 0 }, { &PORTB, 2 },
};

// Lookup table for 7 segment digit bit masks
// Indicies (starting at 0) as follows:
//  < 0 >
// ^     ^
// 1     2
// v     v
//  < 3 >
// ^     ^
// 4     5
// v     v
//  < 6 >
static const uint8_t N_DIG_MASKS = 10;
static const uint8_t DIG_MASK_LUT[10] = {
    0b01110111, 0b00100100, 0b01011101, 0b01101101, 0b00101110,
    0b01101011, 0b01111011, 0b00100101, 0b01111111, 0b01101111,
};
// Lookup table for best-effort letters on the 7 segment display
static const uint8_t LETTER_MASK_LUT[26] = {
    /* A */ 0b00111111, // A
    /* B */ 0b01111010, // b
    /* C */ 0b01010011, // C
    /* D */ 0b01111100, // d
    /* E */ 0b01011011, // E
    /* F */ 0b00011011, // F
    /* G */ 0b01101011, // G
    /* H */ 0b00111110, // H
    /* I */ 0b00110000, // I (looks like "I")
    /* J */ 0b01100100, // J
    /* K */ 0b00111110, // K (same as H, limited)
    /* L */ 0b01010010, // L
    /* M */ 0b00110111, // M (approx)
    /* N */ 0b01110110, // n
    /* O */ 0b01111111, // O (same as 0)
    /* P */ 0b00011111, // P
    /* Q */ 0b01101111, // q (approx)
    /* R */ 0b00010111, // r
    /* S */ 0b01101011, // S
    /* T */ 0b01011010, // t
    /* U */ 0b01110110, // U
    /* V */ 0b01111100, // V (approx)
    /* W */ 0b01111110, // W (approx)
    /* X */ 0b00111110, // X (looks like H)
    /* Y */ 0b01101110, // Y
    /* Z */ 0b01011101, // Z (same as 2)
};
// Macro to get mask for a given character. Only works with lower case values
// but that is fine as the display does mix upper/lower case anyways.
#define MASK_FOR_CHAR(c) (LETTER_MASK_LUT[(c) - 'a'])
// Horizontal bars at top/mid/bot position
static const uint8_t HBAR_MASK_LUT[3] = {
    0b00000001, // Top segment (idx 0)
    0b00001000, // Middle segment (idx 3)
    0b01000000, // Bottom segment (idx 6)
};

// State transitions are
// 0b00 0b01 0b11 0b10
// 0b00 means both blocked, 0b11 means both open.
// Thus (TODO: Check)
// left to right -> tape moving up
// right to left -> tape moving down
// Due to the sensor placement/hole pitch the 0b11 (both open) state is the
// shortest but still easily detectable.
// Three states would suffice to detect the direction but we can also use all
// four, but three is simpler I guess.
// Using three states also has the nice side effect of the state bit masks
// representing their index in the state sequence! So we don't need the
// sequence in a variable at all!
// static const uint8_t STATE_SEQ[4] = { 0b00, 0b01, 0b11, 0b10 };

typedef enum {
    COUNT,
    HPP_INPUT,
} uiState;

// Buttons pull low when pressed so invert the register values

#define BUTTON_UPPER_PRESSED (!(PORTB.IN & PIN4_bm))
#define BUTTON_LOWER_PRESSED (!(PORTB.IN & PIN3_bm))
#define BUTTON_UPPER_bm 0b01
#define BUTTON_LOWER_bm 0b10
#define BUTTON_STATE                                                                               \
    ((BUTTON_UPPER_PRESSED * BUTTON_UPPER_bm) | (BUTTON_LOWER_PRESSED * BUTTON_LOWER_bm))

#define BUTTON_UPPER_INTFLAG (PORTB.INTFLAGS & PIN4_bm)
#define BUTTON_LOWER_INTFLAG (PORTB.INTFLAGS & PIN3_bm)

// Firmware specific setup (pins, reference voltages, ...)
void setupPins();
void setupDacVref(VREF_DAC0REFSEL_t ref0, VREF_DAC1REFSEL_t ref1, VREF_DAC2REFSEL_t ref2);

// Digit display
void driveMask(uint8_t mask, const Pin *pins, const uint8_t nPins);
void blankDisplay();
void clearDigitBuffer(uint8_t *buffer);
void setDigitBufferInt(uint16_t val, bool hide_leading_zeros, uint8_t *buffer);
void displayACStates(uint8_t *ac_states, uint8_t *buffer);
void setDigitBufferIntMasked(uint16_t val, uint8_t digit_mask, bool hide_leading_zeros,
                             uint8_t *buffer);
void displayDigit(uint8_t mask, uint8_t dig);
void displayHPPValue(uint16_t val, uint8_t *buffer);
void displayErr(uint8_t *buffer);
void digitsMuxNext(uint8_t *segMasks);

// Timers
void tcaInterruptInit(TCA_t *timer, TCA_SINGLE_CLKSEL_t clksel, register16_t count);
void tcbInterruptInit(TCB_t *timer, TCB_CLKSEL_t clksel, uint16_t count);

// AC
void setDacRef(DAC_t *dac, register8_t val);
void acInterruptInit(AC_t *ac, AC_MUXPOS_t pos, AC_MUXNEG_t neg, AC_HYSMODE_t hysmode,
                     AC_INTMODE_t intmode);
bool getAcOutState(AC_t *ac);
