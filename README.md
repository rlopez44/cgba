# CGBA
First attempt at a Game Boy Advance emulator.

# Building the Emulator
There are two builds of the emulator available:
* A release build created by running `make`
* A debug build created by running `make debug`

# Running the Emulator
The emulator is invoked as follows:

    cgba <romfile>

# Installing the Emulator
You can install the emulator to `/usr/local/bin` using
`make install`.

# Requirements
SDL2 is required to build the emulator. You can
install SDL2 as follows:
* Arch Linux: `pacman -S sdl2`
* Debian/Ubuntu: `apt install libsdl2-dev`
* MacOS (Homebrew): `brew install sdl2`

# References
* [*Decoding the ARM7TDMI Instruction Set (Game Boy Advance)*](https://www.gregorygaines.com/blog/decoding-the-arm7tdmi-instruction-set-game-boy-advance/)
* ARM7TDMI Data Sheet
* [Tonc](https://www.coranac.com/tonc/text/toc.htm)
* [GBATEK](http://problemkaputt.de/gbatek.htm)
* [GBA Tests](https://github.com/jsmolka/gba-tests)
