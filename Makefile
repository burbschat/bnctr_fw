# Define variables that may change between projects
PORT = /dev/ttyUSB0
DEVICE = attiny1616
PROGRAMMER = serialupdi

# Clock speed must be provided for delays to be timed properly 
CLOCK = 20000000

# Baud rate for programming
BAUD = 115200

# Define sources
SRCS := main.c uart.c
# Obtain object file names from source file names
OBJS := $(SRCS:.c=.o)

# Compile with options:
# - All warnings (-Wall)
# - Optimized for size (-0s)
# - Targeted to our mcu (-mmcu)
# - F_CPU defined as our clock speed (-DF_CPU)
# - Create dependency .d files for header files (-MMD -MP)
CC = avr-gcc 
CFLAGS = -Wall -Os -mmcu=$(DEVICE) -DF_CPU=$(CLOCK) -MMD -MP

default: main.hex

# Compile single source file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Read .d files to resolve header dependencies
-include $(OBJS:.o=.d)

# Link objects
main.elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Produce binary file for flashing
main.hex: main.elf
	avr-objcopy -O ihex $< $@

# List section sizes and total size of binary files
size: main.elf
	avr-size $<

# Flash the .hex file using avrdude
flash: main.elf
	avrdude -v -p $(DEVICE) -c $(PROGRAMMER) -P $(PORT) -b $(BAUD) -U flash:w:main.hex:i

# Fuses are non-volatile settings that can be set by avrdude and persist between resets
# Setting fuse2 to 0x7e enables 20Mhz clock
# See page 34 of https://ww1.microchip.com/downloads/en/DeviceDoc/ATtiny1614-16-17-DataSheet-DS40002204A.pdf
fuse:
	avrdude -v -p $(DEVICE) -c $(PROGRAMMER) -P $(PORT) -b $(BAUD) -U fuse2:w:0x7e:m

clean:
	rm $(OBJS)
	rm main.elf
	rm main.hex
