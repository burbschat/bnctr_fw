#pragma once
#include <avr/io.h>
#include <stdbool.h>
#include <stdint.h>

// This is the amount of dead time where we allow a digit to be on before
// moving on to the next. Ideally, we would never busy-wait like this. We're
// already polling the IR sensors in the downtime during count mode. This is
// just used to increase subjective display brightness in modes where nothing
// needs to be done between segment updates
#define SCAN_DELAY = 500;

// Define type to completely describe a pin (port and pin number)
typedef struct {
    PORT_t *port;
    uint8_t pin;
} Pin;

static inline void pinHigh(Pin p) { p.port->OUTSET = (1 << p.pin); }
static inline void pinLow(Pin p) { p.port->OUTCLR = (1 << p.pin); }
static inline void pinToggle(Pin p) { p.port->OUTTGL = (uint8_t)(1u << p.pin); }

// Define pins as assigned on the board
// Note that the GRN/YEL LED pins are also used as the AC output indicators so
// they can either be set manually or automatically reflect the AC states.
#define YEL_LED ((Pin){ &PORTB, 3 })
#define GRN_LED ((Pin){ &PORTA, 5 })
#define IR_LED ((Pin){ &PORTA, 4 })

// Use arrays for pins that we might want to index. Consequently indecies start
// at 0, not as 1 (as for the schematic), but start at 0 is nicer for the code
// so adopt that here.
#define N_DIGS 3
static const Pin DIGS[] = {
    { &PORTA, 2 },
    { &PORTA, 3 },
    { &PORTA, 1 },
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

// Firmware specific setup (pins, reference voltages, ...)
void setupPins();
void setupDacVref(VREF_DAC0REFSEL_t ref1, VREF_DAC1REFSEL_t ref2);

// Digit display
void driveMask(uint8_t mask, const Pin *pins, const uint8_t nPins);
void blankDisplay();
void clearDigitBuffer(uint8_t *buffer);
void setDigitBufferInt(uint16_t val, uint8_t *buffer);
void displayDigit(uint8_t mask, uint8_t dig);
void digitsMuxNext(uint8_t *segMasks);

// Timers
void tcaInterruptInit(TCA_t *timer, TCA_SINGLE_CLKSEL_t clksel, register16_t count);
void tcbInterruptInit(TCB_t *timer, TCB_CLKSEL_t clksel, uint16_t count);

// AC
void setDacRef(DAC_t *dac, register8_t val);
void acInterruptInit(AC_t *ac, AC_MUXPOS_t pos, AC_MUXNEG_t neg, AC_HYSMODE_t hysmode,
                     AC_INTMODE_t intmode, bool outen);
bool getAcOutState(AC_t *ac);
