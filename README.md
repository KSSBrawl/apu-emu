# apu-emu
This is a program to demonstrate an emulator for the NES APU that I wrote. Included with this emulator is a partial C port of the PPMCK NSF driver as well as a ROM compatible with it to play back the song [Artificial Intelligence Bomb](https://www.youtube.com/watch?v=4gtGeZ2wOmo) by naruto2413.
# Building
### Windows
* Download msys from https://www.msys2.org/
```
pacman -S git gcc make mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image
git clone https://github.com/KSSBrawl/apu-emu.git
cd apu-emu
make
```
### Debian / Ubuntu
```
sudo apt install git libsdl2-dev libsdl2-image-dev
git clone https://github.com/KSSBrawl/apu-emu.git
cd apu-emu
make
```
# Build configuration
The following build options are available to pass to Make:
| Option           | Description                                        |
|------------------|----------------------------------------------------|
| DEBUG            | 1 = Debug build                                    |
| USE_MIXER_LOOKUP | 1 = Use lookup tables to approximate the APU mixer |
