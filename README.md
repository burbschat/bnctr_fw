# BNCTR FW
Firmware for my version of the [BeanCounter](https://github.com/NPoole/BeanCounter).
This is a ~port~ rewrite (*work in progress!*) of the original firmware by Nick
Poole. Not compatible with the original BeanCounter hardware, but rather my own
version of it (which steals all the good ideas from the original).

The original firmware had to use polling due to limitations of the used MCU but
this version can rely on interrupts thanks to the analog comparators of the
ATTiny1616. Thus, this firmware (together with the updated hardware) is
potentially more capable than the original.

Also, this firmware is *not* an Arduino sketch but rather written in plain C
and build using the vendor (AVR) provided toolchain.

## Toolchain Setup
### Arch Linux
* Compiling: `avr-gcc avr-libc`
* Flashing: `avrdude`
* Debugging: `avr-gdb`

### Debian-like
Didn't try but probably
* Compiling: `gcc-avr avr-libc binutils-avr`
* Flashing: `avrdude`
* Debugging: `gdb-avr`

## LSP
I use NeoVim with LSP and to get the `compile_commands.json` which lets the
language server know about the used sources etc. I use
[`bear`](https://github.com/rizsotto/Bear).
Might work the same for other editors using LSP.

## Debug
Make sure to compile with `-g` (add debug symbols) and `-O0` (no optimizations)
for debugging (or expect stuff to be optimized away).
The makefile contains debug targets for that purpose. For example `make
flash_debug` to build and flash the image with debug symbols. Note that a debug
image will be much larger (have a look at `make size_debug`).

Using [`avr-absurd`](https://github.com/feline-felicity/avr-absurd). Exactly
what I was looking for for this use case (UPDI through serial to USB bridge).
Many thanks to the developer.
```sh
avr-absurd -P /dev/ttyUSB0 -b 115200
avr-gdb main_debug.elf
```
In gdb, connect to the server on localhost port 3333:
```
target extended-remote :3333
```
Use gbd as usual. Apparently the attiny1616 only supports up to two breakpoints
so keep that in mind I suppose.

`avr-absurd` obviously occupies the serial port. To re-compile and flash
without stopping `avr-absurt` consider re-compiling with `make main_debug.elf`
and then use the `load` command in gdb to flash to the device (ridiculously
useful). This also reloads the debug info like code listings (gdb `list`
command).

## References
* General and programming: https://michael-crum.com/attiny1616/
* Interrupts: https://www.ojisanseiuchi.com/2024/03/03/a-quick-word-on-attiny-1-series-interrupts/
* A type timers: https://www.microchip.com/en-us/application-notes/tb3217
* B type timers: https://www.microchip.com/en-us/application-notes/tb3214
* Analog comparator (AC): https://www.microchip.com/en-us/application-notes/tb3211
