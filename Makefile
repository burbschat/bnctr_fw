# Define variables that may change between projects
PORT = /dev/ttyUSB0
DEVICE = attiny1616
PROGRAMMER = serialupdi

# ------------------------------------------------------------------------------
# Preprocessor-values-passed-to-firmware

# Clock speed must be provided for delays to be timed properly 
F_CPU = 20000000
# Could reduce the CPU clock speed but it appeared that with 20 MHz it is
# almost impossible to skip a count but at 10 MHz pulling the tape rapidly can
# cause skipped counts.
# Also, dividing down the clock does not seem to significantly reduce power
# consumption at all, so 20 MHz is probably fine.
# PDIV = CLKCTRL_PDIV_16X_gc
# F_CPU = 1250000

# Time in ms to wait before illuminating the next digit
MUX_STEP_PERIOD_MS = 1
# Duty cycle divider for digit illumination (duty = 1/MUX_DUTY_DIV).
# This is a per-digit duty cycle as the digits are flashed one at a time.
MUX_DUTY_DIV = 4
# Duration in ms for a long button press event
BUTTON_LONGPRESS_TIMEOUT_MS = 500
# ------------------------------------------------------------------------------

# Baud rate for programming
BAUD = 115200

# Define sources
SRCS := main.c utils.c uart.c
# Obtain object file names from source file names
OBJS := $(SRCS:.c=.o)
OBJS_DEBUG := $(SRCS:.c=_debug.o)

# Compile with options:
# - All warnings (-Wall)
# - Optimized for size (-0s)
# - Targeted to our mcu (-mmcu)
# - Values for preprocessor (-D*)
# - Create dependency .d files for header files (-MMD -MP)
CC = avr-gcc 
CFLAGS = -Wall -Os \
		-mmcu=$(DEVICE) \
		-DF_CPU=$(F_CPU) \
		-DPDIV=$(if $(PDIV),$(PDIV),0) \
		-DMUX_STEP_PERIOD_MS=$(MUX_STEP_PERIOD_MS) \
		-DMUX_DUTY_DIV=$(MUX_DUTY_DIV) \
		-DBUTTON_LONGPRESS_TIMEOUT_MS=$(BUTTON_LONGPRESS_TIMEOUT_MS) \
		-MMD \
		-MP
# Extra flags for debug build:
# - Include debug symbols (-g)
# - No optimization (-O0, last one wins so -Os ignored)
CFLAGS_DEBUG = $(CFLAGS) -g -O0

default: main.hex

# Compile single source file
# Makefile is a dependency as well as it provides some preprocessor values
%.o: %.c Makefile
	$(CC) $(CFLAGS) -c $< -o $@

# Compile single source file with debug flags
%_debug.o: %.c
	$(CC) $(CFLAGS_DEBUG) -c $< -o $@

# Read .d files to resolve header dependencies
-include $(OBJS:.o=.d)
-include $(OBJS_DEBUG:.o=.d)

# Link objects
main.elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

main_debug.elf: $(OBJS_DEBUG)
	$(CC) $(CFLAGS_DEBUG) -o $@ $^

# Produce binary file for flashing
%.hex: %.elf
	avr-objcopy -O ihex $< $@

# List section sizes and total size of binary files
size_%: %.elf
	avr-size $<

# Size shorthands
size: size_main
size_debug: size_main_debug

# Flash the .hex file using avrdude
flash_%: %.hex
	avrdude -v -p $(DEVICE) -c $(PROGRAMMER) -P $(PORT) -b $(BAUD) -U flash:w:$<:i

# Flash shorthands
flash: flash_main
flash_debug: flash_main_debug

# Fuses are non-volatile settings that can be set by avrdude and persist between resets
# Setting fuse2 to 0x7e enables 20Mhz clock
# See page 34 of https://ww1.microchip.com/downloads/en/DeviceDoc/ATtiny1614-16-17-DataSheet-DS40002204A.pdf
fuse:
	avrdude -v -p $(DEVICE) -c $(PROGRAMMER) -P $(PORT) -b $(BAUD) -U fuse2:w:0x7e:m

clean:
	rm -f $(OBJS) $(OBJS_DEBUG) main.elf main.hex main_debug.elf main_debug.hex
