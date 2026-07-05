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

## References
* https://michael-crum.com/attiny1616/
